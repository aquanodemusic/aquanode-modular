#include "CuratedPresets.h"
#include "CuratedPresetData.h"
#include "../Engine/PluginProcessor.h"

namespace aquanode::presets
{
    int curatedPresetCount() { return curated::numEntries; }

    int curatedPresetCount (const juce::String& category)
    {
        int n = 0;
        for (int i = 0; i < curated::numEntries; ++i)
            if (category == curated::entries[i].category)
                ++n;
        return n;
    }

    int curatedPresetIndex (const juce::String& category, int n)
    {
        for (int i = 0; i < curated::numEntries; ++i)
            if (category == curated::entries[i].category && n-- == 0)
                return i;
        return -1;
    }

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
