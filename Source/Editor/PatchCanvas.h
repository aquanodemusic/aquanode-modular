#pragma once

// The patch area: cards, cables, zoom, drag and drop.

#include "ModuleCard.h"

//==============================================================================
// The patch map: gray background with a "+" grid, holds the module
// rectangles, draws all cables, handles cable dragging and selection.
class PatchCanvas : public juce::Component
{
public:
    explicit PatchCanvas (AquanodeModularAudioProcessor& proc);

    void rebuild();                // sync child components with the processor model
    void refreshAllModuleValues(); // pull every module's knob values from the DSP

    int getSelectedModuleId() const { return selectedModuleId; }
    void selectModule (int instanceId);

    //=== Region Select (the top bar's switch) ===============================
    // On, a drag on empty space draws a box instead of moving the view: every
    // module the box touches is selected. Dragging one of them moves them all
    // by the same amount, Clone Selected and Delete Selected (or the Delete
    // key) act on all of them, and a dropped module is never patched into what
    // it lands on. Two fingers still move and zoom the view (the middle mouse
    // button moves it too).
    void setRegionSelect (bool on);
    bool isRegionSelect() const { return regionSelect; }
    std::vector<int> getSelection() const;          // every selected module (the one tapped, or the box's)
    bool isSelected (int instanceId) const;
    void moduleClicked (int instanceId);            // a press on a card: keeps a box selection that holds it
    void moveSelectionWith (int movedId, juce::Point<int> delta);   // the others follow the dragged one
    void setSelection (const std::vector<int>& ids);

    // cable dragging (driven by ModuleComponent mouse events)
    void beginCableDrag (int moduleId, const juce::String& socketId, bool isInput,
                         aquanode::SocketKind kind, juce::Point<int> canvasPos);
    void updateCableDrag (juce::Point<int> canvasPos);
    void endCableDrag (juce::Point<int> canvasPos);
    bool isDraggingCable() const { return dragActive; }

    void moduleMoved();            // repaint cables while a module is dragged

    // A module dragged by its body is patched in where it is dropped:
    //  - onto a cable it fits (audio in and out on an audio cable, MIDI in and
    //    out on a MIDI cable): in between the cable's two modules, beside the
    //    cable, which stays
    //  - an Audio Out into a module with an audio output: that output into it
    //  - a module with a MIDI output into one with Add Midi In (or Midi In):
    //    its output into that input; a MIDI processor into a module with a
    //    MIDI output: fed by it; a sound into the Audio Out, or into a free
    //    audio input; an effect into a sound: fed by it
    //  - envelopes, LFOs, KeyTrack, Velocity, clocks, Step Seq, Midi Controls
    //    and VirtualDX7's modulators into the input of the module that fits
    //    them (Env In, Pitch In, a filter's cutoff, Trig In, Clock In...)
    //  - a modulation source right on a knob: a knob cable (25 %)
    //  - Quantize, Slew, DC Offset or S&H onto a modulation or knob cable: in
    //    series, replacing it; Tuner, Env Follow, the analysers and Record
    //    onto an audio cable: listening to it only
    // While it is dragged, the cable and the modules it would connect light up.
    // canvasPos is the pointer, in canvas (screen) coordinates.
    void moduleDragged (int moduleId, juce::Point<int> canvasPos);
    void moduleDropped (int moduleId);
    struct DropPlan
    {
        int moduleId { -1 };          // the module dragged
        int cable { -1 };             // the cable it goes beside (or replaces, removeCable)
        int paramCable { -1 };        // a knob cable it goes into (replacing it, removeParamCable)
        int target { -1 };            // the module it is dropped into
        juce::String knob;            // ... onto this knob of it
        int removeCable { -1 }, removeParamCable { -1 };
        std::vector<CableInfo> adds;  // the cables a drop makes
        std::vector<ParamCableInfo> paramAdds;
        std::vector<int> lit;         // the modules shown lit
        bool empty() const { return adds.empty() && paramAdds.empty(); }
    };
    DropPlan planDrop (int moduleId, juce::Point<float> world) const;   // world coordinates
    const DropPlan& currentDrop() const { return drop; }

    juce::Point<int> getPanOffset() const { return panOffset; }
    void resetView();              // recentre the patch (pan back to origin, zoom to 100%)
    void fitToContent();           // zoom and pan so every module is visible (after a generated patch)

    //=== view zoom ============================================================
    // Zoom is a pure VIEW transform, never a re-layout: module bounds stay in
    // world coordinates and every module carries the same AffineTransform.
    // JUCE inverts that transform for hit-testing and for the coordinates in
    // every MouseEvent, so knobs, cable drags and ComponentDragger all keep
    // working at any zoom with no scaling maths of their own - a 10 px finger
    // move at 50 % becomes a 20 unit world move, and the module still tracks
    // the finger exactly. Nothing is re-measured, so no layout can break.
    static constexpr float minZoom = 0.1f;        // pinch / wheel / buttons
    static constexpr float minFitZoom = 0.2f;     // fitting a patch on screen goes no smaller
    static constexpr float maxZoom = 2.0f;

    float getZoom() const { return zoom; }

    // keeps the world point currently under focusScreenPos pinned there
    void setZoom (float newZoom, juce::Point<float> focusScreenPos);
    void zoomBy (float factor);    // about the centre of the visible canvas

    juce::AffineTransform worldToScreen() const
    {
        return juce::AffineTransform::scale (zoom)
                   .translated ((float) panOffset.x, (float) panOffset.y);
    }

    juce::Point<float> screenToWorld (juce::Point<float> p) const
    {
        return { (p.x - (float) panOffset.x) / zoom,
                 (p.y - (float) panOffset.y) / zoom };
    }

    // called by the editor when a module is collapsed/expanded
    void moduleCollapsedChanged() { repaint(); }

    // replaces a plugin's GUI card by its patch version (async: the card that
    // asked is deleted by the rebuild)
    void convertPluginCard (int instanceId);

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    std::function<bool (const juce::KeyPress&)> onShortcut;   // the editor's (Undo / Redo)
    void resized() override;

private:
    ModuleComponent* findModuleComponent (int instanceId) const;
    juce::Path cablePath (juce::Point<float> from, juce::Point<float> to) const;
    bool getCableEndpoints (const CableInfo& c, juce::Point<float>& from, juce::Point<float>& to) const;
    int cableIndexNear (juce::Point<float> pos, float maxDistance = 9.0f) const;
    int paramCableIndexNear (juce::Point<float> pos, float maxDistance = 6.0f) const;
    void applyViewToChildren();    // push world position + view transform onto every module
    void rebuildGrid();
    float gridScreenSpacing() const;

    AquanodeModularAudioProcessor& processor;
    std::vector<std::unique_ptr<ModuleComponent>> moduleComponents;

    int selectedModuleId { -1 };
    std::vector<int> regionSelection;   // the box's modules (Region Select)
    bool regionSelect { false };
    bool regionDragging { false };
    juce::Point<float> regionStart, regionEnd;   // screen coordinates
    void updateRegionSelection();
    int selectedCableIndex { -1 };
    int selectedParamCableIndex { -1 };   // knob-modulation cable under the mouse

    // live cable drag state
    bool dragActive { false };
    int dragModuleId { -1 };
    juce::String dragSocketId;
    bool dragFromInput { false };
    aquanode::SocketKind dragKind { aquanode::SocketKind::Audio };
    juce::Point<int> dragCurrentPos;

    juce::Path gridPath;

    DropPlan drop;   // what the module being dragged would be patched into

    // dashed cables, kept while their ends stay put: a module display repainting
    // 20 times a second would otherwise dash every cable of the patch anew each time
    struct DashedCable { juce::Point<float> from, to; juce::Path dashed; };
    mutable std::vector<DashedCable> dashedCables, dashedParamCables;
    const juce::Path& dashedCable (std::vector<DashedCable>& cache, size_t index, juce::Point<float> from,
                                   juce::Point<float> to, float dash, float gap) const;

    // view panning (drag on empty canvas) + zoom
    float zoom { 1.0f };
    juce::Point<int> panOffset;
    bool panning { false };
    juce::Point<int> panStartOffset;
    juce::Point<int> panMouseStart;

    // two-finger pinch (touch screens): true when the event was part of it
    bool pinchTouch (const juce::MouseEvent& e, bool down, bool up);
    struct Touch { int index; juce::Point<float> pos; };
    std::vector<Touch> touches;
    bool pinching { false };
    float pinchStartZoom { 1.0f }, pinchStartDist { 1.0f };
    juce::Point<float> pinchStartMid;
    juce::Point<int> pinchStartPan;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchCanvas)
};
