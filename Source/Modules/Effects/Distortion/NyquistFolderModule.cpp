//==============================================================================
// Nyquist Folder (6.9.17) - a lower Nyquist for any sound, as FM12's Nyquist
// Limit does for its operators: everything above the Nyquist set folds back
// under it (a tone at Nyquist + 500 Hz comes out at Nyquist - 500 Hz, and so on,
// mirrored back and forth).
//
//   Fold          the sound sampled at twice the Nyquist (no filter before, so
//                 the highs fold), then smoothed back with a steep low-pass at
//                 the Nyquist: only the folded spectrum, no steps
//   Fold + Steps  the held steps too (a sample-rate reducer's crunch: the
//                 images above the Nyquist stay)
//   Reduce        nothing folds: a steep low-pass at the Nyquist alone
//
// The low-pass is an 8-pole Butterworth (four biquads). Nyquist at the top
// (the host's own): nothing changes. Mix blends in the dry sound. Flexible:
// per voice when patched per voice. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class NyquistFolderModule final : public SynthModule
{
public:
    enum P { pNyquist = 0, pMode, pMix };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.nyquistfolder";
        d.displayName = "Nyquist Folder";
        d.searchKeywords = "nyquist fold alias sample rate reduce reducer lofi decimate fm12";
        d.description = "A lower Nyquist for any sound, as FM12's Nyquist Limit: what lies above it folds back under it, "
                        "mirrored. Fold: only the folded spectrum, smooth. Fold + Steps: with the held steps of a sample-rate "
                        "reducer. Reduce: nothing folds, the highs are cut above the Nyquist. Mix blends in the dry sound.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 19;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        auto ny = makeRotary ("nyquist", "Nyquist", 100.0f, 24000.0f, 4000.0f, 0, "Hz", true);
        ny.textFromValue = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
        d.params = {
            ny,
            makeCombo ("mode", "Mode", { "Fold", "Fold + Steps", "Reduce" }, 0, 0, 2).noMod(),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 0, "%")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < kMaxVoices; ++v) voiceReset (v); }
    void voiceReset (int v) override
    {
        auto& s = state[(size_t) v];
        s = {};
        s.acc = 1.0;   // (a point at once)
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override { processVoiceSample (0, inputs, outputs); }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        auto& s = state[(size_t) v];
        const float inL = inputs[0][0], inR = inputs[0][1];
        const float sr = (float) sampleRate;
        const float nyquist = juce::jmin (param (pNyquist), 0.5f * sr);
        const int mode = juce::jlimit (0, 2, (int) std::lround (param (pMode)));
        const float mix = param (pMix) * 0.01f;

        // the filter follows the Nyquist (every 32 samples, or when the mode changes)
        if (--s.ctrlLeft < 0 || mode != s.mode)
        {
            s.ctrlLeft = 31;
            s.mode = mode;
            const float fc = juce::jmin (nyquist, 0.45f * sr);
            if (std::abs (fc - s.fc) > 0.5f) { s.fc = fc; design (s, fc, sr); }
        }

        float wetL = inL, wetR = inR;
        if (nyquist < 0.499f * sr)
        {
            if (mode != 2)
            {
                // points at twice the Nyquist, read between the samples (no filter before: the highs fold)
                const double step = 2.0 * (double) nyquist / (double) sr;
                const double before = s.acc;
                s.acc += step;
                if (s.acc >= 1.0)
                {
                    const float t = (float) juce::jlimit (0.0, 1.0, (1.0 - before) / step);
                    s.heldL = s.prevL + (inL - s.prevL) * t;
                    s.heldR = s.prevR + (inR - s.prevR) * t;
                    s.acc -= std::floor (s.acc);
                }
                wetL = s.heldL;
                wetR = s.heldR;
            }
            if (mode != 1)   // Fold: smoothed back; Reduce: the cut alone
            {
                wetL = filter (s, 0, wetL);
                wetR = filter (s, 1, wetR);
            }
        }
        s.prevL = inL;
        s.prevR = inR;
        if (! std::isfinite (wetL) || ! std::isfinite (wetR)) { voiceReset (v); wetL = wetR = 0.0f; }
        outputs[0] = { inL + mix * (wetL - inL), inR + mix * (wetR - inR) };
    }

private:
    struct Biquad { float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; float z1[2] {}, z2[2] {}; };
    struct State
    {
        Biquad bq[4];
        double acc { 1.0 };
        float heldL { 0 }, heldR { 0 }, prevL { 0 }, prevR { 0 }, fc { -1.0f };
        int ctrlLeft { 0 }, mode { -1 };
    };

    // an 8-pole Butterworth low-pass: four sections, their Qs from the poles' angles
    static void design (State& s, float fc, float sr)
    {
        static constexpr float qs[4] = { 0.50979558f, 0.60134489f, 0.89997622f, 2.5629154f };
        const float w = juce::MathConstants<float>::twoPi * fc / sr;
        const float cw = std::cos (w), sw = std::sin (w);
        for (int k = 0; k < 4; ++k)
        {
            const float alpha = sw / (2.0f * qs[k]);
            const float a0 = 1.0f + alpha;
            auto& b = s.bq[k];
            b.b0 = (1.0f - cw) * 0.5f / a0;
            b.b1 = (1.0f - cw) / a0;
            b.b2 = b.b0;
            b.a1 = -2.0f * cw / a0;
            b.a2 = (1.0f - alpha) / a0;
        }
    }

    static float filter (State& s, int ch, float x)
    {
        for (auto& b : s.bq)
        {
            // (transposed direct form II)
            const float y = b.b0 * x + b.z1[ch];
            b.z1[ch] = b.b1 * x - b.a1 * y + b.z2[ch];
            b.z2[ch] = b.b2 * x - b.a2 * y;
            x = y;
        }
        return x;
    }

    std::array<State, kMaxVoices> state {};
};

AQUANODE_REGISTER_MODULE (NyquistFolderModule, NyquistFolderModule::descriptor)
