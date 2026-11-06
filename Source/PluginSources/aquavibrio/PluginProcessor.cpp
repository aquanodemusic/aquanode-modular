#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace aquavibrio
{

// The hardware table, plus the two controls that are not hardware parameters
// (neither is stored in presets): the engine switch and the master volume.
static juce::AudioProcessorValueTreeState::ParameterLayout createLayoutWithEngineMode()
{
    auto layout = createParameterLayout();
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "EngineMode", 1 }, "Engine",
                                                               juce::StringArray { "Native", "Original" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "MasterVolume", 1 }, "Master Volume",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.04f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
            })));
    return layout;
}

AquaVibrioProcessor::AquaVibrioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",   juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)
          // The hardware has a second pair of outputs, and Second Output Balance
          // decides how much of the patch goes there. The host may leave this
          // bus disabled, which is fine - the engine checks.
          .withOutput ("Output 2", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "AquaVibrio", createLayoutWithEngineMode())
{
    engineMode = apvts.getRawParameterValue ("EngineMode");
    masterVolume = apvts.getRawParameterValue ("MasterVolume");
    engineSeen.store (isOriginalEngine() ? 1 : 0);
    noteLayers.setMirror (&originalEngine);
    startTimer (50);   // builds the sampled wavetables the Original engine asks for

    // Sampled waveforms, if the user keeps them in Documents/Aqua Vibrio/Waves.
    {
        const auto waves = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile ("Aqua Vibrio").getChildFile ("Waves");
        if (waves.isDirectory())
            setSampledWaveFolder (waves);
    }
}

// Finds the two sets of user-supplied samples at or below the chosen folder
// (or a level or two above it): the classic single-cycle waves (a folder
// holding "Wave 03.wav" .. "Wave 64.wav") and the wavetables (a folder of
// "NN Name.wav" files).
int AquaVibrioProcessor::setSampledWaveFolder (const juce::File& folder)
{
    const auto isClassic = [] (const juce::File& d) { return d.getChildFile ("Wave 03.wav").existsAsFile(); };
    const auto isTables = [] (const juce::File& d)
    {
        int n = 0;
        for (const auto& f : d.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            const auto name = f.getFileName();
            if (name.length() > 4 && juce::CharacterFunctions::isDigit (name[0])
                && juce::CharacterFunctions::isDigit (name[1]) && name[2] == ' ')
                ++n;
        }
        return n >= 8;
    };

    juce::File classic, tables;
    const auto search = [&] (const juce::File& root)
    {
        if (! root.isDirectory()) return;
        juce::Array<juce::File> dirs { root };
        dirs.addArray (root.findChildFiles (juce::File::findDirectories, true));
        for (const auto& d : dirs)
        {
            if (d.getRelativePathFrom (root).length() > 0
                && d.getRelativePathFrom (root).retainCharacters ("/\\").length() > 3)
                continue;   // at most a few levels down
            if (classic == juce::File{} && isClassic (d)) classic = d;
            if (tables == juce::File{} && ! isClassic (d) && isTables (d)) tables = d;
        }
    };

    search (folder);
    for (auto up = folder.getParentDirectory(), k = juce::File{}; (classic == juce::File{} || tables == juce::File{})
             && up != folder && up.getFullPathName().length() > 3; up = up.getParentDirectory())
    {
        search (up);
        if (up.getParentDirectory() == up || up == folder.getParentDirectory().getParentDirectory()) break;
    }

    const int n = originalEngine.loadSampledWaves (classic);
    const int t = originalEngine.setSampledTableFolder (tables);
    if (n > 0 || t > 0)
    {
        sampledWaveFolder = folder;
    }
    return n + t;
}

void AquaVibrioProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    engine.bindTo (apvts);
    engine.prepare (sampleRate, blockSize);
    originalEngine.bindTo (apvts);
    originalEngine.prepare (sampleRate, blockSize);

    masterGain.reset (sampleRate, 0.03);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolume->load() + patchTrimDb.load()));
}

//==============================================================================
// The master volume follows the engine: switching to ORIGINAL sets +12 dB
// (that engine plays at the reference's own, quieter level), switching back
// to NATIVE sets 0 dB. From the timer, so on the message thread whoever
// flipped the switch (the button, the host, automation).
void AquaVibrioProcessor::timerCallback()
{
    originalEngine.serviceWavetables();

    const int now = isOriginalEngine() ? 1 : 0;
    const int was = engineSeen.exchange (now);
    if (was != now)
        setMasterVolumeDb (now == 1 ? kOriginalMasterDb : 0.0f);
}

void AquaVibrioProcessor::setMasterVolumeDb (float db)
{
    if (auto* p = apvts.getParameter ("MasterVolume"))
        p->setValueNotifyingHost (p->convertTo0to1 (db));
}

bool AquaVibrioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto second = layouts.getChannelSet (false, 1);
    return second.isDisabled() || second == juce::AudioChannelSet::stereo();
}

void AquaVibrioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Merges notes played on the on-screen keyboard into the same buffer the
    // host's MIDI arrives in, and - by scanning that buffer alone - lets the
    // keyboard light up for notes played from the host or a real controller
    // too, so the widget is a two-way window onto whatever the engine hears.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
            {
                engine.setTempo (*bpm);
                originalEngine.setTempo (*bpm);
            }

    // Switching engines: the one being left lets go of its notes, so they
    // don't hang there waiting for a note-off it will never see.
    const int wanted = isOriginalEngine() ? 1 : 0;
    display::originalMode().store (wanted == 1, std::memory_order_relaxed);
    if (wanted != activeEngine)
    {
        if (activeEngine == 0) engine.allNotesOff();
        if (activeEngine == 1) originalEngine.allNotesOff();
        activeEngine = wanted;
    }

    // Deliberately NOT cleared: the engine reads each input sample for the
    // vocoder and the input follower before writing its own output over it.
    auto* const second = (getBusCount (false) > 1 && getBus (false, 1) != nullptr
                              && getBus (false, 1)->isEnabled()
                              ? getBusBuffer (buffer, false, 1).getArrayOfWritePointers()
                              : nullptr);

    if (activeEngine == 1)
    {
        originalEngine.setSecondOutput (second);
        originalEngine.renderBlock (buffer, midi);
    }
    else
    {
        engine.setSecondOutput (second);
        engine.renderBlock (buffer, midi);
    }

    // Master volume, over every output channel (both output pairs).
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterVolume->load() + patchTrimDb.load()));
    const int outs = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (masterGain.isSmoothing())
    {
        const float from = masterGain.getCurrentValue();
        masterGain.skip (n);
        for (int c = 0; c < outs; ++c)
            buffer.applyGainRamp (c, 0, n, from, masterGain.getCurrentValue());
    }
    else if (masterGain.getTargetValue() != 1.0f)
    {
        for (int c = 0; c < outs; ++c)
            buffer.applyGain (c, 0, n, masterGain.getTargetValue());
    }
}

juce::AudioProcessorEditor* AquaVibrioProcessor::createEditor()
{
    return new AquaVibrioEditor (*this);
}

//==============================================================================
namespace
{
    // mono audio <-> text for the state: 24-bit FLAC, base64
    juce::String encodeMono (const float* data, int n)
    {
        if (data == nullptr || n <= 0) return {};
        juce::AudioBuffer<float> b (1, n);
        for (int i = 0; i < n; ++i)
            b.setSample (0, i, juce::jlimit (-1.0f, 1.0f, data[i]));
        juce::MemoryBlock mb;
        {
            juce::FlacAudioFormat flac;
            auto* stream = new juce::MemoryOutputStream (mb, false);
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (stream, 48000.0, 1, 24, {}, 0));
            if (w == nullptr) { delete stream; return {}; }
            w->writeFromAudioSampleBuffer (b, 0, n);
        }
        return juce::Base64::toBase64 (mb.getData(), mb.getSize());
    }

    std::vector<float> decodeMono (const juce::String& text, int maxSamples)
    {
        juce::MemoryOutputStream bytes;
        if (text.isEmpty() || ! juce::Base64::convertFromBase64 (bytes, text) || bytes.getDataSize() == 0)
            return {};
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (
            new juce::MemoryInputStream (bytes.getData(), bytes.getDataSize(), true), true));
        if (r == nullptr || r->lengthInSamples < 1)
            return {};
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, maxSamples);
        juce::AudioBuffer<float> b (1, n);
        if (! r->read (&b, 0, n, 0, true, false))
            return {};
        return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + n);
    }

    int selectedTable (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
            return juce::roundToInt (p->convertFrom0to1 (p->getValue()));
        return 0;
    }
}

void AquaVibrioProcessor::writeSampledAudio (juce::ValueTree& state)
{
    // the classic sampled waves, end to end, and each one's length
    if (originalEngine.getNumSampledWaves() > 0)
    {
        if (wavesCodeSerial != originalEngine.getSampledWavesSerial())
        {
            std::vector<float> all;
            juce::StringArray lengths;
            for (const auto& c : originalEngine.getSampledWaveCycles())
            {
                all.insert (all.end(), c.begin(), c.end());
                lengths.add (juce::String ((int) c.size()));
            }
            wavesCode = encodeMono (all.data(), (int) all.size());
            wavesLengths = lengths.joinIntoString (",");
            wavesCodeSerial = originalEngine.getSampledWavesSerial();
        }
        if (wavesCode.isNotEmpty())
        {
            state.setProperty ("sampledWaves", wavesCode, nullptr);
            state.setProperty ("sampledWaveLengths", wavesLengths, nullptr);
        }
    }

    // the sampled tables this patch selects (and any it has played since)
    juce::ValueTree tables ("TIWavetables");
    const int sel[2] = { selectedTable (apvts, "Osc1WavetableWavetableselect"),
                         selectedTable (apvts, "Osc2WavetableWavetableselect") };
    for (int id = 1; id < original::Engine::numSampledTableSlots(); ++id)
    {
        if (id != sel[0] && id != sel[1] && ! originalEngine.isSampledTableBuilt (id))
            continue;
        std::vector<float> frames;
        if (! originalEngine.getSampledTableFrames (id, frames))
            continue;
        const auto source = juce::String ((int) frames.size()) + ":" + juce::String (frames.empty() ? 0.0f : frames[frames.size() / 2]);
        auto& cached = tableCodes[id];
        if (cached.first != source || cached.second.isEmpty())
            cached = { source, encodeMono (frames.data(), (int) frames.size()) };
        if (cached.second.isEmpty())
            continue;
        juce::ValueTree t ("Table");
        t.setProperty ("id", id, nullptr);
        t.setProperty ("audio", cached.second, nullptr);
        tables.appendChild (t, nullptr);
    }
    if (tables.getNumChildren() > 0)
        state.appendChild (tables, nullptr);
    writeCustomWaves (state);   // an .fxb patch's own waves and level
    if (std::abs (patchTrimDb.load()) > 0.001f)
        state.setProperty ("patchTrimDb", patchTrimDb.load(), nullptr);
}

void AquaVibrioProcessor::readSampledAudio (const juce::ValueTree& state)
{
    // the folder's own waves win; the state's stand in where there is none
    if (originalEngine.getNumSampledWaves() == 0 && state.hasProperty ("sampledWaves"))
    {
        const auto all = decodeMono (state.getProperty ("sampledWaves").toString(), 62 << 16);
        std::vector<std::vector<float>> cycles;
        size_t at = 0;
        for (const auto& len : juce::StringArray::fromTokens (state.getProperty ("sampledWaveLengths").toString(), ",", {}))
        {
            const auto n = (size_t) juce::jmax (0, len.getIntValue());
            if (at + n > all.size()) break;
            cycles.emplace_back (all.begin() + (long) at, all.begin() + (long) (at + n));
            at += n;
        }
        if (! cycles.empty())
            originalEngine.loadSampledWaveCycles (std::move (cycles));
    }

    for (const auto t : state.getChildWithName ("TIWavetables"))
    {
        const int id = (int) t.getProperty ("id", 0);
        if (originalEngine.hasSampledTableFile (id))
            continue;
        auto frames = decodeMono (t.getProperty ("audio").toString(), 2048 * 62);
        if (frames.size() >= 2048)
            originalEngine.setSampledTableData (id, std::move (frames));
    }
    readCustomWaves (state);
    patchTrimDb.store ((float) (double) state.getProperty ("patchTrimDb", 0.0));
}

void AquaVibrioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The layers travel inside the same state tree as the parameters. With
    // notes selected the knobs show the selection, so the committed view -
    // base patch plus layers, selection included - is what gets stored.
    const juce::ScopedLock sl (noteLayers.getLock());
    auto state = apvts.copyState();
    noteLayers.writeCommittedState (state);
    state.setProperty ("sampledWaveFolder", sampledWaveFolder.getFullPathName(), nullptr);
    writeSampledAudio (state);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AquaVibrioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto full = juce::ValueTree::fromXml (*xml);
            // the sampled audio is not kept in the parameter tree
            auto tree = full.createCopy();
            tree.removeProperty ("sampledWaves", nullptr);
            tree.removeProperty ("sampledWaveLengths", nullptr);
            if (auto t = tree.getChildWithName ("TIWavetables"); t.isValid())
                tree.removeChild (t, nullptr);
            const bool hasMaster = tree.getChildWithProperty ("id", "MasterVolume").isValid();
            {
                const juce::ScopedLock sl (noteLayers.getLock());
                apvts.replaceState (tree);
            }

            // A restored session keeps its own master volume: this is not an
            // engine switch. (Sessions from before the knob existed get the
            // level that goes with their engine.)
            engineSeen.store (isOriginalEngine() ? 1 : 0);
            if (! hasMaster)
                setMasterVolumeDb (isOriginalEngine() ? kOriginalMasterDb : 0.0f);
            noteLayers.restoreFrom (tree.getChildWithName (NoteLayerManager::treeType));

            const juce::String waves = tree.getProperty ("sampledWaveFolder", {}).toString();
            if (waves.isNotEmpty() && juce::File::isAbsolutePath (waves) && juce::File (waves) != sampledWaveFolder)
                setSampledWaveFolder (juce::File (waves));
            readSampledAudio (full);
        }
}

//==============================================================================
int AquaVibrioProcessor::loadBank (const juce::File& file)
{
    presetFiles.clear();
    presetNames.clear();
    bankPatches.clear();
    currentPatch = -1;

    // A hardware bank: every single dump in a .mid / .syx / .bin file becomes
    // one entry of the list, the way the hardware shows a ROM bank. An
    // .fxb bank is read into the same singles (AquaVibrioFxbBank.h).
    // A bank is also known by its first bytes, whatever its name says (on
    // Android a picked document can arrive without its own name).
    const auto looksLikeBank = [] (const juce::File& f)
    {
        char head[4] {};
        if (auto in = f.createInputStream(); in != nullptr && in->read (head, 4) == 4)
            return std::memcmp (head, "CcnK", 4) == 0 || std::memcmp (head, "MThd", 4) == 0 || (unsigned char) head[0] == 0xf0;
        return false;
    };
    if (file.existsAsFile() && (file.hasFileExtension ("mid;midi;syx;bin;fxb") || looksLikeBank (file)))
    {
        juce::MemoryBlock data;
        if (file.loadFileAsData (data))
        {
            fxb::Bank fxbBank;
            if (fxb::parseFxb (data, fxbBank))
                bankPatches = fxb::convertBank (fxbBank);
            else
                bankPatches = parseBankFile (data);
        }
        bankFile = file;

        for (const auto& p : bankPatches)
            presetNames.add (p.getName());

        if (! bankPatches.empty())
            selectPatch (0);

        return (int) bankPatches.size();
    }

    const auto dir = file.isDirectory() ? file : file.getParentDirectory();
    if (! dir.isDirectory())
        return 0;

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();

    int selectIndex = -1;

    for (auto& f : files)
    {
        const auto json = f.loadFileAsString();
        const auto parsed = juce::JSON::parse (json);

        // Only files that actually look like our presets go in the list -
        // an unrelated .json file living in the same folder is silently
        // skipped rather than corrupting the bank.
        if (! parsed.isObject() || parsed.getProperty ("format", {}).toString() != "AquaVibrioPreset")
            continue;

        if (! file.isDirectory() && f == file)
            selectIndex = (int) presetFiles.size();

        presetNames.add (presetNameFromJson (json, f));
        presetFiles.push_back (f);
    }

    if (! presetFiles.empty())
        selectPatch (selectIndex >= 0 ? selectIndex : 0);

    return (int) presetFiles.size();
}

void AquaVibrioProcessor::selectPatch (int index)
{
    if (! bankPatches.empty())
    {
        if (index < 0 || index >= (int) bankPatches.size())
            return;

        const auto& patch = bankPatches[(size_t) index];
        noteLayers.loadWholePatch ([&] { applyPatch (patch, apvts); }, juce::var());
        applyPatchWaves (patch);
        patchTrimDb.store (patch.trimDb);
        currentPatch = index;
        engine.allNotesOff();
        originalEngine.allNotesOff();
        return;
    }

    if (index < 0 || index >= (int) presetFiles.size())
        return;

    // With notes selected the preset becomes their patch; otherwise it is the
    // new patch, and brings its own note layers (or none).
    const auto json = presetFiles[(size_t) index].loadFileAsString();
    const auto parsed = juce::JSON::parse (json);
    bool ok = false;

    noteLayers.loadWholePatch ([&] { ok = applyJsonPreset (json, apvts); },
                               parsed.isObject() ? parsed.getProperty ("noteLayers", {}) : juce::var());

    if (ok)
    {
        // a preset saved from an .fxb patch brings that patch's own waves and level
        customWavesFromJson (parsed.isObject() ? parsed.getProperty ("customWaves", {}) : juce::var());
        patchTrimDb.store (parsed.isObject() ? (float) (double) parsed.getProperty ("trimDb", 0.0) : 0.0f);
        currentPatch = index;
        engine.allNotesOff();
        originalEngine.allNotesOff();
    }
}

void AquaVibrioProcessor::saveCurrentPatch (const juce::File& file, const juce::String& name)
{
    const auto base = noteLayers.committedBaseValues();
    auto json = presetToJson (apvts, name, noteLayers.toJson(), &base);
    if (originalEngine.hasCustomWaves() || std::abs (patchTrimDb.load()) > 0.001f)
    {
        // the patch's own waves and level go along with it (an .fxb patch's)
        auto parsed = juce::JSON::parse (json);
        if (auto* o = parsed.getDynamicObject())
        {
            if (originalEngine.hasCustomWaves()) o->setProperty ("customWaves", customWavesToJson());
            if (std::abs (patchTrimDb.load()) > 0.001f) o->setProperty ("trimDb", patchTrimDb.load());
            json = juce::JSON::toString (parsed);
        }
    }
    file.replaceWithText (json);
}

//==============================================================================
// An .fxb patch's own waves
//==============================================================================
juce::File AquaVibrioProcessor::findWaveFile (const juce::String& bankWavePath, bool wavetable) const
{
    // "\Factory\Brute.wav" lives in the bank's Waveforms folder (a wavetable in
    // its Wavetables folder): looked for around the bank and the sampled waves
    const auto rel = bankWavePath.replaceCharacter ('\\', '/').trimCharactersAtStart ("/");
    if (rel.isEmpty())
        return {};
    const juce::String kind = wavetable ? "Wavetables" : "Waveforms";
    juce::Array<juce::File> roots;
    auto addWithParents = [&roots] (juce::File f, int levels)
    {
        for (int i = 0; i < levels && f != juce::File{}; ++i)
        {
            roots.addIfNotAlreadyThere (f);
            const auto up = f.getParentDirectory();
            if (up == f) break;
            f = up;
        }
    };
    if (bankFile != juce::File{}) addWithParents (bankFile.getParentDirectory(), 4);
    if (sampledWaveFolder != juce::File{}) addWithParents (sampledWaveFolder, 4);
    addWithParents (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Aqua Vibrio").getChildFile ("Waves"), 2);

    for (const auto& r : roots)
        for (const auto& candidate : { r.getChildFile (kind).getChildFile (rel), r.getChildFile (fxb::kBankFolder).getChildFile (kind).getChildFile (rel),
                                       r.getChildFile (rel) })
            if (candidate.existsAsFile())
                return candidate;
    return {};
}

void AquaVibrioProcessor::refreshPatchWaves()
{
    // after the Waves folder changed: the bank patch now playing finds its own
    // waves without being loaded again (its knobs stay as they are)
    if (currentPatch >= 0 && currentPatch < (int) bankPatches.size() && bankPatches[(size_t) currentPatch].hasWaveFiles())
        applyPatchWaves (bankPatches[(size_t) currentPatch]);
}

void AquaVibrioProcessor::applyPatchWaves (const Patch& patch)
{
    std::array<original::Engine::CustomWave, 3> waves;
    missingWaveFiles.clear();
    for (int n = 0; n < 3; ++n)
    {
        const auto& name = patch.waveFiles[(size_t) n];
        if (name.isEmpty())
            continue;
        const bool table = patch.waveIsTable[(size_t) n];
        const auto file = findWaveFile (name, table);
        if (file == juce::File{})
        {
            missingWaveFiles.add (name);
            continue;
        }
        auto& samples = waveFileCache[file.getFullPathName() + (table ? "|t" : "|w")];
        if (samples.empty())
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
            {
                const int len = (int) std::min<juce::int64> (reader->lengthInSamples, table ? 2048 * 62 : 1 << 16);
                juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
                if (len > 16 && reader->read (&buf, 0, len, 0, true, true))
                    samples.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
            }
            if (waveFileCache.size() > 48)
                waveFileCache.clear();   // a bound on what browsing many banks keeps
        }
        if (samples.size() < 16)
        {
            missingWaveFiles.add (name);
            continue;
        }
        auto& w = waves[(size_t) n];
        // played inverted, as the sampled hardware waves are (they line up with the saw that way)
        w.frames.resize (samples.size());
        for (size_t i = 0; i < samples.size(); ++i) w.frames[i] = table ? samples[i] : -samples[i];
        w.wavetable = table;
        w.slot = 2;
        w.name = name;
    }
    originalEngine.setCustomWaves (waves);
}

void AquaVibrioProcessor::writeCustomWaves (juce::ValueTree& state) const
{
    juce::ValueTree t ("CustomWaves");
    const auto& waves = originalEngine.getCustomWaves();
    for (int n = 0; n < 3; ++n)
    {
        const auto& w = waves[(size_t) n];
        if (w.empty()) continue;
        juce::ValueTree c ("Wave");
        c.setProperty ("osc", n, nullptr);
        c.setProperty ("wavetable", w.wavetable, nullptr);
        c.setProperty ("slot", w.slot, nullptr);
        c.setProperty ("name", w.name, nullptr);
        c.setProperty ("audio", encodeMono (w.frames.data(), (int) w.frames.size()), nullptr);
        t.appendChild (c, nullptr);
    }
    if (t.getNumChildren() > 0)
        state.appendChild (t, nullptr);
}

void AquaVibrioProcessor::readCustomWaves (const juce::ValueTree& state)
{
    std::array<original::Engine::CustomWave, 3> waves;
    for (const auto c : state.getChildWithName ("CustomWaves"))
    {
        const int n = (int) c.getProperty ("osc", -1);
        if (n < 0 || n > 2) continue;
        auto& w = waves[(size_t) n];
        w.wavetable = (bool) c.getProperty ("wavetable", false);
        w.slot = (int) c.getProperty ("slot", 2);
        w.name = c.getProperty ("name").toString();
        w.frames = decodeMono (c.getProperty ("audio").toString(), 2048 * 62);
    }
    originalEngine.setCustomWaves (waves);
}

juce::var AquaVibrioProcessor::customWavesToJson() const
{
    juce::Array<juce::var> list;
    const auto& waves = originalEngine.getCustomWaves();
    for (int n = 0; n < 3; ++n)
    {
        const auto& w = waves[(size_t) n];
        if (w.empty()) continue;
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("osc", n);
        o->setProperty ("wavetable", w.wavetable);
        o->setProperty ("slot", w.slot);
        o->setProperty ("name", w.name);
        o->setProperty ("audio", encodeMono (w.frames.data(), (int) w.frames.size()));
        list.add (juce::var (o.release()));
    }
    return list;
}

void AquaVibrioProcessor::customWavesFromJson (const juce::var& v)
{
    std::array<original::Engine::CustomWave, 3> waves;
    if (auto* arr = v.getArray())
        for (const auto& item : *arr)
        {
            const int n = (int) item.getProperty ("osc", -1);
            if (n < 0 || n > 2) continue;
            auto& w = waves[(size_t) n];
            w.wavetable = (bool) item.getProperty ("wavetable", false);
            w.slot = (int) item.getProperty ("slot", 2);
            w.name = item.getProperty ("name", {}).toString();
            w.frames = decodeMono (item.getProperty ("audio", {}).toString(), 2048 * 62);
        }
    missingWaveFiles.clear();
    originalEngine.setCustomWaves (waves);
}

juce::String AquaVibrioProcessor::getPatchName (int index) const
{
    return index >= 0 && index < presetNames.size() ? presetNames[index] : juce::String();
}

juce::String AquaVibrioProcessor::getCurrentPatchName() const
{
    return getPatchName (currentPatch);
}

} // namespace aquavibrio

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aquavibrio::AquaVibrioProcessor();
}
