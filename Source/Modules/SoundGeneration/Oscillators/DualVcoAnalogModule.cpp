#include "DualVcoAnalogModule.h"

using namespace aquanode;

//==============================================================================
// presets: every knob of the sound, in the card's own units
//==============================================================================
struct DualVcoAnalogModule::Preset
{
    const char* name;
    // wave1, wave2, oscMix, tune2, fine2, octave, noise, cutoff, reso, envAmt, keyTrack, velCut, lfoCut,
    // fAtt, fDec, fSus, fRel, att, dec, sus, rel, velAmp, lfoRate, vibrato, pwm, glide, glideBend, glideMode, voices
    float v[29];
};

namespace
{
    // the preset table's columns, as parameter ids
    const char* const kPresetIds[29] = {
        "wave1", "wave2", "oscMix", "tune2", "fine2", "octave", "noise", "cutoff", "reso", "envAmt", "keyTrack", "velCut", "lfoCut",
        "fAtt", "fDec", "fSus", "fRel", "att", "dec", "sus", "rel", "velAmp", "lfoRate", "vibrato", "pwm", "glide", "glideBend",
        "glideMode", "voices" };

    const DualVcoAnalogModule::Preset kPresets[] = {
        { "Init",           { 0, 0, 0, 0, 0, 0, 0, 9000, 8, 0, 100, 0, 0, 4.1f, 300, 100, 300, 4.1f, 300, 100, 180, 60, 5.0f, 0, 0, 0.5f, 0, 0, 0 } },
        { "7th Lead",       { 0, 0, 100, 7, -7.1f, 1, 0, 3368, 63, 4.33f, 100, 0, 0.092f, 4.1f, 38.8f, 0, 26.7f, 14.6f, 174, 100, 5.1f, 100, 5.31f, 0, 0, 26.8f, 12.35f, 2, 0 } },
        { "Fifth Sweep",    { 0, 0, 100, -7, -6.3f, 0, 0, 24890, 60, -6.58f, 100, 0, 0, 3490, 3238, 81, 975, 4.1f, 174, 100, 840, 100, 5.31f, 0.078f, 0, 23.3f, 0, 1, 0 } },
        { "Glide Mono",     { 0, 0, 0, -12, 0, 0, 0, 1099, 0, 0, 100, 0, 0, 4.1f, 38.8f, 0, 26.7f, 65.6f, 174, 100, 70.7f, 0, 5.31f, 0.05f, 0, 62, 0, 2, 1 } },
        { "Detune Lead",    { 0, 0, 84, 0, -17.2f, 0, 40, 1397, 1, 1.39f, 100, 1.55f, 0.16f, 4.1f, 8.0f, 100, 26.7f, 18.3f, 2399, 100, 38.8f, 100, 6.11f, 0, 67, 43.7f, -0.15f, 2, 0 } },
        { "Hard Lead",      { 0, 0, 71, 12, 0, 0, 0, 1640, 52, 3.29f, 100, 1.82f, 0.13f, 7389, 11.7f, 42, 5078, 6.9f, 3762, 100, 12.6f, 100, 6.55f, 0, 11.6f, 13.3f, 36, 1, 0 } },
        { "Bubbles",        { 0, 0, 0, -12, -0.2f, 1, 0, 117, 77, 5.19f, 100, 1.18f, 0.37f, 82.1f, 15.8f, 43.6f, 11.7f, 4.1f, 70.7f, 65, 13.6f, 100, 0.527f, 0, 0, 357, 0, 0, 0 } },
        { "Backing Chords", { 0, 0, 100, -12, -18.7f, 0, 50, 205, 25, 3.46f, 100, 1.18f, 0, 4.1f, 56.4f, 0, 26.7f, 4.1f, 174, 100, 38.8f, 100, 5.31f, 0, 0, 28.7f, 0, 0, 0 } },
        { "Acid Saw Bass",  { 0, 0, 0, 0, 0, -2, 0, 1514, 75, 3.29f, 100, 0, 0.44f, 4.1f, 273, 0, 273, 4.1f, 1649, 100, 24.7f, 100, 0.113f, 0, 0.04f, 76.5f, 0, 1, 1 } },
        { "Acid Square",    { 0, 0, 75, 0, 0, -2, 0, 1514, 75, 3.29f, 100, 0, 0.44f, 11.7f, 161, 0, 76.2f, 4.1f, 1649, 100, 24.7f, 100, 0.113f, 0, 0.04f, 76.5f, 0, 1, 1 } },
        { "Analog Bass",    { 0, 0, 100, -12, -10.9f, -1, 0, 205, 51, 6.06f, 100, 0, 0.029f, 4.1f, 3004, 0, 19.7f, 4.1f, 174, 100, 129, 0, 5.31f, 0, 0, 9.4f, 0, 1, 1 } },
        { "Low Pulses",     { 0, 0, 97, -12, -3.3f, -2, 0, 11180, 40, 0.35f, 100, 0, 0, 4.1f, 1317, 0, 26.7f, 4.1f, 174, 100, 38.8f, 100, 5.31f, 0, 46, 28.7f, 0, 0, 0 } },
        { "Sub Sine",       { 1, 1, 0, -12, 0, -2, 0, 260, 76, 0.52f, 100, 0, 0, 4.1f, 38.8f, 0, 26.7f, 4.1f, 253, 25, 38.8f, 100, 5.31f, 0, 0, 28.7f, 0, 0, 1 } },
        { "Soft Pad",       { 0, 0, 39, 0, -4.9f, 0, 0, 306, 78, 0, 100, 0, 0, 4.1f, 38.8f, 0, 26.7f, 56.4f, 174, 80, 779, 100, 5.31f, 0, 0, 5.7f, 0, 2, 0 } },
        { "Strings",        { 0, 0, 100, 0, -7.1f, 0, 30, 535, 26, 4.33f, 100, 1.73f, 0.071f, 4.1f, 622, 30, 5900, 1916, 779, 100, 95.4f, 100, 6.55f, 0.1f, 0, 0.5f, -0.97f, 0, 0 } },
        { "Brass",          { 0, 0, 40, 0, -6.3f, 0, 23, 421, 15, 4.33f, 100, 0, 0, 76.2f, 38.8f, 67, 26.7f, 48.6f, 1051, 76, 88.5f, 100, 5.31f, 0, 0.36f, 20.2f, -3.07f, 0, 0 } },
        { "Power PWM",      { 0, 0, 100, -12, -8.8f, -1, 0, 13120, 13, 4.33f, 100, 0, 0, 24.7f, 38.8f, 77, 52.3f, 4.1f, 174, 100, 150, 0, 2.64f, 0, 6.8f, 28.7f, 0, 0, 0 } },
        { "Soft Keys",      { 0, 0, 31, 0, -0.2f, 0, 0, 282, 26, 0.52f, 100, 1.18f, 0, 4.1f, 21.3f, 0, 76.2f, 4.1f, 1649, 0, 111, 100, 5.31f, 0, 0, 28.7f, 0, 0, 0 } },
        { "Mono Lead",      { 0, 0, 62, -12, 0, -1, 0, 3109, 39, 0.17f, 100, 0, 1.52f, 6.9f, 202, 5.8f, 2226, 10.8f, 38.8f, 76, 19.7f, 0, 1.06f, 0, 16, 28.7f, 0, 1, 1 } },
        { "Sine Duo",       { 1, 1, 50, 12, 6, 0, 0, 6000, 0, 0, 100, 0, 0, 4.1f, 300, 100, 300, 6.0f, 900, 70, 420, 70, 5.0f, 0.06f, 0, 0.5f, 0, 0, 0 } },
    };

    constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

    // each preset's Level: every one about as loud as the others (its loudest
    // 100 ms of one held note near -16 dBFS, the sources' convention)
    const float kPresetLevel[] = { -6.0f, 0.0f, 0.0f, -3.9f, -2.2f, 0.6f, 1.3f, 2.4f, 0.2f, -0.9f, -3.9f,
                                   -1.7f, 5.3f, 4.3f, 5.6f, -1.8f, -5.1f, -0.8f, -2.4f, -4.0f };
    static_assert (sizeof (kPresetLevel) / sizeof (kPresetLevel[0]) == (size_t) kNumPresets, "a level per preset");

    inline double polyBlep (double t, double dt) noexcept
    {
        if (t < dt)        { t /= dt;               return t + t - t * t - 1.0; }
        if (t > 1.0 - dt)  { t = (t - 1.0) / dt;    return t * t + t + t + 1.0; }
        return 0.0;
    }

    inline float oscillator (int wave, double p, double dt) noexcept
    {
        switch (wave)
        {
            case 1:  return (float) std::sin (juce::MathConstants<double>::twoPi * p);   // Sine
            case 2:  return (float) (1.0 - 4.0 * std::abs (p - 0.5));                   // Triangle
            default: return (float) (2.0 * p - 1.0 - polyBlep (p, dt));                  // Saw
        }
    }

    inline float xorshiftNoise (uint32_t& s) noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s & 0xffffff) / 8388608.0f - 1.0f;
    }
}

const DualVcoAnalogModule::Preset* DualVcoAnalogModule::presets() { return kPresets; }
int DualVcoAnalogModule::numPresets() { return kNumPresets; }

//==============================================================================
void DualVcoAnalogModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void DualVcoAnalogModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
    pool.resetAll();
    lastNote = -1;
    heldNotes = 0;
}

void DualVcoAnalogModule::voiceReset (int v)
{
    pool.resetVoice (v);
    oGlide.resetVoice (v);
    vco[v].reset ((uint32_t) v + 1u);
    active[v] = false;
    ampEnv[v] = Env();
    filtEnv[v] = Env();
    lp[v] = bp[v] = 0.0f;
    glideCur[v] = 0.0f;
    noiseSeed[v] = 0x9E3779B9u * (uint32_t) (v + 1);
    drift1[v] = drift2[v] = driftT1[v] = driftT2[v] = 0.0f;
    driftCount[v] = 0;
}

void DualVcoAnalogModule::voiceNoteOn (int v, int n, bool retrigger)
{
    pool.noteOn (v, voiceLimit());

    if (oscEngine())
    {
        // Oscillators: free-running VCOs - every new note catches them at some phase
        oGlide.noteOn (v, (float) n, isMonoVoice());
        if (! retrigger)
            vco[v].reset (++noteCounter * 2654435761u + (uint32_t) v);
        note[v] = n;
        active[v] = true;
        return;
    }

    const bool vco = (int) std::lround (param (pMode)) == 0;
    const float drift = param (pDrift) * 0.01f;
    const int glideMode = (int) std::lround (param (pGlideMode));

    // where the note starts: Glide Bend off it, and with glide from the last note
    const bool fromLast = lastNote >= 0 && (glideMode == 2 || (glideMode == 1 && heldNotes > 0));
    const float start = (fromLast ? (float) (lastNote - n) : 0.0f) + param (pGlideBend);
    if (! retrigger || ! active[v])
        glideCur[v] = start;
    else
        glideCur[v] += (float) (note[v] - n);   // a re-struck voice slides on from where it is
    note[v] = n;
    lastNote = n;
    ++heldNotes;

    if (! retrigger || ! active[v])
    {
        if (vco)
        {
            // free-running VCOs: caught at any phase; each voice a little its own
            phase1[v] = rng.nextDouble();
            phase2[v] = rng.nextDouble();
            detune1[v] = (rng.nextFloat() * 2.0f - 1.0f) * drift * 4.0f;    // cents
            detune2[v] = (rng.nextFloat() * 2.0f - 1.0f) * drift * 4.0f;
            cutOffset[v] = (rng.nextFloat() * 2.0f - 1.0f) * drift * 0.12f; // octaves
            envScale[v] = 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * drift * 0.08f;
        }
        else
        {
            // DCOs: every note the same, in tune
            phase1[v] = 0.0;
            phase2[v] = 0.5;
            detune1[v] = detune2[v] = cutOffset[v] = 0.0f;
            envScale[v] = 1.0f;
            drift1[v] = drift2[v] = 0.0f;
        }
        // PWM wants the second oscillator half a cycle from the first, so the
        // difference of the two saws is a pulse
        if (param (pPwm) > 0.0f)
            phase2[v] = std::fmod (phase1[v] + 0.5, 1.0);

        // the LFO is one for the whole synth: a new voice picks up its phase
        for (int o = 0; o < kMaxVoices; ++o)
            if (o != v && active[o])
            {
                lfoPhase[v] = lfoPhase[o];
                break;
            }
        lp[v] = bp[v] = 0.0f;
    }

    active[v] = true;
    ampEnv[v].start();
    filtEnv[v].start();
}

void DualVcoAnalogModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    ampEnv[v].release();
    filtEnv[v].release();
    heldNotes = juce::jmax (0, heldNotes - 1);
}

double DualVcoAnalogModule::voiceTailSeconds() const
{
    if (oscEngine())
        return SynthModule::voiceTailSeconds();   // (no envelope of its own: the voice's)
    return decayTailSeconds (juce::jmax (getParameterBase (pRel), 1.0f) * 1.1);
}

void DualVcoAnalogModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float poolGain = pool.nextGain (v, sampleRate);
    if (oscEngine())
    {
        // ---- the Oscillators engine: the classic Dual VCO, no gate
        if (pool.isSilent (v))
        {
            outputs[0] = { 0.0f, 0.0f };
            return;
        }
        const bool mono = isMonoVoice();
        const double n = oGlide.next (v, mono ? param (pOGlide) : 0.0f, mono, ! pool.isMuted (v), sampleRate)
                       + pitchBendSemitones();
        const double pitchMod = isInputConnected (0) ? inputs[0][0] * param (pOPitchMod) : 0.0;
        const float y = vco[v].process (sampleRate, n, pitchMod,
                                        (int) param (pOWave1), param (pOPw1) * 0.1f, (int) param (pOScale1),
                                        (int) param (pOWave2), param (pOPitch2), (int) param (pOScale2),
                                        param (pOFine), param (pOLevel1) * 0.1f, param (pOLevel2) * 0.1f,
                                        param (pOAnalog) * 0.01f)
                      * juce::Decibels::decibelsToGain (param (pLevel) + 1.0f) * poolGain;   // (+1 dB: the old card's trim)
        outputs[0] = { y, y };
        return;
    }
    if (! active[v] || pool.isSilent (v))
    {
        outputs[0] = { 0.0f, 0.0f };
        return;
    }

    const double sr = sampleRate;
    const bool vco = (int) std::lround (param (pMode)) == 0;
    const float drift = vco ? param (pDrift) * 0.01f : 0.0f;

    // ---- the shared LFO
    lfoPhase[v] += param (pLfoRate) / sr;
    if (lfoPhase[v] >= 1.0) lfoPhase[v] -= 1.0;
    const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase[v]);

    // ---- glide: toward the note, rising twice as fast as falling
    const float glideMs = param (pGlide);
    if (glideMs <= 1.0f)
        glideCur[v] = 0.0f;
    else if (glideCur[v] != 0.0f)
    {
        const float c = coefFor (glideMs, glideCur[v] < 0.0f ? 0.5f : 1.0f);
        glideCur[v] -= c * glideCur[v];
        if (std::abs (glideCur[v]) < 1.0e-4f) glideCur[v] = 0.0f;
    }

    // ---- the analog drift: two slow random walks, one per oscillator
    if (drift > 0.0f)
    {
        if (--driftCount[v] <= 0)
        {
            driftT1[v] = rng.nextFloat() * 2.0f - 1.0f;
            driftT2[v] = rng.nextFloat() * 2.0f - 1.0f;
            driftCount[v] = (int) (sr * (0.4 + 1.2 * rng.nextDouble()));
        }
        const float dc = (float) (1.0 / (0.7 * sr));
        drift1[v] += dc * (driftT1[v] - drift1[v]);
        drift2[v] += dc * (driftT2[v] - drift2[v]);
    }

    const int octave = (int) std::lround (param (pOctave)) - 2;
    const double offset1 = 12.0 * octave + glideCur[v] + pitchBendSemitones() + param (pVibrato) * lfo
                         + 0.01 * (detune1[v] + drift * 6.0f * drift1[v]);
    const double offset2 = offset1 + param (pTune2) + 0.01 * (param (pFine2) + detune2[v] - detune1[v]
                                                              + drift * 6.0f * (drift2[v] - drift1[v]));
    const double baseHz = midiNoteToHz (note[v]);
    const double hz1 = juce::jlimit (1.0, sr * 0.45, baseHz * std::exp2 (offset1 / 12.0));
    double hz2 = baseHz * std::exp2 (offset2 / 12.0);
    hz2 /= 1.0 + 0.05 * (double) (param (pPwm) * 0.01f) * lfo;   // PWM: the second oscillator's period wobbles
    hz2 = juce::jlimit (1.0, sr * 0.45, hz2);

    // ---- the oscillators: the second subtracted from the first
    const double dt1 = hz1 / sr, dt2 = hz2 / sr;
    const double prev1 = phase1[v], prev2 = phase2[v];
    phase1[v] += dt1; if (phase1[v] >= 1.0) phase1[v] -= 1.0;
    phase2[v] += dt2; if (phase2[v] >= 1.0) phase2[v] -= 1.0;
    const float mix = param (pOscMix) * 0.01f;
    float x;
    if (aquanode::antiAliasOn())
    {
        // (6.9.21) Anti-alias: both oscillators at the half step and at the step, band-limited at twice
        // the rate (the triangle's corners too), brought back down
        if (! aaWasOn[v]) { aaDown[v].reset(); aaWasOn[v] = true; }
        const int w1 = (int) std::lround (param (pWave1)), w2 = (int) std::lround (param (pWave2));
        auto osc = [] (int wave, double p, double dt)
        {
            if (wave == 1) return std::sin (juce::MathConstants<double>::twoPi * p);
            if (wave == 2) return aquanode::blTriangle (p, dt);
            return aquanode::blSaw (p, dt);
        };
        auto both = [&] (double p1, double p2) { return (float) (osc (w1, p1, dt1 * 0.5) - mix * osc (w2, p2, dt2 * 0.5)); };
        x = aaDown[v].process (both (aquanode::wrap01 (prev1 + 0.5 * dt1), aquanode::wrap01 (prev2 + 0.5 * dt2)), both (phase1[v], phase2[v]));
    }
    else
    {
        aaWasOn[v] = false;
        x = oscillator ((int) std::lround (param (pWave1)), phase1[v], dt1)
          - mix * oscillator ((int) std::lround (param (pWave2)), phase2[v], dt2);
    }
    const float noiseAmt = param (pNoise) * 0.01f;
    x += noiseAmt * noiseAmt * 0.9f * xorshiftNoise (noiseSeed[v]);

    // ---- the envelopes
    const float es = envScale[v];
    const float fe = filtEnv[v].next (coefFor (param (pFAtt), es), coefFor (param (pFDec), es),
                                      param (pFSus) * 0.01f, coefFor (param (pFRel), es));
    const float ae = ampEnv[v].next (coefFor (param (pAtt), es), coefFor (param (pDec), es),
                                     param (pSus) * 0.01f, coefFor (param (pRel), es));
    if (ampEnv[v].stage == Env::Idle)
    {
        active[v] = false;
        outputs[0] = { 0.0f, 0.0f };
        return;
    }

    // ---- the filter: a resonant two-pole low-pass, run twice per sample,
    // its band softly limited so the resonance sings instead of exploding
    const double sounding = note[v] + offset1;
    const double octs = (sounding - 60.0) / 12.0 * param (pKeyTrack) * 0.01
                      + param (pEnvAmt) * fe + param (pLfoCut) * lfo
                      + param (pVelCut) * (2.0f * vel[v] - 1.0f) + cutOffset[v];
    const double fc = juce::jlimit (15.0, sr * 0.47, (double) param (pCutoff) * std::exp2 (octs));
    const float f = (float) (2.0 * std::sin (juce::MathConstants<double>::pi * fc / (2.0 * sr)));
    const float r = param (pReso) * 0.01f;
    const float q = juce::jmax (0.004f, (1.0f - r) * (1.0f - r));
    float l = lp[v], b = bp[v];
    for (int i = 0; i < 2; ++i)
    {
        l += f * b;
        const float h = x - l - q * b;
        b += f * h;
        b -= 0.08f * b * b * b;
    }
    lp[v] = l;
    bp[v] = b;

    // ---- the amp: envelope, velocity, and the level the mix and resonance take away
    const float va = param (pVelAmp) * 0.01f;
    const float velCurve = (0.004f * juce::square (vel[v] * 127.0f + 64.0f) - 8.0f) / 137.9f;
    const float velGain = 1.0f - va + va * juce::jmax (0.0f, velCurve);
    const float comp = (3.2f - mix - 1.5f * noiseAmt * noiseAmt) * (1.5f - 0.5f * r) / 2.6f;
    const float y = l * ae * velGain * comp * juce::Decibels::decibelsToGain (param (pLevel)) * poolGain;
    outputs[0] = { y, y };
}

//==============================================================================
void DualVcoAnalogModule::applyPreset (int index)
{
    index = ((index % kNumPresets) + kNumPresets) % kNumPresets;
    const auto& p = kPresets[index];
    for (int i = 0; i < 29; ++i)
    {
        float value = p.v[i];
        const juce::String id (kPresetIds[i]);
        if (id == "octave")     value += 2.0f;                                  // -2..+2 -> its list
        if (id == "voices")     value = value >= 0.5f ? 1.0f : (float) kMaxVoices;
        if (id == "glide")      value = juce::jmax (0.5f, value);
        if (const auto* spec = getDescriptor().findParam (id))
            value = juce::jlimit (spec->minValue, spec->maxValue, value);
        setParameter (id, value);
    }
    setParameter ("level", kPresetLevel[index]);
    setParameter ("preset", (float) index);
}

void DualVcoAnalogModule::uiButtonClicked (const juce::String& paramId)
{
    const int current = (int) std::lround (getParameter ("preset"));
    if (paramId == "next")      applyPreset (current + 1);
    else if (paramId == "prev") applyPreset (current - 1);
}

juce::String DualVcoAnalogModule::titleSuffix() const
{
    if (std::lround (getParameter ("engine")) == 1)
        return " - Oscillators";
    const int i = (int) std::lround (getParameter ("preset"));
    return i >= 0 && i < kNumPresets ? juce::String (" - ") + kPresets[i].name : juce::String();
}

//==============================================================================
static ModuleDescriptor dualVcoAnalogDescriptor()
{
    const auto& p = kPresets[0].v;   // a new card starts as Init
    ModuleDescriptor d;
    d.typeId = "osc.dualvcoanalog";
    d.displayName = "Dual VCO";
    d.description =
        "Two analog engines in one card. Synth: a whole analog polysynth voice - two oscillators (Saw, Sine, Triangle, "
        "the second subtracted from the first), noise, a singing two-pole low-pass with its envelope, an amp envelope, "
        "an LFO and glide that can swoop every note in from Glide Bend or from the last note played; VCO lets the "
        "oscillators run free and drift, DCO keeps every note in tune. < and > at the top step through its presets, "
        "from Init. Oscillators: the classic Dual VCO - VCO 1 Triangle/Saw/Pulse/Noise, VCO 2 Saw/Pulse/Ring Mod with "
        "its own Pitch, in feet - which never gates, so the VCA goes after the filter (Volume with an Analog EG in Vol "
        "Mod); Pitch In takes an MG or envelope.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 40;
    d.sockets = {
        modIn    ("pitchIn",   "Pitch In"),
        midiIn   ("addMidiIn", "Add MIDI In"),
        audioOut ("audioOut",  "Audio Out")
    };
    const juce::StringArray waves { "Saw", "Sine", "Triangle" };
    auto syn = [] (ParamSpec spec) { return spec.alsoVisibleWhen ("engine", 0.0f); };   // the Synth engine's
    d.params = {
        syn (makeCombo  ("wave1",    "Osc 1",      waves, (int) p[0], 2, 2)),
        syn (makeCombo  ("wave2",    "Osc 2",      waves, (int) p[1], 2, 2)),
        syn (makeRotary ("oscMix",   "Osc Mix",    0.0f, 100.0f, p[2], 2, "%")),
        syn (makeRotary ("tune2",    "Osc 2 Tune", -24.0f, 24.0f, p[3], 3, "st", false, 1.0f)),
        syn (makeRotary ("fine2",    "Osc 2 Fine", -50.0f, 50.0f, p[4], 3, "ct")),
        syn (makeSteppedList ("octave", "Octave",  { "-2", "-1", "0", "+1", "+2" }, (int) p[5] + 2, 3)),
        syn (makeRotary ("noise",    "Noise",      0.0f, 100.0f, p[6], 3, "%")),
        syn (makeCombo  ("mode",     "Mode",       { "VCO", "DCO" }, 0, 4, 2)),
        syn (makeRotary ("drift",    "Drift",      0.0f, 100.0f, 40.0f, 4, "%").visibleWhen ("mode", 0.0f)),
        syn (makeRotary ("cutoff",   "Cutoff",     20.0f, 25000.0f, p[7], 5, "Hz", true)),
        syn (makeRotary ("reso",     "Resonance",  0.0f, 100.0f, p[8], 5, "%")),
        syn (makeRotary ("envAmt",   "Env Amt",    -8.0f, 8.0f, p[9], 5, "oct")),
        syn (makeRotary ("keyTrack", "Key Track",  0.0f, 100.0f, p[10], 5, "%")),
        syn (makeRotary ("velCut",   "Vel > Cut",  -4.5f, 4.5f, p[11], 6, "oct")),
        syn (makeRotary ("lfoCut",   "LFO > Cut",  0.0f, 4.0f, p[12], 6, "oct")),
        syn (makeRotary ("fAtt",     "F Attack",   1.0f, 10000.0f, p[13], 7, "ms", true)),
        syn (makeRotary ("fDec",     "F Decay",    1.0f, 10000.0f, p[14], 7, "ms", true)),
        syn (makeRotary ("fSus",     "F Sustain",  0.0f, 100.0f, p[15], 7, "%")),
        syn (makeRotary ("fRel",     "F Release",  1.0f, 10000.0f, p[16], 7, "ms", true)),
        syn (makeRotary ("att",      "Attack",     1.0f, 10000.0f, p[17], 8, "ms", true)),
        syn (makeRotary ("dec",      "Decay",      1.0f, 10000.0f, p[18], 8, "ms", true)),
        syn (makeRotary ("sus",      "Sustain",    0.0f, 100.0f, p[19], 8, "%")),
        syn (makeRotary ("rel",      "Release",    1.0f, 10000.0f, p[20], 8, "ms", true)),
        syn (makeRotary ("velAmp",   "Velocity",   0.0f, 100.0f, p[21], 6, "%")),
        syn (makeRotary ("lfoRate",  "LFO Rate",   0.02f, 20.0f, p[22], 9, "Hz", true)),
        syn (makeRotary ("vibrato",  "Vibrato",    0.0f, 2.0f, p[23], 9, "st")),
        syn (makeRotary ("pwm",      "PWM",        0.0f, 100.0f, p[24], 9, "%")),
        syn (makeRotary ("glide",    "Glide",      0.5f, 3000.0f, p[25], 10, "ms", true)),
        syn (makeRotary ("glideBend", "Glide Bend", -36.0f, 36.0f, p[26], 10, "st")),
        syn (makeCombo  ("glideMode", "Glide From", { "Bend Only", "Legato", "Always" }, (int) p[27], 10, 2)),
        makeRotary ("voices",   "Voices",     1.0f, (float) kMaxVoices, p[28] >= 0.5f ? 1.0f : (float) kMaxVoices, 1, {}, false, 1.0f).noMod(),
        syn (makeButton ("prev",     "<",          0, 1)),
        syn (makeButton ("next",     "Preset >",   0, 2)),
        makeRotary ("preset",   "Preset",     -1.0f, 64.0f, 0.0f, 0, {}, false, 1.0f).hide().noMod(),
        makeRotary ("level",    "Level",      -24.0f, 12.0f, kPresetLevel[0], 1, "dB")
    };
    d.params.push_back (makeOutputTrim (-12.0f));   // brought down to sit with the other sources

    // the engine, and the Oscillators engine's own knobs (the classic Dual VCO's)
    d.params.push_back (makeCombo ("engine", "Engine", { "Synth", "Oscillators" }, 0, 1, 2));
    auto osc = [&d] (ParamSpec spec) { d.params.push_back (spec.alsoVisibleWhen ("engine", 1.0f)); };
    osc (makeCombo  ("oWave1",  "VCO 1 Wave", { "Triangle", "Saw", "Pulse", "Noise" }, 1, 11, 2));
    osc (makeRotary ("oPw1",    "VCO 1 PW",    0.0f, 10.0f, 0.0f, 11).visibleWhen ("oWave1", 2.0f));
    osc (makeSteppedList ("oScale1", "VCO 1 Feet", { "32'", "16'", "8'", "4'" }, 2, 11));
    osc (makeCombo  ("oWave2",  "VCO 2 Wave", { "Saw", "Pulse 50%", "Pulse 10%", "Ring Mod" }, 0, 12, 2));
    osc (makeRotary ("oPitch2", "VCO 2 Pitch", -12.0f, 12.0f, 0.0f, 12, "st"));
    osc (makeSteppedList ("oScale2", "VCO 2 Feet", { "16'", "8'", "4'", "2'" }, 1, 12));
    osc (makeRotary ("oFine",   "Fine",       -50.0f, 50.0f, 0.0f, 13, "ct"));
    osc (makeRotary ("oLevel1", "VCO 1 Level", 0.0f, 10.0f, 10.0f, 13));
    osc (makeRotary ("oLevel2", "VCO 2 Level", 0.0f, 10.0f, 0.0f, 13));
    osc (makeRotary ("oAnalog", "Analog",      0.0f, 100.0f, 30.0f, 14, "%"));
    osc (makeRotary ("oPitchMod", "Pitch Mod", 0.0f, 24.0f, 1.0f, 14, "st"));
    osc (makeRotary ("oGlide",  "Glide", 0.0f, 1000.0f, 0.0f, 14, "ms").visibleWhen ("voices", 1.0f));

    d.rowSections = { { 2, "Oscillators" }, { 5, "Filter" }, { 7, "Filter Envelope" }, { 8, "Amp Envelope" },
                      { 9, "LFO" }, { 10, "Glide" }, { 11, "VCO 1" }, { 12, "VCO 2" }, { 13, "Mixer" } };
    return d;
}

AQUANODE_REGISTER_MODULE (DualVcoAnalogModule, dualVcoAnalogDescriptor)

#if AQUANODE_TEST_HARNESS
// (test harness, 6.9.21) a fixed seed for the start phases and the drift, so two runs can be compared
void dualVcoAnalogSeedForTest (aquanode::SynthModule& m, juce::int64 seed)
{
    if (auto* d = dynamic_cast<DualVcoAnalogModule*> (&m)) d->seedForTest (seed);
}
#endif
