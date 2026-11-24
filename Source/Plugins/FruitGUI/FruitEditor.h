#pragma once
#include "FruitProcessor.h"

//==============================================================================
// Fruit DAW's panel: four FL Studio-style windows in a 2 x 2 grid (16:9) -
// the channel rack, the piano roll, the playlist and the mixer - and the
// cards of whatever is loaded, opened over them from a slot.
//==============================================================================
namespace fruitui
{
    // the green colourway, taken from FL Studio's
    const juce::Colour desk { 0xff6e9ea0 }, panel { 0xff628f88 }, titleBar { 0xff3b5f55 }, toolbar { 0xff34635b },
                       nameBtn { 0xff8ab2b2 }, stepOn { 0xff1fd5c9 }, stepOff { 0xff0e6d69 }, light { 0xffe1f5f3 },
                       grid { 0xff136358 }, gridAlt { 0xff12655b }, gridLine { 0xff1a6358 }, trackHead { 0xff406b61 },
                       patternList { 0xff12504d }, clipBody { 0xff1c554e }, clipHead { 0xff3c6a6a }, loopRed { 0xff955e61 },
                       mixerBg { 0xff254d45 }, strip { 0xff214847 }, faderCap { 0xffd5f4f7 }, selected { 0xff40f0ee },
                       slotPanel { 0xff59867f }, dimText { 0xff949da2 }, darkText { 0xff10302c };

    struct Window : public juce::Component     // a title bar and a body, like FL's windows
    {
        juce::String title;
        std::function<void (juce::Graphics&, juce::Rectangle<int>)> paintTitleExtras;
        static constexpr int kTitleH = 30;
        juce::Rectangle<int> body() const { return getLocalBounds().withTrimmedTop (kTitleH); }
        juce::Rectangle<int> titleArea() const { return getLocalBounds().withHeight (kTitleH); }
        void paint (juce::Graphics&) override;
    };

    // a number you set by dragging up/down or tapping the arrows, 01..99
    struct NumberBox : public juce::Component
    {
        int value = 1, lo = 1, hi = 99;
        juce::String caption;
        std::function<void (int)> onChange;
        void set (int v, bool notify) { v = juce::jlimit (lo, hi, v); if (v == value) return; value = v; repaint(); if (notify && onChange) onChange (v); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        int startValue = 1;
        bool dragged = false;
    };

    // gentle shading: a raised surface (lighter above, a highlight on its top edge) or a
    // sunken one (darker above, a shadow under its top edge)
    void raised (juce::Graphics&, juce::Rectangle<float>, float corner, juce::Colour base, float amount = 1.0f);
    void sunken (juce::Graphics&, juce::Rectangle<float>, float corner, juce::Colour base, float amount = 1.0f);

    struct Button : public juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };
}

class FruitEditor;

//==============================================================================
// the card of a loaded synth or effect: its knobs (a native module's) or its
// own panel (a plugin card's), opened over the windows from its slot
class FruitSlotCard : public juce::Component, private juce::Timer
{
public:
    FruitSlotCard (FruitProcessor& p, int moduleId, const juce::String& title);
    ~FruitSlotCard() override;
    int getModuleId() const { return moduleId; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void()> onClose;
private:
    void timerCallback() override;
    bool updateShown();
    FruitProcessor& proc;
    int moduleId;
    juce::String title;
    struct Control
    {
        int index = -1;
        int visIndex = -1;          // shown only while this parameter equals visEquals (visibleWhen)
        float visEquals = 0.0f;
        bool visFrom = false;      // (shown from visEquals on, not only at it)
        bool shown = true;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::Label> label;
    };
    std::vector<std::unique_ptr<Control>> controls;
    std::unique_ptr<juce::Component> content;
    juce::TextButton closeButton { "x" };
    juce::ComponentDragger dragger;
    int knobRows = 0, contentW = 0, contentH = 0;
    static constexpr int kTitleH = 28, kKnob = 78, kKnobH = 112, kCols = 6;
};

//==============================================================================
class FruitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    // part All: the four windows in one panel. One part: that window alone, for
    // the patch version's four cards (they share the processor's view state).
    enum Part { All = 0, RackPart, RollPart, PlaylistPart, MixerPart };
    explicit FruitEditor (FruitProcessor&, int part = All);
    static juce::Point<int> partSize (int part);
    ~FruitEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    FruitProcessor& proc;
    const int part;
    int& currentPattern;      // shared by every panel of this Fruit DAW (FruitProcessor::view)
    int& selectedInsert;
    int& selectedTrack;
    void openCard (int moduleId, const juce::String& title);   // or close it, when it is open
    void closeCards (int moduleId);
    void patternSelected (int p);
    void refreshAll();

    struct Rack;
    struct Roll;
    struct Playlist;
    struct Mixer;
private:
    void timerCallback() override;
    std::unique_ptr<Rack> rack;
    std::unique_ptr<Roll> roll;
    std::unique_ptr<Playlist> playlist;
    std::unique_ptr<Mixer> mixer;
    std::vector<std::unique_ptr<FruitSlotCard>> cards;
    int shownVersion = -1, scanTick = 0, shownView = -1;
    double tempoSeen = 0.0;
    int tempoStill = 0;
    juce::Component* cardParent();      // where slot cards open: this panel, or over the whole app for a single part
    std::unique_ptr<juce::TooltipWindow> tooltips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruitEditor)
};
