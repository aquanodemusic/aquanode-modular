#pragma once
// MIDI Stamp's stamps (6.7.10) - see MidiStampData.cpp
namespace stampdata
{
    struct N { float start, length; unsigned char pitch, velocity; };
    struct Clip { int stamp; float beats; int tonic; const N* notes; int count; };
    constexpr int kNumStamps = 8;
    constexpr int kBeatStamp = 7;   // (drum pads: not transposed)
    extern const Clip clips[];
    extern const int numClips;
    extern const char* const stampNames[kNumStamps];
}
