#include "BandSplitterModule.h"

using namespace aquanode;

// RBJ cookbook, Q = 1/sqrt(2) (Butterworth). Two in series make an LR4 leg (24 dB/oct); at the
// crossover each leg sits at -6 dB and low + high add up to the allpass of the same poles - which
// is why every lower band goes through the allpasses of the crossovers above it: then all the bands
// together are one allpass, flat at every frequency. No latency, no pre-ringing.
BandSplitterModule::Coef BandSplitterModule::butterworth (double freq, double sr, int kind)
{
    const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70710678118654752);
    const double a0 = 1.0 + alpha;
    double b0, b1, b2;
    if (kind == 0)      { b0 = (1.0 - c) * 0.5; b1 = 1.0 - c; b2 = b0; }
    else if (kind == 1) { b0 = (1.0 + c) * 0.5; b1 = -(1.0 + c); b2 = b0; }
    else                { b0 = 1.0 - alpha; b1 = -2.0 * c; b2 = 1.0 + alpha; }
    Coef k;
    k.b0 = (float) (b0 / a0); k.b1 = (float) (b1 / a0); k.b2 = (float) (b2 / a0);
    k.a1 = (float) (-2.0 * c / a0); k.a2 = (float) ((1.0 - alpha) / a0);
    return k;
}

void BandSplitterModule::updateCoefficients (int bands)
{
    const double guard = sampleRate * 0.49;   // clear of the pole at Nyquist
    static const int ids[kMaxSplits] = { pSplit1, pSplit2, pSplit3, pSplit4, pSplit5, pSplit6 };
    bool changed = last[kMaxSplits] != (double) bands;
    double prev = 0.0;
    for (int k = 0; k < bands - 1; ++k)
    {
        // each split kept a little above the one under it, so no band collapses
        double f = juce::jlimit (20.0, guard, (double) param (ids[k]));
        if (k > 0) f = juce::jlimit (juce::jmin (guard, prev * 1.05), guard, f);
        prev = f;
        if (f != last[k]) { last[k] = f; changed = true; }
    }
    if (! changed) return;
    last[kMaxSplits] = (double) bands;
    for (int k = 0; k < bands - 1; ++k)
    {
        splits[k] = last[k];
        lpC[k] = butterworth (splits[k], sampleRate, 0);
        hpC[k] = butterworth (splits[k], sampleRate, 1);
        apC[k] = butterworth (splits[k], sampleRate, 2);
    }
}

void BandSplitterModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const int bands = juce::jlimit (2, kMaxBands, (int) std::lround (param (pBands)));
    updateCoefficients (bands);
    const int nSplits = bands - 1;

    for (int c = 0; c < 2; ++c)
    {
        auto& s = state[v][c];
        float band[kMaxBands] {};
        float rest = inputs[0][(size_t) c];
        for (int k = 0; k < nSplits; ++k)
        {
            band[k] = s.lp[k][1].process (lpC[k], s.lp[k][0].process (lpC[k], rest));
            rest = s.hp[k][1].process (hpC[k], s.hp[k][0].process (hpC[k], rest));
        }
        band[nSplits] = rest;
        // the lower bands through the allpasses of the crossovers above them
        int a = 0;
        for (int k = 0; k + 1 < nSplits; ++k)
            for (int j = k + 1; j < nSplits; ++j)
                band[k] = s.ap[a++].process (apC[j], band[k]);
        for (int k = 0; k < kMaxBands; ++k)
            outputs[k][(size_t) c] = band[k];
    }
}

static ModuleDescriptor bandSplitterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "effect.bandsplitter";
    d.displayName = "Band Splitter";
    d.description =
        "Splits one signal into 2 to 7 frequency bands (Bands) with Linkwitz-Riley 4th-order crossovers - the "
        "clean, phase-coherent splitting that multiband tools use - in a tree where each lower band also goes "
        "through the phase turn of the crossovers above it, so all the bands summed back give the input's level "
        "at every frequency. Each split is kept above the one under it. Band 1 is the lowest; the highest band "
        "is the one numbered Bands, the outputs above it stay silent.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 26;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("band1Out", "Band 1 (Low)"),
        audioOut ("band2Out", "Band 2"),
        audioOut ("band3Out", "Band 3"),
        audioOut ("band4Out", "Band 4"),
        audioOut ("band5Out", "Band 5"),
        audioOut ("band6Out", "Band 6"),
        audioOut ("band7Out", "Band 7")
    };
    d.params = {
        makeRotary ("split1", "Split 1", 20.0f,   2000.0f,  200.0f,  1, "Hz", true),
        makeRotary ("split2", "Split 2", 200.0f,  20000.0f, 2000.0f, 1, "Hz", true).visibleFrom ("bands", 3.0f),
        makeRotary ("bands",  "Bands",   2.0f,    7.0f,     3.0f,    0, {}, false, 1.0f).noMod(),
        makeRotary ("split3", "Split 3", 20.0f,   20000.0f, 5000.0f, 1, "Hz", true).visibleFrom ("bands", 4.0f),
        makeRotary ("split4", "Split 4", 20.0f,   20000.0f, 9000.0f, 2, "Hz", true).visibleFrom ("bands", 5.0f),
        makeRotary ("split5", "Split 5", 20.0f,   20000.0f, 13000.0f, 2, "Hz", true).visibleFrom ("bands", 6.0f),
        makeRotary ("split6", "Split 6", 20.0f,   20000.0f, 17000.0f, 2, "Hz", true).visibleFrom ("bands", 7.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BandSplitterModule, bandSplitterDescriptor)
