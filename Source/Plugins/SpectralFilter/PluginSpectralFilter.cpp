// Spectral Filter, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/spectralfilter) inside namespace
// aqp_spectralfilter (see PluginPrelude.h), its GUI module and its patch version.
#include "PluginSpectralFilterConfig.h"

namespace aqp_spectralfilter
{
    #include "../../PluginSources/spectralfilter/PluginProcessor.cpp"
    #include "../../PluginSources/spectralfilter/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (SpectralFilterGuiModule, "spectralfilter", aqp_spectralfilter)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& SpectralFilterWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "spectralfilter";
            x.kind = "Curves";
            x.description = "SpectralFilter whole, as one module: draw on its panel per FFT bin how loud each frequency is "
                            "(+-24 dB), how its phase turns, where it is shifted to and where it is panned - with Shift and Auto "
                            "scrolling each curve, Strength scaling it and the Frequency Window limiting where it acts. Its "
                            "knobs are also here and take cables. The curves are saved with the patch.";
            x.section = aquanode::ModuleSection::Effect;
            x.midi = false;
            x.audioIn = true;
            x.showPanel = true;
            x.panelWidth = 860; x.panelHeight = 700; x.cardWidth = 560;
            return x;
        }();
        return def;
    }

    bool SpectralFilterWholeModuleLoad (juce::AudioProcessor&, const juce::File&) { return false; }
}

AQP_WHOLE_PLUGIN_PATCH (SpectralFilterWholeModule, SpectralFilterWholeModuleDef, SpectralFilterWholeModuleLoad)
