// Reflex, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/reflex - the engine is JUCE-free)
// inside namespace aqp_reflex (see PluginPrelude.h), and its GUI module.
#include "PluginReflexConfig.h"

namespace aqp_reflex
{
    #include "../../PluginSources/reflex/Engine.cpp"
    #include "../../PluginSources/reflex/Randomizer.cpp"
    #include "../../PluginSources/reflex/PluginProcessor.cpp"
    #include "../../PluginSources/reflex/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (ReflexGuiModule, "reflex", aqp_reflex)

// the randomizer's preset types (Anything, then Super Random - no Mutate or
// Blend, which need a patch of their own), for the Random Plugin Preset module;
// saved as a Reflex XML preset
#include "../Host/RandomPresetSources.h"
namespace
{
    std::vector<int> reflexRandomEntries()      // menu indices of the entries offered
    {
        using aqp_reflex::rfx::randomizer::Kind;
        std::vector<int> v;
        const auto& m = aqp_reflex::rfx::randomizer::menu();
        for (int i = 0; i < (int) m.size(); ++i)
            if (m[(size_t) i].kind == Kind::Category || m[(size_t) i].kind == Kind::Anything || m[(size_t) i].kind == Kind::SuperRandom)
                v.push_back (i);
        return v;
    }

    const bool reflexRandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        aquanode::plugins::randompreset::Source s;
        s.pluginId = "reflex";
        s.name = "Reflex";
        s.lists = []
        {
            using aqp_reflex::rfx::randomizer::Kind;
            aquanode::plugins::randompreset::Lists l;
            const auto& m = aqp_reflex::rfx::randomizer::menu();
            for (int i : reflexRandomEntries())
            {
                const auto& e = m[(size_t) i];
                if (e.kind == Kind::Anything) l.defaultKind = l.kinds.size();
                l.kinds.add (juce::String (e.name));
                l.kindHeadings.add (juce::String (e.heading));
            }
            return l;
        };
        s.roll = [] (juce::AudioProcessor& p, int kind, int, int, juce::uint32) -> juce::String
        {
            auto* rx = dynamic_cast<aqp_reflex::ReflexProcessor*> (&p);
            const auto entries = reflexRandomEntries();
            if (rx == nullptr || entries.empty())
                return {};
            rx->randomize (entries[(size_t) juce::jlimit (0, (int) entries.size() - 1, kind)], rx->randomVariation);
            return rx->currentPresetName;
        };
        s.presetExtension = "xml";
        s.presetData = [] (juce::AudioProcessor& p, const juce::String& name, juce::MemoryBlock& out)
        {
            auto* rx = dynamic_cast<aqp_reflex::ReflexProcessor*> (&p);
            if (rx == nullptr)
                return false;
            if (name.isNotEmpty())
                rx->currentPresetName = name;
            return aquanode::plugins::randompreset::viaTempFile ("xml", [rx] (const juce::File& f) { return rx->saveXmlPreset (f); }, out);
        };
        return s;
    }());
}
