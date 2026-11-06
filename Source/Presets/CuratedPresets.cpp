#include "CuratedPresets.h"
#include "CuratedPresetData.h"
#include "../Engine/PluginProcessor.h"

namespace aquanode::presets
{
    int curatedPresetCount() { return curated::numEntries; }

    juce::String curatedPresetName (int index)
    {
        if (index < 0 || index >= curated::numEntries)
            return {};
        return juce::String::fromUTF8 (curated::entries[index].name);
    }

    juce::String loadCuratedPreset (AquanodeModularAudioProcessor& p, int index)
    {
        if (index < 0 || index >= curated::numEntries)
            return {};
        const auto& e = curated::entries[index];
        juce::MemoryInputStream in (e.data, (size_t) e.size, false);
        const auto name = curatedPresetName (index);
        if (! p.importPatchFromStream (in, name))
            return {};
        p.generatedKeepsTrims = true;   // its sound sources at the levels it was saved with
        return name;
    }
}
