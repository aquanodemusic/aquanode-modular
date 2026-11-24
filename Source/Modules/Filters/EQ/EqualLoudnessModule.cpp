//==============================================================================
// Equal Loudness (6.9.18) - an EQ from the ear's equal-loudness contours (the
// Fletcher-Munson curves, in their present form: ISO 226:2003), made into an
// impulse response and convolved.
//
//   Compensate        lows and highs raised where the ear hears them softer at
//                     that level, so all frequencies sound equally loud
//   Imitate the Ear   the other way: they are lowered, as the ear hears them
//
// Phon is the listening level the contour is for (20 - 90: at low levels the
// ear loses the lows the most, at high ones the curves flatten). The curve is
// taken against 1 kHz (0 dB there). Amount scales it; Range is the most it may
// raise or lower (a soft limit - near 20 Hz the contours ask for 50 dB and more).
// No boost below 20 Hz (it fades back to 0 dB by 10 Hz) nor past 12.5 kHz (the
// standard's last frequency: held there).
// (6.9.19) Curve: Measured (the default) - two equal-loudness responses the user
// measured and sent (their magnitude, read in 1/6-octave bands; the audio files
// themselves are not used), Level going from the quiet one (0 %: +34 dB at 20 Hz,
// a dip of -4 dB around 3 kHz, the highs rising only past 10 kHz) to the loud one
// (100 %: +26 dB at 20 Hz, the highs rising from 3 kHz). ISO 226 is the curve of
// 6.9.18 (Phon; patches from then keep it). Range now only reins in what passes
// three quarters of it (a soft knee), so the measured curves come through whole.
// The impulse response is minimum phase (no latency of its own, as an analogue
// EQ), about 170 ms long; it is rebuilt when a knob moves. Save IR writes it as
// a .wav (IR Forge and the Convolver load it). Latency: 64 samples (the blocks).
// Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/ImpulseSave.h"

using namespace aquanode;

namespace loudness
{
    // ISO 226:2003 - the frequencies, and af, Lu, Tf at each
    constexpr int kPoints = 29;
    constexpr float kHz[kPoints] = { 20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000,
                                     1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };
    constexpr float kAf[kPoints] = { 0.532f, 0.506f, 0.480f, 0.455f, 0.432f, 0.409f, 0.387f, 0.367f, 0.349f, 0.330f, 0.315f,
                                     0.301f, 0.288f, 0.276f, 0.267f, 0.259f, 0.253f, 0.250f, 0.246f, 0.244f, 0.243f, 0.243f,
                                     0.243f, 0.242f, 0.242f, 0.245f, 0.254f, 0.271f, 0.301f };
    constexpr float kLu[kPoints] = { -31.6f, -27.2f, -23.0f, -19.1f, -15.9f, -13.0f, -10.3f, -8.1f, -6.2f, -4.5f, -3.1f,
                                     -2.0f, -1.1f, -0.4f, 0.0f, 0.3f, 0.5f, 0.0f, -2.7f, -4.1f, -1.0f, 1.7f, 2.5f, 1.2f,
                                     -2.1f, -7.1f, -11.2f, -10.7f, -3.1f };
    constexpr float kTf[kPoints] = { 78.5f, 68.7f, 59.5f, 51.1f, 44.0f, 37.5f, 31.5f, 26.5f, 22.1f, 17.9f, 14.4f, 11.4f,
                                     8.6f, 6.2f, 4.4f, 3.0f, 2.2f, 2.4f, 3.5f, 1.7f, -1.3f, -4.2f, -6.0f, -5.4f, -1.5f,
                                     6.0f, 12.6f, 13.9f, 12.3f };

    // (6.9.19) the measured responses, against 1 kHz (dB): quiet (Level 0 %) and loud (100 %)
    constexpr int kMeasured = 64;
    constexpr float kMeasuredHz[kMeasured] = { 10.0f, 11.3f, 12.8f, 14.5f, 16.4f, 18.5f, 21.0f, 23.7f, 26.9f, 30.4f, 34.4f, 38.9f, 44.0f, 49.8f, 56.4f, 63.8f, 72.2f, 81.7f, 92.4f, 104.6f, 118.3f, 133.9f, 151.5f, 171.4f, 194.0f, 219.5f, 248.3f, 281.0f, 317.9f, 359.7f, 407.0f, 460.6f, 521.1f, 589.6f, 667.2f, 754.9f, 854.2f, 966.5f, 1093.6f, 1237.4f, 1400.1f, 1584.2f, 1792.6f, 2028.3f, 2295.0f, 2596.8f, 2938.3f, 3324.6f, 3761.8f, 4256.5f, 4816.2f, 5449.6f, 6166.2f, 6977.0f, 7894.5f, 8932.6f, 10107.2f, 11436.3f, 12940.2f, 14641.8f, 16567.2f, 18745.8f, 21210.8f, 24000.0f };
    constexpr float kQuiet[kMeasured] = { 33.70f, 33.70f, 33.70f, 33.70f, 33.70f, 33.70f, 33.70f, 33.70f, 33.70f, 33.70f, 33.70f, 33.66f, 33.27f, 31.98f, 29.83f, 28.13f, 26.88f, 25.61f, 24.01f, 22.14f, 20.60f, 19.32f, 17.51f, 16.72f, 15.77f, 13.69f, 11.98f, 11.33f, 9.21f, 8.17f, 7.60f, 6.06f, 4.75f, 3.51f, 2.41f, 1.70f, 0.95f, 0.18f, -0.76f, -1.82f, -1.99f, -2.78f, -3.50f, -3.41f, -3.85f, -4.10f, -3.79f, -3.89f, -3.64f, -3.68f, -3.26f, -2.84f, -2.45f, -1.93f, -1.36f, -0.80f, -0.03f, 0.95f, 2.05f, 3.71f, 6.48f, 11.87f, 14.07f, 14.07f };
    constexpr float kLoud[kMeasured] = { 26.12f, 26.12f, 26.12f, 26.11f, 26.08f, 25.98f, 25.78f, 25.39f, 24.75f, 23.90f, 22.86f, 21.69f, 20.31f, 18.82f, 17.28f, 16.67f, 16.46f, 15.57f, 14.23f, 12.73f, 11.80f, 11.25f, 10.14f, 9.55f, 8.64f, 7.41f, 5.87f, 5.36f, 5.53f, 4.69f, 3.28f, 2.70f, 1.83f, 1.51f, 1.22f, 0.68f, 0.49f, -0.16f, -0.17f, -0.43f, -0.36f, -0.15f, -0.61f, 0.19f, -0.12f, 0.32f, 1.26f, 1.35f, 2.08f, 2.78f, 3.55f, 4.47f, 5.44f, 6.47f, 7.32f, 8.43f, 9.50f, 10.58f, 12.23f, 14.13f, 17.21f, 22.80f, 25.16f, 25.16f };

    inline double measuredAt (double hz, double level01)
    {
        auto at = [hz] (const float* t)
        {
            if (hz <= kMeasuredHz[0]) return (double) t[0];
            if (hz >= kMeasuredHz[kMeasured - 1]) return (double) t[kMeasured - 1];
            int i = 0;
            while (i < kMeasured - 2 && hz > kMeasuredHz[i + 1]) ++i;
            const double u = std::log (hz / kMeasuredHz[i]) / std::log ((double) kMeasuredHz[i + 1] / kMeasuredHz[i]);
            return t[i] + u * (t[i + 1] - t[i]);
        };
        return at (kQuiet) + juce::jlimit (0.0, 1.0, level01) * (at (kLoud) - at (kQuiet));
    }

    // the sound pressure level (dB) heard as loud as `phon` at the standard's i-th frequency
    inline double splAt (int i, double phon)
    {
        const double af = kAf[i], lu = kLu[i], tf = kTf[i];
        const double a = 4.47e-3 * (std::pow (10.0, 0.025 * phon) - 1.15) + std::pow (0.4 * std::pow (10.0, (tf + lu) / 10.0 - 9.0), af);
        return 10.0 / af * std::log10 (juce::jmax (1.0e-12, a)) - lu + 94.0;
    }

    // the contour against 1 kHz (dB), between the standard's frequencies on a log scale
    struct Contour
    {
        explicit Contour (double phon)
        {
            for (int i = 0; i < kPoints; ++i) rel[i] = splAt (i, phon);
            const double ref = rel[17];   // 1 kHz
            for (auto& r : rel) r -= ref;
        }
        double at (double hz) const
        {
            if (hz <= kHz[0]) return rel[0];
            if (hz >= kHz[kPoints - 1]) return rel[kPoints - 1];
            int i = 0;
            while (i < kPoints - 2 && hz > kHz[i + 1]) ++i;
            const double t = std::log (hz / kHz[i]) / std::log ((double) kHz[i + 1] / kHz[i]);
            return rel[i] + t * (rel[i + 1] - rel[i]);
        }
        double rel[kPoints] {};
    };
}

class EqualLoudnessModule final : public SynthModule, private juce::Thread
{
public:
    enum P { pMode = 0, pPhon, pAmount, pRange, pSave, pCurve, pLevel };
    static constexpr int kBlock = 64;

    EqualLoudnessModule() : juce::Thread ("Equal Loudness IR") {}
    ~EqualLoudnessModule() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (5000);
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.equalloudness";
        d.displayName = "Equal Loudness";
        d.searchKeywords = "fletcher munson equal loudness contour iso 226 phon convolution impulse response ir eq";
        d.description = "An EQ from the ear's equal-loudness contours (the Fletcher-Munson curves, as ISO 226:2003 has them), "
                        "convolved as an impulse response. Compensate raises the lows and highs the ear hears softer at that "
                        "level, so all frequencies sound equally loud; Imitate the Ear lowers them, as the ear hears them. "
                        "Curve: Measured (from two measured responses, Level from the quiet one to the loud one) or ISO 226 "
                        "(Phon: the listening level - the curves flatten as it rises). Amount scales the curve (0 dB at 1 kHz), "
                        "Range is the most it may raise or lower. Save IR writes the impulse response as a .wav.";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 30;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("mode", "Mode", { "Compensate", "Imitate the Ear" }, 0, 0, 2).noMod(),
            makeRotary ("phon", "Phon", 20.0f, 90.0f, 60.0f, 0, "phon", false, 1.0f).noMod().visibleWhen ("curve", 1.0f),
            makeRotary ("amount", "Amount", 0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f).noMod(),
            makeRotary ("range", "Range", 6.0f, 48.0f, 48.0f, 0, "dB", false, 1.0f).noMod(),
            makeButton ("saveIR", "Save IR", 1, 2),
            // (6.9.19) the measured curves; patches from 6.9.18 keep ISO 226
            makeCombo ("curve", "Curve", { "Measured", "ISO 226" }, 0, 0, 2).noMod().whenMissing (1.0f),
            makeRotary ("level", "Level", 0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f).noMod().visibleWhen ("curve", 0.0f)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    int latencySamples() const override { return kBlock; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        {
            const juce::ScopedLock sl (convLock);
            convolution.prepare ({ sr, (juce::uint32) kBlock, 2 });
            convolution.reset();
        }
        if (isThreadRunning()) { signalThreadShouldExit(); notify(); stopThread (5000); }
        builtKey = {};   // (prepared afresh: the IR goes in again)
        irReady.store (false);
        reset();
        startThread (juce::Thread::Priority::low);
        notify();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBlock; ++i) inBuf[c][i] = outBuf[c][i] = dlyBuf[c][i] = 0.0f;
        blockPos = 0;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const float inL = inputs[0][0], inR = inputs[0][1];
        const float wetL = outBuf[0][blockPos], wetR = outBuf[1][blockPos];
        const float dryL = dlyBuf[0][blockPos], dryR = dlyBuf[1][blockPos];
        dlyBuf[0][blockPos] = inBuf[0][blockPos] = inL;
        dlyBuf[1][blockPos] = inBuf[1][blockPos] = inR;
        if (++blockPos >= kBlock)
        {
            blockPos = 0;
            bool done = false;
            if (irReady.load (std::memory_order_acquire))
            {
                float* inPtrs[2] = { inBuf[0], inBuf[1] };
                float* outPtrs[2] = { outBuf[0], outBuf[1] };
                juce::dsp::AudioBlock<float> inBlock (inPtrs, 2, (size_t) kBlock), outBlock (outPtrs, 2, (size_t) kBlock);
                const juce::ScopedTryLock tl (convLock);
                if (tl.isLocked())
                {
                    convolution.process (juce::dsp::ProcessContextNonReplacing<float> (inBlock, outBlock));
                    done = true;
                }
            }
            if (! done)   // (no IR yet: the sound as it came, as late as the convolved one)
                for (int c = 0; c < 2; ++c) std::copy (std::begin (inBuf[c]), std::end (inBuf[c]), std::begin (outBuf[c]));
        }
        const bool ready = irReady.load (std::memory_order_relaxed);
        outputs[0] = { ready ? wetL : dryL, ready ? wetR : dryR };   // (a knob moved: the thread sees it within 100 ms)
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id != "saveIR") return;
        auto ir = makeIR (currentKey());
        juce::AudioBuffer<float> buf (1, (int) ir.size());
        std::copy (ir.begin(), ir.end(), buf.getWritePointer (0));
        impulse::save (chooser, buf, sampleRate > 0.0 ? sampleRate : 48000.0,
                       juce::String ("Equal Loudness ") + (currentKey().mode == 0 ? "Compensate " : "Ear ") + juce::String (juce::roundToInt (currentKey().phon)) + " phon",
                       "Equal Loudness");
    }

    // the curve the knobs ask for, in dB at hz (the card's view and the tests)
    float curveDb (double hz) const { return (float) gainDb (currentKey(), hz); }
    bool ready() const { return irReady.load(); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 96; }
    int preferredModuleWidth() const override { return 330; }

private:
    struct Key
    {
        int mode { -1 }, curve { 0 };
        float phon { 0 }, amount { 0 }, range { 0 }, level { 0 };
        double rate { 0 };
        bool operator== (const Key& o) const { return mode == o.mode && phon == o.phon && amount == o.amount && range == o.range && rate == o.rate
                                                      && curve == o.curve && level == o.level; }
    };
    Key currentKey() const
    {
        Key k;
        k.mode = (int) std::lround (getParameterBase (pMode));
        k.phon = std::round (getParameterBase (pPhon));
        k.amount = getParameterBase (pAmount);
        k.range = getParameterBase (pRange);
        k.curve = (int) std::lround (getParameterBase (pCurve));
        k.level = getParameterBase (pLevel);
        k.rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        return k;
    }

public:
    double curveDbForTest (double hz) const { return gainDb (currentKey(), hz); }
private:
    static double gainDb (const Key& k, double hz)
    {
        const double sign = k.mode == 1 ? -1.0 : 1.0;
        double g;
        if (k.curve == 0)
        {
            // (6.9.19) measured; Range a soft knee past three quarters of it
            g = loudness::measuredAt (juce::jmax (1.0, hz), k.level * 0.01) * sign * k.amount * 0.01;
            const double knee = 0.75 * k.range, room = 0.25 * k.range, mag = std::abs (g);
            if (mag > knee) g = (g < 0.0 ? -1.0 : 1.0) * (knee + room * std::tanh ((mag - knee) / juce::jmax (0.5, room)));
        }
        else
        {
            const loudness::Contour c (k.phon);
            g = c.at (juce::jmax (1.0, hz)) * sign * k.amount * 0.01;
            g = k.range * std::tanh (g / juce::jmax (1.0f, k.range));
        }
        if (g > 0.0 && hz < 20.0)   // (no boost of the infrasound: back to 0 dB by 10 Hz)
            g *= juce::jlimit (0.0, 1.0, std::log2 (juce::jmax (1.0, hz) / 10.0));
        return g;
    }

    // a minimum-phase IR for the curve: log|H| -> real cepstrum -> folded -> exp -> IR
    static std::vector<float> makeIR (const Key& k)
    {
        const double rate = k.rate;
        const int len = juce::nextPowerOfTwo ((int) (rate * 0.17));   // (8192 at 48 kHz: down to about 10 Hz)
        const int order = juce::roundToInt (std::log2 ((double) len)) + 2;   // (4x the length: little cepstral aliasing)
        const int n = 1 << order, half = n / 2;
        const loudness::Contour contour (k.phon);
        juce::dsp::FFT fft (order);
        std::vector<juce::dsp::Complex<float>> a ((size_t) n), b ((size_t) n);
        for (int i = 0; i <= half; ++i)
        {
            const double hz = (double) i * rate / (double) n;
            const float lm = (float) (gainDb (k, hz) * 0.11512925465);   // dB -> natural log
            a[(size_t) i] = { lm, 0.0f };
            if (i > 0 && i < half) a[(size_t) (n - i)] = { lm, 0.0f };
        }
        fft.perform (a.data(), b.data(), true);   // -> cepstrum
        std::fill (a.begin(), a.end(), juce::dsp::Complex<float> { 0.0f, 0.0f });
        a[0] = { b[0].real(), 0.0f };
        for (int i = 1; i < half; ++i) a[(size_t) i] = { 2.0f * b[(size_t) i].real(), 0.0f };
        a[(size_t) half] = { b[(size_t) half].real(), 0.0f };
        fft.perform (a.data(), b.data(), false);   // -> log spectrum with its minimum phase
        for (int i = 0; i < n; ++i)
        {
            const float mag = std::exp (juce::jlimit (-60.0f, 60.0f, b[(size_t) i].real()));
            a[(size_t) i] = { mag * std::cos (b[(size_t) i].imag()), mag * std::sin (b[(size_t) i].imag()) };
        }
        fft.perform (a.data(), b.data(), true);   // -> the impulse response
        std::vector<float> ir ((size_t) len);
        const int fadeStart = len - len / 8;
        for (int i = 0; i < len; ++i)
        {
            float w = 1.0f;
            if (i >= fadeStart) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - fadeStart) / (float) (len - fadeStart));
            ir[(size_t) i] = b[(size_t) i].real() * w;
        }
        return ir;
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            const auto key = currentKey();
            if (! (key == builtKey) && key.rate > 0.0)
            {
                auto ir = makeIR (key);
                juce::AudioBuffer<float> buf (1, (int) ir.size());
                std::copy (ir.begin(), ir.end(), buf.getWritePointer (0));
                {
                    const juce::ScopedLock sl (convLock);
                    convolution.loadImpulseResponse (std::move (buf), key.rate, juce::dsp::Convolution::Stereo::no,
                                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
                }
                builtKey = key;
                irReady.store (true, std::memory_order_release);
            }
            wait (100);
        }
    }

    juce::CriticalSection convLock;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 1024 } };
    std::atomic<bool> irReady { false };
    Key builtKey;   // (the builder thread's own)
    float inBuf[2][kBlock] {}, outBuf[2][kBlock] {}, dlyBuf[2][kBlock] {};
    int blockPos { 0 };
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// the card: the curve (dB against frequency)
//==============================================================================
namespace
{
    class LoudnessView final : public juce::Component, private juce::Timer
    {
    public:
        explicit LoudnessView (EqualLoudnessModule& m) : module (&m), raw (&m) { setInterceptsMouseClicks (false, false); startTimerHz (10); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (r, 4.0f);
            if (module.get() == nullptr) return;
            auto plot = r.reduced (6.0f, 6.0f).withTrimmedBottom (10.0f);
            constexpr float span = 40.0f;   // +-40 dB
            auto yOf = [&] (float db) { return plot.getCentreY() - juce::jlimit (-span, span, db) / span * plot.getHeight() * 0.5f; };
            auto xOf = [&] (double hz) { return plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * plot.getWidth(); };
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (double hz : { 50.0, 200.0, 1000.0, 5000.0 })
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.drawVerticalLine ((int) xOf (hz), plot.getY(), plot.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (xOf (hz) - 14.0f, plot.getBottom() + 1.0f, 28.0f, 10.0f), juce::Justification::centred, false);
            }
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawHorizontalLine ((int) yOf (0.0f), plot.getX(), plot.getRight());
            juce::Path p;
            for (int i = 0; i <= 120; ++i)
            {
                const double hz = 20.0 * std::pow (1000.0, i / 120.0);   // (20 Hz - 20 kHz)
                const float x = xOf (hz), y = yOf (raw->curveDb (hz));
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (juce::Colour (0xff8fd6ff));
            g.strokePath (p, juce::PathStrokeType (1.8f));
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawText ("+40 dB", plot.withHeight (11.0f), juce::Justification::topLeft, false);
            g.drawText ("-40 dB", plot.withTop (plot.getBottom() - 11.0f), juce::Justification::bottomLeft, false);
        }
    private:
        void timerCallback() override { repaint(); }
        juce::WeakReference<SynthModule> module;
        EqualLoudnessModule* raw;
    };
}

std::unique_ptr<juce::Component> EqualLoudnessModule::createExtraContentComponent()
{
    return std::make_unique<LoudnessView> (*this);
}

AQUANODE_REGISTER_MODULE (EqualLoudnessModule, EqualLoudnessModule::descriptor)

#if AQUANODE_TEST_HARNESS
// (test harness, 6.9.19) the curve the module builds its IR from, in dB at hz
double equalLoudnessCurveDbForTest (aquanode::SynthModule& m, double hz)
{
    auto* e = dynamic_cast<EqualLoudnessModule*> (&m);
    return e != nullptr ? e->curveDbForTest (hz) : 0.0;
}
#endif
