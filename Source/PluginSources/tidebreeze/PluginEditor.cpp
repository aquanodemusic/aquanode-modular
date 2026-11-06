#include "PluginEditor.h"

using namespace tb;
using namespace tbui;

//==============================================================================
// phosphor text
//==============================================================================
void tbui::glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, float intensity)
{
    g.setColour (c.withAlpha (0.10f * intensity));
    for (auto o : { juce::Point<float> (-1.2f, 0.0f), { 1.2f, 0.0f }, { 0.0f, -1.2f }, { 0.0f, 1.2f } })
        g.drawText (t, r.translated (o.x, o.y), j, false);
    g.setColour (c.withAlpha (0.18f * intensity));
    g.drawText (t, r.translated (0.5f, 0.0f), j, false);
    g.setColour (c.withMultipliedAlpha (intensity));
    g.drawText (t, r, j, false);
}

//==============================================================================
// look and feel: brushed silver keys on the frame, phosphor on the glass
//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff083f50));
    setColour (juce::PopupMenu::textColourId, juce::Colour (0xffd8f8ff));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::cyanDeep().withAlpha (0.6f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, Palette::cyan());
    setColour (juce::ComboBox::textColourId, Palette::phBright());
    setColour (juce::ComboBox::arrowColourId, Palette::phMid());
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, Palette::phBright());
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, Palette::phBright());
    setColour (juce::Label::textColourId, Palette::phBright());
    setColour (juce::ScrollBar::thumbColourId, Palette::phDim().withAlpha (0.5f));
}

juce::Font LookAndFeel::font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font LookAndFeel::crt (float size, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return crt (11.5f, true); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Palette::inset());
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Palette::phDim().withAlpha (0.18f));
    g.drawRoundedRectangle (r.expanded (0.8f), 4.5f, 2.0f);
    g.setColour (Palette::phDim().withAlpha (0.7f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path tri;
    const float cx = (float) w - 10.0f, cy = (float) h * 0.5f;
    tri.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (Palette::phMid());
    g.fillPath (tri);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const bool onFrame = s.getProperties().getWithDefault ("frame", false);
    const float d = (float) std::min (w, h) - 4.0f;
    const auto c = juce::Point<float> ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = d * 0.5f;
    const float br = r * 0.76f;
    // the knob: a cyan glass bead in a dark ring, lit from above
    g.setColour (juce::Colour (0x55000000));
    g.fillEllipse (c.x - br, c.y - br + 1.5f, br * 2.0f, br * 2.0f);
    juce::ColourGradient body (juce::Colour (0xff62d9ef), c.x - br * 0.4f, c.y - br, juce::Colour (0xff0a4b60), c.x + br * 0.3f, c.y + br, false);
    body.addColour (0.5, juce::Colour (0xff1a93b2));
    g.setGradientFill (body);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colour (0xff032d39));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.2f);
    juce::ColourGradient spec (juce::Colours::white.withAlpha (0.45f), c.x, c.y - br, juce::Colours::transparentWhite, c.x, c.y, false);
    g.setGradientFill (spec);
    g.fillEllipse (c.x - br * 0.62f, c.y - br * 0.92f, br * 1.24f, br * 0.8f);
    // the track and the value arc (bipolar knobs fill from the centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float ang = a0 + pos * (a1 - a0);
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    juce::Path track, arc;
    track.addCentredArc (c.x, c.y, r * 0.97f, r * 0.97f, 0.0f, a0, a1, true);
    arc.addCentredArc (c.x, c.y, r * 0.97f, r * 0.97f, 0.0f, std::min (from, ang), std::max (from, ang), true);
    if (onFrame)
    {
        g.setColour (juce::Colour (0xff7d939c));
        g.strokePath (track, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::cyanDeep());
        g.strokePath (arc, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour (Palette::phDim().withAlpha (0.22f));
        g.strokePath (track, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::phMid().withAlpha (0.22f));
        g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::phBright());
        g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.3f, -br * 0.92f, 2.6f, br * 0.5f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const bool onFrame = b.getProperties().getWithDefault ("frame", false);
    if (onFrame)
    {
        // a hardware key on the silver frame; a lit key glows cyan
        g.setColour (juce::Colour (0x40000000));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeef3f5), 0, r.getY(), juce::Colour (0xffaebdc4), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (0xff6f8590).darker (over ? 0.3f : 0.0f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 4.0f, r.getRight() - 4.0f);
        if (on || down)
        {
            auto led = r.withHeight (3.0f).withY (r.getBottom() - 5.0f).reduced (8.0f, 0);
            g.setColour (Palette::cyan().withAlpha (0.35f));
            g.fillRoundedRectangle (led.expanded (2.0f), 2.5f);
            g.setColour (Palette::cyan());
            g.fillRoundedRectangle (led, 1.5f);
        }
        return;
    }
    // a glass key on the screen
    g.setColour (on ? Palette::phMid().withAlpha (0.28f) : Palette::inset().withMultipliedAlpha (over ? 1.25f : 1.0f));
    g.fillRoundedRectangle (r, 4.0f);
    if (on)
    {
        g.setColour (Palette::phBright().withAlpha (0.25f));
        g.drawRoundedRectangle (r.expanded (1.0f), 5.0f, 2.5f);
    }
    g.setColour (on ? Palette::phBright() : Palette::phDim().withAlpha (0.65f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::ignoreUnused (down);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool onFrame = b.getProperties().getWithDefault ("frame", false);
    const bool isSmall = b.getProperties().getWithDefault ("small", false);
    if (onFrame)
    {
        g.setColour (Palette::navy());
        g.setFont (font (11.0f, true));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1);
        return;
    }
    g.setFont (crt (isSmall ? 10.0f : 11.5f, true));
    glowText (g, b.getButtonText(), b.getLocalBounds().toFloat().reduced (3.0f, 0.0f), juce::Justification::centred,
              Palette::phBright(), b.getToggleState() ? 1.0f : 0.8f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (3.0f);
    g.setColour (Palette::inset());
    g.fillEllipse (box);
    g.setColour (Palette::phDim().withAlpha (0.75f));
    g.drawEllipse (box, 1.0f);
    if (b.getToggleState())
    {
        g.setColour (Palette::phBright().withAlpha (0.25f));
        g.fillEllipse (box.expanded (1.5f));
        g.setColour (Palette::phBright());
        g.fillEllipse (box.reduced (3.0f));
    }
    g.setFont (crt (11.0f, true));
    glowText (g, b.getButtonText(), r.withTrimmedLeft (4.0f), juce::Justification::centredLeft, Palette::phBright(), 0.9f);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool, bool)
{
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize) : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (Palette::phDim().withAlpha (0.5f));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

//==============================================================================
// controls
//==============================================================================
namespace
{
    using APVTS = juce::AudioProcessorValueTreeState;

    int paramInt (APVTS& s, const juce::String& id)
    {
        auto* raw = s.getRawParameterValue (id);
        return raw != nullptr ? (int) std::lround (raw->load()) : 0;
    }

    struct ParamKnob : juce::Component
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        std::unique_ptr<APVTS::SliderAttachment> att;
        juce::String label;
        juce::RangedAudioParameter* param = nullptr;
        bool onFrame = false;
        ParamKnob (APVTS& s, const juce::String& id, const juce::String& text) : label (text)
        {
            param = s.getParameter (id);
            jassert (param != nullptr);
            addAndMakeVisible (slider);
            att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
            slider.setMouseDragSensitivity (180);
            slider.onValueChange = [this] { repaint(); };
        }
        void setOnFrame() { onFrame = true; slider.getProperties().set ("frame", true); }
        void resized() override { slider.setBounds (getLocalBounds().withTrimmedTop (13).withTrimmedBottom (13)); }
        void paint (juce::Graphics& g) override
        {
            const auto value = param != nullptr ? param->getCurrentValueAsText() : juce::String();
            if (onFrame)
            {
                g.setColour (Palette::navy().withAlpha (0.8f));
                g.setFont (LookAndFeel::font (10.0f, true));
                g.drawFittedText (label.toUpperCase(), getLocalBounds().removeFromTop (13), juce::Justification::centred, 1);
                g.setColour (Palette::navy());
                g.setFont (LookAndFeel::font (11.0f, true));
                g.drawFittedText (value, getLocalBounds().removeFromBottom (13), juce::Justification::centred, 1);
                return;
            }
            g.setColour (Palette::phDim());
            g.setFont (LookAndFeel::crt (9.5f, true));
            g.drawFittedText (label.toUpperCase(), getLocalBounds().removeFromTop (13), juce::Justification::centred, 1);
            g.setFont (LookAndFeel::crt (10.5f, true));
            glowText (g, value, getLocalBounds().removeFromBottom (13).toFloat(), juce::Justification::centred, Palette::phBright());
        }
    };

    struct ParamChoice : juce::Component
    {
        juce::ComboBox box;
        std::unique_ptr<APVTS::ComboBoxAttachment> att;
        juce::String label;
        ParamChoice (APVTS& s, const juce::String& id, const juce::String& text, const juce::StringArray& items) : label (text)
        {
            box.addItemList (items, 1);
            addAndMakeVisible (box);
            att = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            if (label.isNotEmpty()) r.removeFromTop (13);
            box.setBounds (r.withSizeKeepingCentre (r.getWidth(), std::min (24, r.getHeight())));
        }
        void paint (juce::Graphics& g) override
        {
            if (label.isEmpty()) return;
            g.setColour (Palette::phDim());
            g.setFont (LookAndFeel::crt (9.5f, true));
            g.drawFittedText (label.toUpperCase(), getLocalBounds().removeFromTop (13), juce::Justification::centredLeft, 1);
        }
    };

    struct ParamToggle : juce::ToggleButton
    {
        std::unique_ptr<APVTS::ButtonAttachment> att;
        ParamToggle (APVTS& s, const juce::String& id, const juce::String& text) : juce::ToggleButton (text)
        {
            att = std::make_unique<APVTS::ButtonAttachment> (s, id, *this);
        }
    };

    // a titled well in the glass
    struct Section : juce::Component
    {
        juce::String title;
        explicit Section (juce::String t) : title (std::move (t)) { setInterceptsMouseClicks (false, true); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (Palette::well());
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colour (0x30001820));
            g.drawRoundedRectangle (r.translated (0.0f, 1.0f), 6.0f, 1.0f);
            g.setColour (Palette::phDim().withAlpha (0.35f));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            auto head = r.removeFromTop (20.0f).reduced (9.0f, 0.0f);
            g.setFont (LookAndFeel::crt (11.0f, true));
            glowText (g, title.toUpperCase(), head, juce::Justification::centredLeft, Palette::phBright());
            g.setColour (Palette::phDim().withAlpha (0.55f));
            g.fillRect (head.getX(), head.getBottom() - 1.0f, head.getWidth(), 1.0f);
        }
    };

    // the inset a graph is drawn in, with its phosphor grid
    void paintScope (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Palette::inset());
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Palette::phDim().withAlpha (0.35f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (Palette::phDim().withAlpha (0.12f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
        for (int i = 1; i < 8; ++i) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY() + 2.0f, r.getBottom() - 2.0f);
    }
    // a phosphor trace: a wide soft glow under a bright line
    void trace (juce::Graphics& g, const juce::Path& p)
    {
        g.setColour (Palette::phMid().withAlpha (0.16f));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::phMid().withAlpha (0.3f));
        g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::phBright());
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // an envelope drawn from its levels and times
    struct EgGraph : juce::Component
    {
        // the start level, then segments of (time, level) - a level id, or a
        // fixed level when the id is empty; the sustain is held before the
        // last segment (the release)
        struct Seg { juce::String time, level; float fixed = 0.0f; };
        APVTS& s;
        juce::String startId;
        std::vector<Seg> segs;
        bool bipolar = true;
        EgGraph (APVTS& st, juce::String start, std::vector<Seg> sg, bool bip) : s (st), startId (std::move (start)), segs (std::move (sg)), bipolar (bip) {}
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            paintScope (g, r);
            auto yOf = [&] (float lvl) { const float n = bipolar ? (lvl + 99.0f) / 198.0f : lvl / 99.0f; return r.getBottom() - 6.0f - n * (r.getHeight() - 12.0f); };
            auto levelOf = [&] (const Seg& sg) { return sg.level.isEmpty() ? sg.fixed : (float) paramInt (s, sg.level); };
            std::vector<float> w;
            for (auto& sg : segs) w.push_back (6.0f + 0.85f * (float) paramInt (s, sg.time));
            const float sustainW = 40.0f;
            float total = sustainW; for (auto v : w) total += v;
            const float sx = (r.getWidth() - 12.0f) / total;
            std::vector<juce::Point<float>> pts;
            float x = r.getX() + 6.0f;
            pts.push_back ({ x, yOf ((float) paramInt (s, startId)) });
            for (size_t i = 0; i < segs.size(); ++i)
            {
                if (i + 1 == segs.size()) { x += sustainW * sx; pts.push_back ({ x, pts.back().y }); }
                x += w[i] * sx;
                pts.push_back ({ x, yOf (levelOf (segs[i])) });
            }
            if (bipolar)
            {
                g.setColour (Palette::phDim().withAlpha (0.4f));
                g.drawHorizontalLine ((int) yOf (0.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
            }
            juce::Path p, fill;
            p.startNewSubPath (pts[0]);
            for (size_t i = 1; i < pts.size(); ++i) p.lineTo (pts[i]);
            fill = p;
            fill.lineTo (pts.back().x, yOf (0.0f)); fill.lineTo (pts[0].x, yOf (0.0f)); fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (Palette::phMid().withAlpha (0.22f), 0, r.getY(), Palette::phMid().withAlpha (0.03f), 0, r.getBottom(), false));
            g.fillPath (fill);
            trace (g, p);
            for (auto& pt : pts)
            {
                g.setColour (Palette::phBright().withAlpha (0.25f)); g.fillEllipse (pt.x - 4.5f, pt.y - 4.5f, 9.0f, 9.0f);
                g.setColour (Palette::phBright()); g.fillEllipse (pt.x - 2.2f, pt.y - 2.2f, 4.4f, 4.4f);
            }
        }
    };

    struct FilterGraph : juce::Component
    {
        APVTS& s; int osc;
        FilterGraph (APVTS& st, int o) : s (st), osc (o) {}
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            paintScope (g, r);
            const int type = paramInt (s, oscId (osc, "fType"));
            const float fa = cutoffHz ((float) paramInt (s, oscId (osc, "fA")));
            const float fb = cutoffHz ((float) paramInt (s, oscId (osc, "fB")));
            const float q = 0.6f + std::pow ((float) paramInt (s, oscId (osc, "fRes")) / 99.0f, 2.0f) * 22.0f;
            juce::Path p, fill;
            for (int i = 0; i <= 120; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, (float) i / 120.0f);
                auto lp2 = [] (float f0, float fc, float qq) { const float x = f0 / fc; return 1.0f / std::sqrt ((1 - x * x) * (1 - x * x) + x * x / (qq * qq)); };
                float mag;
                if (type == 0) mag = lp2 (f, fa, 0.54f) * lp2 (f, fa, q);
                else { const float x = f / fb; mag = lp2 (f, fa, 0.707f + (q - 0.6f) * 0.27f) * (x * x / std::sqrt ((1 - x * x) * (1 - x * x) + x * x * 2.0f)); }
                const float db = juce::jlimit (-48.0f, 24.0f, gainToDb (mag));
                const float px = r.getX() + 2.0f + (r.getWidth() - 4.0f) * (float) i / 120.0f;
                const float py = r.getY() + 3.0f + (r.getHeight() - 6.0f) * (1.0f - (db + 48.0f) / 72.0f);
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            fill = p;
            fill.lineTo (r.getRight() - 2.0f, r.getBottom()); fill.lineTo (r.getX() + 2.0f, r.getBottom()); fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (Palette::phMid().withAlpha (0.2f), 0, r.getY(), Palette::phMid().withAlpha (0.02f), 0, r.getBottom(), false));
            g.fillPath (fill);
            trace (g, p);
        }
    };

    struct LfoGraph : juce::Component
    {
        APVTS& s; juce::String id;
        LfoGraph (APVTS& st, juce::String waveId) : s (st), id (std::move (waveId)) {}
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            paintScope (g, r);
            Lfo l; l.shape = paramInt (s, id); l.hz = 2.0f; l.keySync = true; l.seed = 99; l.trigger (0.0f);
            juce::Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const float v = l.advance (1.0f / 200.0f);
                const float px = r.getX() + 3.0f + (r.getWidth() - 6.0f) * (float) i / 200.0f;
                const float py = r.getCentreY() - v * (r.getHeight() * 0.5f - 4.0f);
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            trace (g, p);
        }
    };

    // a wave's sample (the zone around C4): attack, then the loop
    struct WaveView : juce::Component
    {
        TidebreezeAudioProcessor& proc; std::function<int()> choice;
        WaveView (TidebreezeAudioProcessor& p, std::function<int()> c) : proc (p), choice (std::move (c)) {}
        // the outline per pixel column, worked out once per wave and width
        const void* cachedWave = nullptr; int cachedCols = 0; float cachedPeak = 1.0f;
        std::vector<std::pair<float, float>> cols2;
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            paintScope (g, r);
            const int c = choice();
            const auto wave = proc.peekWave (c);   // a slot's wave can be replaced at any time
            if (wave == nullptr) proc.requestWave (c);
            g.setFont (LookAndFeel::crt (10.5f, true));
            if (wave == nullptr || wave->zones.empty())
            {
                const bool empty = c >= numFactoryWaves() && ! proc.bank.slot (WaveBank::slotOf (c)).used();
                glowText (g, proc.waveName (c).toUpperCase() + (empty ? juce::String() : juce::String ("   RENDERING_")), r.reduced (8.0f), juce::Justification::centred, Palette::phMid());
                return;
            }
            const auto& z = wave->zoneFor (60.0f);
            const int cols = std::max (1, (int) r.getWidth() - 4);
            if (cachedWave != (const void*) &z || cachedCols != cols)
            {
                // a long sample is read at a lower level of its pyramid (still several values per column)
                size_t level = 0;
                while (level + 1 < z.levels.size() && (int) z.levels[level + 1].size() >= cols * 8) ++level;
                const auto& buf = z.levels[level];
                const int n = (int) buf.size();
                cachedPeak = 1.0e-6f; for (auto v : buf) cachedPeak = std::max (cachedPeak, std::abs (v));
                cols2.assign ((size_t) cols, { 0.0f, 0.0f });
                for (int x = 0; x < cols; ++x)
                {
                    const int a = (int) ((double) x / cols * n), b = std::max (a + 1, (int) ((double) (x + 1) / cols * n));
                    float lo = 0, hi = 0;
                    for (int i = a; i < b && i < n; ++i) { lo = std::min (lo, buf[(size_t) i]); hi = std::max (hi, buf[(size_t) i]); }
                    cols2[(size_t) x] = { hi, lo };
                }
                cachedWave = (const void*) &z; cachedCols = cols;
                held = wave;
            }
            const int n = z.length();
            const float peak = cachedPeak;
            if (! wave->oneShot)
            {
                const float x0 = r.getX() + 2.0f + (float) cols * (float) z.loopStart / (float) std::max (1, n);
                g.setColour (Palette::phMid().withAlpha (0.07f));
                g.fillRect (juce::Rectangle<float> (x0, r.getY() + 1.0f, r.getRight() - 1.0f - x0, r.getHeight() - 2.0f));
                const float dash[] = { 3.0f, 3.0f };
                g.setColour (Palette::phBright().withAlpha (0.8f));
                g.drawDashedLine (juce::Line<float> (x0, r.getY() + 2.0f, x0, r.getBottom() - 2.0f), dash, 2, 1.0f);
            }
            const float h2 = r.getHeight() * 0.40f;
            juce::Path env;   // the outline, for its glow
            for (int x = 0; x < cols; ++x)
            {
                const float px = r.getX() + 2.0f + (float) x;
                if (x == 0) env.startNewSubPath (px, r.getCentreY() - cols2[0].first / peak * h2); else env.lineTo (px, r.getCentreY() - cols2[(size_t) x].first / peak * h2);
            }
            g.setColour (Palette::phMid().withAlpha (0.65f));
            for (int x = 0; x < cols; ++x)
                g.drawVerticalLine ((int) (r.getX() + 2.0f) + x, r.getCentreY() - cols2[(size_t) x].first / peak * h2, r.getCentreY() - cols2[(size_t) x].second / peak * h2 + 1.0f);
            trace (g, env);
            const juce::String info = proc.waveName (c).toUpperCase() + "   " + juce::String (wave->zones.size()) + (wave->zones.size() == 1 ? " ZONE" : " ZONES")
                                    + "   " + juce::String ((double) z.length() / wave->renderRate, 2) + " S"
                                    + (wave->oneShot ? "   ONE-SHOT" : "   LOOP " + juce::String ((double) (z.loopEnd - z.loopStart) / wave->renderRate, 2) + " S");
            g.setFont (LookAndFeel::crt (9.5f, true));
            glowText (g, info, r.reduced (8.0f, 5.0f), juce::Justification::topLeft, Palette::phBright());
        }
        tb::WavePtr held;   // keeps the cached zone alive
    };

    // the effect chain as it is now: layers -> inserts -> out, sends alongside
    struct ChainView : juce::Component
    {
        APVTS& s;
        explicit ChainView (APVTS& st) : s (st) {}
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            auto box = [&] (juce::Rectangle<float> b, const juce::String& t, bool on)
            {
                g.setColour (Palette::inset());
                g.fillRoundedRectangle (b, 3.0f);
                g.setColour (Palette::phDim().withAlpha (on ? 0.8f : 0.25f));
                g.drawRoundedRectangle (b, 3.0f, 1.0f);
                g.setFont (LookAndFeel::crt (10.0f, true));
                glowText (g, t.toUpperCase(), b.reduced (4.0f, 0.0f), juce::Justification::centred, Palette::phBright(), on ? 1.0f : 0.4f);
            };
            auto arrow = [&] (float x, float y0, float y1)
            {
                g.setColour (Palette::phDim().withAlpha (0.7f));
                g.drawLine (x, y0, x, y1 - 3.0f, 1.0f);
                juce::Path a; a.addTriangle (x - 3.0f, y1 - 4.0f, x + 3.0f, y1 - 4.0f, x, y1);
                g.fillPath (a);
            };
            const float bh = 19.0f, gap = 8.0f;
            float y = r.getY();
            box ({ r.getX(), y, r.getWidth(), bh }, paramInt (s, "mode") == 1 ? "Layer A + Layer B" : "Layer A", true);
            for (int n = 0; n < 3; ++n)
            {
                arrow (r.getCentreX(), y + bh, y + bh + gap);
                y += bh + gap;
                const int type = paramInt (s, fxId ("ifx", n, "type"));
                const bool on = type != FxNone && paramInt (s, fxId ("ifx", n, "on")) == 1;
                box ({ r.getX() + 10.0f, y, r.getWidth() - 20.0f, bh }, "Ins " + juce::String (n + 1) + " " + effectNames()[type], on);
            }
            arrow (r.getCentreX(), y + bh, y + bh + gap);
            y += bh + gap;
            const float half = (r.getWidth() - gap) * 0.5f;
            for (int n = 0; n < 2; ++n)
            {
                const int type = paramInt (s, fxId ("mfx", n, "type"));
                const bool on = type != FxNone && paramInt (s, fxId ("mfx", n, "on")) == 1 && paramInt (s, fxId ("mfx", n, "send")) > 0;
                box ({ r.getX() + (float) n * (half + gap), y, half, bh }, effectNames()[type], on);
            }
            arrow (r.getCentreX(), y + bh, y + bh + gap);
            y += bh + gap;
            box ({ r.getX(), y, r.getWidth(), bh }, "EQ > Volume > Out", true);
        }
    };

    //==========================================================================
    // a page: owns its controls
    //==========================================================================
    struct PageComp : juce::Component
    {
        std::vector<std::unique_ptr<juce::Component>> owned;
        template <typename T> T* add (std::unique_ptr<T> c, juce::Rectangle<int> b)
        {
            auto* raw = c.get();
            raw->setBounds (b);
            addAndMakeVisible (*raw);
            owned.push_back (std::move (c));
            return raw;
        }
        Section* section (const juce::String& title, juce::Rectangle<int> b)
        {
            auto* s = add (std::make_unique<Section> (title), b);
            s->toBack();
            return s;
        }
        void timerRepaint()
        {
            for (auto& c : owned)
                if (dynamic_cast<EgGraph*> (c.get()) || dynamic_cast<FilterGraph*> (c.get()) || dynamic_cast<LfoGraph*> (c.get())
                    || dynamic_cast<WaveView*> (c.get()) || dynamic_cast<ChainView*> (c.get()))
                    c->repaint();
        }
    };
}

//==============================================================================
// the editor
//==============================================================================
TidebreezeAudioProcessorEditor::TidebreezeAudioProcessorEditor (TidebreezeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);
    setSize (kWidth, kHeight);

    // the macro knobs
    const char* rt[4] = { "rtCutoff", "rtReso", "rtEgInt", "rtRelease" };
    const char* rtName[4] = { "Cutoff", "Resonance", "Env Depth", "Release" };
    for (int i = 0; i < 4; ++i)
    {
        auto k = std::make_unique<ParamKnob> (proc.apvts, rt[i], rtName[i]);
        k->setOnFrame();
        k->setBounds (16 + i * 72, 82, 70, 76);
        addAndMakeVisible (*k);
        top.push_back (std::move (k));
    }
    // the performance knobs (lined up with the macros), and the voice keys:
    // the layer count, mono, hold and the arpeggio
    {
        const char* ids[3] = { "porta", "bend", "volume" };
        const char* names[3] = { "Glide", "Bend", "Volume" };
        for (int i = 0; i < 3; ++i)
        {
            auto k = std::make_unique<ParamKnob> (proc.apvts, ids[i], names[i]);
            k->setOnFrame();
            k->setBounds (rightX + i * 96, 88, 88, 72);
            addAndMakeVisible (*k);
            top.push_back (std::move (k));
        }
        struct KeyDef { const char* id; const char* text; };
        const KeyDef keys[] = { { "mode", "2 LAYERS" }, { "voiceMode", "MONO" }, { "hold", "HOLD" }, { "arpOn", "ARPEGGIO" } };
        int x = rightX;
        for (int i = 0; i < 4; ++i)
        {
            const auto& kd = keys[i];
            auto key = std::make_unique<juce::TextButton> (kd.text);
            key->getProperties().set ("frame", true);
            key->setClickingTogglesState (true);
            const int w = i == 3 ? rightX + rightW - x : 66;
            key->setBounds (x, 58, w, 28);
            x += w + 5;
            addAndMakeVisible (*key);
            keyAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kd.id, *key));
            top.push_back (std::move (key));
        }
    }
    for (auto* b : { &diceButton, &loadButton, &saveButton })
    {
        b->getProperties().set ("frame", true);
        addAndMakeVisible (*b);
    }
    for (auto* b : { &prevButton, &nextButton })   // on the display's glass
        addAndMakeVisible (*b);
    prevButton.onClick = [this] { stepProgram (-1); };
    nextButton.onClick = [this] { stepProgram (1); };
    diceButton.onClick = [this] { showRandomMenu(); };
    loadButton.onClick = [this] { loadPreset(); };
    saveButton.onClick = [this] { savePreset(); };

    const char* tabNames[NumPages] = { "PLAY", "LAYER", "MOD", "EFFECTS", "WAVES" };
    for (int i = 0; i < NumPages; ++i)
    {
        tabs[i].setButtonText (tabNames[i]);
        tabs[i].getProperties().set ("frame", true);
        tabs[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs[i]);
    }
    for (int i = 0; i < 2; ++i)
    {
        oscTabs[i].setButtonText (i == 0 ? "LAYER A" : "LAYER B");
        oscTabs[i].getProperties().set ("frame", true);
        oscTabs[i].onClick = [this, i] { showOsc (i); };
        addAndMakeVisible (oscTabs[i]);
    }

    proc.onProgramChanged = [this]
    {
        juce::Component::SafePointer<TidebreezeAudioProcessorEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->buildPage(); safe->repaint(); } });
    };
    showPage (PagePlay);
    startTimerHz (15);
}

TidebreezeAudioProcessorEditor::~TidebreezeAudioProcessorEditor()
{
    proc.onProgramChanged = nullptr;
    pageComp.reset();
    keyAttachments.clear();
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> TidebreezeAudioProcessorEditor::logoArea() const { return { 16, 8, 290, 60 }; }
juce::Rectangle<int> TidebreezeAudioProcessorEditor::layerSummaryArea (int o) const
{
    auto s = screenArea();
    return { s.getX() + 6, s.getY() + 33 + o * 64, 150, 52 };
}
juce::Rectangle<int> TidebreezeAudioProcessorEditor::displayArea() const { return { 324, 12, 462, 132 }; }
juce::Rectangle<int> TidebreezeAudioProcessorEditor::screenArea() const { return { 16, 196, kWidth - 32, 410 }; }
juce::Rectangle<int> TidebreezeAudioProcessorEditor::pageArea() const
{
    auto s = screenArea();
    return { s.getX() + 162, s.getY() + 5, 900, 400 };
}

juce::Image TidebreezeAudioProcessorEditor::makeTubeOverlay (juce::Rectangle<int> glass, float radius)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, glass.getWidth()), juce::jmax (1, glass.getHeight()), true);
    juce::Graphics g (img);
    auto r = img.getBounds().toFloat();
    juce::Path clip; clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    // scanlines
    for (int y = 0; y < img.getHeight(); y += 3)
    {
        g.setColour (juce::Colour (0xff012b38).withAlpha (0.16f));
        g.fillRect (0, y, img.getWidth(), 1);
    }
    // the tube's darker edges
    juce::ColourGradient vig (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                              juce::Colour (0xff01222c).withAlpha (0.45f), r.getX(), r.getY(), true);
    vig.addColour (0.62, juce::Colours::transparentBlack);
    g.setGradientFill (vig);
    g.fillRect (r);
    // the reflection on the glass
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.09f), r.getX(), r.getY(),
                               juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.45f, false);
    g.setGradientFill (refl);
    g.fillEllipse (r.getX() - r.getWidth() * 0.1f, r.getY() - r.getHeight() * 0.35f, r.getWidth() * 1.2f, r.getHeight() * 0.75f);
    return img;
}

void TidebreezeAudioProcessorEditor::resized()
{
    auto d = displayArea();
    prevButton.setBounds (d.getRight() - 92, d.getY() + 38, 36, 26);
    nextButton.setBounds (d.getRight() - 50, d.getY() + 38, 36, 26);
    // the preset keys in one row beside the display
    diceButton.setBounds (rightX, 14, 88, 28);
    loadButton.setBounds (rightX + 96, 14, 88, 28);
    saveButton.setBounds (rightX + 192, 14, 88, 28);
    // the navigation row right above the screen: the layer keys over the
    // layer column, the page tabs over the page
    auto pa = pageArea();
    auto s = screenArea();
    const int tabW = (pa.getWidth() - 4 * 6) / 5;
    for (int i = 0; i < NumPages; ++i) tabs[i].setBounds (pa.getX() + i * (tabW + 6), navY, tabW, 26);
    oscTabs[0].setBounds (s.getX() + 6, navY, 72, 26);
    oscTabs[1].setBounds (s.getX() + 84, navY, 72, 26);
    if (pageComp != nullptr) pageComp->setBounds (pa);
    displayOverlay = makeTubeOverlay (displayArea(), 10.0f);
    screenOverlay = makeTubeOverlay (screenArea(), 14.0f);
}

// a metal bezel around a tube, with a screw in each corner
void TidebreezeAudioProcessorEditor::paintBezel (juce::Graphics& g, juce::Rectangle<float> glass, float radius)
{
    auto bez = glass.expanded (7.0f);
    g.setColour (juce::Colour (0x50000000));
    g.fillRoundedRectangle (bez.translated (0.0f, 2.0f), radius + 5.0f);
    juce::ColourGradient metal (juce::Colour (0xff434a57), bez.getX(), bez.getY(), juce::Colour (0xff151920), bez.getX(), bez.getBottom(), false);
    metal.addColour (0.15, juce::Colour (0xff2e3440));
    g.setGradientFill (metal);
    g.fillRoundedRectangle (bez, radius + 5.0f);
    g.setColour (juce::Colour (0xff0c0e13));
    g.drawRoundedRectangle (bez.reduced (0.5f), radius + 5.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (bez.getX() + 14, bez.getY() + 1.5f, bez.getRight() - 14, bez.getY() + 1.5f, 1.0f);
    for (auto c : { bez.getTopLeft() + juce::Point<float> (5.5f, 5.5f), bez.getTopRight() + juce::Point<float> (-5.5f, 5.5f),
                    bez.getBottomLeft() + juce::Point<float> (5.5f, -5.5f), bez.getBottomRight() + juce::Point<float> (-5.5f, -5.5f) })
    {
        g.setColour (juce::Colour (0xff0e1015));
        g.fillEllipse (c.x - 2.4f, c.y - 2.4f, 4.8f, 4.8f);
        g.setColour (juce::Colour (0xff5a6272));
        g.drawLine (c.x - 1.5f, c.y - 1.5f, c.x + 1.5f, c.y + 1.5f, 0.8f);
    }
}

// the cyan glass of a tube
void TidebreezeAudioProcessorEditor::paintGlass (juce::Graphics& g, juce::Rectangle<float> glass, float radius)
{
    g.setColour (juce::Colour (0xff05070b));
    g.fillRoundedRectangle (glass.expanded (2.5f), radius + 2.0f);
    juce::ColourGradient tube (Palette::glassCentre(), glass.getCentreX(), glass.getCentreY() - glass.getHeight() * 0.15f,
                               Palette::glassEdge(), glass.getX(), glass.getBottom(), true);
    g.setGradientFill (tube);
    g.fillRoundedRectangle (glass, radius);
}

void TidebreezeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // the frame: brushed silver
    auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (Palette::frameTop(), 0, 0, Palette::frameBottom(), 0, b.getHeight(), false));
    g.fillRoundedRectangle (b, 10.0f);
    juce::Random rng (7);
    for (int i = 0; i < 240; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f + 0.05f * rng.nextFloat()));
        g.drawHorizontalLine ((int) (rng.nextFloat() * b.getHeight()), 6.0f, b.getWidth() - 6.0f);
    }
    g.setColour (Palette::frameLine());
    g.drawRoundedRectangle (b.reduced (1.0f), 10.0f, 1.5f);

    // the logo: a wave, the name, the line under it
    {
        auto r = logoArea().toFloat();
        juce::Path wave;
        const float wx = r.getX() + 2.0f, wy = r.getY() + 26.0f;
        for (int i = 0; i <= 60; ++i)
        {
            const float t = (float) i / 60.0f;
            const float y = wy - std::sin (t * kTwoPi * 1.5f) * 9.0f * (1.0f - t * 0.4f);
            if (i == 0) wave.startNewSubPath (wx + t * 44.0f, y); else wave.lineTo (wx + t * 44.0f, y);
        }
        g.setColour (Palette::cyanDeep());
        g.strokePath (wave, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::navy());
        g.setFont (LookAndFeel::font (31.0f, true));
        g.drawText ("TIDEBREEZE", juce::Rectangle<float> (r.getX() + 54.0f, r.getY(), 240.0f, 42.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff3d5a69));
        g.setFont (LookAndFeel::font (10.5f, true));
        g.drawText ("GENERATIVE WAVE SYNTH", juce::Rectangle<float> (r.getX() + 56.0f, r.getY() + 40.0f, 240.0f, 14.0f).toNearestInt(), juce::Justification::centredLeft);
        g.drawText ("MACROS", 20, 70, 200, 12, juce::Justification::centredLeft);
        g.drawText ("PRESET", rightX + 2, 2, 200, 12, juce::Justification::centredLeft);
        g.drawText ("VOICE", rightX + 2, 46, 200, 12, juce::Justification::centredLeft);
    }

    // the preset display: a small tube
    {
        auto d = displayArea().toFloat();
        paintBezel (g, d, 10.0f);
        paintGlass (g, d, 10.0f);
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (d, 10.0f);
        g.reduceClipRegion (clip);
        auto in = d.reduced (14.0f, 9.0f);
        auto head = in.removeFromTop (18.0f);
        g.setFont (LookAndFeel::crt (11.0f, true));
        auto tag = head.withWidth (64.0f);
        g.setColour (Palette::phBright().withAlpha (0.14f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (Palette::phBright().withAlpha (0.8f));
        g.drawRoundedRectangle (tag, 3.0f, 1.0f);
        glowText (g, "PRESET", tag, juce::Justification::centred, Palette::phBright());
        const bool two = paramInt (proc.apvts, "mode") == 1;
        glowText (g, two ? "2 LAYERS" : "1 LAYER", head.withTrimmedLeft (76.0f), juce::Justification::centredLeft, Palette::phMid(), 0.9f);
        g.setFont (LookAndFeel::crt (9.5f));
        glowText (g, "TAP THE NAME FOR PRESETS", head, juce::Justification::centredRight, Palette::phDim(), 0.9f);
        g.setColour (Palette::phDim().withAlpha (0.7f));
        g.fillRect (head.getX(), head.getBottom() + 1.0f, head.getWidth(), 1.0f);
        g.setFont (LookAndFeel::crt (24.0f, true));
        glowText (g, proc.programName.toUpperCase(), juce::Rectangle<float> (in.getX(), in.getY() + 6.0f, in.getWidth() - 104.0f, 34.0f), juce::Justification::centredLeft, Palette::phBright());
        g.setFont (LookAndFeel::crt (11.0f, true));
        const juce::String where = proc.currentProgram >= 0 ? "FACTORY #" + juce::String (proc.currentProgram + 1).paddedLeft ('0', 2) : "ROLLED";
        glowText (g, where, juce::Rectangle<float> (in.getX(), in.getY() + 46.0f, 150.0f, 16.0f), juce::Justification::centredLeft, Palette::phMid());
        glowText (g, proc.programCategory.toUpperCase(), juce::Rectangle<float> (in.getX() + 150.0f, in.getY() + 46.0f, 280.0f, 16.0f), juce::Justification::centredLeft, Palette::phMid());
        const bool arp = paramInt (proc.apvts, "arpOn") == 1;
        auto foot = juce::Rectangle<float> (in.getX(), in.getBottom() - 16.0f, in.getWidth(), 16.0f);
        g.setColour (Palette::phDim().withAlpha (0.6f));
        g.fillRect (foot.getX(), foot.getY() - 4.0f, foot.getWidth(), 1.0f);
        glowText (g, arp ? "ARP ON" : "ARP OFF", foot.withWidth (80.0f), juce::Justification::centredLeft, Palette::phBright(), arp ? 1.0f : 0.5f);
        glowText (g, juce::String (proc.currentTempo(), 1) + " BPM", foot.withTrimmedLeft (84.0f).withWidth (110.0f), juce::Justification::centredLeft, Palette::phMid(), 0.9f);
        // the level meter: phosphor segments
        const float lvl = juce::jlimit (0.0f, 1.0f, (gainToDb (proc.meterLevel()) + 48.0f) / 48.0f);
        auto m = juce::Rectangle<float> (foot.getRight() - 190.0f, foot.getY() + 4.0f, 190.0f, 8.0f);
        const int segs = 24;
        const float sw = m.getWidth() / (float) segs;
        for (int i = 0; i < segs; ++i)
        {
            const bool lit = (float) (i + 1) / (float) segs <= lvl + 0.001f;
            auto sr = juce::Rectangle<float> (m.getX() + (float) i * sw + 1.0f, m.getY(), sw - 2.0f, m.getHeight());
            if (lit) { g.setColour (Palette::phBright().withAlpha (0.25f)); g.fillRect (sr.expanded (1.0f)); }
            g.setColour (lit ? (i >= segs - 2 ? juce::Colours::white : Palette::phBright()) : Palette::phDim().withAlpha (0.18f));
            g.fillRect (sr);
        }
    }

    // the screen: the big tube
    {
        auto s = screenArea().toFloat();
        paintBezel (g, s, 14.0f);
        paintGlass (g, s, 14.0f);
        // the left column: the layers at a glance
        auto left = juce::Rectangle<float> (s.getX() + 6.0f, s.getY() + 5.0f, 150.0f, s.getHeight() - 10.0f);
        g.setColour (Palette::well());
        g.fillRoundedRectangle (left, 6.0f);
        g.setColour (Palette::phDim().withAlpha (0.35f));
        g.drawRoundedRectangle (left.reduced (0.5f), 6.0f, 1.0f);
        const bool perLayer = page != PageEffects;
        auto head = juce::Rectangle<float> (left.getX() + 9.0f, left.getY(), left.getWidth() - 18.0f, 20.0f);
        g.setFont (LookAndFeel::crt (11.0f, true));
        glowText (g, perLayer ? "EDIT LAYER" : "THE LAYERS", head, juce::Justification::centredLeft, Palette::phBright());
        g.setColour (Palette::phDim().withAlpha (0.55f));
        g.fillRect (head.getX(), head.getBottom() - 1.0f, head.getWidth(), 1.0f);
        const int layers = paramInt (proc.apvts, "mode") == 1 ? 2 : 1;
        if (perLayer)
        {
            // the layer being edited, lit
            auto sel = layerSummaryArea (oscEdit).toFloat().reduced (3.0f, 0.0f);
            g.setColour (Palette::phMid().withAlpha (0.14f));
            g.fillRoundedRectangle (sel, 4.0f);
            g.setColour (Palette::phBright().withAlpha (0.55f));
            g.drawRoundedRectangle (sel, 4.0f, 1.0f);
        }
        for (int o = 0; o < 2; ++o)
        {
            const auto y = left.getY() + 30.0f + (float) o * 64.0f;
            const bool used = o < layers;
            g.setFont (LookAndFeel::crt (9.5f, true));
            glowText (g, juce::String (o == 0 ? "LAYER A" : "LAYER B") + (used ? "" : "  OFF"), juce::Rectangle<float> (left.getX() + 9.0f, y, 136.0f, 14.0f),
                      juce::Justification::centredLeft, Palette::phDim(), used ? 1.0f : 0.5f);
            g.setFont (LookAndFeel::crt (11.0f, true));
            glowText (g, proc.waveName (paramInt (proc.apvts, oscId (o, "hiWave"))).toUpperCase(), juce::Rectangle<float> (left.getX() + 9.0f, y + 15.0f, 136.0f, 16.0f),
                      juce::Justification::centredLeft, Palette::phBright(), used ? 1.0f : 0.45f);
            g.setFont (LookAndFeel::crt (9.5f));
            glowText (g, "LOW " + proc.waveName (paramInt (proc.apvts, oscId (o, "loWave"))).toUpperCase(), juce::Rectangle<float> (left.getX() + 9.0f, y + 32.0f, 136.0f, 14.0f),
                      juce::Justification::centredLeft, Palette::phMid(), used ? 0.85f : 0.4f);
        }
        g.setFont (LookAndFeel::crt (9.0f));
        g.setColour (Palette::phDim());
        g.drawFittedText ("TAP A LAYER TO EDIT IT.\nROLL OR THE LOGO:\nA RANDOM PRESET.", juce::Rectangle<float> (left.getX() + 9.0f, left.getBottom() - 46.0f, 136.0f, 40.0f).toNearestInt(), juce::Justification::bottomLeft, 3);
    }
}

void TidebreezeAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // the tubes' scanlines, darker edges and reflections lie over everything on them
    g.setOpacity (1.0f);
    for (auto [img, area, radius] : { std::make_tuple (&displayOverlay, displayArea(), 10.0f), std::make_tuple (&screenOverlay, screenArea(), 14.0f) })
    {
        if (img->isValid()) g.drawImageAt (*img, area.getX(), area.getY());
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (area.toFloat(), radius, 1.5f);
        g.setColour (Palette::phMid().withAlpha (0.12f));
        g.drawRoundedRectangle (area.toFloat().reduced (1.5f), radius - 1.0f, 1.0f);
    }
}

void TidebreezeAudioProcessorEditor::timerCallback()
{
    if (auto* pc = dynamic_cast<PageComp*> (pageComp.get())) pc->timerRepaint();
    // rebuild when an effect type or the layer count changes (the knobs are named by effect)
    int types = 0;
    for (int n = 0; n < 3; ++n) types = types * 31 + paramInt (proc.apvts, fxId ("ifx", n, "type"));
    for (int n = 0; n < 2; ++n) types = types * 31 + paramInt (proc.apvts, fxId ("mfx", n, "type"));
    types = types * 3 + paramInt (proc.apvts, "mode");
    if (types != lastFxTypes) { const bool first = lastFxTypes < 0; lastFxTypes = types; if (! first) buildPage(); }
    if (proc.bank.changed.exchange (false) && pageComp != nullptr) pageComp->repaint();
    repaint (displayArea().expanded (8));
    repaint (screenArea().withWidth (168));
}

void TidebreezeAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    if (logoArea().contains (e.getPosition())) { showRandomMenu(); return; }
    auto nameArea = displayArea().withTrimmedTop (28).withHeight (46).withTrimmedRight (104);
    if (nameArea.contains (e.getPosition())) { showProgramMenu(); return; }
    // a tap on a layer in the screen's left column edits it
    for (int o = 0; o < 2; ++o)
        if (layerSummaryArea (o).contains (e.getPosition()) && page != PageEffects && (o == 0 || paramInt (proc.apvts, "mode") == 1))
            showOsc (o);
}

void TidebreezeAudioProcessorEditor::showPage (int p)
{
    page = juce::jlimit (0, NumPages - 1, p);
    for (int i = 0; i < NumPages; ++i) tabs[i].setToggleState (i == page, juce::dontSendNotification);
    buildPage();
    repaint (screenArea().withWidth (168));
}

void TidebreezeAudioProcessorEditor::showOsc (int o)
{
    oscEdit = juce::jlimit (0, 1, o);
    buildPage();
    repaint (screenArea().withWidth (168));
}

void TidebreezeAudioProcessorEditor::showEnvelope (int which)
{
    envEdit = juce::jlimit (0, 2, which);
    buildPage();
}

void TidebreezeAudioProcessorEditor::stepProgram (int delta)
{
    const int n = (int) factoryPrograms().size();
    const int cur = proc.currentProgram < 0 ? 0 : proc.currentProgram;
    proc.loadFactoryProgram ((cur + delta + n) % n);
}

void TidebreezeAudioProcessorEditor::showProgramMenu()
{
    juce::PopupMenu menu;
    const auto& progs = factoryPrograms();
    for (int c : categoryMenuOrder())
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) progs.size(); ++i)
            if (progs[(size_t) i].category == c)
                sub.addItem (1 + i, juce::String (i + 1).paddedLeft ('0', 2) + "  " + progs[(size_t) i].name, true, i == proc.currentProgram);
        if (sub.getNumItems() > 0) menu.addSubMenu (categoryNames()[c], sub);
    }
    juce::Component::SafePointer<TidebreezeAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r) { if (safe != nullptr && r > 0) safe->proc.loadFactoryProgram (r - 1); });
}

void TidebreezeAudioProcessorEditor::showRandomMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Random Preset");
    for (int c : categoryMenuOrder()) menu.addItem (100 + c, categoryNames()[c]);
    menu.addSeparator();
    menu.addItem (99, "Surprise Me (any category)");
    menu.addItem (98, "Super Random (anything goes)");
    juce::Component::SafePointer<TidebreezeAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        safe->proc.rollProgram (r == 98 ? kSuperRandom : (r == 99 ? -1 : r - 100));
    });
}

void TidebreezeAudioProcessorEditor::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Save Tidebreeze preset", juce::File(), "*.tidebreeze");
    juce::Component::SafePointer<TidebreezeAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return;
        auto f = c.getResult();
        if (f == juce::File()) return;
        juce::MemoryBlock mb; safe->proc.getStateInformation (mb);
       #if JUCE_ANDROID
        f.replaceWithData (mb.getData(), mb.getSize());   // the picker's own document
       #else
        (f.hasFileExtension ("tidebreeze") ? f : f.withFileExtension ("tidebreeze")).replaceWithData (mb.getData(), mb.getSize());
       #endif
    });
}

void TidebreezeAudioProcessorEditor::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Tidebreeze preset", juce::File(), "*.tidebreeze");
    juce::Component::SafePointer<TidebreezeAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return;
        auto f = c.getResult();
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData (mb)) safe->proc.setStateInformation (mb.getData(), (int) mb.getSize());
    });
}

// an audio file into a user slot (-1: the next free one, or the oldest). On
// Android the picked document is copied into the app's storage first (see
// Aquanode Modular's file chooser for the plugins), and the wave keeps the
// audio itself, so the preset never depends on the file again.
void TidebreezeAudioProcessorEditor::loadWaveFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a wave (audio file)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<TidebreezeAudioProcessorEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, slot] (const juce::FileChooser& c)
    {
        if (safe == nullptr) return;
        const auto f = c.getResult();
        if (! f.existsAsFile()) return;
        juce::String error;
        const int choice = safe->proc.loadUserWave (f, slot, error);
        if (choice < 0)
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tidebreeze", "The wave could not be loaded.\n\n" + error);
            return;
        }
        safe->proc.ensureProgramWaves();
        safe->waveFocus = choice;
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->buildPage(); });
    });
}

//==============================================================================
// the pages (900 x 400)
//==============================================================================
void TidebreezeAudioProcessorEditor::buildPage()
{
    const bool two = paramInt (proc.apvts, "mode") == 1;
    if (! two && oscEdit == 1) oscEdit = 0;
    for (int i = 0; i < 2; ++i) oscTabs[i].setToggleState (i == oscEdit, juce::dontSendNotification);
    const bool perLayer = page != PageEffects;
    oscTabs[0].setVisible (perLayer); oscTabs[1].setVisible (perLayer);
    oscTabs[1].setEnabled (two);

    auto pc = std::make_unique<PageComp>();
    auto& s = proc.apvts;
    const int o = oscEdit;
    const juce::String layerName = o == 0 ? "Layer A" : "Layer B";
    auto oid = [o] (const char* n) { return oscId (o, n); };
    auto knob = [&] (const juce::String& id, const juce::String& name, juce::Rectangle<int> b) { return pc->add (std::make_unique<ParamKnob> (s, id, name), b); };
    auto choice = [&] (const juce::String& id, const juce::String& name, const juce::StringArray& items, juce::Rectangle<int> b) { return pc->add (std::make_unique<ParamChoice> (s, id, name, items), b); };
    auto toggle = [&] (const juce::String& id, const juce::String& name, juce::Rectangle<int> b) { return pc->add (std::make_unique<ParamToggle> (s, id, name), b); };
    juce::StringArray waveItems;
    for (int i = 0; i < numWaveChoices(); ++i) waveItems.add (proc.waveName (i));
    const juce::StringArray filterTypes { "Resonant Low-Pass", "Low-Pass + High-Pass" };
    // the envelopes' graphs: amp (ends at 0), filter (ends at its end level), pitch (common)
    auto egGraph = [&] (int which, juce::Rectangle<int> b)
    {
        if (which == 2)
            return pc->add (std::make_unique<EgGraph> (s, "pegStart", std::vector<EgGraph::Seg> { { "pegAttackT", "pegAttack" }, { "pegDecayT", {}, 0.0f },
                                                                                                 { "pegReleaseT", "pegRelease" } }, true), b);
        const bool amp = which == 1;
        const char* e = amp ? "aeg" : "feg";
        auto id = [&] (const char* n) { return oid ((juce::String (e) + n).toRawUTF8()); };
        std::vector<EgGraph::Seg> sg { { id ("AttackT"), id ("Attack") }, { id ("DecayT"), id ("Break") }, { id ("SlopeT"), id ("Sustain") },
                                       { id ("ReleaseT"), amp ? juce::String() : id ("Release"), 0.0f } };
        return pc->add (std::make_unique<EgGraph> (s, id ("Start"), sg, ! amp), b);
    };
    juce::Component::SafePointer<TidebreezeAudioProcessorEditor> safe (this);

    switch (page)
    {
        case PagePlay:
        {
            const int c0 = 0, c1 = 303, c2 = 606, cw = 294;
            // row 1
            pc->section (layerName, { c0, 0, cw, 112 });
            choice (oid ("hiWave"), "High wave", waveItems, { c0 + 8, 22, 190, 38 });
            choice (oid ("loWave"), "Low wave", waveItems, { c0 + 8, 64, 190, 38 });
            choice (oid ("octave"), "Octave", octaveNames(), { c0 + 206, 22, 80, 38 });
            toggle (oid ("hiRev"), "Reverse", { c0 + 206, 72, 84, 22 });
            pc->section ("Pitch", { c1, 0, cw, 112 });
            knob (oid ("transpose"), "Transpose", { c1 + 8, 22, 66, 86 }); knob (oid ("tune"), "Tune", { c1 + 78, 22, 66, 86 });
            knob ("porta", "Glide", { c1 + 148, 22, 66, 86 }); knob ("bend", "Bend", { c1 + 218, 22, 66, 86 });
            pc->section ("Amp", { c2, 0, cw, 112 });
            knob (oid ("aLvl"), "Level", { c2 + 8, 22, 66, 86 }); knob (oid ("aPan"), "Pan", { c2 + 78, 22, 66, 86 });
            knob (oid ("aVel"), "Velocity", { c2 + 148, 22, 66, 86 }); knob ("volume", "Volume", { c2 + 218, 22, 66, 86 });
            // row 2
            pc->section ("Filter", { c0, 120, cw, 148 });
            choice (oid ("fType"), "Type", filterTypes, { c0 + 8, 142, 152, 38 });
            pc->add (std::make_unique<FilterGraph> (s, o), { c0 + 166, 144, 120, 54 });
            knob (oid ("fA"), "Cutoff", { c0 + 8, 202, 66, 64 }); knob (oid ("fRes"), "Resonance", { c0 + 78, 202, 66, 64 });
            knob (oid ("fKt"), "Keytrack", { c0 + 148, 202, 66, 64 }); knob (oid ("fB"), "High-Pass", { c0 + 218, 202, 66, 64 });
            pc->section ("Filter Envelope", { c1, 120, cw, 148 });
            egGraph (0, { c1 + 8, 144, 196, 116 });
            knob (oid ("fEgA"), "Depth", { c1 + 210, 140, 76, 62 }); knob (oid ("fVelA"), "Velocity", { c1 + 210, 202, 76, 62 });
            pc->section ("Amp Envelope", { c2, 120, cw, 148 });
            egGraph (1, { c2 + 8, 144, 160, 116 });
            knob (oid ("aegAttackT"), "Attack", { c2 + 172, 140, 58, 62 }); knob (oid ("aegReleaseT"), "Release", { c2 + 232, 140, 58, 62 });
            knob (oid ("aegDecayT"), "Decay", { c2 + 172, 202, 58, 62 }); knob (oid ("aegSustain"), "Sustain", { c2 + 232, 202, 58, 62 });
            // row 3
            pc->section ("Effects", { c0, 276, cw, 124 });
            for (int n = 0; n < 5; ++n)
            {
                const bool ins = n < 3;
                const int k = ins ? n : n - 3;
                const char* slot = ins ? "ifx" : "mfx";
                const int y = 298 + n * 20;
                toggle (fxId (slot, k, "on"), ins ? "Ins " + juce::String (k + 1) : "Send " + juce::String (k + 1), { c0 + 8, y, 72, 19 });
                choice (fxId (slot, k, "type"), {}, effectNames(), { c0 + 82, y, 204, 19 });
            }
            pc->section ("Master EQ", { c1, 276, cw, 124 });
            knob ("eqLo", "Low", { c1 + 8, 300, 66, 92 }); knob ("eqMid", "Mid", { c1 + 78, 300, 66, 92 });
            knob ("eqMidHz", "Mid Freq", { c1 + 148, 300, 66, 92 }); knob ("eqHi", "High", { c1 + 218, 300, 66, 92 });
            pc->section ("Arpeggio", { c2, 276, cw, 124 });
            toggle ("arpOn", "On", { c2 + 8, 298, 48, 22 });
            toggle ("arpLatch", "Latch", { c2 + 58, 298, 62, 22 });
            choice ("arpPat", {}, arpPatternNames(), { c2 + 122, 298, 104, 22 });
            choice ("arpRes", {}, arpResNames(), { c2 + 230, 298, 58, 22 });
            knob ("arpOct", "Octaves", { c2 + 8, 326, 54, 70 }); knob ("arpGate", "Gate", { c2 + 64, 326, 54, 70 });
            knob ("arpVel", "Velocity", { c2 + 120, 326, 54, 70 }); knob ("arpSwing", "Swing", { c2 + 176, 326, 54, 70 });
            knob ("arpTempo", "Tempo", { c2 + 232, 326, 54, 70 });
            break;
        }
        case PageLayer:
        {
            // the waves
            pc->section (layerName + " - Waves", { 0, 0, 548, 196 });
            for (int hl = 0; hl < 2; ++hl)
            {
                const int y = 24 + hl * 86;
                const char* w = hl == 0 ? "hiWave" : "loWave";
                choice (oid (w), hl == 0 ? "High wave (above the split)" : "Low wave", waveItems, { 8, y, 200, 38 });
                toggle (oid (hl == 0 ? "hiRev" : "loRev"), "Reverse", { 8, y + 44, 92, 22 });
                toggle (oid (hl == 0 ? "hiOfs" : "loOfs"), "Skip Attack", { 102, y + 44, 108, 22 });
                knob (oid (hl == 0 ? "hiLvl" : "loLvl"), "Level", { 212, y, 62, 76 });
                auto& p = proc;
                const juce::String wid = oid (w);
                pc->add (std::make_unique<WaveView> (p, [&p, wid] { return paramInt (p.apvts, wid); }), { 280, y, 260, 78 });
            }
            // pitch
            pc->section ("Pitch", { 556, 0, 344, 196 });
            choice (oid ("octave"), "Octave", octaveNames(), { 564, 24, 86, 38 });
            toggle ("legato", "Legato", { 564, 70, 86, 22 });
            knob (oid ("transpose"), "Transpose", { 656, 22, 74, 78 }); knob (oid ("tune"), "Tune", { 734, 22, 74, 78 });
            knob (oid ("pSlope"), "Key Follow", { 812, 22, 80, 78 });
            knob (oid ("delay"), "Delay", { 564, 108, 76, 80 }); knob (oid ("pegInt"), "Env Depth", { 646, 108, 76, 80 });
            knob ("porta", "Glide", { 728, 108, 76, 80 }); knob ("bend", "Bend", { 810, 108, 76, 80 });
            // filter
            pc->section ("Filter", { 0, 204, 548, 196 });
            choice (oid ("fType"), "Type", filterTypes, { 8, 228, 200, 38 });
            pc->add (std::make_unique<FilterGraph> (s, o), { 216, 228, 324, 70 });
            knob (oid ("fA"), "Cutoff", { 8, 306, 84, 86 }); knob (oid ("fRes"), "Resonance", { 96, 306, 84, 86 });
            knob (oid ("fB"), "High-Pass", { 184, 306, 84, 86 }); knob (oid ("fKt"), "Keytrack", { 272, 306, 84, 86 });
            knob (oid ("fVelA"), "Velocity", { 360, 306, 84, 86 }); knob (oid ("fTrim"), "Input", { 448, 306, 84, 86 });
            // amp and velocity
            pc->section ("Amp", { 556, 204, 344, 196 });
            knob (oid ("aLvl"), "Level", { 564, 228, 104, 80 }); knob (oid ("aPan"), "Pan", { 674, 228, 104, 80 });
            knob (oid ("aVel"), "Velocity", { 784, 228, 104, 80 });
            knob (oid ("velLo"), "Vel Low", { 564, 312, 104, 80 }); knob (oid ("velHi"), "Vel High", { 674, 312, 104, 80 });
            knob (oid ("velSw"), "Vel Split", { 784, 312, 104, 80 });
            break;
        }
        case PageMod:
        {
            // the envelopes, one at a time
            const char* envTitle[3] = { "Filter Envelope", "Amp Envelope", "Pitch Envelope (both layers)" };
            pc->section (envEdit == 2 ? juce::String (envTitle[2]) : layerName + " - " + envTitle[envEdit], { 0, 0, 492, 400 });
            const char* segName[3] = { "FILTER", "AMP", "PITCH" };
            for (int k = 0; k < 3; ++k)
            {
                auto* seg = pc->add (std::make_unique<juce::TextButton> (segName[k]), { 266 + k * 74, 2, 70, 16 });
                seg->getProperties().set ("small", true);
                seg->setToggleState (k == envEdit, juce::dontSendNotification);
                seg->onClick = [safe, k] { juce::MessageManager::callAsync ([safe, k] { if (safe != nullptr) safe->showEnvelope (k); }); };
            }
            egGraph (envEdit, { 8, 26, 476, 160 });
            auto row = [&] (int r, std::initializer_list<std::pair<juce::String, const char*>> items)
            {
                int k = 0;
                for (auto& it : items) knob (it.first, it.second, { 8 + (k++) * 80, 194 + r * 102, 76, 96 });
            };
            if (envEdit == 0)
            {
                row (0, { { oid ("fegStart"), "Start" }, { oid ("fegAttack"), "Peak" }, { oid ("fegBreak"), "Knee" }, { oid ("fegSustain"), "Sustain" },
                          { oid ("fegRelease"), "End" }, { oid ("fEgA"), "Depth" } });
                row (1, { { oid ("fegAttackT"), "Attack" }, { oid ("fegDecayT"), "Decay" }, { oid ("fegSlopeT"), "Settle" }, { oid ("fegReleaseT"), "Release" },
                          { oid ("fVelA"), "Velocity" }, { oid ("fEgB"), "HP Depth" } });
            }
            else if (envEdit == 1)
            {
                row (0, { { oid ("aegStart"), "Start" }, { oid ("aegAttack"), "Peak" }, { oid ("aegBreak"), "Knee" }, { oid ("aegSustain"), "Sustain" } });
                row (1, { { oid ("aegAttackT"), "Attack" }, { oid ("aegDecayT"), "Decay" }, { oid ("aegSlopeT"), "Settle" }, { oid ("aegReleaseT"), "Release" } });
            }
            else
            {
                row (0, { { "pegStart", "Start" }, { "pegAttack", "Peak" }, { "pegRelease", "End" }, { oscId (0, "pegInt"), "Depth A" }, { oscId (1, "pegInt"), "Depth B" } });
                row (1, { { "pegAttackT", "Attack" }, { "pegDecayT", "Decay" }, { "pegReleaseT", "Release" } });
            }
            // the two LFOs
            for (int l = 0; l < 2; ++l)
            {
                const int y = l * 204;
                auto lid = [o, l] (const char* n) { return oscId (o, (juce::String ("l") + juce::String (l + 1) + n).toRawUTF8()); };
                pc->section (layerName + " - LFO " + juce::String (l + 1), { 500, y, 400, 196 });
                choice (lid ("Wave"), {}, lfoShapeNames(), { 508, y + 26, 150, 24 });
                pc->add (std::make_unique<LfoGraph> (s, lid ("Wave")), { 664, y + 26, 92, 24 });
                toggle (lid ("Sync"), "Sync", { 762, y + 27, 60, 22 });
                choice (lid ("Div"), {}, lfoDivNames(), { 822, y + 26, 70, 24 });
                knob (lid ("Freq"), "Speed", { 508, y + 56, 74, 68 }); knob (lid ("Fade"), "Fade In", { 586, y + 56, 74, 68 });
                knob (lid ("Delay"), "Delay", { 664, y + 56, 74, 68 }); knob (lid ("Ofs"), "Offset", { 742, y + 56, 74, 68 });
                toggle (lid ("Key"), "Restart", { 820, y + 78, 76, 22 });
                const char* pitchId = l == 0 ? "l1Pitch" : "l2Pitch";
                const char* cutId = l == 0 ? "fL1A" : "fL2A";
                const char* ampId = l == 0 ? "aL1" : "aL2";
                knob (oid (pitchId), "To Pitch", { 508, y + 124, 74, 68 }); knob (oid (cutId), "To Cutoff", { 586, y + 124, 74, 68 });
                knob (oid (ampId), "To Level", { 664, y + 124, 74, 68 });
            }
            break;
        }
        case PageEffects:
        {
            for (int n = 0; n < 5; ++n)
            {
                const bool ins = n < 3;
                const int k = ins ? n : n - 3;
                const char* slot = ins ? "ifx" : "mfx";
                const int y = n * 81;
                const int type = paramInt (s, fxId (slot, k, "type"));
                pc->section (ins ? "Insert " + juce::String (k + 1) + "  -  in series" : "Send " + juce::String (k + 1) + "  -  in parallel", { 0, y, 660, 76 });
                toggle (fxId (slot, k, "on"), "On", { 8, y + 34, 46, 22 });
                choice (fxId (slot, k, "type"), {}, effectNames(), { 58, y + 32, 170, 26 });
                const auto names = effectParamNames (type);
                for (int q = 0; q < 5; ++q) knob (fxId (slot, k, ("p" + juce::String (q + 1)).toRawUTF8()), names[q], { 236 + q * 59, y + 20, 57, 56 });
                if (ins) knob (fxId (slot, k, "mix"), "Dry/Wet", { 236 + 5 * 59, y + 20, 57, 56 });
                else { knob (fxId (slot, k, "send"), "Send", { 236 + 5 * 59, y + 20, 57, 56 }); knob (fxId (slot, k, "ret"), "Return", { 236 + 6 * 59, y + 20, 57, 56 }); }
            }
            pc->section ("Master EQ", { 668, 0, 232, 196 });
            knob ("eqLo", "Low", { 676, 24, 104, 82 }); knob ("eqMid", "Mid", { 788, 24, 104, 82 });
            knob ("eqMidHz", "Mid Freq", { 676, 108, 104, 82 }); knob ("eqHi", "High", { 788, 108, 104, 82 });
            pc->section ("Signal Flow", { 668, 204, 232, 196 });
            pc->add (std::make_unique<ChainView> (s), { 678, 230, 212, 162 });
            break;
        }
        case PageWaves:
        {
            pc->section ("Waves", { 0, 0, 280, 400 });
            struct WaveList : juce::Component, juce::ListBoxModel
            {
                TidebreezeAudioProcessor& p; int& focus; std::function<void()> changed;
                juce::ListBox list;
                WaveList (TidebreezeAudioProcessor& pr, int& f) : p (pr), focus (f)
                {
                    list.setModel (this); list.setRowHeight (22); addAndMakeVisible (list);
                    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
                    list.selectRow (focus);
                }
                int getNumRows() override { return numWaveChoices(); }
                void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
                {
                    if (selected)
                    {
                        g.setColour (Palette::phMid().withAlpha (0.22f)); g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f, 3.0f);
                        g.setColour (Palette::phBright()); g.fillRect (2.0f, 4.0f, 2.0f, (float) h - 8.0f);
                    }
                    const bool factory = row < numFactoryWaves();
                    g.setFont (LookAndFeel::crt (11.5f, true));
                    glowText (g, juce::String (row + 1).paddedLeft ('0', 2) + "  " + p.waveName (row).toUpperCase(), { 8.0f, 0.0f, (float) w - 76.0f, (float) h },
                              juce::Justification::centredLeft, factory ? Palette::phBright() : Palette::phMid(), selected ? 1.0f : 0.85f);
                    g.setFont (LookAndFeel::crt (9.5f));
                    g.setColour (Palette::phDim());
                    g.drawText (p.bank.groupName (row).toUpperCase(), w - 70, 0, 64, h, juce::Justification::centredRight);
                }
                void selectedRowsChanged (int row) override { if (row >= 0) { focus = row; if (changed) changed(); } }
                void resized() override { list.setBounds (getLocalBounds()); }
            };
            auto& pr = proc;
            int& focus = waveFocus;
            focus = juce::jlimit (0, numWaveChoices() - 1, focus);
            const bool userFocus = WaveBank::isUser (focus);
            const int focusSlot = userFocus ? WaveBank::slotOf (focus) : -1;
            auto* wl = pc->add (std::make_unique<WaveList> (pr, focus), { 6, 24, 268, 370 });
            wl->list.scrollToEnsureRowIsOnscreen (focus);
            pc->add (std::make_unique<WaveView> (pr, [&focus] { return focus; }), { 288, 0, 612, 228 });
            // choosing another wave rebuilds the controls below (they depend on what it is)
            wl->changed = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->buildPage(); }); };
            pc->section ("New waves", { 288, 236, 612, 164 });

            // roll one from a recipe
            auto* arch = pc->add (std::make_unique<juce::ComboBox>(), { 296, 262, 192, 26 });
            for (int a = 0; a < NumArchetypes; ++a) arch->addItem (archetypeName (a), a + 1);
            arch->setSelectedId (juce::jlimit (1, (int) NumArchetypes, lastArchetype + 1), juce::dontSendNotification);
            arch->onChange = [this, arch] { lastArchetype = arch->getSelectedId() - 1; };
            auto* roll = pc->add (std::make_unique<juce::TextButton> ("ROLL WAVE"), { 494, 262, 112, 26 });
            roll->onClick = [safe, arch, &pr, &focus]
            {
                if (safe == nullptr) return;
                focus = pr.rollWave (arch->getSelectedId() - 1);
                // the page (and this button) is rebuilt after the click returns
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->buildPage(); });
            };
            // or load an audio file: into the user slot shown, else the next free (or oldest) one
            auto* load = pc->add (std::make_unique<juce::TextButton> (userFocus ? "LOAD FILE > U" + juce::String (focusSlot + 1) : juce::String ("LOAD FILE")),
                                  { 612, 262, 146, 26 });
            load->onClick = [safe, focusSlot] { juce::MessageManager::callAsync ([safe, focusSlot] { if (safe != nullptr) safe->loadWaveFile (focusSlot); }); };
            auto* audition = pc->add (std::make_unique<juce::TextButton> ("AUDITION C4"), { 764, 262, 128, 26 });
            audition->onClick = [&pr]
            {
                pr.keyboardState.noteOn (1, 60, 0.8f);
                juce::Timer::callAfterDelay (1600, [&pr] { pr.keyboardState.noteOff (1, 60, 0.0f); });
            };

            const int layer = o;
            auto assign = [&pr, &focus, layer] (const char* which)
            {
                if (auto* p = pr.apvts.getParameter (oscId (layer, which))) p->setValueNotifyingHost (p->convertTo0to1 ((float) focus));
            };
            auto* toHigh = pc->add (std::make_unique<juce::TextButton> ("USE IN " + layerName.toUpperCase() + " (HIGH)"), { 296, 296, 220, 26 });
            auto* toLow = pc->add (std::make_unique<juce::TextButton> ("USE IN " + layerName.toUpperCase() + " (LOW)"), { 522, 296, 220, 26 });
            toHigh->onClick = [assign] { assign ("hiWave"); };
            toLow->onClick = [assign] { assign ("loWave"); };
            auto* clear = pc->add (std::make_unique<juce::TextButton> ("CLEAR SLOT"), { 748, 296, 144, 26 });
            clear->setEnabled (userFocus && pr.bank.slot (focusSlot).used());
            clear->onClick = [safe, &pr, focusSlot]
            {
                if (focusSlot < 0) return;
                pr.clearUserWave (focusSlot);
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->buildPage(); });
            };

            const auto& slot = pr.bank.slot (juce::jmax (0, focusSlot));
            if (userFocus && slot.kind == WaveBank::Slot::Sample && slot.sample != nullptr)
            {
                // a loaded wave: where it sounds at its own pitch, and its loop
                struct SampleControls : juce::Component
                {
                    TidebreezeAudioProcessor& p; int slotIndex;
                    juce::ComboBox root, loop;
                    juce::Slider start { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                                 xfade { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
                    juce::String info;
                    SampleControls (TidebreezeAudioProcessor& pr, int s) : p (pr), slotIndex (s)
                    {
                        const auto& u = p.bank.slot (s);
                        for (int n = 0; n < 128; ++n)
                        {
                            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                            root.addItem (juce::String (names[n % 12]) + juce::String (n / 12 - 1), n + 1);
                        }
                        root.setSelectedId (u.settings.rootNote + 1, juce::dontSendNotification);
                        loop.addItemList (loopModeNames(), 1);
                        loop.setSelectedId (u.settings.loopMode + 1, juce::dontSendNotification);
                        for (auto* sl : { &start, &xfade })
                        {
                            sl->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
                            sl->setMouseDragSensitivity (180);
                            addAndMakeVisible (*sl);
                        }
                        start.setRange (2.0, 95.0, 0.1);
                        start.setValue (u.settings.loopStart * 100.0, juce::dontSendNotification);
                        xfade.setRange (0.0, 500.0, 1.0);
                        xfade.setSkewFactorFromMidPoint (80.0);
                        xfade.setValue (u.settings.xfadeMs, juce::dontSendNotification);
                        for (auto* c : { &root, &loop }) { addAndMakeVisible (*c); c->onChange = [this] { apply(); }; }
                        for (auto* sl : { &start, &xfade }) sl->onValueChange = [this] { apply(); };
                        const auto& d = *u.sample;
                        info = d.fileName.toUpperCase() + "\n" + juce::String ((double) d.audio.size() / d.rate, 2) + " S  " + juce::String (d.rate / 1000.0, 1) + " KHZ";
                        showLoop();
                    }
                    void showLoop() { start.setEnabled (loop.getSelectedId() - 1 == LoopSustain); xfade.setEnabled (start.isEnabled()); repaint(); }
                    void apply()
                    {
                        SampleSettings st;
                        st.rootNote = juce::jlimit (0, 127, root.getSelectedId() - 1);
                        st.loopMode = juce::jlimit (0, NumLoopModes - 1, loop.getSelectedId() - 1);
                        st.loopStart = (float) start.getValue() * 0.01f;
                        st.xfadeMs = (float) xfade.getValue();
                        p.setUserWaveSettings (slotIndex, st);
                        showLoop();
                    }
                    void resized() override
                    {
                        root.setBounds (0, 18, 84, 24);
                        loop.setBounds (92, 18, 110, 24);
                        start.setBounds (212, 12, 60, 40);
                        xfade.setBounds (282, 12, 60, 40);
                    }
                    void paint (juce::Graphics& g) override
                    {
                        g.setColour (Palette::phDim());
                        g.setFont (LookAndFeel::crt (9.5f, true));
                        g.drawText ("ROOT KEY", 0, 0, 84, 14, juce::Justification::centredLeft);
                        g.drawText ("LOOP", 92, 0, 110, 14, juce::Justification::centredLeft);
                        g.drawText ("LOOP START", 202, 0, 80, 12, juce::Justification::centred);
                        g.drawText ("CROSSFADE", 272, 0, 80, 12, juce::Justification::centred);
                        g.setFont (LookAndFeel::crt (10.5f, true));
                        const float a = start.isEnabled() ? 1.0f : 0.4f;
                        glowText (g, juce::String (start.getValue(), 1) + " %", { 202.0f, 52.0f, 80.0f, 13.0f }, juce::Justification::centred, Palette::phBright(), a);
                        glowText (g, juce::String ((int) xfade.getValue()) + " MS", { 272.0f, 52.0f, 80.0f, 13.0f }, juce::Justification::centred, Palette::phBright(), a);
                        g.setFont (LookAndFeel::crt (9.5f, true));
                        g.setColour (Palette::phMid());
                        g.drawFittedText (info, juce::Rectangle<int> (356, 4, getWidth() - 356, 56), juce::Justification::topLeft, 3);
                    }
                };
                pc->add (std::make_unique<SampleControls> (pr, focusSlot), { 296, 330, 596, 66 });
            }
            else
            {
                auto* note = pc->add (std::make_unique<juce::Label> ("", "Rolled and loaded waves go into the 16 user slots and are saved with the preset: "
                                                                        "a rolled one as its type and seed, a loaded file as its audio (mono, up to "
                                                                        + juce::String ((int) WaveBank::kMaxSampleSeconds) + " s). Pick a user slot to load into it."),
                                     { 296, 330, 596, 62 });
                note->setFont (LookAndFeel::crt (10.5f));
                note->setColour (juce::Label::textColourId, Palette::phDim());
                note->setJustificationType (juce::Justification::topLeft);
            }
            break;
        }
        default: break;
    }

    if (pageComp != nullptr) removeChildComponent (pageComp.get());
    pageComp = std::move (pc);
    addAndMakeVisible (*pageComp);
    resized();
}
