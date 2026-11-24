// The processor, part 2: processBlock - the whole patch, sample by sample.
#include "PluginProcessor.h"
#include "../Editor/PluginEditor.h"
#include "../Modules/Core/ModuleRegistry.h"
#include "../Modules/Utilities/MIDI/DiscardMidiModule.h"
#include "../Modules/Utilities/Control/DawModModule.h"
#include "../Modules/Utilities/MIDI/PianoRollModule.h"
#include "PatchStateFiles.h"
#include "../Plugins/Host/HostedPlugin.h"
#include "../Platform/AndroidPerfHint.h"

#include <algorithm>

using namespace aquanode;

//==============================================================================
void AquanodeModularAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // "Don't slow down" (Android): the app's own patch reports each block's
    // work against its length, so the phone keeps the clock up (AndroidPerfHint.h)
    struct BlockTimer
    {
        bool on { false };
        long long blockNs { 0 };
        std::chrono::steady_clock::time_point t0;
        ~BlockTimer()
        {
            if (on)
                aquanode::perfhint::reportBlock ((long long) std::chrono::duration_cast<std::chrono::nanoseconds> (
                                                     std::chrono::steady_clock::now() - t0).count(), blockNs);
        }
    } blockTimer;
   #if JUCE_ANDROID
    if (wrapperType == wrapperType_Standalone && aquanode::perfhint::switchedOn().load (std::memory_order_relaxed)
        && currentSampleRate > 0.0)
    {
        blockTimer.on = true;
        blockTimer.blockNs = (long long) (1.0e9 * buffer.getNumSamples() / currentSampleRate);
        blockTimer.t0 = std::chrono::steady_clock::now();
    }
   #endif

    std::shared_ptr<const ProcessGraph> graph;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        graph = renderGraph;
    }

    if (graph != nullptr)
    {
        audioAdoptedSerial.store (graph->serial, std::memory_order_release);

        // a fresh graph may have dropped param cables -> wipe stale offsets
        if (graph->serial != audioLastGraphSerial)
        {
            audioLastGraphSerial = graph->serial;
            for (const auto& node : graph->nodes)
            {
                node->dsp->clearParamMods();
                // only feedback sources keep their previous sample from here on:
                // bring every node's copy up to date once, so a new feedback
                // cable reads the last sample as before
                std::copy (node->outBuf.begin(), node->outBuf.end(), node->prevOutBuf.begin());
                if (node->perVoice)
                    for (int k = 0; k < numActiveVoices; ++k)
                    {
                        const size_t at = (size_t) (activeVoiceList[k] * node->numOut);
                        std::copy (node->outV.begin() + (std::ptrdiff_t) at,
                                   node->outV.begin() + (std::ptrdiff_t) (at + (size_t) node->numOut),
                                   node->prevOutV.begin() + (std::ptrdiff_t) at);
                    }
            }
        }
    }

    const int numSamples = buffer.getNumSamples();
    const auto blockStartTime = std::chrono::steady_clock::now();   // (the CPU / RAM module's global reading)
    const bool metering = aquanode::cpuMeterActive();

    // copy the host input before clearing (in and out may share the buffer)
    if (inputScratch.getNumSamples() < numSamples)
        inputScratch.setSize (2, numSamples, false, false, true);

    const int numHostIns = getTotalNumInputChannels();
    for (int ch = 0; ch < 2; ++ch)
    {
        if (ch < numHostIns)
            inputScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        else if (numHostIns == 1)
            inputScratch.copyFrom (ch, 0, buffer, 0, 0, numSamples);
        else
            inputScratch.clear (ch, 0, numSamples);
    }

    buffer.clear();

    if (graph == nullptr || graph->nodes.empty())
        return;

    // host tempo (120 BPM fallback). The standalone app and Android have no
    // play head at all, so HostTransport::hasBpm tells modules that offer a
    // "lock to host" whether there is anything to lock to.
    double bpm = patchBpm.load();      // the BPM box
    HostTransport hostTransport;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 0.0)
            {
                bpm = *hostBpm;
                hostTransport.hasBpm = true;
                hostTransport.bpm = *hostBpm;
            }
            hostTransport.isPlaying = pos->getIsPlaying();
        }
    currentBpm.store (bpm, std::memory_order_relaxed);
    hostGivesBpm.store (hostTransport.hasBpm, std::memory_order_relaxed);
    // MTS-ESP: a master may have come or gone since the last block
    aquanode::tuning::refresh();

    // Play All / Stop All: the count first (it is raised after the request
    // was stored), so every module sees the same command this block
    hostTransport.rollCommand = rollCommandSerial.load (std::memory_order_acquire);
    hostTransport.rollCommandPlays = rollCommandPlay.load (std::memory_order_relaxed);

    const auto& nodes = graph->nodes;

    if (resetAllPending.exchange (false, std::memory_order_acquire))
    {
        for (auto& n : nodes)
        {
            n->dsp->reset();
            n->dsp->resetGlobalMidiHits();
            n->dsp->midiAllNotesOff();
            std::fill (std::begin (n->voiceHeard), std::end (n->voiceHeard), false);
            std::fill (n->outBuf.begin(), n->outBuf.end(), StereoFrame { 0.0f, 0.0f });
            std::fill (n->prevOutBuf.begin(), n->prevOutBuf.end(), StereoFrame { 0.0f, 0.0f });
            std::fill (n->outV.begin(), n->outV.end(), StereoFrame { 0.0f, 0.0f });
            std::fill (n->prevOutV.begin(), n->prevOutV.end(), StereoFrame { 0.0f, 0.0f });
        }
        for (auto& vs : voiceStates)
            vs = VoiceState();
        refreshActiveVoices();
    }

    // one read of the host's automation values per block; every module gets a
    // pointer to this snapshot so the values are stable across the block
    for (size_t i = 0; i < dawModParams.size(); ++i)
        dawModSnapshot[i] = dawModParams[i] != nullptr ? dawModParams[i]->get() : 0.0f;

    for (size_t ni = 0; ni < nodes.size(); ++ni)
    {
        auto& n = nodes[ni];
        n->dsp->setHostPlayHead (getPlayHead());
        // tempo per lane (6.9.10): a patched Tempo In wins over the patch's BPM
        // (its own Tempo In, or the lane it is in - see rebuildGraph)
        double laneBpm = bpm;
        bool fromLane = false;
        if (ni < graph->tempoSource.size() && graph->tempoSource[ni] >= 0)
        {
            const size_t ti = (size_t) graph->tempoSource[ni];
            const auto& src = *nodes[ti];
            const int sock = graph->tempoOutSock[ti];
            if (! src.perVoice && sock >= 0 && sock < (int) src.outBuf.size())
            {
                // (its value this block; before the first sample the knob itself)
                double v = (double) src.outBuf[(size_t) sock][0] * aquanode::kTempoSignalScale;
                if (v < 19.5) v = (double) src.dsp->getParameter ("bpm");
                if (v >= 19.5 && v <= 1000.5)
                {
                    laneBpm = juce::jlimit (20.0, 999.0, v);
                    fromLane = true;
                }
            }
        }
        n->dsp->setTempoFromLane (fromLane);
        n->dsp->setTempo (laneBpm);
        n->dsp->setHostTransport (hostTransport);
        n->dsp->setMidiControls (&midiState);
        n->dsp->setHostModValues (dawModSnapshot.data());
        // a tempo a module asks for: into the BPM box - or, where a host sets the tempo (a nested
        // Aquanode's is the patch around it), on to that host
        if (const double wanted = n->dsp->takeTempoRequest(); wanted > 0.0
            && ! (ni < graph->tempoOutPatched.size() && graph->tempoOutPatched[ni] != 0))   // (a Tempo feeding lanes: theirs only)
        {
            if (hostTransport.hasBpm) tempoRequest.store (wanted, std::memory_order_relaxed);
            else patchBpm.store (juce::jlimit (20.0, 999.0, wanted));
        }
        n->dsp->blockStart();
    }

    // (6.9.12) how far Slide moves a wavetable in the default MPE map: the first MPE module's knob
    midiState.slideWavetableDepth = 0.0f;   // (6.9.15: 0 without an MPE module)
    midiState.mpeBendRange = 48.0f;
    for (auto& n : nodes) if (const float r = n->dsp->mpeBendRangeSetting(); r >= 0.0f) { midiState.mpeBendRange = r; break; }
    for (auto& n : nodes)
        if (const float d = n->dsp->slideWavetableSetting(); d >= 0.0f)
        {
            midiState.slideWavetableDepth = d;
            break;
        }

    // the pitch-bend range comes from the first Midi Controls module, if any
    midiState.bendRangeSemis = 2.0f;
    for (auto& n : nodes)
        if (const float r = n->dsp->bendRangeSetting(); r >= 0.0f)
        {
            midiState.bendRangeSemis = r;
            break;
        }

    // the patch's own tuning (a Microtuning module in Global mode, 6.9.10):
    // every pitch this block goes through it (a nested patch keeps the one
    // around it unless it has its own), and whether the keys are Linear steps
    const aquanode::tuning::Table* patchTuning = nullptr;
    bool linearKeys = false;
    for (auto& n : nodes)
    {
        if (patchTuning == nullptr)
            patchTuning = n->dsp->globalTuningTable();
        linearKeys = linearKeys || n->dsp->usesLinearKeys();
    }
    const aquanode::tuning::Scope tuningScope (patchTuning);
    for (auto& n : nodes)
        n->dsp->setLinearKeysInPatch (linearKeys);

    // a played key a Discard MIDI in Global mode leaves out: no one hears it
    auto keyDiscarded = [&nodes] (int note)
    {
        for (auto& n : nodes)
            if (n->dsp->discardsPlayedKey (note))
                return true;
        return false;
    };

    // the editor's own MIDI (the on-screen keys with MPE on) joins at the start
    {
        const juce::SpinLock::ScopedTryLockType sl (uiMidiLock);
        screenMidi.clear();
        if (sl.isLocked() && ! uiMidi.isEmpty())
        {
            midiMessages.addEvents (uiMidi, 0, -1, 0);
            for (const auto meta : uiMidi)
                if (! meta.getMessage().isNoteOnOrOff() && screenMidi.size() < screenMidi.capacity())
                    screenMidi.push_back (meta.getMessage());
            uiMidi.clear();
        }
    }

    // Fold in any notes played on the on-screen keyboard (and mirror the
    // incoming stream back onto it for visual feedback). Must run before the
    // engine walks the buffer below so tapped notes are seen this block.
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);
    const bool mpe = mpeEnabled.load (std::memory_order_relaxed);
    midiState.mpeSwitch = mpe;

    auto midiIt = midiMessages.begin();
    const auto midiEnd = midiMessages.end();

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto* hostInL = inputScratch.getReadPointer (0);
    const auto* hostInR = inputScratch.getReadPointer (1);

    const double invSr = 1.0 / currentSampleRate;
    MidiDriverEvent driverEvents[kMaxDriverEventsPerSample];

    // reused per-contribution accumulator (all edge type conversions)
    auto accumulate = [] (StereoFrame& dst, const StereoFrame& frame, EdgeConversion conv)
    {
        switch (conv)
        {
            case EdgeConversion::AudioToMod:
                dst[0] += (frame[0] + frame[1]) * 0.5f;   // stereo -> mono
                break;
            case EdgeConversion::ModToAudio:
                dst[0] += frame[0];                        // mono -> both channels
                dst[1] += frame[0];
                break;
            case EdgeConversion::ModToMod:
                dst[0] += frame[0];
                break;
            case EdgeConversion::Direct:
                dst[0] += frame[0];
                dst[1] += frame[1];
                break;
        }
    };
    auto accumulateEdge = [&accumulate] (StereoFrame& dst, const StereoFrame& frame, const CompiledEdge& e)
    {
        if (e.trimSrc != nullptr)
        {
            const float g = e.trimSrc->outputTrim();
            dst[0] += frame[0] * g;
            dst[1] += frame[1] * g;
        }
        else
            accumulate (dst, frame, e.conversion);
    };

    // Soft Clip on the whole output: the highest of the Audio Outs' (0 = off)
    float softClip = 0.0f;
    for (size_t ni = 0; ni < graph->nodes.size(); ++ni)
        if (graph->hostOut[ni])
            softClip = juce::jmax (softClip, graph->nodes[ni]->dsp->hostSoftClip());

    // Sleep on silence: a sleeping module wakes when one of its knobs is
    // moved, it gets a new sample or its switch is turned off. Once a block.
    for (const auto& np : graph->nodes)
    {
        auto* d = np->dsp.get();
        if (! d->hasSleepSwitch())
            continue;
        np->sleepInNeed = (int) (currentSampleRate * juce::jlimit (1.0, 30.0, d->silenceMemorySeconds() + 1.0))
                        + juce::jmax (0, d->latencySamples());
        const auto& sl = d->sleepState;
        if (sl.asleep && (! d->sleepSwitchedOn()
                          || d->getSampleChangeCounter() != sl.sampleCounter
                          || std::abs (d->baseParamSum() - sl.paramSum) > 1.0e-9))
            d->wakeFromSleep();
    }
    const int sleepOutNeed = (int) currentSampleRate;   // a second of silence coming out

    // the patch's delay as it is right now (see patchLatency)
    {
        if (latencyScratch.size() < graph->nodes.size())
            latencyScratch.resize (graph->nodes.size());   // (reserved in prepareToPlay; a bigger patch grows it once)
        if (latencyInScratch.size() < graph->nodes.size())
            latencyInScratch.resize (graph->nodes.size());
        int longest = 0;
        for (size_t ni = 0; ni < graph->nodes.size(); ++ni)
        {
            int in = 0;
            for (const auto& e : graph->incoming[ni])
                if (! e.feedback)
                    in = juce::jmax (in, latencyScratch[(size_t) e.srcNode]);
            latencyInScratch[ni] = in;
            latencyScratch[ni] = in + juce::jmax (0, graph->nodes[ni]->dsp->latencySamples());
            if (graph->hostOut[ni])
                longest = juce::jmax (longest, latencyScratch[ni]);
        }

        // latency compensation (6.7.12): each delay line waits for the latest
        // input of its module (or, for an Audio Out, for the latest Audio Out)
        for (auto& d : graph->delays)
        {
            const int want = d.dstNode >= 0 ? latencyInScratch[(size_t) d.dstNode] - latencyScratch[(size_t) d.srcNode]
                                            : longest - latencyScratch[(size_t) d.srcNode];
            if (want > d.mask)
                compensationShort.store (true, std::memory_order_relaxed);
            d.delay = juce::jlimit (0, d.mask, want);
        }

        if (softClip > 0.0f)
            longest += AudioOutModule::kLatency;
        patchLatency.store (longest, std::memory_order_relaxed);
    }

    // a delay line: this sample in, the one from its delay ago out
    auto delayed = [&graph] (int index, aquanode::StereoFrame x) noexcept
    {
        auto& d = graph->delays[(size_t) index];
        d.buf[(size_t) d.write] = x;
        const auto y = d.buf[(size_t) ((d.write - d.delay) & d.mask)];
        d.write = (d.write + 1) & d.mask;
        return y;
    };

    // a knob cable's value onto its knob
    auto applyKnob = [] (ModuleInstance& node, const CompiledParamEdge& pe, float value)
    {
        if (pe.absolute)
        {
            if (pe.absoluteSource != nullptr && ! pe.absoluteSource->drivesKnobsNow())
                return;   // (MIDI Learn before its control has moved: the knob as it is)

            // 0..1 -> the knob's whole range (inverted for a negative depth),
            // blended with the knob's own setting by |depth|: 100% = the curve alone
            const float v = juce::jlimit (0.0f, 1.0f, value);
            const float span = pe.maxValue - pe.minValue;
            const float target = pe.depth >= 0.0f ? pe.minValue + v * span
                                                  : pe.maxValue - v * span;
            const float base = node.dsp->getParameterBase (pe.paramIdx);
            node.dsp->addParamMod (pe.paramIdx, std::abs (pe.depth) * (target - base));
        }
        else
        {
            node.dsp->addParamMod (pe.paramIdx, (value * pe.polMul + pe.polAdd) * pe.scale);
        }
    };

    for (int s = 0; s < numSamples; ++s)
    {
        // sample-accurate MIDI -> global voice events (omni)
        while (midiIt != midiEnd && (*midiIt).samplePosition <= s)
        {
            const auto rawMsg = (*midiIt).getMessage();
            auto msg = rawMsg;
            // (6.9.15) the MPE module's Bend Range: a member channel's bend scaled from MPE's 48
            // semitones, for everything that hears it (the voices, the plugins, MIDI Add)
            if (mpe && msg.isPitchWheel() && msg.getChannel() >= 2 && midiState.mpeBendRange < 47.999f)
                msg = juce::MidiMessage::pitchWheel (msg.getChannel(), juce::jlimit (0, 16383,
                          8192 + (int) std::lround ((msg.getPitchWheelValue() - 8192) * (double) midiState.mpeBendRange / 48.0)));

            // one of the on-screen keys' own (pressure, bend, slide): played, not learned
            screenMidiNow = false;
            if (! screenMidi.empty() && ! msg.isNoteOnOrOff())
                for (auto it = screenMidi.begin(); it != screenMidi.end(); ++it)
                    if (it->getRawDataSize() == rawMsg.getRawDataSize()
                        && std::memcmp (it->getRawData(), rawMsg.getRawData(), (size_t) rawMsg.getRawDataSize()) == 0)
                    {
                        screenMidi.erase (it);
                        screenMidiNow = true;
                        break;
                    }

            // hosted plugins hear the host MIDI as it is: notes when their
            // Midi In is empty, everything else always
            const bool keyLeftOut = msg.isNoteOn()
                                    && (aquanode::tuning::shouldFilterNote (msg.getNoteNumber()) || keyDiscarded (msg.getNoteNumber()));
            if (! graph->rawNodes.empty() && ! keyLeftOut)
            {
                const bool isNote = msg.isNoteOnOrOff();
                // (6.9.12) with MPE, a finger's own bend, pressure and Slide (on its
                // member channel) belong to its note: only what plays the keys hears
                // them - a plugin fed by MIDI Add has its own notes on those channels,
                // and the finger's bend would detune whichever of them sat there
                const bool fingerExpression = mpe && msg.getChannel() >= 2
                    && (msg.isPitchWheel() || msg.isChannelPressure() || msg.isAftertouch()
                        || (msg.isController() && msg.getControllerNumber() == 74));
                for (int ni : ((isNote || fingerExpression) ? graph->keyboardRawNodes : graph->rawNodes))
                    nodes[(size_t) ni]->dsp->deliverRawMidi (msg);
            }
            if (keyLeftOut)
                {}   // a key the tuning's keyboard map (or a Global Discard MIDI) leaves out
            else if (mpe && msg.getChannel() >= 2 && ! msg.isNoteOff() && ! msg.isAllNotesOff() && ! msg.isAllSoundOff())
                mpeMessage (*graph, msg);   // a member channel: its note's own expression
            else if (msg.isNoteOn())
                keyboardEvent (*graph, { msg.getNoteNumber(), msg.getFloatVelocity(), true });
            else if (msg.isNoteOff())
            {
                if (mpe && msg.getChannel() >= 2 && mpeChanNote[msg.getChannel()] == msg.getNoteNumber())
                    mpeChanNote[msg.getChannel()] = -1;   // the finger let go
                keyboardEvent (*graph, { msg.getNoteNumber(), 0.0f, false });
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                std::fill (std::begin (sustainedKeys), std::end (sustainedKeys), false);
                engineAllNotesOff (*graph);
            }
            else
                handleControlMessage (*graph, msg);
            screenMidiNow = false;
            ++midiIt;
        }

        // note drivers run on their own clock, emitting their own notes
        // (several per sample for a Piano Roll chord, each with a velocity),
        // which travel down whatever their Midi Out is patched into
        for (const auto& md : graph->midiDrivers)
        {
            const int numEvents = md.dsp->midiDriverAdvanceEvents (currentSampleRate, driverEvents,
                                                                   kMaxDriverEventsPerSample);
            for (int ev = 0; ev < numEvents; ++ev)
                emitMidi (*graph, md.instanceId, driverEvents[ev], 0);
        }

        // released voices die once their patch-wide tail has elapsed
        for (int v = 0; v < kMaxVoices; ++v)
        {
            if (voiceStates[v].active && ! voiceStates[v].held)
            {
                bool heldOpen = false;
                for (const auto* h : graph->voiceHolders)
                    if (h->voiceHeldOpen (v)) { heldOpen = true; break; }
                if (heldOpen)
                    continue;
                voiceStates[v].countdown -= invSr;
                if (voiceStates[v].countdown <= 0.0)
                    deactivateVoice (nodes, v);
            }
        }

        float sumL = 0.0f, sumR = 0.0f;

        // CPU / RAM module: the module whose turn it is, timed on this sample
        const int timedNode = metering && meterNode < (int) nodes.size() ? meterNode : -1;

        for (size_t ni = 0; ni < nodes.size(); ++ni)
        {
            auto& node = *nodes[ni];
            struct NodeClock
            {
                aquanode::SynthModule* dsp { nullptr };
                std::chrono::steady_clock::time_point t0;
                ~NodeClock()
                {
                    if (dsp != nullptr)
                        dsp->cpuNs += (double) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
                }
            } nodeClock;
            if ((int) ni == timedNode && ! node.dsp->timesItself())
            {
                nodeClock.dsp = node.dsp.get();
                nodeClock.t0 = std::chrono::steady_clock::now();
            }

            // ---- knob modulation: sum every param cable into its offset.
            // A module running in the global lane gets per-voice sources
            // voice-summed, like any global consumer. A module running per
            // voice gets each voice's OWN value of a per-voice source (an
            // envelope on a cutoff knob moves each note's filter by that
            // note's envelope) - that part is applied in the voice loop below.
            const auto& pedges = graph->incomingParams[ni];
            auto applyParamEdge = [&node, &applyKnob] (const CompiledParamEdge& pe, float value) { applyKnob (node, pe, value); };

            auto globalEdgeValue = [&nodes, this] (const CompiledParamEdge& pe)
            {
                auto& psrc = *nodes[(size_t) pe.srcNode];
                float value = 0.0f;
                if (psrc.perVoice)
                {
                    const auto& lanes = pe.feedback ? psrc.prevOutV : psrc.outV;
                    for (int k = 0; k < numActiveVoices; ++k)
                    {
                        const int v = activeVoiceList[k];
                        const auto& f = lanes[(size_t) (v * psrc.numOut + pe.srcSock)];
                        value += 0.5f * (f[0] + f[1]);
                    }
                }
                else
                {
                    const auto& f = (pe.feedback ? psrc.prevOutBuf : psrc.outBuf)[(size_t) pe.srcSock];
                    value = 0.5f * (f[0] + f[1]);
                }
                return value;
            };

            if (! pedges.empty() && ! node.perVoice)
            {
                for (const auto& pe : pedges)
                    node.dsp->setParamMod (pe.paramIdx, 0.0f);
                for (const auto& pe : pedges)
                {
                    const float v = globalEdgeValue (pe);
                    applyParamEdge (pe, pe.delayIndex >= 0 ? delayed (pe.delayIndex, { v, v })[0] : v);
                }
            }

            if (node.perVoice)
            {
                // an input the module listens to outside its voices (a
                // recorder's): voice-summed, every sample, notes or not
                if (const int tap = node.dsp->globalTapInput(); tap >= 0)
                {
                    StereoFrame heard { 0.0f, 0.0f };
                    for (const auto& e : graph->incoming[ni])
                    {
                        if (e.dstSock != tap)
                            continue;
                        auto& src = *nodes[(size_t) e.srcNode];
                        if (src.perVoice)
                        {
                            const auto& lanes = e.feedback ? src.prevOutV : src.outV;
                            for (int k = 0; k < numActiveVoices; ++k)
                                accumulateEdge (heard, lanes[(size_t) (activeVoiceList[k] * src.numOut + e.srcSock)], e);
                        }
                        else
                            accumulateEdge (heard, (e.feedback ? src.prevOutBuf : src.outBuf)[(size_t) e.srcSock], e);
                    }
                    node.dsp->globalTapSample (heard);
                }

                // ---- per-voice lane: run this module once for every active
                // voice, with that voice's own inputs
                const bool gated = graph->midi[ni].gated;
                const bool tapping = node.dsp->tapOn.load (std::memory_order_relaxed);   // (its card's graph)
                float tapIn = 0.0f, tapOut = 0.0f;

                for (int k = 0; k < numActiveVoices; ++k)
                {
                    const int v = activeVoiceList[k];

                    // a generator that was never given this voice's note
                    // stays silent on it, whatever its envelope is doing
                    if (gated && ! node.voiceHeard[v])
                    {
                        for (int i = 0; i < node.numOut; ++i)
                            node.outV[(size_t) (v * node.numOut + i)] = { 0.0f, 0.0f };
                        continue;
                    }

                    // this voice's knob modulation
                    if (! pedges.empty())
                    {
                        for (const auto& pe : pedges)
                            node.dsp->setParamMod (pe.paramIdx, 0.0f);
                        for (const auto& pe : pedges)
                        {
                            auto& psrc = *nodes[(size_t) pe.srcNode];
                            if (psrc.perVoice)
                            {
                                const auto& f = (pe.feedback ? psrc.prevOutV : psrc.outV)
                                                    [(size_t) (v * psrc.numOut + pe.srcSock)];
                                applyParamEdge (pe, 0.5f * (f[0] + f[1]));
                            }
                            else
                            {
                                applyParamEdge (pe, globalEdgeValue (pe));
                            }
                        }
                    }

                    for (int i = 0; i < node.numIn; ++i)
                        node.inBuf[(size_t) i] = { 0.0f, 0.0f };

                    for (const auto& e : graph->incoming[ni])
                    {
                        auto& src = *nodes[(size_t) e.srcNode];
                        const StereoFrame* frame;
                        if (src.perVoice)
                            frame = &(e.feedback ? src.prevOutV : src.outV)
                                        [(size_t) (v * src.numOut + e.srcSock)];
                        else   // global source broadcasts to every voice
                            frame = &(e.feedback ? src.prevOutBuf : src.outBuf)[(size_t) e.srcSock];

                        accumulateEdge (node.inBuf[(size_t) e.dstSock], *frame, e);
                    }

                    midiState.currentVoice = v;   // its own MPE bend
                    node.dsp->processVoiceSample (v, node.inBuf.data(),
                                                  node.outV.data() + (size_t) (v * node.numOut));
                    if (tapping)
                    {
                        if (node.numIn > 0) tapIn += 0.5f * (node.inBuf[0][0] + node.inBuf[0][1]);
                        if (node.numOut > 0)
                        {
                            const auto& o = node.outV[(size_t) (v * node.numOut)];
                            tapOut += 0.5f * (o[0] + o[1]);
                        }
                    }
                }
                midiState.currentVoice = -1;
                if (tapping)
                    node.dsp->tapWrite (tapIn, tapOut);
            }
            else
            {
                // ---- global lane: per-voice sources are summed here - this
                // is THE polyphonic voice sum (at Audio Out, effects, etc.)
                if (graph->hostIn[ni])
                    node.dsp->setHostInput (hostInL[s], hostInR[s]);

                for (int i = 0; i < node.numIn; ++i)
                    node.inBuf[(size_t) i] = { 0.0f, 0.0f };

                // voices reaching a module that counts them: those whose
                // signal arrived at its inputs within the last 20 ms
                if (node.dsp->wantsVoiceCount())
                {
                    const int hold = (int) (0.02 * currentSampleRate);
                    for (int v = 0; v < kMaxVoices; ++v)
                        if (node.voiceQuiet[v] < hold) ++node.voiceQuiet[v];
                    for (const auto& e : graph->incoming[ni])
                    {
                        auto& src = *nodes[(size_t) e.srcNode];
                        if (! src.perVoice)
                            continue;
                        const auto& lanes = e.feedback ? src.prevOutV : src.outV;
                        for (int k = 0; k < numActiveVoices; ++k)
                        {
                            const int v = activeVoiceList[k];
                            const auto& f = lanes[(size_t) (v * src.numOut + e.srcSock)];
                            if (std::abs (f[0]) > 1.0e-6f || std::abs (f[1]) > 1.0e-6f)
                                node.voiceQuiet[v] = 0;
                        }
                    }
                    int count = 0;
                    for (int k = 0; k < numActiveVoices; ++k)
                        if (node.voiceQuiet[activeVoiceList[k]] < hold) ++count;
                    node.dsp->setVoiceCount (count);
                }

                for (const auto& e : graph->incoming[ni])
                {
                    auto& src = *nodes[(size_t) e.srcNode];
                    // (a cable that waits for a slower one (6.7.12) is summed on its own, then delayed)
                    StereoFrame waiting { 0.0f, 0.0f };
                    auto& dst = e.delayIndex >= 0 ? waiting : node.inBuf[(size_t) e.dstSock];

                    if (src.perVoice)
                    {
                        const auto& lanes = e.feedback ? src.prevOutV : src.outV;
                        for (int k = 0; k < numActiveVoices; ++k)
                        {
                            const int v = activeVoiceList[k];
                            accumulateEdge (dst, lanes[(size_t) (v * src.numOut + e.srcSock)], e);
                        }
                    }
                    else
                    {
                        accumulateEdge (dst, (e.feedback ? src.prevOutBuf : src.outBuf)[(size_t) e.srcSock], e);
                    }

                    if (e.delayIndex >= 0)
                    {
                        const auto late = delayed (e.delayIndex, waiting);
                        auto& in = node.inBuf[(size_t) e.dstSock];
                        in[0] += late[0];
                        in[1] += late[1];
                    }
                }

                // ---- sleep on silence: once the audio going in has been
                // silent for longer than the module can hold it and nothing has
                // come out for a second, it is not run until sound comes in
                bool sleeping = false;
                if (node.dsp->sleepSwitchedOn())
                {
                    auto& sl = node.dsp->sleepState;
                    bool quietIn = true;
                    for (int i = 0; i < node.numIn && quietIn; ++i)
                        if ((node.dsp->audioInMask >> i) & 1u)
                            quietIn = std::abs (node.inBuf[(size_t) i][0]) < aquanode::kSleepSilence
                                   && std::abs (node.inBuf[(size_t) i][1]) < aquanode::kSleepSilence;
                    sl.quietIn = quietIn ? juce::jmin (sl.quietIn + 1, 1 << 30) : 0;
                    if (sl.asleep)
                    {
                        if (quietIn)
                            sleeping = true;
                        else
                            node.dsp->wakeFromSleep();
                    }
                }
                else if (node.dsp->sleepState.asleep)
                    node.dsp->wakeFromSleep();

                if (sleeping)
                {
                    for (int i = 0; i < node.numOut; ++i)
                        node.outBuf[(size_t) i] = { 0.0f, 0.0f };
                }
                else
                {
                    node.dsp->processSample (node.inBuf.data(), node.outBuf.data());

                    if (node.dsp->sleepSwitchedOn())
                    {
                        auto& sl = node.dsp->sleepState;
                        bool quietOut = true;
                        for (int i = 0; i < node.numOut && quietOut; ++i)
                            if ((node.dsp->audioOutMask >> i) & 1u)
                                quietOut = std::abs (node.outBuf[(size_t) i][0]) < aquanode::kSleepSilence
                                        && std::abs (node.outBuf[(size_t) i][1]) < aquanode::kSleepSilence;
                        sl.quietOut = quietOut ? juce::jmin (sl.quietOut + 1, 1 << 30) : 0;
                        if (sl.quietOut >= sleepOutNeed && sl.quietIn >= node.sleepInNeed)
                        {
                            sl.asleep = true;
                            sl.paramSum = node.dsp->baseParamSum();
                            sl.sampleCounter = node.dsp->getSampleChangeCounter();
                            node.dsp->sleepingNow.store (true, std::memory_order_relaxed);
                        }
                    }
                }
                if (node.dsp->tapOn.load (std::memory_order_relaxed))
                    node.dsp->tapWrite (node.numIn > 0 ? 0.5f * (node.inBuf[0][0] + node.inBuf[0][1]) : 0.0f,
                                        node.numOut > 0 ? 0.5f * (node.outBuf[0][0] + node.outBuf[0][1]) : 0.0f);

                if (graph->hostOut[ni])
                {
                    float l = 0.0f, r = 0.0f;
                    node.dsp->getHostOutput (l, r);
                    if (const int di = graph->hostOutDelay.empty() ? -1 : graph->hostOutDelay[ni]; di >= 0)
                    {
                        const auto late = delayed (di, { l, r });   // lined up with the latest Audio Out (6.7.12)
                        l = late[0];
                        r = late[1];
                    }
                    sumL += l;
                    sumR += r;
                }
            }
        }

        // cache this sample's outputs for next sample's feedback edges
        for (size_t ni = 0; ni < nodes.size(); ++ni)
        {
            if (! graph->feedbackSource[ni])
                continue;
            auto* n = nodes[ni].get();
            if (n->perVoice)
            {
                for (int k = 0; k < numActiveVoices; ++k)
                {
                    const int v = activeVoiceList[k];
                    for (int i = 0; i < n->numOut; ++i)
                        n->prevOutV[(size_t) (v * n->numOut + i)] = n->outV[(size_t) (v * n->numOut + i)];
                }
            }
            else
            {
                std::copy (n->outBuf.begin(), n->outBuf.end(), n->prevOutBuf.begin());
            }
        }

        if (softClip > 0.0f)
            outputClipper.process (sumL, sumR, softClip);
        outL[s] = sumL;
        if (outR != nullptr)
            outR[s] = sumR;
    }

    // CPU / RAM module: this block's shares (of real time), and each meter's chain
    {
        const double blockNs = 1.0e9 * (double) numSamples / juce::jmax (1.0, currentSampleRate);
        const double used = (double) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - blockStartTime).count();
        meterGlobalShare += 0.03f * ((float) (used / blockNs) - meterGlobalShare);
        if (metering)
        {
            // hosted plugins: their own exact time, every block
            for (const auto& n : nodes)
            {
                auto& d = *n->dsp;
                if (! d.timesItself()) continue;
                const float now = (float) (d.cpuNs / blockNs);
                d.cpuNs = 0.0;
                const float prev = d.cpuShare.load (std::memory_order_relaxed);
                d.cpuShare.store (prev + 0.03f * (now - prev), std::memory_order_relaxed);
            }
            // the module whose turn it is: its window so far, and on to the next when it is over
            if (meterNode >= (int) nodes.size()) { meterNode = 0; meterWindowLeft = 0; }
            if (meterWindowLen <= 0 || meterWindowLeft <= 0)
            {
                meterWindowLen = meterWindowLeft = juce::jmax (numSamples, (int) (0.1 * currentSampleRate));
                meterWindowNs = 0.0;
            }
            if (meterNode < (int) nodes.size())
            {
                auto& d = *nodes[(size_t) meterNode]->dsp;
                if (d.timesItself()) meterWindowLeft = 0;   // (a hosted plugin: nothing to do, on to the next)
                else { meterWindowNs += d.cpuNs; d.cpuNs = 0.0; meterWindowLeft -= numSamples; }
                if (meterWindowLeft <= 0)
                {
                    if (! d.timesItself())
                    {
                        const double windowNs = 1.0e9 * (double) (meterWindowLen - meterWindowLeft) / juce::jmax (1.0, currentSampleRate);
                        const float now = (float) (meterWindowNs / juce::jmax (1.0, windowNs));
                        const float prev = d.cpuShare.load (std::memory_order_relaxed);
                        d.cpuShare.store (prev <= 0.0f ? now : prev + 0.5f * (now - prev), std::memory_order_relaxed);
                    }
                    meterNode = (meterNode + 1) % (int) nodes.size();
                    meterWindowLeft = 0;
                }
            }
            for (const auto& m : graph->cpuMeters)
            {
                aquanode::SynthModule::CpuReading r;
                r.globalShare = meterGlobalShare;
                r.connected = m.connected;
                for (int ni : m.upstream)
                {
                    const auto& inst = *nodes[(size_t) ni];
                    const float share = inst.dsp->cpuShare.load (std::memory_order_relaxed);
                    r.chainShare += share;
                    r.chainRam += inst.dsp->ramBytes.load (std::memory_order_relaxed);
                    // the three heaviest
                    int at = r.numTop;
                    while (at > 0 && r.topShare[at - 1] < share) --at;
                    if (at < 3)
                    {
                        for (int k = juce::jmin (2, r.numTop); k > at; --k) { r.topShare[k] = r.topShare[k - 1]; r.topName[k] = r.topName[k - 1]; }
                        r.topShare[at] = share;
                        r.topName[at] = inst.reg != nullptr ? &inst.reg->descriptor.displayName : nullptr;
                        r.numTop = juce::jmin (3, r.numTop + 1);
                    }
                }
                r.chainShare = juce::jmin (r.chainShare, juce::jmax (r.globalShare, 0.0f));   // (a chain is never more than the whole)
                m.dsp->setCpuReading (r);
            }
        }
    }

    // Safety net: if anything in the patch produced NaN or infinity, never
    // hand it to the host - a single NaN reaching a DAW channel can silence a
    // whole bus (its reverbs and EQs keep recirculating it). The block is
    // muted and every module's state is wiped, so the patch heals itself on
    // the next block instead of staying broken.
    bool finite = true;
    for (int ch = 0; ch < buffer.getNumChannels() && finite; ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
            {
                finite = false;
                break;
            }
    }
    if (! finite)
    {
        buffer.clear();
        resetAllPending.store (true, std::memory_order_release);
    }
}

const juce::Array<int>& AquanodeModularAudioProcessor::blockSizeChoices()
{
    static const juce::Array<int> c { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    return c;
}

