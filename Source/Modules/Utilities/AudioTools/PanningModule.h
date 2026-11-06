#pragma once

#include "../../Core/ModuleCore.h"

// Panning - stereo balance with an additive bipolar modulation input (reads
// as 0.0 when unpatched). Flexible lane, stateless.
// Inputs: 0 = Audio In (Pan moves by a cable onto its knob). Output: 0 = Audio Out.
class PanningModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void processVoiceSample (int, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        const float pan = juce::jlimit (-1.0f, 1.0f, param (0) * 0.01f);   // a cable onto Pan moves it
        outputs[0][0] = inputs[0][0] * (pan > 0.0f ? 1.0f - pan : 1.0f);
        outputs[0][1] = inputs[0][1] * (pan < 0.0f ? 1.0f + pan : 1.0f);
    }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }
};
