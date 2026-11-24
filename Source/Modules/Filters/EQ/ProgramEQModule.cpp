// Tube Program EQ - in the spirit of the classic passive tube program
// equalizers (the EQP-1A kind), with their famous low-end trick.
//
// Low: a frequency (20, 30, 60, 100 Hz), Boost (0 - 10, up to about +13.5
// dB) and Atten (0 - 10, up to about -17.5 dB) - at the same frequency, as
// on the hardware, but not the same curve: the boost is a resonant shelf a
// little above it, the attenuation a broad, gentle shelf a little below. So
// boosting and cutting together does not cancel: the very lows rise with a
// bump just around the frequency, and a dip opens above it (at 60 Hz, both
// at 5: about +3.6 dB at 48 Hz, -2.7 dB at 110 Hz) - deep, tight bass with
// the mud taken out.
// High: a boost bell at 3, 4, 5, 8, 10, 12 or 16 kHz (Boost 0 - 10, up to
// +18 dB; Bandwidth from sharp to broad), and an attenuation shelf at 5, 10
// or 20 kHz (Atten 0 - 10, up to -16 dB) - the same trick up top: boost air
// and shelve off the fizz.
// Then the tube make-up stage (a touch of even colour when driven, clean at
// ordinary levels) and Output. EQ Out passes the sound through the tube
// stage alone.
//
// (6.9.19) Character: Strong (new cards) - the low trick as the hardware is
// heard: the boost a resonant shelf at the frequency, the attenuation a broad
// dip about three times above it plus a gentle shelf below, so Boost and Atten
// together lift the very lows and carve the mud right above (at 60 Hz, both at
// 10: about +10 dB at 30 Hz, -12 dB at 200 Hz; both at 5: +5 / -6 dB), and the
// tube stage driven twice as hard. Gentle is the curve from before (patches
// from before keep it).
// Every band is a biquad, and the card's curve (Graph) is worked out from
// those same coefficients - the curve is exactly what it does.
//
// Flexible: per voice when fed per voice. Input: 0 = Audio In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

namespace programeq
{
    struct Coeffs { double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };

    // RBJ cookbook shelves and bell (bilinear, prewarped)
    inline Coeffs shelf (bool low, double f0, double gainDb, double q, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (5.0, sr * 0.45, f0) / sr;
        const double c = std::cos (w0), al = std::sin (w0) / (2.0 * q), sA = 2.0 * std::sqrt (A) * al;
        double b0, b1, b2, a0, a1, a2;
        if (low)
        {
            b0 = A * ((A + 1) - (A - 1) * c + sA); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sA);
            a0 = (A + 1) + (A - 1) * c + sA;       a1 = -2 * ((A - 1) + (A + 1) * c);    a2 = (A + 1) + (A - 1) * c - sA;
        }
        else
        {
            b0 = A * ((A + 1) + (A - 1) * c + sA); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sA);
            a0 = (A + 1) - (A - 1) * c + sA;       a1 = 2 * ((A - 1) - (A + 1) * c);      a2 = (A + 1) - (A - 1) * c - sA;
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
    inline Coeffs bell (double f0, double gainDb, double q, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (5.0, sr * 0.45, f0) / sr;
        const double c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
        const double a0 = 1 + al / A;
        return { (1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0 };
    }

    constexpr double kLowHz[]  = { 20, 30, 60, 100 };
    constexpr double kHighHz[] = { 3000, 4000, 5000, 8000, 10000, 12000, 16000 };
    constexpr double kAttHz[]  = { 5000, 10000, 20000 };

    struct Bands { Coeffs c[5]; };   // (the fifth: Strong's shelf under the dip; else flat)

    // the bands from the knobs
    inline Bands design (const float* k, double sr, bool strong = false)
    {
        // k: lowFreq, lowBoost, lowAtten, highFreq, highBoost, bandwidth, attFreq, highAtten
        const double lowF = kLowHz[juce::jlimit (0, 3, (int) std::lround (k[0]))];
        const double highF = kHighHz[juce::jlimit (0, 6, (int) std::lround (k[3]))];
        const double attF = kAttHz[juce::jlimit (0, 2, (int) std::lround (k[6]))];
        const double bw = juce::jlimit (0.0, 10.0, (double) k[5]) / 10.0;      // 0 sharp .. 1 broad
        Bands b;
        const double boost = juce::jlimit (0.0, 10.0, (double) k[1]) / 10.0, atten = juce::jlimit (0.0, 10.0, (double) k[2]) / 10.0;
        if (strong)
        {
            b.c[0] = shelf (true, lowF, 13.5 * boost, 1.0, sr);          // the resonant boost, at the frequency
            b.c[1] = bell (lowF * 3.0, -12.0 * atten, 0.7, sr);          // the dip above it
            b.c[4] = shelf (true, lowF * 0.5, -6.0 * atten, 0.5, sr);    // and a gentle shelf under
        }
        else
        {
            b.c[0] = shelf (true,  lowF * 1.2, 13.5 * boost, 1.2, sr);   // the resonant boost
            b.c[1] = shelf (true,  lowF * 0.7, -17.5 * atten, 0.4, sr);  // the broad attenuation
        }
        b.c[2] = bell (highF, 18.0 * juce::jlimit (0.0, 10.0, (double) k[4]) / 10.0, 2.5 * std::pow (0.16, bw), sr);   // Q 2.5 .. 0.4
        b.c[3] = shelf (false, attF, -16.0 * juce::jlimit (0.0, 10.0, (double) k[7]) / 10.0, 0.7, sr);
        return b;
    }

    struct Biquad
    {
        double z1 { 0 }, z2 { 0 };
        double tick (const Coeffs& c, double x)
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
}

class ProgramEQModule : public SynthModule
{
public:
    // (6.9.15) the low-end trick in one press: Boost and Atten together at 60 Hz
    void uiButtonClicked (const juce::String& id) override
    {
        if (id != "lowTrick") return;
        setParameter ("lowFreq", 2.0f);    // 60 Hz
        setParameter ("lowBoost", 6.0f);
        setParameter ("lowAtten", 4.0f);
        setParameter ("eq", 0.0f);         // In
    }
    enum P { pLowFreq = 0, pLowBoost, pLowAtten, pHighFreq, pHighBoost, pBandwidth, pAttFreq, pHighAtten, pOutput, pEq,
             pTrick, pCharacter };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.programeq";
        d.displayName = "Tube Program EQ";
        d.description =
            "A passive tube program EQ with its famous low-end trick: Boost and Atten at the same "
            "low frequency (20, 30, 60, 100 Hz) do not cancel - the very lows rise with a bump around it and a dip "
            "opens right above (deep bass, the mud taken out). Up top a boost bell (3 to 16 kHz, Bandwidth sharp to "
            "broad) and an attenuation shelf (5, 10, 20 kHz). Then a tube make-up stage and Output. Its curve "
            "(Graph) is worked out from the filters it runs. Boost + Cut Low sets the trick up in one press: 60 Hz, "
            "Boost 6 and Atten 4 (then move them to taste). Character: Strong (the trick as the hardware is heard, "
            "a hotter tube) or Gentle (the softer curve of earlier versions).";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 24;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo  ("lowFreq",   "Low Freq",  { "20 Hz", "30 Hz", "60 Hz", "100 Hz" }, 2, 0, 1).noMod(),
            makeRotary ("lowBoost",  "Boost",     0.0f, 10.0f, 0.0f, 0),
            makeRotary ("lowAtten",  "Atten",     0.0f, 10.0f, 0.0f, 0),
            makeCombo  ("highFreq",  "High Freq", { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" }, 5, 1, 1).noMod(),
            makeRotary ("highBoost", "Boost",     0.0f, 10.0f, 0.0f, 1),
            makeRotary ("bandwidth", "Bandwidth", 0.0f, 10.0f, 5.0f, 1),
            makeCombo  ("attFreq",   "Atten Sel", { "5 kHz", "10 kHz", "20 kHz" }, 1, 2, 1).noMod(),
            makeRotary ("highAtten", "Atten",     0.0f, 10.0f, 0.0f, 2),
            makeRotary ("output",    "Output",    -12.0f, 12.0f, 0.0f, 2, "dB"),
            makeCombo  ("eq",        "EQ",        { "In", "Out" }, 0, 2, 1).noMod(),
            makeButton ("lowTrick",  "Boost + Cut Low", 3, 3),   // (6.9.15) the trick, set up in one press
            // (6.9.19) Strong for new cards; patches from before keep Gentle
            makeCombo  ("character", "Character", { "Gentle", "Strong" }, 1, 3, 2).noMod().whenMissing (0.0f)
        };
        d.rowSections = { { 0, "Low" }, { 1, "High" }, { 2, "High Atten & Out" } };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    int preferredModuleWidth() const override { return 420; }   // (room for the frequency menus' names)

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        dcR = 1.0 - juce::MathConstants<double>::twoPi * 5.0 / sr;
        lastKey = -1.0;
        reset();
    }

    void reset() override
    {
        for (auto& lane : lanes) lane = {};
    }
    void voiceReset (int v) override { lanes[(size_t) v] = {}; }

    void blockStart() override
    {
        float k[8];
        double key = sampleRate;
        for (int i = 0; i < 8; ++i) { k[i] = param (i); key = key * 1.000173 + k[i]; }
        const bool strong = param (pCharacter) > 0.5f;
        key += strong ? 0.5 : 0.0;
        if (key != lastKey)
        {
            lastKey = key;
            bands = programeq::design (k, sampleRate, strong);
        }
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override { render (kMaxVoices, in, out); }
    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override { render (v, in, out); }

    // the curve: the same four biquads, the tube stage's small-signal gain, the DC blocker, Output
    bool magnitudeResponseDb (const float* hz, float* db, int n) const override
    {
        float k[8];
        for (int i = 0; i < 8; ++i) k[i] = getParameterBase (i);
        const auto b = programeq::design (k, sampleRate, getParameterBase (pCharacter) > 0.5f);
        const bool eqOn = getParameterBase (pEq) < 0.5f;
        const double outGain = std::pow (10.0, getParameterBase (pOutput) / 20.0) * kTubeSlope;
        for (int i = 0; i < n; ++i)
        {
            const auto z1 = response::zInv (hz[i], sampleRate);
            response::C h = outGain * (1.0 - z1) / (1.0 - dcR * z1);
            if (eqOn)
                for (const auto& c : b.c)
                    h *= response::biquad (c.b0, c.b1, c.b2, c.a1, c.a2, z1);
            db[i] = response::db (h);
        }
        return true;
    }

private:
    static constexpr int kLanes = kMaxVoices + 1;
    static inline const double kTubeBias = std::tanh (0.04);
    static inline const double kTubeSlope = 1.0 - kTubeBias * kTubeBias;   // the tube stage's gain at ordinary levels

    struct Lane
    {
        programeq::Biquad f[2][5];
        double dcX[2] {}, dcY[2] {};
    };

    void render (int l, const StereoFrame* in, StereoFrame* out)
    {
        auto& lane = lanes[(size_t) l];
        const bool eqOn = param (pEq) < 0.5f;
        const double outGain = std::pow (10.0, param (pOutput) / 20.0);
        const double drive = param (pCharacter) > 0.5f ? 1.2 : 0.6;   // (Strong: the tube driven twice as hard)
        for (int c = 0; c < 2; ++c)
        {
            double x = in[0][(size_t) c];
            if (eqOn)
                for (int s = 0; s < 5; ++s)
                    x = lane.f[c][s].tick (bands.c[s], x);
            // the tube stage: transparent when quiet, a touch of even colour when hot
            const double t = (std::tanh (drive * x + 0.04) - kTubeBias) / drive;
            const double y = t - lane.dcX[c] + dcR * lane.dcY[c];
            lane.dcX[c] = t;
            lane.dcY[c] = std::isfinite (y) ? y : 0.0;
            if (! std::isfinite (x)) { lane = {}; }
            out[0][(size_t) c] = (float) (lane.dcY[c] * outGain);
        }
    }

    programeq::Bands bands;
    double lastKey { -1.0 }, dcR { 0.9993 };
    std::array<Lane, kLanes> lanes {};
};

AQUANODE_REGISTER_MODULE (ProgramEQModule, ProgramEQModule::descriptor)
