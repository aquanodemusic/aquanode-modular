#include "KeyboardMidiModule.h"

static aquanode::ModuleDescriptor keyboardMidiDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.keyboardmidi";
    d.displayName = "Keyboard MIDI";
    d.description =
        "The notes you play - DAW track, controller or on-screen keys - as a cable. Generators "
        "hear the keyboard anyway; this is for the drums and Droplets, whose MIDI In turns every "
        "key into a hit. Like Discard MIDI it replaces the raw keyboard for whatever it feeds.";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 5;
    // 6.9.10: not offered any more - every generator with a MIDI In hears the
    // played keys while nothing is patched into it. Older patches still load it.
    d.listedInSidebar = false;
    d.sockets = {
        midiOut ("midiOut", "MIDI Out")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (KeyboardMidiModule, keyboardMidiDescriptor)
