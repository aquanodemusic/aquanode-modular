#pragma once
//==============================================================================
// TR-808, TR-909 and Drum Pads: 4 x 4 pad drum modules.
//
//   TR-808 / TR-909  the machines' voices, physically modelled and fitted to
//                    recordings of the machines (Shared/PhysDrumSynth.h). Each
//                    pad plays one of the machine's sounds; the 808's own knobs
//                    (Tone, Decay, Snappy, Tuning) move between the fitted
//                    recordings of every setting.
//   Drum Pads        sixteen pads for sounds you load (wav, aiff, flac, ogg, mp3).
//
// Above the pads, the tweaker: a menu for the pad being edited, its sound,
// and its knobs - Level, Pan, Tune, Decay, Tone and three knobs that move the
// model itself (Sweep, Snappy, Ring ...). Tap a pad to hear and edit it; MIDI
// notes from the Base Note up (C1 = 36 by default) play pads 1..16.
//
// Every sound is rendered once on a background thread whenever its knobs
// change (three round-robin variations) and played from that render, so a
// hit costs almost nothing however complex its model.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/PhysDrumPresets.h"

namespace aquanode::drumpads
{
    constexpr int kPads = 16;
    constexpr int kVariants = 3;
    constexpr int kVoices = 32;

    enum class Kit { TR808 = 0, TR909, Samples };

    // one pad's rendered sound
    struct PadSound
    {
        std::vector<float> l[kVariants], r[kVariants];
        int variants { 0 };
        bool stereo { false };
    };

    // a loaded sample (Drum Pads)
    struct PadSample
    {
        std::vector<float> l, r;
        double rate { 44100.0 };
        juce::String name;
    };

    // what a pad's knobs say, as the renderer needs it
    struct PadSpec
    {
        int sound { 0 };
        double level { 0.8 }, pan { 0.0 }, tune { 0.0 }, decay { 100.0 }, tone { 50.0 }, knobA { 0.0 }, knobB { 0.0 };
        double extra[3] { 0.0, 0.0, 0.0 };
        int choke { 0 };
    };

    // the sounds a kit offers in a pad's Sound menu
    struct KitSound { juce::String name, shortName; };
    const std::vector<KitSound>& kitSounds (Kit k);
    juce::String knobLabel (Kit k, int sound, int which);     // the 808's machine knobs ("" = none)
    std::array<juce::String, 3> physicalLabels (Kit k, int sound);

    // shared between the module and its background renders
    struct RenderSlots
    {
        juce::SpinLock lock;
        std::shared_ptr<const PadSound> sound[kPads];
        std::atomic<juce::uint64> wanted[kPads] {};
        std::shared_ptr<const PadSample> sample[kPads];   // Drum Pads: what each pad holds
        std::atomic<bool> alive { true };
    };

    class PadDrumModule : public SynthModule, private juce::Timer
    {
    public:
        explicit PadDrumModule (Kit k);
        ~PadDrumModule() override;

        static ModuleDescriptor makeDescriptor (Kit k);

        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        bool wantsRawMidi() const override { return true; }
        void rawMidiMessage (const juce::MidiMessage& m) override;
        void midiAllNotesOff() override { stopAll = true; }

        void prepare (double sr) override;
        void reset() override;
        void blockStart() override;
        void processSample (const StereoFrame* inputs, StereoFrame* outputs) override;
        double voiceTailSeconds() const override { return 0.0; }

        std::unique_ptr<juce::Component> createExtraContentComponent() override;
        int preferredModuleWidth() const override { return 440; }
        int extraContentHeight() const override { return 468; }

        juce::String saveCustomState() const override;
        void loadCustomState (const juce::String& state) override;

        //=== for the card's UI (message thread) =================================
        Kit kit() const { return kitKind; }
        PadSpec padSpec (int pad) const;
        static juce::String pid (int pad, const char* what) { return "p" + juce::String (pad) + what; }
        void triggerFromUi (int pad, float velocity);
        float padFlash (int pad) const { return flash[(size_t) pad].load (std::memory_order_relaxed); }
        void decayFlashes() { for (auto& f : flash) f.store (f.load (std::memory_order_relaxed) * 0.8f, std::memory_order_relaxed); }
        std::shared_ptr<const PadSound> currentSound (int pad) const;
        std::shared_ptr<const PadSample> currentSample (int pad) const;
        void loadPadSample (int pad, std::shared_ptr<PadSample> s);   // Drum Pads
        void clearPadSample (int pad);
        juce::String padName (int pad) const;
        int selectedPad { 0 };

    private:
        void timerCallback() override;
        void requestRenders (bool force);
        juce::uint64 specKey (int pad) const;
        void trigger (int pad, float velocity);

        Kit kitKind;
        std::shared_ptr<RenderSlots> slots;
        std::atomic<double> renderRate { 0.0 };
        juce::uint64 requested[kPads] {};

        struct Voice
        {
            std::shared_ptr<const PadSound> snd;
            int pad { -1 }, variant { 0 }, pos { 0 }, choke { 0 };
            float gl { 0 }, gr { 0 }, fade { 1 }, fadeStep { 0 };
            bool active { false };
        };
        std::array<Voice, kVoices> voices;
        int nextVariant[kPads] {};
        juce::uint32 nextVoice { 0 };

        // the pads tapped on the card, handed to the audio thread
        juce::AbstractFifo uiFifo { 64 };
        std::array<std::pair<int, float>, 64> uiQueue {};
        std::atomic<bool> stopAll { false };
        std::array<std::atomic<float>, kPads> flash {};

        // sounds a voice let go of: freed on the message thread, never the audio thread
        static constexpr int kRetire = 128;
        std::array<std::shared_ptr<const PadSound>, kRetire> retire;
        std::array<std::atomic<bool>, kRetire> retireFull {};
        void retireSound (std::shared_ptr<const PadSound>&& s);

        int pBase { -1 }, pLevel { -1 };
        int pPadLevel[kPads] {}, pPadPan[kPads] {}, pPadChoke[kPads] {};   // (looked up in prepare: no strings on the audio thread)
    };
}
