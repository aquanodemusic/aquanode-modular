#pragma once

// A live graph of what plays: what goes in and what comes out, on one log-
// frequency axis. (Filter cards show FilterCurve instead since 5.19.4: the
// static curve their knobs set. Response mode - out over in, measured while
// playing - is kept for any later use.)
//
// Two more ways to show it (the same reading, drawn otherwise):
//   Spectrum     what comes out (filled) over what goes in (a line), on an
//                absolute dBFS scale - Clip Preserve's graph: the folds' new
//                harmonics stand out
//   Periodogram  what goes in, filled, on an absolute dBFS scale, with a peak
//                hold line if asked - the EQ Curve Viewer module
//
// The engine copies the module's first input and output into its tap; this
// reads the newest 4096 samples twenty times a second.

#include <JuceHeader.h>
#include "../Modules/Core/ModuleCore.h"

class FilterGraph : public juce::Component,
                    private juce::Timer
{
public:
    static constexpr int kHeight = 104;

    enum class Mode { Response, Spectrum, Periodogram };
    struct View
    {
        float rangeDb = 96.0f;     // the absolute scales: from +6 dBFS down this far
        float smoothing = 0.35f;   // how much of each new reading goes in (1 = none kept)
        bool peakHold = false;
        int fftSize = 4096;        // 512 .. 8192 (the tap keeps 8192)
        bool reassign = false;     // each bin's energy moved to the frequency it really measured
    };

    FilterGraph (aquanode::SynthModule& m, std::function<juce::Colour()> accentOf,
                 Mode modeToUse = Mode::Response, std::function<View()> viewOf = {})
        : module (&m), accent (std::move (accentOf)), mode (modeToUse), view (std::move (viewOf))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const bool glassOn = aquanode::themeColours().glass;
        g.setColour (glassOn ? juce::Colours::black.withAlpha (0.22f) : juce::Colour (0xff141210).withAlpha (0.82f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (glassOn ? 0.2f : 0.14f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

        auto plot = area.reduced (6.0f, 5.0f).withTrimmedBottom (9.0f);
        const auto c = accent ? accent() : juce::Colour (0xff74cc8a);

        // the grid: 100 Hz, 1 kHz, 10 kHz; the 0 dB line of the response
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        for (const auto& [hz, label] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
        {
            const float x = xOf (hz, plot);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.38f));
            g.drawText (label, juce::Rectangle<float> (x - 14.0f, plot.getBottom() + 1.0f, 28.0f, 9.0f), juce::Justification::centred);
        }
        if (mode == Mode::Response)
        {
            const float zeroY = yOfResponse (0.0f, plot);
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.fillRect (plot.getX(), zeroY, plot.getWidth(), 1.0f);
        }
        else
        {
            // the dBFS lines, every 24 dB (12 on a short range)
            const float step = currentView().rangeDb <= 48.0f ? 12.0f : 24.0f;
            for (float db = 0.0f; db > 6.0f - currentView().rangeDb; db -= step)
            {
                const float y = yOfSpectrum (db, plot);
                g.setColour (juce::Colours::white.withAlpha (db == 0.0f ? 0.18f : 0.08f));
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.38f));
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 1.0f, y + 1.0f, 26.0f, 9.0f),
                            juce::Justification::topLeft);
            }
        }

        if (! hasSignal)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawText (mode == Mode::Periodogram ? "Waiting for sound" : "Play something through it", plot, juce::Justification::centred);
            return;
        }

        const int w = (int) inDb.size();
        auto pathOf = [&] (const std::vector<float>& db, bool closed)
        {
            juce::Path p;
            for (int i = 0; i < w; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) (w - 1);
                const float y = yOfSpectrum (db[(size_t) i], plot);
                if (i == 0) p.startNewSubPath (x, closed ? plot.getBottom() : y);
                if (i == 0 && closed) p.lineTo (x, y);
                else p.lineTo (x, y);
            }
            if (closed)
            {
                p.lineTo (plot.getRight(), plot.getBottom());
                p.closeSubPath();
            }
            return p;
        };

        if (mode == Mode::Periodogram)
        {
            // what goes in, filled, its outline brighter; the held peaks a fine line
            g.setColour (c.withAlpha (0.32f));
            g.fillPath (pathOf (inDb, true));
            g.setColour (c.brighter (0.5f).withAlpha (0.95f));
            g.strokePath (pathOf (inDb, false), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (currentView().peakHold && ! peakDbHold.empty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.strokePath (pathOf (peakDbHold, false), juce::PathStrokeType (0.9f));
            }
            return;
        }

        // what comes out, filled; what goes in, a faint line
        g.setColour (c.withAlpha (0.30f));
        g.fillPath (pathOf (outDb, true));
        g.setColour (juce::Colours::white.withAlpha (0.32f));
        g.strokePath (pathOf (inDb, false), juce::PathStrokeType (1.0f));
        if (mode == Mode::Spectrum)
        {
            g.setColour (c.brighter (0.5f).withAlpha (0.9f));
            g.strokePath (pathOf (outDb, false), juce::PathStrokeType (1.3f));
            return;
        }

        // the response (out over in), where there was input to measure it by
        juce::Path resp;
        bool drawing = false;
        for (int i = 0; i < w; ++i)
        {
            if (! valid[(size_t) i]) { drawing = false; continue; }
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) (w - 1);
            const float y = yOfResponse (respDb[(size_t) i], plot);
            if (! drawing) { resp.startNewSubPath (x, y); drawing = true; }
            else resp.lineTo (x, y);
        }
        g.setColour (c.brighter (0.5f).withAlpha (0.95f));
        g.strokePath (resp, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    static constexpr int kOrder = 12, kN = 1 << kOrder;
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;

    static float xOf (float hz, juce::Rectangle<float> plot)
    {
        return plot.getX() + plot.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
    }
    View currentView() const { return view ? view() : View(); }
    bool absolute() const { return mode != Mode::Response; }

    // spectra: the loudest 84 dB (Response) or +6 dBFS down the view's range
    // (absolute); the response: -48 .. +18 dB
    float yOfSpectrum (float db, juce::Rectangle<float> plot) const
    {
        const float range = absolute() ? currentView().rangeDb : 84.0f;
        const float top = absolute() ? 6.0f : peakDb;
        const float t = juce::jlimit (0.0f, 1.0f, (db - (top - range)) / range);
        return plot.getBottom() - t * plot.getHeight();
    }
    static float yOfResponse (float db, juce::Rectangle<float> plot)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 66.0f);
        return plot.getBottom() - t * plot.getHeight();
    }

    // the FFT and its windows for a size (made anew when the size changes)
    void ensureSize (int n)
    {
        if (n == curN && fftObj != nullptr)
            return;
        curN = n;
        fftObj = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) n)));
        window.resize ((size_t) n);
        dwindow.resize ((size_t) n);
        const float w = juce::MathConstants<float>::twoPi / (float) (n - 1);
        for (int i = 0; i < n; ++i)
        {
            window[(size_t) i] = 0.5f - 0.5f * std::cos (w * (float) i);
            dwindow[(size_t) i] = 0.5f * w * std::sin (w * (float) i);   // its derivative, per sample
        }
        avgIn.clear();
        avgOut.clear();
        peakDbHold.clear();
    }

    void timerCallback() override
    {
        if (isShowing())
            update();
    }

    void update()
    {
        if (module == nullptr)
            return;
        const double sr = module->tapSampleRate() > 0.0 ? module->tapSampleRate() : 48000.0;
        const auto v = currentView();
        const int n = mode == Mode::Response ? kN
                    : juce::jlimit (512, aquanode::SynthModule::Tap::size, juce::nextPowerOfTwo (v.fftSize));
        ensureSize (n);
        std::vector<float> in ((size_t) n), out ((size_t) n);
        module->tapRead (in.data(), out.data(), n);

        auto spectrum = [&] (const std::vector<float>& x, std::vector<float>& power)
        {
            std::vector<float> buf ((size_t) n * 2, 0.0f);
            for (int i = 0; i < n; ++i)
                buf[(size_t) i] = x[(size_t) i] * window[(size_t) i];
            fftObj->performFrequencyOnlyForwardTransform (buf.data());
            power.resize ((size_t) n / 2);
            for (int b = 0; b < n / 2; ++b)
                power[(size_t) b] = buf[(size_t) b] * buf[(size_t) b];
        };
        // reassigned: every bin's power moved to the frequency its phase says
        // it measured (the derivative-window estimate), so a tone is one line
        auto reassigned = [&] (const std::vector<float>& x, std::vector<float>& power)
        {
            std::vector<float> a ((size_t) n * 2, 0.0f), d ((size_t) n * 2, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                a[(size_t) i] = x[(size_t) i] * window[(size_t) i];
                d[(size_t) i] = x[(size_t) i] * dwindow[(size_t) i];
            }
            fftObj->performRealOnlyForwardTransform (a.data(), true);
            fftObj->performRealOnlyForwardTransform (d.data(), true);
            power.assign ((size_t) n / 2, 0.0f);
            const float toBins = (float) n / juce::MathConstants<float>::twoPi;
            for (int k = 1; k < n / 2; ++k)
            {
                const float re = a[(size_t) k * 2], im = a[(size_t) k * 2 + 1];
                const float p = re * re + im * im;
                if (p < 1.0e-14f)
                    continue;
                const float dre = d[(size_t) k * 2], dim = d[(size_t) k * 2 + 1];
                const float shift = (dim * re - dre * im) / p * toBins;   // Im (X_dh conj X_h) / |X_h|^2, in bins
                const int to = juce::roundToInt ((float) k - shift);
                if (to >= 1 && to < n / 2 && std::abs (shift) < 4.0f)
                    power[(size_t) to] += p / 1.5f;   // (a Hann lobe's three bins in one: a tone keeps its level)
            }
        };
        std::vector<float> pin, pout;
        if (mode == Mode::Periodogram && v.reassign)
            reassigned (in, pin);
        else
            spectrum (in, pin);
        if (mode == Mode::Periodogram)
            pout = pin;   // (it passes the signal through: out is in)
        else
            spectrum (out, pout);

        // averaged over time a little, so it reads steadily
        const float k = mode == Mode::Response ? 0.35f : juce::jlimit (0.02f, 1.0f, v.smoothing);
        if (avgIn.size() != pin.size()) { avgIn = pin; avgOut = pout; }
        for (size_t b = 0; b < pin.size(); ++b)
        {
            avgIn[b] += k * (pin[b] - avgIn[b]);
            avgOut[b] += k * (pout[b] - avgOut[b]);
        }
        // dBFS: a full-scale sine reads 0 (a Hann window's gain, N / 4)
        const float offset = absolute() ? 20.0f * std::log10 ((float) n / 4.0f) : 0.0f;

        // onto the pixels: a sixth of an octave around each
        const int w = juce::jmax (16, getWidth() - 12);
        inDb.assign ((size_t) w, -200.0f);
        outDb.assign ((size_t) w, -200.0f);
        respDb.assign ((size_t) w, 0.0f);
        valid.assign ((size_t) w, false);
        const float binHz = (float) sr / (float) n;
        const float pixelRatio = std::pow (kMaxHz / kMinHz, 1.0f / (float) (w - 1));
        float maxIn = -200.0f, maxAll = -200.0f;
        for (int i = 0; i < w; ++i)
        {
            const float hz = kMinHz * std::pow (kMaxHz / kMinHz, (float) i / (float) (w - 1));
            const int b0 = juce::jlimit (1, n / 2 - 1, (int) std::floor (hz * 0.944f / binHz));
            const int b1 = juce::jlimit (b0, n / 2 - 1, (int) std::ceil (hz * 1.059f / binHz));
            double si = 0.0, so = 0.0;
            if (absolute())
            {
                // the absolute scales: the strongest bin in this pixel's own
                // slice (a tone reads its level, and stays as narrow as it is);
                // where the bins are wider than a pixel, between the two nearest
                const float half = std::sqrt (pixelRatio);
                const int c0 = (int) std::ceil (hz / half / binHz), c1 = (int) std::floor (hz * half / binHz);
                if (c1 >= c0 && c0 >= 1)
                    for (int b = c0; b <= juce::jmin (c1, n / 2 - 1); ++b)
                    {
                        si = juce::jmax (si, (double) avgIn[(size_t) b]);
                        so = juce::jmax (so, (double) avgOut[(size_t) b]);
                    }
                else
                {
                    const float pos = juce::jlimit (1.0f, (float) (n / 2 - 2), hz / binHz);
                    const int lo = (int) pos;
                    const float fr = pos - (float) lo;
                    si = avgIn[(size_t) lo] * (1.0f - fr) + avgIn[(size_t) lo + 1] * fr;
                    so = avgOut[(size_t) lo] * (1.0f - fr) + avgOut[(size_t) lo + 1] * fr;
                }
            }
            else
            {
                for (int b = b0; b <= b1; ++b) { si += avgIn[(size_t) b]; so += avgOut[(size_t) b]; }
                si /= (double) (b1 - b0 + 1);
                so /= (double) (b1 - b0 + 1);
            }
            inDb[(size_t) i] = 10.0f * std::log10 ((float) si + 1.0e-20f) - offset;
            outDb[(size_t) i] = 10.0f * std::log10 ((float) so + 1.0e-20f) - offset;
            maxIn = juce::jmax (maxIn, inDb[(size_t) i]);
            maxAll = juce::jmax (maxAll, juce::jmax (inDb[(size_t) i], outDb[(size_t) i]));
        }
        hasSignal = maxAll > -110.0f - offset;
        peakDb += 0.3f * (maxAll - peakDb);
        // the held peaks fall 9 dB a second
        if (mode == Mode::Periodogram && v.peakHold)
        {
            if ((int) peakDbHold.size() != w) peakDbHold = inDb;
            for (int i = 0; i < w; ++i)
                peakDbHold[(size_t) i] = juce::jmax (inDb[(size_t) i], peakDbHold[(size_t) i] - 0.45f);
        }
        else
            peakDbHold.clear();
        for (int i = 0; i < w; ++i)
            if (inDb[(size_t) i] > maxIn - 62.0f && inDb[(size_t) i] > -120.0f)
            {
                valid[(size_t) i] = true;
                respDb[(size_t) i] = juce::jlimit (-60.0f, 30.0f, outDb[(size_t) i] - inDb[(size_t) i]);
            }
        repaint();
    }

public:
    // the latest curve, in dBFS per pixel (the harness reads it)
    const std::vector<float>& curveDb() const { return inDb; }
    void refreshNow() { update(); }

private:
    juce::WeakReference<aquanode::SynthModule> module;
    std::function<juce::Colour()> accent;
    Mode mode;
    std::function<View()> view;
    std::vector<float> peakDbHold;
    std::unique_ptr<juce::dsp::FFT> fftObj;
    int curN { 0 };
    std::vector<float> window, dwindow, avgIn, avgOut, inDb, outDb, respDb;
    std::vector<bool> valid;
    float peakDb { -40.0f };
    bool hasSignal { false };
};
