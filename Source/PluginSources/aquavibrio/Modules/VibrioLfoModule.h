#pragma once
//==============================================================================
//  VibrioLfoModule.h
//
//  The third module written for this synth. `util.lfo` covers the first five
//  hardware's shapes and nothing else, and four of the hardware's LFO controls
//  have no equivalent in it at all:
//
//    Shape        68 shapes, not 5 - past S&G the list runs into the same
//                 wave table the oscillators read
//    Symmetry     skews the cycle itself rather than the output, so a
//                 triangle becomes a ramp instead of a squashed triangle
//    Mode         Poly gives every voice its own phase, Mono shares one
//    Keytrigger   whether a new note restarts the cycle, and at what phase
//    Env Mode     one-shot: the LFO runs a single cycle and stops, which is
//                 how the hardware uses an LFO as an extra envelope
//
//  Output: 0 = Mod Out, bipolar -1..1, per voice.
//==============================================================================

#include <JuceHeader.h>
#include "../EmbeddedModules/ModuleCore.h"
#include "VibrioOscModule.h"

namespace aquavibrio
{

class VibrioLfoModule : public aquanode::SynthModule
{
public:
    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void processVoiceSample (int voice, const aquanode::StereoFrame*,
                             aquanode::StereoFrame* outputs) override
    {
        if (! resolved)
            resolve();

        auto& s = state[(size_t) voice];

        const float rate = juce::jmax (0.001f, param (pRate));
        const int shape = juce::jlimit (0, 67, (int) param (pShape));
        const float symmetry = juce::jlimit (-0.95f, 0.95f, param (pSymmetry));
        const bool mono = param (pMode) > 0.5f;
        const bool envMode = param (pEnvMode) > 0.5f;

        // In Mono every voice reads one shared phase, so the whole chord
        // moves together instead of each note drifting on its own.
        // A mono LFO has one phase shared by every voice. It must advance once
        // per SAMPLE, not once per voice, or it runs N times too fast with N
        // voices sounding - so the engine calls advanceShared() for it.
        double& phase = mono ? sharedPhase : s.phase;

        // Reference Env Mode (opt-in): the one shot runs 1.75x the LFO's rate.
        const double increment = (double) rate / sampleRate * (envMode && referenceEnvMode ? 1.75 : 1.0);

        if (! mono && ! (envMode && s.finished))
        {
            phase += increment;

            if (phase >= 1.0)
            {
                if (envMode)
                {
                    phase = 1.0;
                    s.finished = true;      // one shot: stop at the end
                }
                else
                {
                    phase -= std::floor (phase);
                }
            }
        }

        // Symmetry stretches the first half of the cycle at the expense of
        // the second. Done on the phase, so the waveform's own shape changes
        // - a triangle really does become a ramp.
        const double warped = symmetry == 0.0f ? phase : skewPhase (phase, symmetry);

        float value = 0.0f;

        switch (shape)
        {
            case 0:
                if (referenceShapes)
                {
                    // The reference's sine takes Symmetry as a change of shape, not a
                    // skew (measured): below the middle it blends into the triangle
                    // (all the way at -1), above it is driven into a clip with a
                    // gain of 1 / (1 - s); its peaks stay where they were.
                    const float sn = (float) -std::cos (phase * juce::MathConstants<double>::twoPi);
                    const float sy = juce::jlimit (-1.0f, 0.95f, symmetry / 0.95f);
                    if (sy < 0.0f)
                    {
                        const float tri = (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
                        value = sn + (tri - sn) * -sy;
                    }
                    else
                        value = juce::jlimit (-1.0f, 1.0f, sn / (1.0f - sy));
                }
                else
                    value = (float) std::sin (warped * juce::MathConstants<double>::twoPi);
                break;
            case 1:  value = (float) (warped < 0.5 ? 4.0 * warped - 1.0 : 3.0 - 4.0 * warped); break;
            case 2:  value = referenceShapes ? (float) (1.0 - 2.0 * warped) : (float) (2.0 * warped - 1.0); break;
            case 3:  value = warped < 0.5 ? 1.0f : -1.0f; break;

            case 4:  // Sample & Hold: a new random level each cycle
                if (warped < s.lastPhase)
                    s.held = rng.nextFloat() * 2.0f - 1.0f;
                value = s.held;
                break;

            case 5:  // Sample & Glide: the same, smoothed
                if (warped < s.lastPhase)
                    s.target = rng.nextFloat() * 2.0f - 1.0f;
                s.held += (s.target - s.held) * (float) (increment * 8.0);
                value = s.held;
                break;

            default: // Waves 3..64 - the oscillators' table (the loaded one, if any), read as an LFO
                // (the loaded waves are stored inverted for the oscillators, which
                // is how the reference plays them there; as an LFO it does not)
                // (and the reference's Symmetry acts on a wave only below the middle,
                // where it holds the first half of the wave back - measured: the half
                // point moves to 0.5 + 0.5 * min (1, (-s / 0.5)^1.5) of the cycle)
                {
                    double wp = referenceShapes ? phase : warped;
                    if (referenceShapes && symmetry < 0.0f)
                    {
                        const double sy = juce::jlimit (0.0, 1.0, (double) -symmetry / 0.95);
                        const double knee = juce::jmin (0.995, 0.5 + 0.5 * juce::jmin (1.0, std::pow (sy / 0.5, 1.5)));
                        wp = phase < knee ? 0.5 * phase / knee : 0.5 + 0.5 * (phase - knee) / (1.0 - knee);
                    }
                    value = waveTable != nullptr ? -waveTable->read (juce::jlimit (0, 63, shape - 4), wp, 1.0, 8192.0)
                                                 : VibrioWaveTable::instance().read (juce::jlimit (0, 63, shape - 4), wp, 1.0, 8192.0);
                }
                break;
        }

        s.lastPhase = warped;

        // Reference Env Mode: unipolar, from the bottom of the shape (0 .. 2
        // in the LFO's own units), so the one shot ends - and stays - at zero.
        if (envMode && referenceEnvMode)
            value += 1.0f;

        outputs[0][0] = value;
        outputs[0][1] = value;
    }

    void voiceNoteOn (int voice, int, bool retrigger) override
    {
        auto& s = state[(size_t) voice];
        s.finished = false;

        if (! resolved)
            resolve();

        // Reference Env Mode always starts the one shot with the note.
        if (referenceEnvMode && param (pEnvMode) > 0.5f && ! retrigger)
            s.phase = juce::jmax (0.0, (double) param (pKeytrigger));

        // Keytrigger off means the LFO free-runs and a new note lands
        // wherever the cycle happens to be. On, it restarts - at the phase
        // the Keytrigger value asks for, which is how a patch gets an LFO
        // that always begins at the top of its sweep.
        const float keytrigger = param (pKeytrigger);

        if (keytrigger >= 0.0f && ! retrigger)
            s.phase = (double) keytrigger;

        // Unison LFO Phase fans the voices out across the cycle so a unison
        // stack shimmers instead of pulsing as one.
        const float fan = param (pPhaseSpread);

        if (fan > 0.0f)
            s.phase = std::fmod (s.phase + (double) (fan * (float) voice) / 16.0, 1.0);
    }

    void voiceReset (int voice) override { state[(size_t) voice] = {}; }

    void reset() override
    {
        rng.setSeed (0x1f0);   // repeatable sample & hold after every reset
        sharedPhase = 0.0;
    }

    // Opt-in (the Original engine): the reference's phase for the sine and
    // the direction of the saw - the sine starts at its trough like the
    // triangle (-cos), the saw falls. Off: sin and a rising saw.
    void setReferenceShapes (bool on) { referenceShapes = on; }

    // the wave table the oscillators read (sampled waves, once loaded); null: the built-in one
    void setWaveTable (const VibrioWaveTable* t) { waveTable = t; }

    // Opt-in (the Original engine): the reference's Env Mode - a unipolar one
    // shot (0 at both ends, the shape's peak at 2) that always restarts with
    // the note and runs 1.75x the LFO rate (measured). Off: a bipolar one
    // shot at the LFO rate, held wherever the cycle ends.
    void setReferenceEnvMode (bool on) { referenceEnvMode = on; }

    // Once per sample, from the engine: moves the shared (Mono mode) phase.
    void advanceShared()
    {
        if (! resolved)
            resolve();

        sharedPhase += (double) juce::jmax (0.001f, param (pRate)) / sampleRate;
        if (sharedPhase >= 1.0)
            sharedPhase -= std::floor (sharedPhase);
    }

private:
    // Piecewise: the first `symmetry`-weighted portion of the cycle is
    // stretched to fill the first half of the waveform, the rest squeezed
    // into the second.
    static double skewPhase (double phase, float symmetry)
    {
        const double pivot = juce::jlimit (0.02, 0.98, 0.5 + 0.5 * (double) symmetry);

        return phase < pivot ? 0.5 * phase / pivot
                             : 0.5 + 0.5 * (phase - pivot) / (1.0 - pivot);
    }

    struct VoiceState
    {
        double phase { 0.0 }, lastPhase { 0.0 };
        float held { 0.0f }, target { 0.0f };
        bool finished { false };
    };

    void resolve()
    {
        pRate = paramIndex ("rate");
        pShape = paramIndex ("shape");
        pSymmetry = paramIndex ("symmetry");
        pMode = paramIndex ("mode");
        pEnvMode = paramIndex ("envMode");
        pKeytrigger = paramIndex ("keytrigger");
        pPhaseSpread = paramIndex ("phaseSpread");
        resolved = true;
    }

    std::array<VoiceState, aquanode::kMaxVoices> state {};
    double sharedPhase { 0.0 };
    juce::Random rng;
    bool referenceShapes { false }, referenceEnvMode { false };
    const VibrioWaveTable* waveTable { nullptr };

    int pRate { -1 }, pShape { -1 }, pSymmetry { -1 }, pMode { -1 },
        pEnvMode { -1 }, pKeytrigger { -1 }, pPhaseSpread { -1 };
    bool resolved { false };
};

} // namespace aquavibrio
