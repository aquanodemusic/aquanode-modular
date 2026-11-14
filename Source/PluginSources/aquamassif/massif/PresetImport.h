#pragma once
// Massif - reading the original's .nmsv sound files (and its plugin state
// chunks) into a Patch.
//
// What is understood (format study, black-box: chunks of the original made by
// changing one host parameter at a time, and the factory files):
//  - container: a chunked file whose payload is LZF-compressed ("01 <u32 raw> <u32 packed>")
//  - the 137 knob values (normalized, in host-parameter order)
//  - knob records: [u32 id][00][u32 v|selector][f32 lo][f32 hi][u32 n][n x (f32 amount, i32 source, u8, u8)]
//  - selectors stored in the records' second word (osc mode / wavetable, filter
//    types, insert types, noise type, feedback position, mod osc targets) and
//    the FX types / voicing / bypass at fixed distances from them
// Modulation: sources 0-3 envelopes, 4-7 the four modulation pages, 14-21
// macros; the slot lists of the oscillator, envelope, insert/FX and EQ knobs.
#include "Patch.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace massif
{
namespace nmsv
{
    inline uint32_t u32 (const std::vector<uint8_t>& b, size_t o) { uint32_t v = 0; if (o + 4 <= b.size()) std::memcpy (&v, b.data() + o, 4); return v; }
    inline int32_t i32 (const std::vector<uint8_t>& b, size_t o) { return (int32_t) u32 (b, o); }
    inline float f32 (const std::vector<uint8_t>& b, size_t o) { float v = 0; if (o + 4 <= b.size()) std::memcpy (&v, b.data() + o, 4); return v; }

    // The payload ends: ... [u32 x4][u32 0..2][f32 preset BPM][u8 flag][u8 flag][00 00][00 00 0d 62 65 85 01 00]
    // [00 x6] - the preset BPM 22 bytes before the end (1350 of the 1353 factory sounds have this tail; the loop
    // sounds' BPMs in their names match it). Its place, or 0 without that tail.
    inline size_t presetBpmPos (const std::vector<uint8_t>& b)
    {
        static const uint8_t tail[8] = { 0x00, 0x00, 0x0d, 0x62, 0x65, 0x85, 0x01, 0x00 };
        if (b.size() < 200 || std::memcmp (b.data() + b.size() - 14, tail, 8) != 0) return 0;
        return b.size() - 22;
    }
    inline float presetBpmAt (const std::vector<uint8_t>& b)
    {
        const size_t o = presetBpmPos (b); if (o == 0) return 0.0f;
        const float v = f32 (b, o);
        return v >= 20.0f && v <= 999.0f ? v : 0.0f;
    }

    inline std::vector<uint8_t> lzf (const uint8_t* d, size_t n, size_t outLen)
    {
        std::vector<uint8_t> o; o.reserve (outLen); size_t i = 0;
        while (i < n && o.size() < outLen)
        {
            const unsigned c = d[i++];
            if (c < 32) { const size_t len = c + 1; if (i + len > n) break; o.insert (o.end(), d + i, d + i + len); i += len; }
            else
            {
                size_t len = c >> 5; if (len == 7) { if (i >= n) break; len += d[i++]; }
                if (i >= n) break;
                const size_t back = ((size_t) (c & 0x1f) << 8) + d[i++] + 1;
                if (back > o.size()) break;
                const size_t ref = o.size() - back;
                for (size_t k = 0; k < len + 2; ++k) o.push_back (o[ref + k]);
            }
        }
        return o;
    }

    inline bool payload (const std::vector<uint8_t>& f, std::vector<uint8_t>& raw)
    {
        for (size_t p = 0; p + 9 < f.size(); ++p)
        {
            if (f[p] != 1) continue;
            const uint32_t rawLen = u32 (f, p + 1), cl = u32 (f, p + 5);
            if (rawLen > 100 && rawLen < 10000000 && cl > 10 && cl <= f.size() - p - 9 && cl <= rawLen + rawLen / 31 + 64 && p + 9 + cl + 40 >= f.size())
            {
                raw = lzf (f.data() + p + 9, cl, rawLen);
                if (raw.size() == rawLen) return true;
            }
        }
        return false;
    }

    struct Rec { size_t at; int id; int second; int extra; std::vector<std::pair<float, int>> slots; };

    inline bool slotOk (const std::vector<uint8_t>& b, size_t o)
    {
        if (o + 10 > b.size()) return false;
        const float a = f32 (b, o); const int s = i32 (b, o + 4);
        return a == a && a >= -16.0f && a <= 16.0f && (s == -1 || (s >= 0 && s <= 40)) && b[o + 8] <= 1 && b[o + 9] <= 1;
    }
    inline int slotsAt (const std::vector<uint8_t>& b, size_t o)
    {
        const uint32_t c = u32 (b, o);
        if (c < 1 || c > 4) return 0;
        for (uint32_t k = 0; k < c; ++k) if (! slotOk (b, o + 4 + 10 * k)) return 0;
        return (int) c;
    }
    inline bool header (const std::vector<uint8_t>& b, size_t o)
    {
        return o + 9 <= b.size() && u32 (b, o) < 200 && b[o + 4] == 0 && u32 (b, o + 5) >= 1 && u32 (b, o + 5) <= 24;
    }
    inline std::vector<Rec> records (const std::vector<uint8_t>& b)
    {
        static const int pref[] = { 8, 16, 12, 20, 24, 4, 0, 28, 32, 36, 38, 40, 44, 48 };
        std::vector<Rec> out; size_t o = 0;
        while (o + 20 < b.size())
        {
            if (header (b, o))
            {
                int bestScore = -1, bestEx = 0, bestN = 0;
                for (int ex : pref)
                {
                    const size_t q = o + 9 + (size_t) ex; const int n = slotsAt (b, q);
                    if (n == 0) continue;
                    const size_t e = q + 4 + 10 * (size_t) n;
                    const int score = (header (b, e) ? 2 : 0) + ((ex == 8 || ex == 16 || ex == 12) ? 1 : 0);
                    if (score > bestScore) { bestScore = score; bestEx = ex; bestN = n; }
                    if (score >= 3) break;
                }
                if (bestScore >= 1)
                {
                    Rec r; r.at = o; r.id = (int) u32 (b, o); r.second = (int) u32 (b, o + 5); r.extra = bestEx;
                    const size_t q = o + 9 + (size_t) bestEx;
                    for (int k = 0; k < bestN; ++k) r.slots.push_back ({ f32 (b, q + 4 + 10 * (size_t) k), i32 (b, q + 8 + 10 * (size_t) k) });
                    out.push_back (r);
                    o = q + 4 + 10 * (size_t) bestN; continue;
                }
            }
            ++o;
        }
        return out;
    }

    // the sound's name in the container: the first length-prefixed UTF-16 string 32 bytes after a "DSIN" (as UTF-8)
    inline std::string soundName (const std::vector<uint8_t>& f)
    {
        for (size_t s = 0; s + 40 < f.size() && s < 4096; ++s)
            if (std::memcmp (f.data() + s, "DSIN", 4) == 0)
            {
                const uint32_t n = u32 (f, s + 32);
                if (n < 1 || n > 255 || s + 36 + 2 * (size_t) n > f.size()) continue;
                std::string out; bool ok = true;
                for (uint32_t i = 0; i < n && ok; ++i)
                {
                    const uint32_t c = (uint32_t) f[s + 36 + 2 * i] | ((uint32_t) f[s + 37 + 2 * i] << 8);
                    if (c < 32) { ok = false; break; }
                    if (c < 0x80) out += (char) c;
                    else if (c < 0x800) { out += (char) (0xc0 | (c >> 6)); out += (char) (0x80 | (c & 63)); }
                    else { out += (char) (0xe0 | (c >> 12)); out += (char) (0x80 | ((c >> 6) & 63)); out += (char) (0x80 | (c & 63)); }
                }
                if (ok) return out;
            }
        return {};
    }

    // internal enum ids (as stored) -> menu index, measured
    inline int menuOf (const int* table, int n, int stored, int fallback)
    {
        for (int i = 0; i < n; ++i) if (table[i] == stored) return i;
        return fallback;
    }
    static const int kWavetableIds[87] = { -2, -4, 0, 73, 31, 1, 51, 44, 46, 35, 41, 50, 47, 57, 52, 36, 76, 34, 37, 38, 39, 45, 77, 48, 49, 56, 83, 9, 63, 21, 22, 23, 27, 28, 2, 3, 19, 16, 70, 29, 69, 68, 60, 8, 72, 6, 14, 59, 62, 7, 81, 54, 4, 18, 12, 53, 65, 66, 5, 17, 11, 71, 20, 24, 25, 80, 26, 32, 33, 43, 10, 30, 40, 42, 58, 74, 67, 64, 75, 78, 79, 15, 61, 13, 55, 82, 84 };
    static const int kModeIds[5] = { 5, 2, 3, 4, 1 };
    static const int kFilterIds[13] = { 11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12 };
    static const int kInsertIds[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    static const int kFxIds[18] = { 17, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 13, 15, 16 };
    static const int kFbIds[5] = { -1, 0, 1, 2, 3 };

    // modulation sources as stored: 0-3 envelopes, 4-7 the mod pages, 13-20 the macros, then the MIDI ones
    inline int mapSource (int s)
    {
        if (s >= 0 && s <= 7) return s;
        if (s >= 13 && s <= 20) return msMacro1 + (s - 13);
        if (s == 21) return msKeyTrack;
        if (s == 22) return msVelocity;
        if (s == 23) return msAftertouch;
        if (s == 24) return msTrigRandom;
        if (s >= 8 && s <= 11) return msIntEnv5 + (s - 8);   // the mod pages' internal envelopes
        if (s == 12) return msOscEnv;                        // the OSC page's internal envelope
        return msNone;
    }

    // A knob as stored: [u32 version 2|3][f32 range lo][f32 range hi][u32 n][n slots] and then its tag,
    // [u32 knob index][00] - the index is the knob's place in the automation list. (The tag follows
    // its knob; reading it as a header attributes every slot to the knob before.)
    struct KnobSlots { int knob; size_t tagAt; float lo, hi; std::vector<std::pair<float, int>> slots; };
    inline std::vector<KnobSlots> knobSlots (const std::vector<uint8_t>& b)
    {
        std::vector<KnobSlots> out; size_t o = 0;
        while (o + 27 < b.size())
        {
            const uint32_t ver = u32 (b, o);
            if (ver == 2 || ver == 3 || ver == 4)
            {
                // most knobs: [ver][f32 lo][f32 hi][u32 n]...; a few (the unison spreads) carry their range twice:
                // [ver][f32 lo][f32 hi - lo][f32 lo][f32 hi][u32 n]...
                const bool twoRanges = slotsAt (b, o + 12) <= 0 && o + 40 < b.size() && f32 (b, o + 4) == f32 (b, o + 12)
                                       && std::abs (f32 (b, o + 4) + f32 (b, o + 8) - f32 (b, o + 16)) < 1.0e-4f;
                const size_t sh = twoRanges ? 8 : 0;
                const int n = slotsAt (b, o + 12 + sh);
                if (n > 0)
                {
                    const size_t e = o + 16 + sh + 10 * (size_t) n;
                    const float lo = f32 (b, o + 4), hi = twoRanges ? f32 (b, o + 16) : f32 (b, o + 8);
                    if (e + 5 <= b.size() && lo == lo && hi == hi && std::abs (lo) < 1.0e4f && std::abs (hi) < 1.0e4f && u32 (b, e) < (uint32_t) kNumKnobs && b[e + 4] == 0)
                    {
                        KnobSlots k; k.knob = (int) u32 (b, e); k.tagAt = e; k.lo = lo; k.hi = hi;
                        for (int i = 0; i < n; ++i) k.slots.push_back ({ f32 (b, o + 16 + sh + 10 * (size_t) i), i32 (b, o + 20 + sh + 10 * (size_t) i) });
                        out.push_back (std::move (k));
                        o = e + 5;
                        // the modulation pages follow the glide knob: step over them (an LFO page can read like a
                        // knob record - shapes, phases - and once gave knob 2 the phases as modulation)
                        if (out.back().knob == 126)
                        {
                            size_t p = o + 17; bool ok = true;
                            for (int m = 0; m < 4 && ok; ++m)
                            {
                                const uint32_t t = u32 (b, p);
                                if (t == 0) p += 24; else if (t == 1) p += 336; else if (t == 2) p += 144; else ok = false;
                            }
                            if (ok && u32 (b, p) == 3) o = p + 4;
                        }
                        continue;
                    }
                }
            }
            ++o;
        }
        return out;
    }
}

// The modulation pages 5..8 (found by changing each control in the original's own editor and comparing
// the saved state):
//  - right after the GLIDE-TIME knob (tag 126): 17 bytes, then per page [u32 type] + its block,
//    then [u32 3].  type 0 LFO: [u32 2][f32 phase A][f32 phase B][u32 shape A][u32 shape B];
//    type 1 performer: [u32 1] 16 x [f32 level A][u32 curve A][f32 level B][u32 curve B][u8 xfade row],
//    [u32 2] 16 x [u8 amp mod row][u8 1][u8 0], [u32 play marker];
//    type 2 stepper: [u32 2] 16 x f32 step, 16 x u8 glide row, [u32 2] 16 x [u8 amp mod row][u8 1][u8 0], [u32 marker]
//    (which row belongs to which fader was found by rendering crafted sounds with the original)
//  - after each page's Amp Mod fader tag (70..73): [u32 loop start][u32 loop end][u8 mono]
//  - after each page's rate tag (58..61): [u8 sync][u8 position sync (newer files)][u8 restart][u32 ratio
//    numerator][u32 ratio denominator as a power of two]
//  - after that of page 8: 5 x [u32 2][f32 attack][f32 decay], the internal envelopes (pages 5..8, vibrato)
inline void importModPages (const std::vector<uint8_t>& raw, const std::array<size_t, kNumKnobs>& tagAt, Patch& q)
{
    using namespace nmsv;
    const size_t none = (size_t) -1;
    auto fin = [] (float v, float lo, float hi, float d) { return (v == v && v >= lo && v <= hi) ? v : d; };
    // the internal envelopes also tell the layout of the rate tags' bytes (10 or 11 bytes)
    int rateGap = 0;
    if (tagAt[61] != none)
        for (int d : { 11, 10 })
        {
            const size_t o = tagAt[61] + 5 + (size_t) d;
            bool ok = o + 60 <= raw.size();
            for (int k = 0; k < 5 && ok; ++k)
            {
                const float a = f32 (raw, o + 12 * (size_t) k + 4), b = f32 (raw, o + 12 * (size_t) k + 8);
                ok = u32 (raw, o + 12 * (size_t) k) == 2 && a >= 0 && a <= 1 && b >= 0 && b <= 1;
            }
            if (ok)
            {
                rateGap = d;
                for (int k = 0; k < 5; ++k) { q.intAttack[(size_t) k] = f32 (raw, o + 12 * (size_t) k + 4); q.intDecay[(size_t) k] = f32 (raw, o + 12 * (size_t) k + 8); }
                break;
            }
        }
    for (int m = 0; m < 4; ++m)
    {
        const size_t tr = tagAt[(size_t) (58 + m)];
        if (tr != none && rateGap != 0 && tr + 20 < raw.size())
        {
            const size_t nOff = rateGap == 11 ? 8 : 7;
            const uint32_t num = u32 (raw, tr + nOff), den = u32 (raw, tr + nOff + 4);
            if (num >= 1 && num <= 64 && den <= 8) { q.syncNum[(size_t) m] = (int) num; q.syncDen[(size_t) m] = 1 << den; }
        }
        const size_t tl = tagAt[(size_t) (70 + m)];
        if (tl != none && tl + 14 <= raw.size())
        {
            const uint32_t a = u32 (raw, tl + 5), b = u32 (raw, tl + 9);
            if (a <= 15 && b <= 15 && a <= b)
            {
                q.stepper[(size_t) m].loopStart = q.performer[(size_t) m].loopStart = (int) a;
                q.stepper[(size_t) m].loopEnd = q.performer[(size_t) m].loopEnd = (int) b;
            }
            const bool mono = raw[tl + 13] != 0;
            q.lfo[(size_t) m].mono = q.stepper[(size_t) m].mono = q.performer[(size_t) m].mono = mono;
        }
    }
    // the envelopes' switches, after their release knobs' tags (122..125): [u32 shape A][u32 shape B]
    // [u32 loop count (33 = infinite)][u8 trigger zero reset][u8 linear][u8 play mode]
    for (int e = 0; e < 4; ++e)
    {
        const size_t te = tagAt[(size_t) (kEnv1RelT + e)];
        if (te == none || te + 20 > raw.size()) continue;
        const uint32_t sa = u32 (raw, te + 5), sb = u32 (raw, te + 9), lc = u32 (raw, te + 13);
        if (sa >= (uint32_t) esNum || sb >= (uint32_t) esNum || lc > 33 || raw[te + 17] > 1 || raw[te + 18] > 1 || raw[te + 19] > 2) continue;
        auto& x = q.env[(size_t) e];
        x.morphShapeA = (int) sa; x.morphShapeB = (int) sb; x.loopCount = (int) lc;
        x.retrigger = raw[te + 17] != 0; x.linearDecay = raw[te + 18] != 0; x.playMode = raw[te + 19];
    }
    if (tagAt[126] == none) return;
    Patch t = q;
    size_t o = tagAt[126] + 5 + 17;
    for (int m = 0; m < 4; ++m)
    {
        if (o + 340 > raw.size()) return;
        const uint32_t type = u32 (raw, o);
        if (type == 0)
        {
            auto& l = t.lfo[(size_t) m]; t.modType[(size_t) m] = mtLFO;
            l.phaseA = fin (f32 (raw, o + 8), -4, 4, 0); l.phaseB = fin (f32 (raw, o + 12), -4, 4, 0);
            l.shapeA = (int) std::min (u32 (raw, o + 16), (uint32_t) (csNum - 1)); l.shapeB = (int) std::min (u32 (raw, o + 20), (uint32_t) (csNum - 1));
            o += 24;
        }
        else if (type == 1)
        {
            auto& pf = t.performer[(size_t) m]; t.modType[(size_t) m] = mtPerformer;
            for (size_t k = 0; k < 16; ++k)
            {
                const size_t r = o + 8 + 17 * k;
                pf.levelA[k] = fin (f32 (raw, r), 0, 1, 1); pf.curveA[k] = (int) std::min (u32 (raw, r + 4), (uint32_t) (pcNum - 1));
                pf.levelB[k] = fin (f32 (raw, r + 8), 0, 1, 1); pf.curveB[k] = (int) std::min (u32 (raw, r + 12), (uint32_t) (pcNum - 1));
                pf.morphOn[k] = raw[r + 16] != 0; pf.ampOn[k] = raw[o + 284 + 3 * k] != 0;
            }
            pf.startStep = (int) std::min (u32 (raw, o + 332), 15u);
            o += 336;
        }
        else if (type == 2)
        {
            auto& st = t.stepper[(size_t) m]; t.modType[(size_t) m] = mtStepper;
            for (size_t k = 0; k < 16; ++k)
            {
                st.step[k] = fin (f32 (raw, o + 8 + 4 * k), 0, 1, 0);
                st.glideOn[k] = raw[o + 72 + k] != 0; st.ampOn[k] = raw[o + 92 + 3 * k] != 0;
            }
            st.startStep = (int) std::min (u32 (raw, o + 140), 15u);
            o += 144;
        }
        else return;      // not understood: keep the defaults
    }
    if (o + 4 <= raw.size() && u32 (raw, o) == 3) q = t;
}

// Keytracking (the KTR OSC / KTR FLT pages): five curves - the oscillators' user curve, then cutoff and
// resonance curves of filter sets 1 and 2 - each [u32 2][u32 128][u32 128][u8 n] and 2n - 2 floats: y of the
// first point, x / y of the inner points, y of the last (x 0 and 1 implied), notes / 127. After the first:
// per oscillator 1..3, mod osc and inserts [u32 1][u32 0 off / 1 linear / 2 curve][u32 60]. After the last:
// per filter [u32 1][u32 set (1, 2; else off)][u32 60][u32 0].
namespace nmsv
{
    // a keytracking curve block at o: [u32 2][u32 128][u32 128][u8 n] and 2n - 2 floats, or the older form
    // [u32 1][u32 128][u32 128] and 8 floats (always five points: a third of the factory sounds)
    struct CurveBlock { int version { 0 }, n { 0 }; size_t floats { 0 }, end { 0 }; };
    inline bool curveAt (const std::vector<uint8_t>& raw, size_t o, CurveBlock& cb)
    {
        if (o + 13 > raw.size() || u32 (raw, o + 4) != 128 || u32 (raw, o + 8) != 128) return false;
        if (u32 (raw, o) == 2 && raw[o + 12] >= 2 && raw[o + 12] <= 16) { cb = { 2, raw[o + 12], o + 13, o + 13 + 4 * (size_t) (2 * raw[o + 12] - 2) }; }
        else if (u32 (raw, o) == 1) { cb = { 1, 5, o + 12, o + 12 + 32 }; }
        else return false;
        if (cb.end > raw.size()) return false;
        for (size_t i = 0; i < (size_t) (2 * cb.n - 2); ++i) { const float v = f32 (raw, cb.floats + 4 * i); if (! (v >= -0.01f && v <= 1.01f)) return false; }
        return true;
    }
}

inline void importKeyTracking (const std::vector<uint8_t>& raw, Patch& q)
{
    using namespace nmsv;
    std::array<KeyCurve, 5> c; int found = 0; size_t o = 0x100, end = std::min (raw.size(), (size_t) 0x1000);
    while (found < 5 && o + 17 < end)
    {
        CurveBlock cb;
        if (curveAt (raw, o, cb))
        {
            if (found == 0 && o >= 36)
            {
                // the kept bytes before the first curve (see Patch::nmsvKeep)
                q.nmsvKeep = { raw[o - 12], raw[o - 11] };
            }
            const int n = cb.n; const size_t f = cb.floats;
            std::vector<float> xs, ys; bool ok = true;
            xs.push_back (0.0f); ys.push_back (f32 (raw, f));
            for (int i = 1; i < n - 1; ++i) { xs.push_back (f32 (raw, f + 4 * (size_t) (2 * i - 1))); ys.push_back (f32 (raw, f + 4 * (size_t) (2 * i))); }
            xs.push_back (1.0f); ys.push_back (f32 (raw, f + 4 * (size_t) (2 * n - 3)));
            for (size_t i = 0; i < xs.size(); ++i) ok = ok && xs[i] == xs[i] && ys[i] == ys[i] && xs[i] >= -0.01f && xs[i] <= 1.01f && ys[i] >= -0.01f && ys[i] <= 1.01f && (i == 0 || xs[i] >= xs[i - 1]);
            if (ok)
            {
                // the patch keeps five breakpoints: resample longer curves at the stored x positions (first, last
                // and three spread inner ones)
                auto& kc = c[(size_t) found];
                // more than five points: drop the inner points that lie closest to the line through their neighbours
                while (xs.size() > 5)
                {
                    size_t best = 1; float bestD = 1.0e9f;
                    for (size_t i = 1; i + 1 < xs.size(); ++i)
                    {
                        const float t = (xs[i] - xs[i - 1]) / std::max (1.0e-6f, xs[i + 1] - xs[i - 1]);
                        const float d = std::abs (ys[i] - (ys[i - 1] + t * (ys[i + 1] - ys[i - 1])));
                        if (d < bestD) { bestD = d; best = i; }
                    }
                    xs.erase (xs.begin() + (long) best); ys.erase (ys.begin() + (long) best);
                }
                while (xs.size() < 5)   // fewer: split the widest gap
                {
                    size_t w = 0; for (size_t i = 0; i + 1 < xs.size(); ++i) if (xs[i + 1] - xs[i] > xs[w + 1] - xs[w]) w = i;
                    xs.insert (xs.begin() + (long) w + 1, 0.5f * (xs[w] + xs[w + 1])); ys.insert (ys.begin() + (long) w + 1, 0.5f * (ys[w] + ys[w + 1]));
                }
                for (size_t j = 0; j < 5; ++j) { kc.x[j] = xs[j] * 127.0f; kc.y[j] = ys[j] * 127.0f; }
                ++found; o = cb.end;
                // after the oscillators' curve: per osc 1..3, mod osc, inserts [u32 1][u32 0 off / 1 linear / 2 curve][u32 60]
                if (found == 1 && o + 60 <= raw.size())
                {
                    bool good = true; for (int t = 0; t < 5; ++t) good = good && u32 (raw, o + 12 * (size_t) t) == 1 && u32 (raw, o + 12 * (size_t) t + 4) <= 2;
                    if (good) { for (int t = 0; t < 5; ++t) q.keyTrackOsc[(size_t) t] = (int) u32 (raw, o + 12 * (size_t) t + 4); o += 60; }
                }
                continue;
            }
        }
        ++o;
    }
    if (found < 5) return;
    q.oscUserCurve = c[0]; q.filterCurveCut[0] = c[1]; q.filterCurveRes[0] = c[2]; q.filterCurveCut[1] = c[3]; q.filterCurveRes[1] = c[4];
    // the filters' sets
    for (size_t p = o; p + 40 < raw.size() && p < o + 64; ++p)
        if (u32 (raw, p) == 1 && u32 (raw, p + 12) == 0 && u32 (raw, p + 16) == 1 && u32 (raw, p + 28) == 0
            && (u32 (raw, p + 8) == 60 || u32 (raw, p + 8) == 0) && u32 (raw, p + 4) <= 3 && u32 (raw, p + 20) <= 3)
        {
            const uint32_t a = u32 (raw, p + 4), b = u32 (raw, p + 20);
            q.keyTrackFilter[0] = (a == 1 || a == 2) ? (int) a : 0;
            q.keyTrackFilter[1] = (b == 1 || b == 2) ? (int) b : 0;
            break;
        }
}

// raw bytes of a .nmsv file (or a plugin chunk) -> patch; false = not understood
inline int& switchMask() { static int m = 0x7fffffff; return m; }   // experiments (the command line tool)
inline bool importNmsv (const std::vector<uint8_t>& file, Patch& p, std::string* why = nullptr)
{
    using namespace nmsv;
    std::vector<uint8_t> raw;
    if (! payload (file, raw)) { if (why) *why = "no compressed payload found"; return false; }
    if (raw.size() < 0x300 + 137 * 4) { if (why) *why = "payload too small"; return false; }
    Patch q;                                         // defaults for anything not in the file
    if (auto n = soundName (file); ! n.empty()) q.name = n;
    const size_t base = (raw[0xc8] == 't' && raw[0xc9] == 's' && raw[0xca] == 'r' && raw[0xcb] == 'p') ? 0xd0 : 0xc0;
    for (int i = 0; i < kNumKnobs; ++i)
    {
        const float v = f32 (raw, base + 4 * (size_t) i);
        q.knob[(size_t) i] = (v == v && v >= -0.001f && v <= 1.001f) ? std::clamp (v, 0.0f, 1.0f) : q.knob[(size_t) i];
    }
    const auto recs = records (raw);
    std::map<int, const Rec*> byId;
    for (auto& r : recs) if (! byId.count (r.id)) byId[r.id] = &r;
    const auto knobs = knobSlots (raw);
    std::array<size_t, kNumKnobs> tagAt; tagAt.fill ((size_t) -1);
    for (auto& k : knobs) if (tagAt[(size_t) k.knob] == (size_t) -1) tagAt[(size_t) k.knob] = k.tagAt;
    // a knob's tag position: from the knob records (reliable), else from the record-header scan
    auto posOf = [&] (int id, size_t& pos) -> bool
    {
        if (id >= 0 && id < kNumKnobs && tagAt[(size_t) id] != (size_t) -1) { pos = tagAt[(size_t) id]; return true; }
        auto it = byId.find (id); if (it == byId.end()) return false; pos = it->second->at; return true;
    };
    auto at = [&] (int id, int off, bool& ok) -> int
    {
        size_t pos = 0; ok = posOf (id, pos) && (long) pos + off >= 0 && pos + (size_t) (off + 4) <= raw.size();
        return ok ? i32 (raw, (size_t) ((long) pos + off)) : 0;
    };
    bool ok;
    const int oscRec[3] = { 11, 16, 21 };
    for (int o = 0; o < 3; ++o)
    {
        const int m = at (oscRec[o], 5, ok); if (ok) q.oscMode[(size_t) o] = menuOf (kModeIds, 5, m, 0);
        const int w = at (oscRec[o], 9, ok); if (ok) q.wavetable[(size_t) o] = menuOf (kWavetableIds, 87, w, 2);
    }
    { const int t = at (21, 17, ok); if (ok) q.filterType[0] = menuOf (kFilterIds, 13, t, 0); }
    { const int t = at (35, 5, ok); if (ok) q.filterType[1] = menuOf (kFilterIds, 13, t, 0); }
    { const int t = at (38, 9, ok); if (ok) q.insertType[0] = menuOf (kInsertIds, 9, t, 0); }
    { const int t = at (41, 10, ok); if (ok) q.insertType[1] = menuOf (kInsertIds, 9, t, 0); }   // after insert 1's on byte
    if (size_t o = 0; posOf (28, o) && o + 30 < raw.size())
    {
        auto tgt = [&] (int k, int maxT) { const int v = i32 (raw, o + 9 + 4 * (size_t) k); return v >= 0 && v < maxT ? v + 1 : 0; };
        q.rmTarget = tgt (0, 3); q.pmTarget = tgt (1, 3); q.posTarget = tgt (2, 3); q.fmTarget = tgt (3, 2);
    }
    { const int t = at (26, 5, ok); if (ok) q.fbPos = menuOf (kFbIds, 5, t, 0); }
    { const int t = at (23, 5, ok); if (ok && t >= 0 && t < 12) q.noiseType = t; }
    { const int t = at (44, -0x28, ok); if (ok) q.fxType[0] = menuOf (kFxIds, 18, t, 0); }
    { const int t = at (48, -0x28, ok); if (ok) q.fxType[1] = menuOf (kFxIds, 18, t, 0); }
    if (size_t o = 0; posOf (56, o))
    {
        if (o + 0x31 < raw.size()) { q.bypassTarget = std::clamp ((int) raw[o + 0x2e], 0, 2); q.bypassOn = (switchMask() & 256) ? raw[o + 0x2f] == 0 : raw[o + 0x2f] != 0; q.bypassSource = std::clamp ((int) raw[o + 0x30], 0, 3); }
    }
    // macro names: 8 length-prefixed strings ending right before "<ver> 0 10.58"
    std::array<int, 2> keep65 { { -1, -1 } };
    {
        // (the float is 10.5833 - stored as 0x41295555 or ...56)
        size_t P = 0;
        for (size_t s = 0x300; s + 8 < raw.size(); ++s)
            if (u32 (raw, s) == 0 && raw[s + 6] == 0x29 && raw[s + 7] == 0x41 && std::abs (f32 (raw, s + 4) - 10.5833f) < 0.001f) { P = s - 4; break; }
        if (P > 300)
            for (size_t c = P - 8 * 30; c + 31 < P; ++c)
            {
                size_t o = c; bool good = true; std::array<std::string, 8> nm;
                for (int k = 0; k < 8 && good; ++k)
                {
                    const uint32_t n = u32 (raw, o); if (n > 30) { good = false; break; }
                    // 8 bit text (some names carry umlauts): kept as UTF-8
                    std::string s;
                    for (uint32_t i = 0; i < n && good; ++i)
                    {
                        const uint8_t ch = raw[o + 4 + i];
                        if (ch < 32 || ch == 127) good = false;
                        else if (ch < 128) s += (char) ch;
                        else { s += (char) (0xc0 | (ch >> 6)); s += (char) (0x80 | (ch & 63)); }
                    }
                    nm[(size_t) k] = s; o += 4 + n;
                }
                if (good && o == P)
                {
                    q.macroName = nm;
                    if (c >= 0x22)
                    {
                        q.unison = std::clamp ((int) raw[c - 0x22], 1, 16);
                        q.voiceMode = std::clamp ((int) raw[c - 0x1e], 0, 2);
                        q.triggerMode = std::clamp ((int) raw[c - 0x1a], 0, 2);
                        q.uniPitchOn = raw[c - 0x16] != 0; q.uniWtOn = raw[c - 0x14] != 0; q.uniPanOn = raw[c - 0x0b] != 0;
                        q.uniChordMode = raw[c - 0x15] == 0;          // the voicing page's "Chord" switch (0 = on)
                        // the OSC page's switches: glide "Equal" (1) or "Rate" (0), "Restart via Gate"
                        q.glideRateMode = raw[c - 6] == 0;
                        q.restartPhase = raw[c - 5] != 0;
                        keep65[0] = 1;
                        // the pitch bend range: semitones down (negative) and up
                        const float bd = f32 (raw, c - 0x13), bu = f32 (raw, c - 0x0f);
                        if (std::isfinite (bd) && std::isfinite (bu) && std::abs (bd) <= 48.0f && std::abs (bu) <= 48.0f) { q.bendDown = bd; q.bendUp = bu; }
                    }
                    break;
                }
            }
    }
    // modulation: every knob's slots, in their places (the third slot of a knob is its side chain slot)
    for (auto& m : q.mod) m = {};
    q.ampMod = {};
    bool ampSet = false;
    {
        // the switches: single bytes at fixed places after a knob's tag (found by flipping each switch in the
        // plugin and comparing its state). Except for the EQ and the "... on" options, 1 means off.
        auto byteAt = [&] (int tag, long d, int& out)
        {
            if (tagAt[(size_t) tag] == (size_t) -1) return false;
            const long x = (long) tagAt[(size_t) tag] + d;
            if (x < 0 || x >= (long) raw.size()) return false;
            out = raw[(size_t) x]; return true;
        };
        int b = 0; const int sw = switchMask();
        if (sw & 1) for (int i = 0; i < 3; ++i) if (byteAt (23, 9 + i, b)) q.oscOn[(size_t) i] = b == 0;
        if ((sw & 2) && byteAt (23, 12, b)) q.noiseOn = b == 0;
        if ((sw & 4) && byteAt (23, 17, b)) q.eqOn = b == 1;
        if ((sw & 8) && byteAt (23, -82, b)) q.fbOn = b == 0;
        if ((sw & 16) && byteAt (41, 5, b)) q.insertOn[0] = b == 0;
        if ((sw & 16) && byteAt (43, 5, b)) q.insertOn[1] = b == 0;
        // the inserts' places (the routing page): after insert 2's on byte, 7 slots (pre F1, pre F2, post F1, serial,
        // post F2, feedback loop, pre amp) holding the insert there (0, 1) or -1
        if (tagAt[43] != (size_t) -1 && tagAt[43] + 34 <= raw.size())
        {
            int pos[2] = { ipOff, ipOff }; bool ok = true;
            for (int i = 0; i < 7; ++i)
            {
                const int32_t v = i32 (raw, tagAt[43] + 6 + 4 * (size_t) i);
                if (v == 0 || v == 1) pos[v] = ipPreF1 + i; else if (v != -1) ok = false;
            }
            if (ok) { q.insertPos[0] = pos[0]; q.insertPos[1] = pos[1]; }
        }
        if ((sw & 32) && byteAt (56, 53, b)) q.modOscOn = b == 0;
        if ((sw & 64) && byteAt (51, 30, b)) q.filterOn[0] = b == 0;
        if ((sw & 64) && byteAt (51, 31, b)) q.filterOn[1] = b == 0;
        if (byteAt (128, 5, b)) q.vibMono = b == 1;
        const int fxTag[2] = { 47, 51 };
        for (int f = 0; f < 2; ++f)
        {
            if ((sw & 128) && byteAt (fxTag[f], 5, b)) q.fxOn[(size_t) f] = b == 0;
            int ln = 0, ld = 0, rn = 0, rd = 0;     // the synced delay: numerators 1..32, denominators as a power of two
            if (byteAt (fxTag[f], 6, ln) && byteAt (fxTag[f], 10, ld) && byteAt (fxTag[f], 14, rn) && byteAt (fxTag[f], 18, rd)
                && ln >= 1 && ln <= 32 && rn >= 1 && rn <= 32 && ld <= 7 && rd <= 7)
                q.fxSync[(size_t) f] = { { ln, 1 << ld, rn, 1 << rd } };
        }
    }
    {
        // a slot is 10 bytes: f32 amount, i32 source, u8 muted, u8 side chain flag. The last slot of a knob with its
        // flag set is the side chain: it scales the slots that have the flag set too (by its source's value as it
        // is, a bipolar one too; its amount does nothing). Found on the filter FM knob of Hollow Coppice (velocity,
        // side chain macro 2 at 0: the original ignores the velocity there) and checked against the original with
        // test sounds (macro 0 / 0.5, key tracking, an LFO). A muted slot does nothing whatever its source (46 in
        // the factory sounds; tested with a macro at 0.5 and 1).
        std::array<bool, kNumKnobs> seen {};
        for (auto& k : knobs)
        {
            if (seen[(size_t) k.knob]) continue;
            seen[(size_t) k.knob] = true;
            const size_t n = k.slots.size(), first = k.tagAt - 10 * n;
            auto flag = [&] (size_t i) { return first + 10 * i + 9 < raw.size() && raw[first + 10 * i + 9] != 0; };
            auto muted = [&] (size_t i) { return first + 10 * i + 8 < raw.size() && raw[first + 10 * i + 8] != 0; };
            auto& km = q.mod[(size_t) k.knob];
            const bool sc = n >= 2 && mapSource (k.slots[n - 1].second) != msNone && flag (n - 1) && ! muted (n - 1);
            for (size_t i = 0; i < n && i < 3; ++i)
            {
                if (muted (i)) continue;
                const int src = mapSource (k.slots[i].second);
                if (sc && i == n - 1) { km.slot[2] = { src, k.slots[i].first }; km.sideChain = true; continue; }
                if (src != msNone && std::abs (k.slots[i].first) > 1.0e-4f) km.slot[i] = { src, k.slots[i].first };
            }
            if (sc) for (size_t i = 0; i < 2; ++i) km.scTarget[i] = i < n - 1 && flag (i);
        }
    }
    // the unison pitch spread's range (its record carries it, in octaves)
    for (auto& k : knobs)
        if (k.knob == kUniPitch && k.hi > k.lo && k.hi - k.lo < 8.0f) { q.uniRangeDown = k.lo * 12.0f; q.uniRangeUp = k.hi * 12.0f; break; }
    importKeyTracking (raw, q);
    // the kept bytes: the 2 before the first curve (only with the macro block found too)
    if (q.nmsvKeep.size() != 2 || keep65[0] < 0) q.nmsvKeep.clear();
    // the OSC page's oscillator phases (osc 1..3, modulation osc; 0..1 = 0..360 degrees), used when "Restart via
    // Gate" is on: four floats 6 bytes after the glide knob's tag
    if (tagAt[126] != (size_t) -1 && tagAt[126] + 22 <= raw.size())
        for (size_t i = 0; i < 4; ++i)
        {
            const float v = f32 (raw, tagAt[126] + 6 + 4 * i);
            if (std::isfinite (v)) q.oscPhase[i] = std::clamp (v, 0.0f, 1.0f);
        }
    importModPages (raw, tagAt, q);
    if (const float bpm = presetBpmAt (raw); bpm > 0.0f) q.presetBpm = bpm;
    // two knobs have no tag of their own: what follows tag 54 is the bypass gain's slots (left alone: the
    // bypass follows the amp envelope here already), what follows tag 56 is the amp's
    if (byId.count (56))
    {
        int k = 0;
        for (auto& [a, sIdx] : byId[56]->slots) { const int src = mapSource (sIdx); if (src != msNone && k < 2) { q.ampMod[0].slot[(size_t) k++] = { src, a == 0.0f ? 1.0f : a }; ampSet = true; } }
    }
    if (! ampSet) q.ampMod[0].slot[0] = { msEnv4, 1.0f };
    p = q;
    return true;
}
} // namespace massif
