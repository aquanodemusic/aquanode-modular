#include "PluginEditor.h"

using namespace massif;

//==============================================================================
namespace amui
{
juce::Colour sourceColour (int s)
{
    if (s >= msEnv1 && s <= msEnv4) return envCol;
    if (s >= msMod5 && s <= msMod8) return modCol;
    if (s >= msMacro1 && s <= msMacro8) return macroCol;
    return midiCol;
}
juce::Colour sourceInk (int s)
{
    if (darkPlates) return sourceColour (s);
    if (s >= msEnv1 && s <= msEnv4) return juce::Colour (0xff1c63c8);
    if (s >= msMod5 && s <= msMod8) return juce::Colour (0xff0d8f55);
    if (s >= msMacro1 && s <= msMacro8) return juce::Colour (0xffd0640a);
    return juce::Colour (0xff8f7400);
}
juce::String sourceShort (int s)
{
    if (s >= msEnv1 && s <= msEnv4) return juce::String (s - msEnv1 + 1);
    if (s >= msMod5 && s <= msMod8) return juce::String (s - msMod5 + 5);
    if (s >= msMacro1 && s <= msMacro8) return juce::String (s - msMacro1 + 1);
    switch (s) { case msKeyTrack: return "K"; case msVelocity: return "V"; case msAftertouch: return "A"; case msTrigRandom: return "R"; default: return "?"; }
}
juce::String sourceName (int s)
{
    if (s >= msEnv1 && s <= msEnv4) return "Envelope " + juce::String (s - msEnv1 + 1);
    if (s >= msMod5 && s <= msMod8) return "Modulator " + juce::String (s - msMod5 + 5);
    if (s >= msMacro1 && s <= msMacro8) return "Macro " + juce::String (s - msMacro1 + 1);
    switch (s) { case msKeyTrack: return "Key Tracking"; case msVelocity: return "Velocity"; case msAftertouch: return "Aftertouch"; case msTrigRandom: return "Trigger Random"; default: return "-"; }
}

//==============================================================================
void crtGlass (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    // the bezel: a dark rubber rim, then the glass
    g.setColour (mk (0xff0a0f12)); g.fillRoundedRectangle (r, corner + 1.0f);
    auto in = r.reduced (1.5f);
    g.setGradientFill (juce::ColourGradient (crtCentre, in.getCentreX(), in.getCentreY(), crtEdge, in.getX(), in.getY(), true));
    g.fillRoundedRectangle (in, corner);
    // scanlines
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (in, corner); g.reduceClipRegion (clip);
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        for (float y = in.getY() + 1.0f; y < in.getBottom(); y += 3.0f) g.fillRect (in.getX(), y, in.getWidth(), 1.0f);
        // the glass's reflection along the top
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0, in.getY(), juce::Colours::transparentWhite, 0, in.getY() + in.getHeight() * 0.45f, false));
        g.fillRect (in);
    }
    g.setColour (juce::Colours::white.withAlpha (0.18f)); g.drawLine (r.getX() + corner, r.getBottom() + 0.5f, r.getRight() - corner, r.getBottom() + 0.5f, 1.0f);
}

void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, float size, bool bold)
{
    g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    // a soft dark drop under the near-white trace, so it reads on the bright glass
    g.setColour (crtInk.withAlpha (0.45f * c.getFloatAlpha()));
    for (auto d : { juce::Point<float> (0.0f, 1.0f), juce::Point<float> (0.8f, 1.2f), juce::Point<float> (-0.6f, 0.9f) })
        g.drawText (t, r.translated (d.x, d.y), j, true);
    g.setColour (c.withAlpha (0.25f * c.getFloatAlpha()));
    for (auto d : { juce::Point<float> (-1, 0), juce::Point<float> (1, 0) })
        g.drawText (t, r.translated (d.x, d.y), j, true);
    g.setColour (c); g.drawText (t, r, j, true);
}

//==============================================================================
Look::Look() { refresh(); }

void Look::refresh()
{
    setColour (juce::ComboBox::backgroundColourId, crtEdge);
    setColour (juce::ComboBox::textColourId, phosphor);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, gl (0xff0a4b5e));
    setColour (juce::PopupMenu::textColourId, phosphor);
    setColour (juce::PopupMenu::headerTextColourId, accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDeep);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, mk (0xff3a4750));
    setColour (juce::TextButton::textColourOffId, keyText);
    setColour (juce::TextButton::textColourOnId, navy);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::Slider::textBoxTextColourId, phosphor);
    setColour (juce::Slider::textBoxBackgroundColourId, crtEdge);
    setColour (juce::Slider::textBoxOutlineColourId, line);
    setColour (juce::TooltipWindow::backgroundColourId, gl (0xff0a4b5e));
    setColour (juce::TooltipWindow::textColourId, phosphor);
    setColour (juce::TooltipWindow::outlineColourId, accentDeep);
    setColour (juce::TextEditor::backgroundColourId, crtEdge);
    setColour (juce::TextEditor::textColourId, phosphor);
    setColour (juce::TextEditor::highlightColourId, accentDeep);
    setColour (juce::BubbleComponent::backgroundColourId, gl (0xff0a4b5e));
    setColour (juce::BubbleComponent::outlineColourId, accentDeep);
    setColour (juce::AlertWindow::backgroundColourId, mk (0xff2a343a));
    setColour (juce::AlertWindow::textColourId, keyText);
}

// a silver knob with a knurled rim on a dark collar; the value is an LED arc in the groove around it
void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float a = a0 + pos * (a1 - a0);
    const float tr = rad - 2.0f;
    // the groove
    juce::Path track; track.addCentredArc (c.x, c.y, tr, tr, 0, a0, a1, true);
    g.setColour (mk (0xff0b1114)); g.strokePath (track, juce::PathStrokeType (4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    g.setColour (juce::Colours::white.withAlpha (0.35f)); g.strokePath (track, juce::PathStrokeType (5.6f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    g.setColour (mk (0xff0b1114)); g.strokePath (track, juce::PathStrokeType (4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    // the LED arc (from the centre for the knobs that rest there)
    const bool bipolar = std::abs ((float) s.getDoubleClickReturnValue() - 0.5f) < 0.001f && s.getProperties().contains ("centre");
    const float from = bipolar ? a0 + 0.5f * (a1 - a0) : a0;
    juce::Path val; val.addCentredArc (c.x, c.y, tr, tr, 0, juce::jmin (from, a), juce::jmax (from, a), true);
    const auto led = s.isEnabled() ? accent : textDim;
    g.setColour (led.withAlpha (0.25f)); g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    g.setColour (led); g.strokePath (val, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    // shadow on the plate, then the body: a knurled dark steel knob lit from the top left
    const float kr = rad * 0.72f;
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillEllipse (c.x - kr - 0.5f, c.y - kr + 2.0f, 2 * kr + 1.0f, 2 * kr + 1.5f);
    g.setGradientFill (juce::ColourGradient (mt (0xff6d7b84), c.x - kr * 0.6f, c.y - kr * 0.8f, mk (0xff141b1f), c.x + kr * 0.7f, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
    g.setColour (mk (0xff0a0f12)); g.drawEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr, 1.0f);
    // knurling: notches around the rim, turning with the knob
    const int notches = kr > 14.0f ? 28 : 18;
    for (int i = 0; i < notches; ++i)
    {
        const float na = a + juce::MathConstants<float>::twoPi * (float) i / (float) notches;
        const float sx = std::sin (na), cy2 = -std::cos (na);
        g.setColour (juce::Colours::white.withAlpha (0.10f + 0.16f * juce::jmax (0.0f, -(sx + cy2) * 0.7f)));
        g.drawLine (c.x + sx * kr * 0.80f, c.y + cy2 * kr * 0.80f, c.x + sx * kr * 0.97f, c.y + cy2 * kr * 0.97f, 1.0f);
    }
    // the cap: a brushed-silver disc
    const float cr = kr * 0.66f;
    g.setGradientFill (juce::ColourGradient (mt (0xfff3f7f9), c.x - cr * 0.5f, c.y - cr * 0.7f, mt (0xff8b9ca5), c.x + cr * 0.6f, c.y + cr, false));
    g.fillEllipse (c.x - cr, c.y - cr, 2 * cr, 2 * cr);
    g.setColour (mk (0xff222b30)); g.drawEllipse (c.x - cr, c.y - cr, 2 * cr, 2 * cr, 0.8f);
    // the pointer: a lit line from the cap out over the rim
    juce::Path p; p.addRoundedRectangle (-1.5f, -kr * 0.98f, 3.0f, kr * 0.62f, 1.2f);
    const auto tf = juce::AffineTransform::rotation (a).translated (c.x, c.y);
    g.setColour (mk (0xff0a0f12)); g.fillPath (p, tf);
    juce::Path p2; p2.addRoundedRectangle (-0.7f, -kr * 0.94f, 1.4f, kr * 0.54f, 0.6f);
    g.setColour (s.isEnabled() ? accent : juce::Colours::lightgrey); g.fillPath (p2, tf);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s)
{
    const bool vert = st == juce::Slider::LinearVertical;
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    // the slot
    auto slot = vert ? juce::Rectangle<float> (r.getCentreX() - 2.5f, r.getY() - 3.0f, 5.0f, r.getHeight() + 6.0f)
                     : juce::Rectangle<float> (r.getX() - 3.0f, r.getCentreY() - 2.5f, r.getWidth() + 6.0f, 5.0f);
    g.setColour (mk (0xff0b1114)); g.fillRoundedRectangle (slot, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.45f)); g.drawRoundedRectangle (slot.expanded (0.6f), 3.0f, 0.8f);
    // the cap: a dark block with a lit line
    auto cap = vert ? juce::Rectangle<float> (r.getCentreX() - 8.0f, pos - 5.0f, 16.0f, 10.0f)
                    : juce::Rectangle<float> (pos - 5.0f, r.getCentreY() - 8.0f, 10.0f, 16.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.0f);
    g.setGradientFill (juce::ColourGradient (mt (0xff66747d), cap.getX(), cap.getY(), mk (0xff171e22), cap.getRight(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (mk (0xff0a0f12)); g.drawRoundedRectangle (cap, 2.0f, 0.8f);
    g.setColour (s.isEnabled() ? accent : juce::Colours::lightgrey);
    if (vert) g.fillRect (cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
    else g.fillRect (cap.getCentreX() - 1.0f, cap.getY() + 2.0f, 2.0f, cap.getHeight() - 4.0f);
}

// a little LCD window
void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (mk (0xff0a0f12)); g.fillRoundedRectangle (r, 3.0f);
    g.setGradientFill (juce::ColourGradient (crtCentre.darker (0.25f), r.getCentreX(), r.getCentreY(), crtEdge, 0, 0, true));
    g.fillRoundedRectangle (r.reduced (1.2f), 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.16f)); g.drawLine (2.0f, (float) h - 0.5f, (float) w - 2.0f, (float) h - 0.5f, 1.0f);
    juce::Path a; a.addTriangle ((float) w - 12, (float) h * 0.38f, (float) w - 4, (float) h * 0.38f, (float) w - 8, (float) h * 0.66f);
    g.setColour (box.isEnabled() ? phosphor : textDim); g.fillPath (a);
}

// buttons: rubber keys, lit when on; "crt" buttons are displays
void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.getProperties().contains ("crt")) { crtGlass (g, r, 4.0f); return; }
    const bool on = b.getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 3.5f);
    if (on) g.setGradientFill (juce::ColourGradient (accent.brighter (0.5f), 0, r.getY(), accentDeep, 0, r.getBottom(), false));
    else    g.setGradientFill (juce::ColourGradient (mk (down ? 0xff20292f : (over ? 0xff46545e : 0xff3a4750)), 0, r.getY(),
                                                     mk (down ? 0xff161d22 : 0xff1f282e), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r.withTrimmedBottom (1.0f), 3.5f);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.5f : 0.14f)); g.drawLine (r.getX() + 3.0f, r.getY() + 1.0f, r.getRight() - 3.0f, r.getY() + 1.0f, 1.0f);
    g.setColour (mk (0xff0a0f12)); g.drawRoundedRectangle (r.withTrimmedBottom (1.0f), 3.5f, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    if (b.getProperties().contains ("crt"))
    {
        glowText (g, b.getButtonText(), r.reduced (10.0f, 0.0f), juce::Justification::centredLeft, phosphor, juce::jmin (16.0f, r.getHeight() * 0.62f), true);
        juce::Path a; a.addTriangle (r.getRight() - 18.0f, r.getCentreY() - 3.0f, r.getRight() - 8.0f, r.getCentreY() - 3.0f, r.getRight() - 13.0f, r.getCentreY() + 4.0f);
        g.setColour (phosphor); g.fillPath (a);
        return;
    }
    g.setFont (juce::FontOptions (juce::jmin (11.5f, r.getHeight() * 0.58f), juce::Font::bold));
    g.setColour (b.getToggleState() ? navy : (b.isEnabled() ? keyText : keyText.withAlpha (0.5f)));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1);
}

// section switches: an LED lens in a metal ring, then the stencilled name
void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const float h = (float) b.getHeight(), d = juce::jmin (15.0f, h - 4.0f);
    const float cx = 3.0f + d * 0.5f, cy = h * 0.5f;
    const bool on = b.getToggleState();
    g.setGradientFill (juce::ColourGradient (mt (0xff59666e), cx - d * 0.3f, cy - d * 0.4f, mk (0xff12181c), cx + d * 0.5f, cy + d * 0.5f, false));
    g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
    const float l = d * 0.62f;
    g.setGradientFill (juce::ColourGradient (on ? gl (0xffd8fbff) : gl (0xff24424b), cx - l * 0.2f, cy - l * 0.25f,
                                             on ? accentDeep : gl (0xff0a1c22), cx + l * 0.5f, cy + l * 0.5f, true));
    g.fillEllipse (cx - l * 0.5f, cy - l * 0.5f, l, l);
    g.setColour (mk (0xff0a0f12)); g.drawEllipse (cx - l * 0.5f, cy - l * 0.5f, l, l, 0.8f);
    g.setFont (juce::FontOptions (juce::jmin (12.5f, h * 0.62f), juce::Font::bold));
    g.setColour (on ? text : textDim.withAlpha (over ? 0.9f : 0.65f));
    g.drawText (b.getButtonText(), juce::Rectangle<float> (d + 9.0f, 0.0f, (float) b.getWidth() - d - 9.0f, h), juce::Justification::centredLeft, true);
}
} // namespace amui

using namespace amui;

//==============================================================================
MKnob::MKnob (AquaMassifEditor& e, int knobIndex, const juce::String& l, int numSlots, bool sideChainSlot)
    : knob (knobIndex), ed (e), label (l), slots (numSlots + (sideChainSlot ? 1 : 0)), sc (sideChainSlot)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue (true, (double) Patch().knob[(size_t) knobIndex]);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    if (knobIndex == kOsc1Pitch || knobIndex == kOsc2Pitch || knobIndex == kOsc3Pitch || knobIndex == kModOscPitch || knobIndex == kPan
        || knobIndex == kEqLow || knobIndex == kEqPeak || knobIndex == kEqHigh)
        slider.getProperties().set ("centre", true);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ed.proc.apvts, AquaMassifProcessor::knobId (knobIndex), slider);
    setTooltip (AquaMassifProcessor::knobName (knobIndex));
}

void MKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (slots > 0 ? 24 : 12);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

juce::Rectangle<int> MKnob::slotRect (int i) const
{
    const int bw = 13, gap = 2, total = slots * bw + (slots - 1) * gap;
    return { getWidth() / 2 - total / 2 + i * (bw + gap), getHeight() - 24, bw, 11 };
}

int MKnob::slotAt (juce::Point<int> p) const
{
    for (int i = 0; i < slots; ++i) if (slotRect (i).expanded (1).contains (p)) return i;
    return -1;
}

void MKnob::paint (juce::Graphics& g)
{
    if (hiddenKnob) return;
    g.setColour (textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText (label, 0, getHeight() - 12, getWidth(), 12, juce::Justification::centred, 1);
    const auto& m = ed.proc.extra.mod[(size_t) knob];
    for (int i = 0; i < slots; ++i)
    {
        auto r = slotRect (i).toFloat();
        const auto& s = m.slot[(size_t) i];
        const bool isSc = sc && i == slots - 1;
        g.setColour (bg); g.fillRoundedRectangle (r, 2);
        if (s.source >= 0) { g.setColour (sourceColour (s.source).withAlpha (0.3f)); g.fillRoundedRectangle (r, 2); }
        g.setColour (isSc && m.sideChain ? midiCol : line);
        g.drawRoundedRectangle (r, 2, 1);
        g.setColour (juce::Colours::white.withAlpha (0.4f)); g.drawLine (r.getX() + 1.0f, r.getBottom() + 0.5f, r.getRight() - 1.0f, r.getBottom() + 0.5f, 0.8f);
        if (s.source >= 0)
        {
            g.setColour (sourceColour (s.source));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (sourceShort (s.source), r, juce::Justification::centred);
        }
        else if (isSc)
        {
            g.setColour (phosphor.withAlpha (0.45f)); g.setFont (juce::FontOptions (7.5f)); g.drawText ("SC", r, juce::Justification::centred);
        }
        // a slot the side chain scales: a small mark under it
        if (sc && ! isSc && i < 2 && m.sideChain && m.slot[2].source >= 0 && m.scTarget[(size_t) i] && s.source >= 0)
        {
            juce::Path t;     // (a corner mark: there is no room under the slot)
            t.addTriangle (r.getRight() - 5.0f, r.getY() + 1.0f, r.getRight() - 1.0f, r.getY() + 1.0f, r.getRight() - 1.0f, r.getY() + 5.0f);
            g.setColour (midiCol); g.fillPath (t);
        }
    }
}

void MKnob::paintOverChildren (juce::Graphics& g)
{
    if (hiddenKnob) return;
    // modulation rings
    const auto b = slider.getBounds().toFloat().reduced (6.0f);
    const float rad0 = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f + 2.0f;
    const auto c = b.getCentre();
    const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
    const float base = (float) ed.proc.knob (knob);
    const auto& m = ed.proc.extra.mod[(size_t) knob];
    for (int i = 0; i < 2 + (sc ? 0 : 1) && i < slots; ++i)
    {
        const auto& s = m.slot[(size_t) i];
        if (s.source < 0 || (sc && i == slots - 1)) continue;
        const bool bip = isBipolarSource (s.source);
        float lo = bip ? base - std::abs (s.amount) : base + juce::jmin (0.0f, s.amount);
        float hi = bip ? base + std::abs (s.amount) : base + juce::jmax (0.0f, s.amount);
        lo = juce::jlimit (0.0f, 1.0f, lo); hi = juce::jlimit (0.0f, 1.0f, hi);
        const float rr = rad0 + 3.0f * (float) i;
        juce::Path p; p.addCentredArc (c.x, c.y, rr, rr, 0, a0 + lo * (a1 - a0), a0 + hi * (a1 - a0), true);
        g.setColour (sourceInk (s.source)); g.strokePath (p, juce::PathStrokeType (2.2f));
    }
    if (dropHover) { g.setColour (accentDeep); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 4, 1.5f); }
}

bool MKnob::isInterestedInDragSource (const SourceDetails& d) { return d.description.toString().startsWith ("src:") && slots > 0; }

void MKnob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("src:", false, false).getIntValue();
    auto& m = ed.proc.extra.mod[(size_t) knob];
    int s = slotAt (d.localPosition.toInt());
    if (s < 0)
    {
        for (int i = 0; i < slots - (sc ? 1 : 0); ++i) if (m.slot[(size_t) i].source < 0 || m.slot[(size_t) i].source == src) { s = i; break; }
        if (s < 0) s = 0;
    }
    const float amt = m.slot[(size_t) s].source == src ? m.slot[(size_t) s].amount : 0.3f;
    m.slot[(size_t) s] = { src, amt };
    if (sc && s == slots - 1) m.sideChain = true;
    ed.modsChanged();
}

void MKnob::mouseDown (const juce::MouseEvent& e)
{
    const int s = slotAt (e.getPosition());
    if (s < 0) return;
    if (e.mods.isPopupMenu()) { slotMenu (s); return; }
    draggingSlot = s;
    dragStartAmount = ed.proc.extra.mod[(size_t) knob].slot[(size_t) s].amount;
}

void MKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingSlot < 0) return;
    auto& sl = ed.proc.extra.mod[(size_t) knob].slot[(size_t) draggingSlot];
    if (sl.source < 0) return;
    const float sens = e.mods.isShiftDown() ? 0.0008f : 0.005f;
    sl.amount = juce::jlimit (-1.0f, 1.0f, dragStartAmount - (float) e.getDistanceFromDragStartY() * sens);
    ed.modsChanged();
}

void MKnob::mouseUp (const juce::MouseEvent&) { draggingSlot = -1; }

void MKnob::slotMenu (int s)
{
    juce::PopupMenu m, srcs;
    for (int i = msEnv1; i <= msTrigRandom; ++i) srcs.addItem (100 + i, sourceName (i));
    auto& km = ed.proc.extra.mod[(size_t) knob];
    m.addSubMenu ("Assign", srcs);
    m.addItem (1, "Remove", km.slot[(size_t) s].source >= 0);
    m.addItem (2, "Invert amount", km.slot[(size_t) s].source >= 0);
    m.addItem (3, "Amount 0", km.slot[(size_t) s].source >= 0);
    if (sc && s == slots - 1) m.addItem (4, "Side chain (scales the other slots)", true, km.sideChain);
    if (sc && s < slots - 1 && s < 2) m.addItem (5, "Scaled by the side chain", km.sideChain, km.scTarget[(size_t) s]);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, s] (int r)
    {
        auto& kmm = ed.proc.extra.mod[(size_t) knob];
        if (r >= 100) { kmm.slot[(size_t) s] = { r - 100, kmm.slot[(size_t) s].source >= 0 ? kmm.slot[(size_t) s].amount : 0.3f }; if (sc && s == slots - 1) kmm.sideChain = true; }
        else if (r == 1) { kmm.slot[(size_t) s] = {}; if (sc && s == slots - 1) kmm.sideChain = false; }
        else if (r == 2) kmm.slot[(size_t) s].amount = -kmm.slot[(size_t) s].amount;
        else if (r == 3) kmm.slot[(size_t) s].amount = 0;
        else if (r == 4) kmm.sideChain = ! kmm.sideChain;
        else if (r == 5 && s < 2) kmm.scTarget[(size_t) s] = ! kmm.scTarget[(size_t) s];
        else return;
        ed.modsChanged();
    });
}

//==============================================================================
void SourceHandle::paint (juce::Graphics& g)
{
    // a little socket: metal ring, the source's colour inside, a cross to grab
    auto r = getLocalBounds().toFloat().reduced (1);
    const auto col = sourceColour (source);
    g.setGradientFill (juce::ColourGradient (mt (0xffeef3f5), r.getX(), r.getY(), mt (0xff66777f), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.5f);
    auto in = r.reduced (2.0f);
    g.setColour (mk (0xff0d1418)); g.fillRoundedRectangle (in, 2.5f);
    g.setColour (col.withAlpha (isMouseOver() ? 0.55f : 0.3f)); g.fillRoundedRectangle (in, 2.5f);
    const auto c = r.getCentre(); const float a = juce::jmin (in.getWidth(), in.getHeight()) * 0.3f;
    g.setColour (col.brighter (0.3f));
    g.drawLine (c.x - a, c.y, c.x + a, c.y, 1.6f); g.drawLine (c.x, c.y - a, c.x, c.y + a, 1.6f);
}

void SourceHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! c->isDragAndDropActive() && e.getDistanceFromDragStart() > 3)
        {
            juce::Image img (juce::Image::ARGB, 18, 18, true);
            { juce::Graphics g (img); g.setColour (sourceColour (source)); g.fillRoundedRectangle (0, 0, 18, 18, 4);
              g.setColour (bg); g.setFont (juce::FontOptions (11.0f, juce::Font::bold)); g.drawText (sourceShort (source), 0, 0, 18, 18, juce::Justification::centred); }
            c->startDragging ("src:" + juce::String (source), this, juce::ScaledImage (img));
        }
}

//==============================================================================
void StepEditor::paint (juce::Graphics& g)
{
    auto& p = ed.proc.extra;
    const int pg = juce::jlimit (0, 3, page);
    const int type = ed.proc.choice ("mod" + juce::String (pg + 5) + "Type");
    auto r = getLocalBounds().toFloat();
    crtGlass (g, r);
    const float w = r.getWidth() / 16.0f, top = 14.0f, rowH = 10.0f, barH = r.getHeight() - top - 2 * rowH - 18;
    const int ls = type == mtStepper ? p.stepper[(size_t) pg].loopStart : p.performer[(size_t) pg].loopStart;
    const int le = type == mtStepper ? p.stepper[(size_t) pg].loopEnd : p.performer[(size_t) pg].loopEnd;
    for (int i = 0; i < 16; ++i)
    {
        const float x = (float) i * w;
        const bool inLoop = i >= ls && i <= le;
        g.setColour (inLoop ? accent.withAlpha (0.35f) : juce::Colours::black.withAlpha (0.35f));
        g.fillRect (x + 1, 2.0f, w - 2, top - 4);
        g.setColour (inLoop ? phosphor : phosphor.withAlpha (0.5f)); g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (x, 0, w, top - 2), juce::Justification::centred);
        if (type == mtStepper)
        {
            const float v = p.stepper[(size_t) pg].step[(size_t) i];
            g.setColour (inLoop ? modCol : modCol.withAlpha (0.35f));
            g.fillRect (x + 2, top + barH * (1 - v), w - 4, barH * v);
            g.setColour (p.stepper[(size_t) pg].glideOn[(size_t) i] ? accent : juce::Colours::black.withAlpha (0.4f)); g.fillRect (x + 3, top + barH + 3, w - 6, rowH - 3);
            g.setColour (p.stepper[(size_t) pg].ampOn[(size_t) i] ? modCol : juce::Colours::black.withAlpha (0.4f)); g.fillRect (x + 3, top + barH + rowH + 4, w - 6, rowH - 3);
        }
        else
        {
            const auto& pf = p.performer[(size_t) pg];
            juce::Path pa, pb;
            for (int k = 0; k <= 12; ++k)
            {
                const float u = (float) k / 12.0f;
                const float ya = pf.levelA[(size_t) i] * curves::perf (pf.curveA[(size_t) i], juce::jmin (u, 0.999f));
                const float yb = pf.levelB[(size_t) i] * curves::perf (pf.curveB[(size_t) i], juce::jmin (u, 0.999f));
                const float px = x + 2 + u * (w - 4);
                if (k == 0) { pa.startNewSubPath (px, top + barH * (1 - ya)); pb.startNewSubPath (px, top + barH * (1 - yb)); }
                else { pa.lineTo (px, top + barH * (1 - ya)); pb.lineTo (px, top + barH * (1 - yb)); }
            }
            g.setColour (modCol); g.strokePath (pa, juce::PathStrokeType (1.5f));
            g.setColour (accent.withAlpha (0.6f)); g.strokePath (pb, juce::PathStrokeType (1.0f));
            g.setColour (pf.morphOn[(size_t) i] ? accent : juce::Colours::black.withAlpha (0.4f)); g.fillRect (x + 3, top + barH + 3, w - 6, rowH - 3);
            g.setColour (pf.ampOn[(size_t) i] ? modCol : juce::Colours::black.withAlpha (0.4f)); g.fillRect (x + 3, top + barH + rowH + 4, w - 6, rowH - 3);
        }
    }
    g.setColour (phosphor.withAlpha (0.55f)); g.setFont (juce::FontOptions (9.0f));
    g.drawText (type == mtStepper ? "rows: glide / amp mod - click a step number to set the loop start (shift: end)"
                                  : "click: upper curve of the step - right click: lower - rows: xfade / amp mod", r.withTop (r.getBottom() - 12.0f), juce::Justification::centred);
}

void StepEditor::mouseDown (const juce::MouseEvent& e) { edit (e, true); }
void StepEditor::mouseDrag (const juce::MouseEvent& e) { edit (e, false); }

void StepEditor::edit (const juce::MouseEvent& e, bool start)
{
    auto& p = ed.proc.extra;
    const int pg = juce::jlimit (0, 3, page);
    const int type = ed.proc.choice ("mod" + juce::String (pg + 5) + "Type");
    const float w = (float) getWidth() / 16.0f, top = 14.0f, rowH = 10.0f, barH = (float) getHeight() - top - 2 * rowH - 18;
    const int i = juce::jlimit (0, 15, (int) ((float) e.x / w));
    const float y = (float) e.y;
    if (start)
    {
        dragValue = y >= top && y < top + barH;
        dragRow = y >= top + barH + rowH + 2 ? 1 : (y >= top + barH ? 0 : -1);
        if (y < top)
        {
            auto& ls = type == mtStepper ? p.stepper[(size_t) pg].loopStart : p.performer[(size_t) pg].loopStart;
            auto& le = type == mtStepper ? p.stepper[(size_t) pg].loopEnd : p.performer[(size_t) pg].loopEnd;
            if (e.mods.isShiftDown() || e.mods.isPopupMenu()) le = juce::jmax (ls, i); else { ls = i; le = juce::jmax (le, i); }
            ed.modsChanged(); repaint(); return;
        }
    }
    if (type == mtStepper)
    {
        auto& s = p.stepper[(size_t) pg];
        if (dragValue) s.step[(size_t) i] = juce::jlimit (0.0f, 1.0f, 1.0f - (y - top) / barH);
        else if (start && dragRow == 0) s.glideOn[(size_t) i] = ! s.glideOn[(size_t) i];
        else if (start && dragRow == 1) s.ampOn[(size_t) i] = ! s.ampOn[(size_t) i];
    }
    else if (start)
    {
        auto& pf = p.performer[(size_t) pg];
        if (dragValue)
        {
            juce::PopupMenu m;
            for (int c = 0; c < pcNum; ++c) m.addItem (c + 1, curves::perfTables[c].name);
            const bool b = e.mods.isPopupMenu();
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, pg, i, b] (int r)
            {
                if (r <= 0) return;
                auto& pff = ed.proc.extra.performer[(size_t) pg];
                (b ? pff.curveB : pff.curveA)[(size_t) i] = r - 1;
                ed.modsChanged(); repaint();
            });
        }
        else if (dragRow == 0) pf.morphOn[(size_t) i] = ! pf.morphOn[(size_t) i];
        else if (dragRow == 1) pf.ampOn[(size_t) i] = ! pf.ampOn[(size_t) i];
    }
    ed.modsChanged(); repaint();
}

//==============================================================================
void CurveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    crtGlass (g, r);
    const auto p = ed.proc.currentPatch();
    juce::Path path;
    const float W = r.getWidth() - 8, H = r.getHeight() - 10, x0 = r.getX() + 4, y0 = r.getY() + 5;
    if (source < 4)
    {
        const int e = source; const float* k = p.knob.data();
        const float tA = law::attackSeconds (k[kEnv1AttT + e]), tD = law::decayTau (k[kEnv1DecT + e]) * 3.0f,
                    tS = p.env[(size_t) e].loopCount > 0 ? law::sustainSeconds (k[kEnv1SusT + e]) : 0.25f, tR = law::releaseTau (k[kEnv1RelT + e]) * 3.0f,
                    tDl = law::delaySeconds (k[kEnv1Delay + e]);
        const float total = juce::jmax (0.05f, tDl + tA + tD + tS + tR);
        auto X = [&] (float t) { return x0 + W * t / total; };
        auto Y = [&] (float v) { return y0 + H * (1.0f - v); };
        path.startNewSubPath (X (0), Y (0)); path.lineTo (X (tDl), Y (0)); path.lineTo (X (tDl + tA), Y (k[kEnv1AttL + e]));
        for (int i = 1; i <= 20; ++i) { const float t = tD * (float) i / 20.0f; path.lineTo (X (tDl + tA + t), Y (k[kEnv1DecL + e] + (k[kEnv1AttL + e] - k[kEnv1DecL + e]) * std::exp (-3.0f * (float) i / 20.0f))); }
        if (p.env[(size_t) e].loopCount > 0) path.lineTo (X (tDl + tA + tD + tS), Y (k[kEnv1SusL + e]));
        else path.lineTo (X (tDl + tA + tD + tS), Y (k[kEnv1DecL + e]));
        const float lv = p.env[(size_t) e].loopCount > 0 ? k[kEnv1SusL + e] : k[kEnv1DecL + e];
        for (int i = 1; i <= 20; ++i) path.lineTo (X (tDl + tA + tD + tS + tR * (float) i / 20.0f), Y (lv * std::exp (-3.0f * (float) i / 20.0f)));
        g.setColour (envCol);
    }
    else
    {
        const int m = source - 4;
        const auto& l = p.lfo[(size_t) m];
        const float x = p.knob[(size_t) (kMod5Morph + m)];
        for (int i = 0; i <= 120; ++i)
        {
            const float u = (float) i / 60.0f;
            const float cb = curve (l.shapeB, u + l.phaseB), ca = curve (l.shapeA, u + l.phaseA);
            const float v = cb + x * (ca - cb);
            const float px = x0 + W * (float) i / 120.0f, py = y0 + H * (0.5f - 0.48f * v);
            if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
        }
        g.setColour (modCol);
    }
    g.strokePath (path, juce::PathStrokeType (1.6f));
    // live value
    const float live = ed.proc.modView[juce::jlimit (0, 7, source)].load();
    g.setColour (phosphor);
    const float ly = source < 4 ? y0 + H * (1 - live) : y0 + H * (0.5f - 0.48f * live);
    g.fillEllipse (r.getRight() - 10, ly - 3, 6, 6);
}

//==============================================================================
AquaMassifEditor::AquaMassifEditor (AquaMassifProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    // header
    for (auto* b : { &loadB, &saveB, &importB, &exportB, &prevB, &nextB, &initB, &folderB }) addAndMakeVisible (b);
    loadB.onClick = [this] { loadPreset(); };
    saveB.onClick = [this] { savePreset(); };
    importB.onClick = [this] { importSound(); };
    exportB.onClick = [this] { exportSound(); };
    prevB.onClick = [this] { stepPreset (-1); };
    nextB.onClick = [this] { stepPreset (1); };
    initB.onClick = [this] { proc.initPatch(); refreshAll(); };
    folderB.onClick = [this] { tablesMenu(); };
    loadB.setTooltip ("Load a Massif preset (.massif): every setting and the wavetables it plays");
    saveB.setTooltip ("Save as a Massif preset (.massif): every setting, and the wavetables the oscillators play in its samples folder (a generated one as its recipe)");
    importB.setTooltip ("Import a sound file of the original (.nmsv)");
    exportB.setTooltip ("Export as a sound file of the original (.nmsv). Its built-in tables only: a generated or loaded table is written as the default one");
    folderB.setTooltip ("Wavetables: load a zip of them (as it is), or choose a folder");
    presetName.getProperties().set ("crt", true);
    presetName.setTooltip ("The sound: tap for the built-in sounds, or to load and save");
    presetName.onClick = [this] { presetMenu(); };
    addAndMakeVisible (presetName);
    // the patch generator: a genre, a sound type, Generate
    {
        namespace gen = massif::generator;
        for (int g = 0; g < gen::gNumGenres; ++g) genreBox.addItem (gen::genreName (g), g + 1);
        for (int t = 0; t <= gen::tAnything; ++t)
        {
            if (auto* h = gen::sectionBefore (t)) typeBox.addSectionHeading (h);
            typeBox.addItem (gen::typeName (t), t + 1);
        }
        typeBox.addSeparator(); typeBox.addItem ("Super Random", 100);
        genreBox.setSelectedId (proc.genGenre + 1, juce::dontSendNotification);
        typeBox.setSelectedId (proc.genType + 1, juce::dontSendNotification);
        genreBox.setTooltip ("Generate: the genre to lean towards (wobble rates, detune, drive, space)");
        typeBox.setTooltip ("Generate: the kind of sound. Super Random, at the bottom, sets every knob anywhere, with only the guards that keep it audible");
        genB.setTooltip ("Generate a new sound of the chosen type and genre");
        genB.onClick = [this] { generatePatch(); };
        for (auto* c : { (juce::Component*) &genreBox, (juce::Component*) &typeBox, (juce::Component*) &genB }) addAndMakeVisible (c);
    }

    // oscillators
    for (int o = 0; o < 3; ++o)
    {
        const auto n = juce::String (o + 1); const int b = kOsc1Pitch + 5 * o;
        addToggle ("osc" + n + "On", "OSC " + n);
        addCombo ("wt" + n); addCombo ("mode" + n);
        addKnob (b, "Pitch", 2, true); addKnob (b + 1, "Wt-Position", 2, true); addKnob (b + 2, "Intensity", 2, false); addKnob (b + 3, "Amp", 2, false);
        addFader (b + 4, true);
        for (int d = 0; d < 2; ++d)
        {
            auto& btn = wtStep[(size_t) (o * 2 + d)];
            btn.setButtonText (d == 0 ? "<" : ">");
            btn.onClick = [this, o, d, n] { const int cur = proc.choice ("wt" + n); proc.setChoice ("wt" + n, (cur + (d == 0 ? 86 : 1)) % 87); };
            addAndMakeVisible (btn);
        }
        wtInfo[(size_t) o].setFont (juce::FontOptions (9.5f)); wtInfo[(size_t) o].setColour (juce::Label::textColourId, textDim);
        addAndMakeVisible (wtInfo[(size_t) o]);
        wtLoad[(size_t) o].setButtonText ("Load");
        wtLoad[(size_t) o].setTooltip ("Load a wavetable file (WAV, FLAC, AIFF, Ogg) into this oscillator. One named like an original table takes "
                                       "that table's place and is kept in Massif's own folder; any other replaces this oscillator's current table "
                                       "for this sound (every oscillator playing that table hears it) and is saved with the preset");
        wtLoad[(size_t) o].onClick = [this, o] { loadOscTable (o); };
        addAndMakeVisible (wtLoad[(size_t) o]);
        addAndMakeVisible (waves.add (new WaveView (*this, o, false)));
    }
    // mod osc
    addToggle ("modOscOn", "MOD OSC");
    addKnob (kModOscPitch, "Pitch", 2, false); addKnob (kModOscRM, "Ring Mod", 2, false); addKnob (kModOscPhase, "Phase", 2, false);
    addKnob (kModOscPos, "Position", 2, false); addKnob (kModOscFltFM, "Filter FM", 2, false);
    addCombo ("rmT"); addCombo ("pmT"); addCombo ("posT");
    addCombo ("fmT")->setTooltip ("Filter FM: the modulation oscillator sweeps this filter's cutoff (up to +-7.5 kHz). "
                                  "Works on LP4, LP2, HP4, HP2, Allpass, Double Notch, Daft and Acid, like the original");
    // noise, feedback
    addToggle ("noiseOn", "NOISE"); addCombo ("noiseType"); addKnob (kNoiseColor, "Color", 2, false); addKnob (kNoiseAmp, "Amp", 2, false); addFader (kNoiseRoute, true);
    addToggle ("fbOn", "FEEDBACK"); addCombo ("fbPos"); addKnob (kFbAmp, "Amp", 2, false); addFader (kFbRoute, true);
    // filters
    for (int f = 0; f < 2; ++f)
    {
        const auto n = juce::String (f + 1);
        addToggle ("f" + n + "On", "FILTER " + n); addCombo ("f" + n + "Type");
        addKnob (kF1Cut + 3 * f, "Cutoff", 2, true); addKnob (kF1Bw + 3 * f, "Bandwidth", 2, false); addKnob (kF1Res + 3 * f, "Resonance", 2, true);
        addKnob (kF1Gain + f, "Out", 2, false);
    }
    addFader (kSerPar, false); addFader (kFltMix, false);
    // inserts
    for (int i = 0; i < 2; ++i)
    {
        const auto n = juce::String (i + 1);
        addToggle ("i" + n + "On", "INSERT " + n); addCombo ("i" + n + "Type"); addCombo ("i" + n + "Pos");
        addKnob (kIns1P1 + 2 * i, "Dry/Wet", 1, true); addKnob (kIns1P2 + 2 * i, "Amount", 1, true);
    }
    // fx
    for (int f = 0; f < 2; ++f)
    {
        const auto n = juce::String (f + 1);
        addToggle ("fx" + n + "On", "FX " + n); addCombo ("fx" + n + "Type");
        for (int k = 0; k < 4; ++k) addKnob (kFx1DW + 4 * f + k, k == 0 ? "Dry/Wet" : "Knob " + juce::String (k + 1), 1, true);
    }
    addToggle ("eqOn", "EQ");
    addKnob (kEqLow, "Low", 2, false); addKnob (kEqPeak, "Peak", 2, false); addKnob (kEqPeakFreq, "Freq", 2, false); addKnob (kEqHigh, "High", 2, false);
    addToggle ("ampOn", "AMP"); addKnob (kPan, "Pan", 1, true);
    addToggle ("bypassOn", "BYPASS"); addCombo ("bypassSrc"); addCombo ("bypassTgt"); addKnob (kBypassGain, "Bypass", 1, true);
    addKnob (kMasterVol, "Master", 0, false);
    // macros
    for (int m = 0; m < 8; ++m)
    {
        addKnob (kMacro1 + m, juce::String (m + 1), 0, false);
        auto* h = handles.add (new SourceHandle (msMacro1 + m)); addAndMakeVisible (h);
        auto& lb = macroNames[(size_t) m];
        lb.setEditable (true); lb.setJustificationType (juce::Justification::centred); lb.setFont (juce::FontOptions (10.5f));
        lb.setColour (juce::Label::textColourId, sourceInk (msMacro1));
        lb.onTextChange = [this, m] { proc.extra.macroName[(size_t) m] = macroNames[(size_t) m].getText().toStdString(); proc.extraChanged(); };
        addAndMakeVisible (lb);
    }
    for (int s : { (int) msKeyTrack, (int) msVelocity, (int) msAftertouch, (int) msTrigRandom })
    { auto* h = handles.add (new SourceHandle (s)); addAndMakeVisible (h); }
    // center window tabs
    const char* tabNames[] = { "ENV 1", "ENV 2", "ENV 3", "ENV 4", "MOD 5", "MOD 6", "MOD 7", "MOD 8", "OSC", "VOICES", "WT GEN" };
    for (int t = 0; t < 11; ++t)
    {
        auto* b = tabs.add (new juce::TextButton (tabNames[t])); addAndMakeVisible (b);
        b->setClickingTogglesState (false);
        b->onClick = [this, t] { showPage (t); };
        if (t < 8) { auto* h = handles.add (new SourceHandle (t)); addAndMakeVisible (h); }
    }
    // envelope pages
    for (int e = 0; e < 4; ++e)
    {
        const int ks[] = { kEnv1Delay, kEnv1AttT, kEnv1AttL, kEnv1DecT, kEnv1DecL, kEnv1SusT, kEnv1SusL, kEnv1Morph, kEnv1RelT, kEnv1Vel, kEnv1Ktr };
        const char* ls[] = { "Delay", "Attack", "Att Level", "Decay", "Dec Level", "Loop Time", "Sus Level", "Morph", "Release", "Velocity", "Keytrack" };
        for (int i = 0; i < 11; ++i) addKnob (ks[i] + e, ls[i], i < 9 ? 1 : 0, false);
    }
    // mod pages
    for (int m = 0; m < 4; ++m)
    {
        const auto n = juce::String (m + 5);
        addCombo ("mod" + n + "Type");
        addKnob (kMod5Rate + m, "Rate", 1, false); addKnob (kMod5Amp + m, "Amp", 1, false); addKnob (kMod5Morph + m, "Morph / Glide", 1, false);
        addKnob (kMod5Sync + m, "Sync", 0, false); addKnob (kMod5Restart + m, "Restart", 0, false);
    }
    // osc page / voicing page
    addKnob (kGlide, "Glide", 1, false); addKnob (kVibRate, "Vib Rate", 1, false); addKnob (kVibDepth, "Vib Depth", 1, false);
    addToggle ("vibMono", "Mono vibrato");
    addCombo ("voiceMode"); addCombo ("trigger");
    addFader (kUniPitch, false); addFader (kUniWtPos, false); addFader (kUniPan, false);
    addToggle ("uniPitchOn", "Pitch spread"); addToggle ("uniWtOn", "Wt spread"); addToggle ("uniPanOn", "Pan spread");
    {
        auto add = [this] (const juce::String& id)
        {
            auto* s = faders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 16);
            faderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *s));
            addAndMakeVisible (s); byId[id] = s;
        };
        add ("voices"); add ("unison");
    }
    // envelope / lfo extra controls
    envPlay.addItemList ({ "Gate", "One Shot", "Hold" }, 1);
    for (int i = 0; i <= 32; ++i) envLoops.addItem (i == 0 ? "Off" : juce::String (i), i + 1);
    envLoops.addItem ("Infinite", 34);
    for (auto* c : { &envShapeA, &envShapeB }) for (int i = 0; i < esNum; ++i) c->addItem (curves::envTables[i].name, i + 1);
    static const char* basic[4] = { "Sine", "Saw", "Square", "Triangle" };
    for (auto* c : { &lfoShapeA, &lfoShapeB })
    {
        for (int i = 0; i < 4; ++i) c->addItem (basic[i], i + 1);
        for (int i = 0; i < 31; ++i) c->addItem (curves::lfoTables[i].name, i + 5);
    }
    for (int i = 1; i <= 32; ++i) syncNum.addItem (juce::String (i), i);
    for (int d : { 1, 2, 4, 8, 12, 16, 24, 32 }) syncDen.addItem (juce::String (d), d);
    for (auto* c : { &envPlay, &envLoops, &envShapeA, &envShapeB, &lfoShapeA, &lfoShapeB, &syncNum, &syncDen }) addAndMakeVisible (c);
    for (auto* t : { &envLinear, &envRetrig, &lfoMono, &glideRate, &chordMode, &restartPhase }) addAndMakeVisible (t);
    auto envEdit = [this]
    {
        if (currentPage > 3) return;
        auto& x = proc.extra.env[(size_t) currentPage];
        x.playMode = envPlay.getSelectedId() - 1; x.loopCount = envLoops.getSelectedId() == 34 ? 33 : envLoops.getSelectedId() - 1;
        x.morphShapeA = envShapeA.getSelectedId() - 1; x.morphShapeB = envShapeB.getSelectedId() - 1;
        x.linearDecay = envLinear.getToggleState(); x.retrigger = envRetrig.getToggleState();
        proc.extraChanged();
    };
    envPlay.onChange = envEdit; envLoops.onChange = envEdit; envShapeA.onChange = envEdit; envShapeB.onChange = envEdit;
    envLinear.onClick = envEdit; envRetrig.onClick = envEdit;
    auto lfoEdit = [this]
    {
        if (currentPage < 4 || currentPage > 7) return;
        const int m = currentPage - 4; auto& l = proc.extra.lfo[(size_t) m];
        l.shapeA = lfoShapeA.getSelectedId() - 1; l.shapeB = lfoShapeB.getSelectedId() - 1; l.mono = lfoMono.getToggleState();
        proc.extra.syncNum[(size_t) m] = juce::jmax (1, syncNum.getSelectedId()); proc.extra.syncDen[(size_t) m] = juce::jmax (1, syncDen.getSelectedId());
        proc.extraChanged();
    };
    lfoShapeA.onChange = lfoEdit; lfoShapeB.onChange = lfoEdit; lfoMono.onClick = lfoEdit; syncNum.onChange = lfoEdit; syncDen.onChange = lfoEdit;
    for (auto* s : { &bendUp, &bendDown, &uniRange, &tune, &bpm })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16); addAndMakeVisible (s);
    }
    bendUp.setRange (-24, 24, 1); bendDown.setRange (-24, 24, 1); uniRange.setRange (0, 24, 0.01); tune.setRange (-12, 12, 0.01); bpm.setRange (20, 300, 0.1);
    bendUp.onValueChange = [this] { proc.extra.bendUp = (float) bendUp.getValue(); proc.extraChanged(); };
    bendDown.onValueChange = [this] { proc.extra.bendDown = (float) bendDown.getValue(); proc.extraChanged(); };
    uniRange.onValueChange = [this] { proc.extra.uniRangeUp = (float) uniRange.getValue(); proc.extraChanged(); };
    tune.onValueChange = [this] { proc.extra.globalTune = (float) tune.getValue(); proc.extraChanged(); };
    bpm.onValueChange = [this] { proc.extra.presetBpm = (float) bpm.getValue(); proc.extraChanged(); };
    glideRate.onClick = [this] { proc.extra.glideRateMode = glideRate.getToggleState(); proc.extraChanged(); };
    chordMode.onClick = [this] { proc.extra.uniChordMode = chordMode.getToggleState(); proc.extraChanged(); };
    restartPhase.onClick = [this] { proc.extra.restartPhase = restartPhase.getToggleState(); proc.extraChanged(); };
    // the wavetable generator
    genOsc.addItemList ({ "Osc 1", "Osc 2", "Osc 3" }, 1); genOsc.setSelectedId (1, juce::dontSendNotification);
    for (int i = 0; i < gen::rcNumRecipes; ++i) genRecipe.addItem (gen::recipeName (i), i + 1);
    genRecipe.setSelectedId (1, juce::dontSendNotification);
    for (int f : { 8, 16, 32, 64 }) genFrames.addItem (juce::String (f) + " frames", f);
    genFrames.setSelectedId (32, juce::dontSendNotification);
    for (auto* c : { &genOsc, &genRecipe, &genFrames }) addAndMakeVisible (c);
    for (int i = 0; i < 3; ++i)
    {
        auto& sl = genAmt[(size_t) i];
        sl.setSliderStyle (juce::Slider::LinearHorizontal); sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sl.setRange (0.0, 1.0); sl.setValue (0.5, juce::dontSendNotification); addAndMakeVisible (sl);
        genAmtLabel[(size_t) i].setFont (juce::FontOptions (10.5f)); genAmtLabel[(size_t) i].setColour (juce::Label::textColourId, textDim);
        addAndMakeVisible (genAmtLabel[(size_t) i]);
    }
    genSeed.setSliderStyle (juce::Slider::IncDecButtons); genSeed.setRange (1, 9999, 1); genSeed.setValue (1, juce::dontSendNotification);
    genSeed.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 46, 18); addAndMakeVisible (genSeed);
    for (auto* b : { &genDice, &genMake, &genRestore, &genExport }) addAndMakeVisible (b);
    genRecipe.onChange = [this] { recipeChanged(); };
    genOsc.onChange = [this] { if (genView) genView->setOsc (juce::jmax (0, genOsc.getSelectedId() - 1)); };
    genDice.onClick = [this] { juce::Random r; genSeed.setValue (1 + r.nextInt (9999), juce::dontSendNotification);
                               for (auto& sl : genAmt) sl.setValue (r.nextFloat(), juce::dontSendNotification); generate(); };
    genMake.onClick = [this] { generate(); };
    genRestore.onClick = [this] { proc.restoreTable (proc.choice ("wt" + juce::String (juce::jmax (1, genOsc.getSelectedId())))); updateLabels(); };
    genExport.onClick = [this] { exportTable(); };
    genMake.setTooltip ("Writes the recipe's table into the wavetable the chosen oscillator plays (it is saved with the patch)");
    genRestore.setTooltip ("Gives that wavetable back its file from your folder (or its stand-in)");
    genView = std::make_unique<WaveView> (*this, 0, true); addAndMakeVisible (*genView);
    recipeChanged();
    steps = std::make_unique<StepEditor> (*this); addAndMakeVisible (*steps);
    curve = std::make_unique<CurveView> (*this); addAndMakeVisible (*curve);
    pageInfo.setColour (juce::Label::textColourId, textDim); pageInfo.setFont (juce::FontOptions (10.5f)); addAndMakeVisible (pageInfo);
    voicesInfo.setColour (juce::Label::textColourId, textDim); voicesInfo.setFont (juce::FontOptions (10.5f)); addAndMakeVisible (voicesInfo);

    setTheme (juce::Colour (proc.themeMetal), juce::Colour (proc.themeGlow));
    look.refresh();
    applyTheme();
    setResizable (true, true);
    setResizeLimits (900, 530, 2520, 1480);
    getConstrainer()->setFixedAspectRatio (1260.0 / 740.0);
    setSize (1260, 740);
    showPage (0);
    refreshAll();
    startTimerHz (15);
}

AquaMassifEditor::~AquaMassifEditor() { setLookAndFeel (nullptr); }

MKnob* AquaMassifEditor::addKnob (int k, const juce::String& label, int slots, bool sc)
{
    // every knob with slots gets the side chain slot, as in the original (imported sounds use it on any knob)
    juce::ignoreUnused (sc);
    auto* m = knobs.add (new MKnob (*this, k, label, slots, slots > 0));
    addAndMakeVisible (m); knobById[k] = m;
    return m;
}

juce::ComboBox* AquaMassifEditor::addCombo (const juce::String& id, juce::Component*)
{
    auto* c = combos.add (new juce::ComboBox());
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) c->addItemList (p->choices, 1);
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *c));
    addAndMakeVisible (c); byId[id] = c;
    return c;
}

juce::ToggleButton* AquaMassifEditor::addToggle (const juce::String& id, const juce::String& text)
{
    auto* t = toggles.add (new juce::ToggleButton (text));
    toggleAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *t));
    addAndMakeVisible (t); byId[id] = t;
    return t;
}

juce::Slider* AquaMassifEditor::addFader (int k, bool vertical)
{
    auto* s = faders.add (new juce::Slider (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
    s->setPopupDisplayEnabled (true, true, nullptr);
    s->setDoubleClickReturnValue (true, (double) Patch().knob[(size_t) k]);
    faderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, AquaMassifProcessor::knobId (k), *s));
    s->setTooltip (AquaMassifProcessor::knobName (k));
    addAndMakeVisible (s); faderById[k] = s;
    return s;
}

//==============================================================================
void AquaMassifEditor::paint (juce::Graphics& g)
{
    if (chassis.isNull() || chassis.getWidth() != getWidth() || chassis.getHeight() != getHeight()) rebuildChassis();
    g.drawImageAt (chassis, 0, 0);
    // the output meter: LED segments
    const float sc = (float) getWidth() / 1260.0f;
    auto mr = masterArea.reduced ((int) (10 * sc)).removeFromRight ((int) (12 * sc)).toFloat();
    const float lv = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (meter, -48.0f) + 48.0f) / 48.0f);
    const int segs = 14; const float sh = mr.getHeight() / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const bool lit = (float) (i + 1) / (float) segs <= lv + 0.001f;
        const auto col = i >= segs - 2 ? juce::Colour (0xffff5a4d) : (i >= segs - 5 ? juce::Colour (0xfff0dc5a) : accent);
        g.setColour (lit ? col : col.withAlpha (0.13f));
        g.fillRect (mr.getX() + 1.5f, mr.getBottom() - (float) (i + 1) * sh + 1.0f, mr.getWidth() - 3.0f, sh - 2.0f);
    }
}

// The chassis, painted once per size: brushed silver, the plates
// the controls sit on, and the lettering.
void AquaMassifEditor::rebuildChassis()
{
    const int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;
    chassis = juce::Image (juce::Image::RGB, W, H, false);
    juce::Graphics g (chassis);
    const float sc = (float) W / 1260.0f;
    juce::Random rnd (4711);

    g.setGradientFill (juce::ColourGradient (frameHi, 0, 0, frameLo, 0, (float) H, false)); g.fillAll();
    // brushing
    for (int i = 0; i < H * 4; ++i)
    {
        const float y = rnd.nextFloat() * (float) H, x = rnd.nextFloat() * (float) W - 60.0f, len = (30.0f + rnd.nextFloat() * 280.0f) * sc;
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.01f + 0.035f * rnd.nextFloat()));
        g.fillRect (x, y, len, 1.0f);
    }
    // wear: a few scratches and dents
    for (int i = 0; i < 34; ++i)
    {
        const float x = rnd.nextFloat() * (float) W, y = rnd.nextFloat() * (float) H, len = (8.0f + rnd.nextFloat() * 46.0f) * sc, an = rnd.nextFloat() * 6.28f;
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawLine (x, y, x + std::cos (an) * len, y + std::sin (an) * len, 0.7f);
        g.setColour (juce::Colours::black.withAlpha (0.10f)); g.drawLine (x + 0.8f, y + 0.8f, x + std::cos (an) * len + 0.8f, y + std::sin (an) * len + 0.8f, 0.7f);
    }
    // the rim
    g.setColour (juce::Colours::white.withAlpha (0.14f)); g.drawRect (0.5f, 0.5f, (float) W - 1.0f, (float) H - 1.0f, 1.0f);
    g.setColour (juce::Colours::black); g.drawRect (0.0f, 0.0f, (float) W, (float) H, 1.0f);

    // the header: the name stamped in
    {
        auto lr = juce::Rectangle<float> (1040.0f * sc, 4.0f * sc, 210.0f * sc, 30.0f * sc);
        g.setFont (juce::FontOptions (21.0f * sc, juce::Font::bold | juce::Font::italic));
        g.setColour (juce::Colours::black.withAlpha (0.8f)); g.drawText ("MASSIF", lr.translated (0.0f, 1.5f), juce::Justification::centredRight);
        g.setGradientFill (juce::ColourGradient (mt (0xfff4f8fa), 0, lr.getY() + 4.0f, mt (0xff8ea1ab), 0, lr.getBottom() - 4.0f, false));
        g.drawText ("MASSIF", lr, juce::Justification::centredRight);
        g.setColour (accent); g.fillRect (lr.getRight() - 96.0f * sc, lr.getBottom() - 3.0f * sc, 96.0f * sc, 1.6f * sc);
    }

    // the plates
    auto plate = [&] (juce::Rectangle<int> rc, const juce::String& title)
    {
        auto f = rc.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (f.expanded (0.8f).translated (0.0f, 1.2f), 5.0f);
        g.setGradientFill (juce::ColourGradient (panelHi, f.getX(), f.getY(), panel, f.getX(), f.getBottom(), false));
        g.fillRoundedRectangle (f, 4.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (f, 4.0f); g.reduceClipRegion (clip);
            const int n = (int) (f.getHeight() * 2.4f);
            for (int i = 0; i < n; ++i)
            {
                const float y = f.getY() + rnd.nextFloat() * f.getHeight(), x = f.getX() + rnd.nextFloat() * f.getWidth() - 30.0f, len = 20.0f + rnd.nextFloat() * 180.0f;
                g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + 0.045f * rnd.nextFloat()));
                g.fillRect (x, y, len, 1.0f);
            }
            // wear on the plate
            for (int i = 0; i < 3; ++i)
            {
                const float x = f.getX() + rnd.nextFloat() * f.getWidth(), y = f.getY() + rnd.nextFloat() * f.getHeight(), len = (6.0f + rnd.nextFloat() * 30.0f) * sc, an = rnd.nextFloat() * 6.28f;
                g.setColour (juce::Colours::black.withAlpha (0.09f)); g.drawLine (x, y, x + std::cos (an) * len, y + std::sin (an) * len, 0.7f);
                g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawLine (x + 0.7f, y + 0.7f, x + std::cos (an) * len + 0.7f, y + std::sin (an) * len + 0.7f, 0.6f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.75f)); g.fillRect (f.getX(), f.getY(), f.getWidth(), 1.3f);
            g.setColour (juce::Colours::black.withAlpha (0.22f)); g.fillRect (f.getX(), f.getBottom() - 1.5f, f.getWidth(), 1.5f);
        }
        g.setColour (mt (0xff4d6069)); g.drawRoundedRectangle (f.reduced (0.5f), 4.0f, 1.0f);
        if (title.isNotEmpty())
        {
            g.setColour (textDim); g.setFont (juce::FontOptions (10.0f * sc, juce::Font::bold));
            g.drawText (title, rc.reduced ((int) (8 * sc), (int) (4 * sc)), juce::Justification::topRight);
        }
    };
    for (int o = 0; o < 3; ++o) plate (oscArea[o], "");
    plate (modOscArea, ""); plate (noiseArea, ""); plate (fbArea, "");
    plate (fltArea[0], ""); plate (fltArea[1], ""); plate (routeArea, "");
    plate (insArea[0], ""); plate (insArea[1], ""); plate (fxArea[0], ""); plate (fxArea[1], ""); plate (eqArea, "");
    plate (ampArea, ""); plate (masterArea, "OUT"); plate (centerArea, ""); plate (macroArea, "MACROS");

    // lettering on the plates
    g.setColour (textDim); g.setFont (juce::FontOptions (10.0f * sc));
    auto ends = [&] (int k, const char* lo, const char* hi)
    {
        if (auto* sl = faderById[k])
        {
            auto r = sl->getBounds();
            g.drawText (lo, r.getX() - (int) (30 * sc), r.getY(), (int) (26 * sc), r.getHeight(), juce::Justification::centredRight);
            g.drawText (hi, r.getRight() + (int) (4 * sc), r.getY(), (int) (26 * sc), r.getHeight(), juce::Justification::centredLeft);
        }
    };
    ends (kSerPar, "par", "ser"); ends (kFltMix, "F2", "F1");
    for (int k : { kOsc1Route, kOsc2Route, kOsc3Route, kNoiseRoute, kFbRoute })
        if (auto* sl = faderById[k])
        {
            auto r = sl->getBounds();
            g.drawText ("F1", r.getX() - 4, r.getY() - (int) (15 * sc), r.getWidth() + 8, (int) (11 * sc), juce::Justification::centred);
            g.drawText ("F2", r.getX() - 4, r.getBottom() + (int) (4 * sc), r.getWidth() + 8, (int) (11 * sc), juce::Justification::centred);
        }
    g.setFont (juce::FontOptions (10.0f * sc, juce::Font::bold));
    g.drawText ("ROUTING", routeArea.reduced ((int) (8 * sc), (int) (4 * sc)), juce::Justification::topLeft);
    g.setFont (juce::FontOptions (10.0f * sc));
    g.drawText ("drag onto a knob:", juce::Rectangle<int> ((int) (892 * sc), (int) (530 * sc), (int) (110 * sc), (int) (16 * sc)), juce::Justification::centredRight);
    const char* midiNames[4] = { "KTr", "Vel", "AT", "Rnd" };
    for (int i = 0; i < 4; ++i)
        g.drawText (midiNames[i], juce::Rectangle<int> ((int) ((1026 + i * 56) * sc), (int) (530 * sc), (int) (32 * sc), (int) (16 * sc)), juce::Justification::centredLeft);
    // the meter's well
    auto mr = masterArea.reduced ((int) (10 * sc)).removeFromRight ((int) (12 * sc)).toFloat();
    g.setColour (mk (0xff0b1114)); g.fillRoundedRectangle (mr.expanded (1.5f), 2.0f);
}

void AquaMassifEditor::resized()
{
    const float s = (float) getWidth() / 1260.0f;
    auto R = [s] (int x, int y, int w, int h) { return juce::Rectangle<int> ((int) std::round (x * s), (int) std::round (y * s), (int) std::round (w * s), (int) std::round (h * s)); };
    // header
    loadB.setBounds (R (8, 7, 46, 24)); saveB.setBounds (R (57, 7, 46, 24)); importB.setBounds (R (108, 7, 54, 24)); exportB.setBounds (R (165, 7, 54, 24));
    prevB.setBounds (R (224, 7, 22, 24)); nextB.setBounds (R (248, 7, 22, 24)); initB.setBounds (R (274, 7, 38, 24));
    presetName.setBounds (R (316, 6, 258, 26));
    genreBox.setBounds (R (580, 7, 100, 24)); typeBox.setBounds (R (684, 7, 128, 24)); genB.setBounds (R (816, 7, 68, 24));
    folderB.setBounds (R (890, 7, 92, 24));
    // areas
    for (int o = 0; o < 3; ++o) oscArea[o] = R (8, 40 + o * 134, 404, 130);
    modOscArea = R (8, 444, 404, 110); noiseArea = R (8, 558, 200, 176); fbArea = R (212, 558, 200, 176);
    fltArea[0] = R (416, 40, 300, 120); routeArea = R (416, 164, 300, 70); fltArea[1] = R (416, 238, 300, 120);
    insArea[0] = R (416, 362, 148, 188); insArea[1] = R (568, 362, 148, 188);
    fxArea[0] = R (720, 40, 340, 120); fxArea[1] = R (720, 164, 340, 120); eqArea = R (720, 288, 340, 110);
    ampArea = R (1064, 40, 188, 244); masterArea = R (1064, 288, 188, 110);
    centerArea = R (416, 554, 644, 180); macroArea = R (720, 402, 532, 148);
    // re-shape: center window across the bottom right, macros above it
    centerArea = R (416, 554, 836, 180);

    auto knobAt = [&] (int k, int x, int y, int w = 64, int h = 80) { if (auto* m = knobById[k]) m->setBounds (R (x, y, w, h)); };
    auto comp = [&] (const juce::String& id, int x, int y, int w, int h) { if (auto* c = byId[id]) c->setBounds (R (x, y, w, h)); };
    for (int o = 0; o < 3; ++o)
    {
        const int y = 40 + o * 134, b = kOsc1Pitch + 5 * o; const auto n = juce::String (o + 1);
        comp ("osc" + n + "On", 14, y + 6, 70, 20);
        wtStep[(size_t) (o * 2)].setBounds (R (88, y + 6, 20, 20)); comp ("wt" + n, 110, y + 6, 150, 20); wtStep[(size_t) (o * 2 + 1)].setBounds (R (262, y + 6, 20, 20));
        comp ("mode" + n, 290, y + 6, 84, 20);
        wtInfo[(size_t) o].setBounds (R (110, y + 26, 122, 13));
        wtLoad[(size_t) o].setBounds (R (234, y + 25, 48, 15));
        waves[o]->setBounds (R (292, y + 42, 70, 58));
        knobAt (b, 14, y + 40); knobAt (b + 1, 84, y + 40); knobAt (b + 2, 154, y + 40); knobAt (b + 3, 224, y + 40);
        if (auto* f = faderById[b + 4]) f->setBounds (R (370, y + 50, 24, 62));
    }
    comp ("modOscOn", 14, 450, 90, 20);
    knobAt (kModOscPitch, 14, 470, 64, 80); knobAt (kModOscRM, 94, 470, 64, 62); knobAt (kModOscPhase, 168, 470, 64, 62); knobAt (kModOscPos, 242, 470, 64, 62); knobAt (kModOscFltFM, 316, 470, 64, 62);
    comp ("rmT", 94, 532, 66, 18); comp ("pmT", 168, 532, 66, 18); comp ("posT", 242, 532, 66, 18); comp ("fmT", 316, 532, 76, 18);
    comp ("noiseOn", 14, 564, 80, 20); comp ("noiseType", 14, 588, 120, 20); knobAt (kNoiseColor, 14, 616, 60, 80); knobAt (kNoiseAmp, 80, 616, 60, 80);
    if (auto* f = faderById[kNoiseRoute]) f->setBounds (R (160, 622, 24, 70));
    comp ("fbOn", 218, 564, 90, 20); comp ("fbPos", 218, 588, 120, 20); knobAt (kFbAmp, 218, 616, 60, 80);
    if (auto* f = faderById[kFbRoute]) f->setBounds (R (360, 622, 24, 70));
    for (int f = 0; f < 2; ++f)
    {
        const int y = f == 0 ? 40 : 238; const auto n = juce::String (f + 1);
        comp ("f" + n + "On", 422, y + 6, 86, 20); comp ("f" + n + "Type", 512, y + 6, 130, 20);
        knobAt (kF1Cut + 3 * f, 422, y + 32); knobAt (kF1Bw + 3 * f, 492, y + 32); knobAt (kF1Res + 3 * f, 562, y + 32); knobAt (kF1Gain + f, 638, y + 32, 64, 80);
    }
    if (auto* f = faderById[kSerPar]) f->setBounds (R (500, 168, 160, 20));
    if (auto* f = faderById[kFltMix]) f->setBounds (R (500, 204, 160, 20));
    for (int i = 0; i < 2; ++i)
    {
        const int x = i == 0 ? 416 : 568; const auto n = juce::String (i + 1);
        comp ("i" + n + "On", x + 6, 368, 100, 20); comp ("i" + n + "Type", x + 6, 392, 136, 20); comp ("i" + n + "Pos", x + 6, 416, 136, 20);
        knobAt (kIns1P1 + 2 * i, x + 6, 444, 66, 86); knobAt (kIns1P2 + 2 * i, x + 76, 444, 66, 86);
    }
    for (int f = 0; f < 2; ++f)
    {
        const int y = f == 0 ? 40 : 164; const auto n = juce::String (f + 1);
        comp ("fx" + n + "On", 726, y + 6, 70, 20); comp ("fx" + n + "Type", 800, y + 6, 150, 20);
        for (int k = 0; k < 4; ++k) knobAt (kFx1DW + 4 * f + k, 726 + k * 80, y + 32, 72, 82);
    }
    comp ("eqOn", 726, 294, 60, 20);
    knobAt (kEqLow, 726, 314, 72, 80); knobAt (kEqPeak, 806, 314, 72, 80); knobAt (kEqPeakFreq, 886, 314, 72, 80); knobAt (kEqHigh, 966, 314, 72, 80);
    comp ("ampOn", 1070, 46, 70, 20); knobAt (kPan, 1070, 68, 70, 82);
    comp ("bypassOn", 1070, 154, 90, 20); comp ("bypassSrc", 1070, 176, 82, 20); comp ("bypassTgt", 1156, 176, 90, 20); knobAt (kBypassGain, 1150, 68, 80, 82);
    knobAt (kMasterVol, 1070, 296, 90, 96);
    // macros: a row of 8 (above the center window, right part)
    macroArea = R (720, 402, 532, 148);
    for (int m = 0; m < 8; ++m)
    {
        knobAt (kMacro1 + m, 726 + m * 64, 426, 56, 64);
        handles[m]->setBounds (R (726 + m * 64 + 20, 492, 16, 16));
        macroNames[(size_t) m].setBounds (R (722 + m * 64, 510, 64, 16));
    }
    for (int i = 0; i < 4; ++i) handles[8 + i]->setBounds (R (1008 + i * 56, 530, 16, 16));
    // tabs
    int tx = 422;
    for (int t = 0; t < tabs.size(); ++t)
    {
        const int tw = t < 8 ? 56 : (t == 10 ? 66 : 58);
        tabs[t]->setBounds (R (tx, 560, tw, 20)); tx += tw + 2;
        if (t < 8) { handles[12 + t]->setBounds (R (tx, 562, 16, 16)); tx += 20; }
    }
    layoutPage();
    chassis = juce::Image();     // the lettering follows the layout
    repaint();
}

void AquaMassifEditor::showPage (int page)
{
    currentPage = page;
    for (int t = 0; t < tabs.size(); ++t) tabs[t]->setToggleState (t == page, juce::dontSendNotification);
    layoutPage();
    updateLabels();
}

void AquaMassifEditor::layoutPage()
{
    const float s = (float) getWidth() / 1260.0f;
    auto R = [s] (int x, int y, int w, int h) { return juce::Rectangle<int> ((int) std::round (x * s), (int) std::round (y * s), (int) std::round (w * s), (int) std::round (h * s)); };
    const int page = currentPage;
    // hide every page control first
    std::vector<juce::Component*> pageComps { &envPlay, &envLoops, &envShapeA, &envShapeB, &lfoShapeA, &lfoShapeB, &syncNum, &syncDen,
                                              &envLinear, &envRetrig, &lfoMono, &bendUp, &bendDown, &uniRange, &tune, &bpm, &glideRate, &chordMode, &restartPhase,
                                              steps.get(), curve.get(), &voicesInfo, &genOsc, &genRecipe, &genFrames, &genAmt[0], &genAmt[1], &genAmt[2],
                                              &genAmtLabel[0], &genAmtLabel[1], &genAmtLabel[2], &genSeed, &genDice, &genMake, &genRestore, &genExport, genView.get() };
    for (auto* c : pageComps) c->setVisible (false);
    for (int e = 0; e < 4; ++e)
        for (int k : { kEnv1Delay, kEnv1AttT, kEnv1AttL, kEnv1DecT, kEnv1DecL, kEnv1SusT, kEnv1SusL, kEnv1Morph, kEnv1RelT, kEnv1Vel, kEnv1Ktr })
            if (auto* m = knobById[k + e]) m->setVisible (false);
    for (int m = 0; m < 4; ++m)
    {
        for (int k : { kMod5Rate, kMod5Amp, kMod5Morph, kMod5Sync, kMod5Restart }) if (auto* kk = knobById[k + m]) kk->setVisible (false);
        if (auto* c = byId["mod" + juce::String (m + 5) + "Type"]) c->setVisible (false);
    }
    for (int k : { kGlide, kVibRate, kVibDepth }) if (auto* m = knobById[k]) m->setVisible (false);
    for (auto id : { "vibMono", "voiceMode", "trigger", "uniPitchOn", "uniWtOn", "uniPanOn", "voices", "unison" }) if (auto* c = byId[id]) c->setVisible (false);
    for (int k : { kUniPitch, kUniWtPos, kUniPan }) if (auto* f = faderById[k]) f->setVisible (false);

    const int y0 = 584;
    pageInfo.setBounds (R (422, 714, 820, 16));
    if (page < 4)
    {
        const int ks[] = { kEnv1Delay, kEnv1AttT, kEnv1AttL, kEnv1DecT, kEnv1DecL, kEnv1SusT, kEnv1SusL, kEnv1Morph, kEnv1RelT, kEnv1Vel, kEnv1Ktr };
        for (int i = 0; i < 11; ++i) if (auto* m = knobById[ks[i] + page]) { m->setBounds (R (422 + i * 58, y0 + 4, 56, 76)); m->setVisible (true); }
        curve->setBounds (R (422, y0 + 84, 300, 44)); curve->setSource (page); curve->setVisible (true);
        envPlay.setBounds (R (730, y0 + 84, 90, 20)); envLoops.setBounds (R (826, y0 + 84, 80, 20));
        envShapeA.setBounds (R (912, y0 + 84, 100, 20)); envShapeB.setBounds (R (1018, y0 + 84, 100, 20));
        envLinear.setBounds (R (730, y0 + 108, 110, 20)); envRetrig.setBounds (R (846, y0 + 108, 100, 20));
        for (auto* c : std::initializer_list<juce::Component*> { &envPlay, &envLoops, &envShapeA, &envShapeB, &envLinear, &envRetrig }) c->setVisible (true);
        const auto& x = proc.extra.env[(size_t) page];
        envPlay.setSelectedId (x.playMode + 1, juce::dontSendNotification);
        envLoops.setSelectedId (x.loopCount >= 33 ? 34 : x.loopCount + 1, juce::dontSendNotification);
        envShapeA.setSelectedId (x.morphShapeA + 1, juce::dontSendNotification); envShapeB.setSelectedId (x.morphShapeB + 1, juce::dontSendNotification);
        envLinear.setToggleState (x.linearDecay, juce::dontSendNotification); envRetrig.setToggleState (x.retrigger, juce::dontSendNotification);
        pageInfo.setText ("Envelope " + juce::String (page + 1) + (page == 3 ? " (plays the amp by default)" : "") + " - loops: the sustain stage runs back and forth between the decay and sustain levels; morph blends shape A and B.", juce::dontSendNotification);
    }
    else if (page < 8)
    {
        const int m = page - 4; const auto n = juce::String (m + 5);
        const int type = proc.choice ("mod" + n + "Type");
        if (auto* c = byId["mod" + n + "Type"]) { c->setBounds (R (422, y0 + 6, 100, 20)); c->setVisible (true); }
        int x = 422; const int ks[] = { kMod5Rate, kMod5Amp, kMod5Morph, kMod5Sync, kMod5Restart };
        for (int i = 0; i < 5; ++i) if (auto* kk = knobById[ks[i] + m]) { kk->setBounds (R (x + (i % 3) * 62, y0 + 30 + (i / 3) * 0, 60, 72)); kk->setVisible (i < 3); x += 0; }
        syncNum.setBounds (R (614, y0 + 32, 50, 20)); syncDen.setBounds (R (668, y0 + 32, 50, 20)); syncNum.setVisible (true); syncDen.setVisible (true);
        syncNum.setSelectedId (proc.extra.syncNum[(size_t) m], juce::dontSendNotification); syncDen.setSelectedId (proc.extra.syncDen[(size_t) m], juce::dontSendNotification);
        if (auto* kk = knobById[kMod5Sync + m]) { kk->setBounds (R (614, y0 + 56, 50, 50)); kk->setVisible (true); }
        if (auto* kk = knobById[kMod5Restart + m]) { kk->setBounds (R (668, y0 + 56, 50, 50)); kk->setVisible (true); }
        if (type == mtLFO)
        {
            curve->setBounds (R (730, y0 + 6, 300, 98)); curve->setSource (page); curve->setVisible (true);
            lfoShapeA.setBounds (R (1040, y0 + 6, 100, 20)); lfoShapeB.setBounds (R (1040, y0 + 30, 100, 20)); lfoMono.setBounds (R (1040, y0 + 56, 80, 20));
            for (auto* c : std::initializer_list<juce::Component*> { &lfoShapeA, &lfoShapeB, &lfoMono }) c->setVisible (true);
            const auto& l = proc.extra.lfo[(size_t) m];
            lfoShapeA.setSelectedId (l.shapeA + 1, juce::dontSendNotification); lfoShapeB.setSelectedId (l.shapeB + 1, juce::dontSendNotification);
            lfoMono.setToggleState (l.mono, juce::dontSendNotification);
            pageInfo.setText ("LFO: two curves, Morph blends from B (left) to A (right). Sync knob up: rate from the tempo (" + juce::String (proc.extra.syncNum[(size_t) m]) + "/" + juce::String (proc.extra.syncDen[(size_t) m]) + ").", juce::dontSendNotification);
        }
        else
        {
            steps->setBounds (R (730, y0 + 4, 516, 120)); steps->setPage (m); steps->setVisible (true);
            pageInfo.setText (type == mtStepper ? "Stepper: 16 steps; Morph knob = glide (on the steps with the glide row lit)." : "Performer: a curve per step, Morph blends the A and B sequences.", juce::dontSendNotification);
        }
    }
    else if (page == 8)
    {
        int i = 0; for (int k : { kGlide, kVibRate, kVibDepth }) if (auto* m = knobById[k]) { m->setBounds (R (422 + (i++) * 66, y0 + 6, 62, 76)); m->setVisible (true); }
        if (auto* c = byId["vibMono"]) { c->setBounds (R (422, y0 + 90, 110, 20)); c->setVisible (true); }
        glideRate.setBounds (R (540, y0 + 90, 140, 20)); glideRate.setVisible (true); glideRate.setToggleState (proc.extra.glideRateMode, juce::dontSendNotification);
        restartPhase.setBounds (R (690, y0 + 90, 120, 20)); restartPhase.setVisible (true); restartPhase.setToggleState (proc.extra.restartPhase, juce::dontSendNotification);
        bendUp.setBounds (R (680, y0 + 8, 200, 18)); bendDown.setBounds (R (680, y0 + 32, 200, 18)); tune.setBounds (R (680, y0 + 56, 200, 18)); bpm.setBounds (R (960, y0 + 8, 200, 18));
        for (auto* c : std::initializer_list<juce::Component*> { &bendUp, &bendDown, &tune, &bpm }) c->setVisible (true);
        bendUp.setValue (proc.extra.bendUp, juce::dontSendNotification); bendDown.setValue (proc.extra.bendDown, juce::dontSendNotification);
        tune.setValue (proc.extra.globalTune, juce::dontSendNotification); bpm.setValue (proc.extra.presetBpm, juce::dontSendNotification);
        pageInfo.setText ("Glide, vibrato, pitch bend up / down (semitones), global tune and the preset tempo (used when the host gives none).", juce::dontSendNotification);
    }
    else if (page == 10)
    {
        genView->setBounds (R (422, y0 + 4, 250, 122));
        genOsc.setBounds (R (684, y0 + 6, 80, 20)); genRecipe.setBounds (R (770, y0 + 6, 150, 20)); genFrames.setBounds (R (926, y0 + 6, 100, 20));
        for (int i = 0; i < 3; ++i)
        {
            genAmtLabel[(size_t) i].setBounds (R (684, y0 + 34 + i * 26, 84, 18)); genAmt[(size_t) i].setBounds (R (776, y0 + 34 + i * 26, 244, 18));
        }
        genSeed.setBounds (R (1040, y0 + 6, 110, 20)); genDice.setBounds (R (1156, y0 + 6, 60, 20));
        genMake.setBounds (R (1040, y0 + 36, 176, 28)); genRestore.setBounds (R (1040, y0 + 72, 84, 22)); genExport.setBounds (R (1132, y0 + 72, 84, 22));
        for (auto* c : pageComps) if (c == &genOsc || c == &genRecipe || c == &genFrames || c == &genSeed || c == &genDice || c == &genMake || c == &genRestore || c == &genExport
                                      || c == genView.get() || c == &genAmt[0] || c == &genAmt[1] || c == &genAmt[2]
                                      || c == &genAmtLabel[0] || c == &genAmtLabel[1] || c == &genAmtLabel[2]) c->setVisible (true);
        pageInfo.setText ("Wavetable generator: a recipe writes a new table into the wavetable the chosen oscillator plays. Dice rolls the amounts and the seed. Generated tables are saved with the patch; Export writes a .wav for your folder.", juce::dontSendNotification);
    }
    else
    {
        if (auto* c = byId["voices"]) { c->setBounds (R (470, y0 + 6, 160, 18)); c->setVisible (true); }
        if (auto* c = byId["unison"]) { c->setBounds (R (470, y0 + 28, 160, 18)); c->setVisible (true); }
        if (auto* c = byId["voiceMode"]) { c->setBounds (R (422, y0 + 54, 110, 20)); c->setVisible (true); }
        if (auto* c = byId["trigger"]) { c->setBounds (R (540, y0 + 54, 110, 20)); c->setVisible (true); }
        const char* ids[] = { "uniPitchOn", "uniWtOn", "uniPanOn" }; const int fk[] = { kUniPitch, kUniWtPos, kUniPan };
        for (int i = 0; i < 3; ++i)
        {
            if (auto* c = byId[ids[i]]) { c->setBounds (R (680, y0 + 6 + i * 26, 110, 20)); c->setVisible (true); }
            if (auto* f = faderById[fk[i]]) { f->setBounds (R (796, y0 + 6 + i * 26, 220, 20)); f->setVisible (true); }
        }
        uniRange.setBounds (R (1024, y0 + 6, 150, 18)); uniRange.setVisible (true); uniRange.setValue (proc.extra.uniRangeUp, juce::dontSendNotification);
        chordMode.setBounds (R (1024, y0 + 30, 120, 20)); chordMode.setVisible (true); chordMode.setToggleState (proc.extra.uniChordMode, juce::dontSendNotification);
        voicesInfo.setBounds (R (422, y0 + 84, 240, 16)); voicesInfo.setVisible (true);
        pageInfo.setText ("Voices (polyphony), unison voices per note, mono / poly and the trigger mode; the spreads fan the unison voices out in pitch, wavetable position and pan.", juce::dontSendNotification);
    }
}

void AquaMassifEditor::updateLabels()
{
    // insert and fx knob labels follow their types
    auto setL = [this] (int k, const juce::String& s) { if (auto* m = knobById[k]) m->setLabel (s); };
    for (int i = 0; i < 2; ++i)
    {
        const int t = proc.choice ("i" + juce::String (i + 1) + "Type");
        static const char* p1[] = { "-", "Dry/Wet", "Dry/Wet", "Dry/Wet", "Dry/Wet", "Highpass", "Dry/Wet", "Dry/Wet", "Dry/Wet" };
        static const char* p2[] = { "-", "Time", "Pitch", "Crush", "Pitch", "Lowpass", "Drive", "Drive", "Drive" };
        setL (kIns1P1 + 2 * i, p1[juce::jlimit (0, 8, t)]); setL (kIns1P2 + 2 * i, p2[juce::jlimit (0, 8, t)]);
    }
    for (int f = 0; f < 2; ++f)
    {
        const int t = proc.choice ("fx" + juce::String (f + 1) + "Type");
        juce::StringArray l;
        switch (t)
        {
            case fxReverb: case fxReverbSmall: l = { "Dry/Wet", "Size", "Density", "Color" }; break;
            case fxChorus: case fxChorus1: case fxChorusE: l = { "Dry/Wet", "Rate", "Depth", "Offset" }; break;
            case fxFlangerPlus: case fxFlangerPlus1: case fxFlangerMinus: case fxFlangerMinus1: l = { "Dry/Wet", "Rate", "Depth", "Feedback" }; break;
            case fxPhaser: case fxPhaser1: l = { "Dry/Wet", "Rate", "Depth", "Feedback" }; break;
            case fxDelaySync: l = { "Dry/Wet", "Damp", "Feedback", "-" }; break;
            case fxDelay: l = { "Dry/Wet", "Damp", "Time L", "Time R" }; break;
            case fxDimExp: l = { "Dry/Wet", "Size", "-", "-" }; break;
            case fxClassicTube: case fxTubeTube: case fxBrightTube: l = { "Dry/Wet", "Drive", "-", "-" }; break;
            default: l = { "Dry/Wet", "-", "-", "-" }; break;
        }
        for (int k = 0; k < 4; ++k) setL (kFx1DW + 4 * f + k, l[k]);
    }
    for (int fl = 0; fl < 2; ++fl)
    {
        const int t = proc.choice ("f" + juce::String (fl + 1) + "Type");
        setL (kF1Bw + 3 * fl, t == ftComb ? "Damping" : (t == ftScream ? "Scream" : "Bandwidth"));
        setL (kF1Res + 3 * fl, t == ftComb ? "Feedback" : "Resonance");
        setL (kF1Cut + 3 * fl, t == ftComb ? "Pitch" : "Cutoff");
    }
    for (int m = 0; m < 8; ++m) macroNames[(size_t) m].setText (proc.extra.macroName[(size_t) m].empty() ? "Macro " + juce::String (m + 1) : juce::String (proc.extra.macroName[(size_t) m]), juce::dontSendNotification);
    presetName.setButtonText (juce::String (proc.extra.name));
    for (int o = 0; o < 3; ++o)
    {
        const int slot = proc.choice ("wt" + juce::String (o + 1));
        const int kind = proc.slotKind (slot);
        wtInfo[(size_t) o].setText (kind == AquaMassifProcessor::tkFile ? "original table"
                                    : kind == AquaMassifProcessor::tkGenerated ? "made in WT GEN"
                                    : kind == AquaMassifProcessor::tkUser ? "loaded: " + proc.slotUserName (slot).upToLastOccurrenceOf (".", false, false)
                                    : "stand-in (no file)", juce::dontSendNotification);
        wtInfo[(size_t) o].setTooltip (kind == AquaMassifProcessor::tkUser ? proc.slotUserName (slot) : juce::String());
    }
    folderB.setTooltip ("Wavetables: load a zip of them (as it is), or choose a folder. "
                        + juce::String (proc.getNumFileTables()) + " of 87 original tables are here"
                        + (proc.getNumFileTables() < 87 ? " (the others play stand-ins)." : "."));
}

void AquaMassifEditor::refreshAll()
{
    updateLabels();
    layoutPage();
    for (auto* k : knobs) k->repaint();
    repaint();
}

void AquaMassifEditor::timerCallback()
{
    meter = proc.outLevel.load();
    if (juce::Colour (proc.themeMetal) != metalTint || juce::Colour (proc.themeGlow) != glowTint) applyTheme();   // a state came in
    if (++uiTick % 8 == 0) updateLabels();
    voicesInfo.setText (juce::String (proc.activeVoices.load()) + " voices playing", juce::dontSendNotification);
    repaint (masterArea);
    // the mod page follows a type change
    if (currentPage >= 4 && currentPage < 8)
    {
        const int t = proc.choice ("mod" + juce::String (currentPage + 1) + "Type");
        if (t != lastModType) { lastModType = t; layoutPage(); }
    }
}

//==============================================================================
// files: a chosen file is read (and written) through its URL, which also works for the documents of Android's
// file picker, where there is no path
namespace
{
    juce::String chosenName (const juce::URL& url, const juce::File& f)
    {
        if (f != juce::File()) return f.getFileName();
        auto n = juce::URL::removeEscapeChars (url.toString (false));
        n = n.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf (":", false, false);
        return n;
    }
    // The picked file's bytes. getResult() first: on the desktop the file itself, on Android the app's own copy of
    // the picked document (the picker shim makes it). The document's URI is no help there: Android calls a
    // shared-storage document a local file (/storage/emulated/0/...) that the app may not read - every load
    // came back empty. The URI's stream is the fallback.
    bool readChosen (const juce::FileChooser& fc, juce::MemoryBlock& mb, juce::String& name, juce::File& file)
    {
        const auto url = fc.getURLResult();
        const auto res = fc.getResult();
        if (url.isEmpty() && res == juce::File()) return false;
        mb.reset();
        if (res.existsAsFile() && res.loadFileAsData (mb) && mb.getSize() > 0)
        {
            file = res; name = res.getFileName();
            return true;
        }
        mb.reset(); file = juce::File();
        if (url.isEmpty()) return false;
        name = chosenName (url, {});
        std::unique_ptr<juce::InputStream> in (url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)));
        if (in == nullptr) return false;
        in->readIntoMemoryBlock (mb);
        return mb.getSize() > 0;
    }
    // the bytes to the chosen place (a local file gets the extension); file: where it went, if a file the app
    // can find again. getResult() first, as above: on Android it is a scratch file whose bytes the picker shim
    // puts into the document afterwards.
    bool writeChosen (const juce::FileChooser& fc, const juce::MemoryBlock& mb, const juce::String& ext, juce::File& file, juce::String& name)
    {
        const auto url = fc.getURLResult();
        if (auto res = fc.getResult(); res != juce::File())
        {
            if (! res.hasFileExtension (ext)) res = res.withFileExtension (ext);
            name = res.getFileNameWithoutExtension();
            if (! res.replaceWithData (mb.getData(), mb.getSize())) return false;
           #if JUCE_ANDROID
            file = juce::File();      // (a scratch copy: not a place to browse from)
           #else
            file = res;
           #endif
            return true;
        }
        if (url.isEmpty()) return false;
        file = juce::File();
        name = chosenName (url, file).upToLastOccurrenceOf (".", false, false);
       #if AQP_HOSTED
        return aqp::files::writeDocument (url, mb.getData(), mb.getSize()).isEmpty();   // the app's one way to save
       #else
        std::unique_ptr<juce::OutputStream> out (url.createOutputStream());
        if (out == nullptr) return false;
        const bool ok = out->write (mb.getData(), mb.getSize());
        out->flush();
        return ok;
       #endif
    }
    void warn (const juce::String& text)
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Massif", text);
    }
    juce::File startDir (const juce::File& last)
    {
        if (last.existsAsFile()) return last.getParentDirectory();
        const auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Massif").getChildFile ("Presets");
        return d.isDirectory() ? d : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }
}

void AquaMassifEditor::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a Massif preset", startDir (proc.getLastPresetFile()), "*.massif;*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        juce::MemoryBlock mb; juce::String name; juce::File f;
        if (! readChosen (fc, mb, name, f)) return;
        juce::String err;
        if (AquaMassifProcessor::isBundleZip (mb)) { importBundle (mb, name); refreshAll(); return; }
        if (name.endsWithIgnoreCase (".nmsv"))
        {
            // a sound of the original picked here: it is imported
            if (! proc.importNmsvData (mb, name.upToLastOccurrenceOf (".", false, false), f, err)) warn (err);
        }
        else if (f.existsAsFile()) { if (! proc.loadPresetFile (f, err)) warn (err); }
        else if (! proc.loadPresetData (mb, name.upToLastOccurrenceOf (".", false, false), err)) warn (err);
        refreshAll();
    });
}

void AquaMassifEditor::savePreset()
{
    const auto dir = startDir (proc.getLastPresetFile());
    chooser = std::make_unique<juce::FileChooser> ("Save a Massif preset", dir.getChildFile (juce::File::createLegalFileName (juce::String (proc.extra.name)) + ".massif"), "*.massif");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getURLResult().isEmpty()) return;
        // the name it is saved under is the sound's name
        const auto url = fc.getURLResult();
        auto nm = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : chosenName (url, {}).upToLastOccurrenceOf (".", false, false);
        const auto keep = proc.extra.name;
        if (nm.isNotEmpty()) proc.extra.name = nm.toStdString();
        const auto mb = proc.presetData();
        juce::File f; juce::String name;
        if (writeChosen (fc, mb, ".massif", f, name)) proc.presetSaved (f, name.isNotEmpty() ? name : nm);
        else { proc.extra.name = keep; warn ("The preset could not be saved there."); }
        refreshAll();
    });
}

void AquaMassifEditor::importSound()
{
    auto last = proc.getLastPresetFile();
    chooser = std::make_unique<juce::FileChooser> ("Import a sound of the original (.nmsv), or a zip of them", startDir (last), "*.nmsv;*.zip");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        juce::MemoryBlock mb; juce::String name; juce::File f;
        if (! readChosen (fc, mb, name, f)) return;
        juce::String err;
        if (AquaMassifProcessor::isBundleZip (mb)) importBundle (mb, name);
        else if (! proc.importNmsvData (mb, name.upToLastOccurrenceOf (".", false, false), f, err)) warn (err);
        refreshAll();
    });
}

void AquaMassifEditor::chooseExportTemplate (std::function<void()> then)
{
    chooser = std::make_unique<juce::FileChooser> ("The export template: any .nmsv sound of yours (an init sound is best)", startDir (proc.getLastPresetFile()), "*.nmsv");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, then] (const juce::FileChooser& fc)
    {
        juce::MemoryBlock mb; juce::String name; juce::File f;
        if (! readChosen (fc, mb, name, f)) return;
        juce::String err;
        if (! proc.setExportTemplate (mb, err)) { warn (err); return; }
        if (then) juce::MessageManager::callAsync ([then] { then(); });
    });
}

void AquaMassifEditor::exportSound()
{
    if (! proc.hasExportTemplate())
    {
        // the .nmsv format is written over a sound file of the user's: one is needed once
        messageBox = juce::NativeMessageBox::showScopedAsync (juce::MessageBoxOptions::makeOptionsOkCancel (
                                               juce::MessageBoxIconType::InfoIcon, "Massif",
                                               "To write .nmsv files, Massif needs one sound file of the original as its layout "
                                               "(once - it is kept). Choose any .nmsv of yours; an init sound is best.",
                                               "Choose", "Cancel"),
                                           [this] (int r) { if (r == 1) chooseExportTemplate ([this] { exportSound(); }); });
        return;
    }
    const auto dir = startDir (proc.getLastPresetFile());
    chooser = std::make_unique<juce::FileChooser> ("Export as a sound of the original (.nmsv)", dir.getChildFile (juce::File::createLegalFileName (juce::String (proc.extra.name)) + ".nmsv"), "*.nmsv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto url = fc.getURLResult();
        if (url.isEmpty()) return;
        // the sound's name in the file: the name it is saved under
        const auto nm = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : chosenName (url, {}).upToLastOccurrenceOf (".", false, false);
        juce::MemoryBlock mb; juce::String err;
        if (! proc.exportNmsvData (mb, err, nm)) { warn (err); return; }
        juce::File f; juce::String name;
        if (! writeChosen (fc, mb, ".nmsv", f, name)) warn ("The sound could not be written there.");
    });
}

void AquaMassifEditor::importBundle (const juce::MemoryBlock& zip, const juce::String& fileName)
{
    juce::String err;
    const int n = proc.importBundle (zip, fileName.upToLastOccurrenceOf (".", false, false), err);
    if (n == 0) { warn (err); return; }
    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Massif",
        juce::String (n) + " sounds loaded from " + fileName + ". The preset menu lists them, < and > step through them, "
        "and they are kept with Aquanode Modular's patch.");
}

void AquaMassifEditor::stepPreset (int dir)
{
    // the bundle's sounds, while the current one is one of them
    if (const int n = proc.getNumBundleSounds(); n > 0 && proc.getBundleIndex() >= 0)
    {
        int i = proc.getBundleIndex();
        juce::String err;
        for (int k = 0; k < n; ++k)
        {
            i = (i + dir + n) % n;
            if (proc.loadBundleSound (i, err)) break;
        }
        refreshAll();
        return;
    }
    const auto cur = proc.getLastPresetFile();
    if (! cur.existsAsFile())
    {
        // no file loaded: through the built-in sounds
        const int n = proc.getNumFactoryPatches(); int i = proc.getFactoryIndex();
        i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
        proc.loadFactoryPatch (i); refreshAll();
        return;
    }
    // through the files of the same kind in its folder
    auto files = cur.getParentDirectory().findChildFiles (juce::File::findFiles, false, cur.hasFileExtension (".nmsv") ? "*.nmsv" : "*.massif");
    files.sort();
    int i = files.indexOf (cur); if (i < 0) i = 0;
    for (int n = 0; n < files.size(); ++n)
    {
        i = (i + dir + files.size()) % files.size();
        juce::String err;
        if (proc.loadPresetFile (files[i], err)) break;
    }
    refreshAll();
}

void AquaMassifEditor::tablesMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (proc.getNumFileTables()) + " of 87 original tables here");
    m.addItem (1, "Load wavetables from a zip ...");
    m.addItem (2, "Choose a wavetable folder ...");
    if (proc.getWavetableFolder() != juce::File())
        m.addItem (3, "Folder: " + proc.getWavetableFolder().getFileName(), false, false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folderB), [this] (int r)
    {
        if (r == 1) loadTableZip();
        else if (r == 2) chooseFolder();
    });
}

void AquaMassifEditor::loadTableZip()
{
    chooser = std::make_unique<juce::FileChooser> ("A zip of wavetables (.wav, any folders inside)", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.zip");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        juce::MemoryBlock mb; juce::String name; juce::File f;
        if (! readChosen (fc, mb, name, f)) return;
        juce::MemoryInputStream in (mb, false);
        juce::String err;
        const int n = proc.importWavetableZip (in, err);
        if (n == 0) warn (err.isNotEmpty() ? err : "No wavetables were taken from this zip.");
        else juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Massif",
                 juce::String (n) + " wavetables loaded; " + juce::String (proc.getNumFileTables()) + " of 87 original tables are here now.");
        refreshAll();
    });
}

void AquaMassifEditor::loadOscTable (int osc)
{
    chooser = std::make_unique<juce::FileChooser> ("A wavetable for oscillator " + juce::String (osc + 1), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.wav;*.flac;*.aif;*.aiff;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, osc] (const juce::FileChooser& fc)
    {
        juce::MemoryBlock mb; juce::String name; juce::File f;
        if (! readChosen (fc, mb, name, f)) return;
        juce::String err;
        if (proc.loadOscTable (osc, mb, name, err) < 0) warn (err);
        refreshAll();
    });
}

void AquaMassifEditor::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Folder with your wavetables (.wav)", proc.getWavetableFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult(); if (! dir.isDirectory()) return;
        const int n = proc.setWavetableFolder (dir);
        if (n == 0) warn ("No wavetables with known names were found in that folder.");
        refreshAll();
    });
}

//==============================================================================
void AquaMassifEditor::generatePatch()
{
    const int id = typeBox.getSelectedId();
    const int type = id == 100 ? -1 : juce::jmax (0, id - 1);
    const int genre = juce::jmax (0, genreBox.getSelectedId() - 1);
    proc.generatePatch (type, genre);
    refreshAll();
}

void AquaMassifEditor::presetMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Built-in sounds");
    const auto& fp = massif::factoryPatches();
    juce::StringArray cats;
    for (auto& f : fp) cats.addIfNotAlreadyThere (f.category);
    for (auto& c : cats)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) fp.size(); ++i)
            if (c == fp[(size_t) i].category) sub.addItem (100 + i, fp[(size_t) i].name, true, proc.getFactoryIndex() == i);
        m.addSubMenu (c, sub);
    }
    if (const int nb = proc.getNumBundleSounds(); nb > 0)
    {
        // the bundle: its folders (without the part they all share) as submenus, long ones in runs of 50
        m.addSectionHeader (proc.getBundleName() + " (" + juce::String (nb) + " sounds)");
        juce::String common = proc.getBundleSound (0).folder;
        for (int i = 1; i < nb && common.isNotEmpty(); ++i)
        {
            const auto& f = proc.getBundleSound (i).folder;
            while (common.isNotEmpty() && ! (f == common || f.startsWith (common + "/"))) common = common.upToLastOccurrenceOf ("/", false, false);
        }
        auto shown = [&common] (const juce::String& folder)
        {
            auto r = common.isEmpty() ? folder : folder.substring (common.length()).trimCharactersAtStart ("/");
            return r.isEmpty() ? juce::String ("Sounds") : r;
        };
        const int cur = proc.getBundleIndex();
        for (int i = 0; i < nb;)
        {
            const auto folder = proc.getBundleSound (i).folder;
            int j = i; while (j < nb && proc.getBundleSound (j).folder == folder) ++j;
            juce::PopupMenu sub;
            const int count = j - i;
            for (int c = i; c < j; c += 50)
            {
                const int e = juce::jmin (j, c + 50);
                juce::PopupMenu part;
                for (int k = c; k < e; ++k) (count > 60 ? part : sub).addItem (10000 + k, proc.getBundleSound (k).name, true, k == cur);
                if (count > 60)
                    sub.addSubMenu (proc.getBundleSound (c).name.substring (0, 12).trim() + " ... " + proc.getBundleSound (e - 1).name.substring (0, 12).trim(),
                                    part, true, nullptr, cur >= c && cur < e);
            }
            m.addSubMenu (shown (folder), sub, true, nullptr, cur >= i && cur < j);
            i = j;
        }
        m.addItem (7, "Forget this bundle");
    }
    m.addSeparator();
    m.addItem (1, "Load a Massif preset (.massif) ...");
    m.addItem (2, "Save as a Massif preset (.massif) ...");
    m.addItem (4, "Import a sound of the original (.nmsv), or a zip of them ...");
    m.addItem (5, "Export as a sound of the original (.nmsv) ...");
    m.addItem (6, "Choose the export template (.nmsv) ...");
    m.addSeparator();
    m.addItem (3, "Init");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName), [this] (int r)
    {
        if (r >= 10000) { juce::String err; if (! proc.loadBundleSound (r - 10000, err)) warn (err); refreshAll(); }
        else if (r >= 100) { proc.loadFactoryPatch (r - 100); refreshAll(); }
        else if (r == 7) { proc.clearBundle(); refreshAll(); }
        else if (r == 1) loadPreset();
        else if (r == 2) savePreset();
        else if (r == 3) { proc.initPatch(); refreshAll(); }
        else if (r == 4) importSound();
        else if (r == 5) exportSound();
        else if (r == 6) chooseExportTemplate ({});
    });
}

void AquaMassifEditor::recipeChanged()
{
    const int r = juce::jmax (0, genRecipe.getSelectedId() - 1);
    for (int i = 0; i < 3; ++i) genAmtLabel[(size_t) i].setText (gen::recipeParam (r, i), juce::dontSendNotification);
}

void AquaMassifEditor::generate()
{
    const int osc = juce::jmax (1, genOsc.getSelectedId());
    proc.generateTable (proc.choice ("wt" + juce::String (osc)), juce::jmax (0, genRecipe.getSelectedId() - 1),
                        (float) genAmt[0].getValue(), (float) genAmt[1].getValue(), (float) genAmt[2].getValue(),
                        (int) genSeed.getValue(), juce::jmax (8, genFrames.getSelectedId()));
    updateLabels();
}

void AquaMassifEditor::exportTable()
{
    const int slot = proc.choice ("wt" + juce::String (juce::jmax (1, genOsc.getSelectedId())));
    auto dir = proc.getWavetableFolder();
    if (! dir.isDirectory()) dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Export the wavetable", dir.getChildFile (juce::String (massif::slotInfos()[(size_t) slot].fileHint) + ".wav"), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, slot] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult(); if (f == juce::File()) return;
        if (! f.hasFileExtension (".wav")) f = f.withFileExtension (".wav");
        if (! proc.exportTable (slot, f))
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Massif", "The file could not be written.");
    });
}

//==============================================================================
void WaveView::timerCallback()
{
    const int slot = ed.proc.choice ("wt" + juce::String (osc + 1));
    const float pos = ed.proc.posView[juce::jlimit (0, 2, osc)].load();   // (6.9.16: the stack shows it too)
    const auto t = ed.proc.builtTable (slot);
    if (slot != lastSlot || std::abs (pos - lastPos) > 0.002f || t.get() != lastTable) { lastSlot = slot; lastPos = pos; lastTable = t.get(); repaint(); }
}

void WaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    crtGlass (g, r);
    const int slot = ed.proc.choice ("wt" + juce::String (osc + 1));
    const auto t = ed.proc.builtTable (slot);
    auto in = r.reduced (5.0f, 5.0f);
    if (t == nullptr || t->numFrames < 1)
    {
        glowText (g, "...", in, juce::Justification::centred, phosphor.withAlpha (0.7f), 11.0f);
        return;
    }
    const int L = massif::kLevels - 1;
    auto frame = [&] (float fpos, juce::Rectangle<float> box, float alpha, float thick)
    {
        const float x = juce::jlimit (0.0f, 1.0f, fpos) * (float) (t->numFrames - 1);
        const int f0 = juce::jmin (t->numFrames - 1, (int) x), f1 = juce::jmin (t->numFrames - 1, f0 + 1); const float ff = x - (float) f0;
        const float* a = t->level (f0, L); const float* b = t->level (f1, L);
        juce::Path p; const int n = juce::jmax (16, (int) box.getWidth());
        for (int i = 0; i <= n; ++i)
        {
            const int idx = juce::jmin (massif::kFrameSize - 1, i * massif::kFrameSize / n);
            const float v = juce::jlimit (-1.0f, 1.0f, (a[idx] + ff * (b[idx] - a[idx])) * 0.62f);
            const float px = box.getX() + box.getWidth() * (float) i / (float) n, py = box.getCentreY() - v * box.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (accent.withAlpha (0.25f * alpha)); g.strokePath (p, juce::PathStrokeType (thick + 2.0f));
        g.setColour (phosphor.withAlpha (alpha)); g.strokePath (p, juce::PathStrokeType (thick));
    };
    if (! stack)
    {
        g.setColour (phosphor.withAlpha (0.12f)); g.drawLine (in.getX(), in.getCentreY(), in.getRight(), in.getCentreY(), 1.0f);
        const float now = juce::jlimit (0.0f, 1.0f, ed.proc.posView[juce::jlimit (0, 2, osc)].load());
        frame (now, in, 1.0f, 1.3f);
        // (6.9.16) where in the table it plays: a bar along the bottom, its marker moving with the position
        auto bar = in.reduced (4.0f, 0.0f).removeFromBottom (4.0f);
        g.setColour (phosphor.withAlpha (0.18f)); g.fillRoundedRectangle (bar, 1.5f);
        const float mx = bar.getX() + now * bar.getWidth();
        g.setColour (accent.withAlpha (0.35f)); g.fillRoundedRectangle (juce::Rectangle<float> (mx - 4.0f, bar.getY() - 3.0f, 8.0f, bar.getHeight() + 6.0f), 2.0f);
        g.setColour (phosphor); g.fillRoundedRectangle (juce::Rectangle<float> (mx - 2.0f, bar.getY() - 2.0f, 4.0f, bar.getHeight() + 4.0f), 1.5f);
        return;
    }
    // the whole table: frames stacked from the back (last frame) to the front (first)
    const int n = juce::jmin (12, t->numFrames);
    const float dx = in.getWidth() * 0.30f, dy = in.getHeight() * 0.45f;
    for (int k = n - 1; k >= 0; --k)
    {
        const float u = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
        auto box = juce::Rectangle<float> (in.getX() + dx * u, in.getBottom() - in.getHeight() * 0.55f - dy * u, in.getWidth() - dx, in.getHeight() * 0.55f);
        frame (u, box, 0.35f + 0.65f * (1.0f - u), 1.1f);
    }
    // (6.9.16) where it plays now, bright in the stack, as it moves
    {
        const float u = juce::jlimit (0.0f, 1.0f, ed.proc.posView[juce::jlimit (0, 2, osc)].load());
        auto box = juce::Rectangle<float> (in.getX() + dx * u, in.getBottom() - in.getHeight() * 0.55f - dy * u, in.getWidth() - dx, in.getHeight() * 0.55f);
        frame (u, box, 1.0f, 1.9f);
    }
    glowText (g, juce::String (massif::slotInfos()[(size_t) slot].name) + "  -  " + juce::String (t->numFrames) + " frames", in.removeFromTop (12.0f),
              juce::Justification::topLeft, phosphor.withAlpha (0.8f), 10.0f);
}

//==============================================================================
// The colours - tap the MASSIF name
//==============================================================================
namespace
{
class ThemePicker : public juce::Component, private juce::ChangeListener
{
public:
    std::function<void (juce::Colour, juce::Colour)> onChange;

    ThemePicker (juce::Colour metal, juce::Colour glow)
    {
        const int flags = juce::ColourSelector::showColourAtTop | juce::ColourSelector::editableColour | juce::ColourSelector::showColourspace;
        for (auto* l : { &metalLbl, &glowLbl })
        {
            l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
            l->setJustificationType (juce::Justification::centred);
            l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
            addAndMakeVisible (*l);
        }
        metalLbl.setText ("Metal", juce::dontSendNotification);
        glowLbl.setText ("Glow", juce::dontSendNotification);
        for (auto* sel : { &metalSel, &glowSel })
        {
            *sel = std::make_unique<juce::ColourSelector> (flags);
            (*sel)->addChangeListener (this);
            addAndMakeVisible (**sel);
        }
        metalSel->setCurrentColour (metal, juce::dontSendNotification);
        glowSel->setCurrentColour (glow, juce::dontSendNotification);
        reset.setTooltip ("Back to the silver and the blue");
        reset.onClick = [this]
        {
            metalSel->setCurrentColour (amui::metalDefault, juce::dontSendNotification);
            glowSel->setCurrentColour (amui::glowDefault, juce::dontSendNotification);
            changeListenerCallback (nullptr);
        };
        addAndMakeVisible (reset);
        setSize (2 * 230 + 3 * 8, 8 + 18 + 250 + 8 + 26 + 8);
    }
    ~ThemePicker() override { metalSel->removeChangeListener (this); glowSel->removeChangeListener (this); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        reset.setBounds (r.removeFromBottom (26).withSizeKeepingCentre (90, 26));
        r.removeFromBottom (8);
        auto left = r.removeFromLeft ((r.getWidth() - 8) / 2);
        r.removeFromLeft (8);
        metalLbl.setBounds (left.removeFromTop (18)); metalSel->setBounds (left);
        glowLbl.setBounds (r.removeFromTop (18));     glowSel->setBounds (r);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (onChange) onChange (metalSel->getCurrentColour(), glowSel->getCurrentColour());
    }
    juce::Label metalLbl, glowLbl;
    std::unique_ptr<juce::ColourSelector> metalSel, glowSel;
    juce::TextButton reset { "RESET" };
};
}

juce::Rectangle<int> AquaMassifEditor::logoArea() const
{
    const float sc = (float) getWidth() / 1260.0f;
    return juce::Rectangle<float> (1040.0f * sc, 2.0f * sc, 214.0f * sc, 34.0f * sc).toNearestInt();
}

void AquaMassifEditor::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (logoArea().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void AquaMassifEditor::mouseDown (const juce::MouseEvent& e)
{
    if (logoArea().contains (e.getPosition())) showThemePicker();
}

void AquaMassifEditor::showThemePicker()
{
    auto picker = std::make_unique<ThemePicker> (juce::Colour (proc.themeMetal), juce::Colour (proc.themeGlow));
    juce::Component::SafePointer<AquaMassifEditor> safe (this);
    picker->onChange = [safe] (juce::Colour metal, juce::Colour glow)
    {
        if (safe == nullptr) return;
        safe->proc.themeMetal = metal.withAlpha (1.0f).getARGB();
        safe->proc.themeGlow = glow.withAlpha (1.0f).getARGB();
        safe->applyTheme();
    };
    juce::CallOutBox::launchAsynchronously (std::move (picker), logoArea(), this);
}

void AquaMassifEditor::applyTheme()
{
    setTheme (juce::Colour (proc.themeMetal), juce::Colour (proc.themeGlow));
    look.refresh();
    // the lettering that carries its own colour
    for (auto& l : wtInfo) l.setColour (juce::Label::textColourId, textDim);
    for (auto& l : genAmtLabel) l.setColour (juce::Label::textColourId, textDim);
    for (auto& l : macroNames) l.setColour (juce::Label::textColourId, sourceInk (msMacro1));
    pageInfo.setColour (juce::Label::textColourId, textDim);
    voicesInfo.setColour (juce::Label::textColourId, textDim);
    chassis = juce::Image();
    sendLookAndFeelChange();
    repaint();
}
