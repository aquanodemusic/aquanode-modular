#include "FruitModules.h"
#include "FruitEditor.h"
#include "../Host/PluginCatalog.h"
#include "../../Editor/ModuleCard.h"

static aquanode::ModuleDescriptor fruitNoteFeedDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fruit.notefeed";
    d.displayName = "Fruit DAW - Notes";
    d.description = "The notes of one of Fruit DAW's synth slots (only inside a Fruit DAW card).";
    d.section = ModuleSection::Utility;
    d.listedInSidebar = false;
    d.sockets = { midiOut ("midiOut", "MIDI Out") };
    return d;
}
AQUANODE_REGISTER_MODULE (FruitNoteFeed, fruitNoteFeedDescriptor)

static aquanode::ModuleDescriptor fruitAudioFeedDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fruit.audiofeed";
    d.displayName = "Fruit DAW - Samples";
    d.description = "The playlist's audio clips that go to one of Fruit DAW's mixer inserts (only inside a Fruit DAW card).";
    d.section = ModuleSection::Utility;
    d.listedInSidebar = false;
    d.sockets = { audioOut ("audioOut", "Audio Out") };
    return d;
}
AQUANODE_REGISTER_MODULE (FruitAudioFeed, fruitAudioFeedDescriptor)

static aquanode::ModuleDescriptor fruitMeterDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fruit.meter";
    d.displayName = "Fruit DAW - Meter";
    d.description = "The end of one of Fruit DAW's mixer inserts: polarity, pan and its level meter (only inside a Fruit DAW card).";
    d.section = ModuleSection::Utility;
    d.listedInSidebar = false;
    d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
    d.params = {
        makeRotary ("pan", "Pan", -100.0f, 100.0f, 0.0f, 0, "%").whenMissing (0.0f),
        makeCombo ("invert", "Polarity", { "Normal", "Inverted" }, 0, 0, 2).noMod().whenMissing (0.0f)
    };
    return d;
}
AQUANODE_REGISTER_MODULE (FruitMeter, fruitMeterDescriptor)


//==============================================================================
// the patch version: Fruit DAW as four cards
//==============================================================================
namespace
{
    // A part card's content: that window of the Fruit DAW with the same key. It
    // looks for it among the patch's cards once it is on one, keeps the Fruit DAW
    // alive while it shows it, and lends its panel to the Full Size view.
    class FruitPartHolder : public juce::Component,
                            public aquanode::ExtraContentParamListener,
                            public aquanode::plugins::PluginEditorLender,
                            private juce::ComponentListener,
                            private juce::Timer
    {
    public:
        explicit FruitPartHolder (FruitPartModule& m) : module (&m), part (m.part)
        {
            setSize (m.width.load(), m.height.load());
            startTimer (150);
        }
        ~FruitPartHolder() override
        {
            if (editor != nullptr)
            {
                if (auto* parent = editor->getParentComponent()) parent->removeChildComponent (editor.get());
                editor->removeComponentListener (this);
            }
            editor = nullptr;      // before the processor
            processor = nullptr;
        }
        void paint (juce::Graphics& g) override
        {
            if (editor != nullptr && ! lent) return;
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText (editor == nullptr ? "Looking for its Fruit DAW ..." : "Shown in full size", getLocalBounds(), juce::Justification::centred);
        }
        void parentHierarchyChanged() override { connect(); }
        juce::Component* lendEditor() override
        {
            if (editor == nullptr || lent) return nullptr;
            lent = true;
            removeChildComponent (editor.get());
            repaint();
            return editor.get();
        }
        void returnEditor() override
        {
            if (editor == nullptr || ! lent) return;
            lent = false;
            editor->setTransform ({});
            addAndMakeVisible (editor.get());
            editor->setTopLeftPosition (0, 0);
            repaint();
        }
    private:
        void timerCallback() override { connect(); }
        void connect()
        {
            if (editor != nullptr) { stopTimer(); return; }
            auto* m = dynamic_cast<FruitPartModule*> (module.get());
            auto* card = findParentComponentOfClass<ModuleComponent>();
            if (m == nullptr || card == nullptr || m->key.isEmpty()) return;
            auto& patch = card->getProcessor();
            for (int id : patch.getModuleIds())
                if (auto* inst = patch.getInstance (id))
                    if (auto* h = dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get()))
                        if (auto* fp = dynamic_cast<FruitProcessor*> (h->getPlugin()); fp != nullptr && fp->splitKey == m->key)
                        {
                            processor = h->sharePlugin();
                            editor = std::make_unique<FruitEditor> (*fp, part);
                            editor->setSize (m->width.load(), m->height.load());
                            addAndMakeVisible (editor.get());
                            editor->setTopLeftPosition (0, 0);
                            editor->addComponentListener (this);
                            stopTimer();
                            repaint();
                            return;
                        }
        }
        void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
        {
            if (! wasResized || &c != editor.get() || lent) return;
            if (auto* m = dynamic_cast<FruitPartModule*> (module.get())) { m->width = c.getWidth(); m->height = c.getHeight(); }
            setSize (c.getWidth(), c.getHeight());
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FruitPartHolder> (this)]
            {
                if (safe != nullptr && safe->paramsChangedByContent) safe->paramsChangedByContent();
            });
        }
        juce::WeakReference<aquanode::SynthModule> module;
        const int part;
        std::shared_ptr<juce::AudioProcessor> processor;
        std::unique_ptr<FruitEditor> editor;
        bool lent = false;
    };

    aquanode::ModuleDescriptor fruitPartDescriptor (const char* typeId, const char* name)
    {
        using namespace aquanode;
        ModuleDescriptor d;
        d.typeId = typeId;
        d.displayName = name;
        d.description = "One of the four cards of Fruit DAW's patch version: the channel rack (which has the sockets), the piano roll, "
                        "the playlist and the mixer. They work as one and are deleted together.";
        d.section = ModuleSection::Oscillator;
        d.accent = juce::Colour (0xff1fd5c9);     // Fruit DAW's own
        d.listedInSidebar = false;
        return d;
    }
    aquanode::ModuleDescriptor fruitRackDescriptor()
    {
        auto d = fruitPartDescriptor ("fruit.part.rack", "Fruit DAW - Channel Rack");
        d.sockets = { aquanode::midiIn ("midiIn", "MIDI In") };      // the keys, unless something is patched in
        return d;
    }
    aquanode::ModuleDescriptor fruitRollDescriptor()     { return fruitPartDescriptor ("fruit.part.roll", "Fruit DAW - Piano Roll"); }
    aquanode::ModuleDescriptor fruitPlaylistDescriptor() { return fruitPartDescriptor ("fruit.part.playlist", "Fruit DAW - Playlist"); }
    aquanode::ModuleDescriptor fruitMixerDescriptor()    { return fruitPartDescriptor ("fruit.part.mixer", "Fruit DAW - Mixer"); }
}

FruitPartModule::FruitPartModule (int whichPart)
    : part (whichPart), width (FruitEditor::partSize (whichPart).x), height (FruitEditor::partSize (whichPart).y) {}
FruitRackPart::FruitRackPart()         : FruitPartModule (FruitEditor::RackPart) {}
void FruitRackPart::rawMidiMessage (const juce::MidiMessage& m) { FruitProcessor::sendMidiTo (key, m); }
FruitRollPart::FruitRollPart()         : FruitPartModule (FruitEditor::RollPart) {}
FruitPlaylistPart::FruitPlaylistPart() : FruitPartModule (FruitEditor::PlaylistPart) {}
FruitMixerPart::FruitMixerPart()       : FruitPartModule (FruitEditor::MixerPart) {}

std::unique_ptr<juce::Component> FruitPartModule::createExtraContentComponent()
{
    return std::make_unique<FruitPartHolder> (*this);
}

AQUANODE_REGISTER_MODULE (FruitRackPart, fruitRackDescriptor)
AQUANODE_REGISTER_MODULE (FruitRollPart, fruitRollDescriptor)
AQUANODE_REGISTER_MODULE (FruitPlaylistPart, fruitPlaylistDescriptor)
AQUANODE_REGISTER_MODULE (FruitMixerPart, fruitMixerDescriptor)

// The patch version: a Fruit DAW card showing the channel rack only, and the
// three part cards beside and under it, as the one panel lays them out.
static const bool fruitPatchRegistered = aquanode::plugins::registerPatchVersion ("fruitgui",
    [] (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin) -> aquanode::plugins::PatchPorts
    {
        aquanode::plugins::PatchPorts ports;
        ports.name = "Fruit DAW";
        // the mixer card owns the Fruit DAW and has the Audio Out; the channel rack card has the Midi In
        const int colW = FruitEditor::partSize (FruitEditor::RackPart).x + 60, rowH = FruitEditor::partSize (FruitEditor::RackPart).y + 110;
        const int owner = patch.addModule ("plugin.fruitgui.mixer", origin + juce::Point<int> (colW, rowH));
        auto* inst = patch.getInstance (owner);
        auto* hosted = inst != nullptr ? dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get()) : nullptr;
        auto* fp = hosted != nullptr ? dynamic_cast<FruitProcessor*> (hosted->getPlugin()) : nullptr;
        if (fp == nullptr) return ports;
        {
            juce::MemoryBlock state;
            original.getStateInformation (state);
            fp->setStateInformation (state.getData(), (int) state.getSize());
        }
        fp->setSplit (juce::Uuid().toString(), FruitEditor::MixerPart);
        hosted->noteEditorSize (FruitEditor::partSize (FruitEditor::MixerPart).x, FruitEditor::partSize (FruitEditor::MixerPart).y);
        const std::pair<const char*, juce::Point<int>> parts[] = { { "fruit.part.rack", { 0, 0 } }, { "fruit.part.roll", { colW, 0 } },
                                                                    { "fruit.part.playlist", { 0, rowH } } };
        for (const auto& p : parts)
        {
            const int id = patch.addModule (p.first, origin + p.second);
            if (auto* pi = patch.getInstance (id)) pi->dsp->loadCustomState (fp->splitKey);
            if (juce::String (p.first) == "fruit.part.rack") ports.midiIn.push_back ({ id, "midiIn" });
        }
        ports.audioOut.push_back ({ owner, "audioOut" });
        return ports;
    }, nullptr);

// (6.9.16) the DAW preset's Fruit DAW: a card with three lanes - a synth on each (Sine, Saw
// and Triangle Oscillators) through an effect on its own insert (Delay, Reverb, Chorus) - a
// pattern each, the three on the playlist from the start, its Audio Out into the patch's
int fruitDawThreeLanes (AquanodeModularAudioProcessor& patch, juce::Point<int> at)
{
    const int id = patch.addModule ("plugin.fruitgui", at);
    auto* inst = patch.getInstance (id);
    auto* hosted = inst != nullptr ? dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get()) : nullptr;
    auto* fp = hosted != nullptr ? dynamic_cast<FruitProcessor*> (hosted->getPlugin()) : nullptr;
    if (fp == nullptr) return id;
    const char* const waves[3] = { "Sine", "Saw", "Tri" };
    const char* const effects[3] = { "fx.delay", "fx.reverb", "fx.chorus" };
    for (int lane = 1; lane <= 3; ++lane)
    {
        if (! fp->loadSynth (lane, "osc.basic")) continue;
        if (auto* osc = fp->getEngine().getInstance (fp->slots[(size_t) lane].synthId))
        {
            osc->dsp->setParameter ("volume", 0.8f);
            if (const auto* spec = osc->descriptor().findParam ("waveform"))
                if (const int w = spec->choices.indexOf (waves[lane - 1]); w >= 0)
                    osc->dsp->setParameter ("waveform", (float) w);
        }
        fp->loadEffect (lane, 0, effects[lane - 1]);
        auto& pat = fp->song.patterns[(size_t) lane];
        pat.slot = lane;
        pat.lengthBeats = 16.0;
        fruit::Clip c;
        c.track = lane;
        c.start = 0.0;
        c.length = 16.0;
        c.pattern = lane;
        fp->song.clips.push_back (c);
    }
    fp->songChanged();
    return id;
}
