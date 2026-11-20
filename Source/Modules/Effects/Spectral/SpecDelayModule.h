#pragma once

#include "SpectralCore.h"

// Spectral Delay - adapted from the SpectralLatency VST: every bin gets its own
// delay line (in whole FFT frames), set by the drawn curve times Longest Time.
// Draw a rising line and the highs arrive later than the lows - chords smear
// into arpeggios of frequency. Feedback (new here) regenerates each bin into
// its own delay for spectral shimmer trails. The original's negative-delay /
// host-latency-compensation half is dropped; delays are positive only.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpecDelayModule : public aquanode::SpectralModuleBase
{
public:
    // the history holds about 5.5 s at 48 kHz at every FFT size: more frames
    // of fewer bins at 512 points, fewer frames of more bins at 4096
    static constexpr int kMaxFrames = 2100;
    static constexpr size_t kCapacity = (size_t) 2100 * 257;
    enum ParamIndex { pMaxTime = 0, pFeedback, pDryWet };

    SpecDelayModule() : SpectralModuleBase (1, 0.0f)
    {
        for (int c = 0; c < 2; ++c)
            history[c].assign (kCapacity, { 0.0f, 0.0f });
    }

protected:
    void processSpectrum (int channel, std::complex<float>* bins, const std::complex<float>*) override
    {
        const double framesPerSecond = sampleRate / (double) hop;
        const int frames = juce::jlimit (2, kMaxFrames, (int) (kCapacity / (size_t) numBins));
        const float maxSeconds = param (pMaxTime);
        const float fb = param (pFeedback) * 0.01f * 0.9f;

        auto* hist = history[channel].data();
        const int w = writeFrame[channel];

        for (int b = 0; b < numBins; ++b)
        {
            const int delayFrames = juce::jlimit (0, frames - 1,
                (int) (binCurve[b] * maxSeconds * framesPerSecond));

            const int r = (w - delayFrames + frames) % frames;
            const std::complex<float> delayed = hist[(size_t) r * (size_t) numBins + (size_t) b];

            if (delayFrames == 0)
            {
                // zero delay: pass through (reading slot w here would be stale
                // pre-write data; feedback at zero delay would self-amplify)
                hist[(size_t) w * (size_t) numBins + (size_t) b] = bins[b];
            }
            else
            {
                hist[(size_t) w * (size_t) numBins + (size_t) b] = bins[b] + delayed * fb;
                bins[b] = delayed;
            }
        }

        writeFrame[channel] = (w + 1) % frames;
    }

    void spectralReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (history[c].begin(), history[c].end(), std::complex<float> (0.0f, 0.0f));
            writeFrame[c] = 0;
        }
    }

    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return 3; }

private:
    std::vector<std::complex<float>> history[2];
    int writeFrame[2] {};
};
