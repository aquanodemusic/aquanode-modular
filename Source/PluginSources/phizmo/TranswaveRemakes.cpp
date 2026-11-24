// Built-in transwave remakes (see TranswaveRemakes.h).
#include "TranswaveRemakes.h"
#include <cmath>
#include <mutex>

namespace twremake
{
namespace data
{
    struct Entry { const char* name; int styles; int offset; int length; };
}
}

#include "TranswaveRemakeData.inl"

namespace twremake
{
namespace
{
    const std::vector<juce::uint8>& raw()
    {
        static std::vector<juce::uint8> bytes;
        static std::once_flag once;
        std::call_once (once, []
        {
            juce::String b64;
            for (int i = 0; i < data::kNumChunks; ++i)
                b64 += data::kChunks[i];
            juce::MemoryOutputStream z;
            if (! juce::Base64::convertFromBase64 (z, b64))
                return;
            juce::MemoryInputStream zin (z.getData(), z.getDataSize(), false);
            juce::GZIPDecompressorInputStream gz (&zin, false, juce::GZIPDecompressorInputStream::zlibFormat);
            juce::MemoryBlock mb;
            gz.readIntoMemoryBlock (mb);
            if ((int) mb.getSize() == data::kRawSize)
                bytes.assign ((const juce::uint8*) mb.getData(), (const juce::uint8*) mb.getData() + mb.getSize());
        });
        return bytes;
    }

    float lcgPhase (juce::uint32& s)
    {
        s = s * 1664525u + 1013904223u;
        return (float) ((double) (s >> 8) / 16777216.0 * 2.0 * juce::MathConstants<double>::pi
                        - juce::MathConstants<double>::pi);
    }

    // the phase laws the tables are rebuilt with
    void phases (int law, std::vector<double>& ph)
    {
        const double pi = juce::MathConstants<double>::pi;
        ph.assign (1024, 0.0);
        juce::uint32 s = (law == 3) ? 12345u : 777u;
        for (int h = 0; h < 1024; ++h)
        {
            switch (law)
            {
                case 1:  ph[(size_t) h] = (h % 2) ? -pi / 2 : pi / 2; break;
                case 2:  ph[(size_t) h] = pi * h * (h - 1) / 1023.0; break;
                case 4:  ph[(size_t) h] = (h % 2) ? 0.0 : pi; break;
                case 5:  ph[(size_t) h] = pi * h * (h - 1) / 2047.0; break;
                default: ph[(size_t) h] = lcgPhase (s); break;   // 3 and 6: fixed scrambles
            }
        }
    }
}

int count() { return data::kNumEntries; }

juce::String name (int i)
{
    return (i >= 0 && i < data::kNumEntries) ? juce::String (data::kEntries[i].name) : juce::String();
}

int styles (int i) { return (i >= 0 && i < data::kNumEntries) ? data::kEntries[i].styles : 0; }

int indexOf (const juce::String& n)
{
    for (int i = 0; i < data::kNumEntries; ++i)
        if (n == data::kEntries[i].name)
            return i;
    return -1;
}

int pick (int styleBits, juce::Random& r)
{
    juce::Array<int> fit;
    for (int i = 0; i < data::kNumEntries; ++i)
        if ((data::kEntries[i].styles & styleBits) != 0)
            fit.add (i);
    return fit.isEmpty() ? -1 : fit[r.nextInt (fit.size())];
}

bool render (int index, std::vector<std::vector<float>>& frames)
{
    frames.clear();
    const auto& all = raw();
    if (all.empty() || index < 0 || index >= data::kNumEntries)
        return false;
    const auto& e = data::kEntries[index];
    if (e.offset + e.length > (int) all.size())
        return false;
    const juce::uint8* p = all.data() + e.offset;
    const juce::uint8* end = p + e.length;

    auto u16 = [&p] { const int v = p[0] | (p[1] << 8); p += 2; return v; };
    const int F = u16();
    const int K = *p++;
    const int law = *p++;
    const int H = 64 * (int) *p++;
    const double rmsTarget = u16() / 65535.0;
    int octaves = 0;
    for (int h = H; h < 1024; h *= 2) ++octaves;
    const int NB = 12 * octaves;
    const int W = H + NB;
    if (F <= 0 || K <= 0 || H <= 0 || p + 2 * K + K * W > end)
        return false;

    std::vector<int> keys ((size_t) K);
    for (auto& k : keys) k = u16();
    std::vector<float> rows ((size_t) (K * W));
    std::vector<int> prev ((size_t) W, 0);
    for (int k = 0; k < K; ++k)
        for (int j = 0; j < W; ++j)
        {
            prev[(size_t) j] = (prev[(size_t) j] + *p++) & 255;
            rows[(size_t) (k * W + j)] = (float) prev[(size_t) j];
        }

    std::vector<double> ph;
    phases (law, ph);
    // the band centres, in log harmonic
    std::vector<double> lc ((size_t) NB);
    for (int i = 0; i < NB; ++i)
        lc[(size_t) i] = std::log ((double) H) + (i + 0.5) / 12.0 * std::log (2.0);

    juce::dsp::FFT fft (11);
    std::vector<float> buf ((size_t) (2 * kFrameSize));
    std::vector<double> q ((size_t) W), qh (1024, 0.0);
    frames.assign ((size_t) F, std::vector<float> ((size_t) kFrameSize, 0.0f));
    double sumSq = 0.0;

    for (int f = 0; f < F; ++f)
    {
        // the keyframes around this frame
        int j = 0;
        while (j + 1 < K && keys[(size_t) (j + 1)] <= f) ++j;
        if (j >= K - 1)
            for (int c = 0; c < W; ++c) q[(size_t) c] = rows[(size_t) ((K - 1) * W + c)];
        else
        {
            const int a = keys[(size_t) j], b = keys[(size_t) (j + 1)];
            const double t = (double) (f - a) / (double) juce::jmax (1, b - a);
            for (int c = 0; c < W; ++c)
                q[(size_t) c] = rows[(size_t) (j * W + c)] * (1.0 - t) + rows[(size_t) ((j + 1) * W + c)] * t;
        }

        for (int h = 1; h <= H; ++h) qh[(size_t) h] = q[(size_t) (h - 1)];
        for (int h = H + 1; h < 1024; ++h)
        {
            const double lh = std::log ((double) h);
            double v;
            if (lh <= lc.front()) v = q[(size_t) H];
            else if (lh >= lc.back()) v = q[(size_t) (W - 1)];
            else
            {
                int i = 0;
                while (lc[(size_t) (i + 1)] < lh) ++i;
                const double t = (lh - lc[(size_t) i]) / (lc[(size_t) (i + 1)] - lc[(size_t) i]);
                v = q[(size_t) (H + i)] * (1.0 - t) + q[(size_t) (H + i + 1)] * t;
            }
            qh[(size_t) h] = v;
        }

        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int h = 1; h < 1024; ++h)
        {
            if (qh[(size_t) h] < 1.0) continue;
            const double a = std::pow (10.0, (qh[(size_t) h] - 96.0) / 20.0);
            const float re = (float) (a * std::cos (ph[(size_t) h]));
            const float im = (float) (a * std::sin (ph[(size_t) h]));
            buf[(size_t) (2 * h)] = re;
            buf[(size_t) (2 * h + 1)] = im;
            const int m = kFrameSize - h;
            buf[(size_t) (2 * m)] = re;
            buf[(size_t) (2 * m + 1)] = -im;
        }
        fft.performRealOnlyInverseTransform (buf.data());
        auto& fr = frames[(size_t) f];
        for (int i = 0; i < kFrameSize; ++i)
        {
            fr[(size_t) i] = buf[(size_t) i];
            sumSq += (double) buf[(size_t) i] * buf[(size_t) i];
        }
    }

    // the table's level: its measured RMS, kept under full scale
    const double rms = std::sqrt (sumSq / ((double) F * kFrameSize)) + 1.0e-12;
    double g = rmsTarget / rms, peak = 0.0;
    for (auto& fr : frames)
        for (float v : fr) peak = juce::jmax (peak, std::abs ((double) v) * g);
    if (peak > 0.999) g *= 0.999 / peak;
    for (auto& fr : frames)
        for (auto& v : fr) v = (float) (v * g);
    return true;
}
}
