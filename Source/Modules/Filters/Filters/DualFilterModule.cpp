#include "DualFilterModule.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

// DualFilterCore: the high-pass's high into the low-pass's low (its input
// clip is unity at ordinary levels), then Output
bool DualFilterModule::magnitudeResponseDb (const float* hz, float* db, int n) const
{
    const double hpFc = juce::jlimit (8.0, sampleRate * 0.45, (double) getParameterBase (pHpfCutoff));
    const double lpFc = juce::jlimit (8.0, sampleRate * 0.45, (double) getParameterBase (pLpfCutoff));
    const double hpK = vintagedsp::ResonantSvf::damping (getParameterBase (pHpfPeak) * 0.1f);
    const double lpK = vintagedsp::ResonantSvf::damping (getParameterBase (pLpfPeak) * 0.1f);
    const double out = getParameterBase (pOutput);
    for (int i = 0; i < n; ++i)
        db[i] = response::db (out * response::svf (response::sOf (hz[i], hpFc, sampleRate), hpK).high
                                  * response::svf (response::sOf (hz[i], lpFc, sampleRate), lpK).low);
    return true;
}

static ModuleDescriptor dualFilterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.dualfilter";
    d.displayName = "Dual Filter";
    d.description =
        "A resonant 12 dB high-pass into a resonant 12 dB low-pass, with "
        "the soft-clipped Peak that makes it scream (above ~9 it self-oscillates). Each filter "
        "has a CV input with its own depth (10 = 3 octaves per unit): a Skew LFO "
        "Tri Out, an Analog EG, or S&H fed by pink noise for the classic random filter jumps.";
    d.section = ModuleSection::Filter;
    d.family = ModuleFamily::Vintage;
    d.sidebarOrder = 40;
    d.sockets = {
        audioIn  ("audioIn", "Audio In"),
        modIn    ("hpfExt",  "HPF CV"),   // (6.9.19: the MG inputs went - one CV input per filter)
        modIn    ("lpfExt",  "LPF CV"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("hpfCutoff", "HPF Cutoff",  25.0f, 19000.0f, 25.0f, 0, "Hz", true),
        makeRotary ("hpfPeak",   "HPF Peak",    0.0f, 10.0f, 0.0f, 0),
        makeRotary ("hpfMg",     "HPF MG",      0.0f, 10.0f, 0.0f, 0).hide(),   // (6.9.19: kept for older patches)
        makeRotary ("hpfExt",    "HPF CV",      0.0f, 10.0f, 0.0f, 0),
        makeRotary ("lpfCutoff", "LPF Cutoff",  25.0f, 19000.0f, 19000.0f, 1, "Hz", true),
        makeRotary ("lpfPeak",   "LPF Peak",    0.0f, 10.0f, 0.0f, 1),
        makeRotary ("lpfMg",     "LPF MG",      0.0f, 10.0f, 0.0f, 1).hide(),
        makeRotary ("lpfExt",    "LPF CV",      0.0f, 10.0f, 0.0f, 1),
        makeRotary ("output",    "Output",      0.0f, 2.0f, 1.0f, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DualFilterModule, dualFilterDescriptor)
