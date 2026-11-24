//==============================================================================
// Tempo (6.7.9) - the patch's tempo as a knob, so it can be automated: an
// LFO, an envelope, a Curve CV, a DAW Mod or a MIDI Learn on its BPM moves
// the tempo of everything that follows it (clocks, sequencers, arps, delays
// in sync, the Fruit DAW ...).
//
// Every block it sets the patch's tempo - the top bar's BPM, which shows it
// all the time. While a Tempo module is in the patch, the top bar's BPM turns
// its knob (the two stay in step); while something modulates the knob, the
// top bar only shows it. In a DAW that gives a tempo, the DAW's stays (an
// Aquanode inside another Aquanode hands it on to the outer patch).
// With several, the last one in the patch's order wins.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class TempoModule final : public SynthModule
{
public:
    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void blockStart() override
    {
        // the knob as it is this block (with what modulates it)
        const double bpm = juce::jlimit (20.0, 400.0, (double) param (0));
        if (std::abs (bpm - lastSent) > 1.0e-4 || std::abs (tempoBpm - bpm) > 1.0e-3)
        {
            lastSent = bpm;
            pending.store (bpm, std::memory_order_relaxed);
        }
    }

    // the processor takes it once a block: into the patch's tempo (or on to a host Aquanode)
    double takeTempoRequest() override { return pending.exchange (0.0, std::memory_order_relaxed); }

    // Tempo Out (6.9.10): the tempo as a signal (BPM / 1000) for the Tempo Ins of a lane
    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        const float v = (float) (juce::jlimit (20.0, 400.0, (double) param (0)) / kTempoSignalScale);
        outputs[0] = { v, v };
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.tempo";
        d.displayName = "Tempo";
        d.section = ModuleSection::Utility;
        d.searchKeywords = "tempo bpm global automate speed clock";
        d.description =
            "The patch's tempo as a knob, so it can be automated: an LFO, an envelope, a Curve CV, a DAW Mod or a MIDI "
            "Learn on BPM moves the tempo of everything in the patch - clocks, sequencers, arps, synced delays. The top "
            "bar's BPM always shows it, and while the module is in the patch the top bar turns its knob; while something "
            "modulates the knob, the top bar only shows the tempo. In a DAW that gives a tempo, the DAW's stays. "
            "Tempo Out gives a lane its own tempo: patched into the Tempo In of modules (sequencers, arps, LFOs, "
            "delays, plugins), they follow this Tempo instead, and this one no longer sets the patch's tempo.";
        d.sockets = { modOut ("tempoOut", "Tempo Out") };
        d.params = { makeRotary ("bpm", "BPM", 20.0f, 400.0f, 120.0f, 0, "", false, 0.1f) };
        return d;
    }

private:
    double lastSent { -1.0 };
    std::atomic<double> pending { 0.0 };
};

AQUANODE_REGISTER_MODULE (TempoModule, TempoModule::descriptor)
