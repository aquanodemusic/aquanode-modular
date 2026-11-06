#pragma once
//==============================================================================
//  AquaVibrioFxbBank.h
//
//  .fxb sound banks ("CcnK" / "FBCh", plug-in ID kPluginId below) read into
//  hardware singles, so the Original engine plays them as it plays a .mid bank.
//
//  The bank is stored parameter by parameter rather than patch by patch: after
//  the 128 names come the parameters, each with its 128 values - a float per
//  patch (type 0), an array of floats per patch (type 1: the arpeggiator's
//  user pattern) or a string per patch (type 2: the oscillators' wave files).
//  Newer versions append parameters; the first 246 keep their meaning.
//
//  The plugin that writes these banks reads a single dump into those
//  parameters by tables of its own; the tables (AquaVibrioFxbTables.h) were
//  measured by importing banks of singles with every byte swept into it and
//  saving the bank again. A bank is read
//  back here by finding, for each stored value, the byte that gives it, plus
//  the few parameters that depend on two bytes (an LFO's rate is its rate
//  byte or its clock byte, depending on Sync, and so on). What has no
//  counterpart in a single (the plugin's reverb modulation, its Master knob,
//  the links between its LFOs) is left out.
//
//  The banks' own waveform and wavetable files ("\Factory\Brute.wav") are named
//  in the patch: waveFiles carries them, and the processor loads them as the
//  oscillators' own waves (Engine::setCustomWaves) when the patch is chosen.
//==============================================================================
#include <JuceHeader.h>
#include <cmath>
#include "AquaVibrioPatch.h"
#include "AquaVibrioFxbTables.h"

namespace aquavibrio::fxb
{
// The names these banks use, which have to match exactly: the plug-in ID in
// every bank's header, and the folders their patches name for the hardware's
// own single-cycle waves and its sampled wavetables (these play the waves
// loaded with Waves, by number).
inline constexpr char kPluginId[] = "VIPR";
inline constexpr char kClassicWaveFolder[] = "Virus Classic Waveforms";
inline constexpr char kSampledTableFolder[] = "Virus TI Wavetables";
// the folder that holds a bank's Waveforms and Wavetables folders, when the
// one chosen with Waves (or the bank's own) is the folder above it
inline constexpr char kBankFolder[] = "Viper";

struct Bank
{
    struct Record
    {
        int type { 0 };
        std::vector<float> values;                  // type 0
        std::vector<std::vector<float>> arrays;     // type 1
        juce::StringArray strings;                  // type 2
    };
    juce::StringArray names;
    std::vector<Record> records;
};

namespace detail
{
    struct Reader
    {
        const juce::uint8* d; size_t size; size_t at { 0 };
        bool ok { true };
        bool need (size_t n) { if (at + n > size) ok = false; return ok; }
        juce::uint32 u32() { if (! need (4)) return 0; const auto v = (juce::uint32) d[at] | ((juce::uint32) d[at + 1] << 8) | ((juce::uint32) d[at + 2] << 16) | ((juce::uint32) d[at + 3] << 24); at += 4; return v; }
        float f32() { const auto u = u32(); float f; std::memcpy (&f, &u, 4); return std::isfinite (f) ? f : 0.0f; }
        juce::String str()
        {
            if (! need (1)) return {};
            const size_t n = d[at++];
            if (! need (n)) return {};
            const auto s = juce::String::fromUTF8 (reinterpret_cast<const char*> (d + at), (int) n);
            at += n;
            return s;
        }
    };
}

// The bank chunk itself ("/C/..." then "FsCa"), as an .fxb carries it after its header.
inline bool parseChunk (const juce::uint8* c, size_t size, Bank& out)
{
    if (size < 16 || c[0] != '/' || c[1] != 'C' || c[2] != '/')
        return false;
    size_t start = 0;
    for (size_t i = 0; i + 4 <= juce::jmin<size_t> (size, 64); ++i)
        if (std::memcmp (c + i, "FsCa", 4) == 0) { start = i + 4; break; }
    if (start == 0)
        return false;

    detail::Reader r { c, size, start };
    const auto version = r.u32();
    const auto count = r.u32();
    if (version >= 2) r.u32();
    if (! r.ok || count == 0 || count > 1024)
        return false;
    out = {};
    for (juce::uint32 k = 0; k < count && r.ok; ++k)
        out.names.add (r.str());
    const auto numParams = r.u32();
    if (! r.ok || numParams > 4096)
        return false;
    for (juce::uint32 p = 0; p < numParams && r.ok; ++p)
    {
        Bank::Record rec;
        rec.type = (int) r.u32();
        const auto n = r.u32();
        if (n != count) { r.ok = false; break; }
        if (rec.type == 0)
            for (juce::uint32 k = 0; k < n && r.ok; ++k) rec.values.push_back (r.f32());
        else if (rec.type == 1)
            for (juce::uint32 k = 0; k < n && r.ok; ++k)
            {
                const auto len = r.u32();
                if (len > 4096) { r.ok = false; break; }
                std::vector<float> a;
                for (juce::uint32 i = 0; i < len && r.ok; ++i) a.push_back (r.f32());
                rec.arrays.push_back (std::move (a));
            }
        else if (rec.type == 2)
            for (juce::uint32 k = 0; k < n && r.ok; ++k) rec.strings.add (r.str());
        else
            r.ok = false;
        out.records.push_back (std::move (rec));
    }
    return r.ok && out.records.size() >= 246;
}

inline bool isFxbBank (const juce::MemoryBlock& data)
{
    const auto* d = static_cast<const juce::uint8*> (data.getData());
    return data.getSize() > 160 && std::memcmp (d, "CcnK", 4) == 0 && std::memcmp (d + 8, "FBCh", 4) == 0
        && std::memcmp (d + 16, kPluginId, 4) == 0;
}

inline bool parseFxb (const juce::MemoryBlock& data, Bank& out)
{
    const auto* d = static_cast<const juce::uint8*> (data.getData());
    if (isFxbBank (data))
    {
        const size_t chunk = ((size_t) d[156] << 24) | ((size_t) d[157] << 16) | ((size_t) d[158] << 8) | (size_t) d[159];
        return parseChunk (d + 160, juce::jmin (chunk, data.getSize() - 160), out);
    }
    return parseChunk (d, data.getSize(), out);   // a bare chunk
}

namespace detail
{
    inline int nearest (const float* table, int size, float v, int from, int to)
    {
        int best = -1; float bd = 1.0e9f;
        for (int k = juce::jmax (0, from); k <= juce::jmin (size - 1, to); ++k)
        {
            if (std::isnan (table[k])) continue;
            const float d = std::abs (table[k] - v);
            if (d < bd - 1.0e-9f) { bd = d; best = k; }
        }
        return best;
    }
    inline const Rule* ruleFor (int record)
    {
        for (const auto& r : kRules) if (r.record == record) return &r;
        return nullptr;
    }
    inline int distinct (const Rule& r)
    {
        std::vector<float> v;
        for (float t : r.table)
            if (! std::isnan (t) && std::none_of (v.begin(), v.end(), [t] (float x) { return std::abs (x - t) < 1.0e-5f; }))
                v.push_back (t);
        return (int) v.size();
    }
    inline int classicWave (const juce::String& s)
    {
        if (s == "\\Sine") return 0;
        if (s == "\\Triangle") return 1;
        if (s.containsIgnoreCase (kClassicWaveFolder))
        {
            const int n = s.fromLastOccurrenceOf ("Wave ", false, false).getIntValue();
            if (n >= 3 && n <= 64) return n - 1;
        }
        return -1;
    }
    inline int sampledTable (const juce::String& s)
    {
        if (! s.containsIgnoreCase (kSampledTableFolder)) return -1;
        const int n = s.fromLastOccurrenceOf ("\\", false, false).getIntValue();
        return n >= 1 && n <= 99 ? n : -1;
    }
}

// One patch of the bank as a four-page single (pages A to D).
inline Patch convertPatch (const Bank& bank, int p)
{
    using namespace detail;
    std::array<juce::uint8, 512> b {};
    std::memcpy (b.data(), kDefaultBytes, 512);

    auto V = [&] (int r, float d = 0.0f)
    {
        return r < (int) bank.records.size() && bank.records[(size_t) r].type == 0 && p < (int) bank.records[(size_t) r].values.size()
                 ? bank.records[(size_t) r].values[(size_t) p] : d;
    };
    auto S = [&] (int r) -> juce::String
    {
        return r < (int) bank.records.size() && bank.records[(size_t) r].type == 2 && p < bank.records[(size_t) r].strings.size()
                 ? bank.records[(size_t) r].strings[p] : juce::String();
    };
    auto on = [&] (int r) { return V (r) > 0.005f; };

    // every parameter with a byte of its own (fewer distinct values first)
    for (const auto& rule : kRules)
    {
        if (rule.record >= (int) bank.records.size() || bank.records[(size_t) rule.record].type != 0) continue;
        const int k = nearest (rule.table, 128, V (rule.record), 0, 127);
        if (k >= 0) b[(size_t) rule.byte] = (juce::uint8) k;
    }
    // on/off parameters that share a byte with a mode: off is that byte's one value
    for (const auto& rule : kRules)
    {
        if (rule.record >= (int) bank.records.size() || bank.records[(size_t) rule.record].type != 0 || distinct (rule) != 2) continue;
        const int k = nearest (rule.table, 128, V (rule.record), 0, 127);
        if (k < 0) continue;
        int same = 0, only = -1;
        for (int i = 0; i < 128; ++i)
            if (! std::isnan (rule.table[i]) && std::abs (rule.table[i] - rule.table[k]) < 1.0e-6f) { ++same; only = i; }
        if (same == 1) b[(size_t) rule.byte] = (juce::uint8) only;
    }

    auto set = [&] (int byte, int v) { b[(size_t) byte] = (juce::uint8) juce::jlimit (0, 127, v); };
    auto table = [] (int record) -> const float* { const auto* r = ruleFor (record); return r != nullptr ? r->table : nullptr; };

    // unison: the plugin takes the larger of page A's and page D's
    for (int o : { 504, 505, 506, 507 }) b[(size_t) o] = 0;

    // LFO rates: free (the rate byte, value = byte / 128) or synced (the clock byte)
    const int lfo[3][4] = { { 121, 95, 67, 146 }, { 122, 97, 79, 147 }, { 123, 99, 135, 149 } };
    for (const auto& l : lfo)
    {
        if (on (l[1])) set (l[3], juce::jmax (1, nearest (kLfoClock, 22, V (l[0]), 1, 21)));
        else { set (l[3], 0); set (l[2], (int) std::lround (V (l[0]) * 128.0f)); }
    }
    // filter saturation: the digital filter modes only
    if (b[51] <= 3) set (49, juce::jmin (14, (int) std::lround (V (26) * 100.0f)));
    // voicing: Poly 8 / 16 / 32 / 64 are Poly, then Mono 1 .. 4
    {
        const int vo = (int) std::lround (V (242) * 100.0f);
        set (94, vo <= 3 ? 0 : juce::jlimit (1, 4, vo - 3));
    }
    // delay and reverb: off is a send of 0; one colour knob is low pass below its middle, high pass above
    if (! on (173)) set (113, 0);
    if (auto* lp = table (178)) if (auto* hp = table (179))
        set (119, V (179) > 0.003f ? nearest (hp, 128, V (179), 65, 127) : nearest (lp, 128, V (178), 0, 64));
    if (! on (212)) set (258, 0);
    if (auto* lp = table (219)) if (auto* hp = table (220))
        set (262, V (220) > 0.003f ? nearest (hp, 128, V (220), 65, 127) : nearest (lp, 128, V (219), 0, 64));
    if (on (214)) set (264, juce::jmax (1, nearest (kPredelayClock, 17, V (216), 1, 16)));
    else { set (264, 0); set (263, juce::jmax (0, nearest (kPredelayFree, 128, V (216), 0, 92))); }
    // distortion: off is curve 0
    if (! on (206)) set (228, 0);
    else if (auto* t = table (207)) set (228, juce::jmax (1, nearest (t, 128, V (207), 1, 19)));
    auto level = [] (float v) { return v > 0.996f ? 127 : (int) std::lround (v * 128.0f); };
    set (229, level (V (209)));
    set (328, level (V (208)));
    set (329, level (V (211)));
    // the filter effects' frequency
    if (on (222)) set (277, level (V (229)));
    // sections the plugin switches off on their own
    if (V (198, 1.0f) < 0.5f) set (213, 0);                                  // phaser: no mix
    if (V (190, 1.0f) < 0.5f) { set (220, 64); set (223, 64); set (224, 64); } // EQ: flat
    if (V (170, 1.0f) < 0.5f) set (225, 0);                                  // character: none
    // arpeggiator: Arp Note plays the held notes on every step (chord, one octave); Arp Out is Arp > Matrix
    {
        const int am = (int) std::lround (V (232) * 100.0f);
        set (129, am == 7 ? 6 : am == 8 ? 7 : juce::jlimit (0, 6, am));
        if (am == 7) set (131, 0);
        if (230 < (int) bank.records.size() && bank.records[230].type == 1 && p < (int) bank.records[230].arrays.size())
        {
            const auto& st = bank.records[230].arrays[(size_t) p];
            if (st.size() >= 96)
            {
                const int length = juce::jlimit (1, 32, (int) std::lround (V (231, 0.125f) * 32.0f));
                bool allOn = true;
                for (int k = 0; k < length; ++k) allOn = allOn && st[(size_t) (3 * k)] > 0.5f;
                if (allOn && (length == 1 || length == 16 || length == 32))
                    set (130, 1);                                            // every sixteenth
                else
                {
                    set (130, 0);                                            // the user grid
                    set (383, length - 1);
                    for (int k = 0; k < 32; ++k)
                    {
                        set (384 + 3 * k, (int) std::lround (st[(size_t) (3 * k + 1)] * 127.0f));       // length
                        set (384 + 3 * k + 1, (int) std::lround (st[(size_t) (3 * k + 2)] * 127.0f));   // velocity
                        set (384 + 3 * k + 2, st[(size_t) (3 * k)] > 0.5f ? 1 : 0);                     // on
                    }
                }
            }
        }
    }

    Patch out;
    out.hasFourPages = true;
    std::memcpy (out.bytes.data(), b.data(), 512);

    // the oscillators' waves: the hardware's own by number, the bank's own files as the patch's own waves
    const int wsel[2] = { 19, 24 }, modeByte[2] = { 286, 291 };
    for (int n = 0; n < 2; ++n)
    {
        const auto s = S (42 + n);
        const bool wt = b[(size_t) modeByte[n]] >= 2;
        if (wt)
        {
            const int table = sampledTable (s);
            if (table > 0) out.bytes[(size_t) wsel[n]] = (juce::uint8) table;
            else if (s.endsWithIgnoreCase (".wav")) { out.waveFiles[(size_t) n] = s; out.waveIsTable[(size_t) n] = true; out.bytes[(size_t) wsel[n]] = 1; }
        }
        else
        {
            const int w = classicWave (s);
            if (w >= 0) out.bytes[(size_t) wsel[n]] = (juce::uint8) w;
            else if (s.endsWithIgnoreCase (".wav")) { out.waveFiles[(size_t) n] = s; out.waveIsTable[(size_t) n] = false; out.bytes[(size_t) wsel[n]] = 2; }
        }
    }
    {
        const auto s = S (44);
        const char* fixed[] = { "\\Off", "\\Slave", "\\Saw", "\\Pulse", "\\Sine", "\\Triangle" };
        int mode = -1;
        for (int k = 0; k < 6; ++k) if (s == fixed[k]) mode = k;
        if (mode < 0) { const int w = classicWave (s); if (w >= 2) mode = w + 4; }
        if (mode < 0 && s.endsWithIgnoreCase (".wav")) { mode = 6; out.waveFiles[2] = s; out.waveIsTable[2] = false; }
        if (mode >= 0) out.bytes[169] = (juce::uint8) mode;
    }

    // The plugin's Master knob is stored with each patch: its middle is the level a
    // single plays at, and the gain follows the knob squared (measured)
    out.trimDb = juce::jlimit (-24.0f, 12.0f, 40.0f * std::log10 (juce::jmax (0.01f, V (239, 0.5f)) / 0.5f));

    auto name = p < bank.names.size() ? bank.names[p] : juce::String();
    out.longName = name;
    out.setName (name);
    return out;
}

inline std::vector<Patch> convertBank (const Bank& bank)
{
    std::vector<Patch> patches;
    for (int p = 0; p < bank.names.size(); ++p)
        patches.push_back (convertPatch (bank, p));
    return patches;
}
} // namespace aquavibrio::fxb
