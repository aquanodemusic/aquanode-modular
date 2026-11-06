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
