// PrecisionUtility, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/precisionutility) inside namespace aqp_precisionutility
// (see PluginPrelude.h), and its GUI module.
#include "PluginPrecisionUtilityConfig.h"

namespace aqp_precisionutility
{
    #include "../../PluginSources/precisionutility/PluginProcessor.cpp"
    #include "../../PluginSources/precisionutility/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (PrecisionUtilityGuiModule, "precisionutility", aqp_precisionutility)
