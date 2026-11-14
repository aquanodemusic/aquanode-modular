#include "RotaryModule.h"

using namespace aquanode;

namespace
{
    // rotor speeds in Hz at Rate 100 %: { drum, horn } for Stop, Slow, Fast
    constexpr double kSpeeds[3][2] = { { 0.0, 0.0 }, { 0.588, 0.792 }, { 6.37, 7.68 } };
    // seconds to get 90 % of the way there at Inertia's middle setting, per target:
    // { drum, horn } - the light horn is always quicker than the heavy drum
    constexpr double kSpinTime[3][2] = { { 0.12, 0.10 }, { 0.27, 0.18 }, { 0.14, 0.09 } };
}

void RotaryModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void RotaryModule::reset()
{
    std::fill (std::begin (buf), std::end (buf), 0.0f);
    bufPos = 0;
    xo1 = xo2 = 0.0f;
    hornPhase = 1.6;
    drumPhase = 0.0;
    const int sp = juce::jlimit (0, 2, (int) std::lround (getParameterBase (pSpeed)));
    const double rate = getParameterBase (pRate) * 0.01;
    drumSpeed = kSpeeds[sp][0] * rate;
    hornSpeed = kSpeeds[sp][1] * rate;
}

void RotaryModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const double sr = sampleRate;
    const float inL = inputs[0][0], inR = inputs[0][1];

    // ---- the rotors: each heads for its speed with its own inertia
    const int sp = juce::jlimit (0, 2, (int) std::lround (param (pSpeed)));
    const double rate = param (pRate) * 0.01;
    const double inertia = 0.25 * std::pow (48.0, (double) param (pInertia) * 0.01);
    // a slowing rotor coasts (the time is the target's: Slow / Stop)
    const double dTime = kSpinTime[sp][0] * inertia, hTime = kSpinTime[sp][1] * inertia;
    const double dCoef = 1.0 - std::pow (10.0, -1.0 / (sr * juce::jmax (0.001, dTime)));
    const double hCoef = 1.0 - std::pow (10.0, -1.0 / (sr * juce::jmax (0.001, hTime)));
    drumSpeed += dCoef * (kSpeeds[sp][0] * rate - drumSpeed);
    hornSpeed += hCoef * (kSpeeds[sp][1] * rate - hornSpeed);

    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    drumPhase += twoPi * drumSpeed / sr;
    hornPhase += twoPi * hornSpeed / sr;
    if (drumPhase > twoPi) drumPhase -= twoPi;
    if (hornPhase > twoPi) hornPhase -= twoPi;

    const float hc = (float) std::cos (hornPhase);
    const float hc3 = hc * hc * hc;   // the horn's narrow lobe
    const float hs = (float) std::sin (hornPhase);
    const float dc = (float) std::cos (drumPhase);
    const float ds = (float) std::sin (drumPhase);

    // ---- the cabinet amp, then the crossover (two one-poles: lows / the rest)
    float x = 0.5f * (inL + inR);
    if (const float drive = param (pDrive) * 0.01f; drive > 0.0f)
    {
        const float k = 1.0f + 11.0f * drive * drive;
        x = std::tanh (x * k) / std::pow (k, 0.7f);
    }

    const float xoHz = juce::jlimit (60.0f, (float) sr * 0.4f, param (pXover));
    const float c = xoMemo (xoHz / (float) sr, [] (float f) { return std::exp (-juce::MathConstants<float>::twoPi * f); });
    xo1 = c * (xo1 - x) + x;
    xo2 = c * (xo2 - xo1) + xo1;
    const float low = xo2, high = x - xo2;

    // ---- throb: the level each rotor sends out as it turns
    const float hornThrob = 0.9f * juce::square (param (pHornThrob) * 0.01f);
    const float drumThrob = 0.9f * juce::square (param (pDrumThrob) * 0.01f);
    float h = (1.0f - hornThrob * hc3) * high;
    float l = (1.0f - drumThrob * dc) * low;

    // ---- the horn's Doppler: its sound read back from a line whose length
    // follows the horn round, added to the direct sound
    const float depth = juce::square (param (pHornDepth) * 0.01f) * (float) sr / 760.0f;
    bufPos = (bufPos + kBufSize - 1) & (kBufSize - 1);
    buf[bufPos] = h;
    const float dly = juce::jlimit (0.0f, (float) (kBufSize - 4), depth * (1.0f + hc3));
    const int di = (int) dly;
    const float frac = dly - (float) di;
    const float a0 = buf[(bufPos + di) & (kBufSize - 1)], a1 = buf[(bufPos + di + 1) & (kBufSize - 1)];
    h += a0 + frac * (a1 - a0);

    // ---- width: each rotor swings its sound across
    const float hornWidth = juce::square (param (pHornWidth) * 0.01f);
    const float drumWidth = juce::square (param (pDrumWidth) * 0.01f);
    const float hw = h * hornWidth * hs, lw = l * drumWidth * ds;
    float outL = l + h + hw - lw;
    float outR = l + h - hw + lw;

    const float level = juce::Decibels::decibelsToGain (param (pLevel)) * 0.95f;
    outL *= level;
    outR *= level;

    const float mix = juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f);
    outputs[0][0] = inL + mix * (outL - inL);
    outputs[0][1] = inR + mix * (outR - inR);
}

static ModuleDescriptor rotaryDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.rotary";
    d.displayName = "Rotary";
    d.description =
        "A rotating speaker cabinet: a bass drum and a treble horn, each spinning at its own speed with its own "
        "inertia, swinging the sound around the room - Throb is the level, Width the sweep across, Depth the "
        "horn's Doppler swirl. Speed switches Stop / Slow / Fast, the rotors speeding up and coasting down as "
        "real ones do; cable a Gate onto it to switch it from the keys.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 21;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeSteppedList ("speed",  "Speed",      { "Stop", "Slow", "Fast" }, 1, 0),
        makeRotary ("rate",       "Rate",       10.0f, 200.0f, 100.0f, 0, "%"),
        makeRotary ("inertia",    "Inertia",    0.0f, 100.0f, 60.0f, 0, "%"),
        makeRotary ("xover",      "X-Over",     200.0f, 4000.0f, 800.0f, 0, "Hz", true),
        makeRotary ("hornDepth",  "Horn Depth", 0.0f, 100.0f, 60.0f, 1, "%"),
        makeRotary ("hornThrob",  "Horn Throb", 0.0f, 100.0f, 70.0f, 1, "%"),
        makeRotary ("hornWidth",  "Horn Width", 0.0f, 100.0f, 70.0f, 1, "%"),
        makeRotary ("drumThrob",  "Drum Throb", 0.0f, 100.0f, 50.0f, 2, "%"),
        makeRotary ("drumWidth",  "Drum Width", 0.0f, 100.0f, 50.0f, 2, "%"),
        makeRotary ("drive",      "Drive",      0.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("level",      "Level",      -24.0f, 12.0f, 0.0f, 3, "dB"),
        makeRotary ("mix",        "Mix",        0.0f, 100.0f, 100.0f, 3, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RotaryModule, rotaryDescriptor)
