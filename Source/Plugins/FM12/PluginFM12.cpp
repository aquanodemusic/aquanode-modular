// FM12, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/fm12) inside namespace
// aqp_fm12 (see PluginPrelude.h), and its GUI module.
#include "PluginFM12Config.h"

namespace aqp_fm12
{
    #include "../../PluginSources/fm12/PluginProcessor.cpp"
    #include "../../PluginSources/fm12/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (FM12GuiModule, "fm12", aqp_fm12)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& FM12WholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "fm12";
            x.kind = "Synth";
            x.description = "FM12 whole, as one module: twelve operators with their envelopes, ratios, levels, delays and feedback, the engine modes, the LFO, the pitch envelope and the chorus, played by the plugin's own engine. Its knobs are the plugin's parameters and take cables (the 12 x 12 routing comes with the sound and is kept, not shown). Built from its default sound, a GUI card or an .fm12preset.";
            x.section = aquanode::ModuleSection::Oscillator;
            x.midi = true;
            x.audioIn = false;
            x.sideIn = false;
            x.hide = [] (const juce::String& id) { return id.startsWith ("route_"); };
            return x;
        }();
        return def;
    }

    bool FM12WholeModuleLoad (juce::AudioProcessor& o, const juce::File& f)
    {
        juce::MemoryBlock mb;
        if (f.hasFileExtension ("syx") || ! f.loadFileAsData (mb) || mb.getSize() == 0) return false;   // (a .syx: in the GUI card's DX7 menu)
        o.setStateInformation (mb.getData(), (int) mb.getSize());
        return true;
    }
}

AQP_WHOLE_PLUGIN_PATCH (FM12WholeModule, FM12WholeModuleDef, FM12WholeModuleLoad)
