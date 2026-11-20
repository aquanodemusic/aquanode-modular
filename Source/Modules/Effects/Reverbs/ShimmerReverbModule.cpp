//==============================================================================
// Shimmer Reverb (6.7.10) - a reverb whose tail climbs (or sinks) as it rings.
//
// Bloom Reverb's room - long allpass diffusers smearing the sound into a
// cloud, then eight gently moving delay lines through a Householder matrix -
// with a pitch shifter in its feedback: part of the tail (Shimmer) is
// shifted by Shift semitones (-24..+24, whole steps; Cents fine-tunes) and
// sent back in through the diffusers, so every pass climbs one more
// interval, each new one smeared as softly as the first. That is the
// classic feedback loop of a pitch shifter and a reverb, brewed into one.
//
// The shifter: four overlapping read heads on a short buffer, each faded in
// and out with a Hann window (Smooth: how long each grain is - longer is
// smoother and blurrier). Damp darkens the room and keeps the climbing
// octaves soft; Low Cut keeps sinking ones out of the mud. The loop rides a
// gentle level guard, so a shimmer that piles up never runs away.
// Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/FxKit.h"

using namespace aquanode;

class ShimmerReverbModule : public SynthModule
{
public:
    enum ParamIndex { pShift = 0, pCents, pShimmer, pSmooth, pDecay, pSize, pBloom, pPreDelay, pDamp, pMod, pLowCut, pWidth, pDryWet };

    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;
    static constexpr int kHeads = 4;

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
        for (auto& b : shiftBuf) b.prepare (ms (420.0));
        reset();
    }

    void reset() override
    {
        preDelay.reset();
        for (auto& ch : diff) for (auto& d : ch) d.reset();
        for (auto& l : lines) l.reset();
        for (auto& b : shiftBuf) b.reset();
        for (auto& v : damp) v = 0.0f;
        for (auto& v : lowState) v = 0.0f;
        for (int h = 0; h < kHeads; ++h) headPhase[h] = (float) h / (float) kHeads;
        loopLp[0] = loopLp[1] = loopHp[0] = loopHp[1] = 0.0f;
        feedL = feedR = 0.0f; guard = 0.0f;
        inEnv = tailEnv = lastTail = 0.0f; regen = 1.0f; guardCount = 0;
        lfoPhase = 0.0;
        countdown = 0;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        if (--countdown <= 0) { countdown = 32; update(); }

        const float inL = inputs[0][0], inR = inputs[0][1];
        preDelay.push (0.5f * (inL + inR));
        const float side = 0.5f * (inL - inR);
        const float pd = preDelay.read (juce::jmax (1.0f, preSamples));

        // the climbing tail comes back in before the diffusers: each pass as soft as the first
        float d[2] = { pd + side * 0.5f + feedL, pd - side * 0.5f + feedR };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kDiffusers; ++i)
                d[c] = diff[c][i].process (d[c], diffLen[c][i], diffGain);

        lfoPhase += lfoInc;
        if (lfoPhase > 1.0) lfoPhase -= 1.0;
        float y[kLines];
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float wob = modDepth * std::sin ((float) (2.0 * juce::MathConstants<double>::pi * (lfoPhase + i * 0.137)));
            y[i] = lines[i].read (lineLen[i] + wob);
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

        // the shimmer: the tail shifted and sent round again
        shiftBuf[0].push (wl); shiftBuf[1].push (wr);
        float sl = 0.0f, sr = 0.0f, wsum = 0.0f;
        for (int k = 0; k < kHeads; ++k)
        {
            headPhase[k] += headInc;
            if (headPhase[k] >= 1.0f) headPhase[k] -= 1.0f;
            if (headPhase[k] < 0.0f) headPhase[k] += 1.0f;
            const float ph = headPhase[k];
            const float w = std::sin (juce::MathConstants<float>::pi * ph);
            const float win = w * w;
            const float delay = 3.0f + ph * grain;
            sl += shiftBuf[0].read (delay) * win;
            sr += shiftBuf[1].read (delay + 0.37f * grain * 0.1f) * win;   // (the right a hair apart: width)
            wsum += win;
        }
        const float norm = wsum > 1.0e-3f ? 1.0f / wsum : 0.0f;
        sl *= norm; sr *= norm;
        // soft at the top (Damp), clear at the bottom (Low Cut), and a level guard on the loop
        for (int c = 0; c < 2; ++c)
        {
            float& v = c == 0 ? sl : sr;
            loopLp[c] += loopLpCoef * (v - loopLp[c]); v = loopLp[c];
            loopHp[c] += loopHpCoef * (v - loopHp[c]); v -= loopHp[c];
        }
        const float lvl = std::abs (sl) + std::abs (sr);
        guard += (lvl > guard ? 0.002f : 0.00005f) * (lvl - guard);
        // the decay guard: once the input is quiet the tail must keep falling (at
        // least at two thirds of Decay's rate) - if the loop holds it up, it eases off
        inEnv += 0.0005f * (std::abs (inL) + std::abs (inR) - inEnv);
        tailEnv += 0.0005f * (std::abs (wl) + std::abs (wr) - tailEnv);
        if (++guardCount >= kGuardHop)
        {
            guardCount = 0;
            const bool quiet = inEnv < 0.05f * tailEnv;
            if (quiet && tailEnv > 1.0e-6f && lastTail > 1.0e-6f)
            {
                const float dropDb = 20.0f * std::log10 (lastTail / tailEnv);
                const float wantDb = 60.0f * (float) kGuardHop / (float) sampleRate / (1.5f * juce::jmax (0.1f, param (pDecay)));
                regen = dropDb < wantDb ? regen * 0.85f : juce::jmin (1.0f, regen * 1.02f);
            }
            else regen = juce::jmin (1.0f, regen * 1.05f);
            lastTail = tailEnv;
        }
        const float g = shimmerGain * regen / (1.0f + 3.0f * guard);
        feedL = fxkit::flushDenormal (std::tanh (sl * g * 1.2f) / 1.2f);
        feedR = fxkit::flushDenormal (std::tanh (sr * g * 1.2f) / 1.2f);

        // low cut and width on the reverb
        for (int c = 0; c < 2; ++c)
        {
            float& w = c == 0 ? wl : wr;
            lowState[c] += lowCoef * (w - lowState[c]);
            w -= lowState[c];
        }
        const float mid = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * width;
        wl = mid + sd; wr = mid - sd;
        outputs[0] = { inL * (1.0f - wet) + wl * wet, inR * (1.0f - wet) + wr * wet };
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.shimmerverb";
        d.displayName = "Shimmer Reverb";
        d.description =
            "A reverb whose tail climbs as it rings: Bloom Reverb's room with a pitch shifter in its feedback. Shimmer "
            "sends part of the tail, shifted by Shift (-24..+24 semitones; Cents fine-tunes), back in through the "
            "diffusers, so every pass rises one more interval - as softly smeared as the first. Smooth: the shifter's "
            "grain length (longer is smoother). Decay, Size, Bloom, Pre-Delay, Damp (keeps the climbing octaves soft), "
            "Mod, Low Cut, Width, Dry/Wet.";
        d.section = ModuleSection::Effect;
        d.searchKeywords = "reverb shimmer octave pitch ambient pad heavenly feedback";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("shift",    "Shift",     -24.0f, 24.0f, 12.0f, 0, "st", false, 1.0f),
            makeRotary ("cents",    "Cents",     -100.0f, 100.0f, 0.0f, 0, "ct", false, 1.0f),
            makeRotary ("shimmer",  "Shimmer",   0.0f, 100.0f, 45.0f, 0, "%"),
            makeRotary ("smooth",   "Smooth",    30.0f, 200.0f, 100.0f, 0, "ms", false, 1.0f),
            makeRotary ("decay",    "Decay",     0.5f, 30.0f, 6.0f, 1, "s", true),
            makeRotary ("size",     "Size",      0.0f, 100.0f, 70.0f, 1, "%"),
            makeRotary ("bloom",    "Bloom",     0.0f, 100.0f, 60.0f, 1, "%"),
            makeRotary ("preDelay", "Pre-Delay", 0.0f, 250.0f, 20.0f, 1, "ms"),
            makeRotary ("damp",     "Damp",      800.0f, 18000.0f, 7000.0f, 2, "Hz", true),
            makeRotary ("mod",      "Mod",       0.0f, 100.0f, 30.0f, 2, "%"),
            makeRotary ("lowCut",   "Low Cut",   20.0f, 600.0f, 120.0f, 2, "Hz", true),
            makeRotary ("width",    "Width",     0.0f, 150.0f, 110.0f, 2, "%"),
            makeRotary ("dryWet",   "Dry/Wet",   0.0f, 100.0f, 35.0f, 3, "%")
        };
        return d;
    }

private:
    static constexpr double kDiffMs[kDiffusers] = { 4.7, 7.3, 11.9, 17.3 };
    static constexpr double kLineMs[kLines] = { 29.7, 37.1, 41.1, 43.7, 53.9, 59.3, 67.1, 73.3 };

    void update()
    {
        const double sr = sampleRate;
        const float size = param (pSize) * 0.01f;
        const float scale = 0.35f + 1.6f * size;
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
        const float dampHz = param (pDamp);
        dampCoef = 1.0f - std::exp (-2.0f * fxkit::kPi * dampHz / (float) sr);
        loopLpCoef = 1.0f - std::exp (-2.0f * fxkit::kPi * juce::jmin (dampHz * 0.85f, 0.45f * (float) sr) / (float) sr);
        loopHpCoef = 1.0f - std::exp (-2.0f * fxkit::kPi * juce::jmax (60.0f, param (pLowCut) * 1.4f) / (float) sr);
        modDepth = param (pMod) * 0.01f * (float) (0.0012 * sr);
        lfoInc = 0.31 / sr;
        width = param (pWidth) * 0.01f;
        lowCoef = 1.0f - std::exp (-2.0f * fxkit::kPi * param (pLowCut) / (float) sr);
        wet = param (pDryWet) * 0.01f;
        // the shifter: the ratio, and how fast the heads sweep their grains
        const double semis = std::lround (param (pShift)) + param (pCents) * 0.01;
        const double ratio = std::pow (2.0, semis / 12.0);
        grain = (float) (juce::jlimit (30.0f, 200.0f, param (pSmooth)) * 0.001 * sr);
        headInc = (float) ((1.0 - ratio) / (double) grain);
        shimmerGain = param (pShimmer) * 0.01f * 1.25f;
    }

    fxkit::DelayLine preDelay;
    fxkit::Allpass diff[2][kDiffusers];
    fxkit::DelayLine lines[kLines];
    fxkit::DelayLine shiftBuf[2];
    float lineLen[kLines] {}, lineGain[kLines] {}, damp[kLines] {};
    float diffLen[2][kDiffusers] {};
    float diffGain { 0.6f }, preSamples { 0.0f }, dampCoef { 0.5f }, modDepth { 0.0f };
    float width { 1.0f }, lowCoef { 0.0f }, wet { 0.35f };
    float lowState[2] {};
    float headPhase[kHeads] {}, headInc { 0.0f }, grain { 4800.0f }, shimmerGain { 0.3f };
    float loopLp[2] {}, loopHp[2] {}, loopLpCoef { 0.5f }, loopHpCoef { 0.01f };
    float feedL { 0.0f }, feedR { 0.0f }, guard { 0.0f };
    static constexpr int kGuardHop = 4096;
    float inEnv { 0.0f }, tailEnv { 0.0f }, lastTail { 0.0f }, regen { 1.0f };
    int guardCount { 0 };
    double lfoPhase { 0.0 }, lfoInc { 0.0 };
    int countdown { 0 };
};

AQUANODE_REGISTER_MODULE (ShimmerReverbModule, ShimmerReverbModule::descriptor)
