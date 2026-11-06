#pragma once
#include "PluginProcessor.h"
#include "CgSampleView.h"

namespace covegroove
{
    // the look: a walnut case and warm brown faceplates (the soil), steel
    // knobs on knurled skirts, square-cornered keys and steps, and saturated
    // sea colours on them - a blue, cyan or green for each lane, cyan and
    // green lamps
    namespace colours
    {
        const juce::Colour wood     { 0xff221a15 };
        const juce::Colour plate    { 0xff3b3028 };
        const juce::Colour plateHi  { 0xff4a3d33 };
        const juce::Colour slot     { 0xff1b1511 };
        const juce::Colour edge     { 0xff15100d };
        const juce::Colour text     { 0xffeee2c9 };
        const juce::Colour dim      { 0xffb2a189 };
        const juce::Colour cyan     { 0xff1ad6ec };
        const juce::Colour green    { 0xff35e08a };
        const juce::Colour blue     { 0xff3a8dff };
        const juce::Colour coral    { 0xffff5d4a };
        const juce::Colour cream    { 0xffeadcc0 };
        juce::Colour lane (int i);
    }

    class CgLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        CgLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int h) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    };

    // a cream knob with its name under it; double-click resets it
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& name, juce::Colour c);
        void resized() override;
        void paint (juce::Graphics&) override;
        juce::Slider slider;
    private:
        juce::String name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    // EDIT on a lane playing its own sample: a small key with a waveform on it
    class SampleEditButton : public juce::TextButton
    {
    public:
        SampleEditButton() : juce::TextButton ("EDIT") {}
        juce::Colour colour { colours::cyan };
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    // all eight lanes' steps; a step's bar is as tall as it is loud.
    //   tap an empty slot: on, as loud as the height you tap at
    //   tap a step: off;   drag up / down on a step: louder / softer
    //   drag across: paint (or clear) a row
    //   long-press (or right-click): the second note / accent
    class StepGrid : public juce::Component, private juce::Timer
    {
    public:
        explicit StepGrid (CoveGrooveProcessor& p);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        int rowHeight() const { return getHeight() / kNumLanes; }
    private:
        enum class Gesture { None, Undecided, Velocity, Paint };
        void timerCallback() override;
        bool cellAt (juce::Point<int> pos, int& lane, int& step) const;
        juce::Rectangle<float> cellRect (int lane, int step) const;
        juce::Rectangle<float> slotRect (int lane, int step) const;
        uint8_t velocityAt (int lane, float y) const;
        CoveGrooveProcessor& proc;
        Pattern shown;
        int shownStep { -2 }, shownVersion { -1 };

        Pattern editing;
        Gesture gesture { Gesture::None };
        int pressLane { -1 }, pressStep { -1 };
        bool pressWasOn { false }, paintOn { false }, longPressDone { false };
        uint8_t paintVel { 100 };
        juce::Point<int> pressPos;
        juce::uint32 pressTime { 0 };
        int showVelLane { -1 }, showVelStep { -1 };
    };

    // a lane's own sample, over the steps: LOAD a sound file, shape it with
    // the volume curve (or one of the ready shapes), PLAY it, CLOSE
    class SamplePanel : public juce::Component, private juce::Timer
    {
    public:
        explicit SamplePanel (CoveGrooveProcessor& p);
        void show (int lane);
        int getLane() const { return lane; }
        void paint (juce::Graphics&) override;
        void resized() override;
        void chooseFile();
    private:
        void timerCallback() override;
        void refresh();
        CoveGrooveProcessor& proc;
        int lane { 0 }, shownVersion { -1 }, shownModel { -1 };
        CurveEditor curveEditor;
        juce::TextButton loadButton { "LOAD" }, clearButton { "CLEAR" }, playButton { "PLAY" }, closeButton { "CLOSE" };
        std::array<juce::TextButton, samples::kNumShapes> shapeButtons;
        juce::String info;
        std::shared_ptr<juce::FileChooser> chooser;
    };

    class CoveGrooveEditor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit CoveGrooveEditor (CoveGrooveProcessor&);
        ~CoveGrooveEditor() override;
        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int kWidth = 1100, kHeight = 620;

    private:
        void timerCallback() override;
        void savePreset();
        void loadPreset();
        void fillPresetBox();
        void stepPreset (int delta);
        void showDiceMenu();
        void drawPlate (juce::Graphics&, juce::Rectangle<int>, bool recessed = false) const;
        void drawTitleStrip (juce::Graphics&, juce::Rectangle<int>) const;
        void makeTextures();

        CoveGrooveProcessor& proc;
        CgLookAndFeel lnf;
        using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

        // header
        juce::ComboBox presetBox, kitBox, clockBox, styleBox, barsBox;
        juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, loadButton { "LOAD" },
                         runButton { "RUN" }, generateButton { "GENERATE" }, randomButton { "RANDOM" };
        std::unique_ptr<Knob> tempo, density, variation, swing;

        // lanes
        struct LaneRow
        {
            juce::TextButton mute { "M" }, top { "TOP" };
            SampleEditButton edit;
            juce::TextButton rnd { "RND" };       // a new pattern for this lane
            juce::Label name;
            juce::ComboBox model;
            std::unique_ptr<Knob> level, tune, decay, tone, pan, send;
            std::unique_ptr<ButtonAtt> muteAtt, topAtt;
            std::unique_ptr<ComboAtt> modelAtt;
            int lastHits { 0 }, shownModel { -1 };
            float glow { 0.0f };
        };
        std::array<LaneRow, kNumLanes> lanes;
        StepGrid grid;
        SamplePanel samplePanel;

        // effects
        juce::ComboBox fxPresetBox;
        std::unique_ptr<Knob> phaser, phaserRate, filterFreq, filterReso, filterMotion, filterMix,
                              delayFeedback, delayTone, delayMix, drive, driveTone, driveMix, volume;
        juce::ComboBox filterRate, filterShape, delayTime, driveMode;
        juce::TextButton pingButton { "PING" };
        juce::Label fxLabels[5];

        std::unique_ptr<ComboAtt> clockAtt, styleAtt, barsAtt, filterRateAtt, filterShapeAtt, delayTimeAtt, driveModeAtt;
        std::unique_ptr<ButtonAtt> runAtt, pingAtt;
        std::shared_ptr<juce::FileChooser> chooser;
        juce::Rectangle<int> headerArea, laneArea, fxArea, titleArea, laneStrip, fxStrip;
        std::array<juce::Rectangle<int>, kNumLanes> bands, lamps;     // each lane's strip and its lamp
        juce::Image woodImage, grainImage;
        int shownProgram { -2 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoveGrooveEditor)
    };
}
