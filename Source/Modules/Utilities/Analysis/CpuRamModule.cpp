// CPU / RAM - how much the patch (unconnected) or the chain feeding its input
// (connected) costs: its share of the audio thread's real time and its memory.
//
// Global: the whole audio block's time against the block's length (as a DAW's
// meter shows it), and the app's resident memory as the system reports it.
// A chain: every module upstream of Audio In - by audio, knob or MIDI cables,
// back to the start. While a CPU / RAM module is in the patch the engine times
// one module at a time on every sample for 0.1 s (longer than any FFT hop),
// then the next; hosted plugins time their own blocks exactly. A module's memory is what the app's resident memory grew by
// when the module was made and prepared - an estimate (shared tables, samples
// loaded later and the system's paging are not in it), shown as "approx.".
// The audio passes through unchanged.

#include "../../Core/ModuleCore.h"

using namespace aquanode;

namespace
{
    class CpuRamModule final : public SynthModule
    {
    public:
        CpuRamModule()  { cpuMeterCount().fetch_add (1); }
        ~CpuRamModule() override { cpuMeterCount().fetch_sub (1); }

        void processSample (const StereoFrame* inputs, StereoFrame* outputs) override { outputs[0] = inputs[0]; }

        bool isCpuMeter() const override { return true; }
        void setCpuReading (const CpuReading& r) override   // (audio thread)
        {
            chain.store (r.chainShare, std::memory_order_relaxed);
            global.store (r.globalShare, std::memory_order_relaxed);
            chainRam.store (r.chainRam, std::memory_order_relaxed);
            connected.store (r.connected, std::memory_order_relaxed);
            for (int i = 0; i < 3; ++i)
            {
                topName[i].store (i < r.numTop ? r.topName[i] : nullptr, std::memory_order_relaxed);
                topShare[i].store (i < r.numTop ? r.topShare[i] : 0.0f, std::memory_order_relaxed);
            }
            fresh.store (true, std::memory_order_release);
        }

        std::unique_ptr<juce::Component> createExtraContentComponent() override;
        int extraContentHeight() const override { return 172; }

        std::atomic<float> chain { 0.0f }, global { 0.0f };
        std::atomic<juce::int64> chainRam { 0 };
        std::atomic<bool> connected { false }, fresh { false };
        std::atomic<const juce::String*> topName[3] { nullptr, nullptr, nullptr };
        std::atomic<float> topShare[3] { 0.0f, 0.0f, 0.0f };
    };

    // 8.2 %, 61 % (one decimal under 10)
    juce::String percent (float pct)
    {
        pct = juce::jmax (0.0f, pct);
        return (pct < 10.0f ? juce::String (pct, 1) : juce::String ((int) std::lround (pct))) + " %";
    }

    juce::String megabytes (juce::int64 bytes)
    {
        const double mb = (double) bytes / (1024.0 * 1024.0);
        return mb >= 100.0 ? juce::String ((int) std::lround (mb)) + " MB" : juce::String (mb, 1) + " MB";
    }

    class CpuRamDisplay final : public juce::Component, private juce::Timer
    {
    public:
        explicit CpuRamDisplay (CpuRamModule& m) : module (&m) { startTimerHz (4); timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff121417));
            g.fillRoundedRectangle (r, 4.0f);
            auto in = r.reduced (8.0f, 6.0f);
            const auto cyan = juce::Colour (0xff1ad6ec), green = juce::Colour (0xff35e08a), amber = juce::Colour (0xffffb648), red = juce::Colour (0xffff5a5a);
            auto loadColour = [&] (float pct) { return pct < 50.0f ? green : (pct < 80.0f ? amber : red); };
            const float shown = 100.0f * (isConnected ? chainPct : globalPct);

            // the numbers: CPU and RAM large, what they are under them, and
            // (connected) the whole patch's and the app's in a line of their own
            auto top = in.removeFromTop (46.0f);
            auto cpuArea = top.removeFromLeft (top.getWidth() * 0.5f);
            g.setColour (loadColour (shown));
            g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
            g.drawFittedText (percent (shown), cpuArea.removeFromTop (32.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText (isConnected ? "CPU, this chain" : "CPU, whole patch", cpuArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

            g.setColour (cyan);
            g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
            g.drawFittedText (isConnected ? "~" + megabytes (chainRamBytes) : megabytes (processRam), top.removeFromTop (32.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText (isConnected ? "RAM, this chain (approx.)" : "RAM, the whole app", top.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            if (isConnected)
            {
                g.drawFittedText ("Whole patch " + percent (100.0f * globalPct) + ",  app " + megabytes (processRam),
                                  in.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            }

            // the heaviest three of the chain
            if (isConnected)
            {
                auto list = in.removeFromBottom (42.0f);
                g.setFont (juce::Font (juce::FontOptions (12.0f)));
                for (int i = 0; i < 3; ++i)
                {
                    auto row = list.removeFromTop (14.0f);
                    if (i >= numTop) continue;
                    g.setColour (juce::Colours::white.withAlpha (0.8f));
                    g.drawText (juce::String (i + 1) + ". " + names[i], row.removeFromLeft (row.getWidth() - 56.0f), juce::Justification::centredLeft, true);
                    g.setColour (loadColour (100.0f * shares[i] * 3.0f));
                    g.drawText (percent (100.0f * shares[i]), row, juce::Justification::centredRight, false);
                }
            }

            // the last 30 seconds
            auto graph = in.reduced (0.0f, 4.0f);
            if (graph.getHeight() > 8.0f && ! history.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.fillRoundedRectangle (graph, 3.0f);
                float peak = 0.25f;
                for (float v : history) peak = juce::jmax (peak, v);
                const float scaleTop = std::ceil (peak * 4.0f) / 4.0f;   // (25 % steps)
                juce::Path p;
                const int n = (int) history.size();
                for (int i = 0; i < n; ++i)
                {
                    const float x = graph.getX() + graph.getWidth() * (float) (kHistory - n + i) / (float) (kHistory - 1);
                    const float y = graph.getBottom() - graph.getHeight() * juce::jlimit (0.0f, 1.0f, history[(size_t) i] / scaleTop);
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (loadColour (shown).withAlpha (0.9f));
                g.strokePath (p, juce::PathStrokeType (1.5f));
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText (juce::String ((int) std::lround (scaleTop * 100.0f)) + " %", graph.reduced (3.0f, 1.0f), juce::Justification::topRight, false);
            }
        }

    private:
        static constexpr int kHistory = 120;   // 30 s at 4 a second

        void timerCallback() override
        {
            auto* m = dynamic_cast<CpuRamModule*> (module.get());
            if (m == nullptr) return;
            processRam = processResidentBytes();
            if (! m->fresh.exchange (false, std::memory_order_acquire))
            {
                repaint();   // (no audio running: the memory still updates)
                return;
            }
            isConnected = m->connected.load (std::memory_order_relaxed);
            chainPct = m->chain.load (std::memory_order_relaxed);
            globalPct = m->global.load (std::memory_order_relaxed);
            chainRamBytes = m->chainRam.load (std::memory_order_relaxed);
            numTop = 0;
            for (int i = 0; i < 3; ++i)
                if (const auto* nm = m->topName[i].load (std::memory_order_relaxed))
                {
                    names[numTop] = *nm;
                    shares[numTop] = m->topShare[i].load (std::memory_order_relaxed);
                    ++numTop;
                }
            history.push_back (isConnected ? chainPct : globalPct);
            if ((int) history.size() > kHistory) history.erase (history.begin());
            repaint();
        }

        juce::WeakReference<SynthModule> module;
        bool isConnected { false };
        float chainPct { 0.0f }, globalPct { 0.0f };
        juce::int64 chainRamBytes { 0 }, processRam { 0 };
        juce::String names[3];
        float shares[3] {};
        int numTop { 0 };
        std::vector<float> history;
    };

    std::unique_ptr<juce::Component> CpuRamModule::createExtraContentComponent()
    {
        return std::make_unique<CpuRamDisplay> (*this);
    }
}

static ModuleDescriptor cpuRamDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.cpuram";
    d.displayName = "CPU / RAM";
    d.description =
        "How much the patch costs. Unconnected: the whole patch's share of the audio thread's time (as a DAW's "
        "meter shows it) and the app's memory. Connected: the chain feeding Audio In - every module upstream, by "
        "audio, knob or MIDI cables, back to the start - its CPU share, its memory (approx.: what the app grew by "
        "when those modules were made) and its three heaviest modules. The audio passes straight through. While "
        "one is in the patch the modules are timed one after another, 0.1 s each, so the meter itself costs very little.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 18;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (CpuRamModule, cpuRamDescriptor)
