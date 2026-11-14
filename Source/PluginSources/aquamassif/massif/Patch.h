#pragma once
// Massif - the sound: every knob, selector and modulation of one patch.
//
// The knob list follows the classic 8-macro / 3-oscillator / 2-filter
// wavetable synth layout one to one (index = position in the host
// automation list of the original), so a sound read from a preset file maps
// straight onto it. Values are normalized 0..1 like the original's knobs;
// what each value means (Hz, seconds, dB) is in the DSP, measured.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace massif
{
// ---------------------------------------------------------------------------
// continuous knobs (all modulatable)
enum Knob : int
{
    kMacro1 = 0, kMacro2, kMacro3, kMacro4, kMacro5, kMacro6, kMacro7, kMacro8,
    kOsc1Pitch = 8, kOsc1Pos, kOsc1Intensity, kOsc1Amp, kOsc1Route,
    kOsc2Pitch = 13, kOsc2Pos, kOsc2Intensity, kOsc2Amp, kOsc2Route,
    kOsc3Pitch = 18, kOsc3Pos, kOsc3Intensity, kOsc3Amp, kOsc3Route,
    kNoiseColor = 23, kNoiseAmp, kNoiseRoute,
    kFbAmp = 26, kFbRoute,
    kModOscPitch = 28, kModOscRM, kModOscPhase, kModOscPos, kModOscFltFM,
    kF1Cut = 33, kF1Bw, kF1Res, kF2Cut, kF2Bw, kF2Res,
    kSerPar = 39,
    kIns1P1 = 40, kIns1P2, kIns2P1, kIns2P2,
    kFx1DW = 44, kFx1P2, kFx1P3, kFx1P4, kFx2DW, kFx2P2, kFx2P3, kFx2P4,
    kF1Gain = 52, kF2Gain, kFltMix, kBypassGain, kPan, kMasterVol,
    kMod5Rate = 58, kMod6Rate, kMod7Rate, kMod8Rate,
    kMod5Amp = 62, kMod6Amp, kMod7Amp, kMod8Amp,
    kMod5Morph = 66, kMod6Morph, kMod7Morph, kMod8Morph,     // LFO xfade / stepper glide / performer morph
    kMod5IntLevel = 70, kMod6IntLevel, kMod7IntLevel, kMod8IntLevel, // (internal env amount)
    kMod5Sync = 74, kMod6Sync, kMod7Sync, kMod8Sync,          // switches stored as knobs
    kMod5Restart = 78, kMod6Restart, kMod7Restart, kMod8Restart,
    kEnv1Vel = 82, kEnv2Vel, kEnv3Vel, kEnv4Vel,
    kEnv1Ktr = 86, kEnv2Ktr, kEnv3Ktr, kEnv4Ktr,
    kEnv1Delay = 90, kEnv2Delay, kEnv3Delay, kEnv4Delay,
    kEnv1AttT = 94, kEnv2AttT, kEnv3AttT, kEnv4AttT,
    kEnv1AttL = 98, kEnv2AttL, kEnv3AttL, kEnv4AttL,
    kEnv1DecT = 102, kEnv2DecT, kEnv3DecT, kEnv4DecT,
    kEnv1DecL = 106, kEnv2DecL, kEnv3DecL, kEnv4DecL,
    kEnv1SusT = 110, kEnv2SusT, kEnv3SusT, kEnv4SusT,
    kEnv1SusL = 114, kEnv2SusL, kEnv3SusL, kEnv4SusL,
    kEnv1Morph = 118, kEnv2Morph, kEnv3Morph, kEnv4Morph,
    kEnv1RelT = 122, kEnv2RelT, kEnv3RelT, kEnv4RelT,
    kGlide = 126, kVibRate, kVibDepth,
    kUniPitch = 129, kUniWtPos, kUniPan,
    kEqLow = 132, kEqPeak, kEqPeakFreq, kEqHigh,
    kEnvHoldReset = 136,
    kNumKnobs = 137
};

// ---------------------------------------------------------------------------
// selector option lists (menu order)
enum OscMode { omSpectrum = 0, omBendPlus, omBendMinusPlus, omBendMinus, omFormant, omNum };
enum FilterType { ftNone = 0, ftLP4, ftLP2, ftHP4, ftHP2, ftAllpass, ftDoubleNotch, ftBandpass, ftBandreject,
                  ftScream, ftDaft, ftComb, ftAcid, ftNum };
enum InsertType { itNone = 0, itDelay, itSampleHold, itBitcrush, itFreqShift, itHPLP, itSineShaper, itParabolicShaper,
                  itClip, itNum };
enum InsertPos { ipOff = 0, ipPreF1, ipPreF2, ipPostF1, ipSerial, ipPostF2, ipFbLoop, ipPreAmp, ipNum };
enum FxType { fxNone = 0, fxReverb, fxReverbSmall, fxFlangerPlus, fxFlangerPlus1, fxFlangerMinus, fxFlangerMinus1,
              fxChorus, fxChorus1, fxChorusE, fxPhaser, fxPhaser1, fxDelay, fxDelaySync, fxDimExp, fxClassicTube,
              fxTubeTube, fxBrightTube, fxNum };
enum FbPos { fbOff = 0, fbPostF1, fbPostF2, fbPreAmp, fbPostAmp, fbNum };
enum BypassTarget { btFx1 = 0, btFx2, btEq, btNum };
enum VoiceMode { vmPoly = 0, vmMono, vmMonoRotate };
enum TriggerMode { tmAlways = 0, tmLegato, tmLegatoTrill };
enum ModType { mtLFO = 0, mtStepper, mtPerformer };
enum EnvPlay { epGate = 0, epOneShot, epHold };

// ---------------------------------------------------------------------------
// modulation sources
enum ModSource : int
{
    msNone = -1,
    msEnv1 = 0, msEnv2, msEnv3, msEnv4,
    msMod5, msMod6, msMod7, msMod8,
    msMacro1, msMacro2, msMacro3, msMacro4, msMacro5, msMacro6, msMacro7, msMacro8,
    msKeyTrack, msVelocity, msAftertouch, msTrigRandom,
    msOscEnv,             // the OSC page's internal envelope
    msIntEnv5, msIntEnv6, msIntEnv7, msIntEnv8,   // the mod pages' internal (attack / decay) envelopes
    msVibEnv,             // stored source 12 (seen on the vibrato knobs)
    msNumSources
};
inline bool isBipolarSource (int s) { return s >= msMod5 && s <= msMod8; }

struct ModSlot
{
    int source { msNone };
    float amount { 0.0f };          // in knob units (-1..1 spans the whole knob)
};

// up to two normal slots and a third that can work as a side chain
struct KnobMod
{
    std::array<ModSlot, 3> slot {};
    bool sideChain { false };       // slot 2 multiplies slots 0/1
    std::array<bool, 2> scTarget { { true, true } };  // which slots the side chain acts on
};

// ---------------------------------------------------------------------------
// LFO curve shapes: the original's menu - 0 sine, 1 saw (falling), 2 square, 3 triangle, then the 31 tables
// of Curves.h (index 4 + table). The other names are the nearest tables, kept for the generator.
enum CurveShape : int { csSine = 0, csSaw, csSquare, csTriangle, csTable = 4, csNum = 35,
                        csFall1 = 4, csRandomSteps1 = 9, csBending1 = 13, csNoise = 22, csExpDown = 23, csExpDown4 = 24,
                        csExpShuffle = 25, csTripletRamps = 26, csPulse25 = 27, csVibrato = 28, csFlat = 30, csRampUp = 31,
                        csSineUni = 32, csTriangleUni = 33, csHalfSine = 34,
                        csRampDown = csRampUp, csStairs = csRandomSteps1, csExpUp = csExpDown4 };
// envelope sustain-stage shapes: the original's 22 (Curves.h, envTables), stored by that index
enum EnvShape : int { esLinear = 0, esSinoid, esExtreme1, esExtreme2, esCurve1, esCurve2, esCurve3, esCurve4, esCurve5, esCurve6,
                      esSteps, esTriangle15, esTriangle25, esTriangle35, esSine15, esSine25, esSine35, esExpo2, esExpo4,
                      esPeakExpo2, esPeakExpo4, esSwitch, esNum };
// performer curves: the original's 16 (unipolar) - see Curves.h
enum PerfCurve : int { pcUpLin = 0, pcDownLin, pcUpX4, pcDownX4, pcUpArcTan, pcDownArcTan, pcUpSinHalf, pcDownSinHalf,
                       pcBar, pcRoundedBlock, pcTriangle, pcSinus, pcDown1x2, pcDown2x2, pcDown3x2, pcDown4x2, pcNum };

struct EnvExtra
{
    int playMode { epGate };
    bool retrigger { true };        // "Trg Zero Reset": restart from zero
    bool linearDecay { false };
    int loopCount { 0 };            // 0 = sustain stage skipped, 33 = infinite
    int morphShapeA { esLinear };
    int morphShapeB { esSinoid };
};

struct LfoExtra
{
    int shapeA { csSine };          // upper curve (XFade fader up)
    int shapeB { csSine };          // lower curve
    float phaseA { 0.0f }, phaseB { 0.0f };   // curve offsets in cycles
    bool mono { false };
};

struct StepperExtra
{
    std::array<float, 16> step {};
    std::array<bool, 16> glideOn {};       // steps that follow the Glide fader
    std::array<bool, 16> ampOn {};         // steps that follow the Amp Mod fader
    int loopStart { 0 }, loopEnd { 15 };
    int startStep { 0 };                   // the play marker
    bool mono { false };
};

struct PerformerExtra
{
    std::array<int, 16> curveA {};
    std::array<int, 16> curveB {};
    std::array<float, 16> levelA {};
    std::array<float, 16> levelB {};
    std::array<bool, 16> morphOn {};       // steps that follow the XFade Seq fader
    std::array<bool, 16> ampOn {};         // steps that follow the Amp Mod fader
    int loopStart { 0 }, loopEnd { 15 };
    int startStep { 0 };
    bool mono { false };
};

struct KeyCurve           // keytracking response: 5 breakpoints (in, out) 0..127
{
    std::array<float, 5> x { { 0.0f, 32.0f, 64.0f, 96.0f, 127.0f } };
    std::array<float, 5> y { { 0.0f, 32.0f, 64.0f, 96.0f, 127.0f } };
    float eval (float note) const
    {
        if (note <= x[0]) return y[0];
        for (int i = 0; i < 4; ++i)
            if (note <= x[(size_t) i + 1])
            {
                const float t = (note - x[(size_t) i]) / std::max (1.0e-3f, x[(size_t) i + 1] - x[(size_t) i]);
                return y[(size_t) i] + t * (y[(size_t) i + 1] - y[(size_t) i]);
            }
        return y[4];
    }
};

// ---------------------------------------------------------------------------
struct Patch
{
    std::string name { "Init" };
    std::array<float, kNumKnobs> knob {};
    std::array<KnobMod, kNumKnobs> mod {};
    std::array<KnobMod, 2> ampMod {};       // amp section: [0] two multiplying slots
    std::array<std::string, 8> macroName {};

    // selectors
    std::array<int, 3> wavetable { { 2, 2, 2 } };
    std::array<int, 3> oscMode { { 0, 0, 0 } };
    std::array<bool, 3> oscOn { { true, true, true } };
    bool modOscOn { true };
    int rmTarget { 0 }, pmTarget { 0 }, posTarget { 0 }, fmTarget { 0 };   // 0 = off, 1..3 osc / 1..2 filter
    bool noiseOn { true }; int noiseType { 0 };
    bool fbOn { true }; int fbPos { fbPostAmp };
    std::array<bool, 2> filterOn { { true, true } };
    std::array<int, 2> filterType { { ftNone, ftNone } };
    std::array<bool, 2> insertOn { { true, true } };
    std::array<int, 2> insertType { { itNone, itNone } };
    std::array<int, 2> insertPos { { ipPostF1, ipPreAmp } };
    std::array<bool, 2> fxOn { { true, true } };
    std::array<int, 2> fxType { { fxNone, fxNone } };
    std::array<std::array<int, 4>, 2> fxSync { { { { 1, 4, 1, 4 } }, { { 1, 4, 1, 4 } } } };  // delay sync L num/den, R num/den
    bool ampOn { true }, eqOn { false }, bypassOn { true };
    int bypassSource { 0 }, bypassTarget { btFx1 };
    int maxVoices { 16 }, unison { 1 };
    bool uniPitchOn { false }, uniWtOn { false }, uniPanOn { false };
    bool uniChordMode { false };
    float uniRangeUp { 1.0f }, uniRangeDown { 0.0f };   // the pitch spread's range in semitones (spread = down + fader x (up - down))        // semitones spanned by the pitch spread fader
    int voiceMode { vmPoly }, triggerMode { tmAlways };
    bool vibMono { false };
    std::array<int, 4> modType { { mtLFO, mtLFO, mtLFO, mtStepper } };  // sources 5..8
    std::array<int, 4> syncNum { { 1, 1, 1, 1 } }, syncDen { { 1, 1, 1, 1 } };

    // preset-only data
    std::array<EnvExtra, 4> env {};
    std::array<LfoExtra, 4> lfo {};
    std::array<StepperExtra, 4> stepper {};
    std::array<PerformerExtra, 4> performer {};
    std::array<float, 5> intAttack { { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f } };   // internal envelopes: pages 5..8, vibrato
    std::array<float, 5> intDecay { { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f } };
    bool glideRateMode { false };
    float bendUp { 2.0f }, bendDown { -2.0f };
    bool restartPhase { true };
    std::array<float, 4> oscPhase {};          // osc1..3, mod osc start phase 0..1
    float oscEnvAttack { 0.0f }, oscEnvDecay { 0.5f };
    std::array<int, 5> keyTrackOsc { { 1, 1, 1, 1, 1 } };   // per osc1..3, modosc, inserts: 0 none, 1 linear, 2 user
    KeyCurve oscUserCurve;
    std::array<int, 2> keyTrackFilter { { 1, 1 } };          // 0 none, 1 set1, 2 set2
    std::array<KeyCurve, 2> filterCurveCut, filterCurveRes;
    float globalTune { 0.0f };       // semitones
    // Settings of the original's files that Massif keeps, to write them back on export, without playing them: the
    // bytes 12 and 11 before the first keytracking curve (hidden in the original's pages; they only move the
    // free-running oscillators' phases). Empty: not known (a sound made in Massif).
    std::vector<uint8_t> nmsvKeep;
    float presetBpm { 120.0f };
    bool highQuality { true };

    Patch() { setDefaults(); }

    void setDefaults()
    {
        static const float defaults[kNumKnobs] = {
            0,0,0,0,0,0,0,0,
            0.5f,0.5f,1,1,0.5f,  0.5f,0,1,0,0.5f,  0.5f,0,1,0,0.5f,
            0,0,0.5f, 0,0.5f,
            0.5f,0,0,0,0,
            0,0,0, 0,0,0, 0,
            0,0,0,0,
            0,0.5f,0.5f,0.5f, 0,0.5f,0.5f,0.5f,
            0.8f,0.8f,0.5f,0,0.5f,0.5f,
            0.5f,0.5f,0.5f,0.5f, 1,1,1,1, 1,1,1,1, 0,0,0,0, 0,0,0,0, 1,1,1,1,
            0,0,0,0, 0,0,0,0, 0,0,0,0,
            0.05f,0.05f,0.05f,0.05f, 1,1,1,1, 0.5f,0.5f,0.5f,0.5f, 0.5f,0.5f,0.5f,0.5f,
            0.5f,0.5f,0.5f,0.5f, 1,1,1,1, 0,0,0,0, 0.1f,0.1f,0.1f,0.1f,
            0,0,0, 0,0,0.5f, 0.5f,0.5f,0.5f,0.5f, 0 };
        for (int i = 0; i < kNumKnobs; ++i) knob[(size_t) i] = defaults[i];
        for (auto& m : mod) m = {};
        ampMod = {};
        ampMod[0].slot[0] = { msEnv4, 1.0f };
        // stepper default pattern: a rising line
        for (auto& s : stepper)
            for (int i = 0; i < 16; ++i) { s.step[(size_t) i] = (float) i / 15.0f; s.ampOn[(size_t) i] = false; s.glideOn[(size_t) i] = false; }
        for (auto& p : performer)
            for (int i = 0; i < 16; ++i) { p.curveA[(size_t) i] = pcDownLin; p.curveB[(size_t) i] = pcDownArcTan; p.levelA[(size_t) i] = 1; p.levelB[(size_t) i] = 1; p.morphOn[(size_t) i] = true; p.ampOn[(size_t) i] = false; }
        intAttack.fill (0.5f); intDecay.fill (0.5f);
    }
};
} // namespace massif
