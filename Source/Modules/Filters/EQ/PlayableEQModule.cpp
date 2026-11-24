//==============================================================================
// Playable EQ (6.9.2) - an EQ you play from a keyboard.
//
// Every note you play puts a band on the audio passing through, tuned to the
// note: a Bell (boost or cut), a Band Pass (only what lies under the notes
// gets through) or a Notch. Up to 12 bands at once; Harmonics gives each note
// bands on its first 2 - 6 harmonics as well (a chord then carves the audio
// into its overtones). The bands fade in and out (Attack, Release) and glide
// (Glide; in Mono one band slides from note to note).
//
// MPE: every note bends on its own channel and its peak slides with it;
// Pressure (or plain aftertouch) raises the band's gain, Slide (CC 74) narrows
// it. Velocity sets how strong each band starts. Octave and Fine move all the
// bands from the keys. Mix blends with the dry audio, Output trims.
// With nothing patched into its Midi In it plays from the keys.
// Global. Inputs: Audio In, Midi In. Output: Audio Out (stereo).
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/LookPalette.h"

using namespace aquanode;

namespace playeq
{
    constexpr int kBands = 12;
    constexpr int kControl = 16;

    struct Svf
    {
        float g { 0.1f }, k { 1.0f }, a1 { 0 }, a2 { 0 }, a3 { 0 };
        float ic1[2] {}, ic2[2] {};
        void set (float hz, float kk, float sr)
        {
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (15.0f, 0.45f * sr, hz) / sr);
            k = juce::jlimit (0.002f, 8.0f, kk);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        // returns the band (v1); lp and hp are what's left around it
        float band (int c, float x)
        {
            const float v3 = x - ic2[c], v1 = a1 * ic1[c] + a2 * v3, v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = 2.0f * v1 - ic1[c]; ic2[c] = 2.0f * v2 - ic2[c];
            return v1;
        }
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
    };

    // the response of one band at a frequency (for the card)
    inline float responseDb (int type, float f, float fc, float q, float gainDb, float depth)
    {
        const float x = f / fc - fc / f;
        const float d = q * x;
        if (type == 1) return 10.0f * std::log10 (1.0f / (1.0f + d * d) + 1.0e-6f);                   // band pass
        if (type == 2) { const float n = d * d / (1.0f + d * d); return 10.0f * std::log10 (1.0f - depth + depth * n + 1.0e-6f); }   // notch
        const float a2 = juce::Decibels::decibelsToGain (gainDb);                                       // bell
        return 10.0f * std::log10 ((a2 * a2 + d * d) / (1.0f + d * d));
    }
}

using namespace playeq;

class PlayableEQModule final : public SynthModule
{
public:
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    bool playedKeysReachRawMidi (const ModuleDescriptor&) const override { return true; }   // (played from the keys when nothing is patched in)
    double voiceTailSeconds() const override { return 0.0; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fs = (float) juce::jmax (8000.0, sr);
        reset();
    }
    void reset() override
    {
        for (auto& b : bands) { b = Band(); }
        for (auto& f : filters) f.reset();
        for (auto& b : chanBend) b = 0.0f;
        for (auto& p : chanPressure) p = 0.0f;
        for (auto& s : chanSlide) s = 0.0f;
        masterBend = 0.0f;
        counter = 0;
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        const int ch = juce::jlimit (1, 16, m.getChannel());
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
        else if (m.isNoteOff()) noteOff (m.getNoteNumber(), ch);
        else if (m.isPitchWheel())
        {
            const float b = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            if (ch == 1) masterBend = b * 2.0f; else chanBend[ch] = b * 48.0f;
        }
        else if (m.isChannelPressure()) chanPressure[ch] = m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
        {
            for (auto& b : bands) if (b.active && b.note == m.getNoteNumber()) b.polyPressure = m.getAfterTouchValue() / 127.0f;
        }
        else if (m.isController())
        {
            if (m.getControllerNumber() == 74) chanSlide[ch] = m.getControllerValue() / 127.0f;
            else if (m.getControllerNumber() == 120 || m.getControllerNumber() == 123) allOff();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    void midiAllNotesOff() override { for (auto& b : bands) b.held = false; }

    void blockStart() override
    {
        type = juce::jlimit (0, 2, (int) std::lround (p ("type")));
        gainDb = p ("gain");
        q = p ("q");
        maxBands = juce::jlimit (1, kBands, (int) std::lround (p ("bands")));
        harmonics = juce::jlimit (1, 6, (int) std::lround (p ("harmonics")));
        offset = 12.0f * std::round (p ("octave")) + p ("fine") * 0.01f;
        glide = p ("glide") * 0.001f;
        attack = p ("attack") * 0.001f; release = p ("release") * 0.001f;
        velAmt = p ("velocity") * 0.01f;
        pressureAmt = p ("pressure") * 0.01f;
        slideAmt = p ("slide") * 0.01f;
        mono = p ("mode") > 0.5f;
        mix = p ("mix") * 0.01f;
        out = juce::Decibels::decibelsToGain (p ("output"));
        attackK = std::exp (-1.0f / juce::jmax (1.0f, attack * fs));
        releaseK = std::exp (-1.0f / juce::jmax (1.0f, release * fs));
    }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        if ((counter++ % kControl) == 0) control();
        const float xl = in[0][0], xr = in[0][1];
        float yl = xl, yr = xr, bpl = 0.0f, bpr = 0.0f;
        for (int i = 0; i < kBands; ++i)
        {
            auto& b = bands[(size_t) i];
            b.env = b.envTarget + (b.env - b.envTarget) * (b.envTarget > b.env ? attackK : releaseK);
            if (! b.active && b.env < 1.0e-4f) continue;
            auto& f = filters[(size_t) i];
            if (type == 0)          // bell, in series: y + (A^2 - 1) k band
            {
                yl += b.m1 * f.k * f.band (0, yl);
                yr += b.m1 * f.k * f.band (1, yr);
            }
            else if (type == 2)     // notch, in series
            {
                yl -= b.depth * f.k * f.band (0, yl);
                yr -= b.depth * f.k * f.band (1, yr);
            }
            else                    // band pass, side by side
            {
                bpl += b.bpGain * f.k * f.band (0, xl);
                bpr += b.bpGain * f.k * f.band (1, xr);
            }
        }
        if (type == 1) { yl = bpl * 2.0f; yr = bpr * 2.0f; }
        outputs[0] = { (xl + mix * (yl - xl)) * out, (xr + mix * (yr - xr)) * out };
    }

    //=== the card ==============================================================
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 140; }
    int preferredModuleWidth() const override { return 420; }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.playableeq";
        d.displayName = "Playable EQ";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 25;
        d.searchKeywords = "playable eq keyboard notes bell band pass notch harmonic mpe tuned resonant filter";
        d.description =
            "An EQ you play: every note puts a band on the audio, tuned to the note - a Bell (Gain boosts or cuts), a "
            "Band Pass (only what lies under the notes gets through) or a Notch. Up to 12 bands at once (Bands); "
            "Harmonics gives each note bands on its first 2 - 6 harmonics too. Q, Attack and Release, Glide (in Mono "
            "one band slides from note to note). MPE: each note bends on its own channel and its peak slides with it, "
            "Pressure (or aftertouch) raises its gain, Slide (CC 74) narrows it; Velocity sets how strong each band "
            "starts. Octave and Fine move all the bands from the keys. Mix with the dry audio, Output. With nothing "
            "patched into MIDI In it plays from the keys.";
        d.sockets = { audioIn ("audioIn", "Audio In"), midiIn ("midiIn", "MIDI In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo  ("type",      "Type",      { "Bell", "Band Pass", "Notch" }, 0, 0, 2).noMod(),
            makeRotary ("gain",      "Gain",      -24.0f, 24.0f, 12.0f, 0, "dB"),
            makeRotary ("q",         "Q",         0.5f, 80.0f, 12.0f, 0, {}, true),
            makeRotary ("bands",     "Bands",     1.0f, (float) kBands, (float) kBands, 0, {}, false, 1.0f).noMod(),
            makeRotary ("harmonics", "Harmonics", 1.0f, 6.0f, 1.0f, 0, {}, false, 1.0f).noMod(),
            makeRotary ("octave",    "Octave",    -3.0f, 3.0f, 0.0f, 1, {}, false, 1.0f),
            makeRotary ("fine",      "Fine",      -100.0f, 100.0f, 0.0f, 1, "ct"),
            makeRotary ("glide",     "Glide",     0.0f, 2000.0f, 20.0f, 1, "ms"),
            makeRotary ("attack",    "Attack",    1.0f, 2000.0f, 15.0f, 1, "ms", true),
            makeRotary ("release",   "Release",   5.0f, 5000.0f, 250.0f, 1, "ms", true),
            makeRotary ("velocity",  "Velocity",  0.0f, 100.0f, 50.0f, 2, "%"),
            makeRotary ("pressure",  "Pressure",  0.0f, 100.0f, 50.0f, 2, "%"),
            makeRotary ("slide",     "Slide",     0.0f, 100.0f, 0.0f, 2, "%"),
            makeCombo  ("mode",      "Mode",      { "Poly", "Mono" }, 0, 2, 1).noMod(),
            makeRotary ("mix",       "Mix",       0.0f, 100.0f, 100.0f, 2, "%"),
            makeRotary ("output",    "Output",    -24.0f, 12.0f, 0.0f, 2, "dB"),
        };
        return d;
    }

    // what the card shows (message thread)
    struct Shown { float hz, q, gainDb, depth, env; int note; };
    std::vector<Shown> shownBands() const
    {
        std::vector<Shown> v;
        const juce::SpinLock::ScopedLockType sl (shownLock);
        for (int i = 0; i < kBands; ++i) if (shown[(size_t) i].env > 0.01f) v.push_back (shown[(size_t) i]);
        return v;
    }
    int shownType() const { return type; }

private:
    float p (const char* id) const { const int i = paramIndex (id); return i >= 0 ? param (i) : 0.0f; }

    struct Band
    {
        bool active { false }, held { false };
        int note { -1 }, channel { 1 }, harmonic { 1 };
        float vel { 0.8f }, polyPressure { 0.0f };
        float env { 0.0f }, envTarget { 0.0f };
        float hz { 440.0f };
        bool fresh { true };
        juce::uint32 age { 0 };
        float m1 { 0.0f }, depth { 0.0f }, bpGain { 0.0f };
    };

    void noteOn (int note, float vel, int ch)
    {
        if (vel <= 0.0f) { noteOff (note, ch); return; }
        if (mono)
        {
            // one note: its bands slide to the new key
            bool moved = false;
            for (auto& b : bands)
                if (b.active) { b.note = note; b.channel = ch; b.vel = vel; b.held = true; b.envTarget = 1.0f; moved = true; }
            if (moved) return;
        }
        int used = 0;
        for (auto& b : bands) if (b.active) ++used;
        for (int h = 1; h <= harmonics; ++h)
        {
            Band* pick = nullptr;
            if (used < maxBands)
                for (auto& b : bands) if (! b.active && (pick == nullptr || b.env < pick->env)) pick = &b;
            if (pick == nullptr)
                for (auto& b : bands)
                    if (b.active && (pick == nullptr || (! b.held && pick->held) || (b.held == pick->held && b.age < pick->age))) pick = &b;
            if (pick == nullptr) return;
            const bool wasSounding = pick->env > 0.01f;
            *pick = Band();
            pick->active = true; pick->held = true;
            pick->note = note; pick->channel = ch; pick->harmonic = h; pick->vel = vel;
            pick->envTarget = 1.0f / (float) h;   // the harmonics fall away 1/n
            pick->env = wasSounding ? pick->env : 0.0f;
            pick->age = ++ageCounter;
            pick->fresh = true;
            if (! wasSounding) filters[(size_t) (pick - bands.data())].reset();
            ++used;
        }
    }
    void noteOff (int note, int ch)
    {
        for (auto& b : bands)
            if (b.active && b.held && b.note == note && (b.channel == ch || ch == 1 || b.channel == 1))
            {
                b.held = false; b.envTarget = 0.0f;
            }
    }
    void allOff() { for (auto& b : bands) { b.held = false; b.envTarget = 0.0f; } }

    void control()
    {
        const float dt = (float) kControl / fs;
        const float glideK = glide > 0.0f ? 1.0f - std::exp (-dt / (glide * 0.35f)) : 1.0f;
        Shown now[kBands] {};
        for (int i = 0; i < kBands; ++i)
        {
            auto& b = bands[(size_t) i];
            if (b.active && ! b.held && b.env < 1.0e-4f) b.active = false;
            if (! b.active && b.env < 1.0e-4f) { now[i].env = 0.0f; continue; }
            const float semis = (float) b.note + offset + masterBend + (b.channel > 1 ? chanBend[b.channel] : 0.0f);
            const float target = (float) midiNoteToHz ((double) semis) * (float) b.harmonic;
            if (b.fresh) { b.hz = target; b.fresh = false; }
            else b.hz *= std::pow (target / b.hz, glideK);
            const float pressure = juce::jmax (b.polyPressure, chanPressure[b.channel > 1 ? b.channel : 1]);
            const float strength = (1.0f - velAmt * (1.0f - b.vel)) * (1.0f + pressureAmt * pressure);
            const float qNow = q * (1.0f + 3.0f * slideAmt * chanSlide[b.channel > 1 ? b.channel : 1]);
            const float e = b.env;
            auto& f = filters[(size_t) i];
            if (type == 0)
            {
                const float dB = juce::jlimit (-36.0f, 36.0f, gainDb * strength * e);
                const float A = std::pow (10.0f, dB / 40.0f);
                f.set (b.hz, 1.0f / (qNow * A), fs);
                b.m1 = A * A - 1.0f;
                now[i] = { b.hz, qNow, dB, 0.0f, e, b.note };
            }
            else if (type == 2)
            {
                f.set (b.hz, 1.0f / qNow, fs);
                b.depth = juce::jlimit (0.0f, 1.0f, strength * e);
                now[i] = { b.hz, qNow, 0.0f, b.depth, e, b.note };
            }
            else
            {
                f.set (b.hz, 1.0f / qNow, fs);
                b.bpGain = juce::jlimit (0.0f, 2.0f, strength * e);
                now[i] = { b.hz, qNow, 0.0f, 0.0f, b.bpGain, b.note };
            }
        }
        const juce::SpinLock::ScopedTryLockType sl (shownLock);
        if (sl.isLocked()) for (int i = 0; i < kBands; ++i) shown[(size_t) i] = now[i];
    }

    float fs { 48000.0f };
    std::array<Band, kBands> bands;
    std::array<Svf, kBands> filters;
    int type { 0 }, maxBands { kBands }, harmonics { 1 };
    float gainDb { 12 }, q { 12 }, offset { 0 }, glide { 0.02f }, attack { 0.015f }, release { 0.25f };
    float velAmt { 0.5f }, pressureAmt { 0.5f }, slideAmt { 0 }, mix { 1 }, out { 1 }, attackK { 0.99f }, releaseK { 0.999f };
    bool mono { false };
    float chanBend[17] {}, chanPressure[17] {}, chanSlide[17] {};
    float masterBend { 0 };
    juce::uint32 ageCounter { 0 }, counter { 0 };
    mutable juce::SpinLock shownLock;
    std::array<Shown, kBands> shown {};
};

//==============================================================================
// The card: the curve the notes make, each band named
//==============================================================================
namespace
{
    class PlayableEqView final : public juce::Component, private juce::Timer
    {
    public:
        explicit PlayableEqView (PlayableEQModule& m) : module (&m) { startTimerHz (30); }
        PlayableEQModule* eq() const { return dynamic_cast<PlayableEQModule*> (module.get()); }

        void paint (juce::Graphics& g) override
        {
            auto* m = eq();
            if (m == nullptr) return;
            const auto look = LookPalette::current (juce::Colour (0xff9be38f), ThemeColours::Filter);
            look.fillBackground (g, getLocalBounds().toFloat());
            const auto p = getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (10.0f);
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (p.expanded (2.0f), 4.0f);
            auto xOf = [&] (float hz) { return p.getX() + p.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
            const int type = m->shownType();
            const float range = type == 0 ? 30.0f : 48.0f;
            auto yOf = [&] (float db) { return type == 0 ? p.getCentreY() - db / range * p.getHeight() * 0.5f : p.getY() + (-db / range) * p.getHeight(); };
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.drawVerticalLine ((int) xOf (hz), p.getY(), p.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz), juce::Rectangle<float> (xOf (hz) - 14.0f, p.getBottom() + 1.0f, 28.0f, 10.0f), juce::Justification::centred, false);
            }
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawHorizontalLine ((int) yOf (0.0f), p.getX(), p.getRight());
            const auto bands = m->shownBands();
            juce::Path path;
            const int n = (int) p.getWidth();
            for (int i = 0; i <= n; ++i)
            {
                const float x = p.getX() + (float) i;
                const float hz = 20.0f * std::pow (1000.0f, (float) i / (float) n);
                float db = 0.0f;
                if (type == 1)
                {
                    float amp = 0.0f;
                    // (6.9.19: each band's own magnitude - responseDb is already the level in dB - so
                    // away from the notes the curve falls as the sound does, not half as far)
                    for (const auto& b : bands) amp += b.env * juce::Decibels::decibelsToGain (playeq::responseDb (1, hz, b.hz, b.q, 0.0f, 0.0f));
                    db = juce::Decibels::gainToDecibels (amp, -48.0f);
                }
                else
                    for (const auto& b : bands) db += playeq::responseDb (type, hz, b.hz, b.q, b.gainDb, b.depth);
                const float y = juce::jlimit (p.getY(), p.getBottom(), yOf (juce::jlimit (-range, range, db)));
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (juce::Colour (0xff9be38f));
            g.strokePath (path, juce::PathStrokeType (1.8f));
            // the bands' notes
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            for (const auto& b : bands)
            {
                const float x = xOf (juce::jlimit (20.0f, 20000.0f, b.hz));
                g.setColour (juce::Colours::white.withAlpha (0.25f + 0.6f * juce::jmin (1.0f, b.env)));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                g.drawText (juce::MidiMessage::getMidiNoteName (b.note, true, true, 4), juce::Rectangle<float> (x - 18.0f, p.getY() + 2.0f, 36.0f, 12.0f), juce::Justification::centred, false);
            }
            if (bands.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.drawText ("play notes: each one is a band", p, juce::Justification::centred, false);
            }
        }

    private:
        void timerCallback() override { repaint(); }
        juce::WeakReference<SynthModule> module;
    };
}

std::unique_ptr<juce::Component> PlayableEQModule::createExtraContentComponent()
{
    return std::make_unique<PlayableEqView> (*this);
}

AQUANODE_REGISTER_MODULE (PlayableEQModule, PlayableEQModule::descriptor)
