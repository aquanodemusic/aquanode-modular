// The processor, part 2: processBlock - the whole patch, sample by sample.
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
void AquanodeModularAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

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
    double bpm = 120.0;
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

    for (auto& n : nodes)
    {
        n->dsp->setHostPlayHead (getPlayHead());
        n->dsp->setTempo (bpm);
        n->dsp->setHostTransport (hostTransport);
        n->dsp->setMidiControls (&midiState);
        n->dsp->setHostModValues (dawModSnapshot.data());
        n->dsp->blockStart();
    }

    // the pitch-bend range comes from the first Midi Controls module, if any
    midiState.bendRangeSemis = 2.0f;
    for (auto& n : nodes)
        if (const float r = n->dsp->bendRangeSetting(); r >= 0.0f)
        {
            midiState.bendRangeSemis = r;
            break;
        }

    // the editor's own MIDI (the on-screen keys with MPE on) joins at the start
    {
        const juce::SpinLock::ScopedTryLockType sl (uiMidiLock);
        if (sl.isLocked() && ! uiMidi.isEmpty())
        {
            midiMessages.addEvents (uiMidi, 0, -1, 0);
            uiMidi.clear();
        }
    }

    // Fold in any notes played on the on-screen keyboard (and mirror the
    // incoming stream back onto it for visual feedback). Must run before the
    // engine walks the buffer below so tapped notes are seen this block.
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);
    const bool mpe = mpeEnabled.load (std::memory_order_relaxed);

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

    for (int s = 0; s < numSamples; ++s)
    {
        // sample-accurate MIDI -> global voice events (omni)
        while (midiIt != midiEnd && (*midiIt).samplePosition <= s)
        {
            const auto msg = (*midiIt).getMessage();

            // hosted plugins hear the host MIDI as it is: notes when their
            // Midi In is empty, everything else always
            if (! graph->rawNodes.empty()
                && ! (msg.isNoteOn() && aquanode::tuning::shouldFilterNote (msg.getNoteNumber())))
            {
                const bool isNote = msg.isNoteOnOrOff();
                for (int ni : (isNote ? graph->keyboardRawNodes : graph->rawNodes))
                    nodes[(size_t) ni]->dsp->rawMidiMessage (msg);
            }
            if (msg.isNoteOn() && aquanode::tuning::shouldFilterNote (msg.getNoteNumber()))
                {}   // a key the MTS-ESP master's keyboard map leaves out
            else if (mpe && msg.getChannel() >= 2 && ! msg.isNoteOff() && ! msg.isAllNotesOff() && ! msg.isAllSoundOff())
                mpeMessage (*graph, msg);   // a member channel: its note's own expression
            else if (msg.isNoteOn())
                keyboardEvent (*graph, { msg.getNoteNumber(), msg.getFloatVelocity(), true });
            else if (msg.isNoteOff())
                keyboardEvent (*graph, { msg.getNoteNumber(), 0.0f, false });
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                std::fill (std::begin (sustainedKeys), std::end (sustainedKeys), false);
                engineAllNotesOff (*graph);
            }
            else
                handleControlMessage (*graph, msg);
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

        for (size_t ni = 0; ni < nodes.size(); ++ni)
        {
            auto& node = *nodes[ni];

            // ---- knob modulation: sum every param cable into its offset.
            // A module running in the global lane gets per-voice sources
            // voice-summed, like any global consumer. A module running per
            // voice gets each voice's OWN value of a per-voice source (an
            // envelope on a cutoff knob moves each note's filter by that
            // note's envelope) - that part is applied in the voice loop below.
            const auto& pedges = graph->incomingParams[ni];
            auto applyParamEdge = [&node] (const CompiledParamEdge& pe, float value)
            {
                if (pe.absolute)
                {
                    if (pe.absoluteSource != nullptr && ! pe.absoluteSource->drivesKnobsNow())
                        return;   // (Midi Learn before its control has moved: the knob as it is)

                    // 0..1 -> the knob's whole range (inverted for a
                    // negative depth), blended with the knob's own
                    // setting by |depth|: 100% = the curve alone
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
                    applyParamEdge (pe, globalEdgeValue (pe));
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
                }
                midiState.currentVoice = -1;
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
                    auto& dst = node.inBuf[(size_t) e.dstSock];

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
                }

                node.dsp->processSample (node.inBuf.data(), node.outBuf.data());

                if (graph->hostOut[ni])
                {
                    float l = 0.0f, r = 0.0f;
                    node.dsp->getHostOutput (l, r);
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
