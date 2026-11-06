// The editor: the top bar, the sidebar, the patch area and the keys put together,
// Import / Export Patch, and adding the plugins.
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
              const auto note = adder->apply (processor, seed == 0 ? 1u : seed);
              if (note.isNotEmpty() && ! processor.patchName.contains ("+ " + note))
                  processor.patchName = processor.patchName + " + " + note;
              canvas.rebuild();
              canvas.fitToContent();
              return;
          }

          // clicking a sidebar name opens one instance in the patch map,
          // near the top left of what is in view
          const auto pos = spawnPosition();
          const int newId = processor.addModule (typeId, pos);
          canvas.rebuild();
          canvas.selectModule (newId);
      }),
      canvas (p),
      mutatorPanel (p, canvas)
{
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

    for (auto* b : { &initButton, &regionButton, &cloneButton, &deleteButton, &exportButton, &importButton, &renderButton,
                     &mutatorButton, &sidebarButton, &keysButton, &undoPatchButton, &redoPatchButton })
        toolbarStrip.content.addAndMakeVisible (b);

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
    };
    cloneButton.setTooltip ("A copy of the selected module (or of every module a Region Select box holds, with the cables between them)");
    deleteButton.setTooltip ("Delete the selected module (or every module a Region Select box holds) with its cables");

    // Render to File: active while a Piano Roll with notes is patched somewhere
    renderButton.setTooltip ("Render the patch to a WAV or FLAC file: every Piano Roll with notes that is patched "
                             "somewhere, played through once from the start, and the tails after it");
    renderButton.onClick = [this] { showRenderPanel(); };
    renderButton.setEnabled (aquanode::render::canRender (processor));

    // Undo / Redo of the whole patch (32 steps, in memory only)
    undoPatchButton.setTooltip ("Undo the last change to the patch (Ctrl+Z) - up to 32 steps");
    redoPatchButton.setTooltip ("Redo what was undone (Ctrl+Shift+Z or Ctrl+Y)");
    undoPatchButton.onClick = [this] { undoRedo (false); };
    redoPatchButton.onClick = [this] { undoRedo (true); };
    canvas.onShortcut = [this] (const juce::KeyPress& k)
    {
        const auto mods = k.getModifiers();
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

    for (auto* b : { &zoomOutButton, &zoomInButton })
        toolbarStrip.content.addAndMakeVisible (b);
    // the patch's name and the zoom are shown in the sidebar, under its title
    sidebarHeader.getPatchName = [this] { return processor.patchName; };
    sidebarHeader.onZoomClicked = [this] { canvas.resetView(); };

    sidebarButton.onClick = [this]
    {
        sidebarVisible = ! sidebarVisible;
        processor.uiSidebarVisible = sidebarVisible;
        sidebarViewport.setVisible (sidebarVisible);
        sidebarHeader.setVisible (sidebarVisible);
        resized();
    };

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
        fileChooser = std::make_unique<juce::FileChooser> ("Export Patch", suggested, "*.aqua");
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
                    problem = aqp::android::replaceDocument (url, zipped.getData(), zipped.getDataSize());

                // a name typed without the ending gets it
                if (problem.isEmpty())
                    if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                    {
                        const auto name = doc.getInfo().getName();
                        if (name.isNotEmpty() && ! name.endsWithIgnoreCase (".aqua") && ! name.endsWithIgnoreCase (".zip"))
                            doc.renameTo (name + ".aqua");
                    }

                if (problem.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export Patch",
                                                            "The patch could not be saved: " + problem + ".");
               #else
                auto file = fc.getResult();
                if (file != juce::File())
                    if (! processor.exportPatchToZip (file.withFileExtension ("aqua")))   // a .zip inside
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export Patch",
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
        fileChooser = std::make_unique<juce::FileChooser> ("Import Patch", juce::File(), patterns);
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
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import Patch",
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
    setResizeLimits (kBaseWidth / 2, kBaseHeight / 2, kBaseWidth * 3, kBaseHeight * 3);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseWidth / (double) kBaseHeight);
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

AquanodeModularAudioProcessorEditor::~AquanodeModularAudioProcessorEditor()
{
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
    std::vector<juce::Button*> buttons { &initButton, &regionButton, &cloneButton, &deleteButton, &exportButton, &importButton, &renderButton,
                                         &mutatorButton, &sidebarButton, &keysButton, &zoomOutButton, &zoomInButton,
                                         &undoPatchButton, &redoPatchButton };
    for (auto* b : mutatorPanel.getButtons())
        buttons.push_back (b);
    const bool themed = aquanode::barsAreThemed();
    const auto bar = aquanode::barColour();
    const auto face = bar.interpolatedWith (themeColours().get (ThemeColours::Oscillator), 0.35f).brighter (0.15f);
    const auto on = themeColours().get (ThemeColours::Oscillator);
    auto* gloss = themeColours().skeuomorphic ? &aeroButtonLookAndFeel() : nullptr;   // Skeuomorphic: glossy pills
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

void AquanodeModularAudioProcessorEditor::resized()
{
   #if ! JUCE_ANDROID
    // Uniform scaling: lay out on the base area and draw every child (but the
    // host's resize corner) through one scale, so all of it grows and shrinks
    // together - text included.
    uiScale = juce::jmax (0.1f, juce::jmin ((float) getWidth() / (float) kBaseWidth, (float) getHeight() / (float) kBaseHeight));
    for (auto* child : getChildren())
        if (dynamic_cast<juce::ResizableCornerComponent*> (child) == nullptr)
            child->setTransform (juce::AffineTransform::scale (uiScale));
   #endif
    // the layout's own size: the window's, before the scale
    const int layoutW = juce::roundToInt ((float) getWidth() / uiScale);
    const int layoutH = juce::roundToInt ((float) getHeight() / uiScale);
    auto area = juce::Rectangle<int> (layoutW, layoutH);

    // Bottom on-screen keyboard, height scaled to the window (clamped sane):
    // finger-sized on a phone, a little more compact for a mouse.
   #if JUCE_ANDROID
    const int kbH = juce::jlimit (110, 220, layoutH / 4);
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
    // Slightly wider sidebar and bigger buttons for touch.
    const int sideW = juce::jlimit (200, 300, layoutW / 5);
    const int bh = 40, gap = 8, topY = 8;
   #else
    const int sideW = SidebarComponent::sidebarWidth;
    const int bh = 22, gap = 5, topY = 8;
   #endif

    // Hidden sidebar gives its whole width back to the patch area.
    {
        auto side = area.removeFromLeft (sidebarVisible ? sideW : 0);
        sidebarHeader.setBounds (side.removeFromTop (sidebarVisible ? SidebarHeader::preferredHeight() : 0));
        sidebarViewport.setBounds (side);
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

    // the mutator strip, when open, right below the top bar (clear of the
    // keyboard's grip at the bottom)
   #if JUCE_ANDROID
    const int mutH = 46;
   #else
    const int mutH = 34;
   #endif
    mutatorPanel.setBounds (area.getX(), strip.getBottom(), area.getWidth(), mutH);

    {
        // left to right on screen: Keys, Sidebar, Initialize ... Mutator, then [-][+]
        std::vector<juce::TextButton*> buttons { &keysButton, &sidebarButton, &undoPatchButton, &redoPatchButton, &initButton,
                                                 &regionButton, &cloneButton, &deleteButton, &exportButton, &importButton, &renderButton, &mutatorButton };
       #if JUCE_ANDROID
        const int zw = 30;
       #else
        const int zw = 25;
       #endif
        // what each button's text needs
        std::vector<int> natural;
        int needed = gap + 2 * zw + gap + gap;
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
        auto zoomArea = bar.removeFromRight (zw * 2 + gap);
        zoomInButton.setBounds (zoomArea.removeFromRight (zw));
        zoomArea.removeFromRight (gap);
        zoomOutButton.setBounds (zoomArea);
        bar.removeFromRight (gap);

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
                                       juce::jmin (620, area.getWidth() - 24), juce::jmin (420, area.getHeight() - 62));
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

    updateZoomReadout();
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
    menu.addSectionHeader (entry->name + " - Patch version");
    menu.addItem ("New patch", [safeThis, id] { if (safeThis) safeThis->buildPluginPatch (id, {}, true); });
    menu.addItem ("Add to this patch", [safeThis, id] { if (safeThis) safeThis->buildPluginPatch (id, {}, false); });

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