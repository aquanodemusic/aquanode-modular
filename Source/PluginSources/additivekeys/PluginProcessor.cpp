#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
K5AudioProcessor::K5AudioProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    resetHarmonics (0);
    resetHarmonics (1);

    for (int i = 0; i < numVoices; ++i)
        { auto* v = new K5SynthVoice(); v->mpe = &mpeState; synth.addVoice (v); }

    synth.addSound (new K5Sound());

    cacheParameterPointers();

    // One listener per parameter, all of them just raising a dirty flag. The
    // actual work happens once, at the top of the next processBlock.
    for (auto* parameter : getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.addParameterListener (withID->paramID, this);
}

K5AudioProcessor::~K5AudioProcessor()
{
    for (auto* parameter : getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.removeParameterListener (withID->paramID, this);
}

//==============================================================================
void K5AudioProcessor::cacheParameterPointers()
{
    auto raw = [this] (const juce::String& id) -> std::atomic<float>*
    {
        auto* pointer = apvts.getRawParameterValue (id);
        jassert (pointer != nullptr);   // an ID in createLayout() got renamed
        return pointer;
    };

    for (int s = 0; s < 2; ++s)
    {
        const juce::String prefix = (s == 0 ? "s1_" : "s2_");
        auto& p = sourceParams[(size_t) s];

        p.level  = raw (prefix + "level");
        p.detune = raw (prefix + "detune");

        p.pitchA     = raw (prefix + "pitchA");
        p.pitchD     = raw (prefix + "pitchD");
        p.pitchS     = raw (prefix + "pitchS");
        p.pitchR     = raw (prefix + "pitchR");
        p.pitchDepth = raw (prefix + "pitchDepth");

        p.ampA = raw (prefix + "ampA");
        p.ampD = raw (prefix + "ampD");
        p.ampS = raw (prefix + "ampS");
        p.ampR = raw (prefix + "ampR");

        p.cutoff        = raw (prefix + "cutoff");
        p.resonance     = raw (prefix + "resonance");
        p.filtEnvAmount = raw (prefix + "filtEnvAmount");
        p.slope24       = raw (prefix + "slope24");
        p.filtA         = raw (prefix + "filtA");
        p.filtD         = raw (prefix + "filtD");
        p.filtS         = raw (prefix + "filtS");
        p.filtR         = raw (prefix + "filtR");

        p.harmMode = raw (prefix + "harmMode");
        p.harmTilt = raw (prefix + "harmTilt");

        for (int g = 0; g < HarmonicGenerator::numEnvGroups; ++g)
        {
            const auto gp = prefix + "grp" + juce::String (g) + "_";
            p.group[(size_t) g].a = raw (gp + "A");
            p.group[(size_t) g].d = raw (gp + "D");
            p.group[(size_t) g].s = raw (gp + "S");
            p.group[(size_t) g].r = raw (gp + "R");
        }
    }

    globalParams.masterGain   = raw ("masterGain");
    globalParams.lfoShape     = raw ("lfoShape");
    globalParams.lfoRate      = raw ("lfoRate");
    globalParams.lfoDelay     = raw ("lfoDelay");
    globalParams.lfoVibrato   = raw ("lfoVibrato");
    globalParams.lfoTremolo   = raw ("lfoTremolo");
    globalParams.lfoFilterMod = raw ("lfoFilterMod");

    for (int b = 0; b < DigitalFormantFilter::numBands; ++b)
    {
        const auto bp = "dftBand" + juce::String (b) + "_";
        globalParams.band[(size_t) b].gain = raw (bp + "gain");
        globalParams.band[(size_t) b].freq = raw (bp + "freq");
        globalParams.band[(size_t) b].q    = raw (bp + "q");
    }
}

//==============================================================================
void K5AudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<K5SynthVoice*> (synth.getVoice (i)))
        {
            v->prepare (sampleRate);
            v->voice.setSharedDFT (&sharedDFT);
        }

    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (globalParams.masterGain->load());

    parametersDirty.store (true, std::memory_order_release);
}

void K5AudioProcessor::updateSource (K5Source& source, const SourceParamPtrs& p)
{
    // EnvShape is a fixed-size struct, so nothing here touches the heap.
    EnvShape attack, release;

    source.setLevel (p.level->load());
    source.setDetuneCents (p.detune->load());

    // DFG — pitch envelope.
    buildADSRStages (p.pitchA->load(), p.pitchD->load(), p.pitchS->load(), p.pitchR->load(),
                     attack, release);
    source.setPitchEnvelope (attack, release, p.pitchDepth->load());

    // DDA — amplitude envelope.
    buildADSRStages (p.ampA->load(), p.ampD->load(), p.ampS->load(), p.ampR->load(),
                     attack, release);
    source.setAmpEnvelope (attack, release);

    // DDF — filter and its own envelope.
    {
        auto& ddf = source.getDDF();
        buildADSRStages (p.filtA->load(), p.filtD->load(), p.filtS->load(), p.filtR->load(),
                         attack, release);
        ddf.setEnvelopeStages (attack, release);
        ddf.setParams (p.cutoff->load(), p.resonance->load(),
                       p.filtEnvAmount->load(), p.slope24->load() > 0.5f);
    }

    // DHG — 63 harmonics grouped onto 4 envelope busses.
    //
    // Rather than exposing 63 raw level sliders per source, harmonic content
    // is shaped by "tilt" (spectral slope) plus the hardware harmonic MODE,
    // which is how a K5 patch is normally programmed from the front panel for
    // anything short of painstaking custom spectra. A future editor could add
    // a per-harmonic bar graph calling dhg.setHarmonic() directly.
    {
        auto& dhg = source.getDHG();

        for (int g = 0; g < HarmonicGenerator::numEnvGroups; ++g)
        {
            const auto& gp = p.group[(size_t) g];
            buildADSRStages (gp.a->load(), gp.d->load(), gp.s->load(), gp.r->load(),
                             attack, release);
            dhg.setEnvelopeStages (g, attack, release);
        }

        dhg.setMode (static_cast<HarmonicGenerator::HarmonicMode> ((int) p.harmMode->load()));

        // the drawn levels, sloped by Tilt (-1 bass-heavy .. +1 bright): with
        // the default 1/n levels this is the old (1/n)^(1 - tilt)
        const float tilt = juce::jmin (0.95f, p.harmTilt->load());
        const int src = (&p == &sourceParams[0]) ? 0 : 1;
        for (int h = 0; h < HarmonicGenerator::numHarmonics; ++h)
        {
            const int harmonicNumber = h + 1;
            const float level = harmonics[(size_t) src].level[(size_t) h].load (std::memory_order_relaxed)
                                  * std::pow ((float) harmonicNumber, tilt);
            dhg.setHarmonic (h, juce::jmin (1.0f, level),
                             harmonics[(size_t) src].group[(size_t) h].load (std::memory_order_relaxed));
        }
    }
}

void K5AudioProcessor::updateVoiceGlobals (K5Voice& voice)
{
    auto& lfo = voice.getLFO();
    lfo.setShape (static_cast<LFO::Shape> ((int) globalParams.lfoShape->load()));
    lfo.setRateHz (globalParams.lfoRate->load());
    lfo.setDelaySeconds (globalParams.lfoDelay->load());

    voice.setModRouting (globalParams.lfoVibrato->load(),
                         globalParams.lfoTremolo->load(),
                         globalParams.lfoFilterMod->load());
}

void K5AudioProcessor::updateSharedDFT()
{
    for (int b = 0; b < DigitalFormantFilter::numBands; ++b)
    {
        const auto& band = globalParams.band[(size_t) b];
        sharedDFT.setBand (b, band.freq->load(), band.gain->load(), band.q->load());
    }

    // Rebuilds the 1024-point lookup table only if a band actually moved.
    sharedDFT.updateIfNeeded();
}

void K5AudioProcessor::applyParametersToVoices()
{
    updateSharedDFT();

    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<K5SynthVoice*> (synth.getVoice (i)))
        {
            updateSource (v->voice.getS1(), sourceParams[0]);
            updateSource (v->voice.getS2(), sourceParams[1]);
            updateVoiceGlobals (v->voice);
        }
    }
}

//==============================================================================
void K5AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Only touch the voices when something actually changed. Previously this
    // ran every block for all 16 voices and allocated a std::vector per
    // envelope while doing it — roughly 350 heap allocations per buffer,
    // inside the real-time callback.
    if (parametersDirty.exchange (false, std::memory_order_acquire))
        applyParametersToVoices();

    buffer.clear();
    if (mpeState.on)   // MPE: each member channel's pressure, for the notes it starts
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isChannelPressure() && msg.getChannel() >= 2)
                mpeState.press[(size_t) msg.getChannel()] = (float) msg.getChannelPressureValue() / 127.0f;
        }
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    // Master trim — 16 voices x 2 sources x 63 partials adds up fast.
    masterGain.setTargetValue (globalParams.masterGain->load());
    masterGain.applyGain (buffer, buffer.getNumSamples());
}

//==============================================================================
juce::AudioProcessorEditor* K5AudioProcessor::createEditor()
{
    return new K5AudioProcessorEditor (*this);
}

void K5AudioProcessor::setHarmonic (int source, int h, float level, int group)
{
    if (! juce::isPositiveAndBelow (source, 2) || ! juce::isPositiveAndBelow (h, numHarm))
        return;
    harmonics[(size_t) source].level[(size_t) h].store (juce::jlimit (0.0f, 1.0f, level));
    harmonics[(size_t) source].group[(size_t) h].store (juce::jlimit (0, 3, group));
    markDirty();
}

void K5AudioProcessor::resetHarmonics (int source)
{
    for (int h = 0; h < numHarm; ++h)
        setHarmonic (source, h, 1.0f / (float) (h + 1), h % 4);
}

juce::ValueTree K5AudioProcessor::harmonicsToTree() const
{
    juce::ValueTree t ("Harmonics");
    for (int src = 0; src < 2; ++src)
    {
        juce::StringArray lv, gr;
        for (int h = 0; h < numHarm; ++h)
        {
            lv.add (juce::String (harmonics[(size_t) src].level[(size_t) h].load(), 4));
            gr.add (juce::String (harmonics[(size_t) src].group[(size_t) h].load()));
        }
        juce::ValueTree s ("Source");
        s.setProperty ("index", src + 1, nullptr);
        s.setProperty ("levels", lv.joinIntoString (","), nullptr);
        s.setProperty ("groups", gr.joinIntoString (","), nullptr);
        t.appendChild (s, nullptr);
    }
    return t;
}

void K5AudioProcessor::harmonicsFromTree (const juce::ValueTree& t)
{
    resetHarmonics (0);
    resetHarmonics (1);
    if (! t.isValid())
        return;
    for (const auto& s : t)
    {
        const int src = (int) s.getProperty ("index", 0) - 1;
        if (! juce::isPositiveAndBelow (src, 2))
            continue;
        const auto lv = juce::StringArray::fromTokens (s.getProperty ("levels").toString(), ",", "");
        const auto gr = juce::StringArray::fromTokens (s.getProperty ("groups").toString(), ",", "");
        for (int h = 0; h < numHarm; ++h)
            setHarmonic (src, h,
                         h < lv.size() ? lv[h].getFloatValue() : 1.0f / (float) (h + 1),
                         h < gr.size() ? gr[h].getIntValue() : h % 4);
    }
}

void K5AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        state.removeChild (state.getChildWithName ("Harmonics"), nullptr);
        state.appendChild (harmonicsToTree(), nullptr);
        state.setProperty ("soundName", soundName, nullptr);
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }
}

void K5AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            harmonicsFromTree (tree.getChildWithName ("Harmonics"));   // older states: the default harmonics
            setSoundName (tree.getProperty ("soundName", "Init").toString());
            tree.removeChild (tree.getChildWithName ("Harmonics"), nullptr);
            tree.removeProperty ("soundName", nullptr);
            apvts.replaceState (tree);
        }
    parametersDirty.store (true, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout K5AudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto addFloat = [&params] (const juce::String& id, const juce::String& name,
                               juce::NormalisableRange<float> range, float def)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def));
    };

    auto addChoice = [&params] (const juce::String& id, const juce::String& name,
                                const juce::StringArray& choices, int def)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id, 1 }, name, choices, def));
    };

    // ---- Per-source (S1 / S2) ----
    for (auto prefix : { juce::String ("s1_"), juce::String ("s2_") })
    {
        const auto label = prefix.upToFirstOccurrenceOf ("_", false, false).toUpperCase();

        addFloat (prefix + "level",  label + " Level",  { 0.0f, 1.0f }, 0.8f);
        addFloat (prefix + "detune", label + " Detune (cents)", { -50.0f, 50.0f }, 0.0f);

        addFloat (prefix + "pitchA", label + " Pitch Env Attack",  { 0.001f, 5.0f, 0.0f, 0.4f }, 0.01f);
        addFloat (prefix + "pitchD", label + " Pitch Env Decay",   { 0.001f, 5.0f, 0.0f, 0.4f }, 0.2f);
        addFloat (prefix + "pitchS", label + " Pitch Env Sustain", { 0.0f, 1.0f }, 0.5f);
        addFloat (prefix + "pitchR", label + " Pitch Env Release", { 0.001f, 5.0f, 0.0f, 0.4f }, 0.1f);
        addFloat (prefix + "pitchDepth", label + " Pitch Env Depth (semitones)", { 0.0f, 24.0f }, 0.0f);

        addFloat (prefix + "ampA", label + " Amp Attack",  { 0.001f, 8.0f, 0.0f, 0.4f }, 0.005f);
        addFloat (prefix + "ampD", label + " Amp Decay",   { 0.001f, 8.0f, 0.0f, 0.4f }, 0.3f);
        addFloat (prefix + "ampS", label + " Amp Sustain", { 0.0f, 1.0f }, 0.8f);
        addFloat (prefix + "ampR", label + " Amp Release", { 0.001f, 8.0f, 0.0f, 0.4f }, 0.3f);

        addFloat (prefix + "cutoff",    label + " DDF Cutoff",    { 20.0f, 18000.0f, 0.0f, 0.3f }, 4000.0f);
        addFloat (prefix + "resonance", label + " DDF Resonance", { 0.0f, 0.99f }, 0.1f);
        addFloat (prefix + "filtEnvAmount", label + " DDF Env Amount", { -1.0f, 1.0f }, 0.0f);
        addChoice (prefix + "slope24", label + " DDF Slope", { "12 dB/oct", "24 dB/oct" }, 0);
        addFloat (prefix + "filtA", label + " DDF Env Attack",  { 0.001f, 5.0f, 0.0f, 0.4f }, 0.01f);
        addFloat (prefix + "filtD", label + " DDF Env Decay",   { 0.001f, 5.0f, 0.0f, 0.4f }, 0.2f);
        addFloat (prefix + "filtS", label + " DDF Env Sustain", { 0.0f, 1.0f }, 0.3f);
        addFloat (prefix + "filtR", label + " DDF Env Release", { 0.001f, 5.0f, 0.0f, 0.4f }, 0.2f);

        addChoice (prefix + "harmMode", label + " Harmonic Mode", { "All", "Odd", "Even", "Octave", "Fifth" }, 0);
        addFloat (prefix + "harmTilt", label + " Harmonic Tilt", { -1.0f, 1.0f }, 0.0f);

        for (int g = 0; g < HarmonicGenerator::numEnvGroups; ++g)
        {
            const auto gp = prefix + "grp" + juce::String (g) + "_";
            const auto gLabel = label + " Harm Bus " + juce::String (g + 1);
            addFloat (gp + "A", gLabel + " Attack",  { 0.001f, 5.0f, 0.0f, 0.4f }, 0.01f);
            addFloat (gp + "D", gLabel + " Decay",   { 0.001f, 5.0f, 0.0f, 0.4f }, 0.3f);
            addFloat (gp + "S", gLabel + " Sustain", { 0.0f, 1.0f }, 0.8f);
            addFloat (gp + "R", gLabel + " Release", { 0.001f, 5.0f, 0.0f, 0.4f }, 0.3f);
        }
    }

    // ---- Master ----
    addFloat ("masterGain", "Master Gain", { 0.0f, 1.5f }, 0.7f);

    // ---- Global LFO ----
    addChoice ("lfoShape", "LFO Shape", { "Triangle", "Saw", "Square", "Random" }, 0);
    addFloat ("lfoRate",  "LFO Rate (Hz)", { 0.02f, 20.0f, 0.0f, 0.4f }, 4.0f);
    addFloat ("lfoDelay", "LFO Delay (s)", { 0.0f, 5.0f }, 0.0f);
    addFloat ("lfoVibrato",   "LFO -> Vibrato (semitones)", { 0.0f, 12.0f }, 0.0f);
    addFloat ("lfoTremolo",   "LFO -> Tremolo (depth)",     { 0.0f, 1.0f }, 0.0f);
    addFloat ("lfoFilterMod", "LFO -> Filter (octaves)",    { 0.0f, 4.0f }, 0.0f);

    // ---- Shared DFT (11-band formant filter) ----
    const float defaultFreqs[DigitalFormantFilter::numBands] =
        { 250.0f, 400.0f, 600.0f, 850.0f, 1150.0f, 1500.0f, 1950.0f, 2500.0f, 3200.0f, 4100.0f, 5300.0f };

    for (int b = 0; b < DigitalFormantFilter::numBands; ++b)
    {
        const auto bp = "dftBand" + juce::String (b) + "_";
        const auto bLabel = "DFT Band " + juce::String (b + 1);
        addFloat (bp + "gain", bLabel + " Gain", { 0.0f, 1.0f }, 0.0f);
        addFloat (bp + "freq", bLabel + " Freq", { 80.0f, 12000.0f, 0.0f, 0.3f }, defaultFreqs[(size_t) b]);
        addFloat (bp + "q",    bLabel + " Q",    { 0.0f, 0.99f }, 0.6f);
    }

    return { params.begin(), params.end() };
}

//==============================================================================
// Presets
//==============================================================================
std::unique_ptr<juce::XmlElement> PresetManager::toXml (const juce::String& name) const
{
    auto xml = std::make_unique<juce::XmlElement> (presetTag);
    xml->setAttribute ("name", name);
    for (auto* prm : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (ranged == nullptr)
            continue;
        auto* e = xml->createNewChildElement ("PARAM");
        e->setAttribute ("id", ranged->paramID);
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged))
            e->setAttribute ("value", choice->getIndex());
        else
            e->setAttribute ("value", (double) ranged->convertFrom0to1 (ranged->getValue()));
    }
    const auto h = processor.harmonicsToTree();
    for (const auto& s : h)
    {
        auto* e = xml->createNewChildElement ("HARMONICS");
        e->setAttribute ("source", (int) s.getProperty ("index"));
        e->setAttribute ("levels", s.getProperty ("levels").toString());
        e->setAttribute ("groups", s.getProperty ("groups").toString());
    }
    return xml;
}

void PresetManager::resetToDefaults()
{
    for (auto* prm : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    processor.resetHarmonics (0);
    processor.resetHarmonics (1);
    processor.setSoundName ("Init");
}

bool PresetManager::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (presetTag))
        return false;
    resetToDefaults();
    juce::ValueTree h ("Harmonics");
    for (auto* child : xml.getChildIterator())
    {
        if (child->hasTagName ("PARAM"))
        {
            if (auto* prm = processor.apvts.getParameter (child->getStringAttribute ("id")))
            {
                const float v = (float) child->getDoubleAttribute ("value");
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            }
        }
        else if (child->hasTagName ("HARMONICS"))
        {
            juce::ValueTree s ("Source");
            s.setProperty ("index", child->getIntAttribute ("source"), nullptr);
            s.setProperty ("levels", child->getStringAttribute ("levels"), nullptr);
            s.setProperty ("groups", child->getStringAttribute ("groups"), nullptr);
            h.appendChild (s, nullptr);
        }
    }
    processor.harmonicsFromTree (h.getNumChildren() > 0 ? h : juce::ValueTree());
    processor.setSoundName (xml.getStringAttribute ("name", "Preset"));
    processor.markDirty();
    return true;
}

bool PresetManager::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! fromXml (*xml))
        return false;
    if (xml->getStringAttribute ("name").isEmpty())
        processor.setSoundName (file.getFileNameWithoutExtension());
    return true;
}

bool PresetManager::savePreset (const juce::File& file, const juce::String& name)
{
    auto xml = toXml (name.isNotEmpty() ? name : file.getFileNameWithoutExtension());
    return xml != nullptr && xml->writeTo (file);
}

#include "PresetMaker.h"

void K5AudioProcessor::rollPreset (int kind, juce::uint32 seed)
{
    juce::Random r ((juce::int64) seed);
    akmaker::Maker m { *this, r };
    kind = juce::jlimit (0, akmaker::kinds().size() - 1, kind);
    m.make (kind, r.nextInt (2));
    setSoundName (akmaker::kinds()[kind] + " " + juce::String ((int) (seed % 1000u)).paddedLeft ('0', 3));
    matchLoudness();
}

void K5AudioProcessor::loadFactory (int index)
{
    const auto& f = akmaker::factory();
    if (! juce::isPositiveAndBelow (index, (int) f.size()))
        return;
    juce::Random r ((juce::int64) f[(size_t) index].seed);
    akmaker::Maker m { *this, r };
    m.make (f[(size_t) index].kind, f[(size_t) index].variant);
    setSoundName (f[(size_t) index].name);
    matchLoudness();
}

juce::StringArray K5AudioProcessor::presetKinds() { return akmaker::kinds(); }

juce::StringArray K5AudioProcessor::factoryNames()
{
    juce::StringArray n;
    for (const auto& f : akmaker::factory())
        n.add (f.name);
    return n;
}

// Plays a held middle C through a copy of the sound and sets Master Gain so
// its loudest 100 ms sit at the same level (-15 dBFS RMS) for every sound.
void K5AudioProcessor::matchLoudness()
{
    juce::MemoryBlock state;
    getStateInformation (state);
    K5AudioProcessor tmp;
    tmp.setStateInformation (state.getData(), (int) state.getSize());
    if (auto* g = tmp.apvts.getParameter ("masterGain"))
        g->setValueNotifyingHost (g->convertTo0to1 (1.0f));

    constexpr double sr = 44100.0;
    constexpr int block = 512;
    tmp.setPlayConfigDetails (0, 2, sr, block);
    tmp.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buf (2, block);
    const int total = (int) (sr * 2.5), offAt = (int) (sr * 2.0);
    const int win = (int) (sr * 0.1);
    double winSum = 0.0, loudest = 0.0;
    int winCount = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        if (offAt >= pos && offAt < pos + block) midi.addEvent (juce::MidiMessage::noteOff (1, 60), offAt - pos);
        buf.clear();
        tmp.processBlock (buf, midi);
        const float* d = buf.getReadPointer (0);
        for (int i = 0; i < block; ++i)
        {
            winSum += (double) d[i] * d[i];
            if (++winCount == win)
            {
                loudest = juce::jmax (loudest, std::sqrt (winSum / win));
                winSum = 0.0;
                winCount = 0;
            }
        }
    }
    if (loudest < 1.0e-5)
        return;
    const float gain = juce::jlimit (0.02f, 1.5f, (float) (0.178 / loudest));
    if (auto* g = apvts.getParameter ("masterGain"))
        g->setValueNotifyingHost (g->convertTo0to1 (gain));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new K5AudioProcessor();
}
