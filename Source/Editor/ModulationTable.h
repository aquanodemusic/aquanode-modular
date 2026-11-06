#pragma once

// The sidebar's modulation table.

#include <JuceHeader.h>
#include "../Engine/PluginProcessor.h"
#include "../Modules/Core/TapToTypeEditor.h"
#include "../Engine/PatchRender.h"

//==============================================================================
// The Modulation table at the end of the sidebar: one row for every knob
// cable of the patch - its source, the module and knob it moves, its Amount
// (-100..100 %) and its Bipolar switch (both ways around the knob, or one
// way, as in Vita). Rows come and go with the cables; refresh() is polled.
class ModulationTable : public juce::Component
{
public:
    explicit ModulationTable (AquanodeModularAudioProcessor& p);
    ~ModulationTable() override;

    void refresh();                          // follow the patch's knob cables
    std::function<void()> onHeightChanged;   // the sidebar lays itself out again

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct RowView;
    struct Key { int from; juce::String socket; int to; juce::String param; };
    int desiredHeight() const;
    int findIndex (const Key& k) const;

    AquanodeModularAudioProcessor& processor;
    std::vector<std::unique_ptr<RowView>> rowViews;
    juce::String structure;   // which cables the rows show

    static constexpr int headerH = 44, rowH = 66;
};
