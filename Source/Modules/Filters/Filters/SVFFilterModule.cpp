#include "SVFFilterModule.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

bool SVFFilterModule::magnitudeResponseDb (const float* hz, float* db, int n) const
{
    const double fc = juce::jmin (juce::jlimit (20.0, 20000.0, (double) getParameterBase (pCutoff)), sampleRate * 0.49);
    const double k = 1.0 - getParameterBase (pResonance) * 0.98;
    const double m = getParameterBase (pMorph) * 0.02;
    for (int i = 0; i < n; ++i)
    {
        const auto r = response::svf (response::sOf (hz[i], fc, sampleRate), k);
        db[i] = response::db (morphed (m, r.low, r.band, r.high));
    }
    return true;
}

void SVFFilterModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float modIn = inputs[1][0];
    const float depth = param (pModDepth) * 0.01f;   // -1..+1

    // exponential (octave-based) cutoff modulation: +-5 octaves at full depth+signal
    const float cutoff = juce::jlimit (20.0f, 20000.0f,
        param (pCutoff) * powMemo[v] (depth * modIn * 5.0f, [] (float x) { return std::pow (2.0f, x); }));

    // TPT ("topology-preserving") state-variable filter: unconditionally
    // stable at every cutoff and resonance. The previous Chamberlin form went
    // unstable above roughly a quarter of the sample rate (~12 kHz at 48 kHz),
    // so a fully opened or upward-swept filter turned into a full-scale
    // oscillation. Same response shape everywhere below that.
    const float fc = juce::jmin (cutoff, (float) (sampleRate * 0.49));
    const float g = tanMemo[v] (juce::MathConstants<float>::pi * fc / (float) sampleRate, [] (float x) { return std::tan (x); });
    const float k = 1.0f - param (pResonance) * 0.98f;     // damping (1/Q), as before
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const double m = param (pMorph) * 0.02;   // (6.9.19) 0 LP, 1 BP, 2 HP, blended between

    for (int c = 0; c < 2; ++c)
    {
        const float in = inputs[0][(size_t) c];
        float& ic1 = band[v][c];   // integrator states
        float& ic2 = low[v][c];

        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;      // band
        const float v2 = ic2 + a2 * ic1 + a3 * v3; // low
        ic1 = juce::jlimit (-4.0f, 4.0f, 2.0f * v1 - ic1);   // same gentle ceiling as before
        ic2 = juce::jlimit (-4.0f, 4.0f, 2.0f * v2 - ic2);
        const float high = in - k * v1 - v2;

        outputs[0][(size_t) c] = (float) morphed<double> (m, v2, v1, high);
    }
}

static ModuleDescriptor svfDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.svf";
    d.displayName = "SVF Filter";
    d.description =
        "A state-variable filter whose Type knob glides from lowpass through bandpass to highpass. Mod In "
        "sweeps the cutoff - an ADSR, LFO or KeyTrack (at 100% Mod Depth KeyTrack gives exact "
        "one-octave-per-octave tracking). Flexible lane: fed per-voice it runs one filter per "
        "note, in a global chain it runs once.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 0;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("cutoffMod", "Cutoff"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.2f, 0),
        // (6.9.19) Filter Mode is the Type knob now (a patch from before: its mode, ProcessorState)
        makeCombo  ("mode",      "Filter Mode", { "Lowpass", "Bandpass", "Highpass" }, 0, 0, 2).hide(),
        makeRotary ("modDepth",  "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%"),
        [] { auto t = makeRotary ("morph", "Type", 0.0f, 100.0f, 0.0f, 0, "%");
             t.textFromValue = [] (double v)
             {
                 if (v < 1.0) return juce::String ("Lowpass");
                 if (std::abs (v - 50.0) < 1.0) return juce::String ("Bandpass");
                 if (v > 99.0) return juce::String ("Highpass");
                 return v < 50.0 ? "LP " + juce::String (juce::roundToInt (100.0 - v * 2.0)) + " / BP " + juce::String (juce::roundToInt (v * 2.0))
                                 : "BP " + juce::String (juce::roundToInt (200.0 - v * 2.0)) + " / HP " + juce::String (juce::roundToInt (v * 2.0 - 100.0));
             };
             return t; }()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SVFFilterModule, svfDescriptor)
