// AM-20, compiled into Aquanode Modular: an MS-20-style polysynth modelled
// from measurements (Source/PluginSources/am20 - the engine is JUCE-free)
// inside namespace aqp_am20 (see PluginPrelude.h), and its GUI module.
#include "PluginAM20Config.h"

namespace aqp_am20
{
    #include "../../PluginSources/am20/PluginProcessor.cpp"
    #include "../../PluginSources/am20/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (AM20GuiModule, "am20", aqp_am20)
