#include "CenterCombModule.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

// the peaking bands: Count pairs either side of Freq, Spread apart, the
// outer ones thinned by Damp (an inactive band is skipped)
int CenterCombModule::computeFilters (float freq, float gainDb, int count, float spread, float q, float damp,
                                      double sr, Coeffs* out, bool multiplicative)
{
    const float lowLimit = 20.0f;
    const float highLimit = (float) (sr * 0.475);
    const int active = count * 2 + 1;

    for (int i = 0; i < maxFilters; ++i)
    {
        auto& cf = out[i];
        cf.active = false;

        if (i >= active)
            continue;

        const int offsetIdx = i - count;
        // Linear: Spread Hz apart; Multiplicative (6.9.19): each one Ratio times its neighbour
        const float f = multiplicative ? (float) ((double) freq * std::pow ((double) spread, (double) offsetIdx)) + (float) i * 0.0001f
                                       : freq + (float) offsetIdx * spread;
        if (f < lowLimit || f > highLimit)
            continue;

        float weight = 1.0f - damp * (std::abs ((float) offsetIdx) / (float) (count + 1));
        weight = juce::jmax (0.0f, weight);
        if (weight <= 0.001f)
            continue;

        const float A = std::pow (10.0f, gainDb * weight / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float alpha = std::sin (w0) / (2.0f * juce::jmax (0.1f, q));
        const float cosw = std::cos (w0);

        const float a0 = 1.0f + alpha / A;
        cf.b0 = (1.0f + alpha * A) / a0;
        cf.b1 = (-2.0f * cosw) / a0;
        cf.b2 = (1.0f - alpha * A) / a0;
        cf.a1 = (-2.0f * cosw) / a0;
        cf.a2 = (1.0f - alpha / A) / a0;
        cf.active = true;
    }
    return active;
}

void CenterCombModule::blockStart()
{
    const float freq = param (pFreq);
    const float gainDb = param (pGain);
    const int count = juce::jlimit (0, maxPairs, (int) param (pCount));
    const int mode = param (pSpreadMode) > 0.5f ? 1 : 0;
    const float spread = mode == 1 ? param (pRatio) : param (pSpread);
    const float q = param (pQ);
    const float damp = param (pDamp) * 0.01f;

    if (freq == lastFreq && gainDb == lastGain && count == lastCount
        && spread == lastSpread && q == lastQ && damp == lastDamp && mode == lastMode)
        return;

    lastFreq = freq; lastGain = gainDb; lastCount = count;
    lastSpread = spread; lastQ = q; lastDamp = damp; lastMode = mode;

    activeFilters = computeFilters (freq, gainDb, count, spread, q, damp, sampleRate, coeffs, mode == 1);
}

bool CenterCombModule::magnitudeResponseDb (const float* hz, float* db, int n) const
{
    Coeffs c[maxFilters] {};
    const bool multi = getParameterBase (pSpreadMode) > 0.5f;
    const int active = computeFilters (getParameterBase (pFreq), getParameterBase (pGain),
                                       juce::jlimit (0, maxPairs, (int) getParameterBase (pCount)),
                                       multi ? getParameterBase (pRatio) : getParameterBase (pSpread), getParameterBase (pQ),
                                       getParameterBase (pDamp) * 0.01f, sampleRate, c, multi);
    for (int i = 0; i < n; ++i)
    {
        const auto z1 = response::zInv (hz[i], sampleRate);
        response::C h = 1.0;
        for (int f = 0; f < active; ++f)
            if (c[f].active)
                h *= response::biquad (c[f].b0, c[f].b1, c[f].b2, c[f].a1, c[f].a2, z1);
        db[i] = response::db (h);
    }
    return true;
}

void CenterCombModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    for (int c = 0; c < 2; ++c)
    {
        float x = inputs[0][(size_t) c];

        // filters in series, like the original
        for (int i = 0; i < activeFilters; ++i)
        {
            const auto& cf = coeffs[i];
            if (! cf.active)
                continue;

            const float y = cf.b0 * x + cf.b1 * x1[c][i] + cf.b2 * x2[c][i]
                            - cf.a1 * y1[c][i] - cf.a2 * y2[c][i];
            x2[c][i] = x1[c][i];  x1[c][i] = x;
            y2[c][i] = y1[c][i];  y1[c][i] = juce::jlimit (-8.0f, 8.0f, y);
            x = y1[c][i];
        }

        outputs[0][(size_t) c] = x;
    }
    if (param (pLimiter) > 0.5f)   // (6.9.19)
        limiter.process (outputs[0][0], outputs[0][1]);
}

static ModuleDescriptor centerCombDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.centercomb";
    d.displayName = "Center Comb";
    d.description =
        "A bank of peaking filters spread symmetrically around a centre frequency, with Damp "
        "thinning the outer peaks. It has no delay line, so it colours rather "
        "than rings - for the ringing, pitched kind reach for Comb Filter instead. Spread Mode: Linear sets the "
        "peaks Spread Hz apart (the graph then has a linear frequency scale, so they stand evenly), Multiplicative "
        "each Ratio times its neighbour (a log scale). The Limiter keeps the output under full scale.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 2;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("freq",   "Center",  20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("gain",   "Gain",    -24.0f, 24.0f, 6.0f, 0, "dB"),
        makeRotary ("count",  "Count",   0.0f, 16.0f, 2.0f, 0, {}, false, 1.0f),
        makeRotary ("spread", "Spread",  10.0f, 2000.0f, 100.0f, 0, "Hz", true).visibleWhen ("spreadMode", 0.0f),
        makeRotary ("q",      "Q",       0.5f, 100.0f, 10.0f, 0, {}, true),
        makeRotary ("damp",   "Damp",    0.0f, 100.0f, 50.0f, 1, "%"),
        // (6.9.19) on for new cards; patches from before keep it off (as they sounded)
        makeCombo ("limiter", "Limiter", { "Off", "On" }, 1, 1, 2).noMod().whenMissing (0.0f),
        makeCombo ("spreadMode", "Spread Mode", { "Linear", "Multiplicative" }, 0, 1, 2).noMod(),
        makeRotary ("ratio", "Ratio", 1.01f, 10.0f, 1.05f, 0, "x", true).visibleWhen ("spreadMode", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CenterCombModule, centerCombDescriptor)
