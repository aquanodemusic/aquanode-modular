// The processor, part 7: undo / redo of the whole patch.
#include "PluginProcessor.h"
#include "../Editor/PluginEditor.h"
#include "../Modules/Core/ModuleRegistry.h"
#include "../Modules/Utilities/MIDI/MidiAddModule.h"
#include "../Modules/Utilities/MIDI/DiscardMidiModule.h"
#include "../Modules/Utilities/Control/DawModModule.h"
#include "../Modules/Utilities/MIDI/PianoRollModule.h"
#include "PatchStateFiles.h"

#include <algorithm>

using namespace aquanode;

//==============================================================================
// Undo / Redo of the whole patch
juce::String AquanodeModularAudioProcessor::patchFingerprint() const
{
    // cheap enough to take a few times a second: everything an edit changes, without
    // encoding samples or the plugins' own (large) states
    juce::String s;
    s.preallocateBytes (4096);
    s << patchName << "|";
    for (const auto& inst : instances)
    {
        const auto& d = inst->descriptor();
        s << inst->id << ":" << d.typeId << "@" << inst->position.x << "," << inst->position.y
          << (inst->collapsed ? "c" : "") << inst->customColour.toString() << "[";
        for (const auto& p : d.params)
        {
            if (p.type == ParamType::Button)
                continue;
            // transport, not an edit
            if ((d.typeId == "util.pianoroll" && p.id == "playing") || (d.typeId == "osc.acid303" && p.id == "running"))
                continue;
            // playing a controller, not an edit (what it learned is one)
            if (d.typeId == "io.midilearn" && (p.id == "value" || p.id == "active"))
                continue;
            s << juce::String (inst->dsp->getParameter (p.id), 5) << ",";
        }
        s << "]";
        if (d.typeId == "fx.externalplugin")
            s << inst->dsp->titleSuffix();   // which plugin (its own state is the plugin's, like the built-in ones')
        else if (! d.typeId.startsWith ("plugin."))
            s << inst->dsp->saveCustomState().hashCode64() << "/" << inst->dsp->getSampleChangeCounter();
        s << ";";
    }
    for (const auto& c : cables)
        s << c.fromModule << c.fromSocket << ">" << c.toModule << c.toSocket << ";";
    for (const auto& c : paramCables)
        s << c.fromModule << c.fromSocket << ">" << c.toModule << c.paramId << "=" << juce::String (c.depth, 4) << ";";
    return s;
}

AquanodeModularAudioProcessor::HistoryStep AquanodeModularAudioProcessor::captureStep()
{
    HistoryStep h;
    {
        const juce::ScopedValueSetter<bool> svs (skipAudioInXml, true);
        h.xml = buildPatchXml (false, nullptr);
    }
    if (h.xml != nullptr)
        if (auto* col = h.xml->getChildByName ("Colours"))
            h.xml->removeChildElement (col, true);   // colours are not part of the patch's history
    for (const auto& inst : instances)
        if (inst->dsp->hasAudioToSave())
        {
            double rate = 44100.0;
            if (auto a = inst->dsp->getAudioToSave (rate))
                h.audio[inst->id] = { a, rate };
        }
    h.name = patchName;
    h.print = patchFingerprint();
    return h;
}

void AquanodeModularAudioProcessor::restoreStep (const HistoryStep& h)
{
    if (h.xml == nullptr)
        return;
    applyPatchXml (*h.xml, nullptr);
    patchName = h.name;
    for (const auto& [id, buf] : h.audio)
        if (auto* inst = getInstance (id))
            if (buf.first != nullptr)
                inst->dsp->restoreSavedAudio (std::make_shared<juce::AudioBuffer<float>> (*buf.first), buf.second);
}

void AquanodeModularAudioProcessor::historyTick()
{
    commitHistory (false);
}

void AquanodeModularAudioProcessor::commitHistory (bool evenWithMouseDown)
{
    if (! historyStarted)
    {
        currentStep = captureStep();
        historyStarted = true;
        return;
    }
    if (! evenWithMouseDown && juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;   // a drag still going: it becomes one step when it ends
    if (patchFingerprint() == currentStep.print)
        return;
    undoSteps.push_back (std::move (currentStep));
    if ((int) undoSteps.size() > kHistorySteps)
        undoSteps.erase (undoSteps.begin());
    redoSteps.clear();
    currentStep = captureStep();
}

bool AquanodeModularAudioProcessor::undoPatch()
{
    commitHistory (true);   // an edit not yet taken in is the newest step
    if (undoSteps.empty())
        return false;
    redoSteps.push_back (std::move (currentStep));
    if ((int) redoSteps.size() > kHistorySteps)
        redoSteps.erase (redoSteps.begin());
    currentStep = std::move (undoSteps.back());
    undoSteps.pop_back();
    restoreStep (currentStep);
    currentStep.print = patchFingerprint();
    return true;
}

bool AquanodeModularAudioProcessor::redoPatch()
{
    commitHistory (true);
    if (redoSteps.empty())
        return false;
    undoSteps.push_back (std::move (currentStep));
    currentStep = std::move (redoSteps.back());
    redoSteps.pop_back();
    restoreStep (currentStep);
    currentStep.print = patchFingerprint();
    return true;
}
