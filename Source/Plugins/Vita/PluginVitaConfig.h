#pragma once
// Vita inside Aquanode Modular: the context its sources are compiled in - by PluginVita.cpp (the
// whole synth, for its GUI version) and by the patch version's modules, which include its engine
// headers the same way:
//     #include "PluginVitaConfig.h"
//     namespace aqp_vita { #include AQP_VITA_SRC(synth_oscillator.h) }
//
// Vita is the GPL Vital synthesizer by Matt Tytel ("measured-1.5.5" sources, see
// PluginSources/vita/VITAL_README.md and VITAL_LICENSE), without Vital's name, logo and sign-in,
// as Matt Tytel asks of forks. Aquanode Modular's copy is made by tools/build/make_vita_sources.py.
#include "../Host/PluginPrelude.h"

// system headers Vita's sources include, in before its namespace opens (see PluginPrelude.h)
#include <clocale>
#include <exception>
#include <forward_list>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <istream>
#include <iterator>
#include <list>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <valarray>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 #include <immintrin.h>
#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
 #include <arm_neon.h>
#endif

// the third-party libraries Vita uses stay global (one copy for the GUI and patch versions)
#include "../../PluginSources/vita/Source/json/json.h"
#include "../../PluginSources/vita/Source/concurrentqueue/concurrentqueue.h"
#include "../../PluginSources/vita/Source/kissfft/kissfft.h"

#define JucePlugin_Name "Vita"
#define JucePlugin_IsSynth 1
#define JucePlugin_WantsMidiInput 1
#define JucePlugin_ProducesMidiOutput 0
#define JucePlugin_IsMidiEffect 0

// no sign-in (Vital's Firebase login is not part of Vita)
#ifndef NO_AUTH
 #define NO_AUTH 1
#endif

// where Aquanode Modular's copy of Vita's sources is, relative to src/Plugins:
//     #include AQP_VITA_SRC(synth_base.h)
#define AQP_VITA_SRC(file) AQP_STRINGIFY(../../PluginSources/vita/Source/file)

// Vita's name and version where its sources ask for ProjectInfo (Aquanode Modular's own
// ProjectInfo stays untouched outside the namespace)
namespace aqp_vita
{
    // Vita's sources are written for a JuceHeader.h that declares "using namespace juce"
    using namespace juce;

    // <windows.h> (see PluginPrelude.h) declares a global GDI function ::Rectangle, which
    // makes Vita's unqualified Rectangle<int> ambiguous on Windows (MSVC C2872); this
    // using-declaration finds juce::Rectangle first, inside the namespace
    using juce::Rectangle;

    // Vita draws with juce::Graphics; the little that stays of Vital's shaders is in vita_gfx.h
    #include "../../PluginSources/vita/Source/vita_gfx.h"

    namespace ProjectInfo
    {
        const char* const projectName = "Vita";
        const char* const companyName = "Aquanode";
        const char* const versionString = "1.5.5";
        const int versionNumber = 0x10505;
    }
}
