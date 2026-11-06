#include "TbWaveBank.h"

namespace tb
{
namespace
{
    // rendered waves, shared by every bank in the process: by recipe (a
    // factory wave's name, a rolled wave's archetype and seed, a loaded
    // wave's audio and settings), held weakly, with the last few recipes kept
    // alive so going back to a program is instant (loaded waves are not kept:
    // they can be large, and making them again is quick)
    struct SharedWaves
    {
        std::mutex m;
        std::map<juce::String, std::weak_ptr<const Wave>> byKey;
        std::deque<WavePtr> recent;
        WavePtr get (const juce::String& key, const std::function<WavePtr()>& make, bool keep)
        {
            {
                std::lock_guard<std::mutex> g (m);
                auto it = byKey.find (key);
                if (it != byKey.end()) if (auto w = it->second.lock()) return w;
            }
            WavePtr w = make();
            std::lock_guard<std::mutex> g (m);
            for (auto it = byKey.begin(); it != byKey.end();)   // forget the expired ones
                it = it->second.expired() ? byKey.erase (it) : std::next (it);
            byKey[key] = w;
            if (keep)
            {
                recent.push_front (w);
                while (recent.size() > 10) recent.pop_back();
            }
            return w;
        }
    };
    SharedWaves& sharedWaves() { static SharedWaves s; return s; }

    juce::String sampleKey (const SampleData& d, const SampleSettings& s)
    {
        return "s:" + d.key + ":" + juce::String (s.rootNote) + ":" + juce::String (s.loopMode) + ":"
             + juce::String (s.loopStart, 5) + ":" + juce::String (s.xfadeMs, 2);
    }
}

class WaveBank::Thread : public juce::Thread
{
public:
    explicit Thread (WaveBank& b) : juce::Thread ("Tidebreeze waves"), bank (b) {}
    juce::WaitableEvent wake;
    void run() override
    {
        while (! threadShouldExit())
        {
            Job job; bool have = false;
            {
                const juce::ScopedLock sl (bank.jobLock);
                if (! bank.jobs.empty()) { job = bank.jobs.front(); bank.jobs.erase (bank.jobs.begin()); have = true; }
            }
            if (! have) { wake.wait (250); continue; }
            bank.runJob (job);
        }
    }
private:
    WaveBank& bank;
};

WaveBank::WaveBank()
{
    liveWaves.resize ((size_t) numWaveChoices());
    queued.assign ((size_t) numWaveChoices(), false);
}

WaveBank::~WaveBank()
{
    if (thread != nullptr)
    {
        thread->signalThreadShouldExit();
        thread->wake.signal();
        thread->stopThread (8000);
    }
}

//==============================================================================
juce::String WaveBank::waveName (int choice) const
{
    const int nf = numFactoryWaves();
    if (choice < nf) return factoryWaves()[(size_t) juce::jmax (0, choice)].name;
    const int s = choice - nf;
    if (s < kUserWaveSlots && slots[(size_t) s].used()) return slots[(size_t) s].name;
    return "User " + juce::String (s + 1) + " (empty)";
}

juce::String WaveBank::groupName (int choice) const
{
    if (choice < numFactoryWaves()) return factoryWaves()[(size_t) juce::jmax (0, choice)].group;
    const auto& s = slot (slotOf (choice));
    return s.kind == Slot::Rolled ? "Rolled" : (s.kind == Slot::Sample ? "Sample" : "");
}

int WaveBank::nextSlot()
{
    for (int i = 0; i < kUserWaveSlots; ++i) if (! slots[(size_t) i].used()) return i;
    const int s = nextUserSlot;
    nextUserSlot = (nextUserSlot + 1) % kUserWaveSlots;
    return s;
}

void WaveBank::slotChanged (int s, bool keepOldWave)
{
    const int choice = choiceOf (s);
    {
        const juce::ScopedLock jl (jobLock);
        ++userGen[(size_t) s];
        queued[(size_t) choice] = false;          // a render of the old one no longer counts
        if (! keepOldWave)
        {
            const juce::SpinLock::ScopedLockType wl (waveLock);
            if (liveWaves[(size_t) choice] != nullptr) retired.push_back (std::move (liveWaves[(size_t) choice]));
            liveWaves[(size_t) choice] = nullptr;
        }
    }
    ++version;
    changed = true;
}

int WaveBank::rollWave (int archetype, juce::uint32 seed, int s)
{
    if (seed == 0) seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff) | 1u;
    if (s < 0 || s >= kUserWaveSlots) s = nextSlot();
    auto& u = slots[(size_t) s];
    u = Slot();
    u.kind = Slot::Rolled; u.archetype = archetype; u.seed = seed;
    u.name = juce::String (archetypeName (archetype)) + " " + juce::String ((int) (seed % 1000u));
    slotChanged (s, false);
    requestWave (choiceOf (s));
    return choiceOf (s);
}

int WaveBank::setSample (SamplePtr sample, const SampleSettings& settings, const juce::String& name, int s)
{
    if (sample == nullptr) return -1;
    if (s < 0 || s >= kUserWaveSlots) s = nextSlot();
    auto& u = slots[(size_t) s];
    u = Slot();
    u.kind = Slot::Sample; u.sample = std::move (sample); u.settings = settings;
    u.name = name.isNotEmpty() ? name : juce::String ("Sample");
    slotChanged (s, false);
    requestWave (choiceOf (s));
    return choiceOf (s);
}

int WaveBank::loadFile (const juce::File& file, int s, juce::String& error)
{
    auto d = readSampleFile (file, kMaxSampleSeconds, error);
    if (d == nullptr) return -1;
    const auto settings = guessSettings (*d);
    const auto name = d->fileName.substring (0, 28);
    return setSample (std::move (d), settings, name, s);
}

void WaveBank::setSampleSettings (int s, const SampleSettings& settings)
{
    if (s < 0 || s >= kUserWaveSlots) return;
    auto& u = slots[(size_t) s];
    if (u.kind != Slot::Sample || u.settings == settings) return;
    u.settings = settings;
    // the old wave plays on until the new one is there
    slotChanged (s, true);
    queue (choiceOf (s), true);
}

void WaveBank::clearSlot (int s)
{
    if (s < 0 || s >= kUserWaveSlots) return;
    slots[(size_t) s] = Slot();
    slotChanged (s, false);
}

void WaveBank::clearAll()
{
    for (int s = 0; s < kUserWaveSlots; ++s) { slots[(size_t) s] = Slot(); slotChanged (s, false); }
    nextUserSlot = 0;
}

void WaveBank::copyFrom (const WaveBank& other, const std::function<bool (int)>& include)
{
    for (int s = 0; s < kUserWaveSlots; ++s)
    {
        slots[(size_t) s] = (include == nullptr || include (s)) ? other.slots[(size_t) s] : Slot();
        slotChanged (s, false);
    }
    nextUserSlot = other.nextUserSlot;
}

//==============================================================================
void WaveBank::runJob (const Job& job)
{
    const bool user = job.kind != Slot::Empty;
    bool current = true;
    if (user)
    {
        const juce::ScopedLock sl (jobLock);
        current = userGen[(size_t) slotOf (job.choice)] == job.gen;
    }
    WavePtr w;
    if (current)   // a job the slot has moved on from is not worth rendering
    {
        if (! user)
        {
            const auto& fw = factoryWaves()[(size_t) job.choice];
            w = sharedWaves().get ("f:" + juce::String (fw.name), [&fw] { return render (fw.make()); }, true);
        }
        else if (job.kind == Slot::Rolled)
        {
            const int arch = job.archetype; const juce::uint32 seed = job.seed;
            w = sharedWaves().get ("u:" + juce::String (arch) + ":" + juce::String ((juce::int64) seed),
                                   [arch, seed] { return render (roll (arch, seed)); }, true);
        }
        else if (job.sample != nullptr)
        {
            const auto sample = job.sample; const auto settings = job.settings;
            w = sharedWaves().get (sampleKey (*sample, settings), [sample, settings] { return fromSample (*sample, settings); }, false);
        }
    }
    {
        const juce::ScopedLock sl (jobLock);
        const bool stillCurrent = ! user || userGen[(size_t) slotOf (job.choice)] == job.gen;
        if (stillCurrent)
        {
            queued[(size_t) job.choice] = false;
            if (w != nullptr)
            {
                WavePtr old;
                {
                    const juce::SpinLock::ScopedLockType wl (waveLock);
                    old = std::move (liveWaves[(size_t) job.choice]);
                    liveWaves[(size_t) job.choice] = w;
                }
                if (old != nullptr) retired.push_back (std::move (old));
            }
        }
    }
    --jobsInFlight;
    changed = true;
}

void WaveBank::queue (int choice, bool evenIfThere)
{
    if (choice < 0 || choice >= numWaveChoices()) return;
    if (! evenIfThere)
    {
        const juce::SpinLock::ScopedLockType wl (waveLock);
        if (liveWaves[(size_t) choice] != nullptr) return;
    }
    Job job; job.choice = choice;
    if (isUser (choice))
    {
        const auto& u = slots[(size_t) slotOf (choice)];
        if (! u.used()) return;
        job.kind = u.kind; job.archetype = u.archetype; job.seed = u.seed;
        job.sample = u.sample; job.settings = u.settings;
    }
    const juce::ScopedLock sl (jobLock);
    if (queued[(size_t) choice]) return;
    if (job.kind != Slot::Empty) job.gen = userGen[(size_t) slotOf (choice)];
    queued[(size_t) choice] = true;
    ++jobsInFlight;
    jobs.push_back (job);
    if (thread == nullptr)
    {
        thread = std::make_unique<Thread> (*this);
        thread->startThread (juce::Thread::Priority::low);
    }
    thread->wake.signal();
}

void WaveBank::requestWave (int choice) { queue (choice, false); }

WavePtr WaveBank::peekWave (int choice) const
{
    if (choice < 0 || choice >= numWaveChoices()) return nullptr;
    const juce::SpinLock::ScopedLockType wl (waveLock);
    return liveWaves[(size_t) choice];
}

WavePtr WaveBank::tryWave (int choice) const
{
    if (choice < 0 || choice >= numWaveChoices()) return nullptr;
    const juce::SpinLock::ScopedTryLockType wl (waveLock);
    return wl.isLocked() ? liveWaves[(size_t) choice] : nullptr;
}

void WaveBank::ensure (const std::vector<bool>& used)
{
    {
        const juce::ScopedLock jl (jobLock);
        retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const WavePtr& w) { return w.use_count() <= 1; }), retired.end());
        const juce::SpinLock::ScopedLockType wl (waveLock);
        for (int i = 0; i < numWaveChoices(); ++i)
            if ((i >= (int) used.size() || ! used[(size_t) i]) && liveWaves[(size_t) i] != nullptr)
            {
                retired.push_back (std::move (liveWaves[(size_t) i]));
                liveWaves[(size_t) i] = nullptr;
            }
    }
    for (int i = 0; i < (int) used.size() && i < numWaveChoices(); ++i) if (used[(size_t) i]) requestWave (i);
}

bool WaveBank::waitFor (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (pending())
    {
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::Thread::sleep (2);
    }
    return true;
}

//==============================================================================
void WaveBank::writeXml (juce::XmlElement& parent, const std::function<bool (int)>& include) const
{
    auto* uw = parent.createNewChildElement ("UserWaves");
    for (int i = 0; i < kUserWaveSlots; ++i)
    {
        const auto& u = slots[(size_t) i];
        if (! u.used() || (include != nullptr && ! include (i))) continue;
        auto* e = uw->createNewChildElement ("Wave");
        e->setAttribute ("slot", i);
        e->setAttribute ("name", u.name);
        if (u.kind == Slot::Rolled)
        {
            e->setAttribute ("archetype", u.archetype);
            e->setAttribute ("seed", juce::String ((juce::uint64) u.seed));
        }
        else if (u.sample != nullptr)
        {
            e->setAttribute ("kind", "sample");
            e->setAttribute ("root", u.settings.rootNote);
            e->setAttribute ("loop", u.settings.loopMode);
            e->setAttribute ("loopStart", (double) u.settings.loopStart);
            e->setAttribute ("xfade", (double) u.settings.xfadeMs);
            e->setAttribute ("file", u.sample->fileName);
            e->createNewChildElement ("Audio")->addTextElement (u.sample->encoded.toBase64Encoding());
        }
    }
}

void WaveBank::readXml (const juce::XmlElement* uw)
{
    std::array<Slot, kUserWaveSlots> fresh;
    if (uw != nullptr)
        for (auto* e : uw->getChildIterator())
        {
            const int s = e->getIntAttribute ("slot", -1);
            if (s < 0 || s >= kUserWaveSlots) continue;
            auto& u = fresh[(size_t) s];
            u.name = e->getStringAttribute ("name");
            if (e->getStringAttribute ("kind") == "sample")
            {
                juce::MemoryBlock mb;
                const auto* audio = e->getChildByName ("Audio");
                if (audio == nullptr || ! mb.fromBase64Encoding (audio->getAllSubText().trim())) continue;
                auto d = decodeSample (mb, e->getStringAttribute ("file", u.name));
                if (d == nullptr) continue;
                u.kind = Slot::Sample;
                u.sample = std::move (d);
                u.settings.rootNote = juce::jlimit (0, 127, e->getIntAttribute ("root", 60));
                u.settings.loopMode = juce::jlimit (0, NumLoopModes - 1, e->getIntAttribute ("loop", LoopSustain));
                u.settings.loopStart = (float) juce::jlimit (0.0, 0.98, e->getDoubleAttribute ("loopStart", 0.5));
                u.settings.xfadeMs = (float) juce::jlimit (0.0, 1000.0, e->getDoubleAttribute ("xfade", 80.0));
            }
            else
            {
                u.kind = Slot::Rolled;
                u.archetype = juce::jlimit (0, NumArchetypes - 1, e->getIntAttribute ("archetype"));
                u.seed = (juce::uint32) e->getStringAttribute ("seed").getLargeIntValue();
            }
            if (u.name.isEmpty()) u.name = "User " + juce::String (s + 1);
        }
    for (int s = 0; s < kUserWaveSlots; ++s) { slots[(size_t) s] = fresh[(size_t) s]; slotChanged (s, false); }
    nextUserSlot = 0;
}
}
