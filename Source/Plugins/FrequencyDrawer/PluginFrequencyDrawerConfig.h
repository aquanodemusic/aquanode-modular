#pragma once
// Frequency Drawer inside Aquanode Modular: the context its original sources are compiled
// in - by PluginFrequencyDrawer.cpp (the whole plugin, for its GUI version) and by any
// other file that includes its headers the same way:
//     #include "PluginFrequencyDrawerConfig.h"
//     namespace aqp_frequencydrawer { #include AQP_FREQUENCYDRAWER_SRC(PluginProcessor.h) }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Frequency Drawer"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins:
//     #include AQP_FREQUENCYDRAWER_SRC(PluginProcessor.h)
 #define AQP_FREQUENCYDRAWER_SRC(file) AQP_STRINGIFY(../../PluginSources/frequencydrawer/file)
