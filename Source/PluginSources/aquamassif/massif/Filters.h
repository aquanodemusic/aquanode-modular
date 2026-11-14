#pragma once
// Massif - the twelve filter types.
//
// Built from two cores, a zero-delay-feedback 4-pole ladder (with a tanh at
// its input) and a state-variable filter, and fitted to measured responses
// of the original (noise through each type, cutoff/resonance/bandwidth
// sweeps; the constants below are the fits). Cutoff knob law for all types:
//     f = A * 2^(10 * (cut - 0.5))       A ~ 1050 Hz (per type below)
#include <algorithm>
#include <cmath>
#include <vector>

namespace massif
{
inline float fastTanh (float x)
{
    if (x > 4.5f) return 1.0f; if (x < -4.5f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ---------------------------------------------------------------------------
constexpr float kPiFilter = 3.14159265f;
struct Svf         // Cytomic / Simper trapezoidal SVF
{
    float ic1 { 0 }, ic2 { 0 };
    float a1 { 0 }, a2 { 0 }, a3 { 0 }, k { 1 };
    void reset() { ic1 = ic2 = 0; }
    void set (float hz, float Q, float sr)
    {
        const float g = std::tan (kPiF * std::clamp (hz, 5.0f, 0.49f * sr) / sr);
        k = 1.0f / std::max (0.02f, Q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    // for an edge that may lie above Nyquist: no prewarping up there, so the response below it keeps its slope
    void setWide (float hz, float Q, float sr)
    {
        const float x = kPiF * std::max (5.0f, hz) / sr;
        const float g = x < 0.7854f ? std::tan (x) : x * 1.2732f;
        k = 1.0f / std::max (0.02f, Q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    // returns lp, bp, hp through refs
    inline void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
        if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0; }
    }
    static constexpr float kPiF = 3.14159265f;
};

// ---------------------------------------------------------------------------
struct Ladder      // 4 x one-pole TPT with feedback k from the last stage
{
    float s[4] { 0, 0, 0, 0 };
    float G { 0 }, g { 0 };
    void reset() { s[0] = s[1] = s[2] = s[3] = 0; }
    void set (float hz, float sr)
    {
        g = std::tan (3.14159265f * std::clamp (hz, 5.0f, 0.49f * sr) / sr);
        G = g / (1.0f + g);
    }
    // y[0..3] = stage outputs, u = input after feedback
    inline void tick (float x, float k, float drive, float* y, float& u, bool fbOnly = false)
    {
        // linear ZDF estimate of y4, then the input saturation
        const float G2 = G * G, G4 = G2 * G2;
        const float b = 1.0f / (1.0f + g);
        const float s0 = s[0] * b, s1 = s[1] * b, s2 = s[2] * b, s3 = s[3] * b;
        const float sigma = G * G * G * s0 + G * G * s1 + G * s2 + s3;
        const float y4est = (G4 * x + sigma) / (1.0f + k * G4);
        if (fbOnly) u = x - k * fastTanh (y4est * drive) / drive;
        else { u = x - k * y4est; if (drive > 0.0f) u = fastTanh (u * drive) / drive; }
        float in = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (in - s[i]) * G;
            const float out = v + s[i];
            s[i] = out + v;
            y[i] = out; in = out;
        }
        for (float& st : s) if (! std::isfinite (st)) st = 0;
    }
    // the same with a one-pole high-pass (TPT, gain Gh) in the feedback path, state hs: the loop loses its grip
    // on everything below the high-pass corner, so the resonance fades out at low cutoffs
    inline void tickFbHp (float x, float k, float drive, float Gh, float& hs, float* y, float& u)
    {
        const float G2 = G * G, G4 = G2 * G2;
        const float b = 1.0f / (1.0f + g);
        const float s0 = s[0] * b, s1 = s[1] * b, s2 = s[2] * b, s3 = s[3] * b;
        const float sigma = G * G * G * s0 + G * G * s1 + G * s2 + s3;
        const float kc = k * G4 * (1.0f - Gh);
        const float y4est = (G4 * x + sigma + kc * hs) / (1.0f + kc);
        u = x - k * (1.0f - Gh) * (y4est - hs);
        if (drive > 0.0f) u = fastTanh (u * drive) / drive;
        float in = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (in - s[i]) * G;
            const float out = v + s[i];
            s[i] = out + v;
            y[i] = out; in = out;
        }
        const float v = (y[3] - hs) * Gh; hs = hs + 2.0f * v;
        for (float& st : s) if (! std::isfinite (st)) st = 0;
        if (! std::isfinite (hs)) hs = 0;
    }
};

// ---------------------------------------------------------------------------
struct Comb
{
    std::vector<float> buf; int w { 0 }; float lp { 0 };
    static float& satK() { static float k = 0.17f; return k; }
    void prepare (float sr) { buf.assign ((size_t) (sr * 0.06f) + 8, 0.0f); w = 0; lp = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); lp = 0; }
    inline float tick (float x, float hz, float fb, float damp, float sr)
    {
        if (buf.empty()) return x;
        const float d = std::clamp (sr / std::max (15.0f, hz), 2.0f, (float) buf.size() - 3.0f);
        float rp = (float) w - d; while (rp < 0) rp += (float) buf.size();
        const int i0 = (int) rp; const float fr = rp - (float) i0;
        const float a = buf[(size_t) i0], c = buf[(size_t) ((i0 + 1) % (int) buf.size())];
        const float dl = a + (c - a) * fr;
        lp += (dl - lp) * damp;
        const float y = x + fb * fastTanh (lp * satK()) / satK();
        buf[(size_t) w] = y; w = (w + 1) % (int) buf.size();
        return y;
    }
};

// ---------------------------------------------------------------------------
// one filter slot
struct Filter
{
    float sr { 44100.0f };
    Ladder lad; Svf a, b, c; Comb comb;
    float hpState { 0 }, hpX { 0 };     // acid's input high-pass
    int type { 0 };

    void prepare (float rate) { sr = rate; comb.prepare (rate); reset(); }
    void reset() { lad.reset(); a.reset(); b.reset(); c.reset(); comb.reset(); hpState = hpX = 0; scrLp = scrHp = 0; acidHs = 0; }

    static float hzOf (float cut, float A) { return A * std::exp2 (10.0f * (cut - 0.5f)); }
    // The band-pass with its bandwidth near 0: its two resonances meet and their peaks stack, which the original's
    // does not do - measured with white noise (the band's power against the original's, dB; the band's width
    // matches already): res 0.3 / 0.6 / 0.9 against bandwidth 0 / 0.02 / 0.05 / 0.1. Gone from bandwidth 0.1 on.
    static float bpNarrowGain (float res, float bw)
    {
        if (bw >= 0.1f) return 1.0f;
        static const float bws[4] = { 0.0f, 0.02f, 0.05f, 0.1f };
        static const float db[3][4] = { { -5.0f, -4.4f, -0.5f, 0.0f }, { -9.8f, -8.5f, -1.2f, 0.0f }, { -19.5f, -12.7f, -2.0f, 0.0f } };
        int j = 0; while (j < 2 && bw > bws[j + 1]) ++j;
        const float tb = std::clamp ((bw - bws[j]) / (bws[j + 1] - bws[j]), 0.0f, 1.0f);
        float row[3];
        for (int i = 0; i < 3; ++i) row[i] = db[i][j] + tb * (db[i][j + 1] - db[i][j]);
        // res 0 -> nothing (no resonance to stack); 0.3 .. 0.9 between the rows; above 0.9 on along the last step
        float d;
        if (res <= 0.3f) d = row[0] * std::max (0.0f, res) / 0.3f;
        else if (res <= 0.6f) d = row[0] + (row[1] - row[0]) * (res - 0.3f) / 0.3f;
        else d = row[1] + (row[2] - row[1]) * (res - 0.6f) / 0.3f;
        return std::pow (10.0f, d / 20.0f);
    }

    // set coefficients for the block (cut/bw/res 0..1, fmHz added)
    float kLad { 0 }, drive { 0.6f }, gainT { 1 }, mixC[5] { 0, 0, 0, 0, 1 };
    float daftMix { 0 }, daftDrive { 8 }, daftIn { 1 };
    // filter FM (the modulation oscillator): only the ladder types take it - LP4, LP2, HP4, HP2, Allpass, Double Notch,
    // Daft and Acid (the original greys its button out for the others). Measured: a linear sweep in Hz around the
    // cutoff, added before Daft's own pole-frequency scaling, bottoming out near 0 Hz (it does not fold over).
    float fmHz { 0 }, fmK { 1 }; bool fmOk { false };
    float acidGh { 0 }, acidHs { 0 };   // acid's feedback high-pass
    inline void fm (float addHz) { if (fmOk) lad.set ((fmHz + addHz) * fmK, sr); }
    static float& ladDrive() { static float d = 0.28f; return d; }
    float combHz { 1000 }, combFb { 0 }, combDamp { 1 };
    float spread { 1 };
    float hpCoef { 0 };
    float scrK { 0 }, scrCoef { 0 }, scrLp { 0 }, scrHp { 0 };
    static constexpr float scrLim = 1.236f;

    void setParams (int t, float cut, float bw, float res, float fmMul)
    {
        if (t != type) { type = t; reset(); }
        res = std::clamp (res, 0.0f, 1.0f); bw = std::clamp (bw, 0.0f, 1.0f);
        fmOk = false; fmK = 1.0f;
        switch (type)
        {
            // LP4, LP2, HP4, HP2, Allpass and Double Notch are one resonant 4-pole ladder read through different tap mixes
            // (measured: noise through each type over a resonance sweep, fitted with the five taps u, y1..y4 free;
            // the feedback came out the same for all of them, k = 4.21 (1 - (1-res)^2), self-oscillating above
            // res 0.78, with the level lifted by 1 + 0.504 k - 0.135 k^2). Daft is the 2-pole tap with its own laws.
            case ftLP4: case ftLP2: case ftHP4: case ftHP2: case ftAllpass: case ftDoubleNotch:
            {
                static const float mix[6][5] = { { 0, 0, 0, 0, 1 }, { 0, 0, 1, 0, 0 }, { 1, -4, 6, -4, 1 }, { 1, -2, 1, 0, 0 },
                                                 { 1, -4, 4, 0, 0 }, { 1, -4, 12, -16, 8 } };
                static const float base[6] = { 1.368f, 1.035f, 1.037f, 0.681f, 0.395f, 0.408f };
                const int m = type == ftLP4 ? 0 : type == ftLP2 ? 1 : type == ftHP4 ? 2 : type == ftHP2 ? 3 : type == ftAllpass ? 4 : 5;
                fmHz = hzOf (cut, 1052.0f) * fmMul; fmOk = true;
                lad.set (fmHz, sr);
                kLad = 4.21f * (1.0f - (1.0f - res) * (1.0f - res));
                gainT = base[m] * (1.0f + 0.504f * kLad - 0.135f * kLad * kLad);
                for (int i = 0; i < 5; ++i) mixC[i] = mix[m][i];
                break;
            }
            // Daft: the 2-pole tap with the saturation in the feedback only (loud input stays clean), a pole frequency that
            // drops with resonance and comes back a little once it sings. Above res 0.5 it sings by itself: there the
            // drive, the gain, the feedback, the pole frequency and a touch of the 3-pole tap were fitted per resonance
            // (0.6 / 0.75 / 0.9 / 1.0) to noise at two levels and two cutoffs - the tone's pitch (within 20 cents), its
            // level, how cleanly it rings over loud noise, and the noise's spectrum around it (2 dB rms)
            case ftDaft:
            {
                fmHz = hzOf (cut, 1053.0f) * fmMul; fmOk = true;
                daftDrive = 8.0f; daftMix = 0.0f;
                float fr;
                if (res <= 0.5f)
                {
                    fr = std::exp2 (-1.166f * std::pow (res, 0.795f));
                    kLad = 2.13f * res + 18.09f * res * res * res;
                    gainT = 1.517f * (1.0f + 1.5f * res);
                }
                else
                {
                    static const float rN[5] = { 0.5f, 0.6f, 0.75f, 0.9f, 1.0f };
                    static const float dN[5] = { 8.0f, 8.0f, 6.150f, 4.835f, 4.793f };
                    static const float gN[5] = { 2.655f, 2.257f, 1.767f, 1.471f, 1.381f };
                    static const float kN[5] = { 3.326f, 4.387f, 5.102f, 5.164f, 5.317f };
                    static const float fN[5] = { 0.6279f, 0.6508f, 0.6993f, 0.7281f, 0.7327f };
                    static const float mN[5] = { 0.0f, 0.071f, -0.041f, -0.126f, -0.110f };
                    int i = 0; while (i < 3 && res > rN[i + 1]) ++i;
                    const float t = (res - rN[i]) / (rN[i + 1] - rN[i]);
                    auto at = [&] (const float* v) { return v[i] + (v[i + 1] - v[i]) * t; };
                    daftDrive = at (dN); gainT = at (gN); kLad = at (kN); fr = at (fN); daftMix = at (mN);
                }
                // Hot input (round 10, three detuned saws and noise at four levels x cutoff 0.5 / 0.75 / 1 x res 0 ... 1):
                // the original squeezes a loud input at every resonance - 3 dB at res 0, 5.5 dB at res 0.45 with three saws
                // at full level - while the saturation in the feedback alone let the loop lose its grip and the level
                // climb (+11 dB, Bumper +17 dB). Now a soft input stage (drive 0.45 + 3.11 res - 3.25 res^2), below res 0.5
                // a feedback saturation that fades in with the resonance, and the gain trimmed to match: levels within
                // 1.0 dB rms over 132 cases (was 5.0).
                daftIn = std::max (0.05f, 0.454f + 3.112f * res - 3.253f * res * res);
                if (res <= 0.5f)
                {
                    gainT *= std::exp2 ((-1.64f * res + 2.743f * res * res) / 6.02f);
                    daftDrive = std::max (0.05f, 8.0f * std::pow (res / 0.5f, 1.736f));
                }
                else gainT *= std::exp2 (-0.134f * std::clamp ((0.6f - res) / 0.1f, 0.0f, 1.0f) / 6.02f);
                fmK = fr; lad.set (fmHz * fr, sr);
                for (int i = 0; i < 5; ++i) mixC[i] = 0.0f;
                mixC[2] = 1.0f - daftMix; mixC[3] = daftMix;
                break;
            }
            // Acid: the 4-pole ladder (same pole frequency as LP4) behind a one-pole high-pass at 237 Hz, with a second
            // one-pole high-pass at 107 Hz inside the feedback path - the resonance fades at low cutoffs and the tone
            // thins out there. Feedback 2.17 + 1.42 res, gain 2.47. Fitted to noise at cutoff 0.25 / 0.4 / 0.55 x res
            // 0.3 / 0.6 / 0.9: every band within 1 dB rms, the level within 0.5 dB (it was up to 7.5 dB too loud low down)
            case ftAcid:
            {
                fmHz = hzOf (cut, 1052.0f) * fmMul; fmOk = true; lad.set (fmHz, sr);
                kLad = 2.168f + 1.418f * res; gainT = 2.471f;
                hpCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 236.6f / sr);
                const float gh = std::tan (3.14159265f * 106.7f / sr); acidGh = gh / (1.0f + gh);
                break;
            }
            // measured: (high-pass + half the band-pass) at the lower edge into a 2-pole low-pass at the upper edge,
            // same edges and damping as the band-reject; the level drops with resonance (1 -> 0.41)
            case ftBandpass: { const float hz = hzOf (cut, 1045.5f) * fmMul; spread = std::exp2 (5.29f * bw);
                               const float Q = 1.0f / (2.0f * std::pow (1.0f - res, 2.08f) + 0.042f);
                               a.set (hz / spread, Q, sr); b.setWide (hz * spread, Q, sr); gainT = 0.41f + 0.59f * std::pow (1.0f - res, 2.2f);
                               gainT *= bpNarrowGain (res, bw); break; }
            // measured: a 2-pole low-pass at the lower edge plus (high-pass + half the band-pass) at the upper edge,
            // edges 5.29 octaves * bandwidth either side of the cutoff, damping 2 (1-res)^2.08 + 0.042
            case ftBandreject: { const float hz = hzOf (cut, 1045.5f) * fmMul; spread = std::exp2 (std::max (0.04f, 5.29f * bw));
                                 const float Q = 1.0f / (2.0f * std::pow (1.0f - res, 2.08f) + 0.042f);
                                 a.set (hz / spread, Q, sr); b.setWide (hz * spread, Q, sr); gainT = 0.70f; break; }
            // measured: a 2-pole low-pass with positive feedback of its own output through a first-order high-pass
            // (corner set by the Scream knob over 8.5 octaves, independent of the cutoff); the sum passes a cubic soft
            // limiter before the low-pass. Feedback gain 1.76 (1 - (1-res)^1.84): above 1 the loop sings by itself, as a
            // sine near the cutoff and as a square far below it. Fitted to pitch, level and third harmonic of that tone
            // over a bandwidth x resonance grid (0.3 semitones rms).
            case ftScream: { const float hz = hzOf (cut, 1045.5f) * (1.0f - 0.025f * res) * fmMul; a.set (hz, 0.5f + 1.137f * res, sr);
                             scrK = 1.76f * (1.0f - std::pow (1.0f - res, 1.84f));
                             scrCoef = 1.0f - std::exp (-2.0f * 3.14159265f * 8.38f * std::exp2 (8.48f * bw) / sr);
                             gainT = 1.0f - 0.68f * res + 0.30f * res * res; break; }
            // Comb: teeth at multiples of the cutoff, feedback 1 - 0.906 (1-res)^2.18 (measured from peak/trough ratios),
            // a one-pole damping in the loop at 0.706 x cutoff x 2^(12.4 bw) that is fully open from bw 0.5 up
            case ftComb: combHz = hzOf (cut, 1048.0f) * fmMul; combFb = 1.0f - 0.906f * std::pow (1.0f - res, 2.18f) + 0.004f * std::pow (res, 20.0f);
                         combDamp = bw >= 0.5f ? 1.0f : 1.0f - std::exp (-2.0f * 3.14159265f * std::min (0.45f * sr, 0.706f * combHz * std::exp2 (12.4f * bw)) / sr);
                         gainT = 1.0f; break;
            default: gainT = 1.0f; break;
        }
    }

    inline float tick (float x)
    {
        float lp, bp, hp, lp2, bp2, hp2;
        switch (type)
        {
            case ftAcid:
            {
                hpState += (x - hpState) * hpCoef; x = x - hpState;
                float y[4], u; lad.tickFbHp (x, kLad, ladDrive(), acidGh, acidHs, y, u);
                return gainT * y[3];
            }
            case ftLP4: case ftLP2: case ftHP4: case ftHP2: case ftAllpass: case ftDoubleNotch: case ftDaft:
            {
                if (type == ftDaft) x = fastTanh (x * daftIn) / daftIn;
                float y[4], u; lad.tick (x, kLad, type == ftDaft ? daftDrive : ladDrive(), y, u, type == ftDaft);
                return gainT * (mixC[0] * u + mixC[1] * y[0] + mixC[2] * y[1] + mixC[3] * y[2] + mixC[4] * y[3]);
            }
            case ftBandpass: a.tick (x, lp, bp, hp); b.tick (hp + 0.5f * a.k * bp, lp2, bp2, hp2); return gainT * lp2;
            case ftBandreject: a.tick (x, lp, bp, hp); b.tick (x, lp2, bp2, hp2); return gainT * (lp + hp2 + 0.5f * b.k * bp2);
            case ftScream:
            {
                const float c = std::clamp ((x + scrK * scrHp) * (1.0f / scrLim), -1.5f, 1.5f);
                a.tick (scrLim * (c - c * c * c * (4.0f / 27.0f)), lp, bp, hp);
                scrLp += (lp - scrLp) * scrCoef; scrHp = lp - scrLp;
                return gainT * lp;
            }
            case ftComb: return gainT * comb.tick (x, combHz, combFb, combDamp, sr);
            default: return x;
        }
    }
};
} // namespace massif
