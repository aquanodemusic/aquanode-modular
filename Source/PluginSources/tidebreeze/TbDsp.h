#pragma once
//==============================================================================
// Tidebreeze - envelopes, LFOs and filters of a voice
//==============================================================================
#include "TbCommon.h"

namespace tb
{
    //==========================================================================
    // The workstation envelope: four levels and four times.
    //
    //   Start level -> (Attack time) -> Attack level -> (Decay time) ->
    //   Break level -> (Slope time) -> Sustain level ... note off ...
    //   (Release time) -> Release level
    //
    // Levels are -99..99 (filter / pitch) or 0..99 (amp); the output is
    // level / 99. Segments are linear in level; the amp maps it to gain with
    // a curve (Voice), which is how these envelopes sound.
    //==========================================================================
    struct Envelope
    {
        float startL = 0, attackL = 99, breakL = 99, sustainL = 99, releaseL = 0;
        float attackT = 0, decayT = 0, slopeT = 0, releaseT = 0;   // seconds

        enum Stage { Idle, Attack, Decay, Slope, Sustain, Release };
        Stage stage = Idle;
        float value = 0.0f, from = 0.0f, to = 0.0f, pos = 0.0f, rate = 0.0f;

        void trigger()
        {
            value = startL;
            begin (Attack, attackL, attackT, 1.0f);
        }
        void release (float scale = 1.0f)
        {
            if (stage == Idle || stage == Release) return;
            begin (Release, releaseL, releaseT * scale, 1.0f);
        }
        void begin (Stage s, float target, float seconds, float)
        {
            stage = s; from = value; to = target; pos = 0.0f;
            rate = seconds > 0.0005f ? 1.0f / seconds : 1.0e6f;
        }
        // advance dt seconds; returns level / 99
        float advance (float dt)
        {
            switch (stage)
            {
                case Idle: case Sustain: break;
                default:
                    pos += dt * rate;
                    if (pos >= 1.0f)
                    {
                        value = to;
                        if (stage == Attack)       begin (Decay, breakL, decayT, 1.0f);
                        else if (stage == Decay)   begin (Slope, sustainL, slopeT, 1.0f);
                        else if (stage == Slope)   { stage = Sustain; value = sustainL; }
                        else                       { stage = Idle; value = releaseL; }
                    }
                    else
                        value = from + (to - from) * pos;
                    break;
            }
            return value * (1.0f / 99.0f);
        }
        bool finished() const { return stage == Idle; }
    };

    //==========================================================================
    // LFO shapes (the workstation set): output -1..1 before the offset
    //==========================================================================
    enum LfoShape
    {
        LfoTriangle, LfoSaw, LfoSquare, LfoSine, LfoGuitar, LfoExpTri, LfoExpSawDown, LfoExpSawUp,
        LfoStepTri4, LfoStepTri6, LfoStepSaw4, LfoStepSaw6,
        LfoRandom1, LfoRandom2, LfoRandom3, LfoRandom4, LfoRandom5, LfoRandom6, NumLfoShapes
    };
    inline const juce::StringArray& lfoShapeNames()
    {
        static const juce::StringArray n { "Triangle", "Saw", "Square", "Sine", "Bend Up", "Soft Triangle", "Falling Curve",
                                           "Rising Curve", "Stairs 4", "Stairs 6", "Steps Down 4", "Steps Down 6",
                                           "Random Hold", "Random Glide", "Random Sparse", "Drift Slow",
                                           "Drift", "Drift Fast" };
        return n;
    }
    // tempo divisions, in beats
    inline const juce::StringArray& lfoDivNames()
    {
        static const juce::StringArray n { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars", "8 Bars" };
        return n;
    }
    inline float divBeats (int i)
    {
        static const float b[] = { 0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f };
        return b[juce::jlimit (0, 11, i)];
    }

    struct Lfo
    {
        int shape = LfoTriangle;
        float hz = 1.0f;
        bool keySync = true;
        float fadeSec = 0.0f, delaySec = 0.0f, offset = 0.0f;   // offset -1..1
        float phase = 0.0f, age = 0.0f;
        float sh = 0.0f, shNext = 0.0f, shPrev = 0.0f;
        float vx = 0.0f, vy = 0.0f, vtx = 0.0f, vty = 0.0f;     // vector randoms: a point wandering to random targets
        juce::uint32 seed = 1;

        float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (2.0f / 16777216.0f) - 1.0f; }

        void trigger (float startPhase)
        {
            if (keySync) phase = startPhase;
            age = 0.0f;
            shPrev = sh = rnd(); shNext = rnd();
            vx = rnd(); vy = rnd(); vtx = rnd(); vty = rnd();
        }

        // advance dt seconds; returns -1..1 (scaled by the fade-in)
        float advance (float dt)
        {
            age += dt;
            const float before = phase;
            phase += hz * dt;
            const bool wrapped = phase >= 1.0f;
            if (wrapped) phase -= std::floor (phase);
            const float p = phase;
            float v = 0.0f;
            switch (shape)
            {
                case LfoTriangle:   v = p < 0.25f ? p * 4.0f : (p < 0.75f ? 2.0f - p * 4.0f : p * 4.0f - 4.0f); break;
                case LfoSaw:        v = 1.0f - 2.0f * p; break;
                case LfoSquare:     v = p < 0.5f ? 1.0f : -1.0f; break;
                case LfoSine:       v = std::sin (kTwoPi * p); break;
                case LfoGuitar:     v = std::abs (std::sin (kPi * p)) * 2.0f - 1.0f; break;   // rectified: vibrato that only bends up
                case LfoExpTri:     { const float t = p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f; v = t * t * 2.0f - 1.0f; break; }
                case LfoExpSawDown: { const float t = 1.0f - p; v = t * t * 2.0f - 1.0f; break; }
                case LfoExpSawUp:   v = p * p * 2.0f - 1.0f; break;
                case LfoStepTri4: case LfoStepTri6:
                {
                    const float steps = shape == LfoStepTri4 ? 4.0f : 6.0f;
                    const float q = std::floor (p * steps * 2.0f) / (steps * 2.0f);
                    v = q < 0.5f ? q * 4.0f - 1.0f : 3.0f - q * 4.0f;
                    break;
                }
                case LfoStepSaw4: case LfoStepSaw6:
                {
                    const float steps = shape == LfoStepSaw4 ? 4.0f : 6.0f;
                    v = 1.0f - 2.0f * std::floor (p * steps) / (steps - 1.0f);
                    break;
                }
                case LfoRandom1: if (wrapped) sh = rnd(); v = sh; break;                                     // sample & hold
                case LfoRandom2:                                                                               // S&H, glided
                    if (wrapped) { shPrev = shNext; shNext = rnd(); }
                    v = shPrev + (shNext - shPrev) * (0.5f - 0.5f * std::cos (kPi * p));
                    break;
                case LfoRandom3: if (wrapped && rnd() > -0.3f) sh = rnd(); v = sh; break;                   // S&H, irregular
                default:                                                                                       // vector randoms
                {
                    if (wrapped) { vtx = rnd(); vty = rnd(); }
                    const float speed = shape == LfoRandom4 ? 3.0f : (shape == LfoRandom5 ? 6.0f : 12.0f);
                    const float k = 1.0f - std::exp (-speed * hz * dt);
                    vx += (vtx - vx) * k; vy += (vty - vy) * k;
                    v = shape == LfoRandom6 ? vy : vx;
                    break;
                }
            }
            juce::ignoreUnused (before);
            float g = 1.0f;
            if (age < delaySec) g = 0.0f;
            else if (fadeSec > 0.0f) g = std::min (1.0f, (age - delaySec) / fadeSec);
            return juce::jlimit (-1.0f, 1.0f, v + offset) * g;
        }
    };

    //==========================================================================
    // a TPT state-variable filter (Zavalishin), stable under fast modulation
    //==========================================================================
    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        void set (float hz, float q, double sr)
        {
            hz = juce::jlimit (10.0f, (float) sr * 0.45f, hz);
            g = std::tan (kPi * hz / (float) sr);
            k = 1.0f / std::max (0.3f, q);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0; }
        // returns low; band and high through refs
        inline float process (float x, float& band, float& high)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            band = v1; high = x - k * v1 - v2;
            return v2;
        }
        inline float low (float x) { float b, h; return process (x, b, h); }
        inline float highOut (float x) { float b, h; process (x, b, h); return h; }
    };

    // the oscillator filter: a 24 dB resonant low-pass (two SVFs, the
    // resonance on the second), or a 12 dB low-pass and 12 dB high-pass in series
    struct VoiceFilter
    {
        Svf s1, s2;
        int type = 0;       // 0 resonant LPF, 1 LPF + HPF
        void reset() { s1.reset(); s2.reset(); }
        void set (float cutA, float cutB, float reso01, double sr)
        {
            if (type == 0)
            {
                s1.set (cutA, 0.54f, sr);
                s2.set (cutA, 0.6f + reso01 * reso01 * 22.0f, sr);
            }
            else
            {
                s1.set (cutA, 0.707f + reso01 * reso01 * 6.0f, sr);
                s2.set (cutB, 0.707f, sr);
            }
        }
        inline float process (float x)
        {
            if (type == 0) return s2.low (s1.low (x));
            return s2.highOut (s1.low (x));
        }
    };
}
