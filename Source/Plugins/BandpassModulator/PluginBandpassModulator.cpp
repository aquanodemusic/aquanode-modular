// Bandpass Modulator, compiled into Aquanode Modular: its sources (in
// Source/PluginSources/bandpassmod) inside namespace aqp_bandpassmod, and its
// GUI module.
#include "PluginBandpassModulatorConfig.h"

namespace aqp_bandpassmod
{
    #include "../../PluginSources/bandpassmod/PluginProcessor.cpp"
    #include "../../PluginSources/bandpassmod/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (BandpassModulatorGuiModule, "bandpassmod", aqp_bandpassmod)
