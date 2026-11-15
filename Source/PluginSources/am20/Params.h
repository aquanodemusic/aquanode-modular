#pragma once
// AM-20: the parameter list. Every value is 0..1, the way the original plugin
// stores its knobs, so its presets map one to one (see PresetIO.h for the
// original's chunk slots).
#include <cmath>
#include <cstdio>
#include <string>

namespace am20
{
enum Param
{
    // ---- the front panel (chunk slots 0..35, the same order) ----
    VCO1Wave = 0, VCO1PW, VCO1Scale, Porta, VCO2Wave, VCO2Tune, VCO2Scale, FineTune,
    VCO1Level, VCO2Level, PitchMG, PitchEG1,
    HPFCut, HPFPeak, HPFMG, HPFEG2,
    LPFCut, LPFPeak, LPFMG, LPFEG2,
    MGWave, MGFreq,
    EG1Delay, EG1Attack, EG1Release,
    EG2Hold, EG2Attack, EG2Decay, EG2Sustain, EG2Release,
    ESPLevel, ESPLowCut, ESPHighCut, ESPCVAdjust, ESPThreshold,
    Volume,
    // ---- the plugin's extra page ----
    Assign, Trigger, PolyVoices, UnisonVoices, UnisonDetune, UnisonSpread,
    Transpose, BendRange, Analog, Pan,
    MGTempoSync, MGKeySync, MGBaseNote, MGTimes,
    ExtSrc1, ExtSrc2,
    Ext1PW, Ext1VCO2, Ext1HPF, Ext1LPF, Ext1Amp,
    Ext2PW, Ext2VCO2, Ext2HPF, Ext2LPF, Ext2Amp,
    NumParams
};

// where each parameter lives in the original's program chunk (float slot, the
// chunk's 4-byte word index counted from byte 48), measured by changing one
// control at a time in the original and comparing the saved state
inline int chunkSlot (int p)
{
    if (p >= 0 && p <= Volume) return p;
    switch (p)
    {
        case Assign: return 72;       case Trigger: return 73;
        case PolyVoices: return 40;   case UnisonVoices: return 41;
        case UnisonDetune: return 39; case UnisonSpread: return 76;
        case Transpose: return 42;    case BendRange: return 43;
        case Analog: return 44;       case Pan: return 47;
        case MGTempoSync: return 48;  case MGKeySync: return 49;
        case MGBaseNote: return 50;   case MGTimes: return 51;
        case ExtSrc1: return 74;      case ExtSrc2: return 75;
        case Ext1PW: return 56;  case Ext1VCO2: return 57; case Ext1HPF: return 59; case Ext1LPF: return 61; case Ext1Amp: return 63;
        case Ext2PW: return 64;  case Ext2VCO2: return 65; case Ext2HPF: return 67; case Ext2LPF: return 69; case Ext2Amp: return 71;
        default: return -1;
    }
}

// the default sound: the original's own start-up state
inline float defaultValue (int p)
{
    switch (p)
    {
        case VCO1Wave: return 1.0f / 3.0f;   case VCO1Scale: return 2.0f / 3.0f;
        case Porta: return 0.28f;            case VCO2Tune: return 0.5f;
        case VCO2Scale: return 1.0f / 3.0f;  case FineTune: return 0.5f;
        case VCO1Level: return 0.95f;        case LPFCut: return 1.0f;
        case MGWave: return 0.5f;            case MGFreq: return 0.25f;
        case EG2Decay: return 0.5f;          case EG2Sustain: return 1.0f;
        case ESPLevel: return 0.84f;         case ESPHighCut: return 1.0f;
        case Volume: return 1.0f;
        case PolyVoices: return 7.0f / 31.0f;   // 8 voices
        case Assign: return 1.0f;            // AM-20 plays polyphonic by default
        case Transpose: return 0.5f;         case BendRange: return 1.0f / 6.0f;
        case Analog: return 0.05f;           case Pan: return 0.5f;
        case ExtSrc1: return 1.0f / 14.0f;   // Velocity
        case ExtSrc2: return 5.0f / 14.0f;   // Mod wheel
        case Ext1PW: case Ext1VCO2: case Ext1HPF: case Ext1LPF: case Ext1Amp:
        case Ext2PW: case Ext2VCO2: case Ext2HPF: case Ext2LPF: case Ext2Amp: return 0.5f;
        default: return 0.0f;
    }
}

inline const char* paramName (int p)
{
    static const char* n[NumParams] = {
        "VCO 1 Waveform", "VCO 1 Pulse Width", "VCO 1 Scale", "Portamento", "VCO 2 Waveform", "VCO 2 Pitch", "VCO 2 Scale", "Fine Tune",
        "VCO 1 Level", "VCO 2 Level", "Pitch LFO Amount", "Pitch Env 1 Amount",
        "HPF Cutoff", "HPF Resonance", "HPF LFO Amount", "HPF Env 2 Amount",
        "LPF Cutoff", "LPF Resonance", "LPF LFO Amount", "LPF Env 2 Amount",
        "LFO Waveform", "LFO Rate",
        "Env 1 Delay", "Env 1 Attack", "Env 1 Release",
        "Env 2 Hold", "Env 2 Attack", "Env 2 Decay", "Env 2 Sustain", "Env 2 Release",
        "ESP Signal Level", "ESP Low Cut", "ESP High Cut", "ESP CV Tune", "ESP Threshold",
        "Volume",
        "Voice Assign", "Trigger", "Poly Voices", "Unison Voices", "Unison Detune", "Unison Spread",
        "Transpose", "Bend Range", "Analog", "Pan",
        "LFO Tempo Sync", "LFO Key Sync", "LFO Base Note", "LFO Times",
        "Ext Mod Source 1", "Ext Mod Source 2",
        "Src 1 > VCO1 PW", "Src 1 > VCO2 Pitch", "Src 1 > HPF", "Src 1 > LPF", "Src 1 > Amp",
        "Src 2 > VCO1 PW", "Src 2 > VCO2 Pitch", "Src 2 > HPF", "Src 2 > LPF", "Src 2 > Amp" };
    return (p >= 0 && p < NumParams) ? n[p] : "";
}

// short ids for the XML presets (stable, never reordered)
inline const char* paramId (int p)
{
    static const char* n[NumParams] = {
        "vco1Wave", "vco1Pw", "vco1Scale", "portamento", "vco2Wave", "vco2Pitch", "vco2Scale", "fineTune",
        "vco1Level", "vco2Level", "pitchMg", "pitchEg1",
        "hpfCutoff", "hpfPeak", "hpfMg", "hpfEg2",
        "lpfCutoff", "lpfPeak", "lpfMg", "lpfEg2",
        "mgWave", "mgFreq",
        "eg1Delay", "eg1Attack", "eg1Release",
        "eg2Hold", "eg2Attack", "eg2Decay", "eg2Sustain", "eg2Release",
        "espLevel", "espLowCut", "espHighCut", "espCvAdjust", "espThreshold",
        "volume",
        "assign", "trigger", "polyVoices", "unisonVoices", "unisonDetune", "unisonSpread",
        "transpose", "bendRange", "analog", "pan",
        "mgTempoSync", "mgKeySync", "mgBaseNote", "mgTimes",
        "extSrc1", "extSrc2",
        "src1Pw", "src1Vco2", "src1Hpf", "src1Lpf", "src1Amp",
        "src2Pw", "src2Vco2", "src2Hpf", "src2Lpf", "src2Amp" };
    return (p >= 0 && p < NumParams) ? n[p] : "";
}

// ---- stepped controls ----
inline int vco1WaveIndex (float v) { return v < 0.25f ? 0 : v < 0.55f ? 1 : v < 0.8f ? 2 : 3; }   // Tri, Saw, Pulse, Noise
inline int vco2WaveIndex (float v) { return v < 0.25f ? 0 : v < 0.55f ? 1 : v < 0.8f ? 2 : 3; }   // Saw, Square, Pulse 10%, Ring
inline int scaleIndex (float v)    { return v < 0.25f ? 0 : v < 0.55f ? 1 : v < 0.8f ? 2 : 3; }
inline int polyVoices (float v)    { return 1 + (int) std::lround (v * 31.0f); }
inline int unisonVoices (float v)  { return 1 + (int) std::lround (v * 15.0f); }
inline int transposeSemis (float v){ return (int) std::lround (v * 48.0f) - 24; }
inline float bendRangeSemis (float v) { return v * 12.0f; }
inline int extSourceIndex (float v){ return (int) std::lround (v * 14.0f); }
inline int mgBaseNoteIndex (float v){ return (int) std::lround (v * 9.0f); }   // 1/1 .. 1/32
inline int mgTimes (float v)       { return 16 - (int) std::lround (v * 15.0f); }

inline const char* extSourceName (int i)
{
    static const char* n[15] = { "None", "Velocity", "KBD Track", "Pressure", "Pitch Bend", "Mod Wheel", "Breath",
                                 "CC 16", "CC 17", "LFO Tri", "LFO Pulse", "Env 1", "Env 1 Inv", "Env 2", "Env 2 Inv" };
    return (i >= 0 && i < 15) ? n[i] : "";
}

inline std::string displayText (int p, float v)
{
    char b[48];
    auto knob = [&] { std::snprintf (b, sizeof b, "%.2f", v * 10.0f); return std::string (b); };
    auto bip = [&] { std::snprintf (b, sizeof b, "%+.2f", (v - 0.5f) * 10.0f); return std::string (b); };
    static const char* w1[4] = { "Triangle", "Saw", "Pulse", "Noise" };
    static const char* w2[4] = { "Saw", "Square", "Pulse 10%", "Ring" };
    static const char* s1[4] = { "32'", "16'", "8'", "4'" };
    static const char* s2[4] = { "16'", "8'", "4'", "2'" };
    static const char* notes[10] = { "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    switch (p)
    {
        case VCO1Wave: return w1[vco1WaveIndex (v)];
        case VCO2Wave: return w2[vco2WaveIndex (v)];
        case VCO1Scale: return s1[scaleIndex (v)];
        case VCO2Scale: return s2[scaleIndex (v)];
        case VCO2Tune: std::snprintf (b, sizeof b, "%+.2f st", (v - 0.5f) * 24.0f); return b;
        case FineTune: std::snprintf (b, sizeof b, "%+.0f ct", (v - 0.5f) * 200.0f); return b;
        case Assign: return v >= 0.5f ? "Poly" : "Mono";
        case Trigger: return v >= 0.5f ? "Multi" : "Single";
        case PolyVoices: return std::to_string (polyVoices (v));
        case UnisonVoices: return std::to_string (unisonVoices (v));
        case Transpose: std::snprintf (b, sizeof b, "%+d", transposeSemis (v)); return b;
        case BendRange: std::snprintf (b, sizeof b, "%.1f st", bendRangeSemis (v)); return b;
        case Pan: if (std::fabs (v - 0.5f) < 0.005f) return "C"; std::snprintf (b, sizeof b, "%c%.1f", v < 0.5f ? 'L' : 'R', std::fabs (v - 0.5f) * 10.0f); return b;
        case MGTempoSync: case MGKeySync: return v >= 0.5f ? "On" : "Off";
        case MGBaseNote: return notes[mgBaseNoteIndex (v)];
        case MGTimes: return std::to_string (mgTimes (v));
        case ExtSrc1: case ExtSrc2: return extSourceName (extSourceIndex (v));
        case Ext1PW: case Ext1VCO2: case Ext1HPF: case Ext1LPF: case Ext1Amp:
        case Ext2PW: case Ext2VCO2: case Ext2HPF: case Ext2LPF: case Ext2Amp: return bip();
        default: return knob();
    }
}

inline bool isStepped (int p)
{
    switch (p)
    {
        case VCO1Wave: case VCO2Wave: case VCO1Scale: case VCO2Scale: case Assign: case Trigger:
        case PolyVoices: case UnisonVoices: case Transpose: case MGTempoSync: case MGKeySync:
        case MGBaseNote: case MGTimes: case ExtSrc1: case ExtSrc2: return true;
        default: return false;
    }
}
inline int numSteps (int p)
{
    switch (p)
    {
        case VCO1Wave: case VCO2Wave: case VCO1Scale: case VCO2Scale: return 4;
        case Assign: case Trigger: case MGTempoSync: case MGKeySync: return 2;
        case PolyVoices: return 32;  case UnisonVoices: return 16;  case Transpose: return 49;
        case MGBaseNote: return 10;  case MGTimes: return 16;  case ExtSrc1: case ExtSrc2: return 15;
        default: return 0;
    }
}
// the value a stepped control stores for step i (the original's own spacing)
inline float stepValue (int p, int i)
{
    switch (p)
    {
        case VCO1Wave: case VCO2Wave: case VCO1Scale: case VCO2Scale: return (float) i / 3.0f;
        default: { const int n = numSteps (p); return n > 1 ? (float) i / (float) (n - 1) : 0.0f; }
    }
}
}
