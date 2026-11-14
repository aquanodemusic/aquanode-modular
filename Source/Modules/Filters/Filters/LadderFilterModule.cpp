#include "LadderFilterModule.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

// four one-poles, the fourth fed back a sample late; Drive only saturates
// (its tanh is left out: the shape at ordinary levels)
bool LadderFilterModule::magnitudeResponseDb (const float* hz, float* db, int n) const
{
    const double cutoff = juce::jlimit (20.0, 20000.0, (double) getParameterBase (pCutoff));
    const double g = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (cutoff, sampleRate * 0.45) / sampleRate);
    const double k = getParameterBase (pResonance) * 4.4;
    const double outVol = getParameterBase (pOutputVolume);
    const int mode = (int) getParameterBase (pMode);
    for (int i = 0; i < n; ++i)
    {
        const auto z1 = response::zInv (hz[i], sampleRate);
        const auto h1 = response::onePole (g, z1);
        const auto h2 = h1 * h1, h4 = h2 * h2;
        const auto x = 1.0 / (1.0 + k * z1 * h4);
        const auto y = mode == 0 ? h2 * x : mode == 1 ? h4 * x : mode == 2 ? (1.0 - h2) * x : (1.0 - h4) * x;
        db[i] = response::db (y * outVol);
    }
    return true;
}

void LadderFilterModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float modIn = inputs[1][0];
    const float depth = param (pModDepth) * 0.01f;

    const float cutoff = juce::jlimit (20.0f, 20000.0f,
        param (pCutoff) * std::pow (2.0f, depth * modIn * 5.0f));

    const float g = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                    * juce::jmin (cutoff, (float) (sampleRate * 0.45)) / (float) sampleRate);
    const float k = param (pResonance) * 4.4f;   // > ~4 self-oscillates (res near/above ~0.9)
    const float drive = param (pDrive);
    const float outVol = param (pOutputVolume);
    const int mode = (int) param (pMode);        // 0=12LP 1=24LP 2=12HP 3=24HP

    for (int c = 0; c < 2; ++c)
    {
        const float in = inputs[0][(size_t) c];
        const float x = std::tanh (drive * in - k * s4[v][c]);

        s1[v][c] += g * (x        - s1[v][c]);
        s2[v][c] += g * (s1[v][c] - s2[v][c]);
        s3[v][c] += g * (s2[v][c] - s3[v][c]);
        s4[v][c] += g * (s3[v][c] - s4[v][c]);

        float y = 0.0f;
        switch (mode)
        {
            case 0: y = s2[v][c];      break;   // 12dB LP
            case 1: y = s4[v][c];      break;   // 24dB LP
            case 2: y = x - s2[v][c];  break;   // 12dB HP
            case 3: y = x - s4[v][c];  break;   // 24dB HP
        }
        outputs[0][(size_t) c] = y * outVol;
    }
}

static ModuleDescriptor ladderDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.ladder";
    d.displayName = "Ladder Filter";
    d.description =
        "A 4-pole ladder with drive and self-oscillating resonance, in 12 or 24 dB "
        "lowpass and highpass. Mod In sweeps the cutoff - an ADSR, LFO or KeyTrack. Flexible "
        "lane: one ladder per note when fed per-voice, otherwise one globally.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 1;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("cutoffMod", "Cutoff"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.2f, 0),
        makeRotary ("drive",     "Drive",     1.0f, 5.0f, 1.0f, 0),
        makeRotary ("modDepth",  "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("outputVolume", "Out Vol", 0.0f, 1.2f, 1.0f, 0),
        makeCombo  ("mode", "Filter Mode", { "12dB LP", "24dB LP", "12dB HP", "24dB HP" }, 1, 1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LadderFilterModule, ladderDescriptor)
