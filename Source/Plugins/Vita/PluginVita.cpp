// Vita, compiled into Aquanode Modular: the synth's sources (Aquanode Modular's own copy, in
// Source/PluginSources/vita/Source) inside namespace aqp_vita (see PluginPrelude.h), and its GUI module.
// Vita is the GPL Vital synthesizer by Matt Tytel without Vital's name, logo and sign-in.
#include "PluginVitaConfig.h"
// (6.9.12) Phizmo's wavetable generator, for Vita's Generate menu (PhizmoWaveFactory.cpp)
namespace aqp_phizmo
{
    int phizmoArchetypeCount();
    const char* phizmoArchetypeName (int);
    bool phizmoBuildTranswave (int archIdx, juce::int64 seed, juce::Random& rng, int numFrames, int cycleSamples,
                               std::vector<std::vector<float>>& frames, juce::String& label, juce::String& statusOut);
}
#include <complex>      // (6.9.12) for Aqua Massif's table recipes, read inside namespace aqp_vita
#include <fstream>
#include <functional>
#include <cctype>
#include <cstring>
#include "../../PluginSources/vita/VitaBinaryData.h"   // namespace aqp_vita::BinaryData

namespace aqp_vita
{
    #include "../../PluginSources/vita/Source/common.cpp"
    #include "../../PluginSources/vita/Source/synthesis.cpp"
    #include "../../PluginSources/vita/Source/interface_look_and_feel.cpp"
    #include "../../PluginSources/vita/Source/interface_editor_components.cpp"
    #include "../../PluginSources/vita/Source/interface_editor_sections.cpp"
    #include "../../PluginSources/vita/Source/interface_editor_sections2.cpp"
    #include "../../PluginSources/vita/Source/interface_wavetable.cpp"
    #include "../../PluginSources/vita/Source/plugin.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (VitaGuiModule, "vita", aqp_vita)

// Vita FX: the same synth with an audio input into its effects, showing only its effects page
namespace aqp_vitafx
{
    juce::AudioProcessor* createPluginFilter() { return aqp_vita::createPluginFilterFx(); }
}
AQP_DEFINE_PLUGIN (VitaFxGuiModule, "vitafx", aqp_vitafx)

// the patch version: Vita's voice as modules (after the sources above, whose parameter tables its
// descriptors read while the program starts)
#include "PluginVitaPatch.h"

// for tests: every built-in Soundscape put back together - the number that fail, and how many there are
int aqVitaSoundscapesCheck (int& total, size_t& bytes)
{
    total = aqp_vita::VitaSoundscapes::count();
    int failed = 0; bytes = 0;
    for (int i = 0; i < total; ++i)
    {
        aqp_vita::json j; std::string error;
        if (! aqp_vita::VitaSoundscapes::build (i, j, error)) { ++failed; continue; }
        const std::string text = j.dump();
        bytes += text.size();
        if (text.find ("\"@w") != std::string::npos || text.find ("\"@g") != std::string::npos || text.find ("\"@s") != std::string::npos) ++failed;
    }
    // Generate Growl on a preset's state: its routings in place
    aqp_vita::json j; std::string error;
    if (aqp_vita::VitaSoundscapes::build (0, j, error))
    {
        for (int seed = 1; seed <= 20; ++seed)
        {
            aqp_vita::json g = j;
            for (auto& m : g["settings"]["modulations"]) { m["source"] = ""; m["destination"] = ""; }
            aqp_vita::VitaSoundscapes::growl ((uint32_t) seed, g);
            int routed = 0;
            for (auto& m : g["settings"]["modulations"]) if (! m.value ("source", std::string()).empty()) ++routed;
            if (routed < 3 || g["settings"]["osc_1_spectral_morph_type"].get<float>() != 6.0f) ++failed;
        }
    }
    return failed;
}

#if AQUANODE_TEST_HARNESS
// for tests: a soundscape's MPE and bend settings and what the pitch wheel / mod wheel move
// for tests: Vita's MPE after loading a soundscape (its window open or not)
bool aqVitaMpeAfterSoundscape (juce::AudioProcessor& p, const std::string& nameWanted, bool followAquanode)
{
    auto* v = dynamic_cast<aqp_vita::SynthPlugin*> (&p);
    if (v == nullptr) return false;
    v->setMpePolicy (! followAquanode, false);   // (fromPreset, bendFromPreset)
    v->enableMpe (true);
    for (int i = 0; i < aqp_vita::VitaSoundscapes::count(); ++i)
        if (aqp_vita::VitaSoundscapes::name (i) == nameWanted)
        {
            std::string error;
            v->loadSoundscape (i, error);
            break;
        }
    return v->isMpeEnabled();
}

// for tests: which oscillators' frames Slide may move (after loading a soundscape, or the init preset)
std::string aqVitaFrameFree (juce::AudioProcessor& p, const std::string& nameWanted)
{
    auto* v = dynamic_cast<aqp_vita::SynthPlugin*> (&p);
    if (v == nullptr) return {};
    if (nameWanted.empty())
        v->loadInitPreset();
    else
        for (int i = 0; i < aqp_vita::VitaSoundscapes::count(); ++i)
            if (aqp_vita::VitaSoundscapes::name (i) == nameWanted)
            {
                std::string error;
                v->loadSoundscape (i, error);
                break;
            }
    std::string out;
    for (int i = 0; i < 3; ++i)
        out += v->aqFrameFree (i) ? "1" : "0";
    return out;
}

// for tests: every table of Vita's own (the default set, Massif's stand-ins, a Generate of each
// kind) loaded into oscillator 1: its frames' level (RMS of frames 0, 128 and 256) and how far
// the first and last frame differ
std::string aqVitaWavetableCheck (juce::AudioProcessor& p)
{
    auto* v = dynamic_cast<aqp_vita::SynthPlugin*> (&p);
    if (v == nullptr) return {};
    auto* creator = v->getWavetableCreator (0);
    std::string out;
    auto measure = [&] (aqp_vita::json table, const std::string& what)
    {
        creator->jsonToState (table);
        auto* wt = v->getEngine()->getWavetable (0);
        auto rms = [&] (int frame)
        {
            const float* b = wt->getBuffer (frame);
            double e = 0.0;
            for (int i = 0; i < aqp_vita::vital::Wavetable::kWaveformSize; ++i) e += (double) b[i] * b[i];
            return std::sqrt (e / aqp_vita::vital::Wavetable::kWaveformSize);
        };
        double diff = 0.0;
        {
            const float* a = wt->getBuffer (0); const float* b = wt->getBuffer (256);
            for (int i = 0; i < aqp_vita::vital::Wavetable::kWaveformSize; ++i) diff += (double) (a[i] - b[i]) * (a[i] - b[i]);
            diff = std::sqrt (diff / aqp_vita::vital::Wavetable::kWaveformSize);
        }
        char line[256];
        std::snprintf (line, sizeof (line), "%s|%s|%.3f|%.3f|%.3f|%.3f\n", what.c_str(), wt->getName().c_str(), rms (0), rms (128), rms (256), diff);
        out += line;
    };
    for (int i = 0; i < (int) aqp_vita::VitaWavetables::defaultNames().size(); ++i)
        measure (aqp_vita::VitaWavetables::defaultTable (i), "default");
    for (int i = 0; i < aqp_vita::VitaWavetables::massifCount(); ++i)
        measure (aqp_vita::VitaWavetables::massifStandIn (i), "massif");
    for (int i = 0; i < (int) aqp_vita::VitaWavetables::generatorKinds().size(); ++i)
        measure (aqp_vita::VitaWavetables::generate (i, 1234u + (uint32_t) i), "generate:" + aqp_vita::VitaWavetables::generatorKinds()[(size_t) i]);
    return out;
}

void aqVitaPressNote (juce::AudioProcessor& p, int note)
{
    if (auto* v = dynamic_cast<aqp_vita::SynthPlugin*> (&p))
        v->getKeyboardState()->noteOn (1, note, 1.0f);
}

std::string aqVitaSoundscapeInfo (const std::string& nameWanted)
{
    std::string out;
    for (int i = 0; i < aqp_vita::VitaSoundscapes::count(); ++i)
    {
        const auto name = aqp_vita::VitaSoundscapes::name (i);
        if (! nameWanted.empty() && name.find (nameWanted) == std::string::npos) continue;
        aqp_vita::json j; std::string error;
        if (! aqp_vita::VitaSoundscapes::build (i, j, error)) continue;
        const auto& s = j["settings"];
        out += name + " [" + aqp_vita::VitaSoundscapes::category (i) + "]: mpe " + std::to_string (s.value ("mpe_enabled", -1.0f))
             + ", bend " + std::to_string (s.value ("pitch_bend_range", -1.0f)) + ", poly " + std::to_string (s.value ("polyphony", -1.0f));
        for (const auto& m : s["modulations"])
        {
            const auto src = m.value ("source", std::string());
            if (src == "pitch_wheel" || src == "mod_wheel" || src.rfind ("slide", 0) == 0 || src.rfind ("lift", 0) == 0 || src.rfind ("aftertouch", 0) == 0)
                out += "; " + src + " -> " + m.value ("destination", std::string());
        }
        out += "\n";
    }
    return out;
}
#endif

#if AQUANODE_TEST_HARNESS
// for tests: every Generate kind - how many soundscapes it learns from, and
// whether five new ones come out unlike the soundscapes they came from
std::string aqVitaGenerateCheck()
{
    std::string out;
    for (const auto& kind : aqp_vita::VitaSoundscapes::generatorKinds())
    {
        int ok = 0;
        for (uint32_t seed = 1; seed <= 5; ++seed)
        {
            aqp_vita::json j; std::string error;
            if (aqp_vita::VitaSoundscapes::generate (kind, seed, j, error) && j.count ("settings") > 0 && j["settings"].is_object())
                ++ok;
        }
        out += kind + ": learns from " + std::to_string (aqp_vita::VitaSoundscapes::kindSize (kind)) + ", made " + std::to_string (ok) + "/5\n";
    }
    return out;
}
#endif

//==============================================================================
// (6.9.11) Vita for the Random Plugin Preset module: one of the Aquanode
// Soundscapes (of any category, or of one), or a new preset of a kind made by
// its Generate menu; saved as Vita's own preset file
#include "../Host/RandomPresetSources.h"
namespace
{
    const bool vitaRandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        namespace rp = aquanode::plugins::randompreset;
        rp::Source s;
        s.pluginId = "vita";
        s.name = "Vita";
        s.lists = []
        {
            rp::Lists l;
            l.kindHeadings.add ("Aquanode Soundscapes");
            l.kinds.add ("Any Soundscape");
            for (const auto& c : aqp_vita::VitaSoundscapes::categories())
                l.kinds.add (juce::String (c));
            while (l.kindHeadings.size() < l.kinds.size()) l.kindHeadings.add ({});
            l.kindHeadings.add ("Generate (new presets)");
            for (const auto& k : aqp_vita::VitaSoundscapes::generatorKinds())
                if (aqp_vita::VitaSoundscapes::kindSize (k) > 0)
                    l.kinds.add ("Generate: " + juce::String (k));
            l.defaultKind = 0;
            return l;
        };
        s.roll = [] (juce::AudioProcessor& p, int kind, int, int, juce::uint32 seed) -> juce::String
        {
            auto* vita = dynamic_cast<aqp_vita::SynthPlugin*> (&p);
            if (vita == nullptr)
                return {};
            const auto cats = aqp_vita::VitaSoundscapes::categories();
            juce::Random rng ((juce::int64) seed);
            if (kind <= (int) cats.size())
            {
                // a soundscape: of any category (kind 0), or of one
                std::vector<int> pool;
                for (int i = 0; i < aqp_vita::VitaSoundscapes::count(); ++i)
                    if (kind == 0 || aqp_vita::VitaSoundscapes::category (i) == cats[(size_t) (kind - 1)])
                        pool.push_back (i);
                if (pool.empty())
                    return {};
                std::string error;
                const int idx = pool[(size_t) rng.nextInt ((int) pool.size())];
                if (! vita->loadSoundscape (idx, error))
                    return {};
                return juce::String (aqp_vita::VitaSoundscapes::name (idx));
            }
            std::vector<std::string> gens;
            for (const auto& k : aqp_vita::VitaSoundscapes::generatorKinds())
                if (aqp_vita::VitaSoundscapes::kindSize (k) > 0)
                    gens.push_back (k);
            const int g = kind - 1 - (int) cats.size();
            if (g < 0 || g >= (int) gens.size())
                return {};
            vita->generateKind (gens[(size_t) g], seed == 0 ? 1u : (uint32_t) seed);
            return juce::String (vita->getPresetName());
        };
        s.presetExtension = juce::String (aqp_vita::vital::kPresetExtension);
        s.presetData = [] (juce::AudioProcessor& p, const juce::String& name, juce::MemoryBlock& out)
        {
            auto* vita = dynamic_cast<aqp_vita::SynthPlugin*> (&p);
            if (vita == nullptr)
                return false;
            auto j = vita->saveToJson();
            if (name.isNotEmpty())
                j["preset_name"] = name.toStdString();
            const auto text = j.dump();
            out.replaceAll (text.data(), text.size());
            return ! text.empty();
        };
        return s;
    }());
}

//==============================================================================
// (6.9.15) the built-in tables for Aquanode's Wavetable module: 0 Vita's own set, 1 Massif's
// tables (their stand-ins), 2 the generators (Massif's recipes, Vita's own, Phizmo's)
int aqBuiltinTableCount (int group)
{
    if (group == 0) return (int) aqp_vita::VitaWavetables::defaultNames().size();
    if (group == 1) return aqp_vita::VitaWavetables::massifCount();
    if (group == 2) return (int) aqp_vita::VitaWavetables::generatorKinds().size();
    return 0;
}

std::string aqBuiltinTableName (int group, int i)
{
    if (group == 0) return aqp_vita::VitaWavetables::defaultNames()[(size_t) juce::jlimit (0, aqBuiltinTableCount (0) - 1, i)];
    if (group == 1) return aqp_vita::VitaWavetables::massifName (i);
    if (group == 2) return aqp_vita::VitaWavetables::generatorKinds()[(size_t) juce::jlimit (0, aqBuiltinTableCount (2) - 1, i)];
    return {};
}

std::vector<float> aqBuiltinTable (int group, int i, uint32_t seed, std::string& name)
{
    std::vector<float> raw;
    if (group == 0) raw = aqp_vita::VitaWavetables::defaultFrames (i, name);
    else if (group == 1) { raw = aqp_vita::VitaWavetables::massifFrames (i); name = aqp_vita::VitaWavetables::massifName (i); }
    else if (group == 2) raw = aqp_vita::VitaWavetables::generateFrames (i, seed, name);
    return raw.empty() ? raw : aqp_vita::VitaWavetables::prepareFrames (raw);
}
