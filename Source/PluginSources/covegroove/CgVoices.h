#pragma once
//==============================================================================
// Cove Groove - the eight drum voices. Every sound is synthesized; the shapes
// (pitch curves, burst timings, partials, decays, filter bands) were fitted to
// a set of hardgroove reference hits - only those numbers are here.
//
//   Kick       sine with a ~40 ms exponential pitch fall from ~6x down to the
//              tuned pitch, held at full level, then gated off fast; a short
//              band-passed noise click; tanh drive
//   Bass       the offbeat sub: sine / saw / pulse / rumble, a short pitch
//              blip, held for Decay, gated off
//   Clap       three noise bursts ~9.5 ms apart through a band-pass around
//              1.5 kHz, then a decaying tail (Snap: one burst; Room: a long
//              darker tail; Snare: a tonal body under the noise)
//   Closed Hat six band-limited squares at the analog box's inharmonic ratios plus
//   Open Hat   noise, through a band-pass and a high-pass; closed ~29 ms,
//              open held a little, then falling
//   Cymbal     Ride (metal + bell partials), Crash, Shaker, Reverse Crash
//   Perc       Conga, Tom, Bongo (a membrane: falling pitch, an overtone, a
//              slap of noise), Rim
//   FX         Zap (falling sine + noise), Stab (a gated saw fifth through a
//              low-pass, driven), Noise (falling band-passed noise), Blip
//
// Each lane has eight sounds: the four above (fitted to the references), then
// drum-machine and kit sounds made the same way, from what the classic boxes
// and a played kit do (no samples):
//   Kick       Analog (a long sine boom, barely falling), Hybrid (a fast fall, a
//              noisy attack), Kit (a felt beater on a damped head, an
//              overtone), Crunch (a Hybrid fall driven hard)
//   Bass       Analog (a gliding sine boom that rings), Acid (a saw through a
//              resonant low-pass with its own envelope), Reese (two detuned
//              saws and a sub), Fingered (a plucked string's partials)
//   Clap       Analog Clap, Hybrid Clap (four bursts, a band and a tail each), Hybrid
//              Snare (two falling tones and snappy noise), Kit Snare (a head's
//              modes, the stick, the wires)
//   Hats       Analog (the six squares through high bands), Hybrid (bright, noisy),
//              Kit (noise over high metal with the stick's chick), Pedal /
//              Half Open
//   Cymbal     Analog Cym, Hybrid Ride, Kit Ride (a ping and a bell over the wash),
//              Kit Crash
//   Perc       Cowbell (the analog box's two squares), Analog Tom, Clave, Kit Tom
//   FX         Tamb (jingles in a few quick bursts), Maracas, Riser (noise
//              sweeping up into the next bar), Metal (a struck bar's partials)
//
// Tune moves a voice in semitones, Decay scales its length (x0.25..x4), Tone
// moves its brightness (click, band, slap...). An accented step ("alt")
// plays the voice's second note where it has one: the conga's high note, the
// bass an octave up.
//==============================================================================
#include "CgDsp.h"
#include <memory>
#include <vector>
#include <string>
#include <cstdio>

namespace covegroove
{
    constexpr int kNumLanes = 8;
    constexpr int kClassicModels = 8;      // the synthesized sounds with an engine of their own per lane
    constexpr int kShapedModels = 2;       // two more per lane, from one shaped engine fitted to recorded hits
    constexpr int kSampleModel = kClassicModels + kShapedModels;   // the lane's own loaded sample
    constexpr int kNumModels = kSampleModel + 1;
    enum LaneType { Kick = 0, Bass, Clap, ClosedHat, OpenHat, Cymbal, Perc, Fx };

    inline const char* laneName (int lane)
    {
        static const char* n[kNumLanes] = { "Kick", "Bass", "Clap", "Closed Hat", "Open Hat", "Cymbal", "Perc", "FX" };
        return n[std::clamp (lane, 0, kNumLanes - 1)];
    }

    inline const char* modelName (int lane, int model)
    {
        static const char* n[kNumLanes][kNumModels] = {
            { "Hard", "Long", "Tight", "Round", "Analog", "Hybrid", "Kit", "Crunch", "Gated", "Drop", "Sample" },
            { "Sub", "Saw", "Pulse", "Rumble", "Analog", "Acid", "Reese", "Fingered", "Gate Rumble", "Verb Rumble", "Sample" },
            { "Clap", "Snap", "Room", "Snare", "Analog Clap", "Hybrid Clap", "Hybrid Snare", "Kit Snare", "Gate Clap", "Verb Clap", "Sample" },
            { "Metal", "Dark", "Noise", "Tick", "Analog", "Hybrid", "Kit", "Pedal", "Crisp", "Ring Tick", "Sample" },
            { "Metal", "Bright", "Noise", "Sizzle", "Analog", "Hybrid", "Kit", "Half Open", "Wash", "Ring", "Sample" },
            { "Ride", "Crash", "Shaker", "Rev Crash", "Analog Cym", "Hybrid Ride", "Kit Ride", "Kit Crash", "Dark Crash", "Swell", "Sample" },
            { "Conga", "Tom", "Bongo", "Rim", "Cowbell", "Analog Tom", "Clave", "Kit Tom", "Gate Tom", "Gate Hit", "Sample" },
            { "Zap", "Stab", "Noise", "Blip", "Tamb", "Maracas", "Riser", "Metal", "Gate FX", "Reverb", "Sample" } };
        return n[std::clamp (lane, 0, kNumLanes - 1)][std::clamp (model, 0, kNumModels - 1)];
    }

    // what a sound does in the pattern (the generator writes the cymbal and FX
    // lanes after it)
    enum Role { RoleRide, RoleCrash, RoleShaker, RoleSwell, RoleSweep, RoleStab, RoleBlip, RoleJingle };

    inline Role cymbalRole (int model)
    {
        static const Role r[kNumModels] = { RoleRide, RoleCrash, RoleShaker, RoleSwell, RoleRide, RoleRide, RoleRide, RoleCrash, RoleCrash, RoleSwell, RoleRide };
        return r[std::clamp (model, 0, kNumModels - 1)];
    }

    inline Role fxRole (int model)
    {
        static const Role r[kNumModels] = { RoleSweep, RoleStab, RoleSweep, RoleBlip, RoleJingle, RoleShaker, RoleSwell, RoleStab, RoleStab, RoleCrash, RoleStab };
        return r[std::clamp (model, 0, kNumModels - 1)];
    }

    // a lane's own sample: the frames (left, and right when it has one) and their rate
    struct SampleData
    {
        std::vector<float> left, right;
        double rate { 44100.0 };
        int frames() const { return (int) left.size(); }
        bool stereo() const { return ! right.empty(); }
    };

    // the volume curve over a sample: points from the start (x 0) to the end (x 1)
    // of what plays, their level 0..1, straight lines between them
    struct Curve
    {
        static constexpr int kMaxPoints = 16;
        int n { 2 };
        float x[kMaxPoints] { 0.0f, 1.0f };
        float y[kMaxPoints] { 1.0f, 1.0f };

        float at (float u) const
        {
            if (n <= 0) return 1.0f;
            if (u <= x[0]) return y[0];
            for (int i = 1; i < n; ++i)
                if (u <= x[i])
                {
                    const float d = x[i] - x[i - 1];
                    return d <= 1.0e-6f ? y[i] : y[i - 1] + (y[i] - y[i - 1]) * (u - x[i - 1]) / d;
                }
            return y[n - 1];
        }

        // the ends stay at 0 and 1, the points in order, the levels 0..1
        void tidy()
        {
            n = std::clamp (n, 2, kMaxPoints);
            for (int i = 0; i < n; ++i) { x[i] = clampf (x[i], 0.0f, 1.0f); y[i] = clampf (y[i], 0.0f, 1.0f); }
            std::sort (x, x + n);
            x[0] = 0.0f; x[n - 1] = 1.0f;
        }

        bool isFlat() const { return n == 2 && y[0] >= 0.999f && y[1] >= 0.999f; }

        // "x:y,x:y,..."
        std::string toString() const
        {
            std::string t;
            char b[48];
            for (int i = 0; i < n; ++i)
            {
                std::snprintf (b, sizeof (b), "%s%.4f:%.4f", i ? "," : "", (double) x[i], (double) y[i]);
                t += b;
            }
            return t;
        }

        bool fromString (const std::string& t)
        {
            Curve c; c.n = 0;
            size_t i = 0;
            while (i < t.size() && c.n < kMaxPoints)
            {
                float a = 0.0f, b = 0.0f;
                if (std::sscanf (t.c_str() + i, "%f:%f", &a, &b) != 2) break;
                c.x[c.n] = a; c.y[c.n] = b; ++c.n;
                const auto comma = t.find (',', i);
                if (comma == std::string::npos) break;
                i = comma + 1;
            }
            if (c.n < 2) return false;
            // the points as written, in order (tidy() would move a hand-made pair)
            c.n = std::min (c.n, kMaxPoints);
            c.tidy();
            *this = c;
            return true;
        }
    };

    // one lane's sound settings
    struct Sound
    {
        int model { 0 };
        float tune { 0.0f };        // semitones, -24..24
        float decay { 1.0f };       // length factor, 0.25..4
        float tone { 0.5f };        // 0..1
        std::shared_ptr<const SampleData> sample;   // for the Sample sound
        Curve curve;                                // its volume curve
    };

    // the analog and hybrid boxes' sounds: what one of them is made of (see DrumVoice's Box)
    struct BoxOsc   { float level, freq, wave, ratio, tauP, attack, tau, tau2, mix2; };   // wave 0 sine, 1 triangle, 2 square
    struct BoxBand  { float level, fc, q, tau, tau2, mix2; };
    struct BoxNoise { float level, fc, q, attack, tau, tau2, mix2; };
    struct BoxSpec
    {
        BoxOsc osc[2];                          // the tones: a pitch falling to its note, an attack, a two-stage decay
        float oscBpHz, oscBpQ;                  // a band-pass on the tones (0: none) - the cowbell's
        float metalScale, metalAttack, metalHpHz;   // six squares at the analog box's metal frequencies (x scale)
        BoxBand metal[2];                       // ... through two band-passes, each with its decay
        BoxNoise noise[2];                      // band-passed noise; a clap's first layer comes in bursts
        float bursts, burstGap, burstTail;
        float click, clickHz;                   // a short click at the blow
        float hpHz, lpHz, drive, gain, width;
        float gate, gateFade;                   // everything held, then cut in gateFade seconds (0: no gate) - the analog box's open hat
    };
    constexpr int kBoxFloats = (int) (sizeof (BoxSpec) / sizeof (float));

    // which built-in sound is a box sound: its index in the table, or -1
    inline int boxIndex (int lane, int model)
    {
        switch (lane)
        {
            case 0: return model == 4 ? 0 : (model == 5 ? 1 : -1);                 // Kick Analog, Hybrid
            case 1: return model == 4 ? 2 : -1;                                     // Bass Analog
            case 2: return model == 4 ? 3 : (model == 5 ? 4 : (model == 6 ? 5 : -1)); // Analog Clap, Hybrid Clap, Hybrid Snare
            case 3: return model == 4 ? 6 : (model == 5 ? 7 : -1);                 // Closed Hat Analog, Hybrid
            case 4: return model == 4 ? 8 : (model == 5 ? 9 : -1);                 // Open Hat Analog, Hybrid
            case 5: return model == 4 ? 10 : (model == 5 ? 11 : -1);               // Analog Cym, Hybrid Ride
            case 6: return model == 4 ? 12 : (model == 5 ? 13 : (model == 6 ? 14 : -1)); // Cowbell, Analog Tom, Clave
            case 7: return model == 5 ? 15 : -1;                                    // Maracas
            default: return -1;
        }
    }
    constexpr int kNumBoxSpecs = 16;
    const BoxSpec& boxSpec (int index);         // the fitted table (below the voice)

    // one hit
    class DrumVoice
    {
    public:
        void prepare (float sampleRate) { sr = sampleRate; active = false; }

        bool isActive() const { return active; }
        int laneType() const { return lane; }

        // a short fade instead of a click (re-struck mono voices, the open hat choked)
        void fadeOut (float seconds)
        {
            if (! active) return;
            const float n = std::max (1.0f, seconds * sr);
            if (fadeStep <= 0.0f || 1.0f / n > fadeStep) fadeStep = 1.0f / n;
        }

        void start (int laneType, const Sound& s, float velocity, bool alt, uint32_t seed)
        {
            lane = std::clamp (laneType, 0, kNumLanes - 1);
            model = std::clamp (s.model, 0, kNumModels - 1);
            n = 0;
            active = true;
            fade = 1.0f; fadeStep = 0.0f;
            rngL = Rng (seed * 2654435761u + 17u);
            rngR = Rng (seed * 2246822519u + 91u);
            const float v = clampf (velocity, 0.0f, 1.0f);
            gain = 0.12f + 0.88f * v * v * (2.0f - v);   // a soft knee
            tuneRatio = std::pow (2.0f, clampf (s.tune, -36.0f, 36.0f) / 12.0f);
            decay = clampf (s.decay, 0.1f, 6.0f);
            tone = clampf (s.tone, 0.0f, 1.0f);
            for (auto& o : osc) o.phase = 0.0f;
            for (auto& f : flt) f.reset();
            phase = phase2 = phase3 = 0.0f;
            dc = 0.0f;
            env1 = env2 = env3 = 1.0f;

            kind = model == kSampleModel ? Kind::Sampled : (model >= kClassicModels ? Kind::Shaped : Kind::Classic);
            if (kind == Kind::Classic && boxIndex (lane, model) >= 0) kind = Kind::Box;
            if (kind == Kind::Shaped) { startShaped (model - kClassicModels); return; }
            if (kind == Kind::Box) { startBox (boxSpec (boxIndex (lane, model)), alt); return; }
            if (kind == Kind::Sampled) { startSample (s, alt); return; }
            smp.reset();

            switch (lane)
            {
                case Kick:      startKick(); break;
                case Bass:      startBass (alt); break;
                case Clap:      startClap(); break;
                case ClosedHat: startHat (false); break;
                case OpenHat:   startHat (true); break;
                case Cymbal:    startCymbal(); break;
                case Perc:      startPerc (alt); break;
                default:        startFx(); break;
            }
        }

        // one sample, stereo (not added: written)
        void render (float& outL, float& outR)
        {
            outL = outR = 0.0f;
            if (! active) return;
            const float t = (float) n / sr;
            float l = 0.0f, r = 0.0f;
            if (kind == Kind::Shaped) renderShaped (t, l, r);
            else if (kind == Kind::Box) renderBox (t, l, r);
            else if (kind == Kind::Sampled) renderSample (l, r);
            else switch (lane)
            {
                case Kick:      renderKick (t, l, r); break;
                case Bass:      renderBass (t, l, r); break;
                case Clap:      renderClap (t, l, r); break;
                case ClosedHat:
                case OpenHat:   renderHat (t, l, r); break;
                case Cymbal:    renderCymbal (t, l, r); break;
                case Perc:      renderPerc (t, l, r); break;
                default:        renderFx (t, l, r); break;
            }
            ++n;
            if (fadeStep > 0.0f)
            {
                fade -= fadeStep;
                if (fade <= 0.0f) { active = false; return; }
            }
            outL = l * gain * fade;
            outR = r * gain * fade;
            if (t > maxLength) active = false;
        }

    private:
        //=== Kick ============================================================
        float fEnd {}, ratio {}, pitchK {}, hold {}, sagK {}, relK {}, clickAmt {}, topAmt {}, drive {}, driveNorm {}, overAmt {}, kickGain {};

        void startKick()
        {
            struct M { float fEnd, ratio, tauP, hold, rel, click, drive, clickHz, top, over; };
            static const M m[kClassicModels] = {
                { 50.0f, 6.0f, 0.040f, 0.200f, 0.020f, 0.45f, 3.2f, 2600.0f, 0.30f, 0.0f },   // Hard: the references
                { 46.0f, 5.2f, 0.052f, 0.330f, 0.140f, 0.30f, 2.0f, 2600.0f, 0.30f, 0.0f },   // Long
                { 56.0f, 7.0f, 0.024f, 0.100f, 0.030f, 0.60f, 3.0f, 2600.0f, 0.30f, 0.0f },   // Tight
                { 48.0f, 3.2f, 0.030f, 0.000f, 0.450f, 0.15f, 1.0f, 2600.0f, 0.00f, 0.0f },   // Round: an Analog-ish fall
                { 49.0f, 1.8f, 0.020f, 0.000f, 1.100f, 0.10f, 0.6f, 1500.0f, 0.00f, 0.0f },   // Analog: a long boom, barely falling
                { 53.0f, 4.5f, 0.032f, 0.000f, 0.450f, 0.60f, 1.8f, 3500.0f, 0.35f, 0.0f },   // Hybrid: a fast fall, a noisy attack
                { 60.0f, 1.4f, 0.012f, 0.000f, 0.300f, 0.70f, 0.9f, 1800.0f, 0.10f, 0.4f },   // Kit: felt on a damped head
                { 52.0f, 5.0f, 0.034f, 0.120f, 0.250f, 0.65f, 7.0f, 3200.0f, 0.35f, 0.0f } }; // Crunch: a Hybrid driven hard
            const auto& k = m[model];
            fEnd = k.fEnd * tuneRatio;
            ratio = k.ratio;
            pitchK = std::exp (-1.0f / (k.tauP * sr));
            hold = k.hold * decay;
            sagK = std::exp (-1.0f / (std::max (0.05f, hold * 4.0f) * sr));
            relK = std::exp (-6.9f / (std::max (0.004f, k.rel * (hold > 0.0f ? 1.0f : decay)) * sr));
            clickAmt = k.click * (0.3f + 1.4f * tone);
            drive = k.drive * (0.6f + 1.2f * tone);
            driveNorm = 1.0f / std::tanh (drive);
            flt[0].set (sr, k.clickHz, 0.8f);
            flt[1].set (sr, 3000.0f, 0.5f);         // the top: noise riding the first 50 ms
            topAmt = k.top * (0.4f + 1.2f * tone);
            overAmt = k.over * (0.5f + tone);
            kickGain = model == 6 ? 1.5f : 1.0f;
            maxLength = hold + k.rel * 3.0f * decay + 0.05f;
        }

        void renderKick (float t, float& l, float& r)
        {
            env1 *= pitchK;                                   // the pitch curve
            const float f = fEnd * (1.0f + (ratio - 1.0f) * env1);
            phase += f / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            float a = std::min (1.0f, t / 0.0004f);
            if (t < hold) env2 *= sagK; else env2 *= relK;
            a *= env2;
            const float nz = rngL.next();
            const float click = flt[0].process (nz) * std::exp (-t / 0.003f);
            flt[1].process (nz);
            const float top = flt[1].bp * std::exp (-t / 0.06f) * topAmt * 1.6f;
            float x = std::sin (kTwoPi * phase) * a + click * clickAmt + top * a;
            if (overAmt > 0.0f)
            {
                // the head's second mode, gone in a few tens of ms
                phase2 += 1.59f * f / sr; if (phase2 >= 1.0f) phase2 -= 1.0f;
                x += overAmt * std::sin (kTwoPi * phase2) * std::exp (-t / 0.035f) * std::min (1.0f, t / 0.0004f);
            }
            const float y = std::tanh (drive * x) * driveNorm * 0.92f * kickGain;
            l = r = y;
            if (t > hold && env2 < 1.0e-4f) active = false;
        }

        //=== Bass ============================================================
        float freq {}, blipK {}, attackK {}, releaseK2 {}, holdB {}, blipAmt {}, ringK {}, envF {}, envFK {}, cutBase {}, cutEnv {};
        int ctl { 0 };
        float hAmp[6] {}, hMul[6] {};

        void startBass (bool alt)
        {
            freq = 55.0f * tuneRatio * (alt ? 2.0f : 1.0f);
            blipK = std::exp (-1.0f / (0.008f * sr));
            blipAmt = 0.18f;
            holdB = 0.14f * decay;
            releaseK2 = std::exp (-6.9f / (0.025f * sr));
            flt[0].set (sr, 180.0f + tone * tone * 3200.0f, 1.3f);    // saw / pulse low-pass
            flt[1].set (sr, 90.0f, 0.7f);                              // rumble noise
            drive = 1.0f + tone * 2.5f;
            driveNorm = 1.0f / std::tanh (drive);
            maxLength = holdB + 0.2f;
            ctl = 0;
            switch (model)
            {
                case 4:     // Analog: a gliding boom that rings out
                    blipK = std::exp (-1.0f / (0.03f * sr));
                    blipAmt = 0.55f;
                    holdB = 0.0f;
                    ringK = std::exp (-6.9f / (0.9f * decay * sr));
                    drive = 0.8f + tone * 2.0f;
                    driveNorm = 1.0f / std::tanh (drive);
                    maxLength = 0.9f * decay + 0.05f;
                    break;
                case 5:     // Acid: a resonant low-pass with its own envelope
                    cutBase = 90.0f * std::pow (2.0f, tone * 4.0f);
                    cutEnv = alt ? 3800.0f : 2600.0f;
                    envF = 1.0f;
                    envFK = std::exp (-1.0f / (0.09f * decay * sr));
                    drive = 1.6f + tone * 1.5f;
                    driveNorm = 1.0f / std::tanh (drive);
                    break;
                case 6:     // Reese: two saws a little apart, a sub under them
                    flt[0].set (sr, 300.0f * std::pow (2.0f, tone * 3.0f), 0.8f);
                    break;
                case 7:     // Fingered: a plucked string's partials, ringing for a while
                    holdB = 0.35f * decay;
                    releaseK2 = std::exp (-6.9f / (0.05f * sr));
                    flt[1].set (sr, 700.0f, 0.7f);                     // the finger's thump
                    for (int h = 0; h < 6; ++h)
                    {
                        hAmp[h] = std::pow (0.35f + 0.4f * tone, (float) h);
                        hMul[h] = std::exp (-(1.2f + 0.9f * (float) ((h + 1) * (h + 1))) / (decay * sr));
                    }
                    drive = 1.0f + tone;
                    driveNorm = 1.0f / std::tanh (drive);
                    maxLength = holdB + 0.3f;
                    break;
                default: break;
            }
        }

        void renderBass (float t, float& l, float& r)
        {
            env1 *= blipK;
            const float f = freq * (1.0f + blipAmt * env1);
            const float inc = f / sr;
            float s = 0.0f;
            switch (model)
            {
                case 0:  phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                         s = std::sin (kTwoPi * phase) + 0.3f * tone * std::sin (2.0f * kTwoPi * phase); break;
                case 1:  flt[0].process (osc[0].saw (inc)); s = flt[0].lp * 1.4f; break;
                case 2:  flt[0].process (osc[0].square (inc)); s = flt[0].lp * 1.2f; break;
                case 3:  phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                         s = std::sin (kTwoPi * phase) + 2.5f * flt[1].process (rngL.next()) * (0.4f + tone); break;
                case 4:  phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                         env3 *= ringK;
                         s = std::sin (kTwoPi * phase) * env3; break;
                case 5:
                {
                    envF *= envFK;
                    if ((ctl++ & 7) == 0) flt[0].set (sr, cutBase + cutEnv * envF, 5.0f);
                    flt[0].process (osc[0].saw (inc));
                    s = flt[0].lp * 1.1f;
                    break;
                }
                case 6:
                {
                    const float x = osc[0].saw (inc * 1.007f) + osc[1].saw (inc * 0.993f);
                    flt[0].process (x);
                    phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                    s = flt[0].lp * 1.1f + 0.8f * std::sin (kTwoPi * phase);
                    break;
                }
                default:
                {
                    // partials 1..6, the higher ones dying faster; Tone is the pluck's brightness
                    phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                    const float w = kTwoPi * phase;
                    for (int h = 0; h < 6; ++h)
                    {
                        s += hAmp[h] * std::sin (w * (float) (h + 1));
                        hAmp[h] *= hMul[h];
                    }
                    s += 0.8f * flt[1].process (rngL.next()) * std::exp (-t / 0.006f);
                    break;
                }
            }
            float a = std::min (1.0f, t / 0.002f);
            if (model != 4 && t >= holdB) env2 *= releaseK2;
            a *= env2;
            const float y = std::tanh (drive * s * a) * driveNorm * 0.8f;
            l = r = y;
            if (model == 4 ? (env3 < 1.0e-4f) : (t > holdB && env2 < 1.0e-4f)) active = false;
        }

        //=== Clap ============================================================
        int bursts {};
        float burstGap {}, tailK {}, tailLevel {}, bodyFreq {}, bodyLevel {};

        void startClap()
        {
            if (model >= 6) { startSnare(); return; }
            struct M { int bursts; float gap, fc, q, tail, tailLevel, tailBand, tailQ, body; };
            static const M m[6] = {
                { 3, 0.0095f, 1550.0f, 1.1f, 0.13f, 0.50f, 1.0f, 1.1f, 0.0f },     // Clap: the references
                { 1, 0.0095f, 1550.0f, 1.0f, 0.06f, 0.50f, 1.0f, 1.0f, 0.0f },     // Snap
                { 3, 0.0095f, 1550.0f, 1.1f, 0.26f, 0.60f, 0.7f, 0.8f, 0.0f },     // Room
                { 3, 0.0095f, 3410.0f, 1.1f, 0.11f, 0.50f, 1.0f, 1.1f, 0.7f },     // Snare: a tonal body under the noise
                { 4, 0.0085f, 1000.0f, 1.6f, 0.15f, 0.65f, 0.9f, 1.0f, 0.0f },     // Analog Clap
                { 4, 0.0105f, 1250.0f, 1.0f, 0.21f, 0.75f, 1.2f, 0.7f, 0.0f } };   // Hybrid Clap
            const auto& k = m[model];
            const float bandHz = k.fc * tuneRatio * std::pow (2.0f, (tone - 0.5f) * 1.6f);
            bursts = k.bursts;
            burstGap = k.gap;
            tailLevel = k.tailLevel;
            bodyLevel = k.body;
            flt[0].set (sr, bandHz, k.q);
            flt[1].set (sr, bandHz, k.q);
            flt[2].set (sr, bandHz * k.tailBand, k.tailQ);   // the tail's own band
            flt[3].set (sr, bandHz * k.tailBand, k.tailQ);
            tailK = std::exp (-1.0f / (k.tail * decay * sr));
            bodyFreq = 185.0f * tuneRatio;
            maxLength = 0.03f + k.tail * decay * 7.0f;
        }

        void renderClap (float t, float& l, float& r)
        {
            if (model >= 6) { renderSnare (t, l, r); return; }
            const float c = rngL.next(), d = rngR.next();
            const float nl = 0.9f * c + 0.25f * d, nr = 0.9f * c - 0.25f * d;   // partly decorrelated
            float burst = 0.0f;
            for (int b = 0; b < bursts; ++b)
            {
                const float tb = t - (float) b * burstGap;
                if (tb >= 0.0f) burst += (b == 0 ? 1.6f : 1.25f) * std::exp (-tb / 0.0028f);
            }
            const float tailStart = (float) (bursts - 1) * burstGap + 0.002f;
            float tailEnv = 0.0f;
            if (t >= tailStart) { env1 *= tailK; tailEnv = tailLevel * env1 * std::min (1.0f, (t - tailStart) / 0.002f); }
            float yl = flt[0].process (nl) * burst + flt[2].process (nl) * tailEnv;
            float yr = flt[1].process (nr) * burst + flt[3].process (nr) * tailEnv;
            if (bodyLevel > 0.0f)
            {
                const float f = bodyFreq * (1.0f + 0.5f * std::exp (-t / 0.006f));
                phase += f / sr; if (phase >= 1.0f) phase -= 1.0f;
                const float b = bodyLevel * std::sin (kTwoPi * phase) * std::exp (-t / (0.05f * decay));
                yl += b; yr += b;
            }
            // a little of the room under the band
            room.setLowpass (sr, 600.0f);
            const float lowRoom = room.lowpass (c) * 0.4f * (burst * 0.5f + tailEnv);
            l = (yl + lowRoom) * 3.1f; r = (yr + lowRoom) * 3.1f;
            if (t > tailStart + 0.01f && env1 < 2.0e-4f && t > 0.05f) active = false;
        }

        //=== Snares (the clap lane's Hybrid Snare and Kit Snare) ================
        float snareF {}, wireK {}, wireLevel {}, bodyK1 {}, bodyK2 {};
        float modeAmp[4] {}, modeMul[4] {}, modeRatio[4] {}, modePh[4] {};

        void startSnare()
        {
            const float tr = tuneRatio;
            if (model == 6)
            {
                // Hybrid: two tones falling a little into place, "snappy" noise high-passed
                snareF = 180.0f * tr;
                bodyK1 = std::exp (-1.0f / (0.055f * decay * sr));
                bodyK2 = std::exp (-1.0f / (0.035f * decay * sr));
                wireK = std::exp (-1.0f / (0.07f * decay * sr));
                wireLevel = 0.5f + 0.9f * tone;
                flt[0].set (sr, 6500.0f, 0.6f); flt[1].set (sr, 6500.0f, 0.6f);
                flt[2].set (sr, 2200.0f, 0.7f); flt[3].set (sr, 2200.0f, 0.7f);
                maxLength = 0.6f * decay + 0.05f;
            }
            else
            {
                // a played snare: the head's modes, the stick, the wires buzzing after
                snareF = 195.0f * tr;
                static const float ratio[4] = { 1.0f, 1.59f, 2.14f, 2.65f };
                static const float amp[4] = { 1.0f, 0.55f, 0.35f, 0.25f };
                static const float tau[4] = { 0.16f, 0.07f, 0.05f, 0.035f };
                for (int i = 0; i < 4; ++i)
                {
                    modeAmp[i] = amp[i];
                    modeMul[i] = std::exp (-1.0f / (tau[i] * decay * sr));
                    modeRatio[i] = ratio[i];
                    modePh[i] = 0.0f;
                }
                wireK = std::exp (-1.0f / (0.16f * decay * sr));
                wireLevel = 0.6f + 1.0f * tone;
                flt[0].set (sr, 5200.0f, 0.5f); flt[1].set (sr, 5200.0f, 0.5f);
                flt[2].set (sr, 1800.0f, 0.7f); flt[3].set (sr, 1800.0f, 0.7f);
                maxLength = 1.1f * decay + 0.05f;
            }
            env2 = env3 = 1.0f;
        }

        void renderSnare (float t, float& l, float& r)
        {
            const float c = rngL.next(), d = rngR.next();
            const float nl = 0.85f * c + 0.3f * d, nr = 0.85f * c - 0.3f * d;
            float body = 0.0f;
            if (model == 6)
            {
                const float fall = 1.0f + 0.15f * std::exp (-t / 0.01f);
                phase += snareF * fall / sr; if (phase >= 1.0f) phase -= 1.0f;
                phase2 += snareF * 1.83f * fall / sr; if (phase2 >= 1.0f) phase2 -= 1.0f;
                env2 *= bodyK1; env3 *= bodyK2;
                body = (std::sin (kTwoPi * phase) * env2 + 0.6f * std::sin (kTwoPi * phase2) * env3) * (1.3f - 0.6f * tone);
            }
            else
            {
                const float fall = 1.0f + 0.08f * std::exp (-t / 0.008f);
                for (int i = 0; i < 4; ++i)
                {
                    float& ph = modePh[i];
                    ph += snareF * modeRatio[i] * fall / sr; if (ph >= 1.0f) ph -= 1.0f;
                    body += modeAmp[i] * std::sin (kTwoPi * ph);
                    modeAmp[i] *= modeMul[i];
                }
                body *= 0.9f;
            }
            env1 *= wireK;
            const float a = std::min (1.0f, t / 0.0007f);
            // the wires: high, then a lower buzz; the stick: a short bright crack
            const float wl = (flt[0].process (nl) * 0.8f + flt[2].process (nl) * 0.5f) * env1 * wireLevel;
            const float wr = (flt[1].process (nr) * 0.8f + flt[3].process (nr) * 0.5f) * env1 * wireLevel;
            const float stick = (model == 7 ? 1.2f : 0.6f) * c * std::exp (-t / 0.0025f);
            l = (body + wl + stick) * a * 1.25f;
            r = (body + wr + stick) * a * 1.25f;
            if (t > 0.02f && env1 < 1.0e-4f && (model == 7 ? modeAmp[0] : env2) < 1.0e-4f) active = false;
        }

        //=== Hats ============================================================
        float metalScale {}, noiseMix {}, hatHold {}, hatK {}, sizzle {}, hatGate {}, stickAmt {}, sizzleHz {}, hatGain {};
        OnePole room;

        void startHat (bool open)
        {
            struct M { float bp, hp, q, noise, hold, tau, gate, metal, sizzle, sizzleHz, stick, gain; };
            static const M closed[kClassicModels] = {
                { 2900.0f, 1300.0f, 0.8f, 0.35f, 0.0f, 0.029f, 0.0f, 1.0f, 0.0f, 31.0f, 0.0f, 1.0f },    // Metal: the references
                { 2400.0f,  900.0f, 0.8f, 0.35f, 0.0f, 0.029f, 0.0f, 1.0f, 0.0f, 31.0f, 0.0f, 1.0f },    // Dark
                { 7000.0f, 3000.0f, 0.7f, 1.00f, 0.0f, 0.029f, 0.0f, 1.0f, 0.0f, 31.0f, 0.0f, 1.0f },    // Noise
                { 9000.0f, 5000.0f, 0.8f, 0.35f, 0.0f, 0.012f, 0.0f, 1.6f, 0.0f, 31.0f, 0.0f, 1.0f },    // Tick
                { 7100.0f, 5800.0f, 1.2f, 0.08f, 0.0f, 0.040f, 0.0f, 1.0f, 0.0f, 31.0f, 0.0f, 1.3f },    // Analog: the six squares, high bands
                { 10000.0f, 7000.0f, 0.6f, 0.75f, 0.0f, 0.032f, 0.0f, 2.2f, 0.0f, 31.0f, 0.0f, 0.75f },  // Hybrid: bright and noisy
                { 8500.0f, 4000.0f, 0.5f, 0.85f, 0.0f, 0.045f, 0.0f, 3.3f, 0.0f, 31.0f, 1.0f, 0.65f },   // Kit: the stick's chick on top
                { 6000.0f, 2500.0f, 0.6f, 0.70f, 0.0f, 0.020f, 0.0f, 3.0f, 0.0f, 31.0f, 0.6f, 0.7f } };  // Pedal: the foot's chick
            static const M opened[kClassicModels] = {
                { 7000.0f, 2200.0f, 0.6f, 0.30f, 0.035f, 0.045f, 0.19f, 1.0f, 0.0f, 31.0f, 0.0f, 1.0f }, // Metal: the references
                { 10500.0f, 5000.0f, 0.6f, 0.30f, 0.035f, 0.045f, 0.19f, 1.3f, 0.0f, 31.0f, 0.0f, 1.0f },// Bright
                { 7000.0f, 2200.0f, 0.6f, 1.00f, 0.035f, 0.045f, 0.19f, 1.0f, 0.0f, 31.0f, 0.0f, 1.0f }, // Noise
                { 7000.0f, 2200.0f, 0.6f, 0.60f, 0.050f, 0.110f, 0.0f, 1.0f, 1.0f, 31.0f, 0.0f, 1.0f },  // Sizzle
                { 7100.0f, 5800.0f, 1.2f, 0.08f, 0.000f, 0.280f, 0.0f, 1.0f, 0.0f, 31.0f, 0.0f, 1.3f },  // Analog
                { 10000.0f, 7000.0f, 0.6f, 0.70f, 0.020f, 0.160f, 0.0f, 2.2f, 0.0f, 31.0f, 0.0f, 0.75f },// Hybrid
                { 8000.0f, 3500.0f, 0.45f, 0.80f, 0.050f, 0.400f, 0.0f, 3.3f, 0.0f, 31.0f, 0.8f, 0.6f }, // Kit
                { 7500.0f, 3500.0f, 0.5f, 0.80f, 0.010f, 0.160f, 0.0f, 3.0f, 2.0f, 18.0f, 0.6f, 1.0f } };// Half Open: the plates rattle
            const auto& k = open ? opened[model] : closed[model];
            noiseMix = k.noise; hatHold = k.hold; sizzle = k.sizzle; sizzleHz = k.sizzleHz; hatGate = k.gate;
            metalScale = k.metal; stickAmt = k.stick * (0.5f + tone); hatGain = k.gain;
            const float tr = std::pow (2.0f, (tone - 0.5f) * 1.4f);
            flt[0].set (sr, k.bp * tr * tuneRatio, k.q);
            flt[1].set (sr, k.bp * tr * tuneRatio, k.q);
            flt[2].set (sr, k.hp * tr * tuneRatio, 0.7f);
            flt[3].set (sr, k.hp * tr * tuneRatio, 0.7f);
            room.setLowpass (sr, 900.0f);
            hatHold *= decay;
            hatGate *= decay;
            hatK = std::exp (-1.0f / (k.tau * decay * sr));
            metalScale *= tuneRatio;
            maxLength = hatHold + k.tau * decay * 9.0f + 0.01f;
        }

        float metal()
        {
            static const float f[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            float m = 0.0f;
            for (int i = 0; i < 6; ++i)
                m += osc[i].square (std::min (0.45f, f[i] * metalScale / sr));
            return m * (1.0f / 6.0f);
        }

        void renderHat (float t, float& l, float& r)
        {
            const float m = metal();
            const float c = rngL.next(), d = rngR.next();
            const float nl = 0.8f * c + 0.35f * d, nr = 0.8f * c - 0.35f * d;
            const float xl = (1.0f - noiseMix) * m + noiseMix * nl;
            const float xr = (1.0f - noiseMix) * m + noiseMix * nr;
            flt[0].process (xl); flt[1].process (xr);
            flt[2].process (flt[0].bp); flt[3].process (flt[1].bp);
            float a = std::min (1.0f, t / (lane == OpenHat ? 0.001f : 0.0005f));
            if (t >= hatHold) env1 *= hatK;
            if (sizzle > 1.5f) a *= 0.7f + 0.3f * std::sin (kTwoPi * sizzleHz * t);
            else if (sizzle > 0.0f) a *= 0.85f + 0.15f * std::sin (kTwoPi * sizzleHz * t);
            if (hatGate > 0.0f && t > hatGate) a *= std::max (0.0f, 1.0f - (t - hatGate) / 0.012f);   // the open hat's gate
            a *= env1;
            l = flt[2].hp * a * 5.2f * hatGain;
            r = flt[3].hp * a * 5.2f * hatGain;
            if (stickAmt > 0.0f && t < 0.03f)
            {
                // the stick (or the foot): a short, mid-high chick before the wash
                const float st = room.highpass (c) * std::exp (-t / 0.004f) * stickAmt * 0.6f;
                l += st; r += st;
            }
            if (t > hatHold && env1 < 1.0e-4f) active = false;
        }

        //=== Cymbal ==========================================================
        float washK {}, bellK {}, revLength {}, bellF1 {}, bellF2 {}, bellLevel {}, swell {}, cymGain {};

        void startCymbal()
        {
            struct M { float bp, hp, q, tau, metal, noise, bellF1, bellF2, bell, bellTau, swell; };
            static const M m[kClassicModels] = {
                { 5200.0f, 2500.0f, 0.6f, 0.45f, 1.9f, 0.25f, 2780.0f, 4010.0f, 0.16f, 0.35f, 0.0f },   // Ride: metal + bell
                { 6000.0f, 1500.0f, 0.4f, 0.33f, 2.0f, 0.40f, 0.0f, 0.0f, 0.0f, 0.35f, 1.0f },          // Crash
                { 7500.0f, 4000.0f, 1.4f, 0.04f, 1.9f, 1.00f, 0.0f, 0.0f, 0.0f, 0.35f, 0.0f },          // Shaker
                { 6500.0f, 2200.0f, 0.4f, 0.25f, 2.0f, 0.45f, 0.0f, 0.0f, 0.0f, 0.35f, 0.0f },          // Rev Crash
                { 5000.0f, 3500.0f, 0.5f, 0.35f, 1.0f, 0.10f, 0.0f, 0.0f, 0.0f, 0.35f, 0.0f },          // Analog Cym: the six squares, ringing
                { 8000.0f, 4500.0f, 0.7f, 0.55f, 2.6f, 0.35f, 3200.0f, 5100.0f, 0.10f, 0.25f, 0.0f },   // Hybrid Ride: bright, a ping
                { 6000.0f, 3000.0f, 0.5f, 0.70f, 2.4f, 0.40f, 2400.0f, 3650.0f, 0.12f, 0.70f, 0.5f },   // Kit Ride: ping and bell over the wash
                { 5000.0f, 1800.0f, 0.35f, 0.80f, 2.8f, 0.60f, 0.0f, 0.0f, 0.0f, 0.35f, 1.0f } };       // Kit Crash
            const auto& k = m[model];
            const float tr = std::pow (2.0f, (tone - 0.5f) * 1.2f) * tuneRatio;
            metalScale = k.metal * tuneRatio; noiseMix = k.noise;
            revLength = model == 3 ? 0.65f * decay : 0.0f;
            flt[0].set (sr, k.bp * tr, k.q); flt[1].set (sr, k.bp * tr, k.q);
            flt[2].set (sr, k.hp * tr, 0.7f); flt[3].set (sr, k.hp * tr, 0.7f);
            washK = std::exp (-1.0f / (k.tau * decay * sr));
            bellK = std::exp (-1.0f / (k.bellTau * decay * sr));
            bellF1 = k.bellF1; bellF2 = k.bellF2; bellLevel = k.bell; swell = k.swell;
            cymGain = model == 4 ? 1.6f : 1.0f;
            maxLength = revLength + k.tau * decay * 8.0f + 0.02f;
        }

        void renderCymbal (float t, float& l, float& r)
        {
            const float m = model == 2 ? 0.0f : metal();
            const float c = rngL.next(), d = rngR.next();
            const float nl = 0.85f * c + 0.3f * d, nr = 0.85f * c - 0.3f * d;
            const float xl = (1.0f - noiseMix) * m + noiseMix * nl;
            const float xr = (1.0f - noiseMix) * m + noiseMix * nr;
            flt[0].process (xl); flt[1].process (xr);
            flt[2].process (flt[0].bp); flt[3].process (flt[1].bp);
            float a;
            if (model == 3)
            {
                // reversed: swells up to the end of its length, then dies away
                if (t < revLength) a = std::exp ((t - revLength) / 0.16f);
                else { env1 *= washK; a = env1; if (env1 < 1.0e-4f) active = false; }
            }
            else if (model == 2)
            {
                env1 *= washK;
                a = std::min (1.0f, t / 0.008f) * env1;
            }
            else
            {
                env1 *= washK;
                // a crash blooms: a hit, then the wash rising for a few tens of ms
                const float bloom = swell > 0.0f ? (0.7f * std::exp (-t / 0.01f) + (1.0f - std::exp (-t / (0.03f * swell)))) : std::min (1.0f, t / 0.0008f);
                a = bloom * env1;
            }
            l = flt[2].hp * a * 5.6f * cymGain;
            r = flt[3].hp * a * 5.6f * cymGain;
            if (bellLevel > 0.0f)
            {
                // the bell (the stick's ping)
                env2 *= bellK;
                const float tr = tuneRatio * std::pow (2.0f, (tone - 0.5f) * 0.5f);
                phase += bellF1 * tr / sr; if (phase >= 1.0f) phase -= 1.0f;
                phase2 += bellF2 * tr / sr; if (phase2 >= 1.0f) phase2 -= 1.0f;
                const float b = (std::sin (kTwoPi * phase) + 0.6f * std::sin (kTwoPi * phase2)) * bellLevel * env2 * std::min (1.0f, t / 0.0005f);
                l += b; r += b;
            }
            if (model != 3 && t > 0.01f && env1 < 1.0e-4f) active = false;
        }

        //=== Perc ============================================================
        float percFreq {}, blipRatio {}, overtone {}, overK {}, slapAmt {}, slapTau {}, percK {}, overRatio {};

        void startPerc (bool alt)
        {
            if (model == 4) { startCowbell (alt); return; }
            struct M { float f, blip, blipTau, tau, over, slapHz, slap, slapTau, slapQ, overRatio, overTau; };
            static const M m[kClassicModels] = {
                { 196.0f, 1.40f, 0.010f, 0.11f, 0.35f, 2200.0f, 0.55f, 0.004f, 1.2f, 1.59f, 0.03f },   // Conga
                { 118.0f, 1.60f, 0.020f, 0.24f, 0.25f, 1500.0f, 0.35f, 0.005f, 1.2f, 1.59f, 0.03f },   // Tom
                { 392.0f, 1.25f, 0.006f, 0.07f, 0.30f, 3500.0f, 0.60f, 0.003f, 1.2f, 1.59f, 0.03f },   // Bongo
                { 820.0f, 1.00f, 0.005f, 0.012f, 0.0f, 1700.0f, 1.20f, 0.006f, 6.0f, 1.59f, 0.03f },   // Rim
                { 540.0f, 1.00f, 0.005f, 0.10f, 0.0f, 2000.0f, 0.00f, 0.004f, 1.2f, 1.59f, 0.03f },    // (Cowbell: its own)
                { 130.0f, 1.25f, 0.060f, 0.22f, 0.05f, 600.0f, 0.25f, 0.010f, 0.8f, 1.59f, 0.03f },    // Analog Tom: a gliding sine
                { 2500.0f, 1.00f, 0.005f, 0.025f, 0.0f, 2500.0f, 0.10f, 0.002f, 2.0f, 1.59f, 0.03f },  // Clave
                { 105.0f, 1.35f, 0.030f, 0.30f, 0.45f, 3000.0f, 0.45f, 0.004f, 1.2f, 1.50f, 0.08f } }; // Kit Tom: a struck head
            const auto& k = m[model];
            percFreq = k.f * tuneRatio * (alt ? 1.335f : 1.0f);
            blipRatio = k.blip;
            blipK = std::exp (-1.0f / (k.blipTau * sr));
            percK = std::exp (-1.0f / (k.tau * decay * sr));
            overtone = k.over;
            overRatio = k.overRatio;
            overK = std::exp (-1.0f / (k.overTau * sr));
            slapAmt = k.slap * (0.3f + 1.4f * tone);
            slapTau = k.slapTau;
            flt[0].set (sr, k.slapHz * std::pow (2.0f, (tone - 0.5f)), k.slapQ);
            maxLength = k.tau * decay * 8.0f + 0.02f;
        }

        void renderPerc (float t, float& l, float& r)
        {
            if (model == 4) { renderCowbell (t, l, r); return; }
            env1 *= blipK;
            const float f = percFreq * (1.0f + (blipRatio - 1.0f) * env1);
            phase += f / sr; if (phase >= 1.0f) phase -= 1.0f;
            phase2 += overRatio * f / sr; if (phase2 >= 1.0f) phase2 -= 1.0f;
            env2 *= percK;
            env3 *= overK;   // the overtone dies first
            const float a = std::min (1.0f, t / 0.0006f);
            float s = std::sin (kTwoPi * phase) * env2 + overtone * std::sin (kTwoPi * phase2) * env3 * env2;
            s += flt[0].process (rngL.next()) * std::exp (-t / slapTau) * slapAmt;
            l = r = s * a * 0.85f;
            if (t > 0.01f && env2 < 1.0e-4f) active = false;
        }

        // the analog box's cowbell: two squares a fifth-ish apart through a band, a
        // fast first drop and a longer ring
        float bellA {}, bellB {};

        void startCowbell (bool alt)
        {
            percFreq = 540.0f * tuneRatio * (alt ? 1.335f : 1.0f);
            const float bandHz = 2000.0f * std::pow (2.0f, tone - 0.5f) * tuneRatio;
            flt[0].set (sr, bandHz, 1.2f);
            flt[1].set (sr, bandHz * 0.4f, 0.7f);
            bellA = std::exp (-1.0f / (0.012f * sr));
            bellB = std::exp (-1.0f / (0.15f * decay * sr));
            maxLength = 0.15f * decay * 8.0f + 0.02f;
        }

        void renderCowbell (float t, float& l, float& r)
        {
            const float x = osc[0].square (percFreq / sr) + osc[1].square (percFreq * 1.4815f / sr);
            flt[0].process (x); flt[1].process (x);
            env1 *= bellA; env2 *= bellB;
            const float a = std::min (1.0f, t / 0.0004f) * (0.6f * env1 + 0.4f * env2);
            const float y = (flt[0].bp * 0.9f + flt[1].bp * 0.35f) * a;
            l = r = y * 1.1f;
            if (t > 0.01f && env2 < 1.0e-4f) active = false;
        }

        //=== FX ==============================================================
        float fxK {}, fxHold {}, sweepTau {};
        float barF[4] {}, barAmp[4] {}, barMul[4] {}, barPh[4] {};

        void startFx()
        {
            switch (model)
            {
                case 0: fxK = std::exp (-1.0f / (0.07f * decay * sr)); sweepTau = 0.022f; break;
                case 1: fxHold = 0.30f * decay; fxK = std::exp (-6.9f / (0.04f * sr)); break;
                case 2: fxK = std::exp (-1.0f / (0.12f * decay * sr)); sweepTau = 0.06f * decay; break;
                case 3: fxK = std::exp (-1.0f / (0.05f * decay * sr)); sweepTau = 0.015f; break;
                case 4:     // Tamb: jingles through high bands, in three quick bursts
                {
                    const float tr = std::pow (2.0f, (tone - 0.5f) * 1.2f) * tuneRatio;
                    metalScale = 6.0f * tuneRatio; noiseMix = 0.5f;
                    flt[0].set (sr, 9000.0f * tr, 0.8f); flt[1].set (sr, 9000.0f * tr, 0.8f);
                    flt[2].set (sr, 5000.0f * tr, 0.7f); flt[3].set (sr, 5000.0f * tr, 0.7f);
                    fxK = std::exp (-1.0f / (0.10f * decay * sr));
                    break;
                }
                case 5:     // Maracas: a short hiss
                {
                    const float tr = std::pow (2.0f, (tone - 0.5f) * 1.2f) * tuneRatio;
                    flt[0].set (sr, 9000.0f * tr, 0.9f); flt[1].set (sr, 9000.0f * tr, 0.9f);
                    flt[2].set (sr, 6000.0f * tr, 0.7f); flt[3].set (sr, 6000.0f * tr, 0.7f);
                    fxK = std::exp (-1.0f / (0.025f * decay * sr));
                    break;
                }
                case 6:     // Riser: noise sweeping up into the next bar, then cut
                    fxHold = 0.9f * decay;
                    fxK = std::exp (-6.9f / (0.006f * sr));
                    break;
                default:    // Metal: a struck bar's partials (free-free bar ratios)
                {
                    static const float ratio[4] = { 1.0f, 2.756f, 5.404f, 8.933f };
                    static const float tau[4] = { 0.45f, 0.25f, 0.12f, 0.06f };
                    for (int i = 0; i < 4; ++i)
                    {
                        barF[i] = 330.0f * tuneRatio * ratio[i];
                        barAmp[i] = std::pow (0.3f + 0.5f * tone, (float) i) * (barF[i] < 0.45f * sr ? 1.0f : 0.0f);
                        barMul[i] = std::exp (-1.0f / (tau[i] * decay * sr));
                        barPh[i] = 0.0f;
                    }
                    fxK = barMul[0];
                    break;
                }
            }
            if (model <= 3)
            {
                flt[0].set (sr, 170.0f * std::pow (2.0f, tone * 3.5f), 1.6f);   // the stab's low-pass
                drive = 2.5f + tone * 2.0f;
                driveNorm = 1.0f / std::tanh (drive);
            }
            maxLength = 1.5f * decay + 0.1f;
        }

        void renderFx (float t, float& l, float& r)
        {
            float s = 0.0f;
            switch (model)
            {
                case 0:
                {
                    const float f = (250.0f + 2300.0f * std::exp (-t / sweepTau)) * tuneRatio;
                    phase += f / sr; if (phase >= 1.0f) phase -= 1.0f;
                    flt[1].set (sr, std::max (700.0f, f) * std::pow (2.0f, tone - 0.5f), 1.2f);
                    env1 *= fxK;
                    s = (std::sin (kTwoPi * phase) * std::exp (-t / 0.05f) + 1.2f * flt[1].process (rngL.next())) * env1;
                    s = std::tanh (1.5f * s);
                    break;
                }
                case 1:
                {
                    const float f = 98.0f * tuneRatio;
                    const float x = osc[0].saw (f / sr) + osc[1].saw (f * 1.4983f / sr) + 0.7f * osc[2].saw (f * 1.006f / sr);
                    if (t >= fxHold) env1 *= fxK;
                    const float a = std::min (1.0f, t / 0.002f) * env1;
                    flt[0].process (x);
                    s = std::tanh (drive * flt[0].lp * 0.5f * a) * driveNorm * 0.8f;
                    if (t > fxHold && env1 < 1.0e-4f) active = false;
                    break;
                }
                case 2:
                {
                    const float fc = (800.0f + 5200.0f * std::exp (-t / sweepTau)) * tuneRatio * std::pow (2.0f, (tone - 0.5f));
                    flt[1].set (sr, fc, 2.5f);
                    env1 *= fxK;
                    s = flt[1].process (rngL.next()) * 2.5f * env1 * std::min (1.0f, t / 0.002f);
                    break;
                }
                case 3:
                {
                    const float f = (700.0f + 700.0f * std::exp (-t / sweepTau)) * tuneRatio * std::pow (2.0f, (tone - 0.5f));
                    phase += f / sr; if (phase >= 1.0f) phase -= 1.0f;
                    env1 *= fxK;
                    s = std::sin (kTwoPi * phase) * env1 * std::min (1.0f, t / 0.0005f) * 0.8f;
                    break;
                }
                case 4:
                case 5:
                {
                    // jingles / beads: three or two quick bursts, then the tail
                    const float m = model == 4 ? metal() : 0.0f;
                    const float c = rngL.next(), d = rngR.next();
                    const float nl = 0.8f * c + 0.35f * d, nr = 0.8f * c - 0.35f * d;
                    const float mix = model == 4 ? noiseMix : 1.0f;
                    flt[0].process ((1.0f - mix) * m + mix * nl); flt[1].process ((1.0f - mix) * m + mix * nr);
                    flt[2].process (flt[0].bp); flt[3].process (flt[1].bp);
                    env1 *= fxK;
                    float bursts = 0.0f;
                    if (model == 4)
                    {
                        static const float at[3] = { 0.0f, 0.007f, 0.016f };
                        for (float b : at) if (t >= b) bursts += std::exp (-(t - b) / 0.012f);
                        bursts = 0.6f * bursts + 0.5f * env1;
                    }
                    else
                        bursts = std::min (1.0f, t / 0.003f) * env1;
                    l = flt[2].hp * bursts * 4.0f;
                    r = flt[3].hp * bursts * 4.0f;
                    if (t > 0.03f && env1 < 1.0e-4f) active = false;
                    return;
                }
                case 6:
                {
                    // the band climbs from 400 Hz to 8 kHz, the level with it
                    const float x = std::min (1.0f, t / std::max (0.01f, fxHold));
                    const float fc = 400.0f * std::pow (20.0f, x) * tuneRatio * std::pow (2.0f, tone - 0.5f);
                    if ((n & 7) == 0) { flt[1].set (sr, fc, 2.5f); flt[2].set (sr, fc, 2.5f); }
                    if (t >= fxHold) env1 *= fxK;
                    const float a = (0.15f + 0.85f * x * x) * env1;
                    const float c = rngL.next(), d = rngR.next();
                    l = flt[1].process (0.8f * c + 0.35f * d) * a * 1.1f;
                    r = flt[2].process (0.8f * c - 0.35f * d) * a * 1.1f;
                    if (t > fxHold && env1 < 1.0e-4f) active = false;
                    return;
                }
                default:
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        barPh[i] += barF[i] / sr; if (barPh[i] >= 1.0f) barPh[i] -= 1.0f;
                        s += barAmp[i] * std::sin (kTwoPi * barPh[i]);
                        barAmp[i] *= barMul[i];
                    }
                    env1 *= fxK;
                    s = s * std::min (1.0f, t / 0.0004f) * 0.55f + rngL.next() * 0.3f * std::exp (-t / 0.002f);
                    break;
                }
            }
            l = r = s;
            if (model != 1 && model != 6 && t > 0.01f && env1 < 1.0e-4f) active = false;
        }


        //=== Shaped: models 8 and 9 of every lane ==============================
        // A body (a sine falling in pitch to its note, held, then decaying) and up
        // to three band-passed noise layers, each with an attack and a two-stage
        // decay (a clap's first layer comes in bursts); a low-pass, a drive and
        // a hard gate at the end. The numbers were fitted to recorded hits (their
        // levels over time in eight bands), nothing of the recordings is kept.
        struct ShapedLayer { float level, fc, q, attack, tau, tau2, mix2; };
        struct ShapedSpec
        {
            float bLevel, bFreq, bRatio, bTauP, bHold, bTau;
            ShapedLayer layer[3];
            float gate, drive, lpHz;
            int bursts;
            float gain, width;
        };

        static const ShapedSpec& shapedSpec (int lane, int which)
        {
            static const ShapedSpec t[kNumLanes * kShapedModels] = {
                // Kick: Gated (after Bassdrum 03, 1.8 dB from it)
                { 1.23454f, 40.128f, 4.96317f, 0.0374513f, 0.155916f, 0.0610717f,
                  { { 1.10616f, 3912.15f, 1.59887f, 0.00145772f, 0.00424625f, 0.0156567f, 0.259487f }, { 0.795388f, 480.669f, 1.76799f, 0.710552f, 0.00750174f, 0.0353593f, 0.167184f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.229138f, 1.33868f, 8184.59f, 0, 1.0000f, 0.0f },
                // Kick: Drop (after Bassdrum 04, 2.6 dB from it)
                { 0.833802f, 31.0639f, 6.51617f, 0.00696617f, 0.0945735f, 1.37595f,
                  { { 1.95923f, 183.287f, 13.0153f, 0.0211895f, 0.0671571f, 0.0150837f, 0.474208f }, { 1.88034f, 194.239f, 4.39399f, 0.00372048f, 0.0561046f, 0.0169106f, 0.365026f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.271927f, 3.15621f, 13520.3f, 0, 0.8222f, 0.0f },
                // Bass: Gate Rumble (after Gate FX 01, 1.2 dB from it)
                { 1.71382f, 71.4742f, 1.17996f, 0.0234467f, 0.301492f, 0.527051f,
                  { { 1.84111f, 76.3919f, 19.0398f, 0.00211039f, 0.465722f, 0.0323084f, 0.188259f }, { 1.31917f, 72.6472f, 2.86902f, 0.00183403f, 0.565262f, 0.526325f, 0.980859f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.428571f, 0.267126f, 2270.21f, 0, 0.2570f, 0.1f },
                // Bass: Verb Rumble (after Reverb 04, 4.2 dB from it)
                { 0.565941f, 45.3066f, 8.84875f, 0.0118545f, 0.0701388f, 1.15546f,
                  { { 0.417445f, 171.195f, 9.78521f, 0.000308331f, 0.496204f, 0.854424f, 0.188684f }, { 1.50649f, 1664.15f, 0.928203f, 0.00157177f, 0.00790071f, 0.0156709f, 0.328076f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  4.84413f, 1.91433f, 6468.67f, 0, 0.4955f, 0.1f },
                // Clap: Gate Clap (after Clap 02, 2.2 dB from it)
                { 0.0f, 163.643f, 5.34336f, 0.0518153f, 0.0294875f, 0.905653f,
                  { { 0.222791f, 62.6468f, 0.786699f, 0.00108005f, 0.234552f, 0.213399f, 0.219026f }, { 1.5511f, 1646.98f, 0.865877f, 0.000379577f, 0.011491f, 0.0685614f, 0.411271f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.526893f, 7.65638f, 12870.6f, 4, 0.5129f, 0.35f },
                // Clap: Verb Clap (after Clap 01, 2.2 dB from it)
                { 0.0f, 137.71f, 9.55284f, 0.0245304f, 0.19669f, 1.24946f,
                  { { 0.284726f, 1415.65f, 2.93249f, 0.33636f, 0.143149f, 0.0703525f, 0.553172f }, { 0.690537f, 721.084f, 0.362245f, 0.000721331f, 0.0277748f, 0.109117f, 0.669893f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  1.02025f, 4.27737f, 16881.3f, 4, 2.1627f, 0.35f },
                // Closed Hat: Crisp (after HiHat Closed 01, 1.8 dB from it)
                { 0.0f, 56.1552f, 11.272f, 0.0319668f, 0.426869f, 1.35419f,
                  { { 1.7883f, 2924.48f, 1.12627f, 0.00147479f, 0.00497494f, 0.0253033f, 0.926415f }, { 1.25693f, 223.8f, 10.8059f, 0.639996f, 0.00377495f, 0.0400656f, 0.474331f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.152948f, 1.17793f, 8500.75f, 0, 0.5689f, 0.3f },
                // Closed Hat: Ring Tick (after HiHat Open 02, 1.3 dB from it)
                { 0.0f, 168.424f, 2.00001f, 0.0149369f, 0.244902f, 1.20948f,
                  { { 0.907514f, 3728.07f, 0.683622f, 0.0431997f, 0.0641418f, 0.0235362f, 0.505076f }, { 0.879919f, 12583.4f, 0.739442f, 0.00370738f, 0.2656f, 0.315731f, 0.480385f }, { 0.288137f, 3121.92f, 0.822847f, 0.000652742f, 0.683311f, 0.0318158f, 0.805369f } },
                  0.07f, 0.911456f, 3393.1f, 0, 1.4962f, 0.3f },
                // Open Hat: Wash (after HiHat Open 03, 1.4 dB from it)
                { 0.0f, 366.159f, 10.5346f, 0.035894f, 0.0117849f, 1.19025f,
                  { { 1.71484f, 9457.72f, 0.376194f, 0.000759824f, 0.0557948f, 0.509032f, 0.155391f }, { 1.51368f, 10982.6f, 0.621816f, 0.0102174f, 0.0294332f, 0.0567289f, 0.283701f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.211587f, 2.6628f, 5057.84f, 0, 0.9441f, 0.35f },
                // Open Hat: Ring (after HiHat Open 02, 1.3 dB from it)
                { 0.0f, 168.424f, 2.00001f, 0.0149369f, 0.244902f, 1.20948f,
                  { { 0.907514f, 3728.07f, 0.683622f, 0.0431997f, 0.0641418f, 0.0235362f, 0.505076f }, { 0.879919f, 12583.4f, 0.739442f, 0.00370738f, 0.2656f, 0.315731f, 0.480385f }, { 0.288137f, 3121.92f, 0.822847f, 0.000652742f, 0.683311f, 0.0318158f, 0.805369f } },
                  0.156644f, 0.911456f, 3393.1f, 0, 1.9498f, 0.35f },
                // Cymbal: Dark Crash (after Crash 01, 5.3 dB from it)
                { 0.00677505f, 120.39f, 9.08697f, 0.0784413f, 0.132751f, 0.0445071f,
                  { { 1.46482f, 81.3232f, 1.22029f, 0.023915f, 0.00918947f, 0.155644f, 0.678559f }, { 0.897912f, 2884.49f, 8.81117f, 0.000799816f, 0.00329412f, 0.154403f, 0.439663f }, { 1.9873f, 3959.65f, 6.54358f, 0.00163502f, 0.14772f, 0.0968949f, 0.0324345f } },
                  1.59467f, 7.27479f, 14985.2f, 0, 0.2600f, 0.45f },
                // Cymbal: Swell (after Crash 02, 3.5 dB from it)
                { 0.0f, 203.186f, 10.4985f, 0.048252f, 0.0910934f, 0.474776f,
                  { { 0.675524f, 7058.38f, 23.6084f, 0.879872f, 0.411232f, 0.428115f, 0.900026f }, { 0.147704f, 5501.03f, 0.832892f, 0.00238339f, 0.0287629f, 0.116855f, 0.0138883f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  3.42893f, 0.845809f, 7412.6f, 0, 0.1884f, 0.45f },
                // Perc: Gate Tom (after Percussion 03, 2.7 dB from it)
                { 0.206192f, 83.8314f, 3.14011f, 0.0275304f, 0.137769f, 0.0605993f,
                  { { 0.843926f, 3656.87f, 1.04191f, 0.000383233f, 0.197338f, 0.050172f, 0.873571f }, { 1.24397f, 445.273f, 1.24954f, 0.000744589f, 0.073492f, 0.0798042f, 0.0303971f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.433946f, 0.329923f, 6971.71f, 0, 1.5136f, 0.25f },
                // Perc: Gate Hit (after Percussion 01, 3.2 dB from it)
                { 0.00515876f, 105.268f, 6.14764f, 0.00517365f, 0.024211f, 1.82934f,
                  { { 0.91123f, 115.272f, 0.691391f, 0.00114485f, 0.0321547f, 0.0757119f, 0.592736f }, { 1.54055f, 894.616f, 1.36927f, 0.00255233f, 0.015749f, 0.0573585f, 0.0365488f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.254989f, 1.88909f, 3589.45f, 0, 2.2909f, 0.25f },
                // FX: Gate FX (after Gate FX 02, 3.5 dB from it)
                { 0.0265019f, 361.894f, 9.65263f, 0.0670668f, 0.154534f, 0.50501f,
                  { { 0.578794f, 1505.73f, 4.24342f, 0.000758488f, 0.149145f, 0.0215073f, 0.629282f }, { 1.25132f, 1068.73f, 1.54379f, 0.000780424f, 0.0148728f, 0.0534572f, 0.612078f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  0.431859f, 0.761762f, 3593.3f, 0, 0.8610f, 0.45f },
                // FX: Reverb (after Reverb 01, 4.5 dB from it)
                { 0.468833f, 58.3416f, 10.7965f, 0.00952825f, 0.42469f, 0.673592f,
                  { { 1.99295f, 4881.83f, 1.17577f, 0.00135233f, 0.223744f, 0.0140831f, 0.804942f }, { 1.96166f, 94.8057f, 0.688467f, 0.0415597f, 0.727059f, 1.22382f, 0.571001f }, { 0.0f, 1000.0f, 1.0f, 0.001f, 0.1f, 0.1f, 0.0f } },
                  8.72095f, 5.98338f, 7584.92f, 0, 0.0933f, 0.45f }
            };
            return t[std::clamp (lane, 0, kNumLanes - 1) * kShapedModels + std::clamp (which, 0, kShapedModels - 1)];
        }

        struct Biquad
        {
            float b0 {}, b2 {}, a1 {}, a2 {}, z1 {}, z2 {};
            void setBandPass (float sampleRate, float fc, float q)
            {
                const float w0 = kTwoPi * clampf (fc, 20.0f, sampleRate * 0.45f) / sampleRate;
                const float alpha = std::sin (w0) / (2.0f * clampf (q, 0.2f, 60.0f));
                const float a0 = 1.0f + alpha;
                b0 = 0.5f * std::sin (w0) / a0; b2 = -b0;
                a1 = -2.0f * std::cos (w0) / a0; a2 = (1.0f - alpha) / a0;
                z1 = z2 = 0.0f;
            }
            float process (float x)
            {
                const float y = b0 * x + z1;
                z1 = -a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        const ShapedSpec* spec { nullptr };
        Biquad lyL[3], lyR[3];
        float lyGain[3] {}, lyEnvA[3] {}, lyEnvB[3] {}, lyKA[3] {}, lyKB[3] {}, lyAtk[3] {}, lyMix[3] {};
        float bodyF {}, bodyKP {}, bodyEnvP {}, bodyHold {}, bodyK {}, bodyEnv {};
        float lpB {}, lpA {}, lpL {}, lpR {}, lpXL {}, lpXR {};
        float gateT {}, shDrive {}, shNorm {}, widthMid {}, widthSide {};

        void startShaped (int which)
        {
            spec = &shapedSpec (lane, which);
            const auto& sp = *spec;
            const float fTone = std::pow (2.0f, (tone - 0.5f) * 1.2f);
            bodyF = sp.bFreq * tuneRatio;
            bodyKP = std::exp (-1.0f / (std::max (1.0e-4f, sp.bTauP) * sr));
            bodyEnvP = 1.0f;
            bodyHold = sp.bHold * decay;
            bodyK = std::exp (-1.0f / (std::max (1.0e-3f, sp.bTau * decay) * sr));
            bodyEnv = 1.0f;
            for (int i = 0; i < 3; ++i)
            {
                const auto& L = sp.layer[i];
                lyGain[i] = L.level * std::sqrt (clampf (L.q, 0.2f, 60.0f));
                lyL[i].setBandPass (sr, L.fc * tuneRatio * fTone, L.q);
                lyR[i].setBandPass (sr, L.fc * tuneRatio * fTone, L.q);
                lyEnvA[i] = lyEnvB[i] = 1.0f;
                lyKA[i] = std::exp (-1.0f / (std::max (1.0e-3f, L.tau * decay) * sr));
                lyKB[i] = std::exp (-1.0f / (std::max (1.0e-3f, L.tau2 * decay) * sr));
                lyAtk[i] = 1.0f / (std::max (1.0e-4f, L.attack) * sr);
                lyMix[i] = L.mix2;
            }
            const float K = std::tan (kPi * clampf (sp.lpHz * std::pow (2.0f, (tone - 0.5f) * 2.0f), 100.0f, sr * 0.45f) / sr);
            lpB = K / (1.0f + K); lpA = (K - 1.0f) / (K + 1.0f);
            lpL = lpR = lpXL = lpXR = 0.0f;
            shDrive = std::max (0.05f, sp.drive * (0.7f + 0.6f * tone));
            shNorm = 1.0f / std::tanh (shDrive);
            gateT = sp.gate * decay;
            widthSide = sp.width; widthMid = std::sqrt (std::max (0.0f, 1.0f - sp.width * sp.width));
            maxLength = gateT + 0.01f;
        }

        void renderShaped (float t, float& l, float& r)
        {
            const auto& sp = *spec;
            float yl = 0.0f, yr = 0.0f;
            if (sp.bLevel > 0.0f)
            {
                const float f = bodyF * (1.0f + (sp.bRatio - 1.0f) * bodyEnvP);
                bodyEnvP *= bodyKP;
                phase += f / sr; if (phase >= 1.0f) phase -= 1.0f;
                if (t >= bodyHold) bodyEnv *= bodyK;
                const float b = sp.bLevel * std::sin (kTwoPi * phase) * bodyEnv * std::min (1.0f, t / 0.0005f);
                yl += b; yr += b;
            }
            for (int i = 0; i < 3; ++i)
            {
                if (sp.layer[i].level <= 0.0f) continue;
                const float c = rngL.next() * 1.7320508f, d = rngR.next() * 1.7320508f;
                const float nl = lyL[i].process (widthMid * c + widthSide * d);
                const float nr = lyR[i].process (widthMid * c - widthSide * d);
                float env = std::min (1.0f, (float) n * lyAtk[i]) * ((1.0f - lyMix[i]) * lyEnvA[i] + lyMix[i] * lyEnvB[i]);
                lyEnvA[i] *= lyKA[i]; lyEnvB[i] *= lyKB[i];
                if (i == 0 && sp.bursts >= 2)
                {
                    // a clap: the first layer comes as a few bursts, then its tail
                    const float gap = 0.0095f;
                    float bu = 0.0f;
                    for (int k = 0; k < sp.bursts; ++k)
                    {
                        const float tb = t - (float) k * gap;
                        if (tb >= 0.0f) bu += std::exp (-tb / 0.0028f) * (k == 0 ? 1.0f : 0.8f);
                    }
                    env *= (t >= (float) (sp.bursts - 1) * gap ? 0.35f : 0.0f) + bu;
                }
                yl += lyGain[i] * nl * env;
                yr += lyGain[i] * nr * env;
            }
            // the low-pass (one pole, bilinear), the drive, the gate
            lpL = lpB * (yl + lpXL) - lpA * lpL; lpXL = yl;
            lpR = lpB * (yr + lpXR) - lpA * lpR; lpXR = yr;
            const float g = clampf ((gateT + 0.004f - t) / 0.004f, 0.0f, 1.0f) * sp.gain;
            l = std::tanh (shDrive * lpL) * shNorm * g;
            r = std::tanh (shDrive * lpR) * shNorm * g;
            if (t > gateT + 0.004f) active = false;
        }

        //=== Box: the analog and hybrid boxes' sounds =======================================
    public:
        // a box sound from a spec of its own (the fitting tools use it)
        void startBoxWith (int laneType, const BoxSpec& sp, const Sound& s, float velocity, bool alt, uint32_t seed)
        {
            Sound s2 = s;
            s2.model = 0;
            start (laneType, s2, velocity, alt, seed);
            model = 0;
            kind = Kind::Box;
            startBox (sp, alt);
        }
    private:
        // After what the boxes' circuits do: two tones, six squares at the analog box's
        // metal frequencies through two band-passes, two noise layers, a click,
        // then a high-pass, a low-pass and a drive. Tune moves every frequency,
        // Decay every decay, Tone the filters and the noise (the snappy).
        BoxSpec box {};
        float bxPhase[2] {}, bxPE[2] {}, bxKP[2] {}, bxEA[2] {}, bxEB[2] {}, bxKA[2] {}, bxKB[2] {}, bxAtk[2] {}, bxF[2] {};
        float mxEA[2] {}, mxEB[2] {}, mxKA[2] {}, mxKB[2] {}, mxAtk {}, mxHpA {}, mxHpZ {};
        float nxEA[2] {}, nxEB[2] {}, nxKA[2] {}, nxKB[2] {}, nxAtk[2] {}, nxGain[2] {};
        float bxHpA {}, bxHpZL {}, bxHpZR {}, bxDrive {}, bxNorm {}, bxGateT {}, bxGateF {};
        Biquad bxTone, bxMetal[2], bxNL[2], bxNR[2], bxClick;
        BlepOsc bxSq[2];

        static void unityBandPass (Biquad& b, float sampleRate, float fc, float q)
        {
            const float w0 = kTwoPi * clampf (fc, 20.0f, sampleRate * 0.45f) / sampleRate;
            const float alpha = std::sin (w0) / (2.0f * clampf (q, 0.2f, 60.0f));
            const float a0 = 1.0f + alpha;
            b.b0 = alpha / a0; b.b2 = -b.b0;
            b.a1 = -2.0f * std::cos (w0) / a0; b.a2 = (1.0f - alpha) / a0;
            b.z1 = b.z2 = 0.0f;
        }
        static float decayK (float tau, float sampleRate) { return std::exp (-1.0f / (std::max (1.0e-4f, tau) * sampleRate)); }

        void startBox (const BoxSpec& sp, bool alt)
        {
            box = sp;
            const float altRatio = alt ? (lane == Bass ? 2.0f : (lane == Perc ? 1.335f : 1.0f)) : 1.0f;
            const float fTone = std::pow (2.0f, (tone - 0.5f) * 0.8f);
            float longest = 0.01f;
            for (int i = 0; i < 2; ++i)
            {
                const auto& o = sp.osc[i];
                bxPhase[i] = 0.0f; bxPE[i] = 1.0f; bxEA[i] = bxEB[i] = 1.0f;
                bxF[i] = o.freq * tuneRatio * altRatio;
                bxKP[i] = decayK (o.tauP, sr);
                bxKA[i] = decayK (o.tau * decay, sr);
                bxKB[i] = decayK (o.tau2 * decay, sr);
                bxAtk[i] = 1.0f / (std::max (1.0e-4f, o.attack) * sr);
                bxSq[i].phase = 0.0f;
                if (o.level > 0.0f) longest = std::max (longest, o.attack + 7.0f * std::max (o.tau * (1.0f - o.mix2), o.tau2 * o.mix2) * decay);
            }
            if (sp.oscBpHz > 0.0f) unityBandPass (bxTone, sr, sp.oscBpHz * tuneRatio * altRatio, sp.oscBpQ);
            for (int k = 0; k < 6; ++k) osc[k].phase = (float) k * 0.137f;
            mxAtk = 1.0f / (std::max (1.0e-4f, sp.metalAttack) * sr);
            for (int j = 0; j < 2; ++j)
            {
                const auto& b = sp.metal[j];
                unityBandPass (bxMetal[j], sr, b.fc * tuneRatio * fTone, b.q);
                mxEA[j] = mxEB[j] = 1.0f;
                mxKA[j] = decayK (b.tau * decay, sr);
                mxKB[j] = decayK (b.tau2 * decay, sr);
                if (b.level > 0.0f) longest = std::max (longest, 7.0f * std::max (b.tau * (1.0f - b.mix2), b.tau2 * b.mix2) * decay);
            }
            mxHpA = 1.0f - std::exp (-kTwoPi * clampf (sp.metalHpHz * tuneRatio, 10.0f, sr * 0.45f) / sr);
            mxHpZ = 0.0f;
            for (int j = 0; j < 2; ++j)
            {
                const auto& L = sp.noise[j];
                unityBandPass (bxNL[j], sr, L.fc * tuneRatio * fTone, L.q);
                unityBandPass (bxNR[j], sr, L.fc * tuneRatio * fTone, L.q);
                nxEA[j] = nxEB[j] = 1.0f;
                nxKA[j] = decayK (L.tau * decay, sr);
                nxKB[j] = decayK (L.tau2 * decay, sr);
                nxAtk[j] = 1.0f / (std::max (1.0e-4f, L.attack) * sr);
                nxGain[j] = L.level * (0.6f + 0.8f * tone);
                if (L.level > 0.0f) longest = std::max (longest, L.attack + 7.0f * std::max (L.tau * (1.0f - L.mix2), L.tau2 * L.mix2) * decay
                                                                 + (j == 0 ? sp.bursts * sp.burstGap : 0.0f));
            }
            unityBandPass (bxClick, sr, sp.clickHz * fTone, 0.7f);
            bxHpA = 1.0f - std::exp (-kTwoPi * clampf (sp.hpHz * tuneRatio, 5.0f, sr * 0.45f) / sr);
            bxHpZL = bxHpZR = 0.0f;
            const float K = std::tan (kPi * clampf (sp.lpHz * std::pow (2.0f, (tone - 0.5f) * 2.0f), 100.0f, sr * 0.45f) / sr);
            lpB = K / (1.0f + K); lpA = (K - 1.0f) / (K + 1.0f);
            lpL = lpR = lpXL = lpXR = 0.0f;
            bxDrive = sp.drive;
            bxNorm = bxDrive > 0.05f ? 1.0f / std::tanh (bxDrive) : 1.0f;
            widthSide = clampf (sp.width, 0.0f, 1.0f);
            widthMid = std::sqrt (std::max (0.0f, 1.0f - widthSide * widthSide));
            bxGateT = sp.gate > 0.0f ? sp.gate * decay : 0.0f;
            bxGateF = std::max (0.001f, sp.gateFade);
            if (bxGateT > 0.0f) longest = std::min (longest, bxGateT + bxGateF);
            maxLength = std::min (12.0f, longest + 0.05f);
        }

        void renderBox (float t, float& l, float& r)
        {
            const auto& sp = box;
            float energy = 0.0f;
            // the tones
            float tones = 0.0f;
            for (int i = 0; i < 2; ++i)
            {
                const auto& o = sp.osc[i];
                if (o.level <= 0.0f) continue;
                const float f = bxF[i] * (1.0f + (o.ratio - 1.0f) * bxPE[i]);
                bxPE[i] *= bxKP[i];
                const float inc = std::min (0.45f, f / sr);
                float w;
                if (o.wave >= 1.5f) w = bxSq[i].square (inc);
                else
                {
                    bxPhase[i] += inc; if (bxPhase[i] >= 1.0f) bxPhase[i] -= 1.0f;
                    w = o.wave >= 0.5f ? 4.0f * std::abs (bxPhase[i] - 0.5f) - 1.0f : std::sin (kTwoPi * bxPhase[i]);
                }
                const float env = std::min (1.0f, (float) n * bxAtk[i]) * ((1.0f - o.mix2) * bxEA[i] + o.mix2 * bxEB[i]);
                bxEA[i] *= bxKA[i]; bxEB[i] *= bxKB[i];
                tones += o.level * w * env;
                energy += o.level * env;
            }
            if (sp.oscBpHz > 0.0f) tones = bxTone.process (tones);
            float yl = tones, yr = tones;
            // the metal: six squares through two band-passes and a high-pass
            if (sp.metal[0].level > 0.0f || sp.metal[1].level > 0.0f)
            {
                static const float mf[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                float sq = 0.0f;
                for (int k = 0; k < 6; ++k) sq += osc[k].square (std::min (0.45f, mf[k] * sp.metalScale * tuneRatio / sr));
                sq *= (1.0f / 6.0f);
                const float atk = std::min (1.0f, (float) n * mxAtk);
                float m = 0.0f;
                for (int j = 0; j < 2; ++j)
                {
                    const auto& b = sp.metal[j];
                    if (b.level <= 0.0f) continue;
                    const float env = atk * ((1.0f - b.mix2) * mxEA[j] + b.mix2 * mxEB[j]);
                    mxEA[j] *= mxKA[j]; mxEB[j] *= mxKB[j];
                    m += b.level * bxMetal[j].process (sq) * env;
                    energy += b.level * env;
                }
                mxHpZ += mxHpA * (m - mxHpZ);
                m -= mxHpZ;
                yl += m; yr += m;
            }
            // the noise
            for (int j = 0; j < 2; ++j)
            {
                const auto& L = sp.noise[j];
                if (L.level <= 0.0f) continue;
                const float c = rngL.next() * 1.7320508f, d = rngR.next() * 1.7320508f;
                const float nl = bxNL[j].process (widthMid * c + widthSide * d);
                const float nr = bxNR[j].process (widthMid * c - widthSide * d);
                float env = std::min (1.0f, (float) n * nxAtk[j]) * ((1.0f - L.mix2) * nxEA[j] + L.mix2 * nxEB[j]);
                nxEA[j] *= nxKA[j]; nxEB[j] *= nxKB[j];
                const int bursts = (int) std::lround (sp.bursts);
                if (j == 0 && bursts >= 2)
                {
                    float bu = 0.0f;
                    for (int k = 0; k < bursts; ++k)
                    {
                        const float tb = t - (float) k * sp.burstGap;
                        if (tb >= 0.0f) bu += std::exp (-tb / 0.0025f);
                    }
                    env *= (t >= (float) (bursts - 1) * sp.burstGap ? sp.burstTail : 0.0f) + bu;
                }
                yl += nxGain[j] * nl * env;
                yr += nxGain[j] * nr * env;
                energy += nxGain[j] * env;
            }
            // the click
            if (sp.click > 0.0f && t < 0.01f)
            {
                const float c = bxClick.process (rngL.next() * std::exp (-t / 0.0006f)) * sp.click;
                yl += c; yr += c;
                energy += sp.click;
            }
            // high-pass, low-pass, drive
            bxHpZL += bxHpA * (yl - bxHpZL); yl -= bxHpZL;
            bxHpZR += bxHpA * (yr - bxHpZR); yr -= bxHpZR;
            lpL = lpB * (yl + lpXL) - lpA * lpL; lpXL = yl;
            lpR = lpB * (yr + lpXR) - lpA * lpR; lpXR = yr;
            float g = sp.gain;
            if (bxGateT > 0.0f)
            {
                g *= clampf ((bxGateT + bxGateF - t) / bxGateF, 0.0f, 1.0f);
                if (t > bxGateT + bxGateF) active = false;
            }
            if (bxDrive > 0.05f) { l = std::tanh (bxDrive * lpL) * bxNorm * g; r = std::tanh (bxDrive * lpR) * bxNorm * g; }
            else { l = lpL * g; r = lpR * g; }
            if (t > 0.02f && energy < 2.0e-5f) active = false;
        }

        //=== Sample: the lane's own sound ======================================
        // Tune is the playback rate, Decay shortens it (the curve goes with it),
        // Tone tilts it (dark below the middle, thin above), the second note is
        // an accent; the volume curve shapes it start to end.
        std::shared_ptr<const SampleData> smp;
        Curve curve;
        double spos {}, sratio {}, sspan {};
        float accent {}, tiltL1 {}, tiltL2 {}, tiltR1 {}, tiltR2 {}, tiltA {};
        int tiltMode {};

        void startSample (const Sound& s, bool alt)
        {
            smp = s.sample;
            if (smp == nullptr || smp->frames() < 2) { active = false; return; }
            curve = s.curve;
            sratio = smp->rate / sr * tuneRatio;
            const double played = (double) (smp->frames() - 1) / sratio;
            sspan = played * std::min (1.0f, decay);
            spos = 0.0;
            accent = alt ? 1.5f : 1.0f;
            tiltL1 = tiltL2 = tiltR1 = tiltR2 = 0.0f;
            tiltMode = tone < 0.48f ? -1 : (tone > 0.52f ? 1 : 0);
            const float fc = tiltMode < 0 ? 20000.0f * std::pow (2.0f, -(0.5f - tone) * 16.0f)
                                          : 20.0f * std::pow (2.0f, (tone - 0.5f) * 16.0f);
            tiltA = 1.0f - std::exp (-kTwoPi * clampf (fc, 10.0f, sr * 0.45f) / sr);
            maxLength = (float) (sspan / sr) + 0.01f;
        }

        void renderSample (float& l, float& r)
        {
            const auto& d = *smp;
            if ((double) n >= sspan) { active = false; return; }
            const int i0 = (int) spos;
            if (i0 >= d.frames() - 1) { active = false; return; }
            const float f = (float) (spos - (double) i0);
            float a = d.left[(size_t) i0] + f * (d.left[(size_t) i0 + 1] - d.left[(size_t) i0]);
            float b = d.stereo() ? d.right[(size_t) i0] + f * (d.right[(size_t) i0 + 1] - d.right[(size_t) i0]) : a;
            spos += sratio;
            if (tiltMode != 0)
            {
                tiltL1 += tiltA * (a - tiltL1); tiltL2 += tiltA * (tiltL1 - tiltL2);
                tiltR1 += tiltA * (b - tiltR1); tiltR2 += tiltA * (tiltR1 - tiltR2);
                if (tiltMode < 0) { a = tiltL2; b = tiltR2; }
                else { a -= tiltL2; b -= tiltR2; }
            }
            const float u = (float) ((double) n / sspan);
            const float endFade = std::min (1.0f, (float) ((sspan - (double) n) / (0.003 * sr)));
            const float amp = curve.at (u) * endFade * accent;
            l = a * amp;
            r = b * amp;
        }

        //=== state ===========================================================
        float sr { 48000.0f };
        int lane { 0 }, model { 0 };
        enum class Kind { Classic, Shaped, Sampled, Box };
        Kind kind { Kind::Classic };
        int64_t n { 0 };
        bool active { false };
        float gain { 1.0f }, fade { 1.0f }, fadeStep { 0.0f };
        float tuneRatio { 1.0f }, decay { 1.0f }, tone { 0.5f };
        float phase { 0.0f }, phase2 { 0.0f }, dc { 0.0f };
        float env1 { 1.0f }, env2 { 1.0f }, env3 { 1.0f };
        float maxLength { 2.0f };
        float phase3 { 0.0f };
        BlepOsc osc[6];
        Svf flt[4];
        Rng rngL, rngR;
    };

    // the analog and hybrid boxes' sounds, fitted to recordings of the two boxes (their levels over time in
    // third-octave bands, and their spectra); nothing of the recordings is kept
    inline const BoxSpec& boxSpec (int index)
    {
        static const BoxSpec t[kNumBoxSpecs] = {
#include "CgBoxTable.inc"
        };
        return t[std::clamp (index, 0, kNumBoxSpecs - 1)];
    }
}
