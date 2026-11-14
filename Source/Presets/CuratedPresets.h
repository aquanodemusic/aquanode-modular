#pragma once

#include <JuceHeader.h>

class AquanodeModularAudioProcessor;

// Hand-made patches that rows of Random Preset draw from as one of their
// engines ("Presets"): the .aqua files of Source/Presets/Curated, built in
// (tools/assets/embed_presets.py) - those in Curated/ itself for the Chord row,
// those in a subfolder for the row of its name (Curated/FX/ -> FX). A roll that
// picks this engine loads one of its row's presets, each as often as the
// others, exactly as Import Patch would - its colours left out, its levels as saved.
namespace aquanode::presets
{
    int curatedPresetCount();
    juce::String curatedPresetName (int index);

    // the presets of one row: how many, and the n-th one's index
    int curatedPresetCount (const juce::String& category);
    int curatedPresetIndex (const juce::String& category, int n);

    // replaces the patch with preset `index`; returns its name ("" if it would not load)
    juce::String loadCuratedPreset (AquanodeModularAudioProcessor& p, int index);
}
