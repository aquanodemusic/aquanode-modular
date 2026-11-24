// SpectralCompare, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/spectralcompare) inside namespace
// aqp_spectralcompare (see PluginPrelude.h), and its GUI module.
#include "PluginSpectralCompareConfig.h"

namespace aqp_spectralcompare
{
    #include "../../PluginSources/spectralcompare/PluginProcessor.cpp"
    #include "../../PluginSources/spectralcompare/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (SpectralCompareGuiModule, "spectralcompare", aqp_spectralcompare)

//==============================================================================
// (6.9.16) the patch version: its spectral morph, one module (Host/WholePluginPatch.h) -
// Morph and Clarity with their smoothing; the compare view, the gate and the enhancer stay
// with the plugin card
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& SpectralCompareWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "spectralcompare";
            x.kind = "Morph";
            x.description = "SpectralCompare's spectral morph as one module: the spectrum of Audio In blended toward the one on "
                            "Side In - Morph how far, Clarity how cleanly - each with its smoothing (how fast the morph "
                            "follows, and how smoothly the audio moves). The compare view, the gate and the enhancer stay "
                            "with the plugin card.";
            x.section = aquanode::ModuleSection::Utility;
            x.midi = false;
            x.audioIn = true;
            x.sideIn = true;
            static const juce::StringArray shown { "morph", "clarity", "smoothMorph", "smoothMorphAudio" };
            x.hide = [] (const juce::String& id) { return ! shown.contains (id); };
            return x;
        }();
        return def;
    }

    bool SpectralCompareWholeModuleLoad (juce::AudioProcessor&, const juce::File&) { return false; }
}

AQP_WHOLE_PLUGIN_PATCH (SpectralCompareWholeModule, SpectralCompareWholeModuleDef, SpectralCompareWholeModuleLoad)
