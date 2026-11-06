#pragma once
// The on-screen keys (only the editor uses it).
#include "EditorShared.h"

// juce::MidiKeyboardComponent inherits juce::Timer privately, so inside a class
// derived from it the name "juce::Timer" finds the inaccessible base (MSVC C2247/C2248).
// Called from outside the class, the static juce::Timer::callAfterDelay is plain public.
namespace expressive_keyboard_detail
{
    inline void callAfterDelay (int ms, std::function<void()> f)
    {
        juce::Timer::callAfterDelay (ms, std::move (f));
    }
}

//==============================================================================
// The on-screen keys. With MPE off, JUCE's own keyboard as always. With MPE
// on, every finger is its own note on a MIDI channel of its own (2..16):
// sliding sideways bends it (a key's width a semitone, up to 48), up and down
// on the key is Slide (CC 74, top = full), and where the screen reports it the
// touch's pressure is the note's pressure. Each finger shows as a glowing
// mark with a line back to its key.
//==============================================================================
class ExpressiveKeyboard : public juce::MidiKeyboardComponent
{
public:
    explicit ExpressiveKeyboard (AquanodeModularAudioProcessor& p)
        : juce::MidiKeyboardComponent (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard), processor (p) {}

    //==========================================================================
    // The keys' width: a zoom on the width the window gives them. Two fingers
    // moving apart or together (a pinch) widen or narrow the keys, and so do
    // Shift + drag (right: wider), Shift + mouse wheel and a trackpad pinch.
    // The keys under the fingers (or the pointer) stay where they are.
    std::function<void()> onZoomFinished;   // a gesture ended: the zoom can be kept

    void setBaseKeyWidth (float w)
    {
        baseKeyWidth = juce::jmax (4.0f, w);
        setKeyWidth (keyWidthForZoom (zoom()));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! e.mods.isShiftDown())
        {
            juce::MidiKeyboardComponent::mouseWheelMove (e, w);
            return;
        }
        // (some systems turn a Shift + wheel into a sideways wheel)
        const float amount = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        zoomAround (zoom() * std::exp (amount * 1.2f), e.position.x, noteAtX (e.position.x));
        finishSoon();
    }

    void mouseMagnify (const juce::MouseEvent& e, float scale) override
    {
        zoomAround (zoom() * scale, e.position.x, noteAtX (e.position.x));
        finishSoon();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int src = e.source.getIndex();
        if (e.source.isMouse() && e.mods.isShiftDown())
        {
            // Shift + drag: the width, no note
            shiftDrag = true;
            gestureStartX = e.position.x;
            gestureStartZoom = zoom();
            gestureAnchorNote = noteAtX (e.position.x);
            return;
        }
        fingers.erase (src);
        fingers.emplace (src, e);
        if (fingers.size() == 2 && pinchSources.empty())
        {
            // two fingers down: a pinch, once they move apart or together
            pinchStartDistance = fingerDistance();
            gestureStartZoom = zoom();
            gestureAnchorNote = noteAtX (fingerMidX());
        }
        if (! mpe()) { juce::MidiKeyboardComponent::mouseDown (e); return; }
        const auto nv = getNoteAndVelocityAtPosition (e.position);
        if (nv.note < 0)
            return;
        release (e.source.getIndex());   // the same finger, a new press
        Touch t;
        t.source = e.source.getIndex();
        t.note = nv.note;
        t.channel = freeChannel();
        t.start = t.pos = e.position;
        sendExpression (t, e, true);
        processor.addUiMidi (juce::MidiMessage::noteOn (t.channel, t.note, (juce::uint8) 100));
        touches.push_back (t);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int src = e.source.getIndex();
        if (shiftDrag && e.source.isMouse())
        {
            zoomAround (gestureStartZoom * std::exp ((e.position.x - gestureStartX) / 160.0f), gestureStartX, gestureAnchorNote);
            return;
        }
        if (fingers.count (src) > 0)
        {
            fingers.erase (src);
            fingers.emplace (src, e);
        }
        if (pinchAllowed() && pinchSources.empty() && fingers.size() == 2 && pinchStartDistance > 0.0f
            && std::abs (fingerDistance() - pinchStartDistance) > juce::jmax (28.0f, getKeyWidth() * 0.8f))
            startPinch();
        if (pinchSources.count (src) > 0)
        {
            if (pinchSources.size() == 2 && fingers.size() == 2)
                zoomAround (gestureStartZoom * fingerDistance() / pinchStartDistance, fingerMidX(), gestureAnchorNote);
            return;   // the pinching fingers play nothing
        }
        if (! mpe()) { juce::MidiKeyboardComponent::mouseDrag (e); return; }
        for (auto& t : touches)
            if (t.source == e.source.getIndex())
            {
                t.pos = e.position;
                sendExpression (t, e, false);
                repaint();
            }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int src = e.source.getIndex();
        if (shiftDrag && e.source.isMouse())
        {
            shiftDrag = false;
            if (onZoomFinished != nullptr) onZoomFinished();
            return;
        }
        fingers.erase (src);
        if (fingers.size() < 2)
            pinchStartDistance = 0.0f;
        if (pinchSources.count (src) > 0)
        {
            pinchSources.erase (src);
            if (pinchSources.empty() && onZoomFinished != nullptr)
                onZoomFinished();
            return;   // its note went when the pinch began
        }
        if (! mpe() && touches.empty()) { juce::MidiKeyboardComponent::mouseUp (e); return; }
        release (e.source.getIndex());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        juce::MidiKeyboardComponent::paint (g);
        for (const auto& t : touches)
        {
            const auto glow = juce::Colour (0xff8dffb0);
            g.setColour (glow.withAlpha (0.6f));
            g.drawLine (t.start.x, t.start.y, t.pos.x, t.pos.y, 2.0f);
            const float r = 10.0f + 10.0f * t.pressure;
            g.setColour (glow.withAlpha (0.28f));
            g.fillEllipse (t.pos.x - r, t.pos.y - r, 2.0f * r, 2.0f * r);
            g.setColour (glow);
            g.fillEllipse (t.pos.x - 5.0f, t.pos.y - 5.0f, 10.0f, 10.0f);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            const float semis = t.bend * 48.0f;
            if (std::abs (semis) >= 0.05f)
                g.drawText ((semis > 0 ? "+" : "") + juce::String (semis, 1),
                            juce::Rectangle<float> (t.pos.x - 24.0f, t.pos.y - r - 14.0f, 48.0f, 12.0f), juce::Justification::centred);
        }
    }

    // MPE switched off (or on) while fingers are down: let every note go
    void stopAll()
    {
        while (! touches.empty())
            release (touches.back().source);
        repaint();
    }

private:
    struct Touch
    {
        int source = 0, note = 60, channel = 2;
        juce::Point<float> start, pos;
        float bend = 0.0f, slide = 0.0f, pressure = 0.0f;
        int sentBend = -1, sentSlide = -1, sentPressure = -1;
    };

    bool mpe() const { return processor.mpeEnabled.load (std::memory_order_relaxed); }

    // two fingers may zoom (the "<- ->" pill beside the grip); off, they always play
    bool pinchAllowed() const { return processor.uiKeyPinch != 0; }

    // the zoom (1 = the window's own width for the keys)
    static constexpr float kMinZoom = 0.35f, kMaxZoom = 4.0f;
    float zoom() const { return processor.uiKeyZoom > 0.0f ? processor.uiKeyZoom : 1.0f; }
    float keyWidthForZoom (float z) const
    {
        const float widest = juce::jmax (8.0f, (float) getWidth() / 4.0f);   // at least four white keys in view
        return juce::jlimit (8.0f, widest, baseKeyWidth * z);
    }
    // where a point lies along the keys, in semitones (a white key's width is 12/7 of one)
    float noteAtX (float x) const { return (float) getLowestVisibleKey() + x / (getKeyWidth() * 7.0f / 12.0f); }

    void zoomAround (float newZoom, float anchorX, float anchorNote)
    {
        processor.uiKeyZoom = juce::jlimit (kMinZoom, kMaxZoom, newZoom);
        const float w = keyWidthForZoom (processor.uiKeyZoom);
        setKeyWidth (w);
        setLowestVisibleKey (juce::jlimit (0, 127, juce::roundToInt (anchorNote - anchorX / (w * 7.0f / 12.0f))));
        repaint();
    }

    void finishSoon()
    {
        const int generation = ++zoomGeneration;
        juce::Component::SafePointer<ExpressiveKeyboard> safe (this);
        expressive_keyboard_detail::callAfterDelay (700, [safe, generation]
        {
            if (safe != nullptr && safe->zoomGeneration == generation && safe->onZoomFinished != nullptr)
                safe->onZoomFinished();
        });
    }

    float fingerDistance() const
    {
        if (fingers.size() < 2) return 0.0f;
        auto it = fingers.begin();
        const auto a = it->second.position;
        const auto b = (++it)->second.position;
        return std::abs (a.x - b.x);   // the keys run sideways: only the sideways spread counts
    }
    float fingerMidX() const
    {
        float x = 0.0f;
        for (const auto& f : fingers) x += f.second.position.x;
        return fingers.empty() ? 0.0f : x / (float) fingers.size();
    }

    void startPinch()
    {
        for (const auto& f : fingers)
        {
            pinchSources.insert (f.first);
            if (mpe()) release (f.first);
            else       juce::MidiKeyboardComponent::mouseUp (f.second);   // lets that finger's note go
        }
        // the anchor where the fingers are now, at the zoom they started from
        gestureAnchorNote = noteAtX (fingerMidX());
        pinchStartDistance = juce::jmax (20.0f, fingerDistance());
        gestureStartZoom = zoom();
        repaint();
    }

    float baseKeyWidth { 20.0f };
    std::map<int, juce::MouseEvent> fingers;   // touches down, by source, as last seen
    std::set<int> pinchSources;
    float pinchStartDistance { 0.0f }, gestureStartX { 0.0f }, gestureStartZoom { 1.0f }, gestureAnchorNote { 60.0f };
    bool shiftDrag { false };
    int zoomGeneration { 0 };

    int freeChannel()
    {
        for (int i = 0; i < 15; ++i)
        {
            const int ch = 2 + (nextChannel + i) % 15;
            bool used = false;
            for (const auto& t : touches) used = used || t.channel == ch;
            if (! used) { nextChannel = (ch - 2 + 1) % 15; return ch; }
        }
        return 2 + (nextChannel++ % 15);
    }

    void sendExpression (Touch& t, const juce::MouseEvent& e, bool first)
    {
        const float semitoneW = juce::jmax (4.0f, getKeyWidth() * 7.0f / 12.0f);
        t.bend = juce::jlimit (-1.0f, 1.0f, (t.pos.x - t.start.x) / semitoneW / 48.0f);
        t.slide = juce::jlimit (0.0f, 1.0f, 1.0f - t.pos.y / (float) juce::jmax (1, getHeight()));
        if (e.isPressureValid()) t.pressure = juce::jlimit (0.0f, 1.0f, e.pressure);
        const int bendValue = juce::jlimit (0, 16383, 8192 + juce::roundToInt (t.bend * 8191.0f));
        const int slideValue = juce::roundToInt (t.slide * 127.0f), pressValue = juce::roundToInt (t.pressure * 127.0f);
        if (first || bendValue != t.sentBend)
            processor.addUiMidi (juce::MidiMessage::pitchWheel (t.channel, bendValue));
        if (first || slideValue != t.sentSlide)
            processor.addUiMidi (juce::MidiMessage::controllerEvent (t.channel, 74, slideValue));
        if (first || pressValue != t.sentPressure)
            processor.addUiMidi (juce::MidiMessage::channelPressureChange (t.channel, pressValue));
        t.sentBend = bendValue;
        t.sentSlide = slideValue;
        t.sentPressure = pressValue;
    }

    void release (int source)
    {
        for (size_t i = 0; i < touches.size(); ++i)
            if (touches[i].source == source)
            {
                processor.addUiMidi (juce::MidiMessage::noteOff (touches[i].channel, touches[i].note));
                touches.erase (touches.begin() + (long) i);
                return;
            }
    }

    AquanodeModularAudioProcessor& processor;
    std::vector<Touch> touches;
    int nextChannel { 0 };
};
