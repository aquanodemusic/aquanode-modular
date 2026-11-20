// Spline Folder - a wavefolder whose fold you draw.
//
// The curve maps the input (after Drive and Bias, -1 .. 1 across) to the
// output: points joined by a smooth spline (cubic Hermite, Catmull-Rom
// tangents; Tension 0, or Curve Linear, joins them with straight lines -
// piecewise linear). A point can
// hold a jump - two values at one x, so the curve leaps there (a
// discontinuity: wraps, steps, saws). Where the input goes past -1 .. 1,
// Outside decides: Mirror folds it back and forth across the range (as Clip
// Preserve's Mirror does), Wrap brings it in from the other side, Hold keeps
// the end values.
//
// Drawing it: drag a point (the ends move only up and down); double-tap the
// empty plot to add a point there, double-tap a point to remove it; hold a
// point (or right-click it) for its menu - a jump on or off, remove. Shapes
// has ready ones (folds, steps, wraps, random) and Make Odd, which mirrors
// the right half onto the left (a symmetric fold: no even harmonics, no DC).
//
// Anti-aliased by first-order antiderivative anti-aliasing: the spline's
// exact integral (a quartic per segment, continuous across jumps) through
// the Outside mapping, differenced - so even the jumps alias far less than
// they would raw. A 5 Hz DC blocker after it (a bias or a lopsided curve
// makes DC), then Mix and Output.
//
// Flexible: per voice when fed per voice. Inputs: 0 = Audio In, 1 = Bias In
// (+-1 at full). Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../Shared/LookPalette.h"

using namespace aquanode;

namespace splinefold
{
    constexpr int kMaxKnots = 32;

    struct Knot { float x { 0.0f }, y { 0.0f }, yR { 0.0f }; bool jump { false }; };

    struct Curve
    {
        int n { 0 };
        std::array<Knot, kMaxKnots> k {};

        float right (int i) const { return k[(size_t) i].jump ? k[(size_t) i].yR : k[(size_t) i].y; }

        void add (float x, float y, bool jump = false, float yR = 0.0f)
        {
            if (n >= kMaxKnots) return;
            k[(size_t) n++] = { x, y, jump ? yR : y, jump };
        }

        // sorted, ends at -1 and 1, at least two points, values in range
        void tidy()
        {
            std::sort (k.begin(), k.begin() + n, [] (const Knot& a, const Knot& b) { return a.x < b.x; });
            if (n < 2) { n = 0; add (-1.0f, -1.0f); add (1.0f, 1.0f); return; }
            k[0].x = -1.0f; k[0].jump = false;
            k[(size_t) n - 1].x = 1.0f; k[(size_t) n - 1].jump = false;
            for (int i = 0; i < n; ++i)
            {
                auto& p = k[(size_t) i];
                p.y = juce::jlimit (-1.5f, 1.5f, p.y);
                p.yR = p.jump ? juce::jlimit (-1.5f, 1.5f, p.yR) : p.y;
            }
        }

        juce::String toString() const
        {
            juce::StringArray parts;
            for (int i = 0; i < n; ++i)
            {
                const auto& p = k[(size_t) i];
                parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4)
                           + (p.jump ? "," + juce::String (p.yR, 4) : juce::String()));
            }
            return "spline1;" + parts.joinIntoString (";");
        }

        bool fromString (const juce::String& s)
        {
            auto parts = juce::StringArray::fromTokens (s, ";", {});
            if (parts.size() < 3 || parts[0] != "spline1") return false;
            Curve c;
            for (int i = 1; i < parts.size(); ++i)
            {
                auto f = juce::StringArray::fromTokens (parts[i], ",", {});
                if (f.size() < 2) continue;
                c.add (f[0].getFloatValue(), f[1].getFloatValue(), f.size() > 2, f.size() > 2 ? f[2].getFloatValue() : 0.0f);
            }
            if (c.n < 2) return false;
            c.tidy();
            *this = c;
            return true;
        }
    };

    // the curve as Hermite segments, with its integral, for one tension
    struct Shape
    {
        struct Seg { double x0, h, y0, y1, m0, m1, cum; };
        int segs { 0 };
        std::array<Seg, kMaxKnots> s {};
        double total { 0.0 }, yStart { 0.0 }, yEnd { 0.0 };

        void build (const Curve& c, double tension)
        {
            segs = juce::jmax (0, c.n - 1);
            std::array<double, kMaxKnots> d {};   // each segment's secant slope
            for (int j = 0; j < segs; ++j)
            {
                const double h = juce::jmax (1.0e-6, (double) (c.k[(size_t) j + 1].x - c.k[(size_t) j].x));
                d[(size_t) j] = ((double) c.k[(size_t) j + 1].y - (double) c.right (j)) / h;
            }
            double cum = 0.0;
            for (int j = 0; j < segs; ++j)
            {
                auto& g = s[(size_t) j];
                g.x0 = c.k[(size_t) j].x;
                g.h = juce::jmax (1.0e-6, (double) (c.k[(size_t) j + 1].x - c.k[(size_t) j].x));
                g.y0 = c.right (j);
                g.y1 = c.k[(size_t) j + 1].y;
                // Catmull-Rom tangents (one-sided at the ends and at jumps), eased toward the secant by Tension
                const bool smoothStart = j > 0 && ! c.k[(size_t) j].jump;
                const bool smoothEnd = j + 1 < segs && ! c.k[(size_t) j + 1].jump;
                auto blend = [&] (double a, double ha, double b, double hb) { return (a * hb + b * ha) / (ha + hb); };
                const double cr0 = smoothStart ? blend (d[(size_t) j - 1], s[(size_t) j - 1].h, d[(size_t) j], g.h) : d[(size_t) j];
                const double hNext = smoothEnd ? juce::jmax (1.0e-6, (double) (c.k[(size_t) j + 2].x - c.k[(size_t) j + 1].x)) : 1.0;
                const double cr1 = smoothEnd ? blend (d[(size_t) j], g.h, d[(size_t) j + 1], hNext) : d[(size_t) j];
                g.m0 = d[(size_t) j] + tension * (cr0 - d[(size_t) j]);
                g.m1 = d[(size_t) j] + tension * (cr1 - d[(size_t) j]);
                g.cum = cum;
                cum += segIntegral (g, 1.0);
            }
            total = cum;
            yStart = segs > 0 ? s[0].y0 : 0.0;
            yEnd = segs > 0 ? s[(size_t) segs - 1].y1 : 0.0;
        }

        static double segIntegral (const Seg& g, double t)   // from the segment's start to t (0..1), in x units
        {
            const double t2 = t * t, t3 = t2 * t, t4 = t3 * t;
            return g.h * (g.y0 * (0.5 * t4 - t3 + t)
                        + g.h * g.m0 * (0.25 * t4 - (2.0 / 3.0) * t3 + 0.5 * t2)
                        + g.y1 * (-0.5 * t4 + t3)
                        + g.h * g.m1 * (0.25 * t4 - t3 / 3.0));
        }

        int find (double x) const
        {
            int j = 0;
            while (j + 1 < segs && x >= s[(size_t) j + 1].x0) ++j;
            return j;
        }

        // f on [-1, 1]
        double f (double x) const
        {
            if (segs == 0) return x;
            x = juce::jlimit (-1.0, 1.0, x);
            const auto& g = s[(size_t) find (x)];
            const double t = juce::jlimit (0.0, 1.0, (x - g.x0) / g.h), t2 = t * t, t3 = t2 * t;
            return (2 * t3 - 3 * t2 + 1) * g.y0 + (t3 - 2 * t2 + t) * g.h * g.m0 + (-2 * t3 + 3 * t2) * g.y1 + (t3 - t2) * g.h * g.m1;
        }

        // its integral from -1 to x (x in [-1, 1])
        double F (double x) const
        {
            if (segs == 0) return 0.5 * (x * x - 1.0);
            x = juce::jlimit (-1.0, 1.0, x);
            const auto& g = s[(size_t) find (x)];
            return g.cum + segIntegral (g, juce::jlimit (0.0, 1.0, (x - g.x0) / g.h));
        }

        // the whole transfer for any input: f after the Outside mapping (0 Mirror,
        // 1 Wrap, 2 Hold), and its antiderivative G
        double T (double u, int outside) const
        {
            if (outside == 1) return f (u + 1.0 - 2.0 * std::floor ((u + 1.0) / 2.0) - 1.0);
            if (outside == 2) return f (u);
            const double m = u - 1.0 - 4.0 * std::floor ((u - 1.0) / 4.0);
            return f (std::abs (m - 2.0) - 1.0);
        }
        double G (double u, int outside) const
        {
            if (outside == 1)
            {
                const double q = std::floor ((u + 1.0) / 2.0);
                return q * total + F (u + 1.0 - 2.0 * q - 1.0);
            }
            if (outside == 2)
            {
                if (u < -1.0) return yStart * (u + 1.0);
                if (u > 1.0) return total + yEnd * (u - 1.0);
                return F (u);
            }
            const double q = std::floor ((u - 1.0) / 4.0);
            const double m = u - 1.0 - 4.0 * q;
            const double base = q * 2.0 * total + total;   // G at u = 1 + 4q
            return m <= 2.0 ? base + total - F (1.0 - m)
                            : base + total + F (m - 3.0);
        }
    };

    // the ready shapes (Shapes menu)
    inline Curve shapeNamed (int which, juce::Random& rng)
    {
        Curve c;
        switch (which)
        {
            case 0: c.add (-1, -1); c.add (1, 1); break;                                                 // Straight
            case 1: c.add (-1, 0); c.add (-0.5f, -1); c.add (0, 0); c.add (0.5f, 1); c.add (1, 0); break; // Sine Fold
            case 2: for (int i = 0; i <= 8; ++i)                                                          // Double Fold
                        c.add (-1.0f + 0.25f * (float) i, (i % 4 == 1) ? -1.0f : (i % 4 == 3 ? 1.0f : 0.0f));
                    break;
            case 3: c.add (-1, -1); c.add (-0.55f, -0.9f); c.add (0, 0); c.add (0.55f, 0.9f); c.add (1, 1); break;   // Soft Clip
            case 4: c.add (-1, 1); c.add (0, 0); c.add (1, 1); break;                                    // Rectify
            case 5: c.add (-1, -1); c.add (-0.5f, -1, true, -1.0f / 3.0f);                               // Steps
                    c.add (0, -1.0f / 3.0f, true, 1.0f / 3.0f); c.add (0.5f, 1.0f / 3.0f, true, 1); c.add (1, 1); break;
            case 6: c.add (-1, -1); c.add (0, 1, true, -1); c.add (1, 1); break;                        // Wrap Saw
            case 7: c.add (-1, 0); c.add (-0.6f, -1); c.add (-0.2f, 0.8f, true, -0.8f);                  // Broken Fold
                    c.add (0.2f, 0.8f, true, -0.8f); c.add (0.6f, 1); c.add (1, 0); break;
            case 8: case 9:                                                                              // Random (with jumps)
            {
                const int n = 5 + rng.nextInt (5);
                for (int i = 0; i < n; ++i)
                {
                    const float x = i == 0 ? -1.0f : i == n - 1 ? 1.0f : -1.0f + 2.0f * ((float) i + rng.nextFloat() * 0.6f - 0.3f) / (float) (n - 1);
                    const bool jump = which == 9 && i > 0 && i < n - 1 && rng.nextFloat() < 0.35f;
                    c.add (x, rng.nextFloat() * 2.0f - 1.0f, jump, rng.nextFloat() * 2.0f - 1.0f);
                }
                break;
            }
            default: c.add (-1, -1); c.add (1, 1); break;
        }
        c.tidy();
        return c;
    }

    // the right half mirrored onto the left: f(-x) = -f(x)
    inline Curve madeOdd (const Curve& in)
    {
        Curve c;
        std::vector<Knot> right;
        for (int i = 0; i < in.n; ++i)
            if (in.k[(size_t) i].x > 0.0005f) right.push_back (in.k[(size_t) i]);
        for (auto it = right.rbegin(); it != right.rend(); ++it)   // the left half, mirrored (a jump's sides swap)
            c.add (-it->x, it->jump ? -it->yR : -it->y, it->jump, -it->y);
        const float yz = 0.0f;
        c.add (0.0f, yz);
        for (auto& p : right) c.add (p.x, p.y, p.jump, p.yR);
        c.tidy();
        return c;
    }
}

class SplineFolderModule : public SynthModule
{
public:
    // (patches save knobs by name, so Curve sits beside Tension)
    enum P { pDrive = 0, pBias, pTension, pCurve, pOutside, pMix, pOutput, pBiasDepth };

    // Curve Linear: straight lines between the points (piecewise linear), whatever Tension says
    float effectiveTension (float tensionKnob, float curveKnob) const { return curveKnob > 0.5f ? 0.0f : tensionKnob * 0.01f; }

    SplineFolderModule()
    {
        juce::Random r;
        edit = splinefold::shapeNamed (1, r);
        publish();
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.splinefold";
        d.displayName = "Spline Folder";
        d.description =
            "A wavefolder you draw: the curve maps the input (after Drive and Bias) to the output, points joined by "
            "a smooth spline (Tension 0 or Curve Linear: straight lines), and a point can hold a jump - a discontinuity, for wraps, "
            "steps and saws. Drag points; double-tap to add or remove one; hold a point for a jump. Outside folds "
            "what goes past the edges back (Mirror), wraps it (Wrap) or holds the ends. Shapes has ready folds, "
            "steps and random curves; anti-aliased, DC-blocked. Bias In moves the fold (an LFO there sweeps it).";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 13;
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("biasIn", "Bias In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("drive",   "Drive",   -12.0f, 36.0f, 0.0f, 0, "dB"),
            makeRotary ("bias",    "Bias",    -1.0f, 1.0f, 0.0f, 0),
            makeRotary ("tension", "Tension", 0.0f, 100.0f, 100.0f, 0, "%"),
            makeCombo  ("curve",   "Curve",   { "Spline", "Linear" }, 0, 0, 1).noMod(),
            makeCombo  ("outside", "Outside", { "Mirror", "Wrap", "Hold" }, 0, 1, 1).noMod(),
            makeRotary ("mix",     "Mix",     0.0f, 100.0f, 100.0f, 1, "%"),
            makeRotary ("output",  "Output",  -24.0f, 12.0f, 0.0f, 1, "dB"),
            makeRotary ("biasDepth", "Bias In", 0.0f, 100.0f, 100.0f, 1, "%")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        dcR = 1.0 - juce::MathConstants<double>::twoPi * 5.0 / sr;
        reset();
    }

    void reset() override
    {
        for (int l = 0; l < kLanes; ++l) clearLane (l);
    }
    void voiceReset (int v) override { clearLane (v); }

    void blockStart() override
    {
        const float tension = effectiveTension (param (pTension), param (pCurve));
        if (dirty.load (std::memory_order_acquire) || tension != liveTension)
        {
            const juce::SpinLock::ScopedTryLockType sl (lock);
            if (sl.isLocked())
            {
                live = pending;
                dirty.store (false, std::memory_order_relaxed);
                liveTension = tension;
                shape.build (live, liveTension);
            }
        }
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override { renderLane (kMaxVoices, in, out); }
    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override { renderLane (v, in, out); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 400; }
    int extraContentHeight() const override { return 236; }

    juce::String saveCustomState() const override { return edit.toString(); }
    void loadCustomState (const juce::String& state) override
    {
        if (edit.fromString (state))
            publish();
    }

    //=== the editor (message thread) =========================================
    splinefold::Curve edit;
    void publish()
    {
        edit.tidy();
        const juce::SpinLock::ScopedLockType sl (lock);
        pending = edit;
        dirty.store (true, std::memory_order_release);
    }
    float knob (int i) const { return getParameterBase (i); }

private:
    static constexpr int kLanes = kMaxVoices + 1;

    void clearLane (int l)
    {
        for (int c = 0; c < 2; ++c)
        {
            lastU[(size_t) l][(size_t) c] = 0.0;
            first[(size_t) l][(size_t) c] = true;
            dcX[(size_t) l][(size_t) c] = dcY[(size_t) l][(size_t) c] = 0.0;
        }
    }

    void renderLane (int l, const StereoFrame* in, StereoFrame* out)
    {
        const double drive = juce::Decibels::decibelsToGain ((double) param (pDrive));
        const double bias = param (pBias) + (isInputConnected (1) ? in[1][0] * param (pBiasDepth) * 0.01f : 0.0f);
        const int outside = juce::jlimit (0, 2, (int) std::lround (param (pOutside)));
        const float mix = param (pMix) * 0.01f;
        const float outGain = juce::Decibels::decibelsToGain (param (pOutput));

        for (int c = 0; c < 2; ++c)
        {
            const float x = in[0][(size_t) c];
            double u = (double) x * drive + bias;
            if (! std::isfinite (u)) u = 0.0;
            u = juce::jlimit (-1000.0, 1000.0, u);

            double& prev = lastU[(size_t) l][(size_t) c];
            double y;
            if (first[(size_t) l][(size_t) c]) { y = shape.T (u, outside); first[(size_t) l][(size_t) c] = false; }
            else
            {
                const double du = u - prev;
                y = std::abs (du) > 1.0e-5 ? (shape.G (u, outside) - shape.G (prev, outside)) / du
                                           : shape.T (0.5 * (u + prev), outside);
            }
            prev = u;

            // the DC blocker
            double& x1 = dcX[(size_t) l][(size_t) c];
            double& y1 = dcY[(size_t) l][(size_t) c];
            const double blocked = y - x1 + dcR * y1;
            x1 = y;
            y1 = std::isfinite (blocked) ? blocked : 0.0;

            const float wet = (float) juce::jlimit (-4.0, 4.0, y1);
            out[0][(size_t) c] = (x + mix * (wet - x)) * outGain;
        }
    }

    juce::SpinLock lock;
    splinefold::Curve pending, live;
    std::atomic<bool> dirty { false };
    float liveTension { -1.0f };
    splinefold::Shape shape;
    double dcR { 0.9993 };
    std::array<std::array<double, 2>, kLanes> lastU {}, dcX {}, dcY {};
    std::array<std::array<bool, 2>, kLanes> first {};
};

//==============================================================================
namespace
{
    class SplineEditor final : public juce::Component, private juce::Timer
    {
    public:
        explicit SplineEditor (SplineFolderModule& m) : module (&m)
        {
            shapesButton.setButtonText ("Shapes");
            shapesButton.onClick = [this] { showShapes(); };
            addAndMakeVisible (shapesButton);
            startTimerHz (15);
            rebuild();
        }

        void resized() override
        {
            shapesButton.setBounds (6, 5, 64, 20);
        }

        void paint (juce::Graphics& g) override
        {
            // (6.7.10: the colours follow the look - Glass, Skeuomorphic or the default)
            const auto look = aquanode::LookPalette::current (juce::Colour (0xffffb04f), aquanode::ThemeColours::Effect);
            look.fillBackground (g, getLocalBounds().toFloat());
            const auto p = plot();
            // the grid: the centre lines, full scale, the straight line faint
            g.setColour (look.grid.withAlpha (0.07f));
            for (float v : { -0.5f, 0.5f })
            {
                g.fillRect (xOf (v), p.getY(), 1.0f, p.getHeight());
                g.fillRect (p.getX(), yOf (v), p.getWidth(), 1.0f);
            }
            g.setColour (look.grid.withAlpha (0.16f));
            g.fillRect (xOf (0.0f), p.getY(), 1.0f, p.getHeight());
            g.fillRect (p.getX(), yOf (0.0f), p.getWidth(), 1.0f);
            g.setColour (look.grid.withAlpha (0.12f));
            g.drawRect (juce::Rectangle<float> (xOf (-1.0f), yOf (1.0f), xOf (1.0f) - xOf (-1.0f), yOf (-1.0f) - yOf (1.0f)), 1.0f);
            const float dashes[] = { 3.0f, 4.0f };
            g.drawDashedLine (juce::Line<float> (xOf (-1.0f), yOf (-1.0f), xOf (1.0f), yOf (1.0f)), dashes, 2, 1.0f);

            // the curve, broken at its jumps (a dotted line shows each leap)
            const auto& c = module->edit;
            const auto accent = look.accent;
            juce::Path path;
            for (int j = 0; j + 1 < c.n; ++j)
            {
                const float x0 = c.k[(size_t) j].x, x1 = c.k[(size_t) j + 1].x;
                const int steps = juce::jmax (2, (int) ((xOf (x1) - xOf (x0)) / 2.0f));
                for (int s = 0; s <= steps; ++s)
                {
                    const double x = x0 + (x1 - x0) * (double) s / steps;
                    const double xs = s == 0 ? x + 1.0e-7 : (s == steps ? x - 1.0e-7 : x);
                    const float px = xOf ((float) x), py = yOf ((float) shape.f (xs));
                    if (s == 0 && (j == 0 || c.k[(size_t) j].jump)) path.startNewSubPath (px, py);
                    else path.lineTo (px, py);
                }
                if (c.k[(size_t) j + 1].jump)
                {
                    g.setColour (accent.withAlpha (0.45f));
                    const float jd[] = { 2.0f, 3.0f };
                    g.drawDashedLine (juce::Line<float> (xOf (x1), yOf (c.k[(size_t) j + 1].y), xOf (x1), yOf (c.k[(size_t) j + 1].yR)), jd, 2, 1.0f);
                }
            }
            look.strokeGlowing (g, path, 2.0f);

            // the points: filled; a jump's right-hand value a ring
            for (int i = 0; i < c.n; ++i)
            {
                const auto& k = c.k[(size_t) i];
                const bool sel = i == dragKnot;
                look.fillHandle (g, { xOf (k.x), yOf (k.y) }, 10.0f, sel);
                g.setColour (sel ? juce::Colours::white : accent);
                if (k.jump)
                    g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ xOf (k.x), yOf (k.yR) }), 2.0f);
            }

            g.setColour (juce::Colours::white.withAlpha (0.38f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText ("drag  -  double-tap: add / remove  -  hold: jump", getLocalBounds().toFloat().removeFromBottom (13.0f).reduced (6.0f, 0.0f),
                        juce::Justification::centredRight);
            g.drawText ("in", juce::Rectangle<float> (xOf (1.0f) - 16.0f, yOf (0.0f) + 1.0f, 16.0f, 10.0f), juce::Justification::centredRight);
            g.drawText ("out", juce::Rectangle<float> (xOf (0.0f) + 3.0f, yOf (1.5f), 24.0f, 10.0f), juce::Justification::centredLeft);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto [knot, rightSide] = handleAt (e.position);
            dragKnot = knot;
            dragRight = rightSide;
            moved = false;
            downTime = juce::Time::getMillisecondCounter();
            if (knot >= 0 && e.mods.isPopupMenu()) { showKnotMenu (knot); dragKnot = -1; }
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragKnot < 0) return;
            if (e.getDistanceFromDragStart() > 3) moved = true;
            if (! moved) return;
            auto& c = module->edit;
            auto& k = c.k[(size_t) dragKnot];
            const float y = juce::jlimit (-1.5f, 1.5f, valueOfY (e.position.y));
            if (dragRight && k.jump) k.yR = y; else { k.y = y; if (! k.jump) k.yR = y; }
            if (dragKnot > 0 && dragKnot < c.n - 1)
                k.x = juce::jlimit (c.k[(size_t) dragKnot - 1].x + 0.01f, c.k[(size_t) dragKnot + 1].x - 0.01f, valueOfX (e.position.x));
            module->publish();
            rebuild();
        }

        void mouseUp (const juce::MouseEvent&) override { dragKnot = -1; repaint(); }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto& c = module->edit;
            const auto [knot, rightSide] = handleAt (e.position);
            juce::ignoreUnused (rightSide);
            if (knot > 0 && knot < c.n - 1)
            {
                for (int i = knot; i + 1 < c.n; ++i) c.k[(size_t) i] = c.k[(size_t) i + 1];
                --c.n;
            }
            else if (knot < 0 && plot().contains (e.position) && c.n < splinefold::kMaxKnots)
            {
                const float x = juce::jlimit (-0.995f, 0.995f, valueOfX (e.position.x));
                for (int i = 0; i < c.n; ++i)
                    if (std::abs (c.k[(size_t) i].x - x) < 0.01f) return;
                c.add (x, juce::jlimit (-1.5f, 1.5f, valueOfY (e.position.y)));
            }
            dragKnot = -1;
            module->publish();
            rebuild();
        }

    private:
        juce::Rectangle<float> plot() const
        {
            return getLocalBounds().toFloat().withTrimmedTop (28.0f).withTrimmedBottom (14.0f).reduced (14.0f, 2.0f);
        }
        // x: -1 .. 1 across; y: -1.5 .. 1.5 (full scale inside)
        float xOf (float v) const { const auto p = plot(); return p.getX() + (v + 1.0f) * 0.5f * p.getWidth(); }
        float yOf (float v) const { const auto p = plot(); return p.getCentreY() - v / 1.5f * p.getHeight() * 0.5f; }
        float valueOfX (float px) const { const auto p = plot(); return (px - p.getX()) / p.getWidth() * 2.0f - 1.0f; }
        float valueOfY (float py) const { const auto p = plot(); return (p.getCentreY() - py) / (p.getHeight() * 0.5f) * 1.5f; }

        std::pair<int, bool> handleAt (juce::Point<float> pos) const
        {
            const auto& c = module->edit;
            int best = -1; bool right = false; float bestD = 16.0f;
            for (int i = 0; i < c.n; ++i)
            {
                const auto& k = c.k[(size_t) i];
                const float d0 = pos.getDistanceFrom ({ xOf (k.x), yOf (k.y) });
                if (d0 < bestD) { bestD = d0; best = i; right = false; }
                if (k.jump)
                {
                    const float d1 = pos.getDistanceFrom ({ xOf (k.x), yOf (k.yR) });
                    if (d1 < bestD) { bestD = d1; best = i; right = true; }
                }
            }
            return { best, right };
        }

        void timerCallback() override
        {
            // holding a point still opens its menu
            if (dragKnot >= 0 && ! moved && juce::Time::getMillisecondCounter() - downTime > 500
                && juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            {
                const int k = dragKnot;
                dragKnot = -1;
                showKnotMenu (k);
            }
            const float t = module->effectiveTension (module->knob (SplineFolderModule::pTension), module->knob (SplineFolderModule::pCurve));
            if (t != shownTension) rebuild();
        }

        void showKnotMenu (int knot)
        {
            auto& c = module->edit;
            if (knot < 0 || knot >= c.n) return;
            const bool end = knot == 0 || knot == c.n - 1;
            juce::PopupMenu m;
            m.addItem (1, c.k[(size_t) knot].jump ? "Join the jump (one value)" : "Jump here (two values)", ! end);
            m.addItem (2, "Remove point", ! end);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                             [safe = juce::Component::SafePointer<SplineEditor> (this), knot] (int r)
            {
                if (safe == nullptr || r == 0) return;
                auto& cc = safe->module->edit;
                if (knot >= cc.n) return;
                if (r == 1)
                {
                    auto& k = cc.k[(size_t) knot];
                    k.jump = ! k.jump;
                    k.yR = k.jump ? juce::jlimit (-1.5f, 1.5f, k.y >= 0.0f ? k.y - 1.0f : k.y + 1.0f) : k.y;
                }
                else
                {
                    for (int i = knot; i + 1 < cc.n; ++i) cc.k[(size_t) i] = cc.k[(size_t) i + 1];
                    --cc.n;
                }
                safe->module->publish();
                safe->rebuild();
            });
        }

        void showShapes()
        {
            juce::PopupMenu m;
            const char* names[] = { "Straight", "Sine Fold", "Double Fold", "Soft Clip", "Rectify",
                                    "Steps (jumps)", "Wrap Saw (a jump)", "Broken Fold (jumps)", "Random", "Random with Jumps" };
            for (int i = 0; i < 10; ++i) m.addItem (i + 1, names[i]);
            m.addSeparator();
            m.addItem (100, "Make Odd (mirror the right half)");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapesButton),
                             [safe = juce::Component::SafePointer<SplineEditor> (this)] (int r)
            {
                if (safe == nullptr || r == 0) return;
                auto& c = safe->module->edit;
                if (r == 100) c = splinefold::madeOdd (c);
                else c = splinefold::shapeNamed (r - 1, safe->rng);
                safe->module->publish();
                safe->rebuild();
            });
        }

        void rebuild()
        {
            shownTension = module->effectiveTension (module->knob (SplineFolderModule::pTension), module->knob (SplineFolderModule::pCurve));
            shape.build (module->edit, shownTension);
            repaint();
        }

        SplineFolderModule* module;
        juce::TextButton shapesButton;
        splinefold::Shape shape;
        juce::Random rng;
        float shownTension { -1.0f };
        int dragKnot { -1 };
        bool dragRight { false }, moved { false };
        juce::uint32 downTime { 0 };
    };
}

std::unique_ptr<juce::Component> SplineFolderModule::createExtraContentComponent()
{
    return std::make_unique<SplineEditor> (*this);
}

AQUANODE_REGISTER_MODULE (SplineFolderModule, SplineFolderModule::descriptor)

// (for the test harness: the antiderivative against the transfer, numerically,
// over every ready shape, tension and Outside mode - the largest mismatch)
double splineFolderSelfTest()
{
    juce::Random rng (7);
    double worst = 0.0;
    for (int which = 0; which < 10; ++which)
        for (double tension : { 0.0, 0.5, 1.0 })
        {
            const auto c = splinefold::shapeNamed (which, rng);
            splinefold::Shape s;
            s.build (c, tension);
            for (int outside = 0; outside < 3; ++outside)
                for (int i = 0; i < 4000; ++i)
                {
                    const double u = -7.0 + 14.0 * (i + 0.37) / 4000.0, h = 1.0e-5;
                    // skip the jumps (and the folds' corners, where T itself jumps)
                    const double a = s.T (u - h, outside), b = s.T (u + h, outside);
                    if (std::abs (a - b) > 1.0e-3) continue;
                    const double d = (s.G (u + h, outside) - s.G (u - h, outside)) / (2.0 * h);
                    worst = juce::jmax (worst, std::abs (d - s.T (u, outside)));
                }
        }
    return worst;
}
