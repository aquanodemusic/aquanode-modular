#include "SpectralPitchQuantizerModule.h"

using namespace aquanode;

// C C# D D# E F F# G G# A A# B  ->  G# G# A# A# C# C# C# D# D# D# F# F#
const int SpectralPitchQuantizerModule::kDefaultMap[12] = { 8, 8, 10, 10, 1, 1, 1, 3, 3, 3, 6, 6 };

const char* const SpectralPitchQuantizerModule::kScaleNames[kNumScales] = {
    "Custom", "Major", "Minor", "Penta Major", "Penta Minor", "Blues", "Dorian", "Phrygian", "Lydian",
    "Mixolydian", "Locrian", "Harmonic Minor", "Melodic Minor", "Whole Tone", "Hirajoshi", "Major Triad",
    "Minor Triad", "Chromatic" };

namespace
{
    // each scale's notes above its root, as a 12-bit mask (bit n = n semitones up)
    constexpr int scaleMask (std::initializer_list<int> notes)
    {
        int m = 0;
        for (int n : notes) m |= 1 << n;
        return m;
    }
    const int kScaleMasks[SpectralPitchQuantizerModule::kNumScales] = {
        0,                                         // Custom: the map as it is
        scaleMask ({ 0, 2, 4, 5, 7, 9, 11 }),      // Major
        scaleMask ({ 0, 2, 3, 5, 7, 8, 10 }),      // Minor
        scaleMask ({ 0, 2, 4, 7, 9 }),             // Penta Major
        scaleMask ({ 0, 3, 5, 7, 10 }),            // Penta Minor
        scaleMask ({ 0, 3, 5, 6, 7, 10 }),         // Blues
        scaleMask ({ 0, 2, 3, 5, 7, 9, 10 }),      // Dorian
        scaleMask ({ 0, 1, 3, 5, 7, 8, 10 }),      // Phrygian
        scaleMask ({ 0, 2, 4, 6, 7, 9, 11 }),      // Lydian
        scaleMask ({ 0, 2, 4, 5, 7, 9, 10 }),      // Mixolydian
        scaleMask ({ 0, 1, 3, 5, 6, 8, 10 }),      // Locrian
        scaleMask ({ 0, 2, 3, 5, 7, 8, 11 }),      // Harmonic Minor
        scaleMask ({ 0, 2, 3, 5, 7, 9, 11 }),      // Melodic Minor
        scaleMask ({ 0, 2, 4, 6, 8, 10 }),         // Whole Tone
        scaleMask ({ 0, 2, 3, 7, 8 }),             // Hirajoshi
        scaleMask ({ 0, 4, 7 }),                   // Major Triad
        scaleMask ({ 0, 3, 7 }),                   // Minor Triad
        0xfff };                                   // Chromatic
}

void SpectralPitchQuantizerModule::setParameter (const juce::String& id, float value)
{
    const bool wasStamping = stamping;
    SynthModule::setParameter (id, value);
    if (wasStamping)
        return;
    if (id == "root" || id == "scale")
        stampScale();
    else if (id.startsWith ("map") && std::lround (getParameter ("scale")) != 0)
    {
        // a cell moved by hand (or a patch's map): no longer the scale - unless
        // it still is exactly what the scale stamps
        int want[12];
        const int mask = kScaleMasks[juce::jlimit (0, kNumScales - 1, (int) std::lround (getParameter ("scale")))];
        const int root = juce::jlimit (0, 11, (int) std::lround (getParameter ("root")));
        bool same = true;
        for (int pc = 0; pc < 12; ++pc)
        {
            int best = pc, bestDist = 99;
            for (int d = 0; d < 12; ++d)
                if (mask & (1 << d))
                {
                    const int target = (root + d) % 12;
                    int dist = target - pc;
                    if (dist > 6) dist -= 12;
                    if (dist < -6) dist += 12;
                    if (std::abs (dist) < std::abs (bestDist) || (std::abs (dist) == std::abs (bestDist) && dist > bestDist))
                        { bestDist = dist; best = target; }
                }
            want[pc] = best;
            if (std::lround (getParameter ("map" + juce::String (pc))) != want[pc]) same = false;
        }
        if (! same)
            SynthModule::setParameter ("scale", 0.0f);
    }
}

void SpectralPitchQuantizerModule::stampScale()
{
    const int scale = juce::jlimit (0, kNumScales - 1, (int) std::lround (getParameter ("scale")));
    if (scale == 0)
        return;   // Custom: the map stays
    const int mask = kScaleMasks[scale];
    const int root = juce::jlimit (0, 11, (int) std::lround (getParameter ("root")));
    const juce::ScopedValueSetter<bool> svs (stamping, true);
    for (int pc = 0; pc < 12; ++pc)
    {
        // the nearest note of the scale, the nearest way round (a tie goes up)
        int best = pc, bestDist = 99;
        for (int d = 0; d < 12; ++d)
            if (mask & (1 << d))
            {
                const int target = (root + d) % 12;
                int dist = target - pc;
                if (dist > 6) dist -= 12;
                if (dist < -6) dist += 12;
                if (std::abs (dist) < std::abs (bestDist) || (std::abs (dist) == std::abs (bestDist) && dist > bestDist))
                    { bestDist = dist; best = target; }
            }
        SynthModule::setParameter ("map" + juce::String (pc), (float) best);
    }
}

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
        dryRing[c].assign ((size_t) kN, 0.0f);
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
    airLast = -1.0f;
    reset();
}

// Air's filters: the shelf follows the knob; the band (the mids and up, which
// the harmonics are made from) and the top they are kept above stay put
void SpectralPitchQuantizerModule::updateAir (float air)
{
    const double sr = sampleRate;
    auto biquad = [sr] (BQ& f, int type, double fc, double q, double gainDb)
    {
        fc = juce::jlimit (20.0, sr * 0.45, fc);
        const double w = juce::MathConstants<double>::twoPi * fc / sr, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;
        if (type == 0)   // high shelf
        {
            const double A = std::pow (10.0, gainDb / 40.0), sq = 2.0 * std::sqrt (A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sq);
            a0 = (A + 1) - (A - 1) * cw + sq;       a1 = 2 * ((A - 1) - (A + 1) * cw);      a2 = (A + 1) - (A - 1) * cw - sq;
        }
        else             // high-pass
        {
            b0 = (1 + cw) * 0.5; b1 = -(1 + cw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        }
        f.b0 = (float) (b0 / a0); f.b1 = (float) (b1 / a0); f.b2 = (float) (b2 / a0);
        f.a1 = (float) (a1 / a0); f.a2 = (float) (a2 / a0);
    };
    for (int c = 0; c < 2; ++c)
    {
        biquad (airShelf[c], 0, 2500.0, 0.6, 18.0 * juce::jmin (1.0f, air * 1.4f));
        biquad (airBand[c], 1, 500.0, 0.707, 0.0);
        biquad (airTop[c], 1, 2500.0, 0.707, 0.0);
    }
    airLast = air;
}

void SpectralPitchQuantizerModule::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
        std::fill (dryRing[c].begin(), dryRing[c].end(), 0.0f);
        std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f);
        airShelf[c].z1 = airShelf[c].z2 = airBand[c].z1 = airBand[c].z2 = airTop[c].z1 = airTop[c].z2 = 0.0f;
        airEnv[c] = 0.0f;
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
    // Dry/Wet against the input as it was kN samples ago (the ring's oldest
    // sample), so the two stay in step; Wet Gain lifts the resynthesis
    const float mix = juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f);
    const float wetG = mix * juce::Decibels::decibelsToGain (param (pWetGain));
    const float air = juce::jlimit (0.0f, 1.0f, param (pAir) * 0.01f);
    if (std::abs (air - airLast) > 0.002f)
        updateAir (air);
    const float harmonics = juce::jmax (0.0f, air - 0.25f) / 0.75f * 2.5f;   // from a quarter up
    const float envA = 1.0f - std::exp (-1.0f / (0.01f * (float) sampleRate));
    for (int c = 0; c < 2; ++c)
    {
        // Air: what the quantizer hears gets a brighter top (the dry path not)
        float x = inputs[0][(size_t) c];
        if (air > 0.0f)
        {
            const float band = airBand[c].process (x);
            airEnv[c] += envA * (std::abs (band) - airEnv[c]);
            float y = airShelf[c].process (x);
            if (harmonics > 0.0f && airEnv[c] > 1.0e-6f)
            {
                // harmonics of the mids and up, as strong at any level: the band
                // driven into a curve against its own envelope, its top kept
                const float e = airEnv[c] * 1.6f;
                const float shaped = std::tanh (8.0f * band / e) * e;
                y += harmonics * airTop[c].process (shaped - band);
            }
            x = y;
        }
        const float dry = dryRing[c][(size_t) ringPos];
        dryRing[c][(size_t) ringPos] = inputs[0][(size_t) c];
        inRing[c][(size_t) ringPos] = x;
        outputs[0][(size_t) c] = outAcc[c][(size_t) outIdx] * wetG + dry * (1.0f - mix);
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
    // Mono (0) and Multi (1) exactly as before 6.2's additions; Bright (2) is
    // Multi with Pass Quiet, Flicker, Snap and Midi In
    const int engineChoice = juce::jlimit (0, 2, (int) std::lround (param (pEngine)));
    const bool engineB = engineChoice != 0;
    const bool bright = engineChoice == 2;
    if ((int) engineB != engineNow)
    {
        // the other engine's bookkeeping means nothing to this one
        engineNow = (int) engineB;
        numPrevPeaks = 0;
        polyHold = 0;
        noteShift.fill (0.0f);
        noteLastSeen.fill (-100);
    }

    Controls ctl;
    // Tolerance: a dead zone around each semitone, 0..50 cents (50 % = 16.5 c:
    // the folded residue the original leaves on a vibrato it corrects)
    ctl.tolSemis = 0.5f * std::pow (param (pTolerance) * 0.01f, 1.6f);
    ctl.keep = param (pIntonation) * 0.01f;
    const float clean = ctl.clean = param (pClean) * 0.01f;
    const float glide = param (pGlide) * 0.01f;
    // Warp, engine A, as the original's Electrify first measured: 0 = every partial
    // of a single note moved on its own (a gliding one - vibrato, slides -
    // partly as one harmonic whole); towards 50 % a gliding note moves wholly
    // as one; from 50 % to 100 % the moves bend into one smooth, stretched
    // frequency warp over the spectrum ("electric"). Chords always move note
    // by note. (Engine B: see assignShiftsB.)
    ctl.warpKnob = param (pWarp) * 0.01f;
    ctl.noteMix = ctl.warpKnob <= 0.5f ? ctl.warpKnob : 1.0f - ctl.warpKnob;      // up to 0.5 at 50 %
    ctl.warpSmooth = ctl.warpKnob <= 0.5f ? 0.0f : std::pow ((ctl.warpKnob - 0.5f) * 2.0f, 2.0f);
    // Top Fold, engine A: 0 = upper harmonics go with their note (a smooth,
    // dense top); 100 % = every harmonic above the 24th of a chord note, and
    // every one above the 5th of a single note, folds onto the map by its own
    // pitch - the earlier, sparser, brighter version of this module
    ctl.topFold = param (pTopFold) * 0.01f;

    for (int pc = 0; pc < 12; ++pc)
    {
        const int outPc = juce::jlimit (0, 11, (int) std::lround (param (pMap0 + pc)));
        ctl.offsets[pc] = engineB ? offsetForB (pc, outPc) : offsetFor (pc, outPc);
    }
    // Midi In (Bright only): a held note's pitch class is let free
    if (bright)
    {
        int held = 0;
        for (int n = 0; n < 128; ++n)
            if (heldNotes[(size_t) n] != 0)
                held |= 1 << (n % 12);
        // Snap: every pitch class mapped onto itself is tuned onto its semitone -
        // except those Midi In lets free, which pass as they are
        ctl.snapMask = param (pSnap) > 0.5f ? (0xfff & ~held) : 0;
        if (held != 0)
            for (int pc = 0; pc < 12; ++pc)
                if ((held >> pc) & 1)
                    ctl.offsets[pc] = 0;
    }

    const float hopSec = (float) kHop / sr;
    ctl.devSlowA = 1.0f - std::exp (-hopSec / 0.25f);   // 250 ms
    ctl.motionA = 1.0f - std::exp (-hopSec / 0.06f);    // 60 ms
    const float glideTau = 0.4f * glide * glide;        // 0 / 100 ms / 400 ms
    ctl.glideA = glideTau <= 1.0e-4f ? 1.0f : 1.0f - std::exp (-hopSec / glideTau);

    // What counts as a partial: it stands this far above the floor around it.
    // Engine B's Clean opens that gate from its middle up, until at 100 %
    // every bump of the spectrum is taken for a partial and put on the map
    const float tonalFactor = engineB && clean > 0.5f ? 8.0f - 6.4f * (clean - 0.5f) * 2.0f : 8.0f;

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

    const int bassBin = 2 * (int) (300.0f * (float) kN / sr);   // ~600 Hz: full strictness from here up
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
        // (engine B, in the bass: the partials of a very low note stand less
        // than three bins apart, their lobes fill the valleys between them -
        // 4.5x is all they rise above the floor; the original moves them too)
        const float factor = engineB && k < bassBin ? juce::jmin (tonalFactor, 4.5f + 3.5f * juce::jmax (0.0f, (float) (k - bassBin / 2) / (float) (bassBin / 2)))
                                                    : tonalFactor;
        if (m < factor * medianScratch[(size_t) (n / 5)])
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

    // ---- where every partial goes: the engine's own rules --------------------
    if (engineB)
        assignShiftsB (ctl);
    else
        assignShiftsA (ctl);

    // ---- Pass Quiet (6.2): partials far under the loudest one stay where
    // they are, as the original leaves them (it moves what lies within ~36 dB of
    // the loudest partial and leaves what lies more than ~48 dB under it: the
    // weak top of a filtered sound keeps its own pitch, and sounds brighter).
    // At 0 nothing here runs: both engines exactly as before.
    {
        const float quietKnob = bright ? param (pQuiet) * 0.01f : 0.0f;
        if (quietKnob > 0.0005f && numPeaks > 0)
        {
            float loudest = 0.0f;
            for (int i = 0; i < numPeaks; ++i)
                loudest = juce::jmax (loudest, peaks[(size_t) i].mag);
            // held, let go at ~20 dB a second (a decaying note does not flip its partials at once)
            const float fall = std::pow (10.0f, -20.0f / 20.0f * (float) kHop / (float) sampleRate);
            quietRef = juce::jmax (loudest, quietRef * fall);
            // 1 % leaves only what is ~63 dB under, 50 % ~39 dB (the original), 100 % ~15 dB
            const float thrDb = -63.0f + 48.0f * quietKnob;
            for (int i = 0; i < numPeaks; ++i)
            {
                auto& p = peaks[(size_t) i];
                const Peak* q = p.prev >= 0 ? &prevPeaks[(size_t) p.prev] : nullptr;
                const float rel = juce::Decibels::gainToDecibels (p.mag / juce::jmax (1.0e-20f, quietRef), -200.0f);
                // a little hysteresis: no flicker at the edge
                const bool wasQuiet = q != nullptr && q->quiet;
                p.quiet = rel < thrDb + (wasQuiet ? 3.0f : -3.0f);
                if (p.quiet)
                {
                    p.shift = 0.0f;
                    p.shiftB = 0.0f;
                    p.mixB = 0.0f;
                    p.cur = 0.0f;
                }
            }
        }
        else
        {
            quietRef = 0.0f;
            for (int i = 0; i < numPeaks; ++i)
                peaks[(size_t) i].quiet = false;
        }
    }

    // ---- Flicker (6.2): the original's own unsteadiness. A partial near the
    // middle between two semitones (the 11th harmonic, 49 cents from both)
    // is not sure which note it is: it now and then goes where its other
    // neighbouring note goes, and back - the shimmer two originals on the same
    // sound never agree on. The nearer the middle, the more it flips; the knob
    // sets how often. At 0 nothing here runs: both engines exactly as before.
    {
        const float flickerKnob = bright ? param (pFlicker) * 0.01f : 0.0f;
        if (flickerKnob > 0.0005f)
        {
            for (int i = 0; i < numPeaks; ++i)
            {
                auto& p = peaks[(size_t) i];
                const Peak* q = p.prev >= 0 ? &prevPeaks[(size_t) p.prev] : nullptr;
                p.flipped = q != nullptr && q->flipped;
                // (a quiet partial stays put; one riding in a louder one's lump goes with it)
                if (p.quiet || (engineB && p.absorber >= 0))
                {
                    p.flipped = false;
                    continue;
                }
                const float midi = 69.0f + 12.0f * (p.logf - std::log2 (440.0f));
                const float near = std::round (midi);
                const float dev = std::abs (midi - near);                        // 0 .. 0.5
                const float doubt = juce::jlimit (0.0f, 1.0f, (dev - 0.2f) / 0.3f);   // from 20 cents off, most at 50
                // per frame (~21 ms at 48 kHz): a flip now and then, back again a little later
                const float chance = flickerKnob * doubt * doubt * 0.35f;
                if (doubt <= 0.0f)
                    p.flipped = false;
                else if (flickerRandom.nextFloat() < chance)
                    p.flipped = ! p.flipped;
                if (! p.flipped)
                    continue;
                const int other = juce::jlimit (0, 127, (int) near + (midi > near ? 1 : -1));
                const float landing = (float) other + (float) ctl.offsets[other % 12];
                p.shift = landing - midi;
                p.shiftB = 0.0f;
                p.mixB = 0.0f;
            }
        }
        else
            for (int i = 0; i < numPeaks; ++i)
                peaks[(size_t) i].flipped = false;
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
    const float regionShare = juce::jlimit (0.0f, 1.0f, (clean - 0.5f) * 2.0f);   // engine B
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

        // Engine B: partials that land on one pitch (within a third of a bin)
        // become one oscillator - the lowest of them - at a level between the
        // sum of their powers and the sum of their amplitudes, where
        // the original's measure (ten harmonics of a low saw folded onto one note
        // come out 14 dB above each, its octave bands 1 to 4 kHz 3 dB above
        // the dry saw's: the bright, folded top)
        if (engineB)
        {
            int count = 0;
            for (int i = 0; i < numPeaks; ++i)
            {
                peakGain[(size_t) i] = 1.0f;
                const auto& p = peaks[(size_t) i];
                if (! moves (p))
                    continue;
                const float kIn = hannKernel (p.freq / binW - (float) p.bin);
                peakScale[(size_t) i] = kIn > 1.0e-6f ? std::abs (in[(size_t) p.bin]) / kIn : 0.0f;
                landing[(size_t) count++] = { p.freq * std::exp2 (p.shift / 12.0f) / binW, i };
            }
            std::sort (landing.begin(), landing.begin() + count,
                       [] (const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first < y.first; });
            for (int a = 0; a < count;)
            {
                int b = a + 1;
                while (b < count && landing[(size_t) b].first - landing[(size_t) (b - 1)].first < 0.34f)
                    ++b;
                if (b - a > 1)
                {
                    int lead = landing[(size_t) a].second;
                    float sum = 0.0f, power = 0.0f;
                    for (int j = a; j < b; ++j)
                    {
                        const int idx = landing[(size_t) j].second;
                        lead = juce::jmin (lead, idx);
                        sum += peakScale[(size_t) idx];
                        power += peakScale[(size_t) idx] * peakScale[(size_t) idx];
                        peakGain[(size_t) idx] = 0.0f;
                    }
                    const float level = std::pow (sum, 0.55f) * std::pow (power, 0.225f);
                    peakGain[(size_t) lead] = peakScale[(size_t) lead] > 1.0e-9f ? level / peakScale[(size_t) lead] : 1.0f;
                }
                a = b;
            }
        }

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
                float lobeGain = engineB ? peakGain[(size_t) i] : 1.0f;
                if (engineB && regionShare > 0.0f && lobeGain > 0.0f)
                {
                    // Clean's upper half drops what travels with a partial - so
                    // that a noisy bump does not just get quieter, the pure
                    // tone that is left takes over the level of all it stood for
                    float had = 0.0f, ideal = 0.0f;
                    for (int k = p.lo; k <= p.hi; ++k)
                    {
                        had += std::norm (in[(size_t) k]);
                        const float lobe = scale * hannKernel (fIn - (float) k);
                        ideal += lobe * lobe;
                    }
                    if (ideal > 1.0e-18f)
                        lobeGain *= 1.0f + regionShare * (juce::jlimit (1.0f, 4.0f, std::sqrt (had / ideal)) - 1.0f);
                }
                if (lobeGain > 0.0f)
                    for (int m = -kLobe; m <= kLobe; ++m)
                    {
                        const int j = newBin + m;
                        if (j < 1 || j >= kBins - 1)
                            continue;
                        const float dlt = fOut - (float) j;
                        out[(size_t) j] += polarSigned (weight * lobeGain * scale * hannKernel (dlt), psi + piF * dlt);
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
// Engine A (the module up to 6.0): where every partial goes
void SpectralPitchQuantizerModule::assignShiftsA (const Controls& c)
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    const float sr = (float) sampleRate;
    const int* offsets = c.offsets;
    const float tolSemis = c.tolSemis, keep = c.keep, glideA = c.glideA, devSlowA = c.devSlowA, motionA = c.motionA;
    const float noteMix = c.noteMix, warpSmooth = c.warpSmooth, topFold = c.topFold;

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
        // comes back near 100 % - as the original's Feel does. Inside the
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
        // retuning either (the original is a mapper, not a tuner: its identity
        // map keeps a 30-cent-sharp tone 30 cents sharp)
        const float target = c.targetFor (note % 12, keptDev, dev);

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
    // the original measures as: a lone saw's 3rd harmonic goes where its own
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
                // a gliding note's partials go with the note (the original: at
                // Electrify 0.5 a vibrato comes out as one clean harmonic
                // note, while a steady tone's partials still move each alone)
                const float motion = best >= 0 ? peaks[(size_t) best].motion : p.motion;
                const float moving = juce::jlimit (0.0f, 1.0f, (motion - 0.7f) / 1.5f);
                // and a steady note's upper harmonics go with it too: from the
                // 6th to the 16th they hand over from their own pitch to the
                // note's (the original keeps a steady saw's top as the mapped
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
}

//==============================================================================
// Engine B (6.1): where every partial goes, after the original 1.x measured again
// on a second, wider test set (Nov 2026: sines, saws from C0 to B11, chords,
// plucks, noise and drums, through eighteen settings of its five knobs).
//
// What those renders show, and what this engine does:
//
//  - It only moves what it can tell apart. The original listens with long windows
//    in the bass and ever shorter ones above (4096 below ~500 Hz, 2048 up to
//    ~7 kHz, 1024 above, at 48 kHz); partials that stand closer than about
//    2.5 of those bins - the top of a low note, the crowded upper harmonics of
//    a low chord - are not moved at all and pass as they came in. Engine A
//    moved them anyway, and where many folded onto one note their sum
//    collapsed (a saw's top came out up to 10 dB down). Here the same limits
//    decide: 30 Hz of room below ~500 Hz, 60 Hz up to ~7 kHz, 120 Hz above.
//
//  - A note from A#3 up carries its harmonics with it: they all move by the
//    note's own interval, as one rigid harmonic series, crowded or not
//    (the original: every saw from A#3 up). Lower notes' partials go each where
//    its own pitch maps, the folded, bell-like bass of the original.
//
//  - A partial has to be heard for two frames before it moves, and one that
//    moves keeps moving while it is there (a quarter less room is enough to
//    stay): no flicker between moved and unmoved, which is what the original
//    itself does on low notes.
//
//  - A pitch that races - a kick drum's sweep - is not a note and passes.
//
//  - Partials that land on the same pitch become one, a little louder than
//    their powers add up to (the original's bright, folded top), instead of
//    cancelling each other.
//
// The knobs: Tolerance and Intonation as in engine A. Clean, below its
// middle, is engine A's (how much of a moved partial's noise travels with
// it); from the middle up it also opens the gates - less room needed, sooner,
// weaker bumps count - until at 100 % the whole spectrum is on the map, noise
// included (the original's Purify: the organ-like end). Warp lowers the note
// from which harmonics travel with their note: A#3 at 0, B0 at 100 %, where
// every note keeps its shape (Electrify). Top Fold undoes that from the top
// down: at 100 % every harmonic goes by its own pitch.
void SpectralPitchQuantizerModule::assignShiftsB (const Controls& c)
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    const float sr = (float) sampleRate;
    const float binHz = sr / (float) kN;
    const float hzPerRad = sr / twoPi;

    const float open = juce::jlimit (0.0f, 1.0f, (c.clean - 0.5f) * 2.0f);
    const float roomScale = c.clean <= 0.5f ? 1.0f - 0.6f * c.clean : 0.7f * (1.0f - open);
    const int needAge = c.clean <= 0.5f ? 2 : (open < 0.5f ? 1 : 0);
    const int groupNote = (int) std::lround (58.0f - 35.0f * juce::jlimit (0.0f, 1.0f, c.warpKnob));
    const float unfolded = (1.0f - c.topFold) * (1.0f - c.topFold);
    const int foldFrom = c.topFold <= 0.001f ? 1000 : 2 + (int) std::lround (unfolded * 22.0f);
    constexpr int kMaxHarm = 24;

    // one of the original's bins at this pitch, in Hz (its windows: 4096, 2048, 1024)
    auto theirBin = [binHz] (float hz)
    {
        const float low = binHz, mid = 2.0f * binHz, high = 4.0f * binHz;
        const float a = 34.0f * binHz, b = 51.0f * binHz;       // ~400 .. 600 Hz
        const float c2 = 478.0f * binHz, d = 683.0f * binHz;    // ~5.6 .. 8 kHz
        if (hz <= a)  return low;
        if (hz < b)   return low + (mid - low) * (hz - a) / (b - a);
        if (hz <= c2) return mid;
        if (hz < d)   return mid + (high - mid) * (hz - c2) / (d - c2);
        return high;
    };

    // ---- 1: each partial's note, its own way on the map, and whether it
    // stands clear of its neighbours
    for (int i = 0; i < numPeaks; ++i)
    {
        auto& p = peaks[(size_t) i];
        const Peak* q = p.prev >= 0 ? &prevPeaks[(size_t) p.prev] : nullptr;
        const float hz = juce::jmax (1.0f, p.freq * hzPerRad);
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        int note = juce::jlimit (0, 127, (int) std::lround (midi));
        if (q != nullptr && std::abs (midi - (float) q->note) < 0.7f)
            note = q->note;
        const float dev = midi - (float) note;

        p.devSlow = dev;
        if (q != nullptr && q->note == note)
            p.devSlow = q->devSlow + (dev - q->devSlow) * c.devSlowA;
        const float keptDev = std::abs (dev) < c.tolSemis
                                  ? dev
                                  : c.keep * p.devSlow + c.keep * c.keep * c.keep * c.keep * (dev - p.devSlow);
        p.note = note;
        p.logf = std::log2 (hz);
        // how fast its pitch runs, cents per hop (~60 ms smoothed)
        p.motion = 0.0f;
        if (q != nullptr)
        {
            const float inst = std::abs (1200.0f * std::log2 (juce::jmax (1.0e-6f, p.freq) / juce::jmax (1.0e-6f, q->freq)));
            p.motion = q->motion + (juce::jmin (inst, 200.0f) - q->motion) * c.motionA;
        }
        p.mixB = 0.0f;
        p.shiftB = 0.0f;
        p.own = c.targetFor (note % 12, keptDev, dev);
        p.age = q != nullptr ? juce::jmin (q->age + 1, 100000) : 0;
        p.absorber = -1;

        // Two partials closer than the original could ever tell apart are one lump
        // to it: the stronger takes the weaker along. Others within 2.2 of
        // its bins crowd a partial: it is not clear.
        const bool was = q != nullptr && q->accepted;
        const float bin = theirBin (hz);
        const float room = 2.2f * bin * roomScale * (was ? 0.75f : 1.0f);
        const float lump = 0.8f * bin;
        bool clear = true;
        auto meet = [&] (int j)
        {
            const auto& n = peaks[(size_t) j];
            const float apart = std::abs (n.freq - p.freq) * hzPerRad;
            if (apart >= juce::jmax (room, lump))
                return false;
            if (apart < lump)
            {
                if (n.mag > p.mag && p.absorber < 0)
                    p.absorber = j;
            }
            else if (n.mag >= 0.1f * p.mag)
                clear = false;
            return true;
        };
        for (int j = i - 1; j >= 0 && meet (j); --j) {}
        for (int j = i + 1; j < numPeaks && meet (j); ++j) {}
        p.clear = clear && p.absorber < 0;
    }

    // ---- 2: from the bottom up, harmonics find the note they belong to
    int heads[48];
    int numHeads = 0;
    for (int i = 0; i < numPeaks; ++i)
    {
        auto& p = peaks[(size_t) i];
        const Peak* q = p.prev >= 0 ? &prevPeaks[(size_t) p.prev] : nullptr;
        const bool was = q != nullptr && q->accepted;
        p.rootNote = -1;
        p.rootK = 0;
        p.hold = 0;
        p.accepted = false;
        p.target = p.cur = p.shift = 0.0f;
        // it has to be heard for a moment before it moves - and a pitch that
        // races (a kick drum's sweep: over ~2800 cents a second, a vibrato
        // stays far below) is no note: moved, it would only tear
        if (p.absorber >= 0 || ! (was || p.age >= needAge) || p.motion > (was ? 36.0f : 30.0f))
            continue;

        float target = p.own;
        int best = -1, bestK = 0;
        float bestScore = 0.0f;
        for (int h = 0; h < numHeads; ++h)
        {
            const auto& r = peaks[(size_t) heads[h]];
            const float ratio = p.freq / r.freq;
            const int k = (int) std::lround (ratio);
            if (k < 2 || k > kMaxHarm || k >= foldFrom)
                continue;
            if (std::abs (1200.0f * std::log2 (ratio / (float) k)) > juce::jmin (30.0f, 10.0f + 1.5f * (float) k))
                continue;
            float score = r.mag / (float) k;
            if (q != nullptr && q->rootNote == r.note && q->rootK == k)
                score *= 4.0f;                      // it stays with the note it had
            if (score > bestScore) { bestScore = score; best = heads[h]; bestK = k; }
        }

        if (best >= 0)
        {
            // a harmonic of a note that moves: it goes along, crowded or not
            target = peaks[(size_t) best].target;
            p.rootNote = peaks[(size_t) best].note;
            p.rootK = bestK;
            p.hold = 8;
        }
        else if (was && q->rootNote >= 0 && q->hold > 0)
        {
            // its note dropped out for a moment (~85 ms at most): it stays put
            target = q->target;
            p.rootNote = q->rootNote;
            p.rootK = q->rootK;
            p.hold = q->hold - 1;
        }
        else
        {
            if (! p.clear)
                continue;
            // A note of its own - and one that carries its harmonics, if it is
            // high enough and not itself the harmonic of something below that
            // is at least as strong (a low note's partials stay single)
            if (p.note >= groupNote && numHeads < 48)
            {
                bool harmonic = false;
                for (int j = i - 1; j >= 0 && ! harmonic; --j)
                {
                    const auto& n = peaks[(size_t) j];
                    const float ratio = p.freq / n.freq;
                    if (ratio > 16.5f)
                        break;
                    const float k = std::round (ratio);
                    harmonic = k >= 2.0f && n.mag >= 0.8f * p.mag
                               && std::abs (1200.0f * std::log2 (ratio / k)) < 20.0f;
                }
                if (! harmonic)
                    heads[numHeads++] = i;
            }
        }

        p.accepted = true;
        p.target = target;
        // Glide: a newly heard partial starts unshifted and slides onto its target
        const float from = was ? q->cur : (c.glideA >= 1.0f ? target : 0.0f);
        p.cur = from + (target - from) * c.glideA;
        p.shift = p.cur;
    }

    // a lump's weaker partial rides in the region of the stronger one
    for (int i = 0; i < numPeaks; ++i)
    {
        const auto& a = peaks[(size_t) i];
        if (a.absorber < 0)
            continue;
        auto& b = peaks[(size_t) a.absorber];
        if (b.accepted && std::abs (b.shift) >= 0.002f)
        {
            b.lo = juce::jmin (b.lo, a.lo);
            b.hi = juce::jmax (b.hi, a.hi);
        }
    }
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
        "A polyphonic FFT pitch quantizer: every tonal partial is heard as a note and moved where the matrix "
        "says (columns = the note heard, rows = where it goes). Noise, drums and notes the matrix leaves alone "
        "pass untouched. Engines: Multi (default) moves the partials it recognizes, notes from A#3 up carry "
        "their harmonics; Mono is the denser module; Bright is Multi plus Pass Quiet (quiet partials "
        "keep their pitch, brighter), Flicker (added unsteadiness), Snap (a pill on a note's own row "
        "tunes it onto its semitone) and MIDI In (held notes pass unquantized). Tolerance and Intonation keep "
        "partials near their semitone and their detune, Clean drops the noise around moved partials, Glide "
        "slides onto new targets, Warp sets from which note harmonics travel along (Mono: bends the moves), "
        "Top Fold folds upper harmonics onto the map, Air adds highs. Latency: 4096 samples.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 62;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        midiIn   ("midiIn",   "MIDI In"),     // (6.2) held notes are let free
        audioOut ("audioOut", "Audio Out")
    };

    d.params = {
        makeRotary ("tolerance",  "Tolerance",  0.0f, 100.0f, 0.0f,  0, "%", false, 1.0f),
        makeRotary ("intonation", "Intonation", 0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f),
        makeRotary ("clean",      "Clean",      0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f),
        makeRotary ("glide",      "Glide",      0.0f, 100.0f, 0.0f,  0, "%", false, 1.0f),
        makeRotary ("warp",       "Warp",       0.0f, 100.0f, 0.0f,  0, "%", false, 1.0f)
    };
    for (int i = 0; i < 12; ++i)
        d.params.push_back (makeRotary ("map" + juce::String (i), "Map " + juce::String (kNoteNames[i]),
                                        0.0f, 11.0f, (float) SpectralPitchQuantizerModule::kDefaultMap[i],
                                        1, {}, false, 1.0f).hide().noMod());
    // after the map, so the indices of saved patches stay put
    d.params.push_back (makeRotary ("topFold", "Top Fold", 0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f));
    // (after the map, too). Wet Gain lifts the resynthesised signal, which came
    // out quieter than what goes in; patches from before keep their level
    d.params.push_back (makeRotary ("mix",     "Dry/Wet",  0.0f, 100.0f, 100.0f, 2, "%", false, 1.0f));
    // (6.1: both start at nothing - engine B comes out as loud and as bright
    // as what goes in; the original's own starting point for mapping is Threshold
    // and Electrify at 0, Feel and Purify at their middle)
    d.params.push_back (makeRotary ("wetGain", "Wet Gain", -12.0f, 24.0f, 0.0f, 2, "dB").whenMissing (0.0f));
    d.params.push_back (makeRotary ("air",     "Air",      0.0f, 100.0f, 0.0f, 2, "%").whenMissing (0.0f));
    // Root and Scale (stamp the map; Custom leaves it as drawn)
    {
        juce::StringArray roots, scales;
        for (auto* n : kNoteNames) roots.add (n);
        for (auto* n : SpectralPitchQuantizerModule::kScaleNames) scales.add (n);
        d.params.push_back (makeCombo ("root",  "Root",  roots, 0, 1, 1).noMod().whenMissing (0.0f));
        d.params.push_back (makeCombo ("scale", "Scale", scales, 0, 1, 2).noMod().whenMissing (0.0f));
    }
    // Engine: Multi (B, 6.1's) for new ones; a patch saved before it keeps Mono (A), and sounds as it did
    // (6.2.1: a third, Bright, holds 6.2's additions; Mono and Multi are as they were)
    d.params.push_back (makeCombo ("engine", "Engine", { "Mono", "Multi", "Bright" }, 1, 1, 2).noMod().whenMissing (0.0f));
    // Bright only: Pass Quiet (at the original's 50 %), Flicker and Snap
    d.params.push_back (makeRotary ("passQuiet", "Pass Quiet", 0.0f, 100.0f, 50.0f, 2, "%", false, 1.0f).whenMissing (50.0f).visibleWhen ("engine", 2.0f));
    // Flicker (6.2): off unless asked for
    d.params.push_back (makeRotary ("flicker", "Flicker", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f).whenMissing (0.0f).visibleWhen ("engine", 2.0f));
    // Snap (6.2): Off - a note whose pill is on its own row passes as it is (as before); On - it is
    // tuned onto its semitone, as in the original (Chromatic then tunes every partial)
    d.params.push_back (makeCombo ("snap", "Snap", { "Off", "On" }, 0, 1, 1).noMod().whenMissing (0.0f).visibleWhen ("engine", 2.0f));
    return d;
}

AQUANODE_REGISTER_MODULE (SpectralPitchQuantizerModule, spectralPitchQuantizerDescriptor)