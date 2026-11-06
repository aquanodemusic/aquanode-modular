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
    std::array<juce::Colour, ThemeColours::NumSlots> savedColours;
    bool savedGrid = true, savedSkeuo = false, savedKnown = false;
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
    t.showGrid = file.getBoolValue ("grid.show", true);
    t.skeuomorphic = file.getBoolValue ("ui.skeuomorphic", false);
    for (int i = 0; i < ThemeColours::NumSlots; ++i)
        savedColours[(size_t) i] = t.colour[i];
    savedGrid = t.showGrid;
    savedSkeuo = t.skeuomorphic;
    savedKnown = true;
}

void ThemePanel::save()
{
    const auto& t = themeColours();
    if (savedKnown && t.showGrid == savedGrid && t.skeuomorphic == savedSkeuo
        && std::equal (savedColours.begin(), savedColours.end(), t.colour))
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
    }
    if (t.showGrid)
        file.removeValue ("grid.show");
    else
        file.setValue ("grid.show", false);
    if (t.skeuomorphic)
        file.setValue ("ui.skeuomorphic", true);
    else
        file.removeValue ("ui.skeuomorphic");
    savedGrid = t.showGrid;
    savedSkeuo = t.skeuomorphic;
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
    for (int i = 0; i < ThemeColours::NumGridSlots; ++i)
        t.colour[i] = juce::Colour (c[juce::jlimit (0, numColourways - 1, which)][i]);
}

ThemePanel::ThemePanel (std::function<void()> onColoursChanged) : changed (std::move (onColoursChanged))
{
    slots.onPick = [this] (int slot) { showSlot (slot); };
    addAndMakeVisible (slots);
    knobChips.onPick = [this] (int slot) { showSlot (slot); };
    knobChips.setTooltip ("The Skeuomorphic knobs' value ring: the colour at its start and at its end");
    addAndMakeVisible (knobChips);

    selector.addChangeListener (this);
    selector.setColour (juce::ColourSelector::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (selector);

    resetButton.onClick = [this]
    {
        themeColours().colour[current] = ThemeColours::defaultColour (current);
        showSlot (current);
        save();
        if (changed) changed();
    };
    resetAllButton.onClick = [this]
    {
        themeColours().resetAll();
        gridToggle.setToggleState (true, juce::dontSendNotification);
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
        b.setTooltip ("Every colour at once: the " + juce::String (colourwayName (i)) + " colourway");
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
        save();
        if (changed) changed();
    };
    addAndMakeVisible (skeuoButton);

    resetViewButton.setTooltip ("The patch area back to its origin, at 100 %");
    resetViewButton.onClick = [this] { if (onResetView) onResetView(); };
    addAndMakeVisible (resetViewButton);

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
    selector.setCurrentColour (themeColours().get (current), juce::dontSendNotification);
}

void ThemePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (updating)
        return;
    themeColours().colour[current] = selector.getCurrentColour().withAlpha (1.0f);
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
        g.setColour (themeColours().get (i));
        g.fillRoundedRectangle (swatch, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (swatch, 4.0f, 1.0f);
        sw.removeFromLeft (8.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.8f));
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (11.0f, 15.0f, r.getHeight() * 0.42f))));
        g.drawFittedText (ThemeColours::name (i), sw.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
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
juce::Rectangle<int> ThemePanel::KnobChips::chip (int which) const
{
    return getLocalBounds().withHeight (getHeight() / 2).translated (0, which * (getHeight() / 2)).reduced (0, 1);
}

void ThemePanel::KnobChips::paint (juce::Graphics& g)
{
    for (int k = 0; k < 2; ++k)
    {
        const int slot = ThemeColours::KnobStart + k;
        auto r = chip (k).toFloat();
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
        g.setColour (themeColours().get (slot));
        g.fillRoundedRectangle (swatch, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (swatch, 3.0f, 1.0f);
        inner.removeFromLeft (5.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.8f));
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 14.0f, r.getHeight() * 0.8f))));
        g.drawFittedText (ThemeColours::name (slot), inner.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
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
    g.drawText ("Colours", titleArea, juce::Justification::centredLeft);
}

void ThemePanel::resized()
{
   #if JUCE_ANDROID
    const int rowH = 44;
   #else
    const int rowH = 26;
   #endif
    auto r = getLocalBounds().reduced (12);

    // the top: the title, Skeuomorphic and the colourways, Reset View to Origin
    // at the right - all in one row. Where that would squeeze the four buttons
    // (a phone held upright), the title gives way and Reset View moves down to
    // the Reset / Reset All / Done row: the four always share the top row.
    bool resetViewBelow = false;
    {
        auto top = r.removeFromTop (rowH);
        r.removeFromTop (8);
        // Skeuomorphic, the knob ring's two colours (a narrow column), then the
        // colourways - narrower than Skeuomorphic, as their names are short
        const int gap = 8, eachMax = 130, eachMin = 76;
        const float n = 1.0f + 0.85f + 0.72f * numColourways;   // widths in units of Skeuomorphic's
        auto eachFor = [&] (int titleW, int resetW)
        {
            const int used = (titleW > 0 ? titleW + gap : 0) + (resetW > 0 ? resetW + gap : 0) + (1 + numColourways) * gap;
            return juce::jmin (eachMax, (int) ((float) (top.getWidth() - used) / n));
        };
        int titleW = 84, resetW = juce::jlimit (120, 210, (top.getWidth() - titleW) / 4);
        if (eachFor (titleW, resetW) < eachMin) titleW = 0;
        if (eachFor (titleW, resetW) < eachMin) resetW = 0;
        const int each = eachFor (titleW, resetW);
        resetViewBelow = resetW == 0;

        titleArea = top.removeFromLeft (titleW);
        if (titleW > 0) top.removeFromLeft (gap);
        if (resetW > 0) { resetViewButton.setBounds (top.removeFromRight (resetW)); top.removeFromRight (gap); }
        resetViewButton.setButtonText (resetViewBelow ? "Reset View" : "Reset View to Origin");
        skeuoButton.setBounds (top.removeFromLeft (each));
        top.removeFromLeft (gap);
        // the colourways as wide as their names need (at least), the knob chips in what is left
        int wayW = (int) (each * 0.72f);
        for (auto& b : colourwayButtons)
        {
            const auto font = getLookAndFeel().getTextButtonFont (b, rowH);
            wayW = juce::jmax (wayW, (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, b.getButtonText())) + 14);
        }
        wayW = juce::jmin (wayW, (top.getWidth() - numColourways * gap - 60) / numColourways);
        knobChips.setBounds (top.removeFromLeft (juce::jmax (60, top.getWidth() - numColourways * (wayW + gap))));
        for (auto& b : colourwayButtons)
        {
            top.removeFromLeft (gap);
            b.setBounds (top.removeFromLeft (wayW));
        }
    }

    // wide (a phone on its side, a desktop window): the chips and buttons on the
    // left, the selector on the right; tall: one above the other
    const bool wide = r.getWidth() > r.getHeight() * 1.15f;
    juce::Rectangle<int> left, right;
    if (wide)
    {
        left = r.removeFromLeft (juce::jmin (r.getWidth() / 2 - 6, 460));
        r.removeFromLeft (12);
        right = r;
    }
    else
    {
        right = r.removeFromBottom (juce::jmax (r.getHeight() / 2, juce::jmin (r.getHeight() - 200, 420)));
        r.removeFromBottom (8);
        left = r;
    }

    auto buttons = left.removeFromBottom (rowH);
    left.removeFromBottom (6);
    // Grid Markers: beside Random Preset in the chips' free cell when there are two
    // columns, else on a row of its own under them
    const bool twoColumns = left.getWidth() >= 300;
    if (! twoColumns)
        gridToggle.setBounds (left.removeFromBottom (rowH).reduced (2, 0));
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
    slots.setBounds (left);
    if (twoColumns)
        gridToggle.setBounds (slots.chip (ThemeColours::NumGridSlots).translated (left.getX(), left.getY()).reduced (6, 0));
    selector.setBounds (right);
}
