#include "PatchKit.h"

namespace aquanode::plugins::kit
{

juce::RangedAudioParameter* findParam (juce::AudioProcessor& original, const juce::String& id)
{
    for (auto* p : original.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() == id)
                return ranged;
    return nullptr;
}

float param (juce::AudioProcessor& original, const juce::String& id, float fallback)
{
    auto* p = findParam (original, id);
    if (p == nullptr)
        return fallback;

    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
        return (float) c->getIndex();
    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p))
        return b->get() ? 1.0f : 0.0f;
    if (auto* i = dynamic_cast<juce::AudioParameterInt*> (p))
        return (float) i->get();
    return p->convertFrom0to1 (p->getValue());
}

juce::Colour accentOf (const juce::String& pluginId)
{
    return baseSpec (pluginId).accent;
}

juce::String moduleName (const juce::String& pluginId, const juce::String& kind)
{
    return baseSpec (pluginId).displayName + " - " + kind;
}

ParamSpec makeOnOff (juce::String id, juce::String label, bool defaultOn, int row)
{
    return makeCombo (std::move (id), std::move (label), { "Off", "On" }, defaultOn ? 1 : 0, row, 1);
}

//==============================================================================
int Builder::add (const juce::String& typeId, int dx, int dy)
{
    return patch.addModule (typeId, origin + juce::Point<int> (dx, dy));
}

SynthModule* Builder::dsp (int module) const
{
    auto* inst = patch.getInstance (module);
    return inst != nullptr ? inst->dsp.get() : nullptr;
}

void Builder::set (int module, const juce::String& paramId, float value)
{
    if (auto* d = dsp (module))
    {
        if (const auto* spec = d->getDescriptor().findParam (paramId))
            value = juce::jlimit (spec->minValue, spec->maxValue, value);
        d->setParameter (paramId, value);
    }
}

int Builder::copy (int module, juce::AudioProcessor& original,
                   const std::function<juce::String (const juce::String&)>& map)
{
    auto* d = dsp (module);
    if (d == nullptr)
        return 0;

    int copied = 0;
    for (const auto& spec : d->getDescriptor().params)
    {
        const auto pluginId = map != nullptr ? map (spec.id) : spec.id;
        if (pluginId.isEmpty() || findParam (original, pluginId) == nullptr)
            continue;
        set (module, spec.id, param (original, pluginId));
        ++copied;
    }
    return copied;
}

bool Builder::cable (int from, const juce::String& outSocket, int to, const juce::String& inSocket)
{
    return patch.addCable (from, outSocket, to, inSocket);
}

bool Builder::mod (int from, const juce::String& outSocket, int to, const juce::String& paramId, float depth)
{
    return patch.addParamCable (from, outSocket, to, paramId, juce::jlimit (-1.0f, 1.0f, depth));
}

void Builder::setCustomState (int module, const juce::String& state)
{
    if (auto* d = dsp (module))
        d->loadCustomState (state);
}

//==============================================================================
InstanceModule::InstanceModule (const juce::String& pluginId, bool takesMidi)
    : plugin (createOriginal (pluginId, 48000.0)), midi (takesMidi)
{
    midiQueue.ensureSize (1024);
}

InstanceModule::~InstanceModule()
{
    if (plugin != nullptr)
        plugin->setPlayHead (nullptr);
}

void InstanceModule::rawMidiMessage (const juce::MidiMessage& m)
{
    if (midi)
        midiQueue.addEvent (m, pos);
}

void InstanceModule::midiAllNotesOff()
{
    if (midi)
        midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos);
}

void InstanceModule::relink()
{
    links.clear();
    if (plugin == nullptr)
        return;
    const auto& d = getDescriptor();
    for (int i = 0; i < (int) d.params.size(); ++i)
    {
        const auto& spec = d.params[(size_t) i];
        if (spec.type == ParamType::Button)
            continue;
        const auto id = pluginParamFor (spec.id);
        if (id.isNotEmpty())
            if (auto* p = findParam (*plugin, id))
                links.push_back ({ p, i, -1.0f });
    }
}

void InstanceModule::pushKnobs (bool all)
{
    const juce::SpinLock::ScopedTryLockType cardLock (cards.lock);
    for (auto& l : links)
    {
        float raw = all ? getParameterBase (l.knob) : param (l.knob);
        if (cardLock.isLocked())
            cards.read (*this, l.knob, all, raw);   // (a section card holds it)
        const float v = l.param->convertTo0to1 (raw);
        if (all || v != l.last)
        {
            l.param->setValueNotifyingHost (v);
            l.last = v;
        }
    }
}

void InstanceModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    if (plugin == nullptr)
        return;

    const auto& d = getDescriptor();
    inAudio = d.inputIndexOf ("audioIn");
    outAudio = d.outputIndexOf ("audioOut");
    relink();

    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        plugin->releaseResources();
        plugin->setRateAndBufferSizeDetails (sr, kBlock);
        plugin->prepareToPlay (sr, kBlock);
    }
    plugin->setPlayHead (&playHead);

    io.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), kBlock);
    io.clear();
    for (auto& f : inFifo) f.fill (0.0f);
    for (auto& f : outFifo) f.fill (0.0f);
    midiQueue.clear();
    pos = 0;
    sampleCounter = 0;
}

void InstanceModule::reset()
{
    midiQueue.clear();
    if (midi)
        midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0);
}

void InstanceModule::processSample (const StereoFrame* in, StereoFrame* out)
{
    if (inAudio >= 0)
    {
        inFifo[0][(size_t) pos] = in[inAudio][0];
        inFifo[1][(size_t) pos] = in[inAudio][1];
    }
    if (outAudio >= 0)
        out[outAudio] = { outFifo[0][(size_t) pos], outFifo[1][(size_t) pos] };
    extraOutputs (out, pos);

    if (++pos >= kBlock)
    {
        pos = 0;
        runBlock();
    }
}

void InstanceModule::render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiIn)
{
    plugin->processBlock (buffer, midiIn);
}

void InstanceModule::runBlock()
{
    if (plugin == nullptr)
        return;

    // everything the plugin is handed happens under its callback lock, as in
    // a host: the message thread swapping its sound or state never meets a
    // block (or a parameter listener) half way
    const juce::ScopedTryLock sl (plugin->getCallbackLock());
    if (! sl.isLocked() || plugin->isSuspended())
    {
        midiQueue.clear();
        for (auto& f : outFifo) f.fill (0.0f);
        sampleCounter += kBlock;
        return;
    }

    pushKnobs (false);
    beforeBlock();

    // the patch's clock as the plugin's play head: the host's, else a
    // free-running one at the patch tempo
    {
        juce::AudioPlayHead::PositionInfo info;
        bool fromHost = false;
        if (hostPlayHead != nullptr && ! tempoFromLane())   // (6.9.14: a fed Tempo In wins over the host's tempo)
            if (auto host = hostPlayHead->getPosition(); host && host->getBpm() && *host->getBpm() > 0.0)   // (one without a tempo - standalone, Android - does not count)
            {
                info = *host;
                fromHost = true;
            }
        if (! fromHost)
        {
            const double secs = (double) sampleCounter / sampleRate;
            info.setBpm (tempoBpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setTimeInSamples (sampleCounter);
            info.setTimeInSeconds (secs);
            info.setPpqPosition (secs * tempoBpm / 60.0);
            info.setIsPlaying (hostTransport.isPlaying);
        }
        playHead.info = info;
    }

    io.clear();
    if (plugin->getTotalNumInputChannels() > 0)
        for (int ch = 0; ch < juce::jmin (2, io.getNumChannels()); ++ch)
            io.copyFrom (ch, 0, inFifo[(size_t) ch].data(), kBlock);

    render (io, midiQueue);
    midiQueue.clear();

    const bool mono = plugin->getTotalNumOutputChannels() == 1;
    for (int i = 0; i < kBlock; ++i)
    {
        const float l = io.getSample (0, i), r = io.getSample (mono ? 0 : 1, i);
        outFifo[0][(size_t) i] = std::isfinite (l) ? l : 0.0f;
        outFifo[1][(size_t) i] = std::isfinite (r) ? r : 0.0f;
    }
    sampleCounter += kBlock;
}

juce::String InstanceModule::saveCustomState() const
{
    if (plugin == nullptr)
        return {};
    juce::MemoryBlock mb;
    plugin->getStateInformation (mb);
    return mb.toBase64Encoding();
}

void InstanceModule::loadCustomState (const juce::String& s)
{
    juce::MemoryBlock mb;
    if (plugin == nullptr || ! mb.fromBase64Encoding (s) || mb.getSize() == 0)
        return;
    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        plugin->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    for (auto& l : links) l.last = -1.0f;   // the knobs have the last word
    stateRestored();
}

void InstanceModule::copyStateFrom (juce::AudioProcessor& source)
{
    juce::MemoryBlock mb;
    source.getStateInformation (mb);
    loadCustomState (mb.toBase64Encoding());
}

//==============================================================================
// (6.9.14) section cards
//==============================================================================
void CardReceiver::set (const ModuleDescriptor& d, std::vector<std::shared_ptr<SynthModule::CardLink>> links)
{
    std::vector<Hit> hits (d.params.size());
    for (const auto& l : links)
        for (int i = 0; i < (int) l->ids.size(); ++i)
            if (auto* spec = d.findParam (l->ids[(size_t) i]))
            {
                const auto k = (size_t) (spec - d.params.data());
                if (k < hits.size() && hits[k].link == nullptr) hits[k] = { l.get(), i };
            }
    const juce::SpinLock::ScopedLockType sl (lock);
    held.swap (links);
    byKnob.swap (hits);
}

bool CardReceiver::read (SynthModule& engine, int knob, bool base, float& value) const
{
    if (knob < 0 || (size_t) knob >= byKnob.size()) return false;
    const auto& h = byKnob[(size_t) knob];
    if (h.link == nullptr || ! h.link->live.load (std::memory_order_acquire)) return false;
    const float b = h.link->base[(size_t) h.idx].load (std::memory_order_relaxed);
    if (! juce::exactlyEqual (engine.getParameterBase (knob), b))
        engine.setParameterByIndex (knob, b);   // (saved with the engine too, and what it plays without the card)
    value = base ? b : h.link->mod[(size_t) h.idx].load (std::memory_order_relaxed);
    return true;
}

std::vector<juce::String> CardDef::knobIds() const
{
    std::vector<juce::String> out;
    for (const auto& i : ids) if (i.isNotEmpty()) out.push_back (i);
    return out;
}

int CardDef::rows() const
{
    int r = 0, used = 0;
    for (const auto& i : ids)
    {
        if (i.isEmpty()) { if (used > 0) { ++r; used = 0; } continue; }
        if (isDrawn (i)) continue;
        if (r == 0 && used == 0) r = 1;
        if (used >= 5) { ++r; used = 0; }
        ++used;
    }
    return r;
}

ModuleDescriptor cardBaseDescriptor (const CardDef& def)
{
    ModuleDescriptor d;
    d.typeId = def.typeId();
    d.displayName = moduleName (def.pluginId, def.kind);
    d.description = def.description;
    d.section = ModuleSection::Utility;
    d.accent = accentOf (def.pluginId);
    d.sidebarOrder = 420;
    d.sockets = { modIn ("linkIn", "Link In"), modOut ("linkOut", "Link Out") };
    return d;
}

void completeCardDescriptor (const CardDef& def, ModuleDescriptor& d)
{
    // five places a row (a menu takes two), a new row where the def says
    int row = 0, used = 0;
    for (const auto& id : def.ids)
    {
        if (id.isEmpty()) { if (used > 0) { ++row; used = 0; } continue; }
        if (d.findParam (id) != nullptr || def.spec == nullptr) continue;
        auto spec = def.spec (id);
        if (def.isDrawn (id)) { spec.hidden = true; spec.modulatable = false; spec.row = 0; d.params.push_back (std::move (spec)); continue; }
        const int units = juce::jmax (1, spec.widthUnits);
        if (used + units > 5) { ++row; used = 0; }
        used += units;
        spec.row = row;
        d.params.push_back (std::move (spec));
    }
}

bool registerCard (const CardDef& def)
{
    return ModuleFactory::instance().registerModule (cardBaseDescriptor (def),
        [&def] { return std::make_unique<CardModule> (def); },
        [&def] (ModuleDescriptor& d) { completeCardDescriptor (def, d); });
}

CardModule::CardModule (const CardDef& d)
    : def (d), link (std::make_shared<CardLink> (d.knobIds()))
{
}

void CardModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    index.clear();
    for (const auto& id : link->ids) index.push_back (paramIndex (id));
    counter = 0;
    write();
}

void CardModule::write()
{
    for (size_t i = 0; i < index.size(); ++i)
        if (index[i] >= 0)
        {
            link->base[i].store (getParameterBase (index[i]), std::memory_order_relaxed);
            link->mod[i].store (param (index[i]), std::memory_order_relaxed);
        }
    link->live.store (! index.empty(), std::memory_order_release);
}

void CardModule::processSample (const StereoFrame*, StereoFrame* out)
{
    if (counter++ % 16 == 0) write();   // (the engine reads them every 32 samples)
    out[0] = { 0.0f, 0.0f };
}

void CardModule::setParameter (const juce::String& id, float value)
{
    SynthModule::setParameter (id, value);
    for (size_t i = 0; i < link->ids.size() && i < index.size(); ++i)
        if (link->ids[i] == id && index[i] >= 0)
        {
            link->base[i].store (value, std::memory_order_relaxed);
            link->mod[i].store (value, std::memory_order_relaxed);
        }
}

std::vector<juce::Point<int>> cardPlaces (const std::vector<const CardDef*>& cards, int x0, int y0, int columns)
{
    // a card: its title and sockets (~90 px), ~92 px a row of knobs, its picture
    std::vector<juce::Point<int>> out;
    std::vector<int> colY ((size_t) juce::jmax (1, columns), y0);
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const auto c = i % colY.size();
        const auto* cd = cards[i];
        out.push_back ({ x0 + (int) c * 300, colY[c] });
        colY[c] += 90 + cd->rows() * 92 + cd->extraHeight + 40;
    }
    return out;
}

} // namespace aquanode::plugins::kit
