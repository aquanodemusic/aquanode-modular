//==============================================================================
// Clip Preserve - the detail-preserving clipper as a module
//
// ClipPreserve's own signal path: the sound is driven into a hard clip at
// Threshold; what the clip cut off (the "delta": driven minus clipped) goes
// through a two-pole high-pass (HP) and optionally a two-pole low-pass (LP),
// and Preserve of it is folded back onto the clipped sound - so hats and
// transients riding on a loud bass survive the clip. A loud signal on Side In
// raises the fold-back where it hits (SC Gain), as the plugin's sidechain.
// Delta Out is the folded-back detail on its own.
//
// About the plugin's "it changed the sound globally": the likeliest cause is
// that the plugin clears every channel past its main stereo pair at the end
// of each block - its sidechain bus too, and a host that hands that bus over
// as the buffer of the track it comes from then hears that track change. The
// drive also lifts everything below the threshold (the clip only bites above
// it), which reads as "the whole sound got louder". Here nothing writes into
// anything but this module's own outputs, and every module keeps its own
// filter and oversampler state, so it acts on the lane it is patched into only.
//
// 2x Oversample runs the clip and the delta chain at twice the rate (a 31-tap
// half-band filter up and down; 15 samples of latency at the base rate).
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

namespace
{
    // windowed-sinc half-band low-pass at a quarter of the rate (2x up / down)
    struct HalfBand
    {
        static constexpr int N = 31;   // odd, centre at 15
        std::array<float, N> h {};

        HalfBand()
        {
            const int c = N / 2;
            float sum = 0.0f;
            for (int n = 0; n < N; ++n)
            {
                const int k = n - c;
                const double sinc = k == 0 ? 0.5 : std::sin (juce::MathConstants<double>::pi * k * 0.5) / (juce::MathConstants<double>::pi * k);
                const double w = 0.42 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / (N - 1))
                               + 0.08 * std::cos (2.0 * juce::MathConstants<double>::twoPi * n / (N - 1));   // Blackman
                h[(size_t) n] = (float) (sinc * w);
                sum += h[(size_t) n];
            }
            for (auto& x : h)
                x /= sum;   // unity at DC
        }
    };

    const HalfBand& halfBand() { static const HalfBand hb; return hb; }

    struct Resampler2x
    {
        std::array<float, HalfBand::N> upLine {}, downLine {};
        int upPos { 0 }, downPos { 0 };

        void reset() { upLine.fill (0.0f); downLine.fill (0.0f); upPos = downPos = 0; }

        // one base-rate sample in, two 2x-rate samples out (zero-stuffed, gain 2)
        void up (float x, float& y0, float& y1)
        {
            const auto& h = halfBand().h;
            auto push = [this] (float v) { upPos = (upPos + 1) % HalfBand::N; upLine[(size_t) upPos] = v; };
            auto conv = [this, &h]
            {
                float acc = 0.0f;
                for (int i = 0; i < HalfBand::N; ++i)
                    acc += h[(size_t) i] * upLine[(size_t) ((upPos - i + HalfBand::N) % HalfBand::N)];
                return acc;
            };
            push (2.0f * x); y0 = conv();
            push (0.0f);     y1 = conv();
        }

        // two 2x-rate samples in, one base-rate sample out
        float down (float a, float b)
        {
            const auto& h = halfBand().h;
            downPos = (downPos + 1) % HalfBand::N; downLine[(size_t) downPos] = a;
            downPos = (downPos + 1) % HalfBand::N; downLine[(size_t) downPos] = b;
            float acc = 0.0f;
            for (int i = 0; i < HalfBand::N; ++i)
                acc += h[(size_t) i] * downLine[(size_t) ((downPos - i + HalfBand::N) % HalfBand::N)];
            return acc;
        }
    };

    struct Pole
    {
        float z { 0.0f };
        float hp (float x, float a) { z = a * z + (1.0f - a) * x; return x - z; }
        float lp (float x, float a) { z = a * z + (1.0f - a) * x; return z; }
    };
}

class ClipPreserveModule final : public SynthModule
{
public:
    enum P { pDrive, pThreshold, pPreserve, pOutput, pHpFreq, pLpFreq, pScGain, pHpOn, pLpOn, pOversample };   // the order of d.params

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.clippreserve";
        d.displayName = "Clip Preserve";
        d.description = "ClipPreserve's detail-preserving clipper: Drive pushes the sound into a hard clip at "
                        "Threshold, and Preserve of what the clip cut off - high-passed (HP), optionally low-passed "
                        "(LP) - is folded back on top, so the hats and transients on a loud bass survive. A loud "
                        "signal on Side In folds back more where it hits (SC Gain). Delta Out is the folded-back "
                        "detail alone. It acts on its own lane only.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 30;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("sideIn", "Side In"),
                      audioOut ("audioOut", "Audio Out"), audioOut ("deltaOut", "Delta Out") };
        d.params = {
            makeRotary ("drive", "Drive", 0.0f, 40.0f, 0.0f, 0, "dB"),
            makeRotary ("threshold", "Threshold", -24.0f, 0.0f, 0.0f, 0, "dB"),
            makeRotary ("preserve", "Preserve", 0.0f, 100.0f, 50.0f, 0, "%"),
            makeRotary ("output", "Output", -12.0f, 12.0f, 0.0f, 0, "dB"),

            makeRotary ("hpfreq", "HP Freq", 500.0f, 20000.0f, 8000.0f, 1, "Hz", true),
            makeRotary ("lpfreq", "LP Freq", 200.0f, 20000.0f, 8000.0f, 1, "Hz", true),
            makeRotary ("scgain", "SC Gain", 0.0f, 400.0f, 100.0f, 1, "%"),

            makeCombo ("hpon", "HP", { "HP Off", "HP On" }, 1, 2, 1),
            makeCombo ("lpon", "LP", { "LP Off", "LP On" }, 0, 2, 1),
            makeCombo ("oversample", "Oversample", { "1x", "2x" }, 0, 2, 2).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (auto& c : ch)
            c = {};
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float driveGain = juce::Decibels::decibelsToGain (param (pDrive));
        const float threshold = juce::Decibels::decibelsToGain (param (pThreshold));
        const float preserve = param (pPreserve) / 100.0f;
        const float outGain = juce::Decibels::decibelsToGain (param (pOutput));
        const float scGain = param (pScGain) / 100.0f;
        const bool hpOn = param (pHpOn) > 0.5f, lpOn = param (pLpOn) > 0.5f;
        const bool os = param (pOversample) > 0.5f;

        // the coefficients follow the knobs (and the rate the clip runs at)
        const double fs = sampleRate * (os ? 2.0 : 1.0);
        const float hpHz = param (pHpFreq), lpHz = param (pLpFreq);
        if (hpHz != lastHp || lpHz != lastLp || fs != lastFs)
        {
            lastHp = hpHz; lastLp = lpHz; lastFs = fs;
            aHP = std::exp (-juce::MathConstants<float>::twoPi * hpHz / (float) fs);
            aLP = std::exp (-juce::MathConstants<float>::twoPi * lpHz / (float) fs);
        }

        // the side signal's peak raises the fold-back (0 while nothing is patched)
        const float sc = isInputConnected (1) ? juce::jmax (std::abs (in[1][0]), std::abs (in[1][1])) : 0.0f;
        const float amount = juce::jlimit (0.0f, 1.0f, preserve * (1.0f + sc * scGain));

        for (size_t c = 0; c < 2; ++c)
        {
            auto& s = ch[c];
            const float x = in[0][c];
            float y, delta;
            if (os)
            {
                float u0, u1;
                s.os.up (x, u0, u1);
                float d0, d1;
                const float y0 = kernel (s, u0, driveGain, threshold, amount, hpOn, lpOn, d0);
                const float y1 = kernel (s, u1, driveGain, threshold, amount, hpOn, lpOn, d1);
                y = s.os.down (y0, y1);
                delta = s.osDelta.down (d0, d1);
            }
            else
            {
                y = kernel (s, x, driveGain, threshold, amount, hpOn, lpOn, delta);
            }
            out[0][c] = std::isfinite (y) ? y * outGain : 0.0f;
            out[1][c] = std::isfinite (delta) ? delta * outGain : 0.0f;
        }
    }

private:
    struct Channel
    {
        Pole hp1, hp2, lp1, lp2;
        Resampler2x os, osDelta;
    };

    float kernel (Channel& s, float x, float drive, float threshold, float amount, bool hpOn, bool lpOn, float& foldBack) const
    {
        const float driven = x * drive;
        const float clipped = juce::jlimit (-threshold, threshold, driven);
        float d = driven - clipped;
        if (hpOn) d = s.hp2.hp (s.hp1.hp (d, aHP), aHP);
        if (lpOn) d = s.lp2.lp (s.lp1.lp (d, aLP), aLP);
        foldBack = d * amount;
        return clipped + foldBack;
    }

    std::array<Channel, 2> ch;
    float aHP { 0.0f }, aLP { 0.0f }, lastHp { -1.0f }, lastLp { -1.0f };
    double lastFs { 0.0 };
};

AQUANODE_REGISTER_MODULE (ClipPreserveModule, ClipPreserveModule::descriptor)
