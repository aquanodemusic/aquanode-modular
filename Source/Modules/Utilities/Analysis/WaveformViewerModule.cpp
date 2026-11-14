// Waveform Viewer - an oscilloscope: the samples themselves, no FFT. x =
// time, y = amplitude (the +-1 lines are full scale, 0 dBFS).
//
// Up to three sounds over each other - Cyan, Pink, Yellow - or with Delta,
// Yellow becomes Cyan minus Pink (what a module adds or takes away, when
// Cyan is its output and Pink its input). Time is the width of the window
// (0.5 ms to 2 s), Zoom the height. Trigger holds a periodic wave still: each
// picture starts where the trigger sound (the first one patched) crosses
// Level, rising or falling, found to a fraction of a sample; Free runs.
// Channel picks Mid, Left or Right. Freeze holds the picture. Thru passes
// Cyan on.
//
// The audio thread writes the three inputs into one ring (one write index,
// so they stay sample-aligned); the display copies the newest stretch it
// needs thirty times a second.
#include "../../Core/ModuleCore.h"
#include "WaveDraw.h"

using namespace aquanode;

namespace
{
    constexpr int kInputs = 3;
    const juce::Colour kColour[kInputs] = { juce::Colour (0xff3fe6f2), juce::Colour (0xffff6fbe), juce::Colour (0xffffd23f) };
}

class WaveformViewerModule : public SynthModule
{
public:
    enum P { pTime = 0, pZoom, pTrigger, pLevel, pChannel, pDelta, pFreeze };
    static constexpr int kRing = 1 << 18;   // 5.4 s at 48 kHz: a 2 s window and room to find its trigger

    WaveformViewerModule()
    {
        for (auto& r : ring)
            r.assign ((size_t) kRing, 0.0f);
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.waveform";
        d.displayName = "Waveform Viewer";
        d.description =
            "An oscilloscope, no FFT: x = time, y = amplitude (the +-1 lines are full scale). Up to three sounds "
            "over each other (Cyan, Pink, Yellow), or with Delta Yellow becomes Cyan minus Pink. Time is the window "
            "(0.5 ms to 2 s), Zoom the height; Trigger holds a periodic wave still where the first patched sound "
            "crosses Level (rising or falling), Free lets it run. Channel: Mid, Left or Right. Thru passes Cyan on.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 42;
        d.sockets = { audioIn ("in1", "Cyan"), audioIn ("in2", "Pink"), audioIn ("in3", "Yellow"), audioOut ("thru", "Thru") };
        d.params = {
            makeRotary ("time", "Time", 0.5f, 2000.0f, 20.0f, 0, "ms", true),
            makeRotary ("zoom", "Zoom", 0.1f, 20.0f, 1.0f, 0, "x", true),
            makeCombo ("trigger", "Trigger", { "Free", "Rising", "Falling" }, 1, 0, 1).noMod(),
            makeRotary ("level", "Level", -1.0f, 1.0f, 0.0f, 0),
            makeCombo ("channel", "Channel", { "Mid", "Left", "Right" }, 0, 1, 1).noMod(),
            makeCombo ("delta", "Delta", { "Off", "On" }, 0, 1, 1).noMod(),
            makeCombo ("freeze", "Freeze", { "Run", "Freeze" }, 0, 1, 1).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int ch = (int) param (pChannel);
        const int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < kInputs; ++i)
        {
            const float v = ! isInputConnected (i) ? 0.0f
                          : ch == 1 ? in[i][0] : ch == 2 ? in[i][1] : 0.5f * (in[i][0] + in[i][1]);
            ring[(size_t) i][(size_t) w] = v;
        }
        writePos.store ((w + 1) & (kRing - 1), std::memory_order_release);
        out[0] = in[0];
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 560; }
    int extraContentHeight() const override { return 250; }

    //=== for the display (message thread) ====================================
    // the newest n samples of input i, oldest first
    void read (int i, float* dest, int n) const
    {
        const int end = writePos.load (std::memory_order_acquire);
        const auto& r = ring[(size_t) i];
        for (int k = 0; k < n; ++k)
            dest[k] = r[(size_t) ((end - n + k) & (kRing - 1))];
    }
    float knob (int index) const { return getParameterBase (index); }
    bool inputLive (int i) const { return isInputConnected (i); }
    double rate() const { return sampleRate; }

private:
    std::array<std::vector<float>, kInputs> ring;
    std::atomic<int> writePos { 0 };
};

//==============================================================================
namespace
{
    class WaveformView final : public juce::Component, private juce::Timer
    {
    public:
        explicit WaveformView (WaveformViewerModule& m) : module (&m)
        {
            setOpaque (true);
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff0b0d10));
            const auto plot = getLocalBounds().toFloat().reduced (8.0f, 8.0f).withTrimmedBottom (12.0f);
            const float zoom = module->knob (WaveformViewerModule::pZoom);

            // the grid: ten divisions across, the centre line, full scale (+-1) brighter
            for (int k = 1; k < 10; ++k)
            {
                g.setColour (juce::Colours::white.withAlpha (k == 5 ? 0.12f : 0.06f));
                g.fillRect (plot.getX() + plot.getWidth() * (float) k / 10.0f, plot.getY(), 1.0f, plot.getHeight());
            }
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float v : { 1.0f, -1.0f, 0.5f, -0.5f })
            {
                const float y = wavedraw::yOf (v, plot, zoom);
                if (y < plot.getY() || y > plot.getBottom()) continue;
                const bool full = std::abs (v) == 1.0f;
                g.setColour (juce::Colours::white.withAlpha (full ? 0.28f : 0.08f));
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                if (full)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.45f));
                    g.drawText (v > 0 ? "+1" : "-1", juce::Rectangle<float> (plot.getX() + 2.0f, y + (v > 0 ? 1.0f : -11.0f), 20.0f, 10.0f),
                                juce::Justification::topLeft);
                }
            }
            // the trigger level
            const int trig = (int) module->knob (WaveformViewerModule::pTrigger);
            if (trig != wavedraw::Free)
            {
                const float y = wavedraw::yOf (module->knob (WaveformViewerModule::pLevel), plot, zoom);
                if (y >= plot.getY() && y <= plot.getBottom())
                {
                    g.setColour (juce::Colours::white.withAlpha (0.35f));
                    const float dashes[] = { 3.0f, 3.0f };
                    g.drawDashedLine (juce::Line<float> (plot.getRight() - 24.0f, y, plot.getRight(), y), dashes, 2, 1.0f);
                }
            }
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawText (wavedraw::timeText (windowMs) + " across, " + wavedraw::timeText (windowMs / 10.0) + " / div",
                        juce::Rectangle<float> (plot.getX(), plot.getBottom() + 1.0f, plot.getWidth(), 11.0f), juce::Justification::centredLeft);

            bool any = false;
            g.saveState();
            g.reduceClipRegion (plot.toNearestInt());
            for (int i = 0; i < kInputs; ++i)
            {
                if (! shown[(size_t) i]) continue;
                any = true;
                wavedraw::drawTrace (g, plot, data[(size_t) i].data(), (int) data[(size_t) i].size(), start, windowN, zoom,
                                     kColour[i].withAlpha (i == 2 && deltaOn ? 0.95f : 0.9f), 1.5f);
            }
            g.restoreState();
            if (! any)
            {
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawText ("Waiting for sound (Cyan, Pink, Yellow)", plot, juce::Justification::centred);
            }
            if (deltaOn)
            {
                g.setColour (kColour[2]);
                g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                g.drawText ("Yellow = Cyan - Pink", juce::Rectangle<float> (plot.getRight() - 120.0f, plot.getY() + 1.0f, 118.0f, 11.0f),
                            juce::Justification::topRight);
            }
        }

    private:
        void timerCallback() override
        {
            if (module->knob (WaveformViewerModule::pFreeze) > 0.5f)
                return;
            const double sr = module->rate() > 0.0 ? module->rate() : 48000.0;
            windowMs = juce::jlimit (0.5, 2000.0, (double) module->knob (WaveformViewerModule::pTime));
            windowN = windowMs * 0.001 * sr;
            // room to find a trigger: up to another window (at least 50 ms), within the ring
            const int search = (int) juce::jmin ((double) WaveformViewerModule::kRing / 2, juce::jmax (windowN, 0.05 * sr));
            const int len = juce::jmin (WaveformViewerModule::kRing - 8, (int) std::ceil (windowN) + search + 4);
            deltaOn = module->knob (WaveformViewerModule::pDelta) > 0.5f;

            int trigSource = -1;
            for (int i = 0; i < kInputs; ++i)
            {
                const bool live = module->inputLive (i) || (i == 2 && deltaOn && (module->inputLive (0) || module->inputLive (1)));
                shown[(size_t) i] = live;
                data[(size_t) i].resize ((size_t) len);
                if (module->inputLive (i)) module->read (i, data[(size_t) i].data(), len);
                else std::fill (data[(size_t) i].begin(), data[(size_t) i].end(), 0.0f);
                if (live && trigSource < 0 && module->inputLive (i)) trigSource = i;
            }
            if (deltaOn)
                for (int k = 0; k < len; ++k)
                    data[2][(size_t) k] = data[0][(size_t) k] - data[1][(size_t) k];
            start = trigSource < 0 ? 0.0
                  : wavedraw::triggerStart (data[(size_t) trigSource].data(), len, windowN,
                                            (int) module->knob (WaveformViewerModule::pTrigger),
                                            module->knob (WaveformViewerModule::pLevel), search);
            repaint();
        }

        WaveformViewerModule* module;
        std::array<std::vector<float>, kInputs> data;
        std::array<bool, kInputs> shown {};
        double start { 0.0 }, windowN { 960.0 }, windowMs { 20.0 };
        bool deltaOn { false };
    };
}

std::unique_ptr<juce::Component> WaveformViewerModule::createExtraContentComponent()
{
    return std::make_unique<WaveformView> (*this);
}

AQUANODE_REGISTER_MODULE (WaveformViewerModule, WaveformViewerModule::descriptor)
