#pragma once

#include "../../Core/ModuleCore.h"

// Inspector - measures what any module does to the frequency response. Use
// two of them: the first as Generator in front of the module under test, the
// second as Analyzer after it, both on the same Link (A-D).
//
// The Generator plays a periodic test signal: a chirp whose spectrum is flat
// (every frequency at the same level), repeating every Size samples. It
// publishes where in that period it is, sample by sample, on its Link. The
// Analyzer files every sample it receives under the period position the
// Generator was at when it was sent - so after a whole period it holds one
// period of the response, perfectly lined up, whatever the module under test
// does in between. Divided by the test signal's spectrum that gives the
// transfer function: magnitude and phase. The module's latency appears as a
// delay; Auto finds it (the impulse response's peak) and takes it out of the
// phase, or set Latency by hand. Average smooths over several periods.
//
// Nonlinear modules measure differently at different Levels, and a response
// longer than Size (a long reverb) folds back onto itself - pick a bigger
// Size for those.
// Input: 0 = Audio In. Output: 0 = Audio Out (Generator: the test signal;
// Analyzer: what comes in, passed through).
class InspectorModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pMode = 0, pLink, pSize, pLevel, pLatency, pAuto, pAverage, pSmooth, pView };

    static constexpr int kNumLinks = 4;
    static constexpr int kMaxSize = 65536;

    static int sizeForChoice (int c) { return 8192 << juce::jlimit (0, 3, c); }
    static const std::vector<float>& testSignal (int size);   // the periodic flat-spectrum chirp, peak 1

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 460; }
    int extraContentHeight() const override { return 250; }

    //=== for the display (message thread) ======================================
    // the last averaged period, in period order; false when nothing new
    bool fetchResponse (std::vector<float>& dest, int& size, juce::uint32& version) const;
    bool isGenerator() const { return getParameterBase (pMode) < 0.5f; }
    bool generatorAlive() const;
    double getSampleRate() const { return sampleRate; }
    void noteMeasuredLatency (int samples) { measuredLatency.store (samples, std::memory_order_relaxed); }
    int getMeasuredLatency() const { return measuredLatency.load (std::memory_order_relaxed); }

    void uiButtonClicked (const juce::String& paramId) override;

private:
    // what a Generator tells its Analyzers, per Link
    struct LinkState
    {
        std::atomic<int> pos { -1 };
        std::atomic<int> size { 0 };
        std::atomic<juce::uint32> beat { 0 };   // goes up every sample while a Generator runs
    };
    static LinkState& link (int i);

    // Generator
    int genPos { 0 };
    int genSize { 0 };

    // Analyzer
    std::vector<float> ring, accum;
    int lastPos { -1 };
    int anSize { 0 };
    int periodsSeen { 0 }, averaged { 0 };
    juce::uint32 lastBeat { 0 };
    int quietSamples { 0 };
    std::atomic<bool> alive { false };

    mutable juce::SpinLock publishLock;
    std::vector<float> published;
    int publishedSize { 0 };
    std::atomic<juce::uint32> publishedVersion { 0 };
    std::atomic<int> measuredLatency { 0 };
};
