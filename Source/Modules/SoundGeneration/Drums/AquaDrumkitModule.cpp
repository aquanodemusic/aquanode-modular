//==============================================================================
// Aqua Drumkit (6.9.15) - the drum modules' nine engines on a 3 x 3 kit:
// Kick (Classic, Snap Kick), Snare, Clap (Classic, Snappy, Clack), Hats
// (Classic, Snip) and the Modal Drum, one engine a pad. Each pad runs the very
// module it comes from (made inside the kit, its model fixed), so it sounds
// exactly like that module; every one of the module's knobs is the pad's, with
// Level and Pan beside them. Tap a pad: it plays and shows its knobs in the
// eight places (the ones a pad does not need greyed out). MIDI In plays the pads
// from Base Note up, in the pads' order (left to right, top row first).
// The pads ring out after their note ends, as the drum modules do.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

namespace
{
    struct PadKind { const char* type; int model; const char* name; };
    constexpr int kPads = 9, kSlots = 8;
    const PadKind kKinds[kPads] = {
        { "osc.kick", 0, "Kick" },       { "osc.kick", 1, "Snap Kick" },  { "osc.snare", -1, "Snare" },
        { "osc.clap", 0, "Clap" },       { "osc.clap", 1, "Snappy Clap" }, { "osc.clap", 2, "Clack" },
        { "osc.hats", 0, "Hats" },       { "osc.hats", 1, "Snip Hats" },   { "osc.modaldrum", -1, "Modal Drum" } };

    // the knobs a pad's module shows (not its voices, model or calibration)
    std::vector<const ParamSpec*> padKnobs (const ModuleDescriptor& d)
    {
        std::vector<const ParamSpec*> out;
        for (const auto& p : d.params)
            if (! p.hidden && p.id != "voices" && p.id != "model" && p.type != ParamType::Button)
                out.push_back (&p);
        return out;
    }

    const ModuleDescriptor* kindDescriptor (int pad)
    {
        if (auto* r = ModuleFactory::instance().find (kKinds[pad].type)) return &r->descriptor;
        return nullptr;
    }

    juce::String pid (int pad, const juce::String& id) { return "p" + juce::String (pad + 1) + "_" + id; }
}

class AquaDrumkitModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.aquadrumkit";
        d.displayName = "Aqua Drumkit";
        d.description = "The drum modules' nine engines on a 3 x 3 kit: Kick and Snap Kick, Snare, Clap, Snappy Clap and Clack, "
                        "Hats and Snip Hats, and the Modal Drum (its Tune sets its pitch here). Each pad is the very module it "
                        "comes from, with all of its knobs, and Level and Pan. Tap a pad: it plays and shows its knobs (the "
                        "places it does not need are greyed out). MIDI In plays the pads from Base Note up, left to right, the "
                        "top row first; the hits ring out after the note ends, as the drums do.";
        d.section = ModuleSection::Oscillator;
        d.searchKeywords = "drum kit drumkit pads kick snare clap hats hihat modal drum";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
        d.params.push_back (makeSteppedList ("base", "Base Note", midiNoteNameChoices(), 36, 0).noMod());
        d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.8f, 0));
        return d;
    }

    // each pad's knobs, hidden (the card's pads show them): its module's, then Level and Pan.
    // (6.9.16) added when the first kit is made, not at registration: the drum modules are
    // all registered by then, whatever order the program started them in - before, the
    // pads whose module came later (Hats, Snip Hats, the Modal Drum) had no knobs.
    static void completeDescriptor (ModuleDescriptor& d)
    {
        for (int p = 0; p < kPads; ++p)
        {
            if (auto* kd = kindDescriptor (p))
                for (auto* spec : padKnobs (*kd))
                {
                    auto s = *spec;
                    s.id = pid (p, spec->id);
                    s.label = juce::String (kKinds[p].name) + " " + spec->label;
                    s.hidden = true;
                    s.row = 9;
                    d.params.push_back (s);
                }
            auto lv = makeRotary (pid (p, "level"), juce::String (kKinds[p].name) + " Level", 0.0f, 1.0f, 0.8f, 9);
            lv.hidden = true; lv.label = "Level";
            d.params.push_back (lv);
            auto pn = makeRotary (pid (p, "pan"), juce::String (kKinds[p].name) + " Pan", -1.0f, 1.0f, 0.0f, 9);
            pn.hidden = true;
            d.params.push_back (pn);
        }
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    int preferredModuleWidth() const override { return 440; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        ensurePads();
        for (auto& p : pads)
            if (p.engine != nullptr)
                p.engine->prepare (sr);
        pushKnobs (true);
    }

    void reset() override
    {
        for (auto& p : pads)
            if (p.engine != nullptr) p.engine->reset();
    }

    void blockStart() override
    {
        ensurePads();
        pushKnobs (false);
        for (auto& p : pads)
            if (p.engine != nullptr) p.engine->blockStart();
        if (const int pv = previewPad.exchange (-1); pv >= 0)
            hit (pv, 0.85f, -1);
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (! m.isNoteOnOrOff()) return;
        const int pad = m.getNoteNumber() - (int) std::lround (param (pBase));
        if (pad < 0 || pad >= kPads) return;
        if (m.isNoteOn()) { hit (pad, m.getFloatVelocity(), m.getNoteNumber()); lastPlayed.store (pad); }
        else release (pad, m.getNoteNumber());
    }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        float l = 0.0f, r = 0.0f;
        StereoFrame in[8] {}, out[8] {};
        for (int i = 0; i < kPads; ++i)
        {
            auto& p = pads[(size_t) i];
            if (p.engine == nullptr) continue;
            for (auto& o : out) o = { 0.0f, 0.0f };
            p.engine->processSample (in, out);
            const float g = p.gain;   // (as loud as the module itself plays from a key)
            l += out[0][0] * g * p.panL;
            r += out[0][1] * g * p.panR;
        }
        const float lv = param (pLevel);
        outputs[0] = { l * lv, r * lv };
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 390; }

    // for the card (message thread)
    int selected() const { return selectedPad.load(); }
    void select (int pad, bool play) { selectedPad.store (juce::jlimit (0, kPads - 1, pad)); if (play) previewPad.store (pad); }
    int lastHit() const { return lastPlayed.load(); }
    static const char* padName (int pad) { return kKinds[juce::jlimit (0, kPads - 1, pad)].name; }
    // the pad's knobs: its module's, then Level and Pan
    static juce::StringArray slotIds (int pad)
    {
        juce::StringArray ids;
        if (auto* kd = kindDescriptor (pad))
            for (auto* s : padKnobs (*kd)) ids.add (pid (pad, s->id));
        ids.add (pid (pad, "level"));
        ids.add (pid (pad, "pan"));
        return ids;
    }

    juce::String saveCustomState() const override { return juce::String (selectedPad.load()); }
    void loadCustomState (const juce::String& s) override { selectedPad.store (juce::jlimit (0, kPads - 1, s.getIntValue())); }

private:
    enum { pBase = 0, pLevel = 1 };

    struct Pad
    {
        std::unique_ptr<SynthModule> engine;
        std::vector<std::pair<int, int>> map;   // (the kit's knob, the engine's knob)
        int levelIdx { -1 }, panIdx { -1 };
        float gain { 0.8f }, panL { 1.0f }, panR { 1.0f };
        int voiceOfNote[128];
        int nextVoice { 0 };
    };

    void ensurePads()
    {
        if (padsMade) return;
        padsMade = true;
        for (int i = 0; i < kPads; ++i)
        {
            auto& p = pads[(size_t) i];
            std::fill (std::begin (p.voiceOfNote), std::end (p.voiceOfNote), -1);
            p.engine = ModuleFactory::instance().createInstance (kKinds[i].type);
            if (p.engine == nullptr) continue;
            const auto& ed = p.engine->getDescriptor();
            if (kKinds[i].model >= 0) p.engine->setParameter ("model", (float) kKinds[i].model);
            for (auto* s : padKnobs (ed))
            {
                const int mine = paramIndex (pid (i, s->id)), theirs = p.engine->paramIndex (s->id);
                if (mine >= 0 && theirs >= 0) p.map.push_back ({ mine, theirs });
            }
            p.levelIdx = paramIndex (pid (i, "level"));
            p.panIdx = paramIndex (pid (i, "pan"));
            if (sampleRate > 0.0) p.engine->prepare (sampleRate);
        }
    }

    void pushKnobs (bool all)
    {
        for (auto& p : pads)
        {
            if (p.engine == nullptr) continue;
            for (const auto& [mine, theirs] : p.map)
            {
                const float v = param (mine);
                if (all || ! juce::exactlyEqual (p.engine->getParameterBase (theirs), v))
                    p.engine->setParameterByIndex (theirs, v);
            }
            p.gain = p.levelIdx >= 0 ? param (p.levelIdx) : 0.8f;
            const float pan = p.panIdx >= 0 ? juce::jlimit (-1.0f, 1.0f, param (p.panIdx)) : 0.0f;
            p.panL = std::sqrt (2.0f) * std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
            p.panR = std::sqrt (2.0f) * std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
        }
    }

    void hit (int pad, float velocity, int note)
    {
        auto& p = pads[(size_t) pad];
        if (p.engine == nullptr) return;
        const int v = p.nextVoice;
        p.nextVoice = (p.nextVoice + 1) % (kMaxVoices - 1);
        // (the Modal Drum: at its Tune - the kit's key does not transpose it)
        p.engine->globalMidiNoteOn (v, 60, juce::jlimit (0.0f, 1.0f, velocity));
        if (note >= 0) { if (p.voiceOfNote[note] >= 0) p.engine->globalMidiNoteOff (p.voiceOfNote[note]); p.voiceOfNote[note] = v; }
        else p.engine->globalMidiNoteOff (v);   // (a tap on the card: a one-shot)
    }

    void release (int pad, int note)
    {
        auto& p = pads[(size_t) pad];
        if (p.engine == nullptr || p.voiceOfNote[note] < 0) return;
        p.engine->globalMidiNoteOff (p.voiceOfNote[note]);
        p.voiceOfNote[note] = -1;
    }

    std::array<Pad, kPads> pads;
    bool padsMade { false };
    std::atomic<int> selectedPad { 0 }, previewPad { -1 }, lastPlayed { -1 };
};

//==============================================================================
// the card: the nine pads, the selected one's eight places
//==============================================================================
namespace
{
    // a knob as the module cards make it: its range, skew and value text from the knob's spec
    void setUpKnob (juce::Slider& s, const ParamSpec& spec)
    {
        s.textFromValueFunction = nullptr;
        s.valueFromTextFunction = nullptr;
        s.setTextValueSuffix ({});
        s.setRange (spec.minValue, spec.maxValue, spec.interval);
        if (spec.logarithmic && spec.minValue > 0.0f)
            s.setSkewFactorFromMidPoint (std::sqrt ((double) spec.minValue * spec.maxValue));
        else
            s.setSkewFactor (1.0);
        if (spec.type == ParamType::RotarySteppedList)
        {
            const auto choices = spec.choices;
            s.textFromValueFunction = [choices] (double v) { return choices[juce::jlimit (0, choices.size() - 1, (int) std::round (v))]; };
        }
        else if (spec.unitSuffix == "Hz")
            s.textFromValueFunction = [] (double v)
            {
                if (v >= 10000.0) return juce::String (v / 1000.0, 1) + "k";
                if (v >= 1000.0)  return juce::String (v / 1000.0, 2) + "k";
                return juce::String (v, v < 10.0 ? 2 : (v < 100.0 ? 1 : 0)) + " Hz";
            };
        else
        {
            const float range = spec.maxValue - spec.minValue;
            const int dp = spec.interval == 1.0f || range > 1000.0f ? 0 : range > 20.0f ? 1 : 2;
            const juce::String suffix = spec.unitSuffix.isNotEmpty() ? " " + spec.unitSuffix : juce::String();
            s.textFromValueFunction = [dp, suffix] (double v) { return juce::String (v, dp) + suffix; };
        }
        if (spec.textFromValue) s.textFromValueFunction = spec.textFromValue;
        s.setDoubleClickReturnValue (true, spec.defaultValue);
    }

    class DrumkitEditor final : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit DrumkitEditor (AquaDrumkitModule& m) : weak (&m), raw (&m)
        {
            for (int i = 0; i < kSlots; ++i)
            {
                auto& s = sliders[(size_t) i];
                s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
                s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
                s.setColour (juce::Slider::rotarySliderFillColourId, accent);
                s.onValueChange = [this, i] { slotMoved (i); };
                addAndMakeVisible (s);
                auto& lb = labels[(size_t) i];
                lb.setJustificationType (juce::Justification::centred);
                lb.setFont (juce::Font (juce::FontOptions (10.5f)));
                lb.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (lb);
            }
            refreshSlots();
            startTimerHz (15);
        }

        // as the 808 and 909 cards: the selected pad's name, its knobs, the pads below
        void resized() override
        {
            auto r = getLocalBounds();
            name = r.removeFromTop (22).reduced (4, 0);
            r.removeFromTop (4);
            constexpr int perRow = 4, rowH = 80;
            auto knobArea = r.removeFromTop (2 * rowH);
            const int kw = knobArea.getWidth() / perRow;
            for (int i = 0; i < kSlots; ++i)
            {
                auto cell = juce::Rectangle<int> (knobArea.getX() + (i % perRow) * kw, knobArea.getY() + (i / perRow) * rowH, kw, rowH);
                labels[(size_t) i].setBounds (cell.removeFromTop (15));
                sliders[(size_t) i].setBounds (cell.reduced (8, 0).withTrimmedBottom (3));
            }
            r.removeFromTop (6);
            padArea = r.toFloat();
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (AquaDrumkitModule::padName (m->selected()), name, juce::Justification::centredLeft);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText ("tap a pad: it plays", name, juce::Justification::centredRight);
            // the pads, left to right, the top row first (the order MIDI In plays them in)
            for (int i = 0; i < kPads; ++i)
            {
                const auto pr = padRect (i).reduced (3.0f);
                const bool sel = i == m->selected();
                const float fl = i == flashPad ? flashLevel : 0.0f;
                g.setColour (accent.withAlpha (0.22f + 0.6f * fl).interpolatedWith (juce::Colours::white, 0.4f * fl));
                g.fillRoundedRectangle (pr, 5.0f);
                g.setColour (sel ? juce::Colours::white : accent.withAlpha (0.7f));
                g.drawRoundedRectangle (pr, 5.0f, sel ? 2.0f : 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
                g.drawFittedText (AquaDrumkitModule::padName (i), pr.toNearestInt().reduced (4), juce::Justification::centred, 2);
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.setFont (juce::Font (juce::FontOptions (9.0f)));
                g.drawText (juce::String (i + 1), pr.toNearestInt().reduced (5, 3), juce::Justification::topLeft);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            for (int i = 0; i < kPads; ++i)
                if (padRect (i).contains (e.position))
                {
                    m->select (i, true);
                    flashPad = i; flashLevel = 0.6f;
                    refreshSlots();
                    repaint();
                    return;
                }
        }

    private:
        AquaDrumkitModule* mod() const { return weak.get() != nullptr ? raw : nullptr; }

        juce::Rectangle<float> padRect (int i) const
        {
            const float w = padArea.getWidth() / 3.0f, h = padArea.getHeight() / 3.0f;
            return { padArea.getX() + (float) (i % 3) * w, padArea.getY() + (float) (i / 3) * h, w, h };
        }

        void refreshSlots()
        {
            auto* m = mod();
            if (m == nullptr) return;
            shownPad = m->selected();
            ids = AquaDrumkitModule::slotIds (shownPad);
            const auto& d = m->getDescriptor();
            for (int i = 0; i < kSlots; ++i)
            {
                auto& s = sliders[(size_t) i];
                const ParamSpec* spec = i < ids.size() ? d.findParam (ids[i]) : nullptr;
                const bool used = spec != nullptr;
                s.setEnabled (used);
                s.setAlpha (used ? 1.0f : 0.3f);
                labels[(size_t) i].setAlpha (used ? 1.0f : 0.3f);
                updating = true;
                if (! used)
                {
                    labels[(size_t) i].setText ("-", juce::dontSendNotification);
                    s.textFromValueFunction = [] (double) { return juce::String(); };
                    s.setRange (0.0, 1.0);
                    s.setValue (0.0, juce::dontSendNotification);
                    s.updateText();
                    updating = false;
                    continue;
                }
                juce::String lb = spec->label;
                lb = lb.fromFirstOccurrenceOf (juce::String (AquaDrumkitModule::padName (shownPad)) + " ", false, false);
                if (lb.isEmpty()) lb = spec->label;
                labels[(size_t) i].setText (lb, juce::dontSendNotification);
                setUpKnob (s, *spec);
                s.setValue (m->getParameter (ids[i]), juce::dontSendNotification);
                s.updateText();
                updating = false;
            }
        }

        void slotMoved (int i)
        {
            auto* m = mod();
            if (m == nullptr || updating || i >= ids.size()) return;
            m->setParameter (ids[i], (float) sliders[(size_t) i].getValue());
            if (paramValuesChangedByContent) paramValuesChangedByContent();
        }

        void timerCallback() override
        {
            auto* m = mod();
            if (m == nullptr) return;
            if (const int h = m->lastHit(); h >= 0 && h != lastSeenHit) { lastSeenHit = h; flashPad = h; flashLevel = 0.6f; }
            if (flashLevel > 0.0f) { flashLevel = juce::jmax (0.0f, flashLevel - 0.15f); repaint(); }
            if (m->selected() != shownPad) { refreshSlots(); repaint(); }
            // knobs moved elsewhere (a preset, undo): follow them
            for (int i = 0; i < juce::jmin (kSlots, ids.size()); ++i)
                if (! sliders[(size_t) i].isMouseButtonDown() && ! juce::exactlyEqual ((float) sliders[(size_t) i].getValue(), m->getParameter (ids[i])))
                {
                    updating = true;
                    sliders[(size_t) i].setValue (m->getParameter (ids[i]), juce::dontSendNotification);
                    updating = false;
                }
        }

        juce::WeakReference<SynthModule> weak;
        AquaDrumkitModule* raw;
        const juce::Colour accent { 0xff4fc3f7 };
        std::array<juce::Slider, kSlots> sliders;
        std::array<juce::Label, kSlots> labels;
        juce::StringArray ids;
        juce::Rectangle<float> padArea;
        juce::Rectangle<int> name;
        int shownPad { -1 }, flashPad { -1 }, lastSeenHit { -1 };
        float flashLevel { 0.0f };
        bool updating { false };
    };
}

std::unique_ptr<juce::Component> AquaDrumkitModule::createExtraContentComponent()
{
    return std::make_unique<DrumkitEditor> (*this);
}

// (registered with a completion: the pads' knobs are added when the first kit is made)
extern "C" AQUANODE_USED_ATTR bool AquaDrumkitModule_aqRegistered =
    aquanode::ModuleFactory::instance().registerModule (AquaDrumkitModule::descriptor(),
                                                        [] { return std::make_unique<AquaDrumkitModule>(); },
                                                        &AquaDrumkitModule::completeDescriptor);
AQUANODE_KEEP_SYMBOL (AquaDrumkitModule_aqRegistered)
