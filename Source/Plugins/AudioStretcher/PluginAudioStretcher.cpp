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

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& AudioStretcherWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "audiostretcher";
            x.kind = "Player";
            x.description = "AudioStretcher whole, as one module: load a sound on its panel, choose the region, set Pitch, Fine and Speed (and the fades) - here also as knobs that take cables - and Play: the Rubber Band Library renders it in the background and it plays from Audio Out. The sound is saved with the patch.";
            x.section = aquanode::ModuleSection::Oscillator;
            x.midi = false;
            x.audioIn = false;
            x.sideIn = false;
            x.showPanel = true;
            x.panelWidth = 400; x.panelHeight = 690; x.cardWidth = 360;
            return x;
        }();
        return def;
    }

    bool AudioStretcherWholeModuleLoad (juce::AudioProcessor&, const juce::File&) { return false; }
}

AQP_WHOLE_PLUGIN_PATCH (AudioStretcherWholeModule, AudioStretcherWholeModuleDef, AudioStretcherWholeModuleLoad)
