// Multiband Delay - the input split into three bands (Linkwitz-Riley, so they
// rejoin flat), each echoing on its own: its own time, feedback and level.
// Lows can thump on the beat while the highs scatter in dotted eighths.
// Tempo snaps every time to the nearest note length at the patch's tempo;
// Spread pushes the right side's echoes later for width.
// Global. Input: 0 = Audio In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../Shared/FxKit.h"

using namespace aquanode;

class MultibandDelayModule : public SynthModule
{
public:
    enum ParamIndex { pLowTime = 0, pMidTime, pHighTime, pSync,
                      pLowFb, pMidFb, pHighFb,
                      pLowLvl, pMidLvl, pHighLvl,
                      pLowX, pHighX, pSpread, pDryWet };

    static constexpr double kMaxSeconds = 2.2;

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int n = (int) (kMaxSeconds * 1.2 * sr) + 8;
        for (auto& b : bands)
        {
            for (auto& d : b.line) d.prepare (n);
            for (auto& s : b.time) s.prepare (sr, 0.08f);
        }
        reset();
    }

    void reset() override
    {
        for (auto& c : split1) c.reset();
        for (auto& c : split2) c.reset();
        for (auto& c : lowAp) c.reset();
        for (auto& b : bands)
        {
            for (auto& d : b.line) d.reset();
            for (int c = 0; c < 2; ++c) { b.fb[c] = 0.0f; b.time[c].snap (-1.0f); }
        }
        countdown = 0;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        // crossover settings, every 32 samples
        if (--countdown <= 0)
        {
            countdown = 32;
            const float lo = param (pLowX);
            const float hi = juce::jmax (lo * 1.5f, param (pHighX));
            for (int c = 0; c < 2; ++c)
            {
                split1[c].set (sampleRate, lo);
                split2[c].set (sampleRate, hi);
                lowAp[c].set (sampleRate, hi);
            }
            const bool sync = param (pSync) >= 0.5f;
            const float spread = param (pSpread) * 0.01f;
            for (int b = 0; b < 3; ++b)
            {
                double secs = param (pLowTime + b) * 0.001;
                if (sync) secs = fxkit::snapToDivision (secs, juce::jlimit (20.0, 400.0, tempoBpm));
                secs = juce::jlimit (0.001, kMaxSeconds, secs);
                target[b][0] = (float) (secs * sampleRate);
                target[b][1] = (float) (juce::jmin (kMaxSeconds * 1.15, secs * (1.0 + 0.15 * spread)) * sampleRate);
                fbGain[b] = juce::jlimit (0.0f, 0.97f, param (pLowFb + b) * 0.01f);
                level[b] = param (pLowLvl + b) * 0.01f;
            }
            wet = param (pDryWet) * 0.01f;
        }

        float out[2] {};
        for (int c = 0; c < 2; ++c)
        {
            const float x = inputs[0][c];
            float low, rest, mid, high;
            split1[c].process (x, low, rest);
            split2[c].process (rest, mid, high);
            low = lowAp[c].allpass (low);
            const float parts[3] = { low, mid, high };
            float w = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                auto& band = bands[b];
                float& t = band.time[c].value;
                if (t < 0.0f) band.time[c].snap (target[b][c]);
                const float delayed = band.line[c].read (band.time[c].next (target[b][c]));
                // a soft ceiling in the loop: high feedback can't run away
                const float in = parts[b] + std::tanh (delayed * fbGain[b] * 0.8f) * 1.25f;
                band.line[c].push (fxkit::flushDenormal (in));
                w += delayed * level[b];
            }
            out[c] = x * (1.0f - wet) + w * wet;
        }
        outputs[0] = { out[0], out[1] };
    }

private:
    struct Band
    {
        fxkit::DelayLine line[2];
        fxkit::Smoother time[2];
        float fb[2] {};
    };
    Band bands[3];
    fxkit::LR4 split1[2], split2[2], lowAp[2];
    float target[3][2] {}, fbGain[3] {}, level[3] { 1.0f, 1.0f, 1.0f };
    float wet { 0.3f };
    int countdown { 0 };
};

static ModuleDescriptor multibandDelayDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.multibanddelay";
    d.displayName = "Multiband Delay";
    d.description =
        "Three bands (Low, Mid, High, split where the X knobs say), each echoing on its own: its own time, "
        "feedback and level - a bass that repeats on the beat under highs that scatter. Tempo snaps every "
        "time to the nearest note length at the patch's tempo; Spread puts the right side's echoes later.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 43;
    d.searchKeywords = "echo bands split crossover";
    d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"), tempoIn() };
    d.params = {
        makeRotary ("lowTime",  "Low Time",  1.0f, 2000.0f, 500.0f, 0, "ms", true),
        makeRotary ("midTime",  "Mid Time",  1.0f, 2000.0f, 375.0f, 0, "ms", true),
        makeRotary ("highTime", "High Time", 1.0f, 2000.0f, 250.0f, 0, "ms", true),
        makeCombo  ("sync",     "Time", { "Free", "Tempo" }, 0, 0, 2),
        makeRotary ("lowFb",    "Low Fdbk",  0.0f, 95.0f, 25.0f, 1, "%"),
        makeRotary ("midFb",    "Mid Fdbk",  0.0f, 95.0f, 40.0f, 1, "%"),
        makeRotary ("highFb",   "High Fdbk", 0.0f, 95.0f, 55.0f, 1, "%"),
        makeRotary ("lowLvl",   "Low Level", 0.0f, 150.0f, 70.0f, 2, "%"),
        makeRotary ("midLvl",   "Mid Level", 0.0f, 150.0f, 100.0f, 2, "%"),
        makeRotary ("highLvl",  "High Level", 0.0f, 150.0f, 100.0f, 2, "%"),
        makeRotary ("lowX",     "Low X",     40.0f, 1000.0f, 250.0f, 3, "Hz", true),
        makeRotary ("highX",    "High X",    800.0f, 12000.0f, 3000.0f, 3, "Hz", true),
        makeRotary ("spread",   "Spread",    0.0f, 100.0f, 35.0f, 3, "%"),
        makeRotary ("dryWet",   "Dry/Wet",   0.0f, 100.0f, 35.0f, 3, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MultibandDelayModule, multibandDelayDescriptor)
