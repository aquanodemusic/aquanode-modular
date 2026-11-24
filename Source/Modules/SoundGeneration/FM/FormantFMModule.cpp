//==============================================================================
// Formant FM (6.9.2) - singing, speaking and whispering FM.
//
// Eight voiced operators, each a formant: a peak of the played note's
// harmonics around its own frequency, made the classic FM way (Chowning's
// singing voice) - a carrier on the harmonic nearest the formant (two, cross-
// faded, when the formant sits between harmonics) phase-modulated at the
// note's frequency, the index setting how wide the peak's skirt spreads. So the
// formant stays put while the pitch moves under it, as in a voice.
// Eight unvoiced operators: noise through a resonant band each (the s, the sh,
// breath, air). Breath adds aspiration: noise through the voiced formants.
//
// Formant Sequences move the first five voiced and four unvoiced operators
// through vowels and consonants in time - vowel cycles, "wah", "yeah", a
// whisper, nonsense speech - from a small vocabulary of phonemes: the vowel
// formants are the published averages of Peterson & Barney (1952), the
// consonants approximations from the phonetics literature. Shift moves every
// formant (up: a smaller throat), Track lets them follow the key a little,
// Width scales every skirt. Ensemble sings each note as one, two or three
// voices, detuned by Spread and spread across the stereo field; Vibrato with
// its delay, a formant Sweep (overtone singing on V2), an ADSR.
//
// The card: the formants drawn as a spectrum, voiced and unvoiced, each a
// handle to drag (across: frequency, up and down: level, the wings: skirt);
// Presets.
// Global (its own voices, MPE: each channel its own bend).
// Input: Midi In. Output: Audio Out (stereo).
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/LookPalette.h"

using namespace aquanode;

namespace formantfm
{
    constexpr int kOps = 8;
    constexpr int kVoices = 16;
    constexpr int kSeqVoiced = 5, kSeqNoise = 4;
    constexpr int kControl = 16;   // control rate: every 16 samples

    struct SinTable
    {
        float t[4097];
        SinTable() { for (int i = 0; i <= 4096; ++i) t[i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / 4096.0); }
    };
    inline const SinTable& sinTable() { static const SinTable s; return s; }
    inline float sin01 (double ph)
    {
        ph -= std::floor (ph);
        const double x = ph * 4096.0;
        const int i = (int) x;
        const float f = (float) (x - i);
        const auto& t = sinTable().t;
        return t[i] + f * (t[i + 1] - t[i]);
    }

    //==========================================================================
    // the vocabulary
    //==========================================================================
    struct Frame
    {
        float vf[kSeqVoiced], vdb[kSeqVoiced], vb[kSeqVoiced];
        float voiced;
        float uf[kSeqNoise], udb[kSeqNoise], ub[kSeqNoise];
        float noise;
    };
    struct Vowel { const char* sym; float f[5]; float db[5]; };
    // Peterson & Barney (1952): men's averages, F1 - F3 and their levels; F4, F5 typical values
    const Vowel kVowels[] = {
        { "i",  { 270, 2290, 3010, 3500, 4500 }, { -4, -24, -28, -34, -40 } },
        { "ih", { 390, 1990, 2550, 3500, 4500 }, { -3, -23, -27, -34, -40 } },
        { "e",  { 400, 2100, 2700, 3500, 4500 }, { -3, -20, -25, -32, -38 } },
        { "eh", { 530, 1840, 2480, 3500, 4500 }, { -2, -17, -24, -32, -38 } },
        { "ae", { 660, 1720, 2410, 3500, 4500 }, { -1, -12, -22, -30, -36 } },
        { "a",  { 730, 1090, 2440, 3500, 4500 }, { -1,  -5, -28, -32, -38 } },
        { "aw", { 570,  840, 2410, 3500, 4500 }, {  0,  -7, -34, -36, -42 } },
        { "o",  { 450,  800, 2600, 3500, 4500 }, { -1, -10, -35, -38, -44 } },
        { "oo", { 440, 1020, 2240, 3500, 4500 }, { -1, -12, -34, -36, -42 } },
        { "u",  { 300,  870, 2240, 3500, 4500 }, { -3, -19, -43, -44, -48 } },
        { "uh", { 640, 1190, 2390, 3500, 4500 }, { -1, -10, -27, -32, -38 } },
        { "er", { 490, 1350, 1690, 3300, 4500 }, { -5, -15, -20, -30, -36 } },
        // the voiced consonants: nasals, liquids, glides
        { "m",  { 250, 1100, 2300, 3500, 4500 }, { -6, -26, -36, -44, -50 } },
        { "n",  { 250, 1500, 2500, 3500, 4500 }, { -6, -28, -36, -44, -50 } },
        { "l",  { 360, 1300, 2700, 3500, 4500 }, { -3, -16, -28, -36, -42 } },
        { "r",  { 490, 1350, 1690, 3300, 4500 }, { -5, -14, -18, -30, -36 } },
        { "w",  { 300,  700, 2200, 3500, 4500 }, { -4, -20, -42, -44, -48 } },
        { "y",  { 270, 2300, 3000, 3500, 4500 }, { -5, -22, -26, -34, -40 } },
    };
    constexpr float kBw[5] = { 70.0f, 100.0f, 140.0f, 220.0f, 280.0f };
    struct Noise { const char* sym; float f[4]; float b[4]; float db[4]; float amp; };
    const Noise kNoises[] = {
        { "s",  { 4500, 6500, 8500, 3000 }, { 1500, 2500, 3000, 800 }, {  -8,   0,  -4, -24 }, 0.9f },
        { "sh", { 2600, 3500, 5000, 1800 }, {  700, 1200, 2500, 600 }, {   0,  -3,  -9, -18 }, 0.9f },
        { "f",  { 1800, 4500, 7500, 9000 }, { 3000, 3000, 4000, 3000 }, { -12, -10, -10, -14 }, 0.45f },
        { "th", { 1500, 4000, 6500, 8500 }, { 3000, 3000, 4000, 3000 }, { -14, -12, -12, -16 }, 0.35f },
        { "t+", { 4000, 5500, 2800, 8000 }, { 3000, 3000, 1500, 3000 }, {  -2,   0,  -8,  -8 }, 1.0f },
        { "k+", { 2500, 3500, 1800, 5000 }, { 1200, 1500, 1000, 2500 }, {   0,  -4,  -6, -14 }, 1.0f },
        { "p+", { 1000, 2000, 3500,  600 }, { 1500, 1500, 2500,  500 }, {   0,  -6, -12, -10 }, 0.7f },
    };

    inline const Vowel* findVowel (const juce::String& s) { for (const auto& v : kVowels) if (s == v.sym) return &v; return nullptr; }
    inline const Noise* findNoise (const juce::String& s) { for (const auto& n : kNoises) if (s == n.sym) return &n; return nullptr; }

    inline void setVowel (Frame& f, const Vowel& v, float voiced)
    {
        for (int k = 0; k < kSeqVoiced; ++k) { f.vf[k] = v.f[k]; f.vdb[k] = v.db[k]; f.vb[k] = kBw[k]; }
        f.voiced = voiced;
    }
    inline void setNoise (Frame& f, const Noise& n, float amp)
    {
        for (int k = 0; k < kSeqNoise; ++k) { f.uf[k] = n.f[k]; f.ub[k] = n.b[k]; f.udb[k] = n.db[k]; }
        f.noise = amp;
    }
    // noise shaped like a vowel: whisper, h
    inline void setNoiseFromVowel (Frame& f, const Vowel& v, float amp)
    {
        for (int k = 0; k < kSeqNoise; ++k) { f.uf[k] = v.f[k]; f.ub[k] = kBw[k] * 1.8f; f.udb[k] = v.db[k]; }
        f.noise = amp;
    }

    struct Step { Frame frame; float dur; float glide; };
    struct Sequence { juce::String name; std::vector<Step> steps; float length { 0.0f }; };

    // "sym:seconds ...": vowels, *vowel (whispered), h (before a vowel), s sh f th,
    // the stops p t k (b d g: voiced), m n l r w y, _ (a pause)
    inline Sequence parse (const juce::String& name, const juce::String& text)
    {
        Sequence seq; seq.name = name;
        Frame prev {};
        setVowel (prev, kVowels[5], 0.0f);
        setNoise (prev, kNoises[0], 0.0f);
        auto tokens = juce::StringArray::fromTokens (text, " ", "");
        auto nextVowel = [&] (int from) -> const Vowel*
        {
            for (int j = from; j < tokens.size(); ++j)
                if (auto* v = findVowel (tokens[j].upToFirstOccurrenceOf (":", false, false).trimCharactersAtStart ("*"))) return v;
            return &kVowels[5];
        };
        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto sym = tokens[i].upToFirstOccurrenceOf (":", false, false);
            const float dur = juce::jmax (0.01f, tokens[i].fromFirstOccurrenceOf (":", false, false).getFloatValue());
            Frame f = prev;
            float glide = juce::jmin (0.06f, dur * 0.45f);
            if (sym.startsWith ("*"))
            {
                if (auto* v = findVowel (sym.substring (1))) { setVowel (f, *v, 0.0f); setNoiseFromVowel (f, *v, 1.0f); }
            }
            else if (auto* v = findVowel (sym))
            {
                const bool consonant = sym == "m" || sym == "n" || sym == "l" || sym == "r" || sym == "w" || sym == "y";
                setVowel (f, *v, consonant ? (sym == "m" || sym == "n" ? 0.45f : 0.7f) : 1.0f);
                f.noise = 0.0f;
                if (consonant) glide = juce::jmin (0.035f, dur * 0.5f);
            }
            else if (sym == "h")
            {
                const auto* v = nextVowel (i + 1);
                setVowel (f, *v, 0.0f);
                setNoiseFromVowel (f, *v, 0.55f);
                glide = 0.015f;
            }
            else if (sym == "p" || sym == "t" || sym == "k" || sym == "b" || sym == "d" || sym == "g")
            {
                // the closure, then the burst
                const bool voicedStop = sym == "b" || sym == "d" || sym == "g";
                const juce::String burst = (sym == "p" || sym == "b") ? "p+" : (sym == "t" || sym == "d") ? "t+" : "k+";
                Frame closure = prev;
                closure.voiced = voicedStop ? 0.12f : 0.0f;
                closure.vf[0] = 200.0f;
                closure.noise = 0.0f;
                seq.steps.push_back ({ closure, dur * 0.6f, 0.012f });
                seq.length += dur * 0.6f;
                Frame b = closure;
                setNoise (b, *findNoise (burst), voicedStop ? 0.45f : 0.8f);
                seq.steps.push_back ({ b, dur * 0.4f, 0.003f });
                seq.length += dur * 0.4f;
                prev = b;
                continue;
            }
            else if (auto* n = findNoise (sym))
            {
                f.voiced = 0.0f;
                setNoise (f, *n, n->amp);
                glide = juce::jmin (0.03f, dur * 0.4f);
            }
            else if (sym == "_")
            {
                f.voiced = 0.0f; f.noise = 0.0f; glide = 0.02f;
            }
            else continue;
            seq.steps.push_back ({ f, dur, glide });
            seq.length += dur;
            prev = f;
        }
        return seq;
    }

    const juce::StringArray kSequenceNames { "Off", "A-E-I-O-U", "Wah", "Wow", "Yeah", "Ooh-Aah", "Hey", "Choir Morph", "Whisper", "Speak", "Ha-Ha-Ha", "Sing Words" };
    inline const std::vector<Sequence>& sequences()
    {
        static const std::vector<Sequence> s = []
        {
            std::vector<Sequence> v;
            v.push_back ({});
            v.push_back (parse ("A-E-I-O-U", "a:0.5 e:0.5 i:0.5 o:0.5 u:0.5"));
            v.push_back (parse ("Wah", "u:0.1 a:0.7"));
            v.push_back (parse ("Wow", "w:0.08 a:0.32 oo:0.2 u:0.4"));
            v.push_back (parse ("Yeah", "y:0.1 eh:0.22 ae:0.6"));
            v.push_back (parse ("Ooh-Aah", "u:0.8 a:0.8"));
            v.push_back (parse ("Hey", "h:0.08 eh:0.2 e:0.15 i:0.4"));
            v.push_back (parse ("Choir Morph", "a:1.2 o:1.2 u:1.2 o:1.2"));
            v.push_back (parse ("Whisper", "*a:0.6 *i:0.6 *u:0.6 *e:0.6"));
            v.push_back (parse ("Speak", "b:0.05 a:0.16 d:0.05 i:0.14 k:0.06 o:0.18 m:0.07 a:0.16 s:0.12 u:0.22 _:0.12 l:0.06 e:0.14 n:0.07 aw:0.2 _:0.15"));
            v.push_back (parse ("Ha-Ha-Ha", "h:0.07 a:0.15 _:0.06 h:0.07 a:0.15 _:0.06 h:0.07 a:0.3 _:0.2"));
            v.push_back (parse ("Sing Words", "l:0.08 a:0.5 _:0.04 l:0.08 a:0.5 m:0.1 oo:0.4 n:0.1 i:0.5 sh:0.12 u:0.5 r:0.1 e:0.6"));
            return v;
        }();
        return s;
    }

    // a frame at a time into a sequence (looping or holding its last frame)
    inline Frame frameAt (const Sequence& s, float t, bool loop)
    {
        if (s.steps.empty()) return {};
        if (loop && s.length > 0.0f) t = std::fmod (t, s.length);
        float start = 0.0f;
        const Frame* prev = &s.steps.back().frame;
        if (! loop) prev = &s.steps.front().frame;
        for (size_t i = 0; i < s.steps.size(); ++i)
        {
            const auto& st = s.steps[i];
            if (t < start + st.dur || i + 1 == s.steps.size())
            {
                const float a = juce::jlimit (0.0f, 1.0f, (t - start) / juce::jmax (0.001f, st.glide));
                const float w = a * a * (3.0f - 2.0f * a);
                Frame f;
                const Frame& p = (i == 0 && ! loop) ? st.frame : *prev;
                for (int k = 0; k < kSeqVoiced; ++k)
                {
                    f.vf[k] = std::exp (std::log (p.vf[k]) + w * (std::log (st.frame.vf[k]) - std::log (p.vf[k])));
                    f.vdb[k] = p.vdb[k] + w * (st.frame.vdb[k] - p.vdb[k]);
                    f.vb[k] = p.vb[k] + w * (st.frame.vb[k] - p.vb[k]);
                }
                for (int k = 0; k < kSeqNoise; ++k)
                {
                    f.uf[k] = std::exp (std::log (p.uf[k]) + w * (std::log (st.frame.uf[k]) - std::log (p.uf[k])));
                    f.udb[k] = p.udb[k] + w * (st.frame.udb[k] - p.udb[k]);
                    f.ub[k] = p.ub[k] + w * (st.frame.ub[k] - p.ub[k]);
                }
                f.voiced = p.voiced + w * (st.frame.voiced - p.voiced);
                f.noise = p.noise + w * (st.frame.noise - p.noise);
                return f;
            }
            start += st.dur;
            prev = &st.frame;
        }
        return s.steps.back().frame;
    }

    //==========================================================================
    // presets: (id, value) on top of the defaults
    //==========================================================================
    struct PV { const char* id; float v; };
    struct Preset { const char* name; std::vector<PV> values; };
    inline const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> p {
            { "Init (Aah)", {} },
            { "Choir Aah", { { "ensemble", 2 }, { "spread", 11 }, { "attack", 380 }, { "release", 1100 }, { "vibrato", 16 }, { "vibDelay", 0.35f },
                             { "v4l", -18 }, { "v4f", 2900 }, { "v5l", -24 }, { "breath", 8 }, { "shift", 2 } } },
            { "Choir Ooh", { { "ensemble", 2 }, { "spread", 10 }, { "attack", 420 }, { "release", 1200 }, { "vibrato", 14 }, { "vibDelay", 0.4f },
                             { "v1f", 300 }, { "v1l", -3 }, { "v2f", 870 }, { "v2l", -19 }, { "v3f", 2240 }, { "v3l", -40 }, { "v4f", 2900 }, { "v4l", -26 }, { "v5l", -40 }, { "breath", 10 } } },
            { "Vox Pad", { { "ensemble", 2 }, { "spread", 13 }, { "attack", 900 }, { "release", 1600 }, { "sequence", 1 }, { "speed", 35 }, { "loop", 1 }, { "vibrato", 10 }, { "breath", 12 }, { "v4l", -20 }, { "v4f", 2900 } } },
            { "Talking Lead", { { "sequence", 9 }, { "speed", 100 }, { "loop", 1 }, { "attack", 8 }, { "release", 160 }, { "voices", 1 }, { "glide", 60 }, { "vibrato", 6 }, { "width", 120 } } },
            { "Wah Pluck", { { "sequence", 2 }, { "speed", 160 }, { "loop", 0 }, { "attack", 2 }, { "decay", 420 }, { "sustain", 0 }, { "release", 220 }, { "width", 140 } } },
            { "Yeah Stab", { { "sequence", 4 }, { "speed", 120 }, { "loop", 0 }, { "attack", 4 }, { "decay", 700 }, { "sustain", 20 }, { "release", 300 }, { "ensemble", 1 }, { "spread", 8 } } },
            { "Whisper Pad", { { "sequence", 8 }, { "speed", 50 }, { "loop", 1 }, { "attack", 700 }, { "release", 1500 }, { "ensemble", 1 }, { "noiseLevel", 10 } } },
            { "Overtone Singer", { { "v1f", 420 }, { "v1l", -6 }, { "v2f", 1400 }, { "v2l", 0 }, { "v2s", 25 }, { "v3f", 2500 }, { "v3l", -30 }, { "v4l", -60 }, { "v5l", -60 },
                                   { "sweep", 9 }, { "sweepRate", 0.18f }, { "sweepOp", 2 }, { "attack", 300 }, { "release", 900 }, { "vibrato", 0 } } },
            { "Formant Bass", { { "v1f", 570 }, { "v1l", 0 }, { "v2f", 840 }, { "v2l", -6 }, { "v3l", -30 }, { "width", 180 }, { "attack", 3 }, { "decay", 380 }, { "sustain", 55 }, { "release", 120 },
                                { "vibrato", 0 }, { "track", 35 }, { "voices", 1 }, { "glide", 30 } } },
            { "Breathy Flute", { { "v1f", 300 }, { "v1l", -2 }, { "v2f", 900 }, { "v2l", -26 }, { "v3l", -50 }, { "v4l", -60 }, { "v5l", -60 }, { "breath", 30 },
                                 { "u1f", 3200 }, { "u1l", -22 }, { "u1s", 2600 }, { "attack", 90 }, { "release", 260 }, { "vibrato", 12 }, { "vibDelay", 0.25f } } },
            { "Glass Voices", { { "shift", 12 }, { "width", 40 }, { "ensemble", 2 }, { "spread", 7 }, { "attack", 250 }, { "decay", 2200 }, { "sustain", 40 }, { "release", 1800 }, { "vibrato", 8 },
                                { "v6l", -24 }, { "v6f", 5200 }, { "v6s", 60 } } },
            { "Robot Speak", { { "sequence", 9 }, { "speed", 75 }, { "loop", 1 }, { "vibrato", 0 }, { "width", 60 }, { "attack", 5 }, { "release", 120 }, { "voices", 1 }, { "glide", 0 } } },
        };
        return p;
    }

    const char* const kOpIds[kOps][3] = {
        { "v1f", "v1l", "v1s" }, { "v2f", "v2l", "v2s" }, { "v3f", "v3l", "v3s" }, { "v4f", "v4l", "v4s" },
        { "v5f", "v5l", "v5s" }, { "v6f", "v6l", "v6s" }, { "v7f", "v7l", "v7s" }, { "v8f", "v8l", "v8s" } };
    const char* const kNoiseIds[kOps][3] = {
        { "u1f", "u1l", "u1s" }, { "u2f", "u2l", "u2s" }, { "u3f", "u3l", "u3s" }, { "u4f", "u4l", "u4s" },
        { "u5f", "u5l", "u5s" }, { "u6f", "u6l", "u6s" }, { "u7f", "u7l", "u7s" }, { "u8f", "u8l", "u8s" } };
    // the defaults: an open "a", sung
    constexpr float kVoicedF[kOps] = { 730, 1090, 2440, 3500, 4500, 5500, 6500, 8000 };
    constexpr float kVoicedL[kOps] = { -1, -5, -28, -32, -38, -60, -60, -60 };
    constexpr float kVoicedS[kOps] = { 80, 100, 140, 220, 280, 300, 300, 300 };
    constexpr float kNoiseF[kOps] = { 3000, 4500, 6500, 8500, 1500, 2500, 5500, 10000 };
    constexpr float kNoiseL[kOps] = { -60, -60, -60, -60, -60, -60, -60, -60 };
    constexpr float kNoiseS[kOps] = { 1200, 1500, 2500, 3000, 800, 1000, 2000, 4000 };

    // a TPT state-variable band-pass, peak 1
    struct Bp
    {
        float g { 0.1f }, k { 1.0f }, a1 { 0 }, a2 { 0 }, a3 { 0 }, ic1 { 0 }, ic2 { 0 };
        void set (float hz, float bw, float sr)
        {
            hz = juce::jlimit (20.0f, 0.45f * sr, hz);
            g = std::tan (juce::MathConstants<float>::pi * hz / sr);
            k = juce::jlimit (0.005f, 4.0f, bw / hz);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        float process (float x)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            return k * v1;
        }
        void reset() { ic1 = ic2 = 0.0f; }
    };
}

using namespace formantfm;

class FormantFMModule final : public SynthModule
{
public:
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    double voiceTailSeconds() const override { return 0.0; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fs = (float) juce::jmax (8000.0, sr);
        for (int k = 0; k < kOps; ++k)
            for (int j = 0; j < 3; ++j)
            {
                pV[k][j] = paramIndex (kOpIds[k][j]);
                pU[k][j] = paramIndex (kNoiseIds[k][j]);
            }
        reset();
    }

    void reset() override
    {
        for (auto& v : voices) v = Voice();
        for (auto& b : chanBend) b = 0.0f;
        masterBend = 0.0f; modWheel = 0.0f;
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
            if (ch == 1) masterBend = b * 2.0f;
            else chanBend[ch] = b * 48.0f;   // an MPE member channel
        }
        else if (m.isController())
        {
            if (m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
            else if (m.getControllerNumber() == 64) { sustainPedal = m.getControllerValue() >= 64; if (! sustainPedal) releaseSustained(); }
            else if (m.getControllerNumber() == 120 || m.getControllerNumber() == 123) allOff();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
    }
    void midiAllNotesOff() override { allOff(); }

    void blockStart() override
    {
        for (int k = 0; k < kOps; ++k)
        {
            opF[k] = param (pV[k][0]); opDb[k] = param (pV[k][1]); opS[k] = param (pV[k][2]);
            nzF[k] = param (pU[k][0]); nzDb[k] = param (pU[k][1]); nzS[k] = param (pU[k][2]);
        }
        seqIndex = juce::jlimit (0, (int) sequences().size() - 1, (int) std::lround (p ("sequence")));
        speed = p ("speed") * 0.01f;
        loop = p ("loop") > 0.5f;
        seqMix = p ("seqMix") * 0.01f;
        shift = std::pow (2.0f, p ("shift") / 12.0f);
        track = p ("track") * 0.01f;
        width = p ("width") * 0.01f;
        breath = p ("breath") * 0.01f;
        noiseLevel = juce::Decibels::decibelsToGain (p ("noiseLevel"));
        ensemble = juce::jlimit (0, 2, (int) std::lround (p ("ensemble"))) + 1;
        spread = p ("spread");
        vibCents = p ("vibrato");
        vibRate = p ("vibRate");
        vibDelay = p ("vibDelay");
        sweep = p ("sweep");
        sweepRate = p ("sweepRate");
        sweepOp = (int) std::lround (p ("sweepOp"));
        attack = p ("attack") * 0.001f; decay = p ("decay") * 0.001f;
        sustain = p ("sustain") * 0.01f; release = p ("release") * 0.001f;
        velAmt = p ("velocity") * 0.01f;
        poly = juce::jlimit (1, kVoices, (int) std::lround (p ("voices")));
        glide = p ("glide") * 0.001f;
        level = p ("level");
    }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        if ((counter++ % kControl) == 0) control();
        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
            if (v.active) renderVoice (v, l, r);
        outputs[0] = { l * level, r * level };
    }

    //=== the card ==============================================================
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 200; }
    int preferredModuleWidth() const override { return 470; }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.formantfm";
        d.displayName = "Formant FM";
        d.section = ModuleSection::Oscillator;
        d.sidebarOrder = 6;
        d.searchKeywords = "formant fm voice vowel choir speech talk whisper sing sequence vocal";
        d.description =
            "Singing, speaking and whispering FM. Eight voiced operators, each a formant - the note's harmonics peaking "
            "around its own frequency, made the classic FM way, so the formant stays put while the pitch moves under it - "
            "and eight unvoiced ones, noise through a band each (the s, the sh, air). Breath sends noise through the "
            "voiced formants. Sequence moves the first five voiced and four unvoiced operators through vowels and "
            "consonants in time (A-E-I-O-U, Wah, Yeah, a whisper, nonsense speech; the vowels are the published averages "
            "of a classic 1952 study), Speed and Loop play it, Seq Mix blends it with the operators as set. Shift moves "
            "every formant, Track lets them follow the key, Width scales every skirt. Ensemble sings each note as one, two "
            "or three voices; Vibrato with its delay; Sweep moves one formant slowly (overtone singing). Drag the "
            "formants on the card (across: frequency, up and down: level, the wings: width); Presets on the card. "
            "MPE: each channel bends its own note.";
        d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("out", "Audio Out") };
        for (int k = 0; k < kOps; ++k)
        {
            d.params.push_back (makeRotary (kOpIds[k][0], "V" + juce::String (k + 1) + " Freq", 50.0f, 12000.0f, kVoicedF[k], 9, "Hz", true).hide());
            d.params.push_back (makeRotary (kOpIds[k][1], "V" + juce::String (k + 1) + " Level", -60.0f, 0.0f, kVoicedL[k], 9, "dB").hide());
            d.params.push_back (makeRotary (kOpIds[k][2], "V" + juce::String (k + 1) + " Width", 10.0f, 3000.0f, kVoicedS[k], 9, "Hz", true).hide());
        }
        for (int k = 0; k < kOps; ++k)
        {
            d.params.push_back (makeRotary (kNoiseIds[k][0], "U" + juce::String (k + 1) + " Freq", 200.0f, 16000.0f, kNoiseF[k], 9, "Hz", true).hide());
            d.params.push_back (makeRotary (kNoiseIds[k][1], "U" + juce::String (k + 1) + " Level", -60.0f, 0.0f, kNoiseL[k], 9, "dB").hide());
            d.params.push_back (makeRotary (kNoiseIds[k][2], "U" + juce::String (k + 1) + " Width", 50.0f, 8000.0f, kNoiseS[k], 9, "Hz", true).hide());
        }
        d.params.push_back (makeCombo ("sequence", "Sequence", kSequenceNames, 0, 0, 3).noMod());
        d.params.push_back (makeRotary ("speed", "Speed", 10.0f, 400.0f, 100.0f, 0, "%", true));
        d.params.push_back (makeCombo ("loop", "Loop", { "Once", "Loop" }, 1, 0, 2).noMod());
        d.params.push_back (makeRotary ("seqMix", "Seq Mix", 0.0f, 100.0f, 100.0f, 0, "%"));
        d.params.push_back (makeRotary ("shift", "Shift", -24.0f, 24.0f, 0.0f, 1, "st", false, 0.1f));
        d.params.push_back (makeRotary ("track", "Track", 0.0f, 100.0f, 10.0f, 1, "%"));
        d.params.push_back (makeRotary ("width", "Width", 25.0f, 400.0f, 100.0f, 1, "%", true));
        d.params.push_back (makeRotary ("breath", "Breath", 0.0f, 100.0f, 4.0f, 1, "%"));
        d.params.push_back (makeRotary ("noiseLevel", "Noise", -24.0f, 12.0f, 0.0f, 1, "dB"));
        d.params.push_back (makeCombo ("ensemble", "Ensemble", { "Solo", "Duo", "Choir" }, 0, 2, 2).noMod());
        d.params.push_back (makeRotary ("spread", "Spread", 0.0f, 40.0f, 9.0f, 2, "ct"));
        d.params.push_back (makeRotary ("vibrato", "Vibrato", 0.0f, 100.0f, 12.0f, 2, "ct"));
        d.params.push_back (makeRotary ("vibRate", "Vib Rate", 2.0f, 9.0f, 5.3f, 2, "Hz"));
        d.params.push_back (makeRotary ("vibDelay", "Vib Delay", 0.0f, 2.0f, 0.3f, 2, "s"));
        d.params.push_back (makeRotary ("sweep", "Sweep", 0.0f, 24.0f, 0.0f, 3, "st"));
        d.params.push_back (makeRotary ("sweepRate", "Sweep Rate", 0.02f, 8.0f, 0.25f, 3, "Hz", true));
        d.params.push_back (makeCombo ("sweepOp", "Sweep On", { "All", "V1", "V2", "V3", "V4" }, 0, 3, 2).noMod());
        d.params.push_back (makeRotary ("voices", "Voices", 1.0f, (float) kVoices, 12.0f, 3, {}, false, 1.0f).noMod());
        d.params.push_back (makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 3, "ms"));
        d.params.push_back (makeRotary ("attack", "Attack", 1.0f, 5000.0f, 60.0f, 4, "ms", true));
        d.params.push_back (makeRotary ("decay", "Decay", 5.0f, 8000.0f, 600.0f, 4, "ms", true));
        d.params.push_back (makeRotary ("sustain", "Sustain", 0.0f, 100.0f, 85.0f, 4, "%"));
        d.params.push_back (makeRotary ("release", "Release", 5.0f, 8000.0f, 400.0f, 4, "ms", true));
        d.params.push_back (makeRotary ("velocity", "Velocity", 0.0f, 100.0f, 50.0f, 4, "%"));
        d.params.push_back (makeRotary ("level", "Level", 0.0f, 1.0f, 0.7f, 4));
        d.params.push_back (makeOutputTrim (0.0f));
        return d;
    }

    // a preset from the card's menu (message thread)
    void applyPreset (int index)
    {
        const auto& all = presets();
        if (index < 0 || index >= (int) all.size()) return;
        for (const auto& spec : descriptor().params)
            if (! spec.calibration) setParameter (spec.id, spec.defaultValue);
        for (const auto& pv : all[(size_t) index].values) setParameter (pv.id, pv.v);
    }

    // what the card shows: the newest sounding voice's formants right now
    struct Snapshot { float vf[kOps], vdb[kOps], vs[kOps], uf[kOps], udb[kOps], us[kOps]; bool live { false }; };
    Snapshot snapshot() const
    {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        return snap;
    }

private:
    float p (const char* id) const { const int i = paramIndex (id); return i >= 0 ? param (i) : 0.0f; }

    struct Sub { double phase { 0.0 }; float ratio { 1.0f }, pan { 0.0f }; };
    struct Voice
    {
        bool active { false }, held { false }, sustained { false };
        int note { -1 }, channel { 1 };
        float vel { 0.8f };
        juce::uint32 age { 0 };
        int stage { 0 };       // 0 attack, 1 decay, 2 sustain, 3 release
        float env { 0.0f };
        float t { 0.0f };      // seconds since the note began (the vibrato's delay)
        float seqT { 0.0f };   // the sequence's own time (at its Speed)
        float pitch { 60.0f }, pitchTarget { 60.0f };
        Sub sub[3];
        // per operator, this control step: carrier harmonic, cross-fade gains, index
        int h[kOps] {};
        float g1[kOps] {}, g2[kOps] {}, idx[kOps] {};
        float gainNow[kOps] {}, gainStep[kOps] {};
        Bp nz[kOps], asp[kSeqVoiced];
        float nzGain[kOps] {}, aspGain[kSeqVoiced] {};
        juce::uint32 seed { 1 };
        float jitter { 0.0f }, jitterT { 0.0f };
        float f0 { 220.0f };
    };

    void noteOn (int note, float vel, int ch)
    {
        if (vel <= 0.0f) { noteOff (note, ch); return; }
        if (poly == 1)
        {
            // mono: the voice glides to the new key (legato keeps the envelope and the sequence)
            for (auto& v : voices)
                if (v.active && ! (v.stage == 3))
                {
                    v.note = note; v.channel = ch; v.held = true; v.vel = vel;
                    v.pitchTarget = (float) note;
                    if (glide <= 0.0f) v.pitch = (float) note;
                    return;
                }
        }
        Voice* pick = nullptr;
        int activeCount = 0;
        for (auto& v : voices) if (v.active) ++activeCount;
        for (auto& v : voices) if (! v.active) { pick = &v; break; }
        if (pick == nullptr || activeCount >= poly)
        {
            // the oldest (released first)
            for (auto& v : voices)
                if (v.active && (pick == nullptr || ! pick->active || (v.stage == 3) > (pick->stage == 3) || ((v.stage == 3) == (pick->stage == 3) && v.age < pick->age)))
                    pick = &v;
        }
        auto& v = *pick;
        const bool wasActive = v.active;
        v.active = true; v.held = true; v.sustained = false;
        v.note = note; v.channel = ch; v.vel = vel; v.age = ++ageCounter;
        v.stage = 0; if (! wasActive) v.env = 0.0f;
        v.t = 0.0f; v.seqT = 0.0f;
        v.pitch = v.pitchTarget = (float) note;
        v.seed = 0x9e3779b9u * (v.age + 7u);
        for (int s = 0; s < 3; ++s)
        {
            v.sub[s].phase = rand01 (v.seed);
            const float c = s == 0 ? 0.0f : (s == 1 ? -1.0f : 1.0f);
            v.sub[s].ratio = std::pow (2.0f, c * spread / 1200.0f);
            v.sub[s].pan = ensemble == 1 ? 0.0f : c * 0.6f;
        }
        for (auto& b : v.nz) b.reset();
        for (auto& b : v.asp) b.reset();
        for (auto& g : v.gainNow) g = 0.0f;
    }

    void noteOff (int note, int ch)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == note && (v.channel == ch || ch == 1 || v.channel == 1))
            {
                v.held = false;
                if (sustainPedal) v.sustained = true;
                else v.stage = 3;
            }
    }
    void releaseSustained() { for (auto& v : voices) if (v.active && v.sustained) { v.sustained = false; v.stage = 3; } }
    void allOff() { for (auto& v : voices) { v.active = false; } }

    static float rand01 (juce::uint32& s) { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 16777216.0f); }
    static float randSym (juce::uint32& s) { return rand01 (s) * 2.0f - 1.0f; }

    // every 16 samples: the formants, the envelope's coefficients
    void control()
    {
        const float dt = (float) kControl / fs;
        const auto& seqs = sequences();
        Snapshot shown;
        juce::uint32 newest = 0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            v.t += dt;
            v.seqT += dt * speed;
            // the pitch: glide, bends (the master's and the note's own channel), vibrato, a little natural jitter
            if (glide > 0.0f) v.pitch += (v.pitchTarget - v.pitch) * juce::jmin (1.0f, dt / glide * 3.0f);
            else v.pitch = v.pitchTarget;
            const float vibOn = juce::jlimit (0.0f, 1.0f, (v.t - vibDelay) / 0.4f);
            const float vib = (vibCents * vibOn + 40.0f * modWheel) * 0.01f * sin01 (v.t * vibRate);
            v.jitterT -= dt;
            if (v.jitterT <= 0.0f) { v.jitterT = 0.05f; v.jitter = 0.7f * v.jitter + 0.3f * randSym (v.seed) * 0.06f; }
            const float semis = v.pitch + masterBend + chanBend[v.channel] + vib + v.jitter;
            v.f0 = (float) midiNoteToHz ((double) semis);

            // the formants this moment: as set, blended with the sequence
            float f[kOps], db[kOps], w[kOps], uf[kOps], udb[kOps], uw[kOps];
            for (int k = 0; k < kOps; ++k) { f[k] = opF[k]; db[k] = opDb[k]; w[k] = opS[k]; uf[k] = nzF[k]; udb[k] = nzDb[k]; uw[k] = nzS[k]; }
            float voicedAmp = 1.0f, seqNoise = 0.0f;
            if (seqIndex > 0)
            {
                const auto fr = frameAt (seqs[(size_t) seqIndex], v.seqT, loop);
                for (int k = 0; k < kSeqVoiced; ++k)
                {
                    f[k] = std::exp (std::log (f[k]) + seqMix * (std::log (fr.vf[k]) - std::log (f[k])));
                    db[k] = db[k] + seqMix * (fr.vdb[k] - db[k]);
                    w[k] = w[k] + seqMix * (fr.vb[k] - w[k]);
                }
                for (int k = 0; k < kSeqNoise; ++k)
                {
                    uf[k] = std::exp (std::log (uf[k]) + seqMix * (std::log (fr.uf[k]) - std::log (uf[k])));
                    udb[k] = udb[k] + seqMix * (fr.udb[k] - udb[k]);
                    uw[k] = uw[k] + seqMix * (fr.ub[k] - uw[k]);
                }
                voicedAmp = 1.0f + seqMix * (fr.voiced - 1.0f);
                seqNoise = seqMix * fr.noise;
            }
            // Shift, Track, the Sweep
            const float trackRatio = std::pow (v.f0 / 220.0f, track);
            const float sw = sweep > 0.0f ? std::pow (2.0f, sweep / 12.0f * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * sweepPhase))) : 1.0f;
            for (int k = 0; k < kOps; ++k)
            {
                float ratio = shift * trackRatio;
                if (sweep > 0.0f && (sweepOp == 0 || sweepOp == k + 1)) ratio *= sw;
                f[k] *= ratio;
                uf[k] *= shift;
            }
            // the envelope's level at the end of this step
            stepEnvelope (v, dt);
            if (! v.active) continue;
            const float velGain = 1.0f - velAmt * (1.0f - v.vel);
            const float amp = v.env * velGain;

            // the voiced operators: carrier harmonic, cross-fade, index
            for (int k = 0; k < kOps; ++k)
            {
                const float a = db[k] <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db[k]) * voicedAmp;
                const float n = juce::jmax (1.0f, f[k] / v.f0);
                int h = (int) std::floor (n);
                float frac = n - (float) h;
                if (h < 1) { h = 1; frac = 0.0f; }
                float index = juce::jlimit (0.0f, 6.0f, 0.5f * w[k] * width / v.f0);
                // keep the sidebands below Nyquist
                const float top = 0.45f * fs / v.f0;
                if ((float) h > top) { v.g1[k] = v.g2[k] = 0.0f; v.gainStep[k] = -v.gainNow[k] / (float) kControl; continue; }
                index = juce::jmin (index, juce::jmax (0.0f, top - (float) h - 2.0f));
                v.h[k] = h;
                v.g1[k] = std::cos (frac * juce::MathConstants<float>::halfPi);
                v.g2[k] = (float) (h + 1) < top ? std::sin (frac * juce::MathConstants<float>::halfPi) : 0.0f;
                v.idx[k] = index;
                v.gainStep[k] = (a * amp - v.gainNow[k]) / (float) kControl;
            }
            // the unvoiced operators (and the sequence's own noise on the first four)
            float ua[kOps];
            for (int k = 0; k < kOps; ++k)
            {
                float a = udb[k] <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (udb[k]);
                if (k < kSeqNoise && seqIndex > 0) a = juce::jmax (a * (1.0f - seqMix), juce::Decibels::decibelsToGain (udb[k]) * seqNoise);
                ua[k] = a;
                v.nz[k].set (uf[k], juce::jmax (30.0f, uw[k] * width), fs);
                v.nzGain[k] = a * noiseLevel * amp * std::sqrt (1000.0f / juce::jmax (50.0f, uw[k] * width)) * 0.35f;
            }
            // Breath: noise through the voiced formants
            for (int k = 0; k < kSeqVoiced; ++k)
            {
                const float a = db[k] <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db[k]);
                v.asp[k].set (f[k], juce::jmax (60.0f, w[k] * width * 1.6f), fs);
                v.aspGain[k] = breath * a * amp * 0.5f;
            }
            if (v.age > newest)
            {
                newest = v.age;
                for (int k = 0; k < kOps; ++k)
                {
                    shown.vf[k] = f[k]; shown.vdb[k] = db[k] + juce::Decibels::gainToDecibels (juce::jmax (1.0e-3f, voicedAmp)); shown.vs[k] = w[k] * width;
                    shown.uf[k] = uf[k]; shown.us[k] = uw[k] * width;
                    shown.udb[k] = ua[k] > 0.0f ? juce::Decibels::gainToDecibels (ua[k]) : -60.0f;
                }
                shown.live = true;
            }
        }
        sweepPhase += dt * sweepRate;
        if (sweepPhase >= 1.0f) sweepPhase -= 1.0f;
        const juce::SpinLock::ScopedTryLockType sl (snapLock);
        if (sl.isLocked()) snap = shown;
    }

    void stepEnvelope (Voice& v, float dt)
    {
        switch (v.stage)
        {
            case 0: v.env += dt / juce::jmax (0.001f, attack); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } break;
            case 1: v.env = sustain + (v.env - sustain) * std::exp (-dt / juce::jmax (0.001f, decay * 0.3f)); if (std::abs (v.env - sustain) < 1.0e-3f) v.stage = 2; break;
            case 2: v.env = sustain; if (sustain <= 0.0f) v.stage = 3; break;
            default: v.env *= std::exp (-dt / juce::jmax (0.001f, release * 0.3f)); if (v.env < 1.0e-4f) v.active = false; break;
        }
    }

    void renderVoice (Voice& v, float& l, float& r)
    {
        // the voiced operators, per ensemble voice
        float s[3] = { 0.0f, 0.0f, 0.0f };
        for (int k = 0; k < kOps; ++k) v.gainNow[k] += v.gainStep[k];
        for (int e = 0; e < ensemble; ++e)
        {
            auto& sb = v.sub[e];
            sb.phase += (double) (v.f0 * sb.ratio / fs);
            if (sb.phase >= 1.0) sb.phase -= 1.0;
            const float m = sin01 (sb.phase);
            float acc = 0.0f;
            for (int k = 0; k < kOps; ++k)
            {
                const float g = v.gainNow[k];
                if (g < 1.0e-5f) continue;
                const double hp = sb.phase * v.h[k];
                const double pm = (double) (v.idx[k] * m) / juce::MathConstants<double>::twoPi;
                float y = v.g1[k] * sin01 (hp + pm);
                if (v.g2[k] > 1.0e-4f) y += v.g2[k] * sin01 (hp + sb.phase + pm);
                acc += g * y;
            }
            s[e] = acc;
        }
        // noise: the unvoiced operators and the breath
        const float n = randSym (v.seed);
        float nz = 0.0f;
        for (int k = 0; k < kOps; ++k)
            if (v.nzGain[k] > 1.0e-6f) nz += v.nzGain[k] * v.nz[k].process (n);
        for (int k = 0; k < kSeqVoiced; ++k)
            if (v.aspGain[k] > 1.0e-6f) nz += v.aspGain[k] * v.asp[k].process (n);

        const float norm = ensemble == 1 ? 0.32f : (ensemble == 2 ? 0.24f : 0.2f);
        for (int e = 0; e < ensemble; ++e)
        {
            const float p = v.sub[e].pan;
            l += s[e] * norm * (1.0f - juce::jmax (0.0f, p));
            r += s[e] * norm * (1.0f + juce::jmin (0.0f, p));
        }
        l += nz * 0.5f; r += nz * 0.5f;
    }

    float fs { 48000.0f };
    std::array<Voice, kVoices> voices;
    int pV[kOps][3] {}, pU[kOps][3] {};
    float opF[kOps] {}, opDb[kOps] {}, opS[kOps] {}, nzF[kOps] {}, nzDb[kOps] {}, nzS[kOps] {};
    int seqIndex { 0 }, ensemble { 1 }, poly { 12 }, sweepOp { 0 };
    bool loop { true }, sustainPedal { false };
    float speed { 1 }, seqMix { 1 }, shift { 1 }, track { 0.1f }, width { 1 }, breath { 0.04f }, noiseLevel { 1 };
    float spread { 9 }, vibCents { 12 }, vibRate { 5.3f }, vibDelay { 0.3f }, sweep { 0 }, sweepRate { 0.25f }, sweepPhase { 0 };
    float attack { 0.06f }, decay { 0.6f }, sustain { 0.85f }, release { 0.4f }, velAmt { 0.5f }, glide { 0 }, level { 0.7f };
    float chanBend[17] {};
    float masterBend { 0 }, modWheel { 0 };
    juce::uint32 ageCounter { 0 }, counter { 0 };
    mutable juce::SpinLock snapLock;
    Snapshot snap;
};

//==============================================================================
// The card: the formants as a spectrum, each a handle to drag
//==============================================================================
namespace
{
    class FormantView final : public juce::Component, public aquanode::ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit FormantView (FormantFMModule& m) : module (&m)
        {
            for (auto* b : { &presetsButton, &layerButton })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff20303c));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
            }
            presetsButton.setTooltip ("Presets: voices, choirs, talking and whispering sounds");
            layerButton.setTooltip ("Which formants you drag: the voiced ones (V) or the noise (U)");
            presetsButton.onClick = [this]
            {
                juce::PopupMenu menu;
                menu.addSectionHeader ("Formant FM presets");
                int id = 1;
                for (const auto& p : presets()) menu.addItem (id++, p.name);
                juce::Component::SafePointer<FormantView> safe (this);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton), [safe] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    if (auto* fm = safe->fm()) fm->applyPreset (r - 1);
                    if (safe->paramsChangedByContent) safe->paramsChangedByContent();
                    safe->repaint();
                });
            };
            layerButton.onClick = [this] { noiseLayer = ! noiseLayer; selected = -1; layerButton.setButtonText (noiseLayer ? "Edit: U" : "Edit: V"); repaint(); };
            startTimerHz (24);
        }
        FormantFMModule* fm() const { return dynamic_cast<FormantFMModule*> (module.get()); }

        void resized() override
        {
            presetsButton.setBounds (getWidth() - 80, 4, 76, 20);
            layerButton.setBounds (getWidth() - 152, 4, 68, 20);
        }

        juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedTop (28.0f).reduced (6.0f, 4.0f).withTrimmedBottom (12.0f); }
        float xOf (float hz) const { const auto p = plot(); return p.getX() + p.getWidth() * (float) (std::log (hz / 50.0) / std::log (12000.0 / 50.0)); }
        float hzOf (float x) const { const auto p = plot(); return 50.0f * std::pow (240.0f, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth())); }
        float yOf (float db) const { const auto p = plot(); return p.getY() + p.getHeight() * (-db / 60.0f); }
        float dbOf (float y) const { const auto p = plot(); return juce::jlimit (-60.0f, 0.0f, -(y - p.getY()) / p.getHeight() * 60.0f); }

        void paint (juce::Graphics& g) override
        {
            auto* m = fm();
            if (m == nullptr) return;
            const auto look = LookPalette::current (juce::Colour (0xff8fd8ff), ThemeColours::Oscillator);
            look.fillBackground (g, getLocalBounds().toFloat());
            const auto p = plot();
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (p.expanded (3.0f, 2.0f), 4.0f);
            // the grid
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float hz : { 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                const float x = xOf (hz);
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz), juce::Rectangle<float> (x - 14.0f, p.getBottom() + 1.0f, 28.0f, 10.0f), juce::Justification::centred, false);
            }
            for (float db : { -12.0f, -24.0f, -36.0f, -48.0f })
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawHorizontalLine ((int) yOf (db), p.getX(), p.getRight());
            }
            // the envelopes: voiced (as set), unvoiced, and live (what the newest voice sings now)
            auto curve = [&] (const float* f, const float* db, const float* w, juce::Colour c, bool fill, float thick)
            {
                juce::Path path;
                const int n = (int) p.getWidth();
                for (int i = 0; i <= n; ++i)
                {
                    const float x = p.getX() + (float) i;
                    const float hz = hzOf (x);
                    float amp = 0.0f;
                    for (int k = 0; k < kOps; ++k)
                    {
                        if (db[k] <= -59.5f) continue;
                        const float d = (hz - f[k]) / juce::jmax (10.0f, w[k] * 0.5f);
                        amp += juce::Decibels::decibelsToGain (db[k]) / (1.0f + d * d);
                    }
                    const float y = yOf (juce::jmax (-60.0f, juce::Decibels::gainToDecibels (amp, -60.0f)));
                    if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                }
                if (fill)
                {
                    juce::Path f2 = path;
                    f2.lineTo (p.getRight(), p.getBottom()); f2.lineTo (p.getX(), p.getBottom()); f2.closeSubPath();
                    g.setColour (c.withAlpha (0.16f)); g.fillPath (f2);
                }
                g.setColour (c); g.strokePath (path, juce::PathStrokeType (thick));
            };
            float vf[kOps], vdb[kOps], vs[kOps], uf[kOps], udb[kOps], us[kOps];
            for (int k = 0; k < kOps; ++k)
            {
                vf[k] = m->getParameter (kOpIds[k][0]); vdb[k] = m->getParameter (kOpIds[k][1]); vs[k] = m->getParameter (kOpIds[k][2]);
                uf[k] = m->getParameter (kNoiseIds[k][0]); udb[k] = m->getParameter (kNoiseIds[k][1]); us[k] = m->getParameter (kNoiseIds[k][2]);
            }
            const auto voicedColour = juce::Colour (0xff8fd8ff), noiseColour = juce::Colour (0xffffc27a);
            curve (uf, udb, us, noiseColour.withAlpha (noiseLayer ? 0.95f : 0.5f), true, noiseLayer ? 1.6f : 1.0f);
            curve (vf, vdb, vs, voicedColour.withAlpha (noiseLayer ? 0.5f : 0.95f), true, noiseLayer ? 1.0f : 1.8f);
            if (live.live)
            {
                curve (live.vf, live.vdb, live.vs, juce::Colours::white.withAlpha (0.55f), false, 1.0f);
                curve (live.uf, live.udb, live.us, noiseColour.brighter (0.4f).withAlpha (0.45f), false, 0.8f);
            }
            // the handles
            const float* hf = noiseLayer ? uf : vf;
            const float* hd = noiseLayer ? udb : vdb;
            const float* hs = noiseLayer ? us : vs;
            const auto hc = noiseLayer ? noiseColour : voicedColour;
            for (int k = 0; k < kOps; ++k)
            {
                const auto c = handle (hf[k], hd[k]);
                const bool off = hd[k] <= -59.5f;
                if (k == selected)
                {
                    // the wings: the width
                    const float x0 = xOf (juce::jmax (50.0f, hf[k] - hs[k] * 0.5f)), x1 = xOf (juce::jmin (12000.0f, hf[k] + hs[k] * 0.5f));
                    g.setColour (hc.withAlpha (0.8f));
                    g.drawLine (x0, c.y, x1, c.y, 1.5f);
                    g.fillEllipse (x0 - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
                    g.fillEllipse (x1 - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
                }
                g.setColour (off ? juce::Colours::black.withAlpha (0.5f) : hc.darker (0.6f));
                g.fillEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
                g.setColour (k == selected ? juce::Colours::white : hc.withAlpha (off ? 0.4f : 0.95f));
                g.drawEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, k == selected ? 2.0f : 1.2f);
                g.setFont (juce::Font (juce::FontOptions (9.0f)));
                g.drawText (juce::String (k + 1), juce::Rectangle<float> (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f), juce::Justification::centred, false);
            }
            // the readout
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            juce::String text = noiseLayer ? "Unvoiced (noise) formants" : "Voiced formants";
            if (selected >= 0)
                text = juce::String (noiseLayer ? "U" : "V") + juce::String (selected + 1) + "   " + juce::String ((int) hf[selected]) + " Hz   "
                     + (hd[selected] <= -59.5f ? juce::String ("off") : juce::String (hd[selected], 1) + " dB") + "   width " + juce::String ((int) hs[selected]) + " Hz";
            g.drawText (text, juce::Rectangle<int> (8, 4, getWidth() - 164, 20), juce::Justification::centredLeft, true);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragWing = 0;
            auto* m = fm();
            if (m == nullptr) return;
            const auto& ids = noiseLayer ? kNoiseIds : kOpIds;
            // a wing of the selected formant?
            if (selected >= 0)
            {
                const float f = m->getParameter (ids[selected][0]), s = m->getParameter (ids[selected][2]);
                const float y = handle (f, m->getParameter (ids[selected][1])).y;
                for (int side : { -1, 1 })
                {
                    const float x = xOf (juce::jlimit (50.0f, 12000.0f, f + side * s * 0.5f));
                    if (e.position.getDistanceFrom ({ x, y }) < 9.0f) { dragWing = side; return; }
                }
            }
            selected = -1;
            float best = 14.0f;
            for (int k = 0; k < kOps; ++k)
            {
                const float d = e.position.getDistanceFrom (handle (m->getParameter (ids[k][0]), m->getParameter (ids[k][1])));
                if (d < best) { best = d; selected = k; }
            }
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* m = fm();
            if (m == nullptr || selected < 0) return;
            const auto& ids = noiseLayer ? kNoiseIds : kOpIds;
            if (dragWing != 0)
            {
                const float f = m->getParameter (ids[selected][0]);
                const float edge = hzOf (e.position.x);
                const float w = juce::jlimit (noiseLayer ? 50.0f : 10.0f, noiseLayer ? 8000.0f : 3000.0f, 2.0f * std::abs (edge - f));
                m->setParameter (ids[selected][2], w);
            }
            else
            {
                m->setParameter (ids[selected][0], juce::jlimit (noiseLayer ? 200.0f : 50.0f, noiseLayer ? 16000.0f : 12000.0f, hzOf (e.position.x)));
                m->setParameter (ids[selected][1], dbOf (e.position.y));
            }
            if (paramValuesChangedByContent) paramValuesChangedByContent();
            repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            // twice: a formant off, or back on
            auto* m = fm();
            if (m == nullptr || selected < 0) return;
            const auto& ids = noiseLayer ? kNoiseIds : kOpIds;
            m->setParameter (ids[selected][1], m->getParameter (ids[selected][1]) <= -59.5f ? -12.0f : -60.0f);
            if (paramValuesChangedByContent) paramValuesChangedByContent();
            repaint();
        }

    private:
        juce::Point<float> handle (float hz, float db) const { return { xOf (juce::jlimit (50.0f, 12000.0f, hz)), yOf (juce::jmax (-58.0f, db)) }; }
        void timerCallback() override
        {
            if (auto* m = fm())
            {
                const auto s = m->snapshot();
                if (s.live || live.live) { live = s; repaint(); }
            }
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton presetsButton { "Presets" }, layerButton { "Edit: V" };
        bool noiseLayer { false };
        int selected { -1 }, dragWing { 0 };
        FormantFMModule::Snapshot live;
    };
}

std::unique_ptr<juce::Component> FormantFMModule::createExtraContentComponent()
{
    return std::make_unique<FormantView> (*this);
}

AQUANODE_REGISTER_MODULE (FormantFMModule, FormantFMModule::descriptor)

// for tests
void formantFmApplyPreset (SynthModule* m, int index)
{
    if (auto* f = dynamic_cast<FormantFMModule*> (m)) f->applyPreset (index);
}
int formantFmPresetCount() { return (int) formantfm::presets().size(); }
