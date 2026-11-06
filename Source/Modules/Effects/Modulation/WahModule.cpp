//==============================================================================
// Wah - a resonant band-pass swept between Low and High
//
// The pedal position (0 = heel, Low; 1 = toe, High) comes from one of three
// places: Auto follows the input's loudness (an envelope follower with its own
// attack and release, scaled by Sensitivity - the funk/quack wah), LFO sweeps
// it at Rate or at a tempo division, Pedal is the Position knob. The Pedal In
// cable adds to whichever it is (an LFO, an envelope, the mod wheel through
// Midi Controls), so a cable on Pedal In with Mode on Pedal is a hand-held wah.
//
// The filter is a zero-delay state-variable band-pass, swept on a log scale
// and lifted at its peak as a pedal's is, with a touch of the low-pass under it
// so the heel position keeps some body (as an inductor wah does). Per channel,
// one sweep for both, so the stereo image stays put.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class WahModule final : public SynthModule
{
public:
    enum P { pMode = 0, pPosition, pLow, pHigh, pResonance, pSens, pAttack, pRelease,
             pRate, pDivision, pDepth, pModDepth, pMix };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.wah";
        d.displayName = "Wah";
        d.description = "A resonant band-pass swept between Low and High, like a wah pedal. Auto opens it with the "
                        "input's loudness (Sensitivity, Attack, Release: the funky quack), LFO sweeps it at Rate or a "
                        "tempo division, Pedal is the Position knob. Pedal In adds a cable's sweep to any of them - "
                        "an envelope, an LFO or the mod wheel.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 4;
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("pedalIn", "Pedal In"),
                      audioOut ("audioOut", "Audio Out"), modOut ("positionOut", "Position Out") };

        juce::StringArray divisions { "Free" };
        divisions.addArray (seqDivisionChoices());
        d.params = {
            makeCombo  ("mode",      "Mode",        { "Auto", "LFO", "Pedal" }, 0, 0, 2),
            makeRotary ("position",  "Position",    0.0f, 100.0f, 50.0f, 0, "%").visibleWhen ("mode", 2.0f),
            makeRotary ("low",       "Low",         150.0f, 1200.0f, 380.0f, 0, "Hz", true),
            makeRotary ("high",      "High",        800.0f, 6000.0f, 2200.0f, 0, "Hz", true),
            makeRotary ("resonance", "Resonance",   0.7f, 20.0f, 6.0f, 1, "", true),
            makeRotary ("sens",      "Sensitivity", 0.0f, 100.0f, 60.0f, 1, "%").visibleWhen ("mode", 0.0f),
            makeRotary ("attack",    "Attack",      1.0f, 200.0f, 8.0f, 1, "ms", true).visibleWhen ("mode", 0.0f),
            makeRotary ("release",   "Release",     10.0f, 1500.0f, 180.0f, 1, "ms", true).visibleWhen ("mode", 0.0f),
            makeRotary ("rate",      "Rate",        0.05f, 12.0f, 1.5f, 1, "Hz", true).visibleWhen ("mode", 1.0f),
            makeCombo  ("division",  "Sync",        divisions, 0, 1, 2).visibleWhen ("mode", 1.0f),
            makeRotary ("depth",     "Depth",       0.0f, 100.0f, 100.0f, 2, "%"),
            makeRotary ("modDepth",  "Pedal In",    -100.0f, 100.0f, 100.0f, 2, "%"),
            makeRotary ("mix",       "Dry/Wet",     0.0f, 100.0f, 100.0f, 2, "%")
        };
        return d;
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        smoothCoef = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));
        reset();
    }

    void reset() override
    {
        for (auto& s : ic1) s = 0.0f;
        for (auto& s : ic2) s = 0.0f;
        env = 0.0f;
        smoothPos = 0.0f;
        phase = 0.0;
        lastA = lastR = -1.0f;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int mode = (int) param (pMode);
        const float depth = param (pDepth) * 0.01f;

        // the pedal position, 0..1
        float pos = 0.0f;
        if (mode == 0)
        {
            const float a = param (pAttack), r = param (pRelease);
            if (a != lastA || r != lastR)
            {
                lastA = a; lastR = r;
                attackCoef  = 1.0f - std::exp (-1.0f / (a * 0.001f * (float) sampleRate));
                releaseCoef = 1.0f - std::exp (-1.0f / (r * 0.001f * (float) sampleRate));
            }
            const float level = 0.5f * (std::abs (in[0][0]) + std::abs (in[0][1]));
            env += (level > env ? attackCoef : releaseCoef) * (level - env);
            const float sens = param (pSens) * 0.01f;
            // ~-40 dB at full sensitivity already opens it half way
            pos = juce::jlimit (0.0f, 1.0f, env * (0.5f + 30.0f * sens * sens));
        }
        else if (mode == 1)
        {
            const int div = (int) param (pDivision);
            const double hz = div == 0 ? (double) param (pRate)
                                       : tempoBpm / 60.0 / seqDivisionBeats (div - 1);
            phase += hz / sampleRate;
            phase -= std::floor (phase);
            pos = 0.5f - 0.5f * (float) std::cos (phase * juce::MathConstants<double>::twoPi);
        }
        else
        {
            pos = param (pPosition) * 0.01f;
        }
        pos *= depth;
        pos = juce::jlimit (0.0f, 1.0f, pos + in[1][0] * param (pModDepth) * 0.01f);

        // a few ms of smoothing so a stepped cable or knob never clicks
        smoothPos += smoothCoef * (pos - smoothPos);
        out[1][0] = out[1][1] = smoothPos;

        const float lo = param (pLow), hi = juce::jmax (lo + 10.0f, param (pHigh));
        const float hz = juce::jmin ((float) sampleRate * 0.45f, lo * std::pow (hi / lo, smoothPos));
        const float g = std::tan (juce::MathConstants<float>::pi * hz / (float) sampleRate);
        const float k = 1.0f / param (pResonance);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float wet = param (pMix) * 0.01f;

        for (size_t c = 0; c < 2; ++c)
        {
            const float x = in[0][c];
            const float v3 = x - ic2[c];
            const float v1 = a1 * ic1[c] + a2 * v3;
            const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = 2.0f * v1 - ic1[c];
            ic2[c] = 2.0f * v2 - ic2[c];
            const float y = k * v1 * 2.2f + v2 * 0.15f;   // a pedal's peak boost
            out[0][c] = x + wet * (y - x);
        }
    }

private:
    std::array<float, 2> ic1 {}, ic2 {};
    float env { 0.0f }, smoothPos { 0.0f }, smoothCoef { 0.004f };
    float attackCoef { 0.01f }, releaseCoef { 0.001f }, lastA { -1.0f }, lastR { -1.0f };
    double phase { 0.0 };
};

AQUANODE_REGISTER_MODULE (WahModule, WahModule::descriptor)
