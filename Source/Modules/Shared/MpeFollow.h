#pragma once
#include "../Core/ModuleCore.h"

//==============================================================================
// MPE for a module that makes its own notes from the keys (MIDI Bloom, Rain,
// Stamp - 6.9.11): the notes it plays take the bend, pressure and slide of the
// finger that leads (the latest one pressed), and follow every change of them
// while they sound. Recording and saving stay plain MIDI.
//
// heard()     every event the module is fed (processMidiEvent, MPE accepted)
// decorate()  what its midiDriverAdvanceEvents made, before it goes out
namespace aquanode
{
struct MpeFollow
{
    struct Expression { bool mpe { false }; float bend { 0.0f }, pressure { -1.0f }, slide { -1.0f }; };

    void heard (const MidiDriverEvent& in)
    {
        const int k = juce::jlimit (0, 127, in.note);
        if (in.isOn || in.isExpression)
            expr[k] = { in.mpe, in.bend, in.pressure, in.slide };
        if (in.isOn && ! in.isExpression)
            lead = k;
        if (in.isExpression && k == lead)
            pending = true;
    }

    int decorate (MidiDriverEvent* ev, int n, int maxEvents)
    {
        const bool mpe = lead >= 0 && expr[lead].mpe;
        for (int i = 0; i < n; ++i)
        {
            auto& e = ev[i];
            const int k = juce::jlimit (0, 127, e.note);
            if (e.isExpression)
                continue;
            if (e.isOn)
            {
                if (mpe)
                {
                    e.mpe = true;
                    e.bend = expr[lead].bend;
                    e.pressure = expr[lead].pressure;
                    e.slide = expr[lead].slide;
                }
                sounding[k] = true;
                soundingMpe[k] = e.mpe;
            }
            else
            {
                e.mpe = soundingMpe[k];   // (let go where it went)
                sounding[k] = false;
            }
        }
        if (pending && mpe)
        {
            for (int k = 0; k < 128 && n < maxEvents; ++k)
                if (sounding[k] && soundingMpe[k])
                {
                    MidiDriverEvent x;
                    x.note = k;
                    x.velocity = 0.0f;
                    x.mpe = true;
                    x.isExpression = true;
                    x.bend = expr[lead].bend;
                    x.pressure = expr[lead].pressure;
                    x.slide = expr[lead].slide;
                    ev[n++] = x;
                }
        }
        pending = false;
        return n;
    }

    void reset()
    {
        lead = -1;
        pending = false;
        std::fill (std::begin (sounding), std::end (sounding), false);
        std::fill (std::begin (soundingMpe), std::end (soundingMpe), false);
    }

    Expression expr[128];
    int lead { -1 };
    bool pending { false };
    bool sounding[128] {}, soundingMpe[128] {};
};
}
