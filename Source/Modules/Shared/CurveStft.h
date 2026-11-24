#pragma once
// (6.9.19) A 2048-point short-time Fourier transform that multiplies the sound's
// spectrum by a gain for every bin: the engine of the EQs whose curve is any
// shape (Ripple EQ, FM Filter, Audio to Filter). Hann windows in and out at 75 %
// overlap (their squares sum to 1.5, taken back out), so a flat curve gives the
// sound back unchanged, 2048 samples late (the module reports that latency).
// LiveCurve: the curve as the card shows it (128 points, 20 Hz - 20 kHz), written
// by the audio thread, read by the card's Graph.
#include "../Effects/Spectral/SpectralCore.h"

namespace aquanode
{
class CurveStft
{
public:
    static constexpr int kN = 2048, kHop = 512, kBins = kN / 2 + 1, kOut = 2 * kN;

    CurveStft()
    {
        fft = std::make_unique<FftRadix2> (11);
        for (int i = 0; i < kN; ++i)
            window[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kN);
        std::fill (std::begin (gains), std::end (gains), 1.0f);
    }

    void reset()
    {
        for (auto& c : in) std::fill (std::begin (c), std::end (c), 0.0f);
        for (auto& c : out) std::fill (std::begin (c), std::end (c), 0.0f);
        t = 0; count = 0;
    }

    // one stereo sample through; fill (float* gains) is called when a frame is due, to set
    // the gain of every bin (kBins of them) for that frame
    template <typename Fill>
    void process (float& l, float& r, Fill&& fill)
    {
        in[0][t % kN] = l;
        in[1][t % kN] = r;
        const int o = t % kOut;
        l = out[0][o]; out[0][o] = 0.0f;
        r = out[1][o]; out[1][o] = 0.0f;
        if (++count >= kHop)
        {
            count = 0;
            fill (gains);
            frame();
        }
        t = (t + 1) % (kN * kOut);   // (a multiple of both ring sizes)
    }

    // the last kN samples of a mono signal, windowed, as a magnitude per bin (for an analysis
    // the module does itself in fill: Audio to Filter's Shape In)
    void magnitudes (const float* ring, int ringPos, float* mags)
    {
        for (int i = 0; i < kN; ++i)
            frameBuf[i] = ring[(ringPos + i) % kN] * window[i];
        fft->forwardReal (frameBuf, bins);
        for (int b = 0; b < kBins; ++b) mags[b] = std::abs (bins[b]);
    }
    int writeIndex() const { return t % kN; }        // where this sample goes in a kN ring kept in step
    int ringStart() const { return (t + 1) % kN; }   // (in fill) the oldest sample of such a ring

    static float binHz (int b, double sr) { return (float) (b * sr / kN); }

private:
    void frame()
    {
        const int start = (t + 1) % kN;   // the oldest sample in the ring
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < kN; ++i)
                frameBuf[i] = in[c][(start + i) % kN] * window[i];
            fft->forwardReal (frameBuf, bins);
            for (int b = 0; b < kBins; ++b) bins[b] *= gains[b];
            fft->inverseReal (bins, frameBuf);
            constexpr float norm = 1.0f / 1.5f;
            for (int i = 0; i < kN; ++i)
                out[c][(t + 1 + i) % kOut] += frameBuf[i] * window[i] * norm;
        }
    }

    std::unique_ptr<FftRadix2> fft;
    float window[kN] {}, frameBuf[kN] {}, gains[kBins] {};
    std::complex<float> bins[FftRadix2::kMaxBins] {};
    float in[2][kN] {}, out[2][kOut] {};
    int t { 0 }, count { 0 };
};

struct LiveCurve
{
    static constexpr int kPts = 128;
    std::atomic<float> db[kPts] {};

    static float hzOf (int i) { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1)); }
    void store (int i, float v) { db[i].store (v, std::memory_order_relaxed); }
    float at (float hz) const
    {
        const float x = juce::jlimit (0.0f, (float) (kPts - 1), std::log (juce::jmax (1.0f, hz) / 20.0f) / std::log (1000.0f) * (float) (kPts - 1));
        const int i0 = juce::jmin (kPts - 2, (int) x);
        const float f = x - (float) i0;
        return db[i0].load (std::memory_order_relaxed) * (1.0f - f) + db[i0 + 1].load (std::memory_order_relaxed) * f;
    }
    // from per-bin linear gains
    void fromGains (const float* gains, double sr)
    {
        for (int i = 0; i < kPts; ++i)
        {
            const float bin = hzOf (i) * (float) CurveStft::kN / (float) sr;
            const int b0 = juce::jlimit (0, CurveStft::kBins - 2, (int) bin);
            const float f = juce::jlimit (0.0f, 1.0f, bin - (float) b0);
            const float g = gains[b0] * (1.0f - f) + gains[b0 + 1] * f;
            store (i, juce::Decibels::gainToDecibels (g, -100.0f));
        }
    }
};
}
