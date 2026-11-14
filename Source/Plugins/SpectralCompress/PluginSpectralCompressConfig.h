#pragma once
// SpectralCompress inside Aquanode Modular: the context its original sources are compiled
// in - by PluginSpectralCompress.cpp (the whole plugin, for its GUI version) and by any
// other file that includes its headers the same way:
//     #include "PluginSpectralCompressConfig.h"
//     namespace aqp_spectralcompress { #include AQP_SPECTRALCOMPRESS_SRC(PluginProcessor.h) }
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "SpectralCompress"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 0
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// where Aquanode Modular's copy of the plugin's sources is, relative to
// src/Plugins:
//     #include AQP_SPECTRALCOMPRESS_SRC(PluginProcessor.h)
 #define AQP_SPECTRALCOMPRESS_SRC(file) AQP_STRINGIFY(../../PluginSources/spectralcompress/file)
