#pragma once

#include "../../Core/ModuleCore.h"

// Spectral Pitch Quantizer - a polyphonic FFT pitch mapper, modelled on
// measurements of a well-known original pitch quantizer plugin.
//
// Two engines share one analysis and one resynthesis and differ in what they
// decide to move, and where to:
//   Engine A  the module as described below. Patches saved before the
//             switch existed keep it.
//   Engine B  the default: the rules read off a second, wider set of
//             renders - see assignShiftsB in the .cpp. In short: only
//             partials that stand clear are moved, the crowded rest passes;
//             notes from A#3 up carry their harmonics, lower notes fold
//             partial by partial; no flicker; a brighter folded top.
//
// A 4096-point STFT (512 hop, 8x overlap; latency exactly 4096 samples, as
// the original's at 48 kHz) finds the tonal peaks of every frame: a peak
// counts when it stands 8x above the 20th percentile of the +-32 bins around
// it, so dense chords keep all their partials while noise passes unmoved.
// Each peak's true frequency (phase vocoder) is read as a note, and the
// 12 x 12 matrix in the card says which note its pitch class goes to (the
// nearest way). Peaks are tracked from frame to frame, keep their note with
// hysteresis and are resynthesised as ideal sinusoids at their new frequency
// (the Hann window's own lobe), each a continuous oscillator, plus whatever
// of the input lobe is not that sinusoid (Clean). Unmoved peaks, notes the
// matrix leaves where they are, and everything that is not a tonal peak
// pass through untouched, so an identity matrix is transparent.
//
// What moves together, as measured on the original: a single steady note's
// partials move each by its own pitch (a saw's 3rd harmonic lands where its
// own pitch class maps - the folded, partly inharmonic tone), except its
// upper harmonics (from the 6th, fully by the 16th), which move with the note
// so the top does not pile up onto a few notes as sparse, piercing peaks;
// with two or more notes at once, or a gliding note (vibrato, slides), the
// partials move with the note they are the lowest harmonic of (up to the
// 80th), so each note keeps its shape.
//
// The five knobs, renamed from the original's for what they do:
//   Tolerance  (Threshold) a dead zone around each semitone, 0..50 cents
//              (50 % = 16.5 c): inside it nothing is corrected
//   Intonation (Feel)      how much of a partial's detune survives: its
//              steady detune in proportion, its vibrato only near 100 %
//   Clean      (Purify)    how much of what is not the ideal sinusoid (chirp,
//              noise, a merged second partial) travels with a moved partial
//   Glide      (Glide)     every newly heard note starts unshifted and
//              slides onto its target
//   Warp       (Electrify) 0: partials of single notes move each alone;
//              towards 50 % gliding notes move as one; to 100 % all moves
//              bend into one smooth, stretched frequency warp ("electric")
//   Top Fold   (new)       0: a note's upper harmonics stay with it (a dense,
//              smooth top, close to the original); 100 %: they fold onto
//              the map one by one as well - sparse, piercing, brighter (this
//              module's first version)
//   Air        (new)       the quantizer's bright, folded sound lives in the
//              top of the spectrum, so a dull input gives it little to move:
//              Air lifts the highs of what it hears (a shelf from ~2.5 kHz,
//              up to +18 dB) and, from a quarter up, adds harmonics made from
//              the mids and up (level-independent), so even a soft pad gets a
//              top to fold. Only the wet path: Dry/Wet's dry stays as it was.
//
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
class SpectralPitchQuantizerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pTolerance = 0, pIntonation, pClean, pGlide, pWarp,
        pMap0,                 // map0 .. map11: output pitch class per input pitch class
        pTopFold = pMap0 + 12,
        pMix, pWetGain, pAir, pRoot, pScale,
        pEngine,               // 0 = Mono (engine A, the module up to 6.0), 1 = Multi (engine B, 6.1), 2 = Bright (6.2: Multi + the four below)
        pQuiet,                // Pass Quiet (6.2): 0 = off, both engines exactly as before
        pFlicker,              // Flicker (6.2): 0 = off, both engines exactly as before
        pSnap                  // Snap (6.2): a note whose pill is on its own row is tuned onto it; Off = as before
    };

    // Root and Scale: picking a scale stamps the map (every note heard goes to
    // the nearest note of the scale on Root); editing the map by hand turns
    // Scale back to Custom
    static constexpr int kNumScales = 18;
    static const char* const kScaleNames[kNumScales];
    void setParameter (const juce::String& id, float value) override;

    // The default matrix: the preset this module was matched
    // against (C major pentatonic folded onto F# pentatonic by Key Transform).
    static const int kDefaultMap[12];

    static constexpr int kOrder = 12;
    static constexpr int kN = 1 << kOrder;        // 4096
    static constexpr int kHop = kN / 8;           // 512
    static constexpr int kBins = kN / 2 + 1;
    static constexpr int kMaxPeaks = 600;
    static constexpr int kMedianReach = 32;
    static constexpr int kMaxNotes = 16, kPolyHoldFrames = 8;   // notes heard at once; ~85 ms hold
    // Warp's smoothing grid: 1/24 octave from 16 Hz (log2 = 4) over 11 octaves
    static constexpr float warpWidthOct = 0.25f;
    static constexpr float kGridLowOct = 4.0f, kGridPerOct = 24.0f;
    static constexpr int kGrid = 11 * 24 + 1;

    SpectralPitchQuantizerModule();

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    int latencySamples() const override { return kN; }

    // Midi In (6.2, heard in Bright only): the notes held on what is patched there are let free -
    // their pitch classes pass as if their columns held no pill. Nothing
    // patched, nothing changes (the played keys never reach an effect).
    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (m.isNoteOn())
            heldNotes[(size_t) juce::jlimit (0, 127, m.getNoteNumber())] = 1;
        else if (m.isNoteOff())
            heldNotes[(size_t) juce::jlimit (0, 127, m.getNoteNumber())] = 0;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            heldNotes.fill (0);
    }
    void midiAllNotesOff() override { heldNotes.fill (0); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 300; }
    int extraContentHeight() const override { return 214; }

    // semitones pitch class pc moves to reach outPc: the nearest way, -6 .. +5
    static int offsetFor (int pc, int outPc) noexcept
    {
        int d = ((outPc - pc) % 12 + 12) % 12;
        return d > 5 ? d - 12 : d;
    }

    // Engine B's reading of the matrix: the nearest way as well, but a tritone
    // goes up from the lower half of the octave and down from the upper one,
    // as the original does (C to F# above, G to C# below)
    static int offsetForB (int pc, int outPc) noexcept
    {
        const int d = ((outPc - pc) % 12 + 12) % 12;
        if (d == 6) return pc < 6 ? 6 : -6;
        return d > 5 ? d - 12 : d;
    }

private:
    void stampScale();
    bool stamping { false };
    void processFrame();

    // The periodic Hann window's spectral lobe for a sinusoid dlt bins away
    // from a bin, relative to the window sum (1 at dlt = 0; real and signed:
    // its phase is pi * dlt): 0.5 sinc (d) + 0.25 sinc (d - 1) + 0.25 sinc (d + 1).
    static float hannKernel (float dlt) noexcept
    {
        auto sinc = [] (float x) { return std::abs (x) < 1.0e-5f ? 1.0f : std::sin (juce::MathConstants<float>::pi * x) / (juce::MathConstants<float>::pi * x); };
        return 0.5f * sinc (dlt) + 0.25f * sinc (dlt - 1.0f) + 0.25f * sinc (dlt + 1.0f);
    }
    static constexpr int kLobe = 3;     // bins each side of a synthesised partial

    struct Peak
    {
        int bin = 0, lo = 0, hi = 0;   // region [lo, hi]
        float freq = 0;                // rad / sample (true frequency)
        float mag = 0;
        int note = 0;
        float shift = 0;               // semitones, final
        float logf = 0;                // octaves, for Warp smoothing
        int prev = -1;                 // the previous frame's peak it continues
        float psi[2] {};               // output oscillator phase at the frame start, per channel
        float outFreq = 0;             // rad / sample, after the move
        float devSlow = 0;             // its deviation from its note, smoothed (semitones)
        float motion = 0;              // how fast its pitch moves, cents per hop, smoothed
        // version B: the partial moved with its note (see Warp)
        float shiftB = 0, mixB = 0;    // semitones; its share of the power
        float psiB[2] {};
        float outFreqB = 0;
        // engine B
        float own = 0;                 // where its own pitch sends it (semitones)
        float target = 0, cur = 0;     // where it is sent; where it is on the way there (Glide)
        int age = 0;                   // frames it has been heard
        bool clear = false;            // no other partial crowds it
        bool quiet = false;            // Pass Quiet: too far under the loudest to be moved
        bool flipped = false;          // Flicker: sent where its other neighbouring note goes, this frame
        bool accepted = false;         // it is moved
        int absorber = -1;             // the stronger partial it is one lump with
        int rootNote = -1, rootK = 0;  // the note it is a harmonic of, and which one
        int hold = 0;                  // frames it still waits for that note to come back
    };

    // one frame's knobs, as both engines read them
    struct Controls
    {
        int offsets[12] {};
        float tolSemis = 0, keep = 0, clean = 0, glideA = 1, devSlowA = 1, motionA = 1;
        float warpKnob = 0, noteMix = 0, warpSmooth = 0, topFold = 0;
        int snapMask = 0;              // Snap: pitch classes that stay on their note but are tuned onto it
        // where a note goes, in semitones from where it is (dev: how far it lies off its semitone)
        float targetFor (int pc, float keptDev, float dev) const
        {
            if (offsets[pc] == 0 && ((snapMask >> pc) & 1) == 0)
                return 0.0f;
            return (float) offsets[pc] + keptDev - dev;
        }
    };
    void assignShiftsA (const Controls&);
    void assignShiftsB (const Controls&);
    int engineNow = -1;
    std::array<uint8_t, 128> heldNotes {};     // Midi In: audio thread only
    float quietRef = 0.0f;                     // Pass Quiet: the loudest partial, held and slowly let go
    juce::Random flickerRandom { 0x5eed };     // Flicker: the same flicker on every render

    juce::dsp::FFT fft { kOrder };
    std::vector<float> window;
    float olaGain = 1.0f;

    // ring of the last kN input samples per channel (as the quantizer hears
    // them, after Air), the dry input, output accumulators
    std::vector<float> inRing[2], dryRing[2], outAcc[2];

    // Air: a high shelf, and harmonics from the upper mids
    struct BQ
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x) noexcept { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    BQ airShelf[2], airBand[2], airTop[2];
    float airEnv[2] {};
    float airLast = -1.0f;
    void updateAir (float air);
    int ringPos = 0, hopCount = 0;
    int64_t frameIndex = 0;

    // per frame scratch
    std::vector<float> fftBuf;                    // 2 * kN
    std::vector<std::complex<float>> spec[3];     // mid, L, R
    std::vector<std::complex<float>> outSpec[2];
    std::vector<float> midMag, midPhase, prevMidPhase;
    std::vector<float> chPhase[2];
    std::vector<uint8_t> claimed;
    std::array<float, 2 * 32 + 1> medianScratch {};
    std::array<float, kGrid> gridW {}, gridS {}, gridSmooth {};
    std::array<float, 2 * 18 + 1> gridKernel {};   // +-3 widths of 6 cells
    std::array<Peak, kMaxPeaks> peaks, prevPeaks;
    std::array<float, kMaxPeaks> peakScale {}, peakGain {};      // engine B: partials landing on one pitch
    std::array<std::pair<float, int>, kMaxPeaks> landing {};
    int numPeaks = 0, numPrevPeaks = 0;
    int polyHold = 0;

    // per input note glide state
    std::array<float, 128> noteShift {};
    std::array<int64_t, 128> noteLastSeen {};
};
