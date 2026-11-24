//==============================================================================
// Spectrogram - SpectralResolve's visualizer as a module (it was "Reassigned
// Spectrogram" until 5.19.2)
//
// A scrolling spectrogram: x = time, y = frequency, brightness = amplitude.
// With Reassign on, every bin's energy is moved to the frequency - and, with
// Time Fix, the moment - it really belongs to, so a sine is a thin line
// instead of a blurred band and a click a sharp stroke; off, it is the plain
// FFT, bin by bin. Draw Sharp keeps every point (or bin) crisp, Smooth blurs
// them softly into each other. Envelope draws, faintly at the right edge, each
// input's slow average spectrum (level across, frequency up).
//
// Three inputs, one colour each: Cyan, Pink and Yellow. Where they overlap
// the colours add up like light, so it is easy to see where two sounds fight
// for the same frequencies. With Delta on, Yellow shows instead where Cyan and
// Pink differ (the brighter, the bigger the difference). Thru passes Cyan on
// unchanged, to put the module inline.
//
// Compared with the plugin's view:
//   - each reassigned point is drawn with sub-pixel precision (split between
//     the two pixel rows - and, with Time Reassign, the two columns - it falls
//     between), the way a good spectrum view keeps its lines sharp at any
//     zoom, and the brightest of the points landing on a pixel wins
//   - Decimation (Off by default) Auto picks the largest decimation that still keeps High,
//     so a low range gets the finest frequency resolution by itself; the hop
//     is kept the same length in real time with Speed Lock on, so the scroll
//     speed never changes with it (until 5.19.3 a low High slowed it down)
//   - the analysis runs on a background thread, never inside the audio
//     callback (a 16384-point frame on a phone would otherwise click)
//   - Low and High pick any range (20 - 200 Hz, 10 - 11 kHz ...), on a log or
//     linear axis; Labels shows the frequency grid; a tap (or a held finger
//     moved around) shows the frequency and nearest note under it
//==============================================================================
#include "SpectralAnalysis.h"
#include "LightColour.h"

using namespace aquanode;
using namespace aquanode::spectral;

namespace
{
    constexpr int kInputs = 3;
    constexpr int kQueueSlots = 48;

    const juce::StringArray& fftChoices()   { static const juce::StringArray c { "1024", "2048", "4096", "8192", "16384" }; return c; }
    const juce::StringArray& hopChoices()   { static const juce::StringArray c { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }; return c; }
    // ("Blackman" is the 4-term Blackman-Harris; the list is one unit wide since 5.19.3)
    const juce::StringArray& winChoices()   { static const juce::StringArray c { "Hann", "Blackman", "Nuttall", "Kaiser" }; return c; }
    const juce::StringArray& decimChoices() { static const juce::StringArray c { "Auto", "Off", "/2", "/4", "/8", "/16" }; return c; }

    // light colours, added up where the inputs overlap: A, B, C/Delta (cyan, pink, yellow to begin
    // with; 6.9.15: their knobs, in nm, set them - each view sets these as it draws)
    constexpr float kOriginal[kInputs][3] = { { 0.0f, 1.0f, 1.0f }, { 1.0f, 0.35f, 0.75f }, { 1.0f, 1.0f, 0.0f } };
    constexpr float kDefaultNm[kInputs] = { 487.0f, 730.0f, 578.0f };
    float kColour[kInputs][3] = { { 0.0f, 1.0f, 1.0f }, { 1.0f, 0.35f, 0.75f }, { 1.0f, 1.0f, 0.0f } };

    ParamSpec lightKnob (const char* id, const char* label, float def)
    {
        auto p = makeRotary (id, label, aquanode::light::kMinNm, aquanode::light::kMaxNm, def, 4, {}, false, 1.0f).noMod();
        p.textFromValue = [] (double v) { return aquanode::light::textOf (v); };
        return p;
    }

    // one hop's worth of all three inputs, handed from the analysis thread to the display
    struct FrameSet
    {
        std::array<std::vector<Event>, kInputs> events;
        std::array<int, kInputs> count {};
        std::array<std::vector<float>, kInputs> bins;   // the plain spectrum (Reassign off, or Delta)
        std::array<int, kInputs> numBins {};
        double binHz { 1.0 };
        int serial { 0 };   // the analysis settings it was made with
    };
}

class ReassignedSpectrogramModule final : public SynthModule,
                                          private juce::TimeSliceClient
{
public:
    // (patches save knobs by name, so a new one can sit where it belongs)
    enum P { pLow, pHigh, pRange, pGain, pSpeed, pFft, pHop, pWindow, pSpeedLock, pDecim, pScale, pLabels, pTimeRe,
             pThreshold, pMaxShift, pFreeze, pReassign, pDraw, pEnvelope, pDelta, pAccuracy,
             pColA, pColB, pColC, pFreezeA, pFreezeB, pFreezeC };

    // Accuracy: how far a reassigned point may move from its bin and still be
    // drawn (anything moved further is mostly noise). 0 % lets points move 4
    // bins, 50 % one bin, 100 % only a tenth of one - a log scale either side
    static float maxShiftForAccuracy (float a)
    {
        a = juce::jlimit (0.0f, 1.0f, a);
        return a <= 0.5f ? std::pow (4.0f, 1.0f - 2.0f * a) : std::pow (10.0f, -(2.0f * a - 1.0f));
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.spectrogram";
        d.displayName = "Spectrogram";
        d.description = "A scrolling spectrogram: x = time, y = frequency, brightness = amplitude. Reassign moves every "
                        "bin's energy to its true frequency (and time, with Time Fix), so tones are thin lines; Accuracy "
                        "drops the points that had to move too far (noise), stricter the higher it is. Off, the plain FFT. Draw Sharp or Smooth; Envelope adds each input's slow average spectrum, faintly, at "
                        "the right edge. Three inputs - A, B, C - whose colours add up where they overlap; "
                        "with Delta on, C shows where A and B differ. Low / High pick any range, a tap shows "
                        "the frequency and note under it. Decimate trades time for finer frequencies (Off by default); "
                        "Speed Lock keeps the scroll speed while it does. Thru passes A on. Each has its colour as light "
                        "(nm; past 700 the purples, which no single wavelength makes) and its own Freeze (its last "
                        "spectrum goes on being drawn).";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 40;
        d.sockets = { audioIn ("in1", "A"), audioIn ("in2", "B"), audioIn ("in3", "C"),
                      audioOut ("thru", "Thru") };
        d.params = {
            makeRotary ("low", "Low", 20.0f, 20000.0f, 20.0f, 0, "Hz", true),
            makeRotary ("high", "High", 20.0f, 22000.0f, 20000.0f, 0, "Hz", true),
            makeRotary ("range", "Range", 30.0f, 140.0f, 90.0f, 0, "dB"),
            makeRotary ("gain", "Gain", -24.0f, 24.0f, 0.0f, 0, "dB"),
            makeRotary ("speed", "Speed", 1.0f, 8.0f, 2.0f, 0, "px", false, 1.0f),

            makeCombo ("fft", "FFT", fftChoices(), 2, 1, 1).noMod(),
            makeCombo ("hop", "Hop", hopChoices(), 2, 1, 1).noMod(),
            makeCombo ("window", "Window", winChoices(), 3, 1, 1).noMod(),
            makeCombo ("speedLock", "Speed Lock", { "Off", "On" }, 1, 1, 1).noMod(),
            makeCombo ("decim", "Decimate", decimChoices(), 1, 1, 1).noMod(),   // Off (5.19.3; it was Auto)

            makeCombo ("scale", "Axis", { "Log", "Linear" }, 0, 2, 1).noMod(),
            makeCombo ("labels", "Labels", { "Off", "On" }, 1, 2, 1).noMod(),
            makeCombo ("timeRe", "Time Fix", { "Off", "On" }, 1, 2, 1).noMod(),
            makeRotary ("threshold", "Floor", -100.0f, -5.0f, -60.0f, 2, "dB"),
            // (5.19.3: Accuracy below took its place; kept for older patches)
            makeRotary ("maxShift", "Peak Shift", 0.1f, 4.0f, 1.0f, 2, "bins").hide(),
            makeCombo ("freeze", "Freeze (all)", { "Run", "Freeze" }, 0, 3, 1).noMod().hide(),   // (6.9.15: one each now)
            // (after the old ones: saved patches keep their places)
            makeCombo ("reassign", "Reassign", { "Off", "On" }, 1, 3, 1).noMod().whenMissing (1.0f),
            makeCombo ("draw", "Draw", { "Sharp", "Smooth" }, 0, 3, 1).noMod().whenMissing (0.0f),
            makeCombo ("envelope", "Envelope", { "Off", "On" }, 1, 3, 1).noMod().whenMissing (0.0f),
            makeCombo ("delta", "Delta", { "Off", "On" }, 0, 3, 1).noMod().whenMissing (0.0f),
            makeRotary ("accuracy", "Accuracy", 0.0f, 100.0f, 50.0f, 2, "%"),
            lightKnob ("colA", "A Colour", kDefaultNm[0]), lightKnob ("colB", "B Colour", kDefaultNm[1]),
            lightKnob ("colC", "C/Delta Colour", kDefaultNm[2]),
            makeCombo ("freezeA", "Freeze A", { "Off", "On" }, 0, 5, 1).noMod(),
            makeCombo ("freezeB", "Freeze B", { "Off", "On" }, 0, 5, 1).noMod(),
            makeCombo ("freezeC", "Freeze C/Delta", { "Off", "On" }, 0, 5, 1).noMod()
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
        // the hop counts decimated samples: with Speed Lock it is kept the same
        // length in real time, so decimating (Auto: a lower High) never slows
        // the scroll; off, a decimated picture scrolls slower (and costs less)
        if (knob (pSpeedLock) > 0.5f)
            c.hopDenom *= c.decim;
        c.timeReassign = knob (pTimeRe) > 0.5f;
        c.thresholdDb = knob (pThreshold);
        c.maxShiftBins = maxShiftForAccuracy (knob (pAccuracy) * 0.01f);
        c.keepBins = knob (pReassign) < 0.5f || knob (pDelta) > 0.5f;   // the plain spectrum, when it is drawn
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

        // (6.9.15) each its own Freeze: a frozen one keeps its last spectrum
        bool frozenOne[kInputs];
        bool allFrozen = true;
        for (int i = 0; i < kInputs; ++i)
        {
            frozenOne[i] = knob (pFreeze) > 0.5f || knob (pFreezeA + i) > 0.5f;
            if ((mask & (1 << i)) && ! frozenOne[i]) allFrozen = false;
        }
        const bool frozen = knob (pFreeze) > 0.5f || allFrozen;
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
                if ((mask & (1 << i)) && ! frozenOne[i])
                    analyzers[(size_t) i].analyse();   // (outside the lock: the display keeps drawing)

            const juce::ScopedLock sl (queueLock);
            auto& set = pool[(size_t) ((queueHead + queueCount) % kQueueSlots)];
            for (int i = 0; i < kInputs; ++i)
            {
                set.count[(size_t) i] = 0;
                set.numBins[(size_t) i] = 0;
                if (! (mask & (1 << i)))
                    continue;
                const auto& a = analyzers[(size_t) i];
                set.count[(size_t) i] = a.eventCount();
                std::copy (a.eventData(), a.eventData() + a.eventCount(), set.events[(size_t) i].begin());
                if (cfg.keepBins)
                {
                    set.numBins[(size_t) i] = a.binCount();
                    set.bins[(size_t) i].assign (a.binData(), a.binData() + a.binCount());
                    set.binHz = a.binHz();
                }
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
            setColours (*m);
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

            // Envelope: each input's slow average spectrum, faintly, along the right
            // edge (its level leftwards from the edge, frequency up as the picture)
            if (m->knob (ReassignedSpectrogramModule::pEnvelope) > 0.5f)
            {
                const float range = m->knob (ReassignedSpectrogramModule::pRange);
                const float gain = m->knob (ReassignedSpectrogramModule::pGain);
                const float span = (float) plot.getWidth() * 0.22f;
                const bool delta = m->knob (ReassignedSpectrogramModule::pDelta) > 0.5f;
                for (int i = 0; i < kInputs; ++i)
                {
                    const auto& e = envelope[(size_t) i];
                    if ((int) e.size() != plot.getHeight() || ! (m->inputLive (i) || (i == 2 && delta)))
                        continue;
                    juce::Path p;
                    bool open = false;
                    for (int cy = 0; cy < (int) e.size(); ++cy)
                    {
                        const float t = juce::jlimit (0.0f, 1.0f, (e[(size_t) cy] + gain + range) / range);
                        if (t <= 0.01f) { open = false; continue; }
                        const float x = (float) plot.getRight() - 2.0f - t * span, y = (float) plot.getY() + (float) cy;
                        if (! open) { p.startNewSubPath (x, y); open = true; } else p.lineTo (x, y);
                    }
                    const auto c = juce::Colour::fromFloatRGBA (kColour[i][0], kColour[i][1], kColour[i][2], 1.0f);
                    g.setColour (c.withAlpha (0.5f));
                    g.strokePath (p, juce::PathStrokeType (1.1f));
                }
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
                const bool deltaOn = m->knob (ReassignedSpectrogramModule::pDelta) > 0.5f;
                const char* names[kInputs] = { "A", "B", deltaOn ? "A / B" : "C" };
                for (int i = 0; i < kInputs; ++i)
                {
                    const auto c = juce::Colour::fromFloatRGBA (kColour[i][0], kColour[i][1], kColour[i][2], 1.0f);
                    const bool live = i == 2 && deltaOn ? (m->inputLive (0) && m->inputLive (1)) : m->inputLive (i);
                    auto cell = legend.removeFromLeft (i == 2 && deltaOn ? 76 : 48);
                    g.setColour (c.withAlpha (live ? 1.0f : 0.25f));
                    g.fillEllipse (cell.removeFromLeft (10).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
                    g.drawText (names[i], cell, juce::Justification::centredLeft);
                }
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                const auto lo = juce::jmin (m->knob (ReassignedSpectrogramModule::pLow), m->knob (ReassignedSpectrogramModule::pHigh));
                const auto hi = juce::jmax (m->knob (ReassignedSpectrogramModule::pLow), m->knob (ReassignedSpectrogramModule::pHigh));
                juce::String frozen;
                if (m->knob (ReassignedSpectrogramModule::pFreeze) > 0.5f) frozen = "   FROZEN";
                else
                {
                    const char* fn[kInputs] = { "A", "B", deltaOn ? "Delta" : "C" };
                    juce::StringArray f;
                    for (int k = 0; k < kInputs; ++k) if (m->knob (ReassignedSpectrogramModule::pFreezeA + k) > 0.5f) f.add (fn[k]);
                    if (! f.isEmpty()) frozen = "   FROZEN: " + f.joinIntoString (" ");
                }
                g.drawText (hzText (lo) + " - " + hzText (hi) + frozen,
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

            const bool reassign = m->knob (ReassignedSpectrogramModule::pReassign) > 0.5f;
            const bool smoothDraw = m->knob (ReassignedSpectrogramModule::pDraw) > 0.5f;
            const bool delta = m->knob (ReassignedSpectrogramModule::pDelta) > 0.5f;
            for (auto& e : envelope)
                if ((int) e.size() != ih) e.assign ((size_t) ih, -200.0f);

            // a brightness for a level, and the level on a row from the plain spectrum
            auto brightOf = [&] (float db)
            {
                const float v = juce::jlimit (0.0f, 1.0f, (db + gain + range) / range);
                return v * v * (3.0f - 2.0f * v);   // smooth, darker floor
            };
            auto rowDb = [&] (const FrameSet& set, int i, int cy)
            {
                const int nb = set.numBins[(size_t) i];
                const auto& bins = set.bins[(size_t) i];
                const double h0 = hzOf ((float) cy + 0.5f), h1 = hzOf ((float) cy - 0.5f), hc = hzOf ((float) cy);
                const int k0 = (int) std::floor (juce::jmin (h0, h1) / set.binHz), k1 = (int) std::ceil (juce::jmax (h0, h1) / set.binHz);
                if (k1 - k0 > 2)
                {
                    float v = -200.0f;   // several bins on this row: the loudest
                    for (int k = juce::jmax (1, k0); k <= juce::jmin (nb - 1, k1); ++k)
                        v = juce::jmax (v, bins[(size_t) k]);
                    return v;
                }
                // a bin taller than a row: Sharp holds it (steps), Smooth slides between bins
                const double kf = hc / set.binHz;
                const int lo = juce::jlimit (1, juce::jmax (1, nb - 2), (int) std::floor (kf));
                if (! smoothDraw)
                    return bins[(size_t) juce::jlimit (1, nb - 1, (int) std::lround (kf))];
                const float f = (float) (kf - (double) lo);
                return bins[(size_t) lo] + (bins[(size_t) lo + 1] - bins[(size_t) lo]) * f;
            };

            setColours (*m);
            const bool deltaHeld = m->knob (ReassignedSpectrogramModule::pFreezeC) > 0.5f;   // (6.9.15)
            m->drainFrames ([&] (const FrameSet& set)
            {
                if (set.serial != shownSerial)
                    return;
                any = true;
                block.assign ((size_t) speed * (size_t) ih * 3, 0.0f);
                rowLevel.assign ((size_t) kInputs * (size_t) ih, -200.0f);

                auto put = [&] (int cx, int cy, float b, int i)
                {
                    if (cy < 0 || cy >= ih || cx < 0 || cx >= speed || b <= 0.0f)
                        return;
                    float* px = &block[((size_t) cx * (size_t) ih + (size_t) cy) * 3];
                    for (int c = 0; c < 3; ++c)
                        px[c] = juce::jmax (px[c], b * kColour[i][c] * 1.6f);
                };

                for (int i = 0; i < kInputs; ++i)
                {
                    if (i == 2 && delta)
                        continue;   // Yellow shows the delta instead

                    if (reassign)
                    {
                        const auto* ev = set.events[(size_t) i].data();
                        for (int n = 0; n < set.count[(size_t) i]; ++n)
                        {
                            const float bright = brightOf (ev[n].db);
                            if (bright <= 0.0f)
                                continue;
                            const float y = yOf (ev[n].hz);
                            if (y < -3.0f || y > h + 2.0f)
                                continue;
                            const int yr = juce::jlimit (0, ih - 1, (int) std::lround (y));
                            rowLevel[(size_t) i * (size_t) ih + (size_t) yr] = juce::jmax (rowLevel[(size_t) i * (size_t) ih + (size_t) yr], ev[n].db);
                            // sub-pixel in frequency: split between the two rows it falls between
                            // (Smooth: spread softly over a few). In time the point spans its
                            // frame's columns, brightest at the moment reassignment puts it
                            const float centre = ((float) speed - 1.0f) * 0.5f + ev[n].hops * (float) speed;
                            const int y0 = (int) std::floor (y);
                            const float fy = y - (float) y0;
                            for (int cx = 0; cx < speed; ++cx)
                            {
                                const float dist = std::abs ((float) cx - centre) / (float) speed;
                                const float wx = speed == 1 || smoothDraw ? 1.0f : juce::jmax (0.0f, 1.0f - dist * dist);
                                if (wx <= 0.0f)
                                    continue;
                                if (smoothDraw)
                                {
                                    for (int dy = -2; dy <= 3; ++dy)
                                    {
                                        const float d = (float) dy - fy;
                                        put (cx, y0 + dy, bright * wx * std::exp (-0.6f * d * d), i);
                                    }
                                }
                                else
                                {
                                    put (cx, y0, bright * wx * (1.0f - fy), i);
                                    put (cx, y0 + 1, bright * wx * fy, i);
                                }
                            }
                        }
                    }
                    else if (set.numBins[(size_t) i] > 2)
                    {
                        // the plain FFT, row by row
                        for (int cy = 0; cy < ih; ++cy)
                        {
                            const float db = rowDb (set, i, cy);
                            rowLevel[(size_t) i * (size_t) ih + (size_t) cy] = db;
                            const float bright = brightOf (db);
                            for (int cx = 0; cx < speed; ++cx)
                                put (cx, cy, bright, i);
                        }
                    }
                }

                // Delta: where Cyan and Pink differ, in yellow (from their plain spectra)
                if (delta && set.numBins[0] > 2 && set.numBins[1] > 2)
                {
                    // (6.9.15) Freeze C/Delta: the difference as it was when frozen
                    if ((int) heldDelta.size() != 2 * ih) { heldDelta.assign ((size_t) (2 * ih), 0.0f); heldDeltaValid = false; }
                    for (int cy = 0; cy < ih; ++cy)
                    {
                        float shownLevel, rowL;
                        if (deltaHeld && heldDeltaValid)
                        {
                            shownLevel = heldDelta[(size_t) cy];
                            rowL = heldDelta[(size_t) (ih + cy)];
                        }
                        else
                        {
                            const float a = rowDb (set, 0, cy), b = rowDb (set, 1, cy);
                            const float level = brightOf (juce::jmax (a, b));
                            const float diff = juce::jlimit (0.0f, 1.0f, std::abs (a - b) / 24.0f);
                            shownLevel = level * diff;
                            rowL = juce::jmax (a, b) - (1.0f - diff) * range;
                            heldDelta[(size_t) cy] = shownLevel;
                            heldDelta[(size_t) (ih + cy)] = rowL;
                        }
                        rowLevel[2 * (size_t) ih + (size_t) cy] = rowL;
                        for (int cx = 0; cx < speed; ++cx)
                            put (cx, cy, shownLevel, 2);
                    }
                    if (! deltaHeld) heldDeltaValid = true;
                }

                // the slow average spectra (Envelope), in dB
                for (int i = 0; i < kInputs; ++i)
                    for (int cy = 0; cy < ih; ++cy)
                    {
                        float& e = envelope[(size_t) i][(size_t) cy];
                        const float v = juce::jmax (rowLevel[(size_t) i * (size_t) ih + (size_t) cy], -gain - range - 6.0f);
                        e = e <= -199.0f ? v : e + 0.04f * (v - e);
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

        // (6.9.15) the three colours from this module's knobs
        static void setColours (const ReassignedSpectrogramModule& m)
        {
            for (int i = 0; i < kInputs; ++i)
            {
                const auto c = aquanode::light::colourAt (m.knob (ReassignedSpectrogramModule::pColA + i), kDefaultNm[i],
                                                          juce::Colour::fromFloatRGBA (kOriginal[i][0], kOriginal[i][1], kOriginal[i][2], 1.0f));
                kColour[i][0] = c.getFloatRed(); kColour[i][1] = c.getFloatGreen(); kColour[i][2] = c.getFloatBlue();
            }
        }
        std::vector<float> heldDelta;
        bool heldDeltaValid { false };
        juce::WeakReference<SynthModule> module;
        juce::Image image;
        int writeCol { 0 };
        std::vector<float> block, rowLevel;
        std::array<std::vector<float>, kInputs> envelope;   // each input's slow average, dB per row
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
