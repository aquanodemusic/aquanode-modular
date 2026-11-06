//==============================================================================
// Resonate as modules: Resonator (Effects) and Resonate - Bank (its patch
// version)
//
// Both run a whole Resonate - the same code its GUI card runs, all four
// engines (Modelled, Native, Crisp, Dark) - in 32-sample blocks, and every
// knob drives the plugin parameter it stands for in the engine that is picked
// (each engine keeps a parameter set of its own, as in the plugin), so every
// knob takes cables. Notes on Midi In retune the resonators, as in the plugin.
//
//   Resonator (fx.resonate): ONE tuned resonator with every mode and behaviour
//     Resonate has - the engine, Note and Fine, Decay, Color, Const, Full / Odd,
//     its Gain and Pan, the input filter, Dry/Wet and Wet Only, and the Crisp
//     engine's extras (Center, Exp Decay, 2x, Smooth, Chorus, LFO). Patches made
//     with the earlier Resonator (a comb with Mode A / B and an LFO - the Crisp
//     engine's ancestor) load into it on the Crisp engine.
//
//   Resonate - Bank (fx.resonate5): the plugin as a module, all seven
//     resonators with their own On, Pitch (II-VII, around I's Note), Fine, Gain,
//     Pan - and with Per Res their own Decay, Color and Const - plus everything
//     global. It is what the Patch pill and To Patch build, and patches made with
//     the earlier five-resonator Resonate module load into it (the same knobs).
//==============================================================================
#include "PluginResonateConfig.h"

namespace aqp_resonate
{
    #include AQP_RESONATE_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "resonate" };
    using RS = aqp_resonate::ResonateAudioProcessor;
    constexpr int kNumRes = 7;
    const char* const kRoman[kNumRes] = { "I", "II", "III", "IV", "V", "VI", "VII" };

    // each engine's own parameter set: Modelled keeps the plugin's first ids
    const char* const kEnginePrefix[4] = { "", "nat_", "crs_", "drk_" };
    constexpr int kCrisp = 2;

    juce::StringArray semitoneChoices()
    {
        juce::StringArray s;
        for (int i = -24; i <= 24; ++i)
            s.add (i > 0 ? "+" + juce::String (i) : juce::String (i));
        return s;
    }

    // how a knob's value becomes the plugin's
    enum class Map { Same, Semitones, FilterOn, FilterType };

    struct KnobLink
    {
        juce::String knob;     // the module's knob
        juce::String param;    // the plugin's parameter (without the engine's prefix)
        bool perEngine;        // in each engine's own set
        Map map { Map::Same };
    };

    // the plugin's value for a knob's (Map::FilterType: < 0 = leave it)
    float mapped (Map m, float v)
    {
        switch (m)
        {
            case Map::Semitones:  return v - 24.0f;               // the list -24..+24
            case Map::FilterOn:   return v > 0.5f ? 1.0f : 0.0f;  // Filter Off / a type
            case Map::FilterType: return v > 0.5f ? v - 1.0f : -1.0f;
            case Map::Same:       break;
        }
        return v;
    }

    //==========================================================================
    // the part both modules share: a Resonate instance whose parameters follow
    // the knobs, in the picked engine's set
    class ResonateKnobsModule : public kit::InstanceModule
    {
    public:
        ResonateKnobsModule() : InstanceModule (kPlugin, true) {}

        void prepare (double sr) override
        {
            // (the descriptor is only there once the module is made)
            resolved.clear();
            for (const auto& l : links())
            {
                Resolved r;
                r.knob = paramIndex (l.knob);
                r.map = l.map;
                for (int e = 0; e < 4; ++e)
                    r.param[(size_t) e] = plugin != nullptr ? kit::findParam (*plugin, (l.perEngine ? juce::String (kEnginePrefix[e]) : juce::String()) + l.param)
                                                            : nullptr;
                if (r.knob >= 0)
                    resolved.push_back (r);
            }
            fixedParams.clear();
            for (const auto& [id, v] : fixed())
                for (int e = 0; e < 4; ++e)
                    if (plugin != nullptr)
                        if (auto* p = kit::findParam (*plugin, juce::String (kEnginePrefix[e]) + id))
                            fixedParams.push_back ({ p, v });
            engineKnob = paramIndex ("engine");
            lastEngine = -1;
            InstanceModule::prepare (sr);
        }

    protected:
        virtual const std::vector<KnobLink>& links() const = 0;

        // plugin settings held where they are, in every engine's set
        virtual std::vector<std::pair<juce::String, float>> fixed() const { return {}; }

        juce::String pluginParamFor (const juce::String&) const override { return {}; }   // all linked here

        void beforeBlock() override
        {
            const int e = engineKnob >= 0 ? juce::jlimit (0, 3, (int) param (engineKnob)) : 0;
            const bool all = e != lastEngine;
            if (all)
            {
                if (auto* p = kit::findParam (*plugin, "engine"))
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) e));
                for (auto& f : fixedParams)
                    f.param->setValueNotifyingHost (f.param->convertTo0to1 (f.value));
            }
            lastEngine = e;

            for (auto& r : resolved)
            {
                auto* p = r.param[(size_t) e];
                if (p == nullptr)
                    continue;
                const float v = mapped (r.map, param (r.knob));
                if (v < 0.0f && r.map == Map::FilterType)
                    continue;
                if (! all && v == r.last)
                    continue;
                p->setValueNotifyingHost (p->convertTo0to1 (v));
                r.last = v;
            }
        }

        void stateRestored() override { lastEngine = -1; }   // every knob goes in again

    private:
        struct Resolved
        {
            int knob { -1 };
            Map map { Map::Same };
            std::array<juce::RangedAudioParameter*, 4> param {};
            float last { std::numeric_limits<float>::quiet_NaN() };
        };
        struct Fixed { juce::RangedAudioParameter* param; float value; };

        std::vector<Resolved> resolved;
        std::vector<Fixed> fixedParams;
        int engineKnob { -1 }, lastEngine { -1 };
    };

    //=== the knobs both have ====================================================
    void addGlobalParams (std::vector<ParamSpec>& p, int noteDefault, float decayDefault, float colorDefault)
    {
        p.push_back (makeCombo ("engine", "Engine", { "Modelled", "Native", "Crisp", "Dark" }, 0, 0, 2).noMod());
        p.push_back (makeSteppedList ("note", "Note", midiNoteNameChoices(), noteDefault, 0));
        p.push_back (makeRotary ("decay", "Decay", 0.0f, 100.0f, decayDefault, 0));
        p.push_back (makeRotary ("color", "Color", 0.0f, 100.0f, colorDefault, 0));
        p.push_back (makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%"));

        p.push_back (makeCombo ("mode", "Shape", { "Full", "Odd" }, 0, 1, 2));
        p.push_back (makeCombo ("const", "Const", { "Free", "Const" }, 0, 1, 2));
        p.push_back (makeCombo ("filter", "Filter", { "Filter Off", "Lowpass", "Highpass", "Bandpass", "Notch" }, 0, 1, 2));
        p.push_back (makeRotary ("filterFreq", "Filter Hz", 20.0f, 12000.0f, 1000.0f, 1, "Hz", true));

        p.push_back (makeCombo ("wetOnly", "Wet Only", { "Dry + Wet", "Wet Only" }, 0, 2, 2));
        // the Crisp engine's extras
        p.push_back (makeCombo ("center", "Center", { "Off", "On" }, 1, 2, 2).visibleWhen ("engine", (float) kCrisp));
        p.push_back (makeCombo ("expDecay", "Exp Decay", { "Off", "On" }, 0, 2, 2).visibleWhen ("engine", (float) kCrisp));
        p.push_back (makeCombo ("oversample", "2x", { "Off", "On" }, 0, 2, 2).visibleWhen ("engine", (float) kCrisp).noMod());
        p.push_back (makeRotary ("smooth", "Smooth", 0.0f, 100.0f, 0.0f, 3).visibleWhen ("engine", (float) kCrisp));
        p.push_back (makeRotary ("chorus", "Chorus", 0.0f, 100.0f, 0.0f, 3).visibleWhen ("engine", (float) kCrisp));
        p.push_back (makeRotary ("lfoRate", "LFO Rate", 0.1f, 5.0f, 0.5f, 3, "Hz", true).visibleWhen ("engine", (float) kCrisp));
        p.push_back (makeRotary ("lfoDepth", "LFO Depth", 0.0f, 20.0f, 5.0f, 3).visibleWhen ("engine", (float) kCrisp));
    }

    void addGlobalLinks (std::vector<KnobLink>& l)
    {
        l.push_back ({ "note", "res1_note", true });
        l.push_back ({ "decay", "decay", true });
        l.push_back ({ "color", "color", true });
        l.push_back ({ "dryWet", "drywet", false });
        l.push_back ({ "mode", "mode", true });
        l.push_back ({ "const", "const_mode", true });
        l.push_back ({ "filter", "filter_enabled", true, Map::FilterOn });
        l.push_back ({ "filter", "filter_type", true, Map::FilterType });
        l.push_back ({ "filterFreq", "filter_freq", true });
        l.push_back ({ "wetOnly", "wet_only", false });
        for (auto [k, p] : { std::pair<const char*, const char*> { "center", "center_mode" }, { "expDecay", "exp_decay" },
                             { "oversample", "oversample_2x" }, { "smooth", "smooth" }, { "chorus", "chorus" },
                             { "lfoRate", "lfo_rate" }, { "lfoDepth", "lfo_depth" } })
            l.push_back ({ k, p, true });
    }
}

//==============================================================================
// Resonator: one resonator of Resonate's, under Effects
//==============================================================================
class ResonatorSingleModule final : public ResonateKnobsModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.resonate";
        d.displayName = "Resonator";
        d.description = "One tuned resonator with everything Resonate's resonators can do: four engines (Modelled, Native, "
                        "Crisp, Dark), Note and Fine, Decay and Color, Const (the same decay at every pitch), Full or Odd "
                        "(a hollow, square tone), its Gain and Pan, a filter on the input, Dry/Wet, and the Crisp engine's "
                        "Center, Exp Decay, 2x, Smooth, Chorus and LFO. Notes on Midi In retune it. Feed it noise for a "
                        "sung drone. Resonate (Plugins) has seven of them.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 9;
        d.sockets = { audioIn ("audioIn", "Audio In"), midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        addGlobalParams (d.params, 60, 75.0f, 45.0f);
        d.params.push_back (makeRotary ("fine", "Fine", -50.0f, 50.0f, 0.0f, 4, "ct"));
        d.params.push_back (makeRotary ("resGain", "Gain", -48.0f, 12.0f, 0.0f, 4, "dB"));
        d.params.push_back (makeRotary ("pan", "Pan", -100.0f, 100.0f, 0.0f, 4, {}, false, 1.0f));
        return d;
    }

protected:
    const std::vector<KnobLink>& links() const override
    {
        static const std::vector<KnobLink> l = []
        {
            std::vector<KnobLink> v;
            addGlobalLinks (v);
            v.push_back ({ "fine", "res1_fine", true });
            v.push_back ({ "resGain", "res1_gain", true });
            v.push_back ({ "pan", "res1_pan", true });
            return v;
        }();
        return l;
    }

    std::vector<std::pair<juce::String, float>> fixed() const override
    {
        // resonator I alone, with the shared Decay and Color
        std::vector<std::pair<juce::String, float>> f { { "res1_enabled", 1.0f }, { "per_res_mode", 0.0f }, { "width", 100.0f } };
        for (int i = 2; i <= kNumRes; ++i)
            f.push_back ({ "res" + juce::String (i) + "_enabled", 0.0f });
        return f;
    }
};

//==============================================================================
// Resonate - Bank: all seven resonators (Resonate's patch version)
//==============================================================================
class ResonateBankPatchModule final : public ResonateKnobsModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.resonate5";
        d.displayName = kit::moduleName (kPlugin, "Bank");
        d.listedWithPlugin = kPlugin;
        d.accent = kit::accentOf (kPlugin);
        d.description = "Resonate whole as one module: seven tuned resonators - I at Note, II to VII tuned around it in "
                        "semitones (G3, +2, +9, +9, +2 to begin with) - each with its own On, Fine, Gain and Pan, and with "
                        "Per Res its own Decay, Color and Const; the engine (Modelled, Native, Crisp, Dark), Full / Odd, "
                        "Width, the input filter, Dry/Wet and the Crisp engine's extras. Notes on Midi In retune the "
                        "resonators round robin. Every knob takes cables.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 400;
        d.sockets = { audioIn ("audioIn", "Audio In"), midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        addGlobalParams (d.params, 55, 75.0f, 45.0f);
        d.params.push_back (makeRotary ("gain", "Gain", -48.0f, 12.0f, 0.0f, 1, "dB"));
        d.params.push_back (makeRotary ("width", "Width", 0.0f, 100.0f, 100.0f, 2, "%", false, 1.0f));
        d.params.push_back (makeCombo ("perRes", "Per Res", { "Shared", "Per Res" }, 0, 2, 2));

        const int defaultPitch[kNumRes] = { 0, 2, 9, 9, 2, 0, 0 };
        const float defaultPan[kNumRes] = { 0.0f, -100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f };
        const auto semis = semitoneChoices();
        for (int i = 0; i < kNumRes; ++i)
        {
            const juce::String n (i + 1), r (kRoman[i]);
            const int row = 4 + i;
            d.params.push_back (makeCombo ("on" + n, r, { r + " Off", r + " On" }, i < 5 ? 1 : 0, row, 2));
            if (i > 0)
                d.params.push_back (makeSteppedList ("pitch" + n, r + " Pitch", semis, 24 + defaultPitch[i], row));
            d.params.push_back (makeRotary ("fine" + n, r + " Fine", -50.0f, 50.0f, 0.0f, row, "ct"));
            d.params.push_back (makeRotary ("gain" + n, r + " Gain", -48.0f, 12.0f, 0.0f, row, "dB"));
            d.params.push_back (makeRotary ("pan" + n, r + " Pan", -100.0f, 100.0f, defaultPan[i], row, {}, false, 1.0f));
            d.params.push_back (makeRotary ("decay" + n, r + " Decay", 0.0f, 100.0f, 50.0f, row).visibleWhen ("perRes", 1.0f));
            d.params.push_back (makeRotary ("color" + n, r + " Color", 0.0f, 100.0f, 50.0f, row).visibleWhen ("perRes", 1.0f));
        }
        return d;
    }

protected:
    const std::vector<KnobLink>& links() const override
    {
        static const std::vector<KnobLink> l = []
        {
            std::vector<KnobLink> v;
            addGlobalLinks (v);
            v.push_back ({ "gain", "gain", false });
            v.push_back ({ "width", "width", true });
            v.push_back ({ "perRes", "per_res_mode", true });
            for (int i = 1; i <= kNumRes; ++i)
            {
                const juce::String n (i), res = "res" + n + "_";
                v.push_back ({ "on" + n, res + "enabled", true });
                if (i > 1)
                    v.push_back ({ "pitch" + n, res + "pitch", true, Map::Semitones });
                v.push_back ({ "fine" + n, res + "fine", true });
                v.push_back ({ "gain" + n, res + "gain", true });
                v.push_back ({ "pan" + n, res + "pan", true });
                v.push_back ({ "decay" + n, res + "decay", true });
                v.push_back ({ "color" + n, res + "color", true });
            }
            return v;
        }();
        return l;
    }
};

AQUANODE_REGISTER_MODULE (ResonatorSingleModule, ResonatorSingleModule::descriptor)
AQUANODE_REGISTER_MODULE (ResonateBankPatchModule, ResonateBankPatchModule::descriptor)

//==============================================================================
namespace aquanode::plugins::resonate
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int m = b.add ("fx.resonate5", 0, 0);

        // the knobs from the picked engine's own settings
        const int engine = juce::jlimit (0, 3, (int) kit::param (original, "engine"));
        const juce::String pre (kEnginePrefix[engine]);
        auto engineParam = [&] (const juce::String& id, float fallback) { return kit::param (original, pre + id, fallback); };

        b.set (m, "engine", (float) engine);
        b.set (m, "note", engineParam ("res1_note", 48.0f));
        b.set (m, "decay", engineParam ("decay", 75.0f));
        b.set (m, "color", engineParam ("color", 45.0f));
        b.set (m, "dryWet", kit::param (original, "drywet", 100.0f));
        b.set (m, "wetOnly", kit::param (original, "wet_only"));
        b.set (m, "gain", kit::param (original, "gain"));
        b.set (m, "mode", engineParam ("mode", 0.0f));
        b.set (m, "const", engineParam ("const_mode", 0.0f));
        b.set (m, "width", engineParam ("width", 100.0f));
        b.set (m, "perRes", engineParam ("per_res_mode", 0.0f));
        b.set (m, "filter", engineParam ("filter_enabled", 0.0f) > 0.5f ? 1.0f + engineParam ("filter_type", 0.0f) : 0.0f);
        b.set (m, "filterFreq", engineParam ("filter_freq", 1000.0f));
        for (auto [k, p] : { std::pair<const char*, const char*> { "center", "center_mode" }, { "expDecay", "exp_decay" },
                             { "oversample", "oversample_2x" }, { "smooth", "smooth" }, { "chorus", "chorus" },
                             { "lfoRate", "lfo_rate" }, { "lfoDepth", "lfo_depth" } })
            if (kit::findParam (original, pre + p) != nullptr)
                b.set (m, k, engineParam (p, 0.0f));
        for (int i = 1; i <= kNumRes; ++i)
        {
            const juce::String n (i), res = "res" + n + "_";
            b.set (m, "on" + n, engineParam (res + "enabled", i == 1 ? 1.0f : 0.0f));
            if (i > 1)
                b.set (m, "pitch" + n, engineParam (res + "pitch", 0.0f) + 24.0f);
            b.set (m, "fine" + n, engineParam (res + "fine", 0.0f));
            b.set (m, "gain" + n, engineParam (res + "gain", 0.0f));
            b.set (m, "pan" + n, engineParam (res + "pan", 0.0f));
            b.set (m, "decay" + n, engineParam (res + "decay", 50.0f));
            b.set (m, "color" + n, engineParam (res + "color", 50.0f));
        }

        // and the rest of its state (MIDI In, the per resonator Const ...)
        if (auto* r = dynamic_cast<kit::InstanceModule*> (b.dsp (m)))
            r->copyStateFrom (original);

        PatchPorts ports;
        ports.name = "Resonate";
        ports.audioOut = { { m, "audioOut" } };
        ports.audioIn = { { m, "audioIn" } };
        ports.midiIn = { { m, "midiIn" } };
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* r = dynamic_cast<RS*> (&original);
        return r != nullptr && r->loadPresetFromFile (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
