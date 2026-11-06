//==============================================================================
// Resonance Suppressor - 32 bands of dynamic resonance control, zero latency
//
// A bank of 32 band-pass filters, spread evenly in octaves from Low to High,
// listens to the sound (or to Side In) and measures each band against its
// neighbours. Where a band stands out above them - a ringing resonance, a
// harsh whistle, a boomy room mode - by more than Selectivity allows, a bell
// cut at that band pulls it back towards its neighbours, by as much as Depth
// says, following it with Attack and Release. Everything else passes as it
// was: an idle bell (0 dB) is exactly transparent, so a sound without
// resonances comes out sample for sample as it went in.
//
// The cuts are 32 bell filters in series on the audio path - minimum-phase
// filters, no FFT and no look-ahead, so there is no latency at all (a cut
// shifts the phase around its band a little, as any EQ does). Only the bands
// actually cutting are worked out, so a clean sound costs little more than
// the detector.
//
//   Side In: the detector listens to this instead (while it is patched): the
//            sound is then cut where the side signal resonates - a vocal
//            carving its own resonances out of a pad, say.
//   Removed Out: what was taken away (in minus out).
//   Delta: Audio Out plays the removed part too, to hear what is cut.
//   Reactance (off and hidden until its button shows it): a curve drawn over
//            frequency that multiplies each band's cut - 0 leaves a band
//            alone, 1 is as Depth says, 2 cuts twice as hard.
//==============================================================================
#include "../../Core/ModuleCore.h"

namespace
{
    using namespace aquanode;

    constexpr int kMaxBands = 64;       // 32 bands, or 64 (Bands)
    constexpr int kCtl = 16;            // reductions and coefficients every 16 samples
    constexpr float kMaxCutDb = 24.0f;
    // a band is measured against the bands 2..5 away on each side (4..10 with
    // 64 bands - the same stretch in octaves; its direct neighbours hear its own
    // resonance leaking in): the quieter half of them is the floor it stands above
    constexpr int kNearest = 2, kFarthest = 5;

    // the Reactance curve: 64 points from 20 Hz to 20 kHz, each a multiplier 0..2
    constexpr int kCurvePoints = 64;
    constexpr float kCurveLo = 20.0f, kCurveHi = 20000.0f, kCurveMax = 2.0f;
    inline float curvePointHz (int i) { return kCurveLo * std::pow (kCurveHi / kCurveLo, (float) i / (float) (kCurvePoints - 1)); }

    // a TPT state-variable filter used as a band-pass (detector) or a bell (cut)
    struct Svf
    {
        float g = 0.0f, k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
        void setCoeffs (float gIn, float kIn)
        {
            g = gIn; k = kIn;
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0.0f; }
        // returns the band output (v1)
        inline float band (float x)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            return v1;
        }
    };
}

class ResonanceSuppressorModule final : public SynthModule
{
public:
    enum P { pDepth, pSharp, pSelect, pMix, pDelta, pAttack, pRelease, pLow, pHigh, pBands, pReact };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "filter.resonance";
        d.displayName = "Resonance Suppressor";
        d.description = "Tames resonances as they happen, with no latency: 32 (or 64) bands from Low to High listen to the sound "
                        "(or to Side In while it is patched), and wherever one stands out above its neighbours by more than "
                        "Selectivity allows, a bell cut pulls it back - as far as Depth says, following Attack and Release. "
                        "What has no resonances passes unchanged. Removed Out (and Delta) is what was taken away.";
        d.section = ModuleSection::Filter;
        d.sidebarOrder = 14;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("sideIn", "Side In"),
                      audioOut ("audioOut", "Audio Out"), audioOut ("removedOut", "Removed Out") };
        d.params = {
            makeRotary ("depth", "Depth", 0.0f, 100.0f, 50.0f, 0, "%"),
            makeRotary ("sharpness", "Sharpness", 0.0f, 100.0f, 50.0f, 0, "%"),
            makeRotary ("selectivity", "Selectivity", 0.0f, 100.0f, 50.0f, 0, "%"),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 0, "%"),
            makeCombo ("delta", "Delta", { "Off", "On" }, 0, 0, 1).noMod(),
            makeRotary ("attack", "Attack", 0.2f, 50.0f, 3.0f, 1, "ms", true),
            makeRotary ("release", "Release", 5.0f, 500.0f, 80.0f, 1, "ms", true),
            makeRotary ("low", "Low", 20.0f, 2000.0f, 80.0f, 1, "Hz", true),
            makeRotary ("high", "High", 1000.0f, 20000.0f, 16000.0f, 1, "Hz", true),
            makeCombo ("bands", "Bands", { "32", "64" }, 0, 1, 1).noMod(),
            makeCombo ("reactance", "Reactance", { "Off", "On" }, 0, 2, 1).noMod().hide()   // the card's button
        };
        return d;
    }

    ResonanceSuppressorModule()
    {
        for (auto& c : curve) c.store (1.0f, std::memory_order_relaxed);
    }

    //=== the Reactance curve (message thread writes, the audio thread reads) ===
    bool reactanceOn() const { return getParameterBase (pReact) >= 0.5f; }
    float curvePoint (int i) const { return curve[(size_t) juce::jlimit (0, kCurvePoints - 1, i)].load (std::memory_order_relaxed); }
    void setCurvePoint (int i, float v)
    {
        curve[(size_t) juce::jlimit (0, kCurvePoints - 1, i)].store (juce::jlimit (0.0f, kCurveMax, v), std::memory_order_relaxed);
    }
    // the multiplier at a frequency (the curve read between its points, on a log axis)
    float curveAt (float hz) const
    {
        const float x = std::log (juce::jlimit (kCurveLo, kCurveHi, hz) / kCurveLo) / std::log (kCurveHi / kCurveLo) * (float) (kCurvePoints - 1);
        const int i = juce::jlimit (0, kCurvePoints - 2, (int) x);
        const float f = x - (float) i;
        return curvePoint (i) + (curvePoint (i + 1) - curvePoint (i)) * f;
    }

    juce::String saveCustomState() const override
    {
        bool flat = true;
        juce::StringArray v;
        for (int i = 0; i < kCurvePoints; ++i)
        {
            const float c = curvePoint (i);
            flat = flat && std::abs (c - 1.0f) < 1.0e-4f;
            v.add (juce::String (c, 3));
        }
        return flat ? juce::String() : "curve:" + v.joinIntoString (",");
    }

    void loadCustomState (const juce::String& state) override
    {
        const auto v = juce::StringArray::fromTokens (state.fromFirstOccurrenceOf ("curve:", false, false), ",", "");
        for (int i = 0; i < kCurvePoints; ++i)
            setCurvePoint (i, i < v.size() ? v[i].getFloatValue() : 1.0f);
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        lastLow = lastHigh = lastSharp = lastAtk = lastRel = -1.0f;
        nb = getParameterBase (pBands) >= 0.5f ? 64 : 32;
        reset();
    }
    void reset() override
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            det[b].reset(); cut[0][b].reset(); cut[1][b].reset();
            power[b] = 1.0e-12f; reductionDb[b] = 0.0f; active[b] = false;
            shownReduction[b].store (0.0f, std::memory_order_relaxed);
        }
        ctl = 0;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float xl = in[0][0], xr = in[0][1];
        // what the detector hears: the side signal while it is patched
        const bool side = isInputConnected (1);
        const float d = side ? 0.5f * (in[1][0] + in[1][1]) : 0.5f * (xl + xr);

        if (ctl-- <= 0) { ctl = kCtl - 1; update(); }

        // the detector: every band's power, rising at Attack and falling at Release
        for (int b = 0; b < nb; ++b)
        {
            const float y = det[b].band (d) * det[b].k;    // unity gain at the band's centre
            const float p = y * y;
            power[b] += (p > power[b] ? attackK[b] : releaseK[b]) * (p - power[b]);
        }

        // the cuts: only the bands that are cutting
        float yl = xl, yr = xr;
        for (int b = 0; b < nb; ++b)
        {
            if (! active[b]) continue;
            auto& cl = cut[0][b];
            auto& cr = cut[1][b];
            yl += cutK[b] * cl.band (yl);
            yr += cutK[b] * cr.band (yr);
        }
        if (! std::isfinite (yl) || ! std::isfinite (yr)) { for (auto& c : cut) for (auto& f : c) f.reset(); yl = xl; yr = xr; }

        const float mix = param (pMix) * 0.01f;
        const float wl = xl + (yl - xl) * mix, wr = xr + (yr - xr) * mix;
        const float rl = xl - wl, rr = xr - wr;
        out[0] = param (pDelta) >= 0.5f ? StereoFrame { rl, rr } : StereoFrame { wl, wr };
        out[1] = { rl, rr };
    }

    int numBands() const { return getParameterBase (pBands) >= 0.5f ? 64 : 32; }
    float bandReduction (int b) const { return shownReduction[juce::jlimit (0, kMaxBands - 1, b)].load (std::memory_order_relaxed); }
    float bandFrequency (int b) const
    {
        const float lo = getParameterBase (pLow), hi = juce::jmax (lo * 1.5f, getParameterBase (pHigh));
        return lo * std::pow (hi / lo, ((float) b + 0.5f) / (float) numBands());
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return reactanceOn() ? 98 + 118 : 98; }

private:
    // every kCtl samples: the band layout (when it moved), the times, and each
    // band's cut from how far it stands above its neighbours
    void update()
    {
        const float lo = param (pLow), hi = juce::jmax (lo * 1.5f, param (pHigh));
        const float sharp = param (pSharp) * 0.01f;
        const int bands = param (pBands) >= 0.5f ? 64 : 32;
        if (bands != nb)
        {
            // another band count: a fresh start for the detector and the cuts
            nb = bands;
            reset();
            lastLow = -1.0f;
        }
        if (! juce::exactlyEqual (lo, lastLow) || ! juce::exactlyEqual (hi, lastHigh) || ! juce::exactlyEqual (sharp, lastSharp))
        {
            lastLow = lo; lastHigh = hi; lastSharp = sharp; layoutChanged = true;
            // a band's width: its share of the range, narrowed or widened by Sharpness
            const float bwOct = std::log2 (hi / lo) / (float) nb;
            const float qBase = 1.0f / (2.0f * std::sinh (std::log (2.0f) * 0.5f * bwOct));
            q = qBase * std::exp2 (sharp * 2.0f - 1.0f);    // half to twice the share
            const float nyq = (float) sampleRate * 0.45f;
            for (int b = 0; b < nb; ++b)
            {
                const float f = juce::jmin (nyq, lo * std::pow (hi / lo, ((float) b + 0.5f) / (float) nb));
                bandHz[b] = f;
                gOf[b] = std::tan (juce::MathConstants<float>::pi * f / (float) sampleRate);
                det[b].setCoeffs (gOf[b], 1.0f / q);
            }
        }
        // the times; a low band needs a few of its own cycles to know its level
        const float atkMs = param (pAttack), relMs = param (pRelease);
        if (! juce::exactlyEqual (atkMs, lastAtk) || ! juce::exactlyEqual (relMs, lastRel) || layoutChanged)
        {
            lastAtk = atkMs; lastRel = relMs; layoutChanged = false;
            for (int b = 0; b < nb; ++b)
            {
                const float f = juce::jmax (10.0f, bandHz[b]);
                const float atk = juce::jmax (atkMs * 0.001f, 2.0f / f), rel = juce::jmax (relMs * 0.001f, 6.0f / f);
                attackK[b] = 1.0f - std::exp (-1.0f / (atk * (float) sampleRate));
                releaseK[b] = 1.0f - std::exp (-1.0f / (rel * (float) sampleRate));
            }
        }

        // each band in dB, and the mean of its neighbours (leaving itself out)
        float db[kMaxBands];
        for (int b = 0; b < nb; ++b) db[b] = 10.0f * std::log10 (power[b] + 1.0e-12f);
        const int spread = nb / 32, nearest = kNearest * spread, farthest = kFarthest * spread;
        const float threshold = 1.0f + param (pSelect) * 0.11f;         // 1 .. 12 dB above the neighbours
        const float depth = param (pDepth) * 0.01f;
        const bool react = param (pReact) >= 0.5f;
        for (int b = 0; b < nb; ++b)
        {
            float around[2 * (kFarthest * 2 - kNearest * 2 + 1)];
            int n = 0;
            for (int k = nearest; k <= farthest; ++k)
                for (int other : { b - k, b + k })
                    if (other >= 0 && other < nb) around[n++] = db[other];
            std::sort (around, around + n);
            const int half = juce::jmax (1, n / 2);
            float sum = 0.0f;
            for (int i = 0; i < half; ++i) sum += around[i];
            const float ref = sum / (float) half;
            // silence is not a resonance: a band must also be audible at all
            const float excess = db[b] > -90.0f ? db[b] - ref - threshold : 0.0f;
            const float target = juce::jlimit (0.0f, kMaxCutDb, excess * depth * (react ? curveAt (bandHz[b]) : 1.0f));
            reductionDb[b] = target;   // the detector's own Attack / Release already shape it

            const bool cutting = target > 0.02f;
            if (cutting && ! active[b]) { cut[0][b].reset(); cut[1][b].reset(); }
            active[b] = cutting;
            if (cutting)
            {
                // a bell of gain A at the band: y = x + k (A^2 - 1) band
                const float A = std::pow (10.0f, -target / 40.0f);
                const float k = 1.0f / (q * A);
                cut[0][b].setCoeffs (gOf[b], k);
                cut[1][b].setCoeffs (gOf[b], k);
                cutK[b] = k * (A * A - 1.0f);
            }
            shownReduction[b].store (target, std::memory_order_relaxed);
        }
    }

    Svf det[kMaxBands], cut[2][kMaxBands];
    float gOf[kMaxBands] {}, power[kMaxBands] {}, reductionDb[kMaxBands] {}, cutK[kMaxBands] {};
    bool active[kMaxBands] {};
    std::atomic<float> shownReduction[kMaxBands] {};
    float q = 4.0f, attackK[kMaxBands] {}, releaseK[kMaxBands] {}, bandHz[kMaxBands] {};
    int nb = 32;                        // bands in use
    std::array<std::atomic<float>, kCurvePoints> curve;
    float lastLow = -1.0f, lastHigh = -1.0f, lastSharp = -1.0f, lastAtk = -1.0f, lastRel = -1.0f;
    bool layoutChanged = true;
    int ctl = 0;
};

namespace
{
    // the Reactance curve: drag to draw it, double-tap to make it flat (x1) again
    class ReactanceEditor final : public juce::Component
    {
    public:
        explicit ReactanceEditor (ResonanceSuppressorModule& m) : module (&m) {}

        std::function<void()> changed;

        void paint (juce::Graphics& g) override
        {
            auto* m = dynamic_cast<ResonanceSuppressorModule*> (module.get());
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (r, 3.0f);
            if (m == nullptr) return;
            const auto plot = plotArea();

            // the bands' range, and the x0 / x1 / x2 lines
            const float lo = xOf (m->bandFrequency (0)), hi = xOf (m->bandFrequency (m->numBands() - 1));
            g.setColour (juce::Colour (0xff6fd9c4).withAlpha (0.08f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (lo, plot.getY(), hi, plot.getBottom()));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float v : { 0.0f, 1.0f, 2.0f })
            {
                const float y = yOf (v);
                g.setColour (juce::Colours::white.withAlpha (v == 1.0f ? 0.3f : 0.1f));
                g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText ("x" + juce::String ((int) v), juce::Rectangle<float> (r.getX() + 2.0f, y - 6.0f, 18.0f, 12.0f),
                            juce::Justification::centredLeft);
            }
            for (float hz : { 100.0f, 1000.0f, 10000.0f })
            {
                g.setColour (juce::Colours::white.withAlpha (0.1f));
                g.drawVerticalLine ((int) xOf (hz), plot.getY(), plot.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (xOf (hz) + 2.0f, plot.getBottom() - 11.0f, 30.0f, 11.0f), juce::Justification::centredLeft);
            }

            juce::Path p;
            for (int i = 0; i < kCurvePoints; ++i)
            {
                const juce::Point<float> pt (xOf (curvePointHz (i)), yOf (m->curvePoint (i)));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (juce::Colour (0xffffb86b));
            g.strokePath (p, juce::PathStrokeType (2.0f));
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText ("Reactance: how hard each frequency is suppressed (drag to draw, double-tap: flat)",
                        r.reduced (24.0f, 2.0f).removeFromTop (12.0f), juce::Justification::centredLeft);
        }

        void mouseDown (const juce::MouseEvent& e) override { last = -1; draw (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { draw (e.position); }
        void mouseUp (const juce::MouseEvent&) override { last = -1; }
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (auto* m = dynamic_cast<ResonanceSuppressorModule*> (module.get()))
                for (int i = 0; i < kCurvePoints; ++i) m->setCurvePoint (i, 1.0f);
            repaint();
            if (changed) changed();
        }

    private:
        juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (22.0f, 14.0f).withTrimmedBottom (-10.0f); }
        float xOf (float hz) const
        {
            const auto p = plotArea();
            return p.getX() + p.getWidth() * std::log (juce::jlimit (kCurveLo, kCurveHi, hz) / kCurveLo) / std::log (kCurveHi / kCurveLo);
        }
        float yOf (float v) const { const auto p = plotArea(); return p.getBottom() - p.getHeight() * v / kCurveMax; }
        int pointAt (float x) const
        {
            const auto p = plotArea();
            return juce::jlimit (0, kCurvePoints - 1, juce::roundToInt ((x - p.getX()) / p.getWidth() * (float) (kCurvePoints - 1)));
        }

        // every point between the last one drawn and this one, on the line between them
        void draw (juce::Point<float> pos)
        {
            auto* m = dynamic_cast<ResonanceSuppressorModule*> (module.get());
            if (m == nullptr) return;
            const auto p = plotArea();
            const int i = pointAt (pos.x);
            const float v = juce::jlimit (0.0f, kCurveMax, (p.getBottom() - pos.y) / p.getHeight() * kCurveMax);
            if (last < 0) { m->setCurvePoint (i, v); }
            else
            {
                const int a = juce::jmin (last, i), b = juce::jmax (last, i);
                for (int k = a; k <= b; ++k)
                {
                    const float t = b == a ? 1.0f : (float) (k - last) / (float) (i - last);
                    m->setCurvePoint (k, lastValue + (v - lastValue) * t);
                }
            }
            last = i;
            lastValue = v;
            repaint();
            if (changed) changed();
        }

        juce::WeakReference<SynthModule> module;
        int last = -1;
        float lastValue = 1.0f;
    };

    // the bands and how far each is being cut, repainted only when that changes;
    // the Reactance button, and its curve below while it is on
    class SuppressorDisplay final : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit SuppressorDisplay (ResonanceSuppressorModule& m) : module (&m), curveEditor (m)
        {
            reactButton.setClickingTogglesState (true);
            reactButton.setToggleState (m.reactanceOn(), juce::dontSendNotification);
            reactButton.onClick = [this]
            {
                if (auto* mm = dynamic_cast<ResonanceSuppressorModule*> (module.get()))
                {
                    mm->setParameter ("reactance", reactButton.getToggleState() ? 1.0f : 0.0f);
                    shownReact = reactButton.getToggleState();
                    curveEditor.setVisible (shownReact);
                    if (paramsChangedByContent) paramsChangedByContent();   // the card grows or shrinks
                }
            };
            addAndMakeVisible (reactButton);
            addChildComponent (curveEditor);
            curveEditor.setVisible (m.reactanceOn());
            shownReact = m.reactanceOn();
            curveEditor.changed = [this] { if (paramValuesChangedByContent) paramValuesChangedByContent(); };
            startTimerHz (20);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            reactButton.setBounds (r.removeFromTop (20).removeFromRight (150).reduced (1));
            barsArea = r.removeFromTop (78);
            r.removeFromTop (6);
            curveEditor.setBounds (r.removeFromTop (112));
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = dynamic_cast<ResonanceSuppressorModule*> (module.get());
            auto r = barsArea.toFloat();
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (r, 3.0f);
            if (m == nullptr) return;
            auto plot = r.reduced (4.0f, 4.0f).withTrimmedBottom (12.0f);
            // the cut depth scale: 0 at the top, 24 dB at the bottom, lines every 6 dB
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            for (int db = 6; db < 24; db += 6)
                g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * (float) db / kMaxCutDb), plot.getX(), plot.getRight());
            const int bands = m->numBands();
            const float w = plot.getWidth() / (float) bands;
            const float gap = bands > 32 ? 0.5f : 1.0f;
            for (int b = 0; b < bands; ++b)
            {
                const float red = shown[b];
                const float h = plot.getHeight() * juce::jlimit (0.0f, 1.0f, red / kMaxCutDb);
                auto bar = juce::Rectangle<float> (plot.getX() + (float) b * w + gap, plot.getY(), w - 2.0f * gap, h);
                g.setColour (juce::Colour (0xff6fd9c4).withAlpha (0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, red / 6.0f)));
                g.fillRect (bar);
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRect (plot.getX() + (float) b * w + gap, plot.getY(), w - 2.0f * gap, 1.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            auto label = [&] (float hz)
            {
                if (hz >= 10000.0f) return juce::String (juce::roundToInt (hz / 1000.0f)) + "k";
                if (hz >= 1000.0f) return juce::String (hz / 1000.0f, 1) + "k";
                return juce::String (juce::roundToInt (hz));
            };
            const auto foot = r.reduced (4.0f, 0.0f).removeFromBottom (13.0f);
            g.drawText (label (m->bandFrequency (0)), foot, juce::Justification::centredLeft);
            g.drawText (label (m->bandFrequency (bands / 2)), foot, juce::Justification::centred);
            g.drawText (label (m->bandFrequency (bands - 1)), foot, juce::Justification::centredRight);
        }

    private:
        void timerCallback() override
        {
            auto* m = dynamic_cast<ResonanceSuppressorModule*> (module.get());
            if (m == nullptr) return;
            // switched elsewhere (Undo, a patch loaded): follow
            if (m->reactanceOn() != shownReact)
            {
                shownReact = m->reactanceOn();
                reactButton.setToggleState (shownReact, juce::dontSendNotification);
                curveEditor.setVisible (shownReact);
                if (paramsChangedByContent) paramsChangedByContent();
            }
            if (shownReact) curveEditor.repaint();
            bool changed = m->numBands() != shownBands;
            shownBands = m->numBands();
            for (int b = 0; b < shownBands; ++b)
            {
                const float v = m->bandReduction (b);
                if (std::abs (v - shown[b]) > 0.05f) { shown[b] = v; changed = true; }
            }
            if (changed) repaint (barsArea);
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton reactButton { "Reactance curve" };
        ReactanceEditor curveEditor;
        juce::Rectangle<int> barsArea;
        float shown[kMaxBands] {};
        int shownBands = 0;
        bool shownReact = false;
    };
}

std::unique_ptr<juce::Component> ResonanceSuppressorModule::createExtraContentComponent()
{
    return std::make_unique<SuppressorDisplay> (*this);
}

AQUANODE_REGISTER_MODULE (ResonanceSuppressorModule, ResonanceSuppressorModule::descriptor)
