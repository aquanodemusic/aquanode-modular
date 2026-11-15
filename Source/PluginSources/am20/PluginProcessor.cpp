#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Factory.h"

using namespace am20;

juce::AudioProcessorValueTreeState::ParameterLayout AM20Processor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    for (int i = 0; i < NumParams; ++i)
    {
        auto attrs = juce::AudioParameterFloatAttributes()
                         .withStringFromValueFunction ([i] (float v, int) { return juce::String (displayText (i, v)); });
        ps.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (i), 1 }, paramName (i),
                                                                   juce::NormalisableRange<float> (0.0f, 1.0f), defaultValue (i), attrs));
    }
    return { ps.begin(), ps.end() };
}

AM20Processor::AM20Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AM20", createLayout())
{
    for (int i = 0; i < NumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (pid (i));
    loadFactoryBank();
    // the start-up sound
    currentProgram = 0;
    for (int i = 0; i < (int) bank.size(); ++i) if (bank[(size_t) i].name == "Vaporizing Pad") { currentProgram = i; break; }
    if (! bank.empty()) applyPreset (bank[(size_t) currentProgram]);
}

void AM20Processor::loadFactoryBank()
{
    bank = factory::presets();
    bankName = "AM-20";
}

bool AM20Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AM20Processor::prepareToPlay (double sampleRate, int)
{
    engine.oversample = highQuality.load();
    engine.setSampleRate (sampleRate);
    cableVersionSeen = -1;
}

void AM20Processor::cablesChanged()
{
    {
        const juce::SpinLock::ScopedLockType sl (cableLock);
        cablesForAudio = cables;
    }
    ++cableVersion;
    if (onChange) onChange();
}

bool AM20Processor::addCable (int a, int b, juce::String& why)
{
    Cable c;
    if (! orientCable (a, b, c)) { why = "A cable joins an output (OUT) to an input (IN)."; return false; }
    for (auto& e : cables)
    {
        Cable o; orientCable (e.first, e.second, o);
        if (o.out == c.out && o.in == c.in) { why = "Those two jacks are joined already."; return false; }
    }
    if ((int) cables.size() >= presetio::MaxCables) { why = "The patch panel holds 17 cables."; return false; }
    cables.push_back ({ c.out, c.in });
    cablesChanged();
    return true;
}

void AM20Processor::removeCable (int index)
{
    if (index < 0 || index >= (int) cables.size()) return;
    cables.erase (cables.begin() + index);
    cablesChanged();
}

void AM20Processor::clearCables() { cables.clear(); cablesChanged(); }

void AM20Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (int i = 0; i < NumParams; ++i) engine.p[i] = raw[(size_t) i]->load();
    if (cableVersion.load() != cableVersionSeen)
    {
        const juce::SpinLock::ScopedTryLockType sl (cableLock);
        if (sl.isLocked()) { engine.setCables (cablesForAudio); cableVersionSeen = cableVersion.load(); }
    }
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) engine.setTempo (*bpm);
    engine.setOversampling (highQuality.load());
    engine.setMomentary (momentary.load());
    const float w = guiWheel.load();
    if (w >= 0.0f) engine.setWheel (w);
    else if (lastGuiWheel >= 0.0f) engine.setWheel (lastGuiWheel);     // the wheel stays where it was let go
    if (w >= 0.0f) lastGuiWheel = w;

    events.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.getRawDataSize() < 1 || m.getRawDataSize() > 3) continue;
        const auto* d = m.getRawData();
        MidiEv e; e.frame = meta.samplePosition; e.b0 = d[0];
        e.b1 = m.getRawDataSize() > 1 ? d[1] : 0; e.b2 = m.getRawDataSize() > 2 ? d[2] : 0;
        events.push_back (e);
    }
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
    engine.processBlock (L, R, n, events.data(), (int) events.size());
    midi.clear();
}

// ---------------------------------------------------------------- presets
Preset AM20Processor::currentPreset() const
{
    Preset p;
    p.name = presetName.toStdString();
    for (int i = 0; i < NumParams; ++i) p.values[i] = raw[(size_t) i]->load();
    p.cables = cables;
    p.fx = currentFx;
    return p;
}

void AM20Processor::applyPreset (const Preset& p)
{
    for (int i = 0; i < NumParams; ++i)
        if (auto* prm = apvts.getParameter (pid (i))) prm->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p.values[i]));
    presetName = juce::String (p.name);
    currentFx = p.fx;
    // keep only cables that join an output to an input
    cables.clear();
    for (auto& c : p.cables) { Cable o; if (orientCable (c.first, c.second, o)) cables.push_back ({ o.out, o.in }); }
    cablesChanged();
}

void AM20Processor::setCurrentProgram (int index)
{
    if (index < 0 || index >= (int) bank.size()) return;
    currentProgram = index;
    applyPreset (bank[(size_t) index]);
}

const juce::String AM20Processor::getProgramName (int index)
{
    return (index >= 0 && index < (int) bank.size()) ? juce::String (bank[(size_t) index].name) : juce::String();
}

void AM20Processor::changeProgramName (int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) bank.size()) bank[(size_t) index].name = newName.toStdString();
}

std::unique_ptr<juce::XmlElement> AM20Processor::presetToXml (const Preset& p, bool withFx)
{
    auto e = std::make_unique<juce::XmlElement> ("AM20Preset");
    e->setAttribute ("name", juce::String (p.name));
    e->setAttribute ("version", 1);
    for (int i = 0; i < NumParams; ++i)
    {
        auto* x = e->createNewChildElement ("Param");
        x->setAttribute ("id", paramId (i));
        x->setAttribute ("value", juce::String (p.values[i], 6));
        x->setAttribute ("shows", juce::String (displayText (i, p.values[i])));   // for people reading the file
    }
    for (auto& c : p.cables)
    {
        auto* x = e->createNewChildElement ("Cable");
        Cable o; orientCable (c.first, c.second, o);
        x->setAttribute ("out", jackName (o.out));
        x->setAttribute ("in", jackName (o.in));
        x->setAttribute ("outJack", o.out);
        x->setAttribute ("inJack", o.in);
    }
    if (withFx && ! p.fx.empty())
        e->setAttribute ("originalEffects", juce::Base64::toBase64 (p.fx.data(), p.fx.size()));
    return e;
}

bool AM20Processor::xmlToPreset (const juce::XmlElement& e, Preset& p, bool withFx)
{
    if (! e.hasTagName ("AM20Preset")) return false;
    p = Preset();
    p.name = e.getStringAttribute ("name", "Untitled").toStdString();
    for (auto* x : e.getChildWithTagNameIterator ("Param"))
    {
        const auto id = x->getStringAttribute ("id");
        for (int i = 0; i < NumParams; ++i)
            if (id == paramId (i)) { p.values[i] = juce::jlimit (0.0f, 1.0f, (float) x->getDoubleAttribute ("value", defaultValue (i))); break; }
    }
    for (auto* x : e.getChildWithTagNameIterator ("Cable"))
    {
        int a = x->getIntAttribute ("outJack", -1), b = x->getIntAttribute ("inJack", -1);
        if (a < 0 || b < 0)     // hand-written files may give only the names
            for (int j = 0; j < NumJacks; ++j)
            {
                if (x->getStringAttribute ("out") == jackName (j)) a = j;
                if (x->getStringAttribute ("in") == jackName (j)) b = j;
            }
        Cable o; if (orientCable (a, b, o)) p.cables.push_back ({ o.out, o.in });
    }
    if (withFx && e.hasAttribute ("originalEffects"))
    {
        juce::MemoryOutputStream m;
        if (juce::Base64::convertFromBase64 (m, e.getStringAttribute ("originalEffects")) && m.getDataSize() == presetio::FxSize)
        {
            auto* b = static_cast<const uint8_t*> (m.getData());
            p.fx.assign (b, b + m.getDataSize());
        }
    }
    return true;
}

static juce::MemoryBlock xmlBytes (const juce::XmlElement& x)
{
    const auto s = x.toString();
    return juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

juce::MemoryBlock AM20Processor::presetAsAm20() const { return xmlBytes (*presetToXml (currentPreset())); }

juce::MemoryBlock AM20Processor::bankAsAm20() const
{
    juce::XmlElement b ("AM20Bank");
    b.setAttribute ("name", bankName);
    for (auto& p : bank) b.addChildElement (presetToXml (p).release());
    return xmlBytes (b);
}

juce::MemoryBlock AM20Processor::presetAsFxp() const
{
    const auto v = presetio::writeFxp (currentPreset());
    return juce::MemoryBlock (v.data(), v.size());
}

juce::MemoryBlock AM20Processor::bankAsFxb() const
{
    const auto v = presetio::writeFxb (bank, currentProgram);
    return juce::MemoryBlock (v.data(), v.size());
}

bool AM20Processor::loadPresetData (const juce::MemoryBlock& data, const juce::String& fileName, juce::String& error)
{
    const auto* d = static_cast<const uint8_t*> (data.getData());
    const size_t sz = data.getSize();
    if (sz < 4) { error = "The file is empty."; return false; }
    std::vector<Preset> ps;
    const bool original = std::memcmp (d, "CcnK", 4) == 0 || std::memcmp (d, "gorp", 4) == 0 || std::memcmp (d, "prog", 4) == 0;
    if (original)
    {
        std::string err;
        ps = presetio::readFile (d, sz, err);
        if (ps.empty()) { error = juce::String (err); return false; }
    }
    else
    {
        auto x = juce::XmlDocument::parse (data.toString());
        if (x == nullptr) { error = "This is neither an AM-20 preset nor a file of the original."; return false; }
        if (x->hasTagName ("AM20Bank"))
            for (auto* c : x->getChildWithTagNameIterator ("AM20Preset")) { Preset p; if (xmlToPreset (*c, p)) ps.push_back (p); }
        else { Preset p; if (xmlToPreset (*x, p)) ps.push_back (p); }
        if (ps.empty()) { error = "No AM-20 preset in this file."; return false; }
    }
    if (ps.size() > 1)
    {
        bank = ps;
        bankName = juce::File::createFileWithoutCheckingPath (fileName).getFileNameWithoutExtension();
        if (bankName.isEmpty()) bankName = "Bank";
        currentProgram = 0;
        applyPreset (bank[0]);
    }
    else
    {
        applyPreset (ps[0]);
        if (presetName.isEmpty()) presetName = juce::File::createFileWithoutCheckingPath (fileName).getFileNameWithoutExtension();
    }
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return true;
}

// ---------------------------------------------------------------- host state
void AM20Processor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("AM20State");
    root.setAttribute ("version", 1);
    root.setAttribute ("current", currentProgram);
    root.setAttribute ("uiScale", (double) uiScale);
    root.setAttribute ("bankName", bankName);
    root.setAttribute ("highQuality", highQuality.load());
    root.addChildElement (presetToXml (currentPreset(), true).release());
    auto* b = root.createNewChildElement ("AM20Bank");
    for (auto& p : bank) b->addChildElement (presetToXml (p, true).release());
    copyXmlToBinary (root, destData);
}

void AM20Processor::setStateInformation (const void* data, int sizeInBytes)
{
    auto x = getXmlFromBinary (data, sizeInBytes);
    if (x == nullptr || ! x->hasTagName ("AM20State")) return;
    if (auto* b = x->getChildByName ("AM20Bank"))
    {
        std::vector<Preset> ps;
        for (auto* c : b->getChildWithTagNameIterator ("AM20Preset")) { Preset p; if (xmlToPreset (*c, p, true)) ps.push_back (p); }
        if (! ps.empty()) bank = ps;
    }
    bankName = x->getStringAttribute ("bankName", "AM-20");
    currentProgram = juce::jlimit (0, juce::jmax (0, (int) bank.size() - 1), x->getIntAttribute ("current", 0));
    uiScale = (float) juce::jlimit (0.5, 2.0, x->getDoubleAttribute ("uiScale", 1.0));
    highQuality = x->getBoolAttribute ("highQuality", highQuality.load());
    if (auto* c = x->getChildByName ("AM20Preset")) { Preset p; if (xmlToPreset (*c, p, true)) applyPreset (p); }
}

juce::AudioProcessorEditor* AM20Processor::createEditor() { return new AM20Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AM20Processor(); }
