#include "CrossfadeModule.h"

static aquanode::ModuleDescriptor crossfadeDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.crossfade";
    d.displayName = "Crossfade";
    d.description =
        "An equal-power A/B crossfader. A cable onto Fade moves it (a negative depth inverts it): "
        "an LFO there auto-morphs between two sound sources, a Gate "
        "hard-switches them.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 17;
    d.sockets = {
        audioIn  ("aIn",    "A In"),
        audioIn  ("bIn",    "B In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("fade",     "Fade",      0.0f, 100.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CrossfadeModule, crossfadeDescriptor)

static aquanode::ModuleDescriptor abSwitchDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.abswitch";            // (the A/B Switch of 6.1: saved patches keep finding it)
    d.displayName = "A/B Fader";
    d.description =
        "Compares two sounds or two effects on the same sound: send the source into both and bring "
        "both back here. Listen: Fade (the default) moves smoothly from A only (knob fully left) to "
        "B only (fully right), linearly, so two identical signals stay at the same level anywhere "
        "between; A or B alone; or Difference (A minus B: what one does that the other does not - "
        "silence means they are identical, and a latency mismatch shows as a comb). Every change "
        "glides over 5 ms, so it never clicks.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 18;
    d.sockets = {
        audioIn  ("aIn",    "A In"),
        audioIn  ("bIn",    "B In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        // (Fade is the last choice so that patches saved with A, B or Difference keep them)
        makeCombo ("listen", "Listen", { "A", "B", "Difference", "Fade" }, 3, 0, 3),
        makeRotary ("balance", "A - B", 0.0f, 100.0f, 50.0f, 0, "%").visibleWhen ("listen", 3.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ABSwitchModule, abSwitchDescriptor)

static aquanode::ModuleDescriptor latencyDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.latency";
    d.displayName = "Latency";
    d.description =
        "Adds latency to a chain: delays what goes through by whole milliseconds (Coarse, up to 2 s) "
        "and hundredths of one (Fine, fractional samples come out interpolated), or by a number of "
        "blocks of a chosen size. Use it to line one path up with another - a parallel chain through "
        "a plugin with latency of its own, the two inputs of the A/B Fader in Difference. With "
        "Report on, the delay counts toward the latency the patch reports, so a DAW lines its "
        "track up too. A change fades over 10 ms, without clicks.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 19;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo ("mode", "Unit", { "Milliseconds", "Blocks" }, 0, 0, 2).noMod(),
        makeRotary ("coarse", "Coarse", 0.0f, 2000.0f, 0.0f, 0, "ms", false, 1.0f).visibleWhen ("mode", 0.0f).noMod(),
        makeRotary ("fine",   "Fine",   0.0f, 99.0f,   0.0f, 0, "/100 ms", false, 1.0f).visibleWhen ("mode", 0.0f).noMod(),
        makeRotary ("blocks", "Blocks", 0.0f, 64.0f,   1.0f, 0, "", false, 1.0f).visibleWhen ("mode", 1.0f).noMod(),
        makeCombo ("blockSize", "Block Size", { "16", "32", "64", "128", "256", "512", "1024", "2048" }, 1, 0, 2).visibleWhen ("mode", 1.0f).noMod(),
        makeCombo ("report", "Report", { "Off", "On" }, 1, 1, 1).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LatencyModule, latencyDescriptor)
