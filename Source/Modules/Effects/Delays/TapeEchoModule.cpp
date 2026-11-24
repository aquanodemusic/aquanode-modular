// Aquanode Modular - Tape Echo: Dusk Audio's Tape Echo 2 as a module
//
// The sound is Dusk Audio's: the DSP core of their Tape Echo 2 (a component-modelled three-head
// tape echo with a spring reverb, GNU GPL v3.0 or later, in DuskTapeEcho/ - see the NOTICE there).
// This file only wraps it as an Aquanode effect: the knobs, tempo sync at the patch's tempo (the
// shell's own way: the note belongs to the first playing head and is held to the motor's range),
// and the samples in and out. No presets, no tape picture.
#include "../../Core/ModuleCore.h"
#include "DuskTapeEcho/TapeEchoDSP.hpp"

using namespace aquanode;

namespace
{
    const juce::StringArray& teModes()
    {
        static const juce::StringArray m { "Head 1", "Head 2", "Head 3", "Heads 2+3", "Head 1 + Rev", "Head 2 + Rev",
                                           "Head 3 + Rev", "Heads 1+2 + Rev", "Heads 2+3 + Rev", "Heads 1+3 + Rev",
                                           "Heads 1+2+3 + Rev", "Reverb Only" };
        return m;
    }
    // the sync notes, shortest first, as indices into Dusk's division table
    constexpr int kNoteOrder[] = { 10, 9, 11, 0, 1, 8, 2, 3, 4, 5, 13, 12, 6, 7, 15, 14 };
    constexpr int kNumNotes = (int) (sizeof (kNoteOrder) / sizeof (kNoteOrder[0]));
    const juce::StringArray& teNotes()
    {
        static const juce::StringArray n = []
        {
            juce::StringArray s;
            for (int i : kNoteOrder) s.add (kSyncDivisions[i].name);
            return s;
        }();
        return n;
    }
}

class TapeEchoModule final : public SynthModule
{
public:
    enum P { pMode = 0, pRate, pIntensity, pEcho, pReverb, pBass, pTreble, pInput, pWow, pAge, pNoise,
             pEchoPan, pRevPan, pMix, pOutput, pSync, pNote };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        pushParams();            // first: prepare snaps the motor and the levels to them
        dsp.prepare (sr, 64);
        dsp.reset();
        counter = 0;
    }
    void reset() override { dsp.reset(); counter = 0; }
    double voiceTailSeconds() const override { return 0.0; }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        if ((counter++ & 15) == 0) pushParams();
        float l = inputs[0][0], r = inputs[0][1];
        float* io[2] = { &l, &r };
        dsp.processBlock (io, io, 2, 1);
        outputs[0][0] = std::isfinite (l) ? l : 0.0f;
        outputs[0][1] = std::isfinite (r) ? r : 0.0f;
    }

private:
    void pushParams()
    {
        const int mode = juce::jlimit (1, 12, (int) std::lround (param (pMode)) + 1);
        dsp.setMode (mode);
        float rate = param (pRate) * 0.01f;
        if (std::lround (param (pSync)) == 1)
        {
            // the note belongs to the first playing head; held to the motor's range, as the shell does
            const int div = kNoteOrder[juce::jlimit (0, kNumNotes - 1, (int) std::lround (param (pNote)))];
            const double ms = syncDelayMs (juce::jmax (20.0, tempoBpm), div);
            const double head1 = (ms - duskaudio::TapeEchoDSP::leadingHeadOffsetMsForMode (mode))
                                 / duskaudio::TapeEchoDSP::leadingHeadRatioForMode (mode);
            const double clamped = juce::jlimit ((double) duskaudio::TapeEchoDSP::kMinDelayMs, (double) duskaudio::TapeEchoDSP::kMaxDelayMs, head1);
            rate = duskaudio::TapeEchoDSP::repeatRateForDelayMs ((float) clamped);
        }
        dsp.setRepeatRate (rate);
        dsp.setIntensity (param (pIntensity) * 0.01f);
        dsp.setEchoLevel (param (pEcho) * 0.01f);
        dsp.setReverbLevel (param (pReverb) * 0.01f);
        dsp.setBass (param (pBass) * 0.01f);
        dsp.setTreble (param (pTreble) * 0.01f);
        dsp.setInputGain (param (pInput) * 0.01f);
        dsp.setWowFlutter (param (pWow) * 0.01f);
        dsp.setTapeAge (0.5f * (float) juce::jlimit (0, 2, (int) std::lround (param (pAge))));
        dsp.setNoise (param (pNoise) * 0.01f);
        dsp.setEchoPan (0.5f + 0.005f * param (pEchoPan));
        dsp.setReverbPan (0.5f + 0.005f * param (pRevPan));
        dsp.setMix (param (pMix) * 0.01f);
        dsp.setOutputVolume ((param (pOutput) + 20.0f) / 40.0f);
    }

    duskaudio::TapeEchoDSP dsp;
    uint32_t counter { 0 };
};

static ModuleDescriptor tapeEchoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.tapeecho";
    d.displayName = "Tape Echo";
    d.description =
        "Dusk Audio's Tape Echo 2: a component-modelled three-head tape echo with a spring reverb. Heads picks the "
        "playback heads and the spring (12 ways); Repeat Rate is the tape's speed (head 1 about 177 ms slow to 69 ms "
        "fast, heads 2 and 3 further down the tape), and a change of speed glides the pitch as a motor does. "
        "Intensity feeds the heads back to the record head - past about 75 % it sings on its own. Echo and Reverb "
        "are the two returns, Bass and Treble shape the echoes only, Input drives the record amplifier and the tape. "
        "Wow & Flutter, the tape's Age (New, Used, Old: more wear, hiss and wobble) and Noise (its hum and hiss "
        "raised up to 60 dB). Sync holds a note at the patch's tempo for the first playing head, kept within the "
        "motor's range. DSP by Dusk Audio (GPLv3).";
    d.section = ModuleSection::Effect;
    d.listedAfter = "fx.dubecho";
    d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"), tempoIn() };
    d.params = {
        makeCombo  ("mode",      "Heads",        teModes(), 10, 0, 2).noMod(),
        makeRotary ("rate",      "Repeat Rate",  0.0f, 100.0f, 35.0f, 0, "%"),
        makeRotary ("intensity", "Intensity",    0.0f, 100.0f, 40.0f, 0, "%"),
        makeRotary ("echo",      "Echo",         0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("reverb",    "Reverb",       0.0f, 100.0f, 30.0f, 1, "%"),
        makeRotary ("bass",      "Bass",         -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("treble",    "Treble",       -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("input",     "Input",        0.0f, 100.0f, 50.0f, 2, "%"),
        makeRotary ("wow",       "Wow & Flutter", 0.0f, 100.0f, 0.0f, 2, "%"),
        makeCombo  ("age",       "Tape Age",     { "New", "Used", "Old" }, 1, 2, 2).noMod(),
        makeRotary ("noise",     "Noise",        0.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("echoPan",   "Echo Pan",     -100.0f, 100.0f, 0.0f, 3, "%"),
        makeRotary ("revPan",    "Reverb Pan",   -100.0f, 100.0f, 0.0f, 3, "%"),
        makeRotary ("mix",       "Mix",          0.0f, 100.0f, 50.0f, 3, "%"),
        makeRotary ("output",    "Output",       -20.0f, 20.0f, 0.0f, 3, "dB"),
        makeCombo  ("sync",      "Sync",         { "Free", "Tempo" }, 0, 4, 2).noMod(),
        makeCombo  ("note",      "Note",         teNotes(), 9, 4, 2).visibleWhen ("sync", 1.0f)
    };
    d.rowSections = { { 0, "Tape" }, { 1, "Returns" }, { 2, "Wear" }, { 3, "Out" }, { 4, "Sync" } };
    return d;
}

AQUANODE_REGISTER_MODULE (TapeEchoModule, tapeEchoDescriptor)
