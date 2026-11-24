//==============================================================================
// (6.9.19) EQs whose curve is a shape:
//
// Ripple EQ - the curve is a wave packet, as a quantum particle's wave is
//   drawn: ripples that build up towards Center and die away again (a sine
//   under a bell-shaped envelope, Width octaves wide, Ripples per octave),
//   Depth dB tall. Speed moves the ripples through the packet (they travel
//   up or down, the envelope stays); 0 holds them still. Endless: no
//   envelope, the ripples over the whole spectrum.
// FM Filter - two-operator FM, and the spectrum of the sound it makes is the
//   filter: a carrier at Pitch, a modulator at Pitch x Ratio with Index, so
//   the curve has peaks where that FM tone has partials (Pitch x |1 + n x
//   Ratio|), each as tall as the partial is loud (Bessel J_n(Index)), Width
//   cents wide, and Floor between them. Index 0: one peak at Pitch.
// Audio to Filter - the spectrum of a sound is the filter: every 2048
//   samples of Shape In (unpatched: Audio In itself) measured by an FFT,
//   smoothed (Smooth: across frequency and over time), and the loud parts of
//   it let through, the quiet parts down to Floor. Contrast sharpens it.
// Tilt EQ - the whole spectrum tilted around Pivot, Tilt dB per octave (up
//   to 6, a first-order slope): + brightens, - darkens. Seven first-order
//   pole-zero pairs spread over the range give a straight slope (within
//   about 0.3 dB) from 20 Hz to 20 kHz; no latency.
//
// The first three work on a 2048-point FFT (CurveStft: 2048 samples of
// latency, reported); Tilt is plain filters. Global. Inputs: Audio In (and
// Shape In). Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Core/FilterResponse.h"
#include "../../Shared/CurveStft.h"

using namespace aquanode;

namespace shapeeq
{
    // J_0 .. J_n(x) for n up to maxN, by Miller's backward recurrence
    inline void besselJ (double x, int maxN, double* j)
    {
        for (int n = 0; n <= maxN; ++n) j[n] = 0.0;
        if (std::abs (x) < 1.0e-9) { j[0] = 1.0; return; }
        constexpr int kMaxStart = 254;
        const int start = juce::jmin (kMaxStart, 2 * ((juce::jmax (maxN, (int) std::abs (x)) + 15 + (int) std::sqrt (40.0 * (maxN + 1))) / 2));
        double jp1 = 0.0, jk = 1.0e-30, sum = 0.0;
        double tmp[kMaxStart + 2] {};
        for (int k = start; k > 0; --k)
        {
            const double jm1 = 2.0 * k / x * jk - jp1;
            jp1 = jk;
            jk = jm1;
            if (k - 1 <= maxN) tmp[k - 1] = jk;
            if ((k - 1) % 2 == 0) sum += (k - 1 == 0 ? 1.0 : 2.0) * jk;
            if (std::abs (jk) > 1.0e100) { jk *= 1.0e-100; jp1 *= 1.0e-100; sum *= 1.0e-100; for (int i = 0; i <= maxN; ++i) tmp[i] *= 1.0e-100; }
        }
        for (int n = 0; n <= maxN; ++n) j[n] = tmp[n] / sum;
    }

    // the base of the three FFT EQs: a curve as linear gains per bin, the dry / wet mix and Output
    class CurveEQBase : public SynthModule
    {
    public:
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        int latencySamples() const override { return CurveStft::kN; }

        void prepare (double sr) override
        {
            SynthModule::prepare (sr);
            reset();
        }
        void reset() override
        {
            stft.reset();
            for (auto& d : dry) std::fill (std::begin (d), std::end (d), 0.0f);
            dryPos = 0;
            onReset();
        }

        bool magnitudeResponseDb (const float* hz, float* db, int n) const override
        {
            const float wet = juce::jlimit (0.0f, 1.0f, getParameterBase (mixIndex()) * 0.01f);
            const float outG = juce::Decibels::decibelsToGain (getParameterBase (outputIndex()));
            for (int i = 0; i < n; ++i)
            {
                const float g = juce::Decibels::decibelsToGain (shown.at (hz[i]), -100.0f);
                db[i] = juce::Decibels::gainToDecibels ((1.0f - wet) + wet * g * outG, -100.0f);
            }
            return true;
        }

    protected:
        virtual int mixIndex() const = 0;
        virtual int outputIndex() const = 0;
        virtual void fillGains (float* gains) = 0;      // once a frame (every 512 samples)
        virtual void beforeSample (const StereoFrame*) {}
        virtual void onReset() {}

        void render (const StereoFrame* in, StereoFrame* out)
        {
            beforeSample (in);
            float l = in[0][0], r = in[0][1];
            // the dry sound, delayed as much as the wet one
            dry[0][dryPos] = l; dry[1][dryPos] = r;
            const int rd = (dryPos + 1) % (CurveStft::kN + 1);
            const float dl = dry[0][rd], dr = dry[1][rd];
            dryPos = rd;
            stft.process (l, r, [this] (float* g) { fillGains (g); shown.fromGains (g, sampleRate); });
            const float wet = juce::jlimit (0.0f, 1.0f, param (mixIndex()) * 0.01f);
            const float outG = juce::Decibels::decibelsToGain (param (outputIndex()));
            out[0][0] = dl * (1.0f - wet) + l * outG * wet;
            out[0][1] = dr * (1.0f - wet) + r * outG * wet;
        }

        CurveStft stft;
        LiveCurve shown;
        float dry[2][CurveStft::kN + 1] {};
        int dryPos { 0 };
    };
}

//==============================================================================
class RippleEQModule final : public shapeeq::CurveEQBase
{
public:
    enum P { pCenter = 0, pWidth, pRipples, pDepth, pSpeed, pShape, pOutput, pMix };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.rippleeq";
        d.displayName = "Ripple EQ";
        d.searchKeywords = "ripple wave packet quantum eq moving comb wobble";
        d.description = "An EQ curve shaped like a wave packet: ripples that build up towards Center and die away again "
                        "(Width octaves wide, Ripples per octave, Depth dB tall). Speed lets the ripples travel through "
                        "the packet - up or down, slow or fast; 0 holds them still. Endless spreads them over the whole "
                        "spectrum. (2048 samples of latency.)";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 32;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("center",  "Center",  40.0f, 16000.0f, 1000.0f, 0, "Hz", true),
            makeRotary ("width",   "Width",   0.25f, 8.0f, 3.0f, 0, "oct", true).visibleWhen ("shape", 0.0f),
            makeRotary ("ripples", "Ripples", 0.25f, 12.0f, 2.0f, 0, "/oct", true),
            makeRotary ("depth",   "Depth",   -24.0f, 24.0f, 9.0f, 0, "dB"),
            makeRotary ("speed",   "Speed",   -8.0f, 8.0f, 0.5f, 1, "Hz"),
            makeCombo  ("shape",   "Shape", { "Packet", "Endless" }, 0, 1, 2).noMod(),
            makeRotary ("output",  "Output",  -18.0f, 6.0f, 0.0f, 1, "dB"),
            makeRotary ("mix",     "Mix",     0.0f, 100.0f, 100.0f, 1, "%")
        };
        return d;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override { render (in, out); }

protected:
    int mixIndex() const override { return pMix; }
    int outputIndex() const override { return pOutput; }
    void onReset() override { phase = 0.0; }

    void fillGains (float* g) override
    {
        const double centre = param (pCenter), sigma = juce::jmax (0.05, (double) param (pWidth) * 0.5);
        const double k = juce::MathConstants<double>::twoPi * (double) param (pRipples);
        const double depth = param (pDepth);
        const bool endless = param (pShape) > 0.5f;
        phase += juce::MathConstants<double>::twoPi * (double) param (pSpeed) * CurveStft::kHop / sampleRate;
        phase = std::fmod (phase, juce::MathConstants<double>::twoPi * 1000.0);
        for (int b = 0; b < CurveStft::kBins; ++b)
        {
            const double hz = juce::jmax (10.0, (double) CurveStft::binHz (b, sampleRate));
            const double u = std::log2 (hz / centre);
            const double env = endless ? 1.0 : std::exp (-0.5 * (u / sigma) * (u / sigma));
            g[b] = (float) std::pow (10.0, depth * env * std::cos (k * u - phase) / 20.0);
        }
    }

private:
    double phase { 0.0 };
};

//==============================================================================
class FMFilterModule final : public shapeeq::CurveEQBase
{
public:
    enum P { pPitch = 0, pRatio, pIndex, pWidth, pFloor, pDepth, pOutput, pMix };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.fmfilter";
        d.displayName = "FM Filter";
        d.searchKeywords = "fm frequency modulation filter comb harmonic bessel operator";
        d.description = "Two-operator FM whose spectrum is the filter: a carrier at Pitch and a modulator at Pitch x Ratio "
                        "with Index - the curve has a peak wherever that FM tone has a partial, as tall as the partial is "
                        "loud, Width cents wide, with Floor between them. Index 0 is one peak at Pitch; turning it up "
                        "spreads the peaks out the way FM brightens a tone. Depth sets how much of the curve is applied. "
                        "(2048 samples of latency.)";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 33;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("pitch",  "Pitch",  20.0f, 2000.0f, 110.0f, 0, "Hz", true),
            makeRotary ("ratio",  "Ratio",  0.25f, 8.0f, 1.0f, 0, "x", true),
            makeRotary ("index",  "Index",  0.0f, 12.0f, 2.0f, 0),
            makeRotary ("width",  "Width",  5.0f, 600.0f, 60.0f, 0, "ct", true),
            makeRotary ("floor",  "Floor",  -60.0f, 0.0f, -30.0f, 1, "dB"),
            makeRotary ("depth",  "Depth",  0.0f, 100.0f, 100.0f, 1, "%"),
            makeRotary ("output", "Output", -18.0f, 18.0f, 6.0f, 1, "dB"),
            makeRotary ("mix",    "Mix",    0.0f, 100.0f, 100.0f, 1, "%")
        };
        return d;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override { render (in, out); }

protected:
    int mixIndex() const override { return pMix; }
    int outputIndex() const override { return pOutput; }
    void onReset() override { lastKey = -1.0; }

    void fillGains (float* g) override
    {
        const double pitch = param (pPitch), ratio = param (pRatio), index = param (pIndex);
        const double width = juce::jmax (1.0, (double) param (pWidth));
        const double floorLin = std::pow (10.0, param (pFloor) / 20.0), depth = juce::jlimit (0.0, 1.0, param (pDepth) * 0.01);
        const double key = pitch + ratio * 1.37 + index * 7.1 + width * 0.013 + floorLin * 3.3 + depth * 11.0;
        if (key == lastKey) { std::copy (std::begin (cached), std::end (cached), g); return; }
        lastKey = key;

        // the partials: Pitch x |1 + n Ratio|, amplitude J_n(Index) (a negative frequency folds over, its sign turned)
        constexpr int kMaxN = 48;
        const int maxN = juce::jmin (kMaxN, (int) index + 12);
        double j[kMaxN + 1];
        shapeeq::besselJ (index, maxN, j);
        struct Line { double hz, amp; };
        Line lines[2 * kMaxN + 1];
        int nl = 0;
        for (int n = -maxN; n <= maxN; ++n)
        {
            double hz = pitch * (1.0 + n * ratio);
            double a = (n < 0 && (-n) % 2 == 1) ? -j[-n] : j[std::abs (n)];
            if (hz < 0.0) { hz = -hz; a = -a; }
            if (hz < 5.0 || hz > sampleRate * 0.49) continue;
            bool merged = false;
            for (int i = 0; i < nl; ++i)
                if (std::abs (lines[i].hz - hz) < 0.5) { lines[i].amp += a; merged = true; break; }
            if (! merged) lines[nl++] = { hz, a };
        }
        double peak = 1.0e-9;
        for (int i = 0; i < nl; ++i) peak = juce::jmax (peak, std::abs (lines[i].amp));

        // each partial a bump Width cents wide (Gaussian in cents), the loudest of them at each bin
        float m[CurveStft::kBins] {};
        const double binHz = sampleRate / CurveStft::kN;
        for (int i = 0; i < nl; ++i)
        {
            const double a = std::abs (lines[i].amp) / peak;
            if (a < 1.0e-4) continue;
            const double lo = lines[i].hz * std::exp2 (-3.0 * width / 1200.0), hi = lines[i].hz * std::exp2 (3.0 * width / 1200.0);
            const int b0 = juce::jmax (0, (int) (lo / binHz)), b1 = juce::jmin (CurveStft::kBins - 1, (int) (hi / binHz) + 1);
            for (int b = b0; b <= b1; ++b)
            {
                const double c = 1200.0 * std::log2 (juce::jmax (1.0, b * binHz) / lines[i].hz) / width;
                m[b] = juce::jmax (m[b], (float) (a * std::exp (-c * c)));
            }
            // a peak narrower than a bin still shows in the bin it falls in
            const int bc = juce::jlimit (0, CurveStft::kBins - 1, (int) std::lround (lines[i].hz / binHz));
            m[bc] = juce::jmax (m[bc], (float) a);
        }
        for (int b = 0; b < CurveStft::kBins; ++b)
        {
            const double v = floorLin + (1.0 - floorLin) * m[b];
            cached[b] = g[b] = (float) (1.0 - depth * (1.0 - v));
        }
    }

private:
    double lastKey { -1.0 };
    float cached[CurveStft::kBins] {};
};

//==============================================================================
class AudioToFilterModule final : public shapeeq::CurveEQBase
{
public:
    enum P { pSmooth = 0, pContrast, pFloor, pDepth, pOutput, pMix };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.audiotofilter";
        d.displayName = "Audio to Filter";
        d.searchKeywords = "audio spectrum filter fft imprint match shape vocoder cross";
        d.description = "The spectrum of a sound becomes the filter: every 2048 samples of Shape In are measured, and where "
                        "that sound is loud Audio In passes, where it is quiet Audio In goes down (to Floor). Smooth blurs "
                        "the shape across frequency and over time, Contrast sharpens it. With nothing in Shape In, Audio In "
                        "shapes itself (its loud parts stand out more). (2048 samples of latency.)";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 34;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("shapeIn", "Shape In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("smooth",   "Smooth",   0.0f, 100.0f, 40.0f, 0, "%"),
            makeRotary ("contrast", "Contrast", 0.25f, 4.0f, 1.0f, 0, "x", true),
            makeRotary ("floor",    "Floor",    -60.0f, 0.0f, -36.0f, 0, "dB"),
            makeRotary ("depth",    "Depth",    0.0f, 100.0f, 100.0f, 1, "%"),
            makeRotary ("output",   "Output",   -18.0f, 18.0f, 6.0f, 1, "dB"),
            makeRotary ("mix",      "Mix",      0.0f, 100.0f, 100.0f, 1, "%")
        };
        return d;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override { render (in, out); }

protected:
    int mixIndex() const override { return pMix; }
    int outputIndex() const override { return pOutput; }
    void onReset() override
    {
        std::fill (std::begin (shapeRing), std::end (shapeRing), 0.0f);
        std::fill (std::begin (held), std::end (held), 0.0f);
    }

    void beforeSample (const StereoFrame* in) override
    {
        const auto& s = isInputConnected (1) ? in[1] : in[0];
        shapeRing[stft.writeIndex()] = 0.5f * (s[0] + s[1]);
    }

    void fillGains (float* g) override
    {
        float mags[CurveStft::kBins];
        stft.magnitudes (shapeRing, stft.ringStart(), mags);
        const float smooth = juce::jlimit (0.0f, 1.0f, param (pSmooth) * 0.01f);
        // across frequency: a running mean a share of an octave wide (constant-Q), from a prefix sum
        double prefix[CurveStft::kBins + 1];
        prefix[0] = 0.0;
        for (int b = 0; b < CurveStft::kBins; ++b) prefix[b + 1] = prefix[b] + mags[b];
        const float share = smooth * 0.35f;   // up to about half an octave
        float sm[CurveStft::kBins];
        for (int b = 0; b < CurveStft::kBins; ++b)
        {
            const int half = (int) ((float) b * share * 0.5f + smooth * 2.0f);
            const int lo = juce::jmax (0, b - half), hi = juce::jmin (CurveStft::kBins - 1, b + half);
            sm[b] = (float) ((prefix[hi + 1] - prefix[lo]) / (double) (hi - lo + 1));
        }
        // over time: rising quickly, falling at Smooth
        const float fall = 1.0f - 0.97f * smooth, rise = juce::jmin (1.0f, fall * 3.0f);
        float peak = 1.0e-9f;
        for (int b = 0; b < CurveStft::kBins; ++b)
        {
            held[b] += (sm[b] > held[b] ? rise : fall) * (sm[b] - held[b]);
            if (b > 0) peak = juce::jmax (peak, held[b]);
        }
        const float contrast = param (pContrast), floorLin = juce::Decibels::decibelsToGain (param (pFloor));
        const float depth = juce::jlimit (0.0f, 1.0f, param (pDepth) * 0.01f);
        for (int b = 0; b < CurveStft::kBins; ++b)
        {
            const float m = std::pow (juce::jlimit (0.0f, 1.0f, held[b] / peak), contrast);
            const float v = floorLin + (1.0f - floorLin) * m;
            g[b] = 1.0f - depth * (1.0f - v);
        }
    }

private:
    float shapeRing[CurveStft::kN] {}, held[CurveStft::kBins] {};
};

//==============================================================================
class TiltEQModule final : public SynthModule
{
public:
    enum P { pTilt = 0, pPivot, pOutput };
    static constexpr int kSections = 7;

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.tilteq";
        d.displayName = "Tilt EQ";
        d.searchKeywords = "tilt eq slope bright dark balance pink";
        d.description = "Tilts the whole spectrum around Pivot: Tilt dB per octave, + brightens (the highs up, the lows "
                        "down), - darkens. A straight slope from 20 Hz to 20 kHz (up to 6 dB per octave), no latency. "
                        "-3 dB per octave turns white noise pink.";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 35;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("tilt",   "Tilt",   -6.0f, 6.0f, 0.0f, 0, "dB/oct"),
            makeRotary ("pivot",  "Pivot",  100.0f, 10000.0f, 1000.0f, 0, "Hz", true),
            makeRotary ("output", "Output", -18.0f, 18.0f, 0.0f, 0, "dB")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        lastKey = -1.0f;
        reset();
    }
    void reset() override { for (auto& c : z) for (auto& v : c) v = 0.0f; countdown = 0; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (--countdown <= 0)
        {
            countdown = 16;
            const float key = param (pTilt) * 13.0f + param (pPivot) + param (pOutput) * 101.0f;
            if (key != lastKey) { lastKey = key; co = design (param (pTilt), param (pPivot), param (pOutput), sampleRate); }
        }
        for (int c = 0; c < 2; ++c)
        {
            float x = in[0][c] * co.gain;
            for (int s = 0; s < kSections; ++s)
            {
                const float y = co.b0[s] * x + z[c][s];
                z[c][s] = co.b1[s] * x - co.a1[s] * y;
                x = y;
            }
            out[0][c] = x;
        }
    }

    bool magnitudeResponseDb (const float* hz, float* db, int n) const override
    {
        const auto c = design (getParameterBase (pTilt), getParameterBase (pPivot), getParameterBase (pOutput), sampleRate);
        for (int i = 0; i < n; ++i)
            db[i] = response::db (respond (c, hz[i], sampleRate));
        return true;
    }

private:
    struct Co { float b0[kSections], b1[kSections], a1[kSections]; float gain { 1.0f }; };

    static response::C respond (const Co& c, double hz, double sr)
    {
        const auto z1 = response::zInv (hz, sr);
        response::C h = c.gain;
        for (int s = 0; s < kSections; ++s)
            h *= ((double) c.b0[s] + (double) c.b1[s] * z1) / (1.0 + (double) c.a1[s] * z1);
        return h;
    }

    // a zero and a pole 1.5 octaves apart (their order by the tilt's sign), every 1.5 octaves
    // from 20 Hz: each section adds Tilt over its stretch, so together a straight slope
    static Co design (float tiltDbOct, float pivot, float outDb, double sr)
    {
        Co c {};
        const double alpha = juce::jlimit (-1.0, 1.0, tiltDbOct / 6.0206);
        const double spacing = 1.5, K = 2.0 * sr;
        for (int s = 0; s < kSections; ++s)
        {
            const double f = 20.0 * std::exp2 (spacing * s);
            double fz = f, fp = f * std::exp2 (spacing * std::abs (alpha));
            if (alpha < 0.0) std::swap (fz, fp);
            auto warp = [sr] (double hz) { return 2.0 * sr * std::tan (juce::MathConstants<double>::pi * juce::jmin (hz, sr * 0.46) / sr); };
            const double wz = warp (fz), wp = warp (fp);
            // H(s) = (1 + s / wz) / (1 + s / wp), bilinear
            const double a0 = K / wp + 1.0;
            c.b0[s] = (float) ((K / wz + 1.0) / a0);
            c.b1[s] = (float) ((1.0 - K / wz) / a0);
            c.a1[s] = (float) ((1.0 - K / wp) / a0);
        }
        // 0 dB at Pivot, then Output
        c.gain = 1.0f;
        const double atPivot = std::abs (respond (c, pivot, sr));
        c.gain = (float) (std::pow (10.0, outDb / 20.0) / juce::jmax (1.0e-6, atPivot));
        return c;
    }

    Co co {};
    float z[2][kSections] {};
    float lastKey { -1.0f };
    int countdown { 0 };
};

AQUANODE_REGISTER_MODULE (RippleEQModule, RippleEQModule::descriptor)
AQUANODE_REGISTER_MODULE (FMFilterModule, FMFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (AudioToFilterModule, AudioToFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (TiltEQModule, TiltEQModule::descriptor)
