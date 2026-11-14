#pragma once
// SpectralStereoize inside Aquanode Modular: the context its original sources are compiled
// in - by PluginSpectralStereoize.cpp (the whole plugin, for its GUI version) and by any
// other file that includes its headers the same way:
//     #include "PluginSpectralStereoizeConfig.h"
//     namespace aqp_spectralstereoize { #include AQP_SPECTRALSTEREOIZE_SRC(PluginProcessor.h) }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "SpectralStereoize"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 0
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins:
//     #include AQP_SPECTRALSTEREOIZE_SRC(PluginProcessor.h)
 #define AQP_SPECTRALSTEREOIZE_SRC(file) AQP_STRINGIFY(../../PluginSources/spectralstereoize/file)
