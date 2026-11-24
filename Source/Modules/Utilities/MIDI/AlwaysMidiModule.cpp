#include "AlwaysMidiModule.h"

static aquanode::ModuleDescriptor alwaysMidiDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.alwaysmidi";
    d.displayName = "Always MIDI";
    d.description =
        "Holds one note down forever, so a generator sounds with nobody at the keyboard. Patch "
        "its MIDI Out into a generator's Add MIDI In or into a MIDI Add's MIDI In to "
        "drone a whole chord. Renew re-triggers the note rather than holding it, which is what "
        "keeps envelopes and one-shot voices like Pluck or Kick alive; it is the drone's pulse.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 23;
    d.sockets = {
        midiOut ("midiOut", "MIDI Out")
    };
    d.params = {
        // same note naming as Resonator: shows "C#4", accepts a typed note
        // name or a raw MIDI number
        makeSteppedList ("note", "Note", midiNoteNameChoices(), 48, 0),
        makeRotary ("renew", "Renew", 0.05f, 30.0f, 2.0f, 1, "s", true),
        makeRotary ("fade",  "Fade",  1.0f, 500.0f, 20.0f, 1, "ms", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (AlwaysMidiModule, alwaysMidiDescriptor)
