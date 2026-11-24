#pragma once
//==============================================================================
// RotaryCabinet (6.9.1) - the rotating speaker the Rotary module is made of,
// shared with the Drawbar Organ, which has one built in: a bass drum and a
// treble horn split at X-Over, each turning at its own speed with its own
// inertia, each moving the sound three ways (Throb: level, Width: across,
// the horn's Depth: its Doppler with a cabinet reflection), and the cabinet
// amp's tube drive before them.
//==============================================================================
#include "../Core/ModuleCore.h"

namespace aquanode
{
struct RotaryCabinet
{
    // the knobs, each block (the Rotary module's units)
    struct Settings
    {
        int speed { 1 };                  // 0 Stop, 1 Slow, 2 Fast
        double rate { 100.0 };            // %
        double inertia { 60.0 };          // %
        float xover { 800.0f };           // Hz
        float hornDepth { 60.0f }, hornThrob { 70.0f }, hornWidth { 70.0f };   // %
        float drumThrob { 50.0f }, drumWidth { 50.0f };                         // %
        float drumDepth { 0.0f };         // % - the drum's own Doppler (its turning scoop; the Rotary module has none)
        float drive { 0.0f };             // %
        float levelDb { 0.0f };
        // (6.9.11) the horn's speed at Slow and at Fast in RPM (the drum keeps
        // its usual share of it); < 0: kSpeeds (the Rotary module)
        double slowRpm { -1.0 }, fastRpm { -1.0 };
    };

    // rotor speeds in Hz at Rate 100 %: { drum, horn } for Stop, Slow, Fast
    static constexpr double kSpeeds[3][2] = { { 0.0, 0.0 }, { 0.588, 0.792 }, { 6.37, 7.68 } };
    // seconds to get 90 % of the way there at Inertia's middle setting, per target:
    // { drum, horn } - the light horn is always quicker than the heavy drum
    static constexpr double kSpinTime[3][2] = { { 0.12, 0.10 }, { 0.27, 0.18 }, { 0.14, 0.09 } };
    static constexpr double kSlowRpm = 0.792 * 60.0, kFastRpm = 7.68 * 60.0;   // the horn's, as above

    // { drum, horn } in Hz for a switch position, Rate and the RPM knobs
    static void targetSpeeds (const Settings& s, int sp, double& drum, double& horn)
    {
        double rate = s.rate * 0.01;
        if (sp == 1 && s.slowRpm >= 0.0) rate *= s.slowRpm / kSlowRpm;
        if (sp == 2 && s.fastRpm >= 0.0) rate *= s.fastRpm / kFastRpm;
        drum = kSpeeds[sp][0] * rate;
        horn = kSpeeds[sp][1] * rate;
    }

    void prepare (double sampleRate, const Settings& s)
    {
        sr = sampleRate;
        reset (s);
    }

    void reset (const Settings& s)
    {
        std::fill (std::begin (buf), std::end (buf), 0.0f);
        std::fill (std::begin (dBuf), std::end (dBuf), 0.0f);
        bufPos = dBufPos = 0;
        xo1 = xo2 = 0.0f;
        hornPhase = 1.6;
        drumPhase = 0.0;
        targetSpeeds (s, juce::jlimit (0, 2, s.speed), drumSpeed, hornSpeed);
    }

    void process (const Settings& s, float inL, float inR, float& outLeft, float& outRight)
    {
        // ---- the rotors: each heads for its speed with its own inertia
        const int sp = juce::jlimit (0, 2, s.speed);
        if (s.inertia != lastInertiaPct) { lastInertiaPct = s.inertia; inertiaMul = 0.25 * std::pow (48.0, s.inertia * 0.01); }
        const double inertia = inertiaMul;
        // a slowing rotor coasts (the time is the target's: Slow / Stop)
        double dTarget = 0.0, hTarget = 0.0;
        targetSpeeds (s, sp, dTarget, hTarget);
        const double dTime = kSpinTime[sp][0] * inertia, hTime = kSpinTime[sp][1] * inertia;
        if (dTime != lastDTime || hTime != lastHTime)
        {
            lastDTime = dTime; lastHTime = hTime;
            dCoefNow = 1.0 - std::pow (10.0, -1.0 / (sr * juce::jmax (0.001, dTime)));
            hCoefNow = 1.0 - std::pow (10.0, -1.0 / (sr * juce::jmax (0.001, hTime)));
        }
        drumSpeed += dCoefNow * (dTarget - drumSpeed);
        hornSpeed += hCoefNow * (hTarget - hornSpeed);

        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        drumPhase += twoPi * drumSpeed / sr;
        hornPhase += twoPi * hornSpeed / sr;
        if (drumPhase > twoPi) drumPhase -= twoPi;
        if (hornPhase > twoPi) hornPhase -= twoPi;

        const float hc = (float) std::cos (hornPhase);
        const float hc3 = hc * hc * hc;   // the horn's narrow lobe
        const float hs = (float) std::sin (hornPhase);
        const float dc = (float) std::cos (drumPhase);
        const float ds = (float) std::sin (drumPhase);

        // ---- the cabinet amp, then the crossover (two one-poles: lows / the rest)
        float x = 0.5f * (inL + inR);
        if (const float drive = s.drive * 0.01f; drive > 0.0f)
        {
            const float k = 1.0f + 11.0f * drive * drive;
            x = std::tanh (x * k) / std::pow (k, 0.7f);
        }

        const float xoHz = juce::jlimit (60.0f, (float) sr * 0.4f, s.xover);
        const float c = xoMemo (xoHz / (float) sr, [] (float f) { return std::exp (-juce::MathConstants<float>::twoPi * f); });
        xo1 = c * (xo1 - x) + x;
        xo2 = c * (xo2 - xo1) + xo1;
        const float low = xo2, high = x - xo2;

        // ---- throb: the level each rotor sends out as it turns
        const float hornThrob = 0.9f * juce::square (s.hornThrob * 0.01f);
        const float drumThrob = 0.9f * juce::square (s.drumThrob * 0.01f);
        float h = (1.0f - hornThrob * hc3) * high;
        float l = (1.0f - drumThrob * dc) * low;
        if (s.drumDepth > 0.0f)
        {
            // the drum's scoop moves its sound to and fro as well (a gentler, slower bend than the horn's)
            dBufPos = (dBufPos + kBufSize - 1) & (kBufSize - 1);
            dBuf[dBufPos] = l;
            const float dd = juce::jlimit (0.0f, (float) (kBufSize - 4), juce::square (s.drumDepth * 0.01f) * (float) sr / 450.0f * (1.0f + ds));
            const int i0 = (int) dd;
            const float f0 = dd - (float) i0;
            const float b0 = dBuf[(dBufPos + i0) & (kBufSize - 1)], b1 = dBuf[(dBufPos + i0 + 1) & (kBufSize - 1)];
            l = b0 + f0 * (b1 - b0);
        }

        // ---- the horn's Doppler: its sound read back from a line whose length
        // follows the horn round, added to the direct sound
        const float depth = juce::square (s.hornDepth * 0.01f) * (float) sr / 760.0f;
        bufPos = (bufPos + kBufSize - 1) & (kBufSize - 1);
        buf[bufPos] = h;
        const float dly = juce::jlimit (0.0f, (float) (kBufSize - 4), depth * (1.0f + hc3));
        const int di = (int) dly;
        const float frac = dly - (float) di;
        const float a0 = buf[(bufPos + di) & (kBufSize - 1)], a1 = buf[(bufPos + di + 1) & (kBufSize - 1)];
        h += a0 + frac * (a1 - a0);

        // ---- width: each rotor swings its sound across
        const float hornWidth = juce::square (s.hornWidth * 0.01f);
        const float drumWidth = juce::square (s.drumWidth * 0.01f);
        const float hw = h * hornWidth * hs, lw = l * drumWidth * ds;
        float outL = l + h + hw - lw;
        float outR = l + h - hw + lw;

        const float level = juce::Decibels::decibelsToGain (s.levelDb) * 0.95f;
        outLeft = outL * level;
        outRight = outR * level;
    }

    double hornHz() const { return hornSpeed; }

private:
    static constexpr int kBufSize = 2048;   // the horn's Doppler line (a few ms at any rate)
    double sr { 48000.0 };
    float buf[kBufSize] {}, dBuf[kBufSize] {};
    int bufPos { 0 }, dBufPos { 0 };
    float xo1 { 0.0f }, xo2 { 0.0f };       // the crossover's two one-poles
    double hornPhase { 1.6 }, drumPhase { 0.0 };   // radians
    double hornSpeed { 0.0 }, drumSpeed { 0.0 };   // Hz
    Memo xoMemo;
    double lastDTime { -1.0 }, lastHTime { -1.0 }, dCoefNow { 0.0 }, hCoefNow { 0.0 };   // (worked out when they change)
    double lastInertiaPct { -1.0 }, inertiaMul { 1.0 };
};
}
