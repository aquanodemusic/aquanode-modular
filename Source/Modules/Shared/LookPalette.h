#pragma once
//==============================================================================
// LookPalette (6.7.10) - colours for a card's own drawing (a curve editor, a
// display) that follow the app's look: the default borrows a little from
// Glass and Skeuomorphic (a lagoon-to-teal night, a glowing accent with a
// glossy top), Glass leans fully into Glass (jungle lagoon, mint, a halo),
// Skeuomorphic into a hardware panel (a recessed screen, the colourway's own
// colour for the accent, glossy round handles). Message thread.
//==============================================================================
#include "../Core/ModuleCore.h"

namespace aquanode
{
struct LookPalette
{
    juce::Colour bgTop, bgBottom, grid, accent, text;
    bool glow { true }, skeuo { false }, glass { false };

    // defaultAccent: the editor's own colour in the default look;
    // section: whose colourway colour the Skeuomorphic look takes
    static LookPalette current (juce::Colour defaultAccent, int section = ThemeColours::Effect)
    {
        const auto& th = themeColours();
        LookPalette p;
        p.text = juce::Colours::white;
        if (th.glass)
        {
            p.glass = true;
            p.bgTop = juce::Colour (0xff0b2a27); p.bgBottom = juce::Colour (0xff0a1b2c);
            p.grid = juce::Colour (0xffbff8ea);
            p.accent = juce::Colour (0xff7ff5c8);
        }
        else if (th.skeuomorphic)
        {
            p.skeuo = true; p.glow = false;
            p.bgTop = juce::Colour (0xff26231f); p.bgBottom = juce::Colour (0xff1a1815);
            p.grid = juce::Colour (0xffd8ffd0);
            p.accent = th.colour[juce::jlimit (0, (int) ThemeColours::NumSlots - 1, section)].brighter (0.3f);   // (lit on its dark screen)
        }
        else
        {
            p.bgTop = juce::Colour (0xff101c25); p.bgBottom = juce::Colour (0xff0c171b);
            p.grid = juce::Colour (0xffcfeeff);
            p.accent = defaultAccent;
        }
        return p;
    }

    // the background: a gradient; Skeuomorphic: a recessed screen with a bevel;
    // Glass: a frosted rim
    void fillBackground (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, r.getY(), bgBottom, 0.0f, r.getBottom(), false));
        g.fillRect (r);
        if (skeuo)
        {
            auto screen = r.reduced (3.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d1410), 0.0f, screen.getY(), juce::Colour (0xff121c16), 0.0f, screen.getBottom(), false));
            g.fillRoundedRectangle (screen, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (screen.translated (0.0f, -0.5f), 4.0f, 1.2f);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (screen.translated (0.0f, 0.8f), 4.0f, 1.0f);
        }
        else if (glass)
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 1.0f);
        }
    }

    // a stroked path, with a soft glow under it where the look has one
    void strokeGlowing (juce::Graphics& g, const juce::Path& path, float width) const
    {
        if (glow)
        {
            g.setColour (accent.withAlpha (glass ? 0.22f : 0.16f));
            g.strokePath (path, juce::PathStrokeType (width * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (accent.withAlpha (0.95f));
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // a handle (a point on a curve): a dot with a glossy top, a glossy knob in Skeuomorphic
    void fillHandle (juce::Graphics& g, juce::Point<float> c, float size, bool selected) const
    {
        const auto r = juce::Rectangle<float> (size, size).withCentre (c);
        const auto col = selected ? juce::Colours::white : accent;
        if (glow)
        {
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (r.expanded (3.0f));
        }
        if (skeuo)
        {
            g.setGradientFill (juce::ColourGradient (col.brighter (0.5f), r.getCentreX(), r.getY(), col.darker (0.5f), r.getCentreX(), r.getBottom(), false));
            g.fillEllipse (r);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (r, 1.0f);
        }
        else
        {
            g.setColour (col);
            g.fillEllipse (r);
        }
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillEllipse (r.withHeight (r.getHeight() * 0.42f).reduced (r.getWidth() * 0.2f, r.getHeight() * 0.05f));
    }
};
} // namespace aquanode
