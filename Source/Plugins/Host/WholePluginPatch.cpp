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
    if (def.midi) d.sockets.push_back (midiIn ("midiIn", "Midi In"));
    if (def.audioIn) d.sockets.push_back (audioIn ("audioIn", "Audio In"));
    if (def.sideIn) d.sockets.push_back (audioIn ("sideIn", def.sideInLabel));
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

        const bool hidden = def.hide != nullptr && def.hide (e.id);
        ParamSpec spec;
        auto* r = e.p;
        // the knob starts where a fresh instance of the plugin is (its start-up sound), not its nominal default
        const float now = r->convertFrom0to1 (r->getValue());
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r))
            spec = makeCombo (e.id, label, c->choices, c->getIndex(), hidden ? 0 : place (2), 2).noMod();
        else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (r))
            spec = makeCombo (e.id, label, { "Off", "On" }, b->get() ? 1 : 0, hidden ? 0 : place (1), 1).noMod();
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
        PanelHost (juce::AudioProcessor& p, int width, int panelW, int panelH)
        {
           #if JUCE_MAJOR_VERSION >= 9
            editor.reset (p.createEditorAndMakeActive());
           #else
            editor.reset (p.createEditorIfNeeded());   // the same, before JUCE 9
           #endif
            if (editor == nullptr) return;
            const int w = editor->getWidth() > 0 ? editor->getWidth() : panelW;
            const int h = editor->getHeight() > 0 ? editor->getHeight() : panelH;
            scale = (float) width / (float) juce::jmax (1, w);
            editor->setBounds (0, 0, w, h);
            editor->setTransform (juce::AffineTransform::scale (scale));
            addAndMakeVisible (*editor);
        }
        ~PanelHost() override { editor.reset(); }
        void resized() override
        {
            if (editor == nullptr) return;
            // fill the card, keeping the panel's own shape
            const float s = juce::jmin ((float) getWidth() / (float) juce::jmax (1, editor->getWidth()),
                                        (float) getHeight() / (float) juce::jmax (1, editor->getHeight()));
            if (s > 0.0f) editor->setTransform (juce::AffineTransform::scale (s));
        }
    private:
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
    return std::make_unique<PanelHost> (*plugin, def.cardWidth, def.panelWidth, def.panelHeight);
}

int Module::extraContentHeight() const
{
    if (! def.showPanel || def.panelWidth <= 0) return 0;
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

}
