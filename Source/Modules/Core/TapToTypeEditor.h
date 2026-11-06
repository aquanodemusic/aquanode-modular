#pragma once

#include <JuceHeader.h>

namespace aquanode
{
//==============================================================================
// A text field that takes the keyboard focus only when it is tapped (or
// clicked), and lets go of it again when it loses it. JUCE hands the focus of
// a component that goes away (a card being rebuilt, a control hidden while
// the Piano Roll is resized) to the first child that wants it - an ordinary
// TextEditor, which on Android then opens the on-screen keyboard out of
// nowhere. This one never wants the focus unless someone means to type.
//==============================================================================
class TapToTypeEditor : public juce::TextEditor
{
public:
    TapToTypeEditor() { setWantsKeyboardFocus (false); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        setWantsKeyboardFocus (true);
        grabKeyboardFocus();
        juce::TextEditor::mouseDown (e);
    }
    void focusLost (FocusChangeType cause) override
    {
        juce::TextEditor::focusLost (cause);
        setWantsKeyboardFocus (false);
    }
};
}
