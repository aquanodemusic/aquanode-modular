//==============================================================================
// Supersaw (6.9.2) - a supersaw designer with its own arpeggiator.
//
// Seven saws per note. Where each sits (its detune) and how loud it is make a
// design; there are two, A and B, and Morph blends from one to the other -
// a knob to play and to modulate. Design A starts as the classic late-90s
// layout: the seven offsets and the Mix curves (centre saw falling, side saws
// rising as Mix turns) published in a 2010 analysis of the original, and a
// Detune curve like its own - gentle over most of the knob, steep at the top.
// The seven saws are band-limited (PolyBLEP), start at random phases (or run
// free, or reset), drift a little if you like, spread across the stereo field
// by their detune, and a high-pass follows the note (as on the original,
// thinning the low end the detuning muddies). Then a 24 dB low-pass with its
// own decay envelope, and the amplifier's ADSR.
//
// Chord plays a chord from one key (octave, fifth, major, minor, sus, sevenths,
// a ninth). Arp plays what you hold - Up, Down, Up-Down, Random, As Played,
// Chord - at the patch's tempo (1/4 .. 1/32, triplets), over 1 - 4 octaves,
// with Gate and Swing; Latch keeps it going after you let go, until a new
// chord. So one key can play a whole arpeggio by itself.
//
// Its defaults are Hypersaw: the classic feeling - Morph 30 %, Detune 60 %, Drift 50 %,
// Cutoff 1.25 kHz with Reso 70 % and F Env 30 %, and a random 1/16 arp.
//
// The card: the seven saws as bars (across: detune, up: level), A and B and
// the blend between them; drag to edit the design you have chosen, or set it
// to Classic, Even or Random, or copy A to B.
// Global (its own voices; MPE: each channel bends its own note).
// Input: Midi In. Output: Audio Out (stereo).
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/LookPalette.h"

using namespace aquanode;

namespace supersaw
{
    constexpr int kSaws = 7;
    constexpr int kVoices = 16;
    constexpr int kCentre = 3;
    // the classic layout: each saw's offset relative to the note (published measurements, 2010)
    constexpr float kClassicOffsets[kSaws] = { -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f };
    constexpr float kMaxOffset = 0.11f;
    const char* const kAIds[kSaws][2] = { { "a1d", "a1l" }, { "a2d", "a2l" }, { "a3d", "a3l" }, { "a4d", "a4l" }, { "a5d", "a5l" }, { "a6d", "a6l" }, { "a7d", "a7l" } };
    const char* const kBIds[kSaws][2] = { { "b1d", "b1l" }, { "b2d", "b2l" }, { "b3d", "b3l" }, { "b4d", "b4l" }, { "b5d", "b5l" }, { "b6d", "b6l" }, { "b7d", "b7l" } };
    // design B starts evenly spread, its outer saws a little softer
    constexpr float kBPos[kSaws] = { -1.0f, -0.6667f, -0.3333f, 0.0f, 0.3333f, 0.6667f, 1.0f };
    constexpr float kBLevel[kSaws] = { 0.7f, 0.85f, 1.0f, 1.0f, 1.0f, 0.85f, 0.7f };

    // Detune: gentle over most of the knob, steep at the top
    inline float detuneCurve (float x) { x = juce::jlimit (0.0f, 1.0f, x); return 0.15f * x + 0.35f * x * x * x + 0.5f * std::pow (x, 8.0f); }
    // Mix: the centre saw and the six side saws (the published fit)
    inline float centreGain (float m) { return -0.55366f * m + 0.99785f; }
    inline float sideGain (float m) { return -0.73764f * m * m + 1.2841f * m + 0.044372f; }

    const juce::StringArray kChordNames { "Off", "Octave", "Fifth", "Major", "Minor", "Sus2", "Sus4", "Maj7", "Min7", "Min9" };
    inline const std::vector<int>& chordIntervals (int c)
    {
        static const std::vector<std::vector<int>> t { { 0 }, { 0, 12 }, { 0, 7, 12 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 2, 7 }, { 0, 5, 7 },
                                                       { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 3, 7, 10, 14 } };
        return t[(size_t) juce::jlimit (0, (int) t.size() - 1, c)];
    }
    const juce::StringArray kArpNames { "Off", "Up", "Down", "Up-Down", "Random", "As Played", "Chord" };
    const juce::StringArray kRateNames { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    constexpr double kRateBeats[6] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };

    inline float polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    struct Svf
    {
        float g { 0.1f }, k { 1.4f }, a1 { 0 }, a2 { 0 }, a3 { 0 }, ic1 { 0 }, ic2 { 0 };
        void set (float hz, float q, float sr)
        {
            g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, 0.45f * sr, hz) / sr);
            k = 1.0f / q;
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void process (float x, float& lp, float& hp)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            lp = v2; hp = x - k * v1 - v2;
        }
        void reset() { ic1 = ic2 = 0.0f; }
    };

    struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
    // what the other presets build on (the defaults are Hypersaw)
    const std::vector<std::pair<const char*, float>> kNeutral { { "morph", 0 }, { "detune", 50 }, { "drift", 10 }, { "cutoff", 12000 }, { "reso", 10 }, { "fenv", 0 }, { "arp", 0 } };
    inline const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> p {
            { "Hypersaw", {} },   // the defaults: Morph 30, Detune 60, Drift 50, Cutoff 1.25k with Reso 70 and F Env 30, a random 1/16 arp
            { "Classic Lead", { { "detune", 62 }, { "mix", 70 }, { "cutoff", 9000 }, { "release", 250 } } },
            { "Trance Arp", { { "detune", 55 }, { "mix", 60 }, { "arp", 3 }, { "rate", 3 }, { "octaves", 2 }, { "gate", 55 }, { "latch", 1 }, { "chord", 4 },
                              { "cutoff", 5200 }, { "fenv", 45 }, { "fdecay", 220 }, { "decay", 260 }, { "sustain", 40 }, { "release", 160 } } },
            { "Hoover Morph", { { "detune", 85 }, { "mix", 85 }, { "morph", 60 }, { "glide", 120 }, { "voices", 1 }, { "cutoff", 7000 }, { "reso", 18 } } },
            { "Wide Pad", { { "detune", 48 }, { "mix", 75 }, { "spread", 100 }, { "attack", 900 }, { "release", 1800 }, { "cutoff", 3800 }, { "chord", 7 }, { "drift", 35 } } },
            { "Pluck Arp", { { "arp", 5 }, { "rate", 3 }, { "octaves", 3 }, { "gate", 35 }, { "latch", 1 }, { "chord", 8 }, { "cutoff", 1800 }, { "fenv", 80 },
                             { "fdecay", 160 }, { "decay", 180 }, { "sustain", 0 }, { "release", 140 }, { "detune", 45 } } },
            { "Gated Stabs", { { "arp", 6 }, { "rate", 3 }, { "gate", 40 }, { "latch", 1 }, { "chord", 4 }, { "swing", 22 }, { "cutoff", 6000 }, { "release", 90 } } },
            { "Soft Strings", { { "detune", 35 }, { "mix", 55 }, { "attack", 450 }, { "release", 1200 }, { "cutoff", 2600 }, { "morph", 100 }, { "drift", 25 } } },
        };
        return p;
    }
}

using namespace supersaw;

class SupersawModule final : public SynthModule
{
public:
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    double voiceTailSeconds() const override { return 0.0; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fs = (float) juce::jmax (8000.0, sr);
        for (int k = 0; k < kSaws; ++k)
            for (int j = 0; j < 2; ++j) { pA[k][j] = paramIndex (kAIds[k][j]); pB[k][j] = paramIndex (kBIds[k][j]); }
        reset();
    }

    void reset() override
    {
        for (auto& v : voices) v = Voice();
        keys.clear(); latched.clear(); arpNotes.clear();
        for (auto& b : chanBend) b = 0.0f;
        masterBend = 0.0f;
        arpOn = -1; arpStep = 0; arpCount = 0; arpSamples = 0.0; arpNoteOffAt = -1.0; arpRunning = false;
        counter = 0;
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        const int ch = juce::jlimit (1, 16, m.getChannel());
        if (m.isNoteOn()) keyDown (m.getNoteNumber(), m.getFloatVelocity(), ch);
        else if (m.isNoteOff()) keyUp (m.getNoteNumber());
        else if (m.isPitchWheel())
        {
            const float b = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            if (ch == 1) masterBend = b * 2.0f; else chanBend[ch] = b * 48.0f;
        }
        else if (m.isController() && (m.getControllerNumber() == 120 || m.getControllerNumber() == 123)) allOff();
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    void midiAllNotesOff() override { allOff(); }

    void blockStart() override
    {
        const float morph = p ("morph") * 0.01f;
        const float mix = p ("mix") * 0.01f;
        const float spreadAmt = p ("spread") * 0.01f;
        detuneAmount = detuneCurve (p ("detune") * 0.01f) * kMaxOffset;
        for (int k = 0; k < kSaws; ++k)
        {
            const float posA = param (pA[k][0]), posB = param (pB[k][0]);
            const float lvA = param (pA[k][1]), lvB = param (pB[k][1]);
            pos[k] = posA + morph * (posB - posA);
            lvl[k] = (lvA + morph * (lvB - lvA)) * (k == kCentre ? centreGain (mix) : sideGain (mix));
            const float pan = juce::jlimit (-1.0f, 1.0f, pos[k] * spreadAmt);
            panL[k] = std::sqrt (0.5f * (1.0f - pan)); panR[k] = std::sqrt (0.5f * (1.0f + pan));
        }
        drift = p ("drift") * 0.01f;
        phaseMode = (int) std::lround (p ("phase"));
        hpTrack = p ("hpf") > 0.5f;
        square = p ("wave") > 0.5f;   // (6.9.15)
        cutoff = p ("cutoff"); reso = p ("reso") * 0.01f;
        fenv = p ("fenv") * 0.01f; fdecay = p ("fdecay") * 0.001f; keyTrack = p ("keytrack") * 0.01f;
        attack = p ("attack") * 0.001f; decay = p ("decay") * 0.001f; sustain = p ("sustain") * 0.01f; release = p ("release") * 0.001f;
        velAmt = p ("velocity") * 0.01f;
        poly = juce::jlimit (1, kVoices, (int) std::lround (p ("voices")));
        glide = p ("glide") * 0.001f;
        chord = (int) std::lround (p ("chord"));
        const int newArp = (int) std::lround (p ("arp"));
        if (newArp != arpMode) { if (arpMode > 0 || newArp > 0) releaseAll(); arpMode = newArp; arpRunning = false; restartDirect(); }
        rate = juce::jlimit (0, 5, (int) std::lround (p ("rate")));
        octaves = juce::jlimit (1, 4, (int) std::lround (p ("octaves")));
        gate = p ("gate") * 0.01f;
        swing = p ("swing") * 0.01f;
        const bool newLatch = p ("latch") > 0.5f;
        if (latch && ! newLatch) { latched.clear(); if (arpMode == 0) releaseUnheld(); }
        latch = newLatch;
        level = p ("level");
        rebuildArpNotes();
    }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        if ((counter++ % 16) == 0) control();
        if (arpMode > 0) arpTick();
        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
            if (v.active) renderVoice (v, l, r);
        outputs[0] = { l * level, r * level };
    }

    //=== the card ==============================================================
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 170; }
    int preferredModuleWidth() const override { return 440; }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.supersaw";
        d.displayName = "Super";   // (6.9.15: was Supersaw; saws or squares)
        d.section = ModuleSection::Oscillator;
        d.sidebarOrder = 7;
        d.searchKeywords = "supersaw super saw square supersquare trance hoover detune unison arp arpeggiator chord jp";
        d.description =
            "A supersaw (or supersquare: Wave) designer: seven saws per note, where each sits (its detune) and how loud it is making a design. "
            "There are two, A and B, and Morph blends from one to the other. Design A starts as the classic late-90s "
            "layout (its seven offsets and Mix curves, from a published analysis of the original), Detune turns them out "
            "gently over most of the knob and steeply at the top, Mix brings the side saws up against the centre one. "
            "Band-limited saws at random phases (or free running, or reset), Drift, the saws spread across the stereo "
            "field by their detune, a high-pass following the note as on the original, a 24 dB low-pass with its own "
            "decay, an ADSR. Chord plays a chord from one key; Arp plays what you hold (Up, Down, Up-Down, Random, As "
            "Played, Chord) at the patch's tempo over 1 - 4 octaves with Gate and Swing, and Latch keeps it going after "
            "you let go. On the card: drag the saws of design A or B, or set one to Classic, Even or Random; Presets. "
            "MPE: each channel bends its own note.";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out"), tempoIn() };
        for (int k = 0; k < kSaws; ++k)
        {
            d.params.push_back (makeRotary (kAIds[k][0], "A" + juce::String (k + 1) + " Detune", -1.0f, 1.0f, kClassicOffsets[k] / kMaxOffset, 9).hide());
            d.params.push_back (makeRotary (kAIds[k][1], "A" + juce::String (k + 1) + " Level", 0.0f, 1.0f, 1.0f, 9).hide());
        }
        for (int k = 0; k < kSaws; ++k)
        {
            d.params.push_back (makeRotary (kBIds[k][0], "B" + juce::String (k + 1) + " Detune", -1.0f, 1.0f, kBPos[k], 9).hide());
            d.params.push_back (makeRotary (kBIds[k][1], "B" + juce::String (k + 1) + " Level", 0.0f, 1.0f, kBLevel[k], 9).hide());
        }
        d.params.push_back (makeRotary ("morph", "Morph", 0.0f, 100.0f, 30.0f, 0, "%"));
        d.params.push_back (makeRotary ("detune", "Detune", 0.0f, 100.0f, 60.0f, 0, "%"));
        d.params.push_back (makeRotary ("mix", "Mix", 0.0f, 100.0f, 65.0f, 0, "%"));
        d.params.push_back (makeRotary ("spread", "Spread", 0.0f, 100.0f, 60.0f, 0, "%"));
        d.params.push_back (makeRotary ("drift", "Drift", 0.0f, 100.0f, 50.0f, 0, "%"));
        d.params.push_back (makeCombo ("phase", "Phase", { "Random", "Free", "Reset" }, 0, 0, 2).noMod());
        d.params.push_back (makeCombo ("hpf", "HPF", { "Off", "Track" }, 1, 1, 2).noMod());
        d.params.push_back (makeRotary ("cutoff", "Cutoff", 30.0f, 20000.0f, 1250.0f, 1, "Hz", true));
        d.params.push_back (makeRotary ("reso", "Reso", 0.0f, 100.0f, 70.0f, 1, "%"));
        d.params.push_back (makeRotary ("fenv", "F Env", -100.0f, 100.0f, 30.0f, 1, "%"));
        d.params.push_back (makeRotary ("fdecay", "F Decay", 5.0f, 5000.0f, 300.0f, 1, "ms", true));
        d.params.push_back (makeRotary ("keytrack", "Key Track", 0.0f, 100.0f, 30.0f, 1, "%"));
        d.params.push_back (makeRotary ("attack", "Attack", 1.0f, 5000.0f, 5.0f, 2, "ms", true));
        d.params.push_back (makeRotary ("decay", "Decay", 5.0f, 8000.0f, 500.0f, 2, "ms", true));
        d.params.push_back (makeRotary ("sustain", "Sustain", 0.0f, 100.0f, 85.0f, 2, "%"));
        d.params.push_back (makeRotary ("release", "Release", 5.0f, 8000.0f, 300.0f, 2, "ms", true));
        d.params.push_back (makeRotary ("velocity", "Velocity", 0.0f, 100.0f, 40.0f, 2, "%"));
        d.params.push_back (makeRotary ("voices", "Voices", 1.0f, (float) kVoices, 12.0f, 2, {}, false, 1.0f).noMod());
        d.params.push_back (makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 2, "ms"));
        d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 2));
        d.params.push_back (makeCombo ("chord", "Chord", kChordNames, 0, 3, 2).noMod());
        d.params.push_back (makeCombo ("arp", "Arp", kArpNames, 4, 3, 2).noMod());
        d.params.push_back (makeCombo ("rate", "Rate", kRateNames, 3, 3, 2).noMod());
        d.params.push_back (makeRotary ("octaves", "Octaves", 1.0f, 4.0f, 1.0f, 3, {}, false, 1.0f).noMod());
        d.params.push_back (makeRotary ("gate", "Gate", 5.0f, 100.0f, 60.0f, 3, "%"));
        d.params.push_back (makeRotary ("swing", "Swing", 0.0f, 60.0f, 0.0f, 3, "%"));
        d.params.push_back (makeCombo ("latch", "Latch", { "Off", "On" }, 0, 3, 2).noMod());
        d.params.push_back (makeOutputTrim (-3.0f));
        d.params.push_back (makeCombo ("wave", "Wave", { "Saw", "Square" }, 0, 0, 2).noMod().whenMissing (0.0f));   // (6.9.15)
        return d;
    }

    // the card (message thread)
    void applyPreset (int index)
    {
        const auto& all = presets();
        if (index < 0 || index >= (int) all.size()) return;
        for (const auto& spec : descriptor().params)
            if (! spec.calibration && ! spec.hidden) setParameter (spec.id, spec.defaultValue);
        if (index > 0) for (const auto& [id, v] : kNeutral) setParameter (id, v);
        for (const auto& [id, v] : all[(size_t) index].values) setParameter (id, v);
    }
    void setDesign (bool b, int kind)   // 0 Classic, 1 Even, 2 Random
    {
        const auto& ids = b ? kBIds : kAIds;
        juce::Random rnd;
        for (int k = 0; k < kSaws; ++k)
        {
            float pos = kind == 0 ? kClassicOffsets[k] / kMaxOffset : (kind == 1 ? (float) (k - kCentre) / (float) kCentre : 0.0f);
            float lv = kind == 2 ? 0.45f + 0.55f * rnd.nextFloat() : 1.0f;
            if (kind == 2) pos = k == kCentre ? 0.0f : (float) (k - kCentre) / (float) kCentre * (0.5f + 0.5f * rnd.nextFloat());
            setParameter (ids[k][0], pos);
            setParameter (ids[k][1], lv);
        }
    }
    void copyAtoB()
    {
        for (int k = 0; k < kSaws; ++k)
            for (int j = 0; j < 2; ++j) setParameter (kBIds[k][j], getParameter (kAIds[k][j]));
    }
    float centsAtEdge() const { return 1200.0f * std::log2 (1.0f + detuneCurve (getParameter ("detune") * 0.01f) * kMaxOffset); }

private:
    float p (const char* id) const { const int i = paramIndex (id); return i >= 0 ? param (i) : 0.0f; }

    struct Voice
    {
        bool active { false }, held { false };
        int note { -1 }, owner { -1 }, channel { 1 };
        float vel { 0.8f };
        juce::uint32 age { 0 };
        int stage { 0 };
        float env { 0.0f }, fenvLevel { 0.0f };
        float pitch { 60.0f }, pitchTarget { 60.0f };
        double phase[kSaws] {};
        double inc[kSaws] {};
        float driftNow[kSaws] {}, driftTarget[kSaws] {};
        Svf hpL, hpR, lp1L, lp1R, lp2L, lp2R;
        juce::uint32 seed { 1 };
    };
    static float rand01 (juce::uint32& s) { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 16777216.0f); }

    //=== notes: keys -> (chord) -> voices or the arp ===========================
    void keyDown (int note, float vel, int ch)
    {
        if (vel <= 0.0f) { keyUp (note); return; }
        if (latch && keys.empty() && ! latched.empty())
        {
            // a new chord after letting go: the latched one ends
            if (arpMode == 0) for (int n : latched) for (int i : chordIntervals (chord)) voiceOff (n + i, n);
            latched.clear();
        }
        keys.push_back ({ note, vel, ch });
        if (latch) latched.push_back (note);
        lastVel = vel;
        if (arpMode == 0)
            for (int i : chordIntervals (chord)) voiceOn (note + i, vel, note, ch);
        rebuildArpNotes();
    }
    void keyUp (int note)
    {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i].note == note) { keys.erase (keys.begin() + (long) i); break; }
        if (arpMode == 0 && ! latch)
            for (int i : chordIntervals (chord)) voiceOff (note + i, note);
        rebuildArpNotes();
    }
    void restartDirect()
    {
        if (arpMode != 0) return;
        for (const auto& k : keys) for (int i : chordIntervals (chord)) voiceOn (k.note + i, k.vel, k.note, k.channel);
    }
    void releaseUnheld()
    {
        for (auto& v : voices)
            if (v.active && v.held)
            {
                bool held = false;
                for (const auto& k : keys) if (k.note == v.owner) held = true;
                if (! held) { v.held = false; v.stage = 3; }
            }
    }
    void releaseAll() { for (auto& v : voices) if (v.active) { v.held = false; v.stage = 3; } }
    void allOff() { for (auto& v : voices) v.active = false; keys.clear(); latched.clear(); arpNotes.clear(); arpRunning = false; }

    void rebuildArpNotes()
    {
        std::vector<int> src;
        if (latch) src = latched;
        else for (const auto& k : keys) src.push_back (k.note);
        std::vector<int> notes;
        for (int n : src) for (int i : chordIntervals (chord)) notes.push_back (n + i);
        if (arpMode != 5) { std::sort (notes.begin(), notes.end()); notes.erase (std::unique (notes.begin(), notes.end()), notes.end()); }
        std::vector<int> all;
        for (int o = 0; o < octaves; ++o) for (int n : notes) all.push_back (n + 12 * o);
        arpNotes = all;
        if (arpNotes.empty()) arpRunning = false;
    }

    // the arp: at the patch's tempo
    void arpTick()
    {
        if (arpNotes.empty())
        {
            if (arpOn >= 0) { voiceOff (arpOn, -2); arpOn = -1; }
            return;
        }
        const double stepLen = 60.0 / juce::jmax (20.0, tempoBpm) * kRateBeats[rate] * fs;
        const double thisLen = stepLen * (1.0 + ((arpCount & 1) == 0 ? swing : -swing));
        if (! arpRunning) { arpRunning = true; arpSamples = thisLen; arpStep = 0; arpCount = 0; }   // the first step: at once
        if (arpOn >= 0 && arpNoteOffAt >= 0.0 && arpSamples >= arpNoteOffAt) { voiceOff (arpOn, -2); if (arpMode == 6) for (int n : chordNotesOn) voiceOff (n, -2); arpOn = -1; arpNoteOffAt = -1.0; }
        arpSamples += 1.0;
        if (arpSamples < thisLen) return;
        arpSamples = 0.0;
        if (arpOn >= 0) { voiceOff (arpOn, -2); if (arpMode == 6) for (int n : chordNotesOn) voiceOff (n, -2); arpOn = -1; }
        const int n = (int) arpNotes.size();
        int pick = 0;
        switch (arpMode)
        {
            case 1: case 5: pick = arpStep % n; break;
            case 2: pick = n - 1 - arpStep % n; break;
            case 3: { const int period = juce::jmax (1, 2 * n - 2); const int s = arpStep % period; pick = s < n ? s : period - s; break; }
            case 4: rnd = rnd * 1664525u + 1013904223u; pick = (int) ((rnd >> 8) % (juce::uint32) n); break;
            default: pick = 0; break;
        }
        ++arpStep; ++arpCount;
        const float vel = lastVel;
        if (arpMode == 6)
        {
            // Chord: the whole chord on every step
            chordNotesOn.clear();
            for (int k = 0; k < n && k < 8; ++k) { voiceOn (arpNotes[(size_t) k], vel, -2, 1); chordNotesOn.push_back (arpNotes[(size_t) k]); }
            arpOn = arpNotes[0];
        }
        else
        {
            arpOn = arpNotes[(size_t) pick];
            voiceOn (arpOn, vel, -2, 1);
        }
        arpNoteOffAt = juce::jmax (1.0, thisLen * gate);
    }

    void voiceOn (int note, float vel, int owner, int ch)
    {
        if (note < 0 || note > 127) return;
        if (poly == 1)
        {
            for (auto& v : voices)
                if (v.active && v.stage != 3)
                {
                    v.note = note; v.owner = owner; v.held = true; v.vel = vel; v.channel = ch;
                    v.pitchTarget = (float) note;
                    if (glide <= 0.0f) v.pitch = (float) note;
                    if (arpMode > 0) { v.stage = 0; }
                    return;
                }
        }
        Voice* pick = nullptr;
        int activeCount = 0;
        for (auto& v : voices) if (v.active) ++activeCount;
        for (auto& v : voices) if (! v.active) { pick = &v; break; }
        if (pick == nullptr || activeCount >= poly)
            for (auto& v : voices)
                if (v.active && (pick == nullptr || ! pick->active || (v.stage == 3) > (pick->stage == 3) || ((v.stage == 3) == (pick->stage == 3) && v.age < pick->age)))
                    pick = &v;
        auto& v = *pick;
        const bool was = v.active;
        v.active = true; v.held = true; v.note = note; v.owner = owner; v.vel = vel; v.channel = ch;
        v.age = ++ageCounter; v.stage = 0;
        if (! was) { v.env = 0.0f; v.hpL.reset(); v.hpR.reset(); v.lp1L.reset(); v.lp1R.reset(); v.lp2L.reset(); v.lp2R.reset(); }
        v.fenvLevel = 1.0f;
        v.pitch = (glide > 0.0f && was) ? v.pitch : (float) note;
        v.pitchTarget = (float) note;
        v.seed = 0x2545f491u * (v.age + 3u);
        for (int k = 0; k < kSaws; ++k)
        {
            if (phaseMode == 0) v.phase[k] = rand01 (v.seed);
            else if (phaseMode == 2) v.phase[k] = 0.0;
            else if (! was) v.phase[k] = std::fmod (0.137 * (k + 1) * (ageCounter % 97), 1.0);
            v.driftNow[k] = v.driftTarget[k] = 0.0f;
        }
    }
    void voiceOff (int note, int owner)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == note && (owner == -2 ? v.owner == -2 : v.owner == owner))
            {
                v.held = false; v.stage = 3;
                if (owner != -2) break;
            }
    }

    //=== control (every 16 samples) and audio ==================================
    void control()
    {
        const float dt = 16.0f / fs;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (glide > 0.0f) v.pitch += (v.pitchTarget - v.pitch) * juce::jmin (1.0f, dt / glide * 3.0f);
            else v.pitch = v.pitchTarget;
            const float semis = v.pitch + masterBend + (v.channel > 1 ? chanBend[v.channel] : 0.0f);
            const float f0 = (float) midiNoteToHz ((double) semis);
            for (int k = 0; k < kSaws; ++k)
            {
                // drift: each saw wanders a few cents, slowly
                if (rand01 (v.seed) < dt * 2.0f) v.driftTarget[k] = (rand01 (v.seed) * 2.0f - 1.0f) * drift * 6.0f;
                v.driftNow[k] += (v.driftTarget[k] - v.driftNow[k]) * dt * 3.0f;
                const float hz = f0 * (1.0f + pos[k] * detuneAmount) * std::pow (2.0f, v.driftNow[k] / 1200.0f);
                v.inc[k] = juce::jlimit (0.0, 0.45, (double) hz / fs);
            }
            // envelopes
            switch (v.stage)
            {
                case 0: v.env += dt / juce::jmax (0.001f, attack); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } break;
                case 1: v.env = sustain + (v.env - sustain) * std::exp (-dt / juce::jmax (0.001f, decay * 0.3f)); if (std::abs (v.env - sustain) < 1.0e-3f) v.stage = 2; break;
                case 2: v.env = sustain; if (sustain <= 0.0f) v.stage = 3; break;
                default: v.env *= std::exp (-dt / juce::jmax (0.001f, release * 0.3f)); if (v.env < 1.0e-4f) v.active = false; break;
            }
            v.fenvLevel *= std::exp (-dt / juce::jmax (0.001f, fdecay * 0.3f));
            if (! v.active) continue;
            // the filters
            v.hpL.set (f0 * 0.9f, 0.6f, fs); v.hpR.set (f0 * 0.9f, 0.6f, fs);
            const float fc = cutoff * std::pow (2.0f, fenv * 6.0f * v.fenvLevel) * std::pow (f0 / 261.6f, keyTrack);
            const float q = 0.55f + reso * 6.0f;
            v.lp1L.set (fc, 0.54f, fs); v.lp1R.set (fc, 0.54f, fs);
            v.lp2L.set (fc, q, fs); v.lp2R.set (fc, q, fs);
        }
    }

    void renderVoice (Voice& v, float& l, float& r)
    {
        float sl = 0.0f, sr = 0.0f;
        for (int k = 0; k < kSaws; ++k)
        {
            if (lvl[k] <= 1.0e-4f) { v.phase[k] += v.inc[k]; if (v.phase[k] >= 1.0) v.phase[k] -= 1.0; continue; }
            double ph = v.phase[k] + v.inc[k];
            if (ph >= 1.0) ph -= 1.0;
            v.phase[k] = ph;
            float saw;
            if (square)   // (6.9.15) a band-limited square: two steps a cycle
            {
                double h = ph + 0.5; if (h >= 1.0) h -= 1.0;
                saw = (ph < 0.5 ? 1.0f : -1.0f) + polyBlep (ph, v.inc[k]) - polyBlep (h, v.inc[k]);
                saw *= 0.8f;   // (about as loud as the saws)
            }
            else
                saw = (float) (2.0 * ph - 1.0) - polyBlep (ph, v.inc[k]);
            sl += saw * lvl[k] * panL[k];
            sr += saw * lvl[k] * panR[k];
        }
        float lpo, hpo;
        if (hpTrack) { v.hpL.process (sl, lpo, hpo); sl = hpo; v.hpR.process (sr, lpo, hpo); sr = hpo; }
        v.lp1L.process (sl, lpo, hpo); sl = lpo; v.lp2L.process (sl, lpo, hpo); sl = lpo;
        v.lp1R.process (sr, lpo, hpo); sr = lpo; v.lp2R.process (sr, lpo, hpo); sr = lpo;
        const float g = v.env * (1.0f - velAmt * (1.0f - v.vel)) * 0.4f;
        l += sl * g; r += sr * g;
    }

    struct Key { int note; float vel; int channel; };
    float fs { 48000.0f };
    std::array<Voice, kVoices> voices;
    int pA[kSaws][2] {}, pB[kSaws][2] {};
    float pos[kSaws] {}, lvl[kSaws] {}, panL[kSaws] {}, panR[kSaws] {};
    bool square { false };
    float detuneAmount { 0.01f }, drift { 0.1f }, cutoff { 12000 }, reso { 0.1f }, fenv { 0 }, fdecay { 0.3f }, keyTrack { 0.3f };
    float attack { 0.005f }, decay { 0.5f }, sustain { 0.85f }, release { 0.3f }, velAmt { 0.4f }, glide { 0 }, level { 0.7f };
    float gate { 0.6f }, swing { 0 }, lastVel { 0.8f };
    int phaseMode { 0 }, poly { 12 }, chord { 0 }, arpMode { 0 }, rate { 3 }, octaves { 1 };
    bool hpTrack { true }, latch { false };
    std::vector<Key> keys;
    std::vector<int> latched, arpNotes, chordNotesOn;
    int arpOn { -1 }, arpStep { 0 }, arpCount { 0 };
    double arpSamples { 0.0 }, arpNoteOffAt { -1.0 };
    bool arpRunning { false };
    juce::uint32 rnd { 12345 }, ageCounter { 0 }, counter { 0 };
    float chanBend[17] {};
    float masterBend { 0 };
};

//==============================================================================
// The card: the seven saws of A and B and the blend between them
//==============================================================================
namespace
{
    class SawView final : public juce::Component, public aquanode::ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit SawView (SupersawModule& m) : module (&m)
        {
            for (auto* b : { &editA, &editB, &classic, &even, &random, &copy, &presetsButton })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d2433));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
                b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d6fd6));
            }
            editA.setClickingTogglesState (false); editB.setClickingTogglesState (false);
            editA.setToggleState (true, juce::dontSendNotification);
            editA.onClick = [this] { editingB = false; editA.setToggleState (true, juce::dontSendNotification); editB.setToggleState (false, juce::dontSendNotification); repaint(); };
            editB.onClick = [this] { editingB = true; editB.setToggleState (true, juce::dontSendNotification); editA.setToggleState (false, juce::dontSendNotification); repaint(); };
            auto design = [this] (int kind) { if (auto* s = saw()) { s->setDesign (editingB, kind); changed(); } };
            classic.onClick = [design] { design (0); };
            even.onClick = [design] { design (1); };
            random.onClick = [design] { design (2); };
            copy.onClick = [this] { if (auto* s = saw()) { s->copyAtoB(); changed(); } };
            editA.setTooltip ("Drag the saws of design A");
            editB.setTooltip ("Drag the saws of design B");
            classic.setTooltip ("The classic layout of the late-90s supersaw");
            even.setTooltip ("Seven saws evenly spread");
            random.setTooltip ("A new random design");
            copy.setTooltip ("Copy design A into B");
            presetsButton.onClick = [this]
            {
                juce::PopupMenu menu;
                menu.addSectionHeader ("Supersaw presets");
                int id = 1;
                for (const auto& pr : presets()) menu.addItem (id++, pr.name);
                juce::Component::SafePointer<SawView> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton), [safe] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    if (auto* s = safe->saw()) s->applyPreset (r - 1);
                    if (safe->paramsChangedByContent) safe->paramsChangedByContent();
                    safe->repaint();
                });
            };
            startTimerHz (20);
        }
        SupersawModule* saw() const { return dynamic_cast<SupersawModule*> (module.get()); }

        void resized() override
        {
            auto row = getLocalBounds().removeFromTop (26).reduced (4, 3);
            editA.setBounds (row.removeFromLeft (30)); row.removeFromLeft (2);
            editB.setBounds (row.removeFromLeft (30)); row.removeFromLeft (8);
            classic.setBounds (row.removeFromLeft (54)); row.removeFromLeft (2);
            even.setBounds (row.removeFromLeft (44)); row.removeFromLeft (2);
            random.setBounds (row.removeFromLeft (56)); row.removeFromLeft (2);
            copy.setBounds (row.removeFromLeft (44));
            presetsButton.setBounds (row.removeFromRight (64));
        }

        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedTop (30.0f).reduced (10.0f, 4.0f).withTrimmedBottom (14.0f); }
        float xOf (float pos) const { const auto p = plot(); return p.getCentreX() + pos * p.getWidth() * 0.47f; }
        float yOf (float lv) const { const auto p = plot(); return p.getBottom() - lv * p.getHeight(); }

        void paint (juce::Graphics& g) override
        {
            auto* s = saw();
            if (s == nullptr) return;
            const auto look = LookPalette::current (juce::Colour (0xff7fb0ff), ThemeColours::Oscillator);
            look.fillBackground (g, getLocalBounds().toFloat());
            const auto p = plot();
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (p.expanded (6.0f, 3.0f), 4.0f);
            // the cents axis at the present Detune
            const float edge = s->centsAtEdge();
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float f : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            {
                const float x = xOf (f);
                g.setColour (juce::Colours::white.withAlpha (f == 0.0f ? 0.2f : 0.08f));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText ((f > 0 ? "+" : "") + juce::String ((int) std::lround (f * edge)) + " ct", juce::Rectangle<float> (x - 22.0f, p.getBottom() + 2.0f, 44.0f, 11.0f), juce::Justification::centred, false);
            }
            const float morph = s->getParameter ("morph") * 0.01f;
            auto bars = [&] (const char* const ids[kSaws][2], juce::Colour c, float w, bool handles)
            {
                for (int k = 0; k < kSaws; ++k)
                {
                    const float pos = s->getParameter (ids[k][0]), lv = s->getParameter (ids[k][1]);
                    const float x = xOf (pos);
                    g.setColour (c.withAlpha (0.25f));
                    g.fillRect (x - w * 0.5f, yOf (lv), w, p.getBottom() - yOf (lv));
                    g.setColour (c);
                    g.drawLine (x, yOf (lv), x, p.getBottom(), handles ? 2.0f : 1.2f);
                    if (handles)
                    {
                        g.fillEllipse (x - 5.0f, yOf (lv) - 5.0f, 10.0f, 10.0f);
                        g.setColour (juce::Colours::black);
                        g.drawText (juce::String (k + 1), juce::Rectangle<float> (x - 5.0f, yOf (lv) - 5.0f, 10.0f, 10.0f), juce::Justification::centred, false);
                    }
                }
            };
            const auto colA = juce::Colour (0xff5fd3ff), colB = juce::Colour (0xffff7ad9);
            bars (kAIds, colA.withAlpha (editingB ? 0.45f : 0.95f), 10.0f, ! editingB);
            bars (kBIds, colB.withAlpha (editingB ? 0.95f : 0.45f), 10.0f, editingB);
            // the blend that sounds
            for (int k = 0; k < kSaws; ++k)
            {
                const float pos = s->getParameter (kAIds[k][0]) + morph * (s->getParameter (kBIds[k][0]) - s->getParameter (kAIds[k][0]));
                const float lv = s->getParameter (kAIds[k][1]) + morph * (s->getParameter (kBIds[k][1]) - s->getParameter (kAIds[k][1]));
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRect (xOf (pos) - 1.5f, yOf (lv), 3.0f, p.getBottom() - yOf (lv));
            }
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("A", juce::Rectangle<float> (p.getX() + 2.0f, p.getY() + 1.0f, 20.0f, 14.0f), juce::Justification::centredLeft, false);
            g.drawText ("Morph " + juce::String ((int) std::lround (morph * 100.0f)) + " %", juce::Rectangle<float> (p.getCentreX() - 50.0f, p.getY() + 1.0f, 100.0f, 14.0f), juce::Justification::centred, false);
            g.drawText ("B", juce::Rectangle<float> (p.getRight() - 22.0f, p.getY() + 1.0f, 20.0f, 14.0f), juce::Justification::centredRight, false);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* s = saw();
            dragSaw = -1;
            if (s == nullptr) return;
            const auto& ids = editingB ? kBIds : kAIds;
            float best = 16.0f;
            for (int k = 0; k < kSaws; ++k)
            {
                const float x = xOf (s->getParameter (ids[k][0]));
                const float d = std::abs (e.position.x - x);
                if (d < best) { best = d; dragSaw = k; }
            }
            mouseDrag (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* s = saw();
            if (s == nullptr || dragSaw < 0) return;
            const auto& ids = editingB ? kBIds : kAIds;
            const auto p = plot();
            s->setParameter (ids[dragSaw][0], juce::jlimit (-1.0f, 1.0f, (e.position.x - p.getCentreX()) / (p.getWidth() * 0.47f)));
            s->setParameter (ids[dragSaw][1], juce::jlimit (0.0f, 1.0f, (p.getBottom() - e.position.y) / p.getHeight()));
            if (paramValuesChangedByContent) paramValuesChangedByContent();
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override { dragSaw = -1; }

    private:
        void changed() { if (paramValuesChangedByContent) paramValuesChangedByContent(); repaint(); }
        void timerCallback() override
        {
            auto* s = saw();
            if (s == nullptr) return;
            juce::String now;
            for (int k = 0; k < kSaws; ++k) now << s->getParameter (kAIds[k][0]) << s->getParameter (kAIds[k][1]) << s->getParameter (kBIds[k][0]) << s->getParameter (kBIds[k][1]);
            now << s->getParameter ("morph") << s->getParameter ("detune");
            if (now != shown) { shown = now; repaint(); }
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton editA { "A" }, editB { "B" }, classic { "Classic" }, even { "Even" }, random { "Random" }, copy { "A>B" }, presetsButton { "Presets" };
        bool editingB { false };
        int dragSaw { -1 };
        juce::String shown;
    };
}

std::unique_ptr<juce::Component> SupersawModule::createExtraContentComponent()
{
    return std::make_unique<SawView> (*this);
}

AQUANODE_REGISTER_MODULE (SupersawModule, SupersawModule::descriptor)

// for tests
void supersawApplyPreset (SynthModule* m, int index)
{
    if (auto* s = dynamic_cast<SupersawModule*> (m)) s->applyPreset (index);
}
juce::String supersawPresetName (int index)
{
    const auto& p = supersaw::presets();
    return index >= 0 && index < (int) p.size() ? juce::String (p[(size_t) index].name) : juce::String();
}
