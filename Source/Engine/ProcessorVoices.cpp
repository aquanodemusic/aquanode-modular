// The processor, part 3: the global voice manager and the MIDI the patch hears.
#include "PluginProcessor.h"
#include "../Editor/PluginEditor.h"
#include "../Modules/Core/ModuleRegistry.h"
#include "../Modules/Utilities/MIDI/DiscardMidiModule.h"
#include "../Modules/Utilities/Control/DawModModule.h"
#include "../Modules/Utilities/MIDI/PianoRollModule.h"
#include "PatchStateFiles.h"

#include <algorithm>

using namespace aquanode;

//==============================================================================
// global voice manager (audio thread)
//==============================================================================
void AquanodeModularAudioProcessor::refreshActiveVoices()
{
    numActiveVoices = 0;
    for (int v = 0; v < kMaxVoices; ++v)
        if (voiceStates[v].active)
            activeVoiceList[numActiveVoices++] = v;
}

double AquanodeModularAudioProcessor::computeVoiceTail (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes) const
{
    double tail = 0.02;   // minimum: covers unpatched-env gate ramps
    for (const auto& n : nodes)
        if (n->perVoice)
            tail = juce::jmax (tail, n->dsp->voiceTailSeconds());
    return tail + 0.05;   // small safety margin for filter ringing etc.
}

void AquanodeModularAudioProcessor::deactivateVoice (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int v)
{
    voiceStates[v] = VoiceState();

    for (const auto& n : nodes)
    {
        if (! n->perVoice)
        {
            n->dsp->globalMidiNoteOff (v);   // no-op unless a drum took this voice as a hit
            continue;
        }

        n->voiceHeard[v] = false;
        n->dsp->voiceReset (v);
        for (int i = 0; i < n->numOut; ++i)
        {
            n->outV[(size_t) (v * n->numOut + i)] = { 0.0f, 0.0f };
            n->prevOutV[(size_t) (v * n->numOut + i)] = { 0.0f, 0.0f };
        }
    }

    refreshActiveVoices();
}

void AquanodeModularAudioProcessor::startVoice (const ProcessGraph& graph, int note,
                                                int midiSource, float velocity01)
{
    const auto& nodes = graph.nodes;
    const bool fromStream = midiSource >= 0;

    // A note from a MIDI stream that no sound-making module listens to has
    // nobody to play it. It used to take a voice anyway, and every module
    // without a Midi In (ADSR, LFO, filters...) followed that voice - so an
    // Always Midi into a Midi Add that led nowhere still opened envelopes,
    // and any oscillator on such an envelope droned at a stale pitch with
    // nothing played. Such a note now simply does not start a voice.
    if (fromStream && ! graph.feeds (midiSource))
        return;

    // 1) the same pitch from the same stream is still HELD -> retrigger it.
    //    A note that has been released keeps its voice for its release tail
    //    and the new note gets a fresh voice, the way a polysynth (and a
    //    DX7) does it: re-striking a key during its release starts a clean
    //    note instead of picking up the old one's envelopes mid-release.
    int v = -1;
    bool retrigger = false;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceStates[i].active && voiceStates[i].held && voiceStates[i].note == note
            && voiceStates[i].midiSource == midiSource)
        {
            v = i;
            retrigger = true;
            break;
        }

    // 2) otherwise a free voice
    if (v < 0)
        for (int i = 0; i < kMaxVoices; ++i)
            if (! voiceStates[i].active)
            {
                v = i;
                break;
            }

    // 3) otherwise steal the oldest sounding voice
    if (v < 0)
    {
        v = 0;
        for (int i = 1; i < kMaxVoices; ++i)
            if (voiceStates[i].order < voiceStates[v].order)
                v = i;
        deactivateVoice (nodes, v);   // hard reset the stolen voice everywhere
    }

    // a new note starts with no MPE expression of its own (an MPE note-on sets it after)
    mpeChannelOfVoice[v] = 0;
    midiState.voiceBend[v] = 0.0f;
    midiState.voiceSlide[v] = 0.0f;
    midiState.voiceSlideSet[v] = false;
    midiState.voicePressure[v] = 0.0f;

    voiceStates[v].note = note;
    voiceStates[v].midiSource = midiSource;
    voiceStates[v].active = true;
    voiceStates[v].held = true;
    voiceStates[v].countdown = 0.0;
    voiceStates[v].order = ++voiceOrderCounter;
    refreshActiveVoices();

    // Notes from a stream only reach the modules patched to that stream.
    // The played keys reach every module with nothing in its Midi In.
    // Modules with no Midi In at all (ADSR, LFO, filters...) shadow whichever
    // voice is active, exactly like for a played note - an ADSR has to open
    // for a Piano Roll note too - and gated generators that were not given
    // this voice's note stay silent on it whatever those envelopes do.
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        auto& n = *nodes[i];
        const auto& routing = graph.midi[i];

        if (! n.perVoice)
        {
            // a drum sitting in the global lane: a note from the stream in its
            // Midi In - or a played key, with nothing there - arrives as a hit
            if (routing.hitsInGlobalLane && (fromStream ? routing.listensTo (midiSource) : ! routing.replaces))
                n.dsp->globalMidiNoteOn (v, note, velocity01);
            continue;
        }

        const bool shouldHear = routing.followsChain
            ? (fromStream && routing.listensTo (midiSource))     // keeps to its chain's streams
            : fromStream ? (routing.listensTo (midiSource) || ! routing.gated)
                         : (! routing.replaces);

        n.voiceHeard[v] = shouldHear;

        if (shouldHear)
        {
            n.dsp->voiceNoteOn (v, note, retrigger);
            n.dsp->voiceVelocity (v, velocity01);
        }
    }
}

void AquanodeModularAudioProcessor::releaseVoices (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int midiSource, int note)
{
    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (! voiceStates[v].active || ! voiceStates[v].held
            || voiceStates[v].midiSource != midiSource || voiceStates[v].note != note)
            continue;

        voiceStates[v].held = false;
        voiceStates[v].countdown = computeVoiceTail (nodes);

        for (const auto& n : nodes)
        {
            if (n->perVoice)
                n->dsp->voiceNoteOff (v);
            else
                n->dsp->globalMidiNoteOff (v);
        }
    }
}

// A note event leaving a stream's Midi Out: voices for the sound-making
// modules listening to it, and the event handed to every processor listening
// to it - which may send events on again, and so on down any chain.
void AquanodeModularAudioProcessor::emitMidi (const ProcessGraph& graph, int sourceId,
                                              const MidiDriverEvent& e, int depth)
{
    if (e.note < 0 || e.note > 127)
        return;

    const auto* stream = graph.findStream (sourceId);
    if (stream == nullptr)
        return;

    // an MPE note's expression: onto the voices playing it (its own bend, Pressure, Slide)
    auto expressVoices = [&]
    {
        for (int v = 0; v < kMaxVoices; ++v)
            if (voiceStates[v].active && voiceStates[v].held && voiceStates[v].note == e.note
                && voiceStates[v].midiSource == sourceId)
            {
                midiState.voiceBend[v] = e.bend;
                if (e.pressure >= 0.0f) midiState.voicePressure[v] = e.pressure;
                if (e.slide >= 0.0f) { midiState.voiceSlide[v] = e.slide; midiState.voiceSlideSet[v] = true; }
            }
    };

    if (stream->hasConsumers)
    {
        if (e.isExpression) expressVoices();
        else if (e.isOn)    { startVoice (graph, e.note, sourceId, e.velocity); if (e.mpe) expressVoices(); }
        else                releaseVoices (graph.nodes, sourceId, e.note);
    }

    if (! stream->rawListeners.empty())
    {
        // a note-off whose note-on went out on a member channel goes there too
        // (6.9.10: a processor may hand on a plain note-off for an MPE note)
        bool onMember = false;
        if (! e.isOn && ! e.isExpression && ! e.mpe)
            for (const auto& m : mpeOutChannels)
                onMember = onMember || (m.source == sourceId && m.note == e.note);

        if (e.mpe || e.isExpression || onMember)
        {
            // as MPE: every note on a member channel of its own (2..16), its bend
            // (+-48 semitones), pressure and Slide (CC 74) on that channel
            // (slot i is member channel i + 2)
            int ch = 0;
            for (size_t i = 0; i < mpeOutChannels.size(); ++i)
                if (mpeOutChannels[i].source == sourceId && mpeOutChannels[i].note == e.note) { ch = (int) i + 2; break; }
            if (e.isOn && ! e.isExpression && ch == 0)
            {
                int pick = -1;
                for (size_t k = 0; k < mpeOutChannels.size() && pick < 0; ++k)
                {
                    const size_t i = (mpeOutNext + k) % mpeOutChannels.size();   // round the channels (a release tail keeps its own)
                    // (6.9.10: not one a played finger holds right now - a plugin hearing
                    // the keys too would mix the two notes' bends)
                    if (mpeOutChannels[i].source < 0 && mpeChanNote[i + 2] < 0) pick = (int) i;
                }
                if (pick < 0)   // all taken: the oldest note's channel
                {
                    pick = 0;
                    for (size_t i = 1; i < mpeOutChannels.size(); ++i)
                        if (mpeOutChannels[i].age < mpeOutChannels[(size_t) pick].age) pick = (int) i;
                }
                ++mpeOutNext;
                mpeOutChannels[(size_t) pick] = { sourceId, e.note, pick + 2, mpeOutNext };
                ch = pick + 2;
            }
            // (6.9.10) MPE goes only to what plays it; the others hear the
            // note itself on channel 1 and none of its expression
            auto takesMpe = [&graph] (int ni) { return (size_t) ni < graph.rawTakesMpe.size() && graph.rawTakesMpe[(size_t) ni] != 0; };
            if (! e.isExpression)
            {
                const auto plain = e.isOn ? juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.0f, 1.0f, e.velocity))
                                          : juce::MidiMessage::noteOff (1, e.note);
                for (int ni : stream->rawListeners)
                    if (! takesMpe (ni))
                        graph.nodes[(size_t) ni]->dsp->deliverRawMidi (plain);
            }
            if (ch != 0)   // (no channel: the MPE modules miss it; the processors below still hear it)
            {
                auto send = [&] (const juce::MidiMessage& msg)
                {
                    for (int ni : stream->rawListeners)
                        if (takesMpe (ni))
                            graph.nodes[(size_t) ni]->dsp->deliverRawMidi (msg);
                };
                if (e.isOn || e.isExpression)
                {
                    const int wheel = juce::jlimit (0, 16383, 8192 + (int) std::lround (e.bend / 48.0f * 8192.0f));
                    send (juce::MidiMessage::pitchWheel (ch, wheel));
                    if (e.pressure >= 0.0f) send (juce::MidiMessage::channelPressureChange (ch, juce::jlimit (0, 127, (int) std::lround (e.pressure * 127.0f))));
                    if (e.slide >= 0.0f) send (juce::MidiMessage::controllerEvent (ch, 74, juce::jlimit (0, 127, (int) std::lround (e.slide * 127.0f))));
                }
                if (e.isOn && ! e.isExpression)
                    send (juce::MidiMessage::noteOn (ch, e.note, juce::jlimit (0.0f, 1.0f, e.velocity)));
                else if (! e.isOn && ! e.isExpression)
                {
                    send (juce::MidiMessage::noteOff (ch, e.note));
                    for (auto& m : mpeOutChannels)
                        if (m.source == sourceId && m.note == e.note) m.source = -1;
                }
            }
        }
        else
        {
            const auto msg = e.isOn ? juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.0f, 1.0f, e.velocity))
                                    : juce::MidiMessage::noteOff (1, e.note);
            for (int ni : stream->rawListeners)
                graph.nodes[(size_t) ni]->dsp->deliverRawMidi (msg);
        }
    }

    // (expression goes on only to the processors that take MPE - feedProcessor)
    if (depth < kMaxMidiChainDepth)
        for (int nodeIndex : stream->processors)
            feedProcessor (graph, nodeIndex, e, depth + 1);
}

void AquanodeModularAudioProcessor::feedProcessor (const ProcessGraph& graph, int nodeIndex,
                                                   const MidiDriverEvent& e, int depth)
{
    auto& node = *graph.nodes[(size_t) nodeIndex];

    MidiDriverEvent out[16];
    int n = 0;
    if (node.dsp->acceptsMpeEvents())
        n = node.dsp->processMidiEvent (e, out, 16);
    else
    {
        // a processor that knows notes only: no expression, and plain notes
        // (so anything that is not a note-on is a real note-off)
        if (e.isExpression)
            return;
        MidiDriverEvent plain;
        plain.note = e.note;
        plain.velocity = e.velocity;
        plain.isOn = e.isOn;
        n = node.dsp->processMidiEvent (plain, out, 16);
    }
    for (int i = 0; i < n; ++i)
        emitMidi (graph, node.id, out[i], depth);
}

// MPE, a member channel (2..16): a note-on plays as any key and its voice
// takes the channel; bend, Slide (CC 74) and pressure go to the voice playing
// on that channel (kept for the channel, for a note-on still to come)
void AquanodeModularAudioProcessor::mpeMessage (const ProcessGraph& graph, const juce::MidiMessage& msg)
{
    constexpr float kMemberBendSemis = 48.0f;   // MPE's default for member channels
    const int ch = juce::jlimit (1, 16, msg.getChannel());
    auto toVoices = [&]
    {
        for (int v = 0; v < kMaxVoices; ++v)
            if (mpeChannelOfVoice[v] == ch)
            {
                midiState.voiceBend[v] = mpeChanBend[ch] * kMemberBendSemis;
                midiState.voiceSlide[v] = mpeChanSlide[ch];
                midiState.voiceSlideSet[v] = mpeChanSlideSet[ch];
                midiState.voicePressure[v] = mpeChanPressure[ch];
            }
    };

    if (msg.isNoteOn())
    {
        // (6.9.10) the processors fed by the keys (MIDI Add) get the finger as an MPE note
        MidiDriverEvent on;
        on.note = msg.getNoteNumber();
        on.velocity = msg.getFloatVelocity();
        on.isOn = true;
        on.mpe = true;
        on.bend = mpeChanBend[ch] * kMemberBendSemis;
        on.pressure = mpeChanPressure[ch];
        on.slide = mpeChanSlideSet[ch] ? mpeChanSlide[ch] : -1.0f;   // (6.9.12: none sent - none)
        mpeChanNote[ch] = on.note;
        keyboardEvent (graph, on);
        // the voice that took it: the newest held one on this key
        int best = -1;
        for (int v = 0; v < kMaxVoices; ++v)
            if (voiceStates[v].active && voiceStates[v].held && voiceStates[v].note == msg.getNoteNumber()
                && (best < 0 || voiceStates[v].order > voiceStates[best].order))
                best = v;
        for (int v = 0; v < kMaxVoices; ++v)
            if (mpeChannelOfVoice[v] == ch) mpeChannelOfVoice[v] = 0;   // an older note on the channel lets go of it
        if (best >= 0)
            mpeChannelOfVoice[best] = ch;
        toVoices();
    }
    else if (msg.isPitchWheel())
    {
        mpeChanBend[ch] = juce::jlimit (-1.0f, 1.0f, (float) (msg.getPitchWheelValue() - 8192) / 8192.0f);
        toVoices();
        keyboardExpression (graph, ch);
    }
    else if (msg.isController() && msg.getControllerNumber() == 74)
    {
        mpeChanSlide[ch] = (float) msg.getControllerValue() / 127.0f;
        mpeChanSlideSet[ch] = true;
        toVoices();
        keyboardExpression (graph, ch);
        handleControlMessage (graph, msg);
    }
    else if (msg.isChannelPressure())
    {
        mpeChanPressure[ch] = (float) msg.getChannelPressureValue() / 127.0f;
        toVoices();
        keyboardExpression (graph, ch);
        handleControlMessage (graph, msg);
    }
    else
        handleControlMessage (graph, msg);
}

// a member channel's bend, pressure or slide moved: the processors fed by the
// keys hear it as the expression of the note held there (6.9.10)
void AquanodeModularAudioProcessor::keyboardExpression (const ProcessGraph& graph, int ch)
{
    constexpr float kMemberBendSemis = 48.0f;
    if (ch < 2 || ch > 16 || mpeChanNote[ch] < 0 || graph.keyboardProcessors.empty())
        return;
    MidiDriverEvent e;
    e.note = mpeChanNote[ch];
    e.velocity = 0.0f;
    e.mpe = true;
    e.isExpression = true;
    e.bend = mpeChanBend[ch] * kMemberBendSemis;
    e.pressure = mpeChanPressure[ch];
    e.slide = mpeChanSlideSet[ch] ? mpeChanSlide[ch] : -1.0f;
    for (int nodeIndex : graph.keyboardProcessors)
        feedProcessor (graph, nodeIndex, e, 1);
}

// a played key (host MIDI or the on-screen keyboard)
void AquanodeModularAudioProcessor::handleControlMessage (const ProcessGraph& graph, const juce::MidiMessage& msg)
{
    if (msg.isPitchWheel())
    {
        midiState.bend = juce::jlimit (-1.0f, 1.0f, (float) (msg.getPitchWheelValue() - 8192) / 8192.0f);
        midiState.lastControl = 128;
        if (! screenMidiNow) ++midiState.controlCount;   // (6.9.10: MIDI Learn skips the on-screen keys' own expression)
    }
    else if (msg.isController())
    {
        const int cc = msg.getControllerNumber();
        const float v = (float) msg.getControllerValue() / 127.0f;
        midiState.cc[cc & 127] = v;
        midiState.lastControl = cc & 127;
        if (! screenMidiNow) ++midiState.controlCount;   // (6.9.10: MIDI Learn skips the on-screen keys' own expression)

        if (cc == 1)
            midiState.modWheel = v;

        if (cc == 64)
        {
            const bool down = msg.getControllerValue() >= 64;
            if (midiState.sustain && ! down)
            {
                // pedal up: every key let go while it was down is released now
                midiState.sustain = false;
                for (int note = 0; note < 128; ++note)
                    if (sustainedKeys[note])
                    {
                        sustainedKeys[note] = false;
                        keyboardEvent (graph, { note, 0.0f, false });
                    }
            }
            midiState.sustain = down;
        }
    }
    else if (msg.isChannelPressure())
    {
        midiState.aftertouch = (float) msg.getChannelPressureValue() / 127.0f;
        midiState.lastControl = 129;
        if (! screenMidiNow) ++midiState.controlCount;   // (6.9.10: MIDI Learn skips the on-screen keys' own expression)
    }
    else if (msg.isAftertouch())
    {
        midiState.aftertouch = (float) msg.getAfterTouchValue() / 127.0f;
        midiState.lastControl = 129;
        if (! screenMidiNow) ++midiState.controlCount;   // (6.9.10: MIDI Learn skips the on-screen keys' own expression)
    }
}

void AquanodeModularAudioProcessor::keyboardEvent (const ProcessGraph& graph, const MidiDriverEvent& e)
{
    // Sustain pedal: a key let go while the pedal is down keeps sounding until
    // the pedal comes up (striking it again re-triggers it as usual)
    if (e.note >= 0 && e.note < 128)
    {
        if (! e.isOn && midiState.sustain)
        {
            sustainedKeys[e.note] = true;
            return;
        }
        if (e.isOn)
            sustainedKeys[e.note] = false;
    }

    // the key itself, for every module with nothing in its Midi In - always
    // started, since an ADSR alone (an FX patch) wants it too
    if (e.isOn) startVoice (graph, e.note, -1, e.velocity);
    else        releaseVoices (graph.nodes, -1, e.note);

    // and every processor with an empty Midi In (Midi Add chords the keys,
    // Discard Midi filters them, the Arp arpeggiates them, Keyboard Midi
    // passes them on)
    for (int nodeIndex : graph.keyboardProcessors)
        feedProcessor (graph, nodeIndex, e, 1);
}

void AquanodeModularAudioProcessor::engineAllNotesOff (const ProcessGraph& graph)
{
    const auto& nodes = graph.nodes;
    const double tail = computeVoiceTail (nodes);
    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (voiceStates[v].active && voiceStates[v].held)
        {
            voiceStates[v].held = false;
            voiceStates[v].countdown = tail;

            for (const auto& n : nodes)
            {
                if (n->perVoice)
                    n->dsp->voiceNoteOff (v);
                else
                    n->dsp->globalMidiNoteOff (v);
            }
        }
    }

    for (const auto& n : nodes)
        n->dsp->midiAllNotesOff();
}
