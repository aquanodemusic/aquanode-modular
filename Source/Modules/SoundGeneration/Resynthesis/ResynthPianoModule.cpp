//==============================================================================
// Resynth Piano - a grand piano rebuilt from recordings of it, without the
// recordings: tools/assets/piano_analyse.py took each sampled note apart into what
// makes it (its fundamental and how far its partials stretch above the
// harmonic series - the strings' stiffness, every partial's level over 48
// moments from the strike to 30 seconds on, their starting phases, the first
// 60 ms of what the partials do not explain - the hammer - and the rest of
// the noise around the partials in 16 bands: the thump, the body, the strings
// ringing along), about 300 KB for 33 recordings that took 29 MB.
//
// A key plays its own note from the sampled notes around it: its partials'
// levels are blended from the neighbours below and above (in dB, partial by
// partial), its tuning follows theirs (the stretched octaves of a real
// piano), and the two dynamics recorded (soft and loud) are blended by
// velocity - a softer touch darker as well as quieter. Played as a bank of up
// to 64 sine partials per note at their own, slightly inharmonic frequencies,
// plus the hammer and the noise bands.
//
// Knobs: Volume, Voices; Velocity (how much the touch changes it), Bright
// (tilts the partials), Decay (how long the notes ring), Release (how fast the
// dampers stop a key let go - the top octave and a half has none, as on a
// grand), Hammer (the strike), Body (the knock of the strike: noise around the partials, gone within a few hundred ms), Stretch
// (the inharmonicity, 0 = harmonic), Beat (strings slightly apart: the slow
// beating of a real piano's unisons), Width (low notes left, high right), Tune,
// Partials (how many per note: fewer for less CPU). FM In bends the pitch
// (1 = 60 semitones, as on the Piano), the sustain pedal holds the notes.
//==============================================================================
#include "../../Core/ModuleCore.h"

namespace aquanode::pianodata
{
    extern const int modelSize;
    extern const unsigned char model[];
}

namespace
{
    using namespace aquanode;

    constexpr int kNT = 48, kKMax = 64, kNB = 16, kBlock = 32;

    //=== the model, read once from the embedded data ==========================
    struct Entry
    {
        int note = 60, layer = 5, K = 0;
        float f0 = 261.6f, B = 0.0f, cents = 0.0f, attScale = 1.0f;
        float env[kKMax][kNT] {};      // dB
        float phase[kKMax] {};         // radians
        float noise[kNB][kNT] {};      // dB of power
        std::vector<float> attack;     // 44.1 kHz
    };

    struct Model
    {
        int att = 0;
        float times[kNT] {};
        float edges[kNB + 1] {};
        std::vector<Entry> entries;
        std::vector<int> soft, loud;   // indices, by note
        bool ok = false;
    };

    const Model& model()
    {
        static const Model m = []
        {
            Model md;
            const auto* p = pianodata::model;
            const int size = pianodata::modelSize;
            int at = 0;
            auto u32 = [&] { std::uint32_t v; std::memcpy (&v, p + at, 4); at += 4; return (int) v; };
            auto f32 = [&] { float v; std::memcpy (&v, p + at, 4); at += 4; return v; };
            if (size < 32 || std::memcmp (p, "AQPN", 4) != 0)
                return md;
            at = 4;
            const int version = u32(), num = u32(), nt = u32(), kmax = u32(), att = u32(), nb = u32();
            if (version != 2 || nt != kNT || kmax != kKMax || nb != kNB)
                return md;
            md.att = att;
            for (int i = 0; i < kNT; ++i) md.times[i] = f32();
            for (int i = 0; i <= kNB; ++i) md.edges[i] = f32();
            for (int e = 0; e < num && at < size; ++e)
            {
                Entry en;
                en.note = p[at]; en.layer = p[at + 1]; en.K = p[at + 2];
                at += 4;
                en.f0 = f32(); en.B = f32(); en.attScale = f32(); (void) f32();
                en.cents = 1200.0f * std::log2 (en.f0 / (440.0f * std::pow (2.0f, (float) (en.note - 69) / 12.0f)));
                for (int k = 0; k < kKMax; ++k)
                    for (int t = 0; t < kNT; ++t)
                        en.env[k][t] = (float) p[at++] * 0.5f - 120.0f;
                for (int k = 0; k < kKMax; ++k)
                    en.phase[k] = (float) p[at++] / 255.0f * juce::MathConstants<float>::twoPi;
                en.attack.resize ((size_t) att);
                for (int i = 0; i < att; ++i)
                {
                    std::int16_t s;
                    std::memcpy (&s, p + at, 2);
                    at += 2;
                    en.attack[(size_t) i] = (float) s / 32767.0f * en.attScale;
                }
                for (int b = 0; b < kNB; ++b)
                    for (int t = 0; t < kNT; ++t)
                        en.noise[b][t] = (float) p[at++] * 0.5f - 160.0f;
                md.entries.push_back (std::move (en));
            }
            for (int i = 0; i < (int) md.entries.size(); ++i)
                (md.entries[(size_t) i].layer >= 4 ? md.loud : md.soft).push_back (i);
            auto byNote = [&md] (int a, int b) { return md.entries[(size_t) a].note < md.entries[(size_t) b].note; };
            std::sort (md.soft.begin(), md.soft.end(), byNote);
            std::sort (md.loud.begin(), md.loud.end(), byNote);
            md.ok = ! md.loud.empty() && ! md.soft.empty();
            return md;
        }();
        return m;
    }

    // the recorded notes around a key, in one dynamic: the one below, the one above, how far between
    void around (const Model& m, const std::vector<int>& layer, float key, int& a, int& b, float& w)
    {
        a = b = layer.front();
        w = 0.0f;
        for (size_t i = 0; i < layer.size(); ++i)
        {
            const auto& e = m.entries[(size_t) layer[i]];
            if ((float) e.note <= key) a = layer[i];
            if ((float) e.note >= key) { b = layer[i]; break; }
            b = layer[i];
        }
        const float na = (float) m.entries[(size_t) a].note, nb = (float) m.entries[(size_t) b].note;
        w = nb > na ? juce::jlimit (0.0f, 1.0f, (key - na) / (nb - na)) : 0.0f;
    }
}

class ResynthPianoModule final : public SynthModule
{
public:
    enum P { pVolume, pVoices, pVelocity, pBright, pDecay, pRelease, pHammer, pBody, pStretch, pBeat, pWidth, pTune, pPartials };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.resynthpiano";
        d.displayName = "Resynth Piano";
        d.description = "A grand piano rebuilt from recordings without the recordings: every sampled note taken apart "
                        "into its partials (their stretched tuning and how each one fades), the hammer and the noise "
                        "around them - 300 KB where the samples took 29 MB. A key blends the recorded notes around it and "
                        "the soft and loud recordings by velocity. Bright, Decay, Hammer, Body, Stretch (inharmonicity), "
                        "Beat (the unisons' slow beating), Width, Release (the dampers; the top keys have none).";
        d.section = ModuleSection::Oscillator;
        d.sockets = {
            modIn ("fmIn", "FM In"),
            midiIn ("addMidiIn", "Add Midi In"),
            audioOut ("audioOut", "Audio Out")
        };
        d.params = {
            makeRotary ("volume", "Volume", 0.0f, 2.0f, 1.0f, 0),
            makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, 16.0f, 0, {}, false, 1.0f).noMod(),
            makeRotary ("velocity", "Velocity", 0.0f, 100.0f, 80.0f, 0, "%"),
            makeRotary ("bright", "Bright", -6.0f, 6.0f, 0.0f, 0, "dB"),
            makeRotary ("decay", "Decay", 0.25f, 4.0f, 1.0f, 1, "x", true),
            makeRotary ("release", "Release", 0.03f, 3.0f, 0.25f, 1, "s", true),
            makeRotary ("hammer", "Hammer", 0.0f, 2.0f, 1.0f, 1),
            makeRotary ("body", "Body", 0.0f, 2.0f, 1.0f, 1),
            makeRotary ("stretch", "Stretch", 0.0f, 2.0f, 1.0f, 2),
            makeRotary ("beat", "Beat", 0.0f, 1.0f, 0.3f, 2),
            makeRotary ("width", "Width", 0.0f, 1.0f, 0.5f, 2),
            makeRotary ("tune", "Tune", -50.0f, 50.0f, 0.0f, 2, "ct"),
            makeRotary ("partials", "Partials", 8.0f, 64.0f, 48.0f, 2, {}, false, 1.0f).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    double voiceTailSeconds() const override { return decayTailSeconds (param (pRelease) * 1000.0); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        (void) model();   // read the model now, not on the first note
        for (int v = 0; v < kMaxVoices; ++v) voiceReset (v);
    }
    void reset() override
    {
        pool.resetAll();
        for (int v = 0; v < kMaxVoices; ++v) voiceReset (v);
    }

    void voiceReset (int v) override
    {
        auto& vc = voices[(size_t) v];
        vc.active = false;
        vc.pos = kBlock;
        pool.resetVoice (v);
    }

    void voiceNoteOn (int v, int note, bool) override
    {
        pool.noteOn (v, voiceLimit());
        noteOn[v] = note;
        pendingStart[v] = true;
    }
    void voiceVelocity (int v, float velocity01) override
    {
        velocity[v] = velocity01;
        if (pendingStart[v])
        {
            start (v, noteOn[v], velocity01);
            pendingStart[v] = false;
        }
    }
    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        voices[(size_t) v].released = true;
    }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        out[0] = { 0.0f, 0.0f };
        const float gain = pool.nextGain (v, sampleRate);
        auto& vc = voices[(size_t) v];
        if (pendingStart[v])   // a note-on without its velocity yet
        {
            start (v, noteOn[v], velocity[v]);
            pendingStart[v] = false;
        }
        if (pool.isSilent (v) || ! vc.active)
            return;
        if (vc.pos >= kBlock)
        {
            render (vc, in[0][0]);
            vc.pos = 0;
        }
        const float g = gain * param (pVolume);
        out[0] = { vc.out[0][vc.pos] * g, vc.out[1][vc.pos] * g };
        ++vc.pos;
    }

private:
    struct Voice
    {
        bool active = false, released = false;
        int pos = kBlock, K = 0, note = 60;
        double t = 0.0, tRel = 0.0;
        float key = 60.0f, f0 = 261.6f, B = 0.0f;
        float env[kKMax][kNT] {};
        float noise[kNB][kNT] {};
        float bandGainPrev[kNB] {}, ic1[kNB] {}, ic2[kNB] {};
        float re[kKMax] {}, im[kKMax] {}, amp[kKMax] {}, tilt[kKMax] {}, beatRate[kKMax] {}, beatPhase[kKMax] {};
        const std::vector<float>* attackSoft = nullptr;
        const std::vector<float>* attackLoud = nullptr;
        float attackMix = 1.0f, attackGain = 1.0f;
        double attackPos = 0.0, attackStep = 1.0;
        float level = 1.0f, panL = 0.7f, panR = 0.7f;
        bool damped = true;
        std::uint32_t seed = 1;
        float out[2][kBlock] {};
    };

    void start (int v, int note, float vel01)
    {
        const auto& m = model();
        auto& vc = voices[(size_t) v];
        if (! m.ok)
        {
            vc.active = false;
            return;
        }
        const float key = (float) note;
        const float sens = param (pVelocity) * 0.01f;
        const float touch = 1.0f - sens * (1.0f - juce::jlimit (0.0f, 1.0f, vel01));

        // the recorded notes around the key, soft and loud, and how much of each
        int sa, sb, la, lb;
        float sw, lw;
        around (m, m.soft, key, sa, sb, sw);
        around (m, m.loud, key, la, lb, lw);
        const float loudness = juce::jlimit (0.0f, 1.0f, (touch - 0.5f) / 0.42f);
        const int src[4] = { sa, sb, la, lb };
        const float wt[4] = { (1.0f - loudness) * (1.0f - sw), (1.0f - loudness) * sw, loudness * (1.0f - lw), loudness * lw };

        // its tuning (the recorded notes' stretch), inharmonicity, partials
        float cents = 0.0f, logB = 0.0f, wsum = 0.0f;
        int K = 0, strongest = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto& e = m.entries[(size_t) src[i]];
            cents += wt[i] * e.cents;
            logB += wt[i] * std::log (juce::jmax (1.0e-6f, e.B));
            wsum += wt[i];
            K = juce::jmax (K, e.K);
            if (wt[i] > wt[strongest]) strongest = i;
        }
        cents /= juce::jmax (1.0e-6f, wsum);
        vc.B = std::exp (logB / juce::jmax (1.0e-6f, wsum));
        vc.key = key;
        vc.note = note;
        vc.f0 = 440.0f * std::pow (2.0f, (key - 69.0f) / 12.0f + (cents + param (pTune)) / 1200.0f);
        const float nyq = (float) sampleRate * 0.45f;
        const float stretchB = vc.B * param (pStretch);
        K = juce::jmin (K, (int) param (pPartials));
        while (K > 0 && (float) K * vc.f0 * std::sqrt (1.0f + stretchB * (float) (K * K)) > juce::jmin (nyq, 18000.0f)) --K;
        vc.K = K;

        // each partial's levels, blended in dB from the recorded notes that have it
        for (int k = 0; k < K; ++k)
        {
            float w = 0.0f;
            for (int t = 0; t < kNT; ++t) vc.env[k][t] = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                const auto& e = m.entries[(size_t) src[i]];
                if (k >= e.K || wt[i] <= 0.0f) continue;
                for (int t = 0; t < kNT; ++t) vc.env[k][t] += wt[i] * e.env[k][t];
                w += wt[i];
            }
            if (w <= 0.0f)
                for (int t = 0; t < kNT; ++t) vc.env[k][t] = -120.0f;
            else
                for (int t = 0; t < kNT; ++t) vc.env[k][t] /= w;
        }
        for (int b = 0; b < kNB; ++b)
            for (int t = 0; t < kNT; ++t)
            {
                float db = 0.0f;
                for (int i = 0; i < 4; ++i) db += wt[i] * m.entries[(size_t) src[i]].noise[b][t];
                vc.noise[b][t] = db / juce::jmax (1.0e-6f, wsum);
            }

        // a softer touch than the soft recording: quieter, and darker still
        const float below = touch < 0.5f ? (0.5f - touch) / 0.5f : 0.0f;
        vc.level = juce::Decibels::decibelsToGain (-30.0f * below);
        const float brightDb = param (pBright) - 6.0f * below;
        seedCounter = seedCounter * 1664525u + 1013904223u;
        vc.seed = seedCounter | 1u;
        for (int k = 0; k < K; ++k)
        {
            vc.tilt[k] = juce::Decibels::decibelsToGain (brightDb * std::log2 ((float) k + 1.0f));
            const float ph = m.entries[(size_t) src[strongest]].phase[k];
            vc.re[k] = std::cos (ph);
            vc.im[k] = std::sin (ph);
            vc.amp[k] = 0.0f;
            vc.beatRate[k] = 0.25f + 1.4f * rand01 (vc.seed);
            vc.beatPhase[k] = juce::MathConstants<float>::twoPi * rand01 (vc.seed);
        }
        for (int b = 0; b < kNB; ++b) { vc.ic1[b] = vc.ic2[b] = 0.0f; vc.bandGainPrev[b] = 0.0f; }

        // the hammer: the attacks of the nearest soft and loud recordings, at this key's pitch
        const int nearSoft = sw < 0.5f ? sa : sb, nearLoud = lw < 0.5f ? la : lb;
        vc.attackSoft = &m.entries[(size_t) nearSoft].attack;
        vc.attackLoud = &m.entries[(size_t) nearLoud].attack;
        vc.attackMix = loudness;
        const float fromNote = (float) m.entries[(size_t) (loudness >= 0.5f ? nearLoud : nearSoft)].note;
        vc.attackStep = std::pow (2.0, juce::jlimit (-12.0, 12.0, (double) (key - fromNote)) / 12.0) * 44100.0 / sampleRate;
        vc.attackPos = 0.0;
        vc.attackGain = vc.level;

        // where it sits: low keys left, high right
        const float pan = juce::jlimit (-1.0f, 1.0f, (key - 64.0f) / 40.0f) * param (pWidth);
        vc.panL = std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * juce::MathConstants<float>::sqrt2;
        vc.panR = std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * juce::MathConstants<float>::sqrt2;
        vc.damped = note < 89;   // a grand's top keys have no dampers
        vc.t = 0.0;
        vc.tRel = 0.0;
        vc.released = false;
        vc.active = true;
        vc.pos = kBlock;
    }

    static float rand01 (std::uint32_t& s)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s >> 8) * (1.0f / 16777216.0f);
    }

    // where time t (seconds) falls on the model's log-spaced times: a point and how far past it
    struct TimeAt { int i = 0; float f = 0.0f; };
    static TimeAt timeAt (const float* times, float t)
    {
        if (t <= times[0]) return {};
        int i = 1;
        while (i < kNT - 1 && times[i] < t) ++i;
        const float a = times[i - 1], b = times[i];
        return { i - 1, juce::jlimit (0.0f, 1.0f, (t - a) / juce::jmax (1.0e-6f, b - a)) };
    }
    static float atTime (const float* table, TimeAt at)
    {
        return at.i >= kNT - 1 ? table[kNT - 1] : table[at.i] + (table[at.i + 1] - table[at.i]) * at.f;
    }

    void render (Voice& vc, float fmIn)
    {
        const auto& m = model();
        const float sr = (float) sampleRate;
        const float dt = (float) kBlock / sr;
        std::fill (std::begin (vc.out[0]), std::end (vc.out[0]), 0.0f);
        std::fill (std::begin (vc.out[1]), std::end (vc.out[1]), 0.0f);

        // the model's time, stretched by Decay after the strike
        const float decay = param (pDecay);
        auto modelTime = [decay] (double t) { return (float) (t < 0.08 ? t : 0.08 + (t - 0.08) / decay); };
        const auto tEnd = timeAt (m.times, modelTime (vc.t + dt));

        // the dampers once the key is up (the top keys ring on)
        float release = 1.0f;
        if (vc.released)
        {
            const float tau = vc.damped ? param (pRelease) : juce::jmax (param (pRelease), 3.0f);
            release = std::exp (-(float) (vc.tRel + dt) / tau);
            vc.tRel += dt;
        }

        // the partials: a bank of rotating phasors, each ramping to its level at the block's end
        const float semis = fmIn * 60.0f + pitchBendSemitones();
        const float ratio = std::pow (2.0f, semis / 12.0f);
        const float stretchB = vc.B * param (pStretch);
        const float beat = param (pBeat);
        const float scale = vc.level * release;
        float peak = 0.0f;
        for (int k = 0; k < vc.K; ++k)
        {
            const float n = (float) (k + 1);
            const float hz = n * vc.f0 * std::sqrt (1.0f + stretchB * n * n) * ratio;
            if (hz >= sr * 0.48f) { vc.amp[k] = 0.0f; continue; }
            const float w = juce::MathConstants<float>::twoPi * hz / sr;
            const float c = std::cos (w), s = std::sin (w);
            vc.beatPhase[k] += juce::MathConstants<float>::twoPi * vc.beatRate[k] * dt;
            if (vc.beatPhase[k] > juce::MathConstants<float>::twoPi) vc.beatPhase[k] -= juce::MathConstants<float>::twoPi;
            const float beating = 1.0f + beat * 0.35f * std::sin (vc.beatPhase[k]) * juce::jmin (1.0f, n * 0.25f);
            const float target = juce::Decibels::decibelsToGain (atTime (vc.env[k], tEnd), -119.0f) * vc.tilt[k] * scale * beating;
            float a = vc.amp[k];
            const float da = (target - a) / (float) kBlock;
            float re = vc.re[k], im = vc.im[k];
            for (int i = 0; i < kBlock; ++i)
            {
                a += da;
                vc.out[0][i] += a * re;
                const float nr = re * c - im * s;
                im = re * s + im * c;
                re = nr;
            }
            const float mag = 1.0f / juce::jmax (1.0e-6f, std::sqrt (re * re + im * im));
            vc.re[k] = re * mag;
            vc.im[k] = im * mag;
            vc.amp[k] = target;
            peak = juce::jmax (peak, target);
        }

        // the noise around the partials: 16 bands of filtered noise, after the hammer.
        // Only the strike's knock is noise: what the recordings hold between the
        // partials later on is their beating and the room, not hiss, so the bands
        // die away within a few hundred ms (played on, they hissed under the tone)
        const float body = param (pBody);
        const float tb = (float) vc.t;
        const float fadeIn = juce::jlimit (0.0f, 1.0f, (tb - 0.04f) / 0.02f) * std::exp (-juce::jmax (0.0f, tb - 0.06f) / 0.12f);
        float bandGain[kNB];
        for (int b = 0; b < kNB; ++b)
        {
            const float lo = m.edges[b], hi = m.edges[b + 1];
            const float fc = std::sqrt (lo * hi);
            if (fc >= sr * 0.45f || body <= 0.0f || fadeIn < 1.0e-4f) { bandGain[b] = 0.0f; continue; }
            const float q = fc / (hi - lo);
            const float enbw = juce::MathConstants<float>::halfPi * fc / q;          // the band's noise bandwidth
            const float power = std::pow (10.0f, atTime (vc.noise[b], tEnd) * 0.1f);
            bandGain[b] = std::sqrt (power / ((1.0f / 3.0f) * enbw / (sr * 0.5f))) * body * fadeIn * scale;
            peak = juce::jmax (peak, bandGain[b] * 0.05f);
        }
        for (int b = 0; b < kNB; ++b)
        {
            if (bandGain[b] <= 0.0f && vc.bandGainPrev[b] <= 0.0f) continue;
            const float lo = m.edges[b], hi = m.edges[b + 1];
            const float fc = std::sqrt (lo * hi), q = fc / (hi - lo);
            const float gg = std::tan (juce::MathConstants<float>::pi * fc / sr), kk = 1.0f / q;
            const float a1 = 1.0f / (1.0f + gg * (gg + kk)), a2 = gg * a1, a3 = gg * a2;
            float g0 = vc.bandGainPrev[b];
            const float dg = (bandGain[b] - g0) / (float) kBlock;
            float ic1 = vc.ic1[b], ic2 = vc.ic2[b];
            for (int i = 0; i < kBlock; ++i)
            {
                const float x = rand01 (vc.seed) * 2.0f - 1.0f;
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                g0 += dg;
                vc.out[1][i] += kk * v1 * g0;   // (out[1] holds the noise until the mix below)
            }
            vc.ic1[b] = ic1;
            vc.ic2[b] = ic2;
            vc.bandGainPrev[b] = bandGain[b];
        }

        // the hammer: the recorded attack, at the key's pitch
        const float hammer = param (pHammer) * vc.attackGain * release;
        if (hammer > 0.0f && vc.attackSoft != nullptr)
        {
            const int len = (int) vc.attackSoft->size();
            for (int i = 0; i < kBlock && vc.attackPos < len - 1; ++i)
            {
                const int j = (int) vc.attackPos;
                const float f = (float) (vc.attackPos - j);
                auto at = [&] (const std::vector<float>& a) { return a[(size_t) j] + (a[(size_t) j + 1] - a[(size_t) j]) * f; };
                vc.out[1][i] += hammer * (at (*vc.attackSoft) * (1.0f - vc.attackMix) + at (*vc.attackLoud) * vc.attackMix);
                vc.attackPos += vc.attackStep;
            }
            if (vc.attackPos < len - 1) peak = 1.0f;
        }

        // to the sides
        for (int i = 0; i < kBlock; ++i)
        {
            const float s = vc.out[0][i] + vc.out[1][i];
            vc.out[0][i] = s * vc.panL;
            vc.out[1][i] = s * vc.panR;
        }
        vc.t += dt;
        // a note faded below hearing ends (the engine frees its voice)
        if (vc.t > 0.5 && peak < 1.0e-5f)
            vc.active = false;
    }

    std::array<Voice, kMaxVoices> voices;
    int noteOn[kMaxVoices] {};
    float velocity[kMaxVoices] {};
    bool pendingStart[kMaxVoices] {};
    std::uint32_t seedCounter { 12345u };
    ModuleVoicePool pool;
};

AQUANODE_REGISTER_MODULE (ResynthPianoModule, ResynthPianoModule::descriptor)
