#pragma once
// Massif - the patch generator.
//
// Random patches that still sound like what the menu says. Every sound type
// is a recipe with windows to randomise inside (which tables, filter types,
// envelope shapes, effects, modulation), and a genre bends the recipe the way
// that scene tends to (wobble rates and growls for dubstep, detuned reeses for
// drum & bass, supersaws and gates for trance, and so on).
//
// The windows come from looking at how the original's factory library is put
// together, counted per sound category: which filter types basses use, how
// many unison voices leads run, which effects pads sit in, how long the amp
// envelopes are. Those are statistics only: no sound is copied, every patch
// here is put together from scratch each time.
#include "Factory.h"
#include <cstdint>
#include <random>
#include <string>

namespace massif
{
namespace generator
{
enum Genre { gAny = 0, gDubstep, gDnB, gHouse, gTechno, gTrance, gElectro, gHipHop, gAmbient, gIndustrial, gSynthwave, gDubTechno, gNumGenres };

enum Type
{
    tSubBass = 0, tAnalogBass, tDigitalBass, tGrowlBass, tWobbleBass, tReeseBass, tAcidLine,
    tMonoLead, tPolyLead, tHugeLead, tDirtyLead, tSyncLead, tSoftLead, tVoxLead,
    tDeepPad, tEvolvingPad, tBrightPad, tAiryPad, tDirtyPad, tChordPad, tStrings, tChoir,
    tKeys, tOrgan, tMallet, tPluck, tBrass,
    tSequence, tGate, tStab, tPercSynth, tSweep, tSoundscape, tSfx, tDubChord,
    tAnything, tNumTypes
};

inline const char* genreName (int g)
{
    static const char* n[gNumGenres] = { "Any Genre", "Dubstep", "Drum & Bass", "House", "Techno", "Trance", "Electro", "Hip Hop / Downbeat",
                                         "Ambient", "Industrial", "Synthwave", "Dub Techno" };
    return n[std::clamp (g, 0, gNumGenres - 1)];
}

inline const char* typeName (int t)
{
    static const char* n[tNumTypes] = {
        "Sub Bass", "Analog Bass", "Digital Bass", "Growl Bass", "Wobble Bass", "Reese Bass", "Acid Line",
        "Mono Lead", "Poly Lead", "Huge Lead", "Dirty Lead", "Sync Lead", "Soft Lead", "Vox Lead",
        "Deep Pad", "Evolving Pad", "Bright Pad", "Airy Pad", "Dirty Pad", "Chord Pad", "Strings", "Choir",
        "Keys", "Organ", "Mallet & Bell", "Pluck", "Brass",
        "Sequence", "Trance Gate", "Stab", "Percussive", "Sweep & Riser", "Soundscape", "Sound FX", "Dub Techno Chord",
        "Anything" };
    return n[std::clamp (t, 0, tNumTypes - 1)];
}

// the menu's section headings: the first type of each section
inline const char* sectionBefore (int t)
{
    switch (t)
    {
        case tSubBass: return "Bass"; case tMonoLead: return "Lead"; case tDeepPad: return "Pad";
        case tKeys: return "Keys & Plucks"; case tSequence: return "Rhythm & FX"; case tAnything: return "Other";
        default: return nullptr;
    }
}

class Rng
{
public:
    explicit Rng (uint32_t seed) : e (seed) {}
    float uni (float a, float b) { return a + (b - a) * std::uniform_real_distribution<float> (0.0f, 1.0f) (e); }
    int range (int a, int b) { return std::uniform_int_distribution<int> (a, b) (e); }   // inclusive
    bool chance (float p) { return uni (0.0f, 1.0f) < p; }
    template <typename T> T pick (std::initializer_list<T> l) { return *(l.begin() + range (0, (int) l.size() - 1)); }
    // weighted: { value, weight } pairs
    int weighted (std::initializer_list<std::pair<int, float>> l)
    {
        float tot = 0; for (auto& x : l) tot += x.second;
        float r = uni (0.0f, tot);
        for (auto& x : l) { if ((r -= x.second) <= 0.0f) return x.first; }
        return l.begin()->first;
    }
    std::mt19937& engine() { return e; }
private:
    std::mt19937 e;
};

// table families (slots whose files are in the usual wavetable folder; the
// few slots that often lack a file are left out)
namespace tables
{
    inline int basic (Rng& r)    { return r.weighted ({ { 2, 6 }, { 3, 2 }, { 4, 2 }, { 5, 3 }, { 6, 2 }, { 7, 1 }, { 8, 1 } }); }
    inline int warm (Rng& r)     { return r.weighted ({ { 5, 3 }, { 6, 3 }, { 4, 2 }, { 2, 2 }, { 16, 1 }, { 24, 1 } }); }
    inline int additive (Rng& r) { return r.range (14, 25); }
    inline int analog (Rng& r)   { return r.pick ({ 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49 }); }
    inline int growl (Rng& r)    { return r.pick ({ 34, 35, 36, 39, 40, 43, 47, 49, 51, 52, 53, 60, 61, 64, 65, 73, 74, 78 }); }
    inline int vocal (Rng& r)    { return r.pick ({ 51, 52, 53, 54, 55, 58, 59 }); }
    inline int digital (Rng& r)  { return r.pick ({ 9, 10, 11, 12, 13, 56, 57, 60, 61, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80 }); }
    inline int chord (Rng& r)    { return r.pick ({ 81, 82, 83, 84 }); }
    inline int bright (Rng& r)   { return r.weighted ({ { 2, 4 }, { 7, 3 }, { 8, 3 }, { 0, 2 }, { 3, 2 }, { 69, 1 }, { 72, 1 } }); }
}

using namespace fac;

struct Ctx
{
    Patch& p; Rng& r; int genre;
    bool is (int g) const { return genre == g; }
};

// ------------------------------------------------------------------ helpers
inline void fxc (Ctx& c, int slot, int type, float wet, float a = 0.5f, float b = 0.5f, float d = 0.5f) { fac::fx (c.p, slot, type, wet, a, b, d); }
inline void clearOscs (Patch& p) { for (int o = 0; o < 3; ++o) osc (p, o, 2, 0, 0, 0); }

inline void ampEnv (Ctx& c, float a0, float a1, float d0, float d1, float s0, float s1, float r0, float r1)
{
    env (c.p, 3, c.r.uni (a0, a1), c.r.uni (d0, d1), c.r.uni (s0, s1), c.r.uni (r0, r1));
}

inline void filterEnv (Ctx& c, int knob, float amt0, float amt1, float d0, float d1, float s0 = 0.0f, float s1 = 0.2f, float a1 = 0.02f)
{
    env (c.p, 0, c.r.uni (0.0f, a1), c.r.uni (d0, d1), c.r.uni (s0, s1), c.r.uni (0.25f, 0.45f));
    mod (c.p, knob, 0, msEnv1, c.r.uni (amt0, amt1));
}

// a synced LFO on mod 5..8 (m = 0..3)
inline void syncedLfo (Ctx& c, int m, int num, int den, int shapeA, int shapeB, float morph = 0.0f)
{
    lfo (c.p, m, 0.5f, shapeA, shapeB, morph); synced (c.p, m, num, den);
}

inline void wobbleRate (Ctx& c, int& num, int& den)
{
    if (c.is (gDnB)) { num = 1; den = c.r.pick ({ 8, 16, 16 }); return; }
    if (c.is (gDubstep)) { static const int d[5] = { 4, 8, 8, 12, 16 }; const int k = c.r.range (0, 4); num = k == 4 ? 3 : 1; den = d[k]; return; }
    num = 1; den = c.r.pick ({ 4, 8, 8, 16 });
}

inline void space (Ctx& c, int slot, float wet0, float wet1, bool synced = true)
{
    // a delay or a reverb, sized by the genre
    const bool dry = c.is (gTechno) || c.is (gIndustrial);
    if (c.r.chance (synced ? 0.55f : 0.0f))
    {
        fx (c.p, slot, fxDelaySync, c.r.uni (wet0, wet1) * (dry ? 0.6f : 1.0f), c.r.uni (0.35f, 0.6f), c.r.uni (0.3f, 0.6f), 0.5f);
        c.p.fxSync[(size_t) slot] = c.r.chance (0.5f) ? std::array<int, 4> { { 3, 16, 3, 16 } }
                                                      : (c.r.chance (0.5f) ? std::array<int, 4> { { 1, 8, 3, 16 } } : std::array<int, 4> { { 1, 4, 1, 4 } });
    }
    else fx (c.p, slot, c.r.chance (0.7f) ? fxReverb : fxReverbSmall, c.r.uni (wet0, wet1) * (dry ? 0.6f : 1.0f),
             c.r.uni (0.45f, c.is (gAmbient) ? 0.95f : 0.8f), c.r.uni (0.4f, 0.6f), c.r.uni (0.35f, 0.7f));
}

inline void colour (Ctx& c, int slot)
{
    // a widener or a tube
    const int t = c.r.weighted ({ { fxDimExp, 3 }, { fxChorus, 2 }, { fxChorusE, 2 }, { fxClassicTube, 2 }, { fxBrightTube, 1 }, { fxTubeTube, 1 }, { fxPhaser, 1 } });
    fx (c.p, slot, t, t >= fxClassicTube ? c.r.uni (0.3f, 0.6f) : c.r.uni (0.25f, 0.5f), c.r.uni (0.3f, 0.7f), c.r.uni (0.3f, 0.6f), c.r.uni (0.3f, 0.6f));
}

inline void shaper (Ctx& c, int i, int pos, float amt0, float amt1)
{
    c.p.insertType[(size_t) i] = c.r.weighted ({ { itParabolicShaper, 4 }, { itSineShaper, 4 }, { itClip, 2 }, { itBitcrush, c.is (gIndustrial) ? 3.0f : 0.7f },
                                                 { itFreqShift, c.is (gIndustrial) ? 1.5f : 0.4f } });
    c.p.insertPos[(size_t) i] = pos;
    c.p.knob[(size_t) (kIns1P1 + 2 * i)] = c.r.uni (0.5f, 1.0f);
    c.p.knob[(size_t) (kIns1P2 + 2 * i)] = c.r.uni (amt0, amt1);
}

inline void wide (Ctx& c, int maxVoices, float pitch0, float pitch1)
{
    const int v = c.r.range (2, std::max (2, maxVoices));
    unison (c.p, v, c.r.uni (pitch0, pitch1), c.r.uni (0.7f, 1.0f));
    c.p.uniRangeUp = 0.5f; c.p.uniRangeDown = 0.0f;
}

inline void macros4 (Ctx& c, int fltKnob, int fxSlot)
{
    // the first macros: the usual hands-on controls
    macro (c.p, 0, "Cutoff"); mod (c.p, fltKnob, 2, msMacro1, c.r.uni (0.25f, 0.4f));
    macro (c.p, 1, "Reso"); mod (c.p, fltKnob + 2, 1, msMacro2, c.r.uni (0.2f, 0.35f));
    if (fxSlot >= 0) { macro (c.p, 2, "Space"); mod (c.p, kFx1DW + 4 * fxSlot, 0, msMacro3, c.r.uni (0.3f, 0.5f)); }
    macro (c.p, 3, "Attack"); mod (c.p, kEnv4AttT, 0, msMacro4, 0.45f);
}

// ------------------------------------------------------------------ basses
inline void bassCommon (Ctx& c)
{
    mono (c.p, c.r.chance (0.6f) ? c.r.uni (0.1f, 0.35f) : 0.0f, c.r.chance (0.7f) ? tmLegato : tmAlways);
    ampEnv (c, 0.0f, 0.05f, 0.4f, 0.6f, 0.85f, 1.0f, 0.1f, 0.25f);
}

inline void subBass (Ctx& c)
{
    clearOscs (c.p);
    osc (c.p, 0, c.r.pick ({ 5, 6, 6 }), -12, 0.0f, 1.0f);
    if (c.r.chance (0.45f)) osc (c.p, 1, tables::warm (c.r), c.r.pick ({ 0.0f, -12.0f }), c.r.uni (0.1f, 0.6f), c.r.uni (0.15f, 0.35f));
    oneFilter (c.p, c.r.chance (0.6f) ? ftLP4 : ftLP2, cut (c.r.uni (180, 600)), c.r.uni (0.0f, 0.2f));
    bassCommon (c);
    if (c.r.chance (0.5f)) shaper (c, 0, ipPostF1, 0.1f, 0.35f);
    if (c.is (gHipHop) || c.r.chance (0.35f)) { env (c.p, 1, 0.0f, c.r.uni (0.3f, 0.45f), 0.0f, 0.2f); mod (c.p, kOsc1Pitch, 0, msEnv2, c.r.uni (0.05f, 0.15f)); }
    if (c.r.chance (0.5f)) fx (c.p, 0, c.r.pick ({ (int) fxBrightTube, (int) fxClassicTube, (int) fxTubeTube }), c.r.uni (0.2f, 0.45f), c.r.uni (0.3f, 0.6f));
    macro (c.p, 0, "Drive"); mod (c.p, kIns1P2, 0, msMacro1, 0.4f);
    macro (c.p, 1, "Harmonics"); mod (c.p, kOsc2Amp, 0, msMacro2, 0.4f);
    macro (c.p, 2, "Tone"); mod (c.p, kF1Cut, 1, msMacro3, 0.3f);
    macro (c.p, 3, "Glide"); mod (c.p, kGlide, 0, msMacro4, 0.4f);
}

inline void analogBass (Ctx& c)
{
    clearOscs (c.p);
    const int t = c.r.chance (0.3f) ? 0 : tables::basic (c.r);
    osc (c.p, 0, t, -12, c.r.uni (0.5f, 1.0f), 1.0f);
    osc (c.p, 1, tables::basic (c.r), c.r.pick ({ -12.0f, -24.0f, 0.0f }) + c.r.uni (-0.12f, 0.12f), c.r.uni (0.0f, 1.0f), c.r.uni (0.35f, 0.7f));
    if (c.r.chance (0.4f)) osc (c.p, 2, c.r.pick ({ 5, 6 }), -24, 0.0f, c.r.uni (0.3f, 0.6f));
    oneFilter (c.p, c.r.weighted ({ { ftLP4, 5 }, { ftLP2, 2 }, { ftDaft, 2 }, { ftAcid, 1 } }), cut (c.r.uni (200, 700)), c.r.uni (0.1f, 0.45f));
    filterEnv (c, kF1Cut, 0.2f, 0.45f, 0.4f, 0.6f);
    mod (c.p, kF1Cut, 1, msVelocity, c.r.uni (0.05f, 0.15f));
    bassCommon (c);
    if (c.r.chance (0.5f)) shaper (c, 0, ipPostF1, 0.15f, 0.45f);
    colour (c, 0);
    macros4 (c, kF1Cut, -1);
    macro (c.p, 2, "Env Depth"); mod (c.p, kF1Cut, 2, msMacro3, 0.2f);
}

inline void digitalBass (Ctx& c)
{
    clearOscs (c.p);
    osc (c.p, 0, c.r.chance (0.5f) ? tables::digital (c.r) : tables::analog (c.r), -12, c.r.uni (0.0f, 1.0f), 1.0f,
         c.r.weighted ({ { omSpectrum, 5 }, { omBendMinusPlus, 3 }, { omFormant, 1 } }), c.r.uni (0.4f, 1.0f));
    osc (c.p, 1, c.r.pick ({ 5, 6 }), -12, 0.0f, c.r.uni (0.3f, 0.6f));
    oneFilter (c.p, c.r.weighted ({ { ftLP4, 4 }, { ftBandpass, 2 }, { ftScream, 2 }, { ftDaft, 1 }, { ftComb, 1 } }), cut (c.r.uni (300, 1400)), c.r.uni (0.15f, 0.45f), c.r.uni (0.3f, 0.7f));
    filterEnv (c, kF1Cut, 0.1f, 0.3f, 0.45f, 0.6f, 0.2f, 0.6f);
    lfo (c.p, 0, c.r.uni (0.2f, 0.45f), csTriangle, csSine); mod (c.p, kOsc1Pos, 0, msMod5, c.r.uni (0.1f, 0.3f));
    bassCommon (c);
    shaper (c, 0, ipPostF1, 0.2f, 0.5f);
    fx (c.p, 0, c.r.pick ({ (int) fxBrightTube, (int) fxTubeTube, (int) fxDimExp }), c.r.uni (0.25f, 0.5f), c.r.uni (0.3f, 0.6f));
    macros4 (c, kF1Cut, -1);
    macro (c.p, 2, "Timbre"); mod (c.p, kOsc1Pos, 1, msMacro3, 0.5f);
}

inline void growlBass (Ctx& c, bool wobble)
{
    clearOscs (c.p);
    const int t = tables::growl (c.r);
    osc (c.p, 0, t, -12, c.r.uni (0.0f, 0.6f), 1.0f, c.r.weighted ({ { omSpectrum, 3 }, { omBendMinusPlus, 3 }, { omFormant, 2 }, { omBendMinus, 1 } }), c.r.uni (0.3f, 0.8f));
    osc (c.p, 1, c.r.chance (0.5f) ? t : tables::growl (c.r), c.r.pick ({ -12.0f, 0.0f }) + c.r.uni (-0.1f, 0.1f), c.r.uni (0.2f, 0.9f), c.r.uni (0.4f, 0.8f));
    osc (c.p, 2, c.r.pick ({ 5, 6 }), -24, 0.0f, c.r.uni (0.4f, 0.7f));
    c.p.filterType[0] = c.r.weighted ({ { ftScream, 4 }, { ftBandpass, 3 }, { ftLP4, 2 }, { ftComb, 1 }, { ftDaft, 1 } });
    c.p.knob[kF1Cut] = cut (c.r.uni (500, 1600)); c.p.knob[kF1Res] = c.r.uni (0.2f, 0.45f); c.p.knob[kF1Bw] = c.r.uni (0.3f, 0.65f);
    c.p.filterType[1] = ftLP4; c.p.knob[kF2Cut] = cut (c.r.uni (2000, 6000)); c.p.knob[kF2Res] = 0.1f;
    c.p.knob[kSerPar] = 1.0f; c.p.knob[kFltMix] = 0.0f;                               // filter 1 into filter 2
    for (int o = 0; o < 2; ++o) c.p.knob[(size_t) (kOsc1Route + 5 * o)] = 1.0f;
    c.p.knob[kOsc3Route] = 0.0f;                                                        // the sub skips the growl
    int num = 1, den = 8; wobbleRate (c, num, den);
    syncedLfo (c, 0, num, den, c.r.pick ({ (int) csSine, (int) csTriangle, (int) csSaw, (int) csRampDown }), csSine, 0.0f);
    const float depth = wobble ? c.r.uni (0.35f, 0.6f) : c.r.uni (0.1f, 0.25f);
    mod (c.p, kOsc1Pos, 0, msMod5, depth); mod (c.p, kF1Cut, 0, msMod5, depth * 0.6f);
    if (c.r.chance (0.6f)) mod (c.p, kOsc1Intensity, 0, msMod5, depth * 0.5f);
    mono (c.p, c.r.uni (0.1f, 0.3f));
    ampEnv (c, 0.0f, 0.03f, 0.5f, 0.6f, 1.0f, 1.0f, 0.08f, 0.2f);
    shaper (c, 0, ipPostF1, 0.3f, 0.7f);
    if (c.r.chance (0.5f)) { c.p.insertType[1] = itSineShaper; c.p.insertPos[1] = ipPreAmp; c.p.knob[kIns2P1] = c.r.uni (0.4f, 0.8f); c.p.knob[kIns2P2] = c.r.uni (0.2f, 0.5f); }
    fx (c.p, 0, c.r.pick ({ (int) fxBrightTube, (int) fxTubeTube, (int) fxClassicTube }), c.r.uni (0.3f, 0.6f), c.r.uni (0.4f, 0.7f));
    if (c.r.chance (0.5f)) fx (c.p, 1, fxDimExp, c.r.uni (0.2f, 0.4f), 0.5f);
    macro (c.p, 0, "Wobble"); mod (c.p, kMod5Amp, 0, msMacro1, 0.0f); c.p.knob[kMod5Amp] = wobble ? 1.0f : 0.6f;
    mod (c.p, kOsc1Pos, 1, msMacro1, 0.3f);
    macro (c.p, 1, "Throat"); mod (c.p, kOsc1Intensity, 1, msMacro2, 0.4f);
    macro (c.p, 2, "Grit"); mod (c.p, kIns1P2, 0, msMacro3, 0.35f);
    macro (c.p, 3, "Cutoff"); mod (c.p, kF1Cut, 1, msMacro4, 0.35f);
    macro (c.p, 4, "Rate"); mod (c.p, kMod5Rate, 0, msMacro5, 0.3f);
}

inline void reeseBass (Ctx& c)
{
    clearOscs (c.p);
    const float det = c.r.uni (0.08f, c.is (gDnB) ? 0.25f : 0.18f);
    const int t = c.r.chance (0.7f) ? 2 : tables::basic (c.r);
    osc (c.p, 0, t, -12 - det, c.r.uni (0.7f, 1.0f), 0.9f);
    osc (c.p, 1, t, -12 + det, c.r.uni (0.7f, 1.0f), 0.9f);
    osc (c.p, 2, c.r.pick ({ 5, 6 }), -24, 0.0f, c.r.uni (0.4f, 0.65f));
    oneFilter (c.p, c.r.weighted ({ { ftLP4, 5 }, { ftLP2, 2 }, { ftDaft, 1 } }), cut (c.r.uni (250, 900)), c.r.uni (0.1f, 0.35f));
    lfo (c.p, 0, c.r.uni (0.05f, 0.18f)); mod (c.p, kF1Cut, 0, msMod5, c.r.uni (0.05f, 0.15f));
    if (c.r.chance (0.6f)) { lfo (c.p, 1, c.r.uni (0.05f, 0.15f), csTriangle, csTriangle); mod (c.p, kOsc1Pos, 0, msMod6, 0.2f); mod (c.p, kOsc2Pos, 0, msMod6, -0.2f); }
    mono (c.p, c.r.uni (0.15f, 0.35f));
    ampEnv (c, 0.0f, 0.05f, 0.5f, 0.6f, 1.0f, 1.0f, 0.15f, 0.3f);
    if (c.r.chance (0.6f)) shaper (c, 0, ipPostF1, 0.15f, 0.4f);
    fx (c.p, 0, fxDimExp, c.r.uni (0.25f, 0.45f), 0.5f);
    if (c.r.chance (0.5f)) fx (c.p, 1, fxClassicTube, c.r.uni (0.2f, 0.4f), 0.4f);
    macro (c.p, 0, "Cutoff"); mod (c.p, kF1Cut, 1, msMacro1, 0.4f);
    macro (c.p, 1, "Detune"); mod (c.p, kOsc1Pitch, 0, msMacro2, -0.004f); mod (c.p, kOsc2Pitch, 0, msMacro2, 0.004f);
    macro (c.p, 2, "Movement"); mod (c.p, kMod5Amp, 0, msMacro3, 0.0f);
    macro (c.p, 3, "Sub"); mod (c.p, kOsc3Amp, 0, msMacro4, 0.3f);
}

inline void acidLine (Ctx& c)
{
    clearOscs (c.p);
    osc (c.p, 0, c.r.chance (0.6f) ? 2 : 0, c.r.pick ({ -12.0f, 0.0f }), c.r.chance (0.6f) ? 1.0f : 0.0f, 1.0f);
    oneFilter (c.p, c.r.chance (0.7f) ? ftAcid : ftLP4, cut (c.r.uni (300, 700)), c.r.uni (0.55f, 0.75f));
    filterEnv (c, kF1Cut, 0.25f, 0.45f, 0.42f, 0.56f, 0.0f, 0.1f);
    mod (c.p, kF1Cut, 1, msVelocity, c.r.uni (0.1f, 0.2f));
    mono (c.p, c.r.uni (0.15f, 0.3f));
    ampEnv (c, 0.0f, 0.01f, 0.5f, 0.6f, 0.8f, 1.0f, 0.08f, 0.2f);
    if (c.r.chance (0.6f)) shaper (c, 0, ipPostF1, 0.2f, 0.5f);
    fx (c.p, 0, fxClassicTube, c.r.uni (0.3f, 0.5f), c.r.uni (0.3f, 0.5f));
    fx (c.p, 1, fxDelaySync, c.r.uni (0.12f, 0.25f), 0.5f, 0.45f); c.p.fxSync[1] = { { 3, 16, 3, 16 } };
    macro (c.p, 0, "Cutoff"); mod (c.p, kF1Cut, 2, msMacro1, 0.4f);
    macro (c.p, 1, "Reso"); mod (c.p, kF1Res, 0, msMacro2, 0.25f);
    macro (c.p, 2, "Env Mod"); mod (c.p, kEnv1DecT, 0, msMacro3, 0.25f);
    macro (c.p, 3, "Accent"); mod (c.p, kIns1P2, 0, msMacro4, 0.4f);
}

// ------------------------------------------------------------------ leads
inline void leadCommon (Ctx& c, bool mono_)
{
    if (mono_) mono (c.p, c.r.uni (0.15f, 0.4f), c.r.chance (0.7f) ? tmLegato : tmAlways);
    c.p.knob[kVibRate] = c.r.uni (0.45f, 0.65f); c.p.knob[kVibDepth] = 0.0f;
    mod (c.p, kVibDepth, 0, msAftertouch, 0.2f);
    space (c, 1, 0.15f, 0.35f);
}

inline void lead (Ctx& c, int kind)
{
    clearOscs (c.p);
    int t1 = tables::bright (c.r);
    if (kind == tVoxLead) t1 = tables::vocal (c.r);
    if (kind == tSoftLead) t1 = tables::warm (c.r);
    if (kind == tDirtyLead) t1 = c.r.chance (0.5f) ? tables::analog (c.r) : tables::basic (c.r);
    if (kind == tSyncLead) t1 = 1;
    const int mode1 = kind == tVoxLead ? c.r.pick ({ (int) omFormant, (int) omSpectrum }) : c.r.weighted ({ { omSpectrum, 6 }, { omFormant, 1 }, { omBendMinusPlus, 1 } });
    osc (c.p, 0, t1, 0, kind == tSyncLead ? c.r.uni (0.1f, 0.4f) : c.r.uni (0.3f, 1.0f), 1.0f, mode1, c.r.uni (0.5f, 1.0f));
    osc (c.p, 1, c.r.chance (0.6f) ? t1 : tables::basic (c.r), c.r.pick ({ 0.0f, 12.0f, -12.0f, 7.0f }) + c.r.uni (-0.08f, 0.08f), c.r.uni (0.0f, 1.0f), c.r.uni (0.3f, 0.6f));
    if (kind == tHugeLead || c.r.chance (0.3f)) osc (c.p, 2, tables::basic (c.r), c.r.pick ({ -12.0f, 12.0f }), c.r.uni (0.5f, 1.0f), c.r.uni (0.25f, 0.45f));
    int ft = c.r.weighted ({ { ftLP2, 4 }, { ftLP4, 3 }, { ftBandpass, 2 }, { ftDaft, 1 }, { ftAcid, 1 } });
    if (kind == tDirtyLead) ft = c.r.weighted ({ { ftBandpass, 3 }, { ftScream, 3 }, { ftLP4, 2 }, { ftHP2, 1 } });
    if (kind == tSoftLead) ft = ftLP2;
    oneFilter (c.p, ft, cut (c.r.uni (kind == tSoftLead ? 900.0f : 1600.0f, kind == tSoftLead ? 2500.0f : 5500.0f)), c.r.uni (0.05f, 0.3f), c.r.uni (0.3f, 0.6f));
    filterEnv (c, kF1Cut, 0.05f, 0.25f, 0.45f, 0.6f, 0.3f, 0.7f);
    const bool monoLead = kind == tMonoLead || kind == tSyncLead || kind == tVoxLead || kind == tSoftLead || (kind == tDirtyLead && c.r.chance (0.6f));
    leadCommon (c, monoLead);
    ampEnv (c, kind == tSoftLead ? 0.1f : 0.0f, kind == tSoftLead ? 0.3f : 0.08f, 0.45f, 0.6f, 0.8f, 1.0f, 0.15f, 0.35f);
    if (kind == tSyncLead) { env (c.p, 1, 0.0f, c.r.uni (0.45f, 0.6f), c.r.uni (0.2f, 0.5f), 0.3f); mod (c.p, kOsc1Pos, 0, msEnv2, c.r.uni (0.3f, 0.5f)); }
    if (kind == tHugeLead || c.is (gTrance) || (kind == tPolyLead && c.r.chance (0.5f)))
        wide (c, kind == tHugeLead || c.is (gTrance) ? 8 : 4, 0.2f, kind == tHugeLead ? 0.5f : 0.35f);
    if (kind == tDirtyLead || c.is (gIndustrial)) shaper (c, 0, ipPostF1, 0.25f, 0.6f);
    colour (c, 0);
    if (kind == tVoxLead) { lfo (c.p, 0, c.r.uni (0.2f, 0.4f), csTriangle, csSine); mod (c.p, kOsc1Pos, 1, msMod5, 0.2f); }
    macros4 (c, kF1Cut, 1);
    macro (c.p, 3, "Glide"); c.p.mod[kEnv4AttT] = {}; mod (c.p, kGlide, 0, msMacro4, 0.3f);
    macro (c.p, 4, "Vibrato"); mod (c.p, kVibDepth, 1, msMacro5, 0.25f);
    if (kind == tSyncLead || kind == tVoxLead) { macro (c.p, 5, kind == tSyncLead ? "Sync" : "Vowel"); mod (c.p, kOsc1Pos, 2, msMacro6, 0.5f); }
}

// ------------------------------------------------------------------ pads
inline void pad (Ctx& c, int kind)
{
    clearOscs (c.p);
    int t1 = tables::basic (c.r), t2 = tables::basic (c.r);
    switch (kind)
    {
        case tDeepPad: t1 = tables::warm (c.r); t2 = tables::warm (c.r); break;
        case tEvolvingPad: t1 = c.r.chance (0.5f) ? tables::additive (c.r) : tables::digital (c.r); t2 = tables::additive (c.r); break;
        case tBrightPad: t1 = tables::bright (c.r); t2 = tables::bright (c.r); break;
        case tAiryPad: t1 = tables::warm (c.r); t2 = tables::additive (c.r); break;
        case tDirtyPad: t1 = tables::analog (c.r); t2 = tables::digital (c.r); break;
        case tChordPad: t1 = c.r.chance (0.5f) ? tables::chord (c.r) : tables::basic (c.r); t2 = tables::basic (c.r); break;
        case tStrings: t1 = c.r.chance (0.6f) ? 7 : 2; t2 = c.r.chance (0.5f) ? 8 : 2; break;
        case tChoir: t1 = tables::vocal (c.r); t2 = tables::vocal (c.r); break;
        default: break;
    }
    const int m1 = kind == tChoir ? omFormant : c.r.weighted ({ { omSpectrum, 6 }, { omBendMinusPlus, 1 }, { omFormant, 1 } });
    osc (c.p, 0, t1, 0, c.r.uni (0.0f, 1.0f), 0.8f, m1, c.r.uni (0.4f, 1.0f));
    osc (c.p, 1, t2, c.r.pick ({ 0.0f, 12.0f, 7.0f, -12.0f }) + c.r.uni (0.03f, 0.1f), c.r.uni (0.0f, 1.0f), c.r.uni (0.4f, 0.75f));
    if (c.r.chance (0.6f)) osc (c.p, 2, c.r.chance (0.5f) ? tables::warm (c.r) : t1, c.r.pick ({ -12.0f, 12.0f, 19.0f }), c.r.uni (0.0f, 1.0f), c.r.uni (0.2f, 0.45f));
    int ft = c.r.weighted ({ { ftLP2, 4 }, { ftLP4, 3 }, { ftBandpass, 1 }, { ftHP2, kind == tAiryPad ? 3.0f : 0.5f } });
    if (kind == tDirtyPad) ft = c.r.pick ({ (int) ftScream, (int) ftComb, (int) ftBandpass });
    float hz = kind == tDeepPad ? c.r.uni (350, 1200) : kind == tBrightPad || kind == tAiryPad ? c.r.uni (2000, 6000) : c.r.uni (900, 3200);
    if (ft == ftHP2) hz = c.r.uni (200, 600);
    oneFilter (c.p, ft, cut (hz), c.r.uni (0.05f, 0.3f), c.r.uni (0.3f, 0.6f));
    // slow motion
    lfo (c.p, 0, c.r.uni (0.05f, 0.2f), c.r.pick ({ (int) csSine, (int) csTriangle }), csSine);
    mod (c.p, kF1Cut, 0, msMod5, c.r.uni (0.05f, 0.15f));
    lfo (c.p, 1, c.r.uni (0.04f, 0.18f), csTriangle, csTriangle);
    const float pm = kind == tEvolvingPad ? c.r.uni (0.25f, 0.45f) : c.r.uni (0.05f, 0.25f);
    mod (c.p, kOsc1Pos, 0, msMod6, pm); mod (c.p, kOsc2Pos, 0, msMod6, -pm);
    if (kind == tEvolvingPad || c.is (gAmbient))
    { lfo (c.p, 2, c.r.uni (0.02f, 0.08f), csSine, csTriangle, 0.5f); mod (c.p, kOsc1Intensity, 0, msMod7, c.r.uni (0.1f, 0.3f)); mod (c.p, kPan, 0, msMod7, 0.2f); }
    if (kind == tAiryPad || c.r.chance (0.3f)) { c.p.knob[kNoiseAmp] = c.r.uni (0.1f, 0.3f); c.p.knob[kNoiseColor] = c.r.uni (0.6f, 0.95f); }
    ampEnv (c, kind == tStrings || kind == tChordPad ? 0.25f : 0.4f, 0.62f, 0.55f, 0.65f, 0.85f, 1.0f, 0.45f, 0.65f);
    if (kind == tChordPad && c.r.chance (0.5f)) { unison (c.p, 3, 0.292f, 0.8f); c.p.uniChordMode = true; c.p.uniRangeUp = 24.0f; c.p.uniRangeDown = 0.0f; }
    else wide (c, kind == tStrings || c.is (gTrance) ? 6 : 4, 0.2f, 0.4f);
    if (kind == tDirtyPad) shaper (c, 0, ipPostF1, 0.2f, 0.5f);
    colour (c, 0);
    fx (c.p, 1, fxReverb, c.r.uni (0.35f, c.is (gAmbient) ? 0.65f : 0.5f), c.r.uni (0.7f, 0.95f), c.r.uni (0.4f, 0.6f), c.r.uni (0.3f, 0.7f));
    macro (c.p, 0, "Brightness"); mod (c.p, kF1Cut, 1, msMacro1, 0.35f);
    macro (c.p, 1, "Motion"); mod (c.p, kMod6Amp, 0, msMacro2, -0.8f);
    macro (c.p, 2, "Space"); mod (c.p, kFx2DW, 0, msMacro3, 0.4f);
    macro (c.p, 3, "Attack"); mod (c.p, kEnv4AttT, 0, msMacro4, -0.4f);
    macro (c.p, 4, "Release"); mod (c.p, kEnv4RelT, 0, msMacro5, 0.25f);
    macro (c.p, 5, "Timbre"); mod (c.p, kOsc1Pos, 1, msMacro6, 0.5f);
}

// ------------------------------------------------------------------ keys, plucks, brass
inline void keys (Ctx& c, int kind)
{
    clearOscs (c.p);
    switch (kind)
    {
        case tKeys:
            osc (c.p, 0, tables::warm (c.r), 0, c.r.uni (0.0f, 0.5f), 1.0f);
            osc (c.p, 1, c.r.pick ({ 5, 6 }), c.r.pick ({ 12.0f, 19.0f, 24.0f }), 0.0f, c.r.uni (0.15f, 0.35f));
            if (c.r.chance (0.5f)) { c.p.knob[kModOscPitch] = semi (c.r.pick ({ 12.0f, 19.0f, 24.0f })); c.p.pmTarget = 1; env (c.p, 1, 0.0f, c.r.uni (0.4f, 0.55f), 0.0f, 0.3f); mod (c.p, kModOscPhase, 0, msEnv2, c.r.uni (0.15f, 0.35f)); }
            oneFilter (c.p, ftLP2, cut (c.r.uni (900, 2500)), c.r.uni (0.0f, 0.15f));
            filterEnv (c, kF1Cut, 0.1f, 0.25f, 0.5f, 0.65f, 0.1f, 0.3f);
            mod (c.p, kF1Cut, 1, msVelocity, c.r.uni (0.15f, 0.3f));
            ampEnv (c, 0.0f, 0.01f, 0.68f, 0.76f, 0.0f, 0.15f, 0.35f, 0.5f); c.p.knob[kEnv4Vel] = 0.6f;
            fx (c.p, 0, c.r.pick ({ (int) fxChorus, (int) fxPhaser, (int) fxChorusE }), c.r.uni (0.25f, 0.45f), c.r.uni (0.3f, 0.5f));
            break;
        case tOrgan:
            osc (c.p, 0, c.r.chance (0.6f) ? 16 : tables::additive (c.r), 0, c.r.uni (0.2f, 0.7f), 1.0f);
            osc (c.p, 1, c.r.pick ({ 5, 6, 16 }), 12, c.r.uni (0.0f, 0.5f), c.r.uni (0.2f, 0.4f));
            osc (c.p, 2, 6, -12, 0.0f, c.r.uni (0.3f, 0.5f));
            oneFilter (c.p, ftLP2, cut (c.r.uni (3000, 7000)), 0.0f);
            env (c.p, 0, 0.0f, 0.2f, 0.0f, 0.1f); mod (c.p, kOsc2Amp, 0, msEnv1, c.r.uni (0.2f, 0.4f));
            ampEnv (c, 0.0f, 0.01f, 0.5f, 0.5f, 1.0f, 1.0f, 0.06f, 0.12f);
            fx (c.p, 0, fxChorus, c.r.uni (0.4f, 0.6f), c.r.uni (0.6f, 0.8f), 0.45f);
            fx (c.p, 1, c.r.chance (0.5f) ? fxClassicTube : fxReverbSmall, c.r.uni (0.25f, 0.4f), 0.4f);
            break;
        case tMallet:
            osc (c.p, 0, 6, 0, 0.0f, 1.0f);
            osc (c.p, 1, c.r.pick ({ 5, 6, 23, 24 }), c.r.pick ({ 12.0f, 19.0f, 24.0f, 28.0f }), 0.0f, c.r.uni (0.15f, 0.35f));
            c.p.knob[kModOscPitch] = semi (c.r.pick ({ 19.0f, 24.0f, 31.0f, 35.0f })); c.p.pmTarget = 1; c.p.knob[kModOscPhase] = 0.0f;
            env (c.p, 0, 0.0f, c.r.uni (0.5f, 0.65f), 0.0f, 0.5f); mod (c.p, kModOscPhase, 0, msEnv1, c.r.uni (0.25f, 0.45f)); mod (c.p, kModOscPhase, 1, msVelocity, 0.2f);
            oneFilter (c.p, ftNone, 0.5f, 0.0f);
            ampEnv (c, 0.0f, 0.0f, 0.68f, 0.78f, 0.0f, 0.0f, 0.55f, 0.68f);
            fx (c.p, 0, fxChorus, 0.25f, 0.3f, 0.4f);
            break;
        case tPluck:
            osc (c.p, 0, c.r.chance (0.6f) ? tables::bright (c.r) : tables::warm (c.r), 0, c.r.uni (0.3f, 1.0f), 1.0f);
            osc (c.p, 1, tables::basic (c.r), c.r.pick ({ 12.0f, 0.07f, 7.0f }), c.r.uni (0.0f, 1.0f), c.r.uni (0.2f, 0.45f));
            oneFilter (c.p, c.r.chance (0.7f) ? ftLP2 : ftLP4, cut (c.r.uni (500, 1500)), c.r.uni (0.1f, 0.35f));
            filterEnv (c, kF1Cut, 0.3f, 0.5f, 0.45f, 0.58f, 0.0f, 0.05f);
            mod (c.p, kF1Cut, 1, msVelocity, 0.15f);
            ampEnv (c, 0.0f, 0.0f, 0.55f, 0.66f, 0.0f, 0.05f, 0.4f, 0.55f);
            if (c.is (gTrance) || c.r.chance (0.4f)) wide (c, 5, 0.2f, 0.35f);
            break;
        case tBrass:
            osc (c.p, 0, 2, 0, 1.0f, 1.0f); osc (c.p, 1, 2, c.r.uni (0.05f, 0.12f), 1.0f, 0.8f);
            if (c.r.chance (0.5f)) osc (c.p, 2, 2, -12, 1.0f, 0.4f);
            oneFilter (c.p, c.r.chance (0.6f) ? ftLP4 : ftLP2, cut (c.r.uni (500, 1100)), c.r.uni (0.05f, 0.2f));
            env (c.p, 0, c.r.uni (0.1f, 0.25f), c.r.uni (0.5f, 0.6f), c.r.uni (0.4f, 0.6f), 0.35f); mod (c.p, kF1Cut, 0, msEnv1, c.r.uni (0.25f, 0.4f));
            mod (c.p, kF1Cut, 1, msVelocity, 0.15f);
            ampEnv (c, 0.06f, 0.18f, 0.5f, 0.6f, 0.85f, 1.0f, 0.3f, 0.42f);
            c.p.knob[kVibRate] = 0.55f; mod (c.p, kVibDepth, 0, msAftertouch, 0.2f);
            fx (c.p, 0, fxChorusE, 0.25f, 0.4f);
            break;
        default: break;
    }
    space (c, 1, 0.15f, 0.35f);
    if (kind == tMallet) { macro (c.p, 0, "Strike"); mod (c.p, kModOscPhase, 2, msMacro1, 0.4f); macro (c.p, 1, "Ratio"); mod (c.p, kModOscPitch, 0, msMacro2, 0.094f); }
    else { macro (c.p, 0, "Tone"); mod (c.p, kF1Cut, 2, msMacro1, 0.3f); macro (c.p, 1, "Body"); mod (c.p, kOsc2Amp, 1, msMacro2, 0.4f); }
    macro (c.p, 2, "Space"); mod (c.p, kFx2DW, 0, msMacro3, 0.35f);
    macro (c.p, 3, "Length"); mod (c.p, kEnv4DecT, 0, msMacro4, 0.15f); mod (c.p, kEnv4RelT, 0, msMacro4, 0.2f);
}

// ------------------------------------------------------------------ rhythm and effects
inline void sequence (Ctx& c, int kind)
{
    clearOscs (c.p);
    const bool bassy = kind == tSequence && c.r.chance (0.5f);
    osc (c.p, 0, c.r.chance (0.5f) ? 2 : tables::bright (c.r), bassy ? -12.0f : 0.0f, c.r.uni (0.5f, 1.0f), 1.0f);
    osc (c.p, 1, tables::basic (c.r), c.r.pick ({ 0.08f, 12.0f, -12.0f }), c.r.uni (0.0f, 1.0f), c.r.uni (0.3f, 0.6f));
    oneFilter (c.p, c.r.weighted ({ { ftLP4, 4 }, { ftLP2, 3 }, { ftAcid, 1 } }), cut (c.r.uni (400, 2000)), c.r.uni (0.2f, 0.5f));
    if (kind == tGate)
    {
        std::array<float, 16> st {}; for (auto& s : st) s = c.r.chance (0.6f) ? 1.0f : 0.0f; st[0] = 1.0f;
        stepper (c.p, 3, { st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7], st[8], st[9], st[10], st[11], st[12], st[13], st[14], st[15] }, 1, 16, c.r.uni (0.05f, 0.2f));
        c.p.ampMod[0].slot[1] = { msMod8, 1.0f };
        wide (c, 7, 0.25f, 0.45f);
        ampEnv (c, 0.05f, 0.2f, 0.5f, 0.6f, 1.0f, 1.0f, 0.35f, 0.5f);
        macro (c.p, 4, "Soft Gate"); mod (c.p, kMod8Morph, 0, msMacro5, 0.6f);
    }
    else
    {
        std::array<float, 16> st {}; for (auto& s : st) s = c.r.uni (0.0f, 1.0f);
        stepper (c.p, 3, { st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7], st[8], st[9], st[10], st[11], st[12], st[13], st[14], st[15] }, 1, 16);
        mod (c.p, kF1Cut, 0, msMod8, c.r.uni (0.25f, 0.45f));
        if (c.r.chance (0.4f)) mod (c.p, kOsc1Pos, 0, msMod8, c.r.uni (0.2f, 0.4f));
        env (c.p, 0, 0.0f, c.r.uni (0.35f, 0.5f), 0.0f, 0.2f); mod (c.p, kF1Cut, 1, msEnv1, c.r.uni (0.1f, 0.25f));
        ampEnv (c, 0.0f, 0.0f, 0.5f, 0.6f, 1.0f, 1.0f, 0.15f, 0.25f);
        macro (c.p, 4, "Steps"); mod (c.p, kMod8Amp, 0, msMacro5, -0.8f);
        if (bassy) mono (c.p, 0.0f, tmAlways);
    }
    fx (c.p, 0, fxDelaySync, c.r.uni (0.2f, 0.35f), 0.5f, 0.5f); c.p.fxSync[0] = { { 3, 16, 3, 16 } };
    colour (c, 1);
    macros4 (c, kF1Cut, 0);
}

inline void stab (Ctx& c)
{
    clearOscs (c.p);
    osc (c.p, 0, c.r.chance (0.4f) ? tables::chord (c.r) : 2, 0, c.r.uni (0.4f, 1.0f), 1.0f);
    osc (c.p, 1, 2, c.r.uni (0.05f, 0.1f), 1.0f, c.r.uni (0.4f, 0.7f));
    oneFilter (c.p, c.r.chance (0.6f) ? ftLP2 : ftLP4, cut (c.r.uni (600, 1500)), c.r.uni (0.15f, 0.35f));
    filterEnv (c, kF1Cut, 0.3f, 0.45f, 0.5f, 0.6f, 0.0f, 0.05f);
    ampEnv (c, 0.0f, 0.0f, 0.55f, 0.64f, 0.0f, 0.0f, 0.35f, 0.45f);
    if (c.r.chance (0.6f)) { unison (c.p, 3, 0.292f, 0.8f); c.p.uniChordMode = true; c.p.uniRangeUp = 24.0f; c.p.uniRangeDown = 0.0f; }
    colour (c, 0); space (c, 1, 0.2f, 0.35f);
    macros4 (c, kF1Cut, 1);
}

// Dub techno chords, after the classic "DT chord" programming: a minor chord stacked on the three
// oscillators (fifth / minor third / root or below), a three voice unison for width, band-reject or
// band-pass filters played by a held, looping filter envelope and velocity, slow noise and triangle
// LFOs drifting the pitches and the timbre, and a long synced delay / chorus / phaser behind macros.
// The ingredients and ranges were read off such patches; each sound is assembled anew.
inline void dubChord (Ctx& c)
{
    auto& p = c.p; auto& r = c.r;
    // the chord: intervals of osc 1..3
    static const int voicings[][3] = { { 7, 3, 0 }, { 7, 3, 0 }, { 7, 3, -3 }, { 7, 3, -7 }, { 7, 3, -5 }, { 10, 3, 0 }, { 7, 5, 0 }, { 14, 7, 3 } };
    const auto& v = voicings[r.range (0, 7)];
    const int t1 = r.weighted ({ { 0, 3 }, { 8, 2 }, { 6, 1 }, { 55, 1 } });
    const int t2 = r.weighted ({ { 8, 2 }, { 54, 2 }, { 70, 1 }, { 0, 1 } });
    const int t3 = r.weighted ({ { 26, 2 }, { 10, 1 }, { 22, 1 }, { 24, 1 } });
    osc (p, 0, t1, (float) v[0], r.uni (0.3f, 0.96f), r.uni (0.66f, 0.93f), omSpectrum, r.uni (0.69f, 0.9f));
    osc (p, 1, t2, (float) v[1], r.chance (0.5f) ? r.uni (0.0f, 0.1f) : r.uni (0.6f, 0.75f), r.uni (0.65f, 0.98f), omSpectrum, r.uni (0.77f, 1.0f));
    osc (p, 2, t3, (float) v[2], r.uni (0.0f, 0.7f), r.uni (0.64f, 0.83f), omSpectrum, r.uni (0.7f, 1.0f));
    if (r.chance (0.2f)) p.oscMode[0] = r.pick ({ (int) omBendPlus, (int) omFormant });
    for (int o = 0; o < 3; ++o) p.knob[(size_t) (kOsc1Route + 5 * o)] = 0.5f;
    // noise: white, a little
    p.noiseType = 0; p.knob[kNoiseColor] = 1.0f; p.knob[kNoiseAmp] = r.chance (0.8f) ? r.uni (0.1f, 0.3f) : 0.0f; p.knob[kNoiseRoute] = 0.5f;
    // filters: band-reject (mostly) into a resonant second filter, serial
    // (a low-pass here is a 2-pole one held below self-oscillation: the 4-pole ladder sings from res 0.78, and
    // with the filter envelope opening it to the top that song went up past 10 kHz and folded back as noise -
    // the harsh, "bitcrushed" chords)
    p.filterType[0] = r.weighted ({ { ftBandreject, 4 }, { ftBandpass, 1 }, { ftLP2, 1 } });
    p.filterType[1] = r.weighted ({ { ftBandreject, 3 }, { ftScream, 1 }, { ftBandpass, 1 }, { ftLP2, 1 }, { ftNone, 1 } });
    p.knob[kF1Cut] = r.uni (0.0f, 0.6f); p.knob[kF1Bw] = r.uni (0.2f, 0.5f); p.knob[kF1Res] = r.uni (0.63f, 0.83f);
    p.knob[kF2Cut] = r.uni (0.25f, 1.0f); p.knob[kF2Bw] = r.uni (0.0f, 0.7f); p.knob[kF2Res] = r.uni (0.85f, 0.93f);
    if (p.filterType[0] == ftLP2) { p.knob[kF1Cut] = r.uni (0.2f, 0.45f); p.knob[kF1Res] = r.uni (0.35f, 0.6f); }
    // a band-pass swept to the top leaves only hiss: it stays in the middle
    if (p.filterType[0] == ftBandpass) { p.knob[kF1Cut] = r.uni (0.25f, 0.45f); p.knob[kF1Res] = r.uni (0.5f, 0.7f); }
    if (p.filterType[1] == ftLP2 || p.filterType[1] == ftScream) p.knob[kF2Res] = r.uni (0.4f, 0.65f);
    p.knob[kSerPar] = 0.0f; p.knob[kFltMix] = 1.0f;
    p.knob[kF1Gain] = r.uni (0.54f, 1.0f); p.knob[kF2Gain] = r.uni (0.55f, 0.71f);
    // the envelopes. 3: the filter, held with one sustain pass; 1: timbre; 4: the amp, a gate with a short release
    env (p, 2, r.uni (0.19f, 0.43f), r.uni (0.24f, 0.38f), 0.41f, 0.3f);
    p.knob[kEnv1AttL + 2] = 0.92f; p.knob[kEnv1SusT + 2] = r.uni (0.88f, 0.94f); p.knob[kEnv1SusL + 2] = 0.0f;
    p.knob[kEnv1Vel + 2] = 0.945f; p.knob[kEnv1Ktr + 2] = 0.29f;
    p.env[2].playMode = r.chance (0.7f) ? epHold : epGate; p.env[2].loopCount = 1; p.env[2].morphShapeA = esExpo2; p.env[2].morphShapeB = esExpo4;
    mod (p, kEnv1AttL + 2, 0, msVelocity, 0.4f); mod (p, kEnv1DecT + 2, 0, msVelocity, 0.18f); mod (p, kEnv1Morph + 2, 0, msKeyTrack, 1.0f);
    env (p, 0, r.uni (0.04f, 0.45f), r.uni (0.53f, 0.84f), 0.27f, 0.57f);
    p.knob[kEnv1SusT] = 0.93f; p.knob[kEnv1SusL] = 0.0f; p.knob[kEnv1Morph] = 0.077f; p.knob[kEnv1Vel] = 0.86f;
    p.env[0].loopCount = 1; p.env[0].morphShapeA = esExpo2; p.env[0].morphShapeB = esLinear;
    env (p, 3, 0.0f, 0.876f, 0.99f, r.uni (0.08f, 0.13f));
    p.knob[kEnv1SusT + 3] = 0.5f; p.knob[kEnv1SusL + 3] = 1.0f; p.knob[kEnv1Vel + 3] = r.uni (0.45f, 0.6f);
    mod (p, kF1Cut, 0, msEnv3, p.filterType[0] == ftLP2 || p.filterType[0] == ftBandpass ? r.uni (0.3f, 0.4f) : r.uni (0.55f, 0.65f));
    mod (p, kF1Bw, 0, msEnv3, r.uni (0.24f, 0.3f)); mod (p, kF1Bw, 1, msVelocity, -r.uni (0.1f, 0.33f));
    mod (p, kF2Cut, 0, msVelocity, r.chance (0.6f) ? -r.uni (0.28f, 0.66f) : r.uni (0.2f, 0.35f));
    mod (p, kF2Bw, 0, msVelocity, -r.uni (0.19f, 0.31f)); mod (p, kF2Res, 0, msVelocity, -r.uni (0.1f, 0.3f));
    if (r.chance (0.4f)) mod (p, kOsc1Pos, 0, msEnv1, r.uni (0.3f, 0.5f));
    // slow drift: a noise LFO and a triangle LFO on pitch and timbre, each by a hair
    lfo (p, 0, 0.0f, csNoise, csSine, 1.0f); p.knob[kMod5Rate] = 0.57f;
    lfo (p, 1, 0.0f, csTriangle, csSine, 1.0f); p.knob[kMod6Rate] = 0.378f;
    mod (p, kOsc1Pitch, 0, msMod5, 0.0002f); mod (p, kOsc2Pitch, 0, msMod6, 0.0007f); mod (p, kOsc3Pitch, 0, msMod6, 0.0005f);
    mod (p, kOsc1Intensity, 0, msMod6, r.uni (-0.19f, 0.19f)); mod (p, kOsc1Intensity, 1, msKeyTrack, -r.uni (0.24f, 0.65f));
    mod (p, kF1Bw, 2, msMod6, r.uni (-0.06f, 0.04f));
    // width: three unison voices spread in table position and pan
    // ... and a fifth above and below on the "Osc Pitch" macro: the unison stacks every oscillator at -7 / 0 / +7
    p.unison = 3; p.uniPitchOn = true; p.uniChordMode = true; p.uniRangeDown = -0.09f; p.uniRangeUp = 7.02f; p.knob[kUniPitch] = 0.0f;
    p.uniWtOn = r.chance (0.3f); p.uniPanOn = true; p.knob[kUniWtPos] = 0.141f; p.knob[kUniPan] = 0.0f;
    p.knob[kGlide] = 0.263f; p.knob[kVibRate] = 0.353f;
    // the space: a chorus, phaser or delay, then a long synced delay (or a reverb), mixes on macros
    p.fxType[0] = r.weighted ({ { fxChorusE, 2 }, { fxPhaser, 3 }, { fxDelay, 1 } });
    p.knob[kFx1P2] = r.uni (0.45f, 0.62f); p.knob[kFx1P3] = r.uni (0.25f, 0.95f); p.knob[kFx1P4] = r.uni (0.18f, 0.7f);
    p.fxType[1] = r.chance (0.7f) ? fxDelaySync : fxReverb;
    p.fxSync[1] = { { 3, 16, 3, 16 } };
    if (r.chance (0.5f)) p.fxSync[1] = { { 3, 8, 1, 4 } };
    p.knob[kFx2P3] = r.uni (0.46f, 0.48f); p.knob[kFx2P4] = 0.034f;
    p.knob[kFx1DW] = 0.0f; p.knob[kFx2DW] = 0.0f; p.knob[kFx2P2] = 0.0f;
    macro (p, 0, "Osc Pitch", r.chance (0.75f) ? 1.0f : r.uni (0.4f, 0.6f)); mod (p, kUniPitch, 0, msMacro1, 1.0f);
    macro (p, 1, "Fltr Attck", 0.0f); mod (p, kEnv1AttT + 2, 1, msMacro2, 0.55f);
    macro (p, 2, "Filter Mix", 0.0f); mod (p, kFltMix, 0, msMacro3, -1.0f);
    macro (p, 3, "Cutoff", 0.0f); mod (p, kF1Cut, 2, msMacro4, 0.74f);
    macro (p, 4, p.fxType[1] == fxReverb ? "Rev Mix" : "Delay Mix", r.uni (0.4f, 0.6f)); mod (p, kFx2DW, 0, msMacro5, 1.0f);
    macro (p, 5, p.fxType[1] == fxReverb ? "Rev Size" : "Dly Damp", r.uni (0.2f, 0.4f)); mod (p, kFx2P2, 0, msMacro6, 1.0f);
    macro (p, 6, p.fxType[0] == fxPhaser ? "Phasr Mix" : p.fxType[0] == fxDelay ? "Echo Mix" : "Chorus Mix", r.uni (0.45f, 0.92f)); mod (p, kFx1DW, 0, msMacro7, 1.0f);
    macro (p, 7, "Stereo", r.uni (0.6f, 0.75f)); mod (p, kUniPan, 0, msMacro8, 1.0f);
    // a gentle EQ now and then
    if (r.chance (0.35f)) { p.eqOn = true; p.knob[kEqLow] = 0.52f; p.knob[kEqPeak] = r.uni (0.55f, 0.74f); p.knob[kEqPeakFreq] = r.uni (0.8f, 1.0f); p.knob[kEqHigh] = r.uni (0.4f, 0.5f); }
}

inline void percussive (Ctx& c)
{
    clearOscs (c.p);
    const int kind = c.r.range (0, 2);      // 0 kick-like, 1 snare/clap-like, 2 hat/zap
    if (kind == 0)
    {
        osc (c.p, 0, 6, -24, 0.0f, 1.0f); oneFilter (c.p, ftLP2, cut (c.r.uni (300, 800)), 0.1f);
        env (c.p, 1, 0.0f, c.r.uni (0.3f, 0.45f), 0.0f, 0.2f); mod (c.p, kOsc1Pitch, 0, msEnv2, c.r.uni (0.25f, 0.4f));
        ampEnv (c, 0.0f, 0.0f, 0.5f, 0.6f, 0.0f, 0.0f, 0.2f, 0.35f);
        shaper (c, 0, ipPostF1, 0.2f, 0.5f);
    }
    else if (kind == 1)
    {
        osc (c.p, 0, 6, c.r.uni (-5.0f, 5.0f), 0.0f, 0.6f);
        c.p.knob[kNoiseAmp] = 1.0f; c.p.knob[kNoiseColor] = c.r.uni (0.6f, 0.95f);
        oneFilter (c.p, ftBandpass, cut (c.r.uni (900, 2500)), 0.2f, c.r.uni (0.4f, 0.7f));
        env (c.p, 1, 0.0f, 0.3f, 0.0f, 0.2f); mod (c.p, kOsc1Pitch, 0, msEnv2, 0.15f);
        ampEnv (c, 0.0f, 0.0f, 0.45f, 0.56f, 0.0f, 0.0f, 0.2f, 0.35f);
    }
    else
    {
        c.p.knob[kNoiseAmp] = 1.0f; c.p.knob[kNoiseColor] = 1.0f;
        osc (c.p, 0, tables::digital (c.r), c.r.uni (12.0f, 36.0f), c.r.uni (0.0f, 1.0f), 0.4f);
        oneFilter (c.p, ftHP2, cut (c.r.uni (2500, 7000)), c.r.uni (0.1f, 0.4f));
        ampEnv (c, 0.0f, 0.0f, 0.35f, 0.5f, 0.0f, 0.0f, 0.1f, 0.25f);
    }
    if (c.r.chance (0.4f)) fx (c.p, 0, fxReverbSmall, c.r.uni (0.1f, 0.25f), 0.4f);
    macro (c.p, 0, "Tune"); mod (c.p, kOsc1Pitch, 1, msMacro1, 0.1f);
    macro (c.p, 1, "Decay"); mod (c.p, kEnv4DecT, 0, msMacro2, 0.2f);
    macro (c.p, 2, "Tone"); mod (c.p, kF1Cut, 1, msMacro3, 0.3f);
}

inline void sweep (Ctx& c)
{
    clearOscs (c.p);
    osc (c.p, 0, tables::basic (c.r), 0, 1.0f, 0.7f); osc (c.p, 1, tables::basic (c.r), 0.1f, 0.8f, 0.6f);
    c.p.knob[kNoiseAmp] = c.r.uni (0.4f, 0.8f); c.p.knob[kNoiseColor] = c.r.uni (0.7f, 1.0f);
    oneFilter (c.p, c.r.pick ({ (int) ftLP4, (int) ftBandpass, (int) ftHP2 }), cut (c.r.uni (200, 500)), c.r.uni (0.3f, 0.55f), 0.5f);
    env (c.p, 0, c.r.uni (0.55f, 0.75f), 0.6f, 1.0f, 0.5f); mod (c.p, kF1Cut, 0, msEnv1, c.r.uni (0.45f, 0.7f));
    env (c.p, 1, c.r.uni (0.55f, 0.75f), 0.6f, 1.0f, 0.5f); mod (c.p, kOsc1Pitch, 0, msEnv2, c.r.uni (0.0f, 0.1f));
    ampEnv (c, 0.4f, 0.65f, 0.6f, 0.6f, 1.0f, 1.0f, 0.5f, 0.65f);
    wide (c, 6, 0.3f, 0.6f);
    fx (c.p, 0, fxFlangerPlus, c.r.uni (0.3f, 0.5f), 0.5f, 0.6f); fx (c.p, 1, fxReverb, c.r.uni (0.4f, 0.6f), 0.9f, 0.5f);
    macro (c.p, 0, "Sweep"); mod (c.p, kF1Cut, 1, msMacro1, 0.5f);
    macro (c.p, 1, "Noise"); mod (c.p, kNoiseAmp, 0, msMacro2, 0.5f);
    macro (c.p, 2, "Rise"); mod (c.p, kOsc1Pitch, 1, msMacro3, 0.15f); mod (c.p, kOsc2Pitch, 0, msMacro3, 0.15f);
    macro (c.p, 3, "Space"); mod (c.p, kFx2DW, 0, msMacro4, 0.35f);
}

inline void soundscape (Ctx& c, bool sfx)
{
    clearOscs (c.p);
    osc (c.p, 0, c.r.chance (0.5f) ? tables::digital (c.r) : tables::additive (c.r), c.r.pick ({ 0.0f, -12.0f, 7.0f }), c.r.uni (0.0f, 1.0f), 0.8f,
         c.r.weighted ({ { omSpectrum, 3 }, { omBendMinusPlus, 2 }, { omFormant, 2 }, { omBendPlus, 1 } }), c.r.uni (0.2f, 1.0f));
    osc (c.p, 1, tables::vocal (c.r), c.r.pick ({ 0.0f, 12.0f, 19.0f }) + c.r.uni (0.0f, 0.2f), c.r.uni (0.0f, 1.0f), c.r.uni (0.3f, 0.6f), omFormant, c.r.uni (0.3f, 0.9f));
    c.p.knob[kNoiseAmp] = c.r.uni (0.0f, 0.4f); c.p.knob[kNoiseColor] = c.r.uni (0.3f, 0.9f);
    oneFilter (c.p, c.r.pick ({ (int) ftComb, (int) ftBandpass, (int) ftLP2, (int) ftAllpass, (int) ftDoubleNotch }), cut (c.r.uni (400, 3000)), c.r.uni (0.2f, 0.6f), c.r.uni (0.3f, 0.7f));
    lfo (c.p, 0, c.r.uni (0.02f, 0.12f), c.r.pick ({ (int) csSine, (int) csTriangle, (int) csNoise }), csSine, 0.5f);
    lfo (c.p, 1, c.r.uni (0.03f, 0.15f), csTriangle, csTriangle);
    mod (c.p, kOsc1Pos, 0, msMod5, c.r.uni (0.2f, 0.5f)); mod (c.p, kF1Cut, 0, msMod6, c.r.uni (0.1f, 0.3f)); mod (c.p, kPan, 0, msMod6, 0.3f);
    mod (c.p, kOsc2Pos, 0, msMod6, c.r.uni (0.2f, 0.5f));
    if (sfx)
    {
        c.p.knob[kModOscPitch] = semi (c.r.uni (-24.0f, 24.0f)); c.p.rmTarget = c.r.range (0, 2); c.p.knob[kModOscRM] = c.r.uni (0.3f, 0.8f);
        env (c.p, 1, 0.0f, c.r.uni (0.5f, 0.7f), 0.0f, 0.5f); mod (c.p, kOsc1Pitch, 0, msEnv2, c.r.uni (-0.3f, 0.3f));
        if (c.r.chance (0.5f)) { c.p.insertType[0] = c.r.pick ({ (int) itFreqShift, (int) itSampleHold, (int) itBitcrush }); c.p.insertPos[0] = ipPostF1; c.p.knob[kIns1P1] = 0.8f; c.p.knob[kIns1P2] = c.r.uni (0.3f, 0.7f); }
        ampEnv (c, 0.0f, 0.2f, 0.6f, 0.75f, 0.0f, 0.5f, 0.4f, 0.6f);
    }
    else ampEnv (c, 0.5f, 0.68f, 0.6f, 0.6f, 1.0f, 1.0f, 0.55f, 0.7f);
    if (c.r.chance (0.5f) && ! sfx) { c.p.fbPos = fbPostF1; c.p.knob[kFbAmp] = c.r.uni (0.1f, 0.35f); }
    fx (c.p, 0, c.r.pick ({ (int) fxDelay, (int) fxFlangerMinus, (int) fxPhaser1, (int) fxDimExp }), c.r.uni (0.3f, 0.5f), c.r.uni (0.4f, 0.8f), c.r.uni (0.4f, 0.7f));
    fx (c.p, 1, fxReverb, c.r.uni (0.45f, 0.7f), c.r.uni (0.8f, 1.0f), c.r.uni (0.4f, 0.6f));
    macro (c.p, 0, "Drift"); mod (c.p, kMod5Rate, 0, msMacro1, 0.3f);
    macro (c.p, 1, "Colour"); mod (c.p, kF1Cut, 1, msMacro2, 0.4f);
    macro (c.p, 2, "Texture"); mod (c.p, kNoiseAmp, 0, msMacro3, 0.4f);
    macro (c.p, 3, "Space"); mod (c.p, kFx2DW, 0, msMacro4, 0.3f);
}

// ------------------------------------------------------------------ genre touches
inline void genreTouch (Ctx& c, int type)
{
    auto& p = c.p;
    switch (c.genre)
    {
        case gTechno:
            if (c.r.chance (0.5f) && p.insertType[1] == itNone) { p.insertType[1] = itClip; p.insertPos[1] = ipPreAmp; p.knob[kIns2P1] = 0.7f; p.knob[kIns2P2] = c.r.uni (0.2f, 0.4f); }
            break;
        case gIndustrial:
            if (p.insertType[1] == itNone) { p.insertType[1] = c.r.pick ({ (int) itBitcrush, (int) itSampleHold, (int) itFreqShift }); p.insertPos[1] = ipPreAmp; p.knob[kIns2P1] = c.r.uni (0.4f, 0.8f); p.knob[kIns2P2] = c.r.uni (0.3f, 0.6f); }
            if (c.r.chance (0.4f) && p.filterType[1] == ftNone) { p.filterType[1] = ftComb; p.knob[kF2Cut] = cut (c.r.uni (300, 1500)); p.knob[kF2Res] = c.r.uni (0.3f, 0.6f); p.knob[kFltMix] = 0.7f; }
            break;
        case gSynthwave:
            for (int o = 0; o < 3; ++o) if (p.oscOn[(size_t) o] && p.wavetable[(size_t) o] > 8 && c.r.chance (0.6f)) p.wavetable[(size_t) o] = c.r.pick ({ 0, 2, 7 });
            if (type >= tMonoLead && type <= tVoxLead) { p.knob[kVibDepth] = 0.04f; }
            fxc (c, 0, fxChorusE, c.r.uni (0.35f, 0.5f));
            break;
        case gHipHop:
            p.knob[kF1Cut] = std::max (0.0f, p.knob[kF1Cut] - c.r.uni (0.03f, 0.08f));
            if (p.fxType[0] == fxNone || c.r.chance (0.5f)) fxc (c, 0, fxTubeTube, c.r.uni (0.25f, 0.4f), 0.4f);
            break;
        case gAmbient:
            if (type < tDeepPad || type > tChoir) { p.knob[kEnv4RelT] = std::max (p.knob[kEnv4RelT], c.r.uni (0.5f, 0.65f)); }
            fxc (c, 1, fxReverb, c.r.uni (0.4f, 0.6f), c.r.uni (0.85f, 1.0f), 0.5f, 0.6f);
            break;
        case gHouse:
            if (type == tStab || type == tChordPad || type == tKeys || type == tOrgan) { p.fxType[1] = fxReverbSmall; p.knob[kFx2DW] = c.r.uni (0.15f, 0.3f); }
            break;
        default: break;
    }
}


// ------------------------------------------------------------------ the entry points
inline int genreDefaultType (int genre, Rng& r)
{
    switch (genre)
    {
        case gDubstep:   return r.pick ({ (int) tWobbleBass, (int) tGrowlBass, (int) tGrowlBass, (int) tSubBass, (int) tDirtyLead, (int) tSweep });
        case gDnB:       return r.pick ({ (int) tReeseBass, (int) tReeseBass, (int) tSubBass, (int) tDigitalBass, (int) tStab, (int) tDeepPad });
        case gHouse:     return r.pick ({ (int) tStab, (int) tChordPad, (int) tOrgan, (int) tAnalogBass, (int) tPluck, (int) tKeys });
        case gTechno:    return r.pick ({ (int) tAcidLine, (int) tSequence, (int) tAnalogBass, (int) tStab, (int) tPercSynth, (int) tSoundscape });
        case gTrance:    return r.pick ({ (int) tHugeLead, (int) tPluck, (int) tGate, (int) tBrightPad, (int) tSequence, (int) tSweep });
        case gElectro:   return r.pick ({ (int) tDigitalBass, (int) tDirtyLead, (int) tSyncLead, (int) tSequence, (int) tStab });
        case gHipHop:    return r.pick ({ (int) tSubBass, (int) tSoftLead, (int) tKeys, (int) tDeepPad, (int) tMallet, (int) tBrass });
        case gAmbient:   return r.pick ({ (int) tEvolvingPad, (int) tAiryPad, (int) tSoundscape, (int) tMallet, (int) tChoir, (int) tDeepPad });
        case gIndustrial:return r.pick ({ (int) tDirtyPad, (int) tGrowlBass, (int) tSfx, (int) tPercSynth, (int) tDirtyLead });
        case gSynthwave: return r.pick ({ (int) tPolyLead, (int) tAnalogBass, (int) tBrightPad, (int) tSequence, (int) tBrass, (int) tStrings });
        case gDubTechno: return r.pick ({ (int) tDubChord, (int) tDubChord, (int) tDubChord, (int) tDubChord, (int) tDeepPad, (int) tSubBass });
        default:         return r.range (0, tAnything - 1);
    }
}

inline std::string nameFor (int type, Rng& r)
{
    static const char* adj[] = { "Deep", "Cold", "Salt", "Blue", "Glass", "Hollow", "Rust", "Night", "Static", "Tidal", "Chrome", "Murky", "Bright",
                                 "Low", "Neon", "Drift", "Iron", "Silver", "Sunken", "Foggy", "Electric", "Lunar", "Coral", "Brass", "Dusty", "Wired" };
    static const char* bassN[] = { "Hull", "Keel", "Trench", "Anchor", "Current", "Pressure", "Rumble", "Engine", "Floor", "Wake" };
    static const char* leadN[] = { "Beacon", "Signal", "Flare", "Siren", "Laser", "Whistle", "Ray", "Lamp", "Spark", "Arc" };
    static const char* padN[] = { "Haze", "Tide", "Horizon", "Mist", "Shelf", "Lagoon", "Aurora", "Swell", "Dawn", "Cloud" };
    static const char* keyN[] = { "Keys", "Bells", "Drops", "Harp", "Chimes", "Tines", "Pluck", "Ripple", "Mallets", "Horns" };
    static const char* dtN[] = { "Chords", "Stab", "Echo Chord", "Dub", "Chord Haze", "Basement", "Spiral", "Fog Chord", "Pressure Chord", "Tape Chord" };
    static const char* fxN[] = { "Sonar", "Riser", "Pulse", "Machine", "Grid", "Loop", "Shift", "Transmission", "Shoal", "Debris" };
    const char* const* pool = fxN;
    if (type == tDubChord) pool = dtN;
    else if (type <= tAcidLine) pool = bassN; else if (type <= tVoxLead) pool = leadN; else if (type <= tChoir) pool = padN; else if (type <= tBrass) pool = keyN;
    return std::string (adj[r.range (0, 25)]) + " " + pool[r.range (0, 9)];
}

// Generates a patch of the given type (tAnything: picked from the genre) into p.
inline void generate (Patch& p, int type, int genre, uint32_t seed)
{
    Rng r (seed);
    if (type < 0 || type >= tAnything) type = genre != gAny ? genreDefaultType (genre, r) : r.range (0, tAnything - 1);
    else if (type == tAnything) type = genreDefaultType (genre, r);
    p = Patch();
    for (int o = 0; o < 3; ++o) p.knob[(size_t) (kOsc1Route + 5 * o)] = 1.0f;
    p.knob[kNoiseAmp] = 0.0f;
    Ctx c { p, r, genre };
    switch (type)
    {
        case tSubBass: subBass (c); break;
        case tAnalogBass: analogBass (c); break;
        case tDigitalBass: digitalBass (c); break;
        case tGrowlBass: growlBass (c, false); break;
        case tWobbleBass: growlBass (c, true); break;
        case tReeseBass: reeseBass (c); break;
        case tAcidLine: acidLine (c); break;
        case tMonoLead: case tPolyLead: case tHugeLead: case tDirtyLead: case tSyncLead: case tSoftLead: case tVoxLead: lead (c, type); break;
        case tDeepPad: case tEvolvingPad: case tBrightPad: case tAiryPad: case tDirtyPad: case tChordPad: case tStrings: case tChoir: pad (c, type); break;
        case tKeys: case tOrgan: case tMallet: case tPluck: case tBrass: keys (c, type); break;
        case tSequence: case tGate: sequence (c, type); break;
        case tStab: stab (c); break;
        case tPercSynth: percussive (c); break;
        case tSweep: sweep (c); break;
        case tSoundscape: soundscape (c, false); break;
        case tSfx: soundscape (c, true); break;
        case tDubChord: dubChord (c); break;
        default: analogBass (c); break;
    }
    genreTouch (c, type);
    p.presetBpm = genre == gDubTechno ? 122.0f : genre == gDnB ? 174.0f : genre == gDubstep ? 140.0f : genre == gTrance ? 138.0f : genre == gHipHop ? 90.0f : genre == gTechno ? 130.0f : 124.0f;
    p.knob[kMasterVol] = 0.8f;
    p.name = nameFor (type, r);
}

// Every knob and selector across its range, with only the guards that keep
// the result audible and not deafening.
inline void superRandom (Patch& p, uint32_t seed)
{
    Rng r (seed);
    p = Patch();
    for (int k = 0; k < kNumKnobs; ++k)
        if (k >= kMacro1 + 8) p.knob[(size_t) k] = r.uni (0.0f, 1.0f);
    for (int o = 0; o < 3; ++o) { p.wavetable[(size_t) o] = r.range (0, 84); p.oscMode[(size_t) o] = r.range (0, omNum - 1); p.oscOn[(size_t) o] = r.chance (0.75f); }
    p.oscOn[0] = true; p.knob[kOsc1Amp] = r.uni (0.6f, 1.0f); p.knob[kOsc1Pitch] = semi ((float) r.range (-12, 12));
    for (int f = 0; f < 2; ++f) p.filterType[(size_t) f] = r.range (0, ftNum - 1);
    for (int i = 0; i < 2; ++i) { p.insertType[(size_t) i] = r.chance (0.5f) ? r.range (1, itNum - 1) : itNone; p.insertPos[(size_t) i] = r.range (1, ipNum - 1); }
    for (int i = 0; i < 2; ++i) p.fxType[(size_t) i] = r.range (0, fxNum - 1);
    for (int m = 0; m < 4; ++m) { p.modType[(size_t) m] = r.range (0, 1); p.lfo[(size_t) m].shapeA = r.range (0, csNum - 1); p.lfo[(size_t) m].shapeB = r.range (0, csNum - 1); }
    // a few random routings
    const int n = r.range (3, 9);
    for (int i = 0; i < n; ++i) mod (p, r.range (kOsc1Pitch + 1, kEqHigh), r.range (0, 1), r.range (msEnv1, msMod8), r.uni (-0.5f, 0.5f));
    // guards: an audible amp envelope, a filter that is not shut, no runaway feedback, sane volume
    env (p, 3, r.uni (0.0f, 0.4f), r.uni (0.4f, 0.7f), r.uni (0.5f, 1.0f), r.uni (0.2f, 0.6f));
    p.knob[kF1Cut] = std::max (p.knob[kF1Cut], 0.35f); p.knob[kF2Cut] = std::max (p.knob[kF2Cut], 0.35f);
    p.knob[kFbAmp] = std::min (p.knob[kFbAmp], 0.4f); p.knob[kMasterVol] = 0.7f; p.knob[kBypassGain] = 0.0f;
    p.knob[kGlide] = std::min (p.knob[kGlide], 0.3f); p.knob[kVibDepth] = std::min (p.knob[kVibDepth], 0.1f);
    p.voiceMode = r.range (0, 1); p.unison = r.chance (0.3f) ? r.range (2, 6) : 1;
    p.name = "Wild " + std::to_string (seed % 10000);
}
} // namespace generator
} // namespace massif
