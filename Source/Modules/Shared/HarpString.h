#pragma once

#include "PhysInstruments.h"
#include "StudioVoicing.h"
#include <complex>

// The concert harp (6.9.1, Pluck's Harp model): a gut string plucked near its middle by the
// pad of a finger, as two polarisations of a waveguide string (a hair apart in tuning and
// decay: the slow beating of a real string), into the soundboard.
// Fitted to a recorded harp, note by note: the decay per key and of the highs, the finger's
// softness (the spectrum at the pluck), the point on the string (the weak even harmonics in
// the middle range) and the soundboard's radiation (studio::Harp).
namespace aquanode::harp
{
    // one polarisation: a waveguide whose loss is the same at every frequency up to the knee
    // and a little more above it (a shelf, as the recorded strings decay - not a low-pass)
    struct HarpLoop
    {
        static constexpr int kSize = 4096;
        void reset() { std::fill (std::begin (buf), std::end (buf), 0.0); w = 0; lp = 0.0; }
        void set (double f0, double t60, double extraDb, double kneeHz, double sr)
        {
            a = 1.0 - std::exp (-6.283185307179586 * std::min (kneeHz, 0.4 * sr) / sr);
            sh = 1.0 - std::pow (10.0, -std::max (0.0, extraDb) / 20.0);
            g = std::min (std::pow (10.0, -3.0 / (std::max (0.02, t60) * f0)) / std::abs (response (6.283185307179586 * f0 / sr)), 0.99999);
        }
        std::complex<double> response (double wn) const
        {
            const auto z = std::polar (1.0, -wn);
            return (1.0 - sh) + sh * a / (1.0 - (1.0 - a) * z);
        }
        double process (double exc, double f, double sr, bool recompute)
        {
            if (recompute)
            {
                const double wn = 6.283185307179586 * f / sr;
                d = std::max (4.0, sr / f + std::arg (response (wn)) / wn);   // less the shelf's own delay
            }
            const double y = physinst::lagrange<kSize> (buf, w, d);
            lp += a * (y - lp);
            buf[w] = g * ((1.0 - sh) * y + sh * lp) + exc;
            w = (w + 1) & (kSize - 1);
            return y;
        }
        double buf[kSize] {};
        int w { 0 };
        double lp { 0 }, g { 0.999 }, sh { 0.0 }, a { 0.1 }, d { 100 };
    };

    struct Shape { double t60Scale { 1.0 }, hiScale { 1.0 }, fingerScale { 1.0 }; };

    class HarpString
    {
    public:
        void reset() { p1.reset(); p2.reset(); active = false; n = 0; level = 0.0; t = 0.0; }
        bool isActive() const { return active; }

        void start (double key, double f0, double velocity, const Shape& sh, double sr, uint64_t seed)
        {
            rng.reseed (seed);
            freq = f0;
            const double k = (key - 60.0) / 12.0;
            // the decay: ~8 s on the bass strings, ~1.5 s at the top; the highs die sooner
            const double t60 = std::clamp (std::exp (kT60[0] + kT60[1] * k + kT60[2] * k * k + kT60Top * std::clamp (k, 0.0, 1.5)) * sh.t60Scale, 0.05, 40.0);
            // above the knee every pass loses a little more: the highs die away sooner
            const double extra = kExtraDb * std::pow (2.0, -std::clamp (key - 62.0, 0.0, 36.0) / 12.0) * sh.hiScale;
            const double knee = std::max (kKneeHz, 1.5 * f0);
            // the finger: a soft pulse, wider on the bass strings, firmer when played harder
            const double vel = std::clamp (velocity, 0.0, 1.0);
            const double ms = kFingerMs * std::pow (2.0, -std::clamp (key - 56.0, -12.0, 36.0) / 12.0 * kFingerSlope) * sh.fingerScale * (1.25 - 0.5 * vel);
            pulseLen = std::max (2, (int) (ms * 1.0e-3 * sr));
            // ... and the instant the string leaves the skin: a short click on top
            snapLen = std::max (2, (int) (kSnapMs * 1.0e-3 * sr));
            snapAmp = std::pow (10.0, kSnapDb / 20.0) * (double) pulseLen / (double) snapLen;
            // plucked near the middle (a little either side each time)
            const double pos = kPosition + 0.06 * (rng.uni() - 0.5);
            dp = std::max (1, (int) std::lround (pos * sr / f0));
            amp = kAmp / std::sqrt ((double) pulseLen);
            p1.reset(); p2.reset();
            p1.set (f0, t60, extra, knee, sr);
            p2.set (f0, t60 * kPolT60, extra / kPolT60, knee, sr);
            det = std::pow (2.0, kDetune * (0.5 + rng.uni()) / 1200.0);
            n = 0; active = true; level = 0.0; ctl = 0; t = 0.0;
        }

        double process (double bendRatio, double sr)
        {
            if (! active) return 0.0;
            const double e = pulse (n) - pulse (n - dp);
            ++n;
            const bool rc = (ctl++ & 15) == 0;
            const double f = freq * (1.0 + kGlide * std::exp (-t / 0.05)) * bendRatio;   // a plucked string starts a hair sharp
            const double y = (1.0 - kPolMix) * p1.process (e, f, sr, rc) + kPolMix * p2.process (e, f * det, sr, rc);
            t += 1.0 / sr;
            level = std::max (std::abs (y), level * 0.9995);
            if (n > pulseLen + dp && t > 0.1 && level < 2.0e-6) active = false;
            if (! std::isfinite (y)) { reset(); return 0.0; }
            return y;
        }

        // the fitted constants
        static constexpr double kT60[3] = { 1.586, -0.3322, -0.0108 }, kT60Top = 0.3;   // ln T60 over (key - 60) / 12
        static constexpr double kExtraDb = 0.11, kKneeHz = 600.0;   // the shelf: dB per pass above the knee
        static constexpr double kFingerMs = 1.6, kFingerSlope = 0.6, kPosition = 0.46, kSnapMs = 0.12, kSnapDb = -48.0;
        static constexpr double kPolMix = 0.3, kPolT60 = 1.35, kDetune = 0.7, kGlide = 0.0015, kAmp = 1.0;

    private:
        double pulse (int i) const
        {
            double v = 0.0;
            if (i >= 0 && i < pulseLen) v = 0.5 - 0.5 * std::cos (6.283185307179586 * (i + 1) / (pulseLen + 1));
            const int j = i - (pulseLen - snapLen);   // the click comes as the finger lets go
            if (j >= 0 && j < snapLen) v += snapAmp * (0.5 - 0.5 * std::cos (6.283185307179586 * (j + 1) / (snapLen + 1)));
            return v * amp;
        }

        HarpLoop p1, p2;
        physinst::Rng rng;
        double freq { 220.0 }, det { 1.0 }, amp { 1.0 }, level { 0.0 }, t { 0.0 };
        int n { 0 }, pulseLen { 2 }, snapLen { 2 }, dp { 1 }, ctl { 0 };
        double snapAmp { 0.0 };
        bool active { false };
    };
}
