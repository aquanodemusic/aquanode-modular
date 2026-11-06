#pragma once

#include "../../Core/ModuleCore.h"

// Macro - one big knob whose cable sets other knobs. Patch Macro Out into as
// many knobs as you like (of any module): each follows the Macro across its
// WHOLE range - Macro at 0 % is the knob's minimum, 100 % its maximum - and
// the cable's depth blends between the knob's own setting (0 %) and the
// Macro (100 %, the default); a negative depth turns it the other way. So one
// turn can open a filter, raise a drive and darken a reverb together. The
// Macro knob itself takes cables too (an LFO, Midi Controls, DAW Mod).
// Global lane; the value is smoothed over a few milliseconds.
class MacroModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pValue = 0 };

    bool drivesKnobsAbsolutely() const override { return true; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        coef = 1.0f - std::exp ((float) (-1.0 / (0.005 * sr)));
        reset();
    }
    void reset() override { smoothed = param (pValue) * 0.01f; }

    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        smoothed += (juce::jlimit (0.0f, 1.0f, param (pValue) * 0.01f) - smoothed) * coef;
        outputs[0] = { smoothed, smoothed };
    }

private:
    float smoothed { 0.5f }, coef { 0.01f };
};
