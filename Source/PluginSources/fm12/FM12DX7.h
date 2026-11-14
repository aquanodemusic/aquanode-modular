#pragma once
// FM12DX7.h - DX7 voices as FM12 sounds.
//
// A DX7 voice (the 155-byte VCED single-voice record) becomes six of FM12's
// twelve operators in DX7 Mode: the algorithm as matrix routes, frequency
// ratios (with the voice's transpose folded in), output levels as the DX7's
// modulation depths and carrier levels, its four-rate / four-level envelopes
// as the closest Attack / Decay / Sustain / Release, and the feedback on the
// operator the algorithm feeds back into. Two voices can share all twelve
// operators (a layer). The voice's LFO (speed, delay, wave, key sync, pitch
// depth x sensitivity, amplitude depth x the operators' sensitivities) goes to
// FM12's LFO, and its pitch envelope, where it is a sweep from or a blip to a
// pitch and back, to FM12's pitch envelope (start and time). Keyboard level /
// rate scaling and velocity sensitivity are taken at middle C, velocity 100.
//
// The timing and level maths follow the DX7's own: 0.75 dB per level step,
// envelope rates as level steps per sample block at 44.1 kHz, the attack's
// jump to about -50 dB and its curve, and a modulation depth of two cycles
// (4 pi) for an operator at full level.

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>
#include "PluginProcessor.h"

namespace fm12dx7
{
    using Vced = std::array<uint8_t, 155>;

    struct Voice
    {
        Vced vced {};
        juce::String name;
    };

    //==========================================================================
    // the app hands its built-in DX7 voices in through this (Aquanode Modular
    // sets it; on its own FM12 has only .syx import)
    struct VoiceSource
    {
        std::function<juce::StringArray()> categories;
        std::function<std::vector<Voice> (int category)> voices;
    };
    inline VoiceSource& voiceSource() { static VoiceSource s; return s; }

    //==========================================================================
    // the 32 algorithms: per destination operator (0 = OP1) the operators that
    // modulate it, the carriers, and which operator feeds back into which
    struct Algo { uint8_t modMask[6]; uint8_t carrierMask; uint8_t fbSrc, fbDst; uint8_t numCarriers; };
    inline const Algo& algorithm (int i)
    {
        static const Algo a[32] = {
            { { 0x02, 0x00, 0x08, 0x10, 0x20, 0x00 }, 0x05, 5, 5, 2 }, { { 0x02, 0x00, 0x08, 0x10, 0x20, 0x00 }, 0x05, 1, 1, 2 },
            { { 0x02, 0x04, 0x00, 0x10, 0x20, 0x00 }, 0x09, 5, 5, 2 }, { { 0x02, 0x04, 0x00, 0x10, 0x20, 0x00 }, 0x09, 3, 5, 2 },
            { { 0x02, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x15, 5, 5, 3 }, { { 0x02, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x15, 4, 5, 3 },
            { { 0x02, 0x00, 0x18, 0x00, 0x20, 0x00 }, 0x05, 5, 5, 2 }, { { 0x02, 0x00, 0x18, 0x00, 0x20, 0x00 }, 0x05, 3, 3, 2 },
            { { 0x02, 0x00, 0x18, 0x00, 0x20, 0x00 }, 0x05, 1, 1, 2 }, { { 0x02, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x09, 2, 2, 2 },
            { { 0x02, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x09, 5, 5, 2 }, { { 0x02, 0x00, 0x38, 0x00, 0x00, 0x00 }, 0x05, 1, 1, 2 },
            { { 0x02, 0x00, 0x38, 0x00, 0x00, 0x00 }, 0x05, 5, 5, 2 }, { { 0x02, 0x00, 0x08, 0x30, 0x00, 0x00 }, 0x05, 5, 5, 2 },
            { { 0x02, 0x00, 0x08, 0x30, 0x00, 0x00 }, 0x05, 1, 1, 2 }, { { 0x16, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x01, 5, 5, 1 },
            { { 0x16, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x01, 1, 1, 1 }, { { 0x0E, 0x00, 0x00, 0x10, 0x20, 0x00 }, 0x01, 2, 2, 1 },
            { { 0x02, 0x04, 0x00, 0x20, 0x20, 0x00 }, 0x19, 5, 5, 3 }, { { 0x04, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x0B, 2, 2, 3 },
            { { 0x04, 0x04, 0x00, 0x20, 0x20, 0x00 }, 0x1B, 2, 2, 4 }, { { 0x02, 0x00, 0x20, 0x20, 0x20, 0x00 }, 0x1D, 5, 5, 4 },
            { { 0x00, 0x04, 0x00, 0x20, 0x20, 0x00 }, 0x1B, 5, 5, 4 }, { { 0x00, 0x00, 0x20, 0x20, 0x20, 0x00 }, 0x1F, 5, 5, 5 },
            { { 0x00, 0x00, 0x00, 0x20, 0x20, 0x00 }, 0x1F, 5, 5, 5 }, { { 0x00, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x0B, 5, 5, 3 },
            { { 0x00, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x0B, 2, 2, 3 }, { { 0x02, 0x00, 0x08, 0x10, 0x00, 0x00 }, 0x25, 4, 4, 3 },
            { { 0x00, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x17, 5, 5, 4 }, { { 0x00, 0x00, 0x08, 0x10, 0x00, 0x00 }, 0x27, 4, 4, 4 },
            { { 0x00, 0x00, 0x00, 0x00, 0x20, 0x00 }, 0x1F, 5, 5, 5 }, { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3F, 5, 5, 6 },
        };
        return a[juce::jlimit (0, 31, i)];
    }

    //==========================================================================
    // DX7 maths, the same curves Aquanode Modular's native DX7 engine (in
    // VirtualDX7) was fitted to the original with: levels in 0.75 dB units,
    // envelope rates doubling every 6.25 steps, the attack's jump and boost,
    // keyboard level and rate scaling, velocity sensitivity, and a modulation
    // depth of 2.07 cycles for an operator at full level.
    constexpr float kModCycles = 2.07f;
    constexpr float kRefNote = 60.0f;     // levels and rates are taken at middle C ...
    constexpr int   kRefVelocity = 100;   // ... and a firm velocity

    inline int scaleOutLevel (int l)
    {
        static const int lut[20] = { 0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46 };
        l = juce::jlimit (0, 99, l);
        return l >= 20 ? l + 28 : lut[l];
    }
    inline float unitsToDb (int units) { return ((float) units - 127.0f) * 0.75f; }
    inline float levelDb (int l) { return unitsToDb (scaleOutLevel (l)); }
    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
    inline float rateDbPerSecond (float rate)
    {
        return 16600.0f * std::exp2 ((juce::jlimit (0.0f, 99.0f, rate) - 99.0f) / 6.25f);
    }

    // the LFO and pitch envelope tables, as Aquanode Modular's native DX7
    // engine has them (measured from the hardware)
    inline float lfoSpeedHz (int speed)
    {
        static const float tab[100] = {
             0.062541f,  0.125031f,  0.312393f,  0.437120f,  0.624610f,  0.750694f,  0.936330f,  1.125302f,  1.249609f,  1.436782f,
             1.560915f,  1.752081f,  1.875117f,  2.062494f,  2.247191f,  2.374451f,  2.560492f,  2.686728f,  2.873976f,  2.998950f,
             3.188013f,  3.369840f,  3.500175f,  3.682224f,  3.812065f,  4.000800f,  4.186202f,  4.310716f,  4.501260f,  4.623209f,
             4.814636f,  4.930480f,  5.121901f,  5.315191f,  5.434783f,  5.617346f,  5.750431f,  5.946717f,  6.062811f,  6.248438f,
             6.431695f,  6.564264f,  6.749460f,  6.868132f,  7.052186f,  7.250580f,  7.375719f,  7.556294f,  7.687577f,  7.877738f,
             7.993605f,  8.181967f,  8.372405f,  8.504848f,  8.685079f,  8.810573f,  8.986341f,  9.122423f,  9.300595f,  9.500285f,
             9.607994f,  9.798158f,  9.950249f, 10.117361f, 11.251125f, 11.384335f, 12.562814f, 13.676149f, 13.904338f, 15.092062f,
            16.366612f, 16.638935f, 17.869907f, 19.193858f, 19.425019f, 20.833333f, 21.034918f, 22.502250f, 24.003841f, 24.260068f,
            25.746653f, 27.173913f, 27.578599f, 29.052876f, 30.693677f, 31.191516f, 32.658393f, 34.317090f, 34.674064f, 36.416606f,
            38.197097f, 38.550501f, 40.387722f, 40.749796f, 42.625746f, 44.326241f, 44.883303f, 46.772685f, 48.590865f, 49.261084f };
        return tab[juce::jlimit (0, 99, speed)];
    }
    inline float lfoHoldSeconds (int delay) { const float t = (float) juce::jlimit (0, 99, delay) / 99.0f; return 6.0f * t * t; }
    inline float pmsSemitones (int pms)
    {
        static const float tab[8] = { 0.0f, 0.27f, 0.55f, 0.90f, 1.5f, 2.5f, 4.2f, 7.0f };
        return tab[juce::jlimit (0, 7, pms)];
    }
    inline float amsDb (int ams) { static const float tab[4] = { 0.0f, 11.0f, 22.0f, 44.0f }; return tab[juce::jlimit (0, 3, ams)]; }
    inline float pitchEgSemitones (int level)
    {
        static const int8_t tab[100] = {
            -128, -116, -104,  -95,  -85,  -76,  -68,  -61,  -56,  -52,  -49,  -46,  -43,  -41,  -39,  -37,  -35,  -33,  -32,  -31,
             -30,  -29,  -28,  -27,  -26,  -25,  -24,  -23,  -22,  -21,  -20,  -19,  -18,  -17,  -16,  -15,  -14,  -13,  -12,  -11,
             -10,   -9,   -8,   -7,   -6,   -5,   -4,   -3,   -2,   -1,    0,    1,    2,    3,    4,    5,    6,    7,    8,    9,
              10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,
              30,   31,   32,   33,   34,   35,   38,   40,   43,   46,   49,   53,   58,   65,   73,   82,   92,  103,  115,  127 };
        return (float) tab[juce::jlimit (0, 99, level)] * 0.375f;
    }
    inline float pitchEgRate (int rate)       // semitones per second
    {
        static const uint8_t tab[100] = {
              1,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   9,   9,  10,  10,  11,  11,
             12,  12,  13,  13,  14,  14,  15,  16,  16,  17,  18,  18,  19,  20,  21,  22,  23,  24,  25,  26,
             27,  28,  30,  31,  33,  34,  36,  37,  38,  39,  41,  42,  44,  46,  47,  49,  51,  53,  54,  56,
             58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  79,  82,  85,  88,  91,  94,  98, 102, 106, 110,
            115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255 };
        return (float) tab[juce::jlimit (0, 99, rate)] * 0.5634f;
    }

    inline float rateScaleOffset (int rs, int note)
    {
        if (rs <= 0) return 0.0f;
        const int x = juce::jlimit (0, 31, note / 3 - 7);
        return (float) ((rs * x) >> 3) * 1.88f;
    }
    inline int scaleCurveUnits (int group, int depth, int curve)
    {
        static const uint8_t expScale[33] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56,
                                              66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250 };
        const bool linear = (curve == 0 || curve == 3);
        const int scale = linear ? (group * depth * 329) >> 12
                                 : ((int) expScale[juce::jmin (group, 32)] * depth * 329) >> 15;
        return curve < 2 ? -scale : scale;
    }
    inline int keyScaleUnits (int note, int bp, int ld, int rd, int lc, int rc)
    {
        const int offset = note - bp - 17;
        return offset >= 0 ? scaleCurveUnits ((offset + 1) / 3, rd, rc)
                           : scaleCurveUnits ((-(offset - 1)) / 3, ld, lc);
    }
    inline float velocityDb (int velocity, int kvs)
    {
        if (kvs <= 0) return 0.0f;
        static const float vel[9]   = { 4, 16, 32, 48, 64, 80, 100, 110, 127 };
        static const float units[9] = { 51.9f, 46.2f, 37.1f, 29.1f, 21.1f, 16.0f, 7.5f, 3.5f, 0.0f };
        const float v = juce::jlimit (vel[0], vel[8], (float) velocity);
        int i = 0;
        while (i < 7 && v > vel[i + 1]) ++i;
        const float t = (v - vel[i]) / (vel[i + 1] - vel[i]);
        return -0.75f * (units[i] + (units[i + 1] - units[i]) * t) * (float) kvs / 7.0f;
    }

    // the operator's frequency as a ratio of the note (fixed-frequency operators
    // as a ratio of middle C, where they sound as set)
    inline float ratioOf (const uint8_t* op, int transpose)
    {
        const bool fixed = (op[17] & 1) != 0;
        const int coarse = op[18] & 31;
        const int fine = juce::jlimit (0, 99, (int) op[19]);
        const double detune = std::exp2 (((double) juce::jlimit (0, 14, (int) op[20]) - 7.0) * 1.7 / 1200.0);
        double r;
        if (fixed)
            r = std::pow (10.0, (double) (coarse & 3) + fine / 100.0) / 261.6256;
        else
            r = (coarse == 0 ? 0.5 : (double) coarse) * (1.0 + fine / 100.0) * std::exp2 ((transpose - 24) / 12.0);
        return (float) juce::jlimit (0.0, 20.0, r * detune);
    }

    //==========================================================================
    // The DX7 envelope (four rates and levels), sampled every dt seconds while
    // the key is held, in dB - and FM12's DX7 Mode envelope for given A, D, peak
    // and sustain, the same way. The converter fits the second to the first.
    inline std::vector<float> dx7EnvCurve (const uint8_t* op, float dt, int steps)
    {
        float rates[4], targets[4];
        const float rso = rateScaleOffset (op[13] & 7, (int) kRefNote);
        for (int i = 0; i < 4; ++i) { rates[i] = rateDbPerSecond ((float) op[i] + rso); targets[i] = levelDb (op[4 + i]); }
        std::vector<float> out ((size_t) steps);
        float x = -96.0f;
        int stage = 0;
        for (int k = 0; k < steps; ++k)
        {
            const float target = targets[stage];
            if (x < target)
            {
                if (x < -53.1f) x = -53.1f;
                const float norm = juce::jlimit (0.0f, 1.0f, (x + 96.0f) / 96.0f);
                x += rates[stage] * (2.0f + 15.0f * (1.0f - norm)) * dt;
                if (x >= target) { x = target; if (stage < 2) ++stage; }
            }
            else if (x > target)
            {
                x -= rates[stage] * dt;
                if (x <= target) { x = target; if (stage < 2) ++stage; }
            }
            else if (stage < 2) ++stage;
            x = juce::jlimit (-96.0f, 0.0f, x);
            out[(size_t) k] = x;
        }
        return out;
    }

    inline float fm12EnvAt (float t, float attack, float decay, float peakDb, float sustainRelDb)
    {
        // attack from the jump to the peak along the DX7 curve (closed form of the boost law)
        float x;
        if (attack > 0.0005f && t < attack)
        {
            // u = (x + 96) / 96 relative to the peak; du/dt = rate (17 - 15u) / 96, rate = 10.48 / attack
            const float rate = 10.48f / attack;
            const float u0 = 1.0f - 53.1f / 96.0f;
            const float u = (17.0f - (17.0f - 15.0f * u0) * std::exp (-15.0f * rate * t / 96.0f)) / 15.0f;
            x = juce::jmin (0.0f, (u - 1.0f) * 96.0f);
        }
        else
        {
            const float td = t - attack;
            if (decay > 0.0005f && td < decay) x = sustainRelDb * (td / decay);
            else x = sustainRelDb;
        }
        return peakDb + x;
    }

    //==========================================================================
    struct OpSettings
    {
        float attack = 0, decay = 0, sustain = 1, release = 0, level = 0, ratio = 1, feedback = 0;
        bool carrier = false;
    };

    // a DX7 voice's six operators as FM12 operator settings (index 0 = OP1),
    // and its routes (src -> dst, 0-based within the six)
    struct Converted
    {
        std::array<OpSettings, 6> ops;
        std::vector<std::pair<int, int>> routes;
        bool audible = false;     // something reaches the output

        // the voice-wide settings: FM12's LFO and pitch envelope
        int   lfoWave = 0;
        float lfoRate = 5.0f, lfoDelay = 0.0f, lfoPitchCents = 0.0f, lfoWheelCents = 0.0f, lfoAmpDb = 0.0f;
        int   lfoAmpTarget = 0;   // carriers, modulators, all
        bool  lfoSync = true;
        float peStart = 0.0f, peTime = 0.2f;
    };

    inline Converted convert (const Vced& v)
    {
        Converted c;
        const auto& alg = algorithm (v[134]);
        const int fb = juce::jlimit (0, 7, (int) v[135]);
        const int transpose = juce::jlimit (0, 48, (int) v[144]);
        float carrierSum = 0;

        constexpr float dt = 0.01f;
        constexpr int steps = 400;           // the first four seconds of a held note

        for (int i = 0; i < 6; ++i)
        {
            const uint8_t* b = v.data() + (5 - i) * 21;     // VCED runs OP6 .. OP1
            auto& o = c.ops[(size_t) i];
            o.carrier = (alg.carrierMask & (1u << i)) != 0;
            o.ratio = ratioOf (b, transpose);

            // ---- envelope: fit FM12's A / D / peak / S to the DX7 curve ----
            const auto curve = dx7EnvCurve (b, dt, steps);
            const float l3 = levelDb (b[6]);
            const float maxDb = *std::max_element (curve.begin(), curve.end());
            int peakAt = (int) (std::max_element (curve.begin(), curve.end()) - curve.begin());
            const float attack0 = (float) peakAt * dt;
            const float floorDb = maxDb - 60.0f;

            float bestErr = 1.0e30f, bestA = attack0, bestD = 0.0f, bestP = maxDb;
            const float sustainAbs = juce::jmin (l3, maxDb);
            for (float pDb = maxDb; pDb >= juce::jmax (sustainAbs, maxDb - 18.0f) - 0.01f; pDb -= 0.5f)
                for (float aMul : { 0.6f, 0.8f, 1.0f, 1.25f, 1.6f })
                    for (int di = 0; di <= 48; ++di)
                    {
                        const float d = di == 0 ? 0.0f : 0.002f * std::pow (15000.0f, (float) (di - 1) / 47.0f);
                        const float a = attack0 * aMul;
                        float err = 0.0f;
                        for (int k = 0; k < steps; k += 2)
                        {
                            const float e1 = juce::jmax (curve[(size_t) k], floorDb);
                            const float e2 = juce::jmax (fm12EnvAt ((float) k * dt, a, d, pDb, sustainAbs - pDb), floorDb);
                            err += std::abs (e1 - e2) * (1.0f + (e1 - floorDb) / 30.0f);
                            if (err >= bestErr) break;
                        }
                        if (err < bestErr) { bestErr = err; bestA = a; bestD = d; bestP = pDb; }
                    }

            const float sustainRel = sustainAbs - bestP;
            o.attack = juce::jlimit (0.0f, 10.0f, bestA);
            o.decay = juce::jlimit (0.0f, 30.0f, bestD);
            o.sustain = sustainAbs <= -95.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, dbToGain (sustainRel));
            const float rso = rateScaleOffset (b[13] & 7, (int) kRefNote);
            const float r4 = rateDbPerSecond ((float) b[3] + rso);
            const float span = o.sustain > 0.0f ? sustainRel + 96.0f : 96.0f;
            o.release = juce::jlimit (0.0f, 30.0f, span / r4);

            // ---- level: output level, keyboard scaling and velocity at the reference note ----
            const int units = juce::jmin (127, scaleOutLevel (b[16])
                                  + keyScaleUnits ((int) kRefNote, b[8], b[9], b[10], b[11] & 3, b[12] & 3));
            const float staticDb = unitsToDb (units) + velocityDb (kRefVelocity, b[15] & 7);
            const float peakGain = dbToGain (staticDb + bestP);
            if (o.carrier)
            {
                o.level = juce::jlimit (0.0f, 1.0f, peakGain / (float) alg.numCarriers);
                carrierSum += o.level;
            }
            else
            {
                o.level = juce::jlimit (0.0f, 1.0f, peakGain * kModCycles / 21.0f);   // FM12's modulators: 21 cycles at level 1
            }
            if (fb > 0 && i == alg.fbDst)
                o.feedback = juce::jlimit (0.0f, 1.0f, std::exp2 ((float) (fb - 7)) * peakGain
                                                         / (o.carrier ? (float) alg.numCarriers : 1.0f));
        }

        for (int dst = 0; dst < 6; ++dst)
            for (int src = 0; src < 6; ++src)
                if (alg.modMask[dst] & (1u << src))
                    c.routes.push_back ({ src, dst });
        // algorithms 4 and 6 feed back over two or three operators; FM12's routes have no depth
        // of their own, so their feedback goes onto the operator that algorithm feeds back into

        // ---- LFO: speed, delay, wave and key sync as they are; pitch depth x sensitivity; the mod
        // wheel at half travel's worth of the same sensitivity (the DX7's usual wheel range 50) ----
        c.lfoRate = lfoSpeedHz (v[137]);
        c.lfoDelay = lfoHoldSeconds (v[138]);
        c.lfoWave = juce::jlimit (0, 5, (int) v[142]);
        c.lfoSync = v[141] != 0;
        const float pms = pmsSemitones (v[143] & 7);
        c.lfoPitchCents = (float) juce::jlimit (0, 99, (int) v[139]) / 99.0f * pms * 100.0f;
        c.lfoWheelCents = 0.5f * pms * 100.0f;
        // amplitude: the depth's curve times the deepest operator sensitivity, on the operators that have one
        {
            const float depth = std::pow ((float) juce::jlimit (0, 99, (int) v[140]) / 99.0f, 2.9f);
            float maxAms = 0.0f;
            bool onCarrier = false, onModulator = false;
            for (int i = 0; i < 6; ++i)
            {
                const int ams = v[(size_t) ((5 - i) * 21 + 14)] & 3;
                if (ams == 0 || c.ops[(size_t) i].level <= 0.0f) continue;
                maxAms = juce::jmax (maxAms, amsDb (ams));
                (c.ops[(size_t) i].carrier ? onCarrier : onModulator) = true;
            }
            c.lfoAmpDb = juce::jlimit (0.0f, 44.0f, depth * maxAms);
            c.lfoAmpTarget = onCarrier && onModulator ? 2 : (onModulator ? 1 : 0);
        }

        // ---- pitch envelope: a sweep from L4 (where the note starts) to L1, or a quick blip up to
        // L1 that falls back to L2 - both as FM12's start and time; a slow rise or a held offset is
        // left out ----
        {
            const float l[4] = { pitchEgSemitones (v[130]), pitchEgSemitones (v[131]), pitchEgSemitones (v[132]), pitchEgSemitones (v[133]) };
            const float r[4] = { pitchEgRate (v[126]), pitchEgRate (v[127]), pitchEgRate (v[128]), pitchEgRate (v[129]) };
            if (std::abs (l[3]) > 0.01f)                                   // starts off pitch (L4), sweeps in
            {
                c.peStart = l[3];
                c.peTime = std::abs (l[3] - l[0]) / juce::jmax (0.5f, r[0]);
            }
            else if (std::abs (l[0]) > 0.01f && std::abs (l[0]) / juce::jmax (0.5f, r[0]) < 0.05f)   // a quick blip to L1
            {
                c.peStart = l[0];
                c.peTime = std::abs (l[0] - l[1]) / juce::jmax (0.5f, r[1]);
            }
            c.peStart = juce::jlimit (-48.0f, 48.0f, c.peStart);
            c.peTime = juce::jlimit (0.0f, 10.0f, c.peTime);
        }

        c.audible = carrierSum > 0.0005f;
        return c;
    }

    //==========================================================================
    // writes a converted voice into FM12's operators [first, first + 6); the other
    // operators are untouched (a layer) or cleared (clearOthers)
    inline void apply (FM12SynthAudioProcessor& p, const Converted& c, int first, bool clearOthers, float levelScale = 1.0f)
    {
        auto set = [&p] (const juce::String& id, float value)
        {
            if (auto* prm = p.apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        };
        auto opId = [] (int op, const char* name) { return "op" + juce::String (op) + "_" + name; };

        if (clearOthers)
        {
            for (int op = 0; op < 12; ++op)
            {
                set (opId (op, "level"), 0.0f);
                set ("feedback_" + juce::String (op), 0.0f);
                set (opId (op, "cancel"), 0.0f);
                set (opId (op, "delay"), 0.0f);
                set (opId (op, "osc"), 0.0f);
                for (int to = 0; to < 12; ++to)
                    if (to != op)
                        set ("route_" + juce::String (op) + "_" + juce::String (to), 0.0f);
            }
        }
        else
        {
            for (int op = first; op < first + 6; ++op)
                for (int to = 0; to < 12; ++to)
                    if (to != op)
                    {
                        set ("route_" + juce::String (op) + "_" + juce::String (to), 0.0f);
                        set ("route_" + juce::String (to) + "_" + juce::String (op), 0.0f);
                    }
        }

        for (int i = 0; i < 6; ++i)
        {
            const auto& o = c.ops[(size_t) i];
            const int op = first + i;
            set (opId (op, "attack"), o.attack);
            set (opId (op, "decay"), o.decay);
            set (opId (op, "sustain"), o.sustain);
            set (opId (op, "release"), o.release);
            set (opId (op, "level"), o.carrier ? o.level * levelScale : o.level);
            set (opId (op, "ratio"), o.ratio);
            set (opId (op, "cancel"), 0.0f);
            set (opId (op, "delay"), 0.0f);
            set (opId (op, "osc"), 0.0f);
            set ("feedback_" + juce::String (op), o.feedback);
        }
        for (const auto& r : c.routes)
            set ("route_" + juce::String (first + r.first) + "_" + juce::String (first + r.second), 1.0f);

        if (clearOthers)                     // the first (or only) voice's LFO and pitch envelope
        {
            set ("lfoWave", (float) c.lfoWave);
            set ("lfoRate", juce::jlimit (0.05f, 50.0f, c.lfoRate));
            set ("lfoDelay", c.lfoDelay);
            set ("lfoPitch", c.lfoPitchCents);
            set ("lfoWheel", c.lfoWheelCents);
            set ("lfoAmp", c.lfoAmpDb);
            set ("lfoAmpTarget", (float) c.lfoAmpTarget);
            set ("lfoSync", c.lfoSync ? 1.0f : 0.0f);
            set ("peStart", c.peStart);
            set ("peTime", c.peTime);
        }
        set ("dx7Mode", 1.0f);
        set ("fmEngineMode", 0.0f);          // phase modulation, as in the DX7
        set ("feedbackModeExp", 0.0f);
        set ("nyquistLimit", 24000.0f);
        set ("masterVol", 0.8f);
    }

    // one voice on operators 1-6 (the rest silent)
    inline bool applyVoice (FM12SynthAudioProcessor& p, const Voice& v)
    {
        const auto c = convert (v.vced);
        if (! c.audible) return false;
        apply (p, c, 0, true);
        p.setVoiceName (v.name);
        return true;
    }

    // two voices layered: the first on operators 1-6, the second on 7-12, each at half level
    inline bool applyLayer (FM12SynthAudioProcessor& p, const Voice& a, const Voice& b)
    {
        const auto ca = convert (a.vced), cb = convert (b.vced);
        if (! ca.audible || ! cb.audible) return false;
        apply (p, ca, 0, true, 0.6f);
        apply (p, cb, 6, false, 0.6f);
        p.setVoiceName (a.name + " + " + b.name);
        return true;
    }

    //==========================================================================
    // .syx reading: 32-voice bulk dumps (several in one file too, or a bare
    // 4096-byte bank) and single-voice dumps
    inline Vced unpackVmem (const uint8_t* p)
    {
        Vced v {};
        for (int op = 0; op < 6; ++op)
        {
            const uint8_t* s = p + op * 17;
            uint8_t* d = v.data() + op * 21;
            for (int k = 0; k < 11; ++k) d[k] = s[k] & 127;
            d[11] = s[11] & 3;          d[12] = (s[11] >> 2) & 3;
            d[13] = s[12] & 7;          d[20] = (s[12] >> 3) & 15;
            d[14] = s[13] & 3;          d[15] = (s[13] >> 2) & 7;
            d[16] = s[14] & 127;
            d[17] = s[15] & 1;          d[18] = (s[15] >> 1) & 31;
            d[19] = s[16] & 127;
        }
        for (int k = 0; k < 8; ++k) v[(size_t) (126 + k)] = p[102 + k] & 127;
        v[134] = p[110] & 31;
        v[135] = p[111] & 7;            v[136] = (p[111] >> 3) & 1;
        for (int k = 0; k < 4; ++k) v[(size_t) (137 + k)] = p[112 + k] & 127;
        v[141] = p[116] & 1;            v[142] = (p[116] >> 1) & 7;     v[143] = (p[116] >> 4) & 7;
        v[144] = p[117] & 63;
        for (int k = 0; k < 10; ++k) v[(size_t) (145 + k)] = p[118 + k] & 127;
        return v;
    }

    inline juce::String nameOf (const Vced& v)
    {
        juce::String n;
        for (int k = 0; k < 10; ++k)
        {
            const char ch = (char) v[(size_t) (145 + k)];
            n += (ch >= 32 && ch < 127) ? juce::String::charToString ((juce::juce_wchar) ch) : juce::String (" ");
        }
        return n.trim();
    }

    inline bool looksLikeVoice (const Vced& v)
    {
        if (v[134] > 31 || v[135] > 7 || v[144] > 48) return false;
        for (int op = 0; op < 6; ++op)
            for (int k = 0; k < 8; ++k)
                if (v[(size_t) (op * 21 + k)] > 99) return false;
        return true;
    }

    inline std::vector<Voice> parseSysex (const juce::MemoryBlock& mb)
    {
        std::vector<Voice> out;
        const auto* d = static_cast<const uint8_t*> (mb.getData());
        const size_t n = mb.getSize();
        auto add = [&out] (const Vced& v)
        {
            if (! looksLikeVoice (v)) return;
            Voice voice { v, nameOf (v) };
            if (voice.name.equalsIgnoreCase ("INIT VOICE")) return;
            if (voice.name.isEmpty()) voice.name = "Voice " + juce::String ((int) out.size() + 1);
            out.push_back (voice);
        };

        if (n == 4096)        // a bare bank
        {
            for (int i = 0; i < 32; ++i) add (unpackVmem (d + i * 128));
            return out;
        }
        for (size_t i = 0; i + 6 <= n; ++i)
        {
            if (d[i] != 0xF0 || d[i + 1] != 0x43) continue;
            const int format = d[i + 3];
            if (format == 9 && i + 6 + 4096 <= n)            // 32 voices
            {
                for (int k = 0; k < 32; ++k) add (unpackVmem (d + i + 6 + k * 128));
                i += 4096 + 7;
            }
            else if (format == 0 && i + 6 + 155 <= n)        // one voice
            {
                Vced v {};
                std::copy (d + i + 6, d + i + 6 + 155, v.begin());
                add (v);
                i += 155 + 7;
            }
        }
        return out;
    }
}
