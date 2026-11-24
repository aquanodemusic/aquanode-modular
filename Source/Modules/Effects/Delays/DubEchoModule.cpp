#include "DubEchoModule.h"

using namespace aquanode;

namespace
{
    const juce::StringArray& dubDivisions()
    {
        static const juce::StringArray c { "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2" };
        return c;
    }

    double dubBeats (int i)
    {
        static const double b[] = { 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0 };
        return b[juce::jlimit (0, 9, i)];
    }

    // Head 2's place on the tape, as a ratio of the main head's time
    const juce::StringArray& head2Choices()
    {
        static const juce::StringArray c { "Off", "x1/4", "x1/3", "x1/2", "x2/3", "x3/4", "x4/3", "x3/2", "x5/3", "x2" };
        return c;
    }

    double head2Ratio (int i)
    {
        static const double r[] = { 0.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 4.0 / 3.0, 1.5, 5.0 / 3.0, 2.0 };
        return r[juce::jlimit (0, 9, i)];
    }

    // the measured tone filter: a resonant 4-pole low-pass over a shelf ~11.4 dB down
    constexpr float kToneFloor = 0.268f;
    constexpr double kToneQ1 = 1.65, kToneQ2 = 0.65;

    // the tape's saturation: odd harmonics, soft, ceiling ~0.87 (fitted to the 3rd / 5th
    // harmonics and the compression measured from -20 dB to +10 dB into the tape)
    inline float tapeSat (float x) noexcept
    {
        const float k = 1.15f;
        return x / std::sqrt (1.0f + k * k * x * x);
    }
}

void DubEchoModule::Biquad::setLowpass (double fc, double q, double sr) noexcept
{
    fc = juce::jlimit (10.0, sr * 0.45, fc);
    const double w = juce::MathConstants<double>::twoPi * fc / sr;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (float) ((1.0 - cw) * 0.5 / a0);
    b1 = (float) ((1.0 - cw) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * cw / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void DubEchoModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    for (auto& c : ch)
        c.line.assign ((size_t) (sr * kMaxSeconds) + 16, 0.0f);
    smCoef = 1.0f - std::exp (-1.0f / (0.22f * (float) sr));          // a tape speed change takes a moment
    topCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (17800.0f, (float) sr * 0.42f) / (float) sr);
    dcCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 12.0f / (float) sr);
    lastTone = -1.0f;
    // the hiss's band, as measured on the RE-201: a gentle rise to ~1 kHz, gone by 10 kHz
    hissHpCoef = std::exp (-juce::MathConstants<float>::twoPi * 250.0f / (float) sr);
    hissLp1.setLowpass (juce::jmin (3600.0, sr * 0.45), 0.54, sr);
    hissLp2.setLowpass (juce::jmin (3600.0, sr * 0.45), 1.31, sr);
    recLp1.setLowpass (juce::jmin (3600.0, sr * 0.45), 0.54, sr);
    recLp2.setLowpass (juce::jmin (3600.0, sr * 0.45), 1.31, sr);
    reset();
}

void DubEchoModule::reset()
{
    for (auto& c : ch)
    {
        std::fill (c.line.begin(), c.line.end(), 0.0f);
        c.lp1.clear();
        c.lp2.clear();
        c.top = c.dc = c.fbState = 0.0f;
    }
    writePos = 0;
    smoothDelay = 0.0;
    wowA = wowB = wowTargetA = wowTargetB = 0.0;
    wowCountA = wowCountB = 0;
    flutterState = flutterState2 = 0.0f;
    noiseLp1 = noiseLp2 = 0.0f;
    hissX = hissHp = recX = recHp = 0.0f;
    hissLp1.clear();
    hissLp2.clear();
    recLp1.clear();
    recLp2.clear();
}

float DubEchoModule::readLine (const std::vector<float>& line, double delaySamples) const noexcept
{
    // 4-point Hermite: the wow keeps the read point moving, so plain linear
    // interpolation would dull the repeats
    const int size = (int) line.size();
    double rp = (double) writePos - delaySamples;
    while (rp < 0.0)
        rp += size;
    const int i1 = (int) rp % size;
    const int i0 = (i1 - 1 + size) % size, i2 = (i1 + 1) % size, i3 = (i1 + 2) % size;
    const float f = (float) (rp - std::floor (rp));
    const float y0 = line[(size_t) i0], y1 = line[(size_t) i1], y2 = line[(size_t) i2], y3 = line[(size_t) i3];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

void DubEchoModule::updateTone (float toneKnob)
{
    // Tone 0..100 % is the original's 3..7: the corner moves ~2.2x per step
    const double disp = 3.0 + 4.0 * juce::jlimit (0.0, 1.0, (double) toneKnob * 0.01);
    const double fc = 219.0 * std::pow (2.0, (disp - 3.0) * 1.128);
    for (auto& c : ch)
    {
        c.lp1.setLowpass (fc, kToneQ1, sampleRate);
        c.lp2.setLowpass (fc, kToneQ2, sampleRate);
    }
}

void DubEchoModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (ch[0].line.empty())
    {
        outputs[0] = inputs[0];
        return;
    }

    const double sr = sampleRate;

    if (const float tone = param (pTone); tone != lastTone)
    {
        updateTone (tone);
        lastTone = tone;
    }

    // ---- the tape speed: the main head's time, glided, plus wow and flutter
    double seconds = param (pTime) * 0.001;
    if (param (pSync) > 0.5f)
        seconds = dubBeats ((int) std::lround (param (pDivision))) * 60.0 / juce::jmax (20.0, tempoBpm);
    const double ratio2 = head2Ratio ((int) std::lround (param (pHead2)));
    const double longest = juce::jmax (1.0, ratio2);
    seconds = juce::jlimit (0.002, (kMaxSeconds - 0.08) / longest, seconds);
    const double target = seconds * sr;
    if (smoothDelay <= 0.0)
        smoothDelay = target;
    smoothDelay += smCoef * (target - smoothDelay);

    // wow: two smoothed random walks, a slow one and a quicker one (the
    // measured drift has most of its energy below 0.3 Hz, falling above)
    if (--wowCountA <= 0)
    {
        wowTargetA = rng.nextDouble() * 2.0 - 1.0;
        wowCountA = (int) (sr * (1.6 + 1.8 * rng.nextDouble()));
    }
    if (--wowCountB <= 0)
    {
        wowTargetB = rng.nextDouble() * 2.0 - 1.0;
        wowCountB = (int) (sr * (0.35 + 0.5 * rng.nextDouble()));
    }
    wowA += (wowTargetA - wowA) * (1.0 / (1.1 * sr));
    wowB += (wowTargetB - wowB) * (1.0 / (0.28 * sr));

    // flutter: band-limited noise, roughly 4..15 Hz
    const float white = rng.nextFloat() * 2.0f - 1.0f;
    const float fc1 = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 15.0 / sr));
    const float fc2 = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 4.0 / sr));
    flutterState += fc1 * (white - flutterState);
    flutterState2 += fc2 * (flutterState - flutterState2);
    const float flutter = (flutterState - flutterState2) * 9.0f;

    const double wowAmt = param (pWow) * 0.01, flutterAmt = param (pFlutter) * 0.01;
    // in seconds of delay: Wow at 20 % gives the original's ~0.7 cent drift
    const double wowSec = wowAmt * wowAmt * 0.012 * (wowA + 0.45 * wowB) + wowAmt * 0.0006 * wowA;
    const double flutterSec = flutterAmt * flutterAmt * 0.00022 * flutter;
    const double modSamples = (wowSec + flutterSec) * sr;

    // stereo: the right channel's heads a little further along, its wow mirrored
    const double width = param (pWidth) * 0.01;
    const double delayL = juce::jmax (4.0, smoothDelay + modSamples);
    const double delayR = juce::jmax (4.0, smoothDelay * (1.0 + 0.031 * width) + modSamples * (1.0 - 2.0 * width));

    // ---- Echo Noise: noise recorded onto the tape with the input - the machine's
    // hiss and a quiet rumble - so it repeats, darkens and saturates with every
    // pass and builds up with Feedback, as an old echo's noise does
    const float eAmt = param (pEchoNoise) * 0.01f;
    float noise = 0.0f;
    if (eAmt > 0.0f)
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;
        const float nc = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 260.0 / sr));
        noiseLp1 += nc * (w - noiseLp1);
        noiseLp2 += nc * (noiseLp1 - noiseLp2);
        const float w2 = rng.nextFloat() * 2.0f - 1.0f;
        recHp = hissHpCoef * (recHp + w2 - recX);
        recX = w2;
        const float band = recLp2.process (recLp1.process (recHp));
        const float levelDb = -84.0f + 54.0f * std::pow (eAmt, 0.6f);   // -84 dBFS at a trace, -30 flat out
        noise = band * juce::Decibels::decibelsToGain (levelDb) * 4.6f
              + eAmt * eAmt * 0.012f * noiseLp2;                         // (a quiet rumble: the RE-201 has little below 250 Hz)
    }
    const float nAmt = param (pNoise) * 0.01f;

    // ---- the playback hiss, on the heads (one machine: the same in both channels)
    float hiss = 0.0f;
    if (nAmt > 0.0f)
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;
        hissHp = hissHpCoef * (hissHp + w - hissX);
        hissX = w;
        const float band = hissLp2.process (hissLp1.process (hissHp));
        // -90 dBFS at a trace, about -72 at the default, -40 flat out (RMS, at the heads)
        const float levelDb = -90.0f + 50.0f * std::pow (nAmt, 0.6f);
        hiss = band * juce::Decibels::decibelsToGain (levelDb) * 4.6f;   // (the band's RMS is ~1 / 4.6)
    }

    const float drive = param (pDrive) * 0.1f;               // 0..10, the original's 0..50 / 5
    const float fb = param (pFeedback) * 0.01f;               // 0..1.2
    const float head2Level = ratio2 > 0.0 ? param (pHead2Level) * 0.01f : 0.0f;
    const float loopNorm = 1.0f / (1.0f + 0.5f * head2Level);

    float wet[2];
    const double delays[2] = { delayL, delayR };
    for (int c = 0; c < 2; ++c)
    {
        auto& s = ch[c];

        // the heads
        const float h1 = readLine (s.line, delays[c]);
        const float h2 = head2Level > 0.0f ? readLine (s.line, juce::jmax (4.0, delays[c] * ratio2)) : 0.0f;
        const float heads = h1 + head2Level * h2 + hiss;
        wet[c] = heads;

        // record: the input and the loop, through the tone filter and the saturation
        float x = drive * inputs[0][c] + fb * loopNorm * heads + noise;
        const float lp = s.lp2.process (s.lp1.process (x));
        x = kToneFloor * x + (1.0f - kToneFloor) * lp;
        s.top += topCoef * (x - s.top);
        x = tapeSat (s.top);
        s.dc += dcCoef * (x - s.dc);
        x -= s.dc;
        s.line[(size_t) writePos] = x;
    }
    writePos = (writePos + 1) % (int) ch[0].line.size();

    // equal-power Mix; the tape plays about 6 dB under the original's level
    const float mix = juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f);
    const float theta = mix * juce::MathConstants<float>::halfPi;
    const float dryG = std::cos (theta), wetG = 0.5f * std::sin (theta);
    outputs[0][0] = inputs[0][0] * dryG + wet[0] * wetG;
    outputs[0][1] = inputs[0][1] * dryG + wet[1] * wetG;
}

static ModuleDescriptor dubEchoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.dubecho";
    d.displayName = "Dub Echo";
    d.description =
        "A two-head tape echo with the lush, worn repeats of the old dub boxes: Drive pushes the tape into a soft "
        "saturation, Tone darkens every repeat a little more, and Feedback above 100 % lets the loop sing on its own. "
        "Head 2 adds a second playback head at a musical ratio of Time; Wow and Flutter wobble the tape, and turning "
        "Time glides the pitch of the repeats, as a tape does. Noise is the machine's playback hiss on top (shaped "
        "after a classic three-head tape echo's); Echo Noise records noise onto the tape with the sound, so it repeats, darkens and "
        "builds up with Feedback.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 3;
    d.listedAfter = "fx.echo";
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out"),
        tempoIn()   // (6.9.10: tempo per lane)
    };
    d.params = {
        makeRotary ("time",      "Time",      40.0f, 2000.0f, 352.0f, 0, "ms", true),
        makeCombo  ("sync",      "Sync",      { "Free", "Tempo" }, 0, 0, 2).noMod(),
        makeCombo  ("division",  "Division",  dubDivisions(), 5, 0, 2).visibleWhen ("sync", 1.0f),
        makeRotary ("feedback",  "Feedback",  0.0f, 120.0f, 64.0f, 1, "%"),
        makeRotary ("tone",      "Tone",      0.0f, 100.0f, 60.0f, 1, "%"),
        makeRotary ("drive",     "Drive",     0.0f, 100.0f, 39.0f, 1, "%"),
        makeCombo  ("head2",     "Head 2",    head2Choices(), 0, 2, 2),
        makeRotary ("head2Level", "Head 2 Lvl", 0.0f, 100.0f, 70.0f, 2, "%"),
        makeRotary ("wow",       "Wow",       0.0f, 100.0f, 20.0f, 3, "%"),
        makeRotary ("flutter",   "Flutter",   0.0f, 100.0f, 12.0f, 3, "%"),
        makeRotary ("noise",     "Noise",     0.0f, 100.0f, 19.0f, 3, "%"),
        makeRotary ("width",     "Width",     0.0f, 100.0f, 30.0f, 4, "%"),
        makeRotary ("mix",       "Mix",       0.0f, 100.0f, 30.0f, 4, "%"),
        makeRotary ("echoNoise", "Echo Noise", 0.0f, 100.0f, 30.0f, 3, "%")
    };
    d.rowSections = { { 0, "Tape" }, { 2, "Heads" }, { 3, "Wear" } };
    return d;
}

AQUANODE_REGISTER_MODULE (DubEchoModule, dubEchoDescriptor)
