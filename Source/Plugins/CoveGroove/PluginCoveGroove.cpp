// Cove Groove, compiled into Aquanode Modular: the hardgroove loop generator's
// sources (Source/PluginSources/covegroove, written for Aquanode Modular, in
// namespace covegroove) and its GUI module. PluginPrelude.h comes first so the
// Android file picker shim applies to its Save / Load as to every plugin's.
#include "../Host/PluginPrelude.h"

#include "../../PluginSources/covegroove/PluginProcessor.cpp"
#include "../../PluginSources/covegroove/PluginEditor.cpp"

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (CoveGrooveGuiModule, "covegroove", aqp_covegroove)

