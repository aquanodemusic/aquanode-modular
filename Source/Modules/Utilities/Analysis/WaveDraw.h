#pragma once

// Drawing a waveform - no FFT, just the samples: the Waveform Viewer module
// and the Waveform view of Clip Preserve's and Spline Folder's Graph.
//
//   triggerStart  where a window should begin so a periodic wave stands still:
//                 the newest crossing of a level (rising or falling) that
//                 still leaves a whole window after it, found to a fraction
//                 of a sample; free-running (the newest window) without one
//   drawTrace     one wave across a plot: through every sample when there
//                 are up to six a pixel, as each pixel column's min..max when there are
//                 more samples than pixels (so no peak is ever skipped)

#include <JuceHeader.h>

namespace aquanode::wavedraw
{
    enum Trigger { Free = 0, Rising, Falling };

    // x: len samples, oldest first; a window of n samples; the crossing is
    // looked for among the last `search` possible starts
    inline double triggerStart (const float* x, int len, double n, int trigger, float level, int search)
    {
        const double newest = (double) len - n;   // the newest possible start
        if (newest < 1.0)
            return juce::jmax (0.0, newest);
        if (trigger == Free)
            return newest;
        const int hi = (int) std::floor (newest);
        const int lo = juce::jmax (1, hi - search);
        for (int i = hi; i >= lo; --i)
        {
            const float a = x[i - 1], b = x[i];
            const bool cross = trigger == Rising ? (a < level && b >= level) : (a > level && b <= level);
            if (cross)
                return (double) (i - 1) + (b != a ? (double) ((level - a) / (b - a)) : 0.0);
        }
        return newest;
    }

    inline float yOf (float v, juce::Rectangle<float> plot, float scale)
    {
        return plot.getCentreY() - v * scale * plot.getHeight() * 0.5f;
    }

    // the wave x[start .. start + n) across plot; scale: 1 = full scale fills the plot
    inline void drawTrace (juce::Graphics& g, juce::Rectangle<float> plot, const float* x, int len,
                           double start, double n, float scale, juce::Colour colour, float thickness = 1.5f)
    {
        if (len < 2 || n <= 0.0)
            return;
        const float w = plot.getWidth();
        const double perPixel = n / (double) w;
        g.setColour (colour);
        if (perPixel <= 6.0)
        {
            juce::Path p;
            const int i0 = juce::jmax (0, (int) std::floor (start));
            const int i1 = juce::jmin (len - 1, (int) std::ceil (start + n));
            for (int i = i0; i <= i1; ++i)
            {
                const float px = plot.getX() + (float) (((double) i - start) / n) * w;
                const float py = yOf (x[i], plot, scale);
                if (i == i0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        // dense: each column's min..max, joined to the column before so the
        // line stays unbroken - the band's top edge out, its bottom edge back
        const int cols = juce::jmax (1, (int) w);
        std::vector<float> top ((size_t) cols), bottom ((size_t) cols);
        float prevY = yOf (x[juce::jlimit (0, len - 1, (int) start)], plot, scale);
        for (int c = 0; c < cols; ++c)
        {
            const int a = juce::jlimit (0, len - 1, (int) std::floor (start + c * perPixel));
            const int b = juce::jlimit (a + 1, len, (int) std::floor (start + (c + 1) * perPixel));
            float lo = x[a], hi = x[a];
            for (int i = a + 1; i < b; ++i) { lo = juce::jmin (lo, x[i]); hi = juce::jmax (hi, x[i]); }
            top[(size_t) c] = juce::jmin (yOf (hi, plot, scale), prevY) - thickness * 0.5f;
            bottom[(size_t) c] = juce::jmax (yOf (lo, plot, scale), prevY) + thickness * 0.5f;
            prevY = yOf (x[b - 1], plot, scale);
        }
        juce::Path band;
        band.startNewSubPath (plot.getX(), top[0]);
        for (int c = 0; c < cols; ++c) band.lineTo (plot.getX() + (float) c + 0.5f, top[(size_t) c]);
        for (int c = cols - 1; c >= 0; --c) band.lineTo (plot.getX() + (float) c + 0.5f, bottom[(size_t) c]);
        band.closeSubPath();
        g.fillPath (band);
    }

    // a length in readable units
    inline juce::String timeText (double ms)
    {
        if (ms >= 1000.0) return juce::String (ms / 1000.0, ms >= 10000.0 ? 0 : 2) + " s";
        if (ms >= 10.0)   return juce::String (ms, 0) + " ms";
        return juce::String (ms, 2) + " ms";
    }
}
