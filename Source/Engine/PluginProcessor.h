#pragma once

#include <JuceHeader.h>
#include <map>
#include <algorithm>
#include <vector>
#include "../Modules/Core/ModuleCore.h"
#include "../Modules/InputOutput/AudioRouting/AudioOutModule.h"

//==============================================================================
// One patched instance of a module in the graph.
struct ModuleInstance
{
    int id { 0 };
    const aquanode::RegisteredModule* reg { nullptr };
    std::unique_ptr<aquanode::SynthModule> dsp;
    juce::Point<int> position;

    // editor-only: module drawn as only its header, controls hidden. Lives
    // here rather than in the ModuleComponent so it survives a canvas rebuild
    // and gets saved with the patch alongside the position.
    bool collapsed { false };

    // editor-only: a colour of its own for this one card (transparent = the
    // module type's colour), saved with the patch only when set
    juce::Colour customColour;
    juce::Colour cardColour() const { return customColour.isTransparent() ? descriptor().colour() : customColour; }

    // resolved voice lane (Flexible modules resolve at graph compile time)
    bool perVoice { false };
    int numIn { 1 }, numOut { 1 };

    // instance ids of every Midi Add / Arp / Always Midi / Discard Midi
    // patched into this module's "Add Midi In" socket. Empty = nothing
    // patched, so the module only hears the keyboard. Several sources may
    // feed one generator at once: it then answers to all of them (three
    // Always Midi drones, or an Arp AND a Midi Add, on one oscillator).
    // midiSourceReplaces is true when ANY connected source answers yes to
    // "do my notes replace the played ones?" (Arp, Always Midi, Discard
    // Midi) - one replacing source is enough to take the keyboard away.
    std::vector<int> midiSourceIds;
    bool midiSourceReplaces { false };

    bool listensTo (int sourceInstanceId) const
    {
        return std::find (midiSourceIds.begin(), midiSourceIds.end(), sourceInstanceId)
                 != midiSourceIds.end();
    }

    // audio thread only: did this module get a note-on for engine voice v?
    // A gated module (a generator with a Midi In) stays silent on every voice
    // it was not given a note for - otherwise its envelope, shared by the
    // whole voice, would make it sound at a stale pitch on notes that were
    // meant for some OTHER generator (the "phantom note" bug).
    bool voiceHeard[aquanode::kMaxVoices] {};

    // audio thread only, for a global module that counts voices (Polyphony
    // Sum where voices are summed): samples since each voice last reached it
    int voiceQuiet[aquanode::kMaxVoices] {};

    // runtime socket buffers (audio thread only)
    std::vector<aquanode::StereoFrame> inBuf, outBuf, prevOutBuf;              // global lane
    std::vector<aquanode::StereoFrame> outV, prevOutV;   // per-voice lanes, [voice * numOut + socket]

    const aquanode::ModuleDescriptor& descriptor() const { return reg->descriptor; }
};

struct CableInfo
{
    int fromModule { 0 };
    juce::String fromSocket;
    int toModule { 0 };
    juce::String toSocket;
};

// modulation of a knob: any output -> a rotary parameter, scaled by depth
// (-1..1 of the parameter's full range). A module running per voice gets each
// voice's own value of a per-voice source; a module in the global lane gets
// per-voice sources voice-summed, like any other global consumer.
struct ParamCableInfo
{
    int fromModule { 0 };
    juce::String fromSocket;
    int toModule { 0 };
    juce::String paramId;
    float depth { 0.25f };
    // the knob moves both ways around its setting (true) or one way (false);
    // a new cable starts as its source runs: an LFO bipolar, an envelope not
    bool bipolar { true };
};

//==============================================================================
class AquanodeModularAudioProcessor : public juce::AudioProcessor,
                                      public juce::ChangeBroadcaster
{
public:
    AquanodeModularAudioProcessor();
    ~AquanodeModularAudioProcessor() override;

    //=== AudioProcessor =======================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Aquanode Modular"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 21.0; }   // covers max 20s ADSR release

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //=== patch model (message thread) =========================================
    int addModule (const juce::String& typeId, juce::Point<int> position);
    void removeModule (int instanceId);                 // removes touching cables too
    int cloneModule (int instanceId);                   // params + sample, no cables
    void setModulePosition (int instanceId, juce::Point<int> position);

    bool addCable (int fromModule, const juce::String& fromSocket,
                   int toModule, const juce::String& toSocket);
    void removeCable (int cableIndex);

    bool addParamCable (int fromModule, const juce::String& fromSocket,
                        int toModule, const juce::String& paramId, float depth);
    void removeParamCable (int index);
    void setParamCableDepth (int index, float depth);
    void setParamCableBipolar (int index, bool bipolar);
    // whether a module's output runs 0..1 (SocketSpec::unipolar)
    bool outputIsUnipolar (int moduleId, const juce::String& socket) const;
    const std::vector<ParamCableInfo>& getParamCables() const { return paramCables; }

    std::vector<int> getModuleIds() const;
    ModuleInstance* getInstance (int instanceId) const;
    const std::vector<CableInfo>& getCables() const { return cables; }

    bool exportPatchToZip (const juce::File& zipFile);
    bool importPatchFromZip (const juce::File& zipFile);

    // Stream variants: used by the Android build to write to / read from a
    // document the user picked via the system file dialog (Storage Access
    // Framework). Same .zip container as the File versions -> fully compatible.
    bool exportPatchToStream (juce::OutputStream& out);
    bool importPatchFromStream (juce::InputStream& in, const juce::String& fileName = {});

    // Blank slate: Oscillator -> Polyphony Sum -> Audio Out; in Aquanode
    // Modular FX (AQUANODE_FX_BUILD=1, the only build switch) Audio In -> Audio Out.
    void initializePatch();

    // Clears the patch and lets a patch generator (src/Presets) build a new
    // one into it - the Preset Generation rows (Colorbass, FM, genres). The builder
    // returns the patch's display name, which becomes patchName.
    juce::String generatePatch (const std::function<juce::String (AquanodeModularAudioProcessor&)>& build);
    // set by a generator whose patch is a starting point to build on (DAW):
    // its sound sources keep today's balanced levels instead of the full
    // level generated sounds are designed with (generatePatch resets it)
    bool generatedKeepsTrims { false };

    // A short name for what is loaded ("DnB Bass 07", "Init"), shown in the
    // patch area. Never saved in a patch: an imported patch is called after
    // its file. Message thread only.
    juce::String patchName { "Init" };

    // On-screen keyboard (the bottom keyboard every build shows, toggled by
    // the Keys button). The editor attaches a MidiKeyboardComponent to this
    // state; processBlock merges its events into the incoming MIDI so clicks
    // and taps play exactly like host/keyboard notes. It also records the
    // incoming stream, so on-screen keys light up when a real controller plays.
    juce::MidiKeyboardState keyboardState;

    // MPE (the switch beside the module search, kept in the app's settings):
    // with it on, notes on MIDI channels 2..16 each bend, slide and press on
    // their own (MPE controllers; the on-screen keys, every finger its own
    // note), and MPE modules give each voice's Slide and pressure. Off: every
    // channel plays as before.
    std::atomic<bool> mpeEnabled { false };
    // MIDI the editor makes (the on-screen keys with MPE on): played at the
    // start of the next block. Message thread.
    void addUiMidi (const juce::MidiMessage& m)
    {
        const juce::SpinLock::ScopedLockType sl (uiMidiLock);
        uiMidi.addEvent (m, 0);
    }

    // The Sidebar / Keys panel toggles live here rather than in the editor, so
    // closing and reopening a plugin window keeps them as they were left.
    bool uiSidebarVisible { true };
    bool uiKeyboardVisible { true };
    int uiKeyboardHeight { 0 };            // on-screen keyboard height set by its handle (0 = automatic)
    int uiEditorWidth { 0 };               // desktop: the window's width (0 = not read from the settings yet)
    float uiKeyZoom { 0.0f };              // the on-screen keys' width zoom (0 = not read from the settings yet)
    int uiKeyPinch { -1 };                 // two fingers zoom the keys (1) or play (0); -1 = not read yet
    bool uiRegionSelect { false };         // the top bar's Region Select
    juce::StringArray uiExpandedHeaders;   // sidebar categories left open

    // set by generatePatch: the next editor to open zooms to fit the whole
    // patch (a generated patch never fits the window at 100 %)
    bool uiFitOnOpen { false };

    // Preset Generation's "With Mutation" toggle: every generated patch is
    // then mutated (Mutator, 50 %, five times). A session setting of the
    // editor, not part of the patch. Message thread only.
    bool generateWithMutation { false };

    // a setting that changes the wiring (a Reroute module's bus) moved: the
    // graph is put together again (message thread)
    void routingChanged() { rebuildGraph(); }

    // Play All / Stop All on a Piano Roll card (message thread): every Piano
    // Roll of the patch goes back to its start and plays - or stops there -
    // all on the same sample (the command reaches them in one audio block)
    void playAllPianoRolls (bool play);
    bool anyPianoRollPlaying() const;

    // Undo / Redo of the whole patch: 32 steps back and 32 forward, kept in
    // memory only (an exported patch is the patch as it is now). An edit
    // made after an undo drops the steps that could be redone - no branches.
    // Message thread. historyTick() is called a few times a second by the
    // editor: a change to the patch (modules, cables, knobs, positions, the
    // cards' own content) becomes one step once the mouse is up again.
    static constexpr int kHistorySteps = 32;
    void historyTick();
    bool canUndoPatch() const { return ! undoSteps.empty(); }
    bool canRedoPatch() const { return ! redoSteps.empty(); }
    bool undoPatch();
    bool redoPatch();

    // the Mutator's amount (0..1; 25 % to start), used by its Mutate and by
    // every card's own Randomize. UI state, not saved.
    float mutationAmount { 0.25f };

    // Each card's Randomize / Undo pair (in its top row): the knob values a
    // card had before its last Randomize, until Undo puts them back. A card
    // with an entry here can only Undo; one without can only Randomize, so
    // every change stays one step that can be taken back. UI state, not
    // saved; dropped with the module and on every new patch.
    std::map<int, std::map<juce::String, float>> moduleRandomUndo;

private:
    // Audio Out's Soft Clip, on the sum of every Audio Out (audio thread)
    AudioOutModule::Clipper outputClipper;

    // Play All / Stop All: a count that goes up with every press, and what it
    // asked for (read once per block into HostTransport)
    std::atomic<juce::uint32> rollCommandSerial { 0 };

    struct HistoryStep
    {
        std::unique_ptr<juce::XmlElement> xml;
        std::map<int, std::pair<std::shared_ptr<const juce::AudioBuffer<float>>, double>> audio;   // by module id
        juce::String name, print;
    };
    HistoryStep captureStep();
    void commitHistory (bool evenWithMouseDown);
    void restoreStep (const HistoryStep&);
    juce::String patchFingerprint() const;
    std::vector<HistoryStep> undoSteps, redoSteps;
    HistoryStep currentStep;
    bool historyStarted { false }, skipAudioInXml { false };
    std::atomic<bool> rollCommandPlay { false };
    void attachModuleHooks (ModuleInstance& inst);   // a new instance's links back to the patch

    //=== compiled process graph (published to the audio thread) ==============
    enum class EdgeConversion { Direct, AudioToMod, ModToAudio, ModToMod };

    struct CompiledEdge
    {
        int srcNode { 0 };          // index into ProcessGraph::nodes (topological order)
        int srcSock { 0 };
        int dstSock { 0 };
        bool feedback { false };    // DFS back-edge -> read previous sample's value
        EdgeConversion conversion { EdgeConversion::Direct };
        // audio into audio from a module with an output trim (makeOutputTrim):
        // the signal is heard at the trimmed level, while the same output into
        // a modulation input (FM In) or onto a knob keeps its full range
        const aquanode::SynthModule* trimSrc { nullptr };
    };

    struct CompiledParamEdge
    {
        int srcNode { 0 };
        int srcSock { 0 };
        int paramIdx { 0 };
        float scale { 0.0f };       // depth * (param max - min)
        bool feedback { false };

        // absolute cables (Curve CV): the source's 0..1 is the knob's whole
        // range, blended with the knob's own setting by |depth|
        bool absolute { false };
        float depth { 0.0f };
        float minValue { 0.0f }, maxValue { 1.0f };
        const aquanode::SynthModule* absoluteSource { nullptr };   // asked whether it drives yet

        // the cable's Bipolar switch against how its source runs: the
        // source's value x becomes x * polMul + polAdd (2x - 1 centres a 0..1
        // source, x / 2 + 1/2 lifts a -1..1 one; otherwise as it is)
        float polMul { 1.0f }, polAdd { 0.0f };
    };

    // a MIDI note driver (Arp, Always Midi, Piano Roll) whose notes reach
    // something; ticked once per sample so it can emit notes on its own clock
    struct MidiDriverRef
    {
        int instanceId { -1 };
        aquanode::SynthModule* dsp { nullptr };
    };

    // How one node takes part in MIDI routing, frozen into the compiled graph
    // so the audio thread never reads the instances' routing fields while the
    // message thread is rebuilding them.
    struct NodeMidiRouting
    {
        std::vector<int> sources;       // instance ids patched into its Midi In
        bool replaces { false };        // a source that takes the played keys away
        bool gated { false };           // silent on voices it got no note-on for
        bool hitsInGlobalLane { false };// global-lane drum: notes arrive as hits
        bool raw { false };             // wants the MIDI itself (hosted plugin)
        bool followsChain { false };    // no Midi In, in a chain played only by streams:
                                        // hears those (in sources), not the keys

        bool listensTo (int id) const
        {
            return std::find (sources.begin(), sources.end(), id) != sources.end();
        }
    };

    // One MIDI stream: everything a module with a Midi Out sends on. Its notes
    // start voices on the sound-making modules listening to it directly
    // (hasConsumers), and are handed to every MIDI processor listening to it,
    // which in turn send on streams of their own.
    struct MidiStream
    {
        int sourceId { -1 };
        bool hasConsumers { false };
        std::vector<int> processors;    // node indices of listening MIDI processors
        std::vector<int> rawListeners;  // node indices of raw-MIDI modules listening
    };

    struct ProcessGraph
    {
        std::vector<std::shared_ptr<ModuleInstance>> nodes;      // topological order
        std::vector<NodeMidiRouting> midi;                       // per node
        std::vector<MidiStream> streams;
        std::vector<int> keyboardProcessors;    // processors fed by the played keys (empty Midi In)
        std::vector<int> rawNodes;              // every raw-MIDI module (hosted plugins)
        std::vector<int> keyboardRawNodes;      // raw-MIDI modules hearing the played keys

        const MidiStream* findStream (int sourceId) const
        {
            for (const auto& st : streams)
                if (st.sourceId == sourceId)
                    return &st;
            return nullptr;
        }

        // does this source start voices on anything that makes sound?
        bool feeds (int sourceId) const
        {
            const auto* st = findStream (sourceId);
            return st != nullptr && st->hasConsumers;
        }

        // per node, frozen at compile time: does a feedback cable read its previous
        // sample (only then is its output kept a sample), does it take the host's
        // input, does it give the host's output
        std::vector<char> feedbackSource, hostIn, hostOut;

        std::vector<MidiDriverRef> midiDrivers;
        std::vector<const aquanode::SynthModule*> voiceHolders;   // canHoldVoicesOpen() modules
        std::vector<std::vector<CompiledEdge>> incoming;         // per node
        std::vector<std::vector<CompiledParamEdge>> incomingParams;
        juce::uint64 serial { 0 };
    };

    void rebuildGraph();
    void purgeRetired();
    void clearPatch();
    static void initInstanceBuffers (ModuleInstance& inst);

    //=== global voice manager (audio thread only; whole-patch voices)
    struct VoiceState
    {
        int note { -1 };            // the pitch this voice sounds
        int midiSource { -1 };      // the stream that started it, -1 = the played keys
        bool active { false };
        bool held { false };
        double countdown { 0.0 };       // seconds of tail left after note-off
        juce::uint64 order { 0 };
    };

    // MIDI routing (audio thread). A note event travels from its source
    // through every processor chain it is patched into; wherever it meets
    // sound-making modules it starts or releases a voice.
    static constexpr int kMaxMidiChainDepth = 16;
    void keyboardEvent (const ProcessGraph& graph, const aquanode::MidiDriverEvent& e);
    void handleControlMessage (const ProcessGraph& graph, const juce::MidiMessage& msg);

    // non-note MIDI (bend, wheels, CCs, sustain), audio thread only
    aquanode::MidiControlState midiState;

    // MPE: which channel each voice's note came on (0: none), and each
    // member channel's latest bend / Slide / pressure (sent before or after
    // its note-on). Audio thread.
    int mpeChannelOfVoice[aquanode::kMaxVoices] {};
    float mpeChanBend[17] {}, mpeChanSlide[17] {}, mpeChanPressure[17] {};
    void mpeMessage (const ProcessGraph& graph, const juce::MidiMessage& msg);
    juce::SpinLock uiMidiLock;
    juce::MidiBuffer uiMidi;   // MIDI from the editor (the MPE keyboard), merged in at the next block
    bool sustainedKeys[128] {};     // released while the pedal was down
    void emitMidi (const ProcessGraph& graph, int sourceId, const aquanode::MidiDriverEvent& e, int depth);
    void feedProcessor (const ProcessGraph& graph, int nodeIndex, const aquanode::MidiDriverEvent& e, int depth);

    void refreshActiveVoices();
    void startVoice (const ProcessGraph& graph, int note, int midiSource, float velocity01);
    void releaseVoices (const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int midiSource, int note);
    void engineAllNotesOff (const ProcessGraph& graph);
    void deactivateVoice (const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int v);
    double computeVoiceTail (const std::vector<std::shared_ptr<ModuleInstance>>& nodes) const;

    VoiceState voiceStates[aquanode::kMaxVoices];
    juce::uint64 voiceOrderCounter { 0 };
    int activeVoiceList[aquanode::kMaxVoices] {};
    int numActiveVoices { 0 };

    std::unique_ptr<juce::XmlElement> buildPatchXml (bool embedSamplesAsBase64,
                                                     juce::ZipFile::Builder* zipBuilder);
    bool importFromZipObject (juce::ZipFile& zip);   // shared by File + stream import
    // patchFiles: a file of the patch (samples/...) by its path in it - from
    // the .aqua, or beside a patch.xml opened on its own; nullptr for a state
    // that keeps everything inside it
    void applyPatchXml (const juce::XmlElement& xml,
                        const std::function<bool (const juce::String& path, juce::MemoryBlock& bytes)>& patchFiles);

    static bool writeSampleAsWav (const juce::AudioBuffer<float>& buffer, double rate, juce::MemoryBlock& dest,
                                  juce::String* formatName = nullptr);
    static std::shared_ptr<juce::AudioBuffer<float>> readWavFromMemory (const void* data, size_t size, double& rate);

    //=== state ================================================================
    std::vector<std::shared_ptr<ModuleInstance>> instances;      // message thread owns
    std::vector<CableInfo> cables;
    std::vector<ParamCableInfo> paramCables;

    // twelve host-automatable modulation slots, claimed by DAW Mod modules
    std::array<juce::AudioParameterFloat*, 12> dawModParams { };
    std::array<float, 12> dawModSnapshot { };   // audio thread: one read per block
    juce::uint64 audioLastGraphSerial { 0 };   // audio thread only: param-mod zeroing on graph swap
    int nextInstanceId { 1 };

    juce::SpinLock graphLock;
    std::shared_ptr<const ProcessGraph> renderGraph;             // audio thread copies under lock
    juce::uint64 graphSerial { 0 };
    std::atomic<juce::uint64> audioAdoptedSerial { 0 };

    // retired objects kept alive until the audio thread has adopted a newer graph
    std::vector<std::pair<juce::uint64, std::shared_ptr<void>>> graveyard;

    juce::AudioBuffer<float> inputScratch;
    double currentSampleRate { 44100.0 };
    bool isPrepared { false };
    std::atomic<bool> resetAllPending { false };   // set on module delete / lane change -> audio thread wipes all voice/DSP state
    std::map<int, bool> lastResolvedLanes;         // instance id -> perVoice, for lane-change detection

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquanodeModularAudioProcessor)
};
