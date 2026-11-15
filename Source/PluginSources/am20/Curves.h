#pragma once
// AM-20: the curves measured from the original (see MEASUREMENTS.md). Knob
// positions are 0..1; tables are sampled at even steps and interpolated in the
// log domain where the quantity is exponential.
#include <cmath>
#include <algorithm>

namespace am20::curves
{
inline float lerpTable (const float* t, int n, float x)          // x 0..1 over n points
{
    x = std::clamp (x, 0.0f, 1.0f) * (float) (n - 1);
    const int i = std::min ((int) x, n - 2);
    const float f = x - (float) i;
    return t[i] + (t[i + 1] - t[i]) * f;
}
inline float logTable (const float* t, int n, float x)
{
    x = std::clamp (x, 0.0f, 1.0f) * (float) (n - 1);
    const int i = std::min ((int) x, n - 2);
    const float f = x - (float) i;
    return t[i] * std::pow (t[i + 1] / t[i], f);
}

// ---- filters: cutoff in Hz against the knob, from the self-oscillation pitch
// at every 0.5 of the knob (no key tracking; HPF and LPF are the same)
inline float cutoffHz (float knob)
{
    static const float t[21] = { 12.76f, 18.47f, 26.74f, 38.69f, 56.01f, 81.04f, 117.33f, 169.79f, 245.78f, 355.81f,
                                 514.87f, 745.72f, 1079.8f, 1564.1f, 2267.4f, 3294.9f, 4800.0f, 6960.8f, 10074.0f, 14575.0f, 21094.0f };
    return logTable (t, 21, knob);
}
// knob position for a cutoff in Hz (modulation is added in octaves, so this is
// only needed for display)
inline float octavesPerKnob() { return 10.67f; }

// ---- envelope time constants (seconds of the RC curve), every 0.1 of the knob
inline float eg2AttackTau (float knob)
{
    static const float t[11] = { 0.0094f, 0.0187f, 0.0629f, 0.1550f, 0.3204f, 0.6056f, 1.0935f, 1.9267f, 3.381f, 5.8985f, 10.224f };
    if (knob < 0.1f) return 0.0094f + (0.0187f - 0.0094f) * knob / 0.1f;
    return logTable (t, 11, knob);
}
// EG2's decay and release share one curve
inline float eg2Tau (float knob)
{
    static const float t[11] = { 0.0080f, 0.0170f, 0.0508f, 0.1205f, 0.2457f, 0.4619f, 0.8317f, 1.4662f, 2.5647f, 4.4824f, 7.768f };
    if (knob < 0.1f) return 0.0080f + (0.0170f - 0.0080f) * knob / 0.1f;
    return logTable (t, 11, knob);
}
// EG1's attack and release (plain RC curves, a little shorter than EG2's)
inline float eg1Tau (float knob)
{
    static const float t[11] = { 0.0020f, 0.0200f, 0.0500f, 0.1240f, 0.2250f, 0.4070f, 0.7200f, 1.260f, 2.200f, 3.860f, 6.700f };
    return logTable (t, 11, knob);
}
// EG1 delay in seconds
inline float eg1Delay (float knob)
{
    static const float t[11] = { 0.0f, 0.039f, 0.080f, 0.161f, 0.310f, 0.581f, 1.02f, 1.80f, 3.10f, 5.44f, 9.50f };
    if (knob <= 0.0f) return 0.0f;
    if (knob < 0.1f) return 0.039f * knob / 0.1f;
    return logTable (t, 11, knob);
}
// EG2 hold: the gate is held open at least this long after the note starts
inline float eg2Hold (float knob)
{
    static const float t[11] = { 0.0f, 0.034f, 0.116f, 0.282f, 0.578f, 1.088f, 1.962f, 3.456f, 6.054f, 10.50f, 18.31f };
    if (knob <= 0.0f) return 0.0f;
    if (knob < 0.1f) return 0.034f * knob / 0.1f;
    return logTable (t, 11, knob);
}

// ---- modulation intensity knobs: semitones of pitch per unit of source
// (unit = the full swing of an envelope; the MG swings +-0.5 units)
inline float pitchDepth (float knob)
{
    static const float t[11] = { 0.0f, 2.48f, 5.2f, 8.35f, 12.06f, 16.6f, 22.3f, 29.5f, 39.0f, 50.4f, 64.6f };
    return lerpTable (t, 11, knob);
}
// the same knobs on the filters move the cutoff 1.37 times as many octaves
inline float filterDepthOct (float knob) { return pitchDepth (knob) / 12.0f * 1.37f; }

// ---- MG: frequency is linear in the knob, 0.1 Hz .. 19 Hz
inline float mgHz (float knob) { return 0.1f + 18.9f * std::clamp (knob, 0.0f, 1.0f); }

// ---- portamento: RC glide time constant
inline float portaTau (float knob)
{
    static const float t[11] = { 0.0f, 0.0039f, 0.0057f, 0.0084f, 0.0135f, 0.0216f, 0.036f, 0.0605f, 0.106f, 0.186f, 0.33f };
    if (knob <= 0.0f) return 0.0f;
    return lerpTable (t, 11, knob);
}

// ---- VCO mixer level knobs (audio taper): gain for the knob
inline float mixLevel (float knob)
{
    static const float t[21] = { 0.0f, 0.0176f, 0.0359f, 0.0553f, 0.0757f, 0.0979f, 0.1219f, 0.1470f, 0.1761f, 0.2069f, 0.2422f,
                                 0.2813f, 0.3266f, 0.3753f, 0.4345f, 0.4977f, 0.5719f, 0.6583f, 0.7566f, 0.8668f, 1.0f };
    return lerpTable (t, 21, knob);
}

// ---- VCA: the envelope opens the VCA from 0.1135 of its range on (below that
// it is shut), measured on attack, decay and release
constexpr float vcaThreshold = 0.1135f;
// EG2 internal levels: attack charges toward 1.6 and stops at 1.1 (the VCA is
// fully open from 1.0), decay and release head 0.035 below their target
constexpr float eg2AttackTarget = 1.6f;
constexpr float eg2Peak = 1.1f;
constexpr float eg2Undershoot = 0.035f;
inline float eg2SustainLevel (float knob)
{
    static const float t[21] = { 0.0f, 0.06f, 0.126f, 0.177f, 0.234f, 0.291f, 0.348f, 0.405f, 0.461f, 0.516f, 0.570f,
                                 0.624f, 0.677f, 0.729f, 0.780f, 0.830f, 0.880f, 0.928f, 0.976f, 1.04f, 1.10f };
    return lerpTable (t, 21, knob);
}

// ---- output: a full-level VCO saw (+-0.5 inside) leaves at 0.2136 peak
constexpr float outputGain = 0.4395f;
}
