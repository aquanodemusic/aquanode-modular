//==============================================================================
// Electric Piano (6.7.7) - a tine piano in the Rhodes manner, modelled, with
// the classic chain after it: tone, a tube drive, a four-stage phaser and the
// Suitcase's stereo vibrato (an auto-pan).
//
// Each key: a hammer strikes a tine. Its swing - the fundamental, falling
// fast at first and then slowly (low keys ring for many seconds, high ones
// briefly), and the tine's second mode, a bright "bell" near seven times the
// pitch that dies within a fraction of a second - passes a magnetic pickup.
// The pickup sits a little off the tine's line, and what it gives is the
// change of its magnetic field: a soft touch swings the tine little and comes
// out almost pure, a hard one swings it near the pickup and comes out
// growling with harmonics (the "bark"), which mellow as the note dies away.
// A short thump of the hammer's felt; the damper ends a key let go (Release),
// the sustain pedal holds them. Bark moves the pickup closer, Bell sets the
// second mode, Decay the length, Velocity how much the touch matters.
//
// After the sum of the keys (one chain for all, so a chord costs little more
// than a note): Tone (a low-pass, the amp's treble), Drive (an oversampled
// asymmetric tube clipper, level kept), the phaser (four all-pass stages swept
// by an LFO, a little feedback - Phaser Rate, Phaser Mix) and the stereo
// vibrato (a soft-square LFO panning left and right - Pan Rate, Pan Depth).
// The defaults follow a recorded Suitcase piano: warm, the pan near 3 Hz.
//
// 6.7.9: the tine speaks a little more (a longer bell and two short pings of
// the tine's other modes, near 1.5 and 2.4 times the pitch, as a recorded
// Mark I has them) and the drive is warm rather than harsh - the highs go
// into the tube tilted down, a speaker's roll-off comes after. Engine:
// Classic is the piano as it was; Mellow follows a darker recorded Rhodes -
// the pickup further off and nearer the tine's line (low keys swing wide and
// hum with their second harmonic, high ones come out near pure), a softer
// hammer, and a drive that warms only the lows and mids while the tine
// passes it clean.
//
// Global (its own polyphony: 24 voices). Input: Midi In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class ElectricPianoModule final : public SynthModule
{
public:
    enum ParamIndex { pTone = 0, pBark, pBell, pDecay, pRelease, pDrive, pPhRate, pPhMix, pPanRate, pPanDepth, pVelocity, pLevel, pEngine };

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    double voiceTailSeconds() const override { return 0.0; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fs = juce::jmax (8000.0, sr);
        for (auto& v : voices) v.active = false;
        resetFx();
    }

    void reset() override
    {
        for (auto& v : voices) v.active = false;
        resetFx();
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (cc == 64)
            {
                sustainPedal = m.getControllerValue() >= 64;
                if (! sustainPedal)
                    for (auto& v : voices) if (v.active && v.pedalHeld) { v.pedalHeld = false; v.releasing = true; }
            }
            else if (cc == 120 || cc == 123) allOff();
        }
        else if (m.isPitchWheel()) ownBend = (m.getPitchWheelValue() - 8192) / 8192.0;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }

    void midiAllNotesOff() override { allOff(); }

    void blockStart() override
    {
        // the knobs, once a block
        const int e = juce::jlimit (0, 2, (int) std::lround (param (pEngine)));
        if (e != engine)
        {
            engine = e;
            // Studio (6.9.1) plays through the Mellow chain: the drive warms the lows, the tine passes clean
            mellow = engine >= 1;
            studio = engine == 2;
            // the pickup: where the tine's line passes it (gaps beside it, in front); Studio sets it per key
            offset = engine == 1 ? 0.42 : 0.7;
            height = engine == 1 ? 0.62 : 0.5;
        }
        const float bark = param (pBark) * 0.01f;
        swing = 0.45f + 0.65f * bark;                 // how far a full blow swings the tine, in pickup gaps
        bellAmt = param (pBell) * 0.01f;
        decayScale = param (pDecay) * 0.01f;
        releaseCoeff = std::exp (-1.0 / (juce::jmax (5.0f, param (pRelease)) * 0.001 * fs));
        velSens = param (pVelocity) * 0.01f;
        const double semis = midiControls != nullptr ? (double) pitchBendSemitones() : ownBend * 2.0;
        bendRatio = std::pow (2.0, semis / 12.0);

        // Tone: a 2-pole low-pass (the amp's treble)
        toneLp.set (juce::jlimit (300.0, 0.45 * fs, (double) param (pTone)), 0.6, fs);
        const float driveDb = param (pDrive) * 0.01f * (mellow ? 26.0f : 28.0f);
        drive = juce::Decibels::decibelsToGain (driveDb);
        // the level kept as the drive rises (measured: the loudness from 0 to 100 %; Mellow
        // a touch up, to sit level with Classic)
        const float keep = mellow ? 0.762f - 0.0139f * driveDb - juce::jlimit (0.0f, 0.017f, 0.0034f * (driveDb - 15.6f))
                                  : 0.811f - 0.0213f * driveDb + 0.0068f * juce::jmax (0.0f, driveDb - 11.0f);
        driveMakeup = (mellow ? 1.1f : 1.0f) / std::pow (drive, keep);
        phInc = param (pPhRate) / fs;
        phMix = param (pPhMix) * 0.01f * 0.5f;        // 100 %: wet and dry equal - the deepest notches
        panInc = param (pPanRate) / fs;
        panDepth = param (pPanDepth) * 0.01f * 0.8f;
        level = param (pLevel);   // (the Output Trim is the engine's, on the cable)
    }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        // the keys
        double sum = 0.0;
        for (auto& v : voices)
            if (v.active)
                sum += renderVoice (v);

        // the chain, on the sum
        double x = toneLp.process (sum);
        x = driveStage (x);
        x = dcBlock (x);

        // the phaser: four all-passes swept between ~160 Hz and ~1.8 kHz
        phPhase += phInc; if (phPhase >= 1.0) phPhase -= 1.0;
        const double sweepL = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * phPhase);
        const double sweepR = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (phPhase + 0.08));   // (a touch of width)
        const double l = phaser (phL, x, sweepL), r = phaser (phR, x, sweepR);

        // the stereo vibrato: a soft-square LFO, left up while right goes down
        panPhase += panInc; if (panPhase >= 1.0) panPhase -= 1.0;
        const double s = std::tanh (2.2 * std::sin (juce::MathConstants<double>::twoPi * panPhase)) / std::tanh (2.2);
        const double gl = 1.0 + panDepth * s, gr = 1.0 - panDepth * s;
        outputs[0] = { (float) (l * gl * level), (float) (r * gr * level) };
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.epiano";
        d.displayName = "Electric Piano";
        d.section = ModuleSection::Oscillator;
        d.sidebarOrder = 4;
        d.searchKeywords = "rhodes electric piano tine suitcase phaser drive epiano keys";
        d.description =
            "A tine electric piano in the Rhodes manner, modelled: a hammer on a tine (its fundamental, falling fast then "
            "slowly, and a short bright bell partial), a magnetic pickup a little off the tine's line - a soft touch comes "
            "out almost pure, a hard one barks with harmonics that mellow as the note dies - the felt's thump and the "
            "damper. Then the classic chain: Tone, a tube Drive, a four-stage phaser (Phaser: how much, Ph. Rate: how fast) and the "
            "Suitcase's stereo vibrato (Pan Rate, Pan Depth). Bark moves the pickup closer, Bell sets the bell partial "
            "and the tine's ping, Decay the length, Release the damper, Velocity how much the touch matters. Engine: "
            "Classic, Mellow - a darker, rounder piano whose drive warms only the lows and mids - or Studio, fitted key by key to a "
            "recorded Rhodes: long sustain, the second harmonic taking over in the middle when struck hard. The sustain pedal holds notes.";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
        d.params = {
            makeRotary ("tone",     "Tone",        300.0f, 12000.0f, 3000.0f, 0, "Hz", true, 1.0f),
            makeRotary ("bark",     "Bark",          0.0f,   100.0f,   50.0f, 0, "%",  false, 1.0f),
            makeRotary ("bell",     "Bell",          0.0f,   100.0f,   40.0f, 0, "%",  false, 1.0f),
            makeRotary ("decay",    "Decay",        25.0f,   300.0f,  100.0f, 0, "%",  false, 1.0f),
            makeRotary ("release",  "Release",      10.0f,  1500.0f,   90.0f, 0, "ms", true, 1.0f),
            makeRotary ("drive",    "Drive",         0.0f,   100.0f,   22.0f, 1, "%",  false, 1.0f),
            makeRotary ("phRate",   "Ph. Rate",      0.05f,    6.0f,    0.5f, 1, "Hz", true, 0.01f),   // the phaser's speed
            makeRotary ("phMix",    "Phaser",        0.0f,   100.0f,   55.0f, 1, "%",  false, 1.0f),  // how much of it
            makeRotary ("panRate",  "Pan Rate",      0.1f,    10.0f,    2.9f, 1, "Hz", true, 0.01f),
            makeRotary ("panDepth", "Pan Depth",     0.0f,   100.0f,   30.0f, 1, "%",  false, 1.0f),
            makeRotary ("velocity", "Velocity",      0.0f,   100.0f,   80.0f, 2, "%",  false, 1.0f),
            makeRotary ("level",    "Level",         0.0f,     1.0f,    0.7f, 2),
            // 6.7.9: Classic, or Mellow - darker, rounder, the drive warming only the lows
            makeCombo ("engine", "Engine", { "Classic", "Mellow", "Studio" }, 0, 2, 2).noMod().whenMissing (0.0f)
        };
        d.params.push_back (makeOutputTrim (-1.5f));   // (beside the Grand and the other keys)
        return d;
    }

private:
    //=== a key ================================================================
    struct Voice
    {
        bool active { false }, releasing { false }, pedalHeld { false };
        int note { -1 };
        juce::uint32 age { 0 };
        double c1 { 1.0 }, s1 { 0.0 }, rc1 { 1.0 }, rs1 { 0.0 };      // the fundamental: a rotating phasor
        double c2 { 1.0 }, s2 { 0.0 }, rc2 { 1.0 }, rs2 { 0.0 };      // the bell
        double fast { 0.0 }, slow { 0.0 }, fastK { 1.0 }, slowK { 1.0 };   // the two stages of the fall
        double bell { 0.0 }, bellK { 1.0 };
        double attack { 0.0 }, attackK { 0.0 };                        // the hammer's contact (a few ms)
        double rel { 1.0 };
        double thump { 0.0 }, thumpK { 0.0 }, thumpLp { 0.0 };
        double prevFlux { 0.0 }, gain { 1.0 };
        double f0 { 440.0 }, bendSeen { 1.0 }, bellRatio { 7.0 };
        int renorm { 0 };
        juce::uint32 noise { 12345 };
        // 6.7.9: the tine's other modes, a short ping at the strike
        double c3 { 1.0 }, s3 { 0.0 }, rc3 { 1.0 }, rs3 { 0.0 }, ratio3 { 1.52 };
        double c4 { 1.0 }, s4 { 0.0 }, rc4 { 1.0 }, rs4 { 0.0 };
        double ping { 0.0 }, pingK { 1.0 }, ping4 { 0.5 };
        double keySwing { 1.0 };   // how far this key's tine swings beside the others
        double po { 0.7 }, ph { 0.5 };   // its pickup: offset from the tine's line, distance in front (6.9.1: per key in Studio)
    };

    static constexpr int kVoices = 24;

    void setPitch (Voice& v)
    {
        const double f = v.f0 * bendRatio;
        const double w1 = juce::MathConstants<double>::twoPi * f / fs;
        v.rc1 = std::cos (w1); v.rs1 = std::sin (w1);
        const double f2 = juce::jmin (f * v.bellRatio, 0.45 * fs);
        const double w2 = juce::MathConstants<double>::twoPi * f2 / fs;
        v.rc2 = std::cos (w2); v.rs2 = std::sin (w2);
        const double w3 = juce::MathConstants<double>::twoPi * juce::jmin (f * v.ratio3, 0.45 * fs) / fs;
        v.rc3 = std::cos (w3); v.rs3 = std::sin (w3);
        const double w4 = juce::MathConstants<double>::twoPi * juce::jmin (f * 2.41, 0.45 * fs) / fs;
        v.rc4 = std::cos (w4); v.rs4 = std::sin (w4);
        v.bendSeen = bendRatio;
    }

    void noteOn (int note, float velocity)
    {
        // a key already sounding is struck again (as on the instrument: one tine per key)
        Voice* v = nullptr;
        for (auto& c : voices) if (c.active && c.note == note) { v = &c; break; }
        if (v == nullptr) for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr)
        {
            // all busy: the quietest released one, else the oldest
            double best = 1.0e9;
            for (auto& c : voices)
            {
                const double loud = (c.fast + c.slow) * c.rel * (c.releasing ? 0.1 : 1.0);
                if (loud < best) { best = loud; v = &c; }
            }
        }
        const float vel = juce::jlimit (0.02f, 1.0f, 1.0f - velSens + velSens * velocity);
        const double n = (double) note;
        auto& k = *v;
        // the same key struck again while its tine still swings: it swings on (no jump),
        // the blow adding to what is left
        const bool again = k.active && k.note == note;
        const double left = again ? (k.fast + k.slow) * k.rel : 0.0;
        k.active = true; k.releasing = false; k.pedalHeld = false;
        k.note = note; k.age = ++ageCounter;
        // (each key a hair off true, as tuned tines are: under a cent)
        const double cents = ((double) ((note * 7919) % 17) - 8.0) * 0.08;
        k.f0 = aquanode::midiNoteToHz ((double) n) * std::pow (2.0, cents / 1200.0);   // (patch tuning, 6.9.10)
        // (6.7.9: on the low keys the bell sits up near a kilohertz, where a recorded
        // Rhodes has it - a tine, not a clonk in the middle of the chord)
        k.bellRatio = juce::jlimit (5.4, 21.0, 7.0 * std::pow (2.0, -(n - 55.0) / 15.0));
        // the pings: near 1.5 and 2.4 times the pitch in the middle; at the top the
        // first closes in on the pitch (a slow beat against it, as the short tines have)
        k.ratio3 = juce::jmap (juce::jlimit (0.0, 1.0, (n - 72.0) / 16.0), 1.52, 1.04);
        setPitch (k);
        if (! again) { k.c1 = 1.0; k.s1 = 0.0; k.c2 = 1.0; k.s2 = 0.0; k.c3 = 1.0; k.s3 = 0.0; k.c4 = 1.0; k.s4 = 0.0; k.renorm = 0; }
        // Mellow: long low tines swing wide past the pickup, short high ones barely
        k.keySwing = mellow ? juce::jlimit (0.4, 1.7, std::pow (2.0, -(n - 62.0) / 22.0)) : 1.0;
        k.po = offset; k.ph = height;
        if (studio)
        {
            // Studio (6.9.1): fitted to a recorded Rhodes at two strengths, key by key - the pickup's
            // offset from the tine's line (near it in the middle: there a hard blow swings the tine past
            // it and the second harmonic takes over; further off at both ends) and how far the tine
            // swings for a medium and a hard blow (log-interpolated between them for the touch)
            const double kk = (n - 66.0) / 30.0;
            auto poly = [kk] (const double* c) { return ((c[0] * kk + c[1]) * kk + c[2]) * kk + c[3]; };
            static const double pa[4] = { 0.4117, -0.6316, -2.2976, -1.1611 };
            static const double pb[4] = { 0.2136, -0.6592, -2.507, -0.5643 };
            // (the offset as fitted with the swings below: near the line from the low middle to the
            // high middle, stepping away above 84 where the recorded tines come out near pure)
            static const double ok[] = { 36, 42, 48, 54, 60, 66, 72, 77, 79, 84, 85, 87, 99 };
            static const double ov[] = { 0.61, 0.43, 0.29, 0.20, 0.16, 0.17, 0.18, 0.19, 0.30, 0.29, 0.38, 0.62, 0.58 };
            k.po = interp (ok, ov, 13, n);
            k.ph = 0.8;
            const double la = poly (pa), lb = poly (pb);
            const double A = std::exp (la + (lb - la) * ((double) vel - 0.65) / 0.35);
            k.keySwing = juce::jlimit (0.01, 40.0, A / (0.775 * std::pow ((double) vel, 1.4)));
        }

        // how long it rings: ~14 s at the bottom, ~6 s in the middle, ~1 s at the top
        // (Mellow: the long sustain of the darker recorded piano)
        // (Studio: as the recorded tines ring, key by key - the low ones for many seconds with hardly any loss)
        static const double tk[] = { 36, 48, 56, 60, 66, 72, 78, 84, 90, 96, 108 };
        static const double tv[] = { 40, 40, 28, 20, 12, 10.5, 5.8, 5.6, 5.4, 3.2, 2.5 };
        const double t60 = (studio ? interp (tk, tv, 11, n)
                            : mellow ? juce::jlimit (1.0, 30.0, 11.5 * std::pow (2.0, -(n - 60.0) / 16.0))
                                     : juce::jlimit (0.6, 16.0, 6.5 * std::pow (2.0, -(n - 60.0) / 16.0))) * decayScale;
        k.slowK = std::exp (-6.9 / (t60 * fs));
        const double fastT = juce::jlimit (0.04, 0.35, 0.25 * std::pow (2.0, -(n - 48.0) / 24.0));
        k.fastK = std::exp (-1.0 / (fastT * fs));
        const double a = std::pow ((double) vel, 1.4);
        const double fastShare = studio ? 0.04 : mellow ? 0.15 : 0.3;   // (Studio: the recorded tines barely fall at first)
        k.fast = a * fastShare; k.slow = juce::jmax (a * (1.0 - fastShare), left);
        // the bell: brighter for a harder blow, gone in a fraction of a second (6.7.9: a
        // little longer and louder - the tine speaks more)
        const double bellT = juce::jlimit (0.04, 0.6, 0.4 * std::pow (2.0, -(n - 48.0) / 20.0));
        k.bellK = std::exp (-1.0 / (bellT * fs));
        k.bell = bellAmt * (studio ? 0.12 : mellow ? 0.42 : 0.6) * std::pow ((double) vel, 0.5) * a   // (a soft touch less)
                 * juce::jlimit (0.5, 1.0, std::pow (2.0, (n - 52.0) / 20.0));   // (up near 1 kHz on the low keys: there it needs less)
        // the pings: the strike's other modes, some tens of milliseconds
        const double pingT = juce::jlimit (0.025, 0.12, 0.09 * std::pow (2.0, -(n - 60.0) / 30.0));
        k.pingK = std::exp (-1.0 / (pingT * fs));
        k.ping = bellAmt * (studio ? 0.06 : mellow ? 0.16 : 0.2) * std::pow ((double) vel, 0.7) * a
                 * (1.0 + 2.5 * juce::jlimit (0.0, 1.0, (n - 76.0) / 12.0));   // (the short tines' beat: stronger)
        k.ping4 = n > 84.0 ? 0.0 : 0.6;
        // the hammer's contact: shorter for a harder blow (Mellow: a softer felt)
        k.attack = again ? juce::jlimit (0.0, 1.0, left / juce::jmax (1.0e-6, a)) : 0.0;
        k.attackK = 1.0 - std::exp (-1.0 / ((mellow ? 0.0034 - 0.0018 * vel : 0.0022 - 0.0016 * vel) * fs));
        k.rel = 1.0;
        k.thump = (mellow ? 0.03 : 0.05) * (double) vel; k.thumpK = std::exp (-1.0 / (0.004 * fs)); k.thumpLp = 0.0;
        if (! again) k.prevFlux = flux (k, 0.0);
        // the pickup turns the swing into its field's change: normalised so a quiet note
        // comes out at its swing whatever its pitch
        k.gain = 1.0 / (smallSignalSlope (k) * juce::MathConstants<double>::twoPi * k.f0 / fs);
        if (mellow && ! studio) k.gain *= std::pow (k.keySwing, -0.35);   // (the wide low swings: as loud as the rest)
        if (studio) k.gain *= studioLevel (n);
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.note == note && ! v.releasing)
            {
                if (sustainPedal) v.pedalHeld = true;
                else v.releasing = true;
            }
    }

    void allOff()
    {
        for (auto& v : voices) v.active = false;
        sustainPedal = false;
    }

    double flux (const Voice& v, double x) const
    {
        const double d = x - v.po;
        return 1.0 / std::sqrt (d * d + v.ph * v.ph);
    }
    double smallSignalSlope (const Voice& v) const
    {
        return v.po / std::pow (v.po * v.po + v.ph * v.ph, 1.5);
    }
    // Studio: the recorded piano's level across the keys (as its fundamental and second harmonic carry it)
    static double studioLevel (double n)
    {
        static const double keys[] = { 36, 48, 60, 72, 84, 96, 108 };
        static const double db[]   = { 8.0, 4.5, -0.5, 0.2, -3.0, -10.5, -13.5 };
        return juce::Decibels::decibelsToGain (interp (keys, db, 7, n));
    }
    static double interp (const double* xs, const double* ys, int count, double x)
    {
        if (x <= xs[0]) return ys[0];
        for (int i = 1; i < count; ++i)
            if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
        return ys[count - 1];
    }

    double renderVoice (Voice& v)
    {
        if (v.bendSeen != bendRatio) setPitch (v);
        // the phasors turn
        const double c1 = v.c1 * v.rc1 - v.s1 * v.rs1, s1 = v.s1 * v.rc1 + v.c1 * v.rs1;
        v.c1 = c1; v.s1 = s1;
        const double c2 = v.c2 * v.rc2 - v.s2 * v.rs2, s2 = v.s2 * v.rc2 + v.c2 * v.rs2;
        v.c2 = c2; v.s2 = s2;
        const bool pinging = v.ping > 1.0e-6;
        if (pinging)
        {
            const double c3 = v.c3 * v.rc3 - v.s3 * v.rs3, s3 = v.s3 * v.rc3 + v.c3 * v.rs3;
            v.c3 = c3; v.s3 = s3;
            const double c4 = v.c4 * v.rc4 - v.s4 * v.rs4, s4 = v.s4 * v.rc4 + v.c4 * v.rs4;
            v.c4 = c4; v.s4 = s4;
        }
        if (++v.renorm >= 1024)
        {
            v.renorm = 0;
            const double m1 = 1.0 / std::sqrt (v.c1 * v.c1 + v.s1 * v.s1), m2 = 1.0 / std::sqrt (v.c2 * v.c2 + v.s2 * v.s2);
            v.c1 *= m1; v.s1 *= m1; v.c2 *= m2; v.s2 *= m2;
            const double m3 = 1.0 / std::sqrt (v.c3 * v.c3 + v.s3 * v.s3), m4 = 1.0 / std::sqrt (v.c4 * v.c4 + v.s4 * v.s4);
            v.c3 *= m3; v.s3 *= m3; v.c4 *= m4; v.s4 *= m4;
        }
        // the fall, the hammer's contact, the damper
        v.fast *= v.fastK; v.slow *= v.slowK; v.bell *= v.bellK;
        v.attack += (1.0 - v.attack) * v.attackK;
        if (v.releasing) v.rel *= releaseCoeff;
        const double amp = (v.fast + v.slow) * v.attack * v.rel;
        // the tine's swing, in pickup gaps, and what the pickup makes of it
        const double sw = swing * v.keySwing;
        const double x = sw * amp * v.s1;
        const double fl = flux (v, x);
        double out = (fl - v.prevFlux) * v.gain / sw;
        v.prevFlux = fl;
        // the bell: the tine's second mode, as the pickup hears it (beside the
        // fundamental, not mixed with it in the pickup's curve - that would
        // smear a chord with off-pitch sidebands)
        out += v.bell * v.attack * v.rel * v.c2;
        if (pinging)
        {
            out += v.ping * v.attack * v.rel * (v.s3 + v.ping4 * v.s4);
            v.ping *= v.pingK;
        }
        // the felt's thump: a short burst of dark noise
        if (v.thump > 1.0e-5)
        {
            v.noise = v.noise * 1664525u + 1013904223u;
            const double wn = ((double) (v.noise >> 9) / 4194304.0) - 1.0;
            v.thumpLp += 0.12 * (wn - v.thumpLp);
            out += v.thumpLp * v.thump;
            v.thump *= v.thumpK;
        }
        if (amp < 2.0e-5 && v.thump < 1.0e-5 && (v.releasing || v.slow < 2.0e-5)) v.active = false;
        return out * 0.5;
    }

    //=== the chain ============================================================
    struct Lp2   // a 2-pole low-pass (TPT state-variable)
    {
        double g { 0.5 }, k { 1.4 }, a1 { 0.0 }, a2 { 0.0 }, a3 { 0.0 }, ic1 { 0.0 }, ic2 { 0.0 };
        void set (double hz, double q, double sr)
        {
            g = std::tan (juce::MathConstants<double>::pi * hz / sr); k = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        double process (double v0)
        {
            const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
            return v2;
        }
        void clear() { ic1 = ic2 = 0.0; }
    };

    // the drive (6.7.9: warm, not harsh). Classic: the whole signal into an
    // asymmetric tube clipper, its highs tilted down on the way in (a chord's
    // tine and bell then do not grind against each other), a speaker's
    // roll-off after. Mellow: only the lows and mids into a softer tube, the
    // highs - the tine - past it clean. Both at twice the rate (the halves
    // between samples guessed straight, the result smoothed back down).
    double driveStage (double x)
    {
        if (mellow)
        {
            // the split: a gentle 2-pole low-pass, the rest is the highs (together exactly the input)
            const double lo = splitLp.process (x), hi = x - lo;
            const double mid = 0.5 * (lo + driveLast);
            driveLast = lo;
            auto tube = [this] (double v)
            {
                const double u = v * drive;
                return (std::tanh (u + 0.12) - std::tanh (0.12)) * driveMakeup;
            };
            downLp2.process (downLp1.process (tube (mid)));
            const double warm = downLp2.process (downLp1.process (tube (lo)));
            // the speaker: a soft top, and the very bottom thinned as a Suitcase's is
            // (low keys speak in their harmonics, not a boom)
            return cabHp.process (cabLp.process (warm + hi * (0.85 + 0.15 * driveMakeup)));
        }
        // the highs tilted down going in: -5 dB above ~2 kHz
        const double tilted = x - 0.44 * preHp.process (x);
        const double mid = 0.5 * (tilted + driveLast);
        driveLast = tilted;
        auto tube = [this] (double v)
        {
            const double u = v * drive;
            return (std::tanh (u + 0.25) - std::tanh (0.25)) * driveMakeup;
        };
        downLp2.process (downLp1.process (tube (mid)));
        const double y = downLp2.process (downLp1.process (tube (tilted)));
        // half the tilt given back, then the speaker
        return cabLp.process (y + 0.3 * postHp.process (y));
    }

    struct Hp2   // a 2-pole high-pass (TPT state-variable)
    {
        double g { 0.01 }, k { 1.4 }, a1 { 0.0 }, a2 { 0.0 }, a3 { 0.0 }, ic1 { 0.0 }, ic2 { 0.0 };
        void set (double hz, double q, double sr)
        {
            g = std::tan (juce::MathConstants<double>::pi * hz / sr); k = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        double process (double v0)
        {
            const double v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
            return v0 - k * v1 - v2;
        }
        void clear() { ic1 = ic2 = 0.0; }
    };

    struct Hp1   // a one-pole high-pass
    {
        double a { 0.9 }, x1 { 0.0 }, y1 { 0.0 };
        void set (double hz, double sr) { a = std::exp (-juce::MathConstants<double>::twoPi * hz / sr); }
        double process (double v) { const double y = a * (y1 + v - x1); x1 = v; y1 = y; return y; }
        void clear() { x1 = y1 = 0.0; }
    };

    double dcBlock (double x)
    {
        const double y = x - dcX + 0.9995 * dcY;
        dcX = x; dcY = y;
        return y;
    }

    struct PhaserState { double x[4] {}, y[4] {}, fb { 0.0 }; };
    double phaser (PhaserState& st, double in, double sweep)
    {
        const double fc = 160.0 * std::pow (11.0, sweep);                 // 160 Hz .. 1.76 kHz
        const double t = std::tan (juce::MathConstants<double>::pi * juce::jmin (fc, 0.45 * fs) / fs);
        const double a = (t - 1.0) / (t + 1.0);
        double v = in + 0.25 * st.fb;
        for (int i = 0; i < 4; ++i)
        {
            const double y = a * v + st.x[i] - a * st.y[i];
            st.x[i] = v; st.y[i] = y;
            v = y;
        }
        st.fb = v;
        return in * (1.0 - phMix) + v * phMix;
    }

    void resetFx()
    {
        toneLp.clear(); downLp1.clear(); downLp2.clear();
        splitLp.clear(); cabLp.clear(); cabHp.clear(); preHp.clear(); postHp.clear();
        cabHp.set (85.0, 0.7, fs);
        splitLp.set (900.0, 0.5, fs);
        cabLp.set (juce::jmin (5800.0, 0.45 * fs), 0.62, fs);
        preHp.set (2000.0, fs); postHp.set (2000.0, fs);
        driveLast = 0.0; dcX = dcY = 0.0;
        phL = {}; phR = {};
        phPhase = 0.0; panPhase = 0.0;
        // (a 4-pole Butterworth just under the original Nyquist, at the doubled rate)
        downLp1.set (0.45 * fs, 0.541, 2.0 * fs); downLp2.set (0.45 * fs, 1.307, 2.0 * fs);
    }

    std::array<Voice, kVoices> voices {};
    juce::uint32 ageCounter { 0 };
    bool sustainPedal { false };
    double fs { 48000.0 }, ownBend { 0.0 }, bendRatio { 1.0 };
    float swing { 0.8f }, bellAmt { 0.35f }, decayScale { 1.0f }, velSens { 0.8f };
    double releaseCoeff { 0.999 };
    Lp2 toneLp, downLp1, downLp2, splitLp, cabLp;
    Hp2 cabHp;
    Hp1 preHp, postHp;
    bool mellow { false }, studio { false };
    int engine { -1 };
    double offset { 0.7 }, height { 0.5 };   // the pickup (Classic; Mellow sits further off)
    float drive { 1.0f }, driveMakeup { 1.0f };
    double driveLast { 0.0 }, dcX { 0.0 }, dcY { 0.0 };
    PhaserState phL, phR;
    double phPhase { 0.0 }, phInc { 0.0 }, panPhase { 0.0 }, panInc { 0.0 };
    float phMix { 0.25f }, panDepth { 0.4f }, level { 0.7f };
};

AQUANODE_REGISTER_MODULE (ElectricPianoModule, ElectricPianoModule::descriptor)
