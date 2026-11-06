#pragma once

#include <JuceHeader.h>

class AliasSynthAudioProcessor : public juce::AudioProcessor
{
public:
    // --- 1. Construction & Destruction ---
    AliasSynthAudioProcessor();
    ~AliasSynthAudioProcessor() override;

    // --- 2. Audio & MIDI Lifecycle ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- 3. Editor & UI ---
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // --- 4. DAW Information Overrides ---
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // --- 5. Programs (Presets) ---
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // --- 6. State Management (Persistence) ---
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- 7. Public Custom Logic & Data ---
    void loadSample();

    // Aquanode Modular: loading a file (also used by the patch version), and
    // the sample as one shared buffer that the voices pick up at the start of
    // a block - swapped under a lock, so loading never pulls the buffer out
    // from under a playing voice
    bool loadSampleFile (const juce::File& file);
    void setSample (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double rate);
    std::shared_ptr<const juce::AudioBuffer<float>> getSample (double& rate) const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> isSampleLoaded{ false };

private:
    std::shared_ptr<const juce::AudioBuffer<float>> sampleData;
    double sampleSourceRate = 44100.0;
    mutable juce::SpinLock sampleLock;

    // --- 8. Private Internal Components ---
    juce::Synthesiser synth;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AliasSynthAudioProcessor)
};