#pragma once

// A module's card in the patch area (ModuleComponent).

#include <JuceHeader.h>
#include "../Engine/PluginProcessor.h"
#include "../Modules/Core/TapToTypeEditor.h"
#include "../Engine/PatchRender.h"

class PatchCanvas;

//==============================================================================
// Generic module rectangle - built entirely from the instance's
// ModuleDescriptor. No module-specific UI code lives here.
class ModuleComponent : public juce::Component
{
public:
    ModuleComponent (AquanodeModularAudioProcessor& proc, PatchCanvas& canvas, int instanceId);

    int getInstanceId() const { return instanceId; }

    struct SocketRef
    {
        juce::String socketId;
        bool isInput { false };
        aquanode::SocketKind kind { aquanode::SocketKind::Audio };
        juce::Point<int> centre;   // local coords
    };

    // returns nullptr if no socket is near the given local point
    const SocketRef* findSocketNear (juce::Point<int> localPos, int radius = 14) const;
    juce::Point<int> socketCentreInParent (const juce::String& socketId, bool isInput) const;

    // knob modulation: hit-test rotary knobs that accept param cables
    const aquanode::ParamSpec* findModulatableKnobNear (juce::Point<int> localPos) const;
    juce::Point<int> knobCentreInParent (const juce::String& paramId) const;
    void showKnobModMenu (const juce::String& paramId);   // right-click: depth / remove
    bool knobHasModulation (const juce::String& paramId) const;

    // touch-only long-press state (desktop uses right-click and never arms these)
    juce::String longPressParamId;
    bool longPressMoved { false };

    // the card's own colour: right click on its title, or hold the title still
    void showColourPicker();
    juce::Rectangle<int> titleArea() const;
    int titlePressId { 0 };   // the press a pending hold belongs to

    void paintOverChildren (juce::Graphics& g) override;   // modulation rings

    void applySkin();              // the Skeuomorphic look on its controls, or the standard one
    void refreshLayout();          // recompute (visibility may have changed) and resize
    void refreshFromModel();       // pull all knob/combo values from the DSP (mutator etc.)
    void refreshControlValues();   // knob values only, no relayout

    //=== collapse (hide the controls, keep the header and its sockets) ========
    // Collapsed modules shrink to their header, so a big patch stays readable
    // without losing any routing: every socket is still there and every audio
    // cable still lands. Knob-modulation cables are the one thing that cannot
    // survive, since their target has no on-screen position while hidden - so
    // they are simply not drawn, and reappear untouched on the same knob when
    // the module is opened again. The cable itself is never removed.
    bool isCollapsed() const;
    void toggleCollapsed();

    //=== the card's own Random button (left of the collapse tab) ============
    // "Random" mutates this card's knobs by the Mutator's amount (25 % to
    // start); the button then reads "Revert" and puts them back. One after the
    // other - a second Random only after the first was reverted - so each
    // change stays traceable.
    bool showsRandomize() const;
    juce::Rectangle<int> randomizeHitArea() const;
    void randomizeCard();
    void undoCard();

    // GUI cards of the merged plugins: the "To Patch" pill in the header,
    // shown when that plugin has a patch version
    juce::Rectangle<int> convertPillArea() const;
    bool showsConvertPill() const;

    // false while collapsed, or when the knob is hidden by a visibleWhen rule.
    // The canvas asks before drawing (or hit-testing) a param cable.
    bool isKnobVisible (const juce::String& paramId) const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr int defaultModuleWidth = 250;

    // 250 for almost everything; a module can ask for a wider card (the
    // Piano Roll, whose grid lives inside it and can be resized)
    int moduleWidth() const;
    int maxRowUnits() const;

private:
    struct ControlEntry
    {
        const aquanode::ParamSpec* spec { nullptr };
        std::unique_ptr<juce::Component> component;
        juce::Rectangle<int> labelArea;
        bool visible { true };
    };

    bool isParamVisible (const aquanode::ParamSpec& spec) const;
    int layoutEverything (bool apply);   // returns total height

    // the little collapse tab drawn above the title. The drawn rectangle is
    // deliberately smaller than the area this returns: the hit target has to
    // stay thumb-sized on a phone without a chunky bar spoiling the header.
    juce::Rectangle<int> collapseHitArea() const;
    juce::Rectangle<int> collapseTabArea() const;
    void buildControls();

    AquanodeModularAudioProcessor& processor;
    PatchCanvas& canvas;
    const int instanceId;

    std::vector<ControlEntry> controls;
    std::vector<SocketRef> sockets;
    std::unique_ptr<juce::Component> extraContent;
    int headerHeight { 0 };
    std::vector<std::pair<int, juce::String>> sectionHeaders;   // y and title of each separator (rowSections)

    juce::ComponentDragger dragger;
    bool draggingCable { false };
    bool draggingBody { false };

    // triple-click self-cable delete: three rapid clicks anywhere inside the
    // module (without the mouse wandering) remove every cable the module has
    // routed back into itself. Self-modulation cables start and end on the
    // same rectangle, which makes them impossible to select on the canvas -
    // this gesture is their delete path. Regular cables are untouched.
    bool handleRapidClick (const juce::MouseEvent& e);   // true = self cables were deleted
    juce::int64 lastClickTimeMs { 0 };
    juce::Point<int> lastClickScreenPos;
    int rapidClickCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleComponent)
};
