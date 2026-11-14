#pragma once

//==============================================================================
// Included first by every Plugins/Plugin*.cpp - the translation units that
// compile one of the merged plugins' original sources inside a namespace of
// its own (namespace aqp_<plugin> { #include "...PluginProcessor.cpp" }).
//
// Every system and JUCE header any of those sources include has to be in
// before the namespace opens: the sources' own #includes then hit include
// guards and add nothing, instead of pulling the standard library into the
// plugin's namespace. A plugin that starts using a new system header needs
// it added here.
//==============================================================================
#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
 // <windows.h> would define min / max as macros, which breaks every
 // std::min (...) / std::max (...) / numeric_limits<>::max() in the plugins
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <intrin.h>
 #undef min
 #undef max
 // ...and these as empty / one-word macros (rpcndr.h: small = char, minwindef.h: near and
 // far = nothing), which break plain variables of those names in the plugins' sources
 // (Tidebreeze's "bool small", Vita's "float far" / "near" in paths.h)
 #undef small
 #undef near
 #undef far
 #undef hyper
 // ...and <dlgs.h>'s dialog control ids, all short lower-case names (cmb2 = 0x0471 ...), which
 // break variables and tables of those names (Reflex's wave tables cmb2, cmb3, cmb4)
 #undef psh1
 #undef psh2
 #undef psh3
 #undef psh4
 #undef psh5
 #undef psh6
 #undef psh7
 #undef psh8
 #undef psh9
 #undef psh10
 #undef psh11
 #undef psh12
 #undef psh13
 #undef psh14
 #undef psh15
 #undef psh16
 #undef chx1
 #undef chx2
 #undef chx3
 #undef chx4
 #undef chx5
 #undef chx6
 #undef chx7
 #undef chx8
 #undef chx9
 #undef chx10
 #undef chx11
 #undef chx12
 #undef chx13
 #undef chx14
 #undef chx15
 #undef chx16
 #undef rad1
 #undef rad2
 #undef rad3
 #undef rad4
 #undef rad5
 #undef rad6
 #undef rad7
 #undef rad8
 #undef rad9
 #undef rad10
 #undef rad11
 #undef rad12
 #undef rad13
 #undef rad14
 #undef rad15
 #undef rad16
 #undef grp1
 #undef grp2
 #undef grp3
 #undef grp4
 #undef grp5
 #undef grp6
 #undef grp7
 #undef grp8
 #undef grp9
 #undef grp10
 #undef grp11
 #undef grp12
 #undef grp13
 #undef grp14
 #undef grp15
 #undef grp16
 #undef frm1
 #undef frm2
 #undef frm3
 #undef frm4
 #undef rct1
 #undef rct2
 #undef rct3
 #undef rct4
 #undef ico1
 #undef ico2
 #undef ico3
 #undef ico4
 #undef stc1
 #undef stc2
 #undef stc3
 #undef stc4
 #undef stc5
 #undef stc6
 #undef stc7
 #undef stc8
 #undef stc9
 #undef stc10
 #undef stc11
 #undef stc12
 #undef stc13
 #undef stc14
 #undef stc15
 #undef stc16
 #undef stc17
 #undef stc18
 #undef stc19
 #undef stc20
 #undef stc21
 #undef stc22
 #undef stc23
 #undef stc24
 #undef stc25
 #undef stc26
 #undef stc27
 #undef stc28
 #undef stc29
 #undef stc30
 #undef stc31
 #undef stc32
 #undef lst1
 #undef lst2
 #undef lst3
 #undef lst4
 #undef lst5
 #undef lst6
 #undef lst7
 #undef lst8
 #undef lst9
 #undef lst10
 #undef lst11
 #undef lst12
 #undef lst13
 #undef lst14
 #undef lst15
 #undef lst16
 #undef cmb1
 #undef cmb2
 #undef cmb3
 #undef cmb4
 #undef cmb5
 #undef cmb6
 #undef cmb7
 #undef cmb8
 #undef cmb9
 #undef cmb10
 #undef cmb11
 #undef cmb12
 #undef cmb13
 #undef cmb14
 #undef cmb15
 #undef cmb16
 #undef edt1
 #undef edt2
 #undef edt3
 #undef edt4
 #undef edt5
 #undef edt6
 #undef edt7
 #undef edt8
 #undef edt9
 #undef edt10
 #undef edt11
 #undef edt12
 #undef edt13
 #undef edt14
 #undef edt15
 #undef edt16
 #undef scr1
 #undef scr2
 #undef scr3
 #undef scr4
 #undef scr5
 #undef scr6
 #undef scr7
 #undef scr8
 #undef ctl1
 #undef pshHelp
#else
 #include <dlfcn.h>
 #include <unistd.h>
 #if ! defined(__APPLE__)
  #include <endian.h>
 #endif
#endif

#include "HostedPlugin.h"

//==============================================================================
// The JucePlugin_ macros the Projucer sets for Aquanode Modular itself would
// tell a merged plugin it is Aquanode Modular. Each Plugin*.cpp includes one
// of these two blocks (through AQP_PLUGIN_IS_SYNTH / AQP_PLUGIN_IS_EFFECT)
// right before its namespace, with the plugin's own name.
//==============================================================================
#undef JucePlugin_Name
#undef JucePlugin_IsSynth
#undef JucePlugin_WantsMidiInput
#undef JucePlugin_ProducesMidiOutput
#undef JucePlugin_IsMidiEffect
#undef JucePlugin_PreferredChannelConfigurations

// The plugins' sources can tell they run inside Aquanode Modular: there they
// write no files of their own accord (preset folders, factory banks, reports)
// - only what you save through a file dialog.
#define AQP_HOSTED 1

// AQP_<PLUGIN>_SRC(file) in the Plugin<Name>Config.h headers
#define AQP_STRINGIFY_IMPL(x) #x
#define AQP_STRINGIFY(x) AQP_STRINGIFY_IMPL(x)

// aqp::files::writeDocument / readDocument: the one way the app and every
// plugin save into / read from what the user picked (on every platform)
#include "../../Platform/AndroidDocuments.h"

//==============================================================================
// Android: the merged plugins' file pickers hand back real files (see
// AndroidFileChooserShim.h). Every header of Aquanode Modular's own that
// names juce::FileChooser comes in first, so only the plugins' sources see
// the replacement.
//==============================================================================
#if JUCE_ANDROID
 #include "../../Engine/PluginProcessor.h"
 #include "../../Editor/PluginEditor.h"
 #include "../../Modules/Utilities/MIDI/PianoRollModule.h"
 #include "../../Modules/Effects/Reverbs/IRForgeModule.h"
 #include "../../Modules/Utilities/AudioTools/RecordModule.h"
 #include "PatchKit.h"
 #include "PatchWidgets.h"
 #include "../../Platform/AndroidFileChooserShim.h"
 #define FileChooser AqpFileChooser
#endif
