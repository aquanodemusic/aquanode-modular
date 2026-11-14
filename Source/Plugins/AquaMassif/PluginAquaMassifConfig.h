#pragma once
// Massif inside Aquanode Modular: the context its sources are compiled in
// (by PluginAquaMassif.cpp, inside namespace aqp_aquamassif).
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Massif"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

#define AQP_AQUAMASSIF_SRC(file) AQP_STRINGIFY(../../PluginSources/aquamassif/file)
