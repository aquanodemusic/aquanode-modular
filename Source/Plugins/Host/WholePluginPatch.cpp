#include "WholePluginPatch.h"

namespace aquanode::plugins::whole
{

ModuleDescriptor baseDescriptor (const Def& def)
{
    ModuleDescriptor d;
    d.typeId = def.typeId();
    d.displayName = kit::moduleName (def.pluginId, def.kind);
    d.description = def.description;
    d.section = def.section;
    d.accent = kit::accentOf (def.pluginId);
    d.sidebarOrder = 400;
    if (def.midi) d.sockets.push_back (midiIn ("midiIn", "MIDI In"));
    if (def.audioIn) d.sockets.push_back (audioIn ("audioIn", "Audio In"));
    if (def.sideIn) d.sockets.push_back (audioIn ("sideIn", def.sideInLabel));
    if (def.cardsIn) d.sockets.push_back (modIn ("cardsIn", "Cards In"));
    d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
    return d;
}

void completeDescriptor (const Def& def, ModuleDescriptor& d)
{
    auto plugin = createOriginal (def.pluginId, 48000.0);
    if (plugin == nullptr) return;

    struct P { juce::RangedAudioParameter* p; juce::String id, name; };
    std::vector<P> list;
    juce::StringArray names;
    for (auto* prm : plugin->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->getParameterID();
            if (id.isEmpty() || d.findParam (id) != nullptr) continue;
            list.push_back ({ r, id, r->getName (40) });
            names.add (r->getName (40));
        }

    // knobs eight to a row (a menu takes two places), the plugin's order
    int row = 0, used = 0;
    auto place = [&row, &used] (int units)
    {
        if (used + units > 8) { ++row; used = 0; }
        used += units;
        return row;
    };
    for (const auto& e : list)
    {
        // a name the plugin uses more than once ("Attack" on every operator): with its group
        juce::String label = e.name;
        int same = 0;
        for (const auto& n : names) if (n == e.name) ++same;
        if (same > 1 && e.id.containsChar ('_')) label = e.id.upToFirstOccurrenceOf ("_", false, false).toUpperCase() + " " + e.name;

        const float* held = nullptr;
        for (const auto& f : def.fixed) if (f.first == e.id) held = &f.second;
        const bool hidden = held != nullptr || (def.hide != nullptr && def.hide (e.id));
        ParamSpec spec;
        auto* r = e.p;
        // the knob starts where a fresh instance of the plugin is (its start-up sound), not its nominal default
        const float now = r->convertFrom0to1 (r->getValue());
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r))
            spec = makeCombo (e.id, label, c->choices, c->getIndex(), hidden ? 0 : place (2), 2).noMod();
        else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (r))
        {
            const int w = def.wideSwitches ? 2 : 1;
            spec = makeCombo (e.id, label, { "Off", "On" }, b->get() ? 1 : 0, hidden ? 0 : place (w), w).noMod();
        }
        else if (auto* i = dynamic_cast<juce::AudioParameterInt*> (r))
            spec = makeRotary (e.id, label, (float) i->getRange().getStart(), (float) i->getRange().getEnd(), (float) i->get(),
                               hidden ? 0 : place (1), {}, false, 1.0f);
        else
        {
            const auto& range = r->getNormalisableRange();
            const bool logLike = range.skew < 0.7f && range.start > 0.0f;
            spec = makeRotary (e.id, label, range.start, range.end, juce::jlimit (range.start, range.end, now),
                               hidden ? 0 : place (1), r->getLabel(), logLike, range.interval);
        }
        if (hidden) { spec.hidden = true; spec.modulatable = false; }
        if (held != nullptr) spec.defaultValue = juce::jlimit (spec.minValue, spec.maxValue, *held);
        for (const auto& st : def.startAt)
            if (st.first == e.id && held == nullptr) spec.defaultValue = juce::jlimit (spec.minValue, spec.maxValue, st.second);
        d.params.push_back (std::move (spec));
    }
}

//==============================================================================
namespace
{
    // the plugin's own panel on the card, scaled to its width
    class PanelHost final : public juce::Component
    {
    public:
        PanelHost (juce::AudioProcessor& p, int width, int panelW, int panelH, juce::Rectangle<int> cropArea = {})
            : crop (cropArea)
        {
           #if JUCE_MAJOR_VERSION >= 9
            editor.reset (p.createEditorAndMakeActive());
           #else
            editor.reset (p.createEditorIfNeeded());   // the same, before JUCE 9
           #endif
            if (editor == nullptr) return;
            const int w = editor->getWidth() > 0 ? editor->getWidth() : panelW;
            const int h = editor->getHeight() > 0 ? editor->getHeight() : panelH;
            scale = (float) width / (float) juce::jmax (1, crop.isEmpty() ? w : crop.getWidth());
            editor->setBounds (0, 0, w, h);
            editor->setTransform (shown (scale));
            addAndMakeVisible (*editor);
        }
        ~PanelHost() override { editor.reset(); }
        void resized() override
        {
            if (editor == nullptr) return;
            // fill the card, keeping the panel's own shape (or the part of it shown)
            const auto part = crop.isEmpty() ? editor->getLocalBounds() : crop;
            const float s = juce::jmin ((float) getWidth() / (float) juce::jmax (1, part.getWidth()),
                                        (float) getHeight() / (float) juce::jmax (1, part.getHeight()));
            if (s > 0.0f) editor->setTransform (shown (s));
        }
    private:
        // (6.9.16) the crop's corner to the card's, then scaled
        juce::AffineTransform shown (float s) const
        {
            return juce::AffineTransform::translation ((float) -crop.getX(), (float) -crop.getY()).scaled (s);
        }
        juce::Rectangle<int> crop;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        float scale { 1.0f };
    };
}

Module::Module (const Def& d) : InstanceModule (d.pluginId, d.midi), def (d)
{
    // the side input always on (the card's socket feeds it, silence when nothing is patched)
    if (def.sideIn && plugin != nullptr)
        if (auto* bus = plugin->getBus (true, 1))
            bus->enable (true);
}

void Module::prepare (double sr)
{
    InstanceModule::prepare (sr);
    sideIdx = def.sideIn ? getDescriptor().inputIndexOf ("sideIn") : -1;
    sidePos = 0;
    for (auto& f : sideFifo) f.fill (0.0f);
}

void Module::processSample (const StereoFrame* in, StereoFrame* out)
{
    if (sideIdx >= 0)
    {
        sideFifo[0][(size_t) sidePos] = in[sideIdx][0];
        sideFifo[1][(size_t) sidePos] = in[sideIdx][1];
        sidePos = (sidePos + 1) % kBlock;
    }
    InstanceModule::processSample (in, out);
}

void Module::render (juce::AudioBuffer<float>& io, juce::MidiBuffer& midiIn)
{
    // the side input: the plugin's second input bus (channels 2 and 3)
    if (sideIdx >= 0 && io.getNumChannels() >= 4)
        for (int c = 0; c < 2; ++c)
            io.copyFrom (2 + c, 0, sideFifo[(size_t) c].data(), juce::jmin (kBlock, io.getNumSamples()));
    plugin->processBlock (io, midiIn);
}

std::unique_ptr<juce::Component> Module::createExtraContentComponent()
{
    if (! def.showPanel || plugin == nullptr) return nullptr;
    return std::make_unique<PanelHost> (*plugin, def.cardWidth, def.panelWidth, def.panelHeight, def.panelCrop);
}

int Module::extraContentHeight() const
{
    if (! def.showPanel || def.panelWidth <= 0) return 0;
    if (! def.panelCrop.isEmpty())
        return juce::roundToInt ((float) def.panelCrop.getHeight() * (float) def.cardWidth / (float) def.panelCrop.getWidth());
    return juce::roundToInt ((float) def.panelHeight * (float) def.cardWidth / (float) def.panelWidth);
}

bool Module::hasAudioToSave() const
{
    double rate = 0.0;
    return getAudioToSave (rate) != nullptr;
}

std::shared_ptr<const juce::AudioBuffer<float>> Module::getAudioToSave (double& rate) const
{
    if (plugin == nullptr) return nullptr;
    if (const auto* a = audioAccessFor (def.pluginId); a != nullptr && a->get != nullptr)
        return a->get (*plugin, rate);
    return nullptr;
}

void Module::restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
{
    if (plugin == nullptr) return;
    if (const auto* a = audioAccessFor (def.pluginId); a != nullptr && a->set != nullptr)
        a->set (*plugin, std::move (audio), rate);
}

//==============================================================================
PatchPorts build (const Def& def, AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin)
{
    kit::Builder b { patch, origin };
    const int m = b.add (def.typeId(), 0, 0);
    if (auto* mod = dynamic_cast<Module*> (b.dsp (m)))
    {
        mod->copyStateFrom (original);      // everything the plugin keeps, knobs or not
        b.copy (m, original);              // and the knobs at the plugin's values
        for (const auto& [id, v] : def.fixed)   // (6.9.16) but those this version holds
            b.set (m, id, v);
        for (const auto& [id, v] : def.startAt)   // (6.9.21) and where this version starts some others
            b.set (m, id, v);
        if (const auto* a = audioAccessFor (def.pluginId); a != nullptr && a->get != nullptr)
        {
            double rate = 44100.0;
            if (auto sound = a->get (original, rate))
                mod->restoreSavedAudio (std::make_shared<juce::AudioBuffer<float>> (*sound), rate);
        }
    }
    PatchPorts ports;
    ports.name = baseSpec (def.pluginId).displayName;
    ports.audioOut = { { m, "audioOut" } };
    if (def.audioIn) ports.audioIn = { { m, "audioIn" } };
    if (def.sideIn) ports.sideIn = { { m, "sideIn" } };
    if (def.midi) ports.midiIn = { { m, "midiIn" } };
    return ports;
}


//==============================================================================
// (6.9.14) the patch version in smaller cards
//==============================================================================
ParamSpec pluginSpec (const juce::String& pluginId, const juce::String& paramId)
{
    // every knob of the plugin, made once per plugin (as the whole module makes them)
    static std::map<juce::String, ModuleDescriptor> made;
    static std::mutex m;
    const std::lock_guard<std::mutex> lg (m);
    auto it = made.find (pluginId);
    if (it == made.end())
    {
        Def plain;
        plain.pluginId = pluginId;
        ModuleDescriptor d;
        completeDescriptor (plain, d);
        it = made.emplace (pluginId, std::move (d)).first;
    }
    if (const auto* spec = it->second.findParam (paramId))
        return *spec;
    return makeRotary (paramId, paramId, 0.0f, 1.0f, 0.0f, 0);
}

namespace
{
    PatchPorts buildWithCards (const Def& engineDef, const std::vector<kit::CardDef>& cards, int columns,
                               AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        std::vector<const kit::CardDef*> list;
        for (const auto& c : cards) list.push_back (&c);
        const auto places = kit::cardPlaces (list, 0, 0, columns);
        const int engineX = juce::jmin ((int) cards.size(), columns) * 300 + 40;

        const int m = b.add (engineDef.typeId(), engineX, 0);
        if (auto* mod = dynamic_cast<Module*> (b.dsp (m)))
        {
            mod->copyStateFrom (original);
            b.copy (m, original);
        }
        int prev = -1;
        for (size_t i = 0; i < cards.size(); ++i)
        {
            const int c = b.add (cards[i].typeId(), places[i].x, places[i].y);
            if (c < 0) continue;
            b.copy (c, original);   // the knobs at the plugin's values
            if (prev >= 0) b.cable (prev, "linkOut", c, "linkIn");
            prev = c;
        }
        if (prev >= 0) b.cable (prev, "linkOut", m, "cardsIn");

        PatchPorts ports;
        ports.name = baseSpec (engineDef.pluginId).displayName;
        ports.audioOut = { { m, "audioOut" } };
        if (engineDef.audioIn) ports.audioIn = { { m, "audioIn" } };
        if (engineDef.sideIn) ports.sideIn = { { m, "sideIn" } };
        if (engineDef.midi) ports.midiIn = { { m, "midiIn" } };
        return ports;
    }
}

bool registerWithCards (const Def& wholeDef, const Def& engineDef, const std::vector<kit::CardDef>& cards,
                        PresetLoadFn loader, int columns)
{
    auto& f = ModuleFactory::instance();
    f.registerModule (baseDescriptor (wholeDef), [&wholeDef] { return std::make_unique<Module> (wholeDef); },
                      [&wholeDef] (ModuleDescriptor& d) { completeDescriptor (wholeDef, d); });
    f.registerModule (baseDescriptor (engineDef), [&engineDef] { return std::make_unique<Module> (engineDef); },
                      [&engineDef] (ModuleDescriptor& d) { completeDescriptor (engineDef, d); });
    for (const auto& c : cards)
        kit::registerCard (c);
    return registerPatchVersion (engineDef.pluginId,
        [&engineDef, &cards, columns] (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin)
        { return buildWithCards (engineDef, cards, columns, patch, original, origin); },
        loader);
}

}
