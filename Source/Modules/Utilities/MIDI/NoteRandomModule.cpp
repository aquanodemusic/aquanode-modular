//==============================================================================
// Note Random - a random value for every note played, as modulation: the same
// key played twice comes out a little different each time, the way no two
// strikes on an instrument are alike. Per voice, so each note of a chord has
// its own value. Into a Fine knob it detunes each note a touch, into a
// cutoff it gives each note its own brightness, into a Volume its own level.
//
//   Amount  how far the value goes (100 %: the whole -1..1, or 0..1 Unipolar)
//   Mode    Every Note: a new value at each note-on
//           Per Key: each key keeps a value of its own (a slightly detuned
//           piano: the same key always sounds the same), set by Seed
//           Both: the key's own value plus a smaller new one every note
//   Spread  Even (every value as likely) or Centred (most notes near the
//           middle, now and then one further out - more natural for pitch)
//   Drift   and the value wanders slowly away from where the note began,
//           by up to this share of Amount, at Drift Rate
//   Glide   how long the value takes to reach a new note's value (0: at once)
//   Seed    which values Per Key gives
//
// No inputs. Output: 0 = Mod Out.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class NoteRandomModule final : public SynthModule
{
public:
    enum P { pAmount, pMode, pSpread, pPolarity, pDrift, pDriftRate, pGlide, pSeed };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.noterandom";
        d.displayName = "Note Random";
        d.description = "A random value for every note played, per voice: the same key sounds a little different each time. "
                        "Into a Fine knob it detunes each note slightly, into a cutoff each note gets its own brightness, into "
                        "a Volume its own level. Amount sets how far it goes; Mode: a new value every note, a fixed one per key "
                        "(Seed picks which), or both; Spread Even or Centred; Drift lets the value wander slowly while the note "
                        "sounds; Glide eases into each new value.";
        d.section = ModuleSection::Utility;
        d.sockets = { modOut ("modOut", "Mod Out") };
        d.params = { makeRotary ("amount", "Amount", 0.0f, 100.0f, 20.0f, 0, "%"),
                     makeCombo ("mode", "Mode", { "Every Note", "Per Key", "Both" }, 0, 0, 2),
                     makeCombo ("spread", "Spread", { "Even", "Centred" }, 1, 0, 2),
                     makeCombo ("polarity", "Polarity", { "Bipolar", "Unipolar" }, 0, 1, 2),
                     makeRotary ("drift", "Drift", 0.0f, 100.0f, 0.0f, 1, "%"),
                     makeRotary ("driftRate", "Drift Rate", 0.02f, 10.0f, 0.5f, 1, "Hz", true),
                     makeRotary ("glide", "Glide", 0.0f, 2000.0f, 0.0f, 2, "ms"),
                     makeRotary ("seed", "Seed", 0.0f, 999.0f, 1.0f, 2, {}, false, 1.0f) };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& s : state) s = {}; }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& s = state[(size_t) v];
        const int mode = (int) param (pMode);
        const bool centred = param (pSpread) > 0.5f;
        float value = 0.0f;
        if (mode == 0 || mode == 2)
            value += (mode == 2 ? 0.35f : 1.0f) * draw (random, centred);
        if (mode == 1 || mode == 2)
        {
            juce::Random keyed ((juce::int64) (juce::roundToInt (param (pSeed)) * 1000003 + juce::jlimit (0, 127, note) * 7919 + 17));
            keyed.nextInt();   // nearby seeds would start alike
            value += (mode == 2 ? 0.65f : 1.0f) * draw (keyed, centred);
        }
        s.target = juce::jlimit (-1.0f, 1.0f, value);
        if (! s.started || param (pGlide) < 0.5f)
            s.value = s.target;
        s.started = true;
        s.driftPos = 0.0f;
        s.driftFrom = s.driftTo = 0.0f;
        s.driftPhase = 1.0f;   // a fresh drift point at once
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        auto& s = state[(size_t) v];
        const float glideMs = param (pGlide);
        if (glideMs >= 0.5f)
            s.value += (1.0f - std::exp (-1.0f / (glideMs * 0.001f * (float) sampleRate))) * (s.target - s.value);
        else
            s.value = s.target;

        // the drift: a smooth random path (cosine-joined points) around the note's value
        float drift = 0.0f;
        const float driftAmt = param (pDrift) * 0.01f;
        if (driftAmt > 0.0f)
        {
            s.driftPhase += param (pDriftRate) / (float) sampleRate;
            if (s.driftPhase >= 1.0f)
            {
                s.driftPhase -= std::floor (s.driftPhase);
                s.driftFrom = s.driftTo;
                s.driftTo = random.nextFloat() * 2.0f - 1.0f;
            }
            const float t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * s.driftPhase);
            drift = (s.driftFrom + (s.driftTo - s.driftFrom) * t) * driftAmt;
        }

        float x = juce::jlimit (-1.0f, 1.0f, s.value + drift) * param (pAmount) * 0.01f;
        if (param (pPolarity) > 0.5f)
            x = 0.5f * (x + param (pAmount) * 0.01f);   // 0..Amount
        out[0] = { x, x };
    }

private:
    // -1..1: even, or centred (the mean of three: most values near the middle)
    static float draw (juce::Random& r, bool centred)
    {
        if (! centred)
            return r.nextFloat() * 2.0f - 1.0f;
        return (r.nextFloat() + r.nextFloat() + r.nextFloat()) * (2.0f / 3.0f) - 1.0f;
    }

    struct State { float value = 0.0f, target = 0.0f, driftFrom = 0.0f, driftTo = 0.0f, driftPos = 0.0f, driftPhase = 1.0f; bool started = false; };
    std::array<State, kMaxVoices> state {};
    juce::Random random { juce::Time::currentTimeMillis() };
};

AQUANODE_REGISTER_MODULE (NoteRandomModule, NoteRandomModule::descriptor)
