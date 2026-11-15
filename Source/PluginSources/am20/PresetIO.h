#pragma once
// AM-20: reading and writing the original's preset files (.fxp programs and
// .fxb banks). JUCE-free.
//
// The layout was worked out from the outside only (see MEASUREMENTS.md): the
// original was asked for its state through the plain VST2 interface after one
// control at a time had been changed - knob by knob, and cable by cable on its
// patch panel - and the saved states were compared. Nothing was disassembled.
//
//   program chunk ("gorp", or "prog" inside a bank), 2745 bytes, little endian:
//     0  tag, 4 size (2737, or 2040 in banks), 8 version (2.5; 1.x programs
//        are 2048 bytes long), 16 name (32 bytes)
//     48 + 4 * slot   float 0..1 per control (slots: am20::chunkSlot)
//     384 + 4 * i     patch cables, up to 17: jack, jack, colour, 0 (0xffff = none)
//     2048 / 2344     the two effect slots, 296 bytes each: effect program name
//                     at +0, effect type at +32 ("xfon" = none). AM-20 has no
//                     effects: it keeps them untouched for .fxp / .fxb export
//                     and writes empty slots otherwise. Older versions of the
//                     original wrote 2048-byte programs without them.
//     2640            trailer (tag, name)
//   .fxp: VST2 "CcnK"/"FPCh" header (plugin id "KLMV") + the program chunk
//   .fxb: "CcnK"/"FBCh" + a "bolg" chunk: global data, then "prog" records
#include "Params.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

namespace am20
{
struct Preset
{
    std::string name { "Init" };
    float values[NumParams] {};
    std::vector<std::pair<int, int>> cables;     // as stored: either order
    // the original's two effect slots (chunk bytes 2048..2639) exactly as they
    // were in a loaded .fxp / .fxb, so exporting there again keeps them;
    // empty when the program has no effects (and in everything from .am20)
    std::vector<uint8_t> fx;
    Preset() { for (int i = 0; i < NumParams; ++i) values[i] = defaultValue (i); }
};

namespace presetio
{
    constexpr size_t ChunkSize = 2745;
    constexpr int CableOffset = 384, MaxCables = 17;
    constexpr size_t FxOffset = 2048, FxSlotSize = 296, FxSize = 2 * FxSlotSize;

    inline uint32_t rdLE (const uint8_t* d) { return (uint32_t) d[0] | (uint32_t) d[1] << 8 | (uint32_t) d[2] << 16 | (uint32_t) d[3] << 24; }
    inline uint32_t rdBE (const uint8_t* d) { return (uint32_t) d[0] << 24 | (uint32_t) d[1] << 16 | (uint32_t) d[2] << 8 | d[3]; }
    inline float rdF (const uint8_t* d) { uint32_t u = rdLE (d); float f; std::memcpy (&f, &u, 4); return f; }
    inline void wrLE (uint8_t* d, uint32_t v) { d[0] = (uint8_t) v; d[1] = (uint8_t) (v >> 8); d[2] = (uint8_t) (v >> 16); d[3] = (uint8_t) (v >> 24); }
    inline void wrF (uint8_t* d, float f) { uint32_t u; std::memcpy (&u, &f, 4); wrLE (d, u); }
    inline void wrBE (std::vector<uint8_t>& o, uint32_t v) { o.push_back ((uint8_t) (v >> 24)); o.push_back ((uint8_t) (v >> 16)); o.push_back ((uint8_t) (v >> 8)); o.push_back ((uint8_t) v); }

    inline std::string cString (const uint8_t* d, size_t max)
    {
        std::string s;
        for (size_t i = 0; i < max && d[i]; ++i) s += (d[i] >= 32 && d[i] < 127) ? (char) d[i] : '?';
        while (! s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }

    // an effect slot that holds an effect (its type is not "xfon", no effect)
    inline bool fxSlotUsed (const uint8_t* slot)
    {
        const uint8_t* t = slot + 32;
        return (t[0] | t[1] | t[2] | t[3]) != 0 && std::memcmp (t, "xfon", 4) != 0;
    }

    // what the effect slots hold, for the editor: "Dyna Compressor + Stereo Delay"
    inline std::string fxDescription (const Preset& p)
    {
        if (p.fx.size() != FxSize) return {};
        std::string out;
        for (size_t k = 0; k < 2; ++k)
        {
            const uint8_t* slot = p.fx.data() + k * FxSlotSize;
            if (! fxSlotUsed (slot)) continue;
            auto n = cString (slot, 32);
            if (n.empty() || n == "Init Program") n = "effect " + std::to_string (k + 1);
            out += (out.empty() ? "" : " + ") + n;
        }
        return out;
    }

    // one program record (tag "gorp" or "prog"); false if it does not look like one
    inline bool readProgram (const uint8_t* d, size_t sz, Preset& p)
    {
        if (sz < 452) return false;
        if (std::memcmp (d, "gorp", 4) != 0 && std::memcmp (d, "prog", 4) != 0) return false;
        p = Preset();
        p.name = cString (d + 16, 32);
        for (int i = 0; i < NumParams; ++i)
        {
            const int slot = chunkSlot (i);
            if (slot < 0) continue;
            const size_t off = 48 + 4 * (size_t) slot;
            if (off + 4 > sz) continue;
            const float v = rdF (d + off);
            if (v == v) p.values[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }
        for (int c = 0; c < MaxCables; ++c)
        {
            const uint8_t* e = d + CableOffset + 4 * c;
            if (e[0] == 0xff || e[1] == 0xff) continue;
            if (e[0] < 35 && e[1] < 35 && e[0] != e[1]) p.cables.push_back ({ (int) e[0], (int) e[1] });
        }
        // the effect slots, when the record has them and one is in use: the size field is not
        // reliable (2040 in banks), the version is - 1.x programs end at 2048, 2.x have effects
        if (d[11] >= 2 && sz >= FxOffset + FxSize
            && (fxSlotUsed (d + FxOffset) || fxSlotUsed (d + FxOffset + FxSlotSize)))
            p.fx.assign (d + FxOffset, d + FxOffset + FxSize);
        return true;
    }

    // the original's own start-up values for the slots AM-20 does not use, and
    // two empty effect slots (or the ones the program was loaded with), so a
    // written program loads cleanly in the original
    inline void writeProgram (const Preset& p, uint8_t* d, bool bankRecord)
    {
        std::memset (d, 0, ChunkSize);
        static const std::pair<int, uint32_t> base[] = {
            {8,0x02050000u}, {48,0x3eaaaaabu}, {56,0x3f2aaaabu}, {60,0x3e8f5c29u}, {68,0x3f000000u}, {72,0x3eaaaaabu}, {76,0x3f000000u}, {80,0x3f733333u},
            {112,0x3f800000u}, {128,0x3f000000u}, {132,0x3e800000u}, {156,0x3f000000u}, {160,0x3f800000u}, {168,0x3f570a3du}, {176,0x3f800000u},
            {188,0x3f800000u}, {216,0x3f000000u}, {220,0x3e2aaaabu}, {224,0x3d4ccccdu}, {228,0x3f800000u}, {232,0x3ea712ddu}, {236,0x3f000000u},
            {272,0x3f000000u}, {276,0x3f000000u}, {280,0x3f000000u}, {284,0x3f000000u}, {288,0x3f000000u}, {292,0x3f000000u}, {296,0x3f000000u},
            {300,0x3f000000u}, {304,0x3f000000u}, {308,0x3f000000u}, {312,0x3f000000u}, {316,0x3f000000u}, {320,0x3f000000u}, {324,0x3f000000u},
            {328,0x3f000000u}, {332,0x3f000000u}, {344,0x3d924925u}, {348,0x3eb6db6eu}, {364,0x3f4ccccdu}, {368,0x3f4ccccdu},
            {2048,0x45206f4eu}, {2052,0x63656666u}, {2056,0x00000074u}, {2080,0x6e6f6678u}, {2084,0x02050000u}, {2092,0x3f800000u}, {2096,0x3f608f82u},
            {2100,0x3f608f82u}, {2108,0x3f000000u}, {2112,0x3f000000u}, {2156,0x3f000000u}, {2160,0x3f000000u}, {2172,0x3f000000u}, {2176,0x3f000000u},
            {2188,0x3f000000u}, {2192,0x3f000000u}, {2204,0x3f000000u}, {2208,0x3f000000u}, {2344,0x45206f4eu}, {2348,0x63656666u}, {2352,0x00000074u},
            {2376,0x6e6f6678u}, {2380,0x02050000u}, {2388,0x3f800000u}, {2392,0x3f471c72u}, {2396,0x3f471c72u}, {2404,0x3f000000u}, {2408,0x3f000000u},
            {2436,0x3f800000u}, {2452,0x3f000000u}, {2456,0x3f000000u}, {2468,0x3f000000u}, {2472,0x3f000000u}, {2484,0x3f000000u}, {2488,0x3f000000u},
            {2500,0x3f000000u}, {2504,0x3f000000u}, {2640,0xffffffffu}, {2644,0xffffffffu}, {2648,0x74206f4eu}, {2652,0x00006761u} };
        for (auto& b : base) wrLE (d + b.first, b.second);
        if (p.fx.size() == FxSize) std::memcpy (d + FxOffset, p.fx.data(), FxSize);   // the effects it came with
        std::memcpy (d, bankRecord ? "prog" : "gorp", 4);
        wrLE (d + 4, (uint32_t) (ChunkSize - 8));
        const size_t n = std::min<size_t> (p.name.size(), 31);
        std::memcpy (d + 16, p.name.data(), n);
        std::memcpy (d + 2680, p.name.data(), std::min<size_t> (n, 64));
        for (int i = 0; i < NumParams; ++i) { const int slot = chunkSlot (i); if (slot >= 0) wrF (d + 48 + 4 * slot, p.values[i]); }
        for (int c = 0; c < MaxCables; ++c)
        {
            uint8_t* e = d + CableOffset + 4 * c;
            if (c < (int) p.cables.size()) { e[0] = (uint8_t) p.cables[(size_t) c].first; e[1] = (uint8_t) p.cables[(size_t) c].second; e[2] = (uint8_t) (c % 4); e[3] = 0; }
            else { e[0] = e[1] = 0xff; e[2] = e[3] = 0; }
        }
    }

    // every program in an .fxp / .fxb file, or in a bare chunk
    inline std::vector<Preset> readFile (const uint8_t* d, size_t sz, std::string& error)
    {
        std::vector<Preset> out;
        auto scan = [&] (const uint8_t* c, size_t n)
        {
            for (size_t i = 0; i + 452 <= n; ++i)
            {
                if ((c[i] == 'g' && std::memcmp (c + i, "gorp", 4) == 0) || (c[i] == 'p' && std::memcmp (c + i, "prog", 4) == 0))
                {
                    // the size field varies between versions; check that the knobs look like knobs instead
                    bool plausible = true;
                    for (int k = 0; k < 36 && plausible; ++k) { const float v = rdF (c + i + 48 + 4 * k); plausible = v == v && v >= -0.001f && v <= 1.001f; }
                    if (! plausible) continue;
                    Preset p;
                    if (readProgram (c + i, n - i, p)) { out.push_back (p); i += 447; }
                }
            }
        };
        if (sz >= 60 && std::memcmp (d, "CcnK", 4) == 0)
        {
            const bool prog = std::memcmp (d + 8, "FPCh", 4) == 0, bank = std::memcmp (d + 8, "FBCh", 4) == 0;
            if (! prog && ! bank) { error = "This VST2 file stores plain parameters, not the MS-20's own data"; return out; }
            if (std::memcmp (d + 16, "KLMV", 4) != 0) { error = "This VST2 file was not made by the MS-20 plugin"; return out; }
            const size_t hdr = prog ? 60 : 160;
            if (sz < hdr) { error = "The file is too short"; return out; }
            const size_t csz = rdBE (d + hdr - 4);
            if (hdr + csz > sz) { error = "The file is cut short"; return out; }
            scan (d + hdr, csz);
            // in a bank the last program is the one that was playing; drop it when it repeats the list
            if (bank && out.size() > 1) out.pop_back();
        }
        else scan (d, sz);
        if (out.empty() && error.empty()) error = "No MS-20 programs found in the file";
        return out;
    }

    inline std::vector<uint8_t> writeFxp (const Preset& p)
    {
        std::vector<uint8_t> o;
        o.insert (o.end(), { 'C', 'c', 'n', 'K' }); wrBE (o, 0);
        o.insert (o.end(), { 'F', 'P', 'C', 'h' }); wrBE (o, 1);
        o.insert (o.end(), { 'K', 'L', 'M', 'V' }); wrBE (o, 1); wrBE (o, 0);
        char nm[28] = {}; std::memcpy (nm, p.name.data(), std::min<size_t> (p.name.size(), 27)); o.insert (o.end(), nm, nm + 28);
        wrBE (o, (uint32_t) ChunkSize);
        const size_t at = o.size(); o.resize (at + ChunkSize);
        writeProgram (p, o.data() + at, false);
        return o;
    }

    inline std::vector<uint8_t> writeFxb (const std::vector<Preset>& ps, int current)
    {
        std::vector<uint8_t> chunk (2048, 0);
        std::memcpy (chunk.data(), "bolg", 4); wrLE (chunk.data() + 8, 0x02050000u);
        for (auto& p : ps) { const size_t at = chunk.size(); chunk.resize (at + ChunkSize); writeProgram (p, chunk.data() + at, true); }
        if (! ps.empty()) { const size_t at = chunk.size(); chunk.resize (at + ChunkSize); writeProgram (ps[(size_t) std::max (0, std::min (current, (int) ps.size() - 1))], chunk.data() + at, false); }
        wrLE (chunk.data() + 4, (uint32_t) (chunk.size() - 8));
        std::vector<uint8_t> o;
        o.insert (o.end(), { 'C', 'c', 'n', 'K' }); wrBE (o, 0);
        o.insert (o.end(), { 'F', 'B', 'C', 'h' }); wrBE (o, 1);
        o.insert (o.end(), { 'K', 'L', 'M', 'V' }); wrBE (o, 1); wrBE (o, (uint32_t) ps.size());
        o.resize (o.size() + 128, 0);
        wrBE (o, (uint32_t) chunk.size());
        o.insert (o.end(), chunk.begin(), chunk.end());
        return o;
    }
}
}
