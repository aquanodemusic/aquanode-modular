//==============================================================================
// DC Blocker - VirtualDX7's DC block as a module
//
// A leaky integrator follows the signal's DC offset and is taken off it: a
// one-pole high-pass whose corner is Corner (0 = off, the signal passes as it
// is). As in VirtualDX7, turning it off does not freeze the offset it had
// found but lets it go over ~10 ms, so switching never leaves a step behind.
// Flexible and per channel: inside a per-voice chain it runs per voice.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class DCBlockerModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.dcblock";
        d.displayName = "DC Blocker";
        d.description = "Takes the DC offset off a sound with VirtualDX7's leaky integrator: it follows the offset "
                        "and subtracts it - a one-pole high-pass at Corner (0 Hz = off, the sound passes untouched). "
                        "For asymmetric shapers, ring mods and folders that push a sound off centre. Removed Out "
                        "is the offset it took away.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 15;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"),
                      audioOut ("dcOut", "Removed Out") };
        d.params = {
            makeRotary ("corner", "Corner", 0.0f, 20.0f, 5.0f, 0, "Hz")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        letGo = std::exp (-1.0f / (0.010f * (float) sr));
        reset();
    }

    void reset() override
    {
        for (auto& v : dc)
            v = { 0.0f, 0.0f };
    }

    void voiceReset (int v) override { dc[(size_t) v + 1] = { 0.0f, 0.0f }; }

    void processSample (const StereoFrame* in, StereoFrame* out) override { run (dc[0], in, out); }
    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override { run (dc[(size_t) v + 1], in, out); }

private:
    void run (StereoFrame& state, const StereoFrame* in, StereoFrame* out)
    {
        const float hz = param (0);
        if (hz != lastHz)
        {
            lastHz = hz;
            a = hz > 0.0f ? 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sampleRate) : 0.0f;
        }
        for (size_t c = 0; c < 2; ++c)
        {
            const float x = in[0][c];
            if (hz > 0.0f)
                state[c] += a * (x - state[c]);
            else
                state[c] = std::abs (state[c]) < 1.0e-9f ? 0.0f : state[c] * letGo;
            out[0][c] = x - state[c];
            out[1][c] = state[c];
        }
    }

    // [0] the global lane, [1 + v] each voice
    std::array<StereoFrame, (size_t) kMaxVoices + 1> dc {};
    float lastHz { -1.0f }, a { 0.0f }, letGo { 0.999f };
};

AQUANODE_REGISTER_MODULE (DCBlockerModule, DCBlockerModule::descriptor)
