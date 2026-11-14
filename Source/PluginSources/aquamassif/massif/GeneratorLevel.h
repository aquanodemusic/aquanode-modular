#pragma once
// Massif - setting a generated patch's level: one short offline render of a
// note (a fifth for pads and keys), then the master volume is set so the
// patch peaks below -6 dBFS and sits near -17 dBFS RMS.
#include "Engine.h"
#include "Generator.h"
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace massif
{
namespace generator
{
using TableFor = std::function<std::shared_ptr<const Wavetable> (int slot)>;

inline void trimLevel (Patch& p, int type, const TableFor& tableFor, float sampleRate = 44100.0f)
{
    const int note = type <= tAcidLine ? 36 : type <= tVoxLead ? 60 : 48;
    const bool chord = type >= tDeepPad && type <= tBrass;
    float k = p.knob[kMasterVol];
    Engine eng; const int bs = 256; eng.prepare (sampleRate, bs);
    eng.setPatch (p);
    for (int o = 0; o < 3; ++o) eng.setTable (o, tableFor (p.wavetable[(size_t) o]));
    std::vector<float> L ((size_t) bs), R ((size_t) bs);
    const int total = (int) (2.6f * sampleRate), off = (int) (2.0f * sampleRate);
    double sum = 0; float peak = 0; int count = 0;
    eng.noteOn (note, 100); if (chord) eng.noteOn (note + 7, 90);
    for (int pos = 0; pos < total; pos += bs)
    {
        if (pos <= off && pos + bs > off) { eng.noteOff (note); if (chord) eng.noteOff (note + 7); }
        std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
        eng.process (L.data(), R.data(), bs, (double) p.presetBpm);
        for (int i = 0; i < bs; ++i)
        {
            const float a = std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i]));
            if (! std::isfinite (a)) continue;
            peak = std::max (peak, a);
            if (pos < off) { sum += 0.5 * ((double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]); ++count; }
        }
    }
    const float rms = count > 0 ? (float) std::sqrt (sum / count) : 0.0f;
    if (peak < 1.0e-5f || rms < 1.0e-6f) return;
    const float pkT = type >= tSequence && type <= tSfx ? 0.35f : 0.5f;     // gates, hits and effects run hotter than one render shows
    const float g = std::min (pkT / peak, 0.141f / rms);            // -6 dBFS peak, -17 dBFS rms
    p.knob[kMasterVol] = std::clamp (k * std::sqrt (g), 0.25f, 1.0f);   // master gain goes with the knob squared
}
} // namespace generator
} // namespace massif
