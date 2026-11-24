#pragma once
// Granulate FX inside Aquanode Modular: the context its original sources are compiled
// in (by PluginGranulateFX.cpp), inside namespace aqp_granulatefx (see PluginPrelude.h).
#include "../Host/PluginPrelude.h"

#define JucePlugin_Name "Granulate FX"
#define JucePlugin_IsSynth 0
#define JucePlugin_WantsMidiInput 0
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

 #define AQP_GRANULATEFX_SRC(file) AQP_STRINGIFY(../../PluginSources/granulatefx/file)
