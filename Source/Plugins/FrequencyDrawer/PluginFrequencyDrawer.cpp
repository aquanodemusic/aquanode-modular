// Frequency Drawer, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/frequencydrawer) inside namespace
// aqp_frequencydrawer (see PluginPrelude.h), and its GUI module.
#include "PluginFrequencyDrawerConfig.h"

namespace aqp_frequencydrawer
{
    #include "../../PluginSources/frequencydrawer/PluginProcessor.cpp"
    #include "../../PluginSources/frequencydrawer/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (FrequencyDrawerGuiModule, "frequencydrawer", aqp_frequencydrawer)
