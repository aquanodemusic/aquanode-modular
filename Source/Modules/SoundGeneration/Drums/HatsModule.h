#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/SnapDrums.h"

// Hats - classic analog metallic percussion: six detuned square oscillators
// summed, band-passed, high-passed and shaped by a snappy exponential decay.
// Short Decay = closed hat, long = open hat / ride-ish. Metal spreads the
// oscillator ratios for clangier tones. Trig In edges and MIDI note-on fire it.
// Input: 0 = Trig In. Output: 0 = Audio Out.
class HatsModule : public aquanode::SynthModule
{
public:
    static constexpr int kNumOscs = 6;
    enum ParamIndex { pTune = 0, pDecay, pTone, pMetal };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        pModel = paramIndex ("model");
        for (auto& sn : snip) sn.prepare ((float) sr);
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
        env[v] = 0.0f;
        lastTrig[v] = 0.0f;
        bpLow[v] = bpBand[v] = 0.0f;
        hpState[v] = hpPrevIn[v] = 0.0f;
        for (int o = 0; o < kNumOscs; ++o)
            phase[v][o] = o * 0.13;   // decorrelated start phases
        snip[v].reset();
    }

    void voiceNoteOn (int v, int, bool) override { pool.noteOn (v, voiceLimit()); hit (v); }

    double voiceTailSeconds() const override
    {
        if (snipModel()) return 0.24 * param (pDecay) / 120.0 + 0.02;   // down 20 dB in ~40 ms at the default
        return aquanode::decayTailSeconds (juce::jmax (5.0f, param (pDecay))) + 0.05;
    }

    bool snipModel() const { return pModel >= 0 && param (pModel) > 0.5f; }
    void hit (int v)
    {
        env[v] = 1.0f;
        if (snipModel())
        {
            // the Snip: Decay, Tone (brightness) and Tune (its two rings) scale the measured hit
            snip[v].setTone (std::pow (2.0f, (param (pTone) - 60.0f) / 60.0f), param (pTune) / 205.0f);
            snip[v].trigger (param (pDecay) / 120.0f);
        }
    }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
        renderGlobalMidiHits (inputs, outputs, 0);   // notes on Midi In, see ModuleCore.h
    }

    // global lane: notes arriving on Midi In play as overlapping hits
    bool acceptsGlobalMidiNotes() const override { return true; }

private:
    aquanode::ModuleVoicePool pool;
    float env [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    double phase [aquanode::kMaxVoices][kNumOscs] {};
    float bpLow [aquanode::kMaxVoices] {};
    float bpBand [aquanode::kMaxVoices] {};
    float hpState [aquanode::kMaxVoices] {};
    float hpPrevIn [aquanode::kMaxVoices] {};
    int pModel { -1 };
    aquanode::snapdrums::Snip snip [aquanode::kMaxVoices];
};
