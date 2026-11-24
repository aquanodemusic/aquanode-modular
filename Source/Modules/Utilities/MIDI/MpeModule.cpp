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
    enum P { pSmooth, pIgnoreDefault, pWavetable, pBendRange };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.mpe";
        d.displayName = "MPE";
        d.description = "Each note's own expression (switch MPE on beside the module search), per voice. On the on-screen "
                        "keys: Bend is moving the finger sideways (left and right, the note's own pitch bend), Slide is "
                        "moving it up and down on the key (CC 74, the top is full), Pressure is how hard it presses (where "
                        "the screen reports it). Into a filter, every "
                        "note opens as its own finger slides. The on-screen keys play every finger as its own note; MPE "
                        "controllers send the same. Ignore Default MPE Map: Off, a synth this module is cabled into keeps "
                        "its own use of the note's expression too (pitch bends the note, Slide moves its wavetable, pressure "
                        "plays a wind or a bow) and the cable adds to it; On, whatever this module sends into a synth "
                        "(Slide, Pressure, Bend - straight or through utilities) is used only where the cable says, and that "
                        "synth's own use of it stops. The title shows what it took over. Slide > Wavetable: how far a "
                        "finger moving up or down the key moves a synth's wavetable position in the default map, each way "
                        "from where the patch has it - at 0 % (where it starts, and without an MPE module) the position "
                        "stays as the patch sets it; up to 100 % the slide moves it, up to the whole table. Bend Range: how "
                        "far a finger's whole bend takes its note, 0 to 48 semitones (48 is MPE's own; 0 keeps every note "
                        "on its key). Both for the whole patch.";
        d.section = ModuleSection::Utility;
        d.sockets = { unipolarOut ("slideOut", "Slide Out"), unipolarOut ("pressOut", "Press Out"), modOut ("bendOut", "Bend Out") };
        d.params = { makeRotary ("smooth", "Smooth", 0.0f, 200.0f, 10.0f, 0, "ms"),
                     makeCombo ("ignoreDefault", "Ignore Default MPE Map", { "Off", "On" }, 0, 0, 2).routing(),   // (the graph works it out)
                     // (6.9.12) how far Slide moves a wavetable position in the default map, each way
                     makeRotary ("wavetable", "Slide > Wavetable", 0.0f, 100.0f, 0.0f, 0, "%"),
                     // (6.9.15) how far a finger's full bend goes (MPE's own is 48 semitones)
                     makeRotary ("bendRange", "Bend Range", 0.0f, 48.0f, 48.0f, 0, " st").whenMissing (48.0f) };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    float slideWavetableSetting() const override { return juce::jlimit (0.0f, 1.0f, param (pWavetable) * 0.01f); }
    float mpeBendRangeSetting() const override { return juce::jlimit (0.0f, 48.0f, param (pBendRange)); }
    // (6.9.12) which defaults its cables took over (graph build)
    void setMpeTakenOver (int bits) override { takenOver.store (bits, std::memory_order_relaxed); }
    juce::String titleSuffix() const override
    {
        const int b = takenOver.load (std::memory_order_relaxed);
        if (b == 0) return {};
        juce::StringArray what;
        if ((b & kMpeSlide) != 0) what.add ("Slide");
        if ((b & kMpePressure) != 0) what.add ("Pressure");
        if ((b & kMpeBend) != 0) what.add ("Bend");
        return " - only here: " + what.joinIntoString (", ");
    }

    void reset() override
    {
        for (auto& v : state) v = {};
    }
    std::atomic<int> takenOver { 0 };
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
