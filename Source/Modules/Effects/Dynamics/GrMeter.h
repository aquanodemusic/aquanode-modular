#pragma once

// A needle meter for the character compressors (Opto, FET): how much gain
// reduction they apply right now, 0 to -20 dB, swinging like the meters on
// the hardware they recall (a VU's 300 ms ballistics), with the reading in
// numbers under it.

#include <JuceHeader.h>
#include "../../Core/ModuleCore.h"

namespace aquanode
{
    class GrMeter final : public juce::Component, private juce::Timer
    {
    public:
        // grSource: the gain reduction in dB (>= 0), written by the audio
        // thread as the most since the meter last looked (it swaps in 0)
        GrMeter (std::atomic<float>& grSource, juce::Colour faceColour, juce::String caption)
            : source (grSource), face (faceColour), label (std::move (caption))
        {
            setInterceptsMouseClicks (false, false);
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (8.0f, 4.0f);
            const float w = juce::jmin (r.getWidth(), r.getHeight() * 2.1f);
            auto meter = r.withSizeKeepingCentre (w, r.getHeight());
            g.setColour (face);
            g.fillRoundedRectangle (meter, 6.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (meter.reduced (0.5f), 6.0f, 1.2f);

            const juce::Point<float> pivot (meter.getCentreX(), meter.getBottom() - 8.0f);
            const float radius = juce::jmin (meter.getWidth() * 0.46f, meter.getHeight() - 18.0f);
            // 0 dB at the right, -20 at the left
            auto angleOf = [] (float grDb) { return juce::jmap (juce::jlimit (0.0f, 20.0f, grDb), 0.0f, 20.0f, 0.78f, -0.78f); };
            auto at = [&] (float angle, float rad) { return pivot + juce::Point<float> (std::sin (angle) * rad, -std::cos (angle) * rad); };

            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float gr : { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 15.0f, 20.0f })
            {
                const float a = angleOf (gr);
                const bool big = gr == 0.0f || gr == 5.0f || gr == 10.0f || gr == 20.0f;
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.drawLine (juce::Line<float> (at (a, radius * (big ? 0.84f : 0.9f)), at (a, radius)), big ? 1.6f : 1.0f);
                if (big)
                    g.drawText (gr == 0.0f ? "0" : "-" + juce::String ((int) gr),
                                juce::Rectangle<float> (26.0f, 10.0f).withCentre (at (a, radius * 0.7f)), juce::Justification::centred);
            }
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (label, meter.withTrimmedTop (meter.getHeight() * 0.42f).withHeight (12.0f), juce::Justification::centred);

            // the needle
            g.setColour (juce::Colour (0xffb3261e));
            g.drawLine (juce::Line<float> (pivot, at (angleOf (needle), radius * 0.98f)), 1.8f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pivot));

            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText ("GR " + juce::String (-shown, 1) + " dB", meter.removeFromBottom (14.0f).withTrimmedLeft (meter.getWidth() * 0.58f),
                        juce::Justification::centredLeft);
        }

    private:
        void timerCallback() override
        {
            const float target = source.exchange (0.0f);
            peakHeld = juce::jmax (target, peakHeld * 0.6f);
            // a VU's swing: about 300 ms to settle
            needle += (peakHeld - needle) * 0.18f;
            shown += (peakHeld - shown) * 0.3f;
            repaint();
        }

        std::atomic<float>& source;
        juce::Colour face;
        juce::String label;
        float needle { 0.0f }, shown { 0.0f }, peakHeld { 0.0f };
    };
}
