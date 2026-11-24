#pragma once
// Granulate inside Aquanode Modular: the context its original sources are compiled
// in (by PluginGranulate.cpp), inside namespace aqp_granulate (see PluginPrelude.h).
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Granulate"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

 #define AQP_GRANULATE_SRC(file) AQP_STRINGIFY(../../PluginSources/granulate/file)
