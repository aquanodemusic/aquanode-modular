#include "AquaFilterModule.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

// the ladder modes: one-poles tuned by Huovilainen's coefficients, the last
// (LP12: the second) fed back a sample late; the SVF modes: a TPT stage.
// Drive saturates before it all and is left out (the shape at ordinary
// levels); LP24+'s output clip is unity there too
bool AquaFilterModule::magnitudeResponseDb (const float* hz, float* db, int n) const
{
    const double sr = sampleRate;
    const double cutoff = juce::jlimit (20.0, juce::jmin (20000.0, sr * 0.45), (double) getParameterBase (pCutoff));
    const int type = (int) getParameterBase (pType);
    const double r = shapeResonance (getParameterBase (pResonance), (int) getParameterBase (pResCurve));

    if (type <= 2)
    {
        const double fc = cutoff / sr, fc2 = fc * fc, fc3 = fc2 * fc;
        const double fcr = 1.8730 * fc3 + 0.4955 * fc2 - 0.6490 * fc + 0.9988;
        const double acr = -3.9364 * fc2 + 1.8409 * fc + 0.9968;
        const double tune = juce::jlimit (0.0, 1.0, 1.0 - std::exp (-juce::MathConstants<double>::twoPi * fcr * fc));
        const double res4 = 4.2 * r * acr;
        for (int i = 0; i < n; ++i)
        {
            const auto z1 = response::zInv (hz[i], sr);
            const auto h1 = response::onePole (tune, z1);
            const auto h2 = h1 * h1;
            db[i] = response::db (type == 0 ? h2 / (1.0 + 0.5 * res4 * z1 * h2)
                                            : (h2 * h2) / (1.0 + res4 * z1 * h2 * h2));
        }
        return true;
    }
    const double k = 2.0 * (1.0 - r * 0.985);
    for (int i = 0; i < n; ++i)
    {
        const auto s = response::svf (response::sOf (hz[i], cutoff, sr), k);
        db[i] = response::db (type == 3 ? s.band : s.high);
    }
    return true;
}

float AquaFilterModule::ladderProcess (LadderState& s, float x, float tune, float res4) const
{
    x -= res4 * s.y[3];
    x = tanhA (x);
    s.y[0] += tune * (x            - tanhA (s.y[0]));
    s.y[1] += tune * (tanhA (s.y[0]) - tanhA (s.y[1]));
    s.y[2] += tune * (tanhA (s.y[1]) - tanhA (s.y[2]));
    s.y[3] += tune * (tanhA (s.y[2]) - tanhA (s.y[3]));
    return s.y[3];
}

void AquaFilterModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float modIn = inputs[1][0];
    const float depth = param (pModDepth) * 0.01f;

    const float cutoff = juce::jlimit (20.0f, juce::jmin (20000.0f, (float) (sampleRate * 0.45)),
        param (pCutoff) * std::pow (2.0f, depth * modIn * 5.0f));

    const int   type  = (int) param (pType);       // 0=LP12 1=LP24 2=LP24+ 3=BP 4=HP
    const int   curve = (int) param (pResCurve);   // 0=Quadratic 1=Cubic
    const float r     = shapeResonance (param (pResonance), curve);
    const float drive = param (pDrive) * (param (pDriveRange) > 0.5f ? 4.0f : 1.0f);

    // pre-filter saturation with gain compensation, exactly as in the plugin up to 1;
    // (6.9.19) above 1 the gain keeps rising (x2.8 per unit, up to about +44 dB at 4),
    // the saturator held to its bounded range so it stays a clean clip into fuzz
    const bool hot = drive > 1.0f;
    const float driveGain = hot ? 7.0f * std::exp2 (1.5f * (drive - 1.0f)) : 1.0f + drive * 6.0f;
    const float driveComp = hot ? 0.625f / (1.0f + 0.15f * (drive - 1.0f)) : 1.0f / (1.0f + drive * 0.6f);
    auto driveIn = [hot, driveGain, driveComp] (float x)
    {
        x *= driveGain;
        return tanhA (hot ? juce::jlimit (-3.0f, 3.0f, x) : x) * driveComp;
    };

    const float sr = (float) sampleRate;

    if (type <= 2)   // ladder modes: Huovilainen coefficients
    {
        const float fc  = cutoff / sr;
        const float fc2 = fc * fc;
        const float fc3 = fc2 * fc;
        const float fcr = 1.8730f * fc3 + 0.4955f * fc2 - 0.6490f * fc + 0.9988f;
        const float acr = -3.9364f * fc2 + 1.8409f * fc + 0.9968f;
        const float tune = juce::jlimit (0.0f, 1.0f,
            1.0f - std::exp (-juce::MathConstants<float>::twoPi * fcr * fc));
        const float res4 = 4.2f * r * acr;

        for (int c = 0; c < 2; ++c)
        {
            const float in = driveIn (inputs[0][(size_t) c]);
            auto& s = ladder[v][c];
            float y;

            if (type == 0)   // LP12: two ladder stages, half feedback
            {
                float x = in - res4 * 0.5f * s.y[1];
                x = tanhA (x);
                s.y[0] += tune * (x            - tanhA (s.y[0]));
                s.y[1] += tune * (tanhA (s.y[0]) - tanhA (s.y[1]));
                y = s.y[1];
            }
            else if (type == 1)   // LP24
            {
                y = ladderProcess (s, in, tune, res4);
            }
            else   // LP24+: extra saturation on the ladder output
            {
                y = tanhA (ladderProcess (s, in, tune, res4) * 1.35f) / 1.35f;
            }

            outputs[0][(size_t) c] = y;
        }
    }
    else   // SVF modes (Simper): BP / HP
    {
        const float g  = std::tan (juce::MathConstants<float>::pi * cutoff / sr);
        const float k  = 2.0f * (1.0f - r * 0.985f);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        for (int c = 0; c < 2; ++c)
        {
            const float in = driveIn (inputs[0][(size_t) c]);
            auto& s = svf[v][c];

            const float v3 = in - s.ic2;
            const float v1 = a1 * s.ic1 + a2 * v3;
            const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;

            outputs[0][(size_t) c] = (type == 3) ? v1                    // BP
                                                 : in - k * v1 - v2;     // HP
        }
    }
}

//==============================================================================
static ModuleDescriptor aquaFilterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.aquafilter";
    d.displayName = "Multimode Drive Filter";
    d.description =
        "The filter core of the AquaFilter plugin: a tanh-stage ladder in 12/24 dB lowpass "
        "plus an extra-saturated 24+ mode, and an SVF for bandpass and highpass, with Drive "
        "saturating before the filter. Mod In sweeps the cutoff - an ADSR, LFO or Env Follow. "
        "Res Curve reshapes the resonance knob rather than the sound: Quadratic bites earlier, "
        "Cubic keeps the lower half tamer. Drive Range Hot gives Drive four times the push (into fuzz), "
        "Classic the gentler range of earlier versions.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 7;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("cutoffMod", "Cutoff"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 2000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.3f, 0),
        makeRotary ("drive",     "Drive",     0.0f, 1.0f, 0.0f, 0),
        makeRotary ("modDepth",  "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%"),
        makeCombo  ("type",     "Filter Type", { "LP12", "LP24", "LP24+", "BP", "HP" }, 1, 1, 2),
        makeCombo  ("resCurve", "Res Curve",   { "Quadratic", "Cubic" }, 1, 1, 2),
        // (6.9.19) Hot: Drive's full turn is four times the Classic one (patches from before: Classic)
        makeCombo  ("driveRange", "Drive Range", { "Classic", "Hot" }, 1, 1, 2).noMod().whenMissing (0.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (AquaFilterModule, aquaFilterDescriptor)
