//==============================================================================
// Sidechain Comp - a compressor keyed by another sound
//
// The classic pumping duck: the level of Side In (the key - a kick, say)
// pulls the gain of Audio In down. A feed-forward stereo-linked compressor
// with a soft knee whose detector listens to the key (high-passed by Key HP,
// so a bass key does not over-trigger), or to Audio In itself while nothing
// is patched into Side In. Lookahead delays the audio a little so the duck is
// already down when the key hits. Gain Out is the gain it applies (1 = none,
// lower while ducking) to duck anything else with the same key - patch it
// into a knob. Listen plays the key, to set Key HP by ear.
//
// The key can come from anywhere in the patch: across a big patch, a Reroute
// Send by the kick and a Reroute Return by this module's Side In.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class SidechainCompModule final : public SynthModule
{
public:
    enum P { pThreshold, pRatio, pAttack, pRelease, pKnee, pMakeup, pKeyHp, pLookahead, pMix, pListen };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.sidechaincomp";
        d.displayName = "Sidechain Comp";
        d.description = "Ducks Audio In by the level of Side In (the key - a kick, say), for pumping pads and "
                        "basses: a soft-knee compressor whose detector hears the key, high-passed by Key HP (or "
                        "Audio In itself while Side In is free). Lookahead has the duck down as the key hits. Gain "
                        "Out is the gain it applies, to duck other things by the same key. Listen plays the key. "
                        "A Reroute Send / Return pair brings a key from anywhere in the patch.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 17;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("sideIn", "Side In"),
                      audioOut ("audioOut", "Audio Out"), modOut ("gainOut", "Gain Out") };
        d.params = {
            makeRotary ("threshold", "Threshold", -60.0f, 0.0f, -24.0f, 0, "dB"),
            makeRotary ("ratio", "Ratio", 1.0f, 20.0f, 6.0f, 0, ":1", true),
            makeRotary ("attack", "Attack", 0.1f, 100.0f, 2.0f, 0, "ms", true),
            makeRotary ("release", "Release", 10.0f, 2000.0f, 180.0f, 0, "ms", true),
            makeRotary ("knee", "Knee", 0.0f, 24.0f, 6.0f, 0, "dB"),

            makeRotary ("makeup", "Makeup", 0.0f, 24.0f, 0.0f, 1, "dB"),
            makeRotary ("keyHp", "Key HP", 20.0f, 1000.0f, 20.0f, 1, "Hz", true),
            makeRotary ("lookahead", "Lookahead", 0.0f, 10.0f, 0.0f, 1, "ms"),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 1, "%"),
            makeCombo ("listen", "Listen", { "Audio", "Key" }, 0, 2, 2)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        delaySize = (int) std::ceil (sr * 0.011) + 2;
        for (auto& line : delay)
            line.assign ((size_t) delaySize, 0.0f);
        reset();
    }

    void reset() override
    {
        for (auto& line : delay)
            std::fill (line.begin(), line.end(), 0.0f);
        writePos = 0;
        envDb = 0.0f;
        hpState = { 0.0f, 0.0f };
        hpLastIn = { 0.0f, 0.0f };
        lastHp = -1.0f;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const bool keyed = isInputConnected (1);
        const StereoFrame& key = keyed ? in[1] : in[0];

        // the key, high-passed (one pole) so a low key does not over-trigger
        const float hpHz = param (pKeyHp);
        if (hpHz != lastHp)
        {
            lastHp = hpHz;
            hpCoeff = std::exp (-juce::MathConstants<float>::twoPi * hpHz / (float) sampleRate);
        }
        StereoFrame k;
        for (size_t c = 0; c < 2; ++c)
        {
            hpState[c] = hpCoeff * (hpState[c] + key[c] - hpLastIn[c]);
            hpLastIn[c] = key[c];
            k[c] = hpState[c];
        }

        // peak detector in dB, attack / release smoothed
        const float peak = juce::jmax (std::abs (k[0]), std::abs (k[1]), 1.0e-9f);
        const float levelDb = juce::Decibels::gainToDecibels (peak, -120.0f);
        const float threshold = param (pThreshold), ratio = juce::jmax (1.0f, param (pRatio));
        const float knee = param (pKnee);

        // the gain computer (soft knee): how far to pull down, in dB
        const float over = levelDb - threshold;
        float reduction = 0.0f;
        if (knee > 0.0f && std::abs (over) <= knee * 0.5f)
            reduction = (1.0f / ratio - 1.0f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        else if (over > 0.0f)
            reduction = (1.0f / ratio - 1.0f) * over;

        const float attackMs = param (pAttack), releaseMs = param (pRelease);
        const float coeff = reduction < envDb
                              ? std::exp (-1.0f / (0.001f * attackMs * (float) sampleRate))
                              : std::exp (-1.0f / (0.001f * releaseMs * (float) sampleRate));
        envDb = reduction + coeff * (envDb - reduction);

        const float gain = juce::Decibels::decibelsToGain (envDb + param (pMakeup));
        const float mix = param (pMix) / 100.0f;

        // the audio, delayed by the lookahead
        const int la = juce::jlimit (0, delaySize - 1, (int) (param (pLookahead) * 0.001f * (float) sampleRate));
        StereoFrame dry;
        for (size_t c = 0; c < 2; ++c)
        {
            delay[c][(size_t) writePos] = in[0][c];
            int r = writePos - la;
            if (r < 0) r += delaySize;
            dry[c] = delay[c][(size_t) r];
        }
        if (++writePos >= delaySize)
            writePos = 0;

        if (param (pListen) > 0.5f)
        {
            out[0] = k;
        }
        else
        {
            for (size_t c = 0; c < 2; ++c)
            {
                const float y = dry[c] * (1.0f - mix) + dry[c] * gain * mix;
                out[0][c] = std::isfinite (y) ? y : 0.0f;
            }
        }
        const float g = juce::Decibels::decibelsToGain (envDb);
        out[1] = { g, g };
    }

private:
    std::array<std::vector<float>, 2> delay;
    int delaySize { 1 }, writePos { 0 };
    float envDb { 0.0f }, hpCoeff { 0.0f }, lastHp { -1.0f };
    StereoFrame hpState {}, hpLastIn {};
};

AQUANODE_REGISTER_MODULE (SidechainCompModule, SidechainCompModule::descriptor)
