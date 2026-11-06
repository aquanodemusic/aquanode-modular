#include "TbWaves.h"

namespace tb
{
namespace
{
    //==========================================================================
    // a plain radix-2 complex FFT (the same numbers on every platform, unlike
    // the platform FFT engines, so a seed renders the same wave everywhere)
    //==========================================================================
    struct Fft
    {
        int n, logN;
        std::vector<float> cosT, sinT;
        std::vector<int> rev;
        explicit Fft (int order) : n (1 << order), logN (order)
        {
            cosT.resize ((size_t) n / 2); sinT.resize ((size_t) n / 2);
            for (int i = 0; i < n / 2; ++i)
            {
                cosT[(size_t) i] = (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / n);
                sinT[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * i / n);
            }
            rev.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                int r = 0;
                for (int b = 0; b < logN; ++b) r |= ((i >> b) & 1) << (logN - 1 - b);
                rev[(size_t) i] = r;
            }
        }
        // in place, inverse (exp +i), no scaling
        void inverse (std::vector<float>& re, std::vector<float>& im) const
        {
            for (int i = 0; i < n; ++i)
                if (rev[(size_t) i] > i) { std::swap (re[(size_t) i], re[(size_t) rev[(size_t) i]]); std::swap (im[(size_t) i], im[(size_t) rev[(size_t) i]]); }
            for (int size = 2; size <= n; size <<= 1)
            {
                const int half = size / 2, step = n / size;
                for (int start = 0; start < n; start += size)
                    for (int k = 0; k < half; ++k)
                    {
                        const float c = cosT[(size_t) (k * step)], s = sinT[(size_t) (k * step)];
                        const int a = start + k, b = a + half;
                        const float tr = re[(size_t) b] * c - im[(size_t) b] * s;
                        const float ti = re[(size_t) b] * s + im[(size_t) b] * c;
                        re[(size_t) b] = re[(size_t) a] - tr; im[(size_t) b] = im[(size_t) a] - ti;
                        re[(size_t) a] += tr; im[(size_t) a] += ti;
                    }
            }
        }
    };

    float smoothstep (float x) { x = clamp01 (x); return x * x * (3.0f - 2.0f * x); }

    // periodic shape 0..1 for sweeps: 0 triangle, 1 saw up, 2 saw down, 3 sine
    float shapeAt (int shape, float ph)
    {
        ph -= std::floor (ph);
        switch (shape)
        {
            case 1:  return ph;
            case 2:  return 1.0f - ph;
            case 3:  return 0.5f - 0.5f * std::cos (kTwoPi * ph);
            default: return ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f;
        }
    }

    // a rate fitted to whole cycles per loop, so the motion repeats with it
    float fitRate (float hz, float loopSec)
    {
        if (hz <= 0.0f || loopSec <= 0.0f) return 0.0f;
        return std::max (1.0f, std::round (hz * loopSec)) / loopSec;
    }

    struct SpectralShape
    {
        const WaveSpec& sp;
        // morph position (0 = vowel A, 1 = vowel B) and the sweep phases at time t
        float morph = 0.0f;
        float sweepCentre[2] = { 0, 0 };

        void at (float t)
        {
            const float tl = (t - sp.attackSec) / std::max (0.05f, sp.loopSec);
            morph = sp.formMorphCycles > 0.0f ? 0.5f - 0.5f * std::cos (kTwoPi * sp.formMorphCycles * tl) : 0.0f;
            for (int i = 0; i < sp.numSweeps; ++i)
            {
                const auto& s = sp.sweep[i];
                const float p = shapeAt (s.shape, s.cycles * tl + s.phase);
                sweepCentre[i] = s.fromHz * std::pow (s.toHz / std::max (1.0f, s.fromHz), p);
            }
        }
        float formantDb (float f) const
        {
            if (sp.numFormants <= 0) return 0.0f;
            float lin = dbToGain (sp.formFloorDb);
            for (int i = 0; i < sp.numFormants; ++i)
            {
                const float hz = sp.formHz[i] + (sp.formBHz[i] - sp.formHz[i]) * morph * (sp.formBHz[i] > 0 ? 1.0f : 0.0f);
                const float bw = std::max (20.0f, sp.formBw[i] + (sp.formBBw[i] - sp.formBw[i]) * morph * (sp.formBBw[i] > 0 ? 1.0f : 0.0f));
                const float g = sp.formDb[i] + (sp.formBDb[i] - sp.formDb[i]) * morph * (sp.formBHz[i] > 0 ? 1.0f : 0.0f);
                const float x = (f - hz) / (bw * 0.5f);
                lin += dbToGain (g) / (1.0f + x * x);
            }
            return gainToDb (lin);
        }
        float sweepDb (float f) const
        {
            float db = 0.0f;
            for (int i = 0; i < sp.numSweeps; ++i)
            {
                const float o = std::log2 (std::max (1.0f, f) / std::max (1.0f, sweepCentre[i])) / std::max (0.02f, sp.sweep[i].widthOct);
                db += sp.sweep[i].gainDb * std::exp (-0.5f * o * o);
            }
            return db;
        }
    };

    float baseHarmDb (const WaveSpec& sp, int k)
    {
        float db = k <= 32 ? sp.harm[k - 1] : sp.harm[31] + sp.tiltDbOct * std::log2 ((float) k / 32.0f);
        if ((k & 1) == 0) db += sp.evenDb;
        return db;
    }

    // smooth random wander, periodic over the loop: three sines at whole cycles
    struct Drift
    {
        float rate[3] {}, ph[3] {}, amp[3] {};
        void init (Rng& r, float rateHz, float loopSec)
        {
            for (int i = 0; i < 3; ++i)
            {
                rate[i] = fitRate (rateHz * (0.6f + 0.5f * (float) i) * r.uni (0.8f, 1.25f), loopSec);
                ph[i] = r.uni(); amp[i] = r.uni (0.5f, 1.0f);
            }
        }
        float at (float t) const
        {
            float v = 0.0f, n = 0.0f;
            for (int i = 0; i < 3; ++i) { v += amp[i] * std::sin (kTwoPi * (rate[i] * t + ph[i])); n += amp[i]; }
            return n > 0.0f ? v / n : 0.0f;
        }
    };

    //==========================================================================
    // halfband decimation for the mip levels, wrapping around the loop
    //==========================================================================
    // a 31-tap windowed-sinc lowpass at a quarter of the rate (half the new
    // Nyquist), then every other sample
    const std::vector<float>& halfbandTaps()
    {
        static const std::vector<float> taps = []
        {
            std::vector<float> t (31);
            double sum = 0.0;
            for (int i = 0; i < 31; ++i)
            {
                const double x = i - 15;
                const double sinc = x == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * 0.5 * x) / (juce::MathConstants<double>::pi * 0.5 * x);
                const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / 30.0) + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * i / 30.0);
                t[(size_t) i] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (auto& v : t) v = (float) (v / sum);
            return t;
        }();
        return taps;
    }

    std::vector<float> halve (const std::vector<float>& x, int loopStart, int loopEnd, bool looped)
    {
        const auto& taps = halfbandTaps();
        const int n = (int) x.size();
        auto at = [&] (int i)
        {
            if (i < 0) return 0.0f;
            if (i >= n)
            {
                if (! looped || loopEnd <= loopStart) return 0.0f;
                i = loopStart + (i - n) % (loopEnd - loopStart);
            }
            return x[(size_t) i];
        };
        std::vector<float> y ((size_t) ((n + 1) / 2));
        for (int o = 0; o < (int) y.size(); ++o)
        {
            const int c = o * 2;
            float acc = 0.0f;
            for (int k = 0; k < 31; ++k)
                acc += taps[(size_t) k] * at (c + k - 15);
            y[(size_t) o] = acc;
        }
        return y;
    }
}

//==============================================================================
static Zone renderZone (const WaveSpec& sp, double rate)
{
    const double sr = rate;
    const float f0 = sp.rootHz;
    const int nAttack = std::max (1, (int) std::lround (sp.attackSec * sr));
    const int nLoop = std::max (64, (int) std::lround (sp.loopSec * sr));
    const int N = nAttack + nLoop;
    const float loopSec = (float) nLoop / (float) sr;
    Rng rng (sp.seed);

    std::vector<float> harmBuf ((size_t) N, 0.0f), noiseBuf ((size_t) N, 0.0f), eventBuf ((size_t) N, 0.0f);

    // ---- pitch motion, common to the harmonic part's copies -------------------
    Drift drift; drift.init (rng, sp.driftRateHz, loopSec);
    const float vibRate = fitRate (sp.vibRateHz, loopSec);
    auto pitchCents = [&] (float t)
    {
        float c = 0.0f;
        if (sp.vibCents != 0.0f && vibRate > 0.0f) c += sp.vibCents * std::sin (kTwoPi * vibRate * (t - sp.attackSec));
        if (sp.driftCents != 0.0f) c += sp.driftCents * drift.at (t);
        if (sp.scoopCents != 0.0f) c += sp.scoopCents * std::exp (-t / std::max (0.005f, sp.scoopSec * 0.33f));
        return c;
    };

    const int copies = juce::jlimit (1, 5, sp.copies);
    float copyCents[5] {}, copyPhase[5] {};
    for (int c = 0; c < copies; ++c)
    {
        copyCents[c] = copies == 1 ? 0.0f : sp.detuneCents * (-1.0f + 2.0f * (float) c / (float) (copies - 1));
        copyPhase[c] = rng.uni();
    }
    float maxRatio = 1.0f;
    for (int c = 0; c < copies; ++c) maxRatio = std::max (maxRatio, std::exp2 ((copyCents[c] + std::abs (sp.vibCents) + std::abs (sp.driftCents) + std::max (0.0f, sp.scoopCents)) / 1200.0f));

    SpectralShape shape { sp };

    // ---- harmonic part ---------------------------------------------------------
    if (sp.harmDb > -90.0f && ! sp.eventsReplaceTone)
    {
        const float nyq = 0.46f * (float) sr;
        if (sp.stretch == 0.0f)
        {
            // an evolving wavetable: a frame of the spectrum every hop samples,
            // one table per frame (inverse FFT), read by each copy
            constexpr int order = 11, T = 1 << order, hop = 512;
            const int K = std::max (1, std::min ({ sp.maxPartials, T / 2 - 1, (int) (nyq / (f0 * maxRatio)) }));
            std::vector<float> phases ((size_t) K + 1);
            for (auto& p : phases) p = rng.uni() * kTwoPi;
            Fft fft (order);
            std::vector<float> re (T), im (T), frameA (T + 1), frameB (T + 1);
            auto makeFrame = [&] (int j, std::vector<float>& out)
            {
                const float t = (float) (j * hop) / (float) sr;
                shape.at (t);
                std::fill (re.begin(), re.end(), 0.0f); std::fill (im.begin(), im.end(), 0.0f);
                for (int k = 1; k <= K; ++k)
                {
                    const float f = f0 * (float) k;
                    const float db = baseHarmDb (sp, k) + shape.formantDb (f) + shape.sweepDb (f);
                    if (db < -100.0f) continue;
                    const float a = dbToGain (db) * 0.5f;
                    re[(size_t) k] = a * std::cos (phases[(size_t) k]); im[(size_t) k] = a * std::sin (phases[(size_t) k]);
                    re[(size_t) (T - k)] = re[(size_t) k]; im[(size_t) (T - k)] = -im[(size_t) k];
                }
                fft.inverse (re, im);
                for (int i = 0; i < T; ++i) out[(size_t) i] = re[(size_t) i];
                out[T] = out[0];
            };
            int frame = 0;
            makeFrame (0, frameA); makeFrame (1, frameB);
            double ph[5], inc[5] {};
            for (int c = 0; c < copies; ++c) ph[c] = copyPhase[c] * T;
            const double baseInc = (double) f0 * T / sr;
            for (int n = 0; n < N; ++n)
            {
                if (n >= (frame + 1) * hop)
                {
                    ++frame; std::swap (frameA, frameB); makeFrame (frame + 1, frameB);
                }
                const float fr = (float) (n - frame * hop) / (float) hop;
                if ((n & 31) == 0)
                {
                    // the pitch motion, every 32 samples
                    const float pc = pitchCents ((float) n / (float) sr);
                    for (int c = 0; c < copies; ++c) inc[c] = baseInc * std::exp2 ((copyCents[c] + pc) / 1200.0);
                }
                float sum = 0.0f;
                for (int c = 0; c < copies; ++c)
                {
                    const int i1 = (int) ph[c];
                    const float x = (float) (ph[c] - i1);
                    const int i0 = (i1 - 1) & (T - 1), i2 = (i1 + 1) & (T - 1), i3 = (i1 + 2) & (T - 1);
                    auto cubic = [x] (float y0, float y1, float y2, float y3)
                    {
                        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                        return ((c3 * x + c2) * x + c1) * x + y1;
                    };
                    const float a = cubic (frameA[(size_t) i0], frameA[(size_t) i1], frameA[(size_t) i2], frameA[(size_t) i3]);
                    const float b = cubic (frameB[(size_t) i0], frameB[(size_t) i1], frameB[(size_t) i2], frameB[(size_t) i3]);
                    sum += a + fr * (b - a);
                    ph[c] += inc[c];
                    while (ph[c] >= T) ph[c] -= T;
                }
                harmBuf[(size_t) n] = sum / std::sqrt ((float) copies);
            }
        }
        else
        {
            // inharmonic partials (bells, bars): one oscillator per partial
            struct Partial { float f, phase, db; };
            std::vector<Partial> partials;
            for (int k = 1; k <= std::min (sp.maxPartials, 64); ++k)
            {
                const float f = f0 * (float) k * std::sqrt (1.0f + sp.stretch * (float) (k * k));
                if (f * maxRatio > nyq) break;
                const float db = baseHarmDb (sp, k);
                if (db < -90.0f) continue;
                partials.push_back ({ f, rng.uni() * kTwoPi, db });
            }
            const int hop = 256;
            std::vector<float> amp (partials.size());
            for (int c = 0; c < copies; ++c)
            {
                std::vector<double> phs (partials.size());
                for (size_t p = 0; p < partials.size(); ++p) phs[p] = partials[p].phase + copyPhase[c] * kTwoPi;
                for (int n0 = 0; n0 < N; n0 += hop)
                {
                    const float t = (float) n0 / (float) sr;
                    shape.at (t);
                    const float cents = copyCents[c] + pitchCents (t);
                    const double ratio = std::exp2 (cents / 1200.0);
                    for (size_t p = 0; p < partials.size(); ++p)
                        amp[p] = dbToGain (partials[p].db + shape.formantDb (partials[p].f) + shape.sweepDb (partials[p].f));
                    const int n1 = std::min (N, n0 + hop);
                    for (size_t p = 0; p < partials.size(); ++p)
                    {
                        const double inc = 2.0 * juce::MathConstants<double>::pi * partials[p].f * ratio / sr;
                        double phx = phs[p];
                        const float a = amp[p];
                        for (int n = n0; n < n1; ++n) { harmBuf[(size_t) n] += a * (float) std::sin (phx); phx += inc; }
                        phs[p] = std::fmod (phx, 2.0 * juce::MathConstants<double>::pi);
                    }
                }
            }
            for (auto& v : harmBuf) v /= std::sqrt ((float) copies);
        }

        // tremolo of the harmonic part
        if (sp.harmAmDb != 0.0f && sp.harmAmRateHz > 0.0f)
        {
            const float r = fitRate (sp.harmAmRateHz, loopSec);
            for (int n = 0; n < N; ++n)
            {
                const float t = (float) n / (float) sr - sp.attackSec;
                harmBuf[(size_t) n] *= dbToGain (-sp.harmAmDb * (0.5f - 0.5f * std::cos (kTwoPi * r * t)));
            }
        }
    }

    auto rms = [] (const std::vector<float>& b, int from, int to)
    {
        double s = 0.0; for (int i = from; i < to; ++i) s += (double) b[(size_t) i] * b[(size_t) i];
        return (float) std::sqrt (s / std::max (1, to - from));
    };
    const float harmRms = std::max (1.0e-6f, rms (harmBuf, nAttack, N));
    const bool hasHarm = sp.harmDb > -90.0f && ! sp.eventsReplaceTone;

    // ---- breath: random-phase spectral synthesis, overlap-added -------------------
    if (sp.noiseDb > -90.0f || sp.attackNoiseDb > -90.0f)
    {
        constexpr int order = 11, T = 1 << order, hop = T / 4;
        Fft fft (order);
        std::vector<float> re (T), im (T), win (T);
        for (int i = 0; i < T; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) T);
        std::vector<float> mag ((size_t) T / 2);
        for (int start = -T; start < N; start += hop)
        {
            const float t = (float) (start + T / 2) / (float) sr;
            shape.at (std::max (0.0f, t));
            for (int b = 1; b < T / 2; ++b)
            {
                const float f = (float) b * (float) sr / (float) T;
                float db = sp.noiseTiltDbOct * std::log2 (f / 1000.0f);
                if (f < sp.noiseLowHz) db -= 24.0f * std::log2 (sp.noiseLowHz / f);
                if (f > sp.noiseHighHz) db -= 24.0f * std::log2 (f / sp.noiseHighHz);
                if (sp.noiseFormants) db += shape.formantDb (f);
                if (sp.noiseSweeps) db += shape.sweepDb (f);
                float lin = dbToGain (db);
                if (sp.noiseFocus > 0.0f)
                {
                    const float k = std::max (1.0f, std::round (f / f0));
                    const float d = (f - k * f0) / (std::max (0.005f, sp.noiseFocusBw) * f0);
                    lin *= (1.0f - sp.noiseFocus) + sp.noiseFocus * 3.0f * std::exp (-0.5f * d * d);
                }
                mag[(size_t) b] = lin;
            }
            std::fill (re.begin(), re.end(), 0.0f); std::fill (im.begin(), im.end(), 0.0f);
            for (int b = 1; b < T / 2; ++b)
            {
                const float p = rng.uni() * kTwoPi;
                re[(size_t) b] = mag[(size_t) b] * std::cos (p); im[(size_t) b] = mag[(size_t) b] * std::sin (p);
                re[(size_t) (T - b)] = re[(size_t) b]; im[(size_t) (T - b)] = -im[(size_t) b];
            }
            fft.inverse (re, im);
            for (int i = 0; i < T; ++i)
            {
                const int n = start + i;
                if (n >= 0 && n < N) noiseBuf[(size_t) n] += re[(size_t) i] * win[(size_t) i];
            }
        }
        const float nr = std::max (1.0e-9f, rms (noiseBuf, nAttack, N));
        const float ref = hasHarm ? harmRms : 0.1f;
        const float base = sp.noiseDb > -90.0f ? dbToGain (sp.noiseDb) * ref / nr : 0.0f;
        const float burst = sp.attackNoiseDb > -90.0f ? dbToGain (sp.attackNoiseDb) * ref / nr : 0.0f;
        const float amR = fitRate (sp.noiseAmRateHz, loopSec);
        for (int n = 0; n < N; ++n)
        {
            const float t = (float) n / (float) sr;
            float g = base;
            if (amR > 0.0f && sp.noiseAmDb != 0.0f) g *= dbToGain (-sp.noiseAmDb * (0.5f - 0.5f * std::cos (kTwoPi * amR * (t - sp.attackSec))));
            if (burst > 0.0f) g += burst * std::exp (-t / std::max (0.005f, sp.attackNoiseSec));
            noiseBuf[(size_t) n] *= g;
        }
    }

    // ---- events ---------------------------------------------------------------------
    if (sp.eventType == 1)
    {
        // a click train, repeating with the loop: clicks per loop a multiple of 8
        const int perLoop = std::max (8, (int) std::lround (sp.eventRateHz * loopSec / 8.0f) * 8);
        const float period = loopSec / (float) perLoop;
        const float fc = f0 * sp.clickPitchMul;
        const float tau = std::max (0.002f, sp.eventDecaySec);
        const float gate = sp.clickGateSec > 0.0f ? sp.clickGateSec : tau * 7.0f;
        const int clickLen = std::min (N, (int) (gate * (float) sr));
        float amps[12];
        for (int m = 0; m < 12; ++m) amps[m] = (fc * (float) (m + 1) < 0.45f * (float) sr) ? dbToGain (sp.clickHarm[m]) : 0.0f;
        const float tickGain = sp.clickNoiseDb > -90.0f ? dbToGain (sp.clickNoiseDb) : 0.0f;
        const float thumpGain = sp.clickThumpDb > -90.0f ? dbToGain (sp.clickThumpDb) : 0.0f;
        int step = 0;
        for (double tc = sp.attackSec - std::floor (sp.attackSec / period) * period; tc * sr < N; tc += period, ++step)
        {
            const int s0 = (int) std::lround (tc * sr);
            const bool accent = ((sp.eventAccents >> (step & 7)) & 1) != 0;
            const float g = (accent ? 1.0f : 0.5f) * (0.85f + 0.15f * rng.uni());
            float phs[12]; for (int m = 0; m < 12; ++m) phs[m] = rng.uni() * kTwoPi;
            for (int i = 0; i < clickLen && s0 + i < N; ++i)
            {
                const float t = (float) i / (float) sr;
                float e = std::exp (-t / tau) * std::min (1.0f, t / 0.0008f);
                if (sp.clickGateSec > 0.0f) e *= std::min (1.0f, (gate - t) / 0.003f);
                float s = 0.0f;
                for (int m = 0; m < 12; ++m) if (amps[m] > 0.0f) s += amps[m] * std::sin (kTwoPi * fc * (float) (m + 1) * t + phs[m]);
                if (tickGain > 0.0f) s += tickGain * rng.bi() * std::exp (-t / 0.004f) / std::max (0.05f, e);
                if (thumpGain > 0.0f) s += thumpGain * std::sin (kTwoPi * fc * sp.clickThumpMul * t) * std::exp (-t / 0.03f) / std::max (0.05f, e);
                eventBuf[(size_t) (s0 + i)] += g * e * s;
            }
        }
    }
    else if (sp.eventType == 2)
    {
        // bubbles: damped sines that sweep upward as they collapse (Minnaert),
        // at random times; on partials of the note when bubbleHarmonic
        const float rateHz = std::max (0.2f, sp.eventRateHz);
        double tb = 0.0;
        while (true)
        {
            tb += -std::log (std::max (1.0e-6f, rng.uni())) / rateHz;
            const int s0 = (int) (tb * sr);
            if (s0 >= N) break;
            float f;
            if (sp.bubbleHarmonic)
            {
                const int k = rng.between ((int) std::max (1.0f, std::round (sp.bubbleLowMul)), (int) std::max (1.0f, std::round (sp.bubbleHighMul)));
                f = f0 * (float) k;
            }
            else
                f = f0 * rng.lg (sp.bubbleLowMul, std::max (sp.bubbleLowMul * 1.01f, sp.bubbleHighMul));
            if (f > 0.4f * (float) sr) continue;
            const float tau = juce::jlimit (0.003f, 0.6f, sp.eventDecaySec * std::sqrt (f0 * 4.0f / f));
            const float chirp = sp.bubbleChirp * rng.uni (0.5f, 1.5f);
            const float g = rng.uni (0.25f, 1.0f);
            const int len = std::min (N - s0, (int) (tau * 6.0f * (float) sr));
            float ph = 0.0f;
            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / (float) sr;
                const float fi = f * (1.0f + chirp * (1.0f - std::exp (-t / tau)));
                ph += kTwoPi * fi / (float) sr;
                eventBuf[(size_t) (s0 + i)] += g * std::exp (-t / tau) * std::min (1.0f, t / 0.0005f) * std::sin (ph);
            }
        }
    }
    else if (sp.eventType == 3)
    {
        // click trains: the bursts sit inside the loop (clear of its crossfade),
        // each with a creaking start (pulses twice as far apart), a glide and a
        // fade; pulses jitter and now and then drop out
        const float ratio = f0 / 261.63f;
        const float tone0 = sp.trainToneHz * std::pow (ratio, sp.trainToneTrack);
        const float rate0 = sp.eventRateHz * std::pow (ratio, sp.trainRateTrack);
        const float pulseTau = std::max (0.0015f, sp.eventDecaySec);
        const float pulseGate = sp.clickGateSec > 0.0f ? sp.clickGateSec : pulseTau * 4.0f;
        const int count = std::max (1, sp.trainCount);
        const float xfade = std::max (0.0f, sp.xfadeSec);
        const float slot = (loopSec - xfade) / (float) count;
        for (int b = 0; b < count; ++b)
        {
            const float len = std::min (sp.trainSec * rng.uni (0.7f, 1.3f), slot * 0.9f);
            const float startT = sp.attackSec + (float) b * slot + rng.uni (0.0f, std::max (0.0f, slot - len));
            const float glide = sp.trainGlideOct * rng.bi();
            const float level = rng.uni (0.6f, 1.0f);
            float t = 0.0f;
            while (t < len)
            {
                const float u = t / len;
                const float creak = 1.0f + std::exp (-t / 0.12f);          // twice as slow at the start
                const float interval = creak / rate0 * rng.uni (0.85f, 1.15f);
                const float env = std::min (1.0f, t / 0.04f) * (u > 0.7f ? std::exp (-(u - 0.7f) * 9.0f) : 1.0f);
                if (! rng.chance (0.08f))
                {
                    const float tone = tone0 * std::exp2 (glide * (u * u * (3.0f - 2.0f * u)) + 0.04f * rng.bi());
                    const int s0 = (int) std::lround ((startT + t) * sr);
                    const int plen = (int) (pulseGate * (float) sr);
                    const float g = level * env * rng.uni (0.7f, 1.0f);
                    float phs[12]; for (int m = 0; m < 12; ++m) phs[m] = rng.uni() * kTwoPi;
                    for (int i = 0; i < plen && s0 + i < N; ++i)
                    {
                        const float ti = (float) i / (float) sr;
                        const float e = std::exp (-ti / pulseTau) * std::min (1.0f, ti / 0.0006f) * std::min (1.0f, (pulseGate - ti) / 0.002f);
                        float v = 0.0f;
                        for (int m = 0; m < 12; ++m)
                        {
                            const float f = tone * (float) (m + 1);
                            if (f > 0.45f * (float) sr) break;
                            if (sp.clickHarm[m] > -90.0f) v += dbToGain (sp.clickHarm[m]) * std::sin (kTwoPi * f * ti + phs[m]);
                        }
                        eventBuf[(size_t) (s0 + i)] += g * e * v;
                    }
                }
                t += interval;
            }
        }
    }
    if (sp.eventType != 0)
    {
        const float er = std::max (1.0e-9f, rms (eventBuf, nAttack, N));
        const float ref = hasHarm ? harmRms : 0.1f;
        const float g = dbToGain (sp.eventDb) * ref / er;
        for (auto& v : eventBuf) v *= g;
    }

    // ---- mix, attack and swell ------------------------------------------------------
    std::vector<float> out ((size_t) N);
    const float harmGain = hasHarm ? 1.0f : 0.0f;
    const float swR = fitRate (sp.levelSwellCycles / std::max (0.05f, loopSec), loopSec);
    for (int n = 0; n < N; ++n)
    {
        const float t = (float) n / (float) sr;
        float g = smoothstep (t / std::max (0.001f, sp.attackRise));
        if (swR > 0.0f && sp.levelSwellDb != 0.0f) g *= dbToGain (-sp.levelSwellDb * (0.5f - 0.5f * std::cos (kTwoPi * swR * (t - sp.attackSec))));
        out[(size_t) n] = g * (harmGain * harmBuf[(size_t) n] + noiseBuf[(size_t) n] + eventBuf[(size_t) n]);
    }

    auto wave = std::make_unique<Zone>();
    wave->rootHz = f0;

    if (sp.oneShot)
    {
        const float k = std::log (1000.0f) / std::max (0.05f, sp.oneShotDecay);
        for (int n = 0; n < N; ++n)
        {
            const float t = (float) n / (float) sr;
            out[(size_t) n] *= std::exp (-k * std::max (0.0f, t - sp.attackRise));
            const int left = N - n;
            if (left < 256) out[(size_t) n] *= (float) left / 256.0f;
        }
        wave->loopStart = wave->loopEnd = N;
    }
    else
    {
        // crossfade the end of the loop into what led up to its start, so the
        // loop end continues exactly where the loop start continues
        const int nX = juce::jlimit (16, std::min (nAttack, nLoop / 2), (int) (sp.xfadeSec * sr));
        for (int i = 0; i < nX; ++i)
        {
            const float th = (float) i / (float) nX * kPi * 0.5f;
            const int a = N - nX + i, b = nAttack - nX + i;
            out[(size_t) a] = out[(size_t) a] * std::cos (th) + out[(size_t) b] * std::sin (th);
        }
        wave->loopStart = nAttack;
        wave->loopEnd = N;
    }

    // ---- normalise: the loop (or the whole one-shot) at -18 dBFS rms --------------------
    {
        const int from = sp.oneShot ? 0 : wave->loopStart;
        const float r = std::max (1.0e-9f, rms (out, from, N));
        float g = dbToGain (-18.0f + sp.gainDb) / r;
        float peak = 0.0f;
        for (auto v : out) peak = std::max (peak, std::abs (v));
        if (peak * g > 0.98f) g = 0.98f / peak;
        for (auto& v : out) v *= g;
    }

    // ---- mip pyramid -----------------------------------------------------------------------
    wave->levels.push_back (std::move (out));
    for (int L = 1; L <= 6; ++L)
    {
        const auto& prev = wave->levels.back();
        if ((int) prev.size() < 256) break;
        wave->levels.push_back (halve (prev, wave->loopStart >> (L - 1), wave->loopEnd >> (L - 1), ! sp.oneShot));
    }
    return std::move (*wave);
}

WavePtr render (const WaveSpec& spec, double rate)
{
    auto wave = std::make_shared<Wave>();
    wave->renderRate = rate;
    wave->oneShot = spec.oneShot;
    if (spec.zoneStepSemis <= 0)
    {
        wave->zones.push_back (renderZone (spec, rate));
    }
    else
    {
        // zones from C1 to C7, each rendered at its own root; the spec's root
        // note keeps its zone, the others are placed every zoneStepSemis from it
        const float rootNote = 69.0f + 12.0f * std::log2 (spec.rootHz / 440.0f);
        const float step = (float) spec.zoneStepSemis;
        float first = rootNote;
        while (first - step >= spec.zoneLowNote - 0.01f) first -= step;
        for (float note = first; note <= 96.0f + 0.01f; note += step)
        {
            WaveSpec z = spec;
            z.rootHz = midiToHz (note);
            z.seed = spec.seed + (juce::uint32) std::lround (note) * 7919u;
            auto zone = renderZone (z, rate);
            zone.topNote = note + step * 0.5f;
            wave->zones.push_back (std::move (zone));
        }
        wave->zones.back().topNote = 127.0f;
    }
    return wave;
}

//==============================================================================
// user waves from audio files
//==============================================================================
namespace
{
    float rmsOf (const std::vector<float>& b, int from, int to)
    {
        from = juce::jlimit (0, (int) b.size(), from); to = juce::jlimit (from, (int) b.size(), to);
        double s = 0.0; for (int i = from; i < to; ++i) s += (double) b[(size_t) i] * b[(size_t) i];
        return (float) std::sqrt (s / std::max (1, to - from));
    }

    juce::AudioFormatManager& sampleFormats()
    {
        static juce::AudioFormatManager fm;
        static const bool once = [] { fm.registerBasicFormats();
                                     #if JUCE_USE_MP3AUDIOFORMAT
                                      fm.registerFormat (new juce::MP3AudioFormat(), false);
                                     #endif
                                      return true; }();
        juce::ignoreUnused (once);
        return fm;
    }

    // the audio, losslessly: FLAC at 24 bits (WAV when FLAC won't take the rate)
    bool encodeSample (const std::vector<float>& audio, double rate, juce::MemoryBlock& dest)
    {
        juce::AudioBuffer<float> buf (1, (int) audio.size());
        buf.copyFrom (0, 0, audio.data(), (int) audio.size());
        juce::FlacAudioFormat flac;
        juce::WavAudioFormat wav;
        for (juce::AudioFormat* fmt : { (juce::AudioFormat*) &flac, (juce::AudioFormat*) &wav })
        {
            dest.reset();
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::MemoryOutputStream> (dest, false);
            auto w = fmt->createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            if (w == nullptr) continue;
            const bool ok = w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            w.reset();   // flushes into dest
            if (ok && dest.getSize() > 0) return true;
        }
        return false;
    }

    // mono, at the reader's rate, up to maxSamples
    bool readMono (juce::AudioFormatReader& r, juce::int64 maxSamples, std::vector<float>& out)
    {
        const int n = (int) std::min<juce::int64> ({ r.lengthInSamples, maxSamples, (juce::int64) std::numeric_limits<int>::max() / 2 });
        if (n <= 0) return false;
        const int ch = juce::jlimit (1, 8, (int) r.numChannels);
        juce::AudioBuffer<float> buf (ch, n);
        buf.clear();
        if (! r.read (&buf, 0, n, 0, true, ch > 1)) return false;
        out.assign ((size_t) n, 0.0f);
        for (int c = 0; c < ch; ++c)
        {
            const float* src = buf.getReadPointer (c);
            for (int i = 0; i < n; ++i) out[(size_t) i] += src[i] / (float) ch;
        }
        return true;
    }

    // FNV-1a over the encoded bytes (and their count)
    juce::String keyOf (const juce::MemoryBlock& encoded)
    {
        juce::uint64 h = 1469598103934665603ull;
        const auto* b = static_cast<const juce::uint8*> (encoded.getData());
        for (size_t i = 0; i < encoded.getSize(); ++i) { h ^= b[i]; h *= 1099511628211ull; }
        return juce::String::toHexString ((juce::int64) h) + "-" + juce::String ((juce::int64) encoded.getSize());
    }

    // "Strings C#3", "pad_eb2", "vox-A4" -> the MIDI note (C4 = 60); -1 when none
    int noteFromName (const juce::String& name)
    {
        const juce::String s = name.trim();
        int found = -1;
        for (int i = 0; i < s.length(); ++i)
        {
            const juce::juce_wchar c = juce::CharacterFunctions::toUpperCase (s[i]);
            if (c < 'A' || c > 'G') continue;
            // a note name starts a word: at the start, or after a separator
            if (i > 0 && juce::CharacterFunctions::isLetterOrDigit (s[i - 1])) continue;
            int k = i + 1, accidental = 0;
            if (k < s.length() && (s[k] == '#' || s[k] == 's' || s[k] == 'S')) { accidental = 1; ++k; }
            else if (k < s.length() && s[k] == 'b') { accidental = -1; ++k; }
            bool neg = false;
            if (k < s.length() && s[k] == '-') { neg = true; ++k; }
            if (k >= s.length() || ! juce::CharacterFunctions::isDigit (s[k])) continue;
            const int octave = (int) (s[k] - '0') * (neg ? -1 : 1);
            ++k;
            // and ends one: the end, or a separator
            if (k < s.length() && juce::CharacterFunctions::isLetterOrDigit (s[k])) continue;
            static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
            const int note = (octave + 1) * 12 + semis[c - 'A'] + accidental;
            if (note >= 0 && note <= 127) found = note;   // the last one wins ("Pad 2 C3")
        }
        return found;
    }

    // the pitch a stretch of the sound plays (YIN), in Hz; 0 when it has none
    float detectPitch (const std::vector<float>& x, double rate)
    {
        const int n = (int) x.size();
        const int tauMin = std::max (2, (int) (rate / 2000.0)), tauMax = (int) (rate / 40.0);
        const int w = std::max (1024, tauMax + 16);
        if (n < w + tauMax + 2) return 0.0f;
        // after the attack, where it has settled
        const int start = juce::jlimit (0, n - w - tauMax - 1, (int) (n * 0.3));
        std::vector<float> d ((size_t) tauMax + 1, 0.0f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double acc = 0.0;
            for (int j = 0; j < w; ++j) { const float e = x[(size_t) (start + j)] - x[(size_t) (start + j + tau)]; acc += (double) e * e; }
            d[(size_t) tau] = (float) acc;
        }
        // cumulative mean normalised difference
        std::vector<float> c ((size_t) tauMax + 1, 1.0f);
        double run = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            c[(size_t) tau] = run > 0.0 ? (float) (d[(size_t) tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (c[(size_t) tau] < 0.12f)
            {
                while (tau + 1 < tauMax && c[(size_t) tau + 1] < c[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            int m = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau) if (c[(size_t) tau] < c[(size_t) m]) m = tau;
            if (c[(size_t) m] < 0.25f) best = m;
        }
        if (best < 0) return 0.0f;
        // between the samples around the dip
        double t = best;
        if (best > 1 && best < tauMax)
        {
            const double a = c[(size_t) best - 1], b = c[(size_t) best], e = c[(size_t) best + 1];
            const double den = a - 2.0 * b + e;
            if (std::abs (den) > 1.0e-12) t += juce::jlimit (-0.5, 0.5, 0.5 * (a - e) / den);
        }
        return (float) (rate / t);
    }
}

WavePtr fromSample (const SampleData& data, const SampleSettings& st)
{
    auto wave = std::make_shared<Wave>();
    wave->renderRate = data.rate;
    const int N = (int) data.audio.size();
    if (N < 64) return wave;

    std::vector<float> out (data.audio);
    Zone z;
    z.rootHz = midiToHz ((float) juce::jlimit (0, 127, st.rootNote));
    z.topNote = 127.0f;
    const bool oneShot = st.loopMode == LoopOneShot;
    wave->oneShot = oneShot;
    if (oneShot)
    {
        const int fade = std::min (256, N / 4);
        for (int i = 0; i < fade; ++i) out[(size_t) (N - 1 - i)] *= (float) i / (float) fade;
        z.loopStart = z.loopEnd = N;
    }
    else
    {
        // the loop runs from loopStart to the end; its end is crossfaded into
        // what leads up to its start, so the end goes on exactly as the start does
        int ls = juce::jlimit (1, N - 32, (int) std::lround (juce::jlimit (0.0f, 0.98f, st.loopStart) * (float) N));
        {
            // the loop start nudged (up to 25 ms) to where the sound is most like
            // what leads up to the loop end, so the loop joins in phase
            const int w = std::min ({ 2048, ls - 1, (N - ls) / 2 });
            const int shift = std::min ((int) (0.025 * data.rate), std::min (ls - w - 1, (N - ls) / 2 - w));
            if (w >= 64 && shift > 0)
            {
                const float* a = data.audio.data();
                double ee = 0.0;
                for (int i = 0; i < w; ++i) ee += (double) a[N - w + i] * a[N - w + i];
                double best = -2.0; int bestLs = ls;
                for (int d = -shift; d <= shift; ++d)
                {
                    const int c = ls + d;
                    double xy = 0.0, yy = 0.0;
                    for (int i = 0; i < w; ++i) { const double y = a[c - w + i]; xy += (double) a[N - w + i] * y; yy += y * y; }
                    const double r = (ee > 0.0 && yy > 0.0) ? xy / std::sqrt (ee * yy) : -1.0;
                    if (r > best) { best = r; bestLs = c; }
                }
                ls = bestLs;
            }
        }
        const int nX = juce::jlimit (0, std::min (ls, (N - ls) / 2), (int) (st.xfadeMs * 0.001f * (float) data.rate));
        // how alike the two ends are: a recording's sustain often is (a steady
        // tone), and an equal-power fade of alike signals swells by 3 dB in the
        // middle - so alike ends fade linearly, unlike ones with equal power
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (int i = 0; i < nX; ++i)
        {
            const double x = data.audio[(size_t) (N - nX + i)], y = data.audio[(size_t) (ls - nX + i)];
            xy += x * y; xx += x * x; yy += y * y;
        }
        const float r = xx > 0.0 && yy > 0.0 ? (float) (xy / std::sqrt (xx * yy)) : 0.0f;
        const float power = juce::jlimit (0.0f, 1.0f, 1.0f - r);   // 0: linear, 1: equal power
        for (int i = 0; i < nX; ++i)
        {
            const float t = (float) i / (float) nX, th = t * kPi * 0.5f;
            const float gOut = (1.0f - power) * (1.0f - t) + power * std::cos (th);
            const float gIn = (1.0f - power) * t + power * std::sin (th);
            const int a = N - nX + i, b = ls - nX + i;
            out[(size_t) a] = data.audio[(size_t) a] * gOut + data.audio[(size_t) b] * gIn;
        }
        z.loopStart = ls;
        z.loopEnd = N;
    }

    // the loop (or the whole one-shot) as loud as the factory waves' (-18 dBFS rms)
    {
        const float r = std::max (1.0e-9f, rmsOf (out, oneShot ? 0 : z.loopStart, N));
        float g = dbToGain (-18.0f) / r;
        float peak = 0.0f;
        for (auto v : out) peak = std::max (peak, std::abs (v));
        if (peak * g > 0.98f) g = 0.98f / std::max (1.0e-9f, peak);
        for (auto& v : out) v *= g;
    }

    z.levels.push_back (std::move (out));
    for (int L = 1; L <= 6; ++L)
    {
        const auto& prev = z.levels.back();
        if ((int) prev.size() < 256) break;
        z.levels.push_back (halve (prev, z.loopStart >> (L - 1), z.loopEnd >> (L - 1), ! oneShot));
    }
    wave->zones.push_back (std::move (z));
    return wave;
}

SamplePtr readSampleFile (const juce::File& file, double maxSeconds, juce::String& error)
{
    auto in = std::make_unique<juce::FileInputStream> (file);
    if (! in->openedOk()) { error = "The file could not be opened."; return nullptr; }
    // by its content, whatever its name says (a picked document may have lost its extension)
    std::unique_ptr<juce::AudioFormatReader> r (sampleFormats().createReaderFor (std::move (in)));
    if (r == nullptr) { error = "This is not an audio file Tidebreeze can read (WAV, AIFF, FLAC, Ogg Vorbis or MP3)."; return nullptr; }
    if (r->sampleRate < 4000.0 || r->sampleRate > 400000.0) { error = "The file's sample rate is not usable."; return nullptr; }

    auto d = std::make_shared<SampleData>();
    d->rate = r->sampleRate;
    if (! readMono (*r, (juce::int64) (maxSeconds * r->sampleRate), d->audio) || d->audio.size() < 64)
    {
        error = "The file holds no audio.";
        return nullptr;
    }
    // without the silence before and after it, at a sensible level
    float peak = 0.0f;
    for (auto v : d->audio) peak = std::max (peak, std::abs (v));
    if (peak < 1.0e-5f) { error = "The file is silent."; return nullptr; }
    {
        auto& a = d->audio;
        const float lead = peak * 0.001f, tail = peak * 0.0005f;   // -60 / -66 dB
        int first = 0, last = (int) a.size() - 1;
        while (first < last && std::abs (a[(size_t) first]) < lead) ++first;
        while (last > first && std::abs (a[(size_t) last]) < tail) --last;
        first = std::max (0, first - (int) (0.002 * d->rate));
        last = std::min ((int) a.size() - 1, last + (int) (0.01 * d->rate));
        if (last - first + 1 >= 64 && (first > 0 || last < (int) a.size() - 1))
            a = std::vector<float> (a.begin() + first, a.begin() + last + 1);
        for (auto& v : a) v *= 0.98f / peak;
    }
    if (! encodeSample (d->audio, d->rate, d->encoded)) { error = "The audio could not be stored."; return nullptr; }
    d->key = keyOf (d->encoded);
    // the name without its extension(s): "Pad.flac.wav" (a copied document) -> "Pad"
    auto name = file.getFileName();
    for (;;)
    {
        const auto ext = name.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (name.containsChar ('.') && juce::StringArray { "wav", "wave", "aif", "aiff", "flac", "ogg", "mp3" }.contains (ext))
            name = name.upToLastOccurrenceOf (".", false, false);
        else break;
    }
    d->fileName = name.isNotEmpty() ? name : juce::String ("Sample");
    return d;
}

SamplePtr decodeSample (const juce::MemoryBlock& encoded, const juce::String& fileName)
{
    if (encoded.getSize() == 0) return nullptr;
    auto d = std::make_shared<SampleData>();
    d->encoded = encoded;
    std::unique_ptr<juce::AudioFormatReader> r (sampleFormats().createReaderFor (std::make_unique<juce::MemoryInputStream> (d->encoded, false)));
    if (r == nullptr) return nullptr;
    d->rate = r->sampleRate;
    if (! readMono (*r, r->lengthInSamples, d->audio) || d->audio.size() < 64) return nullptr;
    d->key = keyOf (d->encoded);
    d->fileName = fileName;
    return d;
}

SampleSettings guessSettings (const SampleData& d)
{
    SampleSettings s;
    const int n = (int) d.audio.size();
    int note = noteFromName (d.fileName);
    if (note < 0)
    {
        const float hz = detectPitch (d.audio, d.rate);
        if (hz > 20.0f) note = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    }
    s.rootNote = note >= 0 ? juce::jlimit (0, 127, note) : 60;

    // fading away to nothing (a hit, a pluck, a bell): one-shot; holding on: loop the second half
    const int win = std::max (1, (int) (0.05 * d.rate));
    float loudest = 0.0f;
    for (int i = 0; i + win <= n; i += win) loudest = std::max (loudest, rmsOf (d.audio, i, i + win));
    const float end = rmsOf (d.audio, n - n / 5, n);
    const bool decays = end < loudest * 0.06f;   // -24 dB
    if ((double) n / d.rate < 0.25 || decays)
        s.loopMode = LoopOneShot;
    else
    {
        s.loopMode = LoopSustain;
        s.loopStart = 0.4f;
        s.xfadeMs = (float) juce::jlimit (5.0, 150.0, (double) n / d.rate * 1000.0 * 0.15);
    }
    return s;
}
}
