#include "NoiseModule.h"

static aquanode::ModuleDescriptor noiseDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "osc.noise";
    d.displayName = "Noise";
    d.description =
        "White, pink or brown noise, per voice: each held note gets its own independent stream, so a "
        "burst gated by an ADSR behaves per note. The classic excitation source - try it into "
        "Resonator, Pluck or the Formant Filter. As a per-voice source it only sounds while notes "
        "are held. MIDI In (optional) makes it answer only to the notes patched there. Filter (optional): "
        "Low + High Cut or Band Pass, with Q, and Tilt to brighten or darken.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 3;
    d.sockets = {
        audioOut ("audioOut", "Audio Out"),
        midiIn   ("midiIn",   "MIDI In")
    };
    d.params = {
        makeCombo  ("type", "Type", { "White", "Pink", "Brown" }, 0, 0, 2),   // (6.9.19: Brown)
        makeRotary ("level", "Level", 0.0f, 1.0f, 0.8f, 0)
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 0, {}, false, 1.0f).noMod()
    };
    d.params.push_back (makeOutputTrim (-9.5f));   // brought down to sit with the other sources
    // (6.9.19) the optional filter
    d.params.push_back (makeCombo  ("filter",   "Filter",    { "Off", "Low + High Cut", "Band Pass" }, 0, 1, 3).noMod());
    d.params.push_back (makeRotary ("lowCut",   "Low Cut",   20.0f, 20000.0f, 20.0f, 1, "Hz", true).visibleWhen ("filter", 1.0f));
    d.params.push_back (makeRotary ("highCut",  "High Cut",  20.0f, 20000.0f, 20000.0f, 1, "Hz", true).visibleWhen ("filter", 1.0f));
    d.params.push_back (makeRotary ("bandFreq", "Band Freq", 20.0f, 20000.0f, 1000.0f, 1, "Hz", true).visibleWhen ("filter", 2.0f));
    d.params.push_back (makeRotary ("q",        "Q",         0.5f, 12.0f, 0.707f, 1, {}, true).visibleFrom ("filter", 1.0f));
    d.params.push_back (makeRotary ("tilt",     "Tilt",      -12.0f, 12.0f, 0.0f, 1, "dB").visibleFrom ("filter", 1.0f));
    return d;
}

AQUANODE_REGISTER_MODULE (NoiseModule, noiseDescriptor)
