#pragma once
//==============================================================================
// Cove Groove - the classic drum machines' voices, synthesized: three more
// sounds per lane, after the 808, 909 and 707 boxes. Nothing recorded is in
// here - every number was fitted by measuring recordings of the hardware
// (pitch curves of the kicks and toms, decay to -20 / -40 dB, the spectrum's
// octave bands and centroid) against these recipes:
//
//   808  bridged-T resonators: the kick a sine bending ~1.6x down onto its
//        note in the first 20 ms, then a long, pure ring; the snare two drum
//        tones and high-passed snappy noise; the hats and cymbal six square
//        waves at the box's inharmonic frequencies through its band-passes;
//        the cowbell two squares through a band-pass with a two-stage decay;
//        rim, clave and maracas as short rings and noise ticks.
//   909  a triangle swept from ~270 Hz down to its note for the kick (with a
//        noise-and-pulse attack), pitch-dropping tones under bright noise for
//        the snare, four noise bursts and a tail for the clap, and its 6-bit
//        sampled metals - hats, ride, crash - here metal and noise through
//        6-bit quantization, which gives them their grit.
//   707  short, punchy 6-bit hits: a quick-dropping kick, dull 6-bit hats.
//
// Tune moves the pitch (and the metals' oscillators), Decay scales the
// length, Tone the brightness / snap / attack, as on the other sounds.
//==============================================================================
#include "CgDsp.h"

namespace covegroove
{
    constexpr int kMachineModels = 3;   // per lane, after the lane's Sample model

    inline const char* machineName (int lane, int which)
    {
        static const char* n[8][kMachineModels] = {
            { "Retro 808 Kick", "Retro 909 Kick", "707 Kick" },
            { "Retro 808 Boom", "Retro 808 Low Tom", "Retro 909 Low Tom" },
            { "Retro 808 Snare", "Retro 909 Snare", "Retro 909 Clap" },
            { "Retro 808 Hat", "Retro 909 Hat", "707 Hat" },
            { "Retro 808 Open", "Retro 909 Open", "707 Open" },
            { "Retro 808 Cymbal", "Retro 909 Ride", "Retro 909 Crash" },
            { "Retro 808 Cowbell", "Retro 808 Rim", "Retro 909 Tom" },
            { "Retro 808 Clave", "Retro 808 Maracas", "707 Tamb" } };
        return n[std::clamp (lane, 0, 7)][std::clamp (which, 0, kMachineModels - 1)];
    }

    class MachineVoice
    {
    public:
        void start (int laneType, int which, float sampleRate, float tuneRatio, float decayScale, float toneAmt, bool alt, uint32_t seed)
        {
            lane = laneType; kind = std::clamp (which, 0, kMachineModels - 1);
            sr = sampleRate; tr = tuneRatio; dec = decayScale; tone = toneAmt; accent = alt;
            rng = Rng (seed * 2654435761u + 7u);
            n = 0;
            for (auto& p : ph) p = 0.0f;
            for (auto& o : sq) o.phase = 0.0f;
            for (auto& f : flt) f.reset();
            hold = 0.0f;
            maxLen = 4.0f;
            setup();
        }

        float length() const { return maxLen; }

        // one sample; mono drums come out on both sides, the metals slightly spread
        void render (float& l, float& r)
        {
            const float t = (float) n / sr;
            float y = 0.0f, side = 0.0f;
            switch (lane)
            {
                case 0: y = kick (t); break;
                case 1: y = bass (t); break;
                case 2: y = snare (t); break;
                case 3: case 4: y = hat (t, side); break;
                case 5: y = cymbal (t, side); break;
                case 6: y = perc (t); break;
                default: y = fx (t, side); break;
            }
            ++n;
            // levels matched to the lane's other sounds (RMS over the first quarter second)
            static const float kLevel[8][kMachineModels] = {
                { 1.0f, 1.0f, 1.6f }, { 1.0f, 1.8f, 1.4f }, { 1.0f, 1.25f, 2.5f }, { 7.0f, 3.2f, 2.7f },
                { 5.5f, 4.0f, 3.5f }, { 5.0f, 1.1f, 1.4f }, { 1.7f, 3.5f, 1.4f }, { 1.0f, 5.6f, 3.2f } };
            const float g = kLevel[std::clamp (lane, 0, 7)][kind];
            l = (y + side) * g;
            r = (y - side) * g;
        }

    private:
        //=== building blocks ===================================================
        float sine (int i, float hz)
        {
            ph[i] += hz / sr;
            if (ph[i] >= 1.0f) ph[i] -= 1.0f;
            return std::sin (kTwoPi * ph[i]);
        }
        float tri (int i, float hz)
        {
            ph[i] += hz / sr;
            if (ph[i] >= 1.0f) ph[i] -= 1.0f;
            return 1.0f - 4.0f * std::abs (ph[i] - 0.5f);
        }
        static float ex (float t, float tau) { return std::exp (-t / std::max (1.0e-4f, tau)); }
        // the 6-bit boxes: their sampled sounds' grit, quantized to 64 steps around the hit's own level
        static float bits6 (float x, float level)
        {
            const float q = std::max (1.0e-4f, level) / 32.0f;
            return std::round (x / q) * q;
        }
        float metal808 (float scale)
        {
            // the analog box's six squares (its famous inharmonic set)
            static const float f[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            float s = 0.0f;
            for (int i = 0; i < 6; ++i)
                s += sq[(size_t) i].square (f[i] * scale / sr);
            return s * (1.0f / 6.0f);
        }
        float metal909 (float scale)
        {
            // a brighter, denser set for the sampled box's metals
            static const float f[6] = { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f };
            float s = 0.0f;
            for (int i = 0; i < 6; ++i)
                s += sq[(size_t) i].square (f[i] * 1.6f * scale / sr);
            return s * (1.0f / 6.0f);
        }

        //=== per-sound setup ===================================================
        void setup()
        {
            switch (lane)
            {
                case 0:
                    flt[0].set (sr, kind == 1 ? 3200.0f + 2500.0f * tone : 900.0f, kind == 1 ? 0.6f : 0.8f);   // the attack's noise band
                    maxLen = kind == 0 ? 2.5f * dec + 0.3f : (kind == 1 ? 0.9f * dec + 0.2f : 0.3f * dec + 0.05f);
                    break;
                case 1: maxLen = 3.5f * dec + 0.3f; break;
                case 2:
                    flt[0].set (sr, 1200.0f + 1500.0f * tone, 0.7f);                    // the 808's snappy noise (high-pass) ...
                    flt[1].set (sr, kind == 1 ? 7500.0f + 4000.0f * tone : 1100.0f * (0.8f + 0.5f * tone) * tr, kind == 1 ? 0.6f : 1.6f);
                    flt[2].set (sr, kind == 1 ? 900.0f : (kind == 0 ? 7000.0f : 1500.0f), kind == 1 ? 0.7f : 0.8f);   // ... and its top
                    maxLen = 1.2f * dec + 0.1f;
                    break;
                case 3: case 4:
                {
                    const bool open = lane == 4;
                    if (kind == 0)
                    {
                        flt[0].set (sr, (open ? 7100.0f : 8800.0f) * (0.9f + 0.3f * tone), open ? 1.3f : 1.0f);
                        flt[1].set (sr, open ? 6200.0f : 9000.0f * (0.85f + 0.3f * tone), 0.7f);
                    }
                    else
                    {
                        flt[0].set (sr, kind == 1 ? 9000.0f : 7000.0f, 0.8f);
                        flt[1].set (sr, kind == 1 ? 900.0f + 900.0f * tone : 700.0f + 700.0f * tone, 0.6f);
                        flt[2].set (sr, kind == 1 ? 11500.0f : 8500.0f, kind == 1 ? 0.7f : 1.1f);
                    }
                    maxLen = open ? 2.0f * dec + 0.2f : 0.5f * dec + 0.05f;
                    break;
                }
                case 5:
                    if (kind == 0) { flt[0].set (sr, 3440.0f, 1.2f); flt[1].set (sr, 7100.0f, 1.0f); flt[2].set (sr, 5600.0f + 2000.0f * tone, 0.7f); }
                    else if (kind == 1) { flt[0].set (sr, 500.0f + 800.0f * tone, 0.6f); flt[1].set (sr, 6500.0f, 0.9f); flt[2].set (sr, 13000.0f, 0.7f); }
                    else { flt[0].set (sr, 1500.0f * (0.8f + 0.5f * tone), 0.3f); flt[1].set (sr, 200.0f, 0.6f); flt[2].set (sr, 10000.0f, 0.7f); }
                    maxLen = 6.0f * dec + 0.3f;
                    break;
                case 6:
                    if (kind == 0) { flt[0].set (sr, 820.0f * tr * (0.85f + 0.35f * tone), 1.6f); flt[1].set (sr, 300.0f, 0.7f); }
                    else if (kind == 1) { flt[0].set (sr, 480.0f * tr, 7.0f); flt[1].set (sr, 1700.0f * tr, 6.0f); }
                    else { flt[0].set (sr, 2500.0f, 0.8f); }
                    maxLen = kind == 0 ? 1.5f * dec + 0.1f : (kind == 1 ? 0.15f : 1.2f * dec + 0.1f);
                    break;
                default:
                    if (kind == 0) flt[0].set (sr, 2500.0f * tr, 55.0f + 30.0f * tone);
                    else if (kind == 1) { flt[0].set (sr, 4500.0f + 2500.0f * tone, 0.8f); flt[1].set (sr, 12500.0f, 0.7f); }
                    else { flt[0].set (sr, 6500.0f * (0.85f + 0.3f * tone), 1.6f); flt[1].set (sr, 3600.0f, 0.7f); flt[2].set (sr, 9500.0f, 0.9f); }
                    maxLen = kind == 2 ? 0.8f * dec + 0.05f : 0.25f;
                    break;
            }
        }

        //=== Kick ============================================================
        float kick (float t)
        {
            if (kind == 0)
            {
                // 808: a sine bending ~1.6x down onto ~53 Hz in its first 20 ms, then a pure ring
                const float f = 53.0f * tr * (1.0f + 0.65f * ex (t, 0.012f));
                const float body = sine (0, f);
                const float a = ex (t, 0.17f * dec);
                // Tone adds the attack: a short click at the blow
                const float click = (0.15f + 0.85f * tone) * ex (t, 0.0012f) * (rng.next() * 0.3f + 1.0f) * (t < 0.004f ? 1.0f : 0.0f);
                return 0.95f * std::tanh (1.25f * body * a) / std::tanh (1.25f) + 0.25f * click;
            }
            if (kind == 1)
            {
                // 909: a triangle swept from ~270 Hz down to ~55 Hz (tau 28 ms), shaped toward a sine
                const float f = tr * (55.0f + 215.0f * ex (t, 0.028f));
                const float tw = tri (0, f);
                const float body = 1.5f * tw - 0.5f * tw * tw * tw;
                const float a = t < 0.012f ? 1.0f : ex (t - 0.012f, 0.055f * dec);
                // the attack: a pulse and a burst of low-passed noise
                const float nz = flt[0].process (rng.next());
                const float atk = (0.3f + 0.7f * tone) * (t < 0.0025f ? 0.5f : 0.0f) + (0.4f + 0.8f * tone) * ex (t, 0.004f) * nz * flt[0].k;
                return 0.9f * std::tanh (1.4f * body * a) / std::tanh (1.4f) + atk;
            }
            // 707: a short, quick-dropping thump, 6-bit, gated
            const float f = 80.0f * tr * (1.0f + 0.75f * ex (t, 0.008f));
            const float body = sine (0, f) + 0.15f * sine (1, 2.0f * f);
            const float gateT = 0.1f * dec;
            const float a = ex (t, 0.05f * std::sqrt (dec)) * (t < gateT ? 1.0f : ex (t - gateT, 0.005f));
            const float click = (0.25f + 0.6f * tone) * ex (t, 0.004f) * flt[0].process (rng.next()) * flt[0].k;
            const float y = 0.9f * body * a + click;
            return std::round (y * 128.0f) / 128.0f;   // its 8-bit-ish grit
        }

        //=== Bass (the offbeat sub) ============================================
        float bass (float t)
        {
            if (kind == 0)
            {
                // 808 boom: the kick tuned down to a bass note, long and round, a touch of 2nd harmonic
                const float f = 55.0f * tr * (1.0f + 0.25f * ex (t, 0.015f));
                const float s = sine (0, f);
                const float a = ex (t, 0.45f * dec);
                return 0.85f * a * (s + 0.12f * (1.0f + tone) * s * s - 0.06f);
            }
            if (kind == 1)
            {
                // 808 low tom: a steady ~81 Hz ring with a little noise on the blow
                const float f = 81.0f * tr * (accent ? 1.5f : 1.0f);
                const float a = ex (t, 0.075f * dec);
                return 0.8f * (sine (0, f) * a + 0.15f * tone * ex (t, 0.004f) * rng.next());
            }
            // 909 low tom: a triangle falling from twice its note, noise in the attack
            const float f = 82.0f * tr * (accent ? 1.5f : 1.0f) * (1.0f + 1.0f * ex (t, 0.03f));
            const float tw = tri (0, f);
            const float a = ex (t, 0.12f * dec);
            return 0.85f * (1.4f * tw - 0.4f * tw * tw * tw) * a + 0.12f * (0.5f + tone) * ex (t, 0.012f) * rng.next();
        }

        //=== Snare / clap ======================================================
        float snare (float t)
        {
            if (kind == 0)
            {
                // 808: tones at ~172 and ~330 Hz, and snappy high-passed noise
                const float body = sine (0, 172.0f * tr) * ex (t, 0.03f * dec) + 0.14f * sine (1, 330.0f * tr) * ex (t, 0.02f * dec);
                flt[0].process (rng.next());
                flt[2].process (flt[0].hp);
                const float snap = flt[2].lp * (0.4f + 0.6f * tone) * ex (t, 0.026f * dec * (0.7f + 0.6f * tone));
                return 0.85f * body + 1.15f * snap;
            }
            if (kind == 1)
            {
                // 909: two triangles dropping from ~195 / 340 Hz, under bright noise
                const float f1 = tr * (122.0f + 75.0f * ex (t, 0.012f)), f2 = tr * (228.0f + 115.0f * ex (t, 0.012f));
                const float body = (0.75f * tri (0, f1) + 0.3f * tri (1, f2)) * ex (t, 0.032f * dec);
                const float nz = rng.next();
                flt[1].process (nz);                    // low-pass for the top
                flt[2].process (flt[1].lp);             // then high-pass
                const float noise = flt[2].hp * (0.7f + 0.6f * tone) * ex (t, 0.042f * dec * (0.6f + 0.8f * tone));
                return 0.75f * body + 0.95f * noise;
            }
            // 909 clap: four bursts of band-passed noise ~10 ms apart, then a short tail
            const float gap = 0.0095f;
            float e = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float tk = t - (float) k * gap;
                if (tk >= 0.0f && tk < gap) e = std::max (e, ex (tk, 0.0035f));
            }
            const float tl = t - 3.0f * gap;
            if (tl >= 0.0f) e = std::max (e, ex (tl, 0.02f * dec));
            const float nz = rng.next();
            const float band = flt[1].process (nz) * flt[1].k;
            const float tail = tl >= 0.0f ? flt[2].process (nz) * flt[2].k * 0.22f * ex (tl, 0.05f * dec) : 0.0f;
            return 1.6f * (band * e + tail);
        }

        //=== Hats ==============================================================
        float hat (float t, float& side)
        {
            const bool open = lane == 4;
            if (kind == 0)
            {
                // 808: the six squares through the 7.1 kHz band-pass, then a high-pass
                const float m = metal808 (tr);
                const float bp = flt[0].process (m) * flt[0].k;
                flt[1].process (bp);
                const float y = flt[1].hp;
                const float a = open ? ex (t, 0.14f * dec) : ex (t, 0.019f * dec);
                side = 0.05f * y * a;
                return 1.7f * y * a;
            }
            // the 6-bit boxes: metal and noise, quantized at the sample's level, then the VCA
            const float m = metal909 (tr) * (kind == 1 ? 0.8f : 0.6f) + rng.next() * (kind == 1 ? 0.55f : 0.45f);
            flt[1].process (m);
            float y = flt[1].hp;
            const float sampleLevel = ex (t, open ? 0.25f : 0.05f);
            y = bits6 (y * sampleLevel, sampleLevel) ;
            y = flt[2].process (y), y = flt[2].lp;                 // the box's top end
            float a;
            if (kind == 1) a = open ? ex (t, 0.055f * dec) : 0.55f * ex (t, 0.006f) + 0.45f * ex (t, 0.045f * dec);
            else { a = open ? ex (t, 0.085f * dec) : ex (t, 0.03f * dec); const float g = open ? 0.3f * dec : 0.1f * dec; if (t > g) a *= ex (t - g, 0.006f); }
            side = 0.08f * y * a;
            return 0.75f * y * a;
        }

        //=== Cymbals ===========================================================
        float cymbal (float t, float& side)
        {
            if (kind == 0)
            {
                // 808: the six squares through its two band-passes, the high part fading first
                const float m = metal808 (tr);
                const float lo = flt[0].process (m) * flt[0].k, hi = flt[1].process (m) * flt[1].k;
                flt[2].process (lo * ex (t, 0.5f * dec) * 0.6f + hi * ex (t, 0.3f * dec));
                const float y = flt[2].hp;
                side = 0.12f * y;
                return 1.2f * y;
            }
            if (kind == 1)
            {
                // 909 ride: bell partials, metal and a little noise, 6-bit, a long shimmer
                static const float p[6] = { 2780.0f, 3410.0f, 4010.0f, 5230.0f, 6120.0f, 7340.0f };
                float bell = 0.0f;
                for (int i = 0; i < 6; ++i) bell += sine (i, p[i] * tr);
                const float m = 0.5f * metal909 (tr * 1.3f) + 0.18f * bell + 0.25f * rng.next();
                flt[0].process (m);
                const float lvl = ex (t, 0.6f);
                float y = bits6 (flt[0].hp * lvl, lvl);
                y = flt[2].process (y), y = flt[2].lp;
                const float a = ex (t, 0.38f * dec);
                side = 0.1f * y * a;
                return 0.8f * y * a;
            }
            // 909 crash: a wide band of noise and metal around 3 kHz, 6-bit, swelling in
            const float m = 0.6f * rng.next() + 0.5f * metal909 (tr * 0.9f);
            const float b = flt[0].process (m) * flt[0].k;
            flt[1].process (b);
            const float lvl = ex (t, 0.8f);
            float y = bits6 (flt[1].hp * lvl, lvl);
            y = flt[2].process (y), y = flt[2].lp;
            const float a = (1.0f - ex (t, 0.004f)) * ex (t, 0.42f * dec);
            side = 0.15f * y * a;
            return 0.9f * y * a;
        }

        //=== Percussion ========================================================
        float perc (float t)
        {
            if (kind == 0)
            {
                // 808 cowbell: two squares at 540 / 800 Hz through a band-pass, decaying in two stages
                const float m = 0.5f * (sq[0].square (540.0f * tr / sr) + sq[1].square (800.0f * tr / sr));
                const float bp = flt[0].process (m) * flt[0].k;
                flt[1].process (bp);
                const float a = 0.72f * ex (t, 0.015f) + 0.28f * ex (t, 0.11f * dec);
                return 1.1f * flt[1].hp * a;
            }
            if (kind == 1)
            {
                // 808 rim: two high resonances rung by a click, gone in ~10 ms
                const float x = (n == 0 ? 1.0f : 0.0f) * 9.0f + (t < 0.0012f ? rng.next() * 1.2f : 0.0f);
                const float y = flt[0].process (x) * flt[0].k + 1.1f * flt[1].process (x) * flt[1].k + (t < 0.0012f ? 0.3f * x : 0.0f);
                return 0.9f * std::tanh (2.0f * y) * ex (t, 0.012f + 0.01f * tone);
            }
            // 909 mid tom: a triangle falling from twice its ~130 Hz note
            const float f = 130.0f * tr * (accent ? 1.35f : 1.0f) * (1.0f + 0.9f * ex (t, 0.025f));
            const float tw = tri (0, f);
            flt[0].process (rng.next());
            return 0.85f * (1.4f * tw - 0.4f * tw * tw * tw) * ex (t, 0.09f * dec) + 0.15f * (0.4f + tone) * flt[0].bp * ex (t, 0.015f);
        }

        //=== FX lane: small percussion =========================================
        float fx (float t, float& side)
        {
            if (kind == 0)
            {
                // 808 clave: a ring at ~2.5 kHz, ~10 ms
                const float x = n == 0 ? 1.0f : 0.0f;
                flt[0].process (x);
                return 2.0f * flt[0].bp;     // the ring itself (a high Q: its band-pass rings ~10 ms)
            }
            if (kind == 1)
            {
                // 808 maracas: a tick of high noise
                flt[0].process (rng.next());
                flt[1].process (flt[0].hp);
                const float y = flt[1].lp * (1.0f - ex (t, 0.0012f)) * ex (t, 0.0035f * std::sqrt (dec));
                side = 0.1f * y;
                return 0.9f * y;
            }
            // 707 tambourine: jingles in a few quick bursts, then the tail, 6-bit
            float e = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float tk = t - (float) k * 0.011f;
                if (tk >= 0.0f) e = std::max (e, (k == 2 ? 1.0f : 0.7f) * ex (tk, k == 2 ? 0.045f * dec : 0.004f));
            }
            const float m = rng.next() * 0.6f + 0.4f * metal909 (tr * 2.0f);
            const float b = flt[0].process (m) * flt[0].k;
            flt[1].process (b);
            float y = bits6 (flt[1].hp * e, e);
            flt[2].process (y);
            y = flt[2].lp;
            side = 0.1f * y;
            return 1.2f * y;
        }

        int lane { 0 }, kind { 0 };
        float sr { 48000.0f }, tr { 1.0f }, dec { 1.0f }, tone { 0.5f };
        bool accent { false };
        Rng rng;
        uint32_t n { 0 };
        float ph[8] {};
        std::array<BlepOsc, 6> sq {};
        std::array<Svf, 3> flt {};
        float hold { 0.0f };
        float maxLen { 4.0f };
    };
}
