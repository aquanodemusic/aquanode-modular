#pragma once

#include "../../Core/ModuleCore.h"

// KeyTrack - outputs each voice's note pitch as a modulation signal, scaled
// so that patching it into a filter's Cutoff Mod at 100% Mod Depth gives
// exact 1-octave-per-octave keyboard tracking (out = (note - center) / 60,
// and the filters' exponential curve is 2^(mod * 5) octaves).
// No inputs. Output: 0 = Modulation Out (bipolar).
class KeyTrackModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pCenter = 0 };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void reset() override
    {
        for (auto& n : noteOfVoice) n = 60.0f;
    }

    void voiceReset (int v) override { noteOfVoice[v] = 60.0f; }
    // (6.9.14) the note's pitch as tuned: a Microtuning (Global) or an MTS-ESP master moves it
    // too, so a filter keeps tracking what sounds (in 12-TET at A4 = 440 it is the key itself)
    void voiceNoteOn (int v, int note, bool) override
    {
        const double hz = aquanode::tuning::noteToHz ((double) note);
        noteOfVoice[v] = hz > 0.0 ? (float) (69.0 + 12.0 * std::log2 (hz / 440.0)) : (float) note;
    }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        const float out = (noteOfVoice[v] - param (pCenter)) / 60.0f;
        outputs[0][0] = out;
        outputs[0][1] = out;
    }

private:
    float noteOfVoice [aquanode::kMaxVoices] { };
};
