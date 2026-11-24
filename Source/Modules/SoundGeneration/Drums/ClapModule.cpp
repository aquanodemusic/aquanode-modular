#include "ClapModule.h"

using namespace aquanode;

static constexpr float kSnappyGain = 4.5f, kClackGain = 6.0f;   // into a soft clip (as a mastered drum track): next to Classic

void ClapModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
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

void ClapModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float trigIn = inputs[0][0];
    if (trigIn > 0.5f && lastTrig[v] <= 0.5f)
        trigger (v);
    lastTrig[v] = trigIn;

    if (const int m = model(); m > 0)
    {
        const float y = std::tanh (m == 1 ? snappy[v].process() * kSnappyGain : clack[v].process() * kClackGain);
        outputs[0][0] = y;
        outputs[0][1] = y;
        return;
    }

    if (burstEnv[v] < 1.0e-5f && tailEnv[v] < 1.0e-5f && ! running[v])
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    // re-fire the short bursts, Spread milliseconds apart
    if (burstsLeft[v] > 0)
    {
        burstTimer[v] += 1.0 / sampleRate;
        if (burstTimer[v] >= param (pSpread) * 0.001)
        {
            burstTimer[v] = 0.0;
            if (--burstsLeft[v] > 0)
                burstEnv[v] = 1.0f;          // next hand
            else
            {
                tailEnv[v] = 1.0f;           // last burst hands over to the tail
                running[v] = false;
            }
        }
    }

    burstEnv[v] *= std::exp ((float) (-1.0 / (juce::jmax (1.0f, param (pBurstDecay)) * 0.001 * sampleRate)));
    tailEnv[v]  *= std::exp ((float) (-1.0 / (juce::jmax (5.0f, param (pTailDecay))  * 0.001 * sampleRate)));

    const float noise = random.nextFloat() * 2.0f - 1.0f;
    const float bpFreq = juce::jlimit (400.0f, 6000.0f, 700.0f + param (pTone) * 45.0f);
    const float f = 2.0f * std::sin (juce::MathConstants<float>::pi
                                     * juce::jmin (bpFreq, (float) (sampleRate * 0.22)) / (float) sampleRate);
    bpLow[v] += f * bpBand[v];
    const float hp = noise - bpLow[v] - bpBand[v] * 0.55f;   // fairly wide band
    bpBand[v] += f * hp;

    const float tailMix = param (pTailMix) * 0.01f;
    const float amp = burstEnv[v] + tailEnv[v] * tailMix;
    const float out = std::tanh (bpBand[v] * amp * 2.2f);

    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor clapDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.clap";
    d.displayName = "Clap";
    d.description =
        "The classic analog clap trick: three short noise bursts a few milliseconds apart (that stutter is "
        "what the ear reads as many hands), then a diffuse tail. Trig In takes a Clock, Euclid or "
        "Step Seq gate. MIDI In plays it from notes instead (a Piano Roll, an Arp...; with nothing patched it hears the played keys): every note is a clap, alongside Trig In. "
        "Model Snappy and Clack were measured from a drum track: Snappy is a tight clap (three cracks, two resonances, a short room), "
        "Clack a bright crack with a thin hiss behind it. Their knobs scale the measured sound: Tone moves the colour, Spread the cracks, "
        "Burst Dec and Tail Dec the lengths, Tail Mix the room.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 9;
    d.sockets = {
        modIn    ("trigIn",   "Trig In"),
        audioOut ("audioOut", "Audio Out"),
        midiIn   ("midiIn",   "MIDI In")
    };
    d.params = {
        makeRotary ("tone",       "Tone",      0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("spread",     "Spread",    3.0f, 40.0f, 10.0f, 0, "ms"),
        makeRotary ("burstDecay", "Burst Dec", 1.0f, 60.0f, 8.0f, 0, "ms", true),
        makeRotary ("tailDecay",  "Tail Dec",  20.0f, 1200.0f, 220.0f, 1, "ms", true),
        makeRotary ("tailMix",    "Tail Mix",  0.0f, 100.0f, 60.0f, 1, "%"),
        makeRotary ("voices",     "Voices",    1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod(),
        makeCombo ("model", "Model", { "Classic", "Snappy", "Clack" }, 0, 1, 2).noMod()
    };
    d.params.push_back (makeOutputTrim (-6.0f));   // brought down to sit with the other sources
    return d;
}

AQUANODE_REGISTER_MODULE (ClapModule, clapDescriptor)
