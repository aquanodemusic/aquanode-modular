// The processor: construction, prepare / release, the bus layout, the editor.
// Its other parts: ProcessorAudio.cpp (processBlock), ProcessorVoices.cpp (voices
// and MIDI), ProcessorPatch.cpp (modules and cables), ProcessorGraph.cpp (the
// compiled graph), ProcessorState.cpp (saving and loading), ProcessorHistory.cpp
// (undo / redo).
#include "PluginProcessor.h"
#include "../Editor/PluginEditor.h"
#include "../Modules/Core/ModuleRegistry.h"
#include "../Modules/Utilities/MIDI/MidiAddModule.h"
#include "../Modules/Utilities/MIDI/DiscardMidiModule.h"
#include "../Modules/Utilities/Control/DawModModule.h"
#include "../Modules/Utilities/MIDI/PianoRollModule.h"
#include "PatchStateFiles.h"
#include "../Plugins/Host/HostedPlugin.h"

#include <algorithm>

using namespace aquanode;

//==============================================================================
AquanodeModularAudioProcessor::AquanodeModularAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    aquanode::forceLinkAllModules();   // guarantees every module is linked + registered
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && mm->isThisTheMessageThread())
        latencyWatch = std::make_unique<LatencyWatch> (*this);

    // Fixed, always-present automation targets. They exist whether or not any
    // DAW Mod module is patched, so a host's automation lanes never shift
    // around as the patch changes.
    for (int i = 0; i < (int) dawModParams.size(); ++i)
    {
        auto* p = new juce::AudioParameterFloat (
            juce::ParameterID { "dawmod" + juce::String (i + 1), 1 },
            "DAW Mod " + juce::String (i + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
        dawModParams[(size_t) i] = p;
        addParameter (p);
    }

    rebuildGraph();   // publish an (empty) graph so processBlock is always valid

    // Android: JUCE's empty permission / picker dialog kept invisible (StandaloneInput.cpp)
    if (wrapperType == wrapperType_Standalone)
    {
        extern void aquanodeHideAndroidOverlays();
        aquanodeHideAndroidOverlays();
    }

    // MPE: as it was last switched (the app's settings)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Aquanode Modular";
        o.filenameSuffix = ".settings";
        o.folderName = "Aquanode Modular";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile file (o);
        mpeEnabled.store (file.getBoolValue ("mpe.enabled", false));
        // the plugins' block (the top bar's Plugin Block)
        aquanode::plugins::HostedPluginModule::globalBlockSize().store (juce::jlimit (16, 512, file.getIntValue ("plugins.block", 128)));
    }

    // MTS-ESP: the client registers here, on the message thread; the audio
    // thread only asks once per block whether a master is there
    aquanode::tuning::refresh();
}

AquanodeModularAudioProcessor::~AquanodeModularAudioProcessor() = default;

//==============================================================================
bool aquanodeDeviceRestartKeepsState();   // (StandaloneInput.cpp)

void AquanodeModularAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // the standalone app opening or closing the microphone restarts the device
    // at the same rate: nothing in the patch needs preparing again (tails ring
    // on, held notes stay held)
    if (aquanodeDeviceRestartKeepsState() && isPrepared && sampleRate == currentSampleRate)
    {
        if (inputScratch.getNumSamples() < samplesPerBlock)
            inputScratch.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);
        return;
    }

    currentSampleRate = sampleRate;
    isPrepared = true;
    inputScratch.setSize (2, juce::jmax (1, samplesPerBlock));

    for (auto& inst : instances)
        inst->dsp->prepare (sampleRate);
    outputClipper.reset();
    latencyScratch.reserve (1024);
    latencyInScratch.reserve (1024);

    for (auto& vs : voiceStates)
        vs = VoiceState();
    refreshActiveVoices();

    // the patch's latency (a graph built off the message thread could not report it yet)
    if (getLatencySamples() != patchLatency.load (std::memory_order_relaxed))
        setLatencySamples (patchLatency.load (std::memory_order_relaxed));

    purgeRetired();
}

void AquanodeModularAudioProcessor::releaseResources()
{
}

bool AquanodeModularAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::disabled();
}

//==============================================================================
juce::AudioProcessorEditor* AquanodeModularAudioProcessor::createEditor()
{
    return new AquanodeModularAudioProcessorEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AquanodeModularAudioProcessor();
}
