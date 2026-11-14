#pragma once
// Bandpass Modulator inside Aquanode Modular: the context its sources are
// compiled in (see PluginPrelude.h).
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Bandpass Modulator"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

 #define AQP_BANDPASSMOD_SRC(file) AQP_STRINGIFY(../../PluginSources/bandpassmod/file)
