//==============================================================================
// Soft Pad (6.7.10) - a pad for the mood, more felt than heard.
//
// Sines first: every note is three sines a few cents apart (Detune), each
// drifting on its own slow wander (Drift), the outer two to the left and the
// right. A whisper of a soft home organ (Organ: the octave, twelfth and
// fifteenth above, as low drawbars, with a slow chorale shimmer) and of an
// electric piano's tine (Tine: each key starts with a soft struck body and a
// faint bell before the pad swells in). Swell and Release are its own (as a
// pad's swell is part of what it is); an envelope in Env In replaces them,
// voice by voice. Below ~120 Hz and above Tone (3.2 kHz) it stays quiet, and
// its level sits well under the other sound sources: it sets a mood.
//
// It plays chords by itself: the keyboard on the card shows the chord each
// key plays - the key and up to five more notes (Note 1-5, -24..+24, 0 off),
// ready-made in the Chord menu, leaning on stacked fourths and the
// pentatonic: 6/9 in Fourths, Fourths, So What, Fifths, Pentatonic, Minor
// Pentatonic, Sus2 Open, Quartal Around, Single. Visual (Reference Only)
// picks the note the keyboard shows it on.
// PerVoice. Inputs: Add Midi In, Env In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

namespace softpad
{
    constexpr int kSlots = 5;
    constexpr int kNotes = kSlots + 1;
    const juce::StringArray kChordNames { "6/9 in Fourths", "Fourths", "So What", "Fifths", "Pentatonic", "Minor Pentatonic",
                                          "Sus2 Open", "Quartal Around", "Single", "Custom" };
    constexpr int kNumChords = 9, kCustom = 9;
    const float kChords[kNumChords][kSlots] = {
        { 4, 9, 14, 19, 0 },     // 6/9 in fourths: the key, then E A D G above C - pentatonic and quartal at once
        { 5, 10, 15, 0, 0 },     // stacked fourths
        { 5, 10, 15, 19, 0 },    // three fourths and a major third
        { 7, 14, 21, 0, 0 },     // stacked fifths
        { 2, 4, 7, 9, 0 },       // the major pentatonic, close
        { 3, 5, 7, 10, 0 },      // the minor pentatonic, close
        { 7, 14, 19, 0, 0 },     // root, fifth, ninth, twelfth
        { -10, -5, 5, 10, 0 },   // fourths below and above
        { 0, 0, 0, 0, 0 } };     // the key alone

    struct Ph   // a sine as a turning phasor (cheap: no sin() per sample)
    {
        float c { 1.0f }, s { 0.0f }, rc { 1.0f }, rs { 0.0f };
        void set (double w) noexcept { rc = (float) std::cos (w); rs = (float) std::sin (w); }
        float step() noexcept { const float nc = c * rc - s * rs; s = s * rc + c * rs; c = nc; return s; }
        void renorm() noexcept { const float m = 1.0f / std::sqrt (c * c + s * s); c *= m; s *= m; }
        void start (float phase) noexcept { c = std::cos (phase); s = std::sin (phase); }
    };

    juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }
}

class SoftPadModule final : public SynthModule
{
public:
    enum P { pChord = 0, pVisual, pNote1, pSwell = pNote1 + softpad::kSlots, pRelease, pOrgan, pTine, pDrift, pDetune, pTone,
             pVelocity, pLevel };

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    double voiceTailSeconds() const override { return juce::jmax (0.2, (double) getParameterBase (pRelease)) * 1.6; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < kMaxVoices; ++v) voiceReset (v); }
    void voiceReset (int v) override { voices[v] = {}; }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        auto& k = voices[v];
        const bool wasSounding = k.active && retrigger;
        k.active = true; k.gate = true; k.key = note;
        // the chord this key plays (read at note-on, as Graphic Midi Add does)
        k.count = 0;
        auto add = [&k] (int p) { if (p < 0 || p > 127) return; for (int i = 0; i < k.count; ++i) if (k.notes[i] == p) return; k.notes[k.count++] = p; };
        add (note);
        for (int s = 0; s < softpad::kSlots; ++s)
        {
            const int o = (int) std::lround (param (pNote1 + s));
            if (o != 0) add (note + o);
        }
        if (! wasSounding)
        {
            k.env = 0.0f; k.relEnv = 0.0f;
            for (int i = 0; i < k.count; ++i)
            {
                auto& n = k.osc[i];
                // every voice starts at its own phases (no beating bloom at the start)
                const juce::uint32 h = (juce::uint32) (note * 2654435761u + (juce::uint32) i * 40503u + ++counter * 977u);
                auto rnd = [&] (int salt) { return (float) ((h >> (salt * 3)) & 1023u) / 1023.0f; };
                for (int j = 0; j < 7; ++j) n.ph[j].start (rnd (j) * 6.2831853f);
                n.driftPhase = rnd (8);
                n.driftRate = 0.05f + 0.11f * rnd (9);
            }
        }
        k.tine = 1.0f; k.bell = 1.0f;
        k.update = 0;
    }
    void voiceVelocity (int v, float velocity01) override { voices[v].vel = velocity01; }
    void voiceNoteOff (int v) override { voices[v].gate = false; }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        auto& k = voices[v];
        if (! k.active) { outputs[0] = { 0.0f, 0.0f }; return; }
        const double sr = sampleRate;

        if (--k.update <= 0)
        {
            k.update = 32;
            const float drift = param (pDrift) * 0.01f * 9.0f;     // cents of wander
            const float det = param (pDetune);                     // cents between the three sines
            const double bend = pitchBendSemitones();
            for (int i = 0; i < k.count; ++i)
            {
                auto& n = k.osc[i];
                n.driftPhase += n.driftRate * 32.0f / (float) sr;
                if (n.driftPhase > 1.0f) n.driftPhase -= 1.0f;
                const float wob = drift * std::sin (6.2831853f * n.driftPhase);
                const double f0 = aquanode::midiNoteToHz ((double) k.notes[i] + bend);   // (patch tuning, 6.9.10)
                const double w0 = juce::MathConstants<double>::twoPi * f0 / sr;
                auto cents = [] (double c) { return std::pow (2.0, c / 1200.0); };
                n.ph[0].set (w0 * cents (-det + wob));
                n.ph[1].set (w0 * cents (wob * 0.4));
                n.ph[2].set (w0 * cents (det - wob * 0.7));
                for (int h = 0; h < 3; ++h) n.ph[3 + h].set (juce::jmin (w0 * (double) (h + 2), 3.0));   // 4', 2 2/3', 2' (as drawbars)
                n.ph[6].set (juce::jmin (w0 * 6.3, 3.0));                                                // the tine's bell
                for (auto& p : n.ph) p.renorm();
            }
            // the envelopes' rates, the filters
            k.swellK = 1.0f - std::exp (-1.0f / (juce::jmax (0.02f, param (pSwell)) * 0.3f * (float) sr));
            k.relK = 1.0f - std::exp (-1.0f / (juce::jmax (0.05f, param (pRelease)) * 0.3f * (float) sr));
            k.tineK = std::exp (-6.9f / (1.6f * (float) sr));
            k.bellK = std::exp (-6.9f / (0.35f * (float) sr));
            const float tone = juce::jlimit (400.0f, 0.45f * (float) sr, param (pTone));
            k.lpG = std::tan (juce::MathConstants<float>::pi * tone / (float) sr);
            k.hpA = std::exp (-6.2831853f * 120.0f / (float) sr);
            k.organ = param (pOrgan) * 0.01f;
            k.tineAmt = param (pTine) * 0.01f;
            k.lesliePh += 0.8f * 32.0f / (float) sr;    // (a slow chorale shimmer on the organ's part)
            if (k.lesliePh > 1.0f) k.lesliePh -= 1.0f;
            const float vs = param (pVelocity) * 0.01f;
            k.gain = param (pLevel) * (1.0f - vs + vs * std::pow (juce::jlimit (0.0f, 1.0f, k.vel), 1.5f))
                     / std::sqrt ((float) juce::jmax (1, k.count));
        }

        // the envelopes: Env In when patched, else the pad's own swell and release
        float amp, relAmp;
        if (isInputConnected (1))
        {
            amp = relAmp = juce::jmax (0.0f, inputs[1][0]);
            if (! k.gate && amp < 1.0e-4f && k.relEnv < 1.0e-4f) { k.active = false; outputs[0] = { 0.0f, 0.0f }; return; }
            k.relEnv = amp;
        }
        else
        {
            k.env += ((k.gate ? 1.0f : 0.0f) - k.env) * (k.gate ? k.swellK : k.relK);
            k.relEnv += ((k.gate ? 1.0f : 0.0f) - k.relEnv) * (k.gate ? 0.02f : k.relK);   // (instant on: the tine's)
            amp = k.env * k.env * (3.0f - 2.0f * k.env);   // (a softly rounded swell)
            relAmp = k.relEnv;
            if (! k.gate && k.env < 1.0e-4f && k.relEnv < 1.0e-4f) { k.active = false; outputs[0] = { 0.0f, 0.0f }; return; }
        }
        k.tine *= k.tineK; k.bell *= k.bellK;
        const float leslie = 1.0f + 0.18f * std::sin (6.2831853f * k.lesliePh);

        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < k.count; ++i)
        {
            auto& n = k.osc[i];
            const float a = n.ph[0].step(), b = n.ph[1].step(), c = n.ph[2].step();
            float padL = (a * 0.46f + b * 0.34f + c * 0.20f) * amp;
            float padR = (c * 0.46f + b * 0.34f + a * 0.20f) * amp;
            if (k.organ > 0.001f)
            {
                const float h = (n.ph[3].step() * 0.42f + n.ph[4].step() * 0.24f + n.ph[5].step() * 0.16f) * k.organ * 0.55f * amp;
                padL += h * leslie; padR += h * (2.0f - leslie);
            }
            if (k.tineAmt > 0.001f && k.tine > 1.0e-4f)
            {
                const float t = (b * k.tine * 0.75f + n.ph[6].step() * k.bell * 0.22f) * k.tineAmt * relAmp;
                padL += t; padR += t;
            }
            l += padL; r += padR;
        }
        l *= k.gain; r *= k.gain;

        // kept out of the way: a gentle high-pass at 120 Hz, a soft low-pass at Tone
        auto filt = [&k] (float x, int ch)
        {
            const float hp = k.hpA * (k.hpY[ch] + x - k.hpX[ch]);
            k.hpX[ch] = x; k.hpY[ch] = hp;
            // TPT one-pole pair (12 dB/oct)
            const float g = k.lpG / (1.0f + k.lpG);
            const float v1 = (hp - k.lp1[ch]) * g; const float y1 = v1 + k.lp1[ch]; k.lp1[ch] = y1 + v1;
            const float v2 = (y1 - k.lp2[ch]) * g; const float y2 = v2 + k.lp2[ch]; k.lp2[ch] = y2 + v2;
            return y2;
        };
        outputs[0] = { filt (l, 0), filt (r, 1) };
    }

    // picking a chord sets the Note knobs; turning one makes it Custom (or the chord it now matches)
    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        if (applying) return;
        const juce::ScopedValueSetter<bool> guard (applying, true);
        if (id == "chord")
        {
            const int c = (int) std::lround (value);
            if (c >= 0 && c < softpad::kNumChords)
                for (int i = 0; i < softpad::kSlots; ++i)
                    SynthModule::setParameter ("note" + juce::String (i + 1), softpad::kChords[c][i]);
        }
        else if (id.startsWith ("note"))
        {
            int match = softpad::kCustom;
            for (int c = 0; c < softpad::kNumChords && match == softpad::kCustom; ++c)
            {
                bool same = true;
                for (int i = 0; i < softpad::kSlots && same; ++i)
                    same = (int) std::lround (getParameterBase (pNote1 + i)) == (int) softpad::kChords[c][i];
                if (same) match = c;
            }
            SynthModule::setParameter ("chord", (float) match);
        }
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.softpad";
        d.displayName = "Soft Pad";
        d.searchKeywords = "pad ambient sine soft mood organ rhodes quartal pentatonic chord background";
        d.description =
            "A pad for the mood, more felt than heard: three drifting sines per note (Detune, Drift), a whisper of a soft "
            "home organ (Organ) and of an electric piano's tine at each key (Tine), its own slow Swell and Release (an "
            "envelope in Env In replaces them), quiet below 120 Hz and above Tone. It plays chords by itself - the card's "
            "keyboard shows the chord each key plays: the key and up to five more notes (Note 1-5), ready-made in Chord, "
            "leaning on stacked fourths and the pentatonic. Visual (Reference Only) picks the note the keyboard shows it on.";
        d.section = ModuleSection::Oscillator;
        d.sockets = { midiIn ("addMidiIn", "Add MIDI In"), modIn ("envIn", "Env In"), audioOut ("out", "Audio Out") };
        d.params.push_back (makeCombo ("chord", "Chord", softpad::kChordNames, 0, 0, 3).noMod());
        auto visual = makeRotary ("visual", "Visual (Reference Only)", 24.0f, 96.0f, 60.0f, 0, {}, false, 1.0f).noMod();
        visual.widthUnits = 3;
        visual.textFromValue = [] (double v) { return softpad::noteName (juce::jlimit (0, 127, (int) std::lround (v))); };
        d.params.push_back (visual);
        for (int i = 0; i < softpad::kSlots; ++i)
            d.params.push_back (makeRotary ("note" + juce::String (i + 1), "Note " + juce::String (i + 1),
                                            -24.0f, 24.0f, softpad::kChords[0][i], 1, "st", false, 1.0f));
        d.params.push_back (makeRotary ("swell",    "Swell",    0.02f, 10.0f, 2.4f, 2, "s", true, 0.01f));
        d.params.push_back (makeRotary ("release",  "Release",  0.05f, 12.0f, 3.5f, 2, "s", true, 0.01f));
        d.params.push_back (makeRotary ("organ",    "Organ",    0.0f, 100.0f, 22.0f, 2, "%", false, 1.0f));
        d.params.push_back (makeRotary ("tine",     "Tine",     0.0f, 100.0f, 18.0f, 2, "%", false, 1.0f));
        d.params.push_back (makeRotary ("drift",    "Drift",    0.0f, 100.0f, 35.0f, 3, "%", false, 1.0f));
        d.params.push_back (makeRotary ("detune",   "Detune",   0.0f, 25.0f, 7.0f, 3, "ct", false, 0.1f));
        d.params.push_back (makeRotary ("tone",     "Tone",     500.0f, 12000.0f, 3200.0f, 3, "Hz", true, 1.0f));
        d.params.push_back (makeRotary ("velocity", "Velocity", 0.0f, 100.0f, 35.0f, 3, "%", false, 1.0f));
        d.params.push_back (makeRotary ("level",    "Level",    0.0f, 1.0f, 0.5f, 3));
        d.params.push_back (makeOutputTrim (-15.0f));   // (well under the other sources: it sets a mood)
        return d;
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 96; }

    // for the keyboard (message thread)
    int visualNote() const { return juce::jlimit (0, 127, (int) std::lround (getParameterBase (pVisual))); }
    int offset (int slot) const { return (int) std::lround (getParameterBase (pNote1 + slot)); }

private:
    struct Osc { softpad::Ph ph[7]; float driftPhase { 0.0f }, driftRate { 0.1f }; };
    struct Voice
    {
        bool active { false }, gate { false };
        int key { 60 }, count { 0 }, update { 0 };
        int notes[softpad::kNotes] {};
        Osc osc[softpad::kNotes];
        float env { 0.0f }, relEnv { 0.0f }, tine { 0.0f }, bell { 0.0f }, vel { 0.8f };
        float swellK { 0.001f }, relK { 0.001f }, tineK { 0.999f }, bellK { 0.99f };
        float organ { 0.0f }, tineAmt { 0.0f }, gain { 0.0f }, lesliePh { 0.0f };
        float lpG { 0.2f }, hpA { 0.99f };
        float hpX[2] {}, hpY[2] {}, lp1[2] {}, lp2[2] {};
    };
    Voice voices[kMaxVoices];
    juce::uint32 counter { 0 };
    bool applying { false };
};

//==============================================================================
namespace
{
    // the chord a key plays, on a keyboard (as Graphic Midi Add shows it)
    class PadChordKeyboard final : public juce::Component, public aquanode::ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit PadChordKeyboard (SoftPadModule& m) : module (&m) { timerCallback(); startTimerHz (15); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            juce::ColourGradient bg (juce::Colour (0xff17202c), r.getX(), r.getY(), juce::Colour (0xff1f2a24), r.getRight(), r.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (r, 4.0f);
            if (module == nullptr) return;
            std::vector<int> notes { shown.root };
            for (int o : shown.offsets)
                if (o != 0 && shown.root + o >= 0 && shown.root + o <= 127
                    && std::find (notes.begin(), notes.end(), shown.root + o) == notes.end()) notes.push_back (shown.root + o);
            std::sort (notes.begin(), notes.end());
            const auto lit = juce::Colour (0xffa8f0d8), added = juce::Colour (0xffd8f6ff);
            auto in = r.reduced (6.0f, 3.0f);
            auto top = in.removeFromTop (14.0f);
            juce::StringArray names;
            for (int n : notes) names.add (softpad::noteName (n));
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawFittedText (names.joinIntoString ("  "), top.toNearestInt(), juce::Justification::centred, 1, 0.6f);

            const int first = 12 * (notes.front() / 12);
            int last = juce::jmin (127, 12 * (notes.back() / 12) + 11);
            if (last - first + 1 < 24) last = juce::jmin (127, first + 23);
            auto isBlack = [] (int n) { const int pc = n % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
            int whites = 0;
            for (int n = first; n <= last; ++n) if (! isBlack (n)) ++whites;
            auto area = in.reduced (0.0f, 2.0f);
            const float kw = area.getWidth() / (float) juce::jmax (1, whites), kh = area.getHeight();
            auto fillFor = [&] (int n) -> juce::Colour
            {
                if (n == shown.root) return lit;
                if (std::find (notes.begin(), notes.end(), n) != notes.end()) return added.withAlpha (0.85f);
                return juce::Colours::transparentBlack;
            };
            std::vector<std::pair<int, juce::Rectangle<float>>> blacks;
            int wi = 0;
            for (int n = first; n <= last; ++n)
            {
                if (isBlack (n)) { const float x = area.getX() + (float) wi * kw; blacks.push_back ({ n, { x - kw * 0.32f, area.getY(), kw * 0.64f, kh * 0.6f } }); continue; }
                const auto key = juce::Rectangle<float> (area.getX() + (float) wi * kw, area.getY(), kw, kh).reduced (0.4f, 0.0f);
                const auto fill = fillFor (n);
                g.setColour (fill.isTransparent() ? juce::Colour (0xffd6dde2) : fill);
                g.fillRoundedRectangle (key, 1.2f);
                ++wi;
            }
            for (auto& [n, key] : blacks)
            {
                const auto fill = fillFor (n);
                g.setColour (juce::Colour (0xff24292e));
                g.fillRoundedRectangle (key, 1.0f);
                if (! fill.isTransparent()) { g.setColour (fill); g.fillRoundedRectangle (key.reduced (0.8f), 0.8f); }
            }
        }

    private:
        struct Shown
        {
            int root { 60 };
            std::array<int, softpad::kSlots> offsets {};
            bool operator!= (const Shown& o) const { return root != o.root || offsets != o.offsets; }
        };
        void timerCallback() override
        {
            auto* m = dynamic_cast<SoftPadModule*> (module.get());
            if (m == nullptr) return;
            Shown now;
            now.root = m->visualNote();
            for (int s = 0; s < softpad::kSlots; ++s) now.offsets[(size_t) s] = m->offset (s);
            if (now != shown)
            {
                const bool moved = now.offsets != shown.offsets;
                shown = now;
                repaint();
                if (moved && paramValuesChangedByContent != nullptr) paramValuesChangedByContent();
            }
        }
        juce::WeakReference<SynthModule> module;
        Shown shown;
    };
}

std::unique_ptr<juce::Component> SoftPadModule::createExtraContentComponent()
{
    return std::make_unique<PadChordKeyboard> (*this);
}

AQUANODE_REGISTER_MODULE (SoftPadModule, SoftPadModule::descriptor)
