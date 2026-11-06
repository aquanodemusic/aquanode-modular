#pragma once
//==============================================================================
// Tidebreeze - the editor
//
// A brushed-silver frame with the four macro knobs, a CRT preset display,
// ROLL / LOAD / SAVE and the layer keys on top; below it a cyan CRT screen
// (glass, phosphor glow, scanlines) with five pages - PLAY (everything at a
// glance), LAYER (waves, pitch, filter, amp of one layer), MOD (its
// envelopes and LFOs), EFFECTS and WAVES. Tapping the preset name lists the
// factory presets by category; ROLL (and the Tidebreeze logo) rolls a random
// preset of a category or a Super Random one; the WAVES page rolls new waves
// and loads audio files as waves into the user slots.
//==============================================================================
#include "PluginProcessor.h"

namespace tbui
{
    struct Palette
    {
        // the brushed-silver frame
        static juce::Colour frameTop()    { return juce::Colour (0xffd5dee2); }
        static juce::Colour frameBottom() { return juce::Colour (0xff9fb2ba); }
        static juce::Colour frameLine()   { return juce::Colour (0xff60757f); }
        static juce::Colour navy()        { return juce::Colour (0xff13304a); }   // lettering on the frame
        static juce::Colour cyan()        { return juce::Colour (0xff3fe0f5); }   // lit keys
        static juce::Colour cyanDeep()    { return juce::Colour (0xff1593b0); }
        // the CRT: glass and phosphor
        static juce::Colour glassCentre() { return juce::Colour (0xff2bbfdd); }
        static juce::Colour glassEdge()   { return juce::Colour (0xff0b6884); }
        static juce::Colour phBright()    { return juce::Colour (0xfff2feff); }
        static juce::Colour phMid()       { return juce::Colour (0xffc4f6ff); }
        static juce::Colour phDim()       { return juce::Colour (0xff8fe3f2); }
        static juce::Colour well()        { return juce::Colour (0x34002a38); }   // panels, sunk into the glass
        static juce::Colour inset()       { return juce::Colour (0x66002430); }   // graphs, fields
    };

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
        juce::Font getPopupMenuFont() override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
        static juce::Font font (float size, bool bold = false);   // the frame's lettering
        static juce::Font crt (float size, bool bold = false);    // the screens' monospaced phosphor
    };

    // text with a soft phosphor bloom
    void glowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, juce::Colour, float intensity = 1.0f);
}

class TidebreezeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit TidebreezeAudioProcessorEditor (TidebreezeAudioProcessor&);
    ~TidebreezeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // the pages
    enum Page { PagePlay, PageLayer, PageMod, PageEffects, PageWaves, NumPages };
    void showPage (int page);
    void showOsc (int layer);           // 0 = layer A, 1 = layer B
    void showEnvelope (int which);      // MOD page: 0 filter, 1 amp, 2 pitch
    void showWave (int choice) { waveFocus = choice; showPage (PageWaves); }   // the WAVES page on a wave

    static constexpr int kWidth = 1100, kHeight = 620;

private:
    void timerCallback() override;
    void buildPage();
    void showProgramMenu();
    void showRandomMenu();
    void stepProgram (int delta);
    void savePreset();
    void loadPreset();
    void loadWaveFile (int slot);       // WAVES page: an audio file into a user slot (-1: the next free one)
    void paintBezel (juce::Graphics&, juce::Rectangle<float> glass, float radius);
    void paintGlass (juce::Graphics&, juce::Rectangle<float> glass, float radius);
    juce::Image makeTubeOverlay (juce::Rectangle<int> glass, float radius);

    juce::Rectangle<int> logoArea() const;
    juce::Rectangle<int> layerSummaryArea (int layer) const;   // a layer in the screen's left column (a tap edits it)
    static constexpr int rightX = 804, rightW = 280;          // the preset keys, voice keys and performance knobs
    static constexpr int navY = 161;                           // the layer keys and page tabs
    juce::Rectangle<int> displayArea() const;
    juce::Rectangle<int> screenArea() const;
    juce::Rectangle<int> pageArea() const;

    TidebreezeAudioProcessor& proc;
    tbui::LookAndFeel laf;
    juce::Image displayOverlay, screenOverlay;    // scanlines, vignette and reflection over the tubes

    // the frame's controls
    std::vector<std::unique_ptr<juce::Component>> top;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> keyAttachments;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, diceButton { "ROLL" }, loadButton { "LOAD" }, saveButton { "SAVE" };
    juce::TextButton tabs[NumPages];
    juce::TextButton oscTabs[2];
    int page = PagePlay, oscEdit = 0, envEdit = 0;

    // the current page's controls (rebuilt on page / layer change)
    std::unique_ptr<juce::Component> pageComp;
    std::unique_ptr<juce::FileChooser> chooser;
    int waveFocus = 0;     // WAVES page: the wave shown
    int lastArchetype = 0; // WAVES page: the archetype ROLL WAVE rolls
    int lastFxTypes = -1;  // the effect types and layer count the page was built for

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidebreezeAudioProcessorEditor)
};
