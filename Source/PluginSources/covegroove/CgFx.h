#pragma once
//==============================================================================
// Cove Groove - the bus effects: the top bus's subtle phaser and moving
// band-pass, the tempo delay, and (DriveCore.h) the master drive.
//==============================================================================
#include "CgDsp.h"
#include <vector>

namespace covegroove
{
    //=== Phaser: six first-order all-passes per side, swept by a sine ========
    struct Phaser
    {
        float amount { 0.3f };      // 0..1: how much of the phased sound (and how deep)
        float rateHz { 0.25f };
        float centreHz { 900.0f };

        void prepare (float sampleRate) { sr = sampleRate; reset(); }
        void reset() { for (auto& s : z) s.fill (0.0f); fb[0] = fb[1] = 0.0f; lfo = 0.0f; }

        void process (float& l, float& r)
        {
            if (amount <= 0.0001f) return;
            lfo += rateHz / sr;
            if (lfo >= 1.0f) lfo -= 1.0f;
            float* io[2] = { &l, &r };
            for (int c = 0; c < 2; ++c)
            {
                const float ph = lfo + (c == 0 ? 0.0f : 0.25f);
                const float sweep = std::sin (kTwoPi * ph) * (0.6f + 1.2f * amount);   // octaves
                const float f = clampf (centreHz * std::pow (2.0f, sweep), 60.0f, sr * 0.45f);
                const float t = std::tan (kPi * f / sr);
                const float a = (t - 1.0f) / (t + 1.0f);
                float x = *io[c] + fb[c] * 0.35f;
                for (int st = 0; st < kStages; ++st)
                {
                    const float y = a * x + z[c][(size_t) st];
                    z[c][(size_t) st] = x - a * y;
                    x = y;
                }
                fb[c] = x;
                const float wet = 0.5f * amount;
                *io[c] = *io[c] * (1.0f - wet) + x * wet;
            }
        }

    private:
        static constexpr int kStages = 6;
        float sr { 48000.0f };
        std::array<float, kStages> z[2] {};
        float fb[2] {};
        float lfo { 0.0f };
    };

    //=== the moving band-pass ================================================
    // A band-pass around Freq; Motion sweeps it (octaves either way) once
    // every Rate bars, as a sine, a ramp, or a smooth random walk to a new
    // place each cycle. Mix blends it with the straight sound.
    struct MotionFilter
    {
        float freqHz { 1800.0f }, reso { 0.3f }, motion { 0.0f }, mix { 0.0f };
        int shape { 0 };            // 0 sine, 1 ramp, 2 random

        void prepare (float sampleRate) { sr = sampleRate; reset(); }
        void reset() { f[0].reset(); f[1].reset(); from = to = 0.0f; }

        // pos: where the sweep is, in cycles (the engine counts it in bars)
        void process (float& l, float& r, double cyclePos)
        {
            if (mix <= 0.0001f) { lastCycle = (int64_t) std::floor (cyclePos); return; }
            const double fl = std::floor (cyclePos);
            const float ph = (float) (cyclePos - fl);
            const int64_t cyc = (int64_t) fl;
            if (cyc != lastCycle)
            {
                lastCycle = cyc;
                from = to;
                to = rng.next();
            }
            float m = 0.0f;
            switch (shape)
            {
                case 1:  m = 2.0f * ph - 1.0f; break;
                case 2:  { const float e = 0.5f - 0.5f * std::cos (kPi * ph); m = from + (to - from) * e; break; }
                default: m = std::sin (kTwoPi * ph); break;
            }
            if (++ctl >= 16)
            {
                ctl = 0;
                const float hz = clampf (freqHz * std::pow (2.0f, m * motion * 3.0f), 40.0f, sr * 0.45f);
                const float q = 0.6f + reso * reso * 9.0f;
                f[0].set (sr, hz, q);
                f[1].set (sr, hz, q);
            }
            f[0].process (l); f[1].process (r);
            const float bl = f[0].bp * f[0].k, br = f[1].bp * f[1].k;   // unity at the centre
            const float makeup = 1.0f + 0.6f * reso;
            l += (bl * makeup - l) * mix;
            r += (br * makeup - r) * mix;
        }

    private:
        float sr { 48000.0f };
        Svf f[2];
        Rng rng { 0xC0FFEEu };
        float from { 0.0f }, to { 0.0f };
        int64_t lastCycle { -1 };
        int ctl { 16 };
    };

    //=== the tempo delay =====================================================
    // Time in 16th steps (dotted 1/8 = 3 by default), the feedback through a
    // high-pass at 250 Hz and a low-pass at Tone, ping-pong if wanted.
    struct TempoDelay
    {
        float steps { 3.0f }, feedback { 0.45f }, toneHz { 5000.0f }, mix { 0.25f };
        bool pingPong { true };

        void prepare (float sampleRate)
        {
            sr = sampleRate;
            buf[0].assign ((size_t) (sr * 4.0f) + 4, 0.0f);
            buf[1].assign ((size_t) (sr * 4.0f) + 4, 0.0f);
            hp[0].setLowpass (sr, 250.0f); hp[1].setLowpass (sr, 250.0f);
            reset();
        }
        void reset()
        {
            for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
            pos = 0; smoothed = -1.0f;
            hp[0].reset(); hp[1].reset(); lp[0].reset(); lp[1].reset();
        }

        // in: what the lanes send; returns the echoes (wet only, at mix)
        void process (float inL, float inR, float& outL, float& outR, double bpm)
        {
            const size_t len = buf[0].size();
            const float target = clampf ((float) (steps * 15.0 / std::max (20.0, bpm)) * sr, 16.0f, (float) len - 4.0f);
            smoothed = smoothed < 0.0f ? target : smoothed + 0.0005f * (target - smoothed);
            lp[0].setLowpass (sr, toneHz); lp[1].setLowpass (sr, toneHz);

            float rp = (float) pos - smoothed;
            while (rp < 0.0f) rp += (float) len;
            const size_t i0 = (size_t) rp;
            const size_t i1 = (i0 + 1) % len;
            const float fr = rp - (float) i0;
            const float dl = buf[0][i0] + fr * (buf[0][i1] - buf[0][i0]);
            const float dr = buf[1][i0] + fr * (buf[1][i1] - buf[1][i0]);

            const float fbL = lp[0].lowpass (hp[0].highpass (dl)) * feedback;
            const float fbR = lp[1].lowpass (hp[1].highpass (dr)) * feedback;
            if (pingPong)
            {
                buf[0][pos] = 0.5f * (inL + inR) + fbR;      // the input enters left, the repeats cross
                buf[1][pos] = fbL;
            }
            else
            {
                buf[0][pos] = inL + fbL;
                buf[1][pos] = inR + fbR;
            }
            if (++pos >= len) pos = 0;
            outL = dl * mix;
            outR = dr * mix;
        }

    private:
        float sr { 48000.0f };
        std::vector<float> buf[2];
        size_t pos { 0 };
        float smoothed { -1.0f };
        OnePole hp[2], lp[2];
    };
}
