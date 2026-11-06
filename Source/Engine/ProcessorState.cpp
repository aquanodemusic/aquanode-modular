// The processor, part 6: saving and loading - the patch as XML, the session state,
// export and import, and the migration of patches made by older versions.
#include "PluginProcessor.h"
#include "../Editor/PluginEditor.h"
#include "../Modules/Core/ModuleRegistry.h"
#include "../Modules/Utilities/MIDI/MidiAddModule.h"
#include "../Modules/Utilities/MIDI/DiscardMidiModule.h"
#include "../Modules/Utilities/Control/DawModModule.h"
#include "../Modules/Utilities/MIDI/PianoRollModule.h"
#include "PatchStateFiles.h"

#include <algorithm>

using namespace aquanode;

//==============================================================================
// serialization
//==============================================================================
// Samples ride along in the patch as 16-bit FLAC - lossless at the same 16
// bits the WAV embedding used, at about half the size, which matters now that
// the Sampler takes long files. WAV remains the fallback (a sample rate FLAC
// does not take) and is still read from older patches.
bool AquanodeModularAudioProcessor::writeSampleAsWav (const juce::AudioBuffer<float>& buffer,
                                                      double rate, juce::MemoryBlock& dest,
                                                      juce::String* formatName)
{
    auto writeWith = [&] (juce::AudioFormat& format, const char* name)
    {
        dest.reset();
        auto* stream = new juce::MemoryOutputStream (dest, false);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream, rate, (unsigned int) buffer.getNumChannels(), 16, {}, 0));

        if (writer == nullptr)
        {
            delete stream;   // writer would have taken ownership on success
            return false;
        }

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        writer.reset();      // flushes into dest
        if (dest.getSize() == 0)
            return false;
        if (formatName != nullptr)
            *formatName = name;
        return true;
    };

    juce::FlacAudioFormat flac;
    if (writeWith (flac, "flac"))
        return true;
    juce::WavAudioFormat wav;
    return writeWith (wav, "wav");
}

// A choice that was renamed: a patch saved with its old name keeps it.
// (Patches store a combo's choice by name.)
//==============================================================================
// Module types that were replaced by another one load as that one, so an old
// patch never brings back a module the sidebar no longer offers:
//   Felt / Mid / Hard Piano  -> Piano with Model Felt / Mid / Hard (the same
//                               sound, sample for sample - piano_test)
//   NoteAdder - Chord        -> NoteAdder - Manual / Scale / Per Note Chord,
//                               after its Mode (the same knobs and custom chords)
//   Resonator (Mode A / B)   -> Resonator on the Crisp engine (its ancestor)
// (Resonate and Resonator keep their type ids: the modules behind them were
// rebuilt on the Resonate plugin's own engines, with the same knobs.)
struct ModuleMigration
{
    juce::String type;
    std::vector<std::pair<juce::String, float>> set;   // knobs set after the patch's own
};

static ModuleMigration migrateModuleType (const juce::String& typeId, const juce::XmlElement& moduleXml)
{
    if (typeId == "osc.feltpiano") return { "osc.piano", { { "model", 0.0f } } };
    if (typeId == "osc.midpiano")  return { "osc.piano", { { "model", 1.0f } } };
    if (typeId == "osc.hardpiano") return { "osc.piano", { { "model", 2.0f } } };

    // the earlier Resonator (Mode A / B, a comb with an LFO) was the Crisp
    // engine's ancestor: its patches play on that engine of the new one
    if (typeId == "fx.resonate")
        for (auto* pe : moduleXml.getChildWithTagNameIterator ("Param"))
            if (pe->getStringAttribute ("id") == "mode")
            {
                const auto v = pe->getStringAttribute ("value");
                if (v == "A" || v == "B")
                    return { typeId, { { "engine", 2.0f } } };
            }

    if (typeId == "plugin.noteadder.chord")
    {
        juce::String mode;
        for (auto* pe : moduleXml.getChildWithTagNameIterator ("Param"))
            if (pe->getStringAttribute ("id") == "mode")
                mode = pe->getStringAttribute ("value");
        const bool numeric = mode.containsOnly ("0123456789.");
        const int m = numeric ? (int) mode.getFloatValue() : (mode == "Scale" ? 1 : mode == "Per Note" ? 2 : 0);
        return { m == 1 ? "plugin.noteadder.scale" : m == 2 ? "plugin.noteadder.pernote" : "plugin.noteadder.manual", {} };
    }
    return { typeId, {} };
}

static int renamedChoice (const juce::String& typeId, const juce::String& paramId, const juce::String& text)
{
    struct Renamed { const char* type; const char* param; const char* old; int index; };
    static const Renamed renamed[] = {
        { "osc.grand", "mode", "Mode A", 0 },     // Grand's voicings, 6.5: Clear
        { "osc.grand", "mode", "Mode B", 1 },     //                          Felt
        { "fx.resonate", "mode", "A", 0 },        // the earlier Resonator's modes, 6.6: Full
        { "fx.resonate", "mode", "B", 1 },        //                                     Odd
        { "osc.spectralresynth", "reassign", "Off", 0 },   // Spectral Resynthesis 5.6: Reassign Off / On,
        { "osc.spectralresynth", "reassign", "On", 1 },    // since 5.7 the Engine: Bins / Partials
    };
    for (const auto& r : renamed)
        if (typeId == r.type && paramId == r.param && text == r.old)
            return r.index;
    return -1;
}

std::shared_ptr<juce::AudioBuffer<float>> AquanodeModularAudioProcessor::readWavFromMemory (
    const void* data, size_t size, double& rate)
{
    // FLAC (current patches) or WAV (older ones), whichever the data is
    std::unique_ptr<juce::AudioFormatReader> reader;
    {
        juce::FlacAudioFormat flac;
        reader.reset (flac.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
    }
    if (reader == nullptr)
    {
        juce::WavAudioFormat wav;
        reader.reset (wav.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
    }

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    const int numChannels = juce::jmax (1, juce::jmin (2, (int) reader->numChannels));
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, (int) reader->lengthInSamples);
    reader->read (buffer.get(), 0, (int) reader->lengthInSamples, 0, true, numChannels > 1);
    rate = reader->sampleRate;
    return buffer;
}

std::unique_ptr<juce::XmlElement> AquanodeModularAudioProcessor::buildPatchXml (
    bool embedSamplesAsBase64, juce::ZipFile::Builder* zipBuilder)
{
    std::unique_ptr<aquanode::patchfiles::FileSink> patchFiles;
    if (zipBuilder != nullptr)
        patchFiles = std::make_unique<aquanode::patchfiles::FileSink> (*zipBuilder);

    auto root = std::make_unique<juce::XmlElement> ("AquanodePatch");
    root->setAttribute ("version", 1);

    // the colours travel with the patch (and with the session the app and a DAW keep)
    {
        const auto& t = aquanode::themeColours();
        auto* col = root->createNewChildElement ("Colours");
        for (int i = 0; i < aquanode::ThemeColours::NumSlots; ++i)
            col->setAttribute (aquanode::ThemeColours::key (i), t.colour[i].toDisplayString (false));
        col->setAttribute ("grid", t.showGrid ? 1 : 0);
    }

    auto* modulesEl = root->createNewChildElement ("Modules");
    for (const auto& inst : instances)
    {
        auto* m = modulesEl->createNewChildElement ("Module");
        m->setAttribute ("id", inst->id);
        m->setAttribute ("type", inst->descriptor().typeId);
        m->setAttribute ("x", inst->position.x);
        m->setAttribute ("y", inst->position.y);
        if (inst->collapsed)
            m->setAttribute ("collapsed", 1);
        if (! inst->customColour.isTransparent())
            m->setAttribute ("colour", inst->customColour.toDisplayString (false));

        for (const auto& p : inst->descriptor().params)
        {
            if (p.type == ParamType::Button)
                continue;

            auto* pe = m->createNewChildElement ("Param");
            pe->setAttribute ("id", p.id);

            const float v = inst->dsp->getParameter (p.id);
            if ((p.type == ParamType::Combo || p.type == ParamType::RotarySteppedList)
                && p.choices.size() > 0)
                pe->setAttribute ("value", p.choices[juce::jlimit (0, p.choices.size() - 1, (int) v)]);
            else
                pe->setAttribute ("value", v);
        }

        // opaque per-module blob (Draw LFO's curve, a plugin's whole state, ...).
        // A .aqua keeps it readable, its sounds as files in samples/
        // (PatchStateFiles.h); a host's state keeps it as it is.
        {
            const auto custom = inst->dsp->saveCustomState();
            std::unique_ptr<juce::XmlElement> readable;
            if (! embedSamplesAsBase64 && patchFiles != nullptr)
                readable = aquanode::patchfiles::writeState (custom, inst->descriptor().typeId, inst->id, *patchFiles);
            if (readable != nullptr)
                m->addChildElement (readable.release());
            else if (custom.isNotEmpty())
                m->createNewChildElement ("CustomState")->addTextElement (custom);
        }

        // loaded samples, and Record's take (an undo step keeps the buffers themselves)
        if (inst->dsp->hasAudioToSave() && ! skipAudioInXml)
        {
            double rate = 44100.0;
            if (auto sample = inst->dsp->getAudioToSave (rate))
            {
                juce::MemoryBlock wavData;
                juce::String formatName { "wav" };
                if (writeSampleAsWav (*sample, rate, wavData, &formatName))
                {
                    auto* se = m->createNewChildElement ("EmbeddedSample");
                    se->setAttribute ("format", formatName);

                    if (embedSamplesAsBase64)
                    {
                        se->setAttribute ("base64",
                            juce::Base64::toBase64 (wavData.getData(), wavData.getSize()));
                    }
                    else if (zipBuilder != nullptr)
                    {
                        const auto fileName = "samples/" + juce::String (inst->id) + "_"
                            + inst->descriptor().typeId.replaceCharacter ('.', '_') + "." + formatName;
                        se->setAttribute ("file", fileName);
                        zipBuilder->addEntry (new juce::MemoryInputStream (wavData, true),
                                              5, fileName, juce::Time::getCurrentTime());
                    }
                }
            }
        }
    }

    // The host's automation values belong to the DAW session, not to a patch
    // that gets shared around, so they only ride along in the plugin's own
    // state blob (base64 mode) and never in an exported patch zip.
    if (embedSamplesAsBase64)
    {
        auto* hm = root->createNewChildElement ("HostMods");
        for (size_t i = 0; i < dawModParams.size(); ++i)
            if (dawModParams[i] != nullptr)
                hm->setAttribute ("m" + juce::String ((int) i + 1),
                                  (double) dawModParams[i]->get());
    }

    auto* cablesEl = root->createNewChildElement ("Cables");
    for (const auto& c : cables)
    {
        auto* ce = cablesEl->createNewChildElement ("Cable");
        ce->setAttribute ("fromModule", c.fromModule);
        ce->setAttribute ("fromSocket", c.fromSocket);
        ce->setAttribute ("toModule", c.toModule);
        ce->setAttribute ("toSocket", c.toSocket);
    }

    auto* paramCablesEl = root->createNewChildElement ("ParamCables");
    for (const auto& pc : paramCables)
    {
        auto* pe = paramCablesEl->createNewChildElement ("ParamCable");
        pe->setAttribute ("fromModule", pc.fromModule);
        pe->setAttribute ("fromSocket", pc.fromSocket);
        pe->setAttribute ("toModule", pc.toModule);
        pe->setAttribute ("param", pc.paramId);
        pe->setAttribute ("depth", pc.depth);
        pe->setAttribute ("bipolar", pc.bipolar ? 1 : 0);
    }

    return root;
}

void AquanodeModularAudioProcessor::applyPatchXml (const juce::XmlElement& xml,
    const std::function<bool (const juce::String&, juce::MemoryBlock&)>& patchFiles)
{
    // a sample file of the patch, decoded
    auto sampleLoader = [&patchFiles] (const juce::String& fileRef, double& rate) -> std::shared_ptr<juce::AudioBuffer<float>>
    {
        juce::MemoryBlock data;
        if (patchFiles == nullptr || fileRef.isEmpty() || ! patchFiles (fileRef, data) || data.getSize() == 0)
            return nullptr;
        return readWavFromMemory (data.getData(), data.getSize(), rate);
    };

    if (! xml.hasTagName ("AquanodePatch"))
        return;

    clearPatch();
    patchName = "Patch";   // a patch never stores a name; importPatchFrom* name it after its file

    // its colours; a patch saved before they were kept with it leaves the
    // current ones as they are
    {
        auto& t = aquanode::themeColours();
        if (auto* col = xml.getChildByName ("Colours"))
        {
            for (int i = 0; i < aquanode::ThemeColours::NumSlots; ++i)
                if (col->hasAttribute (aquanode::ThemeColours::key (i)))
                    t.colour[i] = juce::Colour::fromString (col->getStringAttribute (aquanode::ThemeColours::key (i))).withAlpha (1.0f);
            t.showGrid = col->getIntAttribute ("grid", 1) != 0;
        }
    }

    int maxId = 0;

    if (auto* modulesEl = xml.getChildByName ("Modules"))
    {
        for (auto* m : modulesEl->getChildWithTagNameIterator ("Module"))
        {
            const auto migration = migrateModuleType (m->getStringAttribute ("type"), *m);
            const auto typeId = migration.type;
            auto* reg = ModuleFactory::instance().find (typeId);
            if (reg == nullptr)
                continue;   // module type no longer registered - skip gracefully

            auto inst = std::make_shared<ModuleInstance>();
            inst->id = m->getIntAttribute ("id");
            inst->reg = reg;
            inst->dsp = ModuleFactory::instance().createInstance (typeId);
            inst->position = { m->getIntAttribute ("x"), m->getIntAttribute ("y") };
            inst->collapsed = m->getBoolAttribute ("collapsed", false);
            if (m->hasAttribute ("colour"))
                inst->customColour = juce::Colour::fromString ("ff" + m->getStringAttribute ("colour")).withAlpha (1.0f);
            maxId = juce::jmax (maxId, inst->id);
            initInstanceBuffers (*inst);
            attachModuleHooks (*inst);

            for (auto* pe : m->getChildWithTagNameIterator ("Param"))
            {
                const auto paramId = pe->getStringAttribute ("id");
                const auto* spec = reg->descriptor.findParam (paramId);
                if (spec == nullptr)
                {
                    // a removed depth knob: kept for the cable into its removed input
                    inst->dsp->setParameter (paramId, (float) pe->getStringAttribute ("value").getDoubleValue());
                    continue;
                }

                const auto text = pe->getStringAttribute ("value");
                float value = (float) text.getDoubleValue();

                if ((spec->type == ParamType::Combo || spec->type == ParamType::RotarySteppedList))
                {
                    const int idx = spec->choices.indexOf (text);
                    if (idx >= 0)
                        value = (float) idx;
                    else if (const int old = renamedChoice (typeId, paramId, text); old >= 0)
                        value = (float) old;
                }

                inst->dsp->setParameter (paramId, value);
            }

            // parameters the patch was saved without: those that say what an
            // older patch meant by their absence (a level trim: the old level)
            for (const auto& spec : reg->descriptor.params)
                if (spec.hasMissingValue)
                {
                    bool saved = false;
                    for (auto* pe : m->getChildWithTagNameIterator ("Param"))
                        if (pe->getStringAttribute ("id") == spec.id) { saved = true; break; }
                    if (! saved)
                        inst->dsp->setParameter (spec.id, spec.missingValue);
                }

            for (const auto& [id, v] : migration.set)
                inst->dsp->setParameter (id, v);

            if (auto* cs = m->getChildByName ("CustomState"))
                inst->dsp->loadCustomState (cs->getAllSubText());
            else if (auto* st = m->getChildByName ("State"))
            {
                // a .aqua's readable state: put back together, its sounds from samples/
                juce::String custom;
                if (aquanode::patchfiles::readState (*st, patchFiles, custom))
                    inst->dsp->loadCustomState (custom);
            }

            if (auto* se = m->getChildByName ("EmbeddedSample"))
            {
                double rate = 44100.0;
                std::shared_ptr<juce::AudioBuffer<float>> sample;

                const auto base64 = se->getStringAttribute ("base64");
                if (base64.isNotEmpty())
                {
                    juce::MemoryOutputStream decoded;
                    if (juce::Base64::convertFromBase64 (decoded, base64))
                        sample = readWavFromMemory (decoded.getData(), decoded.getDataSize(), rate);
                }
                else
                    sample = sampleLoader (se->getStringAttribute ("file"), rate);

                if (sample != nullptr)
                    inst->dsp->restoreSavedAudio (sample, rate);
            }

            if (isPrepared)
                inst->dsp->prepare (currentSampleRate);

            instances.push_back (inst);
        }
    }

    nextInstanceId = maxId + 1;

    if (auto* cablesEl = xml.getChildByName ("Cables"))
    {
        for (auto* ce : cablesEl->getChildWithTagNameIterator ("Cable"))
        {
            CableInfo c { ce->getIntAttribute ("fromModule"), ce->getStringAttribute ("fromSocket"),
                          ce->getIntAttribute ("toModule"), ce->getStringAttribute ("toSocket") };

            // into an input that is a knob cable now (patches saved before it was removed)
            if (auto* dst = getInstance (c.toModule))
                if (dst->descriptor().inputIndexOf (c.toSocket) < 0)
                    if (const auto* legacy = aquanode::legacyKnobInput (dst->descriptor().typeId, c.toSocket))
                    {
                        ParamCableInfo pc;
                        pc.fromModule = c.fromModule;
                        pc.fromSocket = c.fromSocket;
                        pc.toModule = c.toModule;
                        pc.paramId = legacy->param;
                        pc.depth = juce::jlimit (-1.0f, 1.0f, legacy->depthParam != nullptr
                                       ? dst->dsp->legacyValue (legacy->depthParam, legacy->depthDefault) * legacy->depthScale
                                       : legacy->fixedDepth);
                        pc.bipolar = ! outputIsUnipolar (pc.fromModule, pc.fromSocket);   // as the input took it
                        paramCables.push_back (pc);
                        continue;
                    }
            cables.push_back (c);
        }
    }

    if (auto* hm = xml.getChildByName ("HostMods"))
    {
        for (size_t i = 0; i < dawModParams.size(); ++i)
        {
            const auto attr = "m" + juce::String ((int) i + 1);
            if (dawModParams[i] != nullptr && hm->hasAttribute (attr))
                *dawModParams[i] = (float) hm->getDoubleAttribute (attr);
        }
    }

    if (auto* paramCablesEl = xml.getChildByName ("ParamCables"))
    {
        for (auto* pe : paramCablesEl->getChildWithTagNameIterator ("ParamCable"))
        {
            ParamCableInfo pc;
            pc.fromModule = pe->getIntAttribute ("fromModule");
            pc.fromSocket = pe->getStringAttribute ("fromSocket");
            pc.toModule = pe->getIntAttribute ("toModule");
            pc.paramId = pe->getStringAttribute ("param");
            pc.depth = juce::jlimit (-1.0f, 1.0f, (float) pe->getDoubleAttribute ("depth", 0.3));

            // Curve CV cables set the knob absolutely now, and start at 100 %.
            // Patches saved before that carry the old 30 % default (which the
            // depth menu never offers), so 30 % from a Curve CV means "default":
            // lift it to 100 % so the saved curve covers the whole knob range.
            if (std::abs (pc.depth - 0.3f) < 0.001f)
                if (auto* src = getInstance (pc.fromModule))
                    if (src->dsp != nullptr && src->dsp->drivesKnobsAbsolutely())
                        pc.depth = 1.0f;

            // saved before the Bipolar switch: as its source runs (what it did then)
            pc.bipolar = pe->hasAttribute ("bipolar") ? pe->getIntAttribute ("bipolar") != 0
                                                      : ! outputIsUnipolar (pc.fromModule, pc.fromSocket);
            paramCables.push_back (pc);
        }
    }

    rebuildGraph();
    resetAllPending.store (true, std::memory_order_release);   // fresh patch: wipe voices + DSP state
    sendChangeMessage();
}

//==============================================================================
void AquanodeModularAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // host state: one opaque blob, samples base64-embedded inline (no zip)
    if (auto xml = buildPatchXml (true, nullptr))
        copyXmlToBinary (*xml, destData);
}

void AquanodeModularAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyPatchXml (*xml, nullptr);
}

//==============================================================================
bool AquanodeModularAudioProcessor::exportPatchToStream (juce::OutputStream& out)
{
    juce::ZipFile::Builder builder;

    auto xml = buildPatchXml (false, &builder);   // samples as real files in the zip
    if (xml == nullptr)
        return false;

    const auto xmlText = xml->toString();
    juce::MemoryBlock xmlBlock (xmlText.toRawUTF8(), xmlText.getNumBytesAsUTF8());
    builder.addEntry (new juce::MemoryInputStream (xmlBlock, true),
                      5, "patch.xml", juce::Time::getCurrentTime());

    double progress = 0.0;
    return builder.writeToStream (out, &progress);
}

bool AquanodeModularAudioProcessor::exportPatchToZip (const juce::File& zipFile)
{
    zipFile.deleteFile();
    juce::FileOutputStream out (zipFile);
    if (! out.openedOk())
        return false;

    return exportPatchToStream (out);
}

//==============================================================================
// Import Patch, whatever the file is called: what its bytes are decides.
//   a zip (.aqua, or an older .zip): patch.xml - at the top, or in the one
//       folder a zip made of a patch's folder has - and the samples/ it names
//   a patch's XML on its own (.xml, patch.xml taken out of a .aqua, say):
//       samples embedded in it, or the files it names beside it
//   a saved app state (the binary blob a host keeps)
namespace
{
    enum class PatchFileKind { zip, xml, state, unknown };

    PatchFileKind kindOfPatchFile (const juce::MemoryBlock& data)
    {
        const auto* b = static_cast<const char*> (data.getData());
        auto starts = [&data, b] (const char* magic)
        {
            const auto n = std::strlen (magic);
            return data.getSize() >= n && std::memcmp (b, magic, n) == 0;
        };
        if (starts ("PK\x03\x04") || starts ("PK\x05\x06")) return PatchFileKind::zip;
        if (starts ("VC2!"))                                 return PatchFileKind::state;
        size_t i = starts ("\xef\xbb\xbf") ? 3 : 0;
        while (i < data.getSize() && i < 4096 && juce::CharacterFunctions::isWhitespace ((juce::juce_wchar) (unsigned char) b[i])) ++i;
        if (i < data.getSize() && b[i] == '<')              return PatchFileKind::xml;
        return PatchFileKind::unknown;
    }

    // the zip entry of patch.xml (-1: none), and the folder it is in
    int findPatchXml (juce::ZipFile& zip, juce::String& prefix)
    {
        prefix.clear();
        if (const int top = zip.getIndexOfFileName ("patch.xml"); top >= 0)
            return top;
        for (int i = 0; i < zip.getNumEntries(); ++i)
            if (const auto* e = zip.getEntry (i); e != nullptr && e->filename.endsWithIgnoreCase ("/patch.xml")
                                                    && ! e->filename.startsWith ("__MACOSX"))
            {
                prefix = e->filename.upToLastOccurrenceOf ("/", true, false);
                return i;
            }
        return -1;
    }
}

bool AquanodeModularAudioProcessor::importFromZipObject (juce::ZipFile& zip)
{
    juce::String prefix;
    const int xmlIndex = findPatchXml (zip, prefix);
    if (xmlIndex < 0)
        return false;

    juce::String xmlText;
    {
        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (xmlIndex));
        if (stream == nullptr)
            return false;
        xmlText = stream->readEntireStreamAsString();
    }

    auto xml = juce::parseXML (xmlText);
    if (xml == nullptr || ! xml->hasTagName ("AquanodePatch"))
        return false;

    applyPatchXml (*xml, [&zip, prefix] (const juce::String& path, juce::MemoryBlock& data)
    {
        int idx = zip.getIndexOfFileName (prefix + path);
        if (idx < 0)
            idx = zip.getIndexOfFileName (path);
        if (idx < 0)
            return false;

        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (idx));
        if (stream == nullptr)
            return false;

        data.reset();
        stream->readIntoMemoryBlock (data);
        return true;
    });

    return true;
}

// importPatchFromStream / importPatchFromZip: the one import both go through.
// folder: where a bare XML's sample files are (none for a picked document).
static bool importPatchData (AquanodeModularAudioProcessor& proc, const juce::MemoryBlock& data, const juce::File& folder,
                             const std::function<bool (juce::ZipFile&)>& fromZip,
                             const std::function<void (const juce::XmlElement&, const juce::File&)>& fromXml)
{
    switch (kindOfPatchFile (data))
    {
        case PatchFileKind::zip:
        {
            juce::ZipFile zip (new juce::MemoryInputStream (data, false), true);   // ZipFile needs random access
            return fromZip (zip);
        }
        case PatchFileKind::state:
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize());
            if (xml == nullptr || ! xml->hasTagName ("AquanodePatch"))
                return false;
            fromXml (*xml, {});
            return true;
        }
        case PatchFileKind::xml:
        {
            auto xml = juce::parseXML (juce::String::createStringFromData (data.getData(), (int) data.getSize()));
            if (xml == nullptr || ! xml->hasTagName ("AquanodePatch"))
                return false;
            fromXml (*xml, folder);
            return true;
        }
        case PatchFileKind::unknown:
        default:
            break;
    }
    juce::ignoreUnused (proc);
    return false;
}

bool AquanodeModularAudioProcessor::importPatchFromStream (juce::InputStream& in, const juce::String& fileName)
{
    juce::MemoryBlock mb;
    in.readIntoMemoryBlock (mb);

    const bool ok = importPatchData (*this, mb, {},
        [this] (juce::ZipFile& zip) { return importFromZipObject (zip); },
        [this] (const juce::XmlElement& xml, const juce::File&) { applyPatchXml (xml, nullptr); });

    // the patch is called after the file it came from
    if (ok && fileName.isNotEmpty())
        patchName = fileName;
    return ok;
}

bool AquanodeModularAudioProcessor::importPatchFromZip (const juce::File& zipFile)
{
    juce::MemoryBlock mb;
    if (! zipFile.loadFileAsData (mb))
        return false;

    const bool ok = importPatchData (*this, mb, zipFile.getParentDirectory(),
        [this] (juce::ZipFile& zip) { return importFromZipObject (zip); },
        [this] (const juce::XmlElement& xml, const juce::File& folder)
        {
            // samples embedded in the XML (base64), or files it names beside
            // it - patch.xml and samples/ unpacked from a .aqua into one folder
            applyPatchXml (xml, [folder] (const juce::String& path, juce::MemoryBlock& data)
            {
                if (folder == juce::File() || path.isEmpty() || path.contains (".."))
                    return false;
                data.reset();
                return folder.getChildFile (path).loadFileAsData (data);
            });
        });

    // the patch is called after the file it came from
    if (ok)
        patchName = zipFile.getFileNameWithoutExtension();
    return ok;
}
