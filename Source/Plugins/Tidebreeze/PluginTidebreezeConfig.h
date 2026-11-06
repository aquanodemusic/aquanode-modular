#pragma once
// Tidebreeze inside Aquanode Modular: the context its sources are compiled in
// (by PluginTidebreeze.cpp, inside namespace aqp_tidebreeze).
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Tidebreeze"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins:
//     #include AQP_TIDEBREEZE_SRC(PluginProcessor.h)
#define AQP_TIDEBREEZE_SRC(file) AQP_STRINGIFY(../../PluginSources/tidebreeze/file)
