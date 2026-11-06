// The processor, part 5: the patch compiled into the graph the audio thread runs.
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
// graph compilation: DFS topological order + back-edge (feedback) detection
//==============================================================================
void AquanodeModularAudioProcessor::rebuildGraph()
{
    purgeRetired();

    auto graph = std::make_shared<ProcessGraph>();
    graph->serial = ++graphSerial;

    const int n = (int) instances.size();

    // resolve cables to (node, socket) index edges; drop any stale ones
    struct RawEdge { int from, fromSock, to, toSock; SocketKind fromKind, toKind; bool feedback = false;
                     bool isParam = false; int paramIdx = -1; float scale = 0.0f;
                     bool absolute = false; float depth = 0.0f, minValue = 0.0f, maxValue = 1.0f;
                     float polMul = 1.0f, polAdd = 0.0f; };
    std::vector<RawEdge> rawEdges;
    std::vector<std::vector<int>> adjacency ((size_t) n);   // outgoing raw-edge indices

    auto nodeIndexOf = [this, n] (int instanceId) -> int
    {
        for (int i = 0; i < n; ++i)
            if (instances[(size_t) i]->id == instanceId)
                return i;
        return -1;
    };

    // MIDI routing is re-derived from the cables below
    for (auto& inst : instances)
    {
        inst->midiSourceIds.clear();
        inst->midiSourceReplaces = false;
    }

    for (const auto& c : cables)
    {
        const int from = nodeIndexOf (c.fromModule);
        const int to = nodeIndexOf (c.toModule);
        if (from < 0 || to < 0)
            continue;

        const auto& fromDesc = instances[(size_t) from]->descriptor();
        const auto& toDesc = instances[(size_t) to]->descriptor();
        const int fromSock = fromDesc.outputIndexOf (c.fromSocket);
        const int toSock = toDesc.inputIndexOf (c.toSocket);
        if (fromSock < 0 || toSock < 0)
            continue;

        // MIDI cables are not signal flow: they only tell a generator which
        // Midi Add to listen to, so they never become audio/mod graph edges
        if (fromDesc.outputs()[(size_t) fromSock]->kind == SocketKind::Midi
            || toDesc.inputs()[(size_t) toSock]->kind == SocketKind::Midi)
        {
            auto& dstInst = *instances[(size_t) to];
            if (! dstInst.listensTo (c.fromModule))
                dstInst.midiSourceIds.push_back (c.fromModule);

            // one replacing source (Arp, Always Midi, Discard Midi) is enough
            // to take the played keys away from this generator
            if (instances[(size_t) from]->dsp->midiSourceReplacesInput())
                dstInst.midiSourceReplaces = true;
            continue;
        }

        RawEdge e;
        e.from = from;  e.fromSock = fromSock;
        e.to = to;      e.toSock = toSock;
        e.fromKind = fromDesc.outputs()[(size_t) fromSock]->kind;
        e.toKind = toDesc.inputs()[(size_t) toSock]->kind;

        adjacency[(size_t) from].push_back ((int) rawEdges.size());
        rawEdges.push_back (e);
    }

    // parameter-modulation cables join the same dependency graph so sources
    // are always computed before the knobs they drive (cycles -> feedback)
    for (const auto& pc : paramCables)
    {
        const int from = nodeIndexOf (pc.fromModule);
        const int to = nodeIndexOf (pc.toModule);
        if (from < 0 || to < 0)
            continue;

        const auto& fromDesc = instances[(size_t) from]->descriptor();
        const auto& toDesc = instances[(size_t) to]->descriptor();
        const int fromSock = fromDesc.outputIndexOf (pc.fromSocket);
        if (fromSock < 0)
            continue;

        int paramIdx = -1;
        float span = 0.0f;
        for (size_t i = 0; i < toDesc.params.size(); ++i)
            if (toDesc.params[i].id == pc.paramId)
            {
                paramIdx = (int) i;
                span = toDesc.params[i].maxValue - toDesc.params[i].minValue;
                break;
            }
        if (paramIdx < 0)
            continue;

        RawEdge e;
        e.from = from;  e.fromSock = fromSock;
        e.to = to;      e.toSock = 0;
        e.fromKind = fromDesc.outputs()[(size_t) fromSock]->kind;
        e.toKind = SocketKind::Modulation;
        e.isParam = true;
        e.paramIdx = paramIdx;
        e.scale = pc.depth * span;
        e.absolute = instances[(size_t) from]->dsp->drivesKnobsAbsolutely();
        e.depth = pc.depth;
        e.minValue = toDesc.params[(size_t) paramIdx].minValue;
        e.maxValue = toDesc.params[(size_t) paramIdx].maxValue;
        {
            const bool unipolarSource = fromDesc.outputs()[(size_t) fromSock]->unipolar;
            if (pc.bipolar && unipolarSource)        { e.polMul = 2.0f; e.polAdd = -1.0f; }
            else if (! pc.bipolar && ! unipolarSource) { e.polMul = 0.5f; e.polAdd = 0.5f; }
        }

        adjacency[(size_t) from].push_back ((int) rawEdges.size());
        rawEdges.push_back (e);
    }

    // AudioReroute: every Reroute Send feeds the Reroute Returns of its bus
    // through their hidden bus sockets, as if a cable ran between them - so a
    // Return is worked out after its Send, and one that feeds back into its
    // own Send becomes a feedback edge (one sample late) like any loop
    for (int s = 0; s < n; ++s)
    {
        auto& send = *instances[(size_t) s];
        if (! send.dsp->isRerouteSend() || send.dsp->rerouteBus() < 0)
            continue;
        const int fromSock = send.descriptor().outputIndexOf ("busOut");
        if (fromSock < 0)
            continue;
        for (int r = 0; r < n; ++r)
        {
            auto& ret = *instances[(size_t) r];
            if (r == s || ret.dsp->isRerouteSend() || ret.dsp->rerouteBus() != send.dsp->rerouteBus())
                continue;
            const int toSock = ret.descriptor().inputIndexOf ("busIn");
            if (toSock < 0)
                continue;
            RawEdge e;
            e.from = s;  e.fromSock = fromSock;
            e.to = r;    e.toSock = toSock;
            e.fromKind = SocketKind::Audio;
            e.toKind = SocketKind::Audio;
            adjacency[(size_t) s].push_back ((int) rawEdges.size());
            rawEdges.push_back (e);
        }
    }

    // iterative DFS: reverse postorder = topological order for forward edges;
    // any edge into a "gray" (on-stack) node closes a cycle -> feedback edge
    enum : char { White = 0, Gray, Black };
    std::vector<char> colour ((size_t) n, White);
    std::vector<int> postorder;
    postorder.reserve ((size_t) n);

    for (int start = 0; start < n; ++start)
    {
        if (colour[(size_t) start] != White)
            continue;

        std::vector<std::pair<int, size_t>> stack;   // (node, next child index)
        stack.emplace_back (start, 0);
        colour[(size_t) start] = Gray;

        while (! stack.empty())
        {
            auto& [node, child] = stack.back();
            if (child < adjacency[(size_t) node].size())
            {
                const int edgeIdx = adjacency[(size_t) node][child++];
                const int target = rawEdges[(size_t) edgeIdx].to;

                if (colour[(size_t) target] == White)
                {
                    colour[(size_t) target] = Gray;
                    stack.emplace_back (target, 0);
                }
                else if (colour[(size_t) target] == Gray)
                {
                    rawEdges[(size_t) edgeIdx].feedback = true;   // back edge
                }
                // Black: forward/cross edge - fine
            }
            else
            {
                colour[(size_t) node] = Black;
                postorder.push_back (node);
                stack.pop_back();
            }
        }
    }

    // topological order = reverse postorder
    std::vector<int> orderPosition ((size_t) n, 0);
    graph->nodes.reserve ((size_t) n);
    for (auto it = postorder.rbegin(); it != postorder.rend(); ++it)
    {
        orderPosition[(size_t) *it] = (int) graph->nodes.size();
        graph->nodes.push_back (instances[(size_t) *it]);
    }

    graph->incoming.assign ((size_t) n, {});
    graph->incomingParams.assign ((size_t) n, {});

    // ---- resolve voice lanes -------------------------------------------------
    // PerVoice modules are always per-voice; Flexible modules become per-voice
    // when ANY source feeding them is per-voice (an osc->filter chain runs one
    // filter per note; a reverb->volume chain stays global). Iterate to a
    // fixpoint since flags only ever flip global -> per-voice.
    for (auto& inst : instances)
        inst->perVoice = (inst->dsp->voiceMode() == VoiceMode::PerVoice);

    //
    // Except where a global AUDIO signal comes in too (a Delay after one
    // synth, met by another synth at a Polyphony Sum): run per voice, that
    // module would get the global audio added to every voice - once per
    // sounding voice, and not at all while none of its own voices sounds (a
    // patch went silent once its other Piano Rolls were emptied). Such a
    // module is where the voices are summed: it stays in the global lane.
    // (Modulation from the global lane - an LFO into a filter's cutoff input -
    // still reaches every voice.)
    auto globalAudioInto = [&] (int to)
    {
        for (const auto& e : rawEdges)
            if (! e.isParam && e.to == to && e.fromKind == SocketKind::Audio && e.toKind == SocketKind::Audio
                && ! instances[(size_t) e.from]->perVoice)
                return true;
        return false;
    };
    for (bool changed = true; changed;)
    {
        changed = false;
        for (const auto& e : rawEdges)
        {
            if (e.isParam)
                continue;   // knob modulation is global; it never changes lanes
            auto& src = instances[(size_t) e.from];
            auto& dst = instances[(size_t) e.to];
            if (src->perVoice && ! dst->perVoice
                && dst->dsp->voiceMode() == VoiceMode::Flexible && ! globalAudioInto (e.to))
            {
                dst->perVoice = true;
                changed = true;
            }
        }
    }

    // a lane flip leaves stale state in the other lane's buffers - full refresh
    bool anyLaneChanged = false;
    for (const auto& inst : instances)
    {
        auto it = lastResolvedLanes.find (inst->id);
        if (it != lastResolvedLanes.end() && it->second != inst->perVoice)
            anyLaneChanged = true;
    }
    lastResolvedLanes.clear();
    for (const auto& inst : instances)
        lastResolvedLanes[inst->id] = inst->perVoice;
    if (anyLaneChanged)
        resetAllPending.store (true, std::memory_order_release);

    // reset connection flags, then set from live edges
    for (auto& inst : instances)
        for (int i = 0; i < 8; ++i)
            inst->dsp->setInputConnected (i, false);

    for (const auto& e : rawEdges)
    {
        if (e.isParam)
        {
            CompiledParamEdge pe;
            pe.srcNode = orderPosition[(size_t) e.from];
            pe.srcSock = e.fromSock;
            pe.paramIdx = e.paramIdx;
            pe.scale = e.scale;
            pe.feedback = e.feedback;
            pe.absolute = e.absolute;
            pe.absoluteSource = e.absolute ? instances[(size_t) e.from]->dsp.get() : nullptr;
            pe.depth = e.depth;
            pe.minValue = e.minValue;
            pe.maxValue = e.maxValue;
            pe.polMul = e.polMul;
            pe.polAdd = e.polAdd;
            graph->incomingParams[(size_t) orderPosition[(size_t) e.to]].push_back (pe);
            continue;
        }

        CompiledEdge ce;
        ce.srcNode = orderPosition[(size_t) e.from];
        ce.srcSock = e.fromSock;
        ce.dstSock = e.toSock;
        ce.feedback = e.feedback;

        if (e.fromKind == SocketKind::Audio && e.toKind == SocketKind::Modulation)
            ce.conversion = EdgeConversion::AudioToMod;
        else if (e.fromKind == SocketKind::Modulation && e.toKind == SocketKind::Audio)
            ce.conversion = EdgeConversion::ModToAudio;
        else if (e.fromKind == SocketKind::Modulation)
            ce.conversion = EdgeConversion::ModToMod;
        else
            ce.conversion = EdgeConversion::Direct;

        if (e.fromKind == SocketKind::Audio && e.toKind == SocketKind::Audio
            && instances[(size_t) e.from]->descriptor().findParam ("outTrim") != nullptr)
            ce.trimSrc = instances[(size_t) e.from]->dsp.get();

        graph->incoming[(size_t) orderPosition[(size_t) e.to]].push_back (ce);
        instances[(size_t) e.to]->dsp->setInputConnected (e.toSock, true);
    }

    // DAW Mod slot assignment. A module keeps the slot it already claimed
    // (saved with the patch), so deleting one DAW Mod never re-points another
    // one's automation lane. Only genuinely unclaimed modules take free slots,
    // in creation order; anything past the twelfth gets none and stays silent.
    {
        bool taken[DawModModule::kMaxSlots] = {};
        std::vector<DawModModule*> unclaimed;

        for (const auto& inst : instances)
        {
            auto* dm = dynamic_cast<DawModModule*> (inst->dsp.get());
            if (dm == nullptr)
                continue;

            const int want = (int) dm->getParameter ("slot");
            if (want >= 0 && want < DawModModule::kMaxSlots && ! taken[want])
            {
                taken[want] = true;
                dm->setSlot (want);
            }
            else
            {
                unclaimed.push_back (dm);   // new, or its slot was taken by a clone
            }
        }

        for (auto* dm : unclaimed)
        {
            int slot = -1;
            for (int i = 0; i < DawModModule::kMaxSlots; ++i)
                if (! taken[i])
                {
                    slot = i;
                    taken[i] = true;
                    break;
                }

            dm->setSlot (slot);
            dm->setParameter ("slot", (float) slot);   // remember the claim
        }
    }

    // ---- MIDI routing, frozen into the graph for the audio thread --------
    // Every module with a Midi Out is a stream. A "note consumer" turns
    // notes into sound: it has a Midi In and no Midi Out (generators, the
    // drums, Noise). A "processor" has both (Midi Add, Discard Midi, Arp) or
    // listens to the keys with no input at all (Keyboard Midi). Streams flow
    // from module to module along the MIDI cables, through any number of
    // processors, and fan out to as many listeners as are patched.
    auto isNoteConsumer = [] (const ModuleInstance& inst)
    {
        bool hasMidiIn = false, hasMidiOut = false;
        for (const auto& sk : inst.descriptor().sockets)
            if (sk.kind == SocketKind::Midi)
                (sk.direction == SocketDirection::Input ? hasMidiIn : hasMidiOut) = true;
        return hasMidiIn && ! hasMidiOut;
    };

    graph->midi.resize (graph->nodes.size());
    for (size_t i = 0; i < graph->nodes.size(); ++i)
    {
        const auto& inst = *graph->nodes[i];
        auto& r = graph->midi[i];
        r.sources = inst.midiSourceIds;

        // anything patched into a Midi In replaces the played keys there
        r.replaces = ! r.sources.empty();

        // Generators (Add Midi In) are always gated. Modules that grew a
        // plain "Midi In" (drums, Noise, Droplets) only once it is patched:
        // unpatched they follow every voice exactly as they always did.
        const auto& desc = inst.descriptor();
        r.gated = desc.inputIndexOf ("addMidiIn") >= 0
               || (desc.inputIndexOf ("midiIn") >= 0 && ! r.sources.empty());
        // a drum in the global lane takes notes as hits: those of the stream
        // patched into its Midi In, or - with that empty - the played keys,
        // for the trigger-driven ones (a note is a trigger, alongside Trig In)
        const bool keysHit = r.sources.empty() && desc.inputIndexOf ("trigIn") >= 0;
        r.hitsInGlobalLane = ! inst.perVoice && inst.dsp->acceptsGlobalMidiNotes()
                          && (! r.sources.empty() || keysHit);
        r.raw = inst.dsp->wantsRawMidi();

        if (r.raw)
        {
            graph->rawNodes.push_back ((int) i);
            if (r.sources.empty() && inst.dsp->playedKeysReachRawMidi (desc))
                graph->keyboardRawNodes.push_back ((int) i);
        }
    }

    // A chain played only by MIDI streams keeps to them. The modules of a
    // per-voice chain that have no Midi In of their own (ADSR, filters, VCA,
    // Polyphony Sum...) follow every voice, so the played keys used to reach
    // a chain driven by an Arp or a Piano Roll too: its envelopes opened for
    // them (heard wherever they move a knob in the global lane) and its
    // Polyphony Sum counted them and pulled the level down. Now, when every
    // module of a per-voice chain that takes notes listens to a stream (none
    // to the keys), the rest of that chain follows those streams alone.
    {
        const int numNodes = (int) graph->nodes.size();
        std::vector<int> parent ((size_t) numNodes);
        for (int k = 0; k < numNodes; ++k) parent[(size_t) k] = k;
        std::function<int (int)> find = [&] (int k) { return parent[(size_t) k] == k ? k : (parent[(size_t) k] = find (parent[(size_t) k])); };
        for (const auto& e : rawEdges)
        {
            const int a = orderPosition[(size_t) e.from], b = orderPosition[(size_t) e.to];
            if (graph->nodes[(size_t) a]->perVoice && graph->nodes[(size_t) b]->perVoice)
                parent[(size_t) find (a)] = find (b);
        }

        auto takesNotes = [] (const ModuleInstance& inst)
        {
            for (const auto& sk : inst.descriptor().sockets)
                if (sk.kind == SocketKind::Midi && sk.direction == SocketDirection::Input)
                    return true;
            return false;
        };

        // per chain: does anything in it take the keys, and which streams
        std::map<int, bool> chainHasKeys;
        std::map<int, std::vector<int>> chainStreams;
        for (int k = 0; k < numNodes; ++k)
        {
            const auto& inst = *graph->nodes[(size_t) k];
            if (! inst.perVoice || ! takesNotes (inst))
                continue;
            const int root = find (k);
            const auto& src = graph->midi[(size_t) k].sources;
            if (src.empty())
                chainHasKeys[root] = true;
            for (int id : src)
                if (std::find (chainStreams[root].begin(), chainStreams[root].end(), id) == chainStreams[root].end())
                    chainStreams[root].push_back (id);
        }
        for (int k = 0; k < numNodes; ++k)
        {
            const auto& inst = *graph->nodes[(size_t) k];
            if (! inst.perVoice || takesNotes (inst))
                continue;
            const int root = find (k);
            if (chainHasKeys[root] || chainStreams[root].empty())
                continue;
            auto& r = graph->midi[(size_t) k];
            r.followsChain = true;
            r.sources = chainStreams[root];
            r.gated = true;     // silent on the voices of the keys and of other streams
        }
    }

    // one stream per module with a Midi Out
    for (const auto& inst : graph->nodes)
    {
        bool hasMidiOut = false;
        for (const auto& sk : inst->descriptor().sockets)
            if (sk.kind == SocketKind::Midi && sk.direction == SocketDirection::Output)
                hasMidiOut = true;
        if (hasMidiOut)
            graph->streams.push_back ({ inst->id, false, {} });
    }

    auto streamOf = [&graph] (int id) -> MidiStream*
    {
        for (auto& st : graph->streams)
            if (st.sourceId == id)
                return &st;
        return nullptr;
    };

    for (size_t i = 0; i < graph->nodes.size(); ++i)
    {
        const auto& inst = *graph->nodes[i];
        const bool raw = graph->midi[i].raw;
        const bool consumer = ! raw && isNoteConsumer (inst);
        const bool processor = ! raw && inst.dsp->isMidiProcessor();

        for (int src : graph->midi[i].sources)
            if (auto* st = streamOf (src))
            {
                if (raw && src != inst.id)
                    st->rawListeners.push_back ((int) i);
                else if (consumer)
                    st->hasConsumers = true;
                else if (processor && src != inst.id)
                    st->processors.push_back ((int) i);
            }

        if (processor && graph->midi[i].sources.empty())
            graph->keyboardProcessors.push_back ((int) i);
    }

    // MIDI cables may be patched in a loop (Midi Add A -> B -> A); a note
    // would then circle forever. The loop is broken at its closing cable,
    // found by a depth-first walk over the processor links.
    {
        std::map<int, int> state;   // instance id -> 0 unvisited, 1 on stack, 2 done
        std::function<void (int)> visit = [&] (int id)
        {
            state[id] = 1;
            if (auto* st = streamOf (id))
            {
                auto& procs = st->processors;
                for (size_t k = 0; k < procs.size();)
                {
                    const int target = graph->nodes[(size_t) procs[k]]->id;
                    if (state[target] == 1)
                    {
                        procs.erase (procs.begin() + (long) k);   // closes a loop: dropped
                        continue;
                    }
                    if (state[target] == 0)
                        visit (target);
                    ++k;
                }
            }
            state[id] = 2;
        };
        // walk from where notes are born first (drivers, keyboard-fed
        // processors - anything with an empty Midi In), so the cable that is
        // dropped is the one closing the loop, not one a note needs on its way
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < graph->nodes.size(); ++i)
            {
                const int id = graph->nodes[i]->id;
                const bool root = graph->midi[i].sources.empty();
                if ((pass == 0) == root && streamOf (id) != nullptr && state[id] == 0)
                    visit (id);
            }
    }

    // does a stream end up anywhere audible, directly or down a chain?
    std::map<int, int> reachMemo;   // 0 unknown, 1 yes, 2 no
    std::function<bool (int)> reaches = [&] (int id) -> bool
    {
        if (reachMemo[id] != 0)
            return reachMemo[id] == 1;
        reachMemo[id] = 2;   // provisional, guards any leftover cycle
        bool result = false;
        if (const auto* st = streamOf (id))
        {
            result = st->hasConsumers || ! st->rawListeners.empty();
            for (int p : st->processors)
                result = reaches (graph->nodes[(size_t) p]->id) || result;
        }
        reachMemo[id] = result ? 1 : 2;
        return result;
    };

    // MIDI note drivers (Arp, Always Midi, Piano Roll) get ticked every
    // sample when their notes reach something that sounds - or always, for
    // the ones whose clock should run regardless (a Piano Roll's playhead)
    for (const auto& inst : graph->nodes)
        if (inst->dsp->isMidiNoteDriver() && (reaches (inst->id) || inst->dsp->runsWithoutListeners()))
            graph->midiDrivers.push_back ({ inst->id, inst->dsp.get() });

    for (const auto& inst : graph->nodes)
        if (inst->perVoice && inst->dsp->canHoldVoicesOpen())
            graph->voiceHolders.push_back (inst->dsp.get());

    graph->feedbackSource.assign (graph->nodes.size(), 0);
    graph->hostIn.assign (graph->nodes.size(), 0);
    graph->hostOut.assign (graph->nodes.size(), 0);
    for (size_t ni = 0; ni < graph->nodes.size(); ++ni)
    {
        for (const auto& e : graph->incoming[ni])
            if (e.feedback)
                graph->feedbackSource[(size_t) e.srcNode] = 1;
        for (const auto& pe : graph->incomingParams[ni])
            if (pe.feedback)
                graph->feedbackSource[(size_t) pe.srcNode] = 1;
        graph->hostIn[ni] = graph->nodes[ni]->dsp->wantsHostInput() ? 1 : 0;
        graph->hostOut[ni] = graph->nodes[ni]->dsp->providesHostOutput() ? 1 : 0;
    }

    // publish: keep the old graph alive in the graveyard until the audio
    // thread has adopted the new one, so nothing is freed under its feet
    std::shared_ptr<const ProcessGraph> old;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        old = renderGraph;
        renderGraph = graph;
    }
    if (old != nullptr)
        graveyard.emplace_back (graph->serial, std::const_pointer_cast<ProcessGraph> (old));
}

void AquanodeModularAudioProcessor::purgeRetired()
{
    const auto adopted = audioAdoptedSerial.load (std::memory_order_acquire);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
        [adopted] (const auto& entry) { return adopted >= entry.first; }),
        graveyard.end());
}
