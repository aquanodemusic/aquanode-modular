#pragma once

// The Graph of Clip Preserve and Spline Folder: two views of what they do to
// the sound, switched by the little Spectrum | Wave switch at its top right.
//
//   Spectrum  FilterGraph's spectrum view: what comes out (filled) over what
//             goes in (a line), in dBFS - the new harmonics
//   Wave      the waveform, no FFT: what goes in, times Drive (a faint line -
//             what the fold really meets), and what comes out (bright), two
//             periods of it held still on the input's rising crossing, with
//             the Ceiling's lines - so a sine's top is seen folding back down
//             from the ceiling (or wrapping in from the floor). Its height
//             follows the sound; with Fixed it stays at -1 .. 1 (full scale)
//             and shows the output alone
//
// Wave is where a card starts (since 5.19.6).
//
// Both read the card's tap (the module's first input and output, mid).

#include <JuceHeader.h>
#include "../Modules/Core/ModuleCore.h"
#include "../Modules/Utilities/Analysis/WaveDraw.h"
#include "FilterGraph.h"

class TapWaveform : public juce::Component,
                    private juce::Timer
{
public:
    TapWaveform (aquanode::SynthModule& m, std::function<juce::Colour()> accentOf)
        : module (&m), accent (std::move (accentOf))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const bool glassOn = aquanode::themeColours().glass;
        g.setColour (glassOn ? juce::Colours::black.withAlpha (0.22f) : juce::Colour (0xff141210).withAlpha (0.82f));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (glassOn ? 0.2f : 0.14f));
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

        const auto plot = area.reduced (6.0f, 5.0f).withTrimmedBottom (9.0f);
        const auto c = accent ? accent() : juce::Colour (0xff74cc8a);

        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);
        // the ceiling (and floor), where the folding happens
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        for (float v : { ceiling, -ceiling })
        {
            const float y = aquanode::wavedraw::yOf (v, plot, scale);
            g.setColour (c.brighter (0.4f).withAlpha (0.45f));
            const float dashes[] = { 4.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashes, 2, 1.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawText (hasSignal ? aquanode::wavedraw::timeText (windowMs) + " across" : juce::String(),
                    juce::Rectangle<float> (plot.getX(), plot.getBottom() + 1.0f, 120.0f, 9.0f), juce::Justification::centredLeft);
        if (fixed)
        {
            g.drawText ("fixed: -1 .. +1", juce::Rectangle<float> (plot.getRight() - 120.0f, plot.getBottom() + 1.0f, 120.0f, 9.0f),
                        juce::Justification::centredRight);
            for (float v : { 1.0f, -1.0f })
            {
                const float y = aquanode::wavedraw::yOf (v, plot, scale);
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText (v > 0 ? "+1" : "-1", juce::Rectangle<float> (plot.getX() + 2.0f, v > 0 ? y + 1.0f : y - 10.0f, 18.0f, 9.0f),
                            juce::Justification::topLeft);
            }
        }
        else
            g.drawText (driveGain > 1.001f || driveGain < 0.999f ? "faint: in x Drive" : "faint: in",
                        juce::Rectangle<float> (plot.getRight() - 120.0f, plot.getBottom() + 1.0f, 120.0f, 9.0f), juce::Justification::centredRight);

        if (! hasSignal)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.drawText ("Play something through it", plot, juce::Justification::centred);
            return;
        }
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt().expanded (0, 1));
        if (! fixed)
            aquanode::wavedraw::drawTrace (g, plot, in.data(), (int) in.size(), start, windowN, scale, juce::Colours::white.withAlpha (0.35f), 1.0f);
        aquanode::wavedraw::drawTrace (g, plot, out.data(), (int) out.size(), start, windowN, scale, c.brighter (0.5f).withAlpha (0.95f), 1.6f);
        g.restoreState();
    }

private:
    void timerCallback() override
    {
        const int n = aquanode::SynthModule::Tap::size;
        in.resize ((size_t) n);
        out.resize ((size_t) n);
        module->tapRead (in.data(), out.data(), n);
        const double sr = module->tapSampleRate() > 0.0 ? module->tapSampleRate() : 48000.0;

        // what the fold meets: the input times Drive (dB); the ceiling (dB, else full scale)
        driveGain = 1.0f;
        ceiling = 1.0f;
        for (const auto& p : module->getDescriptor().params)
        {
            if (p.id == "drive")   driveGain = juce::Decibels::decibelsToGain (module->getParameter ("drive"));
            if (p.id == "ceiling") ceiling = juce::Decibels::decibelsToGain (module->getParameter ("ceiling"));
        }
        float peak = 0.0f;
        for (int i = n / 2; i < n; ++i)
        {
            in[(size_t) i] *= driveGain;
            peak = juce::jmax (peak, std::abs (in[(size_t) i]), std::abs (out[(size_t) i]));
        }
        for (int i = 0; i < n / 2; ++i) in[(size_t) i] *= driveGain;
        hasSignal = peak > 1.0e-4f;
        if (! hasSignal) { repaint(); return; }

        // two periods, held on the input's rising crossings (20 ms without a period)
        int last = -1, before = -1;
        for (int i = n - 1; i > 0 && before < 0; --i)
            if (in[(size_t) (i - 1)] < 0.0f && in[(size_t) i] >= 0.0f)
            {
                if (last < 0) last = i;
                else if (last - i > 8) before = i;
            }
        const double period = before > 0 ? (double) (last - before) : 0.0;
        windowN = period > 0.0 ? juce::jlimit (0.001 * sr, 0.06 * sr, 2.0 * period) : 0.02 * sr;
        windowMs = 1000.0 * windowN / sr;
        start = aquanode::wavedraw::triggerStart (in.data(), n, windowN, aquanode::wavedraw::Rising, 0.0f, (int) (0.08 * sr));

        // the height: everything in view, the ceiling inside, eased so it does
        // not jump - or, Fixed, full scale (-1 .. 1) filling it, always
        if (fixed)
            scale = 0.92f;
        else
        {
            const float want = 0.92f / juce::jmax (ceiling * 1.15f, peak);
            scale = scale <= 0.0f ? want : scale + 0.25f * (want - scale);
        }
        repaint();
    }

public:
    void setFixed (bool f) { fixed = f; scale = f ? 0.92f : 0.0f; repaint(); }
private:

    aquanode::SynthModule* module;
    std::function<juce::Colour()> accent;
    std::vector<float> in, out;
    double start { 0.0 }, windowN { 960.0 }, windowMs { 20.0 };
    float driveGain { 1.0f }, ceiling { 1.0f }, scale { 0.0f };
    bool hasSignal { false }, fixed { false };
};

// the two views and their switch
class TapGraphs : public juce::Component
{
public:
    TapGraphs (aquanode::SynthModule& m, std::function<juce::Colour()> accentOf, int initialView, std::function<void (int)> onViewChanged)
        : spectrum (m, accentOf, FilterGraph::Mode::Spectrum), wave (m, accentOf), view (initialView), changed (std::move (onViewChanged))
    {
        addChildComponent (spectrum);
        addChildComponent (wave);
        fixed = view == 2;
        if (view == 2) view = 1;
        wave.setFixed (fixed);
        showView();
    }

    void resized() override
    {
        spectrum.setBounds (getLocalBounds());
        wave.setBounds (getLocalBounds());
    }

    // only the switch (and, in Wave, the Fixed pill) takes clicks
    bool hitTest (int x, int y) override
    {
        return switchArea().contains (x, y) || (view == 1 && fixedArea().contains (x, y));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (view == 1 && fixedArea().contains (e.getPosition()))
        {
            fixed = ! fixed;
            wave.setFixed (fixed);
        }
        else if (switchArea().contains (e.getPosition()))
            view = e.x < switchArea().getCentreX() ? 0 : 1;
        else
            return;
        showView();
        if (changed) changed (view == 1 && fixed ? 2 : view);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const auto r = switchArea().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        auto half = r.withWidth (r.getWidth() * 0.5f);
        if (view == 1) half.setX (r.getCentreX());
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (half, r.getHeight() * 0.5f);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.setColour (juce::Colours::white.withAlpha (view == 0 ? 0.95f : 0.55f));
        g.drawText ("Spectrum", r.withWidth (r.getWidth() * 0.5f), juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (view == 1 ? 0.95f : 0.55f));
        g.drawText ("Wave", r.withTrimmedLeft (r.getWidth() * 0.5f), juce::Justification::centred);

        if (view == 1)   // the Fixed pill: full scale at a fixed height, the output alone
        {
            const auto f = fixedArea().toFloat();
            g.setColour (fixed ? juce::Colours::white.withAlpha (0.3f) : juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (f, f.getHeight() * 0.5f);
            g.setColour (juce::Colours::white.withAlpha (fixed ? 0.95f : 0.55f));
            g.drawText ("Fixed", f, juce::Justification::centred);
        }
    }

private:
    juce::Rectangle<int> switchArea() const { return { getWidth() - 104, 4, 98, 15 }; }
    juce::Rectangle<int> fixedArea() const { return { getWidth() - 152, 4, 44, 15 }; }
    void showView()
    {
        spectrum.setVisible (view == 0);
        wave.setVisible (view == 1);
        repaint();
    }

    FilterGraph spectrum;
    TapWaveform wave;
    int view { 1 };
    bool fixed { false };
    std::function<void (int)> changed;
};
