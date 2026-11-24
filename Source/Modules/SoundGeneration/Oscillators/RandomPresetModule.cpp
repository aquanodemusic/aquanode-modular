// Random Plugin Preset: one of the synth plugins, playing a preset its own
// generator rolled. Four dropdowns - Plugin, Engine (Native / Original where the
// plugin has both, "-" elsewhere), Kind (the plugin's own names for its kinds of
// preset, its "Anything" first chosen) and Genre (Massif's; "-" for the others).
// Apply rolls a preset of that kind, loading the plugin first if it is not the
// one playing; Plugin Window shows it in the plugin's own window (the Full Size
// view; Return to Patch comes back); Save Preset writes it as the plugin's own
// preset file; Open in Plugin puts a plugin card holding it in the module's
// place, wired the same way.
//
// The plugin runs inside a GUI card of its own (a HostedPluginModule, made by
// the module factory, never on the canvas) that this module passes its audio,
// MIDI and host information to. It is swapped on the message thread under a
// spin lock the audio thread only try-locks: a few samples of silence while a
// plugin is put in. The patch keeps the dropdowns, which plugin is playing and
// its whole state.
#include "../../Core/ModuleCore.h"
#include "../../../Plugins/Host/HostedPlugin.h"
#include "../../../Plugins/Host/PluginCatalog.h"
#include "../../../Plugins/Host/RandomPresetSources.h"
#include "../../../Platform/AndroidDocuments.h"

using namespace aquanode;
namespace rp = aquanode::plugins::randompreset;

class RandomPresetModule final : public SynthModule,
                                 public plugins::randompreset::PluginPresetHolder
{
public:
    RandomPresetModule()
    {
        // AlphaBeta first, as it always was (the list is A-Z, and Additive Keys now sorts before it)
        const auto all = rp::sources();
        if (rp::find ("alphabeta") != nullptr)
            choosePlugin ("alphabeta");
        else if (! all.empty())
            choosePlugin (all.front()->pluginId);
    }

    ~RandomPresetModule() override
    {
        std::shared_ptr<SynthModule> old;
        {
            const juce::SpinLock::ScopedLockType sl (swapLock);
            old = std::move (inner);
        }
    }

    //=== the dropdowns and buttons (message thread) ===========================
    const juce::String& chosenPluginId() const { return chosenPlugin; }
    int chosenKindIndex() const { return chosenKind; }
    int chosenGenreIndex() const { return chosenGenre; }
    int chosenEngineIndex() const { return chosenEngine; }

    void choosePlugin (const juce::String& id)
    {
        if (id == chosenPlugin)
            return;
        chosenPlugin = id;
        chosenKind = chosenGenre = chosenEngine = 0;
        if (auto* s = rp::find (id))
        {
            const auto& l = rp::listsOf (*s);
            chosenKind = l.defaultKind;
            chosenGenre = l.defaultGenre;
            chosenEngine = l.defaultEngine;
        }
        ++changeCounter;
    }

    void chooseKind (int k)  { chosenKind = juce::jmax (0, k); ++changeCounter; }
    void chooseGenre (int g) { chosenGenre = juce::jmax (0, g); ++changeCounter; }
    void chooseEngine (int e) { chosenEngine = juce::jmax (0, e); ++changeCounter; }

    // loads the chosen plugin if it is not the one playing, then rolls a preset
    // of the chosen kind into it
    void apply()
    {
        configured = true;
        auto* source = rp::find (chosenPlugin);
        if (source == nullptr)
            return;

        if (playingPlugin != chosenPlugin || inner == nullptr)
        {
            auto fresh = makePlugin (chosenPlugin);
            if (fresh == nullptr)
            {
                status = "Could not load " + source->name;
                ++changeCounter;
                return;
            }
            setInner (std::move (fresh));
            playingPlugin = chosenPlugin;
        }

        juce::String name;
        if (auto* p = playingProcessor())
        {
            const auto seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff) | 1u;
            name = source->roll (*p, chosenKind, chosenGenre, chosenEngine, seed);
        }
        presetName = name.isNotEmpty() ? name : juce::String ("Random preset");
        playingEngine = chosenEngine;
        status = statusFor (*source);
        ++changeCounter;
    }

    // Save Preset: the playing sound as the plugin's own preset file
    void savePreset()
    {
        auto* source = rp::find (playingPlugin);
        auto* p = playingProcessor();
        if (source == nullptr || p == nullptr || source->presetData == nullptr)
            return;
        const auto ext = source->presetExtension;
        auto base = juce::File::createLegalFileName (presetName.isNotEmpty() ? presetName : source->name);
        chooser = std::make_unique<juce::FileChooser> ("Save the " + source->name + " preset",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (base + "." + ext), "*." + ext);
        juce::WeakReference<SynthModule> weak (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
            [weak, ext] (const juce::FileChooser& fc)
            {
                auto* self = dynamic_cast<RandomPresetModule*> (weak.get());
                if (self == nullptr)
                    return;
                // getURLResult: on Android the picked document is a content:// URI
                auto destination = fc.getURLResult();
                if (destination.isEmpty())
                    return;
                if (destination.isLocalFile() && ! destination.getLocalFile().hasFileExtension (ext))
                    destination = juce::URL (destination.getLocalFile().withFileExtension (ext));
                auto* src = rp::find (self->playingPlugin);
                auto* proc = self->playingProcessor();
                juce::MemoryBlock data;
                juce::String problem;
                if (src == nullptr || proc == nullptr || ! src->presetData (*proc, self->presetName, data))
                    problem = "the plugin could not write it";
                else
                    problem = aqp::files::writeDocument (destination, data.getData(), data.getSize());
                if (problem.isNotEmpty())
                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Random Plugin Preset",
                                                                 "The preset was not saved: " + problem + ".");
            });
    }

    //=== PluginPresetHolder (Open in Plugin) ==================================
    juce::String heldPluginId() const override { return inner != nullptr ? playingPlugin : juce::String(); }
    juce::AudioProcessor* heldPlugin() const override { return playingProcessor(); }

    juce::String statusFor (const rp::Source& s) const
    {
        const auto& l = rp::listsOf (s);
        juce::String t = s.name;
        if (l.engines.size() > 1 && playingEngine < l.engines.size())
            t << " (" << l.engines[playingEngine] << ")";
        return t + ": " + (presetName.isNotEmpty() ? presetName : juce::String ("its last preset"));
    }

    bool isConfigured() const { return configured; }
    bool hasPlugin() const { return inner != nullptr; }
    bool playsChosenPlugin() const { return inner != nullptr && playingPlugin == chosenPlugin; }
    bool canSave() const { auto* s = rp::find (playingPlugin); return inner != nullptr && s != nullptr && s->presetData != nullptr; }
    const juce::String& statusText() const { return status; }
    int getChangeCounter() const { return changeCounter.load(); }

    // the plugin playing, and the card module that holds it alive (message thread)
    juce::AudioProcessor* playingProcessor() const
    {
        auto* h = dynamic_cast<plugins::HostedPluginModule*> (inner.get());
        return h != nullptr ? h->getPlugin() : nullptr;
    }
    std::shared_ptr<SynthModule> playingModule() const { return inner; }

    //=== SynthModule ==========================================================
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }

    void prepare (double newSampleRate) override
    {
        SynthModule::prepare (newSampleRate);
        const juce::SpinLock::ScopedLockType sl (swapLock);
        if (inner != nullptr)
            inner->prepare (newSampleRate);
        prepared = true;
    }

    void reset() override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked() && inner != nullptr)
            inner->reset();
    }

    void blockStart() override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked() && inner != nullptr)
        {
            inner->setHostPlayHead (hostPlayHead);
            inner->setTempo (tempoBpm);
            inner->setHostTransport (hostTransport);
            inner->setMidiControls (midiControls);
            inner->setHostModValues (hostModValues);
            inner->blockStart();
        }
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked() && inner != nullptr)
            inner->processSample (inputs, outputs);
        else
            outputs[0] = { 0.0f, 0.0f };
    }

    void rawMidiMessage (const juce::MidiMessage& message) override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked() && inner != nullptr)
            inner->rawMidiMessage (message);
    }

    void midiAllNotesOff() override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked() && inner != nullptr)
            inner->midiAllNotesOff();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 300; }
    int extraContentHeight() const override { return 198; }

    //=== the patch ============================================================
    juce::String saveCustomState() const override
    {
        const auto innerState = inner != nullptr ? inner->saveCustomState() : juce::String();
        const auto head = chosenPlugin + "|" + juce::String (chosenKind) + "|" + juce::String (chosenGenre)
                        + "|" + juce::String (chosenEngine) + "|" + juce::String (playingEngine)
                        + "|" + playingPlugin + "|" + presetName;
        if (head == cachedHead && innerState == cachedInner && cachedOut.isNotEmpty())
            return cachedOut;

        juce::XmlElement root ("RandomPreset");
        root.setAttribute ("version", 1);
        root.setAttribute ("plugin", chosenPlugin);
        root.setAttribute ("kind", chosenKind);
        root.setAttribute ("genre", chosenGenre);
        root.setAttribute ("engine", chosenEngine);
        root.setAttribute ("playingEngine", playingEngine);
        root.setAttribute ("playing", playingPlugin);
        root.setAttribute ("preset", presetName);
        if (innerState.isNotEmpty())
            root.createNewChildElement ("State")->addTextElement (innerState);
        cachedHead = head;
        cachedInner = innerState;
        cachedOut = root.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
        return cachedOut;
    }

    void loadCustomState (const juce::String& text) override
    {
        auto xml = juce::parseXML (text);
        if (xml == nullptr || ! xml->hasTagName ("RandomPreset"))
            return;
        configured = true;
        chosenPlugin = xml->getStringAttribute ("plugin", chosenPlugin);
        chosenKind = juce::jmax (0, xml->getIntAttribute ("kind", chosenKind));
        chosenGenre = juce::jmax (0, xml->getIntAttribute ("genre", chosenGenre));
        chosenEngine = juce::jmax (0, xml->getIntAttribute ("engine", chosenEngine));
        playingEngine = juce::jmax (0, xml->getIntAttribute ("playingEngine", chosenEngine));
        presetName = xml->getStringAttribute ("preset");

        const auto playing = xml->getStringAttribute ("playing");
        std::shared_ptr<SynthModule> fresh;
        if (playing.isNotEmpty())
        {
            fresh = makePlugin (playing);
            if (fresh != nullptr)
                fresh->loadCustomState (xml->getChildElementAllSubText ("State", {}));
        }
        setInner (std::move (fresh));
        playingPlugin = inner != nullptr ? playing : juce::String();

        if (auto* s = rp::find (playingPlugin))
            status = statusFor (*s);
        else
            status = playing.isNotEmpty() ? "Not available here: " + playing : juce::String();
        ++changeCounter;
    }

private:
    static std::shared_ptr<SynthModule> makePlugin (const juce::String& id)
    {
        auto* entry = plugins::findEntry (id);
        if (entry == nullptr)
            return nullptr;
        auto m = ModuleFactory::instance().createInstance (entry->guiTypeId);
        if (dynamic_cast<plugins::HostedPluginModule*> (m.get()) == nullptr)
            return nullptr;
        return std::shared_ptr<SynthModule> (std::move (m));
    }

    // message thread: the new plugin is prepared before it goes live, the old
    // one is let go of here (never on the audio thread)
    void setInner (std::shared_ptr<SynthModule> fresh)
    {
        if (fresh != nullptr && prepared)
            fresh->prepare (sampleRate);
        std::shared_ptr<SynthModule> old;
        {
            const juce::SpinLock::ScopedLockType sl (swapLock);
            old = std::move (inner);
            inner = std::move (fresh);
        }
        old = nullptr;
    }

    std::shared_ptr<SynthModule> inner;
    juce::SpinLock swapLock;
    bool prepared { false };

    juce::String chosenPlugin, playingPlugin, presetName, status;
    int chosenKind { 0 }, chosenGenre { 0 }, chosenEngine { 0 }, playingEngine { 0 };
    std::unique_ptr<juce::FileChooser> chooser;
    bool configured { false };
    std::atomic<int> changeCounter { 0 };

    mutable juce::String cachedHead, cachedInner, cachedOut;
};

//==============================================================================
// the card: Plugin / Engine / Kind / Genre, Apply, Plugin Window, Save Preset
// and Open in Plugin, and what is playing. It lends the playing plugin's editor
// to the Full Size view (Plugin Window).
namespace
{
    class RandomPresetPanel final : public juce::Component,
                                    public plugins::PluginEditorLender,
                                    private juce::Timer
    {
    public:
        explicit RandomPresetPanel (RandomPresetModule& m) : module (&m)
        {
            for (auto* b : { &applyButton, &openButton, &saveButton, &inPluginButton })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3038));
                b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1a8fa6));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            }
            for (auto* c : { &pluginBox, &engineBox, &kindBox, &genreBox })
            {
                addAndMakeVisible (*c);
                c->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a3038));
                c->setColour (juce::ComboBox::textColourId, juce::Colours::white);
                c->setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a424c));
                c->setColour (juce::ComboBox::arrowColourId, juce::Colours::white.withAlpha (0.8f));
            }
            applyButton.setTooltip ("Rolls a new preset of this kind - loading the plugin first if it is not the one playing");
            openButton.setTooltip ("Shows the preset in the plugin's own window; Return to Patch comes back");
            saveButton.setTooltip ("Saves the preset as the plugin's own preset file (.massif, .abpreset, .syx ...)");
            inPluginButton.setTooltip ("Puts the plugin itself, holding this preset, in this module's place - wired the same way");
            engineBox.setTooltip ("The plugin's engine, where it has two (Native or Original); - where it has one");
            pluginBox.setTooltip ("The plugin whose own generator rolls the preset");
            kindBox.setTooltip ("The kind of preset, in the plugin's own words");
            genreBox.setTooltip ("The genre to lean towards (Massif); - where the plugin has none");

            const auto all = rp::sources();
            for (int i = 0; i < (int) all.size(); ++i)
            {
                pluginBox.addItem (all[(size_t) i]->name, i + 1);
                pluginIds.add (all[(size_t) i]->pluginId);
            }

            pluginBox.onChange = [this]
            {
                const int i = pluginBox.getSelectedItemIndex();
                if (auto* p = mod(); p != nullptr && i >= 0 && i < pluginIds.size())
                {
                    p->choosePlugin (pluginIds[i]);
                    fillLists();
                }
            };
            kindBox.onChange  = [this] { if (auto* p = mod()) p->chooseKind (kindBox.getSelectedId() - 1); };
            engineBox.onChange = [this] { if (auto* p = mod(); p != nullptr && engineBox.getSelectedId() > 0 && engineBox.getSelectedId() < 1000) p->chooseEngine (engineBox.getSelectedId() - 1); };
            saveButton.onClick = [this] { if (auto* p = mod()) p->savePreset(); };
            inPluginButton.onClick = [this]
            {
                auto* p = mod();
                if (p == nullptr)
                    return;
                if (! p->hasPlugin())
                {
                    p->apply();
                    refresh();
                }
                if (p->hasPlugin() && requestOpenInPlugin)
                    requestOpenInPlugin();
            };
            genreBox.onChange = [this] { if (auto* p = mod(); p != nullptr && genreBox.getSelectedId() > 0) p->chooseGenre (genreBox.getSelectedId() - 1); };
            applyButton.onClick = [this] { if (auto* p = mod()) { p->apply(); refresh(); } };
            openButton.onClick = [this]
            {
                auto* p = mod();
                if (p == nullptr)
                    return;
                if (! p->playsChosenPlugin())
                {
                    p->apply();
                    refresh();
                }
                if (p->hasPlugin() && requestFullSize)
                    requestFullSize();
            };

            fillLists();
            refresh();
            startTimerHz (4);

            // a module just taken from the sidebar plays at once
            if (! m.isConfigured())
            {
                juce::Component::SafePointer<RandomPresetPanel> safe (this);
                juce::MessageManager::callAsync ([safe]
                {
                    if (safe != nullptr)
                        if (auto* p = safe->mod(); p != nullptr && ! p->isConfigured())
                        {
                            p->apply();
                            safe->refresh();
                        }
                });
            }
        }

        ~RandomPresetPanel() override
        {
            dropEditor();
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 3);
            auto row = [&r] (juce::ComboBox& box, juce::Rectangle<int>& labelArea)
            {
                auto line = r.removeFromTop (24);
                labelArea = line.removeFromLeft (46);
                box.setBounds (line);
                r.removeFromTop (4);
            };
            row (pluginBox, pluginLabel);
            row (engineBox, engineLabel);
            row (kindBox, kindLabel);
            row (genreBox, genreLabel);
            auto pair = [&r] (juce::Button& a, juce::Button& b)
            {
                auto line = r.removeFromTop (26);
                const int w = (line.getWidth() - 4) / 2;
                a.setBounds (line.removeFromLeft (w));
                line.removeFromLeft (4);
                b.setBounds (line);
                r.removeFromTop (4);
            };
            pair (applyButton, openButton);
            pair (saveButton, inPluginButton);
            statusArea = r;
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText ("Plugin", pluginLabel, juce::Justification::centredLeft);
            g.drawText ("Engine", engineLabel, juce::Justification::centredLeft);
            g.drawText ("Kind", kindLabel, juce::Justification::centredLeft);
            g.drawText ("Genre", genreLabel, juce::Justification::centredLeft);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText (shown, statusArea, juce::Justification::centredLeft, 1, 0.8f);
        }

        //=== PluginEditorLender: the playing plugin's editor, for Open ========
        bool wantsFullSizePill() const override { return false; }

        juce::Component* lendEditor() override
        {
            auto* p = mod();
            if (p == nullptr || editor != nullptr)
                return nullptr;
            keep = p->playingModule();
            auto* proc = p->playingProcessor();
            if (keep == nullptr || proc == nullptr || ! proc->hasEditor())
            {
                keep = nullptr;
                return nullptr;
            }
           #if JUCE_MAJOR_VERSION >= 9
            editor.reset (proc->createEditorAndMakeActive());
           #else
            editor.reset (proc->createEditorIfNeeded());
           #endif
            if (editor == nullptr)
                keep = nullptr;
            return editor.get();
        }

        void returnEditor() override
        {
            dropEditor();
        }

    private:
        RandomPresetModule* mod() const { return dynamic_cast<RandomPresetModule*> (module.get()); }

        void dropEditor()
        {
            if (editor != nullptr)
            {
                if (auto* parent = editor->getParentComponent())
                    parent->removeChildComponent (editor.get());
                if (auto* h = dynamic_cast<plugins::HostedPluginModule*> (keep.get()))
                    if (auto* proc = h->getPlugin())
                        proc->editorBeingDeleted (editor.get());
            }
            editor = nullptr;      // the editor goes before the plugin
            keep = nullptr;
        }

        // the Kind and Genre lists of the chosen plugin, and the choices
        void fillLists()
        {
            auto* p = mod();
            if (p == nullptr)
                return;
            const int pi = pluginIds.indexOf (p->chosenPluginId());
            pluginBox.setSelectedItemIndex (juce::jmax (0, pi), juce::dontSendNotification);

            kindBox.clear (juce::dontSendNotification);
            genreBox.clear (juce::dontSendNotification);
            engineBox.clear (juce::dontSendNotification);
            auto dash = [] (juce::ComboBox& box)
            {
                box.addItem ("-", 1000);
                box.setSelectedId (1000, juce::dontSendNotification);
                box.setEnabled (false);
            };
            const auto* s = rp::find (p->chosenPluginId());
            if (s == nullptr)
            {
                dash (kindBox); dash (genreBox); dash (engineBox);
                return;
            }
            const auto& l = rp::listsOf (*s);
            if (l.engines.isEmpty())
                dash (engineBox);
            else
            {
                for (int i = 0; i < l.engines.size(); ++i)
                    engineBox.addItem (l.engines[i], i + 1);
                engineBox.setSelectedId (juce::jlimit (0, l.engines.size() - 1, p->chosenEngineIndex()) + 1, juce::dontSendNotification);
                engineBox.setEnabled (true);
            }
            for (int i = 0; i < l.kinds.size(); ++i)
            {
                if (i < l.kindHeadings.size() && l.kindHeadings[i].isNotEmpty())
                    kindBox.addSectionHeading (l.kindHeadings[i]);
                kindBox.addItem (l.kinds[i], i + 1);
            }
            kindBox.setSelectedId (juce::jlimit (0, juce::jmax (0, l.kinds.size() - 1), p->chosenKindIndex()) + 1, juce::dontSendNotification);
            kindBox.setEnabled (l.kinds.size() > 0);

            if (l.genres.isEmpty())
                dash (genreBox);
            else
            {
                for (int i = 0; i < l.genres.size(); ++i)
                    genreBox.addItem (l.genres[i], i + 1);
                genreBox.setSelectedId (juce::jlimit (0, l.genres.size() - 1, p->chosenGenreIndex()) + 1, juce::dontSendNotification);
                genreBox.setEnabled (true);
            }
        }

        void refresh()
        {
            if (auto* p = mod())
            {
                shown = p->statusText().isNotEmpty() ? p->statusText() : juce::String ("Press Apply to roll a preset");
                const bool any = p->hasPlugin() || rp::find (p->chosenPluginId()) != nullptr;
                openButton.setEnabled (any);
                inPluginButton.setEnabled (any);
                saveButton.setEnabled (p->canSave());
                seen = p->getChangeCounter();
                repaint();
            }
        }

        void timerCallback() override
        {
            if (auto* p = mod(); p != nullptr && p->getChangeCounter() != seen)
            {
                // a patch loaded or an undo put other choices in: the lists follow
                if (pluginIds.indexOf (p->chosenPluginId()) != pluginBox.getSelectedItemIndex()
                    || kindBox.getSelectedId() - 1 != p->chosenKindIndex()
                    || (engineBox.getSelectedId() < 1000 && engineBox.getSelectedId() - 1 != p->chosenEngineIndex()))
                    fillLists();
                refresh();
            }
        }

        juce::WeakReference<SynthModule> module;
        juce::ComboBox pluginBox, engineBox, kindBox, genreBox;
        juce::TextButton applyButton { "Apply" }, openButton { "Plugin Window" }, saveButton { "Save Preset" }, inPluginButton { "Open in Plugin" };
        juce::Rectangle<int> pluginLabel, engineLabel, kindLabel, genreLabel, statusArea;
        juce::StringArray pluginIds;
        juce::String shown;
        int seen { -1 };

        std::shared_ptr<SynthModule> keep;                    // the plugin, while its editor is out
        std::unique_ptr<juce::AudioProcessorEditor> editor;
    };
}

std::unique_ptr<juce::Component> RandomPresetModule::createExtraContentComponent()
{
    return std::make_unique<RandomPresetPanel> (*this);
}

static ModuleDescriptor randomPresetDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.randompreset";
    d.displayName = "Random Plugin Preset";
    d.description =
        "One of the synth plugins, playing a preset its own generator rolled: pick the Plugin, its Engine where it has "
        "two, the Kind of preset (and for Massif a Genre), then Apply rolls one. Plugin Window shows it in the plugin's "
        "own window, Save Preset writes it as the plugin's own preset file, Open in Plugin puts the plugin itself in "
        "the module's place. Plays from the keys unless something is patched into MIDI In.";
    d.section = ModuleSection::Oscillator;
    d.sockets = {
        midiIn   ("midiIn",   "MIDI In"),
        audioOut ("audioOut", "Audio Out"),
        tempoIn()   // (6.9.10: tempo per lane)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RandomPresetModule, randomPresetDescriptor)
