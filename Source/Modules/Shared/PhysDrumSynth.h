#pragma once
//==============================================================================
// Physically modelled drums: the TR-808 and TR-909 voices fitted to recordings
// of the machines, and the struck-body voice behind the fitted kits (large
// drums, world, metal, electro, classic, orchestral).
//
// A port of the Python reference models (aqpm/machines.py, aqpm/kits.py) that
// were fitted to the recordings. Every voice renders a whole hit at once, the
// way the reference does: several of them normalise against the hit's own peak
// or level, which only a complete render knows. The modules and Cove Groove
// render each sound once (on a background thread, whenever its knobs change)
// in a few round-robin variants and play those back - so playing costs almost
// nothing and every hit is the model's, sample for sample.
//
// Plain C++ (no JUCE), so it can be checked against the reference offline.
// Sample-rate independent: delay lengths and the 909's 32 kHz ROM follow the
// rate (the reference ran at 48 kHz).
//==============================================================================
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace aquanode::physdrums
{
    using Buf = std::vector<double>;
    constexpr double kTwoPi = 6.283185307179586476925;
    constexpr double kPi = 3.141592653589793238463;

    //==========================================================================
    // Noise
    //==========================================================================
    struct Rng
    {
        explicit Rng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { if (s == 0) s = 1; next(); next(); }
        uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
        double uni() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
        double sym() { return uni() * 2.0 - 1.0; }                                     // [-1, 1)
        uint64_t s;
    };

    inline Buf noise (int n, uint64_t seed)
    {
        Rng r (seed);
        Buf y ((size_t) n);
        for (auto& v : y) v = r.sym();
        return y;
    }

    inline double maxAbs (const Buf& x) { double m = 0.0; for (double v : x) m = std::max (m, std::abs (v)); return m; }
    inline double stdOf (const Buf& x, size_t n = 0)
    {
        n = (n == 0 || n > x.size()) ? x.size() : n;
        if (n == 0) return 0.0;
        double s = 0.0, q = 0.0;
        for (size_t i = 0; i < n; ++i) { s += x[i]; q += x[i] * x[i]; }
        const double mean = s / (double) n;
        return std::sqrt (std::max (0.0, q / (double) n - mean * mean));
    }
    inline void scale (Buf& x, double g) { for (auto& v : x) v *= g; }

    //==========================================================================
    // Filters (exactly the reference's)
    //==========================================================================
    inline void onepoleLP (Buf& x, double fc, double sr)
    {
        const double a = 1.0 - std::exp (-kTwoPi * fc / sr);
        double z = 0.0;
        for (auto& v : x) { z += a * (v - z); v = z; }
    }
    inline void onepoleHP (Buf& x, double fc, double sr)
    {
        const double a = 1.0 - std::exp (-kTwoPi * fc / sr);
        double z = 0.0;
        for (auto& v : x) { z += a * (v - z); v = v - z; }
    }
    inline Buf onepoleHPCopy (const Buf& x, double fc, double sr) { Buf y = x; onepoleHP (y, fc, sr); return y; }

    // Zavalishin TPT state-variable filter; mode 0 LP, 1 BP (unity peak), 2 HP
    inline void svfConst (Buf& x, double fc, double q, double sr, int mode)
    {
        const double f = std::clamp (fc, 10.0, sr * 0.49);
        const double k = 1.0 / std::max (0.05, q);
        const double g = std::tan (kPi * f / sr);
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        double ic1 = 0.0, ic2 = 0.0;
        for (auto& xv : x)
        {
            const double v3 = xv - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
            xv = mode == 0 ? v2 : (mode == 1 ? k * v1 : xv - k * v1 - v2);
        }
    }
    inline Buf svfCopy (const Buf& x, double fc, double q, double sr, int mode) { Buf y = x; svfConst (y, fc, q, sr, mode); return y; }

    // a 2-pole resonator as a rotating phasor; freq / tau may move every sample
    template <typename FreqFn, typename TauFn>
    inline Buf ringer (const Buf& exc, FreqFn freq, TauFn tau, double sr)
    {
        Buf y (exc.size());
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < exc.size(); ++i)
        {
            const double w = kTwoPi * freq (i) / sr;
            const double r = std::exp (-1.0 / (tau (i) * sr));
            const double c = std::cos (w) * r, s = std::sin (w) * r;
            const double nre = c * re - s * im + exc[i];
            const double nim = s * re + c * im;
            re = nre; im = nim;
            y[i] = im;
        }
        return y;
    }
    inline Buf ringerConst (const Buf& exc, double f, double tau, double sr)
    {
        return ringer (exc, [f] (size_t) { return f; }, [tau] (size_t) { return tau; }, sr);
    }

    // parallel modes; fscale (per sample) multiplies every mode frequency
    inline Buf modalBank (const Buf& exc, const std::vector<double>& freqs, const std::vector<double>& taus,
                          const std::vector<double>& amps, const Buf& fscale, double sr)
    {
        const size_t n = exc.size(), m = freqs.size();
        Buf y (n, 0.0);
        std::vector<double> re (m, 0.0), im (m, 0.0), rr (m), w0 (m), c (m), s (m);
        for (size_t j = 0; j < m; ++j) { rr[j] = std::exp (-1.0 / (taus[j] * sr)); w0[j] = kTwoPi * freqs[j] / sr; }
        double lastFs = -1.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double fs = fscale[i];
            if (fs != lastFs)
            {
                for (size_t j = 0; j < m; ++j)
                {
                    const double w = w0[j] * fs;
                    if (w > 3.0) { c[j] = 0.0; s[j] = 0.0; continue; }
                    c[j] = std::cos (w) * rr[j]; s[j] = std::sin (w) * rr[j];
                }
                lastFs = fs;
            }
            const double e = exc[i];
            double acc = 0.0;
            for (size_t j = 0; j < m; ++j)
            {
                if (c[j] == 0.0 && s[j] == 0.0) continue;
                const double nre = c[j] * re[j] - s[j] * im[j] + e * amps[j];
                const double nim = s[j] * re[j] + c[j] * im[j];
                re[j] = nre; im[j] = nim;
                acc += nim;
            }
            y[i] = acc;
        }
        return y;
    }

    inline Buf blepSquare (double freq, double sr, int n, double phase0)
    {
        Buf y ((size_t) n);
        double ph = phase0;
        const double inc = freq / sr;
        for (int i = 0; i < n; ++i)
        {
            ph += inc;
            if (ph >= 1.0) ph -= 1.0;
            double v = ph < 0.5 ? 1.0 : -1.0;
            double t = ph;
            if (t < inc) { t /= inc; v += t + t - t * t - 1.0; }
            else if (t > 1.0 - inc) { t = (t - 1.0) / inc; v += t * t + t + t + 1.0; }
            t = ph + 0.5;
            if (t >= 1.0) t -= 1.0;
            if (t < inc) { t /= inc; v -= t + t - t * t - 1.0; }
            else if (t > 1.0 - inc) { t = (t - 1.0) / inc; v -= t * t + t + t + 1.0; }
            y[(size_t) i] = v;
        }
        return y;
    }

    // sine with a per-sample frequency; shape bends it toward the 909's diode-shaped wave
    template <typename FreqFn>
    inline Buf sweepOsc (int n, FreqFn freq, double sr, double shape)
    {
        Buf y ((size_t) n);
        double ph = 0.0;
        const double norm = 1.0 / std::tanh (2.2);
        for (int i = 0; i < n; ++i)
        {
            ph += freq ((size_t) i) / sr;
            if (ph >= 1.0) ph -= 1.0;
            double s = std::sin (kTwoPi * ph);
            if (shape > 0.0)
                s = (1.0 - shape) * s + shape * std::tanh (2.2 * s) * norm;
            y[(size_t) i] = s;
        }
        return y;
    }

    // a hit read back as an old sampler does: held at its own rate, quantised,
    // repitched by dropping / repeating samples
    inline Buf romplerCrush (const Buf& x, double sr, double rate, int bits, double pitchRatio)
    {
        const int nOut = (int) ((double) x.size() / pitchRatio);
        Buf y ((size_t) std::max (0, nOut));
        const double q = std::pow (2.0, bits - 1);
        const double step = rate / sr;
        double tick = 0.0, held = 0.0;
        long long last = -1;
        for (int i = 0; i < nOut; ++i)
        {
            const long long k = (long long) tick;
            if (k != last)
            {
                const long long src = (long long) ((double) k * (sr / rate) * pitchRatio);
                if (src < (long long) x.size())
                    held = std::floor (std::clamp (x[(size_t) src], -1.0, 1.0) * q + 0.5) / q;
                else
                    held = 0.0;
                last = k;
            }
            y[(size_t) i] = held;
            tick += step;
        }
        return y;
    }

    // the kits' room: 8 damped delay lines through a Householder matrix (lengths for 48 kHz, scaled)
    inline void hall (const Buf& inL, const Buf& inR, Buf& outL, Buf& outR, double sr, double t60, double dampHz, double predelayMs)
    {
        static const int base[8] = { 1423, 1637, 1811, 2029, 2281, 2539, 2803, 3109 };
        int lens[8];
        for (int j = 0; j < 8; ++j) lens[j] = std::max (8, (int) std::lround (base[j] * sr / 48000.0));
        std::vector<double> buf[8];
        for (int j = 0; j < 8; ++j) buf[j].assign ((size_t) lens[j], 0.0);
        int idx[8] {};
        double lp[8] {}, g[8], o[8];
        for (int j = 0; j < 8; ++j) g[j] = std::pow (10.0, -3.0 * lens[j] / (t60 * sr));
        const double a = 1.0 - std::exp (-kTwoPi * dampHz / sr);
        const int pd = (int) (predelayMs * sr / 1000.0) + 1;
        std::vector<double> preL ((size_t) pd, 0.0), preR ((size_t) pd, 0.0);
        int pi = 0;
        const size_t n = inL.size();
        outL.assign (n, 0.0); outR.assign (n, 0.0);
        for (size_t i = 0; i < n; ++i)
        {
            const double il = preL[(size_t) pi], ir = preR[(size_t) pi];
            preL[(size_t) pi] = inL[i]; preR[(size_t) pi] = inR[i];
            if (++pi >= pd) pi = 0;
            double s = 0.0;
            for (int j = 0; j < 8; ++j) { o[j] = buf[j][(size_t) idx[j]]; s += o[j]; }
            s *= 2.0 / 8.0;
            for (int j = 0; j < 8; ++j)
            {
                const double v = o[j] - s;
                lp[j] += a * (v - lp[j]);
                buf[j][(size_t) idx[j]] = lp[j] * g[j] + ((j % 2 == 0) ? il : ir) * 0.35;
                if (++idx[j] >= lens[j]) idx[j] = 0;
            }
            outL[i] = o[0] - o[2] + o[4] - o[6];
            outR[i] = o[1] - o[3] + o[5] - o[7];
        }
    }

    // 4th-order Butterworth band-pass as two biquads (scipy's butter(2, [lo, hi], 'band'))
    struct Biquad { double b0, b1, b2, a1, a2; };
    inline std::array<Biquad, 2> butterBandpass2 (double lo, double hi, double sr)
    {
        using C = std::complex<double>;
        const double fs2 = 2.0 * sr;
        const double w1 = fs2 * std::tan (kPi * lo / sr), w2 = fs2 * std::tan (kPi * hi / sr);
        const double bw = w2 - w1, wo = std::sqrt (w1 * w2);
        const C pp[2] = { C (-std::sqrt (0.5), std::sqrt (0.5)), C (-std::sqrt (0.5), -std::sqrt (0.5)) };
        C sp[4];
        for (int i = 0; i < 2; ++i)
        {
            const C pl = pp[i] * (bw / 2.0);
            const C d = std::sqrt (pl * pl - wo * wo);
            sp[2 * i] = pl + d; sp[2 * i + 1] = pl - d;
        }
        // digital poles; zeros: two at z = 1 (from s = 0), two at z = -1
        C zp[4];
        C den (1.0, 0.0);
        for (int i = 0; i < 4; ++i) { zp[i] = (fs2 + sp[i]) / (fs2 - sp[i]); den *= (fs2 - sp[i]); }
        const C num = std::pow (C (fs2, 0.0), 2.0);       // (fs2 - 0)^2 for the zeros at s = 0
        const double k = bw * bw * (num / den).real();
        // pair each pole with its conjugate
        std::vector<C> rest (zp, zp + 4);
        std::array<Biquad, 2> out {};
        for (int s = 0; s < 2; ++s)
        {
            // the pole with the largest imaginary part and its conjugate
            size_t bi = 0;
            for (size_t i = 1; i < rest.size(); ++i) if (rest[i].imag() > rest[bi].imag()) bi = i;
            const C p = rest[bi];
            rest.erase (rest.begin() + (long) bi);
            size_t ci = 0; double best = 1e30;
            for (size_t i = 0; i < rest.size(); ++i) { const double d = std::abs (rest[i] - std::conj (p)); if (d < best) { best = d; ci = i; } }
            const C q = rest[ci];
            rest.erase (rest.begin() + (long) ci);
            Biquad b;
            b.b0 = 1.0; b.b1 = 0.0; b.b2 = -1.0;          // (1 - z^-1)(1 + z^-1)
            b.a1 = -(p + q).real(); b.a2 = (p * q).real();
            if (s == 0) { b.b0 *= k; b.b2 *= k; }
            out[(size_t) s] = b;
        }
        return out;
    }
    inline void sosFilter (Buf& x, const std::array<Biquad, 2>& sos)
    {
        for (const auto& b : sos)
        {
            double z1 = 0.0, z2 = 0.0;
            for (auto& v : x)
            {
                const double o = b.b0 * v + z1;
                z1 = b.b1 * v - b.a1 * o + z2;
                z2 = b.b2 * v - b.a2 * o;
                v = o;
            }
        }
    }

    //==========================================================================
    // Small helpers of the voices
    //==========================================================================
    inline Buf pulse (int n, double sr, double ms = 1.0, double roundHz = 2500.0)
    {
        Buf p ((size_t) n, 0.0);
        const int k = std::min (n, std::max (1, (int) (ms * 0.001 * sr)));
        for (int i = 0; i < k; ++i) p[(size_t) i] = 1.0;
        onepoleLP (p, roundHz, sr);
        return p;
    }
    inline double env2 (double t, double tau1, double tau2, double mix)
    {
        return mix * std::exp (-t / tau1) + (1.0 - mix) * std::exp (-t / tau2);
    }
    inline void shapeDrive (Buf& y, double drive, double asym)
    {
        if (drive <= 0.01 && std::abs (asym) < 1.0e-3) return;
        const double g = std::max (drive, 0.01);
        const double off = std::tanh (g * asym), norm = 1.0 / std::tanh (g);
        for (auto& v : y) v = (std::tanh (g * (v + asym)) - off) * norm;
    }
    inline int lenOf (double nS, double sr) { return std::max (16, (int) (sr * nS)); }

    //==========================================================================
    // Voice types and their parameters
    //==========================================================================
    enum class Voice { Kick808 = 0, Snare808, Tom808, Metal808, Cowbell808, Res808, Clap808, Maracas808,
                       Kick909, Snare909, Tom909, Rim909, Clap909, Cymbal909, Perc, Orch, NumVoices };

    // what a knob does when the sound is tuned / lengthened / brightened
    enum ParamKind : uint8_t { PK_None = 0, PK_Freq, PK_Time, PK_Bright };

    struct ParamInfo { const char* name; double def; ParamKind kind; };
    constexpr int kMaxParams = 26;

    inline const std::vector<ParamInfo>& paramInfo (Voice v)
    {
        static const std::vector<ParamInfo> t[(int) Voice::NumVoices] = {
            // Kick808
            { { "f0", 49.5, PK_Freq }, { "b1", 2.5, PK_None }, { "tb1", 0.003, PK_None }, { "b2", 0.5, PK_None }, { "tb2", 0.012, PK_None },
              { "b3", 0.08, PK_None }, { "tau", 0.3, PK_Time }, { "early", 0.45, PK_None }, { "click", 0.3, PK_None }, { "lp", 1500.0, PK_Bright },
              { "drive", 1.0, PK_None }, { "asym", 0.0, PK_None } },
            // Snare808
            { { "f1", 172.0, PK_Freq }, { "f2", 340.0, PK_Freq }, { "tau1", 0.05, PK_Time }, { "tau2", 0.03, PK_Time }, { "mix2", 0.3, PK_None },
              { "bend", 0.0, PK_None }, { "tb", 0.004, PK_None }, { "n_lvl", 0.5, PK_None }, { "n_hp", 1800.0, PK_Bright }, { "n_lp", 9000.0, PK_Bright },
              { "n_tau1", 0.03, PK_Time }, { "n_tau2", 0.12, PK_Time }, { "n_mix", 0.6, PK_None }, { "drive", 1.0, PK_None } },
            // Tom808
            { { "f0", 90.0, PK_Freq }, { "tau", 0.12, PK_Time }, { "bend", 0.04, PK_None }, { "tb", 0.03, PK_None }, { "asym", 0.1, PK_None },
              { "drive", 0.8, PK_None }, { "h_lp", 4000.0, PK_Bright }, { "n_lvl", 0.0, PK_None }, { "n_lp", 500.0, PK_Bright }, { "n_tau", 0.08, PK_Time },
              { "click", 0.05, PK_None } },
            // Metal808
            { { "scale", 1.0, PK_Freq }, { "f1", 3440.0, PK_Freq }, { "q1", 3.5, PK_None }, { "f2", 7100.0, PK_Freq }, { "q2", 3.0, PK_None },
              { "mix", 0.6, PK_None }, { "tau1", 0.012, PK_Time }, { "tau2", 0.15, PK_Time }, { "emix", 0.6, PK_None }, { "hp", 6000.0, PK_Bright },
              { "drive", 4.0, PK_None }, { "atk", 0.0005, PK_None }, { "hi_decay", 1.0, PK_None }, { "f3", 10000.0, PK_Freq }, { "q3", 1.0, PK_None },
              { "mix3", 0.0, PK_None }, { "gate", 0.0, PK_Time } },
            // Cowbell808
            { { "f1", 558.0, PK_Freq }, { "f2", 823.0, PK_Freq }, { "bp", 2640.0, PK_Freq }, { "q", 1.1, PK_None }, { "hp_mix", 0.25, PK_None },
              { "lp", 6000.0, PK_Bright }, { "tau1", 0.014, PK_Time }, { "tau2", 0.12, PK_Time }, { "emix", 0.6, PK_None }, { "drive", 1.0, PK_None } },
            // Res808
            { { "f1", 2422.0, PK_Freq }, { "tau1", 0.006, PK_Time }, { "f2", 500.0, PK_Freq }, { "tau2", 0.004, PK_Time }, { "mix2", 0.0, PK_None },
              { "f3", 1700.0, PK_Freq }, { "tau3", 0.003, PK_Time }, { "mix3", 0.0, PK_None }, { "hp", 200.0, PK_Bright }, { "click", 0.0, PK_None },
              { "drive", 1.0, PK_None } },
            // Clap808
            { { "f", 1050.0, PK_Freq }, { "q", 2.1, PK_None }, { "hp", 500.0, PK_Bright }, { "gap", 0.0103, PK_None }, { "n_burst", 3.0, PK_None },
              { "b_tau", 0.0032, PK_None }, { "tail_lvl", 0.75, PK_None }, { "tail_tau1", 0.022, PK_Time }, { "tail_tau2", 0.12, PK_Time },
              { "tail_mix", 0.6, PK_None }, { "drive", 4.0, PK_None } },
            // Maracas808
            { { "hp", 5200.0, PK_Bright }, { "atk", 0.0035, PK_None }, { "tau", 0.016, PK_Time } },
            // Kick909
            { { "f_end", 50.0, PK_Freq }, { "d1", 2.4, PK_None }, { "tp1", 0.03, PK_None }, { "d2", 0.4, PK_None }, { "tp2", 0.1, PK_None },
              { "shape", 0.3, PK_None }, { "hold", 0.01, PK_Time }, { "tau", 0.12, PK_Time }, { "click", 0.4, PK_None }, { "c_lp", 5500.0, PK_Bright },
              { "c_tau", 0.0022, PK_None }, { "pulse", 0.6, PK_None }, { "mid", 0.0, PK_None }, { "drive", 1.5, PK_None } },
            // Snare909
            { { "f1", 182.0, PK_Freq }, { "f2", 331.0, PK_Freq }, { "bend", 0.5, PK_None }, { "tb", 0.007, PK_None }, { "tau1", 0.055, PK_Time },
              { "tau2", 0.032, PK_Time }, { "mix2", 0.4, PK_None }, { "shape", 0.25, PK_None }, { "nl_lvl", 0.3, PK_None }, { "nl_lp", 5000.0, PK_Bright },
              { "nl_tau", 0.06, PK_Time }, { "nh_lvl", 0.5, PK_None }, { "nh_hp", 2400.0, PK_Bright }, { "nh_tau", 0.12, PK_Time }, { "drive", 1.5, PK_None } },
            // Tom909
            { { "f0", 100.0, PK_Freq }, { "bend", 0.4, PK_None }, { "tb", 0.05, PK_None }, { "tau", 0.12, PK_Time }, { "r2", 1.5, PK_None },
              { "m2", 0.2, PK_None }, { "tau2", 0.05, PK_Time }, { "shape", 0.3, PK_None }, { "n_lvl", 0.3, PK_None }, { "n_lp", 3000.0, PK_Bright },
              { "n_tau", 0.03, PK_Time }, { "drive", 1.3, PK_None } },
            // Rim909
            { { "f1", 224.0, PK_Freq }, { "tau1", 0.01, PK_Time }, { "f2", 490.0, PK_Freq }, { "tau2", 0.006, PK_Time }, { "m2", 0.6, PK_None },
              { "f3", 2500.0, PK_Freq }, { "tau3", 0.002, PK_Time }, { "m3", 0.4, PK_None }, { "hp", 150.0, PK_Bright }, { "drive", 2.0, PK_None } },
            // Clap909
            { { "f", 1250.0, PK_Freq }, { "q", 1.4, PK_None }, { "hp", 500.0, PK_Bright }, { "gap", 0.011, PK_None }, { "n_burst", 3.0, PK_None },
              { "b_tau", 0.0032, PK_None }, { "tail_lvl", 0.75, PK_None }, { "tail_tau1", 0.022, PK_Time }, { "tail_tau2", 0.12, PK_Time },
              { "tail_mix", 0.6, PK_None }, { "drive", 4.0, PK_None } },
            // Cymbal909 (the measured spectrum is in the preset)
            { { "partial_lvl", 1.0, PK_None } },
            // Perc
            { { "f1", 80.0, PK_Freq }, { "strike", 0.3, PK_None }, { "tilt", 0.5, PK_None }, { "tau0", 0.5, PK_Time }, { "f_damp", 400.0, PK_Bright },
              { "p_damp", 1.5, PK_None }, { "glide", 0.0, PK_None }, { "t_glide", 0.1, PK_None }, { "contact_ms", 2.0, PK_None }, { "inharm", 1.0, PK_None },
              { "head2", 0.0, PK_None }, { "head2_ratio", 1.06, PK_None }, { "n_lvl", 0.0, PK_None }, { "n_f", 3000.0, PK_Bright }, { "n_q", 0.7, PK_None },
              { "n_tau1", 0.01, PK_Time }, { "n_tau2", 0.1, PK_Time }, { "n_mix", 0.7, PK_None }, { "n_atk", 0.0005, PK_None }, { "n_follow", 0.0, PK_None },
              { "room", 0.0, PK_None }, { "t60", 2.0, PK_None }, { "room_damp", 4000.0, PK_None }, { "drive", 0.0, PK_None }, { "hp", 20.0, PK_None } },
            // Orch (the physical kit: kick, snare, tom, conga, cymbal)
            { { "kind", 0.0, PK_None }, { "variant", 0.0, PK_None }, { "f01", 52.0, PK_Freq }, { "damp", 0.5, PK_None }, { "hard", 0.5, PK_None },
              { "strike", 0.0, PK_None }, { "tscale", 1.0, PK_Time }, { "bright", 1.0, PK_Bright } }
        };
        return t[(int) v];
    }

    inline int paramIndex (Voice v, const char* name)
    {
        const auto& p = paramInfo (v);
        for (size_t i = 0; i < p.size(); ++i)
            if (std::string (p[i].name) == name) return (int) i;
        return -1;
    }

    //==========================================================================
    // A preset: a fitted sound
    //==========================================================================
    struct CymBand { float lvl, tau1, tau2, mix, atk; };
    struct CymPartial { float f, rel; };
    constexpr int kCymBands = 28;
    inline double cymEdge (int i) { return 30.0 * std::pow (20000.0 / 30.0, (double) i / 28.0); }   // geomspace(30, 20000, 29)

    enum class Family { Membrane = 0, Loaded, Bar, Plate };

    struct Preset
    {
        const char* key;          // "808/BD5050", "large/03"
        const char* name;         // what the menus show
        Voice voice;
        float nS;                 // length of the render, seconds
        int seed;
        float values[kMaxParams];  // in paramInfo order
        // Cymbal909
        const CymBand* bands { nullptr };
        const CymPartial* partials { nullptr };
        int numPartials { 0 };
        // Perc
        Family family { Family::Membrane };
        int nModes { 0 };
        const float* ratios { nullptr };     // mode ratios (membrane / plate / loaded before inharm)
        const float* mvals { nullptr };      // membrane: the Bessel order of each mode
        const float* arand { nullptr };      // bar / plate: the amplitude randoms
        const float* jitter { nullptr };     // the mode frequency jitter (normal), its length:
        int numJitter { 0 };
        float fittedDistance { 0.0f };       // dB, how close the fit got (information)
    };

    //==========================================================================
    // What the knobs of a pad / lane do to a preset
    //==========================================================================
    struct Mods
    {
        double pitch { 1.0 };     // frequency ratio (Tune)
        double decay { 1.0 };     // time ratio (Decay)
        double tone { 0.5 };      // 0..1, 0.5 = as fitted (Tone)
        double extra[3] { 0.0, 0.0, 0.0 };   // the voice's own physical knobs, -1..1 around the fitted value (see physicalKnobs)
    };

    // three knobs per voice type that move the model itself, named for what
    // they do; each scales a model parameter around its fitted value
    struct PhysKnob { const char* label; const char* param; double range; bool logScale; };
    inline const std::array<PhysKnob, 3>& physicalKnobs (Voice v)
    {
        static const std::array<PhysKnob, 3> t[(int) Voice::NumVoices] = {
            std::array<PhysKnob, 3> { { { "Sweep", "b1", 4.0, true }, { "Click", "click", 1.0, false }, { "Drive", "drive", 3.0, true } } },
            std::array<PhysKnob, 3> { { { "Snappy", "n_lvl", 3.0, true }, { "Tone Mix", "mix2", 0.4, false }, { "Bend", "bend", 0.3, false } } },
            std::array<PhysKnob, 3> { { { "Bend", "bend", 0.25, false }, { "Drive", "drive", 3.0, true }, { "Noise", "n_lvl", 0.8, false } } },
            std::array<PhysKnob, 3> { { { "Band Mix", "mix", 0.5, false }, { "Drive", "drive", 3.0, true }, { "High Decay", "hi_decay", 3.0, true } } },
            std::array<PhysKnob, 3> { { { "Band", "bp", 2.0, true }, { "Body", "hp_mix", 0.5, false }, { "Drive", "drive", 3.0, true } } },
            std::array<PhysKnob, 3> { { { "Ring", "tau1", 3.0, true }, { "Second", "mix2", 1.0, false }, { "Drive", "drive", 3.0, true } } },
            std::array<PhysKnob, 3> { { { "Spread", "gap", 1.6, true }, { "Tail", "tail_lvl", 0.6, false }, { "Band", "f", 2.0, true } } },
            std::array<PhysKnob, 3> { { { "Attack", "atk", 3.0, true }, { "Shake", "tau", 2.0, true }, { "Band", "hp", 1.6, true } } },
            std::array<PhysKnob, 3> { { { "Sweep", "d1", 3.0, true }, { "Attack", "click", 1.0, false }, { "Drive", "drive", 3.0, true } } },
            std::array<PhysKnob, 3> { { { "Snappy", "nh_lvl", 3.0, true }, { "Tone Mix", "mix2", 0.4, false }, { "Bend", "bend", 0.6, false } } },
            std::array<PhysKnob, 3> { { { "Bend", "bend", 0.6, false }, { "Noise", "n_lvl", 3.0, true }, { "Overtone", "m2", 0.5, false } } },
            std::array<PhysKnob, 3> { { { "Ring", "tau1", 3.0, true }, { "Second", "m2", 1.0, false }, { "Drive", "drive", 3.0, true } } },
            std::array<PhysKnob, 3> { { { "Spread", "gap", 1.6, true }, { "Tail", "tail_lvl", 0.6, false }, { "Band", "f", 2.0, true } } },
            std::array<PhysKnob, 3> { { { "Ping", "partial_lvl", 4.0, true }, { "-", "", 0.0, false }, { "-", "", 0.0, false } } },
            std::array<PhysKnob, 3> { { { "Strike", "strike", 0.4, false }, { "Ring", "tau0", 3.0, true }, { "Room", "room", 1.0, false } } },
            std::array<PhysKnob, 3> { { { "Damping", "damp", 0.45, false }, { "Snap", "hard", 0.6, false }, { "Strike", "strike", 0.3, false } } }
        };
        return t[(int) v];
    }

    // the preset's values with the knobs applied
    inline std::array<double, kMaxParams> applyMods (const Preset& p, const Mods& m, double& nS)
    {
        const auto& info = paramInfo (p.voice);
        std::array<double, kMaxParams> v {};
        for (size_t i = 0; i < info.size(); ++i) v[i] = p.values[i];
        const double bright = std::pow (2.0, (m.tone - 0.5) * 3.0);    // +-1.5 octaves
        for (size_t i = 0; i < info.size(); ++i)
        {
            switch (info[i].kind)
            {
                case PK_Freq:   v[i] *= m.pitch; break;
                case PK_Time:   v[i] *= m.decay; break;
                case PK_Bright: v[i] *= m.pitch * bright; break;
                default: break;
            }
        }
        const auto& kn = physicalKnobs (p.voice);
        for (int k = 0; k < 3; ++k)
        {
            if (kn[(size_t) k].param[0] == 0 || m.extra[k] == 0.0) continue;
            const int idx = paramIndex (p.voice, kn[(size_t) k].param);
            if (idx < 0) continue;
            if (kn[(size_t) k].logScale) v[(size_t) idx] *= std::pow (kn[(size_t) k].range, m.extra[k]);
            else v[(size_t) idx] = v[(size_t) idx] + m.extra[k] * kn[(size_t) k].range;
            if (v[(size_t) idx] < 0.0 && std::string (kn[(size_t) k].param) != "asym") v[(size_t) idx] = 0.0;
        }
        nS = (double) p.nS * std::max (0.25, m.decay);
        return v;
    }

    //==========================================================================
    // The voices (each the reference function, line by line)
    //==========================================================================
    struct Stereo { Buf l, r; bool stereo { false }; };

    namespace detail
    {
        inline Buf kick808 (const double* v, double nS, double vel, double sr)
        {
            const double f0 = v[0], b1 = v[1], tb1 = v[2], b2 = v[3], tb2 = v[4], b3 = v[5], tau = v[6], early = v[7], click = v[8], lp = v[9], drive = v[10], asym = v[11];
            const int n = lenOf (nS, sr);
            Buf p = pulse (n, sr);
            Buf e = p; scale (e, 0.055);
            Buf body = ringer (e,
                [&] (size_t i) { const double t = (double) i / sr; return f0 * (1.0 + b1 * std::exp (-t / tb1) + b2 * std::exp (-t / tb2) + b3 * vel * std::exp (-t / tau)); },
                [&] (size_t i) { const double t = (double) i / sr; return std::max (tau * (1.0 - early * std::exp (-t / 0.02)), 1.0e-3); }, sr);
            scale (body, 1.0 / (maxAbs (body) + 1e-12));
            Buf c = onepoleHPCopy (p, 600.0, sr);
            for (size_t i = 0; i < body.size(); ++i) body[i] += click * c[i];
            onepoleLP (body, lp, sr);
            shapeDrive (body, drive, asym);
            scale (body, vel);
            return body;
        }

        inline Buf snare808 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double f1 = v[0], f2 = v[1], tau1 = v[2], tau2 = v[3], mix2 = v[4], bend = v[5], tb = v[6], nLvl = v[7], nHp = v[8], nLp = v[9],
                         nTau1 = v[10], nTau2 = v[11], nMix = v[12], drive = v[13];
            const int n = lenOf (nS, sr);
            Buf p = pulse (n, sr, 1.0, 4000.0);
            auto bf = [&] (size_t i) { return 1.0 + bend * std::exp (-((double) i / sr) / tb); };
            Buf a = ringer (p, [&] (size_t i) { return f1 * bf (i); }, [&] (size_t) { return tau1; }, sr);
            scale (a, 1.0 / (maxAbs (a) + 1e-12));
            Buf b = ringer (p, [&] (size_t i) { return f2 * bf (i); }, [&] (size_t) { return tau2; }, sr);
            scale (b, 1.0 / (maxAbs (b) + 1e-12));
            Buf nz = noise (n, seed);
            svfConst (nz, nHp, 0.7, sr, 2);
            svfConst (nz, nLp, 0.7, sr, 0);
            for (int i = 0; i < n; ++i) nz[(size_t) i] *= env2 ((double) i / sr, nTau1, nTau2, nMix);
            scale (nz, 1.0 / (maxAbs (nz) + 1e-12));
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i) y[(size_t) i] = (1.0 - mix2) * a[(size_t) i] + mix2 * b[(size_t) i] + nLvl * nz[(size_t) i];
            shapeDrive (y, drive, 0.0);
            scale (y, vel);
            return y;
        }

        inline Buf tom808 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double f0 = v[0], tau = v[1], bend = v[2], tb = v[3], asym = v[4], drive = v[5], hLp = v[6], nLvl = v[7], nLp = v[8], nTau = v[9], click = v[10];
            const int n = lenOf (nS, sr);
            Buf p = pulse (n, sr);
            Buf y = ringer (p, [&] (size_t i) { return f0 * (1.0 + bend * std::exp (-((double) i / sr) / tb)); }, [&] (size_t) { return tau; }, sr);
            scale (y, 1.0 / (maxAbs (y) + 1e-12));
            shapeDrive (y, drive, asym);
            Buf c = onepoleHPCopy (p, 800.0, sr);
            for (size_t i = 0; i < y.size(); ++i) y[i] += click * c[i];
            onepoleLP (y, hLp, sr);
            if (nLvl > 0.0)
            {
                Buf nz = noise (n, seed);
                onepoleLP (nz, nLp, sr);
                onepoleLP (nz, nLp * 2.0, sr);
                const double sd = stdOf (nz, (size_t) (0.05 * sr)) + 1e-9;
                for (int i = 0; i < n; ++i) y[(size_t) i] += nLvl * nz[(size_t) i] * std::exp (-((double) i / sr) / nTau) / sd * 0.3;
            }
            scale (y, vel);
            return y;
        }

        inline Buf six (int n, double scaleF, double sr, uint64_t seed)
        {
            static const double f[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            Rng r (seed);
            Buf y ((size_t) n, 0.0);
            for (double fr : f)
            {
                const Buf s = blepSquare (fr * scaleF, sr, n, r.uni());
                for (int i = 0; i < n; ++i) y[(size_t) i] += s[(size_t) i];
            }
            scale (y, 1.0 / 6.0);
            return y;
        }

        inline Buf metal808 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double sc = v[0], f1 = v[1], q1 = v[2], f2 = v[3], q2 = v[4], mix = v[5], tau1 = v[6], tau2 = v[7], emix = v[8], hp = v[9],
                         drive = v[10], atk = v[11], hiDecay = v[12], f3 = v[13], q3 = v[14], mix3 = v[15], gate = v[16];
            const int n = lenOf (nS, sr);
            const Buf m = six (n, sc, sr, seed);
            Buf lo = svfCopy (m, f1, q1, sr, 1), hi = svfCopy (m, f2, q2, sr, 1);
            const double slo = stdOf (lo) + 1e-9, shi = stdOf (hi) + 1e-9;
            Buf h3; double s3 = 1.0;
            if (mix3 > 0.0) { h3 = svfCopy (m, f3, q3, sr, 1); s3 = stdOf (h3) + 1e-9; }
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                const double a = 1.0 - std::exp (-t / std::max (atk, 1e-5));
                const double el = env2 (t, tau1, tau2, emix) * a, eh = env2 (t, tau1 * hiDecay, tau2 * hiDecay, emix) * a;
                double s = (1.0 - mix) * lo[(size_t) i] / slo * el + mix * hi[(size_t) i] / shi * eh;
                if (mix3 > 0.0) s += mix3 * h3[(size_t) i] / s3 * eh;
                if (gate > 0.0) s *= std::clamp (1.0 - (t - gate) / 0.006, 0.0, 1.0);
                y[(size_t) i] = s;
            }
            svfConst (y, hp, 0.7, sr, 2);
            const double norm = 1.0 / (maxAbs (y) + 1e-12);
            for (auto& s : y) s = std::tanh (drive * s * norm) * vel;
            return y;
        }

        inline Buf cowbell808 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double f1 = v[0], f2 = v[1], bp = v[2], q = v[3], hpMix = v[4], lp = v[5], tau1 = v[6], tau2 = v[7], emix = v[8], drive = v[9];
            const int n = lenOf (nS, sr);
            Rng r (seed);
            Buf m = blepSquare (f1, sr, n, r.uni());
            const Buf m2 = blepSquare (f2, sr, n, r.uni());
            for (int i = 0; i < n; ++i) m[(size_t) i] += m2[(size_t) i];
            Buf y = svfCopy (m, bp, q, sr, 1);
            const Buf h = onepoleHPCopy (m, 400.0, sr);
            for (int i = 0; i < n; ++i) y[(size_t) i] += hpMix * h[(size_t) i];
            onepoleLP (y, lp, sr);
            for (int i = 0; i < n; ++i) y[(size_t) i] *= env2 ((double) i / sr, tau1, tau2, emix);
            const double norm = 1.0 / (maxAbs (y) + 1e-12);
            for (auto& s : y) s = std::tanh (drive * s * norm) * vel;
            return y;
        }

        inline Buf res808 (double nS, double f1, double tau1, double f2, double tau2, double mix2, double f3, double tau3, double mix3,
                           double hp, double click, double drive, double vel, double sr)
        {
            const int n = lenOf (nS, sr);
            const Buf p = pulse (n, sr, 0.5, 8000.0);
            Buf y ((size_t) n, 0.0);
            const double fs[3] = { f1, f2, f3 }, ts[3] = { tau1, tau2, tau3 }, gs[3] = { 1.0, mix2, mix3 };
            for (int k = 0; k < 3; ++k)
            {
                if (gs[k] <= 0.0) continue;
                Buf r = ringerConst (p, fs[k], ts[k], sr);
                const double g = gs[k] / (maxAbs (r) + 1e-12);
                for (int i = 0; i < n; ++i) y[(size_t) i] += g * r[(size_t) i];
            }
            const Buf c = onepoleHPCopy (p, 2000.0, sr);
            for (int i = 0; i < n; ++i) y[(size_t) i] += click * c[(size_t) i];
            onepoleHP (y, hp, sr);
            const double norm = 1.0 / (maxAbs (y) + 1e-12);
            for (auto& s : y) s = std::tanh (drive * s * norm) * vel;
            return y;
        }

        inline Buf clap (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double f = v[0], q = v[1], hp = v[2], gap = v[3], nBurst = v[4], bTau = v[5], tailLvl = v[6], tt1 = v[7], tt2 = v[8], tmix = v[9], drive = v[10];
            const int n = lenOf (nS, sr);
            Buf nz = noise (n, seed);
            svfConst (nz, f, q, sr, 1);
            onepoleHP (nz, hp, sr);
            const int nb = (int) std::lround (nBurst);
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                double env = 0.0;
                for (int k = 0; k < nb; ++k)
                {
                    const double tk = t - k * gap;
                    if (tk >= 0.0 && tk < gap) env += std::exp (-tk / bTau);
                }
                const double t3 = t - nb * gap;
                if (t3 >= 0.0) env += tailLvl * env2 (t3, tt1, tt2, tmix);
                y[(size_t) i] = nz[(size_t) i] * env;
            }
            const double norm = 1.0 / (maxAbs (y) + 1e-12);
            for (auto& s : y) s = std::tanh (drive * s * norm) * vel;
            return y;
        }

        inline Buf maracas808 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const int n = lenOf (nS, sr);
            Buf y = noise (n, seed);
            svfConst (y, v[0], 0.8, sr, 2);
            for (int i = 0; i < n; ++i) { const double t = (double) i / sr; y[(size_t) i] *= (1.0 - std::exp (-t / v[1])) * std::exp (-t / v[2]); }
            scale (y, vel / (maxAbs (y) + 1e-12));
            return y;
        }

        inline Buf kick909 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double fEnd = v[0], d1 = v[1], tp1 = v[2], d2 = v[3], tp2 = v[4], shp = v[5], hold = v[6], tau = v[7], click = v[8], cLp = v[9],
                         cTau = v[10], pls = v[11], mid = v[12], drive = v[13];
            const int n = lenOf (nS, sr);
            Buf osc = sweepOsc (n, [&] (size_t i) { const double t = (double) i / sr; return fEnd * (1.0 + d1 * std::exp (-t / tp1) + d2 * std::exp (-t / tp2)); }, sr, shp);
            Buf cl = noise (n, seed);
            onepoleLP (cl, cLp, sr);
            const Buf pu = pulse (n, sr, 0.4, 6000.0);
            for (int i = 0; i < n; ++i) cl[(size_t) i] = cl[(size_t) i] * std::exp (-((double) i / sr) / cTau) + pls * pu[(size_t) i];
            onepoleHP (cl, 250.0, sr);
            const int k = std::max (1, (int) (0.0003 * sr));
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                const double env = t < hold ? 1.0 : std::exp (-(t - hold) / tau);
                const double a = i < k ? (k > 1 ? (double) i / (double) (k - 1) : 1.0) : 1.0;
                y[(size_t) i] = osc[(size_t) i] * env * a + click * cl[(size_t) i];
            }
            if (mid > 0.0)
            {
                const Buf b = svfCopy (y, 180.0, 1.2, sr, 1);
                for (int i = 0; i < n; ++i) y[(size_t) i] += mid * b[(size_t) i];
            }
            const double norm = 1.0 / std::tanh (drive);
            for (auto& s : y) s = std::tanh (drive * s) * norm * vel;
            return y;
        }

        inline Buf snare909 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double f1 = v[0], f2 = v[1], bend = v[2], tb = v[3], tau1 = v[4], tau2 = v[5], mix2 = v[6], shp = v[7], nlLvl = v[8], nlLp = v[9],
                         nlTau = v[10], nhLvl = v[11], nhHp = v[12], nhTau = v[13], drive = v[14];
            const int n = lenOf (nS, sr);
            auto bf = [&] (size_t i) { return 1.0 + bend * std::exp (-((double) i / sr) / tb); };
            const Buf o1 = sweepOsc (n, [&] (size_t i) { return f1 * bf (i); }, sr, shp);
            const Buf o2 = sweepOsc (n, [&] (size_t i) { return f2 * bf (i); }, sr, shp);
            const Buf nz = noise (n, seed);
            Buf nlo = svfCopy (nz, nlLp, 0.7, sr, 0), nhi = svfCopy (nz, nhHp, 0.9, sr, 2);
            for (int i = 0; i < n; ++i) { const double t = (double) i / sr; nlo[(size_t) i] *= std::exp (-t / nlTau); nhi[(size_t) i] *= std::exp (-t / nhTau); }
            const double slo = stdOf (nlo, 2000) * 3.0 + 1e-9, shi = stdOf (nhi, 2000) * 3.0 + 1e-9;
            const int k = std::max (1, (int) (0.0003 * sr));
            Buf y ((size_t) n);
            const double norm = 1.0 / std::tanh (drive);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                const double a = i < k ? (k > 1 ? (double) i / (double) (k - 1) : 1.0) : 1.0;
                const double s = ((1.0 - mix2) * o1[(size_t) i] * std::exp (-t / tau1) + mix2 * o2[(size_t) i] * std::exp (-t / tau2)
                                  + nlLvl * nlo[(size_t) i] / slo + nhLvl * nhi[(size_t) i] / shi) * a;
                y[(size_t) i] = std::tanh (drive * s) * norm * vel;
            }
            return y;
        }

        inline Buf tom909 (const double* v, double nS, double vel, double sr, uint64_t seed)
        {
            const double f0 = v[0], bend = v[1], tb = v[2], tau = v[3], r2 = v[4], m2 = v[5], tau2 = v[6], shp = v[7], nLvl = v[8], nLp = v[9], nTau = v[10], drive = v[11];
            const int n = lenOf (nS, sr);
            auto bf = [&] (size_t i) { return 1.0 + bend * std::exp (-((double) i / sr) / tb); };
            const Buf a = sweepOsc (n, [&] (size_t i) { return f0 * bf (i); }, sr, shp);
            const Buf b = sweepOsc (n, [&] (size_t i) { return f0 * r2 * bf (i); }, sr, shp);
            Buf nz = noise (n, seed);
            onepoleLP (nz, nLp, sr);
            const double sd = stdOf (nz) * 3.0 + 1e-9;
            const double norm = 1.0 / std::tanh (drive);
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                const double s = a[(size_t) i] * std::exp (-t / tau) + m2 * b[(size_t) i] * std::exp (-t / tau2) + nLvl * nz[(size_t) i] / sd * std::exp (-t / nTau);
                y[(size_t) i] = std::tanh (drive * s) * norm * vel;
            }
            return y;
        }

        inline Buf cymbal909 (const Preset& p, double partialLvl, double nS, double decay, double tone, double tune, double vel, double sr, uint64_t seed)
        {
            const int n = lenOf (nS, sr);
            const Buf nz = noise (n, seed);
            Buf y ((size_t) n, 0.0);
            for (int b = 0; b < kCymBands; ++b)
            {
                const auto& bd = p.bands[b];
                const double lo = cymEdge (b), hi = cymEdge (b + 1);
                if (bd.lvl < -110.0f || lo > sr * 0.47) continue;
                Buf band = nz;
                sosFilter (band, butterBandpass2 (lo, std::min (hi, sr * 0.48), sr));
                const double sd = stdOf (band) + 1e-12;
                const double c = std::sqrt (lo * hi);
                const double tilt = std::pow (10.0, tone * std::log2 (c / 3000.0) * 3.0 / 20.0);
                const double g = std::pow (10.0, bd.lvl / 20.0) * tilt / sd;
                const double atk = std::max (bd.atk * 0.3, 1.0e-4);
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) i / sr;
                    y[(size_t) i] += g * band[(size_t) i] * env2 (t, bd.tau1 * decay, bd.tau2 * decay, bd.mix) * (1.0 - std::exp (-t / atk));
                }
            }
            Rng r (seed + 977);
            const double base = stdOf (y, (size_t) (0.05 * sr)) + 1e-12;
            for (int k = 0; k < p.numPartials; ++k)
            {
                const auto& pt = p.partials[k];
                int bi = 0;
                while (bi < kCymBands - 1 && cymEdge (bi + 1) <= pt.f) ++bi;
                const double tau2 = p.bands[bi].tau2 * decay;
                const double amp = partialLvl * base * 0.05 * std::pow (10.0, pt.rel / 20.0);
                const double ph = r.uni() * 6.3;
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) i / sr;
                    y[(size_t) i] += amp * std::sin (kTwoPi * pt.f * t + ph) * std::exp (-t / tau2);
                }
            }
            scale (y, 0.95 / (maxAbs (y) + 1e-12));
            const double rate = 32000.0 * sr / 48000.0;     // the ROM rate, kept relative to the output's
            Buf z = romplerCrush (y, sr, rate, 6, tune);
            svfConst (z, rate * 0.45, 0.7, sr, 0);
            scale (z, vel);
            return z;
        }

        // Bessel J_m(x) (the integral form; exact enough for strike amplitudes)
        inline double besselJ (int m, double x)
        {
            constexpr int N = 400;
            double s = 0.0;
            for (int i = 0; i <= N; ++i)
            {
                const double tau = kPi * (double) i / N;
                const double w = (i == 0 || i == N) ? 0.5 : 1.0;
                s += w * std::cos (m * tau - x * std::sin (tau));
            }
            return s / N;
        }

        //======================================================================
        // The physical kit (aqpm/drums.py kit_*): a circular membrane's Bessel
        // modes with tension glide, the stick or beater's contact pulse, snare
        // wires thrown by the head, modal + noise cymbals.
        //======================================================================
        inline double besselZero (int m, int k)
        {
            static const double z[9][5] = {
                { 2.4048255577, 5.5200781103, 8.6537279129, 11.7915344390, 14.9309177085 },
                { 3.8317059702, 7.0155866698, 10.1734681351, 13.3236919363, 16.4706300509 },
                { 5.1356223018, 8.4172441404, 11.6198411721, 14.7959517824, 17.9598194950 },
                { 6.3801618959, 9.7610231300, 13.0152007217, 16.2234661603, 19.4094152264 },
                { 7.5883424345, 11.0647094885, 14.3725366716, 17.6159660498, 20.8269329570 },
                { 8.7714838160, 12.3386041975, 15.7001740797, 18.9801338752, 22.2177998966 },
                { 9.9361095242, 13.5892901705, 17.0038196678, 20.3207892136, 23.5860844356 },
                { 11.0863700192, 14.8212687270, 18.2875828325, 21.6415410198, 24.9349278877 },
                { 12.2250922640, 16.0377741909, 19.5545364310, 22.9451731319, 26.2668146412 } };
            return z[m][k];
        }

        inline void membraneModes (double f01, int nModes, double strikeR, double air, std::vector<double>& f, std::vector<double>& a)
        {
            struct Row { double z, amp; };
            std::vector<Row> rows;
            for (int m = 0; m < 9; ++m)
                for (int k = 0; k < 5; ++k)
                {
                    const double z = besselZero (m, k);
                    const double norm = std::abs (besselJ (m + 1, z)) + 1.0e-9;
                    rows.push_back ({ z, std::abs (besselJ (m, z * strikeR)) / norm });
                }
            std::sort (rows.begin(), rows.end(), [] (const Row& x, const Row& y) { return x.z < y.z; });
            rows.resize ((size_t) std::min<int> (nModes, (int) rows.size()));
            const double z0 = rows[0].z;
            std::vector<double> ratio (rows.size());
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const double r = rows[i].z / z0;
                ratio[i] = r * (1.0 + air * std::exp (-(r - 1.0) * 1.5));
            }
            const double r0 = ratio[0];
            double amax = 1.0e-12;
            for (auto& r : rows) amax = std::max (amax, r.amp);
            f.resize (rows.size()); a.resize (rows.size());
            for (size_t i = 0; i < rows.size(); ++i) { f[i] = f01 * ratio[i] / r0; a[i] = rows[i].amp / amax; }
        }

        // the stick / beater's contact force: harder hits are shorter (brighter); unit area
        inline Buf contact (int n, double ms, double vel, double sr, double bright = 1.0)
        {
            const double dur = ms * 0.001 * (1.35 - 0.6 * vel) / std::max (0.1, bright);
            const int k = std::max (2, (int) (dur * sr));
            Buf p ((size_t) n, 0.0);
            for (int i = 0; i < std::min (k, n); ++i) p[(size_t) i] = (0.5 - 0.5 * std::cos (kTwoPi * i / k)) * (2.0 / k);
            return p;
        }

        inline Buf drumBody (int n, double f01, double vel, double strikeR, double tau0, double fDamp, double pw, double glide, double air,
                             double contactMs, int nModes, uint64_t seed, double tilt, double sr, double bright)
        {
            std::vector<double> f, a;
            membraneModes (f01, nModes, std::clamp (strikeR, 0.0, 0.95), air, f, a);
            Rng rng (seed);
            std::vector<double> taus (f.size());
            for (size_t i = 0; i < f.size(); ++i)
            {
                a[i] *= std::pow (f[i] / f[0], -tilt);
                // a normal deviate (Box-Muller): no head is perfectly even
                const double u1 = std::max (1.0e-12, rng.uni()), u2 = rng.uni();
                f[i] *= 1.0 + 0.004 * std::sqrt (-2.0 * std::log (u1)) * std::cos (kTwoPi * u2);
                taus[i] = tau0 / (1.0 + std::pow (f[i] / fDamp, pw));
            }
            Buf fs ((size_t) n);
            for (int i = 0; i < n; ++i) fs[(size_t) i] = std::sqrt (1.0 + glide * vel * vel * std::exp (-(i / sr) / (0.35 * tau0)));
            Buf y = modalBank (contact (n, contactMs, vel, sr, bright), f, taus, a, fs, sr);
            onepoleHP (y, 0.5 * f01, sr);
            onepoleHP (y, 0.5 * f01, sr);
            return y;
        }

        inline void normalizeTo (Buf& y, double peak) { const double m = maxAbs (y); if (m > 0.0) scale (y, peak / m); }

        inline Buf orchKick (double f01, double vel, double damp, double beater, double strike, double ts, double bright, double sr, uint64_t seed)
        {
            const int n = (int) (sr * 0.9 * std::max (1.0, ts));
            const double tau0 = (0.30 - 0.20 * std::clamp (damp, -0.4, 1.4)) * ts;
            Buf body = drumBody (n, f01, vel, 0.12 + strike, tau0, 190.0 * bright, 1.4, 0.5, 0.25, 3.2 - 1.6 * beater, 22, seed, 1.1, sr, bright);
            Buf front = drumBody (n, f01 * 0.93, vel, 0.2 + strike, tau0 * 1.5, 110.0 * bright, 2.0, 0.5, 0.2, 9.0, 6, seed + 1, 0.0, sr, bright);
            Buf slap = noise (n, seed + 7);
            svfConst (slap, (2600.0 + 2500.0 * beater) * bright, 1.0, sr, 1);
            const double bm = maxAbs (body);
            const int atk = std::max (1, (int) (0.0003 * sr));
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                const double att = i < atk ? 0.5 - 0.5 * std::cos (kPi * i / atk) : 1.0;
                y[(size_t) i] = body[(size_t) i] + 0.5 * front[(size_t) i] + (0.10 + 0.30 * beater) * vel * slap[(size_t) i] * std::exp (-t / 0.004) * att * bm;
            }
            normalizeTo (y, 0.95);
            scale (y, 0.35 + 0.65 * std::pow (vel, 1.3));
            return y;
        }

        inline Buf orchSnare (double f01, double vel, double wires, bool rim, double strike, double damp, double ts, double bright, double sr, uint64_t seed)
        {
            const int n = (int) (sr * 0.5 * std::max (1.0, ts));
            Buf body = drumBody (n, f01, vel, (rim ? 0.75 : 0.25) + strike, 0.075 * ts * (1.0 - 0.6 * damp), 330.0 * bright, 1.4, 0.22, 0.15, 0.8, 28, seed,
                                 rim ? 0.5 : 1.3, sr, bright);
            {
                Buf shell = modalBank (contact (n, 0.5, vel, sr, bright), { 412.0, 655.0, 938.0, 1410.0 }, { 0.05 * ts, 0.04 * ts, 0.03 * ts, 0.02 * ts },
                                       { 0.5, 0.4, 0.3, 0.25 }, Buf ((size_t) n, 1.0), sr);
                for (int i = 0; i < n; ++i) body[(size_t) i] += 0.12 * shell[(size_t) i];
            }
            Buf bn = body; normalizeTo (bn, 1.0);
            // the wires: a follower on the head, chopped by the head's swing
            Buf fol ((size_t) n);
            double z = 0.0;
            const double aUp = 1.0 - std::exp (-1.0 / (0.0025 * sr)), aDn = 1.0 - std::exp (-1.0 / (0.036 * ts * sr));
            for (int i = 0; i < n; ++i) { const double ab = std::abs (bn[(size_t) i]); z += (ab > z ? aUp : aDn) * (ab - z); fol[(size_t) i] = z; }
            Buf lp = bn; onepoleLP (lp, 600.0, sr);
            Buf nz = noise (n, seed + 5);
            Buf w1 = svfCopy (nz, 3200.0 * bright, 0.4, sr, 1), w2 = svfCopy (nz, 6800.0 * bright, 0.8, sr, 1), w3 = svfCopy (nz, 1900.0 * bright, 0.9, sr, 1);
            Buf w ((size_t) n);
            for (int i = 0; i < n; ++i) w[(size_t) i] = w1[(size_t) i] + 0.8 * w2[(size_t) i] + 0.5 * w3[(size_t) i];
            svfConst (w, 9500.0 * bright, 0.7, sr, 0);
            onepoleHP (w, 1000.0, sr);
            for (int i = 0; i < n; ++i)
            {
                const double chop = 0.62 + 0.38 * std::tanh (6.0 * lp[(size_t) i] / (fol[(size_t) i] + 0.02));
                w[(size_t) i] *= std::pow (fol[(size_t) i], 0.8) * chop;
            }
            // the stick: a sub-millisecond crack and its wood
            Buf stick = contact (n, 0.35, vel, sr, bright);
            onepoleHP (stick, 1500.0, sr);
            scale (stick, sr * 0.004);
            {
                Buf wood = modalBank (contact (n, 0.3, vel, sr, bright), { 2150.0, 3900.0, 5700.0 }, { 0.004, 0.003, 0.002 }, { 1.0, 0.7, 0.5 }, Buf ((size_t) n, 1.0), sr);
                for (int i = 0; i < n; ++i) stick[(size_t) i] += 0.6 * wood[(size_t) i];
            }
            const double sm = maxAbs (stick) + 1.0e-12;
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i)
                y[(size_t) i] = bn[(size_t) i] + 0.95 * wires * (0.4 + 0.6 * vel) * w[(size_t) i] + 0.06 * stick[(size_t) i] / sm;
            if (rim)
            {
                Buf hoop = modalBank (contact (n, 0.25, vel, sr, bright), { 540.0, 905.0, 1730.0, 3150.0, 4400.0 }, { 0.035, 0.03, 0.022, 0.012, 0.008 },
                                      { 1.0, 0.9, 0.8, 0.5, 0.4 }, Buf ((size_t) n, 1.0), sr);
                const double hm = maxAbs (hoop) + 1.0e-12;
                for (int i = 0; i < n; ++i) y[(size_t) i] += 1.1 * hoop[(size_t) i] / hm;
            }
            normalizeTo (y, 0.95);
            scale (y, 0.3 + 0.7 * std::pow (vel, 1.2));
            return y;
        }

        inline Buf orchTom (double f01, double vel, double damp, double hard, double strike, double ts, double bright, double sr, uint64_t seed)
        {
            const int n = (int) (sr * 1.4 * std::max (1.0, ts));
            const double tau0 = (0.36 - 0.22 * std::clamp (damp, -0.4, 1.4)) * ts;
            Buf body = drumBody (n, f01, vel, 0.4 + strike, tau0, 260.0 * bright, 1.5, 0.45, 0.12, 0.9, 26, seed, 0.9, sr, bright);
            Buf reso = drumBody (n, f01 * 1.06, vel, 0.5 + strike, tau0 * 0.8, 200.0 * bright, 1.8, 0.3, 0.1, 3.0, 8, seed + 1, 0.0, sr, bright);
            for (int i = 0; i < n; ++i) body[(size_t) i] += 0.35 * reso[(size_t) i];
            normalizeTo (body, 1.0);
            Buf st = noise (n, seed + 9);
            svfConst (st, 1900.0 * bright, 0.35, sr, 1);
            const int atk = std::max (1, (int) (0.0002 * sr));
            for (int i = 0; i < n; ++i)
            {
                const double att = i < atk ? 0.5 - 0.5 * std::cos (kPi * i / atk) : 1.0;
                body[(size_t) i] += (0.25 + 0.5 * (hard - 0.5)) * vel * st[(size_t) i] * std::exp (-(i / sr) / 0.004) * att;
            }
            normalizeTo (body, 0.95);
            scale (body, 0.3 + 0.7 * std::pow (vel, 1.2));
            return body;
        }

        inline Buf orchConga (double f01, double vel, bool slap, double hard, double strike, double damp, double ts, double bright, double sr, uint64_t seed)
        {
            const int n = (int) (sr * 0.7 * std::max (1.0, ts));
            const double tau0 = (slap ? 0.05 : 0.13) * ts * (1.0 - 0.6 * (damp - 0.5));
            Buf body = drumBody (n, f01, vel, (slap ? 0.8 : 0.55) + strike, tau0, 500.0 * bright, 1.2, 0.15, 0.3, slap ? 0.8 : 2.2, 20, seed, 0.0, sr, bright);
            normalizeTo (body, 1.0);
            Buf hand = noise (n, seed + 3);
            svfConst (hand, (slap ? 3200.0 : 1700.0) * bright, 1.2, sr, 1);
            const double hl = (slap ? 0.5 : 0.10) * (1.0 + 1.5 * (hard - 0.5)), ht = slap ? 0.012 : 0.006;
            for (int i = 0; i < n; ++i) body[(size_t) i] += hl * hand[(size_t) i] * std::exp (-(i / sr) / ht);
            normalizeTo (body, 0.95);
            scale (body, 0.3 + 0.7 * std::pow (vel, 1.2));
            return body;
        }

        // kind 0 crash, 1 ride, 2 closed hat, 3 open hat
        inline Buf orchCymbal (int kind, double vel, double damp, double hard, double ts, double bright, double sr, uint64_t seed)
        {
            struct C { double fLo; int nm; double btau[4]; double bloom, nshare, length; };
            static const C table[4] = { { 310.0, 110, { 0.75, 0.55, 0.30, 0.16 }, 14.0, 0.55, 3.0 },
                                        { 420.0, 110, { 0.95, 0.60, 0.22, 0.10 }, 3.0, 0.30, 3.0 },
                                        { 520.0, 70, { 0.020, 0.024, 0.022, 0.016 }, 0.5, 0.60, 0.25 },
                                        { 520.0, 90, { 0.20, 0.26, 0.22, 0.13 }, 2.0, 0.60, 1.3 } };
            const auto& c = table[std::clamp (kind, 0, 3)];
            const double tsc = ts * (1.0 - 0.6 * (damp - 0.5));
            const int n = (int) (sr * c.length * std::max (1.0, tsc));
            Rng rng (seed);
            std::vector<double> f ((size_t) c.nm), taus ((size_t) c.nm), amps ((size_t) c.nm);
            const double hiF = std::min (15500.0, sr * 0.45);
            for (int j = 0; j < c.nm; ++j)
            {
                const double u = rng.uni();
                f[(size_t) j] = std::pow (std::pow (c.fLo, 0.6) + u * (std::pow (hiF, 0.6) - std::pow (c.fLo, 0.6)), 1.0 / 0.6) * bright;
                const int band = f[(size_t) j] < 1500.0 ? 0 : (f[(size_t) j] < 5000.0 ? 1 : (f[(size_t) j] < 10000.0 ? 2 : 3));
                taus[(size_t) j] = c.btau[band] * tsc * (0.7 + 0.6 * rng.uni());
                amps[(size_t) j] = std::pow (f[(size_t) j] / 3000.0, 0.35) * (0.5 + rng.uni()) / (1.0 + std::pow (f[(size_t) j] / 9000.0, 4.0));
            }
            const Buf exc = contact (n, 0.35, vel, sr, 1.0 + (hard - 0.5));
            Buf metal = modalBank (exc, f, taus, amps, Buf ((size_t) n, 1.0), sr);
            Buf nz = noise (n, seed + 1);
            Buf wash ((size_t) n, 0.0);
            const double centers[4] = { 800.0, 2800.0, 6500.0, 11500.0 }, gains[4] = { 0.3, 0.8, 1.0, 0.22 };
            for (int b = 0; b < 4; ++b)
            {
                Buf bandN = svfCopy (nz, std::min (centers[b] * bright, sr * 0.45), 0.9, sr, 1);
                const double bt = c.btau[b] * tsc, bl = 0.0004 + 0.001 * c.bloom * vel * (b + 1) / 4.0;
                for (int i = 0; i < n; ++i)
                {
                    const double t = i / sr;
                    wash[(size_t) i] += gains[b] * bandN[(size_t) i] * std::exp (-t / bt) * (1.0 - std::exp (-t / bl));
                }
            }
            const double mm = maxAbs (metal) + 1.0e-12, wm = maxAbs (wash) + 1.0e-12;
            Buf y ((size_t) n);
            for (int i = 0; i < n; ++i) y[(size_t) i] = (1.0 - c.nshare) * metal[(size_t) i] / mm + c.nshare * wash[(size_t) i] / wm;
            if (kind == 1)
            {
                Buf bell = modalBank (exc, { 690.0 * bright, 1105.0 * bright, 1630.0 * bright, 2480.0 * bright }, { 0.7 * tsc, 0.5 * tsc, 0.4 * tsc, 0.25 * tsc },
                                      { 1.0, 0.8, 0.6, 0.4 }, Buf ((size_t) n, 1.0), sr);
                const double bm = maxAbs (bell) + 1.0e-12;
                for (int i = 0; i < n; ++i) y[(size_t) i] += 0.5 * bell[(size_t) i] / bm;
            }
            onepoleHP (y, 250.0, sr);
            normalizeTo (y, 0.9);
            scale (y, 0.3 + 0.7 * std::pow (vel, 1.2));
            return y;
        }

        inline Buf orch (const double* v, double vel, double sr, uint64_t seed)
        {
            const int kind = (int) std::lround (v[0]), variant = (int) std::lround (v[1]);
            const double f01 = v[2], damp = v[3], hard = v[4], strike = v[5], ts = std::max (0.1, v[6]), bright = std::max (0.1, v[7]);
            switch (kind)
            {
                case 0:  return orchKick (f01, vel, damp, hard, strike, ts, bright, sr, seed);
                case 1:  return orchSnare (f01, vel, std::max (0.0, hard * 2.0), variant == 1, strike, damp, ts, bright, sr, seed);
                case 2:  return orchTom (f01, vel, damp, hard, strike, ts, bright, sr, seed);
                case 3:  return orchConga (f01, vel, variant == 1, hard, strike, damp, ts, bright, sr, seed);
                default: return orchCymbal (variant, vel, damp, hard, ts, bright * f01 / 1000.0, sr, seed);
            }
        }

        inline Stereo perc (const Preset& p, const double* v, double nS, double vel, double sr, uint64_t seed, bool wantStereo)
        {
            const double f1 = v[0], strike = v[1], tilt = v[2], tau0 = v[3], fDamp = v[4], pDamp = v[5], glide = v[6], tGlide = v[7],
                         contactMs = v[8], inharm = v[9], head2 = v[10], head2Ratio = v[11], nLvl = v[12], nF = v[13], nQ = v[14],
                         nTau1 = v[15], nTau2 = v[16], nMix = v[17], nAtk = v[18], nFollow = v[19], room = v[20], t60 = v[21],
                         roomDamp = v[22], drive = v[23], hp = v[24];
            const int n = lenOf (nS, sr);
            // the mode set
            std::vector<double> f, a, mv;
            for (int k = 0; k < p.nModes; ++k)
            {
                double r = p.ratios[k];
                if (p.family == Family::Loaded) r = 1.0 + (r - 1.0) * (1.0 + 0.02 * inharm * k / p.nModes);
                if (p.family == Family::Bar && inharm != 1.0) r = std::pow (r, std::sqrt (std::max (0.0, inharm)));
                const double fr = f1 * r;
                if (fr >= 0.45 * 48000.0 * (sr / 48000.0) && fr >= 0.45 * sr) continue;
                f.push_back (fr);
                mv.push_back (p.mvals != nullptr ? p.mvals[k] : (double) k);
            }
            const size_t L = f.size();
            constexpr double j01 = 2.404825557695773;
            for (size_t k = 0; k < L; ++k)
            {
                double amp;
                if (p.family == Family::Membrane)
                    amp = std::abs (besselJ ((int) mv[k], f[k] / f1 * j01 * strike)) + 0.02;
                else if (p.family == Family::Loaded)
                    amp = 1.0 / (1.0 + std::pow ((double) k * strike, 2.0)) + 0.02;
                else
                {
                    amp = 0.4 + (p.arand != nullptr ? p.arand[k] : 0.5);
                    amp *= std::abs (std::sin (kPi * (double) (k + 1) * (0.07 + strike * 0.5))) + 0.1;
                }
                a.push_back (amp * std::pow (f[k] / f1, -tilt));
            }
            std::vector<double> taus (L);
            for (size_t k = 0; k < L; ++k)
            {
                taus[k] = std::max (tau0 / (1.0 + std::pow (f[k] / fDamp, pDamp)), 2e-3);
                if (p.jitter != nullptr && (int) k < p.numJitter) f[k] *= 1.0 + 0.003 * p.jitter[k];
            }
            // the contact
            Buf exc ((size_t) n, 0.0);
            {
                const int k = std::min (n, std::max (2, (int) (contactMs * (1.3 - 0.5 * vel) * 1e-3 * sr)));
                for (int i = 0; i < k; ++i) exc[(size_t) i] = (0.5 - 0.5 * std::cos (kTwoPi * i / k)) * (2.0 / k);
            }
            Buf fscale ((size_t) n);
            for (int i = 0; i < n; ++i) fscale[(size_t) i] = std::sqrt (1.0 + glide * vel * std::exp (-((double) i / sr) / tGlide));
            // glide changes the rotation every sample; quantised to 1/4096 so the modes are rebuilt only when it moves
            for (auto& s : fscale) s = std::round (s * 4096.0) / 4096.0;
            Buf y = modalBank (exc, f, taus, a, fscale, sr);
            if (head2 > 0.0)
            {
                const size_t m8 = std::min<size_t> (8, L);
                std::vector<double> f2 (f.begin(), f.begin() + (long) m8), t2 (taus.begin(), taus.begin() + (long) m8), a2 (a.begin(), a.begin() + (long) m8);
                for (size_t k = 0; k < m8; ++k) { f2[k] *= head2Ratio; t2[k] = std::max (t2[k] * 1.3, 2e-3); }
                const Buf y2 = modalBank (exc, f2, t2, a2, fscale, sr);
                for (int i = 0; i < n; ++i) y[(size_t) i] += head2 * y2[(size_t) i];
            }
            scale (y, 1.0 / (maxAbs (y) + 1e-12));
            if (nLvl > 0.0)
            {
                Buf nz = noise (n, seed + 7);
                svfConst (nz, nF, nQ, sr, 1);
                Buf fol;
                double folMax = 1.0;
                if (nFollow > 0.0)
                {
                    fol.resize ((size_t) n);
                    for (int i = 0; i < n; ++i) fol[(size_t) i] = std::abs (y[(size_t) i]);
                    onepoleLP (fol, 30.0, sr);
                    folMax = maxAbs (fol) + 1e-12;
                }
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) i / sr;
                    double env = (nMix * std::exp (-t / nTau1) + (1.0 - nMix) * std::exp (-t / nTau2)) * (1.0 - std::exp (-t / std::max (nAtk, 1e-5)));
                    if (nFollow > 0.0) env = (1.0 - nFollow) * env + nFollow * fol[(size_t) i] / folMax;
                    nz[(size_t) i] *= env;
                }
                const double g = nLvl / (maxAbs (nz) + 1e-12);
                for (int i = 0; i < n; ++i) y[(size_t) i] += g * nz[(size_t) i];
            }
            if (drive > 0.0) { const double norm = 1.0 / std::tanh (drive); for (auto& s : y) s = std::tanh (drive * s) * norm; }
            if (hp > 25.0) onepoleHP (y, hp, sr);
            scale (y, 1.0 / (maxAbs (y) + 1e-12));
            Stereo out;
            if (room > 0.0)
            {
                Buf wl, wr;
                hall (y, y, wl, wr, sr, t60, roomDamp, 12.0);
                const double wm = std::max (maxAbs (wl), maxAbs (wr)) + 1e-12;
                out.l = y; out.r = y;
                for (int i = 0; i < n; ++i) { out.l[(size_t) i] += room * wl[(size_t) i] / wm; out.r[(size_t) i] += room * wr[(size_t) i] / wm; }
                out.stereo = wantStereo;
                if (! wantStereo)
                    for (int i = 0; i < n; ++i) out.l[(size_t) i] = 0.5 * (out.l[(size_t) i] + out.r[(size_t) i]);
            }
            else { out.l = y; out.stereo = false; }
            scale (out.l, vel);
            if (out.stereo) scale (out.r, vel); else out.r.clear();
            return out;
        }
    }

    //==========================================================================
    // Render one hit of a preset (the whole of it). Mono unless the preset has
    // a room and stereo is asked for. Message / worker thread only.
    //==========================================================================
    inline Stereo render (const Preset& p, const Mods& m, double sr, double vel, uint64_t seed, bool stereo = true)
    {
        double nS = p.nS;
        const auto vals = applyMods (p, m, nS);
        const double* v = vals.data();
        const uint64_t s = seed * 7919ull + (uint64_t) p.seed;
        Stereo out;
        switch (p.voice)
        {
            case Voice::Kick808:    out.l = detail::kick808 (v, nS, vel, sr); break;
            case Voice::Snare808:   out.l = detail::snare808 (v, nS, vel, sr, s); break;
            case Voice::Tom808:     out.l = detail::tom808 (v, nS, vel, sr, s); break;
            case Voice::Metal808:   out.l = detail::metal808 (v, nS, vel, sr, s); break;
            case Voice::Cowbell808: out.l = detail::cowbell808 (v, nS, vel, sr, s); break;
            case Voice::Res808:     out.l = detail::res808 (nS, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], vel, sr); break;
            case Voice::Clap808:    out.l = detail::clap (v, nS, vel, sr, s); break;
            case Voice::Maracas808: out.l = detail::maracas808 (v, nS, vel, sr, s); break;
            case Voice::Kick909:    out.l = detail::kick909 (v, nS, vel, sr, s); break;
            case Voice::Snare909:   out.l = detail::snare909 (v, nS, vel, sr, s); break;
            case Voice::Tom909:     out.l = detail::tom909 (v, nS, vel, sr, s); break;
            case Voice::Rim909:     out.l = detail::res808 (nS, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], 0.2, v[9], vel, sr); break;
            case Voice::Clap909:    out.l = detail::clap (v, nS, vel, sr, s); break;
            case Voice::Cymbal909:
            {
                const double bright = (m.tone - 0.5) * 2.0;   // the measured spectrum tilted +-3 dB / oct
                out.l = detail::cymbal909 (p, v[0], nS, m.decay, bright, m.pitch, vel, sr, s);
                break;
            }
            case Voice::Perc:       out = detail::perc (p, v, nS, vel, sr, s, stereo); break;
            case Voice::Orch:       out.l = detail::orch (v, vel, sr, s); break;
            case Voice::NumVoices:  break;
        }
        for (auto* b : { &out.l, &out.r })
            for (auto& x : *b)
                if (! std::isfinite (x)) x = 0.0;
        return out;
    }
}
