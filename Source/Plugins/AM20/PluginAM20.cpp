// AM-20, compiled into Aquanode Modular: an MS-20-style polysynth modelled
// from measurements (Source/PluginSources/am20 - the engine is JUCE-free)
// inside namespace aqp_am20 (see PluginPrelude.h), and its GUI module.
#include "PluginAM20Config.h"

namespace aqp_am20
{
    #include "../../PluginSources/am20/PluginProcessor.cpp"
    #include "../../PluginSources/am20/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (AM20GuiModule, "am20", aqp_am20)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& AM20WholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "am20";
            x.kind = "Synth";
            x.description = "AM-20 whole, as one module: both VCOs with the ring modulator, the 12 dB high- and low-pass filters, EG1 and EG2, the MG and the patch panel, played by the plugin's own engine (poly, unison or mono). Every one of its parameters is a knob here, in the plugin's own order, and takes cables. Built from its default sound, from a GUI card (Convert to Patch) or from an .am20 preset or an .fxp / .fxb.";
            x.section = aquanode::ModuleSection::Oscillator;
            x.midi = true;
            x.audioIn = false;
            x.sideIn = false;
            return x;
        }();
        return def;
    }

    bool AM20WholeModuleLoad (juce::AudioProcessor& o, const juce::File& f)
    {
        auto* a = dynamic_cast<aqp_am20::AM20Processor*> (&o);
        juce::MemoryBlock mb;
        if (a == nullptr || ! f.loadFileAsData (mb)) return false;
        juce::String error;
        return a->loadPresetData (mb, f.getFileName(), error);
    }
}

//==============================================================================
// (6.9.14) the patch version in smaller cards: AM-20 - VCOs, Filters, LFO, Envelopes
// and Mod Matrix (the two external sources and where they go), chained into
// AM-20 - Engine (the voice assignment, unison, the ESP, volume and pan). The
// plugin's own engine plays it all, so it sounds as the plugin does.
namespace
{
    using aquanode::plugins::kit::CardDef;

    const std::vector<CardDef>& AM20Cards()
    {
        static const std::vector<CardDef> cards = []
        {
            auto card = [] (const char* id, const char* kind, const char* text, std::vector<juce::String> ids)
            {
                CardDef c;
                c.pluginId = "am20"; c.id = id; c.kind = kind; c.description = text; c.ids = std::move (ids);
                c.spec = [] (const juce::String& pid) { return aquanode::plugins::whole::pluginSpec ("am20", pid); };
                return c;
            };
            std::vector<CardDef> v;
            v.push_back (card ("vcos", "VCOs", "AM-20's two oscillators: VCO 1's wave, pulse width and scale, VCO 2's wave, pitch and "
                               "scale, the fine tune and portamento, both levels, and how far the LFO and Env 1 move the pitch. "
                               "Link Out goes on to the next card, the last one into AM-20 - Engine.",
                               { "vco1Wave", "vco1Pw", "vco1Scale", "vco1Level", "", "vco2Wave", "vco2Pitch", "vco2Scale", "vco2Level", "",
                                 "fineTune", "portamento", "pitchMg", "pitchEg1" }));
            v.push_back (card ("filters", "Filters", "AM-20's high-pass and low-pass filters (12 dB each, in a row): cutoff, peak, "
                               "and how far the LFO and Env 2 move each.",
                               { "hpfCutoff", "hpfPeak", "hpfMg", "hpfEg2", "", "lpfCutoff", "lpfPeak", "lpfMg", "lpfEg2" }));
            v.push_back (card ("lfo", "LFO", "AM-20's modulation generator: its wave and rate, or in time with the tempo (base note "
                               "and times), and whether each note restarts it.",
                               { "mgWave", "mgFreq", "", "mgTempoSync", "mgBaseNote", "mgTimes", "mgKeySync" }));
            v.push_back (card ("envs", "Envelopes", "AM-20's two envelopes: Env 1 (delay, attack, release - the pitch's) and "
                               "Env 2 (hold, attack, decay, sustain, release - the filters' and the amp's).",
                               { "eg1Delay", "eg1Attack", "eg1Release", "", "eg2Hold", "eg2Attack", "eg2Decay", "eg2Sustain", "eg2Release" }));
            v.push_back (card ("matrix", "Mod Matrix", "AM-20's patch panel for two sources (velocity, key tracking, pressure, "
                               "the wheels, breath, CC 16 / 17, the LFO, the envelopes): each sent to VCO 1's pulse width, VCO 2's "
                               "pitch, the two filters and the amp, by its own amount (the middle is none).",
                               { "extSrc1", "src1Pw", "src1Vco2", "src1Hpf", "src1Lpf", "src1Amp", "",
                                 "extSrc2", "src2Pw", "src2Vco2", "src2Hpf", "src2Lpf", "src2Amp" }));
            return v;
        }();
        return cards;
    }

    bool onAM20Card (const juce::String& id)
    {
        for (const auto& c : AM20Cards())
            for (const auto& i : c.ids)
                if (i == id) return true;
        return false;
    }

    const aquanode::plugins::whole::Def& AM20EngineDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            auto x = AM20WholeModuleDef();
            x.suffix = "engine";
            x.kind = "Engine";
            x.cardsIn = true;
            x.description = "AM-20's engine, played by the plugin itself: the voice assignment (poly, unison or mono), "
                            "voices and unison, transpose and bend range, analog drift, the ESP, volume and pan. Its "
                            "VCOs, Filters, LFO, Envelopes and Mod Matrix are cards of their own, chained into Cards In; "
                            "without them it plays as they were last set.";
            x.hide = [] (const juce::String& id) { return onAM20Card (id); };
            return x;
        }();
        return def;
    }
}

extern "C" AQUANODE_USED_ATTR bool AM20Cards_aqRegistered =
    aquanode::plugins::whole::registerWithCards (AM20WholeModuleDef(), AM20EngineDef(), AM20Cards(), AM20WholeModuleLoad, 3);
AQUANODE_KEEP_SYMBOL (AM20Cards_aqRegistered)

//==============================================================================
// (6.9.11) AM-20 for the Random Plugin Preset module: one of its built-in
// sounds (the bank its preset selector shows); saved as an .am20 preset
#include "../Host/RandomPresetSources.h"
namespace
{
    const bool am20RandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        namespace rp = aquanode::plugins::randompreset;
        rp::Source s;
        s.pluginId = "am20";
        s.name = "AM-20";
        s.lists = [] { rp::Lists l; l.kinds.add ("Anything"); return l; };
        s.roll = [] (juce::AudioProcessor& p, int, int, int, juce::uint32 seed) -> juce::String
        {
            std::vector<int> pool;   // (its "Init" left out)
            for (int i = 0; i < p.getNumPrograms(); ++i)
                if (! p.getProgramName (i).equalsIgnoreCase ("Init"))
                    pool.push_back (i);
            if (pool.empty()) return {};
            juce::Random rng ((juce::int64) seed);
            const int idx = pool[(size_t) rng.nextInt ((int) pool.size())];
            p.setCurrentProgram (idx);
            return p.getProgramName (idx);
        };
        s.presetExtension = "am20";
        s.presetData = [] (juce::AudioProcessor& p, const juce::String&, juce::MemoryBlock& out)
        {
            auto* a = dynamic_cast<aqp_am20::AM20Processor*> (&p);
            if (a == nullptr) return false;
            out = a->presetAsAm20();
            return out.getSize() > 0;
        };
        return s;
    }());
}
