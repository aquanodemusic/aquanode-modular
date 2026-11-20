#pragma once
// SpectralFilter inside Aquanode Modular: the context its original sources are compiled
// in - by PluginSpectralFilter.cpp (the whole plugin, for its GUI version) and by any
// other file that includes its headers the same way:
//     #include "PluginSpectralFilterConfig.h"
//     namespace aqp_spectralfilter { #include AQP_SPECTRALFILTER_SRC(PluginProcessor.h) }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "SpectralFilter"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 0
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

 #define AQP_SPECTRALFILTER_SRC(file) AQP_STRINGIFY(../../PluginSources/spectralfilter/file)
