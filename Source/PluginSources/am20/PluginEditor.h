#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// AM-20's window: the MS-20 front panel as plain, readable sections (no
// skeuomorphic artwork), and a second page for the patch panel, the external
// modulation, the ESP and the MG's sync.
class AM20Look : public juce::LookAndFeel_V4
{
public:
    AM20Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (14.0f); }
    juce::Font getPopupMenuFont() override { return font (15.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (14.0f, true); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    static juce::Font font (float h, bool bold = false);

    static inline const juce::Colour bg { 0xff1d1f22 }, panel { 0xff2a2d31 }, panelEdge { 0xff3a3e44 },
                                      text { 0xffeceff1 }, dim { 0xff9aa3ab }, accent { 0xffff8a3d }, accent2 { 0xff53c7d8 };
};

class AM20Editor;

// a knob with its caption above and its value below
class AmKnob : public juce::Component
{
public:
    AmKnob (AM20Processor& p, int param, const juce::String& caption);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    int param; juce::String caption;
    std::unique_ptr<juce::SliderParameterAttachment> att;
    AM20Processor& proc;
};

// a list for a stepped control
class AmChoice : public juce::Component
{
public:
    AmChoice (AM20Processor& p, int param, const juce::String& caption);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ComboBox box;
private:
    int param; juce::String caption;
    std::unique_ptr<juce::ParameterAttachment> att;
};

// a titled box holding controls in one row (or two)
class AmSection : public juce::Component
{
public:
    AmSection (const juce::String& title, juce::Colour colour) : title (title), colour (colour) {}
    void add (juce::Component* c, int row = 0) { items.push_back ({ c, row }); addAndMakeVisible (c); }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    juce::String title; juce::Colour colour;
    struct Item { juce::Component* c; int row; };
    std::vector<Item> items;
};

// the patch panel page's list of cables
class CableList : public juce::Component
{
public:
    explicit CableList (AM20Processor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int rowH = 30;
private:
    AM20Processor& proc;
};

class AM20Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AM20Editor (AM20Processor&);
    ~AM20Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int W = 1100, H = 720;
    AM20Processor& proc;
    AM20Look look;
    juce::Component content;      // laid out at W x H, scaled to the window

    // top bar
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, loadBtn { "Load" }, saveBtn { "Save" }, initBtn { "Init" }, hqBtn { "HQ" }, fxBtn { "+ FX kept" };
    juce::TextButton panelTab { "PANEL" }, patchTab { "PATCH + MOD" };
    juce::ComboBox presetBox;
    // performance strip
    juce::Slider wheel;
    juce::TextButton mswBtn { "MOMENTARY" };
    juce::Label wheelLabel;

    // pages
    juce::Component panelPage, patchPage;
    std::vector<std::unique_ptr<AmSection>> sections;
    std::vector<std::unique_ptr<juce::Component>> owned;
    AmSection* addSection (juce::Component& page, const juce::String& title, juce::Colour c);
    AmKnob* knob (AmSection* s, int param, const juce::String& caption, int row = 0);
    AmChoice* choice (AmSection* s, int param, const juce::String& caption, int row = 0);

    // patch panel page
    CableList cableList { proc };
    juce::Viewport cableView;
    juce::ComboBox fromBox, toBox;
    juce::TextButton connectBtn { "Connect" }, clearBtn { "Remove all" };
    juce::Label patchHelp;

    std::unique_ptr<juce::FileChooser> chooser;
    bool showPatch = false;
    int lastCableCount = -1;

    void showPage (bool patch);
    void refreshPresets();
    void refreshCables();
    void loadFile();
    void saveMenu();
    void saveAs (int kind);
    void timerCallback() override;
};
