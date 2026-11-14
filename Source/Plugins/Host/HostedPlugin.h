#pragma once

#include <JuceHeader.h>
#include "../../Modules/Core/ModuleCore.h"

//==============================================================================
// GUI versions of the merged plugins
//
// The Plugins section of the sidebar offers each of the plugins that were
// merged into Aquanode Modular in two versions. The PATCH version builds the
// plugin out of ordinary modules ("Aquanova - Oscillator", ...). The GUI
// version is this module: the original plugin, running whole inside one card
// with its own window drawn on it. Its knobs are the plugin's own, so they
// take no cables - the card's sockets are only what the original plugin had
// as ports: Midi In for the synths, Audio In / Audio Out, the Vocoder's second
// input and NoteAdder's Midi Out.
//
// The plugin runs in blocks of kBlockSize samples behind the per-sample
// patch, so its audio (and NoteAdder's MIDI) comes out kBlockSize samples
// late - 0.7 ms at 48 kHz. Everything it needs from the host is passed on:
// the MIDI its Midi In hears (sample-accurate inside its block), tempo and
// transport as a play head, and its whole state is saved with the patch.
//==============================================================================
namespace aquanode::plugins
{
    // A plugin GUI card's content can lend its editor out: the window's Full
    // Size view takes the plugin's own editor off the card, shows it alone at
    // its own size, and gives it back. (HostedEditorHolder implements it.)
    struct PluginEditorLender
    {
        virtual ~PluginEditorLender() = default;
        virtual juce::Component* lendEditor() = 0;   // off the card; nullptr if there is none
        virtual void returnEditor() = 0;             // back onto the card

        // false: the card draws no Full Size pill - the content has a button of
        // its own for it (the Random Preset module's Open)
        virtual bool wantsFullSizePill() const { return true; }
        // set by the card: shows the content's editor in the Full Size view
        std::function<void()> requestFullSize;
        // set by the card: puts a plugin card holding the content's preset in
        // the module's place (the Random Plugin Preset module's Open in Plugin)
        std::function<void()> requestOpenInPlugin;
    };

    // A plugin that plays a song of its own (Fruit DAW): the Piano Rolls' Play
    // All / Stop All and Render to File start and stop it too.
    struct SongPlayer
    {
        virtual ~SongPlayer() = default;
        virtual double songBeats() const = 0;          // how long its song is; 0 = nothing to play
        virtual void playSongFromStart (bool play) = 0; // audio thread
    };

    // A plugin shown as several cards that belong together (Fruit DAW's patch
    // version): cards with the same non-empty group are deleted together and
    // are not cloned.
    struct LinkedGroupPlugin
    {
        virtual ~LinkedGroupPlugin() = default;
        virtual juce::String linkGroup() const = 0;
    };

    struct HostedSpec
    {
        juce::String pluginId;          // catalog key, "aquanova"
        juce::String typeId;            // "plugin.aquanova"
        juce::String displayName;       // "Aquanova"
        juce::String description;
        juce::Colour accent;
        ModuleSection section { ModuleSection::Oscillator };

        bool midiIn { false };
        bool midiOut { false };
        bool audioIn { false };         // the plugin's main input
        bool sideIn { false };          // a second input bus (Vocode's modulator)
        juce::String sideInLabel { "Mod In" };

        int defaultWidth { 800 }, defaultHeight { 500 };   // until its editor has been opened once

        std::function<juce::AudioProcessor*()> create;
    };

    ModuleDescriptor makeHostedDescriptor (const HostedSpec& spec);

    class HostedPluginModule : public SynthModule
    {
    public:
        static constexpr int kBlockSize = 32;

        explicit HostedPluginModule (const HostedSpec& spec);
        ~HostedPluginModule() override;

        // the hosted processor itself - for the "convert to patch" path, which
        // reads its parameters, and for the editor card
        juce::AudioProcessor* getPlugin() const { return plugin.get(); }
        // ... kept alive by whoever shows more of it on another card
        std::shared_ptr<juce::AudioProcessor> sharePlugin() const { return plugin; }
        double takeTempoRequest() override
        {
            if (auto* t = dynamic_cast<aquanode::TempoRequester*> (plugin.get())) return t->takeTempoRequest();
            return 0.0;
        }
        juce::String linkGroup() const override
        {
            if (auto* l = dynamic_cast<LinkedGroupPlugin*> (plugin.get())) return l->linkGroup();
            return {};
        }
        const HostedSpec& getSpec() const { return spec; }

        //=== SynthModule ======================================================
        void prepare (double newSampleRate) override;
        void reset() override;
        void blockStart() override;
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        void processSample (const StereoFrame* inputs, StereoFrame* outputs) override;
        // the block the plugin runs behind the patch, plus the plugin's own
        // latency (a nested Aquanode's patch): passed on to the host
        int latencySamples() const override;

        bool wantsRawMidi() const override { return spec.midiIn; }
        void rawMidiMessage (const juce::MidiMessage& message) override;

        bool isMidiNoteDriver() const override { return spec.midiOut; }
        bool midiSourceReplacesInput() const override { return spec.midiOut; }
        int midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents) override;
        void midiAllNotesOff() override;

        std::unique_ptr<juce::Component> createExtraContentComponent() override;
        int preferredModuleWidth() const override;
        int extraContentHeight() const override;

        juce::String saveCustomState() const override;
        void loadCustomState (const juce::String& state) override;

        // the plugin's own audio (Slicer's sample), saved with the patch
        bool hasAudioToSave() const override;
        std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override;
        void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override;

        // the card's editor reports its size here when it opens or resizes
        void noteEditorSize (int w, int h);

    private:
        struct PlayHead : public juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override { return info; }
            PositionInfo info;
        };

        void runBlock();
        void updatePlayHead();

        const HostedSpec& spec;
        std::shared_ptr<juce::AudioProcessor> plugin;
        PlayHead playHead;

        juce::AudioBuffer<float> io;      // the plugin's process buffer
        juce::MidiBuffer midiInBuffer, midiOutScratch;
        std::array<std::array<float, kBlockSize>, 2> outFifo {};
        int pos { 0 };                    // sample inside the current block
        int mainIns { 0 }, outs { 0 }, sideChannels { 0 };
        // the side-chain bus is on only while a cable feeds it, as in a DAW
        // with nothing routed to the side chain (Vocode then auto-vocodes)
        std::atomic<int> sideIns { 0 };
        std::atomic<bool> sideRelayoutPending { false };
        void setSideChainEnabled (bool enabled);
        int inAudioIdx { -1 }, inSideIdx { -1 }, outAudioIdx { -1 }, numOutputs { 1 };
        int samplesThisHostBlock { 0 };
        juce::int64 sampleCounter { 0 };
        bool prepared { false };

        // NoteAdder's output, due one block after it was produced
        struct PendingNote { juce::int64 due; MidiDriverEvent ev; };
        std::vector<PendingNote> pendingOut;
        juce::int64 driverCounter { 0 }, driverCounterAtLastBlock { -1 };

        // Play All / Stop All, passed on to a SongPlayer (the first block only takes note of the count)
        juce::uint32 seenRollCommand { 0 };
        bool rollCommandSeen { false };

        std::atomic<int> editorW, editorH;
        // the last state handed out: the same bytes give back the same text, shared rather than copied -
        // the undo history (32 steps) then holds a big state (a plugin's samples, a bundle of sounds) once
        mutable juce::CriticalSection stateCacheLock;
        mutable juce::MemoryBlock cachedStateBytes;
        mutable juce::String cachedState;

        JUCE_DECLARE_NON_COPYABLE (HostedPluginModule)
    };
}

// Defines and registers the GUI module of one plugin. specFn returns a
// reference to a static HostedSpec; the class name doubles as the
// registration symbol, so it has to be listed in ModuleRegistry.cpp.
#define AQUANODE_HOSTED_PLUGIN(ClassName, specFn) \
    class ClassName final : public aquanode::plugins::HostedPluginModule \
    { \
    public: \
        ClassName() : aquanode::plugins::HostedPluginModule (specFn()) {} \
    }; \
    static aquanode::ModuleDescriptor ClassName##Descriptor() { return aquanode::plugins::makeHostedDescriptor (specFn()); } \
    AQUANODE_REGISTER_MODULE (ClassName, ClassName##Descriptor)
