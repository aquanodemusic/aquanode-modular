#include "PluginEditor.h"

namespace akui
{
//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, panelEdge);
    setColour (juce::ComboBox::outlineColourId, panelEdge.brighter (0.2f));
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, bg);
    setColour (juce::Label::textColourId, dim);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float angle = start + pos * (end - start);
    const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, radius - 3, radius - 3, 0, start, end, true);
    value.addCentredArc (c.x, c.y, radius - 3, radius - 3, 0, start, angle, true);
    g.setColour (panelEdge.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (panel.brighter (0.25f));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.15f, radius * 1.15f).withCentre (c));
    g.setColour (col.brighter (0.4f));
    g.drawLine (juce::Line<float> (c, c.getPointOnCircumference (radius * 0.5f, angle)), 2.5f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto tint = b.findColour (juce::TextButton::buttonOnColourId);
    g.setColour (on ? tint : (down ? panelEdge.brighter (0.3f) : over ? panelEdge.brighter (0.15f) : panelEdge));
    g.fillRoundedRectangle (r, 5.0f);
    if (! on)
    {
        g.setColour (tint.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
}

//==============================================================================
Knob::Knob (const juce::String& name)
{
    caption.setText (name, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (juce::Font (juce::FontOptions (12.0f)));
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 14);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, text);
    addAndMakeVisible (slider);
}

void Knob::attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    att.reset();
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    // readable values: whole Hz and cents, two decimals elsewhere
    const auto range = slider.getRange();
    slider.setNumDecimalPlacesToDisplay (range.getLength() > 90.0 ? 0 : 2);
    slider.setTextValueSuffix (id.containsIgnoreCase ("cutoff") ? " Hz" : id.containsIgnoreCase ("detune") ? " ct" : "");
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (15));
    slider.setBounds (r);
}

//==============================================================================
// harmonic bars: the height is the level in dB (48 dB tall), the colour the bus
static constexpr float kBarRangeDb = 48.0f;

int HarmonicBars::barAt (float x) const
{
    const auto r = getLocalBounds().toFloat().reduced (8, 6);
    return juce::jlimit (0, 62, (int) ((x - r.getX()) / r.getWidth() * 63.0f));
}

float HarmonicBars::levelAt (float y) const
{
    const auto r = getLocalBounds().toFloat().reduced (8, 6).withTrimmedBottom (14);
    const float frac = 1.0f - (y - r.getY()) / r.getHeight();
    if (frac <= 0.02f) return 0.0f;
    return juce::Decibels::decibelsToGain (juce::jmin (1.0f, frac) * kBarRangeDb - kBarRangeDb);
}

void HarmonicBars::apply (int bar, float y)
{
    auto& h = proc.harmonics[(size_t) source];
    if (paintBus >= 0)
        proc.setHarmonic (source, bar, h.level[(size_t) bar].load(), paintBus);
    else
        proc.setHarmonic (source, bar, levelAt (y), h.group[(size_t) bar].load());
}

void HarmonicBars::mouseDown (const juce::MouseEvent& e)
{
    lastBar = barAt (e.position.x);
    lastY = e.position.y;
    apply (lastBar, lastY);
    repaint();
}

void HarmonicBars::mouseDrag (const juce::MouseEvent& e)
{
    const int bar = barAt (e.position.x);
    if (lastBar < 0) lastBar = bar;
    // fill every bar the finger passed, so a quick stroke leaves no gaps
    const int lo = juce::jmin (bar, lastBar), hi = juce::jmax (bar, lastBar);
    for (int b = lo; b <= hi; ++b)
    {
        const float t = hi == lo ? 1.0f : (float) (b - lastBar) / (float) (bar - lastBar);
        apply (b, lastY + (e.position.y - lastY) * t);
    }
    lastBar = bar;
    lastY = e.position.y;
    repaint();
}

void HarmonicBars::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (8, 6);
    auto axis = r.removeFromBottom (14);

    // dB grid
    g.setColour (panelEdge);
    for (int db = 12; db < (int) kBarRangeDb; db += 12)
    {
        const float y = r.getY() + r.getHeight() * (float) db / kBarRangeDb;
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }

    const auto* modeParam = proc.apvts.getRawParameterValue (source == 0 ? "s1_harmMode" : "s2_harmMode");
    const auto* tiltParam = proc.apvts.getRawParameterValue (source == 0 ? "s1_harmTilt" : "s2_harmTilt");
    const int mode = modeParam != nullptr ? (int) modeParam->load() : 0;
    const float tilt = tiltParam != nullptr ? juce::jmin (0.95f, tiltParam->load()) : 0.0f;
    auto passes = [mode] (int n)
    {
        switch (mode)
        {
            case 1: return (n % 2) == 1;
            case 2: return n == 1 || (n % 2) == 0;
            case 3: return (n & (n - 1)) == 0;
            case 4: { int m = n; while ((m % 3) == 0) m /= 3; return (m & (m - 1)) == 0; }
            default: return true;
        }
    };
    auto heightOf = [] (float level)
    {
        if (level <= 0.0f) return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (level) + kBarRangeDb) / kBarRangeDb);
    };

    const float bw = r.getWidth() / 63.0f;
    const auto& h = proc.harmonics[(size_t) source];
    for (int i = 0; i < 63; ++i)
    {
        const int n = i + 1;
        const float lv = h.level[(size_t) i].load();
        const int grp = h.group[(size_t) i].load();
        const bool on = passes (n);
        auto col = busColour (grp);
        const float x = r.getX() + bw * (float) i;
        const float hgt = heightOf (lv) * r.getHeight();
        g.setColour (on ? col.withAlpha (0.88f) : col.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (x + 1.0f, r.getBottom() - hgt, juce::jmax (1.0f, bw - 2.0f), hgt));
        // what Tilt makes of it: a tick where the level ends up
        if (on && std::abs (tilt) > 0.01f && lv > 0.0f)
        {
            const float th = heightOf (juce::jmin (1.0f, lv * std::pow ((float) n, tilt))) * r.getHeight();
            g.setColour (text.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, r.getBottom() - th - 1.0f, juce::jmax (1.0f, bw - 2.0f), 2.0f));
        }
    }

    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (int n : { 1, 8, 16, 24, 32, 48, 63 })
        g.drawText (juce::String (n), juce::Rectangle<float> (r.getX() + bw * (float) (n - 1) - 8, axis.getY() + 1, bw + 16, 12),
                    juce::Justification::centred);
}

//==============================================================================
void EnvelopeCurve::setIds (const juce::String& a, const juce::String& d, const juce::String& s, const juce::String& r,
                            juce::Colour c, const juce::String& t)
{
    ids = { a, d, s, r };
    colour = c;
    title = t;
    repaint();
}

float EnvelopeCurve::value (const juce::String& id) const
{
    if (auto* p = proc.apvts.getRawParameterValue (id)) return p->load();
    return 0.0f;
}

void EnvelopeCurve::setValue (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

float EnvelopeCurve::segWidth (const juce::String& id, float t, float maxW) const
{
    if (auto* p = proc.apvts.getParameter (id))
        return maxW * p->convertTo0to1 (t);
    return 0.0f;
}

float EnvelopeCurve::segTime (const juce::String& id, float w, float maxW) const
{
    if (auto* p = proc.apvts.getParameter (id))
        return p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, w / maxW));
    return 0.0f;
}

juce::Path EnvelopeCurve::curveFor (const std::array<juce::String, 4>& e, juce::Rectangle<float> a) const
{
    const float seg = a.getWidth() * 0.28f, hold = a.getWidth() * 0.16f;
    const float wA = segWidth (e[0], value (e[0]), seg), wD = segWidth (e[1], value (e[1]), seg),
                wR = segWidth (e[3], value (e[3]), seg);
    const float s = juce::jlimit (0.0f, 1.0f, value (e[2]));
    const float x0 = a.getX(), top = a.getY(), bottom = a.getBottom();
    const float ys = bottom - s * a.getHeight();
    juce::Path p;
    p.startNewSubPath (x0, bottom);
    p.quadraticTo (x0 + wA * 0.35f, top + (bottom - top) * 0.15f, x0 + wA, top);          // attack, a little curved
    p.quadraticTo (x0 + wA + wD * 0.2f, ys, x0 + wA + wD, ys);                            // decay, exponential-looking
    p.lineTo (x0 + wA + wD + hold, ys);                                                   // sustain while held
    p.quadraticTo (x0 + wA + wD + hold + wR * 0.2f, bottom, x0 + wA + wD + hold + wR, bottom);   // release
    return p;
}

void EnvelopeCurve::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (r, 6.0f);
    const auto a = plotArea();

    g.setColour (panelEdge);
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * (float) i / 4.0f), a.getX(), a.getRight());

    for (const auto& gh : ghosts)
    {
        int bus = 0;
        if (gh[0].contains ("grp")) bus = gh[0].fromFirstOccurrenceOf ("grp", false, false).getIntValue();
        g.setColour (busColour (bus).withAlpha (0.28f));
        g.strokePath (curveFor (gh, a), juce::PathStrokeType (1.5f));
    }

    if (ids[0].isEmpty()) return;
    auto p = curveFor (ids, a);
    juce::Path fill (p);
    fill.lineTo (a.getRight(), a.getBottom());
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // handles
    const float seg = a.getWidth() * 0.28f, hold = a.getWidth() * 0.16f;
    const float wA = segWidth (ids[0], value (ids[0]), seg), wD = segWidth (ids[1], value (ids[1]), seg),
                wR = segWidth (ids[3], value (ids[3]), seg);
    const float ys = a.getBottom() - juce::jlimit (0.0f, 1.0f, value (ids[2])) * a.getHeight();
    handles = { juce::Point<float> (a.getX() + wA, a.getY()),
                juce::Point<float> (a.getX() + wA + wD, ys),
                juce::Point<float> (a.getX() + wA + wD + hold + wR, a.getBottom()) };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == dragging ? colour.brighter (0.5f) : colour);
        g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (handles[(size_t) i]));
        g.setColour (bg);
        g.drawEllipse (juce::Rectangle<float> (14, 14).withCentre (handles[(size_t) i]), 2.0f);
    }

    // title and readout
    auto fmt = [] (float t) { return t < 1.0f ? juce::String (juce::roundToInt (t * 1000.0f)) + " ms" : juce::String (t, 2) + " s"; };
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.setColour (colour);
    g.drawText (title, getLocalBounds().reduced (10, 3).removeFromTop (16), juce::Justification::left);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (dim);
    g.drawText ("A " + fmt (value (ids[0])) + "   D " + fmt (value (ids[1])) + "   S " + juce::String (juce::roundToInt (value (ids[2]) * 100.0f))
                    + " %   R " + fmt (value (ids[3])),
                getLocalBounds().reduced (10, 3).removeFromTop (16), juce::Justification::right);
}

void EnvelopeCurve::mouseDown (const juce::MouseEvent& e)
{
    dragging = -1;
    float best = 30.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = e.position.getDistanceFrom (handles[(size_t) i]);
        if (d < best) { best = d; dragging = i; }
    }
    if (dragging < 0)   // a tap away from the handles: the nearest one along x
    {
        float bx = 1.0e9f;
        for (int i = 0; i < 3; ++i)
            if (std::abs (e.position.x - handles[(size_t) i].x) < bx) { bx = std::abs (e.position.x - handles[(size_t) i].x); dragging = i; }
    }
    for (auto& id : ids)
        if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture();
    mouseDrag (e);
}

void EnvelopeCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || ids[0].isEmpty()) return;
    const auto a = plotArea();
    const float seg = a.getWidth() * 0.28f, hold = a.getWidth() * 0.16f;
    const float wA = segWidth (ids[0], value (ids[0]), seg), wD = segWidth (ids[1], value (ids[1]), seg);
    switch (dragging)
    {
        case 0: setValue (ids[0], segTime (ids[0], e.position.x - a.getX(), seg)); break;
        case 1:
            setValue (ids[1], segTime (ids[1], e.position.x - a.getX() - wA, seg));
            setValue (ids[2], juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight()));
            break;
        case 2: setValue (ids[3], segTime (ids[3], e.position.x - a.getX() - wA - wD - hold, seg)); break;
        default: break;
    }
    repaint();
}

void EnvelopeCurve::mouseUp (const juce::MouseEvent&)
{
    for (auto& id : ids)
        if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture();
    dragging = -1;
    repaint();
}

//==============================================================================
static constexpr float kFLo = 80.0f, kFHi = 12000.0f;

float FormantView::xOf (float f) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (f / kFLo) / std::log (kFHi / kFLo);
}

float FormantView::fOf (float x) const
{
    const auto a = plotArea();
    return kFLo * std::pow (kFHi / kFLo, juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth()));
}

float FormantView::get (int band, const char* what) const
{
    if (auto* p = proc.apvts.getRawParameterValue ("dftBand" + juce::String (band) + "_" + what)) return p->load();
    return 0.0f;
}

void FormantView::set (int band, const char* what, float v)
{
    if (auto* p = proc.apvts.getParameter ("dftBand" + juce::String (band) + "_" + what))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void FormantView::paint (juce::Graphics& g)
{
    g.setColour (bg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const auto a = plotArea();

    g.setColour (panelEdge);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float f : { 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (f);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (dim);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 18, a.getY() - 13, 36, 12), juce::Justification::centred);
        g.setColour (panelEdge);
    }

    // the shape: each band a bump as wide as its Q makes it (the engine's own law)
    juce::Path curve;
    std::vector<float> sums;
    float peakSum = 1.0f;
    for (int px = 0; px <= (int) a.getWidth(); px += 2)
    {
        const float f = fOf (a.getX() + (float) px);
        float sum = 0.0f;
        for (int b = 0; b < 11; ++b)
        {
            const float gain = get (b, "gain");
            if (gain <= 0.0f) continue;
            const float width = 0.35f + (1.0f - get (b, "q")) * 1.6f;
            const float d = (std::log2 (f) - std::log2 (get (b, "freq"))) / width;
            sum += gain * std::exp (-d * d);
        }
        sums.push_back (sum);
        peakSum = juce::jmax (peakSum, sum);
    }
    for (size_t i = 0; i < sums.size(); ++i)
    {
        // several bands pile up: the shape is scaled to fit rather than cut off
        const float y = a.getBottom() - sums[i] / peakSum * a.getHeight();
        const float x = a.getX() + (float) (i * 2);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    juce::Path fill (curve);
    fill.lineTo (a.getRight(), a.getBottom());
    fill.lineTo (a.getX(), a.getBottom());
    fill.closeSubPath();
    g.setColour (accent.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    for (int b = 0; b < 11; ++b)
    {
        const juce::Point<float> pt (xOf (get (b, "freq")), a.getBottom() - get (b, "gain") * a.getHeight());
        const bool sel = b == selected;
        g.setColour (sel ? accent.brighter (0.4f) : text.withAlpha (0.75f));
        g.fillEllipse (juce::Rectangle<float> (sel ? 16.0f : 12.0f, sel ? 16.0f : 12.0f).withCentre (pt));
        g.setColour (bg);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (16, 16).withCentre (pt), juce::Justification::centred);
    }
}

void FormantView::mouseDown (const juce::MouseEvent& e)
{
    const auto a = plotArea();
    float best = 1.0e9f;
    dragging = -1;
    for (int b = 0; b < 11; ++b)
    {
        const juce::Point<float> pt (xOf (get (b, "freq")), a.getBottom() - get (b, "gain") * a.getHeight());
        const float d = e.position.getDistanceFrom (pt);
        if (d < best) { best = d; dragging = b; }
    }
    if (best > 28.0f)   // nowhere near a dot: the band nearest in frequency
    {
        best = 1.0e9f;
        for (int b = 0; b < 11; ++b)
            if (std::abs (xOf (get (b, "freq")) - e.position.x) < best) { best = std::abs (xOf (get (b, "freq")) - e.position.x); dragging = b; }
    }
    selected = dragging;
    if (onSelect) onSelect (selected);
    mouseDrag (e);
}

void FormantView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto a = plotArea();
    set (dragging, "freq", fOf (e.position.x));
    set (dragging, "gain", juce::jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight()));
    repaint();
}
} // namespace akui

//==============================================================================
K5AudioProcessorEditor::K5AudioProcessorEditor (K5AudioProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);
    rng.setSeedRandomly();
    sourceTabs[0].setButtonText ("Source 1");
    sourceTabs[1].setButtonText ("Source 2");
    for (int g = 0; g < 4; ++g) paintBus[g].setButtonText ("Bus " + juce::String (g + 1));
    const char* envNames[7] = { "Bus 1", "Bus 2", "Bus 3", "Bus 4", "Amp", "Filter", "Pitch" };
    for (int e = 0; e < 7; ++e) envTabs[e].setButtonText (envNames[e]);
    const char* vowelNames[6] = { "A", "E", "I", "O", "U", "Flat" };
    for (int v = 0; v < 6; ++v) vowels[v].setButtonText (vowelNames[v]);
    auto& s = processor.apvts;

    auto tint = [] (juce::TextButton& b, juce::Colour c)
    {
        b.setColour (juce::TextButton::buttonOnColourId, c);
        b.setClickingTogglesState (false);
    };

    for (auto* b : { &presetButton, &prevButton, &nextButton })
    {
        tint (*b, akui::accent);
        addAndMakeVisible (*b);
    }
    presetButton.onClick = [this] { showPresetMenu(); };
    prevButton.onClick = [this]
    {
        const int n = K5AudioProcessor::factoryNames().size();
        factoryIndex = (factoryIndex <= 0 ? n : factoryIndex) - 1;
        processor.loadFactory (factoryIndex);
    };
    nextButton.onClick = [this]
    {
        const int n = K5AudioProcessor::factoryNames().size();
        factoryIndex = (factoryIndex + 1) % n;
        processor.loadFactory (factoryIndex);
    };

    for (int i = 0; i < 2; ++i)
    {
        tint (sourceTabs[i], akui::accent);
        sourceTabs[i].onClick = [this, i] { selectSource (i); };
        addAndMakeVisible (sourceTabs[i]);
    }
    master.attach (s, "masterGain");
    master.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (master);

    // harmonics
    addAndMakeVisible (bars);
    tint (paintLevel, akui::text.darker (0.2f));
    paintLevel.onClick = [this]
    {
        bars.paintBus = -1;
        paintLevel.setToggleState (true, juce::dontSendNotification);
        for (auto& b : paintBus) b.setToggleState (false, juce::dontSendNotification);
    };
    addAndMakeVisible (paintLevel);
    for (int g = 0; g < 4; ++g)
    {
        tint (paintBus[g], akui::busColour (g));
        paintBus[g].setTooltip ("Paint harmonics onto envelope bus " + juce::String (g + 1));
        paintBus[g].onClick = [this, g]
        {
            bars.paintBus = g;
            paintLevel.setToggleState (false, juce::dontSendNotification);
            for (int k = 0; k < 4; ++k) paintBus[k].setToggleState (k == g, juce::dontSendNotification);
            selectEnvelope (g);
        };
        addAndMakeVisible (paintBus[g]);
    }
    paintLevel.setToggleState (true, juce::dontSendNotification);
    tint (resetHarm, akui::accent);
    resetHarm.onClick = [this] { processor.resetHarmonics (source); bars.repaint(); };
    addAndMakeVisible (resetHarm);
    harmMode.addItemList ({ "All", "Odd", "Even", "Octave", "Fifth" }, 1);
    addAndMakeVisible (harmMode);
    for (auto* k : { &tilt, &level, &detune }) addAndMakeVisible (*k);

    // envelopes
    addAndMakeVisible (envelope);
    for (int e = 0; e < 7; ++e)
    {
        tint (envTabs[e], e < 4 ? akui::busColour (e) : akui::text.darker (0.15f));
        envTabs[e].onClick = [this, e] { selectEnvelope (e); };
        addAndMakeVisible (envTabs[e]);
    }
    for (auto* k : { &cutoff, &resonance, &envAmount, &pitchDepth }) addAndMakeVisible (*k);
    slope.addItemList ({ "12 dB/oct", "24 dB/oct" }, 1);
    addAndMakeVisible (slope);

    // formants
    addAndMakeVisible (formants);
    formants.onSelect = [this] (int b) { bandQ.attach (processor.apvts, "dftBand" + juce::String (b) + "_q"); };
    for (int v = 0; v < 6; ++v)
    {
        tint (vowels[v], akui::accent);
        vowels[v].onClick = [this, v] { setVowel (v); };
        addAndMakeVisible (vowels[v]);
    }
    bandQ.attach (s, "dftBand2_q");
    addAndMakeVisible (bandQ);

    // LFO
    lfoShape.addItemList ({ "Triangle", "Saw", "Square", "Random" }, 1);
    lfoShapeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, "lfoShape", lfoShape);
    addAndMakeVisible (lfoShape);
    lfoRate.attach (s, "lfoRate");   lfoDelay.attach (s, "lfoDelay");
    lfoVib.attach (s, "lfoVibrato"); lfoTrem.attach (s, "lfoTremolo"); lfoFilt.attach (s, "lfoFilterMod");
    for (auto* k : { &lfoRate, &lfoDelay, &lfoVib, &lfoTrem, &lfoFilt }) addAndMakeVisible (*k);

    selectSource (0);
    selectEnvelope (4);

    setResizable (true, true);
    setResizeLimits (900, 640, 2400, 1600);
    setSize (1100, 780);
    startTimerHz (15);
}

K5AudioProcessorEditor::~K5AudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void K5AudioProcessorEditor::selectSource (int s)
{
    source = s;
    bars.source = s;
    for (int i = 0; i < 2; ++i) sourceTabs[i].setToggleState (i == s, juce::dontSendNotification);
    const juce::String p = s == 0 ? "s1_" : "s2_";
    auto& st = processor.apvts;
    harmModeAtt.reset();
    harmModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (st, p + "harmMode", harmMode);
    slopeAtt.reset();
    slopeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (st, p + "slope24", slope);
    tilt.attach (st, p + "harmTilt");
    level.attach (st, p + "level");
    detune.attach (st, p + "detune");
    cutoff.attach (st, p + "cutoff");
    resonance.attach (st, p + "resonance");
    envAmount.attach (st, p + "filtEnvAmount");
    pitchDepth.attach (st, p + "pitchDepth");
    selectEnvelope (envSel);
    repaint();
}

void K5AudioProcessorEditor::selectEnvelope (int e)
{
    envSel = e;
    for (int i = 0; i < 7; ++i) envTabs[i].setToggleState (i == e, juce::dontSendNotification);
    const juce::String p = source == 0 ? "s1_" : "s2_";
    const juce::String src = source == 0 ? "Source 1" : "Source 2";
    std::vector<std::array<juce::String, 4>> ghosts;
    if (e < 4)
    {
        const auto g = p + "grp" + juce::String (e) + "_";
        envelope.setIds (g + "A", g + "D", g + "S", g + "R", akui::busColour (e),
                         src + " - Harmonic Bus " + juce::String (e + 1));
        for (int o = 0; o < 4; ++o)
            if (o != e)
            {
                const auto og = p + "grp" + juce::String (o) + "_";
                ghosts.push_back ({ og + "A", og + "D", og + "S", og + "R" });
            }
    }
    else if (e == 4) envelope.setIds (p + "ampA", p + "ampD", p + "ampS", p + "ampR", akui::text, src + " - Amplitude (DDA)");
    else if (e == 5) envelope.setIds (p + "filtA", p + "filtD", p + "filtS", p + "filtR", akui::accent, src + " - Filter Envelope (DDF)");
    else envelope.setIds (p + "pitchA", p + "pitchD", p + "pitchS", p + "pitchR", juce::Colour (0xff8fd36a), src + " - Pitch Envelope (DFG)");
    envelope.setGhosts (ghosts);
}

void K5AudioProcessorEditor::setVowel (int v)
{
    auto set = [this] (int b, const char* what, float val)
    {
        if (auto* prm = processor.apvts.getParameter ("dftBand" + juce::String (b) + "_" + what))
            prm->setValueNotifyingHost (prm->convertTo0to1 (val));
    };
    for (int b = 0; b < 11; ++b) set (b, "gain", 0.0f);
    if (v >= 5) return;    // Flat
    static const float f[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                   { 570, 840, 2410 }, { 300, 870, 2240 } };
    const int bands[3] = { 2, 5, 8 };
    const float gains[3] = { 0.8f, 0.65f, 0.45f };
    for (int k = 0; k < 3; ++k)
    {
        set (bands[k], "freq", f[v][k]);
        set (bands[k], "gain", gains[k]);
        set (bands[k], "q", 0.8f);
    }
}

void K5AudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu factory, make, menu;
    const auto names = K5AudioProcessor::factoryNames();
    for (int i = 0; i < names.size(); ++i) factory.addItem (100 + i, names[i], true, i == factoryIndex);
    const auto kinds = K5AudioProcessor::presetKinds();
    for (int i = 0; i < kinds.size(); ++i) make.addItem (200 + i, kinds[i]);
    make.addSeparator();
    make.addItem (299, "Any Kind");
    menu.addSubMenu ("Factory Sounds", factory);
    menu.addSubMenu ("New Sound", make);
    menu.addItem (300, "Init");
    menu.addSeparator();
    menu.addItem (301, "Load Preset...");
    menu.addItem (302, "Save Preset...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this] (int r)
    {
        if (r >= 100 && r < 200) { factoryIndex = r - 100; processor.loadFactory (factoryIndex); }
        else if (r >= 200 && r < 299) processor.rollPreset (r - 200, (juce::uint32) rng.nextInt());
        else if (r == 299) processor.rollPreset (rng.nextInt (K5AudioProcessor::presetKinds().size()), (juce::uint32) rng.nextInt());
        else if (r == 300) processor.presetManager.resetToDefaults();
        else if (r == 301)
        {
            chooser = std::make_shared<juce::FileChooser> ("Load an Additive Keys preset",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.akpreset;*.xml");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (f.existsAsFile()) processor.presetManager.loadPreset (f);
                });
        }
        else if (r == 302)
        {
            chooser = std::make_shared<juce::FileChooser> ("Save the Additive Keys preset",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                    .getChildFile (juce::File::createLegalFileName (processor.soundName) + ".akpreset"), "*.akpreset");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f == juce::File()) return;
                    if (! f.hasFileExtension (".akpreset")) f = f.withFileExtension (".akpreset");
                    processor.presetManager.savePreset (f, f.getFileNameWithoutExtension());
                    processor.setSoundName (f.getFileNameWithoutExtension());
                });
        }
    });
}

void K5AudioProcessorEditor::timerCallback()
{
    if (shownNameVersion != processor.soundNameVersion.load())
    {
        shownNameVersion = processor.soundNameVersion.load();
        repaint (topBar);
    }
    bars.repaint();
}

void K5AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (akui::bg);
    g.setColour (akui::panel);
    for (auto r : { harmArea, envArea, formantArea, lfoArea })
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (akui::panel.darker (0.3f));
    g.fillRect (topBar);

    // title and the sound's name
    auto t = topBar.reduced (14, 0);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.setColour (akui::accent);
    g.drawText ("Additive Keys", t.removeFromLeft (170), juce::Justification::centredLeft);
    auto nameBox = juce::Rectangle<int> (presetButton.getRight() + 8, topBar.getY() + 9,
                                          prevButton.getX() - presetButton.getRight() - 16, topBar.getHeight() - 18);
    g.setColour (akui::bg);
    g.fillRoundedRectangle (nameBox.toFloat(), 5.0f);
    g.setColour (akui::text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (processor.soundName, nameBox.reduced (10, 0), juce::Justification::centredLeft);

    auto heading = [&g] (juce::Rectangle<int> area, const juce::String& s)
    {
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (akui::dim);
        g.drawText (s, area.reduced (12, 6).removeFromTop (16), juce::Justification::left);
    };
    heading (harmArea, "HARMONICS (DHG)  -  draw the bars; paint them onto a bus to give them its envelope");
    heading (envArea, "ENVELOPES  -  drag the points");
    heading (formantArea, "FORMANT SHAPE (DFT)  -  both sources");
    heading (lfoArea, "LFO");
}

void K5AudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (50);
    {
        auto t = topBar.reduced (10, 9);
        t.removeFromLeft (180);
        master.setBounds (t.removeFromRight (56).withTrimmedTop (-8).withTrimmedBottom (-8));
        t.removeFromRight (10);
        sourceTabs[1].setBounds (t.removeFromRight (92));
        t.removeFromRight (4);
        sourceTabs[0].setBounds (t.removeFromRight (92));
        t.removeFromRight (16);
        nextButton.setBounds (t.removeFromRight (30));
        t.removeFromRight (4);
        prevButton.setBounds (t.removeFromRight (30));
        presetButton.setBounds (t.removeFromLeft (90));
    }

    r.reduce (8, 8);
    auto right = r.removeFromRight (juce::jmax (330, r.getWidth() * 36 / 100));
    right.removeFromLeft (8);
    auto left = r;

    harmArea = left.removeFromTop (left.getHeight() * 46 / 100);
    left.removeFromTop (8);
    envArea = left;
    formantArea = right.removeFromTop (right.getHeight() * 58 / 100);
    right.removeFromTop (8);
    lfoArea = right;

    // harmonics
    {
        auto a = harmArea.reduced (10, 8);
        a.removeFromTop (20);
        auto tools = a.removeFromTop (28);
        paintLevel.setBounds (tools.removeFromLeft (96));
        tools.removeFromLeft (6);
        for (auto& b : paintBus) { b.setBounds (tools.removeFromLeft (58)); tools.removeFromLeft (4); }
        resetHarm.setBounds (tools.removeFromRight (64));
        tools.removeFromRight (6);
        harmMode.setBounds (tools.removeFromRight (90));
        a.removeFromTop (6);
        auto knobs = a.removeFromRight (76);
        const int kh = knobs.getHeight() / 3;
        level.setBounds (knobs.removeFromTop (kh));
        detune.setBounds (knobs.removeFromTop (kh));
        tilt.setBounds (knobs);
        a.removeFromRight (6);
        bars.setBounds (a);
    }
    // envelopes
    {
        auto a = envArea.reduced (10, 8);
        a.removeFromTop (20);
        auto tabs = a.removeFromTop (28);
        const int tw = tabs.getWidth() / 7;
        for (auto& b : envTabs) { b.setBounds (tabs.removeFromLeft (tw).reduced (2, 0)); }
        a.removeFromTop (6);
        auto knobs = a.removeFromBottom (74);
        const int kw = knobs.getWidth() / 5;
        cutoff.setBounds (knobs.removeFromLeft (kw));
        resonance.setBounds (knobs.removeFromLeft (kw));
        envAmount.setBounds (knobs.removeFromLeft (kw));
        slope.setBounds (knobs.removeFromLeft (kw).withSizeKeepingCentre (kw - 12, 26));
        pitchDepth.setBounds (knobs);
        a.removeFromBottom (6);
        envelope.setBounds (a);
    }
    // formants
    {
        auto a = formantArea.reduced (10, 8);
        a.removeFromTop (20);
        auto vrow = a.removeFromTop (28);
        const int vw = vrow.getWidth() / 6;
        for (auto& b : vowels) b.setBounds (vrow.removeFromLeft (vw).reduced (2, 0));
        a.removeFromTop (6);
        bandQ.setBounds (a.removeFromRight (74).withSizeKeepingCentre (74, 90));
        a.removeFromRight (4);
        formants.setBounds (a);
    }
    // LFO
    {
        auto a = lfoArea.reduced (10, 8);
        a.removeFromTop (20);
        lfoShape.setBounds (a.removeFromTop (26).removeFromLeft (130));
        a.removeFromTop (6);
        const int kw = a.getWidth() / 5;
        for (auto* k : { &lfoRate, &lfoDelay, &lfoVib, &lfoTrem, &lfoFilt })
            k->setBounds (a.removeFromLeft (kw).withSizeKeepingCentre (kw, juce::jmin (a.getHeight(), 90)));
    }
}
