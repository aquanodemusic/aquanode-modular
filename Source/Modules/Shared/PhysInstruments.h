#pragma once
//==============================================================================
// Physically modelled instruments, real time: bowed strings (violin, viola,
// cello, double bass, sections, pizzicato), the clarinet (v1 and v2 bore) and
// the guitar (acoustic and electric). Ports of the Python reference models
// (aqpm/strings.py, clarinet.py, guitar.py) that were fitted to recordings;
// the fitted data is in PhysInstrumentData.h.
//
// Plain C++ (no JUCE). Every voice runs one sample at a time. What the
// reference worked out per sample but changes slowly (a loop filter's phase
// delay under vibrato, the bore length) is worked out every 16 samples here.
// Bodies, pickups' shared amp and cabinet run once on the sum of the notes,
// as on the instruments (one body, one amp for all strings).
//==============================================================================
#include "PhysInstrumentData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace aquanode::physinst
{
    constexpr double kTwoPi = 6.283185307179586476925;
    constexpr double kPi = 3.141592653589793238463;

    struct Rng
    {
        explicit Rng (uint64_t seed = 1) { reseed (seed); }
        void reseed (uint64_t seed) { s = seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull; if (s == 0) s = 1; next(); }
        uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
        double uni() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
        double sym() { return uni() * 2.0 - 1.0; }
        double normal() { double u1 = std::max (1e-12, uni()), u2 = uni(); return std::sqrt (-2.0 * std::log (u1)) * std::cos (kTwoPi * u2); }
        uint64_t s { 1 };
    };

    // a slow random wander in -1..1: straight lines between random points `rate` per second
    struct Wander
    {
        void start (Rng& r, double rateHz, double sr) { step = std::max (1.0, sr / std::max (0.05, rateHz)); a = r.sym(); b = r.sym(); pos = 0.0; }
        double next (Rng& r)
        {
            const double v = a + (b - a) * (pos / step);
            if (++pos >= step) { pos = 0.0; a = b; b = r.sym(); }
            return v;
        }
        double a { 0 }, b { 0 }, pos { 0 }, step { 1 };
    };

    inline double smoothstep01 (double x) { x = std::clamp (x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }

    // 4-point Lagrange read from a ring buffer (the reference's _lagrange_read)
    template <int Size>
    inline double lagrange (const double* buf, int w, double delay)
    {
        const int di = (int) delay; const double fr = delay - di;
        const int i1 = (w - di) & (Size - 1);
        const double xm1 = buf[(i1 + 1) & (Size - 1)], x0 = buf[i1], x1 = buf[(i1 - 1) & (Size - 1)], x2 = buf[(i1 - 2) & (Size - 1)];
        const double c1 = x1 - xm1 / 3.0 - x0 / 2.0 - x2 / 6.0;
        const double c2 = (x1 + xm1) / 2.0 - x0;
        const double c3 = (x2 - xm1) / 6.0 + (x0 - x1) / 2.0;
        return ((c3 * fr + c2) * fr + c1) * fr + x0;
    }

    // a TPT state-variable filter, fixed cutoff (mode 0 LP, 1 BP, 2 HP)
    struct Svf
    {
        void set (double fc, double q, double sr, int m)
        {
            mode = m;
            const double f = std::clamp (fc, 10.0, sr * 0.49);
            k = 1.0 / std::max (0.05, q);
            const double g = std::tan (kPi * f / sr);
            a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        double process (double x)
        {
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
            return mode == 0 ? v2 : (mode == 1 ? k * v1 : x - k * v1 - v2);
        }
        void reset() { ic1 = ic2 = 0.0; }
        double a1 { 0 }, a2 { 0 }, a3 { 0 }, k { 1 }, ic1 { 0 }, ic2 { 0 };
        int mode { 0 };
    };

    struct OnePole
    {
        void set (double fc, double sr) { a = 1.0 - std::exp (-kTwoPi * fc / sr); }
        double lp (double x) { z += a * (x - z); return z; }
        double hp (double x) { z += a * (x - z); return x - z; }
        void reset() { z = 0.0; }
        double a { 1 }, z { 0 };
    };

    //==========================================================================
    // A modal body: constant-peak-gain band-passes in parallel (struct of
    // arrays, so the compiler can run four modes at once)
    //==========================================================================
    struct ModalBody
    {
        // maxModes > 0: only the strongest modes (a section's bodies blur the detail anyway)
        void build (const float* f, const float* q, const float* g, int n, double sr, double freqScale = 1.0, double jitter = 0.0, uint64_t seed = 3,
                    int maxModes = 0)
        {
            b0.clear(); a1.clear(); a2.clear(); z1.clear(); z2.clear();
            Rng r (seed);
            float keepAbove = -1.0f;
            if (maxModes > 0 && maxModes < n)
            {
                std::vector<float> mags ((size_t) n);
                for (int i = 0; i < n; ++i) mags[(size_t) i] = std::abs (g[i]);
                std::nth_element (mags.begin(), mags.begin() + (n - maxModes), mags.end());
                keepAbove = mags[(size_t) (n - maxModes)];
            }
            for (int i = 0; i < n; ++i)
            {
                if (keepAbove >= 0.0f && std::abs (g[i]) < keepAbove) { r.sym(); if (jitter > 0.0 && i >= 4) r.uni(); continue; }
                const double fr = f[i] * freqScale * (1.0 + jitter * r.sym());
                double gain = g[i];
                if (jitter > 0.0 && i >= 4 && r.uni() < 0.5) gain = -gain;
                if (fr >= 0.45 * sr) continue;
                const double w = kTwoPi * fr / sr, al = std::sin (w) / (2.0 * q[i]), a0 = 1.0 + al;
                b0.push_back ((float) (gain * al / a0));
                a1.push_back ((float) (-2.0 * std::cos (w) / a0));
                a2.push_back ((float) ((1.0 - al) / a0));
            }
            z1.assign (b0.size(), 0.0f); z2.assign (b0.size(), 0.0f);
        }
        // y = sum over modes of b0 (x - x[n-2]) filtered by 1 / (1 + a1 z^-1 + a2 z^-2), transposed DF II
        float process (float x)
        {
            float acc = 0.0f;
            const size_t n = b0.size();
            float* p1 = z1.data(); float* p2 = z2.data();
            const float* pb = b0.data(); const float* pa1 = a1.data(); const float* pa2 = a2.data();
            for (size_t j = 0; j < n; ++j)
            {
                const float o = pb[j] * x + p1[j];
                p1[j] = -pa1[j] * o + p2[j];
                p2[j] = -pb[j] * x - pa2[j] * o;
                acc += o;
            }
            return acc;
        }
        void reset() { std::fill (z1.begin(), z1.end(), 0.0f); std::fill (z2.begin(), z2.end(), 0.0f); }
        std::vector<float> b0, a1, a2, z1, z2;
    };

    //==========================================================================
    // BOWED STRINGS
    //==========================================================================
    enum class Bowed { Violin = 0, Viola, Cello, Bass };

    struct InstrumentInfo { float scale, fLow, beta, bright, tauFree; const float *bf, *bq, *bg; int bn; };
    inline InstrumentInfo instrumentInfo (Bowed b)
    {
        switch (b)
        {
            case Bowed::Viola: return { inst_viola[0], inst_viola[1], inst_viola[2], inst_viola[3], inst_viola[4], body_viola_f, body_viola_q, body_viola_g, body_viola_n };
            case Bowed::Cello: return { inst_cello[0], inst_cello[1], inst_cello[2], inst_cello[3], inst_cello[4], body_cello_f, body_cello_q, body_cello_g, body_cello_n };
            case Bowed::Bass:  return { inst_bass[0], inst_bass[1], inst_bass[2], inst_bass[3], inst_bass[4], body_bass_f, body_bass_q, body_bass_g, body_bass_n };
            default:           return { inst_violin[0], inst_violin[1], inst_violin[2], inst_violin[3], inst_violin[4], body_violin_f, body_violin_q, body_violin_g, body_violin_n };
        }
    }

    // what a player does with the bow, set per note (and live for the continuous ones)
    struct BowSettings
    {
        double pressure { 0.5 };     // 0 airy .. 1 pressed
        double position { -1.0 };    // fraction from the bridge, < 0: the instrument's own
        double attack { 0.08 }, release { 0.10 };
        double vibCents { 11.0 }, vibRate { 5.3 }, vibDelay { 0.25 };
        double stringQ { 650.0 };
        double brightness { 1.0 };   // scales the loop brightness
        double glideTime { 0.06 };
        // pizzicato, Studio voicings (< 0: the classic pluck)
        double pluckT60 { -1.0 }, pluckBrightHz { -1.0 }, pluckMs { -1.0 }, pluckBeta { -1.0 };
        double pluckBuzz { 0.0 };   // the string snapping against the fingerboard: a bright rattle at the start
    };

    // one string under the bow (or plucked)
    class BowedString
    {
    public:
        static constexpr int kSize = 4096;

        void prepare (double sampleRate) { sr = sampleRate; reset(); }
        void reset()
        {
            std::fill (std::begin (nut), std::end (nut), 0.0);
            std::fill (std::begin (bri), std::end (bri), 0.0);
            w = 0; lp = 0.0; stick = true; nzs = 0.0; qlp = 0.0; active = false; held = false;
        }
        bool isActive() const { return active; }
        bool isHeld() const { return held; }
        int note() const { return midiNote; }

        // start a bowed note (pluck = false) or a pizzicato (pluck = true)
        void start (int noteNumber, double freqHz, double dynamic, const InstrumentInfo& inst, const BowSettings& s, bool pluck,
                    uint64_t seed, double fromFreq, double startDelay, double detuneCents)
        {
            const bool wasActive = active;
            midiNote = noteNumber; target = freqHz * std::pow (2.0, detuneCents / 1200.0);
            glideFrom = (fromFreq > 0.0 && wasActive) ? fromFreq : target;
            info = inst; set = s; isPluck = pluck; dyn = std::clamp (dynamic, 0.0, 1.0);
            rng.reseed (seed);
            if (! wasActive) { std::fill (std::begin (nut), std::end (nut), 0.0); std::fill (std::begin (bri), std::end (bri), 0.0); lp = 0.0; stick = true; nzs = qlp = 0.0; }
            beta = set.position > 0.0 ? set.position : (double) info.beta;
            t = -startDelay; tOff = -1.0; held = true; active = true;
            wVibRate.start (rng, 1.5, sr); wVibDepth.start (rng, 1.2, sr); wDrift.start (rng, 0.7, sr);
            wSpeed.start (rng, 3.0, sr); wForce.start (rng, 2.0, sr);
            vibPhase = rng.uni() * kTwoPi;
            bite = 0.6 + 1.6 * std::clamp (200.0 / target - 1.0, 0.0, 1.5) / 1.5;
            biteTau = std::max (0.10, 8.0 / target);
            if (pluck)
            {
                tauFree = (info.fLow >= 130.0 ? 0.22 : 0.75) * std::pow (info.fLow / target, 0.35) * 1.6;
                brightHz = info.bright * 0.8 * set.brightness;
                pluckLen = std::max (3, (int) ((0.0016 - 0.0008 * dyn) * sr));
                pluckAmp = (0.25 + 0.75 * dyn) * 0.5;
                pluckPos = 0;
                beta = 0.2;
                // the Studio pizzicati: their own decay, finger and point on the string
                if (set.pluckT60 > 0.0) tauFree = set.pluckT60 / 6.9078;
                if (set.pluckBrightHz > 0.0) brightHz = std::min (set.pluckBrightHz * set.brightness, 0.45 * sr);
                if (set.pluckMs > 0.0) pluckLen = std::max (2, (int) (set.pluckMs * (1.25 - 0.5 * dyn) * 1.0e-3 * sr));
                if (set.pluckBeta > 0.0) beta = set.pluckBeta;
                buzzHp = 0.0; buzzX = 0.0; buzzLp = 0.0;
            }
            else
            {
                tauFree = set.stringQ / (kPi * target);
                brightHz = std::min (18000.0, info.bright * (0.40 + 0.65 * dyn) * (1.0 + 0.3 * set.pressure) * (1.0 + target / 1200.0)) * set.brightness;
            }
            aLoop = 1.0 - std::exp (-kTwoPi * brightHz / sr);
            ctl = 0; fNow = -1.0; lastF = -1.0;
        }

        void release() { if (held) { held = false; tOff = std::max (0.0, t); } }
        void setDynamic (double d) { dyn = std::clamp (d, 0.0, 1.0); }      // live (breath control)
        void setPressure (double p) { set.pressure = std::clamp (p, 0.0, 1.0); }
        void setPitch (double hz) { target = hz; }

        double process (double bendRatio, double vibScale)
        {
            if (! active) return 0.0;
            if (t < 0.0) { t += 1.0 / sr; return 0.0; }
            // pitch: glide, delayed and wandering vibrato, slow drift (the reference's bow_note)
            // (the wanders and the vibrato's phase move every sample; the pitch they make is
            // worked out every 4th, and the loop's delay and loss from it every 16th)
            const double rate = set.vibRate * (1.0 + 0.06 * wVibRate.next (rng));
            vibPhase += kTwoPi * rate / sr;
            if (vibPhase > kTwoPi) vibPhase -= kTwoPi;
            const double wDepth = wVibDepth.next (rng), wDr = wDrift.next (rng);
            if ((ctl & 3) == 0 || fNow <= 0.0)
            {
                double f = target;
                if (glideFrom != target) f = target * std::pow (glideFrom / target, std::exp (-t / (set.glideTime / 3.0)));
                const double onset = std::pow (std::clamp ((t - set.vibDelay) / 0.35, 0.0, 1.0), 1.5);
                const double depth = set.vibCents * vibScale * onset * (1.0 + 0.25 * wDepth);
                const double cents = isPluck ? 6.0 * dyn * std::exp (-t / 0.03) : depth * std::sin (vibPhase) + 2.5 * wDr;
                fNow = f * std::pow (2.0, cents / 1200.0) * bendRatio;
            }
            const double f = fNow;

            double vb = 0.0, K = 0.0, pl = 0.0;
            if (isPluck)
            {
                if (pluckPos < pluckLen) { pl = (0.5 - 0.5 * std::cos (kTwoPi * pluckPos / pluckLen)) * pluckAmp; ++pluckPos; }
            }
            else
            {
                const double up = smoothstep01 (t / set.attack);
                double dn = 1.0;
                if (tOff >= 0.0) dn = smoothstep01 (1.0 - (t - tOff) / set.release);
                const double speed = 0.05 + 0.33 * dyn;
                vb = speed * up * dn * (1.0 + 0.05 * wSpeed.next (rng));
                const double kmax = speed / (0.5 * beta);
                K = kmax * (0.10 + 0.55 * std::pow (set.pressure, 1.3)) * (1.0 + bite * std::exp (-t / biteTau)) * (1.0 + 0.04 * wForce.next (rng));
                if (tOff >= 0.0 && t >= tOff + set.release) K = 0.0;
            }

            // the loop filter's phase delay and the loss, every 16 samples
            if ((ctl++ & 15) == 0 || lastF < 0.0)
            {
                lastF = f;
                const double wn = kTwoPi * f / sr;
                const double lpDelay = std::atan2 ((1.0 - aLoop) * std::sin (wn), 1.0 - (1.0 - aLoop) * std::cos (wn)) / wn;
                total = sr / f - lpDelay;
                g = std::exp (-1.0 / (f * tauFree));
                qlpA = 1.0 - std::exp (-kTwoPi * 0.3 * f / sr);
            }
            const double lb = std::max (2.0, beta * total), ln = std::max (2.0, total - lb);
            const double nutOut = lagrange<kSize> (nut, w, ln);
            const double briOut = lagrange<kSize> (bri, w, lb);
            lp += aLoop * (briOut - lp);
            const double inB = -g * lp, inN = -nutOut;
            const double vh = inN + inB;
            double fp = 0.0;
            if (K > 0.0)
            {
                constexpr double muS = 0.8, muD = 0.3, v0 = 0.06, tors = 0.4, dmu = muS - muD;
                const double q = vb - vh;
                qlp += qlpA * (q - qlp);
                const double qe = q - tors / (1.0 + tors) * (q - qlp);
                const double aq = std::abs (q);
                const bool canStick = std::abs (qe) <= K * muS;
                const double A = aq - K * muD;
                const double disc = (A - v0) * (A - v0) + 4.0 * v0 * (A - K * dmu);
                const double dv = disc >= 0.0 ? 0.5 * ((A - v0) + std::sqrt (disc)) : -1.0;
                const bool canSlip = dv > 0.0;
                stick = stick ? canStick : ! canSlip;
                if (stick && ! canStick) stick = false;
                if (stick) fp = qe;
                else if (canSlip)
                {
                    nzs += 0.25 * (rng.sym() - nzs);
                    const double mu = muD + dmu * v0 / (v0 + dv);
                    fp = K * mu * (1.0 + 0.22 * nzs);
                    if (q < 0.0) fp = -fp;
                }
                else fp = q > 0.0 ? K * muS : -K * muS;
            }
            fp += pl;
            nut[w] = inB + fp;
            bri[w] = inN + fp;
            w = (w + 1) & (kSize - 1);
            t += 1.0 / sr;

            // a released (or plucked) string has rung out
            const double a = std::abs (briOut);
            level = std::max (a, level * 0.9995);
            double out = briOut;
            if (isPluck && set.pluckBuzz > 0.0 && t < 1.0)
            {
                // the rattle: noise as strong as the string swings, gone within a few tenths of a second
                const double nz = rng.sym();
                const double hp = nz - buzzX + 0.93 * buzzHp;    // ~500 Hz high-pass
                buzzX = nz; buzzHp = hp;
                buzzLp += 0.6 * (hp - buzzLp);
                out += set.pluckBuzz * level * std::exp (-t / 0.2) * std::min (1.0, t / 0.002) * buzzLp;
            }
            if (! held && (isPluck || (tOff >= 0.0 && t > tOff + set.release)) && level < 1.0e-5 && t > 0.05) active = false;
            if (! std::isfinite (out)) { reset(); return 0.0; }
            return out;
        }

    private:
        double sr { 48000.0 };
        double nut[kSize] {}, bri[kSize] {};
        int w { 0 }, ctl { 0 }, midiNote { -1 };
        double lp { 0 }, nzs { 0 }, qlp { 0 }, qlpA { 0 }, total { 100 }, g { 0.99 }, lastF { -1 }, fNow { -1 };
        bool stick { true }, active { false }, held { false }, isPluck { false };
        InstrumentInfo info {};
        BowSettings set;
        double target { 440 }, glideFrom { 440 }, dyn { 0.7 }, beta { 0.11 }, t { 0 }, tOff { -1 };
        double tauFree { 1 }, brightHz { 5000 }, aLoop { 0.5 }, bite { 0.6 }, biteTau { 0.1 }, vibPhase { 0 }, level { 0 };
        int pluckLen { 0 }, pluckPos { 0 }; double pluckAmp { 0 }, buzzHp { 0 }, buzzX { 0 }, buzzLp { 0 };
        Rng rng;
        Wander wVibRate, wVibDepth, wDrift, wSpeed, wForce;
    };

    //==========================================================================
    // CLARINET
    //==========================================================================
    struct ClarinetTables
    {
        float cutoff, vent, reflGain, zeta, zetaHi, flowMix, reedHz, reedQ, gamma1, radHz, outLp, clarionFrom, altissimoFrom, apC; int nAp;
        const float *thrHz, *thrGamma; int thrN;
        const float *tuneHz, *tuneCents; const int* tuneReg; int tuneN;
    };
    inline ClarinetTables clarinetTables (int version)
    {
        if (version == 1)
        {
            const clarV1Voicing v;
            return { v.cutoff, v.vent, v.refl_gain, v.zeta, v.zeta_hi, v.flow_mix, v.reed_hz, v.reed_q, v.gamma1, v.rad_hz, v.out_lp,
                     v.clarion_from, v.altissimo_from, 0.0f, 0, clarV1_thr_hz, clarV1_thr_gamma, clarV1_thr_n,
                     clarV1_tune_hz, clarV1_tune_cents, clarV1_tune_reg, clarV1_tune_n };
        }
        const clarV2Voicing v;
        return { v.cutoff, v.vent, v.refl_gain, v.zeta, v.zeta_hi, v.flow_mix, v.reed_hz, v.reed_q, v.gamma1, v.rad_hz, v.out_lp,
                 v.clarion_from, v.altissimo_from, v.ap_c, v.n_ap, clarV2_thr_hz, clarV2_thr_gamma, clarV2_thr_n,
                 clarV2_tune_hz, clarV2_tune_cents, clarV2_tune_reg, clarV2_tune_n };
    }
    // linear interpolation in log frequency (np.interp (log f, log xs, ys))
    inline double interpLog (double f, const float* xs, const float* ys, int n, const int* sel = nullptr, int want = 0)
    {
        double px = 0, py = 0; bool have = false;
        const double lf = std::log (f);
        for (int i = 0; i < n; ++i)
        {
            if (sel != nullptr && sel[i] != want) continue;
            const double x = std::log ((double) xs[i]), y = ys[i];
            if (! have) { if (lf <= x) return y; have = true; px = x; py = y; continue; }
            if (lf <= x) return py + (y - py) * (lf - px) / (x - px);
            px = x; py = y;
        }
        if (! have) return sel != nullptr ? interpLog (f, xs, ys, n) : 0.0;
        return py;
    }

    struct ClarinetSettings { double attack { 0.035 }, release { 0.07 }, breath { 0.07 }, vibCents { 0.0 }, vibRate { 5.0 }, glideTime { 0.04 }; };

    class ClarinetVoice
    {
    public:
        static constexpr int kSize = 4096;
        void prepare (double sampleRate, int version) { sr = sampleRate; tab = clarinetTables (version); ver = version; reset(); }
        void reset()
        {
            std::fill (std::begin (up1), std::end (up1), 0.0); std::fill (std::begin (dn1), std::end (dn1), 0.0);
            std::fill (std::begin (up2), std::end (up2), 0.0); std::fill (std::begin (dn2), std::end (dn2), 0.0);
            w = 0; z1 = z2 = z3 = z4 = 0.0; xr = vr = dpPrev = nzs = rad = 0.0; ap.fill (0.0); apy.fill (0.0);
            active = held = false; outLp.reset(); flowHp.reset();
        }
        bool isActive() const { return active; }
        bool isHeld() const { return held; }
        int note() const { return midiNote; }

        void start (int noteNumber, double freqHz, double dynamic, const ClarinetSettings& s, uint64_t seed, double fromFreq)
        {
            const bool legato = active && fromFreq > 0.0;
            midiNote = noteNumber; target = freqHz; set = s; dyn = std::clamp (dynamic, 0.0, 1.0);
            glideFrom = legato ? fromFreq : target;
            rng.reseed (seed);
            if (! legato) { reset(); t = 0.0; }
            else t = std::max (t, set.attack);     // a slur keeps the breath
            tOff = -1.0; held = true; active = true;
            wDrift.start (rng, 6.0 / 3.0, sr); wGamma.start (rng, 12.0 / 3.0, sr);
            reg = target < tab.clarionFrom ? 1 : (target < tab.altissimoFrom ? 3 : 5);
            s1 = lpBiquad (tab.cutoff, 0.522); s2 = lpBiquad (tab.cutoff, 0.806);
            ventFrac = reg == 1 ? 0.25 : 1.0 / reg;
            ventLoss = reg == 1 ? 0.0 : tab.vent;
            tuneCents = interpLog (target, tab.tuneHz, tab.tuneCents, tab.tuneN, tab.tuneReg, reg);
            thr = interpLog (target, tab.thrHz, tab.thrGamma, tab.thrN);
            reedW = kTwoPi * tab.reedHz / sr;
            radA = 1.0 - std::exp (-kTwoPi * tab.radHz / sr);
            outLp.set (tab.outLp, sr); flowHp.set (1500.0, sr);
            ctl = 0;
            setDynamic (dyn);
        }
        void release() { if (held) { held = false; tOff = t; } }
        void setDynamic (double d)
        {
            dyn = std::clamp (d, 0.0, 1.0);
            const double margin = 0.035 + 0.035 * std::clamp ((thr - 0.5) / 0.2, 0.0, 1.0);
            const double span = std::max (0.03, std::min ((double) tab.gamma1, 0.86 - thr - margin));
            gTarget = std::min (0.9, thr + margin + span * dyn);
            zeta = tab.zeta * (0.9 + 0.25 * dyn) * (1.0 + tab.zetaHi * std::clamp ((target - 450.0) / 700.0, 0.0, 1.0));
        }

        // breath01: < 0 = no breath control (the envelope shapes the note); else the breath itself
        double process (double bendRatio, double breath01)
        {
            if (! active) return 0.0;
            double f = target;
            if (glideFrom != target) f = target * std::pow (glideFrom / target, std::exp (-std::max (0.0, t - tSlur) / (set.glideTime / 3.0)));
            const double onset = std::clamp ((t - 0.3) / 0.4, 0.0, 1.0);
            f *= std::pow (2.0, (2.0 * wDrift.next (rng) + set.vibCents * onset * std::sin (kTwoPi * set.vibRate * t)) / 1200.0) * bendRatio;
            if ((ctl++ & 15) == 0)
            {
                // round trip so the reg-th resonance lands on the note (phase lag of the end filters)
                const double lag = phaseLag (s1, f) + phaseLag (s2, f) - (tab.nAp > 0 ? tab.nAp * apPhase (tab.apC, f) : 0.0);
                drt = (reg * kPi - lag) * sr / (kTwoPi * f) * std::pow (2.0, tuneCents / 1200.0);
            }
            double env;
            if (breath01 >= 0.0)
            {
                env = breath01;
                if (! held) env *= std::clamp (1.0 - (t - tOff) / set.release, 0.0, 1.0);
            }
            else
            {
                const double up = smoothstep01 (t / set.attack);
                const double dn = tOff >= 0.0 ? std::clamp (1.0 - (t - tOff) / set.release, 0.0, 1.0) : 1.0;
                env = up * dn;
            }
            double gamma = gTarget * env * (1.0 + (0.08 + 0.14 * (1.0 - dyn)) * std::exp (-t / 0.06)) * (1.0 + 0.012 * wGamma.next (rng));
            gamma = std::min (gamma, 0.93);

            // --- the kernel (the reference's clarinet_kernel, one sample)
            const double one = 0.5 * drt;
            const double d1 = std::max (2.0, ventFrac * one);
            const int di2 = (int) std::max (2.0, std::floor (one - d1));
            const double d2 = std::max (3.0, drt - 2.0 * d1 - di2);
            const double pm = lagrange<kSize> (dn1, w, d1);
            const double aL = lagrange<kSize> (up1, w, d1);
            const double aR = lagrange<kSize> (dn2, w, d2);
            const double xEnd = up2[(w - di2) & (kSize - 1)];
            const double r1 = s1.b0 * xEnd + z1; z1 = s1.b1 * xEnd - s1.a1 * r1 + z2; z2 = s1.b2 * xEnd - s1.a2 * r1;
            const double r = s2.b0 * r1 + z3;    z3 = s2.b1 * r1 - s2.a1 * r + z4;    z4 = s2.b2 * r1 - s2.a2 * r;
            double refl = -tab.reflGain * r;
            for (int k = 0; k < tab.nAp; ++k)
            {
                const double o = tab.apC * refl + ap[(size_t) k] - tab.apC * apy[(size_t) k];
                ap[(size_t) k] = refl; apy[(size_t) k] = o; refl = o;
            }
            const double uEnd = xEnd - refl;
            rad += radA * (uEnd - rad);
            const double sound = uEnd - rad;
            const double pj = (aL + aR) * (1.0 - ventLoss);
            const double acc = reedW * reedW * (dpPrev - xr) - tab.reedQ * reedW * vr;
            vr += acc; xr += vr;
            double h = 1.0 - xr;
            if (h < 0.0) { h = 0.0; if (vr > 0.0) vr = 0.0; xr = 1.0; }
            const double c = gamma - 2.0 * pm;
            const double W = zeta * h;
            const double sq = 0.5 * (-W + std::sqrt (W * W + 4.0 * std::abs (c)));
            const double dp = c >= 0.0 ? sq * sq : -sq * sq;
            double u = c >= 0.0 ? W * sq : -W * sq;
            nzs += 0.35 * (rng.sym() - nzs);
            u *= 1.0 + set.breath * (0.6 + 0.8 * (1.0 - dyn)) * nzs;
            dpPrev = dp;
            up1[w] = pm + u; dn1[w] = pj - aL; up2[w] = pj - aR; dn2[w] = refl;
            w = (w + 1) & (kSize - 1);
            t += 1.0 / sr;
            const double y = outLp.lp (sound + tab.flowMix * flowHp.hp (u));
            level = std::max (std::abs (y), level * 0.9995);
            if (! held && t > tOff + set.release + 0.05 && level < 1.0e-5) active = false;
            if (! std::isfinite (y)) { reset(); return 0.0; }
            return y;
        }
        void slurTo (int noteNumber, double freqHz, double dynamic, uint64_t seed)
        {
            ClarinetSettings s = set;
            const double from = currentPitch();
            const int fromReg = reg;
            start (noteNumber, freqHz, dynamic, s, seed, from);
            if (reg != fromReg) glideFrom = target;    // a register change cannot slide
            tSlur = t;
        }
        double currentPitch() const { return target; }

    private:
        struct BQ { double b0, b1, b2, a1, a2; };
        BQ lpBiquad (double fc, double q) const
        {
            const double w0 = kTwoPi * fc / sr, al = std::sin (w0) / (2.0 * q), c = std::cos (w0), a0 = 1.0 + al;
            return { (1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
        }
        double phaseLag (const BQ& b, double f) const
        {
            const double wv = kTwoPi * f / sr;
            const std::complex<double> z = std::polar (1.0, -wv);
            const auto H = (b.b0 + b.b1 * z + b.b2 * z * z) / (1.0 + b.a1 * z + b.a2 * z * z);
            double a = -std::arg (H);
            if (a < 0.0) a += kTwoPi;     // the lag of a low-pass is positive (unwrapped)
            return a;
        }
        double apPhase (double c, double f) const
        {
            const double wv = kTwoPi * f / sr;
            const std::complex<double> z = std::polar (1.0, -wv);
            return std::arg ((c + z) / (1.0 + c * z));
        }

        double sr { 48000 };
        int ver { 2 };
        ClarinetTables tab {};
        double up1[kSize] {}, dn1[kSize] {}, up2[kSize] {}, dn2[kSize] {};
        int w { 0 }, ctl { 0 }, reg { 1 }, midiNote { -1 };
        BQ s1 {}, s2 {};
        double z1 { 0 }, z2 { 0 }, z3 { 0 }, z4 { 0 }, xr { 0 }, vr { 0 }, dpPrev { 0 }, nzs { 0 }, rad { 0 };
        std::array<double, 4> ap {}, apy {};
        double target { 440 }, glideFrom { 440 }, dyn { 0.6 }, t { 0 }, tOff { -1 }, tSlur { 0 }, drt { 100 };
        double ventFrac { 0.25 }, ventLoss { 0 }, tuneCents { 0 }, thr { 0.4 }, gTarget { 0.5 }, zeta { 0.36 }, reedW { 0.3 }, radA { 0.1 }, level { 0 };
        bool active { false }, held { false };
        ClarinetSettings set;
        OnePole outLp, flowHp;
        Rng rng;
        Wander wDrift, wGamma;
    };

    //==========================================================================
    // GUITAR
    //==========================================================================
    struct GuitarParams
    {
        double pos { 0.18 }, pickMs { 0.6 }, t60 { 4.0 }, bright { 0.8 }, stiff { 0.08 }, detune { 1.2 }, polMix { 0.35 }, polT60 { 1.6 };
        double glide { 0.004 }, tGlide { 0.05 };
        double puPos { 0.12 }, puF { 3500.0 }, puQ { 2.0 };
        double drive { 0.0 }, asym { 0.15 }, bass { 0.0 }, mid { 0.0 }, treble { 0.0 }, cabLp { 5200.0 }, cabQ { 1.2 }, cabHp { 85.0 }, presence { 0.0 };
        double air { 1.0 }, top { 1.0 }, bodyBright { 1.0 };
        bool electric { true };
    };
    inline GuitarParams guitarParamsFrom (const GuitarVoice& v)
    {
        GuitarParams p;
        p.pos = v.pos; p.pickMs = v.pick_ms; p.t60 = v.t60; p.bright = v.bright; p.stiff = v.stiff; p.detune = v.detune; p.polMix = v.pol_mix;
        p.polT60 = v.pol_t60; p.glide = v.glide; p.tGlide = v.t_glide; p.puPos = v.pu_pos; p.puF = v.pu_f; p.puQ = v.pu_q; p.drive = v.drive;
        p.asym = v.asym; p.bass = v.bass; p.mid = v.mid; p.treble = v.treble; p.cabLp = v.cab_lp; p.cabQ = v.cab_q; p.cabHp = v.cab_hp;
        p.presence = v.presence; p.air = v.air; p.top = v.top; p.bodyBright = v.body_bright; p.electric = v.electric;
        return p;
    }

    // the loop loss from the two decay times (the reference's _loop_params)
    inline void guitarLoop (double f0, double t60, double t60hi, double mute, double sr, double& g, double& a)
    {
        t60 *= (1.0 - 0.93 * mute);
        t60hi = std::min (t60hi * (1.0 - 0.95 * mute), t60 * 0.999);
        const double w0 = kTwoPi * f0 / sr, w1 = kTwoPi * std::max (2000.0, 2.5 * f0) / sr;
        const double target = std::pow (10.0, -3.0 / f0 * (1.0 / t60hi - 1.0 / t60));
        auto mag = [] (double av, double wv) { return (1.0 - av) / std::abs (std::complex<double> (1.0, 0.0) - av * std::polar (1.0, -wv)); };
        double lo = 0.0, hi = 0.9999;
        for (int i = 0; i < 50; ++i) { const double m = 0.5 * (lo + hi); if (mag (m, w1) / mag (m, w0) > target) lo = m; else hi = m; }
        a = 0.5 * (lo + hi);
        g = std::min (std::pow (10.0, -3.0 / (t60 * f0)) / mag (a, w0), 0.99995);
    }

    // one polarisation of a plucked string
    struct StringLoop
    {
        static constexpr int kSize = 4096;
        void reset() { std::fill (std::begin (buf), std::end (buf), 0.0); w = 0; lp = 0.0; apx.fill (0.0); apy.fill (0.0); }
        void set (double f0, double t60, double t60hi, double stiff, double mute, double sr)
        {
            guitarLoop (f0, t60, t60hi, mute, sr, g, a);
            c = -stiff; nAp = stiff > 0.0 ? 2 : 0;
        }
        double process (double exc, double f, double sr, bool recompute)
        {
            if (recompute)
            {
                const double wn = kTwoPi * f / sr;
                const double pdLp = std::atan2 (a * std::sin (wn), 1.0 - a * std::cos (wn)) / wn;
                double pdAp = 0.0;
                if (nAp > 0)
                {
                    const double ph = -std::atan2 (std::sin (wn), c + std::cos (wn)) + std::atan2 (c * std::sin (wn), 1.0 + c * std::cos (wn));
                    pdAp = -nAp * ph / wn;
                }
                d = std::max (4.0, sr / f - pdLp - pdAp);
            }
            const double y = lagrange<kSize> (buf, w, d);
            lp = (1.0 - a) * y + a * lp;
            double v = g * lp;
            for (int k = 0; k < nAp; ++k) { const double o = c * v + apx[(size_t) k] - c * apy[(size_t) k]; apx[(size_t) k] = v; apy[(size_t) k] = o; v = o; }
            buf[w] = v + exc;
            w = (w + 1) & (kSize - 1);
            return y;
        }
        double buf[kSize] {};
        int w { 0 }, nAp { 0 };
        double lp { 0 }, g { 0.99 }, a { 0.5 }, c { 0 }, d { 100 };
        std::array<double, 2> apx {}, apy {};
    };

    class GuitarString
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; exc.reserve ((size_t) (sr * 0.25) + 8192); reset(); }   // (start() never allocates)
        void reset() { p1.reset(); p2.reset(); active = held = false; exc.clear(); comb.assign (4096, 0.0); combW = 0; puLp.reset(); }
        bool isActive() const { return active; }
        bool isHeld() const { return held; }
        int note() const { return midiNote; }

        void start (int noteNumber, double f0, double vel, double mute, const GuitarParams& gp, uint64_t seed, double delaySeconds)
        {
            midiNote = noteNumber; freq = f0; p = gp; velocity = std::clamp (vel, 0.0, 1.0); muted = mute;
            rng.reseed (seed);
            // the excitation: the pick's pulse, comb-filtered at the pluck point
            const int k = std::max (2, (int) (p.pickMs * (1.2 - 0.4 * velocity) * 1e-3 * sr));
            const int dp = std::max (1, (int) std::lround (p.pos * sr / f0));
            const int st = (int) (delaySeconds * sr);
            exc.assign ((size_t) (st + k + dp + 4), 0.0);
            for (int i = 0; i < k; ++i)
            {
                const double hw = 0.5 - 0.5 * std::cos (kTwoPi * (i + 1) / (k + 1));
                exc[(size_t) (st + i)] = hw * (1.0 + 0.1 * rng.normal());
            }
            for (size_t i = exc.size() - 1; i >= (size_t) dp; --i) exc[i] -= exc[i - (size_t) dp];
            for (auto& e : exc) e *= velocity;
            excPos = 0;
            p1.reset(); p2.reset();
            p1.set (f0, p.t60 * std::pow (110.0 / f0, 0.35), p.bright, p.stiff, mute, sr);
            p2.set (f0, p.t60 * std::pow (110.0 / f0, 0.35) * p.polT60, p.bright * p.polT60, p.stiff, mute, sr);
            t = -delaySeconds; held = true; active = true; level = 0.0; ctl = 0;
            combD = std::max (1, (int) std::lround (2.0 * p.puPos * sr / f0));
            std::fill (comb.begin(), comb.end(), 0.0); combW = 0;
            puLp.set (p.puF, p.puQ, sr, 0); puLp.reset();
        }
        // the hand comes down on the string when the key is let go (a guitarist mutes it)
        void release (double releaseT60)
        {
            if (! held) return;
            held = false;
            const double t60r = std::max (0.03, releaseT60);
            p1.set (freq, std::min (t60r, p.t60), std::min (t60r * 0.5, p.bright), p.stiff, 0.0, sr);
            p2.set (freq, std::min (t60r, p.t60), std::min (t60r * 0.5, p.bright), p.stiff, 0.0, sr);
        }

        double process (double bendRatio)
        {
            if (! active) return 0.0;
            const double e = excPos < exc.size() ? exc[excPos++] : 0.0;
            const double tt = std::max (0.0, t);
            const double f = freq * (1.0 + p.glide * velocity * std::exp (-tt / p.tGlide)) * bendRatio;
            const bool rc = (ctl++ & 15) == 0;
            double y = p1.process (e, f, sr, rc);
            if (p.polMix > 0.0)
                y = (1.0 - p.polMix) * y + p.polMix * p2.process (e, f * std::pow (2.0, p.detune / 1200.0), sr, rc);
            t += 1.0 / sr;
            // an electric's pickup sees the string at its own position
            if (p.electric)
            {
                comb[(size_t) combW] = y;
                const double back = comb[(size_t) ((combW - combD) & 4095)];
                combW = (combW + 1) & 4095;
                y = puLp.process (y - back);
            }
            level = std::max (std::abs (y), level * 0.9995);
            if (excPos >= exc.size() && t > 0.1 && level < 2.0e-6) active = false;
            if (! std::isfinite (y)) { reset(); return 0.0; }
            return y;
        }

    private:
        double sr { 48000 }, freq { 110 }, velocity { 0.8 }, muted { 0 }, t { 0 }, level { 0 };
        GuitarParams p;
        StringLoop p1, p2;
        std::vector<double> exc, comb;
        size_t excPos { 0 };
        int combW { 0 }, combD { 1 }, ctl { 0 }, midiNote { -1 };
        bool active { false }, held { false };
        Svf puLp;
        Rng rng;
    };

    // the amp and cabinet (electric), once on the sum of the strings
    class GuitarAmp
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; setup(); reset(); }
        void setParams (const GuitarParams& gp, double inputGain) { p = gp; inGain = inputGain; setup(); }
        void reset()
        {
            for (auto* s : { &lo, &hi, &cab1, &cab2, &pres }) s->reset();
            pre.reset(); dcHp.reset(); h1.reset(); h2.reset(); upHist.fill (0.0); dnHist.fill (0.0); upPos = dnPos = 0;
        }
        double process (double x)
        {
            double y = x * inGain;
            if (p.drive > 0.05)
            {
                // 2x oversampled clipper: zero-stuff, half-band, clip, half-band, decimate
                const double gain = std::pow (10.0, p.drive * 0.6);
                const double off = std::tanh (gain * p.asym * 0.3);
                double o[2];
                for (int k = 0; k < 2; ++k)
                {
                    const double u = halfband (upHist, upPos, k == 0 ? 2.0 * y : 0.0);
                    const double c = std::tanh (gain * (u + p.asym * 0.3)) - off;
                    o[k] = pre.lp (c);
                }
                halfband (dnHist, dnPos, o[0]);
                y = halfband (dnHist, dnPos, o[1]);
                y = dcHp.hp (y);
            }
            const double l = lo.process (y), h = hi.process (y), m = y - l - h;
            y = gBass * l + gMid * m + gTreble * h;
            y = h2.hp (h1.hp (y));
            y = cab2.process (cab1.process (y));
            if (p.presence != 0.0) y += p.presence * pres.process (y);
            return y;
        }
    private:
        void setup()
        {
            lo.set (250.0, 0.7, sr, 0); hi.set (2500.0, 0.7, sr, 2);
            cab1.set (p.cabLp, p.cabQ, sr, 0); cab2.set (p.cabLp * 1.4, 0.7, sr, 0); pres.set (2800.0, 1.5, sr, 1);
            h1.set (p.cabHp, sr); h2.set (p.cabHp, sr);
            pre.set (9000.0, 2.0 * sr); dcHp.set (30.0, sr);
            gBass = std::pow (10.0, p.bass / 20.0); gMid = std::pow (10.0, p.mid / 20.0); gTreble = std::pow (10.0, p.treble / 20.0);
        }
        // a 31-tap half-band low-pass at the 2x rate (windowed sinc, Blackman)
        static const std::array<double, 31>& halfbandTaps()
        {
            static const std::array<double, 31> h = []
            {
                std::array<double, 31> t {};
                double sum = 0.0;
                for (int i = 0; i < 31; ++i)
                {
                    const double n = i - 15.0;
                    const double sinc = n == 0.0 ? 1.0 : std::sin (kPi * n / 2.0) / (kPi * n / 2.0);
                    const double win = 0.42 - 0.5 * std::cos (kTwoPi * i / 30.0) + 0.08 * std::cos (2.0 * kTwoPi * i / 30.0);
                    t[(size_t) i] = 0.5 * sinc * win;
                    sum += t[(size_t) i];
                }
                for (auto& v : t) v /= sum;      // unity at DC
                return t;
            }();
            return h;
        }
        static double halfband (std::array<double, 32>& hist, int& pos, double x)
        {
            const auto& taps = halfbandTaps();
            hist[(size_t) pos] = x;
            double acc = 0.0;
            for (int i = 0; i < 31; ++i)
                if (taps[(size_t) i] != 0.0) acc += taps[(size_t) i] * hist[(size_t) ((pos - i) & 31)];
            pos = (pos + 1) & 31;
            return acc;
        }
        double sr { 48000 }, inGain { 1 }, gBass { 1 }, gMid { 1 }, gTreble { 1 };
        GuitarParams p;
        Svf lo, hi, cab1, cab2, pres;
        OnePole pre, dcHp, h1, h2;
        std::array<double, 32> upHist {}, dnHist {};
        int upPos { 0 }, dnPos { 0 };
    };

    // the acoustic's body
    class GuitarBodyRes
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; reset(); }
        void setParams (double air, double top, double bright)
        {
            static const float f[12] = { 98.0f, 204.0f, 390.0f, 510.0f, 590.0f, 720.0f, 880.0f, 1100.0f, 1450.0f, 1900.0f, 2500.0f, 3300.0f };
            static const float q[12] = { 12, 18, 15, 20, 25, 20, 25, 25, 25, 25, 25, 20 };
            float g[12] = { 1.0f, 1.0f, 0.5f, 0.4f, 0.3f, 0.3f, 0.25f, 0.2f, 0.18f, 0.12f, 0.1f, 0.08f };
            g[0] *= (float) air; g[1] *= (float) top;
            for (int i = 4; i < 12; ++i) g[i] *= (float) bright;
            bodyBank.build (f, q, g, 12, sr);
            direct = 0.15 * bright;
            hp.set (1500.0, sr);
        }
        void reset() { bodyBank.reset(); hp.reset(); }
        double process (double x) { return bodyBank.process ((float) x) + direct * hp.hp (x); }
    private:
        double sr { 48000 }, direct { 0.15 };
        ModalBody bodyBank;
        OnePole hp;
    };
}
