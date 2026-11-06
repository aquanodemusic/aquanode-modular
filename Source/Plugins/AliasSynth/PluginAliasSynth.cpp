// AliasSynth, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/aliassynth) inside namespace aqp_aliassynth
// (see PluginPrelude.h), and its GUI module.
#include "PluginAliasSynthConfig.h"

namespace aqp_aliassynth
{
    #include "../../PluginSources/aliassynth/PluginProcessor.cpp"
    #include "../../PluginSources/aliassynth/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (AliasSynthGuiModule, "aliassynth", aqp_aliassynth)
