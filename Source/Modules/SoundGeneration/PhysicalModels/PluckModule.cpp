#include "PluckModule.h"

using namespace aquanode;

void PluckModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // voice-steal de-click: a muted voice ramps out over a few ms instead of
    // being cut dead, and a re-used voice ramps back in
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void PluckModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float trigIn = inputs[0][0];
    if (trigIn > 0.5f && lastTrig[v] <= 0.5f)
    {
        noteOfVoice[v] = -1;    // sequencer trigger -> pitch from Note + Pitch In
        trigger (v);
    }
    lastTrig[v] = trigIn;

    // pitch: keyboard note when present, otherwise the Note param,
    // plus Pitch In in KeyTrack scaling (semitones / 60)
    const double baseNote = noteOfVoice[v] >= 0 ? (double) noteOfVoice[v]
                                                : (double) (int) param (pNote);
    const double semis = baseNote + (double) inputs[1][0] * 60.0;
    const double freq = juce::jlimit (20.0, 8000.0, midiNoteToHz (semis));
    if (harpMode()) { renderHarp (v, semis, freq, outputs); return; }
    const double delaySamples = juce::jlimit (2.0, (double) maxDelaySamples - 2.0, sampleRate / freq);

    if (burstSamples[v] < 0)    // freshly triggered: burst length = one string period
        burstSamples[v] = (int) delaySamples;

    // exciter: noise burst through a Color low-pass (dark pick .. bright pick)
    float excite = 0.0f;
    if (burstSamples[v] > 0)
    {
        --burstSamples[v];
        const float noise = random.nextFloat() * 2.0f - 1.0f;
        const float colorCoeff = 0.05f + 0.95f * param (pColor) * 0.01f;
        colorState[v] += colorCoeff * (noise - colorState[v]);
        excite = colorState[v];
    }

    // read the delayed sample (linear interpolation)
    double readPos = (double) writeIndex[v] - delaySamples;
    while (readPos < 0.0) readPos += maxDelaySamples;
    const int i0 = (int) readPos % maxDelaySamples;
    const int i1 = (i0 + 1) % maxDelaySamples;
    const float frac = (float) (readPos - std::floor (readPos));
    const float delayed = line[v][(size_t) i0] * (1.0f - frac) + line[v][(size_t) i1] * frac;

    // loop damping low-pass
    const float dampCoeff = juce::jlimit (0.02f, 1.0f, 1.0f - param (pDamp) * 0.0095f);
    lpfState[v] += dampCoeff * (delayed - lpfState[v]);

    // loop gain from the Decay time: amplitude falls to -60 dB over pDecay ms
    const double decaySamplesTotal = juce::jmax (1.0, param (pDecay) * 0.001 * sampleRate);
    const float feedback = juce::jlimit (0.0f, 0.9999f,
        (float) std::pow (0.001, delaySamples / decaySamplesTotal));

    const float looped = excite + feedback * lpfState[v];
    line[v][(size_t) writeIndex[v]] = looped;
    if (++writeIndex[v] >= maxDelaySamples)
        writeIndex[v] = 0;

    outputs[0][0] = looped;
    outputs[0][1] = looped;
}

// the Harp: a fitted string model (Shared/HarpString.h); Decay scales its ring, Damp the
// highs' decay, Color the finger (soft pad .. firm tip)
void PluckModule::renderHarp (int v, double semis, double freq, StereoFrame* outputs)
{
    if (harp == nullptr) { outputs[0] = { 0.0f, 0.0f }; return; }
    auto& h = harp[(size_t) v];
    if (burstSamples[v] < 0)
    {
        burstSamples[v] = 0;
        aquanode::harp::Shape sh;
        sh.t60Scale = juce::jlimit (0.03, 5.0, (double) param (pDecay) / 2000.0);
        sh.hiScale = std::pow (2.0, (30.0 - (double) param (pDamp)) / 25.0);
        sh.fingerScale = std::pow (2.0, (80.0 - (double) param (pColor)) / 30.0);
        harpFrom[v] = freq;
        h.start (semis, freq, harpVel[v] > 0.0f ? harpVel[v] : 0.85, sh, sampleRate, harpSeed++ * 2654435761ull);
    }
    const double y = harpEq[v].process (h.process (freq / juce::jmax (1.0, harpFrom[v]), sampleRate));
    outputs[0] = { (float) y, (float) y };
}

static ModuleDescriptor pluckDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.pluck";
    d.displayName = "Pluck";
    d.description =
        "Karplus-Strong plucked string: A noise burst exciting a tuned feedback delay line with "
        "damping in the loop. Model Harp plays a concert harp instead: a gut string plucked near its middle by a "
        "fingertip, two polarisations into the soundboard, fitted to a recorded harp note by note (Decay scales how "
        "long it rings, Damp how soon the highs die away, Color the finger from soft pad to firm tip). "
        "Played from the keyboard it is fully polyphonic; Trig In takes a "
        "Clock, Euclid or Step Seq gate, and Pitch In takes a Step Seq's Pitch Out or KeyTrack "
        "directly (both use the same semitones/60 scaling). MIDI In plays it from notes (the played keys while nothing is patched, a Piano Roll...): each note plucks at that pitch, alongside Trig In.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 6;
    d.sockets = {
        modIn    ("trigIn",   "Trig In"),
        modIn    ("pitchIn",  "Pitch In"),
        audioOut ("audioOut", "Audio Out"),
        midiIn   ("midiIn",   "MIDI In")
    };
    d.params = {
        makeSteppedList ("note", "Note", midiNoteNameChoices(), 60, 0),
        makeRotary ("decay", "Decay", 50.0f, 10000.0f, 2000.0f, 0, "ms", true),
        makeRotary ("damp",  "Damp",  0.0f, 100.0f, 30.0f, 0, "%"),
        makeRotary ("color", "Color", 0.0f, 100.0f, 80.0f, 0, "%")
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod(),
        makeCombo ("model", "Model", { "Karplus", "Harp" }, 0, 1, 2).noMod()
    };
    d.params.push_back (makeOutputTrim (-3.0f));   // brought down to sit with the other sources
    return d;
}

AQUANODE_REGISTER_MODULE (PluckModule, pluckDescriptor)
