#pragma once
// Springer inside Aquanode Modular: the context its original sources are compiled
// in - by PluginSpringer.cpp (the whole plugin, for its GUI version) and by the
// patch version's modules, which include its headers the same way:
//     #include "PluginSpringerConfig.h"
//     namespace aqp_springer { #include ".../PluginProcessor.h" }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Springer"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 0
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins (patch-version files live there too):
//     #include AQP_SPRINGER_SRC(PluginProcessor.h)
 #define AQP_SPRINGER_SRC(file) AQP_STRINGIFY(../../PluginSources/springer/file)
