#pragma once

// The Patch Mutator strip.

#include "PatchCanvas.h"

//==============================================================================
// Patch Mutator - interactive evolution over the patch's knob
// values. Topology never changes; Input/Output modules are excluded so a
// mutation can never blast the master level.
class MutatorPanel : public juce::Component
{
public:
    MutatorPanel (AquanodeModularAudioProcessor&, PatchCanvas&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Mutates the whole patch `times` times in a row at `amount` (0..1) as ONE
    // step: Undo brings back the patch as it was before. Used by Preset
    // Generation's "With Mutation".
    void mutateSeveral (float amount, int times);

    // the colourway's colours (see the editor's applyBarColours)
    void applyBarColours (bool themed, juce::Colour face, juce::Colour on);
    std::vector<juce::Button*> getButtons();

private:
    // instanceId -> (paramId -> value): a "genome" of the current patch
    using Snapshot = std::map<int, std::map<juce::String, float>>;

    Snapshot capture() const;
    void apply (const Snapshot&);
    void pushHistory();
    void doMutate();
    void mutateOnce (float amount);
    void doBreed();
    void doRecall (const Snapshot&, bool exists);
    float mutateValue (const aquanode::ParamSpec&, float current, float amount);

    AquanodeModularAudioProcessor& processor;
    PatchCanvas& canvas;

    juce::Slider amountSlider;
    juce::TextButton mutateButton  { "Mutate" };
    juce::TextButton undoButton    { "Undo" };
    juce::TextButton storeAButton  { "Store A" };
    juce::TextButton storeBButton  { "Store B" };
    juce::TextButton recallAButton { "Recall A" };
    juce::TextButton recallBButton { "Recall B" };
    juce::TextButton breedButton   { "Breed AxB" };

    Snapshot slotA, slotB;
    bool hasA { false }, hasB { false };
    std::vector<Snapshot> history;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutatorPanel)
};
