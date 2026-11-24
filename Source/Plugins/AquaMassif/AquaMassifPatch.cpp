//==============================================================================
// Massif - Patch version
//
// Massif is one signal path: every voice (three wavetable oscillators,
// the modulation oscillator, noise, feedback, two filters, two inserts, amp
// and pan, with its four envelopes and four LFO / stepper / performer pages)
// is summed into two master effects, the EQ and the master volume; the bypass
// bus carries one source around the filters to a point in that chain. The
// modulation reaches past the voices too - the eight effect knobs take it.
// The patch version splits it exactly there:
//
//   Massif - Voice  --audio----------------------> Massif - Effects
//                        --bypass bus-----------------> (FX 1, FX 2, EQ, master)
//                        --8 effect-knob modulations-->
//
// Both modules run the plugin's own engine (one with the effects left out,
// one with the voices left out), so the sound is the plugin's. Their knobs
// are the plugin's own parameters and take cables; everything else of the
// sound (the modulation slots, envelope loops, LFO shapes, steps, macro
// names, generated wavetables) travels in each module's state.
//
// Not split further: the voice's modulation (four envelopes, four pages,
// macros, each onto any knob with a side chain) belongs to every stage of
// the voice at once. Modulation of the EQ and the master volume by the
// envelopes and pages stays behind (macros are knobs of the Voice).
//==============================================================================
#include "PluginAquaMassifConfig.h"

namespace aqp_aquamassif
{
    #include AQP_AQUAMASSIF_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "aquamassif" };
    using AM = aqp_aquamassif::AquaMassifProcessor;
    namespace mf = aqp_aquamassif::massif;
    constexpr int kMods = 8;
    constexpr int kBlock = 32;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.aquamassif." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    // a knob of the plugin (k<index>), a selector (its own options), a switch (...On) or a count
    struct KnobDef { const char* id; const char* label; int row; };

    ParamSpec specFor (const KnobDef& k)
    {
        const juce::String id (k.id);
        if (id.startsWithChar ('k') && id.substring (1).containsOnly ("0123456789"))
        {
            const int index = id.substring (1).getIntValue();
            return makeRotary (id, k.label, 0.0f, 1.0f, mf::Patch().knob[(size_t) juce::jlimit (0, mf::kNumKnobs - 1, index)], k.row);
        }
        if (id == "voices") return makeRotary (id, k.label, 1.0f, 64.0f, 16.0f, k.row, "", false, 1.0f);
        if (id == "unison") return makeRotary (id, k.label, 1.0f, 16.0f, 1.0f, k.row, "", false, 1.0f);
        const auto choices = AM::choiceList (id);
        if (choices.size() > 0)
            return makeCombo (id, k.label, choices, AM::choiceDefault (id), k.row, 2);
        return kit::makeOnOff (id, k.label, id != "eqOn" && ! id.startsWith ("uni") && id != "vibMono", k.row);
    }

    const std::vector<KnobDef>& voiceKnobs()
    {
        static const std::vector<KnobDef> k {
            { "osc1On", "Osc 1", 0 }, { "wt1", "Table", 0 }, { "mode1", "Mode", 0 }, { "k8", "Pitch", 0 }, { "k9", "Position", 0 },
            { "k10", "Intensity", 0 }, { "k11", "Amp", 0 }, { "k12", "F1/F2", 0 },
            { "osc2On", "Osc 2", 1 }, { "wt2", "Table", 1 }, { "mode2", "Mode", 1 }, { "k13", "Pitch", 1 }, { "k14", "Position", 1 },
            { "k15", "Intensity", 1 }, { "k16", "Amp", 1 }, { "k17", "F1/F2", 1 },
            { "osc3On", "Osc 3", 2 }, { "wt3", "Table", 2 }, { "mode3", "Mode", 2 }, { "k18", "Pitch", 2 }, { "k19", "Position", 2 },
            { "k20", "Intensity", 2 }, { "k21", "Amp", 2 }, { "k22", "F1/F2", 2 },

            { "modOscOn", "Mod Osc", 3 }, { "k28", "Pitch", 3 }, { "k29", "Ring Mod", 3 }, { "rmT", "RM >", 3 }, { "k30", "Phase", 3 },
            { "pmT", "PM >", 3 }, { "k31", "Position", 3 }, { "posT", "Pos >", 3 }, { "k32", "Filter FM", 3 }, { "fmT", "FM >", 3 },

            { "noiseOn", "Noise", 4 }, { "noiseType", "Type", 4 }, { "k23", "Color", 4 }, { "k24", "Amp", 4 }, { "k25", "F1/F2", 4 },
            { "fbOn", "Feedback", 4 }, { "fbPos", "From", 4 }, { "k26", "Amp", 4 }, { "k27", "F1/F2", 4 },

            { "f1On", "Filter 1", 5 }, { "f1Type", "Type", 5 }, { "k33", "Cutoff", 5 }, { "k34", "Bandwidth", 5 }, { "k35", "Reso", 5 },
            { "k52", "Out", 5 }, { "k39", "Par/Ser", 5 }, { "k54", "F2/F1 Mix", 5 },
            { "f2On", "Filter 2", 6 }, { "f2Type", "Type", 6 }, { "k36", "Cutoff", 6 }, { "k37", "Bandwidth", 6 }, { "k38", "Reso", 6 },
            { "k53", "Out", 6 },

            { "i1On", "Insert 1", 7 }, { "i1Type", "Type", 7 }, { "i1Pos", "At", 7 }, { "k40", "Knob 1", 7 }, { "k41", "Knob 2", 7 },
            { "i2On", "Insert 2", 8 }, { "i2Type", "Type", 8 }, { "i2Pos", "At", 8 }, { "k42", "Knob 1", 8 }, { "k43", "Knob 2", 8 },

            { "ampOn", "Amp", 9 }, { "k56", "Pan", 9 }, { "bypassOn", "Bypass", 9 }, { "bypassSrc", "Source", 9 }, { "k55", "Level", 9 },
            { "voices", "Voices", 9 }, { "unison", "Unison", 9 }, { "voiceMode", "Mode", 9 }, { "trigger", "Trigger", 9 },

            { "k126", "Glide", 10 }, { "k127", "Vib Rate", 10 }, { "k128", "Vib Depth", 10 }, { "uniPitchOn", "U.Pitch", 10 }, { "k129", "Spread", 10 },
            { "uniWtOn", "U.Wt", 10 }, { "k130", "Spread", 10 }, { "uniPanOn", "U.Pan", 10 }, { "k131", "Spread", 10 },

            { "k0", "Macro 1", 11 }, { "k1", "Macro 2", 11 }, { "k2", "Macro 3", 11 }, { "k3", "Macro 4", 11 },
            { "k4", "Macro 5", 11 }, { "k5", "Macro 6", 11 }, { "k6", "Macro 7", 11 }, { "k7", "Macro 8", 11 },

            { "k94", "E1 Att", 12 }, { "k102", "E1 Dec", 12 }, { "k106", "E1 Level", 12 }, { "k122", "E1 Rel", 12 },
            { "k95", "E2 Att", 12 }, { "k103", "E2 Dec", 12 }, { "k107", "E2 Level", 12 }, { "k123", "E2 Rel", 12 },
            { "k96", "E3 Att", 13 }, { "k104", "E3 Dec", 13 }, { "k108", "E3 Level", 13 }, { "k124", "E3 Rel", 13 },
            { "k97", "E4 Att", 13 }, { "k105", "E4 Dec", 13 }, { "k109", "E4 Level", 13 }, { "k125", "E4 Rel", 13 },

            { "mod5Type", "Mod 5", 14 }, { "k58", "Rate", 14 }, { "k62", "Amp", 14 }, { "k66", "Morph", 14 },
            { "mod6Type", "Mod 6", 14 }, { "k59", "Rate", 14 }, { "k63", "Amp", 14 }, { "k67", "Morph", 14 },
            { "mod7Type", "Mod 7", 15 }, { "k60", "Rate", 15 }, { "k64", "Amp", 15 }, { "k68", "Morph", 15 },
            { "mod8Type", "Mod 8", 15 }, { "k61", "Rate", 15 }, { "k65", "Amp", 15 }, { "k69", "Morph", 15 }
        };
        return k;
    }

    const std::vector<KnobDef>& effectKnobs()
    {
        static const std::vector<KnobDef> k {
            { "fx1On", "FX 1", 0 }, { "fx1Type", "Type", 0 }, { "k44", "Dry/Wet", 0 }, { "k45", "Knob 2", 0 }, { "k46", "Knob 3", 0 }, { "k47", "Knob 4", 0 },
            { "fx2On", "FX 2", 1 }, { "fx2Type", "Type", 1 }, { "k48", "Dry/Wet", 1 }, { "k49", "Knob 2", 1 }, { "k50", "Knob 3", 1 }, { "k51", "Knob 4", 1 },
            { "eqOn", "EQ", 2 }, { "k132", "Low", 2 }, { "k133", "Peak", 2 }, { "k134", "Freq", 2 }, { "k135", "High", 2 },
            { "bypassTgt", "Bypass In", 3 }, { "k57", "Master", 3 }
        };
        return k;
    }

    const char* modName (int m)
    {
        static const char* names[kMods] = { "FX1 Wet", "FX1 K2", "FX1 K3", "FX1 K4", "FX2 Wet", "FX2 K2", "FX2 K3", "FX2 K4" };
        return names[m];
    }
    juce::String modId (int m) { return "modFx" + juce::String (m); }

    //==========================================================================
    // What both modules share: an Massif instance, knobs linked to its
    // parameters, its whole state saved with the module.
    class MassifHostModule : public SynthModule
    {
    public:
        MassifHostModule()
        {
            auto p = createOriginal (kPlugin, 48000.0);
            am.reset (dynamic_cast<AM*> (p.get()));
            if (am != nullptr)
                p.release();
        }

        VoiceMode voiceMode() const override { return VoiceMode::Global; }

        juce::String saveCustomState() const override
        {
            if (am == nullptr) return {};
            juce::MemoryBlock mb;
            am->getStateInformation (mb);
            return mb.toBase64Encoding();
        }
        void loadCustomState (const juce::String& s) override
        {
            juce::MemoryBlock mb;
            if (am != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
                setPluginState (mb);
        }

        void setPluginState (const juce::MemoryBlock& mb)
        {
            if (am == nullptr) return;
            am->setStateInformation (mb.getData(), (int) mb.getSize());
            for (auto& l : links) { l.last = -1.0f; l.raw = std::numeric_limits<float>::quiet_NaN(); }
        }

        AM* plugin() const { return am.get(); }

    protected:
        void prepareHost (double sr, int split)
        {
            if (am == nullptr) return;
            const juce::ScopedLock sl (am->getCallbackLock());
            am->releaseResources();
            am->split = split;
            am->setPlayConfigDetails (0, 2, sr, kBlock);
            am->prepareToPlay (sr, kBlock);
        }

        // the module's knobs -> the plugin's parameters (only on change)
        void relink()
        {
            links.clear();
            if (am == nullptr) return;
            const auto& d = getDescriptor();
            for (int i = 0; i < (int) d.params.size(); ++i)
                if (auto* prm = kit::findParam (*am, d.params[(size_t) i].id))
                    links.push_back ({ prm, i, -1.0f });
        }
        // (6.9.14) section cards into Cards In
        bool takesCardLinks() const override { return getDescriptor().inputIndexOf ("cardsIn") >= 0; }
        void setCardLinks (std::vector<std::shared_ptr<CardLink>> l) override { cards.set (getDescriptor(), std::move (l)); }

        void pushKnobs()
        {
            const juce::SpinLock::ScopedTryLockType cardLock (cards.lock);
            for (auto& l : links)
            {
                float raw = param (l.knob);   // unmoved knob: nothing to convert or send
                if (cardLock.isLocked())
                    cards.read (*this, l.knob, false, raw);   // (a section card holds it)
                if (juce::exactlyEqual (raw, l.raw)) continue;
                l.raw = raw;
                const float v = l.param->convertTo0to1 (raw);
                if (v != l.last) { l.param->setValueNotifyingHost (v); l.last = v; }
            }
        }

        std::unique_ptr<AM> am;

    private:
        struct Link { juce::RangedAudioParameter* param; int knob; float last; float raw = std::numeric_limits<float>::quiet_NaN(); };
        std::vector<Link> links;
        kit::CardReceiver cards;
    };
}

//==============================================================================
// Massif - Voice
//==============================================================================
class AquaMassifVoiceModule : public MassifHostModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("voice", "Voice", ModuleSection::Oscillator, 0);
        d.description = "Massif's voices, played by the plugin's own engine: three wavetable oscillators with "
                        "their modes, the modulation oscillator, noise and feedback, two filters in serial / parallel, "
                        "two inserts, amp and pan, with four envelopes, four LFO / stepper / performer pages, the "
                        "macros and unison - summed as the plugin hands them to its effects. Bypass Out is the bypass "
                        "bus; the eight Mod outputs are the modulation of the effect knobs, for the Massif "
                        "Effects. The modulation slots, loops, shapes, steps and generated wavetables come with the "
                        "module (saved in the patch).";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("audioOut", "Audio Out"), audioOut ("bypassOut", "Bypass Out") };
        for (int m = 0; m < kMods; ++m)
            d.sockets.push_back (modOut (modId (m), juce::String ("Mod ") + modName (m)));
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
        prepareHost (sr, AM::splitVoices);
        relink();
        midiQueue.ensureSize (1024);
        io.setSize (2, kBlock);
        io.clear();
        for (auto& o : out) o.fill (0.0f);
        for (auto& o : byp) o.fill (0.0f);
        mods.fill (0.0f);
        pos = 0;
    }

    void reset() override { midiQueue.clear(); midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0); }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        outputs[1] = { byp[0][(size_t) pos], byp[1][(size_t) pos] };
        for (int m = 0; m < kMods; ++m)
            outputs[2 + m] = { mods[(size_t) m], mods[(size_t) m] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

private:
    void runBlock()
    {
        if (am == nullptr)
            return;
        pushKnobs();
        io.clear();

        bool rendered = false;
        {
            const juce::ScopedTryLock sl (am->getCallbackLock());
            if (sl.isLocked())
            {
                am->split = AM::splitVoices;
                am->tempoOverride = tempoBpm;
                am->processBlock (io, midiQueue);
                rendered = true;
            }
        }
        midiQueue.clear();

        for (int c = 0; c < 2; ++c)
        {
            const float* s = io.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
            {
                out[(size_t) c][(size_t) i] = rendered && std::isfinite (s[i]) ? s[i] : 0.0f;
                const float b = rendered ? am->splitBypass[(size_t) c][(size_t) i] : 0.0f;
                byp[(size_t) c][(size_t) i] = std::isfinite (b) ? b : 0.0f;
            }
        }
        for (int m = 0; m < kMods; ++m)
            mods[(size_t) m] = rendered && std::isfinite (am->splitFxMod[(size_t) m]) ? am->splitFxMod[(size_t) m] : 0.0f;
    }

    juce::MidiBuffer midiQueue;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> out {}, byp {};
    std::array<float, kMods> mods {};
    int pos { 0 };
};

//==============================================================================
// Massif - Effects
//==============================================================================
class AquaMassifEffectsModule final : public MassifHostModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("effects", "Effects", ModuleSection::Effect, 1);
        d.description = "Massif's master section, run by the plugin's own engine: FX 1 and FX 2 (reverbs, "
                        "flangers, choruses, phasers, delays, the dimension expander, tubes), the three-band EQ and "
                        "the master volume. Voice In takes the Massif Voice (or anything), Bypass In its bypass "
                        "bus, joined before FX 1, FX 2 or the EQ; the Mod inputs move the eight effect knobs (from "
                        "the Voice, or any modulation).";
        d.sockets = { audioIn ("busIn", "Voice In"), audioIn ("bypassIn", "Bypass In") };
        for (int m = 0; m < kMods; ++m)
            d.sockets.push_back (modIn (modId (m), juce::String ("Mod ") + modName (m)));
        d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
        for (const auto& k : effectKnobs())
            d.params.push_back (specFor (k));
        return d;
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        prepareHost (sr, AM::splitEffects);
        relink();
        io.setSize (2, kBlock);
        io.clear();
        for (auto& o : out) o.fill (0.0f);
        for (auto& b : busBuf) b.fill (0.0f);
        for (auto& b : bypBuf) b.fill (0.0f);
        modSum.fill (0.0f);
        pos = 0;
    }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        busBuf[0][(size_t) pos] = in[0][0];
        busBuf[1][(size_t) pos] = in[0][1];
        bypBuf[0][(size_t) pos] = in[1][0];
        bypBuf[1][(size_t) pos] = in[1][1];
        for (int m = 0; m < kMods; ++m)
            modSum[(size_t) m] += in[2 + m][0];
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
        if (am == nullptr)
            return;
        pushKnobs();

        for (int c = 0; c < 2; ++c)
            io.copyFrom (c, 0, busBuf[(size_t) c].data(), kBlock);

        bool rendered = false;
        {
            const juce::ScopedTryLock sl (am->getCallbackLock());
            if (sl.isLocked())
            {
                am->split = AM::splitEffects;
                am->tempoOverride = tempoBpm;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < kBlock; ++i)
                        am->splitBypass[(size_t) c][(size_t) i] = bypBuf[(size_t) c][(size_t) i];
                for (int m = 0; m < kMods; ++m)
                    am->splitFxMod[(size_t) m] = juce::jlimit (-1.0f, 1.0f, modSum[(size_t) m] / (float) kBlock);
                am->processBlock (io, noMidi);
                rendered = true;
            }
        }
        modSum.fill (0.0f);

        for (int c = 0; c < 2; ++c)
        {
            const float* s = io.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
                out[(size_t) c][(size_t) i] = rendered && std::isfinite (s[i]) ? s[i] : 0.0f;
        }
    }

    juce::MidiBuffer noMidi;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> busBuf {}, bypBuf {}, out {};
    std::array<float, kMods> modSum {};
    int pos { 0 };
};

//==============================================================================
// (6.9.14) Massif in smaller cards: the voice's engine (Massif - Voice Engine: no knobs
// of its own, the same sockets as Massif - Voice plus Cards In) and its sections
// as cards - Osc 1, Osc 2, Osc 3, Mod Osc, Noise & Feedback, Filters, Inserts,
// Amp & Voices, Macros, Envelopes, LFOs - chained into it. The effects stay
// Massif - Effects.
//==============================================================================
namespace
{
    using kit::CardDef;

    const std::vector<CardDef>& massifCards()
    {
        static const std::vector<CardDef> cards = []
        {
            struct G { const char* id; const char* kind; const char* text; std::vector<int> rows; };
            const G groups[] = {
                { "osc1", "Osc 1", "Massif's first wavetable oscillator: on, its table and mode, pitch, position, intensity, level and where it goes (Filter 1 or 2).", { 0 } },
                { "osc2", "Osc 2", "Massif's second wavetable oscillator: on, its table and mode, pitch, position, intensity, level and where it goes (Filter 1 or 2).", { 1 } },
                { "osc3", "Osc 3", "Massif's third wavetable oscillator: on, its table and mode, pitch, position, intensity, level and where it goes (Filter 1 or 2).", { 2 } },
                { "modosc", "Mod Osc", "Massif's modulation oscillator: its pitch, and how much it ring-modulates, phase-modulates, moves the position of and frequency-modulates the filter of the oscillators it is sent to.", { 3 } },
                { "noise", "Noise & Feedback", "Massif's noise (type, colour, level, filter balance) and its feedback (taken after a filter or the amp, its level and filter balance).", { 4 } },
                { "filters", "Filters", "Massif's two filters: on, type, cutoff, bandwidth, resonance and level each, serial or parallel, and their mix.", { 5, 6 } },
                { "inserts", "Inserts", "Massif's two inserts (on, type, where they sit, two knobs each).", { 7, 8 } },
                { "amp", "Amp & Voices", "Massif's amp and pan, the bypass bus (source and level), voices, unison, voice mode and trigger, glide, vibrato and the unison spreads.", { 9, 10 } },
                { "macros", "Macros", "Massif's eight macro knobs (what they move is in the sound's modulation).", { 11 } },
                { "envs", "Envelopes", "Massif's four envelopes: attack, decay, level and release each.", { 12, 13 } },
                { "lfos", "LFOs", "Massif's four modulation pages (LFO, stepper, performer...): type, rate, amount and morph each.", { 14, 15 } } };
            std::vector<CardDef> v;
            for (const auto& g : groups)
            {
                CardDef c;
                c.pluginId = kPlugin; c.id = g.id; c.kind = g.kind; c.description = g.text;
                for (size_t r = 0; r < g.rows.size(); ++r)
                {
                    if (r > 0) c.ids.push_back ({});
                    for (const auto& k : voiceKnobs())
                        if (k.row == g.rows[r]) c.ids.push_back (k.id);
                }
                c.spec = [] (const juce::String& id)
                {
                    for (const auto& k : voiceKnobs())
                        if (id == k.id) return specFor (k);
                    return makeRotary (id, id, 0.0f, 1.0f, 0.0f, 0);
                };
                v.push_back (std::move (c));
            }
            return v;
        }();
        return cards;
    }
}

class AquaMassifEngineModule final : public AquaMassifVoiceModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = AquaMassifVoiceModule::descriptor();
        d.typeId = "plugin.aquamassif.engine";
        d.displayName = kit::moduleName (kPlugin, "Voice Engine");
        d.sidebarOrder = 399;
        d.description = "Massif's voices, played by the plugin's own engine - as Massif - Voice, with its sections on "
                        "cards of their own (Osc 1 - 3, Mod Osc, Noise & Feedback, Filters, Inserts, Amp & Voices, "
                        "Macros, Envelopes, LFOs) chained into Cards In; without them it plays as they were last set. "
                        "Bypass Out and the eight Mod outputs go to the Massif Effects.";
        d.sockets.insert (d.sockets.begin() + 1, modIn ("cardsIn", "Cards In"));
        for (auto& p : d.params) { p.hidden = true; p.modulatable = false; }
        return d;
    }
};

AQUANODE_REGISTER_MODULE (AquaMassifVoiceModule,   AquaMassifVoiceModule::descriptor)
AQUANODE_REGISTER_MODULE (AquaMassifEngineModule,  AquaMassifEngineModule::descriptor)
static const bool massifCardsRegistered = [] { for (const auto& c : massifCards()) kit::registerCard (c); return true; }();
AQUANODE_REGISTER_MODULE (AquaMassifEffectsModule, AquaMassifEffectsModule::descriptor)

//==============================================================================
namespace aquanode::plugins::aquamassif
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        juce::MemoryBlock state;
        original.getStateInformation (state);
        std::vector<int> cards;

        // (6.9.14) the voice in smaller cards, chained into the Voice Engine
        std::vector<const kit::CardDef*> list;
        for (const auto& c : massifCards()) list.push_back (&c);
        const auto places = kit::cardPlaces (list, 0, 0, 4);
        const int voice = b.add ("plugin.aquamassif.engine", 1240, 0);
        const int fx = b.add ("plugin.aquamassif.effects", 1560, 0);
        int prevCard = -1;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const int c = b.add (list[i]->typeId(), places[i].x, places[i].y);
            if (c < 0) continue;
            if (prevCard >= 0) b.cable (prevCard, "linkOut", c, "linkIn");
            prevCard = c;
            cards.push_back (c);
        }
        if (prevCard >= 0) b.cable (prevCard, "linkOut", voice, "cardsIn");

        for (const int m : { voice, fx })
            if (auto* h = dynamic_cast<MassifHostModule*> (b.dsp (m)))
                if (auto* p = h->plugin())
                {
                    h->setPluginState (state);
                    b.copy (m, *p);   // the knobs: the plugin's own parameters
                    for (const int c : cards) b.copy (c, *p);   // (the cards' too)
                }

        b.cable (voice, "audioOut", fx, "busIn");
        b.cable (voice, "bypassOut", fx, "bypassIn");
        for (int m = 0; m < kMods; ++m)
            b.cable (voice, modId (m), fx, modId (m));

        PatchPorts ports;
        ports.name = "Massif";
        ports.midiIn = { { voice, "midiIn" } };
        ports.audioOut = { { fx, "audioOut" } };
        return ports;
    }

    // original .nmsv sounds and Massif's own .massif presets
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* a = dynamic_cast<AM*> (&original);
        juce::String error;
        return a != nullptr && a->loadPresetFile (file, error);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
