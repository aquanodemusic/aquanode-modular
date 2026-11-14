#pragma once
// Massif - the built-in sounds.
//
// These are Massif's own patches, written here as code (none of them is
// taken from another synth's sound library). They use the table slots whose
// generated stand-ins are plain shapes or recipes, so they play with or
// without a wavetable folder.
#include "Patch.h"
#include <cmath>
#include <functional>
#include <vector>

namespace massif
{
struct FactoryPatch { const char* name; const char* category; std::function<void (Patch&)> make; };

namespace fac
{
    inline float semi (float s) { return 0.5f + s / 128.0f; }                              // pitch knob
    inline float cut (float hzAtC4) { return 0.5f + std::log2 (hzAtC4 / 1045.0f) / 10.0f; }   // cutoff knob
    inline void mod (Patch& p, int knob, int slot, int src, float amt) { p.mod[(size_t) knob].slot[(size_t) slot] = { src, amt }; }
    inline void osc (Patch& p, int o, int table, float semis, float pos, float amp, int mode = omSpectrum, float inten = 1.0f)
    {
        const int b = kOsc1Pitch + 5 * o;
        p.wavetable[(size_t) o] = table; p.oscMode[(size_t) o] = mode; p.oscOn[(size_t) o] = amp > 0.0f;
        p.knob[(size_t) b] = semi (semis); p.knob[(size_t) b + 1] = pos; p.knob[(size_t) b + 2] = inten; p.knob[(size_t) b + 3] = amp; p.knob[(size_t) b + 4] = 1.0f;
    }
    // everything through filter 1
    inline void oneFilter (Patch& p, int type, float cutoff, float res, float bw = 0.5f)
    {
        p.filterType[0] = type; p.knob[kF1Cut] = cutoff; p.knob[kF1Res] = res; p.knob[kF1Bw] = bw;
        p.knob[kFltMix] = 1.0f; p.knob[kNoiseRoute] = 1.0f; p.knob[kFbRoute] = 1.0f;
    }
    // envelope e (0..3): attack, decay, sustain level, release (knob values)
    inline void env (Patch& p, int e, float a, float d, float s, float r)
    {
        p.knob[(size_t) (kEnv1AttT + e)] = a; p.knob[(size_t) (kEnv1DecT + e)] = d; p.knob[(size_t) (kEnv1DecL + e)] = s; p.knob[(size_t) (kEnv1RelT + e)] = r;
        p.knob[(size_t) (kEnv1AttL + e)] = 1.0f;
    }
    // the recipes' LFO rates were written against an earlier (wrong) rate law; this keeps their speeds:
    // the old law in Hz (0.05 / 1.66 / 3.88 / 11.3 / 32.7 at 0, .25, .5, .75, 1), mapped onto the measured one
    inline float lfoRateKnob (float oldK)
    {
        static const float x[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, y[5] = { 0.05f, 1.66f, 3.88f, 11.3f, 32.7f };
        oldK = std::clamp (oldK, 0.0f, 1.0f); int i = 0; while (i < 3 && oldK > x[i + 1]) ++i;
        const float f = (oldK - x[i]) / (x[i + 1] - x[i]);
        const float hz = std::exp (std::log (y[i]) + f * (std::log (y[i + 1]) - std::log (y[i])));
        return std::clamp (0.5f + std::log2 (hz / 1.0221f) / 12.0f, 0.0f, 1.0f);
    }
    inline void lfo (Patch& p, int m, float rate, int shapeA = csSine, int shapeB = csSine, float morph = 1.0f)
    {
        p.modType[(size_t) m] = mtLFO; p.knob[(size_t) (kMod5Rate + m)] = lfoRateKnob (rate); p.knob[(size_t) (kMod5Amp + m)] = 1.0f; p.knob[(size_t) (kMod5Morph + m)] = morph;
        p.lfo[(size_t) m].shapeA = shapeA; p.lfo[(size_t) m].shapeB = shapeB;
    }
    inline void synced (Patch& p, int m, int num, int den) { p.knob[(size_t) (kMod5Sync + m)] = 1.0f; p.syncNum[(size_t) m] = num; p.syncDen[(size_t) m] = den; }
    inline void fx (Patch& p, int i, int type, float wet, float a = 0.5f, float b = 0.5f, float c = 0.5f)
    {
        p.fxType[(size_t) i] = type; const int k = kFx1DW + 4 * i;
        p.knob[(size_t) k] = wet; p.knob[(size_t) k + 1] = a; p.knob[(size_t) k + 2] = b; p.knob[(size_t) k + 3] = c;
    }
    inline void macro (Patch& p, int m, const char* name, float value = 0.0f) { p.macroName[(size_t) m] = name; p.knob[(size_t) (kMacro1 + m)] = value; }
    inline void unison (Patch& p, int voices, float pitch, float pan)
    {
        p.unison = voices; p.uniPitchOn = pitch > 0; p.knob[kUniPitch] = pitch; p.uniPanOn = pan != 0.5f; p.knob[kUniPan] = pan;
    }
    inline void mono (Patch& p, float glide, int trigger = tmLegato) { p.voiceMode = vmMono; p.triggerMode = trigger; p.knob[kGlide] = glide; }
    inline void stepper (Patch& p, int m, std::initializer_list<float> steps, int num, int den, float glide = 0.0f)
    {
        p.modType[(size_t) m] = mtStepper; auto& s = p.stepper[(size_t) m]; int i = 0;
        for (float v : steps) { if (i < 16) { s.step[(size_t) i] = v; s.ampOn[(size_t) i] = false; s.glideOn[(size_t) i] = glide > 0; } ++i; }
        s.loopStart = 0; s.loopEnd = std::max (0, std::min (15, i - 1));
        p.knob[(size_t) (kMod5Amp + m)] = 1.0f; p.knob[(size_t) (kMod5Morph + m)] = glide; synced (p, m, num, den);
    }
}

inline const std::vector<FactoryPatch>& factoryPatches()
{
    using namespace fac;
    static const std::vector<FactoryPatch> list {
        // ---------------------------------------------------------------- bass
        { "Deep Current", "Bass", [] (Patch& p)
          { osc (p, 0, 6, -12, 0.15f, 1.0f); osc (p, 1, 2, -12, 0.0f, 0.45f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP4, cut (700), 0.12f); env (p, 0, 0.0f, 0.58f, 0.25f, 0.3f); mod (p, kF1Cut, 0, msEnv1, 0.22f);
            env (p, 3, 0.02f, 0.6f, 0.85f, 0.32f); mono (p, 0.18f);
            macro (p, 0, "Cutoff"); mod (p, kF1Cut, 1, msMacro1, 0.35f); macro (p, 1, "Square"); mod (p, kOsc2Amp, 0, msMacro2, 0.5f); } },
        { "Rubber Keel", "Bass", [] (Patch& p)
          { osc (p, 0, 2, -12, 0.8f, 1.0f); osc (p, 1, 2, -24, 0.0f, 0.6f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP2, cut (500), 0.35f); env (p, 0, 0.0f, 0.52f, 0.0f, 0.3f); mod (p, kF1Cut, 0, msEnv1, 0.42f);
            mod (p, kF1Cut, 1, msVelocity, 0.12f); env (p, 3, 0.0f, 0.62f, 0.7f, 0.3f);
            p.insertType[0] = itSineShaper; p.insertPos[0] = ipPostF1; p.knob[kIns1P1] = 0.4f; p.knob[kIns1P2] = 0.35f;
            macro (p, 0, "Snap"); mod (p, kEnv1DecT, 0, msMacro1, -0.2f); macro (p, 1, "Drive"); mod (p, kIns1P2, 0, msMacro2, 0.5f); } },
        { "Acid Tide", "Bass", [] (Patch& p)
          { osc (p, 0, 2, -12, 1.0f, 1.0f); osc (p, 1, 2, 0, 0, 0); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftAcid, cut (600), 0.72f); env (p, 0, 0.0f, 0.55f, 0.0f, 0.3f); mod (p, kF1Cut, 0, msEnv1, 0.34f);
            mod (p, kF1Cut, 1, msVelocity, 0.2f); env (p, 3, 0.0f, 0.6f, 0.75f, 0.28f); mono (p, 0.22f);
            fx (p, 0, fxClassicTube, 0.5f, 0.45f); fx (p, 1, fxDelaySync, 0.18f, 0.5f, 0.45f); p.fxSync[1] = { { 3, 16, 3, 16 } };
            macro (p, 0, "Cutoff"); mod (p, kF1Cut, 2, msMacro1, 0.4f); macro (p, 1, "Reso"); mod (p, kF1Res, 0, msMacro2, 0.25f);
            macro (p, 2, "Square"); mod (p, kOsc1Pos, 0, msMacro3, -1.0f); macro (p, 3, "Decay"); mod (p, kEnv1DecT, 0, msMacro4, 0.3f); } },
        { "Growl Reef", "Bass", [] (Patch& p)
          { osc (p, 0, 51, -12, 0.3f, 1.0f, omFormant, 0.35f); osc (p, 1, 2, -24, 0.0f, 0.5f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP4, cut (900), 0.3f); lfo (p, 0, 0.5f, csSine, csTriangle); synced (p, 0, 1, 4);
            mod (p, kOsc1Pos, 0, msMod5, 0.3f); mod (p, kF1Cut, 0, msMod5, 0.18f); mod (p, kOsc1Intensity, 0, msMod5, 0.15f);
            env (p, 3, 0.0f, 0.5f, 1.0f, 0.15f); mono (p, 0.2f);
            p.insertType[0] = itParabolicShaper; p.knob[kIns1P1] = 0.6f; p.knob[kIns1P2] = 0.5f;
            macro (p, 0, "Wobble"); mod (p, kOsc1Pos, 1, msMacro1, 0.4f); macro (p, 1, "Rate"); mod (p, kMod5Rate, 0, msMacro2, 0.4f);
            macro (p, 2, "Throat"); mod (p, kOsc1Intensity, 1, msMacro3, 0.4f); } },
        { "Hull Reese", "Bass", [] (Patch& p)
          { osc (p, 0, 2, -12.12f, 1.0f, 0.9f); osc (p, 1, 2, -11.88f, 1.0f, 0.9f); osc (p, 2, 6, -24, 0.0f, 0.6f);
            oneFilter (p, ftLP4, cut (420), 0.2f); lfo (p, 0, 0.12f); mod (p, kF1Cut, 0, msMod5, 0.08f);
            env (p, 3, 0.03f, 0.5f, 1.0f, 0.3f); p.voiceMode = vmMono; p.knob[kGlide] = 0.2f;
            fx (p, 0, fxDimExp, 0.3f, 0.5f);
            macro (p, 0, "Cutoff"); mod (p, kF1Cut, 1, msMacro1, 0.4f); macro (p, 1, "Detune"); mod (p, kOsc1Pitch, 0, msMacro2, -0.004f); mod (p, kOsc2Pitch, 0, msMacro2, 0.004f); } },
        { "Sonar Sub", "Bass", [] (Patch& p)
          { osc (p, 0, 6, -24, 0.0f, 1.0f); osc (p, 1, 2, 0, 0, 0); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP2, cut (500), 0.0f); env (p, 0, 0.0f, 0.42f, 0.0f, 0.3f); mod (p, kOsc1Pitch, 0, msEnv1, 0.19f);
            env (p, 3, 0.0f, 0.68f, 0.0f, 0.4f); p.voiceMode = vmMono;
            macro (p, 0, "Drop"); mod (p, kOsc1Pitch, 1, msMacro1, 0.1f); macro (p, 1, "Length"); mod (p, kEnv4DecT, 0, msMacro2, 0.25f); } },
        // ---------------------------------------------------------------- lead
        { "Cyan Saw", "Lead", [] (Patch& p)
          { osc (p, 0, 2, 0, 1.0f, 0.9f); osc (p, 1, 2, 12, 1.0f, 0.35f); osc (p, 2, 2, -12, 0.0f, 0.3f);
            oneFilter (p, ftLP2, cut (3200), 0.15f); env (p, 0, 0.0f, 0.5f, 0.3f, 0.3f); mod (p, kF1Cut, 0, msEnv1, 0.15f);
            env (p, 3, 0.02f, 0.5f, 0.9f, 0.3f); unison (p, 5, 0.35f, 0.85f); p.uniRangeUp = 0.5f; p.uniRangeDown = 0.0f;
            fx (p, 0, fxDelaySync, 0.2f, 0.45f, 0.4f); p.fxSync[0] = { { 1, 8, 3, 16 } }; fx (p, 1, fxReverb, 0.2f, 0.55f, 0.5f, 0.5f);
            p.knob[kVibDepth] = 0.0f; mod (p, kVibDepth, 0, msAftertouch, 0.2f);
            macro (p, 0, "Cutoff"); mod (p, kF1Cut, 1, msMacro1, 0.3f); macro (p, 1, "Width"); mod (p, kUniPitch, 0, msMacro2, 0.4f);
            macro (p, 2, "Echo"); mod (p, kFx1DW, 0, msMacro3, 0.4f); } },
        { "Signal Lamp", "Lead", [] (Patch& p)
          { osc (p, 0, 1, 0, 0.25f, 1.0f); osc (p, 1, 2, 0, 0.0f, 0.4f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP4, cut (4200), 0.1f); env (p, 0, 0.0f, 0.55f, 0.2f, 0.3f); mod (p, kOsc1Pos, 0, msEnv1, 0.45f);
            env (p, 3, 0.0f, 0.5f, 1.0f, 0.2f); mono (p, 0.32f); p.knob[kVibRate] = 0.55f; mod (p, kVibDepth, 0, msMacro4, 0.25f);
            fx (p, 0, fxDelaySync, 0.22f, 0.5f, 0.45f); p.fxSync[0] = { { 1, 4, 3, 8 } };
            macro (p, 0, "Sync"); mod (p, kOsc1Pos, 1, msMacro1, 0.5f); macro (p, 1, "Cutoff"); mod (p, kF1Cut, 0, msMacro2, -0.35f);
            macro (p, 2, "Glide"); mod (p, kGlide, 0, msMacro3, 0.3f); macro (p, 3, "Vibrato"); } },
        { "Periscope", "Lead", [] (Patch& p)
          { osc (p, 0, 0, 0, 0.3f, 1.0f); osc (p, 1, 0, 0.1f, 0.6f, 0.7f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftBandpass, cut (1500), 0.35f, 0.6f); lfo (p, 0, 0.3f, csTriangle, csTriangle);
            mod (p, kOsc1Pos, 0, msMod5, 0.25f); mod (p, kOsc2Pos, 0, msMod5, -0.25f); mod (p, kF1Cut, 0, msKeyTrack, 0.1f);
            env (p, 3, 0.03f, 0.5f, 1.0f, 0.25f); fx (p, 0, fxChorus, 0.35f, 0.4f, 0.5f); fx (p, 1, fxReverbSmall, 0.2f);
            macro (p, 0, "Pulse"); mod (p, kOsc1Pos, 1, msMacro1, 0.5f); macro (p, 1, "Focus"); mod (p, kF1Cut, 1, msMacro2, 0.3f); } },
        { "Glass Whistle", "Lead", [] (Patch& p)
          { osc (p, 0, 6, 12, 0.0f, 1.0f); osc (p, 1, 6, 24, 0.5f, 0.15f); osc (p, 2, 2, 0, 0, 0);
            p.noiseType = 0; p.knob[kNoiseAmp] = 0.18f; p.knob[kNoiseColor] = 0.85f;
            oneFilter (p, ftBandpass, cut (2100), 0.5f, 0.3f); env (p, 3, 0.25f, 0.5f, 1.0f, 0.3f);
            p.knob[kVibRate] = 0.6f; p.knob[kVibDepth] = 0.06f; mono (p, 0.3f);
            fx (p, 0, fxReverb, 0.35f, 0.7f, 0.5f, 0.6f);
            macro (p, 0, "Breath"); mod (p, kNoiseAmp, 0, msMacro1, 0.4f); macro (p, 1, "Vibrato"); mod (p, kVibDepth, 0, msMacro2, 0.2f); } },
        // ---------------------------------------------------------------- pad
        { "Tidal Pad", "Pad", [] (Patch& p)
          { osc (p, 0, 2, 0, 0.85f, 0.8f); osc (p, 1, 2, 0.07f, 0.6f, 0.8f); osc (p, 2, 6, -12, 0.3f, 0.5f);
            oneFilter (p, ftLP2, cut (1400), 0.1f); lfo (p, 0, 0.1f); mod (p, kF1Cut, 0, msMod5, 0.1f);
            lfo (p, 1, 0.16f, csTriangle, csTriangle); mod (p, kOsc1Pos, 0, msMod6, 0.15f); mod (p, kOsc2Pos, 0, msMod6, -0.2f);
            env (p, 3, 0.52f, 0.6f, 0.9f, 0.52f); unison (p, 3, 0.3f, 0.9f); p.uniRangeUp = 0.4f; p.uniRangeDown = 0.0f;
            fx (p, 0, fxChorusE, 0.45f, 0.35f, 0.55f); fx (p, 1, fxReverb, 0.4f, 0.75f, 0.5f, 0.5f);
            macro (p, 0, "Brightness"); mod (p, kF1Cut, 1, msMacro1, 0.35f); macro (p, 1, "Motion"); mod (p, kMod6Amp, 0, msMacro2, -0.8f);
            macro (p, 2, "Space"); mod (p, kFx2DW, 0, msMacro3, 0.4f); macro (p, 3, "Attack"); mod (p, kEnv4AttT, 0, msMacro4, -0.4f); } },
        { "Kelp Choir", "Pad", [] (Patch& p)
          { osc (p, 0, 51, 0, 0.2f, 0.9f); osc (p, 1, 51, -12, 0.6f, 0.7f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP2, cut (2600), 0.05f); lfo (p, 0, 0.14f, csTriangle, csSine);
            mod (p, kOsc1Pos, 0, msMod5, 0.3f); mod (p, kOsc2Pos, 0, msMod5, -0.3f);
            env (p, 3, 0.48f, 0.6f, 1.0f, 0.5f); unison (p, 2, 0.2f, 1.0f); p.uniRangeUp = 0.3f; p.uniRangeDown = 0.0f;
            fx (p, 0, fxDimExp, 0.4f, 0.6f); fx (p, 1, fxReverb, 0.45f, 0.8f, 0.6f, 0.45f);
            macro (p, 0, "Vowel"); mod (p, kOsc1Pos, 1, msMacro1, 0.6f); mod (p, kOsc2Pos, 1, msMacro1, 0.4f);
            macro (p, 1, "Dark"); mod (p, kF1Cut, 0, msMacro2, -0.3f); } },
        { "Night Dive", "Pad", [] (Patch& p)
          { osc (p, 0, 5, -12, 0.3f, 0.9f); osc (p, 1, 2, 0, 0.9f, 0.5f); osc (p, 2, 6, 7, 0.0f, 0.25f);
            p.filterType[0] = ftLP4; p.knob[kF1Cut] = cut (520); p.knob[kF1Res] = 0.25f;
            p.filterType[1] = ftBandpass; p.knob[kF2Cut] = cut (1800); p.knob[kF2Res] = 0.3f; p.knob[kF2Bw] = 0.4f; p.knob[kFltMix] = 0.7f;
            lfo (p, 0, 0.07f); mod (p, kF1Cut, 0, msMod5, 0.14f); lfo (p, 1, 0.11f, csTriangle, csTriangle); mod (p, kF2Cut, 0, msMod6, 0.2f);
            env (p, 3, 0.6f, 0.6f, 1.0f, 0.58f); fx (p, 0, fxPhaser, 0.3f, 0.2f, 0.6f, 0.4f); fx (p, 1, fxReverb, 0.5f, 0.85f, 0.6f, 0.35f);
            macro (p, 0, "Depth"); mod (p, kF1Cut, 1, msMacro1, 0.3f); macro (p, 1, "Shimmer"); mod (p, kFltMix, 0, msMacro2, -0.5f); } },
        { "Aurora", "Pad", [] (Patch& p)
          { osc (p, 0, 16, 0, 0.2f, 0.85f, omBendMinusPlus, 0.5f); osc (p, 1, 16, 12, 0.5f, 0.4f); osc (p, 2, 2, 0, 0.9f, 0.4f);
            oneFilter (p, ftHP2, cut (300), 0.1f); lfo (p, 0, 0.09f, csSine, csTriangle);
            mod (p, kOsc1Pos, 0, msMod5, 0.35f); mod (p, kOsc1Intensity, 0, msMod5, 0.2f); mod (p, kOsc2Pos, 0, msMod5, -0.3f);
            env (p, 3, 0.55f, 0.6f, 1.0f, 0.55f); unison (p, 2, 0.25f, 1.0f); p.uniRangeUp = 0.3f; p.uniRangeDown = 0.0f;
            fx (p, 0, fxDimExp, 0.5f, 0.7f); fx (p, 1, fxReverb, 0.45f, 0.8f, 0.5f, 0.7f);
            macro (p, 0, "Stops"); mod (p, kOsc1Pos, 1, msMacro1, 0.6f); macro (p, 1, "Bend"); mod (p, kOsc1Intensity, 1, msMacro2, 0.4f); } },
        // ---------------------------------------------------------------- pluck / keys
        { "Droplet", "Pluck", [] (Patch& p)
          { osc (p, 0, 5, 0, 0.5f, 1.0f); osc (p, 1, 6, 12, 0.0f, 0.3f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP2, cut (1500), 0.25f); env (p, 0, 0.0f, 0.55f, 0.0f, 0.4f); mod (p, kF1Cut, 0, msEnv1, 0.4f); mod (p, kF1Cut, 1, msVelocity, 0.15f);
            env (p, 3, 0.0f, 0.6f, 0.0f, 0.5f); fx (p, 0, fxDelaySync, 0.3f, 0.5f, 0.5f); p.fxSync[0] = { { 1, 8, 3, 16 } }; fx (p, 1, fxReverb, 0.3f, 0.7f);
            macro (p, 0, "Tone"); mod (p, kF1Cut, 2, msMacro1, 0.3f); macro (p, 1, "Length"); mod (p, kEnv4DecT, 0, msMacro2, 0.3f); mod (p, kEnv1DecT, 0, msMacro2, 0.3f); } },
        { "Buoy Bells", "Pluck", [] (Patch& p)
          { osc (p, 0, 6, 0, 0.0f, 1.0f); osc (p, 1, 6, 19, 0.0f, 0.3f); osc (p, 2, 2, 0, 0, 0);
            p.knob[kModOscPitch] = semi (31); p.pmTarget = 1; p.knob[kModOscPhase] = 0.0f;
            env (p, 0, 0.0f, 0.6f, 0.0f, 0.5f); mod (p, kModOscPhase, 0, msEnv1, 0.4f); mod (p, kModOscPhase, 1, msVelocity, 0.2f);
            oneFilter (p, ftNone, 0.5f, 0); env (p, 3, 0.0f, 0.72f, 0.0f, 0.62f); env (p, 1, 0.0f, 0.5f, 0.0f, 0.4f); mod (p, kOsc2Amp, 0, msEnv2, 0.3f);
            fx (p, 0, fxChorus, 0.3f, 0.3f, 0.4f); fx (p, 1, fxReverb, 0.4f, 0.8f, 0.5f, 0.6f);
            macro (p, 0, "Strike"); mod (p, kModOscPhase, 1, msMacro1, 0.4f); macro (p, 1, "Ratio"); mod (p, kModOscPitch, 0, msMacro2, 0.094f); } },
        { "Reef Keys", "Keys", [] (Patch& p)
          { osc (p, 0, 5, 0, 0.2f, 1.0f); osc (p, 1, 6, 12, 0.0f, 0.2f); osc (p, 2, 6, -12, 0.0f, 0.4f);
            oneFilter (p, ftLP2, cut (1200), 0.1f); env (p, 0, 0.0f, 0.6f, 0.1f, 0.4f); mod (p, kF1Cut, 0, msEnv1, 0.2f); mod (p, kF1Cut, 1, msVelocity, 0.25f);
            mod (p, kOsc1Pos, 0, msVelocity, 0.3f); env (p, 3, 0.0f, 0.74f, 0.0f, 0.42f); p.knob[kEnv4Vel] = 0.6f;
            fx (p, 0, fxChorus, 0.3f, 0.3f, 0.5f); fx (p, 1, fxReverbSmall, 0.2f);
            macro (p, 0, "Bark"); mod (p, kOsc1Pos, 1, msMacro1, 0.5f);
            macro (p, 1, "Autopan"); lfo (p, 0, 0.55f); p.knob[kMod5Amp] = 0.0f; mod (p, kMod5Amp, 0, msMacro2, 1.0f); mod (p, kPan, 0, msMod5, 0.35f); } },
        { "Comb Harp", "Pluck", [] (Patch& p)
          { for (int o = 0; o < 3; ++o) osc (p, o, 2, 0, 0, 0);
            // a burst of noise into the comb: the string
            p.knob[kNoiseAmp] = 0.0f; p.knob[kNoiseColor] = 0.8f; env (p, 0, 0.0f, 0.3f, 0.0f, 0.2f); mod (p, kNoiseAmp, 0, msEnv1, 1.0f);
            oneFilter (p, ftComb, 0.5f, 0.93f, 0.35f); env (p, 3, 0.0f, 0.78f, 0.0f, 0.6f);
            fx (p, 0, fxReverb, 0.3f, 0.7f, 0.5f, 0.6f);
            macro (p, 0, "Damping"); mod (p, kF1Bw, 0, msMacro1, 0.4f); macro (p, 1, "Pick"); mod (p, kNoiseColor, 0, msMacro2, -0.4f); } },
        // ---------------------------------------------------------------- sequences
        { "Stepper Shoal", "Sequence", [] (Patch& p)
          { osc (p, 0, 2, -12, 0.9f, 1.0f); osc (p, 1, 2, 0, 0.3f, 0.5f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP4, cut (300), 0.4f);
            stepper (p, 0, { 1.0f, 0.2f, 0.6f, 0.1f, 0.8f, 0.3f, 0.5f, 0.0f, 0.9f, 0.2f, 0.7f, 0.4f, 0.6f, 0.1f, 0.3f, 0.75f }, 1, 16);
            mod (p, kF1Cut, 0, msMod5, 0.4f); env (p, 3, 0.0f, 0.5f, 1.0f, 0.2f);
            fx (p, 0, fxDelaySync, 0.25f, 0.5f, 0.5f); p.fxSync[0] = { { 3, 16, 1, 8 } };
            macro (p, 0, "Depth"); mod (p, kMod5Amp, 0, msMacro1, -0.8f); macro (p, 1, "Cutoff"); mod (p, kF1Cut, 1, msMacro2, 0.3f);
            macro (p, 2, "Glide"); mod (p, kMod5Morph, 0, msMacro3, 0.8f); } },
        { "Gate Runner", "Sequence", [] (Patch& p)
          { osc (p, 0, 2, 0, 1.0f, 0.8f); osc (p, 1, 2, 0.08f, 1.0f, 0.8f); osc (p, 2, 2, -12, 0.0f, 0.4f);
            oneFilter (p, ftLP2, cut (2400), 0.15f);
            stepper (p, 0, { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0 }, 1, 16, 0.15f);
            p.ampMod[0].slot[1] = { msMod5, 1.0f }; env (p, 3, 0.1f, 0.5f, 1.0f, 0.35f);
            unison (p, 3, 0.3f, 0.9f); p.uniRangeUp = 0.4f; p.uniRangeDown = 0.0f;
            fx (p, 0, fxChorusE, 0.3f); fx (p, 1, fxReverb, 0.3f, 0.7f);
            macro (p, 0, "Cutoff"); mod (p, kF1Cut, 0, msMacro1, -0.4f); macro (p, 1, "Soft Gate"); mod (p, kMod5Morph, 0, msMacro2, 0.6f); } },
        { "Morse", "Sequence", [] (Patch& p)
          { osc (p, 0, 5, 0, 0.6f, 1.0f); osc (p, 1, 2, 0, 0, 0); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP2, cut (1600), 0.2f);
            stepper (p, 0, { 0.0f, 0.0f, 0.5833f, 0.0f, 1.0f, 0.5833f, 0.0f, 0.25f }, 1, 16);
            mod (p, kOsc1Pitch, 0, msMod5, 0.09375f); env (p, 0, 0.0f, 0.3f, 0.0f, 0.2f); mod (p, kF1Cut, 0, msEnv1, 0.25f);
            env (p, 3, 0.0f, 0.5f, 1.0f, 0.2f); fx (p, 0, fxDelaySync, 0.3f, 0.5f, 0.5f); p.fxSync[0] = { { 3, 16, 3, 16 } };
            macro (p, 0, "Range"); mod (p, kMod5Amp, 0, msMacro1, -1.0f); macro (p, 1, "Tone"); mod (p, kOsc1Pos, 0, msMacro2, 0.4f); } },
        // ---------------------------------------------------------------- effects
        { "Surf", "Effect", [] (Patch& p)
          { for (int o = 0; o < 3; ++o) osc (p, o, 2, 0, 0, 0);
            p.knob[kNoiseAmp] = 1.0f; p.knob[kNoiseColor] = 0.9f; oneFilter (p, ftBandpass, cut (900), 0.25f, 0.7f);
            lfo (p, 0, 0.08f, csTriangle, csSine); mod (p, kF1Cut, 0, msMod5, 0.25f); lfo (p, 1, 0.05f); mod (p, kPan, 0, msMod6, 0.3f);
            env (p, 3, 0.6f, 0.6f, 1.0f, 0.6f); fx (p, 1, fxReverb, 0.4f, 0.8f);
            macro (p, 0, "Wash"); mod (p, kF1Cut, 1, msMacro1, 0.4f); macro (p, 1, "Swell"); mod (p, kMod5Rate, 0, msMacro2, 0.4f); } },
        { "Pressure Drop", "Effect", [] (Patch& p)
          { osc (p, 0, 2, -12, 1.0f, 0.9f); osc (p, 1, 2, 0, 0, 0); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP4, cut (1200), 0.5f); p.fbPos = fbPostF1; p.knob[kFbAmp] = 0.55f;
            p.insertType[0] = itSampleHold; p.insertPos[0] = ipFbLoop; p.knob[kIns1P1] = 1.0f; p.knob[kIns1P2] = 0.6f;
            env (p, 0, 0.0f, 0.72f, 0.0f, 0.5f); mod (p, kF1Cut, 0, msEnv1, 0.3f); mod (p, kIns1P2, 0, msEnv1, 0.3f);
            env (p, 3, 0.0f, 0.75f, 0.0f, 0.5f);
            macro (p, 0, "Feedback"); mod (p, kFbAmp, 0, msMacro1, 0.35f); macro (p, 1, "Rate"); mod (p, kIns1P2, 1, msMacro2, -0.4f); } },
        { "Depth Charge", "Effect", [] (Patch& p)
          { osc (p, 0, 6, -12, 0.0f, 1.0f); osc (p, 1, 2, 0, 0, 0); osc (p, 2, 2, 0, 0, 0);
            p.knob[kNoiseAmp] = 0.0f; env (p, 1, 0.0f, 0.3f, 0.0f, 0.3f); mod (p, kNoiseAmp, 0, msEnv2, 0.6f);
            oneFilter (p, ftLP2, cut (700), 0.2f); env (p, 0, 0.0f, 0.55f, 0.0f, 0.5f); mod (p, kOsc1Pitch, 0, msEnv1, 0.3f); mod (p, kF1Cut, 0, msEnv1, 0.3f);
            env (p, 3, 0.0f, 0.74f, 0.0f, 0.55f); fx (p, 0, fxTubeTube, 0.5f, 0.6f); fx (p, 1, fxReverb, 0.35f, 0.9f, 0.6f, 0.3f);
            macro (p, 0, "Depth"); mod (p, kOsc1Pitch, 1, msMacro1, -0.1f); macro (p, 1, "Blast"); mod (p, kNoiseAmp, 1, msMacro2, 0.4f); } },
        { "Radio Static", "Effect", [] (Patch& p)
          { osc (p, 0, 2, 0, 0.5f, 0.9f); osc (p, 1, 2, 7, 0.9f, 0.5f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftBandpass, cut (1700), 0.3f, 0.35f);
            p.insertType[0] = itBitcrush; p.insertPos[0] = ipPreF1; p.knob[kIns1P1] = 0.8f; p.knob[kIns1P2] = 0.55f;
            p.insertType[1] = itFreqShift; p.insertPos[1] = ipPostF1; p.knob[kIns2P1] = 0.6f; p.knob[kIns2P2] = 0.42f;
            lfo (p, 0, 0.35f, csNoise, csSquare, 0.7f); mod (p, kIns2P2, 0, msMod5, 0.06f); mod (p, kF1Cut, 0, msMod5, 0.1f);
            env (p, 3, 0.0f, 0.5f, 1.0f, 0.2f);
            macro (p, 0, "Crush"); mod (p, kIns1P2, 0, msMacro1, 0.4f); macro (p, 1, "Drift"); mod (p, kIns2P2, 1, msMacro2, 0.3f); } },
        // ---------------------------------------------------------------- stabs / organ
        { "Harbor Stab", "Stab", [] (Patch& p)
          { osc (p, 0, 2, 0, 1.0f, 0.9f); osc (p, 1, 2, 0, 0.0f, 0.4f); osc (p, 2, 2, 0, 0, 0);
            oneFilter (p, ftLP2, cut (900), 0.2f); env (p, 0, 0.0f, 0.55f, 0.0f, 0.42f); mod (p, kF1Cut, 0, msEnv1, 0.38f);
            env (p, 3, 0.0f, 0.6f, 0.0f, 0.42f); unison (p, 3, 0.292f, 0.8f); p.uniChordMode = true; p.uniRangeUp = 24.0f; p.uniRangeDown = 0.0f;
            fx (p, 0, fxChorusE, 0.3f); fx (p, 1, fxReverb, 0.3f, 0.6f);
            macro (p, 0, "Cutoff"); mod (p, kF1Cut, 1, msMacro1, 0.3f); macro (p, 1, "Chord"); mod (p, kUniPitch, 0, msMacro2, 0.21f); } },
        { "Drawbar Deck", "Organ", [] (Patch& p)
          { osc (p, 0, 16, 0, 0.45f, 1.0f); osc (p, 1, 16, 12, 0.2f, 0.3f); osc (p, 2, 6, -12, 0.0f, 0.5f);
            oneFilter (p, ftLP2, cut (5200), 0.0f); env (p, 3, 0.0f, 0.5f, 1.0f, 0.08f);
            env (p, 0, 0.0f, 0.2f, 0.0f, 0.1f); mod (p, kOsc2Amp, 0, msEnv1, 0.35f);
            fx (p, 0, fxChorus, 0.5f, 0.75f, 0.45f); fx (p, 1, fxClassicTube, 0.35f, 0.4f);
            macro (p, 0, "Drawbars"); mod (p, kOsc1Pos, 0, msMacro1, 0.55f); macro (p, 1, "Rotor"); mod (p, kFx1P2, 0, msMacro2, -0.5f);
            macro (p, 2, "Click"); mod (p, kOsc2Amp, 1, msMacro3, 0.4f); } },
        { "Lighthouse", "Keys", [] (Patch& p)
          { osc (p, 0, 0, 0, 0.0f, 0.8f); osc (p, 1, 0, 0.06f, 0.55f, 0.6f); osc (p, 2, 6, -12, 0.0f, 0.45f);
            oneFilter (p, ftLP4, cut (1300), 0.18f); env (p, 0, 0.0f, 0.5f, 0.2f, 0.4f); mod (p, kF1Cut, 0, msEnv1, 0.25f); mod (p, kF1Cut, 1, msVelocity, 0.15f);
            lfo (p, 0, 0.22f, csTriangle, csTriangle); mod (p, kOsc2Pos, 0, msMod5, 0.2f);
            env (p, 3, 0.0f, 0.6f, 0.6f, 0.4f); fx (p, 0, fxChorusE, 0.35f); fx (p, 1, fxReverb, 0.25f, 0.6f);
            macro (p, 0, "Brass"); mod (p, kF1Cut, 2, msMacro1, 0.3f); macro (p, 1, "Pulse"); mod (p, kOsc1Pos, 0, msMacro2, 0.5f); } },
    };
    return list;
}

inline bool makeFactoryPatch (int index, Patch& p)
{
    const auto& l = factoryPatches();
    if (index < 0 || index >= (int) l.size()) return false;
    p = Patch();
    for (int o = 0; o < 3; ++o) p.knob[(size_t) (kOsc1Route + 5 * o)] = 1.0f;
    l[(size_t) index].make (p);
    p.name = l[(size_t) index].name;
    return true;
}
} // namespace massif
