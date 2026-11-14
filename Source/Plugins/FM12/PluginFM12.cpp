// FM12, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/fm12) inside namespace
// aqp_fm12 (see PluginPrelude.h), and its GUI module.
#include "PluginFM12Config.h"

namespace aqp_fm12
{
    #include "../../PluginSources/fm12/PluginProcessor.cpp"
    #include "../../PluginSources/fm12/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (FM12GuiModule, "fm12", aqp_fm12)
