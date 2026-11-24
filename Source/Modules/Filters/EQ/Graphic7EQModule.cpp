//==============================================================================
// (6.9.19) Graphic 7 EQ - seven filters in a row, each its own kind: Bell, Low
// or High Shelf, Low Pass, High Pass, Band Pass or Notch (or Off), with
// Frequency, Q and Volume (a bell's or a shelf's boost or cut; the level of a
// pass or a notch). The card is a graph with a dot for every filter (as the
// 3-Bell EQ): drag a dot sideways for its frequency, up and down for its
// volume, the wheel (or a right-drag) for its Q; tapping a dot selects it, and
// the knobs show the selected filter only, following the drag.
//
//   Delta: Audio Out plays only what the EQ changed - the sound with the
//          original taken back out (the EQ's output minus its input, so the
//          untouched parts cancel to silence).
//   Suppress: resonances in that changed part are pulled back as they ring
//          (24 bands listening to it, each cut where it stands out above
//          its neighbours, like the Resonance Suppressor), so a big boost
//          does not whistle. 0: off (the EQ exactly as set).
//   Role (6.9.20, per dot): EQ + Suppress (as before), EQ Only, Suppress
//          Only / Excite Only (no filter: a region where resonances in the
//          whole sound are pulled down / pushed up, as far as its Volume).
//          Bands, Threshold and Cut: how Suppress listens and how far.
//          Amount (6.9.21, per dot): its suppression / excitation stronger.
//   Spectrum: the output's spectrum (a periodogram of Audio Out, after the
//          EQ) under the curve. The suppression shows as an orange line
//          under the 0 dB line, each band's cut joined into one smooth line.
//
// RBJ biquads (no latency). Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Core/FilterResponse.h"
#include "../../Effects/Spectral/SpectralCore.h"

using namespace aquanode;

namespace graphic7
{
    constexpr int kBands = 7;
    const float kDefaultHz[kBands] = { 60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 14000.0f };
    inline const juce::StringArray& types()
    {
        static const juce::StringArray t { "Off", "Bell", "Low Shelf", "High Shelf", "Low Pass", "High Pass", "Band Pass", "Notch" };
        return t;
    }
    struct Co { double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };

    // RBJ cookbook; Volume: a bell's / shelf's gain, a pass's or a notch's level
    inline Co design (int type, double hz, double q, double volDb, double sr)
    {
        Co c;
        if (type <= 0) return c;
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.49, hz) / sr;
        const double cs = std::cos (w0), sn = std::sin (w0), al = sn / (2.0 * juce::jmax (0.05, q));
        const double A = std::pow (10.0, volDb / 40.0), lvl = std::pow (10.0, volDb / 20.0);
        double b0, b1, b2, a0, a1, a2;
        switch (type)
        {
            case 1: b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; break;
            case 2: case 3:
            {
                const double sA = 2.0 * std::sqrt (A) * al, s = type == 2 ? 1.0 : -1.0;
                b0 = A * ((A + 1) - s * (A - 1) * cs + sA); b1 = s * 2 * A * ((A - 1) - s * (A + 1) * cs); b2 = A * ((A + 1) - s * (A - 1) * cs - sA);
                a0 = (A + 1) + s * (A - 1) * cs + sA;       a1 = -s * 2 * ((A - 1) + s * (A + 1) * cs);   a2 = (A + 1) + s * (A - 1) * cs - sA;
                break;
            }
            case 4: b0 = (1 - cs) / 2 * lvl; b1 = (1 - cs) * lvl; b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
            case 5: b0 = (1 + cs) / 2 * lvl; b1 = -(1 + cs) * lvl; b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
            case 6: b0 = al * lvl; b1 = 0; b2 = -al * lvl; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;   // unity peak
            default: b0 = lvl; b1 = -2 * cs * lvl; b2 = lvl; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break; // notch
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    struct Biquad
    {
        double z1 { 0 }, z2 { 0 };
        float tick (const Co& c, float x)
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return (float) y;
        }
    };

    // the resonance taming (the Resonance Suppressor's way): n bands listening (6.9.20: 8 to 48,
    // 24 as before), each cut where it stands out above its quieter neighbours by more than
    // Threshold (4 dB before), at most Max Cut (24 dB before). A region bank (6.9.20) does the
    // same on the whole sound inside the Suppress-only dots' regions, and lifts instead of cutting
    // inside the Excite-only dots' regions (each band as far as its dots allow).
    struct Tamer
    {
        static constexpr int kMax = 48;
        struct Svf
        {
            float g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
            void set (float gIn, float kIn) { g = gIn; k = kIn; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
            float band (float x)
            {
                const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
                return v1;
            }
        };
        int n { 24 };
        Svf det[kMax], cut[2][kMax];
        float gOf[kMax] {}, power[kMax] {}, cutK[kMax] {}, atkK[kMax] {}, relK[kMax] {}, q { 4.0f };
        float limitCut[kMax] {}, limitLift[kMax] {};   // (the region bank: how far each band may go)
        float depthScale[kMax] {};                     // (6.9.21: the dots' Amount, per band; 1 = as set)
        bool scaled { false };
        bool active[kMax] {};
        bool region { false };
        float threshold { 4.0f }, maxCut { 24.0f };
        int ctl { 0 };
        std::atomic<float> shownCut[kMax] {};   // (the card's line, dB: + a cut, - a lift)

        static float bandHz (int b, int numBands, double sr)
        {
            return juce::jmin ((float) sr * 0.45f, 50.0f * std::pow (16000.0f / 50.0f, ((float) b + 0.5f) / (float) numBands));
        }

        void prepare (double sr, int numBands)
        {
            n = juce::jlimit (4, kMax, numBands);
            // (24 bands, the count fixed when the code is built - as before 6.9.20, to the last bit)
            if (n == 24) layout<24> (sr); else layoutAny (sr, n);
            reset();
        }
        template <int N> void layout (double sr)
        {
            const float lo = 50.0f, hi = 16000.0f, bw = std::log2 (hi / lo) / (float) N;
            q = 1.0f / (2.0f * std::sinh (std::log (2.0f) * 0.5f * bw));
            for (int b = 0; b < N; ++b)
                setBand (b, juce::jmin ((float) sr * 0.45f, lo * std::pow (hi / lo, ((float) b + 0.5f) / (float) N)), sr);
        }
        void layoutAny (double sr, int numBands)
        {
            const float lo = 50.0f, hi = 16000.0f, bw = std::log2 (hi / lo) / (float) numBands;
            q = 1.0f / (2.0f * std::sinh (std::log (2.0f) * 0.5f * bw));
            for (int b = 0; b < numBands; ++b)
                setBand (b, bandHz (b, numBands, sr), sr);
        }
        void setBand (int b, float f, double sr)
        {
            gOf[b] = std::tan (juce::MathConstants<float>::pi * f / (float) sr);
            det[b].set (gOf[b], 1.0f / q);
            atkK[b] = 1.0f - std::exp (-1.0f / (juce::jmax (0.003f, 2.0f / f) * (float) sr));
            relK[b] = 1.0f - std::exp (-1.0f / (juce::jmax (0.08f, 6.0f / f) * (float) sr));
        }
        void reset()
        {
            for (int b = 0; b < kMax; ++b) { det[b].ic1 = det[b].ic2 = 0; cut[0][b].ic1 = cut[0][b].ic2 = cut[1][b].ic1 = cut[1][b].ic2 = 0; power[b] = 1.0e-12f; active[b] = false; shownCut[b].store (0.0f); }
            ctl = 0;
        }
        void process (float& l, float& r, float depth)
        {
            const float d = 0.5f * (l + r);
            for (int b = 0; b < n; ++b)
            {
                const float y = det[b].band (d) * det[b].k, p = y * y;
                power[b] += (p > power[b] ? atkK[b] : relK[b]) * (p - power[b]);
            }
            if (ctl-- <= 0) { ctl = 15; update (depth); }
            for (int b = 0; b < n; ++b)
                if (active[b]) { l += cutK[b] * cut[0][b].band (l); r += cutK[b] * cut[1][b].band (r); }
            if (! std::isfinite (l) || ! std::isfinite (r)) { reset(); l = r = 0.0f; }
        }
        void update (float depth)
        {
            float db[kMax];
            for (int b = 0; b < n; ++b) db[b] = 10.0f * std::log10 (power[b] + 1.0e-12f);
            // the neighbours 2 to 4 bands away at 24 bands (as many octaves away at other counts)
            const int k0 = juce::jmax (1, (int) std::lround (2.0 * n / 24.0)), k1 = juce::jmax (k0 + 1, (int) std::lround (4.0 * n / 24.0));
            for (int b = 0; b < n; ++b)
            {
                float around[2 * kMax];
                int m = 0;
                for (int k = k0; k <= k1; ++k)
                    for (int o : { b - k, b + k })
                        if (o >= 0 && o < n) around[m++] = db[o];
                std::sort (around, around + m);
                const int half = juce::jmax (1, m / 2);
                float sum = 0.0f;
                for (int i = 0; i < half; ++i) sum += around[i];
                float excess = db[b] > -90.0f ? db[b] - sum / (float) half - threshold : 0.0f;
                if (scaled) excess *= depthScale[b];
                // a cut (+) or, in the region bank, a lift (-)
                float target = juce::jlimit (0.0f, region ? juce::jmin (maxCut, limitCut[b]) : maxCut, excess * depth);
                if (region) target -= juce::jlimit (0.0f, juce::jmin (maxCut, limitLift[b]), excess * depth);
                const bool cutting = std::abs (target) > 0.02f;
                shownCut[b].store (target, std::memory_order_relaxed);
                if (cutting && ! active[b]) { cut[0][b].ic1 = cut[0][b].ic2 = cut[1][b].ic1 = cut[1][b].ic2 = 0; }
                active[b] = cutting;
                if (cutting)
                {
                    const float A = std::pow (10.0f, -target / 40.0f), k = 1.0f / (q * A);
                    cut[0][b].set (gOf[b], k);
                    cut[1][b].set (gOf[b], k);
                    cutK[b] = k * (A * A - 1.0f);
                }
            }
        }
    };
}

class Graphic7EQModule final : public SynthModule
{
public:
    // per band i: type, freq, q, volume at 4i .. 4i+3; then the globals
    enum G { pOutput = 4 * graphic7::kBands, pMix, pDelta, pSuppress, pSpectrum, pSelected,
             pRole0, pBands = pRole0 + graphic7::kBands, pMaxCut, pThreshold, pAmount0 };
    // (6.9.20) a dot's Role: EQ + Suppress (as before), EQ Only (Suppress leaves its change alone),
    // Suppress Only / Excite Only (no filter: a region where resonances in the whole sound are
    // pulled down / pushed up, as far as its Volume, Q its width)
    enum Role { roleEqSuppress = 0, roleEqOnly, roleSuppressOnly, roleExciteOnly };
    static constexpr int kBandChoices[6] = { 8, 12, 16, 24, 32, 48 };
    static constexpr int kScope = 2048;

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.graphic7";
        d.displayName = "Graphic 7 EQ";
        d.searchKeywords = "graphic parametric eq seven band 7 bell shelf pass notch delta spectrum periodogram resonance suppress";
        d.description = "Seven filters, a dot for each on the graph: drag a dot sideways for its frequency, up and down for "
                        "its volume, the wheel (or a right-drag) for its Q. Tap a dot to select it - the knobs show that "
                        "filter: Bell, Low or High Shelf, Low Pass, High Pass, Band Pass, Notch or Off. Delta plays only "
                        "what the EQ changed (the original taken back out). Suppress pulls back resonances in that changed "
                        "part as they ring (the orange line shows how much): Bands, Threshold and Cut set how it listens and "
                        "how far it goes. Each dot's Role: EQ + Suppress, EQ Only (Suppress leaves its change alone), Suppress "
                        "Only or Excite Only (the dot filters nothing: in its region - Q its width - resonances in the whole "
                        "sound are pulled down, or pushed up, as far as its Volume) - and each dot's Amount makes its suppression "
                        "or excitation stronger (up to ten times; 0 % takes it away). Spectrum shows the output's spectrum "
                        "under the curve.";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 31;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        for (int i = 0; i < graphic7::kBands; ++i)
        {
            const auto n = juce::String (i + 1);
            const float sel = (float) i;
            // (each filter on its own row: only the selected one's row shows, the others' rows fold away)
            d.params.push_back (makeCombo  ("type" + n, "Type " + n, graphic7::types(), 1, i, 2).noMod().visibleWhen ("selected", sel));
            d.params.push_back (makeRotary ("freq" + n, "Freq " + n, 20.0f, 20000.0f, graphic7::kDefaultHz[i], i, "Hz", true).visibleWhen ("selected", sel));
            d.params.push_back (makeRotary ("q" + n,    "Q " + n, 0.1f, 18.0f, 1.0f, i, {}, true).visibleWhen ("selected", sel));
            d.params.push_back (makeRotary ("vol" + n,  "Volume " + n, -24.0f, 24.0f, 0.0f, i, "dB").visibleWhen ("selected", sel));
        }
        d.params.push_back (makeRotary ("output",   "Output",   -24.0f, 12.0f, 0.0f, 7, "dB"));
        d.params.push_back (makeRotary ("mix",      "Mix",      0.0f, 100.0f, 100.0f, 7, "%"));
        d.params.push_back (makeCombo  ("delta",    "Delta",    { "Off", "On" }, 0, 8, 2).noMod());
        d.params.push_back (makeRotary ("suppress", "Suppress", 0.0f, 100.0f, 0.0f, 7, "%"));
        d.params.push_back (makeCombo  ("spectrum", "Spectrum", { "Off", "On" }, 1, 8, 2).noMod());
        d.params.push_back (makeCombo  ("selected", "Band", { "1", "2", "3", "4", "5", "6", "7" }, 0, 7, 2).noMod());
        // (6.9.20) each dot's Role (by its knobs), and how Suppress listens
        for (int i = 0; i < graphic7::kBands; ++i)
            d.params.push_back (makeCombo ("role" + juce::String (i + 1), "Role " + juce::String (i + 1),
                                           { "EQ + Suppress", "EQ Only", "Suppress Only", "Excite Only" }, 0, i, 2)
                                    .noMod().visibleWhen ("selected", (float) i));
        d.params.push_back (makeCombo  ("bands",     "Bands",     { "8", "12", "16", "24", "32", "48" }, 3, 9, 2).noMod());
        d.params.push_back (makeRotary ("maxCut",    "Cut",       0.0f, 48.0f, 24.0f, 9, "dB"));   // (6.9.21: was Max Cut)
        d.params.push_back (makeRotary ("threshold", "Threshold", 0.0f, 12.0f, 4.0f, 9, "dB"));
        // (6.9.21) each dot's Amount: more (or less) of its suppression / excitation - deeper and further
        for (int i = 0; i < graphic7::kBands; ++i)
            d.params.push_back (makeRotary ("amount" + juce::String (i + 1), "Amount " + juce::String (i + 1), 0.0f, 1000.0f, 100.0f, i, "%")
                                    .visibleWhen ("selected", (float) i));
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    int preferredModuleWidth() const override { return 420; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        numBands = kBandChoices[juce::jlimit (0, 5, (int) getParameterBase (pBands))];
        tamer.prepare (sr, numBands);
        regionBank.region = true;
        regionBank.prepare (sr, numBands);
        for (auto& k : keys) k = -1.0f;
        reset();
    }
    void reset() override
    {
        for (auto& c : f) for (auto& b : c) b = {};
        tamer.reset();
        regionBank.reset();
        countdown = 0;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (--countdown <= 0)
        {
            countdown = 16;
            allSuppressed = true;
            anyRegion = false;
            for (int i = 0; i < graphic7::kBands; ++i)
            {
                const int t = (int) param (4 * i);
                const float hz = param (4 * i + 1), q = param (4 * i + 2), v = param (4 * i + 3);
                role[i] = juce::jlimit (0, 3, (int) param (pRole0 + i));
                const float key = (float) t * 1000003.0f + hz + q * 3.17f + v * 0.731f;
                if (key != keys[i]) { keys[i] = key; co[i] = graphic7::design (t, hz, q, v, sampleRate); }
                on[i] = t > 0 && role[i] <= roleEqOnly;
                if (on[i] && role[i] == roleEqOnly) allSuppressed = false;
                if (role[i] >= roleSuppressOnly) anyRegion = true;
            }
            updateDeltaAmounts();
            // (6.9.20) Bands, Threshold, Max Cut; the regions of the Suppress / Excite Only dots
            const int nb = kBandChoices[juce::jlimit (0, 5, (int) param (pBands))];
            if (nb != numBands) { numBands = nb; tamer.prepare (sampleRate, nb); regionBank.prepare (sampleRate, nb); }
            // (to a hundredth of a dB: the knobs' 4 and 24 dB exactly, as the suppressor had them fixed before)
            tamer.threshold = regionBank.threshold = std::round (param (pThreshold) * 100.0f) / 100.0f;
            tamer.maxCut = regionBank.maxCut = std::round (param (pMaxCut) * 100.0f) / 100.0f;
            if (anyRegion) updateRegions();
        }
        const float xl = in[0][0], xr = in[0][1];
        float yl = xl, yr = xr;
        // what the EQ changed: the dots set to EQ Only keep their change out of Suppress
        // (when none is, the whole change - y - x - as before)
        float nl = 0.0f, nr = 0.0f;
        for (int i = 0; i < graphic7::kBands; ++i)
            if (on[i])
            {
                const float pl = yl, pr = yr;
                yl = f[0][i].tick (co[i], yl); yr = f[1][i].tick (co[i], yr);
                if (role[i] == roleEqOnly) { nl += yl - pl; nr += yr - pr; }
            }

        // the change, resonances in it pulled back (Suppress), and the dry / wet mix
        float dl = yl - xl, dr = yr - xr;
        if (! allSuppressed) { dl -= nl; dr -= nr; }
        const float sup = param (pSuppress) * 0.01f;
        if (sup > 0.0f) tamer.process (dl, dr, sup * 2.0f);
        else if (suppressing) { suppressing = false; for (auto& c : tamer.shownCut) c.store (0.0f, std::memory_order_relaxed); }
        suppressing = sup > 0.0f;
        if (! allSuppressed) { dl += nl; dr += nr; }
        // (6.9.20) the Suppress / Excite Only regions, on the whole sound as it now is
        if (anyRegion)
        {
            float zl = xl + dl, zr = xr + dr;
            regionBank.process (zl, zr, 2.0f);
            dl = zl - xl; dr = zr - xr;
            regionWasOn = true;
        }
        else if (regionWasOn) { regionWasOn = false; regionBank.reset(); }
        const float mix = juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f), outG = juce::Decibels::decibelsToGain (param (pOutput));
        float ol, orr;
        if (param (pDelta) > 0.5f) { ol = dl * mix; orr = dr * mix; }
        else { ol = xl + dl * mix; orr = xr + dr * mix; }
        out[0] = { ol * outG, orr * outG };

        scope[(size_t) scopePos] = 0.5f * (out[0][0] + out[0][1]);
        scopePos = (scopePos + 1) % kScope;
        scopeWritten.store (scopePos, std::memory_order_relaxed);
    }

    // the curve the dots make (the card's graph), from the knobs as they are now
    void curveDb (const float* hz, float* db, int n) const
    {
        graphic7::Co c[graphic7::kBands];
        bool use[graphic7::kBands];
        for (int i = 0; i < graphic7::kBands; ++i)
        {
            const int t = (int) getParameterBase (4 * i);
            use[i] = t > 0 && roleOf (i) <= roleEqOnly;   // (the Suppress / Excite Only dots filter nothing)
            c[i] = graphic7::design (t, getParameterBase (4 * i + 1), getParameterBase (4 * i + 2), getParameterBase (4 * i + 3), sampleRate);
        }
        const double mix = juce::jlimit (0.0, 1.0, getParameterBase (pMix) * 0.01), outG = std::pow (10.0, getParameterBase (pOutput) / 20.0);
        const bool delta = getParameterBase (pDelta) > 0.5f;
        for (int k = 0; k < n; ++k)
        {
            const auto z1 = response::zInv (hz[k], sampleRate);
            response::C h = 1.0;
            for (int i = 0; i < graphic7::kBands; ++i)
                if (use[i]) h *= response::biquad (c[i].b0, c[i].b1, c[i].b2, c[i].a1, c[i].a2, z1);
            const response::C changed = (h - 1.0) * mix;
            db[k] = response::db ((delta ? changed : 1.0 + changed) * outG);
        }
    }

    // the spectrum's samples (message thread): the last n of Audio Out
    void copyScope (float* dst, int n) const
    {
        const int end = scopeWritten.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) dst[i] = scope[(size_t) ((end - n + i + kScope) % kScope)];
    }
    bool spectrumOn() const { return getParameterBase (pSpectrum) > 0.5f; }
    int roleOf (int dot) const { return juce::jlimit (0, 3, (int) getParameterBase (pRole0 + dot)); }
    // the card's line: the suppression bands, and each one's change in dB (- a cut, + a lift)
    int suppressBands() const { return tamer.n; }
    float suppressCutDb (int band) const { return tamer.shownCut[juce::jlimit (0, graphic7::Tamer::kMax - 1, band)].load (std::memory_order_relaxed); }
    float regionDb (int band) const { return -regionBank.shownCut[juce::jlimit (0, graphic7::Tamer::kMax - 1, band)].load (std::memory_order_relaxed); }
    float suppressLineDb (int band) const { return -suppressCutDb (band) + regionDb (band); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 170; }

private:
    graphic7::Co co[graphic7::kBands];
    graphic7::Biquad f[2][graphic7::kBands];
    bool on[graphic7::kBands] {};
    float keys[graphic7::kBands] {};
    graphic7::Tamer tamer, regionBank;
    bool suppressing { false }, allSuppressed { true }, anyRegion { false }, regionWasOn { false };
    int role[graphic7::kBands] {};
    int numBands { 24 };

    // (6.9.21) the EQ + Suppress dots' Amount: the change's suppression deeper (or gentler) in their region
    void updateDeltaAmounts()
    {
        bool any = false;
        for (int i = 0; i < graphic7::kBands; ++i)
            if (on[i] && role[i] == roleEqSuppress && param (pAmount0 + i) != 100.0f) any = true;
        tamer.scaled = any;
        if (! any) return;
        for (int b = 0; b < tamer.n; ++b) tamer.depthScale[b] = 1.0f;
        for (int i = 0; i < graphic7::kBands; ++i)
        {
            if (! on[i] || role[i] != roleEqSuppress) continue;
            const float amt = param (pAmount0 + i) * 0.01f;
            for (int b = 0; b < tamer.n; ++b)
            {
                const float w = regionWeight (i, graphic7::Tamer::bandHz (b, tamer.n, sampleRate));
                auto& d = tamer.depthScale[b];
                d = amt >= 1.0f ? juce::jmax (d, 1.0f + (amt - 1.0f) * w) : juce::jmin (d, 1.0f - (1.0f - amt) * w);
            }
        }
    }
    float regionWeight (int dot, float bandHz) const
    {
        const float hz = param (4 * dot + 1), q = juce::jmax (0.1f, param (4 * dot + 2));
        const float bwOct = 2.0f * std::asinh (1.0f / (2.0f * q)) / std::log (2.0f);
        const float u = std::log2 (bandHz / hz) / juce::jmax (0.05f, bwOct * 0.5f);
        return std::exp (-0.5f * u * u);
    }

    // each region band's reach: the dots' Volume (as dB, either sign) under a bell as wide as their Q,
    // times their Amount (6.9.21: which also makes it go there harder)
    void updateRegions()
    {
        regionBank.scaled = true;
        for (int b = 0; b < regionBank.n; ++b) { regionBank.limitCut[b] = 0.0f; regionBank.limitLift[b] = 0.0f; regionBank.depthScale[b] = 1.0f; }
        for (int i = 0; i < graphic7::kBands; ++i)
        {
            if (role[i] < roleSuppressOnly) continue;
            const float hz = param (4 * i + 1), q = juce::jmax (0.1f, param (4 * i + 2)), vol = std::abs (param (4 * i + 3));
            const float bwOct = 2.0f * std::asinh (1.0f / (2.0f * q)) / std::log (2.0f);
            const float sigma = juce::jmax (0.05f, bwOct * 0.5f);
            const float amt = param (pAmount0 + i) * 0.01f;
            for (int b = 0; b < regionBank.n; ++b)
            {
                const float u = std::log2 (graphic7::Tamer::bandHz (b, regionBank.n, sampleRate) / hz) / sigma;
                const float w = std::exp (-0.5f * u * u);
                const float reach = vol * w * amt;
                auto& lim = role[i] == roleSuppressOnly ? regionBank.limitCut[b] : regionBank.limitLift[b];
                if (reach > lim) { lim = reach; regionBank.depthScale[b] = juce::jmax (0.0f, 1.0f + (amt - 1.0f) * w); }
            }
        }
    }
    std::array<float, kScope> scope {};
    int scopePos { 0 };
    std::atomic<int> scopeWritten { 0 };
    int countdown { 0 };
};

//==============================================================================
// the card's graph: the curve, a dot per filter (drag: frequency sideways, volume
// up and down; wheel or right-drag: Q; a tap selects - the knobs show that filter),
// the output's spectrum under it and the suppression as an orange line
class Graphic7View final : public juce::Component,
                           public CustomParamCableTargets,
                           public ExtraContentParamListener,
                           private juce::Timer
{
public:
    explicit Graphic7View (Graphic7EQModule& m) : module (&m)
    {
        fft = std::make_unique<FftRadix2> (11);
        std::fill (std::begin (spec), std::end (spec), -120.0f);
        startTimerHz (25);
    }

    void paint (juce::Graphics& g) override
    {
        auto* eq = eqModule();
        if (eq == nullptr) return;
        const auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (b, 3.0f);

        // grid: 0 dB, +-12 dB, 100 Hz / 1 kHz / 10 kHz
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (float f : { 100.0f, 1000.0f, 10000.0f }) g.drawVerticalLine ((int) freqToX (f), b.getY(), b.getBottom());
        for (float d : { -12.0f, 12.0f }) g.drawHorizontalLine ((int) dbToY (d), b.getX(), b.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine ((int) dbToY (0.0f), b.getX(), b.getRight());
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawText ("100", juce::Rectangle<float> (freqToX (100.0f) + 2.0f, b.getBottom() - 11.0f, 30.0f, 10.0f), juce::Justification::left, false);
        g.drawText ("1k", juce::Rectangle<float> (freqToX (1000.0f) + 2.0f, b.getBottom() - 11.0f, 30.0f, 10.0f), juce::Justification::left, false);
        g.drawText ("10k", juce::Rectangle<float> (freqToX (10000.0f) + 2.0f, b.getBottom() - 11.0f, 30.0f, 10.0f), juce::Justification::left, false);

        const int w = juce::jmax (2, getWidth());
        // the output's spectrum (after the EQ), filled from the bottom
        if (eq->spectrumOn())
        {
            juce::Path sp;
            sp.startNewSubPath (0.0f, b.getBottom());
            for (int x = 0; x < w; ++x)
                sp.lineTo ((float) x, specToY (spec[(size_t) juce::jmin (x, kMaxW - 1)]));
            sp.lineTo ((float) (w - 1), b.getBottom());
            sp.closeSubPath();
            g.setColour (juce::Colour (0xff6f9ad9).withAlpha (0.28f));
            g.fillPath (sp);
            g.setColour (juce::Colour (0xff6f9ad9).withAlpha (0.55f));
            g.strokePath (sp, juce::PathStrokeType (0.8f));
        }

        // (6.9.20) the Suppress / Excite Only dots' regions, shaded (orange: pulled down, green: pushed up)
        for (int i = 0; i < graphic7::kBands; ++i)
        {
            const int r = eq->roleOf (i);
            if (r < Graphic7EQModule::roleSuppressOnly) continue;
            const float hz = eq->getParameterBase (4 * i + 1), q = juce::jmax (0.1f, eq->getParameterBase (4 * i + 2));
            const float bw = 2.0f * std::asinh (1.0f / (2.0f * q)) / std::log (2.0f);
            const float x0 = freqToX (hz * std::exp2 (-bw * 0.5f)), x1 = freqToX (hz * std::exp2 (bw * 0.5f));
            g.setColour ((r == Graphic7EQModule::roleSuppressOnly ? kSuppressColour : kExciteColour).withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, b.getY(), juce::jmax (x0 + 2.0f, x1), b.getBottom()));
        }

        // the suppression: each band's change, one smooth line (under 0 dB a cut, above it a lift)
        {
            const int nb = eq->suppressBands();
            float vals[graphic7::Tamer::kMax];
            float most = 0.0f;
            for (int k = 0; k < nb; ++k) { vals[k] = eq->suppressLineDb (k); most = juce::jmax (most, std::abs (vals[k])); }
            if (most > 0.05f)
            {
                juce::Path sl;
                for (int x = 0; x < w; x += 2)
                {
                    // the band position of this pixel, the bands joined with a cosine between them
                    const float hz = xToFreq ((float) x);
                    const float pos = std::log (hz / 50.0f) / std::log (16000.0f / 50.0f) * (float) nb - 0.5f;
                    const int k0 = juce::jlimit (0, nb - 1, (int) std::floor (pos)), k1 = juce::jmin (nb - 1, k0 + 1);
                    const float t = juce::jlimit (0.0f, 1.0f, pos - (float) k0), s = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
                    const float c = pos < 0.0f ? vals[0] * juce::jmax (0.0f, 1.0f + pos) : vals[k0] * (1.0f - s) + vals[k1] * s;
                    const float y = dbToY (c);
                    if (x == 0) sl.startNewSubPath ((float) x, y); else sl.lineTo ((float) x, y);
                }
                g.setColour (kSuppressColour);
                g.strokePath (sl, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
            }
        }

        // the curve
        {
            std::vector<float> hz ((size_t) w), db ((size_t) w);
            for (int x = 0; x < w; ++x) hz[(size_t) x] = xToFreq ((float) x);
            eq->curveDb (hz.data(), db.data(), w);
            juce::Path path;
            for (int x = 0; x < w; ++x)
            {
                const float y = dbToY (db[(size_t) x]);
                if (x == 0) path.startNewSubPath (0.0f, y); else path.lineTo ((float) x, y);
            }
            g.setColour (juce::Colour (0xff6fd9c4));
            g.strokePath (path, juce::PathStrokeType (1.7f));
        }

        // the dots (an Off filter's hollow), the selected one ringed
        const int sel = selected();
        for (int i = 0; i < graphic7::kBands; ++i)
        {
            const auto c = dotCentre (i).toFloat();
            const int role = eq->roleOf (i);
            const bool off = (int) eq->getParameterBase (4 * i) == 0 && role <= Graphic7EQModule::roleEqOnly, isSel = i == sel;
            const float r = isSel ? 6.5f : 5.5f;
            if (role >= Graphic7EQModule::roleSuppressOnly)
            {
                // a region dot: a diamond in its colour
                const auto col = role == Graphic7EQModule::roleSuppressOnly ? kSuppressColour : kExciteColour;
                juce::Path d;
                d.addQuadrilateral (c.x, c.y - r - 1.0f, c.x + r + 1.0f, c.y, c.x, c.y + r + 1.0f, c.x - r - 1.0f, c.y);
                g.setColour (col.withAlpha (isSel ? 1.0f : 0.85f));
                g.fillPath (d);
                g.setColour (juce::Colours::white.withAlpha (isSel ? 0.95f : 0.4f));
                g.strokePath (d, juce::PathStrokeType (isSel ? 1.6f : 1.0f));
                g.setColour (juce::Colours::black.withAlpha (0.8f));
                g.setFont (juce::Font (juce::FontOptions (9.0f)));
                g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r), juce::Justification::centred, false);
                continue;
            }
            if (off) { g.setColour (kColours[i].withAlpha (0.5f)); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.4f); }
            else { g.setColour (kColours[i].withAlpha (isSel ? 1.0f : 0.85f)); g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r); }
            g.setColour (juce::Colours::white.withAlpha (isSel ? 0.95f : 0.4f));
            g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, isSel ? 1.6f : 1.0f);
            g.setColour (off ? kColours[i] : juce::Colours::black.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r), juce::Justification::centred, false);
        }
        // the selected filter in words
        {
            const int t = (int) eq->getParameterBase (4 * sel), role = eq->roleOf (sel);
            const juce::String kind = role == Graphic7EQModule::roleSuppressOnly ? juce::String ("Suppress Only")
                                    : role == Graphic7EQModule::roleExciteOnly ? juce::String ("Excite Only") : graphic7::types()[t];
            const auto text = juce::String (sel + 1) + ": " + kind + "  " + hzText (eq->getParameterBase (4 * sel + 1))
                              + "  Q " + juce::String (eq->getParameterBase (4 * sel + 2), 2) + "  " + juce::String (eq->getParameterBase (4 * sel + 3), 1) + " dB";
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawText (text, b.reduced (6.0f, 3.0f).removeFromTop (12.0f), juce::Justification::left, false);
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = dotAt (e.getPosition());
        if (dragged < 0) return;
        rightDrag = e.mods.isPopupMenu();
        dragStartY = e.position.y;
        if (auto* eq = eqModule()) dragStartQ = eq->getParameterBase (4 * dragged + 2);
        select (dragged);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* eq = eqModule();
        if (eq == nullptr || dragged < 0) return;
        const auto n = juce::String (dragged + 1);
        if (rightDrag)
            eq->setParameter ("q" + n, juce::jlimit (0.1f, 18.0f, dragStartQ * std::exp2 ((dragStartY - e.position.y) / 30.0f)));
        else
        {
            eq->setParameter ("freq" + n, juce::jlimit (20.0f, 20000.0f, xToFreq (e.position.x)));
            eq->setParameter ("vol" + n, juce::jlimit (-24.0f, 24.0f, yToDb (e.position.y)));
            if ((int) eq->getParameterBase (4 * dragged) == 0 && eq->roleOf (dragged) <= Graphic7EQModule::roleEqOnly)
                eq->setParameter ("type" + n, 1.0f);   // (an Off filter dragged: a Bell)
        }
        if (paramValuesChangedByContent) paramValuesChangedByContent();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragged = -1; repaint(); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // a double-tap on a dot: back to 0 dB
        const int d = dotAt (e.getPosition());
        if (auto* eq = eqModule(); eq != nullptr && d >= 0)
        {
            eq->setParameter ("vol" + juce::String (d + 1), 0.0f);
            if (paramValuesChangedByContent) paramValuesChangedByContent();
            repaint();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        auto* eq = eqModule();
        const int d = dotAt (e.getPosition());
        if (eq == nullptr || d < 0) return;
        const auto id = "q" + juce::String (d + 1);
        eq->setParameter (id, juce::jlimit (0.1f, 18.0f, eq->getParameterBase (4 * d + 2) * (1.0f + wheel.deltaY * 0.6f)));
        select (d);
        if (paramValuesChangedByContent) paramValuesChangedByContent();
        repaint();
    }

    // a cable dropped on a dot drives its frequency
    juce::String paramTargetAt (juce::Point<int> p) const override
    {
        const int d = dotAt (p);
        return d < 0 ? juce::String() : "freq" + juce::String (d + 1);
    }
    juce::Point<int> paramTargetCentre (const juce::String& id) const override
    {
        for (int i = 0; i < graphic7::kBands; ++i)
            if (id == "freq" + juce::String (i + 1)) return dotCentre (i);
        return { -1, -1 };
    }

private:
    static constexpr float kRangeDb = 26.0f, kSpecTop = 0.0f, kSpecBottom = -96.0f;
    static constexpr int kMaxW = 1024;
    const juce::Colour kSuppressColour { 0xffffa24a }, kExciteColour { 0xff7be08a };
    const juce::Colour kColours[graphic7::kBands] = {
        juce::Colour (0xffd970b0), juce::Colour (0xffd9b070), juce::Colour (0xff70b0d9), juce::Colour (0xff8fd970),
        juce::Colour (0xffb08fd9), juce::Colour (0xffd98f70), juce::Colour (0xff70d9c8)
    };

    Graphic7EQModule* eqModule() const { return dynamic_cast<Graphic7EQModule*> (module.get()); }
    int selected() const { auto* eq = eqModule(); return eq != nullptr ? juce::jlimit (0, graphic7::kBands - 1, (int) eq->getParameterBase (Graphic7EQModule::pSelected)) : 0; }
    void select (int band)
    {
        auto* eq = eqModule();
        if (eq == nullptr || band == selected()) { repaint(); return; }
        eq->setParameter ("selected", (float) band);
        if (paramsChangedByContent) paramsChangedByContent();   // the card shows that filter's knobs
        repaint();
    }

    static juce::String hzText (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }
    float freqToX (float f) const { return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (1.0f, f) / 20.0f) / std::log (1000.0f)) * (float) getWidth(); }
    float xToFreq (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth()))); }
    float dbToY (float db) const { return (0.5f - juce::jlimit (-1.0f, 1.0f, db / kRangeDb) * 0.5f) * (float) getHeight(); }
    float yToDb (float y) const { return (0.5f - juce::jlimit (0.0f, 1.0f, y / (float) juce::jmax (1, getHeight()))) * 2.0f * kRangeDb; }
    float specToY (float db) const { return (1.0f - juce::jlimit (0.0f, 1.0f, (db - kSpecBottom) / (kSpecTop - kSpecBottom))) * (float) getHeight(); }

    juce::Point<int> dotCentre (int i) const
    {
        auto* eq = eqModule();
        if (eq == nullptr) return { -1, -1 };
        return { (int) freqToX (eq->getParameterBase (4 * i + 1)), (int) dbToY (eq->getParameterBase (4 * i + 3)) };
    }
    int dotAt (juce::Point<int> p) const
    {
        int best = -1, bestDist = 14;   // the grab radius (a finger's)
        for (int i = 0; i < graphic7::kBands; ++i)
        {
            // the selected dot wins a tie, so stacked dots can still be pulled apart
            const int d = (int) p.getDistanceFrom (dotCentre (i)) - (i == selected() ? 2 : 0);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    void timerCallback() override
    {
        auto* eq = eqModule();
        if (eq != nullptr && eq->spectrumOn())
        {
            // the periodogram: 2048 samples of Audio Out, Hann, in dB (a sine at full scale ~ 0 dB),
            // each pixel the loudest bin it covers, falling back gently
            constexpr int n = 2048;
            float buf[n];
            eq->copyScope (buf, n);
            for (int i = 0; i < n; ++i) buf[i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n);
            std::complex<float> bins[n / 2 + 1];
            fft->forwardReal (buf, bins);
            const double sr = juce::jmax (8000.0, eq->tapSampleRate());
            const int w = juce::jmin (kMaxW, juce::jmax (2, getWidth()));
            for (int x = 0; x < w; ++x)
            {
                const float lo = xToFreq ((float) x - 0.5f), hi = xToFreq ((float) x + 0.5f);
                const int b0 = juce::jlimit (1, n / 2, (int) (lo * n / sr)), b1 = juce::jlimit (b0, n / 2, (int) (hi * n / sr));
                float m = 0.0f;
                for (int k = b0; k <= b1; ++k) m = juce::jmax (m, std::abs (bins[k]));
                if (b1 == b0)   // (narrower than a bin: between the two bins around it)
                {
                    const float pos = xToFreq ((float) x) * (float) n / (float) sr;
                    const int k = juce::jlimit (1, n / 2 - 1, (int) pos);
                    const float t = pos - (float) k;
                    m = std::abs (bins[k]) * (1.0f - t) + std::abs (bins[k + 1]) * t;
                }
                const float db = juce::Decibels::gainToDecibels (m / (float) (n / 4), -120.0f);
                auto& s = spec[(size_t) x];
                s = db > s ? db : s + 0.25f * (db - s);
            }
        }
        repaint();
    }

    juce::WeakReference<SynthModule> module;
    std::unique_ptr<FftRadix2> fft;
    std::array<float, kMaxW> spec {};
    int dragged { -1 };
    bool rightDrag { false };
    float dragStartY { 0.0f }, dragStartQ { 1.0f };
};

std::unique_ptr<juce::Component> Graphic7EQModule::createExtraContentComponent()
{
    return std::make_unique<Graphic7View> (*this);
}

AQUANODE_REGISTER_MODULE (Graphic7EQModule, Graphic7EQModule::descriptor)

#if AQUANODE_TEST_HARNESS
// (test harness) the suppression's cut in a band, as the card's line shows it
float graphic7SuppressCutForTest (SynthModule& m, int band)
{
    auto* g = dynamic_cast<Graphic7EQModule*> (&m);
    return g != nullptr ? g->suppressCutDb (band) : -1.0f;
}
#endif

#if AQUANODE_TEST_HARNESS
// (test harness, 6.9.20) the card's suppression line in a band (- a cut, + a lift), and how many bands
float graphic7LineForTest (SynthModule& m, int band, int& numBands)
{
    auto* g = dynamic_cast<Graphic7EQModule*> (&m);
    numBands = g != nullptr ? g->suppressBands() : 0;
    return g != nullptr ? g->suppressLineDb (band) : 0.0f;
}
#endif
