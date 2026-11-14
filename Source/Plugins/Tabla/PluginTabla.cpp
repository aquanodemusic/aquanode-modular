// Tabla, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/tabla) inside namespace aqp_tabla
// (see PluginPrelude.h), and its GUI module.
#include "PluginTablaConfig.h"

namespace aqp_tabla
{
    #include "../../PluginSources/tabla/PluginProcessor.cpp"
    #include "../../PluginSources/tabla/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (TablaGuiModule, "tabla", aqp_tabla)
