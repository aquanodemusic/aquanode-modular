//==============================================================================
// Free Delay - any delay time from a fraction of a sample to ten seconds
//
// Time runs on a logarithmic knob from 1 us to 10 s that gives every decade
// the same share of its travel, so most of it is spent where small changes
// matter: a quarter of the way is 56 us (under three samples), the middle
// 3.2 ms, three quarters 180 ms (type a value to set it exactly). Below a sample
// the delay is a fraction of a sample (read between the last two samples). With
// Feedback the loop is the delay itself, to the fraction of a sample, so a short
// delay is a comb tuned to exactly 1 / Time (a loop is one sample at least).
//
// The delay line reads with a 4-point cubic (linear below two samples), and a
// time that moves glides there over Glide (0: at once) - modulating Time gives
// tape-like pitch bends, flanging, chorusing. Feedback (negative inverts it,
// for the odd-harmonic comb) goes through Damp, a low-pass, and is softly
// limited so it never runs away. Spread sets the right side's time apart from
// the left's (+-50 %), Mix the wet share.
//
// Global effect. Inputs: 0 = Audio In. Output: 0 = Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

namespace
{
    using namespace aquanode;

    constexpr double kMaxSeconds = 10.0;
}

class FreeDelayModule final : public SynthModule
{
public:
    enum P { pTime, pFeedback, pMix, pGlide, pDamp, pSpread };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.freedelay";
        d.displayName = "Free Delay";
        d.description = "A delay of any time from a fraction of a sample to 10 seconds, on a knob that spends most of its "
                        "travel on the short times (the middle is 3 ms). Short times with Feedback are tuned combs, "
                        "longer ones flangers, slaps and echoes; Glide lets a changed time slide there (modulate Time for "
                        "tape bends). Negative Feedback inverts the repeats, Damp darkens them, Spread sets the right "
                        "side's time apart.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 1;
        d.listedAfter = "fx.delay";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };

        auto time = makeRotary ("time", "Time", 0.001f, 10000.0f, 250.0f, 0, "ms").decades();
        time.textFromValue = [] (double ms)
        {
            if (ms >= 1000.0) return juce::String (ms / 1000.0, 3) + " s";
            if (ms >= 100.0)  return juce::String (juce::roundToInt (ms)) + " ms";
            if (ms >= 1.0)    return juce::String (ms, 2) + " ms";
            if (ms >= 0.1)    return juce::String (juce::roundToInt (ms * 1000.0)) + " us";
            return juce::String (ms * 1000.0, 1) + " us";
        };
        time.valueFromText = [] (const juce::String& t)
        {
            const auto v = t.getDoubleValue();
            if (t.containsIgnoreCase ("us")) return v / 1000.0;
            if (t.trim().endsWithIgnoreCase ("s") && ! t.containsIgnoreCase ("ms")) return v * 1000.0;
            return v;
        };
        auto damp = makeRotary ("damp", "Damp", 200.0f, 20000.0f, 20000.0f, 1, "Hz", true);
        damp.textFromValue = [] (double hz) { return hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + "k" : juce::String (juce::roundToInt (hz)) + " Hz"; };
        d.params = {
            time,
            makeRotary ("feedback", "Feedback", -99.0f, 99.0f, 0.0f, 0, "%", false, 1.0f),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f),
            makeRotary ("glide", "Glide", 0.0f, 2000.0f, 20.0f, 1, "ms"),
            damp,
            makeRotary ("spread", "Spread", -50.0f, 50.0f, 0.0f, 1, "%", false, 1.0f)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        int size = 4;
        while (size < (int) (kMaxSeconds * sr) + 8)
            size <<= 1;
        mask = size - 1;
        for (auto& l : line) l.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset() override
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0;
        for (int c = 0; c < 2; ++c) { lastOut[c] = 0.0f; lp[c] = 0.0f; delay[c] = targetSamples (c); }
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float fb = juce::jlimit (-0.99f, 0.99f, param (pFeedback) * 0.01f);
        const float mix = param (pMix) * 0.01f;
        const float glideMs = param (pGlide);
        const float glideK = glideMs < 0.05f ? 1.0f : 1.0f - std::exp (-1.0f / (glideMs * 0.001f * (float) sampleRate));
        const float dampHz = param (pDamp);
        const float dampK = dampHz >= 19990.0f ? 1.0f : 1.0f - std::exp (-juce::MathConstants<float>::twoPi * dampHz / (float) sampleRate);

        for (int c = 0; c < 2; ++c)
        {
            delay[c] += glideK * (targetSamples (c) - delay[c]);

            // the feedback: the line read at the delay (one sample at least - a loop
            // cannot be shorter), so the comb is tuned to exactly 1 / Time
            const double loop = juce::jmax (1.0, delay[c]);
            float back = read (c, loop);
            back = std::isfinite (back) ? back : 0.0f;
            lp[c] += dampK * (back - lp[c]);

            // into the line: the input and the damped, softly limited feedback
            line[c][(size_t) writePos] = in[0][(size_t) c]
                                         + juce::dsp::FastMathApproximations::tanh (juce::jlimit (-3.0f, 3.0f, fb * lp[c]));

            // what is heard: the same read, or below a sample one between this sample and the last
            float y = delay[c] >= 1.0 ? back : read (c, delay[c]);
            y = std::isfinite (y) ? y : 0.0f;
            lastOut[c] = y;
            out[0][(size_t) c] = in[0][(size_t) c] * (1.0f - mix) + y * mix;
        }
        writePos = (writePos + 1) & mask;
    }

private:
    double targetSamples (int c) const
    {
        const double spread = c == 1 ? 1.0 + param (pSpread) * 0.01 : 1.0;
        return juce::jlimit (0.0, (double) (line[0].size() - 4), (double) param (pTime) * 0.001 * sampleRate * spread);
    }

    // the line d samples ago (d = 0: what was written last)
    float read (int c, double d) const noexcept
    {
        const auto& l = line[c];
        const int i = (int) d;
        const float f = (float) (d - i);
        auto at = [&] (int back) { return l[(size_t) ((writePos - back) & mask)]; };
        if (d < 2.0)
            return at (i) + (at (i + 1) - at (i)) * f;   // between the last two samples
        // 4-point cubic (Catmull-Rom) around the read point
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    std::vector<float> line[2];
    int mask { 0 }, writePos { 0 };
    double delay[2] {};
    float lastOut[2] {}, lp[2] {};
};

AQUANODE_REGISTER_MODULE (FreeDelayModule, FreeDelayModule::descriptor)
