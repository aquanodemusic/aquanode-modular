// Beat Repeat - catches a slice of the sound on the beat and stutters it.
// Every Interval (on the 16th Offset says) it rolls Chance; on a hit it takes
// a slice Grid long and repeats it for Gate sixteenths, each repeat Decay
// quieter and Pitch semitones lower, through a Filter. Variation lets the
// grid jump to other lengths. Mix layers the repeats over the sound, Insert
// mutes the sound while they play, Gate leaves only the repeats. It counts
// beats at the patch's tempo from when it starts; a cable into Trig In fires
// a repeat at once and lines its bar up with that moment (a Clock keeps it
// on the song's grid).
// Global. Inputs: 0 = Audio In, 1 = Trig In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../Shared/FxKit.h"

using namespace aquanode;

namespace beatrepeat
{
    inline const juce::StringArray& intervalNames()
    {
        static const juce::StringArray n { "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars" };
        return n;
    }
    inline double intervalQuarters (int i)
    {
        static const double q[] = { 1.0, 2.0, 4.0, 8.0, 16.0 };
        return q[juce::jlimit (0, 4, i)];
    }
    inline const juce::StringArray& gridNames()
    {
        static const juce::StringArray n { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
        return n;
    }
    inline double gridQuarters (int i)
    {
        static const double q[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
        return q[juce::jlimit (0, 7, i)];
    }
}

class BeatRepeatModule : public SynthModule
{
public:
    enum ParamIndex { pInterval = 0, pOffset, pChance, pGrid, pVariation, pGate,
                      pPitch, pDecay, pMode, pFilter, pVolume };

    static constexpr double kBufferSeconds = 12.0;

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (auto& b : buf) b.assign ((size_t) (kBufferSeconds * sr) + 16, 0.0f);
        reset();
    }

    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        write = 0;
        clock = 0.0;
        repeating = false;
        dryGain = 1.0f;
        lastTrig = 0.0f;
        for (auto& f : filt) f.reset();
    }

    // its beat clock runs only while it runs: after a long silence it sleeps
    // (sleep on silence) at most after half a minute
    double silenceMemorySeconds() const override { return 29.0; }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const double sr = sampleRate;
        const double bpm = juce::jlimit (20.0, 400.0, tempoBpm);
        const double quarter = 60.0 / bpm * sr;
        const double sixteenth = quarter * 0.25;
        const double interval = beatrepeat::intervalQuarters ((int) param (pInterval)) * quarter;
        const double offset = juce::jlimit (0.0, interval - 1.0, (double) (int) param (pOffset) * sixteenth);

        // record
        const int n = (int) buf[0].size();
        buf[0][(size_t) write] = inputs[0][0];
        buf[1][(size_t) write] = inputs[0][1];

        // Trig In: a repeat now, and the bar lined up with it
        const float trig = inputs[1][0];
        const bool trigEdge = trig > 0.5f && lastTrig <= 0.5f;
        lastTrig = trig;

        bool start = false;
        if (trigEdge)
        {
            clock = offset;
            start = true;
        }
        else
        {
            const double before = clock;
            clock += 1.0;
            if (clock >= interval) clock -= interval;
            if (before < offset && clock >= offset) start = rng.uni() * 100.0f < param (pChance);
            else if (offset == 0.0 && clock < before) start = rng.uni() * 100.0f < param (pChance);
        }

        if (start)
        {
            repeating = true;
            captureStart = write;
            sliceIndex = 0;
            slicePhase = 0.0;
            gateLeft = juce::jmax (1.0, (double) (int) param (pGate) * sixteenth);
            sliceLen = pickGrid (quarter);
        }

        float rl = 0.0f, rr = 0.0f;
        if (repeating)
        {
            const float pitch = param (pPitch);
            const float decay = param (pDecay) * 0.01f;
            const double rate = std::pow (2.0, -(double) pitch * (double) sliceIndex / 12.0);
            const double src = slicePhase * rate;
            const double pos = (double) captureStart + src;
            const int i0 = (int) std::floor (pos);
            const float t = (float) (pos - (double) i0);
            const int a = ((i0 % n) + n) % n, b = (a + 1) % n;
            const float amp = std::pow (1.0f - decay, (float) sliceIndex);
            // short fades at both ends of each slice: no clicks
            const double fadeN = juce::jmin (sliceLen * 0.25, 0.003 * sr);
            const float fade = (float) juce::jmin (1.0, slicePhase / fadeN, (sliceLen - slicePhase) / fadeN);
            const float g = amp * juce::jmax (0.0f, fade);
            rl = (buf[0][(size_t) a] * (1.0f - t) + buf[0][(size_t) b] * t) * g;
            rr = (buf[1][(size_t) a] * (1.0f - t) + buf[1][(size_t) b] * t) * g;

            slicePhase += 1.0;
            if (slicePhase >= sliceLen)
            {
                slicePhase = 0.0;
                ++sliceIndex;
                if (param (pVariation) > 0.0f) sliceLen = pickGrid (quarter);
            }
            gateLeft -= 1.0;
            if (gateLeft <= 0.0) repeating = false;
        }

        // the filter on the repeats: below 0 a low-pass, above 0 a high-pass
        const float fv = param (pFilter);
        if (std::abs (fv) > 0.5f)
        {
            const float hz = fv < 0.0f ? 20000.0f * std::pow (200.0f / 20000.0f, -fv * 0.01f)
                                       : 20.0f * std::pow (5000.0f / 20.0f, fv * 0.01f);
            if (std::abs (hz - filterHz) > 0.5f)
            {
                filterHz = hz;
                for (auto& f : filt) f.set (sr, hz, 0.8f);
            }
            const auto ol = filt[0].process (rl), orr = filt[1].process (rr);
            rl = fv < 0.0f ? ol.lp : ol.hp;
            rr = fv < 0.0f ? orr.lp : orr.hp;
        }

        // the dry sound: always (Mix), not while repeating (Insert), never (Gate)
        const int mode = (int) param (pMode);
        const float dryTarget = mode == 0 ? 1.0f : (mode == 2 ? 0.0f : (repeating ? 0.0f : 1.0f));
        dryGain += (dryTarget - dryGain) * dryCoef();
        const float vol = param (pVolume) * 0.01f;
        outputs[0] = { inputs[0][0] * dryGain + rl * vol, inputs[0][1] * dryGain + rr * vol };

        write = (write + 1) % n;
    }

private:
    double pickGrid (double quarter)
    {
        int g = (int) param (pGrid);
        const float var = param (pVariation) * 0.01f;
        if (var > 0.0f && rng.uni() < var)
            g = juce::jlimit (0, beatrepeat::gridNames().size() - 1, g + (int) std::lround (rng.bi() * 2.5f));
        return juce::jmax (8.0, beatrepeat::gridQuarters (g) * quarter);
    }

    float dryCoef() const { return 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate)); }

    std::vector<float> buf[2];
    int write { 0 }, captureStart { 0 }, sliceIndex { 0 };
    double clock { 0.0 }, slicePhase { 0.0 }, sliceLen { 1.0 }, gateLeft { 0.0 };
    bool repeating { false };
    float dryGain { 1.0f }, lastTrig { 0.0f }, filterHz { -1.0f };
    fxkit::Svf filt[2];
    fxkit::Rng rng { 0x5eed1234u };
};

static ModuleDescriptor beatRepeatDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.beatrepeat";
    d.displayName = "Beat Repeat";
    d.description =
        "Catches a slice on the beat and stutters it: every Interval (at the Offset sixteenth) it rolls Chance, then repeats "
        "a Grid-long slice for Gate sixteenths, each repeat Decay quieter and Pitch lower. Insert mutes the sound while it "
        "repeats, Gate keeps only the repeats. Trig In fires a repeat at once and lines the bar up (patch a Clock into it).";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 44;
    d.searchKeywords = "stutter glitch repeat slice roll";
    d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("trigIn", "Trig In"), audioOut ("audioOut", "Audio Out"), tempoIn() };
    auto offset = makeRotary ("offset", "Offset", 0.0f, 15.0f, 8.0f, 0, "/16", false, 1.0f);
    auto gate = makeRotary ("gate", "Gate", 1.0f, 16.0f, 4.0f, 1, "/16", false, 1.0f);
    d.params = {
        makeCombo  ("interval", "Interval", beatrepeat::intervalNames(), 2, 0, 2),
        offset,
        makeRotary ("chance",   "Chance",   0.0f, 100.0f, 60.0f, 0, "%"),
        makeSteppedList ("grid", "Grid", beatrepeat::gridNames(), 3, 1),
        makeRotary ("variation","Variation",0.0f, 100.0f, 0.0f, 1, "%"),
        gate,
        makeRotary ("pitch",    "Pitch",    0.0f, 12.0f, 0.0f, 2, "st"),
        makeRotary ("decay",    "Decay",    0.0f, 100.0f, 15.0f, 2, "%"),
        makeCombo  ("mode",     "Mode", { "Mix", "Insert", "Gate" }, 1, 2, 2),
        makeRotary ("filter",   "Filter",   -100.0f, 100.0f, 0.0f, 3, "%"),
        makeRotary ("volume",   "Volume",   0.0f, 150.0f, 100.0f, 3, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BeatRepeatModule, beatRepeatDescriptor)
