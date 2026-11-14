#pragma once

#include "../../Core/ModuleCore.h"

// Dub Echo - a two-head tape echo, modelled on measurements of an old
// SynthEdit dub delay (arcDev Dubb Box, rendered under Wine: impulse and
// sweep responses per repeat, harmonics against level, wow spectra).
//
// What the measurements showed, and what is built here:
//   - the tape: Drive into a resonant low-pass sitting on a shelf (Tone moves
//     its corner, ~220 Hz .. 5 kHz; the highs stay ~11 dB down), then a soft
//     odd-harmonic saturation, then the delay line. The repeats go round the
//     same filter and saturation again, so every repeat is darker and more
//     worn, and Feedback above 100 % makes the loop sing on its own, held up
//     by the saturation instead of exploding.
//   - a second playback head at a musical ratio of the first (Head 2), feeding
//     the loop as well, as on a multi-head echo.
//   - wow: slow, random speed drift (most of it below 1 Hz); Flutter adds the
//     faster part. Changing Time glides the pitch, as a tape speed change does.
//   - the machine's noise: Noise is the playback hiss of a real tape echo,
//     shaped after a recorded Roland RE-201's noise floor (a mid-band hiss,
//     ~250 Hz .. 4 kHz, the highs falling away steeply), on the heads; Echo
//     Noise records that hiss and a little rumble onto the tape with the
//     sound, so it repeats through the tone filter and the saturation and
//     builds up with Feedback; and an equal-power Mix.
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
class DubEchoModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTime = 0, pSync, pDivision, pFeedback, pTone, pDrive, pHead2, pHead2Level,
                      pWow, pFlutter, pNoise, pWidth, pMix, pEchoNoise };

    static constexpr double kMaxSeconds = 3.2;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    double voiceTailSeconds() const override { return 0.0; }

private:
    struct Biquad
    {
        float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 };
        float z1 { 0 }, z2 { 0 };
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void setLowpass (double fc, double q, double sr) noexcept;
        void clear() noexcept { z1 = z2 = 0.0f; }
    };

    struct Channel
    {
        std::vector<float> line;
        Biquad lp1, lp2;      // the tone filter's two resonant sections
        float top { 0 };      // the tape's own gentle top roll-off
        float dc { 0 };       // a DC blocker inside the loop
        float fbState { 0 };  // what the heads played last sample (the loop)
    };

    float readLine (const std::vector<float>& line, double delaySamples) const noexcept;
    void updateTone (float toneKnob);

    Channel ch[2];
    int writePos { 0 };
    double smoothDelay { 0.0 };
    float smCoef { 0.0f }, topCoef { 0.0f }, dcCoef { 0.0f };
    float lastTone { -1.0f };

    // wow and flutter: smoothed random walks
    juce::Random rng { 0x7a9e };
    double wowA { 0.0 }, wowB { 0.0 }, wowTargetA { 0.0 }, wowTargetB { 0.0 };
    int wowCountA { 0 }, wowCountB { 0 };
    float flutterState { 0.0f }, flutterState2 { 0.0f };

    // noise: a brownish rumble through a low-pass, recorded onto the tape
    float noiseLp1 { 0.0f }, noiseLp2 { 0.0f };
    // and the playback hiss (RE-201): white through a high-pass and 4 low-pass poles
    float hissX { 0.0f }, hissHp { 0.0f }, hissHpCoef { 0.0f };
    Biquad hissLp1, hissLp2;
    // Echo Noise: the same kind of hiss, recorded onto the tape
    float recX { 0.0f }, recHp { 0.0f };
    Biquad recLp1, recLp2;
};
