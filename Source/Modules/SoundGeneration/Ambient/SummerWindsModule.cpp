//==============================================================================
// Summer Winds (6.7.10) - wind chimes: a set of tuned tubes the wind plays.
//
// Modelled on a recorded set: six tubes, E5 F#5 G#5 B5 C#6 E6 (an E major
// pentatonic, each a little flat, as the recording is). Struck, a tube
// sounds its pitch for a long time (about 25 s on the low tubes, 9 s on the
// highest), a bright mode at 2.64 times the pitch that is gone within a
// second, and a faint one near 5.1 times; the upper modes come in close
// pairs that beat slowly - the shimmer of a real chime.
//
// Wind: how strong the breeze is. It comes in gusts (a slow wander of its
// own); the stronger the gust, the more often and harder the clapper swings
// into the tubes, often bouncing on to a neighbour. Wind In adds to the knob
// (an LFO or an envelope: gusts of your own). Keys strike chimes at their own
// pitch; Tuning: As Recorded, the Major or Minor Pentatonic of Key (Octave),
// or Held Keys (the tubes take the held notes; as recorded when none are).
// Decay: how long they ring. Brightness: the upper modes. Clack: the
// clapper's knock. Spread: the tubes across the stereo field. Air: a breath
// of the wind itself (off by default - it is about the tones).
// Global. Inputs: Midi In, Wind In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class SummerWindsModule final : public SynthModule
{
public:
    enum P { pWind = 0, pTuning, pKey, pOctave, pDecay, pBright, pClack, pSpread, pAir, pLevel, pTrim, pAirNew };
    static constexpr int kTubes = 6;
    static constexpr int kKeyVoices = 10;
    static constexpr int kVoices = kTubes + kKeyVoices;
    static constexpr int kModes = 5;

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        for (auto& v : voices) v = {};
        // (the breeze already blowing: it plays as soon as it is added)
        numHeld = 0; gust = param (pWind) * 0.01f * 0.75f; gustTarget = gust; gustTimer = 0.5;
        clapperTube = 0; bounceIn = -1; clickEnv = 0.0f;
        retune = true;
    }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (m.isNoteOn())
        {
            addHeld (m.getNoteNumber());
            // a key strikes a chime at its own pitch
            int slot = kTubes + nextKey; nextKey = (nextKey + 1) % kKeyVoices;
            const double f = aquanode::midiNoteToHz ((double) m.getNoteNumber() + pitchBendSemitones());   // (patch tuning, 6.9.10)
            tune (voices[slot], f, 0.25f * (float) (((m.getNoteNumber() * 7) % 11) - 5) / 5.0f);
            strike (voices[slot], 0.35f + 0.65f * m.getFloatVelocity());
            if ((int) std::lround (param (pTuning)) == 3) retune = true;
        }
        else if (m.isNoteOff()) { removeHeld (m.getNoteNumber()); if ((int) std::lround (param (pTuning)) == 3) retune = true; }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) numHeld = 0;
    }

    void blockStart() override
    {
        const int tuning = (int) std::lround (param (pTuning)), key = (int) std::lround (param (pKey)), oct = (int) std::lround (param (pOctave));
        const float decay = param (pDecay) * 0.01f, bright = param (pBright) * 0.01f;
        if (tuning != lastTuning || key != lastKey || oct != lastOct || decay != lastDecay || bright != lastBright) retune = true;
        if (retune)
        {
            retune = false;
            lastTuning = tuning; lastKey = key; lastOct = oct; lastDecay = decay; lastBright = bright;
            double f[kTubes];
            tubeFrequencies (tuning, key, oct, f);
            const float spread = param (pSpread) * 0.01f;
            for (int t = 0; t < kTubes; ++t)
                tune (voices[t], f[t], spread * ((float) t / (float) (kTubes - 1) * 2.0f - 1.0f) * (t % 2 == 0 ? 1.0f : -0.6f));
            for (int k = kTubes; k < kVoices; ++k) retuneDecay (voices[k]);
        }
        clackLevel = param (pClack) * 0.01f;
        // (6.9.15) Air: its whole range is what 0 - 10 % was (that was far too loud); a patch saved
        // before keeps its old Air (hidden now) and sounds the same
        airLevel = param (pAirNew) * 0.001f + param (pAir) * 0.01f;
        level = param (pLevel);
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const double sr = sampleRate;
        // the wind: gusts wandering slowly towards new strengths
        const float windIn = isInputConnected (1) ? inputs[1][0] : 0.0f;   // (inputs: Midi In, Wind In)
        const float wind = juce::jlimit (0.0f, 1.0f, param (pWind) * 0.01f + windIn);
        gustTimer -= 1.0 / sr;
        if (gustTimer <= 0.0)
        {
            gustTarget = juce::jlimit (0.0f, 1.0f, wind * (0.25f + 1.1f * rnd()));
            gustTimer = 1.2 + 4.0 * rnd();
        }
        gust += (gustTarget - gust) * (float) (1.0 / (1.6 * sr));
        // the clapper: strikes come more often (and harder) in a gust
        const float rate = 7.0f * gust * gust * wind;   // strikes a second
        if (rate > 0.0f && rnd() < rate / (float) sr)
        {
            // it swings to a neighbour mostly, now and then across
            const int step = rnd() < 0.75f ? (rnd() < 0.5f ? 1 : kTubes - 1) : 1 + (int) (rnd() * (kTubes - 1));
            clapperTube = (clapperTube + step) % kTubes;
            strike (voices[clapperTube], juce::jlimit (0.08f, 1.0f, 0.25f + 0.75f * gust * (0.5f + 0.5f * rnd())));
            // a bounce on to the next tube, softer, a few tens of milliseconds later
            if (rnd() < 0.45f) { bounceIn = (int) ((0.035 + 0.06 * rnd()) * sr); bounceVel = 0.3f + 0.4f * gust; }
        }
        if (bounceIn > 0 && --bounceIn == 0)
        {
            clapperTube = (clapperTube + (rnd() < 0.5f ? 1 : kTubes - 1)) % kTubes;
            strike (voices[clapperTube], bounceVel * (0.5f + 0.5f * rnd()));
        }

        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            float s = 0.0f;
            for (int m = 0; m < kModes; ++m)
            {
                auto& md = v.mode[m];
                const float nc = (md.c * md.rc - md.s * md.rs) * md.decay;
                md.s = (md.s * md.rc + md.c * md.rs) * md.decay;
                md.c = nc;
                s += md.s * md.gain;
            }
            // the knock of the clapper on it
            if (v.knock > 1.0e-5f)
            {
                noise = noise * 1664525u + 1013904223u;
                const float wn = (float) (noise >> 9) / 4194304.0f - 1.0f;
                // a soft tock, not a tick: the noise band-passed to ~1.5 .. 6 kHz
                v.knockLp += knockLpK * (wn - v.knockLp);
                v.knockLp2 += knockHpK * (v.knockLp - v.knockLp2);
                s += (v.knockLp - v.knockLp2) * v.knock * clackLevel * 0.35f;
                v.knock *= knockK;
            }
            l += s * v.gl; r += s * v.gr;
            if (++v.check >= 4096)
            {
                v.check = 0;
                float e = 0.0f;
                for (auto& md : v.mode) e += md.c * md.c + md.s * md.s;
                if (e < 1.0e-9f && v.knock < 1.0e-5f) v.active = false;
            }
        }
        // a breath of the wind itself (Air), following the gusts
        if (airLevel > 0.001f)
        {
            noise = noise * 1664525u + 1013904223u;
            const float wn = (float) (noise >> 9) / 4194304.0f - 1.0f;
            airLp1 += 0.04f * (wn - airLp1); airLp2 += 0.04f * (airLp1 - airLp2);
            const float a = (airLp1 - airLp2) * 6.0f * airLevel * (0.15f + gust);
            l += a; r += a * 0.9f;
        }
        outputs[0] = { l * level, r * level };
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.summerwinds";
        d.displayName = "Summer Winds";
        d.searchKeywords = "wind chimes bells ambient tubes garden pentatonic nature relaxing summer";
        d.description =
            "Wind chimes: tuned tubes the wind plays, modelled on a recorded set (E5 F#5 G#5 B5 C#6 E6, a little flat): each tube "
            "rings its pitch for many seconds, with a bright mode that fades within a second and slowly beating pairs "
            "above it. Wind: the breeze - it comes in gusts, and the stronger the gust the more often and harder the "
            "clapper swings into the tubes (Wind In adds to it). Keys strike chimes at their own pitch. Tuning: As "
            "Recorded, the Major or Minor Pentatonic of Key (Octave), or Held Keys. Decay, Brightness, Clack (the "
            "knock), Spread, Air (a breath of the wind itself).";
        d.section = ModuleSection::Oscillator;
        d.sockets = { midiIn ("midiIn", "MIDI In"), modIn ("windIn", "Wind In"), audioOut ("out", "Audio Out") };
        d.params = {
            makeRotary ("wind", "Wind", 0.0f, 100.0f, 40.0f, 0, "%", false, 1.0f),
            makeCombo ("tuning", "Tuning", { "As Recorded", "Major Pentatonic", "Minor Pentatonic", "Held Keys" }, 0, 0, 2).noMod(),
            makeSteppedList ("key", "Key", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 4, 0),
            makeRotary ("octave", "Octave", 3.0f, 7.0f, 5.0f, 0, {}, false, 1.0f),
            makeRotary ("decay", "Decay", 15.0f, 200.0f, 70.0f, 1, "%", false, 1.0f),
            makeRotary ("bright", "Brightness", 0.0f, 100.0f, 55.0f, 1, "%", false, 1.0f),
            makeRotary ("clack", "Clack", 0.0f, 100.0f, 18.0f, 1, "%", false, 1.0f),
            makeRotary ("spread", "Spread", 0.0f, 100.0f, 70.0f, 2, "%", false, 1.0f),
            makeRotary ("air", "Air (before 6.9.15)", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f).hide(),
            makeRotary ("level", "Level", 0.0f, 1.0f, 0.6f, 2)
        };
        d.params.push_back (makeOutputTrim (-4.0f));
        d.params.push_back (makeRotary ("airLevel", "Air", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f).whenMissing (0.0f));
        return d;
    }

private:
    struct Mode { float c { 0.0f }, s { 0.0f }, rc { 1.0f }, rs { 0.0f }, decay { 0.999f }, gain { 0.0f }; };
    struct Voice
    {
        bool active { false };
        double freq { 660.0 };
        Mode mode[kModes];
        float gl { 0.7f }, gr { 0.7f }, knock { 0.0f }, knockLp { 0.0f }, knockLp2 { 0.0f };
        int check { 0 };
    };
    Voice voices[kVoices];
    int held[128] {};
    int numHeld { 0 }, nextKey { 0 }, clapperTube { 0 }, bounceIn { -1 };
    float bounceVel { 0.5f }, gust { 0.0f }, gustTarget { 0.3f }, clackLevel { 0.25f }, airLevel { 0.0f }, level { 0.6f };
    float knockK { 0.99f }, knockLpK { 0.5f }, knockHpK { 0.2f }, clickEnv { 0.0f }, airLp1 { 0.0f }, airLp2 { 0.0f };
    double gustTimer { 0.0 };
    juce::uint32 noise { 22222u }, seed { 987654321u };
    bool retune { true };
    int lastTuning { -1 }, lastKey { -1 }, lastOct { -1 };
    float lastDecay { -1.0f }, lastBright { -1.0f };

    float rnd() noexcept { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (float) (seed >> 8) / 16777216.0f; }

    void addHeld (int n) { for (int i = 0; i < numHeld; ++i) if (held[i] == n) return; if (numHeld < 128) held[numHeld++] = n; }
    void removeHeld (int n) { for (int i = 0; i < numHeld; ++i) if (held[i] == n) { for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1]; --numHeld; return; } }

    void tubeFrequencies (int tuning, int key, int oct, double* f) const
    {
        // the recorded set (Hz)
        static const double recorded[kTubes] = { 659.2, 735.4, 823.2, 981.4, 1092.8, 1318.4 };
        auto mtof = [] (double n) { return aquanode::midiNoteToHz (n); };   // (patch tuning, 6.9.10)
        if (tuning == 1 || tuning == 2)
        {
            static const int maj[kTubes] = { 0, 2, 4, 7, 9, 12 }, mnr[kTubes] = { 0, 3, 5, 7, 10, 12 };
            for (int t = 0; t < kTubes; ++t) f[t] = mtof ((oct + 1) * 12 + key + (tuning == 1 ? maj[t] : mnr[t]));
            return;
        }
        if (tuning == 3 && numHeld > 0)
        {
            // the held notes, spread over the tubes (lowest first, then up an octave)
            int ns[128]; int n = 0;
            for (int i = 0; i < numHeld; ++i) ns[n++] = held[i];
            std::sort (ns, ns + n);
            for (int t = 0; t < kTubes; ++t)
            {
                int p = ns[t % n] + 12 * (t / n);
                while (p < 72) p += 12;
                while (p > 96) p -= 12;
                f[t] = mtof (p);
            }
            return;
        }
        for (int t = 0; t < kTubes; ++t) f[t] = recorded[t];
    }

    // a voice's modes for a pitch: the pitch (long), 2.64 x (bright, short, a pair), 5.1 x (faint, a pair)
    void tune (Voice& v, double f, float pan)
    {
        v.freq = f;
        float gl, gr;
        const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        gl = std::cos (a); gr = std::sin (a);
        v.gl = gl; v.gr = gr;
        retuneDecay (v);
    }
    void retuneDecay (Voice& v)
    {
        const double sr = sampleRate, f = v.freq;
        const double decay = juce::jmax (0.1, (double) param (pDecay) * 0.01);
        const double bright = param (pBright) * 0.01;
        // (measured: ~25 s at 660 Hz, ~9 s at 1.3 kHz, under a second for the 2.6 kHz mode)
        const double t60 = juce::jlimit (2.0, 32.0, 25.0 * std::pow (f / 660.0, -1.45)) * decay;
        const double t60b = juce::jlimit (0.2, 2.5, 0.95 * std::pow (f * 2.64 / 2600.0, -0.6)) * std::sqrt (decay);
        const double t60c = t60b * 0.45;
        struct Spec { double ratio, t60; float gain; };
        const Spec spec[kModes] = {
            { 1.0, t60, 1.0f },
            { 2.640, t60b, (float) (0.55 * bright) }, { 2.652, t60b * 1.1, (float) (0.4 * bright) },   // a pair: ~0.5 % apart, beating
            { 5.10, t60c, (float) (0.22 * bright * bright) }, { 5.13, t60c, (float) (0.16 * bright * bright) } };
        for (int m = 0; m < kModes; ++m)
        {
            const double fm = juce::jmin (f * spec[m].ratio, 0.45 * sr);
            const double w = juce::MathConstants<double>::twoPi * fm / sr;
            v.mode[m].rc = (float) std::cos (w); v.mode[m].rs = (float) std::sin (w);
            v.mode[m].decay = (float) std::exp (-6.9 / (spec[m].t60 * sr));
            v.mode[m].gain = spec[m].gain * (fm >= 0.45 * sr ? 0.0f : 1.0f);
        }
        knockK = (float) std::exp (-1.0 / (0.004 * sr));
        knockLpK = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (6000.0, 0.45 * sr) / sr);
        knockHpK = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 1500.0 / sr);
    }

    void strike (Voice& v, float velocity)
    {
        v.active = true; v.check = 0;
        // each strike adds to what still rings, at a phase of its own
        const float hard = velocity * velocity;
        for (int m = 0; m < kModes; ++m)
        {
            const float amp = (m == 0 ? velocity : hard) * 0.32f;
            const float ph = rnd() * 6.2831853f;
            v.mode[m].c += amp * std::cos (ph);
            v.mode[m].s += amp * std::sin (ph);
        }
        v.knock = velocity;
    }
};

AQUANODE_REGISTER_MODULE (SummerWindsModule, SummerWindsModule::descriptor)
