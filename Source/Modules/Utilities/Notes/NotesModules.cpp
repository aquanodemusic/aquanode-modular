// Two utilities that make no sound:
//
//   Text        a note on the patch: type anything into it (what the patch
//               is, how to play it, a lyric...). The text is kept exactly as
//               typed, inside the patch and its presets.
//   Chord Name  names what is being played: the keys held (or the notes on
//               its Midi In, from a Piano Roll, an Arp, Midi Add...) as a
//               chord - "Cmaj7", "F#m7b5", "Gsus4/D" - with the notes and the
//               bass under it.
#include "../../Core/ModuleCore.h"

using namespace aquanode;

//==============================================================================
// Text
//==============================================================================
class TextNoteModule : public SynthModule
{
public:
    enum ParamIndex { pSize = 0, pLines };

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void processSample (const StereoFrame*, StereoFrame*) override {}

    juce::String saveCustomState() const override { return text.isEmpty() ? juce::String() : "text1:" + text; }
    void loadCustomState (const juce::String& state) override
    {
        text = state.startsWith ("text1:") ? state.substring (6) : state;
        if (onTextLoaded) onTextLoaded();
    }

    juce::String text;
    std::function<void()> onTextLoaded;   // the card's editor follows a loaded patch

    int preferredModuleWidth() const override { return 300; }
    int extraContentHeight() const override
    {
        static const int lines[] = { 4, 7, 11, 16 };
        const int n = lines[juce::jlimit (0, 3, (int) std::lround (getParameter ("lines")))];
        return 12 + n * (int) std::ceil (fontSize() * 1.25f);
    }
    float fontSize() const
    {
        static const float sizes[] = { 13.0f, 16.0f, 20.0f, 26.0f };
        return sizes[juce::jlimit (0, 3, (int) std::lround (getParameter ("size")))];
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
};

namespace
{
    class TextNotePanel : public juce::Component
    {
    public:
        explicit TextNotePanel (TextNoteModule& m) : weak (&m)
        {
            editor.setMultiLine (true, true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setScrollbarsShown (true);
            editor.setTextToShowWhenEmpty ("Type a note here...", juce::Colours::white.withAlpha (0.4f));
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black.withAlpha (0.28f));
            editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.18f));
            editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::white.withAlpha (0.45f));
            editor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            editor.setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
            editor.setText (m.text, false);
            editor.onTextChange = [this] { if (auto* mod = module()) mod->text = editor.getText(); };
            m.onTextLoaded = [safe = juce::Component::SafePointer<TextNotePanel> (this)]
            {
                if (safe != nullptr)
                    if (auto* mod = safe->module())
                        safe->editor.setText (mod->text, false);
            };
            addAndMakeVisible (editor);
            applyFont();
        }
        ~TextNotePanel() override
        {
            if (auto* mod = module())
                mod->onTextLoaded = nullptr;
        }
        void resized() override
        {
            applyFont();
            editor.setBounds (getLocalBounds());
        }

    private:
        TextNoteModule* module() const { return static_cast<TextNoteModule*> (weak.get()); }
        void applyFont()
        {
            auto* mod = module();
            if (mod == nullptr) return;
            const float size = mod->fontSize();
            if (std::abs (size - shownSize) > 0.1f)
            {
                shownSize = size;
                editor.setFont (juce::Font (juce::FontOptions (size)));
                editor.applyFontToAllText (juce::Font (juce::FontOptions (size)), true);
            }
        }

        juce::WeakReference<SynthModule> weak;
        juce::TextEditor editor;
        float shownSize { -1.0f };
    };
}

std::unique_ptr<juce::Component> TextNoteModule::createExtraContentComponent()
{
    return std::make_unique<TextNotePanel> (*this);
}

static ModuleDescriptor textNoteDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.text";
    d.displayName = "Text";
    d.description =
        "A note on the patch: type anything into it - what the patch is, how to play it, a lyric. The text is kept "
        "exactly as typed, saved inside the patch and its presets. Size sets the letters, Lines how tall the card is.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 990;
    d.sockets = {};
    d.params = {
        makeCombo ("size",  "Size",  { "Small", "Medium", "Large", "Huge" }, 1, 0, 2).noMod(),
        makeCombo ("lines", "Lines", { "4", "7", "11", "16" }, 1, 0, 2).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (TextNoteModule, textNoteDescriptor)

//==============================================================================
// Chord Name
//==============================================================================
namespace chordnames
{
    const char* const kNotes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    struct Shape { int mask; const char* suffix; int rank; };   // rank: lower is the more usual reading

    constexpr int bits (std::initializer_list<int> n)
    {
        int m = 0;
        for (int x : n) m |= 1 << (x % 12);
        return m;
    }

    const Shape kShapes[] = {
        { bits ({ 0, 4, 7 }), "", 0 },               { bits ({ 0, 3, 7 }), "m", 0 },
        { bits ({ 0, 3, 6 }), "dim", 1 },            { bits ({ 0, 4, 8 }), "aug", 2 },
        { bits ({ 0, 2, 7 }), "sus2", 3 },           { bits ({ 0, 5, 7 }), "sus4", 2 },
        { bits ({ 0, 7 }), "5", 1 },
        { bits ({ 0, 4, 7, 10 }), "7", 0 },          { bits ({ 0, 4, 7, 11 }), "maj7", 0 },
        { bits ({ 0, 3, 7, 10 }), "m7", 0 },         { bits ({ 0, 3, 7, 11 }), "m(maj7)", 2 },
        { bits ({ 0, 3, 6, 10 }), "m7b5", 1 },       { bits ({ 0, 3, 6, 9 }), "dim7", 1 },
        { bits ({ 0, 4, 8, 10 }), "7#5", 2 },        { bits ({ 0, 4, 8, 11 }), "maj7#5", 3 },
        { bits ({ 0, 4, 7, 9 }), "6", 1 },           { bits ({ 0, 3, 7, 9 }), "m6", 1 },
        { bits ({ 0, 4, 7, 2 }), "add9", 1 },        { bits ({ 0, 3, 7, 2 }), "m(add9)", 1 },
        { bits ({ 0, 5, 7, 10 }), "7sus4", 1 },      { bits ({ 0, 2, 7, 10 }), "7sus2", 2 },
        { bits ({ 0, 4, 10 }), "7(no5)", 2 },        { bits ({ 0, 3, 10 }), "m7(no5)", 2 },
        { bits ({ 0, 4, 11 }), "maj7(no5)", 2 },
        { bits ({ 0, 4, 7, 10, 2 }), "9", 0 },       { bits ({ 0, 4, 7, 11, 2 }), "maj9", 0 },
        { bits ({ 0, 3, 7, 10, 2 }), "m9", 0 },      { bits ({ 0, 4, 7, 9, 2 }), "6/9", 1 },
        { bits ({ 0, 4, 7, 10, 1 }), "7b9", 1 },     { bits ({ 0, 4, 7, 10, 3 }), "7#9", 1 },
        { bits ({ 0, 4, 10, 2 }), "9(no5)", 2 },     { bits ({ 0, 3, 10, 2 }), "m9(no5)", 2 },
        { bits ({ 0, 4, 7, 10, 2, 5 }), "11", 1 },   { bits ({ 0, 3, 7, 10, 2, 5 }), "m11", 1 },
        { bits ({ 0, 3, 7, 10, 5 }), "m7(add11)", 2 },
        { bits ({ 0, 4, 7, 11, 6 }), "maj7#11", 1 }, { bits ({ 0, 4, 7, 10, 9 }), "13", 1 },
        { bits ({ 0, 4, 7, 10, 2, 9 }), "13", 0 },   { bits ({ 0, 4, 7, 11, 2, 9 }), "maj13", 1 },
        { bits ({ 0, 3, 7, 10, 2, 9 }), "m13", 1 },
    };

    const char* const kIntervals[12] = { "unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "4th",
                                         "tritone", "5th", "minor 6th", "major 6th", "minor 7th", "major 7th" };

    // the chord the held notes make: its name, and a line of what it is made of
    inline void name (const std::vector<int>& notes, juce::String& chord, juce::String& detail)
    {
        chord.clear();
        detail.clear();
        if (notes.empty())
            return;
        const int bass = *std::min_element (notes.begin(), notes.end());
        int mask = 0;
        for (int n : notes) mask |= 1 << (n % 12);
        juce::StringArray names;
        {
            std::vector<int> sorted (notes);
            std::sort (sorted.begin(), sorted.end());
            sorted.erase (std::unique (sorted.begin(), sorted.end()), sorted.end());
            for (int n : sorted) names.add (juce::String (kNotes[n % 12]) + juce::String (n / 12 - 1));
        }
        const int pcs = juce::countNumberOfBits ((juce::uint32) mask);

        if (pcs == 1)
        {
            chord = kNotes[bass % 12];
            detail = names.joinIntoString (" ");
            return;
        }

        // every root among the notes, every shape: the exact readings, best first
        int bestRank = 1 << 30;
        for (int root = 0; root < 12; ++root)
        {
            if (! (mask & (1 << root)))
                continue;
            const int rel = ((mask >> root) | (mask << (12 - root))) & 0xfff;
            for (const auto& s : kShapes)
                if (s.mask == rel)
                {
                    // a reading with the bass as its root wins over an inversion
                    const int rank = s.rank * 4 + (root == bass % 12 ? 0 : 3);
                    if (rank < bestRank)
                    {
                        bestRank = rank;
                        chord = juce::String (kNotes[root]) + s.suffix;
                        if (root != bass % 12)
                            chord << "/" << kNotes[bass % 12];
                    }
                }
        }

        if (chord.isEmpty())
        {
            if (pcs == 2)
            {
                // two notes: the interval above the bass
                int other = -1;
                for (int pc = 0; pc < 12; ++pc)
                    if ((mask & (1 << pc)) && pc != bass % 12) other = pc;
                const int iv = ((other - bass % 12) + 12) % 12;
                chord = juce::String (kNotes[bass % 12]) + " + " + kNotes[other];
                detail = juce::String (kIntervals[iv]) + "  -  " + names.joinIntoString (" ");
                return;
            }
            chord = "?";
        }
        detail = names.joinIntoString (" ") + "   bass " + kNotes[bass % 12];
    }
}

class ChordNameModule : public SynthModule
{
public:
    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    ChordNameModule() { for (auto& n : held) n.store (-1); }

    void reset() override { for (auto& n : held) n.store (-1); }
    void voiceReset (int v) override { held[(size_t) v].store (-1); }
    void voiceNoteOn (int v, int note, bool) override { held[(size_t) v].store (note); }
    void voiceNoteOff (int v) override { held[(size_t) v].store (-1); }
    void midiAllNotesOff() override { reset(); }
    void processVoiceSample (int, const StereoFrame*, StereoFrame*) override {}

    std::vector<int> heldNotes() const
    {
        std::vector<int> out;
        for (const auto& n : held)
        {
            const int v = n.load (std::memory_order_relaxed);
            if (v >= 0) out.push_back (v);
        }
        return out;
    }

    int preferredModuleWidth() const override { return 260; }
    int extraContentHeight() const override { return 96; }
    std::unique_ptr<juce::Component> createExtraContentComponent() override;

private:
    std::array<std::atomic<int>, kMaxVoices> held;
};

namespace
{
    class ChordNamePanel : public juce::Component, private juce::Timer
    {
    public:
        explicit ChordNamePanel (ChordNameModule& m) : module (&m) { startTimerHz (20); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            r = r.reduced (8.0f, 6.0f);
            const bool none = chord.isEmpty();
            // the last chord stays, faded, after the keys are let go
            g.setColour (juce::Colours::white.withAlpha (none ? 0.25f : 0.95f));
            g.setFont (juce::Font (juce::FontOptions (none ? 18.0f : 34.0f, juce::Font::bold)));
            g.drawFittedText (none ? (lastChord.isEmpty() ? juce::String ("Play a chord") : lastChord) : chord,
                              r.removeFromTop (r.getHeight() * 0.66f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
            g.setColour (juce::Colours::white.withAlpha (none ? 0.2f : 0.6f));
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawFittedText (none ? lastDetail : detail, r.toNearestInt(), juce::Justification::centred, 2, 0.7f);
        }

    private:
        void timerCallback() override
        {
            if (module == nullptr) return;
            auto notes = static_cast<ChordNameModule*> (module.get())->heldNotes();
            std::sort (notes.begin(), notes.end());
            if (notes == shown)
                return;
            shown = notes;
            if (chord.isNotEmpty()) { lastChord = chord; lastDetail = detail; }
            chordnames::name (notes, chord, detail);
            repaint();
        }

        juce::WeakReference<SynthModule> module;
        std::vector<int> shown;
        juce::String chord, detail, lastChord, lastDetail;
    };
}

std::unique_ptr<juce::Component> ChordNameModule::createExtraContentComponent()
{
    return std::make_unique<ChordNamePanel> (*this);
}

static ModuleDescriptor chordNameDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.chordname";
    d.displayName = "Chord Name";
    d.description =
        "Names what is being played: the keys held - or, patched, the notes on its MIDI In (a Piano Roll, an Arp, "
        "MIDI Add...) - as a chord, Cmaj7, F#m7b5, Gsus4/D, with its notes and bass below. Two notes read as their "
        "interval. Makes no sound.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 991;
    d.sockets = { midiIn ("midiIn", "MIDI In") };
    d.params = {};
    return d;
}

AQUANODE_REGISTER_MODULE (ChordNameModule, chordNameDescriptor)
