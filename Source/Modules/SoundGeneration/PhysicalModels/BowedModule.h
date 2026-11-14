#pragma once

#include "../../Core/ModuleCore.h"

// Bowed - the sustaining half of physical modelling. Pluck excites a string
// once and lets it die; this one keeps feeding energy in, so it sings for as
// long as you hold the note. The trick in both cases is the same delay line;
// what differs is the nonlinearity driving it.
//
//   Bow  - a friction curve: the bow grabs the string, the string slips free,
//          it grabs again. That stick-slip cycle is the whole violin.
//   Blow - a reed/jet table: pressure difference pushes the reed shut, which
//          cuts the pressure, which lets it open. Clarinet, roughly.
//
// Pressure is the expressive control (too little and it whispers, too much
// and it chokes), and it plays from the keys like any instrument: velocity is
// how hard the bow starts, aftertouch (or a note's own MPE pressure) leans
// into it while the note is held - After Touch sets how far. Pressure In is
// an extra: anything patched there (an LFO for tremolo bowing, an envelope for
// swells) moves the pressure by Press Mod. Position is where along the string
// you bow, which notches out harmonics; Damp is the loop low-pass, i.e. how
// bright the body is. Pairs beautifully with Voices = 1 and Glide.
// Inputs: 0 = Pressure In, 1 = Env In, 2 = Add Midi In. Output: 0 = Audio Out.
class BowedModule : public aquanode::SynthModule
{
public:
    static constexpr int maxDelaySamples = 4096;

    enum ParamIndex { pVolume = 0, pMode, pPressure, pPosition, pDamp, pPressMod, pVoices, pGlide, pAfterTouch };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    BowedModule()
    {
        for (auto& l : line)
            l.assign (maxDelaySamples, 0.0f);
        for (auto& l : bridge)
            l.assign (maxDelaySamples, 0.0f);
    }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        glide.resetVoice (v);
        gate.resetVoice (v);
        if ((int) line[v].size() == maxDelaySamples)
            std::fill (line[v].begin(), line[v].end(), 0.0f);
        if ((int) bridge[v].size() == maxDelaySamples)
            std::fill (bridge[v].begin(), bridge[v].end(), 0.0f);
        writeIndex[v] = 0;
        lpState[v] = 0.0f;
        zeroState[v] = 0.0f;
        bowSpeed[v] = 0.0f;
        bodyLp[v] = bodyHp[v] = bodyHpX[v] = 0.0f;
        dcX[v] = dcY[v] = 0.0f;
        noiseSeed[v] = 0x2545F491u * (uint32_t) (v + 1);
        velocity[v] = 1.0f;
        touch[v] = 0.0f;
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        juce::ignoreUnused (retrigger);   // legato keeps the string ringing
    }

    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        gate.noteOff (v);
    }

    // velocity goes into the bow's pressure (below), not straight into the level
    void voiceVelocity (int v, float velocity01) override { velocity[v] = velocity01; }
    double voiceTailSeconds() const override { return 0.4; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int v, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

private:
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;

    // Bow: the string as two delay lines either side of the bow (line = the
    // neck side, bridge = the bridge side), as in the McIntyre-Schumacher-
    // Woodhouse / STK bowed string; Blow: one bore (line) and a reed
    std::vector<float> line [aquanode::kMaxVoices];
    std::vector<float> bridge [aquanode::kMaxVoices];
    int writeIndex [aquanode::kMaxVoices] {};
    float lpState [aquanode::kMaxVoices] {};      // the string's / bore's loss filter
    float zeroState [aquanode::kMaxVoices] {};    // the bore's one-zero
    float bowSpeed [aquanode::kMaxVoices] {};     // the bow's (or breath's) speed, eased in and out
    float bodyLp [aquanode::kMaxVoices] {}, bodyHp [aquanode::kMaxVoices] {}, bodyHpX [aquanode::kMaxVoices] {};
    float dcX [aquanode::kMaxVoices] {}, dcY [aquanode::kMaxVoices] {};
    uint32_t noiseSeed [aquanode::kMaxVoices] {};
    float velocity [aquanode::kMaxVoices] {};
    float touch [aquanode::kMaxVoices] {};      // the aftertouch / MPE pressure, smoothed
};
