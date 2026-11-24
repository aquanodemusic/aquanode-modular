#include "SpringerModule.h"

using namespace aquanode;

// per-coil delay scale spread + density delay (ms), echoing the original's
// per-coil "coil length" and alternating densityA/densityB pattern
static const float kCoilScale[SpringerModule::maxCoils] = { 1.0f, 1.13f, 0.87f, 1.27f, 0.79f, 1.41f, 0.71f };
static const float kDensityMs[SpringerModule::maxCoils] = { 5.1f, 7.9f, 5.7f, 8.7f, 4.7f, 9.3f, 6.3f };

namespace
{
    // the tank's springs: no two quite alike (length, dispersion, transition)
    constexpr float kSpringLen[4]  = { 1.0f, 0.873f, 1.137f, 0.941f };
    constexpr float kSpringDisp[4] = { 1.0f, 0.965f, 1.03f, 0.985f };
    constexpr float kSpringFc[4]   = { 1.0f, 1.035f, 0.955f, 1.06f };
}

void SpringerModule::Biquad::set (double fc, double q, double sr, bool highpass) noexcept
{
    fc = juce::jlimit (10.0, sr * 0.45, fc);
    const double w = juce::MathConstants<double>::twoPi * fc / sr;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    if (highpass)
    {
        b0 = (float) ((1.0 + cw) * 0.5 / a0);
        b1 = (float) (-(1.0 + cw) / a0);
    }
    else
    {
        b0 = (float) ((1.0 - cw) * 0.5 / a0);
        b1 = (float) ((1.0 - cw) / a0);
    }
    b2 = b0;
    a1 = (float) (-2.0 * cw / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void SpringerModule::Biquad::setHighShelf (double fc, double gainDb, double sr) noexcept
{
    fc = juce::jlimit (10.0, sr * 0.45, fc);
    const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * fc / sr;
    const double cw = std::cos (w), sq = 2.0 * std::sqrt (A) * std::sin (w) / 2.0 * std::sqrt (2.0);
    const double a0 = (A + 1.0) - (A - 1.0) * cw + sq;
    b0 = (float) (A * ((A + 1.0) + (A - 1.0) * cw + sq) / a0);
    b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0);
    b2 = (float) (A * ((A + 1.0) + (A - 1.0) * cw - sq) / a0);
    a1 = (float) (2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0);
    a2 = (float) (((A + 1.0) - (A - 1.0) * cw - sq) / a0);
}

void SpringerModule::Biquad::setNotch (double fc, double q, double sr) noexcept
{
    fc = juce::jlimit (10.0, sr * 0.45, fc);
    const double w = juce::MathConstants<double>::twoPi * fc / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = b2 = (float) (1.0 / a0);
    b1 = a1 = (float) (-2.0 * cw / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void SpringerModule::prepare (double sr)
{
    SynthModule::prepare (sr);

    // --- Coils
    juce::Random rngCoils (0x5153);   // fixed seed: coil dispersion patterns are stable
    for (int c = 0; c < maxCoils; ++c)
    {
        auto& coil = coils[c];
        coil.delayBuf.assign ((size_t) juce::jmax (64, (int) (sr * 0.16)), 0.0f);   // 100ms max size * 1.41 coil scale + headroom
        coil.densityBuf.assign ((size_t) juce::jmax (8, (int) (kDensityMs[c] * 0.001 * sr)), 0.0f);
        for (int i = 0; i < maxStages; ++i)
            coil.coeffs[i] = 0.35f + rngCoils.nextFloat() * 0.45f;
    }

    // --- Tank: lines long enough for the lowest Chirp Tone (its stretch), at any rate
    lineLen = (int) std::ceil (sr / (2.0 * kMinChirpHz * 0.9)) + 3;
    for (int i = 0; i < kMaxSprings; ++i)
    {
        auto& s = springs[i];
        s.ap.assign ((size_t) (maxTankStages * lineLen), 0.0f);
        s.fx1.assign ((size_t) maxTankStages, 0.0f);
        s.fy1.assign ((size_t) maxTankStages, 0.0f);
        s.loop.assign ((size_t) (sr * 0.17) + 8, 0.0f);
        s.hfLoop.assign ((size_t) (sr * 0.09) + 8, 0.0f);
        // the scattering all-passes: a few ms, mutually prime-ish per spring
        s.sc1.assign ((size_t) juce::jmax (8, (int) (sr * 0.001 * (3.1 + 0.53 * i))), 0.0f);
        s.sc2.assign ((size_t) juce::jmax (8, (int) (sr * 0.001 * (4.9 + 0.71 * i))), 0.0f);
    }
    for (auto& k : lastTankKey)
        k = -1.0f;

    reset();
}

void SpringerModule::reset()
{
    for (auto& coil : coils)
    {
        std::fill (coil.delayBuf.begin(), coil.delayBuf.end(), 0.0f);
        std::fill (coil.densityBuf.begin(), coil.densityBuf.end(), 0.0f);
        coil.writePos = 0;
        coil.densityPos = 0;
        for (auto& m : coil.apMem) m = 0.0f;
        coil.dampingMem = coil.hpMem = coil.lastOut = 0.0f;
    }

    for (auto& s : springs)
    {
        std::fill (s.ap.begin(), s.ap.end(), 0.0f);
        std::fill (s.fx1.begin(), s.fx1.end(), 0.0f);
        std::fill (s.fy1.begin(), s.fy1.end(), 0.0f);
        std::fill (s.loop.begin(), s.loop.end(), 0.0f);
        std::fill (s.hfLoop.begin(), s.hfLoop.end(), 0.0f);
        std::fill (s.sc1.begin(), s.sc1.end(), 0.0f);
        std::fill (s.sc2.begin(), s.sc2.end(), 0.0f);
        s.shelf.clear(); s.hfHp1.clear(); s.hfHp2.clear(); s.hfLp.clear();
        for (auto& m : s.hfMem) m = 0.0f;
        s.loopPos = s.hfPos = s.sc1Pos = s.sc2Pos = 0;
        s.loopOut = s.hfOut = s.dc = 0.0f;
        s.chaos = s.chaos1 = s.chaosTarget = 0.0;
        s.chaosCount = 0;
    }
    for (auto& b : outHp) b.clear();
    for (auto& b : toneLp) b.clear();
    for (auto& c : foldLp) for (auto& b : c) b.clear();
    for (auto& b : foldNotch) b.clear();
    apPos = 0;
}

void SpringerModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if ((int) std::lround (param (pModel)) == 1)
        processCoils (inputs, outputs);
    else
        processTank (inputs, outputs);
}

//==============================================================================
// Tank
//==============================================================================
void SpringerModule::updateTank()
{
    const double sr = sampleRate;
    const float size = param (pSizeMs), chirp = param (pChirp) * 0.01f, damping = param (pDamping) * 0.01f;
    const float res = param (pResonance), tone = param (pTone) * 0.01f, stages = param (pStages);
    const float chirpHz = param (pChirpTone);
    const float key[8] = { size, chirp, damping, res, tone, stages, (float) sr, chirpHz };
    bool changed = false;
    for (int i = 0; i < 8; ++i)
        if (key[i] != lastTankKey[i]) { changed = true; lastTankKey[i] = key[i]; }
    if (! changed)
        return;

    // the transition (where the cascade folds): Chirp Tone, kept below a third of the rate
    const double fe = juce::jlimit (kMinChirpHz * 0.9, sr / 3.0, (double) chirpHz);
    const double t60 = 0.4 + 7.0 * (double) res * res;             // seconds, lows and mids (3.8 s at the default)
    const double hfRatio = juce::jmax (0.08, 1.0 - 0.875 * (double) damping);   // the highs' share of it
    const int m = juce::jlimit (1, maxTankStages, (int) std::lround (stages));
    const double a = 0.45 + 0.35 * chirp;

    for (int i = 0; i < kMaxSprings; ++i)
    {
        auto& s = springs[i];
        s.hfHp1.set (fe * kSpringFc[i], 0.54, sr, true);
        s.hfHp2.set (fe * kSpringFc[i], 1.31, sr, true);
        s.hfLp.set (juce::jmin (sr * 0.45, fe * kSpringFc[i] * 2.5), 0.7, sr, false);   // the falling chirps stay near the top end

        // the decay: the loop gain from the round trip (the delay plus what the
        // cascade itself holds back at low frequencies), the highs' loss on top
        // this spring's stretch: K = sr / (2 f), whole samples N (K - N in 0.5 .. 1.5)
        // plus a first-order all-pass of the rest
        const double K = juce::jlimit (1.5, (double) lineLen - 2.0, sr / (2.0 * fe * kSpringFc[i]));
        const int N = juce::jmax (1, (int) std::floor (K - 0.5));
        const double frac = K - N;
        s.readN = N;
        s.eta = (float) ((1.0 - frac) / (1.0 + frac));
        s.stretch = K;

        const double ai = juce::jlimit (0.1, 0.92, a * kSpringDisp[i]);
        const double dispSec = m * K * (1.0 - ai) / (1.0 + ai) / sr;
        const double loopSec = size * 0.001 * kSpringLen[i] + dispSec;
        tankFb[i] = (float) std::pow (10.0, -3.0 * loopSec / t60);
        const double gHigh = std::pow (10.0, -3.0 * loopSec / (t60 * hfRatio));
        s.shelf.setHighShelf (2500.0, 20.0 * std::log10 (gHigh / tankFb[i]), sr);
        hfFb[i] = (float) std::pow (10.0, -3.0 * (loopSec * 0.5) / (t60 * 0.35));
    }

    // out of the tank: steep at the transition (8 poles, Butterworth), the fold notched
    static constexpr double kQ[4] = { 0.51, 0.60, 0.90, 2.56 };
    const double toneHz = 1500.0 * std::pow (2.0, tone * 3.2);
    for (int c = 0; c < 2; ++c)
    {
        outHp[c].set (70.0, 0.7, sr, true);
        toneLp[c].set (toneHz, 0.7, sr, false);
        for (int k = 0; k < 4; ++k)
            foldLp[c][k].set (fe * 0.9, kQ[k], sr, false);
        foldNotch[c].setNotch (fe, 1.2, sr);
    }
}

void SpringerModule::processTank (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (springs[0].loop.empty())
    {
        outputs[0] = inputs[0];
        return;
    }
    updateTank();

    const double sr = sampleRate;
    const int n = juce::jlimit (1, kMaxSprings, (int) std::lround (param (pCoils)));
    const int m = juce::jlimit (1, maxTankStages, (int) std::lround (param (pStages)));
    const int L = lineLen;
    const float chirp = param (pChirp) * 0.01f;
    const float a = 0.45f + 0.35f * chirp;
    const float drip = param (pDrip) * 0.01f;
    const float wet = param (pDryWet) * 0.01f;
    const float chaos = param (pChaos) * 0.01f;
    const float scatter = 0.125f * chaos;                         // the scattering all-passes' gain
    const double tremble = 20.0 * chaos * sr / 48000.0;            // samples of loop-length tremble
    const double sizeSamples = param (pSizeMs) * 0.001 * sr;
    const double chaosCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 8.0 / sr);

    const float x = (inputs[0][0] + inputs[0][1]) * 0.5f;
    float out[kMaxSprings] {}, outHf[kMaxSprings] {};

    // ---- the chirps: each spring's loop through its stretched all-pass cascade -
    // (6.9.11) the springs stepped side by side, stage by stage (the same sums
    // per spring as one after another; independent chains the CPU overlaps)
    float uOf[kMaxSprings] {};
    {
        int rp = apPos - springs[0].readN;
        const float* rowR[kMaxSprings] {};
        float* rowW[kMaxSprings] {};
        float* fxs[kMaxSprings] {};
        float* fys[kMaxSprings] {};
        float etas[kMaxSprings] {}, ais[kMaxSprings] {};
        for (int i = 0; i < n; ++i)
        {
            auto& s = springs[i];
            uOf[i] = 0.6f * x + tankFb[i] * s.loopOut;
            ais[i] = juce::jlimit (0.1f, 0.92f, a * kSpringDisp[i]);
            rp = apPos - s.readN;
            if (rp < 0) rp += L;
            // (the lines kept position-major: a sample's stages side by side in memory)
            rowR[i] = s.ap.data() + (size_t) rp * (size_t) maxTankStages;
            rowW[i] = s.ap.data() + (size_t) apPos * (size_t) maxTankStages;
            fxs[i] = s.fx1.data();
            fys[i] = s.fy1.data();
            etas[i] = s.eta;
        }
        auto stage = [&] (int i, int j)
        {
            const float d = rowR[i][j];                                // w[n - N] ...
            const float v = etas[i] * (d - fys[i][j]) + fxs[i][j];     // ... and the rest of K (fractional all-pass)
            fxs[i][j] = d;
            fys[i][j] = v;
            const float w = uOf[i] - ais[i] * v;
            uOf[i] = ais[i] * w + v;
            rowW[i][j] = w;
        };
        if (n == 4)
            for (int j = 0; j < m; ++j) { stage (0, j); stage (1, j); stage (2, j); stage (3, j); }
        else
            for (int j = 0; j < m; ++j)
                for (int i = 0; i < n; ++i)
                    stage (i, j);
    }

    for (int i = 0; i < n; ++i)
    {
        auto& s = springs[i];
        float u = uOf[i];
        u = s.shelf.process (u);

        // the scattering: each pass smears a little more
        if (scatter > 0.0f)
        {
            float v = s.sc1[(size_t) s.sc1Pos];
            float w = u + scatter * v;
            s.sc1[(size_t) s.sc1Pos] = w;
            u = v - scatter * w;
            s.sc1Pos = (s.sc1Pos + 1) % (int) s.sc1.size();
            v = s.sc2[(size_t) s.sc2Pos];
            w = u + scatter * v;
            s.sc2[(size_t) s.sc2Pos] = w;
            u = v - scatter * w;
            s.sc2Pos = (s.sc2Pos + 1) % (int) s.sc2.size();
        }
        s.dc += 0.0015f * (u - s.dc);
        u -= s.dc;
        u = u / (1.0f + 0.08f * std::abs (u));             // the transducers' gentle limit

        // the round trip, its length trembling (springs never hold perfectly still)
        if (--s.chaosCount <= 0)
        {
            s.chaosTarget = rng.nextDouble() * 2.0 - 1.0;
            s.chaosCount = (int) (sr / 8.0 * (0.5 + 0.5 * rng.nextDouble()));
        }
        s.chaos1 += chaosCoef * (s.chaosTarget - s.chaos1);
        s.chaos += chaosCoef * (s.chaos1 - s.chaos);
        const int size = (int) s.loop.size();
        const double len = juce::jlimit (8.0, (double) size - 4.0, sizeSamples * kSpringLen[i] + tremble * s.chaos);
        s.loop[(size_t) s.loopPos] = u;
        auto readAt = [&] (double d)
        {
            double rp = (double) s.loopPos - d;
            while (rp < 0.0) rp += size;
            const int i0 = (int) rp % size, i1 = (i0 + 1) % size;
            const float f = (float) (rp - std::floor (rp));
            return s.loop[(size_t) i0] + f * (s.loop[(size_t) i1] - s.loop[(size_t) i0]);
        };
        s.loopOut = readAt (len);
        out[i] = readAt (len * 0.5);                       // the far end: one way down the spring
        s.loopPos = (s.loopPos + 1) % size;

        // ---- the high chirps: falling, quicker, fading sooner
        if (drip > 0.0f)
        {
            float v = x + hfFb[i] * s.hfOut;
            const float ah = -0.62f * kSpringDisp[i];
            for (int j = 0; j < kHfStages; ++j)
            {
                const float y = s.hfMem[j] - ah * v;     // first-order all-pass
                s.hfMem[j] = v + ah * y;
                v = y;
            }
            v = s.hfLp.process (s.hfHp2.process (s.hfHp1.process (v)));
            const int hsize = (int) s.hfLoop.size();
            const int hlen = juce::jlimit (4, hsize - 2, (int) (len * 0.5));
            s.hfLoop[(size_t) s.hfPos] = v;
            int rp = s.hfPos - hlen;
            if (rp < 0) rp += hsize;
            s.hfOut = s.hfLoop[(size_t) rp];
            s.hfPos = (s.hfPos + 1) % hsize;
            outHf[i] = s.hfOut;
        }
    }
    apPos = (apPos + 1) % L;

    auto pan = [n] (const float* o, float& l, float& r)
    {
        if (n == 1) { l = r = o[0]; return; }
        l = o[0] + 0.62f * o[2] + 0.38f * o[3];
        r = o[1] + 0.38f * o[2] + 0.62f * o[3];
    };
    float wl, wr, hl, hr;
    pan (out, wl, wr);
    pan (outHf, hl, hr);
    float w[2] = { wl, wr };
    const float h[2] = { hl, hr };
    const float norm = 1.1f / std::sqrt ((float) n);
    for (int c = 0; c < 2; ++c)
    {
        float v = w[c];
        for (auto& b : foldLp[c]) v = b.process (v);
        v = foldNotch[c].process (v) + drip * 0.45f * h[c];
        w[c] = toneLp[c].process (outHp[c].process (v)) * norm;
    }

    outputs[0][0] = inputs[0][0] * (1.0f - wet) + w[0] * wet;
    outputs[0][1] = inputs[0][1] * (1.0f - wet) + w[1] * wet;
}

//==============================================================================
// Coils (unchanged from 5.15)
//==============================================================================
void SpringerModule::processCoils (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float sizeMs = param (pSizeMs);
    const int activeCoils = juce::jlimit (1, maxCoils, (int) param (pCoils));
    const int stages = juce::jlimit (1, maxStages, (int) param (pStages));
    const float feedback = juce::jlimit (0.0f, 1.02f, param (pResonance) * 0.85f);
    const float dampCoeff = juce::jlimit (0.01f, 1.0f, 1.0f - param (pDamping) * 0.0099f);
    const float chirpShift = (param (pChirp) * 0.01f - 0.5f) * 0.4f;
    const float wet = param (pDryWet) * 0.01f;

    const float inMono = (inputs[0][0] + inputs[0][1]) * 0.5f * 0.75f;   // original's input drive

    float wetL = 0.0f, wetR = 0.0f;

    for (int c = 0; c < activeCoils; ++c)
    {
        auto& coil = coils[c];
        const int delaySize = (int) coil.delayBuf.size();
        const int delaySamples = juce::jlimit (4, delaySize - 2,
            (int) (sizeMs * 0.001f * (float) sampleRate * kCoilScale[c]));

        // feedback with DC block (matches SpringLine::process)
        const float fbSig = coil.lastOut * feedback;
        coil.hpMem += 0.06f * (fbSig - coil.hpMem);
        const float inner = inMono + fbSig - coil.hpMem;

        // main delay
        int readPos = coil.writePos - delaySamples;
        if (readPos < 0) readPos += delaySize;
        float out = coil.delayBuf[(size_t) readPos];
        coil.delayBuf[(size_t) coil.writePos] = inner;
        coil.writePos = (coil.writePos + 1) % delaySize;

        // density smearing allpass (fixed -0.6 coeff like the original)
        {
            const float b1 = -0.6f;
            const float delayOut = coil.densityBuf[(size_t) coil.densityPos];
            const float y = out + b1 * delayOut;
            coil.densityBuf[(size_t) coil.densityPos] = y;
            coil.densityPos = (coil.densityPos + 1) % (int) coil.densityBuf.size();
            out = delayOut + (-b1) * y;
        }

        // allpass dispersion chain - THE spring chirp
        for (int i = 0; i < stages; ++i)
        {
            const float a = juce::jlimit (0.05f, 0.985f, coil.coeffs[i] + chirpShift);
            const float y = coil.apMem[i] - a * out;
            coil.apMem[i] = out + a * y;
            out = y;
        }

        // damping lowpass + soft saturation
        coil.dampingMem += dampCoeff * (out - coil.dampingMem);
        out = std::tanh (coil.dampingMem * 1.2f);
        coil.lastOut = out;

        // alternate coils L/R for width
        if (c & 1) wetR += out;
        else       wetL += out;
    }

    // loudness compensation for coil count (from the original)
    const float norm = 1.2f / (1.0f + std::sqrt ((float) activeCoils) * 0.4f);
    wetL *= norm;
    wetR *= norm;
    if (activeCoils == 1)
        wetR = wetL;

    outputs[0][0] = inputs[0][0] * (1.0f - wet) + wetL * wet;
    outputs[0][1] = inputs[0][1] * (1.0f - wet) + wetR * wet;
}

static ModuleDescriptor springerDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.springer";
    d.displayName = "Spring Reverb";
    d.description =
        "A spring tank, fitted to measured tanks: every pass down a spring arrives as a dripping chirp that bends up "
        "toward the springs' top end, the way a real tank's highs travel slower than its lows. Chirp Tone moves that "
        "top end (a thicker or thinner spring), Stages and Chirp how far and how hard the chirp is spread; Blur Rate "
        "blurs the later passes into the washy tail, Drip adds the bright falling chirps of a struck tank. Size is the "
        "time down a spring and back, Resonance the decay, Damping how much sooner the highs die, Coils how many "
        "springs (up to four in Tank). Model Coils is the older, denser engine.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 7;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("sizeMs",    "Size",      2.0f, 100.0f, 36.0f, 0, "ms", true),
        makeRotary ("coils",     "Coils",     1.0f, 7.0f, 3.0f, 0, {}, false, 1.0f),
        makeRotary ("stages",    "Stages",    1.0f, 200.0f, 140.0f, 0, {}, true, 1.0f),
        makeRotary ("resonance", "Resonance", 0.0f, 1.2f, 0.7f, 0),
        makeRotary ("damping",   "Damping",   0.0f, 100.0f, 40.0f, 0, "%"),
        makeRotary ("chirp",     "Chirp",     0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("dryWet",    "Dry/Wet",   0.0f, 100.0f, 40.0f, 1, "%")
    };
    // after the original seven, so saved patches keep their indices; a patch
    // from before the Tank opens with the engine it was made with
    d.params.push_back (makeCombo ("model", "Model", { "Tank", "Coils" }, 0, 2, 2).noMod().whenMissing (1.0f));
    d.params.push_back (makeRotary ("drip", "Drip", 0.0f, 100.0f, 25.0f, 1, "%").visibleWhen ("model", 0.0f));
    d.params.push_back (makeRotary ("tone", "Tone", 0.0f, 100.0f, 70.0f, 1, "%").visibleWhen ("model", 0.0f));
    d.params.push_back (makeRotary ("chaos", "Blur Rate", 0.0f, 100.0f, 40.0f, 2, "%").visibleWhen ("model", 0.0f));
    d.params.push_back (makeRotary ("chirpTone", "Chirp Tone", (float) SpringerModule::kMinChirpHz, (float) SpringerModule::kMaxChirpHz,
                                    4700.0f, 2, "Hz", true).visibleWhen ("model", 0.0f));
    return d;
}

AQUANODE_REGISTER_MODULE (SpringerModule, springerDescriptor)
