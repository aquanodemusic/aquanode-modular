#pragma once

// The handle above the keys and the keys' zoom pill.

#include <JuceHeader.h>
#include "../Engine/PluginProcessor.h"
#include "../Modules/Core/TapToTypeEditor.h"
#include "../Engine/PatchRender.h"

//==============================================================================
// The grip on top of the on-screen keyboard: drag it up or down to make the
// keyboard taller or shorter, double-click / double-tap for the default.
class KeyboardHeightHandle : public juce::Component
{
public:
    std::function<int()> getHeightNow;            // the keyboard's height when the drag starts
    std::function<void (int)> onHeightChanged;    // new height (0 = default)

    void paint (juce::Graphics& g) override
    {
        const auto pill = getLocalBounds().toFloat().withSizeKeepingCentre (56.0f, 8.0f);
        g.setColour (juce::Colour (0xff4fc3f7).withAlpha (isMouseOverOrDragging() ? 1.0f : 0.85f));
        g.fillRoundedRectangle (pill, 4.0f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        startH = getHeightNow != nullptr ? getHeightNow() : 0;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (onHeightChanged != nullptr)
            onHeightChanged (juce::jmax (1, startH - e.getDistanceFromDragStartY()));
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onHeightChanged != nullptr)
            onHeightChanged (0);
    }

private:
    int startH { 0 };
};

//==============================================================================
// The small "<- ->" pill beside the keyboard's grip: the keys' pinch zoom on
// or off. Off, two fingers on the keys always play (two notes sliding apart
// stay notes); the mouse's Shift + drag / Shift + wheel zoom either way.
class KeyZoomToggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<bool()> isOn;
    std::function<void()> onToggle;

    void paint (juce::Graphics& g) override
    {
        const bool on = isOn != nullptr && isOn();
        const bool over = isMouseOver();
        const auto blue = juce::Colour (0xff4fc3f7);
        const auto pill = getLocalBounds().toFloat().reduced (1.0f, juce::jmax (1.0f, (float) getHeight() * 0.22f));
        const float r = pill.getHeight() * 0.5f;
        if (on)
        {
            g.setColour (blue.withAlpha (over ? 1.0f : 0.88f));
            g.fillRoundedRectangle (pill, r);
        }
        else
        {
            g.setColour (juce::Colour (0xff15191f).withAlpha (0.8f));
            g.fillRoundedRectangle (pill, r);
            g.setColour (blue.withAlpha (over ? 0.95f : 0.6f));
            g.drawRoundedRectangle (pill.reduced (0.5f), r, 1.2f);
        }
        // "<- ->": two arrows pointing apart, a gap between them
        const auto c = pill.getCentre();
        const float reach = pill.getWidth() * 0.34f, gap = pill.getWidth() * 0.06f, head = pill.getHeight() * 0.24f;
        juce::Path p;
        p.startNewSubPath (c.x - gap, c.y);           p.lineTo (c.x - reach, c.y);
        p.startNewSubPath (c.x - reach + head, c.y - head); p.lineTo (c.x - reach, c.y); p.lineTo (c.x - reach + head, c.y + head);
        p.startNewSubPath (c.x + gap, c.y);           p.lineTo (c.x + reach, c.y);
        p.startNewSubPath (c.x + reach - head, c.y - head); p.lineTo (c.x + reach, c.y); p.lineTo (c.x + reach - head, c.y + head);
        g.setColour (on ? juce::Colour (0xff0b2230) : blue.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.4f, pill.getHeight() * 0.12f),
                                               juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()) && onToggle != nullptr)
            onToggle();
        repaint();
    }
};
