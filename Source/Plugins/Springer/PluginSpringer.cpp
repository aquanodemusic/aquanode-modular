// Springer, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/springer) inside namespace
// aqp_springer (see PluginPrelude.h), and its GUI module.
#include "PluginSpringerConfig.h"

namespace aqp_springer
{
    #include "../../PluginSources/springer/PluginProcessor.cpp"
    #include "../../PluginSources/springer/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (SpringerGuiModule, "springer", aqp_springer)
