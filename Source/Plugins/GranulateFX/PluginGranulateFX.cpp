// Granulate FX, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/granulatefx, from aquanode-plugins)
// inside namespace aqp_granulatefx (see PluginPrelude.h), and its GUI module.
#include "PluginGranulateFXConfig.h"

namespace aqp_granulatefx
{
    #include "../../PluginSources/granulatefx/PluginProcessor.cpp"
    #include "../../PluginSources/granulatefx/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (GranulateFXGuiModule, "granulatefx", aqp_granulatefx)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& GranulateFXWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "granulatefx";
            x.kind = "Effect";
            x.description = "Granulate FX whole, as one module: grains taken from the incoming audio, kept in a ring "
                            "buffer whose length you set - number, size, position, spray, window, reversed grains, each "
                            "grain's envelope, amplitude modulation, pitch and its dispersion, stereo spread and volume - "
                            "played by the plugin's own engine.";
            x.section = aquanode::ModuleSection::Effect;
            x.midi = false;
            x.audioIn = true;
            x.sideIn = false;
            return x;
        }();
        return def;
    }
}

AQP_WHOLE_PLUGIN_PATCH (GranulateFXWholeModule, GranulateFXWholeModuleDef, nullptr)
