#pragma once

#include "../../Core/ModuleCore.h"

// Crossfade - equal-power A/B crossfader. Fade sets the position (0% = all A,
// 100% = all B); a cable onto Fade moves it. LFO -> Fade gives auto-morphing
// between two sound sources; a Gate can hard-switch them.
// Inputs: 0 = A In, 1 = B In. Output: 0 = Audio Out.
class CrossfadeModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pFade = 0 };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        const float pos = juce::jlimit (0.0f, 1.0f,
            param (pFade) * 0.01f);   // a cable onto Fade moves it

        const float gainB = std::sin (pos * juce::MathConstants<float>::halfPi);
        const float gainA = std::cos (pos * juce::MathConstants<float>::halfPi);

        outputs[0][0] = inputs[0][0] * gainA + inputs[1][0] * gainB;
        outputs[0][1] = inputs[0][1] * gainA + inputs[1][1] * gainB;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};

// A/B Switch - for comparing two effects on the same sound: the source goes into
// both, both come back here, and Listen picks A, B or their difference (A - B, what
// one does that the other does not). Switching fades over 5 ms, so it never clicks.
// Inputs: 0 = A In, 1 = B In. Output: 0 = Audio Out.
// A/B Fader (shown as such; the class and id keep 6.1's A/B Switch names)
class ABSwitchModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pListen = 0, pBalance };

    void prepare (double sr) override { step = (float) (1.0 / (0.005 * juce::jmax (1000.0, sr))); }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const int listen = juce::jlimit (0, 4, juce::roundToInt (param (pListen)));
        float targetA = listen == 1 ? 0.0f : 1.0f;                       // A, and A in the difference
        float targetB = listen == 1 ? 1.0f : (listen == 2 ? -1.0f : 0.0f);
        if (listen == 3)
        {
            // Fade: linear, A at the left, B at the right
            const float b = juce::jlimit (0.0f, 1.0f, param (pBalance) * 0.01f);
            targetA = 1.0f - b;
            targetB = b;
        }
        else if (listen == 4)
        {
            // (6.9.21) Equal Power: as the Crossfade module, a quarter circle
            const float b = juce::jlimit (0.0f, 1.0f, param (pBalance) * 0.01f);
            targetA = std::cos (b * juce::MathConstants<float>::halfPi);
            targetB = std::sin (b * juce::MathConstants<float>::halfPi);
        }
        gainA += juce::jlimit (-step, step, targetA - gainA);
        gainB += juce::jlimit (-step, step, targetB - gainB);
        outputs[0][0] = inputs[0][0] * gainA + inputs[1][0] * gainB;
        outputs[0][1] = inputs[0][1] * gainA + inputs[1][1] * gainB;
    }

private:
    float gainA = 1.0f, gainB = 0.0f, step = 0.005f;
};

//==============================================================================
// Latency: delays what goes through by a set time - whole milliseconds and
// hundredths of one - or by a number of blocks of a chosen size, to line up
// a chain with another one (a parallel path through a plugin, a module with
// latency of its own). The delay is reported with the patch's latency, so a
// DAW lines the track up as well. A change of the time fades over 10 ms from
// the old delay to the new one: no clicks, no pitch slide.
class LatencyModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pMode = 0, pCoarse, pFine, pBlocks, pBlockSize, pReport };
    static constexpr double kMaxSeconds = 2.1;

    void prepare (double sr) override
    {
        aquanode::SynthModule::prepare (sr);
        const int n = juce::nextPowerOfTwo ((int) std::ceil (kMaxSeconds * sr) + 8);
        for (auto& b : buffer) b.assign ((size_t) n, 0.0f);
        mask = n - 1;
        write = 0;
        current = target = delayFor (sr);
        fadeLeft = 0;
        fadeStep = 1.0f / (float) juce::jmax (1, (int) (0.01 * sr));
    }

    void reset() override
    {
        for (auto& b : buffer) std::fill (b.begin(), b.end(), 0.0f);
        current = target = delayFor (sampleRate);
        fadeLeft = 0;
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        if (mask == 0) { outputs[0] = inputs[0]; return; }
        const double want = delayFor (sampleRate);
        if (want != target && fadeLeft <= 0)
        {
            // (a change during a fade waits for it to end: the knob is read again then)
            previous = current;
            current = target = want;
            fadeLeft = juce::jmax (1, (int) (0.01 * sampleRate));
            fadePos = 0.0f;
        }
        buffer[0][(size_t) write] = inputs[0][0];
        buffer[1][(size_t) write] = inputs[0][1];
        float l = read (0, current), r = read (1, current);
        if (fadeLeft > 0)
        {
            fadePos += fadeStep;
            const float g = juce::jlimit (0.0f, 1.0f, fadePos);
            l = l * g + read (0, previous) * (1.0f - g);
            r = r * g + read (1, previous) * (1.0f - g);
            --fadeLeft;
        }
        outputs[0] = { l, r };
        write = (write + 1) & mask;
    }

    int latencySamples() const override
    {
        if (param (pReport) < 0.5f)
            return 0;
        return (int) std::lround (target);
    }

    // the delay the knobs ask for, in samples
    double delayFor (double sr) const
    {
        double samples;
        if (std::lround (param (pMode)) == 1)
        {
            static const int sizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };
            const int size = sizes[juce::jlimit (0, 7, (int) std::lround (param (pBlockSize)))];
            samples = (double) std::lround (param (pBlocks)) * size;
        }
        else
        {
            const double ms = std::floor (param (pCoarse) + 0.5) + juce::jlimit (0.0, 0.99, std::floor (param (pFine) + 0.5) * 0.01);
            samples = ms * 0.001 * sr;
        }
        return juce::jlimit (0.0, kMaxSeconds * sr - 4.0, samples);
    }

private:
    // a fractional read, 4-point Lagrange (whole samples come through untouched)
    float read (int ch, double delay) const
    {
        const double pos = (double) write - delay;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const int i = (int) fl;
        auto at = [&] (int k) { return buffer[(size_t) ch][(size_t) ((i + k) & mask)]; };
        if (t < 1.0e-6f)
            return at (0);
        const float ym1 = at (-1), y0 = at (0), y1 = at (1), y2 = at (2);
        // (under one sample of delay the 4th point is not written yet: the 3rd stands in)
        const float a = y1;
        const float b = delay < 1.0 ? y1 : y2;
        const float c0 = -t * (t - 1.0f) * (t - 2.0f) / 6.0f;
        const float c1 = (t + 1.0f) * (t - 1.0f) * (t - 2.0f) / 2.0f;
        const float c2 = -(t + 1.0f) * t * (t - 2.0f) / 2.0f;
        const float c3 = (t + 1.0f) * t * (t - 1.0f) / 6.0f;
        return c0 * ym1 + c1 * y0 + c2 * a + c3 * b;
    }

    std::array<std::vector<float>, 2> buffer;
    int mask = 0, write = 0;
    double current = 0.0, target = 0.0, previous = 0.0;
    int fadeLeft = 0;
    float fadePos = 0.0f, fadeStep = 0.01f;
};
