#pragma once
//==============================================================================
// Cove Groove - the volume curve editor: the lane's sample drawn as it is
// (faint) and as it plays through the curve (bright), the curve over it.
//   tap / click empty space: a new point there (and drag it)
//   drag a point: move it (the end points move up and down only)
//   long-press, double-tap or right-click a point: remove it
// Used by the card's sample panel and the patch version's drum modules.
//==============================================================================
#include <JuceHeader.h>
#include "CgVoices.h"

namespace covegroove
{
    class CurveEditor : public juce::Component, private juce::Timer
    {
    public:
        std::function<void (const Curve&)> onChange;     // while dragging, and on every add / remove
        juce::Colour accent { 0xff1ad6ec }, well { 0xff1b1511 }, faint { 0xffb2a189 };
        juce::String emptyText { "No sample loaded" };

        void setSample (std::shared_ptr<const SampleData> s)
        {
            if (s == sample) return;
            sample = std::move (s);
            peaks.clear();
            repaint();
        }

        // from outside (another view, a loaded state): not while a point is held
        void setCurve (const Curve& c)
        {
            if (dragIndex >= 0) return;
            curve = c;
            curve.tidy();
            repaint();
        }
        const Curve& getCurve() const { return curve; }

        void resized() override { peaks.clear(); }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (well);
            g.fillRect (r);
            const auto a = area();
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            for (int i = 1; i < 4; ++i)
            {
                g.fillRect (a.getX(), a.getY() + a.getHeight() * (float) i / 4.0f, a.getWidth(), 1.0f);
                g.fillRect (a.getX() + a.getWidth() * (float) i / 4.0f, a.getY(), 1.0f, a.getHeight());
            }

            // the sample, as it is and as the curve shapes it
            if (sample != nullptr && sample->frames() > 1)
            {
                makePeaks ((int) a.getWidth());
                const float mid = a.getCentreY(), half = a.getHeight() * 0.5f;
                juce::Path raw, shaped;
                const int w = (int) peaks.size();
                for (int pass = 0; pass < 2; ++pass)
                {
                    auto& p = pass == 0 ? raw : shaped;
                    for (int x = 0; x < w; ++x)
                    {
                        const float u = (float) x / (float) juce::jmax (1, w - 1);
                        const float k = pass == 0 ? 1.0f : curve.at (u);
                        const float y = mid - peaks[(size_t) x] * k * half;
                        if (x == 0) p.startNewSubPath (a.getX(), y); else p.lineTo (a.getX() + (float) x, y);
                    }
                    for (int x = w - 1; x >= 0; --x)
                    {
                        const float u = (float) x / (float) juce::jmax (1, w - 1);
                        const float k = pass == 0 ? 1.0f : curve.at (u);
                        p.lineTo (a.getX() + (float) x, mid + peaks[(size_t) x] * k * half);
                    }
                    p.closeSubPath();
                }
                g.setColour (faint.withAlpha (0.22f));
                g.fillPath (raw);
                g.setColour (accent.withAlpha (0.85f));     // one flat colour, as the steps
                g.fillPath (shaped);
            }
            else
            {
                g.setColour (faint);
                g.setFont (juce::Font (juce::FontOptions (12.0f)));
                g.drawText (emptyText, getLocalBounds(), juce::Justification::centred, true);
            }

            // the curve and its points
            juce::Path line;
            for (int i = 0; i < curve.n; ++i)
            {
                const auto p = toScreen (i);
                if (i == 0) line.startNewSubPath (p); else line.lineTo (p);
            }
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.strokePath (line, juce::PathStrokeType (3.5f));
            g.setColour (juce::Colour (0xffeadcc0));
            g.strokePath (line, juce::PathStrokeType (1.6f));
            const float pr = pointRadius();
            for (int i = 0; i < curve.n; ++i)
            {
                const auto p = toScreen (i);
                const bool held = i == dragIndex;
                const auto handle = juce::Rectangle<float> (pr * 1.8f, pr * 1.8f).withCentre (p);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRect (handle.expanded (1.0f).translated (0.0f, 1.0f));
                g.setColour (held ? accent.brighter (0.4f) : juce::Colour (0xffeadcc0));
                g.fillRect (handle);
                g.setColour (accent.darker (0.3f));
                g.drawRect (handle, 1.2f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragIndex = -1;
            const int hit = pointAt (e.position);
            if (e.mods.isPopupMenu())
            {
                if (hit > 0 && hit < curve.n - 1) removePoint (hit);
                return;
            }
            if (hit >= 0) dragIndex = hit;
            else if (curve.n < Curve::kMaxPoints)
            {
                // a new point, between the ones either side of it
                const auto v = fromScreen (e.position);
                int at = 1;
                while (at < curve.n - 1 && curve.x[at] < v.x) ++at;
                for (int i = curve.n; i > at; --i) { curve.x[i] = curve.x[i - 1]; curve.y[i] = curve.y[i - 1]; }
                curve.x[at] = juce::jlimit (curve.x[at - 1] + 0.001f, curve.x[at + 1] - 0.001f, v.x);
                curve.y[at] = v.y;
                ++curve.n;
                dragIndex = at;
                changed();
            }
            pressPos = e.position;
            pressTime = juce::Time::getMillisecondCounter();
            moved = false;
            longPressArmed = hit > 0 && hit < curve.n - 1;   // an existing middle point: a long press removes it
            if (longPressArmed) startTimer (60);
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragIndex < 0) return;
            if (e.position.getDistanceFrom (pressPos) > 5.0f) moved = true;
            if (! moved) return;
            const auto v = fromScreen (e.position);
            const int i = dragIndex;
            if (i > 0 && i < curve.n - 1)
                curve.x[i] = juce::jlimit (curve.x[i - 1] + 0.001f, curve.x[i + 1] - 0.001f, v.x);
            curve.y[i] = v.y;
            changed();
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            stopTimer();
            longPressArmed = false;
            dragIndex = -1;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int hit = pointAt (e.position);
            if (hit > 0 && hit < curve.n - 1) removePoint (hit);
        }

    private:
        void timerCallback() override
        {
            if (! longPressArmed || moved) { stopTimer(); return; }
            if (juce::Time::getMillisecondCounter() - pressTime > 480)
            {
                stopTimer();
                longPressArmed = false;
                if (dragIndex > 0 && dragIndex < curve.n - 1) removePoint (dragIndex);
                dragIndex = -1;
            }
        }

        void removePoint (int i)
        {
            for (int k = i; k < curve.n - 1; ++k) { curve.x[k] = curve.x[k + 1]; curve.y[k] = curve.y[k + 1]; }
            --curve.n;
            dragIndex = -1;
            changed();
            repaint();
        }

        void changed() { if (onChange) onChange (curve); }

        float pointRadius() const { return juce::jlimit (4.0f, 7.0f, (float) getHeight() * 0.06f); }
        juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (pointRadius() + 3.0f); }
        juce::Point<float> toScreen (int i) const
        {
            const auto a = area();
            return { a.getX() + curve.x[i] * a.getWidth(), a.getBottom() - curve.y[i] * a.getHeight() };
        }
        juce::Point<float> fromScreen (juce::Point<float> p) const
        {
            const auto a = area();
            return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / juce::jmax (1.0f, a.getWidth())),
                     juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / juce::jmax (1.0f, a.getHeight())) };
        }
        int pointAt (juce::Point<float> p) const
        {
            // touch-sized: the nearest point within 14 px
            int best = -1;
            float bestD = 14.0f;
            for (int i = 0; i < curve.n; ++i)
                if (const float d = toScreen (i).getDistanceFrom (p); d < bestD) { bestD = d; best = i; }
            return best;
        }

        // the loudest frame in each pixel column, scaled to the loudest of all
        void makePeaks (int w)
        {
            if (! peaks.empty() || w <= 1 || sample == nullptr) return;
            peaks.assign ((size_t) w, 0.0f);
            const int n = sample->frames();
            float top = 1.0e-6f;
            for (int x = 0; x < w; ++x)
            {
                const int a = (int) ((juce::int64) n * x / w), b = juce::jmax (a + 1, (int) ((juce::int64) n * (x + 1) / w));
                float m = 0.0f;
                for (int i = a; i < b && i < n; ++i)
                {
                    m = juce::jmax (m, std::abs (sample->left[(size_t) i]));
                    if (sample->stereo()) m = juce::jmax (m, std::abs (sample->right[(size_t) i]));
                }
                peaks[(size_t) x] = m;
                top = juce::jmax (top, m);
            }
            for (auto& p : peaks) p = std::sqrt (p / top) * 0.96f;   // a gentle root, so the tail shows
        }

        std::shared_ptr<const SampleData> sample;
        std::vector<float> peaks;
        Curve curve;
        int dragIndex { -1 };
        juce::Point<float> pressPos;
        juce::uint32 pressTime { 0 };
        bool moved { false }, longPressArmed { false };
    };
}
