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
