//==============================================================================
// Strings, Cello, Clarinet, Acoustic Guitar, Electric Guitar - the physically
// modelled instruments (Shared/PhysInstrumentEngines.h), each a module that
// plays its own notes from Midi In (its own polyphony, the instrument's body
// or amp once on the sum of its strings, so a chord costs little more than a
// note).
//
// Every one plays from the keys or from a breath: Breath 0 % is the keys alone
// (velocity, the note's own attack and release), 100 % the breath alone (a
// breath controller's CC 2, Expression CC 11, channel pressure or the mod
// wheel - Breath Source), anything between a mix. With breath, the bows' speed,
// the clarinet's blowing pressure or the guitars' volume (a swell pedal)
// follow it live. The mod wheel adds vibrato; the sustain pedal holds notes.
// MPE: each note bends with its own channel, and with Breath Source on
// Pressure each note follows its own pressure.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/PhysInstrumentEngines.h"

using namespace aquanode;
namespace pi = aquanode::physinst;

namespace
{
    const juce::StringArray kBreathSources { "Breath (CC 2)", "Expression (CC 11)", "Pressure", "Mod Wheel" };

    // what every instrument shares: the notes, the pedal, the bend, the breath
    template <class Engine>
    class PhysInstrumentModule : public SynthModule
    {
    public:
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        bool wantsRawMidi() const override { return true; }
        double voiceTailSeconds() const override { return 0.0; }

        void rawMidiMessage (const juce::MidiMessage& m) override
        {
            const int ch = juce::jlimit (1, 16, m.getChannel());
            if (m.isNoteOn())
            {
                if (ch >= 2) { chanPress[ch] = -1.0; }   // MPE: a new note's pressure starts with its own message
                noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
                sinceNoteOn = 0;
            }
            else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
            else if (m.isController())
            {
                const int cc = m.getControllerNumber();
                const double v = m.getControllerValue() / 127.0;
                if (cc == 64) engine.setSustain (v >= 0.5);
                else if (cc == 1) modWheel = v;
                else if (cc == 2) cc2 = v;
                else if (cc == 11) cc11 = v;
                else if (cc == 120 || cc == 123) engine.allNotesOff();
            }
            else if (m.isChannelPressure())
            {
                pressure = m.getChannelPressureValue() / 127.0;
                if (ch >= 2) chanPress[ch] = pressure;
            }
            else if (m.isAftertouch()) pressure = m.getAfterTouchValue() / 127.0;
            else if (m.isPitchWheel())
            {
                const double v = (m.getPitchWheelValue() - 8192) / 8192.0;
                if (ch >= 2) { chanRaw[ch] = v; bendDirty = true; }   // MPE: that note's own bend
                else ownBend = v;
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
        }

        void midiAllNotesOff() override { engine.allNotesOff(); }

        void processSample (const StereoFrame*, StereoFrame* outputs) override
        {
            pi::LiveControls c;
            const double semis = midiControls != nullptr ? (double) pitchBendSemitones() : ownBend * 2.0;
            c.bendRatio = std::pow (2.0, semis / 12.0);
            if (bendDirty || ++bendCheck >= 64)
            {
                // MPE member channels (48 semitones). With MPE off the engine bends everything
                // by such a message already: then it is not applied a second time here.
                bendDirty = false; bendCheck = 0;
                for (int k = 2; k <= 16; ++k)
                {
                    const bool global = midiControls != nullptr && std::abs ((double) midiControls->bend - chanRaw[k]) < 1.0e-4;
                    chanBendRatio[k] = chanRaw[k] == 0.0 || global ? 1.0 : std::pow (2.0, chanRaw[k] * 48.0 / 12.0);
                }
            }
            c.chanBend = chanBendRatio;
            const int src = pSource >= 0 ? (int) std::lround (param (pSource)) : 0;
            if (src == 2)
            {
                for (int k = 2; k <= 16; ++k)
                    if (chanPress[k] >= 0.0) chanBreath[k] = chanBreath[k] < 0.0 ? chanPress[k] : chanBreath[k] + breathCoeff * (chanPress[k] - chanBreath[k]);
                    else chanBreath[k] = -1.0;
                c.chanBreath = chanBreath;
            }
            const double raw = src == 1 ? cc11 : (src == 2 ? pressure : (src == 3 ? modWheel : cc2));
            breath += breathCoeff * (raw - breath);       // ~10 ms: a controller's steps smoothed
            c.breath = breath;
            c.vibScale = 1.0 + 2.0 * (src == 3 ? 0.0 : modWheel);
            double l = 0.0, r = 0.0;
            engine.process (c, l, r);
            if (sinceNoteOn < 1 << 30) ++sinceNoteOn;
            const float g = (pLevel >= 0 ? param (pLevel) : 0.7f) * outputTrim();
            outputs[0] = { (float) l * g, (float) r * g };
        }

    protected:
        virtual void noteOn (int note, float velocity, int channel) { engine.noteOn (note, velocity, channel); }

        void prepareCommon (double sr)
        {
            pLevel = paramIndex ("level");
            pSource = paramIndex ("breathSrc");
            pBreath = paramIndex ("breath");
            breathCoeff = 1.0 - std::exp (-1.0 / (0.01 * sr));
        }
        double breathAmount() const { return pBreath >= 0 ? param (pBreath) * 0.01 : 0.0; }
        float p (const char* id) const { const int i = paramIndex (id); return i >= 0 ? param (i) : 0.0f; }

        static void addBreathParams (ModuleDescriptor& d, int row)
        {
            d.params.push_back (makeRotary ("breath", "Breath", 0.0f, 100.0f, 0.0f, row, "%", false, 1.0f));
            d.params.push_back (makeCombo ("breathSrc", "Breath Source", kBreathSources, 0, row, 2).noMod());
        }

        Engine engine;
        int sinceNoteOn { 1 << 30 };
        double modWheel { 0.0 }, cc2 { 0.0 }, cc11 { 0.0 }, pressure { 0.0 }, ownBend { 0.0 }, breath { 0.0 }, breathCoeff { 0.01 };
        int pLevel { -1 }, pSource { -1 }, pBreath { -1 };
        // MPE: every channel's own bend and pressure
        double chanRaw[17] {}, chanBendRatio[17] { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        double chanPress[17] { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
        double chanBreath[17] { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
        bool bendDirty { false };
        int bendCheck { 0 };
    };

    // Decay / Sustain: after the attack the note falls to the Sustain level in the Decay
    // time (a Sustain of 100 %, the default, leaves the note as the model plays it)
    inline void addDecayParams (ModuleDescriptor& d, int row)
    {
        d.params.push_back (makeRotary ("decay", "Decay", 50.0f, 10000.0f, 1000.0f, row, "ms", true, 1.0f));
        d.params.push_back (makeRotary ("sustainLvl", "Sustain", 0.0f, 100.0f, 100.0f, row, "%", false, 1.0f));
    }

    //==========================================================================
    // the bowed strings
    //==========================================================================
    class BowedInstrument : public PhysInstrumentModule<pi::BowedEngine>
    {
    public:
        explicit BowedInstrument (bool sectionOfPlayers) : section (sectionOfPlayers) {}

        void prepare (double sr) override
        {
            SynthModule::prepare (sr);
            prepareCommon (sr);
            engine.prepare (sr, section);
            blockStart();
        }
        void reset() override { engine.reset(); }

        void blockStart() override
        {
            pi::BowedEngine::Settings s;
            if (section)
            {
                const int sec = (int) std::lround (p ("section"));
                s.instrument = sec == 0 ? pi::BowedEngine::Full : sec - 1;
                s.players = (int) std::lround (p ("players"));
                s.spread = p ("spread") * 0.01;
                s.detune = 5.0;
            }
            else
            {
                s.instrument = (int) pi::Bowed::Cello;
                s.players = 1;
                s.spread = 0.0;
                s.detune = 0.0;
                s.legato = std::lround (p ("mode")) == 1;
                s.glide = p ("glide") * 0.001;
            }
            s.pressure = p ("pressure") * 0.01;
            // the bow's distance from the bridge: 50 % is the instrument's own, each 50 % twice / half as far
            const double own = pi::instrumentInfo (s.instrument >= 0 ? (pi::Bowed) s.instrument : pi::Bowed::Violin).beta;
            s.position = std::clamp (own * std::pow (2.0, (p ("position") - 50.0) / 50.0), 0.03, 0.3);
            if (s.instrument < 0 && std::abs (p ("position") - 50.0f) < 0.5f) s.position = -1.0;   // a full section: each its own
            s.vibCents = p ("vibrato");
            s.vibRate = p ("vibRate");
            s.attack = p ("attack") * 0.001;
            s.release = p ("release") * 0.001;
            s.brightness = p ("bright") * 0.01;
            s.decay = p ("decay") * 0.001;
            s.sustain = p ("sustainLvl") * 0.01;
            // Articulation: Arco, Pizzicato, Arco Studio, Pizz Studio, Pizz Mellow, Pizz Modal
            const int artic = (int) std::lround (p ("artic"));
            s.pizzicato = artic == 1 || artic == 3 || artic == 4 || artic == 5;
            s.studio = artic == 2 ? 1 : (artic == 3 ? 2 : (artic == 4 ? 3 : (artic == 5 ? 4 : 0)));
            s.breathAmount = breathAmount();
            engine.setSettings (s);
        }

    protected:
        const bool section;

        static void addBowParams (ModuleDescriptor& d, int row)
        {
            d.params.push_back (makeRotary ("pressure", "Pressure", 0.0f, 100.0f, 50.0f, row, "%", false, 1.0f));
            d.params.push_back (makeRotary ("position", "Bow Position", 0.0f, 100.0f, 50.0f, row, "%", false, 1.0f));
            d.params.push_back (makeRotary ("bright", "Brightness", 25.0f, 200.0f, 100.0f, row, "%", false, 1.0f));
            d.params.push_back (makeRotary ("vibrato", "Vibrato", 0.0f, 40.0f, 10.0f, row + 1, "ct", false, 0.1f));
            d.params.push_back (makeRotary ("vibRate", "Vib Rate", 3.0f, 8.0f, 5.3f, row + 1, "Hz", false, 0.01f));
            d.params.push_back (makeRotary ("attack", "Attack", 15.0f, 1500.0f, 120.0f, row + 1, "ms", true, 1.0f));
            d.params.push_back (makeRotary ("release", "Release", 30.0f, 3000.0f, 150.0f, row + 1, "ms", true, 1.0f));
            addDecayParams (d, row + 1);
        }
    };

    class StringsModule final : public BowedInstrument
    {
    public:
        StringsModule() : BowedInstrument (true) {}
        static ModuleDescriptor descriptor()
        {
            ModuleDescriptor d;
            d.typeId = "osc.strings";
            d.displayName = "Strings";
            d.section = ModuleSection::Oscillator;
            d.description = "A string section, physically modelled: every note is bowed by several players (Players) - each with "
                            "their own tuning, bow weight, vibrato and entry - spread across the stereo field (Spread), through "
                            "the instruments' bodies. The bow is a friction model (stick and slip, the string's torsion, the bow "
                            "biting in at the start of a stroke) on strings fitted to recordings. Section: the whole section "
                            "(violins from C4, violas from C3, celli from C2, basses below) or one of them. Pressure presses the "
                            "bow (airy to pressed), Bow Position moves it toward the bridge (brighter) or the fingerboard. "
                            "Pizzicato plucks instead; Pizz Modal plays a pizzicato rebuilt partial by partial from a measured "
                            "cello (each partial's level and course, the body's low bloom, the finger leaving the string). Breath: 0 % plays from the keys, 100 % from a breath controller (Breath "
                            "Source) - the bows' speed follows it; the mod wheel adds vibrato, the sustain pedal holds. With MPE every note bends on its own (and, Breath Source on Pressure, follows its own pressure).";
            d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
            d.params.push_back (makeCombo ("section", "Section", { "Full Section", "Violins", "Violas", "Celli", "Basses" }, 0, 0, 2).noMod());
            d.params.push_back (makeRotary ("players", "Players", 1.0f, 8.0f, 4.0f, 0, {}, false, 1.0f).noMod());
            d.params.push_back (makeRotary ("spread", "Spread", 0.0f, 100.0f, 80.0f, 0, "%", false, 1.0f));
            d.params.push_back (makeCombo ("artic", "Articulation", { "Arco", "Pizzicato", "Arco Studio", "Pizz Studio", "Pizz Mellow", "Pizz Modal" }, 0, 0, 2).noMod());
            addBowParams (d, 1);
            addBreathParams (d, 3);
            d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 3));
            d.params.push_back (makeOutputTrim (-3.5f));   // 6.7.0: lifted to meet the other generators (counts twice: here and on the cable)
            return d;
        }
    };

    class CelloModule final : public BowedInstrument
    {
    public:
        CelloModule() : BowedInstrument (false) {}
        static ModuleDescriptor descriptor()
        {
            ModuleDescriptor d;
            d.typeId = "osc.cello";
            d.displayName = "Cello";
            d.section = ModuleSection::Oscillator;
            d.description = "A solo cello, physically modelled: a bowed string (stick and slip under the bow, the string's "
                            "torsion, the bow biting in at the start of a stroke) fitted to recordings, through a cello body "
                            "of 157 measured modes. Pressure presses the bow (airy to pressed), Bow Position moves it toward "
                            "the bridge (brighter) or the fingerboard, Vibrato comes in after the attack. Legato plays one "
                            "line: a key held over the last one glides there on the same bow (Glide). Pizzicato plucks; Pizz Modal "
                            "plays a pizzicato rebuilt partial by partial from a measured cello (each partial's level and course, "
                            "the body's low bloom, the finger leaving the string). "
                            "Breath: 0 % plays from the keys, 100 % from a breath controller (Breath Source) - the bow's speed "
                            "follows it; the mod wheel adds vibrato, the sustain pedal holds. With MPE every note bends on its own (and, Breath Source on Pressure, follows its own pressure).";
            d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
            d.params.push_back (makeCombo ("mode", "Mode", { "Poly", "Legato" }, 0, 0, 2).noMod());
            d.params.push_back (makeRotary ("glide", "Glide", 10.0f, 500.0f, 60.0f, 0, "ms", true, 1.0f));
            d.params.push_back (makeCombo ("artic", "Articulation", { "Arco", "Pizzicato", "Arco Studio", "Pizz Studio", "Pizz Mellow", "Pizz Modal" }, 0, 0, 2).noMod());
            addBowParams (d, 1);
            addBreathParams (d, 3);
            d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 3));
            d.params.push_back (makeOutputTrim (0.5f));   // 6.7.0: lifted to meet the other generators (counts twice: here and on the cable)
            return d;
        }
    };

    //==========================================================================
    // the clarinet
    //==========================================================================
    class ClarinetModule final : public PhysInstrumentModule<pi::ClarinetEngine>
    {
    public:
        static ModuleDescriptor descriptor()
        {
            ModuleDescriptor d;
            d.typeId = "osc.clarinet";
            d.displayName = "Clarinet";
            d.section = ModuleSection::Oscillator;
            d.description = "A clarinet, physically modelled: a beating reed on a bore of two waveguides with its tone holes and "
                            "register vent, the bell radiating the highs - tuned and voiced against recordings across its range "
                            "(Voicing: Refined - refitted to the recordings, with the bore's dispersion - or the Original first fit). The registers change as on the instrument (the "
                            "throat tones, the clarion and altissimo need more breath). Legato plays one line, slurring from "
                            "note to note without a new attack; Poly plays up to four. Attack is the tongue, Breath Noise the "
                            "air in the tone. Breath: 0 % plays from the keys, 100 % from a breath controller (Breath Source) - "
                            "the blowing pressure itself, so the tone darkens and brightens with it; the mod wheel adds vibrato. With MPE every note bends on its own (and, Breath Source on Pressure, follows its own pressure).";
            d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
            d.params.push_back (makeCombo ("version", "Voicing", { "Refined", "Original", "Studio" }, 0, 0, 2).noMod());
            d.params.push_back (makeCombo ("mode", "Mode", { "Legato", "Poly" }, 0, 0, 2).noMod());
            d.params.push_back (makeRotary ("glide", "Glide", 10.0f, 300.0f, 40.0f, 0, "ms", true, 1.0f));
            d.params.push_back (makeRotary ("attack", "Attack", 10.0f, 300.0f, 35.0f, 1, "ms", true, 1.0f));
            d.params.push_back (makeRotary ("release", "Release", 20.0f, 1000.0f, 70.0f, 1, "ms", true, 1.0f));
            addDecayParams (d, 1);
            d.params.push_back (makeRotary ("noise", "Breath Noise", 0.0f, 40.0f, 7.0f, 1, "%", false, 0.1f));
            d.params.push_back (makeRotary ("vibrato", "Vibrato", 0.0f, 40.0f, 0.0f, 2, "ct", false, 0.1f));
            d.params.push_back (makeRotary ("vibRate", "Vib Rate", 3.0f, 8.0f, 5.0f, 2, "Hz", false, 0.01f));
            addBreathParams (d, 3);
            d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 3));
            d.params.push_back (makeOutputTrim (1.5f));   // 6.7.0: lifted to meet the other generators (counts twice: here and on the cable)
            return d;
        }

        void prepare (double sr) override
        {
            SynthModule::prepare (sr);
            prepareCommon (sr);
            blockStart();
            engine.prepare (sr);
        }
        void reset() override { engine.reset(); }

        void blockStart() override
        {
            pi::ClarinetEngine::Settings s;
            const int voicing = (int) std::lround (p ("version"));   // Refined, Original, Studio
            s.version = voicing == 1 ? 1 : 2;
            s.studio = voicing == 2;
            s.mono = std::lround (p ("mode")) == 0;
            s.glide = p ("glide") * 0.001;
            s.attack = p ("attack") * 0.001;
            s.release = p ("release") * 0.001;
            s.breathNoise = p ("noise") * 0.01;
            s.decay = p ("decay") * 0.001;
            s.sustain = p ("sustainLvl") * 0.01;
            s.vibCents = p ("vibrato") + 20.0 * (pSource >= 0 && std::lround (param (pSource)) == 3 ? 0.0 : modWheel);
            s.vibRate = p ("vibRate");
            s.breathAmount = breathAmount();
            engine.setSettings (s);
        }
    };

    //==========================================================================
    // the guitars
    //==========================================================================
    class GuitarModule : public PhysInstrumentModule<pi::GuitarEngine>
    {
    public:
        explicit GuitarModule (bool isElectric) : electric (isElectric) {}

        void prepare (double sr) override
        {
            SynthModule::prepare (sr);
            prepareCommon (sr);
            engine.prepare (sr);
            blockStart();
        }
        void reset() override { engine.reset(); }

        void blockStart() override
        {
            pi::GuitarEngine::Settings s;
            s.electric = electric;
            s.style = (int) std::lround (p ("style"));
            if (electric)
            {
                s.drive = p ("drive") * 0.01;
                s.bass = p ("bass"); s.mid = p ("mid"); s.treble = p ("treble");
                s.presence = p ("presence") * 0.01;
                s.pickup = std::pow (2.0, (p ("pickup") - 50.0) / 50.0);
            }
            else
            {
                s.body = p ("body") * 0.01;
                s.bright = p ("bright") * 0.01;
            }
            s.pickPos = std::pow (2.0, (p ("pickPos") - 50.0) / 50.0);
            s.pick = std::pow (2.0, -(p ("pick") - 50.0) / 50.0);       // harder: a shorter pick
            s.sustain = p ("sustain") * 0.01;
            s.mute = p ("mute") * 0.01;
            s.strumMs = p ("strum");
            s.release = p ("release") * 0.001;
            s.breathAmount = breathAmount();
            engine.setSettings (s);
        }

    protected:
        void noteOn (int note, float velocity, int channel) override
        {
            engine.noteOn (note, velocity, (double) sinceNoteOn / sampleRate, channel);
        }

        static void addPlayParams (ModuleDescriptor& d, int row)
        {
            d.params.push_back (makeRotary ("pickPos", "Pick Position", 0.0f, 100.0f, 50.0f, row, "%", false, 1.0f));
            d.params.push_back (makeRotary ("pick", "Pick", 0.0f, 100.0f, 50.0f, row, "%", false, 1.0f));
            d.params.push_back (makeRotary ("sustain", "Sustain", 25.0f, 400.0f, 100.0f, row, "%", true, 1.0f));
            d.params.push_back (makeRotary ("mute", "Palm Mute", 0.0f, 100.0f, 0.0f, row + 1, "%", false, 1.0f));
            d.params.push_back (makeRotary ("strum", "Strum", 0.0f, 60.0f, 18.0f, row + 1, "ms", false, 0.1f));
            d.params.push_back (makeRotary ("release", "Release", 20.0f, 1000.0f, 80.0f, row + 1, "ms", true, 1.0f));
        }

        const bool electric;
    };

    class ElectricGuitarModule final : public GuitarModule
    {
    public:
        ElectricGuitarModule() : GuitarModule (true) {}
        static ModuleDescriptor descriptor()
        {
            ModuleDescriptor d;
            d.typeId = "osc.guitarelectric";
            d.displayName = "Electric Guitar";
            d.section = ModuleSection::Oscillator;
            d.description = "An electric guitar, physically modelled: six strings (two polarisations each, stiffness, the pick's "
                            "pulse at its position) seen by a magnetic pickup, through an amp (an oversampled clipper, a tone "
                            "stack) and a cabinet - fitted to recordings of a played guitar, from clean to driven. Style picks "
                            "one of them: Clean, Edge, Crunch, Heavy, Lead. Drive, Bass / Mid / Treble and Presence move the "
                            "amp from there, Pickup the pickup toward the neck (darker) or the bridge. Keys that come together "
                            "are strummed (Strum); a key let go mutes its string (Release), Palm Mute damps them all. Breath: "
                            "0 % plays from the keys, 100 % from a breath controller (Breath Source) as a volume swell. With MPE every note bends on its own.";
            d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
            juce::StringArray styles;
            for (int i = 0; i < pi::GuitarEngine::numStyles (true); ++i) styles.add (pi::GuitarEngine::styleName (true, i));
            d.params.push_back (makeCombo ("style", "Style", styles, 0, 0, 2).noMod());
            d.params.push_back (makeRotary ("drive", "Drive", 0.0f, 250.0f, 100.0f, 0, "%", false, 1.0f));
            d.params.push_back (makeRotary ("pickup", "Pickup", 0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f));
            d.params.push_back (makeRotary ("bass", "Bass", -12.0f, 12.0f, 0.0f, 1, "dB", false, 0.1f));
            d.params.push_back (makeRotary ("mid", "Mid", -12.0f, 12.0f, 0.0f, 1, "dB", false, 0.1f));
            d.params.push_back (makeRotary ("treble", "Treble", -12.0f, 12.0f, 0.0f, 1, "dB", false, 0.1f));
            d.params.push_back (makeRotary ("presence", "Presence", -100.0f, 100.0f, 0.0f, 1, "%", false, 1.0f));
            addPlayParams (d, 2);
            addBreathParams (d, 4);
            d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 4));
            d.params.push_back (makeOutputTrim (-1.0f));   // 6.7.0: lifted to meet the other generators (counts twice: here and on the cable)
            return d;
        }
    };

    class AcousticGuitarModule final : public GuitarModule
    {
    public:
        AcousticGuitarModule() : GuitarModule (false) {}
        static ModuleDescriptor descriptor()
        {
            ModuleDescriptor d;
            d.typeId = "osc.guitaracoustic";
            d.displayName = "Acoustic Guitar";
            d.section = ModuleSection::Oscillator;
            d.description = "A steel-string acoustic guitar, physically modelled: six strings (two polarisations each, stiffness, "
                            "the pick's pulse at its position) into the guitar's body - its air and top resonances - fitted to "
                            "recordings of a played guitar (Style: Bright Steel, Dark Steel). Body sets the air and top "
                            "resonances, Brightness the upper body. Keys that come together are strummed (Strum); a key let go "
                            "mutes its string (Release), Palm Mute damps them all. Breath: 0 % plays from the keys, 100 % from a "
                            "breath controller (Breath Source) as a volume swell. With MPE every note bends on its own.";
            d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
            juce::StringArray styles;
            for (int i = 0; i < pi::GuitarEngine::numStyles (false); ++i) styles.add (pi::GuitarEngine::styleName (false, i));
            d.params.push_back (makeCombo ("style", "Style", styles, 0, 0, 2).noMod());
            d.params.push_back (makeRotary ("body", "Body", 0.0f, 200.0f, 100.0f, 0, "%", false, 1.0f));
            d.params.push_back (makeRotary ("bright", "Brightness", 0.0f, 200.0f, 100.0f, 0, "%", false, 1.0f));
            addPlayParams (d, 1);
            addBreathParams (d, 3);
            d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 3));
            d.params.push_back (makeOutputTrim (2.0f));   // 6.7.0: lifted to meet the other generators (counts twice: here and on the cable)
            return d;
        }
    };
}

AQUANODE_REGISTER_MODULE (StringsModule, StringsModule::descriptor)
AQUANODE_REGISTER_MODULE (CelloModule, CelloModule::descriptor)
AQUANODE_REGISTER_MODULE (ClarinetModule, ClarinetModule::descriptor)
AQUANODE_REGISTER_MODULE (ElectricGuitarModule, ElectricGuitarModule::descriptor)
AQUANODE_REGISTER_MODULE (AcousticGuitarModule, AcousticGuitarModule::descriptor)
