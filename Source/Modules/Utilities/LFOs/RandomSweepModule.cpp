//==============================================================================
// Random Sweep (6.9.18) - sample and hold, but each hold is a sweep: at every
// step it picks a new range (From and To) and a new speed, and until the next
// step an LFO sweeps between From and To at that speed - sometimes slow,
// sometimes fast.
//   Hold Rate     how often it picks; Hold Spread lets each hold be shorter or
//                 longer at random (100 %: a quarter to four times as long)
//   Low, High     the range From and To are picked in (0..100 % of the outputs)
//   Slowest, Fastest  the speeds it picks between (on a log scale)
//   Shape         Sine, Triangle or Ramp (From to To, again and again)
//   Smooth        a glide on Sweep where a new hold jumps
//   Retrig Note   a new path for each note
// Outputs (0..1): Sweep (the LFO between From and To), From, To and Speed
// (where the speed lies between 0.05 and 50 Hz, on a log scale - into an Audio
// LFO's Rate, say). Per voice.
//==============================================================================
#include "../../Core/ModuleCore.h"

using namespace aquanode;

class RandomSweepModule final : public SynthModule
{
public:
    enum P { pHoldRate = 0, pHoldSpread, pLow, pHigh, pSlowest, pFastest, pShape, pSmooth, pRetrig };
    static constexpr float kMinSpeed = 0.05f, kMaxSpeed = 50.0f;

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.randomsweep";
        d.displayName = "Random Sweep";
        d.searchKeywords = "random sweep sample hold s&h lfo range speed";
        d.description = "Sample and hold where each hold is a sweep: every step picks a new range (From, To) and a new speed, "
                        "and an LFO sweeps between them at that speed until the next - sometimes slow, sometimes fast. Hold "
                        "Rate and Hold Spread set the steps; Low and High the range they are picked in; Slowest and Fastest "
                        "the speeds. Outputs Sweep, From, To and Speed (0..1; Speed on a log scale, 0.05 to 50 Hz).";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 15;
        d.sockets = { unipolarOut ("sweepOut", "Sweep"), unipolarOut ("fromOut", "From"), unipolarOut ("toOut", "To"),
                      unipolarOut ("speedOut", "Speed") };
        d.params = {
            makeRotary ("holdRate", "Hold Rate", 0.05f, 20.0f, 0.5f, 0, "Hz", true),
            makeRotary ("holdSpread", "Hold Spread", 0.0f, 100.0f, 30.0f, 0, "%", false, 1.0f),
            makeRotary ("low", "Low", 0.0f, 100.0f, 0.0f, 0, "%", false, 1.0f),
            makeRotary ("high", "High", 0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
            makeRotary ("slowest", "Slowest", kMinSpeed, kMaxSpeed, 0.25f, 1, "Hz", true),
            makeRotary ("fastest", "Fastest", kMinSpeed, kMaxSpeed, 8.0f, 1, "Hz", true),
            makeCombo ("shape", "Shape", { "Sine", "Triangle", "Ramp" }, 0, 1, 2).noMod(),
            makeRotary ("smooth", "Smooth", 0.0f, 100.0f, 0.0f, 2, "%", false, 1.0f),
            makeCombo ("retrig", "Retrig", { "Free", "Note" }, 1, 2, 2).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (int v = 0; v < kMaxVoices; ++v) voiceReset (v); }
    void voiceReset (int v) override
    {
        auto& s = st[(size_t) v];
        s = {};
        s.seed = 0x9E3779B9u * (juce::uint32) (v + 1) + counter;
        pick (s);
        s.out = s.from;
    }
    void voiceNoteOn (int v, int, bool retrigger) override
    {
        if (param (pRetrig) > 0.5f && ! retrigger)
        {
            auto& s = st[(size_t) v];
            s.seed = 0x85EBCA6Bu * ++counter;
            pick (s);
            s.out = s.from;
        }
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* outputs) override
    {
        auto& s = st[(size_t) v];
        s.holdLeft -= 1.0 / sampleRate;
        if (s.holdLeft <= 0.0) pick (s);

        s.phase += s.speed / sampleRate;
        s.phase -= std::floor (s.phase);
        float t = 0.0f;
        switch ((int) std::lround (param (pShape)))
        {
            case 1:  t = (float) (s.phase < 0.5 ? 2.0 * s.phase : 2.0 - 2.0 * s.phase); break;
            case 2:  t = (float) s.phase; break;
            default: t = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * s.phase); break;
        }
        const float target = s.from + (s.to - s.from) * t;
        const float smooth = param (pSmooth) * 0.01f;
        if (smooth < 0.001f) s.out = target;
        else s.out += (1.0f - std::exp (-1.0f / ((float) sampleRate * (0.002f + 0.5f * smooth * smooth)))) * (target - s.out);

        const float speedNorm = std::log ((float) s.speed / kMinSpeed) / std::log (kMaxSpeed / kMinSpeed);
        outputs[0] = { s.out, s.out };
        outputs[1] = { s.from, s.from };
        outputs[2] = { s.to, s.to };
        outputs[3] = { speedNorm, speedNorm };

        if (v == 0 && ++historyTick >= 300)   // (a coarse trace for the card)
        {
            historyTick = 0;
            const int pos = historyPos.load (std::memory_order_relaxed);
            history[(size_t) pos].store (s.out, std::memory_order_relaxed);
            historyLo[(size_t) pos].store (juce::jmin (s.from, s.to), std::memory_order_relaxed);
            historyHi[(size_t) pos].store (juce::jmax (s.from, s.to), std::memory_order_relaxed);
            historyPos.store ((pos + 1) % kHistory, std::memory_order_relaxed);
        }
    }

    static constexpr int kHistory = 160;
    float historyAt (int i, int which) const
    {
        const int at = (historyPos.load (std::memory_order_relaxed) + i) % kHistory;
        const auto& h = which == 0 ? history : which == 1 ? historyLo : historyHi;
        return h[(size_t) at].load (std::memory_order_relaxed);
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 70; }

private:
    struct State
    {
        juce::uint32 seed { 1 };
        double phase { 0 }, speed { 1 }, holdLeft { 0 };
        float from { 0 }, to { 1 }, out { 0 };
    };

    static float rnd (State& s)   // 0..1 (xorshift)
    {
        s.seed ^= s.seed << 13; s.seed ^= s.seed >> 17; s.seed ^= s.seed << 5;
        return (float) (s.seed & 0xffffff) / 16777216.0f;
    }

    void pick (State& s)
    {
        if (s.seed == 0) s.seed = 1;
        const float lo = juce::jmin (param (pLow), param (pHigh)) * 0.01f, hi = juce::jmax (param (pLow), param (pHigh)) * 0.01f;
        s.from = lo + (hi - lo) * rnd (s);
        s.to = lo + (hi - lo) * rnd (s);
        const float slow = juce::jmin (param (pSlowest), param (pFastest)), fast = juce::jmax (param (pSlowest), param (pFastest));
        s.speed = (double) (slow * std::pow (fast / slow, rnd (s)));
        s.phase = 0.0;
        const float spread = param (pHoldSpread) * 0.01f;
        s.holdLeft = (1.0 / juce::jmax (0.01, (double) param (pHoldRate))) * std::exp2 (2.0 * spread * (2.0 * rnd (s) - 1.0));
    }

    std::array<State, kMaxVoices> st {};
    juce::uint32 counter { 0 };
    std::array<std::atomic<float>, kHistory> history {}, historyLo {}, historyHi {};
    std::atomic<int> historyPos { 0 };
    int historyTick { 0 };
};

namespace
{
    class SweepView final : public juce::Component, private juce::Timer
    {
    public:
        explicit SweepView (RandomSweepModule& m) : module (&m), raw (&m) { setInterceptsMouseClicks (false, false); startTimerHz (20); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 4.0f);
            if (module.get() == nullptr) return;
            r = r.reduced (4.0f);
            const int n = RandomSweepModule::kHistory;
            auto x = [&] (int i) { return r.getX() + r.getWidth() * (float) i / (float) (n - 1); };
            auto y = [&] (float v) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, v) * r.getHeight(); };
            juce::Path band, line;
            for (int i = 0; i < n; ++i) { if (i == 0) band.startNewSubPath (x (i), y (raw->historyAt (i, 2))); else band.lineTo (x (i), y (raw->historyAt (i, 2))); }
            for (int i = n - 1; i >= 0; --i) band.lineTo (x (i), y (raw->historyAt (i, 1)));
            band.closeSubPath();
            g.setColour (juce::Colour (0xffb4f08c).withAlpha (0.16f));
            g.fillPath (band);
            for (int i = 0; i < n; ++i) { if (i == 0) line.startNewSubPath (x (i), y (raw->historyAt (i, 0))); else line.lineTo (x (i), y (raw->historyAt (i, 0))); }
            g.setColour (juce::Colour (0xffb4f08c));
            g.strokePath (line, juce::PathStrokeType (1.4f));
        }
    private:
        void timerCallback() override { repaint(); }
        juce::WeakReference<SynthModule> module;
        RandomSweepModule* raw;
    };
}

std::unique_ptr<juce::Component> RandomSweepModule::createExtraContentComponent()
{
    return std::make_unique<SweepView> (*this);
}

AQUANODE_REGISTER_MODULE (RandomSweepModule, RandomSweepModule::descriptor)
