#pragma once

#include "../../Core/ModuleCore.h"

// Formant Filter - the Formant module's vowel bank, aimed at whatever you
// patch in instead of its own glottal buzz. Three parallel band-passes sit on
// the measured formant peaks of A E I O U, and Vowel morphs continuously
// between them, so a saw pad turns into a choir and a drum loop starts
// talking.
//
// Size shifts every formant at once - really throat length: down for a chest,
// up for a child. Resonance is how sharply the peaks ring: low is a gentle
// vowel colour, high is a vocoder-ish singing resonance. Every knob is
// modulatable: a cable onto Vowel makes talking pads.
//
// Stretch (6.9.19) spreads the peaks apart or squeezes them together.
// Flexible lane: inside a per-voice chain each voice gets its own filter
// state and can be given its own vowel; in a global chain it runs once.
// Inputs: 0 = Audio In. Output: 0 = Audio Out.
class FormantFilterModule : public aquanode::SynthModule
{
public:
    static constexpr int kFormants = 3;
    static constexpr int kVowels = 5;   // A E I O U

    enum ParamIndex { pVowel = 0, pSize, pResonance, pDryWet, pLevel, pStretch };

    // (6.9.19) Stretch: the peaks spread apart (+) or squeezed together (-) on the
    // octave scale, around 1 kHz: +100 % doubles their distances in octaves, -100 % halves them
    static float stretched (float hz, float stretchPct)
    {
        if (stretchPct == 0.0f) return hz;
        return 1000.0f * std::pow (hz / 1000.0f, std::exp2 (stretchPct * 0.01f));
    }

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < kNumLanes; ++i)
            clearLane (i);
    }

    void voiceReset (int voice) override { clearLane (voice); }

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override
    {
        renderLane (voice, inputs, outputs);
    }

    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override
    {
        renderLane (aquanode::kMaxVoices, inputs, outputs);   // the global lane
    }

    bool magnitudeResponseDb (const float* hz, float* db, int n) const override;   // the static curve

private:
    static constexpr int kNumLanes = aquanode::kMaxVoices + 1;

    void renderLane (int lane, const aquanode::StereoFrame* inputs,
                     aquanode::StereoFrame* outputs);

    void clearLane (int lane)
    {
        if (lane < 0 || lane >= kNumLanes)
            return;
        for (int ch = 0; ch < 2; ++ch)
            for (int f = 0; f < kFormants; ++f)
                low[lane][ch][f] = band[lane][ch][f] = 0.0f;
    }

    // Chamberlin state-variable band-pass state, per lane / channel / formant
    float low  [kNumLanes][2][kFormants] {};
    float band [kNumLanes][2][kFormants] {};
};
