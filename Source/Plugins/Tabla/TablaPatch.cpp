//==============================================================================
// Tabla - Patch version
//
// Tabla is two drums and a shared output stage, so its patch version is:
//
//   Tabla - Bayan    the bass drum, running Tabla's own TablaDrum: the bols
//   Tabla - Dayan    from C3 up (Na, Ta, Tin, Tun, Te, Ti, Ge, Ghe, Dha, Dhin,
//                    Dhage, Tirakita) - each drum plays its own strokes of
//                    them, combined bols and rolls included - into the modal
//                    skin, the tension chirp, the body and air resonances.
//                    Couple In takes the other drum (Sympathetic, and for the
//                    dayan Bass Coupling). The bayan's pitch follows Pressure,
//                    the pitch wheel and the mod wheel.
//   Tabla - Output   bayan left / dayan right, the oversampled soft
//                    saturation (Hardness), the stereo widener and Output.
//==============================================================================
#include "PluginTablaConfig.h"

namespace aqp_tabla
{
    #include AQP_TABLA_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;
namespace tb = aqp_tabla::tabla;

namespace
{
    const juce::String kPlugin { "tabla" };
    constexpr int kFirstBolNote = 60;
}

//==============================================================================
template <int Drum>   // 0 = bayan, 1 = dayan
class TablaDrumModule : public SynthModule
{
public:
    enum { pTuning = 0, pTension, pSyahi, pSize, pDamping, pBrightness, pStrikePos, pHardness, pBodyRes, pAirRes,
           pSympathetic, pRandomness, pArticulation, pSustain, pDecay, pVelResponse, pDrumOnly };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = Drum == 0 ? "plugin.tabla.bayan" : "plugin.tabla.dayan";
        d.displayName = kit::moduleName (kPlugin, Drum == 0 ? "Bayan" : "Dayan");
        d.description = Drum == 0
            ? "Tabla's bass drum: the bols from C3 up play its strokes (Ge, Ghe, and its half of Dha, Dhin, Dhage, "
              "Tirakita) into the modal skin - Syahi pulls its low modes toward a pitched series, Tension bends "
              "the pitch up with the energy of a stroke, Body and Air colour it. Pressure, the pitch wheel and "
              "the mod wheel slide its pitch up. Couple In: the dayan, for Sympathetic ringing."
            : "Tabla's treble drum: the bols from C3 up play its strokes (Na, Ta, Tin, Tun, Te, Ti, and its half "
              "of Dha, Dhin, Dhage, Tirakita) into the modal skin - Syahi pulls its low modes toward a pitched "
              "series, Tension bends the pitch up with the energy of a stroke, Body and Air colour it. Couple In: "
              "the bayan, for Sympathetic ringing and Bass Coupling.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + Drum;
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioIn ("coupleIn", "Couple In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary (tb::pid::tuning,       "Tuning",     -12.0f, 12.0f, 0.0f, 0, "st"),
            makeRotary (tb::pid::tension,      "Tension",      0.0f, 1.0f, 0.25f, 0),
            makeRotary (tb::pid::syahi,        "Syahi",        0.0f, 1.0f, 0.75f, 0),
            makeRotary (tb::pid::size,         "Size",         0.6f, 1.6f, 1.0f, 0),
            makeRotary (tb::pid::damping,      "Damping",      0.0f, 1.0f, 0.4f, 0),
            makeRotary (tb::pid::brightness,   "Bright",       0.0f, 1.0f, 0.5f, 1),
            makeRotary (tb::pid::strikePos,    "Strike",       0.0f, 1.0f, 0.6f, 1),
            makeRotary (tb::pid::hardness,     "Hardness",     0.0f, 1.0f, 0.5f, 1),
            makeRotary (tb::pid::bodyRes,      "Body",         0.0f, 1.0f, 0.5f, 1),
            makeRotary (tb::pid::airRes,       "Air",          0.0f, 1.0f, 0.4f, 1),
            makeRotary (tb::pid::sympathetic,  "Sympath",      0.0f, 1.0f, 0.3f, 2),
            makeRotary (tb::pid::randomness,   "Random",       0.0f, 1.0f, 0.35f, 2),
            makeRotary (tb::pid::articulation, "Artic",        0.3f, 2.0f, 1.0f, 2),
            makeRotary (tb::pid::sustain,      "Sustain",      0.3f, 2.0f, 1.0f, 2),
            makeRotary (tb::pid::decay,        "Decay",        0.3f, 2.0f, 1.0f, 2),
            makeRotary (tb::pid::velResponse,  "Vel",          0.2f, 3.0f, 1.0f, 3),
            Drum == 0 ? makeRotary (tb::pid::pressure,     "Pressure",  0.0f, 1.0f, 0.0f, 3)
                      : makeRotary (tb::pid::bassCoupling, "Bass Cpl",  0.0f, 1.0f, 0.25f, 3)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        drum.prepare (sr, Drum == 0 ? tb::TablaDrum::Type::Bayan : tb::TablaDrum::Type::Dayan);
        schedule.clear();
        schedule.reserve (64);
        clock = 0;
        rebuildCountdown = controlCountdown = 0;
        symp = 0.0f;
        rebuild();
    }

    void reset() override
    {
        drum.reset();
        schedule.clear();
        symp = 0.0f;
    }

    // a bol: this drum's strokes of it, each at its own time
    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (! m.isNoteOn())
            return;
        const int bol = m.getNoteNumber() - kFirstBolNote;
        if (bol < 0 || bol >= (int) tb::Bol::NumBols)
            return;
        const float vel = std::pow (juce::jlimit (0.0f, 1.0f, m.getFloatVelocity()), param (pVelResponse));
        for (auto& s : tb::bolToStrokes ((tb::Bol) bol, vel))
            if (s.drum == Drum && schedule.size() < 64)
                schedule.push_back ({ s.p, clock + (juce::int64) (s.timeMs * 0.001 * sampleRate) });
    }

    void midiAllNotesOff() override { schedule.clear(); }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (--rebuildCountdown <= 0)
        {
            rebuild();
            rebuildCountdown = 32;
        }

        for (int s = (int) schedule.size() - 1; s >= 0; --s)
            if (schedule[(size_t) s].when <= clock)
            {
                auto p = schedule[(size_t) s].p;
                // the Strike knob biases every stroke's contact point, as in Tabla
                p.position = juce::jlimit (0.0f, 1.0f, p.position * 0.7f + param (pStrikePos) * 0.3f);
                drum.strike (p, param (pBrightness) - 0.5f, param (pArticulation), rng, param (pRandomness));
                schedule.erase (schedule.begin() + s);
            }

        if (controlCountdown-- <= 0)
        {
            drum.updateControl();
            controlCountdown = 16;
        }

        // the other drum's ring, smoothed as Tabla smooths the two drums' sum
        float drive = symp * param (pSympathetic) * 0.03f;
        if (Drum == 1)
            drive += symp * param (pDrumOnly) * 0.04f;       // bass coupling: the bayan into the dayan
        const float y = drum.process (drive, param (pTension), param (pBodyRes), param (pAirRes), rng);
        symp += 0.05f * (0.5f * (y + in[0][0]) - symp);
        ++clock;
        out[0] = { y, y };
    }

private:
    void rebuild()
    {
        drum.rebuild (param (pTuning), param (pSyahi), param (pSize), param (pDamping), param (pBrightness),
                      param (pSustain), param (pDecay));
        if (Drum == 0)
        {
            // heel pressure, and the wheels (bend +-4 st, mod wheel up to +5 st), as Tabla's MIDI
            float bend = param (pDrumOnly) * 5.0f;
            if (midiControls != nullptr)
                bend += midiControls->bend * 4.0f + midiControls->modWheel * 5.0f;
            drum.setPressureBend (bend);
        }
    }

    struct Scheduled { tb::StrikeParams p; juce::int64 when; };
    tb::TablaDrum drum;
    std::vector<Scheduled> schedule;
    juce::Random rng;
    juce::int64 clock { 0 };
    int rebuildCountdown { 0 }, controlCountdown { 0 };
    float symp { 0.0f };
};

class TablaBayanModule final : public TablaDrumModule<0> {};
class TablaDayanModule final : public TablaDrumModule<1> {};

AQUANODE_REGISTER_MODULE (TablaBayanModule, TablaBayanModule::descriptor)
AQUANODE_REGISTER_MODULE (TablaDayanModule, TablaDayanModule::descriptor)

//==============================================================================
class TablaOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.tabla.output";
        d.displayName = kit::moduleName (kPlugin, "Output");
        d.description = "Tabla's output: the bayan to the left and the dayan to the right (each a little in the "
                        "other side), a 2x oversampled soft saturation driven by Hardness, the stereo widener "
                        "(Width) and the Output level.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 402;
        d.sockets = { audioIn ("bayanIn", "Bayan In"), audioIn ("dayanIn", "Dayan In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary (tb::pid::hardness, "Hardness", 0.0f, 1.0f, 0.5f, 0),
            makeRotary (tb::pid::width,    "Width",    0.0f, 1.0f, 0.35f, 0),
            makeRotary (tb::pid::output,   "Output",   0.0f, 1.5f, 0.9f, 0)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
        oversampler->initProcessing (1);
        widener.prepare (sr);
        outGain.reset (sr, 0.02);
        outGain.setCurrentAndTargetValue (param (2));
    }

    void reset() override
    {
        if (oversampler != nullptr)
            oversampler->reset();
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float by = in[0][0], dy = in[1][0];
        float l = by * 0.9f + dy * 0.35f;
        float r = by * 0.35f + dy * 0.9f;

        if (oversampler != nullptr)
        {
            const float drive = 1.0f + 2.5f * param (0);
            float* ch[2] = { &l, &r };
            juce::dsp::AudioBlock<float> block (ch, 2, 1);
            auto up = oversampler->processSamplesUp (block);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t n = 0; n < up.getNumSamples(); ++n)
                    d[n] = std::tanh (d[n] * drive) / drive;
            }
            oversampler->processSamplesDown (block);
        }

        widener.process (l, r, param (1));
        outGain.setTargetValue (param (2));
        const float g = outGain.getNextValue();
        out[0] = { l * g, r * g };
    }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    tb::StereoWidener widener;
    juce::LinearSmoothedValue<float> outGain;
};

AQUANODE_REGISTER_MODULE (TablaOutputModule, TablaOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::tabla
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int bayan = b.add ("plugin.tabla.bayan", 0, 0);
        const int dayan = b.add ("plugin.tabla.dayan", 0, 420);
        const int output = b.add ("plugin.tabla.output", 380, 260);
        for (int m : { bayan, dayan, output })
            b.copy (m, original);

        // each drum rings with the other; both into the output stage
        b.cable (bayan, "audioOut", dayan, "coupleIn");
        b.cable (dayan, "audioOut", bayan, "coupleIn");
        b.cable (bayan, "audioOut", output, "bayanIn");
        b.cable (dayan, "audioOut", output, "dayanIn");

        PatchPorts ports;
        ports.name = "Tabla";
        ports.midiIn = { { bayan, "midiIn" }, { dayan, "midiIn" } };
        ports.audioOut = { { output, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
