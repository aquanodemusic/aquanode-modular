//==============================================================================
// Automation Lanes (6.7.11) - four lanes of automation, four outputs.
//
// Every lane runs over Length (1/2 bar .. 8 bars, in time with the bars) in
// steps of its own (Step: 1/32 bar .. 2 bars), and each step's shape says
// what it does with its height: Ramp (rises to it), Slanted (glides from one
// height to the next), Stairs (holds it), Sine (a smooth swell up to it and
// back), Random (holds a random one), On/Off (a gate), Spline (a smooth
// curve through the heights), Draw (a free line drawn on the card). Since
// 6.7.12 every step has a shape of its own (tap a step in the strip under the
// lane); the lane's Shape sets them all at once and shows Mixed when they
// differ. Roll (on each lane) throws a new pattern; Throw All rolls every
// lane - shapes, steps and patterns. Draw on a lane to change it: drag across
// the steps to set their heights, or paint the line in Draw steps.
//
// The outputs drive knobs absolutely, as Curve CV does: 0..1 is the knob's
// whole range, a cable's depth blends the knob's own setting (0 %) with the
// lane (100 %, the default). Smooth rounds every change off a little (no
// clicks on gates). Restart In starts the lanes again from their beginning.
//
// From the sidebar it can come with four effects (Stutter, Reverser, Tape
// Stop, SR Reduce, Bitcrush, Gater, Delay or Pitch Shift, chosen at random),
// each lane already on its effect's amount - and with a patch of just one
// sound source and an Audio Out, the four wired in parallel to it.
// Global. Input: Restart In. Outputs: Lane 1 - 4.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/MidiTake.h"
#include "../../Shared/LookPalette.h"
#include "../../../Engine/PluginProcessor.h"

using namespace aquanode;

namespace lanes
{
    constexpr int kLanes = 4;
    constexpr int kMaxSteps = 256;
    constexpr int kDrawRes = 512;
    enum Shape { sRamp = 0, sSlanted, sStairs, sSine, sRandom, sOnOff, sSpline, sDraw, kNumShapes, kMixed = kNumShapes };
    const juce::StringArray kShapeNames { "Ramp", "Slanted", "Stairs", "Sine", "Random", "On/Off", "Spline", "Draw" };
    const juce::StringArray kComboNames { "Ramp", "Slanted", "Stairs", "Sine", "Random", "On/Off", "Spline", "Draw", "Mixed" };
    const juce::StringArray kLengthNames { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" };
    constexpr double kLengthBars[5] = { 0.5, 1.0, 2.0, 4.0, 8.0 };
    const juce::StringArray kStepNames { "1/32 bar", "1/16 bar", "1/8 bar", "1/4 bar", "1/2 bar", "1 bar", "2 bars" };
    constexpr double kStepBars[7] = { 1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5, 1.0, 2.0 };
    const juce::Colour kColours[kLanes] = { juce::Colour (0xff4fd8c8), juce::Colour (0xffffb04f), juce::Colour (0xffff7fb8), juce::Colour (0xffa8e05a) };

    struct Lane
    {
        std::array<float, kMaxSteps> heights {};
        std::array<float, kDrawRes> drawn {};
        std::array<std::uint8_t, kMaxSteps> shapes {};   // (6.7.12) each step's own shape
    };

    // a lane's value at x (0..1 through Length) for its number of steps, each
    // step with its own shape
    inline float valueAt (const Lane& l, int steps, double x)
    {
        x = x - std::floor (x);
        steps = juce::jlimit (1, kMaxSteps, steps);
        const double sp = x * steps;
        const int i = juce::jlimit (0, steps - 1, (int) sp);
        const float frac = (float) (sp - i);
        const int shape = juce::jmin ((int) l.shapes[(size_t) i], kNumShapes - 1);
        if (shape == sDraw)
        {
            const double p = x * (kDrawRes - 1);
            const int k = juce::jlimit (0, kDrawRes - 2, (int) p);
            const float t = (float) (p - k);
            return l.drawn[(size_t) k] * (1.0f - t) + l.drawn[(size_t) k + 1] * t;
        }
        const float h = l.heights[(size_t) i];
        const float hn = l.heights[(size_t) ((i + 1) % steps)];
        switch (shape)
        {
            case sRamp:    return h * frac;
            case sSlanted: return h + (hn - h) * frac;
            case sSine:    return h * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * frac));
            case sOnOff:   return h >= 0.5f ? 1.0f : 0.0f;
            case sSpline:
            {
                const float p0 = l.heights[(size_t) ((i - 1 + steps) % steps)], p3 = l.heights[(size_t) ((i + 2) % steps)];
                const float t = frac, t2 = t * t, t3 = t2 * t;
                const float v = 0.5f * (2.0f * h + (-p0 + hn) * t + (2.0f * p0 - 5.0f * h + 4.0f * hn - p3) * t2 + (-p0 + 3.0f * h - 3.0f * hn + p3) * t3);
                return juce::jlimit (0.0f, 1.0f, v);
            }
            case sStairs: case sRandom: default: return h;
        }
    }

    // a new pattern of a shape (message thread); Mixed: a few shapes taking turns
    inline void roll (Lane& l, int shape, int steps, juce::Random& rng)
    {
        steps = juce::jlimit (1, kMaxSteps, steps);
        if (shape >= kNumShapes)
        {
            // two or three shapes in a little repeating figure, heights to suit each
            static const int pool[] = { sRamp, sSlanted, sStairs, sSine, sRandom, sOnOff, sSpline };
            int palette[3] { pool[rng.nextInt (7)], pool[rng.nextInt (7)], pool[rng.nextInt (7)] };
            const int used = rng.nextBool() ? 2 : 3;
            const int period = juce::jmin (steps, rng.nextBool() ? 4 : 8);
            std::vector<int> figure ((size_t) period);
            for (int i = 0; i < period; ++i) figure[(size_t) i] = palette[i == 0 ? 0 : rng.nextInt (used)];
            for (int i = 0; i < kMaxSteps; ++i)
            {
                int s = figure[(size_t) (i % period)];
                if (i >= period && rng.nextFloat() < 0.08f) s = palette[rng.nextInt (used)];
                l.shapes[(size_t) i] = (std::uint8_t) s;
                l.heights[(size_t) i] = s == sOnOff ? (rng.nextFloat() < 0.6f ? 1.0f : 0.0f) : 0.3f + 0.7f * rng.nextFloat();
            }
            return;
        }
        std::fill (l.shapes.begin(), l.shapes.end(), (std::uint8_t) shape);
        auto tile = [&] (int period, std::function<float (int)> make)
        {
            std::vector<float> base ((size_t) period);
            for (int i = 0; i < period; ++i) base[(size_t) i] = make (i);
            for (int i = 0; i < kMaxSteps; ++i)
            {
                float v = base[(size_t) (i % period)];
                if (i >= period && rng.nextFloat() < 0.12f) v = make (i % period);   // (a little variation on the repeats)
                l.heights[(size_t) i] = juce::jlimit (0.0f, 1.0f, v);
            }
        };
        const int period = juce::jmin (steps, rng.nextBool() ? 4 : 8);
        switch (shape)
        {
            case sRamp:    tile (period, [&] (int) { return rng.nextFloat() < 0.15f ? 0.0f : 0.45f + 0.55f * rng.nextFloat(); }); break;
            case sSlanted: { float v = rng.nextFloat(); tile (juce::jmin (steps, 8), [&] (int) { v = juce::jlimit (0.0f, 1.0f, v + (rng.nextFloat() - 0.5f) * 0.7f); return v; }); break; }
            case sStairs:
            {
                const int run = 3 + rng.nextInt (6);
                const bool up = rng.nextBool();
                for (int i = 0; i < kMaxSteps; ++i) { const float s = (float) (i % run) / (float) juce::jmax (1, run - 1); l.heights[(size_t) i] = up ? s : 1.0f - s; }
                break;
            }
            case sSine:    tile (period, [&] (int) { return 0.4f + 0.6f * rng.nextFloat(); }); break;
            case sOnOff:
            {
                const float density = 0.25f + 0.45f * rng.nextFloat();
                tile (juce::jmin (steps, rng.nextBool() ? 8 : 16), [&] (int i) { return (i == 0 || rng.nextFloat() < density) ? 1.0f : 0.0f; });
                break;
            }
            case sSpline:  { float v = 0.5f; for (int i = 0; i < kMaxSteps; ++i) { v = juce::jlimit (0.05f, 0.95f, v * 0.5f + rng.nextFloat() * 0.5f); l.heights[(size_t) i] = v; } break; }
            case sDraw:
            {
                const float a = 0.25f + 0.2f * rng.nextFloat(), f1 = (float) (1 + rng.nextInt (3)), f2 = (float) (2 + rng.nextInt (6));
                const float ph1 = rng.nextFloat() * 6.28f, ph2 = rng.nextFloat() * 6.28f;
                for (int i = 0; i < kDrawRes; ++i)
                {
                    const float x = (float) i / (float) (kDrawRes - 1) * juce::MathConstants<float>::twoPi;
                    l.drawn[(size_t) i] = juce::jlimit (0.0f, 1.0f, 0.5f + a * std::sin (f1 * x + ph1) + 0.18f * std::sin (f2 * x + ph2));
                }
                break;
            }
            case sRandom: default: for (int i = 0; i < kMaxSteps; ++i) l.heights[(size_t) i] = rng.nextFloat(); break;
        }
    }
}

class AutomationLanesModule final : public SynthModule
{
public:
    enum P { pLength = 0, pSmooth, pThrow, pShape1, pStep1 };   // then shape / step for lanes 2 - 4
    static int shapeParam (int lane) { return pShape1 + lane * 2; }
    static int stepParam (int lane)  { return pStep1 + lane * 2; }

    AutomationLanesModule()
    {
        juce::Random rng (12345);
        static const int shapes[lanes::kLanes] = { lanes::sRamp, lanes::sOnOff, lanes::sStairs, lanes::sSine };
        for (int k = 0; k < lanes::kLanes; ++k) { lanes::roll (data[(size_t) k], shapes[k], 8, rng); lastCombo[k] = shapes[k]; }
    }

    bool drivesKnobsAbsolutely() const override { return true; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& v : out) v = 0.0f; origin = 0.0; lastGate = 0.0f; }

    void blockStart() override
    {
        clock.blockStart (tempoBpm, sampleRate, tempoFromLane() ? nullptr : hostPlayHead);
        lengthBeats = lanes::kLengthBars[juce::jlimit (0, 4, (int) std::lround (param (pLength)))] * 4.0;
        for (int k = 0; k < lanes::kLanes; ++k)
        {
            // (a patch from before 6.7.12: every step takes the lane's one shape)
            if (shapesFromParam[k].exchange (false))
            {
                const int s = juce::jlimit (0, lanes::kNumShapes - 1, (int) std::lround (param (shapeParam (k))));
                std::fill (data[(size_t) k].shapes.begin(), data[(size_t) k].shapes.end(), (std::uint8_t) s);
            }
            steps[k] = stepsFor (k);
        }
        const float ms = juce::jmax (0.2f, param (pSmooth));
        slew = 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) sampleRate));
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        clock.tick();
        // Restart In: the lanes start again here
        const float g = isInputConnected (0) ? inputs[0][0] : 0.0f;
        if (g > 0.5f && lastGate <= 0.5f) origin = clock.beat;
        lastGate = g;
        double x = (clock.beat - origin) / juce::jmax (0.125, lengthBeats);
        x -= std::floor (x);
        shownX.store ((float) x, std::memory_order_relaxed);
        for (int k = 0; k < lanes::kLanes; ++k)
        {
            const float v = lanes::valueAt (data[(size_t) k], steps[k], x);
            out[k] += (v - out[k]) * slew;
            outputs[k] = { out[k], out[k] };
        }
    }

    void uiButtonClicked (const juce::String& id) override { if (id == "throw") throwAll(); }

    //=== the card (message thread) =============================================
    int stepsFor (int k) const
    {
        const double len = lanes::kLengthBars[juce::jlimit (0, 4, (int) std::lround (getParameterBase (pLength)))];
        const double step = lanes::kStepBars[juce::jlimit (0, 6, (int) std::lround (getParameterBase (stepParam (k))))];
        return juce::jlimit (1, lanes::kMaxSteps, (int) std::lround (len / step));
    }
    // the lane's Shape (Mixed when its steps differ)
    int shapeOf (int k) const { return juce::jlimit (0, (int) lanes::kMixed, (int) std::lround (getParameterBase (shapeParam (k)))); }
    int stepShape (int k, int i) const { return juce::jmin ((int) data[(size_t) k].shapes[(size_t) juce::jlimit (0, lanes::kMaxSteps - 1, i)], lanes::kNumShapes - 1); }
    // one step's shape (and its repeats past the steps in Length), then the lane's Shape: one, or Mixed
    void setStepShape (int k, int i, int s)
    {
        auto& l = lane (k);
        const int n = stepsFor (k);
        for (int j = juce::jlimit (0, n - 1, i); j < lanes::kMaxSteps; j += n)
        {
            l.shapes[(size_t) j] = (std::uint8_t) s;
            if (s == lanes::sOnOff) l.heights[(size_t) j] = l.heights[(size_t) j] >= 0.5f ? 1.0f : 0.0f;
        }
        bool same = true;
        for (int j = 1; j < n && same; ++j) same = l.shapes[(size_t) j] == l.shapes[0];
        const int combo = same ? (int) l.shapes[0] : (int) lanes::kMixed;
        lastCombo[k] = combo;
        setParameter ("shape" + juce::String (k + 1), (float) combo);
        ++edits;
    }
    // the lane's Shape picked on the card: every step takes it (message thread, the card's timer)
    void followShapeChoice()
    {
        for (int k = 0; k < lanes::kLanes; ++k)
        {
            const int c = shapeOf (k);
            if (c == lastCombo[k]) continue;
            lastCombo[k] = c;
            if (c < lanes::kNumShapes)
            {
                auto& l = lane (k);
                std::fill (l.shapes.begin(), l.shapes.end(), (std::uint8_t) c);
                if (c == lanes::sOnOff) for (auto& h : l.heights) h = h >= 0.5f ? 1.0f : 0.0f;
                ++edits;
            }
        }
    }
    lanes::Lane& lane (int k) { return data[(size_t) juce::jlimit (0, lanes::kLanes - 1, k)]; }
    void rollLane (int k)
    {
        juce::Random rng (juce::Random::getSystemRandom().nextInt64());
        lanes::roll (lane (k), shapeOf (k), stepsFor (k), rng);
        ++edits;
    }
    void throwAll()
    {
        auto& r = juce::Random::getSystemRandom();
        static const int shapeWeights[lanes::kNumShapes] = { 3, 2, 2, 2, 2, 3, 2, 1 };
        for (int k = 0; k < lanes::kLanes; ++k)
        {
            int total = 0; for (int w : shapeWeights) total += w;
            int pick = r.nextInt (total), s = 0;
            while (pick >= shapeWeights[s]) { pick -= shapeWeights[s]; ++s; }
            if (r.nextFloat() < 0.3f) s = lanes::kMixed;   // (6.7.12: now and then a lane of mixed steps)
            setParameter ("shape" + juce::String (k + 1), (float) s);
            lastCombo[k] = s;
            setParameter ("step" + juce::String (k + 1), (float) (1 + r.nextInt (4)));   // (1/16 .. 1/2 bar)
            rollLane (k);
        }
    }
    // a lane set up from outside (the auto-connect): shape, step, a fresh roll
    void setUpLane (int k, int shapeIdx, int stepIdx)
    {
        setParameter ("shape" + juce::String (k + 1), (float) shapeIdx);
        lastCombo[k] = shapeIdx;
        setParameter ("step" + juce::String (k + 1), (float) stepIdx);
        rollLane (k);
    }

    std::atomic<float> shownX { 0.0f };
    int edits { 0 };   // (the card redraws when this moves)

    juce::String saveCustomState() const override
    {
        juce::String s;
        for (int k = 0; k < lanes::kLanes; ++k)
        {
            juce::StringArray h, d;
            for (float v : data[(size_t) k].heights) h.add (juce::String (juce::roundToInt (v * 1000.0f)));
            for (float v : data[(size_t) k].drawn) d.add (juce::String (juce::roundToInt (v * 1000.0f)));
            juce::String shapes;   // (6.7.12) a digit a step
            for (auto c : data[(size_t) k].shapes) shapes << juce::String::charToString ((juce::juce_wchar) ('0' + juce::jmin ((int) c, 9)));
            s << h.joinIntoString (",") << "|" << d.joinIntoString (",") << "|" << shapes << ";";
        }
        return s;
    }
    void loadCustomState (const juce::String& state) override
    {
        auto lanesText = juce::StringArray::fromTokens (state, ";", "");
        for (int k = 0; k < juce::jmin (lanes::kLanes, lanesText.size()); ++k)
        {
            const auto parts = juce::StringArray::fromTokens (lanesText[k], "|", "");
            if (parts.size() < 2) continue;
            const auto h = juce::StringArray::fromTokens (parts[0], ",", ""), d = juce::StringArray::fromTokens (parts[1], ",", "");
            for (int i = 0; i < juce::jmin (lanes::kMaxSteps, h.size()); ++i) data[(size_t) k].heights[(size_t) i] = juce::jlimit (0.0f, 1.0f, h[i].getIntValue() * 0.001f);
            for (int i = 0; i < juce::jmin (lanes::kDrawRes, d.size()); ++i) data[(size_t) k].drawn[(size_t) i] = juce::jlimit (0.0f, 1.0f, d[i].getIntValue() * 0.001f);
            if (parts.size() >= 3 && parts[2].length() >= lanes::kMaxSteps)
            {
                for (int i = 0; i < lanes::kMaxSteps; ++i)
                    data[(size_t) k].shapes[(size_t) i] = (std::uint8_t) juce::jlimit (0, lanes::kNumShapes - 1, (int) (parts[2][i] - '0'));
                lastCombo[k] = -1;    // (the card's timer takes the lane's Shape as it was saved)
            }
            else
                shapesFromParam[k].store (true);   // (older: the lane's one shape, once the knobs are in)
        }
        ++edits;
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.autolanes";
        d.displayName = "Automation Lanes";
        d.searchKeywords = "automation lanes modulation steps sequencer ramp gate random draw macro";
        d.description =
            "Four lanes of automation with four outputs, in time with the bars over Length (up to 8 bars). Each lane has "
            "its own Step (1/32 bar .. 2 bars) and Shape: Ramp, Slanted, Stairs, Sine, Random, On/Off, Spline or Draw. Roll "
            "throws a lane a new pattern, Throw All rolls everything; drag on a lane to draw it. The outputs drive knobs "
            "absolutely (0..1 is the knob's whole range; depth blends with the knob's own setting). Smooth rounds the "
            "steps off. From the sidebar it can come with four random effects already wired to its lanes.";
        d.section = ModuleSection::Utility;
        d.sockets = { modIn ("restartIn", "Restart In"),
                      unipolarOut ("lane1", "Lane 1"), unipolarOut ("lane2", "Lane 2"), unipolarOut ("lane3", "Lane 3"), unipolarOut ("lane4", "Lane 4"), tempoIn() };
        d.params = {
            makeSteppedList ("length", "Length", lanes::kLengthNames, 1, 0),
            makeRotary ("smooth", "Smooth", 0.0f, 50.0f, 4.0f, 0, "ms", false, 0.1f),
            makeButton ("throw", "Throw All", 0, 2) };
        static const int defShapes[lanes::kLanes] = { lanes::sRamp, lanes::sOnOff, lanes::sStairs, lanes::sSine };
        for (int k = 0; k < lanes::kLanes; ++k)
        {
            d.params.push_back (makeCombo ("shape" + juce::String (k + 1), "Lane " + juce::String (k + 1), lanes::kComboNames, defShapes[k], 1 + k / 2, 2).noMod());
            d.params.push_back (makeSteppedList ("step" + juce::String (k + 1), "Step", lanes::kStepNames, 2, 1 + k / 2).noMod());
        }
        return d;
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 214; }
    int preferredModuleWidth() const override { return 400; }

private:
    std::array<lanes::Lane, lanes::kLanes> data;
    float out[lanes::kLanes] {};
    int steps[lanes::kLanes] { 8, 8, 8, 8 };
    int lastCombo[lanes::kLanes] { -1, -1, -1, -1 };              // message thread: the Shape last followed
    std::atomic<bool> shapesFromParam[lanes::kLanes] {};      // a pre-6.7.12 patch: fill from the Shape
    double lengthBeats { 4.0 }, origin { 0.0 };
    float slew { 0.01f }, lastGate { 0.0f };
    BeatClock clock;
};

//==============================================================================
namespace
{
    class LanesView final : public juce::Component, private juce::Timer
    {
    public:
        explicit LanesView (AutomationLanesModule& m) : module (&m)
        {
            for (int k = 0; k < lanes::kLanes; ++k)
            {
                auto* b = rollButtons.add (new juce::TextButton ("Roll"));
                addAndMakeVisible (b);
                b->setColour (juce::TextButton::buttonColourId, lanes::kColours[k].withAlpha (0.35f));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                b->setTooltip ("Throws lane " + juce::String (k + 1) + " a new pattern of its shape");
                b->onClick = [this, k] { if (auto* mm = mod()) { mm->rollLane (k); repaint(); } };
            }
            startTimerHz (30);
        }
        AutomationLanesModule* mod() const { return dynamic_cast<AutomationLanesModule*> (module.get()); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4);
            const int h = (r.getHeight() - 3 * 4) / lanes::kLanes;
            strips.clear();
            for (int k = 0; k < lanes::kLanes; ++k)
            {
                auto row = r.removeFromTop (h);
                r.removeFromTop (4);
                rollButtons[k]->setBounds (row.removeFromLeft (40).reduced (0, juce::jmax (0, (h - 22) / 2)));
                row.removeFromLeft (4);
                strips.push_back (row);
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            if (m == nullptr) return;
            const auto look = LookPalette::current (juce::Colour (0xffffb04f), ThemeColours::Utility);
            const float x = m->shownX.load (std::memory_order_relaxed);
            for (int k = 0; k < lanes::kLanes && k < (int) strips.size(); ++k)
            {
                const auto full = strips[(size_t) k].toFloat();
                look.fillBackground (g, full);
                const int shape = m->shapeOf (k), steps = m->stepsFor (k);
                const auto col = look.skeuo ? look.accent : lanes::kColours[k];
                // (6.7.12) the strip of step shapes under the lane: tap one to change it
                const auto bar = shapeBar (k).toFloat();
                const auto r = full.withBottom (bar.getY());
                {
                    g.setColour (juce::Colours::black.withAlpha (0.28f));
                    g.fillRect (bar);
                    const float cw = bar.getWidth() / (float) steps;
                    for (int i = 0; i < steps; ++i)
                    {
                        const int s = m->stepShape (k, i);
                        const auto cell = juce::Rectangle<float> (bar.getX() + cw * (float) i, bar.getY(), cw, bar.getHeight());
                        g.setColour (col.withAlpha (0.10f + 0.06f * (float) (s % 3)));
                        g.fillRect (cell.reduced (cw > 4.0f ? 0.5f : 0.0f, 0.0f));
                        if (cw >= 9.0f)
                        {
                            g.setColour (col.withAlpha (0.9f));
                            g.strokePath (shapeIcon (s, cell.reduced (juce::jmin (3.0f, cw * 0.18f), 2.0f)), juce::PathStrokeType (1.1f));
                        }
                    }
                }
                // the steps' edges
                if (steps <= 64)
                {
                    g.setColour (look.grid.withAlpha (0.08f));
                    for (int i = 1; i < steps; ++i) g.fillRect (r.getX() + r.getWidth() * (float) i / (float) steps, r.getY(), 1.0f, full.getHeight());
                }
                // the lane's line, filled under
                juce::Path line, fill;
                const int n = juce::jmax (64, juce::jmin (800, (int) r.getWidth() * 2));
                for (int i = 0; i <= n; ++i)
                {
                    const double px = (double) i / n;
                    const float v = lanes::valueAt (m->lane (k), steps, juce::jmin (0.99999, px));
                    const float xx = r.getX() + r.getWidth() * (float) px, yy = r.getBottom() - 2.0f - v * (r.getHeight() - 4.0f);
                    if (i == 0) { line.startNewSubPath (xx, yy); fill.startNewSubPath (xx, r.getBottom()); fill.lineTo (xx, yy); }
                    else { line.lineTo (xx, yy); fill.lineTo (xx, yy); }
                }
                fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
                g.setColour (col.withAlpha (0.16f));
                g.fillPath (fill);
                LookPalette lp = look; lp.accent = col;
                lp.strokeGlowing (g, line, 1.6f);
                // where it plays now
                const float px = r.getX() + r.getWidth() * x;
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.fillRect (px - 0.5f, r.getY(), 1.0f, r.getHeight());
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.setFont (juce::Font (juce::FontOptions (9.5f)));
                g.drawText (juce::String (k + 1) + "  " + lanes::kComboNames[shape], r.reduced (5.0f, 2.0f), juce::Justification::topLeft, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // (6.7.12) a step in the shape strip: its shape from a menu
            for (int k = 0; k < (int) strips.size(); ++k)
                if (shapeBar (k).contains (e.getPosition()))
                {
                    pickStepShape (k, e.position);
                    dragLane = -1;
                    return;
                }
            dragLane = laneAt (e.getPosition());
            lastDraw = { -1.0f, 0.0f };
            paintAt (e.position);
        }
        void mouseDrag (const juce::MouseEvent& e) override { paintAt (e.position); }
        void mouseUp (const juce::MouseEvent&) override { dragLane = -1; }

    private:
        int laneAt (juce::Point<int> p) const
        {
            for (int k = 0; k < (int) strips.size(); ++k) if (strips[(size_t) k].contains (p)) return k;
            return -1;
        }
        juce::Rectangle<int> shapeBar (int k) const
        {
            const auto s = strips[(size_t) k];
            return s.withTop (s.getBottom() - juce::jlimit (8, 12, s.getHeight() / 4));
        }
        // a little picture of a step's shape
        static juce::Path shapeIcon (int s, juce::Rectangle<float> c)
        {
            juce::Path p;
            const float x0 = c.getX(), x1 = c.getRight(), y0 = c.getY(), y1 = c.getBottom(), xm = c.getCentreX(), ym = c.getCentreY();
            switch (s)
            {
                case lanes::sRamp:    p.startNewSubPath (x0, y1); p.lineTo (x1, y0); p.lineTo (x1, y1); break;
                case lanes::sSlanted: p.startNewSubPath (x0, y1); p.lineTo (x1, ym); break;
                case lanes::sStairs:  p.startNewSubPath (x0, y1); p.lineTo (x0, ym); p.lineTo (xm, ym); p.lineTo (xm, y0); p.lineTo (x1, y0); break;
                case lanes::sSine:    p.startNewSubPath (x0, y1); p.quadraticTo (xm, y0 - (y1 - y0), x1, y1); break;
                case lanes::sRandom:  p.startNewSubPath (x0, ym); p.lineTo (x0 + (x1 - x0) * 0.33f, ym); p.lineTo (x0 + (x1 - x0) * 0.33f, y0);
                                      p.lineTo (x0 + (x1 - x0) * 0.66f, y0); p.lineTo (x0 + (x1 - x0) * 0.66f, y1); p.lineTo (x1, y1); break;
                case lanes::sOnOff:   p.startNewSubPath (x0, y1); p.lineTo (x0, y0); p.lineTo (xm, y0); p.lineTo (xm, y1); p.lineTo (x1, y1); break;
                case lanes::sSpline:  p.startNewSubPath (x0, ym); p.cubicTo (x0 + (x1 - x0) * 0.35f, y0 - 2.0f, x0 + (x1 - x0) * 0.65f, y1 + 2.0f, x1, ym); break;
                case lanes::sDraw: default:
                                      p.startNewSubPath (x0, ym); p.lineTo (x0 + (x1 - x0) * 0.25f, y0); p.lineTo (xm, y1 - 1.0f);
                                      p.lineTo (x0 + (x1 - x0) * 0.75f, y0 + 1.0f); p.lineTo (x1, ym); break;
            }
            return p;
        }
        void pickStepShape (int k, juce::Point<float> at)
        {
            auto* m = mod();
            if (m == nullptr) return;
            const auto bar = shapeBar (k).toFloat();
            const int steps = m->stepsFor (k);
            const int i = juce::jlimit (0, steps - 1, (int) ((at.x - bar.getX()) / bar.getWidth() * (float) steps));
            juce::PopupMenu menu;
            menu.addSectionHeader ("Step " + juce::String (i + 1) + " of lane " + juce::String (k + 1));
            for (int s = 0; s < lanes::kNumShapes; ++s)
                menu.addItem (1 + s, lanes::kShapeNames[s], true, m->stepShape (k, i) == s);
            juce::Component::SafePointer<LanesView> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, k, i] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                if (auto* mm = safe->mod()) { mm->setStepShape (k, i, r - 1); safe->repaint(); }
            });
        }
        // drawing: a step's height, or the free line
        void paintAt (juce::Point<float> p)
        {
            auto* m = mod();
            if (m == nullptr || dragLane < 0) return;
            const auto r = strips[(size_t) dragLane].withBottom (shapeBar (dragLane).getY()).toFloat();
            const float fx = juce::jlimit (0.0f, 0.99999f, (p.x - r.getX()) / r.getWidth());
            const float v = juce::jlimit (0.0f, 1.0f, (r.getBottom() - 2.0f - p.y) / (r.getHeight() - 4.0f));
            auto& l = m->lane (dragLane);
            const int stepsHere = m->stepsFor (dragLane);
            const int stepHere = juce::jlimit (0, stepsHere - 1, (int) (fx * stepsHere));
            if (m->stepShape (dragLane, stepHere) == lanes::sDraw)
            {
                const int i = (int) (fx * (lanes::kDrawRes - 1));
                if (lastDraw.x >= 0.0f)
                {
                    const int i0 = (int) (lastDraw.x * (lanes::kDrawRes - 1));
                    const int a = juce::jmin (i0, i), b = juce::jmax (i0, i);
                    for (int j = a; j <= b; ++j)
                    {
                        const float t = b == a ? 1.0f : (float) (j - i0) / (float) (i - i0);
                        l.drawn[(size_t) j] = lastDraw.y + (v - lastDraw.y) * juce::jlimit (0.0f, 1.0f, t);
                    }
                }
                else l.drawn[(size_t) i] = v;
                lastDraw = { fx, v };
            }
            else
            {
                const int steps = m->stepsFor (dragLane);
                const int i = juce::jlimit (0, steps - 1, (int) (fx * steps));
                const float h = m->stepShape (dragLane, i) == lanes::sOnOff ? (v >= 0.5f ? 1.0f : 0.0f) : v;
                // (the pattern repeats past the steps shown: keep the repeats in step)
                for (int j = i; j < lanes::kMaxSteps; j += steps) l.heights[(size_t) j] = h;
            }
            ++m->edits;
            repaint();
        }
        void timerCallback() override { if (auto* m = mod()) m->followShapeChoice(); repaint(); }

        juce::WeakReference<SynthModule> module;
        juce::OwnedArray<juce::TextButton> rollButtons;
        std::vector<juce::Rectangle<int>> strips;
        int dragLane { -1 };
        juce::Point<float> lastDraw { -1.0f, 0.0f };
    };
}

std::unique_ptr<juce::Component> AutomationLanesModule::createExtraContentComponent()
{
    return std::make_unique<LanesView> (*this);
}

//==============================================================================
// From the sidebar with four effects (6.7.11): four chosen at random from the
// pool, each lane on its effect's amount; with a patch of just one sound
// source (and its Poly Sum) and an Audio Out, they are wired in parallel to it
// - the source into each, each into the Audio Out, the source still going
// there straight - and each lane opens its effect's level (the effects run
// fully wet, so a lane at 0 is silent).
namespace
{
    juce::String firstSocket (const ModuleDescriptor& d, SocketKind kind, bool output)
    {
        for (const auto& s : d.sockets)
            if (s.kind == kind && (s.direction == SocketDirection::Output) == output && ! s.hidden) return s.id;
        return {};
    }
}

int autoLanesAddWithEffects (AquanodeModularAudioProcessor& proc, juce::Point<int> at, juce::uint32 seed)
{
    // the patch as it is: one sound source (+ Poly Sum) and one Audio Out?
    int source = -1, polysum = -1, audioOut = -1, sources = 0, outs = 0, others = 0;
    for (int id : proc.getModuleIds())
    {
        auto* inst = proc.getInstance (id);
        if (inst == nullptr) continue;
        const auto& d = inst->descriptor();
        if (d.typeId == "io.audioOut") { audioOut = id; ++outs; }
        else if (d.typeId == "util.polysum") { polysum = id; }
        else if (d.section == ModuleSection::Oscillator && firstSocket (d, SocketKind::Audio, true).isNotEmpty()) { source = id; ++sources; }
        else ++others;
    }
    const bool parallel = sources == 1 && outs == 1 && others == 0;

    const int lanesId = proc.addModule ("util.autolanes", at);
    auto* lanesInst = proc.getInstance (lanesId);
    auto* lanesMod = lanesInst != nullptr ? dynamic_cast<AutomationLanesModule*> (lanesInst->dsp.get()) : nullptr;
    if (lanesMod == nullptr) return lanesId;

    // the pool: each with the knob a lane drives, how it sets itself up, and the lane's shape
    struct Fx { const char* type; const char* amount; const char* engage; int shape; int step; };
    static const Fx pool[] = {
        { "fx.stutter",    "stutter", "stutter", lanes::sOnOff,   3 },
        { "fx.reverser",   "reverse", "reverse", lanes::sOnOff,   4 },
        { "fx.tapestop",   "stop",    "stop",    lanes::sOnOff,   4 },
        { "fx.srreduce",   "dryWet",  "",        lanes::sRamp,    2 },
        { "fx.bitcrush",   "dryWet",  "",        lanes::sStairs,  2 },
        { "util.volume",   "level",   "",        lanes::sOnOff,   1 },
        { "fx.delay",      "dryWet",  "",        lanes::sOnOff,   2 },
        { "fx.pitchshift", "mix",     "",        lanes::sOnOff,   3 } };
    constexpr int kPool = (int) (sizeof (pool) / sizeof (pool[0]));
    // (the seed stirred first: juce::Random's first draws from a small seed lean all one way)
    juce::Random rng ((juce::int64) ((juce::uint64) seed * 0x9E3779B97F4A7C15ull ^ 0x5851F42D4C957F2Dull));
    for (int i = 0; i < 4; ++i) rng.nextInt();
    int order[kPool];
    for (int i = 0; i < kPool; ++i) order[i] = i;
    for (int i = kPool - 1; i > 0; --i) std::swap (order[i], order[rng.nextInt (i + 1)]);

    const auto srcInst = source >= 0 ? proc.getInstance (source) : nullptr;
    const bool perVoiceSource = srcInst != nullptr && srcInst->dsp->voiceMode() == VoiceMode::PerVoice;
    const int feedFrom = parallel && perVoiceSource && polysum >= 0 ? polysum : source;
    const juce::String feedSock = feedFrom == polysum && polysum >= 0 ? juce::String ("audioOut")
                                : (srcInst != nullptr ? firstSocket (srcInst->descriptor(), SocketKind::Audio, true) : juce::String());

    for (int k = 0; k < lanes::kLanes; ++k)
    {
        const auto& fx = pool[order[k]];
        const int fxId = proc.addModule (fx.type, at + juce::Point<int> (440, k * 250));
        auto* inst = proc.getInstance (fxId);
        if (inst == nullptr) continue;
        auto* dsp = inst->dsp.get();
        const juce::String type (fx.type);
        // each effect set up for its lane
        if (type == "fx.pitchshift") dsp->setParameter ("pitch", rng.nextBool() ? 12.0f : 7.0f);
        if (type == "fx.delay") { dsp->setParameter ("timeL", 1.0f); dsp->setParameter ("timeR", 2.0f); dsp->setParameter ("feedback", 0.45f); }
        if (type == "fx.bitcrush") dsp->setParameter ("bits", 6.0f);
        if (type == "fx.srreduce") dsp->setParameter ("rate", 3000.0f);
        lanesMod->setUpLane (k, fx.shape, fx.step);
        const juce::String lane = "lane" + juce::String (k + 1);

        if (parallel)
        {
            // fully wet; the lane opens the effect's level (Delay: its send, so the echoes ring on)
            for (const char* wetId : { "dryWet", "mix" })
                for (const auto& p : inst->descriptor().params)
                    if (p.id == wetId) dsp->setParameter (wetId, 100.0f);
            // (each lane opens its effect to 0.6, -4.4 dB: four on top of the source keep their headroom)
            if (type == "fx.delay")
            {
                dsp->setParameter ("send", 0.0f);
                proc.addParamCable (lanesId, lane, fxId, "send", 0.6f);
            }
            else if (type == "util.volume")
            {
                // Level runs 0..2: from 0, 0.3 of the range reaches 0.6 at the lane's top
                dsp->setParameter ("level", 0.0f);
                proc.addParamCable (lanesId, lane, fxId, "level", 0.3f);
            }
            else
            {
                dsp->setParameter ("level", 0.0f);
                proc.addParamCable (lanesId, lane, fxId, "level", 0.6f);
                if (juce::String (fx.engage).isNotEmpty()) proc.addParamCable (lanesId, lane, fxId, fx.engage, 1.0f);
            }
            const auto in = firstSocket (inst->descriptor(), SocketKind::Audio, false);
            const auto out = firstSocket (inst->descriptor(), SocketKind::Audio, true);
            if (feedFrom >= 0 && feedSock.isNotEmpty()) proc.addCable (feedFrom, feedSock, fxId, in);
            if (audioOut >= 0) proc.addCable (fxId, out, audioOut, "audioIn");
        }
        else
        {
            if (type == "util.volume") { dsp->setParameter ("level", 0.0f); proc.addParamCable (lanesId, lane, fxId, "level", 0.5f); }
            else proc.addParamCable (lanesId, lane, fxId, fx.amount, 1.0f);
        }
    }
    return lanesId;
}

AQUANODE_REGISTER_MODULE (AutomationLanesModule, AutomationLanesModule::descriptor)

//==============================================================================
// for tests (6.7.12): lane 1 over one bar in 1/8 steps, every height at the
// top, its steps On/Off and Ramp by turns; and a step's shape / the lane's Shape
void autoLanesTestMixedSteps (SynthModule* m)
{
    auto* a = dynamic_cast<AutomationLanesModule*> (m);
    if (a == nullptr) return;
    a->setParameter ("length", 1.0f);   // 1 bar
    a->setParameter ("step1", 2.0f);    // 1/8 bar
    a->setParameter ("smooth", 0.2f);
    auto& l = a->lane (0);
    for (auto& h : l.heights) h = 1.0f;
    for (int i = 0; i < 8; ++i) a->setStepShape (0, i, i % 2 == 0 ? lanes::sOnOff : lanes::sRamp);
}
int autoLanesTestStepShape (SynthModule* m, int lane, int step)
{
    auto* a = dynamic_cast<AutomationLanesModule*> (m);
    return a != nullptr ? a->stepShape (lane, step) : -1;
}
