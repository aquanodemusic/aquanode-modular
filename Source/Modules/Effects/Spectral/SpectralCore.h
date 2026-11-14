#pragma once

#include "../../Core/ModuleCore.h"
#include <complex>

// SpectralCore - shared machinery for the Spectral effect modules, adapted from
// the SpectralSuite VSTs: a Hann-windowed STFT with 75 % overlap whose size
// each module chooses on its card (FFT: 512, 1024, 2048 or 4096 points; 1024
// is the default and what every patch from before had). Contains a
// self-contained radix-2 FFT (no juce_dsp dependency), an STFT overlap-add
// base class, and the shared drawable spectral-curve editor (64 points on a
// log-frequency axis, live input spectrum drawn behind it; drag to paint,
// double-click to reset). STFT latency is one FFT (fftSize samples); the dry
// path is delay-compensated so Dry/Wet never combs, and the module reports
// the latency (latencySamples) so the host can line the patch up.

namespace aquanode
{

//==============================================================================
// A radix-2 complex FFT of 512 .. 4096 points (order 9 .. 12), chosen at run
// time; every buffer is sized for the largest, so changing the size never
// allocates (it may happen on the audio thread).
class FftRadix2
{
public:
    static constexpr int kMaxOrder = 12;
    static constexpr int kMaxSize = 1 << kMaxOrder;          // 4096
    static constexpr int kMaxBins = kMaxSize / 2 + 1;        // 2049

    explicit FftRadix2 (int order = 10) { setOrder (order); }

    void setOrder (int order)
    {
        order = juce::jlimit (9, kMaxOrder, order);
        if (order == currentOrder)
            return;
        currentOrder = order;
        n = 1 << order;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                r |= ((i >> b) & 1) << (order - 1 - b);
            bitrev[i] = r;
        }
        for (int i = 0; i < n / 2; ++i)
        {
            const float a = -juce::MathConstants<float>::twoPi * (float) i / (float) n;
            twiddle[i] = { std::cos (a), std::sin (a) };
        }
    }

    int size() const noexcept    { return n; }
    int numBins() const noexcept { return n / 2 + 1; }
    int order() const noexcept   { return currentOrder; }

    void forwardReal (const float* in, std::complex<float>* outBins)
    {
        for (int i = 0; i < n; ++i)
            work[bitrev[i]] = { in[i], 0.0f };
        transform (false);
        const int nb = numBins();
        for (int b = 0; b < nb; ++b)
            outBins[b] = work[b];
    }

    void inverseReal (const std::complex<float>* inBins, float* out)
    {
        const int nb = numBins();
        for (int b = 0; b < nb; ++b)
            full[b] = inBins[b];
        for (int b = nb; b < n; ++b)
            full[b] = std::conj (inBins[n - b]);
        for (int i = 0; i < n; ++i)
            work[bitrev[i]] = full[i];
        transform (true);
        const float scale = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
            out[i] = work[i].real() * scale;
    }

private:
    void transform (bool inverse)
    {
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;
            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = twiddle[k * step];
                    if (inverse)
                        w = std::conj (w);
                    const auto a = work[start + k];
                    const auto b = work[start + k + half] * w;
                    work[start + k] = a + b;
                    work[start + k + half] = a - b;
                }
        }
    }

    int currentOrder { 0 }, n { 0 };
    int bitrev[kMaxSize];
    std::complex<float> twiddle[kMaxSize / 2];
    std::complex<float> work[kMaxSize];
    std::complex<float> full[kMaxSize];
};

using Fft1024 = FftRadix2;   // (the name it had while it was fixed at 1024 points)

//==============================================================================
// STFT overlap-add base. Subclasses implement processSpectrum(); modules with
// a second spectral input (Morph) also receive that input's bins.
//==============================================================================
class SpectralModuleBase : public SynthModule
{
public:
    // arrays are sized for the largest FFT; the live size is fftSize / hop / numBins
    static constexpr int kFftSize = FftRadix2::kMaxSize;
    static constexpr int kNumBins = FftRadix2::kMaxBins;
    static constexpr int kCurvePoints = 64;
    static constexpr int kDisplayPoints = 128;

    static const juce::StringArray& fftSizeChoices()
    {
        static const juce::StringArray c { "512", "1024", "2048", "4096" };
        return c;
    }
    static int fftOrderForChoice (int choice) { return 9 + juce::jlimit (0, 3, choice); }

    // measured: the first frame lands one FFT late (read from the knob, so a
    // graph rebuilt right after it was turned already knows the new size)
    int latencySamples() const override
    {
        const int idx = fftSizeParam >= 0 ? fftSizeParam : paramIndex ("fftSize");
        return idx >= 0 ? 1 << fftOrderForChoice ((int) std::lround (getParameterBase (idx))) : fftSize;
    }

    explicit SpectralModuleBase (int numSpectralInputs = 1, float curveDefault = 0.5f)
        : spectralInputs (juce::jlimit (1, 2, numSpectralInputs)),
          defaultCurveValue (curveDefault)
    {
        configure (10);
        for (auto& m : displayMags)
            m.store (0.0f, std::memory_order_relaxed);
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fftSizeParam = paramIndex ("fftSize");
        configure (fftOrderForChoice (fftSizeParam >= 0 ? (int) std::lround (getParameterBase (fftSizeParam)) : 1));
        curveDirty.store (true, std::memory_order_relaxed);
        reset();
    }

    void reset() override
    {
        for (int s = 0; s < 2; ++s)
            for (int c = 0; c < 2; ++c)
                std::fill (inRing[s][c].begin(), inRing[s][c].end(), 0.0f);
        for (int c = 0; c < 2; ++c)
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
        ringPos = 0;
        hopCount = 0;
        spectralReset();
    }

    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        curveDirty.store (true, std::memory_order_relaxed);
    }

    void blockStart() override
    {
        // a new FFT size: everything restarts at it (a short gap, as on any FFT size change)
        if (fftSizeParam >= 0)
        {
            const int order = fftOrderForChoice ((int) std::lround (getParameterBase (fftSizeParam)));
            if (order != fft.order())
            {
                configure (order);
                reset();
                curveDirty.store (true, std::memory_order_relaxed);
            }
        }
        if (curveDirty.exchange (false, std::memory_order_relaxed))
        {
            rebuildBinCurve();
            curveChanged();
        }
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        for (int s = 0; s < spectralInputs; ++s)
            for (int c = 0; c < 2; ++c)
                inRing[s][c][(size_t) ringPos] = inputs[s][(size_t) c];

        const float wet01 = dryWetAmount();
        const float wetG = std::sin (wet01 * juce::MathConstants<float>::halfPi);
        const float dryG = std::cos (wet01 * juce::MathConstants<float>::halfPi);
        const int dryIdx = (ringPos - fftSize) & (kRingSize - 1);

        for (int c = 0; c < 2; ++c)
        {
            const int outIdx = ringPos & (kOutMask);
            const float wet = outRing[c][(size_t) outIdx];
            outRing[c][(size_t) outIdx] = 0.0f;
            outputs[0][(size_t) c] = wet * wetG + inRing[0][c][(size_t) dryIdx] * dryG;
        }

        ringPos = (ringPos + 1) & (kRingSize - 1);

        if (++hopCount >= hop)
        {
            hopCount = 0;
            doFrame();
        }
    }

    // for the curve editor
    float getDisplayMagnitude (int i) const
    {
        return displayMags[(size_t) juce::jlimit (0, kDisplayPoints - 1, i)].load (std::memory_order_relaxed);
    }
    float getCurveDefault() const { return defaultCurveValue; }

    // append the hidden curve params to a descriptor, then the FFT size
    // (last, so the indices of saved patches stay put; patches from before
    // it existed open at 1024 points, the size they were made with)
    static void addCurveParams (ModuleDescriptor& d, float defaultValue)
    {
        for (int i = 0; i < kCurvePoints; ++i)
            d.params.push_back (makeRotary ("c" + juce::String (i), "C" + juce::String (i),
                                            0.0f, 1.0f, defaultValue, 9).hide());
        // (routing(): turning it rebuilds the graph, which re-reports the patch's latency)
        d.params.push_back (makeCombo ("fftSize", "FFT", fftSizeChoices(), 1, 8, 2).routing().whenMissing (1.0f));
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 64; }

protected:
    // subclass interface -----------------------------------------------------
    virtual void processSpectrum (int channel,
                                  std::complex<float>* bins,
                                  const std::complex<float>* sideBins) = 0;
    virtual void spectralReset() {}
    virtual void curveChanged() {}
    virtual float dryWetAmount() const { return 1.0f; }
    virtual int curveParamOffset() const = 0;   // index of "c0" in the param list
    virtual void fftSizeChanged() {}               // after a new size is set up (before reset)

    // the live STFT: its size, hop and number of bins (fftSize / 2 + 1)
    int fftSize { 1024 }, hop { 256 }, numBins { 513 };

    // curve value per bin, log-frequency interpolated (rebuilt in blockStart)
    float binCurve[kNumBins] {};

    static float binToLogPos (int bin, double sampleRate, int size)
    {
        const double freq = juce::jmax (20.0, (double) bin * sampleRate / (double) size);
        const double nyq = sampleRate * 0.5;
        return (float) juce::jlimit (0.0, 1.0, std::log (freq / 20.0) / std::log (nyq / 20.0));
    }

private:
    void rebuildBinCurve()
    {
        const int off = curveParamOffset();
        for (int b = 0; b < numBins; ++b)
        {
            const float x = binToLogPos (b, sampleRate, fftSize) * (float) (kCurvePoints - 1);
            const int i0 = juce::jlimit (0, kCurvePoints - 1, (int) x);
            const int i1 = juce::jmin (kCurvePoints - 1, i0 + 1);
            const float frac = x - (float) i0;
            binCurve[b] = param (off + i0) * (1.0f - frac) + param (off + i1) * frac;
        }
    }

    void configure (int order)
    {
        fft.setOrder (order);
        fftSize = fft.size();
        hop = fftSize / 4;
        numBins = fft.numBins();
        for (int i = 0; i < fftSize; ++i)
            window[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
        fftSizeChanged();
    }

    void doFrame()
    {
        for (int c = 0; c < 2; ++c)
        {
            if (spectralInputs > 1)
            {
                assembleFrame (1, c);
                fft.forwardReal (frame, sideBins);
            }

            assembleFrame (0, c);
            fft.forwardReal (frame, bins);

            if (c == 0)
                updateDisplay();

            processSpectrum (c, bins, spectralInputs > 1 ? sideBins : nullptr);

            fft.inverseReal (bins, frame);

            // overlap-add with synthesis window; Hann^2 at 75% overlap sums to 1.5
            const float norm = 1.0f / 1.5f;
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = (ringPos + i) & kOutMask;
                outRing[c][(size_t) idx] += frame[i] * window[i] * norm;
            }
        }
    }

    void assembleFrame (int source, int channel)
    {
        for (int i = 0; i < fftSize; ++i)
            frame[i] = inRing[source][channel][(size_t) ((ringPos - fftSize + i) & (kRingSize - 1))] * window[i];
    }

    void updateDisplay()
    {
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            // invert the log mapping: display point -> frequency -> bin
            const double t = (double) p / (double) (kDisplayPoints - 1);
            const double freq = 20.0 * std::pow (sampleRate * 0.5 / 20.0, t);
            const int b = juce::jlimit (0, numBins - 1, (int) (freq * fftSize / sampleRate));
            const float mag = std::abs (bins[b]) * 1024.0f / (float) fftSize;   // the same height at any size
            const float old = displayMags[(size_t) p].load (std::memory_order_relaxed);
            displayMags[(size_t) p].store (old * 0.7f + mag * 0.3f, std::memory_order_relaxed);
        }
    }

    static constexpr int kRingSize = 2 * FftRadix2::kMaxSize;   // power of two >= fftSize + latency headroom
    static constexpr int kOutMask = kRingSize - 1;

    const int spectralInputs;
    const float defaultCurveValue;

    FftRadix2 fft;
    int fftSizeParam { -1 };
    float window[kFftSize];
    float frame[kFftSize];
    std::complex<float> bins[kNumBins];
    std::complex<float> sideBins[kNumBins];

    std::array<float, kRingSize> inRing[2][2];
    std::array<float, kRingSize> outRing[2];
    int ringPos { 0 };
    int hopCount { 0 };

    std::atomic<bool> curveDirty { true };
    std::array<std::atomic<float>, kDisplayPoints> displayMags;
};

} // namespace aquanode
