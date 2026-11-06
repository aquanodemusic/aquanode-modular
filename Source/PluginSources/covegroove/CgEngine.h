#pragma once
//==============================================================================
// Cove Groove - the parts that play: the sequencer (when each step fires,
// with swing), the lane bank (voices, level, pan, the routing to the busses)
// and the bus (top bus phaser and band-pass, delay, master drive). The plugin
// runs all three as one Groove; the patch version gives each its own module.
//==============================================================================
#include "CgPattern.h"
#include "CgFx.h"
#include "../../Modules/Shared/DriveCore.h"

namespace covegroove
{
    //=== Sequencer ===========================================================
    // Positions are counted in 16th steps. Odd steps are pushed late by Swing:
    // 50 % is straight, 66 % a triplet shuffle, 75 % the dotted feel.
    class Sequencer
    {
    public:
        float swing { 0.6f };

        void reset() { lastEnd = -1.0e9; }

        // fires every step whose time falls inside this block; f (lane, velocity 0..1, alt, sampleOffset)
        template <typename F>
        void advance (const Pattern& p, double pos16, double inc16, int numSamples, F&& f)
        {
            if (numSamples <= 0 || inc16 <= 0.0) return;
            if (std::abs (pos16 - lastEnd) < 1.0e-6) pos16 = lastEnd;      // continuous: no step twice
            const double a = pos16, b = pos16 + inc16 * numSamples;
            lastEnd = b;
            const int L = p.length();
            const double sw = 2.0 * (double) clampf (swing, 0.5f, 0.75f) - 1.0;
            for (double loopStart = std::floor (a / L) * L - L; loopStart < b; loopStart += L)
                for (int s = 0; s < L; ++s)
                {
                    const double T = loopStart + s + ((s & 1) ? sw : 0.0);
                    if (T < a || T >= b) continue;
                    const int off = std::clamp ((int) ((T - a) / inc16), 0, numSamples - 1);
                    for (int lane = 0; lane < kNumLanes; ++lane)
                    {
                        const auto& st = p.lanes[(size_t) lane][(size_t) s];
                        if (st.vel > 0)
                            f (lane, (float) st.vel / 127.0f, st.alt, off);
                    }
                }
        }

        // the step a position is on (for the grid's playhead)
        static int stepAt (const Pattern& p, double pos16)
        {
            const int L = p.length();
            double m = std::fmod (pos16, (double) L);
            if (m < 0.0) m += L;
            return std::clamp ((int) m, 0, L - 1);
        }

    private:
        double lastEnd { -1.0e9 };
    };

    //=== Lanes ===============================================================
    struct LaneMix
    {
        float level { 0.8f };       // 0..1.5
        float pan { 0.0f };         // -1..1
        float send { 0.0f };        // to the delay, 0..1
        bool top { false };         // through the top bus (phaser, band-pass)
        bool mute { false };
    };

    struct LaneOut
    {
        float dryL { 0.0f }, dryR { 0.0f }, topL { 0.0f }, topR { 0.0f }, sendL { 0.0f }, sendR { 0.0f };
        void clear() { *this = LaneOut(); }
    };

    class LaneBank
    {
    public:
        static constexpr int kVoices = 3;
        std::array<Sound, kNumLanes> sound {};
        std::array<LaneMix, kNumLanes> mix {};

        void prepare (float sampleRate)
        {
            srCache = sampleRate;
            for (auto& lane : voices) for (auto& v : lane) v.prepare (sampleRate);
        }
        void reset() { for (auto& lane : voices) for (auto& v : lane) v.prepare (sampleRateOf()); }

        void trigger (int lane, float vel, bool alt)
        {
            lane = std::clamp (lane, 0, kNumLanes - 1);
            if (mix[(size_t) lane].mute) return;
            auto& vs = voices[(size_t) lane];
            // mono lanes and the hats let the previous hit go quickly; a closed hat chokes the open one
            const bool mono = lane == Kick || lane == Bass || lane == OpenHat || lane == ClosedHat;
            if (mono) for (auto& v : vs) v.fadeOut (lane == OpenHat ? 0.012f : 0.004f);
            if (lane == ClosedHat) for (auto& v : voices[OpenHat]) v.fadeOut (0.010f);
            auto& v = vs[(size_t) next[(size_t) lane]];
            next[(size_t) lane] = (next[(size_t) lane] + 1) % kVoices;
            v.start (lane, sound[(size_t) lane], vel, alt, ++seed);
        }

        // one sample of every lane into the busses
        void render (LaneOut& o)
        {
            o.clear();
            for (int lane = 0; lane < kNumLanes; ++lane)
            {
                float l = 0.0f, r = 0.0f;
                for (auto& v : voices[(size_t) lane])
                {
                    if (! v.isActive()) continue;
                    float a, b;
                    v.render (a, b);
                    l += a; r += b;
                }
                if (l == 0.0f && r == 0.0f) continue;
                const auto& m = mix[(size_t) lane];
                float gl, gr;
                panGains (m.pan, gl, gr);
                l *= m.level * gl; r *= m.level * gr;
                if (m.top) { o.topL += l; o.topR += r; } else { o.dryL += l; o.dryR += r; }
                o.sendL += l * m.send; o.sendR += r * m.send;
            }
        }

        // one lane alone (the patch version's Drum module)
        void renderLane (int lane, float& l, float& r)
        {
            l = r = 0.0f;
            for (auto& v : voices[(size_t) std::clamp (lane, 0, kNumLanes - 1)])
            {
                if (! v.isActive()) continue;
                float a, b;
                v.render (a, b);
                l += a; r += b;
            }
        }

        void chokeLane (int lane, float seconds) { for (auto& v : voices[(size_t) std::clamp (lane, 0, kNumLanes - 1)]) v.fadeOut (seconds); }

    private:
        float sampleRateOf() const { return srCache; }
        std::array<std::array<DrumVoice, kVoices>, kNumLanes> voices {};
        std::array<int, kNumLanes> next {};
        uint32_t seed { 1 };
        float srCache { 48000.0f };
    };

    //=== Bus =================================================================
    class Bus
    {
    public:
        Phaser phaser;
        MotionFilter filter;
        TempoDelay delay;
        aquanode::drive::Settings drive;
        float filterRateBars { 2.0f };      // one sweep every this many bars
        float volume { 0.8f };

        void prepare (float sampleRate)
        {
            phaser.prepare (sampleRate);
            filter.prepare (sampleRate);
            delay.prepare (sampleRate);
            drv.prepare (sampleRate);
        }
        void reset() { phaser.reset(); filter.reset(); delay.reset(); drv.reset(); }

        // pos16: where the groove is (for the filter's sweep), bpm for the delay
        void process (const LaneOut& in, double pos16, double bpm, float& outL, float& outR)
        {
            float tl = in.topL, tr = in.topR;
            phaser.process (tl, tr);
            float el, er;
            delay.process (in.sendL, in.sendR, el, er, bpm);
            tl += el; tr += er;
            filter.process (tl, tr, pos16 / (16.0 * std::max (0.25f, filterRateBars)));
            float l = in.dryL + tl, r = in.dryR + tr;
            drv.process (l, r, drive);
            outL = l * volume;
            outR = r * volume;
        }

    private:
        aquanode::drive::Stereo drv;
    };

    //=== the whole groove (the plugin) ========================================
    class Groove
    {
    public:
        Sequencer seq;
        LaneBank lanes;
        Bus bus;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            lanes.prepare ((float) sampleRate);
            bus.prepare ((float) sampleRate);
            seq.reset();
        }

        void reset() { lanes.reset(); bus.reset(); seq.reset(); }

        // renders a block; pos16 is where the block starts (16th steps), running
        // whether steps fire at all (stopped, the voices ring out)
        void process (const Pattern& p, float* L, float* R, int n, double pos16, double bpm, bool running)
        {
            const double inc16 = bpm / 60.0 * 4.0 / sr;
            int fireAt[256]; int fireLane[256]; float fireVel[256]; bool fireAlt[256];
            int numFire = 0;
            if (running)
                seq.advance (p, pos16, inc16, n, [&] (int lane, float v, bool alt, int off)
                {
                    if (numFire < 256) { fireAt[numFire] = off; fireLane[numFire] = lane; fireVel[numFire] = v; fireAlt[numFire] = alt; ++numFire; }
                });
            else
                seq.reset();
            int k = 0;
            LaneOut o;
            for (int i = 0; i < n; ++i)
            {
                while (k < numFire && fireAt[k] <= i) { lanes.trigger (fireLane[k], fireVel[k], fireAlt[k]); ++k; }
                lanes.render (o);
                float l, r;
                bus.process (o, pos16 + inc16 * i, bpm, l, r);
                L[i] = l; R[i] = r;
            }
        }

    private:
        double sr { 48000.0 };
    };
}
