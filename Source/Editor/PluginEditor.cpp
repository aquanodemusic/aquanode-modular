// The editor: the top bar, the sidebar, the patch area and the keys put together,
// Import / Export Patch, and adding the plugins.
#include "../Modules/Shared/AntiAlias.h"
#include "../Platform/AndroidPerfHint.h"
#include "PluginEditor.h"
#include "../Engine/Mutation.h"
#include "../Presets/EffectAdders.h"
#include "../Presets/PresetGenerators.h"
#include "../Plugins/Host/PluginCatalog.h"
#include "../Plugins/Host/HostedPlugin.h"
#include "../Platform/AndroidDocuments.h"
#include <map>
#include <set>
#include "EditorShared.h"
#include "ExpressiveKeyboard.h"

namespace
{
    // Import Patch could not use a file: why, in a few words (the processor's
    // import only says yes or no)
    juce::String whyNotAPatch (const juce::MemoryBlock& data)
    {
        if (data.getSize() == 0)
            return "the file is empty";
        const auto kind = aqp::android::sniffExtension (data.getData(), data.getSize());
        if (kind == "zip")
        {
            juce::ZipFile zip (new juce::MemoryInputStream (data, false), true);
            for (int i = 0; i < zip.getNumEntries(); ++i)
                if (const auto* e = zip.getEntry (i); e != nullptr && (e->filename == "patch.xml" || e->filename.endsWith ("/patch.xml")))
                    return "its patch.xml is not an Aquanode patch, or could not be read";
            return zip.getNumEntries() > 0 ? "it is a zip, but there is no patch.xml in it" : "the zip could not be read";
        }
        if (kind == "xml")
        {
            auto xml = juce::parseXML (juce::String::createStringFromData (data.getData(), (int) data.getSize()));
            if (xml == nullptr)
                return "its XML could not be read";
            return "it is XML, but not an Aquanode patch (<" + xml->getTagName() + ">)";
        }
        if (kind == "state")
            return "it is a saved plugin state, but not Aquanode Modular's";
        return "it is neither a patch (.aqua / .zip) nor a patch's .xml";
    }
}
//==============================================================================
// Editor
//==============================================================================
// (the Full Size view, defined first so the editor can own it)
class AquanodeModularAudioProcessorEditor::PluginFullView final : public juce::Component,
                                                                    private juce::Timer
{
public:
    PluginFullView (aquanode::plugins::PluginEditorLender& l, juce::Component& ed, std::function<void()> onClose)
        : lender (&l), editor (&ed), close (std::move (onClose))
    {
        setOpaque (true);
        addAndMakeVisible (stage);
        stage.addAndMakeVisible (ed);
        ed.setTopLeftPosition (0, 0);
        back.setButtonText ("Return to Patch");
        back.setColour (juce::TextButton::buttonColourId, juce::Colour (0xb0141210));
        back.setAlpha (0.9f);
        back.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        back.onClick = [this] { if (close) close(); };
        addAndMakeVisible (back);
        startTimerHz (5);
    }

    ~PluginFullView() override { giveBack(); }

    // the editor back onto its card (unless its card - and with it the editor - is gone)
    void giveBack()
    {
        if (auto* e = editor.getComponent())
        {
            stage.removeChildComponent (e);
            e->setTransform ({});
            lender->returnEditor();
        }
        editor = nullptr;
    }

    juce::Point<int> nativeSize() const
    {
        if (auto* e = editor.getComponent())
            return { juce::jmax (1, e->getWidth()), juce::jmax (1, e->getHeight()) };
        return { 1, 1 };
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0c0e)); }

    void resized() override
    {
        // the plugin as large as fits, centred; empty borders where the shapes differ
        const auto n = nativeSize();
        const int h = juce::jmax (1, getHeight());
        const float scale = juce::jmin ((float) getWidth() / (float) n.x, (float) h / (float) n.y);
        stage.setBounds (0, 0, n.x, n.y);
        const float ox = ((float) getWidth() - (float) n.x * scale) * 0.5f;
        const float oy = ((float) h - (float) n.y * scale) * 0.5f;
        stage.setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));
        lastNative = n;

        // small, in the corner: over the plugin's own top bar, out of the way
       #if JUCE_ANDROID
        const int bw = 104, bh = 24;
       #else
        const int bw = 92, bh = 20;
       #endif
        back.setBounds (getWidth() - bw - 4, 4, bw, bh);
        back.toFront (false);
    }

private:
    void timerCallback() override
    {
        if (editor == nullptr)
        {
            if (close) close();     // its card was rebuilt or deleted: back to the patch
            return;
        }
        if (nativeSize() != lastNative)
            resized();              // the plugin changed its own size
    }

    aquanode::plugins::PluginEditorLender* lender;
    juce::Component::SafePointer<juce::Component> editor;
    std::function<void()> close;
    juce::Component stage;
    juce::TextButton back;
    juce::Point<int> lastNative;
};

//==============================================================================
// Full screen for a card: the card itself, borrowed from the canvas, scaled to
// fit (centred, black where the shapes differ), with Return to Patch
class AquanodeModularAudioProcessorEditor::ModuleFullView final : public juce::Component,
                                                                    private juce::Timer
{
public:
    ModuleFullView (AquanodeModularAudioProcessor& p, PatchCanvas& c, ModuleComponent& cardToShow, std::function<void()> onClose)
        : processor (p), canvas (c), instanceId (cardToShow.getInstanceId()), typeId (typeOf (instanceId)), close (std::move (onClose))
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        addAndMakeVisible (stage);
        borrow (cardToShow);
        back.setButtonText ("Return to Patch");
        back.setColour (juce::TextButton::buttonColourId, juce::Colour (0xb0141210));
        back.setAlpha (0.9f);
        back.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        back.onClick = [this] { if (close) close(); };
        addAndMakeVisible (back);
        startTimerHz (5);
    }

    ~ModuleFullView() override { giveBack(); }

    int bottomReserve { 0 };   // kept clear at the bottom (the keyboard's grip)

    // after the canvas made its cards anew (undo, a loaded preset ...): the
    // module's new card, if it still has one, else back to the patch
    void canvasRebuilt()
    {
        if (card.getComponent() != nullptr)
            return;   // (still ours: it was not part of the canvas meanwhile)
        // (the same module: an id a new patch gave to another one doesn't count)
        auto* fresh = canvas.moduleCard (instanceId);
        if (fresh != nullptr && typeOf (instanceId) == typeId)
        {
            borrow (*fresh);
            resized();
        }
        else if (close)
            close();
    }

    void giveBack()
    {
        if (auto* c = card.getComponent())
        {
            stage.removeChildComponent (c);
            canvas.takeCardBack (*c);
        }
        card = nullptr;
    }

    juce::Point<int> nativeSize() const
    {
        if (auto* c = card.getComponent())
            return { juce::jmax (1, c->getWidth()), juce::jmax (1, c->getHeight()) };
        return { 1, 1 };
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

    // Escape leads back; the rest (Undo, Redo ...) as on the patch
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey)
        {
            if (close) close();
            return true;
        }
        return canvas.onShortcut != nullptr && canvas.onShortcut (k);
    }

    // the strip along the top that holds Return to Patch (clear of the card's
    // own top row, where its full screen icon also leads back)
    static int stripHeight()
    {
       #if JUCE_ANDROID
        return 32;
       #else
        return 26;
       #endif
    }

    void resized() override
    {
        const auto n = nativeSize();
        const auto room = getLocalBounds().withTrimmedTop (stripHeight()).withTrimmedBottom (bottomReserve).reduced (2);
        const float scale = juce::jmax (0.05f, juce::jmin ((float) room.getWidth() / (float) n.x, (float) room.getHeight() / (float) n.y));
        stage.setBounds (0, 0, n.x, n.y);
        stage.setTransform (juce::AffineTransform::scale (scale)
                                .translated ((float) room.getX() + ((float) room.getWidth() - (float) n.x * scale) * 0.5f,
                                             (float) room.getY() + ((float) room.getHeight() - (float) n.y * scale) * 0.5f));
        lastNative = n;
        const int bh = stripHeight() - 8, bw = bh * 5;
        back.setBounds (getWidth() - bw - 6, 4, bw, bh);
        back.toFront (false);
    }

private:
    juce::String typeOf (int id) const
    {
        if (auto* inst = processor.getInstance (id))
            return inst->descriptor().typeId;
        return {};
    }

    void borrow (ModuleComponent& c)
    {
        card = &c;
        c.setInFullView (true);
        stage.addAndMakeVisible (c);
        c.setTransform ({});
        c.setTopLeftPosition (0, 0);
    }

    void timerCallback() override
    {
        auto* c = card.getComponent();
        if (c == nullptr) { canvasRebuilt(); return; }   // its card was rebuilt or deleted
        if (c->getTransform() != juce::AffineTransform() || c->getPosition() != juce::Point<int>())
        {
            c->setTransform ({});
            c->setTopLeftPosition (0, 0);
        }
        if (nativeSize() != lastNative) resized();   // it opened, closed or grew
    }

    AquanodeModularAudioProcessor& processor;
    PatchCanvas& canvas;
    const int instanceId;
    const juce::String typeId;
    juce::Component::SafePointer<ModuleComponent> card;
    std::function<void()> close;
    juce::Component stage;
    juce::TextButton back;
    juce::Point<int> lastNative;
};

//==============================================================================
// The computer keyboard as a MIDI keyboard (6.9.10, desktop): the FL Studio
// layout, into the on-screen keys' state, so every generator hears it as
// played keys. Not while a text field has the focus.
struct AquanodeModularAudioProcessorEditor::ComputerKeys : public juce::KeyListener,
                                                          private juce::Timer
{
    ComputerKeys (juce::Component& ownerIn, juce::MidiKeyboardState& stateIn) : owner (ownerIn), state (stateIn)
    {
        owner.addKeyListener (this);
    }

    ~ComputerKeys() override
    {
        owner.removeKeyListener (this);
        releaseAll();
    }

    static int noteFor (int keyCode)
    {
        static const char* lower = "ZSXDCVGBHNJM,";   // C4 ..
        static const char* upper = "Q2W3ER5T6Y7UI9O0P";   // C5 ..
        const int c = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) keyCode);
        for (int i = 0; lower[i] != 0; ++i) if (lower[i] == c) return 60 + i;
        for (int i = 0; upper[i] != 0; ++i) if (upper[i] == c) return 72 + i;
        return -1;
    }

    // a text field being typed in - the search bar, a knob's value box, the
    // Notes module, a name: the keys are its letters then
    static bool typing()
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        for (auto* c = f; c != nullptr; c = c->getParentComponent())
            if (auto* te = dynamic_cast<juce::TextEditor*> (c))
                return ! te->isReadOnly();
        return false;
    }

    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        if (typing() || k.getModifiers().isCommandDown() || k.getModifiers().isAltDown())
            return false;
        if (noteFor (k.getKeyCode()) < 0)
            return false;
        poll();
        return true;   // (a played key does nothing else)
    }

    bool keyStateChanged (bool, juce::Component*) override
    {
        if (typing())
            return false;
        return poll();
    }

    // every mapped key: down now, or not
    bool poll()
    {
        static const char* keys = "ZSXDCVGBHNJM,Q2W3ER5T6Y7UI9O0P";
        bool any = false, changed = false;
        for (int i = 0; keys[i] != 0; ++i)
        {
            const int note = noteFor (keys[i]);
            const bool down = juce::KeyPress::isKeyCurrentlyDown (keys[i]);
            if (down != held[note])
            {
                held[note] = down;
                changed = true;
                if (down) state.noteOn (1, note, 0.8f);
                else      state.noteOff (1, note, 0.0f);
            }
            any = any || down;
        }
        if (any) startTimerHz (30); else stopTimer();   // (a key let go while the window lost the focus)
        return changed;
    }

    void releaseAll()
    {
        for (int n = 0; n < 128; ++n)
            if (held[n]) { state.noteOff (1, n, 0.0f); held[n] = false; }
    }

    void timerCallback() override
    {
        if (! owner.hasKeyboardFocus (true) && ! juce::Process::isForegroundProcess())
        {
            releaseAll();
            stopTimer();
            return;
        }
        poll();
    }

    juce::Component& owner;
    juce::MidiKeyboardState& state;
    bool held[128] {};
};

AquanodeModularAudioProcessorEditor::AquanodeModularAudioProcessorEditor (AquanodeModularAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      sidebar ([this] (const juce::String& typeId, bool openMenu)
      {
          if (typeId.startsWith ("plugingui:"))
          {
              addPluginGui (typeId.fromFirstOccurrenceOf (":", false, false));
              return;
          }

          if (typeId.startsWith ("pluginpatch:"))
          {
              showPluginPatchMenu (typeId.fromFirstOccurrenceOf (":", false, false));
              return;
          }

          if (aquanode::presets::findGenerator (typeId) != nullptr)
          {
              runGenerator (typeId, openMenu);
              return;
          }

          if (const auto* adder = aquanode::presets::findEffectAdder (typeId))
          {
              auto seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
              aquanode::presets::setQuickAddAnchor (canvas.getSelectedModuleId());
              const auto note = adder->apply (processor, seed == 0 ? 1u : seed);
              aquanode::presets::setQuickAddAnchor (-1);
              if (note.isNotEmpty() && ! processor.patchName.contains ("+ " + note))
                  processor.patchName = processor.patchName + " + " + note;
              canvas.rebuild();
              canvas.fitToContent();
              return;
          }

          // Fruit Arranger (6.7.12): with every clean piano roll of the patch on it, its cables moved in
          if (typeId == "util.fruitarranger")
          {
              extern int fruitArrangerAddWithLanes (AquanodeModularAudioProcessor&, juce::Point<int>);
              const int id = fruitArrangerAddWithLanes (processor, spawnPosition());
              canvas.rebuild();
              canvas.selectModule (id);
              return;
          }

          // Automation Lanes (6.7.11): alone, or with four effects on its lanes
          if (typeId == "util.autolanes")
          {
              juce::PopupMenu menu;
              menu.addSectionHeader ("Automation Lanes");
              menu.addItem (1, "Only this module");
              menu.addItem (2, "With four random effects on its lanes");
              juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
              menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sidebar).withMousePosition(), [safe] (int r)
              {
                  if (safe == nullptr || r == 0) return;
                  auto& ed = *safe;
                  const auto at = ed.spawnPosition();
                  int id;
                  if (r == 2)
                  {
                      extern int autoLanesAddWithEffects (AquanodeModularAudioProcessor&, juce::Point<int>, juce::uint32);
                      id = autoLanesAddWithEffects (ed.processor, at, (juce::uint32) juce::Random::getSystemRandom().nextInt());
                  }
                  else id = ed.processor.addModule ("util.autolanes", at);
                  ed.canvas.rebuild();
                  if (r == 2) ed.canvas.fitToContent();
                  ed.canvas.selectModule (id);
              });
              return;
          }

          // clicking a sidebar name opens one instance in the patch map,
          // near the top left of what is in view
          const auto pos = spawnPosition();
          const int newId = processor.addModule (typeId, pos);
          wireNewSynthToOutput (newId);
          canvas.rebuild();
          canvas.selectModule (newId);
      }),
      canvas (p),
      mutatorPanel (p, canvas)
{
    // the first patch is made before the standalone app finishes setting up
    // its device input: settle the input's mute for it now
    processor.refreshStandaloneInputMute();

    // (6.9.16) Disable GUI Transparency, as chosen before
    guiOpaque() = juce::PropertiesFile (settingsOptions()).getBoolValue ("gui.opaque", false);

    // "Don't slow down" (Android performance hint): the remembered switch,
    // and the once-a-second keeper of its session
    if (aquanode::perfhint::available())
    {
        aquanode::perfhint::switchedOn().store (juce::PropertiesFile (settingsOptions()).getBoolValue ("perf.hint", true));
        aquanode::shutdownSafe<aquanode::perfhint::Updater>();
    }

    // the colours chosen before (the app's settings), once per run
    static bool coloursLoaded = false;
    if (! coloursLoaded)
    {
        ThemePanel::loadSaved();
        coloursLoaded = true;
    }
    sidebarHeader.refreshColours();   // (it was made before the saved colours came in)

    addAndMakeVisible (sidebarViewport);
    addAndMakeVisible (sidebarHeader);
    sidebarHeader.onSearchChanged = [this] (const juce::String& text)
    {
        sidebar.setSearch (text);
        sidebarViewport.setViewPosition (0, 0);
    };
    sidebarHeader.onLogoClicked = [this] { showThemePanel(); };
    sidebarHeader.setMpe (processor.mpeEnabled.load());
    sidebarHeader.onMpeChanged = [this] (bool on)
    {
        if (auto* kb = dynamic_cast<ExpressiveKeyboard*> (midiKeyboard.get()))
            kb->stopAll();
        processor.mpeEnabled.store (on);
        // remembered in the app's settings
        juce::PropertiesFile file (settingsOptions());
        if (on) file.setValue ("mpe.enabled", true); else file.removeValue ("mpe.enabled");
        file.saveIfNeeded();
    };
    sidebar.setWithMutation (processor.generateWithMutation);
    sidebar.onWithMutationChanged = [this] (bool on) { processor.generateWithMutation = on; };
    sidebar.setExpandedHeaders (processor.uiExpandedHeaders);
    sidebar.onExpandedChanged = [this] (const juce::StringArray& open) { processor.uiExpandedHeaders = open; };
    sidebarViewport.setViewedComponent (&sidebar, false);   // false: sidebar is a member, viewport must not delete it

    // the Modulation table at the end of the sidebar
    modTable.setSize (SidebarComponent::sidebarWidth - SidebarComponent::scrollBarWidth - 20, 80);
    modTable.onHeightChanged = [this] { sidebar.footerResized(); };
    modTable.refresh();
    sidebar.setFooter (&modTable);
    sidebarViewport.setScrollBarsShown (true, false);        // vertical only
    addAndMakeVisible (canvas);
    addChildComponent (mutatorPanel);   // hidden until the Mutator button toggles it
    addAndMakeVisible (toolbarStrip);   // added before the buttons: they draw on top of it

    // On-screen keyboard: driven by (and mirrored onto) the processor's shared
    // MidiKeyboardState, shown along the bottom on every build and toggled by
    // the Keys button. Both panel toggles remember their state in the
    // processor, so a plugin window reopens the way it was left.
    sidebarVisible = processor.uiSidebarVisible;
    keyboardVisible = processor.uiKeyboardVisible;
    sidebarViewport.setVisible (sidebarVisible);
    sidebarHeader.setVisible (sidebarVisible);

    // the keys' width zoom and whether a pinch sets it, kept in the app's settings
    if (processor.uiKeyZoom <= 0.0f)
        processor.uiKeyZoom = (float) juce::PropertiesFile (settingsOptions()).getDoubleValue ("keys.zoom", 1.0);
    if (processor.uiKeyPinch < 0)
        processor.uiKeyPinch = juce::PropertiesFile (settingsOptions()).getBoolValue ("keys.pinch", true) ? 1 : 0;
   #if ! JUCE_ANDROID && ! JUCE_IOS
    // the computer keyboard as MIDI, if it was switched on (6.9.10)
    setWantsKeyboardFocus (true);
    if (juce::PropertiesFile (settingsOptions()).getBoolValue ("keys.computer", true))   // (6.9.11: on unless switched off)
        computerKeys = std::make_unique<ComputerKeys> (*this, processor.keyboardState);
   #endif
    keyZoomToggle.isOn = [this] { return processor.uiKeyPinch != 0; };
    keyZoomToggle.onToggle = [this]
    {
        processor.uiKeyPinch = processor.uiKeyPinch != 0 ? 0 : 1;
        juce::PropertiesFile file (settingsOptions());
        if (processor.uiKeyPinch != 0) file.removeValue ("keys.pinch"); else file.setValue ("keys.pinch", false);
        file.saveIfNeeded();
    };
    keyZoomToggle.setTooltip ("Pinch zoom on the keys. On: two fingers moving apart or together make the keys wider or "
                              "narrower. Off: two fingers always play - for two notes sliding at once. (With a mouse, "
                              "Shift + drag or Shift + wheel set the width either way.)");
    addChildComponent (keyZoomToggle);
    {
        auto kb = std::make_unique<ExpressiveKeyboard> (processor);
        kb->onZoomFinished = [this]
        {
            juce::PropertiesFile file (settingsOptions());
            if (std::abs (processor.uiKeyZoom - 1.0f) < 0.01f) file.removeValue ("keys.zoom");
            else                                                file.setValue ("keys.zoom", (double) processor.uiKeyZoom);
            file.saveIfNeeded();
        };
        midiKeyboard = std::move (kb);
    }
    midiKeyboard->setOctaveForMiddleC (4);
    midiKeyboard->setKeyPressBaseOctave (5);   // computer keys: 'A' plays middle C
    addChildComponent (*midiKeyboard);
    midiKeyboard->setVisible (keyboardVisible);

    // the top bar: three menus, CPU (a menu too), Undo / Redo, the tempo, the
    // zoom. (Initialize ... Mutator are pressed from the menus; Full Screen is
    // the standalone window's own maximise button now, and F11)
    // (6.7.7: Delete and Move Only back on the bar - pressed all the time;
    // Undo / Redo in the Edit menu, the zoom in the View menu)
    // (6.9.15: Move Only only in the Edit menu)
    for (auto* b : { &patchMenuButton, &editMenuButton, &viewMenuButton, &pluginBlockButton, &deleteButton })
        toolbarStrip.content.addAndMakeVisible (b);
    deleteButton.setButtonText ("Delete");
    toolbarStrip.content.addAndMakeVisible (bpmBox);
    patchMenuButton.setTooltip ("Initialize Patch, Save Patch, Load Patch, Render");
    editMenuButton.setTooltip ("Clone Selected, Delete Selected, Region Select, Move Only");
    viewMenuButton.setTooltip ("Keys, Sidebar, Mutator, Zoom, Reset View");
    patchMenuButton.onClick = [this] { showPatchMenu(); };
    editMenuButton.onClick = [this] { showEditMenu(); };
    viewMenuButton.onClick = [this] { showViewMenu(); };
    canvas.onPluginFullSize = [this] (int id) { showPluginFullSize (id); };
    canvas.onModuleFullScreen = [this] (int id) { toggleModuleFullScreen (id); };
    canvas.onRebuilt = [this] { if (moduleFullView != nullptr) moduleFullView->canvasRebuilt(); };

    // Region Select: a drag on empty space draws a box and selects the modules
    // it touches, rather than moving the view
    regionButton.setClickingTogglesState (true);
    regionButton.setToggleState (processor.uiRegionSelect, juce::dontSendNotification);
    canvas.setRegionSelect (processor.uiRegionSelect);
    regionButton.setTooltip ("On: drag on empty space (finger or mouse) to draw a box - every module it touches is "
                             "selected. Drag one of them to move them all; Clone Selected copies them with the cables "
                             "between them, Delete Selected removes them. Dropping a module never patches it into "
                             "another while this is on. Two fingers (or the middle mouse button) still move the view");
    regionButton.onClick = [this]
    {
        processor.uiRegionSelect = regionButton.getToggleState();
        canvas.setRegionSelect (processor.uiRegionSelect);
        refreshEditMenuButton();
    };
    // Move Only: every drag, wheel and pinch moves and zooms the view, over
    // cards and plugins too (one zoomed up to fill the area keeps nothing back)
    moveOnlyButton.setClickingTogglesState (true);
    moveOnlyButton.setToggleState (processor.uiMoveOnly, juce::dontSendNotification);
    canvas.setMoveOnly (processor.uiMoveOnly);
    moveOnlyButton.setTooltip ("On: every drag, wheel and pinch anywhere in the patch area moves and zooms the view - over "
                               "modules and plugins' screens too, which otherwise keep those gestures. Nothing in the patch "
                               "can be touched while it is on");
    moveOnlyButton.onClick = [this]
    {
        processor.uiMoveOnly = moveOnlyButton.getToggleState();
        canvas.setMoveOnly (processor.uiMoveOnly);
        refreshEditMenuButton();
    };
    refreshEditMenuButton();
    cloneButton.setTooltip ("A copy of the selected module (or of every module a Region Select box holds, with the cables between them)");
    deleteButton.setTooltip ("Delete the selected module (or every module a Region Select box holds) with its cables");

    // Render to File: active while a Piano Roll with notes is patched somewhere
    renderButton.setTooltip ("Render the patch to a WAV or FLAC file: every Piano Roll with notes that is patched "
                             "somewhere, played through once from the start, and the tails after it");
    renderButton.onClick = [this] { showRenderPanel(); };
    pluginBlockButton.setTooltip ("CPU: the Plugin Block Size (every plugin card, unless its own Block pill "
                                  "says otherwise - larger blocks cost much less CPU for plugins like Vita), lined up with "
                                  "the rest of the patch; the latency switches for all patches, CPU Sleep, and on Android "
                                  "the performance hint");
    pluginBlockButton.onClick = [this] { showPluginBlockMenu(); };

    // CPU Sleep: app-wide, remembered (on unless switched off)
    aquanode::cpuSleepEnabled().store (juce::PropertiesFile (settingsOptions()).getBoolValue ("cpu.sleep", true));
    cpuSleepButton.setClickingTogglesState (true);
    cpuSleepButton.setToggleState (aquanode::cpuSleepEnabled().load(), juce::dontSendNotification);
    cpuSleepButton.setTooltip ("CPU Sleep: an effect or filter whose input has been silent for longer than it can still "
                               "bring out (its longest delay, a plugin's tail) and whose output has been silent for a second "
                               "is not run until sound comes in, MIDI reaches it or one of its knobs moves - spectral "
                               "modules and plugin effects stop using CPU on silence");
    cpuSleepButton.onClick = [this]
    {
        const bool on = cpuSleepButton.getToggleState();
        aquanode::cpuSleepEnabled().store (on);
        juce::PropertiesFile file (settingsOptions());
        if (on) file.removeValue ("cpu.sleep"); else file.setValue ("cpu.sleep", false);
        file.saveIfNeeded();
    };
    renderButton.setEnabled (aquanode::render::canRender (processor));

    // Undo / Redo of the whole patch (32 steps, in memory only)
    undoPatchButton.setTooltip ("Undo the last change to the patch (Ctrl+Z) - up to 32 steps");
    redoPatchButton.setTooltip ("Redo what was undone (Ctrl+Shift+Z or Ctrl+Y)");
    undoPatchButton.onClick = [this] { undoRedo (false); };
    redoPatchButton.onClick = [this] { undoRedo (true); };
    canvas.onShortcut = [this] (const juce::KeyPress& k)
    {
        const auto mods = k.getModifiers();
        if (k.getKeyCode() == juce::KeyPress::escapeKey && currentFullView() != nullptr)
        {
            closeModuleFullScreen();
            closePluginFullSize();
            return true;
        }
        if (k.getKeyCode() == juce::KeyPress::F11Key && hasFullScreenButton())
        {
            toggleFullScreen();
            return true;
        }
        if (! mods.isCommandDown())
            return false;
        const int c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode());
        if (c == 'z') { undoRedo (mods.isShiftDown()); return true; }
        if (c == 'y') { undoRedo (true); return true; }
        return false;
    };
    historyTimer.tick = [this]
    {
        processor.historyTick();
        refreshUndoButtons();
        modTable.refresh();
        renderButton.setEnabled (renderJob == nullptr && aquanode::render::canRender (processor));
    };
    historyTimer.startTimerHz (4);
    refreshUndoButtons();

    // the grip that sets the keyboard's height
    addChildComponent (keyboardHandle);
    keyboardHandle.getHeightNow = [this] { return midiKeyboard != nullptr ? midiKeyboard->getHeight() : 0; };
    keyboardHandle.onHeightChanged = [this] (int h)
    {
        processor.uiKeyboardHeight = h;
        resized();
    };

    // (the zoom buttons: in the View menu now, and kept off the bar)
    // the patch's name and the zoom are shown in the sidebar, under its title
    sidebarHeader.getPatchName = [this] { return processor.patchName; };
    sidebarHeader.onZoomClicked = [this] { canvas.resetView(); };

    sidebarButton.onClick = [this] { showSidebar (! sidebarVisible); };

    // the sidebar's edge: slid out of view and back (see SidebarEdge)
    addAndMakeVisible (sidebarEdge);
    sidebarEdge.onDragStart = [this]
    {
        if (! sidebarVisible)
        {
            // (shown, but still all out of view: pulled in by the drag)
            sidebarVisible = true;
            processor.uiSidebarVisible = true;
            sidebarShift = sidebarFullWidth;
            sidebarViewport.setVisible (true);
            sidebarHeader.setVisible (true);
        }
        sidebarShiftAtDrag = sidebarShift;
    };
    sidebarEdge.onDrag = [this] (int screenDx)
    {
        const int dx = juce::roundToInt ((float) screenDx / uiScale);
        sidebarShift = juce::jlimit (0, sidebarFullWidth, sidebarShiftAtDrag - dx);   // (never wider than it is)
        resized();
    };
    sidebarEdge.onDragEnd = [this]
    {
        if (sidebarShift >= sidebarFullWidth - 16)
            showSidebar (false);   // (let go nearly out of view: hidden)
    };
    sidebarEdge.onTap = [this] { if (! sidebarVisible) showSidebar (true); };

    keysButton.onClick = [this]
    {
        keyboardVisible = ! keyboardVisible;
        processor.uiKeyboardVisible = keyboardVisible;
        if (midiKeyboard != nullptr)
            midiKeyboard->setVisible (keyboardVisible);
        resized();
    };

    zoomInButton .onClick = [this] { canvas.zoomBy (1.25f); };
    zoomOutButton.onClick = [this] { canvas.zoomBy (1.0f / 1.25f); };

    mutatorButton.setClickingTogglesState (true);
    mutatorButton.onClick = [this]
    {
        mutatorPanel.setVisible (mutatorButton.getToggleState());
    };

    initButton.onClick = [this]
    {
        processor.initializePatch();
        canvas.rebuild();
    };

    cloneButton.onClick = [this]
    {
        // one module: its copy, without cables; several (Region Select): copies
        // of them all with the cables between them, so a group comes out as the
        // same little patch - and the copies are what is selected afterwards
        const auto sel = canvas.getSelection();
        if (sel.empty())
            return;
        std::map<int, int> copyOf;
        for (const int id : sel)
        {
            const int newId = processor.cloneModule (id);   // params kept, no cables
            if (newId >= 0) copyOf[id] = newId;
        }
        if (sel.size() > 1)
        {
            const auto cables = processor.getCables();
            for (const auto& c : cables)
                if (copyOf.count (c.fromModule) > 0 && copyOf.count (c.toModule) > 0)
                    processor.addCable (copyOf[c.fromModule], c.fromSocket, copyOf[c.toModule], c.toSocket);
            const auto paramCables = processor.getParamCables();
            for (const auto& pc : paramCables)
                if (copyOf.count (pc.fromModule) > 0 && copyOf.count (pc.toModule) > 0)
                {
                    processor.addParamCable (copyOf[pc.fromModule], pc.fromSocket, copyOf[pc.toModule], pc.paramId, pc.depth);
                    const auto& now = processor.getParamCables();
                    for (int k = 0; k < (int) now.size(); ++k)
                        if (now[(size_t) k].fromModule == copyOf[pc.fromModule] && now[(size_t) k].fromSocket == pc.fromSocket
                            && now[(size_t) k].toModule == copyOf[pc.toModule] && now[(size_t) k].paramId == pc.paramId)
                            processor.setParamCableBipolar (k, pc.bipolar);
                }
        }
        canvas.rebuild();
        std::vector<int> copies;
        for (const int id : sel)
            if (copyOf.count (id) > 0) copies.push_back (copyOf[id]);
        if (copies.size() == 1) canvas.selectModule (copies.front());
        else                    canvas.setSelection (copies);
    };

    deleteButton.onClick = [this]
    {
        const auto sel = canvas.getSelection();
        for (const int id : sel)
            processor.removeModule (id);                    // removes its cables too
        if (! sel.empty())
            canvas.rebuild();
    };

    exportButton.onClick = [this]
    {
        // the suggested name: the patch's own, ending in .aqua
        auto base = juce::File::createLegalFileName (processor.patchName.trim());
        if (base.isEmpty() || base == "Init") base = "Patch";
        const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                   .getChildFile (base + ".aqua");
        fileChooser = std::make_unique<juce::FileChooser> ("Save Patch", suggested, "*.aqua");
        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
               #if JUCE_ANDROID
                // The picker returns a document URL (may be a content:// URI).
                // Write the same .aqua (zip) container through it -> 1:1 compatible.
                // Replacing what was there: a patch saved over a bigger one
                // must not keep the old one's tail.
                const auto url = fc.getURLResult();
                if (url.isEmpty())
                    return;

                juce::MemoryOutputStream zipped;
                juce::String problem = processor.exportPatchToStream (zipped) ? juce::String() : juce::String ("the patch could not be packed");
                if (problem.isEmpty())
                    problem = aqp::files::writeDocument (url, zipped.getData(), zipped.getDataSize());

                // a name typed without the ending gets it
                if (problem.isEmpty())
                    if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                    {
                        const auto name = doc.getInfo().getName();
                        if (name.isNotEmpty() && ! name.endsWithIgnoreCase (".aqua") && ! name.endsWithIgnoreCase (".zip"))
                            doc.renameTo (name + ".aqua");
                    }

                if (problem.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Patch",
                                                            "The patch could not be saved: " + problem + ".");
               #else
                auto file = fc.getResult();
                if (file != juce::File())
                    if (! processor.exportPatchToZip (file.withFileExtension ("aqua")))   // a .zip inside
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save Patch",
                                                                "The patch could not be saved to " + file.withFileExtension ("aqua").getFullPathName() + ".");
               #endif
            });
    };

    importButton.onClick = [this]
    {
        // .aqua, an older .zip, or a patch's .xml on its own; on Android every
        // document is offered (a provider may know .aqua by no type at all) and
        // the contents decide
       #if JUCE_ANDROID
        const juce::String patterns { "*" };
       #else
        const juce::String patterns { "*.aqua;*.zip;*.xml" };
       #endif
        fileChooser = std::make_unique<juce::FileChooser> ("Load Patch", juce::File(), patterns);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                juce::MemoryBlock data;
                juce::String name, problem;
               #if JUCE_ANDROID
                const auto url = fc.getURLResult();
                if (url.isEmpty())
                    return;

                std::unique_ptr<juce::InputStream> in;
                auto doc = juce::AndroidDocument::fromDocument (url);
                if (doc.hasValue())
                {
                    name = doc.getInfo().getName();
                    in = doc.createInputStream();
                }
                if (in == nullptr)
                    in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
                if (in != nullptr)
                    in->readIntoMemoryBlock (data);
                else
                    problem = "the file could not be opened";
                if (name.isEmpty())
                    name = juce::URL::removeEscapeChars (url.toString (false)).fromLastOccurrenceOf ("/", false, false)
                                                                              .fromLastOccurrenceOf (":", false, false);
               #else
                const auto file = fc.getResult();
                if (! file.existsAsFile())
                    return;
                name = file.getFileName();
                if (! file.loadFileAsData (data))
                    problem = "the file could not be read";
               #endif

                // "My Pad.aqua" -> "My Pad"
                for (auto ext : { ".aqua", ".zip", ".xml" })
                    if (name.endsWithIgnoreCase (ext))
                        name = name.dropLastCharacters ((int) std::strlen (ext));

                if (problem.isEmpty())
                {
                   #if JUCE_ANDROID
                    juce::MemoryInputStream bytes (data, false);
                    const bool ok = processor.importPatchFromStream (bytes, name);
                   #else
                    const bool ok = processor.importPatchFromZip (fc.getResult());
                   #endif
                    if (ok)
                    {
                        canvas.rebuild();
                        return;
                    }
                    problem = whyNotAPatch (data);
                }
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load Patch",
                                                        "Could not import " + (name.isNotEmpty() ? name : juce::String ("the file"))
                                                        + ": " + problem + ".");
            });
    };

    processor.addChangeListener (this);
    applyBarColours();

    // the app opens with the keyboard as low as it goes (the grip pulls it up)
    if (processor.uiKeyboardHeight <= 0)
        processor.uiKeyboardHeight = 1;

   #if JUCE_ANDROID
    // A patch is saved as a plain document: with no type of its own, the save
    // picker would offer none, and some providers then change the name.
    juce::FileChooser::registerCustomMimeTypeForFileExtension ("application/octet-stream", "aqua");

    // Fill the device screen; the standalone wrapper resizes us to the display,
    // and resized() lays everything out responsively (landscape assumed).
    setResizable (true, false);
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setSize (disp->userArea.getWidth(), disp->userArea.getHeight());
    else
        setSize (1280, 720);
   #else
    // Resizable with a fixed aspect ratio: resized() scales everything uniformly
    // (fonts, sidebar, top bar, patch area, keyboard), so the proportions never change.
    // It always opens at the base size: a size remembered from the last time
    // could be one a host forced on it (some shrink a plugin window to its
    // minimum while opening or closing it).
    setSize (kBaseWidth, kBaseHeight);
    setResizable (true, true);
    // free resizing (5.16): no fixed aspect ratio - resized() scales by the
    // smaller side and lays the rest out in the room there is
    setResizeLimits (kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 4, kBaseHeight * 4);
   #endif

    canvas.rebuild();

    // a generated patch is bigger than the window: show all of it the first
    // time the editor opens on it (async, once the layout has settled)
    if (processor.uiFitOnOpen)
    {
        processor.uiFitOnOpen = false;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> (this)]
        {
            if (safe != nullptr)
                safe->canvas.fitToContent();
        });
    }
}

void AquanodeModularAudioProcessorEditor::setComputerKeys (bool on)
{
   #if JUCE_ANDROID || JUCE_IOS
    juce::ignoreUnused (on);
   #else
    if (on == (computerKeys != nullptr))
        return;
    computerKeys = on ? std::make_unique<ComputerKeys> (*this, processor.keyboardState) : nullptr;
    if (on)
        grabKeyboardFocus();
    juce::PropertiesFile file (settingsOptions());
    if (on) file.removeValue ("keys.computer"); else file.setValue ("keys.computer", false);
    file.saveIfNeeded();
   #endif
}

AquanodeModularAudioProcessorEditor::~AquanodeModularAudioProcessorEditor()
{
    computerKeys = nullptr;
    moduleFullView = nullptr;   // a borrowed card back onto the canvas first
    pluginFullView = nullptr;   // the plugin's editor back onto its card first
    historyTimer.stopTimer();
    processor.removeChangeListener (this);
}

void AquanodeModularAudioProcessorEditor::undoRedo (bool redo)
{
    if (redo ? processor.redoPatch() : processor.undoPatch())
        canvas.rebuild();
    refreshUndoButtons();
}

void AquanodeModularAudioProcessorEditor::refreshUndoButtons()
{
    undoPatchButton.setEnabled (processor.canUndoPatch());
    redoPatchButton.setEnabled (processor.canRedoPatch());
}

void AquanodeModularAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    canvas.resetView();
    canvas.rebuild();
    coloursChanged();     // a loaded patch brings its colours
    ThemePanel::save();   // ... and the app keeps them (written only when they changed)
}

void AquanodeModularAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (themeColours().glass)
    {
        // Glass: the jungle behind the bars too (they are frosted panes over it)
        glass::backdrop().draw (g, getLocalBounds());   // (the patch area draws its piece of the same picture)
        // the sidebar's whole column frosted here (its scroll bar's lane and
        // any room below the list too - the sidebar itself paints none)
        if (sidebarViewport.isVisible())
        {
            // (in the editor's own pixels: the sidebar is drawn through the
            // UI scale's transform, its bounds are not)
            const auto col = sidebarViewport.getBoundsInParent();
            g.setColour (themeColours().get (ThemeColours::Sidebar).withAlpha (0.58f));
            g.fillRect (col);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (col);
        }
        return;
    }
    g.fillAll (themeColours().get (ThemeColours::Sidebar));
}

void AquanodeModularAudioProcessorEditor::showThemePanel()
{
    if (themePanel != nullptr)
    {
        themePanel = nullptr;
        return;
    }
    themePanel = std::make_unique<ThemePanel> ([this] { coloursChanged(); });
    themePanel->onResetView = [this] { canvas.resetView(); };
    themePanel->onClose = [this]
    {
        // not from inside the panel's own button callback
        juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->themePanel = nullptr; });
    };
    addAndMakeVisible (*themePanel);
    resized();
}

void AquanodeModularAudioProcessorEditor::applyBarColours()
{
    // the top bar's and the mutator's buttons follow the colourway; on the
    // default colours they keep the look-and-feel's own
    std::vector<juce::Button*> buttons { &patchMenuButton, &editMenuButton, &viewMenuButton, &pluginBlockButton,
                                         &bpmBox, &zoomOutButton, &zoomInButton, &undoPatchButton, &redoPatchButton,
                                         &deleteButton, &moveOnlyButton };
    for (auto* b : mutatorPanel.getButtons())
        buttons.push_back (b);
    const bool themed = aquanode::barsAreThemed();
    const auto bar = aquanode::barColour();
    const auto face = bar.interpolatedWith (themeColours().get (ThemeColours::Oscillator), 0.35f).brighter (0.15f);
    const auto on = themeColours().get (ThemeColours::Oscillator);
    juce::LookAndFeel* gloss = themeColours().skeuomorphic ? static_cast<juce::LookAndFeel*> (&aeroButtonLookAndFeel())   // Skeuomorphic: glossy pills
                             : themeColours().glass ? static_cast<juce::LookAndFeel*> (&glassLookAndFeel()) : nullptr;   // Glass: panes
    for (auto* b : buttons)
    {
        b->setLookAndFeel (gloss);
        for (auto id : { juce::TextButton::buttonColourId, juce::TextButton::buttonOnColourId,
                         juce::TextButton::textColourOffId, juce::TextButton::textColourOnId })
            b->removeColour (id);
        if (themed)
        {
            b->setColour (juce::TextButton::buttonColourId, face);
            b->setColour (juce::TextButton::buttonOnColourId, on);
            b->setColour (juce::TextButton::textColourOffId, themeColours().textOn (face));
            b->setColour (juce::TextButton::textColourOnId, themeColours().textOn (on));
        }
        b->repaint();
    }
    mutatorPanel.applyBarColours (themed, face, on);

    // the sidebar's scroll bar: a pale pill with Glass, the look's own otherwise
    auto& sb = sidebarViewport.getVerticalScrollBar();
    if (themeColours().glass)
    {
        sb.setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.38f));
        sb.setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
        sb.setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    }
    else
        for (auto id : { juce::ScrollBar::thumbColourId, juce::ScrollBar::backgroundColourId, juce::ScrollBar::trackColourId })
            sb.removeColour (id);
    sb.repaint();
    toolbarStrip.repaint();
    mutatorPanel.repaint();
}

void AquanodeModularAudioProcessorEditor::coloursChanged()
{
    applyBarColours();
    sidebar.refreshColours();       // its rows keep their colours: built anew
    sidebarHeader.refreshColours();

    // everything that draws with the theme: the canvas, its modules, the sidebar
    repaint();
    sidebar.repaint();
    sidebarHeader.repaint();
    canvas.repaint();
    for (auto* c : canvas.getChildren())
    {
        if (auto* mc = dynamic_cast<ModuleComponent*> (c))
            mc->applySkin();   // Skeuomorphic on or off
        c->repaint();
    }
}

// 6.7.6: a sound generator added to a patch that has no Audio Out (an empty
// one, say) makes one and plays into it, as a plugin's card does - through a
// Polyphony Sum where it plays per voice, as Initialize Patch has it (the
// voices summed at a level that does not clip). With an Audio Out there, it is
// left for the cables to decide.
void AquanodeModularAudioProcessorEditor::wireNewSynthToOutput (int newId)
{
    auto* inst = processor.getInstance (newId);
    // a new Tempo module (6.7.9) starts at the patch's tempo rather than moving it
    if (inst != nullptr && inst->descriptor().typeId == "util.tempo")
        inst->dsp->setParameter ("bpm", (float) processor.patchBpm.load());
    if (inst == nullptr || inst->descriptor().section != aquanode::ModuleSection::Oscillator)
        return;
    for (int id : processor.getModuleIds())
        if (auto* other = processor.getInstance (id); other != nullptr && other->descriptor().typeId == "io.audioOut")
            return;
    juce::String outSocket;
    for (const auto& s : inst->descriptor().sockets)
        if (s.direction == aquanode::SocketDirection::Output && s.kind == aquanode::SocketKind::Audio) { outSocket = s.id; break; }
    if (outSocket.isEmpty())
        return;
    auto at = inst->position + juce::Point<int> (inst->dsp->preferredModuleWidth() + 50, 0);
    int from = newId;
    juce::String fromSocket = outSocket;
    if (inst->dsp->voiceMode() == aquanode::VoiceMode::PerVoice)
    {
        const int sum = processor.addModule ("util.polysum", at);
        if (sum >= 0)
        {
            processor.addCable (newId, outSocket, sum, "audioIn");
            from = sum;
            fromSocket = "audioOut";
            at += juce::Point<int> (processor.getInstance (sum)->dsp->preferredModuleWidth() + 50, 0);
        }
    }
    const int out = processor.addModule ("io.audioOut", at);
    if (out >= 0)
        processor.addCable (from, fromSocket, out, "audioIn");
}

void AquanodeModularAudioProcessorEditor::showSidebar (bool show)
{
    sidebarVisible = show;
    sidebarShift = 0;   // (shown: all of it)
    processor.uiSidebarVisible = show;
    sidebarViewport.setVisible (show);
    sidebarHeader.setVisible (show);
    resized();
}

void AquanodeModularAudioProcessorEditor::resized()
{
   #if ! JUCE_ANDROID
    // Uniform scaling: lay out on the base area and draw every child (but the
    // host's resize corner) through one scale, so all of it grows and shrinks
    // together - text included.
    uiScale = juce::jmax (0.1f, juce::jmin ((float) getWidth() / (float) kBaseWidth, (float) getHeight() / (float) kBaseHeight));
    for (auto* child : getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*> (child) == nullptr && child != (juce::Component*) pluginFullView.get()
            && child != (juce::Component*) moduleFullView.get())
            child->setTransform (juce::AffineTransform::scale (uiScale));
   #endif
    // the layout's own size: the window's, before the scale
    const int layoutW = juce::roundToInt ((float) getWidth() / uiScale);
    const int layoutH = juce::roundToInt ((float) getHeight() / uiScale);
    auto area = juce::Rectangle<int> (layoutW, layoutH);

    // Bottom on-screen keyboard, height scaled to the window (clamped sane):
    // finger-sized on a phone, a little more compact for a mouse.
   #if JUCE_ANDROID
    // (6.7.2: the app turns with the phone - held upright, the keyboard takes a
    // smaller share of the taller screen)
    const bool portrait = layoutH > layoutW;
    const int kbH = portrait ? juce::jlimit (100, 200, layoutH / 5) : juce::jlimit (110, 220, layoutH / 4);
    const float kbKeys = 30.0f, kbMinKey = 26.0f;
   #else
    const int kbH = juce::jlimit (90, 140, layoutH / 6);
    const float kbKeys = 36.0f, kbMinKey = 18.0f;
   #endif
    if (midiKeyboard != nullptr && keyboardVisible)
    {
        // the height the grip set, if any (kept within the window)
        const int wanted = processor.uiKeyboardHeight > 0 ? processor.uiKeyboardHeight : kbH;
        const int h = juce::jlimit (50, juce::jmax (60, (int) (layoutH * 0.7f)), wanted);
        auto kbArea = area.removeFromBottom (h);
        midiKeyboard->setBounds (kbArea);
        const float baseKey = juce::jmax (kbMinKey, kbArea.getWidth() / kbKeys);
        if (auto* kb = dynamic_cast<ExpressiveKeyboard*> (midiKeyboard.get()))
            kb->setBaseKeyWidth (baseKey);   // times the zoom a pinch or Shift + drag set
        else
            midiKeyboard->setKeyWidth (baseKey);

       #if JUCE_ANDROID
        const int gripH = 26;   // finger-sized
       #else
        const int gripH = 16;
       #endif
        keyboardHandle.setBounds (kbArea.getCentreX() - 50, kbArea.getY() - gripH, 100, gripH);
        keyboardHandle.setVisible (true);
        keyboardHandle.toFront (false);
        // the "<- ->" pill just right of the grip's own pill (56 wide in its middle)
       #if JUCE_ANDROID
        const int zoomPillW = 36;
       #else
        const int zoomPillW = 26;
       #endif
        keyZoomToggle.setBounds (kbArea.getCentreX() + 28 + 8, kbArea.getY() - gripH, zoomPillW, gripH);
        keyZoomToggle.setVisible (true);
        keyZoomToggle.toFront (false);
    }
    else
    {
        keyboardHandle.setVisible (false);
        keyZoomToggle.setVisible (false);
    }

   #if JUCE_ANDROID
    // Slightly wider sidebar and bigger buttons for touch; upright, a share of
    // the narrow screen that still fits the module names
    const int sideW = portrait ? juce::jlimit (180, 300, (int) (layoutW * 0.46f))
                               : juce::jlimit (200, 300, layoutW / 5);
    const int bh = 40, gap = 8, topY = 8;
   #else
    const int sideW = SidebarComponent::sidebarWidth;
    const int bh = 22, gap = 5, topY = 8;
   #endif

    // Hidden sidebar gives its whole width back to the patch area; slid partly
    // out (its edge dragged), it keeps its width and goes out at the left
    sidebarFullWidth = sideW;
    sidebarShift = juce::jlimit (0, sideW, sidebarShift);
    {
        const int shown = sidebarVisible ? sideW - sidebarShift : 0;
        auto side = area.removeFromLeft (shown);
        auto column = juce::Rectangle<int> (side.getRight() - sideW, side.getY(), sideW, side.getHeight());
        sidebarHeader.setBounds (column.removeFromTop (sidebarVisible ? SidebarHeader::preferredHeight() : 0));
        sidebarViewport.setBounds (column);
        sidebar.footerResized();   // the Modulation table as wide as the sidebar shows
    }
    canvas.setBounds (area);

    // Toolbar strip along the top of the patch area, laid out the same way as
    // the mutator strip: it spans exactly the patch region, so nothing can
    // reach across the sidebar, and the buttons split the width evenly so all
    // six stay visible however narrow the screen gets.
    const int stripH = topY + bh + topY;
    const auto strip = juce::Rectangle<int> (area.getX(), area.getY(), area.getWidth(), stripH);
    toolbarStrip.setBounds (strip);

    // the sidebar's edge, below the top bar (clear of its buttons): on the edge
    // while it shows, a tab at the left when it is hidden
    {
       #if JUCE_ANDROID
        const int ew = 22;
       #else
        const int ew = 14;
       #endif
        sidebarEdge.tab = ! sidebarVisible;
        const auto below = area.withTrimmedTop (stripH);
        if (sidebarVisible)
            sidebarEdge.setBounds (area.getX() - ew / 2, below.getY(), ew, below.getHeight());
        else
            sidebarEdge.setBounds (juce::Rectangle<int> (area.getX(), below.getY(), ew, below.getHeight()));
        sidebarEdge.toFront (false);
        sidebarEdge.repaint();
    }

    // the mutator strip, when open, right below the top bar (clear of the
    // keyboard's grip at the bottom)
   #if JUCE_ANDROID
    const int mutH = 46;
   #else
    const int mutH = 34;
   #endif
    mutatorPanel.setBounds (area.getX(), strip.getBottom(), area.getWidth(), mutH);

    {
        // left to right on screen: View, Delete, Move Only, Edit, Patch, CPU,
        // the tempo (6.7.9; Undo / Redo in Edit, the zoom in View)
        std::vector<juce::TextButton*> buttons;
        for (juce::TextButton* b : std::initializer_list<juce::TextButton*> { &viewMenuButton, &deleteButton, &editMenuButton,
                         &patchMenuButton, &pluginBlockButton, &bpmBox })
            buttons.push_back (b);
        // what each button's text needs
        std::vector<int> natural;
        int needed = gap + gap;
        for (auto* b : buttons)
        {
            const auto font = getLookAndFeel().getTextButtonFont (*b, bh);
            const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, b->getButtonText())) + 18;
            natural.push_back (w);
            needed += w + gap;
        }

        const bool fits = needed <= strip.getWidth();
        const int contentW = fits ? strip.getWidth() : needed;
        toolbarStrip.content.setSize (contentW, stripH);

        auto bar = juce::Rectangle<int> (0, 0, contentW, stripH).reduced (gap, topY);   // content coordinates

        // spare room (a wide screen) is shared out evenly on top of what each needs
        const int n = (int) buttons.size();
        int sum = 0;
        for (int w : natural) sum += w;
        const int extra = fits ? juce::jmax (0, (bar.getWidth() - (n - 1) * gap - sum) / n) : 0;
        int x = bar.getX();
        for (int i = 0; i < n; ++i)
        {
            const int w = natural[(size_t) i] + extra;
            buttons[(size_t) i]->setBounds (x, bar.getY(), w, bar.getHeight());
            x += w + gap;
        }
        toolbarStrip.repaint();
    }

    // the colour selector floats over the patch area, next to the sidebar
    if (themePanel != nullptr)
    {
       #if JUCE_ANDROID
        // on a phone the whole window (over the keyboard too): every chip and the
        // selector stay finger-sized
        auto r = juce::Rectangle<int> (layoutW, layoutH).reduced (8);
       #else
        auto r = juce::Rectangle<int> (area.getX() + 12, area.getY() + 50,
                                       juce::jmin (740, area.getWidth() - 24), juce::jmin (420, area.getHeight() - 62));   // (wide enough for the title beside the looks and colourways)
       #endif
        themePanel->setBounds (r);
        themePanel->toFront (false);
    }

    if (renderPanel != nullptr)
    {
        const int w = juce::jmin (440, area.getWidth() - 16), h = juce::jmin (300, area.getHeight() - 16);
        renderPanel->setBounds (juce::Rectangle<int> (w, h).withCentre (area.getCentre()));
        renderPanel->toFront (false);
    }

    // Full Size: over everything but the on-screen keyboard, which lies over the
    // bottom of the plugin with its grip (so moving it never resizes the plugin;
    // hidden with it when the Keys button hides it)
    if (auto* fullView = currentFullView())
    {
        auto full = getLocalBounds();   // in real pixels
        if (midiKeyboard != nullptr && keyboardVisible)
        {
           #if JUCE_ANDROID
            const float kbKeys = 30.0f, kbMinKey = 26.0f;
            const int zoomPillW = 36;
           #else
            const float kbKeys = 36.0f, kbMinKey = 18.0f;
            const int zoomPillW = 26;
           #endif
            const int gripH = fullViewGripHeight();
            auto kbArea = full.withTop (full.getBottom() - fullViewKeyboardHeight (getHeight()));
            for (auto* c : { (juce::Component*) midiKeyboard.get(), (juce::Component*) &keyboardHandle, (juce::Component*) &keyZoomToggle })
                c->setTransform ({});
            midiKeyboard->setBounds (kbArea);
            const float baseKey = juce::jmax (kbMinKey, kbArea.getWidth() / kbKeys);
            if (auto* kb = dynamic_cast<ExpressiveKeyboard*> (midiKeyboard.get()))
                kb->setBaseKeyWidth (baseKey);
            else
                midiKeyboard->setKeyWidth (baseKey);
            keyboardHandle.setBounds (kbArea.getCentreX() - 50, kbArea.getY() - gripH, 100, gripH);
            keyZoomToggle.setBounds (kbArea.getCentreX() + 28 + 8, kbArea.getY() - gripH, zoomPillW, gripH);
        }
        // a card, like a plugin, keeps its size: the keyboard lies over its
        // bottom (6.7.4; moving the keyboard's grip never resizes it)
        if (moduleFullView != nullptr)
            moduleFullView->bottomReserve = 0;
        fullView->setBounds (full);
        fullView->toFront (false);
        if (midiKeyboard != nullptr && keyboardVisible)
        {
            midiKeyboard->toFront (false);
            keyboardHandle.toFront (false);
            keyZoomToggle.toFront (false);
        }
    }

    updateZoomReadout();
}

int AquanodeModularAudioProcessorEditor::fullViewGripHeight() const
{
   #if JUCE_ANDROID
    return 26;   // finger-sized
   #else
    return 16;
   #endif
}

int AquanodeModularAudioProcessorEditor::fullViewKeyboardHeight (int windowHeight) const
{
    if (midiKeyboard == nullptr || ! keyboardVisible)
        return 0;
    // the height the grip set (as in the patch view), kept within the window
    const int wanted = processor.uiKeyboardHeight > 0 ? processor.uiKeyboardHeight : 50;
    return juce::jlimit (50, juce::jmax (60, (int) (windowHeight * 0.5f)), wanted);
}

//==============================================================================
// Plugins section
//==============================================================================
void AquanodeModularAudioProcessorEditor::addPluginGui (const juce::String& pluginId)
{
    const int card = aquanode::plugins::addGuiVersion (processor, pluginId, spawnPosition());

    canvas.rebuild();
    if (card >= 0)
        canvas.selectModule (card);
}

void AquanodeModularAudioProcessorEditor::showPluginPatchMenu (const juce::String& pluginId)
{
    auto* entry = aquanode::plugins::findEntry (pluginId);
    if (entry == nullptr || ! entry->hasPatchVersion())
        return;

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safeThis (this);
    const auto id = pluginId;

    juce::PopupMenu menu;
    // (6.9.17) a patch version in more than one kind (FM12's): each with its own two choices
    const auto kinds = aquanode::plugins::patchKinds (id);
    for (int k = 0; k < juce::jmax (1, kinds.size()); ++k)
    {
        menu.addSectionHeader (entry->name + " - " + (kinds.size() > 1 ? kinds[k] : juce::String ("Patch version")));
        auto build = [safeThis, id, k] (bool replace)
        {
            if (safeThis == nullptr) return;
            aquanode::plugins::patchKind() = k;
            safeThis->buildPluginPatch (id, {}, replace);
            aquanode::plugins::patchKind() = 0;
        };
        menu.addItem ("New patch", [build] { build (true); });
        menu.addItem ("Add to this patch", [build] { build (false); });
    }

    if (entry->presetPatterns.isNotEmpty() && entry->loadPreset != nullptr)
    {
        menu.addSeparator();
        menu.addItem ("New patch from preset...", [safeThis, id] { if (safeThis) safeThis->pickPluginPreset (id, true); });
        menu.addItem ("Add preset to this patch...", [safeThis, id] { if (safeThis) safeThis->pickPluginPreset (id, false); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sidebarViewport)
                                                  .withMousePosition());
}

void AquanodeModularAudioProcessorEditor::buildPluginPatch (const juce::String& pluginId,
                                                            const juce::File& presetFile, bool replace)
{
    const auto name = aquanode::plugins::addPatchVersion (processor, pluginId, presetFile, replace);

    if (name.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugins",
            presetFile != juce::File() ? "That preset could not be read." : "The patch version could not be built.");
        return;
    }

    processor.uiFitOnOpen = false;
    canvas.rebuild();
    canvas.fitToContent();
}

void AquanodeModularAudioProcessorEditor::pickPluginPreset (const juce::String& pluginId, bool replace)
{
    auto* entry = aquanode::plugins::findEntry (pluginId);
    if (entry == nullptr)
        return;

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safeThis (this);
    fileChooser = std::make_unique<juce::FileChooser> ("Load " + entry->name + " preset", juce::File(),
                                                       aqp::android::pickerPatterns (entry->presetPatterns));
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, pluginId, replace] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;

           #if JUCE_ANDROID
            // A content:// document has no path, and every plugin's own
            // loader reads a juce::File - so the preset is copied to a
            // scratch file of the same name first.
            const auto url = fc.getURLResult();
            if (url.isEmpty())
                return;
            auto doc = juce::AndroidDocument::fromDocument (url);
            if (! doc.hasValue())
                return;
            const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("PluginPresets").getChildFile (doc.getInfo().getName());
            scratch.getParentDirectory().createDirectory();
            scratch.deleteFile();
            {
                auto in = doc.createInputStream();
                juce::FileOutputStream out (scratch);
                if (in == nullptr || ! out.openedOk())
                    return;
                out.writeFromInputStream (*in, -1);
            }
            safeThis->buildPluginPatch (pluginId, scratch, replace);
           #else
            const auto file = fc.getResult();
            if (file.existsAsFile())
                safeThis->buildPluginPatch (pluginId, file, replace);
           #endif
        });
}

void AquanodeModularAudioProcessorEditor::applyGenerated (std::function<juce::String (AquanodeModularAudioProcessor&)> build)
{
    if (build == nullptr)
        return;
    processor.generatePatch (build);
    processor.uiFitOnOpen = false;
    canvas.rebuild();

    // With Mutation: three mutations at 50 % on top, as one Undo step
    if (processor.generateWithMutation)
    {
        mutatorPanel.mutateSeveral (0.5f, 3);
    }

    canvas.fitToContent();
}

void AquanodeModularAudioProcessorEditor::runGenerator (const juce::String& generatorId, bool openMenu)
{
    const auto* gen = aquanode::presets::findGenerator (generatorId);
    if (gen == nullptr)
        return;

    if (! openMenu || ! gen->hasMenu())
    {
        // a whole new patch per tap; the seed comes from the system RNG so
        // every tap is different
        auto seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
        applyGenerated (gen->random (seed == 0 ? 1u : seed));
        return;
    }

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safeThis (this);

    aquanode::presets::MenuContext ctx;
    ctx.anchor = &sidebarViewport;
    ctx.selectionChanged = [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->sidebar.repaint();
    };
    ctx.apply = [safeThis] (aquanode::presets::BuildFn build)
    {
        if (safeThis != nullptr)
            safeThis->applyGenerated (std::move (build));
    };
    ctx.pickFile = [safeThis] (const juce::String& title, const juce::String& patterns,
                               std::function<void (juce::MemoryBlock, juce::String)> onLoaded)
    {
        if (safeThis == nullptr)
            return;

        safeThis->fileChooser = std::make_unique<juce::FileChooser> (title, juce::File(), patterns);
        safeThis->fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                            | juce::FileBrowserComponent::canSelectFiles,
            [onLoaded] (const juce::FileChooser& fc)
            {
                juce::MemoryBlock data;
                juce::String name;
               #if JUCE_ANDROID
                // a content:// document has no path: read it as a stream
                const auto url = fc.getURLResult();
                if (url.isEmpty())
                    return;
                auto doc = juce::AndroidDocument::fromDocument (url);
                if (! doc.hasValue())
                    return;
                if (auto in = doc.createInputStream())
                    in->readIntoMemoryBlock (data);
                name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
               #else
                const auto file = fc.getResult();
                if (! file.existsAsFile())
                    return;
                file.loadFileAsData (data);
                name = file.getFileNameWithoutExtension();
               #endif
                if (data.getSize() > 0 && onLoaded != nullptr)
                    onLoaded (std::move (data), name);
            });
    };

    juce::PopupMenu menu;
    gen->fillMenu (menu, ctx);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sidebarViewport)
                                                  .withMousePosition());
}

juce::Point<int> AquanodeModularAudioProcessorEditor::spawnPosition()
{
    // a little in from the top left corner of the view, stepping along with
    // every module added, in patch coordinates (the view may be scrolled and
    // zoomed anywhere)
    const auto onScreen = juce::Point<float> (40.0f + (float) (placementCounter % 4) * 36.0f,
                                              40.0f + (float) (placementCounter % 6) * 30.0f);
    ++placementCounter;
    auto pos = canvas.screenToWorld (onScreen).roundToInt();

    // not right on top of a card already there
    const float step = 30.0f / juce::jmax (0.1f, canvas.getZoom());
    for (int tries = 0; tries < 12; ++tries)
    {
        bool taken = false;
        for (int id : processor.getModuleIds())
            if (auto* inst = processor.getInstance (id))
                if (std::abs (inst->position.x - pos.x) < 20 && std::abs (inst->position.y - pos.y) < 20)
                    taken = true;
        if (! taken)
            break;
        pos += juce::Point<float> (step * 1.2f, step).roundToInt();
    }
    return pos;
}

void AquanodeModularAudioProcessorEditor::updateZoomReadout()
{
    sidebarHeader.setZoomText (juce::String (juce::roundToInt (canvas.getZoom() * 100.0f)) + "%");
}

//==============================================================================
// Full Screen (the standalone app on a desktop) and the plugins' Full Size view
//==============================================================================
bool AquanodeModularAudioProcessorEditor::hasFullScreenButton() const
{
   #if JUCE_ANDROID
    return false;
   #else
    return processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
   #endif
}

void AquanodeModularAudioProcessorEditor::toggleFullScreen()
{
   #if ! JUCE_ANDROID
    auto* top = getTopLevelComponent();
    if (top == nullptr)
        return;
    auto& desktop = juce::Desktop::getInstance();
    const bool goFull = desktop.getKioskModeComponent() != top;
    desktop.setKioskModeComponent (goFull ? top : nullptr, false);
    resized();
   #endif
}

juce::Component* AquanodeModularAudioProcessorEditor::currentFullView() const
{
    if (pluginFullView != nullptr) return pluginFullView.get();
    return moduleFullView.get();
}

void AquanodeModularAudioProcessorEditor::toggleModuleFullScreen (int instanceId)
{
    if (moduleFullView != nullptr) { closeModuleFullScreen(); return; }
    if (pluginFullView != nullptr) return;
    auto* card = canvas.moduleCard (instanceId);
    if (card == nullptr) return;
    // a plugin card: its own window, at its own size (Full Size)
    if (auto* lender = dynamic_cast<aquanode::plugins::PluginEditorLender*> (card->getExtraContent());
        lender != nullptr && lender->wantsFullSizePill())
    {
        showPluginFullSize (instanceId);
        return;
    }
    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    moduleFullView = std::make_unique<ModuleFullView> (processor, canvas, *card, [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModuleFullScreen(); });
    });
    addAndMakeVisible (*moduleFullView);
    if (moduleFullView->isShowing())
        moduleFullView->grabKeyboardFocus();   // (for Escape)

   #if ! JUCE_ANDROID && ! JUCE_IOS
    // the window takes the card's shape, as large as the screen allows (up to
    // three times its size); the keyboard lies over its bottom, as on a plugin
    sizeBeforeFullView = getLocalBounds();
    const auto n = moduleFullView->nativeSize();
    juce::Rectangle<int> screen (0, 0, 1600, 1000);
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        screen = d->userArea;
    const int kb = 0;
    const int strip = ModuleFullView::stripHeight() + 4;
    // (a card taller than the screen shrinks to fit it)
    const float scale = juce::jlimit (0.5f, 3.0f, juce::jmin ((float) screen.getWidth() * 0.92f / (float) n.x,
                                                              ((float) screen.getHeight() * 0.88f - (float) (kb + strip)) / (float) n.y));
    const int w = juce::roundToInt ((float) n.x * scale) + 4, h = juce::roundToInt ((float) n.y * scale) + kb + strip;
    setResizeLimits (160, 120, juce::jmax (kBaseWidth * 4, w), juce::jmax (kBaseHeight * 4, h));
    setSize (w, h);
   #endif
    resized();
}

void AquanodeModularAudioProcessorEditor::closeModuleFullScreen()
{
    if (moduleFullView == nullptr)
        return;
    moduleFullView->giveBack();
    moduleFullView = nullptr;
   #if ! JUCE_ANDROID && ! JUCE_IOS
    setResizeLimits (kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 4, kBaseHeight * 4);
    if (! sizeBeforeFullView.isEmpty())
        setSize (sizeBeforeFullView.getWidth(), sizeBeforeFullView.getHeight());
   #endif
    resized();
}

void AquanodeModularAudioProcessorEditor::showPluginFullSize (int instanceId)
{
    if (pluginFullView != nullptr || moduleFullView != nullptr)
        return;
    auto* card = canvas.moduleCard (instanceId);
    auto* lender = card != nullptr ? dynamic_cast<aquanode::plugins::PluginEditorLender*> (card->getExtraContent()) : nullptr;
    auto* ed = lender != nullptr ? lender->lendEditor() : nullptr;
    if (ed == nullptr)
        return;

    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    pluginFullView = std::make_unique<PluginFullView> (*lender, *ed, [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePluginFullSize(); });
    });
    addAndMakeVisible (*pluginFullView);

   #if ! JUCE_ANDROID
    // the window takes the plugin's own size, as if it were loaded alone
    sizeBeforeFullView = getLocalBounds();
    const auto n = pluginFullView->nativeSize();
    setResizeLimits (160, 120, juce::jmax (kBaseWidth * 4, n.x), juce::jmax (kBaseHeight * 4, n.y));
    setSize (n.x, n.y);
   #endif
    resized();
}

void AquanodeModularAudioProcessorEditor::closePluginFullSize()
{
    if (pluginFullView == nullptr)
        return;
    pluginFullView->giveBack();
    pluginFullView = nullptr;
   #if ! JUCE_ANDROID
    setResizeLimits (kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 4, kBaseHeight * 4);
    if (! sizeBeforeFullView.isEmpty())
        setSize (sizeBeforeFullView.getWidth(), sizeBeforeFullView.getHeight());
   #endif
    resized();
    canvas.repaint();
}

//==============================================================================
// the BPM box
AquanodeModularAudioProcessorEditor::BpmBox::BpmBox (AquanodeModularAudioProcessor& p) : proc (p)
{
    setClickingTogglesState (false);
    refresh();
    startTimerHz (8);   // (an automated tempo, followed live)
}

// a Tempo module in the patch (6.7.9): the first one's id, -1 for none - and
// whether something modulates its BPM (then the box only shows the tempo)
int AquanodeModularAudioProcessorEditor::BpmBox::tempoModule (bool& modulated) const
{
    modulated = false;
    for (int id : proc.getModuleIds())
        if (auto* inst = proc.getInstance (id); inst != nullptr && inst->descriptor().typeId == "util.tempo")
        {
            // (6.9.10: a Tempo whose Tempo Out feeds lanes is theirs, not the patch's)
            bool feedsLanes = false;
            for (const auto& c : proc.getCables())
                feedsLanes = feedsLanes || (c.fromModule == id && c.fromSocket == "tempoOut");
            if (feedsLanes)
                continue;
            for (const auto& pc : proc.getParamCables())
                if (pc.toModule == id && pc.paramId == "bpm") modulated = true;
            return id;
        }
    return -1;
}

bool AquanodeModularAudioProcessorEditor::BpmBox::locked() const
{
    bool modulated = false;
    return proc.hostGivesBpm.load() || (tempoModule (modulated) >= 0 && modulated);
}

// a tempo set here: on a Tempo module's knob when there is one (it sets the
// patch's tempo from there), else straight
void AquanodeModularAudioProcessorEditor::BpmBox::setTempo (double bpm)
{
    bool modulated = false;
    const int id = tempoModule (modulated);
    if (id >= 0)
        if (auto* inst = proc.getInstance (id))
            inst->dsp->setParameter ("bpm", (float) bpm);
    proc.patchBpm.store (bpm);
}

void AquanodeModularAudioProcessorEditor::BpmBox::refresh()
{
    const bool host = proc.hostGivesBpm.load();
    bool modulated = false;
    const int tempo = tempoModule (modulated);
    const double bpm = host ? proc.currentBpm.load() : proc.patchBpm.load();
    const auto txt = (std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 1)) + " BPM";
    if (txt != getButtonText()) setButtonText (txt);
    const bool lock = host || (tempo >= 0 && modulated);
    setAlpha (lock ? 0.6f : 1.0f);
    const auto tip = host ? juce::String ("The host's tempo")
                   : lock ? juce::String ("The tempo, automated by the patch's Tempo module (its BPM is modulated): shown here, set there")
                   : tempo >= 0 ? juce::String ("The patch's tempo - it turns the Tempo module's knob: drag up or down, or tap to type it")
                   : juce::String ("The patch's tempo, where no host gives one: drag up or down, or tap to type it");
    if (tip != getTooltip()) setTooltip (tip);
}

void AquanodeModularAudioProcessorEditor::BpmBox::mouseDown (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDown (e);
    dragStart = proc.patchBpm.load();
    dragged = false;
}

void AquanodeModularAudioProcessorEditor::BpmBox::mouseDrag (const juce::MouseEvent& e)
{
    if (locked()) return;
    const int d = e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY();
    if (! dragged && std::abs (d) < 4) return;
    dragged = true;
    const double step = e.mods.isShiftDown() ? 0.05 : 0.25;
    setTempo (juce::jlimit (20.0, 400.0, std::round ((dragStart + d * step) * 10.0) / 10.0));
    refresh();
}

void AquanodeModularAudioProcessorEditor::BpmBox::mouseUp (const juce::MouseEvent& e)
{
    juce::TextButton::mouseUp (e);
    if (dragged || locked() || ! getLocalBounds().contains (e.getPosition()))
        return;
    entry = std::make_unique<juce::AlertWindow> ("Tempo", "Beats per minute (20 - 400):", juce::MessageBoxIconType::NoIcon);
    entry->addTextEditor ("bpm", juce::String (proc.patchBpm.load(), 1));
    entry->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    entry->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BpmBox> safe (this);
    entry->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int r)
    {
        if (safe == nullptr || safe->entry == nullptr) return;
        if (r == 1)
        {
            const double v = safe->entry->getTextEditorContents ("bpm").getDoubleValue();
            if (v >= 20.0 && v <= 400.0)
                safe->setTempo (std::round (v * 100.0) / 100.0);
        }
        safe->entry.reset();
        safe->refresh();
    }), false);
}

//==============================================================================
// The top bar's menus (6.7.1). Each item presses the button that always did the
// work; those buttons are no longer shown.
void AquanodeModularAudioProcessorEditor::pressHidden (juce::TextButton& b)
{
    if (! b.isEnabled())
        return;
    if (b.getClickingTogglesState())
        b.setToggleState (! b.getToggleState(), juce::dontSendNotification);
    if (b.onClick)
        b.onClick();
}

void AquanodeModularAudioProcessorEditor::refreshEditMenuButton()
{
    // (Move Only shows on its own button again)
    const bool region = regionButton.getToggleState();
    const juce::String text = region ? "Edit (Region)" : "Edit";
    editMenuButton.setToggleState (region, juce::dontSendNotification);
    if (editMenuButton.getButtonText() != text)
    {
        editMenuButton.setButtonText (text);
        resized();
    }
}

void AquanodeModularAudioProcessorEditor::showPatchMenu()
{
    juce::PopupMenu m;
    m.addItem (5, "Clear Patch");
    m.addItem (1, "Initialize Patch");
    m.addSeparator();
    m.addItem (3, "Save Patch ...");
    m.addItem (2, "Load Patch ...");
    m.addSeparator();
    m.addItem (4, "Render to File ...", renderButton.isEnabled());
    // (6.9.15: Latency Compensation is under CPU, with the other latency choices)
    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patchMenuButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 5)
        {
            // an empty patch area (6.7.7) - nothing at all, not even Audio Out
            safe->processor.generatePatch ([] (AquanodeModularAudioProcessor&) { return juce::String ("Empty"); });
            safe->canvas.rebuild();
        }
        if (r == 1) safe->pressHidden (safe->initButton);
        if (r == 2) safe->pressHidden (safe->importButton);
        if (r == 3) safe->pressHidden (safe->exportButton);
        if (r == 4) safe->pressHidden (safe->renderButton);
    });
}

void AquanodeModularAudioProcessorEditor::showEditMenu()
{
    juce::PopupMenu m;
    m.addItem (5, "Undo", processor.canUndoPatch());
    m.addItem (6, "Redo", processor.canRedoPatch());
    m.addSeparator();
    m.addItem (1, "Clone Selected", cloneButton.isEnabled());
    m.addItem (2, "Delete Selected", deleteButton.isEnabled());
    m.addSeparator();
    m.addItem (3, "Region Select (a drag on empty space draws a box)", true, regionButton.getToggleState());
    m.addItem (4, "Move Only (every gesture moves the view)", true, moveOnlyButton.getToggleState());
   #if ! JUCE_ANDROID && ! JUCE_IOS
    m.addSeparator();
    m.addItem (7, "Computer Keyboard as MIDI (Z..M, Q..P)", true, computerKeys != nullptr);
   #endif
    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&editMenuButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 7) safe->setComputerKeys (safe->computerKeys == nullptr);
        if (r == 1) safe->pressHidden (safe->cloneButton);
        if (r == 2) safe->pressHidden (safe->deleteButton);
        if (r == 3) safe->pressHidden (safe->regionButton);
        if (r == 4) safe->pressHidden (safe->moveOnlyButton);
        if (r == 5) safe->undoRedo (false);
        if (r == 6) safe->undoRedo (true);
    });
}

void AquanodeModularAudioProcessorEditor::showViewMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Keys", true, keyboardVisible);
    m.addItem (2, "Sidebar", true, sidebarVisible);
    m.addItem (3, "Mutator", true, mutatorButton.getToggleState());
    m.addSeparator();
    m.addItem (4, "Zoom In (+)");
    m.addItem (5, "Zoom Out (-)");
    m.addItem (6, "Reset View (to the origin, at 100 %)");   // (6.9.15: from the colour picker)
    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&viewMenuButton), [safe] (int r)
    {
        if (safe != nullptr && r == 6) safe->canvas.resetView();
        if (safe == nullptr) return;
        if (r == 1) safe->pressHidden (safe->keysButton);
        if (r == 2) safe->pressHidden (safe->sidebarButton);
        if (r == 3) safe->pressHidden (safe->mutatorButton);
        if (r == 4) safe->canvas.zoomBy (1.25f);
        if (r == 5) safe->canvas.zoomBy (1.0f / 1.25f);
    });
}

// Full Screen in the standalone app on a desktop: the window's own maximise
// button (JUCE's standalone window shows only minimise and close)
void AquanodeModularAudioProcessorEditor::parentHierarchyChanged()
{
    AudioProcessorEditor::parentHierarchyChanged();
   #if ! JUCE_ANDROID && ! JUCE_IOS
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, false);
   #endif
}

//==============================================================================
// Plugin Block: the block every plugin card runs in (unless its own pill says otherwise)
void AquanodeModularAudioProcessorEditor::showPluginBlockMenu()
{
    using HP = aquanode::plugins::HostedPluginModule;
    using AP = AquanodeModularAudioProcessor;
    const int currentPlugins = HP::globalBlockSize().load();
    juce::PopupMenu m;
    // (6.9.11) the modules always run sample by sample: blocks gave them
    // nothing measurable; the plugins' block, lined up with the rest of the patch
    m.addSectionHeader ("Plugin Block (cards on Global)");
    for (int sz : AP::blockSizeChoices())
        m.addItem (sz, juce::String (sz) + (sz == 1 ? " sample" : " samples") + (sz == 128 ? "  (default)" : ""), true, sz == currentPlugins);
    m.addItem (-1, "Above 1: a plugin's sound comes a block later", false, false);
    static constexpr int perfHintItem = 10000;
    static constexpr int cpuSleepItem = 10001;
    static constexpr int neverAlignItem = 10002;
    static constexpr int reportItem = 10003;
    static constexpr int alignAllItem = 10004, alignPluginsItem = 10005;
    static constexpr int opaqueItem = 10006;
    static constexpr int antiAliasItem = 10007;
    // (6.9.15) one choice, short: what gets lined up where paths meet. The first two are saved
    // with the patch (how it sounds); Nothing is for every patch (the least delay, playing live)
    m.addSeparator();
    m.addSectionHeader ("Line Up Latency");
    const bool never = AP::neverCompensate().load();
    m.addItem (alignAllItem, "Everything (this patch)", true, ! never && processor.getLatencyCompensation());
    m.addItem (alignPluginsItem, "Plugin Blocks Only (this patch)", true, ! never && ! processor.getLatencyCompensation());
    m.addItem (neverAlignItem, "Nothing (all patches)", true, never);
    m.addItem (reportItem, "Report Latency to the DAW", true, AP::reportLatency().load());
    m.addSeparator();
    m.addItem (cpuSleepItem, "CPU Sleep (effects rest while silent)", true, aquanode::cpuSleepEnabled().load());
    m.addItem (opaqueItem, "Disable GUI Transparency (faster Glass and Skeuomorphic)", true, guiOpaque());
    // (6.9.21) all patches: the oscillators that alias on high notes band-limit themselves
    m.addItem (antiAliasItem, "Anti-alias (cleaner high notes, a little more CPU)", true, aquanode::antiAliasOn());
    if (aquanode::perfhint::available())
    {
        m.addSeparator();
        m.addSectionHeader ("Phone CPU");
        m.addItem (perfHintItem, "Stay Fast While Playing", true,
                   aquanode::perfhint::switchedOn().load());
    }
    juce::Component::SafePointer<AquanodeModularAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pluginBlockButton),
                     [safe] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         if (r == cpuSleepItem)
                         {
                             safe->pressHidden (safe->cpuSleepButton);
                             return;
                         }
                         if (r == antiAliasItem)
                         {
                             const bool on = ! aquanode::antiAliasOn();
                             aquanode::antiAliasEnabled().store (on);
                             juce::PropertiesFile file (settingsOptions());
                             if (on) file.setValue ("audio.antialias", true); else file.removeValue ("audio.antialias");
                             file.saveIfNeeded();
                             return;
                         }
                         if (r == opaqueItem)
                         {
                             guiOpaque() = ! guiOpaque();
                             juce::PropertiesFile file (settingsOptions());
                             if (guiOpaque()) file.setValue ("gui.opaque", true); else file.removeValue ("gui.opaque");
                             file.saveIfNeeded();
                             safe->repaint();
                             return;
                         }
                         if (r == perfHintItem)
                         {
                             const bool on = ! aquanode::perfhint::switchedOn().load();
                             aquanode::perfhint::switchedOn().store (on);
                             aquanode::perfhint::update();
                             juce::PropertiesFile file (settingsOptions());
                             if (on) file.removeValue ("perf.hint"); else file.setValue ("perf.hint", false);
                             file.saveIfNeeded();
                             return;
                         }
                         juce::PropertiesFile file (settingsOptions());
                         if (r == alignAllItem || r == alignPluginsItem || (r == neverAlignItem && AP::neverCompensate().load()))
                         {
                             // (Nothing ticked again: back to the patch's own choice)
                             if (AP::neverCompensate().load())
                             {
                                 AP::neverCompensate().store (false);
                                 file.removeValue ("latency.never");
                                 file.saveIfNeeded();
                             }
                             if (r != neverAlignItem)
                                 safe->processor.setLatencyCompensation (r == alignAllItem);
                             AP::rebuildAllGraphs();
                             return;
                         }
                         if (r == neverAlignItem || r == reportItem)
                         {
                             auto& flag = r == neverAlignItem ? AP::neverCompensate() : AP::reportLatency();
                             const bool on = ! flag.load();
                             flag.store (on);
                             const char* key = r == neverAlignItem ? "latency.never" : "latency.report";
                             const bool byDefault = r == reportItem;
                             if (on == byDefault) file.removeValue (key); else file.setValue (key, on);
                             file.saveIfNeeded();
                             AP::rebuildAllGraphs();
                             return;
                         }
                         HP::globalBlockSize().store (r);
                         if (r == 128) file.removeValue ("plugins.block"); else file.setValue ("plugins.block", r);
                         file.saveIfNeeded();
                         AquanodeModularAudioProcessor::rebuildAllGraphs();   // (lined up again with the new latency)
                         safe->resized();
                         safe->repaint();
                     });
}
