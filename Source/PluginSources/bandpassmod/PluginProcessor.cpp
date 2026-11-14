#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

BandpassModulatorAudioProcessor::BandpassModulatorAudioProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BandpassModulator", createLayout())
{
    const char* ids[] = { "volDry", "volWet", "lowLimit", "highLimit", "fine", "speed", "modType", "bandwidth",
                          "strength", "dist", "delaySpeed", "delayVol", "autopan", "skewSpeed", "skewAmt",
                          "noteLock", "midiLock", "seed" };
    for (int i = 0; i < pNote0; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
    for (int n = 0; n < 12; ++n) p[pNote0 + n] = apvts.getRawParameterValue ("note" + juce::String (n));
}

juce::AudioProcessorValueTreeState::ParameterLayout BandpassModulatorAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto hz = [] (float value, int) { return value < 1000.0f ? juce::String (juce::roundToInt (value)) + " Hz"
                                                              : juce::String (value / 1000.0f, 2) + " kHz"; };
    auto pct = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + " %"; };
    auto add = [&v] (auto p) { v.push_back (std::move (p)); };
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;

    add (std::make_unique<F> (juce::ParameterID { "volDry", 1 }, "Vol Dry", juce::NormalisableRange<float> (0.0f, 1.5f), 0.0f, A().withStringFromValueFunction (pct)));
    add (std::make_unique<F> (juce::ParameterID { "volWet", 1 }, "Vol Wet", juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f, A().withStringFromValueFunction (pct)));
    add (std::make_unique<F> (juce::ParameterID { "lowLimit", 1 }, "Low Limit", juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f), 200.0f, A().withStringFromValueFunction (hz)));
    add (std::make_unique<F> (juce::ParameterID { "highLimit", 1 }, "High Limit", juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f), 4000.0f, A().withStringFromValueFunction (hz)));
    add (std::make_unique<F> (juce::ParameterID { "fine", 1 }, "Fine", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
                              A().withStringFromValueFunction ([] (float x, int) { return juce::String (juce::roundToInt (x)) + " ct"; })));
    add (std::make_unique<C> (juce::ParameterID { "speed", 1 }, "Speed", speedNames(), 7));
    add (std::make_unique<C> (juce::ParameterID { "modType", 1 }, "Mod Type", modTypeNames(), 0));
    add (std::make_unique<F> (juce::ParameterID { "bandwidth", 1 }, "Bandwidth", juce::NormalisableRange<float> (0.03f, 3.0f, 0.0f, 0.45f), 0.35f,
                              A().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 2) + " oct"; })));
    add (std::make_unique<F> (juce::ParameterID { "strength", 1 }, "Strength", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, A().withStringFromValueFunction (pct)));
    add (std::make_unique<F> (juce::ParameterID { "dist", 1 }, "Dist", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, A().withStringFromValueFunction (pct)));
    add (std::make_unique<C> (juce::ParameterID { "delaySpeed", 1 }, "Delay Speed", delayNames(), 5));
    add (std::make_unique<F> (juce::ParameterID { "delayVol", 1 }, "Delay Vol", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, A().withStringFromValueFunction (pct)));
    add (std::make_unique<F> (juce::ParameterID { "autopan", 1 }, "Autopan", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, A().withStringFromValueFunction (pct)));
    add (std::make_unique<F> (juce::ParameterID { "skewSpeed", 1 }, "Skew Speed", juce::NormalisableRange<float> (0.02f, 8.0f, 0.0f, 0.4f), 0.25f,
                              A().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 2) + " Hz"; })));
    add (std::make_unique<F> (juce::ParameterID { "skewAmt", 1 }, "Skew Amt", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, A().withStringFromValueFunction (pct)));
    add (std::make_unique<B> (juce::ParameterID { "noteLock", 1 }, "Note Lock", false));
    add (std::make_unique<B> (juce::ParameterID { "midiLock", 1 }, "Lock to Midi In", false));
    add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seed", 1 }, "Seed", 0, 999, 0));
    for (int n = 0; n < 12; ++n)
        add (std::make_unique<B> (juce::ParameterID { "note" + juce::String (n), 1 }, juce::String ("Note ") + kNoteNames[n], n == 0 || n == 7));
    return { v.begin(), v.end() };
}

bool BandpassModulatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && l.getMainInputChannelSet() == out;
}

void BandpassModulatorAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    std::fill (std::begin (ic1), std::end (ic1), 0.0f);
    std::fill (std::begin (ic2), std::end (ic2), 0.0f);
    delayBuf.setSize (2, (int) (sr * 4.5) + 8);
    delayBuf.clear();
    delayWrite = 0;
    internalSteps = internalSeconds = 0.0;
    lastPpq = -1.0;
}

double BandpassModulatorAudioProcessor::speedBeats (int i)
{
    static const double b[12] = { 16, 8, 4, 2, 1.5, 1, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit (0, 11, i)];
}

double BandpassModulatorAudioProcessor::delayBeats (int i)
{
    static const double b[10] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 0.75, 1, 1.5, 2, 4 };
    return b[juce::jlimit (0, 9, i)];
}

void BandpassModulatorAudioProcessor::rebuildNoteList (float lo, float hi, int mask)
{
    if (lo == notesLo && hi == notesHi && mask == notesMask) return;
    notesLo = lo; notesHi = hi; notesMask = mask;
    notes.clear();
    for (int m = 0; m < 136; ++m)
    {
        if ((mask & (1 << (m % 12))) == 0) continue;
        const float f = 440.0f * std::exp2 ((float) (m - 69) / 12.0f);
        if (f >= lo && f <= hi) notes.push_back (f);
    }
}

void BandpassModulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (2, buffer.getNumChannels());

    // held notes for Lock to Midi In
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) held[(size_t) m.getNoteNumber()] = true;
        else if (m.isNoteOff()) held[(size_t) m.getNoteNumber()] = false;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) held.fill (false);
    }
    int heldMask = 0;
    for (int k = 0; k < 128; ++k) if (held[(size_t) k]) heldMask |= 1 << (k % 12);
    heldPitchClassMask.store (heldMask);

    // ---- tempo and position: the host's when it plays, else our own clock ----
    double bpm = 120.0, ppq = -1.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b && *b > 0.0) bpm = *b;
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) { ppq = *q; hostPlaying = true; }
        }

    float lo = p[pLow]->load(), hi = p[pHigh]->load();
    if (lo > hi) std::swap (lo, hi);                    // the limits are interchangeable
    const float fineMul = std::exp2 (p[pFine]->load() / 1200.0f);
    const int type = (int) p[pType]->load();
    const float strength = p[pStrength]->load();
    const double beatsPerStep = speedBeats ((int) p[pSpeed]->load());
    const double stepsPerSecond = bpm / 60.0 / beatsPerStep;
    const float skewAmt = p[pSkewAmt]->load(), skewHz = p[pSkewSpeed]->load();
    const double seed = (double) p[pSeed]->load() * 101.0 + 1.0;
    const float autopan = p[pAutopan]->load();

    int mask = 0;
    for (int k = 0; k < 12; ++k) if (p[pNote0 + k]->load() > 0.5f) mask |= 1 << k;
    if (p[pMidiLock]->load() > 0.5f && heldMask != 0) mask = heldMask;
    const bool lock = (p[pLock]->load() > 0.5f || (p[pMidiLock]->load() > 0.5f && heldMask != 0)) && mask != 0;
    if (lock) rebuildNoteList (lo, hi, mask);
    const bool useNotes = lock && ! notes.empty();

    const float bwOct = p[pBw]->load();
    const float twoBw = std::exp2 (bwOct);
    const float q = std::sqrt (twoBw) / (twoBw - 1.0f);
    const float k = 1.0f / q;

    const float dist = p[pDist]->load();
    const float drive = 1.0f + dist * 14.0f, driveNorm = 1.0f / std::tanh (drive);
    const float dry = p[pDry]->load(), wet = p[pWet]->load();
    const float delVol = p[pDelVol]->load();
    const float delayTarget = (float) juce::jlimit (1.0, sr * 4.0, delayBeats ((int) p[pDelSpeed]->load()) * 60.0 / bpm * sr);
    const float fb = 0.3f + 0.35f * delVol;
    const int dlen = delayBuf.getNumSamples();

    const float logLo = std::log (lo), logHi = std::log (juce::jmax (hi, lo * 1.001f));
    const float freqSmooth = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));     // 4 ms: jumps without clicks
    const float panSmooth = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
    const float delaySmoothK = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));

    // a transport jump (or start) puts our step count where the song is
    if (hostPlaying && (lastPpq < 0.0 || std::abs (ppq - lastPpq) > 0.05))
        internalSteps = ppq / beatsPerStep;

    float* L = buffer.getWritePointer (0);
    float* R = chans > 1 ? buffer.getWritePointer (1) : nullptr;
    float* dL = delayBuf.getWritePointer (0);
    float* dR = delayBuf.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        // where we are, in steps; Skew pushes the step boundaries back and forth in time
        const double secs = hostPlaying ? (ppq + (double) i / sr * bpm / 60.0) * 60.0 / bpm : internalSeconds;
        const double steps = hostPlaying ? (ppq + (double) i / sr * bpm / 60.0) / beatsPerStep : internalSteps;
        const double warped = steps + skewAmt * 0.45 * std::sin (juce::MathConstants<double>::twoPi * skewHz * secs);
        const double stepIndex = std::floor (warped);
        const float frac = (float) (warped - stepIndex);

        // the place in the range, 0..1
        float u;
        switch (type)
        {
            case 1: { const float a = seededRandom (stepIndex + seed), b = seededRandom (stepIndex + 1.0 + seed);
                      const float t = frac * frac * (3.0f - 2.0f * frac); u = a + (b - a) * t; break; }
            case 2: u = 0.5f + 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * warped); break;
            case 3: u = 1.0f - std::abs (2.0f * frac - 1.0f); break;
            case 4: u = frac; break;
            case 5: u = 1.0f - frac; break;
            case 6: case 7:
            {
                const int count = useNotes ? (int) notes.size() : 8;
                int idx = (int) std::fmod (stepIndex, (double) count);
                if (idx < 0) idx += count;
                if (type == 7) idx = count - 1 - idx;
                u = count > 1 ? (float) idx / (float) (count - 1) : 0.5f;
                break;
            }
            default: u = seededRandom (stepIndex + seed); break;
        }
        u = 0.5f + (u - 0.5f) * strength;

        float target;
        if (useNotes)
        {
            if (type == 0)          // random: one of the allowed notes
                target = notes[(size_t) juce::jmin ((int) notes.size() - 1, (int) (u * (float) notes.size()))];
            else                    // moving: the nearest allowed note
            {
                const float f = std::exp (logLo + (logHi - logLo) * u);
                target = notes.front();
                for (float nf : notes) if (std::abs (std::log (nf / f)) < std::abs (std::log (target / f))) target = nf;
            }
        }
        else
            target = std::exp (logLo + (logHi - logLo) * u);
        logFreqSmoothed += (std::log (target * fineMul) - logFreqSmoothed) * freqSmooth;

        const float panTarget = (seededRandom (stepIndex * 1.618 + seed + 7777.0) * 2.0f - 1.0f) * autopan;
        panSmoothed += (panTarget - panSmoothed) * panSmooth;

        // the band-pass (TPT state variable), normalised to unity at its centre
        const float fc = juce::jlimit (10.0f, 0.45f * (float) sr, std::exp (logFreqSmoothed));
        const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;

        float in[2] = { L[i], R != nullptr ? R[i] : L[i] };
        float bp[2];
        for (int c = 0; c < 2; ++c)
        {
            const float v3 = in[c] - ic2[c];
            const float v1 = a1 * ic1[c] + a2 * v3;
            const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = 2.0f * v1 - ic1[c];
            ic2[c] = 2.0f * v2 - ic2[c];
            bp[c] = v1 * k;
            if (dist > 0.001f) bp[c] = std::tanh (bp[c] * drive) * driveNorm;
        }
        // autopan: equal power
        const float pan = juce::jlimit (-1.0f, 1.0f, panSmoothed);
        const float gl = std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41421356f;
        const float gr = std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.41421356f;
        float wl = bp[0] * gl, wr = bp[1] * gr;

        // the echo on the wet signal, a little cross-fed
        delaySmoothed += (delayTarget - delaySmoothed) * delaySmoothK;
        if (delVol > 0.001f)
        {
            float rp = (float) delayWrite - delaySmoothed;
            while (rp < 0.0f) rp += (float) dlen;
            const int i0 = (int) rp, i1 = (i0 + 1) % dlen;
            const float fr = rp - (float) i0;
            const float el = dL[i0] + (dL[i1] - dL[i0]) * fr, er = dR[i0] + (dR[i1] - dR[i0]) * fr;
            dL[delayWrite] = wl + (0.7f * el + 0.3f * er) * fb;
            dR[delayWrite] = wr + (0.7f * er + 0.3f * el) * fb;
            wl += el * delVol;
            wr += er * delVol;
        }
        else
            dL[delayWrite] = dR[delayWrite] = 0.0f;
        delayWrite = (delayWrite + 1) % dlen;

        L[i] = in[0] * dry + wl * wet;
        if (R != nullptr) R[i] = in[1] * dry + wr * wet;

        internalSteps += stepsPerSecond / sr;
        internalSeconds += 1.0 / sr;
    }
    if (hostPlaying) lastPpq = ppq + (double) n / sr * bpm / 60.0;
    else lastPpq = -1.0;

    currentFreq.store (std::exp (logFreqSmoothed));
    currentPan.store (panSmoothed);
}

juce::AudioProcessorEditor* BandpassModulatorAudioProcessor::createEditor()
{
    return new BandpassModulatorAudioProcessorEditor (*this);
}

void BandpassModulatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BandpassModulatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BandpassModulatorAudioProcessor();
}
