#pragma once
// Resonate inside Aquanode Modular: the context its original sources are compiled
// in - by PluginResonate.cpp (the whole plugin, for its GUI version) and by the
// patch version's modules, which include its headers the same way:
//     #include "PluginResonateConfig.h"
//     namespace aqp_resonate { #include ".../PluginProcessor.h" }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Resonate"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins (patch-version files live there too):
//     #include AQP_RESONATE_SRC(PluginProcessor.h)
 #define AQP_RESONATE_SRC(file) AQP_STRINGIFY(../../PluginSources/resonate/file)
