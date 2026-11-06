//==============================================================================
// Reroute Send / Reroute Return - AudioReroute as a pair of modules
//
// A Send takes audio in on one of eight buses and passes it straight on
// (Thru); every Return on the same bus plays it, wherever it sits in the
// patch - a cable over any distance, without the cable. The graph joins each
// Send to the Returns of its bus through hidden sockets (see rerouteBus() in
// ModuleCore.h), so a Return is worked out after its Send and hears it on the
// same sample; a Return that feeds back into its own Send is a loop like any
// other and plays one sample late. Several Sends on one bus are summed, and a
// Send runs per voice when what feeds it does (its Returns then do too).
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

namespace
{
    juce::StringArray busNames()
    {
        juce::StringArray names;
        for (int i = 1; i <= 8; ++i)
            names.add ("Bus " + juce::String (i));
        return names;
    }
}

//==============================================================================
class RerouteSendModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.reroutesend";
        d.displayName = "Reroute Send";
        d.description = "Sends what comes into Audio In over a bus (1 to 8) to every Reroute Return on that bus, "
                        "however far away in the patch it is - a cable without the cable - and passes it on at Thru "
                        "unchanged. Level sets what goes onto the bus. Sidechaining across a big patch, say: a "
                        "kick's Send into a Return beside a compressor's Side In.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 13;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Thru"),
                      hiddenSocket (audioOut ("busOut", "Bus")) };
        d.params = {
            makeCombo ("bus", "Bus", busNames(), 0, 0, 3).routing(),
            makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0, "x")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    bool isRerouteSend() const override { return true; }
    int rerouteBus() const override { return juce::jlimit (0, 7, (int) getParameter ("bus")); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        levelIndex = paramIndex ("level");
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override { run (in, out); }
    void processVoiceSample (int, const StereoFrame* in, StereoFrame* out) override { run (in, out); }

private:
    void run (const StereoFrame* in, StereoFrame* out) const
    {
        const float g = levelIndex >= 0 ? param (levelIndex) : 1.0f;
        out[0] = in[0];
        out[1] = { in[0][0] * g, in[0][1] * g };
    }

    int levelIndex { -1 };
};

//==============================================================================
class RerouteReturnModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.reroutereturn";
        d.displayName = "Reroute Return";
        d.description = "Plays what every Reroute Send on its bus (1 to 8) sends, wherever those sit in the patch. "
                        "Several Returns can listen to one bus; several Sends on a bus are summed.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 14;
        d.sockets = { hiddenSocket (audioIn ("busIn", "Bus")), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("bus", "Bus", busNames(), 0, 0, 3).routing(),
            makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0, "x")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    int rerouteBus() const override { return juce::jlimit (0, 7, (int) getParameter ("bus")); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        levelIndex = paramIndex ("level");
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override { run (in, out); }
    void processVoiceSample (int, const StereoFrame* in, StereoFrame* out) override { run (in, out); }

private:
    void run (const StereoFrame* in, StereoFrame* out) const
    {
        const float g = levelIndex >= 0 ? param (levelIndex) : 1.0f;
        out[0] = { in[0][0] * g, in[0][1] * g };
    }

    int levelIndex { -1 };
};

AQUANODE_REGISTER_MODULE (RerouteSendModule, RerouteSendModule::descriptor)
AQUANODE_REGISTER_MODULE (RerouteReturnModule, RerouteReturnModule::descriptor)
