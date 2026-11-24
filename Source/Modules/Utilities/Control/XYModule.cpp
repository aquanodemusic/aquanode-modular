//==============================================================================
// XY (6.9.15) - a pad for two sources: X is A's amount, Y is B's. Bottom left
// both are silent, top right both play fully, along the bottom only A comes in,
// up the left side only B. It mixes A In and B In into Audio Out by those
// amounts and sends the amounts as modulation (A Out = X, B Out = Y, 0..1) for
// anything else to follow. X and Y take knob cables; Glide smooths jumps. The
// card draws the square and the dot (drag it to set X and Y).
//
// Flexible, as Vector: per voice when fed per voice, else in the global lane.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class XYModule final : public SynthModule
{
public:
    enum P { pX, pY, pGlide };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.xy";
        d.displayName = "XY";
        d.description = "A pad for two sources: X is how much of A, Y how much of B - bottom left both silent, top right "
                        "both full, along the bottom only A, up the left only B. Mixes A In and B In into Audio Out by "
                        "those amounts and sends them as modulation (A Out = X, B Out = Y, 0..1). Cable LFOs or envelopes "
                        "into X and Y to move the dot; drag it on the card (a double tap: the middle).";
        d.section = ModuleSection::Utility;
        d.searchKeywords = "xy pad joystick two sources crossfade vector";
        d.sockets = {
            audioIn ("aIn", "A In"), audioIn ("bIn", "B In"),
            audioOut ("audioOut", "Audio Out"),
            unipolarOut ("aOut", "A Out"), unipolarOut ("bOut", "B Out")
        };
        d.params = {
            makeRotary ("x", "X (A)", 0.0f, 1.0f, 0.5f, 0),
            makeRotary ("y", "Y (B)", 0.0f, 1.0f, 0.5f, 0),
            makeRotary ("glide", "Glide", 0.0f, 1000.0f, 5.0f, 0, "ms")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& l : lanes) l = { param (pX), param (pY) }; }
    void voiceReset (int v) override { lanes[(size_t) v] = { param (pX), param (pY) }; }

    void processSample (const StereoFrame* in, StereoFrame* out) override { run (kMaxVoices, in, out); }
    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override { run (v, in, out); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 190; }

    float shownX() const { return liveX.load (std::memory_order_relaxed); }
    float shownY() const { return liveY.load (std::memory_order_relaxed); }

private:
    struct Lane { float x = 0.5f, y = 0.5f; };

    void run (int lane, const StereoFrame* in, StereoFrame* out)
    {
        const float gMs = param (pGlide);
        const float k = gMs < 0.05f ? 1.0f : 1.0f - std::exp (-1.0f / (gMs * 0.001f * (float) sampleRate));
        auto& l = lanes[(size_t) lane];
        l.x += k * (juce::jlimit (0.0f, 1.0f, param (pX)) - l.x);
        l.y += k * (juce::jlimit (0.0f, 1.0f, param (pY)) - l.y);
        for (int c = 0; c < 2; ++c)
            out[0][(size_t) c] = in[0][(size_t) c] * l.x + in[1][(size_t) c] * l.y;
        out[1] = { l.x, l.x };
        out[2] = { l.y, l.y };
        liveX.store (l.x, std::memory_order_relaxed);
        liveY.store (l.y, std::memory_order_relaxed);
    }

    std::array<Lane, kMaxVoices + 1> lanes {};
    std::atomic<float> liveX { 0.5f }, liveY { 0.5f };
};

namespace
{
    class XYPad : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit XYPad (XYModule& m) : weak (&m), raw (&m) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            const auto sq = square();
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (sq.expanded (6.0f), 6.0f);
            // a wash from nothing (bottom left) to both (top right)
            juce::ColourGradient wash (juce::Colour (0xff4fc3f7).withAlpha (0.02f), sq.getBottomLeft(),
                                       juce::Colour (0xff8dffb0).withAlpha (0.28f), sq.getTopRight(), false);
            g.setGradientFill (wash);
            g.fillRect (sq);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.drawRect (sq, 1.0f);
            for (int i = 1; i < 4; ++i)
            {
                const float f = (float) i / 4.0f;
                g.drawLine (sq.getX() + sq.getWidth() * f, sq.getY(), sq.getX() + sq.getWidth() * f, sq.getBottom(), 0.4f);
                g.drawLine (sq.getX(), sq.getY() + sq.getHeight() * f, sq.getRight(), sq.getY() + sq.getHeight() * f, 0.4f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText ("A >", juce::Rectangle<float> (sq.getRight() - 40.0f, sq.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centredRight);
            g.drawText ("B ^", juce::Rectangle<float> (sq.getX() - 34.0f, sq.getY(), 30.0f, 14.0f), juce::Justification::centredRight);
            if (m == nullptr) return;
            const float x = m->shownX(), y = m->shownY();
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawText ("A " + juce::String (juce::roundToInt (x * 100.0f)) + "%   B " + juce::String (juce::roundToInt (y * 100.0f)) + "%",
                        juce::Rectangle<float> (sq.getX(), sq.getBottom() + 2.0f, sq.getWidth() - 44.0f, 14.0f), juce::Justification::centredLeft);
            const auto live = toScreen (x, y);
            const auto set = toScreen (m->getParameter ("x"), m->getParameter ("y"));
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawEllipse (set.x - 7.0f, set.y - 7.0f, 14.0f, 14.0f, 1.2f);
            g.setColour (juce::Colour (0xff8dffb0).withAlpha (0.35f));
            g.fillEllipse (live.x - 9.0f, live.y - 9.0f, 18.0f, 18.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (live.x - 5.0f, live.y - 5.0f, 10.0f, 10.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override { moveTo (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { moveTo (e.position); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setXY (0.5f, 0.5f); }

    private:
        XYModule* mod() const { return weak.get() != nullptr ? raw : nullptr; }
        juce::Rectangle<float> square() const
        {
            const float side = juce::jmin ((float) getWidth() - 80.0f, (float) getHeight() - 36.0f);
            return juce::Rectangle<float> (side, side).withCentre (getLocalBounds().toFloat().getCentre().translated (0.0f, -6.0f));
        }
        juce::Point<float> toScreen (float x, float y) const
        {
            const auto sq = square();
            return { sq.getX() + x * sq.getWidth(), sq.getBottom() - y * sq.getHeight() };
        }
        void moveTo (juce::Point<float> p)
        {
            const auto sq = square();
            setXY ((p.x - sq.getX()) / sq.getWidth(), (sq.getBottom() - p.y) / sq.getHeight());
        }
        void setXY (float x, float y)
        {
            if (auto* m = mod())
            {
                m->setParameter ("x", juce::jlimit (0.0f, 1.0f, x));
                m->setParameter ("y", juce::jlimit (0.0f, 1.0f, y));
                if (paramValuesChangedByContent) paramValuesChangedByContent();
                repaint();
            }
        }
        void timerCallback() override
        {
            if (auto* m = mod())
            {
                const float x = m->shownX(), y = m->shownY(), sx = m->getParameter ("x"), sy = m->getParameter ("y");
                if (x != lastX || y != lastY || sx != lastSX || sy != lastSY)
                {
                    lastX = x; lastY = y; lastSX = sx; lastSY = sy;
                    repaint();
                }
            }
        }

        juce::WeakReference<SynthModule> weak;
        XYModule* raw;
        float lastX { 9.0f }, lastY { 9.0f }, lastSX { 9.0f }, lastSY { 9.0f };
    };
}

std::unique_ptr<juce::Component> XYModule::createExtraContentComponent()
{
    return std::make_unique<XYPad> (*this);
}

AQUANODE_REGISTER_MODULE (XYModule, XYModule::descriptor)
