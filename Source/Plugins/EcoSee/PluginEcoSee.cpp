// EcoSee, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/ecosee) inside namespace
// aqp_ecosee (see PluginPrelude.h), and its GUI module.
#include "PluginEcoSeeConfig.h"

namespace aqp_ecosee
{
    #include "../../PluginSources/ecosee/PluginProcessor.cpp"
    #include "../../PluginSources/ecosee/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (EcoSeeGuiModule, "ecosee", aqp_ecosee)

//==============================================================================
// (6.9.16) the patch version: its Custom 3D Grid alone, one module (Host/WholePluginPatch.h) -
// the view and its three field pickers (which measure each axis shows), the sidebar left out
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& EcoSeeWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "ecosee";
            x.kind = "3D Grid";
            x.description = "EcoSee's Custom 3D Grid as one module: what comes in at Audio In (and goes on out) as points "
                            "in three dimensions, each axis the measure its picker chooses (centroid, amplitude, time and "
                            "the others EcoSee works out). Drag the view to turn it; Auto Rotate turns it by itself (off at the start), "
                            "Glow Intensity sets how brightly the points glow.";
            x.section = aquanode::ModuleSection::Utility;
            x.midi = false;
            x.audioIn = true;
            x.showPanel = true;
            x.panelWidth = 1200; x.panelHeight = 700; x.cardWidth = 560;
            x.panelCrop = { 270, 0, 930, 700 };   // (the panel without its sidebar)
            x.fixed = { { "VS_VIEW_MODE", 5.0f } };   // the Custom 3D Grid
            // (6.9.21) and Auto Rotate (off at the start: the grid holds still until it is dragged) and Glow Intensity
            static const juce::StringArray shown { "VS_GRID_X", "VS_GRID_Y", "VS_GRID_Z", "MO_AUTO_ROTATE", "VS_GLOW_INTENSITY" };
            x.startAt = { { "MO_AUTO_ROTATE", 0.0f } };
            x.wideSwitches = true;
            x.hide = [] (const juce::String& id) { return ! shown.contains (id); };
            return x;
        }();
        return def;
    }

    bool EcoSeeWholeModuleLoad (juce::AudioProcessor&, const juce::File&) { return false; }
}

AQP_WHOLE_PLUGIN_PATCH (EcoSeeWholeModule, EcoSeeWholeModuleDef, EcoSeeWholeModuleLoad)
