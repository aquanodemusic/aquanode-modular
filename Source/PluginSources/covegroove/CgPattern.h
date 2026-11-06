#pragma once
//==============================================================================
// Cove Groove - the pattern (8 lanes x up to 4 bars of 16ths) and the
// generator that writes hardgroove into it.
//
// What the generator knows (from a set of reference loops at 123-143 BPM):
//   - the kick is four on the floor, always; Variation adds a ghost 16th
//     before a downbeat now and then, and the last bar may drop or move a kick
//   - the bass rolls on the 8th offbeats (2, 6, 10, 14), Density adds
//     gallops on the 16ths before them
//   - claps on 2 and 4, with ghosts and flams on Variation (Tribal: sparser)
//   - closed hats on the 8ths with the 16th offbeats softer (Density thins
//     or fills them), never on the open hat's offbeats; the open hat on the
//     8th offbeats
//   - the cymbal lane follows its sound: a ride on 8ths / offbeats, a shaker
//     on 16ths, a crash on the first downbeat, a reversed crash swelling into
//     the next bar (the drum-machine and kit cymbals play as rides / crashes)
//   - the perc lane plays syncopated conga / tom figures (3-3-2s, tresillos,
//     off-grid groups), with high and low notes ("alt" steps)
//   - the FX lane is sparse: a zap or noise into the second / fourth bar, stabs
//     (and the metal) on syncopations, a riser into the loop; a tambourine
//     on the offbeats, maracas on 16ths
//   - Rumble (after a second set of loops, at 140 BPM): the bass rolls as a
//     gated rumble on the offbeat and the 16th after it, the closed hat runs
//     16ths with the 8th offbeats on top, the toms play 0-2-4-7-10-12 figures
//   Every bar after the first is the first one, varied; the last bar of 2 or
//   4 gets a fill. The shuffle itself is the Swing knob (the references sit at
//   57-72 %), applied when playing.
//==============================================================================
#include "CgVoices.h"
#include <string>
#include <vector>
#include <initializer_list>

namespace covegroove
{
    constexpr int kStepsPerBar = 16;
    constexpr int kMaxBars = 4;
    constexpr int kMaxSteps = kStepsPerBar * kMaxBars;
    enum Style { Rolling = 0, Tribal, Shuffle, Stomp, Rumble, NumStyles };

    inline const char* styleName (int s)
    {
        static const char* n[NumStyles] = { "Rolling", "Tribal", "Shuffle", "Stomp", "Rumble" };
        return n[std::clamp (s, 0, NumStyles - 1)];
    }

    struct Step
    {
        uint8_t vel { 0 };          // 0 = off, 1..127
        bool alt { false };         // the voice's second note / an accent
        bool operator== (const Step& o) const { return vel == o.vel && alt == o.alt; }
    };

    struct Pattern
    {
        int bars { 1 };
        std::array<std::array<Step, kMaxSteps>, kNumLanes> lanes {};

        int length() const { return std::clamp (bars, 1, kMaxBars) * kStepsPerBar; }
        bool operator== (const Pattern& o) const { return bars == o.bars && lanes == o.lanes; }
        bool operator!= (const Pattern& o) const { return ! (*this == o); }

        // "b4|lane0hex|lane1hex|..." - two hex digits a step (alt = +128)
        std::string toString() const
        {
            static const char* hx = "0123456789abcdef";
            std::string s = "b" + std::to_string (bars);
            for (const auto& lane : lanes)
            {
                s += '|';
                for (const auto& st : lane)
                {
                    const int v = (st.vel & 127) | (st.alt ? 128 : 0);
                    s += hx[(v >> 4) & 15];
                    s += hx[v & 15];
                }
            }
            return s;
        }

        bool fromString (const std::string& s)
        {
            if (s.size() < 2 || s[0] != 'b') return false;
            Pattern p;
            p.bars = std::clamp (s[1] - '0', 1, kMaxBars);
            auto hexv = [] (char c) { return c >= 'a' ? c - 'a' + 10 : (c >= 'A' ? c - 'A' + 10 : c - '0'); };
            size_t i = 2;
            for (int l = 0; l < kNumLanes; ++l)
            {
                if (i >= s.size() || s[i] != '|') return false;
                ++i;
                for (int st = 0; st < kMaxSteps && i + 1 < s.size() && s[i] != '|'; ++st, i += 2)
                {
                    const int v = (hexv (s[i]) << 4) | hexv (s[i + 1]);
                    p.lanes[(size_t) l][(size_t) st].vel = (uint8_t) (v & 127);
                    p.lanes[(size_t) l][(size_t) st].alt = (v & 128) != 0;
                }
                while (i < s.size() && s[i] != '|') ++i;
            }
            *this = p;
            return true;
        }
    };

    //==========================================================================
    class Generator
    {
    public:
        struct Settings
        {
            int style { Rolling };
            float density { 0.5f };         // 0..1
            float variation { 0.4f };       // 0..1
            int bars { 1 };
            std::array<int, kNumLanes> models {};   // the lanes' sounds (the cymbal and perc lanes look at it)
        };

        // writes the lanes in laneMask (bit per lane); the pattern's bar count becomes s.bars
        static void generate (Pattern& p, const Settings& s, uint32_t seed, unsigned laneMask = 0xffu)
        {
            Rng rng (seed);
            p.bars = std::clamp (s.bars, 1, kMaxBars);
            for (int lane = 0; lane < kNumLanes; ++lane)
            {
                if ((laneMask & (1u << lane)) == 0) continue;
                auto& L = p.lanes[(size_t) lane];
                for (auto& st : L) st = Step();
                std::array<Step, kStepsPerBar> bar {};
                base (lane, s, rng, bar);
                for (int b = 0; b < p.bars; ++b)
                {
                    std::array<Step, kStepsPerBar> v = bar;
                    if (b > 0) vary (lane, s, rng, v, b);
                    if (p.bars > 1 && b == p.bars - 1) fill (lane, s, rng, v);
                    if (b == 0 && lane == Cymbal && cymbalRole (s.models[Cymbal]) == RoleCrash) v[0] = { 120, false };   // crash on the one
                    for (int i = 0; i < kStepsPerBar; ++i)
                        L[(size_t) (b * kStepsPerBar + i)] = v[(size_t) i];
                }
                // the reversed crash swells into the next loop: once, late in the last bar
                if (lane == Cymbal && cymbalRole (s.models[Cymbal]) == RoleSwell)
                {
                    for (auto& st : L) st = Step();
                    L[(size_t) ((p.bars - 1) * kStepsPerBar + 10)] = { 110, false };
                }
                // a reverb hit opens the loop (and its second half, over four bars)
                if (lane == Fx && fxRole (s.models[Fx]) == RoleCrash)
                {
                    for (auto& st : L) st = Step();
                    L[0] = { 112, false };
                    if (p.bars == 4) L[(size_t) (2 * kStepsPerBar)] = { 100, false };
                }
                // and the riser climbs into it from the middle of the last bar
                if (lane == Fx && fxRole (s.models[Fx]) == RoleSwell)
                {
                    for (auto& st : L) st = Step();
                    L[(size_t) ((p.bars - 1) * kStepsPerBar + 8)] = { 104, false };
                }
            }
            // the closed hat stays off the open hat's steps (it would choke it)
            for (int i = 0; i < p.length(); ++i)
                if (p.lanes[OpenHat][(size_t) i].vel > 0 && (laneMask & ((1u << ClosedHat) | (1u << OpenHat))) != 0)
                    p.lanes[ClosedHat][(size_t) i] = Step();
        }

    private:
        static uint8_t vel (Rng& r, int v, int spread = 8)
        {
            return (uint8_t) std::clamp (v + (int) std::lround (r.next() * (float) spread), 1, 127);
        }
        static bool chance (Rng& r, float p) { return r.uni() < p; }

        static void put (std::array<Step, kStepsPerBar>& bar, std::initializer_list<int> steps, Rng& r, int v, bool alt = false)
        {
            for (int s : steps) bar[(size_t) (s & 15)] = { vel (r, v), alt };
        }

        static void base (int lane, const Settings& s, Rng& r, std::array<Step, kStepsPerBar>& bar)
        {
            const float d = s.density, var = s.variation;
            switch (lane)
            {
                case Kick:
                    put (bar, { 0, 4, 8, 12 }, r, 122, false);
                    if (s.style == Stomp && chance (r, 0.3f + 0.4f * d)) put (bar, { 15 }, r, 80);
                    if (s.style == Rumble && chance (r, 0.15f + 0.3f * var)) put (bar, { chance (r, 0.5f) ? 14 : 10 }, r, 76);
                    break;

                case Bass:
                    if (s.style == Stomp && d < 0.3f) break;
                    if (s.style == Rumble)
                    {
                        // kick - rumble - rumble: the offbeat and the 16th after it
                        put (bar, { 2, 6, 10, 14 }, r, 110);
                        for (int st : { 3, 7, 11, 15 })
                            if (chance (r, 0.45f + 0.55f * d)) put (bar, { st }, r, 84);
                        if (chance (r, d * 0.5f)) put (bar, { 1, 9 }, r, 62);
                        break;
                    }
                    put (bar, { 2, 6, 10, 14 }, r, 108);
                    if (s.style == Rolling && chance (r, d * 0.7f)) put (bar, { chance (r, 0.5f) ? 3 : 11 }, r, 82);
                    if (s.style == Tribal && chance (r, d * 0.6f)) put (bar, { 7 }, r, 84);
                    if (s.style == Shuffle && chance (r, 0.3f + d * 0.6f)) put (bar, { 15 }, r, 78);
                    if (chance (r, var * 0.25f)) bar[14].alt = true;
                    break;

                case Clap:
                    if (s.style == Tribal && d < 0.5f) put (bar, { 12 }, r, 112);
                    else put (bar, { 4, 12 }, r, 114);
                    if (chance (r, var * 0.5f)) put (bar, { chance (r, 0.5f) ? 11 : 15 }, r, 52);
                    if (s.style == Shuffle && chance (r, var * 0.6f)) put (bar, { 7 }, r, 48);
                    if (s.style == Stomp && chance (r, 0.3f + d * 0.4f)) put (bar, { 14 }, r, 96);
                    break;

                case ClosedHat:
                    if (s.style == Rumble)
                    {
                        // 16ths, the 8th offbeats on top
                        for (int i = 0; i < 16; ++i)
                            if (i % 2 == 0 || chance (r, 0.4f + 0.6f * d))
                                bar[(size_t) i] = { vel (r, i % 4 == 2 ? 104 : (i % 2 == 0 ? 82 : 60), 8), false };
                        break;
                    }
                    for (int i = 0; i < 16; i += 2) bar[(size_t) i] = { vel (r, 96), false };
                    for (int i = 1; i < 16; i += 2)
                        if (s.style != Tribal || chance (r, d * 0.5f))
                            if (chance (r, 0.25f + 0.75f * d)) bar[(size_t) i] = { vel (r, 68, 10), false };
                    break;

                case OpenHat:
                    put (bar, { 2, 6, 10, 14 }, r, 104);
                    if (s.style == Shuffle && chance (r, var * 0.5f)) put (bar, { 15 }, r, 70);
                    break;

                case Cymbal:
                {
                    const Role m = cymbalRole (s.models[Cymbal]);
                    if (m == RoleShaker)   // shaker: 16ths, accented
                    {
                        for (int i = 0; i < 16; ++i)
                            if (i % 2 == 0 || chance (r, 0.35f + 0.65f * d))
                                bar[(size_t) i] = { vel (r, i % 4 == 2 ? 100 : (i % 2 == 0 ? 84 : 64), 8), false };
                    }
                    else if (m == RoleRide)   // ride
                    {
                        switch (s.style)
                        {
                            case Tribal:  for (int i = 0; i < 16; i += 2) bar[(size_t) i] = { vel (r, i % 4 == 2 ? 104 : 80), false }; break;
                            case Shuffle: put (bar, { 0, 4, 8, 12 }, r, 96); if (chance (r, 0.3f + 0.6f * d)) put (bar, { 3, 7, 11, 15 }, r, 72); break;
                            default:      put (bar, { 2, 6, 10, 14 }, r, 100); if (chance (r, d)) put (bar, { 0, 4, 8, 12 }, r, 74); break;
                        }
                    }
                    break;
                }

                case Perc:
                {
                    static const std::vector<std::vector<int>> figs[NumStyles] = {
                        { { 0, 3, 6, 10, 13 }, { 2, 5, 8, 11, 14 }, { 3, 6, 9, 12, 14 }, { 0, 3, 7, 10, 13 } },
                        { { 0, 3, 6, 8, 10, 13, 14 }, { 0, 2, 3, 6, 8, 11, 13 }, { 0, 3, 6, 8, 11, 14 }, { 1, 3, 6, 9, 11, 14 } },
                        { { 3, 7, 11, 14, 15 }, { 1, 4, 7, 10, 13 }, { 2, 3, 7, 10, 11, 15 }, { 0, 5, 7, 12, 15 } },
                        { { 6, 14 }, { 3, 11 }, { 7, 15 }, { 6, 11, 14 } },
                        { { 0, 2, 4, 7, 10, 12 }, { 2, 4, 7, 10, 12, 14 }, { 0, 3, 6, 8, 11, 14 }, { 0, 2, 7, 10, 12, 15 } } };
                    const auto& choices = figs[std::clamp (s.style, 0, NumStyles - 1)];
                    const auto& f = choices[(size_t) r.below ((int) choices.size())];
                    for (int st : f)
                        bar[(size_t) st] = { vel (r, 92, 14), chance (r, 0.35f) };
                    for (int i = 0; i < 16; ++i)
                        if (bar[(size_t) i].vel == 0 && chance (r, d * 0.22f))
                            bar[(size_t) i] = { vel (r, 64, 10), chance (r, 0.3f) };
                    break;
                }

                default:   // FX
                {
                    const Role m = fxRole (s.models[Fx]);
                    if (m == RoleStab)   // stabs on syncopations
                    {
                        static const int pick[4][2] = { { 3, 10 }, { 6, 14 }, { 3, 11 }, { 7, 14 } };
                        const auto* pk = pick[r.below (4)];
                        put (bar, { pk[0] }, r, 100);
                        if (chance (r, 0.3f + d * 0.6f)) put (bar, { pk[1] }, r, 92);
                    }
                    else if (m == RoleBlip)   // blips
                    {
                        put (bar, { chance (r, 0.5f) ? 7 : 15 }, r, 90);
                        if (chance (r, d)) put (bar, { 11 }, r, 70);
                    }
                    else if (m == RoleJingle)   // tambourine: the offbeats, a few 16ths
                    {
                        put (bar, { 4, 12 }, r, 100);
                        if (s.style != Stomp) put (bar, { 2, 6, 10, 14 }, r, 72);
                        for (int i = 1; i < 16; i += 2)
                            if (chance (r, d * 0.35f)) bar[(size_t) i] = { vel (r, 56, 8), false };
                    }
                    else if (m == RoleShaker)   // maracas: 16ths, accented
                    {
                        for (int i = 0; i < 16; ++i)
                            if (i % 2 == 0 || chance (r, 0.3f + 0.6f * d))
                                bar[(size_t) i] = { vel (r, i % 4 == 2 ? 96 : (i % 2 == 0 ? 78 : 58), 8), false };
                    }
                    break;   // zap / noise: placed by vary() and fill(); the riser by generate()
                }
            }
        }

        // a later bar: the first one with small changes
        static void vary (int lane, const Settings& s, Rng& r, std::array<Step, kStepsPerBar>& bar, int barIndex)
        {
            const float var = s.variation;
            switch (lane)
            {
                case Kick:
                    if (chance (r, var * 0.35f)) put (bar, { chance (r, 0.5f) ? 15 : 7 }, r, 72);
                    break;
                case Fx:
                {
                    const Role m = fxRole (s.models[Fx]);
                    if (m == RoleSweep && barIndex % 2 == 1 && chance (r, 0.4f + 0.6f * var))
                        put (bar, { chance (r, 0.5f) ? 14 : 12 }, r, 100);
                    else if (m == RoleStab && chance (r, var * 0.5f))
                        bar[(size_t) r.below (16)] = { vel (r, 90), false };
                    else if ((m == RoleJingle || m == RoleShaker) && chance (r, var * 0.5f))
                        bar[(size_t) (1 + 2 * r.below (8))] = { vel (r, 60, 8), false };
                    break;
                }
                case Cymbal:
                    if (cymbalRole (s.models[Cymbal]) == RoleCrash) break;   // the crash stays on the one
                    [[fallthrough]];
                default:
                    for (int i = 0; i < 16; ++i)
                        if (chance (r, var * 0.12f))
                        {
                            auto& st = bar[(size_t) i];
                            // only the weak steps go; new ones only where this lane may add them
                            const bool mayAdd = lane == Perc || ((i & 1) == 1 && lane != Cymbal) || (lane == Cymbal && cymbalRole (s.models[Cymbal]) == RoleRide && i % 2 == 0);
                            if (st.vel > 0 && st.vel < 90 && i % 4 != 0) st = Step();
                            else if (st.vel == 0 && mayAdd) st = { vel (r, 60, 10), lane == Perc && chance (r, 0.4f) };
                        }
                    break;
            }
        }

        // the last bar of 2 or 4: a fill
        static void fill (int lane, const Settings& s, Rng& r, std::array<Step, kStepsPerBar>& bar)
        {
            const float var = s.variation;
            switch (lane)
            {
                case Kick:
                    if (chance (r, var * 0.5f)) { bar[12] = Step(); put (bar, { 14 }, r, 110); }
                    break;
                case Perc:
                    if (chance (r, 0.3f + 0.7f * var))
                        for (int i = 12; i < 16; ++i) bar[(size_t) i] = { vel (r, 70 + 10 * (i - 12), 6), i % 2 == 1 };
                    break;
                case OpenHat:
                    if (chance (r, var)) put (bar, { 15 }, r, 96);
                    break;
                case Clap:
                    if (chance (r, var * 0.6f)) put (bar, { 13, 14, 15 }, r, 70);
                    break;
                case Fx:
                {
                    const Role m = fxRole (s.models[Fx]);
                    if ((m == RoleSweep || m == RoleBlip) && chance (r, 0.5f + 0.5f * var)) put (bar, { 14 }, r, 104);
                    break;
                }
                default:
                    break;
            }
        }
    };
}
