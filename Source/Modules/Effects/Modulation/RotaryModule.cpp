#include "RotaryModule.h"

using namespace aquanode;

// (6.9.1: the cabinet itself is Shared/RotaryCabinet.h, built into the Drawbar Organ too)
RotaryCabinet::Settings RotaryModule::settings() const
{
    RotaryCabinet::Settings s;
    s.speed = juce::jlimit (0, 2, (int) std::lround (param (pSpeed)));
    s.rate = param (pRate);
    s.inertia = param (pInertia);
    s.xover = param (pXover);
    s.hornDepth = param (pHornDepth); s.hornThrob = param (pHornThrob); s.hornWidth = param (pHornWidth);
    s.drumThrob = param (pDrumThrob); s.drumWidth = param (pDrumWidth);
    s.drive = param (pDrive);
    s.levelDb = param (pLevel);
    return s;
}

void RotaryModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void RotaryModule::reset()
{
    RotaryCabinet::Settings s;
    s.speed = juce::jlimit (0, 2, (int) std::lround (getParameterBase (pSpeed)));
    s.rate = getParameterBase (pRate);
    cabinet.prepare (sampleRate, s);
}

void RotaryModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float inL = inputs[0][0], inR = inputs[0][1];
    float outL = 0.0f, outR = 0.0f;
    cabinet.process (settings(), inL, inR, outL, outR);
    const float mix = juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f);
    outputs[0][0] = inL + mix * (outL - inL);
    outputs[0][1] = inR + mix * (outR - inR);
}

static ModuleDescriptor rotaryDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.rotary";
    d.displayName = "Rotary";
    d.description =
        "A rotating speaker cabinet: a bass drum and a treble horn, each spinning at its own speed with its own "
        "inertia, swinging the sound around the room - Throb is the level, Width the sweep across, Depth the "
        "horn's Doppler swirl. Speed switches Stop / Slow / Fast, the rotors speeding up and coasting down as "
        "real ones do; cable a Gate onto it to switch it from the keys.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 21;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeSteppedList ("speed",  "Speed",      { "Stop", "Slow", "Fast" }, 1, 0),
        makeRotary ("rate",       "Rate",       10.0f, 200.0f, 100.0f, 0, "%"),
        makeRotary ("inertia",    "Inertia",    0.0f, 100.0f, 60.0f, 0, "%"),
        makeRotary ("xover",      "X-Over",     200.0f, 4000.0f, 800.0f, 0, "Hz", true),
        makeRotary ("hornDepth",  "Horn Depth", 0.0f, 100.0f, 60.0f, 1, "%"),
        makeRotary ("hornThrob",  "Horn Throb", 0.0f, 100.0f, 70.0f, 1, "%"),
        makeRotary ("hornWidth",  "Horn Width", 0.0f, 100.0f, 70.0f, 1, "%"),
        makeRotary ("drumThrob",  "Drum Throb", 0.0f, 100.0f, 50.0f, 2, "%"),
        makeRotary ("drumWidth",  "Drum Width", 0.0f, 100.0f, 50.0f, 2, "%"),
        makeRotary ("drive",      "Drive",      0.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("level",      "Level",      -24.0f, 12.0f, 0.0f, 3, "dB"),
        makeRotary ("mix",        "Mix",        0.0f, 100.0f, 100.0f, 3, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RotaryModule, rotaryDescriptor)
