#pragma once
// Massif - writing a patch back into the original's .nmsv format.
//
// The file is built on a template: an .nmsv sound the user has (usually the last one imported). Its
// container and every part Massif does not model stay as they are; what Massif knows - the 137 knobs,
// the selectors and switches, the modulation slots, the modulation pages, the envelope switches, the
// voicing, keytracking and macro names - is written over it, at the places PresetImport.h reads them
// from. The payload is stored uncompressed (literal LZF runs), which the original reads like any other.
#include "PresetImport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace massif
{
namespace nmsvout
{
    using namespace nmsv;
    inline void put32 (std::vector<uint8_t>& b, size_t o, uint32_t v) { if (o + 4 <= b.size()) std::memcpy (b.data() + o, &v, 4); }
    inline void putI (std::vector<uint8_t>& b, size_t o, int32_t v) { put32 (b, o, (uint32_t) v); }
    inline void putF (std::vector<uint8_t>& b, size_t o, float v) { if (o + 4 <= b.size()) std::memcpy (b.data() + o, &v, 4); }
    inline void put8 (std::vector<uint8_t>& b, size_t o, uint8_t v) { if (o < b.size()) b[o] = v; }

    // Massif's source -> the stored source number
    inline int storedSource (int s)
    {
        if (s >= msEnv1 && s <= msMod8) return s;
        if (s >= msMacro1 && s <= msMacro8) return 13 + (s - msMacro1);
        if (s == msKeyTrack) return 21;
        if (s == msVelocity) return 22;
        if (s == msAftertouch) return 23;
        if (s == msTrigRandom) return 24;
        if (s >= msIntEnv5 && s <= msIntEnv8) return 8 + (s - msIntEnv5);
        if (s == msOscEnv) return 12;
        return -1;
    }

    // payload position in the file: offset of the "01 <raw> <packed>" header
    inline bool locate (const std::vector<uint8_t>& f, size_t& at, std::vector<uint8_t>& raw)
    {
        for (size_t p = 0; p + 9 < f.size(); ++p)
        {
            if (f[p] != 1) continue;
            const uint32_t rawLen = u32 (f, p + 1), cl = u32 (f, p + 5);
            if (rawLen > 100 && rawLen < 10000000 && cl > 10 && cl <= f.size() - p - 9 && cl <= rawLen + rawLen / 31 + 64 && p + 9 + cl + 40 >= f.size())
            {
                raw = lzf (f.data() + p + 9, cl, rawLen);
                if (raw.size() == rawLen) { at = p; return true; }
            }
        }
        return false;
    }

    inline std::vector<uint8_t> literalLzf (const std::vector<uint8_t>& d)
    {
        std::vector<uint8_t> o; o.reserve (d.size() + d.size() / 32 + 2);
        for (size_t i = 0; i < d.size(); i += 32)
        {
            const size_t n = std::min ((size_t) 32, d.size() - i);
            o.push_back ((uint8_t) (n - 1)); o.insert (o.end(), d.begin() + (long) i, d.begin() + (long) (i + n));
        }
        return o;
    }

    inline void splice (std::vector<uint8_t>& b, size_t from, size_t to, const std::vector<uint8_t>& with)
    {
        b.erase (b.begin() + (long) from, b.begin() + (long) to);
        b.insert (b.begin() + (long) from, with.begin(), with.end());
    }
    inline void app32 (std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) (x >> (8 * i))); }
    inline void appF (std::vector<uint8_t>& v, float x) { uint32_t u; std::memcpy (&u, &x, 4); app32 (v, u); }

    // the macro-name block: [start, end) and the anchor c (= start)
    inline bool macroBlock (const std::vector<uint8_t>& raw, size_t& start, size_t& end)
    {
        size_t P = 0;      // before "<u32 ver> 0 10.5833" (0x41295555 or ...56)
        for (size_t s = 0x300; s + 8 < raw.size(); ++s)
            if (u32 (raw, s) == 0 && raw[s + 6] == 0x29 && raw[s + 7] == 0x41 && std::abs (f32 (raw, s + 4) - 10.5833f) < 0.001f) { P = s - 4; break; }
        if (P <= 300) return false;
        for (size_t c = P - 8 * 30; c + 31 < P; ++c)
        {
            size_t o = c; bool good = true;
            for (int k = 0; k < 8 && good; ++k)
            {
                const uint32_t n = u32 (raw, o); if (n > 30) { good = false; break; }
                for (uint32_t i = 0; i < n; ++i) if (raw[o + 4 + i] < 32 || raw[o + 4 + i] == 127) good = false;
                o += 4 + n;
            }
            if (good && o == P) { start = c; end = P; return true; }
        }
        return false;
    }

    // the keytracking curve blocks (as the import finds them), in the form the template has them
    inline std::vector<CurveBlock> curveBlocks (const std::vector<uint8_t>& raw, std::vector<size_t>* at = nullptr)
    {
        std::vector<CurveBlock> out; size_t o = 0x100; const size_t end = std::min (raw.size(), (size_t) 0x1000);
        while (out.size() < 5 && o + 17 < end)
        {
            CurveBlock cb;
            if (curveAt (raw, o, cb))
            {
                out.push_back (cb); if (at) at->push_back (o);
                o = cb.end;
                continue;
            }
            ++o;
        }
        return out;
    }
    inline std::vector<uint8_t> curveBytes (const KeyCurve& c, int version)
    {
        std::vector<uint8_t> v; app32 (v, (uint32_t) version); app32 (v, 128); app32 (v, 128);
        if (version == 2) v.push_back (5);
        appF (v, std::clamp (c.y[0] / 127.0f, 0.0f, 1.0f));
        for (int i = 1; i < 4; ++i) { appF (v, std::clamp (c.x[(size_t) i] / 127.0f, 0.0f, 1.0f)); appF (v, std::clamp (c.y[(size_t) i] / 127.0f, 0.0f, 1.0f)); }
        appF (v, std::clamp (c.y[4] / 127.0f, 0.0f, 1.0f));
        return v;
    }

    inline std::vector<uint8_t> pagesBytes (const Patch& q, const std::vector<uint8_t>& lead)
    {
        std::vector<uint8_t> v (lead);
        for (int m = 0; m < 4; ++m)
        {
            const int type = q.modType[(size_t) m];
            if (type == mtPerformer)
            {
                const auto& pf = q.performer[(size_t) m];
                app32 (v, 1); app32 (v, 1);
                for (size_t k = 0; k < 16; ++k)
                {
                    appF (v, pf.levelA[k]); app32 (v, (uint32_t) std::clamp (pf.curveA[k], 0, pcNum - 1));
                    appF (v, pf.levelB[k]); app32 (v, (uint32_t) std::clamp (pf.curveB[k], 0, pcNum - 1));
                    v.push_back (pf.morphOn[k] ? 1 : 0);
                }
                app32 (v, 2);
                for (size_t k = 0; k < 16; ++k) { v.push_back (pf.ampOn[k] ? 1 : 0); v.push_back (1); v.push_back (0); }
                app32 (v, (uint32_t) std::clamp (pf.startStep, 0, 15));
            }
            else if (type == mtStepper)
            {
                const auto& s = q.stepper[(size_t) m];
                app32 (v, 2); app32 (v, 2);
                for (size_t k = 0; k < 16; ++k) appF (v, std::clamp (s.step[k], 0.0f, 1.0f));
                for (size_t k = 0; k < 16; ++k) v.push_back (s.glideOn[k] ? 1 : 0);
                app32 (v, 2);
                for (size_t k = 0; k < 16; ++k) { v.push_back (s.ampOn[k] ? 1 : 0); v.push_back (1); v.push_back (0); }
                app32 (v, (uint32_t) std::clamp (s.startStep, 0, 15));
            }
            else
            {
                const auto& l = q.lfo[(size_t) m];
                app32 (v, 0); app32 (v, 2); appF (v, l.phaseA); appF (v, l.phaseB);
                app32 (v, (uint32_t) std::clamp (l.shapeA, 0, csNum - 1)); app32 (v, (uint32_t) std::clamp (l.shapeB, 0, csNum - 1));
            }
        }
        app32 (v, 3);
        return v;
    }
}

// patch + template .nmsv -> a new .nmsv; false (and why) when the template is not understood. Only what
// differs from the template (as Massif reads it) is written, so the template's own details stay where the
// patch agrees with them, and a sound exported over its own file comes back byte for byte.
inline bool exportNmsv (const Patch& q, const std::vector<uint8_t>& templ, std::vector<uint8_t>& out, std::string* why = nullptr)
{
    using namespace nmsvout;
    size_t pAt = 0; std::vector<uint8_t> raw;
    if (! locate (templ, pAt, raw)) { if (why) *why = "the template has no sound data"; return false; }
    Patch tq; if (! importNmsv (templ, tq, why)) return false;
    const std::vector<uint8_t> raw0 = raw;
    const uint32_t oldCl = u32 (templ, pAt + 5);
    const size_t base = (raw[0xc8] == 't' && raw[0xc9] == 's' && raw[0xca] == 'r' && raw[0xcb] == 'p') ? 0xd0 : 0xc0;
    if (raw.size() < base + 4 * kNumKnobs) { if (why) *why = "the template is too small"; return false; }
    auto sameSlot = [] (const ModSlot& a, const ModSlot& b) { return a.source == b.source && (a.source < 0 || a.amount == b.amount); };
    auto sameCurve = [] (const KeyCurve& a, const KeyCurve& b) { return a.x == b.x && a.y == b.y; };

    // ---- fixed-size edits
    for (int i = 0; i < kNumKnobs; ++i)
        if (q.knob[(size_t) i] != tq.knob[(size_t) i]) putF (raw, base + 4 * (size_t) i, std::clamp (q.knob[(size_t) i], 0.0f, 1.0f));
    const auto knobs = knobSlots (raw);
    std::array<size_t, kNumKnobs> tagAt; tagAt.fill ((size_t) -1);
    for (auto& k : knobs) if (tagAt[(size_t) k.knob] == (size_t) -1) tagAt[(size_t) k.knob] = k.tagAt;
    const auto recs = records (raw);
    auto posOf = [&] (int id, size_t& pos) -> bool
    {
        if (id >= 0 && id < kNumKnobs && tagAt[(size_t) id] != (size_t) -1) { pos = tagAt[(size_t) id]; return true; }
        for (auto& r : recs) if (r.id == id) { pos = r.at; return true; }
        return false;
    };
    auto setI = [&] (int id, long off, int32_t v) { size_t p; if (posOf (id, p) && (long) p + off >= 0) putI (raw, (size_t) ((long) p + off), v); };
    // single-byte switches sit after a knob's own tag (as the import reads them)
    auto setB = [&] (int tag, long off, uint8_t v) { const size_t p = tagAt[(size_t) tag]; if (p != (size_t) -1 && (long) p + off >= 0) put8 (raw, (size_t) ((long) p + off), v); };
    auto getB = [&] (int tag, long off) -> int { const size_t p = tagAt[(size_t) tag]; return p != (size_t) -1 && (long) p + off >= 0 && (size_t) ((long) p + off) < raw.size() ? raw[(size_t) ((long) p + off)] : -1; };
    auto idOf = [] (const int* table, int n, int menu, int fallback) { return menu >= 0 && menu < n ? table[menu] : fallback; };

    const int oscRec[3] = { 11, 16, 21 };
    for (size_t o = 0; o < 3; ++o)
    {
        if (q.oscMode[o] != tq.oscMode[o]) setI (oscRec[o], 5, idOf (kModeIds, 5, q.oscMode[o], 5));
        if (q.wavetable[o] != tq.wavetable[o]) setI (oscRec[o], 9, idOf (kWavetableIds, 87, q.wavetable[o], 0));
    }
    if (q.filterType[0] != tq.filterType[0]) setI (21, 17, idOf (kFilterIds, 13, q.filterType[0], 11));
    if (q.filterType[1] != tq.filterType[1]) setI (35, 5, idOf (kFilterIds, 13, q.filterType[1], 11));
    if (q.insertType[0] != tq.insertType[0]) setI (38, 9, idOf (kInsertIds, 9, q.insertType[0], 0));
    if (q.insertType[1] != tq.insertType[1]) setI (41, 10, idOf (kInsertIds, 9, q.insertType[1], 0));
    {
        const int t[4] = { q.rmTarget, q.pmTarget, q.posTarget, q.fmTarget }, tt[4] = { tq.rmTarget, tq.pmTarget, tq.posTarget, tq.fmTarget };
        for (int k = 0; k < 4; ++k) if (t[k] != tt[k]) setI (28, 9 + 4 * k, t[k] > 0 ? t[k] - 1 : -1);
    }
    if (q.fbPos != tq.fbPos) setI (26, 5, idOf (kFbIds, 5, q.fbPos, -1));
    if (q.noiseType != tq.noiseType) setI (23, 5, std::clamp (q.noiseType, 0, 11));
    if (q.fxType[0] != tq.fxType[0]) setI (44, -0x28, idOf (kFxIds, 18, q.fxType[0], 17));
    if (q.fxType[1] != tq.fxType[1]) setI (48, -0x28, idOf (kFxIds, 18, q.fxType[1], 17));
    if (size_t o; posOf (56, o))
    {
        if (q.bypassTarget != tq.bypassTarget) put8 (raw, o + 0x2e, (uint8_t) std::clamp (q.bypassTarget, 0, 2));
        if (q.bypassOn != tq.bypassOn) put8 (raw, o + 0x2f, q.bypassOn ? 0 : 1);
        if (q.bypassSource != tq.bypassSource) put8 (raw, o + 0x30, (uint8_t) std::clamp (q.bypassSource, 0, 3));
    }
    // switches (1 = off, except the EQ and the "... on" ones)
    auto sw = [&] (bool now, bool was, int tag, long off, uint8_t v) { if (now != was) setB (tag, off, v); };
    for (size_t i = 0; i < 3; ++i) sw (q.oscOn[i], tq.oscOn[i], 23, 9 + (long) i, q.oscOn[i] ? 0 : 1);
    sw (q.noiseOn, tq.noiseOn, 23, 12, q.noiseOn ? 0 : 1); sw (q.eqOn, tq.eqOn, 23, 17, q.eqOn ? 1 : 0); sw (q.fbOn, tq.fbOn, 23, -82, q.fbOn ? 0 : 1);
    sw (q.insertOn[0], tq.insertOn[0], 41, 5, q.insertOn[0] ? 0 : 1); sw (q.insertOn[1], tq.insertOn[1], 43, 5, q.insertOn[1] ? 0 : 1);
    sw (q.modOscOn, tq.modOscOn, 56, 53, q.modOscOn ? 0 : 1);
    sw (q.filterOn[0], tq.filterOn[0], 51, 30, q.filterOn[0] ? 0 : 1); sw (q.filterOn[1], tq.filterOn[1], 51, 31, q.filterOn[1] ? 0 : 1);
    sw (q.vibMono, tq.vibMono, 128, 5, q.vibMono ? 1 : 0);
    const int fxTag[2] = { 47, 51 };
    for (size_t f = 0; f < 2; ++f)
    {
        sw (q.fxOn[f], tq.fxOn[f], fxTag[f], 5, q.fxOn[f] ? 0 : 1);
        if (q.fxSync[f] == tq.fxSync[f]) continue;
        const auto& s = q.fxSync[f];
        auto lg = [] (int d) { int e = 0; while ((1 << e) < d && e < 7) ++e; return e; };
        const int ln = getB (fxTag[f], 6), ld = getB (fxTag[f], 10), rn = getB (fxTag[f], 14), rd = getB (fxTag[f], 18);
        if (ln >= 1 && ln <= 32 && rn >= 1 && rn <= 32 && ld >= 0 && ld <= 7 && rd >= 0 && rd <= 7)   // the template has them there
        {
            setB (fxTag[f], 6, (uint8_t) std::clamp (s[0], 1, 32)); setB (fxTag[f], 10, (uint8_t) lg (s[1]));
            setB (fxTag[f], 14, (uint8_t) std::clamp (s[2], 1, 32)); setB (fxTag[f], 18, (uint8_t) lg (s[3]));
        }
    }
    if (tagAt[43] != (size_t) -1 && q.insertPos != tq.insertPos)
        for (int i = 0; i < 7; ++i)
        {
            int32_t v = -1;
            for (int n = 0; n < 2; ++n) if (q.insertPos[(size_t) n] == ipPreF1 + i) v = n;
            putI (raw, tagAt[43] + 6 + 4 * (size_t) i, v);
        }
    // modulation slots: into the template's slots of each knob (as many as it has)
    {
        std::array<bool, kNumKnobs> seen {};
        for (auto& k : knobs)
        {
            if (seen[(size_t) k.knob]) continue;
            seen[(size_t) k.knob] = true;
            const size_t n = k.slots.size();
            const size_t first = k.tagAt - 10 * n;
            // a side chain goes into the knob's last slot, with the flags (its own and its targets')
            auto layout = [n] (const KnobMod& m, std::array<ModSlot, 3>& sl, std::array<int, 3>& fl)
            {
                sl = {}; fl = { { -1, -1, -1 } };
                const bool sc = m.sideChain && m.slot[2].source >= 0 && n >= 2;
                for (size_t i = 0; i < n && i < 3; ++i) sl[i] = m.slot[i];
                if (sc)
                {
                    if (n == 2) sl[1] = m.slot[2];
                    for (size_t i = 0; i + 1 < n && i < 2; ++i) fl[i] = m.scTarget[i] ? 1 : 0;
                    fl[std::min<size_t> (n, 3) - 1] = 1;
                }
                else if (m.sideChain || std::any_of (m.slot.begin(), m.slot.end(), [] (const ModSlot& x) { return x.source >= 0; }))
                    fl[std::min<size_t> (n, 3) - 1] = 0;
            };
            std::array<ModSlot, 3> sl, was; std::array<int, 3> fl, wfl;
            layout (q.mod[(size_t) k.knob], sl, fl); layout (tq.mod[(size_t) k.knob], was, wfl);
            for (size_t i = 0; i < n && i < 3; ++i)
            {
                if (! sameSlot (sl[i], was[i]))
                {
                    const int src = sl[i].source >= 0 ? storedSource (sl[i].source) : -1;
                    putF (raw, first + 10 * i, src >= 0 ? sl[i].amount : 0.0f); putI (raw, first + 10 * i + 4, src);
                    raw[first + 10 * i + 8] = 0;      // (a slot the template had muted plays again)
                }
                if (fl[i] != wfl[i] && fl[i] >= 0) raw[first + 10 * i + 9] = (uint8_t) fl[i];
            }
        }
        // the amp's slots follow tag 56 (found by the record scan, as the import reads them)
        for (auto& r : recs)
            if (r.id == 56 && ! r.slots.empty())
            {
                const size_t first = r.at + 9 + (size_t) r.extra + 4;
                for (size_t i = 0; i < r.slots.size() && i < 2; ++i)
                {
                    const auto& sl = q.ampMod[0].slot[i];
                    if (sameSlot (sl, tq.ampMod[0].slot[i])) continue;
                    const int src = sl.source >= 0 ? storedSource (sl.source) : (i == 0 ? 3 : -1);
                    putF (raw, first + 10 * i, sl.source >= 0 ? sl.amount : (i == 0 ? 1.0f : 0.0f)); putI (raw, first + 10 * i + 4, src);
                }
                break;
            }
    }
    // envelope switches
    for (size_t e = 0; e < 4; ++e)
    {
        const size_t te = tagAt[kEnv1RelT + e]; if (te == (size_t) -1) continue;
        const auto& x = q.env[e]; const auto& w = tq.env[e];
        if (x.morphShapeA == w.morphShapeA && x.morphShapeB == w.morphShapeB && x.loopCount == w.loopCount && x.retrigger == w.retrigger
            && x.linearDecay == w.linearDecay && x.playMode == w.playMode) continue;
        put32 (raw, te + 5, (uint32_t) std::clamp (x.morphShapeA, 0, esNum - 1)); put32 (raw, te + 9, (uint32_t) std::clamp (x.morphShapeB, 0, esNum - 1));
        put32 (raw, te + 13, (uint32_t) std::clamp (x.loopCount, 0, 33));
        put8 (raw, te + 17, x.retrigger ? 1 : 0); put8 (raw, te + 18, x.linearDecay ? 1 : 0); put8 (raw, te + 19, (uint8_t) std::clamp (x.playMode, 0, 2));
    }
    // the oscillator phases after the glide knob's tag
    if (tagAt[126] != (size_t) -1 && q.oscPhase != tq.oscPhase)
        for (size_t i = 0; i < 4; ++i) putF (raw, tagAt[126] + 6 + 4 * i, std::clamp (q.oscPhase[i], 0.0f, 1.0f));
    // mod page loops, mono, sync ratios, internal envelopes
    int rateGap = 0;
    if (tagAt[61] != (size_t) -1)
        for (int d : { 11, 10 })
        {
            const size_t o = tagAt[61] + 5 + (size_t) d; bool ok = o + 60 <= raw.size();
            for (int k = 0; k < 5 && ok; ++k) ok = u32 (raw, o + 12 * (size_t) k) == 2;
            if (ok)
            {
                rateGap = d;
                for (size_t k = 0; k < 5; ++k)
                    if (q.intAttack[k] != tq.intAttack[k] || q.intDecay[k] != tq.intDecay[k])
                    { putF (raw, o + 12 * k + 4, std::clamp (q.intAttack[k], 0.0f, 1.0f)); putF (raw, o + 12 * k + 8, std::clamp (q.intDecay[k], 0.0f, 1.0f)); }
                break;
            }
        }
    for (size_t m = 0; m < 4; ++m)
    {
        const size_t tl = tagAt[70 + m];
        const bool isLfo = q.modType[m] == mtLFO, isStep = q.modType[m] == mtStepper;
        const int ls = isStep ? q.stepper[m].loopStart : q.performer[m].loopStart, le = isStep ? q.stepper[m].loopEnd : q.performer[m].loopEnd;
        const bool mono = isLfo ? q.lfo[m].mono : isStep ? q.stepper[m].mono : q.performer[m].mono;
        if (tl != (size_t) -1)
        {
            // the template's (one value for all page types)
            if (ls != tq.stepper[m].loopStart || le != tq.stepper[m].loopEnd)
            { put32 (raw, tl + 5, (uint32_t) std::clamp (ls, 0, 15)); put32 (raw, tl + 9, (uint32_t) std::clamp (std::max (ls, le), 0, 15)); }
            if (mono != tq.lfo[m].mono) put8 (raw, tl + 13, mono ? 1 : 0);
        }
        const size_t tr = tagAt[58 + m];
        if (tr != (size_t) -1 && rateGap != 0)
        {
            const size_t nOff = rateGap == 11 ? 8 : 7;
            if (q.syncNum[m] != tq.syncNum[m] || q.syncDen[m] != tq.syncDen[m])
            {
                int den = 0; while ((1 << den) < q.syncDen[m] && den < 8) ++den;
                put32 (raw, tr + nOff, (uint32_t) std::clamp (q.syncNum[m], 1, 64)); put32 (raw, tr + nOff + 4, (uint32_t) den);
            }
            if (q.knob[kMod5Sync + m] != tq.knob[kMod5Sync + m]) put8 (raw, tr + 5, q.knob[kMod5Sync + m] > 0.5f ? 1 : 0);
            // the restart switch has its byte here too (the original goes by it): [sync][pos][restart] in the newer
            // layout, [sync][restart] in the older
            if (q.knob[kMod5Restart + m] != tq.knob[kMod5Restart + m]) put8 (raw, tr + nOff - 1, q.knob[kMod5Restart + m] > 0.5f ? 1 : 0);
        }
    }
    // voicing (bytes before the macro names) and the unison spread's range
    size_t mStart = 0, mEnd = 0;
    const bool haveMacros = macroBlock (raw, mStart, mEnd);
    if (haveMacros && mStart >= 0x22)
    {
        const size_t c = mStart;
        if (q.unison != tq.unison) put8 (raw, c - 0x22, (uint8_t) std::clamp (q.unison, 1, 16));
        if (q.voiceMode != tq.voiceMode) put8 (raw, c - 0x1e, (uint8_t) std::clamp (q.voiceMode, 0, 2));
        if (q.triggerMode != tq.triggerMode) put8 (raw, c - 0x1a, (uint8_t) std::clamp (q.triggerMode, 0, 2));
        if (q.uniPitchOn != tq.uniPitchOn) put8 (raw, c - 0x16, q.uniPitchOn ? 1 : 0);
        if (q.uniChordMode != tq.uniChordMode) put8 (raw, c - 0x15, q.uniChordMode ? 0 : 1);
        if (q.uniWtOn != tq.uniWtOn) put8 (raw, c - 0x14, q.uniWtOn ? 1 : 0);
        if (q.uniPanOn != tq.uniPanOn) put8 (raw, c - 0x0b, q.uniPanOn ? 1 : 0);
        if (q.glideRateMode != tq.glideRateMode) put8 (raw, c - 6, q.glideRateMode ? 0 : 1);
        if (q.restartPhase != tq.restartPhase) put8 (raw, c - 5, q.restartPhase ? 1 : 0);
    if (q.bendDown != tq.bendDown || q.bendUp != tq.bendUp)
        { putF (raw, c - 0x13, std::clamp (q.bendDown, -48.0f, 48.0f)); putF (raw, c - 0x0f, std::clamp (q.bendUp, -48.0f, 48.0f)); }
    }
    if (q.uniRangeDown != tq.uniRangeDown || q.uniRangeUp != tq.uniRangeUp)
        for (auto& k : knobs)
            if (k.knob == kUniPitch)
            {
                // [ver][lo][hi - lo][lo][hi][n] ... the record ends at its tag
                const size_t first = k.tagAt - 10 * k.slots.size(), rec = first - 24;
                if (rec + 20 < raw.size() && f32 (raw, rec + 4) == f32 (raw, rec + 12))
                {
                    const float lo = q.uniRangeDown / 12.0f, hi = std::max (q.uniRangeUp, q.uniRangeDown) / 12.0f;
                    putF (raw, rec + 4, lo); putF (raw, rec + 8, hi - lo); putF (raw, rec + 12, lo); putF (raw, rec + 16, hi);
                }
                break;
            }
    // keytracking: the oscillators' modes and the filters' sets (fixed size)
    std::vector<size_t> blocks;
    const auto cbs = curveBlocks (raw, &blocks);
    if (blocks.size() == 5 && q.nmsvKeep.size() == 2 && q.nmsvKeep != tq.nmsvKeep && blocks[0] >= 12)
    { raw[blocks[0] - 12] = q.nmsvKeep[0]; raw[blocks[0] - 11] = q.nmsvKeep[1]; }
    if (blocks.size() == 5)
    {
        size_t o = cbs[0].end;
        bool good = true; for (int t = 0; t < 5; ++t) good = good && u32 (raw, o + 12 * (size_t) t) == 1;
        if (good && q.keyTrackOsc != tq.keyTrackOsc) for (int t = 0; t < 5; ++t) put32 (raw, o + 12 * (size_t) t + 4, (uint32_t) std::clamp (q.keyTrackOsc[(size_t) t], 0, 2));
        const size_t e5 = cbs[4].end;
        if (q.keyTrackFilter != tq.keyTrackFilter)
            for (size_t p = e5; p + 40 < raw.size() && p < e5 + 64; ++p)
                if (u32 (raw, p) == 1 && u32 (raw, p + 12) == 0 && u32 (raw, p + 16) == 1 && u32 (raw, p + 28) == 0
                    && (u32 (raw, p + 8) == 60 || u32 (raw, p + 8) == 0) && u32 (raw, p + 4) <= 3 && u32 (raw, p + 20) <= 3)
                {
                    // 0 off, 1 / 2 the set (as every factory sound stores it)
                    put32 (raw, p + 4, (uint32_t) std::clamp (q.keyTrackFilter[0], 0, 2));
                    put32 (raw, p + 20, (uint32_t) std::clamp (q.keyTrackFilter[1], 0, 2));
                    break;
                }
    }

    // ---- variable-size parts: collected, then applied from the highest offset down
    struct Splice { size_t from, to; std::vector<uint8_t> with; };
    std::vector<Splice> sp;
    bool pagesSame = true;
    for (size_t m = 0; m < 4; ++m)
    {
        if (q.modType[m] != tq.modType[m]) { pagesSame = false; break; }
        const auto& a = q.lfo[m]; const auto& b = tq.lfo[m];
        const auto& s1 = q.stepper[m]; const auto& s2 = tq.stepper[m];
        const auto& p1 = q.performer[m]; const auto& p2 = tq.performer[m];
        if (q.modType[m] == mtLFO) pagesSame = pagesSame && a.shapeA == b.shapeA && a.shapeB == b.shapeB && a.phaseA == b.phaseA && a.phaseB == b.phaseB;
        else if (q.modType[m] == mtStepper) pagesSame = pagesSame && s1.step == s2.step && s1.glideOn == s2.glideOn && s1.ampOn == s2.ampOn && s1.startStep == s2.startStep;
        else pagesSame = pagesSame && p1.levelA == p2.levelA && p1.levelB == p2.levelB && p1.curveA == p2.curveA && p1.curveB == p2.curveB
                         && p1.morphOn == p2.morphOn && p1.ampOn == p2.ampOn && p1.startStep == p2.startStep;
    }
    if (tagAt[126] != (size_t) -1 && ! pagesSame)
    {
        const size_t s = tagAt[126] + 5;
        // the template's own pages, to find where they end
        size_t o = s + 17; bool ok = o < raw.size();
        for (int m = 0; m < 4 && ok; ++m)
        {
            const uint32_t t = u32 (raw, o);
            if (t == 0) o += 24; else if (t == 1) o += 336; else if (t == 2) o += 144; else ok = false;
        }
        if (ok && u32 (raw, o) == 3)
        {
            const std::vector<uint8_t> lead (raw.begin() + (long) s, raw.begin() + (long) s + 17);
            sp.push_back ({ s, o + 4, pagesBytes (q, lead) });
        }
    }
    if (haveMacros && q.macroName != tq.macroName)
    {
        std::vector<uint8_t> v;
        for (auto& n : q.macroName)
        {
            // UTF-8 -> 8 bit text (Latin-1; other characters as '?')
            std::string t;
            for (size_t i = 0; i < n.size() && t.size() < 24;)
            {
                const uint8_t c = (uint8_t) n[i];
                uint32_t cp = c; int extra = 0;
                if (c >= 0xf0) { cp = c & 7; extra = 3; } else if (c >= 0xe0) { cp = c & 15; extra = 2; } else if (c >= 0xc0) { cp = c & 31; extra = 1; }
                ++i;
                for (int k = 0; k < extra && i < n.size(); ++k, ++i) cp = (cp << 6) | ((uint8_t) n[i] & 63);
                if (cp < 32 || cp == 127) continue;
                t += (char) (cp < 256 ? cp : '?');
            }
            app32 (v, (uint32_t) t.size()); v.insert (v.end(), t.begin(), t.end());
        }
        sp.push_back ({ mStart, mEnd, v });
    }
    if (blocks.size() == 5)
    {
        const KeyCurve* cs[5] = { &q.oscUserCurve, &q.filterCurveCut[0], &q.filterCurveRes[0], &q.filterCurveCut[1], &q.filterCurveRes[1] };
        const KeyCurve* ct[5] = { &tq.oscUserCurve, &tq.filterCurveCut[0], &tq.filterCurveRes[0], &tq.filterCurveCut[1], &tq.filterCurveRes[1] };
        for (size_t i = 0; i < 5; ++i)
        {
            if (sameCurve (*cs[i], *ct[i])) continue;
            sp.push_back ({ blocks[i], cbs[i].end, curveBytes (*cs[i], cbs[i].version) });
        }
    }
    std::sort (sp.begin(), sp.end(), [] (const Splice& a, const Splice& b) { return a.from > b.from; });
    for (size_t i = 0; i + 1 < sp.size(); ++i)
        if (sp[i + 1].to > sp[i].from) { if (why) *why = "overlapping parts in the template"; return false; }
    // the payload's own chunk headers (before the knobs) give their sizes, up to the end: u64 sizes at 0, 84, 124
    // and 180 in every factory sound, ending 0, 0, 8 and 24 bytes before the end
    std::vector<size_t> sizeFields;
    {
        const size_t L = raw.size();
        for (size_t qo = 0; qo + 8 <= base; qo += 4)
        {
            const uint32_t v = u32 (raw, qo);
            if (u32 (raw, qo + 4) != 0 || v < 100 || qo + v > L) continue;
            const size_t rest = L - (qo + v);
            if (rest == 0 || rest == 8 || rest == 24) sizeFields.push_back (qo);
        }
    }
    const size_t before = raw.size();
    for (auto& x : sp) splice (raw, x.from, x.to, x.with);
    // the preset BPM, near the end (its place counts from the end, so after the splices)
    if (q.presetBpm != tq.presetBpm && q.presetBpm >= 20.0f && q.presetBpm <= 999.0f)
        if (const size_t o = nmsv::presetBpmPos (raw); o != 0) putF (raw, o, q.presetBpm);
    const int64_t grown = (int64_t) raw.size() - (int64_t) before;
    if (grown != 0)
        for (size_t qo : sizeFields) put32 (raw, qo, (uint32_t) ((int64_t) u32 (raw, qo) + grown));

    // ---- back into the container, with the sound's name (the first name string of its chunks, UTF-16 with its
    // length in front, 32 bytes after a "DSIN")
    std::vector<uint8_t> prefix (templ.begin(), templ.begin() + (long) pAt);
    size_t namePos = 0; uint32_t oldN = 0;
    for (size_t s = 0; s + 40 < prefix.size() && namePos == 0; ++s)
        if (std::memcmp (prefix.data() + s, "DSIN", 4) == 0)
        {
            const uint32_t n = u32 (prefix, s + 32);
            if (n < 1 || n > 255 || s + 36 + 2 * (size_t) n > prefix.size()) continue;
            bool ok = true;
            for (uint32_t i = 0; i < n && ok; ++i) ok = prefix[s + 36 + 2 * i] >= 32 || prefix[s + 37 + 2 * i] != 0;
            if (ok) { namePos = s + 32; oldN = n; }
        }
    std::vector<uint8_t> nameBytes;
    if (namePos != 0 && ! q.name.empty())
    {
        // UTF-8 -> UTF-16 (the basic plane)
        std::vector<uint16_t> u;
        for (size_t i = 0; i < q.name.size() && u.size() < 128;)
        {
            const uint8_t c = (uint8_t) q.name[i];
            uint32_t cp = c; int extra = 0;
            if (c >= 0xf0) { cp = c & 7; extra = 3; } else if (c >= 0xe0) { cp = c & 15; extra = 2; } else if (c >= 0xc0) { cp = c & 31; extra = 1; }
            ++i;
            for (int k = 0; k < extra && i < q.name.size(); ++k, ++i) cp = (cp << 6) | ((uint8_t) q.name[i] & 63);
            if (cp >= 32 && cp < 0xd800) u.push_back ((uint16_t) cp);
        }
        if (! u.empty())
        {
            app32 (nameBytes, (uint32_t) u.size());
            for (auto c : u) { nameBytes.push_back ((uint8_t) (c & 255)); nameBytes.push_back ((uint8_t) (c >> 8)); }
        }
    }
    if (raw == raw0 && (nameBytes.empty() || std::equal (nameBytes.begin(), nameBytes.end(), prefix.begin() + (long) namePos)))
    {
        out = templ;     // nothing changed: the template as it is (compressed as it was)
        return true;
    }
    const int64_t dName = nameBytes.empty() ? 0 : (int64_t) nameBytes.size() - (int64_t) (4 + 2 * (size_t) oldN);
    const auto packed = literalLzf (raw);
    const int64_t dPay = (int64_t) packed.size() - (int64_t) oldCl;
    if (! nameBytes.empty()) splice (prefix, namePos, namePos + 4 + 2 * (size_t) oldN, nameBytes);
    // the chunk sizes (u64, 12 bytes before each "hsin" / "DSIN") of the chunks holding the name or the payload
    for (size_t s = 12; s + 4 <= pAt; ++s)
        if (std::memcmp (templ.data() + s, "hsin", 4) == 0 || std::memcmp (templ.data() + s, "DSIN", 4) == 0)
        {
            const size_t qo = s - 12; uint64_t sz = 0; std::memcpy (&sz, templ.data() + qo, 8);
            if (sz > templ.size()) continue;
            int64_t d = 0;
            if (! nameBytes.empty() && qo < namePos && qo + sz > namePos) d += dName;
            if (qo < pAt && qo + sz > pAt) d += dPay;
            if (d == 0) continue;
            const size_t at = (! nameBytes.empty() && qo > namePos) ? (size_t) ((int64_t) qo + dName) : qo;
            const uint64_t ns = (uint64_t) ((int64_t) sz + d);
            std::memcpy (prefix.data() + at, &ns, 8);
        }
    out = prefix;
    out.push_back (1); app32 (out, (uint32_t) raw.size()); app32 (out, (uint32_t) packed.size());
    out.insert (out.end(), packed.begin(), packed.end());
    out.insert (out.end(), templ.begin() + (long) (pAt + 9 + oldCl), templ.end());
    return true;
}
} // namespace massif
