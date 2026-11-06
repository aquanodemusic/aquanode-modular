#pragma once

#include "../../Core/ModuleCore.h"

// Spectral Pitch Quantizer - a polyphonic FFT pitch mapper, modelled on
// measurements of Zynaptiq PITCHMAP (its VST3 rendered over a test set and
// compared partial by partial with reassigned spectra, Nov 2026).
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
//
// Global effect. Input: 0 = Audio In. Output: 0 = Audio Out.
class SpectralPitchQuantizerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pTolerance = 0, pIntonation, pClean, pGlide, pWarp,
        pMap0,                 // map0 .. map11: output pitch class per input pitch class
        pTopFold = pMap0 + 12
    };

    // The default matrix: the PITCHMAP preset this module was matched
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

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 300; }
    int extraContentHeight() const override { return 214; }

    // semitones pitch class pc moves to reach outPc: the nearest way, -6 .. +5
    static int offsetFor (int pc, int outPc) noexcept
    {
        int d = ((outPc - pc) % 12 + 12) % 12;
        return d > 5 ? d - 12 : d;
    }

private:
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
    };

    juce::dsp::FFT fft { kOrder };
    std::vector<float> window;
    float olaGain = 1.0f;

    // ring of the last kN input samples per channel, output accumulators
    std::vector<float> inRing[2], outAcc[2];
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
    int numPeaks = 0, numPrevPeaks = 0;
    int polyHold = 0;

    // per input note glide state
    std::array<float, 128> noteShift {};
    std::array<int64_t, 128> noteLastSeen {};
};
