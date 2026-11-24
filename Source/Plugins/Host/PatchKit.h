#pragma once

#include <JuceHeader.h>
#include "PluginCatalog.h"
#include "../../Engine/PluginProcessor.h"

//==============================================================================
// Shared helpers for the patch versions of the merged plugins: reading the
// original plugin's parameters, and placing / wiring / setting modules.
//
// The patch modules name their parameters after the original plugin's own
// parameter ids wherever the meaning is the same, so a builder mostly
// says "copy everything that matches" and wires the modules.
//==============================================================================
namespace aquanode::plugins::kit
{
    //=== the original plugin =================================================
    juce::RangedAudioParameter* findParam (juce::AudioProcessor& original, const juce::String& id);

    // The parameter's real value: a float in its own range, a choice as its
    // index, a bool as 0 / 1. fallback when the plugin has no such parameter.
    float param (juce::AudioProcessor& original, const juce::String& id, float fallback = 0.0f);

    //=== the plugin's look in the patch ======================================
    juce::Colour accentOf (const juce::String& pluginId);

    // "83ChorusVerb - Chorus"
    juce::String moduleName (const juce::String& pluginId, const juce::String& kind);

    // the Off / On switch every plugin effect stage carries
    ParamSpec makeOnOff (juce::String id, juce::String label, bool defaultOn, int row);

    //=== mono legato ==========================================================
    // Aquanode Modular gives every note its own voice. Synths that play mono
    // legato (a note struck while another is held carries that note on) get
    // the same from their per-voice modules this way: each module notes which
    // voice is sounding the line, and a legato note's voice takes over that
    // voice's state (envelope level, phases, filter memory) instead of
    // starting afresh.
    struct LegatoTracker
    {
        int current { -1 };
        int held { 0 };

        // the voice to take the state over from, or -1 for a fresh start
        int noteOn (int v, bool legato)
        {
            const int from = (legato && held > 0 && current >= 0 && current != v) ? current : -1;
            current = v;
            ++held;
            return from;
        }
        void noteOff() { held = juce::jmax (0, held - 1); }
        void reset() { current = -1; held = 0; }
    };

    //=== building =============================================================
    struct Builder
    {
        AquanodeModularAudioProcessor& patch;
        juce::Point<int> origin;

        // a module at origin + offset; -1 if the type is unknown
        int add (const juce::String& typeId, int dx, int dy);

        void set (int module, const juce::String& paramId, float value);

        // Every parameter of the module whose id the original plugin also
        // has (after map, if given: module id -> plugin id) takes the
        // plugin's value. Returns how many were copied.
        int copy (int module, juce::AudioProcessor& original,
                  const std::function<juce::String (const juce::String&)>& map = {});

        bool cable (int from, const juce::String& outSocket, int to, const juce::String& inSocket);
        bool mod (int from, const juce::String& outSocket, int to, const juce::String& paramId, float depth);

        // the custom state a module keeps (drawn curves, wavetables, ...)
        void setCustomState (int module, const juce::String& state);
        SynthModule* dsp (int module) const;
    };

    //=== (6.9.14) section cards =================================================
    // The engine's side: which of its knobs a card holds. set() on the message
    // thread (graph build); the audio thread reads under a try-lock.
    struct CardReceiver
    {
        struct Hit { const SynthModule::CardLink* link { nullptr }; int idx { 0 }; };
        void set (const ModuleDescriptor& d, std::vector<std::shared_ptr<SynthModule::CardLink>> links);
        // the knob's value from its card (base: as set), mirroring the card's
        // setting into the engine's own knob; false: no card holds it
        bool read (SynthModule& engine, int knob, bool base, float& value) const;

        juce::SpinLock lock;
        std::vector<std::shared_ptr<SynthModule::CardLink>> held;
        std::vector<Hit> byKnob;
    };

    // A section card: some of an engine's knobs (the same ids), on a smaller card.
    // Link In / Link Out chain the cards; the last goes into the engine's Cards In.
    struct CardDef
    {
        juce::String pluginId, id, kind, description;
        std::vector<juce::String> ids;       // its knobs, in order ("" starts a new row)
        // the knobs' specs, by id (a fresh spec, any row: the card numbers the rows)
        std::function<ParamSpec (const juce::String& id)> spec;
        // a picture on the card instead of knobs (the knobs then hidden), its height
        std::function<std::unique_ptr<juce::Component> (SynthModule&)> extra;
        int extraHeight { 0 }, width { 0 };
        std::function<bool (const juce::String& id)> drawn;   // the knobs the picture shows (hidden as knobs); none: all
        bool isDrawn (const juce::String& id) const { return extra != nullptr && (drawn == nullptr || drawn (id)); }
        juce::String typeId() const { return "plugin." + pluginId + "." + id; }
        std::vector<juce::String> knobIds() const;   // ids without the row breaks
        int rows() const;
    };
    ModuleDescriptor cardBaseDescriptor (const CardDef& def);
    void completeCardDescriptor (const CardDef& def, ModuleDescriptor& d);
    // registers the card's module (def must live for the program: a static)
    bool registerCard (const CardDef& def);

    class CardModule : public SynthModule
    {
    public:
        explicit CardModule (const CardDef& d);
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        void prepare (double sr) override;
        void processSample (const StereoFrame* in, StereoFrame* out) override;
        void setParameter (const juce::String& id, float value) override;
        std::shared_ptr<CardLink> cardLink() const override { return link; }
        std::unique_ptr<juce::Component> createExtraContentComponent() override { return def.extra ? def.extra (*this) : nullptr; }
        int extraContentHeight() const override { return def.extra ? def.extraHeight : 0; }
        int preferredModuleWidth() const override { return def.width; }
    private:
        void write();
        const CardDef& def;
        std::shared_ptr<CardLink> link;
        std::vector<int> index;
        int counter { 0 };
    };

    // where the builder puts a row of cards: x, y of each, columns of cards stacked
    // (heights from their rows), left to right, starting at (x0, y0)
    std::vector<juce::Point<int>> cardPlaces (const std::vector<const CardDef*>& cards, int x0, int y0, int columns);

    //=== a module that runs the original plugin itself ======================
    // The patch module for a plugin (or one stage of it) that is best left to
    // its own engine: a whole instance of the original plugin - the same code
    // its GUI card runs - played in blocks of kBlock samples, so its output
    // is kBlock samples late (as the GUI card's). The module's knobs drive the
    // plugin's parameters of the same ids (pluginParamFor maps them otherwise)
    // and take cables like any knob; Midi In (when the descriptor has one)
    // reaches it sample-accurately inside its block, Audio In feeds its input
    // and Audio Out is its output. It sees the patch's tempo as a play head,
    // and its whole state is saved with the patch.
    class InstanceModule : public SynthModule
    {
    public:
        static constexpr int kBlock = 32;

        InstanceModule (const juce::String& pluginId, bool takesMidi);
        ~InstanceModule() override;

        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        bool wantsRawMidi() const override { return midi; }
        void rawMidiMessage (const juce::MidiMessage& m) override;
        void midiAllNotesOff() override;

        void prepare (double newSampleRate) override;
        void reset() override;
        void processSample (const StereoFrame* inputs, StereoFrame* outputs) override;

        juce::String saveCustomState() const override;
        void loadCustomState (const juce::String& state) override;

        juce::AudioProcessor* instance() const { return plugin.get(); }

        // (6.9.14) section cards into Cards In (a descriptor with that socket)
        bool takesCardLinks() const override { return getDescriptor().inputIndexOf ("cardsIn") >= 0; }
        void setCardLinks (std::vector<std::shared_ptr<CardLink>> l) override { cards.set (getDescriptor(), std::move (l)); }

        // the plugin's state in one go (a builder copying a GUI card's or a
        // preset's whole sound in); the knobs then send theirs again
        void copyStateFrom (juce::AudioProcessor& source);

    protected:
        // the plugin parameter a knob drives; empty = none
        virtual juce::String pluginParamFor (const juce::String& knobId) const { return knobId; }
        // the plugin's block (default: its processBlock); runs under the
        // plugin's callback lock, as do pushKnobs and beforeBlock
        virtual void render (juce::AudioBuffer<float>& io, juce::MidiBuffer& midiIn);
        // after the knobs went in, before the block runs (audio thread)
        virtual void beforeBlock() {}
        // the module's other outputs at this sample (mod outs ...)
        virtual void extraOutputs (StereoFrame* outputs, int sampleInBlock) { juce::ignoreUnused (outputs, sampleInBlock); }
        // right after the plugin's state came back from a patch
        virtual void stateRestored() {}

        // knobs into the plugin's parameters: changed ones, or all of them
        void pushKnobs (bool all);
        // pluginParamFor's answers changed
        void relink();

        std::unique_ptr<juce::AudioProcessor> plugin;

    private:
        struct PlayHead : public juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override { return info; }
            PositionInfo info;
        };
        struct Link { juce::RangedAudioParameter* param; int knob; float last; float raw = std::numeric_limits<float>::quiet_NaN(); };

        void runBlock();

        const bool midi;
        CardReceiver cards;
        std::vector<Link> links;
        juce::AudioBuffer<float> io;
        juce::MidiBuffer midiQueue;
        std::array<std::array<float, kBlock>, 2> inFifo {}, outFifo {};
        int pos { 0 }, inAudio { -1 }, outAudio { -1 };
        juce::int64 sampleCounter { 0 };
        PlayHead playHead;
    };
}
