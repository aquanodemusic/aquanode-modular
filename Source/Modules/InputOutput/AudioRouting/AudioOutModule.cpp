#include "AudioOutModule.h"

static aquanode::ModuleDescriptor audioOutDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "io.audioOut";
    d.displayName = "Audio Out";
    d.description =
        "The patch's output back to the host, with a level knob. Every patch needs one: whatever "
        "reaches this Audio Out is what the DAW hears. Soft Clip rounds off what would clip: the "
        "signal is left as it is below a knee (-3 dBFS at 25 %, -12 dBFS at 100 %) and bends "
        "smoothly above it, leveling off at -1 dBFS and never past 0 dBFS, at twice the sample rate (no harsh "
        "aliasing; 0.65 ms of delay while on). 0 % switches it off. It works on the patch's whole output: "
        "every Audio Out added together, at the highest Soft Clip among them.";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 1;
    d.sockets = { audioIn ("audioIn", "Audio In") };
   #if AQUANODE_FX_BUILD
    constexpr float softClipDefault = 0.0f;    // an effect passes the track through untouched
   #else
    constexpr float softClipDefault = 25.0f;   // on, also in patches saved before it existed
   #endif
    d.params = { makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 0),
                 makeRotary ("softClip", "Soft Clip", 0.0f, 100.0f, softClipDefault, 0, "%").noMod() };
    return d;
}

AQUANODE_REGISTER_MODULE (AudioOutModule, audioOutDescriptor)
