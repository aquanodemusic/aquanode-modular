//==============================================================================
// Noise Exciter (6.7.11) - a breath of band-passed noise that follows the
// sound: bright crunch on a sub bass, air on a pad - not distortion.
//
// The noise rides the sound. Follow: from its envelope (0 %: an even hiss
// under it, Attack / Release) to its waveform (100 %: the noise opens and
// closes with every cycle - on a bass, a buzzing crunch in step with it).
// Squash evens the noise out as OTT would: the quiet parts brought up, the
// loud held back, so it is there all the time the sound is.
//
// Where it sits: a band (Width, in octaves) between Freq - the place it
// likes, 1 kHz to begin with - and where the sound itself is (Track: how far
// it moves towards the sound's brightness, read from how fast it changes).
// Amount: how much noise; the sound itself passes untouched. Output.
// Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class NoiseExciterModule final : public SynthModule
{
public:
    enum P { pAmount = 0, pFreq, pWidth, pTrack, pFollow, pSquash, pAttack, pRelease, pOutput };

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        env = 0.0f; powX = powD = 1.0e-9f; prevMono = 0.0f; countdown = 0;
        for (int c = 0; c < 2; ++c) { s1[c] = s2[c] = s3[c] = s4[c] = 0.0f; }
        centre = 1000.0f;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const float inL = inputs[0][0], inR = inputs[0][1];
        const float mono = 0.5f * (inL + inR), a = std::abs (mono);
        const float sr = (float) sampleRate;

        // the sound's level (Attack / Release) and brightness (the RMS frequency:
        // how fast it changes against how big it is)
        if (--countdown <= 0)
        {
            countdown = 16;
            atk = 1.0f - std::exp (-1.0f / (juce::jmax (0.05f, param (pAttack)) * 0.001f * sr));
            rel = 1.0f - std::exp (-1.0f / (juce::jmax (1.0f, param (pRelease)) * 0.001f * sr));
            const float bright = sr / juce::MathConstants<float>::twoPi * std::sqrt (powD / juce::jmax (1.0e-12f, powX));
            const float track = param (pTrack) * 0.01f, pref = param (pFreq);
            const float target = juce::jlimit (150.0f, 0.42f * sr,
                                               pref * std::pow (juce::jlimit (20.0f, 16000.0f, bright) / pref, track));
            centre += (target - centre) * 0.05f;
            // the band: a TPT state-variable band-pass (Q from Width)
            const float width = juce::jlimit (0.2f, 5.0f, param (pWidth));
            const float q = std::sqrt (std::pow (2.0f, width)) / (std::pow (2.0f, width) - 1.0f);
            g = std::tan (juce::MathConstants<float>::pi * juce::jmin (centre, 0.45f * sr) / sr);
            // (two stages in a row: the skirts fall 12 dB an octave, so it stays a band and not a
            // hiss up to the top; each stage a little wider, so the pair spans Width)
            k = 1.5f / juce::jmax (0.2f, q);
            amount = param (pAmount) * 0.01f;
            follow = param (pFollow) * 0.01f;
            squash = param (pSquash) * 0.01f;
            outGain = juce::Decibels::decibelsToGain (param (pOutput));
        }
        env += (a - env) * (a > env ? atk : rel);
        powX += (mono * mono - powX) * 0.002f;
        const float dlt = mono - prevMono; prevMono = mono;
        powD += (dlt * dlt - powD) * 0.002f;

        // what the noise follows: the envelope, or the waveform itself
        float m = env * (1.0f - follow) + a * 1.57f * follow;
        // Squash: an upward / downward squeeze, as OTT does it (power law, with a
        // gate under -72 dB so silence stays silent)
        const float gate = env / (env + 2.5e-4f);
        m = 0.5f * std::pow (juce::jmax (0.0f, m) * 2.0f, 1.0f - 0.65f * squash) * gate;   // (pivoting at half scale)

        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            noise[c] = noise[c] * 1664525u + 1013904223u + (juce::uint32) c * 7919u;
            const float wn = (float) (noise[c] >> 9) / 4194304.0f - 1.0f;
            const float v1 = (g * (wn - s2[c]) + s1[c]) / (1.0f + g * (g + k));
            const float v2 = s2[c] + g * v1;
            s1[c] = 2.0f * v1 - s1[c]; s2[c] = 2.0f * v2 - s2[c];
            const float b1 = v1 * k;   // (each band-pass unity at its centre)
            const float u1 = (g * (b1 - s4[c]) + s3[c]) / (1.0f + g * (g + k));
            const float u2 = s4[c] + g * u1;
            s3[c] = 2.0f * u1 - s3[c]; s4[c] = 2.0f * u2 - s4[c];
            const float band = u1 * k;
            const float x = c == 0 ? inL : inR;
            out[c] = (x + band * m * amount * 2.2f) * outGain;
        }
        outputs[0] = { out[0], out[1] };
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.noiseexciter";
        d.displayName = "Noise Exciter";
        d.searchKeywords = "noise exciter air crunch bright sub bass enhance texture";
        d.description =
            "Band-passed noise that follows the sound - bright crunch on a sub bass, air on a pad, not distortion. Follow: "
            "the noise rides the envelope (an even hiss, Attack / Release) or the waveform (it opens and closes with every "
            "cycle: a buzzing crunch in step with a bass). Squash evens it out as OTT would. Its band (Width, octaves) sits "
            "between Freq - where it likes to be, 1 kHz to begin with - and where the sound is (Track). Amount; the sound "
            "itself passes untouched. Output.";
        d.section = ModuleSection::Effect;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("amount", "Amount", 0.0f, 100.0f, 30.0f, 0, "%", false, 1.0f),
            makeRotary ("freq", "Freq", 200.0f, 8000.0f, 1000.0f, 0, "Hz", true),
            makeRotary ("width", "Width", 0.3f, 4.0f, 1.5f, 0, "oct", false, 0.05f),
            makeRotary ("track", "Track", 0.0f, 100.0f, 10.0f, 0, "%", false, 1.0f),
            makeRotary ("follow", "Follow", 0.0f, 100.0f, 50.0f, 1, "%", false, 1.0f),
            makeRotary ("squash", "Squash", 0.0f, 100.0f, 50.0f, 1, "%", false, 1.0f),
            makeRotary ("attack", "Attack", 0.1f, 50.0f, 2.0f, 1, "ms", true),
            makeRotary ("release", "Release", 5.0f, 500.0f, 60.0f, 1, "ms", true),
            makeRotary ("output", "Output", -12.0f, 6.0f, 0.0f, 1, "dB")
        };
        return d;
    }

private:
    float env { 0.0f }, powX { 1.0e-9f }, powD { 1.0e-9f }, prevMono { 0.0f };
    float atk { 0.1f }, rel { 0.01f }, centre { 1000.0f }, g { 0.07f }, k { 1.0f };
    float amount { 0.3f }, follow { 0.5f }, squash { 0.5f }, outGain { 1.0f };
    float s1[2] {}, s2[2] {}, s3[2] {}, s4[2] {};
    juce::uint32 noise[2] { 12345u, 67890u };
    int countdown { 0 };
};

AQUANODE_REGISTER_MODULE (NoiseExciterModule, NoiseExciterModule::descriptor)
