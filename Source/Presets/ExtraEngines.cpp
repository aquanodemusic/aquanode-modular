#include "ExtraEngines.h"
#include "../Engine/PluginProcessor.h"
#include "../Plugins/Host/HostedPlugin.h"

namespace aquanode::plugins::tidebreeze { bool rollProgram (juce::AudioProcessor&, int category, juce::uint32 seed); }
namespace aquanode::plugins::phizmo     { bool randomPreset (juce::AudioProcessor&, int style, juce::uint32 seed); }

namespace aquanode::presets
{
namespace
{
    //==========================================================================
    // shared pieces

    // chords for Graphic Midi Add: semitones over the played root (which
    // always sounds), at most seven
    using Chord = std::vector<int>;
    const Chord kMin7 { 3, 7, 10 }, kMin9 { 3, 7, 10, 14 }, kMin11 { 3, 7, 10, 14, 17 },
                kMin9Spread { -12, 7, 10, 15, 14 }, kMaj7 { 4, 7, 11 }, kMaj9 { 4, 7, 11, 14 },
                kMaj7Spread { -12, 7, 11, 16 }, kSus9 { 7, 10, 14, 17 }, kLush { 4, -1, -5, -15, -27 },
                kMin6_9 { 3, 7, 9, 14 }, kDom9 { 4, 10, 14 };

    int addChord (PatchSketch& s, int column, const Chord& c)
    {
        const int id = s.add ("util.graphicmidiadd", column);
        for (int i = 0; i < 7; ++i)
            s.set (id, "note" + juce::String (i + 1), i < (int) c.size() ? (float) c[(size_t) i] : 0.0f);
        return id;
    }

    // a chain of global effects after a source
    struct Chain
    {
        PatchSketch& s;
        int last;
        void then (int id) { s.wire (last, "audioOut", id, "audioIn"); last = id; }
        void finish (int column, float level)
        {
            const int out = s.add ("io.audioOut", column);
            s.set (out, "level", level);
            s.wire (last, "audioOut", out, "audioIn");
        }
    };

    int hall (PatchSketch& s, int col, Dice& d, float tLo, float tHi, float wLo, float wHi)
    {
        const int h = s.add ("fx.smoothhall", col);
        s.set (h, "time", d.range (tLo, tHi));
        s.set (h, "preDelay", d.range (10.0f, 60.0f));
        s.set (h, "highDamp", d.range (5000.0f, 11000.0f));
        s.set (h, "dryWet", d.range (wLo, wHi));
        return h;
    }

    int chorus (PatchSketch& s, int col, Dice& d, float wet)
    {
        const int c = s.add ("fx.chorus", col);
        s.set (c, "rate", d.range (0.2f, 0.9f));
        s.set (c, "depth", d.range (30.0f, 60.0f));
        s.set (c, "spread", d.range (60.0f, 100.0f));
        s.set (c, "dryWet", wet);
        return c;
    }

    int delay (PatchSketch& s, int col, Dice& d, const char* l, const char* r, float wLo, float wHi)
    {
        const int dl = s.add ("fx.delay", col);
        s.setChoice (dl, "timeL", l);
        s.setChoice (dl, "timeR", r);
        s.set (dl, "feedback", d.range (0.25f, 0.5f));
        s.set (dl, "highPass", d.range (300.0f, 800.0f));
        s.set (dl, "dryWet", d.range (wLo, wHi));
        return dl;
    }

    int wah (PatchSketch& s, int col, Dice& d, int mode)
    {
        const int w = s.add ("fx.wah", col);
        s.setChoice (w, "mode", juce::StringArray { "Auto", "LFO", "Pedal" }[mode]);
        s.set (w, "low", d.range (320.0f, 520.0f));
        s.set (w, "high", d.range (1600.0f, 2800.0f));
        s.set (w, "resonance", d.range (3.5f, 8.0f));
        if (mode == 0)
        {
            s.set (w, "sens", d.range (45.0f, 80.0f));
            s.set (w, "attack", d.range (4.0f, 20.0f));
            s.set (w, "release", d.range (120.0f, 400.0f));
        }
        else
        {
            s.setChoice (w, "division", juce::StringArray { "1/1", "1/2", "1/2", "1/4." }[d.index (4)]);
            s.set (w, "depth", d.range (60.0f, 95.0f));
        }
        s.set (w, "mix", d.range (70.0f, 100.0f));
        return w;
    }

    //==========================================================================
    // DX operators (their envelopes are built in)

    struct Eg { float r1, r2, r3, r4, l1, l2, l3, l4; };

    int dxOp (PatchSketch& s, int col, float coarse, float fine, float level, const Eg& eg,
              int detune = 0, float velSens = 0.0f, float feedback = 0.0f)
    {
        const int op = s.add ("osc.dxop", col);
        s.set (op, "coarse", coarse);
        s.set (op, "fine", fine);
        s.set (op, "level", level);
        s.setChoice (op, "detune", detune > 0 ? "+" + juce::String (detune) : juce::String (detune));
        s.set (op, "velSens", velSens);
        s.set (op, "feedback", feedback);
        s.set (op, "r1", eg.r1); s.set (op, "r2", eg.r2); s.set (op, "r3", eg.r3); s.set (op, "r4", eg.r4);
        s.set (op, "l1", eg.l1); s.set (op, "l2", eg.l2); s.set (op, "l3", eg.l3); s.set (op, "l4", eg.l4);
        return op;
    }

    // a two-operator stack summed into `sum`; returns the carrier
    int fmPair (PatchSketch& s, int sum, float carCoarse, float carFine, const Eg& carEg,
                float modCoarse, float modFine, float modLevel, const Eg& modEg,
                int detune, float modVel, float feedback, int carriers)
    {
        const int mod = dxOp (s, 0, modCoarse, modFine, modLevel, modEg, detune, modVel, feedback);
        const int car = dxOp (s, 1, carCoarse, carFine, 99.0f, carEg, detune, 1.0f);
        s.setChoice (car, "com", "/" + juce::String (carriers));
        s.wire (mod, "audioOut", car, "fmIn");
        s.wire (car, "audioOut", sum, "audioIn");
        return car;
    }

    // the chord in front of every operator's Add Midi In
    void chordInto (PatchSketch& s, int chord)
    {
        for (int id : s.processor().getModuleIds())
            if (auto* inst = s.processor().getInstance (id))
                if (inst->descriptor().typeId == "osc.dxop" || inst->descriptor().typeId == "osc.warp")
                    s.wire (chord, "midiOut", id, "addMidiIn");
    }

    //==========================================================================
    // Psychill squelches: a mono saw through a screaming ladder with a snapping filter
    // envelope - rolling 1/16 bass lines, squelchy leads, acid arps

    juce::String buildPsy (AquanodeModularAudioProcessor& p, juce::uint32 seed, int kind)   // 0 bass, 1 lead, 2 arp
    {
        PatchSketch s (p);
        Dice d (seed ^ 0x9517u);

        const int ampEnv = s.add ("util.adsr", 0);
        s.set (ampEnv, "attack", 1.0f);
        s.set (ampEnv, "decay", kind == 1 ? d.range (400.0f, 1200.0f) : d.range (110.0f, 260.0f));
        s.set (ampEnv, "sustain", kind == 1 ? d.range (0.5f, 0.85f) : d.range (0.0f, 0.25f));
        s.set (ampEnv, "release", d.range (15.0f, 45.0f));

        const int filtEnv = s.add ("util.adsr", 0);
        s.set (filtEnv, "attack", 1.0f);
        s.set (filtEnv, "decay", kind == 0 ? d.range (55.0f, 150.0f) : d.range (90.0f, 320.0f));
        s.set (filtEnv, "sustain", kind == 1 ? d.range (0.1f, 0.3f) : 0.0f);
        s.set (filtEnv, "release", 40.0f);

        const int osc = s.add ("osc.warp", 1);
        s.set (osc, "shape", d.chance (0.75f) ? 50.0f : 75.0f);
        s.set (osc, "unison", kind == 1 ? 3.0f : 1.0f);
        s.set (osc, "detune", d.range (8.0f, 18.0f));
        s.set (osc, "voices", 1.0f);
        s.set (osc, "glide", kind == 1 ? d.range (20.0f, 70.0f) : 0.0f);
        s.set (osc, "volume", 0.8f);
        s.set (osc, "transpose", kind == 0 ? -12.0f : (kind == 1 ? 12.0f : 0.0f));
        s.setChoice (osc, "morphType", "None");
        if (d.chance (0.35f))
        {
            s.setChoice (osc, "warpMode", d.chance (0.5f) ? "Sync" : "Bend");
            s.set (osc, "warp", d.range (10.0f, 45.0f));
        }
        s.wire (ampEnv, "modOut", osc, "envIn");

        const int ladder = s.add ("filter.ladder", 2);
        s.setChoice (ladder, "mode", "24dB LP");
        s.set (ladder, "cutoff", kind == 0 ? d.range (110.0f, 320.0f) : d.range (260.0f, 900.0f));
        s.set (ladder, "resonance", kind == 0 ? d.range (0.55f, 0.82f) : d.range (0.78f, 0.95f));
        s.set (ladder, "drive", d.range (1.6f, 3.2f));
        s.set (ladder, "modDepth", d.range (55.0f, 90.0f));
        s.set (ladder, "outputVolume", 1.0f);
        s.wire (osc, "audioOut", ladder, "audioIn");
        s.wire (filtEnv, "modOut", ladder, "cutoffMod");

        const int sum = s.add ("util.polysum", 3);
        s.set (sum, "amount", 0.0f);
        s.wire (ladder, "audioOut", sum, "audioIn");
        Chain c { s, sum };

        if (d.chance (kind == 0 ? 0.5f : 0.8f))
        {
            const int sh = s.add ("fx.waveshaper", 4);
            s.set (sh, "drive", d.range (2.0f, 7.0f));
            s.set (sh, "out", 0.7f);
            s.set (sh, "dryWet", d.range (40.0f, 90.0f));
            c.then (sh);
        }
        if (kind != 0 && d.chance (0.6f))
            c.then (wah (s, 4, d, 0));   // more quack on every note
        if (kind != 0)
            c.then (delay (s, 4, d, d.chance (0.5f) ? "1/8" : "1/16", "1/8", 15.0f, 32.0f));
        if (kind == 1 || d.chance (0.3f))
        {
            const int rv = s.add ("fx.reverb", 4);
            s.set (rv, "roomSize", d.range (35.0f, 70.0f));
            s.set (rv, "decay", d.range (1.2f, 3.0f));
            s.set (rv, "dryWet", kind == 0 ? d.range (5.0f, 10.0f) : d.range (14.0f, 26.0f));
            c.then (rv);
        }

        // the roll: 1/16 notes of whatever is held
        if (kind != 1)
        {
            const int arp = s.add ("util.arp", 0);
            s.setChoice (arp, "division", "1/16");
            s.setChoice (arp, "mode", kind == 0 ? "As Played" : juce::StringArray { "Up", "Up/Down", "Random" }[d.index (3)]);
            s.set (arp, "octaves", kind == 0 ? 1.0f : 2.0f);
            s.set (arp, "gate", kind == 0 ? d.range (45.0f, 65.0f) : d.range (50.0f, 90.0f));
            s.wire (arp, "midiOut", osc, "addMidiIn");
        }

        c.finish (5, kind == 1 ? 0.7f : 0.9f);
        s.layout();
        return kind == 0 ? "Psy Rolling Bass" : (kind == 1 ? "Psy Squelch Lead" : "Psy Acid Arp");
    }

    //==========================================================================
    // Slow FM: DX operator pairs with slow envelopes, detuned against each
    // other - pads, glassy keys and long bells

    juce::String buildSlowFM (AquanodeModularAudioProcessor& p, juce::uint32 seed, int kind)   // 0 pad, 1 keys, 2 bell
    {
        PatchSketch s (p);
        Dice d (seed ^ 0xf3a1u);
        const int sum = s.add ("util.polysum", 2);
        s.set (sum, "amount", 0.5f);

        if (kind == 0)
        {
            const float att = d.range (22.0f, 42.0f);   // DX rate: lower is slower
            const Eg car { att, d.range (18.0f, 30.0f), 25.0f, d.range (28.0f, 40.0f), 99.0f, d.range (88.0f, 96.0f), d.range (85.0f, 93.0f), 0.0f };
            const Eg mod { att - d.range (0.0f, 10.0f), d.range (15.0f, 28.0f), 20.0f, 35.0f, 90.0f, d.range (60.0f, 80.0f), d.range (55.0f, 75.0f), 0.0f };
            const float ratios[] = { 1.0f, 2.0f, 1.0f, 3.0f };
            const float r = ratios[d.index (4)];
            fmPair (s, sum, 1, 0, car, r, 0, d.range (58.0f, 74.0f), mod, -2, 0, d.range (0.0f, 3.0f), 2);
            fmPair (s, sum, 1, 0, car, d.chance (0.5f) ? r : 2.0f, d.chance (0.3f) ? 1.0f : 0.0f, d.range (52.0f, 70.0f), mod, 2, 0, 0, 2);
        }
        else if (kind == 1)
        {
            const Eg car { 95.0f, d.range (22.0f, 34.0f), d.range (15.0f, 25.0f), d.range (35.0f, 50.0f), 99.0f, d.range (75.0f, 88.0f), 0.0f, 0.0f };
            const Eg mod { 95.0f, d.range (30.0f, 50.0f), 25.0f, 45.0f, 99.0f, d.range (40.0f, 60.0f), 0.0f, 0.0f };
            fmPair (s, sum, 1, 0, car, 1, 0, d.range (68.0f, 80.0f), mod, -1, 3, d.range (0.0f, 4.0f), 2);
            fmPair (s, sum, 1, 0, car, d.chance (0.5f) ? 14.0f : 7.0f, 0, d.range (45.0f, 58.0f),
                    Eg { 99.0f, d.range (60.0f, 75.0f), 50.0f, 60.0f, 99.0f, 0.0f, 0.0f, 0.0f }, 1, 4, 0, 2);
        }
        else
        {
            const Eg car { 99.0f, d.range (24.0f, 34.0f), d.range (14.0f, 22.0f), d.range (22.0f, 34.0f), 99.0f, d.range (70.0f, 82.0f), 0.0f, 0.0f };
            const Eg mod { 99.0f, d.range (28.0f, 40.0f), 20.0f, 30.0f, 99.0f, d.range (55.0f, 70.0f), 0.0f, 0.0f };
            // inharmonic ratios: 3.5, 1.41, 2.76...
            const float fines[] = { 17.0f, 41.0f, 38.0f, 0.0f };
            const float coarses[] = { 3.0f, 1.0f, 2.0f, 5.0f };
            const int k = d.index (4);
            fmPair (s, sum, 1, 0, car, coarses[k], fines[k], d.range (68.0f, 82.0f), mod, 0, 2, 0, 2);
            fmPair (s, sum, 2, 0, car, d.chance (0.5f) ? 7.0f : 9.0f, 0, d.range (50.0f, 64.0f), mod, 3, 2, 0, 2);
        }

        Chain c { s, sum };
        if (kind == 0) c.then (chorus (s, 3, d, d.range (25.0f, 45.0f)));
        if (kind == 1 && d.chance (0.5f)) c.then (chorus (s, 3, d, d.range (15.0f, 30.0f)));
        if (kind == 2 || d.chance (0.4f)) c.then (delay (s, 3, d, "1/4", d.chance (0.5f) ? "1/2" : "1/8", 10.0f, 25.0f));
        c.then (hall (s, 3, d, kind == 1 ? 1.8f : 4.0f, kind == 1 ? 3.5f : 8.5f, kind == 1 ? 18.0f : 30.0f, kind == 1 ? 30.0f : 48.0f));
        c.finish (4, 0.8f);
        s.layout();
        return kind == 0 ? "Slow FM Pad" : (kind == 1 ? "Slow FM Keys" : "Slow FM Bell");
    }

    //==========================================================================
    // Tidebreeze: the card rolled in a category; a chord in front for the
    // Chord row (and often for the pads and keys)

    juce::String buildTidebreeze (AquanodeModularAudioProcessor& p, juce::uint32 seed, const juce::String& cat)
    {
        enum { Keyboard, Organ, Bell, Strings, Vocal, SlowSynth, FastSynth, Lead, Motion, Bass, Water, Arp, Breeze };
        std::vector<int> cats;
        float chordChance = 0.0f;
        if (cat == "Chord")     { cats = { Keyboard, Strings, SlowSynth, Breeze, Organ }; chordChance = 1.0f; }
        else if (cat == "Pad")  { cats = { SlowSynth, Breeze, Strings, Vocal, Motion, Water }; chordChance = 0.5f; }
        else if (cat == "Keys") { cats = { Keyboard, Keyboard, Organ }; chordChance = 0.4f; }
        else if (cat == "Bell") { cats = { Bell }; chordChance = 0.4f; }
        else if (cat == "Arp")  cats = { Arp };
        else if (cat == "Lead") cats = { Lead };
        else if (cat == "Pluck") cats = { FastSynth };
        else                    cats = { Bass };

        PatchSketch s (p);
        Dice d (seed ^ 0x71deu);
        const int card = s.add ("plugin.tidebreeze", 1);
        if (card < 0) return {};
        if (auto* h = dynamic_cast<plugins::HostedPluginModule*> (s.dsp (card)))
            if (auto* pl = h->getPlugin())
                plugins::tidebreeze::rollProgram (*pl, cats[(size_t) d.index ((int) cats.size())], seed);

        bool chord = d.chance (chordChance);
        if (chord)
        {
            const Chord* chords[] = { &kLush, &kMin9, &kMaj9, &kMin11, &kMaj7Spread, &kMin9Spread, &kSus9 };
            s.wire (addChord (s, 0, *chords[d.index (7)]), "midiOut", card, "midiIn");
        }

        Chain c { s, card };
        if (cat == "Keys" && d.chance (0.35f)) c.then (wah (s, 2, d, 1));
        if (d.chance (0.5f)) c.then (hall (s, 2, d, 2.5f, 6.0f, 15.0f, 30.0f));
        c.finish (3, chord ? 0.7f : 0.85f);
        s.layout();
        return chord ? "Tidebreeze Chord" : "Tidebreeze " + cat;
    }

    //==========================================================================
    // Phizmo: one of the logo's random presets of the matching style

    juce::String buildPhizmo (AquanodeModularAudioProcessor& p, juce::uint32 seed, const juce::String& cat)
    {
        enum { RandPad, RandBass, RandLead, RandPluck, RandKeys, RandBell, RandTexture, RandArp };
        int style = RandPad;
        if (cat == "Bass") style = RandBass;
        else if (cat == "Lead") style = RandLead;
        else if (cat == "Pluck") style = RandPluck;
        else if (cat == "Keys") style = RandKeys;
        else if (cat == "Bell") style = RandBell;
        else if (cat == "Arp") style = RandArp;
        else if (cat == "FX") style = RandTexture;

        PatchSketch s (p);
        Dice d (seed ^ 0x9412u);
        const int card = s.add ("plugin.phizmo", 1);
        if (card < 0) return {};
        if (auto* h = dynamic_cast<plugins::HostedPluginModule*> (s.dsp (card)))
            if (auto* pl = h->getPlugin())
                plugins::phizmo::randomPreset (*pl, style, seed);

        if (cat == "Keys" && d.chance (0.4f))
            s.wire (addChord (s, 0, d.chance (0.5f) ? kMin9 : kMin7), "midiOut", card, "midiIn");
        Chain c { s, card };
        if ((cat == "Pad" || cat == "Bell") && d.chance (0.5f)) c.then (hall (s, 2, d, 3.0f, 7.0f, 15.0f, 30.0f));
        c.finish (3, 0.8f);
        s.layout();
        return "Phizmo " + cat;
    }

    //==========================================================================
    // Liquid (atmospheric drum & bass, think Aural Imbalance, ASC): wide warm
    // pads, 7th and 9th bells, minor e-piano chords through a slow wah -
    // everything in long halls and dotted echoes

    int liquidPadVoice (PatchSketch& s, Dice& d)
    {
        const int env = s.add ("util.adsr", 0);
        s.set (env, "attack", d.range (500.0f, 1800.0f));
        s.set (env, "decay", d.range (1500.0f, 4000.0f));
        s.set (env, "sustain", d.range (0.7f, 0.95f));
        s.set (env, "release", d.range (2500.0f, 5500.0f));

        const int osc = s.add ("osc.warp", 1);
        s.set (osc, "shape", d.chance (0.6f) ? 50.0f : d.range (20.0f, 40.0f));
        s.set (osc, "unison", (float) (5 + d.index (3)));
        s.set (osc, "detune", d.range (12.0f, 28.0f));
        s.set (osc, "spread", 100.0f);
        s.set (osc, "volume", 0.6f);
        s.setChoice (osc, "morphType", "None");
        s.wire (env, "modOut", osc, "envIn");

        const int lfo = s.add ("util.lfo", 0);
        s.set (lfo, "rate", d.range (0.06f, 0.25f));
        s.setChoice (lfo, "waveform", "Triangle");

        const int f = s.add ("filter.svf", 2);
        s.setChoice (f, "mode", "Lowpass");
        s.set (f, "cutoff", d.range (900.0f, 2600.0f));
        s.set (f, "resonance", d.range (0.1f, 0.35f));
        s.set (f, "modDepth", d.range (12.0f, 30.0f));
        s.wire (osc, "audioOut", f, "audioIn");
        s.wire (lfo, "modOut", f, "cutoffMod");
        return f;
    }

    juce::String buildLiquid (AquanodeModularAudioProcessor& p, juce::uint32 seed, const juce::String& cat)
    {
        PatchSketch s (p);
        Dice d (seed ^ 0x11d0u);
        const int sum = s.add ("util.polysum", 3);
        s.set (sum, "amount", 0.5f);
        juce::String name;
        const Chord* chord = nullptr;

        if (cat == "Pad")
        {
            s.wire (liquidPadVoice (s, d), "audioOut", sum, "audioIn");
            const Chord* chords[] = { &kMin9, &kMin11, &kMaj9, &kMin9Spread, nullptr };
            chord = chords[d.index (5)];
            name = "Atmospheric Pad";
        }
        else if (cat == "Bell")
        {
            const Eg car { 99.0f, d.range (26.0f, 36.0f), d.range (16.0f, 24.0f), d.range (24.0f, 34.0f), 99.0f, d.range (72.0f, 84.0f), 0.0f, 0.0f };
            const Eg mod { 99.0f, d.range (30.0f, 42.0f), 22.0f, 30.0f, 99.0f, d.range (50.0f, 68.0f), 0.0f, 0.0f };
            fmPair (s, sum, 1, 0, car, 3, 17, d.range (66.0f, 78.0f), mod, -1, 3, 0, 2);
            fmPair (s, sum, 1, 0, car, 1, 0, d.range (60.0f, 70.0f), mod, 2, 2, d.range (0.0f, 3.0f), 2);
            const Chord* chords[] = { &kMaj7, &kMin7, &kMaj9, &kMin9, &kDom9 };
            chord = chords[d.index (5)];
            name = chord == &kMaj7 || chord == &kMaj9 ? "Atmospheric Maj7 Bell" : "Atmospheric 7th Bell";
        }
        else   // Keys, Chord: the minor e-piano
        {
            const Eg car { 96.0f, d.range (24.0f, 32.0f), d.range (18.0f, 24.0f), d.range (40.0f, 55.0f), 99.0f, d.range (78.0f, 88.0f), 0.0f, 0.0f };
            const Eg body { 95.0f, d.range (34.0f, 46.0f), 25.0f, 55.0f, 99.0f, d.range (55.0f, 68.0f), 0.0f, 0.0f };
            const Eg tine { 99.0f, d.range (62.0f, 74.0f), 50.0f, 60.0f, 99.0f, 0.0f, 0.0f, 0.0f };
            fmPair (s, sum, 1, 0, car, 1, 0, d.range (70.0f, 79.0f), body, -1, 4, d.range (0.0f, 3.0f), 2);
            fmPair (s, sum, 1, 0, car, 14, 0, d.range (50.0f, 60.0f), tine, 1, 5, 0, 2);
            const Chord* chords[] = { &kMin9, &kMin7, &kMin11, &kMin6_9, &kMin9Spread };
            chord = cat == "Chord" || d.chance (0.7f) ? chords[d.index (5)] : nullptr;
            name = chord != nullptr ? "Atmospheric Minor Keys" : "Atmospheric E-Piano";
        }
        if (chord != nullptr)
            chordInto (s, addChord (s, 0, *chord));

        Chain c { s, sum };
        if (cat == "Keys" || cat == "Chord")
        {
            c.then (wah (s, 4, d, d.chance (0.7f) ? 1 : 0));
            c.then (chorus (s, 4, d, d.range (20.0f, 40.0f)));
        }
        else if (cat == "Pad")
        {
            const int ens = s.add ("fx.ensemble", 4);
            s.set (ens, "mix", d.range (40.0f, 70.0f));
            c.then (ens);
        }
        if (cat != "Pad" || d.chance (0.5f))
            c.then (delay (s, 4, d, "1/4", d.chance (0.5f) ? "1/8" : "1/2", 10.0f, 22.0f));
        c.then (hall (s, 4, d, cat == "Pad" ? 5.0f : 3.0f, cat == "Pad" ? 9.5f : 6.5f, cat == "Pad" ? 35.0f : 25.0f, cat == "Pad" ? 55.0f : 42.0f));
        const int width = s.add ("fx.stereowidth", 4);
        s.set (width, "width", d.range (120.0f, 160.0f));
        s.set (width, "bassMono", 150.0f);
        c.then (width);
        const bool keys = cat == "Keys" || cat == "Chord";
        c.finish (5, keys ? (chord != nullptr ? 1.1f : 1.5f) : (chord != nullptr ? 0.6f : 0.8f));
        s.layout();
        return name;
    }
}

const std::vector<ExtraEngine>& extraEngines()
{
    static const std::vector<ExtraEngine> list = []
    {
        std::vector<ExtraEngine> v;
        auto add = [&v] (const char* engine, const char* cat, std::function<juce::String (AquanodeModularAudioProcessor&, juce::uint32)> f)
        {
            v.push_back ({ engine, cat, std::move (f) });
        };

        add ("Psychill", "Bass", [] (auto& p, auto seed) { return buildPsy (p, seed, 0); });
        add ("Psychill", "Lead", [] (auto& p, auto seed) { return buildPsy (p, seed, 1); });
        add ("Psychill", "Arp",  [] (auto& p, auto seed) { return buildPsy (p, seed, 2); });

        add ("Slow FM", "Pad",  [] (auto& p, auto seed) { return buildSlowFM (p, seed, 0); });
        add ("Slow FM", "Keys", [] (auto& p, auto seed) { return buildSlowFM (p, seed, 1); });
        add ("Slow FM", "Bell", [] (auto& p, auto seed) { return buildSlowFM (p, seed, 2); });

        for (const char* cat : { "Chord", "Pad", "Keys", "Bell", "Arp", "Lead", "Pluck", "Bass" })
            add ("Tidebreeze", cat, [c = juce::String (cat)] (auto& p, auto seed) { return buildTidebreeze (p, seed, c); });
        for (const char* cat : { "Pad", "Bass", "Lead", "Pluck", "Keys", "Bell", "Arp" })
            add ("Phizmo", cat, [c = juce::String (cat)] (auto& p, auto seed) { return buildPhizmo (p, seed, c); });
        for (const char* cat : { "Pad", "Bell", "Keys", "Chord" })
            add ("Atmospheric DnB", cat, [c = juce::String (cat)] (auto& p, auto seed) { return buildLiquid (p, seed, c); });
        return v;
    }();
    return list;
}

} // namespace aquanode::presets
