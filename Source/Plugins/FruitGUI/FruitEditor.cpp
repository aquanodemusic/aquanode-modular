#include "FruitEditor.h"
#include "../../Editor/Sidebar.h"
#include <map>
#include "../../Platform/AndroidDocuments.h"

using namespace fruitui;

namespace
{
    juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    juce::String two (int n) { return juce::String (n).paddedLeft ('0', 2); }
    const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    // the synths or effects to choose from, as a menu (ids from 1), in the
    // sidebar's own subcategories and order (6.2.1)
    juce::PopupMenu groupedMenu (const std::vector<FruitProcessor::Choice>& list, std::vector<aquanode::ModuleSection> sections,
                                 std::function<bool (const FruitProcessor::Choice&)> take)
    {
        juce::PopupMenu menu;
        juce::StringArray order;
        for (auto sec : sections)
            order.addArray (sidebar_groups::groupNames (sec));
        std::map<juce::String, juce::PopupMenu> groups;
        juce::StringArray seen;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& c = list[(size_t) i];
            if (! take (c)) continue;
            juce::String g = "More";
            if (auto* r = aquanode::ModuleFactory::instance().find (c.typeId))
                g = c.typeId == "fx.externalplugin" ? juce::String ("VST3") : sidebar_groups::groupOf (r->descriptor);
            groups[g].addItem (i + 1, c.name);
            seen.addIfNotAlreadyThere (g);
        }
        for (const auto& g : order)
            if (seen.contains (g)) { menu.addSubMenu (g, groups[g]); seen.removeString (g); }
        for (const auto& g : seen)    // groups the sidebar does not name (by a plugin, by the id)
            menu.addSubMenu (g, groups[g]);
        return menu;
    }

    juce::PopupMenu choiceMenu (const std::vector<FruitProcessor::Choice>& list, bool effects)
    {
        juce::PopupMenu plugins, menu;
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].plugin) plugins.addItem (i + 1, list[(size_t) i].name);
        using S = aquanode::ModuleSection;
        auto isFilter = [] (const FruitProcessor::Choice& c)
        {
            if (auto* r = aquanode::ModuleFactory::instance().find (c.typeId)) return r->descriptor.section == aquanode::ModuleSection::Filter;
            return false;
        };
        if (effects)
        {
            menu.addSubMenu ("Effects", groupedMenu (list, { S::Effect, S::Utility }, [&] (const FruitProcessor::Choice& c) { return ! c.plugin && ! isFilter (c); }));
            menu.addSubMenu ("Filters", groupedMenu (list, { S::Filter }, [&] (const FruitProcessor::Choice& c) { return ! c.plugin && isFilter (c); }));
            menu.addSubMenu ("Effect Plugins", plugins);
        }
        else
        {
            menu.addSubMenu ("Sound Generation", groupedMenu (list, { S::Oscillator, S::Utility }, [] (const FruitProcessor::Choice& c) { return ! c.plugin; }));
            menu.addSubMenu ("Synth Plugins", plugins);
        }
        return menu;
    }
}

//==============================================================================
void fruitui::raised (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour base, float amount)
{
    if (r.getWidth() < 2 || r.getHeight() < 2) return;
    g.setGradientFill (juce::ColourGradient (base.brighter (0.16f * amount), 0, r.getY(), base.darker (0.14f * amount), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.20f * amount));
    g.fillRect (juce::Rectangle<float> (r.getX() + corner, r.getY() + 0.5f, r.getWidth() - 2 * corner, 1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.22f * amount));
    g.fillRect (juce::Rectangle<float> (r.getX() + corner, r.getBottom() - 1.0f, r.getWidth() - 2 * corner, 1.0f));
}

void fruitui::sunken (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour base, float amount)
{
    if (r.getWidth() < 2 || r.getHeight() < 2) return;
    g.setGradientFill (juce::ColourGradient (base.darker (0.22f * amount), 0, r.getY(), base.brighter (0.05f * amount), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.28f * amount));
    g.fillRect (juce::Rectangle<float> (r.getX() + corner, r.getY() + 0.5f, r.getWidth() - 2 * corner, 1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.10f * amount));
    g.fillRect (juce::Rectangle<float> (r.getX() + corner, r.getBottom() - 1.0f, r.getWidth() - 2 * corner, 1.0f));
}

void Window::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (r.translated (0, 2), 4.0f);
    g.setGradientFill (juce::ColourGradient (panel.brighter (0.05f), 0, r.getY(), panel.darker (0.07f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    auto t = titleArea().toFloat();
    g.setGradientFill (juce::ColourGradient (titleBar.brighter (0.14f), 0, t.getY(), titleBar.darker (0.10f), 0, t.getBottom(), false));
    g.fillRoundedRectangle (t, 4.0f);
    g.fillRect (t.withTrimmedTop (6));
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRect (juce::Rectangle<float> (t.getX() + 4, t.getY() + 0.5f, t.getWidth() - 8, 1.0f));
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRect (juce::Rectangle<float> (t.getX(), t.getBottom() - 1.0f, t.getWidth(), 1.0f));
    // FL's little arrow and speaker in front of the title
    g.setColour (light.withAlpha (0.85f));
    juce::Path tri;
    tri.addTriangle (10, 10, 10, 20, 17, 15);
    g.fillPath (tri);
    g.setFont (font (15.0f));
    g.drawText (title, titleArea().withTrimmedLeft (26), juce::Justification::centredLeft);
    if (paintTitleExtras) paintTitleExtras (g, titleArea());
}

void NumberBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (caption.isNotEmpty())
    {
        g.setColour (light.withAlpha (0.8f));
        g.setFont (font (13.0f));
        const float cw = juce::GlyphArrangement::getStringWidth (font (13.0f), caption) + 6;
        g.drawText (caption, r.removeFromLeft (cw), juce::Justification::centredLeft);
    }
    sunken (g, r, 3.0f, juce::Colour (0xff1d4640));
    g.setColour (stepOn.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (light);
    g.setFont (font (15.0f, true));
    g.drawText (two (value), r.withTrimmedRight (14), juce::Justification::centred);
    // up / down arrows
    juce::Path up, dn;
    const float ax = r.getRight() - 9, cy = r.getCentreY();
    up.addTriangle (ax - 4, cy - 2, ax + 4, cy - 2, ax, cy - 7);
    dn.addTriangle (ax - 4, cy + 2, ax + 4, cy + 2, ax, cy + 7);
    g.setColour (stepOn);
    g.fillPath (up);
    g.fillPath (dn);
}

void NumberBox::mouseDown (const juce::MouseEvent&) { startValue = value; dragged = false; }

void NumberBox::mouseDrag (const juce::MouseEvent& e)
{
    const int d = -e.getDistanceFromDragStartY() / 6;
    if (! dragged && std::abs (e.getDistanceFromDragStartY()) < 4) return;
    dragged = true;
    set (startValue + d, true);
}

void NumberBox::mouseUp (const juce::MouseEvent& e)
{
    if (dragged) return;
    // a tap on the arrows steps, a tap on the number steps up
    const bool lower = e.position.y > getHeight() * 0.5f && e.position.x > getWidth() - 16;
    set (value + (lower ? -1 : 1), true);
}

void Button::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    const auto base = on ? (getButtonText() == "Rec" ? juce::Colour (0xffe05560) : stepOn) : (down ? nameBtn.brighter (0.2f) : over ? nameBtn.brighter (0.08f) : nameBtn.darker (0.25f));
    if (down) sunken (g, r, 3.0f, base, 0.8f); else raised (g, r, 3.0f, base);
    g.setColour (on ? darkText : darkText.brighter (0.15f));
    g.setFont (font (14.0f));
    g.drawText (getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
// the card of a loaded synth or effect
//==============================================================================
FruitSlotCard::FruitSlotCard (FruitProcessor& p, int id, const juce::String& t)
    : proc (p), moduleId (id), title (t)
{
    auto* inst = proc.getEngine().getInstance (moduleId);
    if (inst != nullptr)
    {
        const auto& params = inst->descriptor().params;
        const bool isPluginCard = dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get()) != nullptr;
        if (! isPluginCard)
            for (int i = 0; i < (int) params.size(); ++i)
            {
                const auto& ps = params[(size_t) i];
                if (ps.hidden) continue;
                auto c = std::make_unique<Control>();
                c->index = i;
                if (ps.visibleWhenParamId.isNotEmpty())
                    for (int j = 0; j < (int) params.size(); ++j)
                        if (params[(size_t) j].id == ps.visibleWhenParamId) { c->visIndex = j; c->visEquals = ps.visibleWhenEquals; c->visFrom = ps.visibleFromMin; break; }
                c->label = std::make_unique<juce::Label> (juce::String(), ps.label);
                c->label->setJustificationType (juce::Justification::centred);
                c->label->setFont (font (12.0f));
                c->label->setColour (juce::Label::textColourId, light);
                addAndMakeVisible (*c->label);
                const float v = inst->dsp->getParameterBase (i);
                if ((ps.type == aquanode::ParamType::Combo || ps.type == aquanode::ParamType::RotarySteppedList) && ! ps.choices.isEmpty())
                {
                    c->combo = std::make_unique<juce::ComboBox>();
                    for (int k = 0; k < ps.choices.size(); ++k) c->combo->addItem (ps.choices[k], k + 1);
                    c->combo->setSelectedItemIndex (juce::jlimit (0, ps.choices.size() - 1, juce::roundToInt (v - ps.minValue)), juce::dontSendNotification);
                    c->combo->onChange = [this, i, ctrl = c.get(), lo = ps.minValue]
                    {
                        if (auto* m = proc.moduleFor (moduleId))
                            m->setParameterByIndex (i, lo + (float) ctrl->combo->getSelectedItemIndex());
                    };
                    addAndMakeVisible (*c->combo);
                }
                else if (ps.type == aquanode::ParamType::Button)
                {
                    c->toggle = std::make_unique<juce::ToggleButton>();
                    c->toggle->setToggleState (v > 0.5f, juce::dontSendNotification);
                    c->toggle->onClick = [this, i, ctrl = c.get()]
                    {
                        if (auto* m = proc.moduleFor (moduleId)) m->setParameterByIndex (i, ctrl->toggle->getToggleState() ? 1.0f : 0.0f);
                    };
                    addAndMakeVisible (*c->toggle);
                }
                else
                {
                    c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                    c->slider->setRange (ps.minValue, ps.maxValue, ps.interval);
                    if (ps.logarithmic && ps.minValue > 0.0f)
                        c->slider->setSkewFactorFromMidPoint (std::sqrt (ps.minValue * ps.maxValue));
                    c->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
                    c->slider->setTextValueSuffix (ps.unitSuffix.isNotEmpty() ? " " + ps.unitSuffix : juce::String());
                    c->slider->setNumDecimalPlacesToDisplay (ps.maxValue - ps.minValue > 50.0f ? 0 : 2);
                    c->slider->setColour (juce::Slider::rotarySliderFillColourId, stepOn);
                    c->slider->setColour (juce::Slider::textBoxTextColourId, light);
                    c->slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                    c->slider->setValue (v, juce::dontSendNotification);
                    c->slider->onValueChange = [this, i, ctrl = c.get()]
                    {
                        if (auto* m = proc.moduleFor (moduleId)) m->setParameterByIndex (i, (float) ctrl->slider->getValue());
                    };
                    addAndMakeVisible (*c->slider);
                }
                controls.push_back (std::move (c));
            }

        content = inst->dsp->createExtraContentComponent();
        if (content != nullptr)
        {
            contentW = juce::jmax (inst->dsp->preferredModuleWidth(), 300);
            contentH = juce::jmax (inst->dsp->extraContentHeight(), 80);
            addAndMakeVisible (*content);
        }
    }
    knobRows = ((int) controls.size() + kCols - 1) / kCols;
    updateShown();
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    const int w = juce::jmax (kCols * kKnob + 20, contentW + 12, 320);
    const int h = kTitleH + knobRows * kKnobH + (content != nullptr ? contentH + 10 : 0) + 12;
    setSize (w, h);
    startTimerHz (10);
}

FruitSlotCard::~FruitSlotCard() = default;

bool FruitSlotCard::updateShown()
{
    // the card follows visibleWhen like the patch cards: a Sync switch swaps Rate for Division
    auto* m = proc.moduleFor (moduleId);
    if (m == nullptr) return false;
    bool changed = false;
    for (auto& c : controls)
    {
        const bool show = c->visIndex < 0 || (c->visFrom ? juce::roundToInt (m->getParameterBase (c->visIndex)) >= juce::roundToInt (c->visEquals)
                                                             : juce::roundToInt (m->getParameterBase (c->visIndex)) == juce::roundToInt (c->visEquals));
        if (show == c->shown) continue;
        c->shown = show;
        changed = true;
        for (auto* comp : std::initializer_list<juce::Component*> { c->label.get(), c->slider.get(), c->combo.get(), c->toggle.get() })
            if (comp != nullptr) comp->setVisible (show);
    }
    return changed;
}

void FruitSlotCard::timerCallback()
{
    // show what moved the knobs from elsewhere (Knob Record clips, the plugin's own preset)
    auto* m = proc.moduleFor (moduleId);
    auto* inst = proc.getEngine().getInstance (moduleId);
    if (m == nullptr || inst == nullptr) return;
    const auto& params = inst->descriptor().params;
    for (auto& c : controls)
    {
        const float v = m->getParameterBase (c->index);
        if (c->slider != nullptr && ! c->slider->isMouseButtonDown() && std::abs (c->slider->getValue() - v) > 1.0e-6)
            c->slider->setValue (v, juce::dontSendNotification);
        if (c->combo != nullptr && ! c->combo->isPopupActive())
        {
            const auto& ps = params[(size_t) c->index];
            const int k = juce::jlimit (0, juce::jmax (0, c->combo->getNumItems() - 1), juce::roundToInt (v - ps.minValue));
            if (c->combo->getSelectedItemIndex() != k) c->combo->setSelectedItemIndex (k, juce::dontSendNotification);
        }
        if (c->toggle != nullptr && c->toggle->getToggleState() != (v > 0.5f))
            c->toggle->setToggleState (v > 0.5f, juce::dontSendNotification);
    }
    if (updateShown()) resized();
}

void FruitSlotCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (r.translated (2, 3), 5.0f);
    g.setColour (panel.darker (0.15f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (titleBar);
    g.fillRoundedRectangle (r.withHeight ((float) kTitleH), 5.0f);
    g.setColour (light);
    g.setFont (font (14.0f, true));
    g.drawText (title, getLocalBounds().withHeight (kTitleH).withTrimmedLeft (10).withTrimmedRight (36), juce::Justification::centredLeft);
    g.setColour (stepOn.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
}

void FruitSlotCard::resized()
{
    closeButton.setBounds (getWidth() - 30, 3, 26, 22);
    int k = 0;
    for (auto& c : controls)
    {
        if (! c->shown) continue;
        const int col = k % kCols, row = k / kCols;
        auto cell = juce::Rectangle<int> (10 + col * kKnob, kTitleH + 4 + row * kKnobH, kKnob, kKnobH - 12);   // 12 px between rows
        c->label->setBounds (cell.removeFromTop (16));
        if (c->slider) c->slider->setBounds (cell.reduced (2, 0));
        if (c->combo) c->combo->setBounds (cell.withSizeKeepingCentre (kKnob - 6, 24));
        if (c->toggle) c->toggle->setBounds (cell.withSizeKeepingCentre (28, 28));
        ++k;
    }
    if (content != nullptr)
        content->setBounds (6, kTitleH + knobRows * kKnobH + 6, getWidth() - 12, contentH);
}

void FruitSlotCard::mouseDown (const juce::MouseEvent& e) { toFront (true); if (e.y < kTitleH) dragger.startDraggingComponent (this, e); }
void FruitSlotCard::mouseDrag (const juce::MouseEvent& e) { if (e.getMouseDownY() < kTitleH) dragger.dragComponent (this, e, nullptr); }

//==============================================================================
// Channel rack: the 99 synth lanes
//==============================================================================
struct FruitEditor::Rack : public Window
{
    struct Rows : public juce::Component
    {
        FruitEditor& ed;
        explicit Rows (FruitEditor& e) : ed (e) {}
        static constexpr int kRowH = 32;
        void paint (juce::Graphics& g) override
        {
            const auto& p = ed.proc;
            const int keys = p.song.patterns[(size_t) ed.currentPattern].slot;
            const auto clip = g.getClipBounds();
            for (int s = 1; s < fruit::kSlots; ++s)
            {
                auto r = juce::Rectangle<int> (0, (s - 1) * kRowH, getWidth(), kRowH);
                if (! r.intersects (clip)) continue;
                const auto& slot = p.slots[(size_t) s];
                const bool sel = s == keys;
                // the little LED, number, and the name button
                g.setColour (sel ? stepOn : stepOff);
                g.fillEllipse ((float) r.getX() + 8, (float) r.getCentreY() - 4, 8, 8);
                auto num = juce::Rectangle<int> (r.getX() + 22, r.getY() + 4, 34, kRowH - 8);
                raised (g, num.toFloat(), 3.0f, titleBar.interpolatedWith (panel, 0.2f));
                g.setColour (light);
                g.setFont (font (14.0f));
                g.drawText (two (s), num, juce::Justification::centred);
                auto name = juce::Rectangle<int> (r.getX() + 62, r.getY() + 3, getWidth() - 72, kRowH - 6);
                const bool open = std::any_of (ed.cards.begin(), ed.cards.end(), [&slot] (const auto& c) { return c->getModuleId() == slot.synthId && slot.synthId >= 0; });
                if (slot.synthId >= 0) raised (g, name.toFloat(), 3.0f, open ? nameBtn.brighter (0.25f) : nameBtn);
                else sunken (g, name.toFloat(), 3.0f, panel.interpolatedWith (nameBtn, 0.25f), 0.6f);
                if (sel)
                {
                    g.setColour (stepOn);
                    g.drawRoundedRectangle (name.toFloat().reduced (0.5f), 3.0f, 1.5f);
                }
                g.setColour (slot.synthId >= 0 ? darkText : light.withAlpha (0.5f));
                g.setFont (font (14.0f));
                g.drawText (slot.synthId >= 0 ? slot.name : juce::String ("(empty - tap to load a synth)"), name.reduced (8, 0), juce::Justification::centredLeft);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown()) return;
            const int s = juce::jlimit (1, fruit::kSlots - 1, e.y / kRowH + 1);
            auto& p = ed.proc;
            auto& slot = p.slots[(size_t) s];
            if (e.x < 60)       // the number: this slot plays the piano roll's pattern (and the keys)
            {
                p.song.patterns[(size_t) ed.currentPattern].slot = s;
                p.keysSlot.store (s);
                p.songChanged();
                ed.refreshAll();
                return;
            }
            auto choose = [this, s]
            {
                auto list = FruitProcessor::synthChoices();
                choiceMenu (list, false).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                    [this, s, list] (int r)
                    {
                        if (r <= 0) return;
                        ed.closeCards (ed.proc.slots[(size_t) s].synthId);
                        ed.proc.loadSynth (s, list[(size_t) r - 1].typeId);
                        ed.refreshAll();
                    });
            };
            if (slot.synthId < 0) { choose(); return; }
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem (1, "Open / Close");
                m.addItem (2, "Replace...");
                m.addItem (3, "Remove");
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, s, choose] (int r)
                {
                    auto& sl = ed.proc.slots[(size_t) s];
                    if (r == 1) ed.openCard (sl.synthId, two (s) + "  " + sl.name);
                    if (r == 2) choose();
                    if (r == 3) { ed.closeCards (sl.synthId); ed.proc.clearSlot (s); ed.refreshAll(); }
                });
                return;
            }
            ed.openCard (slot.synthId, two (s) + "  " + slot.name);
            repaint();
        }
    };

    explicit Rack (FruitEditor& e) : rows (e)
    {
        title = "Channel rack";
        viewport.setViewedComponent (&rows, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (10);
        addAndMakeVisible (viewport);
    }
    void resized() override
    {
        viewport.setBounds (body().reduced (6, 6));
        rows.setSize (viewport.getWidth() - viewport.getScrollBarThickness(), Rows::kRowH * (fruit::kSlots - 1));
    }
    Rows rows;
    juce::Viewport viewport;
};

//==============================================================================
// Piano roll
//==============================================================================
struct FruitEditor::Roll : public Window
{
    struct Grid : public juce::Component
    {
        FruitEditor& ed;
        explicit Grid (FruitEditor& e) : ed (e) {}
        static constexpr int kKeysW = 46;
        float rowH = 12.0f, pxPerBeat = 60.0f;
        float topPitch = 84.0f;     // the pitch at the top of the view
        double scrollBeats = 0.0;
        double snap = 0.25, lastLength = 0.25;
        int dragNote = -1, dragMode = 0;   // 1 move, 2 resize
        double grabBeat = 0, grabStart = 0, grabLength = 0;
        int grabPitch = 0, grabNotePitch = 0;

        fruit::Pattern& pat() { return ed.proc.song.patterns[(size_t) ed.currentPattern]; }
        float yOf (int pitch) const { return (topPitch - (float) pitch) * rowH; }
        int pitchAt (float y) const { return juce::jlimit (0, 127, (int) std::ceil (topPitch - y / rowH)); }
        float xOf (double beat) const { return (float) kKeysW + (float) ((beat - scrollBeats) * pxPerBeat); }
        double beatAt (float x) const { return juce::jmax (0.0, (x - kKeysW) / pxPerBeat + scrollBeats); }
        double snapped (double b) const { return std::floor (b / snap + 1.0e-6) * snap; }

        int noteAt (juce::Point<float> p)
        {
            auto& notes = pat().notes;
            for (int i = (int) notes.size(); --i >= 0;)
            {
                const auto& n = notes[(size_t) i];
                juce::Rectangle<float> r (xOf (n.start), yOf (n.pitch), (float) (n.length * pxPerBeat), rowH);
                if (r.expanded (0, 1).contains (p)) return i;
            }
            return -1;
        }

        void paint (juce::Graphics& g) override
        {
            auto& p = pat();
            g.fillAll (grid);
            const int lo = pitchAt ((float) getHeight()), hi = pitchAt (0);
            for (int pitch = lo; pitch <= hi; ++pitch)
            {
                const bool black = juce::String (kNoteNames[pitch % 12]).contains ("#");
                g.setColour (black ? grid.darker (0.18f) : gridAlt);
                g.fillRect (juce::Rectangle<float> ((float) kKeysW, yOf (pitch), (float) getWidth(), rowH));
                g.setColour (gridLine.darker (0.2f));
                g.drawHorizontalLine ((int) yOf (pitch), (float) kKeysW, (float) getWidth());
            }
            // beats and bars
            const double endBeat = beatAt ((float) getWidth());
            for (double b = std::floor (scrollBeats); b <= endBeat; b += 0.25)
            {
                const bool bar = std::fmod (b, 4.0) < 1.0e-6, beat = std::fmod (b, 1.0) < 1.0e-6;
                g.setColour (bar ? light.withAlpha (0.35f) : beat ? gridLine.brighter (0.4f) : gridLine.brighter (0.1f));
                g.drawVerticalLine ((int) xOf (b), 0.0f, (float) getHeight());
            }
            // past the pattern's end
            const float endX = xOf (p.lengthBeats);
            if (endX < getWidth())
            {
                g.setColour (juce::Colours::black.withAlpha (0.28f));
                g.fillRect (juce::Rectangle<float> (endX, 0, (float) getWidth() - endX, (float) getHeight()));
            }
            // notes
            for (const auto& n : p.notes)
            {
                juce::Rectangle<float> r (xOf (n.start), yOf (n.pitch), (float) (n.length * pxPerBeat), rowH);
                if (r.getRight() < kKeysW || r.getX() > getWidth()) continue;
                g.setColour (stepOn.withAlpha (0.55f + 0.45f * n.velocity));
                g.fillRoundedRectangle (r.reduced (0.5f, 1.0f), 2.5f);
                g.setColour (darkText.withAlpha (0.7f));
                g.drawRoundedRectangle (r.reduced (0.5f, 1.0f), 2.5f, 1.0f);
                if (r.getWidth() > 26 && rowH >= 11)
                {
                    g.setFont (font (10.0f));
                    g.drawText (juce::String (kNoteNames[n.pitch % 12]) + juce::String (n.pitch / 12 - 1), r.reduced (3, 0), juce::Justification::centredLeft);
                }
            }
            // the playhead
            if (ed.proc.getMode() == FruitProcessor::Mode::pattern && ed.proc.playingPattern.load() == ed.currentPattern)
            {
                g.setColour (light);
                const float x = xOf (ed.proc.getPosition());
                g.drawLine (x, 0, x, (float) getHeight(), 1.5f);
            }
            // the keys
            for (int pitch = lo; pitch <= hi; ++pitch)
            {
                const bool black = juce::String (kNoteNames[pitch % 12]).contains ("#");
                juce::Rectangle<float> k (0, yOf (pitch), (float) kKeysW, rowH);
                g.setColour (black ? juce::Colour (0xff1d2b2a) : faderCap);
                g.fillRect (k.reduced (0, 0.5f));
                if (pitch % 12 == 0)
                {
                    g.setColour (darkText);
                    g.setFont (font (10.0f));
                    g.drawText ("C" + juce::String (pitch / 12 - 1), k.reduced (3, 0), juce::Justification::centredRight);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.x < kKeysW) return;
            auto& notes = pat().notes;
            const int hit = noteAt (e.position);
            if (hit >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2))
            {
                notes.erase (notes.begin() + hit);
                ed.proc.songChanged();
                dragNote = -1;
                repaint();
                return;
            }
            if (e.mods.isPopupMenu()) return;
            if (hit >= 0)
            {
                dragNote = hit;
                const auto& n = notes[(size_t) hit];
                dragMode = (e.position.x > xOf (n.start + n.length) - 7) ? 2 : 1;
            }
            else
            {
                fruit::Note n;
                n.start = snapped (beatAt (e.position.x));
                n.pitch = pitchAt (e.position.y);
                n.length = lastLength;
                notes.push_back (n);
                dragNote = (int) notes.size() - 1;
                dragMode = 1;
                ed.proc.songChanged();
            }
            const auto& n = notes[(size_t) dragNote];
            grabBeat = beatAt (e.position.x);
            grabStart = n.start;
            grabLength = n.length;
            grabPitch = pitchAt (e.position.y);
            grabNotePitch = n.pitch;
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto& notes = pat().notes;
            if (dragNote < 0 || dragNote >= (int) notes.size()) return;
            auto& n = notes[(size_t) dragNote];
            const double d = beatAt (e.position.x) - grabBeat;
            if (dragMode == 2)
                n.length = juce::jmax (snap, std::round ((grabLength + d) / snap) * snap);
            else
            {
                n.start = juce::jmax (0.0, std::round ((grabStart + d) / snap) * snap);
                n.pitch = juce::jlimit (0, 127, grabNotePitch + pitchAt (e.position.y) - grabPitch);
            }
            lastLength = n.length;
            ed.proc.songChanged();
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override { dragNote = -1; }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isCommandDown())
                pxPerBeat = juce::jlimit (12.0f, 240.0f, pxPerBeat * (w.deltaY > 0 ? 1.15f : 0.87f));
            else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
                scrollBeats = juce::jmax (0.0, scrollBeats - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 8.0);
            else
                topPitch = juce::jlimit ((float) (getHeight() / rowH), 127.0f, topPitch + w.deltaY * 10.0f);
            repaint();
        }
        // a two-finger-less way on a phone: drag on the keys scrolls the pitches
        void mouseDragOnKeys (float dy) { topPitch = juce::jlimit ((float) (getHeight() / rowH), 127.0f, topPitch + dy / rowH); repaint(); }
    };

    explicit Roll (FruitEditor& e) : ed (e), gridView (e)
    {
        title = "Piano roll";
        slotBox.caption = "Synth";
        patternBox.caption = "Pattern";
        slotBox.onChange = [this] (int v)
        {
            ed.proc.song.patterns[(size_t) ed.currentPattern].slot = v;
            ed.proc.keysSlot.store (v);
            ed.proc.songChanged();
            ed.refreshAll();
        };
        patternBox.onChange = [this] (int v) { ed.patternSelected (v); };
        play.setClickingTogglesState (false);
        play.onClick = [this]
        {
            if (ed.proc.getMode() == FruitProcessor::Mode::pattern) { ed.proc.stop(); ed.proc.recording.store (false); }
            else ed.proc.playPattern (ed.currentPattern);
        };
        record.setClickingTogglesState (false);
        record.setTooltip ("Record: plays the pattern in a loop, and what you play on the keys or a MIDI keyboard goes into it (on the 1/16 grid)");
        record.onClick = [this]
        {
            const bool on = ! ed.proc.recording.load();
            ed.proc.recording.store (on);
            if (on && ed.proc.getMode() != FruitProcessor::Mode::pattern) ed.proc.playPattern (ed.currentPattern);
        };
        for (auto b : { 1, 2, 4, 8, 16 }) length.addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
        length.onChange = [this]
        {
            ed.proc.song.patterns[(size_t) ed.currentPattern].lengthBeats = 4.0 * length.getSelectedId();
            ed.proc.songChanged();
            ed.refreshAll();
        };
        const std::pair<const char*, double> snaps[] = { { "Snap 1 beat", 1.0 }, { "Snap 1/2 beat", 0.5 }, { "Snap 1/4 beat", 0.25 },
                                                         { "Snap 1/8 beat", 0.125 }, { "Snap 1/3 beat", 1.0 / 3.0 } };
        for (int i = 0; i < 5; ++i) snap.addItem (snaps[i].first, i + 1);
        snap.setSelectedId (3, juce::dontSendNotification);
        snap.onChange = [this, snaps] { gridView.snap = snaps[snap.getSelectedId() - 1].second; gridView.lastLength = juce::jmax (gridView.lastLength, gridView.snap); };
        clear.onClick = [this]
        {
            ed.proc.song.patterns[(size_t) ed.currentPattern].notes.clear();
            ed.proc.songChanged();
            gridView.repaint();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &slotBox, &patternBox, &play, &record, &length, &snap, &clear, &gridView })
            addAndMakeVisible (c);
        for (auto* c : { &length, &snap })
        {
            c->setColour (juce::ComboBox::backgroundColourId, nameBtn.darker (0.25f));
            c->setColour (juce::ComboBox::textColourId, darkText);
            c->setColour (juce::ComboBox::arrowColourId, darkText);
            c->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        }
    }
    void refresh()
    {
        const auto& p = ed.proc.song.patterns[(size_t) ed.currentPattern];
        slotBox.set (p.slot, false);
        patternBox.set (ed.currentPattern, false);
        length.setSelectedId (juce::jlimit (1, 16, (int) std::round (p.lengthBeats / 4.0)), juce::dontSendNotification);
        play.setToggleState (ed.proc.getMode() == FruitProcessor::Mode::pattern, juce::dontSendNotification);
        gridView.repaint();
    }
    void resized() override
    {
        auto b = body().reduced (6, 4);
        auto bar = b.removeFromTop (30);
        slotBox.setBounds (bar.removeFromLeft (110).reduced (0, 2));
        bar.removeFromLeft (10);
        patternBox.setBounds (bar.removeFromLeft (128).reduced (0, 2));
        bar.removeFromLeft (10);
        play.setBounds (bar.removeFromLeft (58).reduced (0, 2));
        bar.removeFromLeft (4);
        record.setBounds (bar.removeFromLeft (46).reduced (0, 2));
        bar.removeFromLeft (8);
        length.setBounds (bar.removeFromLeft (92).reduced (0, 3));
        bar.removeFromLeft (6);
        snap.setBounds (bar.removeFromLeft (124).reduced (0, 3));
        clear.setBounds (bar.removeFromRight (60).reduced (0, 2));
        b.removeFromTop (4);
        gridView.setBounds (b);
    }
    FruitEditor& ed;
    NumberBox slotBox, patternBox;
    Button play { "Play" }, record { "Rec" }, clear { "Clear" };
    juce::ComboBox length, snap;
    Grid gridView;
};

//==============================================================================
// Playlist
//==============================================================================
struct FruitEditor::Playlist : public Window
{
    struct Grid : public juce::Component
    {
        FruitEditor& ed;
        explicit Grid (FruitEditor& e) : ed (e) {}
        static constexpr int kHeadW = 82, kRulerH = 22;
        float trackH = 34.0f, pxPerBeat = 12.0f;
        double scrollBeats = 0.0;
        float scrollY = 0.0f;
        double snap = 1.0;
        // dragging
        enum { none, moveClip, trimLeft, trimRight, moveAuto, autoLeft, autoRight, drawAuto, loopNew, loopMove, loopLeft, loopRight };
        int mode = none, index = -1;
        double grabBeat = 0, grabStart = 0, grabLength = 0, grabOffset = 0;
        int grabTrack = 0;
        std::vector<juce::Point<float>> grabPoints;
        float lastDrawX = -1.0f;

        float xOf (double beat) const { return (float) kHeadW + (float) ((beat - scrollBeats) * pxPerBeat); }
        double beatAt (float x) const { return juce::jmax (0.0, (x - kHeadW) / pxPerBeat + scrollBeats); }
        float yOf (int track) const { return (float) kRulerH + (float) (track - 1) * trackH - scrollY; }
        int trackAt (float y) const { return juce::jlimit (1, fruit::kSlots - 1, (int) std::floor ((y - kRulerH + scrollY) / trackH) + 1); }
        double snapped (double b) const { return std::round (b / snap) * snap; }

        juce::Rectangle<float> clipRect (int track, double start, double length) const
        { return { xOf (start), yOf (track) + 1.0f, (float) (length * pxPerBeat), trackH - 2.0f }; }

        void paint (juce::Graphics& g) override
        {
            auto& song = ed.proc.song;
            g.fillAll (grid);
            // tracks
            for (int t = 1; t < fruit::kSlots; ++t)
            {
                const float y = yOf (t);
                if (y + trackH < kRulerH || y > getHeight()) continue;
                g.setColour (t % 2 == 0 ? grid : gridAlt);
                g.fillRect (juce::Rectangle<float> ((float) kHeadW, y, (float) getWidth(), trackH));
                g.setColour (gridLine.darker (0.25f));
                g.drawHorizontalLine ((int) y, (float) kHeadW, (float) getWidth());
            }
            const double endBeat = beatAt ((float) getWidth());
            for (double b = std::floor (scrollBeats); b <= endBeat; b += 1.0)
            {
                const bool bar = std::fmod (b, 4.0) < 1.0e-6;
                g.setColour (bar ? gridLine.brighter (0.5f) : gridLine.brighter (0.12f));
                g.drawVerticalLine ((int) xOf (b), (float) kRulerH, (float) getHeight());
            }
            // pattern clips: a header with the name and the notes drawn small; sample clips: the waveform
            for (const auto& c : song.clips)
            {
                auto r = clipRect (c.track, c.start, c.length);
                if (r.getRight() < kHeadW || r.getX() > getWidth() || r.getBottom() < kRulerH || r.getY() > getHeight()) continue;
                if (c.sample > 0)
                {
                    paintSampleClip (g, c, r);
                    continue;
                }
                g.setColour (clipBody);
                g.fillRect (r);
                g.setColour (clipHead);
                g.fillRect (r.withHeight (12.0f));
                g.setColour (light);
                g.setFont (font (11.0f));
                g.drawText ("Pattern " + juce::String (c.pattern), r.withHeight (12.0f).reduced (4, 0), juce::Justification::centredLeft);
                const auto& pat = song.patterns[(size_t) juce::jlimit (1, fruit::kSlots - 1, c.pattern)];
                if (! pat.notes.empty())
                {
                    int lo = 127, hi = 0;
                    for (const auto& n : pat.notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
                    const float span = (float) juce::jmax (1, hi - lo);
                    auto body = r.withTrimmedTop (14.0f).reduced (1, 2);
                    g.setColour (light.withAlpha (0.8f));
                    const double L = juce::jmax (0.25, pat.lengthBeats);
                    for (const auto& n : pat.notes)
                        for (double base = -c.offset; base < c.length; base += L)
                        {
                            const double s = base + n.start;
                            if (s + n.length < 0.0 || s >= c.length) continue;
                            const double a = juce::jmax (0.0, s), z = juce::jmin (c.length, s + n.length);
                            const float y = body.getBottom() - ((float) (n.pitch - lo) / span) * (body.getHeight() - 2.0f) - 2.0f;
                            g.fillRect (juce::Rectangle<float> (r.getX() + (float) (a * pxPerBeat), y, juce::jmax (1.0f, (float) ((z - a) * pxPerBeat)), 2.0f));
                        }
                }
                g.setColour (darkText.withAlpha (0.8f));
                g.drawRect (r, 1.0f);
            }
            // Knob Record clips: the spline
            for (const auto& a : song.autos)
            {
                auto r = clipRect (a.track, a.start, a.length);
                if (r.getRight() < kHeadW || r.getX() > getWidth() || r.getBottom() < kRulerH || r.getY() > getHeight()) continue;
                g.setColour (loopRed.darker (0.45f).withAlpha (0.92f));
                g.fillRect (r);
                g.setColour (loopRed);
                g.fillRect (r.withHeight (12.0f));
                g.setColour (light);
                g.setFont (font (11.0f));
                g.drawText (a.label, r.withHeight (12.0f).reduced (4, 0), juce::Justification::centredLeft);
                auto body = r.withTrimmedTop (13.0f).reduced (1, 2);
                juce::Path curve;
                for (float px = 0; px <= body.getWidth(); px += 2.0f)
                {
                    const float y = body.getBottom() - a.valueAt (px / juce::jmax (1.0f, body.getWidth())) * body.getHeight();
                    if (px == 0) curve.startNewSubPath (body.getX(), y); else curve.lineTo (body.getX() + px, y);
                }
                g.setColour (light);
                g.strokePath (curve, juce::PathStrokeType (1.5f));
                if (a.points.size() <= 40)
                    for (const auto& pt : a.points)
                        g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre ({ body.getX() + pt.x * body.getWidth(), body.getBottom() - pt.y * body.getHeight() }));
            }
            // the ruler, the song's length and the playhead
            g.setColour (toolbar);
            g.fillRect (0, 0, getWidth(), kRulerH);
            // the loop region: what Play Song loops (the whole song, paler, while none is set)
            {
                double from = 0.0, to = 0.0;
                song.loopRange (from, to);
                const bool set = song.loopEnd > song.loopStart + 0.01;
                const float x0 = juce::jmax ((float) kHeadW, xOf (from)), x1 = juce::jmax (x0, xOf (to));
                auto stripe = juce::Rectangle<float> (x0, 2.0f, x1 - x0, 8.0f);
                if (set) raised (g, stripe, 2.0f, loopRed.brighter (0.15f)); else { g.setColour (loopRed.withAlpha (0.55f)); g.fillRect (stripe); }
                if (set && stripe.getWidth() > 10.0f)
                {
                    g.setColour (light.withAlpha (0.8f));
                    g.fillRect (stripe.withWidth (2.0f)); g.fillRect (stripe.withLeft (stripe.getRight() - 2.0f));
                }
            }
            g.setColour (light.withAlpha (0.8f));
            g.setFont (font (11.0f));
            for (double b = std::ceil (scrollBeats / 4.0) * 4.0; b <= endBeat; b += 4.0)
                g.drawText (juce::String ((int) (b / 4.0) + 1), juce::Rectangle<float> (xOf (b) + 2, 9, 30, 12), juce::Justification::centredLeft);
            if (ed.proc.getMode() == FruitProcessor::Mode::song)
            {
                const float x = xOf (ed.proc.getPosition());
                g.setColour (light);
                g.drawLine (x, 0, x, (float) getHeight(), 1.5f);
            }
            // track headers
            for (int t = 1; t < fruit::kSlots; ++t)
            {
                const float y = yOf (t);
                if (y + trackH < kRulerH || y > getHeight()) continue;
                auto r = juce::Rectangle<float> (0, juce::jmax ((float) kRulerH, y), (float) kHeadW, trackH);
                g.setColour (t == ed.selectedTrack ? trackHead.brighter (0.25f) : trackHead);
                g.fillRect (r.reduced (0, 0.5f));
                g.setColour (light.withAlpha (0.85f));
                g.setFont (font (13.0f));
                g.drawText ("Track " + juce::String (t), r.reduced (6, 0), juce::Justification::centredLeft);
            }
            g.setColour (toolbar);
            g.fillRect (0, 0, kHeadW, kRulerH);
        }

        // a sample's waveform, 256 samples to a pair of peaks, made once
        struct Peaks { std::vector<std::pair<float, float>> v; };
        std::map<const juce::AudioBuffer<float>*, Peaks> peakCache;
        const Peaks& peaksFor (const fruit::Sample& smp)
        {
            auto it = peakCache.find (smp.audio.get());
            if (it != peakCache.end()) return it->second;
            if (peakCache.size() > 120) peakCache.clear();
            auto& p = peakCache[smp.audio.get()];
            const int n = smp.audio->getNumSamples();
            p.v.assign ((size_t) (n / 256 + 1), { 0.0f, 0.0f });
            const float* d = smp.audio->getReadPointer (0);
            for (int i = 0; i < n; ++i)
            {
                auto& q = p.v[(size_t) (i / 256)];
                q.first = juce::jmin (q.first, d[i]); q.second = juce::jmax (q.second, d[i]);
            }
            return p;
        }
        double sampleBeats (const fruit::Sample& smp) const { return smp.beats (ed.proc.currentBpm()); }

        void paintSampleClip (juce::Graphics& g, const fruit::Clip& c, juce::Rectangle<float> r)
        {
            const auto& song = ed.proc.song;
            const bool ok = c.sample <= (int) song.samples.size();
            const auto body = juce::Colour (0xff1d4a5c), head = juce::Colour (0xff2f7f96);
            g.setColour (c.sample == ed.proc.view.sample ? body.brighter (0.12f) : body);
            g.fillRect (r);
            g.setColour (head);
            g.fillRect (r.withHeight (12.0f));
            g.setColour (light);
            g.setFont (font (11.0f));
            // (the header's text stays in view when the clip runs past the window's edges)
            auto top = r.withHeight (12.0f).withLeft (juce::jmax (r.getX(), (float) kHeadW)).withRight (juce::jmin (r.getRight(), (float) getWidth())).reduced (4, 0);
            if (ok)
            {
                const auto& smp = song.samples[(size_t) c.sample - 1];
                // the tempo it found, on the right of the header
                const auto bpm = smp.bpm > 0.0 ? juce::String (smp.bpm, 1) + " BPM" + (smp.stretch ? " > song" : "") : juce::String ("no tempo found");
                const float bw = juce::jmin (top.getWidth() * 0.5f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), bpm) + 4.0f);
                if (top.getWidth() > 90.0f) g.drawText (bpm, top.removeFromRight (bw), juce::Justification::centredRight);
                g.drawText ("Sample " + juce::String (c.sample) + "  " + smp.name, top, juce::Justification::centredLeft, true);
                if (smp.audio != nullptr)
                {
                    const auto& pk = peaksFor (smp).v;
                    auto wave = r.withTrimmedTop (13.0f).reduced (0, 1);
                    // (a stretched sample's picture is its own, pulled to its length in beats)
                    const double beatsToFrames = 60.0 / (smp.stretch && smp.bpm > 0.0 ? smp.bpm : ed.proc.currentBpm()) * smp.rate / 256.0;
                    const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
                    g.setColour (juce::Colour (0xff9fe6f2));
                    const float x0 = juce::jmax (r.getX(), (float) kHeadW), x1 = juce::jmin (r.getRight(), (float) getWidth());
                    for (float x = x0; x < x1; x += 1.0f)
                    {
                        const double beat = (x - r.getX()) / pxPerBeat + c.offset;
                        const int f0 = (int) (beat * beatsToFrames), f1 = juce::jmax (f0 + 1, (int) ((beat + 1.0 / pxPerBeat) * beatsToFrames));
                        if (f0 >= (int) pk.size()) break;
                        float lo = 0.0f, hi = 0.0f;
                        for (int f = f0; f < juce::jmin (f1, (int) pk.size()); ++f) { lo = juce::jmin (lo, pk[(size_t) f].first); hi = juce::jmax (hi, pk[(size_t) f].second); }
                        g.fillRect (juce::Rectangle<float> (x, mid - hi * half, 1.0f, juce::jmax (1.0f, (hi - lo) * half)));
                    }
                }
            }
            else g.drawText ("Sample " + juce::String (c.sample), top, juce::Justification::centredLeft);
            g.setColour (darkText.withAlpha (0.8f));
            g.drawRect (r, 1.0f);
        }

        void sampleMenu (int clipIndex)
        {
            auto& song = ed.proc.song;
            const int number = song.clips[(size_t) clipIndex].sample;
            if (number < 1 || number > (int) song.samples.size()) return;
            const int cur = song.samples[(size_t) number - 1].insert;
            juce::PopupMenu m, to, all;
            m.addItem (1, "Delete this clip");
            m.addItem (2, "Remove Sample " + juce::String (number) + " (all its clips)");
            to.addItem (100, "00  Master", true, cur == 0);
            for (int t = 1; t < fruit::kSlots; ++t)
                if (ed.proc.slots[(size_t) t].synthId >= 0 || ed.proc.inserts[(size_t) t].exists())
                    to.addItem (100 + t, two (t) + "  " + ed.proc.insertName (t), true, cur == t);
            for (int d = 0; d < 10; ++d)
            {
                juce::PopupMenu tens;
                for (int t = juce::jmax (1, d * 10); t < juce::jmin (fruit::kSlots, d * 10 + 10); ++t) tens.addItem (100 + t, two (t) + "  " + ed.proc.insertName (t), true, cur == t);
                all.addSubMenu (two (juce::jmax (1, d * 10)) + " - " + two (juce::jmin (fruit::kSlots - 1, d * 10 + 9)), tens);
            }
            to.addSubMenu ("Any insert", all);
            m.addSubMenu ("Plays through mixer insert " + two (cur), to);
            const auto& smp = song.samples[(size_t) number - 1];
            m.addSeparator();
            m.addItem (10, smp.bpm > 0.0 ? "Set the song's tempo to " + juce::String (smp.bpm, 1) + " BPM" : juce::String ("Set the song's tempo to this"), smp.bpm > 0.0);
            m.addItem (11, "Stretch to the song's tempo", smp.bpm > 0.0, smp.stretch);
            m.addItem (12, "Its tempo is half of that", smp.bpm >= 40.0);
            m.addItem (13, "Its tempo is double that", smp.bpm > 0.0 && smp.bpm <= 400.0);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                [safe = juce::Component::SafePointer<Grid> (this), clipIndex, number] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    auto& sg = safe->ed.proc.song;
                    if (r == 1 && clipIndex < (int) sg.clips.size() && sg.clips[(size_t) clipIndex].sample == number)
                    { sg.clips.erase (sg.clips.begin() + clipIndex); safe->ed.proc.songChanged(); }
                    else if (r == 2) safe->ed.proc.removeSample (number);
                    else if (r >= 100) safe->ed.proc.setSampleInsert (number, r - 100);
                    else if (number <= (int) sg.samples.size())
                    {
                        const auto& sm = sg.samples[(size_t) number - 1];
                        if (r == 10) safe->ed.proc.setSongTempo (sm.bpm);
                        if (r == 11) safe->ed.proc.setSampleStretch (number, ! sm.stretch);
                        if (r == 12) safe->ed.proc.setSampleBpm (number, sm.bpm * 0.5);
                        if (r == 13) safe->ed.proc.setSampleBpm (number, sm.bpm * 2.0);
                    }
                    safe->ed.refreshAll();
                });
        }

        // what is under the pointer: (kind, index); kind 1 pattern clip, 2 knob clip
        std::pair<int, int> hitTest (juce::Point<float> p)
        {
            auto& song = ed.proc.song;
            for (int i = (int) song.autos.size(); --i >= 0;)
                if (clipRect (song.autos[(size_t) i].track, song.autos[(size_t) i].start, song.autos[(size_t) i].length).contains (p)) return { 2, i };
            for (int i = (int) song.clips.size(); --i >= 0;)
                if (clipRect (song.clips[(size_t) i].track, song.clips[(size_t) i].start, song.clips[(size_t) i].length).contains (p)) return { 1, i };
            return { 0, -1 };
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto& song = ed.proc.song;
            if (e.y < kRulerH)
            {
                // the ruler: drag the loop region, its ends, or a new one; double tap: none (the whole song)
                if (e.x < kHeadW) return;
                if (e.getNumberOfClicks() >= 2) { song.loopStart = song.loopEnd = 0.0; ed.proc.songChanged(); mode = none; repaint(); return; }
                grabBeat = beatAt (e.position.x);
                const bool set = song.loopEnd > song.loopStart + 0.01;
                const float x0 = xOf (song.loopStart), x1 = xOf (song.loopEnd);
                if (set && std::abs (e.position.x - x0) < 9.0f) mode = loopLeft;
                else if (set && std::abs (e.position.x - x1) < 9.0f) mode = loopRight;
                else if (set && e.position.x > x0 && e.position.x < x1) mode = loopMove;
                else mode = loopNew;
                grabStart = song.loopStart; grabLength = song.loopEnd - song.loopStart;
                return;
            }
            if (e.x < kHeadW) { ed.selectedTrack = trackAt (e.position.y); repaint(); return; }
            const auto [kind, i] = hitTest (e.position);
            if (kind == 1 && song.clips[(size_t) i].sample > 0 && e.mods.isPopupMenu())
            {
                ed.proc.view.sample = song.clips[(size_t) i].sample;
                sampleMenu (i);
                mode = none;
                repaint();
                return;
            }
            if (kind != 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2))
            {
                if (kind == 1) song.clips.erase (song.clips.begin() + i);
                else song.autos.erase (song.autos.begin() + i);
                ed.proc.songChanged();
                mode = none;
                repaint();
                return;
            }
            if (e.mods.isPopupMenu()) return;
            grabBeat = beatAt (e.position.x);
            if (kind == 1)
            {
                auto& c = song.clips[(size_t) i];
                const auto r = clipRect (c.track, c.start, c.length);
                index = i;
                mode = e.position.x < r.getX() + 7 ? trimLeft : e.position.x > r.getRight() - 7 ? trimRight : moveClip;
                grabStart = c.start; grabLength = c.length; grabOffset = c.offset; grabTrack = c.track;
                ed.selectedTrack = c.track;
                if (c.sample > 0) ed.proc.view.sample = c.sample;                 // the brush is this sample now
                else if (c.pattern != ed.currentPattern || ed.proc.view.sample != 0) ed.patternSelected (c.pattern);
            }
            else if (kind == 2)
            {
                auto& a = song.autos[(size_t) i];
                const auto r = clipRect (a.track, a.start, a.length);
                index = i;
                mode = e.position.x < r.getX() + 7 ? autoLeft : e.position.x > r.getRight() - 7 ? autoRight
                     : e.position.y < r.getY() + 13 ? moveAuto : drawAuto;
                grabStart = a.start; grabLength = a.length; grabTrack = a.track; grabPoints = a.points;
                ed.selectedTrack = a.track;
                lastDrawX = -1.0f;
                if (mode == drawAuto) mouseDrag (e);
            }
            else
            {
                // empty: the piano roll's pattern - or the sample chosen last - placed here
                fruit::Clip c;
                c.track = trackAt (e.position.y);
                c.start = std::floor (grabBeat / snap) * snap;
                c.pattern = ed.currentPattern;
                c.length = song.patterns[(size_t) c.pattern].lengthBeats;
                if (const int sn = ed.proc.view.sample; sn > 0 && sn <= (int) song.samples.size())
                {
                    c.sample = sn;
                    c.pattern = 0;
                    c.length = juce::jmax (0.25, sampleBeats (song.samples[(size_t) sn - 1]));
                }
                song.clips.push_back (c);
                index = (int) song.clips.size() - 1;
                mode = moveClip;
                grabStart = c.start; grabLength = c.length; grabOffset = 0; grabTrack = c.track;
                ed.selectedTrack = c.track;
                ed.proc.songChanged();
            }
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto& song = ed.proc.song;
            const double d = beatAt (e.position.x) - grabBeat;
            if (mode == loopNew || mode == loopMove || mode == loopLeft || mode == loopRight)
            {
                const double bar = juce::jmax (snap, 1.0);                     // whole beats at the finest
                auto q = [bar] (double b) { return std::round (b / bar) * bar; };
                if (mode == loopNew)
                {
                    const double a = q (grabBeat), b = q (beatAt (e.position.x));
                    if (std::abs (b - a) >= bar) { song.loopStart = juce::jmin (a, b); song.loopEnd = juce::jmax (a, b); }
                }
                else if (mode == loopMove) { song.loopStart = juce::jmax (0.0, q (grabStart + d)); song.loopEnd = song.loopStart + grabLength; }
                else if (mode == loopLeft) song.loopStart = juce::jlimit (0.0, grabStart + grabLength - bar, q (grabStart + d));
                else song.loopEnd = juce::jmax (grabStart + bar, q (grabStart + grabLength + d));
                ed.proc.songChanged();
                repaint();
                return;
            }
            if (mode == moveClip || mode == trimLeft || mode == trimRight)
            {
                if (index < 0 || index >= (int) song.clips.size()) return;
                auto& c = song.clips[(size_t) index];
                if (mode == moveClip)
                {
                    c.start = juce::jmax (0.0, snapped (grabStart + d));
                    c.track = trackAt (e.position.y);
                }
                else if (mode == trimLeft)          // cut from the left: what is left plays as it did
                {
                    const double nd = juce::jlimit (-grabStart, grabLength - snap, snapped (d));
                    c.start = grabStart + nd;
                    c.length = grabLength - nd;
                    c.offset = juce::jmax (0.0, grabOffset + nd);
                }
                else                                 // cut from the right, or longer (the pattern repeats)
                    c.length = juce::jmax (snap, snapped (grabLength + d));
                if (c.sample > 0 && c.sample <= (int) song.samples.size())           // a sample ends where its audio does
                    c.length = juce::jmin (c.length, juce::jmax (0.25, sampleBeats (song.samples[(size_t) c.sample - 1]) - c.offset));
                ed.proc.songChanged();
            }
            else if (mode == moveAuto || mode == autoLeft || mode == autoRight)
            {
                if (index < 0 || index >= (int) song.autos.size()) return;
                auto& a = song.autos[(size_t) index];
                if (mode == moveAuto)
                {
                    a.start = juce::jmax (0.0, snapped (grabStart + d));
                    a.track = trackAt (e.position.y);
                }
                else
                {
                    // the curve stays where it was in time; the clip's ends move
                    double ns = grabStart, nl = grabLength;
                    if (mode == autoLeft) { const double nd = juce::jlimit (-grabStart, grabLength - snap, snapped (d)); ns = grabStart + nd; nl = grabLength - nd; }
                    else nl = juce::jmax (snap, snapped (grabLength + d));
                    fruit::AutoClip old = a;
                    old.start = grabStart; old.length = grabLength; old.points = grabPoints;
                    std::vector<juce::Point<float>> pts;
                    const int n = juce::jlimit (2, 64, (int) (nl * 2.0) + 1);
                    for (int k = 0; k < n; ++k)
                    {
                        const float x = (float) k / (float) (n - 1);
                        const double beat = ns + x * nl;
                        const float ox = (float) juce::jlimit (0.0, 1.0, (beat - old.start) / old.length);
                        pts.push_back ({ x, old.valueAt (ox) });
                    }
                    a.start = ns; a.length = nl; a.points = pts;
                }
                ed.proc.songChanged();
            }
            else if (mode == drawAuto)
            {
                // freehand: the drawn stretch replaces the points under it
                if (index < 0 || index >= (int) song.autos.size()) return;
                auto& a = song.autos[(size_t) index];
                auto body = clipRect (a.track, a.start, a.length).withTrimmedTop (13.0f).reduced (1, 2);
                const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - body.getX()) / juce::jmax (1.0f, body.getWidth()));
                const float y = juce::jlimit (0.0f, 1.0f, (body.getBottom() - e.position.y) / juce::jmax (1.0f, body.getHeight()));
                const float x0 = lastDrawX < 0 ? x : juce::jmin (x, lastDrawX), x1 = lastDrawX < 0 ? x : juce::jmax (x, lastDrawX);
                const float gap = 0.5f / juce::jmax (1.0f, body.getWidth() / 6.0f);
                a.points.erase (std::remove_if (a.points.begin(), a.points.end(),
                                                [x0, x1, gap] (const juce::Point<float>& p) { return p.x >= x0 - gap && p.x <= x1 + gap; }),
                                a.points.end());
                a.points.push_back ({ x, y });
                std::sort (a.points.begin(), a.points.end(), [] (const auto& p, const auto& q) { return p.x < q.x; });
                if (a.points.front().x > 0.0f) a.points.insert (a.points.begin(), { 0.0f, a.points.front().y });
                if (a.points.back().x < 1.0f) a.points.push_back ({ 1.0f, a.points.back().y });
                lastDrawX = x;
                ed.proc.songChanged();
            }
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override { mode = none; index = -1; }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isCommandDown())
                pxPerBeat = juce::jlimit (3.0f, 80.0f, pxPerBeat * (w.deltaY > 0 ? 1.15f : 0.87f));
            else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
                scrollBeats = juce::jmax (0.0, scrollBeats - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 40.0);
            else
                scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, trackH * (fruit::kSlots - 1) - (float) getHeight() + kRulerH), scrollY - w.deltaY * 150.0f);
            repaint();
        }
    };

    explicit Playlist (FruitEditor& e) : ed (e), gridView (e)
    {
        title = "Playlist - Arrangement";
        play.onClick = [this]
        {
            if (ed.proc.getMode() == FruitProcessor::Mode::song) ed.proc.stop();
            else ed.proc.playSong();
        };
        knobRecord.setTooltip ("Knob Record: turn a knob on any synth or effect, then press this - a clip that moves that knob "
                               "appears on the selected track at the playhead (or the start); draw its curve with a finger");
        knobRecord.onClick = [this]
        {
            ed.proc.scanForTurnedKnobs();
            const double at = ed.proc.getMode() == FruitProcessor::Mode::song ? std::floor (ed.proc.getPosition() / 4.0) * 4.0 : 0.0;
            ed.proc.addKnobRecordClip (ed.selectedTrack, at);
            gridView.repaint();
        };
        loadSample.setTooltip ("Load Sample: an audio file as a clip on the first empty track, at its own speed. Drag it, cut it at its ends, "
                               "tap empty space for another clip of it. Its header shows the tempo found in it; long-press / right-click "
                               "for the mixer insert it plays through (00, the master, at first)");
        loadSample.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            // (taken here, not in the capture below: MSVC reads `this` in a nested lambda's init-capture as the outer lambda)
            const juce::Component::SafePointer<Playlist> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr) return;
                    std::unique_ptr<juce::InputStream> in;
                    juce::String name;
                   #if JUCE_ANDROID
                    const auto url = fc.getURLResult();
                    if (url.isEmpty()) return;
                    if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                    {
                        name = doc.getInfo().getName();
                        // (a content:// stream does not seek: read it whole first)
                        if (auto raw = doc.createInputStream()) { juce::MemoryBlock mb; raw->readIntoMemoryBlock (mb); in = std::make_unique<juce::MemoryInputStream> (std::move (mb)); }
                    }
                   #else
                    const auto file = fc.getResult();
                    if (! file.existsAsFile()) return;
                    name = file.getFileNameWithoutExtension();
                    in = std::make_unique<juce::FileInputStream> (file);
                   #endif
                    if (safe->ed.proc.addSample (std::move (in), name.upToLastOccurrenceOf (".", false, false).isNotEmpty() ? name.upToLastOccurrenceOf (".", false, false) : name) == 0)
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit DAW", "That file could not be read as audio.");
                    safe->ed.refreshAll();
                });
        };
        saveRegion.setTooltip ("Save Region as FLAC: the loop region (the red stripe on the ruler; the whole song without one) as an audio "
                               "file that loops seamlessly - reverb and echo tails of its end are in its start");
        saveRegion.onClick = [this]
        {
            double rate = 48000.0;
            auto audio = std::make_shared<juce::AudioBuffer<float>> (ed.proc.renderRegion (rate));
            if (audio->getNumSamples() == 0)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit DAW", "There is nothing on the playlist to save yet.");
                return;
            }
            chooser = std::make_unique<juce::FileChooser> ("Save Region as FLAC",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fruit DAW region.flac"), "*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                [audio, rate] (const juce::FileChooser& fc)
                {
                    const auto destination = fc.getURLResult();       // (on Android a content:// document)
                    if (destination.isEmpty()) return;
                    juce::MemoryBlock mb;
                    {
                        juce::FlacAudioFormat flac;
                        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream> (mb, false);
                        if (auto writer = flac.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24)))
                            writer->writeFromAudioSampleBuffer (*audio, 0, audio->getNumSamples());
                    }
                    const auto problem = mb.getSize() > 0 ? aqp::files::writeDocument (destination, mb.getData(), mb.getSize()) : juce::String ("it could not be encoded");
                    if (problem.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Fruit DAW", "Could not save the region: " + problem + ".");
                });
        };
        zoomIn.onClick = [this] { gridView.pxPerBeat = juce::jmin (80.0f, gridView.pxPerBeat * 1.4f); gridView.repaint(); };
        zoomOut.onClick = [this] { gridView.pxPerBeat = juce::jmax (3.0f, gridView.pxPerBeat / 1.4f); gridView.repaint(); };
        for (auto* c : std::initializer_list<juce::Component*> { &play, &loadSample, &saveRegion, &knobRecord, &zoomIn, &zoomOut, &patternList, &gridView })
            addAndMakeVisible (c);
        patternList.ed = &ed;
    }

    // FL's pattern list: tap one to make it the brush (and the piano roll's)
    struct PatternList : public juce::Component
    {
        FruitEditor* ed = nullptr;
        float scroll = 0;
        void paint (juce::Graphics& g) override
        {
            g.fillAll (fruitui::patternList);
            g.setFont (font (13.0f));
            // the samples first ("Sample 1  124 BPM"), then the patterns; the brush is highlighted
            const int ns = (int) ed->proc.song.samples.size();
            for (int row = 0; row < ns + fruit::kSlots - 1; ++row)
            {
                auto r = juce::Rectangle<float> (0, (float) row * 22.0f - scroll, (float) getWidth(), 22.0f);
                if (r.getBottom() < 0 || r.getY() > getHeight()) continue;
                if (row < ns)
                {
                    const auto& smp = ed->proc.song.samples[(size_t) row];
                    if (row + 1 == ed->proc.view.sample) { g.setColour (juce::Colour (0xff2f7f96).withAlpha (0.55f)); g.fillRect (r); }
                    g.setColour (juce::Colour (0xff9fe6f2));
                    auto t = r.reduced (8, 0);
                    g.setFont (font (11.0f));
                    if (smp.bpm > 0.0) g.drawText (juce::String (juce::roundToInt (smp.bpm)), t.removeFromRight (26.0f), juce::Justification::centredRight);
                    g.setFont (font (13.0f));
                    g.drawText ("Sample " + juce::String (row + 1), t, juce::Justification::centredLeft, true);
                    continue;
                }
                const int p = row - ns + 1;
                const bool used = ! ed->proc.song.patterns[(size_t) p].notes.empty();
                if (p == ed->currentPattern && ed->proc.view.sample == 0) { g.setColour (stepOn.withAlpha (0.3f)); g.fillRect (r); }
                g.setColour (used ? light : light.withAlpha (0.35f));
                g.drawText ("Pattern " + juce::String (p), r.reduced (8, 0), juce::Justification::centredLeft);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown()) return;
            const int ns = (int) ed->proc.song.samples.size();
            const int row = (int) ((e.position.y + scroll) / 22.0f);
            if (row < ns) { ed->proc.view.sample = row + 1; ed->refreshAll(); }
            else ed->patternSelected (juce::jlimit (1, fruit::kSlots - 1, row - ns + 1));
        }
        void mouseDrag (const juce::MouseEvent& e) override { scroll = juce::jlimit (0.0f, 22.0f * 99 - getHeight(), scroll - (float) e.getDistanceFromDragStartY() * 0.1f); repaint(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        { scroll = juce::jlimit (0.0f, juce::jmax (0.0f, 22.0f * 99 - getHeight()), scroll - w.deltaY * 120.0f); repaint(); }
    };

    void refresh()
    {
        play.setToggleState (ed.proc.getMode() == FruitProcessor::Mode::song, juce::dontSendNotification);
        knobRecord.setButtonText (ed.proc.lastKnob.moduleId >= 0 ? "Knob Record: " + ed.proc.lastKnob.label : juce::String ("Knob Record"));
        patternList.repaint();
        gridView.repaint();
    }
    void resized() override
    {
        auto b = body().reduced (6, 4);
        auto bar = b.removeFromTop (30);
        play.setBounds (bar.removeFromLeft (82).reduced (0, 2));
        bar.removeFromLeft (4);
        loadSample.setBounds (bar.removeFromLeft (96).reduced (0, 2));
        bar.removeFromLeft (4);
        saveRegion.setBounds (bar.removeFromLeft (96).reduced (0, 2));
        bar.removeFromLeft (8);
        zoomOut.setBounds (bar.removeFromRight (30).reduced (0, 2));
        bar.removeFromRight (4);
        zoomIn.setBounds (bar.removeFromRight (30).reduced (0, 2));
        bar.removeFromRight (8);
        knobRecord.setBounds (bar.reduced (0, 2));
        b.removeFromTop (4);
        patternList.setBounds (b.removeFromLeft (112));
        b.removeFromLeft (4);
        gridView.setBounds (b);
    }
    FruitEditor& ed;
    Button play { "Play Song" }, loadSample { "Load Sample" }, saveRegion { "Save Region" }, knobRecord { "Knob Record" }, zoomIn { "+" }, zoomOut { "-" };
    std::unique_ptr<juce::FileChooser> chooser;
    PatternList patternList;
    Grid gridView;
};

//==============================================================================
// Mixer
//==============================================================================
struct FruitEditor::Mixer : public Window
{
    struct Strips : public juce::Component
    {
        FruitEditor& ed;
        explicit Strips (FruitEditor& e) : ed (e) {}
        static constexpr int kStripW = 46;
        float scroll = 0;
        int dragInsert = -1, dragKind = 0;   // 1 fader, 3 pan
        float grabValue = 0;
        int grabTarget = 0;
        juce::Rectangle<float> stripRect (int i) const { return { (float) (i * kStripW) - scroll, 0.0f, (float) kStripW - 2.0f, (float) getHeight() }; }
        juce::Rectangle<float> faderArea (int i) const { auto r = stripRect (i); return { r.getCentreX() - 6, r.getY() + 112, 12, r.getHeight() - 112 - 74 }; }
        // under the fader (6.2): pan, polarity and the insert's latency (its sends are in the panel on the right)
        juce::Rectangle<float> panArea (int i) const { auto r = stripRect (i); return { r.getX() + 4, r.getBottom() - 66, r.getWidth() - 8, 12 }; }
        juce::Rectangle<float> invertArea (int i) const { auto r = stripRect (i); return { r.getCentreX() - 12, r.getBottom() - 49, 24, 17 }; }
        juce::Rectangle<float> latencyArea (int i) const { auto r = stripRect (i); return { r.getX() + 1, r.getBottom() - 29, r.getWidth() - 2, 25 }; }
        int insertAt (float x) const { return juce::jlimit (0, fruit::kSlots - 1, (int) ((x + scroll) / kStripW)); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (mixerBg);
            for (int i = 0; i < fruit::kSlots; ++i)
            {
                auto r = stripRect (i);
                if (r.getRight() < 0 || r.getX() > getWidth()) continue;
                const bool sel = i == ed.selectedInsert;
                {
                    const auto base = sel ? strip.brighter (0.22f) : strip;
                    g.setGradientFill (juce::ColourGradient (base.brighter (0.10f), r.getX(), 0, base.darker (0.12f), r.getRight(), 0, false));
                    g.fillRect (r);
                    g.setColour (juce::Colours::white.withAlpha (0.07f));
                    g.fillRect (r.withWidth (1.0f));
                }
                g.setColour (light);
                g.setFont (font (13.0f, true));
                g.drawText (i == 0 ? juce::String ("M") : juce::String (i), r.withHeight (20), juce::Justification::centred);
                // the name, upwards
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getY() + 66));
                    g.setColour (ed.proc.inserts[(size_t) i].exists() || i == 0 ? light.withAlpha (0.85f) : light.withAlpha (0.35f));
                    g.setFont (font (12.0f));
                    g.drawText (ed.proc.insertName (i), juce::Rectangle<float> (r.getCentreX() - 44, r.getY() + 58, 88, 16), juce::Justification::centred);
                }
                // meter
                const float lvl = juce::jlimit (0.0f, 1.0f, std::sqrt (ed.proc.meterLevel (i)));
                auto meter = juce::Rectangle<float> (r.getX() + 4, r.getY() + 112, 5, faderArea (i).getHeight());
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.fillRect (meter);
                g.setColour (lvl > 0.97f ? loopRed.brighter (0.4f) : stepOn);
                g.fillRect (meter.withTrimmedTop (meter.getHeight() * (1.0f - lvl)));
                // fader
                auto fa = faderArea (i);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRoundedRectangle (fa.withSizeKeepingCentre (4, fa.getHeight()), 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRect (juce::Rectangle<float> (fa.getCentreX() + 2, fa.getY(), 1, fa.getHeight()));
                const float v = juce::jlimit (0.0f, 1.0f, ed.proc.getVolume (i) / 1.25f);
                const float cy = fa.getBottom() - v * fa.getHeight();
                g.setColour (juce::Colours::black.withAlpha (0.30f));
                g.fillRoundedRectangle (juce::Rectangle<float> (r.getCentreX() - 13, cy - 4, 26, 13), 2.0f);     // the cap's shadow
                raised (g, juce::Rectangle<float> (r.getCentreX() - 13, cy - 7, 26, 13), 2.0f, sel ? selected : faderCap, 1.3f);
                g.setColour (darkText.withAlpha (0.6f));
                g.drawHorizontalLine ((int) cy, r.getCentreX() - 10, r.getCentreX() + 10);
                // pan: a bar from the middle out (double-click: centre)
                {
                    auto pa = panArea (i);
                    sunken (g, pa, 3.0f, juce::Colour (0xff1d3c38), 0.8f);
                    const float pan = ed.proc.getPan (i);
                    const float mid = pa.getCentreX(), half = pa.getWidth() * 0.5f - 2.0f;
                    const float x = mid + pan * half;
                    if (std::abs (pan) > 0.005f)
                    {
                        g.setColour (stepOn.withAlpha (0.85f));
                        g.fillRect (juce::Rectangle<float> (juce::jmin (mid, x), pa.getY() + 3, std::abs (x - mid), pa.getHeight() - 6));
                    }
                    g.setColour (light.withAlpha (0.35f));
                    g.fillRect (juce::Rectangle<float> (mid - 0.5f, pa.getY() + 2, 1.0f, pa.getHeight() - 4));
                    raised (g, juce::Rectangle<float> (x - 3, pa.getY() + 1, 6, pa.getHeight() - 2), 1.5f, faderCap, 1.0f);
                }
                // polarity
                {
                    auto ia = invertArea (i);
                    const bool inv = ed.proc.isInverted (i);
                    if (inv) raised (g, ia, 3.0f, loopRed.brighter (0.1f)); else sunken (g, ia, 3.0f, juce::Colour (0xff1d3c38), 0.8f);
                    g.setColour (inv ? light : light.withAlpha (0.55f));
                    g.setFont (font (12.0f, true));
                    g.drawText (juce::String::fromUTF8 ("\xc3\x98"), ia, juce::Justification::centred);   // the polarity sign
                }
                // its effects' latency
                {
                    const int lat = ed.proc.insertLatency (i);
                    auto la = latencyArea (i);
                    g.setColour (lat > 0 ? light.withAlpha (0.9f) : light.withAlpha (0.4f));
                    g.setFont (font (10.0f, true));
                    g.drawText (juce::String (lat), la.removeFromTop (12), juce::Justification::centred);
                    g.setFont (font (9.0f));
                    const double sr = ed.proc.getSampleRate() > 0.0 ? ed.proc.getSampleRate() : 48000.0;
                    g.drawText (lat > 0 ? juce::String (lat * 1000.0 / sr, lat * 1000.0 / sr < 10.0 ? 2 : 1) + " ms" : juce::String ("smp"),
                                la, juce::Justification::centred);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = insertAt (e.position.x);
            ed.selectedInsert = i;
            dragKind = 0;
            if (faderArea (i).expanded (14, 8).contains (e.position)) { dragKind = 1; grabValue = ed.proc.getVolume (i); }
            else if (panArea (i).expanded (2, 4).contains (e.position)) { dragKind = 3; grabValue = ed.proc.getPan (i); }
            else if (invertArea (i).expanded (2, 2).contains (e.position)) { ed.proc.setInverted (i, ! ed.proc.isInverted (i)); dragKind = 4; }
            dragInsert = i;
            if (auto* m = findParentComponentOfClass<Mixer>()) m->refresh();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragInsert < 0) return;
            if (dragKind == 1)
            {
                const float h = faderArea (dragInsert).getHeight();
                ed.proc.setVolume (dragInsert, juce::jlimit (0.0f, 1.25f, grabValue - (float) e.getDistanceFromDragStartY() / h * 1.25f));
            }
            else if (dragKind == 3)
            {
                // sideways, or up and down (the bar is small): a strip's width is the whole way across
                const float d = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / (float) (kStripW * 2);
                ed.proc.setPan (dragInsert, juce::jlimit (-1.0f, 1.0f, grabValue + d));
            }
            else if (dragKind == 4) {}
            else
                scroll = juce::jlimit (0.0f, juce::jmax (0.0f, (float) (kStripW * fruit::kSlots - getWidth())), scroll - (float) e.getDistanceFromDragStartX() * 0.15f);
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            dragInsert = -1;
            repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int i = insertAt (e.position.x);
            if (faderArea (i).expanded (14, 8).contains (e.position)) { ed.proc.setVolume (i, 0.8f); repaint(); }
            else if (panArea (i).expanded (2, 4).contains (e.position)) { ed.proc.setPan (i, 0.0f); repaint(); }
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            scroll = juce::jlimit (0.0f, juce::jmax (0.0f, (float) (kStripW * fruit::kSlots - getWidth())),
                                   scroll - (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * 200.0f);
            repaint();
        }
    };

    // the selected insert's ten effect slots and its nine sends; it scrolls
    struct Slots : public juce::Component
    {
        FruitEditor& ed;
        explicit Slots (FruitEditor& e) : ed (e) {}
        static constexpr int kHead = 30, kFxH = 28, kSendH = 32, kSendsTop = fruit::kFxPerInsert * kFxH + 28, kNumW = 50;
        static constexpr int kContentH = kSendsTop + fruit::kSends * kSendH + 122;
        float scroll = 0;
        int dragKind = 0, dragSend = -1, grabTarget = 0;   // 1 a send's number, 2 its level, 3 scrolling
        int previewTarget = -2;    // a number being dragged: shown, set only when the finger lifts (-2 none)
        // where a dragged number may land: free of loops and of this insert's other sends
        bool canTarget (int t) const
        {
            const int i = ed.selectedInsert;
            if (t < 0) return true;
            if (t == i || ed.proc.wouldLoop (i, t)) return false;
            for (int j = 0; j < fruit::kSends; ++j)
                if (j != dragSend && ed.proc.inserts[(size_t) i].sends[(size_t) j].target == t) return false;
            return true;
        }
        float grabScroll = 0;
        float maxScroll() const { return (float) juce::jmax (0, kContentH - (getHeight() - kHead)); }
        juce::Rectangle<float> fxRow (int k) const { return { 6, (float) (kHead + k * kFxH) - scroll, (float) getWidth() - 18, (float) kFxH - 3 }; }
        juce::Rectangle<float> sendRow (int k) const { return { 6, (float) (kHead + kSendsTop + k * kSendH) - scroll, (float) getWidth() - 18, (float) kSendH - 4 }; }
        juce::Rectangle<float> sendNum (int k) const { return sendRow (k).withWidth ((float) kNumW); }
        juce::Rectangle<float> sendBar (int k) const { return sendRow (k).withTrimmedLeft ((float) kNumW + 6); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (slotPanel);
            const int i = ed.selectedInsert;
            const auto& in = ed.proc.inserts[(size_t) i];
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (0, kHead, getWidth(), getHeight() - kHead);
                for (int k = 0; k < fruit::kFxPerInsert; ++k)
                {
                    auto r = fxRow (k);
                    const int id = in.exists() ? in.fx[(size_t) k] : -1;
                    auto* inst = ed.proc.getEngine().getInstance (id);
                    if (inst != nullptr) raised (g, r, 3.0f, nameBtn.darker (0.1f));
                    else sunken (g, r, 3.0f, slotPanel.darker (0.14f), 0.6f);
                    // FL's on/off LED
                    g.setColour (inst != nullptr ? stepOn : dimText.withAlpha (0.6f));
                    g.fillEllipse (r.getRight() - 16, r.getCentreY() - 5, 10, 10);
                    g.setColour (juce::Colours::white.withAlpha (inst != nullptr ? 0.5f : 0.15f));
                    g.fillEllipse (r.getRight() - 14, r.getCentreY() - 4, 4, 3);
                    g.setColour (inst != nullptr ? darkText : dimText);
                    g.setFont (font (13.0f));
                    g.drawText (inst != nullptr ? inst->descriptor().displayName : "Slot " + juce::String (k + 1), r.reduced (10, 0), juce::Justification::centredLeft);
                }
                auto head = juce::Rectangle<float> (8, (float) (kHead + kSendsTop - 22) - scroll, (float) getWidth() - 20, 18);
                g.setColour (light);
                g.setFont (font (13.0f, true));
                g.drawText (i == 0 ? juce::String ("The master goes to the output") : juce::String ("Sends  (to track, level)"), head, juce::Justification::centredLeft);
                if (i > 0)
                    for (int k = 0; k < fruit::kSends; ++k)
                    {
                        auto sd = in.sends[(size_t) k];
                        const bool previewing = k == dragSend && dragKind == 1 && previewTarget > -2;
                        if (previewing) sd.target = previewTarget;
                        const bool used = sd.target >= 0;
                        auto nr = sendNum (k);
                        sunken (g, nr, 3.0f, used ? (sd.target == 0 ? juce::Colour (0xff1d4640) : loopRed.darker (0.15f)) : slotPanel.darker (0.2f), 0.8f);
                        g.setColour (previewing && ! canTarget (sd.target) ? light.withAlpha (0.35f) : used ? light : light.withAlpha (0.4f));
                        g.setFont (font (14.0f, true));
                        g.drawText (used ? two (sd.target) : juce::String ("--"), nr, juce::Justification::centred);
                        if (previewing)
                        {
                            g.setColour (selected);
                            g.drawRoundedRectangle (nr.reduced (0.5f), 3.0f, 1.5f);
                        }
                        auto br = sendBar (k);
                        sunken (g, br, 3.0f, slotPanel.darker (0.28f), 0.8f);
                        if (used)
                        {
                            auto fill = br.reduced (2.0f).withWidth (juce::jmax (0.0f, (br.getWidth() - 4.0f) * sd.level));
                            if (fill.getWidth() >= 2.0f) raised (g, fill, 2.0f, stepOn.withMultipliedBrightness (0.85f));
                            g.setColour (sd.level > 0.55f ? darkText : light);
                            g.setFont (font (12.0f));
                            g.drawText (sd.target == 0 ? "Master  " + juce::String (juce::roundToInt (sd.level * 100.0f)) + " %"
                                                       : ed.proc.insertName (sd.target) + "  " + (sd.level <= 0.0f ? juce::String ("sidechain only")
                                                                                                                     : juce::String (juce::roundToInt (sd.level * 100.0f)) + " %"),
                                        br.reduced (8, 0), juce::Justification::centredLeft);
                        }
                        else
                        {
                            g.setColour (dimText);
                            g.setFont (font (12.0f));
                            g.drawText ("Send " + juce::String (k + 1), br.reduced (8, 0), juce::Justification::centredLeft);
                        }
                    }
                g.setColour (light.withAlpha (0.75f));
                g.setFont (font (11.0f));
                if (i > 0)
                    g.drawFittedText (juce::String::fromUTF8 ("Tap a number to choose where this track goes (or drag it), drag the bar for how much. The track's effects with a second input "
                                      "(Sidechain Comp, Vocoder ...) always hear a send in full: at 0 % it is a sidechain only.\n"
                                      "Under each fader: its pan (drag; double-tap centres it), \xc3\x98 to invert its polarity (red while on), and the "
                                      "latency its effects add (samples, then ms)."),
                                      juce::Rectangle<int> (8, kHead + kSendsTop + fruit::kSends * kSendH + 4 - (int) scroll, getWidth() - 22, 112), juce::Justification::topLeft, 9);
            }
            // the header stays; a thin scroll thumb
            g.setColour (slotPanel);
            g.fillRect (0, 0, getWidth(), kHead);
            g.setColour (light);
            g.setFont (font (14.0f, true));
            g.drawText (i == 0 ? juce::String ("Master") : "Insert " + two (i) + "  " + ed.proc.insertName (i), juce::Rectangle<int> (0, 0, getWidth(), 26).reduced (8, 0), juce::Justification::centredLeft);
            if (maxScroll() > 0)
            {
                const float viewH = (float) (getHeight() - kHead), th = viewH * viewH / kContentH;
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRoundedRectangle ((float) getWidth() - 8, (float) kHead, 5, viewH, 2.5f);
                g.setColour (light.withAlpha (0.7f));
                g.fillRoundedRectangle ((float) getWidth() - 8, (float) kHead + (viewH - th) * scroll / maxScroll(), 5, th, 2.5f);
            }
        }
        void resized() override { scroll = juce::jlimit (0.0f, maxScroll(), scroll); }
        int sendAt (juce::Point<float> p, bool& onNumber) const
        {
            if (ed.selectedInsert <= 0 || p.y < kHead) return -1;
            for (int k = 0; k < fruit::kSends; ++k)
                if (sendRow (k).contains (p)) { onNumber = sendNum (k).expanded (3, 0).contains (p); return k; }
            return -1;
        }
        void setLevelFrom (int k, float x)
        {
            auto br = sendBar (k);
            ed.proc.setSendLevel (ed.selectedInsert, k, juce::jlimit (0.0f, 1.0f, (x - br.getX() - 2.0f) / juce::jmax (1.0f, br.getWidth() - 4.0f)));
            repaint();
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            bool num = false;
            dragSend = sendAt (e.position, num);
            grabScroll = scroll;
            dragKind = 3;
            if (dragSend >= 0)
            {
                const auto& sd = ed.proc.inserts[(size_t) ed.selectedInsert].sends[(size_t) dragSend];
                if (num) { dragKind = 1; grabTarget = sd.target; }
                else if (sd.target >= 0) dragKind = 2;
            }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasDraggedSinceMouseDown()) return;
            if (dragKind == 1)
            {
                // only shown while dragging: the tracks passed on the way are not touched
                previewTarget = juce::jlimit (-1, fruit::kSlots - 1, grabTarget - e.getDistanceFromDragStartY() / 7);
                repaint();
            }
            else if (dragKind == 2) setLevelFrom (dragSend, e.position.x);
            else { scroll = juce::jlimit (0.0f, maxScroll(), grabScroll - (float) e.getDistanceFromDragStartY()); repaint(); }
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            bool num = false;
            const int k = sendAt (e.position, num);
            if (k >= 0 && ! num) { ed.proc.setSendLevel (ed.selectedInsert, k, 1.0f); repaint(); }
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            scroll = juce::jlimit (0.0f, maxScroll(), scroll - w.deltaY * 220.0f);
            repaint();
        }
        void chooseTarget (int k)
        {
            const int i = ed.selectedInsert;
            juce::PopupMenu m, all;
            m.addItem (9, "None");
            m.addItem (10, "00  Master");
            m.addSeparator();
            for (int t = 1; t < fruit::kSlots; ++t)
                if (t != i && ed.proc.slots[(size_t) t].synthId >= 0)
                    m.addItem (10 + t, two (t) + "  " + ed.proc.insertName (t), ! ed.proc.wouldLoop (i, t));
            for (int d = 0; d < 10; ++d)
            {
                juce::PopupMenu tens;
                for (int t = juce::jmax (1, d * 10); t < juce::jmin (fruit::kSlots, d * 10 + 10); ++t)
                    tens.addItem (10 + t, two (t) + "  " + ed.proc.insertName (t), t != i && ! ed.proc.wouldLoop (i, t));
                all.addSubMenu (two (juce::jmax (1, d * 10)) + " - " + two (juce::jmin (fruit::kSlots - 1, d * 10 + 9)), tens);
            }
            m.addSubMenu ("Any track", all);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                [safe = juce::Component::SafePointer<Slots> (this), i, k] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    if (! safe->ed.proc.setSendTarget (i, k, r - 10))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit DAW",
                            "That track is already one of this insert's sends, or it leads back here.");
                    safe->ed.refreshAll();
                });
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const int kind = dragKind;
            dragKind = 0;
            const int landed = previewTarget;
            previewTarget = -2;
            if (e.mouseWasDraggedSinceMouseDown())
            {
                // a dragged number: set where it was let go (if it may go there)
                if (kind == 1 && landed > -2 && dragSend >= 0
                    && landed != ed.proc.inserts[(size_t) ed.selectedInsert].sends[(size_t) dragSend].target && canTarget (landed))
                    ed.proc.setSendTarget (ed.selectedInsert, dragSend, landed);
                ed.refreshAll();
                return;
            }
            const int i = ed.selectedInsert;
            if (kind == 1) { if (e.mods.isPopupMenu()) { ed.proc.setSendTarget (i, dragSend, -1); repaint(); } else chooseTarget (dragSend); return; }
            if (kind == 2) { setLevelFrom (dragSend, e.position.x); return; }
            if (dragSend >= 0) { chooseTarget (dragSend); return; }     // an unused send's bar
            const int k = (int) ((e.position.y - kHead + scroll) / kFxH);
            if (e.position.y < kHead || k < 0 || k >= fruit::kFxPerInsert) return;
            const int id = ed.proc.inserts[(size_t) i].exists() ? ed.proc.inserts[(size_t) i].fx[(size_t) k] : -1;
            auto choose = [this, i, k]
            {
                auto list = FruitProcessor::effectChoices();
                choiceMenu (list, true).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                    [this, i, k, list] (int r)
                    {
                        if (r <= 0) return;
                        ed.closeCards (ed.proc.inserts[(size_t) i].fx[(size_t) k]);
                        ed.proc.loadEffect (i, k, list[(size_t) r - 1].typeId);
                        ed.refreshAll();
                    });
            };
            if (id < 0 || ed.proc.getEngine().getInstance (id) == nullptr) { choose(); return; }
            const auto name = ed.proc.getEngine().getInstance (id)->descriptor().displayName;
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m;
                m.addItem (1, "Open / Close");
                m.addItem (2, "Replace...");
                m.addItem (3, "Remove");
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, i, k, id, name, choose] (int r)
                {
                    if (r == 1) ed.openCard (id, (i == 0 ? juce::String ("Master") : "Insert " + two (i)) + "  " + name);
                    if (r == 2) choose();
                    if (r == 3) { ed.closeCards (id); ed.proc.clearEffect (i, k); ed.refreshAll(); }
                });
                return;
            }
            ed.openCard (id, (i == 0 ? juce::String ("Master") : "Insert " + two (i)) + "  " + name);
        }
    };

    explicit Mixer (FruitEditor& e) : ed (e), strips (e), slots (e)
    {
        title = "Mixer";
        addAndMakeVisible (strips);
        addAndMakeVisible (slots);
    }
    void refresh()
    {
        title = "Mixer - " + (ed.selectedInsert == 0 ? juce::String ("Master") : "Insert " + juce::String (ed.selectedInsert));
        repaint();
        strips.repaint();
        slots.repaint();
    }
    void resized() override
    {
        auto b = body().reduced (6, 6);
        slots.setBounds (b.removeFromRight (juce::jmin (250, b.getWidth() / 3)));
        b.removeFromRight (6);
        strips.setBounds (b);
    }
    FruitEditor& ed;
    Strips strips;
    Slots slots;
};

//==============================================================================
juce::Point<int> FruitEditor::partSize (int part)
{
    switch (part)
    {
        case RackPart:     return { 640, 440 };
        case RollPart:     return { 900, 440 };
        case PlaylistPart: return { 640, 440 };
        case MixerPart:    return { 900, 440 };
        default:           return { 1600, 900 };
    }
}

juce::Component* FruitEditor::cardParent()
{
    // a single part is small: its slot cards open over the whole app instead
    if (part != All)
        if (auto* top = getTopLevelComponent(); top != nullptr && top != this) return top;
    return this;
}

FruitEditor::FruitEditor (FruitProcessor& p, int whichPart)
    : juce::AudioProcessorEditor (p), proc (p), part (whichPart),
      currentPattern (p.view.pattern), selectedInsert (p.view.insert), selectedTrack (p.view.track)
{
    currentPattern = proc.playingPattern.load();
    rack = std::make_unique<Rack> (*this);
    roll = std::make_unique<Roll> (*this);
    playlist = std::make_unique<Playlist> (*this);
    mixer = std::make_unique<Mixer> (*this);
    juce::Component* windows[] = { rack.get(), roll.get(), playlist.get(), mixer.get() };
    for (int i = 0; i < 4; ++i)
        if (part == All || part == i + 1) addAndMakeVisible (windows[i]); else addChildComponent (windows[i]);
    if (part == All || part == RackPart) tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    setResizable (true, true);
    if (part == All) setResizeLimits (960, 540, 3840, 2160); else setResizeLimits (420, 280, 2400, 1600);
    setSize (partSize (part).x, partSize (part).y);
    refreshAll();
    startTimerHz (30);
}

FruitEditor::~FruitEditor() { cards.clear(); }

void FruitEditor::paint (juce::Graphics& g) { g.fillAll (desk); }

void FruitEditor::resized()
{
    if (part != All)
    {
        juce::Component* windows[] = { rack.get(), roll.get(), playlist.get(), mixer.get() };
        windows[part - 1]->setBounds (getLocalBounds().reduced (4));
        return;
    }
    // the 2 x 2 grid: synths | piano roll over playlist | mixer
    auto r = getLocalBounds().reduced (6);
    const int gap = 6;
    const int leftW = (r.getWidth() - gap) * 42 / 100;
    const int topH = (r.getHeight() - gap) / 2;
    auto top = r.removeFromTop (topH);
    r.removeFromTop (gap);
    rack->setBounds (top.removeFromLeft (leftW));
    top.removeFromLeft (gap);
    roll->setBounds (top);
    playlist->setBounds (r.removeFromLeft (leftW));
    r.removeFromLeft (gap);
    mixer->setBounds (r);
    if (part == All)
        for (auto& c : cards)
            c->setTopLeftPosition (juce::jlimit (0, juce::jmax (0, getWidth() - 60), c->getX()), juce::jlimit (0, juce::jmax (0, getHeight() - 40), c->getY()));
}

void FruitEditor::openCard (int moduleId, const juce::String& title)
{
    for (auto it = cards.begin(); it != cards.end(); ++it)
        if ((*it)->getModuleId() == moduleId) { cards.erase (it); rack->rows.repaint(); return; }   // open: close it
    if (proc.getEngine().getInstance (moduleId) == nullptr) return;
    auto card = std::make_unique<FruitSlotCard> (proc, moduleId, title);
    card->onClose = [this, moduleId] { const juce::Component::SafePointer<FruitEditor> safe (this); juce::MessageManager::callAsync ([safe, moduleId] { if (safe) safe->closeCards (moduleId); }); };
    const int off = (int) cards.size() * 24;
    auto* parent = cardParent();
    card->setSize (juce::jmin (card->getWidth(), parent->getWidth() - 20), juce::jmin (card->getHeight(), parent->getHeight() - 20));
    card->setTopLeftPosition (juce::jmax (0, (parent->getWidth() - card->getWidth()) / 2 + off), juce::jmax (0, (parent->getHeight() - card->getHeight()) / 3 + off));
    parent->addAndMakeVisible (*card);
    cards.push_back (std::move (card));
    rack->rows.repaint();
}

void FruitEditor::closeCards (int moduleId)
{
    cards.erase (std::remove_if (cards.begin(), cards.end(), [moduleId] (const auto& c) { return c->getModuleId() == moduleId; }), cards.end());
    rack->rows.repaint();
}

void FruitEditor::patternSelected (int p)
{
    currentPattern = juce::jlimit (1, fruit::kSlots - 1, p);
    proc.view.sample = 0;                // the playlist's brush is this pattern again
    proc.keysSlot.store (proc.song.patterns[(size_t) currentPattern].slot);
    if (proc.getMode() == FruitProcessor::Mode::pattern) proc.playPattern (currentPattern);
    else proc.playingPattern.store (currentPattern);
    refreshAll();
}

void FruitEditor::refreshAll()
{
    shownVersion = proc.uiVersion.load();
    shownView = proc.view.sample * 1000000 + currentPattern * 10000 + selectedInsert * 100 + selectedTrack;
    roll->refresh();
    playlist->refresh();
    mixer->refresh();
    rack->rows.repaint();
}

void FruitEditor::timerCallback()
{
    // (another card of the same Fruit DAW may have changed what is selected)
    if (shownVersion != proc.uiVersion.load() || shownView != proc.view.sample * 1000000 + currentPattern * 10000 + selectedInsert * 100 + selectedTrack)
        refreshAll();
    if (part == All || part == RollPart)
    {
        proc.collectRecorded();
        roll->record.setToggleState (proc.recording.load(), juce::dontSendNotification);
    }
    const auto mode = proc.getMode();
    roll->play.setToggleState (mode == FruitProcessor::Mode::pattern, juce::dontSendNotification);
    playlist->play.setToggleState (mode == FruitProcessor::Mode::song, juce::dontSendNotification);
    if (mode == FruitProcessor::Mode::pattern) roll->gridView.repaint();
    if (mode == FruitProcessor::Mode::song) playlist->gridView.repaint();
    mixer->strips.repaint();
    if (part == All || part == PlaylistPart)
    {
        // stretched samples follow the song's tempo once it has stood still for half a second
        const double bpm = proc.currentBpm();
        if (std::abs (bpm - tempoSeen) > 0.005) { tempoSeen = bpm; tempoStill = 0; }
        else if (++tempoStill == 15 && proc.updateStretches()) refreshAll();
    }
    if ((part == All || part == PlaylistPart) && ++scanTick >= 10)   // three times a second: which knob was turned last
    {
        scanTick = 0;
        const auto before = proc.lastKnob.label;
        proc.scanForTurnedKnobs();
        if (proc.lastKnob.label != before) playlist->refresh();
    }
}
