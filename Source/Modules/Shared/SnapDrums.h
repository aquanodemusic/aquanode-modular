#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

// The snappy kit (6.9.1): a kick-clap, a clap, a clack and a snip, measured from a drum track
// hit by hit (spectra in 1 ms steps, the envelopes, the kick's pitch sweep) and rebuilt from
// noise, filters and a swept sine - no samples. Used as extra models by the Kick, Clap and
// Hats modules; their own knobs scale the fitted values (at the defaults: the measured sound).
namespace aquanode::snapdrums
{
    struct Noise
    {
        uint32_t s { 0x9e3779b9u };
        float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((int32_t) s) * (1.0f / 2147483648.0f); }
    };

    // a TPT state-variable filter (stable at every setting)
    struct Svf
    {
        float g { 0.1f }, k { 1.0f }, a1 { 0 }, a2 { 0 }, a3 { 0 }, ic1 { 0 }, ic2 { 0 };
        float lp { 0 }, bp { 0 }, hp { 0 };
        void set (float hz, float q, float sr)
        {
            g = std::tan (3.14159265f * std::min (hz, 0.45f * sr) / sr);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void process (float x)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
        void reset() { ic1 = ic2 = lp = bp = hp = 0.0f; }
    };

    //==========================================================================
    // the snap: a bright crack (2..12 kHz) peaking 2 ms in, gone in ~7 ms, and a thin
    // hiss behind it for ~60 ms - the "clack" on its own, the top of the kick-clap
    //==========================================================================
    struct Snap
    {
        void prepare (float sampleRate) { sr = sampleRate; setTone (1.0f); reset(); }
        void setTone (float scale)
        {
            hp1.set (2000.0f * scale, 0.7f, sr); lp1.set (std::min (12000.0f * scale, 0.45f * sr), 0.7f, sr);
            hp2.set (2500.0f * scale, 0.7f, sr); res.set (1400.0f * scale, 4.0f, sr);
        }
        void trigger (float decayScale, float tailScale = 1.0f, float tailLevel = 1.0f)
        {
            t = 0.0f; tau = 0.0067f * decayScale; tailTau = 0.025f * tailScale; tailGain = tailLevel; active = true;
        }
        void reset() { hp1.reset(); lp1.reset(); hp2.reset(); res.reset(); active = false; }
        float process()
        {
            if (! active) return 0.0f;
            const float n = noise.next();
            const float rise = std::min (1.0f, t / 0.002f);
            const float e = rise * std::exp (-std::max (0.0f, t - 0.002f) / tau);
            const float tail = kTail * tailGain * std::exp (-t / tailTau) * std::min (1.0f, t / 0.003f);
            hp1.process (n); lp1.process (hp1.hp);
            hp2.process (n);
            res.process (n);
            const float y = e * (lp1.lp + kBody * res.bp) + tail * hp2.hp;
            t += 1.0f / sr;
            if (t > std::max (0.05f, 12.0f * std::max (tau, tailTau))) active = false;
            return y;
        }
        static constexpr float kTail = 0.07f, kBody = 0.8f;   // the hiss -23 dB, a little wood in the crack
        float sr { 48000.0f }, t { 0.0f }, tau { 0.0067f }, tailTau { 0.025f }, tailGain { 1.0f };
        bool active { false };
        Svf hp1, lp1, hp2, res;
        Noise noise;
    };

    //==========================================================================
    // the kick-clap's kick: a sine swept 45 + 450 e^(-t/5 ms) + 150 e^(-t/26 ms) Hz, held
    // ~75 ms, then falling 0.29 dB per ms
    //==========================================================================
    struct SnapKick
    {
        struct Shape { float tuneHz { 45.0f }, drop { 1.0f }, sweepTime { 1.0f }, length { 1.0f }, snap { 1.0f }, drive { 0.2f }; };
        void prepare (float sampleRate) { sr = sampleRate; snapper.prepare (sr); reset(); }
        void reset() { active = false; snapper.reset(); }
        void trigger (const Shape& s) { sh = s; t = 0.0f; phase = 0.0; active = true; snapper.trigger (1.0f, 0.5f); }
        float process()
        {
            if (! active) return 0.0f;
            const float ms = t * 1000.0f;
            const float hz = sh.tuneHz * (1.0f + sh.drop * (10.0f * std::exp (-ms / (5.0f * sh.sweepTime)) + 3.33f * std::exp (-ms / (26.0f * sh.sweepTime))));
            phase += hz / sr;
            phase -= std::floor (phase);
            const float hold = 75.0f * sh.length;
            float a = ms < hold ? 1.0f : std::pow (10.0f, -0.0145f * (ms - hold) / sh.length);   // 0.29 dB / ms
            a *= std::min (1.0f, ms / 0.4f);
            const float body = (float) std::sin (phase * 6.283185307179586) * a;
            const float x = body + kSnapLevel * sh.snap * snapper.process();
            const float d = 1.0f + sh.drive * 6.0f;
            t += 1.0f / sr;
            if (ms > hold + 330.0f * sh.length && ! snapper.active) active = false;   // the body is 90 dB down by then
            return std::tanh (x * d) / std::tanh (d);
        }
        static constexpr float kSnapLevel = 2.4f;
        float sr { 48000.0f }, t { 0.0f };
        double phase { 0.0 };
        bool active { false };
        Shape sh;
        Snap snapper;
    };

    //==========================================================================
    // the clap: three cracks 10 ms apart (each down 20 dB in ~3 ms), a 50 ms room behind;
    // its colour from two resonances (1.4 and 4.5 kHz) over a band around 3 kHz
    //==========================================================================
    struct SnappyClap
    {
        void prepare (float sampleRate) { sr = sampleRate; setTone (1.0f); reset(); }
        void setTone (float scale)
        {
            r1.set (1400.0f * scale, 3.0f, sr); r2.set (4500.0f * scale, 2.0f, sr);
            wide.set (3200.0f * scale, 0.6f, sr); air.set (std::min (9000.0f * scale, 0.45f * sr), 0.8f, sr); cup.set (550.0f * scale, 1.0f, sr);
        }
        void reset() { r1.reset(); r2.reset(); wide.reset(); air.reset(); cup.reset(); active = false; }
        void trigger (float spreadMs, float burstScale, float tailScale, float tailMix)
        {
            t = 0.0f; spread = spreadMs * 0.001f; tauB = 0.0015f * burstScale; tauT = 0.015f * tailScale; mix = tailMix; active = true;
        }
        float process()
        {
            if (! active) return 0.0f;
            float e = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float tb = t - b * spread;
                if (tb >= 0.0f) e += (b == 0 ? 1.0f : 0.8f) * std::min (1.0f, tb / 0.0004f) * std::exp (-tb / tauB);
            }
            const float tt = t - 2.0f * spread;
            if (tt >= 0.0f) e += mix * kTail * std::exp (-tt / tauT);
            const float n = noise.next();
            r1.process (n); r2.process (n); wide.process (n); air.process (n); cup.process (n);
            const float y = e * (kR1 * r1.bp + kR2 * r2.bp + kWide * wide.bp + kAir * air.bp + kCup * cup.bp);
            t += 1.0f / sr;
            if (t > 2.0f * spread + 12.0f * tauT + 0.05f) active = false;
            return y;
        }
        static constexpr float kTail = 0.5f, kR1 = 0.6f, kR2 = 0.6f, kWide = 1.0f, kAir = 1.4f, kCup = 1.0f;
        float sr { 48000.0f }, t { 0.0f }, spread { 0.01f }, tauB { 0.0015f }, tauT { 0.018f }, mix { 0.6f };
        bool active { false };
        Svf r1, r2, wide, air, cup;
        Noise noise;
    };

    //==========================================================================
    // the snip: bright noise (2..14 kHz) with two faint rings (5.8 and 8.3 kHz), down
    // 20 dB in ~40 ms
    //==========================================================================
    struct Snip
    {
        void prepare (float sampleRate) { sr = sampleRate; setTone (1.0f, 1.0f); reset(); }
        void setTone (float scale, float ringScale)
        {
            hp.set (1400.0f * scale, 0.5f, sr); lp.set (std::min (16000.0f * scale, 0.45f * sr), 0.7f, sr);
            r1.set (5800.0f * ringScale, 12.0f, sr); r2.set (std::min (8300.0f * ringScale, 0.45f * sr), 12.0f, sr);
        }
        void reset() { hp.reset(); lp.reset(); r1.reset(); r2.reset(); active = false; }
        void trigger (float decayScale) { t = 0.0f; tau = 0.017f * decayScale; active = true; }
        float process()
        {
            if (! active) return 0.0f;
            const float e = std::min (1.0f, t / 0.0005f) * std::exp (-t / tau);
            const float n = noise.next();
            hp.process (n); lp.process (hp.hp); r1.process (n); r2.process (n);
            const float y = e * (lp.lp + kRing * (r1.bp + r2.bp));
            t += 1.0f / sr;
            if (t > 14.0f * tau + 0.01f) active = false;
            return y;
        }
        static constexpr float kRing = 0.5f;
        float sr { 48000.0f }, t { 0.0f }, tau { 0.017f };
        bool active { false };
        Svf hp, lp, r1, r2;
        Noise noise;
    };
}
