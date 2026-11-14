#pragma once
// Massif - insert effects (per voice), master effects and the EQ.
#include "Patch.h"
#include "Filters.h"
#include <array>
#include <cmath>
#include <vector>

namespace massif
{
inline float interpTable5 (const float* t, float x)     // t at x = 0, .25, .5, .75, 1
{
    x = std::clamp (x, 0.0f, 1.0f) * 4.0f; const int i = std::min (3, (int) x); const float f = x - (float) i;
    return t[i] + f * (t[i + 1] - t[i]);
}
inline float interpLog5 (const float* t, float x)
{
    x = std::clamp (x, 0.0f, 1.0f) * 4.0f; const int i = std::min (3, (int) x); const float f = x - (float) i;
    return std::exp (std::log (t[i]) + f * (std::log (t[i + 1]) - std::log (t[i])));
}

// ---------------------------------------------------------------------------
// 90-degree phase splitter (two allpass chains) for the frequency shifter
struct Hilbert
{
    struct AP { float a { 0 }, x1 { 0 }, y1 { 0 }; inline float tick (float x) { const float y = a * (x - y1) + x1; x1 = x; y1 = y; return y; } };
    std::array<AP, 4> re, im; float imDelay { 0 };
    Hilbert()
    {
        static const float c1[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static const float c2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i) { re[(size_t) i].a = c1[i] * c1[i]; im[(size_t) i].a = c2[i] * c2[i]; }
    }
    inline void tick (float x, float& outRe, float& outIm)
    {
        float a = x, b = x;
        for (auto& p : re) a = p.tick (a);
        for (auto& p : im) b = p.tick (b);
        outRe = a; outIm = imDelay; imDelay = b;
    }
};

// ---------------------------------------------------------------------------
struct Insert
{
    float sr { 44100 };
    std::vector<float> buf; int w { 0 };
    float hold { 0 }, holdPh { 0 };
    float lpS { 0 }, hpS { 0 };
    Hilbert hil; double shPh { 0 };
    void prepare (float rate) { sr = rate; reset(); }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; hold = holdPh = 0; lpS = hpS = 0; shPh = 0; hil = Hilbert(); }

    // p1 / p2 = the two knobs, noteHz = key-tracked pitch for S&H / shifter
    inline float tick (int type, float x, float p1, float p2, float noteHz)
    {
        switch (type)
        {
            case itDelay:
            {
                if (buf.empty()) { buf.assign ((size_t) (sr * 0.55f) + 8, 0.0f); w = 0; }
                const float t = 0.0005f + 0.5f * p2 * p2;                 // seconds (to be refined)
                float rp = (float) w - t * sr; while (rp < 0) rp += (float) buf.size();
                // (a position a hair below 0 wraps to exactly the length in float: the index wraps as an integer too)
                const int n = (int) buf.size();
                int i0 = (int) rp; const float fr = rp - (float) i0;
                i0 = ((i0 % n) + n) % n;
                const float d = buf[(size_t) i0] + (buf[(size_t) ((i0 + 1) % n)] - buf[(size_t) i0]) * fr;
                buf[(size_t) w] = x; w = (w + 1) % (int) buf.size();
                return x + p1 * (d - x);
            }
            case itSampleHold:
            {
                const float rate = noteHz * std::exp2 (10.67f * (p2 - 0.5f));
                holdPh += rate / sr;
                if (holdPh >= 1.0f) { holdPh -= std::floor (holdPh); hold = x; }
                return x + p1 * (hold - x);
            }
            case itBitcrush:
            {
                static const float st[5] = { 1.0e-5f, 0.0296f, 0.5004f, 0.9531f, 1.0144f };
                const float q = interpLog5 (st, p2);
                const float y = q * std::round (x / q);
                return x + p1 * (y - x);
            }
            case itFreqShift:
            {
                const float shift = noteHz * (std::exp2 (10.67f * (p2 - 0.5f)) - 1.0f);
                float r, i; hil.tick (x, r, i);
                shPh += shift / sr; shPh -= std::floor (shPh);
                const float c = std::cos (6.2831853f * (float) shPh), s = std::sin (6.2831853f * (float) shPh);
                const float y = r * c - i * s;
                return x + p1 * (y - x);
            }
            case itHPLP:
            {
                const float hpHz = 20.0f * std::exp2 (10.0f * p1), lpHz = 20.0f * std::exp2 (10.0f * p2);
                const float ah = 1.0f - std::exp (-6.2831853f * std::min (hpHz, 0.45f * sr) / sr);
                const float al = 1.0f - std::exp (-6.2831853f * std::min (lpHz, 0.45f * sr) / sr);
                hpS += (x - hpS) * ah; float y = x - hpS;
                lpS += (y - lpS) * al; return lpS;
            }
            case itSineShaper:
            {
                static const float G[5] = { 0.259f, 0.911f, 3.181f, 6.849f, 9.174f };
                static const float A[5] = { 0.962f, 1.122f, 1.001f, 1.003f, 1.093f };
                const float y = interpTable5 (A, p2) * std::sin (interpTable5 (G, p2) * x);
                return x + p1 * (y - x);
            }
            case itParabolicShaper:
            {
                static const float G[5] = { 0.063f, 0.687f, 2.559f, 5.681f, 10.061f };
                static const float A[5] = { 8.259f, 1.112f, 1.005f, 1.005f, 1.005f };
                const float g = interpTable5 (G, p2), a = interpTable5 (A, p2);
                const float v = std::abs (g * x);
                const float s = v < 1.0f ? 1.0f - (1.0f - v) * (1.0f - v) : 1.0f;
                const float y = a * (x < 0 ? -s : s);
                return x + p1 * (y - x);
            }
            case itClip:
            {
                static const float G[5] = { 1.0f, 1.0f, 5.322f, 17.215f, 40.796f };
                static const float A[5] = { 0.821f, 0.988f, 1.005f, 1.005f, 1.005f };
                const float y = interpTable5 (A, p2) * std::clamp (interpLog5 (G, p2) * x, -1.0f, 1.0f);
                return x + p1 * (y - x);
            }
            default: return x;
        }
    }
};

// ---------------------------------------------------------------------------
// stereo delay line with fractional read
struct DelayLine
{
    std::vector<float> b; int w { 0 };
    void prepare (int n) { b.assign ((size_t) n + 4, 0.0f); w = 0; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); }
    inline void push (float x) { b[(size_t) w] = x; w = (w + 1) % (int) b.size(); }
    inline float read (float d) const
    {
        // (a position a hair below 0 wraps to exactly the length in float: that read one sample past the
        // buffer - whatever lay there, once a number of 1e21 - so the index is wrapped as an integer too)
        const int n = (int) b.size();
        float rp = (float) w - 1.0f - d; while (rp < 0) rp += (float) n;
        if (! (rp < (float) n)) rp = std::isfinite (rp) ? std::fmod (rp, (float) n) : 0.0f;
        int i0 = (int) rp; const float f = rp - (float) i0;
        if (i0 >= n) i0 -= n;
        const float a = b[(size_t) i0], c = b[(size_t) ((i0 + 1) % n)];
        return a + (c - a) * f;
    }
};

// The reverb (Reverb and Small Reverb). Measured with noise bursts through the original (size 0.3 / 0.5 / 0.8,
// density 0.2 / 0.8, colour 0 / 0.5 / 1): a fixed 20 ms pre-delay (none in the small one); the two sides
// uncorrelated (L/R correlation 0.00); the decay time bell-shaped over frequency - the lows die away sooner than
// the mids too (at colour 0: 63 Hz 0.53 x, 8 kHz 0.39 x the 500 Hz time; colour 1 makes it nearly flat); the
// level about the same at every size. Built as: pre-delay, two chains of four all-pass diffusers (one per side,
// density sets their gain), an eight-line feedback delay network (Householder matrix) whose lines are spread
// over 1:2.5, each read through a slowly wandering delay (no metallic ringing, no flutter echo), each with a
// three-band loss (lows / mids / highs, the times from size and colour), read out in two orthogonal sign
// patterns for the two sides.
struct Reverb2
{
    // fitted to the measurements (decay times per octave band and levels; 9 settings): mid time scale, low ratio
    // at colour 0, high ratio at colour 0, high split, level, low ratio's colour slope, high ratio's colour slope,
    // early level, the lows' shortening with size, the small one's low ratio, the highs' colour curve
    static float& tune (int i) { static float t[12] = { 1.081f, 0.456f, 0.386f, 1.136f, 0.921f, 0.517f, 0.386f, 0.546f, 1.209f, 2.058f, 0.323f, 0 }; return t[i]; }
    float sr { 44100 };
    std::array<DelayLine, 2> pre;
    std::array<std::array<DelayLine, 4>, 2> ap;
    std::array<DelayLine, 8> line;
    std::array<float, 8> lpLo {}, lpHi {};
    std::array<double, 8> ph {};
    // the block's settings
    float gIn { 0.6f }, wet { 0.2f }, preS { 0 }, aLo { 0 }, aHi { 0 }, early { 0 };
    std::array<float, 2> diffOut {};
    std::array<float, 8> len {}, lenNow {}, gLo {}, gMid {}, gHi {};
    float glideA { 0.0005f };      // the delay lengths follow a size change over ~50 ms (no clicks)
    std::array<std::array<float, 4>, 2> apLen {};
    static constexpr float kLineMs[8] = { 29.7f, 37.1f, 41.1f, 47.3f, 53.0f, 59.9f, 67.7f, 73.9f };
    static constexpr float kApMs[2][4] = { { 4.77f, 3.59f, 12.73f, 9.31f }, { 5.11f, 3.23f, 13.41f, 8.87f } };
    static constexpr float kRate[8] = { 0.31f, 0.43f, 0.57f, 0.67f, 0.79f, 0.89f, 1.03f, 1.13f };

    void prepare (float rate)
    {
        sr = rate;
        for (auto& p : pre) p.prepare ((int) (0.03f * sr) + 8);
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) ap[(size_t) c][(size_t) k].prepare ((int) (kApMs[c][k] * 0.0015f * 2.0f * sr) + 8);
        for (int k = 0; k < 8; ++k) line[(size_t) k].prepare ((int) (kLineMs[k] * 0.001f * 1.75f * 1.01f * sr) + 16);
        for (int k = 0; k < 8; ++k) ph[(size_t) k] = 0.125 * k;
        glideA = 1.0f - std::exp (-1.0f / (0.05f * sr));
        lenNow.fill (0.0f);
        clear();
    }
    void clear()
    {
        for (auto& p : pre) p.clear();
        for (auto& c : ap) for (auto& d : c) d.clear();
        for (auto& l : line) l.clear();
        lpLo = {}; lpHi = {};
    }
    void set (bool small, float size, float density, float color)
    {
        // the mid (500 Hz) decay time against size, as measured (density lengthens it a little: x 0.95 .. 1.1)
        static const float sx[6] = { 0.0f, 0.2f, 0.5f, 0.8f, 0.9f, 1.0f };
        static const float big[6] = { 0.03f, 0.11f, 2.1f, 10.0f, 11.0f, 11.5f }, sml[6] = { 0.02f, 0.06f, 0.25f, 0.8f, 1.0f, 1.2f };
        int si = 0; while (si < 4 && size > sx[si + 1]) ++si;
        const float sf = std::clamp ((size - sx[si]) / (sx[si + 1] - sx[si]), 0.0f, 1.0f);
        const float* tab = small ? sml : big;
        const float tMid = std::exp (std::log (tab[si]) + sf * (std::log (tab[si + 1]) - std::log (tab[si]))) * (0.9f + 0.25f * density) * tune (0);
        const float tLo = tMid * (tune (1) + tune (5) * color) * (small ? tune (9) : std::max (0.3f, 1.0f - tune (8) * (size - 0.5f))),
                    tHi = tMid * (tune (2) + tune (6) * color + tune (10) * color * color);
        const float scale = small ? 0.25f + 0.5f * size : 0.45f + 1.3f * size;
        preS = small ? 1.0f : 0.020f * sr;
        // (at the smallest sizes the diffusers ring shorter too - they would outlast the room: the original's
        // size 0.1 dies away in 60 ms)
        gIn = std::clamp (0.45f + 0.3f * density, 0.0f, 0.8f) * std::min (1.0f, 0.35f + 2.2f * size);
        early = (small ? 0.35f : 0.25f) * (0.3f + 0.7f * density) * tune (7);
        aLo = 1.0f - std::exp (-6.2831853f * 180.0f / sr); aHi = 1.0f - std::exp (-6.2831853f * 1800.0f * tune (3) / sr);
        for (int k = 0; k < 8; ++k)
        {
            len[(size_t) k] = kLineMs[k] * 0.001f * scale * sr;
            const float dsec = len[(size_t) k] / sr;
            gLo[(size_t) k] = std::pow (10.0f, -3.0f * dsec / std::max (0.02f, tLo));
            gMid[(size_t) k] = std::pow (10.0f, -3.0f * dsec / std::max (0.02f, tMid));
            gHi[(size_t) k] = std::pow (10.0f, -3.0f * dsec / std::max (0.02f, tHi));
        }
        const float apScale = small ? std::min (0.6f + 0.6f * size, 0.15f + 2.0f * size) : std::min (0.8f + 0.7f * size, 0.25f + 2.5f * size);
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) apLen[(size_t) c][(size_t) k] = kApMs[c][k] * 0.001f * apScale * sr;
        // the level: the original's is nearly the same at every size
        static const float wx[5] = { 0.0f, 0.3f, 0.5f, 0.8f, 1.0f }, wy[5] = { 0.9f, 0.72f, 0.52f, 0.49f, 0.48f };
        int wi = 0; while (wi < 3 && size > wx[wi + 1]) ++wi;
        const float wf = std::clamp ((size - wx[wi]) / (wx[wi + 1] - wx[wi]), 0.0f, 1.0f);
        wet = (wy[wi] + wf * (wy[wi + 1] - wy[wi])) * tune (4) * (small ? 0.7f : 1.0f);
    }
    inline void tick (float inL, float inR, float& outL, float& outR)
    {
        float in[2] = { inL, inR };
        for (int c = 0; c < 2; ++c)
        {
            pre[(size_t) c].push (in[(size_t) c]);
            float v = pre[(size_t) c].read (preS);
            for (int k = 0; k < 4; ++k)
            {
                auto& d = ap[(size_t) c][(size_t) k];
                const float dd = d.read (apLen[(size_t) c][(size_t) k]);
                const float u = v + gIn * dd; d.push (u); v = dd - gIn * u;
            }
            in[c] = v; diffOut[(size_t) c] = v;
        }
        float s[8];
        for (int k = 0; k < 8; ++k)
        {
            ph[(size_t) k] += kRate[k] / sr; if (ph[(size_t) k] >= 1.0) ph[(size_t) k] -= 1.0;
            const float wob = 1.0f + 0.0045f * std::sin (6.2831853f * (float) ph[(size_t) k]);
            auto& ln = lenNow[(size_t) k];
            ln = ln <= 0.0f ? len[(size_t) k] : ln + (len[(size_t) k] - ln) * glideA;
            s[k] = line[(size_t) k].read (ln * wob);
        }
        float sum = 0; for (float v : s) sum += v; sum *= 0.25f;
        static const float inSign[8] = { 1, 1, -1, 1, 1, -1, -1, -1 };
        for (int k = 0; k < 8; ++k)
        {
            const float v = s[k] - sum;
            lpLo[(size_t) k] += (v - lpLo[(size_t) k]) * aLo;
            lpHi[(size_t) k] += (v - lpHi[(size_t) k]) * aHi;
            const float lo = lpLo[(size_t) k], mid = lpHi[(size_t) k] - lo, hi = v - lpHi[(size_t) k];
            const float fbk = gLo[(size_t) k] * lo + gMid[(size_t) k] * mid + gHi[(size_t) k] * hi;
            line[(size_t) k].push (fbk + inSign[k] * 0.5f * in[k & 1]);
        }
        // two orthogonal read-outs: the sides uncorrelated
        // (the early part the same on both sides: what makes the smallest rooms nearly mono, as measured)
        const float e = early * 0.5f * (diffOut[0] + diffOut[1]);
        outL = wet * (s[0] - s[1] + s[2] - s[3] + s[4] - s[5] + s[6] - s[7] + e);
        outR = wet * (s[0] + s[1] - s[2] - s[3] + s[4] + s[5] - s[6] - s[7] + e);
    }
};

struct MasterFx
{
    float sr { 44100 };
    std::array<DelayLine, 2> mod;            // chorus / flanger lines
    std::array<DelayLine, 2> dly;            // echo
    std::array<float, 2> dlyLp {};
    std::array<std::array<float, 8>, 2> ap {}; // phaser allpass states
    std::array<float, 2> phFb {};
    std::array<float, 2> flFb {};
    double lfoPh { 0 };
    std::array<float, 2> tubeDc {};
    Reverb2 reverb;

    void prepare (float rate)
    {
        sr = rate;
        for (auto& m : mod) m.prepare ((int) (sr * 0.06f));
        for (auto& d : dly) d.prepare ((int) (sr * 2.1f));
        reverb.prepare (sr);
        reset();
    }
    void reset()
    {
        for (auto& m : mod) m.clear(); for (auto& d : dly) d.clear();
        dlyLp = {}; ap = {}; phFb = {}; flFb = {}; tubeDc = {}; lfoPh = 0;
        reverb.clear();
    }

    // dw, p2..p4 = knobs; sync = delay sync times (beats L, R); bpm
    void process (int type, float* L, float* R, int n, float dw, float p2, float p3, float p4, float beatsL, float beatsR, double bpm)
    {
        if (type == fxNone) return;
        const float wetG = std::sin (1.5707963f * dw), dryG = std::cos (1.5707963f * dw);
        // what stays the same over the block, worked out once (the per-sample code below uses it as it did)
        float modRate = 0, dlyA = 0;
        if (type == fxReverb || type == fxReverbSmall)
            reverb.set (type == fxReverbSmall, p2, p3, p4);
        if (type == fxChorus || type == fxChorus1 || type == fxChorusE || type == fxFlangerPlus || type == fxFlangerPlus1
                 || type == fxFlangerMinus || type == fxFlangerMinus1 || type == fxPhaser || type == fxPhaser1)
            modRate = 0.05f * std::exp2 (8.0f * p2);
        else if (type == fxDelay || type == fxDelaySync)
            dlyA = 1.0f - std::exp (-6.2831853f * 22000.0f * std::exp2 (-10.6f * p2) / sr);   // damping: a one-pole low-pass, ~500 Hz at 0.5 (measured with noise)
        // the tubes' gain, make-up and bias offset
        int tubeT = 0; float tubeG = 0, tubePost = 0, tubeOff = 0;
        auto tubeSat = [] (float v, float k)
        {
            const float av = std::abs (v); if (av < 1.0e-9f) return v;
            const float e = (4.0f / k) * std::log (av);
            return e > 30.0f ? v / av : v / std::pow (1.0f + std::exp (e), k / 4.0f);
        };
        // fitted to sines rendered by the original (drive 0..1 at two levels: output level, 2nd and 3rd
        // harmonics): a gain of 2^(a + b drive) into a soft-kneed clipper v / (1 + |v|^(4/k))^(k/4),
        // offset by a bias (the even harmonics of the classic and tube-tube types), then 2^c. Units: the
        // signal before the master volume. The other two knobs do nothing audible.
        static const float tA[3] = { 1.77f, 2.374f, 2.511f }, tBd[3] = { 4.148f, 3.34f, 3.463f }, tC[3] = { -2.111f, -2.313f, -2.595f };
        static const float tK[3] = { 0.609f, 0.889f, 0.05f }, tBias[3] = { 0.179f, 0.382f, 0.0f };
        if (type == fxClassicTube || type == fxTubeTube || type == fxBrightTube)
        {
            tubeT = type == fxClassicTube ? 0 : type == fxTubeTube ? 1 : 2;
            constexpr float mv = 0.8968f * 0.25f;          // the master volume the fit was made at
            tubeG = std::exp2 (tA[tubeT] + tBd[tubeT] * p2) * mv; tubePost = std::exp2 (tC[tubeT]) / mv;
            tubeOff = tubeSat (tBias[tubeT], tK[tubeT]);
        }
        for (int i = 0; i < n; ++i)
        {
            float x[2] = { L[i], R[i] }, y[2] = { x[0], x[1] };
            switch (type)
            {
                case fxReverb: case fxReverbSmall: reverb.tick (x[0], x[1], y[0], y[1]); break;
                case fxChorus: case fxChorus1: case fxChorusE: case fxFlangerPlus: case fxFlangerPlus1: case fxFlangerMinus: case fxFlangerMinus1:
                {
                    const bool flanger = type >= fxFlangerPlus && type <= fxFlangerMinus1;
                    const bool mono = type == fxChorus1 || type == fxFlangerPlus1 || type == fxFlangerMinus1;
                    const float rate = modRate;
                    lfoPh += rate / sr; if (lfoPh > 1) lfoPh -= 1;
                    for (int c = 0; c < 2; ++c)
                    {
                        const float ph = (float) lfoPh + (mono ? 0.0f : 0.5f * (float) c);
                        float lfo = std::sin (6.2831853f * ph);
                        if (type == fxChorusE) lfo = 0.5f * (lfo + std::sin (6.2831853f * (ph * 1.37f + 0.25f)));
                        float dms;
                        if (flanger) dms = 0.3f + 6.0f * p3 * (0.5f + 0.5f * lfo);
                        else dms = 2.0f + 25.0f * p4 + 8.0f * p3 * (0.5f + 0.5f * lfo);
                        const float fbk = flanger ? (type == fxFlangerMinus || type == fxFlangerMinus1 ? -0.95f : 0.95f) * p4 : 0.0f;
                        const float d = mod[(size_t) c].read (std::max (1.0f, dms * 0.001f * sr));
                        mod[(size_t) c].push (x[c] + fbk * d);
                        y[c] = 0.5f * (x[c] + d) * (flanger ? 1.5f : 1.2f);
                    }
                    break;
                }
                case fxPhaser: case fxPhaser1:
                {
                    const bool mono = type == fxPhaser1;
                    const float rate = modRate;
                    lfoPh += rate / sr; if (lfoPh > 1) lfoPh -= 1;
                    for (int c = 0; c < 2; ++c)
                    {
                        const float lfo = 0.5f + 0.5f * std::sin (6.2831853f * ((float) lfoPh + (mono ? 0.0f : 0.25f * (float) c)));
                        const float hz = 200.0f * std::exp2 (4.0f * p3 * lfo);
                        const float t = std::tan (3.14159265f * std::min (hz, 0.45f * sr) / sr); const float a = (t - 1) / (t + 1);
                        float v = x[c] + 0.9f * p4 * phFb[(size_t) c];
                        for (int k = 0; k < 6; ++k) { auto& z = ap[(size_t) c][(size_t) k]; const float o = a * v + z; z = v - a * o; v = o; }
                        phFb[(size_t) c] = v; y[c] = 0.63f * (x[c] + v);
                    }
                    break;
                }
                case fxDelay: case fxDelaySync:
                {
                    float tl, tr, fb, dampK;
                    if (type == fxDelaySync) { tl = (float) (beatsL * 60.0 / bpm); tr = (float) (beatsR * 60.0 / bpm); fb = 0.95f * p3; dampK = p2; }
                    else { tl = 0.001f + 0.5f * p3 * p3; tr = 0.001f + 0.5f * p4 * p4; fb = 0.0f; dampK = p2; }
                    const float t[2] = { tl, tr };
                    const float a = dlyA; (void) dampK;
                    for (int c = 0; c < 2; ++c)
                    {
                        const float d = dly[(size_t) c].read (std::clamp (t[c] * sr, 1.0f, (float) dly[(size_t) c].b.size() - 4.0f));
                        dlyLp[(size_t) c] += (d - dlyLp[(size_t) c]) * a;
                        dly[(size_t) c].push (x[c] + fb * dlyLp[(size_t) c]);
                        y[c] = dlyLp[(size_t) c];
                    }
                    break;
                }
                case fxDimExp:
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        lfoPh += 0.3 / sr / 2; if (lfoPh > 1) lfoPh -= 1;
                        const float lfo = std::sin (6.2831853f * ((float) lfoPh + 0.5f * (float) c));
                        const float d1 = mod[(size_t) c].read ((8.0f + 12.0f * p2 + 0.5f * lfo) * 0.001f * sr);
                        const float d2 = mod[(size_t) (1 - c)].read ((15.0f + 18.0f * p2) * 0.001f * sr);
                        mod[(size_t) c].push (x[c]);
                        y[c] = x[c] + 0.6f * (d1 - 0.5f * d2);
                    }
                    break;
                }
                case fxClassicTube: case fxTubeTube: case fxBrightTube:
                {
                    const int t = tubeT;
                    for (int c = 0; c < 2; ++c)
                    {
                        float v = tubePost * (tubeSat (tubeG * x[c] + tBias[t], tK[t]) - tubeOff);
                        tubeDc[(size_t) c] += (v - tubeDc[(size_t) c]) * 0.0005f; v -= tubeDc[(size_t) c];
                        y[c] = v;
                    }
                    break;
                }
                default: break;
            }
            // dry / wet: equal power (measured on the tube: wet sin, dry cos of dw x 90 degrees)
            L[i] = dryG * x[0] + wetG * y[0];
            R[i] = dryG * x[1] + wetG * y[1];
        }
    }
};

// ---------------------------------------------------------------------------
// master EQ: low shelf, peak, high shelf (cut directions act like slopes, as measured)
struct Biquad
{
    float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; std::array<float, 2> z1 {}, z2 {};
    inline float tick (float x, int c)
    {
        const float y = b0 * x + z1[(size_t) c]; z1[(size_t) c] = b1 * x - a1 * y + z2[(size_t) c]; z2[(size_t) c] = b2 * x - a2 * y; return y;
    }
    void shelf (bool high, float hz, float dB, float sr)
    {
        const float A = std::pow (10.0f, dB / 40.0f), w = 6.2831853f * hz / sr, cs = std::cos (w), sn = std::sin (w);
        const float al = sn / 2.0f * std::sqrt (2.0f), sq = 2.0f * std::sqrt (A) * al;
        float B0, B1, B2, A0, A1, A2;
        if (! high) { B0 = A * ((A + 1) - (A - 1) * cs + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - sq);
                      A0 = (A + 1) + (A - 1) * cs + sq; A1 = -2 * ((A - 1) + (A + 1) * cs); A2 = (A + 1) + (A - 1) * cs - sq; }
        else { B0 = A * ((A + 1) + (A - 1) * cs + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - sq);
               A0 = (A + 1) - (A - 1) * cs + sq; A1 = 2 * ((A - 1) - (A + 1) * cs); A2 = (A + 1) - (A - 1) * cs - sq; }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void peak (float hz, float dB, float Q, float sr)
    {
        const float A = std::pow (10.0f, dB / 40.0f), w = 6.2831853f * hz / sr, cs = std::cos (w), al = std::sin (w) / (2 * Q);
        const float A0 = 1 + al / A;
        b0 = (1 + al * A) / A0; b1 = -2 * cs / A0; b2 = (1 - al * A) / A0; a1 = -2 * cs / A0; a2 = (1 - al / A) / A0;
    }
};

// Measured with noise through the plugin's EQ (16 settings, third-octave transfer curves):
//   low and high: first-order shelves centred at 400 Hz and 4 kHz
//   peak: centre 150 Hz * 46.7^freq, an RBJ peak of Q 1.3
//   all three gains: knob above the middle up to +12 dB as 12 (2b - b^2), below it down to -35 dB as 25b + 10b^2
struct MasterEq
{
    struct Shelf1
    {
        float b0 { 1 }, b1 { 0 }, a1 { 0 }, x1[2] { 0, 0 }, y1[2] { 0, 0 };
        // gain g (linear) below (low) or above (high) the centre
        void set (bool high, float fc, float g, float sr)
        {
            // one prewarped pole; the zero follows from the gain, so it may lie above Nyquist
            const float rg = std::sqrt (std::max (1.0e-4f, g));
            const float K = std::tan (kPi * std::min (high ? fc * rg : fc / rg, 0.45f * sr) / sr);
            if (high) { b0 = (g + K) / (1.0f + K); b1 = (K - g) / (1.0f + K); }
            else      { b0 = (1.0f + g * K) / (1.0f + K); b1 = (g * K - 1.0f) / (1.0f + K); }
            a1 = (K - 1.0f) / (1.0f + K);
        }
        float tick (float x, int c) { const float y = b0 * x + b1 * x1[c] - a1 * y1[c]; x1[c] = x; y1[c] = y; return y; }
    };
    Shelf1 lo, hi; Biquad mid; float sr { 44100 };
    float lastLo { -99 }, lastPk { -99 }, lastPf { -99 }, lastHi { -99 };
    void prepare (float rate) { sr = rate; lastLo = lastPk = lastPf = lastHi = -99; }
    static float gainDb (float knob)
    {
        const float b = 2.0f * std::clamp (knob, 0.0f, 1.0f) - 1.0f;
        return b >= 0.0f ? 12.0f * (2.0f * b - b * b) : -(25.0f * -b + 10.0f * b * b);
    }
    void process (float* L, float* R, int n, float lowK, float peakK, float freqK, float highK)
    {
        if (lowK != lastLo) { lo.set (false, 400.0f, std::pow (10.0f, gainDb (lowK) / 20.0f), sr); lastLo = lowK; }
        if (highK != lastHi) { hi.set (true, 4000.0f, std::pow (10.0f, gainDb (highK) / 20.0f), sr); lastHi = highK; }
        if (peakK != lastPk || freqK != lastPf)
        {
            mid.peak (150.0f * std::pow (46.7f, freqK), gainDb (peakK) * (peakK < 0.5f ? 0.9f : 1.0f), 1.3f, sr);
            lastPk = peakK; lastPf = freqK;
        }
        for (int i = 0; i < n; ++i)
        {
            L[i] = hi.tick (mid.tick (lo.tick (L[i], 0), 0), 0);
            R[i] = hi.tick (mid.tick (lo.tick (R[i], 1), 1), 1);
        }
    }
};
} // namespace massif
