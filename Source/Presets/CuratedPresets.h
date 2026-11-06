#pragma once

#include <JuceHeader.h>

class AquanodeModularAudioProcessor;

// Hand-made patches that the Chord row of Random Preset draws from as one of
// its engines ("Presets"): the .aqua files of Source/Presets/Curated, built in
// (tools/assets/embed_presets.py). A roll that picks this engine loads one of them,
// each as often as the others, exactly as Import Patch would - its colours
// left out, its levels as saved.
namespace aquanode::presets
{
    int curatedPresetCount();
    juce::String curatedPresetName (int index);

    // replaces the patch with preset `index`; returns its name ("" if it would not load)
    juce::String loadCuratedPreset (AquanodeModularAudioProcessor& p, int index);
}
