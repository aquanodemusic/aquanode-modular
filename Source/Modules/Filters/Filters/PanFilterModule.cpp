//==============================================================================
// (6.9.19) Pan Filter - a filter splits the sound in two: the part it lets
// through (a band, or the lows, or the highs) and the rest - the original with
// that part taken out by phase inversion (in minus part), so the two add back
// to exactly the original. Each gets its own pan (and level): a bass to one
// side, a band of the vocal swinging across with an LFO on Part Pan, the
// highs spread away from the middle ... Both pans centred and both levels at 0
// dB: the sound passes untouched.
//
// Split: Band (a band-pass at Frequency, Q its width), Low (a low-pass) or High
// (a high-pass), 12 dB state-variable filters. A pan moves a stereo part:
// towards a side, the far side's sound crosses over (equal power).
// Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

class PanFilterModule final : public SynthModule
{
public:
    enum P { pSplit = 0, pFreq, pQ, pPartPan, pRestPan, pPartLevel, pRestLevel };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.panfilter";
        d.displayName = "Pan Filter";
        d.searchKeywords = "pan filter split band stereo placement spread phase invert";
        d.description = "Splits the sound with a filter - a band, the lows or the highs - into that part and the rest (the "
                        "original with the part phase-inverted out), and gives each its own pan and level. A bass to one "
                        "side, a band swinging across with an LFO on Part Pan, the highs spread away from the middle. Both "
                        "pans centred and levels at 0 dB: the sound as it was.";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 41;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo  ("split",     "Split", { "Band", "Low", "High" }, 0, 0, 2).noMod(),
            makeRotary ("freq",      "Frequency", 20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
            makeRotary ("q",         "Q", 0.3f, 12.0f, 1.0f, 0, {}, true),
            makeRotary ("partPan",   "Part Pan", -100.0f, 100.0f, -60.0f, 1, "%"),
            makeRotary ("restPan",   "Rest Pan", -100.0f, 100.0f, 0.0f, 1, "%"),
            makeRotary ("partLevel", "Part Level", -24.0f, 12.0f, 0.0f, 1, "dB"),
            makeRotary ("restLevel", "Rest Level", -24.0f, 12.0f, 0.0f, 1, "dB")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }
    void reset() override { for (auto& s : st) s = {}; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int split = (int) param (pSplit);
        const float fc = juce::jmin (param (pFreq), (float) sampleRate * 0.49f);
        const float g = tanMemo (juce::MathConstants<float>::pi * fc / (float) sampleRate, [] (float x) { return std::tan (x); });
        const float k = 1.0f / juce::jmax (0.1f, param (pQ));
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;

        const float partPan = param (pPartPan) * 0.01f, restPan = param (pRestPan) * 0.01f;
        const float partG = juce::Decibels::decibelsToGain (param (pPartLevel)), restG = juce::Decibels::decibelsToGain (param (pRestLevel));

        float part[2], rest[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = in[0][c];
            auto& s = st[c];
            const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;
            const float p = split == 1 ? v2 : split == 2 ? x - k * v1 - v2 : k * v1;   // (band: unity at its centre)
            part[c] = p * partG;
            rest[c] = (x - p) * restG;
        }
        // each part panned on its own (its far side's sound moved over to the near side)
        panStereo (partPan, part[0], part[1]);
        panStereo (restPan, rest[0], rest[1]);
        out[0][0] = part[0] + rest[0];
        out[0][1] = part[1] + rest[1];
    }

    // the curve: what a centred mono listener hears (both sides summed), so the pans show as a dip or a lift
    bool magnitudeResponseDb (const float* hz, float* db, int n) const override
    {
        const double fc = juce::jmin ((double) getParameterBase (pFreq), sampleRate * 0.49);
        const double k = 1.0 / juce::jmax (0.1, (double) getParameterBase (pQ));
        const int split = (int) getParameterBase (pSplit);
        const double pg = std::pow (10.0, getParameterBase (pPartLevel) / 20.0) * monoGain (getParameterBase (pPartPan) * 0.01f);
        const double rg = std::pow (10.0, getParameterBase (pRestLevel) / 20.0) * monoGain (getParameterBase (pRestPan) * 0.01f);
        for (int i = 0; i < n; ++i)
        {
            const auto r = response::svf (response::sOf (hz[i], fc, sampleRate), k);
            const response::C p = split == 1 ? r.low : split == 2 ? r.high : r.band * k;
            db[i] = response::db (pg * p + rg * (1.0 - p));
        }
        return true;
    }

private:
    // a stereo pan: towards a side, the far side's sound moves over (cos / sin, equal power)
    static void panStereo (float pan, float& l, float& r)
    {
        pan = juce::jlimit (-1.0f, 1.0f, pan);
        const float th = std::abs (pan) * juce::MathConstants<float>::halfPi, c = std::cos (th), s = std::sin (th);
        if (pan > 0.0f) { r += l * s; l *= c; }
        else if (pan < 0.0f) { l += r * s; r *= c; }
    }
    // what a centred mono sound comes out as, both sides summed (and halved)
    static double monoGain (float pan)
    {
        const double th = std::abs (juce::jlimit (-1.0f, 1.0f, pan)) * juce::MathConstants<double>::halfPi;
        return 0.5 * (1.0 + std::cos (th) + std::sin (th));
    }

    struct State { float ic1 {}, ic2 {}; };
    State st[2];
    Memo tanMemo;
};

AQUANODE_REGISTER_MODULE (PanFilterModule, PanFilterModule::descriptor)
