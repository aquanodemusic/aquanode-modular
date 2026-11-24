//==============================================================================
// Audio LFO (6.9.18) - an LFO that runs from very slow up to audio rates (20 kHz):
// on a filter's cutoff or an oscillator's pitch it makes AM- and FM-like sounds.
// Key Track: the rate follows the note (Ratio x its pitch), so the modulation
// stays in tune with what plays. Saw and Square are band-limited (polyBLEP), so
// they do not alias at high rates. Retrig Note starts each note at Phase. Mod
// Out for any knob, Audio Out to hear it or to use it as audio (half level).
// Per voice. Outputs: Mod Out, Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class AudioLFOModule final : public SynthModule
{
public:
    enum P { pWave = 0, pRate, pKeyTrack, pRatio, pAmount, pRetrig, pPhase, pPolarity };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.audiolfo";
        d.displayName = "Audio LFO";
        d.searchKeywords = "audio rate lfo fm am modulation fast key track";
        d.description = "An LFO from very slow up to audio rates (20 kHz): on a cutoff or a pitch it makes AM- and FM-like "
                        "sounds. Key Track lets the rate follow the note (Ratio x its pitch). Saw and Square stay clean at "
                        "high rates. Retrig Note starts each note at Phase. Mod Out drives any knob; Audio Out is the same "
                        "wave as sound (half level).";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 14;
        d.sockets = { modOut ("modOut", "Mod Out"), audioOut ("audioOut", "Audio Out") };
        auto rate = makeRotary ("rate", "Rate", 0.01f, 20000.0f, 5.0f, 0, "Hz", true).visibleWhen ("keyTrack", 0.0f);
        rate.textFromValue = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (v, v < 10.0 ? 2 : (v < 100.0 ? 1 : 0)) + " Hz"; };
        d.params = {
            makeCombo ("wave", "Wave", { "Sine", "Triangle", "Saw", "Square" }, 0, 0, 2).noMod(),
            rate,
            makeCombo ("keyTrack", "Key Track", { "Off", "On" }, 0, 0, 2).noMod(),
            makeRotary ("ratio", "Ratio", 0.125f, 16.0f, 1.0f, 0, "x", true).visibleWhen ("keyTrack", 1.0f),
            makeRotary ("amount", "Amount", -100.0f, 100.0f, 100.0f, 1, "%", false, 1.0f),
            makeCombo ("retrig", "Retrig", { "Free", "Note" }, 0, 1, 2).noMod(),
            makeRotary ("phase", "Phase", 0.0f, 360.0f, 0.0f, 1, "deg", false, 1.0f).noMod().visibleWhen ("retrig", 1.0f),
            makeCombo ("polarity", "Polarity", { "Bipolar", "Unipolar" }, 0, 1, 2).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < kMaxVoices; ++v) voiceReset (v); }
    void voiceReset (int v) override { phase[v] = 0.0; note[v] = 60.0f; }
    void voiceNoteOn (int v, int n, bool retrigger) override
    {
        note[v] = (float) n;
        if (param (pRetrig) > 0.5f && ! retrigger) phase[v] = param (pPhase) / 360.0;
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs) override
    {
        double hz = param (pKeyTrack) > 0.5f ? midiNoteToHz ((double) note[v] + pitchBendSemitones()) * param (pRatio) : param (pRate);
        hz = juce::jlimit (0.0, 0.45 * sampleRate, hz);
        const double dt = hz / sampleRate;
        const double p = phase[v];
        float y = 0.0f;
        switch ((int) std::lround (param (pWave)))
        {
            case 1:  y = (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); break;
            case 2:  y = (float) (2.0 * p - 1.0 - blep (p, dt)); break;
            case 3:  y = (float) ((p < 0.5 ? 1.0 : -1.0) + blep (p, dt) - blep (std::fmod (p + 0.5, 1.0), dt)); break;
            default: y = (float) std::sin (juce::MathConstants<double>::twoPi * p); break;
        }
        phase[v] = p + dt - std::floor (p + dt);
        if (param (pPolarity) > 0.5f) y = 0.5f * (y + 1.0f);
        const float out = y * param (pAmount) * 0.01f;
        outputs[0] = { out, out };
        outputs[1] = { out * 0.5f, out * 0.5f };
    }

private:
    // the polyBLEP correction near a jump at phase 0 (dt: the phase step)
    static double blep (double t, double dt)
    {
        if (dt <= 0.0) return 0.0;
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    double phase[kMaxVoices] {};
    float note[kMaxVoices] {};
};

AQUANODE_REGISTER_MODULE (AudioLFOModule, AudioLFOModule::descriptor)
