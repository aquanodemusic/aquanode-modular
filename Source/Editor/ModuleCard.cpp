// A module's card: its title, sockets, knobs and combos, its menus, and the knob
// cables drawn over it.
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
// ModuleComponent
//==============================================================================
ModuleComponent::ModuleComponent (AquanodeModularAudioProcessor& proc, PatchCanvas& owner, int idToUse)
    : processor (proc), canvas (owner), instanceId (idToUse)
{
    buildControls();
    applySkin();
    const int h = layoutEverything (false);
    setSize (moduleWidth(), h);

    // Bounds are WORLD coordinates; pan and zoom live in the transform that
    // the canvas pushes onto every module (see PatchCanvas::applyViewToChildren).
    if (auto* inst = processor.getInstance (instanceId))
        setTopLeftPosition (inst->position);
    setTransform (canvas.worldToScreen());

    layoutEverything (true);
}

// the widest row of controls, in knob units (a module with rows of more than
// five knobs grows to fit them rather than letting them spill over its edge)
int ModuleComponent::maxRowUnits() const
{
    std::map<int, int> units;
    if (auto* inst = processor.getInstance (instanceId))
        for (const auto& spec : inst->descriptor().params)
            if (! spec.hidden)
                units[spec.row] += juce::jlimit (1, layout::unitsPerRow, spec.widthUnits);
    int most = layout::unitsPerRow;
    for (const auto& [row, n] : units)
        most = juce::jmax (most, n);
    return most;
}

int ModuleComponent::moduleWidth() const
{
    int w = defaultModuleWidth;
    if (auto* inst = processor.getInstance (instanceId))
        if (const int pw = inst->dsp->preferredModuleWidth(); pw > 0)
            w = pw;
    const int unitW = (defaultModuleWidth - 2 * layout::pad) / layout::unitsPerRow;
    return juce::jmax (w, 2 * layout::pad + maxRowUnits() * unitW);
}

void ModuleComponent::applySkin()
{
    // the Skeuomorphic look on the card's own controls (never on a plugin's panel)
    auto* laf = themeColours().skeuomorphic ? &skeuoLookAndFeel() : nullptr;
    for (auto& c : controls)
        if (c.component != nullptr)
            c.component->setLookAndFeel (laf);   // a slider makes its text box anew, in the new look
    repaint();
}

bool ModuleComponent::isParamVisible (const ParamSpec& spec) const
{
    if (spec.visibleWhenParamId.isEmpty())
        return true;

    if (auto* inst = processor.getInstance (instanceId))
        return std::abs (inst->dsp->getParameter (spec.visibleWhenParamId) - spec.visibleWhenEquals) < 0.5f;

    return true;
}

void ModuleComponent::buildControls()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    const auto& desc = inst->descriptor();
    auto* dsp = inst->dsp.get();
    juce::WeakReference<SynthModule> weakDsp (dsp);

    for (const auto& spec : desc.params)
    {
        if (spec.hidden)
            continue;   // owned by the module's custom extra-content component

        ControlEntry entry;
        entry.spec = &spec;

        switch (spec.type)
        {
            case ParamType::Rotary:
            case ParamType::RotarySteppedList:
            {
                auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag,
                                                              juce::Slider::TextBoxBelow);
                slider->setRange (spec.minValue, spec.maxValue, spec.interval);
                if (spec.evenDecades && spec.minValue > 0.0f)
                {
                    const double lo = spec.minValue, hi = spec.maxValue;
                    slider->setNormalisableRange ({ lo, hi,
                        [] (double a, double b, double n) { return a * std::pow (b / a, n); },
                        [] (double a, double b, double v) { return std::log (juce::jlimit (a, b, v) / a) / std::log (b / a); },
                        [] (double a, double b, double v) { return juce::jlimit (a, b, v); } });
                }
                else if (spec.logarithmic && spec.minValue > 0.0f)
                    slider->setSkewFactorFromMidPoint (std::sqrt ((double) spec.minValue * spec.maxValue));

                if (spec.type == ParamType::RotarySteppedList)
                {
                    const auto choices = spec.choices;
                    slider->textFromValueFunction = [choices] (double v)
                    {
                        return choices[juce::jlimit (0, choices.size() - 1, (int) std::round (v))];
                    };
                    slider->valueFromTextFunction = [choices] (const juce::String& t)
                    {
                        const auto trimmed = t.trim();
                        for (int i = 0; i < choices.size(); ++i)
                            if (choices[i].equalsIgnoreCase (trimmed))
                                return (double) i;
                        return t.getDoubleValue();   // typed a raw number instead
                    };
                }
                else
                {
                    if (spec.unitSuffix.isNotEmpty())
                        slider->setTextValueSuffix (" " + spec.unitSuffix);
                    const float range = spec.maxValue - spec.minValue;
                    int dp = spec.interval == 1.0f ? 0
                             : range > 1000.0f ? 0
                             : range > 20.0f ? 1 : 2;
                    if (spec.logarithmic && spec.minValue < 0.1f)
                        dp = 3;   // e.g. FM Ratio 0.001..100 needs fine resolution near the bottom
                    slider->setNumDecimalPlacesToDisplay (dp);

                    // frequencies: "1.80k" fits the box where "1800 Hz" did not
                    if (spec.unitSuffix == "Hz")
                    {
                        slider->setTextValueSuffix ({});
                        slider->textFromValueFunction = [] (double v)
                        {
                            if (v >= 10000.0) return juce::String (v / 1000.0, 1) + "k";
                            if (v >= 1000.0)  return juce::String (v / 1000.0, 2) + "k";
                            return juce::String (v, v < 10.0 ? 2 : (v < 100.0 ? 1 : 0)) + " Hz";
                        };
                        slider->valueFromTextFunction = [] (const juce::String& t)
                        {
                            const auto s = t.trim().toLowerCase();
                            const double x = s.getDoubleValue();
                            return s.containsChar ('k') ? x * 1000.0 : x;
                        };
                    }
                }

                if (spec.textFromValue)
                {
                    slider->setTextValueSuffix ({});
                    slider->textFromValueFunction = spec.textFromValue;
                    if (spec.valueFromText)
                        slider->valueFromTextFunction = spec.valueFromText;
                }

                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 17);
                slider->setDoubleClickReturnValue (true, spec.defaultValue);   // double-click resets
                slider->setValue (dsp->getParameter (spec.id), juce::dontSendNotification);

                // setValue() does not redraw the text when the value is unchanged,
                // and the box was filled in by setRange() before the choice names
                // existed - a stepped knob sitting on its first choice used to
                // read "0" instead of "-LIN"
                slider->updateText();

                // does any other parameter's visibility depend on this knob?
                // (e.g. Glide only exists while Voices == 1) - if so the knob
                // has to trigger a re-layout, exactly like a combo does
                bool drivesVisibility = false;
                for (const auto& other : desc.params)
                    if (other.visibleWhenParamId == spec.id)
                    {
                        drivesVisibility = true;
                        break;
                    }

                slider->onValueChange = [this, weakDsp, paramId = spec.id,
                                         s = slider.get(), drivesVisibility]
                {
                    if (auto* m = weakDsp.get())
                        m->setParameter (paramId, (float) s->getValue());
                    if (drivesVisibility)
                        refreshLayout();
                };
                entry.component = std::move (slider);
                break;
            }

            case ParamType::HBar:
            {
                auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                              juce::Slider::TextBoxRight);
                slider->setRange (spec.minValue, spec.maxValue, spec.interval);
                if (spec.unitSuffix.isNotEmpty())
                    slider->setTextValueSuffix (" " + spec.unitSuffix);
                if (spec.logarithmic && spec.minValue > 0.0f)
                    slider->setSkewFactorFromMidPoint (std::sqrt ((double) spec.minValue * spec.maxValue));
                const float hrange = spec.maxValue - spec.minValue;
                slider->setNumDecimalPlacesToDisplay (spec.interval == 1.0f || hrange > 1000.0f ? 0
                                                      : hrange > 20.0f ? 1 : 2);
                slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 16);
                slider->setDoubleClickReturnValue (true, spec.defaultValue);
                slider->setValue (dsp->getParameter (spec.id), juce::dontSendNotification);
                slider->onValueChange = [weakDsp, paramId = spec.id, sl = slider.get()]
                {
                    if (auto* m = weakDsp.get())
                        m->setParameter (paramId, (float) sl->getValue());
                };
                entry.component = std::move (slider);
                break;
            }

            case ParamType::Combo:
            {
                auto combo = std::make_unique<juce::ComboBox>();
                combo->addItemList (spec.choices, 1);
                combo->setSelectedItemIndex ((int) dsp->getParameter (spec.id), juce::dontSendNotification);
                combo->onChange = [this, weakDsp, paramId = spec.id, c = combo.get(), routing = spec.affectsRouting]
                {
                    if (auto* m = weakDsp.get())
                        m->setParameter (paramId, (float) c->getSelectedItemIndex());
                    if (routing)
                        processor.routingChanged();   // a Reroute module's bus
                    refreshLayout();   // a combo may drive another param's visibility
                };
                entry.component = std::move (combo);
                break;
            }

            case ParamType::Button:
            {
                auto button = std::make_unique<juce::TextButton> (spec.label);
                button->onClick = [weakDsp, paramId = spec.id,
                                   safeThis = juce::Component::SafePointer<ModuleComponent> (this)]
                {
                    if (auto* m = weakDsp.get())
                        m->uiButtonClicked (paramId);

                    // a button may set the module's own knobs (Rand Tone)
                    if (safeThis != nullptr)
                        safeThis->refreshFromModel();
                };
                entry.component = std::move (button);
                break;
            }
        }

        // right-clicks on modulatable knobs open the modulation menu
        if ((spec.type == ParamType::Rotary || spec.type == ParamType::RotarySteppedList
             || spec.type == ParamType::HBar) && spec.modulatable)
            entry.component->addMouseListener (this, false);
        addAndMakeVisible (entry.component.get());
        controls.push_back (std::move (entry));
    }

    extraContent = dsp->createExtraContentComponent();
    if (extraContent != nullptr)
    {
        addAndMakeVisible (extraContent.get());

        // custom cable targets (e.g. Pitch Lock Filter keys) need their mouse
        // events mirrored here so right-click opens the modulation menu and
        // rapid clicks count toward the self-cable delete gesture
        if (dynamic_cast<CustomParamCableTargets*> (extraContent.get()) != nullptr)
            extraContent->addMouseListener (this, false);

        // custom UIs that set ordinary params themselves (Piano Roll's editor)
        // ask for the generic knobs to be refreshed afterwards
        if (auto* listener = dynamic_cast<ExtraContentParamListener*> (extraContent.get()))
        {
            listener->paramsChangedByContent = [this] { refreshFromModel(); };
            listener->paramValuesChangedByContent = [this] { refreshControlValues(); };
        }
    }
}

int ModuleComponent::layoutEverything (bool apply)
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return 40;

    const auto& desc = inst->descriptor();
    const int innerW = moduleWidth() - 2 * layout::pad;
    int y = layout::topRow;

    if (apply)
        sockets.clear();

    // ---- 3-column header: input column (left) | title (centre) | output column (right)
    // (hidden sockets - a Reroute module's bus - are wired by the graph alone)
    auto visibleOnly = [] (std::vector<const SocketSpec*> list)
    {
        list.erase (std::remove_if (list.begin(), list.end(), [] (const SocketSpec* sk) { return sk->hidden; }), list.end());
        return list;
    };
    const auto inList = visibleOnly (desc.inputs());
    const auto outList = visibleOnly (desc.outputs());
    const int headerRows = juce::jmax ((int) inList.size(), (int) outList.size());
    const int headerH = juce::jmax (layout::headerMinH, headerRows * layout::socketSlotH + layout::pad);

    if (apply)
    {
        const int leftX = layout::pad + layout::socketSize / 2 + 2;
        const int rightX = moduleWidth() - layout::pad - layout::socketSize / 2 - 2;

        auto placeColumn = [&] (const std::vector<const SocketSpec*>& list, bool isInput, int cx)
        {
            const int n = (int) list.size();
            for (int i = 0; i < n; ++i)
            {
                const int slotTop = y + (headerH - n * layout::socketSlotH) / 2 + i * layout::socketSlotH;
                SocketRef ref;
                ref.socketId = list[(size_t) i]->id;
                ref.isInput = isInput;
                ref.kind = list[(size_t) i]->kind;
                ref.centre = { cx, slotTop + layout::socketSlotH / 2 };
                sockets.push_back (ref);
            }
        };

        placeColumn (inList, true, leftX);
        placeColumn (outList, false, rightX);
    }

    headerHeight = headerH;
    y += headerH + 2;

    // Collapsed: stop at the header. Every control is hidden (so it cannot be
    // clicked through the shrunken rectangle) but nothing is destroyed, and
    // the sockets placed above stay exactly where they were relative to the
    // header - so audio cables neither move nor break.
    if (isCollapsed())
    {
        if (apply)
        {
            for (auto& c : controls)
            {
                c.visible = false;
                c.component->setVisible (false);
            }
            if (extraContent != nullptr)
                extraContent->setVisible (false);
        }
        return y + layout::pad - 4;
    }

    if (apply && extraContent != nullptr)
        extraContent->setVisible (true);

    // 4. knob rows per explicit descriptor row/widthUnits (max 5 units per row)
    constexpr int sectionH = 20;   // a titled separator (rowSections)
    if (apply)
        sectionHeaders.clear();
    int currentRow = -1;
    bool first = true;
    for (auto rowIt = 0;; ++rowIt)
    {
        // gather visible params of the next existing row index
        int nextRow = std::numeric_limits<int>::max();
        for (auto& c : controls)
            if (isParamVisible (*c.spec) && c.spec->row > currentRow)
                nextRow = juce::jmin (nextRow, c.spec->row);

        if (nextRow == std::numeric_limits<int>::max())
            break;

        currentRow = nextRow;
        juce::ignoreUnused (rowIt, first);

        std::vector<ControlEntry*> rowControls;
        bool hasKnob = false, hasBar = false, hasTitledCombo = false;
        for (auto& c : controls)
        {
            const bool visible = isParamVisible (*c.spec);
            if (c.spec->row == currentRow)
            {
                if (apply)
                {
                    c.visible = visible;
                    c.component->setVisible (visible);
                }
                if (visible)
                {
                    rowControls.push_back (&c);
                    if (c.spec->type == ParamType::Rotary || c.spec->type == ParamType::RotarySteppedList)
                        hasKnob = true;
                    else if (c.spec->type == ParamType::HBar)
                        hasBar = true;
                    else if (c.spec->type == ParamType::Combo && c.spec->label.isNotEmpty())
                        hasTitledCombo = true;
                }
            }
        }

        if (rowControls.empty())
            continue;

        if (const auto sec = desc.rowSections.find (currentRow); sec != desc.rowSections.end())
        {
            if (apply)
                sectionHeaders.push_back ({ y, sec->second });
            y += sectionH;
        }

        // a row of horizontal bars only needs a label and the bar itself
        // a list (combo box) carries its name above it like a knob does, so a
        // row of lists only grows by that title
        const int rowH = hasKnob ? layout::knobRowH : hasBar ? layout::barRowH
                       : layout::wideRowH + (hasTitledCombo ? 14 : 0);
        const int unitW = innerW / maxRowUnits();

        if (apply)
        {
            int x = layout::pad;
            for (auto* c : rowControls)
            {
                const int w = unitW * juce::jlimit (1, layout::unitsPerRow, c->spec->widthUnits);
                auto area = juce::Rectangle<int> (x, y, w, rowH);

                if (c->spec->type == ParamType::Rotary || c->spec->type == ParamType::RotarySteppedList
                    || c->spec->type == ParamType::HBar)
                {
                    c->labelArea = area.removeFromTop (14);
                    c->component->setBounds (area.reduced (1));
                }
                else if (c->spec->type == ParamType::Combo && c->spec->label.isNotEmpty())
                {
                    // the list's name on top, in line with the knobs' names
                    c->labelArea = area.removeFromTop (14);
                    c->component->setBounds (area.withSizeKeepingCentre (w - 6, 24)
                                                 .withY (hasKnob ? y + (rowH - 24) / 2 + 6 : y + 16));
                }
                else
                {
                    c->labelArea = {};
                    c->component->setBounds (area.withSizeKeepingCentre (w - 6, 24)
                                                 .withY (y + (rowH - 24) / 2 + (hasKnob ? 6 : 0)));
                }
                x += w;
            }
        }
        y += rowH + 2;
    }

    // hide params of rows we never visited (fully hidden rows)
    if (apply)
        for (auto& c : controls)
            if (! isParamVisible (*c.spec))
            {
                c.visible = false;
                c.component->setVisible (false);
            }

    // 5. optional custom content (Sampler waveform display)
    if (auto* dsp = inst->dsp.get(); dsp != nullptr && extraContent != nullptr)
    {
        if (desc.extraSection.isNotEmpty())
        {
            if (apply)
                sectionHeaders.push_back ({ y, desc.extraSection });
            y += sectionH;
        }
        const int h = dsp->extraContentHeight();
        if (apply)
            extraContent->setBounds (layout::pad, y, innerW, h);
        y += h + 4;
    }

    return y + layout::pad - 2;
}

bool ModuleComponent::showsConvertPill() const
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return false;
    auto* hosted = dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get());
    if (hosted == nullptr)
        return false;
    auto* entry = aquanode::plugins::findEntry (hosted->getSpec().pluginId);
    return entry != nullptr && entry->hasPatchVersion();
}

juce::Rectangle<int> ModuleComponent::convertPillArea() const
{
    // right of the centred title, clear of the output socket column
    return { getWidth() - layout::socketColW - 96, layout::topRow + 4, 84, 22 };
}

bool ModuleComponent::isCollapsed() const
{
    if (auto* inst = processor.getInstance (instanceId))
        return inst->collapsed;
    return false;
}

void ModuleComponent::toggleCollapsed()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    inst->collapsed = ! inst->collapsed;
    refreshLayout();                  // re-measures and resizes the rectangle
    canvas.moduleCollapsedChanged();  // param cables to hidden knobs stop drawing
}

juce::Rectangle<int> ModuleComponent::collapseTabArea() const
{
    // centred in the top row, above the title
    return juce::Rectangle<int> (moduleWidth() / 2 - 15, (layout::topRow - 6) / 2, 30, 6);
}

juce::Rectangle<int> ModuleComponent::collapseHitArea() const
{
    // generous enough for a fingertip, but confined to the top margin so it
    // never swallows a press meant for the title area or a socket
    return collapseTabArea().withSizeKeepingCentre (54, layout::topRow).withY (0);
}

bool ModuleComponent::showsRandomize() const
{
    if (auto* inst = processor.getInstance (instanceId))
        return aquanode::mutation::hasMutableParams (inst->descriptor());
    return false;
}

juce::Rectangle<int> ModuleComponent::randomizeHitArea() const
{
    // the "Random" / "Revert" button at the left of the top row (the
    // collapse tab keeps the middle)
    return { layout::pad, 0, 62, layout::topRow };
}

void ModuleComponent::randomizeCard()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr || processor.moduleRandomUndo.count (instanceId) > 0)
        return;   // undo the last one first

    const auto& desc = inst->descriptor();
    std::map<juce::String, float> before;
    for (const auto& p : desc.params)
        if (p.type != ParamType::Button)
            before[p.id] = inst->dsp->getParameter (p.id);

    juce::Random rng;
    const float amount = juce::jlimit (0.01f, 1.0f, processor.mutationAmount);
    for (const auto& p : desc.params)
        if (! aquanode::mutation::skipsParam (desc, p))
            inst->dsp->setParameter (p.id, aquanode::mutation::mutateValue (p, inst->dsp->getParameter (p.id), amount, rng));

    processor.moduleRandomUndo[instanceId] = std::move (before);
    refreshFromModel();
    repaint();
}

void ModuleComponent::undoCard()
{
    auto* inst = processor.getInstance (instanceId);
    const auto it = processor.moduleRandomUndo.find (instanceId);
    if (inst == nullptr || it == processor.moduleRandomUndo.end())
        return;

    for (const auto& [id, v] : it->second)
        inst->dsp->setParameter (id, v);
    processor.moduleRandomUndo.erase (it);
    refreshFromModel();
    repaint();
}

bool ModuleComponent::isKnobVisible (const juce::String& paramId) const
{
    if (isCollapsed())
        return false;

    for (const auto& entry : controls)
        if (entry.spec->id == paramId)
            return entry.visible;

    // hidden cable-target params live inside the custom UI
    if (extraContent != nullptr && extraContent->isVisible())
        if (dynamic_cast<const CustomParamCableTargets*> (extraContent.get()) != nullptr)
            return true;

    return true;
}

void ModuleComponent::refreshLayout()
{
    const int h = layoutEverything (false);
    setSize (moduleWidth(), h);
    layoutEverything (true);
    repaint();
    canvas.moduleMoved();
}

void ModuleComponent::refreshControlValues()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    for (auto& c : controls)
    {
        const float v = inst->dsp->getParameter (c.spec->id);
        if (auto* slider = dynamic_cast<juce::Slider*> (c.component.get()))
        {
            if (! slider->isMouseButtonDown())
                slider->setValue (v, juce::dontSendNotification);
        }
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (c.component.get()))
            combo->setSelectedItemIndex ((int) v, juce::dontSendNotification);
    }
}

void ModuleComponent::refreshFromModel()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    for (auto& c : controls)
    {
        const float v = inst->dsp->getParameter (c.spec->id);
        if (auto* slider = dynamic_cast<juce::Slider*> (c.component.get()))
            slider->setValue (v, juce::dontSendNotification);
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (c.component.get()))
            combo->setSelectedItemIndex ((int) v, juce::dontSendNotification);
    }

    refreshLayout();   // a mutated combo may change another param's visibility
}

const ModuleComponent::SocketRef* ModuleComponent::findSocketNear (juce::Point<int> localPos, int radius) const
{
    for (const auto& s : sockets)
        if (s.centre.getDistanceFrom (localPos) <= radius)
            return &s;
    return nullptr;
}

juce::Point<int> ModuleComponent::socketCentreInParent (const juce::String& socketId, bool isInput) const
{
    for (const auto& s : sockets)
        if (s.socketId == socketId && s.isInput == isInput)
            return getPosition() + s.centre;
    return getBounds().getCentre();
}

void ModuleComponent::paint (juce::Graphics& g)
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;

    const auto& desc = inst->descriptor();
    const auto body = inst->cardColour();
    const auto bounds = getLocalBounds().toFloat();
    const auto text = themeColours().textOnCard (body);   // Dark Text, or Light Text on a dark card

    const bool skeuo = themeColours().skeuomorphic;
    const float divY = (float) (layout::topRow + headerHeight + 1);
    const auto card = bounds.reduced (1.0f);

    if (skeuo)
    {
        // a satin panel lit from above, a touch translucent: a little lighter at
        // the top, a glossy header band, light coming up through the lower
        // edge, a thin bevel and four small flush screws
        juce::ColourGradient panel (body.brighter (0.16f).withAlpha (0.93f), 0.0f, card.getY(),
                                    body.darker (0.08f).withAlpha (0.90f), 0.0f, card.getBottom(), false);
        g.setGradientFill (panel);
        g.fillRoundedRectangle (card, 8.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (card, 8.0f);
            g.reduceClipRegion (clip);
            juce::ColourGradient rail (juce::Colours::white.withAlpha (0.30f), 0.0f, card.getY(),
                                       juce::Colours::black.withAlpha (0.05f), 0.0f, divY, false);
            rail.addColour (0.5, juce::Colours::white.withAlpha (0.10f));
            g.setGradientFill (rail);
            g.fillRect (card.withBottom (divY));
            const float glowH = juce::jmin (22.0f, card.getHeight() * 0.2f);
            juce::ColourGradient lower (body.brighter (0.5f).withAlpha (0.0f), 0.0f, card.getBottom() - glowH,
                                        body.brighter (0.6f).withAlpha (0.22f), 0.0f, card.getBottom(), false);
            g.setGradientFill (lower);
            g.fillRect (card.withTop (card.getBottom() - glowH));
        }
        juce::ColourGradient bevel (juce::Colours::white.withAlpha (0.55f), 0.0f, card.getY(),
                                    juce::Colours::black.withAlpha (0.25f), 0.0f, card.getBottom(), false);
        bevel.addColour (0.3, juce::Colours::white.withAlpha (0.05f));
        g.setGradientFill (bevel);
        g.drawRoundedRectangle (card.reduced (1.5f), 7.0f, 1.0f);
        for (auto p : { juce::Point<float> (card.getX() + 7.0f, card.getY() + 7.0f), { card.getRight() - 7.0f, card.getY() + 7.0f },
                        { card.getX() + 7.0f, card.getBottom() - 7.0f }, { card.getRight() - 7.0f, card.getBottom() - 7.0f } })
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillEllipse (p.x - 2.6f, p.y - 2.3f, 5.2f, 5.2f);
            juce::ColourGradient head (body.brighter (0.5f), p.x - 1.5f, p.y - 2.0f, body.darker (0.35f), p.x + 2.0f, p.y + 2.5f, false);
            g.setGradientFill (head);
            g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawLine (p.x - 1.6f, p.y + 1.0f, p.x + 1.6f, p.y - 1.0f, 0.8f);
        }
    }
    else
    {
        g.setColour (body);
        g.fillRoundedRectangle (card, 8.0f);
    }

    const bool selected = canvas.isSelected (instanceId);
    g.setColour (selected ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (card, 8.0f, selected ? 2.5f : 1.2f);

    // divider under the header, separating socket/title band from knob rows
    // (Skeuomorphic: an engraved line, dark over light)
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.drawLine (bounds.getX() + 6.0f, divY, bounds.getRight() - 6.0f, divY, 1.0f);
    if (skeuo)
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawLine (bounds.getX() + 6.0f, divY + 1.0f, bounds.getRight() - 6.0f, divY + 1.0f, 1.0f);
    }

    // collapse tab: a flat bar when open, a raised "grabber" split in two when
    // collapsed, so the state reads at a glance from across a zoomed-out patch
    {
        const auto tab = collapseTabArea().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        if (isCollapsed())
        {
            g.fillRoundedRectangle (tab.withWidth (tab.getWidth() * 0.45f), 2.5f);
            g.fillRoundedRectangle (tab.withWidth (tab.getWidth() * 0.45f)
                                       .withX (tab.getRight() - tab.getWidth() * 0.45f), 2.5f);
        }
        else
        {
            g.fillRoundedRectangle (tab, 2.5f);
        }
    }

    // the card's own Random button: "Random", and once pressed "Revert"
    if (showsRandomize())
    {
        const bool canUndo = processor.moduleRandomUndo.count (instanceId) > 0;
        const auto r = randomizeHitArea().toFloat().reduced (0.0f, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (canUndo ? 0.45f : 0.18f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (canUndo ? juce::Colours::white : text.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (canUndo ? "Revert" : "Random", r, juce::Justification::centred);
    }

    // module title, centred in the header column between the socket columns
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    // longer names ("Chord Wavetable", "3-Band Compressor") wrap onto a
    // second line rather than being cut short between the socket columns
    g.drawFittedText (desc.displayName + inst->dsp->titleSuffix(),
                      layout::socketColW, layout::topRow,
                      getWidth() - 2 * layout::socketColW, headerHeight,
                      juce::Justification::centred, 2, 0.9f);

    if (showsConvertPill())
    {
        const auto r = convertPillArea().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("To Patch", convertPillArea(), juce::Justification::centred);
    }

    // sockets: circular = Audio, square = Modulation, round = MIDI
    for (const auto& s : sockets)
    {
        const auto c = s.centre.toFloat();
        const float r = layout::socketSize * 0.5f;

        juce::Path shape;
        if (s.kind == SocketKind::Audio)
            shape.addEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        else if (s.kind == SocketKind::Midi)
            shape.addRoundedRectangle(c.x - r, c.y - r, 2 * r, 2 * r, r * 0.5f);
        else
            shape.addRectangle (c.x - r, c.y - r, 2 * r, 2 * r);

        if (skeuo)
        {
            // a jack of glass: a translucent collar lit from above, a white rim,
            // a dark translucent hole (MIDI's collar tinted cyan)
            juce::Path collar;
            const float rc = r + 2.5f;
            if (s.kind == SocketKind::Audio)
                collar.addEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc);
            else if (s.kind == SocketKind::Midi)
                collar.addRoundedRectangle (c.x - rc, c.y - rc, 2 * rc, 2 * rc, rc * 0.5f);
            else
                collar.addRoundedRectangle (c.x - rc, c.y - rc, 2 * rc, 2 * rc, 2.5f);
            const auto tint = s.kind == SocketKind::Midi ? skeuo::glow : juce::Colours::white;
            juce::ColourGradient glass (tint.withAlpha (0.42f), c.x, c.y - rc, juce::Colours::black.withAlpha (0.28f), c.x, c.y + rc, false);
            g.setGradientFill (glass);
            g.fillPath (collar);
            g.setColour (tint.withAlpha (0.55f));
            g.strokePath (collar, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colours::black.withAlpha (0.62f));
            g.fillPath (shape, juce::AffineTransform::scale (0.72f, 0.72f, c.x, c.y));
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.strokePath (shape, juce::PathStrokeType (0.8f), juce::AffineTransform::scale (0.72f, 0.72f, c.x, c.y));
        }
        else
        {
            g.setColour (juce::Colours::black);   // every socket black inside; MIDI keeps its cyan ring
            g.fillPath (shape);
            g.setColour (s.kind == SocketKind::Midi ? juce::Colour (0xff00ffff) : juce::Colours::white);
            g.strokePath (shape, juce::PathStrokeType (1.3f));
        }

        const SocketSpec* spec = nullptr;
        for (const auto& sk : desc.sockets)
            if (sk.id == s.socketId && (sk.direction == SocketDirection::Input) == s.isInput)
                spec = &sk;

        if (spec != nullptr)
        {
            g.setColour (text);
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            if (s.isInput)   // input labels sit to the right of the left column
                g.drawText (spec->label, s.centre.x + (int) r + 3, s.centre.y - 7,
                            layout::socketColW - (int) r - 4, 14, juce::Justification::centredLeft);
            else             // output labels sit to the left of the right column
                g.drawText (spec->label, s.centre.x - (int) r - 3 - (layout::socketColW - (int) r - 4),
                            s.centre.y - 7, layout::socketColW - (int) r - 4, 14,
                            juce::Justification::centredRight);
        }
    }

    // section separators: a line with the section's title on it
    if (! isCollapsed())
        for (const auto& [sy, title] : sectionHeaders)
        {
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 12;
            const float ly = (float) sy + 10.0f;
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.drawLine (bounds.getX() + 8.0f, ly, bounds.getX() + 14.0f, ly, 1.0f);
            g.drawLine (bounds.getX() + 14.0f + (float) tw, ly, bounds.getRight() - 8.0f, ly, 1.0f);
            if (skeuo)
            {
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawLine (bounds.getX() + 8.0f, ly + 1.0f, bounds.getX() + 14.0f, ly + 1.0f, 1.0f);
                g.drawLine (bounds.getX() + 14.0f + (float) tw, ly + 1.0f, bounds.getRight() - 8.0f, ly + 1.0f, 1.0f);
            }
            g.setColour (text.withAlpha (0.8f));
            g.drawText (title, (int) bounds.getX() + 14, sy + 2, tw, 16, juce::Justification::centred);
        }

    // knob labels
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    for (const auto& c : controls)
        if (c.visible && ! c.labelArea.isEmpty())
            g.drawText (c.spec->label, c.labelArea, juce::Justification::centred);
}


const aquanode::ParamSpec* ModuleComponent::findModulatableKnobNear (juce::Point<int> localPos) const
{
    for (const auto& entry : controls)
    {
        if (! entry.visible || (entry.spec->type != aquanode::ParamType::Rotary
                                && entry.spec->type != aquanode::ParamType::RotarySteppedList
                                && entry.spec->type != aquanode::ParamType::HBar)
            || ! entry.spec->modulatable || entry.component == nullptr)
            continue;
        if (entry.component->getBounds().expanded (2).contains (localPos))
            return entry.spec;
    }

    // custom drop targets inside the extra-content component (e.g. Pitch Lock
    // Filter keys): each maps to a hidden parameter flagged hiddenCableTarget
    if (extraContent != nullptr && extraContent->isVisible()
        && extraContent->getBounds().contains (localPos))
    {
        if (auto* targets = dynamic_cast<const CustomParamCableTargets*> (extraContent.get()))
        {
            const auto id = targets->paramTargetAt (localPos - extraContent->getPosition());
            if (id.isNotEmpty())
                if (auto* inst = processor.getInstance (instanceId))
                    if (const auto* spec = inst->descriptor().findParam (id))
                        if ((spec->type == aquanode::ParamType::Rotary
                             || spec->type == aquanode::ParamType::HBar)
                            && spec->modulatable && spec->hiddenCableTarget)
                            return spec;
        }
    }

    return nullptr;
}

juce::Point<int> ModuleComponent::knobCentreInParent (const juce::String& paramId) const
{
    for (const auto& entry : controls)
        if (entry.spec->id == paramId && entry.component != nullptr)
            return getPosition() + entry.component->getBounds().getCentre();

    // hidden cable-target params attach at the point the custom UI reports
    // (a Pitch Lock Filter cable snaps to the centre of its key)
    if (extraContent != nullptr)
        if (auto* targets = dynamic_cast<const CustomParamCableTargets*> (extraContent.get()))
        {
            const auto centre = targets->paramTargetCentre (paramId);
            if (centre.x >= 0)
                return getPosition() + extraContent->getPosition() + centre;
        }

    return getPosition() + juce::Point<int> (moduleWidth() / 2, headerHeight / 2);
}

void ModuleComponent::showKnobModMenu (const juce::String& paramId)
{
    const auto& pcs = processor.getParamCables();
    juce::PopupMenu menu;
    bool any = false;

    // the menu is async: the patch may be reloaded (and this component
    // destroyed) before a choice is made, so callbacks go through a
    // SafePointer and re-find the cable by identity instead of by index
    juce::Component::SafePointer<ModuleComponent> safeThis (this);

    for (int i = 0; i < (int) pcs.size(); ++i)
    {
        if (pcs[(size_t) i].toModule != instanceId || pcs[(size_t) i].paramId != paramId)
            continue;
        any = true;

        juce::String srcName = "module " + juce::String (pcs[(size_t) i].fromModule);
        if (auto* srcInst = processor.getInstance (pcs[(size_t) i].fromModule))
            srcName = srcInst->descriptor().displayName + " / " + pcs[(size_t) i].fromSocket;

        // identify the cable by its contents, not its index: indices shift
        // whenever any other cable is added or removed
        const auto key = pcs[(size_t) i];
        auto findIndex = [safeThis, key]() -> int
        {
            if (safeThis == nullptr)
                return -1;
            const auto& live = safeThis->processor.getParamCables();
            for (int k = 0; k < (int) live.size(); ++k)
                if (live[(size_t) k].fromModule == key.fromModule
                    && live[(size_t) k].fromSocket == key.fromSocket
                    && live[(size_t) k].toModule == key.toModule
                    && live[(size_t) k].paramId == key.paramId)
                    return k;
            return -1;
        };

        juce::PopupMenu sub;
        static const float depths[] = { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f };
        for (int d = 0; d < 8; ++d)
        {
            const float depth = depths[d];
            const bool current = std::abs (key.depth - depth) < 0.011f;
            sub.addItem (juce::String ((int) (depth * 100.0f)) + " %", true, current,
                         [safeThis, findIndex, depth]
                         {
                             const int k = findIndex();
                             if (safeThis != nullptr && k >= 0)
                                 safeThis->processor.setParamCableDepth (k, depth);
                         });
        }
        sub.addSeparator();
        const auto* srcInst = processor.getInstance (key.fromModule);
        const bool absoluteSource = srcInst != nullptr && srcInst->dsp != nullptr && srcInst->dsp->drivesKnobsAbsolutely();
        if (! absoluteSource)
        sub.addItem ("Bipolar", true, key.bipolar, [safeThis, findIndex, key]
        {
            const int k = findIndex();
            if (safeThis != nullptr && k >= 0)
                safeThis->processor.setParamCableBipolar (k, ! key.bipolar);
        });
        sub.addItem ("Remove", [safeThis, findIndex]
        {
            const int k = findIndex();
            if (safeThis != nullptr && k >= 0)
            {
                safeThis->processor.removeParamCable (k);
                safeThis->canvas.repaint();
            }
        });

        menu.addSubMenu (srcName + "  (" + juce::String ((int) (key.depth * 100.0f)) + " %)", sub);
    }

    if (any)
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

bool ModuleComponent::knobHasModulation (const juce::String& paramId) const
{
    for (const auto& pc : processor.getParamCables())
        if (pc.toModule == instanceId && pc.paramId == paramId)
            return true;
    return false;
}

void ModuleComponent::paintOverChildren (juce::Graphics& g)
{
    if (isCollapsed())
        return;   // the knobs the rings belong to are hidden

    // every modulated knob: the stretch its cables move it over, as a filled
    // section of a disc around the knob (from where to where it can go), and
    // its outline; a bar (Mix-style) gets the stretch drawn along it
    const auto& pcs = processor.getParamCables();
    auto* inst = processor.getInstance (instanceId);
    const auto modColour = juce::Colour (0xffd970b0);
    for (const auto& entry : controls)
    {
        const bool rotary = entry.spec->type == aquanode::ParamType::Rotary
                         || entry.spec->type == aquanode::ParamType::RotarySteppedList;
        if (! entry.visible || entry.component == nullptr || (! rotary && entry.spec->type != aquanode::ParamType::HBar))
            continue;

        // the cables' reach, in the knob's own units, summed (each cable adds its own)
        const auto& spec = *entry.spec;
        const float span = spec.maxValue - spec.minValue;
        const float base = inst != nullptr ? inst->dsp->getParameter (spec.id) : spec.defaultValue;
        float down = 0.0f, up = 0.0f;
        bool modulated = false;
        for (const auto& pc : pcs)
        {
            if (pc.toModule != instanceId || pc.paramId != spec.id)
                continue;
            modulated = true;
            const auto* src = processor.getInstance (pc.fromModule);
            if (src != nullptr && src->dsp != nullptr && src->dsp->drivesKnobsAbsolutely())
            {
                // a curve that sets the knob: anywhere between its ends, blended by the depth
                down += std::abs (pc.depth) * (spec.minValue - base);
                up += std::abs (pc.depth) * (spec.maxValue - base);
            }
            else if (pc.bipolar)
            {
                down -= std::abs (pc.depth) * span;
                up += std::abs (pc.depth) * span;
            }
            else
            {
                (pc.depth >= 0.0f ? up : down) += pc.depth * span;
            }
        }
        if (! modulated)
            continue;

        auto* slider = dynamic_cast<juce::Slider*> (entry.component.get());
        const float lo = juce::jlimit (spec.minValue, spec.maxValue, base + down);
        const float hi = juce::jlimit (spec.minValue, spec.maxValue, base + up);
        const auto b = entry.component->getBounds().toFloat();

        if (rotary && slider != nullptr)
        {
            // where the look-and-feel draws the knob inside the slider (the
            // Skeuomorphic one a little bigger)
            const bool skeuo = themeColours().skeuomorphic;
            const auto layout = slider->getLookAndFeel().getSliderLayout (*slider);
            auto knob = layout.sliderBounds.toFloat().reduced (skeuo ? SkeuoLookAndFeel::kKnobInset : 10.0f).translated (b.getX(), b.getY());
            const float radius = juce::jmin (knob.getWidth(), knob.getHeight()) * 0.5f;
            const auto c = knob.getCentre();
            const auto rp = slider->getRotaryParameters();
            auto angleOf = [&] (float v)
            {
                return rp.startAngleRadians + (float) slider->valueToProportionOfLength (v) * (rp.endAngleRadians - rp.startAngleRadians);
            };
            const float a0 = angleOf (lo), a1 = angleOf (hi);
            const float rOut = radius + (skeuo ? 4.0f : 5.5f);
            juce::Path wedge;
            wedge.addPieSegment (c.x - rOut, c.y - rOut, rOut * 2.0f, rOut * 2.0f, a0, juce::jmax (a1, a0 + 0.02f),
                                 radius * 0.55f / rOut);   // a ring section: the knob's middle stays clear
            g.setColour (modColour.withAlpha (0.38f));
            g.fillPath (wedge);
            g.setColour (modColour.withAlpha (0.9f));
            g.strokePath (wedge, juce::PathStrokeType (1.2f));
        }
        else if (slider != nullptr)
        {
            // a bar: the stretch along its track
            const auto layout = slider->getLookAndFeel().getSliderLayout (*slider);
            const auto track = layout.sliderBounds.toFloat().translated (b.getX(), b.getY());
            const float x0 = track.getX() + (float) slider->valueToProportionOfLength (lo) * track.getWidth();
            const float x1 = track.getX() + (float) slider->valueToProportionOfLength (hi) * track.getWidth();
            const auto band = juce::Rectangle<float> (x0, track.getCentreY() - 7.0f, juce::jmax (2.0f, x1 - x0), 14.0f);
            g.setColour (modColour.withAlpha (0.38f));
            g.fillRoundedRectangle (band, 3.0f);
            g.setColour (modColour.withAlpha (0.9f));
            g.drawRoundedRectangle (band, 3.0f, 1.2f);
        }
    }
}

bool ModuleComponent::handleRapidClick (const juce::MouseEvent& e)
{
    const juce::int64 now = e.eventTime.toMilliseconds();

    // the same physical click can arrive twice (own handler + child mouse
    // listener) - only count each event once
    if (now == lastClickTimeMs && e.getScreenPosition() == lastClickScreenPos)
        return false;

    const bool continues = rapidClickCount > 0
        && now - lastClickTimeMs <= 500
        && e.getScreenPosition().getDistanceFrom (lastClickScreenPos) <= 4;

    rapidClickCount = continues ? rapidClickCount + 1 : 1;
    lastClickTimeMs = now;
    lastClickScreenPos = e.getScreenPosition();

    if (rapidClickCount < 3)
        return false;

    rapidClickCount = 0;

    // delete every cable this module routes back into itself (self-modulation
    // loops); cables to or from OTHER modules are deliberately left alone
    const auto& cables = processor.getCables();
    std::vector<int> selfCables;
    for (int i = 0; i < (int) cables.size(); ++i)
        if (cables[(size_t) i].fromModule == instanceId
            && cables[(size_t) i].toModule == instanceId)
            selfCables.push_back (i);

    if (selfCables.empty())
        return false;

    for (auto it = selfCables.rbegin(); it != selfCables.rend(); ++it)
        processor.removeCable (*it);

    canvas.repaint();
    return true;
}

juce::Rectangle<int> ModuleComponent::titleArea() const
{
    return { layout::socketColW, layout::topRow, getWidth() - 2 * layout::socketColW, headerHeight };
}

namespace
{
    // the colour of one card: a selector and a way back to the module type's colour
    class CardColourPanel : public juce::Component,
                            private juce::ChangeListener
    {
    public:
        // room: the window it opens in; the panel always fits inside it
        CardColourPanel (juce::Colour current, juce::Colour typeColour, juce::Rectangle<int> room,
                         std::function<void (juce::Colour)> onChange)
            : changed (std::move (onChange)), standard (typeColour)
        {
            selector.setCurrentColour (current, juce::dontSendNotification);
            selector.setColour (juce::ColourSelector::backgroundColourId, juce::Colours::transparentBlack);
            selector.addChangeListener (this);
            addAndMakeVisible (selector);
            resetButton.onClick = [this]
            {
                selector.setCurrentColour (standard, juce::dontSendNotification);
                if (changed) changed (juce::Colours::transparentBlack);   // back to the type's colour
            };
            addAndMakeVisible (resetButton);
            doneButton.onClick = [this]
            {
                if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                    box->dismiss();
            };
            addAndMakeVisible (doneButton);
           #if JUCE_ANDROID
            const int w = 340, h = 360;     // no sliders here: the colour field and the hex code
           #else
            const int w = 280, h = 330;
           #endif
            // the call-out's arrow and border take about 40 px around it
            setSize (juce::jlimit (160, w, (int) (room.getWidth() * 0.6f) - 40),
                     juce::jlimit (150, h, (int) (room.getHeight() * 0.86f) - 40));
        }
        ~CardColourPanel() override { selector.removeChangeListener (this); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
           #if JUCE_ANDROID
            auto buttons = r.removeFromBottom (juce::jmin (40, getHeight() / 7));
           #else
            auto buttons = r.removeFromBottom (26);
           #endif
            // "Module Colour" (back to the type's colour) and Done side by side
            doneButton.setBounds (buttons.removeFromRight (juce::jmax (64, buttons.getWidth() / 3)));
            buttons.removeFromRight (6);
            resetButton.setBounds (buttons);
            r.removeFromBottom (6);
            selector.setBounds (r);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (changed) changed (selector.getCurrentColour().withAlpha (1.0f));
        }

        std::function<void (juce::Colour)> changed;
        juce::Colour standard;
       #if JUCE_ANDROID
        juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showColourspace };
       #else
        juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                                        | juce::ColourSelector::showColourspace };
       #endif
        juce::TextButton resetButton { "Module Colour" }, doneButton { "Done" };
    };
}

void ModuleComponent::showColourPicker()
{
    auto* inst = processor.getInstance (instanceId);
    if (inst == nullptr)
        return;
    juce::Component::SafePointer<ModuleComponent> safeThis (this);
    const int id = instanceId;
    auto& proc = processor;
    auto* top = getTopLevelComponent();
    auto panel = std::make_unique<CardColourPanel> (inst->cardColour(), inst->descriptor().colour(), top->getLocalBounds(),
        [safeThis, id, &proc] (juce::Colour c)
        {
            if (auto* i = proc.getInstance (id))
                i->customColour = c;
            if (safeThis != nullptr)
            {
                safeThis->repaint();
                safeThis->canvas.repaint();   // its cables wear its colour too
            }
        });
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (this, titleArea()), top);
}

void ModuleComponent::mouseDown (const juce::MouseEvent& e)
{
    canvas.moduleClicked (instanceId);   // (keeps a Region Select box that holds this card)

    // The collapse tab is checked first, before the rapid-click and drag
    // gestures: tapping it repeatedly must open and close the module rather
    // than read as a triple-click that deletes its self-modulation cables.
    if (e.eventComponent == this && ! e.mods.isPopupMenu()
        && collapseHitArea().contains (e.getPosition()))
    {
        rapidClickCount = 0;
        toggleCollapsed();
        return;
    }

    // the card's own Random / Revert button (one after the other)
    if (e.eventComponent == this && ! e.mods.isPopupMenu() && showsRandomize())
    {
        if (randomizeHitArea().contains (e.getPosition()))
        {
            rapidClickCount = 0;
            if (processor.moduleRandomUndo.count (instanceId) > 0)
                undoCard();
            else
                randomizeCard();
            return;
        }
    }

    // the title: a right click, or holding it still (finger or mouse), opens the
    // card's own colour
    ++titlePressId;
    if (e.eventComponent == this && titleArea().contains (e.getPosition())
        && ! (showsConvertPill() && convertPillArea().contains (e.getPosition())))
    {
        if (e.mods.isPopupMenu())
        {
            showColourPicker();
            return;
        }
        juce::Component::SafePointer<ModuleComponent> safeThis (this);
        const int press = titlePressId;
        juce::Timer::callAfterDelay (550, [safeThis, press]
        {
            if (safeThis == nullptr || safeThis->titlePressId != press)
                return;   // released, dragged or pressed again meanwhile
            safeThis->draggingBody = false;
            safeThis->showColourPicker();
        });
    }

    // a plugin's GUI card: the To Patch pill swaps it for its patch version
    if (e.eventComponent == this && ! e.mods.isPopupMenu() && showsConvertPill()
        && convertPillArea().contains (e.getPosition()))
    {
        canvas.convertPluginCard (instanceId);
        return;
    }

    // three rapid clicks (same spot, +-4 px) anywhere in the module delete
    // its self-modulation cables - the one cable type the canvas can't select
    if (! e.mods.isPopupMenu() && handleRapidClick (e))
        return;

    // right-click on a modulated knob (event may arrive via the knob's own
    // mouse listener, so translate into our coordinate space first)
    const auto knobPos = e.eventComponent == this
        ? e.getPosition()
        : e.getEventRelativeTo (this).getPosition();

    if (e.mods.isPopupMenu())
    {
        if (const auto* spec = findModulatableKnobNear (knobPos))
        {
            showKnobModMenu (spec->id);
            return;
        }
    }

    // Touch devices have no right-click, so a long press stands in for it.
    // This is deliberately TOUCH ONLY: on desktop, resting on a knob before a
    // slow fine-tune is a completely normal gesture and must never pop a
    // menu. It also only arms on knobs that actually have a modulation cable,
    // since the menu would be empty otherwise.
    longPressParamId.clear();
    longPressMoved = false;

    if (e.source.isTouch())
    {
        if (const auto* spec = findModulatableKnobNear (knobPos))
        {
            if (knobHasModulation (spec->id))
            {
                longPressParamId = spec->id;

                juce::Component::SafePointer<ModuleComponent> safeThis (this);
                const auto id = spec->id;
                juce::Timer::callAfterDelay (500, [safeThis, id]
                {
                    // still the same un-moved press? (a drag cancels it)
                    if (safeThis != nullptr && ! safeThis->longPressMoved
                        && safeThis->longPressParamId == id)
                        safeThis->showKnobModMenu (id);
                });
            }
        }
    }

    if (e.eventComponent != this)
        return;   // child listener event: knobs handle their own left-clicks

    if (const auto* socket = findSocketNear (e.getPosition()))
    {
        draggingCable = true;
        canvas.beginCableDrag (instanceId, socket->socketId, socket->isInput, socket->kind,
                               getPosition() + socket->centre);
        return;
    }

    draggingBody = true;
    dragger.startDraggingComponent (this, e);
}

void ModuleComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 6)
        ++titlePressId;   // a drag, not a hold
    if (longPressParamId.isNotEmpty() && e.getDistanceFromDragStart() > 8)
        longPressMoved = true;   // this is a knob drag, not a long press

    if (draggingCable)
    {
        canvas.updateCableDrag (e.getEventRelativeTo (&canvas).getPosition());
        return;
    }

    if (draggingBody)
    {
        const auto before = getPosition();
        dragger.dragComponent (this, e, nullptr);
        // getPosition() is already the world position: the view transform is
        // applied on top of the bounds, not baked into them.
        processor.setModulePosition (instanceId, getPosition());
        canvas.moveSelectionWith (instanceId, getPosition() - before);   // a box's other modules come along
        canvas.moduleDragged (instanceId, e.getEventRelativeTo (&canvas).getPosition());
    }
}

void ModuleComponent::mouseUp (const juce::MouseEvent& e)
{
    longPressParamId.clear();   // press finished: nothing left to fire
    ++titlePressId;             // and no colour picker either

    if (draggingCable)
    {
        canvas.endCableDrag (e.getEventRelativeTo (&canvas).getPosition());
        draggingCable = false;
    }
    if (draggingBody)
    {
        draggingBody = false;
        canvas.moduleDropped (instanceId);   // may rebuild the canvas: nothing after this
        return;
    }
}
