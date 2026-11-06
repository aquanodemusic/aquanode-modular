//==============================================================================
// Reassigned Spectrogram - SpectralResolve's visualizer as a module
//
// A scrolling spectrogram (x time, y frequency, brightness amplitude) drawn
// with spectral reassignment: every bin's energy is moved to the frequency -
// and, with Time Reassign, the moment - it really belongs to, so a sine is a
// thin line instead of a blurred band and a click a sharp stroke.
//
// Three inputs, one colour each: 1 cyan, 2 magenta, 3 yellow. Where they
// overlap the colours add up like light (cyan + magenta = a light blue, all
// three white), so it is easy to see where two sounds fight for the same
// frequencies. Thru passes input 1 on unchanged, to put the module inline.
//
// Compared with the plugin's view:
//   - each reassigned point is drawn with sub-pixel precision (split between
//     the two pixel rows - and, with Time Reassign, the two columns - it falls
//     between), the way a good spectrum view keeps its lines sharp at any
//     zoom, and the brightest of the points landing on a pixel wins
//   - Decimation Auto picks the largest decimation that still keeps High,
//     so a low range gets the finest frequency resolution by itself
//   - the analysis runs on a background thread, never inside the audio
//     callback (a 16384-point frame on a phone would otherwise click)
//   - Low and High pick any range (20 - 200 Hz, 10 - 11 kHz ...), on a log or
//     linear axis; Labels shows the frequency grid; a tap (or a held finger
//     moved around) shows the frequency and nearest note under it
//==============================================================================
#include "SpectralAnalysis.h"

using namespace aquanode;
using namespace aquanode::spectral;

namespace
{
    constexpr int kInputs = 3;
    constexpr int kQueueSlots = 48;

    const juce::StringArray& fftChoices()   { static const juce::StringArray c { "1024", "2048", "4096", "8192", "16384" }; return c; }
    const juce::StringArray& hopChoices()   { static const juce::StringArray c { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }; return c; }
    const juce::StringArray& winChoices()   { static const juce::StringArray c { "Hann", "Blackman-Harris", "Nuttall", "Kaiser" }; return c; }
    const juce::StringArray& decimChoices() { static const juce::StringArray c { "Auto", "Off", "/2", "/4", "/8", "/16" }; return c; }

    // light colours, added up where the inputs overlap
    constexpr float kColour[kInputs][3] = { { 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f } };

    // one hop's worth of all three inputs, handed from the analysis thread to the display
    struct FrameSet
    {
        std::array<std::vector<Event>, kInputs> events;
        std::array<int, kInputs> count {};
        int serial { 0 };   // the analysis settings it was made with
    };
}

class ReassignedSpectrogramModule final : public SynthModule,
                                          private juce::TimeSliceClient
{
public:
    enum P { pLow, pHigh, pRange, pGain, pSpeed, pFft, pHop, pWindow, pDecim, pScale, pLabels, pTimeRe,
             pThreshold, pMaxShift, pFreeze };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.spectrogram";
        d.displayName = "Reassigned Spectrogram";
        d.description = "SpectralResolve's scrolling spectrogram (time across, frequency up, brightness the level) "
                        "with spectral reassignment: every bin's energy moved to its true frequency (and time), so "
                        "tones are thin lines. Three inputs in cyan, magenta and yellow - their colours add up where "
                        "they overlap. Low / High pick any range, Labels show the frequencies, a tap shows the "
                        "frequency and note under it. Thru passes input 1 on.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 40;
        d.sockets = { audioIn ("in1", "In 1 Cyan"), audioIn ("in2", "In 2 Magenta"), audioIn ("in3", "In 3 Yellow"),
                      audioOut ("thru", "Thru") };
        d.params = {
            makeRotary ("low", "Low", 20.0f, 20000.0f, 20.0f, 0, "Hz", true),
            makeRotary ("high", "High", 20.0f, 22000.0f, 20000.0f, 0, "Hz", true),
            makeRotary ("range", "Range", 30.0f, 140.0f, 90.0f, 0, "dB"),
            makeRotary ("gain", "Gain", -24.0f, 24.0f, 0.0f, 0, "dB"),
            makeRotary ("speed", "Speed", 1.0f, 8.0f, 2.0f, 0, "px", false, 1.0f),

            makeCombo ("fft", "FFT", fftChoices(), 2, 1, 1).noMod(),
            makeCombo ("hop", "Hop", hopChoices(), 2, 1, 1).noMod(),
            makeCombo ("window", "Window", winChoices(), 3, 1, 2).noMod(),
            makeCombo ("decim", "Decimate", decimChoices(), 0, 1, 1).noMod(),

            makeCombo ("scale", "Axis", { "Log", "Linear" }, 0, 2, 1).noMod(),
            makeCombo ("labels", "Labels", { "Off", "On" }, 1, 2, 1).noMod(),
            makeCombo ("timeRe", "Time Fix", { "Off", "On" }, 1, 2, 1).noMod(),
            makeRotary ("threshold", "Floor", -100.0f, -5.0f, -60.0f, 2, "dB"),
            makeRotary ("maxShift", "Max Shift", 0.1f, 4.0f, 1.0f, 2, "bins"),
            makeCombo ("freeze", "Freeze", { "Run", "Freeze" }, 0, 3, 1).noMod()
        };
        return d;
    }

    ReassignedSpectrogramModule()
    {
        for (auto& f : pool)
            for (auto& v : f.events)
                v.resize ((size_t) kMaxEvents);
    }

    ~ReassignedSpectrogramModule() override
    {
        thread->removeTimeSliceClient (this);
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        thread->removeTimeSliceClient (this);
        SynthModule::prepare (sr);
        for (auto& r : rings)
            r.setSize (1 << 17);
        for (auto& a : analyzers)
            a.prepare (sr);
        for (auto& b : scratch)
            b.assign (4096, 0.0f);
        connectedMask = -1;
        {
            const juce::ScopedLock sl (queueLock);
            queueHead = queueCount = 0;
        }
        thread->addTimeSliceClient (this);
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        for (int i = 0; i < kInputs; ++i)
            if (isInputConnected (i))
                rings[(size_t) i].push ((in[i][0] + in[i][1]) * 0.5f);
        out[0] = in[0];
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 560; }
    int extraContentHeight() const override { return 300; }

    //=== for the display (message thread) ====================================
    // hands every frame set that is waiting to fn; false while frozen (they are dropped)
    template <typename Fn>
    void drainFrames (Fn&& fn)
    {
        const juce::ScopedLock sl (queueLock);
        while (queueCount > 0)
        {
            fn (pool[(size_t) queueHead]);
            queueHead = (queueHead + 1) % kQueueSlots;
            --queueCount;
        }
    }

    float knob (int index) const { return getParameterBase (index); }
    bool inputLive (int i) const { return isInputConnected (i); }
    double displayRate() const { return sampleRate; }
    int settingsSerial() const { return serial.load(); }

private:
    Config makeConfig() const
    {
        Config c;
        c.order = kMinOrder + juce::jlimit (0, 4, (int) knob (pFft));
        c.hopDenom = 2 << juce::jlimit (0, 5, (int) knob (pHop));
        c.window = juce::jlimit (0, 3, (int) knob (pWindow));
        const int dc = juce::jlimit (0, 5, (int) knob (pDecim));
        c.decim = dc == 0 ? autoDecimation (sampleRate, knob (pHigh)) : (1 << (dc - 1));
        c.timeReassign = knob (pTimeRe) > 0.5f;
        c.thresholdDb = knob (pThreshold);
        c.maxShiftBins = knob (pMaxShift);
        return c;
    }

    // the analysis thread
    int useTimeSlice() override
    {
        const auto cfg = makeConfig();
        int mask = 0;
        for (int i = 0; i < kInputs; ++i)
            if (isInputConnected (i))
                mask |= 1 << i;

        // new settings or other inputs: every input starts over together, so
        // their frames stay in step
        if (mask != connectedMask || ! cfg.sameAnalysis (analyzers[0].config()))
        {
            connectedMask = mask;
            for (auto& a : analyzers)
            {
                a.restart();
                a.setConfig (cfg);
            }
            serial.fetch_add (1);
        }
        else
        {
            for (auto& a : analyzers)
                a.setConfig (cfg);
        }
        if (mask == 0)
        {
            for (auto& r : rings)   // nothing patched: nothing to keep
                while (r.pop (scratch[0].data(), 4096) > 0) {}
            return 30;
        }

        int ready = std::numeric_limits<int>::max();
        for (int i = 0; i < kInputs; ++i)
            if (mask & (1 << i))
                ready = juce::jmin (ready, rings[(size_t) i].available());
        ready = juce::jmin (ready, 4096);
        if (ready <= 0)
            return 10;

        for (int i = 0; i < kInputs; ++i)
            if (mask & (1 << i))
                rings[(size_t) i].pop (scratch[(size_t) i].data(), ready);

        const bool frozen = knob (pFreeze) > 0.5f;
        for (int s = 0; s < ready; ++s)
        {
            bool due = false;
            for (int i = 0; i < kInputs; ++i)
                if (mask & (1 << i))
                    due = analyzers[(size_t) i].step (scratch[(size_t) i][(size_t) s]) || due;
            if (! due || frozen)
                continue;

            {
                const juce::ScopedLock sl (queueLock);
                if (queueCount >= kQueueSlots)
                    continue;   // the display is behind: drop this frame
            }
            for (int i = 0; i < kInputs; ++i)
                if (mask & (1 << i))
                    analyzers[(size_t) i].analyse();   // (outside the lock: the display keeps drawing)

            const juce::ScopedLock sl (queueLock);
            auto& set = pool[(size_t) ((queueHead + queueCount) % kQueueSlots)];
            for (int i = 0; i < kInputs; ++i)
            {
                set.count[(size_t) i] = 0;
                if (! (mask & (1 << i)))
                    continue;
                const auto& a = analyzers[(size_t) i];
                set.count[(size_t) i] = a.eventCount();
                std::copy (a.eventData(), a.eventData() + a.eventCount(), set.events[(size_t) i].begin());
            }
            set.serial = serial.load();
            ++queueCount;
        }
        return ready >= 4096 ? 0 : 5;
    }

    juce::SharedResourcePointer<AnalysisThread> thread;
    std::array<SampleRing, kInputs> rings;
    std::array<Analyzer, kInputs> analyzers;
    std::array<std::vector<float>, kInputs> scratch;
    int connectedMask { -1 };
    std::atomic<int> serial { 0 };

    juce::CriticalSection queueLock;
    std::array<FrameSet, kQueueSlots> pool;
    int queueHead { 0 }, queueCount { 0 };
};

//==============================================================================
namespace
{
    class SpectrogramView final : public juce::Component, private juce::Timer
    {
    public:
        explicit SpectrogramView (ReassignedSpectrogramModule& m) : module (&m)
        {
            setOpaque (true);
            startTimerHz (30);
        }

        void resized() override { image = {}; }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff0b0d10));
            auto* m = dynamic_cast<ReassignedSpectrogramModule*> (module.get());
            if (m == nullptr)
                return;
            const auto plot = plotArea();

            if (image.isValid())
            {
                // the ring of columns: the oldest part first, the newest at the right edge
                const int w = image.getWidth(), h = image.getHeight();
                const int right = w - writeCol;
                g.drawImage (image, plot.getX(), plot.getY(), right, h, writeCol, 0, right, h);
                if (writeCol > 0)
                    g.drawImage (image, plot.getX() + right, plot.getY(), writeCol, h, 0, 0, writeCol, h);
            }

            // the frequency grid
            if (m->knob (ReassignedSpectrogramModule::pLabels) > 0.5f)
            {
                g.setFont (juce::Font (juce::FontOptions (9.5f)));
                for (const float hz : gridFrequencies())
                {
                    const float y = (float) plot.getY() + yOf (hz);
                    if (y < (float) plot.getY() + 4.0f || y > (float) plot.getBottom() - 4.0f)
                        continue;
                    g.setColour (juce::Colours::white.withAlpha (0.10f));
                    g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
                    g.setColour (juce::Colours::white.withAlpha (0.55f));
                    g.drawText (hzText (hz), plot.getX() + 3, (int) y - 11, 70, 11, juce::Justification::bottomLeft);
                }
            }

            // which inputs are patched
            {
                auto legend = getLocalBounds().removeFromBottom (14).reduced (4, 0);
                g.setFont (juce::Font (juce::FontOptions (9.5f)));
                const char* names[kInputs] = { "1", "2", "3" };
                for (int i = 0; i < kInputs; ++i)
                {
                    const auto c = juce::Colour::fromFloatRGBA (kColour[i][0], kColour[i][1], kColour[i][2], 1.0f);
                    const bool live = m->inputLive (i);
                    auto cell = legend.removeFromLeft (26);
                    g.setColour (c.withAlpha (live ? 1.0f : 0.25f));
                    g.fillEllipse (cell.removeFromLeft (10).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
                    g.drawText (names[i], cell, juce::Justification::centredLeft);
                }
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                const auto lo = juce::jmin (m->knob (ReassignedSpectrogramModule::pLow), m->knob (ReassignedSpectrogramModule::pHigh));
                const auto hi = juce::jmax (m->knob (ReassignedSpectrogramModule::pLow), m->knob (ReassignedSpectrogramModule::pHigh));
                g.drawText (hzText (lo) + " - " + hzText (hi) + (m->knob (ReassignedSpectrogramModule::pFreeze) > 0.5f ? "   FROZEN" : ""),
                            legend, juce::Justification::centredRight);
            }

            // the frequency under the finger
            if (probe.has_value())
            {
                const auto p = *probe;
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.drawHorizontalLine ((int) p.y, (float) plot.getX(), (float) plot.getRight());
                const float hz = hzOf ((float) p.y - (float) plot.getY());
                const auto text = hzText (hz) + "  " + noteName (hz);
                g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
                auto box = juce::Rectangle<int> (0, 0, 150, 18).withCentre (p.toInt()).translated (0, -16);
                box = box.constrainedWithin (plot);
                g.setColour (juce::Colour (0xcc000000));
                g.fillRoundedRectangle (box.toFloat(), 3.0f);
                g.setColour (juce::Colours::white);
                g.drawText (text, box, juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override { setProbe (e); }
        void mouseDrag (const juce::MouseEvent& e) override { setProbe (e); }
        void mouseUp (const juce::MouseEvent&) override { probeHideAt = juce::Time::getMillisecondCounter() + 1500; }

    private:
        juce::Rectangle<int> plotArea() const { return getLocalBounds().reduced (2).withTrimmedBottom (14); }

        void setProbe (const juce::MouseEvent& e)
        {
            probe = e.position.withY (juce::jlimit ((float) plotArea().getY(), (float) plotArea().getBottom() - 1.0f, e.position.y));
            probeHideAt = 0;
            repaint();
        }

        // the axis
        float lo() const
        {
            auto* m = dynamic_cast<ReassignedSpectrogramModule*> (module.get());
            const float a = m->knob (ReassignedSpectrogramModule::pLow), b = m->knob (ReassignedSpectrogramModule::pHigh);
            return juce::jmax (1.0f, juce::jmin (a, b));
        }
        float hi() const
        {
            auto* m = dynamic_cast<ReassignedSpectrogramModule*> (module.get());
            const float a = m->knob (ReassignedSpectrogramModule::pLow), b = m->knob (ReassignedSpectrogramModule::pHigh);
            return juce::jmax (lo() + 1.0f, juce::jmax (a, b));
        }
        bool logAxis() const
        {
            auto* m = dynamic_cast<ReassignedSpectrogramModule*> (module.get());
            return m->knob (ReassignedSpectrogramModule::pScale) < 0.5f;
        }
        float yOf (float hz) const   // in the plot, 0 = top
        {
            const float h = (float) plotArea().getHeight() - 1.0f;
            const float t = logAxis() ? std::log (juce::jmax (1.0f, hz) / lo()) / std::log (hi() / lo())
                                      : (hz - lo()) / (hi() - lo());
            return (1.0f - t) * h;
        }
        float hzOf (float y) const
        {
            const float h = (float) plotArea().getHeight() - 1.0f;
            const float t = 1.0f - y / juce::jmax (1.0f, h);
            return logAxis() ? lo() * std::pow (hi() / lo(), t) : lo() + t * (hi() - lo());
        }

        std::vector<float> gridFrequencies() const
        {
            std::vector<float> f;
            if (logAxis())
            {
                for (float decade = 10.0f; decade < 100000.0f; decade *= 10.0f)
                    for (const float k : { 1.0f, 2.0f, 5.0f })
                        f.push_back (decade * k);
            }
            else
            {
                const float span = hi() - lo();
                const float rough = span / 6.0f;
                const float mag = std::pow (10.0f, std::floor (std::log10 (rough)));
                const float step = rough / mag < 2.0f ? mag : rough / mag < 5.0f ? 2.0f * mag : 5.0f * mag;
                for (float x = std::ceil (lo() / step) * step; x <= hi(); x += step)
                    f.push_back (x);
            }
            return f;
        }

        void timerCallback() override
        {
            auto* m = dynamic_cast<ReassignedSpectrogramModule*> (module.get());
            if (m == nullptr)
                return;
            if (probe.has_value() && probeHideAt != 0 && juce::Time::getMillisecondCounter() > probeHideAt)
            {
                probe.reset();
                repaint();
            }

            const auto plot = plotArea();
            if (plot.getWidth() < 4 || plot.getHeight() < 4)
                return;

            // a new axis or new analysis: start a clean picture
            const auto key = std::make_tuple (lo(), hi(), logAxis(), plot.getWidth(), plot.getHeight());
            if (! image.isValid() || key != axisKey || m->settingsSerial() != shownSerial)
            {
                axisKey = key;
                shownSerial = m->settingsSerial();
                image = juce::Image (juce::Image::RGB, plot.getWidth(), plot.getHeight(), true);
                image.clear (image.getBounds(), juce::Colour (0xff0b0d10));
                writeCol = 0;
            }

            const float range = m->knob (ReassignedSpectrogramModule::pRange);
            const float gain = m->knob (ReassignedSpectrogramModule::pGain);
            const int speed = juce::jlimit (1, 8, (int) m->knob (ReassignedSpectrogramModule::pSpeed));
            const float h = (float) image.getHeight();
            const int ih = image.getHeight();
            bool any = false;

            m->drainFrames ([&] (const FrameSet& set)
            {
                if (set.serial != shownSerial)
                    return;
                any = true;
                block.assign ((size_t) speed * (size_t) ih * 3, 0.0f);

                for (int i = 0; i < kInputs; ++i)
                {
                    const auto* ev = set.events[(size_t) i].data();
                    for (int n = 0; n < set.count[(size_t) i]; ++n)
                    {
                        const float v = juce::jlimit (0.0f, 1.0f, (ev[n].db + gain + range) / range);
                        if (v <= 0.0f)
                            continue;
                        const float bright = v * v * (3.0f - 2.0f * v);   // smooth, darker floor
                        const float y = yOf (ev[n].hz);
                        if (y < -1.0f || y > h)
                            continue;
                        // sub-pixel in frequency: split between the two rows it falls between.
                        // In time the point spans its frame's columns, brightest at the
                        // moment reassignment puts it (a tone stays one unbroken line, a
                        // click stays a narrow stroke)
                        const float centre = ((float) speed - 1.0f) * 0.5f + ev[n].hops * (float) speed;
                        const int y0 = (int) std::floor (y);
                        const float fy = y - (float) y0;
                        for (int cx = 0; cx < speed; ++cx)
                        {
                            const float dist = std::abs ((float) cx - centre) / (float) speed;
                            const float wx = speed == 1 ? 1.0f : juce::jmax (0.0f, 1.0f - dist * dist);
                            if (wx <= 0.0f)
                                continue;
                            for (int dy = 0; dy < 2; ++dy)
                            {
                                const int cy = y0 + dy;
                                if (cy < 0 || cy >= ih)
                                    continue;
                                const float w = wx * (dy == 0 ? 1.0f - fy : fy);
                                float* px = &block[((size_t) cx * (size_t) ih + (size_t) cy) * 3];
                                for (int c = 0; c < 3; ++c)
                                    px[c] = juce::jmax (px[c], bright * w * kColour[i][c] * 1.6f);
                            }
                        }
                    }
                }

                // the inputs' colours add up like light
                for (int cx = 0; cx < speed; ++cx)
                {
                    const int col = writeCol;
                    writeCol = (writeCol + 1) % image.getWidth();
                    juce::Image::BitmapData bd (image, col, 0, 1, ih, juce::Image::BitmapData::writeOnly);
                    for (int cy = 0; cy < ih; ++cy)
                    {
                        const float* px = &block[((size_t) cx * (size_t) ih + (size_t) cy) * 3];
                        const auto bg = 0.043f;
                        bd.setPixelColour (0, cy, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, bg + px[0]),
                                                                              juce::jlimit (0.0f, 1.0f, bg + 0.008f + px[1]),
                                                                              juce::jlimit (0.0f, 1.0f, bg + 0.02f + px[2]), 1.0f));
                    }
                }
            });

            if (any)
                repaint();
        }

        juce::WeakReference<SynthModule> module;
        juce::Image image;
        int writeCol { 0 };
        std::vector<float> block;
        std::tuple<float, float, bool, int, int> axisKey {};
        int shownSerial { -1 };
        std::optional<juce::Point<float>> probe;
        juce::uint32 probeHideAt { 0 };
    };
}

std::unique_ptr<juce::Component> ReassignedSpectrogramModule::createExtraContentComponent()
{
    return std::make_unique<SpectrogramView> (*this);
}

AQUANODE_REGISTER_MODULE (ReassignedSpectrogramModule, ReassignedSpectrogramModule::descriptor)
