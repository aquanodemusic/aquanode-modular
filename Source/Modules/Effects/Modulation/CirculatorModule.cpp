#include "CirculatorModule.h"

using namespace aquanode;

namespace
{
    // a ceiling: transparent below 0.99, never past 1 - catches the overs a
    // bank of ringing stages can throw when its centre jumps
    inline float softLimit (float y) noexcept
    {
        constexpr float t = 0.99f;
        const float a = std::abs (y);
        if (a <= t)
            return y;
        return std::copysign (t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)), y);
    }
}

void CirculatorModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    smCoef = 1.0f - std::exp (-1.0f / (0.025f * (float) sr / (float) kCtrl));   // 25 ms on the centre
    reset();
}

void CirculatorModule::reset()
{
    for (auto& chs : st)
        for (auto& s : chs)
            s = Stage();
    lastOut[0] = lastOut[1] = 0.0f;
    ctrlCount = 0;
    firstUpdate = true;
    heldCount = 0;
}

void CirculatorModule::rawMidiMessage (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();
        int w = 0;
        for (int i = 0; i < heldCount; ++i)
            if (held[i] != n) held[w++] = held[i];
        heldCount = w;
        if (heldCount < 16)
            held[heldCount++] = n;
        keyNote = n;
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        int w = 0;
        for (int i = 0; i < heldCount; ++i)
            if (held[i] != n) held[w++] = held[i];
        heldCount = w;
        if (heldCount > 0)
            keyNote = held[heldCount - 1];   // back to the newest key still down
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        heldCount = 0;
}

void CirculatorModule::updateCoefficients()
{
    const double sr = sampleRate;
    const int mode = (int) std::lround (param (pMode));
    double hz;
    if (mode == 0)
        hz = param (pFreq);
    else
    {
        const double note = (mode == 1 ? (double) param (pNote) : (double) keyNote) + param (pFine);
        hz = midiNoteToHz (note);
    }
    const double maxHz = juce::jmin (18000.0, sr * 0.45);
    hz = juce::jlimit (20.0, maxHz, hz);

    // the centre glides in octaves (no zipper when it is turned or switched)
    if (firstUpdate)
        smoothedHz = hz;
    else
        smoothedHz *= std::pow (hz / smoothedHz, (double) smCoef);

    const float focus = param (pFocus) * 0.01f;
    const float q = 0.5f + 9.5f * focus * focus * focus;   // 0.5 .. 10, the ring tightening fast at the top
    const float k = 1.0f / q;

    const int stages = juce::jlimit (0, kMaxStages, (int) std::lround (param (pDepth)));
    if (stages > activeStages)
        for (auto& chs : st)
            for (int i = activeStages; i < stages; ++i)
                chs[i] = Stage();   // stages coming in start from silence
    activeStages = stages;

    const double spreadOct = param (pSpread) * 0.01 * 0.5;
    const double staggerOct = param (pStagger) * 0.01 * 2.0;

    for (int c = 0; c < 2; ++c)
    {
        const double centre = smoothedHz * std::pow (2.0, c == 1 ? spreadOct : 0.0);
        const bool staggered = staggerOct > 1.0e-4 && stages > 1;
        Coef shared;
        for (int i = 0; i < stages; ++i)
        {
            Coef t;
            if (! staggered && i > 0)
                t = shared;
            else
            {
                double f = centre;
                if (staggered)
                    f *= std::pow (2.0, staggerOct * ((double) i / (double) (stages - 1) - 0.5));
                f = juce::jlimit (20.0, maxHz, f);
                t.g = (float) std::tan (juce::MathConstants<double>::pi * f / sr);
                t.k = k;
                t.d = 1.0f / (1.0f + t.g * (t.g + k));
                shared = t;
            }
            target[c][i] = t;
            if (firstUpdate)
            {
                cur[c][i] = t;
                step[c][i] = { 0.0f, 0.0f, 0.0f };
            }
            else
            {
                const float inv = 1.0f / (float) kCtrl;
                step[c][i] = { (t.g - cur[c][i].g) * inv, (t.k - cur[c][i].k) * inv, (t.d - cur[c][i].d) * inv };
            }
        }
    }
    firstUpdate = false;
}

void CirculatorModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (ctrlCount <= 0)
    {
        updateCoefficients();
        ctrlCount = kCtrl;
    }
    --ctrlCount;

    float fb = param (pFeedback) * 0.01f;
    if (std::abs (fb) < 0.02f || activeStages == 0)
        fb = 0.0f;
    fb = juce::jlimit (-0.99f, 0.99f, fb);
    const float comp = std::sqrt (1.0f - std::abs (fb) / 1.5f);
    const float mix = juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f);

    for (int c = 0; c < 2; ++c)
    {
        const float in = inputs[0][c];
        float x = (in + fb * softLimit (lastOut[c])) * comp;

        auto* s = st[c];
        auto* cf = cur[c];
        const auto* sp = step[c];
        for (int i = 0; i < activeStages; ++i)
        {
            auto& co = cf[i];
            co.g += sp[i].g;
            co.k += sp[i].k;
            co.d += sp[i].d;
            // a TPT state-variable stage; its all-pass output is x - 2k * band
            const float g = co.g;
            const float hp = (x - (co.k + g) * s[i].s1 - s[i].s2) * co.d;
            const float bp = g * hp + s[i].s1;
            const float lp = g * bp + s[i].s2;
            s[i].s1 = g * hp + bp;
            s[i].s2 = g * bp + lp;
            x = x - 2.0f * co.k * bp;
        }

        x = softLimit (x);
        lastOut[c] = x;
        outputs[0][c] = in + mix * (x - in);
    }
}

static ModuleDescriptor circulatorDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.circulator";
    d.displayName = "Circulator";
    d.description =
        "Phase dispersion: up to 64 all-pass stages twist the phase around one centre, smearing every transient "
        "into a hollow, metallic chirp that rings at that pitch - drums and plucks turn springy and laser-like. "
        "Focus narrows the smear into a ringing tone, Feedback turns it into a steep phaser. The centre is Freq, "
        "a Note, or Key: the last note on its Midi In, so the chirp follows a bass line.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 22;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        midiIn ("midiIn", "Midi In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("mode",     "Centre",   { "Freq", "Note", "Key" }, 0, 0, 2),
        makeRotary ("freq",     "Freq",     20.0f, 18000.0f, 600.0f, 0, "Hz", true).visibleWhen ("mode", 0.0f),
        makeSteppedList ("note", "Note",    midiNoteNameChoices(), 63, 0).visibleWhen ("mode", 1.0f),
        makeRotary ("fine",     "Fine",     -12.0f, 12.0f, 0.0f, 0, "st"),
        makeRotary ("focus",    "Focus",    0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("depth",    "Depth",    0.0f, (float) CirculatorModule::kMaxStages, 32.0f, 1, {}, false, 1.0f),
        makeRotary ("feedback", "Feedback", -99.0f, 99.0f, 0.0f, 1, "%"),
        makeRotary ("spread",   "Spread",   0.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("stagger",  "Stagger",  0.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("mix",      "Mix",      0.0f, 100.0f, 100.0f, 2, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CirculatorModule, circulatorDescriptor)
