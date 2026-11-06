//==============================================================================
// Graphic Midi Add - Midi Add with a keyboard to see the chord on
//
// For every note it hears it plays that note plus up to seven more, each Note
// knob an interval of -48..+48 semitones (0 = that slot off). A note that
// lands outside the MIDI range (C-1 .. G9) is not played. The card's keyboard
// shows the chord built on one note - the one the Visual (Reference Only)
// knob picks, which changes nothing in the sound - with the reference note in
// the brightest colour, the added notes paler, and how many of them would
// fall off the keyboard from there.
//
// As in Midi Add the intervals are read at note-on (so a cable into a Note
// knob revoices the next chord while sounding notes keep their pitch), and a
// note-off releases exactly the notes its note-on played.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

namespace
{
    constexpr int kSlots = 7;
    enum P { pVisual, pChord, pNote1 };

    // the Chord menu: ready chords over the played note (0 = slot off), and
    // Custom, which it shows by itself once a Note knob is turned by hand
    const juce::StringArray kChordNames { "DnB Chord", "Minor 7th", "Minor 9th", "6/9", "Major 7th", "Custom" };
    constexpr int kNumChords = 5, kCustom = 5;
    const float kChords[kNumChords][kSlots] = {
        { 4, -1, -5, -15, -27, 0, 0 },   // DnB Chord: a lush minor chord spread two octaves down
        { 3, 7, 10, 0, 0, 0, 0 },        // minor 7th
        { 3, 7, 10, 14, 0, 0, 0 },       // minor 9th
        { 4, 7, 9, 14, 0, 0, 0 },        // 6/9
        { 4, 7, 11, 0, 0, 0, 0 } };      // major 7th

    juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }

    // the chord on a note: the note itself first, then the intervals that are
    // on, in slot order, each pitch once; ones off the keyboard are counted
    int chordOn (int root, const int* offsets, int* notes, int& offKeyboard)
    {
        int n = 0;
        offKeyboard = 0;
        auto add = [&] (int p)
        {
            if (p < 0 || p > 127) { ++offKeyboard; return; }
            for (int i = 0; i < n; ++i)
                if (notes[i] == p) return;
            notes[n++] = p;
        };
        add (root);
        for (int s = 0; s < kSlots; ++s)
            if (offsets[s] != 0)
                add (root + offsets[s]);
        return n;
    }
}

class GraphicMidiAddModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.graphicmidiadd";
        d.displayName = "Graphic Midi Add";
        d.description = "For every note it hears it plays that note plus up to seven more, -48..+48 semitones "
                        "away (0 = off); notes that would fall off the keyboard (outside C-1..G9) are left out. "
                        "The keyboard shows the chord built on the Visual (Reference Only) note, which changes "
                        "nothing in the sound. The intervals are read at note-on, so a cable into a Note knob "
                        "revoices the next chord while sounding notes keep their pitch.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 20;
        d.sockets = { midiIn ("midiIn", "Midi In"), midiOut ("midiOut", "Midi Out") };

        auto visual = makeRotary ("visual", "Visual (Reference Only)", 0.0f, 127.0f, 60.0f, 0, {}, false, 1.0f).noMod();
        visual.widthUnits = 3;
        visual.textFromValue = [] (double v) { return noteName (juce::jlimit (0, 127, (int) std::lround (v))); };
        visual.valueFromText = [] (const juce::String& t)
        {
            // a number, or a name such as C4, F#2, Bb-1
            const auto s = t.trim().toUpperCase();
            if (s.containsOnly ("0123456789")) return (double) s.getIntValue();
            static const int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
            const int letter = s[0] - 'A';
            if (letter < 0 || letter > 6) return 60.0;
            int pc = pcs[letter], i = 1;
            if (s[i] == '#') { ++pc; ++i; } else if (s[i] == 'B') { --pc; ++i; }
            const int octave = s.substring (i).getIntValue();
            return (double) juce::jlimit (0, 127, (octave + 1) * 12 + pc);
        };
        d.params.push_back (visual);
        d.params.push_back (makeCombo ("chord", "Chord", kChordNames, 0, 0, 3).noMod());

        // the DnB Chord to begin with (a lush chord spread over two octaves down)
        for (int i = 0; i < kSlots; ++i)
            d.params.push_back (makeRotary ("note" + juce::String (i + 1), "Note " + juce::String (i + 1),
                                            -48.0f, 48.0f, kChords[0][i], 1 + i / 4, "st", false, 1.0f));
        return d;
    }

    // picking a chord sets the Note knobs; turning a Note knob makes the
    // chord Custom (or the ready chord it now matches). A patch keeps the
    // chord before its notes, so its own notes come back as they were.
    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        if (applyingChord)
            return;
        const juce::ScopedValueSetter<bool> guard (applyingChord, true);
        if (id == "chord")
        {
            const int c = (int) std::lround (value);
            if (c >= 0 && c < kNumChords)
                for (int i = 0; i < kSlots; ++i)
                    SynthModule::setParameter ("note" + juce::String (i + 1), kChords[c][i]);
        }
        else if (id.startsWith ("note"))
        {
            int match = kCustom;
            for (int c = 0; c < kNumChords && match == kCustom; ++c)
            {
                bool same = true;
                for (int i = 0; i < kSlots && same; ++i)
                    same = (int) std::lround (getParameterBase (pNote1 + i)) == (int) kChords[c][i];
                if (same)
                    match = c;
            }
            SynthModule::setParameter ("chord", (float) match);
        }
    }

    bool isMidiProcessor() const override { return true; }

    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent* out, int maxOut) override
    {
        const int key = juce::jlimit (0, 127, in.note);
        int n = 0;

        // what this key played last time is released, whatever the knobs say now
        for (int i = 0; i < played[key].size && n < maxOut; ++i)
            out[n++] = { played[key].notes[i], 0.0f, false };
        played[key].size = 0;

        if (! in.isOn)
            return n;

        int offsets[kSlots], off = 0;
        for (int s = 0; s < kSlots; ++s)
            offsets[s] = (int) std::lround (param (pNote1 + s));
        auto& c = played[key];
        c.size = chordOn (key, offsets, c.notes, off);
        for (int i = 0; i < c.size && n < maxOut; ++i)
            out[n++] = { c.notes[i], in.velocity, true };
        return n;
    }

    void midiAllNotesOff() override { for (auto& c : played) c.size = 0; }
    void reset() override { midiAllNotesOff(); }

    // for the keyboard (message thread): the knob settings
    int visualNote() const { return juce::jlimit (0, 127, (int) std::lround (getParameterBase (pVisual))); }
    int offset (int slot) const { return (int) std::lround (getParameterBase (pNote1 + slot)); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 96; }

private:
    struct Chord { int notes[kSlots + 1] {}; int size { 0 }; };
    Chord played[128];
    bool applyingChord { false };
};

namespace
{
    // the chord on the reference note, on a keyboard of whole octaves from its
    // lowest note to its highest (two at least): the reference note lit, the
    // added ones paler, their names above and the intervals below
    class ChordKeyboard final : public juce::Component,
                                public aquanode::ExtraContentParamListener,
                                private juce::Timer
    {
    public:
        explicit ChordKeyboard (GraphicMidiAddModule& m) : module (&m) { timerCallback(); startTimerHz (15); }

        void paint (juce::Graphics& g) override
        {
            auto* m = dynamic_cast<GraphicMidiAddModule*> (module.get());
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (r, 3.0f);
            if (m == nullptr) return;

            const int root = shown.root;
            int notes[kSlots + 1], off = 0;
            const int count = chordOn (root, shown.offsets.data(), notes, off);
            std::vector<int> sorted (notes, notes + count);
            std::sort (sorted.begin(), sorted.end());
            if (sorted.empty()) return;

            const auto lit = juce::Colour (0xffffc35a);                       // the reference note
            const auto added = lit.interpolatedWith (juce::Colours::white, 0.55f);
            auto in = r.reduced (5.0f, 3.0f);

            // names on top
            auto top = in.removeFromTop (14.0f);
            juce::StringArray names;
            for (int n : sorted) names.add (noteName (n));
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawFittedText (names.joinIntoString ("  "), top.toNearestInt(), juce::Justification::centred, 1, 0.6f);

            // the intervals, and what falls off the keyboard, below
            auto bottom = in.removeFromBottom (12.0f);
            juce::StringArray steps;
            std::vector<int> on;
            for (int o : shown.offsets) if (o != 0) on.push_back (o);
            std::sort (on.begin(), on.end());
            on.erase (std::unique (on.begin(), on.end()), on.end());
            for (int o : on) steps.add ((o > 0 ? "+" : "") + juce::String (o));
            juce::String foot = on.empty() ? juce::String ("every Note off") : steps.joinIntoString (" ") + " st";
            if (off > 0) foot << "   (" << off << " off the keyboard)";
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.setColour (off > 0 ? juce::Colour (0xffff8a7a) : juce::Colours::white.withAlpha (0.6f));
            g.drawFittedText (foot, bottom.toNearestInt(), juce::Justification::centred, 1, 0.6f);

            // whole octaves around the chord, inside the MIDI range
            const int first = 12 * (sorted.front() / 12);
            int last = juce::jmin (127, 12 * (sorted.back() / 12) + 11);
            if (last - first + 1 < 24) last = juce::jmin (127, first + 23);
            auto isBlack = [] (int n) { const int pc = n % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
            int whites = 0;
            for (int n = first; n <= last; ++n) if (! isBlack (n)) ++whites;

            auto area = in.reduced (0.0f, 3.0f);
            const float kw = area.getWidth() / (float) juce::jmax (1, whites);
            const float kh = area.getHeight();
            auto fillFor = [&] (int n) -> juce::Colour
            {
                if (n == root) return lit;
                if (std::find (sorted.begin(), sorted.end(), n) != sorted.end()) return added;
                return juce::Colours::transparentBlack;
            };

            std::vector<std::pair<int, juce::Rectangle<float>>> blacks;
            int wi = 0;
            for (int n = first; n <= last; ++n)
            {
                if (isBlack (n))
                {
                    const float x = area.getX() + (float) wi * kw;
                    blacks.push_back ({ n, { x - kw * 0.32f, area.getY(), kw * 0.64f, kh * 0.6f } });
                    continue;
                }
                const auto key = juce::Rectangle<float> (area.getX() + (float) wi * kw, area.getY(), kw, kh).reduced (0.4f, 0.0f);
                const auto fill = fillFor (n);
                g.setColour (fill.isTransparent() ? juce::Colour (0xffd8d8d8) : fill);
                g.fillRoundedRectangle (key, 1.2f);
                if (n % 12 == 0 && kw >= 6.0f)
                {
                    // C's octave, small, at the foot of the key
                    g.setColour (juce::Colours::black.withAlpha (0.45f));
                    g.setFont (juce::Font (juce::FontOptions (juce::jmin (8.0f, kw * 0.9f))));
                    g.drawText (juce::String (n / 12 - 1), key.withTop (key.getBottom() - 10.0f), juce::Justification::centred, false);
                }
                ++wi;
            }
            for (auto& [n, key] : blacks)
            {
                const auto fill = fillFor (n);
                g.setColour (juce::Colour (0xff222222));
                g.fillRoundedRectangle (key, 1.0f);
                if (! fill.isTransparent())
                {
                    g.setColour (fill);
                    g.fillRoundedRectangle (key.reduced (0.8f), 0.8f);
                }
            }
        }

    private:
        struct Shown
        {
            int root { -1 };
            std::array<int, kSlots> offsets {};
            bool operator!= (const Shown& o) const { return root != o.root || offsets != o.offsets; }
        };

        void timerCallback() override
        {
            auto* m = dynamic_cast<GraphicMidiAddModule*> (module.get());
            if (m == nullptr) return;
            Shown now;
            now.root = m->visualNote();
            for (int s = 0; s < kSlots; ++s) now.offsets[(size_t) s] = m->offset (s);
            if (now != shown)
            {
                // a chord picked from the menu moved the Note knobs: the card shows them
                const bool notesMoved = now.offsets != shown.offsets;
                shown = now;
                repaint();
                if (notesMoved && paramValuesChangedByContent != nullptr)
                    paramValuesChangedByContent();
            }
        }

        juce::WeakReference<SynthModule> module;
        Shown shown;
    };
}

std::unique_ptr<juce::Component> GraphicMidiAddModule::createExtraContentComponent()
{
    return std::make_unique<ChordKeyboard> (*this);
}

AQUANODE_REGISTER_MODULE (GraphicMidiAddModule, GraphicMidiAddModule::descriptor)
