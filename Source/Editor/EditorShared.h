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
        inline void glassBar (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, float radius)
        {
            g.setColour (juce::Colours::black.withAlpha (0.22f));                         // a soft shadow below
            g.fillRoundedRectangle (r.translated (0.0f, 1.5f), radius);
            juce::ColourGradient body (tint.withAlpha (0.34f), 0.0f, r.getY(), tint.withAlpha (0.14f), 0.0f, r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, radius);
            juce::ColourGradient lower (tint.withAlpha (0.0f), 0.0f, r.getCentreY(), tint.brighter (0.4f).withAlpha (0.32f), 0.0f, r.getBottom(), false);
            g.setGradientFill (lower);                                                    // light coming up through the glass
            g.fillRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
            const auto top = r.reduced (2.0f, 1.0f).withHeight (r.getHeight() * 0.5f);
            juce::ColourGradient shine (juce::Colours::white.withAlpha (0.42f), 0.0f, top.getY(),
                                        juce::Colours::white.withAlpha (0.05f), 0.0f, top.getBottom(), false);
            g.setGradientFill (shine);                                                    // the gloss
            g.fillRoundedRectangle (top, juce::jmax (1.0f, radius - 1.5f));
            g.setColour (tint.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.28f));
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
        static SkeuoLookAndFeel laf;
        return laf;
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
        static AeroButtonLookAndFeel laf;
        return laf;
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
