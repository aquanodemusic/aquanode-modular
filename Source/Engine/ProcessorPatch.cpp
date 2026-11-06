// The processor, part 4: the patch model - modules, cables, knob cables.
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
// patch model
//==============================================================================
void AquanodeModularAudioProcessor::initInstanceBuffers (ModuleInstance& inst)
{
    inst.numIn = juce::jmax (1, inst.descriptor().numInputs());
    inst.numOut = juce::jmax (1, inst.descriptor().numOutputs());
    inst.inBuf.assign ((size_t) inst.numIn, { 0.0f, 0.0f });
    inst.outBuf.assign ((size_t) inst.numOut, { 0.0f, 0.0f });
    inst.prevOutBuf.assign ((size_t) inst.numOut, { 0.0f, 0.0f });

    // per-voice lanes for anything that can run per-voice (PerVoice or Flexible)
    if (inst.dsp->voiceMode() != aquanode::VoiceMode::Global)
    {
        inst.outV.assign ((size_t) (inst.numOut * aquanode::kMaxVoices), { 0.0f, 0.0f });
        inst.prevOutV.assign ((size_t) (inst.numOut * aquanode::kMaxVoices), { 0.0f, 0.0f });
    }
}

void AquanodeModularAudioProcessor::attachModuleHooks (ModuleInstance& inst)
{
    // every Piano Roll's Play All / Stop All acts on the whole patch
    if (auto* roll = dynamic_cast<PianoRollModule*> (inst.dsp.get()))
    {
        roll->playAllRolls = [this] (bool play) { playAllPianoRolls (play); };
        roll->anyRollPlaying = [this] { return anyPianoRollPlaying(); };
    }
}

void AquanodeModularAudioProcessor::playAllPianoRolls (bool play)
{
    rollCommandPlay.store (play, std::memory_order_relaxed);
    rollCommandSerial.fetch_add (1, std::memory_order_release);
}

bool AquanodeModularAudioProcessor::anyPianoRollPlaying() const
{
    for (const auto& inst : instances)
        if (auto* roll = dynamic_cast<const PianoRollModule*> (inst->dsp.get()))
            if (roll->isPlaying())
                return true;
    return false;
}

int AquanodeModularAudioProcessor::addModule (const juce::String& typeId, juce::Point<int> position)
{
    auto* reg = ModuleFactory::instance().find (typeId);
    if (reg == nullptr)
        return -1;

    auto inst = std::make_shared<ModuleInstance>();
    inst->id = nextInstanceId++;
    inst->reg = reg;
    inst->dsp = ModuleFactory::instance().createInstance (typeId);
    inst->position = position;
    initInstanceBuffers (*inst);
    attachModuleHooks (*inst);

    if (isPrepared)
        inst->dsp->prepare (currentSampleRate);

    instances.push_back (inst);
    rebuildGraph();
    return inst->id;
}

void AquanodeModularAudioProcessor::removeModule (int instanceId)
{
    cables.erase (std::remove_if (cables.begin(), cables.end(),
        [instanceId] (const CableInfo& c) { return c.fromModule == instanceId || c.toModule == instanceId; }),
        cables.end());
    paramCables.erase (std::remove_if (paramCables.begin(), paramCables.end(),
        [instanceId] (const ParamCableInfo& c) { return c.fromModule == instanceId || c.toModule == instanceId; }),
        paramCables.end());

    for (auto it = instances.begin(); it != instances.end(); ++it)
    {
        if ((*it)->id == instanceId)
        {
            graveyard.emplace_back (graphSerial + 1, std::static_pointer_cast<void> (*it));
            instances.erase (it);
            break;
        }
    }
    rebuildGraph();
    moduleRandomUndo.erase (instanceId);

    // full refresh: wipe every remaining module's transient state so nothing
    // from the deleted patch (held voices, feedback tails, delay lines) lingers
    resetAllPending.store (true, std::memory_order_release);
}

int AquanodeModularAudioProcessor::cloneModule (int instanceId)
{
    auto* src = getInstance (instanceId);
    if (src == nullptr)
        return -1;

    const int newId = addModule (src->descriptor().typeId, src->position + juce::Point<int> (24, 24));
    if (auto* dst = getInstance (newId))
    {
        for (const auto& p : src->descriptor().params)
            dst->dsp->setParameter (p.id, src->dsp->getParameter (p.id));

        const auto custom = src->dsp->saveCustomState();
        if (custom.isNotEmpty())
            dst->dsp->loadCustomState (custom);
        dst->customColour = src->customColour;

        if (src->dsp->hasAudioToSave())
        {
            double rate = 44100.0;
            if (auto audio = src->dsp->getAudioToSave (rate))
                dst->dsp->restoreSavedAudio (std::make_shared<juce::AudioBuffer<float>> (*audio), rate);
        }

        // a clone of a Reroute module joins its bus at once
        if (dst->dsp->rerouteBus() >= 0)
            rebuildGraph();
    }
    return newId;
}

void AquanodeModularAudioProcessor::setModulePosition (int instanceId, juce::Point<int> position)
{
    if (auto* inst = getInstance (instanceId))
        inst->position = position;
}

bool AquanodeModularAudioProcessor::addCable (int fromModule, const juce::String& fromSocket,
                                              int toModule, const juce::String& toSocket)
{
    auto* src = getInstance (fromModule);
    auto* dst = getInstance (toModule);
    if (src == nullptr || dst == nullptr)
        return false;

    // an input that only did what a knob cable does is a knob cable now
    if (dst->descriptor().inputIndexOf (toSocket) < 0)
        if (const auto* legacy = aquanode::legacyKnobInput (dst->descriptor().typeId, toSocket))
            return addParamCable (fromModule, fromSocket, toModule, legacy->param,
                                  legacy->depthParam != nullptr
                                      ? dst->dsp->legacyValue (legacy->depthParam, legacy->depthDefault) * legacy->depthScale
                                      : legacy->fixedDepth);

    // only output -> input is legal
    const int srcIdx = src->descriptor().outputIndexOf (fromSocket);
    const int dstIdx = dst->descriptor().inputIndexOf (toSocket);
    if (srcIdx < 0 || dstIdx < 0)
        return false;

    // MIDI is its own domain: a Midi Out only ever feeds an Add Midi In, and
    // an Add Midi In never accepts audio or modulation
    const bool srcMidi = src->descriptor().outputs()[(size_t) srcIdx]->kind == aquanode::SocketKind::Midi;
    const bool dstMidi = dst->descriptor().inputs()[(size_t) dstIdx]->kind == aquanode::SocketKind::Midi;
    if (srcMidi != dstMidi)
        return false;

    // Several MIDI sources may feed one socket: a generator can follow an
    // Arp AND a Midi Add, or drone on three Always Midis at once. They stack
    // rather than replace, so nothing is removed here.

    // no exact duplicates (fan-out and fan-in are otherwise unrestricted)
    for (const auto& c : cables)
        if (c.fromModule == fromModule && c.fromSocket == fromSocket
         && c.toModule == toModule && c.toSocket == toSocket)
            return false;

    cables.push_back ({ fromModule, fromSocket, toModule, toSocket });
    rebuildGraph();
    return true;
}

void AquanodeModularAudioProcessor::removeCable (int cableIndex)
{
    if (cableIndex >= 0 && cableIndex < (int) cables.size())
    {
        cables.erase (cables.begin() + cableIndex);
        rebuildGraph();
    }
}

bool AquanodeModularAudioProcessor::addParamCable (int fromModule, const juce::String& fromSocket,
                                                   int toModule, const juce::String& paramId, float depth)
{
    auto* src = getInstance (fromModule);
    auto* dst = getInstance (toModule);
    if (src == nullptr || dst == nullptr)
        return false;
    if (src->descriptor().outputIndexOf (fromSocket) < 0)
        return false;

    // target must be a modulatable rotary parameter that is either visible or
    // explicitly exposed as a cable target by the module's custom UI (e.g.
    // the Pitch Lock Filter's note keys)
    const aquanode::ParamSpec* spec = nullptr;
    for (const auto& ps : dst->descriptor().params)
        if (ps.id == paramId) { spec = &ps; break; }
    // every knob-like control takes cables: round knobs, stepped knobs
    // (Always Midi's Note, a delay's synced Time...) and horizontal bars -
    // which is what lets a DAW Mod automate them from the host
    const bool knobLike = spec != nullptr && (spec->type == aquanode::ParamType::Rotary
                                              || spec->type == aquanode::ParamType::RotarySteppedList
                                              || spec->type == aquanode::ParamType::HBar);
    if (! knobLike
        || (spec->hidden && ! spec->hiddenCableTarget) || ! spec->modulatable)
        return false;

    // duplicate (same source socket -> same knob): only update the depth
    for (auto& pc : paramCables)
        if (pc.fromModule == fromModule && pc.fromSocket == fromSocket
            && pc.toModule == toModule && pc.paramId == paramId)
        {
            pc.depth = juce::jlimit (-1.0f, 1.0f, depth);
            rebuildGraph();
            return true;
        }

    ParamCableInfo pc;
    pc.fromModule = fromModule;
    pc.fromSocket = fromSocket;
    pc.toModule = toModule;
    pc.paramId = paramId;
    pc.depth = juce::jlimit (-1.0f, 1.0f, depth);
    pc.bipolar = ! outputIsUnipolar (fromModule, fromSocket);
    paramCables.push_back (pc);
    rebuildGraph();
    return true;
}

void AquanodeModularAudioProcessor::removeParamCable (int index)
{
    if (index >= 0 && index < (int) paramCables.size())
    {
        paramCables.erase (paramCables.begin() + index);
        rebuildGraph();
    }
}

bool AquanodeModularAudioProcessor::outputIsUnipolar (int moduleId, const juce::String& socket) const
{
    if (auto* inst = getInstance (moduleId))
        for (const auto* sock : inst->descriptor().outputs())
            if (sock->id == socket)
                return sock->unipolar;
    return false;
}

void AquanodeModularAudioProcessor::setParamCableBipolar (int index, bool bipolar)
{
    if (index >= 0 && index < (int) paramCables.size() && paramCables[(size_t) index].bipolar != bipolar)
    {
        paramCables[(size_t) index].bipolar = bipolar;
        rebuildGraph();
    }
}

void AquanodeModularAudioProcessor::setParamCableDepth (int index, float depth)
{
    if (index >= 0 && index < (int) paramCables.size())
    {
        paramCables[(size_t) index].depth = juce::jlimit (-1.0f, 1.0f, depth);
        rebuildGraph();
    }
}

std::vector<int> AquanodeModularAudioProcessor::getModuleIds() const
{
    std::vector<int> ids;
    for (const auto& i : instances)
        ids.push_back (i->id);
    return ids;
}

ModuleInstance* AquanodeModularAudioProcessor::getInstance (int instanceId) const
{
    for (const auto& i : instances)
        if (i->id == instanceId)
            return i.get();
    return nullptr;
}

void AquanodeModularAudioProcessor::initializePatch()
{
    clearPatch();
    patchName = "Init";

    // The one difference between the two builds. Aquanode Modular FX (its
    // project file defines AQUANODE_FX_BUILD=1) starts from the track's own
    // audio passed straight through, Audio In -> Audio Out, so an effect patch
    // starts where it wants to start. The synth starts from an Oscillator into
    // a Polyphony Sum into Audio Out, so it plays as soon as you press a key:
    // voices are summed with no per-voice scaling, and without the Polyphony
    // Sum a six-note chord would arrive at about six times the level of one
    // note and clip (worst on Android, where the device output stage distorts
    // on top). Its Amount defaults to 1/sqrt(N), the law for uncorrelated voices.
   #if AQUANODE_FX_BUILD
    const int src = addModule ("io.audioIn",  { 60, 60 });
    const int out = addModule ("io.audioOut", { 460, 60 });
    addCable (src, "audioOut", out, "audioIn");
   #else
    const int src  = addModule ("osc.basic",    { 60, 60 });
    const int out  = addModule ("io.audioOut",  { 460, 60 });
    const int psum = addModule ("util.polysum", { 260, 60 });
    addCable (src,  "audioOut", psum, "audioIn");
    addCable (psum, "audioOut", out,  "audioIn");
   #endif

    resetAllPending.store (true, std::memory_order_release);   // wipe voices + DSP state
    rebuildGraph();
}

juce::String AquanodeModularAudioProcessor::generatePatch (
    const std::function<juce::String (AquanodeModularAudioProcessor&)>& build)
{
    clearPatch();
    generatedKeepsTrims = false;
    const auto name = build != nullptr ? build (*this) : juce::String();
    patchName = name.isNotEmpty() ? name : juce::String ("Generated");

    // The generators balance their own patches (measured trims, Volume
    // modules): their sound sources play at the level those were made for,
    // not brought down a second time - a generated patch sounds as designed,
    // its drives and folders pushed as hard as before
    if (! generatedKeepsTrims)
        for (const auto& inst : instances)
            for (const auto& spec : inst->descriptor().params)
                if (spec.calibration)
                    inst->dsp->setParameter (spec.id, 1.0f);
    generatedKeepsTrims = false;
    resetAllPending.store (true, std::memory_order_release);   // wipe voices + DSP state
    rebuildGraph();
    uiFitOnOpen = true;
    return patchName;
}

void AquanodeModularAudioProcessor::clearPatch()
{
    cables.clear();
    paramCables.clear();
    for (auto& inst : instances)
        graveyard.emplace_back (graphSerial + 1, std::static_pointer_cast<void> (inst));
    instances.clear();
    nextInstanceId = 1;
    moduleRandomUndo.clear();   // the ids start over
}
