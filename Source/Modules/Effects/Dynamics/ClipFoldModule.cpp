// Clip Preserve - nothing is clipped away: whatever would go past the ceiling
// (0 dBFS, or lower with Ceiling) comes back into range instead.
//
//   Mirror: the part above the ceiling folds back down from it (and the part
//           below the floor back up) - a sine's flattened top becomes a top
//           turned upside down. However loud, it folds back and forth until
//           everything lies inside: a triangle fold of period 4 x ceiling.
//   Wrap:   the part above the ceiling comes in again from the floor, going
//           up (and below the floor, in from the top) - the tip of the wave
//           appears at the bottom. However loud, it wraps round and round:
//           a modulo of period 2 x ceiling.
//
// Both run through first-order antiderivative anti-aliasing (the fold's
// exact integral, differenced), so the new corners alias far less than a
// plain fold would. Drive pushes into it, Mix blends the dry signal back,
// Output trims the result. Global effect.
// Input: 0 = Audio In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class ClipFoldModule : public SynthModule
{
public:
    enum ParamIndex { pMode = 0, pDrive, pCeiling, pMix, pOutput };

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        smCoef = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c) { lastU[c] = 0.0; first[c] = true; }
        smDrive = juce::Decibels::decibelsToGain (param (pDrive));
        smCeil = juce::Decibels::decibelsToGain (param (pCeiling));
    }

    // the folds, of u = signal / ceiling
    static double mirror (double u)            // period 4, the identity on [-1, 1]
    {
        const double m = u - 1.0 - 4.0 * std::floor ((u - 1.0) / 4.0);
        return std::abs (m - 2.0) - 1.0;
    }
    static double mirrorInt (double u)         // its antiderivative (periodic, continuous)
    {
        const double m = u - 1.0 - 4.0 * std::floor ((u - 1.0) / 4.0);
        return m <= 2.0 ? m - 0.5 * m * m : 0.5 * m * m - 3.0 * m + 4.0;
    }
    static double wrap (double u)              // period 2, the identity on [-1, 1)
    {
        const double q = u + 1.0 - 2.0 * std::floor ((u + 1.0) / 2.0);
        return q - 1.0;
    }
    static double wrapInt (double u)
    {
        const double q = u + 1.0 - 2.0 * std::floor ((u + 1.0) / 2.0);
        return 0.5 * q * q - q;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        smDrive += smCoef * (juce::Decibels::decibelsToGain (param (pDrive)) - smDrive);
        smCeil  += smCoef * (juce::Decibels::decibelsToGain (param (pCeiling)) - smCeil);
        const bool wrapMode = std::lround (param (pMode)) == 1;
        const float mix = param (pMix) * 0.01f;
        const float outGain = juce::Decibels::decibelsToGain (param (pOutput));
        const double ceil = juce::jmax (1.0e-4, (double) smCeil);

        for (int c = 0; c < 2; ++c)
        {
            const float x = in[0][(size_t) c];
            double u = (double) x * smDrive / ceil;
            if (! std::isfinite (u)) u = 0.0;
            u = juce::jlimit (-1.0e6, 1.0e6, u);

            double y;
            if (first[c])
            {
                y = wrapMode ? wrap (u) : mirror (u);
                first[c] = false;
            }
            else
            {
                const double du = u - lastU[c];
                if (std::abs (du) > 1.0e-6)
                    y = wrapMode ? (wrapInt (u) - wrapInt (lastU[c])) / du
                                 : (mirrorInt (u) - mirrorInt (lastU[c])) / du;
                else
                {
                    const double mid = 0.5 * (u + lastU[c]);
                    y = wrapMode ? wrap (mid) : mirror (mid);
                }
            }
            lastU[c] = u;

            const float wet = (float) (y * ceil);
            out[0][(size_t) c] = (x + mix * (wet - x)) * outGain;
        }
    }

private:
    double lastU[2] {};
    bool first[2] { true, true };
    float smDrive { 1.0f }, smCeil { 1.0f }, smCoef { 0.001f };
};

static ModuleDescriptor clipFoldDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.clipfold";
    d.displayName = "Clip Preserve";
    d.description =
        "Nothing is clipped away: what would go past the ceiling (0 dBFS, or lower with Ceiling) comes back into "
        "range. Mirror folds it back down from the ceiling (a sine's top turns upside down); Wrap brings it in again "
        "from the floor, going up (the tip appears at the bottom). However loud, it folds or wraps again and again "
        "until all of it lies inside. Drive pushes into it; anti-aliased, Mix and Output after it.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 88;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("mode",    "Mode",    { "Mirror", "Wrap" }, 0, 0, 2),
        makeRotary ("drive",   "Drive",   -24.0f, 36.0f, 0.0f, 1, "dB"),
        makeRotary ("ceiling", "Ceiling", -24.0f, 0.0f, 0.0f, 1, "dB"),
        makeRotary ("mix",     "Mix",     0.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("output",  "Output",  -24.0f, 12.0f, 0.0f, 1, "dB")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ClipFoldModule, clipFoldDescriptor)
