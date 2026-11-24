// Aquanode Modular - Pizz Modal: a cello pizzicato rebuilt from its measured partials
//
// Every partial of a plucked note is a sine of its own (a little stretched, like a real
// string's), with the level it has in a cello pizzicato recording supplied for this project
// (measured per note, in between taken at the same frequency from the two nearest notes, so the
// body's peaks stay where they are) and the course it takes there: the low partials swell for
// the first 50 - 100 ms while the body takes up the string, the high ones are there at once and
// fall fast, then everything rings down at its own rate. On top: the body's low bloom (a handful
// of fixed modes around 100 and 190 Hz that the pluck sets ringing) and the finger leaving the
// string (a short soft noise, 15 ms). No samples are played: all of it is the tables in
// PizzModalData.h and the oscillators here.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "PizzModalData.h"

namespace aquanode::pizzmodal
{
    class Voice
    {
    public:
        static constexpr int kMaxPartials = 160, kMaxOsc = kMaxPartials + kBloom;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            // the finger noise's band (700 Hz - 3.5 kHz) and its loudness through that band
            hp.setHighPass (700.0, sr); lp.setLowPass (3500.0, sr);
            Biquad h = hp, l = lp; uint32_t s = 12345u; double acc = 0.0;
            for (int i = 0; i < 8192; ++i) { const double y = l.process (h.process (white (s))); if (i >= 1024) acc += y * y; }
            noiseRms = std::sqrt (acc / 7168.0) + 1.0e-12;
            reset();
        }
        void reset() { active = false; held = false; n = 0; }
        bool isActive() const { return active; }
        bool isHeld() const { return held; }
        void release() { held = false; }

        // f: the note's frequency (detune included); dyn 0..1; brightness 0.25..2 (1 = as measured)
        void start (int note, double f, double dyn, double brightness, uint64_t seed, double startDelay, int maxPartials = kMaxPartials)
        {
            dyn = std::clamp (dyn, 0.0, 1.0);
            rng = (uint32_t) (seed * 2654435761u + 1u) | 1u;
            const double key = 69.0 + 12.0 * std::log2 (std::max (1.0, f) / 440.0);
            (void) note;
            // the two measured notes around this one
            int lo = 0;
            while (lo < kNotes - 1 && kKey[lo + 1] <= key) ++lo;
            int hi = std::min (kNotes - 1, lo + 1);
            double w = 0.0;
            if (key <= kKey[0]) { lo = hi = 0; }
            else if (key >= kKey[kNotes - 1]) { lo = hi = kNotes - 1; }
            else w = (key - kKey[lo]) / std::max (1.0, (double) (kKey[hi] - kKey[lo]));
            // the string's stiffness grows as the string is stopped shorter
            const double B = std::min (8.0e-5, 0.6e-5 * std::pow (2.0, (key - 36.0) / 10.0));
            const double fMax = std::min (12000.0, 0.45 * sr);
            const double tiltDb = 6.0 * (dyn - 0.85) + 5.0 * std::log2 (std::clamp (brightness, 0.25, 2.0));   // per octave above 300 Hz
            n = 0;
            double total = 0.0;
            const int kLimit = std::clamp (maxPartials, 8, kMaxPartials);
            for (int k = 1; k <= kLimit; ++k)
            {
                const double fk = k * f * std::sqrt (1.0 + B * k * k);
                if (fk > fMax) break;
                double db = (1.0 - w) * levelAt (lo, fk) + w * levelAt (hi, fk);
                if (fk > 300.0) db += tiltDb * std::log2 (fk / 300.0);
                Osc& o = osc[(size_t) n++];
                o.hz = fk; o.fixed = false; o.db = db;
                bandRow (fk, o.row, o.late);
                total += std::pow (10.0, db / 10.0) * 0.5;
            }
            // the partials together at the note's loudness (the recording's own over its range, held beyond it)
            const double target = (1.0 - w) * loud (lo) + w * loud (hi);
            const double norm = target - 10.0 * std::log10 (std::max (1.0e-30, total));
            for (int i = 0; i < n; ++i) osc[(size_t) i].db += norm;
            // the body's low bloom: fixed modes, ref the partials' loudness
            for (int b = 0; b < kBloom; ++b)
            {
                Osc& o = osc[(size_t) n++];
                o.hz = kBloomHz[b]; o.fixed = true; o.db = target + 3.0103 - 3.0 * (1.0 - dyn);
                for (int j = 0; j < kTimes; ++j) o.row[j] = kBloomEnv[b][j];
                o.late = kBloomLateSlope;
            }
            // pick the peak to drop what is far under it
            double peak = -300.0;
            for (int i = 0; i < n; ++i) peak = std::max (peak, osc[(size_t) i].db + maxOf (osc[(size_t) i].row));
            const double ampScale = (0.25 + 0.75 * dyn) * kGain;
            int m = 0;
            for (int i = 0; i < n; ++i)
            {
                Osc o = osc[(size_t) i];
                if (o.db + maxOf (o.row) < peak - 70.0) continue;
                // each pluck a little different: the partials' levels by up to a dB, their phases
                // near the string's own (all starting together) and the bloom's anywhere
                o.base = std::pow (10.0, (o.db + 0.7 * (uni() * 2.0 - 1.0)) / 20.0) * ampScale;
                const double ph = o.fixed ? 6.283185307179586 * uni() : 0.6 * (uni() * 2.0 - 1.0);
                run.re[(size_t) m] = (float) std::sin (ph); run.im[(size_t) m] = (float) std::cos (ph);   // (the real part: a sine from 0 at ph = 0)
                osc[(size_t) m++] = o;
            }
            n = m;
            ratio = -1.0;
            setRotations (1.0);
            seg = 0; t = -std::max (0.0, startDelay); sampleInSeg = 0;
            enterSegment();
            refAmp = std::pow (10.0, (target + 3.0103) / 20.0) * ampScale;
            clickAmp = 0.207 * std::sqrt (std::pow (10.0, target / 10.0)) * ampScale * (0.4 + 0.6 * dyn) / noiseRms;
            hpS = hp; lpS = lp; hpS.clear(); lpS.clear();
            active = n > 0; held = true; quietRun = 0;
        }

        // bendRatio: the pitch bend (1 = none)
        double process (double bendRatio)
        {
            if (! active) return 0.0;
            if (t < 0.0) { t += 1.0 / sr; return 0.0; }
            if ((ctl++ & 15) == 0 && std::abs (bendRatio - ratio) > 1.0e-7) setRotations (bendRatio);
            float acc = 0.0f;
            float* re = run.re.data(); float* im = run.im.data(); const float* cr = run.cr.data(); const float* ci = run.ci.data();
            float* amp = run.amp.data(); const float* mul = run.mul.data();
            for (int i = 0; i < n; ++i)
            {
                const float r = re[i] * cr[i] - im[i] * ci[i];
                const float q = re[i] * ci[i] + im[i] * cr[i];
                re[i] = r; im[i] = q;
                acc += amp[i] * r;
                amp[i] *= mul[i];
            }
            double y = acc;
            // the finger leaving the string
            if (t < 0.12)
            {
                const double e = (1.0 - std::exp (-t / 0.001)) * std::exp (-t / 0.015);
                y += clickAmp * e * lpS.process (hpS.process (white (rng)));
            }
            t += 1.0 / sr;
            if (++sampleInSeg >= segLen) { ++seg; enterSegment(); }
            // keep the phasors on the unit circle; drop what has rung out
            if ((ctl & 255) == 0)
            {
                double loudest = 0.0;
                int m = 0;
                for (int i = 0; i < n; ++i)
                {
                    const size_t a = (size_t) i, b = (size_t) m;
                    const float g = 1.5f - 0.5f * (run.re[a] * run.re[a] + run.im[a] * run.im[a]);
                    loudest = std::max (loudest, (double) run.amp[a]);
                    if (! (run.amp[a] > refAmp * 1.0e-5 || t < 0.3)) continue;
                    if (b != a)
                    {
                        osc[b] = osc[a];
                        run.cr[b] = run.cr[a]; run.ci[b] = run.ci[a]; run.amp[b] = run.amp[a]; run.mul[b] = run.mul[a];
                    }
                    run.re[b] = run.re[a] * g; run.im[b] = run.im[a] * g;
                    ++m;
                }
                n = m;
                if (loudest < refAmp * 1.0e-5 && t > 0.3) active = false;
            }
            if (! std::isfinite (y)) { reset(); return 0.0; }
            return y;
        }

    private:
        // what a partial is (set at the start) ...
        struct Osc
        {
            double hz { 0 }, db { 0 }, base { 0 };
            float row[kTimes] {}; float late { 0 };
            bool fixed { false };
        };
        // ... and what runs every sample, side by side for the loop
        struct Run { std::array<float, kMaxOsc> re {}, im {}, cr {}, ci {}, amp {}, mul {}; };
        struct Biquad
        {
            double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, z1 { 0 }, z2 { 0 };
            void setLowPass (double fc, double fs) { set (fc, fs, false); }
            void setHighPass (double fc, double fs) { set (fc, fs, true); }
            void set (double fc, double fs, bool high)
            {
                const double w0 = 6.283185307179586 * std::min (fc, 0.45 * fs) / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * 0.7071067811865476);
                const double a0 = 1.0 + al;
                if (high) { b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0; }
                else      { b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0; }
                a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
            }
            void clear() { z1 = z2 = 0.0; }
            double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        };

        static double white (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double) (int32_t) s * (1.0 / 2147483648.0); }
        double uni() { return 0.5 + 0.5 * white (rng); }
        static double maxOf (const float* r) { double m = r[0]; for (int j = 1; j < kTimes; ++j) m = std::max (m, (double) r[j]); return m; }

        // a measured note's partial level at a frequency (log-frequency interpolation; above its
        // last partial falling 12 dB an octave)
        static double levelAt (int idx, double fHz)
        {
            const double f0 = 440.0 * std::pow (2.0, (kKey[idx] - 69) / 12.0), B = kStretch[idx];
            const int cnt = kCount[idx]; const float* L = kLevel + kStart[idx];
            auto fOf = [f0, B] (int k) { return k * f0 * std::sqrt (1.0 + B * k * k); };
            if (fHz <= fOf (1)) return L[0];
            // the partial just under fHz
            int k = std::clamp ((int) (fHz / f0), 1, cnt);
            while (k > 1 && fOf (k) > fHz) --k;
            while (k < cnt && fOf (k + 1) <= fHz) ++k;
            if (k >= cnt) return L[cnt - 1] - 12.0 * std::log2 (fHz / fOf (cnt));
            const double a = std::log (fOf (k)), b = std::log (fOf (k + 1)), x = std::log (fHz);
            return L[k - 1] + (L[k] - L[k - 1]) * (x - a) / (b - a);
        }
        // a measured note's loudness (its partials together; smoothed over the keys when measured)
        static double loud (int idx)
        {
            double sum = 0.0; const float* L = kLevel + kStart[idx];
            for (int k = 0; k < kCount[idx]; ++k) sum += std::pow (10.0, L[k] / 10.0) * 0.5;
            return 10.0 * std::log10 (std::max (1.0e-30, sum));
        }
        static void bandRow (double fHz, float* row, float& late)
        {
            const double x = std::log (fHz);
            int b = 0;
            while (b < kBands - 2 && std::log ((double) kBandHz[b + 1]) < x) ++b;
            const double a = std::log ((double) kBandHz[b]), c = std::log ((double) kBandHz[b + 1]);
            const double u = std::clamp ((x - a) / (c - a), 0.0, 1.0);
            for (int j = 0; j < kTimes; ++j) row[j] = (float) ((1.0 - u) * kEnv[b][j] + u * kEnv[b + 1][j]);
            late = (float) ((1.0 - u) * kLateSlope[b] + u * kLateSlope[b + 1]);
        }

        void setRotations (double r)
        {
            ratio = r;
            for (int i = 0; i < n; ++i)
            {
                const Osc& o = osc[(size_t) i];
                const double hz = o.fixed ? o.hz : o.hz * r;
                const double w = 6.283185307179586 * std::min (hz, 0.49 * sr) / sr;
                run.cr[(size_t) i] = (float) std::cos (w); run.ci[(size_t) i] = (float) std::sin (w);
            }
        }
        // the envelopes go from one measured time to the next exponentially (straight in dB)
        void enterSegment()
        {
            sampleInSeg = 0;
            if (seg < kTimes - 1)
            {
                const double dt = (double) kTime[seg + 1] - kTime[seg];
                segLen = std::max (1, (int) std::lround (dt * sr));
                for (int i = 0; i < n; ++i)
                {
                    const Osc& o = osc[(size_t) i];
                    run.amp[(size_t) i] = (float) (o.base * std::pow (10.0, o.row[seg] / 20.0));
                    run.mul[(size_t) i] = (float) std::pow (10.0, (o.row[seg + 1] - o.row[seg]) / (20.0 * segLen));
                }
            }
            else
            {
                segLen = 1 << 30;
                for (int i = 0; i < n; ++i)
                {
                    const Osc& o = osc[(size_t) i];
                    run.amp[(size_t) i] = (float) (o.base * std::pow (10.0, o.row[kTimes - 1] / 20.0));
                    run.mul[(size_t) i] = (float) std::pow (10.0, std::min (-3.0f, o.late) / (20.0 * sr));
                }
            }
        }

        static constexpr double kGain = 0.04;
        double sr { 48000.0 }, t { 0.0 }, ratio { 1.0 }, refAmp { 1.0 }, clickAmp { 0.0 }, noiseRms { 1.0 };
        std::array<Osc, kMaxOsc> osc {};
        Run run;
        int n { 0 }, seg { 0 }, segLen { 1 }, sampleInSeg { 0 }, quietRun { 0 };
        uint32_t ctl { 0 }, rng { 1u };
        bool active { false }, held { false };
        Biquad hp, lp, hpS, lpS;
    };
}
