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

AQP_WHOLE_PLUGIN_PATCH (AM20WholeModule, AM20WholeModuleDef, AM20WholeModuleLoad)
