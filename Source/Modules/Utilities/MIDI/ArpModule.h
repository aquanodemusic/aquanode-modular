#pragma once

#include "../../Core/ModuleCore.h"

// Arp - holds down whatever chord you play and walks through it in time.
// Patch its Midi Out into a generator's Add Midi In and that generator hears
// ONLY the arpeggio, not the chord you are holding - which is the difference
// between this and Midi Add. Midi Add layers notes on top of yours; the Arp
// takes them over. Generators with nothing patched keep playing the chord as
// normal, so one Arp'd Oscillator over one plain pad is a two-cable trick.
//
// Runs from the host tempo. Octaves repeats the chord upward; Gate sets how
// much of each step actually sounds (short = staccato, ~100% = legato lines
// that feed nicely into Glide with Voices = 1); Swing delays every second step.
class ArpModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pDivision = 0, pMode, pOctaves, pGate, pSwing, pChance, pRatchet, pHits };
    enum Mode { mUp = 0, mDown, mUpDown, mRandom, mAsPlayed };

    // this source REPLACES the played notes for anything listening to it
    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }

    void reset() override
    {
        numHeld = 0;
        currentNote = -1;
        stepIndex = 0;
        phase = 0.0;
        goingUp = true;
        gateClosed = true;
        ratchetHits = 1;
        hitIndex = 0;
    }

    //=== called by the engine as keys go down and up (audio thread) ==========
    void midiDriverHeldNoteOn (int note) override
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[i] == note)
                return;                    // already down
        if (numHeld < 128)
            held[numHeld++] = note;        // keep the order they were played in
    }

    void midiDriverHeldNoteOff (int note) override
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                    held[j] = held[j + 1];
                --numHeld;
                return;
            }
    }

    void midiDriverAllHeldOff() override { numHeld = 0; }

    // the notes it arpeggiates arrive like any processor's (the played keys
    // when its Midi In is empty); what it plays comes from its own clock
    bool isMidiProcessor() const override { return true; }

    // MPE (6.9.10): each arp note takes the bend, pressure and slide of the
    // held key it comes from (that key, or an octave of it), and follows them
    // while it sounds
    bool acceptsMpeEvents() const override { return true; }
    int processMidiEvent (const aquanode::MidiDriverEvent& in, aquanode::MidiDriverEvent*, int) override
    {
        const int key = juce::jlimit (0, 127, in.note);
        if (in.isExpression || in.isOn)
        {
            auto& x = expr[key];
            x.mpe = in.mpe;
            x.bend = in.bend;
            x.pressure = in.pressure;
            x.slide = in.slide;
            if (in.isExpression)
            {
                if (currentNote >= 0 && ! gateClosed && sourceKey (currentNote) == key)
                    expressionPending = true;
                return 0;
            }
        }
        if (in.isOn) midiDriverHeldNoteOn (in.note);
        else         midiDriverHeldNoteOff (in.note);
        return 0;
    }

    int midiDriverAdvanceEvents (double sr, aquanode::MidiDriverEvent* events, int maxEvents) override
    {
        int onNote = -1, offNote = -1;
        midiDriverAdvance (sr, onNote, offNote);
        int n = 0;
        if (offNote >= 0 && n < maxEvents)
        {
            aquanode::MidiDriverEvent off;
            off.note = offNote;
            off.velocity = 0.0f;
            off.isOn = false;
            off.mpe = soundingMpe;
            events[n++] = off;
        }
        if (onNote >= 0 && n < maxEvents)
        {
            aquanode::MidiDriverEvent on;
            on.note = onNote;
            on.velocity = 0.85f;
            on.isOn = true;
            const int k = sourceKey (onNote);
            if (k >= 0 && expr[k].mpe)
            {
                on.mpe = true;
                on.bend = expr[k].bend;
                on.pressure = expr[k].pressure;
                on.slide = expr[k].slide;
            }
            soundingMpe = on.mpe;
            events[n++] = on;
            expressionPending = false;
        }
        else if (expressionPending && currentNote >= 0 && soundingMpe && n < maxEvents)
        {
            const int k = sourceKey (currentNote);
            aquanode::MidiDriverEvent x;
            x.note = currentNote;
            x.velocity = 0.0f;
            x.mpe = true;
            x.isExpression = true;
            if (k >= 0) { x.bend = expr[k].bend; x.pressure = expr[k].pressure; x.slide = expr[k].slide; }
            events[n++] = x;
            expressionPending = false;
        }
        return n;
    }
    void midiAllNotesOff() override { numHeld = 0; }

    bool isIdle() const { return numHeld == 0 && currentNote < 0; }

    // One sample of arpeggiator time. Reports at most one note-off and one
    // note-on; both can land on the same sample when a step boundary arrives
    // with the previous step still sounding.
    void midiDriverAdvance (double sr, int& onNote, int& offNote) override
    {
        onNote = -1;
        offNote = -1;

        if (numHeld == 0)
        {
            if (currentNote >= 0)          // chord released: let the last note go
            {
                offNote = currentNote;
                currentNote = -1;
            }
            phase = 0.0;
            stepIndex = 0;
            goingUp = true;
            return;
        }

        const double bpm = tempoBpm > 1.0 ? tempoBpm : 120.0;
        double stepSeconds = (60.0 / bpm) * aquanode::seqDivisionBeats((int)param(pDivision));

        // swing: every second step starts late, so lengthen it and shorten the next
        const double swing = param (pSwing) * 0.01 * 0.5;
        if (swing > 0.0)
            stepSeconds *= (stepCounter % 2 == 0) ? (1.0 + swing) : (1.0 - swing);

        phase += 1.0 / (juce::jmax (0.0005, stepSeconds) * sr);

        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            if (currentNote >= 0 && ! gateClosed)
                offNote = currentNote;

            const int next = pickNext();
            ++stepCounter;
            // Chance and Ratchet: at their defaults (100 %, 0 %) nothing is drawn, so the
            // arpeggio plays exactly as it did. A step that does not play is a rest; the
            // arpeggio still moves on.
            const float chance = param (pChance), ratchet = param (pRatchet);
            const bool plays = next >= 0 && (chance >= 100.0f || random.nextFloat() * 100.0f < chance);
            ratchetHits = plays && ratchet > 0.0f && random.nextFloat() * 100.0f < ratchet
                            ? juce::jlimit (2, 4, (int) std::lround (param (pHits))) : 1;
            hitIndex = 0;
            onNote = plays ? next : -1;
            currentNote = onNote;
            gateClosed = ! plays;
        }
        else if (currentNote >= 0)
        {
            // the step's time shared among its hits, each one gated by Gate
            const double sub = phase * ratchetHits;
            const int hit = (int) sub;
            if (hit > hitIndex && hit < ratchetHits)
            {
                hitIndex = hit;
                if (! gateClosed)
                    offNote = currentNote;
                onNote = currentNote;
                gateClosed = false;
            }
            else if (! gateClosed && sub - hit >= param (pGate) * 0.01)
            {
                offNote = currentNote;
                gateClosed = true;
            }
        }
    }

private:
    // the chord, expanded across the octave range, in the current direction
    int buildSequence (int* out) const
    {
        int sorted[128];
        for (int i = 0; i < numHeld; ++i)
            sorted[i] = held[i];
        std::sort (sorted, sorted + numHeld);

        const bool asPlayed = (int) param (pMode) == mAsPlayed;
        const int* src = asPlayed ? held : sorted;
        const int octaves = juce::jlimit (1, 4, (int) param (pOctaves));

        int n = 0;
        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < numHeld; ++i)
            {
                const int note = src[i] + o * 12;
                if (note <= 127)
                    out[n++] = note;
            }
        return n;
    }

    int pickNext()
    {
        int seq[512];
        const int n = buildSequence (seq);
        if (n <= 0)
            return -1;

        switch ((int) param (pMode))
        {
            case mDown:
                stepIndex = (stepIndex - 1 + n) % n;
                break;

            case mUpDown:
                if (n == 1) { stepIndex = 0; break; }
                if (goingUp)
                {
                    if (++stepIndex >= n - 1) { stepIndex = n - 1; goingUp = false; }
                }
                else
                {
                    if (--stepIndex <= 0) { stepIndex = 0; goingUp = true; }
                }
                break;

            case mRandom:
                stepIndex = random.nextInt (n);
                break;

            default:   // Up / As Played
                stepIndex = (stepIndex + 1) % n;
                break;
        }

        return seq[juce::jlimit (0, n - 1, stepIndex)];
    }

    // the held key an arp note comes from (it, or one of its octaves up)
    int sourceKey (int note) const
    {
        for (int i = 0; i < numHeld; ++i)
            if (note >= held[i] && (note - held[i]) % 12 == 0 && note - held[i] <= 36)
                return held[i];
        return -1;
    }

    struct Expression { bool mpe { false }; float bend { 0.0f }, pressure { -1.0f }, slide { -1.0f }; };
    Expression expr[128];
    bool soundingMpe { false }, expressionPending { false };

    int held[128] {};
    int numHeld { 0 };
    int currentNote { -1 };
    int stepIndex { 0 };
    juce::uint64 stepCounter { 0 };
    double phase { 0.0 };
    bool goingUp { true };
    bool gateClosed { true };
    int ratchetHits { 1 }, hitIndex { 0 };
    juce::Random random;
};
