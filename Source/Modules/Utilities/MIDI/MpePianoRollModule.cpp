#include "MpePianoRollModule.h"

using namespace aquanode;

bool MpePianoRollModule::joinNotes (const MpeRollNote& x, const MpeRollNote& y, bool spline, double grid, MpeRollNote& out)
{
    return joinMpeRollNotes (x, y, spline, grid, out);
}

//==============================================================================
// construction, the list, the snapshot
//==============================================================================
MpePianoRollModule::MpePianoRollModule()
{
    published = std::make_shared<const Snapshot>();
    audioNotes = published;
}

MpePianoRollModule::~MpePianoRollModule() = default;

const juce::StringArray& MpePianoRollModule::gridChoices()
{
    static const juce::StringArray c { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "Off" };
    return c;
}

double MpePianoRollModule::gridBeats() const
{
    static const double b[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0 };
    return b[juce::jlimit (0, 6, (int) std::lround (getParameter ("grid")))];
}

void MpePianoRollModule::setNotes (NoteList list)
{
    const double maxBeat = kMaxBars * kBeatsPerBar;
    list.erase (std::remove_if (list.begin(), list.end(), [maxBeat] (const MpeRollNote& n)
                { return ! n.isValid() || n.start < -1.0e-9 || n.start >= maxBeat; }), list.end());
    for (auto& n : list)
    {
        n.start = juce::jmax (0.0, n.start);
        for (auto& p : n.points) p.pitch = juce::jlimit (0.0f, 127.0f, p.pitch);
        n.velocity = juce::jlimit (0.05f, 1.0f, n.velocity);
        if (n.expr.size() < 2) n.expr = { { 0.0f, 0.7f }, { 1.0f, 0.7f } };
    }
    std::stable_sort (list.begin(), list.end(), [] (const MpeRollNote& a, const MpeRollNote& b) { return a.start < b.start; });
    if (list.size() > 2048) list.resize (2048);
    notes = std::move (list);
    publish();
    editCounter.fetch_add (1, std::memory_order_relaxed);
}

void MpePianoRollModule::publish()
{
    auto snap = std::make_shared<Snapshot>();
    snap->reserve (notes.size());
    for (const auto& n : notes)
    {
        RenderedNote r;
        r.start = n.start;
        r.length = n.length();
        r.key = juce::jlimit (0, 127, (int) std::lround (n.points.front().pitch));
        r.velocity = n.velocity;
        const int steps = juce::jmax (2, (int) std::ceil (r.length * kTableSteps)) + 1;
        r.pitch.resize ((size_t) steps);
        r.expr.resize ((size_t) steps);
        for (int i = 0; i < steps; ++i)
        {
            const double t = juce::jmin (r.length, (double) i / kTableSteps);
            r.pitch[(size_t) i] = n.pitchAt (t);
            r.expr[(size_t) i] = n.exprAt (t);
        }
        snap->push_back (std::move (r));
    }
    std::shared_ptr<const Snapshot> old;
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        old = std::move (published);
        published = std::move (snap);
    }
    retired.push_back (std::move (old));
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p == nullptr || p.use_count() == 1; }), retired.end());
}

void MpePianoRollModule::pushUndo()
{
    undoStack.push_back (notes);
    if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
}

bool MpePianoRollModule::undo()
{
    if (undoStack.empty()) return false;
    auto prev = std::move (undoStack.back());
    undoStack.pop_back();
    setNotes (std::move (prev));
    return true;
}

//==============================================================================
// playing
//==============================================================================
void MpePianoRollModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void MpePianoRollModule::reset()
{
    numActive = 0;
    wasPlaying = false;
}

void MpePianoRollModule::blockStart()
{
    std::shared_ptr<const Snapshot> latest;
    {
        const juce::SpinLock::ScopedTryLockType sl (snapshotLock);
        if (sl.isLocked() && published != audioNotes) latest = published;
    }
    if (latest != nullptr)
    {
        // (active notes keep their index only into the list they started from)
        audioNotes = std::move (latest);
        listChanged = true;
    }
    tuning = 12.0f * std::log2 (juce::jlimit (420.0f, 460.0f, param (pA4)) / 440.0f);
    useSlide = param (pExpr) > 0.5f;

    if (! rollCommandKnown) { lastRollCommand = hostTransport.rollCommand; rollCommandKnown = true; }
    else if (hostTransport.rollCommand != lastRollCommand)
    {
        lastRollCommand = hostTransport.rollCommand;
        rollCommandPending = true;
        rollCommandPlays = hostTransport.rollCommandPlays;
    }
}

void MpePianoRollModule::releaseAll (MidiDriverEvent* events, int& n, int maxEvents)
{
    while (numActive > 0 && n < maxEvents)
    {
        const auto a = active[(size_t) --numActive];
        MidiDriverEvent e;
        e.note = a.key; e.isOn = false; e.mpe = true;
        events[n++] = e;
    }
}

int MpePianoRollModule::midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents)
{
    int n = 0;
    const bool hostAvailable = hostTransport.hasBpm && hostTransport.bpm > 0.0;
    const double bpm = juce::jlimit (10.0, 999.0, tempoFromLane() ? tempoBpm   // (6.9.10: a lane's tempo wins)
                                                  : param (pSync) > 0.5f && hostAvailable ? hostTransport.bpm : (double) param (pTempo));
    const double loopLen = juce::jlimit (1, kMaxBars, (int) std::lround (param (pBars))) * kBeatsPerBar;

    if (rollCommandPending)
    {
        rollCommandPending = false;
        releaseAll (events, n, maxEvents);
        position = 0.0;
        wasPlaying = false;
        setParameterByIndex (pPlaying, rollCommandPlays ? 1.0f : 0.0f);
    }
    const double seekTo = seekRequest.exchange (-1.0, std::memory_order_acq_rel);
    if (seekTo >= 0.0)
    {
        releaseAll (events, n, maxEvents);
        position = seekTo < loopLen ? seekTo : 0.0;
        wasPlaying = false;      // (picks up the notes under the new position)
    }

    if (param (pPlaying) < 0.5f)
    {
        if (wasPlaying) releaseAll (events, n, maxEvents);
        wasPlaying = false;
        uiPlayhead.store (position, std::memory_order_relaxed);
        return n;
    }
    // the list changed under playing notes: let them go, the new list starts them again
    if (listChanged)
    {
        releaseAll (events, n, maxEvents);
        listChanged = false;
        wasPlaying = false;
    }
    if (position >= loopLen) { releaseAll (events, n, maxEvents); position = 0.0; wasPlaying = false; }

    const double step = bpm / 60.0 / sr;
    const double windowEnd = position + step;
    const auto* list = audioNotes.get();

    auto sampleTable = [] (const std::vector<float>& tab, double t)
    {
        const double x = juce::jlimit (0.0, (double) tab.size() - 1.0, t * kTableSteps);
        const int i = (int) x;
        const double f = x - i;
        const int j = juce::jmin ((int) tab.size() - 1, i + 1);
        return (float) (tab[(size_t) i] + (tab[(size_t) j] - tab[(size_t) i]) * f);
    };

    // ---- ends that are due
    for (int i = 0; i < numActive && n < maxEvents;)
    {
        const auto& note = (*list)[(size_t) active[(size_t) i].index];
        if (juce::jmin (note.start + note.length, loopLen) <= position)
        {
            MidiDriverEvent e;
            e.note = active[(size_t) i].key; e.isOn = false; e.mpe = true;
            events[n++] = e;
            active[(size_t) i] = active[(size_t) --numActive];
        }
        else ++i;
    }

    auto keyFor = [this] (int want)
    {
        // the note's first pitch, or the nearest key no other sounding note holds
        for (int k = 0; k < 24; ++k)
        {
            const int cand = juce::jlimit (0, 127, want + ((k & 1) ? (k + 1) / 2 : -(k / 2)));
            bool taken = false;
            for (int a = 0; a < numActive; ++a) taken = taken || active[(size_t) a].key == cand;
            if (! taken) return cand;
        }
        return want;
    };
    auto startNote = [&] (size_t idx, double t)
    {
        const auto& note = (*list)[idx];
        const int key = keyFor (note.key);
        MidiDriverEvent e;
        e.note = key; e.isOn = true; e.velocity = note.velocity; e.mpe = true;
        e.bend = sampleTable (note.pitch, t) - (float) key + tuning;
        const float x = sampleTable (note.expr, t);
        if (useSlide) e.slide = x; else e.pressure = x;
        events[n++] = e;
        active[(size_t) numActive++] = { (int) idx, key };
    };

    // the first sample playing: notes already under the playhead start now
    if (! wasPlaying)
    {
        wasPlaying = true;
        nextIndex = 0;
        if (list != nullptr)
        {
            for (size_t idx = 0; idx < list->size(); ++idx)
            {
                const auto& note = (*list)[idx];
                if (note.start >= position) break;
                if (note.start + note.length > position && note.start < loopLen && numActive < kMaxActive && n < maxEvents)
                    startNote (idx, position - note.start);
            }
            while (nextIndex < list->size() && (*list)[nextIndex].start < position) ++nextIndex;
        }
    }

    // ---- starts inside this sample (the list is sorted by start)
    if (list != nullptr)
        while (nextIndex < list->size() && (*list)[nextIndex].start < windowEnd)
        {
            const auto& note = (*list)[nextIndex];
            if (note.start >= loopLen) { nextIndex = list->size(); break; }
            if (n + 1 > maxEvents) break;            // the rest on the next sample
            if (numActive < kMaxActive) startNote (nextIndex, 0.0);
            ++nextIndex;
        }

    // ---- the sounding notes' expression, every 8 samples
    if (--exprCountdown <= 0)
    {
        exprCountdown = 8;
        for (int i = 0; i < numActive && n < maxEvents; ++i)
        {
            const auto& note = (*list)[(size_t) active[(size_t) i].index];
            const double t = position - note.start;
            MidiDriverEvent e;
            e.note = active[(size_t) i].key; e.isExpression = true; e.mpe = true;
            e.bend = sampleTable (note.pitch, t) - (float) active[(size_t) i].key + tuning;
            const float x = sampleTable (note.expr, t);
            if (useSlide) e.slide = x; else e.pressure = x;
            events[n++] = e;
        }
    }

    position = windowEnd;
    if (position >= loopLen)
    {
        releaseAll (events, n, maxEvents);
        position -= loopLen;
        if (position >= loopLen) position = 0.0;
        wasPlaying = false;      // (the notes at the loop's start begin on the next sample)
    }
    uiPlayhead.store (position, std::memory_order_relaxed);
    return n;
}

//==============================================================================
// saving
//==============================================================================
juce::String MpePianoRollModule::saveCustomState() const
{
    juce::XmlElement x ("MPEROLL");
    for (const auto& n : notes)
    {
        auto* e = x.createNewChildElement ("N");
        e->setAttribute ("s", n.start);
        e->setAttribute ("v", (double) n.velocity);
        juce::StringArray pts, segs, ex;
        for (auto& p : n.points) pts.add (juce::String (p.t, 6) + ":" + juce::String (p.pitch, 4));
        for (auto& s : n.segments) segs.add (juce::String (s.kind) + ":" + juce::String (s.ct, 6) + ":" + juce::String (s.cp, 4));
        for (auto& v : n.expr) ex.add (juce::String (v.first, 5) + ":" + juce::String (v.second, 4));
        e->setAttribute ("p", pts.joinIntoString (" "));
        e->setAttribute ("g", segs.joinIntoString (" "));
        e->setAttribute ("x", ex.joinIntoString (" "));
    }
    return x.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

void MpePianoRollModule::loadCustomState (const juce::String& state)
{
    auto x = juce::parseXML (state);
    if (x == nullptr || ! x->hasTagName ("MPEROLL")) return;
    NoteList list;
    for (auto* e : x->getChildWithTagNameIterator ("N"))
    {
        MpeRollNote n;
        n.start = e->getDoubleAttribute ("s");
        n.velocity = (float) e->getDoubleAttribute ("v", 0.8);
        for (auto& t : juce::StringArray::fromTokens (e->getStringAttribute ("p"), " ", ""))
            n.points.push_back ({ t.upToFirstOccurrenceOf (":", false, false).getDoubleValue(), t.fromFirstOccurrenceOf (":", false, false).getFloatValue() });
        for (auto& t : juce::StringArray::fromTokens (e->getStringAttribute ("g"), " ", ""))
        {
            auto parts = juce::StringArray::fromTokens (t, ":", "");
            MpeRollSegment s;
            if (parts.size() >= 3) { s.kind = juce::jlimit (0, 2, parts[0].getIntValue()); s.ct = parts[1].getDoubleValue(); s.cp = parts[2].getFloatValue(); }
            n.segments.push_back (s);
        }
        for (auto& t : juce::StringArray::fromTokens (e->getStringAttribute ("x"), " ", ""))
            n.expr.push_back ({ t.upToFirstOccurrenceOf (":", false, false).getFloatValue(), t.fromFirstOccurrenceOf (":", false, false).getFloatValue() });
        n.segments.resize (n.points.empty() ? 0 : n.points.size() - 1);
        list.push_back (std::move (n));
    }
    setNotes (std::move (list));
}

//==============================================================================
// the panel
//==============================================================================
namespace
{
    const juce::Colour kNote (0xffff5fa2), kNoteSel (0xffffb0d4), kGreen (0xff3ddc84), kBack (0xff16181d), kRowDark (0xff1d2027), kRowLight (0xff23262e);

    class MpeRollPanel : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        enum Mode { Draw = 0, Curve, Join, Move, Expr };

        explicit MpeRollPanel (MpePianoRollModule& m) : module (&m)
        {
            auto button = [this] (juce::TextButton& b, const juce::String& tip)
            {
                b.setTooltip (tip);
                addAndMakeVisible (b);
            };
            button (playButton, "Play / pause");
            button (rewindButton, "Back to the start");
            button (barsDown, "Fewer bars");
            button (barsUp, "More bars");
            button (hostButton, "Lock to the host's tempo (its own tempo when there is none)");
            button (undoButton, "Undo the last edit");
            button (deleteButton, "Delete the selected note");
            button (clearButton, "Delete every note");
            button (snapButton, "On Notes: lines start and end on the keys. Between Notes: wherever you put them");
            button (slopeButton, "Slope: Draw makes straight sweeps at that many semitones per beat");
            button (zoomOut, "Show more time");
            button (zoomIn, "Show less time, larger");
            button (rowsOut, "Smaller rows");
            button (rowsIn, "Larger rows");
            const char* modeNames[5] = { "Draw", "Curve", "Join", "Move", "Expr" };
            const char* modeTips[5] = { "Draw a line from where you press to where you let go (a tap: a held note)",
                                        "Bend a line by dragging it, drag a point to move it; double-tap: add a point, straighten, remove a point",
                                        "Tap two notes to make them one, joined by a line or a spline",
                                        "Drag a note; drag its right end for its length; double-tap deletes it",
                                        "Draw the note's green line (its Pressure or Slide): drag along the note, up for more" };
            for (int i = 0; i < 5; ++i)
            {
                modeButtons[(size_t) i].setButtonText (modeNames[i]);
                modeButtons[(size_t) i].setClickingTogglesState (false);
                modeButtons[(size_t) i].onClick = [this, i] { mode = (Mode) i; joinFirst = -1; refresh(); };
                button (modeButtons[(size_t) i], modeTips[i]);
            }
            for (auto* c : { &gridBox, &joinBox, &exprBox }) addAndMakeVisible (*c);
            gridBox.addItemList (MpePianoRollModule::gridChoices(), 1);
            joinBox.addItemList ({ "Join: Line", "Join: Spline" }, 1);
            exprBox.addItemList ({ "Green: Pressure", "Green: Slide" }, 1);
            gridBox.onChange = [this] { set ("grid", (float) (gridBox.getSelectedId() - 1)); };
            joinBox.onChange = [this] { set ("join", (float) (joinBox.getSelectedId() - 1)); };
            exprBox.onChange = [this] { set ("expr", (float) (exprBox.getSelectedId() - 1)); };
            auto slider = [this] (juce::Slider& s, double lo, double hi, double stepSize, const juce::String& suffix, const char* id, const juce::String& tip)
            {
                s.setSliderStyle (juce::Slider::LinearBar);
                s.setRange (lo, hi, stepSize);
                s.setTextValueSuffix (suffix);
                s.setTooltip (tip);
                s.onValueChange = [this, &s, id] { set (id, (float) s.getValue()); };
                addAndMakeVisible (s);
            };
            slider (tempoSlider, 20.0, 300.0, 1.0, " bpm", "tempo", "Its own tempo");
            slider (slopeSlider, 0.25, 48.0, 0.25, " st/beat", "slope", "Draw's sweeps: semitones per beat");
            slider (a4Slider, 420.0, 460.0, 0.1, " Hz", "a4", "A4: the keys the notes snap to, and so the pitch every note plays at");
            playButton.onClick = [this] { if (auto* r = roll()) r->setPlaying (! r->isPlaying()); refresh(); };
            rewindButton.onClick = [this] { if (auto* r = roll()) r->rewind(); };
            barsDown.onClick = [this] { if (auto* r = roll()) set ("bars", (float) juce::jmax (1, r->numBars() - 1)); };
            barsUp.onClick = [this] { if (auto* r = roll()) set ("bars", (float) juce::jmin (MpePianoRollModule::kMaxBars, r->numBars() + 1)); };
            hostButton.onClick = [this] { if (auto* r = roll()) set ("sync", r->getParameter ("sync") > 0.5f ? 0.0f : 1.0f); };
            snapButton.onClick = [this] { if (auto* r = roll()) set ("snap", r->getParameter ("snap") > 0.5f ? 0.0f : 1.0f); };
            slopeButton.onClick = [this] { if (auto* r = roll()) set ("slopeOn", r->getParameter ("slopeOn") > 0.5f ? 0.0f : 1.0f); };
            undoButton.onClick = [this] { if (auto* r = roll()) { r->undo(); selected = -1; repaint(); } };
            deleteButton.onClick = [this] { deleteNote (selected); };
            clearButton.onClick = [this] { if (auto* r = roll()) { r->pushUndo(); r->setNotes ({}); selected = -1; repaint(); } };
            zoomOut.onClick = [this] { timeZoom = juce::jmax (1.0, timeZoom / 1.5); clampView(); repaint(); };
            zoomIn.onClick = [this] { timeZoom = juce::jmin (32.0, timeZoom * 1.5); clampView(); repaint(); };
            rowsOut.onClick = [this] { rowH = juce::jmax (5.0f, rowH / 1.25f); clampView(); repaint(); };
            rowsIn.onClick = [this] { rowH = juce::jmin (40.0f, rowH * 1.25f); clampView(); repaint(); };
            addAndMakeVisible (grip);
            grip.onResize = [this] (juce::Point<int> p) { resizeTo (p); };
            refresh();
            startTimerHz (30);
        }

        //======================================================================
        void resized() override
        {
            auto r = getLocalBounds();
            const int rowHgt = 24, gap = 3;
            // two rows of tools (three when narrow)
            auto layoutRow = [gap] (juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Component*, int>> items)
            {
                int total = 0;
                for (auto& it : items) total += it.second;
                const float scale = juce::jmin (1.0f, (float) (row.getWidth() - gap * ((int) items.size() - 1)) / (float) juce::jmax (1, total));
                int x = row.getX();
                for (auto& it : items)
                {
                    const int w = (int) std::floor ((float) it.second * scale);
                    it.first->setBounds (x, row.getY(), w, row.getHeight());
                    x += w + gap;
                }
            };
            auto row1 = r.removeFromTop (rowHgt); r.removeFromTop (gap);
            auto row2 = r.removeFromTop (rowHgt); r.removeFromTop (gap);
            layoutRow (row1, { { &playButton, 54 }, { &rewindButton, 34 }, { &barsDown, 26 }, { &barsUp, 26 }, { &tempoSlider, 110 },
                               { &hostButton, 48 }, { &gridBox, 66 }, { &undoButton, 50 }, { &deleteButton, 54 }, { &clearButton, 50 },
                               { &zoomOut, 28 }, { &zoomIn, 28 }, { &rowsOut, 28 }, { &rowsIn, 28 } });
            layoutRow (row2, { { &modeButtons[0], 48 }, { &modeButtons[1], 50 }, { &modeButtons[2], 44 }, { &modeButtons[3], 48 }, { &modeButtons[4], 44 },
                               { &snapButton, 96 }, { &slopeButton, 50 }, { &slopeSlider, 96 }, { &joinBox, 96 }, { &exprBox, 116 }, { &a4Slider, 100 } });
            area = r.reduced (0, 0);
            keyStrip = area.removeFromLeft (34);
            ruler = area.removeFromTop (16);
            keyStrip.removeFromTop (16);
            grip.setBounds (getWidth() - 16, getHeight() - 16, 16, 16);
            grip.toFront (false);
            if (! viewInitialised && area.getHeight() > 0)
            {
                viewInitialised = true;
                viewLow = 60.0f - area.getHeight() / rowH * 0.5f;
            }
            clampView();
        }

        void paint (juce::Graphics& g) override
        {
            auto* r = roll();
            if (r == nullptr) return;
            g.setColour (kBack);
            g.fillRect (getLocalBounds().withTop (area.getY() - 16));
            const double loopLen = r->loopLengthBeats();

            // rows: black keys darker, C rows marked
            g.saveState();
            g.reduceClipRegion (area);
            const int lo = (int) std::floor (viewLow), hi = (int) std::ceil (viewLow + area.getHeight() / rowH);
            for (int p = lo; p <= hi; ++p)
            {
                const bool black = juce::MidiMessage::isMidiNoteBlack (((p % 12) + 12) % 12);
                const float y = yOf ((float) p + 0.5f);
                g.setColour (black ? kRowDark : kRowLight);
                g.fillRect ((float) area.getX(), y, (float) area.getWidth(), rowH);
                if (((p % 12) + 12) % 12 == 0)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.12f));
                    g.drawHorizontalLine ((int) (y + rowH), (float) area.getX(), (float) area.getRight());
                }
            }
            // beats and bars, the grid
            const double grid = r->gridBeats();
            for (double b = std::floor (viewStart); b <= viewStart + visibleBeats(); b += 1.0)
            {
                const float x = (float) xOf (b);
                g.setColour (juce::Colours::white.withAlpha (std::fmod (b, MpePianoRollModule::kBeatsPerBar) < 1.0e-6 ? 0.28f : 0.12f));
                g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
            }
            if (grid > 0.0 && pxPerBeat() * grid > 8.0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                for (double b = std::floor (viewStart / grid) * grid; b <= viewStart + visibleBeats(); b += grid)
                    g.drawVerticalLine ((int) xOf (b), (float) area.getY(), (float) area.getBottom());
            }
            // past the loop
            if (xOf (loopLen) < area.getRight())
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect ((float) xOf (loopLen), (float) area.getY(), (float) area.getRight() - (float) xOf (loopLen), (float) area.getHeight());
            }

            // the notes: pink bars along their pitch, the green line inside
            const auto& notes = r->getNotes();
            const float thick = juce::jmax (4.0f, rowH * 0.8f);
            for (int i = 0; i < (int) notes.size(); ++i)
            {
                const auto& n = notes[(size_t) i];
                const bool sel = i == selected || i == joinFirst;
                juce::Path path, green;
                const int steps = juce::jlimit (8, 400, (int) (n.length() * pxPerBeat() / 3.0));
                for (int k = 0; k <= steps; ++k)
                {
                    const double t = n.length() * k / steps;
                    const float x = (float) xOf (n.start + t), y = yOf (n.pitchAt (t));
                    const float gy = y + (0.5f - n.exprAt (t)) * thick * 0.8f;
                    if (k == 0) { path.startNewSubPath (x, y); green.startNewSubPath (x, gy); }
                    else { path.lineTo (x, y); green.lineTo (x, gy); }
                }
                g.setColour ((sel ? kNoteSel : kNote).withAlpha (0.9f));
                g.strokePath (path, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
                g.setColour (kGreen);
                g.strokePath (green, juce::PathStrokeType (1.6f));
                if (mode == Curve || sel)
                {
                    g.setColour (juce::Colours::white);
                    for (auto& p : n.points)
                        g.fillEllipse ((float) xOf (n.start + p.t) - 3.5f, yOf (p.pitch) - 3.5f, 7.0f, 7.0f);
                }
            }
            // the line being drawn
            if (drawing)
            {
                g.setColour (kNote.withAlpha (0.6f));
                g.drawLine ((float) xOf (drawA.t), yOf (drawA.pitch), (float) xOf (drawB.t), yOf (drawB.pitch), thick);
            }
            // Expr: the lane with the selected note's line, large (0 at the bottom, 1 at the top)
            if (mode == Expr)
            {
                const auto lane = exprLane().toFloat();
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillRect (lane);
                g.setColour (kGreen.withAlpha (0.35f));
                g.drawHorizontalLine ((int) lane.getY(), lane.getX(), lane.getRight());
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                const juce::String what = r->getParameter ("expr") > 0.5f ? "Slide" : "Pressure";
                if (selected >= 0 && selected < (int) notes.size())
                {
                    const auto& n = notes[(size_t) selected];
                    juce::Path line;
                    const int steps = juce::jlimit (8, 600, (int) (n.length() * pxPerBeat() / 2.0));
                    const float top = lane.getY() + 4.0f, h = lane.getHeight() - 8.0f;
                    for (int k = 0; k <= steps; ++k)
                    {
                        const double t = n.length() * k / steps;
                        const float x = (float) xOf (n.start + t), y = top + (1.0f - n.exprAt (t)) * h;
                        if (k == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
                    }
                    g.setColour (kNote.withAlpha (0.18f));
                    g.fillRect ((float) xOf (n.start), lane.getY(), (float) (n.length() * pxPerBeat()), lane.getHeight());
                    g.setColour (kGreen);
                    g.strokePath (line, juce::PathStrokeType (2.0f));
                    g.setColour (juce::Colours::white.withAlpha (0.6f));
                    g.drawText (what + ": draw here", lane.reduced (6, 2).toNearestInt(), juce::Justification::topLeft);
                }
                else
                    g.drawText ("Tap a note, then draw its " + what + " here (or on the note itself, up for more)",
                                lane.reduced (6, 2).toNearestInt(), juce::Justification::centredLeft);
            }

            // the playhead
            const float px = (float) xOf (r->getPlayheadBeats());
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawVerticalLine ((int) px, (float) area.getY(), (float) area.getBottom());
            g.restoreState();

            // the ruler and the keys
            g.setColour (juce::Colour (0xff101216));
            g.fillRect (ruler);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            for (int bar = (int) std::floor (viewStart / 4.0); bar * 4.0 <= viewStart + visibleBeats(); ++bar)
                if (bar >= 0) g.drawText (juce::String (bar + 1), (int) xOf (bar * 4.0) + 3, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft);
            g.saveState();
            g.reduceClipRegion (keyStrip);
            for (int p = lo; p <= hi; ++p)
            {
                const bool black = juce::MidiMessage::isMidiNoteBlack (((p % 12) + 12) % 12);
                const float y = yOf ((float) p + 0.5f);
                g.setColour (black ? juce::Colour (0xff2a2d35) : juce::Colour (0xffd8d8dc));
                g.fillRect ((float) keyStrip.getX(), y, (float) keyStrip.getWidth() - 1.0f, rowH - 0.5f);
                if (((p % 12) + 12) % 12 == 0 && rowH >= 7.0f)
                {
                    g.setColour (juce::Colours::black);
                    g.setFont (juce::Font (juce::FontOptions (juce::jmin (10.0f, rowH))));
                    g.drawText (juce::MidiMessage::getMidiNoteName (p, true, true, 4), keyStrip.getX() + 2, (int) y, keyStrip.getWidth() - 3, (int) rowH, juce::Justification::centredLeft);
                }
            }
            g.restoreState();
        }

        //=== gestures ==========================================================
        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* r = roll();
            if (r == nullptr) return;
            const auto pos = e.position;
            dragKind = DragKind::None;
            if (keyStrip.contains (pos.toInt())) { dragKind = DragKind::ScrollPitch; pressLow = viewLow; pressPos = pos; return; }
            if (ruler.contains (pos.toInt())) { dragKind = DragKind::ScrollTime; pressStart = viewStart; pressPos = pos; seekTo (snapTime (beatOf (pos.x))); return; }
            if (! area.contains (pos.toInt())) return;
            pressPos = pos;
            const double beat = beatOf (pos.x);
            const float pitch = pitchOf (pos.y);

            if (mode == Draw)
            {
                drawing = true;
                drawA = { snapTime (beat), snapPitch (pitch) };
                drawB = drawA;
                dragKind = DragKind::Drawing;
                repaint();
                return;
            }

            if (mode == Expr)
            {
                // the lane at the bottom: the selected note's (or the note at that time's) line, drawn
                // at the height of the finger; on a note: that note, drawn from where it was
                const bool inLane = exprLane().contains (pos.toInt());
                int target = inLane ? selected : noteAt (pos);
                if (inLane && target < 0)
                {
                    const auto& list = r->getNotes();
                    for (int i = 0; i < (int) list.size() && target < 0; ++i)
                        if (beat >= list[(size_t) i].start && beat <= list[(size_t) i].end()) target = i;
                }
                if (target < 0) { selected = -1; repaint(); return; }
                selected = target;
                original = r->getNotes()[(size_t) target];
                editStart = true;
                dragKind = inLane ? DragKind::DrawExprLane : DragKind::DrawExpr;
                exprAtPress = original.exprAt (beat - original.start);
                lastExprU = -1.0f;
                applyDrag (pos);
                return;
            }

            const int hit = noteAt (pos);
            if (mode == Join)
            {
                if (hit < 0) { joinFirst = -1; repaint(); return; }
                if (joinFirst < 0 || joinFirst == hit) { joinFirst = hit; selected = hit; repaint(); return; }
                const auto& list = r->getNotes();
                MpeRollNote joined;
                if (MpePianoRollModule::joinNotes (list[(size_t) joinFirst], list[(size_t) hit], r->getParameter ("join") > 0.5f,
                                                   r->gridBeats() > 0.0 ? r->gridBeats() : 0.25, joined))
                {
                    r->pushUndo();
                    auto copy = list;
                    const int a = juce::jmax (joinFirst, hit), b = juce::jmin (joinFirst, hit);
                    copy.erase (copy.begin() + a);
                    copy.erase (copy.begin() + b);
                    copy.push_back (joined);
                    r->setNotes (std::move (copy));
                    selected = indexOfStart (joined.start, joined.points.front().pitch);
                }
                joinFirst = -1;
                repaint();
                return;
            }

            if (hit < 0) { selected = -1; repaint(); return; }
            selected = hit;
            const auto& n = r->getNotes()[(size_t) hit];
            original = n;
            editStart = true;

            if (mode == Move)
            {
                const float endX = (float) xOf (n.end());
                dragKind = std::abs (pos.x - endX) < 12.0f ? DragKind::Resize : DragKind::MoveNote;
            }
            else if (mode == Curve)
            {
                // a point, else the segment under the finger
                dragPoint = -1;
                for (size_t k = 0; k < n.points.size(); ++k)
                    if (pos.getDistanceFrom ({ (float) xOf (n.start + n.points[k].t), yOf (n.points[k].pitch) }) < 12.0f) dragPoint = (int) k;
                dragSegment = -1;
                if (dragPoint < 0)
                {
                    const double t = beat - n.start;
                    for (size_t k = 0; k + 1 < n.points.size(); ++k)
                        if (t >= n.points[k].t && t <= n.points[k + 1].t) dragSegment = (int) k;
                }
                dragKind = dragPoint >= 0 ? DragKind::MovePoint : (dragSegment >= 0 ? DragKind::BendSegment : DragKind::None);
            }
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* r = roll();
            if (r == nullptr) return;
            const auto pos = e.position;
            switch (dragKind)
            {
                case DragKind::ScrollPitch: viewLow = pressLow + (pos.y - pressPos.y) / rowH; clampView(); repaint(); return;
                case DragKind::ScrollTime:  viewStart = pressStart - (pos.x - pressPos.x) / pxPerBeat(); clampView(); repaint(); return;
                case DragKind::Drawing:
                {
                    double t = snapTime (beatOf (pos.x));
                    float p = snapPitch (pitchOf (pos.y));
                    if (r->getParameter ("slopeOn") > 0.5f)
                    {
                        // a straight sweep at the slope, up or down as dragged
                        const float slope = r->getParameter ("slope");
                        const float dir = pitchOf (pos.y) >= drawA.pitch ? 1.0f : -1.0f;
                        p = drawA.pitch + dir * slope * (float) std::abs (t - drawA.t);
                    }
                    drawB = { t, p };
                    repaint();
                    return;
                }
                default: break;
            }
            if (dragKind != DragKind::None && selected >= 0) applyDrag (pos);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            auto* r = roll();
            if (r == nullptr) return;
            if (dragKind == DragKind::Drawing)
            {
                drawing = false;
                auto a = drawA, b = drawB;
                if (b.t < a.t) std::swap (a, b);      // a line drawn right to left runs left to right
                double len = b.t - a.t;
                const double grid = r->gridBeats() > 0.0 ? r->gridBeats() : 0.25;
                if (len < 1.0e-3) { len = grid; b = { a.t + len, a.pitch }; }   // a tap: a held note
                MpeRollNote n;
                n.start = juce::jmax (0.0, a.t);
                n.points = { { 0.0, a.pitch }, { len, b.pitch } };
                n.segments = { MpeRollSegment() };
                n.expr = { { 0.0f, 0.7f }, { 1.0f, 0.7f } };
                n.velocity = juce::jlimit (0.05f, 1.0f, r->getParameter ("newVel") / 127.0f);
                r->pushUndo();
                auto list = r->getNotes();
                list.push_back (n);
                r->setNotes (std::move (list));
                selected = indexOfStart (n.start, n.points.front().pitch);
            }
            dragKind = DragKind::None;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            auto* r = roll();
            if (r == nullptr || ! area.contains (e.position.toInt())) return;
            const int hit = noteAt (e.position);
            if (hit < 0) return;
            if (mode == Move) { deleteNote (hit); return; }
            if (mode != Curve) return;
            auto n = r->getNotes()[(size_t) hit];
            const auto pos = e.position;
            // a point (not an end): removed
            for (size_t k = 1; k + 1 < n.points.size(); ++k)
                if (pos.getDistanceFrom ({ (float) xOf (n.start + n.points[k].t), yOf (n.points[k].pitch) }) < 12.0f)
                {
                    n.points.erase (n.points.begin() + (long) k);
                    n.segments.erase (n.segments.begin() + (long) k);
                    replaceNote (hit, n, true);
                    return;
                }
            const double t = beatOf (pos.x) - n.start;
            for (size_t k = 0; k + 1 < n.points.size(); ++k)
                if (t > n.points[k].t && t < n.points[k + 1].t)
                {
                    if (n.segments[k].kind != MpeRollSegment::Line) n.segments[k].kind = MpeRollSegment::Line;   // straighten
                    else
                    {
                        // a new point on the line
                        n.points.insert (n.points.begin() + (long) k + 1, { t, n.pitchAt (t) });
                        n.segments.insert (n.segments.begin() + (long) k + 1, MpeRollSegment());
                    }
                    replaceNote (hit, n, true);
                    return;
                }
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isCommandDown() || e.mods.isCtrlDown()) rowH = juce::jlimit (5.0f, 40.0f, rowH * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f));
            else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY)) viewStart -= (w.deltaX != 0.0f ? w.deltaX : w.deltaY) * visibleBeats() * 0.3;
            else viewLow += w.deltaY * 8.0f;
            clampView();
            repaint();
        }

    private:
        enum class DragKind { None, Drawing, MoveNote, Resize, MovePoint, BendSegment, DrawExpr, DrawExprLane, ScrollPitch, ScrollTime };

        // Expr mode: the selected note's expression, large, along the bottom of the grid
        juce::Rectangle<int> exprLane() const
        {
            const int h = juce::jlimit (40, 110, area.getHeight() / 3);
            return area.withTop (area.getBottom() - h);
        }

        MpePianoRollModule* roll() const { return dynamic_cast<MpePianoRollModule*> (module.get()); }

        void set (const char* id, float v)
        {
            if (syncing) return;
            if (auto* r = roll())
            {
                r->setParameter (id, v);
                if (paramValuesChangedByContent) paramValuesChangedByContent();
                refresh();
            }
        }

        void refresh()
        {
            auto* r = roll();
            if (r == nullptr) return;
            const juce::ScopedValueSetter<bool> svs (syncing, true);
            playButton.setButtonText (r->isPlaying() ? "Pause" : "Play");
            barsDown.setButtonText ("-");
            barsUp.setButtonText ("+" );
            hostButton.setButtonText (r->getParameter ("sync") > 0.5f ? "Host" : "Own");
            snapButton.setButtonText (r->getParameter ("snap") > 0.5f ? "Between Notes" : "On Notes");
            slopeButton.setButtonText (r->getParameter ("slopeOn") > 0.5f ? "Slope" : "Free");
            slopeButton.setToggleState (r->getParameter ("slopeOn") > 0.5f, juce::dontSendNotification);
            if (! tempoSlider.isMouseButtonDown()) tempoSlider.setValue (r->getParameter ("tempo"), juce::dontSendNotification);
            if (! slopeSlider.isMouseButtonDown()) slopeSlider.setValue (r->getParameter ("slope"), juce::dontSendNotification);
            if (! a4Slider.isMouseButtonDown()) a4Slider.setValue (r->getParameter ("a4"), juce::dontSendNotification);
            gridBox.setSelectedId ((int) std::lround (r->getParameter ("grid")) + 1, juce::dontSendNotification);
            joinBox.setSelectedId ((int) std::lround (r->getParameter ("join")) + 1, juce::dontSendNotification);
            exprBox.setSelectedId ((int) std::lround (r->getParameter ("expr")) + 1, juce::dontSendNotification);
            for (int i = 0; i < 5; ++i)
                modeButtons[(size_t) i].setToggleState (i == (int) mode, juce::dontSendNotification);
            undoButton.setEnabled (r->canUndo());
            deleteButton.setEnabled (selected >= 0);
            tempoSlider.setEnabled (r->getParameter ("sync") < 0.5f);
            repaint();
        }

        void timerCallback() override
        {
            auto* r = roll();
            if (r == nullptr) return;
            const int ec = r->getEditCounter();
            if (ec != lastEdit) { lastEdit = ec; if (selected >= (int) r->getNotes().size()) selected = -1; refresh(); }
            const double ph = r->getPlayheadBeats();
            if (std::abs (ph - lastPlayhead) > 1.0e-6 || r->isPlaying() != lastPlaying) { lastPlayhead = ph; lastPlaying = r->isPlaying(); refresh(); }
        }

        // geometry
        double visibleBeats() const { auto* r = roll(); return (r != nullptr ? r->loopLengthBeats() : 16.0) / timeZoom; }
        double pxPerBeat() const { return area.getWidth() / juce::jmax (0.25, visibleBeats()); }
        double xOf (double beat) const { return area.getX() + (beat - viewStart) * pxPerBeat(); }
        double beatOf (float x) const { return viewStart + (x - area.getX()) / pxPerBeat(); }
        float yOf (float pitch) const { return (float) area.getBottom() - (pitch - viewLow + 0.5f) * rowH; }
        float pitchOf (float y) const { return viewLow - 0.5f + ((float) area.getBottom() - y) / rowH; }

        void clampView()
        {
            auto* r = roll();
            const double loopLen = r != nullptr ? r->loopLengthBeats() : 16.0;
            viewStart = juce::jlimit (0.0, juce::jmax (0.0, loopLen - visibleBeats()), viewStart);
            const float rows = area.getHeight() / rowH;
            viewLow = juce::jlimit (0.0f, juce::jmax (0.0f, 128.0f - rows), viewLow);
        }

        double snapTime (double beat) const
        {
            auto* r = roll();
            const double g = r != nullptr ? r->gridBeats() : 0.25;
            beat = juce::jmax (0.0, beat);
            return g > 0.0 ? std::round (beat / g) * g : beat;
        }
        float snapPitch (float p) const
        {
            auto* r = roll();
            const bool onNotes = r == nullptr || r->getParameter ("snap") < 0.5f;
            return juce::jlimit (0.0f, 127.0f, onNotes ? std::round (p) : p);
        }

        int noteAt (juce::Point<float> pos) const
        {
            auto* r = roll();
            if (r == nullptr) return -1;
            const auto& notes = r->getNotes();
            const float tol = juce::jmax (6.0f, rowH * 0.6f);
            int best = -1;
            float bestD = tol;
            for (int i = (int) notes.size() - 1; i >= 0; --i)
            {
                const auto& n = notes[(size_t) i];
                const double t = beatOf (pos.x) - n.start;
                if (t < -0.05 * visibleBeats() / 16.0 || t > n.length() + 0.05 * visibleBeats() / 16.0) continue;
                const float d = std::abs (yOf (n.pitchAt (juce::jlimit (0.0, n.length(), t))) - pos.y);
                if (d < bestD) { bestD = d; best = i; }
            }
            return best;
        }

        int indexOfStart (double start, float pitch) const
        {
            auto* r = roll();
            if (r == nullptr) return -1;
            const auto& notes = r->getNotes();
            for (int i = 0; i < (int) notes.size(); ++i)
                if (std::abs (notes[(size_t) i].start - start) < 1.0e-9 && std::abs (notes[(size_t) i].points.front().pitch - pitch) < 1.0e-4f) return i;
            return -1;
        }

        void replaceNote (int index, const MpeRollNote& n, bool undoStep)
        {
            auto* r = roll();
            if (r == nullptr || index < 0 || index >= (int) r->getNotes().size()) return;
            if (undoStep) r->pushUndo();
            auto list = r->getNotes();
            list[(size_t) index] = n;
            r->setNotes (std::move (list));
            selected = indexOfStart (n.start, n.points.front().pitch);
            repaint();
        }

        void deleteNote (int index)
        {
            auto* r = roll();
            if (r == nullptr || index < 0 || index >= (int) r->getNotes().size()) return;
            r->pushUndo();
            auto list = r->getNotes();
            list.erase (list.begin() + index);
            r->setNotes (std::move (list));
            selected = -1;
            refresh();
        }

        // the drag of the mode, applied to the note it started on (the original's copy)
        void applyDrag (juce::Point<float> pos)
        {
            auto* r = roll();
            if (r == nullptr || selected < 0) return;
            auto n = original;
            const double dBeat = beatOf (pos.x) - beatOf (pressPos.x);
            const float dPitch = pitchOf (pos.y) - pitchOf (pressPos.y);
            const bool onNotes = r->getParameter ("snap") < 0.5f;
            const double grid = r->gridBeats();
            switch (dragKind)
            {
                case DragKind::MoveNote:
                {
                    n.start = juce::jmax (0.0, grid > 0.0 ? std::round ((original.start + dBeat) / grid) * grid : original.start + dBeat);
                    const float shift = onNotes ? std::round (dPitch) : dPitch;
                    for (auto& p : n.points) p.pitch = juce::jlimit (0.0f, 127.0f, p.pitch + shift);
                    for (auto& s : n.segments) s.cp += shift;
                    break;
                }
                case DragKind::Resize:
                {
                    double newLen = original.length() + dBeat;
                    if (grid > 0.0) newLen = std::round ((original.start + newLen) / grid) * grid - original.start;
                    newLen = juce::jmax (grid > 0.0 ? grid : 0.05, newLen);
                    const double k = newLen / original.length();
                    for (auto& p : n.points) p.t *= k;
                    for (auto& s : n.segments) s.ct *= k;
                    break;
                }
                case DragKind::MovePoint:
                {
                    auto& p = n.points[(size_t) dragPoint];
                    const auto& op = original.points[(size_t) dragPoint];
                    p.pitch = juce::jlimit (0.0f, 127.0f, onNotes ? std::round (op.pitch + dPitch) : op.pitch + dPitch);
                    if (dragPoint == 0)
                    {
                        // the first point: the note's start moves, the rest stays where it is
                        double newStart = original.start + dBeat;
                        if (grid > 0.0) newStart = std::round (newStart / grid) * grid;
                        newStart = juce::jlimit (0.0, original.start + original.points[1].t - 0.01, newStart);
                        const double d = newStart - original.start;
                        n.start = newStart;
                        for (size_t k = 1; k < n.points.size(); ++k) n.points[k].t = original.points[k].t - d;
                        for (size_t k = 0; k < n.segments.size(); ++k) n.segments[k].ct = original.segments[k].ct - d;
                        n.points[0].t = 0.0;
                    }
                    else
                    {
                        double t = op.t + dBeat;
                        if (grid > 0.0) t = std::round ((original.start + t) / grid) * grid - original.start;
                        const double lo = original.points[(size_t) dragPoint - 1].t + 0.01;
                        const double hi = dragPoint + 1 < (int) original.points.size() ? original.points[(size_t) dragPoint + 1].t - 0.01 : 1.0e9;
                        p.t = juce::jlimit (lo, hi, t);
                    }
                    break;
                }
                case DragKind::BendSegment:
                {
                    // the line passes under the finger: its control point from there
                    const auto& a = n.points[(size_t) dragSegment];
                    const auto& b = n.points[(size_t) dragSegment + 1];
                    const double mt = juce::jlimit (a.t, b.t, beatOf (pos.x) - n.start);
                    const float mp = pitchOf (pos.y);
                    auto& s = n.segments[(size_t) dragSegment];
                    s.kind = MpeRollSegment::Bend;
                    s.ct = juce::jlimit (a.t, b.t, 2.0 * mt - 0.5 * (a.t + b.t));
                    s.cp = 2.0f * mp - 0.5f * (a.pitch + b.pitch);
                    break;
                }
                case DragKind::DrawExpr:
                case DragKind::DrawExprLane:
                {
                    // the value written where the finger is along the note: in the lane its height
                    // there, on the note itself up or down from where it was
                    const double len = n.length();
                    const float u = (float) juce::jlimit (0.0, 1.0, (beatOf (pos.x) - n.start) / len);
                    const auto lane = exprLane().toFloat();
                    const float v = dragKind == DragKind::DrawExprLane
                                        ? juce::jlimit (0.0f, 1.0f, (lane.getBottom() - 4.0f - pos.y) / juce::jmax (8.0f, lane.getHeight() - 8.0f))
                                        : juce::jlimit (0.0f, 1.0f, exprAtPress + (pressPos.y - pos.y) / 50.0f);
                    n = r->getNotes()[(size_t) selected];      // (expression accumulates over the drag)
                    auto& ex = n.expr;
                    // a straight line from the last place the finger wrote (a fast drag leaves no gaps)
                    const float u0 = lastExprU < 0.0f ? u : lastExprU, v0 = lastExprU < 0.0f ? v : lastExprV;
                    const float lo = juce::jmin (u0, u) - 0.004f, hi = juce::jmax (u0, u) + 0.004f;
                    ex.erase (std::remove_if (ex.begin(), ex.end(), [lo, hi] (const auto& p)
                              { return p.first > lo && p.first < hi && p.first > 0.0f && p.first < 1.0f; }), ex.end());
                    if (u0 != u) ex.push_back ({ u0, v0 });
                    ex.push_back ({ u, v });
                    if (juce::jmin (u0, u) < 0.01f) ex.front().second = u < u0 ? v : v0;
                    if (juce::jmax (u0, u) > 0.99f) ex.back().second = u > u0 ? v : v0;
                    std::sort (ex.begin(), ex.end(), [] (const auto& p, const auto& q) { return p.first < q.first; });
                    if (ex.front().first > 0.0f) ex.insert (ex.begin(), { 0.0f, ex.front().second });
                    if (ex.back().first < 1.0f) ex.push_back ({ 1.0f, ex.back().second });
                    lastExprU = u; lastExprV = v;
                    break;
                }
                default: return;
            }
            auto* rr = roll();
            if (editStart) { rr->pushUndo(); editStart = false; }
            auto list = rr->getNotes();
            list[(size_t) selected] = n;
            rr->setNotes (std::move (list));
            selected = indexOfStart (n.start, n.points.front().pitch);
            if (selected < 0) selected = 0;
            repaint();
        }

        void seekTo (double beat) { if (auto* r = roll()) r->seek (beat); }

        void resizeTo (juce::Point<int> bottomRight)
        {
            auto* r = roll();
            if (r == nullptr) return;
            const int newW = juce::jlimit (MpePianoRollModule::kMinWidth, MpePianoRollModule::kMaxWidth, bottomRight.x + 20);
            const int newH = juce::jlimit (MpePianoRollModule::kMinHeight, MpePianoRollModule::kMaxHeight, bottomRight.y);
            if (newW == r->preferredModuleWidth() && newH == r->extraContentHeight()) return;
            r->setParameter ("viewW", (float) newW);
            r->setParameter ("viewH", (float) newH);
            if (paramsChangedByContent) paramsChangedByContent();
        }

        struct ResizeGrip : public juce::Component
        {
            std::function<void (juce::Point<int>)> onResize;
            ResizeGrip() { setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor); }
            void paint (juce::Graphics& g) override
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                for (int i = 0; i < 3; ++i)
                {
                    const float o = 4.0f + 4.0f * (float) i;
                    g.drawLine ((float) getWidth() - o, (float) getHeight() - 2.0f, (float) getWidth() - 2.0f, (float) getHeight() - o, 1.3f);
                }
            }
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (auto* p = getParentComponent()) grabOffset = getBounds().getBottomRight() - e.getEventRelativeTo (p).getPosition();
            }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (auto* p = getParentComponent())
                    if (onResize) onResize (e.getEventRelativeTo (p).getPosition() + grabOffset);
            }
            juce::Point<int> grabOffset;
        };

        juce::WeakReference<SynthModule> module;
        juce::TextButton playButton { "Play" }, rewindButton { "|<" }, barsDown { "-" }, barsUp { "+" }, hostButton { "Own" },
                         undoButton { "Undo" }, deleteButton { "Delete" }, clearButton { "Clear" }, snapButton { "On Notes" },
                         slopeButton { "Free" }, zoomOut { "t-" }, zoomIn { "t+" }, rowsOut { "v-" }, rowsIn { "v+" };
        std::array<juce::TextButton, 5> modeButtons;
        juce::ComboBox gridBox, joinBox, exprBox;
        juce::Slider tempoSlider, slopeSlider, a4Slider;
        ResizeGrip grip;

        juce::Rectangle<int> area, keyStrip, ruler;
        Mode mode { Draw };
        double viewStart { 0.0 }, timeZoom { 1.0 };
        float viewLow { 48.0f }, rowH { 11.0f };
        bool viewInitialised { false }, syncing { false };

        DragKind dragKind { DragKind::None };
        juce::Point<float> pressPos;
        float pressLow { 0.0f }, exprAtPress { 0.7f }, lastExprU { -1.0f }, lastExprV { 0.7f };
        double pressStart { 0.0 };
        bool drawing { false }, editStart { false };
        MpeRollPoint drawA, drawB;
        int selected { -1 }, joinFirst { -1 }, dragPoint { -1 }, dragSegment { -1 };
        MpeRollNote original;
        int lastEdit { -1 };
        double lastPlayhead { -1.0 };
        bool lastPlaying { false };
    };
}

std::unique_ptr<juce::Component> MpePianoRollModule::createExtraContentComponent()
{
    return std::make_unique<MpeRollPanel> (*this);
}

//==============================================================================
static ModuleDescriptor mpePianoRollDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.mperoll";
    d.displayName = "MPE Piano Roll";
    d.description =
        "A looping roll of MPE notes you draw: every note is a line across the keys in any direction - a held pitch, a "
        "glide, a sweep - with its own Pressure or Slide as the green line inside the pink bar. Draw a line (On Notes "
        "snaps its ends to the keys, Between Notes leaves them free; Slope draws straight sweeps at a set number of "
        "semitones per beat), bend it with Curve, make two notes one with Join (a line or a spline between them; "
        "overlapping, the first glides into the second; starting together, the shorter sweeps into the longer), drag it "
        "with Move, and draw its green line with Expr. A4 (420 - 460 Hz) tunes the keys. Its MPE Out plays every note "
        "with its own bend and expression, and connects only to what can play MPE. Modules: Oscillator, Analog, "
        "Wavefold, Formant, Phase Distort, Additive, Unison, Warp, both Dual VCOs, PWM, Chord Table, Wavetable, "
        "Transwave, Sampler, Multi Sample, Granulator, Spectral Resynthesis, Resynth Piano, Piano, Felt Piano, Mid "
        "Piano, Hard Piano, Grand, Bowed, Wind, Strings, Cello, Clarinet, Acoustic Guitar, Electric Guitar and the "
        "VirtualDX7 Operator. Plugins (each note its own bend, and its own pressure): Vita; Massif (pressure: the "
        "Aftertouch mod source); VirtualDX7 (its own FM engine only, not with a ROM loaded, MIDI Channel on Omni; "
        "pressure: wherever the patch assigns aftertouch); AlphaBeta, both engines (pressure: the Aftertouch source); "
        "Aqua Vibrio, both engines (pressure: the Chan Pressure source); AM-20 (pressure: the Pressure ext source); "
        "AliasSynth (pressure adds or takes away FM); Aquanova (pressure: its aftertouch targets); Phizmo "
        "(pressure: the Pressure mod sources and the vibrato); Reflex (pressure: the vibrato); Additive Keys, FM12 "
        "and Tidebreeze (pressure: brightness - Additive Keys' filter, FM12's modulation depth, Tidebreeze's "
        "filters - left as the patch has it at the roll's resting pressure). An arpeggiator that is on plays its "
        "notes without MPE.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 26;
    d.sockets = { midiOut ("midiOut", "MPE Out"), tempoIn() };   // (6.9.10: tempo per lane)
    d.params = {
        makeRotary ("tempo", "Tempo", 20.0f, 300.0f, 120.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("bars", "Bars", 1.0f, (float) MpePianoRollModule::kMaxBars, 4.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeCombo ("sync", "Clock", { "Own Tempo", "Host Tempo" }, 0, 9, 3).hide().noMod(),
        makeRotary ("playing", "Playing", 0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("grid", "Grid", 0.0f, 6.0f, 2.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeCombo ("snap", "Pitch", { "On Notes", "Between Notes" }, 0, 9, 2).hide().noMod(),
        makeRotary ("a4", "A4", 420.0f, 460.0f, 440.0f, 9, "Hz", false, 0.1f).hide().noMod(),
        makeCombo ("slopeOn", "Slope", { "Free", "Slope" }, 0, 9, 2).hide().noMod(),
        makeRotary ("slope", "Slope", 0.25f, 48.0f, 12.0f, 9, "st/beat", false, 0.25f).hide().noMod(),
        makeCombo ("join", "Join", { "Line", "Spline" }, 1, 9, 2).hide().noMod(),
        makeCombo ("expr", "Green Line", { "Pressure", "Slide" }, 0, 9, 2).hide().noMod(),
        makeRotary ("newVel", "Velocity", 1.0f, 127.0f, 100.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("viewW", "Width", (float) MpePianoRollModule::kMinWidth, (float) MpePianoRollModule::kMaxWidth,
                    (float) MpePianoRollModule::kDefaultWidth, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("viewH", "Height", (float) MpePianoRollModule::kMinHeight, (float) MpePianoRollModule::kMaxHeight,
                    (float) MpePianoRollModule::kDefaultHeight, 9, {}, false, 1.0f).hide().noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MpePianoRollModule, mpePianoRollDescriptor)
