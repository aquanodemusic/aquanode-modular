//==============================================================================
// Periodogram - x = frequency, y = amplitude (SpectralCompare's view, with the
// EQ Curve Viewer folded in; it was "Spectrum Compare" until 5.19.2, the type
// id is kept so patches open)
//
// Up to three sounds' spectra over each other: Cyan, Pink and Yellow. With
// Delta on, Yellow is instead the difference Cyan minus Pink (in dB) around a
// centre line, so you see where one is louder than the other. As a connected
// graph or as bars; and in Reassigned precision every partial is drawn at the
// frequency spectral reassignment finds for it (not the FFT bin's centre),
// so two close tones that one bin would blur show up apart.
//
// The FFT runs on a background thread. Speed is how quickly the curves fall
// back (they rise at once): Very Fast not at all; Envelope draws a faint line
// over each that falls slowly (Slow) or keeps the highest (Hold). A tap (or a
// held finger) reads out the frequency, the note and the levels there. Thru
// passes Cyan on unchanged.
//==============================================================================
#include "SpectralAnalysis.h"
#include "LightColour.h"

using namespace aquanode;
using namespace aquanode::spectral;

namespace
{
    constexpr int kSources = 3;
    // (6.9.15) A, B and C/Delta: their colours from the knobs (in nm), set by each view as it paints
    const juce::Colour kOriginalColour[kSources] = { juce::Colour (0xff3fe6f2), juce::Colour (0xffff6fbe), juce::Colour (0xffffd23f) };
    constexpr float kDefaultNm[kSources] = { 487.0f, 730.0f, 578.0f };   // (cyan, pink - a purple - and yellow, as before)
    juce::Colour kSourceColour[kSources] = { kOriginalColour[0], kOriginalColour[1], kOriginalColour[2] };
    juce::Colour kDeltaColour (0xffffd23f);   // (C's: the delta takes its place)
    ParamSpec lightKnob (const char* id, const char* label, float def)
    {
        auto p = makeRotary (id, label, aquanode::light::kMinNm, aquanode::light::kMaxNm, def, 4, {}, false, 1.0f).noMod();
        p.textFromValue = [] (double v) { return aquanode::light::textOf (v); };
        return p;
    }
    constexpr float kSpeedSmooth[4] = { 0.0f, 0.55f, 0.8f, 0.94f };   // Very Fast, Fast, Medium, Slow

    const juce::StringArray& fftChoices() { static const juce::StringArray c { "1024", "2048", "4096", "8192", "16384" }; return c; }
}

class SpectrumCompareModule final : public SynthModule,
                                    private juce::TimeSliceClient
{
public:
    enum P { pView, pStyle, pPrecision, pLow, pHigh, pTop, pRange, pDeltaRange, pSmooth, pFft, pWindow, pScale, pLabels,
             pHold, pFreeze, pDelta, pSpeed, pColA, pColB, pColC, pFreezeA, pFreezeB, pFreezeC };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.spectrumcompare";
        d.displayName = "Periodogram";
        d.description = "A periodogram: x = frequency, y = amplitude (dBFS). Up to three sounds over each other - A, "
                        "B and C - or, with Delta on, C is the difference A minus B around a centre "
                        "line, where one is louder than the other. As a graph or as bars; Reassigned draws every partial "
                        "at the frequency spectral reassignment finds for it, finer than the FFT's bins. Speed from Very "
                        "Fast to Slow, a faint Envelope that falls slowly (or holds), any range; a tap reads out the "
                        "frequency, note and levels. Thru passes A on, so it can sit in a chain. Each has its colour as light "
                        "(nm; past 700 the purples, which no single wavelength makes) and its own Freeze.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 41;
        d.sockets = { audioIn ("in1", "A"), audioIn ("in2", "B"), audioIn ("in3", "C"), audioOut ("thru", "Thru") };
        d.params = {
            makeCombo ("view", "View", { "All", "A + B", "C Only", "A Only", "B Only" }, 0, 0, 2).noMod(),
            makeCombo ("style", "Style", { "Graph", "Bars" }, 0, 0, 1).noMod(),
            makeCombo ("precision", "Reassign", { "Off", "On" }, 0, 0, 1).noMod(),

            makeRotary ("low", "Low", 20.0f, 20000.0f, 20.0f, 1, "Hz", true),
            makeRotary ("high", "High", 20.0f, 22000.0f, 20000.0f, 1, "Hz", true),
            makeRotary ("top", "Top", -60.0f, 12.0f, 0.0f, 1, "dB"),
            makeRotary ("range", "Range", 24.0f, 140.0f, 90.0f, 1, "dB"),
            makeRotary ("deltaRange", "Delta +/-", 3.0f, 60.0f, 24.0f, 1, "dB"),

            makeRotary ("smooth", "Smooth", 0.0f, 0.99f, 0.8f, 2).hide().noMod(),   // (before Speed: kept for old patches)
            makeCombo ("fft", "FFT", fftChoices(), 3, 2, 1).noMod(),
            makeCombo ("window", "Window", { "Hann", "Blackman-Harris", "Nuttall", "Kaiser" }, 1, 2, 2).noMod(),
            makeCombo ("scale", "Axis", { "Log", "Linear" }, 0, 2, 1).noMod(),

            makeCombo ("labels", "Labels", { "Off", "On" }, 1, 3, 1).noMod(),
            makeCombo ("hold", "Envelope", { "Off", "Slow", "Hold" }, 1, 3, 1).noMod().whenMissing (0.0f),
            makeCombo ("freeze", "Freeze (all)", { "Run", "Freeze" }, 0, 3, 1).noMod().hide(),   // (6.9.15: one each now)
            // (after the old ones, so saved patches keep their places)
            makeCombo ("delta", "Delta", { "Off", "On" }, 0, 0, 1).noMod().whenMissing (1.0f),
            makeCombo ("speed", "Speed", { "Very Fast", "Fast", "Medium", "Slow" }, 1, 2, 2).noMod().whenMissing (2.0f),
            // (6.9.15) each one's colour as light (nm; past 700 the purples), and its own freeze
            lightKnob ("colA", "A Colour", kDefaultNm[0]), lightKnob ("colB", "B Colour", kDefaultNm[1]),
            lightKnob ("colC", "C/Delta Colour", kDefaultNm[2]),
            makeCombo ("freezeA", "Freeze A", { "Off", "On" }, 0, 5, 1).noMod(),
            makeCombo ("freezeB", "Freeze B", { "Off", "On" }, 0, 5, 1).noMod(),
            makeCombo ("freezeC", "Freeze C/Delta", { "Off", "On" }, 0, 5, 1).noMod()
        };
        return d;
    }

    ~SpectrumCompareModule() override { thread->removeTimeSliceClient (this); }

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
        {
            const juce::ScopedLock sl (snapLock);
            for (auto& s : snap)
            {
                s.bins.assign ((size_t) kMaxN / 2, -200.0f);
                s.events.resize ((size_t) kMaxEvents);
                s.numBins = s.numEvents = 0;
                s.fresh = false;
            }
        }
        thread->addTimeSliceClient (this);
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        for (int i = 0; i < kSources; ++i)
            if (isInputConnected (i))
                rings[(size_t) i].push ((in[i][0] + in[i][1]) * 0.5f);
        out[0] = in[0];
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 560; }
    int extraContentHeight() const override { return 280; }

    //=== for the display (message thread) ====================================
    struct Snapshot
    {
        std::vector<float> bins;
        std::vector<Event> events;
        int numBins { 0 }, numEvents { 0 };
        double binHz { 1.0 };
        bool fresh { false };
    };

    // copies source i's latest spectrum into out; false when nothing new came
    bool takeSnapshot (int i, Snapshot& out)
    {
        const juce::ScopedLock sl (snapLock);
        auto& s = snap[(size_t) i];
        if (! s.fresh)
            return false;
        s.fresh = false;
        out.numBins = s.numBins;
        out.numEvents = s.numEvents;
        out.binHz = s.binHz;
        out.bins.assign (s.bins.begin(), s.bins.begin() + s.numBins);
        out.events.assign (s.events.begin(), s.events.begin() + s.numEvents);
        return true;
    }

    float knob (int index) const { return getParameterBase (index); }
    bool inputLive (int i) const { return isInputConnected (i); }

private:
    int useTimeSlice() override
    {
        Config c;
        c.order = kMinOrder + juce::jlimit (0, 4, (int) knob (pFft));
        c.hopDenom = 4;
        c.window = juce::jlimit (0, 3, (int) knob (pWindow));
        c.decim = 1;
        c.timeReassign = false;
        c.thresholdDb = -90.0f;
        c.maxShiftBins = 2.0f;
        c.keepBins = true;

        int done = 0;
        for (int i = 0; i < kSources; ++i)
        {
            auto& a = analyzers[(size_t) i];
            a.setConfig (c);
            if (! isInputConnected (i))
            {
                while (rings[(size_t) i].pop (scratch[(size_t) i].data(), 4096) > 0) {}
                continue;
            }
            const int n = rings[(size_t) i].pop (scratch[(size_t) i].data(), 4096);
            done = juce::jmax (done, n);
            bool due = false;
            for (int s = 0; s < n; ++s)
                due = a.step (scratch[(size_t) i][(size_t) s]) || due;
            if (! due || knob (pFreeze) > 0.5f || knob (pFreezeA + i) > 0.5f)
                continue;

            a.analyse();   // the latest frame is all the display wants
            const juce::ScopedLock sl (snapLock);
            auto& s = snap[(size_t) i];
            s.numBins = a.binCount();
            std::copy (a.binData(), a.binData() + s.numBins, s.bins.begin());
            s.numEvents = a.eventCount();
            std::copy (a.eventData(), a.eventData() + s.numEvents, s.events.begin());
            s.binHz = a.binHz();
            s.fresh = true;
        }
        return done >= 4096 ? 0 : 12;
    }

    juce::SharedResourcePointer<AnalysisThread> thread;
    std::array<SampleRing, kSources> rings;
    std::array<Analyzer, kSources> analyzers;
    std::array<std::vector<float>, kSources> scratch;
    juce::CriticalSection snapLock;
    std::array<Snapshot, kSources> snap;
};

//==============================================================================
namespace
{
    class SpectrumCompareView final : public juce::Component, private juce::Timer
    {
    public:
        explicit SpectrumCompareView (SpectrumCompareModule& m) : module (&m)
        {
            setOpaque (true);
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff0b0d10));
            auto* m = mod();
            if (m != nullptr)   // (6.9.15) this view's colours, from its knobs
            {
                for (int k = 0; k < kSources; ++k)
                    kSourceColour[k] = aquanode::light::colourAt (m->knob (SpectrumCompareModule::pColA + k), kDefaultNm[k], kOriginalColour[k]);
                kDeltaColour = kSourceColour[2];
            }
            if (m == nullptr)
                return;
            const auto plot = plotArea().toFloat();
            const int view = (int) m->knob (SpectrumCompareModule::pView);
            const bool bars = m->knob (SpectrumCompareModule::pStyle) > 0.5f;
            const bool reassigned = m->knob (SpectrumCompareModule::pPrecision) > 0.5f;
            const bool delta = m->knob (SpectrumCompareModule::pDelta) > 0.5f;
            const bool showYellow = view == 0 || view == 2;
            const bool showDelta = delta && showYellow;
            const bool showSource[kSources] = { view == 0 || view == 1 || view == 3, view == 0 || view == 1 || view == 4,
                                                showYellow && ! delta };

            // the grid: frequencies, and the level every 12 dB (or the delta's every 6)
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            const bool labels = m->knob (SpectrumCompareModule::pLabels) > 0.5f;
            for (const float hz : gridFrequencies())
            {
                const float x = plot.getX() + xOf (hz);
                if (x < plot.getX() + 2.0f || x > plot.getRight() - 2.0f)
                    continue;
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                if (labels)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.5f));
                    g.drawText (hzText (hz), (int) x + 2, (int) plot.getBottom() - 12, 60, 11, juce::Justification::left);
                }
            }
            if (! (view == 2 && delta))
            {
                const float top = m->knob (SpectrumCompareModule::pTop);
                for (float db = std::floor (top / 12.0f) * 12.0f; db > top - range(); db -= 12.0f)
                {
                    const float y = plot.getY() + yOfDb (db);
                    g.setColour (juce::Colours::white.withAlpha (0.07f));
                    g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
                    if (labels)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.4f));
                        g.drawText (juce::String ((int) db) + " dB", (int) plot.getRight() - 46, (int) y, 44, 11, juce::Justification::right);
                    }
                }
            }
            if (showDelta)
            {
                g.setColour (kDeltaColour.withAlpha (0.25f));
                g.drawHorizontalLine ((int) (plot.getCentreY()), plot.getX(), plot.getRight());
            }

            // the sources
            for (int i = 0; i < kSources; ++i)
            {
                if (! showSource[i] || ! m->inputLive (i))
                    continue;
                const auto& col = shown[(size_t) i];
                if (bars)
                    drawBars (g, col, kSourceColour[i], plot);
                else if (reassigned)
                    drawStems (g, col, kSourceColour[i], plot);
                else
                    drawGraph (g, col, kSourceColour[i], plot);
                if (m->knob (SpectrumCompareModule::pHold) > 0.5f)
                    drawHold (g, peak[(size_t) i], kSourceColour[i], plot);   // the faint envelope
            }

            // Cyan minus Pink (Yellow's place, with Delta on)
            if (showDelta && m->inputLive (0) && m->inputLive (1))
                drawDelta (g, plot, bars);

            // legend
            {
                auto legend = getLocalBounds().removeFromTop (14).reduced (6, 0);
                const char* names[3] = { "A", "B", delta ? "A - B" : "C" };
                const juce::Colour cols[3] = { kSourceColour[0], kSourceColour[1], kDeltaColour };
                for (int k = 0; k < 3; ++k)
                {
                    auto cell = legend.removeFromLeft (k == 2 ? 84 : 56);
                    const bool live = k < 2 ? m->inputLive (k) : (delta ? (m->inputLive (0) && m->inputLive (1)) : m->inputLive (2));
                    g.setColour (cols[k].withAlpha (live ? 1.0f : 0.3f));
                    g.fillRect (cell.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 3.0f));
                    g.drawText (names[k], cell.withTrimmedLeft (3), juce::Justification::centredLeft);
                }
                {
                    juce::String frozen;
                    if (m->knob (SpectrumCompareModule::pFreeze) > 0.5f) frozen = "FROZEN";
                    else
                    {
                        const char* fn[3] = { "A", "B", delta ? "Delta" : "C" };
                        juce::StringArray f;
                        for (int k = 0; k < 3; ++k) if (m->knob (SpectrumCompareModule::pFreezeA + k) > 0.5f) f.add (fn[k]);
                        if (! f.isEmpty()) frozen = "FROZEN: " + f.joinIntoString (" ");
                    }
                    g.setColour (juce::Colours::white.withAlpha (0.6f));
                    g.drawText (frozen, legend, juce::Justification::centredRight);
                }
            }

            // the readout under the finger
            if (probe.has_value())
            {
                const float x = juce::jlimit (plot.getX(), plot.getRight() - 1.0f, probe->x);
                const int px = juce::jlimit (0, (int) shown[0].size() - 1, (int) (x - plot.getX()));
                const float hz = hzOf (x - plot.getX());
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                juce::String text = hzText (hz) + "  " + noteName (hz);
                auto level = [&] (int i) { return px < (int) shown[(size_t) i].size() ? shown[(size_t) i][(size_t) px] : kFloor; };
                if (m->inputLive (0)) text << "   C: " << dbText (level (0));
                if (m->inputLive (1)) text << "   P: " << dbText (level (1));
                if (! delta && m->inputLive (2)) text << "   Y: " << dbText (level (2));
                if (delta && m->inputLive (0) && m->inputLive (1) && level (0) > kFloor && level (1) > kFloor)
                    text << "   C-P: " << juce::String (level (0) - level (1), 1) << " dB";
                g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
                const int w = juce::jmin ((int) plot.getWidth() - 4, (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12);
                auto box = juce::Rectangle<int> (0, 0, w, 17).withCentre ({ (int) x, (int) plot.getY() + 12 })
                               .constrainedWithin (plot.toNearestInt());
                g.setColour (juce::Colour (0xcc000000));
                g.fillRoundedRectangle (box.toFloat(), 3.0f);
                g.setColour (juce::Colours::white);
                g.drawText (text, box, juce::Justification::centred);
            }
        }

        void resized() override { for (auto& s : shown) s.clear(); for (auto& p : peak) p.clear(); }
        void mouseDown (const juce::MouseEvent& e) override { probe = e.position; probeHideAt = 0; repaint(); }
        void mouseDrag (const juce::MouseEvent& e) override { probe = e.position; repaint(); }
        void mouseUp (const juce::MouseEvent&) override { probeHideAt = juce::Time::getMillisecondCounter() + 2000; }
        void mouseDoubleClick (const juce::MouseEvent&) override { for (auto& p : peak) std::fill (p.begin(), p.end(), kFloor); repaint(); }

    private:
        static constexpr float kFloor = -400.0f;

        SpectrumCompareModule* mod() const { return dynamic_cast<SpectrumCompareModule*> (module.get()); }
        juce::Rectangle<int> plotArea() const { return getLocalBounds().reduced (2).withTrimmedTop (14); }

        float lo() const { auto* m = mod(); return juce::jmax (1.0f, juce::jmin (m->knob (SpectrumCompareModule::pLow), m->knob (SpectrumCompareModule::pHigh))); }
        float hi() const { auto* m = mod(); return juce::jmax (lo() + 1.0f, juce::jmax (m->knob (SpectrumCompareModule::pLow), m->knob (SpectrumCompareModule::pHigh))); }
        bool logAxis() const { return mod()->knob (SpectrumCompareModule::pScale) < 0.5f; }
        float range() const { return mod()->knob (SpectrumCompareModule::pRange); }

        float xOf (float hz) const
        {
            const float w = (float) plotArea().getWidth() - 1.0f;
            const float t = logAxis() ? std::log (juce::jmax (1.0f, hz) / lo()) / std::log (hi() / lo()) : (hz - lo()) / (hi() - lo());
            return t * w;
        }
        float hzOf (float x) const
        {
            const float t = x / juce::jmax (1.0f, (float) plotArea().getWidth() - 1.0f);
            return logAxis() ? lo() * std::pow (hi() / lo(), t) : lo() + t * (hi() - lo());
        }
        float yOfDb (float db) const
        {
            const float top = mod()->knob (SpectrumCompareModule::pTop);
            return juce::jlimit (0.0f, 1.0f, (top - db) / range()) * ((float) plotArea().getHeight() - 1.0f);
        }
        float yOfDelta (float d) const
        {
            const float dr = mod()->knob (SpectrumCompareModule::pDeltaRange);
            const float h = (float) plotArea().getHeight() - 1.0f;
            return juce::jlimit (0.0f, h, h * 0.5f - d / dr * h * 0.5f);
        }

        static juce::String dbText (float db) { return db <= kFloor + 1.0f ? juce::String ("-") : juce::String (db, 1) + " dB"; }

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
                const float rough = (hi() - lo()) / 6.0f;
                const float mag = std::pow (10.0f, std::floor (std::log10 (rough)));
                const float step = rough / mag < 2.0f ? mag : rough / mag < 5.0f ? 2.0f * mag : 5.0f * mag;
                for (float x = std::ceil (lo() / step) * step; x <= hi(); x += step)
                    f.push_back (x);
            }
            return f;
        }

        // a spectrum in pixel columns: interpolated where the bins are wider
        // than a pixel, the loudest bin where a pixel holds several
        void toColumns (const SpectrumCompareModule::Snapshot& s, bool reassigned, std::vector<float>& out) const
        {
            const int w = plotArea().getWidth();
            out.assign ((size_t) w, kFloor);
            if (reassigned)
            {
                for (int n = 0; n < s.numEvents; ++n)
                {
                    const int x = juce::roundToInt (xOf (s.events[(size_t) n].hz));
                    if (x >= 0 && x < w)
                        out[(size_t) x] = juce::jmax (out[(size_t) x], s.events[(size_t) n].db);
                }
                return;
            }
            if (s.numBins < 2)
                return;
            for (int x = 0; x < w; ++x)
            {
                const double h0 = hzOf ((float) x - 0.5f), h1 = hzOf ((float) x + 0.5f), hc = hzOf ((float) x);
                const int k0 = (int) std::floor (h0 / s.binHz), k1 = (int) std::ceil (h1 / s.binHz);
                float v = kFloor;
                if (k1 - k0 <= 2)
                {
                    const double kf = hc / s.binHz;
                    const int a = juce::jlimit (0, s.numBins - 1, (int) std::floor (kf));
                    const int b = juce::jlimit (0, s.numBins - 1, a + 1);
                    const float f = (float) (kf - std::floor (kf));
                    v = s.bins[(size_t) a] + (s.bins[(size_t) b] - s.bins[(size_t) a]) * f;
                }
                else
                {
                    for (int k = juce::jmax (0, k0); k <= juce::jmin (s.numBins - 1, k1); ++k)
                        v = juce::jmax (v, s.bins[(size_t) k]);
                }
                out[(size_t) x] = v;
            }
        }

        void drawGraph (juce::Graphics& g, const std::vector<float>& col, juce::Colour c, juce::Rectangle<float> plot) const
        {
            juce::Path line, fill;
            bool open = false;
            const float bottom = plot.getBottom();
            float lastX = 0.0f;
            for (size_t x = 0; x < col.size(); ++x)
            {
                const float px = plot.getX() + (float) x;
                if (col[x] <= kFloor + 1.0f)
                {
                    if (open) { fill.lineTo (lastX, bottom); fill.closeSubPath(); open = false; }
                    continue;
                }
                const float y = plot.getY() + yOfDb (col[x]);
                if (! open) { line.startNewSubPath (px, y); fill.startNewSubPath (px, bottom); fill.lineTo (px, y); open = true; }
                else        { line.lineTo (px, y); fill.lineTo (px, y); }
                lastX = px;
            }
            if (open) { fill.lineTo (lastX, bottom); fill.closeSubPath(); }
            g.setColour (c.withAlpha (0.16f));
            g.fillPath (fill);
            g.setColour (c.withAlpha (0.95f));
            g.strokePath (line, juce::PathStrokeType (1.4f));
        }

        void drawBars (juce::Graphics& g, const std::vector<float>& col, juce::Colour c, juce::Rectangle<float> plot) const
        {
            // bars of 3 px, each the loudest of its columns
            const int bw = 3;
            for (size_t x0 = 0; x0 < col.size(); x0 += (size_t) bw)
            {
                float v = kFloor;
                for (size_t x = x0; x < juce::jmin (col.size(), x0 + (size_t) bw); ++x)
                    v = juce::jmax (v, col[x]);
                if (v <= kFloor + 1.0f)
                    continue;
                const float y = plot.getY() + yOfDb (v);
                g.setColour (c.withAlpha (0.55f));
                g.fillRect (plot.getX() + (float) x0, y, (float) bw - 1.0f, plot.getBottom() - y);
                g.setColour (c);
                g.fillRect (plot.getX() + (float) x0, y, (float) bw - 1.0f, 1.5f);
            }
        }

        void drawStems (juce::Graphics& g, const std::vector<float>& col, juce::Colour c, juce::Rectangle<float> plot) const
        {
            for (size_t x = 0; x < col.size(); ++x)
            {
                if (col[x] <= kFloor + 1.0f)
                    continue;
                const float y = plot.getY() + yOfDb (col[x]);
                g.setColour (c.withAlpha (0.4f));
                g.fillRect (plot.getX() + (float) x - 0.5f, y, 1.5f, plot.getBottom() - y);
                g.setColour (c);
                g.fillRect (plot.getX() + (float) x - 1.0f, y - 0.5f, 2.5f, 2.0f);
            }
        }

        void drawHold (juce::Graphics& g, const std::vector<float>& col, juce::Colour c, juce::Rectangle<float> plot) const
        {
            juce::Path p;
            bool open = false;
            for (size_t x = 0; x < col.size(); ++x)
            {
                if (col[x] <= kFloor + 1.0f) { open = false; continue; }
                const float px = plot.getX() + (float) x, y = plot.getY() + yOfDb (col[x]);
                if (! open) { p.startNewSubPath (px, y); open = true; } else p.lineTo (px, y);
            }
            g.setColour (c.withAlpha (0.45f));
            g.strokePath (p, juce::PathStrokeType (0.8f));
        }

        void drawDelta (juce::Graphics& g, juce::Rectangle<float> plot, bool bars) const
        {
            // (6.9.15) Freeze C/Delta: the difference as it was
            const bool held = mod()->knob (SpectrumCompareModule::pFreezeC) > 0.5f && ! heldA.empty();
            const auto& a = held ? heldA : shown[0];
            const auto& b = held ? heldB : shown[1];
            const float mid = plot.getY() + yOfDelta (0.0f);
            // only where both are in the window shown (below it a difference means little)
            const float floorDb = mod()->knob (SpectrumCompareModule::pTop) - range();
            auto both = [&] (size_t x) { return a[x] > floorDb && b[x] > floorDb; };
            const size_t n = juce::jmin (a.size(), b.size());
            if (bars)
            {
                for (size_t x = 0; x < n; ++x)
                {
                    if (! both (x))
                        continue;
                    const float y = plot.getY() + yOfDelta (a[x] - b[x]);
                    g.setColour ((a[x] > b[x] ? kSourceColour[0] : kSourceColour[1]).interpolatedWith (kDeltaColour, 0.5f).withAlpha (0.22f));
                    g.fillRect (plot.getX() + (float) x, juce::jmin (y, mid), 1.0f, std::abs (y - mid));
                    g.setColour (kDeltaColour);
                    g.fillRect (plot.getX() + (float) x, y - 0.5f, 1.0f, 1.5f);
                }
                return;
            }
            juce::Path line;
            bool open = false;
            for (size_t x = 0; x < n; ++x)
            {
                if (! both (x)) { open = false; continue; }
                const float px = plot.getX() + (float) x, y = plot.getY() + yOfDelta (a[x] - b[x]);
                g.setColour ((a[x] > b[x] ? kSourceColour[0] : kSourceColour[1]).withAlpha (0.07f));
                g.drawVerticalLine ((int) px, juce::jmin (y, mid), juce::jmax (y, mid));
                if (! open) { line.startNewSubPath (px, y); open = true; } else line.lineTo (px, y);
            }
            g.setColour (kDeltaColour);
            g.strokePath (line, juce::PathStrokeType (1.5f));
        }

        void timerCallback() override
        {
            auto* m = mod();
            if (m == nullptr)
                return;
            if (probe.has_value() && probeHideAt != 0 && juce::Time::getMillisecondCounter() > probeHideAt)
                probe.reset();

            const bool reassigned = m->knob (SpectrumCompareModule::pPrecision) > 0.5f;
            const float smooth = kSpeedSmooth[juce::jlimit (0, 3, (int) std::lround (m->knob (SpectrumCompareModule::pSpeed)))];
            const int envelope = juce::jlimit (0, 2, (int) std::lround (m->knob (SpectrumCompareModule::pHold)));
            const auto key = std::make_tuple (lo(), hi(), logAxis(), reassigned, plotArea().getWidth());
            if (key != axisKey)
            {
                axisKey = key;
                for (auto& s : shown) s.clear();
                for (auto& p : peak) p.clear();
            }

            bool changed = false;
            for (int i = 0; i < kSources; ++i)
            {
                if (! m->takeSnapshot (i, snapshot))
                    continue;
                toColumns (snapshot, reassigned, fresh);
                auto& s = shown[(size_t) i];
                auto& p = peak[(size_t) i];
                if (s.size() != fresh.size()) s.assign (fresh.size(), kFloor);
                if (p.size() != fresh.size()) p.assign (fresh.size(), kFloor);
                const float floorDb = m->knob (SpectrumCompareModule::pTop) - range() - 12.0f;
                for (size_t x = 0; x < fresh.size(); ++x)
                {
                    // rises at once, falls back by Smooth; reassigned points fade where nothing new lands
                    const float v = fresh[x];
                    float& d = s[x];
                    if (v >= d)
                        d = v;
                    else
                        d = (d <= kFloor + 1.0f) ? v : d * smooth + juce::jmax (v, floorDb) * (1.0f - smooth);
                    if (d < floorDb)
                        d = kFloor;
                    // the envelope: the highest so far - falling 9 dB a second (Slow), or kept (Hold)
                    if (envelope == 1 && p[x] > kFloor + 1.0f)
                        p[x] = p[x] - 0.3f < floorDb ? kFloor : p[x] - 0.3f;
                    p[x] = juce::jmax (p[x], d);
                }
                changed = true;
            }
            if (m->knob (SpectrumCompareModule::pFreezeC) < 0.5f)   // (6.9.15) the delta's own freeze
            {
                heldA = shown[0];
                heldB = shown[1];
            }
            if (changed || probe.has_value())
                repaint();
        }
        std::vector<float> heldA, heldB;

        juce::WeakReference<SynthModule> module;
        std::array<std::vector<float>, kSources> shown, peak;
        std::vector<float> fresh;
        SpectrumCompareModule::Snapshot snapshot;
        std::tuple<float, float, bool, bool, int> axisKey {};
        std::optional<juce::Point<float>> probe;
        juce::uint32 probeHideAt { 0 };
    };
}

std::unique_ptr<juce::Component> SpectrumCompareModule::createExtraContentComponent()
{
    return std::make_unique<SpectrumCompareView> (*this);
}

AQUANODE_REGISTER_MODULE (SpectrumCompareModule, SpectrumCompareModule::descriptor)
