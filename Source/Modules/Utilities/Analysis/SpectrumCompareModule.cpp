//==============================================================================
// Spectrum Compare - SpectralCompare's periodogram as a module
//
// Two sounds' spectra over each other (x frequency, y level): source 1 in
// green, source 2 in purple, and their difference (1 minus 2, in dB) - or
// that difference alone - in yellow around a centre line, so you see where
// one is louder than the other. As a connected graph or as bars; and in
// Reassigned precision every partial is drawn as a stem at the frequency
// spectral reassignment finds for it (not the FFT bin's centre), so two
// close tones that one bin would blur show up apart.
//
// The FFT runs on a background thread. Smooth lets the curves fall back
// slowly (they rise at once); Hold keeps each one's peaks. A tap (or a held
// finger) reads out the frequency, the note and both levels there. Thru
// passes source 1 on unchanged.
//==============================================================================
#include "SpectralAnalysis.h"

using namespace aquanode;
using namespace aquanode::spectral;

namespace
{
    constexpr int kSources = 2;
    const juce::Colour kSourceColour[kSources] = { juce::Colour (0xff3ee07a), juce::Colour (0xffb070ff) };
    const juce::Colour kDeltaColour (0xffffd23f);

    const juce::StringArray& fftChoices() { static const juce::StringArray c { "1024", "2048", "4096", "8192", "16384" }; return c; }
}

class SpectrumCompareModule final : public SynthModule,
                                    private juce::TimeSliceClient
{
public:
    enum P { pView, pStyle, pPrecision, pLow, pHigh, pTop, pRange, pDeltaRange, pSmooth, pFft, pWindow, pScale, pLabels,
             pHold, pFreeze };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.spectrumcompare";
        d.displayName = "Spectrum Compare";
        d.description = "SpectralCompare's periodogram (frequency across, level up): source 1 in green, source 2 in "
                        "purple and their difference (1 minus 2) in yellow - or the difference alone - as a graph or as "
                        "bars. Reassigned draws every partial at the frequency spectral reassignment finds for it, "
                        "finer than the FFT's bins. Smooth, Hold, any range; a tap reads out the frequency, note and "
                        "both levels. Thru passes source 1 on.";
        d.section = ModuleSection::Utility;
        d.sidebarOrder = 41;
        d.sockets = { audioIn ("in1", "Source 1"), audioIn ("in2", "Source 2"), audioOut ("thru", "Thru") };
        d.params = {
            makeCombo ("view", "View", { "1 + 2 + Delta", "1 + 2", "Delta Only", "1 Only", "2 Only" }, 0, 0, 2).noMod(),
            makeCombo ("style", "Style", { "Graph", "Bars" }, 0, 0, 1).noMod(),
            makeCombo ("precision", "Precision", { "FFT", "Reassigned" }, 0, 0, 2).noMod(),

            makeRotary ("low", "Low", 20.0f, 20000.0f, 20.0f, 1, "Hz", true),
            makeRotary ("high", "High", 20.0f, 22000.0f, 20000.0f, 1, "Hz", true),
            makeRotary ("top", "Top", -60.0f, 12.0f, 0.0f, 1, "dB"),
            makeRotary ("range", "Range", 24.0f, 140.0f, 90.0f, 1, "dB"),
            makeRotary ("deltaRange", "Delta +/-", 3.0f, 60.0f, 24.0f, 1, "dB"),

            makeRotary ("smooth", "Smooth", 0.0f, 0.99f, 0.8f, 2),
            makeCombo ("fft", "FFT", fftChoices(), 3, 2, 1).noMod(),
            makeCombo ("window", "Window", { "Hann", "Blackman-Harris", "Nuttall", "Kaiser" }, 1, 2, 2).noMod(),
            makeCombo ("scale", "Axis", { "Log", "Linear" }, 0, 2, 1).noMod(),

            makeCombo ("labels", "Labels", { "Off", "On" }, 1, 3, 1).noMod(),
            makeCombo ("hold", "Hold", { "Off", "On" }, 0, 3, 1).noMod(),
            makeCombo ("freeze", "Freeze", { "Run", "Freeze" }, 0, 3, 1).noMod()
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
            if (! due || knob (pFreeze) > 0.5f)
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
            if (m == nullptr)
                return;
            const auto plot = plotArea().toFloat();
            const int view = (int) m->knob (SpectrumCompareModule::pView);
            const bool bars = m->knob (SpectrumCompareModule::pStyle) > 0.5f;
            const bool reassigned = m->knob (SpectrumCompareModule::pPrecision) > 0.5f;
            const bool showDelta = view == 0 || view == 2;
            const bool show1 = view == 0 || view == 1 || view == 3;
            const bool show2 = view == 0 || view == 1 || view == 4;

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
            if (view != 2)
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
                if ((i == 0 && ! show1) || (i == 1 && ! show2) || ! m->inputLive (i))
                    continue;
                const auto& col = shown[(size_t) i];
                if (reassigned)
                    drawStems (g, col, kSourceColour[i], plot);
                else if (bars)
                    drawBars (g, col, kSourceColour[i], plot);
                else
                    drawGraph (g, col, kSourceColour[i], plot);
                if (m->knob (SpectrumCompareModule::pHold) > 0.5f)
                    drawHold (g, peak[(size_t) i], kSourceColour[i], plot);
            }

            // 1 minus 2
            if (showDelta && m->inputLive (0) && m->inputLive (1))
                drawDelta (g, plot, bars);

            // legend
            {
                auto legend = getLocalBounds().removeFromTop (14).reduced (6, 0);
                const char* names[3] = { "Source 1", "Source 2", "1 - 2" };
                const juce::Colour cols[3] = { kSourceColour[0], kSourceColour[1], kDeltaColour };
                for (int k = 0; k < 3; ++k)
                {
                    auto cell = legend.removeFromLeft (70);
                    const bool live = k < 2 ? m->inputLive (k) : (m->inputLive (0) && m->inputLive (1));
                    g.setColour (cols[k].withAlpha (live ? 1.0f : 0.3f));
                    g.fillRect (cell.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 3.0f));
                    g.drawText (names[k], cell.withTrimmedLeft (3), juce::Justification::centredLeft);
                }
                if (m->knob (SpectrumCompareModule::pFreeze) > 0.5f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.6f));
                    g.drawText ("FROZEN", legend, juce::Justification::centredRight);
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
                if (m->inputLive (0)) text << "   1: " << dbText (level (0));
                if (m->inputLive (1)) text << "   2: " << dbText (level (1));
                if (m->inputLive (0) && m->inputLive (1) && level (0) > kFloor && level (1) > kFloor)
                    text << "   1-2: " << juce::String (level (0) - level (1), 1) << " dB";
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
            const auto& a = shown[0];
            const auto& b = shown[1];
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
            const float smooth = juce::jlimit (0.0f, 0.99f, m->knob (SpectrumCompareModule::pSmooth));
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
                    p[x] = juce::jmax (p[x], d);
                }
                changed = true;
            }
            if (changed || probe.has_value())
                repaint();
        }

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
