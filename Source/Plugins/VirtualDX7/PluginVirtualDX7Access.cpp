// VirtualDX7's .syx loading for its patch version. Kept apart from
// VirtualDX7Patch.cpp, which includes the native engine and the effects for
// itself and so cannot see the plugin's processor header as well.
#include "PluginVirtualDX7Config.h"

namespace aqp_virtualdx7
{
    #include AQP_VIRTUALDX7_SRC(PluginProcessor.h)
}

namespace aquanode::plugins::virtualdx7
{
    // a DX7 bank (or single voice) into the plugin, the way its own Import does
    bool loadSysex (juce::AudioProcessor& p, const juce::File& file)
    {
        auto* vdx = dynamic_cast<aqp_virtualdx7::VDX7AudioProcessor*> (&p);
        juce::MemoryBlock mb;
        if (vdx == nullptr || ! file.loadFileAsData (mb))
            return false;
        return vdx->importSysexBank (mb.getData(), mb.getSize(), file.getFileNameWithoutExtension());
    }
}

// VirtualDX7 has no generator of its own: for the Random Plugin Preset module it
// plays one of the built-in FM voices the sidebar's FM row picks from
// (Presets/FMVoices.cpp), of one category or any - always on its built-in
// engine (a ROM loaded through its window is put away again); saved as a
// single-voice .syx
#include "../../Presets/DX7Generator.h"
#include "../Host/RandomPresetSources.h"
namespace
{
    const bool virtualDX7RandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        aquanode::plugins::randompreset::Source s;
        s.pluginId = "virtualdx7";
        s.name = "VirtualDX7";
        s.lists = []
        {
            aquanode::plugins::randompreset::Lists l;
            l.kinds.add ("Anything");
            l.kinds.addArray (aquanode::dx7::categories());
            l.defaultKind = 0;
            return l;
        };
        s.roll = [] (juce::AudioProcessor& p, int kind, int, int, juce::uint32 seed) -> juce::String
        {
            auto* vdx = dynamic_cast<aqp_virtualdx7::VDX7AudioProcessor*> (&p);
            if (vdx == nullptr)
                return {};
            if (vdx->usingEmulator())
                vdx->unloadFirmware();
            const int category = kind - 1;      // -1: any
            std::vector<const aquanode::dx7::FactoryVoice*> pool;
            for (const auto& fv : aquanode::dx7::factoryVoices())
                if (category < 0 || fv.category == category)
                    pool.push_back (&fv);
            if (pool.empty())
                return {};
            juce::Random rng ((juce::int64) seed);
            const auto& chosen = pool[(size_t) rng.nextInt ((int) pool.size())]->voice;
            aqp_virtualdx7::vdx7::Voice v;
            std::copy (chosen.vced.begin(), chosen.vced.end(), v.vced());
            vdx->setVoice (v, true);
            return chosen.name.trim();
        };
        s.presetExtension = "syx";
        s.presetData = [] (juce::AudioProcessor& p, const juce::String&, juce::MemoryBlock& out)
        {
            auto* vdx = dynamic_cast<aqp_virtualdx7::VDX7AudioProcessor*> (&p);
            if (vdx == nullptr)
                return false;
            const auto bytes = vdx->exportVoiceSysex();
            out.replaceAll (bytes.data(), bytes.size());
            return ! bytes.empty();
        };
        return s;
    }());
}
