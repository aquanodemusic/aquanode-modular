#pragma once
#include "K5SynthVoice.h"
#include "PresetManager.h"
#include <juce_audio_processors/juce_audio_processors.h>

class K5AudioProcessor : public juce::AudioProcessor,
                         public aquanode::MpeReceiver,     // (Aquanode: MPE once a member channel plays)
                         private juce::AudioProcessorValueTreeState::Listener
{
public:
    void enableMpe (bool on) override { mpeState.on = on; }
    K5MpeState mpeState;
    K5AudioProcessor();
    ~K5AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Additive Keys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // Longest possible tail: amp release up to 8s, pitch/filter release up to
    // 5s, plus a little slack.
    double getTailLengthSeconds() const override { return 9.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createLayout() };

    //==========================================================================
    // The harmonics as drawn: per source, each harmonic's level (0..1) and the
    // envelope bus (0..3) it follows. Tilt still slopes them (level x n^tilt),
    // so the defaults - 1/n on bus n % 4 - sound exactly like the old tilt-only
    // harmonics. Written by the panel (message thread), read on the next block.
    static constexpr int numHarm = K5Constants::numHarmonics;
    struct HarmonicData
    {
        std::array<std::atomic<float>, numHarm> level;
        std::array<std::atomic<int>, numHarm> group;
    };
    std::array<HarmonicData, 2> harmonics;
    void setHarmonic (int source, int h, float level, int group);
    void resetHarmonics (int source);
    juce::ValueTree harmonicsToTree() const;
    void harmonicsFromTree (const juce::ValueTree& t);   // invalid tree: defaults
    void markDirty() { parametersDirty.store (true, std::memory_order_release); }

    PresetManager presetManager { *this };

    // the preset maker: a new sound of one kind (PresetMaker.h), one of the
    // twenty factory sounds, each brought to the same loudness
    void rollPreset (int kind, juce::uint32 seed);
    void loadFactory (int index);
    void matchLoudness();
    static juce::StringArray presetKinds();
    static juce::StringArray factoryNames();

    // the sound's name, shown on the panel
    juce::String soundName { "Init" };
    std::atomic<int> soundNameVersion { 0 };
    void setSoundName (const juce::String& n) { soundName = n; ++soundNameVersion; }

private:
    static constexpr int numVoices = 16;

    //==========================================================================
    /*  Cached raw pointers into the APVTS.

        getRawParameterValue() does a string-keyed lookup, and the previous
        version called it ~120 times per source per voice per block. The
        lookup is the expensive part; the atomic load itself is free. So we
        resolve every pointer once, here.
    */
    struct SourceParamPtrs
    {
        std::atomic<float>* level      = nullptr;
        std::atomic<float>* detune     = nullptr;

        std::atomic<float>* pitchA     = nullptr;
        std::atomic<float>* pitchD     = nullptr;
        std::atomic<float>* pitchS     = nullptr;
        std::atomic<float>* pitchR     = nullptr;
        std::atomic<float>* pitchDepth = nullptr;

        std::atomic<float>* ampA       = nullptr;
        std::atomic<float>* ampD       = nullptr;
        std::atomic<float>* ampS       = nullptr;
        std::atomic<float>* ampR       = nullptr;

        std::atomic<float>* cutoff        = nullptr;
        std::atomic<float>* resonance     = nullptr;
        std::atomic<float>* filtEnvAmount = nullptr;
        std::atomic<float>* slope24       = nullptr;
        std::atomic<float>* filtA         = nullptr;
        std::atomic<float>* filtD         = nullptr;
        std::atomic<float>* filtS         = nullptr;
        std::atomic<float>* filtR         = nullptr;

        std::atomic<float>* harmMode = nullptr;
        std::atomic<float>* harmTilt = nullptr;

        struct GroupPtrs
        {
            std::atomic<float>* a = nullptr;
            std::atomic<float>* d = nullptr;
            std::atomic<float>* s = nullptr;
            std::atomic<float>* r = nullptr;
        };

        std::array<GroupPtrs, (size_t) HarmonicGenerator::numEnvGroups> group;
    };

    struct GlobalParamPtrs
    {
        std::atomic<float>* masterGain  = nullptr;
        std::atomic<float>* lfoShape    = nullptr;
        std::atomic<float>* lfoRate     = nullptr;
        std::atomic<float>* lfoDelay    = nullptr;
        std::atomic<float>* lfoVibrato  = nullptr;
        std::atomic<float>* lfoTremolo  = nullptr;
        std::atomic<float>* lfoFilterMod = nullptr;

        struct BandPtrs
        {
            std::atomic<float>* gain = nullptr;
            std::atomic<float>* freq = nullptr;
            std::atomic<float>* q    = nullptr;
        };

        std::array<BandPtrs, (size_t) DigitalFormantFilter::numBands> band;
    };

    void cacheParameterPointers();
    void applyParametersToVoices();
    void updateSharedDFT();
    void updateSource (K5Source& source, const SourceParamPtrs& p);
    void updateVoiceGlobals (K5Voice& voice);

    void parameterChanged (const juce::String&, float) override
    {
        parametersDirty.store (true, std::memory_order_release);
    }

    std::array<SourceParamPtrs, 2> sourceParams;
    GlobalParamPtrs globalParams;

    std::atomic<bool> parametersDirty { true };

    juce::Synthesiser synth;
    DigitalFormantFilter sharedDFT;
    juce::SmoothedValue<float> masterGain { 0.7f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (K5AudioProcessor)
};
