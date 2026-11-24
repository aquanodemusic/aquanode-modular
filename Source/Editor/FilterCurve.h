#pragma once

// A filter card's Graph: the curve its knobs set, worked out from the
// filter's own maths (SynthModule::magnitudeResponseDb) - a static EQ curve,
// no signal involved, redrawn the moment a knob moves.
//
// Each pixel column is sampled four times across its width: where a curve
// changes faster than a pixel can show (a comb's teeth up high), the column
// is drawn as the band from its lowest to its highest value, so nothing
// disappears between pixels.

#include <JuceHeader.h>
#include "../Modules/Core/ModuleCore.h"

class FilterCurve : public juce::Component,
                    private juce::Timer
{
public:
    static constexpr int kHeight = 104;

    FilterCurve (aquanode::SynthModule& m, std::function<juce::Colour()> accentOf)
        : module (&m), accent (std::move (accentOf))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (20);
    }

    void resized() override { lastKey = 0; recompute(); }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const bool glassOn = aquanode::themeColours().glass;
        g.setColour (glassOn ? juce::Colours::black.withAlpha (0.22f) : juce::Colour (0xff141210).withAlpha (0.82f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (glassOn ? 0.2f : 0.14f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

        const auto plot = plotArea();
        const auto c = accent ? accent() : juce::Colour (0xff74cc8a);

        // the grid: 100 Hz, 1 kHz, 10 kHz (a linear scale: round steps across it); every 12 dB, 0 dB brighter
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        std::vector<std::pair<float, juce::String>> ticks;
        if (linear)
        {
            float step = 10.0f;
            for (float s : { 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f })
                if ((linHi - linLo) / s <= 6.0f) { step = s; break; }
            for (float t = std::ceil (linLo / step) * step; t <= linHi; t += step)
                ticks.push_back ({ t, t >= 1000.0f ? juce::String (t / 1000.0f, std::fmod (t, 1000.0f) == 0.0f ? 0 : 1) + "k" : juce::String ((int) t) });
        }
        else
            ticks = { { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } };
        for (const auto& [hz, label] : ticks)
        {
            const float x = xOf (hz, plot);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.38f));
            g.drawText (label, juce::Rectangle<float> (x - 14.0f, plot.getBottom() + 1.0f, 28.0f, 9.0f), juce::Justification::centred);
        }
        for (int dbLine = (int) topDb - 6; dbLine > (int) bottomDb() + 6; dbLine -= 12)
        {
            const float y = yOf ((float) dbLine, plot);
            g.setColour (juce::Colours::white.withAlpha (dbLine == 0 ? 0.22f : 0.08f));
            g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (dbLine == 0 ? 0.5f : 0.32f));
            g.drawText ((dbLine > 0 ? "+" : "") + juce::String (dbLine), juce::Rectangle<float> (plot.getX() + 1.0f, y + 1.0f, 24.0f, 9.0f),
                        juce::Justification::topLeft);
        }

        const int w = (int) hiDb.size();
        if (w < 2)
            return;
        auto xAt = [&] (int i) { return plot.getX() + (float) i + 0.5f; };

        // under the curve, filled; where a column holds a band (teeth finer
        // than a pixel), the band itself a little stronger
        juce::Path fill, top, band;
        fill.startNewSubPath (xAt (0), plot.getBottom());
        for (int i = 0; i < w; ++i)
        {
            const float y = yOf (hiDb[(size_t) i], plot);
            fill.lineTo (xAt (i), y);
            if (i == 0) top.startNewSubPath (xAt (i), y); else top.lineTo (xAt (i), y);
            if (hiDb[(size_t) i] - loDb[(size_t) i] > 1.5f)
                band.addRectangle (xAt (i) - 0.5f, y, 1.0f, juce::jmax (1.0f, yOf (loDb[(size_t) i], plot) - y));
        }
        fill.lineTo (xAt (w - 1), plot.getBottom());
        fill.closeSubPath();

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt().expanded (0, 1));
        g.setColour (c.withAlpha (0.24f));
        g.fillPath (fill);
        g.setColour (c.withAlpha (0.30f));
        g.fillPath (band);
        g.setColour (c.brighter (0.5f).withAlpha (0.95f));
        g.strokePath (top, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
    }

private:
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    static constexpr float kRangeDb = 66.0f;   // +18 .. -48 dB, raised in 12 dB steps for a curve above +15
    static constexpr int kSub = 4;              // samples per pixel column
    float topDb { 18.0f };
    float bottomDb() const { return topDb - kRangeDb; }

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (6.0f, 5.0f).withTrimmedBottom (9.0f); }

    float xOf (float hz, juce::Rectangle<float> plot) const
    {
        if (linear) return plot.getX() + plot.getWidth() * (hz - linLo) / juce::jmax (1.0f, linHi - linLo);
        return plot.getX() + plot.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
    }
    // (6.9.19) a linear frequency scale around the peaks: Center Comb with a Linear spread
    // (its peaks stand evenly there, Spread Hz apart)
    bool wantsLinear()
    {
        if (module->getDescriptor().typeId != "fx.centercomb" || module->getDescriptor().findParam ("spreadMode") == nullptr
            || module->getParameter ("spreadMode") > 0.5f)
            return false;
        const float centre = module->getParameter ("freq"), spread = module->getParameter ("spread");
        const float reach = (module->getParameter ("count") + 1.0f) * spread;
        linLo = juce::jmax (kMinHz, centre - reach);
        linHi = juce::jmin (kMaxHz, centre + reach);
        if (linHi - linLo < 200.0f) { linLo = juce::jmax (kMinHz, centre - 100.0f); linHi = linLo + 200.0f; }
        return true;
    }
    bool linear { false };
    float linLo { kMinHz }, linHi { kMaxHz };
    float yOf (float db, juce::Rectangle<float> plot) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - bottomDb()) / kRangeDb);
        return plot.getBottom() - t * plot.getHeight();
    }

    // every knob and the sample rate: the curve is worked out again only
    // when one of them moved
    juce::uint64 key() const
    {
        juce::uint64 h = 1469598103934665603ull;
        auto mix = [&h] (juce::uint64 v) { h = (h ^ v) * 1099511628211ull; };
        for (const auto& p : module->getDescriptor().params)
        {
            const float v = module->getParameter (p.id);
            juce::uint32 bits;
            std::memcpy (&bits, &v, sizeof bits);
            mix (bits);
        }
        mix ((juce::uint64) module->tapSampleRate());
        mix ((juce::uint64) getWidth());
        // (6.9.19) a curve that moves by itself (Ripple EQ's travelling ripples, Audio to
        // Filter's spectrum): a few probes of it, so it is redrawn as it moves
        {
            static const float probes[12] = { 31.0f, 63.0f, 125.0f, 250.0f, 400.0f, 700.0f, 1100.0f, 1900.0f, 3200.0f, 5600.0f, 9000.0f, 15000.0f };
            float db[12] {};
            if (module->magnitudeResponseDb (probes, db, 12))
                for (float v : db)
                {
                    const float q = std::round (v * 20.0f);   // (to 0.05 dB: no redraw for noise)
                    juce::uint32 bits;
                    std::memcpy (&bits, &q, sizeof bits);
                    mix (bits);
                }
        }
        return h;
    }

    void timerCallback() override
    {
        if (key() != lastKey)
        {
            recompute();
            repaint();
        }
    }

    void recompute()
    {
        lastKey = key();
        const auto plot = plotArea();
        const int w = juce::jmax (0, (int) plot.getWidth());
        if (w < 2)
        {
            hiDb.clear(); loDb.clear();
            return;
        }
        // the column i covers x from i to i + 1; its kSub samples spread across that
        hz.resize ((size_t) (w * kSub));
        db.resize (hz.size());
        linear = wantsLinear();
        const float span = std::log (kMaxHz / kMinHz);
        for (int i = 0; i < w * kSub; ++i)
        {
            const float t = ((float) i + 0.5f) / (float) (w * kSub);
            hz[(size_t) i] = linear ? linLo + t * (linHi - linLo) : kMinHz * std::exp (span * t);
        }
        if (! module->magnitudeResponseDb (hz.data(), db.data(), (int) hz.size()))
        {
            hiDb.clear(); loDb.clear();
            return;
        }
        hiDb.assign ((size_t) w, -1000.0f);
        loDb.assign ((size_t) w, 1000.0f);
        for (int i = 0; i < w * kSub; ++i)
        {
            const float v = std::isfinite (db[(size_t) i]) ? db[(size_t) i] : -200.0f;
            auto& hi = hiDb[(size_t) (i / kSub)];
            auto& lo = loDb[(size_t) (i / kSub)];
            hi = juce::jmax (hi, v);
            lo = juce::jmin (lo, v);
        }
        const float most = *std::max_element (hiDb.begin(), hiDb.end());
        topDb = most <= 15.0f ? 18.0f : juce::jmin (54.0f, 18.0f + 12.0f * std::ceil ((most - 15.0f) / 12.0f));
    }

    aquanode::SynthModule* module;
    std::function<juce::Colour()> accent;
    juce::uint64 lastKey { 0 };
    std::vector<float> hz, db, hiDb, loDb;
};
