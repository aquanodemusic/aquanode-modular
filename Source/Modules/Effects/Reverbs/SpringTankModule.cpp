// Aquanode Modular - Spring Tank: the spring reverb of smiarx's ÆLAPSE as a module
//
// The algorithm is ÆLAPSE's (its Springs processor, in smiarx/dsp, GNU GPL v3.0), carried over
// here as plain scalar C++: four springs, each a feedback loop through a long cascade of
// second-order all-passes (the dispersion that makes the chirps), the springs mixed into one
// another by a Householder matrix on every round trip, the loop's length wandering slowly
// (and, with Diffuse - ÆLAPSE's Chaos - by noise), a soft saturation and a DC blocker in the
// loop, a band-pass Tone and a steep Chebyshev low-pass at the Damp frequency on the way out.
// As there, the whole tank runs at a lower rate when Damp is low (up to 8 times), which puts the
// cascade's fold at Damp and keeps it cheap. No GUI of its own, no presets.
#include "../../Core/ModuleCore.h"
#include <array>
#include <vector>

using namespace aquanode;

namespace
{
    constexpr int kN = 4;                       // springs
    constexpr int kCascadeL = 160;              // all-pass stages at the full rate
    constexpr int kMaxDecimate = 8;
    constexpr float kDecimateMaxFreq = 0.78f;
    constexpr float kMinRWithMaxCascadeL = 0.1f;
    constexpr float kNonLinearityGain = 0.2f;
    constexpr float kLoopRippleGain = 0.016f;
    constexpr float kMinScatter = 0.1f;
    constexpr float kFreqFactor[kN]   = { 0.98f, 1.02f, 0.97f, 1.03f };
    constexpr float kRFactor[kN]      = { 1.08f, 0.97f, 1.05f, 0.98f };
    constexpr float kLoopTdFactor[kN] = { 0.8293183583208989f, 1.1876863056468745f, 0.94273342f, 1.2432815625f };
    constexpr float kLoopModFreq[kN]  = { 0.2f, 0.4f, 0.2f, 0.3f };
    constexpr float kLoopModFactor[kN] = { 0.0035f, 0.002f, 0.0038f, 0.0027f };
    // scipy.signal.cheby1(10, 2, 1, analog=True, output='sos'): b2 / (s^2 + a1 s + a2) per section
    constexpr double kCheby[5][3] = { { 0.00255383, 0.21436212, 0.0362477 }, { 1.0, 0.19337886, 0.21788333 },
                                      { 1.0, 0.15346633, 0.51177596 },       { 1.0, 0.09853145, 0.80566858 },
                                      { 1.0, 0.03395162, 0.98730422 } };

    struct BQ
    {
        double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, z1 { 0 }, z2 { 0 };
        double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void reset() { z1 = z2 = 0.0; }
    };

    // an 8th-order Butterworth low-pass (for going down to the tank's rate and back up)
    struct Butter8
    {
        BQ s[4];
        void set (double fc, double sr)
        {
            const double K = std::tan (juce::MathConstants<double>::pi * juce::jmin (fc, sr * 0.49) / sr);
            for (int k = 0; k < 4; ++k)
            {
                const double q = 1.0 / (2.0 * std::cos (juce::MathConstants<double>::pi * (2.0 * k + 1.0) / 16.0));
                const double n = 1.0 / (1.0 + K / q + K * K);
                s[k].b0 = K * K * n; s[k].b1 = 2.0 * s[k].b0; s[k].b2 = s[k].b0;
                s[k].a1 = 2.0 * (K * K - 1.0) * n; s[k].a2 = (1.0 - K / q + K * K) * n;
            }
        }
        double process (double x) { for (auto& b : s) x = b.process (x); return x; }
        void reset() { for (auto& b : s) b.reset(); }
    };

    struct Delay
    {
        std::vector<float> buf;
        int w { 0 }, mask { 0 };
        void init (int minSize) { int n = 1; while (n < minSize) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        float at (int d) const { return buf[(size_t) ((w - d) & mask)]; }
        float cubic (double d) const
        {
            d = juce::jlimit (2.0, (double) mask - 3.0, d);
            const double pos = (double) w - d, fl = std::floor (pos);
            const float t = (float) (pos - fl);
            const int i = (int) fl;
            const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)];
            const float x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
            const float c1 = x1 - xm1 * (1.0f / 3.0f) - x0 * 0.5f - x2 * (1.0f / 6.0f);
            const float c2 = 0.5f * (xm1 + x1) - x0, c3 = (x2 - xm1) * (1.0f / 6.0f) + 0.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }
    };
}

class SpringTankModule final : public SynthModule
{
public:
    enum P { pMix = 0, pWidth, pLength, pDecay, pDamp, pShape, pTone, pScatter, pDiffuse };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int loopMax = (int) (sr * 0.45) + 16;
        for (int i = 0; i < kN; ++i) { loop[i].init (loopMax); pre[i].init (loopMax / 2 + 8); }
        juce::Random r (0xae1a);
        for (int i = 0; i < kN; ++i) lfoPhase[i] = r.nextDouble();
        rate = 0;   // forces the set-up
        update (true);
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < kN; ++i)
        {
            loop[i].clear(); pre[i].clear();
            for (auto& s : ap[i]) s = { 0.0f, 0.0f };
            for (auto& b : lowpass[i]) b.reset();
            eqS1[i] = eqS2[i] = 0.0; dcX[i] = dcY[i] = 0.0f; ripplePrev[i] = 0.0f;
        }
        down.reset(); upL.reset(); upR.reset();
        phase = 0; heldL = heldR = 0.0;
        chaosNow = chaosTarget = 0.0;
    }

    double voiceTailSeconds() const override { return 0.0; }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        if ((counter++ & 31) == 0) update (false);
        const float inL = inputs[0][0], inR = inputs[0][1];
        const double mono = 0.5 * ((double) inL + (double) inR);

        double wl = 0.0, wr = 0.0;
        if (rate <= 1)
        {
            tank (mono, wl, wr);
        }
        else
        {
            const double filtered = down.process (mono);
            double l = 0.0, rr = 0.0;
            if (phase == 0) { tank (filtered, l, rr); l *= rate; rr *= rate; }   // (zero-stuffed back up)
            wl = upL.process (l);
            wr = upR.process (rr);
            if (++phase >= rate) phase = 0;
        }
        const float ol = (float) ((double) inL * dry + wl), orr = (float) ((double) inR * dry + wr);
        outputs[0][0] = std::isfinite (ol) ? ol : 0.0f;
        outputs[0][1] = std::isfinite (orr) ? orr : 0.0f;
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.springtank";
        d.displayName = "Spring Tank";
        d.description =
            "Four springs in a tank, after smiarx's ÆLAPSE: each a loop through a long chain of all-passes that turns "
            "every echo into the spring's chirp, the springs passing sound to one another on every round trip. Length is "
            "the round trip (the echoes' spacing), Decay the time to fade by 60 dB, Damp where the springs' highs end "
            "(and where the chirp folds), Shape how the chirp rings (negative: an octave lower and different), Tone a "
            "broad band-pass, Scatter how unalike the four springs are, Diffuse how much their lengths wander, "
            "smearing the echoes. Width spreads the springs across the stereo field. DSP after ÆLAPSE (GPLv3).";
        d.section = ModuleSection::Effect;
        d.listedAfter = "fx.springer";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("mix",     "Mix",     0.0f, 100.0f, 25.0f, 2, "%", false, 0.1f),
            makeRotary ("width",   "Width",   0.0f, 100.0f, 100.0f, 2, "%", false, 0.1f),
            makeRotary ("length",  "Length",  20.0f, 200.0f, 50.0f, 0, "ms", true, 1.0f),
            makeRotary ("decay",   "Decay",   0.3f, 10.0f, 3.0f, 0, "s", true, 0.01f),
            makeRotary ("damp",    "Damp",    200.0f, 12000.0f, 4500.0f, 0, "Hz", true, 1.0f),
            makeRotary ("shape",   "Shape",   -5.0f, 5.0f, 0.5f, 1, {}, false, 0.01f),
            makeRotary ("tone",    "Tone",    0.0f, 100.0f, 50.0f, 1, "%", false, 0.1f),
            makeRotary ("scatter", "Scatter", 0.0f, 120.0f, 50.0f, 1, "%", false, 0.1f),
            makeRotary ("diffuse", "Diffuse", 0.0f, 100.0f, 25.0f, 1, "%", false, 0.1f)
        };
        d.rowSections = { { 0, "Springs" }, { 1, "Character" }, { 2, "Out" } };
        return d;
    }

private:
    // the tank, at its own rate: one sample in, the stereo wet out
    void tank (double x, double& outL, double& outR)
    {
        // the wander: noise, a new value every so often, glided to over 0.2 s
        if (++chaosCount >= 256) { chaosCount = 0; chaosTarget = random.nextFloat() * 2.0f - 1.0f; }
        chaosNow += chaosGlide * (chaosTarget - chaosNow);

        float loopOut[kN], s = 0.0f;
        for (int i = 0; i < kN; ++i)
        {
            curTd[i] += tdGlide * (targetTd[i] - curTd[i]);
            lfoPhase[i] += kLoopModFreq[i] * decPeriod;
            if (lfoPhase[i] >= 1.0) lfoPhase[i] -= 1.0;
            const double ph = lfoPhase[i] * 2.0 - 1.0;            // a parabolic sine
            const double lfo = 4.0 * ph * (1.0 - std::abs (ph)) * -1.0;
            const double d = curTd[i] + lfo * curTd[i] * kLoopModFactor[i] + chaosNow * curTd[i] * chaosAmt;
            loopOut[i] = loop[i].cubic (d);
            s += loopOut[i];
        }
        double mixL = 0.0, mixR = 0.0;
        for (int i = 0; i < kN; ++i)
        {
            const float h = loopOut[i] - 0.5f * s;                 // Householder: I - 2/N
            const float ripple = ripplePrev[i] + kLoopRippleGain * (h - ripplePrev[i]);
            ripplePrev[i] = h;
            pre[i].push ((float) x);
            float v = pre[i].at (predelay[i]) + ripple * loopGain;
            v = std::tanh (v * kNonLinearityGain) / kNonLinearityGain;
            // DC blocker
            const float dc = v - dcX[i] + dcR * dcY[i];
            dcX[i] = v; dcY[i] = dc; v = dc;
            // the chirps: the all-pass cascade
            a0[i] += coeffGlide * (a0Target[i] - a0[i]);
            a1[i] += coeffGlide * (a1Target[i] - a1[i]);
            const float c0 = a0[i], c1 = a1[i];
            auto* st = ap[i].data();
            for (int k = 0; k < stages; ++k)
            {
                const float v0 = c0 * (v - st[k][0]);
                const float y0 = v0 + st[k][0];
                const float x1 = v0 + v;
                const float v1 = c1 * (x1 - st[k][1]);
                st[k][0] = v1 + st[k][1];
                st[k][1] = v1 + x1;
                v = y0;
            }
            loop[i].push (v);
            // Tone: a broad, normalised band-pass (state-variable)
            const double hp = ((double) v - (2.0 * eqR + eqG) * eqS1[i] - eqS2[i]) * eqDen;
            const double bp = eqG * hp + eqS1[i];
            eqS1[i] = eqG * hp + bp;
            const double lp = eqG * bp + eqS2[i];
            eqS2[i] = eqG * bp + lp;
            double o = 2.0 * eqR * bp;
            for (auto& b : lowpass[i]) o = b.process (o);
            (i == 0 || i == 2 ? mixL : mixR) += o;
        }
        mixL *= 2.0 / kN; mixR *= 2.0 / kN;
        outL = wetC * mixL + wetX * mixR;
        outR = wetC * mixR + wetX * mixL;
    }

    void update (bool snap)
    {
        const double sr = sampleRate;
        const float drywet = param (pMix) * 0.01f, width = param (pWidth) * 0.01f;
        const float td = param (pLength) * 0.001f, t60 = param (pDecay), freq = param (pDamp);
        const float r = param (pShape), tone = param (pTone) * 0.01f;
        const float scatterF = kMinScatter + param (pScatter) * 0.01f, chaos = param (pDiffuse) * 0.01f;

        // the rate: as low as keeps Damp under 0.78 of the tank's Nyquist
        const float freqScale = (float) (2.0 / sr);
        float fs = freq * freqScale;
        const int newRate = juce::jlimit (1, kMaxDecimate, (int) (kDecimateMaxFreq / fs));
        if (newRate != rate)
        {
            rate = newRate; phase = 0; snap = true;
            const double nyqDec = sr * 0.5 / rate;
            down.set (nyqDec * 0.8, sr); upL.set (nyqDec * 0.8, sr); upR.set (nyqDec * 0.8, sr);
            down.reset(); upL.reset(); upR.reset();
            for (int i = 0; i < kN; ++i) { loop[i].clear(); pre[i].clear(); for (auto& sst : ap[i]) sst = { 0.0f, 0.0f }; }
        }
        fs *= (float) rate;
        const double decSr = sr / rate;
        decPeriod = 1.0 / decSr;
        // the cascade's length: shorter for a small |Shape| (less ringing round the fold)
        const float ar = std::abs (r);
        const int full = ar < kMinRWithMaxCascadeL ? (int) (ar / kMinRWithMaxCascadeL * kCascadeL) : kCascadeL;
        stages = juce::jlimit (1, kCascadeL, (full + rate - 1) / rate);

        for (int i = 0; i < kN; ++i)
        {
            float f = fs * (1.0f + (kFreqFactor[i] - 1.0f) * scatterF);
            f = juce::jlimit (0.005f, 0.995f, f);
            const float fap = r < 0.0f ? f * 0.5f : f;
            a1Target[i] = -std::cos (juce::MathConstants<float>::pi * fap);
            const float ri = ar * (1.0f + (kRFactor[i] - 1.0f) * scatterF);
            a0Target[i] = (1.0f - ri) / (1.0f + ri);
            // the output low-pass: the analog Chebyshev, warped to f (of the tank's Nyquist)
            const double K = 1.0 / std::tan (juce::MathConstants<double>::pi * (double) f * 0.5);
            for (int k = 0; k < 5; ++k)
            {
                const double b = kCheby[k][0], a1 = kCheby[k][1], a2 = kCheby[k][2];
                const double n0 = K * K + a1 * K + a2;
                auto& q = lowpass[i][k];
                q.b0 = b / n0; q.b1 = 2.0 * b / n0; q.b2 = b / n0;
                q.a1 = (2.0 * a2 - 2.0 * K * K) / n0; q.a2 = (K * K - a1 * K + a2) / n0;
            }
            // the loop: Length per spring, scattered; the predelay half of it
            const double sampleTd = juce::jmin ((double) td * decSr, 0.2 * decSr);
            targetTd[i] = sampleTd * (1.0 + ((double) kLoopTdFactor[i] - 1.0) * scatterF);
            predelay[i] = juce::jlimit (1, pre[i].mask - 1, (int) (targetTd[i] * 0.5));
            if (snap) { curTd[i] = targetTd[i]; a0[i] = a0Target[i]; a1[i] = a1Target[i]; }
        }
        chaosAmt = 0.07 * std::pow ((double) chaos, 2.5);
        loopGain = (float) std::pow (0.001, (double) td / (double) t60);
        dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 10.0 / decSr);
        // Tone: 80 Hz .. 5 kHz, five octaves wide
        const double eqF = juce::jmin (0.95, (double) (80.0f * std::pow (5000.0f / 80.0f, tone)) * freqScale * rate);
        eqG = std::tan (juce::MathConstants<double>::pi * eqF * 0.5);
        eqR = std::pow (2.0, 2.5) - std::pow (2.0, -2.5) * 0.5;
        eqDen = 1.0 / (1.0 + eqG * (2.0 * eqR + eqG));
        // the glides, at the tank's rate
        tdGlide = 1.0 - std::exp (-1.0 / (0.05 * decSr));
        coeffGlide = (float) (1.0 - std::exp (-1.0 / (0.005 * decSr)));
        chaosGlide = 1.0 - std::exp (-1.0 / (0.2 * decSr));
        // dry and wet (equal power), the width as a rotation
        dry = std::cos (juce::MathConstants<double>::halfPi * drywet);
        const double wet = std::sin (juce::MathConstants<double>::halfPi * drywet);
        const double theta = juce::MathConstants<double>::pi * 0.25 * (1.0 - width);
        wetC = std::cos (theta) * wet; wetX = std::sin (theta) * wet;
    }

    Delay loop[kN], pre[kN];
    std::array<std::array<float, 2>, kCascadeL> ap[kN] {};
    BQ lowpass[kN][5];
    Butter8 down, upL, upR;
    double eqS1[kN] {}, eqS2[kN] {}, eqG { 0.1 }, eqR { 5.5 }, eqDen { 1.0 };
    float dcX[kN] {}, dcY[kN] {}, dcR { 0.999f }, ripplePrev[kN] {};
    float a0[kN] {}, a1[kN] {}, a0Target[kN] {}, a1Target[kN] {}, loopGain { 0.0f }, coeffGlide { 0.01f };
    double targetTd[kN] {}, curTd[kN] {}, lfoPhase[kN] {}, decPeriod { 1.0 / 48000.0 }, tdGlide { 0.001 };
    double chaosNow { 0.0 }, chaosTarget { 0.0 }, chaosAmt { 0.0 }, chaosGlide { 0.0001 };
    int predelay[kN] {}, stages { 1 }, rate { 1 }, phase { 0 }, chaosCount { 0 };
    double dry { 1.0 }, wetC { 0.0 }, wetX { 0.0 }, heldL { 0.0 }, heldR { 0.0 };
    juce::Random random { 0x5eed };
    uint32_t counter { 0 };
};

AQUANODE_REGISTER_MODULE (SpringTankModule, SpringTankModule::descriptor)
