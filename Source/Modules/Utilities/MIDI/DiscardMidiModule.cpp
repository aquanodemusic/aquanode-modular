#include "DiscardMidiModule.h"

using namespace aquanode;

namespace
{
    const char* kClassNames[DiscardMidiModule::kNumClasses] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    constexpr int kCols = 4;
    constexpr int kRows = 4;   // C C# D D# / E F F# G / G# A A# B / ALL
    constexpr int kTextH = 18;  // the line under the boxes

    // the Note Adder's scales (6.9.10), as semitones above the key
    const char* const kScaleNames[DiscardMidiModule::kNumScales] = {
        "Major", "Minor", "Penta Major", "Penta Minor", "Lydian", "Mixolydian", "Dorian", "Phrygian", "Locrian",
        "Harmonic Minor", "Melodic Minor", "Lydian Dom.", "Phrygian Dom.", "Altered", "Whole Tone", "Blues",
        "Neapolitan Maj.", "Neapolitan Min." };
    const int kScaleSteps[DiscardMidiModule::kNumScales][7] = {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 7, 9, -1, -1 }, { 0, 3, 5, 7, 10, -1, -1 },
        { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
        { 0, 1, 3, 5, 6, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 11 }, { 0, 2, 4, 6, 7, 9, 10 },
        { 0, 1, 4, 5, 7, 8, 10 }, { 0, 1, 3, 4, 6, 8, 10 }, { 0, 2, 4, 6, 8, 10, -1 }, { 0, 3, 5, 6, 7, 10, -1 },
        { 0, 1, 3, 5, 7, 9, 11 }, { 0, 1, 3, 5, 7, 8, 11 } };
}

const char* const* DiscardMidiModule::scaleNames() { return kScaleNames; }

bool DiscardMidiModule::scaleHas (int scale, int pitchClass)
{
    for (int s : kScaleSteps[juce::jlimit (0, kNumScales - 1, scale)])
        if (s == pitchClass)
            return true;
    return false;
}

//==============================================================================
// twelve toggle boxes: highlighted = that note class is discarded
//==============================================================================
class DiscardMidiGrid : public juce::Component,
                        public aquanode::CustomParamCableTargets,
                        private juce::Timer
{
public:
    explicit DiscardMidiGrid (DiscardMidiModule& m) : module (&m)
    {
        startTimerHz (12);   // reflect patch loads, mutations, cable-driven boxes
    }

    void paint (juce::Graphics& g) override
    {
        auto* dm = dynamic_cast<DiscardMidiModule*> (module.get());
        if (dm == nullptr)
            return;

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        const bool global = dm->getParameterBase (DiscardMidiModule::kGlobal) > 0.5f;
        const bool greyed = global && ! dm->keyAndScaleApply();

        for (int nc = 0; nc < DiscardMidiModule::kNumBoxes; ++nc)
        {
            // (Global mode: the classes follow the key and scale, dimmed - not tapped)
            const float dim = (global && nc < DiscardMidiModule::kNumClasses) ? (greyed ? 0.25f : 0.6f) : 1.0f;
            const auto box = boxRect (nc).toFloat();
            const bool byHand = dm->isClassSetByHand (nc);
            const bool live = dm->isClassLiveDiscarded (nc);

            // discarded = lit; the live state (cables folded in) is what the
            // engine actually acts on, so that is what the fill shows
            g.setColour ((live ? juce::Colour (0xffd9705a) : juce::Colour (0xff262626)).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (box, 3.0f);

            g.setColour ((live ? juce::Colours::white.withAlpha (0.75f)
                               : juce::Colours::white.withAlpha (0.18f)).withMultipliedAlpha (dim));
            g.drawRoundedRectangle (box, 3.0f, 1.0f);

            g.setColour ((live ? juce::Colours::white : juce::Colours::white.withAlpha (0.55f)).withMultipliedAlpha (dim));
            g.drawText (nc == DiscardMidiModule::kAll ? "ALL" : kClassNames[nc], box, juce::Justification::centred, false);

            // a box a cable is currently overriding gets a marker dot
            if (live != byHand)
            {
                g.setColour (juce::Colour (0xffd970b0));
                g.fillEllipse (box.getRight() - 6.0f, box.getY() + 2.0f, 4.0f, 4.0f);
            }
        }

        // what the boxes mean right now
        juce::String line = "Lit boxes are discarded, on the cables from this card";
        if (global)
        {
            const int key = juce::jlimit (0, 11, (int) std::lround (dm->getParameterBase (DiscardMidiModule::kKey)));
            const int scale = juce::jlimit (0, DiscardMidiModule::kNumScales - 1, (int) std::lround (dm->getParameterBase (DiscardMidiModule::kScale)));
            line = greyed ? juce::String ("Microtuning plays the keys as Linear steps: key and scale are off")
                          : "Every played key: only " + juce::String (kClassNames[key]) + " " + kScaleNames[scale] + " sounds";
        }
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.setColour (greyed ? juce::Colour (0xffffb36b) : juce::Colours::white.withAlpha (0.6f));
        g.drawFittedText (line, getLocalBounds().removeFromBottom (kTextH), juce::Justification::centred, 1, 0.7f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // right-click belongs to the module's knob-modulation menu
        if (e.mods.isPopupMenu())
            return;

        auto* m = module.get();
        if (m == nullptr)
            return;

        const int nc = classAt (e.getPosition());
        if (nc < 0)
            return;
        if (nc < DiscardMidiModule::kNumClasses && m->getParameterBase (DiscardMidiModule::kGlobal) > 0.5f)
            return;   // Global mode: the key and scale decide

        const auto id = paramIdFor (nc);
        m->setParameter (id, m->getParameter (id) > 0.5f ? 0.0f : 1.0f);
        repaint();
    }

    //=== CustomParamCableTargets: each box accepts a knob-modulation cable ===
    juce::String paramTargetAt (juce::Point<int> localPos) const override
    {
        const int nc = classAt (localPos);
        return nc < 0 ? juce::String() : paramIdFor (nc);
    }

    juce::Point<int> paramTargetCentre (const juce::String& paramId) const override
    {
        for (int nc = 0; nc < DiscardMidiModule::kNumBoxes; ++nc)
            if (paramIdFor (nc) == paramId)
                return boxRect (nc).getCentre();
        return { -1, -1 };
    }

private:
    static juce::String paramIdFor (int nc) { return nc == DiscardMidiModule::kAll ? juce::String ("all") : "nc" + juce::String (nc); }

    juce::Rectangle<int> boxRect (int nc) const
    {
        const int gh = getHeight() - kTextH;
        if (nc == DiscardMidiModule::kAll)   // the whole bottom row
            return juce::Rectangle<int> (0, 3 * (gh / kRows), (getWidth() / kCols) * kCols, gh / kRows).reduced (2);
        const int col = nc % kCols;
        const int row = nc / kCols;
        const int w = getWidth() / kCols;
        const int h = gh / kRows;
        return juce::Rectangle<int> (col * w, row * h, w, h).reduced (2);
    }

    int classAt (juce::Point<int> pos) const
    {
        for (int nc = 0; nc < DiscardMidiModule::kNumBoxes; ++nc)
            if (boxRect (nc).expanded (2).contains (pos))
                return nc;
        return -1;
    }

    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> DiscardMidiModule::createExtraContentComponent()
{
    return std::make_unique<DiscardMidiGrid> (*this);
}

//==============================================================================
static ModuleDescriptor discardMidiDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.discardmidi";
    d.displayName = "Discard MIDI";
    d.description =
        "Twelve boxes, one per note class - every one switched on is discarded, in any octave, so "
        "selected notes simply never play. Its MIDI Out goes into a generator's Add MIDI In, and "
        "like Always MIDI it REPLACES the played keys for that generator, so a filtered voice and "
        "an unfiltered one can run off the same keys. ALL discards every note: patched into a module's MIDI In, that module "
        "no longer hears the keys at all. MIDI In filters another stream instead of the keys (a Piano Roll, an Arp...). The boxes take mod cables too: an LFO on a "
        "box gates that note class in and out while you play. Global Mode lets through only the notes of a Key and Scale, "
        "for every played key - every generator and plugin, patched or not. With a Microtuning module the two work "
        "together: this card decides which keys sound, Microtuning their pitch.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 24;
    d.sockets = {
        midiIn  ("midiIn",  "MIDI In"),
        midiOut ("midiOut", "MIDI Out")
    };

    // one hidden toggle per note class, rendered by the grid above; flagged as
    // cable targets so an LFO can gate a class while playing
    for (int i = 0; i < DiscardMidiModule::kNumClasses; ++i)
        d.params.push_back (makeRotary ("nc" + juce::String (i),
                                        "NC" + juce::String (i),
                                        0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f)
                                .hide()
                                .cableTargetInCustomUI());
    d.params.push_back (makeRotary ("all", "All", 0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f)   // kAll
                            .hide()
                            .cableTargetInCustomUI());
    // 6.9.10: Global mode (kGlobal, kKey, kScale) - off for older patches (the default)
    d.params.push_back (makeCombo ("global", "Global Mode", { "Off", "On" }, 0, 0, 2).noMod());
    {
        juce::StringArray keys, scales;
        for (auto* k : kClassNames) keys.add (k);
        for (auto* s : kScaleNames) scales.add (s);
        d.params.push_back (makeCombo ("key", "Key", keys, 0, 1, 2).noMod().visibleWhen ("global", 1.0f));
        d.params.push_back (makeCombo ("scale", "Scale", scales, 0, 1, 3).noMod().visibleWhen ("global", 1.0f));
    }
    return d;
}

AQUANODE_REGISTER_MODULE (DiscardMidiModule, discardMidiDescriptor)
