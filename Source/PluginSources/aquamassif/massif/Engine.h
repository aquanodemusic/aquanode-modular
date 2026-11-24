#pragma once
// Massif - voices and the whole signal flow.
//
//  osc1..3, noise, feedback --(routing faders)--> bus F1, bus F2
//  bus F1 -> [ins Pre F1] -> Filter 1 -> [ins Post F1] --+--> (mix)
//  bus F2 -> [ins Pre F2] -+-> Filter 2 -> [ins Post F2] -+--> (mix)
//        ser/par crossfade |  (F1 out -> [ins Serial])
//  mix -> [ins Pre Amp] -> amp (env^2) -> pan -> voice sum
//  -> FX1 -> FX2 -> EQ -> master volume; bypass joins before FX1 / FX2 / EQ
//  feedback taps: post F1 / post F2 / pre amp / post amp (+ [ins FB loop])
#include "../../../Engine/Tuning.h"   // (Aquanode: the patch tuning, 6.9.10)
#include "Patch.h"
#include "Tables.h"
#include "Modulators.h"
#include "Filters.h"
#include "Effects.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <random>

namespace massif
{
constexpr int kMaxVoices = 64;
constexpr int kCtrl = 16;                 // control-rate block

inline float midiHz (float note) { return (float) ::aquanode::tuning::noteToHz ((double) note); }

// ---------------------------------------------------------------------------
struct OscState
{
    double phase { 0.0 };
};

// measured constants that are fitted by experiment (the command line tool can override them)
namespace tune
{
    inline float& modPhase() { static float v = 0.085f; return v; }   // phase modulation: the modulator's lag (cycles)
    inline float& vaGain() { static float v = 1.0f; return v; }       // the VA oscillators' level against a full-scale table
    inline float& modDelay() { static float v = 8.7f; return v; }     // ring modulation: the modulator's lag (samples at 44.1 kHz)
    inline float& bypGain() { static float v = 1.12f; return v; }     // bypass level against the main path
}

struct Voice
{
    bool active { false };
    int note { 60 }, uniIndex { 0 }, uniCount { 1 };
    int channel { 0 };                        // MPE: the member channel (2..16) it plays on, 0 otherwise
    float vel { 0.8f };
    float trigRand { 0.0f };
    uint64_t age { 0 };
    bool gate { false };
    float pitchNow { 60 }, pitchTarget { 60 }, glideRate { 0 };   // glideRate > 0: gliding
    float glideFrom { 60 }, glideT { 0 }, glideTime { 0 };        // the glide's start, length and time so far (s)
    float uniPitch { 0 }, uniWt { 0 }, uniPan { 0 };
    std::array<OscState, 3> osc {};
    double modPhase { 0 };
    std::array<Envelope, 4> env {};
    std::array<ModPage, 4> page {};
    IntEnv oscEnv;                            // the OSC page's internal envelope (stored source 12)
    std::array<Filter, 2> flt {};
    std::array<Insert, 2> ins {};
    float fb { 0 };                // last feedback tap
    uint32_t noiseSeed { 12345 };
    double noisePh { 0 }; float noiseA { 0 }, noiseB { 0 };
    float ampPrev { 0 };
    float vibPh { 0 };
    float fadeOut { 1 }; bool stealing { false };
    std::array<float, kNumKnobs> k {};        // modulated knob values (control rate)
};

// ---------------------------------------------------------------------------
struct Engine
{
    float sr { 44100 };
    Patch patch;
    std::array<std::shared_ptr<const Wavetable>, 3> table {};
    std::array<Voice, kMaxVoices> voices {};
    std::array<MasterFx, 2> fx {};
    MasterEq eq;
    uint64_t ageCounter { 0 };
    float bend { 0 }, aftertouch { 0 };
    // MPE (Aquanode): the channel of the next noteOn/noteOff (0: not MPE), and each member channel's own bend
    // (-1..1 of +-48 semitones) and pressure (-1: none yet, the voice reads the global aftertouch)
    int noteChannel { 0 };
    std::array<float, 17> chanBend {};
    std::array<float, 17> chanPress = [] { std::array<float, 17> a {}; a.fill (-1.0f); return a; }();
    float voiceBendSemis (const Voice& v) const { return v.channel >= 2 && v.channel <= 16 ? chanBend[(size_t) v.channel] * 48.0f : 0.0f; }
    // (6.9.12, Aquanode's default MPE map) each member channel's Slide (CC 74; -1: none sent): it moves an
    // oscillator's wavetable position, the middle of the key the knob's place, up or down a quarter -
    // for an oscillator whose position nothing in the matrix modulates
    std::array<float, 17> chanSlide = [] { std::array<float, 17> a {}; a.fill (-1.0f); return a; }();
    float voiceSlidePos (const Voice& v, int osc) const
    {
        if (v.channel < 2 || v.channel > 16 || chanSlide[(size_t) v.channel] < 0.0f) return 0.0f;
        for (const auto& sl : patch.mod[(size_t) (kOsc1Pos + 5 * osc)].slot)
            if (sl.source != msNone) return 0.0f;
        return (chanSlide[(size_t) v.channel] - 0.5f) * 2.0f * slideDepth;
    }
    float slideDepth { 0.0f };   // (6.9.15: 0 unless the MPE module says)   // how far a full Slide moves it each way (Aquanode's MPE module)
    float voicePressure (const Voice& v) const
    {
        if (v.channel >= 2 && v.channel <= 16 && chanPress[(size_t) v.channel] >= 0.0f) return chanPress[(size_t) v.channel];
        return aftertouch;
    }
    std::array<float, 8> macro {};
    std::vector<int> held;                 // held notes (mono)
    std::mt19937 rng { 1 };
    std::vector<float> busL, busR, bypL, bypR;
    float globalVibPh { 0 };

    void prepare (float rate, int maxBlock)
    {
        sr = rate;
        for (auto& v : voices)
        {
            for (auto& f : v.flt) f.prepare (rate);
            for (auto& i : v.ins) i.prepare (rate);
            for (auto& e : v.env) e.sr = rate;
            for (auto& p : v.page) p.sr = rate;
            v.active = false;
        }
        // Without "Restart via Gate" the original's oscillators run on freely: a note finds them anywhere in their
        // cycle (measured: the phase at each new note wanders, two equal sines sum or cancel by chance). Here each
        // voice's oscillators start from their own spread-out phases and keep running from note to note.
        uint32_t s = 0x9e3779b9u;
        for (auto& v : voices)
        {
            for (auto& o : v.osc) { s = s * 1664525u + 1013904223u; o.phase = (double) (s >> 8) / 16777216.0; }
            s = s * 1664525u + 1013904223u; v.modPhase = (double) (s >> 8) / 16777216.0;
        }
        for (auto& f : fx) f.prepare (rate);
        eq.prepare (rate);
        busL.assign ((size_t) maxBlock, 0); busR.assign ((size_t) maxBlock, 0);
        bypL.assign ((size_t) maxBlock, 0); bypR.assign ((size_t) maxBlock, 0);
    }

    // ---- patch and tables (call from the message thread) ----
    // (all of these on the audio thread, between blocks)
    void setPatch (const Patch& p)
    {
        patch = p; for (int i = 0; i < 8; ++i) macro[(size_t) i] = p.knob[(size_t) i];
        // the knobs with any modulation (the others are just their values, without the slot walk)
        for (int k = 0; k < kNumKnobs; ++k)
        {
            const auto& m = p.mod[(size_t) k];
            hasMod[(size_t) k] = m.slot[0].source >= 0 || m.slot[1].source >= 0 || (m.slot[2].source >= 0 && ! m.sideChain);
        }
    }
    std::array<bool, kNumKnobs> hasMod {};
    inline float knobNow (const Voice& v, int knob) const
    {
        return hasMod[(size_t) knob] ? modded (v, knob) : (knob < 8 ? macro[(size_t) knob] : patch.knob[(size_t) knob]);
    }
    void setTable (int osc, std::shared_ptr<const Wavetable> t) { table[(size_t) osc] = std::move (t); }

    // ---- MIDI ----
    int freeVoice()
    {
        for (int i = 0; i < kMaxVoices; ++i) if (! voices[(size_t) i].active) return i;
        int best = 0; uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < kMaxVoices; ++i) if (voices[(size_t) i].age < oldest) { oldest = voices[(size_t) i].age; best = i; }
        return best;
    }

    // a knob's value at note on, with the modulation that is known then (macros, velocity, key tracking)
    float noteOnKnob (int knob, float vel, int note) const
    {
        float val = patch.knob[(size_t) knob];
        const auto& m = patch.mod[(size_t) knob];
        auto known = [&] (int s, float& x)
        {
            if (s >= msMacro1 && s <= msMacro8) x = macro[(size_t) (s - msMacro1)];
            else if (s == msVelocity) x = vel;
            else if (s == msKeyTrack) x = (float) note / 127.0f;
            else return false;
            return true;
        };
        float scv = 1.0f;
        const bool sc = m.sideChain && m.slot[2].source >= 0;
        if (sc && known (m.slot[2].source, scv)) scv = std::clamp (scv, -1.0f, 1.0f);
        for (int i = 0; i < 3; ++i)
        {
            const auto& sl = m.slot[(size_t) i];
            if (i == 2 && m.sideChain) continue;
            float x = 0.0f;
            if (sl.source >= 0 && known (sl.source, x)) val += x * sl.amount * (sc && m.scTarget[(size_t) i] ? scv : 1.0f);
        }
        return std::clamp (val, 0.0f, 1.0f);
    }

    void startVoice (Voice& v, int note, float vel, int uniIdx, int uniCount, bool legato, float fromPitch)
    {
        const Patch& p = patch;
        v.active = true; v.gate = true; v.note = note; v.vel = vel; v.age = ++ageCounter;
        v.uniIndex = uniIdx; v.uniCount = uniCount; v.fadeOut = 1.0f; v.stealing = false;
        v.channel = noteChannel;
        // unison spreads
        const float c = uniCount > 1 ? (float) uniIdx - 0.5f * (float) (uniCount - 1) : 0.0f;
        v.uniPitch = 0; v.uniWt = 0; v.uniPan = 0;
        if (uniCount > 1)
        {
            if (p.uniPitchOn)
            {
                // measured: the spread fader runs from the range's low to its high end; voices sit at -S, 0, +S (three),
                // +-S/2 (two), and so on; chord mode stacks them 0, +S, -S, +2S, -2S ...
                const float x = noteOnKnob (kUniPitch, vel, note);
                const float S = p.uniRangeDown + x * (p.uniRangeUp - p.uniRangeDown);
                if (! p.uniChordMode) v.uniPitch = c * S;
                else { const int j = uniIdx; v.uniPitch = j == 0 ? 0.0f : ((j % 2) ? 1.0f : -1.0f) * (float) ((j + 1) / 2) * S; }
            }
            const float n = c / (0.5f * (float) (uniCount - 1));       // -1..1
            if (p.uniWtOn) v.uniWt = 0.5f * n * p.knob[kUniWtPos];
            if (p.uniPanOn) v.uniPan = n * (2.0f * p.knob[kUniPan] - 1.0f);
        }
        const float target = (float) note;
        const float gl = law::glideSeconds (p.knob[kGlide]);
        if (legato && gl > 0.0005f && fromPitch > -100)
        {
            v.pitchNow = fromPitch;
            startGlide (v, target, gl, p.glideRateMode);
        }
        else { v.pitchNow = v.pitchTarget = target; v.glideRate = 0; }
        std::uniform_real_distribution<float> u (0, 1);
        v.trigRand = u (rng);
        const bool retrigEnv = ! legato || p.triggerMode == tmAlways;
        if (retrigEnv)
        {
            for (int e = 0; e < 4; ++e) v.env[(size_t) e].noteOn (p, e, vel, note, false);
            for (int m = 0; m < 4; ++m) v.page[(size_t) m].noteOn (p, m);
            v.oscEnv.on();
            if (p.restartPhase)
            {
                for (int o = 0; o < 3; ++o) v.osc[(size_t) o].phase = p.oscPhase[(size_t) o];
                v.modPhase = p.oscPhase[3];
            }
        }
        if (! legato) { v.fb = 0; for (auto& f : v.flt) f.reset(); for (auto& i : v.ins) i.reset(); v.ampPrev = 0; }
        v.noiseSeed = (uint32_t) (rng() | 1);
    }

    // a glide from where the voice is now: "Equal" takes the glide time for any interval, "Rate" that time per
    // octave (measured: 24 semitones take twice as long as 12)
    static void startGlide (Voice& v, float target, float gl, bool rateMode)
    {
        v.glideFrom = v.pitchNow; v.pitchTarget = target; v.glideTime = 0.0f;
        v.glideT = rateMode ? gl * std::abs (target - v.pitchNow) / 12.0f : gl;
        v.glideRate = v.glideT > 1.0e-4f ? 1.0f : 0.0f;
        if (v.glideRate == 0.0f) v.pitchNow = target;
    }

    void noteOn (int note, int velocity)
    {
        const Patch& p = patch;
        const float vel = (float) velocity / 127.0f;
        const int uni = std::clamp (p.unison, 1, 16);
        if (p.voiceMode == vmPoly)
        {
            for (int u = 0; u < uni; ++u)
            {
                // voice limit
                int count = 0; for (auto& v : voices) if (v.active && v.gate) ++count;
                Voice* v = nullptr;
                if (count >= std::max (1, p.maxVoices)) { int oldest = -1; uint64_t a = UINT64_MAX; for (int i = 0; i < kMaxVoices; ++i) if (voices[(size_t) i].active && voices[(size_t) i].age < a) { a = voices[(size_t) i].age; oldest = i; } if (oldest >= 0) v = &voices[(size_t) oldest]; }
                if (v == nullptr) v = &voices[(size_t) freeVoice()];
                startVoice (*v, note, vel, u, uni, false, -999);
            }
        }
        else
        {
            const bool wasHeld = ! held.empty();
            float from = -999; for (auto& v : voices) if (v.active && v.gate) { from = v.pitchNow; break; }
            held.push_back (note);
            const bool legato = wasHeld && p.triggerMode != tmAlways;
            const bool glide = wasHeld || from > -100;
            if (p.voiceMode == vmMonoRotate)
            {
                for (auto& v : voices) if (v.active && v.gate) { v.gate = false; v.stealing = true; }
                for (int u = 0; u < uni; ++u) startVoice (voices[(size_t) freeVoice()], note, vel, u, uni, false, glide ? from : -999);
            }
            else
            {
                int idx = 0;
                for (int i = 0; i < kMaxVoices && idx < uni; ++i)
                    if (voices[(size_t) i].active && voices[(size_t) i].uniCount == uni && voices[(size_t) i].uniIndex == idx && (voices[(size_t) i].gate || ! legato))
                    { startVoice (voices[(size_t) i], note, vel, idx, uni, legato || voices[(size_t) i].gate, glide ? voices[(size_t) i].pitchNow : -999); ++idx; }
                for (; idx < uni; ++idx) startVoice (voices[(size_t) freeVoice()], note, vel, idx, uni, false, from);
            }
        }
    }

    void noteOff (int note)
    {
        const Patch& p = patch;
        if (p.voiceMode != vmPoly)
        {
            held.erase (std::remove (held.begin(), held.end(), note), held.end());
            if (! held.empty())
            {
                const int back = held.back();
                for (auto& v : voices) if (v.active && v.gate && v.note == note)
                { v.note = back; const float gl = law::glideSeconds (p.knob[kGlide]);
                  if (gl > 0.0005f) startGlide (v, (float) back, gl, p.glideRateMode); else { v.pitchTarget = (float) back; v.pitchNow = v.pitchTarget; } }
                return;
            }
        }
        for (auto& v : voices)
            if (v.active && v.gate && v.note == note && (noteChannel == 0 || v.channel == noteChannel))
            {
                v.gate = false;
                for (int e = 0; e < 4; ++e) v.env[(size_t) e].noteOff (p, e);
            }
    }

    void allNotesOff() { for (auto& v : voices) if (v.active) { v.gate = false; for (int e = 0; e < 4; ++e) v.env[(size_t) e].release(); } held.clear(); }
    void allSoundOff() { for (auto& v : voices) v.active = false; held.clear(); chanBend.fill (0.0f); chanPress.fill (-1.0f); chanSlide.fill (-1.0f); }

    // ---- modulation ----
    float sourceValue (const Voice& v, int s) const
    {
        switch (s)
        {
            case msEnv1: case msEnv2: case msEnv3: case msEnv4: return v.env[(size_t) (s - msEnv1)].output();
            case msMod5: case msMod6: case msMod7: case msMod8: return v.page[(size_t) (s - msMod5)].out;
            case msMacro1: case msMacro2: case msMacro3: case msMacro4: case msMacro5: case msMacro6: case msMacro7: case msMacro8:
                return macro[(size_t) (s - msMacro1)];
            case msKeyTrack: return (float) v.note / 127.0f;
            case msVelocity: return v.vel;
            case msAftertouch: return voicePressure (v);
            case msTrigRandom: return v.trigRand;
            case msIntEnv5: case msIntEnv6: case msIntEnv7: case msIntEnv8: return v.page[(size_t) (s - msIntEnv5)].ie.v;
            case msOscEnv: return v.oscEnv.v;
            default: return 0.0f;
        }
    }

    float modded (const Voice& v, int knob) const
    {
        const auto& m = patch.mod[(size_t) knob];
        float sum = 0.0f; bool any = false;
        for (int i = 0; i < 3; ++i)
        {
            const auto& sl = m.slot[(size_t) i];
            if (sl.source < 0 || (i == 2 && m.sideChain)) continue;
            float val = sourceValue (v, sl.source) * sl.amount;
            // the side chain scales by its source's value as it is: a bipolar LFO turns the modulation round
            // (measured: velocity on the filter FM knob, side chain LFO - its negative half takes the knob below 0)
            if (m.sideChain && m.slot[2].source >= 0 && m.scTarget[(size_t) i]) val *= std::clamp (sourceValue (v, m.slot[2].source), -1.0f, 1.0f);
            sum += val; any = true;
        }
        float base = patch.knob[(size_t) knob];
        if (knob < 8) base = macro[(size_t) knob];
        return any ? std::clamp (base + sum, 0.0f, 1.0f) : base;
    }

    // ---- oscillator read ----
    // The first two entries of the table list ("VA") are not tables but two classic oscillators, found by
    // looking at their waveforms on a 5 x 5 grid of position and intensity:
    //   VA-PWM : position crossfades a pulse into a saw, intensity is the pulse's width (0 and 1: no pulse left)
    //   VA-Sync: a square-to-saw oscillator hard-synced to the note, running 2^(6 * intensity) times as fast
    static inline float polyBlep (float t, float dt)      // the correction for a step of -2 at t = 0
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    static inline float vaOsc (int slot, float ph, float dt, float pos, float inten)
    {
        dt = std::clamp (dt, 1.0e-6f, 0.45f); pos = std::clamp (pos, 0.0f, 1.0f); inten = std::clamp (inten, 0.0f, 1.0f);
        if (slot == 0)
        {
            const float saw = 2.0f * ph - 1.0f - polyBlep (ph, dt);
            float pulse = 0.0f;
            if (inten > 0.0005f && inten < 0.9995f)
            {
                pulse = ph < inten ? -1.0f : 1.0f;
                pulse -= polyBlep (ph, dt);                                             // down at the start of the period
                float e = ph - inten; if (e < 0.0f) e += 1.0f; pulse += polyBlep (e, dt);   // up at the width
            }
            return (1.0f - pos) * pulse + pos * saw;
        }
        float R = std::exp2 (6.0f * inten); R = std::min (R, 0.45f / dt);
        const float x = ph * R; const int cyc = (int) x; const float psi = x - (float) cyc, dts = R * dt;
        float fr = R - std::floor (R); const bool whole = fr < 1.0e-4f;
        // steps inside the period: the slave's own restarts (not the first cycle's start, which is the note's) ...
        float wrap = 0.0f;
        if (psi < dts && cyc > 0) { const float t = psi / dts; wrap = t + t - t * t - 1.0f; }
        else if (psi > 1.0f - dts && (float) (cyc + 1) < R - 1.0e-4f) { const float t = (psi - 1.0f) / dts; wrap = t * t + t + t + 1.0f; }
        // ... and the note's restart, which cuts the last cycle short
        const float master = polyBlep (ph, dt);
        const float saw = 2.0f * psi - 1.0f - wrap - (whole ? 1.0f : fr) * master;
        float sq = psi < 0.5f ? -1.0f : 1.0f;
        const float e = psi - 0.5f;
        if (e >= 0.0f && e < dts) { const float t = e / dts; sq += t + t - t * t - 1.0f; }
        else if (e < 0.0f && e > -dts && (float) cyc + 0.5f < R) { const float t = e / dts; sq += t * t + t + t + 1.0f; }
        sq -= wrap;
        if (whole || fr >= 0.5f) sq -= master;
        return (1.0f - pos) * sq + pos * saw;
    }

    // What a table read needs that stays the same over a control block (the mode's law, the band limit), worked
    // out once per block: the per-sample read below is then lookups and blends (the same results as before).
    struct OscRead
    {
        int mode { 0 }; float gain { 1.0f };
        bool bend { false }, plus { false }; float pw { 1.0f };     // the bend's power
        float r { 1.0f };                                           // the formant's read speed
        int L { 0 }, L2 { 0 }; float lf { 0.0f };                   // the band-limit levels and their blend
    };
    inline OscRead prepOsc (float inten, int mode, float freq) const
    {
        OscRead c; c.mode = mode;
        float effFreq = freq; float xs = 10.0f;
        switch (mode)
        {
            case omSpectrum:
            {
                // Measured (21 intensities x 4 notes x 2 tables): the frame's harmonics in octave bands, band 0 the
                // first harmonic alone. x = I * log2 (33.5 kHz / frame frequency) bands are open above it, the
                // topmost one fading in linearly; the level follows x (-6 dB with only the first harmonic left).
                xs = inten >= 0.999f ? 10.0f : inten * std::log2 (33488.0f / std::max (1.0f, freq));
                static const float gdb[14] = { -6.0f, -5.1f, -4.4f, -3.6f, -2.8f, -2.0f, -1.6f, -1.2f, -0.9f, -0.7f, -0.5f, -0.3f, -0.1f, 0.0f };   // x = 0, 0.4, ...
                const float gx = std::clamp (xs / 0.4f, 0.0f, 12.999f); const int gi = (int) gx;
                c.gain = inten >= 0.999f ? 1.0f : std::pow (10.0f, (gdb[gi] + (gdb[gi + 1] - gdb[gi]) * (gx - (float) gi)) / 20.0f);
                break;
            }
            case omBendPlus: case omBendMinus: case omBendMinusPlus:
            {
                // the warped read keeps the table's own band limit (measured: no harmonics are lost to the bend)
                float amt;
                if (mode == omBendPlus) { amt = inten; c.plus = true; }
                else if (mode == omBendMinus) { amt = inten; c.plus = false; }
                else { c.plus = inten >= 0.5f; amt = std::abs (inten - 0.5f) * 2.0f; }
                if (amt > 0.0f) { c.bend = true; c.pw = c.plus ? std::exp2 (2.0f * amt) : std::pow (2.92f, amt); }
                break;
            }
            case omFormant:
                c.r = std::exp2 (4.0f * inten);
                effFreq = freq * c.r;
                break;
            default: break;
        }
        // anti-aliasing cap (band b holds harmonics up to 2^(b+1)-1)
        const float maxH = (0.45f * sr) / std::max (1.0f, effFreq);
        const float xa = std::log2 (std::max (1.0f, maxH + 1.0f)) - 1.0f + 0.5f;
        // a bend slows the read down where the frame jumps, stretching a band-limited jump into a ramp: the original
        // keeps those jumps sharp (no harmonic loss at any bend), so a bending read takes the full table
        const bool bending = (mode == omBendPlus || mode == omBendMinus) ? inten > 0.0f : (mode == omBendMinusPlus && std::abs (inten - 0.5f) > 1.0e-3f);
        float x = bending ? (float) (kLevels - 1) : std::min (xs, xa);
        x = std::clamp (x, 0.0f, (float) (kLevels - 1));
        c.L = (int) x; c.lf = x - (float) c.L;
        c.L2 = std::min (kLevels - 1, c.L + 1);
        return c;
    }

    inline float readOsc (const Wavetable& t, const OscRead& c, double phase, float pos, float pmCycles) const
    {
        double ph = phase + (double) pmCycles; ph -= std::floor (ph);
        float u = (float) ph; float window = 1.0f;
        if (c.bend)
        {
            if (c.plus) u = u < 0.5f ? 0.5f * std::pow (2.0f * u, c.pw) : 1.0f - 0.5f * std::pow (2.0f * (1.0f - u), c.pw);
            else { const float w = 2.0f * u - 1.0f; u = 0.5f + 0.5f * (w < 0 ? -1.0f : 1.0f) * std::pow (std::abs (w), c.pw); }
        }
        else if (c.mode == omFormant)
        {
            // the window peaks mid-cycle, where the table is read from its start (measured: a table read
            // half a frame on matches every intensity, 0.5 dB rms on the harmonics of a square-saw)
            window = 0.5f - 0.5f * std::cos (2.0f * kPi * u);
            u = u * c.r + 0.5f; u -= std::floor (u);
        }
        const int L = c.L, L2 = c.L2; const float lf = c.lf;
        // frames
        const float fpos = std::clamp (pos, 0.0f, 1.0f) * (float) (t.numFrames - 1);
        const int f0 = std::min (t.numFrames - 1, (int) fpos), f1 = std::min (t.numFrames - 1, f0 + 1);
        const float ff = fpos - (float) f0;
        const float idx = u * (float) kFrameSize; const int i0 = std::min (kFrameSize - 1, (int) idx); const float fr = idx - (float) i0;
        auto rd = [&] (int f, int lev) { const float* d = t.level (f, lev); return d[i0] + (d[i0 + 1] - d[i0]) * fr; };
        float a = rd (f0, L), b = (lf > 0.0f ? rd (f0, L2) : a);
        float v0 = a + lf * (b - a);
        if (ff > 0.0f && f1 != f0)
        {
            const float cc = rd (f1, L), d = (lf > 0.0f ? rd (f1, L2) : cc);
            v0 += ff * ((cc + lf * (d - cc)) - v0);
        }
        return v0 * c.gain * window;
    }

    // The feedback signal as it goes round: untouched up to 8 (far above what any factory sound reaches), then bent
    // towards 10 - a loop with more than unity gain (a singing Daft filter fed back on itself) stays bounded instead
    // of running away to infinity.
    static inline float fbLimit (float x)
    {
        if (! (std::abs (x) > 8.0f)) return x == x ? x : 0.0f;
        const float a = std::abs (x) - 8.0f;
        return std::copysign (8.0f + 2.0f * fastTanh (a * 0.5f), x);
    }

    // ---- noise ----
    // a white "sample" read with linear interpolation at a rate set by the colour: 6 octaves over the knob
    // (measured from the nulls at multiples of rate * sr); just under 1 at the top so the read position keeps drifting
    static double noiseRate (float color) { return std::min (0.9993, std::exp2 (6.0 * ((double) color - 1.0))); }
    inline float noiseTick (Voice& v, double rate)
    {
        v.noisePh += rate;
        while (v.noisePh >= 1.0)
        {
            v.noisePh -= 1.0; v.noiseA = v.noiseB;
            v.noiseSeed ^= v.noiseSeed << 13; v.noiseSeed ^= v.noiseSeed >> 17; v.noiseSeed ^= v.noiseSeed << 5;
            v.noiseB = (float) (v.noiseSeed & 0xffffff) / 8388607.5f - 1.0f;
        }
        return v.noiseA + (v.noiseB - v.noiseA) * (float) v.noisePh;
    }

    // ---- one voice, n samples, added into L/R and the bypass buffer ----
    void renderVoice (Voice& v, float* L, float* R, float* BL, float* BR, int n, double bpm)
    {
        const Patch& p = patch;
        for (int start = 0; start < n; start += kCtrl)
        {
            const int m = std::min (kCtrl, n - start);
            // modulators
            const float* kb = p.knob.data();
            // the envelopes' own knobs with their modulation (velocity on levels, macros on times, ...)
            for (int i = kEnv1Vel; i <= kEnv4RelT; ++i) v.k[(size_t) i] = knobNow (v, i);
            for (int e = 0; e < 4; ++e) v.env[(size_t) e].process (p, e, v.k.data(), m);
            for (int pg = 0; pg < 4; ++pg) v.page[(size_t) pg].tickEnv (p, pg, m);
            v.oscEnv.tick (p.intAttack[4], p.intDecay[4], (float) m / sr);
            for (int i = kMod5Rate; i <= kMod8Restart; ++i) v.k[(size_t) i] = knobNow (v, i);   // the pages' own knobs, modulated
            for (int pg = 0; pg < 4; ++pg) v.page[(size_t) pg].process (p, pg, v.k.data(), m, bpm);
            for (int i = 0; i < kNumKnobs; ++i) v.k[(size_t) i] = knobNow (v, i);
            const auto& k = v.k;
            // amp
            float ampV = 1.0f;
            bool anyAmp = false;
            // only the first slot drives the amp (measured: a source in the second changes nothing)
            for (int s = 0; s < 1; ++s)
            {
                const auto& sl = p.ampMod[0].slot[(size_t) s];
                if (sl.source < 0) continue;
                anyAmp = true; ampV *= std::clamp (sourceValue (v, sl.source) * sl.amount, 0.0f, 1.0f);
            }
            if (! anyAmp || ! p.ampOn) ampV = v.gate ? 1.0f : 0.0f;
            ampV = ampV * ampV;
            const bool envDone = ! v.env[3].active() && p.ampMod[0].slot[0].source == msEnv4;
            // glide
            if (v.glideRate > 0)
            {
                // measured: the same curve for any interval, fast at first, 1 - (1 - t/T)^1.8 of the way after t
                v.glideTime += (float) m / sr;
                if (v.glideTime >= v.glideT) { v.pitchNow = v.pitchTarget; v.glideRate = 0; }
                else v.pitchNow = v.glideFrom + (v.pitchTarget - v.glideFrom) * (1.0f - std::pow (1.0f - v.glideTime / v.glideT, 1.8f));
            }
            // vibrato
            const float vibHz = law::lfoHz (k[kVibRate]);
            float& vph = p.vibMono ? globalVibPh : v.vibPh;
            vph += vibHz * (float) m / sr; vph -= std::floor (vph);
            const float vib = k[kVibDepth] * 2.0f * std::sin (2.0f * kPi * vph);
            const float basePitch = v.pitchNow + bend * (bend >= 0 ? p.bendUp : -p.bendDown) + voiceBendSemis (v) + p.globalTune + v.uniPitch + vib;
            // frequencies
            float freq[3], modF;
            for (int o = 0; o < 3; ++o)
            {
                float note = basePitch;
                if (p.keyTrackOsc[(size_t) o] == 0) note = 64.0f + (basePitch - v.pitchNow);
                else if (p.keyTrackOsc[(size_t) o] == 2) note = p.oscUserCurve.eval (v.pitchNow) + (basePitch - v.pitchNow);
                const float semis = (k[(size_t) (kOsc1Pitch + 5 * o)] - 0.5f) * 128.0f;
                const float div = table[(size_t) o] && p.wavetable[(size_t) o] > 1 ? table[(size_t) o]->pitchDiv : 1.0f;
                freq[o] = midiHz (note + semis) / div;
            }
            {
                float note = p.keyTrackOsc[3] == 0 ? 64.0f + (basePitch - v.pitchNow) : basePitch;
                modF = midiHz (note + (k[kModOscPitch] - 0.5f) * 128.0f);
            }
            const float noteHzIns = midiHz (p.keyTrackOsc[4] == 0 ? 64.0f : basePitch);
            // filters
            float fmMul[2] = { 1.0f, 1.0f };
            for (int f = 0; f < 2; ++f)
            {
                float cut = k[(size_t) (kF1Cut + 3 * f)], res = k[(size_t) (kF1Res + 3 * f)];
                // filter keytracking (default: 100 %, centred on note 60)
                if (p.keyTrackFilter[(size_t) f] > 0)
                {
                    const int set = p.keyTrackFilter[(size_t) f] - 1;
                    cut += (p.filterCurveCut[(size_t) set].eval (v.pitchNow) - 60.0f) / 120.0f;
                    res += (p.filterCurveRes[(size_t) set].eval (v.pitchNow) - 64.0f) / 127.0f * 0.0f;
                }
                v.flt[(size_t) f].setParams (p.filterOn[(size_t) f] ? p.filterType[(size_t) f] : ftNone, cut, k[(size_t) (kF1Bw + 3 * f)], res, fmMul[f]);
            }
            const float fg[2] = { (k[kF1Gain] / 0.8f) * (k[kF1Gain] / 0.8f), (k[kF2Gain] / 0.8f) * (k[kF2Gain] / 0.8f) };
            // level/route constants
            float amp[5], r1[5], r2[5];
            for (int o = 0; o < 3; ++o)
            {
                const float a = k[(size_t) (kOsc1Amp + 5 * o)]; amp[o] = p.oscOn[(size_t) o] && table[(size_t) o] ? a * a : 0.0f;
                const float r = k[(size_t) (kOsc1Route + 5 * o)]; r1[o] = std::min (1.0f, 2.0f * r); r2[o] = std::min (1.0f, 2.0f * (1.0f - r));
            }
            { const float a = k[kNoiseAmp]; amp[3] = p.noiseOn ? a * a : 0.0f; const float r = k[kNoiseRoute]; r1[3] = std::min (1.0f, 2.0f * r); r2[3] = std::min (1.0f, 2.0f * (1.0f - r)); }
            { const float a = k[kFbAmp]; amp[4] = p.fbOn && p.fbPos != fbOff ? a * a : 0.0f; const float r = k[kFbRoute]; r1[4] = std::min (1.0f, 2.0f * r); r2[4] = std::min (1.0f, 2.0f * (1.0f - r)); }
            const float sp = k[kSerPar];
            const float serA = std::cos (0.5f * kPi * sp), serB = std::sin (0.5f * kPi * sp);
            const float mx = k[kFltMix];
            const float mixF1 = std::sin (0.5f * kPi * mx), mixF2 = std::cos (0.5f * kPi * mx);
            const float pan = std::clamp (k[kPan] + 0.5f * v.uniPan, 0.0f, 1.0f);
            const float panL = std::cos (0.5f * kPi * pan), panR = std::sin (0.5f * kPi * pan);
            // the modulation oscillator switched off takes its four modulations with it (the oscillators play on)
            const float mOn = p.modOscOn ? 1.0f : 0.0f;
            const float rm = k[kModOscRM] * mOn, pm = k[kModOscPhase] * mOn, pmod = k[kModOscPos] * mOn, ffm = k[kModOscFltFM] * mOn;
            const float pos[3] = { std::clamp (k[kOsc1Pos] + v.uniWt + voiceSlidePos (v, 0), 0.0f, 1.0f),
                                   std::clamp (k[kOsc2Pos] + v.uniWt + voiceSlidePos (v, 1), 0.0f, 1.0f),
                                   std::clamp (k[kOsc3Pos] + v.uniWt + voiceSlidePos (v, 2), 0.0f, 1.0f) };
            const float inten[3] = { k[kOsc1Intensity], k[kOsc2Intensity], k[kOsc3Intensity] };
            OscRead rdc[3];
            float vaSyncG[3] = { 1.0f, 1.0f, 1.0f };
            for (int o = 0; o < 3; ++o)
            {
                if (p.wavetable[(size_t) o] > 1) rdc[o] = prepOsc (inten[o], p.oscMode[(size_t) o], freq[o]);
                // VA-Sync gets quieter as the slave ratio climbs: -3.5 dB from intensity 0.9 (measured)
                else if (p.wavetable[(size_t) o] == 1) vaSyncG[o] = std::pow (10.0f, -std::clamp ((inten[o] - 0.53f) * 9.5f, 0.0f, 3.5f) / 20.0f);
            }
            const double noiseR = noiseRate (k[kNoiseColor]);
            // the modulation oscillator's sines only when something listens
            const bool needMo = (rm != 0.0f && p.rmTarget > 0) || (pmod != 0.0f && p.posTarget > 0), needMoPm = pm != 0.0f && p.pmTarget > 0;
            // filter FM: the cutoff swings +-7470 Hz x amount^2 (measured on the self-oscillating ladder: 75 / 466 / 1866 /
            // 4212 Hz at 0.1 / 0.25 / 0.5 / 0.75, the same at 3 and 30 Hz and at any cutoff)
            const float fmD = 7470.0f * ffm * ffm;
            const int fmF = fmD > 0.0f && p.fmTarget > 0 && p.fmTarget <= 2 && p.filterOn[(size_t) (p.fmTarget - 1)]
                            && v.flt[(size_t) (p.fmTarget - 1)].fmOk ? p.fmTarget - 1 : -1;
            const int ip0 = p.insertOn[0] ? p.insertPos[0] : ipOff, ip1 = p.insertOn[1] ? p.insertPos[1] : ipOff;
            const int it0 = p.insertType[0], it1 = p.insertType[1];
            const float bypG = p.bypassOn ? k[kBypassGain] * k[kBypassGain] : 0.0f;
            const float ampStep = (ampV - v.ampPrev) / (float) m;

            auto insAt = [&] (int position, float x) -> float
            {
                if (ip0 == position && it0 != itNone) x = v.ins[0].tick (it0, x, k[kIns1P1], k[kIns1P2], noteHzIns);
                if (ip1 == position && it1 != itNone) x = v.ins[1].tick (it1, x, k[kIns2P1], k[kIns2P2], noteHzIns);
                return x;
            };

            for (int i = 0; i < m; ++i)
            {
                // mod osc
                v.modPhase += modF / sr; v.modPhase -= std::floor (v.modPhase);
                // measured against the oscillators (both restart with the note): the ring modulator's sine runs
                // 8.7 samples late, the phase modulator's 0.085 of a cycle late
                const float mo = needMo ? std::sin (2.0f * kPi * ((float) v.modPhase - tune::modDelay() / 44100.0f * modF)) : 0.0f;
                const float moPm = needMoPm ? std::sin (2.0f * kPi * ((float) v.modPhase - tune::modPhase())) : 0.0f;
                if (fmF >= 0) v.flt[(size_t) fmF].fm (fmD * std::sin (2.0f * kPi * (float) v.modPhase));
                // the bypass taps its source before the amp knob (measured: a bypassed oscillator with its amp at 0
                // still sounds, at the level the amp knob would give it)
                float src[5], raw[4] { 0, 0, 0, 0 };
                for (int o = 0; o < 3; ++o)
                {
                    const bool tapped = bypG > 0.0f && p.bypassSource == o && p.oscOn[(size_t) o] && table[(size_t) o];
                    if (amp[o] == 0.0f && ! tapped) { src[o] = 0; v.osc[(size_t) o].phase += freq[o] / sr; continue; }
                    auto& os = v.osc[(size_t) o];
                    os.phase += freq[o] / sr; os.phase -= std::floor (os.phase);
                    const float pmc = (p.pmTarget == o + 1) ? pm * 1.5f * moPm : 0.0f;
                    float ps = pos[o]; if (p.posTarget == o + 1) ps = std::clamp (ps + pmod * mo, 0.0f, 1.0f);
                    float s;
                    if (p.wavetable[(size_t) o] <= 1)
                    {
                        double vp = os.phase + (double) pmc; vp -= std::floor (vp);
                        s = vaOsc (p.wavetable[(size_t) o], (float) vp, freq[o] / sr, ps, inten[o]) * tune::vaGain();
                        if (p.wavetable[(size_t) o] == 1) s *= vaSyncG[o];
                    }
                    else s = readOsc (*table[(size_t) o], rdc[o], os.phase, ps, pmc);
                    if (p.rmTarget == o + 1) s *= (1.0f - rm) + rm * mo;
                    raw[o] = s; src[o] = s * amp[o];
                }
                if (amp[3] != 0.0f || (bypG > 0.0f && p.bypassSource == 3 && p.noiseOn)) raw[3] = noiseTick (v, noiseR);
                src[3] = raw[3] * amp[3];
                float fbs = v.fb;
                if (amp[4] != 0.0f) fbs = insAt (ipFbLoop, fbs);
                src[4] = fbs * amp[4];
                float b1 = 0, b2 = 0;
                for (int s = 0; s < 5; ++s) { b1 += src[s] * r1[s]; b2 += src[s] * r2[s]; }
                b1 = insAt (ipPreF1, b1);
                b2 = insAt (ipPreF2, b2);
                float f1 = p.filterType[0] != ftNone && p.filterOn[0] ? v.flt[0].tick (b1) * fg[0] : b1;
                f1 = insAt (ipPostF1, f1);
                const float ser = insAt (ipSerial, f1);
                float f2in = serA * b2 + serB * ser;
                float f2 = p.filterType[1] != ftNone && p.filterOn[1] ? v.flt[1].tick (f2in) * fg[1] : f2in;
                f2 = insAt (ipPostF2, f2);
                float mixv = mixF1 * f1 + mixF2 * f2;
                if (p.fbPos == fbPostF1) v.fb = fbLimit (f1); else if (p.fbPos == fbPostF2) v.fb = fbLimit (f2);
                mixv = insAt (ipPreAmp, mixv);
                if (p.fbPos == fbPreAmp) v.fb = fbLimit (mixv);
                v.ampPrev += ampStep;
                const float out = mixv * v.ampPrev * v.fadeOut;
                if (p.fbPos == fbPostAmp) v.fb = fbLimit (out);
                L[start + i] += out * panL; R[start + i] += out * panR;
                if (bypG > 0.0f)
                {
                    const float b = raw[std::clamp (p.bypassSource, 0, 3)] * bypG * v.ampPrev * v.fadeOut * tune::bypGain();
                    BL[start + i] += b; BR[start + i] += b;
                }
                if (v.stealing) { v.fadeOut -= 1.0f / (0.004f * sr); if (v.fadeOut <= 0) { v.fadeOut = 0; v.active = false; } }
            }
            v.ampPrev = ampV;
            if (envDone && ! v.gate) { v.active = false; return; }
        }
    }

    // ---- the block ----
    // The knobs after the voices (FX, EQ, master) are modulated once per block:
    // macros and MIDI sources directly, envelopes and the mod pages as the
    // newest voice has them.
    float globalModded (int knob) const
    {
        const Voice* nv = &voices[0];
        for (auto& v : voices) if (v.age > nv->age) nv = &v;
        return modded (*nv, knob);
    }

    std::array<float, 8> fxMod {};          // modulation offsets of the eight FX knobs (this block)
    float eqK[4] { 0.5f, 0.5f, 0.5f, 0.5f }, masterK { 0.5f };

    // the voices: their sum into L/R, the bypass bus into BL/BR
    void renderVoices (float* L, float* R, float* BL, float* BR, int n, double hostBpm)
    {
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        std::fill (BL, BL + n, 0.0f); std::fill (BR, BR + n, 0.0f);
        const double bpm = hostBpm > 0 ? hostBpm : patch.presetBpm;
        for (auto& v : voices) if (v.active) renderVoice (v, L, R, BL, BR, n, bpm);
        for (int i = 0; i < 8; ++i) fxMod[(size_t) i] = globalModded (kFx1DW + i) - patch.knob[(size_t) (kFx1DW + i)];
        for (int i = 0; i < 4; ++i) eqK[i] = globalModded (kEqLow + i);
        masterK = globalModded (kMasterVol);
    }

    // FX 1 -> FX 2 -> EQ -> master on L/R; the bypass bus joins where the patch says.
    // fxOffsets: what is added to the eight FX knobs (the modulation)
    void renderEffects (float* L, float* R, const float* BL, const float* BR, int n, double hostBpm, const float* fxOffsets)
    {
        const Patch& p = patch;
        const double bpm = hostBpm > 0 ? hostBpm : p.presetBpm;
        auto addByp = [&] (int target) { if (BL != nullptr && p.bypassTarget == target) for (int i = 0; i < n; ++i) { L[i] += BL[i]; R[i] += BR[i]; } };
        auto fk = [&] (int k) { return std::clamp (p.knob[(size_t) k] + (fxOffsets ? fxOffsets[k - kFx1DW] : 0.0f), 0.0f, 1.0f); };
        addByp (btFx1);
        auto beats = [&] (int fxi, int c) { const auto& s = p.fxSync[(size_t) fxi]; return (float) s[(size_t) (2 * c)] * 4.0f / (float) std::max (1, s[(size_t) (2 * c + 1)]); };
        if (p.fxOn[0]) fx[0].process (p.fxType[0], L, R, n, fk (kFx1DW), fk (kFx1P2), fk (kFx1P3), fk (kFx1P4), beats (0, 0), beats (0, 1), bpm);
        addByp (btFx2);
        if (p.fxOn[1]) fx[1].process (p.fxType[1], L, R, n, fk (kFx2DW), fk (kFx2P2), fk (kFx2P3), fk (kFx2P4), beats (1, 0), beats (1, 1), bpm);
        addByp (btEq);
        if (p.eqOn) eq.process (L, R, n, eqK[0], eqK[1], eqK[2], eqK[3]);
        const float mv = 0.8968f * masterK * masterK;
        // the output loses its DC through a first-order high-pass at 9.4 Hz (a plain sine: -1.7 dB at 13.75 Hz, -0.5 dB at 27.5 Hz)
        const float a = 1.0f / (1.0f + 2.0f * kPi * 9.4f / sr);
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i] * mv, xr = R[i] * mv;
            dcY[0] = a * (dcY[0] + xl - dcX[0]); dcX[0] = xl; L[i] = dcY[0];
            dcY[1] = a * (dcY[1] + xr - dcX[1]); dcX[1] = xr; R[i] = dcY[1];
        }
    }
    float dcX[2] { 0, 0 }, dcY[2] { 0, 0 };

    void process (float* L, float* R, int n, double hostBpm)
    {
        if ((int) bypL.size() < n) { bypL.resize ((size_t) n); bypR.resize ((size_t) n); }
        renderVoices (L, R, bypL.data(), bypR.data(), n, hostBpm);
        renderEffects (L, R, bypL.data(), bypR.data(), n, hostBpm, fxMod.data());
    }

    // the effects alone (the patch version's Effects module): knobs unmodulated but for fxOffsets
    void processEffectsOnly (float* L, float* R, const float* BL, const float* BR, int n, double hostBpm, const float* fxOffsets)
    {
        for (int i = 0; i < 4; ++i) eqK[i] = patch.knob[(size_t) (kEqLow + i)];
        masterK = patch.knob[kMasterVol];
        renderEffects (L, R, BL, BR, n, hostBpm, fxOffsets);
    }
};
} // namespace massif
