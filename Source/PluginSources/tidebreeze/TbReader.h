#pragma once
//==============================================================================
// Tidebreeze - playing a wave at any pitch
//
// The reader keeps its position in level-0 samples and reads the mip level
// whose rate suits the current pitch (above the root it steps down a level
// per octave, so what it reads is always band-limited), with 4-point cubic
// interpolation. It loops between the wave's loop points; reversed, it plays
// the whole wave backwards once and then loops its sustain backwards (the
// way a reversed multisample sounds: the loop first, the attack last, then
// the loop again).
//==============================================================================
#include "TbWaves.h"

namespace tb
{
    struct WaveReader
    {
        WavePtr wave;
        const Zone* zone = nullptr;
        double pos = 0.0;          // level-0 samples
        bool reverse = false;
        bool done = true;
        bool looping = false;      // reversed: past the first pass

        // the zone for the note played; rootHz() is that zone's root
        void start (WavePtr w, float note, bool startOffset, bool reversed)
        {
            wave = std::move (w);
            reverse = reversed;
            zone = wave != nullptr && ! wave->zones.empty() ? &wave->zoneFor (note) : nullptr;
            done = zone == nullptr || zone->length() < 4;
            if (done) return;
            looping = false;
            if (reverse)
            {
                pos = (double) zone->length() - 2.0;
                looping = startOffset && ! wave->oneShot;   // offset: straight into the reversed loop
            }
            else
                pos = startOffset && ! wave->oneShot ? (double) zone->loopStart : 0.0;
        }
        float rootHz() const { return zone != nullptr ? zone->rootHz : 261.63f; }
        double rate() const { return wave != nullptr ? wave->renderRate : 44100.0; }

        // inc: level-0 samples per output sample
        inline float next (double inc)
        {
            if (done) return 0.0f;
            const auto& w = *zone;
            const bool oneShot = wave->oneShot;
            int level = 0;
            if (inc > 1.0)
            {
                level = (int) std::ceil (std::log2 (inc) - 1.0e-6);
                level = juce::jlimit (0, (int) w.levels.size() - 1, level);
            }
            const auto& buf = w.levels[(size_t) level];
            const double scale = 1.0 / (double) (1 << level);
            const int n = (int) buf.size();
            const double p = pos * scale;
            const int i1 = (int) p;
            const float f = (float) (p - i1);
            auto at = [&] (int i)
            {
                if (i < 0) i = 0;
                if (i >= n)
                {
                    const int ls = w.loopStart >> level, le = std::min (n, w.loopEnd >> level);
                    if (oneShot || le - ls < 2) return 0.0f;
                    i = ls + (i - le) % (le - ls);
                }
                return buf[(size_t) i];
            };
            const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float out = ((c3 * f + c2) * f + c1) * f + y1;

            if (! reverse)
            {
                pos += inc;
                if (oneShot)
                {
                    if (pos >= (double) w.length() - 1.0) done = true;
                }
                else if (pos >= (double) w.loopEnd)
                {
                    const double len = (double) (w.loopEnd - w.loopStart);
                    pos -= len * std::floor ((pos - (double) w.loopStart) / len);
                }
            }
            else
            {
                pos -= inc;
                if (oneShot)
                {
                    if (pos <= 0.0) done = true;
                }
                else if (! looping)
                {
                    // the first pass runs back through the attack; then the
                    // sustain loops backwards
                    if (pos <= 0.0) { looping = true; pos = (double) w.loopEnd - 1.0; }
                }
                else if (pos < (double) w.loopStart)
                    pos += (double) (w.loopEnd - w.loopStart);
            }
            return out;
        }
    };
}
