//==============================================================================
// Reverser (6.7.11) - plays the sound backwards, window by window.
//
// While Reverse is on (above 50 %), every Size of sound (a note length in
// time, or milliseconds with Sync on Free) comes out backwards: two read
// heads take turns, each running back through the last stretch of sound and
// fading in and out (Hann), so the windows join without clicks. Reversing
// needs the sound before it can turn it round: the module is Size late, and
// says so (the host lines the track up); the dry sound is delayed the same,
// so switching Reverse on and off never jumps in time. Mix, Level (for an
// Automation Lane to open and close).
// Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class ReverserModule final : public SynthModule
{
public:
    enum P { pReverse = 0, pSize, pSync, pSizeMs, pMix, pLevel };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int need = juce::nextPowerOfTwo ((int) (sr * 7.0));
        for (auto& b : buf) b.assign ((size_t) need, 0.0f);
        mask = need - 1;
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0; engageEnv = 0.0f; phase = 0.0;
        window = (double) sizeSamples();
    }

    int latencySamples() const override { return (int) std::lround (window); }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        buf[0][(size_t) (writePos & mask)] = inputs[0][0];
        buf[1][(size_t) (writePos & mask)] = inputs[0][1];
        const double L = window;

        // two heads, half a window apart: each reads back from now to 2 L ago
        float rl = 0.0f, rr = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            double p = phase + 0.5 * h;
            p -= std::floor (p);
            const double delay = 2.0 * p * L + 1.0;
            const float w = (float) std::sin (juce::MathConstants<double>::pi * p);
            const float win = w * w;
            const double pos = (double) writePos - delay;
            const juce::int64 i0 = (juce::int64) std::floor (pos);
            const float f = (float) (pos - (double) i0);
            for (int c = 0; c < 2; ++c)
            {
                const float a = buf[(size_t) c][(size_t) (i0 & mask)], b = buf[(size_t) c][(size_t) ((i0 + 1) & mask)];
                (c == 0 ? rl : rr) += (a + (b - a) * f) * win;
            }
        }
        phase += 1.0 / juce::jmax (16.0, L);
        if (phase >= 1.0) { phase -= 1.0; window = (double) sizeSamples(); }   // (Size taken up at a head's turn)

        // the dry sound, as late as the reversed one
        const juce::int64 d = writePos - (juce::int64) std::lround (L);
        const float dl = buf[0][(size_t) (d & mask)], dr = buf[1][(size_t) (d & mask)];
        const bool on = param (pReverse) >= 50.0f;
        engageEnv += ((on ? 1.0f : 0.0f) - engageEnv) * (float) (1.0 / (0.008 * sampleRate));
        const float e = engageEnv;
        const float sl = dl * (1.0f - e) + rl * e, sr = dr * (1.0f - e) + rr * e;
        const float mix = param (pMix) * 0.01f, level = param (pLevel);
        outputs[0] = { (dl * (1.0f - mix) + sl * mix) * level, (dr * (1.0f - mix) + sr * mix) * level };
        ++writePos;
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.reverser";
        d.displayName = "Reverser";
        d.searchKeywords = "reverse backwards glitch reverser";
        d.description =
            "Plays the sound backwards while Reverse is on (above 50 %), window by window: every Size (a note length in "
            "time, or milliseconds with Sync on Free) comes out turned round, two heads crossfading so the windows join "
            "smoothly. It is Size late and reports it (the dry sound is delayed the same, so switching never jumps). "
            "Mix, Level (for an Automation Lane to open and close).";
        d.section = ModuleSection::Effect;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"), tempoIn() };
        d.params = {
            makeRotary ("reverse", "Reverse", 0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
            makeSteppedList ("size", "Size", { "1/2 bar", "1/4", "1/8", "1/16", "1/32" }, 2, 0),
            makeCombo ("sync", "Sync", { "Tempo", "Free" }, 0, 0, 2).noMod(),
            makeRotary ("sizeMs", "Size (ms)", 20.0f, 2000.0f, 250.0f, 1, "ms", true).visibleWhen ("sync", 1.0f),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 1, "%", false, 1.0f),
            makeRotary ("level", "Level", 0.0f, 1.0f, 1.0f, 1)
        };
        return d;
    }

private:
    std::vector<float> buf[2];
    juce::int64 mask { 0 }, writePos { 0 };
    double phase { 0.0 }, window { 12000.0 };
    float engageEnv { 0.0f };

    int sizeSamples() const
    {
        static constexpr double beats[5] = { 2.0, 1.0, 0.5, 0.25, 0.125 };
        double s;
        if (getParameterBase (pSync) > 0.5f) s = getParameterBase (pSizeMs) * 0.001 * sampleRate;
        else s = beats[juce::jlimit (0, 4, (int) std::lround (getParameterBase (pSize)))] * 60.0 / juce::jmax (40.0, tempoBpm > 1.0 ? tempoBpm : 120.0) * sampleRate;
        return (int) juce::jlimit (64.0, sampleRate * 3.0, s);
    }
};

AQUANODE_REGISTER_MODULE (ReverserModule, ReverserModule::descriptor)
