//==============================================================================
// FM12 - Operator (6.9.17) - one of FM12's operators as a module of its own, for
// FM12's two-operator patch version and for any FM patch built by hand.
//
// FM In is a plain audio input: another operator's Audio Out makes this one its
// carrier, and anything else can go there too - an effect between two operators
// changes what reaches the carrier. Mode is FM12's engine, how FM In bends this
// operator:
//   Phase Modulation           FM In moves the phase (the DX7's way)
//   Linear FM                  FM In moves the frequency in Hz (FM Depth x 5 x
//                              the note's pitch at full swing; negative ones fold)
//   Linear FM Through Zero     ... and it may run backwards through zero
//   Exponential FM             FM In moves the pitch in octaves (up to 5)
//   Exponential FM Through Zero  sinh-shaped, through zero
// The swing is FM12's: a modulator at full Level moves its carrier 21 times as
// far in the first three modes, 4 times in the exponential ones (FM Depth scales
// it - FM Depth at the modulator's ratio makes Linear FM the plugin's own).
// Feedback is the operator's own last output back into it, per mode as FM12
// does it. Nyquist Limit folds the operator's frequency at a lower Nyquist, as
// FM12's (all the way up: off). Each voice has its ADSR; Vel Sens how much the
// note's velocity sets the level (FM12's carriers: all of it). A DC blocker in
// the frequency modes, as in FM12.
// (6.9.18) Only FM: the operator's own plain tone (no FM In, no feedback - the
// carrier as it would sound alone) taken away, inverted, so at 100 % only what
// the FM adds is left. Exact in Phase Modulation (the same phase underneath);
// in the frequency modes the modulated phase wanders off the plain one, so less
// of it cancels.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class FM12OperatorModule final : public SynthModule
{
public:
    enum ParamIndex { pMode = 0, pWave, pRatio, pFine, pLevel, pDepth, pFeedback, pVelSens,
                      pAttack, pDecay, pSustain, pRelease, pNyquist, pVoices, pGlide, pOnlyFm };
    enum InputIndex { inFm = 0 };
    static constexpr float kOutScale = 0.5f;   // a full-level operator's Audio Out (FM In reads it back)
    static constexpr int kCtrlBlock = 32;

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.fm12op";
        d.displayName = "FM12 - Operator";
        d.listedWithPlugin = "fm12";
        d.searchKeywords = "fm operator phase linear exponential through zero nyquist fold carrier modulator";
        d.description = "One of FM12's operators as a module: patch one's Audio Out into another's FM In to make it the "
                        "modulator - FM In is a plain audio input, so effects can go between them, and any number of "
                        "operators can be stacked. Mode is FM12's engine: Phase Modulation, Linear FM, Linear FM Through "
                        "Zero, Exponential FM or Exponential FM Through Zero. Ratio and Fine set the pitch against the "
                        "note, Level the output, FM Depth how far FM In moves it, Feedback its own output back into it, "
                        "Nyquist Limit folds its frequency at a lower Nyquist (all the way up: off), as FM12 does. Its "
                        "ADSR is built in; Vel Sens how much velocity sets the level. Only FM takes the operator's own "
                        "plain tone away (an inverted copy of it), leaving what the FM adds.";
        d.section = ModuleSection::Oscillator;
        d.sidebarOrder = 21;
        d.sockets = { audioIn ("fmIn", "FM In"), midiIn ("addMidiIn", "Add MIDI In"), audioOut ("audioOut", "Audio Out") };
        auto nyquist = makeRotary ("nyquist", "Nyquist", 100.0f, 24000.0f, 24000.0f, 3, "Hz", true);   // (FM12's Nyquist Limit)
        nyquist.textFromValue = [] (double v) { return v >= 23999.0 ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; };
        d.params = {
            makeCombo ("mode", "Mode", { "Phase Modulation", "Linear FM", "Linear FM Through Zero",
                                         "Exponential FM", "Exponential FM Through Zero" }, 0, 0, 3).noMod(),
            makeCombo ("wave", "Wave", { "Sine", "Tri", "Saw", "Square" }, 0, 0, 2).noMod(),
            makeRotary ("ratio", "Ratio", 0.0f, 20.0f, 1.0f, 1),
            makeRotary ("fine", "Fine", -100.0f, 100.0f, 0.0f, 1, "ct"),
            makeRotary ("level", "Level", 0.0001f, 1.0f, 1.0f, 1, {}, true),
            makeRotary ("depth", "FM Depth", 0.0f, 20.0f, 1.0f, 1),
            makeRotary ("feedback", "Feedback", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("velSens", "Vel Sens", 0.0f, 1.0f, 1.0f, 1),
            makeRotary ("attack", "Attack", 0.001f, 10.0f, 0.05f, 2, "s", true),
            makeRotary ("decay", "Decay", 0.001f, 30.0f, 0.1f, 2, "s", true),
            makeRotary ("sustain", "Sustain", 0.0f, 1.0f, 0.8f, 2),
            makeRotary ("release", "Release", 0.001f, 30.0f, 0.5f, 2, "s", true),
            nyquist,
            makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod(),
            makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f),
            makeRotary ("onlyFm", "Only FM", 0.0f, 100.0f, 0.0f, 3, "%", false, 1.0f)   // (6.9.18)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (auto& e : env) e.setSampleRate (sr);
        reset();
    }
    void reset() override { for (int v = 0; v < kMaxVoices; ++v) voiceReset (v); }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        glide.resetVoice (v);
        env[v].reset();
        phase[v] = plainPhase[v] = 0.0;
        last[v] = 0.0f;
        dcX[v] = dcY[v] = 0.0f;
        ctrlLeft[v] = 0;
        velocity[v] = 1.0f;
    }
    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        if (! retrigger)
        {
            phase[v] = plainPhase[v] = 0.0;   // (FM12's voices start each operator at phase 0)
            last[v] = 0.0f;
            dcX[v] = dcY[v] = 0.0f;
        }
        ctrlLeft[v] = 0;
        updateEnvelope (v);
        env[v].noteOn();
    }
    void voiceNoteOff (int v) override
    {
        pool.noteOff (v, voiceLimit());
        env[v].noteOff();
    }
    void voiceVelocity (int v, float velocity01) override { velocity[v] = juce::jlimit (0.0f, 1.0f, velocity01); }
    double voiceTailSeconds() const override { return juce::jlimit (0.05, 30.0, (double) param (pRelease) + 0.05); }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const float voiceGain = pool.nextGain (v, sampleRate);
        if (pool.isSilent (v)) { outputs[0] = { 0.0f, 0.0f }; return; }
        if (--ctrlLeft[v] < 0)
        {
            updateEnvelope (v);
            ctrlLeft[v] = kCtrlBlock - 1;
        }

        const int mode = juce::jlimit (0, 4, (int) std::lround (param (pMode)));
        const float semis = glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate) + pitchBendSemitones();
        const float noteHz = (float) noteMemo[v] ((double) semis, [] (double n) { return midiNoteToHz (n); });
        const float baseFreq = noteHz * param (pRatio) * fineMemo[v] (param (pFine) / 1200.0f, [] (float x) { return std::exp2 (x); });
        const float nyquist = param (pNyquist);
        const bool folding = nyquist < 23999.0f;
        const float invSr = 1.0f / (float) sampleRate;

        // FM In: the modulator's raw x envelope x level, swung as FM12 swings it
        const float in = 0.5f * (inputs[inFm][0] + inputs[inFm][1]) / kOutScale;
        const float m = in * (mode <= 2 ? 21.0f : 4.0f) * param (pDepth);
        float fm = mode == 0 ? m : mode <= 2 ? m * noteHz * 5.0f : juce::jlimit (-5.0f, 5.0f, m * 2.0f);
        if (const float fb = param (pFeedback); fb > 0.001f)
        {
            const float fbSig = last[v] * fb;
            if (mode == 0)      fm += fbSig * 0.5f;
            else if (mode <= 2) fm += fbSig * baseFreq * 2.5f;
            else                fm = juce::jlimit (-5.0f, 5.0f, fm + fbSig);
        }

        float raw, plain;
        const float plainAdv = folding ? fold (baseFreq, nyquist) : std::abs (baseFreq);
        if (mode == 0)
        {
            phase[v] += (double) (plainAdv * invSr);
            phase[v] -= std::floor (phase[v]);
            raw = wave ((float) phase[v] + fm);
            plain = wave ((float) phase[v]);
        }
        else
        {
            float f = baseFreq;
            switch (mode)
            {
                case 1:  f = baseFreq + fm; f = folding ? fold (f, nyquist) : std::abs (f); break;
                case 2:  f = baseFreq + fm; if (folding) f = juce::jlimit (-nyquist, nyquist, f); break;
                case 3:  f = baseFreq * std::exp2 (fm); if (folding) f = fold (f, nyquist); break;
                default: f = baseFreq + baseFreq * std::sinh (fm); if (folding) f = juce::jlimit (-nyquist, nyquist, f); break;
            }
            phase[v] += (double) (f * invSr);
            phase[v] -= std::floor (phase[v]);
            raw = wave ((float) phase[v]);
            plainPhase[v] += (double) (plainAdv * invSr);
            plainPhase[v] -= std::floor (plainPhase[v]);
            plain = wave ((float) plainPhase[v]);
        }
        last[v] = raw;

        const float vs = param (pVelSens);
        const float onlyFm = param (pOnlyFm) * 0.01f;   // (6.9.18) the plain tone, inverted, added
        float s = (raw - onlyFm * plain) * env[v].getNextSample() * param (pLevel) * (1.0f - vs + vs * velocity[v]);
        if (mode != 0)   // (FM12's DC blocker)
        {
            const float y = s - dcX[v] + 0.9999f * dcY[v];
            dcX[v] = s;
            dcY[v] = y;
            s = y;
        }
        const float out = s * kOutScale * voiceGain;
        outputs[0] = { out, out };
    }

private:
    // FM12's fold: a frequency mirrored into 0..nyquist at a virtual rate of twice it
    static float fold (float f, float nyquist)
    {
        if (nyquist <= 1.0f) return 0.0f;
        const float vsr = nyquist * 2.0f;
        return std::abs (f - vsr * std::round (f / vsr));
    }

    float wave (float ph) const
    {
        ph -= std::floor (ph);
        switch ((int) std::lround (param (pWave)))
        {
            case 1:  return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            case 2:  return 2.0f * ph - 1.0f;
            case 3:  return ph < 0.5f ? 1.0f : -1.0f;
            default: return std::sin (juce::MathConstants<float>::twoPi * ph);
        }
    }

    void updateEnvelope (int v)
    {
        juce::ADSR::Parameters p;
        p.attack = param (pAttack);
        p.decay = param (pDecay);
        p.sustain = juce::jlimit (0.0f, 1.0f, param (pSustain));
        p.release = param (pRelease);
        env[v].setParameters (p);
    }

    ModuleVoicePool pool;
    ModuleGlide glide;
    juce::ADSR env[kMaxVoices];
    double phase[kMaxVoices] {}, plainPhase[kMaxVoices] {};
    float last[kMaxVoices] {}, dcX[kMaxVoices] {}, dcY[kMaxVoices] {}, velocity[kMaxVoices] {};
    int ctrlLeft[kMaxVoices] {};
    MemoD noteMemo[kMaxVoices];
    Memo fineMemo[kMaxVoices];
};

AQUANODE_REGISTER_MODULE (FM12OperatorModule, FM12OperatorModule::descriptor)
