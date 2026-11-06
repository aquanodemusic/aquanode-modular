// The patch area: the cards, the cables, zoom and pinch, dragging and dropping,
// Region Select.
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
// PatchCanvas
//==============================================================================
PatchCanvas::PatchCanvas (AquanodeModularAudioProcessor& proc)
    : processor (proc)
{
    setWantsKeyboardFocus (true);
}

// Screen-space spacing of the "+" grid at the current zoom. The world spacing
// is doubled as often as needed to keep the on-screen spacing near its usual
// value: without that floor, zooming out to 30 % would quadruple the number of
// crosses drawn on every pan frame, which is exactly when a phone can least
// afford it. Visually the grid coarsens as you pull back, which is what a
// map is expected to do anyway.
float PatchCanvas::gridScreenSpacing() const
{
    float s = (float) layout::gridSpacing * zoom;
    while (s < (float) layout::gridSpacing * 0.75f)
        s *= 2.0f;
    return s;
}

void PatchCanvas::rebuildGrid()
{
    // decorative "+" grid. Extend one cell beyond every edge so it can be
    // translated by the pan offset (mod spacing) and still cover the viewport.
    const float s = gridScreenSpacing();
    const float arm = juce::jlimit (2.0f, 3.0f, 3.0f * zoom);

    gridPath.clear();
    for (float x = 0.0f; x <= (float) getWidth() + s; x += s)
    {
        for (float y = 0.0f; y <= (float) getHeight() + s; y += s)
        {
            gridPath.startNewSubPath (x - arm, y);
            gridPath.lineTo (x + arm, y);
            gridPath.startNewSubPath (x, y - arm);
            gridPath.lineTo (x, y + arm);
        }
    }
}

void PatchCanvas::resized()
{
    rebuildGrid();
}

void PatchCanvas::convertPluginCard (int instanceId)
{
    juce::Component::SafePointer<PatchCanvas> safe (this);
    juce::MessageManager::callAsync ([safe, instanceId]
    {
        if (safe == nullptr)
            return;
        const auto name = aquanode::plugins::convertGuiToPatch (safe->processor, instanceId);
        if (name.isNotEmpty())
            safe->processor.patchName = name;
        safe->rebuild();
    });
}

void PatchCanvas::rebuild()
{
    moduleComponents.clear();
    selectedCableIndex = -1;

    bool selectedStillExists = false;
    for (const int id : processor.getModuleIds())
    {
        auto comp = std::make_unique<ModuleComponent> (processor, *this, id);
        addAndMakeVisible (comp.get());
        moduleComponents.push_back (std::move (comp));
        if (id == selectedModuleId)
            selectedStillExists = true;
    }

    if (! selectedStillExists)
        selectedModuleId = -1;
    regionSelection.erase (std::remove_if (regionSelection.begin(), regionSelection.end(),
                                           [this] (int id) { return processor.getInstance (id) == nullptr; }),
                           regionSelection.end());
    if (selectedModuleId < 0 && ! regionSelection.empty())
        selectedModuleId = regionSelection.front();

    applyViewToChildren();   // new components need the current pan/zoom
    repaint();
}

void PatchCanvas::refreshAllModuleValues()
{
    for (auto& m : moduleComponents)
        m->refreshFromModel();
    repaint();
}

ModuleComponent* PatchCanvas::findModuleComponent (int instanceId) const
{
    for (const auto& m : moduleComponents)
        if (m->getInstanceId() == instanceId)
            return m.get();
    return nullptr;
}

void PatchCanvas::selectModule (int instanceId)
{
    selectedModuleId = instanceId;
    selectedCableIndex = -1;
    regionSelection.clear();
    repaint();
}

void PatchCanvas::setSelection (const std::vector<int>& ids)
{
    regionSelection = ids;
    selectedModuleId = ids.empty() ? -1 : ids.front();
    selectedCableIndex = -1;
    selectedParamCableIndex = -1;
    repaint();
}

std::vector<int> PatchCanvas::getSelection() const
{
    if (! regionSelection.empty())
        return regionSelection;
    if (selectedModuleId >= 0)
        return { selectedModuleId };
    return {};
}

bool PatchCanvas::isSelected (int instanceId) const
{
    if (! regionSelection.empty())
        return std::find (regionSelection.begin(), regionSelection.end(), instanceId) != regionSelection.end();
    return selectedModuleId == instanceId;
}

void PatchCanvas::moduleClicked (int instanceId)
{
    // a press on one of the box's modules keeps the box (to move them all);
    // any other card is selected alone
    if (regionSelect && isSelected (instanceId) && ! regionSelection.empty())
    {
        selectedModuleId = instanceId;
        selectedCableIndex = -1;
        repaint();
        return;
    }
    selectModule (instanceId);
}

void PatchCanvas::moveSelectionWith (int movedId, juce::Point<int> delta)
{
    if (delta.isOrigin() || ! isSelected (movedId))
        return;
    for (const int id : regionSelection)
    {
        if (id == movedId)
            continue;
        if (auto* inst = processor.getInstance (id))
        {
            processor.setModulePosition (id, inst->position + delta);
            if (auto* m = findModuleComponent (id))
                m->setTopLeftPosition (inst->position);   // world coordinates (the view transform stays)
        }
    }
    repaint();
}

void PatchCanvas::setRegionSelect (bool on)
{
    regionSelect = on;
    regionDragging = false;
    drop = {};
    if (! on && regionSelection.size() > 1)
        selectModule (selectedModuleId);   // back to one selected module
    repaint();
}

void PatchCanvas::updateRegionSelection()
{
    const auto a = screenToWorld (regionStart), b = screenToWorld (regionEnd);
    const auto box = juce::Rectangle<float> (a, b);
    std::vector<int> ids;
    for (const auto& m : moduleComponents)
        if (m->getBounds().toFloat().intersects (box))
            ids.push_back (m->getInstanceId());
    regionSelection = ids;
    selectedModuleId = ids.empty() ? -1 : ids.front();
}

void PatchCanvas::moduleMoved()
{
    repaint();
}

namespace
{
    // a module's socket of that direction and kind: one of the preferred ids
    // first, else the first that is there
    const aquanode::SocketSpec* pickSocket (const aquanode::ModuleDescriptor& d, bool input, aquanode::SocketKind kind,
                                            std::initializer_list<const char*> preferred)
    {
        const auto list = input ? d.inputs() : d.outputs();
        for (const char* id : preferred)
            for (const auto* sock : list)
                if (sock->id == id && sock->kind == kind && ! sock->hidden)
                    return sock;
        for (const auto* sock : list)
            if (sock->kind == kind && ! sock->hidden)
                return sock;
        return nullptr;
    }
}

PatchCanvas::DropPlan PatchCanvas::planDrop (int moduleId, juce::Point<float> world) const
{
    using aquanode::SocketKind;
    DropPlan plan;
    plan.moduleId = moduleId;
    const auto* self = processor.getInstance (moduleId);
    if (self == nullptr)
        return plan;
    const auto& md = self->descriptor();
    const auto& type = md.typeId;
    const auto& cables = processor.getCables();

    auto inputFree = [&cables] (int module, const juce::String& socket)
    {
        return std::none_of (cables.begin(), cables.end(), [&] (const CableInfo& c) { return c.toModule == module && c.toSocket == socket; });
    };
    // a module's audio input: the first one free (Mixer, Crossfade, Ring Mod...), else the first
    auto audioInput = [&] (const aquanode::ModuleDescriptor& d, int module) -> const aquanode::SocketSpec*
    {
        const aquanode::SocketSpec* first = nullptr;
        for (const auto* sock : d.inputs())
            if (sock->kind == SocketKind::Audio && ! sock->hidden)
            {
                if (first == nullptr) first = sock;
                if (inputFree (module, sock->id)) return sock;
            }
        return first;
    };
    auto hasInput = [] (const aquanode::ModuleDescriptor& d, const char* id)
    {
        for (const auto* sock : d.inputs()) if (sock->id == id && ! sock->hidden) return true;
        return false;
    };
    auto isOneOf = [&type] (std::initializer_list<const char*> types)
    {
        for (const char* t : types) if (type == t) return true;
        return false;
    };
    // the first of these inputs the target has (a filter's Mod In only on a filter)
    auto firstInput = [&] (const aquanode::ModuleDescriptor& d, std::initializer_list<const char*> ids) -> juce::String
    {
        for (const char* id : ids)
        {
            if (! hasInput (d, id)) continue;
            if (juce::String (id) == "modIn" && d.section != aquanode::ModuleSection::Filter) continue;
            return id;
        }
        return {};
    };
    const auto* modOutput = pickSocket (md, false, SocketKind::Modulation, { "modOut", "envOut", "lfoOut", "triOut", "cvOut", "macroOut", "wheelOut", "gateOut" });

    // ---- the module under the pointer (not the one dragged)
    const ModuleComponent* under = nullptr;
    for (auto it = moduleComponents.rbegin(); it != moduleComponents.rend(); ++it)
        if ((*it)->getInstanceId() != moduleId && (*it)->getBounds().toFloat().contains (world))
        {
            under = it->get();
            break;
        }

    if (under != nullptr)
    {
        const int t = under->getInstanceId();
        const auto* target = processor.getInstance (t);
        if (target == nullptr)
            return plan;
        const auto& td = target->descriptor();
        auto add = [&plan, moduleId, t] (bool fromSelf, const juce::String& out, const juce::String& in)
        {
            plan.adds.push_back (fromSelf ? CableInfo { moduleId, out, t, in } : CableInfo { t, out, moduleId, in });
        };

        // a modulation source right on a knob: a knob cable
        if (modOutput != nullptr)
            if (const auto* knob = under->findModulatableKnobNear ((world - under->getPosition().toFloat()).roundToInt()))
            {
                ParamCableInfo pc;
                pc.fromModule = moduleId;
                pc.fromSocket = modOutput->id;
                pc.toModule = t;
                pc.paramId = knob->id;
                pc.depth = self->dsp != nullptr && self->dsp->drivesKnobsAbsolutely() ? 1.0f : 0.25f;
                pc.bipolar = ! modOutput->unipolar;
                const auto& pcs = processor.getParamCables();
                const bool there = std::any_of (pcs.begin(), pcs.end(), [&pc] (const ParamCableInfo& x)
                    { return x.fromModule == pc.fromModule && x.fromSocket == pc.fromSocket && x.toModule == pc.toModule && x.paramId == pc.paramId; });
                if (! there)
                {
                    plan.paramAdds.push_back (pc);
                    plan.knob = knob->id;
                }
            }

        if (plan.paramAdds.empty())
        {
            const auto* myMidiOut = pickSocket (md, false, SocketKind::Midi, { "midiOut" });
            const auto* myMidiIn = pickSocket (md, true, SocketKind::Midi, { "midiIn", "addMidiIn", "alwaysMidiIn" });
            const auto* theirMidiIn = pickSocket (td, true, SocketKind::Midi, { "addMidiIn", "midiIn" });
            const auto* theirMidiOut = pickSocket (td, false, SocketKind::Midi, { "midiOut" });
            const auto* myAudioOut = pickSocket (md, false, SocketKind::Audio, { "audioOut" });
            const auto* theirAudioOut = pickSocket (td, false, SocketKind::Audio, { "audioOut" });

            if (type == "io.audioOut")
            {
                if (theirAudioOut != nullptr) add (false, theirAudioOut->id, "audioIn");          // a module's output into the Audio Out
            }
            else if (td.typeId == "io.audioOut")
            {
                if (myAudioOut != nullptr) add (true, myAudioOut->id, "audioIn");                 // into the Audio Out
            }
            else if (myMidiOut != nullptr && theirMidiIn != nullptr)
                add (true, myMidiOut->id, theirMidiIn->id);                                        // notes into it
            else if (myMidiIn != nullptr && myMidiOut != nullptr && theirMidiOut != nullptr)
                add (false, theirMidiOut->id, myMidiIn->id);                                       // a MIDI processor fed by it
            else if (isOneOf ({ "util.adsr", "util.analogeg", "util.curveenv", "util.dxenv", "util.gate" }) && modOutput != nullptr)
            {
                const auto in = firstInput (td, { "envIn", "volMod", "egIn", "cutoffMod", "modIn", "pedalIn" });
                if (in.isNotEmpty()) add (true, modOutput->id, in);
            }
            else if (type == "util.dxlfo" && td.typeId == "osc.dxop")
            {
                add (true, "pitchOut", "pitchIn");
                add (true, "amOut", "amIn");
            }
            else if (type == "util.dxpitcheg")
            {
                if (hasInput (td, "pitchIn")) add (true, "pitchOut", "pitchIn");
            }
            else if (isOneOf ({ "util.lfo", "util.multilfo", "util.randomlfo", "util.skewlfo", "util.drawlfo", "util.dxlfo" }) && modOutput != nullptr)
            {
                const auto in = firstInput (td, { "mgIn", "cutoffMod", "modIn", "pedalIn", "posIn", "levelIn", "pitchIn" });
                if (in.isNotEmpty()) add (true, modOutput->id, in);
            }
            else if (type == "util.keytrack")
            {
                const auto in = firstInput (td, { "cutoffMod", "modIn", "pitchIn" });
                if (in.isNotEmpty() && td.section == aquanode::ModuleSection::Filter) add (true, "modOut", in);
            }
            else if (type == "util.velocity")
            {
                const auto in = firstInput (td, { "cutoffMod", "modIn", "levelIn" });
                if (in.isNotEmpty()) add (true, "modOut", in);
            }
            else if (type == "util.stepseq")
            {
                if (hasInput (td, "pitchIn") && td.section == aquanode::ModuleSection::Oscillator) add (true, "pitchOut", "pitchIn");
                if (hasInput (td, "trigIn")) add (true, "gateOut", "trigIn");
                else if (hasInput (td, "clockIn")) add (true, "gateOut", "clockIn");
            }
            else if (isOneOf ({ "util.clock", "util.euclid" }))
            {
                const auto in = firstInput (td, { "trigIn", "clockIn" });
                if (in.isNotEmpty()) add (true, "gateOut", in);
            }
            else if (type == "io.midicontrols")
            {
                if (hasInput (td, "pressIn")) add (true, "touchOut", "pressIn");
                if (hasInput (td, "wheelIn")) add (true, "wheelOut", "wheelIn");
                if (hasInput (td, "holdIn")) add (true, "sustainOut", "holdIn");
            }
            else if (const auto* myIn = audioInput (md, moduleId); myIn != nullptr && theirAudioOut != nullptr
                                                                   && (md.section != aquanode::ModuleSection::Oscillator))
                add (false, theirAudioOut->id, myIn->id);                                          // it plays into the module dropped
            else if (myAudioOut != nullptr)
                if (const auto* theirIn = audioInput (td, t))
                    add (true, myAudioOut->id, theirIn->id);                                       // a sound into a free input
        }

        if (! plan.adds.empty() || ! plan.paramAdds.empty())
        {
            plan.target = t;
            plan.lit = { t };
        }
    }
    else
    {
        // ---- onto a cable: an audio, MIDI or modulation cable, or a knob cable
        const int index = cableIndexNear (world, 14.0f);
        const int pIndex = index < 0 ? paramCableIndexNear (world, 12.0f) : -1;
        const bool modProcessor = isOneOf ({ "util.quantize", "util.slew", "util.dcoffset", "util.samplehold" });
        const auto* modIn = pickSocket (md, true, SocketKind::Modulation, { "modIn", "sigIn" });
        const auto* modOutProc = pickSocket (md, false, SocketKind::Modulation, { "modOut" });

        if (index >= 0)
        {
            const auto c = cables[(size_t) index];
            const auto* from = processor.getInstance (c.fromModule);
            if (from != nullptr && c.fromModule != moduleId && c.toModule != moduleId)
            {
                const int k = from->descriptor().outputIndexOf (c.fromSocket);
                const auto kind = k >= 0 ? from->descriptor().outputs()[(size_t) k]->kind : SocketKind::Modulation;
                const bool tap = isOneOf ({ "util.tuner", "util.envfollow", "util.spectrogram", "util.spectrumcompare", "util.record" });
                if (kind == SocketKind::Audio && tap)
                {
                    if (const auto* in = audioInput (md, moduleId))
                        plan.adds.push_back ({ c.fromModule, c.fromSocket, moduleId, in->id });   // listens, adds nothing to the cable's end
                }
                else if (kind == SocketKind::Audio || kind == SocketKind::Midi)
                {
                    const auto* in = kind == SocketKind::Audio ? audioInput (md, moduleId)
                                                               : pickSocket (md, true, kind, { "midiIn", "addMidiIn" });
                    const auto* out = pickSocket (md, false, kind, { "audioOut", "midiOut" });
                    if (in != nullptr && out != nullptr)
                    {
                        plan.adds.push_back ({ c.fromModule, c.fromSocket, moduleId, in->id });
                        plan.adds.push_back ({ moduleId, out->id, c.toModule, c.toSocket });
                    }
                }
                else if (modProcessor && modIn != nullptr && modOutProc != nullptr)
                {
                    plan.removeCable = index;                                                         // in series
                    plan.adds.push_back ({ c.fromModule, c.fromSocket, moduleId, modIn->id });
                    plan.adds.push_back ({ moduleId, modOutProc->id, c.toModule, c.toSocket });
                }
                if (! plan.adds.empty())
                {
                    plan.cable = index;
                    plan.lit = { c.fromModule, c.toModule };
                }
            }
        }
        else if (pIndex >= 0 && modProcessor && modIn != nullptr && modOutProc != nullptr)
        {
            const auto pc = processor.getParamCables()[(size_t) pIndex];
            if (pc.fromModule != moduleId && pc.toModule != moduleId)
            {
                plan.removeParamCable = pIndex;                                                       // in series
                plan.paramCable = pIndex;
                plan.adds.push_back ({ pc.fromModule, pc.fromSocket, moduleId, modIn->id });
                auto through = pc;
                through.fromModule = moduleId;
                through.fromSocket = modOutProc->id;
                plan.paramAdds.push_back (through);
                plan.lit = { pc.fromModule, pc.toModule };
            }
        }
    }

    // nothing that is there already
    plan.adds.erase (std::remove_if (plan.adds.begin(), plan.adds.end(), [&cables] (const CableInfo& a)
    {
        return std::any_of (cables.begin(), cables.end(), [&a] (const CableInfo& c)
        {
            return c.fromModule == a.fromModule && c.fromSocket == a.fromSocket && c.toModule == a.toModule && c.toSocket == a.toSocket;
        });
    }), plan.adds.end());
    if (plan.adds.empty() && plan.paramAdds.empty())
        plan = DropPlan { moduleId };
    return plan;
}

void PatchCanvas::moduleDragged (int moduleId, juce::Point<int> canvasPos)
{
    // Region Select: a dropped module is never patched into what it lands on
    drop = regionSelect ? DropPlan{} : planDrop (moduleId, screenToWorld (canvasPos.toFloat()));
    repaint();
}

void PatchCanvas::moduleDropped (int moduleId)
{
    const auto plan = drop;
    drop = {};
    if (plan.moduleId == moduleId)
    {
        // a modulation processor goes in series: the cable it replaces first
        if (plan.removeCable >= 0 && plan.removeCable < (int) processor.getCables().size())
            processor.removeCable (plan.removeCable);
        if (plan.removeParamCable >= 0 && plan.removeParamCable < (int) processor.getParamCables().size())
            processor.removeParamCable (plan.removeParamCable);
        for (const auto& c : plan.adds)
            processor.addCable (c.fromModule, c.fromSocket, c.toModule, c.toSocket);
        for (const auto& pc : plan.paramAdds)
        {
            processor.addParamCable (pc.fromModule, pc.fromSocket, pc.toModule, pc.paramId, pc.depth);
            const auto& pcs = processor.getParamCables();
            for (int k = 0; k < (int) pcs.size(); ++k)
                if (pcs[(size_t) k].fromModule == pc.fromModule && pcs[(size_t) k].fromSocket == pc.fromSocket
                    && pcs[(size_t) k].toModule == pc.toModule && pcs[(size_t) k].paramId == pc.paramId)
                    processor.setParamCableBipolar (k, pc.bipolar);
        }
    }
    repaint();
}

juce::Path PatchCanvas::cablePath (juce::Point<float> from, juce::Point<float> to) const
{
    juce::Path p;
    const float sag = 30.0f + from.getDistanceFrom (to) * 0.15f;
    p.startNewSubPath (from);
    p.cubicTo (from.translated (0.0f, sag), to.translated (0.0f, sag), to);
    return p;
}

bool PatchCanvas::getCableEndpoints (const CableInfo& c, juce::Point<float>& from, juce::Point<float>& to) const
{
    auto* fromComp = findModuleComponent (c.fromModule);
    auto* toComp = findModuleComponent (c.toModule);
    if (fromComp == nullptr || toComp == nullptr)
        return false;

    from = fromComp->socketCentreInParent (c.fromSocket, false).toFloat();
    to = toComp->socketCentreInParent (c.toSocket, true).toFloat();
    return true;
}

int PatchCanvas::cableIndexNear (juce::Point<float> pos, float maxDistance) const
{
    // pos is a world point; the tolerance is quoted in screen pixels, so it has
    // to grow as the world shrinks or cables become unselectable when zoomed out
    maxDistance /= juce::jmax (0.01f, zoom);

    const auto& cables = processor.getCables();
    for (int i = 0; i < (int) cables.size(); ++i)
    {
        juce::Point<float> from, to;
        if (! getCableEndpoints (cables[(size_t) i], from, to))
            continue;

        juce::Point<float> nearest;
        cablePath (from, to).getNearestPoint (pos, nearest);
        if (nearest.getDistanceFrom (pos) <= maxDistance)
            return i;
    }
    return -1;
}

int PatchCanvas::paramCableIndexNear (juce::Point<float> pos, float maxDistance) const
{
    maxDistance /= juce::jmax (0.01f, zoom);

    const auto& pcs = processor.getParamCables();
    for (int i = 0; i < (int) pcs.size(); ++i)
    {
        auto* srcComp = findModuleComponent (pcs[(size_t) i].fromModule);
        auto* dstComp = findModuleComponent (pcs[(size_t) i].toModule);
        if (srcComp == nullptr || dstComp == nullptr)
            continue;

        if (! dstComp->isKnobVisible (pcs[(size_t) i].paramId))
            continue;   // not drawn, so it must not be selectable either

        const auto from = srcComp->socketCentreInParent (pcs[(size_t) i].fromSocket, false).toFloat();
        const auto to = dstComp->knobCentreInParent (pcs[(size_t) i].paramId).toFloat();

        juce::Point<float> nearest;
        cablePath (from, to).getNearestPoint (pos, nearest);
        if (nearest.getDistanceFrom (pos) <= maxDistance)
            return i;
    }
    return -1;
}

void PatchCanvas::paint (juce::Graphics& g)
{
    const auto background = themeColours().get (ThemeColours::Background);   // warm dark brown-grey by default
    g.fillAll (background);
    if (! themeColours().showGrid)
        return;   // the grid markers switched off in the colour panel
    g.setColour (background.getPerceivedBrightness() > 0.55f ? background.darker (0.12f)    // the subtle "+" grid
                                                             : background.interpolatedWith (juce::Colour (0xffb0a080), 0.09f));

    const float s = gridScreenSpacing();
    const float ox = std::fmod (std::fmod ((float) panOffset.x, s) + s, s) - s;
    const float oy = std::fmod (std::fmod ((float) panOffset.y, s) + s, s) - s;

    // only the marks that touch the area being redrawn (a module display repaints
    // its own small box many times a second): the same marks at the same places
    // as in the whole grid, so the same pixels
    const auto clip = g.getClipBounds().toFloat().expanded (2.0f).translated (-ox, -oy);
    if (clip.contains (getLocalBounds().toFloat().translated (-ox, -oy)))
    {
        g.strokePath (gridPath, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (ox, oy));
        return;
    }
    const float arm = juce::jlimit (2.0f, 3.0f, 3.0f * zoom);
    juce::Path part;
    for (float x = 0.0f; x <= (float) getWidth() + s; x += s)
    {
        if (x + arm < clip.getX() || x - arm > clip.getRight())
            continue;
        for (float y = 0.0f; y <= (float) getHeight() + s; y += s)
        {
            if (y + arm < clip.getY() || y - arm > clip.getBottom())
                continue;
            part.startNewSubPath (x - arm, y);
            part.lineTo (x + arm, y);
            part.startNewSubPath (x, y - arm);
            part.lineTo (x, y + arm);
        }
    }
    g.strokePath (part, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (ox, oy));
}

const juce::Path& PatchCanvas::dashedCable (std::vector<DashedCable>& cache, size_t index, juce::Point<float> from,
                                            juce::Point<float> to, float dash, float gap) const
{
    if (cache.size() <= index)
        cache.resize (index + 1);
    auto& c = cache[index];
    if (c.dashed.isEmpty() || c.from != from || c.to != to)
    {
        c.from = from;
        c.to = to;
        c.dashed.clear();
        const float dashes[] = { dash, gap };
        juce::PathStrokeType (1.8f).createDashedStroke (c.dashed, cablePath (from, to), dashes, 2);
    }
    return c.dashed;
}

void PatchCanvas::paintOverChildren (juce::Graphics& g)
{
    // Everything below is drawn in WORLD coordinates - the same coordinates the
    // module bounds are in - so the existing endpoint maths needs no change at
    // all: one transform puts the whole cable layer in the right place at the
    // right size. Stroke widths scale with it, which is what we want: cables
    // thin out as you zoom away instead of turning into a solid mat.
    juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (worldToScreen());

    // only what reaches the area being redrawn (in world coordinates): a cable
    // lies inside its ends' and control points' box, widened by its stroke
    const auto clip = g.getClipBounds().toFloat().expanded (6.0f);
    auto reaches = [&clip, this] (juce::Point<float> from, juce::Point<float> to)
    {
        const float sag = 30.0f + from.getDistanceFrom (to) * 0.15f;
        const auto box = juce::Rectangle<float> (from, to).getUnion (juce::Rectangle<float> (from, to).translated (0.0f, sag));
        // widened by the stroke: a cable whose two ends share one x (straight
        // down) has a box of width 0, and juce::Rectangle::intersects() says
        // false for an empty rectangle - such a cable was never drawn until
        // one of its modules moved a little
        return box.expanded (4.0f).intersects (clip);
    };

    // knob-modulation cables: dashed, source socket -> target knob
    {
        const auto& pcs = processor.getParamCables();
        for (int i = 0; i < (int) pcs.size(); ++i)
        {
            const auto& pc = pcs[(size_t) i];
            auto* srcComp = findModuleComponent (pc.fromModule);
            auto* dstComp = findModuleComponent (pc.toModule);
            if (srcComp == nullptr || dstComp == nullptr)
                continue;

            // target knob is hidden (collapsed module, or a visibleWhen rule):
            // the cable still exists and still modulates, it simply has nowhere
            // sensible to land, so it waits until the knob is on screen again
            if (! dstComp->isKnobVisible (pc.paramId))
                continue;

            const auto from = srcComp->socketCentreInParent (pc.fromSocket, false).toFloat();
            const auto to = dstComp->knobCentreInParent (pc.paramId).toFloat();
            if (! reaches (from, to))
                continue;

            g.setColour (i == selectedParamCableIndex ? juce::Colours::white
                                                      : juce::Colour (0xffd970b0).withAlpha (0.8f));
            g.fillPath (dashedCable (dashedParamCables, (size_t) i, from, to, 5.0f, 4.0f));
            g.fillEllipse (to.x - 3.0f, to.y - 3.0f, 6.0f, 6.0f);
        }
    }

    const auto& cables = processor.getCables();

    for (int i = 0; i < (int) cables.size(); ++i)
    {
        const auto& c = cables[(size_t) i];

        juce::Point<float> from, to;
        if (! getCableEndpoints (c, from, to) || ! reaches (from, to))
            continue;

        auto* src = processor.getInstance (c.fromModule);
        auto* dst = processor.getInstance (c.toModule);
        if (src == nullptr || dst == nullptr)
            continue;

        // cable colour always follows the source module's card colour
        auto colour = src->cardColour();
        if (i == selectedCableIndex)
            colour = juce::Colours::white;

        const int sockIdx = src->descriptor().outputIndexOf (c.fromSocket);
        const bool isAudio = sockIdx >= 0
            && src->descriptor().outputs()[(size_t) sockIdx]->kind == SocketKind::Audio;

        g.setColour (colour.withAlpha (0.92f));

        if (isAudio)
        {
            g.strokePath (cablePath (from, to), juce::PathStrokeType (3.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.fillPath (dashedCable (dashedCables, (size_t) i, from, to, 6.0f, 5.0f));
        }
    }

    // a module being dragged onto a cable or into a module: what it would connect, lit
    if (! drop.empty())
    {
        const auto& pcs = processor.getParamCables();
        const bool onCable = drop.cable >= 0 && drop.cable < (int) cables.size();
        const bool onParamCable = drop.paramCable >= 0 && drop.paramCable < (int) pcs.size();

        // in the colourway's own colours: the cable's (its source module's card,
        // as the cable is drawn), or the module dropped into
        auto lit = juce::Colour (0xff4fc3f7);
        const int colourOf = onCable ? cables[(size_t) drop.cable].fromModule
                           : onParamCable ? pcs[(size_t) drop.paramCable].fromModule : drop.target;
        if (auto* m = processor.getInstance (colourOf))
            lit = m->cardColour();

        auto glow = [&g, lit] (const juce::Path& path)
        {
            g.setColour (lit.withAlpha (0.35f));
            g.strokePath (path, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (lit);
            g.strokePath (path, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        if (onCable)
        {
            juce::Point<float> from, to;
            if (getCableEndpoints (cables[(size_t) drop.cable], from, to))
                glow (cablePath (from, to));
        }
        if (onParamCable)
        {
            const auto& pc = pcs[(size_t) drop.paramCable];
            auto* srcComp = findModuleComponent (pc.fromModule);
            auto* dstComp = findModuleComponent (pc.toModule);
            if (srcComp != nullptr && dstComp != nullptr)
                glow (cablePath (srcComp->socketCentreInParent (pc.fromSocket, false).toFloat(),
                                 dstComp->knobCentreInParent (pc.paramId).toFloat()));
        }
        auto outline = [&g, lit] (const ModuleComponent* m, float alpha)
        {
            if (m == nullptr) return;
            const auto r = m->getBounds().toFloat().expanded (4.0f);
            g.setColour (lit.withAlpha (alpha * 0.3f));
            g.drawRoundedRectangle (r.expanded (3.0f), 10.0f, 6.0f);
            g.setColour (lit.withAlpha (alpha));
            g.drawRoundedRectangle (r, 8.0f, 3.0f);
        };
        for (int id : drop.lit)
            outline (findModuleComponent (id), 1.0f);
        outline (findModuleComponent (drop.moduleId), 0.6f);

        // the knob a knob cable would go onto
        if (drop.knob.isNotEmpty())
            if (auto* m = findModuleComponent (drop.target))
            {
                const auto c = m->knobCentreInParent (drop.knob).toFloat();
                g.setColour (lit.withAlpha (0.35f));
                g.drawEllipse (juce::Rectangle<float> (34.0f, 34.0f).withCentre (c), 6.0f);
                g.setColour (lit.brighter (0.4f));
                g.drawEllipse (juce::Rectangle<float> (28.0f, 28.0f).withCentre (c), 2.5f);
            }
    }

    if (dragActive)
    {
        ModuleComponent* comp = findModuleComponent (dragModuleId);
        if (comp != nullptr)
        {
            const auto anchor = comp->socketCentreInParent (dragSocketId, dragFromInput).toFloat();
            const auto pos = dragCurrentPos.toFloat();
            const auto path = dragFromInput ? cablePath (pos, anchor) : cablePath (anchor, pos);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
    }

    // Region Select: the box being drawn
    if (regionDragging)
    {
        const auto box = juce::Rectangle<float> (screenToWorld (regionStart), screenToWorld (regionEnd));
        const auto accent = juce::Colour (0xff4fc3f7);
        const float px = 1.0f / juce::jmax (0.05f, zoom);   // one screen pixel
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (box);
        g.setColour (accent.withAlpha (0.9f));
        juce::Path outline;
        outline.addRectangle (box);
        juce::Path dashed;
        const float dashes[] = { 6.0f * px, 4.0f * px };
        juce::PathStrokeType (1.5f * px).createDashedStroke (dashed, outline, dashes, 2);
        g.fillPath (dashed);
    }
}

//==============================================================================
// Two-finger pinch on empty canvas: zoom about the fingers' midpoint, and
// move the view with it. (Android delivers no magnify gesture, only the
// separate touches.)
bool PatchCanvas::pinchTouch (const juce::MouseEvent& e, bool down, bool up)
{
    if (! e.source.isTouch())
        return false;

    const int index = e.source.getIndex();
    auto it = std::find_if (touches.begin(), touches.end(), [index] (const Touch& t) { return t.index == index; });

    if (up)
    {
        if (it != touches.end())
            touches.erase (it);
        const bool was = pinching;
        if (touches.size() < 2)
            pinching = false;
        return was;
    }
    if (down)
    {
        if (it == touches.end())
            touches.push_back ({ index, e.position });
        if (touches.size() == 2 && ! pinching)
        {
            pinching = true;
            panning = false;
            regionDragging = false;   // two fingers: the view, not a box
            pinchStartZoom = zoom;
            pinchStartPan = panOffset;
            pinchStartDist = juce::jmax (1.0f, touches[0].pos.getDistanceFrom (touches[1].pos));
            pinchStartMid = (touches[0].pos + touches[1].pos) * 0.5f;
        }
        return pinching;
    }
    if (it != touches.end())
        it->pos = e.position;
    if (! pinching || touches.size() < 2)
        return pinching;

    const float dist = juce::jmax (1.0f, touches[0].pos.getDistanceFrom (touches[1].pos));
    const auto mid = (touches[0].pos + touches[1].pos) * 0.5f;
    const float newZoom = juce::jlimit (minZoom, maxZoom, pinchStartZoom * dist / pinchStartDist);

    // the world point that was under the fingers' midpoint stays under it
    const juce::Point<float> anchor { (pinchStartMid.x - (float) pinchStartPan.x) / pinchStartZoom,
                                      (pinchStartMid.y - (float) pinchStartPan.y) / pinchStartZoom };
    const bool zoomChanged = std::abs (newZoom - zoom) > 1.0e-4f;
    zoom = newZoom;
    panOffset = juce::Point<int> (juce::roundToInt (mid.x - anchor.x * zoom), juce::roundToInt (mid.y - anchor.y * zoom));
    applyViewToChildren();
    if (zoomChanged)
    {
        rebuildGrid();
        if (auto* parent = getParentComponent())
            parent->resized();   // refresh the zoom readout
    }
    repaint();
    return true;
}

void PatchCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (pinchTouch (e, true, false))
        return;

    grabKeyboardFocus();

    const auto world = screenToWorld (e.position);
    const int hit = cableIndexNear (world);
    selectedCableIndex = hit;
    selectedParamCableIndex = hit >= 0 ? -1 : paramCableIndexNear (world);
    selectedModuleId = -1;
    regionSelection.clear();

    // pressing empty space (not on any cable) starts panning the view - or, in
    // Region Select, a box (the middle mouse button still moves the view)
    const bool empty = hit < 0 && selectedParamCableIndex < 0;
    regionDragging = regionSelect && empty && ! e.mods.isMiddleButtonDown();
    regionStart = regionEnd = e.position;
    panning = empty && ! regionDragging;
    panStartOffset = panOffset;
    panMouseStart = e.getPosition();

    setMouseCursor (panning ? juce::MouseCursor::DraggingHandCursor
                            : juce::MouseCursor::NormalCursor);
    repaint();
}

void PatchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (pinchTouch (e, false, false))
        return;
    if (regionDragging)
    {
        regionEnd = e.position;
        updateRegionSelection();   // the cards light up as the box reaches them
        repaint();
        return;
    }
    if (! panning)
        return;

    panOffset = panStartOffset + (e.getPosition() - panMouseStart);
    applyViewToChildren();
    repaint();
}

void PatchCanvas::mouseUp (const juce::MouseEvent& e)
{
    pinchTouch (e, false, true);
    if (regionDragging)
    {
        regionDragging = false;
        regionEnd = e.position;
        if (regionStart.getDistanceFrom (regionEnd) > 4.0f)
            updateRegionSelection();
        else
            regionSelection.clear();   // a tap on empty space: nothing selected
        repaint();
    }
    panning = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PatchCanvas::applyViewToChildren()
{
    const auto t = worldToScreen();

    for (auto& m : moduleComponents)
        if (auto* inst = processor.getInstance (m->getInstanceId()))
        {
            m->setTopLeftPosition (inst->position);   // world coordinates
            m->setTransform (t);                      // pan + zoom, view only
        }
}

void PatchCanvas::setZoom (float newZoom, juce::Point<float> focusScreenPos)
{
    newZoom = juce::jlimit (minZoom, maxZoom, newZoom);
    if (std::abs (newZoom - zoom) < 1.0e-4f)
        return;

    // Keep whatever is under the pointer (or under the middle of the screen)
    // pinned in place, so zooming feels like moving a camera rather than like
    // the patch jumping somewhere else and having to be hunted down again.
    const auto anchor = screenToWorld (focusScreenPos);

    zoom = newZoom;

    panOffset = juce::Point<int> (juce::roundToInt (focusScreenPos.x - anchor.x * zoom),
                                  juce::roundToInt (focusScreenPos.y - anchor.y * zoom));

    applyViewToChildren();
    rebuildGrid();
    repaint();

    if (auto* parent = getParentComponent())
        parent->resized();   // refresh the zoom readout
}

void PatchCanvas::zoomBy (float factor)
{
    setZoom (zoom * factor, getLocalBounds().getCentre().toFloat());
}

void PatchCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Plain wheel over empty canvas zooms about the pointer. Sliders consume
    // the wheel themselves, so a knob under the cursor still adjusts normally.
    if (std::abs (w.deltaY) < 1.0e-5f)
        return;

    setZoom (zoom * std::exp (w.deltaY * (w.isReversed ? -1.2f : 1.2f)), e.position);
}

void PatchCanvas::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    // Pinch, where the platform reports it. Android does not deliver magnify
    // gestures through JUCE, which is exactly why the on-screen +/- cluster
    // exists rather than being a convenience.
    if (scaleFactor > 0.0f)
        setZoom (zoom * scaleFactor, e.position);
}

void PatchCanvas::fitToContent()
{
    // the union of every module card, in world coordinates
    juce::Rectangle<int> world;
    for (auto& mc : moduleComponents)
        world = world.isEmpty() ? mc->getBounds() : world.getUnion (mc->getBounds());
    if (world.isEmpty() || getWidth() <= 0 || getHeight() <= 0)
    {
        resetView();
        return;
    }

    // leave room for the toolbar across the top of the patch area
    const int topMargin = 46, margin = 16;
    const float availW = (float) juce::jmax (100, getWidth() - 2 * margin);
    const float availH = (float) juce::jmax (100, getHeight() - topMargin - margin);
    zoom = juce::jlimit (minFitZoom, 1.0f, juce::jmin (availW / (float) world.getWidth(),
                                                    availH / (float) world.getHeight()));

    const float usedW = (float) world.getWidth() * zoom;
    panOffset = { juce::roundToInt ((float) margin + (availW - usedW) * 0.5f - (float) world.getX() * zoom),
                  juce::roundToInt ((float) topMargin - (float) world.getY() * zoom) };
    panning = false;
    applyViewToChildren();
    rebuildGrid();
    repaint();

    if (auto* parent = getParentComponent())
        parent->resized();
}

void PatchCanvas::resetView()
{
    panOffset = {};
    zoom = 1.0f;
    panning = false;
    applyViewToChildren();
    rebuildGrid();
    repaint();

    if (auto* parent = getParentComponent())
        parent->resized();
}

void PatchCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-clicking a cable removes it
    const auto world = screenToWorld (e.position);
    const int hit = cableIndexNear (world);
    if (hit >= 0)
    {
        processor.removeCable (hit);
        selectedCableIndex = -1;
        repaint();
        return;
    }

    // same gesture for a cable that lands on a knob
    const int pHit = paramCableIndexNear (world);
    if (pHit >= 0)
    {
        processor.removeParamCable (pHit);
        selectedParamCableIndex = -1;
        repaint();
    }
}

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    if (onShortcut != nullptr && onShortcut (key))
        return true;
    if (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey)
        return false;

    // a selected cable wins over a selected module: it is the more specific
    // (and more recent) click, so Delete removes only the cable
    if (selectedCableIndex >= 0)
    {
        processor.removeCable (selectedCableIndex);
        selectedCableIndex = -1;
        repaint();
        return true;
    }

    if (selectedParamCableIndex >= 0)
    {
        processor.removeParamCable (selectedParamCableIndex);
        selectedParamCableIndex = -1;
        repaint();
        return true;
    }

    if (const auto sel = getSelection(); ! sel.empty())
    {
        for (const int id : sel)
            processor.removeModule (id);   // takes its cables with it
        selectedModuleId = -1;
        regionSelection.clear();
        rebuild();
        return true;
    }

    return false;
}

void PatchCanvas::beginCableDrag (int moduleId, const juce::String& socketId, bool isInput,
                                  SocketKind kind, juce::Point<int> canvasPos)
{
    dragActive = true;
    dragModuleId = moduleId;
    dragSocketId = socketId;
    dragFromInput = isInput;
    dragKind = kind;
    dragCurrentPos = canvasPos;
    repaint();
}

void PatchCanvas::updateCableDrag (juce::Point<int> canvasPos)
{
    // arrives in canvas (screen) space; everything downstream works in world
    dragCurrentPos = screenToWorld (canvasPos.toFloat()).roundToInt();
    repaint();
}

void PatchCanvas::endCableDrag (juce::Point<int> screenPos)
{
    dragActive = false;

    // module bounds are world coordinates, so the drop point must be too
    const auto canvasPos = screenToWorld (screenPos.toFloat()).roundToInt();

    // Walk the modules TOPMOST FIRST (reverse creation order = reverse
    // z-order) and only consider a module if the drop point is actually on
    // it. The old forward scan let any OLDER module that overlapped the
    // target intercept the drop - connecting the cable to a knob hidden
    // underneath, or cancelling on a covered socket - which made knobs on
    // later-created modules (typically effects) randomly refuse cables.
    for (auto it = moduleComponents.rbegin(); it != moduleComponents.rend(); ++it)
    {
        auto& m = *it;

        // small margin so sockets right at the module edge stay droppable
        if (! m->getBounds().expanded (12).contains (canvasPos))
            continue;

        const auto localPos = canvasPos - m->getPosition();
        if (const auto* target = m->findSocketNear (localPos))
        {
            // direction is normalized to output -> input regardless of which
            // end the drag started from; output->output / input->input cancels
            if (target->isInput == dragFromInput)
                break;

            if (dragFromInput)
                processor.addCable (m->getInstanceId(), target->socketId, dragModuleId, dragSocketId);
            else
                processor.addCable (dragModuleId, dragSocketId, m->getInstanceId(), target->socketId);
            break;
        }

        // dropping an OUTPUT cable onto a rotary knob modulates that parameter.
        // Hidden on/off targets (e.g. Pitch Lock Filter keys) default to full
        // depth so a unipolar LFO or DAW automation can actually cross the
        // 0.5 on-threshold; ordinary knobs start at a gentle 25 % (one of the
        // depth menu's steps; the Modulation table sets any amount).
        // Sources that set a knob absolutely (Curve CV: 0..1 = the knob's whole
        // range) start at 100 %, so the curve really is the knob.
        if (! dragFromInput)
        {
            if (const auto* spec = m->findModulatableKnobNear (localPos))
            {
                const auto* src = processor.getInstance (dragModuleId);
                const bool absolute = src != nullptr && src->dsp != nullptr && src->dsp->drivesKnobsAbsolutely();
                processor.addParamCable (dragModuleId, dragSocketId,
                                         m->getInstanceId(), spec->id,
                                         (spec->hiddenCableTarget || absolute) ? 1.0f : 0.25f);
                break;
            }
        }

        // the topmost module directly under the pointer owns the drop: if it
        // has no target here, a module covered underneath must not steal it
        if (m->getBounds().contains (canvasPos))
            break;
    }

    repaint();
}
