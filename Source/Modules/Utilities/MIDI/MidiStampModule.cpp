//==============================================================================
// MIDI Stamp (6.7.10) - ready-made MIDI clips, a little different every time.
//
// Pick a stamp - Random Pad, Random Chord Stab, Random Lead, Random Bells,
// Random Arp, Random Bass, Random Acid Line, Random Beat - and press Stamp:
// it draws one of the clips behind that name (a handful to dozens each) and
// varies it a little (Variation: velocities, timing, now and then a dropped
// note or an octave jump on the lines); Vary makes another variation of the
// same clip. It plays in time on its Midi Out while a key is held (Play:
// Hold - from the clip's start, in the held key) or all the time (Loop - in
// time with the bars, in Key or the held key), Octave up or down. Beats play
// the drum modules' pads (808, 909, Drum Pads: kick, snare, clap, hats...).
//
// To Piano Roll puts the clip as it is now (in the key now) into the patch's
// Piano Roll - a new one beside the stamp, playing what the stamp plays, if
// there is none - to edit; Save MIDI saves it as a .mid file.
// Inputs: Midi In (the played keys when empty). Output: Midi Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/MpeFollow.h"
#include "../../Shared/MidiTake.h"
#include "../../../Engine/PluginProcessor.h"
#include "../../../Plugins/Host/PluginCatalog.h"
#include "PianoRollModule.h"
#include "MidiStampData.h"

using namespace aquanode;

class MidiStampModule final : public SynthModule
{
public:
    enum P { pStamp = 0, pPlay, pKey, pOctave, pVariation, pTake, pVSeed };

    struct Note { double start, length; int pitch; float velocity; };
    struct Pattern
    {
        int stamp { 0 }, clip { -1 }, number { 0 }, of { 0 }, tonic { 0 };
        double beats { 4.0 };
        std::vector<Note> notes;
    };

    MidiStampModule()
    {
        for (auto& p : pool) p = std::make_unique<Pattern>();
    }

    bool isMidiNoteDriver() const override { return true; }
    bool isMidiProcessor() const override { return true; }
    bool midiSourceReplacesInput() const override { return true; }
    bool runsWithoutListeners() const override { return false; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
        if (live.load() == nullptr) regenerate();   // (the parameters exist from here on, not in the constructor)
    }
    void reset() override { numHeld = 0; numActive = 0; playing = false; }

    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        if (id == "stamp" || id == "take" || id == "vseed" || id == "variation") regenerate();
    }

    void blockStart() override
    {
        clock.blockStart (tempoBpm, sampleRate, tempoFromLane() ? nullptr : hostPlayHead);
        // the newest clip, latched for the block (and acknowledged, so the card
        // never reuses one this thread may still read)
        for (;;)
        {
            const Pattern* p = live.load (std::memory_order_acquire);
            acked.store (p, std::memory_order_release);
            if (live.load (std::memory_order_acquire) == p) { current = p; break; }
        }
    }

    //=== the played keys (audio thread) ========================================
    bool acceptsMpeEvents() const override { return true; }   // (6.9.11: its notes follow the leading finger)
    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent*, int) override
    {
        mpeFollow.heard (in);
        if (in.isExpression) return 0;
        if (in.isOn) addHeld (in.note); else removeHeld (in.note);
        return 0;
    }
    aquanode::MpeFollow mpeFollow;
    void midiDriverHeldNoteOn (int note) override { addHeld (note); }
    void midiDriverHeldNoteOff (int note) override { removeHeld (note); }
    void midiDriverAllHeldOff() override { numHeld = 0; }
    void midiAllNotesOff() override { numHeld = 0; }

    // (6.9.11) its notes, then the leading finger's MPE on them
    int midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents) override
    {
        return mpeFollow.decorate (events, advanceNotes (sr, events, maxEvents), maxEvents);
    }
    int advanceNotes (double, MidiDriverEvent* events, int maxEvents)
    {
        clock.tick();
        const double beat = clock.beat;
        int n = 0;
        // the ones whose time is up end first
        for (int i = 0; i < numActive && n < maxEvents; )
            if (beat >= active[i].offBeat) { events[n++] = { active[i].pitch, 0.0f, false }; active[i] = active[--numActive]; }
            else ++i;

        const auto* p = current;
        const bool loop = param (pPlay) > 0.5f;
        const bool want = p != nullptr && ! p->notes.empty() && (loop || numHeld > 0);
        if (! want)
        {
            // (6.7.12) Hold: the key let go - the clip's notes let go with it (a long pad note no longer rings on)
            if (playing)
                while (numActive > 0 && n < maxEvents) events[n++] = { active[--numActive].pitch, 0.0f, false };
            playing = false;
            return n;
        }
        if (! playing)
        {
            playing = true;
            origin = loop ? 0.0 : beat;              // Hold: from the clip's start; Loop: with the bars
            lastPos = -1.0;
        }
        const double len = juce::jmax (1.0, p->beats);
        double pos = std::fmod (beat - origin, len);
        if (pos < 0.0) pos += len;
        const double from = lastPos;
        lastPos = pos;
        if (from < 0.0) { return n + startNotes (*p, -1.0e-9, pos, beat, events + n, maxEvents - n); }
        if (pos >= from) return n + startNotes (*p, from, pos, beat, events + n, maxEvents - n);
        // wrapped round the loop's end
        int k = startNotes (*p, from, len, beat, events + n, maxEvents - n);
        k += startNotes (*p, -1.0e-9, pos, beat, events + n + k, maxEvents - n - k);
        return n + k;
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.midistamp";
        d.displayName = "MIDI Stamp";
        d.searchKeywords = "midi clips patterns loops random pad lead bass bells arp beat acid stamp ideas";
        d.description =
            "Ready-made MIDI clips, a little different every time. Pick a stamp - Random Pad, Chord Stab, Lead, Bells, "
            "Arp, Bass, Acid Line or Beat - and press Stamp: it draws one of the clips behind the name and varies it "
            "(Variation: velocities, timing, now and then a dropped note or an octave jump); Vary varies the same clip "
            "again. It plays while a key is held (Hold, in the held key) or all the time (Loop, in time with the bars, in "
            "Key or the held key); Octave moves it. Beats play the drum modules' pads. To Piano Roll puts the clip into "
            "the patch's Piano Roll to edit (a new one, wired like the stamp, if there is none); Save MIDI saves it. With MPE, the notes it plays take the bend, pressure and slide of the finger that leads (the latest pressed) and follow them while they sound; what it records and saves is plain MIDI.";
        d.section = ModuleSection::Utility;
        d.sockets = { midiIn ("midiIn", "MIDI In"), midiOut ("midiOut", "MIDI Out"), tempoIn() };
        juce::StringArray stamps;
        for (int i = 0; i < stampdata::kNumStamps; ++i) stamps.add (stampdata::stampNames[i]);
        d.params = {
            makeCombo ("stamp", "Stamp", stamps, 0, 0, 3).noMod(),
            makeCombo ("play", "Play", { "Hold", "Loop" }, 0, 0, 2).noMod(),
            makeSteppedList ("key", "Key", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0, 1),
            makeRotary ("octave", "Octave", -2.0f, 2.0f, 0.0f, 1, {}, false, 1.0f),
            makeRotary ("variation", "Variation", 0.0f, 100.0f, 25.0f, 1, "%", false, 1.0f).noMod()
        };
        auto take = makeRotary ("take", "Take", 0.0f, 999.0f, 0.0f, 2, {}, false, 1.0f).noMod();
        take.hidden = true;
        auto vseed = makeRotary ("vseed", "Variation Seed", 0.0f, 99999.0f, 1.0f, 2, {}, false, 1.0f).noMod();
        vseed.hidden = true;
        d.params.push_back (take);
        d.params.push_back (vseed);
        return d;
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 150; }

    //=== the card (message thread) =============================================
    const Pattern* shownPattern() const { return live.load (std::memory_order_acquire); }
    int keyNow() const { return keyFor (numHeldUi.load()); }
    void stampNew()
    {
        const int stamp = juce::jlimit (0, stampdata::kNumStamps - 1, (int) std::lround (getParameterBase (pStamp)));
        int count = 0;
        for (int i = 0; i < stampdata::numClips; ++i) if (stampdata::clips[i].stamp == stamp) ++count;
        const int was = (int) std::lround (getParameterBase (pTake));
        int take = juce::Random::getSystemRandom().nextInt (juce::jmax (1, count));
        if (count > 1 && take == was % count) take = (take + 1 + juce::Random::getSystemRandom().nextInt (count - 1)) % count;
        SynthModule::setParameter ("take", (float) take);
        setParameter ("vseed", (float) juce::Random::getSystemRandom().nextInt (99999));
    }
    void varyAgain() { setParameter ("vseed", (float) juce::Random::getSystemRandom().nextInt (99999)); }

    // the clip as it would play now: in the key now, Octave applied
    std::vector<PianoRollNote> clipForRoll() const
    {
        std::vector<PianoRollNote> out;
        const auto* p = shownPattern();
        if (p == nullptr) return out;
        const int shift = transposeFor (*p, keyNow());
        for (const auto& nt : p->notes)
            out.push_back ({ nt.start, nt.length, juce::jlimit (0, 127, nt.pitch + shift), nt.velocity });
        return out;
    }
    double clipBeats() const { const auto* p = shownPattern(); return p != nullptr ? p->beats : 4.0; }
    double bpmNow() const { return tempoBpm; }

private:
    std::array<std::unique_ptr<Pattern>, 6> pool;
    std::atomic<const Pattern*> live { nullptr }, acked { nullptr };
    const Pattern* current { nullptr };
    std::atomic<int> numHeldUi { -1 };   // (the newest held key, for the card; -1 none)
    struct Active { int pitch; double offBeat; };
    static constexpr int kMaxActive = 128;
    Active active[kMaxActive] {};
    int numActive { 0 };
    int held[128] {};
    int numHeld { 0 };
    bool playing { false };
    double origin { 0.0 }, lastPos { -1.0 };
    BeatClock clock;

    void addHeld (int note)
    {
        for (int i = 0; i < numHeld; ++i) if (held[i] == note) return;
        if (numHeld < 128) held[numHeld++] = note;
        numHeldUi.store (note);
    }
    void removeHeld (int note)
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[i] == note) { for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1]; --numHeld; break; }
        numHeldUi.store (numHeld > 0 ? held[numHeld - 1] : -1);
    }

    int keyFor (int heldNote) const
    {
        return heldNote >= 0 ? heldNote % 12 : juce::jlimit (0, 11, (int) std::lround (getParameterBase (pKey)));
    }
    int transposeFor (const Pattern& p, int key) const
    {
        const int oct = (int) std::lround (getParameterBase (pOctave)) * 12;
        if (p.stamp == stampdata::kBeatStamp) return 0;   // (pads stay pads)
        int t = ((key - p.tonic) % 12 + 12) % 12;
        if (t > 6) t -= 12;                                // (the nearest way: the register stays)
        return t + oct;
    }

    int startNotes (const Pattern& p, double from, double to, double beat, MidiDriverEvent* events, int maxEvents)
    {
        int n = 0;
        const int key = numHeld > 0 ? held[numHeld - 1] % 12 : juce::jlimit (0, 11, (int) std::lround (param (pKey)));
        int shift;
        {
            const int oct = (int) std::lround (param (pOctave)) * 12;
            if (p.stamp == stampdata::kBeatStamp) shift = 0;
            else { int t = ((key - p.tonic) % 12 + 12) % 12; if (t > 6) t -= 12; shift = t + oct; }
        }
        for (const auto& nt : p.notes)
        {
            if (nt.start <= from || nt.start > to) continue;
            if (n + 2 > maxEvents) break;
            const int pitch = juce::jlimit (0, 127, nt.pitch + shift);
            for (int i = 0; i < numActive; ++i)   // the same pitch still sounding: it ends here
                if (active[i].pitch == pitch) { events[n++] = { pitch, 0.0f, false }; active[i] = active[--numActive]; break; }
            if (numActive >= kMaxActive) break;
            events[n++] = { pitch, nt.velocity, true };
            active[numActive++] = { pitch, beat + juce::jmax (0.02, nt.length) };
        }
        return n;
    }

    // the clip drawn and varied (message thread); published to the audio thread
    void regenerate()
    {
        Pattern* slot = nullptr;
        for (auto& p : pool)
            if (p.get() != live.load() && p.get() != acked.load()) { slot = p.get(); break; }
        if (slot == nullptr) return;
        const int stamp = juce::jlimit (0, stampdata::kNumStamps - 1, (int) std::lround (getParameterBase (pStamp)));
        std::vector<int> list;
        for (int i = 0; i < stampdata::numClips; ++i) if (stampdata::clips[i].stamp == stamp) list.push_back (i);
        slot->notes.clear();
        if (list.empty()) { live.store (slot, std::memory_order_release); return; }
        const int take = ((int) std::lround (getParameterBase (pTake)) % (int) list.size() + (int) list.size()) % (int) list.size();
        const auto& c = stampdata::clips[list[(size_t) take]];
        slot->stamp = stamp; slot->clip = list[(size_t) take]; slot->number = take + 1; slot->of = (int) list.size();
        slot->tonic = c.tonic; slot->beats = c.beats;
        const float var = getParameterBase (pVariation) * 0.01f;
        juce::Random rng ((juce::int64) getParameterBase (pVSeed) * 7919 + slot->clip * 131 + 17);
        const bool line = stamp == 2 || stamp == 3 || stamp == 4;   // (lead, bells, arp: octave jumps)
        const bool sparse = stamp == 0 || stamp == 5;               // (pads, bass: hardly any dropped)
        const bool drums = stamp == stampdata::kBeatStamp;
        for (int i = 0; i < c.count; ++i)
        {
            const auto& nt = c.notes[i];
            if (c.count > 4 && rng.nextFloat() < var * (sparse ? 0.03f : 0.12f)) continue;   // a dropped note
            double start = nt.start + (double) (rng.nextFloat() - 0.5f) * var * (drums ? 0.02 : 0.05);
            start = juce::jlimit (0.0, (double) c.beats - 1.0e-3, start);
            int pitch = nt.pitch;
            if (line && rng.nextFloat() < var * 0.08f) pitch += rng.nextBool() ? 12 : -12;
            const float vel = juce::jlimit (0.04f, 1.0f, (float) nt.velocity / 127.0f * (1.0f - var * 0.35f * rng.nextFloat()) + var * 0.04f);
            slot->notes.push_back ({ start, (double) nt.length, juce::jlimit (0, 127, pitch), vel });
        }
        std::sort (slot->notes.begin(), slot->notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        live.store (slot, std::memory_order_release);
    }
};

//==============================================================================
namespace
{
    class StampView final : public juce::Component, public plugins::PatchEditRequester, private juce::Timer
    {
    public:
        explicit StampView (MidiStampModule& m) : module (&m)
        {
            for (auto* b : { &stampButton, &varyButton, &rollButton, &saveButton })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22303a));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            }
            stampButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2c6e7f));
            stampButton.setTooltip ("Draws another clip of this stamp and varies it");
            varyButton.setTooltip ("A new variation of the same clip");
            rollButton.setTooltip ("Puts the clip, in the key now, into the patch's Piano Roll (a new one beside the stamp, wired like it, if there is none)");
            saveButton.setTooltip ("Saves the clip, in the key now, as a .mid file");
            stampButton.onClick = [this] { if (auto* m = mod()) m->stampNew(); repaint(); };
            varyButton.onClick = [this] { if (auto* m = mod()) m->varyAgain(); repaint(); };
            rollButton.onClick = [this] { toPianoRoll(); };
            saveButton.onClick = [this] { saveMidi(); };
            startTimerHz (10);
        }
        MidiStampModule* mod() const { return dynamic_cast<MidiStampModule*> (module.get()); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4);
            auto row = r.removeFromBottom (26);
            const int w = (row.getWidth() - 12) / 4;
            stampButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
            varyButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
            rollButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4);
            saveButton.setBounds (row);
            preview = r.withTrimmedBottom (4);
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            auto r = preview.toFloat();
            juce::ColourGradient bg (juce::Colour (0xff0e2230), r.getX(), r.getY(), juce::Colour (0xff10303a), r.getRight(), r.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (r, 5.0f);
            const auto* p = m->shownPattern();
            if (p == nullptr || p->notes.empty()) return;
            // the title: the stamp and which of its clips (no other names)
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (juce::String (stampdata::stampNames[p->stamp]) + "  #" + juce::String (p->number) + " of " + juce::String (p->of)
                            + "   " + juce::String ((int) std::lround (p->beats / 4.0)) + (p->beats > 4.0 ? " bars" : " bar"),
                        r.reduced (8.0f, 4.0f).removeFromTop (14.0f).toNearestInt(), juce::Justification::left, false);
            // the notes, as on a small piano roll
            auto grid = r.reduced (8.0f, 6.0f).withTrimmedTop (16.0f);
            int lo = 127, hi = 0;
            for (const auto& nt : p->notes) { lo = juce::jmin (lo, nt.pitch); hi = juce::jmax (hi, nt.pitch); }
            lo -= 1; hi += 1;
            const float rowH = grid.getHeight() / (float) juce::jmax (1, hi - lo + 1);
            const float bx = grid.getWidth() / (float) p->beats;
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            for (int b = 0; b <= (int) p->beats; b += 4) g.fillRect (grid.getX() + bx * (float) b, grid.getY(), 1.0f, grid.getHeight());
            for (const auto& nt : p->notes)
            {
                auto rr = juce::Rectangle<float> (grid.getX() + bx * (float) nt.start, grid.getBottom() - rowH * (float) (nt.pitch - lo + 1),
                                                  juce::jmax (2.0f, bx * (float) nt.length - 1.0f), juce::jmax (2.0f, rowH - 0.6f));
                const auto c = juce::Colour (0xff4fe0d0).interpolatedWith (juce::Colour (0xffffd27a), 1.0f - nt.velocity);
                g.setColour (c.withAlpha (0.45f + 0.55f * nt.velocity));
                g.fillRoundedRectangle (rr, juce::jmin (2.0f, rr.getHeight() * 0.4f));
                g.setColour (juce::Colours::white.withAlpha (0.25f));
                g.fillRect (rr.withHeight (juce::jmin (1.0f, rr.getHeight() * 0.3f)));   // (a little gloss on top)
            }
        }

    private:
        const void* lastShown { nullptr };
        void timerCallback() override
        {
            auto* m = mod();
            if (m == nullptr) return;
            if (m->shownPattern() != lastShown) { lastShown = m->shownPattern(); repaint(); }
        }

        void toPianoRoll()
        {
            auto* m = mod();
            if (m == nullptr || requestPatchEdit == nullptr) return;
            auto notes = m->clipForRoll();
            const double beats = m->clipBeats();
            if (notes.empty()) return;
            requestPatchEdit ([notes, beats] (AquanodeModularAudioProcessor& proc, int stampId)
            {
                int rollId = -1;
                for (int id : proc.getModuleIds())
                    if (auto* inst = proc.getInstance (id); inst != nullptr && inst->descriptor().typeId == "util.pianoroll") { rollId = id; break; }
                if (rollId < 0)
                {
                    // a new roll beside the stamp, playing into what the stamp plays into
                    auto* me = proc.getInstance (stampId);
                    const auto at = me != nullptr ? me->position + juce::Point<int> (0, 420) : juce::Point<int> (80, 80);
                    rollId = proc.addModule ("util.pianoroll", at);
                    const auto cables = proc.getCables();
                    for (const auto& c : cables)
                        if (c.fromModule == stampId && c.fromSocket == "midiOut")
                            proc.addCable (rollId, "midiOut", c.toModule, c.toSocket);
                }
                if (auto* inst = proc.getInstance (rollId))
                    if (auto* roll = dynamic_cast<PianoRollModule*> (inst->dsp.get()))
                    {
                        roll->pushUndo();
                        roll->setParameter ("bars", (float) juce::jlimit (1, PianoRollModule::kMaxBars, (int) std::ceil (beats / 4.0 - 1.0e-6)));
                        roll->setNotes (notes);
                    }
            });
        }

        void saveMidi()
        {
            auto* m = mod();
            if (m == nullptr) return;
            const auto notes = m->clipForRoll();
            const auto* p = m->shownPattern();
            const juce::String name = p != nullptr ? juce::String (stampdata::stampNames[p->stamp]) + " " + juce::String (p->number) : juce::String ("MIDI Stamp");
            saveTake = std::make_unique<MidiTake>();
            // the take sorted by time (note-offs can come after later note-ons)
            std::vector<std::tuple<double, int, float, bool>> evs;
            for (const auto& nt : notes) { evs.push_back ({ nt.start, nt.pitch, nt.velocity, true }); evs.push_back ({ nt.start + nt.length, nt.pitch, 0.0f, false }); }
            std::stable_sort (evs.begin(), evs.end(), [] (const auto& a, const auto& b)
            {
                if (std::get<0> (a) != std::get<0> (b)) return std::get<0> (a) < std::get<0> (b);
                return ! std::get<3> (a) && std::get<3> (b);   // (offs before ons at the same time)
            });
            saveTake->start();
            for (const auto& e : evs) saveTake->add (std::get<0> (e), std::get<1> (e), std::get<2> (e), std::get<3> (e));
            saveTake->stop();
            saveTake->save (chooser, "MIDI Stamp", name, m->bpmNow());
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton stampButton { "Stamp" }, varyButton { "Vary" }, rollButton { "To Piano Roll" }, saveButton { "Save MIDI" };
        juce::Rectangle<int> preview;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<MidiTake> saveTake;
    };
}

std::unique_ptr<juce::Component> MidiStampModule::createExtraContentComponent()
{
    return std::make_unique<StampView> (*this);
}

// for tests: the stamp's clip as it would go into a Piano Roll
int midiStampClipNotes (aquanode::SynthModule* m, int& stamp, double& beats)
{
    auto* s = dynamic_cast<MidiStampModule*> (m);
    if (s == nullptr) return -1;
    const auto* p = s->shownPattern();
    stamp = p != nullptr ? p->stamp : -1;
    beats = s->clipBeats();
    return (int) s->clipForRoll().size();
}
void midiStampNew (aquanode::SynthModule* m) { if (auto* s = dynamic_cast<MidiStampModule*> (m)) s->stampNew(); }

AQUANODE_REGISTER_MODULE (MidiStampModule, MidiStampModule::descriptor)
