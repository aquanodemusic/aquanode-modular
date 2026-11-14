// Massif, compiled into Aquanode Modular: the plugin's sources (in
// Source/PluginSources/aquamassif - the DSP engine is header-only in massif/)
// inside namespace aqp_aquamassif (see PluginPrelude.h), and its GUI module.
#include "PluginAquaMassifConfig.h"

namespace aqp_aquamassif
{
    #include "../../PluginSources/aquamassif/PluginProcessor.cpp"
    #include "../../PluginSources/aquamassif/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (AquaMassifGuiModule, "aquamassif", aqp_aquamassif)

// the patch generator, for the Random Plugin Preset module: every sound type
// (Anything, then Super Random) and every genre; saved as a .massif preset
#include "../Host/RandomPresetSources.h"
namespace
{
    const bool massifRandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        aquanode::plugins::randompreset::Source s;
        s.pluginId = "aquamassif";
        s.name = "Massif";
        s.lists = []
        {
            namespace gen = aqp_aquamassif::massif::generator;
            aquanode::plugins::randompreset::Lists l;
            for (int t = 0; t <= gen::tAnything; ++t)
            {
                l.kinds.add (gen::typeName (t));
                const char* h = gen::sectionBefore (t);
                l.kindHeadings.add (h != nullptr ? juce::String (h) : juce::String());
            }
            l.kinds.add ("Super Random");
            l.defaultKind = gen::tAnything;
            for (int g = 0; g < gen::gNumGenres; ++g)
                l.genres.add (gen::genreName (g));
            l.defaultGenre = gen::gAny;
            return l;
        };
        s.roll = [] (juce::AudioProcessor& p, int kind, int genre, int, juce::uint32) -> juce::String
        {
            namespace gen = aqp_aquamassif::massif::generator;
            auto* m = dynamic_cast<aqp_aquamassif::AquaMassifProcessor*> (&p);
            if (m == nullptr)
                return {};
            const int type = kind > gen::tAnything ? -1 : juce::jlimit (0, (int) gen::tAnything, kind);
            return m->generatePatch (type, juce::jlimit (0, (int) gen::gNumGenres - 1, genre));
        };
        s.presetExtension = "massif";
        s.presetData = [] (juce::AudioProcessor& p, const juce::String& name, juce::MemoryBlock& out)
        {
            auto* m = dynamic_cast<aqp_aquamassif::AquaMassifProcessor*> (&p);
            if (m == nullptr)
                return false;
            if (name.isNotEmpty())
                m->extra.name = name.toStdString();
            out = m->presetData();
            return out.getSize() > 0;
        };
        return s;
    }());
}
