#include "ExtraEngines.h"
#include "../Engine/PluginProcessor.h"
#include "../Plugins/Host/HostedPlugin.h"
#include "../Modules/SoundGeneration/Oscillators/WarpOscModule.h"
#include "../Plugins/Host/RandomPresetSources.h"
#include "EffectAdders.h"

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
        juce::String lastOut { "audioOut" };   // (6.9.11: the first module's own output socket)
        void then (int id) { s.wire (last, lastOut, id, "audioIn"); last = id; lastOut = "audioOut"; }
        void finish (int column, float level)
        {
            const int out = s.add ("io.audioOut", column);
            s.set (out, "level", level);
            s.wire (last, lastOut, out, "audioIn");
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
    // (6.9.11) Physical Model, World, Drums and Experimental

    // a module's first audio output
    juce::String audioOutOf (PatchSketch& s, int id)
    {
        if (auto* m = s.dsp (id))
            for (const auto& so : m->getDescriptor().sockets)
                if (so.kind == SocketKind::Audio && so.direction == SocketDirection::Output)
                    return so.id;
        return "audioOut";
    }

    // the instrument's sound, voices summed where it plays per voice: the id
    // a Chain goes on from (a Poly Sum, or the instrument itself)
    int soundOf (PatchSketch& s, int id, int column)
    {
        auto* m = s.dsp (id);
        if (m != nullptr && m->voiceMode() != VoiceMode::Global)
        {
            const int sum = s.add ("util.polysum", column);
            s.set (sum, "amount", 0.5f);
            s.wire (id, audioOutOf (s, id), sum, "audioIn");
            return sum;
        }
        return id;
    }

    // one physical model, its knobs varied a little around the card's own sound
    int physicalModel (PatchSketch& s, Dice& d, const juce::String& type)
    {
        const int id = s.add (type, 0);
        if (id < 0) return -1;
        if (type == "osc.sitar")
        {
            s.set (id, "jawari", d.range (55.0f, 85.0f));
            s.set (id, "bright", d.range (45.0f, 75.0f));
            s.set (id, "symp", d.range (35.0f, 65.0f));
            s.set (id, "decay", d.range (5.0f, 12.0f));
        }
        else if (type == "osc.bowed")
        {
            s.set (id, "pressure", d.range (35.0f, 60.0f));
            s.set (id, "position", d.range (8.0f, 22.0f));
            s.set (id, "damp", d.range (20.0f, 50.0f));
        }
        else if (type == "osc.wind")
        {
            s.set (id, "breath", d.range (70.0f, 100.0f));
            s.set (id, "tone", d.range (30.0f, 70.0f));
            s.set (id, "noise", d.range (15.0f, 40.0f));
            s.set (id, "vibrato", d.range (10.0f, 35.0f));
        }
        else if (type == "osc.pluck")
        {
            s.setChoice (id, "model", d.chance (0.5f) ? "Harp" : "Karplus");
            s.set (id, "decay", d.jitterMul (2500.0f, 0.8f));
            s.set (id, "damp", d.range (15.0f, 45.0f));
            s.set (id, "color", d.range (55.0f, 90.0f));
        }
        else if (type == "osc.modaldrum")
        {
            s.set (id, "decay", d.jitterMul (900.0f, 0.8f));
            s.set (id, "inharm", d.range (5.0f, 45.0f));
            s.set (id, "bright", d.range (30.0f, 65.0f));
        }
        return id;
    }

    juce::String moduleName (PatchSketch& s, int id)
    {
        auto* m = s.dsp (id);
        return m != nullptr ? m->getDescriptor().displayName : juce::String();
    }

    juce::String buildPhysical (AquanodeModularAudioProcessor& p, juce::uint32 seed, bool world)
    {
        static const char* const physical[] = { "osc.sitar", "osc.bowed", "osc.wind", "osc.pluck", "osc.modaldrum",
                                                "osc.drawbarorgan", "osc.epiano", "osc.guitaracoustic", "osc.guitarelectric",
                                                "osc.strings" };
        static const char* const worldly[] = { "osc.sitar", "osc.sitar", "osc.bowed", "osc.wind", "osc.pluck", "osc.modaldrum" };
        PatchSketch s (p);
        Dice d (seed ^ 0x9b15cau);
        const juce::String type = world ? worldly[d.index (6)] : physical[d.index (10)];
        const int inst = physicalModel (s, d, type);
        if (inst < 0) return {};
        if (world)
        {
            // their own colour: the bowed one slides (an erhu's way), the pluck is a harp
            // in a pentatonic, the wind breathes like a bamboo flute
            if (type == "osc.bowed") { s.set (inst, "voices", 1.0f); s.set (inst, "glide", d.range (60.0f, 180.0f)); }
            if (type == "osc.wind")  { s.set (inst, "noise", d.range (35.0f, 55.0f)); s.set (inst, "vibrato", d.range (25.0f, 45.0f)); }
            if (type == "osc.pluck") s.setChoice (inst, "model", "Harp");
        }
        const int src = soundOf (s, inst, 1);
        Chain c { s, src, src == inst ? audioOutOf (s, inst) : juce::String ("audioOut") };
        if (! world && d.chance (0.3f)) c.then (chorus (s, 2, d, d.range (15.0f, 30.0f)));
        if (d.chance (world ? 0.35f : 0.25f)) c.then (delay (s, 2, d, "1/4.", "1/8", 8.0f, 20.0f));
        c.then (hall (s, 3, d, world ? 2.5f : 1.5f, world ? 6.0f : 4.0f, 15.0f, 30.0f));
        // (each as loud as the rest: measured, two notes held)
        float level = 0.85f;
        if (type == "osc.strings") level = 0.22f;
        if (type == "osc.modaldrum") level = 0.19f;
        c.finish (4, level);
        s.layout();
        return (world ? "World " : "Physical ") + moduleName (s, inst);
    }

    // a drum machine that plays by itself: Cove Groove rolled, or Drum Morph
    // with a kit of its own, running at the patch's tempo
    juce::String buildDrums (AquanodeModularAudioProcessor& p, juce::uint32 seed)
    {
        PatchSketch s (p);
        Dice d (seed ^ 0xd2a5u);
        const int card = s.add ("plugin.covegroove", 0);
        if (card < 0) return {};
        if (auto* h = dynamic_cast<plugins::HostedPluginModule*> (s.dsp (card)))
            if (auto* pl = h->getPlugin())
            {
                if (auto* src = plugins::randompreset::find ("covegroove"))
                    src->roll (*pl, d.index (4), 0, 0, seed | 1u);
                for (auto* prm : pl->getParameters())
                    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
                    {
                        if (withId->paramID == "run")   prm->setValueNotifyingHost (1.0f);
                        if (withId->paramID == "clock")
                            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm))
                                prm->setValueNotifyingHost (ranged->convertTo0to1 (1.0f));   // the patch's tempo
                    }
            }
        Chain c { s, card };
        if (d.chance (0.4f))
        {
            const int comp = s.add ("fx.compress", 1);
            s.set (comp, "threshold", d.range (-20.0f, -12.0f));
            s.set (comp, "ratio", d.range (2.0f, 4.0f));
            s.set (comp, "attack", d.range (10.0f, 30.0f));
            s.set (comp, "release", d.range (80.0f, 200.0f));
            s.set (comp, "makeup", 2.0f);
            c.then (comp);
        }
        c.finish (2, 0.8f);
        s.layout();
        return "Drums";
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

//==============================================================================
// Spectra Pad - remade from a reference pad (Spectra_Pads, its first ten
// seconds). What the analysis found, and how it is built here:
//   * a dense cluster of soft, nearly sine tones on six notes of the major
//     scale (root, 2, 3, 4, 5, 6 - no 7th), spread over four octaves: for C#3
//     played, G#1 C#2 C#3 F#3 A#3 D#4 F#4 G#4 as fundamentals, their few
//     harmonics filling in the rest (the 3rds come from the roots' 5th
//     harmonic) -> Graphic Midi Add with that voicing into a Warp oscillator
//     whose custom spectrum is the measured per-tone harmonic recipe
//   * every partial a cluster of copies up to +-45 cents apart, beating
//     slowly (the partials' levels wander +-6 dB at 0.1 - 0.5 Hz) -> 9 to 12
//     unison voices, +-35 - 45 cents
//   * nothing above ~1 kHz but a faint pink floor ~50 dB down -> a steep low
//     pass near 1 kHz and a whisper of pink noise per voice
//   * very wide (L/R correlation ~0.2 overall, ~0.6 in the bass) -> unison
//     spread, a gentle ensemble, a hall, Stereo Width with the lows kept mono
//   * a soft quarter-second attack, a long release
// variation 0 = the reference as measured; 1 = the Random Preset's spread.
juce::String buildSpectraPad (AquanodeModularAudioProcessor& p, juce::uint32 seed, float variation)
{
    // A remake of a reference pad (a static, very wide C# drone): measured,
    // its strongest partials are equal-tempered notes of C# major without
    // the 7th - C#, D#, F, F#, G#, A# - spread from G#1 to C#5, nothing much
    // above 900 Hz, the notes beating slowly (10 - 19 cents of unison
    // spread), a faint pink floor, wide stereo with a narrower bass, a
    // long soft hall.
    // Two layers of Warp oscillators play it: the loud notes, and the quiet
    // ones 7 dB down. Variation 0 is the measured patch; the Random Preset
    // engine varies the voicing, spread and space around it.
    PatchSketch s (p);
    Dice d (seed ^ 0x5bec7a11u);
    auto vary = [&d, variation] (float centre, float spread) { return centre + variation * d.range (-spread, spread); };

    // over the played root (which sounds in both layers): for C#3 the loud
    // layer adds G#1 C#2 F#3 C#4 D#4 F#4 G#4, the quiet one D#2 A#2 F3 A#3 F4 A#4 C#5
    struct Voicing { std::vector<int> loud, quiet; };
    static const std::vector<Voicing> voicings {
        { { -17, -12, 5, 12, 14, 17, 19 }, { -10, -3, 4, 9, 16, 21, 24 } },   // the reference
        { { -12, -5, 7, 12, 14, 16, 19 },  { -17, 4, 9, 21, 24 } },     // 6/9, the 3rd on top
        { { -17, -12, 2, 7, 12, 14, 19 },  { -5, 9, 16, 21, 26 } },     // sus 2, open
        { { -12, -5, 5, 12, 14, 17, 21 },  { -17, 2, 9, 19, 24 } },     // sus 4, a 13th
        { { -17, -12, 7, 9, 12, 16, 19 },  { -5, 4, 14, 21, 23 } } };   // with the major 7th
    const auto& v = voicings[(size_t) (variation <= 0.0f ? 0 : d.index ((int) voicings.size()))];

    const int env = s.add ("util.adsr", 0);
    s.set (env, "attack", juce::jmax (30.0f, vary (260.0f, 180.0f)));
    s.set (env, "decay", 3000.0f);
    s.set (env, "sustain", 1.0f);
    s.set (env, "release", juce::jmax (800.0f, vary (2600.0f, 1400.0f)));

    // the tone: a few harmonics, as measured on the reference's partials
    // (dB re the fundamental); varied a little per patch, the same for both layers
    std::vector<std::complex<float>> specA (1), specB (1);
    {
        static const float recipeDb[9] = { 0.0f, -11.0f, -13.0f, -21.0f, -15.0f, -25.0f, -36.0f, -30.0f, -40.0f };
        for (int h = 1; h <= 9; ++h)
        {
            const float dbA = recipeDb[h - 1] + (h > 1 ? variation * d.range (-4.0f, 4.0f) : 0.0f);
            const float dbB = dbA + (h > 1 ? d.range (-3.0f, 3.0f) : 0.0f);
            const float ph = d.range (0.0f, juce::MathConstants<float>::twoPi);
            specA.push_back (std::polar (juce::Decibels::decibelsToGain (dbA), ph));
            specB.push_back (std::polar (juce::Decibels::decibelsToGain (dbB), ph));
        }
    }
    const float detune = juce::jlimit (30.0f, 90.0f, vary (52.0f, 14.0f));
    const float blend = juce::jlimit (40.0f, 100.0f, vary (70.0f, 12.0f));
    const int unison = variation <= 0.0f ? 7 : 6 + d.index (3);

    const int sum = s.add ("util.polysum", 3);
    s.set (sum, "amount", 0.35f);

    auto layer = [&] (const std::vector<int>& chord, float volume, int column)
    {
        const int midiAdd = addChord (s, column, chord);
        const int osc = s.add ("osc.warp", column + 1);
        s.setChoice (osc, "wave", "Custom");
        s.set (osc, "shape", 0.0f);
        if (auto* w = dynamic_cast<WarpOscModule*> (s.dsp (osc)))
            w->setCustomSpectra (specA, specB);
        s.set (osc, "unison", (float) unison);
        s.set (osc, "detune", detune);
        s.set (osc, "spread", 100.0f);
        s.set (osc, "blend", blend);
        s.set (osc, "volume", volume);
        s.setChoice (osc, "morphType", "None");
        s.wire (env, "modOut", osc, "envIn");
        s.wire (midiAdd, "midiOut", osc, "addMidiIn");
        s.wire (osc, "audioOut", sum, "audioIn");
    };
    layer (v.loud, 0.7f, 0);
    layer (v.quiet, 0.7f * juce::Decibels::decibelsToGain (vary (-7.0f, 2.0f)), 0);

    // the faint pink floor, per voice - joining after the lowpasses (it is
    // flat right up to 12 kHz in the reference, some 50 dB down)
    const int noise = s.add ("osc.noise", 1);
    s.setChoice (noise, "type", "Pink");
    s.set (noise, "level", juce::jlimit (0.0f, 1.0f, vary (0.004f, 0.002f)));
    const int noiseAmp = s.add ("util.volume", 2);
    s.wire (noise, "audioOut", noiseAmp, "audioIn");
    s.wire (env, "modOut", noiseAmp, "volMod");
    const int noiseSum = s.add ("util.polysum", 3);
    s.set (noiseSum, "amount", 0.5f);
    s.wire (noiseAmp, "audioOut", noiseSum, "audioIn");

    // nothing much above 900 Hz: two 24 dB lowpasses (48 dB/oct), after the sum
    // (no chorus: its short delays comb the bass away - the unison beats instead)
    Chain c { s, sum };
    const float cutoff = vary (800.0f, 150.0f);
    for (int k = 0; k < 2; ++k)
    {
        const int lp = s.add ("filter.lowpass", 4);
        s.set (lp, "cutoff", cutoff);
        s.set (lp, "resonance", 0.0f);
        s.setChoice (lp, "slope", "24 dB/oct");
        c.then (lp);
    }
    const int h = s.add ("fx.smoothhall", 5);
    s.wire (noiseSum, "audioOut", h, "audioIn");
    s.set (h, "time", vary (4.5f, 1.5f));
    s.set (h, "preDelay", vary (25.0f, 15.0f));
    s.set (h, "highDamp", vary (5500.0f, 1500.0f));
    s.set (h, "dryWet", vary (28.0f, 8.0f));
    c.then (h);
    const int width = s.add ("fx.stereowidth", 5);
    s.set (width, "width", vary (150.0f, 15.0f));
    s.set (width, "bassMono", vary (70.0f, 20.0f));
    c.then (width);
    c.finish (6, 0.3f);   // (as loud as the other engines' pads)
    s.layout();
    return variation <= 0.0f ? "Spectra Pad (Reference)" : "Spectra Pad";
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
        // (one tap in five builds the measured remake itself, "Spectra Pad (Reference)")
        add ("Spectra", "Pad",  [] (auto& p, auto seed) { return buildSpectraPad (p, seed, Dice (seed ^ 0x2e7au).chance (0.2f) ? 0.0f : 1.0f); });
        add ("Slow FM", "Keys", [] (auto& p, auto seed) { return buildSlowFM (p, seed, 1); });
        add ("Slow FM", "Bell", [] (auto& p, auto seed) { return buildSlowFM (p, seed, 2); });

        for (const char* cat : { "Chord", "Pad", "Keys", "Bell", "Arp", "Lead", "Pluck", "Bass" })
            add ("Tidebreeze", cat, [c = juce::String (cat)] (auto& p, auto seed) { return buildTidebreeze (p, seed, c); });
        for (const char* cat : { "Pad", "Bass", "Lead", "Pluck", "Keys", "Bell", "Arp" })
            add ("Phizmo", cat, [c = juce::String (cat)] (auto& p, auto seed) { return buildPhizmo (p, seed, c); });
        for (const char* cat : { "Pad", "Bell", "Keys", "Chord" })
            add ("Atmospheric DnB", cat, [c = juce::String (cat)] (auto& p, auto seed) { return buildLiquid (p, seed, c); });
        // (6.9.11)
        add ("Models", "Physical Model", [] (auto& p, auto seed) { return buildPhysical (p, seed, false); });
        add ("Models", "World", [] (auto& p, auto seed) { return buildPhysical (p, seed, true); });
        add ("Cove Groove", "Drums", [] (auto& p, auto seed) { return buildDrums (p, seed); });
        add ("Models", "Experimental", [] (auto& p, auto seed)
        {
            // a physical model or a slow FM bell, through two of the stranger quick adds
            Dice d (seed ^ 0xe4be1u);
            auto name = d.chance (0.6f) ? buildPhysical (p, seed, false) : buildSlowFM (p, seed, 2);
            static const char* const adds[] = { "add.Spectral", "add.Fold", "add.Formant", "add.Octave Wash", "add.Lo-Fi",
                                                "add.Ambient Wash", "add.Color", "add.Band Echo", "add.Grit" };
            const int a = d.index (9);
            int b = d.index (8); if (b >= a) ++b;
            for (int k : { a, b })
                if (auto* adder = findEffectAdder (adds[k]))
                    adder->apply (p, seed + (juce::uint32) k * 7919u);
            return "Experimental " + name.fromFirstOccurrenceOf (" ", false, false);
        });
        return v;
    }();
    return list;
}

} // namespace aquanode::presets
