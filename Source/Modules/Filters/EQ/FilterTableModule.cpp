// Filter Table EQ - a wavetable whose frames are EQ curves. Eight curves per
// table (vowels, a dark-to-bright tilt, petals opening, a moving comb ...);
// Position scans through them, blending the neighbours, so a cable from an
// LFO or an envelope onto Position makes the sound's colour travel. Twelve
// fixed bells (40 Hz - 14 kHz) whose gains follow the curve, so moving it is
// smooth and cheap. Depth scales the curve, Shift slides it up or down.
// Load (6.7.11): a wavetable (.wav) of your own as the "Loaded" table - each
// of eight frames (spread through the table) read for its harmonics, and
// their spectrum, on the same octave scale, becomes the frame's EQ curve. A
// .wav that is not a wavetable gives eight moments through the sound. The
// curves are saved with the patch.
// (6.9.18) Load is a menu: the built-in tables (Vita's, Massif's, a generated
// one - the Wavetable module's) or Load Sample... (wav, aiff, flac, ogg, mp3):
// eight frames picked evenly from the first to the last, whatever the count.
// (6.9.19) Skew: the more it is turned, the quieter the curve's low parts get
// (a power curve on the frame's level, its peaks kept), so the sound leans to
// the quiet side and the peaks stand out. Output up to +4 dB. The view shows
// the frames as their spectra (the curves) or as waveforms (each frame's
// curve played as the harmonics of one cycle) - the view only, not the sound.
// Global. Input: 0 = Audio In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../Shared/FxKit.h"
#include "../../../Platform/AndroidDocuments.h"

using namespace aquanode;

namespace filtertable
{
    constexpr int kBands = 12;
    constexpr int kFrames = 8;
    constexpr float kLoHz = 40.0f, kHiHz = 14000.0f;

    inline float bandHz (float i) { return kLoHz * std::pow (kHiHz / kLoHz, i / (float) (kBands - 1)); }
    inline float octavesFrom (float hz, float ref) { return std::log2 (hz / ref); }

    // (6.9.19) Skew 0..1: the curve's low parts pushed down (a power curve on -18..+18 dB)
    inline float skewDb (float db, float skew)
    {
        if (skew <= 0.0f) return db;
        const float u = juce::jlimit (0.0f, 1.0f, (db + 18.0f) / 36.0f);
        return std::pow (u, 1.0f + 3.0f * skew) * 36.0f - 18.0f + juce::jmax (0.0f, db - 18.0f);
    }

    // a curve as bumps on the octave scale: gain at hz for bumps (centre, dB, width oct)
    struct Bump { float hz, db, width; };
    inline float bumpsAt (float hz, std::initializer_list<Bump> bumps, float base = 0.0f)
    {
        float g = base;
        for (const auto& b : bumps)
        {
            const float o = octavesFrom (hz, b.hz) / b.width;
            g += b.db * std::exp (-0.5f * o * o);
        }
        return g;
    }

    inline const juce::StringArray& tableNames()
    {
        static const juce::StringArray n { "Vowels", "Tilt", "Petals", "Comb", "Telephone", "Air & Body", "Scatter", "Loaded" };
        return n;
    }

    // the curve (dB) of frame f of table t at band hz
    inline float frameDb (int table, int f, float hz)
    {
        const float u = (float) f / (float) (kFrames - 1);   // 0..1 through the table
        switch (table)
        {
            case 0:   // vowels A E I O U and back through A
            {
                static const float formants[kFrames][3] = {
                    { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 },
                    { 325, 700, 2530 },  { 600, 1000, 2600 }, { 500, 1450, 2750 }, { 750, 1200, 2850 } };
                const auto* fm = formants[f];
                return bumpsAt (hz, { { fm[0], 12.0f, 0.28f }, { fm[1], 10.0f, 0.25f }, { fm[2], 7.0f, 0.22f } }, -6.0f);
            }
            case 1:   // dark to bright
            {
                const float tilt = -12.0f + 24.0f * u;   // dB across the whole range
                return tilt * (octavesFrom (hz, 700.0f) / 4.3f);
            }
            case 2:   // petals: three soft peaks opening upward, wider as they rise
            {
                const float c = 150.0f * std::pow (2.0f, u * 4.5f);
                return bumpsAt (hz, { { c, 9.0f, 0.30f + 0.2f * u }, { c * 2.6f, 7.0f, 0.3f + 0.15f * u },
                                      { c * 6.2f, 5.0f, 0.35f } }, -3.0f * u);
            }
            case 3:   // a comb whose teeth walk up by a band per frame
            {
                const float o = octavesFrom (hz, kLoHz) / octavesFrom (kHiHz, kLoHz) * (float) (kBands - 1);
                return 9.0f * std::cos (fxkit::kPi * (o - u * 2.0f));
            }
            case 4:   // a telephone band opening to full range
            {
                const float lo = 900.0f * std::pow (40.0f / 900.0f, u), hi = 2600.0f * std::pow (14000.0f / 2600.0f, u);
                const float below = juce::jmax (0.0f, octavesFrom (lo, hz)), above = juce::jmax (0.0f, octavesFrom (hz, hi));
                return -12.0f * juce::jmin (1.0f, below) - 12.0f * juce::jmin (1.0f, above) + 3.0f * (1.0f - u);
            }
            case 5:   // body (warm low mids) to air (sparkling top)
                return bumpsAt (hz, { { 260.0f, 9.0f * (1.0f - u), 0.6f }, { 11000.0f, 10.0f * u, 0.7f },
                                      { 2500.0f, -4.0f * (1.0f - std::abs (u - 0.5f) * 2.0f), 0.5f } });
            default:  // scatter: smooth seeded random curves
            {
                fxkit::Rng r ((uint32_t) (f * 7919 + 13));
                float g = 0.0f;
                const float o = octavesFrom (hz, kLoHz);
                for (int k = 0; k < 5; ++k)
                {
                    const float c = r.uni() * 8.5f, a = r.bi() * 10.0f, w = 0.25f + r.uni() * 0.5f;
                    const float d = (o - c) / w;
                    g += a * std::exp (-0.5f * d * d);
                }
                return g;
            }
        }
    }
}

// (6.9.18) the built-in tables (Plugins/Vita/PluginVita.cpp, as the Wavetable module uses them)
int aqBuiltinTableCount (int group);
std::string aqBuiltinTableName (int group, int i);
std::vector<float> aqBuiltinTable (int group, int i, uint32_t seed, std::string& name);

class FilterTableModule : public SynthModule
{
public:
    enum ParamIndex { pTable = 0, pPosition, pDepth, pShift, pSharp, pOutput, pDryWet, pSkew, pView };

    FilterTableModule() { rebuildTable (0); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        lastSharp = -1.0f;
        for (auto& s : smooth) s.prepare (sr / 16.0, 0.02f);
        reset();
    }

    void reset() override
    {
        for (auto& ch : bells) for (auto& b : ch) b.reset();
        countdown = 0;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        if (--countdown <= 0)
        {
            countdown = 16;
            const int table = juce::jlimit (0, filtertable::tableNames().size() - 1, (int) param (pTable));
            if (table != builtTable) rebuildTable (table);
            const float sharp = param (pSharp);
            if (sharp != lastSharp)
            {
                lastSharp = sharp;
                for (auto& ch : bells)
                    for (int i = 0; i < filtertable::kBands; ++i)
                        ch[(size_t) i].prepare (sampleRate, filtertable::bandHz ((float) i), sharp);
            }
            const float pos = juce::jlimit (0.0f, 1.0f, param (pPosition) * 0.01f);
            shownPosition.store (pos, std::memory_order_relaxed);
            const float depth = param (pDepth) * 0.01f;
            const float shift = param (pShift) * 0.01f * 1.6f;   // +-1.6 bands (about an octave)
            const float skew = juce::jlimit (0.0f, 1.0f, param (pSkew) * 0.01f);
            // the curve's average gain mostly taken back out: scanning the
            // table changes the colour, much less the loudness
            float dbs[filtertable::kBands], mean = 0.0f;
            for (int i = 0; i < filtertable::kBands; ++i)
            {
                dbs[i] = filtertable::skewDb (curveAt (pos, (float) i - shift), skew) * depth;
                mean += dbs[i] / (float) filtertable::kBands;
            }
            for (int i = 0; i < filtertable::kBands; ++i)
            {
                const float db = juce::jlimit (-24.0f, 24.0f, dbs[i] - 0.75f * mean);
                const float s = smooth[(size_t) i].next (db);
                for (auto& ch : bells) ch[(size_t) i].setGainDb (s);
            }
            outGain = juce::Decibels::decibelsToGain (param (pOutput));
            wet = param (pDryWet) * 0.01f;
        }

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            float y = inputs[0][c];
            for (auto& b : bells[(size_t) c]) y = b.process (y);
            out[c] = inputs[0][c] * (1.0f - wet) + y * outGain * wet;
        }
        outputs[0] = { out[0], out[1] };
    }

    // the table's curve (dB) at a fractional band, at position 0..1
    float curveAt (float pos, float band) const
    {
        const float fp = pos * (float) (filtertable::kFrames - 1);
        const int f0 = juce::jlimit (0, filtertable::kFrames - 2, (int) fp);
        const float ft = fp - (float) f0;
        const float b = juce::jlimit (0.0f, (float) (filtertable::kBands - 1), band);
        const int b0 = juce::jmin (filtertable::kBands - 2, (int) b);
        const float bt = b - (float) b0;
        auto at = [this, b0, bt] (int f)
        {
            return frames[(size_t) f][(size_t) b0] * (1.0f - bt) + frames[(size_t) f][(size_t) b0 + 1] * bt;
        };
        return at (f0) * (1.0f - ft) + at (f0 + 1) * ft;
    }

    // for the card's view (message thread)
    std::atomic<float> shownPosition { 0.0f };
    int shownTable() const { return builtTable; }
    juce::String loadedName() const { return loaded.name; }

    // a wavetable or any sound of your own (message thread): "" when it worked, else what went wrong
    juce::String loadWavetable (const juce::MemoryBlock& data, const juce::String& name)
    {
        // (6.9.18: any of the formats the app reads, not only .wav)
        static juce::AudioFormatManager formats;
        if (formats.getNumKnownFormats() == 0)
        {
            formats.registerBasicFormats();
           #if JUCE_USE_MP3AUDIOFORMAT
            formats.registerFormat (new juce::MP3AudioFormat(), false);
           #endif
        }
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, false)));
        if (reader == nullptr || reader->lengthInSamples < 64) return "not a sound file it can read";
        const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (1 << 22));
        juce::AudioBuffer<float> audio ((int) reader->numChannels, len);
        reader->read (&audio, 0, len, 0, true, true);
        std::vector<float> mono ((size_t) len);
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int i = 0; i < len; ++i) mono[(size_t) i] += audio.getSample (c, i) / (float) audio.getNumChannels();

        // its frame size: a Serum-style 'clm ' chunk ("<!>2048 ..."), else 2048 if it divides evenly
        int frame = 0;
        {
            const auto* bytes = static_cast<const char*> (data.getData());
            for (size_t i = 12; i + 16 < data.getSize() && i < 4096; ++i)
                if (std::memcmp (bytes + i, "clm ", 4) == 0 && std::memcmp (bytes + i + 8, "<!>", 3) == 0)
                { frame = juce::String (juce::CharPointer_UTF8 (bytes + i + 11), 4).getIntValue(); break; }
        }
        if (frame <= 0 && len % 2048 == 0 && len / 2048 >= 1 && len / 2048 <= 256) frame = 2048;
        return curvesFrom (mono, frame, name);
    }

    // (6.9.18) a built-in table: Vita's (0), Massif's (1) or a generated one (2), 2048-sample frames
    juce::String loadBuiltIn (int group, int index, uint32_t seed);

    // the eight curves from a sound: eight of a table's frames, spread evenly from the first
    // to the last (frame > 0), else eight moments through it
    juce::String curvesFrom (const std::vector<float>& mono, int frame, const juce::String& name)
    {
        const int len = (int) mono.size();
        if (len < 64) return "too short";
        const bool table = frame >= 64 && juce::isPowerOfTwo (frame) && len / frame >= 1;

        std::array<std::array<float, filtertable::kBands>, filtertable::kFrames> curves {};
        const int order = table ? juce::roundToInt (std::log2 ((double) frame)) : 12;
        const int n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> work ((size_t) n * 2);
        const int frames = table ? len / frame : 0;
        for (int f = 0; f < filtertable::kFrames; ++f)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            if (table)
            {
                const int src = juce::jlimit (0, frames - 1, juce::roundToInt ((double) f * (frames - 1) / (filtertable::kFrames - 1)));
                for (int i = 0; i < n; ++i) work[(size_t) i] = mono[(size_t) (src * frame + i)];   // (one period: no window)
            }
            else
            {
                const int centre = (int) ((f + 0.5) / filtertable::kFrames * len), start = juce::jlimit (0, juce::jmax (0, len - n), centre - n / 2);
                for (int i = 0; i < n && start + i < len; ++i)
                    work[(size_t) i] = mono[(size_t) (start + i)] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n));
            }
            fft.performFrequencyOnlyForwardTransform (work.data());
            float mean = 0.0f;
            for (int b = 0; b < filtertable::kBands; ++b)
            {
                const float hz = filtertable::bandHz ((float) b);
                // a wavetable's harmonics laid on the EQ's scale as if its fundamental sat at 40 Hz;
                // a sound's own frequencies where they are
                const double binPerHz = table ? 1.0 / 40.0 : (double) n / (sampleRate > 0.0 ? sampleRate : 44100.0);
                const int lo = juce::jmax (1, (int) std::floor (hz * std::pow (2.0, -0.27) * binPerHz));
                const int hi = juce::jlimit (lo, n / 2 - 1, (int) std::ceil (hz * std::pow (2.0, 0.27) * binPerHz));
                double p = 0.0;
                for (int k = lo; k <= hi; ++k) p += (double) work[(size_t) k] * work[(size_t) k];
                // (the natural fall of harmonics taken out - 6 dB an octave for a table, 3 for a
                // sound - so a plain saw is flat and the table's own colour shows)
                curves[(size_t) f][(size_t) b] = 10.0f * (float) std::log10 (p / (double) (hi - lo + 1) + 1.0e-12)
                                                + (table ? 6.0f : 3.0f) * std::log2 (hz / 40.0f);
                mean += curves[(size_t) f][(size_t) b] / (float) filtertable::kBands;
            }
            for (auto& v : curves[(size_t) f]) v = juce::jlimit (-18.0f, 18.0f, (v - mean) * 0.6f);
        }
        loaded.frames = curves;
        loaded.name = name.isNotEmpty() ? name : juce::String ("Wavetable");
        loaded.has = true;
        builtTable = -1;   // (rebuilt on the audio thread's next look)
        return {};
    }

    juce::String saveCustomState() const override
    {
        if (! loaded.has) return {};
        juce::StringArray v;
        for (const auto& fr : loaded.frames) for (float x : fr) v.add (juce::String (x, 1));
        return loaded.name.replaceCharacter ('|', ' ') + "|" + v.joinIntoString (",");
    }
    void loadCustomState (const juce::String& state) override
    {
        if (! state.contains ("|")) return;
        const auto v = juce::StringArray::fromTokens (state.fromFirstOccurrenceOf ("|", false, false), ",", "");
        if (v.size() < filtertable::kFrames * filtertable::kBands) return;
        for (int f = 0; f < filtertable::kFrames; ++f)
            for (int b = 0; b < filtertable::kBands; ++b)
                loaded.frames[(size_t) f][(size_t) b] = juce::jlimit (-18.0f, 18.0f, v[f * filtertable::kBands + b].getFloatValue());
        loaded.name = state.upToFirstOccurrenceOf ("|", false, false);
        loaded.has = true;
        builtTable = -1;
    }
    float frameValue (int f, int band) const { return frames[(size_t) f][(size_t) band]; }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 118; }

private:
    void rebuildTable (int table)
    {
        if (table == filtertable::tableNames().size() - 1)   // Loaded: yours (flat until one is loaded)
        {
            for (int f = 0; f < filtertable::kFrames; ++f)
                for (int i = 0; i < filtertable::kBands; ++i)
                    frames[(size_t) f][(size_t) i] = loaded.has ? loaded.frames[(size_t) f][(size_t) i] : 0.0f;
            builtTable = table;
            return;
        }
        for (int f = 0; f < filtertable::kFrames; ++f)
            for (int i = 0; i < filtertable::kBands; ++i)
                frames[(size_t) f][(size_t) i] = filtertable::frameDb (table, f, filtertable::bandHz ((float) i));
        builtTable = table;
    }

    std::array<std::array<float, filtertable::kBands>, filtertable::kFrames> frames {};
    std::array<std::array<fxkit::Bell, filtertable::kBands>, 2> bells;
    std::array<fxkit::Smoother, filtertable::kBands> smooth;
    int builtTable { -1 }, countdown { 0 };
    struct Loaded { std::array<std::array<float, filtertable::kBands>, filtertable::kFrames> frames {}; juce::String name; bool has { false }; } loaded;
    float lastSharp { -1.0f }, outGain { 1.0f }, wet { 1.0f };
};

//==============================================================================
// the card's view: the table's frames stacked back to front, the curve heard
// now drawn bright in front of them
class FilterTableView : public juce::Component, private juce::Timer
{
public:
    explicit FilterTableView (FilterTableModule& m) : module (&m)
    {
        addAndMakeVisible (loadButton);
        loadButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22303a));
        loadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        loadButton.setTooltip ("A built-in table, or a wavetable or any sound of your own, as the Loaded table: eight of its frames become the EQ curves");
        loadButton.onClick = [this] { showLoadMenu(); };   // (6.9.18: a menu)
        // (6.9.19) the frames as spectra or as waveforms (the view only)
        addAndMakeVisible (viewButton);
        viewButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22303a));
        viewButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        viewButton.setTooltip ("Show the frames as their spectra (the EQ curves) or as waveforms");
        viewButton.onClick = [this]
        {
            if (auto* mm = module.get()) mm->setParameter ("view", waveMode() ? 0.0f : 1.0f);
            updateViewButton();
            repaint();
        };
        updateViewButton();
        startTimerHz (20);
    }

    void resized() override
    {
        loadButton.setBounds (getWidth() - 66, 6, 58, 18);
        viewButton.setBounds (getWidth() - 136, 6, 66, 18);
    }

    bool waveMode() const
    {
        auto* mm = module.get();
        return mm != nullptr && mm->getParameterBase (FilterTableModule::pView) > 0.5f;
    }
    void updateViewButton() { viewButton.setButtonText (waveMode() ? "Waveform" : "Spectrum"); }

    // (6.9.19) a curve as one cycle: harmonic h (at 40 Hz x h on the curve's scale,
    // its level the curve's, less 6 dB per octave as the curves were taken), peak-normalised
    static void cycleFrom (std::function<float (float)> dbAtBand, float* out, int n)
    {
        constexpr int kH = 48;
        static float sinT[1024];
        static bool ready = false;
        if (! ready) { for (int i = 0; i < 1024; ++i) sinT[i] = std::sin (juce::MathConstants<float>::twoPi * (float) i / 1024.0f); ready = true; }
        float amp[kH + 1] {};
        const float bandsPerOct = (float) (filtertable::kBands - 1) / std::log2 (filtertable::kHiHz / filtertable::kLoHz);
        for (int h = 1; h <= kH; ++h)
        {
            const float band = std::log2 ((float) h) * bandsPerOct;
            if (band > (float) (filtertable::kBands - 1)) break;
            amp[h] = juce::Decibels::decibelsToGain (dbAtBand (band)) / (float) h;
        }
        float peak = 1.0e-6f;
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int h = 1; h <= kH; ++h)
                if (amp[h] > 0.0f) v += amp[h] * sinT[(h * i * 1024 / n) & 1023];
            out[i] = v;
            peak = juce::jmax (peak, std::abs (v));
        }
        for (int i = 0; i < n; ++i) out[i] /= peak;
    }

    // (6.9.18) Load: the built-in tables, or a file
    void showLoadMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Load Sample...");
        m.addSeparator();
        static const char* const groups[3] = { "Vita's Tables", "Massif's Tables", "Generate (a new one each time)" };
        for (int g = 0; g < 3; ++g)
        {
            juce::PopupMenu sub;
            for (int i = 0; i < aqBuiltinTableCount (g); ++i)
                sub.addItem (100 + g * 1000 + i, juce::String (aqBuiltinTableName (g, i)));
            if (sub.getNumItems() > 0) m.addSubMenu (groups[g], sub);
        }
        juce::Component::SafePointer<FilterTableView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 1) { safe->load(); return; }
            auto* mod = dynamic_cast<FilterTableModule*> (safe->module.get());
            if (mod == nullptr) return;
            const int g = (r - 100) / 1000, i = (r - 100) % 1000;
            if (mod->loadBuiltIn (g, i, (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30)).isEmpty())
                mod->setParameter ("table", (float) (filtertable::tableNames().size() - 1));
            safe->repaint();
        });
    }

    void load()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a wavetable or a sound", juce::File(),
                                                       aqp::android::pickerPatterns ("*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"));
        juce::Component::SafePointer<FilterTableView> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto* m = dynamic_cast<FilterTableModule*> (safe->module.get());
            const auto url = fc.getURLResult();
            if (m == nullptr || url.isEmpty()) return;
            juce::MemoryBlock data;
            if (const auto problem = aqp::files::readDocument (url, data); problem.isNotEmpty()) return;
            juce::String name = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : juce::String();
           #if JUCE_ANDROID
            if (name.isEmpty())
                if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                    name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
           #endif
            if (name.isEmpty()) name = juce::URL::removeEscapeChars (url.getFileName()).upToLastOccurrenceOf (".", false, false);
            const auto problem = m->loadWavetable (data, name);
            if (problem.isNotEmpty())
            {
                juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Filter Table EQ", "The file could not be loaded: " + problem + ".");
                return;
            }
            m->setParameter ("table", (float) (filtertable::tableNames().size() - 1));
            safe->repaint();
        });
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = dynamic_cast<FilterTableModule*> (module.get());
        if (m == nullptr) return;
        const auto r = getLocalBounds().toFloat().reduced (4.0f, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (r, 6.0f);

        const float depthX = 26.0f, depthY = 26.0f;   // how far back the last frame sits
        const auto front = juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + depthY + 4.0f,
                                                   r.getWidth() - depthX - 12.0f, r.getHeight() - depthY - 10.0f);
        const float pos = m->shownPosition.load (std::memory_order_relaxed);
        auto curvePath = [&front] (std::function<float (float)> dbAt, float dx, float dy)
        {
            juce::Path p;
            const int steps = 48;
            for (int i = 0; i <= steps; ++i)
            {
                const float band = (float) i / (float) steps * (float) (filtertable::kBands - 1);
                const float x = front.getX() + front.getWidth() * (float) i / (float) steps + dx;
                const float y = front.getCentreY() - juce::jlimit (-1.0f, 1.0f, dbAt (band) / 18.0f) * front.getHeight() * 0.48f - dy;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };

        if (waveMode())
        {
            // (6.9.19) the frames as waveforms, stacked the same way
            static constexpr int n = 96;
            auto wavePath = [&front] (const float* w, float dx, float dy)
            {
                juce::Path p;
                for (int i = 0; i < n; ++i)
                {
                    const float x = front.getX() + front.getWidth() * (float) i / (float) (n - 1) + dx;
                    const float y = front.getCentreY() - w[i] * front.getHeight() * 0.42f - dy;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                return p;
            };
            float w[n];
            for (int f = filtertable::kFrames - 1; f >= 0; --f)
            {
                const float back = (float) f / (float) (filtertable::kFrames - 1);
                cycleFrom ([m, f] (float band)
                {
                    const int b0 = juce::jmin (filtertable::kBands - 2, (int) band);
                    const float t = band - (float) b0;
                    return m->frameValue (f, b0) * (1.0f - t) + m->frameValue (f, b0 + 1) * t;
                }, w, n);
                const bool near = std::abs (back - pos) < 0.5f / (float) (filtertable::kFrames - 1);
                g.setColour (juce::Colours::white.withAlpha (near ? 0.45f : 0.16f));
                g.strokePath (wavePath (w, back * depthX, back * depthY), juce::PathStrokeType (1.0f));
            }
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int) front.getCentreY(), front.getX(), front.getRight());
            const float skew = juce::jlimit (0.0f, 1.0f, m->getParameterBase (FilterTableModule::pSkew) * 0.01f);
            cycleFrom ([m, pos, skew] (float band) { return filtertable::skewDb (m->curveAt (pos, band), skew); }, w, n);
            g.setColour (juce::Colour (0xffffe08a));
            g.strokePath (wavePath (w, pos * depthX, pos * depthY), juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            drawLabel (g, *m, r, pos);
            return;
        }

        // the frames, back (the last) to front (the first)
        for (int f = filtertable::kFrames - 1; f >= 0; --f)
        {
            const float back = (float) f / (float) (filtertable::kFrames - 1);
            auto p = curvePath ([m, f] (float band)
            {
                const int b0 = juce::jmin (filtertable::kBands - 2, (int) band);
                const float t = band - (float) b0;
                return m->frameValue (f, b0) * (1.0f - t) + m->frameValue (f, b0 + 1) * t;
            }, back * depthX, back * depthY);
            const bool near = std::abs (back - pos) < 0.5f / (float) (filtertable::kFrames - 1);
            g.setColour (juce::Colours::white.withAlpha (near ? 0.45f : 0.16f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }

        // the 0 dB line and the curve playing now, at its place in the stack
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine ((int) front.getCentreY(), front.getX(), front.getRight());
        const float skew = juce::jlimit (0.0f, 1.0f, m->getParameterBase (FilterTableModule::pSkew) * 0.01f);
        auto now = curvePath ([m, pos, skew] (float band) { return filtertable::skewDb (m->curveAt (pos, band), skew); }, pos * depthX, pos * depthY);
        g.setColour (juce::Colour (0xffffe08a));
        g.strokePath (now, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawLabel (g, *m, r, pos);
    }

    void drawLabel (juce::Graphics& g, FilterTableModule& mod, juce::Rectangle<float> r, float pos)
    {
        auto* m = &mod;
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        const int st = juce::jmax (0, m->shownTable());
        const auto tableLabel = st == filtertable::tableNames().size() - 1
                                    ? (m->loadedName().isNotEmpty() ? "Loaded: " + m->loadedName() : juce::String ("Loaded (none yet: Load one)"))
                                    : filtertable::tableNames()[st];
        g.drawText (tableLabel + "  frame " + juce::String (1.0f + pos * 7.0f, 1),
                    r.reduced (8.0f, 3.0f), juce::Justification::topLeft);
    }

private:
    void timerCallback() override
    {
        auto* m = dynamic_cast<FilterTableModule*> (module.get());
        if (m == nullptr) return;
        const float p = m->shownPosition.load (std::memory_order_relaxed);
        const float sk = m->getParameterBase (FilterTableModule::pSkew);
        if (p != lastPos || m->shownTable() != lastTable || sk != lastSkew)
        {
            lastSkew = sk;
            lastPos = p;
            lastTable = m->shownTable();
            repaint();
        }
    }

    juce::WeakReference<SynthModule> module;
    float lastPos { -1.0f }, lastSkew { -1.0f };
    int lastTable { -2 };
    juce::TextButton loadButton { "Load..." }, viewButton { "Spectrum" };
    std::unique_ptr<juce::FileChooser> chooser;
};

juce::String FilterTableModule::loadBuiltIn (int group, int index, uint32_t seed)
{
    std::string name;
    const auto frames = aqBuiltinTable (group, index, seed, name);
    if (frames.empty()) return "no such table";
    return curvesFrom (frames, 2048, juce::String (name));
}

std::unique_ptr<juce::Component> FilterTableModule::createExtraContentComponent()
{
    return std::make_unique<FilterTableView> (*this);
}

static ModuleDescriptor filterTableDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.filtertable";
    d.displayName = "Filter Table EQ";
    d.description =
        "A wavetable of EQ curves: eight frames per table (vowels, dark to bright, petals opening, a walking "
        "comb ...), and Position glides through them - put an LFO or an envelope on Position and the sound's "
        "colour travels. Depth scales the curve (negative turns it upside down), Shift slides it up or down, "
        "Sharp narrows the bands, Skew makes the curve's low parts quieter (the peaks stand out). The view shows "
        "the frames as spectra or as waveforms.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 60;
    d.searchKeywords = "wavetable eq formant vowel morph";
    d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
    d.params = {
        makeCombo  ("table",    "Table", filtertable::tableNames(), 0, 0, 2),
        makeRotary ("position", "Position", 0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("depth",    "Depth",    -200.0f, 200.0f, 100.0f, 0, "%"),
        makeRotary ("shift",    "Shift",    -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("sharp",    "Sharp",    0.5f, 4.0f, 1.4f, 1, {}, true),
        makeRotary ("output",   "Output",   -18.0f, 4.0f, 0.0f, 1, "dB"),    // (6.9.19: up to +4 dB, was +12)
        makeRotary ("dryWet",   "Dry/Wet",  0.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("skew",     "Skew",     0.0f, 100.0f, 0.0f, 0, "%"),     // (6.9.19)
        makeCombo  ("view",     "View", { "Spectrum", "Waveform" }, 0, 0, 2).noMod().hide()   // (6.9.19, the card's view button)
    };
    d.extraSection = "Table";
    return d;
}

AQUANODE_REGISTER_MODULE (FilterTableModule, filterTableDescriptor)

// for tests: a wavetable loaded from memory, and a frame's curve
juce::String filterTableLoadForTest (aquanode::SynthModule* m, const juce::MemoryBlock& data)
{
    auto* f = dynamic_cast<FilterTableModule*> (m);
    return f != nullptr ? f->loadWavetable (data, "test") : juce::String ("not a Filter Table");
}
float filterTableFrameDb (aquanode::SynthModule* m, int frame, int band)
{
    auto* f = dynamic_cast<FilterTableModule*> (m);
    return f != nullptr ? f->frameValue (frame, band) : 0.0f;
}

#if AQUANODE_TEST_HARNESS
// (test harness) a built-in table into a Filter Table EQ: "" when it worked
juce::String filterTableLoadBuiltInForTest (SynthModule& m, int group, int index)
{
    auto* ft = dynamic_cast<FilterTableModule*> (&m);
    return ft != nullptr ? ft->loadBuiltIn (group, index, 7) : juce::String ("not a Filter Table EQ");
}
#endif
