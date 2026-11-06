#pragma once

#include "../Modules/Core/ModuleCore.h"

//==============================================================================
// What a mutation may change, and how one knob is mutated - shared by the
// Mutator strip (Mutate, Breed, Preset Generation's With Mutation) and the
// per-card Randomize / Undo pair in every module's top row.
//==============================================================================
namespace aquanode::mutation
{
    // modules no mutation touches: Audio In / Out (the master level), the
    // Piano Roll (its tempo, length and play state are the arrangement) and
    // the visualizers
    inline bool skipsModule (const ModuleDescriptor& d)
    {
        return d.section == ModuleSection::InputOutput || d.typeId == "util.pianoroll"
            || d.typeId == "util.spectrogram" || d.typeId == "util.spectrumcompare";   // views, not sound
    }

    // Delays stay as they were: an envelope's or an LFO's Delay stage (the
    // Curve Envelope's in most random presets) would make the notes start
    // late, and Precision Utility's sample delays would comb the sound
    inline bool isKeptDelay (const ParamSpec& p)
    {
        return p.id == "delay" || p.id == "delayL" || p.id == "delayR";
    }

    // knobs no mutation touches
    inline bool skipsParam (const ModuleDescriptor& d, const ParamSpec& p)
    {
        if (p.type == ParamType::Button || p.affectsRouting || p.calibration)
            return true;   // buttons; a Reroute module's bus; a fixed output trim

        // Voices is a polyphony setting, not a sound-design parameter
        if (p.id == "voices")
            return true;

        // what a Midi Learn listens to, and where its control was left
        if (d.typeId == "io.midilearn" && (p.id == "source" || p.id == "value" || p.id == "active"))
            return true;

        // the sequencers' Chance and Ratchet are there to be chosen, not stumbled on: a
        // mutation never thins a sequence out or starts it stuttering
        if ((d.typeId == "util.stepseq" || d.typeId == "util.euclid" || d.typeId == "util.arp")
            && (p.id == "chance" || p.id == "ratchet" || p.id == "hits"))
            return true;

        // the basic oscillator's FM Ratio is its DX7-style harmonic relation to
        // the note: randomising it detunes the whole patch's FM structure
        if (d.typeId == "osc.basic" && p.id == "fmRatio")
            return true;

        if (isKeptDelay (p))
            return true;

        // a card's own view switch (Resonance Suppressor's Reactance curve, shown or not)
        if (d.typeId == "filter.resonance" && p.id == "reactance")
            return true;

        // Spectral Resynthesis: which engine, and how the sound is analysed (each a whole new
        // analysis), and the sound's own Root and Fine - not sound design to wander through
        if (d.typeId == "osc.spectralresynth"
            && (p.id == "reassign" || p.id == "fft" || p.id == "hop" || p.id == "window" || p.id == "decim"
                || p.id == "threshold" || p.id == "maxPeaks" || p.id == "minLen" || p.id == "root" || p.id == "fine"))
            return true;

        // the 303's way of playing and its grid (whether it runs, its tempo, length and key)
        // are the arrangement, as a Piano Roll's are; its sound knobs mutate
        if (d.typeId == "osc.acid303"
            && (p.id == "play" || p.id == "voiceMode" || p.id == "bars" || p.id == "tempo"
                || p.id == "sync" || p.id == "running" || p.id == "transpose" || p.id == "swing" || p.id == "hookStyle"))
            return true;

        return false;
    }

    inline bool hasMutableParams (const ModuleDescriptor& d)
    {
        if (skipsModule (d))
            return false;
        for (const auto& p : d.params)
            if (! skipsParam (d, p))
                return true;
        return false;
    }

    // one knob, mutated by amount (0..1)
    inline float mutateValue (const ParamSpec& p, float v, float amount, juce::Random& rng)
    {
        // approximate Gaussian from four uniforms
        auto gauss = [&rng]
        {
            return (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 2.0f) * 0.7f;
        };

        switch (p.type)
        {
            case ParamType::Button:
                return v;

            case ParamType::Combo:
            case ParamType::RotarySteppedList:
                // discrete params jump to a random choice with low probability -
                // rare but drastic, which is where the fun mutations come from
                if (p.choices.size() > 1 && rng.nextFloat() < amount * 0.25f)
                    return (float) rng.nextInt (p.choices.size());
                return v;

            default:
            {
                if (p.interval == 1.0f)   // integer rotary (grain count, note, stages...)
                {
                    const float range = p.maxValue - p.minValue;
                    const float nv = v + std::round (gauss() * amount * range * 0.2f);
                    return juce::jlimit (p.minValue, p.maxValue, nv);
                }

                // continuous: perturb in normalised, taper-aware space so log
                // knobs (cutoff, times) mutate musically instead of linearly
                const bool logTaper = p.logarithmic && p.minValue > 0.0f;
                float norm;
                if (logTaper)
                    norm = std::log (juce::jmax (p.minValue, v) / p.minValue)
                           / std::log (p.maxValue / p.minValue);
                else
                    norm = (v - p.minValue) / juce::jmax (1.0e-9f, p.maxValue - p.minValue);

                norm = juce::jlimit (0.0f, 1.0f, norm + gauss() * amount * 0.25f);

                return logTaper ? p.minValue * std::pow (p.maxValue / p.minValue, norm)
                                : p.minValue + norm * (p.maxValue - p.minValue);
            }
        }
    }
}
