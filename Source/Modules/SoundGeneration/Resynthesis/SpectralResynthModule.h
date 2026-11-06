#pragma once

#include "../../Core/ModuleCore.h"

//==============================================================================
// Spectral Resynthesis - a sound (a loaded file, or a take recorded from Rec
// In) taken apart, edited where you point, and played back from the keys.
//
// Two engines (Analysis > Engine):
//
//   Bins (STFT): the sound's own short-time spectrum, every bin of every
//     frame (Hann, a quarter-window hop), magnitude and phase. Played back
//     untouched - at its Root, Speed 1 - the original phases go straight back
//     through the inverse transform: the sound comes out as it went in (to
//     about -70 dB). Anything else - another key, another speed, Freeze, an
//     edited region - runs a phase vocoder with the phases locked around each
//     spectral peak (each peak's region moves as one, which keeps partials
//     clean when the pitch moves). Stereo files stay stereo.
//
//   Partials (reassigned): the sound as sine partials plus a noise floor. The
//     peaks of each frame take their exact frequency and level from spectral
//     reassignment (ResynthAnalysis.h, this module's own copy of the
//     analysis), are joined into partial tracks, and play as a bank of sine
//     oscillators; what is not a partial is 24 bands of noise. A model, not
//     the recording: close, never identical (transients soften), but every
//     partial is its own line to move, stretch or scatter.
//
// Edits - a list of operations applied one after another, kept in the patch:
//   on the selection (any number of boxes, time x frequency): Delete, Keep
//   (delete everything around them), Gain, Transpose, Reverse (in time),
//   Rotate (the box's content turned by an angle in time x log-frequency),
//   Smear (what sounds there rings on, fading over the time set) - a region
//   is a box, or any shape drawn by hand (Draw);
//   on a note (tap a partial with Note: its fundamental is searched - which
//   harmonic of which note the partial is - and followed through time, the
//   note and all its harmonics shown in white): Remove, Gain, Transpose,
//   Unison (detuned copies), Fundamental Only, No Fundamental, Odd Only.
//
// Sockets: FM In (playback pitch), Env In (an ADSR), Add Midi In, Rec In
// (what Record records; heard whether or not a note plays), Audio Out.
//==============================================================================
class SpectralResynthModule : public aquanode::SynthModule,
                              private juce::Thread
{
public:
    enum ParamIndex
    {
        pVolume = 0, pLoad, pRecord, pVoices, pGlide,
        pEngine, pFft, pHop, pWindow, pDecim, pThreshold, pMaxPeaks, pMinLen,
        pSpeed, pStart, pEnd, pLoop, pFreeze, pPosition, pRoot, pFine,
        pTranspose, pShiftHz, pScatter, pTilt, pLowCut, pHighCut, pSines, pNoise, pPartials, pSpread
    };

    static constexpr int kBands = 24;            // Partials: noise floor bands
    static constexpr int kBlock = 32;            // Partials: control block (samples)
    static constexpr int kMaxSlots = 1024;       // Partials: oscillators one voice can run
    static constexpr int kMaxPeaksLimit = 256;
    static constexpr int kMaxFramePoints = 480;  // Partials: points one frame keeps after edits (unison adds)
    static constexpr int kMaxBinOrder = 14;      // Bins: up to 16384-point frames
    static constexpr double kMaxRecordSeconds = 120.0;

    //=== edits ================================================================
    // a region: a box (time x frequency), or a shape drawn by hand (Draw) -
    // its outline in poly (x = seconds, y = Hz), the box then its bounds
    struct Box
    {
        float t0 = 0, t1 = 0, f0 = 0, f1 = 0;    // seconds, Hz
        std::vector<juce::Point<float>> poly;    // empty for a box
        std::vector<float> polyLog;              // poly's y as log2 Hz (finishShape fills it)

        bool isShape() const noexcept { return poly.size() >= 3; }
        bool contains (float t, float hz) const noexcept
        {
            if (! (t >= t0 && t <= t1 && hz >= f0 && hz <= f1))
                return false;
            if (! isShape())
                return true;
            // even-odd rule in time x log-frequency (the map's own axes)
            const float y = std::log2 (juce::jmax (1.0f, hz));
            bool in = false;
            const size_t n = poly.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++)
            {
                const float xi = poly[i].x, xj = poly[j].x;
                const float yi = logY (i), yj = logY (j);
                if ((xi > t) != (xj > t) && y < yi + (yj - yi) * (t - xi) / (xj - xi))
                    in = ! in;
            }
            return in;
        }
        // the frequency stretches (Hz) the region covers at time t
        void slice (float t, std::vector<std::pair<float, float>>& out) const
        {
            out.clear();
            if (t < t0 || t > t1)
                return;
            if (! isShape())
            {
                out.push_back ({ f0, f1 });
                return;
            }
            std::vector<float> ys;
            const size_t n = poly.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++)
            {
                const float xi = poly[i].x, xj = poly[j].x;
                if ((xi > t) != (xj > t))
                    ys.push_back (logY (i) + (logY (j) - logY (i)) * (t - xi) / (xj - xi));
            }
            std::sort (ys.begin(), ys.end());
            for (size_t k = 0; k + 1 < ys.size(); k += 2)
                out.push_back ({ std::exp2 (ys[k]), std::exp2 (ys[k + 1]) });
        }
        // after poly was set: the bounds and the log cache
        void finishShape()
        {
            if (! isShape())
            {
                poly.clear();
                polyLog.clear();
                return;
            }
            t0 = t1 = poly.front().x;
            f0 = f1 = poly.front().y;
            polyLog.clear();
            for (const auto& p : poly)
            {
                t0 = juce::jmin (t0, p.x); t1 = juce::jmax (t1, p.x);
                f0 = juce::jmin (f0, p.y); f1 = juce::jmax (f1, p.y);
                polyLog.push_back (std::log2 (juce::jmax (1.0f, p.y)));
            }
        }
        // the shape moved in frequency (Transpose keeps the selection on what it moved)
        void scaleFrequency (float ratio)
        {
            f0 *= ratio; f1 *= ratio;
            for (auto& p : poly) p.y *= ratio;
            if (isShape()) finishShape();
        }
    private:
        float logY (size_t i) const noexcept
        {
            return i < polyLog.size() ? polyLog[i] : std::log2 (juce::jmax (1.0f, poly[i].y));
        }
    };

    // a note: its fundamental through time (x = seconds, y = Hz)
    struct NoteContour
    {
        std::vector<juce::Point<float>> pts;
        bool isEmpty() const { return pts.empty(); }
        float start() const { return pts.empty() ? 0.0f : pts.front().x; }
        float end() const { return pts.empty() ? 0.0f : pts.back().x; }
        float f0At (float t) const;              // 0 outside
        // which harmonic of the note (t, hz) is, 0 for none (within 35 cents)
        int harmonicOf (float t, float hz, float hopSeconds) const;
    };

    enum class Op
    {
        Delete = 0, Keep, Gain, Transpose, Reverse, Rotate, Smear,
        NoteRemove, NoteGain, NoteTranspose, NoteUnison, NoteFundOnly, NoteNoFund, NoteOddOnly
    };

    struct Edit
    {
        Op op { Op::Delete };
        std::vector<Box> regions;               // the region operations
        NoteContour note;                       // the note operations
        float amount { 0.0f };                  // dB, semitones, degrees, unison voices, smear (ms)
        float amount2 { 0.0f };                 // unison: detune (cents)
        bool isNoteOp() const { return op >= Op::NoteRemove; }
    };

    static juce::String describe (const Edit& e);

    //=== the analysis result (immutable once published) =======================
    struct Peak { float hz, amp; };

    struct Point { int track; float hz, amp; };   // amp: linear, a full-scale sine = 1

    struct Track
    {
        int start = 0, length = 0;       // frames
        float meanHz = 0.0f, peakDb = -200.0f, energy = 0.0f;
        int rank = 0;                    // 0 = the most energy
        juce::uint32 hash = 0;           // pan / phase / scatter seed
    };

    struct PartialModel
    {
        double hopSeconds = 0.01;
        float nyquist = 22050.0f;
        int numFrames = 0;
        std::vector<int> frameStart;     // numFrames + 1 offsets into points
        std::vector<Point> points;       // per frame, sorted by track
        std::vector<Track> tracks;
        std::vector<float> noise;        // numFrames * kBands: band power
        std::array<float, kBands + 1> bandEdges {};

        double frameTime (int f) const { return f * hopSeconds; }
        int frameAt (double seconds) const
        {
            return juce::jlimit (0, juce::jmax (0, numFrames - 1), (int) std::floor (seconds / hopSeconds + 0.5));
        }
    };

    // one STFT frame of every channel: magnitude (log, 1/256 dB steps) and
    // phase (int16) per bin; an edited frame also carries each bin's
    // frequency and whether its phase is free (propagated, not the original)
    struct BinFrame
    {
        std::vector<juce::uint16> mag;   // channels * bins
        std::vector<juce::int16> phase;  // channels * bins
        std::vector<float> freq;         // bins (in bins), edited frames only
        std::vector<juce::uint8> freePhase; // bins, edited frames only
        bool edited() const { return ! freePhase.empty(); }
    };

    struct BinModel
    {
        int N = 4096, hop = 1024, bins = 2049, channels = 1, numFrames = 0;
        double rate = 48000.0;           // the engine's rate (the sound is resampled to it)
        float topDb = -200.0f;
        std::vector<std::shared_ptr<const BinFrame>> frames;
        std::shared_ptr<const BinFrame> before;   // the frame centred a hop before the sound starts (playback only)
        double hopSeconds() const { return (double) hop / rate; }
        double frameTime (int f) const { return f * hopSeconds(); }
        int frameAt (double seconds) const
        {
            return juce::jlimit (0, juce::jmax (0, numFrames - 1), (int) std::floor (seconds / hopSeconds() + 0.5));
        }
        float binHz (float k) const { return k * (float) rate / (float) N; }
    };

    struct Model
    {
        int serial = 0;
        bool binsEngine = false;
        double duration = 0.0, fileRate = 44100.0;
        float f0 = 0.0f, f0Confidence = 0.0f;    // the sound's fundamental, where clear
        int editsApplied = 0;
        std::shared_ptr<const PartialModel> partials;
        std::shared_ptr<const BinModel> bins;
        double hopSeconds() const { return binsEngine ? bins->hopSeconds() : partials->hopSeconds; }
        int numFrames() const { return binsEngine ? bins->numFrames : partials->numFrames; }
        int frameAt (double s) const { return binsEngine ? bins->frameAt (s) : partials->frameAt (s); }
        double frameTime (int f) const { return binsEngine ? bins->frameTime (f) : partials->frameTime (f); }
        float nyquist() const { return binsEngine ? (float) bins->rate * 0.5f : partials->nyquist; }
    };

    // the peaks of one frame of a model (Partials: its points; Bins: the
    // magnitude spectrum's peaks), loudest first
    static void framePeaks (const Model& m, int frame, std::vector<Peak>& out);

    // the note a partial at (t, hz) belongs to: which harmonic of which
    // fundamental best explains it and the peaks around it (saw-, square- and
    // triangle-like spectra tried, the fundamental refined from every
    // harmonic found), then followed through the frames before and after
    static NoteContour findNote (const Model& m, double t, float hz);

    //==========================================================================
    SpectralResynthModule();
    ~SpectralResynthModule() override;

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void voiceNoteOn (int voice, int note, bool retrigger) override;
    void voiceVelocity (int voice, float velocity01) override { vel[voice] = velocity01; }
    void voiceNoteOff (int voice) override;
    void voiceReset (int voice) override;
    double voiceTailSeconds() const override { return 0.03; }

    // Rec In, heard outside the voices
    int globalTapInput() const override { return 3; }
    void globalTapSample (const aquanode::StereoFrame& frame) override;

    bool usesLoadedSample() const override { return true; }
    void uiButtonClicked (const juce::String& paramId) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 700; }
    int extraContentHeight() const override { return 470; }

   #if JUCE_ANDROID
    double maxSampleSeconds() const override { return 60.0; }
   #else
    double maxSampleSeconds() const override { return 120.0; }
   #endif

    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

    //=== for the card (message thread) ========================================
    std::shared_ptr<const Model> currentModel() const;
    bool isAnalysing() const { return analysing.load (std::memory_order_relaxed); }
    float analysisProgress() const { return progress.load (std::memory_order_relaxed); }

    std::vector<Edit> getEdits() const;
    void setEdits (std::vector<Edit> e);
    void addEdit (Edit e);
    void undoEdit();
    int editGeneration() const { return editGen.load (std::memory_order_relaxed); }

    // what the card has selected (kept here so it survives the card being
    // rebuilt; not saved with the patch)
    std::vector<Box> selection;
    NoteContour selectedNote;

    void setRootFromAnalysis();
    void toggleRecording();
    bool isRecording() const { return recording.load (std::memory_order_relaxed); }
    double recordedSeconds() const;

    // the file position (seconds) of each sounding voice, < 0 for idle ones
    float voicePosition (int v) const { return voicePos[(size_t) v].load (std::memory_order_relaxed); }

    // for tests: blocks until the loaded sound and the edits are analysed
    bool waitUntilReady (int timeoutMs);

    static float centsBetween (float a, float b) noexcept
    {
        return 1200.0f * std::abs (std::log2 (juce::jmax (1.0e-6f, a) / juce::jmax (1.0e-6f, b)));
    }

private:
    //=== analysis (the thread) ================================================
    struct AnalysisKey
    {
        int counter = -1, engine = 0, order = 0, hopDenom = 0, window = 0, decim = 0, maxPeaks = 0, minLen = 0;
        float threshold = 0.0f;
        double rate = 0.0;
        bool operator== (const AnalysisKey& o) const
        {
            return counter == o.counter && engine == o.engine && order == o.order && hopDenom == o.hopDenom
                && window == o.window && decim == o.decim && maxPeaks == o.maxPeaks && minLen == o.minLen
                && juce::exactlyEqual (threshold, o.threshold) && juce::exactlyEqual (rate, o.rate);
        }
    };

    // Partials, before the edits: each frame's peaks and noise floor
    struct PeakBase
    {
        double hopSeconds = 0.01, duration = 0.0, fileRate = 44100.0;
        float nyquist = 22050.0f, binHz = 10.0f;
        int numFrames = 0, minLen = 4;
        std::vector<int> frameStart;
        std::vector<Peak> peaks;         // per frame, loudest first
        std::vector<float> noise;
        std::array<float, kBands + 1> bandEdges {};
        float f0 = 0.0f, f0Confidence = 0.0f;
    };

    void run() override;
    AnalysisKey currentKey() const;
    std::shared_ptr<PeakBase> analysePeaks (const AnalysisKey& key, const juce::AudioBuffer<float>& sound, double rate);
    std::shared_ptr<BinModel> analyseBins (const AnalysisKey& key, const juce::AudioBuffer<float>& sound, double rate,
                                           float& f0, float& f0Confidence);
    std::shared_ptr<PartialModel> buildPartials (const PeakBase& base, const std::vector<Edit>& edits);
    std::shared_ptr<BinModel> editBins (const std::shared_ptr<const BinModel>& base, const std::vector<Edit>& edits);
    bool keepGoing (const AnalysisKey& key) const;
    void freeRetired();

    //=== playback ==============================================================
    struct Slot { int track = 0; float re = 1, im = 0, c = 1, s = 0, amp = 0, gl = 1, gr = 1; };

    struct Voice
    {
        double t = 0.0;                  // seconds into the sound (Partials), frames (Bins)
        int dir = 1;                     // Ping-Pong direction
        bool ended = false;              // Once: played through
        juce::uint32 seed = 1;
        int pos = 1 << 30;               // next sample of the rendered block
        int blockLen = kBlock;
        int modelSerial = -1;
        // Partials
        int numSlots = 0;
        std::vector<Slot> slots, next;
        float bandIc1[kBands] {}, bandIc2[kBands] {};
        juce::uint32 noiseState = 22222;
        float noiseGain[kBands] {};
        // Bins: the phase vocoder's running phases and overlap-add
        std::vector<float> runPhase[2], ola[2], out[2];
        double srcFrame = 0.0;           // the next frame to synthesise, in source frames
        bool primed = false;
        int lastFrameInt = -1000000;     // the last frame played on the exact path (original phases)
        int prevFrame = -1000000;        // the last frame played, whichever path
        int modelN = 0;                  // the frame size its buffers hold
    };

    void renderPartials (int v, float fmIn);
    void renderBins (int v, float fmIn);
    float pitchRatio (int v, float fmIn);
    double advance (Voice& vc, double step, double ts, double te);   // one step on, in the units of ts/te

    // shared with the audio thread (latched in blockStart)
    mutable juce::SpinLock shareLock;
    std::shared_ptr<const Model> model, latchedModel;

    // analysis thread
    std::vector<std::shared_ptr<const void>> retired;
    AnalysisKey lastKey;
    std::shared_ptr<PeakBase> peakBase;
    std::shared_ptr<const BinModel> binBase;
    float baseF0 = 0.0f, baseF0Confidence = 0.0f;
    int lastEditGen = -1, nextSerial = 1;
    std::atomic<bool> analysing { false }, idle { false };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> rootFromNextAnalysis { false };

    // the edits (message thread writes, the analysis thread reads)
    mutable juce::CriticalSection editLock;
    std::vector<Edit> edits;
    std::atomic<int> editGen { 0 };

    // recording (the audio thread writes; Record arms and stops)
    juce::CriticalSection recLock;
    std::vector<float> recL, recR;
    std::atomic<bool> recording { false }, takeReady { false };
    std::atomic<int> recCount { 0 };
    void finishRecording();
    void newSoundByHand();               // a picked file or a take: no edits, Root from it
    std::unique_ptr<juce::FileChooser> chooser;
    struct RecTimer;
    std::unique_ptr<RecTimer> recTimer;

    // voices (audio thread)
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    std::array<Voice, aquanode::kMaxVoices> voices;
    float vel[aquanode::kMaxVoices] {};
    float gateLvl[aquanode::kMaxVoices] {};
    bool gateOn[aquanode::kMaxVoices] {};
    std::array<std::atomic<float>, aquanode::kMaxVoices> voicePos;
    juce::uint32 seedCounter { 0x9e3779b9u };

    struct Scratch;
    std::unique_ptr<Scratch> scratch;
};
