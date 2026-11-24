#pragma once
// (6.9.21) CPU > Anti-alias: one switch for the whole app (remembered, not part of a patch).
// Off (the default): every module as it always was. On: the oscillators that fold harmonics
// above the top of the audio range back down (aliasing, heard as off-key tones on high notes)
// band-limit themselves - polyBLEP corrections at their jumps, partials above the limit left out,
// or a wavetable's frames with the top harmonics taken away. Modules that are already clean
// never read it.
#include <atomic>
#include <cmath>

namespace aquanode
{
    inline std::atomic<bool>& antiAliasEnabled()
    {
        static std::atomic<bool> on { false };
        return on;
    }
    inline bool antiAliasOn() { return antiAliasEnabled().load (std::memory_order_relaxed); }

    // polyBLEP: the correction for a unit step at phase 0 of a 0..1 phase moving dt per sample
    // (subtract it from a saw's fall, add it at a square's rise)
    inline double polyBlep (double t, double dt)
    {
        if (dt <= 0.0) return 0.0;
        if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    // polyBLAMP: the same for a corner (a triangle's turn), scaled by the slope change
    inline double polyBlamp (double t, double dt)
    {
        if (dt <= 0.0) return 0.0;
        if (t < dt)       { t = t / dt - 1.0; return -1.0 / 3.0 * t * t * t; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt + 1.0; return 1.0 / 3.0 * t * t * t; }
        return 0.0;
    }

    // the band-limited waves at a 0..1 phase moving dt a sample (polyBLEP / polyBLAMP):
    // saw (2t - 1), square (+1 then -1), a pulse (+1 below width w), triangle (-1 at 0, +1 at 0.5)
    inline double wrap01 (double t) { return t - std::floor (t); }
    inline double blSaw (double t, double dt) { return 2.0 * t - 1.0 - polyBlep (t, dt); }
    inline double blPulse (double t, double w, double dt) { return (t < w ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (wrap01 (t - w), dt); }
    inline double blSquare (double t, double dt) { return blPulse (t, 0.5, dt); }
    inline double blTriangle (double t, double dt)
    {
        return 1.0 - 4.0 * std::abs (t - 0.5) + 4.0 * dt * (polyBlamp (t, dt) - polyBlamp (wrap01 (t + 0.5), dt));
    }

    // two samples at twice the rate in, one out: a 47-tap low-pass (Blackman-windowed sinc, cut a
    // little under the old top) - an oscillator made at 2x, brought back down,
    // its partials between the old and the new top cleared away (23 samples late at 2x)
    struct Decimator2x
    {
        static constexpr int kTaps = 47;
        static const float* coeffs()
        {
            static const struct C
            {
                float c[kTaps];
                C()
                {
                    double sum = 0.0, v[kTaps];
                    for (int n = 0; n < kTaps; ++n)
                    {
                        const double x = n - (kTaps - 1) / 2.0, pi = 3.14159265358979323846;
                        // cut at 0.45 of the doubled rate's half (21.6 kHz at 48 kHz): what lies above the old
                        // top is gone before it can fold back
                        const double fc = 0.225;   // (cycles per sample at the doubled rate)
                        const double sinc = x == 0.0 ? 2.0 * fc : std::sin (2.0 * pi * fc * x) / (pi * x);
                        const double w = 0.42 - 0.5 * std::cos (2.0 * pi * n / (kTaps - 1)) + 0.08 * std::cos (4.0 * pi * n / (kTaps - 1));
                        v[n] = sinc * w;
                        sum += v[n];
                    }
                    for (int n = 0; n < kTaps; ++n) c[n] = (float) (v[n] / sum);
                }
            } table;
            return table.c;
        }
        float hist[64] {};
        int pos { 0 };
        void reset() { for (auto& h : hist) h = 0.0f; pos = 0; }
        float process (float first, float second)
        {
            hist[pos] = first;  pos = (pos + 1) & 63;
            hist[pos] = second; pos = (pos + 1) & 63;
            const float* c = coeffs();
            float y = 0.0f;
            for (int k = 0; k < kTaps; ++k)
                if (c[k] != 0.0f) y += c[k] * hist[(pos - 1 - k) & 63];
            return y;
        }
    };

    // four samples at four times the rate in, one out: two of the above in a row
    struct Decimator4x
    {
        Decimator2x first, second;
        void reset() { first.reset(); second.reset(); }
        float process (float a, float b, float c, float d)
        {
            const float x = first.process (a, b);
            return second.process (x, first.process (c, d));
        }
    };
}
