// Resonate, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/resonate) inside namespace aqp_resonate
// (see PluginPrelude.h), and its GUI module.
#include "PluginResonateConfig.h"

namespace aqp_resonate
{
    #include "../../PluginSources/resonate/PluginProcessor.cpp"
    #include "../../PluginSources/resonate/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (ResonateGuiModule, "resonate", aqp_resonate)
