#include "InspectorModule.h"

using namespace aquanode;

namespace
{
    int orderOf (int size) { int o = 0; while ((1 << o) < size) ++o; return o; }

    // the test signal's spectrum: every bin at the same level, the phase a
    // quadratic (a chirp), no DC and nothing at Nyquist
    std::vector<std::complex<float>> chirpSpectrum (int n)
    {
        std::vector<std::complex<float>> X ((size_t) n);
        for (int k = 1; k < n / 2; ++k)
        {
            const double ph = -juce::MathConstants<double>::pi * (double) k * (double) k / (double) n;
            X[(size_t) k] = { (float) std::cos (ph), (float) std::sin (ph) };
            X[(size_t) (n - k)] = std::conj (X[(size_t) k]);
        }
        return X;
    }
}

InspectorModule::LinkState& InspectorModule::link (int i)
{
    static LinkState links[kNumLinks];
    return links[juce::jlimit (0, kNumLinks - 1, i)];
}

const std::vector<float>& InspectorModule::testSignal (int size)
{
    static const std::array<std::vector<float>, 4> table = []
    {
        std::array<std::vector<float>, 4> t;
        for (int c = 0; c < 4; ++c)
        {
            const int n = sizeForChoice (c);
            auto X = chirpSpectrum (n);
            std::vector<std::complex<float>> x ((size_t) n);
            juce::dsp::FFT fft (orderOf (n));
            fft.perform (X.data(), x.data(), true);
            float pk = 1.0e-9f;
            for (auto& v : x) pk = juce::jmax (pk, std::abs (v.real()));
            t[(size_t) c].resize ((size_t) n);
            for (int i = 0; i < n; ++i)
                t[(size_t) c][(size_t) i] = x[(size_t) i].real() / pk;
        }
        return t;
    }();
    return table[(size_t) juce::jlimit (0, 3, orderOf (size) - 13)];
}

void InspectorModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    testSignal (8192);   // builds every size's signal here, never on the audio thread
    ring.assign ((size_t) kMaxSize, 0.0f);
    accum.assign ((size_t) kMaxSize, 0.0f);
    {
        const juce::SpinLock::ScopedLockType sl (publishLock);
        published.assign ((size_t) kMaxSize, 0.0f);
    }
    reset();
}

void InspectorModule::reset()
{
    genPos = 0;
    genSize = 0;
    lastPos = -1;
    anSize = 0;
    periodsSeen = averaged = 0;
    quietSamples = 0;
}

bool InspectorModule::generatorAlive() const { return alive.load (std::memory_order_relaxed); }

void InspectorModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    auto& L = link ((int) std::lround (param (pLink)));

    if ((int) std::lround (param (pMode)) == 0)
    {
        // ---- Generator
        const int size = sizeForChoice ((int) std::lround (param (pSize)));
        if (size != genSize)
        {
            genSize = size;
            genPos = 0;
        }
        const float y = juce::Decibels::decibelsToGain (param (pLevel)) * testSignal (size)[(size_t) genPos];
        L.size.store (size, std::memory_order_relaxed);
        L.pos.store (genPos, std::memory_order_relaxed);
        L.beat.fetch_add (1, std::memory_order_relaxed);
        genPos = (genPos + 1) % size;
        outputs[0] = { y, y };
        return;
    }

    // ---- Analyzer: passes its input on, and files it under the Generator's position
    outputs[0] = inputs[0];
    if (ring.empty())
        return;

    const juce::uint32 beat = L.beat.load (std::memory_order_relaxed);
    if (beat == lastBeat)
    {
        if (++quietSamples > (int) (0.25 * sampleRate))
            alive.store (false, std::memory_order_relaxed);
        return;
    }
    lastBeat = beat;
    quietSamples = 0;
    alive.store (true, std::memory_order_relaxed);

    const int size = L.size.load (std::memory_order_relaxed);
    const int pos = L.pos.load (std::memory_order_relaxed);
    if (size != anSize)
    {
        anSize = size;
        lastPos = -1;
        periodsSeen = averaged = 0;
    }
    if (size <= 0 || size > kMaxSize || pos < 0 || pos >= size)
        return;

    ring[(size_t) pos] = 0.5f * (inputs[0][0] + inputs[0][1]);

    if (lastPos >= 0 && pos < lastPos)
    {
        // a whole period is in: the first is the module's start-up, the rest are averaged
        if (++periodsSeen >= 2)
        {
            const int maxAvg = juce::jlimit (1, 32, (int) std::lround (param (pAverage)));
            averaged = juce::jmin (averaged + 1, maxAvg);
            const float w = 1.0f / (float) averaged;
            for (int i = 0; i < size; ++i)
                accum[(size_t) i] += w * (ring[(size_t) i] - accum[(size_t) i]);

            const juce::SpinLock::ScopedTryLockType sl (publishLock);
            if (sl.isLocked() && (int) published.size() >= size)
            {
                std::copy (accum.begin(), accum.begin() + size, published.begin());
                publishedSize = size;
                publishedVersion.fetch_add (1, std::memory_order_release);
            }
        }
    }
    lastPos = pos;
}

bool InspectorModule::fetchResponse (std::vector<float>& dest, int& size, juce::uint32& version) const
{
    const auto v = publishedVersion.load (std::memory_order_acquire);
    if (v == version)
        return false;
    const juce::SpinLock::ScopedLockType sl (publishLock);
    size = publishedSize;
    if (size <= 0)
        return false;
    dest.assign (published.begin(), published.begin() + size);
    version = v;
    return true;
}

void InspectorModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "auto")
        setParameter ("latency", (float) getMeasuredLatency());
}

//==============================================================================
// the response view: magnitude (dB) and phase (degrees) over a log frequency
// axis, or the impulse response
namespace
{
    class InspectorView final : public juce::Component, private juce::Timer
    {
    public:
        explicit InspectorView (InspectorModule& m) : module (&m) { startTimerHz (8); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff101418));
            g.fillRoundedRectangle (b, 4.0f);
            auto* m = dynamic_cast<InspectorModule*> (module.get());
            if (m == nullptr)
                return;

            const auto font = juce::Font (juce::FontOptions (12.0f));
            g.setFont (font);
            const char* linkName[] = { "A", "B", "C", "D" };
            const auto linkStr = juce::String ("Link ") + linkName[juce::jlimit (0, 3, (int) std::lround (m->getParameter ("link")))];

            if (m->isGenerator())
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawFittedText ("Generator on " + linkStr + ": a flat-spectrum chirp repeating every "
                                  + juce::String (InspectorModule::sizeForChoice ((int) std::lround (m->getParameter ("size"))))
                                  + " samples.\n\nPatch it into the module you want to measure, and that module into an "
                                    "Inspector set to Analyzer on the same Link.",
                                  getLocalBounds().reduced (14), juce::Justification::centred, 6);
                return;
            }

            if (! m->generatorAlive() || points.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawFittedText (m->generatorAlive() ? "Measuring..." : "Analyzer on " + linkStr + ": waiting for a Generator on the same Link.",
                                  getLocalBounds().reduced (14), juce::Justification::centred, 4);
                return;
            }

            const int view = (int) std::lround (m->getParameter ("view"));
            auto plot = b.reduced (34.0f, 12.0f).withTrimmedBottom (14.0f);
            const double nyq = m->getSampleRate() * 0.5;
            auto xOf = [&] (double f) { return plot.getX() + (float) (std::log (f / 10.0) / std::log (nyq / 10.0)) * plot.getWidth(); };

            if (view == 3)
                paintImpulse (g, plot);
            else
            {
                // grid: decades and the dB scale
                g.setColour (juce::Colours::white.withAlpha (0.10f));
                for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
                    if (f < nyq)
                    {
                        const float x = xOf (f);
                        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                        g.setColour (juce::Colours::white.withAlpha (0.45f));
                        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                                    juce::Rectangle<float> (x - 15.0f, plot.getBottom() + 1.0f, 30.0f, 12.0f), juce::Justification::centred);
                        g.setColour (juce::Colours::white.withAlpha (0.10f));
                    }

                float lo = 1.0e9f, hi = -1.0e9f;
                for (auto& p : points) { lo = juce::jmin (lo, p.db); hi = juce::jmax (hi, p.db); }
                const float mid = 0.5f * (lo + hi);
                float span = juce::jmax (12.0f, (hi - lo) * 1.15f);
                span = std::ceil (span / 6.0f) * 6.0f;
                const float top = std::ceil ((mid + span * 0.5f) / 3.0f) * 3.0f, bottom = top - span;
                auto yDb = [&] (float d) { return juce::jmap (d, bottom, top, plot.getBottom(), plot.getY()); };
                auto yPh = [&] (float deg) { return juce::jmap (deg, -180.0f, 180.0f, plot.getBottom(), plot.getY()); };

                const float step = span <= 24.0f ? 3.0f : (span <= 48.0f ? 6.0f : 12.0f);
                for (float d = std::ceil (bottom / step) * step; d <= top + 0.01f; d += step)
                {
                    g.setColour (juce::Colours::white.withAlpha (std::abs (d) < 0.01f ? 0.28f : 0.10f));
                    g.drawHorizontalLine ((int) yDb (d), plot.getX(), plot.getRight());
                    if (view != 2)
                    {
                        g.setColour (juce::Colour (0xff7fd4ff).withAlpha (0.8f));
                        g.drawText (juce::String ((int) std::lround (d)), juce::Rectangle<float> (2.0f, yDb (d) - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
                    }
                }
                if (view != 1)
                {
                    g.setColour (juce::Colour (0xffffb35c).withAlpha (0.8f));
                    for (float deg : { -180.0f, -90.0f, 0.0f, 90.0f, 180.0f })
                        g.drawText (juce::String ((int) deg), juce::Rectangle<float> (plot.getRight() + 2.0f, yPh (deg) - 6.0f, 32.0f, 12.0f), juce::Justification::centredLeft);
                }

                if (view != 1)
                {
                    juce::Path ph;
                    bool started = false;
                    float lastDeg = 0.0f;
                    for (auto& p : points)
                    {
                        const float x = xOf (p.freq);
                        if (! started || std::abs (p.phase - lastDeg) > 180.0f)
                            ph.startNewSubPath (x, yPh (p.phase));   // break the line where it wraps
                        else
                            ph.lineTo (x, yPh (p.phase));
                        started = true;
                        lastDeg = p.phase;
                    }
                    g.setColour (juce::Colour (0xffffb35c).withAlpha (0.85f));
                    g.strokePath (ph, juce::PathStrokeType (1.3f));
                }
                if (view != 2)
                {
                    juce::Path mag;
                    for (size_t i = 0; i < points.size(); ++i)
                    {
                        const float x = xOf (points[i].freq), y = yDb (points[i].db);
                        if (i == 0) mag.startNewSubPath (x, y); else mag.lineTo (x, y);
                    }
                    g.setColour (juce::Colour (0xff7fd4ff));
                    g.strokePath (mag, juce::PathStrokeType (1.8f));
                }
            }

            g.setColour (juce::Colours::white.withAlpha (0.85f));
            const int lat = m->getMeasuredLatency();
            g.drawText ("measured latency " + juce::String (lat) + " smp (" + juce::String (lat * 1000.0 / m->getSampleRate(), 2)
                        + " ms)   compensated " + juce::String ((int) std::lround (m->getParameter ("latency"))),
                        b.reduced (36.0f, 1.0f).removeFromTop (12.0f), juce::Justification::centredLeft);
        }

    private:
        struct Point { double freq; float db, phase; };

        void paintImpulse (juce::Graphics& g, juce::Rectangle<float> plot)
        {
            if (impulse.empty())
                return;
            // the first 1/8 of the period around the peak
            const int n = (int) impulse.size();
            const int show = juce::jmax (64, n / 8);
            float pk = 1.0e-9f;
            for (int i = 0; i < show; ++i) pk = juce::jmax (pk, std::abs (impulse[(size_t) i]));
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
            juce::Path p;
            for (int i = 0; i < show; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) (show - 1);
                const float y = plot.getCentreY() - impulse[(size_t) i] / pk * plot.getHeight() * 0.48f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (juce::Colour (0xff9dffb0));
            g.strokePath (p, juce::PathStrokeType (1.2f));
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText ("impulse response, first " + juce::String (show) + " samples", plot.removeFromBottom (14.0f), juce::Justification::centredRight);
        }

        void timerCallback() override
        {
            auto* m = dynamic_cast<InspectorModule*> (module.get());
            if (m == nullptr)
                return;
            int size = 0;
            const bool fresh = m->fetchResponse (response, size, version);
            const float lat = m->getParameter ("latency");
            const float smooth = m->getParameter ("smooth");
            if (fresh || lat != lastLatency || smooth != lastSmooth)
            {
                if (! response.empty())
                    analyse ((int) response.size(), *m);
                lastLatency = lat;
                lastSmooth = smooth;
            }
            repaint();
        }

        void analyse (int n, InspectorModule& m)
        {
            if (n != specSize)
            {
                specSize = n;
                X = chirpSpectrum (n);
                fft = std::make_unique<juce::dsp::FFT> (orderOf (n));
            }
            std::vector<std::complex<float>> y ((size_t) n), Y ((size_t) n), H ((size_t) n), h ((size_t) n);
            for (int i = 0; i < n; ++i)
                y[(size_t) i] = { response[(size_t) i], 0.0f };
            fft->perform (y.data(), Y.data(), false);
            for (int k = 1; k < n; ++k)
                if (k != n / 2)
                    H[(size_t) k] = Y[(size_t) k] / X[(size_t) k];   // |X| = 1
            fft->perform (H.data(), h.data(), true);

            // the latency: where the impulse response peaks (in the first half of the period)
            int peakAt = 0;
            float pk = -1.0f;
            for (int i = 0; i < n / 2; ++i)
                if (std::abs (h[(size_t) i].real()) > pk) { pk = std::abs (h[(size_t) i].real()); peakAt = i; }
            m.noteMeasuredLatency (peakAt);
            const int lat = (int) std::lround (m.getParameter ("latency"));
            impulse.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
                impulse[(size_t) i] = h[(size_t) ((i + lat) % n)].real() / (float) n;

            // points on a log axis, each the average over its fractional-octave band
            const double sr = m.getSampleRate(), nyq = sr * 0.5;
            const int smoothChoice = (int) std::lround (m.getParameter ("smooth"));
            const double bandOct = smoothChoice == 0 ? 0.0 : (smoothChoice == 1 ? 1.0 / 24.0 : (smoothChoice == 2 ? 1.0 / 12.0 : 1.0 / 3.0));
            const int numPoints = 320;
            points.clear();
            for (int p = 0; p < numPoints; ++p)
            {
                const double f = 10.0 * std::pow (nyq * 0.98 / 10.0, (double) p / (numPoints - 1));
                const double half = std::pow (2.0, bandOct * 0.5);
                int k0 = (int) std::floor (f / half * n / sr), k1 = (int) std::ceil (f * half * n / sr);
                k0 = juce::jlimit (1, n / 2 - 1, k0);
                k1 = juce::jlimit (k0, n / 2 - 1, k1);
                double pw = 0.0;
                std::complex<double> c (0.0, 0.0);
                for (int k = k0; k <= k1; ++k)
                {
                    const auto hk = std::complex<double> (H[(size_t) k]);
                    pw += std::norm (hk);
                    // the phase without the latency's linear part
                    const double a = juce::MathConstants<double>::twoPi * (double) k * lat / (double) n;
                    c += hk * std::complex<double> (std::cos (a), std::sin (a));
                }
                const int cnt = k1 - k0 + 1;
                points.push_back ({ f, (float) (10.0 * std::log10 (juce::jmax (pw / cnt, 1.0e-20))),
                                    (float) juce::radiansToDegrees (std::arg (c)) });
            }
        }

        juce::WeakReference<SynthModule> module;
        std::vector<float> response, impulse;
        std::vector<Point> points;
        juce::uint32 version { 0 };
        float lastLatency { -1.0f }, lastSmooth { -1.0f };
        int specSize { 0 };
        std::vector<std::complex<float>> X;
        std::unique_ptr<juce::dsp::FFT> fft;
    };
}

std::unique_ptr<juce::Component> InspectorModule::createExtraContentComponent()
{
    return std::make_unique<InspectorView> (*this);
}

static ModuleDescriptor inspectorDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.inspector";
    d.displayName = "Inspector";
    d.description =
        "Measures any module's frequency response, magnitude and phase. Put one Inspector set to Generator in "
        "front of the module and a second set to Analyzer after it, on the same Link: the Generator plays a "
        "flat-spectrum test chirp, the Analyzer lines up what comes back and shows the curve. Auto finds the "
        "module's latency and takes it out of the phase; Average smooths over several passes.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 90;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("mode",    "Mode",    { "Generator", "Analyzer" }, 0, 0, 2).noMod(),
        makeCombo  ("link",    "Link",    { "A", "B", "C", "D" }, 0, 0, 1).noMod(),
        makeCombo  ("size",    "Size",    { "8192", "16384", "32768", "65536" }, 2, 0, 2).noMod(),
        makeRotary ("level",   "Level",   -48.0f, 0.0f, -12.0f, 1, "dB").visibleWhen ("mode", 0.0f),
        makeRotary ("latency", "Latency", 0.0f, 32768.0f, 0.0f, 1, "smp", false, 1.0f).noMod().visibleWhen ("mode", 1.0f),
        makeButton ("auto",    "Auto",    1, 1).visibleWhen ("mode", 1.0f),
        makeRotary ("average", "Average", 1.0f, 16.0f, 4.0f, 1, {}, false, 1.0f).noMod().visibleWhen ("mode", 1.0f),
        makeCombo  ("smooth",  "Smooth",  { "Off", "1/24 oct", "1/12 oct", "1/3 oct" }, 2, 2, 2).noMod().visibleWhen ("mode", 1.0f),
        makeCombo  ("view",    "View",    { "Mag + Phase", "Magnitude", "Phase", "Impulse" }, 0, 2, 2).noMod().visibleWhen ("mode", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (InspectorModule, inspectorDescriptor)
