#pragma once
// AudioStretcher inside Aquanode Modular: the context its original sources are
// compiled in (PluginAudioStretcher.cpp, the whole plugin for its GUI version):
//     #include "PluginAudioStretcherConfig.h"
//     namespace aqp_audiostretcher { #include ".../PluginProcessor.h" }
#include "../Host/PluginPrelude.h"

// the Rubber Band Library (compiled on its own: PluginSources/audiostretcher/
// Rubberband/single/RubberBandSingle.cpp) - its header in before the plugin's
// namespace opens, so the plugin's own #include of it adds nothing there
#define USE_RUBBERBAND 1
#include "../../PluginSources/audiostretcher/Rubberband/rubberband/RubberBandStretcher.h"

#define JucePlugin_Name "AudioStretcher"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 0
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

#define AQP_AUDIOSTRETCHER_SRC(file) AQP_STRINGIFY(../../PluginSources/audiostretcher/file)
