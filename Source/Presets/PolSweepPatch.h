#pragma once

#include "PresetCommon.h"

// Pol Sweep: a chord swept through a moving filter, a tuned kick and high
// ticks, recreated from a music snippet (tools/test/make_snippet_patch and the
// Reassigned Spectrogram's analysis, tools/test/reassign_dump). It plays by
// itself: four Piano Rolls at 84.1 BPM, 8 bars, looping -
//   Chords: a G#m11 voicing (F#3 G#3 B3 C#4 | G#4 A#4 B4 D#5 F#5; a few stabs
//           with E) in 31 stabs of Warp Osc saws and a triangle body. Each
//           voice goes through a band-pass whose centre a smooth Random LFO
//           sweeps and every stab pushes up as far as its velocity says (the
//           velocities follow the snippet's brightness, stab by stab); the
//           voices are then summed and swept together through a ladder by a
//           bar-long Skew LFO and a second Random LFO
//   Kick:   short, tuned to ~61 Hz (near B1), the snippet's broken pattern
//   Ticks:  short clicks and longer bright hiss bursts (Hats) on 32nds,
//           through a high-pass
// One of the Synth row's engines in Random Preset.
namespace aquanode::presets
{
    juce::String buildPolSweepPatch (AquanodeModularAudioProcessor& p, juce::uint32 seed);

    // The four Piano Rolls' contents as Piano Roll state ("PR1 start:length:
    // pitch:velocity ..."): chords, kick, clicks, hiss.
    juce::StringArray polSweepPatterns();

    // The patch with other patterns in its Piano Rolls (the calibration in
    // tools/test uses it)
    juce::String buildPolSweepPatchWith (AquanodeModularAudioProcessor& p, const juce::StringArray& patterns);
}
