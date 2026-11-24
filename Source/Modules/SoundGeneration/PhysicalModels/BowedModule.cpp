#include "BowedModule.h"

using namespace aquanode;

void BowedModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
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

void BowedModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double sr = sampleRate;
    const double freq = juce::jmax (20.0, midiNoteToHz ((double) (glide.next (v, glideMillis(), isMonoVoice(),
                                                                          ! pool.isMuted (v), sr) + pitchBendSemitones())));
    const bool envConnected = isInputConnected (1);
    const float envGain = gate.next (v, envConnected, inputs[1][0], sr);

    // the playing: the note (its gate or Env In) moves the bow, velocity sets
    // how fast, aftertouch - or the note's own MPE pressure - leans into it;
    // Pressure is how hard the bow presses (or the breath blows), Pressure In
    // rides on that knob
    float pressNow = 0.0f;
    if (midiControls != nullptr)
        pressNow = juce::jmax (mpeVoicePressure (v), midiControls->aftertouch);   // (6.9.12: unless an MPE module takes it)
    touch[v] += (pressNow - touch[v]) * (1.0f - std::exp (-1.0f / (0.006f * (float) sr)));
    const float lean = param (pAfterTouch) * 0.01f * touch[v];
    const float pressure = juce::jlimit (0.0f, 1.0f, param (pPressure) * 0.01f
                                                     + param (pPressMod) * 0.01f * inputs[0][0] + 0.5f * lean);
    const float speedTarget = envGain * (0.45f + 0.55f * velocity[v] + 0.6f * lean);
    // the bow (breath) comes in over ~25 ms and leaves over ~60 ms
    const float ease = speedTarget > bowSpeed[v] ? 1.0f - std::exp (-1.0f / (0.025f * (float) sr))
                                                 : 1.0f - std::exp (-1.0f / (0.06f * (float) sr));
    bowSpeed[v] += ease * (speedTarget - bowSpeed[v]);

    const float damp = param (pDamp) * 0.01f;
    auto readAt = [] (const std::vector<float>& buf, int write, double delay)
    {
        double rp = (double) write - delay;
        while (rp < 0.0) rp += maxDelaySamples;
        const int i0 = (int) rp % maxDelaySamples, i1 = (i0 + 1) % maxDelaySamples;
        const float frac = (float) (rp - std::floor (rp));
        return buf[(size_t) i0] * (1.0f - frac) + buf[(size_t) i1] * frac;
    };

    float y;
    if ((int) param (pMode) == 0)
    {
        // ---- BOW: the string either side of the bow, the friction table between
        // (the loop's filters add about a sample: measured in tune at A3)
        const double total = juce::jlimit (8.0, (double) maxDelaySamples - 4.0, sr / freq - 1.0);
        const double beta = juce::jlimit (0.02, 0.5, (double) param (pPosition) * 0.01);
        const double bridgeLen = juce::jmax (2.0, total * beta), neckLen = juce::jmax (2.0, total - bridgeLen);
        const float bridgeOut = readAt (bridge[v], writeIndex[v], bridgeLen);
        const float neckOut = readAt (line[v], writeIndex[v], neckLen);

        // the string's losses at the bridge: a one-pole low-pass (Damp darker)
        const float pole = 0.42f + 0.5f * damp - (float) (0.1 * 22050.0 / sr);
        lpState[v] = 0.95f * (1.0f - pole) * bridgeOut + pole * lpState[v];
        const float bridgeRefl = -lpState[v];
        const float nutRefl = -neckOut;
        const float stringVel = bridgeRefl + nutRefl;

        const float bowVel = (0.03f + 0.2f * bowSpeed[v]) * juce::jmin (1.0f, bowSpeed[v] * 4.0f);
        const float velDiff = bowVel - stringVel;
        // the friction: sticks for small differences, slips for large (STK's bow table)
        const float slope = 5.0f - 4.0f * pressure;
        const float t = std::abs ((velDiff + 0.001f) * slope) + 0.75f;
        const float table = juce::jlimit (0.01f, 1.0f, 1.0f / (t * t * t * t));
        const float newVel = velDiff * table;

        line[v][(size_t) writeIndex[v]] = juce::jlimit (-4.0f, 4.0f, bridgeRefl + newVel);
        bridge[v][(size_t) writeIndex[v]] = juce::jlimit (-4.0f, 4.0f, nutRefl + newVel);

        // the body: what the bridge passes on, a little rounded
        bodyLp[v] += 0.55f * (bridgeOut - bodyLp[v]);
        y = bodyLp[v] * 1.05f;   // (about as loud as Wind and the other sources)
    }
    else
    {
        // ---- BLOW: a bore and a reed (STK's clarinet)
        const double len = juce::jlimit (4.0, (double) maxDelaySamples - 4.0, 0.5 * sr / freq - 1.5);
        const float boreOut = readAt (line[v], writeIndex[v], len);
        // the bore's losses: a one-zero, darker with Damp
        const float oz = 0.5f * (boreOut + zeroState[v]);
        zeroState[v] = boreOut;
        lpState[v] += (1.0f - 0.7f * damp) * (oz - lpState[v]);
        noiseSeed[v] ^= noiseSeed[v] << 13; noiseSeed[v] ^= noiseSeed[v] >> 17; noiseSeed[v] ^= noiseSeed[v] << 5;
        const float noise = (float) (noiseSeed[v] & 0xffffff) / 8388608.0f - 1.0f;
        float breath = bowSpeed[v] * (0.5f + 0.42f * pressure) * juce::jmin (1.0f, bowSpeed[v] * 3.0f);
        breath += breath * 0.12f * noise;
        float diff = -0.95f * lpState[v] - breath;
        const float reed = juce::jlimit (-1.0f, 1.0f, 0.7f - 0.3f * diff);
        const float w = juce::jlimit (-2.0f, 2.0f, breath + diff * reed);
        line[v][(size_t) writeIndex[v]] = w;
        y = boreOut * 0.85f;
    }

    if (++writeIndex[v] >= maxDelaySamples)
        writeIndex[v] = 0;

    // no DC out of the bore or the bow's push
    const float dc = y - dcX[v] + 0.995f * dcY[v];
    dcX[v] = y;
    dcY[v] = std::isfinite (dc) ? dc : 0.0f;
    if (! std::isfinite (dc))
    {
        // something blew up: a quiet string again (the note goes on)
        std::fill (line[v].begin(), line[v].end(), 0.0f);
        std::fill (bridge[v].begin(), bridge[v].end(), 0.0f);
        lpState[v] = zeroState[v] = bodyLp[v] = dcX[v] = dcY[v] = 0.0f;
    }

    const float out = std::tanh (dcY[v] * 1.4f) * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor bowedDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.bowed";
    d.displayName = "Bowed";
    d.description =
        "The sustaining half of physical modelling: where Pluck excites a string once and lets it "
        "move, this keeps feeding energy in, so it sings as long as you hold the note. Bow is a "
        "violin's stick-slip friction (a string bowed between bridge and neck); Blow is a clarinet's reed and bore. "
        "It plays from the keys alone - nothing needs patching. Pressure is the "
        "expressive control - too little and it whispers, too much and it sticks - and it plays from "
        "the keys: velocity is how hard the bow starts, aftertouch (or MPE pressure) leans into it by "
        "After Touch. Pressure In is optional: patch an LFO there for tremolo bowing or an envelope for "
        "swells, Press Mod sets how much. Pairs beautifully with Voices = 1 and Glide.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 14;
    d.sockets = {
        modIn    ("pressIn",   "Pressure In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add MIDI In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",   0.0f, 1.0f, 0.7f, 0),
        makeCombo  ("mode",     "Mode",     { "Bow", "Blow" }, 0, 0, 2),
        makeRotary ("pressure", "Pressure", 0.0f, 100.0f, 45.0f, 0, "%"),
        makeRotary ("position", "Position", 2.0f, 50.0f, 14.0f, 1, "%"),
        makeRotary ("damp",     "Damp",     0.0f, 100.0f, 35.0f, 1, "%"),
        makeRotary ("pressMod", "Press Mod", -100.0f, 100.0f, 50.0f, 1, "%").whenMissing (0.0f),
        makeRotary ("voices",   "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f),
        makeRotary ("afterTouch", "After Touch", 0.0f, 100.0f, 60.0f, 2, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BowedModule, bowedDescriptor)
