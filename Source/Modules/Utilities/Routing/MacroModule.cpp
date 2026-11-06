#include "MacroModule.h"

static aquanode::ModuleDescriptor macroDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.macro";
    d.displayName = "Macro";
    d.description =
        "One knob that turns many: patch Macro Out into any knobs of any modules. Each follows the Macro across "
        "its whole range (0 % = the knob's minimum, 100 % = its maximum); the cable's depth blends between the "
        "knob's own setting and the Macro, a negative depth turns it the other way.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 6;
    d.sockets = {
        unipolarOut ("macroOut", "Macro Out")
    };
    d.params = {
        makeRotary ("value", "Macro", 0.0f, 100.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MacroModule, macroDescriptor)
