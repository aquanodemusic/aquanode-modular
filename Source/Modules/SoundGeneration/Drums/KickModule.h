#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/SnapDrums.h"

// Kick - classic analog kick voice: sine with an exponential pitch-drop envelope,
// a click transient, and tanh drive. Fires on rising edges at Trig In
// (Step Seq / Euclid / Clock gates) AND on MIDI note-on when played from the
// keyboard (Flexible: per-voice with notes, global when sequencer-driven).
// Input: 0 = Trig In. Output: 0 = Audio Out.
class KickModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTune = 0, pDrop, pPitchDecay, pDecay, pClick, pDrive };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        pModel = paramIndex ("model");
        for (auto& k : snapKick) k.prepare ((float) sr);
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
        ampEnv[v] = pitchEnv[v] = 0.0f;
        phase[v] = 0.0;
        clickSamples[v] = 0;
        lastTrig[v] = 0.0f;
        snapKick[v].reset();
    }

    void voiceNoteOn (int v, int, bool) override { pool.noteOn (v, voiceLimit()); trigger (v); }

    double voiceTailSeconds() const override
    {
        if (snapModel()) return 0.405 * param (pDecay) / 400.0 + 0.05;   // held 75 ms, then 0.29 dB / ms (at the default Decay)
        return aquanode::decayTailSeconds (juce::jmax (5.0f, param (pDecay))) + 0.05;
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

    bool snapModel() const { return pModel >= 0 && param (pModel) > 0.5f; }

    void trigger (int v)
    {
        if (snapModel())
        {
            // the Snap Kick: the knobs scale the measured kick-clap (the defaults play it as measured)
            aquanode::snapdrums::SnapKick::Shape sh;
            sh.tuneHz = param (pTune) * 0.9f;
            sh.drop = param (pDrop) / 24.0f;
            sh.sweepTime = juce::jmax (0.05f, param (pPitchDecay) / 40.0f);
            sh.length = juce::jmax (0.05f, param (pDecay) / 400.0f);
            sh.snap = param (pClick) / 30.0f;
            sh.drive = param (pDrive) * 0.007f;
            snapKick[v].trigger (sh);
        }
        ampEnv[v] = 1.0f;
        pitchEnv[v] = 1.0f;
        phase[v] = 0.0;
        clickSamples[v] = (int) (0.0015 * sampleRate);   // 1.5 ms click burst
    }

    float ampEnv   [aquanode::kMaxVoices] {};
    float pitchEnv [aquanode::kMaxVoices] {};
    double phase   [aquanode::kMaxVoices] {};
    int clickSamples [aquanode::kMaxVoices] {};
    float lastTrig [aquanode::kMaxVoices] {};
    juce::Random random;
    int pModel { -1 };
    aquanode::snapdrums::SnapKick snapKick [aquanode::kMaxVoices];
};
