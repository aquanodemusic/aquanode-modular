#include "AudioInModule.h"

static aquanode::ModuleDescriptor audioInDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.audioIn";
    d.displayName = "Audio In";
    d.description =
        "Brings the host's audio input into the patch: the signal of the track Aquanode Modular FX "
        "sits on as an effect, or the audio interface / microphone input in the standalone app and "
        "on Android. Run it through any modules and end at Audio Out.";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 0;
    d.sockets = { audioOut ("audioOut", "Audio Out") };
    d.params = { makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0) };
    return d;
}

AQUANODE_REGISTER_MODULE (AudioInModule, audioInDescriptor)
