#pragma once

// The editor: the whole window, made of the parts in this folder.

#include "ModuleCard.h"
#include "PatchCanvas.h"
#include "SidebarHeader.h"
#include "ThemePanel.h"
#include "ModulationTable.h"
#include "Sidebar.h"
#include "MutatorPanel.h"
#include "KeyboardBar.h"

//==============================================================================
// text drawn straight onto the patch area: light on the dark default, dark on a light background
inline juce::Colour overPatch()
{
    const auto& t = aquanode::themeColours();
    return t.textOn (t.get (aquanode::ThemeColours::Background));
}

class AquanodeModularAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::ChangeListener
{
public:
    explicit AquanodeModularAudioProcessorEditor (AquanodeModularAudioProcessor&);
    ~AquanodeModularAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    PatchCanvas& getCanvas() { return canvas; }

    void addPluginGui (const juce::String& pluginId);
    void buildPluginPatch (const juce::String& pluginId, const juce::File& presetFile, bool replace);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    AquanodeModularAudioProcessor& processor;

    // Desktop: the window resizes freely; everything in it scales uniformly
    // with the smaller of its width and height against kBaseWidth x
    // kBaseHeight, and the layout fills whatever room is left over (a wider
    // window gives the patch area more width, not a stretched picture).
    static constexpr int kBaseWidth = 1200, kBaseHeight = 700;
    float uiScale { 1.0f };

    SidebarComponent sidebar;
    ModulationTable modTable { processor };   // at the end of the sidebar
    SidebarHeader sidebarHeader;
    std::unique_ptr<ThemePanel> themePanel;

    // Render to File: its panel (format, tempo, sample rate, then progress)
    // and the render running in the background
    std::unique_ptr<juce::Component> renderPanel;
    std::unique_ptr<aquanode::render::Job> renderJob;
    juce::File renderTemp;
    void showRenderPanel();
    void chooseRenderFile (const aquanode::render::Settings& settings);
    void startRender (const aquanode::render::Settings& settings, const juce::File& destination,
                      const juce::URL& document, const juce::String& shownName);
    void finishRender (const juce::File& destination, const juce::URL& document, const juce::String& shownName);
    void closeRenderPanel();
    void showThemePanel();
    void coloursChanged();
    void applyBarColours();   // the top bar's and the mutator's buttons in the colourway
    KeyboardHeightHandle keyboardHandle;
    KeyZoomToggle keyZoomToggle;    // beside the grip: the keys' pinch zoom on / off
    juce::Viewport sidebarViewport;   // makes the sidebar scrollable when its content exceeds the visible height
    PatchCanvas canvas;

    // Dark bar across the top of the patch area, mirroring the mutator strip at
    // the bottom. Purely the background: the buttons stay editor children so
    // their existing wiring is untouched, and they are added after this so they
    // sit in front of it.
    // The top bar. Its buttons live in a viewport: on a wide screen they share the
    // width as before, on a narrow one each keeps the width its text needs and
    // the bar scrolls sideways (swipe, wheel), with a soft fade at an edge that
    // has more beyond it.
    struct ToolbarStrip : public juce::Component
    {

        struct Port : public juce::Viewport
        {
            std::function<void()> moved;
            void visibleAreaChanged (const juce::Rectangle<int>&) override { if (moved) moved(); }
        };

        ToolbarStrip()
        {
            port.setViewedComponent (&content, false);
            port.setScrollBarsShown (false, false, false, true);
            port.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
            port.moved = [this] { repaint(); };
            addAndMakeVisible (port);
        }

        void paint (juce::Graphics& g) override
        {
            if (aquanode::themeColours().glass)
            {
                // Glass: a frosted pane over the jungle, a sheen from the top, a pale edge below
                const auto b = getLocalBounds().toFloat();
                g.fillAll (aquanode::barColour().withAlpha (0.55f));
                g.fillAll (juce::Colours::white.withAlpha (0.07f));
                juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.14f), 0.0f, b.getY(),
                                            juce::Colours::white.withAlpha (0.02f), 0.0f, b.getBottom(), false);
                g.setGradientFill (sheen);
                g.fillRect (b);
                g.setColour (juce::Colours::white.withAlpha (0.26f));
                g.fillRect (b.withTop (b.getBottom() - 1.0f));
                return;
            }
            g.fillAll (aquanode::barColour().withAlpha ((juce::uint8) 0xdd));
            if (aquanode::themeColours().skeuomorphic)
            {
                // Skeuomorphic: a bar of glass, brighter over its upper half
                const auto b = getLocalBounds().toFloat();
                juce::ColourGradient shine (juce::Colours::white.withAlpha (0.15f), 0.0f, b.getY(),
                                            juce::Colours::white.withAlpha (0.02f), 0.0f, b.getCentreY(), false);
                g.setGradientFill (shine);
                g.fillRect (b.withHeight (b.getHeight() * 0.5f));
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRect (b.withHeight (1.0f));
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRect (b.withTop (b.getBottom() - 1.0f));
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            const int x = port.getViewPositionX();
            const int fade = juce::jmin (36, getWidth() / 6);
            const auto solid = aquanode::barColour();
            if (x > 0)
            {
                g.setGradientFill (juce::ColourGradient (solid, 0.0f, 0.0f, solid.withAlpha (0.0f), (float) fade, 0.0f, false));
                g.fillRect (0, 0, fade, getHeight());
            }
            if (x + port.getWidth() < content.getWidth())
            {
                const float r = (float) getWidth();
                g.setGradientFill (juce::ColourGradient (solid.withAlpha (0.0f), r - (float) fade, 0.0f, solid, r, 0.0f, false));
                g.fillRect (getWidth() - fade, 0, fade, getHeight());
            }
        }

        void resized() override { port.setBounds (getLocalBounds()); }

        Port port;
        juce::Component content;   // holds the buttons
    };
    ToolbarStrip toolbarStrip;

    juce::TextButton undoPatchButton { "Undo" };
    juce::TextButton redoPatchButton { "Redo" };
    void undoRedo (bool redo);
    void refreshUndoButtons();
    // takes finished edits into the patch's history (Undo / Redo), a few times a second
    struct HistoryTimer : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    } historyTimer;

    juce::TextButton initButton    { "Initialize Patch" };
    juce::TextButton regionButton  { "Region Select" };   // a drag on empty space selects (PatchCanvas::setRegionSelect)
    juce::TextButton moveOnlyButton { "Move Only" };      // every gesture moves and zooms the view (PatchCanvas::setMoveOnly)
    juce::TextButton cloneButton   { "Clone Selected" };
    juce::TextButton deleteButton  { "Delete Selected" };
    juce::TextButton exportButton  { "Save Patch" };
    juce::TextButton importButton  { "Load Patch" };
    juce::TextButton renderButton  { "Render" };
    juce::TextButton pluginBlockButton { "CPU" };   // the CPU menu: the plugins' block, CPU Sleep, the Android hint (app-wide)
    juce::TextButton cpuSleepButton { "CPU Sleep" };      // sleep on silence for effects and filters (app-wide)
    void showPluginBlockMenu();

    // 6.7.1: the top bar's menus. The buttons they replace stay as the ones
    // doing the work (never shown): a menu item presses its button.
    juce::TextButton patchMenuButton { "Patch" };   // Initialize, Import, Export, Render
    juce::TextButton editMenuButton  { "Edit" };    // Clone, Delete, Region Select, Move Only

    // (6.9.10) the computer keyboard as a MIDI keyboard (desktop; Edit menu,
    // off unless switched on, remembered): Z S X D C V G B H N J M , = C4 up,
    // Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P = C5 up, into the played keys
    struct ComputerKeys;
    std::unique_ptr<ComputerKeys> computerKeys;
    void setComputerKeys (bool on);
    juce::TextButton viewMenuButton  { "View" };    // Keys, Sidebar, Mutator
    void showPatchMenu();
    void showEditMenu();
    void showViewMenu();
    void pressHidden (juce::TextButton& b);          // as if it had been clicked (a toggle flips first)
    void refreshEditMenuButton();                    // "Edit (Region)" / "Edit (Move Only)" while one is on
    void parentHierarchyChanged() override;          // the standalone window: its maximise button

    // the patch's tempo: drag up / down on it, or tap to type one; shows the
    // host's tempo (and leaves it alone) where a DAW gives one
    struct BpmBox : public juce::TextButton, private juce::Timer
    {
        explicit BpmBox (AquanodeModularAudioProcessor& p);
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void timerCallback() override { refresh(); }
        void refresh();
        int tempoModule (bool& modulated) const;   // (6.7.9: a Tempo module turns with the box, or locks it)
        bool locked() const;
        void setTempo (double bpm);
        AquanodeModularAudioProcessor& proc;
        double dragStart = 120.0;
        bool dragged = false;
        std::unique_ptr<juce::AlertWindow> entry;
    };
    BpmBox bpmBox { processor };
    juce::TextButton mutatorButton { "Mutator" };

    // the standalone app on a desktop: the window over the whole screen (F11 too)
    bool hasFullScreenButton() const;
    void toggleFullScreen();

    // Full Size: a plugin GUI card's own editor alone over the whole window, as
    // if that plugin were loaded on its own - on the desktop the window takes
    // the plugin's size, on Android it fills the screen (scaled, with borders)
    class PluginFullView;
    std::unique_ptr<PluginFullView> pluginFullView;
    juce::Rectangle<int> sizeBeforeFullView;
    void showPluginFullSize (int instanceId);
    // Full Size: the on-screen keyboard over the bottom of the plugin, in real
    // pixels (and its grip's height), 0 while the keyboard is hidden
    int fullViewKeyboardHeight (int windowHeight) const;
    int fullViewGripHeight() const;
    void closePluginFullSize();

    // full screen for any card (6.7.2): the card itself borrowed into a view
    // over the patch, as large as fits - black borders around it on Android, the
    // window taking its shape on a desktop. A plugin card goes to Full Size.
    class ModuleFullView;
    std::unique_ptr<ModuleFullView> moduleFullView;
    void toggleModuleFullScreen (int instanceId);
    void closeModuleFullScreen();
    juce::Component* currentFullView() const;

    // Panel toggles. Hiding the sidebar and the keyboard gives the patch area
    // the whole screen, which is the difference between usable and unusable on
    // a phone. Both are pure layout changes - resized() re-runs and the canvas
    // simply gets more room.
    juce::TextButton sidebarButton { "Sidebar" };
    juce::TextButton keysButton    { "Keys" };
    bool sidebarVisible { true };
    bool keyboardVisible { true };

    // The sidebar's right edge (6.7.4): dragged to the left it slides the
    // sidebar out of view - its contents move along at their own size - and
    // back up to its full width, no further. Let go nearly out of view, the
    // sidebar is hidden; then a small tab at the left edge brings it back (a
    // tap: all of it, a drag: as far as it is pulled).
    class SidebarEdge final : public juce::Component
    {
    public:
        std::function<void()> onDragStart, onDragEnd, onTap;
        std::function<void (int screenDx)> onDrag;
        bool tab { false };   // the sidebar hidden: the tab at the left edge

        juce::Rectangle<float> grip() const
        {
            const float h = juce::jmin ((float) getHeight(), tab ? 64.0f : 56.0f);
            return juce::Rectangle<float> ((float) getWidth(), h).withCentre (getLocalBounds().toFloat().getCentre());
        }
        bool hitTest (int x, int y) override
        {
            if (grip().contains ((float) x, (float) y)) return true;
            // the edge itself along its whole height, on the patch side (the
            // sidebar's scroll bar stays its own)
            return ! tab && x >= getWidth() / 2 && x <= getWidth() / 2 + 5;
        }
        void paint (juce::Graphics& g) override
        {
            const auto gr = grip();
            if (tab)
            {
                juce::Path p;
                p.addRoundedRectangle (gr.getX() - 8.0f, gr.getY(), gr.getWidth() + 8.0f, gr.getHeight(), 7.0f);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillPath (p);
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
                juce::Path chev;   // >
                const auto c = gr.getCentre();
                chev.startNewSubPath (c.x - 2.5f, c.y - 6.0f);
                chev.lineTo (c.x + 3.0f, c.y);
                chev.lineTo (c.x - 2.5f, c.y + 6.0f);
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.strokePath (chev, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            // a pill on the edge with two little lines
            const auto pill = gr.withSizeKeepingCentre (juce::jmin (gr.getWidth(), 10.0f), gr.getHeight());
            g.setColour (juce::Colours::black.withAlpha (dragging ? 0.6f : 0.4f));
            g.fillRoundedRectangle (pill, pill.getWidth() * 0.5f);
            g.setColour (juce::Colours::white.withAlpha (dragging ? 0.75f : 0.45f));
            g.drawRoundedRectangle (pill, pill.getWidth() * 0.5f, 1.0f);
            for (float dx : { -1.6f, 1.6f })
                g.drawLine (pill.getCentreX() + dx, pill.getCentreY() - 9.0f, pill.getCentreX() + dx, pill.getCentreY() + 9.0f, 1.0f);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            downX = e.getScreenX();
            dragging = false;
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int dx = e.getScreenX() - downX;
            if (! dragging && std::abs (dx) < 4)
                return;
            if (! dragging)
            {
                dragging = true;
                if (onDragStart) onDragStart();
            }
            if (onDrag) onDrag (dx);
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging) { dragging = false; if (onDragEnd) onDragEnd(); repaint(); }
            else if (onTap) onTap();
        }
        juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::LeftRightResizeCursor; }

    private:
        int downX { 0 };
        bool dragging { false };
    };
    SidebarEdge sidebarEdge;
    int sidebarShift { 0 };          // how far it is slid out to the left (layout pixels, 0 .. its width)
    int sidebarShiftAtDrag { 0 };
    int sidebarFullWidth { 200 };    // its width, as the last layout made it
    void showSidebar (bool show);
    void wireNewSynthToOutput (int newId);   // (a sound generator into Audio Out, where the patch has none)

    // Zoom controls: the [-][+] buttons live in the top toolbar, right next
    // to Mutator. The percentage is shown in the sidebar under its title
    // (SidebarHeader); a tap on it resets the view to 100 % at the origin.
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomInButton  { "+" };
    void updateZoomReadout();

    MutatorPanel mutatorPanel;

    // Plugins rows: the GUI card, or the patch version's menu

    void showPluginPatchMenu (const juce::String& pluginId);
    void pickPluginPreset (const juce::String& pluginId, bool replace);

    // generator rows: a fresh random patch, or the generator's menu
    void runGenerator (const juce::String& generatorId, bool openMenu);
    void applyGenerated (std::function<juce::String (AquanodeModularAudioProcessor&)> build);

    // Bottom on-screen keyboard, on every build: the Keys button shows and
    // hides it (hidden, the patch area gets its height back). Host and
    // hardware MIDI keep working either way and light up its keys.
    std::unique_ptr<juce::MidiKeyboardComponent> midiKeyboard;   // an ExpressiveKeyboard (MPE)

    std::unique_ptr<juce::FileChooser> fileChooser;
    int placementCounter { 0 };

    // where a module added from the sidebar goes: near the top left of the
    // part of the patch in view, wherever it is scrolled and zoomed
    juce::Point<int> spawnPosition();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquanodeModularAudioProcessorEditor)
};
