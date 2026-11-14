// SpectralCompress, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/spectralcompress) inside namespace
// aqp_spectralcompress (see PluginPrelude.h), and its GUI module.
#include "PluginSpectralCompressConfig.h"

namespace aqp_spectralcompress
{
    #include "../../PluginSources/spectralcompress/PluginProcessor.cpp"
    #include "../../PluginSources/spectralcompress/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (SpectralCompressGuiModule, "spectralcompress", aqp_spectralcompress)
