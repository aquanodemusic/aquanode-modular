//==============================================================================
// Super MIDI (6.9.16) - unison for any synth: each note played as several
// slightly detuned copies, the way the Super oscillator stacks its saws.
//
// It sits between the keys (or a piano roll) and a synth's MIDI In or Add MIDI
// In. With the MPE switch on, every note leaves as Copies notes, each an MPE
// note of its own: on the nearest key nobody else holds, bent back to the
// played pitch and on by its copy's detune. Each copy is a voice of its own in
// the synth (a voice per copy and note - seven copies of a three-note chord
// take 21 voices), so its own bend detunes it. With the MPE switch off nothing
// changes: the notes go through as they came. The card says so, always.
//
// The spread: seven copies as bars (across: detune, up: loudness, as velocity),
// the Super oscillator's classic layout to start with; drag them, or set them
// Classic, Even or Random. Copies picks how many sound, from the middle one
// out. Detune widens them all (the Super oscillator's curve: gentle over most
// of the knob, steep at the top); Mix sets the side copies against the middle
// one (its published curves). Expression (bend, Pressure, Slide) goes on to
// every copy of its note.
//
// Input: MIDI In. Output: MIDI Out (MPE notes: into what plays each note's own
// bend).
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/LookPalette.h"

using namespace aquanode;

namespace supermidi
{
    constexpr int kCopies = 7, kCentre = 3;
    // the Super oscillator's classic layout (each copy's offset relative to the note)
    constexpr float kClassicOffsets[kCopies] = { -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f };
    constexpr float kMaxOffset = 0.11f;
    const char* const kIds[kCopies][2] = { { "c1d", "c1l" }, { "c2d", "c2l" }, { "c3d", "c3l" }, { "c4d", "c4l" }, { "c5d", "c5l" }, { "c6d", "c6l" }, { "c7d", "c7l" } };
    // which copies sound for Copies = n: the middle one, then out in pairs
    constexpr int kOrder[kCopies] = { 3, 2, 4, 1, 5, 0, 6 };
    inline bool copyUsed (int k, int copies)
    {
        for (int i = 0; i < juce::jlimit (1, kCopies, copies); ++i)
            if (kOrder[i] == k) return true;
        return false;
    }
    inline float detuneCurve (float x) { x = juce::jlimit (0.0f, 1.0f, x); return 0.15f * x + 0.35f * x * x * x + 0.5f * std::pow (x, 8.0f); }
    inline float centreGain (float m) { return -0.55366f * m + 0.99785f; }
    inline float sideGain (float m) { return -0.73764f * m * m + 1.2841f * m + 0.044372f; }
}

using namespace supermidi;

class SuperMidiModule final : public SynthModule
{
public:
    enum P { pCopies = 0, pDetune, pMix };   // (then the seven copies' detune and level, hidden)

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.supermidi";
        d.displayName = "Super MIDI";
        d.searchKeywords = "super unison supersaw detune copies stack mpe";
        d.description = "Unison for any synth: each note played as several slightly detuned copies, as the Super oscillator "
                        "stacks its saws. This module needs MPE on to work: with the MPE switch on, every copy is an MPE note "
                        "of its own (a voice of its own in the synth, bent to its detune); with it off the notes go through "
                        "as they came. Into a synth's MIDI In or Add MIDI In (one that plays each note's own bend). The "
                        "spread: seven copies as bars - across their detune, up their loudness (as velocity) - starting as "
                        "the Super oscillator's classic layout; drag them, or set them Classic, Even or Random. Copies: how "
                        "many sound, from the middle one out (a voice each, per note). Detune widens them all, Mix sets the "
                        "side copies against the middle one.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 21;
        d.sockets = { midiIn ("midiIn", "MIDI In"), midiOut ("midiOut", "MIDI Out") };
        d.params.push_back (makeRotary ("copies", "Copies", 1.0f, (float) kCopies, 5.0f, 0, {}, false, 1.0f).noMod());
        d.params.push_back (makeRotary ("detune", "Detune", 0.0f, 100.0f, 40.0f, 0, "%").noMod());
        d.params.push_back (makeRotary ("mix", "Mix", 0.0f, 100.0f, 65.0f, 0, "%").noMod());
        for (int k = 0; k < kCopies; ++k)
        {
            d.params.push_back (makeRotary (kIds[k][0], "Copy " + juce::String (k + 1) + " Detune", -1.0f, 1.0f, kClassicOffsets[k] / kMaxOffset, 9).hide().noMod());
            d.params.push_back (makeRotary (kIds[k][1], "Copy " + juce::String (k + 1) + " Level", 0.0f, 1.0f, 1.0f, 9).hide().noMod());
        }
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    // what it sends replaces the keys at either MIDI input (the middle copy is the note itself)
    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiProcessor() const override { return true; }
    bool acceptsMpeEvents() const override { return true; }
    bool emitsMpeMidi() const override { return true; }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 186; }

    bool mpeOn() const { return midiControls != nullptr && midiControls->mpeSwitch; }

    // a copy's offset from the note, in semitones, at the present Detune
    float copySemitones (int k) const
    {
        const float pos = juce::jlimit (-1.0f, 1.0f, getParameterBase (paramIndex (kIds[k][0])));
        const float ratio = 1.0f + pos * detuneCurve (getParameterBase (pDetune) * 0.01f) * kMaxOffset;
        return 12.0f * std::log2 (juce::jmax (0.5f, ratio));
    }
    float copyGain (int k) const
    {
        const float lv = juce::jlimit (0.0f, 1.0f, getParameterBase (paramIndex (kIds[k][1])));
        const float m = getParameterBase (pMix) * 0.01f;
        return lv * juce::jlimit (0.0f, 1.0f, k == kCentre ? centreGain (m) : sideGain (m));
    }
    int copies() const { return juce::jlimit (1, kCopies, (int) std::lround (getParameterBase (pCopies))); }
    float centsAtEdge() const { return 1200.0f * std::log2 (1.0f + detuneCurve (getParameterBase (pDetune) * 0.01f) * kMaxOffset); }

    // the card (message thread)
    void setDesign (int kind)   // 0 Classic, 1 Even, 2 Random
    {
        juce::Random rnd;
        for (int k = 0; k < kCopies; ++k)
        {
            float pos = kind == 0 ? kClassicOffsets[k] / kMaxOffset : (float) (k - kCentre) / (float) kCentre;
            float lv = 1.0f;
            if (kind == 2)
            {
                pos = k == kCentre ? 0.0f : (float) (k - kCentre) / (float) kCentre * (0.5f + 0.5f * rnd.nextFloat());
                lv = 0.45f + 0.55f * rnd.nextFloat();
            }
            setParameter (kIds[k][0], pos);
            setParameter (kIds[k][1], lv);
        }
    }

    //=== the notes (audio thread) ================================================
    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent* out, int maxOut) override
    {
        if (maxOut <= 0) return 0;
        const int key = juce::jlimit (0, 127, in.note);
        auto& h = held[(size_t) key];

        if (in.isExpression)
        {
            if (! h.on) return 0;
            if (! h.split) { out[0] = in; return 1; }
            int n = 0;
            for (int c = 0; c < h.count && n < maxOut; ++c)
            {
                auto e = in;
                e.note = h.outKey[c];
                e.mpe = true;
                e.bend = in.bend + h.offset[c];
                out[n++] = e;
            }
            return n;
        }

        int n = 0;
        if (h.on)   // (a note-off, or this key again: what it played ends first)
        {
            n = endNote (h, out, maxOut);
            if (! in.isOn) return n;
        }
        else if (! in.isOn)
        {
            out[0] = in;   // (a note this module did not see start: on as it came)
            return 1;
        }

        h.on = true;
        h.split = mpeOn();
        if (! h.split)
        {
            h.count = 1;
            h.outKey[0] = key;
            h.offset[0] = 0.0f;
            used[(size_t) key] = true;
            if (n < maxOut) out[n++] = in;
            return n;
        }

        h.count = 0;
        const int nCopies = copies();
        for (int i = 0; i < nCopies && n < maxOut; ++i)
        {
            const int k = kOrder[i];
            const float g = copyGain (k);
            if (g < 0.01f && k != kCentre) continue;   // (a silent copy: no voice for it)
            const int outKey = freeKeyNear (key);
            if (outKey < 0) break;
            used[(size_t) outKey] = true;
            const int c = h.count++;
            h.outKey[c] = outKey;
            h.offset[c] = (float) (key - outKey) + copySemitones (k);
            auto e = in;
            e.note = outKey;
            e.mpe = true;
            e.bend = (in.mpe ? in.bend : 0.0f) + h.offset[c];
            e.velocity = juce::jlimit (0.01f, 1.0f, in.velocity * juce::jmax (0.05f, g));
            out[n++] = e;
        }
        if (h.count == 0) h.on = false;
        return n;
    }

    void midiAllNotesOff() override
    {
        for (auto& h : held) h = {};
        std::fill (used.begin(), used.end(), false);
    }
    void reset() override { midiAllNotesOff(); }

private:
    struct Held
    {
        bool on { false }, split { false };
        int count { 0 };
        int outKey[kCopies] {};
        float offset[kCopies] {};
    };

    int endNote (Held& h, MidiDriverEvent* out, int maxOut)
    {
        int n = 0;
        for (int c = 0; c < h.count; ++c)
        {
            used[(size_t) h.outKey[c]] = false;
            if (n >= maxOut) continue;
            MidiDriverEvent off;
            off.note = h.outKey[c];
            off.velocity = 0.0f;
            off.isOn = false;
            off.mpe = h.split;
            out[n++] = off;
        }
        h.on = false;
        h.count = 0;
        return n;
    }

    // the key nearest the note that no copy holds (a copy is bent back from
    // its key to the note, so any free key will do)
    int freeKeyNear (int key) const
    {
        for (int d = 0; d < 128; ++d)
        {
            if (key + d <= 127 && ! used[(size_t) (key + d)]) return key + d;
            if (d > 0 && key - d >= 0 && ! used[(size_t) (key - d)]) return key - d;
        }
        return -1;
    }

    std::array<Held, 128> held {};
    std::array<bool, 128> used {};
};

//==============================================================================
// The card: the seven copies (the Super oscillator's spread view, one design)
//==============================================================================
namespace
{
    class SpreadView final : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit SpreadView (SuperMidiModule& m) : module (&m)
        {
            for (auto* b : { &classic, &even, &random })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d2433));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
            }
            auto design = [this] (int kind) { if (auto* s = mod()) { s->setDesign (kind); changed(); } };
            classic.onClick = [design] { design (0); };
            even.onClick = [design] { design (1); };
            random.onClick = [design] { design (2); };
            classic.setTooltip ("The Super oscillator's classic layout");
            even.setTooltip ("Seven copies evenly spread");
            random.setTooltip ("A new random spread");
            startTimerHz (20);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            note = r.removeFromTop (16).reduced (6, 0);   // (its own line: always readable)
            auto row = r.removeFromTop (26).reduced (4, 3);
            classic.setBounds (row.removeFromLeft (54)); row.removeFromLeft (2);
            even.setBounds (row.removeFromLeft (44)); row.removeFromLeft (2);
            random.setBounds (row.removeFromLeft (56));
        }

        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedTop (46.0f).reduced (10.0f, 4.0f).withTrimmedBottom (14.0f); }
        float xOf (float pos) const { const auto p = plot(); return p.getCentreX() + pos * p.getWidth() * 0.47f; }
        float yOf (float lv) const { const auto p = plot(); return p.getBottom() - lv * p.getHeight(); }

        void paint (juce::Graphics& g) override
        {
            auto* s = mod();
            if (s == nullptr) return;
            const auto look = LookPalette::current (juce::Colour (0xff7fb0ff), ThemeColours::Utility);
            look.fillBackground (g, getLocalBounds().toFloat());
            // always: what it needs
            const bool on = s->mpeOn();
            g.setColour (on ? juce::Colours::white.withAlpha (0.75f) : juce::Colour (0xffffc65a));
            g.setFont (juce::Font (juce::FontOptions (11.0f, on ? juce::Font::plain : juce::Font::bold)));
            g.drawFittedText ("This module needs MPE on to work", note, juce::Justification::centred, 1, 0.8f);

            const auto p = plot();
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (p.expanded (6.0f, 3.0f), 4.0f);
            const float edge = s->centsAtEdge();
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float f : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            {
                const float x = xOf (f);
                g.setColour (juce::Colours::white.withAlpha (f == 0.0f ? 0.2f : 0.08f));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText ((f > 0 ? "+" : "") + juce::String ((int) std::lround (f * edge)) + " ct",
                            juce::Rectangle<float> (x - 22.0f, p.getBottom() + 2.0f, 44.0f, 11.0f), juce::Justification::centred, false);
            }
            const int nCopies = s->copies();
            const auto col = juce::Colour (0xff5fd3ff);
            for (int k = 0; k < kCopies; ++k)
            {
                const bool sounding = copyUsed (k, nCopies);
                const float pos = s->getParameter (kIds[k][0]), lv = s->getParameter (kIds[k][1]);
                const float x = xOf (pos), w = 10.0f;
                const auto c = col.withAlpha (sounding ? 0.95f : 0.3f);
                g.setColour (c.withAlpha (sounding ? 0.25f : 0.08f));
                g.fillRect (x - w * 0.5f, yOf (lv), w, p.getBottom() - yOf (lv));
                g.setColour (c);
                g.drawLine (x, yOf (lv), x, p.getBottom(), 2.0f);
                g.fillEllipse (x - 5.0f, yOf (lv) - 5.0f, 10.0f, 10.0f);
                g.setColour (juce::Colours::black);
                g.drawText (juce::String (k + 1), juce::Rectangle<float> (x - 5.0f, yOf (lv) - 5.0f, 10.0f, 10.0f), juce::Justification::centred, false);
            }
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawText (juce::String (nCopies) + (nCopies == 1 ? " copy" : " copies"),
                        juce::Rectangle<float> (p.getX() + 2.0f, p.getY() + 1.0f, 80.0f, 14.0f), juce::Justification::centredLeft, false);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* s = mod();
            dragCopy = -1;
            if (s == nullptr || e.position.y < 44.0f) return;
            float best = 16.0f;
            for (int k = 0; k < kCopies; ++k)
            {
                const float d = std::abs (e.position.x - xOf (s->getParameter (kIds[k][0])));
                if (d < best) { best = d; dragCopy = k; }
            }
            mouseDrag (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* s = mod();
            if (s == nullptr || dragCopy < 0) return;
            const auto p = plot();
            s->setParameter (kIds[dragCopy][0], juce::jlimit (-1.0f, 1.0f, (e.position.x - p.getCentreX()) / (p.getWidth() * 0.47f)));
            s->setParameter (kIds[dragCopy][1], juce::jlimit (0.0f, 1.0f, (p.getBottom() - e.position.y) / p.getHeight()));
            changed();
        }
        void mouseUp (const juce::MouseEvent&) override { dragCopy = -1; }

    private:
        SuperMidiModule* mod() const { return dynamic_cast<SuperMidiModule*> (module.get()); }
        void changed() { if (paramValuesChangedByContent) paramValuesChangedByContent(); repaint(); }
        void timerCallback() override
        {
            auto* s = mod();
            if (s == nullptr) return;
            juce::String now;
            for (int k = 0; k < kCopies; ++k) now << s->getParameter (kIds[k][0]) << ',' << s->getParameter (kIds[k][1]) << ',';
            now << s->getParameter ("detune") << ',' << s->getParameter ("copies") << ',' << (int) s->mpeOn();
            if (now != shown) { shown = now; repaint(); }
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton classic { "Classic" }, even { "Even" }, random { "Random" };
        juce::Rectangle<int> note;
        int dragCopy { -1 };
        juce::String shown;
    };
}

std::unique_ptr<juce::Component> SuperMidiModule::createExtraContentComponent()
{
    return std::make_unique<SpreadView> (*this);
}

AQUANODE_REGISTER_MODULE (SuperMidiModule, SuperMidiModule::descriptor)
