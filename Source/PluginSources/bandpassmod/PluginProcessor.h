#pragma once
// AutoMorph Bandpass Modulator - a band-pass filter that jumps around a
// frequency range on its own, in time with the song.
//
// It moves the way the FL Studio Patcher version did: by default to random
// places, but "random" from a fixed, seeded generator - the classic shader
// hash fract(sin(n * 12.9898) * 43758.5453) of the step number - so the same
// bar always moves the same way, every time the song plays. Note Lock keeps
// the jumps on chosen notes (or on the notes held on Midi In).
#include <JuceHeader.h>
#include <array>
#include <vector>

class BandpassModulatorAudioProcessor : public juce::AudioProcessor
{
public:
    BandpassModulatorAudioProcessor();
    ~BandpassModulatorAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bandpass Modulator"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // the seeded generator: 0..1, always the same for the same n
    static float seededRandom (double n) noexcept
    {
        const double v = std::sin (n * 12.9898) * 43758.5453;
        return (float) (v - std::floor (v));
    }

    static juce::StringArray speedNames()   { return { "4 bars", "2 bars", "1 bar", "1/2", "1/4.", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; }
    static juce::StringArray delayNames()   { return { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1 bar" }; }
    static juce::StringArray modTypeNames() { return { "Random", "Smooth Random", "Sine", "Triangle", "Saw Up", "Saw Down", "Steps Up", "Steps Down" }; }

    // for the panel: where the band is now, and the range
    std::atomic<float> currentFreq { 1000.0f }, currentPan { 0.0f };
    std::atomic<int> heldPitchClassMask { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static double speedBeats (int index);
    static double delayBeats (int index);
    void rebuildNoteList (float lo, float hi, int mask);

    double sr = 44100.0;
    // filter state per channel (TPT state-variable band-pass)
    float ic1[2] {}, ic2[2] {};
    float logFreqSmoothed = std::log (1000.0f), panSmoothed = 0.0f;
    // own clock when no host transport runs
    double internalSteps = 0.0, internalSeconds = 0.0;
    double lastPpq = -1.0;
    // delay
    juce::AudioBuffer<float> delayBuf;
    int delayWrite = 0;
    float delaySmoothed = 0.0f;
    // note lock
    std::vector<float> notes;          // allowed note frequencies inside the range, ascending
    float notesLo = -1, notesHi = -1;
    int notesMask = -1;
    std::array<bool, 128> held {};

    std::atomic<float>* p[32] {};
    enum { pDry, pWet, pLow, pHigh, pFine, pSpeed, pType, pBw, pStrength, pDist, pDelSpeed, pDelVol,
           pAutopan, pSkewSpeed, pSkewAmt, pLock, pMidiLock, pSeed, pNote0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandpassModulatorAudioProcessor)
};
