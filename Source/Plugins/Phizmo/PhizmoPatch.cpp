//==============================================================================
// Phizmo - Patch version
//
// A Phizmo Preset layers up to four Sounds (each two transwave oscillators
// with their own envelopes, filter, LFO, modulation matrix and evolution
// curves). Every oscillator sends to the dry bus or the insert-effect bus,
// and everything feeds the reverb send; the buses are summed, soft-clipped
// and set to the output gain, then the insert effect, the reverb return and
// the output limiter follow. The patch version is that, module by module:
//
//   per Sound:
//   Phizmo - Voice --pitch/pos/amp A--> Phizmo - Osc A --+--> Phizmo - Filter A --+
//                  --pitch/pos/amp B--> Phizmo - Osc B --+--> Phizmo - Filter B --+
//                  --cutoff A/B ---------------------------------^               |
//                  (Phizmo - Ring between Oscs and Filters when the Sound rings)  |
//                                                     dry / insert / reverb <-----+
//   Phizmo - Bus (noise, soft clip, gain) --dry-----------------> Output
//                                         --insert--> Insert FX -> Output
//                                         --reverb--> Reverb ----> Output
//
// The Voice runs Phizmo's own control engine for its Sound (envelopes, LFO,
// modulation matrix, evolution curves, glide, arp) and sends each oscillator's
// pitch, wavetable position and amplitude, and each filter's cutoff, as
// cables; its knobs are that Sound's edit-buffer parameters. Each Osc plays
// one transwave table (kept in its state) the way Phizmo does - bandlimited
// pyramid or vintage, grit, bit crush, smoothing, stereo width - and each
// Filter is that oscillator's filter and the Sound's level. Played the same,
// the patch matches the GUI card closely (about -50 dB difference with
// notes on block boundaries); notes start on their exact sample here, where
// the card starts them at its block. Old patches with the one-piece
// "Phizmo - Sound" module still load and play it.
//==============================================================================
#include "PluginPhizmoConfig.h"

namespace aqp_phizmo
{
    #include AQP_PHIZMO_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "phizmo" };
    using PZ = aqp_phizmo::PhizmoAudioProcessor;
    namespace phz = aqp_phizmo::phz;
    constexpr int kSounds = 4;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.phizmo." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    juce::StringArray offOn() { return { "Off", "On" }; }
    ParamSpec knob (const char* id, const char* label, float lo, float hi, float def, int row,
                    const char* unit = "", bool log = false, float step = 0.0f)
    {
        return makeRotary (id, label, lo, hi, def, row, unit, log, step);
    }
}

//==============================================================================
// Phizmo - Sound
//==============================================================================
class PhizmoSoundModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("sound", "Sound", ModuleSection::Oscillator, 0);
        d.listedInSidebar = false;   // older patches; a Sound is Voice + Osc + Filter modules now
        d.description = "One of a Phizmo Preset's four Sounds, played by Phizmo's own engine: two transwave "
                        "oscillators scanning their wavetables along the evolution curves, with envelopes, "
                        "filter, LFO and the modulation matrix. Low / High Key and Level are its layer; Bus 1 / "
                        "Bus 2 send each oscillator to the insert effect or dry. Its three outputs go to the "
                        "Phizmo Bus. The matrix, curves and wavetables come with the Sound (saved in the patch).";
        d.sockets = { midiIn ("midiIn", "Midi In"),
                      audioOut ("dryOut", "Dry Out"), audioOut ("insOut", "Insert Out"), audioOut ("revOut", "Reverb Out") };
        d.params = {
            makeSteppedList ("sound", "Sound", { "1", "2", "3", "4" }, 0, 0).noMod(),
            knob ("low", "Low Key", 0, 127, 0, 0, "", false, 1.0f),
            knob ("high", "High Key", 0, 127, 127, 0, "", false, 1.0f),
            knob ("layerGain", "Level", 0.0f, 1.0f, 1.0f, 0),
            makeCombo ("busA", "Bus 1", { "Insert", "Dry" }, 0, 1, 1),
            makeCombo ("busB", "Bus 2", { "Insert", "Dry" }, 0, 1, 1),
            makeCombo ("oscAOn", "Osc 1", offOn(), 1, 1, 1),
            makeCombo ("oscBOn", "Osc 2", offOn(), 1, 1, 1),
            knob ("oscMix", "A/B Mix", 0.0f, 1.0f, 0.5f, 1),

            knob ("levelA", "Level 1", 0.0f, 1.0f, 1.0f, 2),
            knob ("octaveA", "Oct 1", -2, 2, 0, 2, "", false, 1.0f),
            knob ("tuneA", "Tune 1", -24, 24, 0, 2, "st", false, 1.0f),
            knob ("fineA", "Fine 1", -100, 100, 0, 2, "ct"),
            knob ("panA", "Pan 1", -1.0f, 1.0f, 0.0f, 2),
            knob ("levelB", "Level 2", 0.0f, 1.0f, 1.0f, 3),
            knob ("octaveB", "Oct 2", -2, 2, 0, 3, "", false, 1.0f),
            knob ("tuneB", "Tune 2", -24, 24, 0, 3, "st", false, 1.0f),
            knob ("fineB", "Fine 2", -100, 100, 0, 3, "ct"),
            knob ("panB", "Pan 2", -1.0f, 1.0f, 0.0f, 3),
            knob ("detune", "Detune", -100, 100, 0, 4, "ct"),
            knob ("spread", "Spread", 0, 50, 0, 4, "ct"),
            knob ("uniDetune", "Uni Det", 0, 50, 0, 4, "ct"),
            knob ("ringMod", "Ring", 0.0f, 1.0f, 0.0f, 4),
            knob ("glide", "Glide", 0.0f, 4.0f, 0.0f, 4, "s", true),

            knob ("evoTime", "Evo Time", 0.1f, 100.0f, 4.0f, 5, "s", true),
            knob ("evoTimeB", "Evo B", 0.1f, 100.0f, 4.0f, 5, "s", true),
            makeCombo ("scanStyle", "Scan", { "Forward", "Fwd Stay", "Back & Forth", "Bwd Stay", "Backward" }, 0, 5, 2),
            knob ("evoMode", "Mode 1", 0, 3, 0, 6, "", false, 1.0f),
            knob ("evoModeB", "Mode 2", 0, 3, 0, 6, "", false, 1.0f),
            knob ("waveStartA", "Start 1", 0.0f, 1.0f, 0.0f, 6),
            knob ("waveStartB", "Start 2", 0.0f, 1.0f, 0.0f, 6),
            knob ("jumpProb", "Jump", 0.0f, 1.0f, 0.0f, 6),
            knob ("waveModA", "W.Mod 1", -1.0f, 1.0f, 0.0f, 7),
            knob ("waveModB", "W.Mod 2", -1.0f, 1.0f, 0.0f, 7),
            knob ("evoLFORate", "Evo LFO", 0.01f, 20.0f, 0.3f, 7, "Hz", true),
            knob ("evoLFODepth", "Evo Dep", 0.0f, 1.0f, 0.0f, 7),
            knob ("keytrack", "Key>Frame", 0.0f, 1.0f, 0.0f, 7),

            makeCombo ("filterType", "Filter", { "LP 4-pole", "Band Pass", "High Pass" }, 0, 8, 2),
            knob ("filterFreq", "Cutoff", 20.0f, 20000.0f, 20000.0f, 8, "Hz", true),
            knob ("filterQ", "Reso", 0.1f, 12.0f, 0.707f, 8, "", true),
            knob ("filterEnvAmt", "Env Amt", -1.0f, 1.0f, 0.0f, 8),
            knob ("filterKeytrack", "F.Key", -1.0f, 1.0f, 0.0f, 9),
            knob ("filterLFODep", "F.LFO", 0.0f, 1.0f, 0.0f, 9),
            knob ("twToFilter", "TW>Filt", -1.0f, 1.0f, 0.0f, 9),
            knob ("grit", "Grit", 0.0f, 1.0f, 0.0f, 9),
            knob ("bitCrush", "Bits", 4.0f, 16.0f, 16.0f, 9),

            knob ("attack", "A 1", 0.001f, 8.0f, 0.01f, 10, "s", true),
            knob ("decay", "D 1", 0.001f, 8.0f, 0.3f, 10, "s", true),
            knob ("sustain", "S 1", 0.0f, 1.0f, 0.8f, 10),
            knob ("release", "R 1", 0.001f, 8.0f, 0.5f, 10, "s", true),
            knob ("ampVelAmt", "Vel", 0.0f, 1.0f, 0.6f, 10),
            knob ("attack2", "A 2", 0.001f, 8.0f, 0.01f, 11, "s", true),
            knob ("decay2", "D 2", 0.001f, 8.0f, 0.3f, 11, "s", true),
            knob ("sustain2", "S 2", 0.0f, 1.0f, 0.8f, 11),
            knob ("release2", "R 2", 0.001f, 8.0f, 0.5f, 11, "s", true),
            knob ("filterAtt", "F.A", 0.001f, 8.0f, 0.01f, 12, "s", true),
            knob ("filterDec", "F.D", 0.001f, 8.0f, 0.5f, 12, "s", true),
            knob ("filterSus", "F.S", 0.0f, 1.0f, 0.0f, 12),
            knob ("filterRel", "F.R", 0.001f, 8.0f, 0.5f, 12, "s", true),
            knob ("twAmt", "TW Amt", -1.0f, 1.0f, 0.0f, 13),
            knob ("twAtt", "TW A", 0.001f, 8.0f, 0.01f, 13, "s", true),
            knob ("twDec", "TW D", 0.001f, 8.0f, 1.0f, 13, "s", true),
            knob ("twSus", "TW S", 0.0f, 1.0f, 0.0f, 13),
            knob ("twRel", "TW R", 0.001f, 8.0f, 0.5f, 13, "s", true),

            makeCombo ("lfoShape", "LFO", { "Triangle", "Sine+Tri", "Sine", "Rise Tri", "Rise Sine", "Saw",
                                            "Square", "Pos Ramp", "Noise" }, 2, 14, 2),
            knob ("lfoSpeed", "LFO Rate", 0.01f, 20.0f, 1.0f, 14, "Hz", true),
            knob ("posLFODepth", "Pos LFO", 0.0f, 1.0f, 0.0f, 14),
            knob ("pitchEnvAmt", "P.Env", -24.0f, 24.0f, 0.0f, 15, "st"),
            knob ("pitchEnvDec", "P.Dec", 0.001f, 4.0f, 0.3f, 15, "s", true),
            knob ("stereoWidth", "Width", 0.0f, 1.0f, 0.5f, 15),
            knob ("stereoPhase", "B Phase", 0.0f, 1.0f, 0.0f, 15),

            makeCombo ("mono", "Mono", offOn(), 0, 16, 1).noMod(),
            knob ("bendRange", "Bend", 0, 12, 2, 16, "st", false, 1.0f),
            makeCombo ("velCurve", "Vel Curve", { "Curve 1", "Curve 2", "Curve 3", "Curve 4", "Fixed 64", "Fixed 127" }, 1, 16, 2)
        };
        return d;
    }

    PhizmoSoundModule()
    {
        auto p = createOriginal (kPlugin, 48000.0);
        pz.reset (dynamic_cast<PZ*> (p.get()));
        if (pz != nullptr)
            p.release();
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override { midiQueue.addEvent (m, pos); }
    void midiAllNotesOff() override { midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos); }

    // which of the Preset's Sounds this module plays (also its edit buffer)
    void useSound (int k)
    {
        k = juce::jlimit (0, kSounds - 1, k);
        if (pz == nullptr)
            return;
        pz->setEditSound (k);
        for (int s = 0; s < kSounds; ++s)
            if (auto* on = kit::findParam (*pz, "soundOn" + juce::String (s)))
                on->setValueNotifyingHost (s == k ? 1.0f : 0.0f);
        sound = k;
        relink();
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        if (pz == nullptr)
            return;
        {
            const juce::ScopedLock sl (pz->getCallbackLock());
            pz->releaseResources();
            pz->setRateAndBufferSizeDetails (sr, kBlock);
            pz->prepareToPlay (sr, kBlock);
        }
        useSound ((int) std::lround (getParameterBase (0)));
        midiQueue.ensureSize (1024);
        for (auto& b : bus) b.fill (0.0f);
        for (auto& o : out) o.fill (0.0f);
        pos = 0;
    }

    void reset() override { midiQueue.clear(); midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0); }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        for (int b = 0; b < 3; ++b)
            outputs[b] = { out[(size_t) b * 2][(size_t) pos], out[(size_t) b * 2 + 1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

    juce::String saveCustomState() const override
    {
        if (pz == nullptr) return {};
        juce::MemoryBlock mb;
        pz->getStateInformation (mb);
        return mb.toBase64Encoding();
    }
    void loadCustomState (const juce::String& s) override
    {
        juce::MemoryBlock mb;
        if (pz != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
        {
            pz->setStateInformation (mb.getData(), (int) mb.getSize());
            useSound ((int) std::lround (getParameterBase (0)));
        }
    }

    PZ* plugin() const { return pz.get(); }

private:
    static constexpr int kBlock = 32;

    void relink()
    {
        links.clear();
        const auto& d = getDescriptor();
        const juce::String k (sound);
        for (int i = 1; i < (int) d.params.size(); ++i)
        {
            auto id = d.params[(size_t) i].id;
            if (id == "low") id = "soundLow" + k;
            else if (id == "high") id = "soundHigh" + k;
            else if (id == "layerGain") id = "soundGain" + k;
            else if (id == "busA") id = "fxBusA" + k;
            else if (id == "busB") id = "fxBusB" + k;
            if (auto* prm = kit::findParam (*pz, id))
                links.push_back ({ prm, i, -1.0f });
        }
    }

    void runBlock()
    {
        if (pz == nullptr)
            return;
        if ((int) std::lround (getParameterBase (0)) != sound)
        {
            // the Sound selector moved: switch on the message thread
            juce::WeakReference<SynthModule> weak (this);
            const int k = (int) std::lround (getParameterBase (0));
            juce::MessageManager::callAsync ([weak, k] { if (auto* m = dynamic_cast<PhizmoSoundModule*> (weak.get())) m->useSound (k); });
            sound = k;   // asked once
        }
        for (auto& l : links)
        {
            const float raw = param (l.knob);   // unmoved knob: nothing to convert or send
            if (juce::exactlyEqual (raw, l.raw)) continue;
            l.raw = raw;
            const float v = l.param->convertTo0to1 (raw);
            if (v != l.last) { l.param->setValueNotifyingHost (v); l.last = v; }
        }

        for (auto& b : bus) b.fill (0.0f);
        bool rendered = false;
        {
            const juce::ScopedTryLock sl (pz->getCallbackLock());
            if (sl.isLocked())
            {
                pz->handleMidiAndArp (midiQueue, kBlock);
                rendered = pz->renderVoiceBuses (kBlock, bus[0].data(), bus[1].data(), bus[2].data(), bus[3].data(),
                                                 bus[4].data(), bus[5].data(), 1u << sound, false);
            }
        }
        midiQueue.clear();
        for (size_t c = 0; c < 6; ++c)
            for (int i = 0; i < kBlock; ++i)
                out[c][(size_t) i] = rendered && std::isfinite (bus[c][(size_t) i]) ? bus[c][(size_t) i] : 0.0f;
    }

    struct Link { juce::RangedAudioParameter* param; int knob; float last; float raw = std::numeric_limits<float>::quiet_NaN(); };

    std::unique_ptr<PZ> pz;
    std::vector<Link> links;
    juce::MidiBuffer midiQueue;
    std::array<std::array<float, kBlock>, 6> bus {}, out {};
    int pos { 0 }, sound { -1 };
};

//==============================================================================
// The Sound taken apart (a Sound of a Phizmo Preset as five modules):
//
//   Phizmo - Voice --Pitch / Pos / Amp 1--> Phizmo - Osc (1) --> [Ring] --> Phizmo - Filter (1) --> Bus
//                  --Pitch / Pos / Amp 2--> Phizmo - Osc (2) --> [Ring] --> Phizmo - Filter (2) --> Bus
//                  --Cutoff 1 / 2---------------------------------------------^
//
// The Voice runs Phizmo's own control of a voice (PhizmoAudioProcessor::patchControl): the
// envelopes, the evolution scans along the drawn curves, the LFO and noise, the modulation
// matrix, pitch and cutoff - one Phizmo voice per Aquanode voice. The Oscs read their
// wavetables the way Phizmo does (vintage or band-limited), the Filters are Phizmo's 4-pole.
//==============================================================================
namespace
{
    // pitch / cutoff outputs follow the patch conventions: semitones / 60, octaves / 5
    constexpr float kSemisPerUnit = 60.0f, kOctavesPerUnit = 5.0f;

    juce::String voiceParamFor (const juce::String& id, int sound)
    {
        const juce::String k (sound);
        if (id == "low") return "soundLow" + k;
        if (id == "high") return "soundHigh" + k;
        return id;
    }
}

class PhizmoVoiceModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("voice", "Voice", ModuleSection::Utility, 1);
        d.description = "The control of a Phizmo Sound: per note, its envelopes, the evolution scans along the drawn "
                        "curves, the LFO and noise, the modulation matrix, pitch and the filter's cutoff - Phizmo's own "
                        "engine, without the sound. Its outputs play two Phizmo - Osc modules (Pitch, frame Position, "
                        "Amp) and two Phizmo - Filter modules (Cutoff). Low / High Key is the Sound's key zone. The "
                        "curves and the matrix come with the module (saved in the patch).";
        d.sockets = { modOut ("pitchA", "Pitch 1"), modOut ("posA", "Pos 1"), modOut ("ampA", "Amp 1"), modOut ("cutA", "Cutoff 1"),
                      modOut ("pitchB", "Pitch 2"), modOut ("posB", "Pos 2"), modOut ("ampB", "Amp 2"), modOut ("cutB", "Cutoff 2") };
        d.params = {
            makeSteppedList ("sound", "Sound", { "1", "2", "3", "4" }, 0, 0).noMod(),
            knob ("low", "Low Key", 0, 127, 0, 0, "", false, 1.0f),
            knob ("high", "High Key", 0, 127, 127, 0, "", false, 1.0f),
            makeCombo ("oscAOn", "Osc 1", offOn(), 1, 0, 1),
            makeCombo ("oscBOn", "Osc 2", offOn(), 1, 0, 1),

            knob ("levelA", "Level 1", 0.0f, 1.0f, 1.0f, 1),
            knob ("octaveA", "Oct 1", -2, 2, 0, 1, "", false, 1.0f),
            knob ("tuneA", "Tune 1", -24, 24, 0, 1, "st", false, 1.0f),
            knob ("fineA", "Fine 1", -100, 100, 0, 1, "ct"),
            knob ("oscMix", "A/B Mix", 0.0f, 1.0f, 0.5f, 1),
            knob ("levelB", "Level 2", 0.0f, 1.0f, 1.0f, 2),
            knob ("octaveB", "Oct 2", -2, 2, 0, 2, "", false, 1.0f),
            knob ("tuneB", "Tune 2", -24, 24, 0, 2, "st", false, 1.0f),
            knob ("fineB", "Fine 2", -100, 100, 0, 2, "ct"),
            knob ("detune", "Detune", -100, 100, 0, 3, "ct"),
            knob ("spread", "Spread", 0, 50, 0, 3, "ct"),
            knob ("uniDetune", "Uni Det", 0, 50, 0, 3, "ct"),
            knob ("bendRange", "Bend", 0, 12, 2, 3, "st", false, 1.0f),
            makeCombo ("velCurve", "Vel Curve", { "Curve 1", "Curve 2", "Curve 3", "Curve 4", "Fixed 64", "Fixed 127" }, 1, 3, 2),

            knob ("evoTime", "Evo Time", 0.1f, 100.0f, 4.0f, 4, "s", true),
            knob ("evoTimeB", "Evo B", 0.1f, 100.0f, 4.0f, 4, "s", true),
            makeCombo ("scanStyle", "Scan", { "Forward", "Fwd Stay", "Back & Forth", "Bwd Stay", "Backward" }, 0, 4, 2),
            knob ("evoMode", "Mode 1", 0, 3, 0, 5, "", false, 1.0f),
            knob ("evoModeB", "Mode 2", 0, 3, 0, 5, "", false, 1.0f),
            knob ("waveStartA", "Start 1", 0.0f, 1.0f, 0.0f, 5),
            knob ("waveStartB", "Start 2", 0.0f, 1.0f, 0.0f, 5),
            knob ("jumpProb", "Jump", 0.0f, 1.0f, 0.0f, 5),
            knob ("waveModA", "W.Mod 1", -1.0f, 1.0f, 0.0f, 6),
            knob ("waveModB", "W.Mod 2", -1.0f, 1.0f, 0.0f, 6),
            knob ("evoLFORate", "Evo LFO", 0.01f, 20.0f, 0.3f, 6, "Hz", true),
            knob ("evoLFODepth", "Evo Dep", 0.0f, 1.0f, 0.0f, 6),
            knob ("keytrack", "Key>Frame", 0.0f, 1.0f, 0.0f, 6),

            knob ("filterEnvAmt", "F.Env", -1.0f, 1.0f, 0.0f, 7),
            knob ("filterKeytrack", "F.Key", -1.0f, 1.0f, 0.0f, 7),
            knob ("filterLFODep", "F.LFO", 0.0f, 1.0f, 0.0f, 7),
            knob ("twToFilter", "TW>Filt", -1.0f, 1.0f, 0.0f, 7),

            knob ("attack", "A 1", 0.001f, 8.0f, 0.01f, 8, "s", true),
            knob ("decay", "D 1", 0.001f, 8.0f, 0.3f, 8, "s", true),
            knob ("sustain", "S 1", 0.0f, 1.0f, 0.8f, 8),
            knob ("release", "R 1", 0.001f, 8.0f, 0.5f, 8, "s", true),
            knob ("ampVelAmt", "Vel", 0.0f, 1.0f, 0.6f, 8),
            knob ("attack2", "A 2", 0.001f, 8.0f, 0.01f, 9, "s", true),
            knob ("decay2", "D 2", 0.001f, 8.0f, 0.3f, 9, "s", true),
            knob ("sustain2", "S 2", 0.0f, 1.0f, 0.8f, 9),
            knob ("release2", "R 2", 0.001f, 8.0f, 0.5f, 9, "s", true),
            knob ("filterAtt", "F.A", 0.001f, 8.0f, 0.01f, 10, "s", true),
            knob ("filterDec", "F.D", 0.001f, 8.0f, 0.5f, 10, "s", true),
            knob ("filterSus", "F.S", 0.0f, 1.0f, 0.0f, 10),
            knob ("filterRel", "F.R", 0.001f, 8.0f, 0.5f, 10, "s", true),
            knob ("twAmt", "TW Amt", -1.0f, 1.0f, 0.0f, 11),
            knob ("twAtt", "TW A", 0.001f, 8.0f, 0.01f, 11, "s", true),
            knob ("twDec", "TW D", 0.001f, 8.0f, 1.0f, 11, "s", true),
            knob ("twSus", "TW S", 0.0f, 1.0f, 0.0f, 11),
            knob ("twRel", "TW R", 0.001f, 8.0f, 0.5f, 11, "s", true),

            makeCombo ("lfoShape", "LFO", { "Triangle", "Sine+Tri", "Sine", "Rise Tri", "Rise Sine", "Saw",
                                            "Square", "Pos Ramp", "Noise" }, 2, 12, 2),
            knob ("lfoSpeed", "LFO Rate", 0.01f, 20.0f, 1.0f, 12, "Hz", true),
            knob ("posLFODepth", "Pos LFO", 0.0f, 1.0f, 0.0f, 12),
            knob ("pitchEnvAmt", "P.Env", -24.0f, 24.0f, 0.0f, 13, "st"),
            knob ("pitchEnvDec", "P.Dec", 0.001f, 4.0f, 0.3f, 13, "s", true)
        };
        return d;
    }

    PhizmoVoiceModule()
    {
        auto p = createOriginal (kPlugin, 48000.0);
        pz.reset (dynamic_cast<PZ*> (p.get()));
        if (pz != nullptr)
            p.release();
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        // the controllers (bend, mod wheel, pressure); the notes come as voices
        if (! m.isNoteOnOrOff())
            midiQueue.addEvent (m, 0);
    }
    double voiceTailSeconds() const override
    {
        return juce::jmax (0.05, (double) juce::jmax (getParameterBase (paramIndex ("release")),
                                                      getParameterBase (paramIndex ("release2")))) * 1.2;
    }

    // which of the Preset's Sounds this module plays (also its edit buffer)
    void useSound (int k)
    {
        k = juce::jlimit (0, kSounds - 1, k);
        if (pz == nullptr)
            return;
        pz->setEditSound (k);
        sound = k;
        links.clear();
        const auto& d = getDescriptor();
        for (int i = 1; i < (int) d.params.size(); ++i)
            if (auto* prm = kit::findParam (*pz, voiceParamFor (d.params[(size_t) i].id, k)))
                links.push_back ({ prm, i, -1.0f });
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        if (pz == nullptr)
            return;
        {
            const juce::ScopedLock sl (pz->getCallbackLock());
            pz->releaseResources();
            pz->setRateAndBufferSizeDetails (sr, 32);
            pz->prepareToPlay (sr, 32);
        }
        useSound ((int) std::lround (getParameterBase (0)));
        midiQueue.ensureSize (256);
        for (auto& c : counted) c = 0;
        lastBlock = 32;
    }

    void blockStart() override
    {
        if (pz == nullptr)
            return;
        if ((int) std::lround (getParameterBase (0)) != sound)
        {
            juce::WeakReference<SynthModule> weak (this);
            const int k = (int) std::lround (getParameterBase (0));
            juce::MessageManager::callAsync ([weak, k] { if (auto* m = dynamic_cast<PhizmoVoiceModule*> (weak.get())) m->useSound (k); });
            sound = k;
        }
        for (auto& l : links)
        {
            const float raw = param (l.knob);
            if (juce::exactlyEqual (raw, l.raw)) continue;
            l.raw = raw;
            const float v = l.param->convertTo0to1 (raw);
            if (v != l.last) { l.param->setValueNotifyingHost (v); l.last = v; }
        }
        // the samples the last block had (the longest any voice ran)
        int n = 0;
        for (auto& c : counted) { n = juce::jmax (n, c); c = 0; }
        if (n > 0) lastBlock = n;
        const juce::ScopedTryLock sl (pz->getCallbackLock());
        if ((ready = sl.isLocked()))
        {
            if (! midiQueue.isEmpty())
                pz->handleMidiAndArp (midiQueue, lastBlock);
            pz->patchBeginBlock (lastBlock);
        }
        midiQueue.clear();
    }

    void voiceNoteOn (int v, int note, bool) override
    {
        notes[v] = note;
        pending[v] = true;
        velocity[v] = 0.8f;
    }
    void voiceVelocity (int v, float velocity01) override
    {
        velocity[v] = velocity01;
        startPending (v);
    }
    void voiceNoteOff (int v) override
    {
        pending[v] = false;
        if (pz != nullptr) pz->patchNoteOff (v);
    }
    void voiceReset (int v) override
    {
        pending[v] = false;
        if (pz != nullptr) pz->patchVoiceReset (v);
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs) override
    {
        ++counted[v];
        for (int i = 0; i < 8; ++i) outputs[i] = { 0.0f, 0.0f };
        if (pz == nullptr || ! ready)
            return;
        startPending (v);
        PZ::VoiceTaps t;
        pz->patchControl (v, t);
        if (! t.active)
            return;
        const double noteHz = aquanode::midiNoteToHz ((double) notes[v]);
        auto pitch = [noteHz] (double hz) { return (float) (12.0 * std::log2 (juce::jmax (1.0e-3, hz) / noteHz) / kSemisPerUnit); };
        auto both = [] (float x) { return StereoFrame { x, x }; };
        outputs[0] = both (pitch (t.freqA));
        outputs[1] = both (t.posA);
        outputs[2] = both (t.ampA);
        outputs[3] = both (t.cutOctA / kOctavesPerUnit);
        outputs[4] = both (pitch (t.freqB));
        outputs[5] = both (t.posB);
        outputs[6] = both (t.ampB);
        outputs[7] = both (t.cutOctB / kOctavesPerUnit);
    }

    juce::String saveCustomState() const override
    {
        if (pz == nullptr) return {};
        juce::MemoryBlock mb;
        pz->getStateInformation (mb);
        return mb.toBase64Encoding();
    }
    void loadCustomState (const juce::String& s) override
    {
        juce::MemoryBlock mb;
        if (pz != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
        {
            pz->setStateInformation (mb.getData(), (int) mb.getSize());
            useSound ((int) std::lround (getParameterBase (0)));
        }
    }

    PZ* plugin() const { return pz.get(); }

private:
    void startPending (int v)
    {
        if (! pending[v] || pz == nullptr)
            return;
        pending[v] = false;
        const int low = (int) std::lround (getParameterBase (1)), high = (int) std::lround (getParameterBase (2));
        if (notes[v] < low || notes[v] > high)
        {
            pz->patchVoiceReset (v);   // outside the Sound's zone: this voice stays silent
            return;
        }
        pz->patchNoteOn (v, sound, notes[v], velocity[v]);
    }

    struct Link { juce::RangedAudioParameter* param; int knob; float last; float raw = std::numeric_limits<float>::quiet_NaN(); };

    std::unique_ptr<PZ> pz;
    std::vector<Link> links;
    juce::MidiBuffer midiQueue;
    int notes[kMaxVoices] {};
    float velocity[kMaxVoices] {};
    bool pending[kMaxVoices] {};
    int counted[kMaxVoices] {};
    int lastBlock { 32 }, sound { -1 };
    bool ready { false };
};

//==============================================================================
// Phizmo - Osc: one transwave oscillator, the wavetable in its state
//==============================================================================
class PhizmoOscModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("osc", "Osc", ModuleSection::Oscillator, 2);
        d.description = "A Phizmo transwave oscillator: it plays its wavetable (kept in the patch) at the frame its "
                        "Pos In names, the way Phizmo reads it - Vintage (the hardware's plain reads, with GRIT) or "
                        "Clean (band-limited). Pitch In is semitones / 60 from the played note, Amp In its loudness "
                        "(a Phizmo - Voice drives all three). Raw Out is the wave before Amp and Pan, for the Ring.";
        d.sockets = { modIn ("pitchIn", "Pitch In"), modIn ("posIn", "Pos In"), modIn ("ampIn", "Amp In"),
                      audioOut ("audioOut", "Audio Out"), audioOut ("rawOut", "Raw Out") };
        d.params = {
            makeCombo ("osc", "Osc", { "1", "2" }, 0, 0, 1).noMod(),
            knob ("position", "Position", 0.0f, 1.0f, 0.0f, 0),
            knob ("pan", "Pan", -1.0f, 1.0f, 0.0f, 0),
            makeCombo ("engine", "Engine", { "Vintage", "Clean" }, 0, 1, 1),
            makeCombo ("interp", "Interp", offOn(), 1, 1, 1),
            knob ("grit", "Grit", 0.0f, 1.0f, 0.0f, 1),
            knob ("bits", "Bits", 4.0f, 16.0f, 16.0f, 2),
            knob ("smooth", "Smooth", 0.0f, 1.0f, 0.0f, 2),
            knob ("width", "Width", 0.0f, 1.0f, 0.5f, 2),
            knob ("phase", "B Phase", 0.0f, 1.0f, 0.0f, 2),
            makeCombo ("gritVintage", "Grit Vtg", offOn(), 1, 1, 1)   // GRIT takes the vintage read
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    double voiceTailSeconds() const override { return 0.05; }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        notes[v] = note;
        if (retrigger) phase[v] = 0.0;
    }
    void voiceReset (int v) override { phase[v] = 0.0; }

    void setTable (std::vector<std::vector<float>> frames, int cycleSamples)
    {
        auto t = std::make_shared<Table>();
        t->frames = std::move (frames);
        t->cycleSamples = juce::jmax (1, cycleSamples);
        t->pyramid = PZ::pyramidFor (t->frames, t->cycleSamples);
        std::atomic_store (&table, std::shared_ptr<const Table> (t));
    }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* outputs) override
    {
        outputs[0] = outputs[1] = { 0.0f, 0.0f };
        const auto t = std::atomic_load (&table);
        if (t == nullptr || t->frames.empty())
            return;

        const double noteHz = aquanode::midiNoteToHz ((double) notes[v]);
        const double hz = noteHz * std::exp2 ((double) in[0][0] * kSemisPerUnit / 12.0);
        phase[v] += hz / sampleRate;
        phase[v] -= std::floor (phase[v]);

        // a voice its Voice does not play (or not any more): nothing to read
        const float amp = isInputConnected (2) ? in[2][0] : 0.0f;
        if (amp == 0.0f)
            return;

        const float pos = isInputConnected (1) ? in[1][0] : param (1);
        const int numFrames = (int) t->frames.size();
        const float fi = juce::jlimit (0.0f, 1.0f, pos) * (float) (numFrames > 0 ? numFrames - 1 : 0);

        // GRIT: fewer steps per cycle (the hardware's decimated read)
        double l = phase[v];
        const float gr = param (5);
        if (gr > 0.0f)
        {
            const float resoFrac = std::pow (1.0f - gr, 3.0f);
            const int q = juce::jmax (6, (int) (juce::jmax (6, t->cycleSamples) * resoFrac));
            l = std::floor (l * q) / (double) q;
        }
        while (l < 0.0) l += 1.0;
        while (l >= 1.0) l -= 1.0;
        const double phB = param (9);
        const double l2 = std::fmod (l + phB, 1.0);

        const bool clean = param (3) > 0.5f && ! (gr > 0.0f && param (10) > 0.5f);
        const bool interp = param (4) > 0.5f;
        const float smoothAmt = param (7), bits = param (6);
        auto read = [&] (double ph)
        {
            float r = 0.0f;
            if (! (clean && t->pyramid != nullptr && PZ::readPyramid (*t->pyramid, fi, ph, hz, sampleRate, r)))
                r = PZ::readFrames (t->frames, t->cycleSamples, fi, ph, interp);
            if (smoothAmt > 0.001f)
            {
                const float fadeLen = smoothAmt * 0.15f;
                float env = 1.0f;
                if (ph < (double) fadeLen)            env = (float) (ph / (double) fadeLen);
                else if (ph > 1.0 - (double) fadeLen) env = (float) ((1.0 - ph) / (double) fadeLen);
                r *= env;
            }
            return PZ::bitCrush (r, bits);
        };

        // the second oscillator of a Sound reads its phase-shifted cycle first (as Phizmo does)
        const bool second = param (0) > 0.5f;
        const float a = read (second ? l2 : l), b = read (second ? l : l2);
        const float w = param (8);
        const float mid = (a + b) * 0.5f, side = (a - b) * 0.5f;
        const float rawL = mid + side * w, rawR = mid - side * w;
        outputs[1] = { rawL, rawR };

        const float x = (juce::jlimit (-1.0f, 1.0f, param (2)) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const float gl = panMemoL (x, [] (float y) { return std::cos (y); });
        const float grr = panMemoR (x, [] (float y) { return std::sin (y); });
        outputs[0] = { rawL * amp * gl * 0.75f, rawR * amp * grr * 0.75f };
    }

    // the table: "phzosc1;<cycle>;<frames>;<base64 floats>"
    juce::String saveCustomState() const override
    {
        const auto t = std::atomic_load (&table);
        if (t == nullptr || t->frames.empty())
            return {};
        juce::MemoryOutputStream mo;
        for (const auto& f : t->frames)
            mo.write (f.data(), f.size() * sizeof (float));
        return "phzosc1;" + juce::String (t->cycleSamples) + ";" + juce::String ((int) t->frames.size()) + ";"
             + mo.getMemoryBlock().toBase64Encoding();
    }
    void loadCustomState (const juce::String& s) override
    {
        auto parts = juce::StringArray::fromTokens (s, ";", "");
        if (parts.size() < 4 || parts[0] != "phzosc1")
            return;
        const int cycle = parts[1].getIntValue(), n = parts[2].getIntValue();
        juce::MemoryBlock mb;
        if (cycle <= 0 || n <= 0 || ! mb.fromBase64Encoding (parts[3]) || mb.getSize() < (size_t) cycle * (size_t) n * sizeof (float))
            return;
        std::vector<std::vector<float>> frames ((size_t) n, std::vector<float> ((size_t) cycle));
        auto* src = static_cast<const float*> (mb.getData());
        for (int i = 0; i < n; ++i)
            std::memcpy (frames[(size_t) i].data(), src + (size_t) i * (size_t) cycle, sizeof (float) * (size_t) cycle);
        setTable (std::move (frames), cycle);
    }

private:
    struct Table
    {
        std::vector<std::vector<float>> frames;
        int cycleSamples { 1 };
        std::shared_ptr<const PZ::PyramidLevels> pyramid;
    };
    std::shared_ptr<const Table> table;
    double phase[kMaxVoices] {};
    int notes[kMaxVoices] {};
    Memo panMemoL, panMemoR;
};

//==============================================================================
// Phizmo - Filter: the 4-pole filter of a Sound's oscillator
//==============================================================================
class PhizmoFilterModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("filter", "Filter", ModuleSection::Filter, 3);
        d.description = "Phizmo's filter for one oscillator: 4-pole low-pass, band-pass or high-pass. Cutoff In moves "
                        "the cutoff by 5 octaves per unit at 100 % Depth (a Phizmo - Voice's Cutoff output carries the "
                        "filter envelope, keyboard, LFO and matrix). Level is the Sound's layer level.";
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("cutoffIn", "Cutoff In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("filterType", "Filter", { "LP 4-pole", "Band Pass", "High Pass" }, 0, 0, 2),
            knob ("filterFreq", "Cutoff", 20.0f, 20000.0f, 20000.0f, 0, "Hz", true),
            knob ("filterQ", "Reso", 0.1f, 12.0f, 0.707f, 1, "", true),
            knob ("depth", "Depth", -100.0f, 100.0f, 100.0f, 1, "%"),
            knob ("level", "Level", 0.0f, 1.0f, 1.0f, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& f : filt) f.reset(); }
    void voiceReset (int v) override { filt[v].reset(); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* outputs) override
    {
        const float oct = in[1][0] * kOctavesPerUnit * param (3) * 0.01f;
        const float hz = juce::jlimit (20.0f, 20000.0f, param (1) * std::exp2 (oct));
        filt[v].setType ((int) std::lround (param (0)), hz, juce::jmax (0.1f, param (2)), sampleRate);
        const float level = param (4);
        float l = filt[v].processL (in[0][0]), r = filt[v].processR (in[0][1]);
        if (! std::isfinite (l) || ! std::isfinite (r)) { filt[v].reset(); l = r = 0.0f; }
        outputs[0] = { l * level, r * level };
    }
    void processSample (const StereoFrame* in, StereoFrame* outputs) override { processVoiceSample (0, in, outputs); }

private:
    aqp_phizmo::StereoFilter4Pole filt[kMaxVoices];
};

//==============================================================================
// Phizmo - Ring: a Sound's ring modulation between its two oscillators
//==============================================================================
class PhizmoRingModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("ring", "Ring", ModuleSection::Effect, 4);
        d.description = "Phizmo's ring modulation of a Sound: oscillator 1 times oscillator 2 (their Raw Outs), at "
                        "the level of their Amps, blended into oscillator 1 by Ring; oscillator 2 fades as much.";
        d.sockets = { audioIn ("aIn", "Osc 1 In"), audioIn ("bIn", "Osc 2 In"), audioIn ("aRaw", "Raw 1"), audioIn ("bRaw", "Raw 2"),
                      modIn ("ampA", "Amp 1"), modIn ("ampB", "Amp 2"),
                      audioOut ("aOut", "Osc 1 Out"), audioOut ("bOut", "Osc 2 Out") };
        d.params = { knob ("ringMod", "Ring", 0.0f, 1.0f, 0.0f, 0) };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void processVoiceSample (int, const StereoFrame* in, StereoFrame* outputs) override
    {
        const float r = juce::jlimit (0.0f, 1.0f, param (0));
        const float envMix = (in[4][0] + in[5][0]) * 0.5f;
        const float wetL = in[2][0] * in[3][0] * envMix, wetR = in[2][1] * in[3][1] * envMix;
        const float comp = 1.0f + r * (1.4142f - 1.0f);
        outputs[0] = { (in[0][0] * (1.0f - r) + wetL * r) * comp, (in[0][1] * (1.0f - r) + wetR * r) * comp };
        outputs[1] = { in[1][0] * (1.0f - r) * comp, in[1][1] * (1.0f - r) * comp };
    }
    void processSample (const StereoFrame* in, StereoFrame* outputs) override { processVoiceSample (0, in, outputs); }
};

//==============================================================================
// Phizmo - Bus
//==============================================================================
class PhizmoBusModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("bus", "Bus", ModuleSection::Utility, 1);
        d.description = "Where Phizmo's Sounds meet: the dry, insert and reverb buses summed, Phizmo's noise "
                        "(through its own filter) added to the dry bus, each bus soft-clipped and set to the "
                        "output Gain - as the plugin does before its effects.";
        d.sockets = { audioIn ("dryIn", "Dry In"), audioIn ("insIn", "Insert In"), audioIn ("revIn", "Reverb In"),
                      audioOut ("dryOut", "Dry Out"), audioOut ("insOut", "Insert Out"), audioOut ("revOut", "Reverb Out") };
        d.params = {
            knob ("noiseLevel", "Noise", 0.0f, 1.0f, 0.0f, 0),
            makeCombo ("noiseFilterType", "N.Filter", { "Low Pass", "Band Pass", "High Pass" }, 0, 0, 2),
            knob ("noiseFreq", "N.Cutoff", 20.0f, 20000.0f, 4000.0f, 0, "Hz", true),
            knob ("noiseQ", "N.Reso", 0.3f, 12.0f, 0.7f, 1),
            knob ("gain", "Gain", 0.0f, 1.0f, 0.7f, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        gain.reset (sr, 0.05);
        gain.setCurrentAndTargetValue (param (4));
        noiseFilter.reset();
    }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        if (--counter < 0)
        {
            counter = 31;
            noiseFilter.setType ((int) std::lround (param (1)), param (2), juce::jmax (0.3f, param (3)), sampleRate);
        }
        float dl = in[0][0], dr = in[0][1];
        const float noise = param (0);
        if (noise > 0.0001f)
        {
            dl += noiseFilter.processL (aqp_phizmo::fastRand() * 2.f - 1.f) * noise * noise * 0.08f;
            dr += noiseFilter.processR (aqp_phizmo::fastRand() * 2.f - 1.f) * noise * noise * 0.08f;
        }
        gain.setTargetValue (param (4));
        const float g = gain.getNextValue();
        outputs[0] = { PZ::softClip (dl) * g, PZ::softClip (dr) * g };
        outputs[1] = { PZ::softClip (in[1][0]) * g, PZ::softClip (in[1][1]) * g };
        outputs[2] = { PZ::softClip (in[2][0]) * g, PZ::softClip (in[2][1]) * g };
    }

private:
    aqp_phizmo::StereoFilter4Pole noiseFilter;
    juce::SmoothedValue<float> gain;
    int counter { 0 };
};

//==============================================================================
// Phizmo - Insert FX
//==============================================================================
class PhizmoInsertModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("insert", "Insert FX", ModuleSection::Effect, 2);
        d.description = "Phizmo's insert effect: 41 algorithms (EQ, reverbs, choruses, flangers, delays, "
                        "distortions, speakers, chatter box, vocoder and their combinations) with Variation, "
                        "Mix and F, the real-time effect-mod macro.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        juce::StringArray algos;
        for (int a = 0; a < phz::PhizmoInsert::NumAlgos; ++a)
            algos.add (phz::PhizmoInsert::algoName (a));
        d.params = {
            makeCombo ("fxAlgo", "Effect", algos, 0, 0, 3),
            knob ("fxVariation", "Variation", 0.0f, 1.0f, 0.35f, 1),
            knob ("fxMix", "Mix", 0.0f, 1.0f, 0.35f, 1),
            knob ("phizF", "F", -1.0f, 1.0f, 0.0f, 1)
        };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); fx.prepare (sr); }
    void reset() override { fx.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        float l = in[0][0], r = in[0][1];
        fx.process (l, r, (int) std::lround (param (0)), param (1), param (2), param (3));
        out[0] = { l, r };
    }
private:
    phz::PhizmoInsert fx;
};

//==============================================================================
// Phizmo - Reverb
//==============================================================================
class PhizmoReverbModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("reverb", "Reverb", ModuleSection::Effect, 3);
        d.description = "Phizmo's global reverb (an 8-line FDN) in its eight variations, as a send: Reverb Out "
                        "is the wet return at Amount, for the Phizmo Output.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        juce::StringArray vars;
        for (int v = 0; v < 8; ++v)
            vars.add (phz::PhizmoReverb::variationName (v));
        d.params = { makeCombo ("revVariation", "Variation", vars, 3, 0, 3),
                     knob ("revAmount", "Amount", 0.0f, 1.0f, 0.25f, 0) };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); fx.prepare (sr); lastVar = -1; }
    void reset() override { fx.reset(); }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int var = (int) std::lround (param (0));
        if (var != lastVar) { fx.setVariation (var); lastVar = var; }
        float l = in[0][0], r = in[0][1];
        fx.process (l, r, 1.0f);
        const float amount = juce::jlimit (0.0f, 1.0f, param (1));
        out[0] = { l * amount, r * amount };
    }
private:
    phz::PhizmoReverb fx;
    int lastVar { -1 };
};

//==============================================================================
// Phizmo - Output
//==============================================================================
class PhizmoOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("output", "Output", ModuleSection::Utility, 4);
        d.description = "Phizmo's end: the dry bus, the insert effect's return and the reverb return summed, "
                        "through the plugin's output limiter (fast attack, slow release, stereo-linked).";
        d.sockets = { audioIn ("dryIn", "Dry In"), audioIn ("insIn", "Insert In"), audioIn ("revIn", "Reverb In"),
                      audioOut ("audioOut", "Audio Out") };
        return d;
    }
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void reset() override { limGain = 1.0f; }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        float l = in[0][0] + in[1][0] + in[2][0];
        float r = in[0][1] + in[1][1] + in[2][1];
        const float peak = juce::jmax (std::abs (l), std::abs (r));
        constexpr float kCeiling = 0.98f;
        const float target = peak > kCeiling ? kCeiling / peak : 1.0f;
        limGain += (target < limGain ? 0.35f : 0.0008f) * (target - limGain);
        limGain = juce::jlimit (0.05f, 1.0f, limGain);
        out[0] = { l * limGain, r * limGain };
    }
private:
    float limGain { 1.0f };
};

AQUANODE_REGISTER_MODULE (PhizmoSoundModule,  PhizmoSoundModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoVoiceModule,  PhizmoVoiceModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoOscModule,    PhizmoOscModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoFilterModule, PhizmoFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoRingModule,   PhizmoRingModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoBusModule,    PhizmoBusModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoInsertModule, PhizmoInsertModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoReverbModule, PhizmoReverbModule::descriptor)
AQUANODE_REGISTER_MODULE (PhizmoOutputModule, PhizmoOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::phizmo
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        auto* src = dynamic_cast<PZ*> (&original);
        auto P = [&] (const juce::String& id, float def) { return kit::param (original, id, def); };

        const int busM = b.add ("plugin.phizmo.bus", 1300, 200);
        b.copy (busM, original);
        const int ins = b.add ("plugin.phizmo.insert", 1650, 0);
        b.copy (ins, original);
        const int rev = b.add ("plugin.phizmo.reverb", 1650, 400);
        b.copy (rev, original);
        const int outM = b.add ("plugin.phizmo.output", 2000, 200);

        // the O macro, folded into what it is assigned to (reverb or effect mix)
        const float phizO = P ("phizO", 0.0f);
        const int oDest = (int) std::lround (P ("phizODest", 0.0f));
        if (oDest == 0) b.set (rev, "revAmount", juce::jlimit (0.0f, 1.0f, P ("revAmount", 0.25f) + phizO * 0.5f));
        if (oDest == 4) b.set (ins, "fxMix", juce::jlimit (0.0f, 1.0f, P ("fxMix", 0.35f) + phizO * 0.5f));

        b.cable (busM, "dryOut", outM, "dryIn");
        b.cable (busM, "insOut", ins, "audioIn");
        b.cable (ins, "audioOut", outM, "insIn");
        b.cable (busM, "revOut", rev, "audioIn");
        b.cable (rev, "audioOut", outM, "revIn");

        juce::MemoryBlock state;
        if (src != nullptr)
            src->getStateInformation (state);

        PatchPorts ports;
        ports.name = "Phizmo";
        ports.audioOut = { { outM, "audioOut" } };

        // each Sound as its Voice, two Oscs, the Ring (when it rings), two Filters
        int y = 0;
        for (int k = 0; k < kSounds; ++k)
        {
            if (P ("soundOn" + juce::String (k), k == 0 ? 1.0f : 0.0f) < 0.5f)
                continue;
            const juce::String ks (k);
            const int voice = b.add ("plugin.phizmo.voice", 0, y);
            auto* vm = dynamic_cast<PhizmoVoiceModule*> (b.dsp (voice));
            if (vm == nullptr || vm->plugin() == nullptr)
                continue;

            // the whole Preset (matrix, curves) controlling Sound k
            if (state.getSize() > 0)
                vm->plugin()->setStateInformation (state.getData(), (int) state.getSize());
            b.set (voice, "sound", (float) k);
            vm->useSound (k);
            if (src != nullptr)
            {
                src->setEditSound (k);
                b.copy (voice, original, [k] (const juce::String& id) { return voiceParamFor (id, k); });
                b.set (voice, "sound", (float) k);
            }

            const bool rings = P ("ringMod", 0.0f) > 1.0e-4f;
            int osc[2], filt[2];
            for (int o = 0; o < 2; ++o)
            {
                osc[o] = b.add ("plugin.phizmo.osc", 380, y + o * 330);
                b.set (osc[o], "osc", (float) o);
                b.set (osc[o], "pan", P (o == 0 ? "panA" : "panB", 0.0f));
                b.set (osc[o], "grit", P ("grit", 0.0f));
                b.set (osc[o], "bits", P ("bitCrush", 16.0f));
                b.set (osc[o], "smooth", P (o == 0 ? "wtSmooth" : "wtSmoothB", 0.0f));
                b.set (osc[o], "width", P ("stereoWidth", 0.5f));
                b.set (osc[o], "phase", P ("stereoPhase", 0.0f));
                if (src != nullptr)
                {
                    b.set (osc[o], "engine", src->bandlimitedEngine() ? 1.0f : 0.0f);
                    b.set (osc[o], "interp", src->frameInterpolation (k) ? 1.0f : 0.0f);
                    b.set (osc[o], "gritVintage", src->gritForcesVintage() ? 1.0f : 0.0f);

                    // its table; a Sound without a wave of its own plays Sound 1's
                    std::vector<std::vector<float>> frames;
                    int cycle = 0;
                    if (src->copySlot (k * 2 + o, frames, cycle) || src->copySlot (o, frames, cycle))
                        if (auto* om = dynamic_cast<PhizmoOscModule*> (b.dsp (osc[o])))
                            om->setTable (std::move (frames), cycle);
                }
                b.cable (voice, o == 0 ? "pitchA" : "pitchB", osc[o], "pitchIn");
                b.cable (voice, o == 0 ? "posA" : "posB", osc[o], "posIn");
                b.cable (voice, o == 0 ? "ampA" : "ampB", osc[o], "ampIn");

                filt[o] = b.add ("plugin.phizmo.filter", rings ? 1100 : 760, y + o * 330);
                b.set (filt[o], "filterType", P ("filterType", 0.0f));
                b.set (filt[o], "filterFreq", P ("filterFreq", 20000.0f));
                b.set (filt[o], "filterQ", P ("filterQ", 0.707f));
                b.set (filt[o], "level", P ("soundGain" + ks, 1.0f));
                b.cable (voice, o == 0 ? "cutA" : "cutB", filt[o], "cutoffIn");

                // the filter's way out: insert or dry, and always the reverb
                const bool insert = (int) std::lround (P ((o == 0 ? "fxBusA" : "fxBusB") + ks, 0.0f)) == 0;
                b.cable (filt[o], "audioOut", busM, insert ? "insIn" : "dryIn");
                b.cable (filt[o], "audioOut", busM, "revIn");
            }

            if (rings)
            {
                const int ring = b.add ("plugin.phizmo.ring", 760, y + 120);
                b.set (ring, "ringMod", P ("ringMod", 0.0f));
                b.cable (osc[0], "audioOut", ring, "aIn");
                b.cable (osc[1], "audioOut", ring, "bIn");
                b.cable (osc[0], "rawOut", ring, "aRaw");
                b.cable (osc[1], "rawOut", ring, "bRaw");
                b.cable (voice, "ampA", ring, "ampA");
                b.cable (voice, "ampB", ring, "ampB");
                b.cable (ring, "aOut", filt[0], "audioIn");
                b.cable (ring, "bOut", filt[1], "audioIn");
            }
            else
            {
                b.cable (osc[0], "audioOut", filt[0], "audioIn");
                b.cable (osc[1], "audioOut", filt[1], "audioIn");
            }
            y += 1520;   // the Voice card is about 1440 tall
        }
        if (src != nullptr)
            src->setEditSound (0);
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* pz = dynamic_cast<PZ*> (&original);
        return pz != nullptr && pz->loadPreset (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
