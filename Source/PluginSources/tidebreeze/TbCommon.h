#pragma once
//==============================================================================
// Tidebreeze - common helpers
//
// Tidebreeze is a wave synth in the spirit of the classic 2000s sample
// synths: presets of one or two layers that play multisamples ("waves"),
// each through its own resonant filter and amp with four-level envelopes
// and LFOs, then insert and send effects. Its waves are not
// recordings: every one is a recipe (TbWaves) rendered into a looping sample
// when a program needs it, and new ones can be rolled from a seed.
//
// The sources are written to compile inside a namespace of their own (as
// Aquanode Modular includes its plugins), so they include no system headers
// themselves - only <JuceHeader.h>, which Aquanode's PluginPrelude.h (or a
// standalone build) brings in first.
//==============================================================================
#include <JuceHeader.h>

namespace tb
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 6.28318530717959f;

    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
    inline float midiToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
    inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

    // 0..99 times: 0 -> 1 ms, 50 -> 0.18 s, 80 -> 4 s, 99 -> 30 s
    inline float egTime (float v)
    {
        if (v <= 0.0f) return 0.0f;
        return 0.001f * std::exp (v * (10.30895f / 99.0f));   // ln (30000) = 10.309
    }

    // 0..99 cutoff -> Hz: 0 -> 30 Hz, 99 -> 20.3 kHz, exponential
    inline float cutoffHz (float v) { return 30.0f * std::exp2 (v * (9.4f / 99.0f)); }

    // 0..99 LFO frequency -> Hz: 0 -> 0.015 Hz, 50 -> 0.67 Hz, 99 -> 30 Hz
    inline float lfoHz (float v) { return 0.015f * std::exp (v * (7.6009f / 99.0f)); }   // ln (2000)

    // a small deterministic random generator (xorshift), so waves and programs
    // rolled from the same seed come out the same on every platform
    struct Rng
    {
        juce::uint64 s;
        explicit Rng (juce::uint64 seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { next(); next(); }
        juce::uint64 next()
        {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            return s;
        }
        float uni() { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }       // 0..1
        float uni (float a, float b) { return a + (b - a) * uni(); }
        float bi() { return uni() * 2.0f - 1.0f; }                                   // -1..1
        float lg (float a, float b) { return a * std::pow (b / a, uni()); }        // log-uniform
        int between (int a, int b) { return a + (int) (uni() * (float) (b - a + 1) * 0.99999f); }
        bool chance (float p) { return uni() < p; }
        float gauss() { float a = 0; for (int i = 0; i < 4; ++i) a += uni(); return (a - 2.0f) * 1.7320508f; }
    };

    // one-pole smoother for parameters
    struct Smooth
    {
        float y = 0.0f, a = 0.0f;
        void setTime (float seconds, double sr) { a = seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * (float) sr)); }
        float process (float x) { y = x + a * (y - x); return y; }
    };
}
