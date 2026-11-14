#pragma once

#include "PresetCommon.h"

// More preset engines for the category rows (6.7): psytrance squelches, slow FM
// pads and bells, Tidebreeze and Phizmo cards rolled in a fitting category
// (Tidebreeze with a chord in front), and the atmospheric (liquid) drum & bass
// kit - wide pads, 7th bells, minor e-piano chords through a wah.
namespace aquanode::presets
{
    struct ExtraEngine
    {
        juce::String engine;     // the name in the row's tick menu
        juce::String category;   // the row
        std::function<juce::String (AquanodeModularAudioProcessor&, juce::uint32 seed)> build;
    };

    const std::vector<ExtraEngine>& extraEngines();

    // the Spectra pad (remade from a reference pad): variation 0 builds the
    // reference as measured, 1 the Random Preset's spread around it
    juce::String buildSpectraPad (AquanodeModularAudioProcessor& p, juce::uint32 seed, float variation);
}
