#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace rfx;

static const char* longName(int i) {
  static const char* n[NumParams] = {
    "Osc 1 Wave","Osc 2 Wave","Osc 3 Wave","Fat Amount","Osc 1 Semi","Osc 2 Semi","Osc 3 Semi","Cutoff","Resonance","Filter Type",
    "Env1 Attack","Env1 Decay","Env1 Sustain","Env1 Release","Env1 > Volume","Env1 > PWM","Env1 > Cutoff","Env1 > Detune",
    "Env2 Attack","Env2 Decay","Env2 Sustain","Env2 Release","Env2 > Reso","Env2 > PWM","Env2 > Cutoff","Env2 > Pitch",
    "LFO1 Rate","LFO1 > Detune","LFO1 > PWM","LFO1 > Cutoff","LFO1 Retrig","LFO2 Rate","LFO2 > Detune","LFO2 > PWM","LFO2 > Cutoff","LFO2 Retrig",
    "LFO3 Rate","LFO3 > Detune","LFO3 > PWM","LFO3 > Cutoff","LFO3 Retrig","Delay Mix","Delay Time","Delay Feedback","Delay Damp",
    "Arp On","Arp Retrig","Arp Mode","Arp Speed","Arp Gate","Arp Octaves","Glide On","Glide Time","Bend Range","Vibrato Rate","Voices","Vibrato Depth","Reserved 1",
    "Drive","Volume","Pan","Spread","Gate Contour","Gate Speed","Gate Pattern","Reserved Pat2","Gate Stereo","Reserved 2",
    "Vel > Cutoff","Key > Cutoff","Vel > Volume","Gate On","Gate Mix","Delay On","Delay Mode","Reverb On","Reverb Mix","Reverb Predelay","Reverb Room","Reverb Damp","Reverb Width",
    "Osc Retrig","Osc 1 Level","Osc 2 Level","Osc 3 Level",
    "Master Gain","Reserved 7","Reserved 8","Reserved 9","Reserved 10","Reserved 11","Reserved 12","Mono","Reserved 14","Reserved 15","Reserved 16","Reserved 17","Classic EQ","Reserved 19","Reserved 20",
    "Fat Voices","Osc Drift" };
  return n[i];
}

juce::AudioProcessorValueTreeState::ParameterLayout ReflexProcessor::createLayout() {
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
  float init[NumParams]; initPatch(init);
  for (int i = 0; i < NumParams; ++i) {
    auto attrs = juce::AudioParameterFloatAttributes()
      .withStringFromValueFunction([i](float v, int) { return juce::String(displayText(i, v)); })
      .withAutomatable(!isHidden(i) || i == dummy6 || i == dummy13 || i == dummy18);
    ps.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { paramID(i), 1 }, longName(i),
                                                             juce::NormalisableRange<float>(0.f, 1.f), init[i], attrs));
  }
  return { ps.begin(), ps.end() };
}

ReflexProcessor::ReflexProcessor()
  : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "REFLEX", createLayout()) {
  for (int i = 0; i < NumParams; ++i) raw[i] = apvts.getRawParameterValue(paramID(i));
  Program init; init.name = "Init";
  float v[NumParams]; initPatch(v);
  for (int i = 0; i < NumParams; ++i) init.values[i] = v[i];
  programs.push_back(init);
  currentPresetName = "Init";
}

bool ReflexProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ReflexProcessor::prepareToPlay(double sampleRate, int) {
  // the engine is calibrated at 44.1 kHz; other host rates run it at 44.1 kHz and resample
  useAdapter = split != 2 && std::fabs(sampleRate - RateAdapter::InternalRate) > 0.5;   // the effects alone run at the host rate
  if (useAdapter) { adapter.prepare(engine, sampleRate); setLatencySamples(adapter.latencySamples()); }
  else { engine.setSampleRate(sampleRate); setLatencySamples(0); }
}

void ReflexProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  for (int i = 0; i < NumParams; ++i) engine.p[i] = raw[i]->load();
  bool playing = false, ppqValid = false; double ppq = 0.0, bpmNow = lastBpm;
  if (auto* ph = getPlayHead()) {
    if (auto pos = ph->getPosition()) {
      if (auto bpm = pos->getBpm()) { bpmNow = *bpm; lastBpm = *bpm; }
      playing = pos->getIsPlaying();
      if (auto q = pos->getPpqPosition()) { ppq = *q; ppqValid = true; }
    }
  }
  if (tempoOverride > 1.0) bpmNow = tempoOverride;
  engine.split = split;
  if (split == 2) {                       // the effects alone, on the buffer's signal
    engine.setTempo(bpmNow);
    const int ne = buffer.getNumSamples();
    float* EL = buffer.getWritePointer(0);
    float* ER = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : EL;
    engine.processEffectsOnly(EL, ER, ne);
    midi.clear();
    return;
  }
  if (!useAdapter) {
    engine.setTempo(bpmNow);
    if (ppqValid) engine.setTransport(playing, ppq);
  }
  events.clear();
  for (const auto meta : midi) {
    auto m = meta.getMessage();
    if (m.getRawDataSize() < 1 || m.getRawDataSize() > 3) continue;
    const auto* d = m.getRawData();
    rfx::MidiEv e; e.frame = meta.samplePosition; e.b0 = d[0];
    e.b1 = m.getRawDataSize() > 1 ? d[1] : 0; e.b2 = m.getRawDataSize() > 2 ? d[2] : 0;
    events.push_back(e);
  }
  const int n = buffer.getNumSamples();
  float* L = buffer.getWritePointer(0);
  float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;
  if (useAdapter) adapter.process(L, R, n, events, playing, ppqValid, ppq, bpmNow);
  else engine.processBlock(L, R, n, events.data(), (int)events.size());
  midi.clear();
}

void ReflexProcessor::applyValues(const std::array<float, NumParams>& v) {
  for (int i = 0; i < NumParams; ++i)
    if (auto* p = apvts.getParameter(paramID(i))) p->setValueNotifyingHost(v[i]);
}

std::array<float, NumParams> ReflexProcessor::currentValues() const {
  std::array<float, NumParams> v {};
  for (int i = 0; i < NumParams; ++i) v[i] = raw[i]->load();
  return v;
}

void ReflexProcessor::setCurrentProgram(int index) {
  if (index < 0 || index >= (int)programs.size()) return;
  currentProgram = index;
  currentPresetName = programs[index].name;
  applyValues(programs[index].values);
}

const juce::String ReflexProcessor::getProgramName(int index) {
  return (index >= 0 && index < (int)programs.size()) ? programs[index].name : juce::String();
}

void ReflexProcessor::changeProgramName(int index, const juce::String& newName) {
  if (index >= 0 && index < (int)programs.size()) programs[index].name = newName;
}

// ---------------- XML ----------------
std::unique_ptr<juce::XmlElement> ReflexProcessor::valuesToXml(const juce::String& name, const std::array<float, NumParams>& v) const {
  auto e = std::make_unique<juce::XmlElement>("Preset");
  e->setAttribute("name", name);
  for (int i = 0; i < NumParams; ++i) {
    auto* p = e->createNewChildElement("Param");
    p->setAttribute("index", i);
    p->setAttribute("id", shortName(i));
    p->setAttribute("value", juce::String(v[i], 9));
  }
  return e;
}

bool ReflexProcessor::xmlToValues(const juce::XmlElement& e, std::array<float, NumParams>& v, juce::String& name) {
  if (!e.hasTagName("Preset")) return false;
  float init[NumParams]; initPatch(init);
  for (int i = 0; i < NumParams; ++i) v[i] = init[i];
  name = e.getStringAttribute("name", "Untitled");
  for (auto* p : e.getChildIterator()) {
    if (!p->hasTagName("Param")) continue;
    int idx = p->getIntAttribute("index", -1);
    if (idx < 0 || idx >= NumParams) {          // fall back to matching by id
      auto id = p->getStringAttribute("id");
      for (int i = 0; i < NumParams; ++i) if (id == shortName(i)) { idx = i; break; }
    }
    if (idx >= 0 && idx < NumParams) v[idx] = juce::jlimit(0.f, 1.f, (float)p->getDoubleAttribute("value"));
  }
  return true;
}

bool ReflexProcessor::saveXmlPreset(const juce::File& f) {
  auto x = valuesToXml(currentPresetName.isEmpty() ? f.getFileNameWithoutExtension() : currentPresetName, currentValues());
  return x->writeTo(f);
}

bool ReflexProcessor::saveXmlBank(const juce::File& f) {
  storeCurrentIntoProgram();
  juce::XmlElement bank("Bank");
  for (auto& p : programs) bank.addChildElement(valuesToXml(p.name, p.values).release());
  return bank.writeTo(f);
}

bool ReflexProcessor::loadXmlPreset(const juce::File& f, juce::String& error) {
  auto x = juce::XmlDocument::parse(f);
  if (!x) { error = "Not a valid XML file"; return false; }
  if (x->hasTagName("Bank")) {
    std::vector<Program> ps;
    for (auto* c : x->getChildIterator()) { Program p; if (xmlToValues(*c, p.values, p.name)) ps.push_back(p); }
    if (ps.empty()) { error = "Bank contains no presets"; return false; }
    programs = ps; setCurrentProgram(0);
    if (onPresetListChanged) onPresetListChanged();
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return true;
  }
  Program p;
  if (!xmlToValues(*x, p.values, p.name)) { error = "XML is not a Reflex preset"; return false; }
  currentPresetName = p.name;
  applyValues(p.values);
  return true;
}

// ---------------- FXB / FXP (VST2 bank/program files, parameter or chunk form) ----------------
static uint32_t be32(const uint8_t* d) { return (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]; }
static float bef(const uint8_t* d) { uint32_t u = be32(d); float f; std::memcpy(&f, &u, 4); return f; }

bool ReflexProcessor::loadFxbOrFxp(const juce::File& f, juce::String& error) {
  juce::MemoryBlock mb;
  if (!f.loadFileAsData(mb) || mb.getSize() < 60) { error = "Could not read file"; return false; }
  const uint8_t* d = (const uint8_t*)mb.getData();
  const size_t sz = mb.getSize();
  if (std::memcmp(d, "CcnK", 4) != 0) { error = "Not an FXB/FXP file"; return false; }
  auto readProgram = [&](size_t off, Program& p) -> size_t {   // returns bytes consumed, 0 on error
    if (off + 56 > sz || std::memcmp(d + off, "CcnK", 4) != 0) return 0;
    if (std::memcmp(d + off + 8, "FxCk", 4) != 0) return 0;
    int np = (int)be32(d + off + 24);
    char nm[29] = { 0 }; std::memcpy(nm, d + off + 28, 28);
    p.name = juce::String(juce::CharPointer_ASCII(nm)).trim();
    float init[NumParams]; initPatch(init);
    for (int i = 0; i < NumParams; ++i) p.values[i] = init[i];
    if (off + 56 + (size_t)np * 4 > sz) return 0;
    for (int i = 0; i < np && i < NumParams; ++i) p.values[i] = juce::jlimit(0.f, 1.f, bef(d + off + 56 + 4 * i));
    return 56 + (size_t)np * 4;
  };
  if (std::memcmp(d + 8, "FxCk", 4) == 0) {            // single program
    Program p;
    if (!readProgram(0, p)) { error = "Corrupt FXP"; return false; }
    currentPresetName = p.name; applyValues(p.values);
    return true;
  }
  if (std::memcmp(d + 8, "FxBk", 4) == 0) {            // bank of programs
    int n = (int)be32(d + 24);
    size_t off = 156;
    std::vector<Program> ps;
    for (int i = 0; i < n; ++i) {
      Program p; size_t used = readProgram(off, p);
      if (!used) break;
      ps.push_back(p); off += used;
    }
    if (ps.empty()) { error = "Bank has no readable programs"; return false; }
    programs = ps;
    setCurrentProgram(0);
    if (onPresetListChanged) onPresetListChanged();
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
    return true;
  }
  if (std::memcmp(d + 8, "FBCh", 4) == 0 || std::memcmp(d + 8, "FPCh", 4) == 0) {
    // chunk form as written by hosts for the original: header + chunk size + opaque chunk
    const bool bank = std::memcmp(d + 8, "FBCh", 4) == 0;
    const size_t hdr = bank ? 160 : 60;
    if (sz < hdr) { error = "Corrupt chunk file"; return false; }
    const size_t csz = be32(d + hdr - 4);
    if (hdr + csz > sz) { error = "Corrupt chunk file"; return false; }
    return loadOriginalChunk(d + hdr, csz, !bank, error);
  }
  error = "Unknown FX file type"; return false;
}

// The original's own chunk: int32 LE numPrograms, numParams, current program;
// then numPrograms x numParams float32 LE values, then numPrograms x 32-byte names.
static uint32_t le32(const uint8_t* d) { return (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24; }
static float lef(const uint8_t* d) { uint32_t u = le32(d); float f; std::memcpy(&f, &u, 4); return f; }

bool ReflexProcessor::loadOriginalChunk(const uint8_t* d, size_t sz, bool singleProgram, juce::String& error) {
  if (sz < 12) { error = "Chunk too small"; return false; }
  const int np = (int)le32(d), npar = (int)le32(d + 4), cur = (int)le32(d + 8);
  if (np <= 0 || np > 4096 || npar <= 0 || npar > 4096 || 12 + (size_t)np * ((size_t)npar * 4 + 32) > sz) { error = "Unrecognised chunk layout"; return false; }
  const uint8_t* vals = d + 12;
  const uint8_t* names = vals + (size_t)np * npar * 4;
  auto prog = [&](int i) {
    Program p; float init[NumParams]; initPatch(init);
    for (int k = 0; k < NumParams; ++k) p.values[k] = init[k];
    for (int k = 0; k < npar && k < NumParams; ++k) p.values[k] = juce::jlimit(0.f, 1.f, lef(vals + ((size_t)i * npar + k) * 4));
    char nm[33] = { 0 }; std::memcpy(nm, names + (size_t)i * 32, 32);
    p.name = juce::String(juce::CharPointer_ASCII(nm)).trim();
    return p;
  };
  if (singleProgram) {
    Program p = prog(juce::jlimit(0, np - 1, cur));
    currentPresetName = p.name; applyValues(p.values);
    return true;
  }
  std::vector<Program> ps;
  for (int i = 0; i < np; ++i) ps.push_back(prog(i));
  programs = ps;
  setCurrentProgram(juce::jlimit(0, np - 1, cur));
  if (onPresetListChanged) onPresetListChanged();
  updateHostDisplay(ChangeDetails().withProgramChanged(true));
  return true;
}

// ---------------- export in the original's formats (plain-parameter FXP / FXB, fxID 'VGRD') ----------------
static void putBE32(juce::MemoryOutputStream& o, uint32_t v) { o.writeByte((char)(v >> 24)); o.writeByte((char)(v >> 16)); o.writeByte((char)(v >> 8)); o.writeByte((char)v); }
static void putBEf(juce::MemoryOutputStream& o, float f) { uint32_t u; std::memcpy(&u, &f, 4); putBE32(o, u); }
static void putTag(juce::MemoryOutputStream& o, const char* t) { o.write(t, 4); }

static void writeFxProgram(juce::MemoryOutputStream& o, const juce::String& name, const std::array<float, NumParams>& v) {
  putTag(o, "CcnK"); putBE32(o, 48 + NumParams * 4);           // byte size after this field
  putTag(o, "FxCk"); putBE32(o, 1); putTag(o, "VGRD"); putBE32(o, 1); putBE32(o, NumParams);
  char nm[28] = { 0 }; name.copyToUTF8(nm, 28); nm[27] = 0; o.write(nm, 28);
  for (int i = 0; i < NumParams; ++i) putBEf(o, v[(size_t)i]);
}

void ReflexProcessor::storeCurrentIntoProgram() {
  if (currentProgram >= 0 && currentProgram < (int)programs.size()) {
    programs[(size_t)currentProgram].values = currentValues();
    if (currentPresetName.isNotEmpty()) programs[(size_t)currentProgram].name = currentPresetName;
  }
}

bool ReflexProcessor::saveFxp(const juce::File& f) {
  juce::MemoryOutputStream o;
  writeFxProgram(o, currentPresetName.isEmpty() ? f.getFileNameWithoutExtension() : currentPresetName, currentValues());
  return f.replaceWithData(o.getData(), o.getDataSize());
}

bool ReflexProcessor::saveFxb(const juce::File& f) {
  storeCurrentIntoProgram();
  juce::MemoryOutputStream o;
  const uint32_t progSize = 56 + NumParams * 4;
  putTag(o, "CcnK"); putBE32(o, (uint32_t)(148 + programs.size() * progSize));
  putTag(o, "FxBk"); putBE32(o, 1); putTag(o, "VGRD"); putBE32(o, 1); putBE32(o, (uint32_t)programs.size());
  for (int i = 0; i < 128; ++i) o.writeByte(0);                 // reserved (version 1 layout, as in the factory banks)
  for (auto& p : programs) writeFxProgram(o, p.name, p.values);
  return f.replaceWithData(o.getData(), o.getDataSize());
}

// ---------------- host state ----------------
void ReflexProcessor::getStateInformation(juce::MemoryBlock& destData) {
  juce::XmlElement root("ReflexState");
  root.setAttribute("version", 1);
  root.setAttribute("current", currentProgram);
  root.setAttribute("uiScale", (double)uiScale);
  root.setAttribute("uiTheme", uiTheme);
  root.setAttribute("randomMenu", randomMenuIndex);
  root.setAttribute("randomVariation", (double)randomVariation);
  root.addChildElement(valuesToXml(currentPresetName, currentValues()).release());
  auto* bank = root.createNewChildElement("Bank");
  for (auto& p : programs) bank->addChildElement(valuesToXml(p.name, p.values).release());
  copyXmlToBinary(root, destData);
}

void ReflexProcessor::setStateInformation(const void* data, int sizeInBytes) {
  auto x = getXmlFromBinary(data, sizeInBytes);
  if (!x || !(x->hasTagName("ReflexState") || x->hasTagName("OutriderState"))) return;   // also accept states saved by the preview builds
  if (auto* bank = x->getChildByName("Bank")) {
    std::vector<Program> ps;
    for (auto* c : bank->getChildIterator()) { Program p; if (xmlToValues(*c, p.values, p.name)) ps.push_back(p); }
    if (!ps.empty()) programs = ps;
  }
  currentProgram = juce::jlimit(0, (int)programs.size() - 1, x->getIntAttribute("current", 0));
  uiScale = (float)juce::jlimit(0.5, 2.0, x->getDoubleAttribute("uiScale", 0.8));
  uiTheme = juce::jlimit(0, 1, x->getIntAttribute("uiTheme", 0));
  randomMenuIndex = juce::jlimit(0, (int)rfx::randomizer::menu().size() - 1, x->getIntAttribute("randomMenu", 0));
  randomVariation = (float)juce::jlimit(0.0, 1.0, x->getDoubleAttribute("randomVariation", 0.35));
  if (auto* cur = x->getChildByName("Preset")) {
    std::array<float, NumParams> v; juce::String nm;
    if (xmlToValues(*cur, v, nm)) { currentPresetName = nm; applyValues(v); }
  }
  if (onPresetListChanged) onPresetListChanged();
}

void ReflexProcessor::randomize(int menuIndex, float variation) {
  randomMenuIndex = menuIndex; randomVariation = variation;
  std::string name;
  auto v = rfx::randomizer::generate(menuIndex, currentValues(), variation, rng, name);
  if (name.empty()) {   // mutation keeps the name and marks it as changed
    if (!currentPresetName.endsWith("*")) currentPresetName = currentPresetName + " *";
  } else currentPresetName = juce::String(name);
  applyValues(v);
  if (onPresetListChanged) onPresetListChanged();
}

juce::AudioProcessorEditor* ReflexProcessor::createEditor() { return new ReflexEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ReflexProcessor(); }
