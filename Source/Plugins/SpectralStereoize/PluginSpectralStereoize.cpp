// SpectralStereoize, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/spectralstereoize) inside namespace
// aqp_spectralstereoize (see PluginPrelude.h), and its GUI module.
#include "PluginSpectralStereoizeConfig.h"

namespace aqp_spectralstereoize
{
    #include "../../PluginSources/spectralstereoize/PluginProcessor.cpp"
    #include "../../PluginSources/spectralstereoize/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (SpectralStereoizeGuiModule, "spectralstereoize", aqp_spectralstereoize)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& SpectralStereoizeWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "spectralstereoize";
            x.kind = "Width";
            x.description = "SpectralStereoize whole, as one module: mid / side width per FFT bin - draw on its panel how wide each frequency should be, from mono to extra wide, over the butterfly plot of the output and of Side In as a reference. Bit for bit transparent with the curve flat. The drawn curve is saved with the patch.";
            x.section = aquanode::ModuleSection::Effect;
            x.midi = false;
            x.audioIn = true;
            x.sideIn = true;
            x.showPanel = true;
            x.panelWidth = 960; x.panelHeight = 540; x.cardWidth = 600;
            return x;
        }();
        return def;
    }

    bool SpectralStereoizeWholeModuleLoad (juce::AudioProcessor&, const juce::File&) { return false; }
}

AQP_WHOLE_PLUGIN_PATCH (SpectralStereoizeWholeModule, SpectralStereoizeWholeModuleDef, SpectralStereoizeWholeModuleLoad)
