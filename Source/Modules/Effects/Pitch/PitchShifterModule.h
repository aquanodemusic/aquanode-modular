#pragma once

#include "../../Core/ModuleCore.h"

//==============================================================================
// Pitch Shifter - a grain pitch shifter with two algorithms:
//
//   A delay line read by up to eight heads. Each head plays one grain, reading
//   the line at the pitch ratio r, so its delay shrinks (r > 1) or grows
//   (r < 1) over the grain; a new head starts every quarter grain where its
//   grain has room.
//
//   Correlated: a new grain every quarter Grain Size, each playing on its own
//   between short crossfades (Crossfade, 10 ms by default). Its start is searched
//   (a quarter grain either way) for the place whose waveform matches what the
//   grain before it plays through the crossfade, so the splice adds up in
//   phase: no comb, no flutter, steady tones stay steady.
//   Grains: Hann windows Grain Size long, four deep, starting at random places
//   with no alignment, adding up with random phases - the airy, shimmering
//   kind (power normalised, about as loud as Correlated).
//
//   Jitter randomises the grains' timing and length (and in Grains how far
//   apart they start). Feedback sends the shifted sound back into the line
//   (rising / falling shimmer), Stereo Wide runs the two sides' grains
//   independently. Dry Delay Match delays the dry sound by the wet sound's
//   delay so that Mix does not comb at small shifts.
//
// The read is a windowed sinc (16 taps), and upward shifts are low-passed
// first, so a tone near the top does not fold back.
//
// Global effect. Inputs: 0 = Audio In. Output: 0 = Audio Out.
//==============================================================================
class PitchShifterModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pPitch = 0, pFine, pGrain, pJitter, pMix, pAlgorithm, pFeedback, pStereo, pDryDelay, pCrossfade };

    static constexpr int kMaxHeads = 8;
    static constexpr int kTaps = 16;
    static constexpr int kPhases = 1024;

    PitchShifterModule();

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Global; }
    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    // the wet sound's current delay (samples), for tests
    double currentDelaySamples() const { return lastDelay; }

private:
    struct Head
    {
        bool active = false;
        double pos = 0.0;        // read position (absolute sample index)
        int n = 0, len = 1;      // where in its window, its length
        int fade = 1;            // its fades in and out (Grains: half its length - a Hann window)
    };

    // one set of heads reading channels [first, first + count) of the line
    struct Shifter
    {
        std::array<Head, kMaxHeads> heads {};
        int newest = -1;
        int untilNext = 0;
        juce::Random rng;
    };

    void startHead (Shifter& s, int first, int count, double r, int grain, bool correlated, float jitter);
    float readLine (int channel, double pos) const noexcept;
    float readLinear (int first, int count, double pos) const noexcept;
    void processShifter (Shifter& s, int first, int count, float* out, double r, int grain, bool correlated, float jitter);
    void designAntiAlias (double r);
    double nominalDelay (double r, int len, int grain, bool correlated) const;
    int correlationLength (int grain) const;
    int searchFor (int grain) const;
    int crossfadeSamples() const;

    std::vector<float> table;          // kPhases * kTaps windowed sinc
    std::vector<float> line[2];        // the delay line, both channels
    std::vector<float> templ;          // the correlation search's template
    Shifter linked, wideL, wideR;
    juce::int64 writeIndex { 0 };
    int mask { 0 };
    int searchSamples { 400 }, margin { 16 };

    // the anti-alias low-pass on what goes into the line (r > 1), per channel
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
    std::array<std::array<Biquad, 4>, 2> aa {};
    double aaRatio { 1.0 };
    float aaBlend { 0.0f };   // how much of the low-pass goes into the line (0 at 0 st and below)

    // the dry sound, delayed for Dry Delay Match
    std::vector<float> dryLine[2];
    double smoothedRatio { 1.0 }, lastDelay { 0.0 };
    float smoothedMix { 1.0f };
    float fbSample[2] {};
    int blockCounter { 0 };
};
