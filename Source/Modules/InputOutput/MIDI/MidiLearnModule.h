#pragma once

#include "../../Core/ModuleCore.h"

// Midi Learn - one knob, fader, wheel or pedal of a MIDI controller as a
// cable into any knob. Tap Learn (or the little screen) and move the control:
// it is learned - a CC, the pitch bend or the pressure - and the screen shows
// it moving. Patch Mod Out into knobs of any modules: like a Macro, each one
// follows the control across its WHOLE range (the control at 0 is the knob's
// minimum, at 127 its maximum), the cable's depth blending between the knob's
// own setting and the control, a negative depth turning it the other way.
// Low and High narrow the range the control sweeps (High below Low turns it
// round); Smooth takes the steps out of a 7-bit controller.
//
// Until the learned control first moves, the knobs keep their own settings -
// nothing jumps when the cable goes in. After that its last position is saved
// with the patch, so a patch opens sounding as it was left. Any number of Midi
// Learns: one per control. The Control menu picks a CC by hand as well.
//
// Works with any MIDI input the app hears - a keyboard or controller plugged
// into an Android phone or tablet, or the host's MIDI in a DAW.
//
// Outputs: 0 = Mod Out (0..1, absolute on knobs). Global lane.
class MidiLearnModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pLearn = 0, pSource, pSmooth, pLow, pHigh, pValue, pActive };
    // pSource: 0 none, 1..128 CC 0..127, 129 pitch bend, 130 pressure
    static constexpr int kNumSources = 131;
    static juce::String sourceName (int source, bool shortForm = false);

    bool drivesKnobsAbsolutely() const override { return true; }
    bool drivesKnobsNow() const override { return driving; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }
    void reset() override { started = false; }

    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override;

    void uiButtonClicked (const juce::String& paramId) override;
    juce::String titleSuffix() const override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 40; }

    //=== for the card (message thread) ========================================
    bool isLearning() const { return learnState.load (std::memory_order_relaxed) != 0; }
    void toggleLearning();
    float shownValue() const { return uiValue.load (std::memory_order_relaxed); }   // 0..1, the control
    bool shownActive() const { return uiActive.load (std::memory_order_relaxed); }
    int learnCount() const { return learned.load (std::memory_order_relaxed); }

private:
    float controlValue (int source) const;   // 0..1 (the bend centred at 0.5); -1 = none

    // 0 idle, 1 asked for (the card), 2 listening (the audio thread took a baseline)
    std::atomic<int> learnState { 0 };
    std::atomic<int> learned { 0 };            // goes up with every control learned
    std::atomic<float> uiValue { 0.0f };
    std::atomic<bool> uiActive { false };

    //=== audio thread =========================================================
    bool started { false }, driving { false };
    int lastSource { -1 };
    float lastSeen { -1.0f };                  // the control's state as last looked at
    float smoothed { 0.0f };
    juce::uint32 baseline { 0 };
};
