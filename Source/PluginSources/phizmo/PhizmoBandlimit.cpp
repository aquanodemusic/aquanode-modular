/*
    PhizmoBandlimit.cpp
    -------------------
    An optional second oscillator engine for Phizmo.

    The existing engine reads the raw frames with bilinear interpolation and no
    band-limiting. On a bright table that aliases above roughly C4 - which is
    arguably correct, since the hardware's sample playback chip does linear
    interpolation and nothing else, and the GRIT control leans into exactly that
    character. This file does NOT replace it. It adds a switchable alternative
    for when you want the same table to stay clean at the top of the keyboard.

    How it works: each frame is analysed once into a mip pyramid. Mip 0 keeps
    1024 harmonics, mip 1 keeps 512, and so on. At playback the mip whose top
    harmonic still fits under Nyquist for the note being played is selected.
    Cost is about 2 MB per slot for a 64-frame table, built once on load.

    Nothing else changes: same frames, same evolution curves, same visualiser.
*/

#include "PluginProcessor.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <mutex>

namespace
{
    // These mirror PhizmoAudioProcessor::MipSet, which is private, so they are
    // repeated here rather than referenced. Keep the two in step.
    constexpr int kMipCount   = 10;
    constexpr int kBaseLength = 2048;
    constexpr int kBaseHarms  = 1024;

    int mipLength (int m) { return juce::jmax (64, kBaseLength >> m); }
    int mipHarms  (int m) { return juce::jmax (1,  kBaseHarms  >> m); }

    // (Aquanode) The pyramids of the last few tables, shared by every Phizmo in the
    // process: a patch version holds one Phizmo per Sound, each loading the same
    // tables, and a pyramid depends on nothing but the frames and the cycle size.
    // An entry is used only for exactly the same frames (compared in full).
    using Levels = std::vector<std::array<std::vector<float>, kMipCount>>;
    struct CachedPyramid
    {
        std::vector<std::vector<float>> frames;
        int cycleSamples = 0;
        std::shared_ptr<const Levels> levels;
    };
    std::mutex pyramidMutex;
    std::vector<CachedPyramid> pyramidCache;   // newest last
    constexpr size_t kCachedPyramids = 8;   // one preset's eight tables

    std::shared_ptr<const Levels> findPyramid (const std::vector<std::vector<float>>& frames, int cycleSamples)
    {
        const std::lock_guard<std::mutex> lock (pyramidMutex);
        for (auto it = pyramidCache.begin(); it != pyramidCache.end(); ++it)
            if (it->cycleSamples == cycleSamples && it->frames == frames)
            {
                auto found = *it;
                pyramidCache.erase (it);
                pyramidCache.push_back (found);
                return found.levels;
            }
        return nullptr;
    }

    // the band-limited pyramid of a table: one FFT per frame, one inverse per mip
    Levels buildLevels (const std::vector<std::vector<float>>& frames, int cycleSamples)
    {
        const int numFrames = (int) frames.size();
        Levels built ((size_t) numFrames);

        juce::dsp::FFT fft (11);   // 2048
        std::vector<float> work ((size_t) (2 * kBaseLength));
        std::vector<float> spec ((size_t) (2 * kBaseLength));

        for (int f = 0; f < numFrames; ++f)
        {
            // Resample the cycle to 2048 so one FFT size covers every cycle length
            // the user might have dialled in.
            std::fill (work.begin(), work.end(), 0.0f);
            const auto& src = frames[(size_t) f];
            for (int i = 0; i < kBaseLength; ++i)
            {
                const double p = (double) i * cycleSamples / (double) kBaseLength;
                const int i0 = (int) p % cycleSamples;
                const int i1 = (i0 + 1) % cycleSamples;
                const float fr = (float) (p - std::floor (p));
                work[(size_t) i] = src[(size_t) i0] * (1.0f - fr) + src[(size_t) i1] * fr;
            }

            fft.performRealOnlyForwardTransform (work.data());

            for (int m = 0; m < kMipCount; ++m)
            {
                const int H = juce::jmin (mipHarms (m), kBaseLength / 2 - 1);
                const int len = mipLength (m);

                std::fill (spec.begin(), spec.end(), 0.0f);
                for (int h = 1; h <= H; ++h)
                {
                    const float re = work[(size_t) (2 * h)];
                    const float im = work[(size_t) (2 * h) + 1];
                    spec[(size_t) (2 * h)]     = re;
                    spec[(size_t) (2 * h) + 1] = im;
                    const int mirror = kBaseLength - h;
                    spec[(size_t) (2 * mirror)]     =  re;
                    spec[(size_t) (2 * mirror) + 1] = -im;
                }

                fft.performRealOnlyInverseTransform (spec.data());

                const int stride = kBaseLength / len;
                auto& level = built[(size_t) f][(size_t) m];
                level.resize ((size_t) len + 1);
                for (int i = 0; i < len; ++i)
                    level[(size_t) i] = spec[(size_t) (i * stride)];
                level[(size_t) len] = level[0];   // guard point for interpolation
            }
        }

        return built;
    }

    void keepPyramid (const std::vector<std::vector<float>>& frames, int cycleSamples, std::shared_ptr<const Levels> levels)
    {
        const std::lock_guard<std::mutex> lock (pyramidMutex);
        pyramidCache.push_back ({ frames, cycleSamples, std::move (levels) });
        if (pyramidCache.size() > kCachedPyramids)
            pyramidCache.erase (pyramidCache.begin());
    }
}

//==============================================================================
void PhizmoAudioProcessor::buildMips (int slot)
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) return;

    // Mark unavailable first: any voice that starts while the rebuild is in
    // flight takes the vintage path instead of touching half-built levels.
    mips[slot].ready.store(false);

    // Snapshot the frames under the wavetable lock, then do the heavy lifting
    // outside it so a long analysis never stalls the audio thread.
    std::vector<std::vector<float>> frames;
    int cycleSamples = 0;
    {
        juce::ScopedLock sl (wt[slot].lock);
        if (! wt[slot].loaded || wt[slot].frames.empty()) { clearMips (slot); return; }
        frames = wt[slot].frames;
        cycleSamples = wt[slot].cycleSamples;
    }

    const int numFrames = (int) frames.size();
    if (auto cached = findPyramid (frames, cycleSamples))
    {
        std::shared_ptr<const Levels> previous;   // let go of outside the lock
        {
            const juce::SpinLock::ScopedLockType sl (mips[slot].swapLock);
            previous = std::move (mips[slot].levels);
            mips[slot].levels = std::move (cached);
            mips[slot].numFrames = numFrames;
        }
        mips[slot].ready.store (true);
        return;
    }
    auto shared = std::make_shared<const Levels> (buildLevels (frames, cycleSamples));
    keepPyramid (frames, cycleSamples, shared);
    std::shared_ptr<const Levels> previous;   // let go of outside the lock
    {
        const juce::SpinLock::ScopedLockType sl (mips[slot].swapLock);
        previous = std::move (mips[slot].levels);
        mips[slot].levels = std::move (shared);
        mips[slot].numFrames = numFrames;
    }
    mips[slot].ready.store (true);
}

void PhizmoAudioProcessor::clearMips (int slot)
{
    if (slot < 0 || slot >= NUM_SOUNDS * 2) return;
    mips[slot].ready.store (false);
    std::shared_ptr<const Levels> previous;   // let go of outside the lock
    const juce::SpinLock::ScopedLockType sl (mips[slot].swapLock);
    previous = std::move (mips[slot].levels);
    mips[slot].numFrames = 0;
}

//==============================================================================
bool PhizmoAudioProcessor::sampleFrameMip (int slot, float fi, double ph, double freqHz,
                                           float& out) const
{
    const auto& ms = mips[slot];
    if (! ms.ready.load()) return false;

    // Try, never block. If a rebuild owns the lock this instant, the caller
    // falls back to the vintage read for this one sample.
    const juce::SpinLock::ScopedTryLockType sl (ms.swapLock);
    if (! sl.isLocked() || ms.numFrames <= 0 || ms.levels == nullptr) return false;

    // Pick the coarsest mip whose top harmonic still fits under Nyquist.
    const double allowed = (currentSampleRate * 0.5) / juce::jmax (1.0, freqHz);
    int m = kMipCount - 1;
    for (int i = 0; i < kMipCount; ++i)
        if ((double) mipHarms (i) <= allowed) { m = i; break; }

    const int i0 = juce::jlimit (0, ms.numFrames - 1, (int) fi);
    const int i1 = juce::jlimit (0, ms.numFrames - 1, i0 + 1);
    const float blend = fi - (float) i0;

    const auto& a = (*ms.levels)[(size_t) i0][(size_t) m];
    const auto& b = (*ms.levels)[(size_t) i1][(size_t) m];
    if (a.empty() || b.empty()) return false;

    const int len = (int) a.size() - 1;
    const double pos = juce::jlimit (0.0, 0.999999, ph) * len;
    const int s0 = (int) pos;
    const float fr = (float) (pos - s0);

    const float va = a[(size_t) s0] + fr * (a[(size_t) (s0 + 1)] - a[(size_t) s0]);
    const float vb = b[(size_t) s0] + fr * (b[(size_t) (s0 + 1)] - b[(size_t) s0]);
    out = va + blend * (vb - va);
    return true;
}

//==============================================================================
// (Aquanode) the pyramid and its read for a table outside the processor (Phizmo - Osc)
std::shared_ptr<const PhizmoAudioProcessor::PyramidLevels>
PhizmoAudioProcessor::pyramidFor (const std::vector<std::vector<float>>& frames, int cycleSamples)
{
    static_assert (std::is_same<PyramidLevels, Levels>::value, "PyramidLevels must be MipSet's levels");
    if (frames.empty() || cycleSamples <= 0)
        return nullptr;
    if (auto cached = findPyramid (frames, cycleSamples))
        return cached;
    auto shared = std::make_shared<const Levels> (buildLevels (frames, cycleSamples));
    keepPyramid (frames, cycleSamples, shared);
    return shared;
}

bool PhizmoAudioProcessor::readPyramid (const PyramidLevels& levels, float fi, double ph, double freqHz,
                                        double sampleRate, float& out)
{
    const int numFrames = (int) levels.size();
    if (numFrames <= 0 || freqHz <= 0.0)
        return false;
    const double allowed = (sampleRate * 0.5) / juce::jmax (1.0, freqHz);
    int m = kMipCount - 1;
    for (int i = 0; i < kMipCount; ++i)
        if ((double) mipHarms (i) <= allowed) { m = i; break; }

    const int i0 = juce::jlimit (0, numFrames - 1, (int) fi);
    const int i1 = juce::jlimit (0, numFrames - 1, i0 + 1);
    const float blend = fi - (float) i0;
    const auto& a = levels[(size_t) i0][(size_t) m];
    const auto& b = levels[(size_t) i1][(size_t) m];
    if (a.empty() || b.empty()) return false;

    const int len = (int) a.size() - 1;
    const double pos = juce::jlimit (0.0, 0.999999, ph) * len;
    const int s0 = (int) pos;
    const float fr = (float) (pos - s0);
    const float va = a[(size_t) s0] + fr * (a[(size_t) (s0 + 1)] - a[(size_t) s0]);
    const float vb = b[(size_t) s0] + fr * (b[(size_t) (s0 + 1)] - b[(size_t) s0]);
    out = va + blend * (vb - va);
    return true;
}
