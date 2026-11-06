#pragma once
//==============================================================================
// Tidebreeze - effects
//
// Every effect takes five parameters 0..1 (what they mean depends on the
// type; paramNames() names them for the editor) and processes a stereo
// buffer in place. As an insert it returns its full output (the slot mixes
// it with the dry signal); as a master effect the program sends into it and
// its wet output returns.
//==============================================================================
#include "TbDsp.h"

namespace tb
{
    enum EffectType
    {
        FxNone, FxPhaser, FxChorus, FxEnsemble, FxHarmonicChorus, FxFlanger, FxModDelay, FxSequenceDelay,
        FxMultitap, FxRandomFilter, FxAutoPan, FxDecimator, FxTiltEq, FxHall, FxSmoothHall, FxWetPlate,
        FxUndertow, FxBreezeBands, NumEffectTypes
    };

    inline const juce::StringArray& effectNames()
    {
        static const juce::StringArray n { "No Effect", "Phaser", "Chorus", "Ensemble", "Split Chorus",
                                           "Flanger", "Mod Delay", "Pattern Delay", "Multitap Echo",
                                           "Drift Filter", "Auto Pan", "Decimator", "Tilt EQ", "Hall",
                                           "Velvet Hall", "Bright Plate", "Undertow", "Breeze Bands" };
        return n;
    }
    inline juce::StringArray effectParamNames (int type)
    {
        switch (type)
        {
            case FxPhaser:         return { "Rate", "Depth", "Manual", "Feedback", "Spread" };
            case FxChorus:         return { "Rate", "Depth", "Delay", "Feedback", "Spread" };
            case FxEnsemble:       return { "Rate", "Depth", "Shimmer", "Tone", "Spread" };
            case FxHarmonicChorus: return { "Rate", "Depth", "Split", "Feedback", "Spread" };
            case FxFlanger:        return { "Rate", "Depth", "Delay", "Feedback", "Spread" };
            case FxModDelay:       return { "Time", "L/R Ratio", "Feedback", "Mod Depth", "Mod Rate" };
            case FxSequenceDelay:  return { "Pattern", "Division", "Feedback", "Damping", "Spread" };
            case FxMultitap:       return { "Time", "Feedback", "Cho Depth", "Cho Rate", "Spread" };
            case FxRandomFilter:   return { "Rate", "Depth", "Resonance", "Centre", "Glide" };
            case FxAutoPan:        return { "Rate", "Depth", "Shape", "Sync", "Phase" };
            case FxDecimator:      return { "Rate", "Bits", "Tone", "Drive", "Hold" };
            case FxTiltEq:         return { "Low", "High", "Presence", "Exciter", "Air" };
            case FxHall: case FxSmoothHall: case FxWetPlate:
                                   return { "Time", "Pre Delay", "Damping", "Size", "Low Cut" };
            case FxUndertow:       return { "Depth", "Rate", "Wobble", "Resonance", "Drift" };
            case FxBreezeBands:    return { "Rate", "Depth", "Resonance", "Centre", "Spread" };
            default:               return { "-", "-", "-", "-", "-" };
        }
    }

    //==========================================================================
    struct DelayLine
    {
        std::vector<float> buf; int w = 0, mask = 0;
        void init (int maxSamples)
        {
            int n = 1; while (n < maxSamples + 4) n <<= 1;
            buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        inline float tap (float delaySamples) const
        {
            const float p = (float) w - 1.0f - delaySamples;
            const int i = (int) std::floor (p);
            const float f = p - (float) i;
            const float a = buf[(size_t) (i & mask)], b = buf[(size_t) ((i + 1) & mask)];
            return a + (b - a) * f;
        }
        inline float tapInt (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    };

    struct Effect
    {
        virtual ~Effect() = default;
        virtual void prepare (double sampleRate) { sr = sampleRate; }
        virtual void reset() {}
        virtual void set (const float* p, double bpm) { std::copy (p, p + 5, par); tempo = bpm; }
        virtual void process (float* L, float* R, int n) = 0;
        double sr = 44100.0, tempo = 120.0;
        float par[5] {};
    };

    //==========================================================================
    struct Phaser : Effect
    {
        float apL[8] {}, apR[8] {}, phase = 0.0f, fbL = 0.0f, fbR = 0.0f;
        void reset() override { std::fill (apL, apL + 8, 0.0f); std::fill (apR, apR + 8, 0.0f); fbL = fbR = 0.0f; }
        void process (float* L, float* R, int n) override
        {
            const float rate = 0.02f * std::pow (400.0f, par[0]);
            const float depth = par[1];
            const float centre = 120.0f * std::pow (40.0f, par[2]);
            const float fb = (par[3] * 2.0f - 1.0f) * 0.85f;
            const float spread = par[4] * 0.5f;
            for (int i = 0; i < n; ++i)
            {
                phase += rate / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
                auto stage = [&] (float* ap, float x, float ph, float& fbs)
                {
                    const float lfo = 0.5f - 0.5f * std::cos (kTwoPi * ph);
                    const float f = juce::jlimit (40.0f, (float) sr * 0.4f, centre * std::pow (2.0f, (lfo - 0.5f) * depth * 5.0f));
                    const float t = std::tan (kPi * f / (float) sr);
                    const float a = (t - 1.0f) / (t + 1.0f);
                    float y = x + fbs * fb;
                    for (int s = 0; s < 8; ++s)
                    {
                        const float o = a * y + ap[s];
                        ap[s] = y - a * o;
                        y = o;
                    }
                    fbs = y;
                    return 0.5f * (x + y);
                };
                L[i] = stage (apL, L[i], phase, fbL);
                R[i] = stage (apR, R[i], phase + spread, fbR);
            }
        }
    };

    //==========================================================================
    // chorus family: modulated delay taps
    //==========================================================================
    struct ChorusBase : Effect
    {
        DelayLine dl, dr; float phase = 0.0f, phase2 = 0.0f, fbL = 0.0f, fbR = 0.0f;
        Svf splitL, splitR, toneL, toneR;
        void prepare (double s) override { Effect::prepare (s); dl.init ((int) (s * 0.06)); dr.init ((int) (s * 0.06)); }
        void reset() override { dl.clear(); dr.clear(); fbL = fbR = 0.0f; splitL.reset(); splitR.reset(); toneL.reset(); toneR.reset(); }
    };

    struct Chorus : ChorusBase
    {
        int mode = 0;   // 0 chorus, 1 harmonic chorus (only above the split), 2 flanger
        void process (float* L, float* R, int n) override
        {
            const float rate = 0.03f * std::pow (200.0f, par[0]);
            const float depthMs = mode == 2 ? par[1] * 4.0f : par[1] * 8.0f;
            const float baseMs = mode == 2 ? 0.3f + par[2] * 6.0f : 2.0f + par[2] * 22.0f;
            const float fb = mode == 2 ? (par[3] * 2.0f - 1.0f) * 0.92f : par[3] * 0.6f;
            const float spread = par[4] * 0.5f;
            if (mode == 1) { const float hz = 120.0f * std::pow (25.0f, par[2]); splitL.set (hz, 0.6f, sr); splitR.set (hz, 0.6f, sr); }
            const float msToS = (float) sr * 0.001f;
            for (int i = 0; i < n; ++i)
            {
                phase += rate / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
                const float mL = 0.5f + 0.5f * std::sin (kTwoPi * phase);
                const float mR = 0.5f + 0.5f * std::sin (kTwoPi * (phase + spread));
                float inL = L[i], inR = R[i], lowL = 0.0f, lowR = 0.0f;
                if (mode == 1) { lowL = splitL.low (inL); lowR = splitR.low (inR); inL -= lowL; inR -= lowR; }
                dl.push (inL + fbL * fb); dr.push (inR + fbR * fb);
                const float wl = dl.tap ((mode == 1 ? 7.0f : baseMs) * msToS + mL * depthMs * msToS);
                const float wr = dr.tap ((mode == 1 ? 7.0f : baseMs) * msToS + mR * depthMs * msToS);
                fbL = wl; fbR = wr;
                if (mode == 1) { L[i] = lowL + 0.5f * (inL + wr); R[i] = lowR + 0.5f * (inR + wl); }
                else { L[i] = 0.5f * (L[i] + wl); R[i] = 0.5f * (R[i] + wr); }
            }
        }
    };

    struct Ensemble : ChorusBase
    {
        void process (float* L, float* R, int n) override
        {
            const float rate = 0.05f * std::pow (60.0f, par[0]);
            const float depthMs = 0.5f + par[1] * 6.0f;
            const float shimmer = par[2];
            const float toneHz = 2000.0f * std::pow (8.0f, par[3]);
            toneL.set (toneHz, 0.6f, sr); toneR.set (toneHz, 0.6f, sr);
            const float spread = par[4];
            const float msToS = (float) sr * 0.001f;
            for (int i = 0; i < n; ++i)
            {
                phase += rate / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
                phase2 += rate * 7.3f / (float) sr; if (phase2 >= 1.0f) phase2 -= 1.0f;
                const float m = 0.5f * (L[i] + R[i]);
                dl.push (m);
                float wl = 0.0f, wr = 0.0f;
                for (int v = 0; v < 3; ++v)
                {
                    const float ph = phase + (float) v / 3.0f;
                    const float mod = std::sin (kTwoPi * ph) + shimmer * 0.25f * std::sin (kTwoPi * (phase2 + (float) v / 3.0f));
                    const float t = dl.tap ((8.0f + depthMs * (0.5f + 0.5f * mod)) * msToS);
                    const float pan = ((float) v - 1.0f) * spread;
                    wl += t * (0.5f - 0.5f * pan); wr += t * (0.5f + 0.5f * pan);
                }
                L[i] = 0.5f * L[i] + toneL.low (wl) * 0.66f;
                R[i] = 0.5f * R[i] + toneR.low (wr) * 0.66f;
            }
        }
    };

    //==========================================================================
    // delays
    //==========================================================================
    struct ModDelay : Effect
    {
        DelayLine dl, dr; float phase = 0.0f, fbL = 0, fbR = 0; Svf dampL, dampR;
        void prepare (double s) override { Effect::prepare (s); dl.init ((int) (s * 2.2)); dr.init ((int) (s * 2.2)); }
        void reset() override { dl.clear(); dr.clear(); fbL = fbR = 0; dampL.reset(); dampR.reset(); }
        void process (float* L, float* R, int n) override
        {
            const float tL = 0.01f * std::pow (150.0f, par[0]);          // 10 ms .. 1.5 s
            const float tR = tL * (0.5f + par[1]);
            const float fb = par[2] * 0.9f;
            const float depth = par[3] * 0.006f;
            const float rate = 0.05f * std::pow (100.0f, par[4]);
            dampL.set (6500.0f, 0.6f, sr); dampR.set (6500.0f, 0.6f, sr);
            for (int i = 0; i < n; ++i)
            {
                phase += rate / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
                const float m = std::sin (kTwoPi * phase) * depth;
                dl.push (L[i] + fbL * fb); dr.push (R[i] + fbR * fb);
                const float wl = dl.tap ((tL + m) * (float) sr), wr = dr.tap ((tR - m) * (float) sr);
                fbL = dampL.low (wl); fbR = dampR.low (wr);
                L[i] = wl; R[i] = wr;   // wet only: the insert's Dry/Wet mixes it
            }
        }
    };

    struct SequenceDelay : Effect
    {
        // four taps on a tempo grid; the pattern picks which ones play, how
        // loud and where: the rhythmic L/C/R echoes of the workstation delays
        DelayLine dl, dr; float fbL = 0, fbR = 0; Svf dampL, dampR;
        void prepare (double s) override { Effect::prepare (s); dl.init ((int) (s * 6.0)); dr.init ((int) (s * 6.0)); }
        void reset() override { dl.clear(); dr.clear(); fbL = fbR = 0; dampL.reset(); dampR.reset(); }
        void process (float* L, float* R, int n) override
        {
            static const float patterns[8][4][2] = {   // { level, pan -1..1 } per tap
                { { 1, -1 }, { 0.8f, 1 }, { 0.6f, -0.5f }, { 0.45f, 0.5f } },
                { { 1, 0 }, { 0, 0 }, { 0.7f, -1 }, { 0.5f, 1 } },
                { { 0.9f, -1 }, { 0.9f, 1 }, { 0, 0 }, { 0.6f, 0 } },
                { { 0, 0 }, { 1, -0.7f }, { 0, 0 }, { 0.8f, 0.7f } },
                { { 1, 1 }, { 0.5f, -1 }, { 0.8f, 1 }, { 0.4f, -1 } },
                { { 0.8f, -0.3f }, { 0.6f, 0.3f }, { 0.4f, -0.6f }, { 0.25f, 0.6f } },
                { { 1, -1 }, { 0, 0 }, { 0, 0 }, { 0.9f, 1 } },
                { { 0.7f, 0 }, { 0.9f, -1 }, { 0.5f, 1 }, { 0.9f, 0 } } };
            const int pat = juce::jlimit (0, 7, (int) (par[0] * 7.99f));
            static const float divs[] = { 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f };
            const float beats = divs[juce::jlimit (0, 5, (int) (par[1] * 5.99f))];
            const float stepSec = juce::jlimit (0.03f, 1.45f, beats * 60.0f / (float) std::max (30.0, tempo));
            const float fb = par[2] * 0.75f;
            const float damp = 1500.0f * std::pow (10.0f, 1.0f - par[3]);
            dampL.set (damp, 0.6f, sr); dampR.set (damp, 0.6f, sr);
            const float spread = par[4];
            const float step = stepSec * (float) sr;
            for (int i = 0; i < n; ++i)
            {
                dl.push (L[i] + fbL * fb); dr.push (R[i] + fbR * fb);
                float wl = 0, wr = 0;
                for (int t = 0; t < 4; ++t)
                {
                    const float lvl = patterns[pat][t][0];
                    if (lvl <= 0.0f) continue;
                    const float pan = patterns[pat][t][1] * spread;
                    const float d = step * (float) (t + 1);
                    const float x = 0.5f * (dl.tap (d) + dr.tap (d)) * lvl;
                    wl += x * (0.5f - 0.5f * pan); wr += x * (0.5f + 0.5f * pan);
                }
                fbL = dampL.low (dl.tap (step * 4.0f)); fbR = dampR.low (dr.tap (step * 4.0f));
                L[i] = L[i] * 0.6f + wl * 0.8f; R[i] = R[i] * 0.6f + wr * 0.8f;
            }
        }
    };

    struct Multitap : Effect
    {
        Chorus chorus; ModDelay delay;
        void prepare (double s) override { Effect::prepare (s); chorus.prepare (s); delay.prepare (s); }
        void reset() override { chorus.reset(); delay.reset(); }
        void process (float* L, float* R, int n) override
        {
            const float c[5] = { par[3], par[2], 0.4f, 0.2f, 0.5f };
            const float d[5] = { par[0], 0.5f + par[4] * 0.5f, par[1], 0.0f, 0.0f };
            chorus.set (c, tempo); delay.set (d, tempo);
            chorus.process (L, R, n); delay.process (L, R, n);
        }
    };

    //==========================================================================
    struct RandomFilter : Effect
    {
        Svf fl, fr; float cl = 0, cr = 0, tl = 0, tr = 0, phase = 0; juce::uint32 seed = 12345;
        float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (1.0f / 16777216.0f); }
        void reset() override { fl.reset(); fr.reset(); }
        void process (float* L, float* R, int n) override
        {
            static const float divs[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f };
            const float rate = par[0] < 0.5f ? (float) tempo / 60.0f / divs[juce::jlimit (0, 4, 4 - (int) (par[0] * 9.99f))]
                                             : 0.1f * std::pow (100.0f, (par[0] - 0.5f) * 2.0f);
            const float depthOct = par[1] * 5.0f;
            const float q = 0.7f + par[2] * par[2] * 12.0f;
            const float centre = 150.0f * std::pow (40.0f, par[3]);
            const float glide = std::exp (-1.0f / ((0.001f + par[4] * 0.2f) * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                phase += rate / (float) sr;
                if (phase >= 1.0f) { phase -= 1.0f; tl = rnd() - 0.5f; tr = rnd() - 0.5f; }
                cl = tl + glide * (cl - tl); cr = tr + glide * (cr - tr);
                if ((i & 7) == 0)
                {
                    fl.set (centre * std::exp2 (cl * depthOct), q, sr);
                    fr.set (centre * std::exp2 (cr * depthOct), q, sr);
                }
                float b, h;
                fl.process (L[i], b, h); L[i] = b * 1.4f;
                fr.process (R[i], b, h); R[i] = b * 1.4f;
            }
        }
    };

    struct AutoPan : Effect
    {
        float phase = 0;
        void process (float* L, float* R, int n) override
        {
            float rate = 0.05f * std::pow (200.0f, par[0]);
            if (par[3] > 0.5f) rate = (float) tempo / 60.0f / std::pow (2.0f, std::round (par[0] * 4.0f) - 2.0f);
            const float depth = par[1], sq = par[2];
            for (int i = 0; i < n; ++i)
            {
                phase += rate / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
                float s = std::sin (kTwoPi * (phase + par[4] * 0.25f));
                s = juce::jlimit (-1.0f, 1.0f, s * (1.0f + sq * 6.0f));
                const float p = s * depth;
                const float m = 0.5f * (L[i] + R[i]);
                const float side = 0.5f * (L[i] - R[i]);
                L[i] = (m + side) * (1.0f - p * 0.9f);
                R[i] = (m - side) * (1.0f + p * 0.9f);
            }
        }
    };

    struct Decimator : Effect
    {
        float holdL = 0, holdR = 0, acc = 0; Svf tl, tr;
        void reset() override { tl.reset(); tr.reset(); holdL = holdR = acc = 0; }
        void process (float* L, float* R, int n) override
        {
            const float rate = 1000.0f * std::pow (44.0f, par[0]);
            const float bits = 4.0f + par[1] * 12.0f;
            const float q = std::exp2 (bits - 1.0f);
            const float drive = 1.0f + par[3] * 6.0f;
            tl.set (1500.0f * std::pow (12.0f, par[2]), 0.6f, sr); tr.set (1500.0f * std::pow (12.0f, par[2]), 0.6f, sr);
            for (int i = 0; i < n; ++i)
            {
                acc += rate / (float) sr;
                if (acc >= 1.0f)
                {
                    acc -= std::floor (acc);
                    holdL = std::round (std::tanh (L[i] * drive) * q) / q;
                    holdR = std::round (std::tanh (R[i] * drive) * q) / q;
                }
                L[i] = tl.low (holdL); R[i] = tr.low (holdR);
            }
        }
    };

    struct TiltEq : Effect
    {
        Svf lo[2], hi[2], pr[2], air[2];
        void reset() override { for (int c = 0; c < 2; ++c) { lo[c].reset(); hi[c].reset(); pr[c].reset(); air[c].reset(); } }
        void process (float* L, float* R, int n) override
        {
            const float gLo = dbToGain ((par[0] - 0.5f) * 24.0f), gHi = dbToGain ((par[1] - 0.5f) * 24.0f);
            const float gPr = dbToGain ((par[2] - 0.5f) * 18.0f), gAir = dbToGain ((par[4] - 0.5f) * 18.0f);
            const float exc = par[3];
            for (int c = 0; c < 2; ++c) { lo[c].set (220.0f, 0.6f, sr); hi[c].set (3500.0f, 0.6f, sr); pr[c].set (2500.0f, 1.2f, sr); air[c].set (11000.0f, 0.6f, sr); }
            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = ch[c][i];
                    float b, h;
                    const float l = lo[c].process (x, b, h);
                    float y = x + (gLo - 1.0f) * l;
                    hi[c].process (y, b, h); y += (gHi - 1.0f) * h;
                    pr[c].process (y, b, h); y += (gPr - 1.0f) * b;
                    air[c].process (y, b, h); y += (gAir - 1.0f) * h + exc * 0.5f * std::tanh (h * 4.0f);
                    ch[c][i] = y;
                }
        }
    };

    //==========================================================================
    // reverbs: diffusers into an 8-line feedback delay network
    //==========================================================================
    struct Reverb : Effect
    {
        int flavour = 0;   // 0 hall, 1 smooth hall, 2 wet plate
        DelayLine pre, lines[8], diff[4];
        float lp[8] {}, phase = 0.0f;
        Svf lowCutL, lowCutR;
        float apState[4] {};
        static constexpr float baseMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 61.3f, 67.1f, 79.3f };
        static constexpr float diffMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
        void prepare (double s) override
        {
            Effect::prepare (s);
            pre.init ((int) (s * 0.25));
            for (auto& l : lines) l.init ((int) (s * 0.25));
            for (auto& d : diff) d.init ((int) (s * 0.03));
        }
        void reset() override
        {
            pre.clear(); for (auto& l : lines) l.clear(); for (auto& d : diff) d.clear();
            std::fill (lp, lp + 8, 0.0f); lowCutL.reset(); lowCutR.reset();
        }
        void process (float* L, float* R, int n) override
        {
            const float t60 = (flavour == 2 ? 0.4f : 0.6f) * std::pow (flavour == 2 ? 12.0f : 16.0f, par[0]);
            const float preMs = par[1] * 180.0f;
            const float dampHz = 20000.0f * std::pow (0.08f, par[2]) * (flavour == 2 ? 1.3f : 1.0f);
            const float size = 0.6f + par[3] * 1.0f;
            const float dampA = std::exp (-kTwoPi * std::min (dampHz, (float) sr * 0.45f) / (float) sr);
            lowCutL.set (20.0f * std::pow (25.0f, par[4]), 0.6f, sr); lowCutR.set (20.0f * std::pow (25.0f, par[4]), 0.6f, sr);
            const float msToS = (float) sr * 0.001f;
            float d[8], g[8];
            for (int k = 0; k < 8; ++k)
            {
                d[k] = baseMs[k] * size * (flavour == 2 ? 0.55f : 1.0f) * msToS;
                g[k] = std::pow (10.0f, -3.0f * (d[k] / (float) sr) / t60);
            }
            const float diffAmt = flavour == 1 ? 0.7f : (flavour == 2 ? 0.75f : 0.6f);
            const float modDepth = flavour == 1 ? 12.0f : 4.0f;
            for (int i = 0; i < n; ++i)
            {
                phase += 0.37f / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
                pre.push (0.5f * (L[i] + R[i]));
                float x = pre.tap (std::max (1.0f, preMs * msToS));
                for (int a = 0; a < 4; ++a)
                {
                    const float dd = diffMs[a] * msToS * (flavour == 2 ? 0.7f : 1.0f);
                    const float v = diff[a].tap (dd);
                    const float y = -diffAmt * x + v;
                    diff[a].push (x + diffAmt * y);
                    x = y;
                }
                float o[8];
                for (int k = 0; k < 8; ++k)
                {
                    const float mod = (k & 1) ? std::sin (kTwoPi * (phase + (float) k * 0.125f)) : 0.0f;
                    o[k] = lines[k].tap (d[k] + mod * modDepth);
                    lp[k] = o[k] + dampA * (lp[k] - o[k]);
                    o[k] = lp[k] * g[k];
                }
                // Householder mix
                float sum = 0.0f; for (int k = 0; k < 8; ++k) sum += o[k];
                sum *= 0.25f;
                for (int k = 0; k < 8; ++k) lines[k].push ((o[k] - sum) + (k < 4 ? x : -x) * 0.5f);
                const float wl = (o[0] + o[2] + o[4] + o[6]) * 0.5f, wr = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
                float b, h;
                lowCutL.process (wl, b, h); L[i] = h;
                lowCutR.process (wr, b, h); R[i] = h;
            }
        }
    };

    //==========================================================================
    // Undertow: the sound as if heard from under the surface - a resonant
    // low-pass swaying with the swell, a slow pitch wobble, and the two
    // sides drifting apart
    //==========================================================================
    struct Undertow : Effect
    {
        Svf fl, fr; DelayLine dl, dr; float phase = 0.0f, phase2 = 0.0f;
        void prepare (double s) override { Effect::prepare (s); dl.init ((int) (s * 0.05)); dr.init ((int) (s * 0.05)); }
        void reset() override { fl.reset(); fr.reset(); dl.clear(); dr.clear(); }
        void process (float* L, float* R, int n) override
        {
            const float depth = par[0];
            const float rate = 0.03f * std::pow (40.0f, par[1]);
            const float wobbleMs = par[2] * 6.0f;
            const float q = 0.7f + par[3] * 6.0f;
            const float drift = par[4];
            const float msToS = (float) sr * 0.001f;
            for (int i = 0; i < n; ++i)
            {
                phase += rate / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
                phase2 += rate * 0.37f / (float) sr; if (phase2 >= 1.0f) phase2 -= 1.0f;
                const float sw = 0.5f - 0.5f * std::cos (kTwoPi * phase);
                if ((i & 7) == 0)
                {
                    const float c = 18000.0f * std::pow (2.0f, -depth * 6.0f * (0.4f + 0.6f * sw));
                    fl.set (c * (1.0f + drift * 0.3f * std::sin (kTwoPi * phase2)), q, sr);
                    fr.set (c * (1.0f - drift * 0.3f * std::sin (kTwoPi * phase2)), q, sr);
                }
                dl.push (L[i]); dr.push (R[i]);
                const float m = 1.0f + std::sin (kTwoPi * phase2);
                const float wl = dl.tap ((1.0f + wobbleMs * m * 0.5f) * msToS);
                const float wr = dr.tap ((1.0f + wobbleMs * (2.0f - m) * 0.5f) * msToS);
                L[i] = fl.low (wl); R[i] = fr.low (wr);
            }
        }
    };

    //==========================================================================
    // Breeze Bands: twelve resonant band-passes on each side, each drifting on
    // a slow random path of its own around the Centre (over Depth octaves), so
    // the sound is heard through a cloud of moving narrow windows - wind
    // through grass, breath through a reed screen. Their sum is held at the
    // level of what comes in (slowly), so no setting makes it vanish or jump.
    //   p0 Rate: how fast the bands wander (0.02 .. 2 Hz)
    //   p1 Depth: how far, 0.3 .. 4 octaves around the centre
    //   p2 Resonance: how narrow the bands are (Q 1.5 .. 24)
    //   p3 Centre: 200 Hz .. 6 kHz
    //   p4 Spread: 0 the two sides move together, 1 independently
    //==========================================================================
    struct BreezeBands : Effect
    {
        static constexpr int kBands = 12, kCtl = 32;
        struct Band { Svf f[2]; float pos[2] {}, from[2] {}, to[2] {}, t = 0.0f, rate = 1.0f, home = 0.0f; };
        std::array<Band, kBands> bands;
        juce::uint32 seed = 0x5eed1234u;
        float inPow = 1.0e-6f, outPow = 1.0e-6f, gain = 1.0f;
        int ctl = 0;

        float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (2.0f / 16777216.0f) - 1.0f; }

        void reset() override
        {
            for (int b = 0; b < kBands; ++b)
            {
                auto& band = bands[(size_t) b];
                band.f[0].reset(); band.f[1].reset();
                band.home = ((float) b + 0.5f) / (float) kBands * 2.0f - 1.0f;   // spread evenly to begin with
                band.rate = 0.6f + 0.8f * (0.5f + 0.5f * rnd());
                band.t = 0.5f + 0.5f * rnd();
                for (int c = 0; c < 2; ++c) { band.from[c] = band.pos[c] = band.home; band.to[c] = rnd(); }
            }
            inPow = outPow = 1.0e-6f; gain = 1.0f; ctl = 0;
        }
        void prepare (double s) override { Effect::prepare (s); reset(); }

        void process (float* L, float* R, int n) override
        {
            const float rateHz = 0.02f * std::pow (100.0f, par[0]);
            const float depthOct = 0.3f + par[1] * 3.7f;
            const float q = 1.5f * std::pow (16.0f, par[2]);
            const float centre = 200.0f * std::pow (30.0f, par[3]);
            const float spread = par[4];
            // the level match: what comes in over 350 ms, what goes out quick to
            // rise (15 ms - a band landing on a loud partial) and slow to fall
            const float inA = std::exp (-1.0f / (0.35f * (float) sr)), outUp = std::exp (-1.0f / (0.015f * (float) sr));
            for (int i = 0; i < n; ++i)
            {
                if (ctl-- <= 0)
                {
                    // each band's path: glides (raised cosine) to a new random point, then the next
                    ctl = kCtl - 1;
                    const float dt = (float) kCtl / (float) sr;
                    for (auto& band : bands)
                    {
                        band.t += dt * rateHz * band.rate;
                        if (band.t >= 1.0f)
                        {
                            band.t -= std::floor (band.t);
                            for (int c = 0; c < 2; ++c) band.from[c] = band.to[c];
                            const float shared = rnd();
                            band.to[0] = juce::jlimit (-1.0f, 1.0f, 0.5f * band.home + 0.7f * shared);
                            band.to[1] = juce::jlimit (-1.0f, 1.0f, 0.5f * band.home + 0.7f * (shared * (1.0f - spread) + rnd() * spread));
                        }
                        const float w = 0.5f - 0.5f * std::cos (kPi * band.t);
                        for (int c = 0; c < 2; ++c)
                        {
                            band.pos[c] = band.from[c] + (band.to[c] - band.from[c]) * w;
                            band.f[c].set (centre * std::exp2 (band.pos[c] * depthOct * 0.5f), q, sr);
                        }
                    }
                    const float target = juce::jlimit (0.25f, 8.0f, std::sqrt (inPow / std::max (1.0e-9f, outPow)));
                    gain += (target - gain) * 0.05f;
                }
                const float xl = L[i], xr = R[i];
                float yl = 0.0f, yr = 0.0f;
                for (auto& band : bands)
                {
                    float bp, hp;
                    band.f[0].process (xl, bp, hp); yl += bp * band.f[0].k;   // k * band: unity at its centre
                    band.f[1].process (xr, bp, hp); yr += bp * band.f[1].k;
                }
                inPow = inA * inPow + (1.0f - inA) * 0.5f * (xl * xl + xr * xr);
                const float yp = 0.5f * (yl * yl + yr * yr) * gain * gain, oa = yp > outPow * gain * gain ? outUp : inA;
                outPow = oa * outPow + (1.0f - oa) * 0.5f * (yl * yl + yr * yr);
                L[i] = yl * gain; R[i] = yr * gain;
            }
        }
    };

    inline std::unique_ptr<Effect> makeEffect (int type)
    {
        switch (type)
        {
            case FxPhaser:         return std::make_unique<Phaser>();
            case FxChorus:         return std::make_unique<Chorus>();
            case FxEnsemble:       return std::make_unique<Ensemble>();
            case FxHarmonicChorus: { auto c = std::make_unique<Chorus>(); c->mode = 1; return c; }
            case FxFlanger:        { auto c = std::make_unique<Chorus>(); c->mode = 2; return c; }
            case FxModDelay:       return std::make_unique<ModDelay>();
            case FxSequenceDelay:  return std::make_unique<SequenceDelay>();
            case FxMultitap:       return std::make_unique<Multitap>();
            case FxRandomFilter:   return std::make_unique<RandomFilter>();
            case FxAutoPan:        return std::make_unique<AutoPan>();
            case FxDecimator:      return std::make_unique<Decimator>();
            case FxTiltEq:         return std::make_unique<TiltEq>();
            case FxHall:           { auto r = std::make_unique<Reverb>(); r->flavour = 0; return r; }
            case FxSmoothHall:     { auto r = std::make_unique<Reverb>(); r->flavour = 1; return r; }
            case FxWetPlate:       { auto r = std::make_unique<Reverb>(); r->flavour = 2; return r; }
            case FxUndertow:       return std::make_unique<Undertow>();
            case FxBreezeBands:    return std::make_unique<BreezeBands>();
            default:               return nullptr;
        }
    }
    inline bool isReverb (int type) { return type == FxHall || type == FxSmoothHall || type == FxWetPlate; }
    // effects whose output is their wet signal only (a master send returns it as is)
    inline bool returnsWetOnly (int type) { return isReverb (type) || type == FxModDelay || type == FxBreezeBands; }
}
