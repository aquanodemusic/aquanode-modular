//==============================================================================
// MIDI Rain (6.7.10) - little short notes falling around the notes you play,
// like drops. On every step of its clock (Speed, in time with the patch) a
// drop may fall (Chance) somewhere in a range around the played note (Low /
// High, in semitones), Length of a step long.
//
// Where it falls moves the way the AutoMorph Bandpass Modulator's band does:
// Shape - Random, Smooth Random, Sine, Triangle, Saw Up / Saw Down (rising
// or falling sweeps over Cycle steps), Steps Up / Steps Down (walking up or
// down the allowed notes one by one) - with Strength (how far from the middle
// of the range it strays) and Skew (the steps pushed back and forth in time).
// "Random" is seeded: the same step always rains the same way (Seed picks
// another weather), so a song plays back the same.
//
// Lock keeps the drops on notes: the Major or Minor Pentatonic of the lowest
// held key, the Held Notes themselves, chosen Notes (the strip on the card),
// or Off (every semitone). Rain From: every held note rains on its own, or
// only the newest. A drop never lands on a held key (it would end it).
//
// The played notes pass through (Through) - with it off, only the rain goes
// on. Rec on the card records what it plays into a .mid file.
// Inputs: Midi In (the played keys when empty). Output: Midi Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/MpeFollow.h"
#include "../../Shared/MidiTake.h"

using namespace aquanode;

namespace
{
    const juce::StringArray kRainSpeeds { "4 bars", "2 bars", "1 bar", "1/2", "1/4.", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    constexpr double kRainBeats[12] = { 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    const juce::StringArray kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

class MidiRainModule final : public SynthModule
{
public:
    enum P { pSpeed = 0, pShape, pFrom, pLow, pHigh, pLock, pCycle, pChance, pLength, pVelocity, pVelRand,
             pStrength, pSkew, pSkewRate, pSeed, pThrough, pMask };
    enum Lock { lMajorPenta = 0, lMinorPenta, lHeld, lNotes, lOff };

    bool isMidiNoteDriver() const override { return true; }
    bool isMidiProcessor() const override { return true; }
    // its stream (the played notes, passed through, and the rain) is what a listener hears
    bool midiSourceReplacesInput() const override { return true; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        numHeld = 0; numDrops = 0; lastStep = -1.0e18;
    }

    void blockStart() override { clock.blockStart (tempoBpm, sampleRate, tempoFromLane() ? nullptr : hostPlayHead); }

    //=== the played keys (audio thread) =======================================
    bool acceptsMpeEvents() const override { return true; }   // (6.9.11: its notes follow the leading finger)
    aquanode::MpeFollow mpeFollow;
    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent* out, int maxOut) override
    {
        mpeFollow.heard (in);
        if (in.isExpression)
        {
            if (param (pThrough) > 0.5f && maxOut > 0) { out[0] = in; return 1; }   // (the keys passed through keep theirs)
            return 0;
        }
        if (in.isOn) addHeld (in.note); else removeHeld (in.note);
        if (param (pThrough) > 0.5f && maxOut > 0)
        {
            take.add (clock.beat, in.note, in.velocity, in.isOn);
            out[0] = in;
            return 1;
        }
        return 0;
    }
    void midiDriverHeldNoteOn (int note) override { addHeld (note); }
    void midiDriverHeldNoteOff (int note) override { removeHeld (note); }
    void midiDriverAllHeldOff() override { numHeld = 0; }
    void midiAllNotesOff() override { numHeld = 0; numDrops = 0; }

    //=== the rain (audio thread, every sample) =================================
    // (6.9.11) its notes, then the leading finger's MPE on them
    int midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents) override
    {
        return mpeFollow.decorate (events, advanceNotes (sr, events, maxEvents), maxEvents);
    }
    int advanceNotes (double, MidiDriverEvent* events, int maxEvents)
    {
        clock.tick();
        int n = 0;
        const double beat = clock.beat;

        // drops whose time is up end first
        for (int i = 0; i < numDrops && n < maxEvents; )
        {
            if (beat >= drops[i].offBeat)
            {
                events[n++] = { drops[i].pitch, 0.0f, false };
                take.add (beat, drops[i].pitch, 0.0f, false);
                drops[i] = drops[--numDrops];
            }
            else ++i;
        }
        if (numHeld == 0) return n;

        const double stepBeats = kRainBeats[juce::jlimit (0, 11, (int) param (pSpeed))];
        const double secs = beat * 60.0 / juce::jmax (1.0, tempoBpm);
        const double warped = beat / stepBeats
                            + param (pSkew) * 0.01 * 0.45 * std::sin (juce::MathConstants<double>::twoPi * param (pSkewRate) * secs);
        const double step = std::floor (warped);
        if (step == lastStep) return n;
        const bool first = lastStep < -1.0e17;
        lastStep = step;
        if (first) return n;   // (the first step starts with the next boundary)

        // a new step: every source note (or the newest) may drop one
        const double seed = (double) param (pSeed) * 101.0 + 1.0;
        const bool every = param (pFrom) < 0.5f;
        const float chance = param (pChance) * 0.01f;
        const int lowest = lowestHeld();
        for (int h = every ? 0 : numHeld - 1; h < numHeld && n < maxEvents; ++h)
        {
            const int src = held[h];
            const double noteSeed = seed + (every ? (double) src * 0.7071 : 0.0);
            if (seededRandom (step * 3.1623 + noteSeed + 911.0) >= chance) continue;
            const int pitch = pickPitch (src, lowest, step, noteSeed);
            if (pitch < 0) continue;
            // the same pitch already falling: it starts again
            for (int i = 0; i < numDrops; ++i)
                if (drops[i].pitch == pitch && n < maxEvents)
                {
                    events[n++] = { pitch, 0.0f, false };
                    take.add (beat, pitch, 0.0f, false);
                    drops[i] = drops[--numDrops];
                    break;
                }
            if (numDrops >= kMaxDrops || n >= maxEvents) break;
            const float vr = param (pVelRand) * 0.01f;
            const float vel = juce::jlimit (0.02f, 1.0f, param (pVelocity) / 127.0f
                                                       * (1.0f - vr * seededRandom (step * 1.4142 + noteSeed + 77.0)));
            events[n++] = { pitch, vel, true };
            take.add (beat, pitch, vel, true);
            drops[numDrops++] = { pitch, beat + stepBeats * juce::jlimit (0.03, 1.0, (double) param (pLength) * 0.01) };
            // (for the card's sky)
            const int w = shownWrite.load (std::memory_order_relaxed);
            shown[(size_t) (w % kShown)] = { pitch, vel, (float) (pitch - src) };
            shownWrite.store (w + 1, std::memory_order_release);
        }
        return n;
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.midirain";
        d.displayName = "MIDI Rain";
        d.searchKeywords = "rain drops generative droplets sparkle random notes arp";
        d.description =
            "Little short notes falling around the notes you play, like drops. Every step of Speed a drop may fall "
            "(Chance) somewhere between Low and High semitones around the played note, Length of a step long. Shape "
            "moves it as the Bandpass Modulator moves its band: Random (seeded - the same step always rains the same way; "
            "Seed changes the weather), Smooth Random, Sine, Triangle, Saw Up / Down (rising or falling over Cycle steps), "
            "Steps Up / Down (walking the allowed notes). Lock keeps it on the Major or Minor Pentatonic of the lowest held "
            "key, the Held Notes, chosen Notes (the strip on the card) or every semitone. Rain From: every held note, or "
            "the newest. Through passes the played notes on. Rec on the card saves what it plays as a .mid file. With MPE, the notes it plays take the bend, pressure and slide of the finger that leads (the latest pressed) and follow them while they sound; what it records and saves is plain MIDI.";
        d.section = ModuleSection::Utility;
        d.sockets = { midiIn ("midiIn", "MIDI In"), midiOut ("midiOut", "MIDI Out"), tempoIn() };
        d.params = {
            makeSteppedList ("speed", "Speed", kRainSpeeds, 7, 0),
            makeCombo  ("shape", "Shape", { "Random", "Smooth Random", "Sine", "Triangle", "Saw Up", "Saw Down", "Steps Up", "Steps Down" }, 0, 0, 2),
            makeCombo  ("from", "Rain From", { "Every Held Note", "Newest Note" }, 1, 0, 2),
            makeRotary ("low",  "Low",  -36.0f, 36.0f, 0.0f, 1, "st", false, 1.0f),
            makeRotary ("high", "High", -36.0f, 48.0f, 24.0f, 1, "st", false, 1.0f),
            makeCombo  ("lock", "Lock", { "Major Pentatonic", "Minor Pentatonic", "Held Notes", "Notes", "Off" }, 0, 1, 2),
            makeRotary ("cycle", "Cycle", 2.0f, 32.0f, 8.0f, 1, "steps", false, 1.0f),
            makeRotary ("chance", "Chance", 0.0f, 100.0f, 60.0f, 2, "%", false, 1.0f),
            makeRotary ("length", "Length", 3.0f, 100.0f, 30.0f, 2, "%", false, 1.0f),
            makeRotary ("velocity", "Velocity", 1.0f, 127.0f, 72.0f, 2, {}, false, 1.0f),
            makeRotary ("velRand", "Vel Rand", 0.0f, 100.0f, 40.0f, 2, "%", false, 1.0f),
            makeRotary ("strength", "Strength", 0.0f, 100.0f, 100.0f, 3, "%", false, 1.0f),
            makeRotary ("skew", "Skew", 0.0f, 100.0f, 0.0f, 3, "%", false, 1.0f),
            makeRotary ("skewRate", "Skew Rate", 0.02f, 8.0f, 0.25f, 3, "Hz", true, 0.01f),
            makeRotary ("seed", "Seed", 0.0f, 999.0f, 0.0f, 2, {}, false, 1.0f).noMod(),
            makeCombo  ("through", "Through", { "Off", "On" }, 1, 3, 2).noMod()
        };
        auto mask = makeRotary ("mask", "Notes", 0.0f, 4095.0f, 661.0f, 4, {}, false, 1.0f).noMod();   // C D E G A
        mask.hidden = true;
        d.params.push_back (mask);
        return d;
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 132; }

    // for the card (message thread)
    MidiTake take;
    struct Shown { int pitch { 0 }; float velocity { 0.0f }; float offset { 0.0f }; };
    static constexpr int kShown = 64;
    std::array<Shown, kShown> shown {};
    std::atomic<int> shownWrite { 0 };
    double bpmNow() const { return tempoBpm; }
    int lockMode() const { return (int) std::lround (getParameterBase (pLock)); }
    int maskNow() const { return (int) std::lround (getParameterBase (pMask)); }

private:
    struct Drop { int pitch; double offBeat; };
    static constexpr int kMaxDrops = 48;
    Drop drops[kMaxDrops] {};
    int numDrops { 0 };
    int held[128] {};
    int numHeld { 0 };
    double lastStep { -1.0e18 };
    BeatClock clock;

    void addHeld (int note)
    {
        for (int i = 0; i < numHeld; ++i) if (held[i] == note) return;
        if (numHeld < 128) held[numHeld++] = note;
    }
    void removeHeld (int note)
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[i] == note) { for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1]; --numHeld; return; }
    }
    int lowestHeld() const { int lo = 128; for (int i = 0; i < numHeld; ++i) lo = juce::jmin (lo, held[i]); return lo; }
    bool isHeld (int p) const { for (int i = 0; i < numHeld; ++i) if (held[i] == p) return true; return false; }

    // the pitch classes a drop may land on (bit k = pitch class k)
    int allowedMask (int lowest) const
    {
        switch ((int) std::lround (param (pLock)))
        {
            case lMajorPenta: { int m = 0; for (int s : { 0, 2, 4, 7, 9 }) m |= 1 << ((lowest + s) % 12); return m; }
            case lMinorPenta: { int m = 0; for (int s : { 0, 3, 5, 7, 10 }) m |= 1 << ((lowest + s) % 12); return m; }
            case lHeld: { int m = 0; for (int i = 0; i < numHeld; ++i) m |= 1 << (held[i] % 12); return m; }
            case lNotes: { const int m = (int) std::lround (param (pMask)) & 4095; return m != 0 ? m : 4095; }
            default: return 4095;
        }
    }

    // where this step's drop falls, as the Bandpass Modulator picks its place
    int pickPitch (int src, int lowest, double step, double seed) const
    {
        int lo = src + (int) std::lround (param (pLow)), hi = src + (int) std::lround (param (pHigh));
        if (lo > hi) std::swap (lo, hi);
        lo = juce::jlimit (0, 127, lo); hi = juce::jlimit (0, 127, hi);
        const int mask = allowedMask (lowest);
        int list[128], count = 0;
        for (int p = lo; p <= hi; ++p)
            if ((mask >> (p % 12)) & 1) list[count++] = p;
        if (count == 0) return -1;

        const int shape = (int) std::lround (param (pShape));
        const double cycle = juce::jmax (2.0, (double) std::lround (param (pCycle)));
        const double ph = step / cycle, frac = ph - std::floor (ph);
        double u;
        switch (shape)
        {
            case 1: { const double c = std::floor (ph);
                      const double a = seededRandom (c + seed), b = seededRandom (c + 1.0 + seed);
                      const double t = frac * frac * (3.0 - 2.0 * frac); u = a + (b - a) * t; break; }
            case 2: u = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * ph); break;
            case 3: u = 1.0 - std::abs (2.0 * frac - 1.0); break;
            case 4: u = frac; break;
            case 5: u = 1.0 - frac; break;
            case 6: case 7:
            {
                int idx = (int) std::fmod (step, (double) count);
                if (idx < 0) idx += count;
                if (shape == 7) idx = count - 1 - idx;
                u = count > 1 ? (double) idx / (double) (count - 1) : 0.5;
                break;
            }
            default: u = seededRandom (step + seed); break;
        }
        u = 0.5 + (u - 0.5) * param (pStrength) * 0.01;
        int idx = juce::jlimit (0, count - 1, (int) std::floor (u * count));
        // never on a held key: the next allowed one up (or down at the top)
        for (int k = 0; k < count && isHeld (list[idx]); ++k)
            idx = (idx + 1) % count;
        return isHeld (list[idx]) ? -1 : list[idx];
    }
};

//==============================================================================
namespace
{
    // the card: a little sky of the latest drops, the Notes strip (for Lock:
    // Notes), Rec
    class RainView final : public juce::Component, private juce::Timer
    {
    public:
        explicit RainView (MidiRainModule& m) : module (&m)
        {
            addAndMakeVisible (recButton);
            recButton.setClickingTogglesState (false);
            recButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3038));
            recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe05560));
            recButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            recButton.setTooltip ("Rec: records what MIDI Rain plays (with the notes passed through); press again to save it as a .mid file");
            recButton.onClick = [this] { toggleRec(); };
            startTimerHz (30);
        }

        MidiRainModule* mod() const { return dynamic_cast<MidiRainModule*> (module.get()); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4);
            auto bottom = r.removeFromBottom (24);
            recButton.setBounds (bottom.removeFromRight (64).reduced (0, 1));
            strip = bottom.reduced (0, 2).withTrimmedRight (6);
            sky = r.withTrimmedBottom (4);
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            // the sky
            auto s = sky.toFloat();
            juce::ColourGradient bg (juce::Colour (0xff0d1b2a), s.getX(), s.getY(), juce::Colour (0xff15324a), s.getX(), s.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (s, 5.0f);
            for (const auto& d : drops)
            {
                const float age = (float) (juce::Time::getMillisecondCounterHiRes() - d.born) / 1400.0f;
                if (age >= 1.0f) continue;
                const float x = s.getX() + 8.0f + (s.getWidth() - 16.0f) * d.x;
                const float y = s.getY() + 6.0f + (s.getHeight() - 12.0f) * (0.08f + 0.9f * age * (0.6f + 0.4f * d.speed));
                const float a = (1.0f - age) * (0.35f + 0.65f * d.velocity);
                const float rr = 2.0f + 3.0f * d.velocity;
                g.setColour (juce::Colour (0xff7fd8ff).withAlpha (a * 0.35f));
                g.fillRect (juce::Rectangle<float> (x - 0.5f, y - rr * 5.0f, 1.0f, rr * 5.0f));   // its trail
                g.setColour (juce::Colour (0xffbfeeff).withAlpha (a));
                g.fillEllipse (x - rr, y - rr * 1.3f, rr * 2.0f, rr * 2.4f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            if (m->take.isRecording())
                g.drawText ("Rec ... " + juce::String (m->take.numEvents()) + " notes", sky.reduced (8, 4), juce::Justification::topLeft, false);

            // the Notes strip: twelve pitch classes (used by Lock: Notes)
            const int lock = m->lockMode();
            const int mask = m->maskNow();
            const float w = (float) strip.getWidth() / 12.0f;
            for (int k = 0; k < 12; ++k)
            {
                auto cell = juce::Rectangle<float> ((float) strip.getX() + w * (float) k, (float) strip.getY(), w, (float) strip.getHeight()).reduced (1.0f, 0.0f);
                const bool on = (mask >> k) & 1;
                const bool black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
                g.setColour (on ? juce::Colour (0xff7fd8ff).withAlpha (lock == MidiRainModule::lNotes ? 1.0f : 0.35f)
                                : (black ? juce::Colour (0xff1c232b) : juce::Colour (0xff2c3640)));
                g.fillRoundedRectangle (cell, 2.0f);
                g.setColour (on ? juce::Colour (0xff0d1b2a) : juce::Colours::white.withAlpha (0.6f));
                g.setFont (juce::Font (juce::FontOptions (juce::jmin (10.0f, w * 0.45f))));
                g.drawText (kNoteNames[k], cell, juce::Justification::centred, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* m = mod();
            if (m == nullptr || ! strip.contains (e.getPosition())) return;
            const int k = juce::jlimit (0, 11, (int) ((float) (e.x - strip.getX()) / ((float) strip.getWidth() / 12.0f)));
            m->setParameter ("mask", (float) (m->maskNow() ^ (1 << k)));
            if (m->lockMode() != MidiRainModule::lNotes) m->setParameter ("lock", (float) MidiRainModule::lNotes);
            repaint();
        }

    private:
        struct Falling { float x, velocity, speed; double born; };
        std::vector<Falling> drops;
        int seen { 0 };

        void timerCallback() override
        {
            auto* m = mod();
            if (m == nullptr) return;
            const int w = m->shownWrite.load (std::memory_order_acquire);
            if (w - seen > MidiRainModule::kShown) seen = w - MidiRainModule::kShown;
            for (; seen < w; ++seen)
            {
                const auto& d = m->shown[(size_t) (seen % MidiRainModule::kShown)];
                drops.push_back ({ juce::jlimit (0.0f, 1.0f, (float) (d.pitch % 48) / 47.0f), d.velocity,
                                   seededRandom ((double) seen * 0.37), juce::Time::getMillisecondCounterHiRes() });
            }
            const double now = juce::Time::getMillisecondCounterHiRes();
            drops.erase (std::remove_if (drops.begin(), drops.end(), [now] (const Falling& f) { return now - f.born > 1400.0; }), drops.end());
            recButton.setToggleState (m->take.isRecording(), juce::dontSendNotification);
            recButton.setButtonText (m->take.isRecording() ? "Stop" : "Rec");
            repaint();
        }

        void toggleRec()
        {
            auto* m = mod();
            if (m == nullptr) return;
            if (! m->take.isRecording()) { m->take.start(); return; }
            m->take.stop();
            m->take.save (chooser, "MIDI Rain", "MIDI Rain", m->bpmNow());
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton recButton { "Rec" };
        juce::Rectangle<int> sky, strip;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}

std::unique_ptr<juce::Component> MidiRainModule::createExtraContentComponent()
{
    return std::make_unique<RainView> (*this);
}

AQUANODE_REGISTER_MODULE (MidiRainModule, MidiRainModule::descriptor)
