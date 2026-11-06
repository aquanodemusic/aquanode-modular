//==============================================================================
// Vector - a joystick for four sources, as on the vector synths: X and Y place
// a dot inside a square, and a square turned by 45 degrees inside it has the
// four sources at its corners - A left, B top, C right, D bottom. Where the
// dot is decides how much of each sounds: on a corner only that one, in the
// middle all four a quarter each, along an edge the two at its ends (the
// square's own corners, outside the diamond, give the pair the edge between
// them would).
//
// It mixes four audio inputs (A..D In -> Audio Out) by those amounts, and
// gives the amounts themselves as modulation (A..D Out, 0..1, together always
// 1) for anything else to follow - four oscillators' levels, four filters,
// four effect sends. X and Y take knob cables (LFOs circling the dot, an
// envelope sweeping it, the Mod Wheel); Glide smooths jumps. The card draws
// the square, the diamond and the dot (drag it to set X and Y), the dot where
// the cables have moved it, and each corner's share.
//
// Flexible: per voice when fed per voice (an envelope per note on X) or when
// its cables come from per-voice sources, else in the global lane.
//==============================================================================
#include "../../Core/ModuleCore.h"

namespace
{
    using namespace aquanode;

    // the four shares at (x, y), each in -1..1 (outside the diamond: its edge)
    inline void vectorWeights (float x, float y, float w[4])
    {
        const float s = juce::jlimit (-1.0f, 1.0f, x + y);
        const float t = juce::jlimit (-1.0f, 1.0f, y - x);
        w[0] = (1.0f - s) * (1.0f + t) * 0.25f;   // A, left
        w[1] = (1.0f + s) * (1.0f + t) * 0.25f;   // B, top
        w[2] = (1.0f + s) * (1.0f - t) * 0.25f;   // C, right
        w[3] = (1.0f - s) * (1.0f - t) * 0.25f;   // D, bottom
    }
}

class VectorModule final : public SynthModule
{
public:
    enum P { pX, pY, pGlide };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.vector";
        d.displayName = "Vector";
        d.description = "A joystick for four sources: X and Y place a dot in a square, and the square turned by 45 degrees "
                        "inside it has A, B, C and D at its corners (left, top, right, bottom). On a corner only that one "
                        "sounds, in the middle all four a quarter each. Mixes A..D In into Audio Out by those shares and "
                        "sends the shares as modulation (A..D Out, 0..1). Cable LFOs or envelopes into X and Y to move the "
                        "dot; drag it on the card.";
        d.section = ModuleSection::Utility;
        d.sockets = {
            audioIn ("aIn", "A In"), audioIn ("bIn", "B In"), audioIn ("cIn", "C In"), audioIn ("dIn", "D In"),
            audioOut ("audioOut", "Audio Out"),
            unipolarOut ("aOut", "A Out"), unipolarOut ("bOut", "B Out"), unipolarOut ("cOut", "C Out"), unipolarOut ("dOut", "D Out")
        };
        d.params = {
            makeRotary ("x", "X", -1.0f, 1.0f, 0.0f, 0),
            makeRotary ("y", "Y", -1.0f, 1.0f, 0.0f, 0),
            makeRotary ("glide", "Glide", 0.0f, 1000.0f, 5.0f, 0, "ms")
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (auto& l : lanes) l = { param (pX), param (pY) };
    }

    void voiceReset (int v) override { lanes[(size_t) v] = { param (pX), param (pY) }; }

    void processSample (const StereoFrame* in, StereoFrame* out) override { run (kMaxVoices, in, out); }
    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override { run (v, in, out); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 214; }

    // where the cables have moved the dot (the last lane that ran), for the card
    float shownX() const { return liveX.load (std::memory_order_relaxed); }
    float shownY() const { return liveY.load (std::memory_order_relaxed); }

private:
    struct Lane { float x = 0.0f, y = 0.0f; };

    void run (int lane, const StereoFrame* in, StereoFrame* out)
    {
        const float gMs = param (pGlide);
        const float k = gMs < 0.05f ? 1.0f : 1.0f - std::exp (-1.0f / (gMs * 0.001f * (float) sampleRate));
        auto& l = lanes[(size_t) lane];
        l.x += k * (juce::jlimit (-1.0f, 1.0f, param (pX)) - l.x);
        l.y += k * (juce::jlimit (-1.0f, 1.0f, param (pY)) - l.y);
        float w[4];
        vectorWeights (l.x, l.y, w);
        for (int c = 0; c < 2; ++c)
            out[0][(size_t) c] = in[0][(size_t) c] * w[0] + in[1][(size_t) c] * w[1] + in[2][(size_t) c] * w[2] + in[3][(size_t) c] * w[3];
        for (int i = 0; i < 4; ++i)
            out[1 + i] = { w[i], w[i] };
        liveX.store (l.x, std::memory_order_relaxed);
        liveY.store (l.y, std::memory_order_relaxed);
    }

    std::array<Lane, kMaxVoices + 1> lanes {};
    std::atomic<float> liveX { 0.0f }, liveY { 0.0f };
};

//==============================================================================
// the card: the square, the diamond, the dot
//==============================================================================
namespace
{
    class VectorPad : public juce::Component,
                      public ExtraContentParamListener,
                      private juce::Timer
    {
    public:
        explicit VectorPad (VectorModule& m) : weak (&m), raw (&m) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            auto* m = mod();
            const auto sq = square();
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (sq.expanded (6.0f), 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRect (sq, 1.0f);
            g.drawLine (sq.getX(), sq.getCentreY(), sq.getRight(), sq.getCentreY(), 0.6f);
            g.drawLine (sq.getCentreX(), sq.getY(), sq.getCentreX(), sq.getBottom(), 0.6f);

            // the diamond: the square turned by 45 degrees, its corners the sources
            juce::Path diamond;
            const auto a = toScreen (-1.0f, 0.0f), b = toScreen (0.0f, 1.0f), c = toScreen (1.0f, 0.0f), d = toScreen (0.0f, -1.0f);
            diamond.startNewSubPath (a); diamond.lineTo (b); diamond.lineTo (c); diamond.lineTo (d); diamond.closeSubPath();
            g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.12f));
            g.fillPath (diamond);
            g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.8f));
            g.strokePath (diamond, juce::PathStrokeType (1.6f));

            // each corner's share, as a disc
            if (m != nullptr)
            {
                float w[4];
                vectorWeights (m->shownX(), m->shownY(), w);
                const juce::Point<float> corners[4] = { a, b, c, d };
                const char* names[4] = { "A", "B", "C", "D" };
                for (int i = 0; i < 4; ++i)
                {
                    const float r = 4.0f + 10.0f * std::sqrt (w[i]);
                    g.setColour (juce::Colour (0xff8dffb0).withAlpha (0.25f + 0.6f * w[i]));
                    g.fillEllipse (corners[i].x - r, corners[i].y - r, 2.0f * r, 2.0f * r);
                    g.setColour (juce::Colours::white);
                    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
                    auto label = juce::Rectangle<float> (0.0f, 0.0f, 40.0f, 14.0f).withCentre (corners[i]);
                    if (i == 0) label = label.translated (-24.0f, 0.0f);
                    if (i == 1) label = label.translated (0.0f, -17.0f);
                    if (i == 2) label = label.translated (24.0f, 0.0f);
                    if (i == 3) label = label.translated (0.0f, 17.0f);
                    g.drawText (juce::String (names[i]) + " " + juce::String (juce::roundToInt (w[i] * 100.0f)) + "%",
                                label.expanded (8.0f, 0.0f), juce::Justification::centred);
                }

                // the dot where the cables have it, and the knobs' own position
                const auto live = toScreen (m->shownX(), m->shownY());
                const auto set = toScreen (m->getParameter ("x"), m->getParameter ("y"));
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawEllipse (set.x - 7.0f, set.y - 7.0f, 14.0f, 14.0f, 1.2f);
                g.setColour (juce::Colour (0xff8dffb0).withAlpha (0.35f));
                g.fillEllipse (live.x - 9.0f, live.y - 9.0f, 18.0f, 18.0f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (live.x - 5.0f, live.y - 5.0f, 10.0f, 10.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override { moveTo (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { moveTo (e.position); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setXY (0.0f, 0.0f); }

    private:
        VectorModule* mod() const { return weak.get() != nullptr ? raw : nullptr; }
        juce::Rectangle<float> square() const
        {
            const float side = juce::jmin ((float) getWidth() - 90.0f, (float) getHeight() - 44.0f);
            return juce::Rectangle<float> (side, side).withCentre (getLocalBounds().toFloat().getCentre());
        }
        juce::Point<float> toScreen (float x, float y) const
        {
            const auto sq = square();
            return { sq.getCentreX() + x * sq.getWidth() * 0.5f, sq.getCentreY() - y * sq.getHeight() * 0.5f };
        }
        void moveTo (juce::Point<float> p)
        {
            const auto sq = square();
            setXY ((p.x - sq.getCentreX()) / (sq.getWidth() * 0.5f), (sq.getCentreY() - p.y) / (sq.getHeight() * 0.5f));
        }
        void setXY (float x, float y)
        {
            if (auto* m = mod())
            {
                m->setParameter ("x", juce::jlimit (-1.0f, 1.0f, x));
                m->setParameter ("y", juce::jlimit (-1.0f, 1.0f, y));
                if (paramValuesChangedByContent) paramValuesChangedByContent();
                repaint();
            }
        }
        void timerCallback() override
        {
            if (auto* m = mod())
            {
                const float x = m->shownX(), y = m->shownY();
                const float sx = m->getParameter ("x"), sy = m->getParameter ("y");
                if (x != lastX || y != lastY || sx != lastSX || sy != lastSY)
                {
                    lastX = x; lastY = y; lastSX = sx; lastSY = sy;
                    repaint();
                }
            }
        }

        juce::WeakReference<SynthModule> weak;
        VectorModule* raw;
        float lastX { 9.0f }, lastY { 9.0f }, lastSX { 9.0f }, lastSY { 9.0f };
    };
}

std::unique_ptr<juce::Component> VectorModule::createExtraContentComponent()
{
    return std::make_unique<VectorPad> (*this);
}

AQUANODE_REGISTER_MODULE (VectorModule, VectorModule::descriptor)
