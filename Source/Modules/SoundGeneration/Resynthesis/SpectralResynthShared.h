#pragma once

// What the Spectral Resynthesis sources share (SpectralResynthModule.cpp, the
// Partials and Bins engines, the card) - not for other modules.
#include "SpectralResynthModule.h"
#include "ResynthAnalysis.h"

namespace aquanode::resynthdetail
{
    inline const juce::StringArray& fftChoices()    { static const juce::StringArray c { "1024", "2048", "4096", "8192", "16384" }; return c; }
    inline const juce::StringArray& hopChoices()    { static const juce::StringArray c { "1/4", "1/8", "1/16" }; return c; }
    inline const juce::StringArray& windowChoices() { static const juce::StringArray c { "Hann", "Blackman-Harris", "Nuttall", "Kaiser" }; return c; }
    inline const juce::StringArray& decimChoices()  { static const juce::StringArray c { "Off", "/2", "/4", "/8" }; return c; }

    inline juce::uint32 hash32 (juce::uint32 x) noexcept
    {
        x ^= x >> 16; x *= 0x7feb352du;
        x ^= x >> 15; x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }
    inline float hashUnit (juce::uint32 x) noexcept { return (float) (hash32 (x) & 0xffffffu) / (float) 0xffffff; }   // 0..1

    // a bin's magnitude in 1/256 dB steps from -160 dB (0 = silence), its phase in int16
    inline juce::uint16 encodeMag (float a) noexcept
    {
        if (! (a > 1.0e-8f)) return 0;
        const float q = (20.0f * std::log10 (a) + 160.0f) * 256.0f;
        return (juce::uint16) juce::jlimit (1.0f, 65535.0f, q + 0.5f);
    }
    inline float decodeMag (juce::uint16 q) noexcept
    {
        return q == 0 ? 0.0f : std::pow (10.0f, ((float) q / 256.0f - 160.0f) / 20.0f);
    }
    inline juce::int16 encodePhase (float ph) noexcept
    {
        ph = std::remainder (ph, juce::MathConstants<float>::twoPi);
        return (juce::int16) juce::jlimit (-32767, 32767, juce::roundToInt (ph * (32767.0f / juce::MathConstants<float>::pi)));
    }
    inline float decodePhase (juce::int16 q) noexcept { return (float) q * (juce::MathConstants<float>::pi / 32767.0f); }
    inline float princarg (float ph) noexcept { return std::remainder (ph, juce::MathConstants<float>::twoPi); }

    // the fundamental of one frame's peaks (loudest first): a harmonic sieve,
    // the candidate explaining the most of the level wins, a little held back
    // for every low harmonic it misses; near ties go to the higher candidate
    inline float frameFundamental (const std::vector<SpectralResynthModule::Peak>& peaks, float& confidence)
    {
        confidence = 0.0f;
        const int n = juce::jmin (24, (int) peaks.size());
        if (n == 0)
            return 0.0f;
        float total = 0.0f;
        for (int j = 0; j < n; ++j) total += peaks[(size_t) j].amp;
        if (total <= 0.0f)
            return 0.0f;
        struct Cand { float hz, score, explained; };
        std::vector<Cand> cands;
        for (int i = 0; i < juce::jmin (8, n); ++i)
            for (int k = 1; k <= 6; ++k)
            {
                const float c = peaks[(size_t) i].hz / (float) k;
                if (c < 25.0f || c > 3000.0f) continue;
                float explained = 0.0f;
                int hmax = 0;
                bool present[17] {};
                for (int j = 0; j < n; ++j)
                {
                    const float r = peaks[(size_t) j].hz / c;
                    const int h = juce::roundToInt (r);
                    if (h < 1 || h > 64) continue;
                    const float dev = 1200.0f * std::abs (std::log2 (r / (float) h));
                    if (dev < 30.0f)
                    {
                        explained += peaks[(size_t) j].amp * (1.0f - dev / 30.0f);
                        if (h <= 16) present[h] = true;
                        hmax = juce::jmax (hmax, h);
                    }
                }
                int missing = 0;
                for (int h = 1; h <= juce::jmin (8, hmax); ++h) if (! present[h]) ++missing;
                const float frac = explained / total;
                cands.push_back ({ c, frac - 0.05f * (float) missing, frac });
            }
        if (cands.empty()) return 0.0f;
        float best = -1.0e9f;
        for (auto& c : cands) best = juce::jmax (best, c.score);
        const Cand* pick = nullptr;
        for (auto& c : cands)
            if (c.score >= best - 0.02f && (pick == nullptr || c.hz > pick->hz)) pick = &c;
        confidence = juce::jlimit (0.0f, 1.0f, pick->explained);
        return pick->hz;
    }

    // the sound's fundamental: the frames' clear fundamentals (hz, frame
    // energy), their energy-weighted median, and how much of all the energy agrees
    inline void soundFundamental (std::vector<std::pair<float, double>> f0s, double totalEnergy, float& f0, float& confidence)
    {
        f0 = 0.0f; confidence = 0.0f;
        if (f0s.empty() || totalEnergy <= 0.0)
            return;
        std::sort (f0s.begin(), f0s.end());
        double sumW = 0.0;
        for (auto& p : f0s) sumW += p.second;
        double acc = 0.0;
        float median = f0s.front().first;
        for (auto& p : f0s) { acc += p.second; if (acc >= sumW * 0.5) { median = p.first; break; } }
        double agree = 0.0;
        for (auto& p : f0s)
            if (SpectralResynthModule::centsBetween (p.first, median) < 50.0f) agree += p.second;
        f0 = median;
        confidence = (float) (agree / totalEnergy);
    }
}

// the audio thread's working memory (one per module: the voices render one after another)
struct SpectralResynthModule::Scratch
{
    // Partials: the oscillators of one block, laid out for the inner loop
    struct Target { int track; float hz, amp; };
    static constexpr int kPad = kMaxSlots + 8;
    alignas (64) float re[kPad], im[kPad], c[kPad], s[kPad], a[kPad], da[kPad], gl[kPad], gr[kPad];
    std::vector<Target> targets;

    // Bins: transforms and one frame's worth of spectra
    std::array<std::unique_ptr<juce::dsp::FFT>, (size_t) (kMaxBinOrder - 9)> ffts;   // orders 10..14
    std::vector<float> work, mag, inPhase, omega, outMag, outPhase, toneGain;
    std::vector<int> peaks, regionPeak;
    std::vector<float> hann;             // the Hann window of hannN points
    int hannN = 0;

    void prepareBins()   // message thread
    {
        for (int o = 10; o <= kMaxBinOrder; ++o)
            if (ffts[(size_t) (o - 10)] == nullptr)
                ffts[(size_t) (o - 10)] = std::make_unique<juce::dsp::FFT> (o);
        const size_t nMax = (size_t) 1 << kMaxBinOrder;
        work.assign (nMax * 2, 0.0f);
        for (auto* v : { &mag, &inPhase, &omega, &outMag, &outPhase, &toneGain })
            v->assign (nMax / 2 + 1, 0.0f);
        peaks.assign (nMax / 2 + 1, 0);
        regionPeak.assign (nMax / 2 + 1, -1);
        hann.assign (nMax, 0.0f);
        hannN = 0;
    }
    juce::dsp::FFT& fft (int order) { return *ffts[(size_t) (order - 10)]; }
};
