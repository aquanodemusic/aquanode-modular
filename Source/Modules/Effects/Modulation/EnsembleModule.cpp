#include "EnsembleModule.h"

using namespace aquanode;

static ModuleDescriptor ensembleDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.ensemble";
    d.displayName = "Ensemble";
    d.description =
        "A vintage polysynth effect section: Chorus, Phase, or the three-phase BBD Ensemble with its slow "
        "sweep and faster shimmer. Mono in, wide stereo out. Put it after the voices "
        "are summed (a Poly Sum), before any other effect. Super: a unison detuner - the sound itself in the "
        "middle and up to six copies of it pitched a little up and down around it, the Super oscillator's "
        "classic layout of seven saws (Voices: how many copies, Detune: how far the outermost goes); Spread "
        "pans them out by their detune, Mix sets the copies against the middle (the Super oscillator's curves), "
        "Speed and Depth let each copy's detune drift slowly.";
    d.section = ModuleSection::Effect;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 40;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("mode",   "Mode", { "Chorus", "Phase", "Ensemble", "Super" }, 2, 0, 2),
        makeRotary ("speed",  "Speed",  0.0f, 10.0f, 3.5f, 0),
        makeRotary ("depth",  "Depth",  0.0f, 100.0f, 85.0f, 1, "%"),
        makeRotary ("spread", "Spread", 0.0f, 100.0f, 80.0f, 1, "%"),
        makeRotary ("mix",    "Mix",    0.0f, 100.0f, 100.0f, 1, "%"),
        // (6.9.16) Super only: the unison detuner's copies and how far they go
        makeRotary ("voices", "Voices", 1.0f, 6.0f, 6.0f, 2, {}, false, 1.0f).noMod().visibleWhen ("mode", 3.0f),
        makeRotary ("detune", "Detune", 0.0f, 100.0f, 22.0f, 2, "ct").visibleWhen ("mode", 3.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (EnsembleModule, ensembleDescriptor)
