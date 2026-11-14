// EcoSee, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/ecosee) inside namespace
// aqp_ecosee (see PluginPrelude.h), and its GUI module.
#include "PluginEcoSeeConfig.h"

namespace aqp_ecosee
{
    #include "../../PluginSources/ecosee/PluginProcessor.cpp"
    #include "../../PluginSources/ecosee/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (EcoSeeGuiModule, "ecosee", aqp_ecosee)
