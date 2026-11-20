// Additive Keys, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/additivekeys) inside namespace
// aqp_additivekeys (see PluginPrelude.h), and its GUI module.
#include "PluginAdditiveKeysConfig.h"

namespace aqp_additivekeys
{
    #include "../../PluginSources/additivekeys/PluginProcessor.cpp"
    #include "../../PluginSources/additivekeys/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (AdditiveKeysGuiModule, "additivekeys", aqp_additivekeys)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& AdditiveKeysWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "additivekeys";
            x.kind = "Synth";
            x.description = "Additive Keys whole, as one module: both sources of 63 harmonics with their four envelope groups, the dynamic filter, the formant shape, the pitch and amplitude envelopes and the LFO, played by the plugin's own engine. Every knob of the plugin is a knob here and takes cables; the drawn harmonics come with the sound (from its default, a GUI card or an .akpreset).";
            x.section = aquanode::ModuleSection::Oscillator;
            x.midi = true;
            x.audioIn = false;
            x.sideIn = false;
            return x;
        }();
        return def;
    }

    bool AdditiveKeysWholeModuleLoad (juce::AudioProcessor& o, const juce::File& f)
    {
        auto* k = dynamic_cast<aqp_additivekeys::K5AudioProcessor*> (&o);
        return k != nullptr && k->presetManager.loadPreset (f);
    }
}

AQP_WHOLE_PLUGIN_PATCH (AdditiveKeysWholeModule, AdditiveKeysWholeModuleDef, AdditiveKeysWholeModuleLoad)
