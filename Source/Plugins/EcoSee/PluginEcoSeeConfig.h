#pragma once
// EcoSee inside Aquanode Modular: the context its original sources are compiled
// in - by PluginEcoSee.cpp (the whole plugin, for its GUI version) and by any
// other file that includes its headers the same way:
//     #include "PluginEcoSeeConfig.h"
//     namespace aqp_ecosee { #include AQP_ECOSEE_SRC(PluginProcessor.h) }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "EcoSee"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins:
//     #include AQP_ECOSEE_SRC(PluginProcessor.h)
 #define AQP_ECOSEE_SRC(file) AQP_STRINGIFY(../../PluginSources/ecosee/file)
