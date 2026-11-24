#pragma once

#include "../../Core/ModuleCore.h"
#include <cstdint>

// Noise - white/pink noise source. PerVoice (in the patch's voice area):
// each held note gets its own independent noise stream, so a noise burst
// gated by an ADSR behaves per note - the classic percussion / pluck
// excitation source (try Noise -> Resonator).
// (6.9.19) Brown noise (white through a leaky integrator, -6 dB per octave
// from about 20 Hz), and an optional Filter: Low + High Cut (two resonant
// 12 dB state-variable filters, Q at both) or Band Pass, then Tilt (a shelf
// pair around 800 Hz, + brightens, - darkens). Off: the noise as before.
// Note: as a per-voice source it sounds while notes are held.
// No inputs. Output: 0 = Audio Out.
class NoiseModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pType = 0, pLevel, pVoices, pTrim, pFilter, pLowCut, pHighCut, pBandFreq, pQ, pTilt };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceNoteOn (int v, int, bool) override { pool.noteOn (v, voiceLimit()); }
    void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        seed[v] = 0x9e3779b9u * (unsigned) (v + 1);
        b0[v] = b1[v] = b2[v] = 0.0f;
        brown[v] = 0.0f;
        for (auto& s : svf[v]) s = {};
        tiltLp[v] = 0.0f;
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        brownLeak = (float) std::exp (-juce::MathConstants<double>::twoPi * 20.0 / sr);
        brownGain = std::sqrt (1.0f - brownLeak * brownLeak);   // the same loudness (RMS) as white
        tiltA = (float) std::exp (-juce::MathConstants<double>::twoPi * 800.0 / sr);
        lastKey = -1.0f;
        reset();
    }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        // voice-steal de-click: ramp out instead of cutting dead
        const float voiceGain = pool.nextGain (v, sampleRate);
        if (pool.isSilent (v))
        {
            outputs[0][0] = 0.0f;
            outputs[0][1] = 0.0f;
            return;
        }

        // fast LCG white noise, independent per voice
        seed[v] = seed[v] * 1664525u + 1013904223u;
        float white = (float) (int32_t) seed[v] / (float) 0x80000000;

        float out = white;
        if ((int) param (pType) == 1)
        {
            // pink: Paul Kellet's economy 3-pole approximation
            b0[v] = 0.99765f * b0[v] + white * 0.0990460f;
            b1[v] = 0.96300f * b1[v] + white * 0.2965164f;
            b2[v] = 0.57000f * b2[v] + white * 1.0526913f;
            out = (b0[v] + b1[v] + b2[v] + white * 0.1848f) * 0.35f;
        }
        else if ((int) param (pType) == 2)
        {
            // (6.9.19) brown: a leaky integrator of the white (a touch under white's RMS)
            brown[v] = brownLeak * brown[v] + brownGain * white;
            out = brown[v] * 0.8f;
        }

        // (6.9.19) the optional filter
        if (const int filter = (int) param (pFilter); filter > 0)
        {
            updateFilters (filter);
            if (filter == 1)
            {
                out = svfTick (svf[v][0], co[0], out, 2);   // the low cut (high-pass)
                out = svfTick (svf[v][1], co[1], out, 0);   // the high cut (low-pass)
            }
            else
                out = svfTick (svf[v][0], co[2], out, 1) * bandMakeup;   // band-pass, its peak at unity x Q's lift
            if (tiltLow != 1.0f || tiltHigh != 1.0f)
            {
                tiltLp[v] = (1.0f - tiltA) * out + tiltA * tiltLp[v];
                out = tiltLp[v] * tiltLow + (out - tiltLp[v]) * tiltHigh;
            }
        }

        out *= param (pLevel) * voiceGain;
        outputs[0][0] = out;
        outputs[0][1] = out;
    }

private:
    struct Svf { float ic1 {}, ic2 {}; };
    struct SvfCo { float a1 { 1 }, a2 {}, a3 {}, k { 1.414f }; };

    static SvfCo svfCo (double hz, double q, double sr)
    {
        SvfCo c;
        const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.49, hz) / sr);
        c.k = (float) (1.0 / juce::jmax (0.3, q));
        c.a1 = (float) (1.0 / (1.0 + g * (g + c.k)));
        c.a2 = (float) (g * c.a1);
        c.a3 = (float) (g * c.a2);
        return c;
    }
    // TPT state-variable filter: 0 low-pass, 1 band-pass (normalised: unity peak), 2 high-pass
    static float svfTick (Svf& s, const SvfCo& c, float x, int kind)
    {
        const float v3 = x - s.ic2;
        const float v1 = c.a1 * s.ic1 + c.a2 * v3;
        const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        return kind == 0 ? v2 : kind == 1 ? c.k * v1 : x - c.k * v1 - v2;
    }

    // the coefficients, worked out again only when a knob moved (all voices share them)
    void updateFilters (int filter)
    {
        const float lc = param (pLowCut), hc = param (pHighCut), bf = param (pBandFreq), q = param (pQ), t = param (pTilt);
        const float key = lc + hc * 1.0001f + bf * 1.0003f + q * 7.0f + t * 13.0f + (float) filter * 101.0f;
        if (key == lastKey) return;
        lastKey = key;
        co[0] = svfCo (filter == 1 ? lc : bf, q, sampleRate);
        co[1] = svfCo (hc, q, sampleRate);
        co[2] = co[0];
        bandMakeup = std::sqrt (juce::jmax (0.5f, q));   // a narrow band would be quiet: some of it back
        tiltLow = juce::Decibels::decibelsToGain (-t);
        tiltHigh = juce::Decibels::decibelsToGain (t);
    }

    Svf svf [aquanode::kMaxVoices][2] {};
    float tiltLp [aquanode::kMaxVoices] {}, brown [aquanode::kMaxVoices] {};
    SvfCo co[3];
    float lastKey { -1.0f }, bandMakeup { 1.0f }, tiltLow { 1.0f }, tiltHigh { 1.0f };
    float brownLeak { 0.9974f }, brownGain { 0.072f }, tiltA { 0.9f };
    aquanode::ModuleVoicePool pool;
    uint32_t seed [aquanode::kMaxVoices] { };
    float b0 [aquanode::kMaxVoices] { }, b1 [aquanode::kMaxVoices] { }, b2 [aquanode::kMaxVoices] { };
};
