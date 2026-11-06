#pragma once

#include "../../Core/ModuleCore.h"

// Tuner - shows the pitch of what comes into Audio In: the note, how many
// cents it is off and the frequency, with a needle. The audio passes through
// unchanged (Audio Out), so it can sit anywhere in a chain. Reads against
// A4 = the A4 knob in equal temperament, or - when an MTS-ESP master is
// connected - against the master's nearest key. Needs a single steady note;
// 25 Hz to 4 kHz. Global lane (a chord or several voices summed has no one
// pitch). The pitch is found in the card (YIN, about 15 times a second), not
// on the audio thread.
class TunerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pA4 = 0 };
    static constexpr int kRing = 8192;

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        for (auto& x : ring) x.store (0.0f, std::memory_order_relaxed);
        writePos.store (0, std::memory_order_relaxed);

    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        outputs[0] = inputs[0];
        // into the ring the card reads
        const int w = writePos.load (std::memory_order_relaxed);
        ring[(size_t) w].store (0.5f * (inputs[0][0] + inputs[0][1]), std::memory_order_relaxed);
        writePos.store ((w + 1) % kRing, std::memory_order_release);
    }

    // the latest n samples, oldest first (message thread)
    void latest (float* dest, int n) const
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = ring[(size_t) ((w - n + i + 2 * kRing) % kRing)].load (std::memory_order_relaxed);
    }
    double ringRate() const { return sampleRate; }

    // the pitch of x (YIN), 0 when there is none; clarity 0..1 (message thread)
    static double detectPitch (const float* x, int n, double rate, double& clarity);

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 118; }

private:
    std::array<std::atomic<float>, kRing> ring {};
    std::atomic<int> writePos { 0 };
};
