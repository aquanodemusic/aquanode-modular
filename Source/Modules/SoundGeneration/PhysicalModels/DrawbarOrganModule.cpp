//==============================================================================
// Drawbar Organ (6.9.1) - a tonewheel organ in the B3 manner, modelled, with
// its rotating speaker built in.
//
// The generator: 91 tonewheels spinning all the time, tuned by the gears of
// the real generator (a hair off equal temperament, as the gear ratios give
// it), each a near-sine with a trace of its third harmonic on the low wheels.
// A key closes nine contacts, one per drawbar - 16', 5 1/3', 8', 4', 2 2/3',
// 2', 1 3/5', 1 1/3', 1' - each picking its wheel (folding back an octave at
// the ends of the generator, as the real one does) at the drawbar's level
// (eight steps of about 3 dB). The wheels' loudness falls off above a few
// hundred hertz the way the recorded organ's does (its taper), a little of
// the other wheels leaks in (Leakage), and the shaft's 20 Hz turn rides on
// it all very faintly.
//
// Key Click: the contacts close a fraction of a millisecond apart, bouncing,
// with a short crackle; on release they open the same way. Percussion (2nd or
// 3rd harmonic, Fast or Slow, Normal or Soft) strikes only when no other key
// is held - legato notes go without it - and takes the 1' drawbar's place.
// The scanner vibrato and chorus (V1 - V3, C1 - C3) bend the whole organ
// with a delay line swept 6.9 times a second; Drive is the preamp's warmth;
// the Leslie (Off, Stop, Slow, Fast - the mod wheel switches Slow to Fast) is
// the Rotary module's cabinet. Expression (CC 11) is the swell pedal.
//
// The defaults follow the recorded organ: 8', 4' and 2 2/3' fully out, no
// percussion, a little click and leakage, the Leslie on Slow.
// Global (its own voices). Input: Midi In. Output: Audio Out (stereo).
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/LookPalette.h"
#include "../../Shared/RotaryCabinet.h"

using namespace aquanode;

namespace drawbar
{
    constexpr int kBars = 9;
    constexpr int kWheels = 91;                // MIDI 24 (C1) .. 114 (F#8)
    constexpr int kLowWheel = 24, kHighWheel = 114;
    // each drawbar's pitch above the key, in semitones
    constexpr int kSemis[kBars] = { -12, 7, 0, 12, 19, 24, 28, 31, 36 };
    const juce::StringArray kIds { "db16", "db513", "db8", "db4", "db223", "db2", "db135", "db113", "db1" };
    const juce::StringArray kNames { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
    // brown (the subharmonics), white (octaves of the key), black (the others)
    constexpr int kColour[kBars] = { 0, 0, 1, 1, 2, 1, 2, 2, 1 };

    // the generator's gears, per pitch class: wheel teeth x 20 Hz shaft x ratio
    constexpr double kGear[12] = { 85.0 / 104.0, 71.0 / 82.0, 67.0 / 73.0, 105.0 / 108.0, 103.0 / 100.0, 84.0 / 77.0,
                                   74.0 / 64.0, 98.0 / 80.0, 96.0 / 74.0, 88.0 / 64.0, 67.0 / 46.0, 108.0 / 70.0 };
    inline double wheelHz (int midi)
    {
        const int o = (midi - kLowWheel) / 12;
        return 20.0 * kGear[midi % 12] * 2.0 * std::pow (2.0, (double) o);
    }
    // where a drawbar's pitch lands on the generator: folded back an octave at its ends
    inline int wheelFor (int note, int bar)
    {
        int w = note + kSemis[bar];
        while (w > kHighWheel) w -= 12;
        while (w < kLowWheel) w += 12;
        return w;
    }

    // the recorded organ's loudness against frequency (its taper), dB
    constexpr float kTaperHz[] = { 32.0f, 50.0f, 75.0f, 110.0f, 150.0f, 220.0f, 300.0f, 450.0f, 600.0f, 900.0f, 1200.0f, 1800.0f, 2400.0f, 3600.0f, 6000.0f };
    // (fitted so that through the built-in Leslie, whose horn lifts what is above its crossover, it matches)
    constexpr float kTaperDb[] = { -2.0f, -1.8f, -0.95f, -0.2f, 0.0f, -0.2f, -2.0f, -4.8f, -5.2f, -7.2f, -11.3f, -15.1f, -15.0f, -14.4f, -15.4f };
    inline float taperDb (double hz)
    {
        constexpr int n = (int) (sizeof (kTaperHz) / sizeof (float));
        if (hz <= kTaperHz[0]) return kTaperDb[0];
        for (int i = 1; i < n; ++i)
            if (hz <= kTaperHz[i])
            {
                const double t = std::log (hz / kTaperHz[i - 1]) / std::log ((double) kTaperHz[i] / kTaperHz[i - 1]);
                return (float) (kTaperDb[i - 1] + t * (kTaperDb[i] - kTaperDb[i - 1]));
            }
        return kTaperDb[n - 1];
    }

    // a drawbar's step as gain: eight steps of about 3 dB
    inline float barGain (float v)
    {
        if (v <= 0.01f) return 0.0f;
        return std::pow (10.0f, -3.0f * (8.0f - juce::jlimit (0.0f, 8.0f, v)) / 20.0f);
    }

    const juce::StringArray kVibratoNames { "Off", "V1", "V2", "V3", "C1", "C2", "C3" };

    // a few classic registrations (drawbar settings, 16' first)
    struct Registration { const char* name; const char* bars; };
    constexpr Registration kRegistrations[] = {
        { "Warm Jazz", "888000000" }, { "Full Organ", "888888888" }, { "Hollow", "808000000" }, { "Mellow Flute", "008000000" },
        { "Bright Gospel", "888800000" }, { "Clicky Lead", "888000008" }, { "Ballad", "838000000" }, { "Reedy", "006876400" },
        { "Soft Pad", "004302000" }, { "Screaming", "888886668" }, { "Bass Pedal", "850000000" }, { "Shimmer", "800000888" } };
}

using namespace drawbar;

class DrawbarOrganModule final : public SynthModule
{
public:
    enum ParamIndex { pDb16 = 0, pDb513, pDb8, pDb4, pDb223, pDb2, pDb135, pDb113, pDb1,
                      pPerc, pPercDecay, pPercVol, pVibrato, pClick, pLeakage, pDrive, pLeslie, pLevel, pSlowRpm, pFastRpm, pInertia };

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    double voiceTailSeconds() const override { return 0.0; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fs = juce::jmax (8000.0, sr);
        for (int w = 0; w < kWheels; ++w)
        {
            const double hz = wheelHz (kLowWheel + w);
            inc[(size_t) w] = hz / fs;
            taper[(size_t) w] = hz < 0.45 * fs ? juce::Decibels::decibelsToGain (taperDb (hz)) : 0.0f;
            // a trace of the third harmonic on the low wheels (where it is still below Nyquist)
            third[(size_t) w] = hz * 3.0 < 0.45 * fs ? 0.03f * juce::jlimit (0.0f, 1.0f, (float) (1.0 - std::log2 (hz / 40.0) / 5.0)) : 0.0f;
            phase[(size_t) w] = std::fmod (0.6180339887 * (w + 1), 1.0);   // (spinning since the power went on)
        }
        vibBuf.assign ((size_t) juce::nextPowerOfTwo ((int) (0.01 * fs) + 8), 0.0f);
        tunedSeen.fill (0.0);   // (the tuning read again at the first block)
        reset();
    }

    void reset() override
    {
        for (auto& v : voices) v.active = false;
        masterBend = 0.0f;
        for (auto& c : chanBend) c = 0.0f;
        for (auto& c : chanPressure) c = 0.0f;
        std::fill (vibBuf.begin(), vibBuf.end(), 0.0f);
        vibPos = 0;
        clickLp1 = clickLp2 = 0.0f;
        cabinet.prepare (fs, cabinetSettings());
        dcIn = dcOut = 0.0f;
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        const int ch = juce::jlimit (1, 16, m.getChannel());
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
        else if (m.isNoteOff()) noteOff (m.getNoteNumber(), ch);
        else if (m.isPitchWheel())
        {
            // MPE: a member channel bends its own note (+-48 st); channel 1 bends them all (+-2 st)
            const float b = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            if (ch == 1) masterBend = b * 2.0f; else chanBend[ch] = b * 48.0f;
        }
        else if (m.isChannelPressure()) chanPressure[ch] = m.getChannelPressureValue() / 127.0f;
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (cc == 1) modWheel = m.getControllerValue() / 127.0f;
            else if (cc == 11) expression = m.getControllerValue() / 127.0f;
            else if (cc == 120 || cc == 123) allOff();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    void midiAllNotesOff() override { allOff(); }

    // (6.9.14) a Global Microtuning (or an MTS-ESP master) retunes the generator: every
    // wheel moves to its note's tuned pitch, keeping its own gear's offset from equal
    // temperament (read inside the block, where the patch's tuning is in place)
    void retuneWheels()
    {
        bool moved = false;
        for (int k = 0; k < 12 && ! moved; ++k)
            moved = ! juce::exactlyEqual (tuning::noteToHz ((double) (60 + k)), tunedSeen[(size_t) k]);
        if (! moved) return;
        for (int k = 0; k < 12; ++k) tunedSeen[(size_t) k] = tuning::noteToHz ((double) (60 + k));
        for (int w = 0; w < kWheels; ++w)
        {
            const int m = kLowWheel + w;
            const double et = 440.0 * std::pow (2.0, (m - 69) / 12.0);
            inc[(size_t) w] = wheelHz (m) * (tuning::noteToHz ((double) m) / et) / fs;
        }
    }

    void blockStart() override
    {
        const int perc = juce::jlimit (0, 2, (int) std::lround (param (pPerc)));
        for (int b = 0; b < kBars; ++b)
            gain[(size_t) b] = barGain (param (b));
        // percussion takes the 1' drawbar's place; at Normal volume the drawbars give way a little
        percBar = perc == 1 ? 3 : (perc == 2 ? 4 : -1);
        const bool soft = param (pPercVol) > 0.5f;
        if (perc > 0)
        {
            gain[8] = 0.0f;
            if (! soft) for (auto& g : gain) g *= 0.71f;
        }
        percAmp = perc > 0 ? (soft ? 0.5f : 1.0f) : 0.0f;
        percK = (float) std::exp (-1.0 / ((param (pPercDecay) > 0.5f ? 0.38 : 0.09) * fs));
        leak = 0.0025f * juce::square (param (pLeakage) * 0.02f);   // (100 %: about -46 dB per wheel)
        click = param (pClick) * 0.01f;
        // (the tube's curve blended in by Drive: at 0 % the organ is clean)
        driveAmt = param (pDrive) * 0.01f;
        driveK = 1.0f + 6.0f * driveAmt;
        const int vib = juce::jlimit (0, 6, (int) std::lround (param (pVibrato)));
        static const float depthMs[4] = { 0.0f, 0.12f, 0.25f, 0.42f };
        vibDepth = (float) (depthMs[vib == 0 ? 0 : (vib - 1) % 3 + 1] * 0.001 * fs);
        chorus = vib >= 4;
        vibOn = vib > 0;
        vibInc = 6.87 / fs;
        leslieOn = (int) std::lround (param (pLeslie)) > 0;
        level = param (pLevel) * (0.25f + 0.75f * expression);
    }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        if ((tuneCounter++ & 255) == 0) retuneWheels();
        // the generator turns
        for (int w = 0; w < kWheels; ++w)
        {
            phase[(size_t) w] += inc[(size_t) w];
            if (phase[(size_t) w] >= 1.0) phase[(size_t) w] -= 1.0;
        }
        ++stamp;

        float sum = 0.0f;
        for (auto& v : voices)
            if (v.active)
                sum += renderVoice (v);

        // the contacts' crackle
        if (clickEnv > 1.0e-5f)
        {
            noise = noise * 1664525u + 1013904223u;
            const float n = (float) (noise >> 9) * (1.0f / 4194304.0f) - 1.0f;
            clickLp1 += 0.55f * (n - clickLp1);
            clickLp2 += 0.08f * (clickLp1 - clickLp2);
            sum += (clickLp1 - clickLp2) * clickEnv;
            clickEnv *= clickK;
        }

        // the shaft's 20 Hz, very faint
        shaft += 20.0 / fs; if (shaft >= 1.0) shaft -= 1.0;
        sum *= 1.0f + 0.012f * (float) std::sin (juce::MathConstants<double>::twoPi * shaft) * juce::jmin (1.0f, leak * 400.0f);

        // the preamp
        float x = sum + driveAmt * (std::tanh (sum * driveK) / driveK * (1.0f + 0.6f * driveAmt) - sum);
        // (a tube's slight lean, taken out again)
        dcOut = x - dcIn + 0.9995f * dcOut; dcIn = x; x = dcOut;

        // the scanner: vibrato (and chorus: the vibrato with the straight sound)
        if (vibOn)
        {
            const int mask = (int) vibBuf.size() - 1;
            vibBuf[(size_t) vibPos] = x;
            vibPhase += vibInc; if (vibPhase >= 1.0) vibPhase -= 1.0;
            const float tri = (float) (vibPhase < 0.5 ? 4.0 * vibPhase - 1.0 : 3.0 - 4.0 * vibPhase);
            const float base = vibDepth + 2.0f;
            const float d = base + vibDepth * (0.85f * tri + 0.15f * (float) std::sin (juce::MathConstants<double>::twoPi * vibPhase));
            const float rp = (float) vibPos - d;
            const int i0 = (int) std::floor (rp);
            const float fr = rp - (float) i0;
            const float a = vibBuf[(size_t) (i0 & mask)], b = vibBuf[(size_t) ((i0 + 1) & mask)];
            const float wet = a + fr * (b - a);
            if (chorus)
            {
                const int j = (int) std::floor ((float) vibPos - base);
                x = 0.5f * (vibBuf[(size_t) (j & mask)] + wet);
            }
            else x = wet;
            vibPos = (vibPos + 1) & mask;
        }

        float l = x, r = x;
        if (leslieOn) cabinet.process (cabinetSettings(), x, x, l, r);
        outputs[0] = { l * level, r * level };
    }

    //=== the card ==============================================================
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 176; }
    int preferredModuleWidth() const override { return 430; }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.drawbarorgan";
        d.displayName = "Drawbar";   // (6.9.15: was Drawbar Organ)
        d.section = ModuleSection::Oscillator;
        d.sidebarOrder = 5;
        d.searchKeywords = "drawbar organ tonewheel hammond b3 leslie rotary percussion click jazz gospel";
        d.description =
            "A tonewheel organ in the B3 manner, modelled: 91 tonewheels tuned by the real generator's gears, nine drawbars "
            "(16' to 1', pull them on the card) folding back at the generator's ends, the recorded organ's taper, a little "
            "leakage. Key Click: the contacts closing a hair apart. Percussion (2nd or 3rd, Fast or Slow, Normal or Soft) "
            "strikes only when no other key is held. The scanner's Vibrato and Chorus (V1 - V3, C1 - C3), the preamp's "
            "Drive, and the Leslie built in (Off, Stop, Slow, Fast; the mod wheel switches Slow to Fast; Slow RPM and Fast RPM: "
            "how fast the horn turns at each, the drum following; Inertia: how quickly the rotors get to a new speed, from "
            "almost at once to several seconds, like a real cabinet's). Expression "
            "(CC 11) is the swell pedal. Presets on the card set classic registrations. MPE: each note bends on its own "
            "channel (its tonewheels let go and turn at the bent speed) and pressure swells it.";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
        static const float defaults[kBars] = { 0, 0, 8, 8, 8, 0, 0, 0, 0 };
        for (int b = 0; b < kBars; ++b)
            d.params.push_back (makeRotary (kIds[b], kNames[b], 0.0f, 8.0f, defaults[b], 9, {}, false, 1.0f).hide().cableTargetInCustomUI());
        d.params.push_back (makeCombo ("perc", "Percussion", { "Off", "2nd", "3rd" }, 0, 0, 2).noMod());
        d.params.push_back (makeCombo ("percDecay", "Perc Decay", { "Fast", "Slow" }, 0, 0, 2).noMod());
        d.params.push_back (makeCombo ("percVol", "Perc Volume", { "Normal", "Soft" }, 0, 0, 1).noMod());
        d.params.push_back (makeCombo ("vibrato", "Vibrato", kVibratoNames, 0, 1, 2).noMod());
        d.params.push_back (makeRotary ("click", "Key Click", 0.0f, 100.0f, 25.0f, 1, "%", false, 1.0f));
        d.params.push_back (makeRotary ("leakage", "Leakage", 0.0f, 100.0f, 40.0f, 1, "%", false, 1.0f));
        d.params.push_back (makeRotary ("drive", "Drive", 0.0f, 100.0f, 8.0f, 2, "%", false, 1.0f));
        d.params.push_back (makeCombo ("leslie", "Leslie", { "Off", "Stop", "Slow", "Fast" }, 2, 2, 2).noMod());
        d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 2));
        // (6.9.11) the horn's speed at Slow and at Fast, and how quickly the
        // rotors get to a new speed - the defaults as it always was
        d.params.push_back (makeRotary ("slowRpm", "Slow RPM", 0.0f, 200.0f, (float) RotaryCabinet::kSlowRpm, 2, " rpm"));
        d.params.push_back (makeRotary ("fastRpm", "Fast RPM", 100.0f, 800.0f, (float) RotaryCabinet::kFastRpm, 2, " rpm"));
        d.params.push_back (makeRotary ("inertia", "Inertia", 0.0f, 100.0f, 60.0f, 2, "%", false, 0.1f));
        d.params.push_back (makeOutputTrim (-3.0f));
        return d;
    }

    // registrations from the card's menu (message thread)
    void setRegistration (const char* bars)
    {
        for (int b = 0; b < kBars && bars[b] != 0; ++b)
            setParameter (kIds[b], (float) (bars[b] - '0'));
    }

private:
    struct Voice
    {
        bool active { false }, releasing { false };
        int note { -1 };
        juce::uint32 age { 0 };
        int wheel[kBars] {};
        int contact[kBars] {};     // samples until each contact closes (then the bar sounds)
        float open[kBars] {};      // each contact: 0 open .. 1 closed (a short ramp)
        int release[kBars] {};     // samples until each contact opens again, once let go
        float perc { 0.0f };
        int percWheel { 0 };
        int channel { 1 };
        bool bent { false };          // MPE: once bent, the note turns its own wheels
        double own[kBars + 1] {};     // (the nine bars and the percussion)
    };
    static constexpr int kVoices = 32;

    RotaryCabinet::Settings cabinetSettings() const
    {
        RotaryCabinet::Settings s;
        const int l = juce::jlimit (0, 3, (int) std::lround (param (pLeslie)));
        s.speed = l <= 1 ? 0 : (l == 3 || modWheel > 0.5f ? 2 : 1);
        s.slowRpm = juce::jmax (0.0, (double) param (pSlowRpm));
        s.fastRpm = juce::jmax (0.0, (double) param (pFastRpm));
        s.inertia = juce::jlimit (0.0, 100.0, (double) param (pInertia));
        // (fitted to the recorded organ: a gentler horn swirl, and the drum's own bend)
        s.hornDepth = 50.0f;
        s.drumDepth = 45.0f;
        return s;
    }

    float wheelValue (int midiWheel)
    {
        const int w = juce::jlimit (0, kWheels - 1, midiWheel - kLowWheel);
        if (wheelStamp[(size_t) w] != stamp)
        {
            wheelStamp[(size_t) w] = stamp;
            const float s = (float) std::sin (juce::MathConstants<double>::twoPi * phase[(size_t) w]);
            wheelOut[(size_t) w] = (s + third[(size_t) w] * (3.0f * s - 4.0f * s * s * s)) * taper[(size_t) w];
        }
        return wheelOut[(size_t) w];
    }

    void noteOn (int note, float, int ch)
    {
        bool othersHeld = false;
        for (auto& c : voices) othersHeld = othersHeld || (c.active && ! c.releasing);
        Voice* v = nullptr;
        for (auto& c : voices) if (c.active && c.note == note && c.channel == ch) { v = &c; break; }
        if (v == nullptr) for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.age < v->age) v = &c; }
        auto& k = *v;
        const bool again = k.active && k.note == note;
        k.active = true; k.releasing = false; k.note = note; k.age = ++ageCounter;
        k.channel = ch; k.bent = false;
        // the nine contacts close a little apart (the more click, the further), bouncing
        const float spread = (0.15f + 2.2f * click) * 0.001f * (float) fs;
        for (int b = 0; b < kBars; ++b)
        {
            k.wheel[b] = wheelFor (note, b);
            noise = noise * 1664525u + 1013904223u;
            k.contact[b] = again ? 0 : (int) ((float) (noise >> 16) / 65536.0f * spread);
            if (! again) k.open[b] = 0.0f;
            k.release[b] = -1;
        }
        // percussion: only struck when no other key is held
        if (percAmp > 0.0f && ! othersHeld)
        {
            k.percWheel = wheelFor (note, percBar);
            k.perc = 1.0f;
        }
        else if (! again) k.perc = 0.0f;
        clickEnv = juce::jmax (clickEnv, 0.06f * click * click);
        clickK = (float) std::exp (-1.0 / (0.0018 * fs));
    }

    void noteOff (int note, int ch)
    {
        const float spread = (0.1f + 1.4f * click) * 0.001f * (float) fs;
        for (auto& v : voices)
            if (v.active && v.note == note && ! v.releasing && (v.channel == ch || ch == 1 || v.channel == 1))
            {
                v.releasing = true;
                for (int b = 0; b < kBars; ++b)
                {
                    noise = noise * 1664525u + 1013904223u;
                    v.release[b] = (int) ((float) (noise >> 16) / 65536.0f * spread);
                }
                clickEnv = juce::jmax (clickEnv, 0.025f * click * click);
            }
    }

    void allOff() { for (auto& v : voices) v.active = false; }

    // a bent note's own wheel: it starts where the generator's wheel is (no click) and turns at the bent speed
    float ownWheel (Voice& v, int slot, int midiWheel, double ratio)
    {
        const int w = juce::jlimit (0, kWheels - 1, midiWheel - kLowWheel);
        double& ph = v.own[slot];
        ph += inc[(size_t) w] * ratio;
        ph -= std::floor (ph);
        const float s = (float) std::sin (juce::MathConstants<double>::twoPi * ph);
        return (s + third[(size_t) w] * (3.0f * s - 4.0f * s * s * s)) * taper[(size_t) w];
    }

    float renderVoice (Voice& v)
    {
        // a contact closing (or opening) ramps over a fraction of a millisecond: the more click, the faster
        const float ramp = 1.0f / (float) ((0.0015 - 0.0013 * click) * fs + 1.0);
        float out = 0.0f;
        bool any = false;
        const float bend = masterBend + (v.channel > 1 ? chanBend[v.channel] : 0.0f);
        if (! v.bent && std::abs (bend) > 1.0e-4f)
        {
            v.bent = true;
            for (int b = 0; b < kBars; ++b) v.own[b] = phase[(size_t) juce::jlimit (0, kWheels - 1, v.wheel[b] - kLowWheel)];
            v.own[kBars] = phase[(size_t) juce::jlimit (0, kWheels - 1, v.percWheel - kLowWheel)];
        }
        const double ratio = v.bent ? std::pow (2.0, (double) bend / 12.0) : 1.0;
        for (int b = 0; b < kBars; ++b)
        {
            if (v.contact[b] > 0) { --v.contact[b]; continue; }
            if (v.releasing)
            {
                if (v.release[b] > 0) --v.release[b];
                else v.open[b] = juce::jmax (0.0f, v.open[b] - ramp);
            }
            else v.open[b] = juce::jmin (1.0f, v.open[b] + ramp);
            if (v.open[b] <= 0.0f) continue;
            any = true;
            const float g = gain[(size_t) b] + leak;
            if (g > 0.0f) out += g * v.open[b] * (v.bent ? ownWheel (v, b, v.wheel[b], ratio) : wheelValue (v.wheel[b]));
        }
        if (v.perc > 1.0e-4f)
        {
            out += percAmp * v.perc * (v.bent ? ownWheel (v, kBars, v.percWheel, ratio) : wheelValue (v.percWheel)) * (v.releasing ? v.open[2] : 1.0f);
            v.perc *= percK;
        }
        if (v.releasing && ! any) v.active = false;
        // MPE pressure swells the note
        return out * 0.16f * (1.0f + 0.6f * chanPressure[v.channel > 1 ? v.channel : 1]);
    }

    double fs { 48000.0 };
    std::array<Voice, kVoices> voices;
    juce::uint32 ageCounter { 0 }, stamp { 0 }, noise { 22222u };
    float masterBend { 0.0f }, chanBend[17] {}, chanPressure[17] {};
    std::array<double, kWheels> phase {}, inc {};
    std::array<float, kWheels> taper {}, third {}, wheelOut {};
    std::array<juce::uint32, kWheels> wheelStamp {};
    std::array<float, kBars> gain {};
    int percBar { -1 };
    std::array<double, 12> tunedSeen {};   // (6.9.14) the tuning the wheels were last set for
    unsigned tuneCounter { 0 };
    float percAmp { 0.0f }, percK { 0.999f }, leak { 0.0f }, click { 0.25f };
    float clickEnv { 0.0f }, clickK { 0.99f }, clickLp1 { 0.0f }, clickLp2 { 0.0f };
    double shaft { 0.0 };
    float driveK { 1.0f }, driveAmt { 0.0f }, dcIn { 0.0f }, dcOut { 0.0f };
    std::vector<float> vibBuf;
    int vibPos { 0 };
    double vibPhase { 0.0 }, vibInc { 0.0 };
    float vibDepth { 0.0f };
    bool vibOn { false }, chorus { false }, leslieOn { true };
    float modWheel { 0.0f }, expression { 1.0f }, level { 0.7f };
    RotaryCabinet cabinet;
};

//==============================================================================
// The card: nine drawbars to pull, and a menu of registrations
//==============================================================================
namespace
{
    class DrawbarView final : public juce::Component, public aquanode::ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit DrawbarView (DrawbarOrganModule& m) : module (&m)
        {
            addAndMakeVisible (presets);
            presets.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a2a20));
            presets.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
            presets.setTooltip ("Classic registrations: the nine drawbars set at once");
            presets.onClick = [this]
            {
                juce::PopupMenu menu;
                menu.addSectionHeader ("Registrations");
                int id = 1;
                for (const auto& r : kRegistrations)
                    menu.addItem (id++, juce::String (r.name) + "   " + r.bars);
                juce::Component::SafePointer<DrawbarView> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presets), [safe] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    if (auto* m = safe->organ()) m->setRegistration (kRegistrations[r - 1].bars);
                    if (safe->paramValuesChangedByContent) safe->paramValuesChangedByContent();
                    safe->repaint();
                });
            };
            startTimerHz (15);
        }
        DrawbarOrganModule* organ() const { return dynamic_cast<DrawbarOrganModule*> (module.get()); }

        void resized() override { presets.setBounds (getWidth() - 86, 4, 80, 20); }

        juce::Rectangle<float> slot (int b) const
        {
            const float w = (float) (getWidth() - 100) / (float) kBars;
            return { 8.0f + w * (float) b + w * 0.18f, 26.0f, w * 0.64f, (float) getHeight() - 32.0f };
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = organ();
            if (m == nullptr) return;
            const auto look = LookPalette::current (juce::Colour (0xffe0b070), ThemeColours::Oscillator);
            look.fillBackground (g, getLocalBounds().toFloat());
            // the wooden rail with the slots
            auto rail = juce::Rectangle<float> (4.0f, 4.0f, (float) getWidth() - 96.0f, 18.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a3a22), 0.0f, rail.getY(), juce::Colour (0xff3a2414), 0.0f, rail.getBottom(), false));
            g.fillRoundedRectangle (rail, 3.0f);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            for (int b = 0; b < kBars; ++b)
            {
                const auto s = slot (b);
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawText (kNames[b], juce::Rectangle<float> (s.getX() - 6.0f, rail.getY(), s.getWidth() + 12.0f, rail.getHeight()), juce::Justification::centred, false);
                const float v = m->getParameter (kIds[b]);
                // the slot
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (s.withWidth (s.getWidth() * 0.34f).withX (s.getCentreX() - s.getWidth() * 0.17f), 2.0f);
                // the rod pulled out to its step, the steps marked on it
                const float stepH = (s.getHeight() - 22.0f) / 8.0f;
                const float rodBottom = s.getY() + 4.0f + v * stepH;
                const auto rod = juce::Rectangle<float> (s.getCentreX() - s.getWidth() * 0.13f, s.getY(), s.getWidth() * 0.26f, rodBottom - s.getY());
                g.setColour (juce::Colour (0xffe8e2d4));
                g.fillRect (rod);
                g.setColour (juce::Colour (0xff3a3020));
                for (int k = 1; k <= (int) v; ++k)
                {
                    const float y = s.getY() + 4.0f + (float) k * stepH;
                    g.drawHorizontalLine ((int) y - 1, rod.getX(), rod.getRight());
                    if (stepH > 9.0f) g.drawText (juce::String (k), juce::Rectangle<float> (rod.getX(), y - stepH, rod.getWidth(), stepH), juce::Justification::centred, false);
                }
                // the handle: brown, white or black
                static const juce::Colour handle[3] = { juce::Colour (0xff7a4a26), juce::Colour (0xfff2efe6), juce::Colour (0xff1c1a18) };
                const auto h = juce::Rectangle<float> (s.getX(), rodBottom, s.getWidth(), 18.0f);
                g.setGradientFill (juce::ColourGradient (handle[kColour[b]].brighter (0.25f), 0.0f, h.getY(), handle[kColour[b]].darker (0.3f), 0.0f, h.getBottom(), false));
                g.fillRoundedRectangle (h, 4.0f);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.drawRoundedRectangle (h, 4.0f, 1.0f);
                g.setColour (kColour[b] == 1 ? juce::Colours::black.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.85f));
                g.drawText (juce::String ((int) std::lround (v)), h, juce::Justification::centred, false);
            }
            // the registration, as numbers
            juce::String reg;
            for (int b = 0; b < kBars; ++b) reg << juce::String ((int) std::lround (m->getParameter (kIds[b])));
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (reg, juce::Rectangle<int> (getWidth() - 90, 30, 86, 20), juce::Justification::centred, false);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawFittedText ("pull a drawbar down to sound it", juce::Rectangle<int> (getWidth() - 90, 56, 86, 40), juce::Justification::centred, 3);
        }

        void mouseDown (const juce::MouseEvent& e) override { dragBar = barAt (e.position); dragTo (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { dragTo (e.position); }
        void mouseUp (const juce::MouseEvent&) override { dragBar = -1; }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            // twice: all the way in, or all the way out
            const int b = barAt (e.position);
            if (auto* m = organ(); m != nullptr && b >= 0)
            {
                m->setParameter (kIds[b], m->getParameter (kIds[b]) > 0.5f ? 0.0f : 8.0f);
                if (paramValuesChangedByContent) paramValuesChangedByContent();
                repaint();
            }
        }

    private:
        int barAt (juce::Point<float> p) const
        {
            for (int b = 0; b < kBars; ++b)
                if (slot (b).expanded (4.0f, 0.0f).contains (p)) return b;
            return -1;
        }
        void dragTo (juce::Point<float> p)
        {
            auto* m = organ();
            if (m == nullptr || dragBar < 0) return;
            const auto s = slot (dragBar);
            const float stepH = (s.getHeight() - 22.0f) / 8.0f;
            const float v = juce::jlimit (0.0f, 8.0f, std::round ((p.y - s.getY() - 4.0f - 9.0f) / stepH));
            if (std::abs (m->getParameter (kIds[dragBar]) - v) > 0.01f)
            {
                m->setParameter (kIds[dragBar], v);
                if (paramValuesChangedByContent) paramValuesChangedByContent();
                repaint();
            }
        }
        void timerCallback() override
        {
            // (knobs moved from elsewhere: cables, automation, a preset)
            auto* m = organ();
            if (m == nullptr) return;
            juce::String now;
            for (int b = 0; b < kBars; ++b) now << juce::String (m->getParameter (kIds[b]), 1) << ",";
            if (now != shown) { shown = now; repaint(); }
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton presets { "Presets" };
        int dragBar { -1 };
        juce::String shown;
    };
}

std::unique_ptr<juce::Component> DrawbarOrganModule::createExtraContentComponent()
{
    return std::make_unique<DrawbarView> (*this);
}

AQUANODE_REGISTER_MODULE (DrawbarOrganModule, DrawbarOrganModule::descriptor)

// for tests
void drawbarSetRegistration (SynthModule* m, const char* bars)
{
    if (auto* o = dynamic_cast<DrawbarOrganModule*> (m)) o->setRegistration (bars);
}
