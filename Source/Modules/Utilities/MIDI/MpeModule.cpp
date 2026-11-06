//==============================================================================
// MPE - each note's own expression as modulation, with MPE switched on (the
// MPE switch beside the module search): Slide (how far up the key the finger
// is, CC 74, 0..1), Pressure (0..1) and Bend (the note's own pitch bend,
// -1..1 = -48..+48 semitones, as MPE sends it). On the on-screen keys every
// finger is its own note; an MPE controller
// sends the same. Per voice: into a filter's cutoff, every note opens on its
// own as its finger slides. Smoothed over Smooth (the controllers send steps).
// With MPE off, or for notes on channel 1, all three rest at 0.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class MpeModule final : public SynthModule
{
public:
    enum P { pSmooth };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.mpe";
        d.displayName = "MPE";
        d.description = "Each note's own expression (switch MPE on beside the module search): Slide (how far up the key the "
                        "finger is, CC 74), Pressure and Bend (the note's own pitch bend), per voice - into a filter, every "
                        "note opens as its own finger slides. The on-screen keys play every finger as its own note; MPE "
                        "controllers send the same.";
        d.section = ModuleSection::Utility;
        d.sockets = { unipolarOut ("slideOut", "Slide Out"), unipolarOut ("pressOut", "Press Out"), modOut ("bendOut", "Bend Out") };
        d.params = { makeRotary ("smooth", "Smooth", 0.0f, 200.0f, 10.0f, 0, "ms") };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void reset() override
    {
        for (auto& v : state) v = {};
    }
    // a new note starts where its finger is (its expression arrives with the
    // note-on, after the voice was started): no slide up from 0
    void voiceNoteOn (int v, int, bool retrigger) override { if (! retrigger) state[(size_t) v].fresh = true; }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        auto& s = state[(size_t) v];
        if (midiControls != nullptr)
        {
            const float ms = param (pSmooth);
            const float k = ms < 0.05f || s.fresh ? 1.0f : 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) sampleRate));
            s.fresh = false;
            s.slide += k * (midiControls->voiceSlide[v] - s.slide);
            s.press += k * (midiControls->voicePressure[v] - s.press);
            s.bend += k * (midiControls->voiceBend[v] / 48.0f - s.bend);
        }
        out[0] = { s.slide, s.slide };
        out[1] = { s.press, s.press };
        out[2] = { s.bend, s.bend };
    }

private:
    struct State { float slide = 0.0f, press = 0.0f, bend = 0.0f; bool fresh = false; };
    std::array<State, kMaxVoices> state {};
};

AQUANODE_REGISTER_MODULE (MpeModule, MpeModule::descriptor)
