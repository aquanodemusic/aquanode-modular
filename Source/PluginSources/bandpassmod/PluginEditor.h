#pragma once
#include "PluginProcessor.h"

// The panel, after the FL Studio Patcher original: a dark grey face, knobs
// ringed with teal dots, section titles over a teal rule - plus a strip
// showing where the band is, and Note Lock.
class BandpassModulatorAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BandpassModulatorAudioProcessorEditor (BandpassModulatorAudioProcessor&);
    ~BandpassModulatorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct LNF : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float start, float end, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::String name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void timerCallback() override;
    Knob& knob (const juce::String& id);
    juce::Rectangle<int> graphArea() const;

    BandpassModulatorAudioProcessor& proc;
    LNF lnf;
    std::map<juce::String, std::unique_ptr<Knob>> knobs;
    juce::ToggleButton noteLock { "Note Lock" }, midiLock { "Midi In" };
    juce::ToggleButton noteButtons[12];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAtt, midiAtt, noteAtt[12];
    juce::Slider seed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> seedAtt;
    float colourPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandpassModulatorAudioProcessorEditor)
};
