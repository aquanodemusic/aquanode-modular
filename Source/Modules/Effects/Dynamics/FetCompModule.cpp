// FET Compressor - in the spirit of the classic FET limiting amplifiers (the
// 1176 kind): very fast, punchy, a little dirty.
//
// Input drives the signal into a fixed threshold (there is no threshold
// knob: more Input, more compression), Output sets the level after. Attack
// from 20 microseconds (it catches the first half-cycle) to 800; Release
// from 50 ms to 1.1 s (the reduction falls at a steady dB rate, as on the
// hardware). Ratio 4, 8, 12 or 20 to 1 - the higher ones
// with a higher threshold and a firmer knee, as the buttons do - or All, the
// buttons all pressed in: the threshold drops, the ratio passes 20:1, and
// past a point the curve turns over (louder in comes out quieter), the
// attack slows a touch and the distortion grows - the famous smash.
//
// The FET is a voltage-controlled resistor, not a perfect one: the more it
// reduces, the more second harmonic it adds; the output stage is a class-A
// amplifier that rounds the very loudest peaks. Mix blends the dry signal
// back (parallel compression). Side In, if patched, is what the detector
// listens to (through Input). Stereo linked. Global.
//
// Inputs: 0 = Audio In, 1 = Side In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "GrMeter.h"

using namespace aquanode;

class FetCompModule : public SynthModule
{
public:
    enum P { pInput = 0, pOutput, pAttack, pRelease, pRatio, pMix };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.fet";
        d.displayName = "FET Compressor";
        d.description =
            "A FET limiting compressor: very fast and punchy. Input drives into a fixed threshold "
            "(more Input, more compression), Output sets the level. Attack 20 to 800 microseconds, Release 50 ms to "
            "1.1 s. Ratio 4, 8, 12, 20 - or All, every button pressed in: the curve turns over and it smashes, "
            "dirtier. The FET adds second harmonic as it works. Mix for parallel compression; Side In keys it. "
            "Needle meter: gain reduction.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 18;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("sideIn", "Side In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("input",   "Input",   0.0f, 36.0f, 6.0f, 0, "dB"),
            makeRotary ("output",  "Output",  -24.0f, 12.0f, 0.0f, 0, "dB"),
            makeRotary ("attack",  "Attack",  20.0f, 800.0f, 250.0f, 0, "us", true),
            makeRotary ("release", "Release", 50.0f, 1100.0f, 300.0f, 0, "ms", true),
            makeCombo  ("ratio",   "Ratio",   { "4:1", "8:1", "12:1", "20:1", "All" }, 0, 1, 1).noMod(),
            makeRotary ("mix",     "Mix",     0.0f, 100.0f, 100.0f, 1, "%")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 5.0 / sr);
        reset();
    }

    void reset() override
    {
        env = 0.0f;
        lastAttack = lastRelease = -1.0f;
        for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = 0.0f;
    }

    // the static curve: gain reduction (dB, >= 0) for a level (dB) at a Ratio setting
    static float reductionDb (float levelDb, int ratioIndex)
    {
        static const float kRatio[5]     = { 4.0f, 8.0f, 12.0f, 20.0f, 24.0f };
        static const float kThreshold[5] = { -12.0f, -11.0f, -10.0f, -9.0f, -16.0f };
        static const float kKnee[5]      = { 6.0f, 5.0f, 4.0f, 3.0f, 8.0f };
        const int i = juce::jlimit (0, 4, ratioIndex);
        const float over = levelDb - kThreshold[i], knee = kKnee[i], slope = 1.0f - 1.0f / kRatio[i];
        float gr;
        if (2.0f * over < -knee) gr = 0.0f;
        else if (2.0f * over > knee) gr = over * slope;
        else { const float t = over + knee * 0.5f; gr = slope * t * t / (2.0f * knee); }
        if (i == 4)   // All: past 8 dB over the curve turns over - louder in, quieter out
            gr += 0.35f * juce::jmax (0.0f, over - 8.0f);
        return juce::jmax (0.0f, gr);
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float inGain = juce::Decibels::decibelsToGain (param (pInput));
        const int ratio = juce::jlimit (0, 4, (int) std::lround (param (pRatio)));
        const bool all = ratio == 4;

        // the detector: a peak follower, stereo linked (its release, an
        // exponential in amplitude, falls at a steady dB rate)
        const StereoFrame& key = isInputConnected (1) ? in[1] : in[0];
        const float peak = juce::jmax (std::abs (key[0]), std::abs (key[1])) * inGain;
        const float attackS = param (pAttack) * 1.0e-6f * (all ? 1.4f : 1.0f);
        const float releaseS = param (pRelease) * 0.001f * (all ? 0.8f : 1.0f);
        if (attackS != lastAttack || releaseS != lastRelease)   // (worked out only when they change)
        {
            lastAttack = attackS; lastRelease = releaseS;
            aAtt = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, attackS * (float) sampleRate));
            aRel = 1.0f - std::exp (-1.0f / (releaseS * (float) sampleRate));
        }
        env += (peak > env ? aAtt : aRel) * (peak - env);

        const float grDb = reductionDb (20.0f * std::log10 (juce::jmax (1.0e-6f, env)), ratio);
        const float gr = juce::Decibels::decibelsToGain (-grDb);
        if (grDb > meterGr.load (std::memory_order_relaxed)) meterGr.store (grDb, std::memory_order_relaxed);

        // the FET's second harmonic grows with the reduction (more with All)
        const float depth = 1.0f - gr;
        const float even = (all ? 0.12f : 0.05f) * depth;
        const float outGain = juce::Decibels::decibelsToGain (param (pOutput));
        const float mix = param (pMix) * 0.01f;
        for (int c = 0; c < 2; ++c)
        {
            const float x = in[0][(size_t) c];
            const float v = x * inGain * gr;
            const float fet = v + even * v * v;
            const float y = fet - dcX[c] + dcR * dcY[c];   // (the even harmonic's offset blocked)
            dcX[c] = fet;
            dcY[c] = std::isfinite (y) ? y : 0.0f;
            // the class-A output stage: transparent until the very loudest peaks
            const float stage = dcY[c] * outGain;
            const float wet = std::abs (stage) < 0.9f ? stage : std::copysign (0.9f + 0.1f * std::tanh ((std::abs (stage) - 0.9f) * 10.0f), stage);
            out[0][(size_t) c] = x * (1.0f - mix) + wet * mix;
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        return std::make_unique<GrMeter> (meterGr, juce::Colour (0xffd8dde2), "FET");
    }
    int extraContentHeight() const override { return 110; }

    std::atomic<float> meterGr { 0.0f };

private:
    float env { 0.0f };
    float lastAttack { -1.0f }, lastRelease { -1.0f }, aAtt { 1.0f }, aRel { 0.001f };
    float dcR { 0.9993f }, dcX[2] {}, dcY[2] {};
};

AQUANODE_REGISTER_MODULE (FetCompModule, FetCompModule::descriptor)
