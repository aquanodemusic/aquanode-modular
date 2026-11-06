#include "PanningModule.h"

static aquanode::ModuleDescriptor panningDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.panning";
    d.displayName = "Panning";
    d.description =
        "Stereo balance. An LFO cable onto Pan is auto-panning.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 4;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = { makeRotary ("pan", "Pan", -100.0f, 100.0f, 0.0f, 0, "%") };
    return d;
}

AQUANODE_REGISTER_MODULE (PanningModule, panningDescriptor)
