//==============================================================================
// Phase Inverter (6.9.18) - the sound upside down: every sample times -1, both
// channels. No knobs. Flexible: per voice when patched per voice.
// Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class PhaseInvertModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.phaseinvert";
        d.displayName = "Phase Inverter";
        d.searchKeywords = "phase invert polarity flip negate";
        d.description = "The sound upside down: every sample times -1, on both channels. Mixed with the sound itself it "
                        "cancels it; mixed with something else, it takes that sound away from it.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 30;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override { outputs[0] = { -inputs[0][0], -inputs[0][1] }; }
    void processVoiceSample (int, const StereoFrame* inputs, StereoFrame* outputs) override { processSample (inputs, outputs); }
};

AQUANODE_REGISTER_MODULE (PhaseInvertModule, PhaseInvertModule::descriptor)
