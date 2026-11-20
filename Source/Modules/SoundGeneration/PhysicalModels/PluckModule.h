#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/HarpString.h"
#include <memory>

// Pluck - Karplus-Strong plucked string: a noise burst excites a tuned
// feedback delay line with a damping low-pass in the loop. Played from the
// keyboard it's fully polyphonic (per-voice strings); triggered from a
// Step Seq / Euclid it runs as a global mono string, with Pitch In taking
// the sequencer's Pitch Out directly (semitones / 60 around Note).
// Inputs: 0 = Trig In, 1 = Pitch In. Output: 0 = Audio Out.
class PluckModule : public aquanode::SynthModule
{
public:
    static constexpr int maxDelaySamples = 8192;
    enum ParamIndex { pNote = 0, pDecay, pDamp, pColor };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    PluckModule()
    {
        for (auto& l : line)
            l.assign (maxDelaySamples, 0.0f);
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        pModel = paramIndex ("model");
        if (harp == nullptr) harp = std::make_unique<aquanode::harp::HarpString[]> ((size_t) aquanode::kMaxVoices);
        for (auto& e : harpEq) e.build (aquanode::studio::voicing (aquanode::studio::Harp), sr);
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
        if ((int) line[v].size() == maxDelaySamples)
            std::fill (line[v].begin(), line[v].end(), 0.0f);
        writeIndex[v] = 0;
        lpfState[v] = colorState[v] = 0.0f;
        burstSamples[v] = 0;
        noteOfVoice[v] = -1;
        lastTrig[v] = 0.0f;
        if (harp != nullptr) harp[(size_t) v].reset();
        harpEq[v].reset();
    }

    void voiceNoteOn (int v, int note, bool) override
    {
        pool.noteOn (v, voiceLimit());
        noteOfVoice[v] = note;
        trigger (v);
    }

    void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); }   // string rings out
    void voiceVelocity (int v, float velocity01) override { harpVel[v] = velocity01; }

    // the Harp's bass strings ring ~8 s at the default Decay
    double voiceTailSeconds() const override { return harpMode() ? 9.0 * param (pDecay) / 2000.0 + 0.3 : param (pDecay) * 0.001 + 0.2; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);
    void renderHarp (int voice, double semis, double freq, aquanode::StereoFrame* outputs);
    bool harpMode() const { return pModel >= 0 && param (pModel) > 0.5f; }

    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
        renderGlobalMidiHits (inputs, outputs, 0);   // notes on Midi In, see ModuleCore.h
    }

    // global lane: notes arriving on Midi In play as overlapping hits
    bool acceptsGlobalMidiNotes() const override { return true; }

private:
    aquanode::ModuleVoicePool pool;

    void trigger (int v)
    {
        burstSamples[v] = -1;   // length resolved on the next sample (needs pitch)
    }

    std::vector<float> line [aquanode::kMaxVoices];
    int writeIndex [aquanode::kMaxVoices] {};
    float lpfState [aquanode::kMaxVoices] {};
    float colorState [aquanode::kMaxVoices] {};
    int burstSamples [aquanode::kMaxVoices] {};
    int noteOfVoice [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    juce::Random random;
    // the Harp model
    int pModel { -1 };
    std::unique_ptr<aquanode::harp::HarpString[]> harp;
    aquanode::studio::EqChain harpEq [aquanode::kMaxVoices];
    float harpVel [aquanode::kMaxVoices] {};
    double harpFrom [aquanode::kMaxVoices] {};
    uint64_t harpSeed { 1 };
};
