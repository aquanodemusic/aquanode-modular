#pragma once

#include "../../Core/ModuleCore.h"

// Convolver - the plain impulse-response player: load any IR file (a hall, a
// plate, a spring, a cabinet, a telephone, a strange sound) and Audio In is
// convolved with it, true stereo when the file is stereo. Nothing is forged or
// analysed (that is IRForge): the file is the IR, only trimmed and turned as
// the knobs say - Length (with a short fade at the cut), Reverse, Normalize
// (the IR brought to one level, so different files sit alike). Pre-Delay,
// Low / High Cut and Width work on the wet sound and take cables; Dry/Wet at
// 100 % is the convolution alone. The loaded IR is saved inside the patch.
//
// The convolution runs in 64-sample blocks (JUCE's non-uniform partitioned
// engine, which also matches the file's sample rate to the patch's), so the
// module is 64 samples late - dry and wet alike, reported to the host.
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
class ConvolverModule : public aquanode::SynthModule,
                        private juce::Thread
{
public:
    enum ParamIndex { pMix = 0, pGain, pPredelay, pLowCut, pHighCut, pWidth, pLength, pReverse, pNormalize };

    static constexpr int kBlock = 64;
    static constexpr double kMaxSeconds = 20.0;     // the longest IR used
    static constexpr int kOverview = 160;

    ConvolverModule();
    ~ConvolverModule() override;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    int latencySamples() const override { return kBlock; }

    // the loaded file's IR is what the patch keeps
    bool hasAudioToSave() const override;
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override;
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 300; }
    int extraContentHeight() const override { return 96; }

    //=== the card (message thread) ============================================
    void openChooser();
    void clearIR();
    void loadFileData (std::shared_ptr<juce::MemoryBlock> data, const juce::String& name);

    struct View
    {
        juce::String name;
        double seconds = 0.0;                 // as used (after Length)
        int channels = 0;
        std::array<float, kOverview> peak {}; // the shaped IR's envelope, 0..1
    };
    std::shared_ptr<const View> view() const;
    bool isLoading() const { return loading.load (std::memory_order_relaxed); }
    juce::String lastError() const;

private:
    void run() override;
    void decodePending();
    void shapeIfNeeded (bool force);
    void setOriginal (std::shared_ptr<const juce::AudioBuffer<float>> ir, double rate, const juce::String& name);

    mutable juce::SpinLock stateLock;
    std::shared_ptr<const juce::AudioBuffer<float>> original;   // as loaded (up to kMaxSeconds)
    double originalRate { 0.0 };
    juce::String irName;
    std::shared_ptr<const View> shapedView;
    juce::String errorText;
    std::atomic<int> originalGen { 0 };

    juce::CriticalSection loadLock;
    std::shared_ptr<juce::MemoryBlock> pendingData;
    juce::String pendingName;
    std::atomic<bool> loading { false };

    // what the IR in the convolver was shaped with
    int shapedGen { -1 };
    float shapedLength { -1.0f }, shapedReverse { -1.0f }, shapedNormalize { -1.0f };

    juce::CriticalSection convLock;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 1024 } };
    std::atomic<bool> irReady { false };

    float inBuf[2][kBlock] {}, outBuf[2][kBlock] {}, dlyBuf[2][kBlock] {};
    int blockPos { 0 };
    float fade { 0.0f }, fadeCoef { 0.001f }, smCoef { 0.001f };
    float smMix { 1.0f }, smGain { 1.0f };

    // the wet path: pre-delay, cuts, width
    std::vector<float> preLine[2];
    int prePos { 0 };
    struct OnePole { float z { 0 }; };
    float hpZ[2][2] {}, lpZ[2][2] {};   // two 1-pole sections each (12 dB / oct)

    std::unique_ptr<juce::FileChooser> chooser;
};
