#pragma once

#include "../../Core/ModuleCore.h"

// Band Splitter: one signal into 2 - 7 bands (6.9.4: Bands), Linkwitz-Riley 4th-order crossovers
// in a tree - the lower bands each through the allpasses of the crossovers above them, so the bands
// add back to the input's level at every frequency (an allpass in phase).
class BandSplitterModule : public aquanode::SynthModule
{
public:
    static constexpr int kMaxBands = 7, kMaxSplits = kMaxBands - 1;
    enum ParamIndex { pSplit1 = 0, pSplit2, pBands, pSplit3, pSplit4, pSplit5, pSplit6 };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (auto& v : last) v = -1.0;
        reset();
    }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& s = state[v][c];
            for (auto& x : s.lp) for (auto& b : x) b.reset();
            for (auto& x : s.hp) for (auto& b : x) b.reset();
            for (auto& b : s.ap) b.reset();
        }
    }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);   // global lane = voice slot 0
    }

private:
    struct Coef { float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };
    // one biquad's memory (transposed direct form II)
    struct Mem
    {
        float z1 { 0 }, z2 { 0 };
        void reset() { z1 = z2 = 0.0f; }
        float process (const Coef& c, float x)
        {
            const float y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
    // per voice and channel: each crossover's two low-pass and two high-pass stages (LR4 legs),
    // and the allpasses the lower bands go through (band k: those of the crossovers above it)
    struct State
    {
        Mem lp[kMaxSplits][2], hp[kMaxSplits][2];
        Mem ap[kMaxSplits * (kMaxSplits - 1) / 2];
    };

    void updateCoefficients (int bands);
    static Coef butterworth (double freq, double sr, int kind);   // 0 low-pass, 1 high-pass, 2 allpass

    State state[aquanode::kMaxVoices][2];
    Coef lpC[kMaxSplits], hpC[kMaxSplits], apC[kMaxSplits];
    double last[kMaxSplits + 1] {};
    double splits[kMaxSplits] {};
};
