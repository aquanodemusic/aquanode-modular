#include "ExternalPluginModule.h"

#if JUCE_PLUGINHOST_VST3

namespace aquanode::plugins
{

//==============================================================================
juce::AudioPluginFormatManager& ExternalPluginModule::formats()
{
    // VST3 only, and nothing is ever scanned: a plugin is loaded from the file the user points at
    static juce::AudioPluginFormatManager fm;
    if (fm.getNumFormats() == 0)
        fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    return fm;
}

//==============================================================================
// the plugin's own interface, in a window of its own
class ExternalPluginModule::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (std::shared_ptr<juce::AudioPluginInstance> p, std::function<void()> onClose)
        : juce::DocumentWindow (p->getName(), juce::Colour (0xff1d2228), juce::DocumentWindow::closeButton),
          plugin (std::move (p)), closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        if (auto* ed = plugin->createEditorIfNeeded())
            setContentOwned (ed, true);
        else
        {
            auto* generic = new juce::GenericAudioProcessorEditor (*plugin);
            generic->setSize (420, 360);
            setContentOwned (generic, true);
        }
        setResizable (getContentComponent() != nullptr && dynamic_cast<juce::GenericAudioProcessorEditor*> (getContentComponent()) != nullptr, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        toFront (true);
    }

    ~PluginWindow() override
    {
        // the editor goes before the plugin it belongs to
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        if (closed) juce::MessageManager::callAsync (closed);
    }

private:
    std::shared_ptr<juce::AudioPluginInstance> plugin;
    std::function<void()> closed;
};

//==============================================================================
ExternalPluginModule::ExternalPluginModule()
{
    midiInBuffer.ensureSize (2048);
}

ExternalPluginModule::~ExternalPluginModule()
{
    window = nullptr;
    std::shared_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        old = std::move (current);
    }
    if (old != nullptr)
    {
        old->setPlayHead (nullptr);
        old->releaseResources();
    }
}

juce::String ExternalPluginModule::pluginName() const
{
    return description.name.isNotEmpty() ? description.name : juce::String();
}

juce::String ExternalPluginModule::titleSuffix() const
{
    return current != nullptr ? " - " + description.name : juce::String();
}

void ExternalPluginModule::configure (juce::AudioPluginInstance& p)
{
    // stereo in (when it has an input) and stereo out, if the plugin takes that; else as it comes
    auto layout = p.getBusesLayout();
    if (layout.inputBuses.size() > 0)  layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (layout.outputBuses.size() > 0) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    for (int i = 1; i < layout.inputBuses.size(); ++i)  layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    if (p.checkBusesLayoutSupported (layout))
        p.setBusesLayout (layout);
    p.setPlayHead (&playHead);
    p.setRateAndBufferSizeDetails (sampleRate > 0.0 ? sampleRate : 48000.0, kBlockSize);
    p.prepareToPlay (sampleRate > 0.0 ? sampleRate : 48000.0, kBlockSize);
}

void ExternalPluginModule::install (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::PluginDescription& desc)
{
    window = nullptr;
    std::shared_ptr<juce::AudioPluginInstance> fresh (std::move (instance));
    configure (*fresh);
    const int newIns = fresh->getMainBusNumInputChannels(), newOuts = fresh->getMainBusNumOutputChannels();
    std::shared_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        old = std::move (current);
        current = fresh;
        ins = juce::jmin (2, newIns);
        outs = juce::jmin (2, juce::jmax (newOuts, 0));
        io.setSize (juce::jmax (2, fresh->getTotalNumInputChannels(), fresh->getTotalNumOutputChannels()), kBlockSize, false, true, false);
        io.clear();
        midiInBuffer.clear();
    }
    if (old != nullptr)
    {
        old->setPlayHead (nullptr);
        old->releaseResources();
    }
    description = desc;
    missingState = {};
    status = desc.name + (desc.manufacturerName.isNotEmpty() ? "  (" + desc.manufacturerName + ")" : juce::String())
           + (desc.isInstrument ? "  - instrument" : "");
    ++changeCounter;
}

bool ExternalPluginModule::instantiate (const juce::PluginDescription& desc, juce::String& error)
{
    auto instance = formats().createPluginInstance (desc, sampleRate > 0.0 ? sampleRate : 48000.0, kBlockSize, error);
    if (instance == nullptr)
        return false;
    install (std::move (instance), desc);
    return true;
}

bool ExternalPluginModule::loadFile (const juce::File& chosen, juce::String& error)
{
    // a file inside a bundle (…/Foo.vst3/Contents/x86_64-win/Foo.vst3): the bundle itself
    juce::File file = chosen;
    for (auto f = chosen.getParentDirectory(); f.exists() && f != f.getParentDirectory(); f = f.getParentDirectory())
        if (f.hasFileExtension (".vst3"))
            file = f;
    if (! file.hasFileExtension (".vst3"))
    {
        error = file.getFileName() + " is not a .vst3 plugin";
        return false;
    }

    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats().getFormats())
        format->findAllTypesForFile (found, file.getFullPathName());
    if (found.isEmpty())
    {
        error = "no plugin found in " + file.getFileName();
        return false;
    }
    return instantiate (*found[0], error);
}

void ExternalPluginModule::chooseAndLoad()
{
    // the user points at the plugin: a <name>.vst3 folder (a bundle) or file - nothing is scanned.
    // Windows' own dialog picks either folders or files, never both: it picks files there, so a
    // single-file plugin is chosen directly and a bundle by the .vst3 file inside it (Contents\
    // x86_64-win\<name>.vst3), which loadFile takes back up to the bundle
    auto start = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("VST3");
   #if JUCE_LINUX
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3");
    start = home.isDirectory() ? home : juce::File ("/usr/lib/vst3");
   #endif
    if (description.fileOrIdentifier.isNotEmpty() && juce::File::isAbsolutePath (description.fileOrIdentifier))
        start = juce::File (description.fileOrIdentifier).getParentDirectory();

    auto chooser = std::make_shared<juce::FileChooser> ("Open a VST3 plugin (.vst3)", start, "*.vst3");
    juce::WeakReference<SynthModule> weak (this);
   #if JUCE_WINDOWS
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
   #else
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::canSelectDirectories;
   #endif
    chooser->launchAsync (flags,
                          [weak, chooser] (const juce::FileChooser& fc)
                          {
                              auto* self = dynamic_cast<ExternalPluginModule*> (weak.get());
                              const auto file = fc.getResult();
                              if (self == nullptr || file == juce::File())
                                  return;
                              juce::String error;
                              if (! self->loadFile (file, error))
                              {
                                  self->status = "Could not load: " + error;
                                  ++self->changeCounter;
                              }
                          });
}

void ExternalPluginModule::unload()
{
    window = nullptr;
    std::shared_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        old = std::move (current);
    }
    if (old != nullptr)
    {
        old->setPlayHead (nullptr);
        old->releaseResources();
    }
    description = {};
    missingState = {};
    status = "No plugin loaded";
    ++changeCounter;
}

void ExternalPluginModule::showWindow (bool show)
{
    if (! show || current == nullptr)
    {
        window = nullptr;
        ++changeCounter;
        return;
    }
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }
    juce::WeakReference<SynthModule> weak (this);
    window = std::make_unique<PluginWindow> (current, [weak]
    {
        if (auto* self = dynamic_cast<ExternalPluginModule*> (weak.get()))
            self->showWindow (false);
    });
    ++changeCounter;
}

//==============================================================================
void ExternalPluginModule::prepare (double newSampleRate)
{
    SynthModule::prepare (newSampleRate);
    std::shared_ptr<juce::AudioPluginInstance> p;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        p = current;
    }
    if (p != nullptr)
    {
        const juce::ScopedLock cl (p->getCallbackLock());
        p->releaseResources();
        p->setRateAndBufferSizeDetails (newSampleRate, kBlockSize);
        p->prepareToPlay (newSampleRate, kBlockSize);
    }
    for (auto& ch : outFifo) ch.fill (0.0f);
    if (io.getNumChannels() == 0)
        io.setSize (2, kBlockSize);
    io.clear();
    pos = 0;
    prepared = true;
}

void ExternalPluginModule::reset()
{
    for (auto& ch : outFifo) ch.fill (0.0f);
    io.clear();
    midiInBuffer.clear();
    for (int ch = 1; ch <= 16; ++ch)
        midiInBuffer.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
}

void ExternalPluginModule::blockStart() { samplesThisHostBlock = 0; }

void ExternalPluginModule::rawMidiMessage (const juce::MidiMessage& message)
{
    midiInBuffer.addEvent (message, pos);
}

void ExternalPluginModule::midiAllNotesOff()
{
    for (int ch = 1; ch <= 16; ++ch)
        midiInBuffer.addEvent (juce::MidiMessage::allNotesOff (ch), pos);
}

void ExternalPluginModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // sockets: Audio In, Midi In, Audio Out
    if (prepared && io.getNumChannels() >= 2)
    {
        io.setSample (0, pos, inputs[0][0]);
        io.setSample (1, pos, inputs[0][1]);
    }
    outputs[0] = { outFifo[0][(size_t) pos], outFifo[1][(size_t) pos] };
    ++samplesThisHostBlock;
    ++sampleCounter;
    if (++pos >= kBlockSize)
    {
        pos = 0;
        runBlock();
    }
}

void ExternalPluginModule::updatePlayHead()
{
    juce::AudioPlayHead::PositionInfo info;
    const int offset = samplesThisHostBlock - kBlockSize;
    bool fromHost = false;
    if (hostPlayHead != nullptr)
        if (auto host = hostPlayHead->getPosition())
        {
            info = *host;
            fromHost = true;
            if (auto t = info.getTimeInSamples()) info.setTimeInSamples (*t + offset);
            if (auto t = info.getTimeInSeconds()) info.setTimeInSeconds (*t + offset / sampleRate);
            if (auto ppq = info.getPpqPosition())
                if (auto bpm = info.getBpm())
                    info.setPpqPosition (*ppq + offset / sampleRate * (*bpm / 60.0));
        }
    if (! fromHost)
    {
        const double secs = (double) (sampleCounter - kBlockSize) / sampleRate;
        info.setBpm (tempoBpm);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setTimeInSamples (sampleCounter - kBlockSize);
        info.setTimeInSeconds (secs);
        info.setPpqPosition (secs * tempoBpm / 60.0);
        info.setIsPlaying (hostTransport.isPlaying);
    }
    playHead.info = info;
}

void ExternalPluginModule::runBlock()
{
    bool ran = false;
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked() && current != nullptr && prepared)
        {
            auto& p = *current;
            updatePlayHead();
            // the plugin's inputs: the stereo pair; anything beyond them silent
            for (int ch = ins; ch < io.getNumChannels(); ++ch)
                io.clear (ch, 0, kBlockSize);
            if (ins == 1)
                for (int i = 0; i < kBlockSize; ++i)
                    io.setSample (0, i, 0.5f * (io.getSample (0, i) + io.getSample (1, i)));
            const juce::ScopedTryLock cl (p.getCallbackLock());
            if (cl.isLocked() && ! p.isSuspended())
            {
                juce::AudioBuffer<float> view (io.getArrayOfWritePointers(),
                                               juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(), 1), kBlockSize);
                p.processBlock (view, midiInBuffer);
                ran = true;
            }
        }
    }
    midiInBuffer.clear();
    for (int i = 0; i < kBlockSize; ++i)
    {
        float l = ran && outs > 0 ? io.getSample (0, i) : 0.0f;
        float r = ran && outs > 1 ? io.getSample (1, i) : l;
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        outFifo[0][(size_t) i] = l;
        outFifo[1][(size_t) i] = r;
    }
    io.clear();
}

//==============================================================================
juce::String ExternalPluginModule::saveCustomState() const
{
    if (description.fileOrIdentifier.isEmpty())
        return {};
    juce::XmlElement root ("ExternalPlugin");
    root.setAttribute ("version", 1);
    if (auto d = description.createXml())
        root.addChildElement (d.release());
    juce::String state = missingState;
    if (current != nullptr)
    {
        juce::MemoryBlock mb;
        current->getStateInformation (mb);
        state = mb.toBase64Encoding();
    }
    root.createNewChildElement ("State")->addTextElement (state);
    return root.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

void ExternalPluginModule::loadCustomState (const juce::String& text)
{
    auto xml = juce::parseXML (text);
    if (xml == nullptr || ! xml->hasTagName ("ExternalPlugin"))
        return;
    juce::PluginDescription desc;
    if (auto* d = xml->getChildByName ("PLUGIN"); d == nullptr || ! desc.loadFromXml (*d))
        return;
    const auto state = xml->getChildElementAllSubText ("State", {});

    juce::String error;
    bool ok = instantiate (desc, error);
    if (! ok)
    {
        // moved since the patch was saved? the same file name in the usual places
        const auto name = juce::File (desc.fileOrIdentifier).getFileName();
        juce::Array<juce::File> places { juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("VST3"),
                                         juce::File ("~/.vst3"), juce::File ("/usr/lib/vst3"), juce::File ("/usr/local/lib/vst3") };
        for (const auto& dir : places)
            if (dir.getChildFile (name).exists())
            {
                juce::String e2;
                if (loadFile (dir.getChildFile (name), e2)) { ok = true; break; }
            }
    }
    if (! ok)
    {
        unload();
        description = desc;          // kept, so a save writes the patch back unchanged
        missingState = state;
        status = "Not on this computer: " + desc.name + " (" + desc.fileOrIdentifier + ")";
        ++changeCounter;
        return;
    }
    juce::MemoryBlock mb;
    if (state.isNotEmpty() && mb.fromBase64Encoding (state) && mb.getSize() > 0)
        current->setStateInformation (mb.getData(), (int) mb.getSize());
}

//==============================================================================
// the card: Load..., Window, Unload and what is loaded
namespace
{
    class ExternalPluginPanel final : public juce::Component, private juce::Timer
    {
    public:
        explicit ExternalPluginPanel (ExternalPluginModule& m) : module (&m)
        {
            for (auto* b : { &loadButton, &windowButton, &unloadButton })
            {
                addAndMakeVisible (*b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3038));
                b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1a8fa6));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            }
            loadButton.setTooltip ("Open a VST3 plugin: point at its .vst3 file (for a .vst3 folder, the .vst3 file inside it)");
            loadButton.onClick = [this] { if (auto* p = mod()) p->chooseAndLoad(); };
            windowButton.onClick = [this] { if (auto* p = mod()) p->showWindow (! p->isWindowShowing()); };
            unloadButton.onClick = [this] { if (auto* p = mod()) p->unload(); };
            refresh();
            startTimerHz (5);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 3);
            auto top = r.removeFromTop (24);
            loadButton.setBounds (top.removeFromLeft (76));
            top.removeFromLeft (4);
            windowButton.setBounds (top.removeFromLeft (76));
            top.removeFromLeft (4);
            unloadButton.setBounds (top.removeFromLeft (70));
            statusArea = r.withTrimmedTop (2);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff141414));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText (shown, statusArea, juce::Justification::centredLeft, 1, 0.8f);
        }

    private:
        ExternalPluginModule* mod() const { return dynamic_cast<ExternalPluginModule*> (module.get()); }

        void refresh()
        {
            if (auto* p = mod())
            {
                shown = p->statusText();
                windowButton.setEnabled (p->hasPlugin());
                unloadButton.setEnabled (p->hasPlugin() || p->pluginName().isNotEmpty());
                windowButton.setToggleState (p->isWindowShowing(), juce::dontSendNotification);
                windowButton.setButtonText (p->isWindowShowing() ? "Close" : "Window");
                seen = p->getChangeCounter();
                repaint();
            }
        }

        void timerCallback() override
        {
            if (auto* p = mod())
                if (p->getChangeCounter() != seen)
                {
                    refresh();
                    // the title shows the plugin's name: the card re-measures itself
                    if (auto* parent = getParentComponent()) parent->repaint();
                }
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton loadButton { "Load..." }, windowButton { "Window" }, unloadButton { "Unload" };
        juce::Rectangle<int> statusArea;
        juce::String shown;
        int seen { -1 };
    };
}

std::unique_ptr<juce::Component> ExternalPluginModule::createExtraContentComponent()
{
    return std::make_unique<ExternalPluginPanel> (*this);
}

} // namespace aquanode::plugins

static aquanode::ModuleDescriptor externalPluginDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.externalplugin";
    d.displayName = "VST3 Plugin";
    d.description =
        "Any VST3 plugin of yours in the patch: Load... and point at its .vst3 folder or file (nothing is "
        "scanned). Audio In goes into an effect, Midi In (the keys while nothing is patched there) into a synth; "
        "Window opens the plugin's own interface. The patch keeps which plugin it was and its whole state. "
        "Desktop only - not on Android.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 60;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        midiIn   ("midiIn",   "Midi In"),
        audioOut ("audioOut", "Audio Out")
    };
    return d;
}

using ExternalPluginModuleReg = aquanode::plugins::ExternalPluginModule;
AQUANODE_REGISTER_MODULE (ExternalPluginModuleReg, externalPluginDescriptor)

#endif
