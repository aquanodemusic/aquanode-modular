// Vita, compiled into Aquanode Modular: the synth's sources (Aquanode Modular's own copy, in
// Source/PluginSources/vita/Source) inside namespace aqp_vita (see PluginPrelude.h), and its GUI module.
// Vita is the GPL Vital synthesizer by Matt Tytel without Vital's name, logo and sign-in.
#include "PluginVitaConfig.h"
#include "../../PluginSources/vita/VitaBinaryData.h"   // namespace aqp_vita::BinaryData

namespace aqp_vita
{
    #include "../../PluginSources/vita/Source/common.cpp"
    #include "../../PluginSources/vita/Source/synthesis.cpp"
    #include "../../PluginSources/vita/Source/interface_look_and_feel.cpp"
    #include "../../PluginSources/vita/Source/interface_editor_components.cpp"
    #include "../../PluginSources/vita/Source/interface_editor_sections.cpp"
    #include "../../PluginSources/vita/Source/interface_editor_sections2.cpp"
    #include "../../PluginSources/vita/Source/interface_wavetable.cpp"
    #include "../../PluginSources/vita/Source/plugin.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (VitaGuiModule, "vita", aqp_vita)

// the patch version: Vita's voice as modules (after the sources above, whose parameter tables its
// descriptors read while the program starts)
#include "PluginVitaPatch.h"
