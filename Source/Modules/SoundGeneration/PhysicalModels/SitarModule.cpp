//==============================================================================
// Sitar - a plucked string on a curved bridge, with sympathetic strings.
//
// The main string is a waveguide (a delay line one period long with its losses
// in the loop). Its bridge (the jawari) is broad and gently curved: the string
// grazes it on every swing, the harder the swing the more - modelled as a
// termination that moves with the string's own displacement, so the loop's
// length is shaken at audio rate in step with the wave. That throws energy up
// into the high harmonics while the note is loud and lets it fall back as it
// fades: the sitar's buzzing tone whose bright formant sweeps down through the
// note. The wire plectrum (mizrab) plucks near the bridge; the gourd and the
// top plate colour the sum with a few resonances.
//
// Below the frets run the sympathetic strings (tarab), tuned to a raga's
// notes from the tonic (Sa). Nobody plucks them: the bridge passes them the
// main strings' sound, and those whose pitch meets a harmonic ring on - the
// shimmering halo around every note.
//
// Voiced against a set of sitar recordings (their pitch, how long each register
// rings, how bright the buzz is early and late in a note); no recording is used.
//
// Mono plays one string as a sitarist does: a note played while the last is
// still held pulls the string there (meend) without a new pluck, over the Meend
// time; Poly lets up to six notes ring. The mod wheel adds a slow gamak.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include <array>
#include <vector>

using namespace aquanode;

namespace
{
    // the tarab tunings: semitones above Sa, eleven strings from below Sa to the next octave
    struct Tuning { const char* name; int notes[11]; };
    const Tuning kTunings[] = {
        { "Kafi",     { -5, -2, 0, 2, 3, 5, 7, 9, 10, 12, 14 } },
        { "Yaman",    { -5, -1, 0, 2, 4, 6, 7, 9, 11, 12, 14 } },
        { "Bhairav",  { -5, -1, 0, 1, 4, 5, 7, 8, 11, 12, 13 } },
        { "Bhairavi", { -5, -2, 0, 1, 3, 5, 7, 8, 10, 12, 13 } },
        { "Khamaj",   { -5, -2, 0, 2, 4, 5, 7, 9, 10, 12, 14 } },
        { "Bilawal",  { -5, -1, 0, 2, 4, 5, 7, 9, 11, 12, 14 } },
        { "Marwa",    { -6, -1, 0, 1, 4, 6, 9, 11, 12, 13, 16 } },
        { "Pentatonic", { -5, -3, 0, 2, 4, 7, 9, 12, 14, 16, 19 } } };
    constexpr int kNumTunings = (int) (sizeof (kTunings) / sizeof (kTunings[0]));

    juce::StringArray tuningNames()
    {
        juce::StringArray s;
        for (const auto& t : kTunings) s.add (t.name);
        return s;
    }

    // a delay line read at a fractional, moving position (cubic Lagrange)
    struct Line
    {
        std::vector<float> buf;
        int w { 0 }, mask { 0 };
        void init (int minSize)
        {
            int n = 1;
            while (n < minSize) n <<= 1;
            buf.assign ((size_t) n, 0.0f);
            mask = n - 1; w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        float read (double delay) const   // delay >= 2
        {
            const double pos = (double) w - delay;
            const double fl = std::floor (pos);
            const float t = (float) (pos - fl);
            const int i = (int) fl;
            const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)];
            const float x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
            const float c0 = x0, c1 = x1 - xm1 * (1.0f / 3.0f) - x0 * 0.5f - x2 * (1.0f / 6.0f);
            const float c2 = 0.5f * (xm1 + x1) - x0, c3 = (x2 - xm1) * (1.0f / 6.0f) + 0.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + c0;
        }
    };

    struct Biquad
    {
        double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, z1 { 0 }, z2 { 0 };
        void bandpass (double f, double q, double sr)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jmin (f, sr * 0.45) / sr, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
            b0 = al / a0; b1 = 0.0; b2 = -al / a0; a1 = -2.0 * std::cos (w) / a0; a2 = (1.0 - al) / a0;
        }
        void highpass (double f, double sr)
        {
            const double w = juce::MathConstants<double>::twoPi * f / sr, al = std::sin (w) / (2.0 * 0.707), c = std::cos (w), a0 = 1.0 + al;
            b0 = (1.0 + c) / 2.0 / a0; b1 = -(1.0 + c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
        }
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0.0; }
    };
}

class SitarModule final : public SynthModule
{
public:
    static constexpr int kVoices = 6;
    static constexpr int kTarab = 11;

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    double voiceTailSeconds() const override { return 0.0; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int maxLen = (int) (sr / 25.0) + 16;
        for (auto& v : voices) v.line.init (maxLen);
        for (auto& v : drones) v.line.init ((int) (sr / 20.0) + 16);
        for (auto& t : tarab) t.line.init (maxLen);
        body[0].bandpass (290.0, 2.2, sr);    // the gourd's air
        gourd.bandpass (135.0, 1.6, sr);      // the gourd's lowest mode, knocked by the pluck
        body[1].bandpass (560.0, 3.0, sr);
        body[2].bandpass (1250.0, 3.5, sr);   // the top plate
        body[3].bandpass (5000.0, 1.8, sr);   // the bridge's bite
        dcBlock.highpass (55.0, sr);
        lastSa = lastTuning = -1;
        reset();
    }

    void reset() override
    {
        for (auto& v : voices) { v.line.clear(); v.active = false; v.held = false; v.lp = v.prevOut = v.lastY = 0.0f; v.exciteLeft = 0; v.age = 0; }
        for (auto& v : drones) { v.line.clear(); v.active = false; v.held = false; v.lp = v.prevOut = v.lastY = 0.0f; v.exciteLeft = 0; v.age = 0; }
        for (auto& t : tarab) { t.line.clear(); t.lp = 0.0f; }
        for (auto& b : body) b.reset();
        gourd.reset();
        dcBlock.reset();
        heldCount = 0;
        vibPhase = 0.0;
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff (m.getNoteNumber());
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (cc == 64) { sustain = m.getControllerValue() >= 64; if (! sustain) for (auto& v : voices) if (! v.held) v.releasing = true; }
            else if (cc == 1) modWheel = m.getControllerValue() / 127.0;
            else if (cc == 120 || cc == 123) allOff();
        }
        else if (m.isPitchWheel()) ownBend = (m.getPitchWheelValue() - 8192) / 8192.0;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    void midiAllNotesOff() override { allOff(); }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        const double sr = sampleRate;
        if ((counter++ & 31) == 0) updateTarabTuning();

        const double bendSemis = midiControls != nullptr ? (double) pitchBendSemitones() : ownBend * 2.0;
        vibPhase += 5.2 / sr;
        if (vibPhase >= 1.0) vibPhase -= 1.0;
        const double vib = modWheel * 0.45 * std::sin (juce::MathConstants<double>::twoPi * vibPhase);
        const double bendRatio = std::pow (2.0, (bendSemis + vib) / 12.0);

        const float jaw = param (pJawari) * 0.01f;
        const float bright = param (pBright) * 0.01f;
        const double decaySec = param (pDecay);
        const double glideCoeff = 1.0 - std::exp (-1.0 / juce::jmax (1.0, param (pMeend) * 0.001 * sr));
        const double releaseSec = 0.25;

        const float twang = param (pTwang) * 0.01f;
        const float chikari = 0.0f;   // (6.9.16: the drone strings are gone - they did not sound good)
        // one string, a step (the played ones, and the drone strings - 6.9.11)
        auto runString = [&] (Voice& v, bool drone, double& exciteOut) -> double
        {
            v.freq += glideCoeff * (v.target - v.freq);
            const double f = juce::jlimit (25.0, sr * 0.2, v.freq * (drone ? 1.0 : bendRatio));
            const double period = sr / f;

            // the loop's losses: the note's own decay (higher strings die sooner), a release when let go
            const double t60 = (v.releasing && ! v.held && ! sustain) ? releaseSec
                             : decaySec * juce::jlimit (0.35, 1.6, std::pow (220.0 / f, 0.45)) * (drone ? 1.3 : 1.0);
            const float g = (float) std::pow (0.001, 1.0 / (t60 * f));
            // the loop filter: two taps, a touch of the last sample - brighter strings keep more of their highs
            const float dark = 1.0f - bright;
            const float smear = (0.03f + 0.12f * dark * dark) * (float) juce::jlimit (0.6, 1.4, std::sqrt (f / 220.0));

            // the mizrab: a sharp, bright pluck near the bridge
            float excite = 0.0f;
            if (v.exciteLeft > 0)
            {
                const int n = v.exciteLen - v.exciteLeft;
                // one period's worth: a sharp edge and a little scrape of the wire
                const float shape = std::exp (-(float) n / juce::jmax (1.0f, (float) v.exciteLen * 0.25f));
                const float noise = (random.nextFloat() * 2.0f - 1.0f) * 0.5f;
                const float raw = v.vel * (1.0f - (float) n / (float) v.exciteLen + noise) * shape * 0.6f;
                v.exciteLp += (0.3f + 0.5f * bright) * (raw - v.exciteLp);   // the plectrum's edge
                excite = v.exciteLp;
                --v.exciteLeft;
            }

            // the jawari: the string grazing the curved bridge - the termination moves with the
            // displacement, more on a loud swing, so the buzz brightens with the note and sweeps down
            const float disp = std::abs (v.prevOut);
            // (6.9.11) Twang: the bridge grazed much harder for the first tenth of a second
            const double twangNow = twang > 0.0f ? 1.0 + 2.5 * (double) twang * std::exp (-(double) v.age / (0.07 * sr)) : 1.0;
            const double graze = (double) jaw * (0.5 + 0.009 * period) * (double) juce::jmin (1.5f, disp * 2.0f) * twangNow;
            const double delay = juce::jlimit (3.0, (double) v.line.mask - 4.0, period - (double) smear - graze);
            const float y = v.line.read (delay);
            const float looped = g * ((1.0f - smear) * y + smear * v.lastY);
            v.lastY = y;
            v.lp += 0.002f * (std::abs (looped) - v.lp);   // (how loud it still is)
            float input = excite - v.pickComb.process (excite);   // the pluck position's comb
            v.line.push (looped + input);
            v.prevOut = looped;
            // the buzz: the bridge contact's formant, high while the swing is wide, sweeping down as it
            // fades (about the 18th harmonic at the pluck, the 8th to 9th a couple of seconds on)
            const float a = std::abs (looped);
            v.env += (a > v.env ? 0.02f : 0.0004f) * (a - v.env);
            if (v.env > v.envPeak) v.envPeak = v.env;
            if ((v.age & 31) == 0)
            {
                const double ratio = juce::jlimit (1.0e-4, 1.0, (double) (v.env / juce::jmax (1.0e-6f, v.envPeak)));
                double n = juce::jlimit (4.0, 22.0, 18.0 * std::pow (ratio, 0.16));
                if (twang > 0.0f)   // (6.9.11) the twang's buzz starts higher still
                    n = juce::jmin (34.0, n * (1.0 + 0.7 * (double) twang * std::exp (-(double) v.age / (0.08 * sr))));
                const double fF = juce::jlimit (300.0, sr * 0.4, n * f);
                v.bzG = std::tan (juce::MathConstants<double>::pi * fF / sr);
                v.bzR = 1.0 / (2.0 * 3.5);
            }
            const double hpB = ((double) looped - (2.0 * v.bzR + v.bzG) * v.bz1 - v.bz2) / (1.0 + v.bzG * (2.0 * v.bzR + v.bzG));
            const double bpB = v.bzG * hpB + v.bz1;
            v.bz1 = v.bzG * hpB + bpB;
            const double lpB = v.bzG * bpB + v.bz2;
            v.bz2 = v.bzG * bpB + lpB;
            const double buzz = (double) jaw * 9.0 * 2.0 * v.bzR * bpB;
            // (the twang's extra buzz rounded off, so it bites without spiking)
            const double voiced = (double) looped + buzz + (twangNow > 1.0 ? 2.0 * (twangNow - 1.0) * buzz / (1.0 + 4.0 * std::abs (buzz)) : 0.0);
            if (++v.age > (int) (sr * 0.05) && v.lp < 2.0e-5f)
                v.active = false;
            exciteOut = (double) excite;
            return voiced;
        };


        double sum = 0.0, exciteSum = 0.0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            double ex = 0.0;
            sum += runString (v, false, ex);
            exciteSum += ex;
        }
        // (6.9.11) the drone strings: low Sa, Pa and Sa, struck with every new pluck
        if (chikari > 0.0f)
            for (auto& v : drones)
            {
                if (! v.active) continue;
                double ex = 0.0;
                sum += (double) chikari * 0.5 * runString (v, true, ex);
                exciteSum += (double) chikari * 0.5 * ex;
            }

        // the sympathetic strings, fed through the bridge
        const float symp = param (pSymp) * 0.01f;
        double tar = 0.0;
        if (symp > 0.0f)
        {
            const float feed = (float) sum * 0.006f;
            for (auto& t : tarab)
            {
                const float y = t.line.read (t.period - 0.5);
                t.lp += 0.5f * (y - t.lp);
                const float looped = t.g * t.lp;
                t.line.push (looped + feed);
                tar += looped;
            }
        }

        // the gourd and the top plate
        const double bodyAmt = param (pBody) * 0.01;
        double coloured = sum;
        if (bodyAmt > 0.0)
            coloured += bodyAmt * (2.5 * gourd.process (sum + 6.0 * exciteSum) + 0.8 * body[0].process (sum) + 0.5 * body[1].process (sum) + 0.9 * body[2].process (sum) + 1.6 * body[3].process (sum));
        const double out = dcBlock.process (coloured * 0.6 + tar * (double) symp * 2.5);
        const float gain = param (pLevel) * outputTrim();
        const float o = std::isfinite (out) ? (float) juce::jlimit (-4.0, 4.0, out) * gain : 0.0f;
        // a little width: the sympathetic halo slightly to one side
        const float side = (float) (tar * (double) symp * 0.6) * gain;
        outputs[0] = { o + (std::isfinite (side) ? side : 0.0f), o - (std::isfinite (side) ? side : 0.0f) };
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.sitar";
        d.displayName = "Sitar";
        d.section = ModuleSection::Oscillator;
        d.description = "A sitar, physically modelled: a wire-plucked string on the broad, curved bridge (jawari) that makes it "
                        "buzz - the louder the swing, the brighter, so the bright formant sweeps down through every note - with "
                        "eleven sympathetic strings tuned to a raga's notes from Sa, ringing along whenever a note meets them, and "
                        "the gourd's colour. Jawari is how far the string grazes the bridge (0 % a plain plucked string), "
                        "Brightness the plectrum and the string's highs, Decay how long it rings, Sympathetic the tarab's halo. "
                        "Mono plays one string: a note played while the last is held pulls the string there (meend) over the "
                        "Meend time, without a new pluck. Poly lets six notes ring. Mod wheel: a slow gamak. Twang: how hard the string "
                        "grazes the bridge right at the pluck.";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
        d.params.push_back (makeCombo ("mode", "Mode", { "Mono", "Poly" }, 0, 0, 2).noMod());
        d.params.push_back (makeRotary ("meend", "Meend", 5.0f, 600.0f, 120.0f, 0, "ms", true, 1.0f));
        d.params.push_back (makeRotary ("jawari", "Jawari", 0.0f, 100.0f, 65.0f, 0, "%", false, 1.0f));
        d.params.push_back (makeRotary ("bright", "Brightness", 0.0f, 100.0f, 60.0f, 1, "%", false, 1.0f));
        d.params.push_back (makeRotary ("decay", "Decay", 1.0f, 20.0f, 8.0f, 1, "s", true, 0.01f));
        d.params.push_back (makeRotary ("body", "Body", 0.0f, 100.0f, 50.0f, 1, "%", false, 1.0f));
        d.params.push_back (makeRotary ("symp", "Sympathetic", 0.0f, 100.0f, 45.0f, 2, "%", false, 1.0f));
        d.params.push_back (makeSteppedList ("sa", "Sa (Tonic)", saChoices(), 1, 2));
        d.params.push_back (makeCombo ("tarabTuning", "Tarab Tuning", tuningNames(), 0, 2, 2).noMod());
        d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 3));
        d.params.push_back (makeOutputTrim (0.0f));
        // (6.9.11) the bridge's twang at the pluck, and the drone strings (patches from before: none of either)
        d.params.push_back (makeRotary ("twang", "Twang", 0.0f, 100.0f, 45.0f, 0, "%", false, 1.0f).whenMissing (0.0f));
        // (6.9.16: the drone strings removed; the knob kept hidden, so the indices stay)
        d.params.push_back (makeRotary ("chikari", "Drone Strings", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f).whenMissing (0.0f).hide());
        d.rowSections = { { 0, "String" }, { 1, "Tone" }, { 2, "Tarab" }, { 3, "Out" } };
        return d;
    }

private:
    enum P { pMode = 0, pMeend, pJawari, pBright, pDecay, pBody, pSymp, pSaParam, pTuningParam, pLevel, pOutTrim, pTwang, pChikari };

    static juce::StringArray saChoices()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }

    struct PickComb   // excitation minus itself a little later: the plucking point's notches
    {
        std::array<float, 2048> buf {};
        int pos { 0 }, delay { 8 };
        float process (float x)
        {
            const float y = buf[(size_t) ((pos - delay) & 2047)];
            buf[(size_t) pos] = x;
            pos = (pos + 1) & 2047;
            return y;
        }
    };

    struct Voice
    {
        Line line;
        bool active { false }, held { false }, releasing { false };
        int note { -1 }, exciteLeft { 0 }, exciteLen { 1 }, age { 0 };
        double freq { 220.0 }, target { 220.0 };
        float vel { 0.8f }, lp { 0.0f }, prevOut { 0.0f }, lastY { 0.0f }, exciteLp { 0.0f }, env { 0.0f }, envPeak { 0.0f };
        double bzG { 0.3 }, bzR { 0.15 }, bz1 { 0.0 }, bz2 { 0.0 };
        PickComb pickComb;
    };

    struct Tarab
    {
        Line line;
        double period { 100.0 };
        float g { 0.99f }, lp { 0.0f };
    };

    void pluck (Voice& v, int note, float velocity)
    {
        v.note = note;
        v.target = midiNoteToHz ((double) note);
        v.vel = 0.25f + 0.75f * velocity;
        const double period = sampleRate / v.target;
        v.exciteLen = juce::jmax (4, (int) period);
        v.exciteLeft = v.exciteLen;
        v.pickComb.delay = juce::jlimit (2, 2000, (int) std::lround (period * 0.165));
        v.active = true; v.held = true; v.releasing = false; v.age = 0; v.env = v.envPeak = 0.0f;
    }

    // (6.9.11) the drone strings struck along with a new pluck, a little softer, from Sa
    void strikeDrones (float velocity) { juce::ignoreUnused (velocity); }   // (6.9.16: no drone strings)

    void noteOn (int note, float velocity)
    {
        const bool mono = std::lround (param (pMode)) == 0;
        if (mono)
        {
            auto& v = voices[0];
            const bool legato = v.active && v.held && heldCount > 0;
            ++heldCount;
            heldStack.push_back (note);
            if (legato) { v.note = note; v.target = midiNoteToHz ((double) note); return; }   // meend: pulled, not plucked
            if (! v.active) v.freq = midiNoteToHz ((double) note);
            pluck (v, note, velocity);
            v.freq = v.target;   // a new pluck starts on its note
            strikeDrones (velocity);
            return;
        }
        // poly: a free string, else the quietest
        int pick = -1;
        for (int i = 0; i < kVoices; ++i) if (! voices[(size_t) i].active) { pick = i; break; }
        if (pick < 0)
        {
            float lowest = 1.0e9f;
            for (int i = 0; i < kVoices; ++i)
                if (std::abs (voices[(size_t) i].lp) < lowest) { lowest = std::abs (voices[(size_t) i].lp); pick = i; }
        }
        auto& v = voices[(size_t) pick];
        pluck (v, note, velocity);
        v.freq = v.target;
        strikeDrones (velocity);
    }

    void noteOff (int note)
    {
        const bool mono = std::lround (param (pMode)) == 0;
        if (mono)
        {
            heldStack.erase (std::remove (heldStack.begin(), heldStack.end(), note), heldStack.end());
            heldCount = (int) heldStack.size();
            auto& v = voices[0];
            if (heldCount > 0)
            {   // back to the note still held, pulled
                v.note = heldStack.back();
                v.target = midiNoteToHz ((double) v.note);
                return;
            }
            v.held = false; v.releasing = true;
            return;
        }
        for (auto& v : voices)
            if (v.active && v.held && v.note == note) { v.held = false; v.releasing = true; }
    }

    void allOff()
    {
        for (auto& v : voices) { v.held = false; v.releasing = true; }
        for (auto& v : drones) v.releasing = true;
        heldStack.clear(); heldCount = 0;
    }

    void updateTarabTuning()
    {
        const int sa = (int) std::lround (param (pSaParam));
        const int tu = juce::jlimit (0, kNumTunings - 1, (int) std::lround (param (pTuningParam)));
        const double dec = param (pDecay);
        if (sa == lastSa && tu == lastTuning && std::abs (dec - lastDecay) < 0.01) return;
        lastSa = sa; lastTuning = tu; lastDecay = dec;
        for (int k = 0; k < kTarab; ++k)
        {
            const double hz = midiNoteToHz (60.0 + sa + kTunings[tu].notes[k]);
            tarab[(size_t) k].period = juce::jlimit (4.0, (double) tarab[(size_t) k].line.mask - 4.0, sampleRate / hz);
            const double t60 = juce::jlimit (1.5, 8.0, dec * 0.6);
            tarab[(size_t) k].g = (float) std::pow (0.001, 1.0 / (t60 * hz));
        }
    }

    std::array<Voice, kVoices> voices;
    std::array<Voice, 3> drones;   // (6.9.11) low Sa, Pa, Sa
    std::array<Tarab, kTarab> tarab;
    std::array<Biquad, 4> body;
    Biquad gourd;
    Biquad dcBlock;
    std::vector<int> heldStack;
    juce::Random random { 0x51a7 };
    int heldCount { 0 }, lastSa { -1 }, lastTuning { -1 };
    double lastDecay { -1.0 };
    bool sustain { false };
    double modWheel { 0.0 }, ownBend { 0.0 }, vibPhase { 0.0 };
    uint32_t counter { 0 };
};

AQUANODE_REGISTER_MODULE (SitarModule, SitarModule::descriptor)
