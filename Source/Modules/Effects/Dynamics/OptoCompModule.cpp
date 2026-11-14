// Opto Compressor - in the spirit of the classic tube leveling amplifiers
// with an optical cell (the LA-2A kind): smooth, slow to grab, and a release
// that depends on what it has been doing.
//
// The sidechain lights a panel; a light-dependent resistor across the signal
// follows the light. The cell is modelled as two parts in one:
//   a fast part   follows the light in about 10 ms and lets go in about 60 ms
//   a slow part   charges only under sustained light (about 1.2 s) and lets
//                 go over about 3 s
// and the gain reduction follows their blend. A short peak charges only the
// fast part, so it lets go quickly; after long, hard compression the slow
// part holds on - about half the reduction goes at once, the rest drifts back
// over seconds. That memory is the sound of these units.
//
// Peak Reduction is the sidechain's gain (there is no threshold knob: more
// Peak Reduction, more compression); Compress is a soft knee at about 3:1,
// Limit a firmer one at about 10:1. Emphasis lifts the sidechain's highs (the
// "R37" trim on the hardware), so bright sounds are held down more. Gain
// makes up the level, through a gentle tube-like stage (a little even
// harmonic colour when driven, transparent when not); Mix blends the dry
// signal back (parallel compression). Side In, if patched, drives the cell
// instead of the input. Stereo linked. Global.
//
// Inputs: 0 = Audio In, 1 = Side In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "GrMeter.h"

using namespace aquanode;

class OptoCompModule : public SynthModule
{
public:
    enum P { pPeak = 0, pGain, pMode, pEmphasis, pMix };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.opto";
        d.displayName = "Opto Compressor";
        d.description =
            "An optical leveling compressor: smooth, slow to grab, and a release that remembers - "
            "after a short peak it lets go quickly, after long hard compression half goes at once and the rest "
            "drifts back over seconds. Peak Reduction drives the cell (more, more compression); Compress about 3:1, "
            "Limit about 10:1. Emphasis lifts the sidechain's highs. Gain makes up through a gentle tube-like stage; "
            "Mix for parallel compression. Side In drives the cell instead. Needle meter: gain reduction.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 17;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("sideIn", "Side In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("peak",     "Peak Reduction", 0.0f, 100.0f, 40.0f, 0),
            makeRotary ("gain",     "Gain",     0.0f, 30.0f, 4.0f, 0, "dB"),
            makeCombo  ("mode",     "Mode",     { "Compress", "Limit" }, 0, 0, 1).noMod(),
            makeRotary ("emphasis", "Emphasis", 0.0f, 100.0f, 0.0f, 1, "%"),
            makeRotary ("mix",      "Mix",      0.0f, 100.0f, 100.0f, 1, "%")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        auto coef = [sr] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (seconds * sr))); };
        lightAtt = coef (0.002);  lightRel = coef (0.015);
        fastAtt  = coef (0.010);  fastRel  = coef (0.060);
        slowAtt  = coef (1.2);    slowRel  = coef (3.0);
        hpCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 1500.0 / sr);
        dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 5.0 / sr);
        reset();
    }

    void reset() override
    {
        light = fast = slow = 0.0f;
        hpX = hpY = 0.0f;
        for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = 0.0f;
    }

    // the static curve: gain reduction (dB, >= 0) for a cell level (dB)
    static float reductionDb (float levelDb, bool limit)
    {
        const float threshold = -12.0f, knee = limit ? 6.0f : 14.0f, ratio = limit ? 10.0f : 3.0f;
        const float over = levelDb - threshold;
        float out;
        if (2.0f * over < -knee) out = 0.0f;
        else if (2.0f * over > knee) out = over * (1.0f - 1.0f / ratio);
        else { const float t = over + knee * 0.5f; out = (1.0f - 1.0f / ratio) * t * t / (2.0f * knee); }
        return juce::jmax (0.0f, out);
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        // the sidechain: the input (or Side In), its highs lifted by Emphasis, times Peak Reduction
        const StereoFrame& key = isInputConnected (1) ? in[1] : in[0];
        const float mono = 0.5f * (key[0] + key[1]);
        hpY = hpCoef * (hpY + mono - hpX);   // a first-order high-pass at 1.5 kHz
        hpX = mono;
        const float scGain = juce::Decibels::decibelsToGain (-12.0f + 0.42f * param (pPeak));
        const float sc = (std::abs (mono) + param (pEmphasis) * 0.03f * std::abs (hpY)) * scGain;

        // the panel and the cell's two parts
        light += (sc > light ? lightAtt : lightRel) * (sc - light);
        fast += (light > fast ? fastAtt : fastRel) * (light - fast);
        slow += (light > slow ? slowAtt : slowRel) * (light - slow);
        const float cell = 0.55f * fast + 0.45f * slow;
        const float levelDb = 20.0f * std::log10 (juce::jmax (1.0e-6f, cell));
        const float grDb = reductionDb (levelDb, param (pMode) > 0.5f);
        const float gr = juce::Decibels::decibelsToGain (-grDb);
        if (grDb > meterGr.load (std::memory_order_relaxed)) meterGr.store (grDb, std::memory_order_relaxed);

        const float makeup = juce::Decibels::decibelsToGain (param (pGain));
        const float mix = param (pMix) * 0.01f;
        for (int c = 0; c < 2; ++c)
        {
            const float x = in[0][(size_t) c];
            // the tube-like make-up stage: transparent when quiet, a touch of even colour when hot
            const float v = x * gr * makeup;
            const float t = (std::tanh (0.8f * v + 0.06f) - kTanhBias) / 0.8f;
            const float y = t - dcX[c] + dcR * dcY[c];   // (its tiny offset blocked)
            dcX[c] = t;
            dcY[c] = std::isfinite (y) ? y : 0.0f;
            out[0][(size_t) c] = x * (1.0f - mix) + dcY[c] * mix;   // (the dry signal as it came)
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        return std::make_unique<GrMeter> (meterGr, juce::Colour (0xffe9d9a8), "OPTO");
    }
    int extraContentHeight() const override { return 110; }

    std::atomic<float> meterGr { 0.0f };

private:
    static inline const float kTanhBias = std::tanh (0.06f);
    float lightAtt {}, lightRel {}, fastAtt {}, fastRel {}, slowAtt {}, slowRel {};
    float light {}, fast {}, slow {};
    float hpCoef {}, hpX {}, hpY {};
    float dcR { 0.9993f }, dcX[2] {}, dcY[2] {};
};

AQUANODE_REGISTER_MODULE (OptoCompModule, OptoCompModule::descriptor)
