//==============================================================================
// Reflex - Patch version
//
// Reflex is one signal path: every voice (three oscillators with their fat
// copies, the ladder filter in its thirteen types, two envelopes, three LFOs,
// glide, vibrato, the arpeggiator) is summed at the amp's drive, volume and
// pan into the trance gate and the output EQ, then into the delay and the
// reverb. The patch version splits it before the delay:
//
//   Reflex - Voice  --audio-->  Reflex - Effects  (delay, reverb)
//
// Both modules run the plugin's own engine (one with the delay and reverb
// left out, one with only them), so the sound is the plugin's. Their knobs
// are the plugin's own parameters and take cables; the whole patch travels
// in each module's state.
//==============================================================================
#include "PluginReflexConfig.h"

namespace aqp_reflex
{
    #include AQP_REFLEX_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "reflex" };
    using RP = aqp_reflex::ReflexProcessor;
    namespace rx = aqp_reflex::rfx;
    constexpr int kBlock = 32;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.reflex." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    struct KnobDef { int index; const char* label; int row; };

    ParamSpec specFor (const KnobDef& k)
    {
        float init[rx::NumParams]; rx::initPatch (init);
        return makeRotary ("p" + juce::String (k.index), k.label, 0.0f, 1.0f, init[k.index], k.row);
    }

    const std::vector<KnobDef>& voiceKnobs()
    {
        using namespace rx;
        static const std::vector<KnobDef> k {
            { OSC1, "Osc 1", 0 }, { OSC1sm, "Semi", 0 }, { OSC1vl, "Level", 0 }, { OSC2, "Osc 2", 0 }, { OSC2sm, "Semi", 0 }, { OSC2vl, "Level", 0 },
            { OSC3, "Osc 3", 1 }, { OSC3sm, "Semi", 1 }, { OSC3vl, "Level", 1 }, { OSCfat, "Fat", 1 }, { FATvcs, "Fat Voices", 1 },
            { OSCage, "Drift", 1 }, { OSCtrg, "Retrig", 1 },

            { FLTtp, "Filter", 2 }, { FLTf, "Cutoff", 2 }, { FLTQ, "Reso", 2 }, { VelCut, "Vel>Cut", 2 }, { KeyCut, "Key>Cut", 2 },

            { Attack, "E1 Att", 3 }, { Decay, "E1 Dec", 3 }, { Sustain, "E1 Sus", 3 }, { Release, "E1 Rel", 3 },
            { EGVol, ">Vol", 3 }, { EGPWM, ">PWM", 3 }, { EGFlt, ">Cut", 3 }, { EGDet, ">Det", 3 },
            { Attack2, "E2 Att", 4 }, { Decay2, "E2 Dec", 4 }, { Sustain2, "E2 Sus", 4 }, { Release2, "E2 Rel", 4 },
            { EG2Res, ">Res", 4 }, { EG2PWM, ">PWM", 4 }, { EG2Flt, ">Cut", 4 }, { EG2Ptc, ">Pitch", 4 },

            { LFO1frq, "LFO 1", 5 }, { LFO1De, ">Det", 5 }, { LFO1PW, ">PWM", 5 }, { LFO1Fl, ">Cut", 5 }, { LFO1trg, "Retrig", 5 },
            { LFO2frq, "LFO 2", 6 }, { LFO2De, ">Det", 6 }, { LFO2PW, ">PWM", 6 }, { LFO2Fl, ">Cut", 6 }, { LFO2trg, "Retrig", 6 },
            { LFO3frq, "LFO 3", 7 }, { LFO3De, ">Det", 7 }, { LFO3PW, ">PWM", 7 }, { LFO3Fl, ">Cut", 7 }, { LFO3trg, "Retrig", 7 },

            { ARPsta, "Arp", 8 }, { ARPmod, "Mode", 8 }, { ARPtme, "Speed", 8 }, { ARPgte, "Gate", 8 }, { ARPoct, "Octaves", 8 }, { ARPtrg, "Retrig", 8 },
            { GLDst, "Glide", 9 }, { GLDspd, "Time", 9 }, { PBrng, "Bend", 9 }, { VIBspd, "Vib Rate", 9 }, { VIBdpt, "Vib Depth", 9 },
            { Voices, "Voices", 9 }, { dummy13, "Mono", 9 },

            { AMPdrv, "Drive", 10 }, { AMPvol, "Volume", 10 }, { AMPpan, "Pan", 10 }, { AMPspd, "Spread", 10 }, { VelVol, "Vel>Vol", 10 },
            { dummy6, "Master", 10 }, { dummy18, "Classic EQ", 10 },
            { TGstat, "Gate", 11 }, { TGcont, "Contour", 11 }, { TGspd, "Speed", 11 }, { TGpat, "Pattern", 11 }, { TGstr, "Stereo", 11 }, { TGmix, "Mix", 11 }
        };
        return k;
    }

    const std::vector<KnobDef>& effectKnobs()
    {
        using namespace rx;
        static const std::vector<KnobDef> k {
            { DLYsta, "Delay", 0 }, { DLYmod, "Mode", 0 }, { DLYmix, "Mix", 0 }, { DLYtme, "Time", 0 }, { DLYfbk, "Feedback", 0 }, { DLYdmp, "Damp", 0 },
            { REVsta, "Reverb", 1 }, { REVmix, "Mix", 1 }, { REVpdl, "Predelay", 1 }, { REVroo, "Room", 1 }, { REVdmp, "Damp", 1 }, { REVwdt, "Width", 1 }
        };
        return k;
    }

    //==========================================================================
    // What both modules share: a Reflex instance, knobs linked to its
    // parameters, its whole state saved with the module.
    class ReflexHostModule : public SynthModule
    {
    public:
        ReflexHostModule()
        {
            auto p = createOriginal (kPlugin, 48000.0);
            rp.reset (dynamic_cast<RP*> (p.get()));
            if (rp != nullptr)
                p.release();
        }

        VoiceMode voiceMode() const override { return VoiceMode::Global; }

        juce::String saveCustomState() const override
        {
            if (rp == nullptr) return {};
            juce::MemoryBlock mb;
            rp->getStateInformation (mb);
            return mb.toBase64Encoding();
        }
        void loadCustomState (const juce::String& s) override
        {
            juce::MemoryBlock mb;
            if (rp != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
                setPluginState (mb);
        }

        void setPluginState (const juce::MemoryBlock& mb)
        {
            if (rp == nullptr) return;
            rp->setStateInformation (mb.getData(), (int) mb.getSize());
            for (auto& l : links) { l.last = -1.0f; l.raw = std::numeric_limits<float>::quiet_NaN(); }
        }

        RP* plugin() const { return rp.get(); }

    protected:
        void prepareHost (double sr, int split)
        {
            if (rp == nullptr) return;
            const juce::ScopedLock sl (rp->getCallbackLock());
            rp->releaseResources();
            rp->split = split;
            rp->setPlayConfigDetails (0, 2, sr, kBlock);
            rp->prepareToPlay (sr, kBlock);
        }

        void relink()
        {
            links.clear();
            if (rp == nullptr) return;
            const auto& d = getDescriptor();
            for (int i = 0; i < (int) d.params.size(); ++i)
                if (auto* prm = kit::findParam (*rp, d.params[(size_t) i].id))
                    links.push_back ({ prm, i, -1.0f });
        }
        void pushKnobs()
        {
            for (auto& l : links)
            {
                const float raw = param (l.knob);
                if (juce::exactlyEqual (raw, l.raw)) continue;
                l.raw = raw;
                const float v = l.param->convertTo0to1 (raw);
                if (v != l.last) { l.param->setValueNotifyingHost (v); l.last = v; }
            }
        }

        std::unique_ptr<RP> rp;

    private:
        struct Link { juce::RangedAudioParameter* param; int knob; float last; float raw = std::numeric_limits<float>::quiet_NaN(); };
        std::vector<Link> links;
    };
}

//==============================================================================
// Reflex - Voice
//==============================================================================
class ReflexVoiceModule final : public ReflexHostModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("voice", "Voice", ModuleSection::Oscillator, 0);
        d.description = "Reflex's voices, played by the plugin's own engine: three oscillators with their fat copies "
                        "and drift, the ladder filter in its thirteen types, two envelopes, three LFOs, glide, vibrato "
                        "and the arpeggiator, through the amp's drive, the trance gate and the output EQ. The delay and "
                        "the reverb are in Reflex - Effects. The whole patch comes with the module (saved in the patch).";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("audioOut", "Audio Out") };
        for (const auto& k : voiceKnobs())
            d.params.push_back (specFor (k));
        return d;
    }

    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override { midiQueue.addEvent (m, pos); }
    void midiAllNotesOff() override { midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        prepareHost (sr, 1);
        relink();
        midiQueue.ensureSize (1024); notes.ensureSize (1024);
        io.setSize (2, kBlock);
        io.clear();
        for (auto& o : out) o.fill (0.0f);
        pos = 0;
    }

    void reset() override { midiQueue.clear(); midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0); }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

private:
    void runBlock()
    {
        if (rp == nullptr)
            return;
        pushKnobs();
        io.clear();
        // all-notes-off goes to the engine straight away: Reflex handles the events that share a frame
        // last to first (as the original does with chords), so one queued with a note would cut it
        bool allOff = false;
        notes.clear();
        for (const auto meta : midiQueue)
        {
            const auto m = meta.getMessage();
            if (m.isAllNotesOff() || m.isAllSoundOff()) allOff = true;
            else notes.addEvent (m, meta.samplePosition);
        }
        bool rendered = false;
        {
            const juce::ScopedTryLock sl (rp->getCallbackLock());
            if (sl.isLocked())
            {
                if (allOff) rp->allNotesOffNow();
                rp->split = 1;
                rp->tempoOverride = tempoBpm;
                rp->processBlock (io, notes);
                rendered = true;
            }
        }
        midiQueue.clear();
        for (int c = 0; c < 2; ++c)
        {
            const float* s = io.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
                out[(size_t) c][(size_t) i] = rendered && std::isfinite (s[i]) ? s[i] : 0.0f;
        }
    }

    juce::MidiBuffer midiQueue, notes;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> out {};
    int pos { 0 };
};

//==============================================================================
// Reflex - Effects
//==============================================================================
class ReflexEffectsModule final : public ReflexHostModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("effects", "Effects", ModuleSection::Effect, 1);
        d.description = "Reflex's delay (mono, stereo, ping-pong and cross, synced to the patch tempo) and its "
                        "reverb, run by the plugin's own engine. Audio In takes Reflex - Voice, or anything.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        for (const auto& k : effectKnobs())
            d.params.push_back (specFor (k));
        return d;
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        prepareHost (sr, 2);
        relink();
        io.setSize (2, kBlock);
        io.clear();
        for (auto& o : out) o.fill (0.0f);
        for (auto& b : inBuf) b.fill (0.0f);
        pos = 0;
    }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        inBuf[0][(size_t) pos] = in[0][0];
        inBuf[1][(size_t) pos] = in[0][1];
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

private:
    void runBlock()
    {
        if (rp == nullptr)
            return;
        pushKnobs();
        for (int c = 0; c < 2; ++c)
            io.copyFrom (c, 0, inBuf[(size_t) c].data(), kBlock);
        bool rendered = false;
        {
            const juce::ScopedTryLock sl (rp->getCallbackLock());
            if (sl.isLocked())
            {
                rp->split = 2;
                rp->tempoOverride = tempoBpm;
                rp->processBlock (io, noMidi);
                rendered = true;
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            const float* s = io.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
                out[(size_t) c][(size_t) i] = rendered && std::isfinite (s[i]) ? s[i] : 0.0f;
        }
    }

    juce::MidiBuffer noMidi;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> inBuf {}, out {};
    int pos { 0 };
};

AQUANODE_REGISTER_MODULE (ReflexVoiceModule,   ReflexVoiceModule::descriptor)
AQUANODE_REGISTER_MODULE (ReflexEffectsModule, ReflexEffectsModule::descriptor)

//==============================================================================
namespace aquanode::plugins::reflex
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        juce::MemoryBlock state;
        original.getStateInformation (state);

        const int voice = b.add ("plugin.reflex.voice", 0, 0);
        const int fx = b.add ("plugin.reflex.effects", 1500, 0);

        for (const int m : { voice, fx })
            if (auto* h = dynamic_cast<ReflexHostModule*> (b.dsp (m)))
                if (auto* p = h->plugin())
                {
                    h->setPluginState (state);
                    b.copy (m, *p);   // the knobs: the plugin's own parameters
                }

        b.cable (voice, "audioOut", fx, "audioIn");

        PatchPorts ports;
        ports.name = "Reflex";
        ports.midiIn = { { voice, "midiIn" } };
        ports.audioOut = { { fx, "audioOut" } };
        return ports;
    }

    // the original's .fxb banks and .fxp programs, and Reflex's own .xml presets
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* r = dynamic_cast<RP*> (&original);
        if (r == nullptr) return false;
        juce::String error;
        if (file.hasFileExtension ("xml")) return r->loadXmlPreset (file, error);
        return r->loadFxbOrFxp (file, error);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
