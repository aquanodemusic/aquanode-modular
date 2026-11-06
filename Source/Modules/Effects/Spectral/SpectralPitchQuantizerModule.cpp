#include "SpectralPitchQuantizerModule.h"

using namespace aquanode;

// C C# D D# E F F# G G# A A# B  ->  G# G# A# A# C# C# C# D# D# D# F# F#
const int SpectralPitchQuantizerModule::kDefaultMap[12] = { 8, 8, 10, 10, 1, 1, 1, 3, 3, 3, 6, 6 };

static const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

SpectralPitchQuantizerModule::SpectralPitchQuantizerModule()
{
    window.resize ((size_t) kN);
    for (int i = 0; i < kN; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kN);

    // Hann analysis * Hann synthesis at 8x overlap sums to a constant
    double acc = 0.0;
    for (int j = 0; j < kHop; ++j)
        for (int i = j; i < kN; i += kHop)
            acc += (double) window[(size_t) i] * window[(size_t) i];
    olaGain = (float) ((double) kHop / acc);

    {
        constexpr int reachCells = (int) (3.0f * warpWidthOct * kGridPerOct);
        for (int d = -reachCells; d <= reachCells; ++d)
        {
            const float x = (float) d / (warpWidthOct * kGridPerOct);
            gridKernel[(size_t) (d + reachCells)] = std::exp (-0.5f * x * x);
        }
    }

    for (int c = 0; c < 2; ++c)
    {
        inRing[c].assign ((size_t) kN, 0.0f);
        outAcc[c].assign ((size_t) (2 * kN), 0.0f);
        outSpec[c].assign ((size_t) kBins, {});
        chPhase[c].assign ((size_t) kBins, 0.0f);
    }
    for (auto& s : spec)
        s.assign ((size_t) kBins, {});
    fftBuf.assign ((size_t) (2 * kN), 0.0f);
    midMag.assign ((size_t) kBins, 0.0f);
    midPhase.assign ((size_t) kBins, 0.0f);
    prevMidPhase.assign ((size_t) kBins, 0.0f);
    claimed.assign ((size_t) kBins, 0);
}

void SpectralPitchQuantizerModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void SpectralPitchQuantizerModule::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
        std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f);
    }
    std::fill (prevMidPhase.begin(), prevMidPhase.end(), 0.0f);
    numPrevPeaks = 0;
    polyHold = 0;
    noteShift.fill (0.0f);
    noteLastSeen.fill (-100);
    ringPos = 0;
    hopCount = 0;
    frameIndex = 0;
}

//==============================================================================
void SpectralPitchQuantizerModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int outIdx = (int) (((frameIndex * kHop + hopCount) % (2 * kN)));
    for (int c = 0; c < 2; ++c)
    {
        inRing[c][(size_t) ringPos] = inputs[0][(size_t) c];
        outputs[0][(size_t) c] = outAcc[c][(size_t) outIdx];
        outAcc[c][(size_t) outIdx] = 0.0f;
    }
    ringPos = (ringPos + 1) % kN;

    if (++hopCount >= kHop)
    {
        hopCount = 0;
        processFrame();
        ++frameIndex;
    }
}

//==============================================================================
// r * e^(i a) for any sign of r. Not std::polar: the Hann lobe is signed, and
// libc++ (Android, macOS) returns NaN from std::polar for a negative radius -
// which the engine's safety net turns into silence.
static inline std::complex<float> polarSigned (float r, float a) noexcept
{
    return { r * std::cos (a), r * std::sin (a) };
}

static inline float wrapPi (float x) noexcept
{
    return x - juce::MathConstants<float>::twoPi * std::round (x / juce::MathConstants<float>::twoPi);
}

void SpectralPitchQuantizerModule::processFrame()
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    const float sr = (float) sampleRate;

    // ---- analysis: L, R and their mid --------------------------------------
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < kN; ++i)
            fftBuf[(size_t) i] = inRing[c][(size_t) ((ringPos + i) % kN)] * window[(size_t) i];
        std::fill (fftBuf.begin() + kN, fftBuf.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fftBuf.data(), true);
        auto& s = spec[c + 1];
        for (int k = 0; k < kBins; ++k)
            s[(size_t) k] = { fftBuf[(size_t) (2 * k)], fftBuf[(size_t) (2 * k + 1)] };
    }
    float maxMag = 0.0f;
    for (int k = 0; k < kBins; ++k)
    {
        const auto m = 0.5f * (spec[1][(size_t) k] + spec[2][(size_t) k]);
        spec[0][(size_t) k] = m;
        midMag[(size_t) k] = std::abs (m);
        midPhase[(size_t) k] = std::arg (m);
        maxMag = juce::jmax (maxMag, midMag[(size_t) k]);
        for (int c = 0; c < 2; ++c)
            chPhase[c][(size_t) k] = std::arg (spec[c + 1][(size_t) k]);
    }

    // ---- controls ------------------------------------------------------------
    // Tolerance: a dead zone around each semitone, 0..50 cents (50 % = 16.5 c:
    // the folded residue PITCHMAP leaves on a vibrato it corrects)
    const float tolSemis = 0.5f * std::pow (param (pTolerance) * 0.01f, 1.6f);
    const float keep = param (pIntonation) * 0.01f;
    const float clean = param (pClean) * 0.01f;
    const float glide = param (pGlide) * 0.01f;
    // Warp, as PITCHMAP's Electrify measures: 0 = every partial of a single
    // note moved on its own (a gliding one - vibrato, slides - partly as one
    // harmonic whole); towards 50 % a gliding note moves wholly as one; from 50 % to 100 % the moves bend into
    // one smooth, stretched frequency warp over the spectrum ("electric").
    // Chords always move note by note.
    const float warpKnob = param (pWarp) * 0.01f;
    const float noteMix = warpKnob <= 0.5f ? warpKnob : 1.0f - warpKnob;      // up to 0.5 at 50 %
    const float warpSmooth = warpKnob <= 0.5f ? 0.0f : std::pow ((warpKnob - 0.5f) * 2.0f, 2.0f);
    // Top Fold: 0 = upper harmonics go with their note (a smooth, dense top,
    // as PITCHMAP measures); 100 % = every harmonic above the 24th of a chord
    // note, and every one above the 5th of a single note, folds onto the map
    // by its own pitch - the earlier, sparser, brighter version of this module
    const float topFold = param (pTopFold) * 0.01f;

    int offsets[12];
    for (int pc = 0; pc < 12; ++pc)
        offsets[pc] = offsetFor (pc, juce::jlimit (0, 11, (int) std::lround (param (pMap0 + pc))));

    const float hopSec = (float) kHop / sr;
    const float devSlowA = 1.0f - std::exp (-hopSec / 0.25f);   // 250 ms
    const float motionA = 1.0f - std::exp (-hopSec / 0.06f);    // 60 ms
    const float glideTau = 0.4f * glide * glide;               // 0 / 100 ms / 400 ms
    const float glideA = glideTau <= 1.0e-4f ? 1.0f : 1.0f - std::exp (-hopSec / glideTau);

    // ---- tonal peaks on the mid --------------------------------------------
    numPeaks = 0;
    const float floorMag = juce::jmax (1.0e-7f, maxMag * 3.0e-4f);   // ~ -70 dB re frame max
    const int minBin = juce::jmax (3, (int) std::ceil (25.0f * (float) kN / sr));
    const int maxBin = juce::jmin (kBins - 4, (int) (16000.0f * (float) kN / sr));
    auto binFreq = [&] (int k)    // phase vocoder frequency of bin k, in bins
    {
        const float omega = twoPi * (float) k / (float) kN;
        const float dphi = wrapPi (midPhase[(size_t) k] - prevMidPhase[(size_t) k] - omega * (float) kHop);
        return (float) k + dphi * (float) kN / (twoPi * (float) kHop);
    };

    for (int k = minBin; k <= maxBin && numPeaks < kMaxPeaks; ++k)
    {
        const float m = midMag[(size_t) k];
        if (m < floorMag || m <= midMag[(size_t) k - 1] || m < midMag[(size_t) k + 1])
            continue;

        // Tonal or noise? A partial stands far above the floor of the spectrum
        // around it: 8x the 20th percentile of +-32 bins (~ +-375 Hz at
        // 48 kHz). A low percentile, not the median, so that a dense chord's
        // own partials - whose lobes fill most bins - do not raise the floor
        // and hide each other. Measured: a bass triad of saws 100 % of its peak
        // energy tonal, white noise 0 %. What is not tonal passes unmoved.
        const int a = juce::jmax (0, k - kMedianReach), b = juce::jmin (kBins - 1, k + kMedianReach);
        const int n = b - a + 1;
        std::copy (midMag.begin() + a, midMag.begin() + b + 1, medianScratch.begin());
        std::nth_element (medianScratch.begin(), medianScratch.begin() + n / 5, medianScratch.begin() + n);
        if (m < 8.0f * medianScratch[(size_t) (n / 5)])
            continue;
        // a true peak's frequency lies within half a bin of its maximum: a
        // vocoder estimate further off is noise, and would blow up the
        // lobe scaling below - keep it within 0.75 bin
        const float fk = juce::jlimit ((float) k - 0.75f, (float) k + 0.75f, binFreq (k));

        auto& p = peaks[(size_t) numPeaks++];
        p.bin = k;
        p.mag = m;
        p.freq = fk * twoPi / (float) kN;
    }

    // regions: valley to valley, at most a few bins past the main lobe
    constexpr int reach = 6;    // a partial's region: main lobe and skirt
    for (int i = 0; i < numPeaks; ++i)
    {
        auto& p = peaks[(size_t) i];
        int lo = p.bin, hi = p.bin;
        const int loLimit = i > 0 ? peaks[(size_t) i - 1].bin + 1 : 1;
        const int hiLimit = i + 1 < numPeaks ? peaks[(size_t) i + 1].bin - 1 : kBins - 2;
        while (lo - 1 >= loLimit && p.bin - (lo - 1) <= reach && midMag[(size_t) lo - 1] <= midMag[(size_t) lo]) --lo;
        while (hi + 1 <= hiLimit && (hi + 1) - p.bin <= reach && midMag[(size_t) hi + 1] <= midMag[(size_t) hi]) ++hi;
        p.lo = lo; p.hi = hi;
    }

    // ---- tracking: each peak continues the previous frame's nearest peak
    // (within 2 bins), so a moved partial keeps one oscillator phase and one
    // note from frame to frame ---------------------------------------------
    {
        int j = 0;
        for (int i = 0; i < numPeaks; ++i)
        {
            auto& p = peaks[(size_t) i];
            p.prev = -1;
            while (j + 1 < numPrevPeaks && std::abs (prevPeaks[(size_t) j + 1].bin - p.bin) <= std::abs (prevPeaks[(size_t) j].bin - p.bin))
                ++j;
            if (j < numPrevPeaks && std::abs (prevPeaks[(size_t) j].bin - p.bin) <= 2)
                p.prev = j;
        }
    }

    // ---- per-peak target shift ---------------------------------------------
    for (int i = 0; i < numPeaks; ++i)
    {
        auto& p = peaks[(size_t) i];
        const float hz = juce::jmax (1.0f, p.freq * sr / twoPi);
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        int note = juce::jlimit (0, 127, (int) std::lround (midi));
        // hysteresis: a tracked partial keeps its note until it is 0.7
        // semitone away from it, so it never flickers between two notes
        if (p.prev >= 0)
        {
            const int was = prevPeaks[(size_t) p.prev].note;
            if (std::abs (midi - (float) was) < 0.7f)
                note = was;
        }
        const float dev = midi - (float) note;

        // Intonation keeps a partial's slow deviation (its steady detune)
        // in proportion, but its fast one (vibrato) only on a 4th-power law:
        // at 50 % a vibrato is corrected almost as hard as at 0 and only
        // comes back near 100 % - as PITCHMAP's Feel does. Inside the
        // Tolerance zone nothing is corrected.
        // how fast it glides (cents per hop, smoothed ~60 ms): a moving
        // note - vibrato, a slide - is heard as one, its partials move with it
        p.motion = 0.0f;
        if (p.prev >= 0)
        {
            const auto& q = prevPeaks[(size_t) p.prev];
            const float inst = std::abs (1200.0f * std::log2 (juce::jmax (1.0e-6f, p.freq) / juce::jmax (1.0e-6f, q.freq)));
            p.motion = q.motion + (juce::jmin (inst, 50.0f) - q.motion) * motionA;
        }

        p.devSlow = dev;
        if (p.prev >= 0 && prevPeaks[(size_t) p.prev].note == note)
        {
            const float was = prevPeaks[(size_t) p.prev].devSlow;
            p.devSlow = was + (dev - was) * devSlowA;
        }
        const float keptDev = std::abs (dev) < tolSemis
                                  ? dev
                                  : keep * p.devSlow + keep * keep * keep * keep * (dev - p.devSlow);
        // a note the matrix leaves where it is is left alone altogether - no
        // retuning either (PITCHMAP is a mapper, not a tuner: its identity
        // map keeps a 30-cent-sharp tone 30 cents sharp)
        const float target = offsets[note % 12] == 0 ? 0.0f : (float) offsets[note % 12] + keptDev - dev;

        // a note not heard in the last frames starts again unshifted
        if (frameIndex - noteLastSeen[(size_t) note] > 2)
            noteShift[(size_t) note] = glideA >= 1.0f ? target : 0.0f;
        noteShift[(size_t) note] += (target - noteShift[(size_t) note]) * glideA;
        noteLastSeen[(size_t) note] = frameIndex;

        p.note = note;
        p.shift = noteShift[(size_t) note];
        p.logf = std::log2 (hz);
    }

    // ---- notes: with two or more notes sounding, every partial moves with
    // the note it is the lowest harmonic of (its whole series keeps its
    // shape); a single note's partials move each on its own. That is what
    // PITCHMAP measures as: a lone saw's 3rd harmonic goes where its own
    // pitch maps, but in a triad a note's overtones follow the note.
    {
        float strongest = 0.0f;
        for (int i = 0; i < numPeaks; ++i)
            strongest = juce::jmax (strongest, peaks[(size_t) i].mag);

        int funds[kMaxNotes];
        int numFunds = 0;
        const float maxFundRad = twoPi * 1000.0f / sr;   // fundamentals live below 1 kHz
        for (int i = 0; i < numPeaks && numFunds < kMaxNotes; ++i)
        {
            const auto& p = peaks[(size_t) i];
            if (p.freq > maxFundRad)
                break;
            if (p.mag < 0.0316f * strongest)          // within 30 dB of the loudest partial
                continue;
            bool overtone = false;
            for (int f = 0; f < numFunds && ! overtone; ++f)
            {
                const auto& q = peaks[(size_t) funds[f]];
                const float h = p.freq / q.freq;
                const float k = std::round (h);
                overtone = k >= 2.0f && k <= 40.0f && q.mag >= 0.1f * p.mag
                           && std::abs (1200.0f * std::log2 (h / k)) < 30.0f;
            }
            if (! overtone)
                funds[numFunds++] = i;
        }

        polyHold = numFunds >= 2 ? kPolyHoldFrames : juce::jmax (0, polyHold - 1);
        const bool poly = polyHold > 0;

        // Version A of every partial: in polyphony the note it belongs to
        // moves it, else its own pitch does. Version B: its note moves it -
        // for a single note only while that note glides (vibrato, a slide),
        // as much as Warp allows up to its middle.
        for (int i = 0; i < numPeaks; ++i)
        {
            auto& p = peaks[(size_t) i];
            int best = -1;
            float bestK = 1.0e9f;
            for (int f = 0; f < numFunds; ++f)
            {
                const auto& q = peaks[(size_t) funds[f]];
                if (funds[f] == i) { best = -1; break; }   // a fundamental moves by itself
                if (q.freq >= p.freq) continue;
                const float h = p.freq / q.freq;
                const float k = std::round (h);
                if (k < 2.0f || k > 80.0f || std::abs (1200.0f * std::log2 (h / k)) >= 35.0f)
                    continue;
                if (k < bestK || (k == bestK && best >= 0 && q.mag > peaks[(size_t) best].mag))
                {
                    bestK = k;
                    best = funds[f];
                }
            }
            const float noteShiftOf = best >= 0 ? peaks[(size_t) best].shift : p.shift;
            if (poly)
            {
                // beyond the 24th harmonic Top Fold crossfades the note's move
                // with the partial's own; the larger share is version A (the
                // one Warp bends), so either end is exactly one of the two
                const float ownShift = p.shift;
                const bool far = best >= 0 && bestK > 24.0f;
                const bool ownLeads = far && topFold >= 0.5f;
                p.shift = ownLeads ? ownShift : noteShiftOf;
                p.shiftB = ownLeads ? noteShiftOf : ownShift;
                p.mixB = far && std::abs (noteShiftOf - ownShift) > 0.01f ? (ownLeads ? 1.0f - topFold : topFold) : 0.0f;
            }
            else
            {
                // a gliding note's partials go with the note (PITCHMAP: at
                // Electrify 0.5 a vibrato comes out as one clean harmonic
                // note, while a steady tone's partials still move each alone)
                const float motion = best >= 0 ? peaks[(size_t) best].motion : p.motion;
                const float moving = juce::jlimit (0.0f, 1.0f, (motion - 0.7f) / 1.5f);
                // and a steady note's upper harmonics go with it too: from the
                // 6th to the 16th they hand over from their own pitch to the
                // note's (PITCHMAP keeps a steady saw's top as the mapped
                // note's dense harmonic series, not folded onto a few notes)
                // (Top Fold takes that back: at 100 % the top folds harmonic by
                // harmonic, and above the 24th not even a glide carries it)
                const float upper = best >= 0 ? (1.0f - topFold) * juce::jlimit (0.0f, 1.0f, (bestK - 6.0f) / 10.0f) : 0.0f;
                const float far = best >= 0 && bestK > 24.0f ? 1.0f - topFold : 1.0f;
                const float mix = far * juce::jmax (moving, upper) * (0.35f + 0.65f * juce::jmin (1.0f, 2.0f * noteMix));
                p.shiftB = noteShiftOf;
                p.mixB = std::abs (noteShiftOf - p.shift) > 0.01f ? mix : 0.0f;
            }
        }
    }

    // Warp: blend each shift towards an amplitude-weighted smooth curve of
    // all shifts over log frequency (a quarter octave wide), worked out on a
    // 1/24-octave grid so it costs the same however many partials there are
    if (warpSmooth > 0.001f && numPeaks > 1)
    {
        std::fill (gridW.begin(), gridW.end(), 0.0f);
        std::fill (gridS.begin(), gridS.end(), 0.0f);
        for (int i = 0; i < numPeaks; ++i)
        {
            const auto& p = peaks[(size_t) i];
            const int g = juce::jlimit (0, kGrid - 1, (int) std::lround ((p.logf - kGridLowOct) * kGridPerOct));
            gridW[(size_t) g] += p.mag;
            gridS[(size_t) g] += p.mag * p.shift;
        }
        constexpr int reachCells = (int) (3.0f * warpWidthOct * kGridPerOct);
        for (int g = 0; g < kGrid; ++g)
        {
            float ws = 0.0f, acc = 0.0f;
            for (int d = -reachCells; d <= reachCells; ++d)
            {
                const int j = g + d;
                if (j < 0 || j >= kGrid || gridW[(size_t) j] <= 0.0f) continue;
                const float w = gridKernel[(size_t) (d + reachCells)];
                ws += w * gridW[(size_t) j];
                acc += w * gridS[(size_t) j];
            }
            gridSmooth[(size_t) g] = ws > 0.0f ? acc / ws : 0.0f;
        }
        for (int i = 0; i < numPeaks; ++i)
        {
            auto& p = peaks[(size_t) i];
            const float gf = juce::jlimit (0.0f, (float) (kGrid - 1), (p.logf - kGridLowOct) * kGridPerOct);
            const int g0 = juce::jmin (kGrid - 2, (int) gf);
            const float fr = gf - (float) g0;
            const float sm = gridSmooth[(size_t) g0] + fr * (gridSmooth[(size_t) g0 + 1] - gridSmooth[(size_t) g0]);
            p.shift += warpSmooth * (sm - p.shift);
        }
    }

    // ---- synthesis, per channel ----------------------------------------------
    // A moved partial is resynthesised: an ideal stationary sinusoid at its
    // new frequency (the Hann window's own lobe, scaled to the input peak),
    // whose phase runs on from the tracked peak of the frame before, plus
    // what of the input lobe is not that ideal sinusoid (chirp, noise, a
    // second partial merged into it), carried along by (1 - Clean). Up to two
    // versions of a partial sound (see Warp), each its own oscillator, their
    // levels split by power. Unmoved peaks and every bin no moving peak
    // claims pass through untouched.
    const float halfN = 0.5f * (float) kN;
    constexpr float piF = juce::MathConstants<float>::pi;
    const float binW = twoPi / (float) kN;
    const float residual = 1.0f - clean;
    for (int c = 0; c < 2; ++c)
    {
        auto& in = spec[c + 1];
        auto& out = outSpec[c];
        auto& ph = chPhase[c];

        auto moves = [] (const Peak& p) { return std::abs (p.shift) >= 0.002f || p.mixB > 0.0f; };

        std::fill (claimed.begin(), claimed.end(), (uint8_t) 0);
        for (int i = 0; i < numPeaks; ++i)
            if (moves (peaks[(size_t) i]))
                for (int k = peaks[(size_t) i].lo; k <= peaks[(size_t) i].hi; ++k)
                    claimed[(size_t) k] = 1;
        for (int k = 0; k < kBins; ++k)
            out[(size_t) k] = claimed[(size_t) k] ? std::complex<float>() : in[(size_t) k];

        for (int i = 0; i < numPeaks; ++i)
        {
            auto& p = peaks[(size_t) i];
            // the input oscillator's phase at the frame start
            const float psiIn = wrapPi (ph[(size_t) p.bin] - (p.freq - binW * (float) p.bin) * halfN);

            if (! moves (p))
            {
                p.psi[c] = p.psiB[c] = psiIn;
                p.outFreq = p.outFreqB = p.freq;
                continue;
            }

            const float fIn = p.freq / binW;      // in bins
            const float kIn = hannKernel (fIn - (float) p.bin);
            const float scale = kIn > 1.0e-6f ? std::abs (in[(size_t) p.bin]) / kIn : 0.0f;
            const Peak* q = p.prev >= 0 ? &prevPeaks[(size_t) p.prev] : nullptr;

            for (int v = 0; v < 2; ++v)
            {
                const float weight = std::sqrt (v == 0 ? 1.0f - p.mixB : p.mixB);
                const float shift = v == 0 ? p.shift : p.shiftB;
                const float newFreq = p.freq * std::exp2 (shift / 12.0f);
                const int newBin = (int) std::lround (newFreq / binW);
                float& psiSlot = v == 0 ? p.psi[c] : p.psiB[c];
                (v == 0 ? p.outFreq : p.outFreqB) = newFreq;

                float psi = psiIn;
                if (q != nullptr)
                {
                    const bool hadIt = v == 0 || q->mixB > 0.0f;
                    if (hadIt)
                        psi = wrapPi ((v == 0 ? q->psi[c] : q->psiB[c])
                                      + 0.5f * ((v == 0 ? q->outFreq : q->outFreqB) + newFreq) * (float) kHop);
                }
                psiSlot = psi;

                if (weight < 1.0e-4f || newBin < 1 || newBin >= kBins - 1)
                    continue;

                const float fOut = newFreq / binW;
                for (int m = -kLobe; m <= kLobe; ++m)
                {
                    const int j = newBin + m;
                    if (j < 1 || j >= kBins - 1)
                        continue;
                    const float dlt = fOut - (float) j;
                    out[(size_t) j] += polarSigned (weight * scale * hannKernel (dlt), psi + piF * dlt);
                }

                if (residual > 0.001f)
                {
                    const float rot = (psi + piF * (fOut - (float) newBin)) - ph[(size_t) p.bin];
                    const auto turn = std::polar (weight * residual, rot);
                    const int d = newBin - p.bin;
                    for (int k = p.lo; k <= p.hi; ++k)
                    {
                        const int ok = k + d;
                        if (ok < 1 || ok >= kBins - 1)
                            continue;
                        const float dlt = fIn - (float) k;
                        const auto ideal = polarSigned (scale * hannKernel (dlt), psiIn + piF * dlt);
                        out[(size_t) ok] += (in[(size_t) k] - ideal) * turn;
                    }
                }
            }
        }

        // inverse
        for (int k = 0; k < kBins; ++k)
        {
            fftBuf[(size_t) (2 * k)] = out[(size_t) k].real();
            fftBuf[(size_t) (2 * k + 1)] = out[(size_t) k].imag();
        }
        for (int k = kBins; k < kN; ++k)
        {
            fftBuf[(size_t) (2 * k)] = 0.0f;
            fftBuf[(size_t) (2 * k + 1)] = 0.0f;
        }
        fft.performRealOnlyInverseTransform (fftBuf.data());

        // the frame covers input samples [t - N, t); it is played from t on,
        // so every sample leaves exactly N samples after it came in
        const int64_t t = (frameIndex + 1) * kHop;
        for (int i = 0; i < kN; ++i)
        {
            const int idx = (int) ((t + i) % (2 * kN));
            outAcc[c][(size_t) idx] += fftBuf[(size_t) i] * window[(size_t) i] * olaGain;
        }
    }

    // this frame's peaks are the next one's history
    numPrevPeaks = numPeaks;
    std::copy (peaks.begin(), peaks.begin() + numPeaks, prevPeaks.begin());

    prevMidPhase = midPhase;
}

//==============================================================================
// The card: the 12 x 12 matrix - columns = the note heard, rows = where it
// goes (to the nearest octave of that note). The diagonal is "unchanged".
//==============================================================================
class SpectralPitchQuantizerPanel : public juce::Component, private juce::Timer
{
public:
    explicit SpectralPitchQuantizerPanel (SpectralPitchQuantizerModule& m) : module (&m)
    {
        startTimerHz (15);
        syncFromModule();
    }

    void paint (juce::Graphics& g) override
    {
        const auto dim = juce::Colours::white.withAlpha (0.45f);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        for (int c = 0; c < 12; ++c)
        {
            for (int r = 0; r < 12; ++r)
            {
                auto cb = cellBounds (c, r).reduced (1.0f);
                if (current[(size_t) c] == r)
                    g.setColour (spectrum ((float) r / 11.0f));
                else
                    g.setColour (juce::Colours::white.withAlpha (c == r ? 0.13f : 0.06f));
                g.fillRoundedRectangle (cb, 2.0f);
            }

            g.setColour (dim);
            g.setFont (juce::Font (juce::FontOptions (8.0f)));
            auto lb = cellBounds (0, c);
            g.drawText (kNoteNames[c], juce::Rectangle<float> (2.0f, lb.getY(), kLeft - 5.0f, lb.getHeight()),
                        juce::Justification::centredRight, false);
            auto bb = cellBounds (c, 0);
            g.drawText (kNoteNames[c], juce::Rectangle<float> (bb.getX(), bb.getBottom() + 1.0f, bb.getWidth(), 10.0f),
                        juce::Justification::centred, false);
        }

        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (7.5f, juce::Font::bold)));
        g.drawText ("IN", juce::Rectangle<float> (2.0f, cellBounds (0, 0).getBottom() + 1.0f, kLeft - 5.0f, 10.0f),
                    juce::Justification::centredRight, false);
        g.drawText ("OUT", juce::Rectangle<float> (2.0f, 3.0f, kLeft, 9.0f), juce::Justification::centredLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override { handleMatrix (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { handleMatrix (e.position); }

private:
    static constexpr float kLeft = 28.0f;

    static juce::Colour spectrum (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return juce::Colour::fromHSV (0.75f - 0.75f * t, 0.62f, 0.95f, 1.0f);
    }

    juce::Rectangle<float> cellBounds (int col, int rowFromBottom) const
    {
        const float top = 14.0f;
        const float bottom = (float) getHeight() - 14.0f;
        const float h = (bottom - top) / 12.0f;
        const float w = ((float) getWidth() - kLeft - 4.0f) / 12.0f;
        return { kLeft + w * (float) col, top + h * (float) (11 - rowFromBottom), w, h };
    }

    void handleMatrix (juce::Point<float> p)
    {
        auto* mod = module.get();
        if (mod == nullptr)
            return;
        for (int c = 0; c < 12; ++c)
            for (int r = 0; r < 12; ++r)
                if (cellBounds (c, r).contains (p))
                {
                    if (current[(size_t) c] != r)
                    {
                        mod->setParameter ("map" + juce::String (c), (float) r);
                        syncFromModule();
                    }
                    return;
                }
    }

    void syncFromModule()
    {
        auto* mod = module.get();
        if (mod == nullptr)
            return;
        bool changed = false;
        for (int i = 0; i < 12; ++i)
        {
            const int v = juce::jlimit (0, 11, (int) std::lround (mod->getParameter ("map" + juce::String (i))));
            if (v != current[(size_t) i]) { current[(size_t) i] = v; changed = true; }
        }
        if (changed)
            repaint();
    }

    void timerCallback() override { syncFromModule(); }

    juce::WeakReference<aquanode::SynthModule> module;
    std::array<int, 12> current { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
};

std::unique_ptr<juce::Component> SpectralPitchQuantizerModule::createExtraContentComponent()
{
    return std::make_unique<SpectralPitchQuantizerPanel> (*this);
}

//==============================================================================
static ModuleDescriptor spectralPitchQuantizerDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.specquantize";
    d.displayName = "Spectral Pitch Quantizer";
    d.description =
        "A polyphonic FFT pitch quantizer modelled on Zynaptiq PITCHMAP: every tonal partial is heard as a note and "
        "moved where the matrix says (columns = the note heard, rows = where it goes, the nearest way), with the "
        "smeared, folded FFT tone of the original - a single steady note's partials move each by its own pitch, "
        "chords and gliding notes move note by note. Tolerance leaves partials this close to their semitone alone, "
        "Intonation keeps that much of their detune (vibrato only near the top), Clean drops what is not a pure "
        "partial, Glide slides newly heard notes onto their target, Warp bends the moves into one smooth, stretched "
        "frequency warp, Top Fold folds the upper harmonics onto the map one by one too (sparser, brighter, "
        "more metallic; 0 % keeps them with their note). Noise and unmoved notes pass through untouched. Latency 4096 samples.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 62;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };

    d.params = {
        makeRotary ("tolerance",  "Tolerance",  0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f),
        makeRotary ("intonation", "Intonation", 0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f),
        makeRotary ("clean",      "Clean",      0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f),
        makeRotary ("glide",      "Glide",      0.0f, 100.0f, 0.0f,  0, "%", false, 1.0f),
        makeRotary ("warp",       "Warp",       0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f)
    };
    for (int i = 0; i < 12; ++i)
        d.params.push_back (makeRotary ("map" + juce::String (i), "Map " + juce::String (kNoteNames[i]),
                                        0.0f, 11.0f, (float) SpectralPitchQuantizerModule::kDefaultMap[i],
                                        1, {}, false, 1.0f).hide().noMod());
    // after the map, so the indices of saved patches stay put
    d.params.push_back (makeRotary ("topFold", "Top Fold", 0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f));
    return d;
}

AQUANODE_REGISTER_MODULE (SpectralPitchQuantizerModule, spectralPitchQuantizerDescriptor)