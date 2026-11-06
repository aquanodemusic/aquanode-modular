#pragma once

#include "PresetCommon.h"

// 303: an acid patch, a third of the time each:
//   - the Warp Osc voice (saw or square, mono with slide) into a resonant 24 dB
//     ladder snapped open by a short envelope, half of them with an Arp in front
//   - the 303 module played from the keys (half of them through an Arp)
//   - the 303 module playing a hook of its own on its grid (a pentatonic arp,
//     octave steps or an acid line, sometimes swung), running as soon as it is made
// After the voice: drive, a delay, sometimes a little reverb.
namespace aquanode::presets
{
    juce::String buildAcid303 (AquanodeModularAudioProcessor& p, juce::uint32 seed);
}
