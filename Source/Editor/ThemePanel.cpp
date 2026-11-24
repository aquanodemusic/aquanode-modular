// The colour panel: the slots, the selector, the colourways, Skeuomorphic and
// the knob ring's colours.
#include "PluginEditor.h"
#include "../Engine/Mutation.h"
#include "../Presets/EffectAdders.h"
#include "../Presets/PresetGenerators.h"
#include "../Plugins/Host/PluginCatalog.h"
#include "../Plugins/Host/HostedPlugin.h"
#include "../Platform/AndroidDocuments.h"
#include <map>
#include <set>
#include "EditorShared.h"

//==============================================================================
// ThemePanel
//==============================================================================
namespace
{
    // what the settings file holds, so an unchanged save never touches it
    std::array<juce::Colour, ThemeColours::NumSlots> savedColours, savedGlassColours;
    bool savedGrid = true, savedSkeuo = false, savedGlass = false, savedWires = false, savedLeaves = true, savedKnown = false;
}

void ThemePanel::loadSaved()
{
    juce::PropertiesFile file (settingsOptions());
    auto& t = themeColours();
    for (int i = 0; i < ThemeColours::NumSlots; ++i)
    {
        const auto key = juce::String ("colour.") + ThemeColours::key (i);
        if (file.containsKey (key))
            t.colour[i] = juce::Colour::fromString (file.getValue (key)).withAlpha (1.0f);
    }
    for (int i = 0; i < ThemeColours::NumSlots; ++i)
    {
        const auto key = juce::String ("glass.") + ThemeColours::key (i);
        if (file.containsKey (key))
            t.glassCol[i] = juce::Colour::fromString (file.getValue (key)).withAlpha (1.0f);
    }
    t.showGrid = file.getBoolValue ("grid.show", true);
    t.wireLines = file.getBoolValue ("ui.wireLines", false);
    t.fallingLeaves = file.getBoolValue ("ui.leaves", true);
    t.skeuomorphic = file.getBoolValue ("ui.skeuomorphic", false);
    t.glass = ! t.skeuomorphic && file.getBoolValue ("ui.glass", false);
    for (int i = 0; i < ThemeColours::NumSlots; ++i)
    {
        savedColours[(size_t) i] = t.colour[i];
        savedGlassColours[(size_t) i] = t.glassCol[i];
    }
    savedGrid = t.showGrid;
    savedWires = t.wireLines;
    savedLeaves = t.fallingLeaves;
    savedSkeuo = t.skeuomorphic;
    savedGlass = t.glass;
    savedKnown = true;
}

void ThemePanel::save()
{
    const auto& t = themeColours();
    if (savedKnown && t.showGrid == savedGrid && t.skeuomorphic == savedSkeuo && t.glass == savedGlass
        && t.wireLines == savedWires && t.fallingLeaves == savedLeaves
        && std::equal (savedColours.begin(), savedColours.end(), t.colour)
        && std::equal (savedGlassColours.begin(), savedGlassColours.end(), t.glassCol))
        return;   // nothing new to write

    juce::PropertiesFile file (settingsOptions());
    for (int i = 0; i < ThemeColours::NumSlots; ++i)
    {
        const auto key = juce::String ("colour.") + ThemeColours::key (i);
        if (t.colour[i] == ThemeColours::defaultColour (i))
            file.removeValue (key);
        else
            file.setValue (key, t.colour[i].toString());
        savedColours[(size_t) i] = t.colour[i];

        const auto gkey = juce::String ("glass.") + ThemeColours::key (i);
        if (t.glassCol[i] == ThemeColours::glassDefault (i))
            file.removeValue (gkey);
        else
            file.setValue (gkey, t.glassCol[i].toString());
        savedGlassColours[(size_t) i] = t.glassCol[i];
    }
    if (t.wireLines)
        file.setValue ("ui.wireLines", true);
    else
        file.removeValue ("ui.wireLines");
    savedWires = t.wireLines;
    if (t.fallingLeaves)
        file.removeValue ("ui.leaves");
    else
        file.setValue ("ui.leaves", false);
    savedLeaves = t.fallingLeaves;
    if (t.showGrid)
        file.removeValue ("grid.show");
    else
        file.setValue ("grid.show", false);
    if (t.skeuomorphic)
        file.setValue ("ui.skeuomorphic", true);
    else
        file.removeValue ("ui.skeuomorphic");
    savedGrid = t.showGrid;
    if (t.glass)
        file.setValue ("ui.glass", true);
    else
        file.removeValue ("ui.glass");
    savedSkeuo = t.skeuomorphic;
    savedGlass = t.glass;
    savedKnown = true;
    file.saveIfNeeded();
}

// the colourways, slot by slot: patch, sidebar, light text, dark text, input / output, sound
// generation, filters, effects, utilities, plugin titles, random preset - light, bright
// grounds with the module types in shades of one family, as a light DAW theme has them
const char* ThemePanel::colourwayName (int which)
{
    static const char* n[numColourways] = { "Cyan Blue", "Sakura", "Green" };
    return n[juce::jlimit (0, numColourways - 1, which)];
}

void ThemePanel::applyColourway (int which)
{
    // (the knob ring's two colours stay as they are)
    static const juce::uint32 c[numColourways][ThemeColours::NumGridSlots] = {
        { 0xff6fb7c7, 0xff2c9fba, 0xfff2fbfc, 0xff000000, 0xff64cbdd, 0xff01d0e4, 0xff31c7b5, 0xff05aacd, 0xff6392e1, 0xff00aeda, 0xff41d0d2 },   // Cyan Blue
        { 0xffe5bdcc, 0xff57183a, 0xfffff4f8, 0xff3a1a2a, 0xffe49db1, 0xffd8577b, 0xffe182a5, 0xffc85684, 0xffb27dce, 0xffed7297, 0xffe7a3bc },   // Sakura
        { 0xffb3d6a9, 0xff194f23, 0xfff2fbef, 0xff132a17, 0xffb3d051, 0xff35b24b, 0xff6ec758, 0xff269c4c, 0xff8ec16f, 0xff52c782, 0xff7ed293 } }; // Green
    auto& t = themeColours();
    which = juce::jlimit (0, numColourways - 1, which);
    if (t.glass)
    {
        // the Glass versions, all of the palette (its leaves and sheen too):
        // gradient top, gradient bottom, light text, dark text, input / output,
        // sound generation, filters, effects, utilities, plugin titles, random
        // preset, leaves, glass sheen
        static const juce::uint32 gc[numColourways][ThemeColours::NumSlots] = {
            { 0xff3a9fd0, 0xff0a2a4e, 0xffffffff, 0xff081a2c, 0xff86ecff, 0xff3fb6ff, 0xff2a7fe0, 0xff6ff0dc, 0xffb4a2ff,
              0xffcdeeff, 0xff86ecff, 0xffe6f6ff, 0xffffffff },   // Cyan Blue: an ocean - sky blue down to the deep
            { 0xffc27a9e, 0xff3c1432, 0xffffffff, 0xff2a0a1c, 0xffffc4da, 0xffff7fae, 0xffd690ff, 0xffffb89c, 0xfff0d88e,
              0xffffe2ef, 0xffffc4da, 0xffffd3e4, 0xfffff2f7 },   // Sakura: dusk under the cherry trees, pink petals
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };          // Green: the jungle (the Glass look's own colours)
        for (int i = 0; i < ThemeColours::NumSlots; ++i)
            t.glassCol[i] = which == 2 ? ThemeColours::glassDefault (i) : juce::Colour (gc[which][i]);
        return;
    }
    for (int i = 0; i < ThemeColours::NumGridSlots; ++i)
        t.colour[i] = juce::Colour (c[which][i]);

    // Skeuomorphic (6.2): Cyan Blue and Green lean a little towards their Glass
    // versions - the ocean and the jungle - in every slot but the two text
    // colours (which stay as readable as they were)
    if (t.skeuomorphic && which != 1)
    {
        static const juce::uint32 oceanGc[ThemeColours::NumGridSlots] = {
            0xff3a9fd0, 0xff0a2a4e, 0xffffffff, 0xff081a2c, 0xff86ecff, 0xff3fb6ff, 0xff2a7fe0, 0xff6ff0dc, 0xffb4a2ff,
            0xffcdeeff, 0xff86ecff };
        constexpr float lean = 0.18f;
        for (int i = 0; i < ThemeColours::NumGridSlots; ++i)
        {
            if (i == 2 || i == 3)
                continue;
            const auto glassCol = which == 0 ? juce::Colour (oceanGc[i]) : ThemeColours::glassDefault (i);
            t.colour[i] = t.colour[i].interpolatedWith (glassCol, lean);
        }
    }
}

// the names and tips that change with the Glass look
void ThemePanel::refreshLabels()
{
    const bool g = themeColours().glass;
    knobChips.setTooltip (g ? "Glass: the colour of the leaves drifting over the patch area, and of the sheen on the glass cards and bars"
                            : "The Skeuomorphic knobs' value ring: the colour at its start and at its end");
    for (int i = 0; i < numColourways; ++i)
        colourwayButtons[i].setTooltip (g ? "The whole Glass palette at once: its " + juce::String (colourwayName (i)) + " version"
                                          : "Every colour at once: the " + juce::String (colourwayName (i)) + " colourway");
    resetAllButton.setTooltip (g ? "The Glass palette back to the jungle's own colours" : "Every colour back to the start");
    slots.repaint();
    knobChips.repaint();
    showSlot (current);
    if (! getLocalBounds().isEmpty())
        resized();   // (the title is longer with Glass)
    repaint();
}

ThemePanel::ThemePanel (std::function<void()> onColoursChanged) : changed (std::move (onColoursChanged))
{
    slots.onPick = [this] (int slot) { showSlot (slot); };
    addAndMakeVisible (slots);
    knobChips.onPick = [this] (int slot) { showSlot (slot); };
    addAndMakeVisible (knobChips);

    selector.addChangeListener (this);
    selector.setColour (juce::ColourSelector::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (selector);

    resetButton.onClick = [this]
    {
        themeColours().edited (current) = themeColours().editedDefault (current);
        showSlot (current);
        save();
        if (changed) changed();
    };
    resetAllButton.onClick = [this]
    {
        if (themeColours().glass)
            themeColours().resetGlass();   // (Glass: its own palette only)
        else
        {
            themeColours().resetAll();
            gridToggle.setToggleState (true, juce::dontSendNotification);
        }
        showSlot (current);
        save();
        if (changed) changed();
    };
    doneButton.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &resetButton, &resetAllButton, &doneButton })
        addAndMakeVisible (b);

    // the colourways, at the top beside the title
    for (int i = 0; i < numColourways; ++i)
    {
        auto& b = colourwayButtons[i];
        b.setButtonText (colourwayName (i));
        b.onClick = [this, i]
        {
            applyColourway (i);
            showSlot (current);
            save();
            if (changed) changed();
        };
        addAndMakeVisible (b);
    }
    // the Skeuomorphic look, left of the colourways: on and off, the colours stay
    skeuoButton.setClickingTogglesState (true);
    skeuoButton.setToggleState (themeColours().skeuomorphic, juce::dontSendNotification);
    skeuoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3fbf7f));
    skeuoButton.setTooltip ("Module cards shaded like hardware panels: satin knobs with a glowing arc (Knob Start into Knob End), "
                            "small screens for the values, glossy section headers and top bar buttons - in the colours set "
                            "here, which it leaves as they are; the plugins' own panels are not changed");
    skeuoButton.onClick = [this]
    {
        // only the look changes: the colours stay whatever they are
        themeColours().skeuomorphic = skeuoButton.getToggleState();
        if (themeColours().skeuomorphic) { themeColours().glass = false; glassButton.setToggleState (false, juce::dontSendNotification); }
        save();
        if (changed) changed();
        refreshLabels();
        repaint();
    };
    addAndMakeVisible (skeuoButton);

    // the Glass look, beside it (the one or the other)
    glassButton.setClickingTogglesState (true);
    glassButton.setToggleState (themeColours().glass, juce::dontSendNotification);
    glassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2fd17a));
    glassButton.setTooltip ("Everything as the Soundscape Ecology plugin looks: a jungle gradient under the patch with drifting "
                            "leaves, frosted glass cards and panels, luminous knob arcs and white text - in a palette of its "
                            "own, which the colours here edit while it is on (yours come back when it is off)");
    glassButton.onClick = [this]
    {
        themeColours().glass = glassButton.getToggleState();
        if (themeColours().glass) { themeColours().skeuomorphic = false; skeuoButton.setToggleState (false, juce::dontSendNotification); }
        save();
        if (changed) changed();
        refreshLabels();
        repaint();
    };
    addAndMakeVisible (glassButton);

    resetViewButton.setTooltip ("The patch area back to its origin, at 100 %");
    resetViewButton.onClick = [this] { if (onResetView) onResetView(); };
    addChildComponent (resetViewButton);   // (6.9.15: Reset View is in the View menu now - kept hidden)

    gridToggle.setToggleState (themeColours().showGrid, juce::dontSendNotification);
    gridToggle.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    gridToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff4fc3f7));
    gridToggle.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colours::white.withAlpha (0.6f));
    gridToggle.onClick = [this]
    {
        themeColours().showGrid = gridToggle.getToggleState();
        save();
        if (changed) changed();
    };
    addAndMakeVisible (gridToggle);

    wireToggle.setToggleState (themeColours().wireLines, juce::dontSendNotification);
    wireToggle.setTooltip ("The cables drawn as straight lines from socket to socket, instead of hanging down");
    for (int c : { juce::ToggleButton::textColourId, juce::ToggleButton::tickColourId, juce::ToggleButton::tickDisabledColourId })
        wireToggle.setColour (c, gridToggle.findColour (c));
    wireToggle.onClick = [this]
    {
        themeColours().wireLines = wireToggle.getToggleState();
        save();
        if (changed) changed();
    };
    addAndMakeVisible (wireToggle);

    leavesToggle.setToggleState (themeColours().fallingLeaves, juce::dontSendNotification);
    leavesToggle.setTooltip ("Glass: the leaves drifting down over the patch area, on or off");
    for (int c : { juce::ToggleButton::textColourId, juce::ToggleButton::tickColourId, juce::ToggleButton::tickDisabledColourId })
        leavesToggle.setColour (c, gridToggle.findColour (c));
    leavesToggle.onClick = [this]
    {
        themeColours().fallingLeaves = leavesToggle.getToggleState();
        save();
        if (changed) changed();
    };
    addAndMakeVisible (leavesToggle);

    refreshLabels();
    showSlot (0);
}

ThemePanel::~ThemePanel()
{
    selector.removeChangeListener (this);
}

void ThemePanel::showSlot (int slot)
{
    current = juce::jlimit (0, ThemeColours::NumSlots - 1, slot);
    slots.selected = current;
    slots.repaint();
    knobChips.selected = current;
    knobChips.repaint();
    const juce::ScopedValueSetter<bool> svs (updating, true);
    selector.setCurrentColour (themeColours().edited (current), juce::dontSendNotification);
}

void ThemePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (updating)
        return;
    themeColours().edited (current) = selector.getCurrentColour().withAlpha (1.0f);
    slots.repaint();
    knobChips.repaint();
    save();
    if (changed) changed();
}

int ThemePanel::SlotGrid::columns() const
{
    return getWidth() >= 300 ? 2 : 1;
}

juce::Rectangle<int> ThemePanel::SlotGrid::chip (int slot) const
{
    const int cols = columns();
    const int rows = (ThemeColours::NumGridSlots + cols - 1) / cols;
    const float w = getWidth() / (float) cols, h = getHeight() / (float) rows;
    const int c = slot % cols, r = slot / cols;
    return juce::Rectangle<float> (c * w, r * h, w, h).reduced (3.0f).toNearestInt();
}

void ThemePanel::SlotGrid::paint (juce::Graphics& g)
{
    for (int i = 0; i < ThemeColours::NumGridSlots; ++i)
    {
        auto r = chip (i).toFloat();
        const bool on = i == selected;
        g.setColour (juce::Colours::white.withAlpha (on ? 0.16f : 0.05f));
        g.fillRoundedRectangle (r, 5.0f);
        if (on)
        {
            g.setColour (juce::Colour (0xff4fc3f7));
            g.drawRoundedRectangle (r.reduced (0.75f), 5.0f, 1.5f);
        }
        auto sw = r.reduced (6.0f);
        const float side = juce::jmin (sw.getHeight(), 26.0f);
        auto swatch = sw.removeFromLeft (side).withSizeKeepingCentre (side, side);
        g.setColour (themeColours().edited (i));
        g.fillRoundedRectangle (swatch, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (swatch, 4.0f, 1.0f);
        sw.removeFromLeft (8.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.8f));
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (11.0f, 15.0f, r.getHeight() * 0.42f))));
        g.drawFittedText (themeColours().label (i), sw.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void ThemePanel::SlotGrid::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < ThemeColours::NumGridSlots; ++i)
        if (chip (i).contains (e.getPosition()))
        {
            if (onPick) onPick (i);
            return;
        }
}

// the knob ring's two colours, one above the other, in the panel's top row
// (or as two cells of the grid: then the same size and spacing as its chips)
juce::Rectangle<int> ThemePanel::KnobChips::chip (int which) const
{
    if (gridStyle)
        return juce::Rectangle<float> (0.0f, (float) which * getHeight() * 0.5f, (float) getWidth(), getHeight() * 0.5f)
                   .reduced (3.0f).toNearestInt();
    return getLocalBounds().withHeight (getHeight() / 2).translated (0, which * (getHeight() / 2)).reduced (0, 1);
}

void ThemePanel::KnobChips::paint (juce::Graphics& g)
{
    for (int k = 0; k < 2; ++k)
    {
        const int slot = ThemeColours::KnobStart + k;
        auto r = chip (k).toFloat();
        if (gridStyle)
        {
            // drawn as the grid draws its chips
            const bool on = selected == slot;
            g.setColour (juce::Colours::white.withAlpha (on ? 0.16f : 0.05f));
            g.fillRoundedRectangle (r, 5.0f);
            if (on)
            {
                g.setColour (juce::Colour (0xff4fc3f7));
                g.drawRoundedRectangle (r.reduced (0.75f), 5.0f, 1.5f);
            }
            auto sw = r.reduced (6.0f);
            const float side = juce::jmin (sw.getHeight(), 26.0f);
            auto swatch = sw.removeFromLeft (side).withSizeKeepingCentre (side, side);
            g.setColour (themeColours().edited (slot));
            g.fillRoundedRectangle (swatch, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRoundedRectangle (swatch, 4.0f, 1.0f);
            sw.removeFromLeft (8.0f);
            g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.8f));
            g.setFont (juce::Font (juce::FontOptions (juce::jlimit (11.0f, 15.0f, r.getHeight() * 0.42f))));
            g.drawFittedText (themeColours().label (slot), sw.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            continue;
        }
        const bool on = selected == slot;
        g.setColour (juce::Colours::white.withAlpha (on ? 0.16f : 0.05f));
        g.fillRoundedRectangle (r, 4.0f);
        if (on)
        {
            g.setColour (juce::Colour (0xff4fc3f7));
            g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 1.2f);
        }
        auto inner = r.reduced (3.0f, 2.0f);
        const float side = juce::jmin (inner.getHeight(), 16.0f);
        auto swatch = inner.removeFromLeft (side).withSizeKeepingCentre (side, side);
        g.setColour (themeColours().edited (slot));
        g.fillRoundedRectangle (swatch, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (swatch, 3.0f, 1.0f);
        inner.removeFromLeft (5.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.8f));
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 14.0f, r.getHeight() * 0.8f))));
        g.drawFittedText (themeColours().label (slot), inner.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
}

void ThemePanel::KnobChips::mouseUp (const juce::MouseEvent& e)
{
    for (int k = 0; k < 2; ++k)
        if (chip (k).contains (e.getPosition()))
        {
            if (onPick) onPick (ThemeColours::KnobStart + k);
            return;
        }
}

void ThemePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xf01b1917));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText ("Colours", titleArea, juce::Justification::centredLeft, true);

    // the small line between the looks and the colourways
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    if (dividerX >= 0)
        g.fillRect (juce::Rectangle<float> ((float) dividerX - 0.5f, (float) topRow.getY() + topRow.getHeight() * 0.18f,
                                            1.0f, topRow.getHeight() * 0.64f));
}

void ThemePanel::resized()
{
   #if JUCE_ANDROID
    const int rowH = 44;
   #else
    const int rowH = 26;
   #endif
    auto r = getLocalBounds().reduced (12);

    // wide (a phone on its side, a desktop window): the chips and buttons on the
    // left, the selector on the right; tall: one above the other. Tall (a phone
    // held upright) also changes the top row: Glass | Skeuomorphic | Cyan |
    // Pink | Green with short names and room for them, the knob colours down
    // in the grid, Reset View with the buttons below
    const bool wide = r.getWidth() > (r.getHeight() - rowH - 8) * 1.15f;
    knobChips.gridStyle = ! wide;
    {
        static const char* shortNames[numColourways] = { "Cyan", "Pink", "Green" };
        for (int i = 0; i < numColourways; ++i)
            colourwayButtons[i].setButtonText (wide ? juce::String (colourwayName (i)) : juce::String (shortNames[i]));
    }

    // the top row: the title, Glass | Skeuomorphic and its knob colours |
    // the colourways, Reset View to Origin at the right. Where that would
    // squeeze the buttons (a phone held upright), the title gives way and Reset
    // View moves down to the Reset / Reset All / Done row.
    bool resetViewBelow = false;
    if (! wide)
    {
        auto top = r.removeFromTop (rowH);
        topRow = top;
        r.removeFromTop (8);
        titleArea = {};
        resetViewBelow = false;   // (6.9.15: in the View menu)
        resetViewButton.setButtonText ("Reset View");
        // each as wide as its name needs, the room left (or missing) shared out
        juce::TextButton* row[] = { &glassButton, &skeuoButton, &colourwayButtons[0], &colourwayButtons[1], &colourwayButtons[2] };
        constexpr int n = (int) (sizeof (row) / sizeof (row[0]));
        const int gap = 8, divW = 2 * gap + 1;
        int need[n], total = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto font = getLookAndFeel().getTextButtonFont (*row[i], rowH);
            need[i] = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, row[i]->getButtonText())) + 18;
            total += need[i];
        }
        const int room = top.getWidth() - divW - (n - 2) * gap;
        const int spare = juce::jmax (0, room - total) / n;
        const float squeeze = total > room ? (float) room / (float) total : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const int w = i == n - 1 ? top.getWidth() : (int) ((float) need[i] * squeeze) + spare;
            row[i]->setBounds (top.removeFromLeft (w));
            if (i == 1)
            {
                dividerX = top.getX() + divW / 2;
                top.removeFromLeft (divW);
            }
            else if (i < n - 1)
                top.removeFromLeft (gap);
        }
    }
    else
    {
        auto top = r.removeFromTop (rowH);
        topRow = top;
        r.removeFromTop (8);
        const int gap = 8, divW = 2 * gap + 1, eachMax = 140, eachMin = 70;
        const float units = 0.62f + 1.15f + 0.8f + 0.82f * numColourways;   // Glass, Skeuomorphic, knob colours, colourways
        const int fixed = divW + (2 + (numColourways - 1)) * gap;
        auto eachFor = [&] (int titleW, int resetW)
        {
            const int used = (titleW > 0 ? titleW + gap : 0) + (resetW > 0 ? resetW + gap : 0) + fixed;
            return juce::jmin (eachMax, (int) ((float) (top.getWidth() - used) / units));
        };
        int titleW = 84;   // (the same in every look: nothing in the row moves when the look changes)
        int resetW = 0;    // (6.9.15: Reset View is in the View menu)
        if (eachFor (titleW, resetW) < eachMin) titleW = 0;
        const int each = eachFor (titleW, resetW);
        resetViewBelow = false;

        titleArea = top.removeFromLeft (titleW);
        if (titleW > 0) top.removeFromLeft (gap);
        if (resetW > 0) { resetViewButton.setBounds (top.removeFromRight (resetW)); top.removeFromRight (gap); }
        {
            const auto font = getLookAndFeel().getTextButtonFont (resetViewButton, rowH);
            const bool fits = ! resetViewBelow && juce::GlyphArrangement::getStringWidth (font, "Reset View to Origin") + 16 <= (float) resetW;
            resetViewButton.setButtonText (fits ? "Reset View to Origin" : "Reset View");
        }
        glassButton.setBounds (top.removeFromLeft ((int) (each * 0.62f)));
        top.removeFromLeft (gap);
        skeuoButton.setBounds (top.removeFromLeft ((int) (each * 1.15f)));
        top.removeFromLeft (gap);
        // the colourways as wide as their names need (at least), the knob colours in what is left
        int wayW = (int) (each * 0.82f);
        for (auto& b : colourwayButtons)
        {
            const auto font = getLookAndFeel().getTextButtonFont (b, rowH);
            wayW = juce::jmax (wayW, (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, b.getButtonText())) + 26);
        }
        wayW = juce::jmin (wayW, (top.getWidth() - divW - (numColourways - 1) * gap - 60) / numColourways);
        knobChips.setBounds (top.removeFromLeft (juce::jmax (60, top.getWidth() - divW - numColourways * wayW - (numColourways - 1) * gap)));
        dividerX = top.getX() + divW / 2;
        top.removeFromLeft (divW);
        for (int i = 0; i < numColourways; ++i)
        {
            if (i > 0) top.removeFromLeft (gap);
            colourwayButtons[i].setBounds (top.removeFromLeft (wayW));
        }
    }

    juce::Rectangle<int> left, right;
    if (wide)
    {
        left = r.removeFromLeft (juce::jmin (r.getWidth() / 2 - 6, 460));
        r.removeFromLeft (12);
        right = r;
    }
    else
    {
        // (the selector a little smaller than half: the grid has a row more)
        right = r.removeFromBottom (juce::jmax ((int) (r.getHeight() * 0.44f), juce::jmin (r.getHeight() - 260, 380)));
        r.removeFromBottom (8);
        left = r;
    }

    auto buttons = left.removeFromBottom (rowH);
    left.removeFromBottom (6);
    // Grid, Wire and Leaves side by side (each as wide as its name wants, the
    // room left shared out): in the free cell to the right of the last chip
    // (Random Preset) where they fit there, else on a row of their own under the chips
    juce::ToggleButton* toggles[3] = { &gridToggle, &wireToggle, &leavesToggle };
    const float toggleFontH = juce::jmin (15.0f, (float) rowH * 0.75f);
    int toggleWant[3], toggleTotal = 0;
    for (int i = 0; i < 3; ++i)
    {
        toggleWant[i] = (int) std::ceil (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (toggleFontH)), toggles[i]->getButtonText()))
                      + (int) (toggleFontH * 1.1f) + 22;   // the tick box and room around it
        toggleTotal += toggleWant[i];
    }
    // (in the cell they may come closer: each needs its tick box and name, and a little room)
    const int toggleTight = toggleTotal - 3 * 12;
    auto placeToggles = [&] (juce::Rectangle<int> row)
    {
        const int pad = row.getWidth() >= toggleTotal ? 0 : 12;
        const int spare = juce::jmax (0, row.getWidth() - (toggleTotal - 3 * pad)) / 3;
        for (int i = 0; i < 3; ++i)
            toggles[i]->setBounds (i == 2 ? row : row.removeFromLeft (toggleWant[i] - pad + spare));
    };
    auto chipsArea = left.withTrimmedBottom (8);
    bool togglesInCell = false;
    if (wide && ThemeColours::NumGridSlots % 2 == 1 && chipsArea.getWidth() >= 300)   // two columns, the last cell free
                                                                                       // (tall: the knob colours go there)
    {
        const int rows = (ThemeColours::NumGridSlots + 1) / 2;
        const float cellW = chipsArea.getWidth() / 2.0f, cellH = chipsArea.getHeight() / (float) rows;
        auto cell = juce::Rectangle<float> (chipsArea.getX() + cellW, chipsArea.getY() + (rows - 1) * cellH, cellW, cellH)
                        .reduced (3.0f).toNearestInt();
        if (toggleTight <= cell.getWidth() - 4)
        {
            placeToggles (cell.withSizeKeepingCentre (cell.getWidth() - 4, juce::jmin (cell.getHeight(), rowH)));
            togglesInCell = true;
        }
        // (7.0.0) where the platform's font measures wider (Windows), the text and tick
        // boxes get smaller until the three fit the cell, rather than dropping to a row of their own
        for (float fh = toggleFontH - 0.5f; fh >= 9.0f && ! togglesInCell; fh -= 0.5f)
        {
            int want[3], total = 0;
            for (int i = 0; i < 3; ++i)
            {
                want[i] = (int) std::ceil (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (fh)), toggles[i]->getButtonText()))
                        + (int) std::ceil (fh * 1.1f) + 12;   // the tick box, a little room around it
                total += want[i];
            }
            if (total > cell.getWidth() - 4 && fh > 9.0f) continue;   // (at the smallest size they go in anyway)
            const int h = juce::jmin (cell.getHeight(), (int) std::ceil (fh / 0.75f));
            auto row = cell.withSizeKeepingCentre (cell.getWidth() - 4, h);
            const int spare = juce::jmax (0, row.getWidth() - total) / 3;
            for (int i = 0; i < 3; ++i)
                toggles[i]->setBounds (i == 2 ? row : row.removeFromLeft (want[i] + spare));
            togglesInCell = true;
        }
    }
    // (6.9.15) tall (a phone held upright): the free cell under Random Preset, left of the knob colours
    if (! wide && ! togglesInCell && ThemeColours::NumGridSlots % 2 == 1 && left.getWidth() >= 300)
    {
        const auto area = left.withTrimmedBottom (8);
        const int gridRows = (ThemeColours::NumGridSlots + 1) / 2;
        const float cellH = (float) area.getHeight() / (float) (gridRows + 1), cellW = (float) area.getWidth() / 2.0f;
        auto cell = juce::Rectangle<float> ((float) area.getX(), (float) area.getY() + cellH * (float) gridRows, cellW, cellH)
                        .reduced (3.0f).toNearestInt();
        // (6.9.16) the text (and tick box) smaller, the padding tiny, until the three fit
        // there: the toggles' font follows their height (three quarters of it, up to 15)
        for (float fh = toggleFontH; fh >= 7.0f && ! togglesInCell; fh -= 0.5f)
        {
            int want[3], total = 0;
            for (int i = 0; i < 3; ++i)
            {
                want[i] = (int) std::ceil (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (fh)), toggles[i]->getButtonText()))
                        + (int) std::ceil (fh * 1.1f) + 10;   // the tick box, a little room around it
                total += want[i];
            }
            if (total > cell.getWidth() - 2) continue;
            const int h = juce::jmin (cell.getHeight(), (int) std::ceil (fh / 0.75f));
            auto row = cell.withSizeKeepingCentre (cell.getWidth() - 2, h);
            const int spare = (row.getWidth() - total) / 3;
            for (int i = 0; i < 3; ++i)
                toggles[i]->setBounds (i == 2 ? row : row.removeFromLeft (want[i] + spare));
            togglesInCell = true;
        }
    }
    if (! togglesInCell)
    {
        placeToggles (left.removeFromBottom (rowH).reduced (2, 0));
        left.removeFromBottom (4);
    }
    if (resetViewBelow)
    {
        const int rw = (buttons.getWidth() - 24) / 4;
        resetViewButton.setBounds (buttons.removeFromRight (rw));
        buttons.removeFromRight (8);
    }
    const int w = (buttons.getWidth() - 16) / 3;
    resetButton.setBounds (buttons.removeFromLeft (w));
    buttons.removeFromLeft (8);
    resetAllButton.setBounds (buttons.removeFromLeft (w));
    buttons.removeFromLeft (8);
    doneButton.setBounds (buttons);
    left.removeFromBottom (8);
    if (wide)
        slots.setBounds (left);
    else
    {
        // the grid with the two knob colours after it, in the right column under
        // Plugin Titles (the cell beside Random Preset and the one below it) -
        // or, in one column, two rows of their own at the end
        const int cols = left.getWidth() >= 300 ? 2 : 1;
        const int gridRows = (ThemeColours::NumGridSlots + cols - 1) / cols;
        const bool freeCell = cols == 2 && ThemeColours::NumGridSlots % 2 == 1;
        const int rows = gridRows + (freeCell ? 1 : 2);
        const float cellH = (float) left.getHeight() / (float) rows;
        const float cellW = (float) left.getWidth() / (float) cols;
        slots.setBounds (left.withHeight (juce::roundToInt (cellH * (float) gridRows)));
        const float y = (float) left.getY() + cellH * (float) (freeCell ? gridRows - 1 : gridRows);
        knobChips.setBounds (juce::Rectangle<float> ((float) left.getX() + cellW * (float) (cols - 1), y, cellW, cellH * 2.0f).toNearestInt());
    }

    selector.setBounds (right);
}