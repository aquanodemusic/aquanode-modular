#include "SpectralLockModule.h"

using namespace aquanode;

const char* const SpectralLockModule::scaleNames[numScales] =
{
    "Chromatic", "Major", "Minor", "Harm Minor", "Dorian", "Phrygian",
    "Lydian", "Mixolydian", "Penta Major", "Penta Minor", "Whole Tone", "Octaves"
};

// input pitch class -> output pitch class; off-scale tones go to the nearest degree
const int SpectralLockModule::scaleTables[numScales][12] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11 },   // Chromatic
    { 0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9,11 },   // Major
    { 0, 0, 2, 3, 3, 5, 5, 7, 8, 8,10,10 },   // Natural minor
    { 0, 0, 2, 3, 3, 5, 5, 7, 8, 8,11,11 },   // Harmonic minor
    { 0, 0, 2, 3, 3, 5, 5, 7, 7, 9, 9,10 },   // Dorian
    { 0, 1, 1, 3, 3, 5, 5, 7, 8, 8,10,10 },   // Phrygian
    { 0, 0, 2, 2, 4, 4, 6, 7, 7, 9, 9,11 },   // Lydian
    { 0, 0, 2, 2, 4, 5, 5, 7, 7, 9,10,10 },   // Mixolydian
    { 0, 0, 2, 2, 4, 4, 4, 7, 7, 9, 9, 9 },   // Pentatonic major
    { 0, 0, 3, 3, 3, 5, 5, 7, 7,10,10,10 },   // Pentatonic minor
    { 0, 0, 2, 2, 4, 4, 6, 6, 8, 8,10,10 },   // Whole tone
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }    // everything to the root
};

namespace
{
    const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    float coefFromTau (float tauSeconds, double sampleRate) noexcept
    {
        return 1.0f - std::exp (-1.0f / juce::jmax (1.0f, (float) (tauSeconds * sampleRate)));
    }

    // deterministic per-band scatter, so panning/detune are stable across sessions
    float hashToBipolar (int k) noexcept
    {
        juce::uint32 x = (juce::uint32) k * 2654435761u;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
        return ((float) (x & 0xffffff) / 8388608.0f) - 1.0f;
    }

    // equal temperament exactly as before (float), the MTS-ESP master's scale when one is connected
    float mtof (float note) noexcept
    {
        return aquanode::tuning::hasMaster() ? (float) aquanode::midiNoteToHz ((double) note)
                                             : 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    }
    float ftom (float f) noexcept    { return 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, f) / 440.0f); }
}

//==============================================================================
void SpectralLockModule::prepare (double sr)
{
    SynthModule::prepare (sr);

    for (int i = 0; i <= tableSize; ++i)
        sineTable[(size_t) i] = std::sin (juce::MathConstants<float>::twoPi * (float) i / (float) tableSize);

    juce::dsp::ProcessSpec spec { sr, 1, 2 };
    loSplit.prepare (spec);
    hiSplit.prepare (spec);
    loSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    hiSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);

    heldSorted.reserve (128);
    buildFilterbank();
    reset();
}

void SpectralLockModule::reset()
{
    loSplit.reset();
    hiSplit.reset();
    for (int k = 0; k < numBands; ++k)
    {
        auto& b = bands[(size_t) k];
        b.s1a = b.s2a = b.s1b = b.s2b = 0.0f;
        b.envSq = b.amp = 0.0f;
        b.gate = 1.0f;
        b.freq = b.targetFreq = b.fc;
        b.lastOutNote = std::numeric_limits<float>::quiet_NaN();
    }
    for (auto& st : aaS) st[0] = st[1] = 0.0f;
    for (auto& ch : upS) for (auto& st : ch) st[0] = st[1] = 0.0f;
    lowPhase = 0;
    lowCpuWas = false;
    globalPeak = blockPeak = transFast = transSlow = 0.0f;
    audioGate = 1.0f;
    smMix = param (pMix) * 0.01f;
    smAmount = param (pAmount) * 0.01f;
    smLevel = param (pLevel) * 0.01f;
    smShimmer = param (pShimmer) * 0.01f;
    midiAllNotesOff();
    ctrlLeft = 0;
    firstBand = 0;
    lastBand = numBands - 1;
}

void SpectralLockModule::buildFilterbank()
{
    numBands = 0;
    const float ratio = std::pow (2.0f, 1.0f / (float) bandsPerOctave);
    for (float f = bankLowHz; f < bankHighHz && numBands < maxBands; f *= ratio)
    {
        auto& b = bands[(size_t) numBands];
        b = Band();
        b.fc = f;
        b.noteIn = ftom (f);
        b.detune = hashToBipolar (numBands * 7 + 3);
        b.freq = b.targetFreq = f;
        b.phase = 0.5f * (hashToBipolar (numBands) + 1.0f);   // scattered start phases

        // RBJ constant-skirt band-pass, unity peak gain
        const float w0 = juce::MathConstants<float>::twoPi * f / (float) sampleRate;
        const float alpha = std::sin (w0) / (2.0f * bandQ);
        const float a0 = 1.0f + alpha;
        b.b0 =  alpha / a0;
        b.b2 = -alpha / a0;
        b.a1 = (-2.0f * std::cos (w0)) / a0;
        b.a2 = (1.0f - alpha) / a0;

        // the envelope follower: roughly five cycles, never under 4 ms
        b.envCoef = coefFromTau (juce::jmax (0.004f, 5.0f / b.fc), sampleRate);

        // Low CPU's copies at half the rate
        const double halfRate = sampleRate * 0.5;
        const float lw0 = juce::MathConstants<float>::twoPi * f / (float) halfRate;
        const float lalpha = std::sin (lw0) / (2.0f * bandQ);
        const float la0 = 1.0f + lalpha;
        b.lb0 =  lalpha / la0;
        b.lb2 = -lalpha / la0;
        b.la1 = (-2.0f * std::cos (lw0)) / la0;
        b.la2 = (1.0f - lalpha) / la0;
        b.envCoefLow = coefFromTau (juce::jmax (0.004f, 5.0f / b.fc), halfRate);
        ++numBands;
    }

    // Low CPU's anti-alias filter: a 4th-order Butterworth low-pass under the
    // half rate's Nyquist, well above the top band
    lowCpuOk = sampleRate * 0.25 > (double) bankHighHz * 1.5;
    const float cutoff = (float) juce::jmin (sampleRate * 0.25 * 0.8, (double) bankHighHz * 1.6);
    const float qs[2] = { 0.5411961f, 1.3065630f };
    for (int st = 0; st < 2; ++st)
    {
        const float w0 = juce::MathConstants<float>::twoPi * cutoff / (float) sampleRate;
        const float alpha = std::sin (w0) / (2.0f * qs[st]), cs = std::cos (w0), a0 = 1.0f + alpha;
        aa[st][0] = (1.0f - cs) * 0.5f / a0;
        aa[st][1] = (1.0f - cs) / a0;
        aa[st][2] = (1.0f - cs) * 0.5f / a0;
        aa[st][3] = -2.0f * cs / a0;
        aa[st][4] = (1.0f - alpha) / a0;
    }
    lastTilt = lastWidth = std::numeric_limits<float>::quiet_NaN();
}

//==============================================================================
// MIDI
void SpectralLockModule::voiceNoteOn (int slot, int note, bool)
{
    note = juce::jlimit (0, 127, note);
    noteHeld[note] = true;
    if (slot >= 0 && slot < kMaxVoices)
        slotNote[slot] = note;
    ctrlLeft = 0;
}

void SpectralLockModule::voiceNoteOff (int slot)
{
    if (slot < 0 || slot >= kMaxVoices)
        return;
    if (slotNote[slot] >= 0)
        noteHeld[slotNote[slot]] = false;
    slotNote[slot] = -1;
    ctrlLeft = 0;
}

void SpectralLockModule::midiAllNotesOff()
{
    for (auto& h : noteHeld) h = false;
    for (auto& s : slotNote) s = -1;
}

//==============================================================================
void SpectralLockModule::updateRangeWindow (float loHz, float hiHz)
{
    if (hiHz < loHz) std::swap (loHz, hiHz);

    int lo = 0, hi = numBands - 1;
    while (lo < numBands - 1 && bands[(size_t) lo].fc < loHz) ++lo;
    while (hi > 0 && bands[(size_t) hi].fc > hiHz) --hi;
    if (hi < lo) hi = lo;

    // silence anything that last left the window so it cannot leak stale
    // energy back in when the range is widened again
    if (lo != firstBand || hi != lastBand)
        for (int k = 0; k < numBands; ++k)
            if (k < lo || k > hi)
            {
                auto& b = bands[(size_t) k];
                b.s1a = b.s2a = b.s1b = b.s2b = 0.0f;
                b.envSq = b.amp = 0.0f;
                b.gate = 0.0f;
            }

    firstBand = lo;
    lastBand = hi;
}

void SpectralLockModule::updateBandMapping()
{
    const int root = juce::jlimit (0, 11, (int) std::lround (param (pRoot)));
    const float pitch = std::round (param (pPitch));
    const float bend = param (pBend) * 0.01f * param (pBendRange);
    const float spray = param (pSpray) * 0.01f;
    const float tilt = param (pTilt) * 0.01f;
    const float widthAmt = param (pWidth) * 0.01f;

    heldSorted.clear();
    for (int i = 0; i < 128; ++i)
        if (noteHeld[i])
            heldSorted.push_back (i);
    const bool useMidi = param (pMidi) > 0.5f && ! heldSorted.empty();
    midiActive.store (useMidi, std::memory_order_relaxed);

    int matrix[12];
    for (int i = 0; i < 12; ++i)
        matrix[i] = juce::jlimit (0, 11, (int) std::lround (param (pMatrix0 + i)));

    // the tilt gain and the pan of every band only move with Tilt and Width
    if (! juce::exactlyEqual (tilt, lastTilt) || ! juce::exactlyEqual (widthAmt, lastWidth))
    {
        lastTilt = tilt;
        lastWidth = widthAmt;
        for (int k = 0; k < numBands; ++k)
        {
            auto& b = bands[(size_t) k];
            b.ampGain = juce::jlimit (0.05f, 8.0f, std::pow (b.fc / 700.0f, tilt * 0.9f));   // tilt around 700 Hz
            const float pan = juce::jlimit (-1.0f, 1.0f, hashToBipolar (k * 13 + 1) * widthAmt);
            const float a = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            b.panL = std::cos (a);
            b.panR = std::sin (a);
        }
    }

    for (int k = firstBand; k <= lastBand; ++k)
    {
        auto& b = bands[(size_t) k];

        const int n = juce::roundToInt (b.noteIn);
        float outNote;
        if (useMidi)
        {
            // magnetise to the nearest held note; bands more than half an
            // octave from every held note drop out
            int best = heldSorted.front(), bestDist = std::abs (n - best);
            for (int h : heldSorted)
                if (std::abs (n - h) < bestDist) { bestDist = std::abs (n - h); best = h; }
            b.muted = bestDist > 6;
            outNote = (float) best;
        }
        else
        {
            b.muted = false;
            const int rel = n - root;
            const int oct = (int) std::floor ((float) rel / 12.0f);
            const int pc = rel - oct * 12;
            outNote = (float) (root + oct * 12 + matrix[pc]);
        }

        outNote += pitch + bend + spray * b.detune * 0.5f;
        if (! juce::exactlyEqual (outNote, b.lastOutNote))
        {
            b.lastOutNote = outNote;
            b.targetFreq = juce::jlimit (10.0f, (float) sampleRate * 0.45f, mtof (outNote));
        }
    }
}

void SpectralLockModule::controlUpdate()
{
    const float loHz = juce::jlimit (bankLowHz, bankHighHz, param (pRangeLo));
    const float hiHz = juce::jmax (juce::jlimit (bankLowHz, bankHighHz, param (pRangeHi)), loHz + 20.0f);
    updateRangeWindow (loHz, hiHz);
    updateBandMapping();

    loSplit.setCutoffFrequency (juce::jlimit (20.0f, (float) sampleRate * 0.45f, loHz));
    hiSplit.setCutoffFrequency (juce::jlimit (20.0f, (float) sampleRate * 0.45f, hiHz));

    // GLIDE: 0.4 ms .. 500 ms
    const float glide = param (pGlide) * 0.01f;
    glideCoef = coefFromTau (0.0004f + glide * glide * 0.5f, sampleRate);
    glideCoefLow = coefFromTau (0.0004f + glide * glide * 0.5f, sampleRate * 0.5);

    // ISOLATE: the threshold rises with the dial, the release gets short
    const float isolate = param (pIsolate) * 0.01f;
    gateAttack = coefFromTau (0.001f, sampleRate);
    gateRelease = coefFromTau (juce::jmap (isolate, 0.09f, 0.0015f), sampleRate);
    gateAttackLow = coefFromTau (0.001f, sampleRate * 0.5);
    gateReleaseLow = coefFromTau (juce::jmap (isolate, 0.09f, 0.0015f), sampleRate * 0.5);

    // peak tracking and the band display, once per control block
    globalPeak = juce::jmax (blockPeak, globalPeak * 0.985f);
    gateThresh = isolate * 0.30f * juce::jmax (globalPeak, 1.0e-4f) + isolate * 2.0e-4f;

    for (auto& d : display)
        d.store (d.load (std::memory_order_relaxed) * 0.97f, std::memory_order_relaxed);
    for (int k = firstBand; k <= lastBand && numBands > 0; ++k)
    {
        const int bin = juce::jlimit (0, numDisplayBins - 1, (k * numDisplayBins) / juce::jmax (1, numBands));
        const float v = juce::jlimit (0.0f, 1.0f, std::sqrt (bands[(size_t) k].amp * bands[(size_t) k].gate * 6.0f));
        if (v > display[(size_t) bin].load (std::memory_order_relaxed))
            display[(size_t) bin].store (v, std::memory_order_relaxed);
    }
    blockPeak = 0.0f;

    transFastC = coefFromTau (0.0015f, sampleRate);
    transSlowC = coefFromTau (0.120f, sampleRate);
    agC = coefFromTau (0.010f, sampleRate);
    smCoef = coefFromTau (0.02f, sampleRate);
}

// the full-quality bank, one sample. The settings are read into locals once
// (the compiler cannot keep members in registers while the bands, floats of
// the same object, are written) and Freeze / Shimmer are decided outside the
// loop; the arithmetic per band is exactly what it always was.
template <bool Freeze, bool Shimmer>
void SpectralLockModule::runBands (float mono, float invSR, float& tonalL, float& tonalR)
{
    const float thresh = gateThresh, gA = gateAttack, gR = gateRelease, glide = glideCoef, shim = smShimmer;
    float peak = blockPeak, sumL = 0.0f, sumR = 0.0f;
    Band* const bs = bands.data();
    for (int k = firstBand; k <= lastBand; ++k)
    {
        Band& b = bs[k];

        // two cascaded band-passes (transposed direct form II, b1 = 0)
        const float b0 = b.b0, b2 = b.b2, a1 = b.a1, a2 = b.a2;
        const float y = b0 * mono + b.s1a;
        b.s1a = -a1 * y + b.s2a;
        b.s2a = b2 * mono - a2 * y;
        const float y2 = b0 * y + b.s1b;
        b.s1b = -a1 * y2 + b.s2b;
        b.s2b = b2 * y - a2 * y2;

        float amp = b.amp;
        if constexpr (! Freeze)
        {
            b.envSq += b.envCoef * (y2 * y2 - b.envSq);
            amp = std::sqrt (b.envSq) * 1.41421356f;
            b.amp = amp;
        }

        const float gTarget = (! b.muted && amp > thresh) ? 1.0f : 0.0f;
        float gate = b.gate;
        gate += (gTarget > gate ? gA : gR) * (gTarget - gate);
        b.gate = gate;

        const float freq = b.freq + glide * (b.targetFreq - b.freq);
        b.freq = freq;
        float phase = b.phase + freq * invSR;
        if (phase >= 1.0f) phase -= 1.0f;
        b.phase = phase;

        float s = sine (phase);
        if constexpr (Shimmer)
        {
            float p2 = phase * 2.0f;
            p2 -= (float) (int) p2;
            s += shim * 0.7f * sine (p2);   // phase-locked octave up
        }

        s *= amp * gate * b.ampGain;
        sumL += s * b.panL;
        sumR += s * b.panR;
        peak = juce::jmax (peak, amp);
    }
    tonalL += sumL;
    tonalR += sumR;
    blockPeak = peak;
}

void SpectralLockModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (--ctrlLeft < 0)
    {
        controlUpdate();
        ctrlLeft = kCtrlBlock - 1;
    }

    const float inL = inputs[0][0], inR = inputs[0][1];
    const float mono = 0.5f * (inL + inR);

    // dry three-way split
    float lowL, restL, midL, highL, lowR, restR, midR, highR;
    loSplit.processSample (0, inL, lowL, restL);
    hiSplit.processSample (0, restL, midL, highL);
    loSplit.processSample (1, inR, lowR, restR);
    hiSplit.processSample (1, restR, midR, highR);

    smMix     += smCoef * (param (pMix) * 0.01f - smMix);
    smAmount  += smCoef * (param (pAmount) * 0.01f - smAmount);
    smLevel   += smCoef * (param (pLevel) * 0.01f - smLevel);
    smShimmer += smCoef * (param (pShimmer) * 0.01f - smShimmer);

    const bool freeze = param (pFreeze) > 0.5f;
    const float invSR = (float) (1.0 / sampleRate);

    // the oscillator bank
    float tonalL = 0.0f, tonalR = 0.0f;
    const bool lowCpu = lowCpuOk && param (pLowCpu) > 0.5f;
    if (lowCpu != lowCpuWas)
    {
        // switching: the anti-alias filter starts from silence, the band
        // filters carry on (their states are close enough either way)
        lowCpuWas = lowCpu;
        for (auto& st : aaS) st[0] = st[1] = 0.0f;
        for (auto& ch : upS) for (auto& st : ch) st[0] = st[1] = 0.0f;
        lowPhase = 0;
    }
    const bool shimmer = smShimmer > 0.001f;
    if (lowCpu)
        processBandsLowCpu (mono, freeze, invSR, tonalL, tonalR);
    else if (freeze)
        shimmer ? runBands<true, true> (mono, invSR, tonalL, tonalR) : runBands<true, false> (mono, invSR, tonalL, tonalR);
    else
        shimmer ? runBands<false, true> (mono, invSR, tonalL, tonalR) : runBands<false, false> (mono, invSR, tonalL, tonalR);

    // transient detector on the dry mid band
    const float rect = std::abs (midL) + std::abs (midR);
    transFast += transFastC * (rect - transFast);
    transSlow += transSlowC * (rect - transSlow);
    const float trans = juce::jlimit (0.0f, 1.0f, (transFast / (transSlow + 1.0e-5f) - 1.0f) * 0.7f);

    // Isolate's hidden overall gate
    const float isolate = param (pIsolate) * 0.01f;
    audioGate += agC * ((transSlow > 6.0e-4f ? 1.0f : 0.0f) - audioGate);
    const float ag = 1.0f - isolate * (1.0f - audioGate);

    // Amount: tonal against noise
    const float noiseGain = juce::jmin (1.0f, (1.0f - smAmount) + trans * (1.0f - smAmount * 0.6f));
    const float level = smLevel * 0.55f;   // filterbank sum trim

    float wetL = tonalL * smAmount * level * ag + midL * noiseGain;
    float wetR = tonalR * smAmount * level * ag + midR * noiseGain;
    if (param (pMuteLow) < 0.5f)  { wetL += lowL;  wetR += lowR; }
    if (param (pMuteHigh) < 0.5f) { wetL += highL; wetR += highR; }

    outputs[0][0] = inL * (1.0f - smMix) + wetL * smMix;
    outputs[0][1] = inR * (1.0f - smMix) + wetR * smMix;
}

// Low CPU: the whole bank runs at half the rate. Every second sample (anti-
// aliased) goes through the band filters, the levels, gates and glides move,
// and the oscillators sing one sample of the half rate - bands that are
// silent skip their sine. The tonal sum comes back to the full rate through
// the same low-pass (zero-stuffed, so twice the level).
void SpectralLockModule::processBandsLowCpu (float mono, bool freeze, float invSR, float& tonalL, float& tonalR)
{
    auto lowPass = [this] (float x, float (*state)[2])
    {
        for (int st = 0; st < 2; ++st)
        {
            const float* c = aa[st];
            const float y = c[0] * x + state[st][0];
            state[st][0] = c[1] * x - c[3] * y + state[st][1];
            state[st][1] = c[2] * x - c[4] * y;
            x = y;
        }
        return x;
    };

    const float x = lowPass (mono, aaS);
    if ((lowPhase ^= 1) != 0)
    {
        const float step = 2.0f * invSR;
        const float topHz = (float) sampleRate * 0.225f;      // under the half rate's Nyquist
        float sumL = 0.0f, sumR = 0.0f;
        const float thresh = gateThresh, gA = gateAttackLow, gR = gateReleaseLow, glide = glideCoefLow, shim = smShimmer;
        const bool shimmer = shim > 0.001f;
        float peak = blockPeak;
        Band* const bs = bands.data();
        for (int k = firstBand; k <= lastBand; ++k)
        {
            Band& b = bs[k];
            float y = b.lb0 * x + b.s1a;
            b.s1a = -b.la1 * y + b.s2a;
            b.s2a = b.lb2 * x - b.la2 * y;
            float y2 = b.lb0 * y + b.s1b;
            b.s1b = -b.la1 * y2 + b.s2b;
            b.s2b = b.lb2 * y - b.la2 * y2;

            if (! freeze)
            {
                b.envSq += b.envCoefLow * (y2 * y2 - b.envSq);
                b.amp = std::sqrt (b.envSq) * 1.41421356f;
            }
            const float gTarget = (! b.muted && b.amp > thresh) ? 1.0f : 0.0f;
            b.gate += (gTarget > b.gate ? gA : gR) * (gTarget - b.gate);
            b.freq += glide * (b.targetFreq - b.freq);
            peak = juce::jmax (peak, b.amp);

            b.phase += juce::jmin (b.freq, topHz) * step;
            if (b.phase >= 1.0f) b.phase -= 1.0f;

            const float g = b.amp * b.gate * b.ampGain;
            if (g < 1.0e-6f)
                continue;                // silent: no sine to work out
            float s = sine (b.phase);
            if (shimmer && b.freq < topHz * 0.5f)
            {
                float p2 = b.phase * 2.0f;
                p2 -= (float) (int) p2;
                s += shim * 0.7f * sine (p2);
            }
            s *= g;
            sumL += s * b.panL;
            sumR += s * b.panR;
        }
        blockPeak = peak;
        heldL = 2.0f * sumL;
        heldR = 2.0f * sumR;
    }
    else
        heldL = heldR = 0.0f;

    tonalL = lowPass (heldL, upS[0]);
    tonalR = lowPass (heldR, upS[1]);
}

//==============================================================================
// The panel in the card: band view, scale menu, root strip and the matrix
//==============================================================================
class SpectralLockPanel : public juce::Component, private juce::Timer
{
public:
    explicit SpectralLockPanel (SpectralLockModule& m) : module (&m)
    {
        scaleBox.setTextWhenNothingSelected ("Scale...");
        for (int i = 0; i < SpectralLockModule::numScales; ++i)
            scaleBox.addItem (SpectralLockModule::scaleNames[i], i + 1);
        scaleBox.onChange = [this]
        {
            const int idx = scaleBox.getSelectedItemIndex();
            if (idx >= 0)
                if (auto* mod = module.get())
                    for (int i = 0; i < 12; ++i)
                        mod->setParameter ("matrix" + juce::String (i),
                                           (float) SpectralLockModule::scaleTables[idx][i]);
            syncFromModule();
        };
        scaleBox.setTooltip ("Stamps a scale into the matrix: every incoming note moves to the nearest note "
                             "of the scale, counted from Root");
        addAndMakeVisible (scaleBox);
        startTimerHz (24);
        syncFromModule();
    }

    void resized() override
    {
        scaleBox.setBounds (getWidth() - 104, toggleY, 104, toggleH);
    }

    void paint (juce::Graphics& g) override
    {
        const auto well = juce::Colours::black.withAlpha (0.55f);
        const auto text = juce::Colours::white.withAlpha (0.8f);
        const auto dim = juce::Colours::white.withAlpha (0.45f);
        const auto accent = juce::Colour (0xff7dd3fc);

        // band view
        auto bv = juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) bandH);
        g.setColour (well);
        g.fillRoundedRectangle (bv, 4.0f);
        {
            auto r = bv.reduced (4.0f);
            const float w = r.getWidth() / (float) SpectralLockModule::numDisplayBins;
            for (int i = 0; i < SpectralLockModule::numDisplayBins; ++i)
            {
                const float v = juce::jlimit (0.0f, 1.0f, smoothed[(size_t) i]);
                if (v < 0.002f) continue;
                const float h = v * r.getHeight();
                g.setColour (spectrum ((float) i / (float) (SpectralLockModule::numDisplayBins - 1)).withAlpha (0.3f + 0.7f * v));
                g.fillRect (juce::Rectangle<float> (r.getX() + w * (float) i, r.getBottom() - h, juce::jmax (1.0f, w - 0.5f), h));
            }
            g.setColour (dim);
            g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            g.drawText (midiOn ? "OSCILLATOR BANK  -  FOLLOWING MIDI" : "OSCILLATOR BANK", bv.reduced (6.0f, 3.0f),
                        juce::Justification::topLeft, false);
        }

        // toggles
        for (int t = 0; t < numToggles; ++t)
        {
            auto tb = toggleBounds (t);
            const bool on = toggleState[(size_t) t];
            g.setColour (on ? accent.withAlpha (0.9f) : well);
            g.fillRoundedRectangle (tb, 3.0f);
            g.setColour (on ? juce::Colour (0xff10202a) : text);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (toggleLabel (t), tb, juce::Justification::centred, false);
        }

        // matrix well
        auto mw = juce::Rectangle<float> (0.0f, (float) matrixTop, (float) getWidth(), (float) (getHeight() - matrixTop));
        g.setColour (well);
        g.fillRoundedRectangle (mw, 4.0f);

        // root strip
        for (int c = 0; c < 12; ++c)
        {
            auto rb = rootBounds (c).reduced (1.0f);
            if (c == root)
            {
                g.setColour (accent.withAlpha (0.9f));
                g.fillRoundedRectangle (rb, 3.0f);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRoundedRectangle (rb, 3.0f);
            }
            g.setColour (c == root ? juce::Colour (0xff10202a) : text);
            g.setFont (juce::Font (juce::FontOptions (9.5f, c == root ? juce::Font::bold : juce::Font::plain)));
            g.drawText (kNoteNames[c], rb, juce::Justification::centred, false);
        }
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
        g.drawText ("ROOT", juce::Rectangle<float> (2.0f, rootBounds (0).getY(), kLeft - 4.0f, rootBounds (0).getHeight()),
                    juce::Justification::centredRight, false);

        // the grid: columns = the note a band is at, rows = where it goes
        for (int c = 0; c < 12; ++c)
        {
            for (int r = 0; r < 12; ++r)
            {
                auto cb = cellBounds (c, r).reduced (1.0f);
                if (current[(size_t) c] == r)
                {
                    g.setColour (spectrum ((float) r / 11.0f));
                    g.fillRoundedRectangle (cb, 2.0f);
                }
                else
                {
                    g.setColour (juce::Colours::white.withAlpha (c == r ? 0.13f : 0.06f));
                    g.fillRoundedRectangle (cb, 2.0f);
                }
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
        g.drawText ("OUT", juce::Rectangle<float> (2.0f, (float) matrixTop + 3.0f, kLeft, 9.0f),
                    juce::Justification::centredLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int t = 0; t < numToggles; ++t)
            if (toggleBounds (t).contains (e.position))
            {
                if (auto* mod = module.get())
                    mod->setParameter (toggleParam (t), toggleState[(size_t) t] ? 0.0f : 1.0f);
                syncFromModule();
                return;
            }
        handleMatrix (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.y >= (float) matrixTop)
            handleMatrix (e.position);
    }

private:
    static constexpr int bandH = 44, toggleY = 48, toggleH = 22, matrixTop = 74, numToggles = 4;
    static constexpr float kLeft = 28.0f, kRootH = 15.0f;

    static juce::String toggleParam (int t) { return t == 0 ? "muteLow" : t == 1 ? "muteHigh" : t == 2 ? "midi" : "freeze"; }
    static juce::String toggleLabel (int t) { return t == 0 ? "MUTE LO" : t == 1 ? "MUTE HI" : t == 2 ? "MIDI" : "FREEZE"; }

    juce::Rectangle<float> toggleBounds (int t) const
    {
        const float avail = (float) getWidth() - 108.0f;
        const float w = avail / (float) numToggles;
        return { w * (float) t, (float) toggleY, w - 4.0f, (float) toggleH };
    }

    static juce::Colour spectrum (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return juce::Colour::fromHSV (0.75f - 0.75f * t, 0.62f, 0.95f, 1.0f);
    }

    juce::Rectangle<float> rootBounds (int col) const
    {
        const float w = ((float) getWidth() - kLeft - 4.0f) / 12.0f;
        return { kLeft + w * (float) col, (float) matrixTop + 14.0f, w, kRootH };
    }

    juce::Rectangle<float> cellBounds (int col, int rowFromBottom) const
    {
        const float top = (float) matrixTop + 14.0f + kRootH + 5.0f;
        const float h = ((float) getHeight() - top - 13.0f) / 12.0f;
        const float w = ((float) getWidth() - kLeft - 4.0f) / 12.0f;
        return { kLeft + w * (float) col, top + h * (float) (11 - rowFromBottom), w, h };
    }

    void handleMatrix (juce::Point<float> p)
    {
        auto* mod = module.get();
        if (mod == nullptr)
            return;
        for (int c = 0; c < 12; ++c)
        {
            if (rootBounds (c).contains (p))
            {
                mod->setParameter ("root", (float) c);
                syncFromModule();
                return;
            }
            for (int r = 0; r < 12; ++r)
                if (cellBounds (c, r).contains (p))
                {
                    if (current[(size_t) c] != r)
                    {
                        mod->setParameter ("matrix" + juce::String (c), (float) r);
                        scaleBox.setSelectedId (0, juce::dontSendNotification);
                        syncFromModule();
                    }
                    return;
                }
        }
    }

    void syncFromModule()
    {
        auto* mod = module.get();
        if (mod == nullptr)
            return;
        bool changed = false;
        const int r = juce::jlimit (0, 11, (int) std::lround (mod->getParameter ("root")));
        if (r != root) { root = r; changed = true; }
        for (int i = 0; i < 12; ++i)
        {
            const int v = juce::jlimit (0, 11, (int) std::lround (mod->getParameter ("matrix" + juce::String (i))));
            if (v != current[(size_t) i]) { current[(size_t) i] = v; changed = true; }
        }
        for (int t = 0; t < numToggles; ++t)
        {
            const bool on = mod->getParameter (toggleParam (t)) > 0.5f;
            if (on != toggleState[(size_t) t]) { toggleState[(size_t) t] = on; changed = true; }
        }
        if (changed)
        {
            // the scale menu names the matrix when it is one of its scales
            int match = -1;
            for (int sc = 0; sc < SpectralLockModule::numScales && match < 0; ++sc)
            {
                bool same = true;
                for (int i = 0; i < 12 && same; ++i)
                    same = current[(size_t) i] == SpectralLockModule::scaleTables[sc][i];
                if (same) match = sc;
            }
            if (match >= 0) scaleBox.setSelectedItemIndex (match, juce::dontSendNotification);
            else            scaleBox.setSelectedId (0, juce::dontSendNotification);
            repaint();
        }
    }

    void timerCallback() override
    {
        syncFromModule();
        auto* mod = dynamic_cast<SpectralLockModule*> (module.get());
        if (mod == nullptr)
            return;
        bool any = false;
        for (int i = 0; i < SpectralLockModule::numDisplayBins; ++i)
        {
            const float v = juce::jmax (mod->displayBin (i), smoothed[(size_t) i] * 0.82f);
            any = any || v > 0.002f || smoothed[(size_t) i] > 0.002f;
            smoothed[(size_t) i] = v;
        }
        const bool m = mod->midiIsActive();
        if (any || m != midiOn)
        {
            midiOn = m;
            repaint (0, 0, getWidth(), bandH);
        }
    }

    juce::WeakReference<aquanode::SynthModule> module;
    juce::ComboBox scaleBox;
    std::array<int, 12> current { { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
    std::array<bool, numToggles> toggleState {};
    int root { -1 };
    bool midiOn { false };
    std::array<float, SpectralLockModule::numDisplayBins> smoothed {};
};

std::unique_ptr<juce::Component> SpectralLockModule::createExtraContentComponent()
{
    return std::make_unique<SpectralLockPanel> (*this);
}

//==============================================================================
static ModuleDescriptor spectralLockDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.spectrallock";
    d.displayName = "Spectral Lock";
    d.description =
        "A filterbank resynthesizer and retuner: the mid band is split into 36 bands per octave and each "
        "band sings its level back through a sine, retuned by the matrix in the card (columns = the note it "
        "hears relative to Root, rows = where it goes; the Scale menu stamps a scale) - or, with MIDI lit, "
        "pulled to the notes held on Midi In. Amount blends the retuned tone against the noise, Isolate gates "
        "out unpitched bands, Shimmer adds an octave, Spray a fixed detune scatter, Freeze holds the bands. "
        "Mute Lo / Mute Hi drop the dry tails outside the range. Low CPU runs the band bank at half the rate "
        "for about half the processor time (off: the full-quality sound). Zero latency.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 62;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        midiIn   ("midiIn",   "Midi In"),
        audioOut ("audioOut", "Audio Out")
    };

    d.params = {
        makeRotary ("mix",     "Mix",     0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
        makeRotary ("amount",  "Amount",  0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
        makeRotary ("isolate", "Isolate", 0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f),
        makeRotary ("glide",   "Glide",   0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f),
        makeRotary ("level",   "Level",   0.0f, 200.0f, 100.0f, 0, "%", false, 1.0f),

        makeRotary ("pitch",     "Pitch",   -24.0f, 24.0f, 0.0f, 1, "st", false, 1.0f),
        makeRotary ("bend",      "Bend",    -100.0f, 100.0f, 0.0f, 1, "%", false, 1.0f),
        makeRotary ("bendRange", "Bend +-", 1.0f, 24.0f, 2.0f, 1, "st", false, 1.0f),
        makeRotary ("shimmer",   "Shimmer", 0.0f, 100.0f, 0.0f, 1, "%", false, 1.0f),
        makeRotary ("spray",     "Spray",   0.0f, 100.0f, 0.0f, 1, "%", false, 1.0f),

        makeRotary ("width", "Width", 0.0f, 200.0f, 100.0f, 2, "%", false, 1.0f),
        makeRotary ("tilt",  "Tilt",  -100.0f, 100.0f, 0.0f, 2, "%", false, 1.0f),
        // (Low CPU, row 2 as well, is appended after the matrix below)

        makeHBar ("rangeLo", "Range Low",  60.0f, 4800.0f, 65.0f, 3, "Hz", 5),
        makeHBar ("rangeHi", "Range High", 60.0f, 4800.0f, 2200.0f, 4, "Hz", 5),

        // toggles: drawn as the lit buttons in the panel
        makeCombo ("muteLow",  "Mute Low",  { "Low In", "Low Mute" }, 0, 5, 2).hide(),
        makeCombo ("muteHigh", "Mute High", { "High In", "High Mute" }, 0, 5, 2).hide(),
        makeCombo ("midi",     "MIDI",      { "Matrix", "MIDI Notes" }, 0, 5, 2).hide(),
        makeCombo ("freeze",   "Freeze",    { "Live", "Freeze" }, 0, 5, 2).hide(),

        makeRotary ("root", "Root", 0.0f, 11.0f, 0.0f, 6, {}, false, 1.0f).hide().noMod()
    };
    for (auto* id : { "rangeLo", "rangeHi" })
        for (auto& p : d.params)
            if (p.id == id)
                p.logarithmic = true;
    for (int i = 0; i < 12; ++i)
        d.params.push_back (makeRotary ("matrix" + juce::String (i), "Matrix " + juce::String (i),
                                        0.0f, 11.0f, (float) SpectralLockModule::scaleTables[SpectralLockModule::kDefaultScale][i],
                                        6, {}, false, 1.0f).hide().noMod());
    d.params.push_back (makeCombo ("lowcpu", "Low CPU", { "Off", "On" }, 0, 2, 1).noMod());   // pLowCpu
    return d;
}

AQUANODE_REGISTER_MODULE (SpectralLockModule, spectralLockDescriptor)
