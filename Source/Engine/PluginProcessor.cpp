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

#include <algorithm>

using namespace aquanode;

//==============================================================================
AquanodeModularAudioProcessor::AquanodeModularAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    aquanode::forceLinkAllModules();   // guarantees every module is linked + registered

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
    }

    // MTS-ESP: the client registers here, on the message thread; the audio
    // thread only asks once per block whether a master is there
    aquanode::tuning::refresh();
}

AquanodeModularAudioProcessor::~AquanodeModularAudioProcessor() = default;

//==============================================================================
void AquanodeModularAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    isPrepared = true;
    inputScratch.setSize (2, juce::jmax (1, samplesPerBlock));

    for (auto& inst : instances)
        inst->dsp->prepare (sampleRate);
    outputClipper.reset();

    for (auto& vs : voiceStates)
        vs = VoiceState();
    refreshActiveVoices();

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
