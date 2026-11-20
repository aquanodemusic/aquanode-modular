#pragma once

//==============================================================================
// Drum Morpher - the analysis and the morphing (no audio thread work here).
//
// Four drum sounds sit at the corners of a square. Each is measured once:
// its onset (silent start trimmed), its loudest moment, its transient (until
// the level has fallen 6 dB from the peak), its -20 dB point and its end; its
// level envelope; and frame by frame (an STFT) its cepstral envelope - the
// spectrum's smooth shape, the "what is ringing" - apart from its fine
// structure (the partials, the "at which pitch"), plus a pitch where it has
// one (the cepstrum's peak).
//
// A point in the square blends the corners with bilinear weights. Their time
// axes are first lined up at those landmarks (onset, peak, -20 dB, end, moved
// to the weighted landmarks of the point), so a short tight kick and a long
// boomy one meet attack on attack and tail on tail. Then per frame: the
// envelopes blend, the fine structures blend after being stretched to the
// blended pitch, the phase comes from the strongest corner, the level
// envelope is pressed onto the result, and the first few ms (the transient)
// are a straight blend of the lined-up waveforms, which keeps the click crisp.
//
// An 8 x 8 grid of these is rendered in the background and kept as 16-bit
// audio; the module plays between grid points (and, by Cepstral, between that
// and a plain crossfade of the lined-up corners), so X and Y can move freely
// while it plays.
//==============================================================================
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace drummorph
{
    constexpr int kGrid = 8;
    constexpr int kCorners = 4;
    constexpr double kRate = 48000.0;      // every corner is brought to this rate
    constexpr double kMaxSeconds = 10.0;   // the first 10 s of a file (after its silent start)

    // corner order: 0 bottom-left, 1 bottom-right, 2 top-left, 3 top-right
    inline void cornerWeights (float x, float y, float w[kCorners])
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        y = juce::jlimit (0.0f, 1.0f, y);
        w[0] = (1.0f - x) * (1.0f - y);
        w[1] = x * (1.0f - y);
        w[2] = (1.0f - x) * y;
        w[3] = x * y;
    }

    //==========================================================================
    // A corner's volume curve (6.7.7), as Cove Groove's: points over the marked
    // part, 0..1 in time (start mark to end mark) and in level, joined by
    // straight lines; the two ends stay at the marks. Flat: level 1 throughout.
    struct VolumeCurve
    {
        static constexpr int kMaxPoints = 16;
        int n { 2 };
        float x[kMaxPoints] { 0.0f, 1.0f };
        float y[kMaxPoints] { 1.0f, 1.0f };

        float at (float u) const
        {
            if (n <= 0) return 1.0f;
            if (u <= x[0]) return y[0];
            for (int i = 1; i < n; ++i)
                if (u <= x[i])
                {
                    const float d = x[i] - x[i - 1];
                    return d <= 1.0e-6f ? y[i] : y[i - 1] + (y[i] - y[i - 1]) * (u - x[i - 1]) / d;
                }
            return y[n - 1];
        }
        void tidy()
        {
            n = juce::jlimit (2, kMaxPoints, n);
            for (int i = 0; i < n; ++i) { x[i] = juce::jlimit (0.0f, 1.0f, x[i]); y[i] = juce::jlimit (0.0f, 1.0f, y[i]); }
            std::sort (x, x + n);
            x[0] = 0.0f; x[n - 1] = 1.0f;
        }
        bool isFlat() const
        {
            for (int i = 0; i < n; ++i) if (y[i] < 0.999f) return false;
            return true;
        }
        juce::String toString() const
        {
            juce::String s;
            for (int i = 0; i < n; ++i) s << (i ? "," : "") << juce::String (x[i], 4) << ":" << juce::String (y[i], 4);
            return s;
        }
        bool fromString (const juce::String& s)
        {
            VolumeCurve c; c.n = 0;
            for (const auto& part : juce::StringArray::fromTokens (s, ",", ""))
            {
                if (c.n >= kMaxPoints || ! part.containsChar (':')) break;
                c.x[c.n] = part.upToFirstOccurrenceOf (":", false, false).getFloatValue();
                c.y[c.n] = part.fromFirstOccurrenceOf (":", false, false).getFloatValue();
                ++c.n;
            }
            if (c.n < 2) return false;
            c.tidy();
            *this = c;
            return true;
        }
    };

    //==========================================================================
    // What is known about one corner's sound
    struct Corner
    {
        juce::String name;
        VolumeCurve curve;                      // the volume curve over the marked part
        std::vector<float> raw;                 // mono at kRate, silent start trimmed, at most kMaxSeconds
        float startFrac { 0.0f }, endFrac { 1.0f };   // the user's start / end marks over raw

        // measured on the marked part
        std::vector<float> audio;               // the marked part
        float landmarks[4] {};                  // onset (0), peak, -20 dB, end - seconds
        float transient { 0.01f };              // seconds from onset
        std::vector<float> envDb;               // level (RMS, 5 ms frames), dB
        float peakDb { -100.0f };

        bool valid() const { return audio.size() > 64; }
        float seconds() const { return (float) audio.size() / (float) kRate; }

        static constexpr int kEnvHop = 240;     // 5 ms at kRate

        float envAt (float t) const
        {
            if (envDb.empty()) return -120.0f;
            const float f = t * (float) kRate / (float) kEnvHop;
            if (f <= 0.0f) return envDb.front();
            const int i = (int) f;
            if (i >= (int) envDb.size() - 1) return envDb.back() - 6.0f * (f - (float) (envDb.size() - 1));
            const float a = f - (float) i;
            return envDb[(size_t) i] * (1.0f - a) + envDb[(size_t) i + 1] * a;
        }

        float sampleAt (double pos) const   // pos in kRate samples, linear
        {
            if (pos < 0.0) return 0.0f;
            const auto i = (size_t) pos;
            if (i + 1 >= audio.size()) return 0.0f;
            const float a = (float) (pos - (double) i);
            return audio[i] * (1.0f - a) + audio[i + 1] * a;
        }

        // the marked part, measured (call after raw / marks change)
        void analyse()
        {
            audio.clear();
            envDb.clear();
            if (raw.size() < 64) return;
            const size_t a = (size_t) (juce::jlimit (0.0f, 1.0f, startFrac) * (float) raw.size());
            const size_t b = (size_t) (juce::jlimit (0.0f, 1.0f, endFrac) * (float) raw.size());
            if (b <= a + 64) return;
            audio.assign (raw.begin() + (long) a, raw.begin() + (long) b);

            // the volume curve over it (what is heard, morphed and measured)
            if (! curve.isFlat())
            {
                const float last = (float) juce::jmax<size_t> (1, audio.size() - 1);
                for (size_t i = 0; i < audio.size(); ++i)
                    audio[i] *= curve.at ((float) i / last);
            }

            // a 3 ms fade at the end mark, so a cut tail does not click
            const size_t fade = std::min<size_t> (audio.size() / 4, (size_t) (0.003 * kRate));
            for (size_t i = 0; i < fade; ++i)
                audio[audio.size() - 1 - i] *= (float) i / (float) fade;

            for (size_t s = 0; s < audio.size(); s += kEnvHop)
            {
                double sq = 0.0;
                const size_t e = std::min (audio.size(), s + kEnvHop);
                for (size_t i = s; i < e; ++i) sq += (double) audio[i] * audio[i];
                envDb.push_back ((float) (10.0 * std::log10 (sq / (double) (e - s) + 1.0e-12)));
            }
            size_t peakI = 0;
            for (size_t i = 0; i < envDb.size(); ++i)
                if (envDb[i] > envDb[peakI]) peakI = i;
            peakDb = envDb[peakI];
            const float frame = (float) kEnvHop / (float) kRate;
            const float tPeak = ((float) peakI + 0.5f) * frame;
            float tTrans = tPeak + 0.005f, t20 = -1.0f, tEnd = -1.0f;
            for (size_t i = peakI; i < envDb.size(); ++i)
            {
                if (envDb[i] < peakDb - 6.0f && tTrans <= tPeak + 0.005f) tTrans = (float) i * frame;
                if (envDb[i] < peakDb - 20.0f && t20 < 0.0f) t20 = (float) i * frame;
                if (envDb[i] > peakDb - 60.0f) tEnd = ((float) i + 1.0f) * frame;
            }
            const float len = seconds();
            if (t20 < 0.0f) t20 = len * 0.7f;
            if (tEnd < 0.0f) tEnd = len;
            transient = juce::jlimit (0.003f, 0.08f, tTrans);
            landmarks[0] = 0.0f;
            landmarks[1] = juce::jlimit (0.001f, len * 0.5f, tPeak);
            landmarks[2] = juce::jlimit (landmarks[1] + 0.002f, len, t20);
            landmarks[3] = juce::jlimit (landmarks[2] + 0.002f, len, tEnd);
        }
    };

    // corner time for a time on the blended landmarks (piecewise linear)
    inline float mapTime (float t, const float* blended, const float* own)
    {
        if (t <= 0.0f) return t;
        for (int s = 0; s < 3; ++s)
        {
            if (t <= blended[s + 1] || s == 2)
            {
                const float span = juce::jmax (1.0e-5f, blended[s + 1] - blended[s]);
                return own[s] + (t - blended[s]) * (own[s + 1] - own[s]) / span;
            }
        }
        return t;
    }

    inline void blendLandmarks (const Corner* const* corners, const float* w, float out[4], float& transient)
    {
        for (int k = 0; k < 4; ++k) out[k] = 0.0f;
        transient = 0.0f;
        for (int i = 0; i < kCorners; ++i)
        {
            if (w[i] <= 0.0f) continue;
            for (int k = 0; k < 4; ++k) out[k] += w[i] * corners[i]->landmarks[k];
            transient += w[i] * corners[i]->transient;
        }
    }

    //==========================================================================
    // Frame-by-frame spectra of a corner, for the morph
    struct Spectra
    {
        int fftSize { 2048 }, hop { 512 }, bins { 1025 }, frames { 0 };
        std::vector<float> env, fine, phase;   // [frame * bins + k]
        std::vector<float> f0;                 // per frame, 0 = no clear pitch

        void analyse (const Corner& c, int order, const std::atomic<bool>& stop)
        {
            fftSize = 1 << order;
            hop = fftSize / 4;
            bins = fftSize / 2 + 1;
            frames = (int) (c.audio.size() / (size_t) hop) + 2;
            env.assign ((size_t) frames * (size_t) bins, -20.0f);
            fine.assign (env.size(), 0.0f);
            phase.assign (env.size(), 0.0f);
            f0.assign ((size_t) frames, 0.0f);

            juce::dsp::FFT fft (order);
            std::vector<float> win ((size_t) fftSize), buf ((size_t) fftSize * 2), cep ((size_t) fftSize * 2);
            for (int i = 0; i < fftSize; ++i)
                win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
            const int lifter = juce::jmax (12, fftSize / 48);   // the envelope's detail
            const int qLo = (int) (kRate / 900.0), qHi = juce::jmin (fftSize / 2 - 2, (int) (kRate / 35.0));

            for (int f = 0; f < frames && ! stop.load(); ++f)
            {
                const long centre = (long) f * hop;
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < fftSize; ++i)
                {
                    const long s = centre - fftSize / 2 + i;
                    if (s >= 0 && s < (long) c.audio.size()) buf[(size_t) i] = c.audio[(size_t) s] * win[(size_t) i];
                }
                fft.performRealOnlyForwardTransform (buf.data(), true);
                float* E = &env[(size_t) f * (size_t) bins];
                float* F = &fine[(size_t) f * (size_t) bins];
                float* P = &phase[(size_t) f * (size_t) bins];
                // log magnitude, and the real cepstrum of it
                std::fill (cep.begin(), cep.end(), 0.0f);
                for (int k = 0; k < bins; ++k)
                {
                    const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
                    const float lm = 0.5f * std::log (re * re + im * im + 1.0e-14f);
                    F[k] = lm;
                    P[k] = std::atan2 (im, re);
                    cep[(size_t) (2 * k)] = lm;
                }
                fft.performRealOnlyInverseTransform (cep.data());   // cep[0..N) = the cepstrum (real)
                // pitch: the strongest cepstral peak in the drum's range
                int bestQ = 0; float bestV = 0.0f;
                for (int q = qLo; q <= qHi; ++q)
                    if (cep[(size_t) q] > bestV) { bestV = cep[(size_t) q]; bestQ = q; }
                f0[(size_t) f] = (bestV > 0.12f && bestQ > 0) ? (float) (kRate / bestQ) : 0.0f;
                // lifter: keep the low quefrencies - the envelope
                for (int q = lifter; q <= fftSize - lifter; ++q) cep[(size_t) q] = 0.0f;
                for (int q = fftSize; q < fftSize * 2; ++q) cep[(size_t) q] = 0.0f;
                fft.performRealOnlyForwardTransform (cep.data(), true);
                for (int k = 0; k < bins; ++k)
                {
                    E[k] = cep[(size_t) (2 * k)];
                    F[k] -= E[k];
                }
            }
        }

        // the frame nearest a time (s), clamped
        int frameAt (float t) const
        {
            return juce::jlimit (0, juce::jmax (0, frames - 1), (int) std::lround (t * kRate / (double) hop));
        }
    };

    //==========================================================================
    // A rendered grid point (16-bit, scaled)
    struct Cell
    {
        std::vector<int16_t> pcm;
        float scale { 0.0f };       // value of 32767
        float landmarks[4] {};
        float transient { 0.01f };
        float sampleAt (double pos) const
        {
            if (pos < 0.0) return 0.0f;
            const auto i = (size_t) pos;
            if (i + 1 >= pcm.size()) return 0.0f;
            const float a = (float) (pos - (double) i);
            return ((float) pcm[i] * (1.0f - a) + (float) pcm[i + 1] * a) * (scale / 32767.0f);
        }
        double length() const { return (double) pcm.size(); }
    };

    // Everything the audio thread plays from - made whole in the background,
    // then handed over at once
    struct Grid
    {
        std::array<Corner, kCorners> corners;   // (their analysis; raw left empty)
        std::array<Cell, kGrid * kGrid> cells;
        int fftOrder { 11 };
        bool any { false };
        size_t bytes() const
        {
            size_t b = 0;
            for (const auto& c : cells) b += c.pcm.size() * 2;
            for (const auto& c : corners) b += c.audio.size() * 4;
            return b;
        }
    };

    //==========================================================================
    // One grid point, from the corners' measures
    inline Cell renderCell (const std::array<Corner, kCorners>& corners, const std::array<Spectra, kCorners>& spec,
                            float x, float y, int order, const std::atomic<bool>& stop)
    {
        Cell cell;
        float w[kCorners];
        cornerWeights (x, y, w);
        float sum = 0.0f;
        for (int i = 0; i < kCorners; ++i) { if (! corners[(size_t) i].valid()) w[i] = 0.0f; sum += w[i]; }
        if (sum <= 0.0f) return cell;
        for (auto& v : w) v /= sum;

        const Corner* cp[kCorners] = { &corners[0], &corners[1], &corners[2], &corners[3] };
        blendLandmarks (cp, w, cell.landmarks, cell.transient);
        const float lengthS = cell.landmarks[3] + 0.02f;
        const int n = juce::jmax (64, (int) (lengthS * kRate));
        const int fftSize = 1 << order, hop = fftSize / 4, bins = fftSize / 2 + 1;

        int dominant = 0;
        for (int i = 1; i < kCorners; ++i) if (w[i] > w[dominant]) dominant = i;

        std::vector<float> out ((size_t) (n + fftSize), 0.0f);
        std::vector<float> win ((size_t) fftSize), buf ((size_t) fftSize * 2), logM ((size_t) bins);
        for (int i = 0; i < fftSize; ++i)
            win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        juce::dsp::FFT fft (order);

        const int frames = n / hop + 2;
        for (int f = 0; f < frames && ! stop.load(); ++f)
        {
            const float t = (float) (f * hop) / (float) kRate;
            std::fill (logM.begin(), logM.end(), 0.0f);
            // the pitch to meet at: the weighted (log) mean of the pitched corners
            float lf = 0.0f, wf = 0.0f;
            int fi[kCorners] {};
            for (int i = 0; i < kCorners; ++i)
            {
                if (w[i] <= 0.0f) continue;
                fi[i] = spec[(size_t) i].frameAt (mapTime (t, cell.landmarks, corners[(size_t) i].landmarks));
                const float p = spec[(size_t) i].f0[(size_t) fi[i]];
                if (p > 0.0f) { lf += w[i] * std::log (p); wf += w[i]; }
            }
            const float f0m = wf > 0.0f ? std::exp (lf / wf) : 0.0f;
            for (int i = 0; i < kCorners; ++i)
            {
                if (w[i] <= 0.0f) continue;
                const auto& s = spec[(size_t) i];
                const float* E = &s.env[(size_t) fi[i] * (size_t) bins];
                const float* F = &s.fine[(size_t) fi[i] * (size_t) bins];
                const float p = s.f0[(size_t) fi[i]];
                const float stretch = (p > 0.0f && f0m > 0.0f) ? juce::jlimit (0.5f, 2.0f, f0m / p) : 1.0f;
                for (int k = 0; k < bins; ++k)
                {
                    float fine;
                    if (stretch == 1.0f) fine = F[k];
                    else
                    {
                        const float src = (float) k / stretch;
                        const int k0 = (int) src;
                        if (k0 + 1 >= bins) fine = F[bins - 1];
                        else { const float a = src - (float) k0; fine = F[k0] * (1.0f - a) + F[k0 + 1] * a; }
                    }
                    logM[(size_t) k] += w[i] * (E[k] + fine);
                }
            }
            const auto& sd = spec[(size_t) dominant];
            const float* P = &sd.phase[(size_t) fi[dominant] * (size_t) bins];
            for (int k = 0; k < bins; ++k)
            {
                const float mag = std::exp (juce::jmin (30.0f, logM[(size_t) k]));
                buf[(size_t) (2 * k)] = mag * std::cos (P[k]);
                buf[(size_t) (2 * k + 1)] = mag * std::sin (P[k]);
            }
            fft.performRealOnlyInverseTransform (buf.data());
            const long start = (long) f * hop - fftSize / 2;
            for (int i = 0; i < fftSize; ++i)
            {
                const long o = start + i;
                if (o >= 0 && o < (long) out.size())
                    out[(size_t) o] += buf[(size_t) i] * win[(size_t) i] * (1.0f / 1.5f);   // (Hann^2 at a quarter hop sums to 1.5)
            }
        }
        out.resize ((size_t) n);

        // the blended level envelope pressed onto it, 5 ms at a time
        {
            const int hopE = Corner::kEnvHop;
            float gPrev = 1.0f;
            for (int s = 0; s < n; s += hopE)
            {
                const int e = juce::jmin (n, s + hopE);
                double sq = 0.0;
                for (int i = s; i < e; ++i) sq += (double) out[(size_t) i] * out[(size_t) i];
                const float have = (float) (10.0 * std::log10 (sq / (double) (e - s) + 1.0e-12));
                const float t = ((float) s + 0.5f * hopE) / (float) kRate;
                float want = 0.0f;
                for (int i = 0; i < kCorners; ++i)
                    if (w[i] > 0.0f) want += w[i] * corners[(size_t) i].envAt (mapTime (t, cell.landmarks, corners[(size_t) i].landmarks));
                const float g = std::pow (10.0f, juce::jlimit (-30.0f, 30.0f, want - have) / 20.0f);
                for (int i = s; i < e; ++i)
                {
                    const float a = (float) (i - s) / (float) (e - s);
                    out[(size_t) i] *= gPrev + (g - gPrev) * a;
                }
                gPrev = g;
            }
        }

        // the transient: the lined-up waveforms blended straight, handing over
        // to the morph over as long again
        {
            const float T = juce::jmax (0.003f, cell.transient);
            const int nT = juce::jmin (n, (int) (2.0f * T * (float) kRate));
            for (int i = 0; i < nT; ++i)
            {
                const float t = (float) i / (float) kRate;
                float xf = 0.0f;
                for (int c = 0; c < kCorners; ++c)
                    if (w[c] > 0.0f)
                        xf += w[c] * corners[(size_t) c].sampleAt ((double) mapTime (t, cell.landmarks, corners[(size_t) c].landmarks) * kRate);
                const float a = t < T ? 1.0f : 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (t - T) / T);
                out[(size_t) i] = a * xf + (1.0f - a) * out[(size_t) i];
            }
        }

        float peak = 0.0f;
        for (float v : out) peak = juce::jmax (peak, std::abs (v));
        cell.scale = juce::jmax (1.0e-6f, peak);
        cell.pcm.resize (out.size());
        for (size_t i = 0; i < out.size(); ++i)
            cell.pcm[i] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (out[i] / cell.scale * 32767.0f));
        return cell;
    }

    // the whole grid; progress 0..1 as it goes
    inline std::shared_ptr<Grid> renderGrid (std::array<Corner, kCorners> corners, int order,
                                             const std::atomic<bool>& stop, std::atomic<float>& progress)
    {
        auto grid = std::make_shared<Grid>();
        grid->fftOrder = order;
        for (auto& c : corners) { c.analyse(); grid->any = grid->any || c.valid(); }
        std::array<Spectra, kCorners> spec;
        for (int i = 0; i < kCorners && ! stop.load(); ++i)
        {
            if (corners[(size_t) i].valid()) spec[(size_t) i].analyse (corners[(size_t) i], order, stop);
            progress.store (0.1f * (float) (i + 1) / (float) kCorners);
        }
        for (int gy = 0; gy < kGrid && ! stop.load(); ++gy)
            for (int gx = 0; gx < kGrid && ! stop.load(); ++gx)
            {
                grid->cells[(size_t) (gy * kGrid + gx)] = renderCell (corners, spec, (float) gx / (kGrid - 1), (float) gy / (kGrid - 1), order, stop);
                progress.store (0.1f + 0.9f * (float) (gy * kGrid + gx + 1) / (float) (kGrid * kGrid));
            }
        for (auto& c : corners) c.raw.clear();
        grid->corners = std::move (corners);
        return grid;
    }

    //==========================================================================
    // A sound (mono, at its own rate) brought in as a corner: the silent start
    // cut off, the first kMaxSeconds kept, at kRate
    inline bool setCornerSound (std::vector<float> mono, double srcRate, Corner& c)
    {
        float peak = 0.0f;
        for (float v : mono) peak = juce::jmax (peak, std::abs (v));
        if (peak < 1.0e-5f || srcRate <= 0.0) return false;
        size_t first = 0;
        const float thr = juce::jmax (1.0e-4f, peak * 0.01f);   // -40 dB under its peak
        while (first < mono.size() && std::abs (mono[first]) < thr) ++first;
        first = first > (size_t) (0.001 * srcRate) ? first - (size_t) (0.001 * srcRate) : 0;   // 1 ms before it
        const size_t keepSrc = std::min (mono.size() - first, (size_t) (kMaxSeconds * srcRate));
        const double ratio = srcRate / kRate;
        const int outLen = (int) ((double) keepSrc / ratio);
        if (outLen <= 0) return false;
        c.raw.assign ((size_t) outLen, 0.0f);
        if (std::abs (ratio - 1.0) < 1.0e-9)
            std::copy (mono.begin() + (long) first, mono.begin() + (long) (first + (size_t) outLen), c.raw.begin());
        else
        {
            juce::LagrangeInterpolator interp;
            std::vector<float> src (mono.begin() + (long) first, mono.begin() + (long) (first + keepSrc));
            src.resize (src.size() + 8, 0.0f);
            interp.process (ratio, src.data(), c.raw.data(), outLen);
        }
        c.startFrac = 0.0f;
        c.endFrac = 1.0f;
        c.curve = {};   // (a new sound: flat)
        return true;
    }

    //==========================================================================
    // A sound file brought in as a corner: to mono at kRate, the silent start
    // cut off, the first kMaxSeconds kept
    inline bool loadCorner (juce::AudioFormatReader& reader, Corner& c)
    {
        if (reader.lengthInSamples <= 0 || reader.sampleRate <= 0.0) return false;
        const double srcRate = reader.sampleRate;
        // a little more than we keep: the silent start comes off first
        const auto want = (juce::int64) ((kMaxSeconds + 5.0) * srcRate);
        const int len = (int) std::min<juce::int64> (reader.lengthInSamples, want);
        const int ch = juce::jlimit (1, 2, (int) reader.numChannels);
        juce::AudioBuffer<float> b (ch, len);
        b.clear();
        reader.read (&b, 0, len, 0, true, ch > 1);
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
            mono[(size_t) i] = ch > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i);
        return setCornerSound (std::move (mono), srcRate, c);
    }
}
