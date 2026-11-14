// Tidebreeze, compiled into Aquanode Modular: the PCM wave workstation's
// sources (Source/PluginSources/tidebreeze) inside namespace aqp_tidebreeze (see
// PluginPrelude.h), and its GUI module.
#include "PluginTidebreezeConfig.h"

namespace aqp_tidebreeze
{
    #include "../../PluginSources/tidebreeze/TbWaves.cpp"        // the wave renderer
    #include "../../PluginSources/tidebreeze/TbWaveLibrary.cpp"  // the factory waves' recipes
    #include "../../PluginSources/tidebreeze/TbPrograms.cpp"     // the factory programs, random programs
    #include "../../PluginSources/tidebreeze/TbWaveBank.cpp"     // the user wave slots, rendering in the background
    #include "../../PluginSources/tidebreeze/PluginProcessor.cpp"
    #include "../../PluginSources/tidebreeze/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (TidebreezeGuiModule, "tidebreeze", aqp_tidebreeze)

// a rolled program of one of Tidebreeze's categories, for Preset Generation
namespace aquanode::plugins::tidebreeze
{
    bool rollProgram (juce::AudioProcessor& p, int category, juce::uint32 seed)
    {
        auto* tb = dynamic_cast<aqp_tidebreeze::TidebreezeAudioProcessor*> (&p);
        if (tb == nullptr)
            return false;
        tb->rollProgram (category, seed | 1u);
        return true;
    }
}

// the ROLL menu's categories, then Surprise Me (any category) and Super Random,
// for the Random Plugin Preset module; saved as a .tidebreeze preset
#include "../Host/RandomPresetSources.h"
namespace
{
    const bool tidebreezeRandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        aquanode::plugins::randompreset::Source s;
        s.pluginId = "tidebreeze";
        s.name = "Tidebreeze";
        s.lists = []
        {
            namespace tb = aqp_tidebreeze::tb;
            aquanode::plugins::randompreset::Lists l;
            for (int c : tb::categoryMenuOrder())
                l.kinds.add (tb::categoryNames()[c]);
            l.defaultKind = l.kinds.size();
            l.kinds.add ("Surprise Me (any category)");
            l.kinds.add ("Super Random");
            return l;
        };
        s.roll = [] (juce::AudioProcessor& p, int kind, int, int, juce::uint32 seed) -> juce::String
        {
            namespace tb = aqp_tidebreeze::tb;
            auto* t = dynamic_cast<aqp_tidebreeze::TidebreezeAudioProcessor*> (&p);
            if (t == nullptr)
                return {};
            const auto& order = tb::categoryMenuOrder();
            const int n = (int) order.size();
            const int category = kind < n ? order[(size_t) juce::jmax (0, kind)] : (kind == n ? -1 : tb::kSuperRandom);
            t->rollProgram (category, seed | 1u);
            return t->programName;
        };
        s.presetExtension = "tidebreeze";
        s.presetData = [] (juce::AudioProcessor& p, const juce::String& name, juce::MemoryBlock& out)
        {
            auto* t = dynamic_cast<aqp_tidebreeze::TidebreezeAudioProcessor*> (&p);
            if (t == nullptr)
                return false;
            if (name.isNotEmpty())
                t->programName = name;
            t->getStateInformation (out);         // what its own Save writes
            return out.getSize() > 0;
        };
        return s;
    }());
}
