#pragma once

#include "../../Core/ModuleCore.h"
#include "MpeRollModel.h"

// MPE Piano Roll - a looping roll of MPE notes you draw: a note is a line in
// any direction across the keys (a glide, a sweep, a held pitch), its pitch
// running along it, with its own expression (Pressure or Slide) as a green
// line inside the pink bar. Its MPE Out plays every note with its own bend,
// pressure and Slide into a module that plays MPE (the voice generators, the
// physically modelled instruments, Vita) - it does not connect to anything
// else.
//
// Modes (the buttons on top):
//   Draw    a line from where you press to where you let go. On Notes snaps
//           its ends to the keys, Between Notes leaves them where they are;
//           Slope draws a straight sweep at that many semitones per beat
//           (up or down, as you drag). A tap draws a held note one grid step long.
//   Curve   drag a line between two points to bend it, drag a point to move
//           it; double-tap a straight line to add a point there, a bent one
//           to straighten it, a point to remove it.
//   Join    tap two notes: they become one, joined by a line or a spline.
//           Apart: the gap is bridged. Overlapping: the first is cut where
//           the second begins and glides into it. Starting together: the
//           shorter sweeps into the longer. Starting and ending together:
//           nothing happens.
//   Move    drag a note (in time, and in pitch: by keys with On Notes);
//           drag its right end to make it longer or shorter; double-tap deletes it.
//   Expr    draw the note's green line: drag along a note, up for more.
//
// A4 tunes the keys the notes snap to (420 - 460 Hz): every note plays that
// much higher or lower. Threading as in the Piano Roll: the note list belongs
// to the message thread, each edit publishes an immutable rendered snapshot
// the audio thread latches once per block.
class MpePianoRollModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pTempo = 0, pBars, pSync, pPlaying, pGrid, pSnap, pA4, pSlopeOn, pSlope, pJoin, pExpr, pVelocity, pViewW, pViewH };

    static constexpr int kMaxBars = 64;
    static constexpr double kBeatsPerBar = 4.0;
    static constexpr int kTableSteps = 128;      // pitch / expression samples per beat in the snapshot
    static constexpr int kMaxActive = 32;
    static constexpr int kDefaultWidth = 760, kMinWidth = 420, kMaxWidth = 1800;
    static constexpr int kDefaultHeight = 380, kMinHeight = 260, kMaxHeight = 1200;

    using NoteList = std::vector<MpeRollNote>;

    MpePianoRollModule();
    ~MpePianoRollModule() override;

    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }
    bool runsWithoutListeners() const override { return true; }
    bool emitsMpeMidi() const override { return true; }

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;
    int midiDriverAdvanceEvents (double sr, aquanode::MidiDriverEvent* events, int maxEvents) override;

    //=== editing (message thread) ===========================================
    const NoteList& getNotes() const { return notes; }
    void setNotes (NoteList newNotes);
    void pushUndo();
    bool undo();
    bool canUndo() const { return ! undoStack.empty(); }
    int getEditCounter() const { return editCounter.load (std::memory_order_relaxed); }

    // the Join rules: false when nothing happens (they start and end together)
    static bool joinNotes (const MpeRollNote& a, const MpeRollNote& b, bool spline, double grid, MpeRollNote& out);

    int numBars() const { return juce::jlimit (1, kMaxBars, (int) std::lround (getParameter ("bars"))); }
    double loopLengthBeats() const { return numBars() * kBeatsPerBar; }
    double gridBeats() const;
    static const juce::StringArray& gridChoices();
    float tuningOffset() const { return 12.0f * std::log2 (juce::jlimit (420.0f, 460.0f, getParameter ("a4")) / 440.0f); }

    bool isPlaying() const { return getParameter ("playing") > 0.5f; }
    void setPlaying (bool on) { setParameter ("playing", on ? 1.0f : 0.0f); }
    void rewind() { seek (0.0); }
    void seek (double beat) { seekRequest.store (juce::jmax (0.0, beat), std::memory_order_release); }
    double getPlayheadBeats() const { return uiPlayhead.load (std::memory_order_relaxed); }

    //=== UI / state ==========================================================
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return juce::jlimit (kMinHeight, kMaxHeight, (int) std::lround (getParameter ("viewH"))); }
    int preferredModuleWidth() const override { return juce::jlimit (kMinWidth, kMaxWidth, (int) std::lround (getParameter ("viewW"))); }
    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

private:
    // what the audio thread plays: each note rendered to tables
    struct RenderedNote
    {
        double start { 0.0 }, length { 0.0 };
        int key { 60 };
        float velocity { 0.8f };
        std::vector<float> pitch, expr;      // kTableSteps per beat, + 1
    };
    using Snapshot = std::vector<RenderedNote>;

    void publish();
    void releaseAll (aquanode::MidiDriverEvent* events, int& n, int maxEvents);

    NoteList notes;
    std::vector<NoteList> undoStack;
    std::vector<std::shared_ptr<const Snapshot>> retired;
    std::atomic<int> editCounter { 0 };

    mutable juce::SpinLock snapshotLock;
    std::shared_ptr<const Snapshot> published;
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<double> uiPlayhead { 0.0 };

    // audio thread
    std::shared_ptr<const Snapshot> audioNotes;
    double position { 0.0 };
    bool wasPlaying { false }, listChanged { false };
    juce::uint32 lastRollCommand { 0 };
    bool rollCommandKnown { false }, rollCommandPending { false }, rollCommandPlays { false };
    struct Active { int index { -1 }; int key { 60 }; };
    std::array<Active, kMaxActive> active {};
    int numActive { 0 };
    size_t nextIndex { 0 };
    int exprCountdown { 0 };
    float tuning { 0.0f };
    bool useSlide { false };
};
