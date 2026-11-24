#include <map>
#include "HostedPlugin.h"
#include "PluginCatalog.h"

namespace aquanode::plugins
{

//==============================================================================
ModuleDescriptor makeHostedDescriptor (const HostedSpec& spec)
{
    ModuleDescriptor d;
    d.typeId = spec.typeId;
    d.displayName = spec.displayName;
    d.description = spec.description;
    d.section = spec.section;
    d.accent = spec.accent;
    d.listedInSidebar = false;   // offered by the Plugins section instead

    if (spec.audioIn)  d.sockets.push_back (audioIn ("audioIn", "Audio In"));
    if (spec.sideIn)   d.sockets.push_back (audioIn ("sideIn", spec.sideInLabel));
    if (spec.midiIn)   d.sockets.push_back (midiIn ("midiIn", "MIDI In"));
    d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
    if (spec.midiOut)  d.sockets.push_back (midiOut ("midiOut", "MIDI Out"));
    d.sockets.push_back (tempoIn());   // (6.9.10: tempo per lane - its play head's BPM)
    // the block the plugin runs in: Global (the top bar's Plugin Block) or its own
    d.params.push_back (makeCombo ("blockSize", "Block", HostedPluginModule::blockChoices(), 0, 0, 2).noMod().hide().whenMissing (0.0f));   // (the card's Block pill)

    // 6.7.0: the synths far from the others' level (measured: a C4 at velocity
    // 100, the loudest 100 ms against about -16 dBFS) meet them on new cards;
    // a saved patch keeps the level it had (the engine applies the trim on
    // the card's cables, like every module's Output Trim)
    static const std::map<juce::String, float> levelTrims {
        { "tabla", 12.0f }, { "am20", 10.0f }, { "aquamassif", 7.0f }, { "reflex", -6.0f } };
    if (const auto it = levelTrims.find (spec.pluginId); it != levelTrims.end())
        d.params.push_back (makeOutputTrim (it->second));

    // (6.9.10) Vita's card: MPE on with Aquanode's MPE (as before) or as the
    // preset says, and the per-finger bend range 48 or the preset's - Preset
    // and Preset keep a mono, range-2 preset like Abyssal Pulse as it was made
    if (spec.pluginId == "vita")
    {
        d.params.push_back (makeCombo ("mpeMode", "MPE", { "Follow Aquanode", "Preset" }, 0, 0, 3).noMod());
        d.params.push_back (makeCombo ("bendRange", "Bend Range", { "48", "Preset" }, 0, 0, 2).noMod());
    }
    return d;
}

const juce::StringArray& HostedPluginModule::blockChoices()
{
    // (6.9.10: 1, 2, 4 and 8 added - patches keep their choice by name)
    static const juce::StringArray c { "Global", "1", "2", "4", "8", "16", "32", "64", "128", "256", "512" };
    return c;
}

std::atomic<int>& HostedPluginModule::globalBlockSize()
{
    static std::atomic<int> size { 128 };
    return size;
}

int HostedPluginModule::wantedBlock() const
{
    const int c = pBlock >= 0 ? (int) std::lround (param (pBlock)) : 0;
    static const int sizes[] = { 0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };   // (as blockChoices)
    if (c >= 1 && c <= 10) return juce::jmin (kMaxBlockSize, sizes[c]);
    return juce::jlimit (1, kMaxBlockSize, globalBlockSize().load (std::memory_order_relaxed));
}

//==============================================================================
// The card's content: the plugin's own editor at its own size. It keeps the
// processor alive for as long as the editor exists, so a module deleted while
// its card is still on screen can never leave the editor dangling.
class HostedEditorHolder : public juce::Component,
                           public ExtraContentParamListener,
                           public PluginEditorLender,
                           private juce::ComponentListener
{
public:
    HostedEditorHolder (std::shared_ptr<juce::AudioProcessor> p, HostedPluginModule& owner)
        : processor (std::move (p)), module (&owner)
    {
        setOpaque (false);
       #if JUCE_MAJOR_VERSION >= 9
        editor.reset (processor->createEditorAndMakeActive());
       #else
        editor.reset (processor->createEditorIfNeeded());   // the same, before JUCE 9
       #endif
        if (editor != nullptr)
        {
            addAndMakeVisible (editor.get());
            editor->setTopLeftPosition (0, 0);
            editor->addComponentListener (this);
            owner.noteEditorSize (editor->getWidth(), editor->getHeight());
            applyCardScale();
        }
        else
        {
            setSize (300, 60);
        }
    }

    ~HostedEditorHolder() override
    {
        if (editor != nullptr)
        {
            if (auto* parent = editor->getParentComponent())
                parent->removeChildComponent (editor.get());   // lent to the full view
            editor->removeComponentListener (this);
            // the host's job (JUCE's editor does not do it itself): without it
            // the plugin still counts this editor as its active one, and the
            // card that replaces this one (every canvas rebuild) gets none
            if (processor != nullptr)
                processor->editorBeingDeleted (editor.get());
        }
        editor = nullptr;     // the editor must go before the processor
        processor = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        if (editor == nullptr || lent)
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText (editor == nullptr ? "This plugin has no window" : "Shown in full size",
                        getLocalBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        if (editor != nullptr && ! lent && editor->getPosition() != juce::Point<int>())
            editor->setTopLeftPosition (0, 0);
    }

    juce::Component* lendEditor() override
    {
        if (editor == nullptr || lent)
            return nullptr;
        lent = true;
        removeChildComponent (editor.get());
        editor->setTransform ({});   // (the full view shows it 1:1, scaled its own way)
        repaint();
        return editor.get();
    }

    void returnEditor() override
    {
        if (editor == nullptr || ! lent)
            return;
        lent = false;
        addAndMakeVisible (editor.get());
        editor->setTopLeftPosition (0, 0);
        applyCardScale();
        repaint();
    }

private:
    // the editor at its card scale (HostedPluginModule::cardScaleFor), this
    // holder the size it then covers
    void applyCardScale()
    {
        if (editor == nullptr)
            return;
        const float s = HostedPluginModule::cardScaleFor (editor->getWidth(), editor->getHeight());
        if (! lent)
            editor->setTransform (juce::AffineTransform::scale (s));
        setSize (juce::jmax (1, juce::roundToInt ((float) editor->getWidth() * s)),
                 juce::jmax (1, juce::roundToInt ((float) editor->getHeight() * s)));
    }

    void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
    {
        if (! wasResized || &c != editor.get())
            return;

        if (auto* m = dynamic_cast<HostedPluginModule*> (module.get()))
            m->noteEditorSize (c.getWidth(), c.getHeight());
        if (! lent)
            applyCardScale();

        // the card re-measures itself - after this callback, never inside it
        juce::Component::SafePointer<HostedEditorHolder> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr && safe->paramsChangedByContent)
                safe->paramsChangedByContent();
        });
    }

    std::shared_ptr<juce::AudioProcessor> processor;
    juce::WeakReference<SynthModule> module;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool lent { false };
};

//==============================================================================
HostedPluginModule::HostedPluginModule (const HostedSpec& s)
    : spec (s),
      editorW (s.defaultWidth),
      editorH (s.defaultHeight)
{
    plugin.reset (spec.create());
    jassert (plugin != nullptr);

    if (spec.sideIn)
        if (auto* bus = plugin->getBus (true, 1))
        {
            bus->enable (true);
            sideChannels = plugin->getChannelCountOfBus (true, 1);
            bus->enable (false);
        }

    plugin->setPlayHead (&playHead);
    pendingOut.reserve (256);
}

HostedPluginModule::~HostedPluginModule()
{
    if (plugin != nullptr)
    {
        plugin->setPlayHead (nullptr);
        if (prepared)
            plugin->releaseResources();
    }
}

void HostedPluginModule::prepare (double newSampleRate)
{
    SynthModule::prepare (newSampleRate);

    const auto& d = getDescriptor();
    inAudioIdx = d.inputIndexOf ("audioIn");
    inSideIdx = d.inputIndexOf ("sideIn");
    outAudioIdx = d.outputIndexOf ("audioOut");
    numOutputs = juce::jmax (1, d.numOutputs());

    mainIns = plugin->getBus (true, 0) != nullptr ? plugin->getChannelCountOfBus (true, 0) : 0;
    sideIns = spec.sideIn && plugin->getBus (true, 1) != nullptr ? plugin->getChannelCountOfBus (true, 1) : 0;
    outs = plugin->getTotalNumOutputChannels();

    const int channels = juce::jmax (1, mainIns + sideChannels, outs);
    pBlock = paramIndex ("blockSize");
    pMpeMode = paramIndex ("mpeMode");     // (Vita's card only)
    pBendRange = paramIndex ("bendRange");
    lastMpePolicy = -1;
    blockSize = wantedBlock();
    io.setSize (channels, kMaxBlockSize, false, true, false);
    io.clear();

    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        if (prepared)
            plugin->releaseResources();
        plugin->setRateAndBufferSizeDetails (newSampleRate, kMaxBlockSize);
        plugin->prepareToPlay (newSampleRate, kMaxBlockSize);
    }

    midiInBuffer.ensureSize (2048);
    midiOutScratch.ensureSize (2048);
    for (auto& ch : outFifo)
        ch.fill (0.0f);
    pos = 0;
    prepared = true;
}

void HostedPluginModule::reset()
{
    // the engine wipes all state after a NaN or a deleted module: the plugin
    // is told by an all-notes-off, its own state stays as the player set it
    for (auto& ch : outFifo)
        ch.fill (0.0f);
    io.clear();
    midiInBuffer.clear();
    for (int ch = 1; ch <= 16; ++ch)
        midiInBuffer.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    pendingOut.clear();
}

void HostedPluginModule::blockStart()
{
    samplesThisHostBlock = 0;

    // (6.9.12) the default MPE map's Slide depth, to a plugin that plays MPE
    if (midiControls != nullptr)
        if (auto* r = dynamic_cast<aquanode::MpeReceiver*> (plugin.get()))
            r->setSlideWavetableDepth (midiControls->slideWavetableDepth);

    // (6.9.10) Vita's card: its MPE and bend-range choices, when they move
    if (pMpeMode >= 0)
    {
        const int policy = (param (pMpeMode) > 0.5f ? 1 : 0) | (pBendRange >= 0 && param (pBendRange) > 0.5f ? 2 : 0);
        if (policy != lastMpePolicy)
            if (auto* r = dynamic_cast<aquanode::MpeReceiver*> (plugin.get()))
            {
                r->setMpePolicy ((policy & 1) != 0, (policy & 2) != 0);
                lastMpePolicy = policy;
            }
    }

    if (! rollCommandSeen || hostTransport.rollCommand != seenRollCommand)
    {
        if (rollCommandSeen)
            if (auto* sp = dynamic_cast<SongPlayer*> (plugin.get()))
                sp->playSongFromStart (hostTransport.rollCommandPlays);
        seenRollCommand = hostTransport.rollCommand;
        rollCommandSeen = true;
    }

    if (spec.sideIn && prepared && inSideIdx >= 0)
    {
        const bool want = isInputConnected (inSideIdx);
        if (want != (sideIns.load() > 0) && ! sideRelayoutPending.exchange (true))
        {
            juce::WeakReference<SynthModule> weak (this);
            juce::MessageManager::callAsync ([weak, want]
            {
                if (auto* self = dynamic_cast<HostedPluginModule*> (weak.get()))
                    self->setSideChainEnabled (want);
            });
        }
    }
}

void HostedPluginModule::setSideChainEnabled (bool enabled)
{
    // message thread: the audio thread only try-locks, and plays silence
    // for the few samples this takes
    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        if (auto* bus = plugin->getBus (true, 1))
        {
            plugin->releaseResources();
            bus->enable (enabled);
            sideIns = enabled ? juce::jmin (sideChannels, plugin->getChannelCountOfBus (true, 1)) : 0;
            plugin->prepareToPlay (sampleRate, kMaxBlockSize);
        }
    }
    sideRelayoutPending = false;
}

int HostedPluginModule::latencySamples() const
{
    return blockSize + (plugin != nullptr ? juce::jmax (0, plugin->getLatencySamples()) : 0);
}

void HostedPluginModule::rawMidiMessage (const juce::MidiMessage& message)
{
    // a note on a member channel - or its Slide, Pressure or Bend, which an MPE controller sends
    // just before the note: a plugin that plays MPE switches it on (6.9.13: else the first
    // note's Slide went missing)
    if (! mpeOn && message.getChannel() >= 2
        && (message.isNoteOn() || message.isPitchWheel() || message.isChannelPressure() || message.isControllerOfType (74)))
        if (auto* r = dynamic_cast<aquanode::MpeReceiver*> (plugin.get()))
        {
            r->enableMpe (true);
            mpeOn = true;
            // (6.9.15) a bend it heard before (as one channel) stays on its pitch wheel otherwise:
            // every channel's wheel back to the middle first
            for (int ch = 1; ch <= 16; ++ch)
                midiInBuffer.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), pos);
        }

    // (6.9.15) a member channel's next note starts unbent unless its own bend comes first
    // (a released note's last bend stayed on the channel, and the next note there - from
    // MIDI Add, or another finger - began detuned)
    if (const int ch = message.getChannel(); ch >= 2 && ch <= 16)
    {
        auto& st = memberState[(size_t) ch];
        if (message.isPitchWheel()) st.resetDue = false;
        else if (message.isNoteOff()) { if (st.held > 0) --st.held; if (st.held == 0) st.resetDue = true; }
        else if (message.isNoteOn())
        {
            if (st.resetDue && st.held == 0) midiInBuffer.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), pos);
            st.resetDue = false;
            ++st.held;
        }
    }

    midiInBuffer.addEvent (message, pos);
}

void HostedPluginModule::midiAllNotesOff()
{
    for (int ch = 1; ch <= 16; ++ch)
        midiInBuffer.addEvent (juce::MidiMessage::allNotesOff (ch), pos);
}

void HostedPluginModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (prepared)
    {
        if (mainIns > 0)
        {
            const int i = inAudioIdx;
            const StereoFrame f = i >= 0 ? inputs[i] : StereoFrame { 0.0f, 0.0f };
            if (mainIns == 1)
                io.setSample (0, pos, 0.5f * (f[0] + f[1]));
            else
            {
                io.setSample (0, pos, f[0]);
                io.setSample (1, pos, f[1]);
            }
        }

        if (const int sideIns = this->sideIns.load (std::memory_order_relaxed); sideIns > 0)
        {
            const int i = inSideIdx;
            const StereoFrame f = i >= 0 ? inputs[i] : StereoFrame { 0.0f, 0.0f };
            io.setSample (mainIns, pos, f[0]);
            if (sideIns > 1)
                io.setSample (mainIns + 1, pos, f[1]);
        }
    }

    for (int k = 0; k < numOutputs; ++k)
        outputs[k] = { 0.0f, 0.0f };
    if (outAudioIdx >= 0)
        outputs[outAudioIdx] = { outFifo[0][(size_t) pos], outFifo[1][(size_t) pos] };

    ++samplesThisHostBlock;
    ++sampleCounter;

    if (++pos >= blockSize)
    {
        pos = 0;
        runBlock();
        // the Block menu (or the top bar's) changed: the next block runs at the new size
        if (const int want = wantedBlock(); want != blockSize)
        {
            for (auto& ch : outFifo) std::fill (ch.begin() + juce::jmin (blockSize, want), ch.end(), 0.0f);
            blockSize = want;
        }
    }
}

void HostedPluginModule::updatePlayHead()
{
    juce::AudioPlayHead::PositionInfo info;

    // where this little block started, relative to the host's block
    const int offset = samplesThisHostBlock - blockSize;
    bool fromHost = false;

    // (a play head without a tempo - the standalone app's and Android's audio player
    // has one that only counts samples - does not count: the plugin gets the patch's
    // own tempo and clock then, as everything else in the patch does)
    if (hostPlayHead != nullptr && ! tempoFromLane())   // (6.9.14: a fed Tempo In wins over the host's tempo)
        if (auto host = hostPlayHead->getPosition(); host && host->getBpm() && *host->getBpm() > 0.0)
        {
            info = *host;
            fromHost = true;
            if (auto t = info.getTimeInSamples())
                info.setTimeInSamples (*t + offset);
            if (auto t = info.getTimeInSeconds())
                info.setTimeInSeconds (*t + offset / sampleRate);
            if (auto ppq = info.getPpqPosition())
                if (auto bpm = info.getBpm())
                    info.setPpqPosition (*ppq + offset / sampleRate * (*bpm / 60.0));
        }

    if (! fromHost)
    {
        // standalone / Android (or a host without a tempo): the patch's own tempo, a free-running clock
        const double secs = (double) (sampleCounter - blockSize) / sampleRate;
        info.setBpm (tempoBpm);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setTimeInSamples (sampleCounter - blockSize);
        info.setTimeInSeconds (secs);
        info.setPpqPosition (secs * tempoBpm / 60.0);
        info.setIsPlaying (hostTransport.isPlaying);
    }

    playHead.info = info;
}

void HostedPluginModule::runBlock()
{
    if (! prepared)
        return;

    updatePlayHead();

    // anything beyond the plugin's own channels must not leak into its output
    const int usedIns = mainIns + sideIns.load (std::memory_order_relaxed);
    for (int ch = usedIns; ch < io.getNumChannels(); ++ch)
        io.clear (ch, 0, blockSize);

    {
        const juce::ScopedTryLock sl (plugin->getCallbackLock());
        if (sl.isLocked() && ! plugin->isSuspended())
        {
            // this block's samples of the buffer (it is sized for the largest block)
            juce::AudioBuffer<float> block (io.getArrayOfWritePointers(), io.getNumChannels(), blockSize);
            if (aquanode::cpuMeterActive())   // (CPU / RAM module: its exact time)
            {
                const auto t0 = std::chrono::steady_clock::now();
                plugin->processBlock (block, midiInBuffer);
                cpuNs += (double) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
            }
            else
                plugin->processBlock (block, midiInBuffer);
        }
        else
            io.clear();
    }

    // MIDI the plugin produced (NoteAdder): emitted one block later, at the
    // sample it belongs to. The due times count on the driver's own clock -
    // the engine ticks it only while Midi Out leads somewhere that sounds, so
    // a card left unpatched for a while must not make its notes that late
    // once it is patched (the sample clock runs on regardless). While the
    // driver is not being ticked, nothing it would play is heard: dropped.
    if (spec.midiOut)
    {
        if (driverCounter == driverCounterAtLastBlock)
            pendingOut.clear();
        driverCounterAtLastBlock = driverCounter;
        for (const auto meta : midiInBuffer)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff())
                continue;
            MidiDriverEvent ev;
            ev.note = m.getNoteNumber();
            ev.isOn = m.isNoteOn();
            ev.velocity = m.getFloatVelocity();
            if (pendingOut.size() < pendingOut.capacity())
                pendingOut.push_back ({ driverCounter + meta.samplePosition, ev });
        }
    }
    midiInBuffer.clear();

    // the processed block becomes the next block's output
    const int numOut = juce::jmin (outs, io.getNumChannels());
    for (int i = 0; i < blockSize; ++i)
    {
        float l = numOut > 0 ? io.getSample (0, i) : 0.0f;
        float r = numOut > 1 ? io.getSample (1, i) : l;
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        outFifo[0][(size_t) i] = l;
        outFifo[1][(size_t) i] = r;
    }
    io.clear();
}

int HostedPluginModule::midiDriverAdvanceEvents (double, MidiDriverEvent* events, int maxEvents)
{
    // called once per sample, before the patch runs that sample
    const auto now = driverCounter++;
    int n = 0;
    for (size_t i = 0; i < pendingOut.size();)
    {
        if (pendingOut[i].due <= now && n < maxEvents)
        {
            events[n++] = pendingOut[i].ev;
            pendingOut.erase (pendingOut.begin() + (long) i);
            continue;
        }
        ++i;
    }
    return n;
}

//==============================================================================
std::unique_ptr<juce::Component> HostedPluginModule::createExtraContentComponent()
{
    if (! plugin->hasEditor())
        return nullptr;
    return std::make_unique<HostedEditorHolder> (plugin, *this);
}

void HostedPluginModule::noteEditorSize (int w, int h)
{
    editorW = juce::jmax (100, w);
    editorH = juce::jmax (40, h);
}

int HostedPluginModule::preferredModuleWidth() const
{
    // the card's 10 px margins either side
    return juce::roundToInt ((float) editorW.load() * cardScale()) + 20;
}

int HostedPluginModule::extraContentHeight() const
{
    return plugin->hasEditor() ? juce::roundToInt ((float) editorH.load() * cardScale()) : 0;
}

juce::String HostedPluginModule::saveCustomState() const
{
    juce::MemoryBlock mb;
    plugin->getStateInformation (mb);
    const auto head = "w=" + juce::String (editorW.load()) + ";h=" + juce::String (editorH.load()) + ";";
    const juce::ScopedLock sl (stateCacheLock);
    if (mb == cachedStateBytes && cachedState.startsWith (head))
        return cachedState;
    cachedStateBytes = mb;
    cachedState = head + mb.toBase64Encoding();
    return cachedState;
}

void HostedPluginModule::loadCustomState (const juce::String& state)
{
    auto rest = state;
    while (rest.startsWith ("w=") || rest.startsWith ("h="))
    {
        const auto item = rest.upToFirstOccurrenceOf (";", false, false);
        (item.startsWith ("w=") ? editorW : editorH) = juce::jmax (40, item.substring (2).getIntValue());
        rest = rest.fromFirstOccurrenceOf (";", false, false);
    }

    juce::MemoryBlock mb;
    if (rest.isNotEmpty() && mb.fromBase64Encoding (rest) && mb.getSize() > 0)
        plugin->setStateInformation (mb.getData(), (int) mb.getSize());
}

//==============================================================================
bool HostedPluginModule::hasAudioToSave() const
{
    double rate = 0.0;
    return getAudioToSave (rate) != nullptr;
}

std::shared_ptr<const juce::AudioBuffer<float>> HostedPluginModule::getAudioToSave (double& rate) const
{
    if (auto* access = audioAccessFor (spec.pluginId); access != nullptr && access->get)
        return access->get (*plugin, rate);
    return nullptr;
}

void HostedPluginModule::restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
{
    if (auto* access = audioAccessFor (spec.pluginId); access != nullptr && access->set && audio != nullptr)
        access->set (*plugin, std::move (audio), rate);
}

} // namespace aquanode::plugins
