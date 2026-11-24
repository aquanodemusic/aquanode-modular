#pragma once
//==============================================================================
// What the editor's parts share: the module cards' layout constants, the
// Skeuomorphic look (its colours, the knobs' and screens' LookAndFeel, the
// glass bars, the water-ring logo) and the app's settings file.
//==============================================================================
#include "PluginEditor.h"

using namespace aquanode;

//==============================================================================
// layout constants
//==============================================================================
namespace layout
{
    constexpr int pad = 10;
    constexpr int topRow = 18;         // the card's top row: Randomize, the collapse tab, Undo
    constexpr int socketColW = 76;     // width of each side socket column in the header
    constexpr int socketSlotH = 30;    // vertical space per socket (incl. label)
    constexpr int headerMinH = 52;     // minimum header height (title area)
    constexpr int knobRowH = 82;       // taller -> bigger knobs
    constexpr int wideRowH = 32;       // rows containing only combos/buttons
    constexpr int barRowH = 40;        // rows of horizontal bars (no knobs)
    constexpr int unitsPerRow = 5;
    constexpr int socketSize = 15;
    constexpr int gridSpacing = 24;
}
//==============================================================================
// The Skeuomorphic look of the module cards' own controls (Colours >
// Skeuomorphic), after AlphaBeta's panel: round satin-silver knobs with a dark
// rim, a dark track around them and a watery blue-to-cyan value arc that glows
// (the glow drawn as VirtualDX7 draws its curves: wide faint strokes under a
// crisp core, here in a bright saturated cyan); values and choices on small
// blue phosphor screens (CRT glass, scan lines, glowing figures); buttons in
// satin silver. Set on the generic controls only - a plugin's own panel inside
// a card keeps its look.
//==============================================================================
    namespace skeuo
    {
        const juce::Colour glow (0xff00c8ff);           // the bright saturated blue-cyan

        // relative luminance and the contrast of two colours (as WCAG has them)
        inline float luminance (juce::Colour c)
        {
            auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
            return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
        }
        inline float contrast (juce::Colour a, juce::Colour b)
        {
            const float la = luminance (a), lb = luminance (b);
            return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
        }
        // a sidebar bright enough that blue water on it would vanish (an azure or cyan one)
        inline bool brightGround (juce::Colour c)   { return luminance (c) > 0.15f; }
        const juce::Colour phosphor (0xff9cefff);       // figures on the screens
        const juce::Colour knobTop (0xffdfe3ea), knobMid (0xffb0b8c4), knobLow (0xff747d8a), knobBot (0xff5e6570),
                           knobRim (0xff3a3e4a), track (0xff3c4350);

        // the knobs' value ring: the colour panel's Knob Start into Knob End
        inline juce::Colour ringStart() { return themeColours().get (ThemeColours::KnobStart); }
        inline juce::Colour ringEnd()   { return themeColours().get (ThemeColours::KnobEnd); }
        inline juce::Colour ringAt (float t) { return ringStart().interpolatedWith (ringEnd(), t); }
        inline juce::Colour ringGlow() { return ringAt (0.6f).brighter (0.1f); }

        // a curve with VirtualDX7's glow: wide faint strokes under a crisp core
        inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float core)
        {
            for (auto [w, alpha] : { std::pair<float, float> { core + 7.0f, 0.05f }, { core + 4.5f, 0.09f }, { core + 2.0f, 0.18f } })
            {
                g.setColour (c.withAlpha (alpha));
                g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // a small CRT: dark blue glass, a rim, scan lines and a reflection
        inline void screen (juce::Graphics& g, juce::Rectangle<float> r)
        {
            const float rad = juce::jmin (4.0f, r.getHeight() * 0.3f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));   // the bevel's light lower edge
            g.fillRoundedRectangle (r.translated (0.0f, 1.0f), rad);
            g.setColour (juce::Colour (0xff05080d));                // the recessed rim
            g.fillRoundedRectangle (r, rad);
            const auto glass = r.reduced (1.2f);
            juce::ColourGradient tube (juce::Colour (0xff12304a), glass.getCentreX(), glass.getCentreY() - glass.getHeight() * 0.2f,
                                       juce::Colour (0xff050f1a), glass.getX(), glass.getBottom(), true);
            g.setGradientFill (tube);
            g.fillRoundedRectangle (glass, rad - 1.0f);
            g.setColour (glow.withAlpha (0.06f));                   // scan lines
            for (float yy = glass.getY() + 1.5f; yy < glass.getBottom() - 1.0f; yy += 2.0f)
                g.fillRect (glass.getX() + 1.5f, yy, glass.getWidth() - 3.0f, 0.7f);
            juce::ColourGradient reflect (juce::Colours::white.withAlpha (0.09f), glass.getX(), glass.getY(),
                                          juce::Colours::white.withAlpha (0.0f), glass.getX(), glass.getCentreY(), false);
            g.setGradientFill (reflect);
            g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.5f), rad - 1.0f);
        }

        // A bar of tinted glass with a bright upper half (the sidebar's section
        // headers): translucent, so it sits on any sidebar colour
        // (strength < 1: a quieter bar - everything fainter)
        inline void glassBar (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, float radius, float strength = 1.0f)
        {
            const float k = strength;
            g.setColour (juce::Colours::black.withAlpha (0.22f * k));                     // a soft shadow below
            g.fillRoundedRectangle (r.translated (0.0f, 1.5f), radius);
            juce::ColourGradient body (tint.withAlpha (0.34f * k), 0.0f, r.getY(), tint.withAlpha (0.14f * k), 0.0f, r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, radius);
            juce::ColourGradient lower (tint.withAlpha (0.0f), 0.0f, r.getCentreY(), tint.brighter (0.4f).withAlpha (0.32f * k), 0.0f, r.getBottom(), false);
            g.setGradientFill (lower);                                                    // light coming up through the glass
            g.fillRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
            const auto top = r.reduced (2.0f, 1.0f).withHeight (r.getHeight() * 0.5f);
            juce::ColourGradient shine (juce::Colours::white.withAlpha (0.42f * k), 0.0f, top.getY(),
                                        juce::Colours::white.withAlpha (0.05f * k), 0.0f, top.getBottom(), false);
            g.setGradientFill (shine);                                                    // the gloss
            g.fillRoundedRectangle (top, juce::jmax (1.0f, radius - 1.5f));
            g.setColour (tint.withAlpha (0.7f * (0.4f + 0.6f * k)));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.28f * k));
            g.drawRoundedRectangle (r.reduced (1.5f), juce::jmax (1.0f, radius - 1.0f), 0.7f);
        }

        // a lighter shading for the group names inside a section: tinted glass
        // fading out to the right, with a fine highlight along its top
        inline void glassStrip (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint)
        {
            const float rad = r.getHeight() * 0.35f;
            juce::ColourGradient fill (tint.withAlpha (0.30f), r.getX(), 0.0f, tint.withAlpha (0.0f), r.getRight(), 0.0f, false);
            fill.addColour (0.55, tint.withAlpha (0.14f));
            g.setGradientFill (fill);
            g.fillRoundedRectangle (r, rad);
            juce::ColourGradient shine (juce::Colours::white.withAlpha (0.30f), r.getX(), 0.0f, juce::Colours::white.withAlpha (0.0f), r.getRight() - r.getWidth() * 0.2f, 0.0f, false);
            g.setGradientFill (shine);
            g.fillRoundedRectangle (r.reduced (1.5f, 1.0f).withHeight (r.getHeight() * 0.42f), rad * 0.6f);
            juce::ColourGradient edge (tint.withAlpha (0.65f), r.getX(), 0.0f, tint.withAlpha (0.0f), r.getRight(), 0.0f, false);
            g.setGradientFill (edge);
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 0.8f);
        }

        // figures on a screen, glowing; squeezed to fit rather than cut short
        inline void phosphorText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float alpha)
        {
            g.setColour (glow.withAlpha (0.22f * alpha));
            for (auto d : { juce::Point<float> (-0.7f, 0.0f), { 0.7f, 0.0f }, { 0.0f, -0.7f }, { 0.0f, 0.7f } })
                g.drawFittedText (text, r.translated (d.x, d.y).toNearestInt(), j, 1, 0.55f);
            g.setColour (phosphor.withAlpha (alpha));
            g.drawFittedText (text, r.toNearestInt(), j, 1, 0.55f);
        }
    }

    class SkeuoLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        // the knob sits kKnobInset in from the slider's knob area (the
        // modulation wedges follow it); its track ring a little outside it
        static constexpr float kKnobInset = 5.0f;

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            using namespace skeuo;
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (kKnobInset);
            const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const float arcR = outer - 2.0f, radius = outer - 6.5f;
            const auto c = bounds.getCentre();
            const float angle = startAngle + pos * (endAngle - startAngle);
            const bool on = slider.isEnabled();

            // the track and the watery value arc, glowing, with a bright end
            juce::Path trackPath;
            trackPath.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (track.withAlpha (0.85f));
            g.strokePath (trackPath, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (on && pos > 0.002f)
            {
                juce::Path value;
                value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
                glowStroke (g, value, ringGlow(), 3.0f);
                juce::ColourGradient water (ringStart(), c.x - arcR, c.y, ringEnd(), c.x + arcR, c.y, false);
                water.addColour (0.4, ringAt (0.4f));
                water.addColour (0.75, ringAt (0.75f));
                g.setGradientFill (water);
                g.strokePath (value, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const float dx = c.x + std::sin (angle) * arcR, dy = c.y - std::cos (angle) * arcR;
                g.setColour (ringGlow().withAlpha (0.3f));
                g.fillEllipse (dx - 5.0f, dy - 5.0f, 10.0f, 10.0f);
                g.setColour (ringEnd().brighter (0.25f));
                g.fillEllipse (dx - 2.4f, dy - 2.4f, 4.8f, 4.8f);
            }

            // the knob: a satin-silver cap, lit from the top left, a dark rim
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillEllipse (c.x - radius + 1.2f, c.y - radius + 2.2f, radius * 2.0f, radius * 2.0f);
            juce::ColourGradient body (knobTop, c.x - radius * 0.4f, c.y - radius * 0.5f, knobBot, c.x + radius * 0.5f, c.y + radius * 0.6f, false);
            body.addColour (0.45, knobMid);
            body.addColour (0.70, knobLow);
            g.setGradientFill (body);
            g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.2f));
            g.fillEllipse (c.x - radius * 0.7f, c.y - radius * 0.75f, radius * 0.9f, radius * 0.65f);
            g.setColour (knobRim);
            g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.3f);

            // the line, and the centre dot
            const float sx = std::sin (angle), sy = -std::cos (angle);
            g.setColour (juce::Colours::white.withAlpha (on ? 0.92f : 0.5f));
            g.drawLine (c.x + sx * radius * 0.25f, c.y + sy * radius * 0.25f, c.x + sx * radius * 0.8f, c.y + sy * radius * 0.8f, 2.0f);
            g.setColour (on ? ringAt (0.35f) : knobLow);
            g.fillEllipse (c.x - 2.2f, c.y - 2.2f, 4.4f, 4.4f);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                               float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            using namespace skeuo;
            if (style != juce::Slider::LinearHorizontal)
            {
                LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
                return;
            }
            const float cy = (float) y + (float) height * 0.5f;
            const auto groove = juce::Rectangle<float> ((float) x, cy - 3.0f, (float) width, 6.0f);
            g.setColour (track.withAlpha (0.85f));
            g.fillRoundedRectangle (groove, 3.0f);
            if (slider.isEnabled() && sliderPos > groove.getX() + 1.0f)
            {
                juce::Path fill;
                fill.startNewSubPath (groove.getX() + 3.0f, cy);
                fill.lineTo (juce::jmax (groove.getX() + 3.0f, sliderPos), cy);
                glowStroke (g, fill, ringGlow(), 3.0f);
                juce::ColourGradient water (ringStart(), groove.getX(), cy, ringEnd(), groove.getRight(), cy, false);
                water.addColour (0.5, ringAt (0.5f));
                g.setGradientFill (water);
                g.strokePath (fill, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            // the handle: a small satin-silver cap
            const auto thumb = juce::Rectangle<float> (sliderPos - 5.0f, cy - 8.0f, 10.0f, 16.0f);
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (thumb.translated (0.5f, 1.5f), 3.0f);
            juce::ColourGradient cap (knobTop, thumb.getX(), thumb.getY(), knobBot, thumb.getX(), thumb.getBottom(), false);
            cap.addColour (0.5, knobMid);
            g.setGradientFill (cap);
            g.fillRoundedRectangle (thumb, 3.0f);
            g.setColour (knobRim);
            g.drawRoundedRectangle (thumb, 3.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (thumb.getCentreX() - 0.75f, thumb.getY() + 4.0f, 1.5f, thumb.getHeight() - 8.0f);
        }

        juce::Font screenFont (float h) const
        {
            return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold));
        }

        void drawLabel (juce::Graphics& g, juce::Label& label) override
        {
            auto* parent = label.getParentComponent();
            const bool inSlider = dynamic_cast<juce::Slider*> (parent) != nullptr;
            const bool inCombo = dynamic_cast<juce::ComboBox*> (parent) != nullptr;
            if (! inSlider && ! inCombo)
            {
                LookAndFeel_V4::drawLabel (g, label);
                return;
            }
            if (inSlider)
                skeuo::screen (g, label.getLocalBounds().toFloat().reduced (1.0f, 0.5f));
            if (label.isBeingEdited())
                return;
            g.setFont (screenFont (juce::jlimit (9.5f, 12.5f, (float) label.getHeight() * 0.7f)));
            skeuo::phosphorText (g, label.getText(), label.getLocalBounds().toFloat().reduced (2.0f, 0.0f),
                                 inCombo ? juce::Justification::centredLeft : juce::Justification::centred,
                                 label.isEnabled() ? 1.0f : 0.5f);
        }

        juce::Label* createSliderTextBox (juce::Slider& slider) override
        {
            auto* l = LookAndFeel_V4::createSliderTextBox (slider);
            l->setColour (juce::Label::textColourId, skeuo::phosphor);
            l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
            l->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff07121d));
            l->setColour (juce::TextEditor::textColourId, skeuo::phosphor);
            l->setColour (juce::TextEditor::highlightColourId, skeuo::glow.withAlpha (0.35f));
            l->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            l->setColour (juce::TextEditor::focusedOutlineColourId, skeuo::glow.withAlpha (0.6f));
            l->setColour (juce::CaretComponent::caretColourId, skeuo::phosphor);
            return l;
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (1.0f, 1.5f);
            skeuo::screen (g, r);
            const float ax = r.getRight() - 9.0f, ay = r.getCentreY();
            juce::Path arrow;
            arrow.addTriangle (ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.0f);
            g.setColour (skeuo::glow.withAlpha (box.isEnabled() ? 0.9f : 0.4f));
            g.fillPath (arrow);
        }

        juce::Font getComboBoxFont (juce::ComboBox& box) override
        {
            return screenFont (juce::jlimit (10.0f, 13.5f, (float) box.getHeight() * 0.55f));
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (2, 1, juce::jmax (8, box.getWidth() - 18), box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
        }

        // buttons in satin silver (on: a cyan wash)
        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                   bool highlighted, bool down) override
        {
            auto r = button.getLocalBounds().toFloat().reduced (1.0f, 1.5f);
            const float corner = juce::jmin (4.0f, r.getHeight() * 0.3f);
            const bool on = button.getToggleState();
            auto top = on ? juce::Colour (0xffbaf0ff) : juce::Colour (0xffe1e5ec);
            auto bottom = on ? juce::Colour (0xff2ab8f5) : juce::Colour (0xffb9bfca);
            if (highlighted) { top = top.brighter (0.05f); bottom = bottom.brighter (0.05f); }
            if (down) std::swap (top, bottom);
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
            juce::ColourGradient satin (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false);
            g.setGradientFill (satin);
            g.fillRoundedRectangle (r, corner);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
            g.setColour (juce::Colour (0xff5a6372).withAlpha (button.isEnabled() ? 0.9f : 0.5f));
            g.drawRoundedRectangle (r, corner, 1.0f);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            g.setFont (getTextButtonFont (button, button.getHeight()));
            g.setColour (juce::Colour (0xff1e2230).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
            g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
        }
    };

    inline SkeuoLookAndFeel& skeuoLookAndFeel()
    {
        return aquanode::shutdownSafe<SkeuoLookAndFeel>();
    }

    // The top bar's buttons in Skeuomorphic: glossy pills in the button's own
    // colour - lighter at the top, a bright gloss over the upper half and a
    // little light coming up through the lower edge
    class AeroButtonLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                   bool highlighted, bool down) override
        {
            auto r = button.getLocalBounds().toFloat().reduced (1.0f, 1.5f);
            const float corner = juce::jmin (6.0f, r.getHeight() * 0.42f);
            auto base = background.withAlpha (1.0f);
            if (! button.isEnabled()) base = base.withMultipliedSaturation (0.4f);
            if (highlighted) base = base.brighter (0.12f);
            if (down)        base = base.darker (0.18f);
            const float alpha = button.isEnabled() ? 0.92f : 0.55f;

            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.2f), corner);
            juce::ColourGradient body (base.brighter (0.30f).withAlpha (alpha), 0.0f, r.getY(),
                                       base.darker (0.30f).withAlpha (alpha), 0.0f, r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, corner);
            juce::ColourGradient lower (base.withAlpha (0.0f), 0.0f, r.getCentreY(),
                                        base.brighter (0.7f).withAlpha (0.45f), 0.0f, r.getBottom(), false);
            g.setGradientFill (lower);
            g.fillRoundedRectangle (r.reduced (1.0f), corner - 1.0f);
            if (! down)
            {
                const auto top = r.reduced (2.0f, 1.0f).withHeight (r.getHeight() * 0.48f);
                juce::ColourGradient shine (juce::Colours::white.withAlpha (0.55f), 0.0f, top.getY(),
                                            juce::Colours::white.withAlpha (0.08f), 0.0f, top.getBottom(), false);
                g.setGradientFill (shine);
                g.fillRoundedRectangle (top, juce::jmax (1.0f, corner - 1.5f));
            }
            g.setColour (base.darker (0.7f).withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawRoundedRectangle (r.reduced (1.5f), juce::jmax (1.0f, corner - 1.0f), 0.7f);
        }
    };

    inline AeroButtonLookAndFeel& aeroButtonLookAndFeel()
    {
        return aquanode::shutdownSafe<AeroButtonLookAndFeel>();
    }

    //==========================================================================
    // The Glass look, after the Soundscape Ecology plugin: a saturated jungle
    // gradient (aqua sky, lagoon, canopy, deep jade) with soft light blooms and
    // a few pale leaves under everything; panels of frosted glass (white at
    // 16 %, a sheen from the top, a pale edge); knobs with a dark track, a
    // luminous arc and a glass cap with a white pointer; white text.
    //==========================================================================
    // (6.9.16) CPU > Disable GUI Transparency: the Glass and Skeuomorphic cards are painted
    // solid and the patch area is not painted under them, so a card's display, knob or
    // meter repaints the card alone (the app's settings: "gui.opaque")
    inline bool& guiOpaque() { static bool on = false; return on; }

    namespace glass
    {
        const juce::Colour canopy (0xff2fd17a), moss (0xff118f56), deepJade (0xff064f3d), loam (0xff123a30),
                           water (0xff17b8d4), sky (0xff5fe3e0), lightWarm (0xffa8f05a);
        inline juce::Colour text()    { return juce::Colours::white.withAlpha (0.94f); }
        inline juce::Colour textDim() { return juce::Colours::white.withAlpha (0.66f); }

        inline bool on() { return themeColours().glass; }

        // the jungle, drawn once per size (it is still: the patch moves over it)
        inline juce::Colour pal (int slot) { return themeColours().glassCol[slot]; }

        // the jungle (from the Glass palette: the gradient's top and bottom, the
        // middle from Filters, Sound Generation and Effects, blooms in those and
        // Input / Output)
        inline void paintJungle (juce::Graphics& g, juce::Rectangle<float> r)
        {
            using T = ThemeColours;
            const auto top      = pal (T::Background);
            const auto upperMid = pal (T::Filter).interpolatedWith (pal (T::Oscillator), 0.55f).darker (0.38f);
            const auto middle   = pal (T::Oscillator).interpolatedWith (pal (T::Effect), 0.2f).darker (0.52f);
            const auto bottom   = pal (T::Sidebar);
            juce::ColourGradient grad (top, r.getX() + r.getWidth() * 0.15f, r.getY(),
                                       bottom, r.getX() + r.getWidth() * 0.85f, r.getBottom(), false);
            grad.addColour (0.26, upperMid);
            grad.addColour (0.52, middle);
            grad.addColour (0.78, middle.interpolatedWith (bottom, 0.60f));
            g.setGradientFill (grad);
            g.fillRect (r);

            const float w = juce::jmax (r.getWidth(), r.getHeight() * 0.8f);
            auto bloom = [&] (float cx, float cy, float rad, juce::Colour c, float alpha)
            {
                const juce::Point<float> centre (r.getX() + r.getWidth() * cx, r.getY() + r.getHeight() * cy);
                juce::ColourGradient gg (c.withAlpha (alpha), centre.x, centre.y, c.withAlpha (0.0f), centre.x + rad, centre.y, true);
                g.setGradientFill (gg);
                g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (centre));
            };
            bloom (0.20f, 0.12f, w * 0.34f, pal (T::Effect),      0.16f);
            bloom (0.82f, 0.30f, w * 0.30f, pal (T::InputOutput), 0.14f);
            bloom (0.55f, 0.92f, w * 0.38f, pal (T::Oscillator),  0.20f);
            bloom (0.08f, 0.62f, w * 0.26f, pal (T::Filter),      0.12f);
        }

        // what the jungle is drawn from (a new palette draws it anew)
        inline juce::uint64 paletteKey()
        {
            juce::uint64 k = 1469598103934665603ull;
            for (int i = 0; i < ThemeColours::NumSlots; ++i)
                k = (k ^ themeColours().glassCol[i].getARGB()) * 1099511628211ull;
            return k;
        }

        // a cached jungle of a size (each user keeps its own)
        // the jungle, drawn once for the whole window: `area` is the window's
        // rectangle in the caller's own coordinates, so a part of the window
        // (the patch area) draws its piece of the one picture - and a patch
        // area that changes size (the keyboard moved) changes nothing in it
        struct Backdrop
        {
            juce::Image image;
            juce::uint64 key { 0 };
            void draw (juce::Graphics& g, juce::Rectangle<int> area)
            {
                if (area.isEmpty())
                    return;
                const auto k = paletteKey();
                if (! image.isValid() || image.getWidth() != area.getWidth() || image.getHeight() != area.getHeight() || k != key)
                {
                    image = juce::Image (juce::Image::RGB, area.getWidth(), area.getHeight(), false);
                    juce::Graphics ig (image);
                    paintJungle (ig, image.getBounds().toFloat());
                    key = k;
                }
                g.drawImageAt (image, area.getX(), area.getY());
            }
        };
        // one for the window and the patch area - let go when JUCE shuts down,
        // never at the DLL's unloading (its image is a Direct2D bitmap on Windows)
        inline Backdrop& backdrop() { return aquanode::shutdownSafe<Backdrop>(); }

        // a leaf: two curved sides meeting in a point, a short stem, a midrib
        // (size = its length; drawn pointing up, around its middle)
        inline juce::Path leafShape (float size)
        {
            const float h = size * 0.5f, w = size * 0.27f;
            juce::Path p;
            p.startNewSubPath (0.0f, h * 0.72f);                                   // the base
            p.cubicTo (w * 1.25f, h * 0.45f, w * 1.05f, -h * 0.55f, 0.0f, -h);     // right side up to the tip
            p.cubicTo (-w * 1.05f, -h * 0.55f, -w * 1.25f, h * 0.45f, 0.0f, h * 0.72f);
            p.closeSubPath();
            return p;
        }
        inline juce::Path leafVeins (float size)
        {
            const float h = size * 0.5f, w = size * 0.27f;
            juce::Path v;
            v.startNewSubPath (0.0f, h);                  // the stem, through the midrib
            v.quadraticTo (w * 0.12f, 0.0f, 0.0f, -h * 0.82f);
            for (float t : { -0.3f, 0.05f, 0.38f })      // three pairs of side veins
            {
                v.startNewSubPath (0.0f, h * (t + 0.18f));
                v.lineTo (w * 0.62f, h * (t - 0.12f));
                v.startNewSubPath (0.0f, h * (t + 0.18f));
                v.lineTo (-w * 0.62f, h * (t - 0.12f));
            }
            return v;
        }

        // the drifting leaves over the patch area's jungle: they fall slowly,
        // swaying and turning, and come in again at the top (only their own
        // little boxes are redrawn as they move)
        struct Leaves
        {
            struct Leaf { float x, y, rot, spin, speed, size, alpha, phase; };
            std::vector<Leaf> leaves;
            juce::Rectangle<float> area;
            juce::Random rng { 0x1eaf };

            // (a new area - a rotated or resized window - keeps every leaf where
            // it was, in proportion; only the very first layout scatters them)
            void layout (juce::Rectangle<float> r)
            {
                if (r == area && ! leaves.empty())
                    return;
                const auto old = area;
                area = r;
                const int n = juce::jlimit (6, 26, (int) (r.getWidth() * r.getHeight() / 60000.0f));
                if (! leaves.empty() && old.getWidth() > 0.0f && old.getHeight() > 0.0f)
                    for (auto& l : leaves)
                    {
                        l.x = r.getX() + (l.x - old.getX()) * r.getWidth() / old.getWidth();
                        l.y = r.getY() + (l.y - old.getY()) * r.getHeight() / old.getHeight();
                    }
                const auto had = leaves.size();
                leaves.resize ((size_t) n);
                for (size_t i = (old.isEmpty() ? 0 : had); i < leaves.size(); ++i)
                    spawn (leaves[i], rng.nextFloat() * r.getHeight());
            }

            void spawn (Leaf& l, float y)
            {
                l.x = area.getX() + rng.nextFloat() * area.getWidth();
                l.y = area.getY() + y;
                l.rot = rng.nextFloat() * juce::MathConstants<float>::twoPi;
                l.spin = (rng.nextFloat() - 0.5f) * 0.5f;          // radians a second
                l.speed = 5.0f + rng.nextFloat() * 9.0f;            // pixels a second: slow
                l.size = 12.0f + rng.nextFloat() * 16.0f;
                l.alpha = 0.07f + rng.nextFloat() * 0.09f;
                l.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            }

            juce::Point<float> position (const Leaf& l, double t) const
            {
                return { l.x + 10.0f * std::sin ((float) t * 0.35f + l.phase), l.y };
            }

            juce::Rectangle<int> boxOf (const Leaf& l, double t) const
            {
                return juce::Rectangle<float> (l.size * 1.2f, l.size * 1.2f).withCentre (position (l, t)).expanded (2.0f).getSmallestIntegerContainer();
            }

            // moves them on by dt seconds; the boxes to redraw go to `dirty`
            template <typename Fn>
            void step (double t, float dt, Fn&& dirty)
            {
                for (auto& l : leaves)
                {
                    const auto before = boxOf (l, t - dt);
                    l.y += l.speed * dt;
                    l.rot += l.spin * dt;
                    if (l.y - l.size > area.getBottom())
                        spawn (l, -l.size);
                    dirty (before.getUnion (boxOf (l, t)));
                }
            }

            void paint (juce::Graphics& g, double t) const
            {
                const auto clip = g.getClipBounds();
                const auto c = pal (ThemeColours::KnobStart);
                for (const auto& l : leaves)
                {
                    if (! boxOf (l, t).intersects (clip))
                        continue;
                    const float sway = 0.35f * std::sin ((float) t * 0.35f + l.phase + 1.2f);
                    const auto tr = juce::AffineTransform::rotation (l.rot + sway).translated (position (l, t));
                    g.setColour (c.withAlpha (l.alpha));
                    g.fillPath (leafShape (l.size), tr);
                    g.setColour (c.withAlpha (l.alpha * 0.9f));
                    g.strokePath (leafVeins (l.size), juce::PathStrokeType (0.7f), tr);
                }
            }
        };

        // a pane of frosted glass: a translucent body (tinted a touch), a sheen
        // from the top, a pale edge (in the palette's Glass Sheen)
        inline void panel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, float corner, bool solid = false)
        {
            const auto sheenC = pal (ThemeColours::KnobEnd);
            g.setColour (juce::Colours::black.withAlpha (0.16f));                         // a soft shadow below
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f), corner);
            // frosted (solid: Disable GUI Transparency - the jungle's dark green behind it, as it shows through)
            const auto frost = pal (ThemeColours::Sidebar).darker (0.1f).interpolatedWith (tint, 0.18f);
            g.setColour (solid ? frost.interpolatedWith (juce::Colour (0xff0e2a22), 0.34f) : frost.withAlpha (0.66f));
            g.fillRoundedRectangle (r, corner);
            g.setColour (sheenC.withAlpha (0.10f));
            g.fillRoundedRectangle (r, corner);
            juce::ColourGradient sheen (sheenC.withAlpha (0.16f), 0.0f, r.getY(),
                                        sheenC.withAlpha (0.02f), 0.0f, r.getBottom(), false);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (r, corner);
            g.setColour (sheenC.withAlpha (0.30f));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        }

        // the leaf mark (before a title): a leaf leaning to the right, with its
        // stem and veins cut out of it
        inline void leafMark (juce::Graphics& g, juce::Rectangle<float> mark, juce::Colour c)
        {
            const float size = juce::jmin (mark.getHeight() * 1.05f, mark.getWidth() * 1.3f);
            const auto tr = juce::AffineTransform::rotation (0.75f).translated (mark.getCentre());
            g.setColour (c.withAlpha (0.95f));
            g.fillPath (leafShape (size), tr);
            g.setColour (c.darker (0.55f).withAlpha (0.85f));
            g.strokePath (leafVeins (size), juce::PathStrokeType (juce::jmax (0.8f, size * 0.06f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tr);
        }
    }

    class GlassLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        static constexpr float kKnobInset = 5.0f;   // as the Skeuomorphic knobs (the modulation wedges follow)

        GlassLookAndFeel()
        {
            using namespace glass;
            setColour (juce::Label::textColourId, text());
            setColour (juce::ComboBox::textColourId, text());
            setColour (juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha (0.12f));
            setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.25f));
            setColour (juce::ComboBox::arrowColourId, textDim());
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf0123a30));
            setColour (juce::PopupMenu::textColourId, text());
            setColour (juce::PopupMenu::highlightedBackgroundColourId, canopy.withAlpha (0.55f));
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::Slider::textBoxTextColourId, text());
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::TextButton::textColourOffId, text());
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            setColour (juce::ToggleButton::textColourId, text());
        }

        // the arc in the card's own colour, a little lit (Soundscape's accent)
        static juce::Colour accentOf (juce::Slider& s)
        {
            return s.findColour (juce::Slider::rotarySliderFillColourId);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& s) override
        {
            // (round, however the slider's box is shaped)
            const auto box = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (kKnobInset);
            const float side = juce::jmin (box.getWidth(), box.getHeight());
            const auto bounds = box.withSizeKeepingCentre (side, side);
            const float radius = side * 0.5f;
            const auto centre = bounds.getCentre();
            const float angle = startAngle + pos * (endAngle - startAngle);
            const float arcR = radius - 1.5f;

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (juce::Colours::black.withAlpha (0.26f));
            g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (pos > 0.002f)
            {
                juce::Path value;
                value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
                const auto glow = s.isEnabled() ? accentOf (s).brighter (0.35f) : juce::Colours::grey;
                g.setColour (glow.withAlpha (0.22f));   // a soft halo under it
                g.strokePath (value, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (glow.withAlpha (0.95f));
                g.strokePath (value, juce::PathStrokeType (2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // the glass cap
            const auto cap = bounds.reduced (radius * 0.24f);
            juce::ColourGradient body (juce::Colours::white.withAlpha (0.30f), cap.getX(), cap.getY(),
                                       juce::Colours::black.withAlpha (0.30f), cap.getX(), cap.getBottom(), false);
            g.setGradientFill (body);
            g.fillEllipse (cap);
            g.setColour (juce::Colours::white.withAlpha (0.32f));
            g.drawEllipse (cap.reduced (0.5f), 1.0f);

            juce::Path pointer;
            const float pw = 2.0f;
            pointer.addRoundedRectangle (-pw * 0.5f, -radius * 0.72f, pw, radius * 0.42f, pw * 0.5f);
            g.setColour (juce::Colours::white.withAlpha (s.isEnabled() ? 0.92f : 0.5f));
            g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s) override
        {
            if (style != juce::Slider::LinearHorizontal)
            {
                LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, minPos, maxPos, style, s);
                return;
            }
            const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
            const float barH = juce::jmin (7.0f, r.getHeight());
            const auto track = r.withSizeKeepingCentre (r.getWidth(), barH);
            g.setColour (juce::Colours::black.withAlpha (0.26f));
            g.fillRoundedRectangle (track, barH * 0.5f);
            const auto filled = track.withWidth (juce::jmax (barH, pos - track.getX()));
            const auto glow = s.isEnabled() ? accentOf (s).brighter (0.30f) : juce::Colours::grey;
            g.setColour (glow.withAlpha (0.92f));
            g.fillRoundedRectangle (filled, barH * 0.5f);
            const float hw = 4.0f;
            const auto handle = juce::Rectangle<float> (hw, juce::jmin (r.getHeight() * 0.82f, 18.0f))
                                    .withCentre ({ juce::jlimit (track.getX() + hw, track.getRight() - hw, pos), track.getCentreY() });
            g.setColour (juce::Colours::white.withAlpha (0.94f));
            g.fillRoundedRectangle (handle, hw * 0.5f);
        }

        juce::Label* createSliderTextBox (juce::Slider& slider) override
        {
            auto* l = LookAndFeel_V4::createSliderTextBox (slider);
            l->setColour (juce::Label::textColourId, glass::text());
            l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
            l->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xe0123a30));
            l->setColour (juce::TextEditor::textColourId, juce::Colours::white);
            l->setColour (juce::TextEditor::highlightColourId, glass::canopy.withAlpha (0.4f));
            l->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            l->setColour (juce::TextEditor::focusedOutlineColourId, glass::sky.withAlpha (0.6f));
            l->setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
            return l;
        }

        void drawLabel (juce::Graphics& g, juce::Label& label) override
        {
            // a value under a knob: on a small pane of darker glass
            if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
            {
                const auto r = label.getLocalBounds().toFloat().reduced (1.0f, 0.5f);
                const float rad = juce::jmin (6.0f, r.getHeight() * 0.5f);
                g.setColour (juce::Colours::black.withAlpha (0.20f));
                g.fillRoundedRectangle (r, rad);
                g.setColour (juce::Colours::white.withAlpha (0.14f));
                g.drawRoundedRectangle (r.reduced (0.5f), rad, 0.8f);
                if (label.isBeingEdited())
                    return;
                g.setFont (juce::Font (juce::FontOptions (juce::jlimit (9.5f, 12.5f, (float) label.getHeight() * 0.68f))));
                g.setColour (glass::text().withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
                g.drawFittedText (label.getText(), label.getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.6f);
                return;
            }
            LookAndFeel_V4::drawLabel (g, label);
        }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f, 1.0f);
            const float rad = juce::jmin (8.0f, r.getHeight() * 0.45f);
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.fillRoundedRectangle (r, rad);
            g.setColour (juce::Colours::white.withAlpha (0.26f));
            g.drawRoundedRectangle (r, rad, 1.0f);
            juce::Path arrow;
            const float cx = (float) w - 10.0f, cy = (float) h * 0.5f;
            arrow.startNewSubPath (cx - 3.5f, cy - 1.8f);
            arrow.lineTo (cx, cy + 2.5f);
            arrow.lineTo (cx + 3.5f, cy - 1.8f);
            g.setColour (glass::textDim().withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
            g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (3, 1, juce::jmax (8, box.getWidth() - 18), box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
        }

        // buttons as panes of glass (on: washed in the accent)
        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                   bool highlighted, bool down) override
        {
            const auto r = button.getLocalBounds().toFloat().reduced (1.0f, 1.5f);
            const float corner = juce::jmin (8.0f, r.getHeight() * 0.45f);
            const bool on = button.getToggleState();
            const auto accent = on ? button.findColour (juce::TextButton::buttonOnColourId) : background;
            g.setColour (on ? accent.withAlpha (0.62f) : juce::Colours::white.withAlpha (highlighted ? 0.20f : 0.13f));
            g.fillRoundedRectangle (r, corner);
            if (! down)
            {
                juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.18f), 0.0f, r.getY(),
                                            juce::Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY(), false);
                g.setGradientFill (sheen);
                g.fillRoundedRectangle (r, corner);
            }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (0.15f));
                g.fillRoundedRectangle (r, corner);
            }
            g.setColour (juce::Colours::white.withAlpha ((highlighted ? 0.45f : 0.28f) * (button.isEnabled() ? 1.0f : 0.5f)));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            g.setFont (getTextButtonFont (button, button.getHeight()));
            g.setColour (glass::text().withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
            g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            auto pill = r.removeFromLeft (juce::jmin (34.0f, r.getWidth())).withSizeKeepingCentre (30.0f, 16.0f);
            g.setColour (b.getToggleState() ? glass::pal (ThemeColours::Oscillator).withAlpha (0.75f) : juce::Colours::white.withAlpha (0.16f));
            g.fillRoundedRectangle (pill, 8.0f);
            g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.45f : 0.28f));
            g.drawRoundedRectangle (pill, 8.0f, 1.0f);
            const auto dot = juce::Rectangle<float> (13.0f, 13.0f)
                                 .withCentre ({ b.getToggleState() ? pill.getRight() - 8.5f : pill.getX() + 8.5f, pill.getCentreY() });
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.fillEllipse (dot);
            if (b.getButtonText().isNotEmpty())
            {
                g.setColour (glass::textDim());
                g.setFont (juce::Font (juce::FontOptions (12.0f)));
                g.drawText (b.getButtonText(), r.translated (4.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }
    };

    inline GlassLookAndFeel& glassLookAndFeel()
    {
        return aquanode::shutdownSafe<GlassLookAndFeel>();
    }

    // Aquanode's mark: a water ring - a thick ring of water, deep blue to cyan,
    // closed but for a small gap at the top right, glowing. Drawn as shapes, so
    // no font is needed. (tools/assets/make_app_icon.py draws the same ring as the
    // app's icon.)
    inline void drawWaterRing (juce::Graphics& g, juce::Rectangle<float> area)
    {
        // an almost closed ring of water, open a little at the top right
        const float side = juce::jmin (area.getWidth(), area.getHeight());
        const float r = side * 0.31f, t = side * 0.15f;
        const auto c = area.getCentre();
        const float gapFrom = 0.22f, gapTo = 0.86f;   // (0 = twelve o'clock, clockwise)
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, r, r, 0.0f, gapTo, juce::MathConstants<float>::twoPi + gapFrom, true);
        juce::Path ringShape;
        juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (ringShape, ring);

        // the glow, then the water, then the gloss
        for (auto [grow, alpha] : { std::pair<float, float> { 4.0f, 0.07f }, { 2.4f, 0.11f }, { 1.2f, 0.17f } })
        {
            g.setColour (skeuo::glow.withAlpha (alpha));
            g.strokePath (ringShape, juce::PathStrokeType (grow * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::ColourGradient water (juce::Colour (0xff0038e0), c.x - r, c.y + r, juce::Colour (0xff1ff4ff), c.x + r, c.y - r, false);
        water.addColour (0.55, juce::Colour (0xff0a9cff));
        g.setGradientFill (water);
        g.fillPath (ringShape);
        juce::Path shine;   // a soft highlight along the upper left of the tube
        shine.addCentredArc (c.x, c.y, r + t * 0.12f, r + t * 0.12f, 0.0f, -2.2f, -0.35f, true);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.strokePath (shine, juce::PathStrokeType (t * 0.22f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.18f));   // and a faint inner one
        juce::Path inner;
        inner.addCentredArc (c.x, c.y, r - t * 0.2f, r - t * 0.2f, 0.0f, 2.3f, 3.6f, true);
        g.strokePath (inner, juce::PathStrokeType (t * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

//==============================================================================
// The app's settings file (colours, the keys' zoom, the MPE switch ...)
namespace
{
    inline juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Aquanode Modular";
        o.filenameSuffix = ".settings";
        o.folderName = "Aquanode Modular";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return o;
    }
}
