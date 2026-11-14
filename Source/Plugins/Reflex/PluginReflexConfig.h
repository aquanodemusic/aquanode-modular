#pragma once
// Reflex inside Aquanode Modular: the context its sources are compiled in - by
// PluginReflex.cpp (the whole plugin, for its GUI version) and by the patch
// version's modules, which include its headers the same way:
//     #include "PluginReflexConfig.h"
//     namespace aqp_reflex { #include AQP_REFLEX_SRC(PluginProcessor.h) }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Reflex"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to src/Plugins
#define AQP_REFLEX_SRC(file) AQP_STRINGIFY(../../PluginSources/reflex/file)
