#pragma once
// FM12 inside Aquanode Modular: the context its original sources are compiled
// in - by PluginFM12.cpp (the whole plugin, for its GUI version) and by any
// other file that includes its headers the same way:
//     #include "PluginFM12Config.h"
//     namespace aqp_fm12 { #include AQP_FM12_SRC(PluginProcessor.h) }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "FM12"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins:
//     #include AQP_FM12_SRC(PluginProcessor.h)
 #define AQP_FM12_SRC(file) AQP_STRINGIFY(../../PluginSources/fm12/file)
