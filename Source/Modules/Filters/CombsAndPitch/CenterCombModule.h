#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/SafetyLimiter.h"

// Center Comb - a bank of peaking filters spread symmetrically around a
// centre frequency (adapted from the CenterComb VST, Regular frequency mode).
// Count sets the number of peak pairs either side of the centre; Damp
// reduces the gain of peaks further from the centre.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class CenterCombModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pFreq = 0, pGain, pCount, pSpread, pQ, pDamp,
                      pLimiter, pSpreadMode, pRatio };   // (6.9.19)
    static constexpr int maxPairs = 16;
    static constexpr int maxFilters = maxPairs * 2 + 1;

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        lastFreq = -1.0f;   // force coefficient rebuild
        limiter.prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < maxFilters; ++i)
                x1[c][i] = x2[c][i] = y1[c][i] = y2[c][i] = 0.0f;
        limiter.reset();
    }

    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    bool magnitudeResponseDb (const float* hz, float* db, int n) const override;   // the static curve

private:
    // RBJ peaking-EQ biquads, coefficients rebuilt in blockStart when knobs move
    struct Coeffs { float b0, b1, b2, a1, a2; bool active; };
    // how many bands are in play; (6.9.19) multiplicative: Spread is the ratio between neighbours
    static int computeFilters (float freq, float gainDb, int count, float spread, float q, float damp,
                               double sr, Coeffs* out, bool multiplicative = false);
    Coeffs coeffs[maxFilters] {};
    int activeFilters { 0 };

    float x1[2][maxFilters] {}, x2[2][maxFilters] {};
    float y1[2][maxFilters] {}, y2[2][maxFilters] {};

    float lastFreq { -1.0f }, lastGain { 0.0f }, lastSpread { 0.0f },
          lastQ { 0.0f }, lastDamp { 0.0f };
    int lastCount { -1 }, lastMode { -1 };
    aquanode::SafetyLimiter limiter;
};
