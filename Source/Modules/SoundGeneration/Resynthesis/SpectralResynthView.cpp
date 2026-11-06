#include "SpectralResynthShared.h"

using namespace aquanode;
using namespace aquanode::resynthdetail;

//==============================================================================
// Spectral Resynthesis's editor:
//   - the status line: what is loaded (or the take being recorded), what was found
//   - tools: Select (drag a box: it joins the selection; tap a region: it
//     leaves it; drag Start / End), Draw (draw any shape around what you
//     want: it joins the selection), Note (tap a partial: its note, found and
//     followed, shows white). Clear Selection, Undo (the last edit), Reset
//     (every edit), - / + and the wheel zoom the frequency axis, up / down
//     slide it; Shift + wheel zooms time
//   - the map: time across (Start..End bright; the voices' positions as white
//     lines), frequency up. Bins: the spectrogram itself; Partials: every
//     partial as a line, brighter where louder. The bars along its bottom and
//     right edges scroll it (drag the bar) and zoom it (drag a bar's end);
//     a double tap on the map or a bar shows everything again
//   - Selection: Delete, Keep, Reverse, Transpose, Rotate, Gain, Smear - on
//     every region at once, one edit after another (the regions stay, and
//     follow a Transpose)
//   - Note: Remove, Transpose, Gain, Unison, Fundamental Only, No Fundamental,
//     Odd Only - on the note and all its harmonics
//   - Amounts: the semitones, degrees, dB and unison the buttons use
//   - the edits so far, in order
//==============================================================================
namespace
{
    using Module = SpectralResynthModule;

    struct AmountSlider : public juce::Slider
    {
        AmountSlider (double lo, double hi, double step, double value, const juce::String& suffix)
            : juce::Slider (juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft)
        {
            setRange (lo, hi, step);
            setValue (value, juce::dontSendNotification);
            setTextValueSuffix (suffix);
            setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
            setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
            setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::black.withAlpha (0.35f));
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        }
    };

    // a scroll bar whose thumb is the part shown: drag the thumb to move it,
    // drag one of its ends to show more or less, tap the track to jump there,
    // double tap to show everything. Values 0..1 (vertical: 0 at the bottom).
    struct RangeBar : public juce::Component
    {
        explicit RangeBar (bool isVertical) : vertical (isVertical) {}

        float lo { 0.0f }, hi { 1.0f };
        float minSpan { 0.01f };
        std::function<void (float, float)> onChange;

        void set (float a, float b) { lo = a; hi = b; repaint(); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (r, 3.0f);
            const auto t = thumb().reduced (1.0f);
            g.setColour (juce::Colours::white.withAlpha (dragging ? 0.75f : 0.5f));
            g.fillRoundedRectangle (t, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));   // the grips at its ends
            if (vertical)
            {
                g.fillRect (t.getX() + 2.0f, t.getY() + 3.0f, t.getWidth() - 4.0f, 1.5f);
                g.fillRect (t.getX() + 2.0f, t.getBottom() - 4.5f, t.getWidth() - 4.0f, 1.5f);
            }
            else
            {
                g.fillRect (t.getX() + 3.0f, t.getY() + 2.0f, 1.5f, t.getHeight() - 4.0f);
                g.fillRect (t.getRight() - 4.5f, t.getY() + 2.0f, 1.5f, t.getHeight() - 4.0f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const float p = pos (e), a = pos (lo), b = pos (hi);
            const float grip = juce::jmin (10.0f, std::abs (b - a) * 0.3f);
            downLo = lo; downHi = hi; downAt = p;
            if (std::abs (p - a) <= grip)      mode = vertical ? Mode::Hi : Mode::Lo;   // vertical: lo is drawn at the bottom
            else if (std::abs (p - b) <= grip) mode = vertical ? Mode::Lo : Mode::Hi;
            else if (p > juce::jmin (a, b) && p < juce::jmax (a, b)) mode = Mode::Move;
            else
            {
                // jump: the thumb centred where the track was tapped
                const float span = hi - lo, c = value (p);
                apply (c - span * 0.5f, c + span * 0.5f);
                downLo = lo; downHi = hi;
                mode = Mode::Move;
            }
            dragging = true;
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float d = value (pos (e)) - value (downAt);
            if (mode == Mode::Move)    apply (downLo + d, downHi + d);
            else if (mode == Mode::Lo) apply (juce::jmin (downLo + d, downHi - minSpan), downHi);
            else if (mode == Mode::Hi) apply (downLo, juce::jmax (downHi + d, downLo + minSpan));
        }
        void mouseUp (const juce::MouseEvent&) override { dragging = false; mode = Mode::None; repaint(); }
        void mouseDoubleClick (const juce::MouseEvent&) override { apply (0.0f, 1.0f); }

    private:
        enum class Mode { None, Move, Lo, Hi };
        const bool vertical;
        Mode mode { Mode::None };
        bool dragging { false };
        float downLo { 0.0f }, downHi { 1.0f }, downAt { 0.0f };

        float length() const { return (float) juce::jmax (1, vertical ? getHeight() : getWidth()); }
        float pos (const juce::MouseEvent& e) const { return vertical ? (float) e.y : (float) e.x; }
        float pos (float v) const { return vertical ? (1.0f - v) * length() : v * length(); }
        float value (float p) const { return vertical ? 1.0f - p / length() : p / length(); }
        juce::Rectangle<float> thumb() const
        {
            const float a = pos (lo), b = pos (hi);
            return vertical ? juce::Rectangle<float> (0.0f, juce::jmin (a, b), (float) getWidth(), juce::jmax (6.0f, std::abs (b - a)))
                            : juce::Rectangle<float> (juce::jmin (a, b), 0.0f, juce::jmax (6.0f, std::abs (b - a)), (float) getHeight());
        }
        void apply (float a, float b)
        {
            const float span = juce::jlimit (minSpan, 1.0f, b - a);
            a = juce::jlimit (0.0f, 1.0f - span, a);
            lo = a; hi = a + span;
            repaint();
            if (onChange) onChange (lo, hi);
        }
    };
}

class SpectralResynthDisplay : public juce::Component,
                               public ExtraContentParamListener,
                               private juce::Timer
{
public:
    explicit SpectralResynthDisplay (Module& m) : weak (&m), raw (&m)
    {
        for (auto* b : { &selectTool, &drawTool, &noteTool })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (0x5e1);
            b->onClick = [this] { repaint(); };
        }
        selectTool.setToggleState (true, juce::dontSendNotification);

        for (auto* b : allButtons())
            addAndMakeVisible (*b);
        for (auto* s : { &semis, &degrees, &decibels, &smearMs, &unisonVoices, &unisonDetune })
            addAndMakeVisible (*s);

        // the bars along the map's edges: time (linear) and frequency (log)
        timeBar.onChange = [this] (float a, float b)
        {
            if (shown == nullptr) return;
            viewT0 = a * (float) shown->duration;
            viewT1 = b * (float) shown->duration;
            invalidate();
        };
        freqBar.onChange = [this] (float a, float b)
        {
            const float top = freqTop(), lo = 15.0f;
            setView (lo * std::pow (top / lo, a), lo * std::pow (top / lo, b), false);
        };
        freqBar.minSpan = 0.06f;
        addAndMakeVisible (timeBar);
        addAndMakeVisible (freqBar);

        clearSel.onClick = [this] { if (auto* m = mod()) { m->selection.clear(); invalidate(); } };
        undo.onClick = [this] { if (auto* m = mod()) m->undoEdit(); };
        reset.onClick = [this] { if (auto* m = mod()) m->setEdits ({}); };
        zoomIn.onClick = [this] { zoomBy (0.6f, viewCentreHz()); };
        zoomOut.onClick = [this] { zoomBy (1.0f / 0.6f, viewCentreHz()); };
        up.onClick = [this] { slide (0.25f); };
        down.onClick = [this] { slide (-0.25f); };

        regionOp (opDelete, Module::Op::Delete);
        regionOp (opKeep, Module::Op::Keep);
        regionOp (opReverse, Module::Op::Reverse);
        regionOp (opTranspose, Module::Op::Transpose);
        regionOp (opRotate, Module::Op::Rotate);
        regionOp (opGain, Module::Op::Gain);
        regionOp (opSmear, Module::Op::Smear);
        noteOp (nRemove, Module::Op::NoteRemove);
        noteOp (nTranspose, Module::Op::NoteTranspose);
        noteOp (nGain, Module::Op::NoteGain);
        noteOp (nUnison, Module::Op::NoteUnison);
        noteOp (nFund, Module::Op::NoteFundOnly);
        noteOp (nNoFund, Module::Op::NoteNoFund);
        noteOp (nOdd, Module::Op::NoteOddOnly);

        startTimerHz (30);
    }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds();
        statusArea = r.removeFromTop (kStatusH).reduced (4, 0);

        auto bar = r.removeFromTop (kRowH);
        selectTool.setBounds (bar.removeFromLeft (64).reduced (1));
        drawTool.setBounds (bar.removeFromLeft (56).reduced (1));
        noteTool.setBounds (bar.removeFromLeft (56).reduced (1));
        bar.removeFromLeft (6);
        clearSel.setBounds (bar.removeFromLeft (104).reduced (1));
        bar.removeFromLeft (6);
        undo.setBounds (bar.removeFromLeft (60).reduced (1));
        reset.setBounds (bar.removeFromLeft (60).reduced (1));
        for (auto* b : { &down, &up, &zoomIn, &zoomOut })
            b->setBounds (bar.removeFromRight (32).reduced (1));

        r.removeFromTop (4);
        {
            auto map = r.removeFromTop (kMapH);
            auto bottom = map.removeFromBottom (kBarW);
            freqBar.setBounds (map.removeFromRight (kBarW));
            map.removeFromRight (2);
            map.removeFromBottom (2);
            timeBar.setBounds (bottom.withWidth (map.getWidth()));
            mapArea = map;
        }
        r.removeFromTop (4);

        // the operations: the buttons of a row all as wide
        auto layoutRow = [] (juce::Rectangle<int> row, std::initializer_list<juce::TextButton*> buttons)
        {
            const int n = (int) buttons.size();
            const int w = row.getWidth();
            int i = 0;
            for (auto* b : buttons)
            {
                const int x0 = row.getX() + w * i / n, x1 = row.getX() + w * (i + 1) / n;
                b->setBounds (juce::Rectangle<int> (x0, row.getY(), x1 - x0, row.getHeight()).reduced (1));
                ++i;
            }
        };
        auto selRow = r.removeFromTop (kRowH);
        selLabel = selRow.removeFromLeft (64);
        layoutRow (selRow, { &opDelete, &opKeep, &opReverse, &opTranspose, &opRotate, &opGain, &opSmear });
        r.removeFromTop (3);

        auto noteRow = r.removeFromTop (kRowH);
        noteLabel = noteRow.removeFromLeft (64);
        layoutRow (noteRow, { &nRemove, &nTranspose, &nGain, &nUnison, &nFund, &nNoFund, &nOdd });
        r.removeFromTop (3);

        auto amounts = r.removeFromTop (kRowH);
        amountLabels.clear();
        const int each = amounts.getWidth() / 6;
        for (auto [s, label] : std::initializer_list<std::pair<juce::Slider*, const char*>> {
                 { &semis, "Pitch" }, { &degrees, "Turn" }, { &decibels, "Gain" }, { &smearMs, "Smear" },
                 { &unisonVoices, "Voices" }, { &unisonDetune, "Detune" } })
        {
            auto cell = amounts.removeFromLeft (each);
            amountLabels.push_back ({ cell.removeFromLeft (36), label });
            s->setBounds (cell.reduced (1, 2));
        }
        r.removeFromTop (3);
        editsArea = r.removeFromTop (kListH).reduced (4, 0);
        invalidate();
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = mod();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (mapArea.toFloat(), 3.0f);

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (m != nullptr && m->isRecording() ? juce::Colour (0xffff5a5a) : juce::Colours::white.withAlpha (0.85f));
        g.drawText (statusText(), statusArea, juce::Justification::centredLeft, true);

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("Selection", selLabel.withTrimmedLeft (4), juce::Justification::centredLeft);
        g.drawText ("Note", noteLabel.withTrimmedLeft (4), juce::Justification::centredLeft);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (const auto& [area, label] : amountLabels)
            g.drawFittedText (label, area.withTrimmedLeft (3), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText (editsText(), editsArea, juce::Justification::centredLeft, true);

        if (m == nullptr || shown == nullptr)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText (m != nullptr && m->isAnalysing() ? "analysing..." : "Load Sound, or patch Rec In and press Record",
                        mapArea, juce::Justification::centred);
            return;
        }

        if (cacheDirty || cache.getWidth() != mapArea.getWidth() || cache.getHeight() != mapArea.getHeight())
            rebuildCache();
        g.drawImageAt (cache, mapArea.getX(), mapArea.getY());

        g.saveState();
        g.reduceClipRegion (mapArea);
        const auto& model = *shown;

        // outside Start..End dimmed
        const float st = juce::jlimit (0.0f, 1.0f, m->getParameter ("start"));
        const float en = juce::jlimit (0.0f, 1.0f, m->getParameter ("end"));
        const float x0 = xOfTime (juce::jmin (st, en) * model.duration), x1 = xOfTime (juce::jmax (st, en) * model.duration);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> ((float) mapArea.getX(), (float) mapArea.getY(), x0 - mapArea.getX(), (float) mapArea.getHeight()));
        g.fillRect (juce::Rectangle<float> (x1, (float) mapArea.getY(), (float) mapArea.getRight() - x1, (float) mapArea.getHeight()));
        g.setColour (juce::Colours::aqua.withAlpha (0.9f));
        g.fillRect (x0 - 1.0f, (float) mapArea.getY(), 2.0f, (float) mapArea.getHeight());
        g.fillRect (x1 - 1.0f, (float) mapArea.getY(), 2.0f, (float) mapArea.getHeight());

        // the selection: boxes and drawn shapes
        for (const auto& b : m->selection)
        {
            const auto shape = regionPath (b);
            g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.16f));
            g.fillPath (shape);
            g.setColour (juce::Colour (0xff4fc3f7));
            g.strokePath (shape, juce::PathStrokeType (1.5f));
        }
        if (dragMode == Drag::Box)
        {
            g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float> (dragFrom.toFloat(), dragTo.toFloat()));
        }
        if (dragMode == Drag::Shape && drawn.size() > 1)
        {
            juce::Path p;
            p.startNewSubPath (drawn.front());
            for (size_t i = 1; i < drawn.size(); ++i) p.lineTo (drawn[i]);
            g.setColour (juce::Colour (0xff4fc3f7).withAlpha (0.2f));
            g.fillPath (p);
            g.setColour (juce::Colour (0xff4fc3f7));
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }

        // the note: white
        if (! m->selectedNote.isEmpty())
        {
            const auto& note = m->selectedNote;
            const int maxH = juce::jlimit (1, 60, (int) (model.nyquist() / juce::jmax (1.0f, note.f0At (note.start()))));
            for (int h = 1; h <= maxH; ++h)
            {
                juce::Path p;
                bool started = false;
                for (const auto& pt : note.pts)
                {
                    const float y = yOfHz (pt.y * (float) h);
                    if (y < (float) mapArea.getY() - 50.0f) break;
                    if (! started) { p.startNewSubPath (xOfTime (pt.x), y); started = true; }
                    else p.lineTo (xOfTime (pt.x), y);
                }
                if (note.pts.size() == 1)
                    p.addEllipse (xOfTime (note.start()) - 2.0f, yOfHz (note.pts.front().y * (float) h) - 2.0f, 4.0f, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (h == 1 ? 1.0f : 0.85f));
                g.strokePath (p, juce::PathStrokeType (h == 1 ? 2.4f : 1.5f));
            }
        }

        // where the voices are
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        for (int v = 0; v < kMaxVoices; ++v)
            if (const float t = m->voicePosition (v); t >= 0.0f)
                g.fillRect (xOfTime (t) - 0.5f, (float) mapArea.getY(), 1.5f, (float) mapArea.getHeight());

        // the frequency grid
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float y = yOfHz (hz);
            if (y < mapArea.getY() + 6 || y > mapArea.getBottom() - 4) continue;
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int) y, (float) mapArea.getX(), (float) mapArea.getRight());
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz),
                        mapArea.getX() + 2, (int) y - 11, 30, 11, juce::Justification::bottomLeft);
        }
        g.restoreState();
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragMode = Drag::None;
        dragFrom = dragTo = e.getPosition();
        auto* m = mod();
        if (m == nullptr || shown == nullptr || ! mapArea.contains (e.getPosition()))
            return;
        if (noteTool.getToggleState())
        {
            dragMode = Drag::NoteTap;
            return;
        }
        if (drawTool.getToggleState())
        {
            dragMode = Drag::Shape;
            drawn.clear();
            drawn.push_back (e.position);
            return;
        }
        const float st = m->getParameter ("start") * (float) shown->duration;
        const float en = m->getParameter ("end") * (float) shown->duration;
        if (std::abs ((float) e.x - xOfTime (st)) < 10.0f) { dragMode = Drag::Start; return; }
        if (std::abs ((float) e.x - xOfTime (en)) < 10.0f) { dragMode = Drag::End; return; }
        dragMode = Drag::Box;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        dragTo = e.getPosition();
        auto* m = mod();
        if (m == nullptr || shown == nullptr)
            return;
        if (dragMode == Drag::Shape)
        {
            const auto p = e.position.withX (juce::jlimit ((float) mapArea.getX(), (float) mapArea.getRight(), e.position.x))
                                     .withY (juce::jlimit ((float) mapArea.getY(), (float) mapArea.getBottom(), e.position.y));
            if (drawn.empty() || drawn.back().getDistanceFrom (p) >= 3.0f)
                drawn.push_back (p);
        }
        if (dragMode == Drag::Start || dragMode == Drag::End)
        {
            const float t = timeOfX ((float) e.x) / (float) juce::jmax (1.0e-6, shown->duration);
            m->setParameter (dragMode == Drag::Start ? "start" : "end", juce::jlimit (0.0f, 1.0f, t));
            if (paramValuesChangedByContent) paramValuesChangedByContent();
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        if (m != nullptr && shown != nullptr)
        {
            if (dragMode == Drag::NoteTap)
            {
                m->selectedNote = Module::findNote (*shown, timeOfX ((float) e.x), hzOfY ((float) e.y));
                noteMessage = m->selectedNote.isEmpty() ? "no note found there" : juce::String();
            }
            else if ((dragMode == Drag::Box || dragMode == Drag::Shape) && e.getDistanceFromDragStart() < 6)
            {
                // a tap on a region takes it out of the selection
                for (int i = (int) m->selection.size(); --i >= 0;)
                    if (regionHit (m->selection[(size_t) i], e.position))
                    {
                        m->selection.erase (m->selection.begin() + i);
                        break;
                    }
            }
            else if (dragMode == Drag::Shape)
            {
                // the drawn outline, at most 200 points, in seconds and Hz
                if (drawn.size() >= 3)
                {
                    Module::Box shape;
                    const size_t step = juce::jmax ((size_t) 1, (drawn.size() + 199) / 200);
                    for (size_t i = 0; i < drawn.size(); i += step)
                        shape.poly.push_back ({ timeOfX (drawn[i].x), hzOfY (drawn[i].y) });
                    shape.finishShape();
                    if (shape.isShape() && shape.t1 > shape.t0 && shape.f1 > shape.f0)
                        m->selection.push_back (std::move (shape));
                }
                drawn.clear();
            }
            else if (dragMode == Drag::Box)
            {
                const auto a = dragFrom.toFloat(), b = dragTo.toFloat();
                Module::Box box;
                box.t0 = timeOfX (juce::jmin (a.x, b.x));
                box.t1 = timeOfX (juce::jmax (a.x, b.x));
                box.f0 = hzOfY (juce::jmax (a.y, b.y));
                box.f1 = hzOfY (juce::jmin (a.y, b.y));
                m->selection.push_back (box);
            }
        }
        dragMode = Drag::None;
        invalidate();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (mapArea.contains (e.getPosition()) && shown != nullptr)
        {
            viewT0 = 0.0f;
            viewT1 = (float) shown->duration;
            setView (30.0f, juce::jmin (20000.0f, shown->nyquist()));
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! mapArea.contains (e.getPosition()) || shown == nullptr)
            return;
        if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            // Shift + wheel zooms time around the pointer; a sideways wheel scrolls it
            const float span = viewT1 - viewT0;
            if (std::abs (w.deltaX) > std::abs (w.deltaY))
                setTimeView (viewT0 - w.deltaX * span * 0.5f, viewT1 - w.deltaX * span * 0.5f);
            else
            {
                const float at = timeOfX ((float) e.x), f = std::pow (0.5f, w.deltaY * 2.0f);
                setTimeView (at + (viewT0 - at) * f, at + (viewT1 - at) * f);
            }
            return;
        }
        zoomBy (std::pow (0.5f, w.deltaY * 2.0f), hzOfY ((float) e.y));
    }

    void mouseMagnify (const juce::MouseEvent& e, float scale) override
    {
        if (mapArea.contains (e.getPosition()) && shown != nullptr && scale > 0.0f)
            zoomBy (1.0f / scale, hzOfY ((float) e.y));
    }

private:
    static constexpr int kStatusH = 18, kRowH = 28, kMapH = 304, kListH = 18;   // 466 of the 470
    static constexpr int kBarW = 12;   // the scroll bars along the map
    enum class Drag { None, Box, Shape, Start, End, NoteTap };

    Module* mod() const { return weak.get() != nullptr ? raw : nullptr; }

    std::vector<juce::Button*> allButtons()
    {
        return { &selectTool, &drawTool, &noteTool, &clearSel, &undo, &reset, &zoomIn, &zoomOut, &up, &down,
                 &opDelete, &opKeep, &opReverse, &opTranspose, &opRotate, &opGain, &opSmear,
                 &nRemove, &nTranspose, &nGain, &nUnison, &nFund, &nNoFund, &nOdd };
    }

    void regionOp (juce::TextButton& b, Module::Op op)
    {
        b.onClick = [this, op]
        {
            auto* m = mod();
            if (m == nullptr || m->selection.empty())
                return;
            Module::Edit e;
            e.op = op;
            e.regions = m->selection;
            e.amount = op == Module::Op::Transpose ? (float) semis.getValue()
                     : op == Module::Op::Rotate ? (float) degrees.getValue()
                     : op == Module::Op::Gain ? (float) decibels.getValue()
                     : op == Module::Op::Smear ? (float) smearMs.getValue() : 0.0f;
            m->addEdit (e);
            // the regions go with what a Transpose moved, so the next edit finds it
            if (op == Module::Op::Transpose)
            {
                const float r = std::pow (2.0f, e.amount / 12.0f);
                for (auto& box : m->selection) box.scaleFrequency (r);
            }
            invalidate();
        };
    }

    void noteOp (juce::TextButton& b, Module::Op op)
    {
        b.onClick = [this, op]
        {
            auto* m = mod();
            if (m == nullptr || m->selectedNote.isEmpty())
                return;
            Module::Edit e;
            e.op = op;
            e.note = m->selectedNote;
            e.amount = op == Module::Op::NoteTranspose ? (float) semis.getValue()
                     : op == Module::Op::NoteGain ? (float) decibels.getValue()
                     : op == Module::Op::NoteUnison ? (float) unisonVoices.getValue() : 0.0f;
            e.amount2 = op == Module::Op::NoteUnison ? (float) unisonDetune.getValue() : 0.0f;
            m->addEdit (e);
            if (op == Module::Op::NoteTranspose)
            {
                const float r = std::pow (2.0f, e.amount / 12.0f);
                for (auto& p : m->selectedNote.pts) p.y *= r;   // the note goes where it was moved
            }
            if (op == Module::Op::NoteRemove)
                m->selectedNote = {};
            invalidate();
        };
    }

    //=== what is shown ========================================================
    void timerCallback() override
    {
        auto* m = mod();
        if (m == nullptr)
            return;
        bool dirty = false;
        auto latest = m->currentModel();
        if (latest != shown)
        {
            const bool first = shown == nullptr || latest == nullptr || shown->binsEngine != latest->binsEngine;
            shown = latest;
            if (shown != nullptr && (first || viewHi > shown->nyquist() * 1.01f))
            {
                viewLo = 30.0f;
                viewHi = juce::jmin (20000.0f, shown->nyquist());
            }
            if (shown != nullptr && std::abs (shown->duration - shownDuration) > 1.0e-6)
            {
                // another sound: all of it in view
                shownDuration = shown->duration;
                viewT0 = 0.0f;
                viewT1 = (float) shown->duration;
            }
            invalidate();
            if (paramValuesChangedByContent) paramValuesChangedByContent();   // Root / Fine may have been set
            dirty = true;
        }
        const int keep = (int) m->getParameter ("partials");
        if (keep != shownKeep) { shownKeep = keep; invalidate(); }
        if (m->selectedNote.pts.size() != shownNotePoints || (! m->selectedNote.isEmpty() && m->selectedNote.start() != shownNoteStart))
        {
            shownNotePoints = m->selectedNote.pts.size();
            shownNoteStart = m->selectedNote.isEmpty() ? -1.0f : m->selectedNote.start();
            invalidate();
        }

        bool anyVoice = false;
        for (int v = 0; v < kMaxVoices; ++v)
            anyVoice = anyVoice || m->voicePosition (v) >= 0.0f;
        const float st = m->getParameter ("start"), en = m->getParameter ("end");
        const bool busy = m->isAnalysing() || m->isRecording();
        if (anyVoice || hadVoices || st != lastStart || en != lastEnd || busy || wasBusy)
            dirty = true;
        hadVoices = anyVoice;
        wasBusy = busy;
        lastStart = st;
        lastEnd = en;

        const bool sel = ! m->selection.empty(), note = ! m->selectedNote.isEmpty();
        for (auto* b : { &opDelete, &opKeep, &opReverse, &opTranspose, &opRotate, &opGain, &opSmear, &clearSel })
            b->setEnabled (sel);
        for (auto* b : { &nRemove, &nTranspose, &nGain, &nUnison, &nFund, &nNoFund, &nOdd })
            b->setEnabled (note);
        const int numEdits = (int) m->getEdits().size();
        undo.setEnabled (numEdits > 0);
        reset.setEnabled (numEdits > 0);
        if (numEdits != shownEdits) { shownEdits = numEdits; dirty = true; }

        if (dirty)
            repaint();
    }

    void invalidate()
    {
        cacheDirty = true;
        updateBars();
        repaint();
    }

    void updateBars()
    {
        const double d = shown != nullptr ? juce::jmax (1.0e-6, shown->duration) : 1.0;
        timeBar.set ((float) (viewT0 / d), (float) (viewT1 / d));
        timeBar.minSpan = (float) juce::jlimit (0.002, 1.0, 0.05 / d);   // 50 ms at the closest
        const float top = freqTop(), lo = 15.0f;
        freqBar.set (std::log (viewLo / lo) / std::log (top / lo), std::log (viewHi / lo) / std::log (top / lo));
    }

    static juce::String timeText (double s)
    {
        const int mins = (int) (s / 60.0);
        return juce::String (mins) + ":" + juce::String (s - mins * 60.0, 1).paddedLeft ('0', 4);
    }

    juce::String statusText() const
    {
        auto* m = mod();
        if (m == nullptr)
            return {};
        if (m->isRecording())
            return juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f")) + " REC " + timeText (m->recordedSeconds()) + " / 2:00.0"
                   "  - press Record again to stop; the take becomes the sound";
        if (m->isAnalysing())
            return (shown != nullptr ? "applying edits / analysing " : "analysing ")
                   + juce::String (juce::roundToInt (m->analysisProgress() * 100.0f)) + " %";
        if (shown == nullptr)
            return "no sound";
        juce::String s;
        if (shown->binsEngine)
            s << "Bins: " << shown->bins->N << "-point frames, " << shown->bins->channels << (shown->bins->channels > 1 ? " channels" : " channel");
        else
            s << "Partials: " << (int) shown->partials->tracks.size() << " tracks";
        s << "  " << timeText (shown->duration);
        if (shown->f0 > 0.0f && shown->f0Confidence >= 0.35f)
            s << "  sound's f0 " << resynth::hzText (shown->f0) << " " << resynth::noteName (shown->f0);
        if (! m->selectedNote.isEmpty())
            s << "  |  note " << resynth::noteName (m->selectedNote.f0At (m->selectedNote.start())) << ", "
              << timeText (m->selectedNote.start()) << " - " << timeText (m->selectedNote.end());
        else if (noteMessage.isNotEmpty())
            s << "  |  " << noteMessage;
        return s;
    }

    juce::String editsText() const
    {
        auto* m = mod();
        if (m == nullptr)
            return {};
        const auto e = m->getEdits();
        if (e.empty())
            return "No edits. Select regions (drag a box, or Draw a shape) or a note (Note, tap), then apply edits one after another.";
        juce::String s ("Edits: ");
        const int from = juce::jmax (0, (int) e.size() - 5);
        if (from > 0) s << "... ";
        for (int i = from; i < (int) e.size(); ++i)
            s << (i + 1) << ". " << Module::describe (e[(size_t) i]) << (i + 1 < (int) e.size() ? "   " : "");
        return s;
    }

    //=== geometry =============================================================
    float xOfTime (double t) const
    {
        const double span = juce::jmax (1.0e-6, (double) (viewT1 - viewT0));
        return (float) mapArea.getX() + (float) ((t - viewT0) / span) * (float) mapArea.getWidth();
    }
    float timeOfX (float x) const
    {
        const double d = shown != nullptr ? shown->duration : 1.0;
        const double t = viewT0 + (double) (x - mapArea.getX()) / juce::jmax (1, mapArea.getWidth()) * (viewT1 - viewT0);
        return (float) juce::jlimit (0.0, d, t);
    }
    float yOfHz (float hz) const
    {
        const float n = std::log (juce::jmax (1.0f, hz) / viewLo) / std::log (viewHi / viewLo);
        return (float) mapArea.getBottom() - n * (float) mapArea.getHeight();
    }
    float hzOfY (float y) const
    {
        const float n = ((float) mapArea.getBottom() - y) / (float) juce::jmax (1, mapArea.getHeight());
        return viewLo * std::pow (viewHi / viewLo, n);
    }
    juce::Rectangle<float> boxRect (const Module::Box& b) const
    {
        return juce::Rectangle<float>::leftTopRightBottom (xOfTime (b.t0), yOfHz (b.f1), xOfTime (b.t1), yOfHz (b.f0));
    }
    juce::Path regionPath (const Module::Box& b) const
    {
        juce::Path p;
        if (! b.isShape())
        {
            p.addRectangle (boxRect (b));
            return p;
        }
        p.startNewSubPath (xOfTime (b.poly.front().x), yOfHz (b.poly.front().y));
        for (size_t i = 1; i < b.poly.size(); ++i)
            p.lineTo (xOfTime (b.poly[i].x), yOfHz (b.poly[i].y));
        p.closeSubPath();
        return p;
    }
    bool regionHit (const Module::Box& b, juce::Point<float> at) const
    {
        return b.isShape() ? b.contains (timeOfX (at.x), hzOfY (at.y)) : boxRect (b).contains (at);
    }
    float freqTop() const { return shown != nullptr ? juce::jmin (22000.0f, shown->nyquist()) : 20000.0f; }
    float viewCentreHz() const { return std::sqrt (viewLo * viewHi); }

    void setView (float lo, float hi, bool bars = true)
    {
        const float top = freqTop();
        const float octaves = juce::jlimit (0.5f, std::log2 (top / 15.0f), std::log2 (hi / lo));
        lo = juce::jlimit (15.0f, top / std::pow (2.0f, octaves), lo);
        viewLo = lo;
        viewHi = lo * std::pow (2.0f, octaves);
        cacheDirty = true;
        if (bars) updateBars();
        repaint();
    }
    void setTimeView (float t0, float t1)
    {
        if (shown == nullptr) return;
        const float d = (float) shown->duration;
        const float span = juce::jlimit (juce::jmin (d, 0.05f), d, t1 - t0);
        t0 = juce::jlimit (0.0f, d - span, t0);
        viewT0 = t0;
        viewT1 = t0 + span;
        invalidate();
    }
    void zoomBy (float factor, float aroundHz)
    {
        const float a = std::log2 (aroundHz), lo = std::log2 (viewLo), hi = std::log2 (viewHi);
        setView (std::pow (2.0f, a + (lo - a) * factor), std::pow (2.0f, a + (hi - a) * factor));
    }
    void slide (float ofView)
    {
        const float oct = std::log2 (viewHi / viewLo) * ofView;
        setView (viewLo * std::pow (2.0f, oct), viewHi * std::pow (2.0f, oct));
    }

    //=== the cached map =======================================================
    void rebuildCache()
    {
        cacheDirty = false;
        const int w = juce::jmax (1, mapArea.getWidth()), h = juce::jmax (1, mapArea.getHeight());
        cache = juce::Image (juce::Image::ARGB, w, h, true);
        if (shown == nullptr || shown->numFrames() <= 0)
            return;
        if (shown->binsEngine)
            drawSpectrogram (w, h);
        else
            drawPartials (w, h);
    }

    // Bins: the spectrogram, the channels' power summed, 90 dB of range
    void drawSpectrogram (int w, int h)
    {
        const auto& b = *shown->bins;
        juce::Image::BitmapData px (cache, juce::Image::BitmapData::writeOnly);
        std::vector<int> binOfRow ((size_t) h);
        for (int y = 0; y < h; ++y)
        {
            const float hz = hzOfY ((float) (mapArea.getY() + y) + 0.5f);
            binOfRow[(size_t) y] = juce::jlimit (0, b.bins - 1, juce::roundToInt (hz * (float) b.N / (float) b.rate));
        }
        auto colourOf = [] (float v)   // 0..1
        {
            const juce::Colour stops[] = { juce::Colour (0xff05070d), juce::Colour (0xff12305a), juce::Colour (0xff1b7f8c),
                                           juce::Colour (0xffd8c84a), juce::Colour (0xffffffff) };
            const float x = juce::jlimit (0.0f, 0.9999f, v) * 4.0f;
            const int i = (int) x;
            return stops[i].interpolatedWith (stops[i + 1], x - (float) i);
        };
        const float top = b.topDb;
        for (int x = 0; x < w; ++x)
        {
            const int f = b.frameAt (timeOfX ((float) (mapArea.getX() + x) + 0.5f));
            const auto& fr = *b.frames[(size_t) f];
            for (int y = 0; y < h; ++y)
            {
                const int k = binOfRow[(size_t) y];
                float pw = 0.0f;
                for (int c = 0; c < b.channels; ++c)
                {
                    const float a = decodeMag (fr.mag[(size_t) (c * b.bins + k)]);
                    pw += a * a;
                }
                const float db = 10.0f * std::log10 (pw / (float) b.channels + 1.0e-20f) - top;
                px.setPixelColour (x, y, colourOf ((db + 90.0f) / 90.0f));
            }
        }
    }

    // Partials: every track as a line, brighter where louder; dim past Partials
    void drawPartials (int w, int h)
    {
        juce::ignoreUnused (h);
        const auto& m = *shown->partials;
        juce::Graphics g (cache);
        g.addTransform (juce::AffineTransform::translation ((float) -mapArea.getX(), (float) -mapArea.getY()));

        const int fFirst = juce::jlimit (0, m.numFrames - 1, m.frameAt (viewT0) - 1);
        const int fLast = juce::jlimit (0, m.numFrames - 1, m.frameAt (viewT1) + 1);
        const int visible = fLast - fFirst + 1;
        const int cols = juce::jmin (w, visible);
        for (int c = 0; c < cols; ++c)
        {
            const float x = (float) mapArea.getX() + (float) c * (float) w / (float) cols;
            const int f = m.frameAt (timeOfX (x + 0.5f));
            for (int b = 0; b < Module::kBands; ++b)
            {
                const float lo = m.bandEdges[(size_t) b], hi = m.bandEdges[(size_t) b + 1];
                const float density = m.noise[(size_t) f * Module::kBands + (size_t) b] / juce::jmax (1.0f, hi - lo);
                const float level = juce::jlimit (0.0f, 1.0f, (10.0f * std::log10 (density + 1.0e-20f) + 110.0f) / 70.0f);
                if (level <= 0.02f) continue;
                g.setColour (juce::Colour (0xff2c4660).withAlpha (level * 0.3f));
                const float y0 = yOfHz (hi), y1 = yOfHz (lo);
                g.fillRect (x, y0, (float) w / (float) cols + 0.5f, y1 - y0);
            }
        }

        const int stride = juce::jmax (1, (int) (visible / (w * 1.5)));
        std::vector<int> lastFrame (m.tracks.size(), -1000000);
        std::vector<juce::Point<float>> lastPt (m.tracks.size());
        int prevF = -1;
        for (int f = fFirst; f <= fLast; f += stride)
        {
            const float x = xOfTime (m.frameTime (f));
            for (int i = m.frameStart[(size_t) f]; i < m.frameStart[(size_t) f + 1]; ++i)
            {
                const auto& p = m.points[(size_t) i];
                const juce::Point<float> pt (x, yOfHz (p.hz));
                if (lastFrame[(size_t) p.track] == prevF && prevF >= 0)
                {
                    const float level = juce::jlimit (0.1f, 1.0f, (juce::Decibels::gainToDecibels (p.amp, -120.0f) + 90.0f) / 66.0f);
                    const bool dim = m.tracks[(size_t) p.track].rank >= shownKeep;
                    g.setColour (dim ? juce::Colour (0xff606060).withAlpha (0.35f)
                                     : juce::Colour (0xff7fd8e8).withMultipliedBrightness (0.45f + 0.55f * level).withAlpha (0.35f + 0.65f * level));
                    g.drawLine ({ lastPt[(size_t) p.track], pt }, dim ? 0.8f : 0.8f + level);
                }
                lastFrame[(size_t) p.track] = f;
                lastPt[(size_t) p.track] = pt;
            }
            prevF = f;
        }
    }

    //==========================================================================
    juce::WeakReference<SynthModule> weak;
    Module* raw;

    juce::TextButton selectTool { "Select" }, drawTool { "Draw" }, noteTool { "Note" }, clearSel { "Clear Selection" },
                     undo { "Undo" }, reset { "Reset" }, zoomIn { "+" }, zoomOut { "-" },
                     up { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")) }, down { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")) },
                     opDelete { "Delete" }, opKeep { "Keep" }, opReverse { "Reverse" }, opTranspose { "Transpose" },
                     opRotate { "Rotate" }, opGain { "Gain" }, opSmear { "Smear" },
                     nRemove { "Remove" }, nTranspose { "Transpose" }, nGain { "Gain" }, nUnison { "Unison" },
                     nFund { "Fund Only" }, nNoFund { "No Fund" }, nOdd { "Odd Only" };
    AmountSlider semis { -48.0, 48.0, 1.0, 12.0, " st" }, degrees { -180.0, 180.0, 15.0, 90.0, juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) },
                 decibels { -48.0, 24.0, 1.0, -6.0, " dB" }, smearMs { 20.0, 4000.0, 10.0, 400.0, " ms" }, unisonVoices { 2.0, 7.0, 1.0, 3.0, "" }, unisonDetune { 1.0, 100.0, 1.0, 12.0, " ct" };

    juce::Rectangle<int> statusArea, mapArea, selLabel, noteLabel, editsArea;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> amountLabels;
    std::shared_ptr<const Module::Model> shown;
    juce::String noteMessage;
    int shownKeep { -1 }, shownEdits { -1 };
    size_t shownNotePoints { 0 };
    float shownNoteStart { -1.0f };
    float viewLo { 30.0f }, viewHi { 20000.0f };
    float viewT0 { 0.0f }, viewT1 { 1.0f };   // seconds shown
    double shownDuration { -1.0 };
    std::vector<juce::Point<float>> drawn;     // the shape being drawn (Draw)
    RangeBar timeBar { false }, freqBar { true };
    float lastStart { -1.0f }, lastEnd { -1.0f };
    bool hadVoices { false }, wasBusy { false }, cacheDirty { true };
    juce::Image cache;
    Drag dragMode { Drag::None };
    juce::Point<int> dragFrom, dragTo;
};

std::unique_ptr<juce::Component> SpectralResynthModule::createExtraContentComponent()
{
    return std::make_unique<SpectralResynthDisplay> (*this);
}
