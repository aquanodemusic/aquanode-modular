#pragma once

//==============================================================================
// FxKit - the small building blocks of the 6.7.0 effects (Multiband Delay,
// Filter Table EQ, Modal Body, Bloom Reverb, Beat Repeat, Drum Morpher): a
// zero-delay-feedback state variable filter, Linkwitz-Riley band splits, an
// interpolating delay line, an allpass diffuser and a cheap random source.
// Header only, no allocation outside prepare(), plain C++17 (MSVC friendly).
//==============================================================================
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include <cstdint>

namespace aquanode::fxkit
{
    constexpr float kPi = 3.14159265358979f;

    inline float flushDenormal (float x) noexcept { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

    // smooths a control toward its target (time constant in seconds)
    struct Smoother
    {
        float value { 0.0f }, coeff { 1.0f };
        void prepare (double sr, float seconds) { coeff = seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (float) (seconds * sr)); }
        void snap (float v) { value = v; }
        float next (float target) noexcept { value += (target - value) * coeff; return value; }
    };

    //==========================================================================
    // Zero-delay-feedback state variable filter (Simper / TPT). One call gives
    // low, band and high pass at once; stable under fast modulation.
    struct Svf
    {
        float ic1 { 0.0f }, ic2 { 0.0f };
        float g { 0.0f }, k { 1.41421356f }, a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f };

        void set (double sr, float hz, float q) noexcept
        {
            hz = juce::jlimit (5.0f, (float) (sr * 0.49), hz);
            g = std::tan (kPi * hz / (float) sr);
            k = 1.0f / juce::jmax (0.05f, q);
            update();
        }
        void setGK (float gIn, float kIn) noexcept { g = gIn; k = kIn; update(); }
        void update() noexcept
        {
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() noexcept { ic1 = ic2 = 0.0f; }

        struct Out { float lp, bp, hp; };
        Out process (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = flushDenormal (2.0f * v1 - ic1);
            ic2 = flushDenormal (2.0f * v2 - ic2);
            return { v2, v1, x - k * v1 - v2 };
        }
    };

    // A bell (peaking) band on an Svf whose frequency is fixed: only the gain
    // moves, so moving it costs a division, no trigonometry.
    struct Bell
    {
        Svf f;
        float g0 { 0.0f }, q { 1.0f }, m1 { 0.0f };
        void prepare (double sr, float hz, float qIn)
        {
            q = qIn;
            g0 = std::tan (kPi * juce::jlimit (5.0f, (float) (sr * 0.49), hz) / (float) sr);
            setGainDb (0.0f);
        }
        void setGainDb (float db) noexcept
        {
            const float A = std::pow (10.0f, db / 40.0f);
            const float k = 1.0f / (q * A);
            f.setGK (g0, k);
            m1 = k * (A * A - 1.0f);
        }
        float process (float x) noexcept { const auto o = f.process (x); return x + m1 * o.bp; }
        void reset() noexcept { f.reset(); }
        // the gain (dB) this bell has at 'hz' for a given band gain - for the
        // card's curve: an analog prototype bell, close to the digital one
        static float responseDb (float hz, float centreHz, float q, float db)
        {
            const double A = std::pow (10.0, db / 40.0);
            const double w = hz / centreHz;
            const double re0 = 1.0 - w * w;
            const double num = re0 * re0 + std::pow (w * A / q, 2.0);
            const double den = re0 * re0 + std::pow (w / (A * q), 2.0);
            return (float) (10.0 * std::log10 (num / den));
        }
    };

    //==========================================================================
    // Linkwitz-Riley (24 dB / octave) split at one frequency: low + high sum
    // back to an allpass, so the bands rejoin flat.
    struct LR4
    {
        Svf lp1, lp2, hp1, hp2;
        void set (double sr, float hz) noexcept
        {
            for (auto* s : { &lp1, &lp2, &hp1, &hp2 }) s->set (sr, hz, 0.70710678f);
        }
        void reset() noexcept { for (auto* s : { &lp1, &lp2, &hp1, &hp2 }) s->reset(); }
        void process (float x, float& low, float& high) noexcept
        {
            low = lp2.process (lp1.process (x).lp).lp;
            high = hp2.process (hp1.process (x).hp).hp;
        }
        // the same allpass a band goes through when the other bands are split
        // again further up (keeps three bands in phase with each other)
        float allpass (float x) noexcept { float l, h; process (x, l, h); return l + h; }
    };

    //==========================================================================
    // Delay line with a fractional (cubic) read. Power-of-two buffer.
    struct DelayLine
    {
        std::vector<float> buf;
        int mask { 0 }, write { 0 };

        void prepare (int maxSamples)
        {
            int n = 1;
            while (n < maxSamples + 4) n <<= 1;
            buf.assign ((size_t) n, 0.0f);
            mask = n - 1;
            write = 0;
        }
        void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); write = 0; }
        void push (float x) noexcept { buf[(size_t) write] = x; write = (write + 1) & mask; }
        // 'delay' samples back from the newest pushed sample (>= 1)
        float read (float delay) const noexcept
        {
            delay = juce::jlimit (1.0f, (float) (mask - 3), delay);
            const float pos = (float) write - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;
            const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)],
                        y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
            // Catmull-Rom
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }
        float readInt (int delay) const noexcept { return buf[(size_t) ((write - delay) & mask)]; }
        int capacity() const noexcept { return mask + 1; }
    };

    // Schroeder allpass diffuser around a delay line
    struct Allpass
    {
        DelayLine d;
        void prepare (int maxSamples) { d.prepare (maxSamples); }
        void reset() noexcept { d.reset(); }
        float process (float x, float delay, float gain) noexcept
        {
            const float delayed = d.read (delay);
            const float v = x + gain * delayed;
            d.push (flushDenormal (v));
            return delayed - gain * v;
        }
    };

    //==========================================================================
    struct Rng
    {
        uint32_t s { 0x9E3779B9u };
        explicit Rng (uint32_t seed = 1) : s (seed ? seed : 1u) {}
        uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0..1
        float bi() noexcept { return uni() * 2.0f - 1.0f; }                              // -1..1
    };

    // equal-power pan, -1..1
    inline void panGains (float pan, float& l, float& r) noexcept
    {
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * kPi;
        l = std::cos (a);
        r = std::sin (a);
    }

    // musical lengths in quarter notes, for tempo-synced times
    struct NoteDivision { const char* name; double quarters; };
    inline const std::vector<NoteDivision>& noteDivisions()
    {
        static const std::vector<NoteDivision> d {
            { "1/64", 1.0 / 16.0 }, { "1/32T", 1.0 / 12.0 }, { "1/32", 1.0 / 8.0 }, { "1/16T", 1.0 / 6.0 },
            { "1/32D", 3.0 / 16.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/16D", 0.375 },
            { "1/8", 0.5 }, { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 }, { "1/4", 1.0 }, { "1/2T", 4.0 / 3.0 },
            { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/1T", 8.0 / 3.0 }, { "1/2D", 3.0 }, { "1/1", 4.0 } };
        return d;
    }
    inline juce::StringArray noteDivisionNames()
    {
        juce::StringArray a;
        for (const auto& n : noteDivisions()) a.add (n.name);
        return a;
    }
    // the division nearest to a time (seconds) at a tempo
    inline double snapToDivision (double seconds, double bpm)
    {
        const double q = seconds * bpm / 60.0;
        double best = noteDivisions().front().quarters, bestErr = 1.0e9;
        for (const auto& n : noteDivisions())
        {
            const double err = std::abs (std::log (q / n.quarters));
            if (err < bestErr) { bestErr = err; best = n.quarters; }
        }
        return best * 60.0 / bpm;
    }
}
