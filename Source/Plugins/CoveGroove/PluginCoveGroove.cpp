// Cove Groove, compiled into Aquanode Modular: the hardgroove loop generator's
// sources (Source/PluginSources/covegroove, written for Aquanode Modular, in
// namespace covegroove) and its GUI module. PluginPrelude.h comes first so the
// Android file picker shim applies to its Save / Load as to every plugin's.
#include "../Host/PluginPrelude.h"

#include "../../PluginSources/covegroove/PluginProcessor.cpp"
#include "../../PluginSources/covegroove/PluginEditor.cpp"

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (CoveGrooveGuiModule, "covegroove", aqp_covegroove)


//==============================================================================
// (6.9.11) Cove Groove for the Random Plugin Preset module: its own dice
// (everything, or the groove, the kit or the sounds alone) or one of its
// factory presets; saved as a .covegroove preset
#include "../Host/RandomPresetSources.h"
namespace
{
    const bool coveGrooveRandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        namespace rp = aquanode::plugins::randompreset;
        rp::Source s;
        s.pluginId = "covegroove";
        s.name = "Cove Groove";
        s.lists = []
        {
            rp::Lists l;
            l.kinds.addArray ({ "Anything", "Groove", "Kit", "Sounds", "Factory Preset" });
            return l;
        };
        s.roll = [] (juce::AudioProcessor& p, int kind, int, int, juce::uint32 seed) -> juce::String
        {
            auto* c = dynamic_cast<covegroove::CoveGrooveProcessor*> (&p);
            if (c == nullptr) return {};
            juce::Random rng ((juce::int64) seed);
            if (kind == 4)
            {
                const int idx = rng.nextInt (juce::jmax (1, c->getNumPrograms()));
                c->setCurrentProgram (idx);
                return c->getProgramName (idx);
            }
            using R = covegroove::CoveGrooveProcessor::Random;
            c->reseed ((juce::int64) seed);
            c->randomize (kind == 1 ? R::Groove : kind == 2 ? R::Kit : kind == 3 ? R::Sounds : R::Everything);
            static const char* const names[] = { "Groove", "Groove", "Kit", "Sounds" };
            return juce::String (names[juce::jlimit (0, 3, kind)]) + " " + juce::String ((int) (seed % 9000u) + 1000);
        };
        s.presetExtension = "covegroove";
        s.presetData = [] (juce::AudioProcessor& p, const juce::String&, juce::MemoryBlock& out)
        {
            auto* c = dynamic_cast<covegroove::CoveGrooveProcessor*> (&p);
            return c != nullptr && rp::viaTempFile ("covegroove", [c] (const juce::File& f) { return c->savePreset (f); }, out);
        };
        return s;
    }());
}
