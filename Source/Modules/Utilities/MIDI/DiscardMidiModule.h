#pragma once

#include "../../Core/ModuleCore.h"

// Discard MIDI - a note filter for the keyboard. Twelve toggle boxes, one per
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
// Global mode (6.9.10): Key and Scale choosers (the Note Adder's scales) in
// place of the boxes, and it applies to every played key - every generator
// and plugin hears only the notes of that key and scale, patched or not. Its
// own MIDI Out is filtered the same way. While a Microtuning module plays the
// keys as Linear steps the key and scale are greyed out and nothing is
// filtered by them (the card says why).
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
    // 6.9.10: Global mode (key and scale, for every played key), appended
    static constexpr int kGlobal = 13, kKey = 14, kScale = 15;
    static constexpr int kNumScales = 18;
    static const char* const* scaleNames();
    static bool scaleHas (int scale, int pitchClass);   // pitch class above the key

    // notes reaching the listeners are the played ones minus these classes
    bool midiSourceReplacesInput() const override { return true; }

    // filters whatever arrives on its Midi In (the played keys when nothing
    // is patched there); note-offs always pass, so nothing can hang
    bool isMidiProcessor() const override { return true; }
    bool acceptsMpeEvents() const override { return true; }   // MPE notes pass as they are

    int processMidiEvent (const aquanode::MidiDriverEvent& in, aquanode::MidiDriverEvent* out, int maxOut) override
    {
        if (maxOut <= 0 || (in.isOn && ! in.isExpression && isNoteDiscarded (in.note)))
            return 0;
        out[0] = in;
        return 1;
    }

    // Global mode: on, and the key and scale apply (not while a Microtuning
    // plays the keys as Linear steps, where a 12-note key means little)
    bool globalModeOn() const { return param (kGlobal) > 0.5f; }
    bool keyAndScaleApply() const { return ! linearKeysInPatch(); }

    // the played keys, for everything that hears them (engine, audio thread)
    bool discardsPlayedKey (int note) const override
    {
        return globalModeOn() && keyAndScaleApply() && ! inKeyAndScale (note);
    }

    bool inKeyAndScale (int note) const
    {
        const int key = juce::jlimit (0, 11, (int) std::lround (param (kKey)));
        const int scale = juce::jlimit (0, kNumScales - 1, (int) std::lround (param (kScale)));
        return scaleHas (scale, ((juce::jlimit (0, 127, note) - key) % 12 + 12) % 12);
    }

    // audio thread: asked once per note-on. param() folds in any modulation,
    // so a cable into a box can gate that class in real time.
    bool isNoteDiscarded (int note) const
    {
        if (param (kAll) > 0.5f)
            return true;
        if (globalModeOn())
            return keyAndScaleApply() && ! inKeyAndScale (note);
        return param (juce::jlimit (0, 127, note) % kNumClasses) > 0.5f;
    }

    // a box: 0..11 the note classes, kAll the ALL box (in Global mode the
    // classes show the ones the key and scale leave out)
    bool isClassDiscarded (int noteClass) const
    {
        if (noteClass >= 0 && noteClass < kNumClasses && globalModeOn())
            return keyAndScaleApply() && ! inKeyAndScale (noteClass);
        return param (juce::jlimit (0, kNumBoxes - 1, noteClass)) > 0.5f;
    }

    // effective state incl. modulation, for the grid UI's live markers
    bool isClassLiveDiscarded (int noteClass) const { return isClassDiscarded (noteClass); }

    bool isClassSetByHand (int noteClass) const
    {
        if (noteClass >= 0 && noteClass < kNumClasses && getParameterBase (kGlobal) > 0.5f)
            return isClassDiscarded (noteClass);   // (no cable marker in Global mode)
        return getParameterBase (juce::jlimit (0, kNumBoxes - 1, noteClass)) > 0.5f;
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 102 + 18; }   // (+ a line of text under the boxes)

    // no audio passes through this module; the base class zeroes the outputs
};
