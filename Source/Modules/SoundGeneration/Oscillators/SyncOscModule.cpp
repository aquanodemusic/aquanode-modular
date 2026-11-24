//==============================================================================
// Sync Osc (6.9.15) - hard sync in the way of the Dual VCO's oscillators: a
// master runs at the key's pitch and restarts a second, the slave, at every one
// of its cycles. The slave's own pitch above the master (Sync, in semitones)
// is what sweeps: the classic tearing lead comes from an envelope or LFO into
// Sync In. Saw, Square or Triangle, band-limited (each step - the slave's own
// and the one where the master restarts it - gets its correction), the master
// mixed in by Master. Env In shapes each voice, as on the other oscillators;
// with nothing there the voice plays while the key is held.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/AntiAlias.h"

using namespace aquanode;

namespace
{
    // the two-sided polyBLEP residual: t in samples from the step (-1..1), for a step of height 1
    inline float blepBefore (double t) { return (float) (t * t * 0.5 + t + 0.5); }      // t in (-1, 0]
    inline float blepAfter (double t)  { return (float) (t - t * t * 0.5 - 0.5); }      // t in [0, 1)

    inline float waveAt (int wave, double ph)
    {
        if (wave == 1) return ph < 0.5 ? 1.0f : -1.0f;                                 // square
        if (wave == 2) return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);    // triangle
        return (float) (2.0 * ph - 1.0);                                               // saw
    }
}

class SyncOscModule final : public SynthModule
{
public:
    enum P { pWave, pSync, pSyncMod, pFine, pMaster, pLevel, pVoices, pGlide };
    enum In { kSyncIn = 0, kPitchIn, kEnvIn };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "osc.syncosc";
        d.displayName = "Sync Osc";
        d.description = "Hard sync: a master oscillator at the key's pitch restarts a second one, the slave, at each of its "
                        "cycles. Sync sets how far the slave sits above (in semitones) - the tone, not the pitch, changes; "
                        "sweep it with an envelope or LFO into Sync In (Sync Mod: semitones per unit) for the classic "
                        "tearing lead. Saw, Square or Triangle, band-limited; Master mixes the master in. Env In shapes "
                        "each voice; with nothing there a voice plays while its key is held. Pitch In: semitones / 60.";
        d.section = ModuleSection::Oscillator;
        d.searchKeywords = "sync hard sync oscillator lead sweep vco";
        d.sockets = {
            modIn ("syncIn", "Sync In"), modIn ("pitchIn", "Pitch In"), modIn ("envIn", "Env In"),
            midiIn ("addMidiIn", "Add MIDI In"), audioOut ("audioOut", "Audio Out")
        };
        d.params = {
            makeCombo  ("wave",    "Wave",     { "Saw", "Square", "Triangle" }, 0, 0, 2).noMod(),
            makeRotary ("sync",    "Sync",     0.0f, 48.0f, 12.0f, 0, "st"),
            makeRotary ("syncMod", "Sync Mod", 0.0f, 48.0f, 24.0f, 0, "st"),
            makeRotary ("fine",    "Fine",     -50.0f, 50.0f, 0.0f, 1, "ct"),
            makeRotary ("master",  "Master",   0.0f, 100.0f, 0.0f, 1, "%"),
            makeRotary ("level",   "Level",    0.0f, 1.0f, 0.7f, 1),
            makeRotary ("voices",  "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod(),
            makeRotary ("glide",   "Glide",    0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < kMaxVoices; ++v) voiceReset (v); }
    void voiceReset (int v) override
    {
        pool.resetVoice (v); glide.resetVoice (v); gate.resetVoice (v);
        mPhase[v] = sPhase[v] = 0.0; held[v] = 0.0f; masterHeld[v] = 0.0f;
    }
    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        pool.noteOn (v, voiceLimit());
        glide.noteOn (v, (float) note, isMonoVoice());
        gate.noteOn (v);
        if (! retrigger) { mPhase[v] = 0.0; sPhase[v] = 0.0; held[v] = 0.0f; }
    }
    void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); gate.noteOff (v); }
    void voiceVelocity (int v, float velocity01) override { gate.setVelocity (v, velocity01); }
    double voiceTailSeconds() const override { return 0.02; }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const float voiceGain = pool.nextGain (v, sampleRate);
        if (pool.isSilent (v)) { outputs[0] = { 0.0f, 0.0f }; return; }

        const double note = glide.next (v, param (pGlide), isMonoVoice(), ! pool.isMuted (v), sampleRate)
                          + pitchBendSemitones() + param (pFine) * 0.01 + inputs[kPitchIn][0] * 60.0f;
        const double mHz = midiNoteToHz (note);
        const double syncSt = juce::jlimit (0.0, 72.0, (double) param (pSync) + (double) inputs[kSyncIn][0] * param (pSyncMod));
        const double mInc = juce::jlimit (0.0, 0.49, mHz / sampleRate);
        const double sInc = juce::jlimit (0.0, 0.49, mInc * std::pow (2.0, syncSt / 12.0));
        const int wave = juce::jlimit (0, 2, (int) std::lround (param (pWave)));

        // one step of the pair (one sample, or with CPU > Anti-alias half a sample at twice the rate):
        // the slave's value one sample late, its steps' corrections reaching back into the sample before
        const bool aa = aquanode::antiAliasOn();
        auto step = [&] (double mInc, double sInc) -> float
        {
        float now = 0.0f;
        double mp = mPhase[v] + mInc, sp = sPhase[v] + sInc;
        bool reset = false;
        double t = 0.0;   // where in this sample the master restarted it (0..1, from the end)
        if (mp >= 1.0)
        {
            mp -= 1.0;
            reset = true;
            t = mInc > 0.0 ? mp / mInc : 0.0;   // (samples since the restart)
        }
        if (reset)
        {
            // the slave where it was at the restart, and from 0 since
            const double before = sp - t * sInc;
            const double wrapped = before - std::floor (before);
            const float h = waveAt (wave, 0.0) - waveAt (wave, wrapped);
            sp = t * sInc;
            now = waveAt (wave, sp);
            held[v] += h * blepBefore (t - 1.0);   // (the sample before)
            now += h * blepAfter (t);
        }
        else
        {
            if (sp >= 1.0)
            {
                sp -= 1.0;
                const double tt = sInc > 0.0 ? sp / sInc : 0.0;
                if (wave != 2)
                {
                    const float h = wave == 0 ? -2.0f : 2.0f;   // (saw falls by 2, square rises by 2 at the cycle)
                    now = waveAt (wave, sp);
                    held[v] += h * blepBefore (tt - 1.0);
                    now += h * blepAfter (tt);
                }
                else now = waveAt (wave, sp);
            }
            else now = waveAt (wave, sp);
            if (wave == 1 && sPhase[v] < 0.5 && sp >= 0.5)   // the square's fall
            {
                const double tt = sInc > 0.0 ? (sp - 0.5) / sInc : 0.0;
                held[v] += -2.0f * blepBefore (tt - 1.0);
                now += -2.0f * blepAfter (tt);
            }
        }
        mPhase[v] = mp;
        sPhase[v] = sp;

        const float out = held[v];
        held[v] = now;

        float masterWave = (float) (2.0 * mp - 1.0);
        if (aa)
        {
            // (6.9.21) Anti-alias: the master saw's fall corrected as the slave's steps are (and one
            // sample late with it)
            float mNow = masterWave;
            if (reset)
            {
                masterHeld[v] += -2.0f * blepBefore (t - 1.0);
                mNow += -2.0f * blepAfter (t);
            }
            masterWave = masterHeld[v];
            masterHeld[v] = mNow;
        }
        const float master = masterWave * param (pMaster) * 0.01f;
        return out * (1.0f - 0.5f * param (pMaster) * 0.01f) + master;
        };
        float mixed;
        if (aa)
        {
            if (! aaWasOn[v]) { aaDown[v].reset(); aaWasOn[v] = true; }
            // (at four times the rate: the restarts make partials far above the top)
            const float a = step (mInc * 0.25, sInc * 0.25), b = step (mInc * 0.25, sInc * 0.25), c = step (mInc * 0.25, sInc * 0.25);
            mixed = aaDown[v].process (a, b, c, step (mInc * 0.25, sInc * 0.25));
        }
        else
        {
            aaWasOn[v] = false;
            mixed = step (mInc, sInc);
        }
        const float amp = gate.next (v, isInputConnected (kEnvIn), inputs[kEnvIn][0], sampleRate);
        const float y = mixed * amp * param (pLevel) * voiceGain * 0.6f;
        outputs[0] = { y, y };
    }

private:
    ModuleVoicePool pool;
    ModuleGlide glide;
    ModuleVoiceGate gate;
    double mPhase[kMaxVoices] {}, sPhase[kMaxVoices] {};
    float held[kMaxVoices] {}, masterHeld[kMaxVoices] {};
    aquanode::Decimator4x aaDown[kMaxVoices];   // (6.9.21: CPU > Anti-alias - the pair at four times the rate)
    bool aaWasOn[kMaxVoices] {};
};

AQUANODE_REGISTER_MODULE (SyncOscModule, SyncOscModule::descriptor)
