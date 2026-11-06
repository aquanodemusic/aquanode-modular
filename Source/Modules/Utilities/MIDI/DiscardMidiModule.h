#pragma once

#include "../../Core/ModuleCore.h"

// Discard Midi - a note filter for the keyboard. Twelve toggle boxes, one per
// note class (C, C#, D ... B, in three rows of four). Every class switched ON
// is discarded: patch this module's Midi Out into a generator's Add Midi In
// and that generator never hears those notes, in any octave. Switch on
// everything outside your key and wrong notes simply stop existing - useful
// live, and lethal on a Step Seq or Arp feeding a scale you want enforced.
//
// Like Always Midi and the Arp (and unlike Midi Add), it REPLACES the played
// keys for anything listening to it: patch it in and that generator answers
// to this module alone, hearing the filtered keyboard rather than the raw
// one. Generators with no cable from it are unaffected, so the same keys can
// drive a filtered voice and an unfiltered one side by side.
//
// ALL, the wide box under them, discards every note: patch it into a
// module's Midi In and that module no longer hears the keys at all (a drum
// played only by its Trig In, say).
//
// The boxes are ordinary hidden parameters driven by the grid UI, flagged as
// cable targets - so an LFO or DAW automation can open and close a note class
// while you play.
class DiscardMidiModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumClasses = 12;
    static constexpr int kAll = 12;           // the ALL box (param index 12)
    static constexpr int kNumBoxes = 13;

    // notes reaching the listeners are the played ones minus these classes
    bool midiSourceReplacesInput() const override { return true; }

    // filters whatever arrives on its Midi In (the played keys when nothing
    // is patched there); note-offs always pass, so nothing can hang
    bool isMidiProcessor() const override { return true; }

    int processMidiEvent (const aquanode::MidiDriverEvent& in, aquanode::MidiDriverEvent* out, int maxOut) override
    {
        if (maxOut <= 0 || (in.isOn && isNoteDiscarded (in.note)))
            return 0;
        out[0] = in;
        return 1;
    }

    // audio thread: asked once per note-on. param() folds in any modulation,
    // so a cable into a box can gate that class in real time.
    bool isNoteDiscarded (int note) const
    {
        return param (kAll) > 0.5f || param (juce::jlimit (0, 127, note) % kNumClasses) > 0.5f;
    }

    // a box: 0..11 the note classes, kAll the ALL box
    bool isClassDiscarded (int noteClass) const
    {
        return param (juce::jlimit (0, kNumBoxes - 1, noteClass)) > 0.5f;
    }

    // effective state incl. modulation, for the grid UI's live markers
    bool isClassLiveDiscarded (int noteClass) const { return isClassDiscarded (noteClass); }

    bool isClassSetByHand (int noteClass) const
    {
        return getParameterBase (juce::jlimit (0, kNumBoxes - 1, noteClass)) > 0.5f;
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 102; }

    // no audio passes through this module; the base class zeroes the outputs
};
