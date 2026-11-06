#pragma once

#include "../../Core/ModuleCore.h"
#include "Acid303Core.h"

// 303 - a bass line synth fitted to recordings of the original hardware (the
// voice is Acid303Core: the saw and the 303's slanted square, the four-pole
// filter with its resonance, the cutoff envelope, the accent sweep, the VCA).
//
// Free: played from its Midi In (the keys when nothing is patched there). A
// note at least as loud as Acc Vel is an accent. Mono slides into a note
// played while another is held, as the 303 does; Poly gives every note its
// own voice and never slides.
//
// Grid: its own 303-style pattern, edited in the card - 16 steps a bar, 1 to
// 4 bars, one lane each for the note, the octave (up / down), the accent, the
// slide and the step's time (note / tie / rest). RUN starts it, on its own
// tempo or the host's; Play All / Stop All on a Piano Roll starts and stops
// it together with the rolls.
//
// FM In: phase modulation, like the other oscillators (the depth is FM x the
// level of what comes in).
struct Acid303Step
{
    int8_t note { 0 };       // semitones above C, 0..12
    int8_t octave { 0 };     // -1 down, 0, +1 up
    bool accent { false };
    bool slide { false };    // legato into the next step
    uint8_t time { 0 };      // 0 note, 1 tie, 2 rest

    bool operator== (const Acid303Step& o) const noexcept
    {
        return note == o.note && octave == o.octave && accent == o.accent && slide == o.slide && time == o.time;
    }
};

struct Acid303Pattern
{
    static constexpr int kStepsPerBar = 16, kMaxBars = 4, kMaxSteps = kStepsPerBar * kMaxBars;
    std::array<Acid303Step, kMaxSteps> steps {};

    static Acid303Pattern classic();                  // the line a new 303 starts with
    static Acid303Pattern random (juce::Random&, int bars);     // an acid line
    // a hook that loops: style 1 a pentatonic arp, 2 octave steps, 3 an acid line, 0 any of
    // those three. The long ones, always 4 bars: 4 DRIFT, 5 BLOOM, 6 TIDE (gentle pentatonic
    // lines that wander off their home loop and find it again - see longLine)
    static Acid303Pattern hook (juce::Random&, int bars, int style);
    static const char* hookStyleName (int style);
    static constexpr int kNumHookStyles = 7, kFirstLongStyle = 4;
    static bool isLongStyle (int style) { return style >= kFirstLongStyle && style < kNumHookStyles; }
    static Acid303Pattern longLine (juce::Random&, int style);
    juce::String toString() const;
    static bool fromString (const juce::String&, Acid303Pattern& out);
    bool operator== (const Acid303Pattern& o) const noexcept { return steps == o.steps; }
};

class Acid303Module : public aquanode::SynthModule
{
public:
    // must match the descriptor's param order
    enum ParamIndex { pWave = 0, pVoiceMode, pPlay, pCutoff, pResonance, pEnvMod, pDecay, pAccent, pVolume,
                      pTune, pSlide, pAccentVel, pFm, pBars, pTempo, pSync, pRunning, pTranspose,
                      pAccentDecay, pSweepTime, pTrack, pDrive, pSwing, pHookStyle };

    static constexpr int kPolyVoices = 8;
    static constexpr int kBaseNote = 36;   // the 303's low C (the recordings' note), C2

    Acid303Module();

    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage&) override;

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    //=== the pattern (message thread) =========================================
    const Acid303Pattern& getPattern() const { return pattern; }
    void setPattern (const Acid303Pattern&);
    int numBars() const { return juce::jlimit (1, Acid303Pattern::kMaxBars, (int) std::lround (getParameter ("bars"))); }
    bool isRunning() const { return getParameter ("running") > 0.5f; }
    void setRunning (bool r) { setParameter ("running", r ? 1.0f : 0.0f); }
    int getPlayStep() const { return uiStep.load (std::memory_order_relaxed); }
    double getEffectiveBpm() const { return uiBpm.load (std::memory_order_relaxed); }
    bool isHostTempoAvailable() const { return uiHostTempo.load (std::memory_order_relaxed); }
    int getEditCounter() const { return editCounter.load (std::memory_order_relaxed); }
    int soundingVoices() const { return uiVoices.load (std::memory_order_relaxed); }   // (tests, UI)

    //=== UI / state ===========================================================
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return getParameter ("play") > 0.5f ? 206 : 0; }
    int preferredModuleWidth() const override { return 640; }
    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

private:
    struct Held { int note; bool accent; };

    aquanode::acid303::Knobs currentKnobs() const;
    double noteHz (int note) const;
    int allocateVoice (int note);
    void startNote (int note, bool accent, bool slide);    // free play
    void stopNote (int note);
    void allVoicesOff();
    void gridAdvance();
    void gridStep (int step);
    int numBarsAudio() const;

    //=== message thread =======================================================
    Acid303Pattern pattern { Acid303Pattern::classic() };
    std::atomic<int> editCounter { 0 };

    //=== shared ===============================================================
    juce::SpinLock patternLock;
    Acid303Pattern pending { Acid303Pattern::classic() };
    std::atomic<juce::uint32> patternVersion { 1 };
    std::atomic<int> uiStep { -1 };
    std::atomic<double> uiBpm { 120.0 };
    std::atomic<bool> uiHostTempo { false };
    std::atomic<int> uiVoices { 0 };

    //=== audio thread =========================================================
    Acid303Pattern audioPattern { Acid303Pattern::classic() };
    juce::uint32 seenVersion { 0 };
    std::array<aquanode::acid303::Voice, kPolyVoices> voices;
    std::array<int, kPolyVoices> voiceNote {};
    std::array<juce::uint32, kPolyVoices> voiceAge {};
    juce::uint32 ageCounter { 0 };
    std::vector<Held> held;            // mono: the keys down, oldest first
    int lastMode { -1 }, lastPlay { -1 }, lastWave { -1 };
    float lastSlide { -1.0f };

    // grid
    bool gridWasRunning { false };
    double stepPos { 0.0 };            // samples into the current step
    int step { -1 };                   // the step playing, -1 = none yet
    int gridNote { -1 };               // the pitch sounding from the grid (-1 none)
    bool gateOffDone { true }, gridPrevSlide { false };
    juce::uint32 lastRollCommand { 0 };
    bool rollCommandKnown { false }, rollCommandPending { false }, rollCommandPlays { false };
};
