// AudioStretcher, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/audiostretcher) inside namespace
// aqp_audiostretcher (see PluginPrelude.h), and its GUI module. It works on a
// loaded sound offline - pitch and speed changed by the Rubber Band Library
// in the background while the card shows how far it is - and plays the result
// from its Audio Out. The sound is saved with the patch.
#include "PluginAudioStretcherConfig.h"

namespace aqp_audiostretcher
{
    using namespace ::RubberBand;
    #include "../../PluginSources/audiostretcher/PluginProcessor.cpp"
    #include "../../PluginSources/audiostretcher/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

namespace
{
    using AS = aqp_audiostretcher::AudioStretcherAudioProcessor;

    std::shared_ptr<const juce::AudioBuffer<float>> soundOf (juce::AudioProcessor& p, double& rate)
    {
        auto* a = dynamic_cast<AS*> (&p);
        if (a == nullptr || ! a->hasAudioLoaded())
            return nullptr;
        rate = a->getLoadedSampleRate();
        // a view of the plugin's own buffer (saving happens there and then)
        return std::shared_ptr<const juce::AudioBuffer<float>> (&a->getLoadedAudio(), [] (const juce::AudioBuffer<float>*) {});
    }

    void setSound (juce::AudioProcessor& p, std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
    {
        if (auto* a = dynamic_cast<AS*> (&p); a != nullptr && audio != nullptr)
            a->setLoadedAudio (std::move (*audio), rate);
    }

    const bool audioRegistered = aquanode::plugins::registerAudioAccess ("audiostretcher", { soundOf, setSound });
}

AQP_DEFINE_PLUGIN (AudioStretcherGuiModule, "audiostretcher", aqp_audiostretcher)
