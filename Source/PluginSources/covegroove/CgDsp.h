#pragma once
//==============================================================================
// Cove Groove - the small DSP pieces every part of it uses (no JUCE, so the
// voices can be rendered and measured by a plain command-line harness).
//==============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace covegroove
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kTwoPi = 6.28318530717959f;

    inline float clampf (float x, float lo, float hi) { return std::min (hi, std::max (lo, x)); }
    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

    // white noise, -1..1 (xorshift)
    struct Rng
    {
        uint32_t s { 0x9E3779B9u };
        explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
        float next()
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) (s >> 8) * (1.0f / 8388608.0f) - 1.0f;
        }
        float uni() { return 0.5f * (next() + 1.0f); }                 // 0..1
        int below (int n) { return n <= 1 ? 0 : (int) (uni() * (float) n) % n; }
    };

    // the TPT state-variable filter (Zavalishin): low, band and high at once
    struct Svf
    {
        float g { 0.0f }, k { 1.0f }, a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f };
        float ic1 { 0.0f }, ic2 { 0.0f };
        float lp { 0.0f }, bp { 0.0f }, hp { 0.0f };

        void set (float sr, float hz, float q)
        {
            g = std::tan (kPi * clampf (hz, 10.0f, sr * 0.49f) / sr);
            k = 1.0f / std::max (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0.0f; }
        float process (float x)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
            return bp;
        }
    };

    struct OnePole
    {
        float a { 0.0f }, z { 0.0f };
        void setLowpass (float sr, float hz) { a = 1.0f - std::exp (-kTwoPi * clampf (hz, 1.0f, sr * 0.49f) / sr); }
        float lowpass (float x) { z += a * (x - z); return z; }
        float highpass (float x) { z += a * (x - z); return x - z; }
        void reset() { z = 0.0f; }
    };

    // a band-limited square / saw (polyBLEP)
    inline float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    struct BlepOsc
    {
        float phase { 0.0f };
        float square (float inc)
        {
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            float y = phase < 0.5f ? 1.0f : -1.0f;
            y += polyBlep (phase, inc);
            float t2 = phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
            y -= polyBlep (t2, inc);
            return y;
        }
        float saw (float inc)
        {
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            return 2.0f * phase - 1.0f - polyBlep (phase, inc);
        }
    };

    // equal-power pan, -1 (left) .. +1 (right)
    inline void panGains (float pan, float& l, float& r)
    {
        const float a = (clampf (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        l = std::cos (a) * 1.41421356f;
        r = std::sin (a) * 1.41421356f;
    }
}
