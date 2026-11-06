#include "ArpModule.h"

static aquanode::ModuleDescriptor arpDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.arp";
    d.displayName = "Arp";
    d.description =
        "Holds whatever chord you play and walks through it in time, from the host tempo. Patch "
        "its Midi Out into a generator's Add Midi In and that generator hears ONLY the arpeggio - "
        "the difference from Midi Add, which layers notes on top of those you play. Generators "
        "with nothing patched keep playing the chord, so one Arp'd Oscillator over one plain pad "
        "is a two-cable trick. Midi In arpeggiates another stream instead of your keys - a Piano Roll's chords, say. "
        "Chance: how likely each step is to play (the others rest); Ratchet: how often a step becomes a quick "
        "burst of Hits (2-4) - at 100 % and 0 % they change nothing.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 22;
    d.sockets = {
        midiIn  ("midiIn",  "Midi In"),
        midiOut ("midiOut", "Midi Out")
    };
    d.params = {
        makeSteppedList ("division", "Rate", seqDivisionChoices(), 8, 0),
        makeCombo  ("mode",    "Mode",    { "Up", "Down", "Up/Down", "Random", "As Played" }, 0, 0, 2),
        makeRotary ("octaves", "Octaves", 1.0f, 4.0f, 1.0f, 0, {}, false, 1.0f),
        makeRotary ("gate",    "Gate",    5.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("swing",   "Swing",   0.0f, 75.0f, 0.0f, 1, "%"),
        // optional: at 100 % / 0 % the arpeggio plays as it always did
        makeRotary ("chance",  "Chance",  0.0f, 100.0f, 100.0f, 1, "%", false, 1.0f),
        makeRotary ("ratchet", "Ratchet", 0.0f, 100.0f, 0.0f, 1, "%", false, 1.0f),
        makeRotary ("hits",    "Hits",    2.0f, 4.0f, 2.0f, 1, {}, false, 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ArpModule, arpDescriptor)
