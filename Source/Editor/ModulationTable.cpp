// The sidebar's modulation table: every knob cable, source and target.
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
// ModulationTable
//==============================================================================
struct ModulationTable::RowView : public juce::Component
{
    ModulationTable& table;
    Key key;
    // left: the source module, under it its output; right: the target module, under it its knob
    juce::Label source, socket, target, knob;
    juce::Slider amount;
    juce::TextButton bipolar { "Bi" };
    juce::Colour colour;
    bool absolute = false;

    RowView (ModulationTable& t, const Key& k) : table (t), key (k)
    {
        for (auto* l : { &source, &socket, &target, &knob })
        {
            const bool name = l == &source || l == &target;
            l->setFont (juce::FontOptions (name ? 12.0f : 10.5f, name ? juce::Font::bold : juce::Font::plain));
            l->setMinimumHorizontalScale (0.7f);
            l->setBorderSize ({ 0, 0, 0, 0 });
            l->setInterceptsMouseClicks (false, false);
            l->setJustificationType (name || l == &socket ? juce::Justification::centredLeft : juce::Justification::centredRight);
            addAndMakeVisible (l);
        }
        target.setJustificationType (juce::Justification::centredRight);

        amount.setSliderStyle (juce::Slider::LinearBar);
        amount.setRange (-100.0, 100.0, 1.0);
        amount.setTextValueSuffix (" %");
        amount.setDoubleClickReturnValue (true, 25.0);
        amount.setTooltip ("How far the source moves the knob (-100..100 % of its range; a tap twice: 25 %)");
        amount.onValueChange = [this]
        {
            const int i = table.findIndex (key);
            if (i >= 0) table.processor.setParamCableDepth (i, (float) amount.getValue() * 0.01f);
        };
        addAndMakeVisible (amount);

        bipolar.setClickingTogglesState (true);
        bipolar.setTooltip ("Bipolar: the knob moves both ways around its setting (as in Vita); off: one way, from its setting up "
                            "(down for a negative Amount)");
        bipolar.onClick = [this]
        {
            const int i = table.findIndex (key);
            if (i >= 0) table.processor.setParamCableBipolar (i, bipolar.getToggleState());
        };
        addAndMakeVisible (bipolar);
    }

    void update (const ParamCableInfo& pc)
    {
        juce::String src = "?", out = pc.fromSocket, dst = "?", par = pc.paramId;
        if (auto* a = table.processor.getInstance (pc.fromModule))
        {
            src = a->descriptor().displayName;
            for (const auto* sock : a->descriptor().outputs())
                if (sock->id == pc.fromSocket) out = sock->label;
            colour = a->cardColour();
            // a Macro or a Curve CV sets the knob itself (0..1 = its whole range): no
            // polarity to choose
            absolute = a->dsp != nullptr && a->dsp->drivesKnobsAbsolutely();
        }
        if (auto* b = table.processor.getInstance (pc.toModule))
        {
            dst = b->descriptor().displayName;
            if (const auto* spec = b->descriptor().findParam (pc.paramId))
                par = spec->label;
        }
        source.setText (src, juce::dontSendNotification);
        socket.setText (out, juce::dontSendNotification);
        target.setText (dst, juce::dontSendNotification);
        knob.setText (par, juce::dontSendNotification);
        source.setTooltip (src + " - " + out);
        target.setTooltip (dst + " - " + par);

        const auto text = SidebarComponent::textColour();
        source.setColour (juce::Label::textColourId, text);
        target.setColour (juce::Label::textColourId, text);
        socket.setColour (juce::Label::textColourId, text.withAlpha (0.6f));
        knob.setColour (juce::Label::textColourId, text.withAlpha (0.6f));
        amount.setColour (juce::Slider::trackColourId, colour.withAlpha (0.85f));
        amount.setColour (juce::Slider::backgroundColourId, text.withAlpha (0.08f));
        amount.setColour (juce::Slider::textBoxTextColourId, text);
        amount.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        bipolar.setColour (juce::TextButton::buttonColourId, text.withAlpha (0.08f));
        bipolar.setColour (juce::TextButton::buttonOnColourId, colour);
        bipolar.setColour (juce::TextButton::textColourOffId, text.withAlpha (0.6f));
        bipolar.setColour (juce::TextButton::textColourOnId, themeColours().textOnCard (colour));

        if (! amount.isMouseButtonDown())
            amount.setValue (std::round (pc.depth * 100.0f), juce::dontSendNotification);
        bipolar.setToggleState (pc.bipolar && ! absolute, juce::dontSendNotification);
        bipolar.setEnabled (! absolute);
        bipolar.setAlpha (absolute ? 0.35f : 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto names = r.removeFromTop (16);
        auto parts = r.removeFromTop (14);
        const int half = names.getWidth() / 2;
        source.setBounds (names.removeFromLeft (half - 2));
        target.setBounds (names.withTrimmedLeft (2));
        socket.setBounds (parts.removeFromLeft (half - 2));
        knob.setBounds (parts.withTrimmedLeft (2));
        r.removeFromTop (4);
        auto line = r.removeFromTop (20);
        bipolar.setBounds (line.removeFromRight (32));
        line.removeFromRight (6);
        amount.setBounds (line);
    }
};

int ModulationTable::findIndex (const Key& k) const
{
    const auto& pcs = processor.getParamCables();
    for (int i = 0; i < (int) pcs.size(); ++i)
        if (pcs[(size_t) i].fromModule == k.from && pcs[(size_t) i].fromSocket == k.socket
            && pcs[(size_t) i].toModule == k.to && pcs[(size_t) i].paramId == k.param)
            return i;
    return -1;
}

namespace
{
    const char* const kModTableEmptyText = "Drag a modulation output onto a knob (or drop an LFO, an envelope... right on it): its row appears here.";

    juce::TextLayout modTableEmptyLayout (int width)
    {
        juce::AttributedString s;
        s.append (kModTableEmptyText, juce::FontOptions (10.5f), SidebarComponent::textColour().withAlpha (0.5f));
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (s, (float) juce::jmax (40, width));
        return tl;
    }
}

int ModulationTable::desiredHeight() const
{
    // with no rows, as tall as the hint needs at this width (a narrow sidebar wraps it
    // onto more lines; it was cut off at three)
    if (rowViews.empty())
        return headerH + (int) std::ceil (modTableEmptyLayout (getWidth()).getHeight()) + 10;
    return headerH + (int) rowViews.size() * rowH;
}

ModulationTable::ModulationTable (AquanodeModularAudioProcessor& p) : processor (p) {}
ModulationTable::~ModulationTable() = default;

void ModulationTable::refresh()
{
    const auto& pcs = processor.getParamCables();
    juce::String now;
    for (const auto& pc : pcs)
        now << pc.fromModule << "." << pc.fromSocket << ">" << pc.toModule << "." << pc.paramId << ";";

    if (now != structure)
    {
        structure = now;
        rowViews.clear();
        for (const auto& pc : pcs)
        {
            auto row = std::make_unique<RowView> (*this, Key { pc.fromModule, pc.fromSocket, pc.toModule, pc.paramId });
            addAndMakeVisible (*row);
            rowViews.push_back (std::move (row));
        }
        const int h = desiredHeight();
        if (h != getHeight())
        {
            setSize (getWidth(), h);
            if (onHeightChanged) onHeightChanged();
        }
        resized();
        repaint();
    }
    // the rows repaint themselves only where a value or a colour really changed: the table costs
    // nothing while the patch sits still
    for (size_t i = 0; i < rowViews.size() && i < pcs.size(); ++i)
        rowViews[i]->update (pcs[i]);
}

void ModulationTable::resized()
{
    // the hint's height follows the width: tell the sidebar once the message loop is free
    if (rowViews.empty() && desiredHeight() != getHeight())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ModulationTable> (this)]
        {
            if (safe == nullptr || ! safe->rowViews.empty()) return;
            const int h = safe->desiredHeight();
            if (h != safe->getHeight())
            {
                safe->setSize (safe->getWidth(), h);
                if (safe->onHeightChanged) safe->onHeightChanged();
            }
        });
    int y = headerH;
    for (auto& r : rowViews)
    {
        r->setBounds (0, y, getWidth(), rowH - 10);
        y += rowH;
    }
}

void ModulationTable::paint (juce::Graphics& g)
{
    const auto text = SidebarComponent::textColour();
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Modulation", 0, 0, getWidth(), 20, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.55f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("Source / Output", 0, 22, getWidth() / 2, 12, juce::Justification::centredLeft);
    g.drawText ("Target / Knob", getWidth() / 2, 22, getWidth() / 2, 12, juce::Justification::centredRight);
    // a faint line between the rows
    g.setColour (text.withAlpha (0.12f));
    for (int k = 0; k < (int) rowViews.size(); ++k)
        g.fillRect (0, headerH + k * rowH - 5, getWidth(), 1);
    if (rowViews.empty())
        modTableEmptyLayout (getWidth()).draw (g, juce::Rectangle<float> (0.0f, (float) headerH, (float) getWidth(),
                                                                            (float) juce::jmax (1, getHeight() - headerH)));
}
