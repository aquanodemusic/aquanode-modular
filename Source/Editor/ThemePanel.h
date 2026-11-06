#pragma once

// The colour panel.

#include <JuceHeader.h>
#include "../Engine/PluginProcessor.h"
#include "../Modules/Core/TapToTypeEditor.h"
#include "../Engine/PatchRender.h"

//==============================================================================
// The colour selector: the patch area's and the sidebar's backgrounds and the
// colours of the module types, kept in the app's settings.
class ThemePanel : public juce::Component,
                   private juce::ChangeListener
{
public:
    explicit ThemePanel (std::function<void()> onColoursChanged);
    ~ThemePanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onClose;
    std::function<void()> onResetView;     // Reset View to Origin: the patch area back to its origin at 100 %

    // the colours from / into the app's settings
    static void loadSaved();
    static void save();

    // a whole colourway at once: 0 Cyan Blue, 1 Sakura, 2 Green
    static constexpr int numColourways = 3;
    static const char* colourwayName (int which);
    static void applyColourway (int which);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showSlot (int slot);

    // the colours to choose from, as a grid of finger-sized chips (name + swatch)
    struct SlotGrid : public juce::Component
    {
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override;
        juce::Rectangle<int> chip (int slot) const;
        int columns() const;
        std::function<void (int)> onPick;
        int selected { 0 };
    };

    // the Skeuomorphic knob ring's start and end colours, beside Skeuomorphic
    struct KnobChips : public juce::Component, public juce::SettableTooltipClient
    {
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override;
        juce::Rectangle<int> chip (int which) const;
        std::function<void (int)> onPick;
        int selected { -1 };
    };

    std::function<void()> changed;
    SlotGrid slots;
    KnobChips knobChips;
    juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                                    | juce::ColourSelector::showColourspace };
    juce::TextButton resetButton { "Reset" }, resetAllButton { "Reset All" }, doneButton { "Done" };
    juce::TextButton colourwayButtons[numColourways], resetViewButton { "Reset View to Origin" };
    juce::TextButton skeuoButton { "Skeuomorphic" };   // the cards shaded like hardware (on / off)
    juce::ToggleButton gridToggle { "Grid Markers" };
    juce::Rectangle<int> titleArea;
    int current { 0 };
    bool updating { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemePanel)
};
