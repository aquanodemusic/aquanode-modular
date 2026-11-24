// SpectralCompress, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/spectralcompress) inside namespace
// aqp_spectralcompress (see PluginPrelude.h), and its GUI module.
#include "PluginSpectralCompressConfig.h"

namespace aqp_spectralcompress
{
    #include "../../PluginSources/spectralcompress/PluginProcessor.cpp"
    #include "../../PluginSources/spectralcompress/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (SpectralCompressGuiModule, "spectralcompress", aqp_spectralcompress)

//==============================================================================
// (6.9.16) the patch version: the plugin whole, one module (Host/WholePluginPatch.h), its knobs
// on the card and its curves and spectrum under them - as the Spectral Delay module's card
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& SpectralCompressWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "spectralcompress";
            x.kind = "Compressor";
            x.description = "SpectralCompress whole, as one module: every FFT bin compressed down and up against its own "
                            "level curve (or matched to the Side In's spectrum, or compressed by it), with Attack, Release, "
                            "the Down and Up offsets, ratios, knees and amounts, a spectral morph toward Side In and Delta "
                            "to hear what it takes away. Its knobs take cables; its curves and spectrum are on the card.";
            x.section = aquanode::ModuleSection::Effect;
            x.midi = false;
            x.audioIn = true;
            x.sideIn = true;
            x.showPanel = true;
            x.panelWidth = 1240; x.panelHeight = 780; x.cardWidth = 620;
            x.hide = [] (const juce::String& id) { return id == "visSmooth"; };
            return x;
        }();
        return def;
    }

    bool SpectralCompressWholeModuleLoad (juce::AudioProcessor&, const juce::File&) { return false; }
}

AQP_WHOLE_PLUGIN_PATCH (SpectralCompressWholeModule, SpectralCompressWholeModuleDef, SpectralCompressWholeModuleLoad)
