// Bloom Reverb - a reverb that opens like a flower: the sound first runs
// through a chain of long allpass diffusers (Bloom sets how long), which smear
// it slowly into a cloud, then into an eight-line feedback delay network with
// gently moving lines. Swell fades the reverb in after a note starts, so it
// rises under the sound instead of answering it at once.
// Global. Input: 0 = Audio In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../Shared/FxKit.h"

using namespace aquanode;

class BloomReverbModule : public SynthModule
{
public:
    enum ParamIndex { pSize = 0, pDecay, pBloom, pPreDelay, pDamp, pMod, pSwell, pWidth, pLowCut, pDryWet };

    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const auto ms = [sr] (double m) { return (int) (m * 0.001 * sr) + 8; };
        preDelay.prepare (ms (260.0));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kDiffusers; ++i)
                diff[c][i].prepare (ms (kDiffMs[i] * 13.5 * 1.1));
        for (int i = 0; i < kLines; ++i)
            lines[i].prepare (ms (kLineMs[i] * 2.0 * 1.05 + 4.0));
        swellEnv.prepare (sr, 0.005f);
        reset();
    }

    void reset() override
    {
        preDelay.reset();
        for (auto& ch : diff) for (auto& d : ch) d.reset();
        for (auto& l : lines) l.reset();
        for (auto& v : damp) v = 0.0f;
        for (auto& v : lowState) v = 0.0f;
        for (auto& v : dcState) v = 0.0f;
        swell = 0.0f;
        lfoPhase = 0.0;
        countdown = 0;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        if (--countdown <= 0) { countdown = 32; update(); }

        const float inL = inputs[0][0], inR = inputs[0][1];

        // pre-delay (mid / side kept: the stereo image goes in whole)
        preDelay.push (0.5f * (inL + inR));
        const float side = 0.5f * (inL - inR);
        const float pd = preDelay.read (juce::jmax (1.0f, preSamples));

        // the bloom: long allpass diffusers smearing the onset into a cloud
        float d[2] = { pd + side * 0.5f, pd - side * 0.5f };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kDiffusers; ++i)
                d[c] = diff[c][i].process (d[c], diffLen[c][i], diffGain);

        // eight delay lines through a Householder matrix
        lfoPhase += lfoInc;
        if (lfoPhase > 1.0) lfoPhase -= 1.0;
        float y[kLines];
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float wob = modDepth * std::sin ((float) (2.0 * juce::MathConstants<double>::pi * (lfoPhase + i * 0.137)));
            y[i] = lines[i].read (lineLen[i] + wob);
            // damping: a one-pole low-pass in each line
            damp[i] += dampCoef * (y[i] - damp[i]);
            y[i] = damp[i] * lineGain[i];
            sum += y[i];
        }
        const float h = sum * (2.0f / (float) kLines);
        for (int i = 0; i < kLines; ++i)
        {
            const float in = (i % 2 == 0 ? d[0] : d[1]) * 0.6f;
            lines[i].push (fxkit::flushDenormal (y[i] - h + in));
        }
        float wl = 0.0f, wr = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float s = (i & 2) ? -1.0f : 1.0f;
            if (i % 2 == 0) wl += y[i] * s; else wr += y[i] * s;
        }
        wl *= 0.45f; wr *= 0.45f;

        // low cut on the reverb (a one-pole high-pass), and a fixed DC guard
        for (int c = 0; c < 2; ++c)
        {
            float& w = c == 0 ? wl : wr;
            lowState[c] += lowCoef * (w - lowState[c]);
            w -= lowState[c];
        }

        // width (mid / side of the reverb)
        const float mid = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * width;
        wl = mid + sd; wr = mid - sd;

        // swell: the reverb fades in after the input starts, and is reset to
        // fade in again once the reverb itself has died away
        const float inLevel = swellEnv.next (std::abs (inL) + std::abs (inR));
        const float wetLevel = std::abs (wl) + std::abs (wr);
        if (swellRate >= 1.0f) swell = 1.0f;
        else
        {
            if (inLevel > 1.0e-3f) swell = juce::jmin (1.0f, swell + swellRate);
            else if (wetLevel < 3.0e-4f && inLevel < 1.0e-4f) swell = juce::jmax (0.0f, swell - swellRate * 4.0f);
        }
        const float sw = swell * swell * (3.0f - 2.0f * swell);

        outputs[0] = { inL * (1.0f - wet) + wl * sw * wet,
                       inR * (1.0f - wet) + wr * sw * wet };
    }

    double voiceTailSeconds() const override { return 0.0; }

private:
    static constexpr double kDiffMs[kDiffusers] = { 4.7, 7.3, 11.9, 17.3 };
    static constexpr double kLineMs[kLines] = { 29.7, 37.1, 41.1, 43.7, 53.9, 59.3, 67.1, 73.3 };

    void update()
    {
        const double sr = sampleRate;
        const float size = param (pSize) * 0.01f;            // 0..1
        const float scale = 0.35f + 1.6f * size;             // line lengths x 0.35 .. 1.95
        const float decay = juce::jmax (0.1f, param (pDecay));
        for (int i = 0; i < kLines; ++i)
        {
            lineLen[i] = (float) (kLineMs[i] * scale * 0.001 * sr);
            lineGain[i] = std::pow (10.0f, -3.0f * lineLen[i] / (decay * (float) sr));
        }
        const float bloom = param (pBloom) * 0.01f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kDiffusers; ++i)
                diffLen[c][i] = (float) (kDiffMs[i] * (1.0 + bloom * 12.0) * (c == 0 ? 1.0 : 1.07) * 0.001 * sr);
        diffGain = 0.5f + 0.2f * bloom;
        preSamples = (float) (param (pPreDelay) * 0.001 * sr);
        dampCoef = 1.0f - std::exp (-2.0f * fxkit::kPi * param (pDamp) / (float) sr);
        modDepth = param (pMod) * 0.01f * (float) (0.0012 * sr);
        lfoInc = 0.31 / sr;
        const float swellS = param (pSwell);
        swellRate = swellS <= 0.001f ? 1.0f : 1.0f / (swellS * (float) sr);
        width = param (pWidth) * 0.01f;
        lowCoef = 1.0f - std::exp (-2.0f * fxkit::kPi * param (pLowCut) / (float) sr);
        wet = param (pDryWet) * 0.01f;
    }

    fxkit::DelayLine preDelay;
    fxkit::Allpass diff[2][kDiffusers];
    fxkit::DelayLine lines[kLines];
    float lineLen[kLines] {}, lineGain[kLines] {}, damp[kLines] {};
    float diffLen[2][kDiffusers] {};
    float diffGain { 0.6f }, preSamples { 0.0f }, dampCoef { 0.5f }, modDepth { 0.0f };
    float swellRate { 1.0f }, swell { 0.0f }, width { 1.0f }, lowCoef { 0.0f }, wet { 0.35f };
    float lowState[2] {}, dcState[2] {};
    double lfoPhase { 0.0 }, lfoInc { 0.0 };
    fxkit::Smoother swellEnv;
    int countdown { 0 };
};

static ModuleDescriptor bloomReverbDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.bloomverb";
    d.displayName = "Bloom Reverb";
    d.description =
        "A reverb that opens like a flower: Bloom smears the sound through long diffusers before it reaches the "
        "room, so the reverb grows instead of answering at once. Swell fades it in after each phrase starts; "
        "Mod moves the room's lines for a softly shimmering tail.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 52;
    d.searchKeywords = "reverb ambient swell diffuse pad shimmer";
    d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
    d.params = {
        makeRotary ("size",     "Size",      0.0f, 100.0f, 60.0f, 0, "%"),
        makeRotary ("decay",    "Decay",     0.3f, 30.0f, 4.5f, 0, "s", true),
        makeRotary ("bloom",    "Bloom",     0.0f, 100.0f, 60.0f, 0, "%"),
        makeRotary ("preDelay", "Pre-Delay", 0.0f, 250.0f, 20.0f, 0, "ms"),
        makeRotary ("damp",     "Damp",      800.0f, 18000.0f, 6500.0f, 1, "Hz", true),
        makeRotary ("mod",      "Mod",       0.0f, 100.0f, 35.0f, 1, "%"),
        makeRotary ("swell",    "Swell",     0.0f, 3.0f, 0.4f, 1, "s"),
        makeRotary ("width",    "Width",     0.0f, 150.0f, 100.0f, 1, "%"),
        makeRotary ("lowCut",   "Low Cut",   20.0f, 600.0f, 90.0f, 2, "Hz", true),
        makeRotary ("dryWet",   "Dry/Wet",   0.0f, 100.0f, 35.0f, 2, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BloomReverbModule, bloomReverbDescriptor)
