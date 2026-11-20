#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/SnapDrums.h"

// Clap - the classic analog clap trick: instead of one noise burst, three short bursts
// fire a few milliseconds apart (that stutter is what the ear reads as many
// hands), followed by a longer diffuse tail. Spread sets the gap between the
// bursts, Tone the band-pass they all share.
// Input: 0 = Trig In. Output: 0 = Audio Out.
class ClapModule : public aquanode::SynthModule
{
public:
    static constexpr int kBursts = 3;

    enum ParamIndex { pTone = 0, pSpread, pBurstDecay, pTailDecay, pTailMix, pVoices };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        pModel = paramIndex ("model");
        for (auto& c : snappy) c.prepare ((float) sr);
        for (auto& c : clack) c.prepare ((float) sr);
        reset();
    }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        burstEnv[v] = tailEnv[v] = 0.0f;
        burstsLeft[v] = 0;
        burstTimer[v] = 0.0;
        bpLow[v] = bpBand[v] = 0.0f;
        lastTrig[v] = 0.0f;
        running[v] = false;
        snappy[v].reset();
        clack[v].reset();
    }

    void voiceNoteOn (int v, int, bool) override
    {
        pool.noteOn (v, voiceLimit());
        trigger (v);
    }

    double voiceTailSeconds() const override
    {
        if (model() > 0) return 0.08 + 0.35 * param (pTailDecay) / 220.0;   // the measured claps ring ~50 ms at the default
        return 0.15 + aquanode::decayTailSeconds (juce::jmax (5.0f, param (pTailDecay)));   // bursts, then the tail
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
        renderGlobalMidiHits (inputs, outputs, 0);   // notes on Midi In, see ModuleCore.h
    }

    // global lane: notes arriving on Midi In play as overlapping hits
    bool acceptsGlobalMidiNotes() const override { return true; }

private:
    int model() const { return pModel >= 0 ? (int) std::lround (param (pModel)) : 0; }

    void trigger (int v)
    {
        // the measured models: the knobs scale them around their defaults
        const float tone = std::pow (2.0f, (param (pTone) - 50.0f) / 50.0f);
        if (model() == 1)
        {
            snappy[v].setTone (tone);
            snappy[v].trigger (param (pSpread), param (pBurstDecay) / 8.0f, param (pTailDecay) / 220.0f, param (pTailMix) / 60.0f);
        }
        else if (model() == 2)
        {
            clack[v].setTone (tone);
            clack[v].trigger (param (pBurstDecay) / 8.0f, param (pTailDecay) / 220.0f, param (pTailMix) / 60.0f);
        }
        burstEnv[v] = 1.0f;
        burstsLeft[v] = kBursts;      // this one plus two more
        burstTimer[v] = 0.0;
        tailEnv[v] = 0.0f;
        running[v] = true;
    }

    aquanode::ModuleVoicePool pool;
    int pModel { -1 };
    aquanode::snapdrums::SnappyClap snappy [aquanode::kMaxVoices];
    aquanode::snapdrums::Snap clack [aquanode::kMaxVoices];
    float burstEnv [aquanode::kMaxVoices] {};
    float tailEnv  [aquanode::kMaxVoices] {};
    int burstsLeft [aquanode::kMaxVoices] {};
    double burstTimer [aquanode::kMaxVoices] {};
    float bpLow [aquanode::kMaxVoices] {}, bpBand [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    bool running [aquanode::kMaxVoices] {};
    juce::Random random;
};
