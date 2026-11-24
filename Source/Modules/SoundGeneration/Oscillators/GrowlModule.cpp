// Aquanode Modular - Growl: a bass growl generator (6.9.4)
//
// The growls dubstep is built from: a wavetable phase-modulated by a sine operator (FM), swept by a
// tempo-synced LFO and an envelope (table, FM, vowel, comb, cutoff, pitch dives), through a vowel
// (formant) filter and a comb, driven, folded, squashed by a three-band upward / downward compressor
// and clipped, a clean sine sub under it (the growl high-passed at 80 Hz then) - the sound that is
// then fed into a pitch quantizer (Spectral Pitch Quantizer) for colour bass. Voiced against the
// band balance, brightness and density of reference growls (measured, nothing taken from them).
//
//  - Tables: six made here from formulas (Vowel Saw, FM Growl, Sync Sweep, Fold Square, Formant
//    Square, Metal Comb), 32 frames each, band-limited in seven levels (1024 .. 64 points, 511 .. 7
//    harmonics), built once and shared by every Growl. And Custom, made on the card: Generate (a
//    random recipe: a base wave, a vowel path, a comb, a fold, each moving over the frames), Iterate
//    (the selected table again through a process whose amount sweeps over the frames - Filter Sweep,
//    Formant Sweep, Comb Sweep, Harmonic Shift, Fold, Self FM, Phase Warp, Sync - each frame its own
//    or one frame swept into all; iterate on the result for more), Undo, and, if wanted, from sound:
//    Capture takes a second of Audio In (its pitch found by autocorrelation), Resample a second of
//    Growl's own output while a key is held (the note's own period): 32 periods across it, each
//    resampled to 1024 points, phase-aligned on the fundamental. Saved with the patch.
//  - Per note: up to 7 unison copies (Detune, Width; below 150 Hz the copies stay in the middle),
//    a sine Sub an octave down, Warp (bends the phase), amp ADSR, Glide, Octave; mono (legato) or
//    up to 8 voices; MPE (member channels bend +-48, pressure opens the vowel).
//  - Movement: an LFO at the patch's tempo (1/1 .. 1/32, triplets; Sine, Triangle, Saw Down, Saw Up,
//    Square, Yoy, Steps) and a decay envelope, each with its own depth to the table position, the
//    vowel and the cutoff; the mod wheel moves the table too.
//  - Vowel: three band-passes through u - o - a - e - i (Peterson & Barney's averages), mixed in by
//    Formant, Res their sharpness; then a 12 dB low-pass (Cutoff, Reso) and Drive (a soft clip).
//  - Squash: lows / mids / highs (88 Hz, 2.5 kHz, Linkwitz-Riley), each pushed down above its
//    threshold and up below another (quiet tails and noise floors under -70 dB left alone) - Squash
//    the depth, Time the speed, Upward / Downward their shares, Squash Out the level after.
#include "../../Core/ModuleCore.h"
#include <atomic>
#include <complex>

using namespace aquanode;

namespace growl
{
    constexpr int kFrames = 32, kLevels = 7, kFull = 1024, kMaxH = 511;
    constexpr int kVoices = 8, kUnison = 7;
    const int kLevelSize[kLevels] = { 1024, 512, 256, 128, 64, 64, 64 };
    const int kLevelMaxH[kLevels] = { 511, 255, 127, 63, 31, 15, 7 };

    const juce::StringArray kTableNames { "Vowel Saw", "FM Growl", "Sync Sweep", "Fold Square", "Formant Square", "Metal Comb", "Custom" };
    constexpr int kResynth = 6;
    const juce::StringArray kRateNames { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
    constexpr double kRateBeats[] = { 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
    const juce::StringArray kFmRatioNames { "0.5", "1", "1.5", "2", "3", "4" };
    constexpr float kFmRatios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
    const juce::StringArray kSubOctNames { "-2", "-1", "0" };
    constexpr int kCombMax = 4096;
    const juce::StringArray kShapeNames { "Sine", "Triangle", "Saw Down", "Saw Up", "Square", "Yoy", "Steps" };

    // vowels u, o, a, e, i: F1 - F3 (Hz), Peterson & Barney (1952), averages
    constexpr float kVowelF[5][3] = { { 300, 870, 2240 }, { 570, 840, 2410 }, { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 } };
    constexpr float kVowelGain[3] = { 1.0f, 0.7f, 0.45f };
    // the Iterate processes: each sweeps its amount from the first frame to the last
    const juce::StringArray kIterateNames { "Filter Sweep", "Formant Sweep", "Comb Sweep", "Harmonic Shift", "Fold", "Self FM", "Phase Warp", "Sync" };
    inline void vowelAt (float v01, float* f)
    {
        const float x = juce::jlimit (0.0f, 1.0f, v01) * 4.0f;
        const int i = juce::jmin (3, (int) x); const float u = x - (float) i;
        for (int k = 0; k < 3; ++k) f[k] = std::exp ((1.0f - u) * std::log (kVowelF[i][k]) + u * std::log (kVowelF[i + 1][k]));
    }

    // a band-limited wavetable: kFrames frames, each in kLevels sizes (plus one wrap sample)
    struct Table
    {
        std::vector<float> data;
        int offset[kFrames][kLevels] {};
        void allocate()
        {
            int n = 0;
            for (int f = 0; f < kFrames; ++f) for (int l = 0; l < kLevels; ++l) { offset[f][l] = n; n += kLevelSize[l] + 1; }
            data.assign ((size_t) n, 0.0f);
        }
        // spectra: per frame kMaxH + 1 complex harmonics (index = harmonic number)
        void build (const std::vector<std::vector<std::complex<float>>>& spectra)
        {
            if (data.empty()) allocate();
            for (int f = 0; f < kFrames; ++f)
                for (int l = 0; l < kLevels; ++l)
                {
                    const int n = kLevelSize[l], order = (int) std::lround (std::log2 ((double) n));
                    juce::dsp::FFT fft (order);
                    std::vector<std::complex<float>> in ((size_t) n), out ((size_t) n);
                    for (int h = 1; h <= kLevelMaxH[l] && h < n / 2; ++h)
                    {
                        in[(size_t) h] = spectra[(size_t) f][(size_t) h];
                        in[(size_t) (n - h)] = std::conj (spectra[(size_t) f][(size_t) h]);
                    }
                    fft.perform (in.data(), out.data(), true);   // inverse (JUCE scales by 1/n)
                    float* d = data.data() + offset[f][l];
                    for (int i = 0; i < n; ++i) d[i] = out[(size_t) i].real() * (float) n * 0.5f;
                    d[n] = d[0];
                }
            // each frame to the same loudness (its full-size level), no frame over full scale
            for (int f = 0; f < kFrames; ++f)
            {
                const float* d = data.data() + offset[f][0];
                double e = 0.0; float pk = 0.0f;
                for (int i = 0; i < kFull; ++i) { e += (double) d[i] * d[i]; pk = juce::jmax (pk, std::abs (d[i])); }
                const float rms = (float) std::sqrt (e / kFull);
                const float g = rms > 1.0e-9f ? juce::jmin (0.35f / rms, 0.98f / juce::jmax (1.0e-9f, pk)) : 0.0f;
                for (int l = 0; l < kLevels; ++l) { float* q = data.data() + offset[f][l]; for (int i = 0; i <= kLevelSize[l]; ++i) q[i] *= g; }
            }
        }
        float read (float pos01, int level, double phase) const noexcept
        {
            const float fp = juce::jlimit (0.0f, 1.0f, pos01) * (float) (kFrames - 1);
            const int f0 = juce::jmin (kFrames - 2, (int) fp); const float fu = fp - (float) f0;
            const int n = kLevelSize[level];
            const double x = phase * n; const int i = (int) x; const float u = (float) (x - i);
            const float* a = data.data() + offset[f0][level];
            const float* b = data.data() + offset[f0 + 1][level];
            const float va = a[i] + u * (a[i + 1] - a[i]), vb = b[i] + u * (b[i + 1] - b[i]);
            return va + fu * (vb - va);
        }
    };

    // a single cycle (8192 points) to its harmonics 1 .. kMaxH
    inline std::vector<std::complex<float>> spectrumOf (const std::vector<float>& cycle)
    {
        const int n = (int) cycle.size(), order = (int) std::lround (std::log2 ((double) n));
        juce::dsp::FFT fft (order);
        std::vector<std::complex<float>> in ((size_t) n), out ((size_t) n);
        for (int i = 0; i < n; ++i) in[(size_t) i] = cycle[(size_t) i];
        fft.perform (in.data(), out.data(), false);
        std::vector<std::complex<float>> s ((size_t) kMaxH + 1);
        for (int h = 1; h <= kMaxH && h < n / 2; ++h) s[(size_t) h] = out[(size_t) h] * (2.0f / (float) n);
        return s;
    }

    // a frame's harmonics back to one cycle of n points
    inline std::vector<float> cycleOf (const std::vector<std::complex<float>>& s, int n)
    {
        const int order = (int) std::lround (std::log2 ((double) n));
        juce::dsp::FFT fft (order);
        std::vector<std::complex<float>> in ((size_t) n), out ((size_t) n);
        for (int h = 1; h <= kMaxH && h < n / 2; ++h) { in[(size_t) h] = s[(size_t) h]; in[(size_t) (n - h)] = std::conj (s[(size_t) h]); }
        fft.perform (in.data(), out.data(), true);
        std::vector<float> y ((size_t) n);
        for (int i = 0; i < n; ++i) y[(size_t) i] = out[(size_t) i].real() * (float) n * 0.5f;
        return y;
    }
    // the gain of a vowel at a harmonic (formants placed for E2)
    inline float vowelGain (float v01, int h)
    {
        float fr[3]; vowelAt (v01, fr);
        const float hz = h * 82.4f;
        float g = 0.12f;
        for (int k = 0; k < 3; ++k) { const float bw = 0.12f * fr[k] + 60.0f, d = (hz - fr[k]) / bw; g += kVowelGain[k] * 2.2f / (1.0f + d * d); }
        return g;
    }
    // one frame through an Iterate process at an amount (0 .. 1)
    inline std::vector<std::complex<float>> processFrame (const std::vector<std::complex<float>>& src, int proc, float a)
    {
        std::vector<std::complex<float>> s = src;
        switch (proc)
        {
            case 0:   // a resonant low-pass from the 2nd harmonic up to the 150th
            {
                const float hc = 2.0f * std::pow (75.0f, a), q = 3.0f;
                for (int h = 1; h <= kMaxH; ++h) { const float r = (float) h / hc; s[(size_t) h] *= 1.0f / std::complex<float> (1.0f - r * r, r / q); }
                return s;
            }
            case 1:   // the vowels u .. i
                for (int h = 1; h <= kMaxH; ++h) s[(size_t) h] *= vowelGain (a, h);
                return s;
            case 2:   // a resonant comb, its teeth closing in
            {
                const float d = 0.02f + 0.28f * a;
                for (int h = 1; h <= kMaxH; ++h) s[(size_t) h] *= 1.0f / (1.0f - 0.8f * std::polar (1.0f, -juce::MathConstants<float>::twoPi * h * d));
                return s;
            }
            case 3:   // the harmonics moved up by 0 .. 12
            {
                const int k = (int) std::lround (a * 12.0f);
                std::vector<std::complex<float>> t ((size_t) kMaxH + 1);
                for (int h = 1 + k; h <= kMaxH; ++h) t[(size_t) h] = s[(size_t) (h - k)];
                if (k > 0) t[1] = s[1] * 0.5f;   // keep a little of the fundamental under it
                return t;
            }
            default: break;
        }
        // the shaping ones work on the cycle itself, 8192 points
        const int n = 8192;
        auto x = cycleOf (s, n);
        float pk = 1.0e-9f; for (float v : x) pk = juce::jmax (pk, std::abs (v));
        for (auto& v : x) v /= pk;
        std::vector<float> y ((size_t) n);
        auto at = [&x, n] (double ph) { ph -= std::floor (ph); const double t = ph * n; const int i = (int) t; const float u = (float) (t - i); return x[(size_t) (i % n)] + u * (x[(size_t) ((i + 1) % n)] - x[(size_t) (i % n)]); };
        for (int i = 0; i < n; ++i)
        {
            const double ph = (double) i / n;
            if (proc == 4) y[(size_t) i] = std::sin (juce::MathConstants<float>::halfPi * (1.0f + 5.0f * a) * x[(size_t) i]);
            else if (proc == 5) y[(size_t) i] = at (ph + 0.35 * a * x[(size_t) i]);
            else if (proc == 6) y[(size_t) i] = at (std::pow (ph, std::pow (2.0, -3.0 * a)));
            else { const double r = 1.0 + 3.0 * a; y[(size_t) i] = at (ph * r) * (float) (1.0 - 0.3 * a * ph); }
        }
        return spectrumOf (y);
    }

    // the formula tables
    inline std::vector<std::vector<std::complex<float>>> makeSpectra (int type)
    {
        std::vector<std::vector<std::complex<float>>> sp ((size_t) kFrames);
        const float kRef = 82.4f;   // formants placed for E2: harmonic = Hz / 82.4
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        for (int f = 0; f < kFrames; ++f)
        {
            const float t = (float) f / (float) (kFrames - 1);
            std::vector<std::complex<float>> s ((size_t) kMaxH + 1);
            if (type == 0 || type == 4)
            {
                // a saw (all harmonics, 1/h) or a square (odd, 1/h) through a vowel moving u .. i
                float fr[3]; vowelAt (type == 0 ? t : 1.0f - t, fr);
                for (int h = 1; h <= kMaxH; ++h)
                {
                    if (type == 4 && (h % 2) == 0) continue;
                    const float hz = h * kRef;
                    float g = 0.12f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float bw = 0.12f * fr[k] + 60.0f, d = (hz - fr[k]) / bw;
                        g += kVowelGain[k] * 2.2f / (1.0f + d * d);
                    }
                    s[(size_t) h] = std::polar (g / (float) h, -juce::MathConstants<float>::halfPi);
                }
            }
            else if (type == 5)
            {
                // a saw through a moving comb: notches sweeping through the harmonics
                const float d = 0.02f + 0.23f * t;
                for (int h = 1; h <= kMaxH; ++h)
                {
                    const float c = std::abs (std::cos (juce::MathConstants<float>::pi * h * d));
                    s[(size_t) h] = std::polar ((0.08f + c) / (float) h, -juce::MathConstants<float>::halfPi);
                }
            }
            else
            {
                std::vector<float> cyc (8192);
                for (int i = 0; i < 8192; ++i)
                {
                    const float ph = (float) i / 8192.0f;
                    float y = 0.0f;
                    if (type == 1)
                    {
                        // two-operator FM: the index rising 0 .. 9, a hint of feedback on the modulator
                        const float idx = 9.0f * t * t;
                        const float mod = std::sin (twoPi * ph + 0.4f * t * std::sin (twoPi * ph));
                        y = std::sin (twoPi * ph + idx * mod);
                    }
                    else if (type == 2)
                    {
                        // a saw synced to the cycle, the slave 1 .. 7 times as fast
                        const float r = 1.0f + 6.0f * t;
                        const float sp2 = std::fmod (ph * r, 1.0f);
                        y = 1.0f - 2.0f * sp2;
                        y *= 1.0f - 0.35f * ph;   // a gentle slope over the cycle, as a synced saw rings down
                    }
                    else
                    {
                        // a square folded harder and harder
                        const float sq = (ph < 0.5f ? 1.0f : -1.0f) * (0.6f + 0.4f * std::sin (twoPi * ph));
                        const float g = 1.0f + 5.0f * t;
                        y = std::sin (juce::MathConstants<float>::halfPi * g * sq);
                    }
                    cyc[(size_t) i] = y;
                }
                s = spectrumOf (cyc);
            }
            sp[(size_t) f] = std::move (s);
        }
        return sp;
    }

    inline const Table& formulaTable (int type)
    {
        static const std::vector<Table> tables = []
        {
            std::vector<Table> t (6);
            for (int i = 0; i < 6; ++i) t[(size_t) i].build (makeSpectra (i));
            return t;
        }();
        return tables[(size_t) juce::jlimit (0, 5, type)];
    }

    // a state-variable filter (Simper)
    struct Svf
    {
        float ic1 { 0 }, ic2 { 0 };
        float g { 0 }, k { 1 }, a1 { 1 }, a2 { 0 }, a3 { 0 };
        void set (float hz, float q, float fs)
        {
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, 0.45f * fs, hz) / fs);
            k = 1.0f / juce::jmax (0.3f, q); a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void process (float x, float& lp, float& bp)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2; lp = v2; bp = v1;
        }
        void reset() { ic1 = ic2 = 0.0f; }
    };
    struct Biquad
    {
        float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, z1 { 0 }, z2 { 0 };
        void set (double f, double fs, int kind)   // 0 LP, 1 HP, 2 allpass (Butterworth poles)
        {
            const double w = juce::MathConstants<double>::twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7071067811865476), a0 = 1.0 + al;
            if (kind == 0)      { b0 = (float) ((1 - c) * 0.5 / a0); b1 = (float) ((1 - c) / a0); b2 = b0; }
            else if (kind == 1) { b0 = (float) ((1 + c) * 0.5 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0; }
            else                { b0 = (float) ((1 - al) / a0); b1 = (float) (-2 * c / a0); b2 = 1.0f; }
            a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
        }
        float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void reset() { z1 = z2 = 0.0f; }
    };

    struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
    constexpr int kLoopPresets = 12;   // (the first twelve; the one-hits after them - 6.9.11)
    inline const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> p {
            { "Yoy Growl", {} },   // the defaults: FM Growl, 1/16 Yoy on table, FM, vowel and comb, drive, clip, squash
            { "Neuro Growl", { { "table", 1 }, { "pos", 50 }, { "fm", 45 }, { "fmRatio", 3 }, { "rate", 6 }, { "shape", 2 }, { "lfoTable", 60 }, { "lfoFm", 50 },
                               { "lfoVowel", 30 }, { "comb", 50 }, { "combFreq", 400 }, { "lfoComb", 80 }, { "fold", 35 }, { "drive", 65 } } },
            { "Wub Wobble", { { "table", 0 }, { "pos", 30 }, { "fm", 10 }, { "rate", 5 }, { "shape", 0 }, { "lfoTable", 20 }, { "lfoFm", 0 }, { "lfoVowel", 20 },
                              { "lfoComb", 0 }, { "comb", 0 }, { "cutoff", 300 }, { "reso", 55 }, { "lfoCutoff", 90 }, { "drive", 50 }, { "clip", 55 } } },
            { "Talking Growl", { { "table", 0 }, { "pos", 55 }, { "fm", 20 }, { "rate", 3 }, { "shape", 1 }, { "lfoTable", 30 }, { "lfoFm", 30 }, { "lfoVowel", 100 },
                                 { "vowel", 0 }, { "formant", 80 }, { "fres", 65 }, { "comb", 15 }, { "drive", 50 } } },
            { "Riddim Square", { { "table", 4 }, { "pos", 35 }, { "fm", 15 }, { "rate", 6 }, { "shape", 4 }, { "lfoTable", 45 }, { "lfoFm", 20 }, { "lfoVowel", 30 },
                                 { "comb", 45 }, { "combFreq", 160 }, { "lfoComb", 60 }, { "formant", 40 }, { "drive", 50 }, { "fold", 20 } } },
            { "Screech", { { "table", 2 }, { "pos", 70 }, { "fm", 55 }, { "fmRatio", 4 }, { "rate", 7 }, { "shape", 6 }, { "lfoTable", 40 }, { "lfoFm", 60 },
                           { "envPitch", 12 }, { "envDecay", 120 }, { "comb", 30 }, { "combFreq", 900 }, { "drive", 60 }, { "octave", 1 }, { "sub", 40 } } },
            { "Dive Bomb", { { "table", 1 }, { "pos", 40 }, { "fm", 35 }, { "envPitch", 24 }, { "envDecay", 350 }, { "envFm", 50 }, { "rate", 4 }, { "lfoTable", 30 },
                             { "lfoFm", 20 }, { "lfoVowel", 40 }, { "comb", 25 }, { "drive", 55 } } },
            { "Metal Comb", { { "table", 5 }, { "pos", 10 }, { "fm", 25 }, { "rate", 6 }, { "shape", 6 }, { "lfoTable", 80 }, { "lfoFm", 30 }, { "lfoVowel", 0 },
                              { "comb", 65 }, { "combFreq", 300 }, { "lfoComb", 100 }, { "formant", 20 }, { "fold", 30 } } },
            { "Colour Bass Feed", { { "table", 0 }, { "pos", 60 }, { "fm", 25 }, { "rate", 4 }, { "shape", 0 }, { "lfoTable", 15 }, { "lfoFm", 15 }, { "lfoVowel", 25 },
                                    { "lfoComb", 0 }, { "comb", 10 }, { "formant", 40 }, { "unison", 3 }, { "detune", 12 }, { "sub", 0 }, { "drive", 45 }, { "clip", 50 } } },
            { "Fold Grind", { { "table", 3 }, { "pos", 15 }, { "fm", 20 }, { "lfoTable", 0 }, { "lfoVowel", 35 }, { "envTable", 70 }, { "envFm", 40 }, { "envDecay", 600 },
                              { "fold", 60 }, { "drive", 60 }, { "comb", 20 } } },
            { "Reese Growl", { { "table", 2 }, { "pos", 25 }, { "fm", 15 }, { "rate", 0 }, { "shape", 0 }, { "lfoTable", 40 }, { "lfoFm", 20 }, { "lfoVowel", 10 },
                               { "lfoComb", 30 }, { "lfoCutoff", 30 }, { "unison", 5 }, { "detune", 28 }, { "width", 70 }, { "comb", 25 }, { "cutoff", 6000 } } },
            { "Roll 1/32T", { { "table", 1 }, { "pos", 45 }, { "fm", 40 }, { "fmRatio", 3 }, { "rate", 9 }, { "shape", 6 }, { "lfoTable", 70 }, { "lfoFm", 60 },
                              { "lfoVowel", 40 }, { "comb", 40 }, { "lfoComb", 70 }, { "fold", 25 } } },
            // (6.9.11) one-hits, after Vita's one-hit growl: a fast attack, a second or so
            // of decay into almost nothing, the envelope sweeping the table, FM and
            // pitch, the slow LFO falling once over the note, comb and sub underneath; no
            // squash and little drive or clip (they would lift the tail back up)
            { "Abyss Hit", { { "table", 1 }, { "pos", 40 }, { "fm", 35 }, { "fmRatio", 3 }, { "envTable", 60 }, { "envFm", 50 }, { "envDecay", 900 },
                             { "rate", 1 }, { "shape", 2 }, { "lfoTable", 30 }, { "lfoFm", 20 }, { "lfoVowel", 20 }, { "lfoCutoff", 50 }, { "cutoff", 2500 },
                             { "reso", 30 }, { "comb", 60 }, { "combFreq", 220 }, { "lfoComb", 40 }, { "sub", 80 },
                             { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 2 }, { "decay", 1100 }, { "sustain", 5 }, { "release", 1000 } } },
            { "Pulse Hit", { { "table", 0 }, { "pos", 45 }, { "unison", 5 }, { "detune", 20 }, { "envTable", 70 }, { "envDecay", 600 }, { "rate", 2 },
                             { "shape", 0 }, { "lfoVowel", 60 }, { "lfoComb", 30 }, { "comb", 50 }, { "cutoff", 1800 }, { "lfoCutoff", 60 }, { "sub", 70 },
                             { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 1 }, { "decay", 900 }, { "sustain", 4 }, { "release", 900 } } },
            { "Dive Hit", { { "table", 2 }, { "pos", 30 }, { "fm", 25 }, { "envPitch", 12 }, { "envDecay", 300 }, { "envTable", 40 }, { "rate", 1 },
                            { "shape", 2 }, { "lfoTable", 30 }, { "lfoVowel", 30 }, { "comb", 40 }, { "sub", 75 },
                            { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 1 }, { "decay", 1200 }, { "sustain", 6 }, { "release", 1000 } } },
            { "Comb Hit", { { "table", 5 }, { "pos", 20 }, { "fm", 20 }, { "comb", 75 }, { "combFreq", 160 }, { "lfoComb", 70 }, { "rate", 1 },
                            { "shape", 2 }, { "lfoTable", 40 }, { "lfoVowel", 0 }, { "envFm", 60 }, { "envDecay", 700 }, { "sub", 70 },
                            { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 2 }, { "decay", 1000 }, { "sustain", 3 }, { "release", 900 } } },
            { "Formant Hit", { { "table", 4 }, { "pos", 40 }, { "formant", 70 }, { "fres", 60 }, { "lfoVowel", 80 }, { "rate", 1 }, { "shape", 3 },
                               { "envTable", 40 }, { "envDecay", 1000 }, { "comb", 30 }, { "sub", 70 },
                               { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 2 }, { "decay", 1100 }, { "sustain", 3 }, { "release", 1100 } } },
            { "Fold Hit", { { "table", 3 }, { "pos", 25 }, { "fold", 50 }, { "envFm", 50 }, { "envTable", 50 }, { "envDecay", 800 }, { "rate", 0 },
                            { "shape", 2 }, { "lfoVowel", 30 }, { "sub", 70 },
                            { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 1 }, { "decay", 1000 }, { "sustain", 4 }, { "release", 900 } } },
            { "Grind Hit", { { "table", 2 }, { "pos", 50 }, { "unison", 3 }, { "detune", 25 }, { "fm", 45 }, { "fmRatio", 4 }, { "envFm", 70 },
                             { "envDecay", 500 }, { "rate", 3 }, { "shape", 6 }, { "lfoTable", 50 }, { "comb", 45 }, { "sub", 70 },
                             { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 1 }, { "decay", 900 }, { "sustain", 5 }, { "release", 850 } } },
            { "Sub Hit", { { "table", 1 }, { "pos", 20 }, { "fm", 20 }, { "sub", 100 }, { "subOct", 1 }, { "cutoff", 900 }, { "lfoCutoff", 40 },
                           { "rate", 0 }, { "shape", 2 }, { "envTable", 40 }, { "envDecay", 1000 }, { "drive", 45 },
                           { "squash", 0 }, { "drive", 20 }, { "clip", 15 }, { "attack", 2 }, { "decay", 1300 }, { "sustain", 8 }, { "release", 1200 } } },
        };
        return p;
    }
}

using namespace growl;

class GrowlModule final : public SynthModule
{
public:
    GrowlModule()
    {
        for (auto& t : resynth) t.allocate();
        // a plain saw until something is captured
        std::vector<std::vector<std::complex<float>>> sp ((size_t) kFrames, std::vector<std::complex<float>> ((size_t) kMaxH + 1));
        for (auto& s : sp) for (int h = 1; h <= kMaxH; ++h) s[(size_t) h] = std::polar (1.0f / (float) h, -juce::MathConstants<float>::halfPi);
        resynth[0].build (sp);
        capture.assign (96000 * 2, 0.0f);
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    double voiceTailSeconds() const override { return 0.0; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fs = (float) juce::jmax (8000.0, sr);
        (void) formulaTable (0);   // built once, here and not on the audio thread
        // the squash's crossovers: LR4 at 88 Hz and 2.5 kHz, the low band through 2.5 kHz's allpass
        static const int kind[9] = { 0, 0, 1, 1, 0, 0, 1, 1, 2 };
        static const double at[9] = { 88, 88, 88, 88, 2500, 2500, 2500, 2500, 2500 };
        for (auto& c : bandState) for (int i = 0; i < 9; ++i) c[i].set (at[i], sr, kind[i]);
        monoHp.set (150.0, sr, 1); monoHp2.set (150.0, sr, 1);
        for (auto& h : growlHp) h.set (80.0, sr, 1);
        reset();
    }
    void reset() override
    {
        for (auto& v : voices) v = Voice();
        for (auto& c : bandState) for (auto& b : c) b.reset();
        for (auto& e : env) e = 0.0f;
        held.clear(); lfoPhase = 0.0; stepValue = 0.5f; stepTarget = 0.5f;
        monoHp.reset(); monoHp2.reset();
        for (auto& h : growlHp) h.reset();
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        const int ch = juce::jlimit (1, 16, m.getChannel());
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
        else if (m.isNoteOff()) noteOff (m.getNoteNumber(), ch);
        else if (m.isPitchWheel())
        {
            const float b = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            if (ch == 1) masterBend = b * 2.0f; else chanBend[ch] = b * 48.0f;
        }
        else if (m.isChannelPressure()) chanPressure[ch] = m.getChannelPressureValue() / 127.0f;
        else if (m.isController() && m.getControllerNumber() == 74 && ch >= 2) chanSlide[ch] = m.getControllerValue() / 127.0f;   // (6.9.12)
        else if (m.isController())
        {
            if (m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
            else if (m.getControllerNumber() == 120 || m.getControllerNumber() == 123) allOff();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    void midiAllNotesOff() override { allOff(); }

    void blockStart() override
    {
        tableType = juce::jlimit (0, kResynth, (int) std::lround (p ("table")));
        table = tableType == kResynth ? &resynth[(size_t) activeResynth.load (std::memory_order_acquire)] : &formulaTable (tableType);
        pos = p ("pos") * 0.01f; warp = p ("warp") * 0.01f; sub = p ("sub") * 0.01f;
        slideFree = ! mpeSlideDefaultOff() && ! isParamCabled (paramIndex ("pos"));
        unison = juce::jlimit (1, kUnison, (int) std::lround (p ("unison")));
        detune = p ("detune") * 0.01f; width = p ("width") * 0.01f;
        for (int u = 0; u < kUnison; ++u)
        {
            const float spread = unison > 1 ? (2.0f * (float) u / (float) (unison - 1) - 1.0f) : 0.0f;
            unisonRatio[u] = std::pow (2.0, spread * detune * 0.5 / 12.0);
        }
        warpExp = std::pow (2.0, -2.0 * warp);
        rate = juce::jlimit (0, 9, (int) std::lround (p ("rate"))); shape = juce::jlimit (0, 6, (int) std::lround (p ("shape")));
        fm = p ("fm") * 0.01f; fmRatio = kFmRatios[juce::jlimit (0, 5, (int) std::lround (p ("fmRatio")))];
        lfoFm = p ("lfoFm") * 0.01f; lfoComb = p ("lfoComb") * 0.01f; envFm = p ("envFm") * 0.01f; envPitch = p ("envPitch");
        comb = p ("comb") * 0.01f; combHz = p ("combFreq"); fold = p ("fold") * 0.01f; clip = p ("clip") * 0.01f;
        subOct = juce::jlimit (0, 2, (int) std::lround (p ("subOct"))) - 2;
        lfoTable = p ("lfoTable") * 0.01f; lfoVowel = p ("lfoVowel") * 0.01f; lfoCutoff = p ("lfoCutoff") * 0.01f;
        envTable = p ("envTable") * 0.01f; envDecay = juce::jmax (0.005f, p ("envDecay") * 0.001f);
        vowel = p ("vowel") * 0.01f; formant = p ("formant") * 0.01f; fres = p ("fres") * 0.01f;
        cutoff = p ("cutoff"); reso = p ("reso") * 0.01f; drive = p ("drive") * 0.01f;
        squash = p ("squash") * 0.01f; squashTime = juce::jmax (0.1f, p ("squashTime") * 0.01f);
        upward = p ("upward") * 0.01f; downward = p ("downward") * 0.01f; squashOut = juce::Decibels::decibelsToGain (p ("squashOut"));
        poly = juce::jlimit (1, kVoices, (int) std::lround (p ("voices")));
        glide = p ("glide") * 0.001f; octave = (int) std::lround (p ("octave"));
        attack = juce::jmax (0.001f, p ("attack") * 0.001f); decay = juce::jmax (0.005f, p ("decay") * 0.001f);
        sustain = p ("sustain") * 0.01f; release = juce::jmax (0.005f, p ("release") * 0.001f);
        level = p ("level");
        // the squash's speed per band (lows slower)
        static const float att[3] = { 0.030f, 0.012f, 0.006f }, rel[3] = { 0.250f, 0.140f, 0.090f };
        for (int b = 0; b < 3; ++b)
        {
            attC[b] = 1.0f - std::exp (-1.0f / (att[b] * squashTime * fs));
            relC[b] = 1.0f - std::exp (-1.0f / (rel[b] * squashTime * fs));
        }
        beatsPerSample = juce::jmax (20.0, tempoBpm) / 60.0 / fs;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        // the capture: Audio In, or Growl's own output (below)
        if (captureMode.load (std::memory_order_relaxed) == 1) feedCapture (0.5f * (inputs[0][0] + inputs[0][1]), true);

        // the LFO, at the patch's tempo (restarted by a first key)
        lfoPhase += beatsPerSample / kRateBeats[rate];
        if (lfoPhase >= 1.0) { lfoPhase -= std::floor (lfoPhase); stepTarget = rng.nextFloat(); }
        stepValue += 0.02f * (stepTarget - stepValue);
        const float lfo = lfoValue ((float) lfoPhase);

        float outL = 0.0f, outR = 0.0f;
        subOut = 0.0f;
        bool any = false;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            any = true;
            renderVoice (v, lfo, outL, outR);
        }
        if (! any && captureMode.load (std::memory_order_relaxed) != 2) { outputs[0][0] = outputs[0][1] = 0.0f; settle(); return; }

        // the copies' width only above 150 Hz: the side high-passed
        {
            const float mid = 0.5f * (outL + outR), side = monoHp2.process (monoHp.process (0.5f * (outL - outR)));
            outL = mid + side; outR = mid - side;
        }
        // with a sub under it the growl leaves the lows to it (high-passed at 80 Hz)
        if (sub > 0.0f) { outL = growlHp[1].process (growlHp[0].process (outL)); outR = growlHp[3].process (growlHp[2].process (outR)); }
        // Drive (a soft clip up to +30 dB into it), Fold, the Squash, the Clip
        if (drive > 0.0f)
        {
            const float g = std::pow (10.0f, 1.5f * drive), comp = 1.0f / (0.55f + 0.45f * std::sqrt (g));
            outL = std::tanh (outL * g) * comp; outR = std::tanh (outR * g) * comp;
        }
        if (fold > 0.0f)
        {
            const float g = 1.0f + 6.0f * fold;
            outL = std::sin (juce::MathConstants<float>::halfPi * g * outL) / (1.0f + 0.3f * fold);
            outR = std::sin (juce::MathConstants<float>::halfPi * g * outR) / (1.0f + 0.3f * fold);
        }
        if (squash > 0.0f) { outL = squashChannel (0, outL); outR = squashChannel (1, outR); }
        if (clip > 0.0f)
        {
            // a hard-kneed clip: dense, loud, its peaks cut
            const float g = std::pow (10.0f, 0.6f * clip);
            auto c = [] (float x) { const float x4 = x * x * x * x; return x / std::sqrt (std::sqrt (1.0f + x4 * x4)); };
            outL = c (outL * g) * 0.9f; outR = c (outR * g) * 0.9f;
        }
        outL = (outL + subOut) * level * kOut; outR = (outR + subOut) * level * kOut;
        if (! std::isfinite (outL) || ! std::isfinite (outR)) { reset(); outL = outR = 0.0f; }
        if (captureMode.load (std::memory_order_relaxed) == 2) feedCapture (0.5f * (outL + outR), false);
        outputs[0][0] = outL; outputs[0][1] = outR;
    }

    // ---- the card's side (message thread)
    void startCapture (bool ownOutput)
    {
        captured = 0; captureStarted = false; capturePeriod = 0.0;
        captureMode.store (ownOutput ? 2 : 1, std::memory_order_release);
    }
    int captureState() const { return captureMode.load (std::memory_order_acquire); }   // 0 idle, 1/2 recording, 3 done, 4 failed
    juce::String captureMessage() const { return lastCaptureMessage; }
    // after a capture: the table from it (message thread), then the Table switched to Custom
    bool finishCapture()
    {
        if (captureMode.load (std::memory_order_acquire) != 3) return false;
        const bool ok = buildResynth (capture.data(), captured, capturePeriod);
        captureMode.store (0, std::memory_order_release);
        if (ok) setParameter ("table", (float) kResynth);
        return ok;
    }
    const Table& shownTable() const
    {
        const int t = juce::jlimit (0, kResynth, (int) std::lround (getParameter ("table")));
        return t == kResynth ? resynth[(size_t) activeResynth.load()] : formulaTable (t);
    }
    float shownPosition() const { return shownPos.load (std::memory_order_relaxed); }
    void applyPreset (int index)
    {
        const auto& all = presets();
        if (index < 0 || index >= (int) all.size()) return;
        for (const auto& spec : descriptor().params)
            if (! spec.calibration && ! spec.hidden) setParameter (spec.id, spec.defaultValue);
        for (const auto& [id, v] : all[(size_t) index].values) setParameter (id, v);
    }

    // the captured table goes with the patch: its 32 frames at 1024 points, 16 bit
    juce::String saveCustomState() const override
    {
        if (! hasResynth) return {};
        juce::MemoryOutputStream out;
        out.writeInt (kFrames); out.writeInt (kFull);
        const auto& t = resynth[(size_t) activeResynth.load()];
        float pk = 1.0e-9f;
        for (int f = 0; f < kFrames; ++f) for (int i = 0; i < kFull; ++i) pk = juce::jmax (pk, std::abs (t.data[(size_t) (t.offset[f][0] + i)]));
        out.writeFloat (pk);
        for (int f = 0; f < kFrames; ++f)
            for (int i = 0; i < kFull; ++i)
                out.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround (t.data[(size_t) (t.offset[f][0] + i)] / pk * 32767.0f)));
        return juce::Base64::toBase64 (out.getData(), out.getDataSize());
    }
    void loadCustomState (const juce::String& state) override
    {
        juce::MemoryOutputStream raw;
        if (state.isEmpty() || ! juce::Base64::convertFromBase64 (raw, state)) return;
        juce::MemoryInputStream in (raw.getData(), raw.getDataSize(), false);
        if (in.readInt() != kFrames || in.readInt() != kFull) return;
        const float pk = in.readFloat();
        std::vector<std::vector<std::complex<float>>> sp;
        std::vector<float> frame ((size_t) kFull);
        for (int f = 0; f < kFrames; ++f)
        {
            for (int i = 0; i < kFull; ++i) frame[(size_t) i] = in.readShort() / 32767.0f * pk;
            sp.push_back (spectrumOf (frame));
        }
        publishResynth (sp);
    }

    // Generate: a new Custom table from a random recipe - a base wave, a vowel path, a comb and a
    // fold, each moving over the frames along its own curve
    void generate (uint32_t seed)
    {
        juce::Random r ((juce::int64) seed);
        const float slope = 0.7f + 0.8f * r.nextFloat();
        const bool odd = r.nextFloat() < 0.35f;
        const float pulse = r.nextFloat() < 0.3f ? 0.1f + 0.3f * r.nextFloat() : 0.0f;
        const float fmRatio = r.nextFloat() < 0.35f ? (float) (1 + r.nextInt (3)) : 0.0f;
        const float v0 = r.nextFloat(), v1 = r.nextFloat(), vowelMix = 0.4f + 0.6f * r.nextFloat();
        const float comb0 = r.nextFloat() < 0.4f ? 0.02f + 0.1f * r.nextFloat() : -1.0f, comb1 = 0.05f + 0.25f * r.nextFloat();
        const float foldMax = r.nextFloat() < 0.5f ? 1.0f + 4.0f * r.nextFloat() : 0.0f;
        const float curve = 0.5f + 1.5f * r.nextFloat();
        std::vector<std::vector<std::complex<float>>> sp;
        for (int f = 0; f < kFrames; ++f)
        {
            const float a = std::pow ((float) f / (float) (kFrames - 1), curve);
            std::vector<std::complex<float>> s ((size_t) kMaxH + 1);
            for (int h = 1; h <= kMaxH; ++h)
            {
                if (odd && (h % 2) == 0) continue;
                float g = 1.0f / std::pow ((float) h, slope);
                if (pulse > 0.0f) g *= std::abs (std::sin (juce::MathConstants<float>::pi * h * pulse)) * 1.6f;
                s[(size_t) h] = std::polar (g, -juce::MathConstants<float>::halfPi);
            }
            if (fmRatio > 0.0f)
            {
                std::vector<float> cyc (8192);
                const float idx = 6.0f * a;
                for (int i = 0; i < 8192; ++i) { const float ph = juce::MathConstants<float>::twoPi * (float) i / 8192.0f; cyc[(size_t) i] = std::sin (ph + idx * std::sin (fmRatio * ph)); }
                s = spectrumOf (cyc);
            }
            if (foldMax > 0.0f) s = processFrame (s, 4, a * foldMax / 5.0f);
            const float v = v0 + (v1 - v0) * a;
            for (int h = 1; h <= kMaxH; ++h) s[(size_t) h] *= (1.0f - vowelMix) + vowelMix * vowelGain (v, h);
            if (comb0 > 0.0f) s = processFrame (s, 2, ((comb0 + (comb1 - comb0) * a) - 0.02f) / 0.28f);
            sp.push_back (std::move (s));
        }
        publishResynth (sp);
        setParameter ("table", (float) kResynth);
    }
    // Iterate: the table now selected, through a process whose amount sweeps over the frames - each
    // frame its own (across the table) or the frame at Position, swept into all 32
    void iterate (int process, bool fromPosition)
    {
        const Table& src = shownTable();
        if (src.data.empty()) return;
        const int at = juce::jlimit (0, kFrames - 1, (int) std::lround (getParameter ("pos") * 0.01f * (kFrames - 1)));
        std::vector<float> frame ((size_t) kFull);
        std::vector<std::vector<std::complex<float>>> sp;
        for (int f = 0; f < kFrames; ++f)
        {
            const int from = fromPosition ? at : f;
            for (int i = 0; i < kFull; ++i) frame[(size_t) i] = src.data[(size_t) (src.offset[from][0] + i)];
            sp.push_back (processFrame (spectrumOf (frame), juce::jlimit (0, 7, process), (float) f / (float) (kFrames - 1)));
        }
        publishResynth (sp);
        setParameter ("table", (float) kResynth);
    }
    // Undo: back to the Custom table before the last one made
    bool undo()
    {
        if (! hasPrevious) return false;
        activeResynth.store (1 - activeResynth.load(), std::memory_order_release);
        setParameter ("table", (float) kResynth);
        return true;
    }

    bool resynthForTest (const float* x, int n, double period) { return buildResynth (x, n, period) ? (setParameter ("table", (float) kResynth), true) : false; }

    static ModuleDescriptor descriptor();
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 120; }
    int preferredModuleWidth() const override { return 440; }

private:
    static constexpr float kOut = 0.3f;
    struct Voice
    {
        bool active { false }, gate { false };
        int note { -1 }, channel { 1 };
        float vel { 0.8f }, env { 0.0f }, modEnv { 0.0f };
        int stage { 0 };   // 0 attack, 1 decay/sustain, 2 release
        double freq { 110.0 }, target { 110.0 };
        double phase[kUnison] {}, subPhase { 0.0 };
        Svf fL[3], fR[3], lpL, lpR;
        uint32_t age { 0 }, ctl { 0 };
        std::array<float, kCombMax> combL {}, combR {};
        int combPos { 0 };
    };

    float p (const char* id) const { const int i = paramIndex (id); return i >= 0 ? param (i) : 0.0f; }
    // a filter with another's coefficients and its own memory
    static Svf copyCoefs (const Svf& mine, const Svf& from) { Svf s = from; s.ic1 = mine.ic1; s.ic2 = mine.ic2; return s; }

    float lfoValue (float ph)
    {
        switch (shape)
        {
            case 0:  return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * ph);
            case 1:  return ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph;
            case 2:  return 1.0f - ph;
            case 3:  return ph;
            case 4:  return ph < 0.5f ? 1.0f : 0.0f;
            case 5:  { const float s = std::sin (juce::MathConstants<float>::pi * juce::jmin (1.0f, ph * 1.6f)); return s * s; }   // up and back, then a rest
            default: return stepValue;
        }
    }

    void noteOn (int note, float velocity, int ch)
    {
        held.push_back ({ note, ch });
        const double f = aquanode::midiNoteToHz (note + 12 * octave);   // (through the patch tuning, 6.9.10)
        if (poly == 1)
        {
            Voice& v = voices[0];
            const bool legato = v.active && v.gate;
            v.note = note; v.channel = ch; v.vel = velocity; v.target = f;
            if (! legato || glide <= 0.0f) v.freq = f;
            if (! legato) { startVoice (v); lfoPhase = 0.0; }
            v.gate = true; v.active = true;
            return;
        }
        Voice* best = nullptr;
        for (auto& v : voices) if (! v.active) { best = &v; break; }
        if (best == nullptr) for (auto& v : voices) if (best == nullptr || v.age < best->age) best = &v;
        bool others = false; for (auto& v : voices) others = others || (v.active && v.gate);
        if (! others) lfoPhase = 0.0;
        Voice& v = *best;
        v.note = note; v.channel = ch; v.vel = velocity; v.target = v.freq = f;
        startVoice (v);
        v.gate = true; v.active = true;
    }
    void startVoice (Voice& v)
    {
        v.stage = 0; v.modEnv = 1.0f; v.age = ++ageCounter;
        for (int u = 0; u < kUnison; ++u) v.phase[u] = rng.nextDouble();
        v.subPhase = 0.0;
        if (v.env <= 1.0e-4f) { for (auto& s : v.fL) s.reset(); for (auto& s : v.fR) s.reset(); v.lpL.reset(); v.lpR.reset(); v.combL.fill (0.0f); v.combR.fill (0.0f); }
    }
    void noteOff (int note, int ch)
    {
        for (size_t i = 0; i < held.size(); ++i) if (held[i].first == note && held[i].second == ch) { held.erase (held.begin() + (long) i); break; }
        if (poly == 1)
        {
            Voice& v = voices[0];
            if (v.note != note) return;
            if (! held.empty())
            {
                // back to the key still held, gliding
                v.note = held.back().first; v.channel = held.back().second;
                v.target = aquanode::midiNoteToHz (v.note + 12 * octave);
                if (glide <= 0.0f) v.freq = v.target;
                return;
            }
            v.gate = false; v.stage = 2;
            return;
        }
        for (auto& v : voices) if (v.active && v.gate && v.note == note && v.channel == ch) { v.gate = false; v.stage = 2; }
    }
    void allOff() { for (auto& v : voices) { v.gate = false; v.stage = 2; } held.clear(); }

    void renderVoice (Voice& v, float lfo, float& outL, float& outR)
    {
        // envelopes
        if (v.stage == 0) { v.env += (1.05f - v.env) * (1.0f - std::exp (-1.0f / (attack * fs))); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
        else if (v.stage == 1) v.env += (sustain - v.env) * (1.0f - std::exp (-4.6f / (decay * fs)));
        else { v.env *= std::exp (-6.9f / (release * fs)); if (v.env < 1.0e-5f) { v.active = false; v.env = 0.0f; return; } }
        v.modEnv *= std::exp (-4.6f / (envDecay * fs));
        // pitch: glide, bends
        if (v.freq != v.target) { const double c = glide > 0.0f ? 1.0 - std::exp (-1.0 / (glide * 0.33 * fs)) : 1.0; v.freq += (v.target - v.freq) * c; }
        const float bend = masterBend + (v.channel >= 2 ? chanBend[v.channel] : 0.0f) + envPitch * v.modEnv;
        const double f = v.freq * std::pow (2.0, bend / 12.0);
        const double fNote = v.freq * std::pow (2.0, (bend - envPitch * v.modEnv) / 12.0);   // (the sub: no dive)
        const float fmIndex = juce::jlimit (0.0f, 1.5f, fm + lfoFm * lfo + envFm * v.modEnv) * 8.0f;   // radians
        // where in the table, the vowel, the cutoff
        const float press = v.channel >= 2 ? chanPressure[v.channel] : 0.0f;
        // (6.9.12) the default MPE map: Slide moves the table position - while
        // nothing else does (no cable on Pos, no Table LFO or envelope)
        float slidePos = 0.0f;
        if (v.channel >= 2 && chanSlide[v.channel] >= 0.0f && lfoTable == 0.0f && envTable == 0.0f && slideFree)
            slidePos = (chanSlide[v.channel] - 0.5f) * 2.0f * (midiControls != nullptr ? midiControls->slideWavetableDepth : 0.0f);
        const float tpos = juce::jlimit (0.0f, 1.0f, pos + lfoTable * lfo + envTable * v.modEnv + modWheel + slidePos);
        if (&v == &voices[0]) shownPos.store (tpos, std::memory_order_relaxed);
        const float vw = juce::jlimit (0.0f, 1.0f, vowel + lfoVowel * lfo + 0.5f * press);
        // the band-limited level for this pitch (the highest copy)
        const double fTop = f * (1.0 + 0.03 * detune);
        // (FM widens the spectrum by about index x ratio harmonics: the table's own taken that much lower)
        const double maxH = juce::jmax (7.0, 0.45 * fs / juce::jmax (1.0, fTop) - (fmIndex + 1.0) * fmRatio * (fmIndex > 0.0f ? 1.0 : 0.0));
        int lvl = 0; while (lvl < kLevels - 1 && kLevelMaxH[lvl] > maxH) ++lvl;
        // the copies
        float l = 0.0f, r = 0.0f;
        const int n = unison;
        for (int u = 0; u < n; ++u)
        {
            const float spread = n > 1 ? (2.0f * (float) u / (float) (n - 1) - 1.0f) : 0.0f;
            const double fu = f * unisonRatio[u];   // up to +-50 cents
            v.phase[u] += fu / fs; if (v.phase[u] >= 1.0) v.phase[u] -= std::floor (v.phase[u]);
            double ph = v.phase[u];
            if (fmIndex > 0.0f)
            {
                // a sine operator at FM Ratio, phase-modulating the table
                const double mph = ph * fmRatio;
                ph += fmIndex * fastSin ((float) (mph - std::floor (mph))) * 0.15915494309189535;
                ph -= std::floor (ph);
            }
            if (warp != 0.0f)
            {
                // the phase bent toward the start (Warp > 0) or the end (< 0) of the cycle
                ph = std::pow (ph, warpExp);
                if (ph >= 1.0) ph = 0.999999;
            }
            const float s = table->read (tpos, lvl, ph);
            const float pan = spread * width;
            l += s * std::sqrt (0.5f * (1.0f - pan)); r += s * std::sqrt (0.5f * (1.0f + pan));
        }
        const float norm = 1.0f / std::sqrt ((float) n);
        l *= norm; r *= norm;
        // the sub: a clean sine (Sub Oct), added after the squash and the clip
        v.subPhase += std::pow (2.0, subOct) * fNote / fs; if (v.subPhase >= 1.0) v.subPhase -= std::floor (v.subPhase);
        const float sb = sub * 0.7f * std::sin (juce::MathConstants<float>::twoPi * (float) v.subPhase);
        // the comb: notches and peaks at Comb Freq's multiples, the LFO sweeping them in octaves
        if (comb > 0.0f)
        {
            const float hz = juce::jlimit (25.0f, 6000.0f, combHz * std::pow (2.0f, 4.0f * lfoComb * (lfo - 0.5f)));
            const float d = juce::jlimit (2.0f, (float) (kCombMax - 2), fs / hz);
            const float fb = 0.92f * comb;
            auto tap = [&] (const std::array<float, kCombMax>& buf)
            {
                float rp = (float) v.combPos - d; if (rp < 0.0f) rp += (float) kCombMax;
                const int i0 = (int) rp; const float u = rp - (float) i0;
                return buf[(size_t) i0] + u * (buf[(size_t) ((i0 + 1) & (kCombMax - 1))] - buf[(size_t) i0]);
            };
            const float yl = l + fb * tap (v.combL), yr = r + fb * tap (v.combR);
            v.combL[(size_t) v.combPos] = yl; v.combR[(size_t) v.combPos] = yr;
            v.combPos = (v.combPos + 1) & (kCombMax - 1);
            const float g = (1.0f - fb) * 1.6f;
            l = l * (1.0f - comb) + yl * comb * g; r = r * (1.0f - comb) + yr * comb * g;
        }
        // the vowel
        const bool coefNow = (v.ctl++ & 7) == 0;
        if (formant > 0.0f)
        {
            const float q = 3.0f + 14.0f * fres;
            if (coefNow)
            {
                float fr[3]; vowelAt (vw, fr);
                for (int k = 0; k < 3; ++k) { v.fL[k].set (fr[k], q, fs); v.fR[k] = copyCoefs (v.fR[k], v.fL[k]); }
            }
            float lp, bp, fl = 0.0f, frr = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                v.fL[k].process (l, lp, bp); fl += bp * kVowelGain[k];
                v.fR[k].process (r, lp, bp); frr += bp * kVowelGain[k];
            }
            const float mk = 1.6f / std::sqrt (q / 3.0f);
            l = l * (1.0f - formant) + fl * formant * mk; r = r * (1.0f - formant) + frr * formant * mk;
        }
        // the low-pass
        const float cut = cutoff * std::pow (2.0f, 6.0f * lfoCutoff * (lfo - 0.5f));
        if (cut < 18000.0f || reso > 0.02f)
        {
            float lp, bp;
            if (coefNow) { v.lpL.set (cut, 0.5f + 9.5f * reso * reso, fs); v.lpR = copyCoefs (v.lpR, v.lpL); }
            v.lpL.process (l, lp, bp); l = lp; v.lpR.process (r, lp, bp); r = lp;
        }
        const float a = v.env * (0.4f + 0.6f * v.vel);
        outL += l * a; outR += r * a; subOut += sb * a;
    }

    // up and down per band
    float squashChannel (int c, float x)
    {
        auto& st = bandState[c];
        const float lo = st[1].process (st[0].process (x));                 // LR4 low at 88 Hz
        const float restA = st[3].process (st[2].process (x));              // above 88 Hz
        const float mid = st[5].process (st[4].process (restA));            // 88 Hz .. 2.5 kHz
        const float hi = st[7].process (st[6].process (restA));             // above 2.5 kHz
        const float loAp = st[8].process (lo);                              // the low band through 2.5 kHz's allpass: all three add up flat
        const float bands[3] = { loAp, mid, hi };
        float y = 0.0f;
        // (the highs lifted most: the fizz on top of a squashed growl)
        static const float downT[3] = { -26.0f, -30.0f, -34.0f }, upT[3] = { -40.0f, -40.0f, -48.0f }, maxUp[3] = { 18.0f, 24.0f, 32.0f }, tilt[3] = { 0.0f, 1.5f, 8.0f };
        for (int b = 0; b < 3; ++b)
        {
            const float a = std::abs (bands[b]);
            float& e = env[c * 3 + b];
            e += (a - e) * (a > e ? attC[b] : relC[b]);
            const float db = juce::Decibels::gainToDecibels (e + 1.0e-9f);
            float gdb = 0.0f;
            if (db > downT[b]) gdb -= downward * (db - downT[b]) * (1.0f - 1.0f / 12.0f);
            if (db < upT[b] && db > -70.0f) gdb += upward * juce::jmin (maxUp[b], (upT[b] - db) * (1.0f - 1.0f / 3.5f)) * juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 10.0f);
            gdb += tilt[b];
            y += bands[b] * juce::Decibels::decibelsToGain (gdb * squash);
        }
        return y * squashOut * juce::Decibels::decibelsToGain (6.0f * squash);
    }
    void settle() { for (auto& e : env) e *= 0.999f; }

    void feedCapture (float x, bool waitForSound)
    {
        if (captured >= (int) capture.size()) return;
        if (! captureStarted)
        {
            if (waitForSound ? std::abs (x) < 0.01f : ! voices[0].active && ! voices[1].active) return;
            captureStarted = true;
            // Resample: the newest sounding voice's period
            if (! waitForSound)
                for (auto& v : voices) if (v.active) capturePeriod = fs / juce::jmax (1.0, v.freq * std::pow (2.0, (masterBend + (v.channel >= 2 ? chanBend[v.channel] : 0.0f)) / 12.0));
        }
        capture[(size_t) captured++] = x;
        if (captured >= (int) (fs * 1.2f)) captureMode.store (3, std::memory_order_release);
    }

    // a table from a recording: 32 periods across it, each to 1024 points, aligned on the fundamental
    bool buildResynth (const float* x, int n, double period)
    {
        lastCaptureMessage = {};
        const int skip = (int) (0.05 * fs);
        if (n < skip + 4096) { lastCaptureMessage = "too short"; return false; }
        if (period <= 0.0)
        {
            // the pitch: the strongest repeat between 25 Hz and 1 kHz (normalised autocorrelation)
            const int a = skip, len = juce::jmin (n - a - (int) (fs / 25.0f) - 2, (int) (fs * 0.4f));
            if (len < 1024) { lastCaptureMessage = "too short"; return false; }
            double best = 0.0; int bestLag = 0;
            std::vector<double> score ((size_t) (fs / 25.0f) + 2, 0.0);
            for (int lag = (int) (fs / 1000.0f); lag < (int) (fs / 25.0f); ++lag)
            {
                double xy = 0.0, xx = 0.0, yy = 0.0;
                for (int i = 0; i < len; i += 2) { const double p0 = x[a + i], p1 = x[a + i + lag]; xy += p0 * p1; xx += p0 * p0; yy += p1 * p1; }
                const double s = xy / std::sqrt (xx * yy + 1.0e-20);
                score[(size_t) lag] = s;
            }
            // the first lag that comes close to the best (the period, not a multiple of it)
            for (size_t lag = 2; lag + 1 < score.size(); ++lag) best = juce::jmax (best, score[lag]);
            for (size_t lag = 2; lag + 1 < score.size(); ++lag)
                if (score[lag] > 0.9 * best && score[lag] >= score[lag - 1] && score[lag] >= score[lag + 1]) { bestLag = (int) lag; break; }
            if (best < 0.5 || bestLag == 0) { lastCaptureMessage = "no clear pitch"; return false; }
            // parabolic refinement
            const double y0 = score[(size_t) bestLag - 1], y1 = score[(size_t) bestLag], y2 = score[(size_t) bestLag + 1];
            const double den = y0 - 2.0 * y1 + y2;
            period = bestLag + (std::abs (den) > 1.0e-12 ? 0.5 * (y0 - y2) / den : 0.0);
        }
        const double usable = (double) (n - skip) - period - 4.0;
        if (usable <= 0.0) { lastCaptureMessage = "too short"; return false; }
        std::vector<std::vector<std::complex<float>>> sp;
        std::vector<float> frame ((size_t) kFull);
        for (int f = 0; f < kFrames; ++f)
        {
            const double start = skip + usable * f / (kFrames - 1);
            for (int i = 0; i < kFull; ++i)
            {
                const double t = start + period * i / kFull;
                const int i0 = (int) t; const float u = (float) (t - i0);
                const float ym = x[juce::jmax (0, i0 - 1)], y0 = x[i0], y1 = x[i0 + 1], y2 = x[juce::jmin (n - 1, i0 + 2)];
                // Catmull-Rom
                frame[(size_t) i] = y0 + 0.5f * u * (y1 - ym + u * (2.0f * ym - 5.0f * y0 + 4.0f * y1 - y2 + u * (3.0f * (y0 - y1) + y2 - ym)));
            }
            auto s = spectrumOf (frame);
            // one period is not quite closed: a little window on the seam would colour it; aligning every
            // frame on its fundamental keeps the frames from cancelling as the position moves between them
            const float ph1 = std::arg (s[1]);
            for (int h = 1; h <= kMaxH; ++h) s[(size_t) h] *= std::polar (1.0f, -(float) h * (ph1 + juce::MathConstants<float>::halfPi));
            sp.push_back (std::move (s));
        }
        publishResynth (sp);
        lastCaptureMessage = "period " + juce::String (period, 1) + " samples (" + juce::String (fs / period, 1) + " Hz)";
        return true;
    }
    void publishResynth (const std::vector<std::vector<std::complex<float>>>& sp)
    {
        const int next = 1 - activeResynth.load();
        resynth[(size_t) next].build (sp);
        activeResynth.store (next, std::memory_order_release);
        hasPrevious = true;   // (the plain saw at the start counts as a table to go back to)
        hasResynth = true;
    }

    float fs { 48000.0f };
    std::array<Voice, kVoices> voices {};
    std::vector<std::pair<int, int>> held;
    uint32_t ageCounter { 0 };
    juce::Random rng { 0x9e0f };
    const Table* table { nullptr };
    std::array<Table, 2> resynth;
    std::atomic<int> activeResynth { 0 };
    bool hasResynth { false }, hasPrevious { false };
    std::vector<float> capture;
    int captured { 0 };
    bool captureStarted { false };
    double capturePeriod { 0.0 };
    std::atomic<int> captureMode { 0 };
    juce::String lastCaptureMessage;
    std::atomic<float> shownPos { 0.0f };

    int tableType { 0 }, unison { 1 }, rate { 4 }, shape { 5 }, poly { 1 }, octave { 0 };
    float pos { 0.4f }, warp { 0.0f }, sub { 0.3f }, detune { 0.15f }, width { 0.5f };
    float lfoTable { 0.3f }, lfoVowel { 0.6f }, lfoCutoff { 0.0f }, envTable { 0.0f }, envDecay { 0.3f };
    float vowel { 0.3f }, formant { 0.6f }, fres { 0.4f }, cutoff { 12000.0f }, reso { 0.1f }, drive { 0.3f };
    float squash { 0.5f }, squashTime { 1.0f }, upward { 1.0f }, downward { 1.0f }, squashOut { 1.0f };
    float glide { 0.05f }, attack { 0.003f }, decay { 0.3f }, sustain { 1.0f }, release { 0.08f }, level { 0.8f };
    double lfoPhase { 0.0 }, beatsPerSample { 0.0 };
    float stepValue { 0.5f }, stepTarget { 0.5f };
    float masterBend { 0.0f }, chanBend[17] {}, chanPressure[17] {}, modWheel { 0.0f };
    float chanSlide[17] { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    bool slideFree { true };
    float attC[3] {}, relC[3] {};
    float env[6] {};
    Biquad bandState[2][9];
    Biquad monoHp, monoHp2, growlHp[4];
    float subOut { 0.0f };
    double unisonRatio[kUnison] { 1, 1, 1, 1, 1, 1, 1 }, warpExp { 1.0 };
    // a sine from a table (one cycle, 0 .. 1), linear between its 4096 points
    static float fastSin (float ph01) noexcept
    {
        static const std::array<float, 4097> t = [] { std::array<float, 4097> a {}; for (int i = 0; i <= 4096; ++i) a[(size_t) i] = (float) std::sin (6.283185307179586 * i / 4096.0); return a; }();
        const float x = ph01 * 4096.0f; const int i = juce::jlimit (0, 4095, (int) x); const float u = x - (float) i;
        return t[(size_t) i] + u * (t[(size_t) i + 1] - t[(size_t) i]);
    }
    float fm { 0.0f }, fmRatio { 1.0f }, lfoFm { 0.0f }, lfoComb { 0.0f }, envFm { 0.0f }, envPitch { 0.0f };
    float comb { 0.0f }, combHz { 300.0f }, fold { 0.0f }, clip { 0.0f };
    int subOct { -1 };
};

ModuleDescriptor GrowlModule::descriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.growl";
    d.displayName = "Growl";
    d.section = ModuleSection::Oscillator;
    d.description = "Dubstep growls: a wavetable phase-modulated by a sine (FM, FM Ratio), swept by a tempo-synced LFO and "
                    "an envelope, through a vowel filter and a comb, driven, folded, squashed by a three-band upward / "
                    "downward compressor and clipped, a clean sine sub under it - and the raw sound for a pitch quantizer "
                    "(Spectral Pitch Quantizer) in colour bass. Table: Vowel Saw, FM Growl, Sync Sweep, Fold Square, "
                    "Formant Square, Metal Comb (32 frames each, Position through them, Warp bending the phase) or Custom, "
                    "made on the card: Generate (a new table from a random recipe), Iterate (the selected table again "
                    "through a Filter Sweep, Formant Sweep, Comb Sweep, Harmonic Shift, Fold, Self FM, Phase Warp or Sync "
                    "whose amount sweeps over the frames - each frame its own, or the frame at Position swept into all; "
                    "iterate again on the result), Undo; and from sound if wanted: Capture (a second of Audio In) or "
                    "Resample (a second of Growl itself while a key is held). LFO (1/1 .. 1/32T; Sine, Triangle, Saw Down, "
                    "Saw Up, Square, Yoy, Steps) to the table, FM, vowel, comb and cutoff; Env to the table, FM and pitch "
                    "(dives); the mod wheel moves the table. Vowel (u - o - a - e - i), Formant, Res; Comb and Comb Freq; "
                    "Cutoff, Reso; Drive, Fold, Clip. Squash, Time, Upward, Downward, Squash Out. Sub (an octave or two "
                    "down or at the note; with it the growl is high-passed at 80 Hz), Unison with Detune and Width, "
                    "Voices (1: mono, gliding), Glide, Octave, ADSR. Presets on the card (twelve held sounds, then one-hits that fall away over a second). MPE: each note bends on its own "
                    "channel, pressure opens its vowel.";
    d.sockets = { midiIn ("midiIn", "MIDI In"), audioIn ("audioIn", "Audio In (Capture)"), audioOut ("out", "Audio Out"), tempoIn() };
    d.params = {
        makeCombo  ("table",      "Table",        kTableNames, 1, 0, 2).noMod(),
        makeRotary ("pos",        "Position",     0.0f, 100.0f, 35.0f, 0, "%"),
        makeRotary ("warp",       "Warp",         -100.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("fm",         "FM",           0.0f, 100.0f, 30.0f, 0, "%"),
        makeCombo  ("fmRatio",    "FM Ratio",     kFmRatioNames, 1, 0, 1).noMod(),
        makeRotary ("unison",     "Unison",       1.0f, 7.0f, 1.0f, 0, {}, false, 1.0f).noMod(),
        makeRotary ("detune",     "Detune",       0.0f, 100.0f, 15.0f, 0, "%"),
        makeRotary ("width",      "Width",        0.0f, 100.0f, 50.0f, 0, "%"),
        makeCombo  ("rate",       "LFO Rate",     kRateNames, 6, 1, 2).noMod(),
        makeCombo  ("shape",      "LFO Shape",    kShapeNames, 5, 1, 2).noMod(),
        makeRotary ("lfoTable",   "LFO > Table",  -100.0f, 100.0f, 40.0f, 1, "%"),
        makeRotary ("lfoFm",      "LFO > FM",     -100.0f, 100.0f, 40.0f, 1, "%"),
        makeRotary ("lfoVowel",   "LFO > Vowel",  -100.0f, 100.0f, 70.0f, 1, "%"),
        makeRotary ("lfoComb",    "LFO > Comb",   -100.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("lfoCutoff",  "LFO > Cutoff", -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("envTable",   "Env > Table",  -100.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("envFm",      "Env > FM",     -100.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("envPitch",   "Env > Pitch",  -24.0f, 24.0f, 0.0f, 2, "st", false, 0.1f),
        makeRotary ("envDecay",   "Env Decay",    10.0f, 3000.0f, 250.0f, 2, "ms", true, 1.0f),
        makeRotary ("vowel",      "Vowel",        0.0f, 100.0f, 30.0f, 2, "%"),
        makeRotary ("formant",    "Formant",      0.0f, 100.0f, 35.0f, 2, "%"),
        makeRotary ("fres",       "Res",          0.0f, 100.0f, 35.0f, 2, "%"),
        makeRotary ("comb",       "Comb",         0.0f, 100.0f, 35.0f, 3, "%"),
        makeRotary ("combFreq",   "Comb Freq",    25.0f, 3000.0f, 220.0f, 3, "Hz", true),
        makeRotary ("cutoff",     "Cutoff",       40.0f, 20000.0f, 20000.0f, 3, "Hz", true),
        makeRotary ("reso",       "Reso",         0.0f, 100.0f, 0.0f, 3, "%"),
        makeRotary ("drive",      "Drive",        0.0f, 100.0f, 55.0f, 3, "%"),
        makeRotary ("fold",       "Fold",         0.0f, 100.0f, 15.0f, 3, "%"),
        makeRotary ("clip",       "Clip",         0.0f, 100.0f, 65.0f, 3, "%"),
        makeRotary ("squash",     "Squash",       0.0f, 100.0f, 85.0f, 4, "%"),
        makeRotary ("squashTime", "Time",         10.0f, 300.0f, 100.0f, 4, "%"),
        makeRotary ("upward",     "Upward",       0.0f, 100.0f, 100.0f, 4, "%"),
        makeRotary ("downward",   "Downward",     0.0f, 100.0f, 100.0f, 4, "%"),
        makeRotary ("squashOut",  "Squash Out",   -12.0f, 12.0f, 0.0f, 4, "dB"),
        makeRotary ("level",      "Level",        0.0f, 1.0f, 0.8f, 4),
        makeRotary ("voices",     "Voices",       1.0f, 8.0f, 1.0f, 5, {}, false, 1.0f).noMod(),
        makeRotary ("glide",      "Glide",        0.0f, 500.0f, 40.0f, 5, "ms", false, 1.0f),
        makeRotary ("octave",     "Octave",       -2.0f, 1.0f, 0.0f, 5, {}, false, 1.0f).noMod(),
        makeRotary ("sub",        "Sub",          0.0f, 100.0f, 75.0f, 5, "%"),
        makeCombo  ("subOct",     "Sub Oct",      kSubOctNames, 1, 5, 1).noMod(),
        makeRotary ("attack",     "Attack",       1.0f, 2000.0f, 2.0f, 5, "ms", true, 1.0f),
        makeRotary ("decay",      "Decay",        5.0f, 4000.0f, 300.0f, 5, "ms", true, 1.0f),
        makeRotary ("sustain",    "Sustain",      0.0f, 100.0f, 100.0f, 5, "%"),
        makeRotary ("release",    "Release",      5.0f, 3000.0f, 40.0f, 5, "ms", true, 1.0f),
        makeOutputTrim (0.0f)
    };
    d.rowSections = { { 0, "Table" }, { 1, "LFO" }, { 2, "Env and Vowel" }, { 3, "Comb, Filter, Dirt" }, { 4, "Squash" }, { 5, "Voice" } };
    return d;
}

//==============================================================================
// the card: Presets, Capture, Resample, and the table drawn frame by frame
namespace growl
{
    class GrowlView final : public juce::Component, public aquanode::ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit GrowlView (GrowlModule& m) : module (&m)
        {
            for (auto* b : { &generateButton, &iterateButton, &undoButton, &captureButton, &resampleButton, &presetsButton })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d2433));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
                b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd64b3d));
            }
            generateButton.setTooltip ("A new Custom table from a random recipe");
            iterateButton.setTooltip ("The selected table again, through a process sweeping over its frames");
            undoButton.setTooltip ("Back to the Custom table before");
            captureButton.setTooltip ("A Custom table from the next second that comes into Audio In");
            resampleButton.setTooltip ("A Custom table from a second of Growl itself - hold a key");
            presetsButton.onClick = [this]
            {
                juce::PopupMenu menu;
                menu.addSectionHeader ("Growl presets");
                int id = 1;
                for (const auto& pr : presets())
                {
                    if (id == kLoopPresets + 1) menu.addSectionHeader ("One-hits");
                    menu.addItem (id++, pr.name);
                }
                juce::Component::SafePointer<GrowlView> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton), [safe] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    if (auto* g = safe->growl()) g->applyPreset (r - 1);
                    safe->changed ({});
                });
            };
            generateButton.onClick = [this] { if (auto* g = growl()) { g->generate ((uint32_t) juce::Random::getSystemRandom().nextInt()); changed ("Generated a new table"); } };
            iterateButton.onClick = [this]
            {
                juce::PopupMenu menu;
                menu.addSectionHeader ("Each frame through");
                for (int i = 0; i < kIterateNames.size(); ++i) menu.addItem (1 + i, kIterateNames[i]);
                menu.addSectionHeader ("The frame at Position, swept through");
                for (int i = 0; i < kIterateNames.size(); ++i) menu.addItem (101 + i, kIterateNames[i]);
                juce::Component::SafePointer<GrowlView> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&iterateButton), [safe] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    const bool fromPos = r > 100;
                    const int proc = (fromPos ? r - 101 : r - 1);
                    if (auto* g = safe->growl()) g->iterate (proc, fromPos);
                    safe->changed ("Iterated: " + kIterateNames[proc] + (fromPos ? " from one frame" : ""));
                });
            };
            undoButton.onClick = [this] { if (auto* g = growl()) changed (g->undo() ? "Back to the table before" : "Nothing to go back to"); };
            captureButton.onClick = [this] { if (auto* g = growl()) { g->startCapture (false); status = "listening at Audio In..."; repaint(); } };
            resampleButton.onClick = [this] { if (auto* g = growl()) { g->startCapture (true); status = "hold a key: resampling..."; repaint(); } };
            startTimerHz (20);
        }
        GrowlModule* growl() const { return dynamic_cast<GrowlModule*> (module.get()); }

        void resized() override
        {
            auto row = getLocalBounds().removeFromTop (26).reduced (4, 3);
            generateButton.setBounds (row.removeFromLeft (68)); row.removeFromLeft (3);
            iterateButton.setBounds (row.removeFromLeft (60)); row.removeFromLeft (3);
            undoButton.setBounds (row.removeFromLeft (44));
            presetsButton.setBounds (row.removeFromRight (62)); row.removeFromRight (8);
            resampleButton.setBounds (row.removeFromRight (70)); row.removeFromRight (3);
            captureButton.setBounds (row.removeFromRight (60));
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff10141d)); g.fillRoundedRectangle (b.reduced (2.0f), 5.0f);
            auto* gm = growl();
            if (gm == nullptr) return;
            const int t = juce::jlimit (0, kResynth, (int) std::lround (gm->getParameter ("table")));
            auto line = b.withTrimmedTop (26.0f).withHeight (15.0f).reduced (8.0f, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.setFont (12.0f);
            g.drawText (kTableNames[t], line, juce::Justification::centredLeft);
            if (status.isNotEmpty())
            {
                g.setColour (juce::Colour (0xffe8a03a)); g.setFont (11.0f);
                g.drawText (status, line.withTrimmedLeft (110.0f), juce::Justification::centredLeft);
            }
            // the frames, back to front, the playing position bright
            const auto& table = gm->shownTable();
            if (table.data.empty()) return;
            auto area = b.withTrimmedTop (44.0f).reduced (10.0f, 6.0f);
            const float posNow = gm->shownPosition();
            const int shown = 12;
            for (int k = shown - 1; k >= 0; --k)
            {
                const float fpos = (float) k / (float) (shown - 1);
                const int f = juce::jlimit (0, kFrames - 1, (int) std::lround (fpos * (kFrames - 1)));
                const float dx = (float) k * area.getWidth() * 0.18f / shown, dy = -(float) k * area.getHeight() * 0.45f / shown;
                const float w = area.getWidth() * 0.82f, h = area.getHeight() * 0.55f;
                const float x0 = area.getX() + dx, ymid = area.getBottom() - h * 0.5f + dy;
                juce::Path path;
                for (int i = 0; i <= 128; ++i)
                {
                    const float v = table.data[(size_t) (table.offset[f][0] + (i * kFull / 128) % kFull)];
                    const float x = x0 + w * (float) i / 128.0f, y = ymid - v * h * 0.5f;
                    if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                }
                const bool near = std::abs (fpos - posNow) < 0.5f / (shown - 1);
                g.setColour (near ? juce::Colour (0xffff6a3d) : juce::Colour (0xff3d8bd6).withAlpha (0.25f + 0.5f * (1.0f - fpos)));
                g.strokePath (path, juce::PathStrokeType (near ? 1.8f : 1.0f));
            }
        }

    private:
        void changed (const juce::String& message)
        {
            status = message;
            if (paramsChangedByContent) paramsChangedByContent();
            repaint();
        }
        void timerCallback() override
        {
            auto* gm = growl();
            if (gm == nullptr) return;
            const int st = gm->captureState();
            captureButton.setToggleState (st == 1, juce::dontSendNotification);
            resampleButton.setToggleState (st == 2, juce::dontSendNotification);
            if (st == 3)
            {
                const bool ok = gm->finishCapture();
                changed (ok ? "Captured: " + gm->captureMessage() : "Capture failed: " + gm->captureMessage());
            }
            repaint();
        }
        juce::WeakReference<SynthModule> module;
        juce::TextButton generateButton { "Generate" }, iterateButton { "Iterate" }, undoButton { "Undo" },
                         captureButton { "Capture" }, resampleButton { "Resample" }, presetsButton { "Presets" };
        juce::String status;
    };
}

std::unique_ptr<juce::Component> GrowlModule::createExtraContentComponent()
{
    return std::make_unique<GrowlView> (*this);
}

AQUANODE_REGISTER_MODULE (GrowlModule, GrowlModule::descriptor)

// for tests
void growlApplyPreset (SynthModule* m, int index) { if (auto* g = dynamic_cast<GrowlModule*> (m)) g->applyPreset (index); }
int growlPresetCount() { return (int) growl::presets().size(); }
bool growlCaptureFrom (SynthModule* m, const float* x, int n, double period)
{
    // the capture path without the card: the samples handed straight to the table builder
    if (auto* g = dynamic_cast<GrowlModule*> (m)) return g->resynthForTest (x, n, period);
    return false;
}
void growlGenerate (SynthModule* m, uint32_t seed) { if (auto* g = dynamic_cast<GrowlModule*> (m)) g->generate (seed); }
void growlIterate (SynthModule* m, int process, bool fromPosition) { if (auto* g = dynamic_cast<GrowlModule*> (m)) g->iterate (process, fromPosition); }
bool growlUndo (SynthModule* m) { auto* g = dynamic_cast<GrowlModule*> (m); return g != nullptr && g->undo(); }
