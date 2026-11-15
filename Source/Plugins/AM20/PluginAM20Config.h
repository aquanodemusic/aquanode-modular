#pragma once
// AM-20 inside Aquanode Modular: the context its sources are compiled in (by
// PluginAM20.cpp, inside namespace aqp_am20).
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "AM-20"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to src/Plugins
#define AQP_AM20_SRC(file) AQP_STRINGIFY(../../PluginSources/am20/file)
