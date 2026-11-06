#pragma once
//==============================================================================
// DriveCore - the distortion behind the Drive module and Cove Groove's master
// drive. No JUCE, so both can share it.
//
//   Tube   an asymmetric tanh: Bias pushes the curve off centre, adding even
//          harmonics (a DC blocker takes the offset away again)
//   Tape   a soft arctangent with a little top-end loss that grows with Drive
//   Fuzz   hard, asymmetric clipping after a lot of gain, a hint of folding
//   Crush  sample-rate and bit reduction, then a soft clip
//
// Tone tilts the sound around 800 Hz before the curve and back after it, so
// positive Tone drives the top harder (brighter, fizzier distortion) and
// negative Tone the low end (thicker), without changing the overall balance
// much. The curves run at twice the sample rate (a 31-tap half-band filter up
// and down), Crush excepted, whose aliasing is the point. Auto Gain follows
// the loudness going in and coming out (300 ms) and evens them out, so Drive
// changes the colour rather than the level; Output trims after it.
//==============================================================================
#include <array>
#include <cmath>
#include <algorithm>

namespace aquanode::drive
{
    enum Mode { Tube = 0, Tape, Fuzz, Crush, NumModes };

    inline const char* modeName (int m)
    {
        static const char* n[NumModes] = { "Tube", "Tape", "Fuzz", "Crush" };
        return n[std::clamp (m, 0, NumModes - 1)];
    }

    struct Settings
    {
        int mode { Tube };
        float drive { 0.3f };       // 0..1
        float tone { 0.0f };        // -1..1
        float bias { 0.2f };        // 0..1, Tube / Fuzz asymmetry
        float mix { 1.0f };         // 0..1
        float outputDb { 0.0f };
        bool autoGain { true };
    };

    class Channel
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            // half-band low-pass for 2x (windowed sinc, cutoff at a quarter of the 2x rate)
            const int M = kTaps - 1;
            float sum = 0.0f;
            for (int i = 0; i < kTaps; ++i)
            {
                const float x = (float) i - 0.5f * (float) M;
                const float sinc = std::abs (x) < 1.0e-6f ? 0.5f : std::sin (0.5f * 3.14159265f * x) / (3.14159265f * x);
                const float w = 0.42f - 0.5f * std::cos (2.0f * 3.14159265f * (float) i / (float) M) + 0.08f * std::cos (4.0f * 3.14159265f * (float) i / (float) M);
                h[(size_t) i] = sinc * w;
                sum += h[(size_t) i];
            }
            for (auto& c : h) c /= sum;
            tiltA = 1.0f - std::exp (-2.0f * 3.14159265f * 800.0f / sr);
            rmsA = 1.0f - std::exp (-1.0f / (0.3f * sr));
            agA = 1.0f - std::exp (-1.0f / (0.05f * sr));
            dcA = 1.0f - std::exp (-2.0f * 3.14159265f * 12.0f / sr);
            reset();
        }

        void reset()
        {
            up.fill (0.0f); down.fill (0.0f); dry.fill (0.0f); dryPos = 0;
            upPos = downPos = 0;
            tiltPre = tiltPost = dcZ = tapeZ = 0.0f;
            holdValue = 0.0f; holdPhase = 1.0f;
            inPow = outPow = 1.0e-6f; agGain = 1.0f;
        }

        float process (float x, const Settings& s)
        {
            const float d = std::clamp (s.drive, 0.0f, 1.0f);
            const float gain = std::pow (10.0f, d * 1.6f);              // up to +32 dB into the curve
            const float tilt = std::clamp (s.tone, -1.0f, 1.0f);
            const float hiG = std::pow (2.0f, tilt * 1.5f), loG = 1.0f / hiG;

            // pre-emphasis
            tiltPre += tiltA * (x - tiltPre);
            const float pre = tiltPre * loG + (x - tiltPre) * hiG;

            float y;
            if (s.mode == Crush)
            {
                y = crush (pre * gain, d) ;
            }
            else
            {
                // 2x: two samples through the curve, filtered back down
                const float u0 = upsample (pre * gain * 2.0f);
                const float u1 = upsample (0.0f);
                const float c0 = shape (u0, s), c1 = shape (u1, s);
                downPush (c0);
                y = downPush (c1, true);
            }

            // de-emphasis and the DC the asymmetry made
            tiltPost += tiltA * (y - tiltPost);
            y = tiltPost * hiG + (y - tiltPost) * loG;
            dcZ += dcA * (y - dcZ);
            y -= dcZ;

            const float out = std::pow (10.0f, s.outputDb * 0.05f);
            float wet = y * makeup (gain);
            if (s.autoGain)
            {
                inPow += rmsA * (x * x - inPow);
                outPow += rmsA * (wet * wet - outPow);
                const float target = std::clamp (std::sqrt ((inPow + 1.0e-9f) / (outPow + 1.0e-9f)), 0.1f, 4.0f);
                agGain += agA * (target - agGain);
                wet *= agGain;
            }
            wet *= out;

            // the dry side waits for the 2x filters (15 samples) so a partial Mix does not comb
            dry[(size_t) dryPos] = x;
            const float dx = s.mode == Crush ? x : dry[(size_t) ((dryPos + kDryLen - kLatency) % kDryLen)];
            if (++dryPos >= kDryLen) dryPos = 0;
            return dx + (wet - dx) * std::clamp (s.mix, 0.0f, 1.0f);
        }

        // a rough loudness match: a full-scale-ish drum loop comes out at about the level it went in
        static float makeup (float gain) { return std::pow (1.0f + 0.35f * (gain - 1.0f), -0.75f); }

    private:
        static constexpr int kTaps = 31;
        static constexpr int kLatency = 15, kDryLen = 32;
        std::array<float, kDryLen> dry {};
        int dryPos { 0 };

        float shape (float x, const Settings& s)
        {
            switch (s.mode)
            {
                case Tube:
                {
                    const float b = 0.6f * std::clamp (s.bias, 0.0f, 1.0f);
                    return std::tanh (x + b) - std::tanh (b);
                }
                case Tape:
                {
                    tapeZ += 0.5f * (x - tapeZ);                                  // a little top loss
                    return 0.6366f * std::atan (tapeZ * 1.5708f);
                }
                default:
                {
                    const float b = 0.4f * std::clamp (s.bias, 0.0f, 1.0f);
                    float v = x * 1.5f;
                    if (v > 1.0f - b) v = (1.0f - b) - 0.15f * std::sin (std::min (3.0f, v - (1.0f - b)));   // fold a hint
                    return std::clamp (v, -1.0f, 1.0f - b);
                }
            }
        }

        float crush (float x, float d)
        {
            // rate from the full rate down to ~2 kHz, bits from 16 down to 3
            const float rate = sr * std::pow (0.04f, d);
            holdPhase += rate / sr;
            if (holdPhase >= 1.0f)
            {
                holdPhase -= 1.0f;
                const float levels = std::pow (2.0f, 16.0f - 13.0f * d);
                holdValue = std::round (std::tanh (x) * levels) / levels;
            }
            return holdValue;
        }

        float upsample (float x)
        {
            up[(size_t) upPos] = x;
            float acc = 0.0f;
            int idx = upPos;
            for (int i = 0; i < kTaps; ++i)
            {
                acc += h[(size_t) i] * up[(size_t) idx];
                if (--idx < 0) idx = kTaps - 1;
            }
            if (++upPos >= kTaps) upPos = 0;
            return acc;
        }

        float downPush (float x, bool output = false)
        {
            down[(size_t) downPos] = x;
            float acc = 0.0f;
            if (output)
            {
                int idx = downPos;
                for (int i = 0; i < kTaps; ++i)
                {
                    acc += h[(size_t) i] * down[(size_t) idx];
                    if (--idx < 0) idx = kTaps - 1;
                }
            }
            if (++downPos >= kTaps) downPos = 0;
            return acc;
        }

        float sr { 48000.0f };
        std::array<float, kTaps> h {}, up {}, down {};
        int upPos { 0 }, downPos { 0 };
        float tiltA { 0.1f }, tiltPre { 0.0f }, tiltPost { 0.0f };
        float dcA { 0.001f }, dcZ { 0.0f }, tapeZ { 0.0f };
        float holdValue { 0.0f }, holdPhase { 1.0f };
        float rmsA { 0.0001f }, agA { 0.001f }, inPow { 1.0e-6f }, outPow { 1.0e-6f }, agGain { 1.0f };
    };

    class Stereo
    {
    public:
        void prepare (double sr) { ch[0].prepare (sr); ch[1].prepare (sr); }
        void reset() { ch[0].reset(); ch[1].reset(); }
        void process (float& l, float& r, const Settings& s) { l = ch[0].process (l, s); r = ch[1].process (r, s); }
    private:
        Channel ch[2];
    };
}
