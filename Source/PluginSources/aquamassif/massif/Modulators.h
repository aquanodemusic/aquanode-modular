#pragma once
// Massif - envelopes, LFOs, the stepper and the performer.
// Time and rate laws are the measured ones (see the notes in Dsp.h).
#include "Patch.h"
#include "Curves.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace massif
{
constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------------------
// measured knob laws
namespace law
{
    // attack: linear rise, T = 9.2 s * k^4
    inline float attackSeconds (float k) { return 9.2f * k * k * k * k; }
    // decay / release: exponential; time for the *amplitude* (env^2) to fall
    // 20 dB, measured at k = 0, 0.1 .. 1 (log-interpolated)
    inline float decay20dB (float k)
    {
        static const float t[11] = { 0.0007f, 0.0073f, 0.021f, 0.0559f, 0.1197f, 0.2823f, 0.6291f, 1.4142f, 3.1768f, 7.1634f, 16.03f };
        k = std::clamp (k, 0.0f, 1.0f) * 10.0f; const int i = std::min (9, (int) k); const float f = k - (float) i;
        return std::exp (std::log (t[i]) + f * (std::log (t[i + 1]) - std::log (t[i])));
    }
    // env time constant: amplitude = env^2 falls 20 dB when env falls by 1/sqrt(10). Re-measured on the envelope
    // itself (an envelope on the cutoff of a singing filter, knob 0.4 .. 0.85, and the amp at 0.55 / 0.75): the
    // original's time constant is 6 % longer than this table gives
    inline float decayTau (float k)   { return 1.06f * decay20dB (k) / 1.1513f; }
    inline float releaseTau (float k) { return 1.06f * 0.95f * decay20dB (k) / 1.1513f; }
    // linear decay: the whole way from the attack level to the decay level takes the attack's law, 9.2 s * k^4
    // (measured at knob 0.25 .. 0.95: 9.11 .. 9.16 s * k^4; it was 2.35 time constants, up to 2.5 x too long)
    inline float linearDecaySeconds (float k) { return attackSeconds (k); }
    // sustain-loop time (one pass), measured at .3 / .5 / .7 / .85 / .92 / 1 (0.119, 0.42, 1.54, 3.87, 6.0, 10 s)
    inline float sustainSeconds (float k)
    {
        static const float x[] = { 0.0f, 0.1f, 0.3f, 0.5f, 0.7f, 0.85f, 0.92f, 1.0f };
        static const float y[] = { 0.002f, 0.012f, 0.119f, 0.42f, 1.54f, 3.87f, 6.0f, 10.0f };
        k = std::clamp (k, 0.0f, 1.0f); int i = 0; while (i < 6 && k > x[i + 1]) ++i;
        const float f = (k - x[i]) / (x[i + 1] - x[i]);
        return std::exp (std::log (y[i]) + f * (std::log (y[i + 1]) - std::log (y[i])));
    }
    inline float delaySeconds (float k) { return 9.2f * k * k * k * k; }
    // LFO / vibrato rate (Hz): measured k=.25 1.66, .5 3.88, 1 32.7 (log-interp + extrapolation)
    inline float lfoHz (float k)
    {
        static const float x[6] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.0001f };
        static const float y[6] = { 0.05f, 1.66f, 3.88f, 11.3f, 32.7f, 32.7f };
        k = std::clamp (k, 0.0f, 1.0f);
        int i = 0; while (i < 4 && k > x[i + 1]) ++i;
        const float f = (k - x[i]) / (x[i + 1] - x[i]);
        return std::exp (std::log (y[i]) + f * (std::log (y[i + 1]) - std::log (y[i])));
    }
    // the glide's whole length against its knob (measured in "Equal" mode: 12 and 24 semitones alike); the
    // curve itself is in Engine (fast first, 1 - (1 - t/T)^1.8)
    inline float glideSeconds (float k)
    {
        static const float kx[7] = { 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 1.0f };
        static const float ty[7] = { 0.031f, 0.076f, 0.185f, 0.43f, 0.78f, 1.5f, 5.5f };
        if (k <= 0.3f) { const float r = std::max (0.0f, k) / 0.3f; return 0.031f * r * r * r * r; }
        int i = 0; while (i < 5 && k > kx[i + 1]) ++i;
        const float f = std::clamp ((k - kx[i]) / (kx[i + 1] - kx[i]), 0.0f, 1.0f);
        return std::exp (std::log (ty[i]) + f * (std::log (ty[i + 1]) - std::log (ty[i])));
    }
}

// ---------------------------------------------------------------------------
// LFO menu shapes (CurveShape): the original's 4 basic curves and 31 tables
inline float curve (int shape, float u, uint32_t seed = 0) { return curves::lfo (shape, u, seed); }

// ---------------------------------------------------------------------------
struct Envelope
{
    enum Stage { Idle, Delay, Attack, Decay, Sustain, Hold, Release };
    Stage stage { Idle };
    float value { 0.0f }, start { 0.0f };
    float t { 0.0f };              // seconds in the stage
    float prog { 0.0f };           // progress through the stage (0..1; the decay: time constants)
    int loopsDone { 0 };
    bool forward { true };
    float scale { 1.0f };          // velocity / keytrack factor
    float sr { 44100.0f };

    void noteOn (const Patch& p, int idx, float vel01, int note, bool retrig)
    {
        const auto& ex = p.env[(size_t) idx];
        const float velF = p.knob[(size_t) (kEnv1Vel + idx)];
        const float ktF = p.knob[(size_t) (kEnv1Ktr + idx)];
        scale = (1.0f - velF + velF * vel01) * std::clamp (1.0f - ktF * std::max (0, note - 69) / 31.0f, 0.0f, 1.0f);
        if (ex.retrigger || retrig || stage == Idle) value = 0.0f;
        start = value; t = 0.0f; prog = 0.0f; loopsDone = 0; forward = true;
        stage = p.knob[(size_t) (kEnv1Delay + idx)] > 0.0005f ? Delay : Attack;
    }
    void noteOff (const Patch& p, int idx)
    {
        const int mode = p.env[(size_t) idx].playMode;
        if (mode == epGate && stage != Idle) { stage = Release; start = value; t = 0.0f; prog = 0.0f; }
    }
    void release() { if (stage != Idle) { stage = Release; start = value; t = 0.0f; prog = 0.0f; } }
    bool active() const { return stage != Idle; }

    // advance by n samples, return the (unscaled) value at the end. The stages advance at their current rate
    // (the knobs can be modulated - even by the envelope itself: measured, an attack whose time the envelope
    // raises takes 60 ms instead of 2.4, rising ever slower), so progress is summed, not read off the clock.
    float process (const Patch& p, int idx, const float* k, int n)
    {
        const auto& ex = p.env[(size_t) idx];
        const float dt = (float) n / sr;
        t += dt;
        const float attL = k[kEnv1AttL + idx], decL = k[kEnv1DecL + idx], susL = k[kEnv1SusL + idx];
        switch (stage)
        {
            case Idle: value = 0.0f; break;
            case Delay:
                value = start;
                prog += dt / std::max (1.0e-6f, law::delaySeconds (k[kEnv1Delay + idx]));
                if (prog >= 1.0f) { stage = Attack; t = 0.0f; prog = 0.0f; start = value; }
                break;
            case Attack:
            {
                prog += dt / std::max (1.0e-6f, law::attackSeconds (k[kEnv1AttT + idx]));
                if (prog >= 1.0f) { value = attL; stage = Decay; t = 0.0f; prog = 0.0f; start = value; }
                else value = start + (attL - start) * prog;
                break;
            }
            case Decay:
            {
                const float tau = std::max (1.0e-5f, law::decayTau (k[kEnv1DecT + idx]));
                if (ex.linearDecay)
                {
                    prog += dt / std::max (1.0e-6f, law::linearDecaySeconds (k[kEnv1DecT + idx]));
                    value = prog >= 1.0f ? decL : start + (decL - start) * prog;
                    if (prog >= 1.0f) goto decayDone;
                }
                else
                {
                    prog += dt / tau;           // time constants so far
                    value = decL + (value - decL) * std::exp (-dt / tau);
                    // with a sustain loop the decay hands over after three time constants (measured); without one
                    // it just keeps settling
                    if (prog > (ex.loopCount > 0 ? 3.0f : 6.9f)) goto decayDone;
                }
                break;
            decayDone:
                value = decL; t = 0.0f; prog = 0.0f; start = value;
                stage = ex.loopCount > 0 ? Sustain : Hold;
                break;
            }
            case Sustain:
            {
                prog += dt / std::max (1.0e-6f, law::sustainSeconds (k[kEnv1SusT + idx]));
                float u = std::min (1.0f, prog);
                const float morph = k[kEnv1Morph + idx];
                // the stage's path: the first shape, morphed towards the second (the same shape twice: no morph);
                // going back it is the same path in reverse
                auto shape = [&] (float x)
                {
                    const float sa = curves::env (ex.morphShapeA, x);
                    return ex.morphShapeA == ex.morphShapeB ? sa : sa + morph * (curves::env (ex.morphShapeB, x) - sa);
                };
                value = forward ? decL + (susL - decL) * shape (u) : decL + (susL - decL) * shape (1.0f - u);
                if (prog >= 1.0f)
                {
                    t = 0.0f; prog = 0.0f; forward = ! forward; ++loopsDone;
                    const bool infinite = ex.loopCount >= 33;
                    if (! infinite && loopsDone >= ex.loopCount)
                    {
                        value = forward ? decL : susL;
                        if (ex.playMode == epOneShot) { stage = Release; start = value; }
                        else stage = Hold;
                    }
                }
                break;
            }
            case Hold:
                if (ex.playMode == epOneShot) { stage = Release; start = value; t = 0.0f; prog = 0.0f; }
                break;
            case Release:
            {
                const float tau = std::max (1.0e-5f, law::releaseTau (k[kEnv1RelT + idx]));
                value *= std::exp (-dt / tau);
                if (value < 1.0e-4f) { value = 0.0f; stage = Idle; }
                break;
            }
        }
        return value;
    }
    float output() const { return value * scale; }
};

// ---------------------------------------------------------------------------
// LFO / stepper / performer: one "modulation page" (sources 5..8). Measured with sounds made for the purpose
// and rendered by the original (a sine oscillator whose pitch the page modulates):
//  - rate: 1.022 Hz x 2^(12 (k - 0.5)) cycles (LFO) or steps (stepper, performer) per second; synced: one cycle
//    or step lasts num/den bars
//  - the Amp knob scales by k^2 (as do knob modulations of it); the LFO's XFade mixes linearly, up = upper curve
//  - stepper: a step plays its value (0..1); steps in the glide row approach it exponentially (time constant
//    from the Glide fader, 0.35 s at the top, independent of the rate); steps in the amp row are scaled by the
//    Amp Mod fader (linear, down = no modulation)
//  - performer: a step plays one cycle of its curve times its level; steps in the xfade row mix lower and upper
//    by the XFade fader (the others play the upper); the amp row as the stepper's
//  - the sequence starts at the loop start on every (restarting) note; the play marker is not a start point
//  - the internal envelope: linear attack to 1, then linear decay to 0 (decay fully up: holds); it modulates
//    only its own page's rate, amp and xfade knobs
namespace law
{
    inline float modHz (float k) { return 1.0221f * std::exp2 (12.0f * (k - 0.5f)); }
    // internal envelope attack / decay (seconds for the full 0..1 travel)
    inline float intEnvSeconds (float s)
    {
        static const float x[] = { 0.0f, 0.1f, 0.25f, 0.4f, 0.5f, 0.6f, 0.7f, 0.75f, 0.85f, 0.95f, 1.0f };
        static const float y[] = { 0.002f, 0.0557f, 0.113f, 0.278f, 0.542f, 1.238f, 3.076f, 4.785f, 8.79f, 9.9f, 10.0f };
        s = std::clamp (s, 0.0f, 1.0f); int i = 0; while (i < 9 && s > x[i + 1]) ++i;
        const float f = (s - x[i]) / (x[i + 1] - x[i]);
        return std::exp (std::log (y[i]) + f * (std::log (y[i + 1]) - std::log (y[i])));
    }
    // stepper glide time constant from the Glide fader
    inline float stepGlideTau (float g)
    {
        static const float x[] = { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f };
        static const float y[] = { 0.0003f, 0.002f, 0.011f, 0.052f, 0.285f, 0.348f };
        g = std::clamp (g, 0.0f, 1.0f); int i = 0; while (i < 4 && g > x[i + 1]) ++i;
        const float f = (g - x[i]) / (x[i + 1] - x[i]);
        return std::exp (std::log (y[i]) + f * (std::log (y[i + 1]) - std::log (y[i])));
    }
}

// an internal envelope (mod pages, OSC page): linear attack to 1, linear decay to 0, decay fully up holds
struct IntEnv
{
    float v { 0.0f }; int stage { 2 };
    void on() { v = 0.0f; stage = 0; }
    void tick (float att, float dec, float dt)
    {
        if (stage == 0) { v += dt / std::max (1.0e-4f, law::intEnvSeconds (att)); if (v >= 1.0f) { v = 1.0f; stage = 1; } }
        else if (stage == 1 && dec < 0.999f) { v -= dt / std::max (1.0e-4f, law::intEnvSeconds (dec)); if (v <= 0.0f) { v = 0.0f; stage = 2; } }
    }
};

struct ModPage
{
    double phase { 0.0 };          // in cycles (LFO) or steps (stepper/performer)
    float out { 0.0f };
    float glided { 0.0f };
    int lastStep { -1 };
    uint32_t seed { 1 };
    float sr { 44100.0f };
    IntEnv ie;                     // the page's internal envelope

    void noteOn (const Patch& p, int idx)
    {
        if (p.knob[(size_t) (kMod5Restart + idx)] > 0.5f)
        {
            phase = p.modType[(size_t) idx] == mtLFO ? 0.0
                  : (double) (p.modType[(size_t) idx] == mtStepper ? p.stepper[(size_t) idx].loopStart : p.performer[(size_t) idx].loopStart);
            lastStep = -1;
        }
        ie.on();
        seed = seed * 1664525u + 1013904223u;
    }

    void tickEnv (const Patch& p, int idx, int n) { ie.tick (p.intAttack[(size_t) idx], p.intDecay[(size_t) idx], (float) n / sr); }

    static float rateHz (const Patch& p, int idx, const float* k, double bpm)
    {
        if (k[kMod5Sync + idx] > 0.5f)
        {
            const double beats = (double) p.syncNum[(size_t) idx] * 4.0 / (double) std::max (1, p.syncDen[(size_t) idx]);
            return (float) (bpm / 60.0 / std::max (1.0e-3, beats));
        }
        return law::modHz (k[kMod5Rate + idx]);
    }

    // k: the page's knobs with their modulation applied
    float process (const Patch& p, int idx, const float* k, int n, double bpm)
    {
        const int type = p.modType[(size_t) idx];
        const float hz = rateHz (p, idx, k, bpm);
        const float ampK = k[kMod5Amp + idx], amp = ampK * ampK;
        const float x = std::clamp (k[kMod5Morph + idx], 0.0f, 1.0f);
        const float ilv = std::clamp (k[kMod5IntLevel + idx], 0.0f, 1.0f);
        phase += hz * (double) n / sr;
        if (type == mtLFO)
        {
            const auto& l = p.lfo[(size_t) idx];
            const float u = (float) (phase - std::floor (phase));
            const uint32_t sd = seed + (uint32_t) phase;
            const float a = curve (l.shapeA, u + l.phaseA, sd), b = curve (l.shapeB, u + l.phaseB, sd);
            out = amp * (b + x * (a - b));        // xfade up = upper curve (A)
        }
        else if (type == mtStepper)
        {
            const auto& s = p.stepper[(size_t) idx];
            const int len = std::max (1, s.loopEnd - s.loopStart + 1);
            const double rel = std::fmod (std::max (0.0, phase - s.loopStart), (double) len);
            const int step = std::clamp (s.loopStart + (int) rel, 0, 15);
            const float target = s.step[(size_t) step];
            if (s.glideOn[(size_t) step] && lastStep >= 0)
            {
                const float tau = law::stepGlideTau (x);
                glided += (target - glided) * (1.0f - std::exp (-(float) n / (sr * tau)));
            }
            else glided = target;
            lastStep = step;
            out = amp * (s.ampOn[(size_t) step] ? ilv : 1.0f) * glided;
        }
        else
        {
            const auto& pf = p.performer[(size_t) idx];
            const int len = std::max (1, pf.loopEnd - pf.loopStart + 1);
            const double rel = std::fmod (std::max (0.0, phase - pf.loopStart), (double) len);
            const int step = std::clamp (pf.loopStart + (int) rel, 0, 15);
            const float frac = (float) (rel - std::floor (rel));
            const float a = pf.levelA[(size_t) step] * curves::perf (pf.curveA[(size_t) step], frac);
            const float b = pf.levelB[(size_t) step] * curves::perf (pf.curveB[(size_t) step], frac);
            const float v = pf.morphOn[(size_t) step] ? b + x * (a - b) : a;
            out = amp * (pf.ampOn[(size_t) step] ? ilv : 1.0f) * v;
        }
        return out;
    }
};
} // namespace massif
