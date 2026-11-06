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

    // Desktop: the window resizes with a fixed aspect ratio and everything in it
    // scales uniformly - the layout always works on a kBaseWidth x kBaseHeight
    // area, and every child is drawn through a scale of uiScale.
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
    juce::TextButton cloneButton   { "Clone Selected" };
    juce::TextButton deleteButton  { "Delete Selected" };
    juce::TextButton exportButton  { "Export Patch" };
    juce::TextButton importButton  { "Import Patch" };
    juce::TextButton renderButton  { "Render" };
    juce::TextButton mutatorButton { "Mutator" };

    // Panel toggles. Hiding the sidebar and the keyboard gives the patch area
    // the whole screen, which is the difference between usable and unusable on
    // a phone. Both are pure layout changes - resized() re-runs and the canvas
    // simply gets more room.
    juce::TextButton sidebarButton { "Sidebar" };
    juce::TextButton keysButton    { "Keys" };
    bool sidebarVisible { true };
    bool keyboardVisible { true };

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
