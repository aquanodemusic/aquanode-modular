#pragma once

#include <JuceHeader.h>
#include "../../Modules/Core/ModuleCore.h"

//==============================================================================
// VST3 Plugin - any VST3 plugin installed on the computer, inside the patch.
//
// Desktop only (Windows, and the Linux build used for testing): the module
// exists where the project is built with plugin hosting (JUCE_PLUGINHOST_VST3,
// set in the desktop exporters) and is left out of Android, which cannot load
// desktop plugins.
//
// Load... picks a .vst3; Window opens the plugin's own interface in a window
// of its own (a plugin's native window cannot follow the patch area's zoom,
// so it is not drawn on the card). Audio In goes into the plugin's main input
// (an effect), Midi In - the played keys while nothing is patched there - into
// its MIDI (a synth); its main output comes out of Audio Out. The plugin runs
// in blocks behind the per-sample patch - 32 samples unless its Block Size
// says otherwise (0.7 ms at 48 kHz; a plugin that behaves differently at
// such small blocks can be given the host's own size) - and gets the host's
// (or the patch's) tempo and transport.
//
// The patch keeps which plugin it was and the plugin's whole state. Opened on
// a computer without that plugin, the card says so and keeps the state, so
// saving the patch there loses nothing.
//==============================================================================
#if JUCE_PLUGINHOST_VST3

namespace aquanode::plugins
{
    class ExternalPluginModule : public SynthModule
    {
    public:
        static constexpr int kBlockSize = 32;       // the default
        static constexpr int kMaxBlock = 2048;
        static constexpr int kBlockSizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };
        enum ParamIndex { pBlockSize = 0 };

        ExternalPluginModule();
        ~ExternalPluginModule() override;

        //=== SynthModule ======================================================
        void prepare (double newSampleRate) override;
        void reset() override;
        void blockStart() override;
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        void processSample (const StereoFrame* inputs, StereoFrame* outputs) override;
        // the block the plugin runs behind the patch, plus what the plugin
        // itself reports: the patch passes it on to the host
        int latencySamples() const override { return activeBlock.load (std::memory_order_relaxed) + pluginLatency.load (std::memory_order_relaxed); }
        bool wantsRawMidi() const override { return true; }
        bool retunesByMpeBend() const override { return true; }   // (6.9.10: a patch tuning bends its notes)
        // a VST3 may be a synth or an effect: the keys reach it while its Midi In is empty
        bool playedKeysReachRawMidi (const ModuleDescriptor&) const override { return true; }
        void rawMidiMessage (const juce::MidiMessage& message) override;
        void midiAllNotesOff() override;

        juce::String titleSuffix() const override;
        std::unique_ptr<juce::Component> createExtraContentComponent() override;
        int extraContentHeight() const override { return 54; }
        int preferredModuleWidth() const override { return 300; }

        juce::String saveCustomState() const override;
        void loadCustomState (const juce::String& state) override;

        //=== message thread ===================================================
        void chooseAndLoad();                                   // Load...
        bool loadFile (const juce::File& file, juce::String& error);   // the first plugin in it
        void unload();
        void showWindow (bool show);
        bool isWindowShowing() const { return window != nullptr; }
        bool hasPlugin() const { return current != nullptr; }
        juce::String pluginName() const;
        juce::String statusText() const { return status; }
        int getChangeCounter() const { return changeCounter.load(); }
        juce::AudioPluginInstance* getPlugin() const { return current.get(); }
        juce::Component* getWindowContent() const { return window != nullptr ? window->getContentComponent() : nullptr; }

        static juce::AudioPluginFormatManager& formats();

    private:
        struct PlayHead : public juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override { return info; }
            PositionInfo info;
        };
        class PluginWindow;

        bool instantiate (const juce::PluginDescription& desc, juce::String& error);
        void install (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::PluginDescription& desc);
        void configure (juce::AudioPluginInstance& p);
        void runBlock();
        void updatePlayHead();

        // the plugin: swapped on the message thread under swapLock, which the
        // audio thread only try-locks (a block of silence while a plugin loads)
        std::shared_ptr<juce::AudioPluginInstance> current;
        juce::SpinLock swapLock;
        juce::PluginDescription description;
        juce::String status { "No plugin loaded" };
        juce::String missingState;          // the saved state of a plugin that is not here
        std::unique_ptr<juce::DocumentWindow> window;
        std::atomic<int> changeCounter { 0 };
        std::atomic<int> pluginLatency { 0 };

        PlayHead playHead;
        juce::AudioBuffer<float> io;
        juce::MidiBuffer midiInBuffer;
        std::array<std::array<float, kMaxBlock>, 2> outFifo {};
        // the block the plugin runs in now; Block Size asks for another one,
        // which the message thread then prepares the plugin for
        std::atomic<int> activeBlock { kBlockSize };
        std::atomic<bool> resizePending { false };
        std::shared_ptr<int> alive = std::make_shared<int> (0);
        int wantedBlock() const { return kBlockSizes[juce::jlimit (0, 7, (int) std::lround (param (pBlockSize)))]; }
        void applyBlockSize();      // message thread
        int pos { 0 }, ins { 0 }, outs { 0 };
        int samplesThisHostBlock { 0 };
        juce::int64 sampleCounter { 0 };
        bool prepared { false };

        JUCE_DECLARE_NON_COPYABLE (ExternalPluginModule)
    };
}

#endif
