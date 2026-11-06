#pragma once

// The sidebar's tooltip and its header (logo, patch name, search, MPE).

#include <JuceHeader.h>
#include "../Engine/PluginProcessor.h"
#include "../Modules/Core/TapToTypeEditor.h"
#include "../Engine/PatchRender.h"

//==============================================================================
// The sidebar's hover tooltip: a floating panel of wrapped text. Lives on the
// top-level component rather than inside the sidebar, so it can extend past
// the sidebar's Viewport instead of being clipped by it.
class SidebarTooltip : public juce::Component
{
public:
    SidebarTooltip() { setInterceptsMouseClicks (false, false); }

    static constexpr int padding = 10;

    // sizes itself to the text; maxWidth caps how wide it grows before wrapping
    void setText (const juce::String& title, const juce::String& body, int maxWidth);

    void paint (juce::Graphics& g) override;

private:
    juce::TextLayout layout;
};

//==============================================================================
// The top of the sidebar, above its scrolling list: the "Aquanode Modular"
// name (a tap opens the colour selector), below it the loaded patch's name and
// the zoom percentage (a tap on it resets the view), and the module search.
class SidebarHeader : public juce::Component,
                      private juce::Timer
{
public:
    SidebarHeader();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    std::function<void()> onLogoClicked;
    std::function<void()> onZoomClicked;
    std::function<void (const juce::String&)> onSearchChanged;
    std::function<juce::String()> getPatchName;   // polled a few times a second
    std::function<void (bool)> onMpeChanged;      // the MPE switch beside the search
    void setMpe (bool on) { mpeButton.setToggleState (on, juce::dontSendNotification); }

    void setZoomText (const juce::String& text) { if (text != zoomText) { zoomText = text; repaint (infoArea); } }

    void clearSearch();
    void refreshColours();   // the search field follows the sidebar's colours
    static int preferredHeight();

private:
    void timerCallback() override;

    juce::Rectangle<int> logoArea, infoArea, zoomArea, clearArea;
    juce::String patchName, zoomText { "100%" };
    aquanode::TapToTypeEditor search;   // focus only when tapped (no keyboard popping up on Android)
    juce::TextButton mpeButton { "MPE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidebarHeader)
};
