#include "RawDataModule.h"

using namespace aquanode;

namespace
{
    const juce::StringArray& encodingNames()
    {
        static const juce::StringArray n { "Signed 16-bit PCM", "Signed 24-bit PCM", "Signed 32-bit PCM",
                                           "Unsigned 8-bit PCM", "Signed 8-bit PCM", "32-bit float", "64-bit float",
                                           "U-Law", "A-Law" };
        return n;
    }
    const int kRates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000 };

    int bytesPer (int enc)
    {
        switch (enc)
        {
            case RawDataModule::encS16: return 2;
            case RawDataModule::encS24: return 3;
            case RawDataModule::encS32: case RawDataModule::encF32: return 4;
            case RawDataModule::encF64: return 8;
            default: return 1;
        }
    }

    // G.711
    float ulaw (juce::uint8 b)
    {
        b = (juce::uint8) ~b;
        const int sign = b & 0x80, exponent = (b >> 4) & 7, mantissa = b & 15;
        int v = ((mantissa << 3) + 0x84) << exponent;
        v -= 0x84;
        return (float) (sign ? -v : v) / 32768.0f;
    }
    float alaw (juce::uint8 b)
    {
        b ^= 0x55;
        const int sign = b & 0x80, exponent = (b >> 4) & 7, mantissa = b & 15;
        int v = exponent == 0 ? (mantissa << 4) + 8 : ((mantissa << 4) + 0x108) << (exponent - 1);
        return (float) (sign ? v : -v) / 32768.0f;
    }

    float readOne (const juce::uint8* p, int enc, bool be)
    {
        auto get = [p, be] (int n, int i) { return (juce::uint32) p[be ? n - 1 - i : i]; };
        switch (enc)
        {
            case RawDataModule::encS16: return (float) (juce::int16) (get (2, 0) | (get (2, 1) << 8)) / 32768.0f;
            case RawDataModule::encS24:
            {
                juce::uint32 u = get (3, 0) | (get (3, 1) << 8) | (get (3, 2) << 16);
                if (u & 0x800000u) u |= 0xff000000u;
                return (float) (juce::int32) u / 8388608.0f;
            }
            case RawDataModule::encS32:
                return (float) ((double) (juce::int32) (get (4, 0) | (get (4, 1) << 8) | (get (4, 2) << 16) | (get (4, 3) << 24)) / 2147483648.0);
            case RawDataModule::encU8: return ((float) p[0] - 128.0f) / 128.0f;
            case RawDataModule::encS8: return (float) (juce::int8) p[0] / 128.0f;
            case RawDataModule::encF32:
            {
                const juce::uint32 u = get (4, 0) | (get (4, 1) << 8) | (get (4, 2) << 16) | (get (4, 3) << 24);
                float f; std::memcpy (&f, &u, 4);
                return std::isfinite (f) ? juce::jlimit (-1.0f, 1.0f, f) : 0.0f;
            }
            case RawDataModule::encF64:
            {
                juce::uint64 u = 0;
                for (int i = 0; i < 8; ++i) u |= (juce::uint64) get (8, i) << (8 * i);
                double d; std::memcpy (&d, &u, 8);
                return std::isfinite (d) ? (float) juce::jlimit (-1.0, 1.0, d) : 0.0f;
            }
            case RawDataModule::encULaw: return ulaw (p[0]);
            case RawDataModule::encALaw: return alaw (p[0]);
            default: return 0.0f;
        }
    }
}

double RawDataModule::rateForIndex (int i)
{
    return (double) kRates[juce::jlimit (0, (int) (sizeof (kRates) / sizeof (kRates[0])) - 1, i)];
}

juce::AudioBuffer<float> RawDataModule::decode (const juce::MemoryBlock& bytes, const Settings& s, juce::int64 maxFrames)
{
    const int bps = bytesPer (s.encoding);
    const int ch = juce::jlimit (1, 8, s.channels);
    const juce::int64 size = (juce::int64) bytes.getSize();
    const juce::int64 from = juce::jlimit ((juce::int64) 0, size, s.offset);
    const juce::int64 usable = (juce::int64) ((double) (size - from) * juce::jlimit (0.0f, 1.0f, s.amount));
    const juce::int64 frames = juce::jmin (usable / (bps * ch), maxFrames, (juce::int64) std::numeric_limits<int>::max());
    if (frames < 2)
        return {};
    // the first two channels are played (more are read past)
    const int outCh = juce::jmin (2, ch);
    juce::AudioBuffer<float> out (outCh, (int) frames);
    const auto* base = static_cast<const juce::uint8*> (bytes.getData()) + from;
    for (int f = 0; f < (int) frames; ++f)
        for (int c = 0; c < outCh; ++c)
            out.setSample (c, f, readOne (base + ((juce::int64) f * ch + c) * bps, s.encoding, s.bigEndian));
    return out;
}

RawDataModule::Settings RawDataModule::detect (const juce::MemoryBlock& bytes, juce::int64 offset)
{
    // real sound changes little from one sample to the next compared with its
    // level; data read the wrong way jumps about. Score: mean step / mean level,
    // with clipped or silent readings ruled out.
    Settings best;
    double bestScore = 1.0e30;
    const juce::int64 probeBytes = juce::jmin ((juce::int64) bytes.getSize() - offset, (juce::int64) 262144);
    if (probeBytes <= 16)
        return best;
    juce::MemoryBlock probe (static_cast<const char*> (bytes.getData()) + offset, (size_t) probeBytes);
    for (int enc = 0; enc < numEncodings; ++enc)
        for (int be = 0; be < (bytesPer (enc) > 1 ? 2 : 1); ++be)
            for (int ch = 1; ch <= 2; ++ch)
            {
                Settings s; s.encoding = enc; s.bigEndian = be != 0; s.channels = ch; s.offset = 0; s.amount = 1.0f;
                const auto b = decode (probe, s, 65536);
                if (b.getNumSamples() < 64) continue;
                double step = 0.0, level = 0.0;
                int clipped = 0;
                for (int c = 0; c < b.getNumChannels(); ++c)
                {
                    const float* d = b.getReadPointer (c);
                    for (int i = 1; i < b.getNumSamples(); ++i)
                    {
                        step += std::abs (d[i] - d[i - 1]);
                        level += std::abs (d[i]);
                        if (std::abs (d[i]) > 0.999f) ++clipped;
                    }
                }
                const double n = (double) b.getNumChannels() * (b.getNumSamples() - 1);
                if (level / n < 1.0e-4 || clipped > n * 0.05) continue;
                double score = step / (level + 1.0e-12) * (1.0 + 0.02 * (ch - 1));
                // bytes that are not floats read as floats come out huge or tiny; ones that read
                // cleanly inside -1..1 almost surely are floats (their int reading is smooth too)
                if ((enc == encF32 || enc == encF64) && clipped < n * 0.001 && level / n > 1.0e-3)
                    score *= 0.25;
                if (score < bestScore) { bestScore = score; best = s; }
            }
    best.offset = offset;
    return best;
}

RawDataModule::Settings RawDataModule::currentSettings() const
{
    Settings s;
    s.encoding = juce::jlimit (0, numEncodings - 1, (int) std::lround (param (pEncoding)));
    s.bigEndian = param (pByteOrder) > 0.5f;
    s.channels = 1 + juce::jlimit (0, 7, (int) std::lround (param (pChannels)));
    s.offset = (juce::int64) std::llround (param (pOffset));
    s.amount = param (pAmount) * 0.01f;
    return s;
}

bool RawDataModule::importNow()
{
    if (! hasRawBytes())
        return false;
    const double rate = rateForIndex ((int) std::lround (param (pRawRate)));
    auto buf = decode (rawBytes, currentSettings(), (juce::int64) (maxSampleSeconds() * rate));
    if (buf.getNumSamples() < 2)
        return false;
    setLoadedSample (std::make_shared<juce::AudioBuffer<float>> (std::move (buf)), rate);
    return true;
}

void RawDataModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "loadSample")
        openRawChooser();
    else if (paramId == "detect")
    {
        if (! hasRawBytes()) return;
        const auto s = detect (rawBytes, (juce::int64) std::llround (param (pOffset)));
        setParameter ("encoding", (float) s.encoding);
        setParameter ("byteOrder", s.bigEndian ? 1.0f : 0.0f);
        setParameter ("channels", (float) (s.channels - 1));
        importNow();
    }
    else if (paramId == "import")
        importNow();
    else
        SamplerModule::uiButtonClicked (paramId);
}

void RawDataModule::openRawChooser()
{
    rawChooser = std::make_unique<juce::FileChooser> ("Load any file as raw data", juce::File(), "*");
    juce::WeakReference<SynthModule> weakThis (this);
    rawChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weakThis] (const juce::FileChooser& fc)
        {
            auto* self = dynamic_cast<RawDataModule*> (weakThis.get());
            if (self == nullptr) return;
            juce::MemoryBlock mb;
           #if JUCE_ANDROID
            auto doc = juce::AndroidDocument::fromDocument (fc.getURLResult());
            if (! doc.hasValue()) return;
            if (auto in = doc.createInputStream())
                in->readIntoMemoryBlock (mb, 512 * 1024 * 1024);
           #else
            const auto f = fc.getResult();
            if (! f.existsAsFile() || ! f.loadFileAsData (mb)) return;
           #endif
            if (mb.getSize() == 0) return;
            self->setRawBytes (std::move (mb));
            self->importNow();
        });
}

//==============================================================================
static ModuleDescriptor rawDataDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.rawdata";
    d.displayName = "Raw Data";
    d.description =
        "Any file played as sound. Load File takes a file of any kind and reads its bytes as samples: "
        "Encoding, Byte Order and Channels say how, Start Offset skips that many bytes first, Amount takes "
        "that share of the rest, and Sample Rate sets how fast it plays at C4. Detect tries the encodings, "
        "byte orders and channel counts and keeps the one that reads most like audio; Import reads the file "
        "again after a change. The result plays like the Sampler: root C4, Start and End, Cycle, Save Sample. "
        "The sound read is saved with the patch; to read it another way after reopening, load the file again.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 1;
    d.sockets = {
        modIn ("fmIn",  "FM In"),
        modIn ("envIn", "Env In"),
        midiIn ("addMidiIn", "Add MIDI In"),
        audioOut ("audioOut", "Audio Out")
    };
    // Sampler's parameters first, in its order (its engine reads them by index)
    d.params = {
        makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.5f, 1),
        makeButton ("loadSample", "Load File", 0, 2),
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f),
        makeRotary ("start", "Start", 0.0f, 1.0f, 0.0f, 1),
        makeRotary ("end",   "End",   0.0f, 1.0f, 1.0f, 1),
        makeCombo  ("cycle", "Cycle", { "Off", "On" }, 0, 3, 2),
        makeRotary ("viewZoom", "Zoom", 1.0f, 1000.0f, 1.0f, 2, "x", true).noMod(),
        makeRotary ("viewPos", "View Pos", 0.0f, 1.0f, 0.0f, 2).noMod(),
        makeButton ("saveSample", "Save Sample", 0, 2),
        makeRotary ("saveAs", "Save As", -36.0f, 36.0f, 0.0f, 0, " st", false, 1.0f).noMod(),
        makeCombo  ("saveFormat", "Format", { "FLAC", "WAV" }, 0, 1, 2).noMod(),
        // the raw reading
        makeCombo  ("encoding", "Encoding", encodingNames(), 0, 4, 3).noMod(),
        makeCombo  ("byteOrder", "Byte Order", { "Little-endian", "Big-endian" }, 0, 4, 2).noMod(),
        makeCombo  ("channels", "Channels", { "1 Channel (Mono)", "2 Channels (Stereo)", "3 Channels", "4 Channels",
                                              "5 Channels", "6 Channels", "7 Channels", "8 Channels" }, 0, 5, 3).noMod(),
        makeRotary ("offset", "Start Offset", 0.0f, 1048576.0f, 0.0f, 5, "bytes", false, 1.0f).noMod(),
        makeRotary ("amount", "Amount", 0.0f, 100.0f, 100.0f, 5, "%").noMod(),
        makeCombo  ("rawRate", "Sample Rate", { "8000 Hz", "11025 Hz", "16000 Hz", "22050 Hz", "32000 Hz", "44100 Hz",
                                                "48000 Hz", "88200 Hz", "96000 Hz", "192000 Hz" }, 5, 6, 2).noMod(),
        makeButton ("detect", "Detect", 6, 1),
        makeButton ("import", "Import", 6, 1)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RawDataModule, rawDataDescriptor)
