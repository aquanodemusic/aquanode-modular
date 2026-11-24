#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace massif;

//==============================================================================
const char* AquaMassifProcessor::knobName (int k)
{
    static const char* n[kNumKnobs] = {
        "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
        "Osc 1 Pitch", "Osc 1 Wt-Position", "Osc 1 Intensity", "Osc 1 Amp", "Osc 1 F1/F2",
        "Osc 2 Pitch", "Osc 2 Wt-Position", "Osc 2 Intensity", "Osc 2 Amp", "Osc 2 F1/F2",
        "Osc 3 Pitch", "Osc 3 Wt-Position", "Osc 3 Intensity", "Osc 3 Amp", "Osc 3 F1/F2",
        "Noise Color", "Noise Amp", "Noise F1/F2", "Feedback Amp", "Feedback F1/F2",
        "Mod Osc Pitch", "Mod Osc Ring Mod", "Mod Osc Phase", "Mod Osc Position", "Mod Osc Filter FM",
        "Filter 1 Cutoff", "Filter 1 Bandwidth", "Filter 1 Resonance", "Filter 2 Cutoff", "Filter 2 Bandwidth", "Filter 2 Resonance",
        "Ser/Par",
        "Insert 1 Knob 1", "Insert 1 Knob 2", "Insert 2 Knob 1", "Insert 2 Knob 2",
        "FX 1 Dry/Wet", "FX 1 Knob 2", "FX 1 Knob 3", "FX 1 Knob 4", "FX 2 Dry/Wet", "FX 2 Knob 2", "FX 2 Knob 3", "FX 2 Knob 4",
        "Filter 1 Out", "Filter 2 Out", "Filter Mix", "Bypass Amount", "Pan", "Master Volume",
        "Mod 5 Rate", "Mod 6 Rate", "Mod 7 Rate", "Mod 8 Rate", "Mod 5 Amp", "Mod 6 Amp", "Mod 7 Amp", "Mod 8 Amp",
        "Mod 5 Morph", "Mod 6 Morph", "Mod 7 Morph", "Mod 8 Morph", "Mod 5 Env Level", "Mod 6 Env Level", "Mod 7 Env Level", "Mod 8 Env Level",
        "Mod 5 Sync", "Mod 6 Sync", "Mod 7 Sync", "Mod 8 Sync", "Mod 5 Restart", "Mod 6 Restart", "Mod 7 Restart", "Mod 8 Restart",
        "Env 1 Velocity", "Env 2 Velocity", "Env 3 Velocity", "Env 4 Velocity", "Env 1 Keytrack", "Env 2 Keytrack", "Env 3 Keytrack", "Env 4 Keytrack",
        "Env 1 Delay", "Env 2 Delay", "Env 3 Delay", "Env 4 Delay", "Env 1 Attack", "Env 2 Attack", "Env 3 Attack", "Env 4 Attack",
        "Env 1 Attack Level", "Env 2 Attack Level", "Env 3 Attack Level", "Env 4 Attack Level",
        "Env 1 Decay", "Env 2 Decay", "Env 3 Decay", "Env 4 Decay", "Env 1 Decay Level", "Env 2 Decay Level", "Env 3 Decay Level", "Env 4 Decay Level",
        "Env 1 Loop Time", "Env 2 Loop Time", "Env 3 Loop Time", "Env 4 Loop Time", "Env 1 Sustain Level", "Env 2 Sustain Level", "Env 3 Sustain Level", "Env 4 Sustain Level",
        "Env 1 Morph", "Env 2 Morph", "Env 3 Morph", "Env 4 Morph", "Env 1 Release", "Env 2 Release", "Env 3 Release", "Env 4 Release",
        "Glide", "Vibrato Rate", "Vibrato Depth", "Unison Pitch Spread", "Unison Wt Spread", "Unison Pan Spread",
        "EQ Low", "EQ Peak", "EQ Peak Freq", "EQ High", "Env Hold Reset" };
    return k >= 0 && k < kNumKnobs ? n[k] : "?";
}

juce::StringArray AquaMassifProcessor::tableNames()
{
    juce::StringArray s;
    for (auto& i : slotInfos()) s.add (i.name);
    return s;
}

namespace
{
    const juce::StringArray kModes { "Spectrum", "Bend +", "Bend -/+", "Bend -", "Formant" };
    const juce::StringArray kFilters { "None", "Lowpass 4", "Lowpass 2", "Highpass 4", "Highpass 2", "Allpass", "Double Notch",
                                       "Bandpass", "Bandreject", "Scream", "Daft", "Comb", "Acid" };
    const juce::StringArray kInserts { "None", "Delay", "Sample & Hold", "Bitcrusher", "Frequency Shifter", "HP/LP",
                                       "Sine Shaper", "Parabolic Shaper", "Clipper" };
    const juce::StringArray kInsPos { "Off", "Pre F1", "Pre F2", "Post F1", "Serial", "Post F2", "Feedback Loop", "Pre Amp" };
    const juce::StringArray kFx { "None", "Reverb", "Reverb Small", "Flanger +", "Flanger + Mono", "Flanger -", "Flanger - Mono",
                                  "Chorus", "Chorus Mono", "Chorus Ensemble", "Phaser", "Phaser Mono", "Delay Sync", "Delay Short",
                                  "Dimension", "Classic Tube", "Tube Tube", "Bright Tube" };
    const juce::StringArray kNoise { "White", "Bright", "Brown", "Tape", "Metal", "Amp", "Paper", "Power", "Pipe", "Hi-Metal", "Murmur", "Alu" };
    const juce::StringArray kFbPos { "Off", "Post F1", "Post F2", "Pre Amp", "Post Amp" };
    const juce::StringArray kOscT { "Off", "Osc 1", "Osc 2", "Osc 3" };
    const juce::StringArray kFltT { "Off", "Filter 1", "Filter 2" };
    const juce::StringArray kBypSrc { "Osc 1", "Osc 2", "Osc 3", "Noise" };
    const juce::StringArray kBypTgt { "before FX 1", "before FX 2", "before EQ" };
    const juce::StringArray kVoiceMode { "Polyphonic", "Monophonic", "Mono Rotate" };
    const juce::StringArray kTrigger { "Always", "Legato", "Legato Trill" };
    const juce::StringArray kModType { "LFO", "Stepper", "Performer" };
}

juce::StringArray AquaMassifProcessor::choiceList (const juce::String& id)
{
    if (id.startsWith ("wt")) return tableNames();
    if (id.startsWith ("mode")) return kModes;
    if (id == "rmT" || id == "pmT" || id == "posT") return kOscT;
    if (id == "fmT") return kFltT;
    if (id == "noiseType") return kNoise;
    if (id == "fbPos") return kFbPos;
    if (id == "f1Type" || id == "f2Type") return kFilters;
    if (id == "i1Type" || id == "i2Type") return kInserts;
    if (id == "i1Pos" || id == "i2Pos") return kInsPos;
    if (id == "fx1Type" || id == "fx2Type") return kFx;
    if (id == "bypassSrc") return kBypSrc;
    if (id == "bypassTgt") return kBypTgt;
    if (id == "voiceMode") return kVoiceMode;
    if (id == "trigger") return kTrigger;
    if (id.startsWith ("mod") && id.endsWith ("Type")) return kModType;
    return {};
}

int AquaMassifProcessor::choiceDefault (const juce::String& id)
{
    if (id.startsWith ("wt")) return 2;
    if (id == "fbPos") return 4;
    if (id == "i1Pos") return 3;
    if (id == "i2Pos") return 7;
    if (id == "mod8Type") return 1;
    return 0;
}

juce::AudioProcessorValueTreeState::ParameterLayout AquaMassifProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    Patch d;
    for (int k = 0; k < kNumKnobs; ++k)
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { knobId (k), 1 }, knobName (k),
                                                             juce::NormalisableRange<float> (0.0f, 1.0f), d.knob[(size_t) k]));
    auto ch = [&] (const char* id, const char* name, const juce::StringArray& opts, int def)
    { l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, opts, def)); };
    auto bo = [&] (const char* id, const char* name, bool def)
    { l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def)); };
    for (int o = 1; o <= 3; ++o)
    {
        const auto n = juce::String (o);
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "wt" + n, 1 }, "Osc " + n + " Wavetable", tableNames(), 2));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode" + n, 1 }, "Osc " + n + " Mode", kModes, 0));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "osc" + n + "On", 1 }, "Osc " + n + " On", true));
    }
    bo ("modOscOn", "Mod Osc On", true);
    ch ("rmT", "Ring Mod Target", kOscT, 0); ch ("pmT", "Phase Mod Target", kOscT, 0); ch ("posT", "Position Mod Target", kOscT, 0);
    ch ("fmT", "Filter FM Target", kFltT, 0);
    bo ("noiseOn", "Noise On", true); ch ("noiseType", "Noise Type", kNoise, 0);
    bo ("fbOn", "Feedback On", true); ch ("fbPos", "Feedback Source", kFbPos, 4);
    bo ("f1On", "Filter 1 On", true); ch ("f1Type", "Filter 1 Type", kFilters, 0);
    bo ("f2On", "Filter 2 On", true); ch ("f2Type", "Filter 2 Type", kFilters, 0);
    bo ("i1On", "Insert 1 On", true); ch ("i1Type", "Insert 1 Type", kInserts, 0); ch ("i1Pos", "Insert 1 Position", kInsPos, 3);
    bo ("i2On", "Insert 2 On", true); ch ("i2Type", "Insert 2 Type", kInserts, 0); ch ("i2Pos", "Insert 2 Position", kInsPos, 7);
    bo ("fx1On", "FX 1 On", true); ch ("fx1Type", "FX 1 Type", kFx, 0);
    bo ("fx2On", "FX 2 On", true); ch ("fx2Type", "FX 2 Type", kFx, 0);
    bo ("ampOn", "Amp On", true); bo ("eqOn", "EQ On", false);
    bo ("bypassOn", "Bypass On", true); ch ("bypassSrc", "Bypass Source", kBypSrc, 0); ch ("bypassTgt", "Bypass Target", kBypTgt, 0);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "voices", 1 }, "Voices", 1, 64, 16));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "unison", 1 }, "Unison", 1, 16, 1));
    bo ("uniPitchOn", "Unison Pitch On", false); bo ("uniWtOn", "Unison Wt On", false); bo ("uniPanOn", "Unison Pan On", false);
    ch ("voiceMode", "Voice Mode", kVoiceMode, 0); ch ("trigger", "Trigger", kTrigger, 0);
    bo ("vibMono", "Vibrato Mono", false);
    for (int m = 5; m <= 8; ++m)
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mod" + juce::String (m) + "Type", 1 },
                                                              "Mod " + juce::String (m) + " Type", kModType, m == 8 ? 1 : 0));
    return l;
}

//==============================================================================
AquaMassifProcessor::AquaMassifProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    for (int k = 0; k < kNumKnobs; ++k) knobRaw[(size_t) k] = apvts.getRawParameterValue (knobId (k));
    static const char* const ids[cNumChoices] = {
        "wt1", "wt2", "wt3", "mode1", "mode2", "mode3", "osc1On", "osc2On", "osc3On", "modOscOn", "rmT", "pmT", "posT", "fmT",
        "noiseOn", "noiseType", "fbOn", "fbPos", "f1On", "f1Type", "f2On", "f2Type", "i1On", "i1Type", "i1Pos", "i2On", "i2Type", "i2Pos",
        "fx1On", "fx1Type", "fx2On", "fx2Type", "ampOn", "eqOn", "bypassOn", "bypassSrc", "bypassTgt", "voices", "unison",
        "uniPitchOn", "uniWtOn", "uniPanOn", "voiceMode", "trigger", "vibMono", "mod5Type", "mod6Type", "mod7Type", "mod8Type" };
    for (int i = 0; i < cNumChoices; ++i) { choiceRaw[(size_t) i] = apvts.getRawParameterValue (ids[i]); jassert (choiceRaw[(size_t) i] != nullptr); }
    loadDefaultFolder();
    audioPatch = extra;
    startTimerHz (20);
}

AquaMassifProcessor::~AquaMassifProcessor()
{
    stopTimer();
    builder.removeAllJobs (true, 2000);
}

bool AquaMassifProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void AquaMassifProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare ((float) sampleRate, juce::jmax (16, samplesPerBlock));
    tmpL.assign ((size_t) juce::jmax (16, samplesPerBlock), 0.0f);
    tmpR.assign ((size_t) juce::jmax (16, samplesPerBlock), 0.0f);
    tmpBL.assign (tmpL.size(), 0.0f); tmpBR.assign (tmpL.size(), 0.0f);
    // the tables already known
    for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
    timerCallback();
}

void AquaMassifProcessor::setKnob (int k, float v)
{
    if (auto* p = apvts.getParameter (knobId (k))) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

void AquaMassifProcessor::setChoice (const juce::String& id, int v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
}

void AquaMassifProcessor::readParams (Patch& p) const
{
    // no strings, no lookups: the audio thread calls this every block
    for (int k = 0; k < kNumKnobs; ++k) p.knob[(size_t) k] = knobRaw[(size_t) k]->load();
    for (int o = 0; o < 3; ++o)
    {
        p.wavetable[(size_t) o] = rc (cWt + o); p.oscMode[(size_t) o] = rc (cMode + o); p.oscOn[(size_t) o] = rc (cOscOn + o) != 0;
    }
    p.modOscOn = rc (cModOscOn) != 0;
    p.rmTarget = rc (cRmT); p.pmTarget = rc (cPmT); p.posTarget = rc (cPosT); p.fmTarget = rc (cFmT);
    p.noiseOn = rc (cNoiseOn) != 0; p.noiseType = rc (cNoiseType);
    p.fbOn = rc (cFbOn) != 0; p.fbPos = rc (cFbPos);
    p.filterOn[0] = rc (cF1On) != 0; p.filterType[0] = rc (cF1Type);
    p.filterOn[1] = rc (cF2On) != 0; p.filterType[1] = rc (cF2Type);
    p.insertOn[0] = rc (cI1On) != 0; p.insertType[0] = rc (cI1Type); p.insertPos[0] = rc (cI1Pos);
    p.insertOn[1] = rc (cI2On) != 0; p.insertType[1] = rc (cI2Type); p.insertPos[1] = rc (cI2Pos);
    p.fxOn[0] = rc (cFx1On) != 0; p.fxType[0] = rc (cFx1Type);
    p.fxOn[1] = rc (cFx2On) != 0; p.fxType[1] = rc (cFx2Type);
    p.ampOn = rc (cAmpOn) != 0; p.eqOn = rc (cEqOn) != 0;
    p.bypassOn = rc (cBypassOn) != 0; p.bypassSource = rc (cBypassSrc); p.bypassTarget = rc (cBypassTgt);
    p.maxVoices = rc (cVoices);
    p.unison = rc (cUnison);
    p.uniPitchOn = rc (cUniPitchOn) != 0; p.uniWtOn = rc (cUniWtOn) != 0; p.uniPanOn = rc (cUniPanOn) != 0;
    p.voiceMode = rc (cVoiceMode); p.triggerMode = rc (cTrigger); p.vibMono = rc (cVibMono) != 0;
    for (int m = 0; m < 4; ++m) p.modType[(size_t) m] = rc (cModType + m);
}

void AquaMassifProcessor::writeParams (const Patch& p)
{
    for (int k = 0; k < kNumKnobs; ++k) setKnob (k, p.knob[(size_t) k]);
    for (int o = 0; o < 3; ++o)
    {
        const auto n = juce::String (o + 1);
        setChoice ("wt" + n, p.wavetable[(size_t) o]); setChoice ("mode" + n, p.oscMode[(size_t) o]); setChoice ("osc" + n + "On", p.oscOn[(size_t) o] ? 1 : 0);
    }
    setChoice ("modOscOn", p.modOscOn); setChoice ("rmT", p.rmTarget); setChoice ("pmT", p.pmTarget); setChoice ("posT", p.posTarget); setChoice ("fmT", p.fmTarget);
    setChoice ("noiseOn", p.noiseOn); setChoice ("noiseType", p.noiseType); setChoice ("fbOn", p.fbOn); setChoice ("fbPos", p.fbPos);
    setChoice ("f1On", p.filterOn[0]); setChoice ("f1Type", p.filterType[0]); setChoice ("f2On", p.filterOn[1]); setChoice ("f2Type", p.filterType[1]);
    setChoice ("i1On", p.insertOn[0]); setChoice ("i1Type", p.insertType[0]); setChoice ("i1Pos", p.insertPos[0]);
    setChoice ("i2On", p.insertOn[1]); setChoice ("i2Type", p.insertType[1]); setChoice ("i2Pos", p.insertPos[1]);
    setChoice ("fx1On", p.fxOn[0]); setChoice ("fx1Type", p.fxType[0]); setChoice ("fx2On", p.fxOn[1]); setChoice ("fx2Type", p.fxType[1]);
    setChoice ("ampOn", p.ampOn); setChoice ("eqOn", p.eqOn);
    setChoice ("bypassOn", p.bypassOn); setChoice ("bypassSrc", p.bypassSource); setChoice ("bypassTgt", p.bypassTarget);
    if (auto* v = apvts.getParameter ("voices")) v->setValueNotifyingHost (v->convertTo0to1 ((float) p.maxVoices));
    if (auto* u = apvts.getParameter ("unison")) u->setValueNotifyingHost (u->convertTo0to1 ((float) juce::jlimit (1, 16, p.unison)));
    setChoice ("uniPitchOn", p.uniPitchOn); setChoice ("uniWtOn", p.uniWtOn); setChoice ("uniPanOn", p.uniPanOn);
    setChoice ("voiceMode", p.voiceMode); setChoice ("trigger", p.triggerMode); setChoice ("vibMono", p.vibMono);
    for (int m = 0; m < 4; ++m) setChoice ("mod" + juce::String (m + 5) + "Type", p.modType[(size_t) m]);
}

Patch AquaMassifProcessor::currentPatch() const
{
    Patch p = extra;
    readParams (p);
    return p;
}

void AquaMassifProcessor::applyPatch (const Patch& p)
{
    extra = p;
    writeParams (p);
    extraChanged();
}

void AquaMassifProcessor::initPatch()
{
    releaseOwnTables();
    Patch p; p.name = "Init";
    applyPatch (p);
    lastPresetFile = juce::File(); factoryIndex = -1; soundFromImport = false; bundleIndex = -1;
}

//==============================================================================
void AquaMassifProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    // the non-parameter part of the patch
    if (const juce::SpinLock::ScopedTryLockType l (patchLock); l.isLocked() && pendingValid)
    {
        audioPatch = pendingPatch; pendingValid = false;
    }
    readParams (audioPatch);
    {
        const juce::SpinLock::ScopedTryLockType l (tableLock);
        if (l.isLocked())
            for (int o = 0; o < 3; ++o)
                if (pendingTable[(size_t) o] != nullptr && pendingSlot[(size_t) o] == audioPatch.wavetable[(size_t) o]
                    && swapTable (o, pendingTable[(size_t) o]))
                {
                    liveSlot[(size_t) o] = pendingSlot[(size_t) o];
                    pendingTable[(size_t) o] = nullptr;
                }
    }
    // a table that is built already goes in at once (a patch change, an offline render)
    for (int o = 0; o < 3; ++o)
    {
        const int want = juce::jlimit (0, 86, audioPatch.wavetable[(size_t) o]);
        if (liveSlot[(size_t) o] == want) continue;
        const juce::SpinLock::ScopedTryLockType l (tableLock);
        if (l.isLocked() && built[(size_t) want] != nullptr && swapTable (o, built[(size_t) want])) liveSlot[(size_t) o] = want;
    }
    // a table not yet built for its slot plays silence rather than the wrong table
    for (int o = 0; o < 3; ++o) if (liveSlot[(size_t) o] != audioPatch.wavetable[(size_t) o]) swapTable (o, nullptr);
    engine.setPatch (audioPatch);

    double bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    if (bpm <= 0.0 && tempoOverride > 0.0) bpm = tempoOverride;

    if ((int) tmpL.size() < n) { tmpL.resize ((size_t) n); tmpR.resize ((size_t) n); }
    if ((int) tmpBL.size() < n) { tmpBL.resize ((size_t) n); tmpBR.resize ((size_t) n); }

    if (split == splitEffects)
    {
        // the patch version's Effects module: the buffer is the voices' sum
        const int m = juce::jmin (n, kSplitBlock);
        float* L = buffer.getWritePointer (0); float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
        engine.processEffectsOnly (L, R, splitBypass[0].data(), splitBypass[1].data(), m, bpm, splitFxMod.data());
        midi.clear();
        return;
    }

    int at = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= at) return;
        if (split == splitVoices) engine.renderVoices (tmpL.data() + at, tmpR.data() + at, tmpBL.data() + at, tmpBR.data() + at, end - at, bpm);
        else engine.process (tmpL.data() + at, tmpR.data() + at, end - at, bpm);
        at = end;
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        // MPE (switched on by the host at the first note on a member channel): channels 2..16 carry one
        // note each with its own bend (+-48 semitones) and pressure; channel 1 stays the global one
        const int ch = mpeOn ? m.getChannel() : 1;
        const bool member = ch >= 2 && ch <= 16;
        engine.noteChannel = member ? ch : 0;
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isPitchWheel() && member) engine.chanBend[(size_t) ch] = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isChannelPressure() && member) engine.chanPress[(size_t) ch] = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isController() && member && m.getControllerNumber() == 74) engine.chanSlide[(size_t) ch] = (float) m.getControllerValue() / 127.0f;   // (6.9.12)
        else if (m.isPitchWheel()) engine.bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isChannelPressure()) engine.aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch()) engine.aftertouch = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isAllNotesOff()) engine.allNotesOff();
        else if (m.isAllSoundOff()) engine.allSoundOff();
    }
    renderTo (n);
    midi.clear();
    if (split == splitVoices)
    {
        for (int i = 0; i < juce::jmin (n, kSplitBlock); ++i) { splitBypass[0][(size_t) i] = tmpBL[(size_t) i]; splitBypass[1][(size_t) i] = tmpBR[(size_t) i]; }
        splitFxMod = engine.fxMod;
    }

    float pk = 0.0f;
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float l = std::isfinite (tmpL[(size_t) i]) ? tmpL[(size_t) i] : 0.0f, r = std::isfinite (tmpR[(size_t) i]) ? tmpR[(size_t) i] : 0.0f;
        if (R) { L[i] = l; R[i] = r; } else L[i] = 0.5f * (l + r);
        pk = juce::jmax (pk, std::abs (l), std::abs (r));
    }
    outLevel.store (juce::jmax (pk, outLevel.load() * 0.9f));
    int nv = 0; const Voice* first = nullptr;
    for (auto& v : engine.voices) if (v.active) { ++nv; if (! first || v.age > first->age) first = &v; }
    activeVoices.store (nv);
    if (first)
    {
        for (int e = 0; e < 4; ++e) modView[e].store (first->env[(size_t) e].output());
        for (int m = 0; m < 4; ++m) modView[4 + m].store (first->page[(size_t) m].out);
        // (6.9.16: with the finger's Slide too - where the newest voice plays)
        for (int o = 0; o < 3; ++o) posView[o].store (juce::jlimit (0.0f, 1.0f, first->k[(size_t) (kOsc1Pos + 5 * o)] + first->uniWt
                                                                             + engine.voiceSlidePos (*first, o)));
    }
    else for (int o = 0; o < 3; ++o) posView[o].store (audioPatch.knob[(size_t) (kOsc1Pos + 5 * o)]);
}

//==============================================================================
// wavetables
namespace
{
    // a sound file's samples (its first channel): WAV here (with the "clm " chunk's frame size), FLAC, AIFF
    // and Ogg through JUCE
    bool decodeSound (const juce::MemoryBlock& mb, std::vector<float>& out, int& frameSize)
    {
        const std::vector<char> bytes ((const char*) mb.getData(), (const char*) mb.getData() + mb.getSize());
        frameSize = kFrameSize;
        if (readWavMemory (bytes, out, frameSize)) return true;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (mb, false)));
        if (r == nullptr || r->lengthInSamples <= 0 || r->lengthInSamples > 16 * 1024 * 1024) return false;
        juce::AudioBuffer<float> buf ((int) juce::jmax (1u, r->numChannels), (int) r->lengthInSamples);
        if (! r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true)) return false;
        out.assign (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
        return ! out.empty();
    }
    // ... as 2048-sample frames: a sound shorter than a frame is one cycle; at most 256 frames
    std::vector<float> soundFrames (const juce::MemoryBlock& mb)
    {
        std::vector<float> raw; int fs = kFrameSize;
        if (! decodeSound (mb, raw, fs)) return {};
        if (raw.size() < (size_t) fs) fs = (int) raw.size();
        if (fs < 16) return {};
        auto v = toFrames2048 (raw, fs);
        if (v.size() > (size_t) 256 * kFrameSize) v.resize ((size_t) 256 * kFrameSize);
        return v;
    }
    // frames -> a 16 bit mono .wav with the "clm " chunk wavetable editors look for (gain: what the samples
    // were divided by to fit)
    juce::MemoryBlock framesToWav (const std::vector<float>& v, float& gain)
    {
        float pk = 0.0f; for (float x : v) pk = std::max (pk, std::abs (x));
        gain = pk > 1.0f ? pk : 1.0f;
        juce::MemoryOutputStream o;
        const char clm[] = "<!>2048 10000000 wavetable (aquamassif)";
        const int clmLen = (int) sizeof (clm) - 1, clmSize = clmLen + (clmLen & 1), dataBytes = (int) v.size() * 2;
        o.write ("RIFF", 4); o.writeInt (4 + 8 + 16 + 8 + clmSize + 8 + dataBytes); o.write ("WAVE", 4);
        o.write ("fmt ", 4); o.writeInt (16); o.writeShort (1); o.writeShort (1); o.writeInt (44100); o.writeInt (44100 * 2); o.writeShort (2); o.writeShort (16);
        o.write ("clm ", 4); o.writeInt (clmSize); o.write (clm, (size_t) clmLen); if (clmLen & 1) o.writeByte (0);
        o.write ("data", 4); o.writeInt (dataBytes);
        for (float x : v) o.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround (x / gain * 32767.0f)));
        return o.getMemoryBlock();
    }
    bool isSoundFile (const juce::String& n)
    {
        for (auto e : { ".wav", ".flac", ".aif", ".aiff", ".ogg" }) if (n.endsWithIgnoreCase (e)) return true;
        return false;
    }
    std::array<bool, 87> allSlots() { std::array<bool, 87> a; a.fill (true); return a; }
}

juce::File AquaMassifProcessor::ownTablesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Massif").getChildFile ("Wavetables");
}

juce::File AquaMassifProcessor::exportTemplateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Massif").getChildFile ("Export Template.nmsv");
}

void AquaMassifProcessor::loadDefaultFolder()
{
    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    for (auto f : { docs.getChildFile ("Massif").getChildFile ("Wavetables"), docs.getChildFile ("Massif"),
                    docs.getChildFile ("Aqua Massif").getChildFile ("Wavetables"), docs.getChildFile ("Aqua Massif") })
        if (f.isDirectory() && setWavetableFolder (f) > 0) return;
    rescanTables (allSlots());     // Massif's own folder (a zip loaded earlier, single tables)
}

// the original's tables in a folder (and its subfolders), by their names
int AquaMassifProcessor::scanFolder (const juce::File& dir, std::array<std::vector<float>, 87>& fresh, std::array<juce::File, 87>& from,
                                     std::array<bool, 87>& got, const std::array<bool, 87>& want) const
{
    if (! dir.isDirectory()) return 0;
    int found = 0;
    for (auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV;*.flac;*.FLAC"))
    {
        const int s = Bank::slotForFile (f.getFileName().toStdString());
        if (s < 0 || got[(size_t) s] || ! want[(size_t) s]) continue;
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) continue;
        std::vector<float> raw; int fs = kFrameSize;
        if (! decodeSound (mb, raw, fs)) continue;
        fresh[(size_t) s] = toFrames2048 (raw, fs);
        if (fresh[(size_t) s].empty()) continue;
        from[(size_t) s] = f; got[(size_t) s] = true; ++found;
    }
    return found;
}

// fills the slots asked for (that hold no generated or loaded table) from the folders: the chosen one first,
// then Massif's own
int AquaMassifProcessor::rescanTables (const std::array<bool, 87>& which)
{
    std::array<std::vector<float>, 87> fresh; std::array<juce::File, 87> from; std::array<bool, 87> got {};
    std::array<bool, 87> want = which;
    {
        const juce::SpinLock::ScopedLockType l (tableLock);
        for (int i = 0; i < 87; ++i) want[(size_t) i] = want[(size_t) i] && (rawKind[(size_t) i] == tkStandIn || rawKind[(size_t) i] == tkFile);
    }
    int found = scanFolder (tableFolder, fresh, from, got, want);
    found += scanFolder (ownTablesFolder(), fresh, from, got, want);
    if (found == 0) return 0;
    std::lock_guard<std::mutex> g (buildMutex);
    const juce::SpinLock::ScopedLockType l (tableLock);
    for (int i = 0; i < 87; ++i)
        if (got[(size_t) i] && (rawKind[(size_t) i] == tkStandIn || rawKind[(size_t) i] == tkFile))
        {
            rawFrames[(size_t) i] = std::move (fresh[(size_t) i]); rawKind[(size_t) i] = tkFile; built[(size_t) i] = nullptr;
            rawPath[(size_t) i] = from[(size_t) i]; rawFile[(size_t) i].reset(); rawName[(size_t) i].clear(); rawRecipe[(size_t) i] = {};
        }
    for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
    return found;
}

int AquaMassifProcessor::setWavetableFolder (const juce::File& dir, int onlySlot)
{
    if (! dir.isDirectory()) return 0;
    std::array<bool, 87> which = allSlots();
    if (onlySlot >= 0) { which.fill (false); which[(size_t) juce::jlimit (0, 86, onlySlot)] = true; }
    // tables in it at all?
    {
        std::array<std::vector<float>, 87> fresh; std::array<juce::File, 87> from; std::array<bool, 87> got {};
        if (scanFolder (dir, fresh, from, got, which) == 0) return 0;
    }
    const auto old = tableFolder;
    tableFolder = dir;
    if (old != dir && onlySlot < 0)
    {
        // what the old folder gave goes back to the stand-ins first
        const juce::SpinLock::ScopedLockType l (tableLock);
        for (int i = 0; i < 87; ++i)
            if (rawKind[(size_t) i] == tkFile && rawPath[(size_t) i] != juce::File() && rawPath[(size_t) i].isAChildOf (old))
            { rawKind[(size_t) i] = tkStandIn; rawFrames[(size_t) i].clear(); rawPath[(size_t) i] = juce::File(); built[(size_t) i] = nullptr; }
    }
    return rescanTables (which);
}

int AquaMassifProcessor::importWavetableZip (juce::InputStream& in, juce::String& error)
{
    juce::ZipFile zip (in);
    if (zip.getNumEntries() <= 0) { error = "This is not a zip file."; return 0; }
    const auto dir = ownTablesFolder();
    if (! dir.createDirectory()) { error = "Massif's wavetable folder could not be made."; return 0; }
    int copied = 0, known = 0;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        if (e == nullptr) continue;
        // the file name alone (the zip's folders do not matter: the tables are found by their names)
        const auto name = e->filename.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
        if (! isSoundFile (name) || name.startsWithChar ('.')) continue;
        const int s = Bank::slotForFile (name.toStdString());
        if (s < 0) continue;
        std::unique_ptr<juce::InputStream> es (zip.createStreamForEntry (i));
        if (es == nullptr) continue;
        juce::MemoryBlock mb; es->readIntoMemoryBlock (mb);
        if (soundFrames (mb).empty()) continue;
        if (dir.getChildFile (juce::File::createLegalFileName (name)).replaceWithData (mb.getData(), mb.getSize())) ++copied;
        ++known;
    }
    if (known == 0) { error = "No wavetables with known names were found in this zip."; return 0; }
    rescanTables (allSlots());
    return copied;
}

int AquaMassifProcessor::loadOscTable (int osc, const juce::MemoryBlock& wav, const juce::String& fileName, juce::String& error)
{
    if (osc < 0 || osc > 2) return -1;
    auto v = soundFrames (wav);
    if (v.empty()) { error = "This file could not be read as a wavetable (WAV, FLAC, AIFF or Ogg)."; return -1; }
    const auto name = fileName.fromLastOccurrenceOf ("/", false, false);
    int s = Bank::slotForFile (name.toStdString());
    if (s >= 0)
    {
        // one of the original's tables: its place, and a copy in Massif's own folder for next time
        const auto dir = ownTablesFolder();
        juce::File copy;
        if (dir.createDirectory())
        {
            copy = dir.getChildFile (juce::File::createLegalFileName (name));
            if (! copy.replaceWithData (wav.getData(), wav.getSize())) copy = juce::File();
        }
        std::lock_guard<std::mutex> g (buildMutex);
        const juce::SpinLock::ScopedLockType l (tableLock);
        rawFrames[(size_t) s] = std::move (v); rawKind[(size_t) s] = tkFile; rawDiv[(size_t) s] = 1.0f; rawRecipe[(size_t) s] = {};
        rawPath[(size_t) s] = copy; rawFile[(size_t) s] = copy == juce::File() ? wav : juce::MemoryBlock(); rawName[(size_t) s] = copy == juce::File() ? name : juce::String();
        built[(size_t) s] = nullptr;
        for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
    }
    else
    {
        // any other table: in place of the oscillator's current one, for this sound (saved with it)
        s = juce::jlimit (0, 86, choice ("wt" + juce::String (osc + 1)));
        std::lock_guard<std::mutex> g (buildMutex);
        const juce::SpinLock::ScopedLockType l (tableLock);
        rawFrames[(size_t) s] = std::move (v); rawKind[(size_t) s] = tkUser; rawDiv[(size_t) s] = 1.0f; rawRecipe[(size_t) s] = {};
        rawPath[(size_t) s] = juce::File(); rawFile[(size_t) s] = wav; rawName[(size_t) s] = name;
        built[(size_t) s] = nullptr;
        for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
    }
    setChoice ("wt" + juce::String (osc + 1), s);
    return s;
}

// a new sound: the tables generated or loaded for the one before go back to the originals
void AquaMassifProcessor::releaseOwnTables()
{
    std::array<bool, 87> which {};
    {
        std::lock_guard<std::mutex> g (buildMutex);
        const juce::SpinLock::ScopedLockType l (tableLock);
        for (int i = 0; i < 87; ++i)
            if (rawKind[(size_t) i] == tkGenerated || rawKind[(size_t) i] == tkUser)
            {
                rawFrames[(size_t) i].clear(); rawKind[(size_t) i] = tkStandIn; rawDiv[(size_t) i] = 1.0f; rawRecipe[(size_t) i] = {};
                rawFile[(size_t) i].reset(); rawName[(size_t) i].clear(); rawPath[(size_t) i] = juce::File(); built[(size_t) i] = nullptr;
                which[(size_t) i] = true;
            }
        for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
    }
    rescanTables (which);
}

int AquaMassifProcessor::getNumFileTables() const
{
    int n = 0; for (int k : rawKind) n += k == tkFile ? 1 : 0; return n;
}

// built[] is read and written under tableLock; buildMutex makes one build at a time and keeps a
// reset (a new folder, a generated table, a state) from landing in the middle of one.
std::shared_ptr<const Wavetable> AquaMassifProcessor::tableFor (int slot)
{
    std::lock_guard<std::mutex> g (buildMutex);
    slot = juce::jlimit (0, 86, slot);
    std::vector<float> raw; int kind;
    {
        const juce::SpinLock::ScopedLockType l (tableLock);
        if (built[(size_t) slot]) return built[(size_t) slot];
        raw = rawFrames[(size_t) slot]; kind = rawKind[(size_t) slot];
    }
    if (raw.empty()) { raw = gen::forSlot (slot); kind = tkStandIn; }
    // the measured playback octave belongs to the original file of a slot
    float div = 1.0f; { const juce::SpinLock::ScopedLockType l (tableLock); div = rawDiv[(size_t) slot] > 0.0f ? rawDiv[(size_t) slot] : 1.0f; }
    auto t = makeTable (slotInfos()[(size_t) slot].name, raw, kind == tkFile ? slotInfos()[(size_t) slot].pitchDiv : (kind == tkGenerated ? div : 1.0f));
    const juce::SpinLock::ScopedLockType l (tableLock);
    built[(size_t) slot] = t;
    return t;
}

std::shared_ptr<const Wavetable> AquaMassifProcessor::builtTable (int slot)
{
    if (slot < 0 || slot >= 87) return nullptr;
    const juce::SpinLock::ScopedLockType l (tableLock);
    return built[(size_t) slot];
}

void AquaMassifProcessor::generateTable (int slot, int recipe, float a, float b, float c, int seed, int frames)
{
    if (slot < 0 || slot >= 87) return;
    auto v = gen::recipe (recipe, a, b, c, (uint32_t) seed, frames);
    std::lock_guard<std::mutex> g (buildMutex);
    const juce::SpinLock::ScopedLockType l (tableLock);
    rawFrames[(size_t) slot] = std::move (v); rawKind[(size_t) slot] = tkGenerated; rawDiv[(size_t) slot] = gen::recipeDiv (recipe); built[(size_t) slot] = nullptr;
    rawRecipe[(size_t) slot] = { recipe, a, b, c, seed, frames };
    rawFile[(size_t) slot].reset(); rawName[(size_t) slot].clear(); rawPath[(size_t) slot] = juce::File();
    for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
}

void AquaMassifProcessor::restoreTable (int slot)
{
    if (slot < 0 || slot >= 87) return;
    {
        std::lock_guard<std::mutex> g (buildMutex);
        const juce::SpinLock::ScopedLockType l (tableLock);
        rawFrames[(size_t) slot].clear(); rawKind[(size_t) slot] = tkStandIn; rawDiv[(size_t) slot] = 1.0f; built[(size_t) slot] = nullptr;
        rawRecipe[(size_t) slot] = {}; rawFile[(size_t) slot].reset(); rawName[(size_t) slot].clear(); rawPath[(size_t) slot] = juce::File();
        for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
    }
    std::array<bool, 87> which {}; which[(size_t) slot] = true;
    rescanTables (which);
}

bool AquaMassifProcessor::exportTable (int slot, const juce::File& wavFile)
{
    if (slot < 0 || slot >= 87) return false;
    std::vector<float> raw;
    {
        const juce::SpinLock::ScopedLockType l (tableLock);
        raw = rawFrames[(size_t) slot];
    }
    if (raw.empty()) raw = gen::forSlot (slot);
    // a mono float .wav with the "clm " chunk wavetable editors look for
    juce::MemoryOutputStream o;
    const char clm[] = "<!>2048 10000000 wavetable (aquamassif)";
    const int clmSize = (int) sizeof (clm) - 1 + (((int) sizeof (clm) - 1) & 1);
    const int dataBytes = (int) raw.size() * 4;
    o.write ("RIFF", 4); o.writeInt (4 + 8 + 16 + 8 + clmSize + 8 + dataBytes); o.write ("WAVE", 4);
    o.write ("fmt ", 4); o.writeInt (16); o.writeShort (3); o.writeShort (1); o.writeInt (44100); o.writeInt (44100 * 4); o.writeShort (4); o.writeShort (32);
    o.write ("clm ", 4); o.writeInt (clmSize); o.write (clm, sizeof (clm) - 1); if ((sizeof (clm) - 1) & 1) o.writeByte (0);
    o.write ("data", 4); o.writeInt (dataBytes);
    for (float x : raw) o.writeFloat (x);
    return wavFile.replaceWithData (o.getData(), o.getDataSize());
}

juce::String AquaMassifProcessor::generatePatch (int type, int genre)
{
    namespace gen = massif::generator;
    releaseOwnTables();
    Patch p;
    const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt() ^ (uint32_t) juce::Time::getHighResolutionTicks();
    if (type < 0) gen::superRandom (p, seed); else gen::generate (p, type, genre, seed);
    // the level: one quick render, with the tables that are ready or their stand-ins
    gen::trimLevel (p, type < 0 ? gen::tAnything : type, [this] (int slot)
    {
        if (auto t = builtTable (slot)) return t;
        return massif::Bank::generated (slot);
    }, (float) juce::jmax (22050.0, getSampleRate() > 0 ? getSampleRate() : 44100.0));
    if (type >= 0) { genType = type; genGenre = genre; }
    applyPatch (p);
    factoryIndex = -1; lastPresetFile = juce::File(); soundFromImport = false; bundleIndex = -1;
    return p.name;
}

void AquaMassifProcessor::loadFactoryPatch (int index)
{
    Patch p;
    if (! makeFactoryPatch (index, p)) return;
    releaseOwnTables();
    applyPatch (p);
    factoryIndex = index; lastPresetFile = juce::File(); soundFromImport = false; bundleIndex = -1;
}

bool AquaMassifProcessor::swapTable (int osc, std::shared_ptr<const massif::Wavetable> t)
{
    auto& cur = engine.table[(size_t) osc];
    if (cur == t) return true;
    if (cur != nullptr)
    {
        // the old one goes to the message thread to be freed (its memory may be the last reference)
        const auto w = retireFifo.write (1);
        if (w.blockSize1 + w.blockSize2 < 1) return false;
        retired[(size_t) (w.blockSize1 > 0 ? w.startIndex1 : w.startIndex2)] = std::move (cur);
    }
    cur = std::move (t);
    return true;
}

void AquaMassifProcessor::freeRetired()
{
    const auto r = retireFifo.read (retireFifo.getNumReady());
    r.forEach ([this] (int i) { retired[(size_t) i].reset(); });
}

void AquaMassifProcessor::timerCallback()
{
    freeRetired();
    // hand the non-parameter part of the patch over when it changed
    const int s = extraSerial.load();
    if (s != sentSerial)
    {
        const juce::SpinLock::ScopedLockType l (patchLock);
        pendingPatch = extra; pendingValid = true; sentSerial = s;
    }
    // build the tables the oscillators want
    for (int o = 0; o < 3; ++o)
    {
        const int want = choice ("wt" + juce::String (o + 1));
        bool needed;
        {
            const juce::SpinLock::ScopedLockType l (tableLock);
            needed = liveSlot[(size_t) o] != want && ! (pendingTable[(size_t) o] != nullptr && pendingSlot[(size_t) o] == want);
        }
        if (! needed) continue;
        {
            const juce::SpinLock::ScopedLockType l (tableLock);
            if (built[(size_t) want] != nullptr) { pendingTable[(size_t) o] = built[(size_t) want]; pendingSlot[(size_t) o] = want; continue; }
        }
        if (buildQueued[(size_t) want].exchange (true)) continue;      // one job per table
        builder.addJob ([this, o, want]
        {
            auto t = tableFor (want);
            buildQueued[(size_t) want] = false;
            const juce::SpinLock::ScopedLockType l (tableLock);
            // a reset since the build began leaves built[] empty again: this table is stale then
            if (built[(size_t) want] == t) { pendingTable[(size_t) o] = t; pendingSlot[(size_t) o] = want; }
        });
    }
}

//==============================================================================
// patch <-> tree (the non-parameter part)
juce::ValueTree AquaMassifProcessor::patchToTree (const Patch& p) const
{
    juce::ValueTree t ("Extra");
    t.setProperty ("name", juce::String (p.name), nullptr);
    juce::StringArray names; for (auto& n : p.macroName) names.add (n);
    t.setProperty ("macroNames", names.joinIntoString ("|"), nullptr);
    juce::String mods;
    for (int k = 0; k < kNumKnobs; ++k)
    {
        const auto& m = p.mod[(size_t) k];
        for (int s = 0; s < 3; ++s)
            if (m.slot[(size_t) s].source >= 0)
                mods << k << ":" << s << ":" << m.slot[(size_t) s].source << ":" << juce::String (m.slot[(size_t) s].amount, 5) << ";";
        if (m.sideChain) mods << k << ":sc:" << (m.scTarget[0] ? 1 : 0) << (m.scTarget[1] ? 1 : 0) << ";";
    }
    t.setProperty ("mods", mods, nullptr);
    juce::String amp;
    for (int s = 0; s < 3; ++s) amp << p.ampMod[0].slot[(size_t) s].source << ":" << juce::String (p.ampMod[0].slot[(size_t) s].amount, 5) << ";";
    t.setProperty ("ampMod", amp, nullptr);
    for (int e = 0; e < 4; ++e)
    {
        const auto& x = p.env[(size_t) e];
        t.setProperty ("env" + juce::String (e), juce::String (x.playMode) + "," + juce::String ((int) x.retrigger) + "," + juce::String ((int) x.linearDecay) + ","
                       + juce::String (x.loopCount) + "," + juce::String (x.morphShapeA) + "," + juce::String (x.morphShapeB), nullptr);
        const auto& l = p.lfo[(size_t) e];
        t.setProperty ("lfo" + juce::String (e), juce::String (l.shapeA) + "," + juce::String (l.shapeB) + "," + juce::String (l.phaseA, 4) + "," + juce::String ((int) l.mono) + "," + juce::String (l.phaseB, 4), nullptr);
        const auto& st = p.stepper[(size_t) e];
        juce::String steps; for (int i = 0; i < 16; ++i) steps << juce::String (st.step[(size_t) i], 4) << "," << (int) st.glideOn[(size_t) i] << "," << (int) st.ampOn[(size_t) i] << ";";
        t.setProperty ("step" + juce::String (e), steps + juce::String (st.loopStart) + "," + juce::String (st.loopEnd), nullptr);
        const auto& pf = p.performer[(size_t) e];
        juce::String perf; for (int i = 0; i < 16; ++i) perf << pf.curveA[(size_t) i] << "," << pf.curveB[(size_t) i] << "," << juce::String (pf.levelA[(size_t) i], 3) << "," << juce::String (pf.levelB[(size_t) i], 3) << "," << (int) pf.morphOn[(size_t) i] << "," << (int) pf.ampOn[(size_t) i] << ";";
        t.setProperty ("perf" + juce::String (e), perf + juce::String (pf.loopStart) + "," + juce::String (pf.loopEnd), nullptr);
        t.setProperty ("sync" + juce::String (e), juce::String (p.syncNum[(size_t) e]) + "/" + juce::String (p.syncDen[(size_t) e]), nullptr);
    }
    for (int f = 0; f < 2; ++f)
        t.setProperty ("fxSync" + juce::String (f), juce::String (p.fxSync[(size_t) f][0]) + "," + juce::String (p.fxSync[(size_t) f][1]) + "," + juce::String (p.fxSync[(size_t) f][2]) + "," + juce::String (p.fxSync[(size_t) f][3]), nullptr);
    t.setProperty ("uniRange", juce::String (p.uniRangeDown, 3) + "," + juce::String (p.uniRangeUp, 3) + "," + juce::String ((int) p.uniChordMode), nullptr);
    t.setProperty ("bend", juce::String (p.bendDown, 3) + "," + juce::String (p.bendUp, 3), nullptr);
    if (! p.nmsvKeep.empty()) t.setProperty ("nmsvKeep", juce::String::toHexString (p.nmsvKeep.data(), (int) p.nmsvKeep.size(), 0), nullptr);
    t.setProperty ("glideRate", (int) p.glideRateMode, nullptr);
    t.setProperty ("phase", juce::String (p.oscPhase[0], 4) + "," + juce::String (p.oscPhase[1], 4) + "," + juce::String (p.oscPhase[2], 4) + "," + juce::String (p.oscPhase[3], 4) + "," + juce::String ((int) p.restartPhase), nullptr);
    juce::String kt; for (int i = 0; i < 5; ++i) kt << p.keyTrackOsc[(size_t) i] << ","; kt << p.keyTrackFilter[0] << "," << p.keyTrackFilter[1];
    t.setProperty ("keytrack", kt, nullptr);
    { juce::String ie; for (int i = 0; i < 5; ++i) ie << juce::String (p.intAttack[(size_t) i], 4) << "," << juce::String (p.intDecay[(size_t) i], 4) << ","; t.setProperty ("intEnv", ie, nullptr); }
    auto curveStr = [] (const KeyCurve& c) { juce::String s; for (int i = 0; i < 5; ++i) s << juce::String (c.x[(size_t) i], 2) << ":" << juce::String (c.y[(size_t) i], 2) << ","; return s; };
    t.setProperty ("oscCurve", curveStr (p.oscUserCurve), nullptr);
    for (int i = 0; i < 2; ++i) { t.setProperty ("fltCut" + juce::String (i), curveStr (p.filterCurveCut[(size_t) i]), nullptr); t.setProperty ("fltRes" + juce::String (i), curveStr (p.filterCurveRes[(size_t) i]), nullptr); }
    t.setProperty ("tune", p.globalTune, nullptr);
    t.setProperty ("bpm", p.presetBpm, nullptr);
    return t;
}

void AquaMassifProcessor::treeToPatch (const juce::ValueTree& t, Patch& p)
{
    if (! t.isValid()) return;
    p.name = t.getProperty ("name", "Init").toString().toStdString();
    auto names = juce::StringArray::fromTokens (t.getProperty ("macroNames").toString(), "|", "");
    for (int i = 0; i < 8; ++i) p.macroName[(size_t) i] = i < names.size() ? names[i].toStdString() : std::string();
    for (auto& m : p.mod) m = {};
    for (auto& e : juce::StringArray::fromTokens (t.getProperty ("mods").toString(), ";", ""))
    {
        auto f = juce::StringArray::fromTokens (e, ":", "");
        if (f.size() >= 2 && f[1] == "sc")
        {
            const int k = f[0].getIntValue();
            if (k >= 0 && k < kNumKnobs)
            {
                p.mod[(size_t) k].sideChain = true;
                if (f.size() == 3 && f[2].length() == 2) p.mod[(size_t) k].scTarget = { { f[2][0] == '1', f[2][1] == '1' } };
            }
            continue;
        }
        if (f.size() != 4) continue;
        const int k = f[0].getIntValue(), s = f[1].getIntValue();
        if (k >= 0 && k < kNumKnobs && s >= 0 && s < 3) p.mod[(size_t) k].slot[(size_t) s] = { f[2].getIntValue(), f[3].getFloatValue() };
    }
    if (t.hasProperty ("ampMod"))
    {
        p.ampMod = {};
        auto a = juce::StringArray::fromTokens (t.getProperty ("ampMod").toString(), ";", "");
        for (int s = 0; s < juce::jmin (3, a.size()); ++s) { auto f = juce::StringArray::fromTokens (a[s], ":", ""); if (f.size() == 2) p.ampMod[0].slot[(size_t) s] = { f[0].getIntValue(), f[1].getFloatValue() }; }
    }
    auto ints = [] (const juce::var& v) { juce::Array<float> r; for (auto& s : juce::StringArray::fromTokens (v.toString(), ",", "")) r.add (s.getFloatValue()); return r; };
    for (int e = 0; e < 4; ++e)
    {
        auto a = ints (t.getProperty ("env" + juce::String (e)));
        if (a.size() >= 6) { auto& x = p.env[(size_t) e]; x.playMode = (int) a[0]; x.retrigger = a[1] != 0; x.linearDecay = a[2] != 0; x.loopCount = (int) a[3]; x.morphShapeA = (int) a[4]; x.morphShapeB = (int) a[5]; }
        auto l = ints (t.getProperty ("lfo" + juce::String (e)));
        if (l.size() >= 4) { auto& x = p.lfo[(size_t) e]; x.shapeA = juce::jlimit (0, csNum - 1, (int) l[0]); x.shapeB = juce::jlimit (0, csNum - 1, (int) l[1]); x.phaseA = l[2]; x.mono = l[3] != 0; x.phaseB = l.size() >= 5 ? (float) l[4] : 0.0f; }
        auto st = juce::StringArray::fromTokens (t.getProperty ("step" + juce::String (e)).toString(), ";", "");
        if (st.size() >= 17)
        {
            auto& x = p.stepper[(size_t) e];
            for (int i = 0; i < 16; ++i) { auto f = ints (st[i]); if (f.size() >= 3) { x.step[(size_t) i] = f[0]; x.glideOn[(size_t) i] = f[1] != 0; x.ampOn[(size_t) i] = f[2] != 0; } }
            auto le = ints (st[16]); if (le.size() >= 2) { x.loopStart = (int) le[0]; x.loopEnd = (int) le[1]; }
        }
        auto pf = juce::StringArray::fromTokens (t.getProperty ("perf" + juce::String (e)).toString(), ";", "");
        if (pf.size() >= 17)
        {
            auto& x = p.performer[(size_t) e];
            for (int i = 0; i < 16; ++i) { auto f = ints (pf[i]); if (f.size() >= 6) { x.curveA[(size_t) i] = (int) f[0]; x.curveB[(size_t) i] = (int) f[1]; x.levelA[(size_t) i] = f[2]; x.levelB[(size_t) i] = f[3]; x.morphOn[(size_t) i] = f[4] != 0; x.ampOn[(size_t) i] = f[5] != 0; } }
            auto le = ints (pf[16]); if (le.size() >= 2) { x.loopStart = (int) le[0]; x.loopEnd = (int) le[1]; }
        }
        auto sy = juce::StringArray::fromTokens (t.getProperty ("sync" + juce::String (e)).toString(), "/", "");
        if (sy.size() == 2) { p.syncNum[(size_t) e] = juce::jmax (1, sy[0].getIntValue()); p.syncDen[(size_t) e] = juce::jmax (1, sy[1].getIntValue()); }
    }
    { auto a = ints (t.getProperty ("intEnv")); if (a.size() >= 10) for (int i = 0; i < 5; ++i) { p.intAttack[(size_t) i] = a[2 * i]; p.intDecay[(size_t) i] = a[2 * i + 1]; } }
    for (int f = 0; f < 2; ++f) { auto a = ints (t.getProperty ("fxSync" + juce::String (f))); if (a.size() >= 4) for (int i = 0; i < 4; ++i) p.fxSync[(size_t) f][(size_t) i] = juce::jmax (1, (int) a[i]); }
    { auto a = ints (t.getProperty ("uniRange")); if (a.size() >= 3) { p.uniRangeDown = a[0]; p.uniRangeUp = a[1]; p.uniChordMode = a[2] != 0; } }
    { auto a = ints (t.getProperty ("bend")); if (a.size() >= 2) { p.bendDown = a[0]; p.bendUp = a[1]; } }
    {
        const auto hx = t.getProperty ("nmsvKeep").toString();
        p.nmsvKeep.clear();
        for (int i = 0; i + 1 < hx.length(); i += 2) p.nmsvKeep.push_back ((uint8_t) hx.substring (i, i + 2).getHexValue32());
        if (p.nmsvKeep.size() >= 2) p.nmsvKeep.resize (2); else p.nmsvKeep.clear();
    }
    p.glideRateMode = (int) t.getProperty ("glideRate", 0) != 0;
    { auto a = ints (t.getProperty ("phase")); if (a.size() >= 5) { for (int i = 0; i < 4; ++i) p.oscPhase[(size_t) i] = a[i]; p.restartPhase = a[4] != 0; } }
    { auto a = ints (t.getProperty ("keytrack")); if (a.size() >= 7) { for (int i = 0; i < 5; ++i) p.keyTrackOsc[(size_t) i] = (int) a[i]; p.keyTrackFilter[0] = (int) a[5]; p.keyTrackFilter[1] = (int) a[6]; } }
    auto curve = [] (const juce::var& v, KeyCurve& c)
    {
        auto pts = juce::StringArray::fromTokens (v.toString(), ",", "");
        for (int i = 0; i < juce::jmin (5, pts.size()); ++i) { auto xy = juce::StringArray::fromTokens (pts[i], ":", ""); if (xy.size() == 2) { c.x[(size_t) i] = xy[0].getFloatValue(); c.y[(size_t) i] = xy[1].getFloatValue(); } }
    };
    if (t.hasProperty ("oscCurve")) curve (t.getProperty ("oscCurve"), p.oscUserCurve);
    for (int i = 0; i < 2; ++i) { if (t.hasProperty ("fltCut" + juce::String (i))) curve (t.getProperty ("fltCut" + juce::String (i)), p.filterCurveCut[(size_t) i]);
                                  if (t.hasProperty ("fltRes" + juce::String (i))) curve (t.getProperty ("fltRes" + juce::String (i)), p.filterCurveRes[(size_t) i]); }
    p.globalTune = (float) t.getProperty ("tune", 0.0);
    p.presetBpm = (float) t.getProperty ("bpm", 120.0);
}

// the tables of states up to 5.20.2 (16 bit, zipped, in one property): read only
void AquaMassifProcessor::decodeTables (const juce::String& s)
{
    if (s.isEmpty()) return;
    juce::MemoryBlock zipped; if (! zipped.fromBase64Encoding (s)) return;
    juce::MemoryInputStream zin (zipped, false);
    juce::GZIPDecompressorInputStream gz (zin);
    juce::MemoryBlock mb; gz.readIntoMemoryBlock (mb);
    juce::MemoryInputStream in (mb, false);
    const int count = in.readInt();
    std::lock_guard<std::mutex> g (buildMutex);
    const juce::SpinLock::ScopedLockType l (tableLock);
    for (int i = 0; i < count && ! in.isExhausted(); ++i)
    {
        const int tag = in.readInt(), n = in.readInt();
        const int slot = tag & 0xffff, kind = juce::jlimit (1, 2, (tag >> 16) & 0xf); const float div = (float) (1 << ((tag >> 20) & 0xf));
        if (slot < 0 || slot >= 87 || n <= 0 || n > 4 * 1024 * 1024) return;
        std::vector<float> v ((size_t) n);
        for (auto& x : v) x = (float) in.readShort() * 2.0f / 32767.0f;
        // a generated table is the patch's own; a file's copy only fills in where the folder has none
        if (kind == tkGenerated || rawKind[(size_t) slot] == tkStandIn) { rawFrames[(size_t) slot] = std::move (v); rawKind[(size_t) slot] = kind; rawDiv[(size_t) slot] = kind == tkGenerated ? div : 1.0f; built[(size_t) slot] = nullptr; }
    }
    for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
}

//==============================================================================
// the tables a sound plays: a generated one as its recipe, any other as its sound file (in samples/)
juce::ValueTree AquaMassifProcessor::tablesToTree (Files& files)
{
    juce::ValueTree t ("Tables");
    std::set<int> used;
    for (int o = 0; o < 3; ++o) used.insert (juce::jlimit (0, 86, choice ("wt" + juce::String (o + 1))));
    std::set<juce::String> names;
    for (const int s : used)
    {
        int kind; Recipe rc; juce::MemoryBlock bytes; juce::String name; juce::File path; std::vector<float> frames; float div;
        {
            const juce::SpinLock::ScopedLockType l (tableLock);
            kind = rawKind[(size_t) s]; rc = rawRecipe[(size_t) s]; bytes = rawFile[(size_t) s]; name = rawName[(size_t) s];
            path = rawPath[(size_t) s]; div = rawDiv[(size_t) s];
            if (kind != tkStandIn && bytes.isEmpty() && ! path.existsAsFile() && ! (kind == tkGenerated && rc.type >= 0)) frames = rawFrames[(size_t) s];
        }
        if (kind == tkStandIn) continue;
        juce::ValueTree e ("Table");
        e.setProperty ("slot", s, nullptr);
        e.setProperty ("table", juce::String (slotInfos()[(size_t) s].name), nullptr);
        e.setProperty ("kind", kind == tkFile ? "original" : (kind == tkGenerated ? "generated" : "loaded"), nullptr);
        if (kind == tkGenerated && rc.type >= 0)
        {
            e.setProperty ("recipe", rc.type, nullptr);
            e.setProperty ("a", rc.a, nullptr); e.setProperty ("b", rc.b, nullptr); e.setProperty ("c", rc.c, nullptr);
            e.setProperty ("seed", rc.seed, nullptr); e.setProperty ("frames", rc.frames, nullptr);
            t.appendChild (e, nullptr);
            continue;
        }
        if (bytes.isEmpty() && path.existsAsFile() && path.loadFileAsData (bytes)) name = path.getFileName();
        float gain = 1.0f;
        if (bytes.isEmpty())
        {
            if (frames.empty()) continue;
            bytes = framesToWav (frames, gain);
            name = juce::String (slotInfos()[(size_t) s].name) + ".wav";
        }
        if (name.isEmpty()) name = juce::String (slotInfos()[(size_t) s].name) + ".wav";
        name = juce::File::createLegalFileName (name);
        const auto base = name.upToLastOccurrenceOf (".", false, false), ext = name.fromLastOccurrenceOf (".", true, false);
        for (int i = 2; names.count (name.toLowerCase()) > 0; ++i) name = base + " " + juce::String (i) + ext;
        names.insert (name.toLowerCase());
        e.setProperty ("file", "samples/" + name, nullptr);
        if (gain != 1.0f) e.setProperty ("gain", gain, nullptr);
        if (kind == tkGenerated && div != 1.0f) e.setProperty ("div", div, nullptr);
        files.push_back ({ "samples/" + name, bytes });
        t.appendChild (e, nullptr);
    }
    return t;
}

void AquaMassifProcessor::treeToTables (const juce::ValueTree& t, const FileSource& files)
{
    if (! t.isValid()) return;
    for (const auto e : t)
    {
        if (! e.hasType ("Table")) continue;
        const int s = (int) e.getProperty ("slot", -1);
        if (s < 0 || s >= 87) continue;
        const auto kindName = e.getProperty ("kind").toString();
        const int kind = kindName == "original" ? tkFile : (kindName == "generated" ? tkGenerated : tkUser);
        std::vector<float> v; Recipe rc; juce::MemoryBlock bytes; juce::String name;
        if (kind == tkGenerated && e.hasProperty ("recipe"))
        {
            rc = { (int) e.getProperty ("recipe"), (float) e.getProperty ("a"), (float) e.getProperty ("b"), (float) e.getProperty ("c"),
                   (int) e.getProperty ("seed"), (int) e.getProperty ("frames") };
            v = gen::recipe (rc.type, rc.a, rc.b, rc.c, (uint32_t) rc.seed, rc.frames);
        }
        else
        {
            const auto path = e.getProperty ("file").toString();
            if (path.isEmpty() || ! files || ! files (path, bytes)) continue;     // a missing file: that table stays as it is
            v = soundFrames (bytes);
            const float g = (float) e.getProperty ("gain", 1.0);
            if (g != 1.0f) for (auto& x : v) x *= g;
            name = path.fromLastOccurrenceOf ("/", false, false);
        }
        if (v.empty()) continue;
        std::lock_guard<std::mutex> gl (buildMutex);
        const juce::SpinLock::ScopedLockType l (tableLock);
        if (kind == tkFile && rawKind[(size_t) s] == tkFile) continue;        // the folder has it already
        rawFrames[(size_t) s] = std::move (v); rawKind[(size_t) s] = kind; rawRecipe[(size_t) s] = rc;
        rawDiv[(size_t) s] = kind == tkGenerated ? (float) e.getProperty ("div", gen::recipeDiv (rc.type)) : 1.0f;
        rawFile[(size_t) s] = bytes; rawName[(size_t) s] = name; rawPath[(size_t) s] = juce::File();
        built[(size_t) s] = nullptr;
    }
    const juce::SpinLock::ScopedLockType l (tableLock);
    for (int o = 0; o < 3; ++o) { liveSlot[(size_t) o] = -1; pendingTable[(size_t) o] = nullptr; }
}

//==============================================================================
// one container for the presets and the state
juce::MemoryBlock AquaMassifProcessor::writeContainer (bool forPreset)
{
    juce::ValueTree root (forPreset ? "MassifPreset" : "AquaMassif");
    root.setProperty ("version", 2, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    auto p = extra; readParams (p);
    root.appendChild (patchToTree (p), nullptr);
    Files files;
    root.appendChild (tablesToTree (files), nullptr);
    if (! forPreset)
    {
        // this machine's: not in a preset
        root.setProperty ("folder", tableFolder.getFullPathName(), nullptr);
        root.setProperty ("presetFile", lastPresetFile.getFullPathName(), nullptr);
        root.setProperty ("factory", factoryIndex, nullptr);
        root.setProperty ("fromImport", soundFromImport ? 1 : 0, nullptr);
        root.setProperty ("uiMetal", juce::String::toHexString ((int) themeMetal), nullptr);
        root.setProperty ("uiGlow", juce::String::toHexString ((int) themeGlow), nullptr);
        root.setProperty ("genType", genType, nullptr); root.setProperty ("genGenre", genGenre, nullptr);
        if (! bundle.empty()) { root.setProperty ("bundleName", bundleName, nullptr); root.setProperty ("bundleIndex", bundleIndex, nullptr); }
    }
    juce::String text;
    if (auto xml = root.createXml()) text = xml->toString();
    // a fixed time: the same sound gives the same bytes
    const juce::Time when (2026, 0, 1, 0, 0);
    juce::ZipFile::Builder zb;
    zb.addEntry (new juce::MemoryInputStream (text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 6, "massif.xml", when);
    for (auto& [path, bytes] : files) zb.addEntry (new juce::MemoryInputStream (bytes, true), 6, path, when);
    // the bundle (states only): stored as it was packed on import, not packed again
    if (! forPreset && ! bundle.empty() && bundleZip.getSize() > 0)
        zb.addEntry (new juce::MemoryInputStream (bundleZip, false), 0, "bundle.zip", when);
    juce::MemoryBlock mb;
    {
        juce::MemoryOutputStream mos (mb, false);
        zb.writeToStream (mos, nullptr);
    }
    return mb;
}

bool AquaMassifProcessor::readContainer (const juce::MemoryBlock& data, bool fromPreset, juce::String& error)
{
    const auto* d = static_cast<const char*> (data.getData());
    if (data.getSize() >= 4 && std::memcmp (d, "PK\x03\x04", 4) == 0)
    {
        juce::MemoryInputStream in (data, false);
        juce::ZipFile zip (in);
        const int at = zip.getIndexOfFileName ("massif.xml");
        if (at < 0) { error = "Not a Massif preset."; return false; }
        std::unique_ptr<juce::InputStream> xs (zip.createStreamForEntry (at));
        auto xml = xs != nullptr ? juce::parseXML (xs->readEntireStreamAsString()) : nullptr;
        if (xml == nullptr) { error = "Not a Massif preset."; return false; }
        const FileSource files = [&zip] (const juce::String& path, juce::MemoryBlock& out)
        {
            const int i = zip.getIndexOfFileName (path);
            if (i < 0) return false;
            std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i));
            if (s == nullptr) return false;
            out.reset(); s->readIntoMemoryBlock (out);
            return out.getSize() > 0;
        };
        const auto root = juce::ValueTree::fromXml (*xml);
        if (! restoreTree (root, fromPreset, files, error)) return false;
        if (! fromPreset)
        {
            juce::MemoryBlock bz; std::vector<BundleSound> b;
            if (files ("bundle.zip", bz) && readBundleZip (bz, b) && ! b.empty())
            {
                bundle = std::move (b); bundleZip = std::move (bz);
                bundleName = root.getProperty ("bundleName").toString();
                bundleIndex = juce::jlimit (-1, (int) bundle.size() - 1, (int) root.getProperty ("bundleIndex", -1));
            }
            else clearBundle();
        }
        return true;
    }
    // the older forms: an XML text (presets), an XML binary (states)
    std::unique_ptr<juce::XmlElement> xml;
    if (data.getSize() >= 4 && std::memcmp (d, "VC2!", 4) == 0) xml = getXmlFromBinary (data.getData(), (int) data.getSize());
    else xml = juce::parseXML (data.toString());
    if (xml == nullptr) { error = "Not a Massif preset."; return false; }
    return restoreTree (juce::ValueTree::fromXml (*xml), fromPreset, {}, error);
}

bool AquaMassifProcessor::restoreTree (const juce::ValueTree& root, bool fromPreset, const FileSource& files, juce::String& error)
{
    if (! root.hasType ("MassifPreset") && ! root.hasType ("AquaMassifPreset") && ! root.hasType ("AquaMassif")) { error = "Not a Massif preset."; return false; }
    auto params = root.getChildWithName (apvts.state.getType());
    if (! params.isValid()) { error = "Not a Massif preset."; return false; }
    if (! fromPreset)
    {
        const juce::File folder (root.getProperty ("folder").toString());
        if (folder != tableFolder && folder.isDirectory()) setWavetableFolder (folder);
    }
    releaseOwnTables();
    decodeTables (root.getProperty ("tables").toString());     // states up to 5.20.2
    treeToTables (root.getChildWithName ("Tables"), files);
    apvts.replaceState (params);
    Patch p; treeToPatch (root.getChildWithName ("Extra"), p);
    extra = p; extraChanged();
    if (fromPreset) { factoryIndex = -1; soundFromImport = false; return true; }
    lastPresetFile = juce::File (root.getProperty ("presetFile").toString());
    factoryIndex = (int) root.getProperty ("factory", -1);
    soundFromImport = (int) root.getProperty ("fromImport", 0) != 0;
    importTemplate.reset();
    if (root.hasProperty ("uiMetal")) themeMetal = (juce::uint32) root.getProperty ("uiMetal").toString().getHexValue32() | 0xff000000u;
    if (root.hasProperty ("uiGlow"))  themeGlow  = (juce::uint32) root.getProperty ("uiGlow").toString().getHexValue32() | 0xff000000u;
    // a state from the first 5.20.0 build carries that build's default colours: they mean "the default"
    // earlier defaults become today's (the first colourway, then AlphaBeta's blue): the AlphaBeta grey and the Aquanode cyan
    if (themeMetal == 0xffa4b5beu && themeGlow == 0xff3fe0f5u) { themeMetal = 0xffb8bdc8u; themeGlow = 0xff3fe0f5u; }
    if (themeMetal == 0xffb8bdc8u && themeGlow == 0xff3ab0ffu) themeGlow = 0xff3fe0f5u;
    genType = juce::jlimit (0, (int) massif::generator::tAnything, (int) root.getProperty ("genType", genType));
    genGenre = juce::jlimit (0, massif::generator::gNumGenres - 1, (int) root.getProperty ("genGenre", genGenre));
    return true;
}

void AquaMassifProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    destData = writeContainer (false);
}

void AquaMassifProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;
    juce::String err;
    readContainer (juce::MemoryBlock (data, (size_t) sizeInBytes), false, err);
}

//==============================================================================
bool AquaMassifProcessor::loadPresetFile (const juce::File& f, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) { error = "Can't read the file."; return false; }
    if (f.hasFileExtension (".nmsv")) return importNmsvData (mb, f.getFileNameWithoutExtension(), f, error);
    if (! loadPresetData (mb, f.getFileNameWithoutExtension(), error)) return false;
    lastPresetFile = f;
    return true;
}

bool AquaMassifProcessor::loadPresetData (const juce::MemoryBlock& data, const juce::String& name, juce::String& error)
{
    if (! readContainer (data, true, error)) return false;
    if (name.isNotEmpty()) extra.name = name.toStdString();
    extraChanged();
    lastPresetFile = juce::File(); bundleIndex = -1;
    return true;
}

juce::MemoryBlock AquaMassifProcessor::presetData()
{
    return writeContainer (true);
}

bool AquaMassifProcessor::savePresetFile (const juce::File& f)
{
    const auto name = f.getFileNameWithoutExtension();
    const auto keep = extra.name; extra.name = name.toStdString();
    const auto mb = presetData();
    if (! f.replaceWithData (mb.getData(), mb.getSize())) { extra.name = keep; return false; }
    presetSaved (f, name);
    return true;
}

void AquaMassifProcessor::presetSaved (const juce::File& f, const juce::String& name)
{
    extra.name = name.toStdString(); extraChanged();
    lastPresetFile = f;
}

//==============================================================================
// the original's sound files
bool AquaMassifProcessor::importNmsvData (const juce::MemoryBlock& data, const juce::String& name, const juce::File& from, juce::String& error)
{
    std::vector<uint8_t> bytes ((const uint8_t*) data.getData(), (const uint8_t*) data.getData() + data.getSize());
    Patch p; std::string why;
    if (! importNmsv (bytes, p, &why)) { error = "This file could not be read as a sound (" + juce::String (why) + ")."; return false; }
    p.name = name.toStdString();
    releaseOwnTables();
    applyPatch (p);
    importTemplate = data; soundFromImport = true;
    lastPresetFile = from; factoryIndex = -1; bundleIndex = -1;
    return true;
}

//==============================================================================
// bundles of the original's sounds
bool AquaMassifProcessor::isBundleZip (const juce::MemoryBlock& zipData)
{
    if (zipData.getSize() < 4 || std::memcmp (zipData.getData(), "PK\x03\x04", 4) != 0) return false;
    juce::MemoryInputStream in (zipData, false);
    juce::ZipFile zip (in);
    if (zip.getIndexOfFileName ("massif.xml") >= 0) return false;
    for (int i = 0; i < zip.getNumEntries(); ++i)
        if (zip.getEntry (i)->filename.endsWithIgnoreCase (".nmsv")) return true;
    return false;
}

bool AquaMassifProcessor::readBundleZip (const juce::MemoryBlock& zipData, std::vector<BundleSound>& out) const
{
    out.clear();
    if (zipData.getSize() < 4 || std::memcmp (zipData.getData(), "PK\x03\x04", 4) != 0) return false;
    juce::MemoryInputStream in (zipData, false);
    juce::ZipFile zip (in);
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        const auto path = e->filename.replaceCharacter ('\\', '/');
        if (! path.endsWithIgnoreCase (".nmsv") || e->uncompressedSize <= 0 || e->uncompressedSize > (1 << 22)) continue;
        if (path.contains ("__MACOSX/") || path.fromLastOccurrenceOf ("/", false, false).startsWith ("._")) continue;
        std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i));
        if (s == nullptr) continue;
        BundleSound b;
        s->readIntoMemoryBlock (b.data);
        if (b.data.getSize() < 64) continue;
        b.name = path.fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
        b.folder = path.upToLastOccurrenceOf ("/", false, false);
        out.push_back (std::move (b));
    }
    // folders first, then the names (natural order: "Pad 2" before "Pad 10")
    std::sort (out.begin(), out.end(), [] (const BundleSound& a, const BundleSound& b)
    {
        const int f = a.folder.compareNatural (b.folder);
        return f != 0 ? f < 0 : a.name.compareNatural (b.name) < 0;
    });
    return ! out.empty();
}

int AquaMassifProcessor::importBundle (const juce::MemoryBlock& zipData, const juce::String& name, juce::String& error)
{
    std::vector<BundleSound> b;
    if (! readBundleZip (zipData, b)) { error = "There are no .nmsv sounds in this zip."; return 0; }
    // kept with the state: only the sounds, packed once (the zip may hold more than them)
    const juce::Time when (2026, 0, 1, 0, 0);
    juce::ZipFile::Builder zb;
    for (auto& s : b)
        zb.addEntry (new juce::MemoryInputStream (s.data, false), 6,
                     (s.folder.isNotEmpty() ? s.folder + "/" : juce::String()) + s.name + ".nmsv", when);
    juce::MemoryBlock packed;
    { juce::MemoryOutputStream mos (packed, false); zb.writeToStream (mos, nullptr); }
    bundle = std::move (b); bundleZip = std::move (packed); bundleName = name;
    bundleIndex = -1;
    juce::String err;
    for (int i = 0; i < (int) bundle.size(); ++i)
        if (loadBundleSound (i, err)) break;     // the first one that reads
    return (int) bundle.size();
}

bool AquaMassifProcessor::loadBundleSound (int index, juce::String& error)
{
    if (index < 0 || index >= (int) bundle.size()) { error = "No such sound in the bundle."; return false; }
    const auto& s = bundle[(size_t) index];
    if (! importNmsvData (s.data, s.name, juce::File(), error)) return false;
    bundleIndex = index;
    return true;
}

void AquaMassifProcessor::clearBundle()
{
    bundle.clear(); bundleZip.reset(); bundleName.clear(); bundleIndex = -1;
}

bool AquaMassifProcessor::hasExportTemplate() const
{
    return importTemplate.getSize() > 0 || exportTemplateFile().existsAsFile() || (soundFromImport && lastPresetFile.existsAsFile());
}

bool AquaMassifProcessor::setExportTemplate (const juce::MemoryBlock& nmsv, juce::String& error)
{
    std::vector<uint8_t> bytes ((const uint8_t*) nmsv.getData(), (const uint8_t*) nmsv.getData() + nmsv.getSize());
    Patch p; std::string why;
    if (! importNmsv (bytes, p, &why)) { error = "This file could not be read as a sound (" + juce::String (why) + ")."; return false; }
    const auto f = exportTemplateFile();
    if (! f.getParentDirectory().createDirectory() || ! f.replaceWithData (nmsv.getData(), nmsv.getSize())) { error = "The template could not be kept."; return false; }
    return true;
}

bool AquaMassifProcessor::exportNmsvData (juce::MemoryBlock& out, juce::String& error, const juce::String& name)
{
    // the template: the sound's own file while the sound is the one imported, else the export template
    juce::MemoryBlock t;
    if (soundFromImport && importTemplate.getSize() > 0) t = importTemplate;
    else if (soundFromImport && lastPresetFile.hasFileExtension (".nmsv") && lastPresetFile.loadFileAsData (t) && t.getSize() > 0) {}
    else if (exportTemplateFile().loadFileAsData (t) && t.getSize() > 0) {}
    else if (importTemplate.getSize() > 0) t = importTemplate;
    else { error = "Massif writes .nmsv files over one of yours: import any .nmsv sound once (an init sound is best), or choose an export template."; return false; }
    Patch q = currentPatch();
    if (name.isNotEmpty()) q.name = name.toStdString();
    // the original has a fixed set of tables: a generated or loaded one is written as the default table
    const Patch d;
    for (int o = 0; o < 3; ++o)
    {
        const int k = slotKind (q.wavetable[(size_t) o]);
        if (k == tkGenerated || k == tkUser) q.wavetable[(size_t) o] = d.wavetable[(size_t) o];
    }
    std::vector<uint8_t> tv ((const uint8_t*) t.getData(), (const uint8_t*) t.getData() + t.getSize()), ov;
    std::string why;
    if (! massif::exportNmsv (q, tv, ov, &why)) { error = "The sound could not be written (" + juce::String (why) + ")."; return false; }
    out = juce::MemoryBlock (ov.data(), ov.size());
    return true;
}

juce::AudioProcessorEditor* AquaMassifProcessor::createEditor() { return new AquaMassifEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AquaMassifProcessor(); }
