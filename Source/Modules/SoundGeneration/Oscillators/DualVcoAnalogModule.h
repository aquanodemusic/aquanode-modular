#pragma once

#include "../../Core/ModuleCore.h"
#include "../../Shared/AntiAlias.h"
#include "../../Shared/VintageDSP.h"

// Dual VCO - two engines in one card (Engine):
//
// Oscillators: the classic Dual VCO - two analog-style oscillators and their
// mixer. VCO 1 Triangle / Saw / Pulse (with its PW) / Noise at 32' 16' 8' 4';
// VCO 2 Saw / Pulse 50% / Pulse 10% / Ring Mod at 16' 8' 4' 2', with its own
// +-12 semitone Pitch. No gate: they run for as long as the voice lives, so
// the VCA belongs after the filter (Volume with an Analog EG in Vol Mod).
// Pitch In (semitones x Pitch Mod) takes an MG or an envelope.
//
// Synth: a whole two-oscillator analog polysynth voice in one
// card, voiced after the classic free JX-style VST synths: two oscillators
// (Saw, Sine or Triangle) where the second is subtracted from the first (so
// two saws at the same pitch, half a cycle apart, make a pulse, and a little
// PWM moves its width), noise, a resonant two-pole low-pass that tracks the
// keys and softly limits its own resonance, a filter envelope, an amp
// envelope, one free-running LFO for vibrato, PWM and the cutoff, and
// glide: Glide Bend starts every note that many semitones off and slides it
// in, and with Glide Mode Always each note slides from the last one played,
// even in poly - the swooping lead of the early-90s rave records (the Rave
// Lead preset, the card's default).
//
// VCO / DCO: as VCOs the oscillators run free (every note catches them at
// some phase) and drift - each voice a little out of tune with the others,
// wandering slowly, its filter and envelopes a touch different (Drift sets
// how much). As DCOs every note starts the same, in perfect tune: the
// controlled, steady analog sound.
//
// The presets (< and >, at the top) set every knob of the Synth engine,
// starting from Init; the title shows the last one picked.
// Inputs: 0 = Pitch In (Oscillators), 1 = Add Midi In. Output: 0 = Audio Out.
// (The old Dual VCO module's patches open on the Oscillators engine.)
class DualVcoAnalogModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pWave1 = 0, pWave2, pOscMix, pTune2, pFine2, pOctave, pNoise, pMode, pDrift,
        pCutoff, pReso, pEnvAmt, pKeyTrack, pVelCut, pLfoCut,
        pFAtt, pFDec, pFSus, pFRel,
        pAtt, pDec, pSus, pRel, pVelAmp,
        pLfoRate, pVibrato, pPwm, pGlide, pGlideBend, pGlideMode,
        pVoices, pPrev, pNext, pPreset, pLevel, pOutTrim,
        // the Oscillators engine
        pEngine, pOWave1, pOPw1, pOScale1, pOWave2, pOPitch2, pOScale2, pOFine,
        pOLevel1, pOLevel2, pOAnalog, pOPitchMod, pOGlide
    };
    bool oscEngine() const { return std::lround (param (pEngine)) == 1; }

    struct Preset;
    static const Preset* presets();
    static int numPresets();

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override;
    void reset() override;
    void voiceReset (int v) override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceVelocity (int v, float velocity01) override { vel[v] = velocity01; }
    void voiceNoteOff (int v) override;
    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    double voiceTailSeconds() const override;

    void uiButtonClicked (const juce::String& paramId) override;
    juce::String titleSuffix() const override;

    void applyPreset (int index);   // message thread

private:
    struct Env
    {
        enum Stage { Idle, Attack, Decay, Release };
        float value { 0.0f };
        Stage stage { Idle };
        void start() noexcept { stage = Attack; }
        void release() noexcept { if (stage != Idle) stage = Release; }
        // the classic exponential RC envelope: the attack charges toward a target
        // above the top and turns at 1, decay and release fall toward theirs
        float next (float att, float dec, float sus, float rel) noexcept
        {
            switch (stage)
            {
                case Attack:  value += att * (1.6f - value); if (value >= 1.0f) { value = 1.0f; stage = Decay; } break;
                case Decay:   value += dec * (sus - value); break;
                case Release: value += rel * (0.0f - value); if (value < 1.0e-5f) { value = 0.0f; stage = Idle; } break;
                case Idle:    break;
            }
            return value;
        }
    };

    float coefFor (float ms, float scale) const noexcept
    {
        return 1.0f - std::exp (-1.0f / (juce::jmax (0.05f, ms * scale) * 0.001f * (float) sampleRate));
    }

    aquanode::ModuleVoicePool pool;

    double phase1[aquanode::kMaxVoices] {}, phase2[aquanode::kMaxVoices] {};
    float vel[aquanode::kMaxVoices] {};
    int note[aquanode::kMaxVoices] {};
    float glideCur[aquanode::kMaxVoices] {};     // semitones, relative to the note
    bool active[aquanode::kMaxVoices] {};
    Env ampEnv[aquanode::kMaxVoices], filtEnv[aquanode::kMaxVoices];
    float lp[aquanode::kMaxVoices] {}, bp[aquanode::kMaxVoices] {};
    double lfoPhase[aquanode::kMaxVoices] {};
    uint32_t noiseSeed[aquanode::kMaxVoices] {};

    // the Oscillators engine
    vintagedsp::DualVco vco[aquanode::kMaxVoices];
    aquanode::ModuleGlide oGlide;
    uint32_t noteCounter { 0 };

    // the analog part: a voice's own static offsets and its slow drift
    float detune1[aquanode::kMaxVoices] {}, detune2[aquanode::kMaxVoices] {};
    float cutOffset[aquanode::kMaxVoices] {}, envScale[aquanode::kMaxVoices] {};
    float drift1[aquanode::kMaxVoices] {}, drift2[aquanode::kMaxVoices] {};
    float driftT1[aquanode::kMaxVoices] {}, driftT2[aquanode::kMaxVoices] {};
    int driftCount[aquanode::kMaxVoices] {};

    aquanode::Decimator2x aaDown [aquanode::kMaxVoices];   // (6.9.21: CPU > Anti-alias)
    bool aaWasOn [aquanode::kMaxVoices] {};
    juce::Random rng;
public:
   #if AQUANODE_TEST_HARNESS
    void seedForTest (juce::int64 seed) { rng.setSeed (seed); }
   #endif
private:
    int lastNote { -1 };
    int heldNotes { 0 };
    juce::String presetName;
};
