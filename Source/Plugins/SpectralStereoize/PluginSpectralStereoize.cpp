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
