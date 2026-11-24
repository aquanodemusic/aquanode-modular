#include "FruitProcessor.h"
#include "../../Editor/Sidebar.h"   // (the sidebar's groups, for the menus)
#include "../../PluginSources/audiostretcher/Rubberband/rubberband/RubberBandStretcher.h"
#include "FruitEditor.h"
#include "../Host/PluginCatalog.h"

using namespace fruit;

//==============================================================================
// the model
//==============================================================================
float AutoClip::valueAt (float x) const
{
    if (points.empty()) return 0.5f;
    if (points.size() == 1 || x <= points.front().x) return juce::jlimit (0.0f, 1.0f, points.front().y);
    if (x >= points.back().x) return juce::jlimit (0.0f, 1.0f, points.back().y);
    size_t i = 1;
    while (i < points.size() && points[i].x < x) ++i;
    const auto& p1 = points[i - 1];
    const auto& p2 = points[i];
    const auto& p0 = i >= 2 ? points[i - 2] : p1;
    const auto& p3 = i + 1 < points.size() ? points[i + 1] : p2;
    const float t = (p2.x - p1.x) > 1.0e-6f ? (x - p1.x) / (p2.x - p1.x) : 0.0f;
    // Catmull-Rom through the points: the curve passes every point you drew
    const float t2 = t * t, t3 = t2 * t;
    const float y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2
                            + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
    return juce::jlimit (0.0f, 1.0f, y);
}

double Song::lengthBeats() const
{
    double end = 0.0;
    for (const auto& c : clips) end = juce::jmax (end, c.start + c.length);
    for (const auto& a : autos) end = juce::jmax (end, a.start + a.length);
    return juce::jmax (4.0, std::ceil (end / 4.0 - 1.0e-9) * 4.0);
}

//==============================================================================
void fruitRegister (FruitProcessor*, bool add);

FruitProcessor::FruitProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    fruitRegister (this, true);
    for (auto& f : feeds) f.store (nullptr);
    engine = std::make_unique<AquanodeModularAudioProcessor>();
    engine->nestingLevel = juce::jmin (AquanodeModularAudioProcessor::kMaxNestingLevel,
                                       AquanodeModularAudioProcessor::creatingLevel() + 1);
    engine->setAlwaysCompensate (true);   // (6.7.12: its tracks lined up, whatever their effects' latency)
    ensureInsert (0);
    songChanged();
}

FruitProcessor::~FruitProcessor()
{
    fruitRegister (this, false);     // (waits for a channel rack card that is sending it MIDI)
    const juce::ScopedLock lk (processLock);
    for (auto& f : feeds) f.store (nullptr);
    engine.reset();
}

juce::AudioProcessorEditor* FruitProcessor::createEditor()
{
    // the patch version's Fruit DAW card is its channel rack; the other three windows are cards of their own
    return new FruitEditor (*this, splitKey.isNotEmpty() ? splitPart : (int) FruitEditor::All);
}

void FruitProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    engine->setPlayConfigDetails (2, 2, sr, maxBlock);
    engine->prepareToPlay (sr, maxBlock);
    const juce::ScopedLock lk (processLock);
    resolveSamples();
}

void FruitProcessor::releaseResources() { engine->releaseResources(); }

//==============================================================================
// the engine underneath
//==============================================================================
int FruitProcessor::add (const juce::String& typeId, int x, int y)
{
    return engine->addModule (typeId, { x, y });
}

aquanode::SynthModule* FruitProcessor::moduleFor (int id) const
{
    if (id < 0) return nullptr;
    auto* inst = engine->getInstance (id);
    return inst != nullptr ? inst->dsp.get() : nullptr;
}

juce::String FruitProcessor::mainIn (const aquanode::ModuleDescriptor& d)
{
    for (auto& s : d.sockets)
        if (s.direction == aquanode::SocketDirection::Input && s.kind == aquanode::SocketKind::Audio && ! s.hidden) return s.id;
    return {};
}

juce::String FruitProcessor::sideIn (const aquanode::ModuleDescriptor& d)
{
    int n = 0;
    for (auto& s : d.sockets)
        if (s.direction == aquanode::SocketDirection::Input && s.kind == aquanode::SocketKind::Audio && ! s.hidden)
            if (++n == 2) return s.id;
    return {};
}

juce::String FruitProcessor::mainOut (const aquanode::ModuleDescriptor& d)
{
    for (auto& s : d.sockets)
        if (s.direction == aquanode::SocketDirection::Output && s.kind == aquanode::SocketKind::Audio && ! s.hidden) return s.id;
    return {};
}

juce::String FruitProcessor::midiInOf (const aquanode::ModuleDescriptor& d)
{
    for (auto& s : d.sockets)
        if (s.direction == aquanode::SocketDirection::Input && s.kind == aquanode::SocketKind::Midi && ! s.hidden) return s.id;
    return {};
}

void FruitProcessor::removeCablesTouching (int moduleId)
{
    const auto& cables = engine->getCables();
    for (int i = (int) cables.size(); --i >= 0;)
        if (cables[(size_t) i].fromModule == moduleId)
            engine->removeCable (i);
}

void FruitProcessor::removeModuleLocked (int id)
{
    if (id < 0) return;
    const juce::ScopedLock lk (processLock);
    engine->removeModule (id);
}

void FruitProcessor::ensureInsert (int i)
{
    auto& in = inserts[(size_t) i];
    if (in.exists()) return;
    const int x = 200 + i * 40, y = 1200;
    in.inId = add ("util.thru", x, y);
    in.scId = add ("util.thru", x, y + 100);
    in.volId = add ("util.volume", x, y + 200);
    in.meterId = add ("fruit.meter", x, y + 300);
    if (auto* v = moduleFor (in.volId)) v->setParameter ("level", 0.8f);
    if (i == 0 && engine->getInstance (masterOutId) == nullptr)
        masterOutId = add ("io.audioOut", 200, 1700);
    rewire (i);
}

void FruitProcessor::connectSynth (int s)
{
    const auto& slot = slots[(size_t) s];
    auto* inst = engine->getInstance (slot.synthId);
    if (inst == nullptr) return;
    const auto& d = inst->descriptor();
    const auto mi = midiInOf (d), out = mainOut (d);
    if (mi.isNotEmpty() && slot.feedId >= 0) engine->addCable (slot.feedId, "midiOut", slot.synthId, mi);
    if (out.isNotEmpty()) engine->addCable (slot.synthId, out, inserts[(size_t) s].inId, "audioIn");
    if (auto* fi = engine->getInstance (slot.feedId))   // (6.9.14) MPE from the keys, for a synth that plays it
        if (auto* feed = dynamic_cast<FruitNoteFeed*> (fi->dsp.get()))
            feed->mpeOk.store (aquanode::typeAcceptsMpe (d.typeId));
}

void FruitProcessor::rewire (int i)
{
    auto& in = inserts[(size_t) i];
    if (! in.exists()) return;
    // everything this insert's modules send out, built again
    removeCablesTouching (in.inId);
    removeCablesTouching (in.scId);
    removeCablesTouching (in.volId);
    for (int id : in.fx) if (id >= 0) removeCablesTouching (id);
    for (auto& sd : in.sends) if (sd.gainId >= 0) removeCablesTouching (sd.gainId);

    int prev = in.inId;
    juce::String prevOut = "audioOut";
    for (int id : in.fx)
    {
        auto* inst = engine->getInstance (id);
        if (inst == nullptr) continue;
        const auto& d = inst->descriptor();
        const auto a = mainIn (d), b = sideIn (d), o = mainOut (d);
        if (a.isEmpty() || o.isEmpty()) continue;
        engine->addCable (prev, prevOut, id, a);
        if (b.isNotEmpty()) engine->addCable (in.scId, "audioOut", id, b);   // the sidechain bus into its second input
        prev = id;
        prevOut = o;
    }
    engine->addCable (prev, prevOut, in.volId, "audioIn");
    engine->addCable (in.volId, "audioOut", in.meterId, "audioIn");
    removeCablesTouching (in.meterId);
    // what leaves the insert leaves from its strip's end: polarity and pan, after the fader
    const int outId = in.meterId;
    if (i == 0)
        engine->addCable (outId, "audioOut", masterOutId, "audioIn");
    else
    {
        for (int k = 0; k < kSends; ++k)
        {
            const int t = inserts[(size_t) i].sends[(size_t) k].target;
            if (t < 0 || t == i || t >= kSlots)
            {
                // an unused send keeps no module
                const int g = inserts[(size_t) i].sends[(size_t) k].gainId;
                if (g >= 0) { removeModuleLocked (g); inserts[(size_t) i].sends[(size_t) k].gainId = -1; }
                continue;
            }
            ensureInsert (t);                       // (may add modules; inserts is an array, so `in` stays valid)
            auto& sd = inserts[(size_t) i].sends[(size_t) k];
            if (sd.gainId < 0 || engine->getInstance (sd.gainId) == nullptr)
                sd.gainId = add ("util.volume", 200 + i * 40, 1800 + k * 30);
            if (auto* gm = moduleFor (sd.gainId)) gm->setParameter ("level", juce::jlimit (0.0f, 1.0f, sd.level));
            engine->addCable (outId, "audioOut", sd.gainId, "audioIn");
            engine->addCable (sd.gainId, "audioOut", inserts[(size_t) t].inId, "audioIn");
            if (t > 0)                              // the track's sidechain bus hears it in full, whatever the level
                engine->addCable (outId, "audioOut", inserts[(size_t) t].scId, "audioIn");
        }
    }
}

void FruitProcessor::rewireAll()
{
    for (int i = 0; i < kSlots; ++i) rewire (i);
}

bool FruitProcessor::loadSynth (int s, const juce::String& typeId)
{
    if (s < 1 || s >= kSlots) return false;
    ensureInsert (s);
    auto& slot = slots[(size_t) s];
    if (slot.synthId >= 0) removeModuleLocked (slot.synthId);
    slot.synthId = add (typeId, 200 + s * 40, 200);
    if (slot.synthId < 0) { slot = {}; return false; }
    if (slot.feedId < 0 || engine->getInstance (slot.feedId) == nullptr)
    {
        slot.feedId = add ("fruit.notefeed", 200 + s * 40, 100);
        feeds[(size_t) s].store (dynamic_cast<FruitNoteFeed*> (moduleFor (slot.feedId)));
    }
    slot.typeId = typeId;
    if (auto* inst = engine->getInstance (slot.synthId)) slot.name = inst->descriptor().displayName;
    connectSynth (s);
    { const juce::ScopedLock lk (processLock); resolveAutomation(); }
    ++uiVersion;
    return true;
}

void FruitProcessor::clearSlot (int s)
{
    if (s < 1 || s >= kSlots) return;
    auto& slot = slots[(size_t) s];
    removeModuleLocked (slot.synthId);    // its cables go with it; the note feed stays
    slot.synthId = -1;
    slot.typeId = slot.name = {};
    { const juce::ScopedLock lk (processLock); resolveAutomation(); }
    ++uiVersion;
}

bool FruitProcessor::loadEffect (int i, int index, const juce::String& typeId)
{
    if (i < 0 || i >= kSlots || index < 0 || index >= kFxPerInsert) return false;
    ensureInsert (i);
    auto& in = inserts[(size_t) i];
    if (in.fx[(size_t) index] >= 0) removeModuleLocked (in.fx[(size_t) index]);
    in.fx[(size_t) index] = add (typeId, 200 + i * 40, 1300 + index * 30);
    rewire (i);
    { const juce::ScopedLock lk (processLock); resolveAutomation(); }
    ++uiVersion;
    return in.fx[(size_t) index] >= 0;
}

void FruitProcessor::clearEffect (int i, int index)
{
    if (i < 0 || i >= kSlots || index < 0 || index >= kFxPerInsert) return;
    auto& in = inserts[(size_t) i];
    removeModuleLocked (in.fx[(size_t) index]);
    in.fx[(size_t) index] = -1;
    rewire (i);
    { const juce::ScopedLock lk (processLock); resolveAutomation(); }
    ++uiVersion;
}

bool FruitProcessor::wouldLoop (int i, int t) const
{
    // does track t (through its sends, and theirs) come back to track i?
    if (t <= 0) return false;
    if (t == i) return true;
    std::array<bool, kSlots> seen {};
    std::vector<int> todo { t };
    while (! todo.empty())
    {
        const int c = todo.back(); todo.pop_back();
        if (c == i) return true;
        if (c <= 0 || seen[(size_t) c]) continue;
        seen[(size_t) c] = true;
        for (const auto& sd : inserts[(size_t) c].sends) if (sd.target > 0) todo.push_back (sd.target);
    }
    return false;
}

bool FruitProcessor::setSendTarget (int i, int k, int t)
{
    if (i < 1 || i >= kSlots || k < 0 || k >= kSends || t >= kSlots) return false;
    ensureInsert (i);
    auto& sends = inserts[(size_t) i].sends;
    if (t >= 0)
    {
        if (wouldLoop (i, t)) return false;
        for (int j = 0; j < kSends; ++j) if (j != k && sends[(size_t) j].target == t) return false;
    }
    const int was = sends[(size_t) k].target;
    sends[(size_t) k].target = juce::jmax (-1, t);
    rewire (i);
    // the track it went to before, if nothing else uses it now, is let go again
    if (was > 0 && was != t)
        releaseIfUnused (was);
    ++uiVersion;
    return true;
}

bool FruitProcessor::isInsertUsed (int t) const
{
    if (t <= 0) return true;
    if (slots[(size_t) t].synthId >= 0) return true;
    const auto& in = inserts[(size_t) t];
    for (int id : in.fx) if (id >= 0) return true;
    for (const auto& smp : song.samples) if (smp.insert == t) return true;
    for (int j = 1; j < kSlots; ++j)
        if (j != t && inserts[(size_t) j].exists())
            for (const auto& sd : inserts[(size_t) j].sends)
                if (sd.target == t) return true;
    return false;
}

void FruitProcessor::releaseIfUnused (int t)
{
    if (t <= 0 || t >= kSlots || ! inserts[(size_t) t].exists() || isInsertUsed (t)) return;
    // (its own sends go with it: they only fed others from nothing)
    auto& in = inserts[(size_t) t];
    std::vector<int> fed;
    for (auto& sd : in.sends)
    {
        if (sd.target > 0) fed.push_back (sd.target);
        if (sd.gainId >= 0) removeModuleLocked (sd.gainId);
    }
    for (int id : { in.inId, in.scId, in.volId, in.meterId, in.audioFeedId }) removeModuleLocked (id);
    in = fruit::Insert();
    // ... and a track only it fed may now be unused too
    for (int j : fed)
        releaseIfUnused (j);
}

float FruitProcessor::getPan (int i) const
{
    if (auto* m = moduleFor (inserts[(size_t) i].meterId)) return m->getParameter ("pan") * 0.01f;
    return 0.0f;
}

void FruitProcessor::setPan (int i, float pan)
{
    ensureInsert (i);
    if (auto* m = moduleFor (inserts[(size_t) i].meterId)) m->setParameter ("pan", juce::jlimit (-1.0f, 1.0f, pan) * 100.0f);
}

bool FruitProcessor::isInverted (int i) const
{
    if (auto* m = moduleFor (inserts[(size_t) i].meterId)) return m->getParameter ("invert") > 0.5f;
    return false;
}

void FruitProcessor::setInverted (int i, bool on)
{
    ensureInsert (i);
    if (auto* m = moduleFor (inserts[(size_t) i].meterId)) m->setParameter ("invert", on ? 1.0f : 0.0f);
}

int FruitProcessor::insertLatency (int i) const
{
    // its effects' latency, one after the other (what the patch reports for the longest path)
    int total = 0;
    for (int id : inserts[(size_t) i].fx)
        if (auto* m = moduleFor (id))
            total += juce::jmax (0, m->latencySamples());
    return total;
}

void FruitProcessor::setSendLevel (int i, int k, float level)
{
    if (i < 1 || i >= kSlots || k < 0 || k >= kSends) return;
    auto& sd = inserts[(size_t) i].sends[(size_t) k];
    sd.level = juce::jlimit (0.0f, 1.0f, level);
    if (auto* gm = moduleFor (sd.gainId)) gm->setParameter ("level", sd.level);
}

void FruitProcessor::setTarget (int i, int t)
{
    // 6.0's single "sidechain to this track": the master as before, and a send to t at 0 %
    if (i < 1 || i >= kSlots) return;
    ensureInsert (i);
    auto& sends = inserts[(size_t) i].sends;
    if (sends[0].target != 0) { sends[0].target = 0; sends[0].level = 1.0f; }
    sends[1].target = (t > 0 && t != i && ! wouldLoop (i, t)) ? t : -1;
    sends[1].level = 0.0f;
    rewire (i);
    ++uiVersion;
}

float FruitProcessor::getVolume (int i) const
{
    if (auto* v = moduleFor (inserts[(size_t) i].volId)) return v->getParameter ("level");
    return 0.8f;
}

void FruitProcessor::setVolume (int i, float level)
{
    ensureInsert (i);
    if (auto* v = moduleFor (inserts[(size_t) i].volId)) v->setParameter ("level", juce::jlimit (0.0f, 2.0f, level));
}

float FruitProcessor::meterLevel (int i) const
{
    if (auto* m = dynamic_cast<FruitMeter*> (moduleFor (inserts[(size_t) i].meterId)))
        return juce::jmax (m->peakL.load(), m->peakR.load());
    return 0.0f;
}

juce::String FruitProcessor::insertName (int i) const
{
    if (i == 0) return "Master";
    if (slots[(size_t) i].name.isNotEmpty()) return slots[(size_t) i].name;
    return "Insert " + juce::String (i);
}

std::vector<FruitProcessor::Choice> FruitProcessor::synthChoices()
{
    std::vector<Choice> v;
    for (const auto& r : aquanode::ModuleFactory::instance().all())
    {
        const auto& d = r.descriptor;
        // (6.2.1: the VST3 Plugin module too - it hosts synths - now that it sits under Utilities)
        if (d.section != aquanode::ModuleSection::Oscillator && d.typeId != "fx.externalplugin") continue;
        if (d.typeId == "plugin.fruitgui") continue;
        const bool plugin = d.typeId.startsWith ("plugin.") && ! d.typeId.fromFirstOccurrenceOf ("plugin.", false, false).contains (".");
        if (! d.listedInSidebar && ! plugin) continue;
        if (d.typeId.startsWith ("plugin.") && ! plugin) continue;    // a plugin's patch-version parts
        // a plugin's module only where the sidebar lists it among the others (Dual VCO, DX Operator ...)
        if (! plugin && d.isPluginModule() && ! sidebar_groups::isListedInGroup (d.typeId)) continue;
        v.push_back ({ d.typeId, d.displayName, plugin });
    }
    std::sort (v.begin(), v.end(), [] (const Choice& a, const Choice& b)
               { return a.plugin != b.plugin ? ! a.plugin : a.name.compareIgnoreCase (b.name) < 0; });
    return v;
}

std::vector<FruitProcessor::Choice> FruitProcessor::effectChoices()
{
    std::vector<Choice> v;
    for (const auto& r : aquanode::ModuleFactory::instance().all())
    {
        const auto& d = r.descriptor;
        if (d.section != aquanode::ModuleSection::Effect && d.section != aquanode::ModuleSection::Filter && d.typeId != "fx.externalplugin") continue;
        const bool plugin = d.typeId.startsWith ("plugin.") && ! d.typeId.fromFirstOccurrenceOf ("plugin.", false, false).contains (".");
        if (! d.listedInSidebar && ! plugin) continue;
        if (d.typeId.startsWith ("plugin.") && ! plugin) continue;
        if (! plugin && d.isPluginModule() && ! sidebar_groups::isListedInGroup (d.typeId)) continue;
        if (mainIn (d).isEmpty() || mainOut (d).isEmpty()) continue;
        v.push_back ({ d.typeId, d.displayName, plugin });
    }
    std::sort (v.begin(), v.end(), [] (const Choice& a, const Choice& b)
               { return a.plugin != b.plugin ? ! a.plugin : a.name.compareIgnoreCase (b.name) < 0; });
    return v;
}

//==============================================================================
// the song, published to the audio thread
//==============================================================================
void FruitProcessor::songChanged()
{
    auto snap = std::make_shared<const Song> (song);
    {
        const juce::ScopedLock lk (processLock);
        auto old = std::atomic_exchange (&playing, std::shared_ptr<const Song> (snap));
        if (old != nullptr) retired.push_back (std::move (old));
        resolveAutomation();
        resolveSamples();
    }
    // the audio thread has let go of anything older than the last swap
    if (retired.size() > 2) retired.erase (retired.begin(), retired.end() - 2);
    ++uiVersion;
}

//==============================================================================
// Samples on the playlist
//==============================================================================
void FruitProcessor::resolveSamples()
{
    // a feed into every mixer insert that samples go to, and a bus for each
    sampleBus.fill (-1);
    busInsert.clear();
    for (const auto& c : song.clips)
    {
        if (c.sample <= 0 || c.sample > (int) song.samples.size()) continue;
        const int t = juce::jlimit (0, kSlots - 1, song.samples[(size_t) c.sample - 1].insert);
        if (sampleBus[(size_t) t] >= 0) continue;
        ensureInsert (t);
        auto& in = inserts[(size_t) t];
        if (in.audioFeedId < 0 || engine->getInstance (in.audioFeedId) == nullptr)
        {
            in.audioFeedId = add ("fruit.audiofeed", 120 + t * 40, 2200);
            engine->addCable (in.audioFeedId, "audioOut", in.inId, "audioIn");
        }
        sampleBus[(size_t) t] = (int) busInsert.size();
        busInsert.push_back (t);
    }
    for (int t = 0; t < kSlots; ++t)
    {
        auto* f = dynamic_cast<FruitAudioFeed*> (moduleFor (inserts[(size_t) t].audioFeedId));
        if (f != nullptr && sampleBus[(size_t) t] < 0) f->setBlock (nullptr, nullptr, 0);
        audioFeeds[(size_t) t].store (sampleBus[(size_t) t] >= 0 ? f : nullptr);
    }
    sampleMix.setSize (juce::jmax (2, 2 * (int) busInsert.size()), juce::jmax (8192, maxBlock), false, true, true);
}

void FruitProcessor::mixSamples (const Song& s, double b0, double L, double bps, int n, double S)
{
    // audio thread: the song's audio clips into their inserts' buses, at their own speed
    const int buses = (int) busInsert.size();
    if (buses == 0) return;
    const bool on = (Mode) mode.load() == Mode::song && n <= sampleMix.getNumSamples() && sampleMix.getNumChannels() >= 2 * buses;
    for (int k = 0; k < buses; ++k)
        if (auto* f = audioFeeds[(size_t) busInsert[(size_t) k]].load())
            f->setBlock (on ? sampleMix.getReadPointer (2 * k) : nullptr, on ? sampleMix.getReadPointer (2 * k + 1) : nullptr, n);
    if (! on) return;
    for (int k = 0; k < 2 * buses; ++k) sampleMix.clear (k, 0, n);
    const double secondsPerBeat = 1.0 / (bps * sr);
    const double fadeBeats = 0.004 / secondsPerBeat;             // 4 ms in and out: no clicks at a cut
    for (const auto& c : s.clips)
    {
        if (c.sample <= 0 || c.sample > (int) s.samples.size() || c.length <= 0.0) continue;
        const auto& smp = s.samples[(size_t) c.sample - 1];
        const int bus = sampleBus[(size_t) juce::jlimit (0, kSlots - 1, smp.insert)];
        const auto* audio = smp.playing();
        if (audio == nullptr || bus < 0) continue;
        const double end = c.start + c.length;
        // (the block may wrap at the loop's end: both stretches are checked per sample)
        if (! ((c.start < b0 + n * bps && end > b0) || (b0 + n * bps > L && c.start < S + b0 + n * bps - L && end > S))) continue;
        const int len = audio->getNumSamples(), chans = audio->getNumChannels();
        const float* a = audio->getReadPointer (0);
        const float* b = audio->getReadPointer (chans > 1 ? 1 : 0);
        float* outL = sampleMix.getWritePointer (2 * bus);
        float* outR = sampleMix.getWritePointer (2 * bus + 1);
        for (int i = 0; i < n; ++i)
        {
            double beat = b0 + i * bps;
            if (beat >= L) beat = S + (beat - L);
            if (beat < c.start || beat >= end) continue;
            const double x = (beat - c.start + c.offset) * secondsPerBeat * smp.rate;
            const int i0 = (int) x;
            if (i0 < 0 || i0 >= len - 1) continue;
            const float f = (float) (x - i0);
            const float g = (float) juce::jmin (1.0, (beat - c.start) / fadeBeats, (end - beat) / fadeBeats);
            outL[i] += (a[i0] + (a[i0 + 1] - a[i0]) * f) * g;
            outR[i] += (b[i0] + (b[i0 + 1] - b[i0]) * f) * g;
        }
    }
}

double FruitProcessor::detectBpm (const juce::AudioBuffer<float>& audio, double rate)
{
    // the onset strength (rises of the compressed loudness in four bands, 200 times a second) correlated
    // with itself: the lag it repeats at best between 60 and 200 BPM, pulled towards 120
    const int n = juce::jmin (audio.getNumSamples(), (int) (rate * 90.0));
    const int hop = juce::jmax (1, (int) std::round (rate / 200.0));
    const int frames = n / hop;
    if (frames < 400) return 0.0;                                 // under two seconds
    const double fps = rate / hop;
    std::vector<float> onset ((size_t) frames, 0.0f);
    const float cut[4] = { 150.0f, 600.0f, 2500.0f, 20000.0f };
    float lp[4] = {}, prev[4] = {};
    float coef[4];
    for (int k = 0; k < 4; ++k) coef[k] = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (cut[k], (float) rate * 0.45f) / (float) rate);
    const int chans = audio.getNumChannels();
    for (int fr = 0; fr < frames; ++fr)
    {
        float e[4] = {};
        for (int i = fr * hop; i < (fr + 1) * hop; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < chans; ++c) x += audio.getSample (c, i);
            float below = 0.0f;
            for (int k = 0; k < 4; ++k)                           // one-pole splits: each band is what the next low-pass adds
            {
                lp[k] += coef[k] * (x - lp[k]);
                const float band = lp[k] - below;
                below = lp[k];
                e[k] += band * band;
            }
        }
        float rise = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            // (compressed, not logarithmic: a loud hit counts for more than a quiet one, which is
            // what tells the beat from the hi-hats between)
            const float loud = std::sqrt (std::sqrt (e[k] / hop));
            rise += juce::jmax (0.0f, loud - prev[k]);
            prev[k] = loud;
        }
        onset[(size_t) fr] = rise;
    }
    double mean = 0.0;
    for (float v : onset) mean += v;
    mean /= frames;
    double energy = 0.0;
    for (auto& v : onset) { v -= (float) mean; energy += (double) v * v; }
    if (energy < 1.0e-6) return 0.0;
    auto corr = [&onset, frames] (double lag)
    {
        const int l0 = (int) lag; const float f = (float) (lag - l0);
        double sum = 0.0;
        for (int i = 0; i + l0 + 1 < frames; ++i) sum += (double) onset[(size_t) i] * (onset[(size_t) (i + l0)] * (1.0f - f) + onset[(size_t) (i + l0 + 1)] * f);
        return sum / juce::jmax (1, frames - l0 - 1);
    };
    // a tempo's score: how well the onsets repeat at one to eight beats of it. Every one of those
    // lands on a beat only for the real tempo and its halves; a wrong ratio (2/3, 3/2) misses half of them
    auto comb = [&corr, fps] (double bpm)
    {
        const double lag = fps * 60.0 / bpm;
        double sum = 0.0;
        for (int k = 1; k <= 8; ++k) sum += corr (k * lag);
        return sum / 8.0;
    };
    double best = 0.0, bestBpm = 0.0;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.5)
    {
        const double score = comb (bpm) * std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.9, 2.0));     // pulled towards 120
        if (score > best) { best = score; bestBpm = bpm; }
    }
    if (bestBpm <= 0.0 || best < 0.15 * energy / frames) return 0.0;      // nothing that repeats
    // finer around it
    double fine = bestBpm, fineScore = -1.0e9;
    for (double bpm = bestBpm - 0.5; bpm <= bestBpm + 0.5; bpm += 0.05)
    {
        const double score = comb (bpm);
        if (score > fineScore) { fineScore = score; fine = bpm; }
    }
    return std::round (fine * 10.0) / 10.0;
}

int FruitProcessor::addSample (std::unique_ptr<juce::InputStream> stream, const juce::String& name)
{
    if (stream == nullptr) return 0;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::move (stream)));
    if (reader == nullptr) return 0;
    const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));    // ten minutes at most
    if (n <= 1) return 0;
    auto buf = std::make_shared<juce::AudioBuffer<float>> (juce::jlimit (1, 2, (int) reader->numChannels), n);
    reader->read (buf.get(), 0, n, 0, true, reader->numChannels > 1);
    return addSample (buf, reader->sampleRate, name);
}

static juce::String encodeFlac (const juce::AudioBuffer<float>& audio, double rate)
{
    juce::MemoryBlock mb;
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream> (mb, false);
        auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (audio.getNumChannels()).withBitsPerSample (24);
        if (auto writer = flac.createWriterFor (out, options))
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        else
            return {};
    }
    return mb.toBase64Encoding();
}

int FruitProcessor::addSample (std::shared_ptr<const juce::AudioBuffer<float>> audio, double rate, const juce::String& name)
{
    if (audio == nullptr || audio->getNumSamples() < 2 || song.samples.size() >= 99) return 0;
    Sample smp;
    smp.audio = audio;
    smp.rate = rate;
    smp.name = name;
    smp.bpm = detectBpm (*audio, rate);
    smp.encoded = encodeFlac (*audio, rate);
    song.samples.push_back (smp);
    const int number = (int) song.samples.size();

    // a clip of all of it on the track highlighted last: at the start, or at the next bar after what is there
    const int track = juce::jlimit (1, kSlots - 1, view.track);
    double end = 0.0;
    for (const auto& c : song.clips) if (c.track == track) end = juce::jmax (end, c.start + c.length);
    for (const auto& a : song.autos) if (a.track == track) end = juce::jmax (end, a.start + a.length);
    Clip c;
    c.track = track;
    c.start = std::ceil (end / 4.0 - 1.0e-9) * 4.0;
    c.length = juce::jmax (0.25, smp.seconds() * currentBpm() / 60.0);
    c.pattern = 0;
    c.sample = number;
    song.clips.push_back (c);
    view.sample = number;
    view.track = track;
    songChanged();
    return number;
}

void FruitProcessor::removeSample (int number)
{
    if (number < 1 || number > (int) song.samples.size()) return;
    song.clips.erase (std::remove_if (song.clips.begin(), song.clips.end(), [number] (const Clip& c) { return c.sample == number; }), song.clips.end());
    for (auto& c : song.clips) if (c.sample > number) --c.sample;
    song.samples.erase (song.samples.begin() + (number - 1));
    if (view.sample == number) view.sample = 0; else if (view.sample > number) --view.sample;
    songChanged();
}

//-- tempo: halve / double, stretch, the song's tempo ---------------------------
static std::shared_ptr<const juce::AudioBuffer<float>> stretchAudio (const juce::AudioBuffer<float>& in, double rate, double timeRatio)
{
    // the Rubber Band Library, offline: longer or shorter at the same pitch. The finer engine for
    // what is short enough to wait for, the faster one beyond a minute
    using RubberBand::RubberBandStretcher;
    const int channels = in.getNumChannels(), n = in.getNumSamples();
    if (n < 16 || timeRatio <= 0.0) return nullptr;
    RubberBandStretcher rb ((size_t) juce::roundToInt (rate), (size_t) channels,
                            RubberBandStretcher::OptionProcessOffline | RubberBandStretcher::OptionThreadingNever
                              | (n / rate <= 60.0 ? RubberBandStretcher::OptionEngineFiner : RubberBandStretcher::OptionEngineFaster),
                            timeRatio, 1.0);
    rb.setExpectedInputDuration ((size_t) n);
    std::vector<const float*> inPtr ((size_t) channels);
    for (int pos = 0; pos < n;)
    {
        const int chunk = juce::jlimit (1, n - pos, (int) rb.getSamplesRequired());
        for (int c = 0; c < channels; ++c) inPtr[(size_t) c] = in.getReadPointer (c) + pos;
        pos += chunk;
        rb.study (inPtr.data(), (size_t) chunk, pos >= n);
    }
    const int expected = (int) std::ceil (n * timeRatio);
    auto out = std::make_shared<juce::AudioBuffer<float>> (channels, expected + 8192);
    out->clear();
    std::vector<float*> outPtr ((size_t) channels);
    int written = 0;
    auto drain = [&]
    {
        for (;;)
        {
            const int avail = rb.available();
            if (avail <= 0) break;
            const int take = juce::jmin (avail, out->getNumSamples() - written);
            if (take <= 0) { std::vector<float> junk ((size_t) avail); for (auto& p : outPtr) p = junk.data(); rb.retrieve (outPtr.data(), (size_t) avail); break; }
            for (int c = 0; c < channels; ++c) outPtr[(size_t) c] = out->getWritePointer (c) + written;
            written += (int) rb.retrieve (outPtr.data(), (size_t) take);
        }
    };
    for (int pos = 0; pos < n;)
    {
        const int chunk = juce::jlimit (1, n - pos, juce::jmax (256, (int) rb.getSamplesRequired()));
        for (int c = 0; c < channels; ++c) inPtr[(size_t) c] = in.getReadPointer (c) + pos;
        pos += chunk;
        rb.process (inPtr.data(), (size_t) chunk, pos >= n);
        drain();
    }
    drain();
    out->setSize (channels, juce::jlimit (1, out->getNumSamples(), juce::jmax (written, 1)), true, false, true);
    return out;
}

static void makeStretched (fruit::Sample& smp, double songBpm)
{
    smp.stretched = nullptr;
    smp.stretchedBpm = 0.0;
    if (! smp.stretch || smp.audio == nullptr || smp.bpm <= 0.0 || songBpm <= 0.0) return;
    smp.stretchedBpm = songBpm;
    if (std::abs (smp.bpm / songBpm - 1.0) < 0.0005) { smp.stretched = smp.audio; return; }     // already at that tempo
    smp.stretched = stretchAudio (*smp.audio, smp.rate, smp.bpm / songBpm);
}

void FruitProcessor::setSampleStretch (int number, bool on)
{
    if (number < 1 || number > (int) song.samples.size()) return;
    auto& smp = song.samples[(size_t) number - 1];
    if (smp.stretch == on || (on && smp.bpm <= 0.0)) return;
    // its clips keep their start and what they play: their lengths go from seconds at the song's
    // tempo to beats of the sample's own, or back
    const double before = smp.beats (currentBpm());
    smp.stretch = on;
    makeStretched (smp, currentBpm());
    const double factor = before > 0.0 ? smp.beats (currentBpm()) / before : 1.0;
    for (auto& c : song.clips)
        if (c.sample == number) { c.length = juce::jmax (0.25, c.length * factor); c.offset *= factor; }
    songChanged();
}

void FruitProcessor::setSampleBpm (int number, double bpm)
{
    if (number < 1 || number > (int) song.samples.size() || bpm <= 0.0) return;
    auto& smp = song.samples[(size_t) number - 1];
    const double before = smp.beats (currentBpm());
    smp.bpm = juce::jlimit (20.0, 999.0, bpm);
    if (smp.stretch)
    {
        makeStretched (smp, currentBpm());
        const double factor = before > 0.0 ? smp.beats (currentBpm()) / before : 1.0;
        for (auto& c : song.clips)
            if (c.sample == number) { c.length = juce::jmax (0.25, c.length * factor); c.offset *= factor; }
    }
    songChanged();
}

bool FruitProcessor::updateStretches()
{
    const double bpm = currentBpm();
    bool changed = false;
    for (auto& smp : song.samples)
        if (smp.stretch && smp.bpm > 0.0 && std::abs (smp.stretchedBpm - bpm) > 0.005)
        { makeStretched (smp, bpm); changed = true; }
    if (changed) songChanged();
    return changed;
}

//-- the loop region as a file ------------------------------------------------------
namespace
{
    struct FixedTempoHead : public juce::AudioPlayHead
    {
        double bpm = 120.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setIsPlaying (true);
            return p;
        }
    };
}

juce::AudioBuffer<float> FruitProcessor::renderRegion (double& rate)
{
    rate = sr > 0.0 ? sr : 48000.0;
    if (song.clips.empty() && song.autos.empty()) return {};
    double from = 0.0, to = 0.0;
    song.loopRange (from, to);
    const double bpm = currentBpm();
    const int length = (int) std::llround ((to - from) * 60.0 / bpm * rate);
    if (length < 16 || length > (int) (rate * 1800.0)) return {};

    // a copy of this Fruit DAW plays it twice; the second pass has the tails of the first in it
    FruitProcessor copy;
    juce::MemoryBlock state;
    getStateInformation (state);
    copy.setStateInformation (state.getData(), (int) state.getSize());
    for (auto& smp : copy.song.samples)
        if (smp.stretch && std::abs (smp.stretchedBpm - bpm) > 0.005) makeStretched (smp, bpm);
    copy.songChanged();
    FixedTempoHead head;
    head.bpm = bpm;
    copy.setPlayHead (&head);
    constexpr int block = 256;
    copy.setPlayConfigDetails (0, 2, rate, block);
    copy.prepareToPlay (rate, block);
    copy.playSong();
    juce::AudioBuffer<float> out (2, length), buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < 2 * length; pos += block)
    {
        const int n = juce::jmin (block, 2 * length - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        midi.clear();
        copy.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                if (pos + i >= length) out.setSample (c, pos + i - length, buf.getSample (c, i));
    }
    copy.setPlayHead (nullptr);
    return out;
}

void FruitProcessor::setSampleInsert (int number, int insert)
{
    if (number < 1 || number > (int) song.samples.size()) return;
    song.samples[(size_t) number - 1].insert = juce::jlimit (0, kSlots - 1, insert);
    songChanged();
}

void FruitProcessor::resolveAutomation()
{
    autoTargets.assign (song.autos.size(), {});
    for (size_t i = 0; i < song.autos.size(); ++i)
    {
        const auto& a = song.autos[i];
        auto* dsp = moduleFor (a.moduleId);
        if (dsp == nullptr) continue;
        auto& t = autoTargets[i];
        if (a.pluginParam >= 0)
        {
            if (auto* h = dynamic_cast<aquanode::plugins::HostedPluginModule*> (dsp))
                if (auto* pl = h->getPlugin())
                    if (a.pluginParam < pl->getParameters().size())
                        t.param = pl->getParameters()[a.pluginParam];
        }
        else if (auto* inst = engine->getInstance (a.moduleId))
        {
            const auto& params = inst->descriptor().params;
            if (juce::isPositiveAndBelow (a.paramIndex, (int) params.size()))
            {
                t.dsp = dsp;
                t.index = a.paramIndex;
                t.lo = params[(size_t) a.paramIndex].minValue;
                t.hi = params[(size_t) a.paramIndex].maxValue;
                t.log = params[(size_t) a.paramIndex].logarithmic && t.lo > 0.0f;
            }
        }
    }
}

double FruitProcessor::songBeats() const
{
    auto s = std::atomic_load (&playing);
    return (s != nullptr && (! s->clips.empty() || ! s->autos.empty())) ? s->lengthBeats() : 0.0;
}

void FruitProcessor::playSongFromStart (bool play) { command.store (play ? 4 : 3); }
void FruitProcessor::playPattern (int p) { playingPattern.store (juce::jlimit (1, kSlots - 1, p)); command.store (1); }
void FruitProcessor::playSong() { command.store (2); }
void FruitProcessor::stop() { command.store (3); }

//==============================================================================
// playing (audio thread)
//==============================================================================
void FruitProcessor::pushNote (int slot, int offset, int pitch, float vel, bool on)
{
    if (slot < 1 || slot >= kSlots) return;
    if (auto* f = feeds[(size_t) slot].load()) f->push (offset, pitch, vel, on);
}

// every Fruit DAW, so a channel rack card finds the one with its key (the audio thread only tries the lock)
namespace
{
    juce::SpinLock& allFruitLock() { static juce::SpinLock l; return l; }
    std::vector<FruitProcessor*>& allFruit() { static std::vector<FruitProcessor*> v; return v; }
}
void fruitRegister (FruitProcessor* f, bool add)
{
    const juce::SpinLock::ScopedLockType sl (allFruitLock());
    auto& v = allFruit();
    v.erase (std::remove (v.begin(), v.end(), f), v.end());
    if (add) v.push_back (f);
}
void FruitProcessor::setSplit (const juce::String& key, int part)
{
    const juce::SpinLock::ScopedLockType sl (allFruitLock());
    splitKey = key;
    splitPart = part;
}
void FruitProcessor::sendMidiTo (const juce::String& key, const juce::MidiMessage& m)
{
    const juce::SpinLock::ScopedTryLockType sl (allFruitLock());
    if (! sl.isLocked() || key.isEmpty()) return;
    for (auto* f : allFruit())
        if (f->splitKey == key)
        {
            if (f->numSentMidi < (int) f->sentMidi.size()) f->sentMidi[(size_t) f->numSentMidi++] = m;
            return;
        }
}

void FruitProcessor::collectRecorded()
{
    int i1, n1, i2, n2;
    recFifo.prepareToRead (recFifo.getNumReady(), i1, n1, i2, n2);
    if (n1 + n2 == 0) return;
    auto& pat = song.patterns[(size_t) juce::jlimit (1, kSlots - 1, playingPattern.load())];
    for (int k = 0; k < n1 + n2; ++k)
    {
        auto nt = recBuf[(size_t) (k < n1 ? i1 + k : i2 + k - n1)];
        nt.start = std::round (nt.start * 4.0) / 4.0;                    // onto the 1/16 grid
        if (nt.start >= pat.lengthBeats) nt.start = 0.0;
        nt.length = juce::jmax (0.25, std::round (nt.length * 4.0) / 4.0);
        // the same key at the same place again replaces what was there
        pat.notes.erase (std::remove_if (pat.notes.begin(), pat.notes.end(), [&nt] (const Note& o)
                             { return o.pitch == nt.pitch && std::abs (o.start - nt.start) < 0.01; }), pat.notes.end());
        pat.notes.push_back (nt);
    }
    recFifo.finishedRead (n1 + n2);
    songChanged();
    ++uiVersion;
}

void FruitProcessor::allNotesOff (int offset)
{
    for (int i = 0; i < numActive; ++i)
        pushNote (active[(size_t) i].slot, offset, active[(size_t) i].pitch, 0.0f, false);
    numActive = 0;
}

void FruitProcessor::emitClip (const Pattern& pat, double clipStart, double clipEnd, double offset,
                               double b0, double b1, double bps, int startSample)
{
    const double L = juce::jmax (0.25, pat.lengthBeats);
    for (const auto& n : pat.notes)
    {
        if (n.start >= L) continue;
        double t0 = n.start - offset;
        if (t0 < 0.0) t0 += std::ceil (-t0 / L) * L;            // its first time round after the trimmed start
        const double first = clipStart + t0;
        double k = std::max (0.0, std::ceil ((b0 - first) / L - 1.0e-9));
        for (double t = first + k * L; t < b1 && t < clipEnd; t += L)
        {
            if (t < b0) continue;
            const int off = startSample + juce::jlimit (0, 1 << 20, (int) ((t - b0) / bps));
            pushNote (pat.slot, off, n.pitch, n.velocity, true);
            if (numActive < (int) active.size())
                active[(size_t) numActive++] = { pat.slot, n.pitch, juce::jmin (t + juce::jmax (0.01, n.length), clipEnd) };
        }
    }
}

void FruitProcessor::sequence (const Song& s, double b0, double b1, double bps, int startSample, int numSamples)
{
    if ((Mode) mode.load() == Mode::pattern)
    {
        const auto& pat = s.patterns[(size_t) playingPattern.load()];
        emitClip (pat, 0.0, pat.lengthBeats, 0.0, b0, b1, bps, startSample);
    }
    else
        for (const auto& c : s.clips)
            if (c.sample <= 0 && c.start < b1 && c.start + c.length > b0 && juce::isPositiveAndBelow (c.pattern, kSlots))
                emitClip (s.patterns[(size_t) c.pattern], c.start, c.start + c.length, c.offset, b0, b1, bps, startSample);

    // note-offs due in this stretch
    for (int i = 0; i < numActive;)
    {
        auto& a = active[(size_t) i];
        if (a.offBeat < b1)
        {
            const int off = startSample + juce::jlimit (0, numSamples - 1, (int) ((a.offBeat - b0) / bps) + 1);
            pushNote (a.slot, off, a.pitch, 0.0f, false);
            active[(size_t) i] = active[(size_t) --numActive];
        }
        else ++i;
    }
}

void FruitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedTryLock lk (processLock);
    if (! lk.isLocked()) { buffer.clear(); return; }

    const int n = buffer.getNumSamples();
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm(); b && *b > 0.0) bpm = *b;
    const double bps = bpm / 60.0 / sr;
    lastBpm.store (bpm, std::memory_order_relaxed);
    auto s = std::atomic_load (&playing);

    for (auto& f : feeds)
        if (auto* fp = f.load()) fp->beginBlock();

    const int cmd = command.exchange (0);
    const bool command4 = cmd == 4;
    switch (cmd)
    {
        case 1: allNotesOff (0); mode.store ((int) Mode::pattern); position.store (0.0); break;
        case 2:     // Play Song: the loop region
        case 4:     // Render / Play All: the whole song
        {
            const bool whole = command4;
            allNotesOff (0); mode.store ((int) Mode::song); wholeSong = whole;
            double from = 0.0, to = 0.0;
            if (s != nullptr && ! whole) s->loopRange (from, to);
            position.store (from);
            break;
        }
        case 3: allNotesOff (0); mode.store ((int) Mode::stopped); position.store (0.0); break;
        default: break;
    }

    // the keys play the slot the piano roll shows
    const int ks = juce::jlimit (1, kSlots - 1, keysSlot.load());
    if (ks != liveSlot)
    {
        for (int p = 0; p < 128; ++p)
            if (liveHeld[(size_t) liveSlot][(size_t) p]) { pushNote (liveSlot, 0, p, 0.0f, false); liveHeld[(size_t) liveSlot][(size_t) p] = false; }
        liveSlot = ks;
    }
    for (int i = 0; i < numSentMidi; ++i) midi.addEvent (sentMidi[(size_t) i], 0);     // the channel rack card's Midi In
    numSentMidi = 0;
    // Record: what the keys play while a pattern loops goes into that pattern
    const bool rec = recording.load (std::memory_order_relaxed) && (Mode) mode.load() == Mode::pattern && s != nullptr;
    const double recL = rec ? juce::jmax (0.25, s->patterns[(size_t) playingPattern.load()].lengthBeats) : 1.0;
    if (! rec) recStart.fill (-1.0);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int off = juce::jlimit (0, n - 1, meta.samplePosition);
        if (rec && (m.isNoteOn() || m.isNoteOff()))
        {
            const int p = m.getNoteNumber();
            const double beat = std::fmod (position.load() + off * bps, recL);
            if (m.isNoteOn()) { recStart[(size_t) p] = beat; recVel[(size_t) p] = m.getFloatVelocity(); }
            else if (recStart[(size_t) p] >= 0.0)
            {
                double len = beat - recStart[(size_t) p];
                if (len <= 0.0) len += recL;
                int i1, n1, i2, n2;
                recFifo.prepareToWrite (1, i1, n1, i2, n2);
                if (n1 > 0) { recBuf[(size_t) i1] = { recStart[(size_t) p], len, p, recVel[(size_t) p] }; recFifo.finishedWrite (1); }
                recStart[(size_t) p] = -1.0;
            }
        }
        // (6.9.14) MPE: a member channel's note carries its own bend (48 semitones), pressure and
        // Slide, and their changes reach it while it sounds - when the keys' synth plays MPE
        const int ch = m.getChannel();
        if (ch >= 2 && ch <= 16)
        {
            auto* lf = feeds[(size_t) liveSlot].load();
            const bool mpe = lf != nullptr && lf->mpeOk.load (std::memory_order_relaxed);
            const auto c = (size_t) ch;
            bool expression = false;
            if (m.isPitchWheel())             { mpeBend[c] = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 48.0f; expression = true; }
            else if (m.isChannelPressure())   { mpePress[c] = (float) m.getChannelPressureValue() / 127.0f; expression = true; }
            else if (m.isControllerOfType (74)) { mpeSlide[c] = (float) m.getControllerValue() / 127.0f; expression = true; }
            if (expression)
            {
                if (mpe && mpeNote[c] >= 0)
                {
                    aquanode::MidiDriverEvent e;
                    e.note = mpeNote[c]; e.mpe = true; e.isExpression = true;
                    e.bend = mpeBend[c]; e.pressure = mpePress[c]; e.slide = mpeSlide[c];
                    lf->pushEvent (off, e);
                }
                continue;
            }
            if (m.isNoteOn() && mpe)
            {
                aquanode::MidiDriverEvent e;
                e.note = m.getNoteNumber(); e.velocity = m.getFloatVelocity(); e.isOn = true; e.mpe = true;
                e.bend = mpeBend[c]; e.pressure = mpePress[c]; e.slide = mpeSlide[c];
                lf->pushEvent (off, e);
                mpeNote[c] = m.getNoteNumber();
                liveHeld[(size_t) liveSlot][(size_t) m.getNoteNumber()] = true;
                continue;
            }
            if (m.isNoteOff() && mpeNote[c] == m.getNoteNumber())
            {
                mpeNote[c] = -1; mpeBend[c] = 0.0f; mpePress[c] = -1.0f; mpeSlide[c] = -1.0f;
            }
        }
        if (m.isNoteOn()) { pushNote (liveSlot, off, m.getNoteNumber(), m.getFloatVelocity(), true); liveHeld[(size_t) liveSlot][(size_t) m.getNoteNumber()] = true; }
        else if (m.isNoteOff()) { pushNote (liveSlot, off, m.getNoteNumber(), 0.0f, false); liveHeld[(size_t) liveSlot][(size_t) m.getNoteNumber()] = false; }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (int p = 0; p < 128; ++p)
                if (liveHeld[(size_t) liveSlot][(size_t) p]) { pushNote (liveSlot, off, p, 0.0f, false); liveHeld[(size_t) liveSlot][(size_t) p] = false; }
    }

    const auto m = (Mode) mode.load();
    if (s != nullptr && (m != Mode::song || s->samples.empty())) mixSamples (*s, 0.0, 1.0, bps, n, 0.0);     // (silence)
    if (m != Mode::stopped && s != nullptr)
    {
        // what loops: the pattern, or the song's loop region (S..L; the whole song without one)
        double S = 0.0, L = s->lengthBeats();
        if (m == Mode::pattern) L = juce::jmax (0.25, s->patterns[(size_t) playingPattern.load()].lengthBeats);
        else if (! wholeSong) s->loopRange (S, L);
        double b0 = position.load();
        if (b0 >= L || b0 < S) b0 = S + std::fmod (juce::jmax (0.0, b0 - S), juce::jmax (0.25, L - S));
        if (m == Mode::song && ! s->samples.empty()) mixSamples (*s, b0, L, bps, n, S);
        const double b1 = b0 + n * bps;
        if (b1 <= L)
            sequence (*s, b0, b1, bps, 0, n);
        else
        {
            const int split = juce::jlimit (0, n - 1, (int) ((L - b0) / bps));
            sequence (*s, b0, L, bps, 0, split + 1);
            allNotesOff (split);                     // the loop point
            sequence (*s, S, S + b1 - L, bps, split, n - split);
        }
        const double newPos = b1 > L ? S + b1 - L : b1;
        position.store (newPos);

        // Knob Record clips move their knobs (song only)
        if (m == Mode::song && autoTargets.size() == s->autos.size())
            for (size_t i = 0; i < s->autos.size(); ++i)
            {
                const auto& a = s->autos[i];
                if (b0 < a.start || b0 >= a.start + a.length || a.length <= 0.0) continue;
                const float v = a.valueAt ((float) ((b0 - a.start) / a.length));
                const auto& t = autoTargets[i];
                if (t.param != nullptr) t.param->setValue (v);
                else if (t.dsp != nullptr)
                    t.dsp->setParameterByIndex (t.index, t.log ? t.lo * std::pow (t.hi / t.lo, v) : t.lo + (t.hi - t.lo) * v);
            }
    }

    for (auto& f : feeds)
        if (auto* fp = f.load()) fp->sortBlock();

    engine->setPlayHead (getPlayHead());
    juce::MidiBuffer none;
    buffer.clear();
    engine->processBlock (buffer, none);
}

//==============================================================================
// Knob Record
//==============================================================================
void FruitProcessor::scanForTurnedKnobs()
{
    std::vector<std::pair<int, juce::String>> modules;      // module id, what it is called on the panel
    for (int s = 1; s < kSlots; ++s)
        if (slots[(size_t) s].synthId >= 0) modules.push_back ({ slots[(size_t) s].synthId, juce::String (s).paddedLeft ('0', 2) + " " + slots[(size_t) s].name });
    for (int i = 0; i < kSlots; ++i)
    {
        const auto& in = inserts[(size_t) i];
        if (! in.exists()) continue;
        const auto where = i == 0 ? juce::String ("Master") : "Insert " + juce::String (i).paddedLeft ('0', 2);
        modules.push_back ({ in.volId, where + " Volume" });
        for (int id : in.fx)
            if (auto* inst = engine->getInstance (id))
                modules.push_back ({ id, where + " " + inst->descriptor().displayName });
    }

    // knobs that Knob Record clips move while the song plays are not "turned"
    std::set<std::pair<int, int>> automated;
    if (getMode() == Mode::song)
        for (const auto& a : song.autos) automated.insert ({ a.moduleId, a.pluginParam >= 0 ? 100000 + a.pluginParam : a.paramIndex });

    for (const auto& [id, where] : modules)
    {
        auto* inst = engine->getInstance (id);
        if (inst == nullptr) continue;
        if (auto* h = dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get()))
        {
            if (auto* pl = h->getPlugin())
            {
                const auto& ps = pl->getParameters();
                for (int k = 0; k < juce::jmin (ps.size(), 4096); ++k)
                {
                    const auto key = std::make_pair (id, 100000 + k);
                    const float v = ps[k]->getValue();
                    auto it = knobSnapshot.find (key);
                    if (it == knobSnapshot.end()) { knobSnapshot[key] = v; continue; }
                    if (std::abs (it->second - v) > 1.0e-5f)
                    {
                        it->second = v;
                        if (automated.count (key) == 0)
                            lastKnob = { id, -1, k, where + ": " + ps[k]->getName (40) };
                    }
                }
            }
            continue;
        }
        const auto& params = inst->descriptor().params;
        for (int k = 0; k < (int) params.size(); ++k)
        {
            if (params[(size_t) k].hidden) continue;
            const auto key = std::make_pair (id, k);
            const float v = inst->dsp->getParameterBase (k);
            auto it = knobSnapshot.find (key);
            if (it == knobSnapshot.end()) { knobSnapshot[key] = v; continue; }
            if (std::abs (it->second - v) > 1.0e-6f)
            {
                it->second = v;
                if (automated.count (key) == 0)
                    lastKnob = { id, k, -1, where + ": " + params[(size_t) k].label };
            }
        }
    }
}

void FruitProcessor::addKnobRecordClip (int track, double start)
{
    if (lastKnob.moduleId < 0 || engine->getInstance (lastKnob.moduleId) == nullptr) return;
    AutoClip a;
    a.track = juce::jlimit (1, kSlots - 1, track);
    a.start = juce::jmax (0.0, start);
    a.length = 16.0;
    a.moduleId = lastKnob.moduleId;
    a.paramIndex = lastKnob.paramIndex;
    a.pluginParam = lastKnob.pluginParam;
    a.label = lastKnob.label;
    // start flat at where the knob is now
    float v = 0.5f;
    if (auto* inst = engine->getInstance (a.moduleId))
    {
        if (a.pluginParam >= 0)
        {
            if (auto* h = dynamic_cast<aquanode::plugins::HostedPluginModule*> (inst->dsp.get()))
                if (auto* pl = h->getPlugin())
                    if (a.pluginParam < pl->getParameters().size()) v = pl->getParameters()[a.pluginParam]->getValue();
        }
        else if (juce::isPositiveAndBelow (a.paramIndex, (int) inst->descriptor().params.size()))
        {
            const auto& ps = inst->descriptor().params[(size_t) a.paramIndex];
            const float x = inst->dsp->getParameterBase (a.paramIndex);
            v = (ps.logarithmic && ps.minValue > 0.0f) ? std::log (x / ps.minValue) / std::log (ps.maxValue / ps.minValue)
                                                        : (x - ps.minValue) / juce::jmax (1.0e-9f, ps.maxValue - ps.minValue);
        }
    }
    a.points = { { 0.0f, juce::jlimit (0.0f, 1.0f, v) }, { 1.0f, juce::jlimit (0.0f, 1.0f, v) } };
    song.autos.push_back (a);
    songChanged();
}

//==============================================================================
// saving
//==============================================================================
static juce::String doublesToBase64 (const std::vector<double>& v)
{
    juce::MemoryOutputStream mo;
    for (double d : v) mo.writeDouble (d);
    return mo.getMemoryBlock().toBase64Encoding();
}

static std::vector<double> base64ToDoubles (const juce::String& s)
{
    std::vector<double> v;
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (s)) return v;
    juce::MemoryInputStream mi (mb, false);
    while (mi.getNumBytesRemaining() >= 8) v.push_back (mi.readDouble());
    return v;
}

void FruitProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("FruitGUI");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("keysSlot", keysSlot.load());
    xml.setAttribute ("pattern", playingPattern.load());
    xml.setAttribute ("masterOut", masterOutId);
    if (splitKey.isNotEmpty()) { xml.setAttribute ("split", splitKey); xml.setAttribute ("splitPart", splitPart); }
    {
        juce::MemoryBlock eng;
        engine->getStateInformation (eng);
        xml.createNewChildElement ("Engine")->addTextElement (eng.toBase64Encoding());
    }
    auto* sl = xml.createNewChildElement ("Slots");
    for (int s = 1; s < kSlots; ++s)
    {
        const auto& slot = slots[(size_t) s];
        if (slot.synthId < 0 && slot.feedId < 0) continue;
        auto* e = sl->createNewChildElement ("Slot");
        e->setAttribute ("index", s); e->setAttribute ("synth", slot.synthId); e->setAttribute ("feed", slot.feedId);
        e->setAttribute ("type", slot.typeId); e->setAttribute ("name", slot.name);
    }
    auto* ins = xml.createNewChildElement ("Inserts");
    for (int i = 0; i < kSlots; ++i)
    {
        const auto& in = inserts[(size_t) i];
        if (! in.exists()) continue;
        auto* e = ins->createNewChildElement ("Insert");
        e->setAttribute ("index", i); e->setAttribute ("in", in.inId); e->setAttribute ("sc", in.scId);
        e->setAttribute ("vol", in.volId); e->setAttribute ("meter", in.meterId);
        if (in.audioFeedId >= 0) e->setAttribute ("afeed", in.audioFeedId);
        {
            juce::StringArray sd;
            for (const auto& x : in.sends) sd.add (juce::String (x.target) + ":" + juce::String (x.level, 4) + ":" + juce::String (x.gainId));
            e->setAttribute ("sends", sd.joinIntoString (","));
        }
        juce::StringArray fx;
        for (int id : in.fx) fx.add (juce::String (id));
        e->setAttribute ("fx", fx.joinIntoString (","));
    }
    auto* pats = xml.createNewChildElement ("Patterns");
    for (int p = 1; p < kSlots; ++p)
    {
        const auto& pat = song.patterns[(size_t) p];
        if (pat.notes.empty() && pat.slot == 1 && pat.lengthBeats == 16.0) continue;
        auto* e = pats->createNewChildElement ("Pattern");
        e->setAttribute ("index", p); e->setAttribute ("length", pat.lengthBeats); e->setAttribute ("slot", pat.slot);
        std::vector<double> d;
        for (const auto& nt : pat.notes) { d.push_back (nt.start); d.push_back (nt.length); d.push_back (nt.pitch); d.push_back (nt.velocity); }
        e->addTextElement (doublesToBase64 (d));
    }
    if (song.loopEnd > song.loopStart) { xml.setAttribute ("loopStart", song.loopStart); xml.setAttribute ("loopEnd", song.loopEnd); }
    auto* clips = xml.createNewChildElement ("Clips");
    for (const auto& c : song.clips)
    {
        auto* e = clips->createNewChildElement ("Clip");
        e->setAttribute ("track", c.track); e->setAttribute ("start", c.start); e->setAttribute ("length", c.length);
        e->setAttribute ("offset", c.offset); e->setAttribute ("pattern", c.pattern);
        if (c.sample > 0) e->setAttribute ("sample", c.sample);
    }
    if (! song.samples.empty())
    {
        auto* samples = xml.createNewChildElement ("Samples");
        for (const auto& smp : song.samples)
        {
            auto* e = samples->createNewChildElement ("Sample");
            e->setAttribute ("name", smp.name); e->setAttribute ("bpm", smp.bpm); e->setAttribute ("insert", smp.insert);
            if (smp.stretch) e->setAttribute ("stretchBpm", smp.stretchedBpm > 0.0 ? smp.stretchedBpm : currentBpm());
            e->addTextElement (smp.encoded);
        }
    }
    auto* autos = xml.createNewChildElement ("Autos");
    for (const auto& a : song.autos)
    {
        auto* e = autos->createNewChildElement ("Auto");
        e->setAttribute ("track", a.track); e->setAttribute ("start", a.start); e->setAttribute ("length", a.length);
        e->setAttribute ("module", a.moduleId); e->setAttribute ("param", a.paramIndex); e->setAttribute ("pluginParam", a.pluginParam);
        e->setAttribute ("label", a.label);
        std::vector<double> d;
        for (const auto& p : a.points) { d.push_back (p.x); d.push_back (p.y); }
        e->addTextElement (doublesToBase64 (d));
    }
    copyXmlToBinary (xml, dest);
}

void FruitProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("FruitGUI")) return;

    {
        const juce::ScopedLock lk (processLock);
        for (auto& f : feeds) f.store (nullptr);
        numActive = 0;
        if (auto* e = xml->getChildByName ("Engine"))
        {
            juce::MemoryBlock eng;
            if (eng.fromBase64Encoding (e->getAllSubText().trim()))
                engine->setStateInformation (eng.getData(), (int) eng.getSize());
        }
    }
    masterOutId = xml->getIntAttribute ("masterOut", -1);
    keysSlot.store (juce::jlimit (1, kSlots - 1, xml->getIntAttribute ("keysSlot", 1)));
    playingPattern.store (juce::jlimit (1, kSlots - 1, xml->getIntAttribute ("pattern", 1)));

    bool converted = false;
    slots.fill ({});
    for (auto& in : inserts) in = {};
    song = {};
    if (auto* sl = xml->getChildByName ("Slots"))
        for (auto* e : sl->getChildWithTagNameIterator ("Slot"))
        {
            const int s = e->getIntAttribute ("index");
            if (s < 1 || s >= kSlots) continue;
            auto& slot = slots[(size_t) s];
            slot.synthId = engine->getInstance (e->getIntAttribute ("synth", -1)) != nullptr ? e->getIntAttribute ("synth") : -1;
            slot.feedId = e->getIntAttribute ("feed", -1);
            slot.typeId = slot.synthId >= 0 ? e->getStringAttribute ("type") : juce::String();
            slot.name = slot.synthId >= 0 ? e->getStringAttribute ("name") : juce::String();
            feeds[(size_t) s].store (dynamic_cast<FruitNoteFeed*> (moduleFor (slot.feedId)));
        }
    if (auto* ins = xml->getChildByName ("Inserts"))
        for (auto* e : ins->getChildWithTagNameIterator ("Insert"))
        {
            const int i = e->getIntAttribute ("index");
            if (i < 0 || i >= kSlots) continue;
            auto& in = inserts[(size_t) i];
            in.inId = e->getIntAttribute ("in", -1); in.scId = e->getIntAttribute ("sc", -1);
            in.volId = e->getIntAttribute ("vol", -1); in.meterId = e->getIntAttribute ("meter", -1);
            in.audioFeedId = e->getIntAttribute ("afeed", -1);
            if (e->hasAttribute ("sends"))
            {
                const auto sd = juce::StringArray::fromTokens (e->getStringAttribute ("sends"), ",", "");
                for (int k = 0; k < kSends; ++k)
                {
                    auto& x = in.sends[(size_t) k];
                    x = {};
                    if (k >= sd.size()) continue;
                    const auto f = juce::StringArray::fromTokens (sd[k], ":", "");
                    x.target = juce::jlimit (-1, kSlots - 1, f[0].getIntValue());
                    x.level = juce::jlimit (0.0f, 1.0f, f[1].getFloatValue());
                    x.gainId = engine->getInstance (f[2].getIntValue()) != nullptr ? f[2].getIntValue() : -1;
                }
            }
            else
            {
                // from 6.0: the master, and "also a sidechain into track n" as a send at 0 %
                const int t = e->getIntAttribute ("target", 0);
                in.sends[0] = { 0, 1.0f, -1 };
                if (t > 0 && t != i) in.sends[1] = { t, 0.0f, -1 };
                converted = true;
            }
            const auto fx = juce::StringArray::fromTokens (e->getStringAttribute ("fx"), ",", "");
            for (int k = 0; k < kFxPerInsert && k < fx.size(); ++k)
                in.fx[(size_t) k] = engine->getInstance (fx[k].getIntValue()) != nullptr ? fx[k].getIntValue() : -1;
        }
    if (auto* pats = xml->getChildByName ("Patterns"))
        for (auto* e : pats->getChildWithTagNameIterator ("Pattern"))
        {
            const int p = e->getIntAttribute ("index");
            if (p < 1 || p >= kSlots) continue;
            auto& pat = song.patterns[(size_t) p];
            pat.lengthBeats = juce::jmax (1.0, e->getDoubleAttribute ("length", 16.0));
            pat.slot = juce::jlimit (1, kSlots - 1, e->getIntAttribute ("slot", 1));
            const auto d = base64ToDoubles (e->getAllSubText().trim());
            for (size_t k = 0; k + 3 < d.size(); k += 4)
                pat.notes.push_back ({ d[k], d[k + 1], (int) d[k + 2], (float) d[k + 3] });
        }
    song.loopStart = xml->getDoubleAttribute ("loopStart", 0.0);
    song.loopEnd = xml->getDoubleAttribute ("loopEnd", 0.0);
    if (auto* clips = xml->getChildByName ("Clips"))
        for (auto* e : clips->getChildWithTagNameIterator ("Clip"))
            song.clips.push_back ({ e->getIntAttribute ("track", 1), e->getDoubleAttribute ("start"), e->getDoubleAttribute ("length", 16.0),
                                    e->getDoubleAttribute ("offset"), e->getIntAttribute ("pattern", 1), e->getIntAttribute ("sample", 0) });
    if (auto* samples = xml->getChildByName ("Samples"))
        for (auto* e : samples->getChildWithTagNameIterator ("Sample"))
        {
            // (a sample that does not decode keeps its place, silent, so the clips' numbers stay right)
            Sample smp;
            smp.name = e->getStringAttribute ("name"); smp.bpm = e->getDoubleAttribute ("bpm"); smp.insert = e->getIntAttribute ("insert");
            smp.encoded = e->getAllSubText().trim();
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (smp.encoded))
            {
                juce::FlacAudioFormat flac;
                if (std::unique_ptr<juce::AudioFormatReader> reader { flac.createReaderFor (new juce::MemoryInputStream (mb, false), true) })
                {
                    auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (buf.get(), 0, (int) reader->lengthInSamples, 0, true, reader->numChannels > 1);
                    smp.audio = buf;
                    smp.rate = reader->sampleRate;
                }
            }
            // (stretched for the tempo it was saved at; the panel does it again if the tempo is another now)
            if (const double sb = e->getDoubleAttribute ("stretchBpm", 0.0); sb > 0.0) { smp.stretch = true; makeStretched (smp, sb); }
            song.samples.push_back (std::move (smp));
        }
    if (auto* autos = xml->getChildByName ("Autos"))
        for (auto* e : autos->getChildWithTagNameIterator ("Auto"))
        {
            AutoClip a;
            a.track = e->getIntAttribute ("track", 1); a.start = e->getDoubleAttribute ("start"); a.length = e->getDoubleAttribute ("length", 16.0);
            a.moduleId = e->getIntAttribute ("module", -1); a.paramIndex = e->getIntAttribute ("param", -1);
            a.pluginParam = e->getIntAttribute ("pluginParam", -1); a.label = e->getStringAttribute ("label");
            const auto d = base64ToDoubles (e->getAllSubText().trim());
            for (size_t k = 0; k + 1 < d.size(); k += 2) a.points.push_back ({ (float) d[k], (float) d[k + 1] });
            song.autos.push_back (a);
        }
    if (! inserts[0].exists()) ensureInsert (0);
    // (always: what leaves an insert leaves from its strip's end since 6.2 - and 6.0's single target became sends)
    juce::ignoreUnused (converted);
    rewireAll();
    // tracks that only a send dragged past in 6.1 brought into being: nothing
    // uses them, nothing was set on them - they go back to unused
    for (int t = 1; t < kSlots; ++t)
        if (inserts[(size_t) t].exists() && ! isInsertUsed (t) && slots[(size_t) t].name.isEmpty()
            && std::abs (getVolume (t) - 0.8f) < 1.0e-4f && std::abs (getPan (t)) < 1.0e-4f && ! isInverted (t))
            releaseIfUnused (t);
    setSplit (xml->getStringAttribute ("split"), xml->getIntAttribute ("splitPart", FruitEditor::RackPart));
    knobSnapshot.clear();
    songChanged();
}
