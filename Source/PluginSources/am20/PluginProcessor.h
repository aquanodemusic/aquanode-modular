#pragma once
// AM-20: an MS-20-style polysynth for Aquanode Modular, modelled from
// measurements of the original (see MEASUREMENTS.md).
#include <JuceHeader.h>
#include "Engine.h"
#include "PresetIO.h"

class AM20Processor : public juce::AudioProcessor,
                      public aquanode::MpeReceiver     // (Aquanode: MPE once a member channel plays)
{
public:
    void enableMpe (bool on) override { engine.mpe = on; }
    AM20Processor();
    ~AM20Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AM-20"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return juce::jmax (1, (int) bank.size()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- the sound ----
    am20::Preset currentPreset() const;                 // knobs, cables and name as they are now
    void applyPreset (const am20::Preset& p);           // message thread
    juce::String presetName { "Init" };
    // the effects of a program loaded from the original's .fxp / .fxb: AM-20
    // plays without them but keeps them for exporting there again ("" = none)
    juce::String effectsKept() const { return juce::String (am20::presetio::fxDescription (currentPreset())); }

    // ---- patch cables (message thread; the audio thread picks them up) ----
    const std::vector<std::pair<int, int>>& getCables() const { return cables; }
    bool addCable (int a, int b, juce::String& why);    // false + reason when the jacks cannot be joined
    void removeCable (int index);
    void clearCables();
    std::function<void()> onChange;                     // the editor's refresh (presets, cables)

    // ---- files ----
    // .am20 (AM-20's own XML, a preset or a bank), the original's .fxp / .fxb
    bool loadPresetData (const juce::MemoryBlock& data, const juce::String& fileName, juce::String& error);
    juce::MemoryBlock presetAsAm20() const;
    juce::MemoryBlock bankAsAm20() const;
    juce::MemoryBlock presetAsFxp() const;
    juce::MemoryBlock bankAsFxb() const;
    juce::File lastPresetFile;

    // the bank shown by the preset selector: the built-in sounds, or a loaded bank
    std::vector<am20::Preset> bank;
    juce::String bankName { "AM-20" };
    void loadFactoryBank();

    // ---- performance controls from the editor ----
    std::atomic<bool> momentary { false };
    std::atomic<float> guiWheel { -1.0f };              // 0..1 while the on-screen wheel is held, else -1
    // 2x oversampling (the measured sound); off is lighter on phones
   #if JUCE_ANDROID
    std::atomic<bool> highQuality { false };
   #else
    std::atomic<bool> highQuality { true };
   #endif

    juce::AudioProcessorValueTreeState apvts;
    float uiScale = 1.0f;

    static juce::String pid (int i) { return am20::paramId (i); }

private:
    am20::Engine engine;
    std::array<std::atomic<float>*, am20::NumParams> raw {};
    std::vector<am20::MidiEv> events;
    std::vector<std::pair<int, int>> cables;
    juce::SpinLock cableLock;
    std::vector<std::pair<int, int>> cablesForAudio;
    std::atomic<int> cableVersion { 0 };
    int cableVersionSeen = -1;
    int currentProgram = 0;
    std::vector<uint8_t> currentFx;                     // see effectsKept()
    float lastGuiWheel = -1.0f;

    void cablesChanged();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    // withFx: the host's session state keeps a loaded program's effects;
    // .am20 files never do (AM-20 has no effects)
    static std::unique_ptr<juce::XmlElement> presetToXml (const am20::Preset& p, bool withFx = false);
    static bool xmlToPreset (const juce::XmlElement& e, am20::Preset& p, bool withFx = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AM20Processor)
};
