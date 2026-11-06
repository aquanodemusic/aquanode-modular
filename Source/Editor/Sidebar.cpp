// The sidebar: its header (logo, patch name, search, MPE), its groups and rows,
// the tooltips, and the modules' search.
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
// SidebarComponent
//==============================================================================
//==============================================================================
// SidebarTooltip
//==============================================================================
void SidebarTooltip::setText (const juce::String& title, const juce::String& body, int maxWidth)
{
    juce::AttributedString s;
    s.setLineSpacing (1.5f);

    s.append (title + "\n", juce::Font (juce::FontOptions (15.5f, juce::Font::bold)),
              juce::Colours::white);
    s.append (body, juce::Font (juce::FontOptions (14.0f)),
              juce::Colours::white.withAlpha (0.82f));

    const int textWidth = maxWidth - 2 * padding;
    layout.createLayout (s, (float) textWidth);

    setSize (maxWidth, (int) std::ceil (layout.getHeight()) + 2 * padding);
}

void SidebarTooltip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xf01a1a1a));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    layout.draw (g, bounds.reduced ((float) padding).translated (0.0f, 1.0f));
}

//==============================================================================
// The sidebar's groups: modules that do the same kind of thing listed together
// inside their category ("Envelopes": ADSR, Analog EG, Curve Env ...)
//==============================================================================
namespace
{
    struct ModuleGroup { const char* name; std::vector<const char*> typeIds; };

    const std::vector<ModuleGroup>& groupsOf (ModuleSection section)
    {
        static const std::vector<ModuleGroup> none;
        static const std::vector<ModuleGroup> inputOutput {
            { "Audio Routing",   { "io.audioIn", "io.audioOut" } },
            { "MIDI",            { "io.keyboardmidi", "io.midicontrols", "io.midilearn" } } };
        static const std::vector<ModuleGroup> oscillators {
            { "Oscillators",     { "osc.basic", "osc.additive", "osc.complex", "osc.drift", "osc.dualvco", "osc.pd",
                                   "osc.pwmosc", "osc.warp", "osc.unison", "osc.formant", "osc.acid303" } },
            { "Wavetables",      { "osc.wavetable", "osc.chordwavetable", "osc.transwave" } },
            { "FM",              { "osc.dxop" } },
            { "Physical Models", { "osc.bowed", "osc.pluck", "osc.piano", "osc.grand",
                                   "osc.wind", "osc.droplets", "osc.modaldrum" } },
            { "Drums",           { "osc.clap", "osc.snare", "osc.kick", "osc.hats" } },
            { "Samples & Grains", { "osc.sampler", "osc.granulate" } },
            { "Resynthesis",     { "osc.spectralresynth", "osc.resynthpiano" } },
            { "Noise",           { "osc.noise" } } };
        static const std::vector<ModuleGroup> filters {
            { "Filters",         { "filter.lowpass", "filter.highpass", "filter.notch", "filter.svf", "filter.warmladder", "filter.ladder",
                                   "filter.dualfilter", "filter.aquafilter", "filter.formant" } },
            { "EQ",              { "filter.bell", "filter.belleq", "filter.shelfeq", "filter.resonance" } },
            { "Combs & Pitch",   { "filter.combfilter", "fx.centercomb", "fx.pitcheq" } } };
        static const std::vector<ModuleGroup> effects {
            { "Delays",          { "fx.delay", "fx.freedelay", "fx.echo", "fx.crossdelay", "fx.adelaysr" } },
            { "Reverbs",         { "fx.reverb", "fx.aquaton", "fx.smoothhall", "fx.springer", "fx.irforge" } },
            { "Grains",          { "fx.granulation" } },
            { "Modulation",      { "fx.chorus", "fx.liquidchor", "fx.ensemble", "fx.flanger", "fx.phaser", "fx.wah", "fx.stereophaser",
                                   "fx.tapewobble", "fx.anyfm" } },
            { "Pitch",           { "fx.pitchshift" } },
            { "Distortion",      { "fx.drive", "fx.clippreserve", "fx.bitcrush", "fx.srreduce", "fx.waveshaper" } },
            { "Dynamics",        { "fx.compress", "fx.sidechaincomp", "fx.threebandcomp", "fx.limiter" } },
            { "Resonators",      { "fx.resonate", "fx.resonate5", "fx.vocode" } },
            { "Stereo & Bands",  { "fx.stereowidth", "effect.bandsplitter", "fx.precision" } },
            { "Spectral",        { "fx.spectrallock" } },
            { "VST3",            { "fx.externalplugin" } } };
        static const std::vector<ModuleGroup> utilities {
            { "Envelopes",       { "util.adsr", "util.analogeg", "util.curveenv", "util.dxenv", "util.dxpitcheg",
                                   "util.envfollow" } },
            { "LFOs",            { "util.multilfo", "util.randomlfo", "util.skewlfo", "util.drawlfo", "util.dxlfo",
                                   "util.lfo" } },
            { "MIDI",            { "util.arp", "util.alwaysmidi", "util.discardmidi", "util.pianoroll", "util.velocity", "util.noterandom",
                                   "util.midiadd", "util.graphicmidiadd", "util.keyboardmidi", "util.midicontrols", "util.mpe" } },
            { "Sequencers & Clocks", { "util.clock", "util.euclid", "util.stepseq", "util.gate" } },
            { "Control",         { "util.curvecv", "util.dcoffset", "util.samplehold", "util.slew", "util.quantize",
                                   "util.dawmod", "util.keytrack", "util.logic", "util.vector" } },
            { "Audio Tools",     { "util.volume", "util.mixer", "util.panning", "util.crossfade", "util.ringmod", "util.splitter",
                                   "util.thru", "util.polysum", "util.record", "util.dcblock" } },
            { "Routing",         { "util.reroutesend", "util.reroutereturn", "util.macro" } },
            { "Analysis",        { "util.spectrogram", "util.spectrumcompare", "util.tuner" } } };
        switch (section)
        {
            case ModuleSection::InputOutput: return inputOutput;
            case ModuleSection::Oscillator: return oscillators;
            case ModuleSection::Filter:     return filters;
            case ModuleSection::Effect:     return effects;
            case ModuleSection::Utility:    return utilities;
            default:                        return none;
        }
    }

    // the group a module is listed in, by its type ("" for none); unlisted
    // types fall back on what their id says they are
    juce::String groupOf (const ModuleDescriptor& d)
    {
        const auto& id = d.typeId;
        if (d.isPluginModule())
            return d.displayName.upToFirstOccurrenceOf (" - ", false, false);   // the plugin it comes from
        if (id.startsWith ("fx.spec"))
            return "Spectral";
        for (const auto& g : groupsOf (d.section))
            for (const char* t : g.typeIds)
                if (id == t)
                    return g.name;
        const auto lower = id.toLowerCase();
        struct Hint { const char* word; const char* group; };
        static const Hint hints[] = {
            { "env", "Envelopes" }, { "lfo", "LFOs" }, { "midi", "MIDI" }, { "arp", "MIDI" }, { "seq", "Sequencers & Clocks" },
            { "delay", "Delays" }, { "echo", "Delays" }, { "verb", "Reverbs" }, { "hall", "Reverbs" },
            { "comp", "Dynamics" }, { "eq", "EQ" }, { "piano", "Physical Models" }, { "drum", "Drums" },
            { "wavetable", "Wavetables" }, { "chorus", "Modulation" }, { "phaser", "Modulation" } };
        for (const auto& h : hints)
            if (lower.contains (h.word))
                return h.group;
        return "More";
    }

    int groupOrder (ModuleSection section, const juce::String& group)
    {
        const auto& groups = groupsOf (section);
        for (size_t i = 0; i < groups.size(); ++i)
            if (group == groups[i].name)
                return (int) i;
        return group == "Spectral" ? 900 : 1000;
    }

    juce::String sectionTitle (ModuleSection section)
    {
        return section == ModuleSection::Filter  ? "Filters"
             : section == ModuleSection::Effect  ? "Effects"
             : section == ModuleSection::Utility ? "Utilities"
             : juce::String (sectionName (section));
    }
}

juce::Colour SidebarComponent::textColour()
{
    const auto& t = themeColours();
    return t.textOn (t.get (ThemeColours::Sidebar), 0.6f);
}

//==============================================================================
// SidebarHeader
//==============================================================================
SidebarHeader::SidebarHeader()
{
    refreshColours();
    search.setFont (juce::Font (juce::FontOptions (13.0f)));
    search.setIndents (8, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.setSelectAllWhenFocused (true);
    search.onTextChange = [this]
    {
        repaint();
        if (onSearchChanged != nullptr)
            onSearchChanged (search.getText());
    };
    search.onEscapeKey = [this] { clearSearch(); };
    addAndMakeVisible (search);

    // MPE, beside the search: every finger on the keys its own note, which
    // bends, slides and presses on its own (and MPE controllers the same)
    mpeButton.setClickingTogglesState (true);
    mpeButton.setTooltip ("MPE on: on the keys every finger is its own note - slide sideways to bend it, up and down to "
                          "slide (CC 74), press harder where the screen tells; MPE controllers play the same way. "
                          "An MPE module gives each note's Slide, Pressure and Bend as cables");
    mpeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3fbf7f));
    mpeButton.onClick = [this] { if (onMpeChanged) onMpeChanged (mpeButton.getToggleState()); };
    addAndMakeVisible (mpeButton);
    startTimerHz (4);
}

void SidebarHeader::refreshColours()
{
    // the search field in the sidebar's colours: a shade off its background
    const auto bg = themeColours().get (ThemeColours::Sidebar);
    const auto text = SidebarComponent::textColour();
    const auto field = bg == ThemeColours::defaultColour (ThemeColours::Sidebar) ? juce::Colour (0xff1e1c1a)   // as it always was
                     : bg.getPerceivedBrightness() > 0.5f ? bg.darker (0.12f) : bg.brighter (0.15f);
    search.setTextToShowWhenEmpty ("Search modules", text.withAlpha (0.45f));
    search.setColour (juce::TextEditor::backgroundColourId, field);
    search.setColour (juce::TextEditor::outlineColourId, text.withAlpha (0.25f));
    search.setColour (juce::TextEditor::focusedOutlineColourId, themeColours().get (ThemeColours::Oscillator));
    search.setColour (juce::TextEditor::textColourId, text);
    search.applyColourToAllText (text);
    mpeButton.setColour (juce::TextButton::buttonColourId, field);
    mpeButton.setColour (juce::TextButton::textColourOffId, text);
    repaint();
}

void SidebarHeader::timerCallback()
{
    if (getPatchName == nullptr)
        return;
    // the patch's name, and while an MTS-ESP master tunes the synth, its scale
    auto name = getPatchName();
    if (aquanode::tuning::hasMaster())
    {
        const juce::String scale (aquanode::tuning::scaleName());
        name << "  -  MTS-ESP" << (scale.isNotEmpty() ? ": " + scale : juce::String());
    }
    if (name != patchName)
    {
        patchName = name;
        repaint (infoArea);
    }
}

int SidebarHeader::preferredHeight()
{
   #if JUCE_ANDROID
    return 104;
   #else
    return 80;
   #endif
}

void SidebarHeader::clearSearch()
{
    search.setText ({}, true);
    search.giveAwayKeyboardFocus();
}

void SidebarHeader::resized()
{
    // the name, the info line (patch name, zoom), then a slim search field
   #if JUCE_ANDROID
    const int infoH = 20, searchH = 32;
   #else
    const int infoH = 16, searchH = 22;
   #endif
    auto r = getLocalBounds().reduced (10, 5);
    {
        // the search a little shorter, the MPE switch at its right
        auto row = r.removeFromBottom (searchH);
        mpeButton.setBounds (row.removeFromRight (juce::jmax (46, searchH + 14)));
        row.removeFromRight (6);
        search.setBounds (row);
    }
    clearArea = search.getBounds().withLeft (search.getRight() - search.getHeight());
    r.removeFromBottom (4);
    infoArea = r.removeFromBottom (infoH);
    zoomArea = infoArea.withLeft (infoArea.getRight() - 44);
    logoArea = r;
}

void SidebarHeader::paint (juce::Graphics& g)
{
    g.fillAll (themeColours().get (ThemeColours::Sidebar));

    // a small palette mark on the right: the hint that a tap opens the colours
    auto row = logoArea;
    const float dot = juce::jlimit (7.0f, 12.0f, row.getHeight() * 0.32f);
    auto mark = row.removeFromRight ((int) (dot * 3.4f + 8.0f)).toFloat();
    row.removeFromRight (6);
    const int dots[] = { ThemeColours::Oscillator, ThemeColours::Filter, ThemeColours::Utility, ThemeColours::InputOutput };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (themeColours().get (dots[i]));
        g.fillEllipse (mark.getRight() - dot - (3 - i) * dot * 0.8f, mark.getCentreY() - dot * 0.5f, dot, dot);
    }

    // Skeuomorphic: the water ring before the name
    const bool bright = themeColours().skeuomorphic && skeuo::brightGround (themeColours().get (ThemeColours::Sidebar));
    if (themeColours().skeuomorphic)
    {
        // a little smaller than the row, so it sits with the name's letters
        const int side = juce::jmin ((int) (row.getHeight() * 0.8f), 30);
        auto ringArea = row.removeFromLeft (side + 2).toFloat();
        const auto ringBox = ringArea.withSizeKeepingCentre ((float) side, (float) side);
        if (bright)
        {
            // on a bright blue ground the water sits in a bubble of clear glass
            const auto bubble = ringBox.expanded (1.5f);
            juce::ColourGradient glass (juce::Colours::white.withAlpha (0.75f), bubble.getCentreX(), bubble.getY() + bubble.getHeight() * 0.3f,
                                        juce::Colours::white.withAlpha (0.25f), bubble.getCentreX(), bubble.getBottom(), false);
            g.setGradientFill (glass);
            g.fillEllipse (bubble);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawEllipse (bubble.reduced (0.5f), 1.0f);
        }
        drawWaterRing (g, ringBox);
        row.removeFromLeft (4);
    }

    // the name, in the colours of the module types it holds, as big as fits on one line
    const juce::String words[] = { "Aquanode", " Modular" };
    float size = (float) juce::jmin (22, row.getHeight() - 4);
    for (; size > 8.0f; size -= 0.5f)
    {
        const float w = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (size, juce::Font::bold)), words[0])
                      + juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (size)), words[1]);
        if (w <= (float) row.getWidth() - 2.0f)
            break;
    }
    if (themeColours().skeuomorphic)
    {
        // "Aquanode" in water, glowing; "Modular" in the sidebar's text colour
        const auto bold = juce::Font (juce::FontOptions (size, juce::Font::bold));
        const float w0 = juce::GlyphArrangement::getStringWidth (bold, words[0]);
        auto first = row.toFloat().withWidth (w0 + 2.0f);
        g.setFont (bold);
        if (bright)
        {
            // glossy white into ice on the azure, over a soft shadow
            g.setColour (juce::Colour (0xff003c78).withAlpha (0.35f));
            g.drawText (words[0], first.translated (0.0f, 1.2f), juce::Justification::centredLeft, false);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, first.getX(), first.getY(),
                                                     juce::Colour (0xffb4f6ff), first.getX(), first.getBottom(), false));
        }
        else
        {
            g.setColour (skeuo::glow.withAlpha (0.18f));
            for (auto d : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
                g.drawText (words[0], first.translated (d.x, d.y), juce::Justification::centredLeft, false);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff27f0ff), first.getX(), first.getY(),
                                                     juce::Colour (0xff1a8cff), first.getRight(), first.getBottom(), false));
        }
        g.drawText (words[0], first, juce::Justification::centredLeft, false);
        g.setFont (juce::Font (juce::FontOptions (size)));
        g.setColour (SidebarComponent::textColour().withAlpha (0.85f));
        g.drawText (words[1], row.toFloat().withTrimmedLeft (w0), juce::Justification::centredLeft, false);
    }
    else
    {
        juce::AttributedString name;
        name.setJustification (juce::Justification::centredLeft);
        name.setWordWrap (juce::AttributedString::none);
        auto first = themeColours().get (ThemeColours::Oscillator).brighter (0.25f);
        if (skeuo::contrast (first, themeColours().get (ThemeColours::Sidebar)) < 2.0f)
            first = SidebarComponent::textColour();   // (cyan on a cyan sidebar would vanish)
        name.append (words[0], juce::Font (juce::FontOptions (size, juce::Font::bold)), first);
        name.append (words[1], juce::Font (juce::FontOptions (size)), SidebarComponent::textColour().withAlpha (0.85f));
        name.draw (g, row.toFloat());
    }

    // the loaded patch's name on the left, the zoom on the right
    {
        const auto text = SidebarComponent::textColour();
       #if JUCE_ANDROID
        const float fs = 13.0f;
       #else
        const float fs = 11.0f;
       #endif
        g.setFont (juce::Font (juce::FontOptions (fs)));
        g.setColour (text.withAlpha (0.65f));
        g.drawText (zoomText, zoomArea, juce::Justification::centredRight);
        g.setColour (text.withAlpha (0.9f));
        g.drawText (patchName, infoArea.withRight (zoomArea.getX() - 4), juce::Justification::centredLeft, true);
    }

    // the search's clear cross, while there is something to clear
    if (search.getText().isNotEmpty())
    {
        const auto c = clearArea.toFloat().getCentre();
        g.setColour (SidebarComponent::textColour().withAlpha (0.6f));
        g.drawLine (c.x - 4.0f, c.y - 4.0f, c.x + 4.0f, c.y + 4.0f, 1.5f);
        g.drawLine (c.x - 4.0f, c.y + 4.0f, c.x + 4.0f, c.y - 4.0f, 1.5f);
    }
}

void SidebarHeader::mouseUp (const juce::MouseEvent& e)
{
    if (logoArea.contains (e.getPosition()) && onLogoClicked != nullptr)
        onLogoClicked();
    else if (zoomArea.contains (e.getPosition()) && onZoomClicked != nullptr)
        onZoomClicked();
}

//==============================================================================
SidebarComponent::SidebarComponent (std::function<void (const juce::String&, bool)> onModuleClicked)
    : moduleClicked (std::move (onModuleClicked))
{
    buildRows();
    startTimerHz (10);   // hover watchdog; the wait itself is timed in ms
}

SidebarComponent::~SidebarComponent()
{
    hideTooltip();
}

const SidebarComponent::Row* SidebarComponent::rowAt (juce::Point<int> pos) const
{
    for (const auto& row : rows)
        if (row.typeId.isNotEmpty() && row.area.contains (pos))
            return &row;
    return nullptr;
}

void SidebarComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto* row = rowAt (e.getPosition());
    const auto typeId = row != nullptr ? row->typeId : juce::String();

    // moving to another module, or off the list, starts the wait over; a few
    // pixels of drift within the same row does not
    if (typeId != hoveredTypeId
        || e.getPosition().getDistanceFrom (hoverPos) > hoverSlackPx)
    {
        if (typeId != hoveredTypeId)
            hideTooltip();

        hoveredTypeId = typeId;
        hoverPos = e.getPosition();
        hoverStartMs = juce::Time::getMillisecondCounter();
    }
}

void SidebarComponent::mouseExit (const juce::MouseEvent&)
{
    hoveredTypeId = {};
    hideTooltip();
}

void SidebarComponent::timerCallback()
{
    if (tooltip != nullptr || hoveredTypeId.isEmpty())
        return;

    // the mouse may have left without a mouseExit (dragged away, another
    // window took over): if it is no longer over the row, drop the hover
    if (! isMouseOver (true))
    {
        hoveredTypeId = {};
        return;
    }

    if (juce::Time::getMillisecondCounter() - hoverStartMs < (juce::uint32) hoverDelayMs)
        return;

    for (const auto& row : rows)
        if (row.typeId == hoveredTypeId && row.area.contains (hoverPos))
        {
            showTooltipFor (row);
            return;
        }
}

void SidebarComponent::showTooltipFor (const Row& row)
{
    juce::String title, body;
    if (row.isToggle)
    {
        title = "With Mutation";
        body = "When on, every patch Random Preset builds is then run through the Mutator: three "
               "mutations in a row at 50 %, so the sound wanders further from the engine's own. The "
               "Mutator's Undo brings back the patch as it was built.";
    }
    else if (const auto* gen = aquanode::presets::findGenerator (row.typeId))
    {
        title = gen->tooltipTitle;
        body = gen->tooltipBody;
    }
    else if (const auto* adder = aquanode::presets::findEffectAdder (row.typeId))
    {
        title = "Add " + adder->label;
        body = adder->tooltipBody;
    }
    else
    {
        auto* reg = ModuleFactory::instance().find (row.typeId);
        if (reg == nullptr || reg->descriptor.description.isEmpty())
            return;
        title = reg->descriptor.displayName;
        body = reg->descriptor.description;
    }

    auto* top = getTopLevelComponent();
    if (top == nullptr)
        return;

    // the window may draw the editor scaled (desktop: a resized window): the
    // tooltip, a child of the window itself, takes the same scale
    const float sc = juce::jmax (0.1f, juce::Component::getApproximateScaleFactorForComponent (this)
                                           / juce::Component::getApproximateScaleFactorForComponent (top));
    const int topW = juce::roundToInt ((float) top->getWidth() / sc), topH = juce::roundToInt ((float) top->getHeight() / sc);

    tooltip = std::make_unique<SidebarTooltip>();
    tooltip->setText (title, body, juce::jmin (340, juce::jmax (200, topW - sidebarWidth - 24)));

    // sit right of the sidebar, vertically centred on the row, and keep
    // the whole panel on screen - the long entries are taller than a row
    // (worked out in the editor's unscaled units)
    const auto rowTopLeft = top->getLocalPoint (this, row.area.getTopLeft()).toFloat() / sc;
    const int x = juce::jmin (sidebarWidth + 6, topW - tooltip->getWidth() - 6);
    const int y = juce::jlimit (6, juce::jmax (6, topH - tooltip->getHeight() - 6),
                                juce::roundToInt (rowTopLeft.y) - tooltip->getHeight() / 2 + row.area.getHeight() / 2);

    tooltip->setTopLeftPosition (x, y);
    tooltip->setTransform (juce::AffineTransform::scale (sc));
    top->addAndMakeVisible (tooltip.get());
    tooltip->toFront (false);
}

void SidebarComponent::hideTooltip()
{
    tooltip.reset();
}

int SidebarComponent::layoutWidth() const
{
    if (auto* vp = findParentComponentOfClass<juce::Viewport>(); vp != nullptr && vp->getWidth() > 0)
        return juce::jmax (160, vp->getWidth() - juce::jmax (scrollBarWidth, vp->getScrollBarThickness()));
    return sidebarWidth - scrollBarWidth;
}

void SidebarComponent::buildRows()
{
    rows.clear();
    int y = 10;

    // leave room on the right for the Viewport's scrollbar so it never covers text;
    // as wide as the sidebar shows (on Android it is as wide as the screen allows)
    const int contentWidth = layoutWidth();

    if (search.isNotEmpty())
    {
        buildSearchRows (y);
        return;
    }

    // Every category is a header that folds its list away (all start folded,
    // so the sidebar stays short); a tap opens or closes it. The plugins sit in
    // their own group right below the category they belong to.
    auto header = [&] (const juce::String& key, const juce::String& text, juce::Colour colour)
    {
        Row r { { 10, y, contentWidth - 20, 22 }, text, {}, colour };
        r.headerKey = key;
        rows.push_back (r);
        y += 26;
        return expandedHeaders.contains (key);
    };

    // a list of modules with its groups' names between them (only when there is more than one group)
    auto emitGrouped = [&] (const std::vector<const RegisteredModule*>& list, juce::Colour labelColour)
    {
        juce::StringArray groups;
        for (const auto* r : list)
            groups.addIfNotAlreadyThere (groupOf (r->descriptor));
        juce::String last;
        for (const auto* r : list)
        {
            const auto g = groupOf (r->descriptor);
            if (groups.size() > 1 && g != last)
            {
                Row label { { 16, y + 3, contentWidth - 26, 16 }, g, {}, labelColour };
                label.isSubLabel = true;
                rows.push_back (label);
                y += 21;
                last = g;
            }
            rows.push_back ({ { 22, y, contentWidth - 32, 18 },
                              r->descriptor.displayName, r->descriptor.typeId, juce::Colours::white });
            y += 20;
        }
    };

    struct PluginGroup
    {
        ModuleSection section;
        juce::String title;
        std::vector<const RegisteredModule*> rows;
    };
    std::vector<PluginGroup> pluginGroups;

    for (const auto section : allSections())
    {
        const juce::String title = sectionTitle (section);

        // the categories keep their fixed running order (I/O first, then sound
        // generation, ...); inside each, the modules that do the same kind of
        // thing are grouped ("Envelopes", "Reverbs"), and listed A-Z. The
        // modules the plugins are taken apart into ("Aquanova - Oscillator")
        // get a category of their own further down.
        std::vector<const RegisteredModule*> plain, fromPlugins;
        for (const auto& r : ModuleFactory::instance().all())
        {
            if (r.descriptor.section != section || ! r.descriptor.listedInSidebar)
                continue;
            if (r.descriptor.isPluginModule())
                fromPlugins.push_back (&r);
            else
                plain.push_back (&r);
        }

        // by name - or, for a module listed after another, right behind that one
        auto sortName = [] (const ModuleDescriptor& d)
        {
            if (d.listedAfter.isNotEmpty())
                if (auto* after = ModuleFactory::instance().find (d.listedAfter))
                    return after->descriptor.displayName + " ";
            return d.displayName;
        };
        auto byName = [sortName] (const RegisteredModule* a, const RegisteredModule* b)
        {
            return sortName (a->descriptor).compareNatural (sortName (b->descriptor)) < 0;
        };
        auto byGroup = [section, byName] (const RegisteredModule* a, const RegisteredModule* b)
        {
            const auto ga = groupOf (a->descriptor), gb = groupOf (b->descriptor);
            const int oa = groupOrder (section, ga), ob = groupOrder (section, gb);
            if (oa != ob) return oa < ob;
            if (ga != gb) return ga.compareNatural (gb) < 0;
            return byName (a, b);
        };
        std::sort (plain.begin(), plain.end(), byGroup);
        std::sort (fromPlugins.begin(), fromPlugins.end(), byGroup);

        if (header ("section." + juce::String ((int) section), title, sectionColour (section)))
        {
            emitGrouped (plain, sectionColour (section));
            y += 8;
        }

        if (! fromPlugins.empty())
            pluginGroups.push_back ({ section, title, std::move (fromPlugins) });
    }

    // Plugins: the plugins merged into Aquanode Modular, all in one block.
    // Tapping a name (or its GUI pill) adds the original plugin as one card;
    // the Patch pill opens a menu that builds it out of its own modules instead.
    if (header ("plugins", "Plugins", pluginSectionColour()))
    {
        // A-Z, like every list in the sidebar
        std::vector<const aquanode::plugins::CatalogEntry*> sortedPlugins;
        for (const auto& entry : aquanode::plugins::catalog())
            sortedPlugins.push_back (&entry);
        std::sort (sortedPlugins.begin(), sortedPlugins.end(), [] (auto* a, auto* b)
                   { return a->name.compareNatural (b->name) < 0; });

        for (const auto* entryPtr : sortedPlugins)
        {
            const auto& entry = *entryPtr;
            if (ModuleFactory::instance().find (entry.guiTypeId) == nullptr)
                continue;   // its sources are not part of this build

            Row row { { 14, y, contentWidth - 24, 22 }, entry.name, entry.guiTypeId, juce::Colours::white };
            row.isPlugin = true;
            row.pluginHasPatch = entry.hasPatchVersion();
            if (auto* reg = ModuleFactory::instance().find (entry.guiTypeId))
                row.colour = reg->descriptor.colour();
            rows.push_back (row);
            y += 25;
        }
        y += 8;
    }

    // then, after the Plugins block, the same categories for the modules the
    // plugins are taken apart into, in the same order
    for (auto& group : pluginGroups)
    {
        const juce::String pluginTitle = group.section == ModuleSection::Oscillator ? "Plugin Generators"
                                       : "Plugin " + group.title;
        if (header ("plugins." + juce::String ((int) group.section), pluginTitle, pluginSectionColour()))
        {
            emitGrouped (group.rows, pluginSectionColour());
            y += 8;
        }
    }

    y += 6;

    // Preset Generation: one row per patch generator. Each REPLACES the whole
    // patch; the instrument and the FX build both offer them (an FX build
    // plays a generated patch from the on-screen keys or the track's MIDI).
    {
        // the With Mutation toggle, at the right end of the title row (listed
        // before the header, so a tap on it is the toggle's)
        Row toggle { { contentWidth - 14 - 72, y + 2, 72, 18 }, "With Mutation", "toggle.withMutation",
                     presetSectionColour() };
        toggle.isToggle = true;
        rows.push_back (toggle);
    }
    const auto& gens = aquanode::presets::generators();
    if (header ("presets", "Random Preset", presetSectionColour()))
    {
        // Random (the generators' last) first, as a button of its own, then
        // the categories A-Z
        auto genRow = [&] (size_t i, bool apart)
        {
            Row row { { 14, y, contentWidth - 28, 24 }, gens[i].label, gens[i].id, presetSectionColour() };
            row.isGenerator = true;
            row.hasMenu = gens[i].hasMenu();
            row.centreFullWidth = apart;
            rows.push_back (row);
            y += apart ? 33 : 29;
        };
        if (! gens.empty())
            genRow (gens.size() - 1, true);
        for (size_t i = 0; i + 1 < gens.size(); ++i)
            genRow (i, false);
        y += 4;
    }

    // Add Effect: rows that ADD to the loaded patch instead of replacing it -
    // effects in front of Audio Out, Midi Add and Chord at the start
    if (header ("addEffect", "Add Effect", presetSectionColour()))
    {
        for (const auto& adder : aquanode::presets::effectAdders())
        {
            Row add { { 14, y, contentWidth - 28, 24 }, adder.label, adder.id, presetSectionColour() };
            add.isGenerator = true;
            add.centreFullWidth = true;
            rows.push_back (add);
            y += 29;
        }
    }

    y += 10;

    // the Modulation table, after a line
    footerLineY = -1;
    if (footer != nullptr)
    {
        footerLineY = y + 4;
        y += 14;
        const int visible = contentWidth;
        footerLineWidth = visible - 20;
        footer->setBounds (10, y, visible - 20, footer->getHeight());
        footer->setVisible (true);
        y += footer->getHeight() + 30;   // room under it, so its last line scrolls fully into view
    }

    // size ourselves to fit every row; the parent Viewport takes care of clipping + scrolling
    setSize (contentWidth, y);
}

// Every module and plugin that matches the search: all its words found in the
// name first, then in the group, category or type, then in the description.
void SidebarComponent::buildSearchRows (int y)
{
    const int contentWidth = layoutWidth();
    juce::StringArray words;
    words.addTokens (search.toLowerCase(), " ", "\"");
    words.removeEmptyStrings();

    auto score = [&words] (const juce::String& name, const juce::String& more, const juce::String& description)
    {
        const auto n = name.toLowerCase(), m = more.toLowerCase(), d = description.toLowerCase();
        int worst = 0;
        for (const auto& w : words)
        {
            if (n.contains (w))      continue;
            else if (m.contains (w)) worst = juce::jmax (worst, 1);
            else if (d.contains (w)) worst = juce::jmax (worst, 2);
            else return -1;
        }
        return worst;
    };

    struct Hit { int score; Row row; };
    std::vector<Hit> hits;

    for (const auto& entry : aquanode::plugins::catalog())
    {
        auto* reg = ModuleFactory::instance().find (entry.guiTypeId);
        if (reg == nullptr)
            continue;
        const int sc = score (entry.name, "plugin", reg->descriptor.description);
        if (sc < 0)
            continue;
        Row row { {}, entry.name, entry.guiTypeId, reg->descriptor.colour() };
        row.isPlugin = true;
        row.pluginHasPatch = entry.hasPatchVersion();
        hits.push_back ({ sc, row });
    }

    for (const auto& r : ModuleFactory::instance().all())
    {
        const auto& d = r.descriptor;
        if (! d.listedInSidebar)
            continue;
        const auto group = groupOf (d);
        const int sc = score (d.displayName, group + " " + sectionTitle (d.section) + " " + d.typeId, d.description);
        if (sc < 0)
            continue;
        Row row { {}, d.displayName, d.typeId, juce::Colours::white };
        row.detail = d.isPluginModule() ? sectionTitle (d.section) : group;
        hits.push_back ({ sc, row });
    }

    // what only the descriptions mention is listed only when nothing is named so
    if (std::any_of (hits.begin(), hits.end(), [] (const Hit& h) { return h.score < 2; }))
        hits.erase (std::remove_if (hits.begin(), hits.end(), [] (const Hit& h) { return h.score >= 2; }), hits.end());

    std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b)
    {
        if (a.score != b.score) return a.score < b.score;
        return a.row.text.compareNatural (b.row.text) < 0;
    });

    rows.push_back ({ { 10, y, contentWidth - 20, 22 },
                      hits.empty() ? juce::String ("Nothing found") : juce::String (hits.size()) + (hits.size() == 1 ? " match" : " matches"),
                      {}, juce::Colour (0xff4fc3f7) });
    y += 28;

    for (auto& h : hits)
    {
        h.row.area = h.row.isPlugin ? juce::Rectangle<int> (14, y, contentWidth - 24, 22)
                                    : juce::Rectangle<int> (18, y, contentWidth - 28, 18);
        y += h.row.isPlugin ? 25 : 20;
        rows.push_back (h.row);
    }
    footerLineY = -1;
    if (footer != nullptr)
        footer->setVisible (false);
    setSize (contentWidth, y + 20);
}

void SidebarComponent::paint (juce::Graphics& g)
{
    g.fillAll (themeColours().get (ThemeColours::Sidebar));
    const auto text = textColour();
    if (footerLineY >= 0)
    {
        g.setColour (text.withAlpha (0.35f));
        g.fillRect (10, footerLineY, footerLineWidth, 1);
    }

    // a first tap (touch screens): the row waits for the second one
    if (pendingTapKey.isNotEmpty() && juce::Time::getMillisecondCounter() - pendingTapMs < (juce::uint32) doubleTapMs)
        for (const auto& row : rows)
            if (row.typeId.isNotEmpty() && pendingTapKey.startsWith (row.typeId + "|"))
            {
                g.setColour (text.withAlpha (0.16f));
                g.fillRoundedRectangle (row.area.toFloat().expanded (2.0f, 1.0f), 4.0f);
            }

    // Skeuomorphic: the section headers on bars of tinted glass and the group
    // names on a lighter shading (drawn first: a header's switch sits on its bar)
    const bool aero = themeColours().skeuomorphic;
    if (aero)
        for (const auto& row : rows)
        {
            if (row.headerKey.isNotEmpty())
                skeuo::glassBar (g, row.area.toFloat().expanded (3.0f, 1.0f), row.colour == juce::Colours::white ? text : row.colour, 7.0f);
            else if (row.isSubLabel)
                skeuo::glassStrip (g, row.area.toFloat().expanded (4.0f, 1.0f), row.colour);
        }

    for (const auto& row : rows)
    {
        if (row.isToggle)
        {
            const auto r = row.area.toFloat().reduced (0.5f);
            const bool on = withMutation;
            if (on)
            {
                g.setColour (row.colour);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (juce::Colours::black);
            }
            else
            {
                g.setColour (row.colour.withAlpha (0.55f));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                g.setColour (text.withAlpha (0.6f));
            }
            g.setFont (juce::Font (juce::FontOptions (10.5f,
                                                      on ? juce::Font::bold : juce::Font::plain)));
            g.drawText (row.text, row.area, juce::Justification::centred);
            continue;
        }

        if (row.isGenerator)
        {
            auto r = row.area.toFloat();
            if (aero)
            {
                skeuo::glassBar (g, r, row.colour, 6.0f);
            }
            else
            {
                g.setColour (row.colour.withAlpha (0.20f));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (row.colour);
                g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.2f);
            }

            // the menu arrow, set off by a thin divider (rows with a choice only)
            auto textArea = row.area.withTrimmedLeft (6);
            if (row.hasMenu)
            {
                const auto arrowArea = generatorMenuArea (row).toFloat();
                g.setColour (row.colour.withAlpha (0.55f));
                g.drawVerticalLine ((int) arrowArea.getX(), r.getY() + 5.0f, r.getBottom() - 5.0f);
                juce::Path tri;
                const auto c = arrowArea.getCentre();
                tri.addTriangle (c.x - 5.0f, c.y - 2.5f, c.x + 5.0f, c.y - 2.5f, c.x, c.y + 3.5f);
                g.setColour (text.withAlpha (0.85f));
                g.fillPath (tri);
                textArea = textArea.withTrimmedRight (30);
            }
            else
            {
                // keep the names of the category rows lined up, menu or not
                textArea = textArea.withTrimmedRight (row.centreFullWidth ? 6 : 30);
            }

            // the name, and the ticked engine (if any) small and dim after it
            juce::String ticked;
            if (const auto* gen = aquanode::presets::findGenerator (row.typeId))
                if (gen->selection != nullptr)
                    ticked = gen->selection();

            juce::AttributedString label;
            label.setJustification (juce::Justification::centred);
            label.setWordWrap (juce::AttributedString::none);
            label.append (row.text, juce::Font (juce::FontOptions (12.5f, juce::Font::bold)), text);
            if (ticked.isNotEmpty())
                label.append ("  " + ticked, juce::Font (juce::FontOptions (11.0f)), text.withAlpha (0.55f));
            label.draw (g, textArea.toFloat());
            continue;
        }

        if (row.isPlugin)
        {
            // accent dot, name, and the GUI / Patch pills
            const auto dot = juce::Rectangle<float> (5.0f, 5.0f)
                                 .withCentre ({ (float) row.area.getX() + 3.5f, (float) row.area.getCentreY() });
            g.setColour (row.colour);
            g.fillEllipse (dot);

            g.setColour (text);
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            g.drawText (row.text, row.area.withTrimmedLeft (11).withTrimmedRight (88),
                        juce::Justification::centredLeft);

            auto pill = [&g, &row, &text] (juce::Rectangle<int> area, const juce::String& label, bool enabled)
            {
                const auto r = area.toFloat().reduced (0.5f);
                g.setColour (row.colour.withAlpha (enabled ? 0.25f : 0.08f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (row.colour.withAlpha (enabled ? 0.9f : 0.3f));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                g.setColour (text.withAlpha (enabled ? 0.95f : 0.35f));
                g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
                g.drawText (label, area, juce::Justification::centred);
            };
            pill (pluginGuiPillArea (row), "GUI", true);
            pill (pluginPatchPillArea (row), "Patch", row.pluginHasPatch);
            continue;
        }

        if (row.isSubLabel)
        {
            auto colour = aero ? row.colour.interpolatedWith (juce::Colours::white, 0.4f) : row.colour.withAlpha (0.8f);
            const auto area = aero ? row.area.withTrimmedLeft (2) : row.area;
            const auto ground = themeColours().get (ThemeColours::Sidebar);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            if (skeuo::contrast (ground.overlaidWith (colour), ground) < 3.0f)
            {
                // a tint that would vanish on its ground: the sidebar's text (on the glass lifted by a shadow)
                colour = text.withAlpha (aero ? 1.0f : 0.85f);
                if (aero)
                {
                    g.setColour (juce::Colour (0xff003c78).withAlpha (0.3f));
                    g.drawText (row.text.toUpperCase(), area.translated (0, 1), juce::Justification::centredLeft);
                }
            }
            g.setColour (colour);
            g.drawText (row.text.toUpperCase(), area, juce::Justification::centredLeft);
            continue;
        }

        const bool isTitle = row.typeId.isEmpty();
        auto rowColour = row.colour == juce::Colours::white ? text : row.colour;
        if (skeuo::contrast (rowColour, themeColours().get (ThemeColours::Sidebar)) < 2.2f)
            rowColour = text;   // a section colour that would vanish on the sidebar (cyan on cyan)
        if (row.detail.isNotEmpty())
        {
            g.setColour (text.withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (row.detail, row.area, juce::Justification::centredRight);
        }
        g.setColour (rowColour);
        g.setFont (juce::Font (juce::FontOptions (isTitle ? 14.0f : 12.5f,
                                                  isTitle ? juce::Font::bold : juce::Font::plain)));
        if (row.headerKey.isNotEmpty())
        {
            // a fold arrow: pointing right when folded, down when open
            const bool open = expandedHeaders.contains (row.headerKey);
            const auto c = juce::Point<float> ((float) row.area.getX() + 5.0f, (float) row.area.getCentreY());
            juce::Path tri;
            if (open) tri.addTriangle (c.x - 4.5f, c.y - 2.5f, c.x + 4.5f, c.y - 2.5f, c.x, c.y + 3.5f);
            else      tri.addTriangle (c.x - 2.5f, c.y - 4.5f, c.x - 2.5f, c.y + 4.5f, c.x + 3.5f, c.y);
            if (aero)
            {
                // on the glass: the text's colour, lifted by a soft shadow
                const auto shadow = text.getPerceivedBrightness() > 0.5f ? juce::Colours::black.withAlpha (0.45f)
                                                                          : juce::Colours::white.withAlpha (0.45f);
                g.setColour (shadow);
                g.fillPath (tri, juce::AffineTransform::translation (0.0f, 1.0f));
                g.drawText (row.text, row.area.withTrimmedLeft (16).translated (0, 1), juce::Justification::centredLeft);
                g.setColour (text);
            }
            g.fillPath (tri);
            g.drawText (row.text, row.area.withTrimmedLeft (16), juce::Justification::centredLeft);
            continue;
        }
        auto nameArea = row.area;
        if (row.detail.isNotEmpty())
            nameArea.removeFromRight (juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (10.0f)), row.detail) + 6);
        g.drawFittedText (row.text, nameArea, juce::Justification::centredLeft, 1, 0.85f);
    }
}

void SidebarComponent::mouseUp (const juce::MouseEvent& e)
{
    // a finger that moved was scrolling the list, not choosing from it
    if (e.mouseWasDraggedSinceMouseDown())
        return;

    // a category header folds or opens its list (unless the tap is on a
    // switch sitting in the header's row)
    for (const auto& row : rows)
        if (row.headerKey.isNotEmpty() && row.area.contains (e.getPosition()))
        {
            const bool onToggle = std::any_of (rows.begin(), rows.end(), [&e] (const Row& r)
                                               { return r.isToggle && r.area.contains (e.getPosition()); });
            if (onToggle)
                break;
            if (expandedHeaders.contains (row.headerKey)) expandedHeaders.removeString (row.headerKey);
            else                                          expandedHeaders.add (row.headerKey);
            hideTooltip();
            buildRows();
            repaint();
            if (onExpandedChanged != nullptr)
                onExpandedChanged (expandedHeaders);
            return;
        }

    for (const auto& row : rows)
    {
        if (row.typeId.isNotEmpty() && row.area.contains (e.getPosition()))
        {
            // Touch screens add on tapping twice: a single tap while scrolling
            // the list is too easy to make by accident.
            if (e.source.isTouch() && ! row.isToggle)
            {
                const juce::String part = row.isPlugin ? (pluginPatchPillArea (row).contains (e.getPosition()) ? "patch" : "gui")
                                        : (row.isGenerator && row.hasMenu && generatorMenuArea (row).contains (e.getPosition()) ? "menu" : "row");
                const auto key = row.typeId + "|" + part;
                const auto now = juce::Time::getMillisecondCounter();
                if (key != pendingTapKey || now - pendingTapMs > (juce::uint32) doubleTapMs)
                {
                    pendingTapKey = key;
                    pendingTapMs = now;
                    repaint();
                    juce::Component::SafePointer<SidebarComponent> safe (this);
                    juce::Timer::callAfterDelay (doubleTapMs + 20, [safe] { if (safe != nullptr) safe->repaint(); });
                    return;
                }
                pendingTapKey.clear();
                repaint();
            }

            // the module is being added; the description has served its purpose
            hideTooltip();
            hoverStartMs = juce::Time::getMillisecondCounter();

            if (row.isToggle)
            {
                withMutation = ! withMutation;
                repaint();
                if (onWithMutationChanged != nullptr)
                    onWithMutationChanged (withMutation);
                return;
            }

            if (row.isPlugin)
            {
                const auto pluginId = row.typeId.fromFirstOccurrenceOf ("plugin.", false, false);
                const bool patch = pluginPatchPillArea (row).contains (e.getPosition()) || e.mods.isPopupMenu();
                if (patch && ! row.pluginHasPatch)
                    return;
                if (moduleClicked != nullptr)
                    moduleClicked ((patch ? "pluginpatch:" : "plugingui:") + pluginId, patch);
                return;
            }

            const bool openMenu = row.isGenerator && row.hasMenu
                && (generatorMenuArea (row).contains (e.getPosition()) || e.mods.isPopupMenu());

            if (moduleClicked != nullptr)
                moduleClicked (row.typeId, openMenu);
            return;
        }
    }
}
