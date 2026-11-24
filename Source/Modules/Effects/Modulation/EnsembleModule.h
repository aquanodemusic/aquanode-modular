#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/VintageDSP.h"

// Ensemble - a vintage polysynth effect section: Chorus, Phase or Ensemble, the
// three-phase BBD ensemble with a slow sweep plus a faster shimmer that makes
// its pads so wide. Global (it works on the summed voices, as on a polysynth).
// (6.9.16) Super: a unison detuner - the dry sound in the middle and up to six
// pitch-shifted copies around it (two crossfaded taps on a delay line each, the
// tap gliding through a 40 ms window so the copy plays a few cents off), at the
// Super oscillator's classic offsets, panned by their detune, mixed with its
// Mix curves; Speed and Depth drift each copy's detune slowly.
// Input: 0 = Audio In. Output: 0 = Audio Out (stereo).
class EnsembleModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pMode = 0, pSpeed, pDepth, pSpread, pMix, pVoices, pDetune };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fx.prepare (sr);
        super.prepare (sr);
    }

    void reset() override { fx.reset(); super.reset(); }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const int mode = (int) param (pMode);
        if (mode == 3)
        {
            const auto o = super.process (inputs[0][0], inputs[0][1], (int) std::lround (param (pVoices)), param (pDetune),
                                          param (pSpread) * 0.01f, param (pMix) * 0.01f, param (pSpeed), param (pDepth) * 0.01f);
            outputs[0] = { o[0], o[1] };
            return;
        }
        const auto o = fx.process (inputs[0][0], inputs[0][1], param (pSpeed) * 0.1f, param (pDepth) * 0.01f,
                                   param (pSpread) * 0.01f, param (pMix) * 0.01f, mode);
        outputs[0] = { o[0], o[1] };
    }

private:
    // the unison detuner (Super)
    struct SuperDetuner
    {
        static constexpr int kCopies = 6, kSize = 1 << 13;
        // the Super oscillator's classic offsets (its seven saws without the middle one), -1..1, nearest first
        static constexpr float kPos[kCopies] = { -0.17749f, 0.18102f, -0.57168f, 0.56514f, -1.0f, 0.97684f };

        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            window = juce::jlimit (64.0f, (float) kSize - 8.0f, 0.040f * sr);
            reset();
        }
        void reset()
        {
            std::fill (std::begin (buf), std::end (buf), 0.0f);
            w = 0;
            for (int k = 0; k < kCopies; ++k)
            {
                phase[k] = (float) k / (float) kCopies;   // (the copies' taps apart: no common crossfade)
                driftFrom[k] = driftTo[k] = 0.0f;
                driftPhase[k] = 1.0f;
            }
        }

        float read (float delay) const
        {
            const float pos = (float) w - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;
            auto at = [this] (int j) { return buf[(size_t) (j & (kSize - 1))]; };
            const float xm = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            // (cubic Hermite)
            const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        std::array<float, 2> process (float l, float r, int voices, float edgeCents, float spread, float mix,
                                      float speed, float depth)
        {
            buf[(size_t) w] = 0.5f * (l + r);
            voices = juce::jlimit (1, kCopies, voices);
            const float centre = juce::jlimit (0.0f, 1.0f, -0.55366f * mix + 0.99785f);
            const float side = juce::jlimit (0.0f, 1.0f, -0.73764f * mix * mix + 1.2841f * mix + 0.044372f)
                               / std::sqrt ((float) voices);
            const float driftRate = 0.05f + speed * 0.2f;   // Hz (Speed 0..10)
            float outL = 0.0f, outR = 0.0f;
            for (int k = 0; k < voices; ++k)
            {
                // the drift: a smooth random path (cosine-joined points), up to a third of the copy's detune
                driftPhase[k] += driftRate * (0.8f + 0.1f * (float) k) / sr;
                if (driftPhase[k] >= 1.0f)
                {
                    driftPhase[k] -= std::floor (driftPhase[k]);
                    driftFrom[k] = driftTo[k];
                    driftTo[k] = rnd.nextFloat() * 2.0f - 1.0f;
                }
                const float t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * driftPhase[k]);
                const float drift = (driftFrom[k] + (driftTo[k] - driftFrom[k]) * t) * depth * 0.33f;
                const float cents = kPos[k] * edgeCents * (1.0f + drift);
                const float ratio = std::exp2 (cents / 1200.0f);
                // the tap glides through the window at (1 - ratio): the copy plays at ratio
                phase[k] += (1.0f - ratio) / window;
                phase[k] -= std::floor (phase[k]);
                const float pa = phase[k], pb = pa + 0.5f - std::floor (pa + 0.5f);
                const float ga = std::sin (juce::MathConstants<float>::pi * pa), gb = std::sin (juce::MathConstants<float>::pi * pb);
                const float y = read (2.0f + pa * window) * ga * ga + read (2.0f + pb * window) * gb * gb;
                const float pan = juce::jlimit (-1.0f, 1.0f, kPos[k] * spread);
                const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                outL += y * std::cos (ang) * 1.41421356f;
                outR += y * std::sin (ang) * 1.41421356f;
            }
            w = (w + 1) & (kSize - 1);
            return { l * centre + outL * side, r * centre + outR * side };
        }

        float buf[kSize] {};
        int w { 0 };
        float sr { 44100.0f }, window { 1764.0f };
        float phase[kCopies] {}, driftFrom[kCopies] {}, driftTo[kCopies] {}, driftPhase[kCopies] {};
        juce::Random rnd { 7 };
    };

    vintagedsp::EnsembleCore fx;
    SuperDetuner super;
};
