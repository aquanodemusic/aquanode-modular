#pragma once
// Tabla inside Aquanode Modular: the context its original sources are compiled
// in - by PluginTabla.cpp (the whole plugin, for its GUI version) and by the
// patch version's modules, which include its headers the same way:
//     #include "PluginTablaConfig.h"
//     namespace aqp_tabla { #include ".../PluginProcessor.h" }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Tabla"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins (patch-version files live there too):
//     #include AQP_TABLA_SRC(PluginProcessor.h)
 #define AQP_TABLA_SRC(file) AQP_STRINGIFY(../../PluginSources/tabla/file)
