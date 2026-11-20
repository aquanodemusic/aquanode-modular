#include "PluginEditor.h"

namespace covegroove
{
    juce::Colour colours::lane (int i)
    {
        // from deep blue through cyan to green
        static const juce::uint32 c[kNumLanes] = { 0xff3d7bff, 0xff2a9dff, 0xff14c3ff, 0xff00e1f0,
                                                   0xff1ee8c0, 0xff22e08d, 0xff52e05a, 0xff9be83a };
        return juce::Colour (c[juce::jlimit (0, kNumLanes - 1, i)]);
    }

    namespace
    {
        juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    }

    //==========================================================================
    CgLookAndFeel::CgLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, colours::slot);
        setColour (juce::ComboBox::textColourId, colours::cream);
        setColour (juce::ComboBox::outlineColourId, colours::edge);
        setColour (juce::ComboBox::arrowColourId, colours::cyan);
        setColour (juce::PopupMenu::backgroundColourId, colours::plate);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::headerTextColourId, colours::cyan);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::cyan.withAlpha (0.35f));
        setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, colours::text);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::BubbleComponent::backgroundColourId, colours::plateHi);
        setColour (juce::BubbleComponent::outlineColourId, colours::edge);
        setColour (juce::TooltipWindow::backgroundColourId, colours::plateHi);
        setColour (juce::TooltipWindow::textColourId, colours::text);
    }

    // AlphaBeta's knob, made rugged: a brushed-steel cap on a knurled dark steel
    // skirt, inside a dark scale that lights in the lane's colour (from the
    // middle on the bipolar knobs: pan, tune, tone), a bright dot at its end
    void CgLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
    {
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 1.0f;
        const auto c = r.getCentre();
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        const float a = start + pos * (end - start);
        using PST = juce::PathStrokeType;

        // the scale
        const float arcW = juce::jlimit (2.4f, 3.8f, rad * 0.16f);
        const float arcR = rad - arcW * 0.5f - 0.6f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
        g.setColour (juce::Colour (0xff120e0c));
        g.strokePath (track, PST (arcW + 1.4f, PST::curved, PST::butt));
        g.setColour (juce::Colour (0xff3c3633));
        g.strokePath (track, PST (arcW, PST::curved, PST::butt));
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? (start + end) * 0.5f : start;
        if (std::abs (a - from) > 0.005f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, a), juce::jmax (from, a), true);
            juce::ColourGradient vg (col.darker (0.4f), c.x - arcR, c.y, col.brighter (0.5f), c.x + arcR, c.y, false);
            vg.addColour (0.45, col);
            g.setGradientFill (vg);
            g.strokePath (value, PST (arcW, PST::curved, PST::butt));
        }
        const juce::Point<float> tip (c.x + arcR * std::sin (a), c.y - arcR * std::cos (a));
        g.setColour (col.withAlpha (0.28f));
        g.fillEllipse (juce::Rectangle<float> (arcW * 2.8f, arcW * 2.8f).withCentre (tip));
        g.setColour (col.brighter (0.7f));
        g.fillEllipse (juce::Rectangle<float> (arcW * 1.25f, arcW * 1.25f).withCentre (tip));

        // the knurled skirt
        const float kr = juce::jmax (5.0f, arcR - arcW * 0.5f - 2.4f);
        const auto body = juce::Rectangle<float> (kr * 2.0f, kr * 2.0f).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (body.translated (0.8f, 1.9f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff70747c), body.getX(), body.getY(),
                                                 juce::Colour (0xff1f2126), body.getRight(), body.getBottom(), false));
        g.fillEllipse (body);
        const int ridges = juce::jlimit (18, 40, (int) (kr * 2.2f));
        for (int i = 0; i < ridges; ++i)
        {
            const float t = kTwoPi * (float) i / (float) ridges;
            const float sn = std::sin (t), cs = std::cos (t);
            const float lit = 0.5f + 0.5f * 0.7071f * (cs - sn);          // light from the upper left
            const float half = kTwoPi * 0.25f / (float) ridges;
            const float s2 = std::sin (t + half), c2 = std::cos (t + half);
            g.setColour (juce::Colours::white.withAlpha (0.06f + 0.30f * lit));
            g.drawLine (c.x + sn * kr * 0.80f, c.y - cs * kr * 0.80f, c.x + sn * kr * 0.97f, c.y - cs * kr * 0.97f, 0.9f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawLine (c.x + s2 * kr * 0.80f, c.y - c2 * kr * 0.80f, c.x + s2 * kr * 0.97f, c.y - c2 * kr * 0.97f, 0.9f);
        }
        g.setColour (juce::Colour (0xff0e0f12));
        g.drawEllipse (body, 1.0f);

        // the steel cap
        const float cr = kr * 0.74f;
        const auto cap = juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (cap.translated (0.0f, 0.9f));
        juce::ColourGradient steel (juce::Colour (0xffd8dce6), c.x - cr * 0.4f, c.y - cr * 0.5f,
                                    juce::Colour (0xff5e6570), c.x + cr * 0.5f, c.y + cr * 0.6f, false);
        steel.addColour (0.45, juce::Colour (0xffb0b8c4));
        steel.addColour (0.70, juce::Colour (0xff747d8a));
        g.setGradientFill (steel);
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillEllipse (c.x - cr * 0.7f, c.y - cr * 0.75f, cr * 0.9f, cr * 0.65f);
        g.setColour (juce::Colour (0xff3a3e4a));
        g.drawEllipse (cap, 1.1f);

        // the pointer, across the cap and the skirt
        const float sa = std::sin (a), ca = std::cos (a);
        const juce::Line<float> line (c.x + sa * cr * 0.22f, c.y - ca * cr * 0.22f, c.x + sa * kr * 0.95f, c.y - ca * kr * 0.95f);
        const float lw = juce::jmax (1.6f, cr * 0.2f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine (line, lw + 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.drawLine (line, lw);
        g.setColour (col);
        g.fillEllipse (juce::Rectangle<float> (cr * 0.42f, cr * 0.42f).withCentre (c));
    }

    // raised keys; a toggled key sits lower and its lamp glows
    void CgLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f, 0.5f);
        const bool on = b.getToggleState();
        const bool sunk = on || down;
        const auto lamp = b.findColour (juce::TextButton::buttonOnColourId);
        auto top = colours::plateHi.brighter (over ? 0.22f : 0.12f), bottom = colours::plate.darker (0.25f);
        if (sunk) std::swap (top, bottom);
        if (on) { top = top.interpolatedWith (lamp, 0.18f); bottom = bottom.interpolatedWith (lamp, 0.22f); }
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0.0f, sunk ? 0.5f : 1.5f), 1.5f);
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 1.5f);
        if (! sunk)
        {
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.drawLine (r.getX() + 1.5f, r.getY() + 1.0f, r.getRight() - 1.5f, r.getY() + 1.0f, 1.0f);
        }
        g.setColour (colours::edge);
        g.drawRoundedRectangle (r, 1.5f, 1.0f);
        if (b.getClickingTogglesState() && r.getWidth() > 30.0f)
        {
            const float d = juce::jmin (5.0f, r.getHeight() * 0.25f);
            const juce::Rectangle<float> led (r.getRight() - d - 3.0f, r.getY() + 3.0f, d, d);
            if (on)
            {
                g.setColour (lamp.withAlpha (0.35f));
                g.fillRect (led.expanded (1.5f));
                g.setColour (lamp.brighter (0.4f));
            }
            else
                g.setColour (colours::slot);
            g.fillRect (led);
        }
    }

    void CgLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));
        auto r = b.getLocalBounds();
        if (down || b.getToggleState()) r.translate (0, 1);
        g.drawText (b.getButtonText(), r, juce::Justification::centred, false);
    }

    juce::Font CgLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (13.0f, (float) h * 0.55f), true); }

    // a recessed window with warm lettering
    void CgLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRect (r);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (r.withHeight (2.0f));
        g.setColour (colours::edge);
        g.drawRect (r, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawLine (r.getX(), r.getBottom() + 0.2f, r.getRight(), r.getBottom() + 0.2f, 1.0f);
        juce::Path p;
        const float ax = (float) w - 9.0f, ay = (float) h * 0.5f;
        p.addTriangle (ax - 3.5f, ay - 1.5f, ax + 3.5f, ay - 1.5f, ax, ay + 2.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (p);
    }

    juce::Font CgLookAndFeel::getComboBoxFont (juce::ComboBox& b) { return font (juce::jmin (13.0f, (float) b.getHeight() * 0.6f)); }

    void CgLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (4, 1, box.getWidth() - 18, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void CgLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
    {
        g.fillAll (colours::plate);
        g.setColour (colours::edge);
        g.drawRect (0, 0, w, h, 1);
    }

    //==========================================================================
    Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& n, juce::Colour c)
        : name (n)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        slider.setMouseDragSensitivity (180);
        addAndMakeVisible (slider);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
        if (auto* prm = s.getParameter (paramId))
        {
            slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
            slider.setTooltip (prm->getName (64));
        }
    }

    void Knob::resized()
    {
        auto r = getLocalBounds();
        if (name.isNotEmpty()) r.removeFromBottom (12);
        slider.setBounds (r);
    }

    void Knob::paint (juce::Graphics& g)
    {
        if (name.isEmpty()) return;
        g.setColour (colours::dim);
        g.setFont (font (10.0f));
        g.drawText (name, getLocalBounds().removeFromBottom (12), juce::Justification::centred, false);
    }

    //==========================================================================
    void SampleEditButton::paintButton (juce::Graphics& g, bool over, bool down)
    {
        getLookAndFeel().drawButtonBackground (g, *this, {}, over, down);
        // a sample fading under its curve
        const auto r = getLocalBounds().toFloat().reduced (3.0f, 4.0f).translated (0.0f, down ? 1.0f : 0.0f);
        juce::Path p;
        const int n = 9;
        for (int i = 0; i <= n; ++i)
        {
            const float u = (float) i / (float) n;
            const float a = (1.0f - u) * 0.5f * r.getHeight() * (i % 2 == 0 ? -1.0f : 1.0f);
            const juce::Point<float> pt (r.getX() + u * r.getWidth(), r.getCentreY() + a);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (colour.withAlpha (over ? 1.0f : 0.85f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    //==========================================================================
    StepGrid::StepGrid (CoveGrooveProcessor& p) : proc (p)
    {
        shown = proc.getPattern();
        startTimerHz (30);
    }

    juce::Rectangle<float> StepGrid::cellRect (int lane, int step) const
    {
        const int L = shown.length();
        const float w = (float) getWidth() / (float) L;
        const float h = (float) rowHeight();
        return { (float) step * w, (float) lane * h, w, h };
    }

    juce::Rectangle<float> StepGrid::slotRect (int lane, int step) const
    {
        const auto c = cellRect (lane, step);
        return c.reduced (c.getWidth() > 14.0f ? 1.5f : 0.6f, 3.0f);
    }

    // the level from the height in the slot: the bottom is soft, the top full
    uint8_t StepGrid::velocityAt (int lane, float y) const
    {
        const auto s = slotRect (lane, 0);
        const float frac = juce::jlimit (0.0f, 1.0f, (s.getBottom() - y) / juce::jmax (1.0f, s.getHeight()));
        return (uint8_t) juce::jlimit (20, 127, (int) std::lround (20.0f + 107.0f * frac));
    }

    void StepGrid::paint (juce::Graphics& g)
    {
        const int L = shown.length();
        const float w = (float) getWidth() / (float) L;
        // (the lanes' strips show between the steps)

        for (int lane = 0; lane < kNumLanes; ++lane)
        {
            const auto col = colours::lane (lane);
            for (int s = 0; s < L; ++s)
            {
                const auto r = slotRect (lane, s);
                const auto& st = shown.lanes[(size_t) lane][(size_t) s];
                // the slot: every other group of four a shade lighter, a hint of the lane's colour in it
                const bool lightGroup = ((s / 4) % 2) == 0;
                g.setColour ((lightGroup ? colours::slot.brighter (0.16f) : colours::slot.brighter (0.05f)).interpolatedWith (col, 0.12f));
                g.fillRect (r);
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.2f);
                if (st.vel > 0)
                {
                    // the bar: one flat colour, as tall as the step is loud
                    const float v = (float) st.vel / 127.0f;
                    const auto bar = r.withTop (r.getBottom() - r.getHeight() * (0.12f + 0.88f * v));
                    g.setColour (col);
                    g.fillRect (bar);
                    if (st.alt)
                    {
                        // the second note: a cream notch at the top of the bar
                        const float dd = juce::jlimit (3.0f, 7.0f, bar.getWidth() * 0.5f);
                        juce::Path tri;
                        tri.addTriangle (bar.getCentreX() - dd * 0.6f, bar.getY() + 1.5f, bar.getCentreX() + dd * 0.6f, bar.getY() + 1.5f, bar.getCentreX(), bar.getY() + 1.5f + dd);
                        g.setColour (colours::cream);
                        g.fillPath (tri);
                    }
                }
            }
        }
        // the bars
        g.setColour (colours::cyan.withAlpha (0.45f));
        for (int b = 1; b < shown.bars; ++b)
        {
            const float x = (float) (b * kStepsPerBar) * w;
            g.fillRect (x - 0.75f, 2.0f, 1.5f, (float) getHeight() - 4.0f);
        }
        // the playhead
        if (shownStep >= 0 && shownStep < L)
        {
            const auto col = cellRect (0, shownStep).withHeight ((float) getHeight());
            g.setColour (colours::cyan.withAlpha (0.13f));
            g.fillRect (col);
            g.setColour (colours::cyan.withAlpha (0.85f));
            g.fillRect (col.getX() + 1.0f, 0.0f, col.getWidth() - 2.0f, 2.0f);
        }
        // the level being set
        if (showVelLane >= 0 && showVelStep >= 0 && showVelStep < L)
        {
            const auto& st = shown.lanes[(size_t) showVelLane][(size_t) showVelStep];
            const auto c = cellRect (showVelLane, showVelStep);
            auto tag = juce::Rectangle<float> (34.0f, 16.0f).withCentre ({ c.getCentreX(), c.getY() - 2.0f });
            tag = tag.withX (juce::jlimit (0.0f, (float) getWidth() - tag.getWidth(), tag.getX())).withY (juce::jmax (0.0f, tag.getY()));
            g.setColour (colours::plateHi.withAlpha (0.95f));
            g.fillRect (tag);
            g.setColour (colours::edge);
            g.drawRect (tag, 1.0f);
            g.setColour (colours::text);
            g.setFont (font (11.0f, true));
            g.drawText (juce::String ((int) st.vel), tag, juce::Justification::centred, false);
        }
    }

    bool StepGrid::cellAt (juce::Point<int> pos, int& lane, int& step) const
    {
        const int L = shown.length();
        if (getWidth() <= 0 || getHeight() <= 0) return false;
        lane = juce::jlimit (0, kNumLanes - 1, pos.y / juce::jmax (1, rowHeight()));
        step = juce::jlimit (0, L - 1, (int) ((float) pos.x / ((float) getWidth() / (float) L)));
        return pos.x >= 0 && pos.y >= 0 && pos.x < getWidth() && pos.y < getHeight();
    }

    void StepGrid::mouseDown (const juce::MouseEvent& e)
    {
        int lane, step;
        gesture = Gesture::None;
        if (! cellAt (e.getPosition(), lane, step)) return;
        editing = proc.getPattern();
        auto& st = editing.lanes[(size_t) lane][(size_t) step];
        if (e.mods.isPopupMenu() || e.mods.isAltDown())
        {
            // the second note / accent
            if (st.vel == 0) st.vel = 100;
            st.alt = ! st.alt;
            proc.setPattern (editing);
            return;
        }
        pressLane = lane; pressStep = step;
        pressPos = e.getPosition();
        pressTime = juce::Time::getMillisecondCounter();
        longPressDone = false;
        pressWasOn = st.vel > 0;
        if (! pressWasOn)
        {
            st = { velocityAt (lane, (float) e.y), false };
            proc.setPattern (editing);
        }
        paintVel = st.vel;
        gesture = Gesture::Undecided;
    }

    void StepGrid::mouseDrag (const juce::MouseEvent& e)
    {
        if (gesture == Gesture::None || pressLane < 0) return;
        const auto d = e.getPosition() - pressPos;
        if (gesture == Gesture::Undecided)
        {
            if (std::abs (d.x) < 6 && std::abs (d.y) < 6) return;
            if (std::abs (d.y) > std::abs (d.x)) gesture = Gesture::Velocity;
            else
            {
                gesture = Gesture::Paint;
                paintOn = ! pressWasOn;            // from an empty slot: paint steps; from a step: clear them
                if (pressWasOn) { editing.lanes[(size_t) pressLane][(size_t) pressStep] = Step(); proc.setPattern (editing); }
            }
        }
        if (gesture == Gesture::Velocity)
        {
            auto& st = editing.lanes[(size_t) pressLane][(size_t) pressStep];
            const uint8_t v = velocityAt (pressLane, (float) e.y);
            if (st.vel != v) { st.vel = v; proc.setPattern (editing); }
            showVelLane = pressLane; showVelStep = pressStep;
            repaint();
        }
        else if (gesture == Gesture::Paint)
        {
            int lane, step;
            cellAt ({ juce::jlimit (0, getWidth() - 1, e.x), pressLane * rowHeight() + 1 }, lane, step);
            const int a = juce::jmin (step, pressStep), b = juce::jmax (step, pressStep);
            bool changed = false;
            for (int s = a; s <= b; ++s)
            {
                auto& st = editing.lanes[(size_t) pressLane][(size_t) s];
                const Step want = paintOn ? Step { paintVel, false } : Step();
                if ((st.vel > 0) != paintOn) { st = want; changed = true; }
            }
            if (changed) proc.setPattern (editing);
        }
    }

    void StepGrid::mouseUp (const juce::MouseEvent&)
    {
        // a tap on a step that played: off
        if (gesture == Gesture::Undecided && pressWasOn && ! longPressDone && pressLane >= 0)
        {
            editing.lanes[(size_t) pressLane][(size_t) pressStep] = Step();
            proc.setPattern (editing);
        }
        gesture = Gesture::None;
        pressLane = -1;
        if (showVelLane >= 0) { showVelLane = showVelStep = -1; repaint(); }
    }

    void StepGrid::timerCallback()
    {
        // a long press: the second note / accent
        if (gesture == Gesture::Undecided && ! longPressDone && pressLane >= 0
            && juce::Time::getMillisecondCounter() - pressTime > 450)
        {
            auto& st = editing.lanes[(size_t) pressLane][(size_t) pressStep];
            if (st.vel == 0) st.vel = 100;
            st.alt = ! st.alt;
            longPressDone = true;
            gesture = Gesture::None;
            proc.setPattern (editing);
        }
        const int v = proc.patternVersion.load();
        const int s = proc.playStep.load();
        if (v != shownVersion) { shownVersion = v; shown = proc.getPattern(); repaint(); }
        if (s != shownStep) { shownStep = s; repaint(); }
    }

    //==========================================================================
    //==========================================================================
    SamplePanel::SamplePanel (CoveGrooveProcessor& p) : proc (p)
    {
        curveEditor.well = colours::slot;
        curveEditor.faint = colours::dim;
        curveEditor.emptyText = "No sample in this lane - LOAD a sound file (WAV, AIFF, FLAC, Ogg, MP3; up to 10 s)";
        curveEditor.onChange = [this] (const Curve& c) { proc.setLaneCurve (lane, c); };
        addAndMakeVisible (curveEditor);
        auto lamp = [this] (juce::TextButton& b, juce::Colour c)
        {
            b.setColour (juce::TextButton::buttonOnColourId, c);
            addAndMakeVisible (b);
        };
        lamp (loadButton, colours::green);
        loadButton.setColour (juce::TextButton::textColourOffId, colours::green);
        loadButton.setTooltip ("A sound file of your own for this lane (it then plays it)");
        loadButton.onClick = [this] { chooseFile(); };
        lamp (clearButton, colours::coral);
        clearButton.setTooltip ("Remove the lane's sample");
        clearButton.onClick = [this] { proc.clearLaneSample (lane); };
        lamp (playButton, colours::cyan);
        playButton.setTooltip ("Play the lane once");
        playButton.onClick = [this] { proc.audition (lane); };
        lamp (closeButton, colours::cyan);
        closeButton.onClick = [this] { setVisible (false); };
        for (int i = 0; i < samples::kNumShapes; ++i)
        {
            auto& b = shapeButtons[(size_t) i];
            b.setButtonText (juce::String (samples::shapeName (i)).toUpperCase());
            b.setTooltip ("Set the curve to " + juce::String (samples::shapeName (i)).toLowerCase());
            b.onClick = [this, i] { proc.setLaneCurve (lane, samples::shape (i)); };
            lamp (b, colours::cyan);
        }
        setVisible (false);
    }

    void SamplePanel::show (int l)
    {
        lane = juce::jlimit (0, kNumLanes - 1, l);
        shownVersion = shownModel = -1;
        curveEditor.accent = colours::lane (lane);
        refresh();
        setVisible (true);
        toFront (false);
        startTimerHz (10);
    }

    void SamplePanel::refresh()
    {
        const auto ls = proc.getLaneSample (lane);
        curveEditor.setSample (ls.data);
        curveEditor.setCurve (ls.curve);
        shownVersion = proc.sampleVersion.load();
        shownModel = (int) proc.apvts.getRawParameterValue (CoveGrooveProcessor::laneParam (lane, "model"))->load();
        if (ls.data == nullptr) info = {};
        else
        {
            info = (ls.name.isNotEmpty() ? ls.name + "   " : juce::String()) + samples::describe (*ls.data);
            if (shownModel != kSampleModel)
                info = "plays " + juce::String (modelName (lane, shownModel)) + " now (Sound: Sample plays this)   " + info;
        }
        clearButton.setEnabled (ls.data != nullptr);
        repaint();
    }

    void SamplePanel::timerCallback()
    {
        if (! isVisible()) { stopTimer(); return; }
        const int model = (int) proc.apvts.getRawParameterValue (CoveGrooveProcessor::laneParam (lane, "model"))->load();
        if (proc.sampleVersion.load() != shownVersion || model != shownModel) refresh();
    }

    void SamplePanel::chooseFile()
    {
        chooser = std::make_shared<juce::FileChooser> ("A sample for the " + juce::String (laneName (lane)) + " lane",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<SamplePanel> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (! f.existsAsFile()) return;
                                  if (! safe->proc.loadLaneSample (safe->lane, f))
                                  {
                                      safe->info = "Could not read " + f.getFileName() + " (or it is silent)";
                                      safe->repaint();
                                  }
                              });
    }

    void SamplePanel::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const auto col = colours::lane (lane);
        g.setGradientFill (juce::ColourGradient (colours::plateHi, 0.0f, r.getY(), colours::plate.darker (0.2f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (col.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (1.0f), 2.0f, 1.6f);
        auto top = getLocalBounds().reduced (12, 8).removeFromTop (18);
        g.setFont (font (13.0f, true));
        g.setColour (col.brighter (0.3f));
        const juce::String title = juce::String (laneName (lane)).toUpperCase() + "   SAMPLE";
        g.drawText (title, top, juce::Justification::centredLeft, false);
        g.setFont (font (11.0f));
        g.setColour (colours::text);
        g.drawText (info, top.withTrimmedLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (13.0f, true), title)) + 16),
                    juce::Justification::centredRight, true);
        g.setColour (colours::dim);
        g.setFont (font (10.0f));
        g.drawText ("VOLUME CURVE   tap: a new point   drag: move it   long-press / double-tap / right-click: remove it",
                    getLocalBounds().reduced (12, 0).removeFromBottom (44).removeFromTop (14), juce::Justification::centredLeft, true);
    }

    void SamplePanel::resized()
    {
        auto r = getLocalBounds().reduced (12, 8);
        r.removeFromTop (22);
        auto bottom = r.removeFromBottom (24);
        r.removeFromBottom (18);
        curveEditor.setBounds (r);
        loadButton.setBounds (bottom.removeFromLeft (56));
        bottom.removeFromLeft (4);
        clearButton.setBounds (bottom.removeFromLeft (56));
        closeButton.setBounds (bottom.removeFromRight (60));
        bottom.removeFromRight (4);
        playButton.setBounds (bottom.removeFromRight (56));
        bottom.removeFromRight (10);
        bottom.removeFromLeft (10);
        const int w = (bottom.getWidth() - 4 * (samples::kNumShapes - 1)) / samples::kNumShapes;
        for (auto& b : shapeButtons) { b.setBounds (bottom.removeFromLeft (w)); bottom.removeFromLeft (4); }
    }


    //==========================================================================
    PhysRow::PhysRow (CoveGrooveProcessor& p) : proc (p)
    {
        for (int l = 0; l < kNumLanes; ++l) laneBox.addItem (laneName (l), l + 1);
        laneBox.setSelectedId (1, juce::dontSendNotification);
        laneBox.setTooltip ("The lane shown here (tap a lane's name to show it)");
        laneBox.onChange = [this] { setLane (laneBox.getSelectedId() - 1); };
        addAndMakeVisible (laneBox);
        modelBox.setTooltip ("Which of the kit's sounds the lane plays (Lane Default: the kit's own sound for this lane)");
        modelBox.onChange = [this]
        {
            const int id = modelBox.getSelectedId();
            if (id <= 0) return;
            if (auto* prm = proc.apvts.getParameter (CoveGrooveProcessor::laneParam (lane, "pm")))
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (id - 1)));
        };
        addAndMakeVisible (modelBox);
        hint.setFont (font (10.0f));
        hint.setMinimumHorizontalScale (0.9f);
        hint.setColour (juce::Label::textColourId, colours::dim);
        hint.setJustificationType (juce::Justification::centredLeft);
        hint.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (hint);
        rebuild();
        startTimerHz (10);
    }

    void PhysRow::setLane (int l)
    {
        l = juce::jlimit (0, kNumLanes - 1, l);
        laneBox.setSelectedId (l + 1, juce::dontSendNotification);
        if (l == lane) return;
        lane = l;
        shownKit = -2;
        lastKey.clear();
        rebuild();
    }

    // the controls for the lane's sound: the kit's sounds and its model's knobs
    void PhysRow::rebuild()
    {
        const int model = (int) proc.apvts.getRawParameterValue (CoveGrooveProcessor::laneParam (lane, "model"))->load();
        const int choice = (int) proc.apvts.getRawParameterValue (CoveGrooveProcessor::laneParam (lane, "pm"))->load();
        const int kit = isPhysModel (model) ? physKitOf (model) : -1;
        shownKit = kit;
        shownChoice = choice;
        modelBox.clear (juce::dontSendNotification);
        for (auto& k : knobs) k.reset();
        if (kit >= 0)
        {
            const auto* def = phys::presetFor (kit, lane, 0);
            modelBox.addItem ("Lane Default" + (def != nullptr ? juce::String (" (") + phys::shortName (*def, kit) + ")" : juce::String()), 1);
            const auto& list = phys::kitPresets (kit);
            for (size_t i = 0; i < list.size() && i < 127; ++i) modelBox.addItem (phys::shortName (*list[i], kit), (int) i + 2);
            modelBox.setSelectedId (juce::jlimit (0, (int) list.size(), choice) + 1, juce::dontSendNotification);
            modelBox.setEnabled (true);
            const auto names = phys::knobNames (phys::presetFor (kit, lane, choice));
            for (int k = 0; k < 3; ++k)
            {
                if (names[(size_t) k].empty()) continue;
                knobs[(size_t) k] = std::make_unique<Knob> (proc.apvts, CoveGrooveProcessor::laneParam (lane, ("x" + std::to_string (k + 1)).c_str()),
                                                            juce::String (names[(size_t) k]), colours::lane (lane));
                addAndMakeVisible (*knobs[(size_t) k]);
            }
            hint.setText ({}, juce::dontSendNotification);
        }
        else
        {
            modelBox.setTextWhenNothingSelected ("-");
            modelBox.setEnabled (false);
            hint.setText ("Modelled kits (808, 909, Classic, Electro, Large Drums, Metal, Orchestral, World): "
                          "choose one in a lane's Sound menu or KIT to tweak it here.", juce::dontSendNotification);
        }
        resized();
        repaint();
    }

    void PhysRow::resized()
    {
        auto r = getLocalBounds().reduced (12, 6);
        auto left = r.removeFromLeft (156);
        left.removeFromTop (18);
        laneBox.setBounds (left.removeFromTop (22));
        r.removeFromLeft (6);
        auto mid = r.removeFromLeft (214);
        mid.removeFromTop (18);
        modelBox.setBounds (mid.removeFromTop (22));
        r.removeFromLeft (8);
        auto kn = r.removeFromLeft (3 * 50);
        for (int k = 0; k < 3; ++k)
        {
            auto cell = kn.removeFromLeft (50);
            if (knobs[(size_t) k] != nullptr) knobs[(size_t) k]->setBounds (cell.withTrimmedTop (2));
        }
        r.removeFromLeft (10);
        viewArea = r;
        hint.setBounds (juce::Rectangle<int> (modelBox.getRight() + 6, getLocalBounds().getY() + 2, 3 * 50 + 6, getHeight() - 4));
    }

    void PhysRow::paint (juce::Graphics& g)
    {
        g.setFont (font (10.0f, true));
        g.setColour (colours::dim);
        g.drawText ("SOUND UP CLOSE", laneBox.getX(), getLocalBounds().getY() + 4, 156, 14, juce::Justification::centredLeft);
        g.drawText (shownKit >= 0 ? juce::String ("MODEL: ") + phys::kit (shownKit).name : juce::String ("MODEL"),
                    modelBox.getX(), getLocalBounds().getY() + 4, 214, 14, juce::Justification::centredLeft);

        // the view: the sound's waveform and its loudness over time
        const auto col = colours::lane (lane);
        const auto v = viewArea.toFloat();
        g.setColour (colours::slot);
        g.fillRoundedRectangle (v, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (v.reduced (0.5f), 2.0f, 1.0f);
        if (shown == nullptr || shown->left.empty())
        {
            g.setFont (font (10.5f));
            g.setColour (colours::dim);
            g.drawText (isPhysModel ((int) proc.apvts.getRawParameterValue (CoveGrooveProcessor::laneParam (lane, "model"))->load()) ? "rendering the model..." : "silent",
                        viewArea, juce::Justification::centred);
            return;
        }
        const auto& x = shown->left;
        const auto& y = shown->stereo() ? shown->right : shown->left;
        const int w = viewArea.getWidth() - 4;
        const float mid = v.getCentreY(), h = v.getHeight() * 0.44f;
        float peak = 1.0e-6f;
        for (size_t i = 0; i < x.size(); ++i) peak = std::max (peak, std::max (std::abs (x[i]), std::abs (y[i])));
        const double per = (double) x.size() / (double) w;
        const auto adsr = proc.laneEnv (lane);
        const bool shaped = ! adsr.flat();
        juce::Path env;
        std::vector<float> rmsCol ((size_t) w), top ((size_t) w), rawTop ((size_t) w);
        for (int px = 0; px < w; ++px)
        {
            const size_t a = (size_t) (px * per), b = std::min (x.size(), (size_t) ((px + 1) * per) + 1);
            float mx = 0.0f; double sq = 0.0;
            for (size_t i = a; i < b; ++i)
            {
                const float s = 0.5f * (x[i] + y[i]);
                mx = std::max (mx, std::abs (s));
                sq += (double) s * s;
            }
            // the lane's ADSR, as it will shape the sound
            const float eg = shaped ? adsr.at ((float) (((double) px + 0.5) * per / shown->rate)) : 1.0f;
            rawTop[(size_t) px] = mx / peak;
            top[(size_t) px] = mx * eg / peak;
            rmsCol[(size_t) px] = eg * (float) std::sqrt (sq / (double) std::max<size_t> (1, b - a)) * 1.4142f / peak;
        }
        for (int px = 0; px < w; ++px)
        {
            const float xx = v.getX() + 2.0f + (float) px, yy = mid - std::min (1.0f, rmsCol[(size_t) px]) * h;
            if (px == 0) env.startNewSubPath (xx, mid); else env.lineTo (xx, yy);
        }
        // the waveform as its outline (6.7.7), above and below the centre, with
        // only a faint fill; before the ADSR shapes it, fainter still
        auto outline = [&] (const std::vector<float>& tops)
        {
            juce::Path p;
            for (int px = 0; px < w; ++px)
            {
                const juce::Point<float> q (v.getX() + 2.0f + (float) px, mid - tops[(size_t) px] * h);
                if (px == 0) p.startNewSubPath (q); else p.lineTo (q);
            }
            for (int px = w - 1; px >= 0; --px)
                p.lineTo (v.getX() + 2.0f + (float) px, mid + tops[(size_t) px] * h);
            p.closeSubPath();
            return p;
        };
        if (shaped)
        {
            g.setColour (col.withAlpha (0.3f));
            g.strokePath (outline (rawTop), juce::PathStrokeType (0.8f));
        }
        const auto wave = outline (top);
        g.setColour (col.withAlpha (0.1f));
        g.fillPath (wave);
        g.setColour (col.withAlpha (0.9f));
        g.strokePath (wave, juce::PathStrokeType (1.0f));
        g.setColour (colours::cream.withAlpha (0.95f));
        g.strokePath (env, juce::PathStrokeType (1.5f));

        // the ADSR itself: its line and its four handles
        {
            juce::Path line;
            const int steps = juce::jmax (8, w / 2);
            for (int i = 0; i <= steps; ++i)
            {
                const float tt = viewSeconds() * (float) i / (float) steps;
                const juce::Point<float> pt (timeToX (tt), levelToY (adsr.at (tt)));
                if (i == 0) line.startNewSubPath (pt); else line.lineTo (pt);
            }
            g.setColour (colours::green.withAlpha (shaped ? 0.95f : 0.45f));
            g.strokePath (line, juce::PathStrokeType (shaped ? 1.6f : 1.0f));
            const char* names[4] = { "A", "D/S", "R", "" };
            for (int hdl = 0; hdl < 4; ++hdl)
            {
                if (hdl == 3 && ! adsr.releases()) continue;
                const auto hp = handlePos (hdl, adsr);
                const bool active = hdl == dragHandle;
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillEllipse (hp.x - 5.5f, hp.y - 5.5f, 11.0f, 11.0f);
                g.setColour (active ? colours::cream : colours::green);
                g.fillEllipse (hp.x - 4.0f, hp.y - 4.0f, 8.0f, 8.0f);
                if (names[hdl][0] != 0)
                {
                    g.setFont (font (9.0f, true));
                    g.drawText (names[hdl], juce::Rectangle<float> (hp.x - 14.0f, hp.y - 17.0f, 28.0f, 10.0f), juce::Justification::centred, false);
                }
            }
        }
        // the marks: the attack (to the loudest), where it has fallen 20 dB, where it ends
        const float total = (float) x.size() / (float) shown->rate;
        auto mark = [&] (float t, const juce::String& label, juce::Colour c)
        {
            if (t <= 0.0f || t > total) return;
            const float xx = v.getX() + 2.0f + (float) w * t / total;
            g.setColour (c.withAlpha (0.7f));
            g.drawVerticalLine ((int) xx, v.getY() + 2.0f, v.getBottom() - 2.0f);
            g.setFont (font (9.5f, true));
            g.drawText (label, juce::Rectangle<float> (xx + 2.0f, v.getBottom() - 13.0f, 60.0f, 11.0f), juce::Justification::centredLeft, false);
        };
        auto ms = [] (float t) { return t < 1.0f ? juce::String (juce::roundToInt (t * 1000.0f)) + " ms" : juce::String (t, 2) + " s"; };
        mark (attackS, "A " + ms (attackS), colours::green);
        mark (decayS, "-20 dB " + ms (decayS), colours::cyan);
        g.setFont (font (10.0f));
        g.setColour (colours::text);
        g.drawText (ms (lengthS) + "  (tap to hear, drag the ADSR handles, double-tap one: flat)", viewArea.reduced (6, 3), juce::Justification::topRight, false);
    }

    //=== the ADSR over the view ================================================
    float PhysRow::viewSeconds() const
    {
        if (shown == nullptr || shown->left.empty() || shown->rate <= 0.0) return 1.0f;
        return juce::jmax (0.01f, (float) ((double) shown->left.size() / shown->rate));
    }

    float PhysRow::timeToX (float t) const
    {
        const float w = (float) viewArea.getWidth() - 4.0f;
        return (float) viewArea.getX() + 2.0f + w * juce::jlimit (0.0f, 1.0f, t / viewSeconds());
    }

    float PhysRow::xToTime (float x) const
    {
        const float w = juce::jmax (1.0f, (float) viewArea.getWidth() - 4.0f);
        return juce::jlimit (0.0f, 1.0f, (x - (float) viewArea.getX() - 2.0f) / w) * viewSeconds();
    }

    float PhysRow::levelToY (float g) const
    {
        const auto v = viewArea.toFloat();
        return v.getCentreY() - juce::jlimit (0.0f, 1.0f, g) * v.getHeight() * 0.44f;
    }

    juce::Point<float> PhysRow::handlePos (int handle, const LaneEnv& e) const
    {
        switch (handle)
        {
            case 0:  return { timeToX (e.attack), levelToY (1.0f) };
            case 1:  return { timeToX (e.attack + e.decay), levelToY (e.sustain) };
            case 2:  return { e.releases() ? timeToX (e.releaseAt) : (float) viewArea.getRight() - 6.0f, levelToY (e.sustain) };
            default: return { timeToX (e.releaseAt + e.release), levelToY (0.0f) };
        }
    }

    int PhysRow::handleAt (juce::Point<float> p) const
    {
        if (shown == nullptr || shown->left.empty()) return -1;
        const auto e = proc.laneEnv (lane);
        int best = -1;
        float bestD = 11.0f;   // (a fingertip)
        for (int hdl = 3; hdl >= 0; --hdl)   // the later handles first: they sit on top
        {
            if (hdl == 3 && ! e.releases()) continue;
            const float d = handlePos (hdl, e).getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = hdl; }
        }
        return best;
    }

    void PhysRow::setEnvParam (const char* what, float value)
    {
        if (auto* prm = proc.apvts.getParameter (CoveGrooveProcessor::laneParam (lane, what)))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    void PhysRow::mouseDown (const juce::MouseEvent& e)
    {
        if (! viewArea.contains (e.getPosition())) return;
        dragHandle = handleAt (e.position);
        if (dragHandle < 0)
        {
            proc.audition (lane);
            return;
        }
        for (const char* w : { "eA", "eD", "eS", "eH", "eR" })
            if (auto* prm = proc.apvts.getParameter (CoveGrooveProcessor::laneParam (lane, w)))
                prm->beginChangeGesture();
        repaint();
    }

    void PhysRow::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragHandle < 0) return;
        const auto env = proc.laneEnv (lane);
        const float t = xToTime (e.position.x);
        const auto v = viewArea.toFloat();
        const float level = juce::jlimit (0.0f, 1.0f, (v.getCentreY() - e.position.y) / (v.getHeight() * 0.44f));
        switch (dragHandle)
        {
            case 0:
                setEnvParam ("eA", t * 1000.0f);
                if (env.releases() && env.releaseAt < t + env.decay) setEnvParam ("eH", (t + env.decay) * 1000.0f);
                break;
            case 1:
                setEnvParam ("eD", juce::jmax (0.0f, t - env.attack) * 1000.0f);
                setEnvParam ("eS", level * 100.0f);
                if (env.releases() && env.releaseAt < t) setEnvParam ("eH", t * 1000.0f);
                break;
            case 2:
                // to the right edge: no release (the sound plays out as it is)
                if (e.position.x >= (float) viewArea.getRight() - 5.0f) setEnvParam ("eH", LaneEnv::kOff * 1000.0f);
                else setEnvParam ("eH", juce::jmax (t, env.attack + env.decay) * 1000.0f);
                setEnvParam ("eS", level * 100.0f);
                break;
            default:
                setEnvParam ("eR", juce::jmax (0.0f, t - env.releaseAt) * 1000.0f);
                break;
        }
        repaint();
    }

    void PhysRow::mouseUp (const juce::MouseEvent&)
    {
        if (dragHandle < 0) return;
        for (const char* w : { "eA", "eD", "eS", "eH", "eR" })
            if (auto* prm = proc.apvts.getParameter (CoveGrooveProcessor::laneParam (lane, w)))
                prm->endChangeGesture();
        dragHandle = -1;
        repaint();
    }

    void PhysRow::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (handleAt (e.position) >= 0)
        {
            proc.resetLaneEnv (lane);
            repaint();
        }
    }

    void PhysRow::timerCallback()
    {
        auto v = [this] (const char* what) { return proc.apvts.getRawParameterValue (CoveGrooveProcessor::laneParam (lane, what))->load(); };
        const int model = (int) v ("model");
        const int kit = isPhysModel (model) ? physKitOf (model) : -1;
        const int choice = (int) v ("pm");
        if (kit != shownKit || (kit >= 0 && choice != shownChoice)) rebuild();

        // the ADSR moved (from here, the host or a preset): just draw it again
        {
            juce::String envKey;
            for (const char* w : { "eA", "eD", "eS", "eH", "eR" }) envKey << v (w) << "|";
            if (envKey != lastEnvKey) { lastEnvKey = envKey; repaint(); }
        }

        // the view, when the sound changed
        juce::String key;
        key << model << "|" << v ("tune") << "|" << v ("decay") << "|" << v ("tone") << "|" << proc.sampleVersion.load() << "|" << proc.physVersion.load();
        if (key == lastKey) return;
        lastKey = key;
        shown = proc.previewLane (lane);
        attackS = decayS = lengthS = 0.0f;
        if (shown != nullptr && ! shown->left.empty())
        {
            // a smoothed level (5 ms), its peak, and where it has fallen 20 dB after it
            const auto& x = shown->left;
            const int win = juce::jmax (1, (int) (0.005 * shown->rate));
            std::vector<float> lvl (x.size());
            double acc = 0.0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                acc += (double) x[i] * x[i];
                if (i >= (size_t) win) acc -= (double) x[i - (size_t) win] * x[i - (size_t) win];
                lvl[i] = (float) std::sqrt (std::max (0.0, acc) / win);
            }
            const auto pk = std::max_element (lvl.begin(), lvl.end());
            const size_t ip = (size_t) std::distance (lvl.begin(), pk);
            size_t id = ip;
            while (id < lvl.size() && lvl[id] > *pk * 0.1f) ++id;
            size_t ie = lvl.size();
            while (ie > ip && lvl[ie - 1] < *pk * 0.001f) --ie;
            attackS = (float) ((double) ip / shown->rate);
            decayS = (float) ((double) id / shown->rate);
            lengthS = (float) ((double) ie / shown->rate);
            // show the part that is heard
            if (ie > 16 && ie < x.size())
            {
                shown->left.resize (std::min (x.size(), ie + ie / 20));
                if (! shown->right.empty()) shown->right.resize (shown->left.size());
            }
        }
        repaint();
    }

    //==========================================================================
    CoveGrooveEditor::CoveGrooveEditor (CoveGrooveProcessor& p)
        : AudioProcessorEditor (p), proc (p), grid (p), samplePanel (p), physRow (p)
    {
        setLookAndFeel (&lnf);
        auto& s = proc.apvts;
        auto lamp = [] (juce::TextButton& b, juce::Colour on, bool toggle)
        {
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.setClickingTogglesState (toggle);
        };

        // header ------------------------------------------------------------
        fillPresetBox();
        presetBox.setTextWhenNothingSelected ("Preset");
        presetBox.onChange = [this] { if (presetBox.getSelectedId() > 0) proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
        addAndMakeVisible (presetBox);
        prevButton.onClick = [this] { stepPreset (-1); };
        nextButton.onClick = [this] { stepPreset (1); };
        prevButton.setTooltip ("The previous preset");
        nextButton.setTooltip ("The next preset");
        saveButton.onClick = [this] { savePreset(); };
        loadButton.onClick = [this] { loadPreset(); };
        for (auto* b : { &prevButton, &nextButton, &saveButton, &loadButton }) { lamp (*b, colours::cyan, false); addAndMakeVisible (*b); }

        for (int k = 0; k < CoveGrooveProcessor::getNumKits(); ++k) kitBox.addItem (CoveGrooveProcessor::getKitName (k), k + 1);
        kitBox.setTextWhenNothingSelected ("KIT");
        kitBox.setTooltip ("A sound for every lane at once: the hardgroove set, the Analog, the Hybrid, a played kit...");
        kitBox.onChange = [this]
        {
            const int id = kitBox.getSelectedId();
            if (id > 0) proc.applyKit (id - 1);
            kitBox.setSelectedId (0, juce::dontSendNotification);
        };
        addAndMakeVisible (kitBox);
        lamp (randomButton, colours::green, false);
        randomButton.setColour (juce::TextButton::textColourOffId, colours::green);
        randomButton.setTooltip ("Randomize: everything, the groove, the kit, the sounds, the effects, the mix or a preset");
        randomButton.onClick = [this] { showDiceMenu(); };
        addAndMakeVisible (randomButton);

        lamp (runButton, colours::cyan, true);
        runButton.setTooltip ("Plays at the Clock's tempo. With the DAW playing, Cove Groove follows its transport instead.");
        addAndMakeVisible (runButton);
        runAtt = std::make_unique<ButtonAtt> (s, "run", runButton);
        clockBox.addItemList ({ "Own Tempo", "Patch Tempo" }, 1);
        addAndMakeVisible (clockBox);
        clockAtt = std::make_unique<ComboAtt> (s, "clock", clockBox);
        for (int st = 0; st < NumStyles; ++st) styleBox.addItem (styleName (st), st + 1);
        addAndMakeVisible (styleBox);
        styleAtt = std::make_unique<ComboAtt> (s, "style", styleBox);
        barsBox.addItemList ({ "1 Bar", "2 Bars", "4 Bars" }, 1);
        addAndMakeVisible (barsBox);
        barsAtt = std::make_unique<ComboAtt> (s, "bars", barsBox);
        lamp (generateButton, colours::green, false);
        generateButton.setColour (juce::TextButton::textColourOffId, colours::green);
        generateButton.setTooltip ("A new groove in the Style, Density and Variation set here (RND on a lane: only that lane).");
        generateButton.onClick = [this] { proc.generate(); };
        addAndMakeVisible (generateButton);
        tempo = std::make_unique<Knob> (s, "tempo", "Tempo", colours::cyan);
        density = std::make_unique<Knob> (s, "density", "Density", colours::green);
        variation = std::make_unique<Knob> (s, "variation", "Variation", colours::green);
        swing = std::make_unique<Knob> (s, "swing", "Swing", colours::cyan);
        for (auto* k : { tempo.get(), density.get(), variation.get(), swing.get() }) addAndMakeVisible (*k);

        // lanes --------------------------------------------------------------
        for (int l = 0; l < kNumLanes; ++l)
        {
            auto& row = lanes[(size_t) l];
            const auto col = colours::lane (l);
            row.name.setText (laneName (l), juce::dontSendNotification);
            row.name.setFont (font (11.5f, true));
            row.name.setMinimumHorizontalScale (0.8f);
            row.name.setColour (juce::Label::textColourId, col.brighter (0.25f));
            row.name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (row.name);
            for (int m = 0; m < kNumModels; ++m) row.model.addItem (modelName (l, m), m + 1);
            addAndMakeVisible (row.model);
            row.modelAtt = std::make_unique<ComboAtt> (s, CoveGrooveProcessor::laneParam (l, "model"), row.model);
            // Sample with no sample in the lane yet: the panel, to load one
            row.model.onChange = [this, l]
            {
                if (lanes[(size_t) l].model.getSelectedId() == kSampleModel + 1 && proc.getLaneSample (l).data == nullptr)
                    samplePanel.show (l);
            };
            row.edit.setTooltip ("The lane's sample and its volume curve");
            row.edit.colour = col.brighter (0.3f);
            row.edit.onClick = [this, l] { if (samplePanel.isVisible() && samplePanel.getLane() == l) samplePanel.setVisible (false); else samplePanel.show (l); };
            addChildComponent (row.edit);
            row.load.setTooltip ("Load a sound file into this lane (it then plays it: Sound Sample)");
            row.load.setColour (juce::TextButton::textColourOffId, col.brighter (0.3f));
            row.load.onClick = [this, l] { samplePanel.show (l); samplePanel.chooseFile(); physRow.setLane (l); };
            addAndMakeVisible (row.load);
            lamp (row.mute, colours::coral, true);
            row.mute.setTooltip ("Mute this lane");
            addAndMakeVisible (row.mute);
            row.muteAtt = std::make_unique<ButtonAtt> (s, CoveGrooveProcessor::laneParam (l, "mute"), row.mute);
            row.rnd.setTooltip ("A new pattern for this lane alone");
            row.rnd.setColour (juce::TextButton::buttonOnColourId, colours::green);
            row.rnd.setColour (juce::TextButton::textColourOffId, colours::green);
            row.rnd.onClick = [this, l] { proc.generate (1u << l); };
            addAndMakeVisible (row.rnd);
            lamp (row.top, colours::cyan, true);
            row.top.setTooltip ("Through the top bus: the phaser and the moving band-pass");
            addAndMakeVisible (row.top);
            row.topAtt = std::make_unique<ButtonAtt> (s, CoveGrooveProcessor::laneParam (l, "top"), row.top);
            auto knob = [&] (const char* what) { auto k = std::make_unique<Knob> (s, CoveGrooveProcessor::laneParam (l, what), juce::String(), col); addAndMakeVisible (*k); return k; };
            row.level = knob ("level"); row.tune = knob ("tune"); row.decay = knob ("decay");
            row.tone = knob ("tone"); row.pan = knob ("pan"); row.send = knob ("send");
        }
        addAndMakeVisible (grid);
        addChildComponent (samplePanel);
        addAndMakeVisible (physRow);

        // effects ------------------------------------------------------------
        for (int f = 0; f < CoveGrooveProcessor::getNumFxPresets(); ++f) fxPresetBox.addItem (CoveGrooveProcessor::getFxPresetName (f), f + 1);
        fxPresetBox.setTextWhenNothingSelected ("FX PRESET");
        fxPresetBox.setTooltip ("The phaser, the band-pass, the delay and the drive set together");
        fxPresetBox.onChange = [this]
        {
            const int id = fxPresetBox.getSelectedId();
            if (id > 0) proc.applyFxPreset (id - 1);
            fxPresetBox.setSelectedId (0, juce::dontSendNotification);
        };
        addAndMakeVisible (fxPresetBox);
        phaser = std::make_unique<Knob> (s, "phaser", "Amount", colours::cyan);
        phaserRate = std::make_unique<Knob> (s, "phaserRate", "Rate", colours::cyan);
        filterFreq = std::make_unique<Knob> (s, "filterFreq", "Freq", colours::cyan);
        filterReso = std::make_unique<Knob> (s, "filterReso", "Reso", colours::cyan);
        filterMotion = std::make_unique<Knob> (s, "filterMotion", "Motion", colours::cyan);
        filterMix = std::make_unique<Knob> (s, "filterMix", "Mix", colours::cyan);
        delayFeedback = std::make_unique<Knob> (s, "delayFeedback", "Feedback", colours::cyan);
        delayTone = std::make_unique<Knob> (s, "delayTone", "Tone", colours::cyan);
        delayMix = std::make_unique<Knob> (s, "delayMix", "Mix", colours::cyan);
        drive = std::make_unique<Knob> (s, "drive", "Drive", colours::blue);
        driveTone = std::make_unique<Knob> (s, "driveTone", "Tone", colours::blue);
        driveMix = std::make_unique<Knob> (s, "driveMix", "Mix", colours::blue);
        volume = std::make_unique<Knob> (s, "volume", "Volume", colours::green);
        for (auto* k : { phaser.get(), phaserRate.get(), filterFreq.get(), filterReso.get(), filterMotion.get(), filterMix.get(),
                         delayFeedback.get(), delayTone.get(), delayMix.get(), drive.get(), driveTone.get(), driveMix.get(), volume.get() })
            addAndMakeVisible (*k);
        filterRate.addItemList ({ "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
        filterShape.addItemList ({ "Sine", "Ramp", "Random" }, 1);
        delayTime.addItemList ({ "1/16", "1/16 dot", "1/8", "1/8 dot", "1/4", "1/4 dot" }, 1);
        driveMode.addItemList ({ "Tube", "Tape", "Fuzz", "Crush" }, 1);
        for (auto* c : { &filterRate, &filterShape, &delayTime, &driveMode }) addAndMakeVisible (*c);
        filterRateAtt = std::make_unique<ComboAtt> (s, "filterRate", filterRate);
        filterShapeAtt = std::make_unique<ComboAtt> (s, "filterShape", filterShape);
        delayTimeAtt = std::make_unique<ComboAtt> (s, "delayTime", delayTime);
        driveModeAtt = std::make_unique<ComboAtt> (s, "driveMode", driveMode);
        lamp (pingButton, colours::cyan, true);
        pingButton.setTooltip ("Ping-pong: the repeats cross from side to side");
        addAndMakeVisible (pingButton);
        pingAtt = std::make_unique<ButtonAtt> (s, "delayPing", pingButton);
        const char* fxNames[5] = { "EFFECTS", "PHASER", "MOVING BAND-PASS", "DELAY", "DRIVE" };
        for (int i = 0; i < 5; ++i)
        {
            fxLabels[i].setText (fxNames[i], juce::dontSendNotification);
            fxLabels[i].setFont (font (10.5f, true));
            fxLabels[i].setColour (juce::Label::textColourId, colours::dim);
            fxLabels[i].setInterceptsMouseClicks (false, false);
            addAndMakeVisible (fxLabels[i]);
        }

        setSize (kWidth, kHeight);
        startTimerHz (30);
    }

    CoveGrooveEditor::~CoveGrooveEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void CoveGrooveEditor::fillPresetBox()
    {
        presetBox.clear (juce::dontSendNotification);
        juce::String group;
        for (int i = 0; i < proc.getNumPrograms(); ++i)
        {
            const auto cat = CoveGrooveProcessor::getProgramCategory (i);
            if (cat != group) { presetBox.addSectionHeading (cat); group = cat; }
            presetBox.addItem (proc.getProgramName (i), i + 1);
        }
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    }

    void CoveGrooveEditor::stepPreset (int delta)
    {
        const int n = proc.getNumPrograms();
        if (n <= 0) return;
        proc.setCurrentProgram (((proc.getCurrentProgram() + delta) % n + n) % n);
    }

    void CoveGrooveEditor::showDiceMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Randomize");
        m.addItem (1, "Everything");
        m.addItem (2, "The groove (style, feel, steps)");
        m.addItem (3, "The kit");
        m.addItem (4, "The sounds (every lane)");
        m.addItem (5, "The effects");
        m.addItem (6, "The mix (levels, pans, sends)");
        m.addSeparator();
        m.addItem (7, "A preset");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomButton),
                         [safe = juce::Component::SafePointer<CoveGrooveEditor> (this)] (int r)
                         {
                             if (safe == nullptr || r <= 0) return;
                             using R = CoveGrooveProcessor::Random;
                             static const R what[7] = { R::Everything, R::Groove, R::Kit, R::Sounds, R::Effects, R::Mix, R::Preset };
                             safe->proc.randomize (what[r - 1]);
                         });
    }

    //==========================================================================
    // the walnut case and the plates' grain, drawn once
    void CoveGrooveEditor::makeTextures()
    {
        const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
        woodImage = juce::Image (juce::Image::RGB, w, h, true);
        {
            juce::Graphics g (woodImage);
            g.setGradientFill (juce::ColourGradient (colours::wood.brighter (0.12f), 0.0f, 0.0f, colours::wood.darker (0.3f), 0.0f, (float) h, false));
            g.fillAll();
            juce::Random r (4711);
            for (int i = 0; i < 260; ++i)
            {
                const float y0 = r.nextFloat() * (float) h;
                const float amp = 2.0f + r.nextFloat() * 6.0f, period = 180.0f + r.nextFloat() * 420.0f, ph = r.nextFloat() * 6.28f;
                juce::Path p;
                p.startNewSubPath (0.0f, y0);
                for (float x = 0.0f; x <= (float) w; x += 12.0f)
                    p.lineTo (x, y0 + amp * std::sin (ph + x / period * 6.28f));
                const bool light = r.nextFloat() < 0.4f;
                g.setColour ((light ? juce::Colour (0xff6b4a33) : juce::Colours::black).withAlpha (0.05f + 0.1f * r.nextFloat()));
                g.strokePath (p, juce::PathStrokeType (0.6f + r.nextFloat() * 1.6f));
            }
        }
        grainImage = juce::Image (juce::Image::ARGB, 96, 96, true);
        {
            juce::Random r (17);
            for (int y = 0; y < 96; ++y)
                for (int x = 0; x < 96; ++x)
                {
                    const float n = r.nextFloat();
                    grainImage.setPixelAt (x, y, (n < 0.5f ? juce::Colours::black : juce::Colours::white).withAlpha (0.25f * std::abs (n - 0.5f)));
                }
        }
    }

    // a faceplate (raised) or a well (recessed)
    void CoveGrooveEditor::drawPlate (juce::Graphics& g, juce::Rectangle<int> area, bool recessed) const
    {
        const auto r = area.toFloat();
        const float rad = 2.5f;
        if (! recessed)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded (1.0f), rad);
        }
        const auto top = recessed ? colours::slot : colours::plateHi, bottom = recessed ? colours::slot.brighter (0.06f) : colours::plate.darker (0.15f);
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        if (grainImage.isValid())
        {
            g.setTiledImageFill (grainImage, 0, 0, 0.6f);
            g.fillRoundedRectangle (r, rad);
        }
        g.setColour (juce::Colours::white.withAlpha (recessed ? 0.04f : 0.12f));
        g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        g.setColour (colours::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
    }

    // a panel's title strip, darker across its top, as on a mixer's window
    void CoveGrooveEditor::drawTitleStrip (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto r = area.toFloat().reduced (1.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.fillRect (r);
        g.setColour (colours::edge);
        g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (r.getX(), r.getBottom(), r.getWidth(), 1.0f);
    }

    void CoveGrooveEditor::paint (juce::Graphics& g)
    {
        if (woodImage.isValid()) g.drawImageAt (woodImage, 0, 0);
        else g.fillAll (colours::wood);
        drawPlate (g, headerArea);
        drawPlate (g, physArea);
        drawPlate (g, laneArea);
        drawPlate (g, fxArea);

        // the name, pressed into the plate
        g.setFont (font (25.0f, true));
        auto t = titleArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawText ("Cove", t.translated (0.0f, 1.2f), juce::Justification::centredLeft, false);
        g.setColour (colours::cyan);
        g.drawText ("Cove", t, juce::Justification::centredLeft, false);
        const float coveW = juce::GlyphArrangement::getStringWidth (font (25.0f, true), "Cove ");
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawText ("Groove", t.withTrimmedLeft (coveW).translated (0.0f, 1.2f), juce::Justification::centredLeft, false);
        g.setColour (colours::cream);
        g.drawText ("Groove", t.withTrimmedLeft (coveW), juce::Justification::centredLeft, false);

        // the lanes' title strip and heads
        drawTitleStrip (g, laneStrip);
        g.setFont (font (10.0f, true));
        g.setColour (colours::dim);
        const auto sy = laneStrip.getY(), sh = laneStrip.getHeight();
        g.drawText ("LANES", lanes[0].mute.getX(), sy, 120, sh, juce::Justification::centredLeft);
        const Knob* knobs[6] = { lanes[0].level.get(), lanes[0].tune.get(), lanes[0].decay.get(), lanes[0].tone.get(), lanes[0].pan.get(), lanes[0].send.get() };
        const char* heads[6] = { "Level", "Tune", "Decay", "Tone", "Pan", "Send" };
        for (int i = 0; i < 6; ++i)
            g.drawText (heads[i], knobs[i]->getX() - 4, sy, knobs[i]->getWidth() + 8, sh, juce::Justification::centred);
        g.drawText ("TOP", lanes[0].top.getX(), sy, lanes[0].top.getWidth(), sh, juce::Justification::centred);
        g.setFont (font (10.0f));
        g.drawText ("STEPS   tap: on (as loud as the height you tap)   drag up / down: louder / softer   drag across: paint   long-press: second note",
                    grid.getX(), sy, grid.getWidth(), sh, juce::Justification::centredLeft, true);

        // each lane's strip: one band under all its controls and steps, its colour down the left edge
        for (int l = 0; l < kNumLanes; ++l)
        {
            const auto col = colours::lane (l);
            const auto b = bands[(size_t) l].toFloat();
            g.setColour (colours::plate.darker (0.22f).interpolatedWith (col, 0.05f));
            g.fillRect (b);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (b.getX(), b.getY(), b.getWidth(), 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRect (b.getX(), b.getBottom() - 1.0f, b.getWidth(), 1.0f);
            g.setColour (col);
            g.fillRect (b.getX(), b.getY(), 4.0f, b.getHeight());
        }

        // each lane's lamp, glowing on its hits
        for (int l = 0; l < kNumLanes; ++l)
        {
            const auto& row = lanes[(size_t) l];
            const auto col = colours::lane (l);
            const auto lampR = lamps[(size_t) l].toFloat();
            if (row.glow > 0.01f)
            {
                g.setColour (col.withAlpha (0.4f * row.glow));
                g.fillRect (lampR.expanded (2.5f * row.glow));
            }
            g.setColour (col.darker (0.6f).interpolatedWith (col.brighter (0.5f), row.glow));
            g.fillRect (lampR);
            g.setColour (colours::edge);
            g.drawRect (lampR, 0.8f);
        }

        // the effects' title strip and the sections' dividers
        drawTitleStrip (g, fxStrip);
        for (int i = 1; i < 5; ++i)
        {
            const int x = fxLabels[i].getX() - 8;
            g.setColour (colours::edge.withAlpha (0.8f));
            g.fillRect (x, fxStrip.getBottom() + 2, 1, fxArea.getBottom() - fxStrip.getBottom() - 8);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (x + 1, fxStrip.getBottom() + 2, 1, fxArea.getBottom() - fxStrip.getBottom() - 8);
        }
    }

    void CoveGrooveEditor::resized()
    {
        auto r = getLocalBounds().reduced (6);
        headerArea = r.removeFromTop (64);
        r.removeFromTop (6);
        physArea = r.removeFromTop (74);
        r.removeFromTop (6);
        fxArea = r.removeFromBottom (110);
        r.removeFromBottom (6);
        laneArea = r;
        makeTextures();

        // header
        {
            auto h = headerArea.reduced (12, 7);
            titleArea = h.removeFromLeft (156);
            h.removeFromLeft (6);
            auto col = [&] (int w) { auto c = h.removeFromLeft (w); h.removeFromLeft (8); return c; };
            auto pr = col (214);
            auto prTop = pr.removeFromTop (22);
            prevButton.setBounds (prTop.removeFromLeft (22));
            nextButton.setBounds (prTop.removeFromRight (22));
            presetBox.setBounds (prTop.reduced (3, 0));
            pr.removeFromTop (4);
            saveButton.setBounds (pr.removeFromLeft (pr.getWidth() / 2 - 2).withHeight (22));
            loadButton.setBounds (pr.removeFromRight (pr.getWidth() - 4).withHeight (22));
            auto kit = col (104);
            kitBox.setBounds (kit.removeFromTop (22));
            kit.removeFromTop (4);
            randomButton.setBounds (kit.withHeight (22));
            auto run = col (84);
            runButton.setBounds (run.removeFromTop (22));
            run.removeFromTop (4);
            clockBox.setBounds (run.withHeight (22));
            tempo->setBounds (col (44));
            auto st = col (100);
            styleBox.setBounds (st.removeFromTop (22));
            st.removeFromTop (4);
            barsBox.setBounds (st.withHeight (22));
            density->setBounds (col (46));
            variation->setBounds (col (50));
            swing->setBounds (col (44));
            generateButton.setBounds (h);
        }

        // lanes
        {
            // the title strip, then a strip per lane: lamp, M, name, sound, RND |
            // steps | Level Tune Decay Tone Pan Send | TOP - every control 22 high
            // on the strip's middle
            laneStrip = laneArea.withHeight (22);
            auto a = laneArea.withTrimmedTop (25).withTrimmedBottom (5).reduced (10, 0);
            const int rowH = a.getHeight() / kNumLanes;
            const int knobW = 40, knobsW = 6 * knobW;
            const int gridX = a.getX() + 252, gridR = a.getRight() - 46 - knobsW - 8;
            grid.setBounds (gridX, a.getY(), gridR - gridX, rowH * kNumLanes);
            samplePanel.setBounds (grid.getBounds().expanded (3));
            physRow.setBounds (physArea);
            for (int l = 0; l < kNumLanes; ++l)
            {
                auto row = juce::Rectangle<int> (a.getX(), a.getY() + l * rowH, a.getWidth(), rowH);
                bands[(size_t) l] = row.withTrimmedBottom (2).expanded (5, 0);
                const int cy = row.getY() + (rowH - 2) / 2;
                auto& lr = lanes[(size_t) l];
                lamps[(size_t) l] = { row.getX() + 3, cy - 3, 6, 6 };
                lr.mute.setBounds (row.getX() + 14, cy - 11, 22, 22);
                lr.name.setBounds (row.getX() + 40, cy - 9, 58, 18);
                lr.edit.setBounds (row.getX() + 97, cy - 9, 18, 18);
                lr.load.setBounds (row.getX() + 117, cy - 9, 18, 18);
                lr.model.setBounds (row.getX() + 138, cy - 11, 78, 22);
                lr.rnd.setBounds (row.getX() + 219, cy - 11, 29, 22);
                int kx = gridR + 8;
                for (auto* k : { lr.level.get(), lr.tune.get(), lr.decay.get(), lr.tone.get(), lr.pan.get(), lr.send.get() })
                {
                    k->setBounds (kx + 2, row.getY() + 2, knobW - 4, rowH - 6);
                    kx += knobW;
                }
                lr.top.setBounds (a.getRight() - 42, cy - 11, 42, 22);
            }
        }

        // effects
        {
            fxStrip = fxArea.withHeight (22);
            auto f = fxArea.reduced (14, 0).withTrimmedBottom (6);
            auto section = [&] (int w, int labelIndex)
            {
                auto s = f.removeFromLeft (w);
                f.removeFromLeft (16);
                fxLabels[labelIndex].setBounds (s.removeFromTop (22));
                s.removeFromTop (5);
                return s;
            };
            auto place = [] (juce::Rectangle<int>& s, Knob& k) { k.setBounds (s.removeFromLeft (48).withHeight (58)); };
            auto ef = section (120, 0);
            fxPresetBox.setBounds (ef.removeFromTop (22));
            ef.removeFromTop (2);
            auto vol = ef.removeFromLeft (48);
            volume->setBounds (vol.withHeight (58).withY (ef.getY()));
            auto ph = section (100, 1);
            place (ph, *phaser); place (ph, *phaserRate);
            auto fl = section (296, 2);
            place (fl, *filterFreq); place (fl, *filterReso); place (fl, *filterMotion); place (fl, *filterMix);
            fl.removeFromLeft (4);
            filterRate.setBounds (fl.removeFromTop (22));
            fl.removeFromTop (6);
            filterShape.setBounds (fl.removeFromTop (22));
            auto dl = section (244, 3);
            auto dlc = dl.removeFromLeft (92);
            delayTime.setBounds (dlc.removeFromTop (22));
            dlc.removeFromTop (6);
            pingButton.setBounds (dlc.removeFromTop (22));
            dl.removeFromLeft (8);
            place (dl, *delayFeedback); place (dl, *delayTone); place (dl, *delayMix);
            auto dr = section (f.getWidth(), 4);
            auto drc = dr.removeFromLeft (80);
            driveMode.setBounds (drc.removeFromTop (22));
            dr.removeFromLeft (8);
            place (dr, *drive); place (dr, *driveTone); place (dr, *driveMix);
        }
    }

    void CoveGrooveEditor::mouseDown (const juce::MouseEvent& e)
    {
        for (int l = 0; l < kNumLanes; ++l)
            if (lanes[(size_t) l].name.getBounds().expanded (2).contains (e.getPosition()))
                physRow.setLane (l);
    }

    void CoveGrooveEditor::timerCallback()
    {
        bool dirty = false;
        for (int l = 0; l < kNumLanes; ++l)
        {
            auto& row = lanes[(size_t) l];
            const int h = proc.laneHits[l].load();
            if (h != row.lastHits) { row.lastHits = h; row.glow = 1.0f; dirty = true; }
            else if (row.glow > 0.0f) { row.glow = juce::jmax (0.0f, row.glow - 0.12f); dirty = true; }
            // EDIT, on a lane playing its own sample
            if (const int m = row.model.getSelectedId() - 1; m != row.shownModel)
            {
                row.shownModel = m;
                row.edit.setVisible (m == kSampleModel);
            }
        }
        if (proc.getCurrentProgram() != shownProgram)
        {
            shownProgram = proc.getCurrentProgram();
            presetBox.setSelectedId (shownProgram + 1, juce::dontSendNotification);
        }
        if (dirty)
            for (const auto& lamp : lamps)
                repaint (lamp.expanded (4));
    }

    void CoveGrooveEditor::savePreset()
    {
        chooser = std::make_shared<juce::FileChooser> ("Save Cove Groove preset",
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Groove.covegroove"),
                                                       "*.covegroove");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  proc.savePreset (f.withFileExtension ("covegroove"));
                              });
    }

    void CoveGrooveEditor::loadPreset()
    {
        chooser = std::make_shared<juce::FileChooser> ("Load Cove Groove preset",
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                       "*.covegroove");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.loadPreset (f);
                              });
    }
}
