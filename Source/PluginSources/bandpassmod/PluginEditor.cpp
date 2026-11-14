#include "PluginEditor.h"

namespace
{
    const juce::Colour face { 0xff3b3e41 }, faceDark { 0xff2e3133 }, teal { 0xff2fd1c0 }, label { 0xffe9eef0 }, dimText { 0xffa5b0b4 };
    const char* kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr int kW = 990, kH = 700;
}

//==============================================================================
void BandpassModulatorAudioProcessorEditor::LNF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                                                  float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const float ringR = size * 0.47f, knobR = size * 0.32f;
    const float angle = start + pos * (end - start);

    // the ring of dots: lit up to the value, the rest faint
    constexpr int dots = 31;
    for (int i = 0; i < dots; ++i)
    {
        const float t = (float) i / (float) (dots - 1);
        const float a = start + t * (end - start);
        const bool lit = t <= pos + 0.001f;
        const auto pt = c.getPointOnCircumference (ringR, a);
        const float d = (i % 5 == 0) ? 3.6f : 2.6f;
        g.setColour (lit ? teal : teal.withAlpha (s.isMouseOverOrDragging() ? 0.22f : 0.12f));
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (pt));
    }

    // the knob: dark, softly lit from above
    auto kr = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (c);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (kr.translated (0.0f, 2.5f).expanded (1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4f53), c.x, kr.getY(), juce::Colour (0xff1f2224), c.x, kr.getBottom(), false));
    g.fillEllipse (kr);
    g.setColour (juce::Colour (0xff5b6166).withAlpha (0.6f));
    g.drawEllipse (kr.reduced (0.5f), 1.0f);
    // its pointer: a small teal dot
    g.setColour (teal);
    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (c.getPointOnCircumference (knobR * 0.68f, angle)));
}

void BandpassModulatorAudioProcessorEditor::LNF::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    const bool black = b.getProperties().getWithDefault ("black", false);
    const bool held = b.getProperties().getWithDefault ("held", false);
    g.setColour (on ? teal.withAlpha (black ? 0.75f : 0.9f) : (black ? faceDark.darker (0.4f) : faceDark));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (held ? juce::Colours::white : teal.withAlpha (over ? 0.8f : 0.45f));
    g.drawRoundedRectangle (r, 4.0f, held ? 2.0f : 1.0f);
    g.setColour (on ? faceDark : label);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
BandpassModulatorAudioProcessorEditor::Knob& BandpassModulatorAudioProcessorEditor::knob (const juce::String& id)
{
    return *knobs[id];
}

BandpassModulatorAudioProcessorEditor::BandpassModulatorAudioProcessorEditor (BandpassModulatorAudioProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    const std::pair<const char*, const char*> list[] = {
        { "volDry", "Vol Dry" }, { "volWet", "Vol Wet" }, { "lowLimit", "Low Limit" }, { "highLimit", "High Limit" },
        { "fine", "Fine" }, { "speed", "Speed" }, { "modType", "Mod Type" }, { "bandwidth", "Bandwidth" },
        { "strength", "Strength" }, { "dist", "Dist" }, { "delaySpeed", "Delay Speed" }, { "delayVol", "Delay Vol" },
        { "autopan", "Autopan" }, { "skewSpeed", "Skew Speed" }, { "skewAmt", "Skew Amt" } };
    for (const auto& [id, name] : list)
    {
        auto k = std::make_unique<Knob>();
        k->name = name;
        k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->slider.setPopupDisplayEnabled (true, true, this);
        k->slider.setTooltip (name);
        addAndMakeVisible (k->slider);
        k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
        knobs[id] = std::move (k);
    }

    addAndMakeVisible (noteLock);
    addAndMakeVisible (midiLock);
    lockAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "noteLock", noteLock);
    midiAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "midiLock", midiLock);
    midiLock.setTooltip ("Lock to the notes held on Midi In (while any are held)");
    for (int n = 0; n < 12; ++n)
    {
        noteButtons[n].setButtonText (kKeyNames[n]);
        noteButtons[n].getProperties().set ("black", juce::String (kKeyNames[n]).contains ("#"));
        addAndMakeVisible (noteButtons[n]);
        noteAtt[n] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "note" + juce::String (n), noteButtons[n]);
    }
    seed.setSliderStyle (juce::Slider::IncDecButtons);
    seed.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
    seed.setColour (juce::Slider::textBoxTextColourId, label);
    seed.setColour (juce::Slider::textBoxOutlineColourId, teal.withAlpha (0.4f));
    seed.setTooltip ("Seed: which of the generator's fixed random sequences the band follows");
    addAndMakeVisible (seed);
    seedAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "seed", seed);

    setSize (kW, kH);
    startTimerHz (30);
}

BandpassModulatorAudioProcessorEditor::~BandpassModulatorAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> BandpassModulatorAudioProcessorEditor::graphArea() const
{
    return { 40, 486, kW - 80, 120 };
}

void BandpassModulatorAudioProcessorEditor::timerCallback()
{
    colourPhase += 0.02f;
    const int held = proc.heldPitchClassMask.load();
    for (int n = 0; n < 12; ++n)
    {
        const bool h = (held & (1 << n)) != 0;
        if ((bool) noteButtons[n].getProperties().getWithDefault ("held", false) != h)
        {
            noteButtons[n].getProperties().set ("held", h);
            noteButtons[n].repaint();
        }
    }
    repaint (graphArea().expanded (4));
}

void BandpassModulatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (face.brighter (0.06f), 0, 0, face.darker (0.12f), 0, (float) getHeight(), false));
    g.fillAll();

    // title
    g.setColour (label);
    g.setFont (juce::Font (juce::FontOptions (44.0f)));
    g.drawText ("AutoMorph Bandpass Modulator", 40, 18, 700, 56, juce::Justification::left);
    g.setFont (juce::Font (juce::FontOptions (16.0f)));
    g.drawText ("by aquanode", 560, 62, 160, 20, juce::Justification::left);

    auto section = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (label);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText (title, r.withHeight (18), juce::Justification::centred);
        g.setColour (teal.withAlpha (0.75f));
        g.fillRect (r.getX(), r.getY() + 22, r.getWidth(), 2);
    };
    section ({ 40, 118, kW - 80, 24 }, "Bandpass Settings (Low/High interchangeable, Default Mod: Random)");
    section ({ 40, 318, 370, 24 }, "FX");
    section ({ 570, 318, 380, 24 }, "Modulation");

    // knob names under each knob
    g.setFont (juce::Font (juce::FontOptions (19.0f)));
    for (const auto& [id, k] : knobs)
    {
        auto b = k->slider.getBounds();
        g.setColour (label);
        g.drawText (k->name, b.getX() - 30, b.getBottom() - 2, b.getWidth() + 60, 24, juce::Justification::centred);
        // the value of a stepped knob is worth seeing all the time
        if (id == "speed" || id == "modType" || id == "delaySpeed")
        {
            g.setColour (teal);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            if (auto* prm = proc.apvts.getParameter (id))
                g.drawText (prm->getCurrentValueAsText(), b.getX() - 30, b.getBottom() + 19, b.getWidth() + 60, 15, juce::Justification::centred);
            g.setFont (juce::Font (juce::FontOptions (19.0f)));
        }
    }

    // where the band is: a frequency strip with the range shaded and the band's bell
    const auto ga = graphArea().toFloat();
    g.setColour (faceDark);
    g.fillRoundedRectangle (ga, 6.0f);
    auto xOf = [ga] (float f) { return ga.getX() + ga.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
    float lo = proc.apvts.getRawParameterValue ("lowLimit")->load(), hi = proc.apvts.getRawParameterValue ("highLimit")->load();
    if (lo > hi) std::swap (lo, hi);
    g.setColour (teal.withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float> (xOf (lo), ga.getY(), juce::jmax (2.0f, xOf (hi) - xOf (lo)), ga.getHeight()));
    g.setColour (dimText.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.drawVerticalLine ((int) xOf (f), ga.getY() + 14, ga.getBottom());
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), (int) xOf (f) - 16, (int) ga.getY() + 2, 32, 12, juce::Justification::centred);
    }
    // the notes it may lock to
    const bool lockOn = proc.apvts.getRawParameterValue ("noteLock")->load() > 0.5f;
    if (lockOn)
        for (int m = 12; m < 136; ++m)
            if (proc.apvts.getRawParameterValue ("note" + juce::String (m % 12))->load() > 0.5f)
            {
                const float f = 440.0f * std::exp2 ((float) (m - 69) / 12.0f);
                if (f < lo || f > hi) continue;
                g.setColour (teal.withAlpha (0.35f));
                g.fillRect (xOf (f) - 0.5f, ga.getBottom() - 10.0f, 1.5f, 8.0f);
            }

    const float fc = proc.currentFreq.load();
    const float bw = proc.apvts.getRawParameterValue ("bandwidth")->load();
    juce::Path bell;
    for (int px = 0; px <= (int) ga.getWidth(); px += 2)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) px / ga.getWidth());
        const float d = std::log2 (f / fc) / juce::jmax (0.02f, bw);
        const float y = ga.getBottom() - 6.0f - (ga.getHeight() - 26.0f) / (1.0f + 4.0f * d * d);
        if (px == 0) bell.startNewSubPath (ga.getX(), y); else bell.lineTo (ga.getX() + (float) px, y);
    }
    // the colour cycles between white and cyan, as on the original
    const float cyc = 0.5f + 0.5f * std::sin (colourPhase);
    g.setColour (juce::Colours::white.interpolatedWith (teal, cyc));
    g.strokePath (bell, juce::PathStrokeType (2.0f));
    // readout: frequency, nearest note, pan
    const int midiNote = juce::roundToInt (69.0f + 12.0f * std::log2 (fc / 440.0f));
    g.setColour (label);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    const float pan = proc.currentPan.load();
    g.drawText (juce::String (juce::roundToInt (fc)) + " Hz  " + kKeyNames[((midiNote % 12) + 12) % 12] + juce::String (midiNote / 12 - 1)
                    + "   pan " + (std::abs (pan) < 0.02f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (pan) * 100.0f)) + (pan < 0 ? " L" : " R")),
                ga.reduced (10, 4).toNearestInt(), juce::Justification::topRight);

    g.setColour (dimText);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("Seed", kW - 202, 640, 44, 26, juce::Justification::centredRight);
}

void BandpassModulatorAudioProcessorEditor::resized()
{
    const int big = 96;
    knob ("volDry").slider.setBounds (715, 12, 76, 76);
    knob ("volWet").slider.setBounds (835, 12, 76, 76);
    const char* row1[] = { "lowLimit", "highLimit", "fine", "speed", "modType", "bandwidth", "strength" };
    const int spacing = (kW - 80) / 7;
    for (int i = 0; i < 7; ++i)
        knob (row1[i]).slider.setBounds (40 + i * spacing + (spacing - big) / 2, 158, big, big);
    const char* fx[] = { "dist", "delaySpeed", "delayVol" };
    for (int i = 0; i < 3; ++i)
        knob (fx[i]).slider.setBounds (40 + i * 123 + (123 - big) / 2, 356, big, big);
    const char* mod[] = { "autopan", "skewSpeed", "skewAmt" };
    for (int i = 0; i < 3; ++i)
        knob (mod[i]).slider.setBounds (570 + i * 127 + (127 - big) / 2, 356, big, big);

    noteLock.setBounds (40, 640, 104, 28);
    const int nw = 42;
    for (int n = 0; n < 12; ++n)
        noteButtons[n].setBounds (152 + n * (nw + 4), 640, nw, 28);
    midiLock.setBounds (152 + 12 * (nw + 4) + 6, 640, 72, 28);
    seed.setBounds (kW - 150, 640, 110, 26);
}
