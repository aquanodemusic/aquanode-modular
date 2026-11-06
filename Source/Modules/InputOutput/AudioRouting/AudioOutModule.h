#pragma once
#include "../../Core/ModuleCore.h"

// Audio Out - the patch's output back to the host, with a level knob and a
// soft clipper.
//
// Soft Clip acts on the patch's whole output - every Audio Out added together
// (the processor runs it on the sum, with the highest Soft Clip of the Audio
// Outs) - so a patch with two Audio Outs sounds as one with a single Audio Out
// after a mix, and the sum never passes 0 dBFS either.
//
// Soft Clip (0-100 %, 0 = off) bends the output smoothly toward full scale
// instead of letting it clip hard: below a knee it leaves the signal as it is,
// above it the curve rounds off and never passes 0 dBFS. The knob sets the
// knee: 25 % starts bending at -3 dBFS, 100 % at -12 dBFS. The curve runs at
// twice the sample rate between linear-phase half-band filters (flat within
// 0.001 dB up to 20 kHz at 48 kHz, -80 dB above), so a hard push does not fold
// harsh overtones back down and what stays below the knee is untouched; it
// delays the output by 31 samples (0.65 ms at 48 kHz) while it is on. The
// curve levels off at -1 dBFS, which leaves room for the filter's ripple
// around a hard-pushed edge; a last guard holds anything still over at 0 dBFS.
class AudioOutModule : public aquanode::SynthModule
{
public:
    static constexpr int kTaps = 32;          // the half-band's even taps (the odd ones are 0 but the centre)
    static constexpr int kLatency = 31;       // samples at the patch's rate

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override {}

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame*) override
    {
        const float level = param (0);
        const float l = inputs[0][0] * level, r = inputs[0][1] * level;
        outL = std::isfinite (l) ? l : 0.0f;
        outR = std::isfinite (r) ? r : 0.0f;
    }

    bool providesHostOutput() const override { return true; }
    void getHostOutput (float& l, float& r) override { l = outL; r = outR; }
    float hostSoftClip() const override { return param (1) * 0.01f; }


    static constexpr float kCeiling = 0.8913f;   // -1 dBFS

    // the curve: as it is up to the knee k, then rounding off toward the ceiling
    static float curve (float x, float k)
    {
        k = std::min (k, 0.9f * kCeiling);
        const float a = std::abs (x);
        if (a <= k) return x;
        const float y = k + (kCeiling - k) * std::tanh ((a - k) / (kCeiling - k));
        return x < 0.0f ? -y : y;
    }

    // one channel: 2x up, the curve, 2x down
    struct Channel
    {
        std::array<float, kTaps> in {}, evens {};
        std::array<float, 17> odds {};
        int pos { 0 }, oddPos { 0 };

        float process (float x, float k)
        {
            in[(size_t) pos] = x;
            // the two samples at twice the rate: the filtered one, and the
            // centre tap's (x 15 samples ago, as 2 * 0.5 = 1)
            float e = 0.0f;
            for (int j = 0, idx = pos; j < kTaps; ++j)
            {
                e += half()[(size_t) j] * in[(size_t) idx];
                if (--idx < 0) idx = kTaps - 1;
            }
            e *= 2.0f;
            const float o = in[(size_t) ((pos + kTaps - 15) % kTaps)];

            evens[(size_t) pos] = curve (e, k);
            odds[(size_t) oddPos] = curve (o, k);

            float y = 0.0f;
            for (int j = 0, idx = pos; j < kTaps; ++j)
            {
                y += half()[(size_t) j] * evens[(size_t) idx];
                if (--idx < 0) idx = kTaps - 1;
            }
            y += 0.5f * odds[(size_t) ((oddPos + 1) % 17)];   // the odd stream 16 samples ago

            if (++pos >= kTaps) pos = 0;
            if (++oddPos >= 17) oddPos = 0;
            return y;
        }

        static const std::array<float, kTaps>& half()
        {
            // a 63-tap Kaiser (beta 8) half-band low-pass at a quarter of the doubled rate: its even taps
            static const std::array<float, kTaps> h {
            -2.401525243681e-05f, 1.090362305618e-04f, -2.935600810029e-04f, 6.381063754399e-04f,
            -1.222076003196e-03f, 2.145908571093e-03f, -3.534414302088e-03f, 5.543647017155e-03f,
            -8.376210429227e-03f, 1.231558303467e-02f, -1.780470021177e-02f, 2.563768528213e-02f,
            -3.748938156761e-02f, 5.772404439432e-02f, -1.024425087815e-01f, 3.170728721009e-01f,
            3.170728721009e-01f, -1.024425087815e-01f, 5.772404439432e-02f, -3.748938156761e-02f,
            2.563768528213e-02f, -1.780470021177e-02f, 1.231558303467e-02f, -8.376210429227e-03f,
            5.543647017155e-03f, -3.534414302088e-03f, 2.145908571093e-03f, -1.222076003196e-03f,
            6.381063754399e-04f, -2.935600810029e-04f, 1.090362305618e-04f, -2.401525243681e-05f };
            return h;
        }
    };

    // the whole output's clipper (the processor holds one, for the sum of the Audio Outs)
    struct Clipper
    {
        void reset() { for (auto& c : ch) c = {}; }
        void process (float& l, float& r, float amount)
        {
            const float knee = juce::Decibels::decibelsToGain (-12.0f * amount);
            l = juce::jlimit (-1.0f, 1.0f, ch[0].process (l, knee));
            r = juce::jlimit (-1.0f, 1.0f, ch[1].process (r, knee));
        }
        Channel ch[2];
    };

private:
    float outL { 0.0f }, outR { 0.0f };
};
