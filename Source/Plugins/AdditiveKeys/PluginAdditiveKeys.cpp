// Additive Keys, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/additivekeys) inside namespace
// aqp_additivekeys (see PluginPrelude.h), and its GUI module.
#include "PluginAdditiveKeysConfig.h"

namespace aqp_additivekeys
{
    #include "../../PluginSources/additivekeys/PluginProcessor.cpp"
    #include "../../PluginSources/additivekeys/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (AdditiveKeysGuiModule, "additivekeys", aqp_additivekeys)
