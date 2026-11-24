// Bandpass Modulator, compiled into Aquanode Modular: its sources (in
// Source/PluginSources/bandpassmod) inside namespace aqp_bandpassmod, and its
// GUI module.
#include "PluginBandpassModulatorConfig.h"

namespace aqp_bandpassmod
{
    #include "../../PluginSources/bandpassmod/PluginProcessor.cpp"
    #include "../../PluginSources/bandpassmod/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (BandpassModulatorGuiModule, "bandpassmod", aqp_bandpassmod)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& BandpassModulatorWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "bandpassmod";
            x.kind = "Effect";
            x.description = "Bandpass Modulator whole, as one module: the band-pass jumping around its range in time with the song (random from its seeded generator, or along its shapes, Skew pushing the jumps), Note Lock on chosen notes or on the notes held on MIDI In, the distortion, tempo echo and autopan on the band - the plugin's own engine. Every knob takes cables.";
            x.section = aquanode::ModuleSection::Effect;
            x.midi = true;
            x.audioIn = true;
            x.sideIn = false;
            return x;
        }();
        return def;
    }

    bool BandpassModulatorWholeModuleLoad (juce::AudioProcessor&, const juce::File&) { return false; }
}

AQP_WHOLE_PLUGIN_PATCH (BandpassModulatorWholeModule, BandpassModulatorWholeModuleDef, BandpassModulatorWholeModuleLoad)
