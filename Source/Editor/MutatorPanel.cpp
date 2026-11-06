// The Patch Mutator strip: mutate, breed, store and recall.
#include "PluginEditor.h"
#include "../Engine/Mutation.h"
#include "../Presets/EffectAdders.h"
#include "../Presets/PresetGenerators.h"
#include "../Plugins/Host/PluginCatalog.h"
#include "../Plugins/Host/HostedPlugin.h"
#include "../Platform/AndroidDocuments.h"
#include <map>
#include <set>
#include "EditorShared.h"

//==============================================================================
// MutatorPanel
//==============================================================================
MutatorPanel::MutatorPanel (AquanodeModularAudioProcessor& p, PatchCanvas& c)
    : processor (p), canvas (c)
{
    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setRange (1.0, 100.0, 1.0);
    amountSlider.setValue (juce::jlimit (1.0, 100.0, (double) processor.mutationAmount * 100.0), juce::dontSendNotification);
    // the cards' own Randomize uses the same amount
    amountSlider.onValueChange = [this] { processor.mutationAmount = (float) amountSlider.getValue() * 0.01f; };
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
    amountSlider.setTextValueSuffix (" %");
    addAndMakeVisible (amountSlider);

    for (auto* b : { &mutateButton, &undoButton, &storeAButton, &storeBButton,
                     &recallAButton, &recallBButton, &breedButton })
        addAndMakeVisible (b);

    mutateButton.onClick  = [this] { doMutate(); };
    undoButton.onClick    = [this]
    {
        if (! history.empty())
        {
            apply (history.back());
            history.pop_back();
            canvas.refreshAllModuleValues();
        }
    };
    storeAButton.onClick  = [this] { slotA = capture(); hasA = true; };
    storeBButton.onClick  = [this] { slotB = capture(); hasB = true; };
    recallAButton.onClick = [this] { doRecall (slotA, hasA); };
    recallBButton.onClick = [this] { doRecall (slotB, hasB); };
    breedButton.onClick   = [this] { doBreed(); };
}

void MutatorPanel::paint (juce::Graphics& g)
{
    g.fillAll (aquanode::barColour().withAlpha ((juce::uint8) 0xdd));   // the strip, in the bars' colour
    g.setColour (aquanode::barTextColour().withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Amount", 8, 0, 52, getHeight(), juce::Justification::centredLeft);
}

void MutatorPanel::applyBarColours (bool themed, juce::Colour face, juce::Colour on)
{
    for (auto id : { juce::Slider::trackColourId, juce::Slider::backgroundColourId, juce::Slider::thumbColourId,
                     juce::Slider::textBoxTextColourId, juce::Slider::textBoxOutlineColourId })
        amountSlider.removeColour (id);
    if (themed)
    {
        amountSlider.setColour (juce::Slider::trackColourId, on);
        amountSlider.setColour (juce::Slider::backgroundColourId, face);
        amountSlider.setColour (juce::Slider::thumbColourId, on.brighter (0.3f));
        amountSlider.setColour (juce::Slider::textBoxTextColourId, aquanode::barTextColour());
        amountSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
}

std::vector<juce::Button*> MutatorPanel::getButtons()
{
    return { &mutateButton, &undoButton, &storeAButton, &storeBButton, &recallAButton, &recallBButton, &breedButton };
}

void MutatorPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    area.removeFromLeft (56);                              // "Amount" label
    amountSlider.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (8);

    const int bw = (area.getWidth() - 6 * 4) / 7;
    for (auto* b : { &mutateButton, &undoButton, &storeAButton, &storeBButton,
                     &recallAButton, &recallBButton, &breedButton })
    {
        b->setBounds (area.removeFromLeft (bw));
        area.removeFromLeft (4);
    }
}

MutatorPanel::Snapshot MutatorPanel::capture() const
{
    Snapshot snap;
    for (const int id : processor.getModuleIds())
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr)
            continue;

        auto& values = snap[id];
        for (const auto& p : inst->descriptor().params)
            if (p.type != ParamType::Button)
                values[p.id] = inst->dsp->getParameter (p.id);
    }
    return snap;
}

void MutatorPanel::apply (const Snapshot& snap)
{
    for (const auto& [id, values] : snap)
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr)
            continue;   // topology changed since capture - skip gracefully

        for (const auto& [paramId, value] : values)
            inst->dsp->setParameter (paramId, value);
    }
    processor.routingChanged();   // a recalled Reroute bus joins its Returns
}

void MutatorPanel::pushHistory()
{
    history.push_back (capture());
    if (history.size() > 64)
        history.erase (history.begin());
}

float MutatorPanel::mutateValue (const ParamSpec& p, float v, float amount)
{
    return aquanode::mutation::mutateValue (p, v, amount, rng);
}

void MutatorPanel::doMutate()
{
    pushHistory();
    mutateOnce ((float) amountSlider.getValue() * 0.01f);
    canvas.refreshAllModuleValues();
}

void MutatorPanel::mutateSeveral (float amount, int times)
{
    pushHistory();
    for (int i = 0; i < times; ++i)
        mutateOnce (amount);
    canvas.refreshAllModuleValues();
}

void MutatorPanel::mutateOnce (float amount)
{
    for (const int id : processor.getModuleIds())
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr || aquanode::mutation::skipsModule (inst->descriptor()))
            continue;   // Audio In/Out levels, the Piano Roll's arrangement

        for (const auto& p : inst->descriptor().params)
        {
            if (aquanode::mutation::skipsParam (inst->descriptor(), p))
                continue;   // Voices, FM Ratio, the Delays (see Mutation.h)

            const float v = inst->dsp->getParameter (p.id);
            inst->dsp->setParameter (p.id, mutateValue (p, v, amount));
        }
    }
}

void MutatorPanel::doBreed()
{
    if (! hasA || ! hasB)
        return;

    pushHistory();
    const float amount = (float) amountSlider.getValue() * 0.01f;

    for (const int id : processor.getModuleIds())
    {
        auto* inst = processor.getInstance (id);
        if (inst == nullptr || aquanode::mutation::skipsModule (inst->descriptor()))
            continue;

        const auto itA = slotA.find (id);
        const auto itB = slotB.find (id);

        for (const auto& p : inst->descriptor().params)
        {
            if (p.type == ParamType::Button || p.calibration)
                continue;
            // the Delays are inherited from neither parent: they stay
            if (aquanode::mutation::isKeptDelay (p))
                continue;

            // uniform crossover: each gene comes from parent A or B; fall
            // back to the current value when a parent doesn't know this param
            float v = inst->dsp->getParameter (p.id);
            const auto* parent = rng.nextBool() ? (itA != slotA.end() ? &itA->second : nullptr)
                                                : (itB != slotB.end() ? &itB->second : nullptr);
            if (parent != nullptr)
            {
                const auto pv = parent->find (p.id);
                if (pv != parent->end())
                    v = pv->second;
            }

            // light mutation on top of the crossover - except Voices, which is
            // inherited as-is (polyphony shouldn't drift with each breed)
            inst->dsp->setParameter (p.id, p.id == "voices" ? v
                                                            : mutateValue (p, v, amount * 0.3f));
        }
    }

    canvas.refreshAllModuleValues();
}

void MutatorPanel::doRecall (const Snapshot& snap, bool exists)
{
    if (! exists)
        return;
    pushHistory();
    apply (snap);
    canvas.refreshAllModuleValues();
}
