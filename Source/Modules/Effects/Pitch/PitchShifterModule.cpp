#include "PitchShifterModule.h"

using namespace aquanode;

namespace
{
    double besselI0 (double x)
    {
        double s = 1.0, term = 1.0;
        const double q = x * x / 4.0;
        for (int k = 1; k < 50; ++k) { term *= q / ((double) k * k); s += term; if (term < 1.0e-14 * s) break; }
        return s;
    }
}

PitchShifterModule::PitchShifterModule()
{
    // the read: a Kaiser-windowed sinc, 16 taps, a little under Nyquist,
    // each phase summing to 1 (a steady level reads back exactly)
    table.resize ((size_t) (kPhases + 1) * kTaps);
    constexpr double cutoff = 0.94, beta = 8.0;
    const int half = kTaps / 2;
    for (int ph = 0; ph <= kPhases; ++ph)
    {
        const double frac = (double) ph / kPhases;
        double sum = 0.0;
        double row[kTaps];
        for (int i = 0; i < kTaps; ++i)
        {
            const double x = (double) (i - (half - 1)) - frac;      // taps at floor-7 .. floor+8
            const double s = std::abs (x) < 1.0e-9 ? 1.0
                           : std::sin (juce::MathConstants<double>::pi * cutoff * x) / (juce::MathConstants<double>::pi * cutoff * x);
            const double u = x / (double) half;
            const double w = std::abs (u) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - u * u)) / besselI0 (beta);
            row[i] = s * w;
            sum += row[i];
        }
        for (int i = 0; i < kTaps; ++i)
            table[(size_t) ph * kTaps + (size_t) i] = (float) (row[i] / sum);
    }
}

void PitchShifterModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    int size = 1;
    while (size < (int) (sr * 2.2))
        size <<= 1;
    mask = size - 1;
    for (auto& l : line) l.assign ((size_t) size, 0.0f);
    for (auto& l : dryLine) l.assign ((size_t) size, 0.0f);
    searchSamples = (int) (sr * 0.020);
    margin = kTaps + 4;
    const int maxL = (int) (sr * 0.016) + 8;
    templ.assign ((size_t) maxL, 0.0f);
    linked.rng.setSeed (0x5eed1);
    wideL.rng.setSeed (0x5eed2);
    wideR.rng.setSeed (0x5eed3);
    reset();
}

void PitchShifterModule::reset()
{
    for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
    for (auto& l : dryLine) std::fill (l.begin(), l.end(), 0.0f);
    for (auto* s : { &linked, &wideL, &wideR })
    {
        for (auto& h : s->heads) h = {};
        s->newest = -1;
        s->untilNext = 0;
    }
    // the line starts out "full" (of silence), so the first grains have their room
    writeIndex = (juce::int64) line[0].size();
    for (auto& ch : aa)
        for (auto& b : ch) b.z1 = b.z2 = 0.0f;
    aaRatio = -1.0;
    designAntiAlias (1.0);
    aaBlend = 0.0f;
    smoothedRatio = 1.0;
    smoothedMix = param (pMix) * 0.01f;   // (Mix runs 0..100 %)
    fbSample[0] = fbSample[1] = 0.0f;
    lastDelay = 0.0;
    blockCounter = 0;
}

void PitchShifterModule::designAntiAlias (double r)
{
    // an 8th-order Butterworth low-pass under what the shift would fold back
    static constexpr double q[4] = { 0.5098, 0.6013, 0.8999, 2.5629 };
    const double fc = sampleRate * 0.47 / juce::jmax (1.0, r);
    const double w0 = juce::MathConstants<double>::twoPi * fc / sampleRate;
    const double cw = std::cos (w0), sw = std::sin (w0);
    for (int i = 0; i < 4; ++i)
    {
        const double alpha = sw / (2.0 * q[i]), inv = 1.0 / (1.0 + alpha);
        for (auto& ch : aa)
        {
            auto& b = ch[(size_t) i];
            b.b0 = (float) ((1.0 - cw) * 0.5 * inv);
            b.b1 = (float) ((1.0 - cw) * inv);
            b.b2 = b.b0;
            b.a1 = (float) (-2.0 * cw * inv);
            b.a2 = (float) ((1.0 - alpha) * inv);
        }
    }
    aaRatio = r;
}

int PitchShifterModule::crossfadeSamples() const
{
    // Correlated's crossfades: 10 ms by default - the level of noise and drums
    // kept, tones steady
    return juce::jmax (16, (int) (sampleRate * (double) param (pCrossfade) * 0.001));
}

int PitchShifterModule::correlationLength (int grain) const
{
    // the template: the crossfade's worth of what the grain before plays (4..15 ms)
    return juce::jlimit ((int) (sampleRate * 0.004), (int) (sampleRate * 0.015), juce::jmax (crossfadeSamples(), grain / 8));
}

int PitchShifterModule::searchFor (int grain) const
{
    // a quarter grain either way (20 ms at the default 80): long enough to line
    // up a kick's or a chord's whole period, which a few ms cannot
    return juce::jlimit ((int) (sampleRate * 0.005), searchSamples, grain / 4);
}

double PitchShifterModule::nominalDelay (double r, int len, int grain, bool correlated) const
{
    // room for the search either way, for the template ahead, and for the
    // grain's whole drift towards the write position (r > 1)
    const double S = correlated ? (double) searchFor (grain) : 0.0;
    const double L = correlated ? (double) correlationLength (grain) * r : 0.0;
    return (double) margin + S + juce::jmax (L, (r - 1.0) * len);
}

float PitchShifterModule::readLine (int c, double pos) const noexcept
{
    const auto& l = line[(size_t) c];
    const double fl = std::floor (pos);
    const int phase = (int) ((pos - fl) * kPhases + 0.5);
    const float* tap = table.data() + (size_t) phase * kTaps;
    const juce::int64 base = (juce::int64) fl - (kTaps / 2 - 1);
    float y = 0.0f;
    if (((base & mask) + kTaps) <= (juce::int64) l.size())
    {
        const float* d = l.data() + (base & mask);
        for (int i = 0; i < kTaps; ++i)
            y += d[i] * tap[i];
    }
    else
        for (int i = 0; i < kTaps; ++i)
            y += l[(size_t) ((base + i) & mask)] * tap[i];
    return y;
}

float PitchShifterModule::readLinear (int first, int count, double pos) const noexcept
{
    const juce::int64 i0 = (juce::int64) std::floor (pos);
    const float f = (float) (pos - (double) i0);
    float y = 0.0f;
    for (int c = first; c < first + count; ++c)
    {
        const float a = line[(size_t) c][(size_t) (i0 & mask)], b = line[(size_t) c][(size_t) ((i0 + 1) & mask)];
        y += a + (b - a) * f;
    }
    return y;
}

void PitchShifterModule::startHead (Shifter& s, int first, int count, double r, int grain, bool correlated, float jitter)
{
    // a free head, else the one closest to its end
    int slot = -1;
    float most = -1.0f;
    for (int i = 0; i < kMaxHeads; ++i)
    {
        if (! s.heads[(size_t) i].active) { slot = i; break; }
        const float done = (float) s.heads[(size_t) i].n / (float) s.heads[(size_t) i].len;
        if (done > most) { most = done; slot = i; }
    }

    // Correlated: a splice every quarter grain, each grain playing on its own
    // between short crossfades; Grains: Hann windows a grain long, four deep
    const double vary = 1.0 + jitter * (s.rng.nextDouble() - 0.5) * 0.8;
    int hop, len, fade;
    if (correlated)
    {
        hop = juce::jmax (32, (int) ((double) grain * 0.25 * vary));
        fade = juce::jmin (hop, crossfadeSamples());
        len = hop + fade;
    }
    else
    {
        len = juce::jmax (64, (int) ((double) grain * vary));
        hop = juce::jmax (16, (int) ((double) len * 0.25 * (1.0 + jitter * (s.rng.nextDouble() - 0.5) * 0.8)));
        fade = len / 2;
    }
    double delay = nominalDelay (r, len, grain, correlated);
    if (! correlated)
        delay += (double) grain * (0.25 + 0.25 * jitter) * s.rng.nextDouble();   // where Grains' randomness comes from
    double pos = (double) writeIndex - std::round (delay);   // (the search may move it by a fraction)

    if (correlated && s.newest >= 0 && s.heads[(size_t) s.newest].active)
    {
        // what the grain before this one is about to play ...
        const auto& ref = s.heads[(size_t) s.newest];
        const int S = searchFor (grain);
        int L = correlationLength (grain);
        L = juce::jmin (L, (int) (((double) writeIndex - ref.pos - margin) / r) - 2);
        L = juce::jmin (L, (int) ((delay - S - margin) / r) - 2);
        L = juce::jmin (L, (int) templ.size() - 1);
        if (L >= 32)
        {
            double energyT = 0.0;
            for (int j = 0; j < L; ++j)
            {
                templ[(size_t) j] = readLinear (first, count, ref.pos + (double) j * r);
                energyT += (double) templ[(size_t) j] * templ[(size_t) j];
            }
            if (energyT > 1.0e-12)
            {
                // ... and the start nearby that matches it best: every 4th lag on
                // every 4th sample, then the lags around the best on every 2nd
                auto score = [&] (int k, int stride)
                {
                    double dot = 0.0, e = 0.0;
                    const double p0 = pos + k;
                    for (int j = 0; j < L; j += stride)
                    {
                        const float v = readLinear (first, count, p0 + (double) j * r);
                        dot += (double) v * templ[(size_t) j];
                        e += (double) v * v;
                    }
                    return dot / std::sqrt (e * energyT / stride + 1.0e-20);
                };
                int best = 0;
                double bestScore = score (0, 4) + 1.0e-3;   // no reason to move: stay
                for (int k = -S; k <= S; k += 4)
                    if (const double sc = score (k, 4); sc > bestScore) { bestScore = sc; best = k; }
                const int centre = best;
                bestScore = score (centre, 2);
                for (int k = centre - 3; k <= centre + 3; ++k)
                    if (k != centre && std::abs (k) <= S)
                        if (const double sc = score (k, 2); sc > bestScore) { bestScore = sc; best = k; }
                // and between the samples: a parabola through the best and its neighbours
                double frac = 0.0;
                if (std::abs (best) < S)
                {
                    const double a = score (best - 1, 1), b = score (best, 1), c = score (best + 1, 1);
                    const double den = a - 2.0 * b + c;
                    if (den < -1.0e-12 && b < 0.99999)   // an exact match (0 st) stays where it is
                        frac = juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den);
                }
                pos += (double) best + frac;
            }
        }
    }

    auto& h = s.heads[(size_t) slot];
    h.active = true;
    h.pos = pos;
    h.n = 0;
    h.len = len;
    h.fade = juce::jmax (1, fade);
    s.newest = slot;
    s.untilNext = hop;   // Correlated: the next one fades in as this one fades out
}

void PitchShifterModule::processShifter (Shifter& s, int first, int count, float* out, double r, int grain,
                                         bool correlated, float jitter)
{
    if (--s.untilNext <= 0)
        startHead (s, first, count, r, grain, correlated, jitter);

    float acc[2] {};
    float wSum = 0.0f, w2Sum = 0.0f;
    const double limit = (double) writeIndex - margin;
    for (auto& h : s.heads)
    {
        if (! h.active)
            continue;
        const int edge = juce::jmin (h.n, h.len - 1 - h.n);
        const float w = edge >= h.fade ? 1.0f
                      : 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * ((float) edge + 0.5f) / (float) h.fade);
        if (h.pos > limit)
            h.pos = limit;   // the pitch went up mid-grain: it may not read ahead of what is written
        for (int c = 0; c < count; ++c)
            acc[c] += w * readLine (first + c, h.pos);
        wSum += w;
        w2Sum += w * w;
        h.pos += r;
        if (++h.n >= h.len)
            h.active = false;
    }

    const float norm = correlated ? 1.0f / juce::jmax (0.05f, wSum)
                                  : 0.94f / std::sqrt (juce::jmax (0.0025f, w2Sum));   // about as loud as Correlated
    for (int c = 0; c < count; ++c)
        out[c] = acc[c] * norm;
}

void PitchShifterModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // Pitch in whole semitones (also when a cable moves it: it steps), Fine the cents
    const double semis = juce::jlimit (-48.0, 48.0, (double) std::round (param (pPitch)) + (double) param (pFine) * 0.01);
    const double target = std::pow (2.0, semis / 12.0);
    const double coeff = 1.0 - std::exp (-1.0 / (0.008 * sampleRate));
    smoothedRatio += (target - smoothedRatio) * coeff;
    const double r = smoothedRatio;

    const int grain = juce::jmax (64, (int) (param (pGrain) * 0.001 * sampleRate));
    const float jitter = juce::jlimit (0.0f, 1.0f, param (pJitter) * 0.01f);
    const bool correlated = param (pAlgorithm) > 0.5f;
    const bool wide = param (pStereo) > 0.5f;
    const float feedback = juce::jlimit (0.0f, 0.95f, param (pFeedback) * 0.01f);

    if (++blockCounter >= 32)
    {
        blockCounter = 0;
        if (std::abs (r - aaRatio) > 0.002 * aaRatio)
            designAntiAlias (r);
    }

    // into the line: the input, the fed-back shift, under the fold-back
    ++writeIndex;
    const auto wi = (size_t) (writeIndex & mask);
    // the low-pass only where the shift goes up (blended in over the first
    // semitone): at 0 st and below the line holds the input untouched, so the
    // wet sound is the dry one delayed, with no phase shift at the top
    const float aaTarget = (float) juce::jlimit (0.0, 1.0, (r - 1.0) / 0.0595);
    aaBlend += (aaTarget - aaBlend) * 0.001f;
    for (int c = 0; c < 2; ++c)
    {
        const float raw = inputs[0][(size_t) c] + feedback * fbSample[c];
        float x = raw;
        for (auto& b : aa[(size_t) c])
        {
            const float y = b.b0 * x + b.z1;
            b.z1 = b.b1 * x - b.a1 * y + b.z2;
            b.z2 = b.b2 * x - b.a2 * y;
            x = y;
        }
        line[(size_t) c][wi] = raw + (x - raw) * aaBlend;
        dryLine[(size_t) c][wi] = inputs[0][(size_t) c];
    }

    float wet[2] {};
    if (wide)
    {
        processShifter (wideL, 0, 1, wet, r, grain, correlated, jitter);
        processShifter (wideR, 1, 1, wet + 1, r, (int) (grain * 1.07), correlated, jitter);
    }
    else
        processShifter (linked, 0, 2, wet, r, grain, correlated, jitter);

    for (int c = 0; c < 2; ++c)
    {
        fbSample[c] = juce::dsp::FastMathApproximations::tanh (juce::jlimit (-3.0f, 3.0f, wet[c]));
        if (! std::isfinite (wet[c])) wet[c] = 0.0f;
    }

    // the dry sound, delayed by the wet sound's average delay with Dry Delay Match
    // the wet sound's average delay: the start delay less half a grain's drift
    const int len = correlated ? grain / 4 + juce::jmin (grain / 4, crossfadeSamples()) : grain;
    const double d = nominalDelay (r, len, grain, correlated) - (r - 1.0) * len * 0.5
                   + (correlated ? 0.0 : grain * 0.25 * 0.5);
    lastDelay += (d - lastDelay) * (1.0 - std::exp (-1.0 / (0.05 * sampleRate)));
    const bool delayDry = param (pDryDelay) > 0.5f;

    smoothedMix += (param (pMix) * 0.01f - smoothedMix) * 0.002f;
    const float mix = smoothedMix;
    for (int c = 0; c < 2; ++c)
    {
        float dry = inputs[0][(size_t) c];
        if (delayDry)
        {
            const double p = (double) writeIndex - lastDelay;
            const juce::int64 i0 = (juce::int64) std::floor (p);
            const float f = (float) (p - (double) i0);
            const float a = dryLine[(size_t) c][(size_t) (i0 & mask)], b = dryLine[(size_t) c][(size_t) ((i0 + 1) & mask)];
            dry = a + (b - a) * f;
        }
        outputs[0][(size_t) c] = dry * (1.0f - mix) + wet[c] * mix;
    }
}

//==============================================================================
static ModuleDescriptor pitchShifterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.pitchshift";
    d.displayName = "Pitch Shifter";
    d.description =
        "Shifts the pitch by up to two octaves either way with overlapping grains read from a delay line. Correlated splices a new grain in every quarter Grain Size, lined up with the one before it "
        "(clean, steady tones; Crossfade: short is punchier, long is smoother), Grains lets four overlapping grains "
        "fall at random (airy, shimmering). Grain Size trades smoothness for delay, Jitter randomises the grains, "
        "Feedback sends the shift back in for rising or falling shimmers, Stereo Wide runs the sides apart, and "
        "Dry Delay Match keeps Mix from combing.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 60;
    d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };

    auto grain = makeRotary ("grain", "Grain Size", 20.0f, 200.0f, 80.0f, 0, "ms", true);
    d.params = {
        makeRotary ("pitch", "Pitch", -24.0f, 24.0f, 0.0f, 0, "st", false, 1.0f),   // whole semitones; Fine does the cents
        makeRotary ("fine", "Fine", -100.0f, 100.0f, 0.0f, 0, "ct"),
        grain,
        makeRotary ("jitter", "Jitter", 0.0f, 100.0f, 0.0f, 1, "%", false, 1.0f),
        makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
        makeCombo ("algorithm", "Algorithm", { "Grains", "Correlated" }, 1, 1, 2),
        makeRotary ("feedback", "Feedback", 0.0f, 95.0f, 0.0f, 1, "%", false, 1.0f),
        makeCombo ("stereo", "Stereo", { "Linked", "Wide" }, 0, 2, 2),
        makeCombo ("dryDelay", "Dry Delay", { "Off", "Match" }, 1, 2, 2),
        makeRotary ("xfade", "Crossfade", 2.0f, 40.0f, 10.0f, 0, "ms", true).visibleWhen ("algorithm", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (PitchShifterModule, pitchShifterDescriptor)
