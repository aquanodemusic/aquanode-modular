//==============================================================================
// AliasSynth - Patch version
//
// AliasSynth is one voice played sixteen times: an oscillator with no
// anti-aliasing at all, its envelope and its volume. The patch version is
//
//   ADSR (the plugin's envelope, in ms) --> Env In
//                                           AliasSynth - Oscillator --> out
//
// "AliasSynth - Oscillator" is AliasSynth's own voice, sample for sample: the
// subtractive saw (one folded fundamental), the additive saw (64 harmonics,
// each folded on its own), the sine or the loaded sample (rooted at C0, C5 or
// C10, read without interpolation), folded around Nyquist Limit - or, with S&H,
// held at that rate instead - with the sine FM modulator before the folding,
// the wavefolder, the bit reduction, transpose with its glide and velocity.
// Every knob takes cables. Its sample is kept with the patch.
//
// Where the patch differs: AliasSynth's ADSR runs in seconds and its own
// curve (juce::ADSR); the patch uses the ADSR module (linear, in ms).
//==============================================================================
#include "PluginAliasSynthConfig.h"

namespace aqp_aliassynth
{
    #include AQP_ALIASSYNTH_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "aliassynth" };
    using AS = aqp_aliassynth::AliasSynthAudioProcessor;

    std::shared_ptr<const juce::AudioBuffer<float>> sampleOf (juce::AudioProcessor& p, double& rate)
    {
        auto* a = dynamic_cast<AS*> (&p);
        return a != nullptr ? a->getSample (rate) : nullptr;
    }

    void setSampleOf (juce::AudioProcessor& p, std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
    {
        if (auto* a = dynamic_cast<AS*> (&p); a != nullptr && audio != nullptr)
            a->setSample (std::move (audio), rate);
    }

    // the GUI card keeps its sample in the patch too
    const bool audioRegistered = registerAudioAccess (kPlugin, { sampleOf, setSampleOf });

    inline float foldFrequency (float fIn, float nyquist)
    {
        if (nyquist >= 22000.0f) return fIn;
        if (nyquist <= 1.0f) return 0.0f;
        const float virtualSR = nyquist * 2.0f;
        return std::abs (fIn - virtualSR * std::round (fIn / virtualSR));
    }
}

//==============================================================================
// AliasSynth - Oscillator
//==============================================================================
class AliasSynthOscModule final : public SynthModule
{
public:
    enum P { pMode, pLoad, pFmAmount, pFmRatio, pNyquist, pSrMode, pFold, pBitDepth, pTranspose, pGlide, pVolume };   // the order of d.params

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.aliassynth.osc";
        d.displayName = kit::moduleName (kPlugin, "Oscillator");
        d.description = "AliasSynth's voice with no anti-aliasing: a saw (one folded fundamental, or 64 harmonics "
                        "folded each on its own), a sine or a loaded sample (rooted at C0, C5 or C10), folded around "
                        "Nyquist Limit - or held at that rate (S&H) - after a sine FM modulator; then the wavefolder "
                        "and the bit reduction. Transpose moves it by octaves (gliding over Glide). Env In wants the "
                        "ADSR; with nothing there the note gates on and off. The sample is kept with the patch.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { modIn ("envIn", "Env In"), midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("mode", "Mode", { "Subtractive Saw", "Additive Saw (64)", "Sine Wave",
                                         "Sample (Root C0)", "Sample (Root C5)", "Sample (Root C10)" }, 2, 0, 3),
            makeButton ("loadSample", "Load Sample", 0, 2),

            makeRotary ("fmAmount", "FM Amt", 0.0f, 10.0f, 0.0f, 1),
            makeRotary ("fmRatio", "FM Ratio", 0.001f, 10.0f, 2.0f, 1, {}, true),
            makeRotary ("nyquistLimit", "Nyquist", 100.0f, 22050.0f, 22050.0f, 1, "Hz", true),
            makeCombo ("srMode", "S&H", { "Fold", "S&H" }, 0, 2, 2),
            makeCombo ("fold", "Wavefold", { "Off", "On" }, 0, 2, 2),
            makeRotary ("bitDepth", "Bits", 1.0f, 24.0f, 24.0f, 2),
            makeRotary ("transpose", "Transpose", -7.0f, 7.0f, 0.0f, 3, "oct"),
            makeRotary ("glideTime", "Glide", 1.0f, 10000.0f, 100.0f, 3, "ms", true),
            makeRotary ("masterVol", "Volume", 0.0f, 1.0f, 0.3f, 3)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    bool usesLoadedSample() const override { return true; }
    double maxSampleSeconds() const override { return 60.0; }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id == "loadSample")
            openSampleChooser();
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int v = 0; v < kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        auto& s = voices[(size_t) v];
        s = {};
        s.transpose.reset (sampleRate, 0.03);
    }

    void blockStart() override
    {
        latched = getLoadedSample (&latchedRate);
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        auto& s = voices[(size_t) v];
        s.note = note;
        s.gateOn = true;
        if (! retrigger)
        {
            s.modPhase = s.naivePhase = 0.0f;
            s.samplePosition = 0.0;
            s.heldSample = 0.0f;
            s.holdCounter = 0;
            s.harmonic.fill (0.0f);
            s.gate = 0.0f;
        }
        // the transpose glide starts on the knob, as the plugin's per note
        s.transpose.reset (sampleRate, juce::jmax (0.001, (double) param (pGlide) * 0.001));
        s.transpose.setCurrentAndTargetValue (param (pTranspose));
    }

    void voiceVelocity (int v, float velocity01) override { voices[(size_t) v].velocity = velocity01; }
    void voiceNoteOff (int v) override { voices[(size_t) v].gateOn = false; }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        auto& s = voices[(size_t) v];
        const int mode = juce::jlimit (0, 5, (int) param (pMode));
        const float fmAmount = param (pFmAmount);
        const float fmRatio = param (pFmRatio);
        const float nyquist = param (pNyquist);
        const bool srMode = param (pSrMode) > 0.5f;
        const bool fold = param (pFold) > 0.5f;
        const float bitDepth = param (pBitDepth);

        s.transpose.setTargetValue (param (pTranspose));
        const float transpose = s.transpose.getNextValue();
        const float noteFreq = (float) midiNoteToHz ((double) s.note + pitchBendSemitones());
        const float baseFreq = noteFreq * std::pow (2.0f, transpose);

        float out;
        if (srMode)
        {
            const int holdFactor = juce::jmax (1, (int) std::round (sampleRate / (nyquist * 2.0f)));
            if (s.holdCounter <= 0)
            {
                s.heldSample = generate (s, mode, baseFreq, fmAmount, fmRatio, 22050.0f);
                s.holdCounter = holdFactor;
            }
            out = s.heldSample;
            --s.holdCounter;
        }
        else
        {
            out = generate (s, mode, baseFreq, fmAmount, fmRatio, nyquist);
        }

        if (fold)
            out = std::sin (out * juce::MathConstants<float>::pi * 0.5f);
        if (bitDepth < 24.0f)
        {
            const float levels = std::pow (2.0f, bitDepth - 1.0f);
            out = std::round (out * levels) / levels;
        }

        float gain;
        if (isInputConnected (0))
        {
            gain = inputs[0][0];
        }
        else
        {
            // unpatched Env In: on / off with a 3 ms ramp
            const float step = (float) (1.0 / (0.003 * sampleRate));
            s.gate = s.gateOn ? juce::jmin (1.0f, s.gate + step) : juce::jmax (0.0f, s.gate - step);
            gain = s.gate;
        }

        out *= gain * s.velocity * param (pVolume);
        if (! std::isfinite (out))
            out = 0.0f;
        outputs[0] = { out, out };
    }

private:
    struct Voice
    {
        int note { 60 };
        bool gateOn { false };
        float gate { 0.0f }, velocity { 1.0f };
        float modPhase { 0.0f }, naivePhase { 0.0f }, heldSample { 0.0f };
        int holdCounter { 0 };
        double samplePosition { 0.0 };
        std::array<float, 64> harmonic {};
        juce::SmoothedValue<float> transpose;
    };

    // AliasVoice::generateRawSignal
    float generate (Voice& s, int mode, float freq, float fmAmt, float fmRatio, float nyLimit)
    {
        const float sr = (float) sampleRate;
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float modSignal = std::sin (s.modPhase * twoPi) * fmAmt;
        s.modPhase += (freq * fmRatio) / sr;
        if (s.modPhase >= 1.0f) s.modPhase -= 1.0f;

        const float modulatedFreq = freq + modSignal * freq;
        float sig = 0.0f;

        if (mode == 0)
        {
            s.naivePhase += foldFrequency (modulatedFreq, nyLimit) / sr;
            s.naivePhase -= std::floor (s.naivePhase);
            sig = 2.0f * s.naivePhase - 1.0f;
        }
        else if (mode == 1)
        {
            for (int k = 1; k <= 64; ++k)
            {
                auto& ph = s.harmonic[(size_t) k - 1];
                sig += (1.0f / (float) k) * std::sin (ph * twoPi);
                ph += foldFrequency (modulatedFreq * (float) k, nyLimit) / sr;
                ph -= std::floor (ph);
            }
            sig *= 0.5f;
        }
        else if (mode == 2)
        {
            auto& ph = s.harmonic[0];
            ph += foldFrequency (modulatedFreq, nyLimit) / sr;
            ph -= std::floor (ph);
            sig = std::sin (ph * twoPi);
        }
        else if (latched != nullptr && latched->getNumSamples() > 0)
        {
            const float root = mode == 3 ? 8.176f : (mode == 4 ? 261.63f : 8372.0f);
            const double speed = (foldFrequency (modulatedFreq, nyLimit) / root) * (latchedRate / sampleRate);
            const int pos = (int) s.samplePosition;
            if (pos >= 0 && pos < latched->getNumSamples())
            {
                sig = latched->getSample (0, pos);
                s.samplePosition += speed;
            }
        }
        return sig;
    }

    std::array<Voice, (size_t) kMaxVoices> voices;
    std::shared_ptr<const juce::AudioBuffer<float>> latched;
    double latchedRate { 44100.0 };
};

AQUANODE_REGISTER_MODULE (AliasSynthOscModule, AliasSynthOscModule::descriptor)

//==============================================================================
namespace aquanode::plugins::aliassynth
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int osc = b.add ("plugin.aliassynth.osc", 300, 0);
        const int env = b.add ("util.adsr", 0, 0);

        b.copy (osc, original);   // the ids are AliasSynth's own

        // AliasSynth's envelope runs in seconds; the ADSR in ms (1 ms at least)
        b.set (env, "attack",  juce::jmax (1.0f, kit::param (original, "attack", 0.01f) * 1000.0f));
        b.set (env, "decay",   juce::jmax (1.0f, kit::param (original, "decay", 0.2f) * 1000.0f));
        b.set (env, "sustain", kit::param (original, "sustain", 0.8f));
        b.set (env, "release", juce::jmax (1.0f, kit::param (original, "release", 0.4f) * 1000.0f));
        b.cable (env, "modOut", osc, "envIn");

        double rate = 44100.0;
        if (auto sample = sampleOf (original, rate))
            if (auto* m = b.dsp (osc))
                m->setLoadedSample (sample, rate);

        PatchPorts ports;
        ports.name = "AliasSynth";
        ports.audioOut = { { osc, "audioOut" } };
        ports.midiIn = { { osc, "midiIn" } };
        return ports;
    }

    // a "preset" is a sample to play
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* a = dynamic_cast<AS*> (&original);
        return a != nullptr && a->loadSampleFile (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
