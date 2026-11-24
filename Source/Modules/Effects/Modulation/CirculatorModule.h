#pragma once

#include "../../Core/ModuleCore.h"

// Circulator - phase dispersion: up to 64 identical second-order all-pass
// stages around one centre frequency. The spectrum stays as it was, but the
// phase is twisted hard around the centre, so a transient is smeared into a
// hollow, metallic "pew" that rings around that pitch - drums, plucks and
// bass stabs turn springy and laser-like. Focus is the stages' Q: low spreads
// the smear wide, high focuses it into a ringing chirp. Feedback runs the bank
// into itself (positive or negative), turning it into a steep, resonant phaser.
//
// The centre comes from Freq (Hz), Note (a pitch, with Fine +-12 semitones) or
// Key - the last note on its Midi In, so the smear follows a bass line.
// Spread moves the right channel's centre apart for width, Stagger spreads the
// stages over a range around the centre (a wider, softer smear).
// Global effect. Inputs: 0 = Audio In, 1 = Midi In. Output: 0 = Audio Out.
class CirculatorModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pMode = 0, pFreq, pNote, pFine, pFocus, pDepth, pFeedback, pSpread, pStagger, pMix, pEngine };

    static constexpr int kMaxStages = 64;
    static constexpr int kCtrl = 16;   // coefficients are worked out every kCtrl samples, glided between

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override;
    void midiAllNotesOff() override { heldCount = 0; }

private:
    struct Stage { float s1 { 0 }, s2 { 0 }; };
    struct Coef { float g { 0.05f }, k { 0.5f }, d { 1.0f }; };

    void updateCoefficients();
    double centreHz() const;
    void processOriginal (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs);

    // Engine Original: GullDSP's Circulate as it is (double precision, per-sample smoothing)
    struct OrigStage { double s1 { 0 }, s2 { 0 }; };
    OrigStage ost[2][kMaxStages];
    double oG { 0.0 }, oK { 0.5 }, oCoefSmooth { 0.001 }, oParamSmooth20 { 0.01 }, oParamSmooth10 { 0.02 };
    double oCentreNorm { 0.5 }, oFocus { 0.5 }, oFeedback { 0.0 };
    float oCurrent[2] {};
    int oStages { 32 };
    bool oSnap { true };

    Stage st[2][kMaxStages];
    Coef cur[2][kMaxStages], target[2][kMaxStages], step[2][kMaxStages];
    float lastOut[2] {};
    int activeStages { 32 };
    int ctrlCount { 0 };
    bool firstUpdate { true };
    double smoothedHz { 600.0 };
    float smCoef { 0.0f };

    // Key mode: the notes held on Midi In, last one wins
    int held[16] {};
    int heldCount { 0 };
    int keyNote { 60 };
};
