#pragma once
/*
    TranswaveRemakes.h
    ------------------
    Built-in transwave tables shared by Phizmo and PhizmOsc (7.0.0).

    These are remakes, not copies: each table was measured and only a coarse
    description of it is kept - up to 64 keyframes of harmonic levels in 1 dB
    steps (single harmonics up to a table-dependent limit, twelfth-octave bands
    above it), with silence below -72 dB. The phases are not kept at all: every
    table is rebuilt with one of a few fixed phase laws, picked so the crest
    factor stays close while the waveform itself comes out different. The
    frames in between keyframes are interpolated. The whole bank is about
    240 kB compressed, compared with about 29 MB of WAV files.

    Tools/TranswaveRemakes/build.py writes TranswaveRemakeData.inl.

    Declared outside the plugins' own namespaces: both wrapper files include
    this header before they open theirs.
*/
#include <JuceHeader.h>
#include <vector>

namespace twremake
{
    enum Style
    {
        Pad = 1, Bass = 2, Lead = 4, Pluck = 8, Keys = 16, Bell = 32, Texture = 64, Arp = 128
    };

    constexpr int kFrameSize = 2048;

    int          count();
    juce::String name (int index);
    int          styles (int index);                     // Style bits
    int          indexOf (const juce::String& name);     // -1 if unknown
    int          pick (int styleBits, juce::Random& r);  // a random table with one of these styles (-1 if none)

    // Rebuilds table 'index' as frames of kFrameSize samples. Thread-safe.
    bool render (int index, std::vector<std::vector<float>>& frames);
}
