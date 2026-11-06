#pragma once
//==============================================================================
// Acid303Core - the voice of the 303 module, fitted to recordings of a real
// original hardware (25 sets of 16 notes, each knob stepped through 25/50/75/100 %
// with the others at 0, 25, 50, 75 or 100 %, saw and square, plain and
// accented). No JUCE, so the fitting tool (tools/measure/acid303-fit) renders the very same
// code the module plays.
//
//   Oscillator  a band-limited saw, and the 303's square, which is not a
//               square: the saw through a transistor shaper, a top that sags
//               to about half its height over the first 53 % of the period,
//               then a fast fall to a nearly flat bottom. Its first 36 harmonics are
//               the measured ones (relative to the saw, which divides the filter
//               out - the same at every cutoff), the rest from a fitted cycle.
//               Mip-mapped tables at four times the sample rate.
//   Filter      four one-pole low-passes in a row, their cutoffs spread as the
//               fit found the 303's (0.08, 1, 1.04, 2.5 x the reference), with the
//               resonance fed back from the end through a high-pass, solved
//               without a delay in the loop, and a soft limit where the feedback
//               comes in. At four times the sample rate.
//   MEG         the cutoff envelope: an instant rise (slightly rounded) and an
//               exponential fall, Decay 0..100 % on plain notes, a short fixed
//               fall on accented ones. Env Mod scales it and also lowers the
//               base cutoff, as on the 303.
//   Accent      a sweep: an accented note's envelope charges a capacitor that
//               opens the filter further and lets go more slowly, so accents in
//               a row build up a little; and the VCA gets louder for a moment.
//   VCA         a fast attack, a slow fall while the gate is held, a quick
//               release, a high-pass after it.
// Every constant in Calibration came out of the fit (tools/measure/acid303-fit/fit_303.py).
//==============================================================================
#include <array>
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>

namespace aquanode::acid303
{
    //==========================================================================
    // the calibration
    enum Param
    {
        Cut0, Cut25, Cut50, Cut75, Cut100,   // log2 Hz of the reference cutoff at Cutoff 0/25/50/75/100 %
        EnvOffset, EnvOffsetCurve,           // octaves Env Mod lowers the base cutoff by: offset * envMod^curve
        EnvDepthMin, EnvDepth, EnvCurve,     // octaves the MEG adds: min + depth * envMod^curve
        DecayA, DecayB, DecayC,              // plain MEG fall: tau = exp (A + B d + C d^2) s
        AccentDecayLn,                       // ln tau of the MEG on accented notes
        MegAttackLn,                         // ln tau of the MEG's rounded rise
        AccentSweep, AccentSweepCurve,       // octaves the accent sweep adds: sweep * accent^curve
        AccentChargeLn, AccentReleaseLn,     // ln tau of the sweep capacitor charging / letting go
        AccentVca, AccentVcaCurve, AccentVcaDecayLn,  // VCA x (1 + vca * accent^curve * e^-t/tau)
        ResMax, ResCurve, ResShift,          // feedback k = max * res^curve; res also moves the cutoff (octaves)
        FeedbackHpLn,                        // ln Hz of the high-pass in the feedback
        DriveLn,                             // ln of the soft limit's drive
        Pole1Ln, Pole3Ln, Pole4Ln,           // ln of the outer stages' cutoffs / the reference
        VcaDecayLn, VcaAttackLn, VcaReleaseLn,
        OutHpLn,                             // ln Hz of the high-pass after the VCA
        SawGainDb, SquareGainDb,
        CutoffCeiling,                       // log2 Hz the cutoff levels off at (the top of the 303's range)
        ResTrack,                            // the feedback grows by this much per octave of cutoff above 1 kHz
        NumParams
    };

    inline const char* paramName (int p)
    {
        static const char* n[NumParams] = {
            "cut0", "cut25", "cut50", "cut75", "cut100", "envOffset", "envOffsetCurve", "envDepthMin", "envDepth", "envCurve",
            "decayA", "decayB", "decayC", "accentDecayLn", "megAttackLn", "accentSweep", "accentSweepCurve",
            "accentChargeLn", "accentReleaseLn", "accentVca", "accentVcaCurve", "accentVcaDecayLn",
            "resMax", "resCurve", "resShift", "feedbackHpLn", "driveLn", "pole1Ln", "pole3Ln", "pole4Ln",
            "vcaDecayLn", "vcaAttackLn", "vcaReleaseLn", "outHpLn", "sawGainDb", "squareGainDb", "cutoffCeiling", "resTrack" };
        return p >= 0 && p < NumParams ? n[p] : "";
    }

    struct Calibration
    {
        std::array<double, NumParams> v {};
        double operator[] (int p) const noexcept { return v[(size_t) p]; }
        double& operator[] (int p) noexcept { return v[(size_t) p]; }
    };

    // the fitted values: fittedCalibration() in Acid303Calibration.h

    //==========================================================================
    // the oscillator's tables
    class Tables
    {
    public:
        static constexpr int kSize = 4096;
        static constexpr int kLevels = 12;     // 2047, 1024, 512 ... 1 harmonics

        static const Tables& get()
        {
            static const Tables t;
            return t;
        }

        static int harmonicsOf (int level) { return level == 0 ? kSize / 2 - 1 : (kSize / 2) >> level; }

        // the level with the most harmonics that all stay under `allowed`
        static int levelFor (double allowed)
        {
            for (int l = 0; l < kLevels; ++l)
                if ((double) harmonicsOf (l) <= allowed)
                    return l;
            return kLevels - 1;
        }

        const float* table (int wave, int level) const { return data[(size_t) wave][(size_t) level].data(); }

    private:
        std::array<std::array<std::vector<float>, kLevels>, 2> data;

        using C = std::complex<double>;

        static void fft (std::vector<C>& a, bool inverse)
        {
            const int n = (int) a.size();
            for (int i = 1, j = 0; i < n; ++i)
            {
                int bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
            }
            for (int len = 2; len <= n; len <<= 1)
            {
                const double ang = 2.0 * 3.14159265358979323846 / len * (inverse ? 1.0 : -1.0);
                const C wl (std::cos (ang), std::sin (ang));
                for (int i = 0; i < n; i += len)
                {
                    C w (1.0, 0.0);
                    for (int j = 0; j < len / 2; ++j)
                    {
                        const C u = a[(size_t) (i + j)], v = a[(size_t) (i + j + len / 2)] * w;
                        a[(size_t) (i + j)] = u + v;
                        a[(size_t) (i + j + len / 2)] = u - v;
                        w *= wl;
                    }
                }
            }
        }

        // the 303 square's fitted cycle, u = phase since the saw's reset: a top that sags
        // (cubic), the fall at d softened over a small width, a nearly flat bottom
        static double squareCycle (double u)
        {
            static constexpr double d = 0.5359, t0 = 1.0655, t1 = -0.6505, t2 = 0.28, t3 = 0.0095, b0 = -0.9195, b1 = 0.2067, wf = 0.0086;
            auto raw = [] (double x)
            {
                x -= std::floor (x);
                if (x < d) { const double r = x / d; return t0 + t1 * r + t2 * r * r + t3 * r * r * r; }
                return b0 + b1 * (x - d) / (1.0 - d);
            };
            if (std::abs (u - d) > 3.0 * wf)
                return raw (u);
            // a raised-cosine average across the fall
            double s = 0.0, ws = 0.0;
            for (int i = -32; i <= 32; ++i)
            {
                const double o = wf * (double) i / 32.0, w = 0.5 + 0.5 * std::cos (3.14159265358979323846 * (double) i / 32.0);
                s += w * raw (u + o); ws += w;
            }
            return s / ws;
        }

        Tables()
        {
            const int N = kSize, H = N / 2 - 1;
            std::vector<C> saw ((size_t) (H + 1)), sq ((size_t) (H + 1));
            // the saw: an ideal ramp, fundamental amplitude 1, its phase 0: c_k = -i * i^k / k
            for (int k = 1; k <= H; ++k)
            {
                const C ik = std::pow (C (0.0, 1.0), k);
                saw[(size_t) k] = C (0.0, -1.0) * ik / (double) k;
            }
            // the square: the fitted cycle's harmonics, aligned the same way ...
            {
                std::vector<C> a ((size_t) N);
                for (int i = 0; i < N; ++i) a[(size_t) i] = squareCycle ((double) i / N);
                fft (a, false);
                const double ph1 = std::arg (a[1]), m1 = std::abs (a[1]);
                for (int k = 1; k <= H; ++k)
                    sq[(size_t) k] = a[(size_t) k] / m1 * std::polar (1.0, -ph1 * k);
            }
            // ... with the measured ones below the 36th (blended over 28..36)
            static const double mre[36] = { 1.0, -0.099549, -0.335324, 0.081637, 0.164982, -0.06504, -0.086251, 0.047449, 0.043431, -0.029968, -0.019055, 0.013333, 0.006261, 0.001561, -0.001749, -0.013377, 0.00291, 0.021495, -0.007863, -0.024781, 0.014401, 0.023852, -0.020684, -0.019586, 0.024794, 0.012384, -0.025878, -0.004255, 0.025035, -0.003342, -0.021078, 0.009119, 0.014177, -0.01142, -0.006409, 0.01163 };
            static const double mim[36] = { 0.0, 0.110916, 0.08639, -0.084398, -0.086955, 0.082604, 0.070395, -0.08237, -0.05135, 0.0802, 0.032743, -0.075128, -0.015593, 0.066715, 0.000983, -0.055171, 0.009934, 0.041898, -0.016778, -0.027746, 0.018957, 0.014642, -0.016884, -0.003822, 0.011732, -0.003705, -0.004662, 0.007706, -0.002623, -0.007229, 0.008577, 0.00417, -0.012876, 0.000767, 0.014351, -0.005067 };
            for (int k = 1; k <= 36; ++k)
            {
                const double w = k <= 28 ? 1.0 : 0.5 + 0.5 * std::cos (3.14159265358979323846 * (double) (k - 28) / 8.0);
                sq[(size_t) k] = w * C (mre[k - 1], mim[k - 1]) + (1.0 - w) * sq[(size_t) k];
            }
            for (int wave = 0; wave < 2; ++wave)
                for (int l = 0; l < kLevels; ++l)
                {
                    const int L = harmonicsOf (l);
                    std::vector<C> a ((size_t) N, C (0.0, 0.0));
                    for (int k = 1; k <= L; ++k)
                        a[(size_t) k] = (wave == 0 ? saw : sq)[(size_t) k];
                    fft (a, true);   // sum c_k e^{+i 2 pi k n / N}
                    auto& t = data[(size_t) wave][(size_t) l];
                    t.resize ((size_t) N + 1);
                    for (int i = 0; i < N; ++i) t[(size_t) i] = (float) a[(size_t) i].real();
                    t[(size_t) N] = t[0];
                }
        }
    };

    //==========================================================================
    // the knobs, 0..1
    struct Knobs
    {
        double cutoff { 0.5 }, resonance { 0.5 }, envMod { 0.5 }, decay { 0.5 }, accent { 0.5 };
        // beyond the 303's own knobs (their defaults are the 303 as measured):
        double accentDecay { 1.0 };   // x the accented notes' envelope fall
        double sweepTime { 1.0 };     // x the accent sweep's charge and release times
        double track { 0.0 };         // the cutoff follows the note: octaves per octave from the low C
        double drive { 0.0 };         // 0..1: harder into the filter's soft limit (up to 32x)
    };

    //==========================================================================
    // one voice
    class Voice
    {
    public:
        void prepare (double sampleRate, const Calibration& c)
        {
            cal = c;
            fsOut = sampleRate;
            fs = (double) kOversample * sampleRate;
            // half-band decimator (2x -> 1x): a 31-tap windowed sinc
            const int M = kTaps - 1;
            double sum = 0.0;
            for (int i = 0; i < kTaps; ++i)
            {
                const double x = (double) i - 0.5 * M;
                const double sinc = std::abs (x) < 1.0e-9 ? 0.5 : std::sin (0.5 * kPi * x) / (kPi * x);
                const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * i / M) + 0.08 * std::cos (4.0 * kPi * i / M);
                fir[(size_t) i] = sinc * w;
                sum += fir[(size_t) i];
            }
            for (auto& h : fir) h /= sum;
            (void) Tables::get();   // built here, not on the audio thread
            derive();
            reset();
        }

        // the calibration can be swapped (the fitting tool); the knobs come with every sample
        void setCalibration (const Calibration& c) { cal = c; derive(); knobsValid = false; coefCountdown = 0; }

        void reset()
        {
            phase = 0.0;
            s.fill (0.0);
            fbState = outHpState = 0.0;
            megRaw = meg = sweep = 0.0;
            vcaAtt = 0.0; vcaDec = 1.0; vcaRel = 1.0; accBoost = 0.0;
            gate = false; accentNote = false; sliding = false;
            hist.fill (0.0); histPos = 0;
            hist4.fill (0.0); hist4Pos = 0;
            idle = true;
            knobsValid = pitchValid = false;
            coefCountdown = 0;
        }

        void setWave (int w) noexcept { wave = std::clamp (w, 0, 1); pitchValid = false; }
        // how long a slide takes (to 95 % of the way: three time constants)
        void setSlideTime (double seconds) noexcept { slideTau = std::max (seconds / 3.0, 1.0e-4); slideCoef = 1.0 - std::exp (-1.0 / (slideTau * fs)); }
        void setStartPhase (double p) noexcept { phase = p - std::floor (p); }
        // a pitch offset on top of the note (the Tune knob), octaves; held notes follow it
        void setTuneOctaves (double o) noexcept { if (o != tuneOct) { tuneOct = o; pitchValid = false; } }

        // a new note: from silence or retriggered, or (slide) a glide to it under the held gate
        void noteOn (double hz, bool accented, bool slide, const Knobs& k)
        {
            targetLog2 = std::log2 (std::max (hz, 1.0));
            if (slide && gate && ! idle)
            {
                sliding = true;
                pitchValid = false;   // the 303: no new envelopes, the pitch glides, the accent of the new step applies
                accentNote = accented;
                return;
            }
            sliding = false;
            pitchLog2 = targetLog2;
            pitchValid = false;
            accentNote = accented;
            const double tau = accented ? std::exp (cal[AccentDecayLn]) * std::max (0.05, k.accentDecay)
                                        : std::exp (cal[DecayA] + cal[DecayB] * k.decay + cal[DecayC] * k.decay * k.decay);
            megMul = std::exp (-1.0 / (tau * fs));
            megRaw = 1.0;
            // the VCA starts again from where it is (no click on a retrigger)
            vcaAtt = idle ? 0.0 : vcaAtt * vcaDec * vcaRel;
            vcaDec = 1.0; vcaRel = 1.0;
            if (accented) accBoost = 1.0;
            gate = true;
            idle = false;
        }

        void noteOff() noexcept { gate = false; }
        bool isGateOn() const noexcept { return gate; }
        bool isIdle() const noexcept { return idle; }
        double currentHz() const noexcept { return std::exp2 (pitchLog2); }

        // one output sample; fm offsets the oscillator's read phase by that many cycles (DX7-style
        // phase modulation, as the other oscillators' FM In)
        float process (const Knobs& k, double fm = 0.0)
        {
            if (idle)
                return 0.0f;
            if (k.cutoff != lastKnobs.cutoff || k.resonance != lastKnobs.resonance || k.envMod != lastKnobs.envMod
                || k.accent != lastKnobs.accent || k.sweepTime != lastKnobs.sweepTime || k.track != lastKnobs.track
                || k.drive != lastKnobs.drive || ! knobsValid)
                knobsChanged (k);
            const Tables& T = Tables::get();
            double y2[kOversample];
            for (int n = 0; n < kOversample; ++n)
            {
                // pitch
                if (sliding || ! pitchValid)
                {
                    if (sliding)
                    {
                        pitchLog2 += (targetLog2 - pitchLog2) * slideCoef;
                        if (std::abs (targetLog2 - pitchLog2) < 1.0e-7) { pitchLog2 = targetLog2; sliding = false; }
                    }
                    inc = std::exp2 (pitchLog2 + tuneOct) / fs;
                    tab = T.table (wave, Tables::levelFor (0.5 / std::max (inc, 1.0e-9)));
                    pitchValid = true;
                }
                double x;
                {
                    double rp = phase + fm;
                    rp -= std::floor (rp);
                    const double pos = rp * Tables::kSize;
                    const int i0 = std::clamp ((int) pos, 0, Tables::kSize - 1);
                    const double fr = pos - i0;
                    x = tab[i0] + fr * (tab[i0 + 1] - tab[i0]);
                }
                phase += inc;
                phase -= std::floor (phase);

                // envelopes
                megRaw *= megMul;
                meg += (megRaw - meg) * megAttCoef;
                const double target = accentNote ? meg : 0.0;
                sweep += (target - sweep) * (target > sweep ? chargeCoef : releaseCoef);
                if (gate)
                {
                    vcaAtt += (1.0 - vcaAtt) * vcaAttCoef;
                    vcaDec *= vcaDecMul;
                }
                else
                    vcaRel *= vcaRelMul;
                accBoost *= accVcaMul;
                const double vca = vcaAtt * vcaDec * vcaRel * (1.0 + kVcaAcc * accBoost);

                // filter (its coefficients every fourth output sample: the envelopes move slowly next to that)
                if (n == 0 && --coefCountdown <= 0)
                {
                    coefCountdown = 2;
                    double lf = kBase + kDepth * meg + kSweep * sweep;
                    if (kTrack != 0.0) lf += kTrack * (pitchLog2 + tuneOct - kLowC);
                    // levelling off at the top of the range (a smooth minimum in octaves)
                    lf = -std::log2 (std::exp2 (-lf) + std::exp2 (-cal[CutoffCeiling]));
                    kFbNow = kFb * (1.0 + cal[ResTrack] * std::max (0.0, lf - 10.0));
                    const double fc = std::exp2 (lf);
                    for (int i = 0; i < 4; ++i)
                    {
                        const double g = std::tan (kPi * std::min (fc * pole[i], 0.45 * fs) / fs);
                        G[i] = g / (1.0 + g);
                    }
                }
                double Gam = 1.0, Sig = 0.0;
                for (int i = 0; i < 4; ++i)
                {
                    Gam *= G[i];
                    Sig = G[i] * Sig + (1.0 - G[i]) * s[(size_t) i];
                }
                const double one = 1.0 - fbG;
                const double u = (x - kFbNow * one * (Sig - fbState)) / (1.0 + kFbNow * one * Gam);
                const double un = softLimit (kDrive * u) / kDrive;
                double v = un;
                for (int i = 0; i < 4; ++i)
                {
                    const double y = G[i] * v + (1.0 - G[i]) * s[(size_t) i];
                    s[(size_t) i] = 2.0 * y - s[(size_t) i];
                    v = y;
                }
                {
                    const double lp = fbG * v + (1.0 - fbG) * fbState;
                    fbState = 2.0 * lp - fbState;
                }
                // VCA, then the high-pass after it
                const double out = v * vca;
                const double lp = outG * out + (1.0 - outG) * outHpState;
                outHpState = 2.0 * lp - outHpState;
                y2[n] = (out - lp) * (wave == 0 ? sawGain : squareGain);
            }
            // decimate in two half-band steps, 4x -> 2x -> 1x (each history written twice, so
            // the taps read straight through)
            auto halfband = [this] (std::array<double, 2 * kTaps>& hst, int& pos, double a, double b)
            {
                hst[(size_t) pos] = hst[(size_t) (pos + kTaps)] = a; if (++pos == kTaps) pos = 0;
                hst[(size_t) pos] = hst[(size_t) (pos + kTaps)] = b; if (++pos == kTaps) pos = 0;
                double acc = 0.0;
                const double* h = &hst[(size_t) pos];
                for (int i = 0; i < kTaps; ++i)
                    acc += fir[(size_t) i] * h[i];
                return acc;
            };
            const double m0 = halfband (hist4, hist4Pos, y2[0], y2[1]);
            const double m1 = halfband (hist4, hist4Pos, y2[2], y2[3]);
            const double acc = halfband (hist, histPos, m0, m1);
            if (! gate && vcaRel < 1.0e-5)
                idle = true;
            return (float) acc;
        }

        double cutoffCurve (double c) const noexcept
        {
            const double x = std::clamp (c, 0.0, 1.0) * 4.0;
            const int i = std::min ((int) x, 3);
            const double f = x - i;
            return cal[Cut0 + i] + f * (cal[Cut0 + i + 1] - cal[Cut0 + i]);
        }

    private:
        static constexpr double kPi = 3.14159265358979323846;
        static constexpr int kTaps = 31;
        static constexpr int kOversample = 4;   // the filter at four times the sample rate (its scream reaches 20 kHz)

        Calibration cal;
        double fs = 88200.0, fsOut = 44100.0;
        int wave = 0;
        double phase = 0.0, pitchLog2 = 6.0, targetLog2 = 6.0, tuneOct = 0.0, slideTau = 0.02, slideCoef = 0.0;
        bool sliding = false, gate = false, accentNote = false, idle = true;
        std::array<double, 4> s {};
        double G[4] { 0.0, 0.0, 0.0, 0.0 };
        int coefCountdown = 0;
        double fbState = 0.0, outHpState = 0.0;
        double megRaw = 0.0, meg = 0.0, megMul = 0.0, sweep = 0.0;
        double vcaAtt = 0.0, vcaDec = 1.0, vcaRel = 1.0, accBoost = 0.0;
        // derived
        double pole[4] { 0.12, 1.0, 2.3, 3.5 };
        double megAttCoef = 1.0, chargeCoef = 1.0, releaseCoef = 1.0, vcaAttCoef = 1.0, vcaDecMul = 1.0, vcaRelMul = 0.0,
               accVcaMul = 0.0, fbG = 0.0, outG = 0.0, drive = 1.0, sawGain = 1.0, squareGain = 1.0;
        std::array<double, kTaps> fir {};
        std::array<double, 2 * kTaps> hist {}, hist4 {};
        int histPos = 0, hist4Pos = 0;
        // cached per knob change / pitch change
        Knobs lastKnobs;
        bool knobsValid = false, pitchValid = false;
        double kBase = 0.0, kDepth = 0.0, kSweep = 0.0, kVcaAcc = 0.0, kFb = 0.0, kFbNow = 0.0, kTrack = 0.0, kDrive = 1.0, inc = 0.0;
        static constexpr double kLowC = 6.0313;   // log2 of 65.41 Hz, the 303's low C
        const float* tab = nullptr;

        void knobsChanged (const Knobs& k)
        {
            const double c = std::clamp (k.cutoff, 0.0, 1.0), r = std::clamp (k.resonance, 0.0, 1.0),
                         e = std::clamp (k.envMod, 0.0, 1.0), a = std::clamp (k.accent, 0.0, 1.0);
            kBase = cutoffCurve (c) - cal[EnvOffset] * std::pow (e, cal[EnvOffsetCurve]) + cal[ResShift] * r;
            kDepth = cal[EnvDepthMin] + cal[EnvDepth] * std::pow (e, cal[EnvCurve]);
            kSweep = cal[AccentSweep] * std::pow (a, cal[AccentSweepCurve]);
            kVcaAcc = cal[AccentVca] * std::pow (a, cal[AccentVcaCurve]);
            kFb = cal[ResMax] * std::pow (r, cal[ResCurve]);
            kTrack = k.track;
            kDrive = drive * std::exp2 (5.0 * std::clamp (k.drive, 0.0, 1.0));
            const double st = std::max (0.05, k.sweepTime);
            chargeCoef = coefOf (std::exp (cal[AccentChargeLn]) * st, fs);
            releaseCoef = coefOf (std::exp (cal[AccentReleaseLn]) * st, fs);
            lastKnobs = k;
            knobsValid = true;
        }

        // tanh, near enough (a Pade form, exact slope at 0, levelling at +-1)
        static double softLimit (double x) noexcept
        {
            const double c = std::clamp (x, -3.0, 3.0);
            return c * (27.0 + c * c) / (27.0 + 9.0 * c * c);
        }

        static double coefOf (double tauSeconds, double fsHz) { return 1.0 - std::exp (-1.0 / (std::max (tauSeconds, 1.0e-6) * fsHz)); }
        static double mulOf (double tauSeconds, double fsHz) { return std::exp (-1.0 / (std::max (tauSeconds, 1.0e-6) * fsHz)); }
        static double tptG (double hz, double fsHz) { const double g = std::tan (kPi * std::min (hz, 0.45 * fsHz) / fsHz); return g / (1.0 + g); }

        void derive()
        {
            pole[0] = std::exp (cal[Pole1Ln]); pole[1] = 1.0; pole[2] = std::exp (cal[Pole3Ln]); pole[3] = std::exp (cal[Pole4Ln]);
            megAttCoef = coefOf (std::exp (cal[MegAttackLn]), fs);
            chargeCoef = coefOf (std::exp (cal[AccentChargeLn]), fs);
            releaseCoef = coefOf (std::exp (cal[AccentReleaseLn]), fs);
            vcaAttCoef = coefOf (std::exp (cal[VcaAttackLn]), fs);
            vcaDecMul = mulOf (std::exp (cal[VcaDecayLn]), fs);
            vcaRelMul = mulOf (std::exp (cal[VcaReleaseLn]), fs);
            accVcaMul = mulOf (std::exp (cal[AccentVcaDecayLn]), fs);
            fbG = tptG (std::exp (cal[FeedbackHpLn]), fs);
            outG = tptG (std::exp (cal[OutHpLn]), fs);
            drive = std::max (std::exp (cal[DriveLn]), 1.0e-4);
            sawGain = std::pow (10.0, cal[SawGainDb] / 20.0);
            squareGain = std::pow (10.0, cal[SquareGainDb] / 20.0);
            slideCoef = coefOf (slideTau, fs);
        }
    };
}
