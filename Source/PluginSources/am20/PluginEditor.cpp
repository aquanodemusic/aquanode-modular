#include "PluginEditor.h"

using namespace am20;

// ---------------------------------------------------------------- look
AM20Look::AM20Look()
{
    setColour (juce::ComboBox::backgroundColourId, panel.brighter (0.08f));
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, panelEdge);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff25282c));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, panel.brighter (0.1f));
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, panelEdge);
    setColour (juce::Slider::thumbColourId, text);
}

juce::Font AM20Look::font (float h, bool bold)
{
    return juce::Font (juce::FontOptions (h).withStyle (bold ? "Bold" : "Regular"));
}

void AM20Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const float rad = d * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const auto acc = s.findColour (juce::Slider::trackColourId);
    // track
    juce::Path track; track.addCentredArc (c.x, c.y, rad - 2.0f, rad - 2.0f, 0.0f, a0, a1, true);
    g.setColour (panelEdge.brighter (0.15f)); g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc (from the middle for bipolar knobs)
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    juce::Path arc; arc.addCentredArc (c.x, c.y, rad - 2.0f, rad - 2.0f, 0.0f, bipolar ? (a0 + a1) * 0.5f : a0, ang, true);
    g.setColour (acc); g.strokePath (arc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // cap
    const float cr = rad * 0.68f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4f56), c.x, c.y - cr, juce::Colour (0xff202327), c.x, c.y + cr, false));
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 1.0f);
    // pointer
    juce::Path p; p.addRoundedRectangle (-1.6f, -cr, 3.2f, cr * 0.62f, 1.5f);
    g.setColour (text); g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void AM20Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& b)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (b.findColour (juce::ComboBox::backgroundColourId)); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (b.findColour (juce::ComboBox::outlineColourId)); g.drawRoundedRectangle (r, 5.0f, 1.0f);
    juce::Path a; const float ax = (float) w - 13.0f, ay = (float) h * 0.5f;
    a.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (accent); g.fillPath (a);
}

void AM20Look::positionComboBoxText (juce::ComboBox& b, juce::Label& l)
{
    l.setBounds (4, 1, b.getWidth() - 22, b.getHeight() - 2);
    l.setFont (font (14.0f));
    l.setJustificationType (juce::Justification::centred);
}

void AM20Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? accent : panel.brighter (0.12f);
    if (over) c = c.brighter (0.08f);
    if (down) c = c.darker (0.15f);
    g.setColour (c); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (panelEdge.brighter (0.2f)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

// ---------------------------------------------------------------- knob
AmKnob::AmKnob (AM20Processor& p, int prm, const juce::String& cap) : param (prm), caption (cap), proc (p)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setDoubleClickReturnValue (true, defaultValue (prm));
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    const bool bipolar = prm == VCO2Tune || prm == FineTune || prm == Pan || (prm >= Ext1PW && prm <= Ext2Amp);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::SliderParameterAttachment> (*p.apvts.getParameter (AM20Processor::pid (prm)), slider);
    slider.onValueChange = [this] { repaint(); };
    slider.setTooltip (paramName (prm));
}

void AmKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (17); r.removeFromBottom (17);
    const int d = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

void AmKnob::paint (juce::Graphics& g)
{
    g.setColour (AM20Look::text); g.setFont (AM20Look::font (13.0f, true));
    g.drawFittedText (caption, getLocalBounds().removeFromTop (16), juce::Justification::centred, 1);
    g.setColour (AM20Look::accent2); g.setFont (AM20Look::font (13.0f));
    g.drawFittedText (juce::String (displayText (param, (float) slider.getValue())), getLocalBounds().removeFromBottom (16), juce::Justification::centred, 1);
}

// ---------------------------------------------------------------- choice
AmChoice::AmChoice (AM20Processor& p, int prm, const juce::String& cap) : param (prm), caption (cap)
{
    const int n = numSteps (prm);
    for (int i = 0; i < n; ++i) box.addItem (juce::String (displayText (prm, stepValue (prm, i))), i + 1);
    addAndMakeVisible (box);
    auto* rp = p.apvts.getParameter (AM20Processor::pid (prm));
    att = std::make_unique<juce::ParameterAttachment> (*rp, [this, n] (float v)
    {
        int best = 0; float bd = 2.0f;
        for (int i = 0; i < n; ++i) { const float d = std::abs (stepValue (param, i) - v); if (d < bd) { bd = d; best = i; } }
        box.setSelectedItemIndex (best, juce::dontSendNotification);
    });
    box.onChange = [this] { const int i = box.getSelectedItemIndex(); if (i >= 0) att->setValueAsCompleteGesture (stepValue (param, i)); };
    att->sendInitialUpdate();
    box.setTooltip (paramName (prm));
}

void AmChoice::resized()
{
    auto r = getLocalBounds(); r.removeFromTop (18);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 6, juce::jmin (28, r.getHeight())));
}

void AmChoice::paint (juce::Graphics& g)
{
    g.setColour (AM20Look::text); g.setFont (AM20Look::font (13.0f, true));
    g.drawFittedText (caption, getLocalBounds().removeFromTop (16), juce::Justification::centred, 1);
}

// ---------------------------------------------------------------- section
void AmSection::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (AM20Look::panel); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (AM20Look::panelEdge); g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (colour); g.fillRoundedRectangle (r.getX() + 10, r.getY() + 8, 4.0f, 14.0f, 2.0f);
    g.setColour (AM20Look::text); g.setFont (AM20Look::font (15.0f, true));
    g.drawText (title, (int) r.getX() + 20, (int) r.getY() + 5, (int) r.getWidth() - 30, 20, juce::Justification::centredLeft);
}

void AmSection::resized()
{
    int rows = 0; for (auto& i : items) rows = juce::jmax (rows, i.row + 1);
    if (rows == 0) return;
    auto area = getLocalBounds().reduced (8, 6); area.removeFromTop (22);
    const int rh = area.getHeight() / rows;
    for (int row = 0; row < rows; ++row)
    {
        std::vector<juce::Component*> cs; for (auto& i : items) if (i.row == row) cs.push_back (i.c);
        auto ra = area.removeFromTop (rh);
        if (cs.empty()) continue;
        const int cw = ra.getWidth() / (int) cs.size();
        for (auto* c : cs) c->setBounds (ra.removeFromLeft (cw).reduced (2, 2));
    }
}

// ---------------------------------------------------------------- cable list
void CableList::paint (juce::Graphics& g)
{
    const auto& cs = proc.getCables();
    g.setFont (AM20Look::font (15.0f));
    if (cs.empty())
    {
        g.setColour (AM20Look::dim);
        g.drawFittedText ("No cables: every connection is the normal one, as on the panel.", getLocalBounds().reduced (8), juce::Justification::topLeft, 3);
        return;
    }
    for (int i = 0; i < (int) cs.size(); ++i)
    {
        auto r = juce::Rectangle<int> (0, i * rowH, getWidth(), rowH - 3);
        g.setColour (AM20Look::panel.brighter (0.06f)); g.fillRoundedRectangle (r.toFloat(), 5.0f);
        static const juce::uint32 cols[4] = { 0xffff8a3d, 0xff53c7d8, 0xffe5d24c, 0xffd06bd8 };
        g.setColour (juce::Colour (cols[i % 4])); g.fillRoundedRectangle ((float) r.getX() + 6, (float) r.getCentreY() - 5, 10, 10, 5);
        g.setColour (AM20Look::text);
        g.drawText (juce::String (jackName (cs[(size_t) i].first)) + "   ->   " + jackName (cs[(size_t) i].second),
                    r.withTrimmedLeft (24).withTrimmedRight (40), juce::Justification::centredLeft);
        g.setColour (AM20Look::dim);
        g.drawText ("x", r.removeFromRight (34), juce::Justification::centred);
    }
}

void CableList::mouseUp (const juce::MouseEvent& e)
{
    const int i = e.y / rowH;
    if (i >= 0 && i < (int) proc.getCables().size() && e.x > getWidth() - 40) proc.removeCable (i);
}

// ---------------------------------------------------------------- file helpers
namespace
{
    juce::String chosenName (const juce::URL& url)
    {
        if (url.isLocalFile()) return url.getLocalFile().getFileName();
        auto n = juce::URL::removeEscapeChars (url.toString (false));
        return n.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf (":", false, false);
    }
    // the picked file's bytes: the file itself on the desktop, the app's copy of the document on Android,
    // the document's stream as the fallback
    bool readChosen (const juce::FileChooser& fc, juce::MemoryBlock& mb, juce::String& name, juce::File& file)
    {
        const auto url = fc.getURLResult();
        const auto res = fc.getResult();
        if (url.isEmpty() && res == juce::File()) return false;
        mb.reset();
        if (res.existsAsFile() && res.loadFileAsData (mb) && mb.getSize() > 0) { file = res; name = res.getFileName(); return true; }
        mb.reset(); file = juce::File();
        if (url.isEmpty()) return false;
        name = chosenName (url);
        std::unique_ptr<juce::InputStream> in (url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)));
        if (in == nullptr) return false;
        in->readIntoMemoryBlock (mb);
        return mb.getSize() > 0;
    }
    bool writeChosen (const juce::FileChooser& fc, const juce::MemoryBlock& mb, const juce::String& ext, juce::File& file)
    {
        const auto url = fc.getURLResult();
        if (auto res = fc.getResult(); res != juce::File())
        {
            if (! res.hasFileExtension (ext)) res = res.withFileExtension (ext);
            if (! res.replaceWithData (mb.getData(), mb.getSize())) return false;
           #if JUCE_ANDROID
            file = juce::File();
           #else
            file = res;
           #endif
            return true;
        }
        if (url.isEmpty()) return false;
        file = juce::File();
       #if AQP_HOSTED
        return aqp::files::writeDocument (url, mb.getData(), mb.getSize()).isEmpty();
       #else
        std::unique_ptr<juce::OutputStream> out (url.createOutputStream());
        if (out == nullptr) return false;
        const bool ok = out->write (mb.getData(), mb.getSize());
        out->flush();
        return ok;
       #endif
    }
    juce::File startDir (const juce::File& last)
    {
        if (last.existsAsFile()) return last.getParentDirectory();
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }
    void warn (const juce::String& t) { juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "AM-20", t); }
}

// ---------------------------------------------------------------- editor
AmSection* AM20Editor::addSection (juce::Component& page, const juce::String& title, juce::Colour c)
{
    sections.push_back (std::make_unique<AmSection> (title, c));
    page.addAndMakeVisible (*sections.back());
    return sections.back().get();
}

AmKnob* AM20Editor::knob (AmSection* s, int param, const juce::String& caption, int row)
{
    auto k = std::make_unique<AmKnob> (proc, param, caption);
    auto* raw = k.get(); s->add (raw, row); owned.push_back (std::move (k));
    return raw;
}

static AmKnob* tip (AmKnob* k, const juce::String& t) { k->slider.setTooltip (t); return k; }

AmChoice* AM20Editor::choice (AmSection* s, int param, const juce::String& caption, int row)
{
    auto k = std::make_unique<AmChoice> (proc, param, caption);
    auto* raw = k.get(); s->add (raw, row); owned.push_back (std::move (k));
    return raw;
}

AM20Editor::AM20Editor (AM20Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.setSize (W, H);

    // ---- top bar ----
    for (auto* b : { &prevBtn, &nextBtn, &loadBtn, &saveBtn, &initBtn, &panelTab, &patchTab, &hqBtn }) content.addAndMakeVisible (*b);
    hqBtn.setClickingTogglesState (true);
    hqBtn.setToggleState (proc.highQuality.load(), juce::dontSendNotification);
    hqBtn.setTooltip ("High quality: the voices run at twice the sample rate, as measured. Off is lighter on the CPU.");
    hqBtn.onClick = [this] { proc.highQuality = hqBtn.getToggleState(); };
    content.addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected ("Init");
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram (i); };
    prevBtn.onClick = [this] { const int n = (int) proc.bank.size(); if (n) proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n); refreshPresets(); };
    nextBtn.onClick = [this] { const int n = (int) proc.bank.size(); if (n) proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % n); refreshPresets(); };
    loadBtn.setTooltip ("Load an AM-20 preset or bank (.am20), or the original's programs and banks (.fxp, .fxb)");
    loadBtn.onClick = [this] { loadFile(); };
    saveBtn.setTooltip ("Save as AM-20 (.am20) or in the original's formats (.fxp, .fxb)");
    saveBtn.onClick = [this] { saveMenu(); };
    initBtn.setTooltip ("Start from the default sound");
    content.addChildComponent (fxBtn);
    fxBtn.onClick = [this]
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "AM-20",
            "This program came from the original with effects: " + proc.effectsKept() + ".\n\n"
            "AM-20 has no effects section and plays it without them, but keeps them: saved as .fxp or .fxb "
            "it goes back to the original with its effects. Saving as .am20 (or Init) leaves them out.");
    };
    initBtn.onClick = [this] { Preset init; proc.applyPreset (init); refreshPresets(); };
    panelTab.setClickingTogglesState (false); patchTab.setClickingTogglesState (false);
    panelTab.onClick = [this] { showPage (false); };
    patchTab.onClick = [this] { showPage (true); };

    // ---- the front panel ----
    content.addAndMakeVisible (panelPage);
    const juce::Colour cVco (0xffff8a3d), cFlt (0xff53c7d8), cMod (0xffe5d24c), cEg (0xff7fd36b), cOut (0xffd06bd8);
    auto* s = addSection (panelPage, "VCO 1", cVco);
    choice (s, VCO1Wave, "Wave"); choice (s, VCO1Scale, "Scale"); knob (s, VCO1PW, "Pulse Width");
    s = addSection (panelPage, "VCO 2", cVco);
    choice (s, VCO2Wave, "Wave"); choice (s, VCO2Scale, "Scale"); knob (s, VCO2Tune, "Pitch");
    s = addSection (panelPage, "VCO MIXER", cVco);
    knob (s, VCO1Level, "VCO 1"); knob (s, VCO2Level, "VCO 2");
    s = addSection (panelPage, "PITCH", cMod);
    knob (s, Porta, "Portamento"); knob (s, FineTune, "Fine Tune"); tip (knob (s, PitchMG, "LFO Amount"), "How far the LFO bends the pitch (MG / T.EXT on the original; a cable into TOTAL takes the LFO's place)");
    tip (knob (s, PitchEG1, "Env 1 Amount"), "How far Envelope 1 bends the pitch (EG1 / EXT on the original; a cable into FREQ takes the envelope's place)");
    s = addSection (panelPage, "HIGH PASS FILTER", cFlt);
    knob (s, HPFCut, "Cutoff"); tip (knob (s, HPFPeak, "Resonance"), "Resonance (PEAK on the original); far up the filter sings on its own");
    tip (knob (s, HPFMG, "LFO Amount"), "How far the LFO moves the cutoff (MG / T.EXT on the original; a cable into TOTAL takes the LFO's place)");
    tip (knob (s, HPFEG2, "Env 2 Amount"), "How far Envelope 2 moves the cutoff (EG2 / EXT on the original; a cable into HPF CUTOFF takes the envelope's place)");
    s = addSection (panelPage, "LOW PASS FILTER", cFlt);
    knob (s, LPFCut, "Cutoff"); tip (knob (s, LPFPeak, "Resonance"), "Resonance (PEAK on the original); far up the filter sings on its own");
    tip (knob (s, LPFMG, "LFO Amount"), "How far the LFO moves the cutoff (MG / T.EXT on the original; a cable into TOTAL takes the LFO's place)");
    tip (knob (s, LPFEG2, "Env 2 Amount"), "How far Envelope 2 moves the cutoff (EG2 / EXT on the original; a cable into LPF CUTOFF takes the envelope's place)");
    s = addSection (panelPage, "LFO  (MG)", cMod);
    knob (s, MGWave, "Wave"); knob (s, MGFreq, "Rate");
    s = addSection (panelPage, "OUTPUT", cOut);
    knob (s, Volume, "Volume"); knob (s, Pan, "Pan");
    s = addSection (panelPage, "ENVELOPE 1  (EG1)", cEg);
    knob (s, EG1Delay, "Delay"); knob (s, EG1Attack, "Attack"); knob (s, EG1Release, "Release");
    s = addSection (panelPage, "ENVELOPE 2  (EG2)", cEg);
    knob (s, EG2Hold, "Hold"); knob (s, EG2Attack, "Attack"); knob (s, EG2Decay, "Decay"); knob (s, EG2Sustain, "Sustain"); knob (s, EG2Release, "Release");
    s = addSection (panelPage, "VOICES", cOut);
    choice (s, Assign, "Assign"); choice (s, Trigger, "Trigger"); choice (s, PolyVoices, "Poly"); choice (s, UnisonVoices, "Unison");
    knob (s, UnisonDetune, "Detune", 1); knob (s, UnisonSpread, "Spread", 1); knob (s, Analog, "Analog", 1); knob (s, BendRange, "Bend", 1); knob (s, Transpose, "Transpose", 1);

    // ---- the patch panel page ----
    content.addChildComponent (patchPage);
    auto* pp = addSection (patchPage, "PATCH PANEL", juce::Colour (0xffff8a3d));
    juce::ignoreUnused (pp);
    for (auto* c : std::initializer_list<juce::Component*> { &fromBox, &toBox, &connectBtn, &clearBtn, &cableView, &patchHelp }) patchPage.addAndMakeVisible (*c);
    fromBox.setTextWhenNothingSelected ("From (an output)");
    toBox.setTextWhenNothingSelected ("To (an input)");
    {
        // jacks in the order of the panel's sections
        static const int outs[] = { jMgTri, jMgRect, jEg1Out, jEg1Rev, jEg2Rev, jKbdCv, jKbdTrig, jWheel, jMsw, jShOut, jMvcaOut,
                                    jPink, jWhite, jSigOut, jPhones, jEspAmp, jEspBpf, jEspFv, jEspEnv, jEspTrig };
        static const int ins[] = { jTotal, jFreq, jVco12Cv, jVco2Cv, jExtSig, jHpCut, jLpCut, jInitGain, jTrigIn, jEg1Trig,
                                   jShIn, jShClock, jMvcaIn, jMvcaCtl, jEspIn };
        for (int o : outs) fromBox.addItem (jackName (o), o + 1);
        for (int i : ins) toBox.addItem (jackName (i), i + 1);
    }
    connectBtn.onClick = [this]
    {
        const int a = fromBox.getSelectedId() - 1, b = toBox.getSelectedId() - 1;
        if (a < 0 || b < 0) { warn ("Pick an output and an input first."); return; }
        juce::String why; if (! proc.addCable (a, b, why)) warn (why);
    };
    clearBtn.onClick = [this] { proc.clearCables(); };
    cableView.setViewedComponent (&cableList, false);
    cableView.setScrollBarsShown (true, false);
    patchHelp.setJustificationType (juce::Justification::topLeft);
    patchHelp.setFont (AM20Look::font (13.5f));
    patchHelp.setColour (juce::Label::textColourId, AM20Look::dim);
    patchHelp.setText ("Every voice has its own patch panel, so patches stay polyphonic. Patching an input replaces its normal "
                       "connection: TOTAL replaces the LFO on all three LFO Amount knobs, FREQ replaces Envelope 1 (EG1) on the pitch, the HPF / LPF "
                       "CUTOFF jacks replace Envelope 2 (EG2) on the filters. MG is the original's name for the LFO. Trigger jacks work like the original's: they fire while pulled low. "
                       "The WHEEL jack sends -5..+5 V from the wheel below (CC 1).", juce::dontSendNotification);

    s = addSection (patchPage, "EXTERNAL MODULATION", cMod);
    choice (s, ExtSrc1, "Source 1"); knob (s, Ext1PW, "VCO1 PW"); knob (s, Ext1VCO2, "VCO2 Pitch"); knob (s, Ext1HPF, "HPF"); knob (s, Ext1LPF, "LPF"); knob (s, Ext1Amp, "Amp");
    choice (s, ExtSrc2, "Source 2", 1); knob (s, Ext2PW, "VCO1 PW", 1); knob (s, Ext2VCO2, "VCO2 Pitch", 1); knob (s, Ext2HPF, "HPF", 1); knob (s, Ext2LPF, "LPF", 1); knob (s, Ext2Amp, "Amp", 1);
    s = addSection (patchPage, "EXTERNAL SIGNAL PROCESSOR", cFlt);
    knob (s, ESPLevel, "Signal Level"); knob (s, ESPLowCut, "Low Cut"); knob (s, ESPHighCut, "High Cut"); tip (knob (s, ESPCVAdjust, "CV Tune"), "How much F-V OUT moves per octave of the pitch the signal processor hears: set it so a patch into a VCO follows the input in tune (CV ADJUST on the original)"); knob (s, ESPThreshold, "Threshold");
    s = addSection (patchPage, "LFO SYNC", cMod);
    choice (s, MGTempoSync, "Tempo Sync"); choice (s, MGKeySync, "Key Sync"); choice (s, MGBaseNote, "Base Note"); choice (s, MGTimes, "Times");

    // ---- performance strip ----
    content.addAndMakeVisible (wheel);
    wheel.setSliderStyle (juce::Slider::LinearHorizontal);
    wheel.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    wheel.setRange (0.0, 1.0); wheel.setValue (0.0, juce::dontSendNotification);
    wheel.setTooltip ("The control wheel: CC 1, and -5..+5 V at the WHEEL jack");
    wheel.onDragStart = [this] { proc.guiWheel = (float) wheel.getValue(); };
    wheel.onValueChange = [this] { proc.guiWheel = (float) wheel.getValue(); };
    wheel.onDragEnd = [this] { proc.guiWheel = (float) wheel.getValue(); };
    content.addAndMakeVisible (wheelLabel);
    wheelLabel.setText ("WHEEL", juce::dontSendNotification);
    wheelLabel.setFont (AM20Look::font (14.0f, true));
    wheelLabel.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (mswBtn);
    mswBtn.setTooltip ("The momentary switch (also the sustain pedal, CC 64)");
    mswBtn.onStateChange = [this] { proc.momentary = mswBtn.isDown(); };

    proc.onChange = [this] { refreshPresets(); refreshCables(); };
    refreshPresets(); refreshCables();
    showPage (false);

    setResizable (true, true);
    setResizeLimits (W / 2, H / 2, W * 2, H * 2);
    getConstrainer()->setFixedAspectRatio ((double) W / (double) H);
    setSize ((int) (W * proc.uiScale), (int) (H * proc.uiScale));
    startTimerHz (10);
}

AM20Editor::~AM20Editor()
{
    proc.onChange = nullptr;
    setLookAndFeel (nullptr);
}

void AM20Editor::paint (juce::Graphics& g)
{
    g.fillAll (AM20Look::bg);
}

void AM20Editor::resized()
{
    const float sc = juce::jmin ((float) getWidth() / (float) W, (float) getHeight() / (float) H);
    content.setTransform (juce::AffineTransform::scale (sc));
    proc.uiScale = sc;

    // top bar (content coordinates)
    int x = 12;
    panelTab.setBounds (x, 10, 96, 30); x += 100;
    patchTab.setBounds (x, 10, 120, 30); x += 136;
    prevBtn.setBounds (x, 10, 32, 30); x += 36;
    presetBox.setBounds (x, 10, 300, 30); x += 304;
    nextBtn.setBounds (x, 10, 32, 30); x += 44;
    loadBtn.setBounds (x, 10, 80, 30); x += 84;
    saveBtn.setBounds (x, 10, 80, 30); x += 84;
    initBtn.setBounds (x, 10, 70, 30); x += 78;
    fxBtn.setBounds (x, 10, 96, 30);
    hqBtn.setBounds (W - 70, 10, 58, 30);

    const juce::Rectangle<int> pageArea (8, 50, W - 16, H - 50 - 64);
    panelPage.setBounds (pageArea); patchPage.setBounds (pageArea);

    // front panel: three rows
    auto at = [this] (int i) { return sections[(size_t) i].get(); };
    const int pw = pageArea.getWidth(), gap = 8;
    const int r1 = 186, r2 = 186, r3 = pageArea.getHeight() - r1 - r2 - 2 * gap;
    auto row = [&] (int y, int h, std::initializer_list<std::pair<int, int>> parts)   // section index, relative width
    {
        int tot = 0; for (auto& p : parts) tot += p.second;
        int xx = 0; const int avail = pw - gap * ((int) parts.size() - 1);
        int n = 0;
        for (auto& p : parts)
        {
            const int w = ++n == (int) parts.size() ? pw - xx : avail * p.second / tot;
            at (p.first)->setBounds (xx, y, w, h); xx += w + gap;
        }
    };
    row (0, r1, { { 0, 3 }, { 1, 3 }, { 2, 2 }, { 3, 4 } });
    row (r1 + gap, r2, { { 4, 4 }, { 5, 4 }, { 6, 2 }, { 7, 2 } });
    row (r1 + r2 + 2 * gap, r3, { { 8, 3 }, { 9, 5 }, { 10, 5 } });

    // patch page: the panel list on the left, the rest on the right
    const int lw = 470;
    auto* patchSec = at (11);
    patchSec->setBounds (0, 0, lw, pageArea.getHeight());
    fromBox.setBounds (14, 34, 212, 30);
    toBox.setBounds (232, 34, 224, 30);
    connectBtn.setBounds (14, 70, 212, 30);
    clearBtn.setBounds (232, 70, 224, 30);
    const int helpH = 112;
    cableView.setBounds (14, 110, lw - 28, pageArea.getHeight() - 110 - helpH - 10);
    cableList.setSize (cableView.getWidth() - 10, juce::jmax (cableView.getHeight(), (int) proc.getCables().size() * cableList.rowH + 4));
    patchHelp.setBounds (14, pageArea.getHeight() - helpH - 6, lw - 28, helpH);
    const int rx = lw + gap, rw = pw - rx;
    at (12)->setBounds (rx, 0, rw, 250);
    at (13)->setBounds (rx, 250 + gap, rw, 140);
    at (14)->setBounds (rx, 250 + gap + 140 + gap, rw, pageArea.getHeight() - (250 + 140 + 2 * gap));

    // performance strip
    wheelLabel.setBounds (20, H - 52, 90, 36);
    wheel.setBounds (120, H - 52, 360, 36);
    mswBtn.setBounds (500, H - 50, 130, 32);
}

void AM20Editor::showPage (bool patch)
{
    showPatch = patch;
    panelPage.setVisible (! patch);
    patchPage.setVisible (patch);
    panelTab.setToggleState (! patch, juce::dontSendNotification);
    patchTab.setToggleState (patch, juce::dontSendNotification);
}

void AM20Editor::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) proc.bank.size(); ++i) presetBox.addItem (juce::String (i + 1) + "  " + juce::String (proc.bank[(size_t) i].name), i + 1);
    const int cur = proc.getCurrentProgram();
    if (cur >= 0 && cur < (int) proc.bank.size() && juce::String (proc.bank[(size_t) cur].name) == proc.presetName)
        presetBox.setSelectedItemIndex (cur, juce::dontSendNotification);
    else
        presetBox.setText (proc.presetName, juce::dontSendNotification);
    const auto fx = proc.effectsKept();
    fxBtn.setVisible (fx.isNotEmpty());
    fxBtn.setTooltip ("Effects kept for .fxp / .fxb: " + fx);
}

void AM20Editor::refreshCables()
{
    cableList.setSize (juce::jmax (100, cableView.getWidth() - 10), juce::jmax (cableView.getHeight(), (int) proc.getCables().size() * cableList.rowH + 4));
    cableList.repaint();
    patchTab.setButtonText (proc.getCables().empty() ? "PATCH + MOD" : "PATCH + MOD (" + juce::String ((int) proc.getCables().size()) + ")");
}

void AM20Editor::timerCallback()
{
    if ((int) proc.getCables().size() != lastCableCount) { lastCableCount = (int) proc.getCables().size(); refreshCables(); }
}

void AM20Editor::loadFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load an AM-20 preset or bank, or the original's .fxp / .fxb", startDir (proc.lastPresetFile), "*.am20;*.fxp;*.fxb");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        juce::MemoryBlock mb; juce::String name; juce::File f;
        if (! readChosen (fc, mb, name, f)) return;
        juce::String err;
        if (! proc.loadPresetData (mb, name, err)) { warn (err); return; }
        if (f != juce::File()) proc.lastPresetFile = f;
        refreshPresets(); refreshCables();
    });
}

void AM20Editor::saveMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Preset as AM-20 (.am20)");
    m.addItem (2, "Bank as AM-20 (.am20)");
    m.addSeparator();
    m.addItem (3, "Preset for the original (.fxp)");
    m.addItem (4, "Bank for the original (.fxb)");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&saveBtn), [this] (int r) { if (r > 0) saveAs (r); });
}

void AM20Editor::saveAs (int kind)
{
    const juce::String ext = kind == 3 ? "fxp" : kind == 4 ? "fxb" : "am20";
    const juce::String base = kind == 2 || kind == 4 ? proc.bankName : proc.presetName;
    chooser = std::make_unique<juce::FileChooser> (kind == 2 || kind == 4 ? "Save the bank" : "Save the preset",
                                                   startDir (proc.lastPresetFile).getChildFile (juce::File::createLegalFileName (base.isEmpty() ? "AM-20" : base) + "." + ext), "*." + ext);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, kind, ext] (const juce::FileChooser& fc)
    {
        const auto url = fc.getURLResult();
        if (url.isEmpty() && fc.getResult() == juce::File()) return;
        if (kind == 1 || kind == 3)
        {
            auto nm = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : chosenName (url).upToLastOccurrenceOf (".", false, false);
            if (nm.isNotEmpty()) proc.presetName = nm;
        }
        const auto mb = kind == 1 ? proc.presetAsAm20() : kind == 2 ? proc.bankAsAm20() : kind == 3 ? proc.presetAsFxp() : proc.bankAsFxb();
        juce::File f;
        if (! writeChosen (fc, mb, "." + ext, f)) { warn ("The file could not be written there."); return; }
        if (f != juce::File()) proc.lastPresetFile = f;
        refreshPresets();
    });
}
