//==============================================================================
// Stutter (6.7.11) - catches a slice and repeats it while engaged.
//
// When Stutter goes on (above 50 %), the next Size of sound (a note length in
// time with the tempo, or milliseconds with Sync on Free) plays as it comes
// and is caught; from then on that slice repeats until Stutter goes off.
// Gate: how much of each repeat sounds. Decay: each repeat a little quieter.
// Ramp: each repeat played a little faster (up) or slower (down), in
// semitones per repeat. Smooth: the fades at the slice's edges and on the
// way in and out (no clicks). Mix: the stutter against the sound as it is;
// Level: the whole output (an Automation Lane can open and close it).
// Global. Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class StutterModule final : public SynthModule
{
public:
    enum P { pStutter = 0, pSize, pSync, pSizeMs, pGate, pDecay, pRamp, pSmooth, pMix, pLevel };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const int need = juce::nextPowerOfTwo ((int) (sr * 8.5));
        for (auto& b : buf) b.assign ((size_t) need, 0.0f);
        mask = need - 1;
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0; engaged = false; engageEnv = 0.0f; capturing = false;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const float inL = inputs[0][0], inR = inputs[0][1];
        buf[0][(size_t) (writePos & mask)] = inL;
        buf[1][(size_t) (writePos & mask)] = inR;

        const bool on = param (pStutter) >= 50.0f;
        if (on && ! engaged)
        {
            // a new slice: caught from here, while it plays as it comes
            engaged = true; capturing = true;
            sliceStart = writePos;
            sliceLen = sliceSamples();
            readPos = 0.0; rate = 1.0; gain = 1.0f; repeats = 0;
        }
        else if (! on && engaged) engaged = false;

        const float smoothS = juce::jmax (8.0f, param (pSmooth) * 0.001f * (float) sampleRate);
        engageEnv += ((engaged ? 1.0f : 0.0f) - engageEnv) * (1.0f / smoothS);

        float wl = inL, wr = inR;
        if (engageEnv > 1.0e-4f)
        {
            if (capturing)
            {
                if (writePos - sliceStart >= sliceLen) capturing = false;   // (caught: the repeats begin)
                else { wl = inL; wr = inR; }
            }
            if (! capturing)
            {
                const double len = (double) sliceLen;
                const double gateLen = len * juce::jlimit (0.05, 1.0, (double) param (pGate) * 0.01);
                float env = 1.0f;
                const double fadeS = juce::jmin ((double) smoothS, gateLen * 0.5);
                if (readPos < fadeS) env = (float) (readPos / fadeS);
                else if (readPos > gateLen - fadeS) env = (float) juce::jmax (0.0, (gateLen - readPos) / fadeS);
                if (readPos >= gateLen) env = 0.0f;
                const double p = (double) sliceStart + readPos;
                const juce::int64 i0 = (juce::int64) std::floor (p);
                const float f = (float) (p - (double) i0);
                auto at = [this] (int c, juce::int64 i) { return buf[(size_t) c][(size_t) (i & mask)]; };
                wl = (at (0, i0) + (at (0, i0 + 1) - at (0, i0)) * f) * env * gain;
                wr = (at (1, i0) + (at (1, i0 + 1) - at (1, i0)) * f) * env * gain;
                readPos += rate;
                if (readPos >= len)
                {
                    readPos -= len;
                    ++repeats;
                    gain *= 1.0f - param (pDecay) * 0.01f * 0.35f;
                    rate = juce::jlimit (0.25, 4.0, rate * std::pow (2.0, (double) param (pRamp) / 12.0));
                }
            }
        }
        const float e = engageEnv;
        const float sl = inL * (1.0f - e) + wl * e, sr = inR * (1.0f - e) + wr * e;
        const float mix = param (pMix) * 0.01f, level = param (pLevel);
        outputs[0] = { (inL * (1.0f - mix) + sl * mix) * level, (inR * (1.0f - mix) + sr * mix) * level };
        ++writePos;
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.stutter";
        d.displayName = "Stutter";
        d.searchKeywords = "stutter repeat glitch buffer slice roll beat";
        d.description =
            "Catches a slice and repeats it while engaged: when Stutter goes on (above 50 %), the next Size of sound "
            "(a note length in time, or milliseconds with Sync on Free) plays and is caught, then repeats until Stutter "
            "goes off. Gate: how much of each repeat sounds; Decay: each repeat quieter; Ramp: each repeat faster or "
            "slower (semitones). Smooth: the fades. Mix, Level (for an Automation Lane to open and close).";
        d.section = ModuleSection::Effect;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"), tempoIn() };
        d.params = {
            makeRotary ("stutter", "Stutter", 0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f),
            makeSteppedList ("size", "Size", { "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }, 4, 0),
            makeCombo ("sync", "Sync", { "Tempo", "Free" }, 0, 0, 2).noMod(),
            makeRotary ("sizeMs", "Size (ms)", 5.0f, 1000.0f, 60.0f, 1, "ms", true).visibleWhen ("sync", 1.0f),
            makeRotary ("gate", "Gate", 5.0f, 100.0f, 100.0f, 1, "%", false, 1.0f),
            makeRotary ("decay", "Decay", 0.0f, 100.0f, 0.0f, 1, "%", false, 1.0f),
            makeRotary ("ramp", "Ramp", -12.0f, 12.0f, 0.0f, 1, "st", false, 0.1f),
            makeRotary ("smooth", "Smooth", 0.5f, 20.0f, 3.0f, 2, "ms", true),
            makeRotary ("mix", "Mix", 0.0f, 100.0f, 100.0f, 2, "%", false, 1.0f),
            makeRotary ("level", "Level", 0.0f, 1.0f, 1.0f, 2)
        };
        return d;
    }

private:
    std::vector<float> buf[2];
    juce::int64 mask { 0 }, writePos { 0 }, sliceStart { 0 }, sliceLen { 1 };
    double readPos { 0.0 }, rate { 1.0 };
    float gain { 1.0f }, engageEnv { 0.0f };
    int repeats { 0 };
    bool engaged { false }, capturing { false };

    juce::int64 sliceSamples() const
    {
        static constexpr double beats[7] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
        double s;
        if (param (pSync) > 0.5f) s = param (pSizeMs) * 0.001 * sampleRate;
        else s = beats[juce::jlimit (0, 6, (int) std::lround (param (pSize)))] * 60.0 / juce::jmax (20.0, tempoBpm > 1.0 ? tempoBpm : 120.0) * sampleRate;
        return (juce::int64) juce::jlimit (16.0, sampleRate * 8.0, s);
    }
};

AQUANODE_REGISTER_MODULE (StutterModule, StutterModule::descriptor)
