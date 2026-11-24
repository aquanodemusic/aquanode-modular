//==============================================================================
// Fruit Arranger (6.7.12) - the playlist of a song made in the patch itself.
//
// Every piano roll of the patch, with the synths it plays and everything
// after them, is a pattern of the song, numbered in the arranger's sidebar
// (its roll's card says "Arranger n" too). Pills of the patterns, of knob
// automation and of samples go on any track: tap one in the sidebar (or play
// with its piano roll, or tap a pill) to highlight it, then tap an empty spot
// to place another of it. Drag a pill to move it, its right end to make it
// longer (a pattern loops inside), tap it twice to delete it.
//
// Clean only: a piano roll's notes go to its own synths alone, and nothing of
// one pattern - no module, no sidechain, no cable - reaches another's; sound
// from outside the patterns (an oscillator of its own, the Audio In) may not
// come in either, while modulators may (an LFO, a Macro, Automation Lanes,
// even shared). The arranger checks the patch after every change of modules
// or cables; what fails is crossed out and stays silent until it is put
// right. New clean piano rolls join by themselves; Rescan also moves their
// cables into the Audio Out over to it (and places their first pill).
//
// Modulate Parameter (as Fruit DAW's Knob Record): turn any knob in the patch,
// press it, and a new track with an automation pill for that knob appears -
// draw its curve below the tracks. Load Sample: audio pills, played at their
// own speed, kept with the patch.
//
// It plays with its own Play / Stop, or with the host's transport where one
// plays. Stopped, every piano roll is its own again (its Play button works).
// Lanes In takes the patterns' sound (lined up: inside its patterns latency
// compensation is always on); Audio Out gives it on, with the samples.
// Global. In: Lanes In. Out: Audio Out (and hidden links to rolls and knobs).
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/LookPalette.h"
#include "../../../Engine/PluginProcessor.h"
#include "../../../Platform/AndroidDocuments.h"
#include "../../../Plugins/Host/PluginCatalog.h"
#include "../../SoundGeneration/SamplesAndGrains/SamplerModule.h"
#include "PianoRollModule.h"
#include "FruitArrangerLinks.h"
#include <map>
#include <set>

using namespace aquanode;

namespace fruitarr
{
    constexpr int kRolls = 16, kAutos = 16, kMaxTracks = 99, kMaxActive = 128;
    constexpr double kRate = 48000.0;              // samples are kept at this rate
    constexpr double kMaxSampleSeconds = 600.0;    // all of them together
    enum class Kind : int { Pattern = 0, Auto = 1, Sample = 2 };

    struct Pill { Kind kind { Kind::Pattern }; int item { 1 }; int track { 0 }; double start { 0.0 }, length { 16.0 }; };
    struct PatternItem { int number { 1 }; int rollId { -1 }; juce::String name { "Piano Roll" }; bool valid { true }; juce::String problem; double loopBeats { 16.0 }; };
    struct AutoItem { int number { 1 }; int moduleId { -1 }; juce::String paramId, label; std::vector<juce::Point<float>> points; bool valid { true }; juce::String problem; };
    struct SampleItem { int number { 1 }; juce::String name; int offset { 0 }, length { 0 }; };
    struct Track { bool mute { false }; };

    // an automation curve: a spline through its points (x 0..1 across the pill, y 0..1 of the knob)
    inline float curveAt (const std::vector<juce::Point<float>>& p, float x)
    {
        if (p.empty()) return 0.5f;
        if (x <= p.front().x) return p.front().y;
        if (x >= p.back().x) return p.back().y;
        size_t i = 0;
        while (i + 2 < p.size() && p[i + 1].x < x) ++i;
        const auto& a = p[i];
        const auto& b = p[i + 1];
        const auto& a0 = i > 0 ? p[i - 1] : a;
        const auto& b1 = i + 2 < p.size() ? p[i + 2] : b;
        const float t = juce::jlimit (0.0f, 1.0f, (x - a.x) / juce::jmax (1.0e-6f, b.x - a.x)), t2 = t * t, t3 = t2 * t;
        const float v = 0.5f * (2.0f * a.y + (-a0.y + b.y) * t + (2.0f * a0.y - 5.0f * a.y + 4.0f * b.y - b1.y) * t2
                                + (-a0.y + 3.0f * a.y - 3.0f * b.y + b1.y) * t3);
        return juce::jlimit (0.0f, 1.0f, v);
    }

    // what the audio thread plays: a copy made on every change
    struct Song
    {
        std::vector<Pill> pills;                                  // valid items on unmuted tracks, by start
        std::array<double, kRolls + 1> loopBeats {};
        std::array<std::vector<juce::Point<float>>, kAutos + 1> curves;
        std::vector<SampleItem> samples;                          // by number - 1
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double endBeats { 16.0 };
    };

    inline juce::Colour patternColour (int number) { return juce::Colour::fromHSV (std::fmod (0.47f + 0.137f * (float) number, 1.0f), 0.55f, 0.92f, 1.0f); }
    const juce::Colour kAutoColour (0xffff9f6b), kSampleColour (0xff5fd6c4), kBad (0xffff5a5a);

    // the Fruit look (6.9.1): Fruit DAW's playlist colours (Plugins/FruitGUI/FruitEditor.h)
    namespace fruitlook
    {
        const juce::Colour panel { 0xff628f88 }, toolbar { 0xff34635b }, nameBtn { 0xff8ab2b2 }, light { 0xffe1f5f3 },
                           grid { 0xff136358 }, gridAlt { 0xff12655b }, gridLine { 0xff1a6358 }, trackHead { 0xff406b61 },
                           patternList { 0xff12504d }, clipBody { 0xff1c554e }, clipHead { 0xff3c6a6a }, loopRed { 0xff955e61 },
                           mixerBg { 0xff254d45 }, selected { 0xff40f0ee }, darkText { 0xff10302c },
                           sampleBody { 0xff1d4a5c }, sampleHead { 0xff2f7f96 }, wave { 0xff9fe6f2 };
    }
}

using namespace fruitarr;

//==============================================================================
class FruitArrangerModule final : public SynthModule
{
public:
    enum P { pPlaying = 0, pLoop, pViewW, pViewH };
    static constexpr int kDefaultW = 780, kMinW = 520, kMaxW = 1800, kDefaultH = 400, kMinH = 300, kMaxH = 1200;

    FruitArrangerModule() { tracks.resize (8); publish(); }

    //=== audio =================================================================
    bool drivesKnobsAbsolutely() const override { return true; }
    bool drivesKnobsNow() const override { return playingNow.load (std::memory_order_relaxed); }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { refreshNeeded = true; jumped = true; }

    void blockStart() override
    {
        {
            const juce::SpinLock::ScopedTryLockType sl (songLock);
            if (sl.isLocked() && published != audioSong) { audioSong = published; refreshNeeded = true; }
        }
        bpm = tempoBpm > 1.0 ? tempoBpm : 120.0;
        bps = bpm / 60.0 / juce::jmax (1000.0, sampleRate);
        hostPlays = false;
        if (hostPlayHead != nullptr)
            if (auto pos = hostPlayHead->getPosition())
                if (pos->getIsPlaying())
                    if (auto ppq = pos->getPpqPosition())
                    {
                        hostPlays = true;
                        if (std::abs (*ppq - beat) > 0.05 || ! wasPlaying) { beat = juce::jmax (0.0, *ppq); refreshNeeded = jumped = true; }
                    }
        const double seek = seekRequest.exchange (-1.0, std::memory_order_acq_rel);
        if (seek >= 0.0 && ! hostPlays) { beat = seek; refreshNeeded = jumped = true; }
        playing = hostPlays || param (pPlaying) > 0.5f;
        if (playing && ! wasPlaying) refreshNeeded = jumped = true;
        wasPlaying = playing;
        playingNow.store (playing, std::memory_order_relaxed);
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        StereoFrame mix = inputs[0];
        auto idle = [&]
        {
            for (int k = 0; k < kRolls; ++k) outputs[1 + k] = { 0.0f, 0.0f };   // (stopped: each roll is its own)
            for (int k = 0; k < kAutos; ++k) outputs[1 + kRolls + k] = { held[k], held[k] };
            outputs[0] = mix;
            uiBeat.store (beat, std::memory_order_relaxed);
        };
        if (! playing || audioSong == nullptr) { idle(); return; }
        const auto& song = *audioSong;
        if (! hostPlays && beat >= song.endBeats)
        {
            if (param (pLoop) < 0.5f) { beat = std::fmod (beat, song.endBeats); refreshNeeded = jumped = true; }
            else
            {
                setParameterByIndex (pPlaying, 0.0f);   // Once: stop at the end, back to the start
                playing = wasPlaying = false;
                playingNow.store (false, std::memory_order_relaxed);
                beat = 0.0;
                idle();
                return;
            }
        }
        const double next = beat + 2.0 * bps;   // (a piano roll hears its Song In two samples late: the link is read a sample late, the roll acts on it the next)
        if (refreshNeeded || next >= activeUntil || beat < activeFrom)
            refreshActive (song, beat);

        // to each roll: 2 + where in its pattern, or 1 for silence (0 is "stopped")
        float rollV[kRolls];
        std::fill (rollV, rollV + kRolls, 1.0f);
        for (int a = 0; a < numActive; ++a)
        {
            const auto& p = song.pills[(size_t) active[a]];
            const double end = p.start + p.length;
            if (p.kind == Kind::Pattern)
            {
                if (next >= p.start && next < end && p.item >= 1 && p.item <= kRolls)
                    rollV[p.item - 1] = 2.0f + (float) std::fmod (next - p.start, juce::jmax (0.25, song.loopBeats[(size_t) p.item]));
            }
            else if (p.kind == Kind::Auto)
            {
                if (beat >= p.start && beat < end && p.item >= 1 && p.item <= kAutos)
                    held[p.item - 1] = curveAt (song.curves[(size_t) p.item], (float) ((beat - p.start) / juce::jmax (1.0e-6, p.length)));
            }
            else if (beat >= p.start && beat < end && song.audio != nullptr && p.item >= 1 && p.item <= (int) song.samples.size())
            {
                const auto& si = song.samples[(size_t) p.item - 1];
                const double t = (beat - p.start) * 60.0 / bpm;        // seconds into the pill
                const double pos = t * kRate;
                if (pos < (double) si.length - 1.0)
                {
                    const int i0 = (int) pos;
                    const float fr = (float) (pos - i0);
                    const int at = si.offset + i0;
                    if (at + 1 < song.audio->getNumSamples())
                    {
                        const float* L = song.audio->getReadPointer (0);
                        const float* R = song.audio->getReadPointer (juce::jmin (1, song.audio->getNumChannels() - 1));
                        const double left = juce::jmin ((end - beat) * 60.0 / bpm, ((double) si.length - 1.0 - pos) / kRate);
                        const float g = (float) juce::jmin (1.0, t / 0.003, left / 0.006);   // (no clicks at either end)
                        mix[0] += (L[at] + (L[at + 1] - L[at]) * fr) * g;
                        mix[1] += (R[at] + (R[at + 1] - R[at]) * fr) * g;
                    }
                }
            }
        }
        for (int k = 0; k < kRolls; ++k) outputs[1 + k] = { rollV[k], rollV[k] };
        for (int k = 0; k < kAutos; ++k) outputs[1 + kRolls + k] = { held[k], held[k] };
        outputs[0] = mix;
        beat += bps;
        uiBeat.store (beat, std::memory_order_relaxed);
    }

    //=== the model (message thread) ============================================
    std::vector<PatternItem> patterns;
    std::vector<AutoItem> autos;
    std::vector<SampleItem> samples;
    std::vector<Track> tracks;
    std::vector<Pill> pills;
    juce::StringArray skipped;                 // piano rolls left out, and why
    std::set<int> laneModules;                 // everything in the clean patterns (last check)
    std::set<int> dismissed;                   // rolls taken off by hand: they rejoin with Rescan
    std::shared_ptr<juce::AudioBuffer<float>> sampleAudio;
    AquanodeModularAudioProcessor* owner { nullptr };
    int selfId { -1 };
    int version { 0 };                         // the card redraws when this moves
    float zoom { 10.0f };                      // the card's view: pixels a beat, where it is
    double scrollBeats { 0.0 };
    int scrollTrack { 0 };
    std::atomic<double> uiBeat { 0.0 };
    std::atomic<double> seekRequest { -1.0 };
    std::atomic<bool> playingNow { false };

    bool isPlayingNow() const { return playingNow.load (std::memory_order_relaxed); }
    double tempo() const { return tempoBpm > 1.0 ? tempoBpm : 120.0; }
    void setPlaying (bool on) { setParameter ("playing", on ? 1.0f : 0.0f); }
    void seek (double b) { seekRequest.store (juce::jmax (0.0, b), std::memory_order_release); uiBeat.store (juce::jmax (0.0, b)); }

    PatternItem* pattern (int n) { for (auto& p : patterns) if (p.number == n) return &p; return nullptr; }
    AutoItem* automation (int n) { for (auto& a : autos) if (a.number == n) return &a; return nullptr; }
    const SampleItem* sample (int n) const { return n >= 1 && n <= (int) samples.size() ? &samples[(size_t) n - 1] : nullptr; }

    double songEnd() const
    {
        double e = 0.0;
        for (const auto& p : pills) e = juce::jmax (e, p.start + p.length);
        return juce::jmax (16.0, std::ceil (e / 4.0 - 1.0e-9) * 4.0);
    }
    bool hasPills (Kind k, int item) const
    {
        for (const auto& p : pills) if (p.kind == k && p.item == item) return true;
        return false;
    }
    bool itemValid (Kind k, int item) const
    {
        if (k == Kind::Pattern) { for (const auto& p : patterns) if (p.number == item) return p.valid; return false; }
        if (k == Kind::Auto)    { for (const auto& a : autos) if (a.number == item) return a.valid; return false; }
        return item >= 1 && item <= (int) samples.size();
    }
    // a new pill's length: the pattern's loop, the automation's last pill, the sample's own
    double defaultLength (Kind k, int item) const
    {
        if (k == Kind::Pattern) { for (const auto& p : patterns) if (p.number == item) return juce::jmax (1.0, p.loopBeats); return 16.0; }
        if (k == Kind::Auto) { double len = 16.0; for (const auto& p : pills) if (p.kind == k && p.item == item) len = p.length; return len; }
        if (const auto* s = sample (item)) return juce::jmax (1.0, std::ceil ((double) s->length / kRate * (tempoBpm > 1.0 ? tempoBpm : 120.0) / 60.0));
        return 16.0;
    }
    void growTracks()
    {
        int used = 0;
        for (const auto& p : pills) used = juce::jmax (used, p.track + 1);
        const int want = juce::jlimit (8, kMaxTracks, juce::jmax (used + 2, (int) patterns.size() + (int) autos.size() + 2));
        if ((int) tracks.size() < want) tracks.resize ((size_t) want);
    }
    int firstEmptyTrack() const
    {
        for (int t = 0; t < kMaxTracks; ++t)
        {
            bool used = false;
            for (const auto& p : pills) used = used || p.track == t;
            if (! used) return t;
        }
        return 0;
    }
    int placePill (Kind k, int item, int track, double start, double length = -1.0)
    {
        Pill p;
        p.kind = k; p.item = item;
        p.track = juce::jlimit (0, kMaxTracks - 1, track);
        p.start = juce::jmax (0.0, start);
        p.length = length > 0.0 ? length : defaultLength (k, item);
        pills.push_back (p);
        growTracks();
        publish();
        return (int) pills.size() - 1;
    }
    void removePill (int index)
    {
        if (index < 0 || index >= (int) pills.size()) return;
        pills.erase (pills.begin() + index);
        publish();
    }
    // an item off the arranger, with its pills (a piano roll stays off until Rescan)
    void removeItem (Kind k, int item)
    {
        pills.erase (std::remove_if (pills.begin(), pills.end(), [&] (const Pill& p) { return p.kind == k && p.item == item; }), pills.end());
        if (k == Kind::Pattern)
        {
            for (auto it = patterns.begin(); it != patterns.end(); ++it)
                if (it->number == item) { dismissed.insert (it->rollId); patterns.erase (it); break; }
        }
        else if (k == Kind::Auto)
            autos.erase (std::remove_if (autos.begin(), autos.end(), [&] (const AutoItem& a) { return a.number == item; }), autos.end());
        else if (const auto* s = sample (item))
        {
            // its audio out of the shared buffer, the later ones moved up
            const int off = s->offset, len = s->length;
            if (sampleAudio != nullptr && len > 0)
            {
                auto shorter = std::make_shared<juce::AudioBuffer<float>> (sampleAudio->getNumChannels(), juce::jmax (1, sampleAudio->getNumSamples() - len));
                shorter->clear();
                for (int c = 0; c < sampleAudio->getNumChannels(); ++c)
                {
                    shorter->copyFrom (c, 0, *sampleAudio, c, 0, off);
                    const int rest = sampleAudio->getNumSamples() - off - len;
                    if (rest > 0) shorter->copyFrom (c, off, *sampleAudio, c, off + len, rest);
                }
                sampleAudio = shorter;
            }
            samples.erase (samples.begin() + (item - 1));
            for (auto& o : samples) { if (o.offset > off) o.offset -= len; }
            for (int i = 0; i < (int) samples.size(); ++i) samples[(size_t) i].number = i + 1;
            for (auto& p : pills) if (p.kind == Kind::Sample && p.item > item) --p.item;
        }
        publish();
    }

    // Modulate Parameter: a knob of the patch on an automation lane of its own (false: all 16 in use)
    bool addAutomation (int moduleId, int paramIndex, const juce::String& label, double at)
    {
        if (owner == nullptr) return false;
        auto* inst = owner->getInstance (moduleId);
        if (inst == nullptr || paramIndex < 0 || paramIndex >= (int) inst->descriptor().params.size()) return false;
        int n = 0;
        for (int k = 1; k <= kAutos && n == 0; ++k) if (automation (k) == nullptr) n = k;
        if (n == 0) return false;
        const auto& spec = inst->descriptor().params[(size_t) paramIndex];
        const float v = juce::jlimit (0.0f, 1.0f, (inst->dsp->getParameterBase (paramIndex) - spec.minValue) / juce::jmax (1.0e-9f, spec.maxValue - spec.minValue));
        AutoItem a;
        a.number = n; a.moduleId = moduleId; a.paramId = spec.id; a.label = label;
        a.points = { { 0.0f, v }, { 1.0f, v } };
        autos.push_back (a);
        std::sort (autos.begin(), autos.end(), [] (const AutoItem& x, const AutoItem& y) { return x.number < y.number; });
        placePill (Kind::Auto, n, firstEmptyTrack(), std::floor (at / 4.0) * 4.0, 16.0);
        return true;
    }

    // Load Sample: a sound as a sample item (already at kRate); false: too long together
    bool addSample (const juce::String& name, const juce::AudioBuffer<float>& audio)
    {
        const int before = sampleAudio != nullptr ? sampleAudio->getNumSamples() : 0;
        if (audio.getNumSamples() < 2 || (double) (before + audio.getNumSamples()) > kMaxSampleSeconds * kRate) return false;
        auto longer = std::make_shared<juce::AudioBuffer<float>> (2, before + audio.getNumSamples());
        for (int c = 0; c < 2; ++c)
        {
            if (before > 0) longer->copyFrom (c, 0, *sampleAudio, juce::jmin (c, sampleAudio->getNumChannels() - 1), 0, before);
            longer->copyFrom (c, before, audio, juce::jmin (c, audio.getNumChannels() - 1), 0, audio.getNumSamples());
        }
        sampleAudio = longer;
        SampleItem s;
        s.number = (int) samples.size() + 1;
        s.name = name.isNotEmpty() ? name : "Sample " + juce::String (s.number);
        s.offset = before;
        s.length = audio.getNumSamples();
        samples.push_back (s);
        publish();
        return true;
    }

    // the audio thread's copy, made anew (message thread)
    void publish()
    {
        auto song = std::make_shared<Song>();
        for (const auto& p : patterns) song->loopBeats[(size_t) juce::jlimit (0, kRolls, p.number)] = p.loopBeats;
        for (const auto& a : autos) song->curves[(size_t) juce::jlimit (0, kAutos, a.number)] = a.points;
        song->samples = samples;
        song->audio = sampleAudio;
        for (const auto& p : pills)
        {
            if (p.track >= 0 && p.track < (int) tracks.size() && tracks[(size_t) p.track].mute) continue;
            if (! itemValid (p.kind, p.item)) continue;
            song->pills.push_back (p);
        }
        std::stable_sort (song->pills.begin(), song->pills.end(), [] (const Pill& a, const Pill& b) { return a.start < b.start; });
        song->endBeats = songEnd();
        std::shared_ptr<const Song> old;
        {
            const juce::SpinLock::ScopedLockType sl (songLock);
            old = published;
            published = song;
        }
        if (old != nullptr) retired.push_back (old);
        // (freed here, never on the audio thread: only once the audio thread has let go)
        retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const std::shared_ptr<const Song>& s) { return s.use_count() == 1; }),
                       retired.end());
        ++version;
        // an automation that got its first playing pill, or lost its last: its knob linked anew
        if (! inSync && owner != nullptr && playableAutos() != linkedAutos)
            owner->routingChanged();
    }

    //=== the check (message thread, every graph build) ==========================
    void sync (AquanodeModularAudioProcessor& proc, int myId, FruitArrangerLinks& links, std::set<int>& claimed)
    {
        owner = &proc;
        selfId = myId;
        const auto ids = proc.getModuleIds();
        auto typeOf = [&proc] (int id) -> juce::String { auto* i = proc.getInstance (id); return i != nullptr ? i->descriptor().typeId : juce::String(); };
        auto nameOf = [&proc] (int id) -> juce::String { auto* i = proc.getInstance (id); return i != nullptr ? i->descriptor().displayName : juce::String ("?"); };

        // the patch as a graph: what each module feeds, and what feeds it (with the kind of signal)
        std::map<int, std::vector<int>> down;
        struct In { int from; SocketKind kind; };
        std::map<int, std::vector<In>> ins;
        for (const auto& c : proc.getCables())
        {
            auto* from = proc.getInstance (c.fromModule);
            if (from == nullptr || proc.getInstance (c.toModule) == nullptr) continue;
            const int si = from->descriptor().outputIndexOf (c.fromSocket);
            const auto kind = si >= 0 ? from->descriptor().outputs()[(size_t) si]->kind : SocketKind::Audio;
            down[c.fromModule].push_back (c.toModule);
            ins[c.toModule].push_back ({ c.fromModule, kind });
        }
        for (const auto& pc : proc.getParamCables())
        {
            if (proc.getInstance (pc.fromModule) == nullptr || proc.getInstance (pc.toModule) == nullptr) continue;
            down[pc.fromModule].push_back (pc.toModule);
            ins[pc.toModule].push_back ({ pc.fromModule, SocketKind::Modulation });
        }
        auto stops = [&] (int id) { const auto t = typeOf (id); return t == "io.audioOut" || t == "util.fruitarranger"; };

        // every piano roll's region: all it reaches, up to the Audio Out (and any arranger)
        std::vector<int> rolls;
        for (int id : ids) if (typeOf (id) == "util.pianoroll") rolls.push_back (id);
        std::map<int, std::set<int>> region;
        std::map<int, std::vector<int>> rollsOf;   // module -> the rolls whose region holds it
        for (int r : rolls)
        {
            auto& reg = region[r];
            std::vector<int> todo { r };
            while (! todo.empty())
            {
                const int at = todo.back();
                todo.pop_back();
                for (int d : down[at])
                    if (d != r && ! stops (d) && reg.insert (d).second)
                        todo.push_back (d);
            }
            for (int m : reg) rollsOf[m].push_back (r);
        }
        // (6.9.14) only the lanes whose sound goes into this arranger's Lanes In belong to it
        // (Add and Rescan first move a clean lane's cables from the Audio Out over to it)
        std::set<int> intoLanes;
        for (const auto& c : proc.getCables())
            if (c.toModule == myId && c.toSocket == "lanesIn") intoLanes.insert (c.fromModule);
        auto reachesLanes = [&] (int r)
        {
            if (adopting || intoLanes.count (r) > 0) return true;
            for (int m : region[r]) if (intoLanes.count (m) > 0) return true;
            return false;
        };
        auto rollLabel = [&] (int r) -> juce::String
        {
            for (const auto& p : patterns) if (p.rollId == r) return "Piano Roll " + juce::String (p.number);
            return "a Piano Roll";
        };
        // why a roll is not clean (empty: clean)
        auto problemOf = [&] (int r) -> juce::String
        {
            const auto& reg = region[r];
            for (int m : reg)
            {
                if (rollsOf[m].size() > 1)
                {
                    const int other = rollsOf[m][0] == r ? rollsOf[m][1] : rollsOf[m][0];
                    return "shares " + nameOf (m) + " with " + rollLabel (other);
                }
                for (const auto& in : ins[m])
                {
                    if (in.from == r || reg.count (in.from) > 0) continue;
                    if (typeOf (in.from) == "util.pianoroll") return nameOf (m) + " also gets notes from " + rollLabel (in.from);
                    if (! rollsOf[in.from].empty()) return "shares " + nameOf (m) + " with " + rollLabel (rollsOf[in.from][0]);
                    if (in.kind == SocketKind::Audio) return nameOf (in.from) + " sends sound into " + nameOf (m) + " from outside";
                    if (in.kind == SocketKind::Midi) return nameOf (in.from) + " sends notes into " + nameOf (m) + " from outside";
                }
            }
            return {};
        };

        bool changed = false;
        // new clean piano rolls join by themselves, with the lowest free number
        skipped.clear();
        for (int r : rolls)
        {
            bool bound = false;
            for (const auto& p : patterns) bound = bound || p.rollId == r;
            if (bound) continue;
            if (claimed.count (r) > 0) continue;          // (on another Fruit Arranger)
            if (! reachesLanes (r)) continue;              // (6.9.14: not this arranger's lane)
            const auto why = problemOf (r);
            if (why.isNotEmpty() || dismissed.count (r) > 0)
            {
                if (why.isNotEmpty()) skipped.add ("A Piano Roll (" + namesOf (proc, region[r], r) + ") is left out: " + why);
                continue;
            }
            int n = 0;
            for (int k = 1; k <= kRolls && n == 0; ++k) if (pattern (k) == nullptr) n = k;
            if (n == 0) { skipped.add ("A Piano Roll is left out: all 16 patterns are in use"); continue; }
            PatternItem p;
            p.number = n;
            p.rollId = r;
            patterns.push_back (p);
            changed = true;
        }
        std::sort (patterns.begin(), patterns.end(), [] (const PatternItem& a, const PatternItem& b) { return a.number < b.number; });

        laneModules.clear();
        for (auto& p : patterns)
        {
            auto* inst = proc.getInstance (p.rollId);
            auto* roll = inst != nullptr ? dynamic_cast<PianoRollModule*> (inst->dsp.get()) : nullptr;
            juce::String problem;
            const bool free = roll != nullptr && claimed.count (p.rollId) == 0 && ! reachesLanes (p.rollId);
            if (roll == nullptr) problem = "its Piano Roll was deleted";
            else if (claimed.count (p.rollId) > 0) problem = "it is on another Fruit Arranger";
            else if (free) problem = "its sound does not go into Lanes In";   // (6.9.14: it plays on its own meanwhile)
            else problem = problemOf (p.rollId);
            const bool valid = problem.isEmpty();
            const auto name = roll != nullptr ? namesOf (proc, region[p.rollId], p.rollId) : p.name;
            const double loop = roll != nullptr ? roll->loopLengthBeats() : p.loopBeats;
            if (valid != p.valid || problem != p.problem || name != p.name || loop != p.loopBeats) changed = true;
            p.valid = valid; p.problem = problem; p.name = name; p.loopBeats = loop;
            if (roll != nullptr && claimed.count (p.rollId) == 0 && ! free)
            {
                roll->arrangerNumber.store (p.number, std::memory_order_relaxed);
                links.songs.push_back ({ myId, p.number - 1, p.rollId });   // (crossed out too: silent in the song, not its own)
                claimed.insert (p.rollId);
                if (valid)
                    for (int m : region[p.rollId]) { laneModules.insert (m); links.aligned.push_back (m); }
            }
        }

        for (auto& a : autos)
        {
            auto* inst = proc.getInstance (a.moduleId);
            int index = -1;
            if (inst != nullptr)
                for (int k = 0; k < (int) inst->descriptor().params.size(); ++k)
                    if (inst->descriptor().params[(size_t) k].id == a.paramId) { index = k; break; }
            const juce::String problem = inst == nullptr ? "its module was deleted" : index < 0 ? "its knob is gone" : juce::String();
            if ((problem.isEmpty()) != a.valid || problem != a.problem) changed = true;
            a.valid = problem.isEmpty();
            a.problem = problem;
        }
        // (a knob is linked only while its automation has a pill that plays: else it is left to itself)
        linkedAutos = playableAutos();
        for (const auto& a : autos)
            if (a.valid && linkedAutos.count (a.number) > 0)
                for (int k = 0; k < (int) proc.getInstance (a.moduleId)->descriptor().params.size(); ++k)
                    if (proc.getInstance (a.moduleId)->descriptor().params[(size_t) k].id == a.paramId)
                    { links.knobs.push_back ({ myId, a.number - 1, a.moduleId, k }); break; }

        links.aligned.push_back (myId);
        for (int id : ids) if (typeOf (id) == "io.audioOut") links.aligned.push_back (id);
        growTracks();
        inSync = true;
        if (changed) publish();
        else ++version;
        inSync = false;
    }

    // the automations with a pill that plays (an unmuted track)
    std::set<int> playableAutos() const
    {
        std::set<int> s;
        for (const auto& p : pills)
            if (p.kind == Kind::Auto && ! (p.track >= 0 && p.track < (int) tracks.size() && tracks[(size_t) p.track].mute))
                s.insert (p.item);
        return s;
    }
    std::set<int> linkedAutos;
    bool inSync { false };
    bool adopting { false };   // (6.9.14) Add / Rescan: lanes join before they are wired to Lanes In

    // a pattern's name: the synths its roll plays
    static juce::String namesOf (AquanodeModularAudioProcessor& proc, const std::set<int>& reg, int roll)
    {
        juce::StringArray names;
        for (const auto& c : proc.getCables())
            if (c.fromModule == roll && reg.count (c.toModule) > 0)
                if (auto* i = proc.getInstance (c.toModule))
                    names.addIfNotAlreadyThere (i->descriptor().displayName);
        return names.isEmpty() ? juce::String ("nothing yet") : names.joinIntoString (" + ");
    }

    // Add (from the sidebar) and Rescan: a first pill for every pattern without
    // one, the patterns' cables into the Audio Out moved over to Lanes In, and
    // the arranger into an Audio Out
    void adopt (AquanodeModularAudioProcessor& proc)
    {
        dismissed.clear();
        adopting = true;                // (every clean piano roll on it, connected to Lanes In or not yet)
        proc.routingChanged();          // (a fresh check)
        adopting = false;
        for (const auto& p : patterns)
            if (p.valid && ! hasPills (Kind::Pattern, p.number))
                placePill (Kind::Pattern, p.number, firstEmptyTrack(), 0.0);
        const auto lanes = laneModules;
        std::vector<std::pair<int, juce::String>> moved;
        for (int i = (int) proc.getCables().size() - 1; i >= 0; --i)
        {
            const auto c = proc.getCables()[(size_t) i];
            auto* to = proc.getInstance (c.toModule);
            if (to == nullptr || to->descriptor().typeId != "io.audioOut" || lanes.count (c.fromModule) == 0) continue;
            moved.push_back ({ c.fromModule, c.fromSocket });
            proc.removeCable (i);
        }
        for (const auto& m : moved) proc.addCable (m.first, m.second, selfId, "lanesIn");
        int out = -1;
        for (int id : proc.getModuleIds())
            if (auto* i = proc.getInstance (id); i != nullptr && i->descriptor().typeId == "io.audioOut") { out = id; break; }
        if (out < 0)
        {
            auto* me = proc.getInstance (selfId);
            const auto at = me != nullptr ? me->position + juce::Point<int> (preferredModuleWidth() + 80, 0) : juce::Point<int> (900, 60);
            out = proc.addModule ("io.audioOut", at);
        }
        bool wired = false;
        for (const auto& c : proc.getCables()) wired = wired || (c.fromModule == selfId && c.toModule == out);
        if (! wired) proc.addCable (selfId, "audioOut", out, "audioIn");
        publish();
    }

    //=== saved with the patch ====================================================
    bool hasAudioToSave() const override { return sampleAudio != nullptr && sampleAudio->getNumSamples() > 1; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override { rate = kRate; return sampleAudio; }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override
    {
        if (audio != nullptr && std::abs (rate - kRate) > 1.0 && audio->getNumSamples() > 1)
            audio = std::make_shared<juce::AudioBuffer<float>> (SamplerModule::resample (*audio, 0, audio->getNumSamples() - 1, rate / kRate));
        sampleAudio = audio;
        quietPublish();
    }
    // (while a patch or an undo step is being put back: no graph build from here)
    void quietPublish() { const bool was = inSync; inSync = true; publish(); inSync = was; }

    juce::String saveCustomState() const override
    {
        juce::XmlElement x ("FruitArranger");
        x.setAttribute ("v", 1);
        x.setAttribute ("zoom", (double) zoom);
        x.setAttribute ("scroll", scrollBeats);
        x.setAttribute ("scrollTrack", scrollTrack);
        for (const auto& p : patterns) { auto* e = x.createNewChildElement ("P"); e->setAttribute ("n", p.number); e->setAttribute ("roll", p.rollId); e->setAttribute ("name", p.name); e->setAttribute ("loop", p.loopBeats); }
        for (const auto& a : autos)
        {
            auto* e = x.createNewChildElement ("A");
            e->setAttribute ("n", a.number); e->setAttribute ("module", a.moduleId); e->setAttribute ("param", a.paramId); e->setAttribute ("label", a.label);
            juce::StringArray pts;
            for (const auto& q : a.points) pts.add (juce::String (q.x, 4) + ":" + juce::String (q.y, 4));
            e->setAttribute ("points", pts.joinIntoString (","));
        }
        for (const auto& s : samples) { auto* e = x.createNewChildElement ("S"); e->setAttribute ("name", s.name); e->setAttribute ("offset", s.offset); e->setAttribute ("length", s.length); }
        for (const auto& t : tracks) { auto* e = x.createNewChildElement ("T"); e->setAttribute ("mute", t.mute ? 1 : 0); }
        for (const auto& p : pills)
        {
            auto* e = x.createNewChildElement ("C");
            e->setAttribute ("k", (int) p.kind); e->setAttribute ("i", p.item); e->setAttribute ("t", p.track);
            e->setAttribute ("s", p.start); e->setAttribute ("l", p.length);
        }
        for (int r : dismissed) x.createNewChildElement ("D")->setAttribute ("roll", r);
        return x.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    }
    void loadCustomState (const juce::String& state) override
    {
        auto x = juce::parseXML (state);
        if (x == nullptr || ! x->hasTagName ("FruitArranger")) return;
        patterns.clear(); autos.clear(); samples.clear(); tracks.clear(); pills.clear(); dismissed.clear();
        zoom = (float) juce::jlimit (1.0, 200.0, x->getDoubleAttribute ("zoom", 10.0));
        scrollBeats = juce::jmax (0.0, x->getDoubleAttribute ("scroll", 0.0));
        scrollTrack = juce::jmax (0, x->getIntAttribute ("scrollTrack", 0));
        for (auto* e : x->getChildIterator())
        {
            if (e->hasTagName ("P"))
            {
                PatternItem p;
                p.number = juce::jlimit (1, kRolls, e->getIntAttribute ("n", 1));
                p.rollId = e->getIntAttribute ("roll", -1);
                p.name = e->getStringAttribute ("name", "Piano Roll");
                p.loopBeats = juce::jmax (1.0, e->getDoubleAttribute ("loop", 16.0));
                if (pattern (p.number) == nullptr) patterns.push_back (p);
            }
            else if (e->hasTagName ("A"))
            {
                AutoItem a;
                a.number = juce::jlimit (1, kAutos, e->getIntAttribute ("n", 1));
                a.moduleId = e->getIntAttribute ("module", -1);
                a.paramId = e->getStringAttribute ("param");
                a.label = e->getStringAttribute ("label");
                for (const auto& tok : juce::StringArray::fromTokens (e->getStringAttribute ("points"), ",", ""))
                    a.points.push_back ({ juce::jlimit (0.0f, 1.0f, tok.upToFirstOccurrenceOf (":", false, false).getFloatValue()),
                                          juce::jlimit (0.0f, 1.0f, tok.fromFirstOccurrenceOf (":", false, false).getFloatValue()) });
                if (a.points.empty()) a.points = { { 0.0f, 0.5f }, { 1.0f, 0.5f } };
                if (automation (a.number) == nullptr) autos.push_back (a);
            }
            else if (e->hasTagName ("S"))
            {
                SampleItem s;
                s.number = (int) samples.size() + 1;
                s.name = e->getStringAttribute ("name");
                s.offset = juce::jmax (0, e->getIntAttribute ("offset"));
                s.length = juce::jmax (0, e->getIntAttribute ("length"));
                samples.push_back (s);
            }
            else if (e->hasTagName ("T")) { Track t; t.mute = e->getIntAttribute ("mute") != 0; tracks.push_back (t); }
            else if (e->hasTagName ("C"))
            {
                Pill p;
                p.kind = (Kind) juce::jlimit (0, 2, e->getIntAttribute ("k"));
                p.item = e->getIntAttribute ("i", 1);
                p.track = juce::jlimit (0, kMaxTracks - 1, e->getIntAttribute ("t"));
                p.start = juce::jmax (0.0, e->getDoubleAttribute ("s"));
                p.length = juce::jmax (0.25, e->getDoubleAttribute ("l", 16.0));
                pills.push_back (p);
            }
            else if (e->hasTagName ("D")) dismissed.insert (e->getIntAttribute ("roll", -1));
        }
        std::sort (patterns.begin(), patterns.end(), [] (const PatternItem& a, const PatternItem& b) { return a.number < b.number; });
        std::sort (autos.begin(), autos.end(), [] (const AutoItem& a, const AutoItem& b) { return a.number < b.number; });
        growTracks();
        quietPublish();
    }

    //=== the card ==============================================================
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return juce::jlimit (kMinH, kMaxH, (int) std::lround (getParameter ("viewH"))); }
    int preferredModuleWidth() const override { return juce::jlimit (kMinW, kMaxW, (int) std::lround (getParameter ("viewW"))); }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "util.fruitarranger";
        d.displayName = "Fruit Arranger";
        d.searchKeywords = "arranger playlist song timeline tracks fruit daw piano roll automation clips samples arrange";
        d.description =
            "The playlist of a song made in the patch: every piano roll with the synths it plays (and all after them) is a "
            "pattern, numbered in its sidebar. Pills of patterns, knob automation and samples go on any track: highlight one "
            "(in the sidebar, by playing with its piano roll, or by tapping a pill), then tap an empty spot to place another. "
            "Drag to move, drag the right end to lengthen, tap twice to delete. Modulate Parameter: turn a knob anywhere, "
            "press it, and draw the knob's curve. Load Sample for audio pills. Only clean patterns play: nothing shared "
            "between two piano rolls' chains and no sound from outside; what fails is crossed out until it is fixed. "
            "Added from the sidebar it moves the patterns' cables into the Audio Out over to Lanes In. Inside its patterns "
            "latency compensation is always on. Own Play / Stop, or the host's transport.";
        d.section = ModuleSection::Utility;
        d.sockets = { audioIn ("lanesIn", "Lanes In"), audioOut ("audioOut", "Audio Out"), tempoIn() };   // (6.9.10: tempo per lane)
        for (int k = 0; k < kRolls; ++k) d.sockets.push_back (hiddenSocket (modOut ("roll" + juce::String (k + 1), "Roll " + juce::String (k + 1))));
        for (int k = 0; k < kAutos; ++k) d.sockets.push_back (hiddenSocket (unipolarOut ("auto" + juce::String (k + 1), "Auto " + juce::String (k + 1))));
        d.params = {
            makeRotary ("playing", "Playing", 0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f).hide().noMod(),
            makeCombo ("loop", "Mode", { "Loop", "Once" }, 0, 9, 2).hide().noMod(),
            makeCombo ("look", "Look", { "Aquanode", "Fruit" }, 0, 9, 2).hide().noMod(),
            makeRotary ("viewW", "Width", (float) kMinW, (float) kMaxW, (float) kDefaultW, 9, {}, false, 1.0f).hide().noMod(),
            makeRotary ("viewH", "Height", (float) kMinH, (float) kMaxH, (float) kDefaultH, 9, {}, false, 1.0f).hide().noMod() };
        return d;
    }

private:
    void refreshActive (const Song& song, double from)
    {
        numActive = 0;
        for (int i = 0; i < (int) song.pills.size() && numActive < kMaxActive; ++i)
        {
            const auto& p = song.pills[(size_t) i];
            if (p.start < from + 1.0 && p.start + p.length > from) active[numActive++] = i;
        }
        activeFrom = from;
        activeUntil = from + 1.0;
        if (jumped)
        {
            // the knobs where their automation has them here: in a pill, after one (its end), or before the first (its start)
            for (int k = 1; k <= kAutos; ++k)
            {
                const Pill* last = nullptr;
                const Pill* first = nullptr;
                for (const auto& p : song.pills)
                {
                    if (p.kind != Kind::Auto || p.item != k) continue;
                    if (first == nullptr) first = &p;
                    if (p.start <= from) last = &p;
                }
                const auto& c = song.curves[(size_t) k];
                if (last != nullptr) held[k - 1] = curveAt (c, (float) juce::jlimit (0.0, 1.0, (from - last->start) / juce::jmax (1.0e-6, last->length)));
                else if (first != nullptr) held[k - 1] = curveAt (c, 0.0f);
            }
        }
        refreshNeeded = jumped = false;
    }

    // shared
    mutable juce::SpinLock songLock;
    std::shared_ptr<const Song> published;
    std::vector<std::shared_ptr<const Song>> retired;   // message thread

    // audio thread
    std::shared_ptr<const Song> audioSong;
    double beat { 0.0 }, bpm { 120.0 }, bps { 0.0 }, activeFrom { 1.0e9 }, activeUntil { -1.0 };
    bool playing { false }, wasPlaying { false }, hostPlays { false }, refreshNeeded { true }, jumped { true };
    int active[kMaxActive] {};
    int numActive { 0 };
    float held[kAutos] {};
};

//==============================================================================
// the check, once per graph build, for every arranger in the patch (the first
// claims a piano roll; a second arranger crosses it out)
void fruitArrangerSync (AquanodeModularAudioProcessor& proc, FruitArrangerLinks& links)
{
    std::set<int> claimed;
    for (int id : proc.getModuleIds())
        if (auto* inst = proc.getInstance (id))
            if (auto* a = dynamic_cast<FruitArrangerModule*> (inst->dsp.get()))
                a->sync (proc, id, links, claimed);
}

// From the sidebar: the arranger with every clean piano roll on it, wired in
int fruitArrangerAddWithLanes (AquanodeModularAudioProcessor& proc, juce::Point<int> at)
{
    const int id = proc.addModule ("util.fruitarranger", at);
    if (auto* inst = proc.getInstance (id))
        if (auto* a = dynamic_cast<FruitArrangerModule*> (inst->dsp.get()))
            a->adopt (proc);
    return id;
}

//==============================================================================
// The card
//==============================================================================
namespace
{
    class ArrangerView final : public juce::Component, public plugins::PatchEditRequester,
                               public aquanode::ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit ArrangerView (FruitArrangerModule& m) : module (&m)
        {
            for (auto* b : { &playButton, &startButton, &loopButton, &zoomOut, &zoomIn, &lookButton, &rescanButton, &modulateButton, &loadButton })
            {
                addAndMakeVisible (b);
                b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22303a));
                b->setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.88f));
            }
            playButton.setTooltip ("Play / Stop the song (with a host playing, its transport leads)");
            startButton.setTooltip ("Back to the start");
            loopButton.setTooltip ("Loop the song, or play it Once");
            rescanButton.setTooltip ("Take in new piano rolls: a first pill each, and their cables into the Audio Out moved over to Lanes In");
            modulateButton.setTooltip ("Modulate Parameter: turn a knob anywhere in the patch, then press this - an automation track for it appears; draw its curve below");
            loadButton.setTooltip ("Load a sound as a sample to place on the tracks (kept with the patch)");
            lookButton.setTooltip ("Switch the look: Aquanode's or Fruit DAW's playlist");
            lookButton.onClick = [this] { if (auto* a = arr()) { a->setParameter ("look", fruitLook() ? 0.0f : 1.0f); applyButtonLook(); repaint(); } };
            // Play from here; Stop goes back to where Play began (|< to the start)
            playButton.onClick = [this]
            {
                auto* a = arr();
                if (a == nullptr) return;
                if (a->isPlayingNow() || a->getParameter ("playing") > 0.5f) { a->setPlaying (false); a->seek (playFrom); }
                else { playFrom = a->uiBeat.load(); a->setPlaying (true); }
            };
            startButton.onClick = [this] { if (auto* a = arr()) a->seek (0.0); };
            loopButton.onClick = [this] { if (auto* a = arr()) a->setParameter ("loop", a->getParameter ("loop") > 0.5f ? 0.0f : 1.0f); };
            zoomIn.onClick = [this] { setZoom (zoomNow() * 1.4f); };
            zoomOut.onClick = [this] { setZoom (zoomNow() / 1.4f); };
            rescanButton.onClick = [this]
            {
                if (requestPatchEdit == nullptr) return;
                requestPatchEdit ([] (AquanodeModularAudioProcessor& proc, int id)
                {
                    if (auto* inst = proc.getInstance (id))
                        if (auto* a = dynamic_cast<FruitArrangerModule*> (inst->dsp.get())) a->adopt (proc);
                });
            };
            modulateButton.onClick = [this] { modulate(); };
            loadButton.onClick = [this] { loadSample(); };
            addAndMakeVisible (grip);
            startTimerHz (30);
        }

        FruitArrangerModule* arr() const { return dynamic_cast<FruitArrangerModule*> (module.get()); }
        bool fruitLook() const { auto* a = arr(); return a != nullptr && a->getParameter ("look") > 0.5f; }
        bool buttonsFruit { false };
        void applyButtonLook()
        {
            const bool f = fruitLook();
            buttonsFruit = f;
            for (auto* b : { &playButton, &startButton, &loopButton, &zoomOut, &zoomIn, &lookButton, &rescanButton, &modulateButton, &loadButton })
            {
                b->setColour (juce::TextButton::buttonColourId, f ? fruitlook::nameBtn : juce::Colour (0xff22303a));
                b->setColour (juce::TextButton::textColourOffId, f ? fruitlook::darkText : juce::Colours::white.withAlpha (0.88f));
                if (f) { b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1fd5c9)); b->setColour (juce::TextButton::textColourOnId, fruitlook::darkText); }
                else { b->removeColour (juce::TextButton::buttonOnColourId); b->removeColour (juce::TextButton::textColourOnId); }
            }
            lookButton.setButtonText (f ? "Aquanode" : "Fruit");
        }

        //=== layout ===============================================================
        static constexpr int toolbarH = 26, sidebarW = 156, headerW = 30, rulerH = 16, bottomH = 70, rowH = 24, itemH = 19;
        juce::Rectangle<int> sidebarArea() const { return { 0, toolbarH, sidebarW, getHeight() - toolbarH }; }
        juce::Rectangle<int> rulerArea() const   { return { sidebarW + headerW, toolbarH, getWidth() - sidebarW - headerW, rulerH }; }
        juce::Rectangle<int> headerArea() const  { return { sidebarW, toolbarH + rulerH, headerW, getHeight() - toolbarH - rulerH - bottomH }; }
        juce::Rectangle<int> trackArea() const   { return { sidebarW + headerW, toolbarH + rulerH, getWidth() - sidebarW - headerW, getHeight() - toolbarH - rulerH - bottomH }; }
        juce::Rectangle<int> bottomArea() const  { return { sidebarW, getHeight() - bottomH, getWidth() - sidebarW, bottomH }; }

        void resized() override
        {
            auto bar = getLocalBounds().removeFromTop (toolbarH).reduced (3, 2);
            auto place = [&bar] (juce::Component& c, int w) { c.setBounds (bar.removeFromLeft (w)); bar.removeFromLeft (3); };
            place (playButton, 50); place (startButton, 28); place (loopButton, 48); place (zoomOut, 24); place (zoomIn, 24);
            bar.removeFromLeft (60);   // (the position, drawn)
            rescanButton.setBounds (bar.removeFromRight (62));
            bar.removeFromRight (3);
            lookButton.setBounds (bar.removeFromRight (54));
            bar.removeFromRight (3);
            modulateButton.setBounds (bar.removeFromRight (juce::jmin (230, juce::jmax (110, bar.getWidth()))));
            auto side = sidebarArea();
            loadButton.setBounds (side.removeFromBottom (24).reduced (4, 2));
            grip.setBounds (getWidth() - 14, getHeight() - 14, 14, 14);
        }

        //=== drawing =============================================================
        double scrollB() const { auto* a = arr(); return a != nullptr ? a->scrollBeats : 0.0; }
        int scrollT() const { auto* a = arr(); return a != nullptr ? a->scrollTrack : 0; }
        double beatAtX (float x) const { return scrollB() + (double) ((x - (float) trackArea().getX()) / zoomNow()); }
        float xAtBeat (double b) const { return (float) trackArea().getX() + (float) ((b - scrollB()) * zoomNow()); }
        int trackAtY (float y) const { return scrollT() + (int) std::floor ((y - (float) trackArea().getY()) / (float) rowH); }
        float yAtTrack (int t) const { return (float) trackArea().getY() + (float) ((t - scrollT()) * rowH); }
        float zoomNow() const { auto* a = arr(); return a != nullptr ? a->zoom : 10.0f; }
        void setZoom (float z)
        {
            if (auto* a = arr())
            {
                const double centre = beatAtX ((float) trackArea().getCentreX());
                a->zoom = juce::jlimit (2.0f, 120.0f, z);
                a->scrollBeats = juce::jmax (0.0, centre - (double) trackArea().getWidth() * 0.5 / a->zoom);
                repaint();
            }
        }
        double snapBeats() const { return zoomNow() >= 22.0f ? 1.0 : 4.0; }

        juce::Colour itemColour (Kind k, int item) const { return k == Kind::Pattern ? patternColour (item) : k == Kind::Auto ? kAutoColour : kSampleColour; }
        juce::String itemName (Kind k, int item) const
        {
            auto* a = arr();
            if (a == nullptr) return {};
            if (k == Kind::Pattern) { if (auto* p = a->pattern (item)) return juce::String (p->number) + "  " + p->name; }
            else if (k == Kind::Auto) { if (auto* au = a->automation (item)) return "A" + juce::String (au->number) + "  " + au->label; }
            else if (auto* s = a->sample (item)) return "S" + juce::String (s->number) + "  " + s->name;
            return "?";
        }

        void paint (juce::Graphics& g) override
        {
            auto* a = arr();
            if (a == nullptr) return;
            const auto look = LookPalette::current (juce::Colour (0xffffb04f), ThemeColours::Utility);
            fruit = fruitLook();
            if (fruit)
            {
                g.fillAll (fruitlook::panel);
                g.setColour (fruitlook::toolbar);
                g.fillRect (getLocalBounds().removeFromTop (toolbarH));
            }
            else
            {
                look.fillBackground (g, getLocalBounds().toFloat());
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRect (getLocalBounds().removeFromTop (toolbarH));
            }

            // the position
            {
                const double b = a->uiBeat.load (std::memory_order_relaxed);
                const int bar = (int) (b / 4.0) + 1, bt = (int) std::fmod (b, 4.0) + 1;
                g.setColour (fruit ? fruitlook::light : juce::Colours::white.withAlpha (0.8f));
                g.setFont (juce::Font (juce::FontOptions (12.0f)));
                g.drawText (juce::String (bar) + "." + juce::String (bt), juce::Rectangle<int> (zoomIn.getRight() + 4, 0, 56, toolbarH), juce::Justification::centred, false);
            }
            paintSidebar (g, *a, look);
            paintTracks (g, *a, look);
            paintBottom (g, *a, look);
        }

        bool fruit { false };   // this paint's look: Fruit DAW's playlist

        // the sidebar: patterns (numbered: the rolls' cards say the same), automation, samples
        struct Entry { Kind kind; int item; juce::Rectangle<int> r; };
        std::vector<Entry> entries;
        void paintSidebar (juce::Graphics& g, FruitArrangerModule& a, const LookPalette& look)
        {
            auto side = sidebarArea().withTrimmedBottom (24).reduced (3, 3);
            if (fruit) { g.setColour (fruitlook::patternList); g.fillRect (side); g.setColour (fruitlook::darkText.withAlpha (0.6f)); g.drawRect (side, 1); }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (look.skeuo ? 0.35f : 0.22f));
                g.fillRoundedRectangle (side.toFloat(), 4.0f);
            }
            entries.clear();
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            auto heading = [&] (const juce::String& text)
            {
                if (side.getHeight() < itemH) return;
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawText (text, side.removeFromTop (16).reduced (5, 0), juce::Justification::bottomLeft, false);
            };
            auto entry = [&] (Kind k, int item, bool valid, const juce::String& text)
            {
                if (side.getHeight() < itemH) return;
                auto r = side.removeFromTop (itemH);
                entries.push_back ({ k, item, r });
                const bool lit = highlightKind == k && highlightItem == item;
                if (lit)
                {
                    g.setColour ((fruit ? fruitlook::selected : look.accent).withAlpha (fruit ? 0.22f : 0.28f));
                    g.fillRoundedRectangle (r.toFloat().reduced (2.0f, 1.0f), fruit ? 0.0f : 3.0f);
                }
                const auto col = itemColour (k, item);
                g.setColour (valid ? col : col.withSaturation (0.1f).withAlpha (0.6f));
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) r.getX() + 5.0f, (float) r.getCentreY() - 4.5f, 9.0f, 9.0f), 2.0f);
                g.setColour ((fruit ? fruitlook::light : juce::Colours::white).withAlpha (valid ? 0.9f : 0.45f));
                const auto tr = r.withTrimmedLeft (18).withTrimmedRight (3);
                g.drawText (text, tr, juce::Justification::centredLeft, true);
                if (! valid)
                {
                    g.setColour (kBad.withAlpha (0.85f));
                    g.drawLine ((float) tr.getX(), (float) r.getCentreY(), (float) juce::jmin (tr.getRight(), tr.getX() + (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 2), (float) r.getCentreY(), 1.2f);
                }
            };
            heading ("Patterns");
            for (const auto& p : a.patterns) entry (Kind::Pattern, p.number, p.valid, juce::String (p.number) + "  " + p.name);
            if (a.patterns.empty()) { g.setColour (juce::Colours::white.withAlpha (0.35f)); if (side.getHeight() >= itemH) g.drawText ("(no piano roll yet)", side.removeFromTop (itemH).reduced (6, 0), juce::Justification::centredLeft, false); }
            heading ("Automation");
            for (const auto& au : a.autos) entry (Kind::Auto, au.number, au.valid, "A" + juce::String (au.number) + "  " + au.label);
            heading ("Samples");
            for (const auto& s : a.samples) entry (Kind::Sample, s.number, true, "S" + juce::String (s.number) + "  " + s.name);
        }

        void paintTracks (juce::Graphics& g, FruitArrangerModule& a, const LookPalette& look)
        {
            const auto area = trackArea();
            const auto ruler = rulerArea();
            const auto headers = headerArea();
            const float z = zoomNow();
            const double end = a.songEnd();
            // ruler: bars
            g.setColour (fruit ? fruitlook::toolbar : juce::Colours::black.withAlpha (0.3f));
            g.fillRect (ruler.withX (sidebarArea().getRight()).withWidth (getWidth() - sidebarW));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            if (fruit)
            {
                // Fruit: the rows first (dark green, alternating), the beats drawn over them below
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (area);
                g.fillAll (fruitlook::grid);
                for (int t = a.scrollTrack; t < (int) a.tracks.size(); ++t)
                {
                    const float y = yAtTrack (t);
                    if (y > (float) area.getBottom()) break;
                    g.setColour (t % 2 == 0 ? fruitlook::grid : fruitlook::gridAlt);
                    g.fillRect (juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), (float) rowH));
                    g.setColour (fruitlook::gridLine.darker (0.25f));
                    g.fillRect ((float) area.getX(), y, (float) area.getWidth(), 1.0f);
                }
                if (z >= 6.0f)
                    for (int bt = juce::jmax (0, (int) std::floor (a.scrollBeats));; ++bt)
                    {
                        const float x = xAtBeat (bt);
                        if (x > (float) area.getRight()) break;
                        g.setColour (fruitlook::gridLine.brighter (0.12f));
                        g.fillRect (x, (float) area.getY(), 1.0f, (float) area.getHeight());
                    }
            }
            const int barStep = z * 4.0f >= 36.0f ? 1 : z * 4.0f >= 18.0f ? 2 : z * 4.0f >= 9.0f ? 4 : 8;
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (ruler.getUnion (area));
                const int firstBar = juce::jmax (0, (int) std::floor (a.scrollBeats / 4.0));
                for (int b = firstBar;; ++b)
                {
                    const float x = xAtBeat (b * 4.0);
                    if (x > (float) area.getRight()) break;
                    if (fruit) g.setColour (fruitlook::gridLine.brighter (b % barStep == 0 ? 0.5f : 0.3f));
                    else g.setColour (juce::Colours::white.withAlpha (b % barStep == 0 ? 0.12f : 0.05f));
                    g.fillRect (x, (float) area.getY(), 1.0f, (float) area.getHeight());
                    if (b % barStep == 0)
                    {
                        g.setColour (fruit ? fruitlook::light.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.6f));
                        g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 2.0f, (float) ruler.getY(), 30.0f, (float) ruler.getHeight()), juce::Justification::centredLeft, false);
                    }
                }
                // past the song's end: dimmer
                const float ex = xAtBeat (end);
                if (ex < (float) area.getRight())
                {
                    g.setColour (juce::Colours::black.withAlpha (0.22f));
                    g.fillRect (juce::Rectangle<float> (juce::jmax ((float) area.getX(), ex), (float) area.getY(), (float) area.getRight() - juce::jmax ((float) area.getX(), ex), (float) area.getHeight()));
                }
            }
            // rows and their headers (number; M when muted)
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (area.getUnion (headers));
                for (int t = a.scrollTrack; t < (int) a.tracks.size(); ++t)
                {
                    const float y = yAtTrack (t);
                    if (y > (float) area.getBottom()) break;
                    if (! fruit)
                    {
                        g.setColour (juce::Colours::white.withAlpha (t % 2 == 0 ? 0.035f : 0.015f));
                        g.fillRect (juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), (float) rowH));
                    }
                    const auto hr = juce::Rectangle<float> ((float) headers.getX(), y, (float) headers.getWidth(), (float) rowH).reduced (fruit ? 0.0f : 2.0f, fruit ? 0.5f : 2.0f);
                    const bool muted = a.tracks[(size_t) t].mute;
                    if (fruit) g.setColour (muted ? fruitlook::loopRed : fruitlook::trackHead);
                    else g.setColour (muted ? kBad.withAlpha (0.45f) : juce::Colours::white.withAlpha (0.08f));
                    g.fillRoundedRectangle (hr, fruit ? 0.0f : 3.0f);
                    g.setColour ((fruit ? fruitlook::light : juce::Colours::white).withAlpha (muted ? 0.9f : fruit ? 0.85f : 0.6f));
                    g.drawText (muted ? "M" : juce::String (t + 1), hr, juce::Justification::centred, false);
                }
            }
            // pills
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (area);
                for (int i = 0; i < (int) a.pills.size(); ++i)
                    paintPill (g, a, i, look);
                // the playhead
                const float px = xAtBeat (a.uiBeat.load (std::memory_order_relaxed));
                if (px >= (float) area.getX() && px <= (float) area.getRight())
                {
                    if (look.glow && ! fruit) { g.setColour (look.accent.withAlpha (0.18f)); g.fillRect (px - 3.0f, (float) area.getY(), 6.0f, (float) area.getHeight()); }
                    g.setColour (fruit ? fruitlook::light : juce::Colours::white.withAlpha (0.85f));
                    g.fillRect (px - 0.75f, (float) area.getY(), 1.5f, (float) area.getHeight());
                }
            }
            const float px = xAtBeat (a.uiBeat.load (std::memory_order_relaxed));
            if (px >= (float) ruler.getX() && px <= (float) ruler.getRight())
            {
                juce::Path tri;
                tri.addTriangle (px - 5.0f, (float) ruler.getY(), px + 5.0f, (float) ruler.getY(), px, (float) ruler.getBottom() - 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillPath (tri);
            }
        }

        juce::Rectangle<float> pillRect (const Pill& p) const
        {
            return { xAtBeat (p.start), yAtTrack (p.track) + 2.0f, (float) (p.length * zoomNow()), (float) rowH - 4.0f };
        }

        void paintPill (juce::Graphics& g, FruitArrangerModule& a, int index, const LookPalette& look)
        {
            const auto& p = a.pills[(size_t) index];
            const auto r = pillRect (p);
            if (r.getRight() < (float) trackArea().getX() || r.getX() > (float) trackArea().getRight()) return;
            if (r.getBottom() < (float) trackArea().getY() || r.getY() > (float) trackArea().getBottom()) return;
            const bool valid = a.itemValid (p.kind, p.item);
            const bool muted = p.track < (int) a.tracks.size() && a.tracks[(size_t) p.track].mute;
            auto col = itemColour (p.kind, p.item);
            if (! valid || muted) col = col.withSaturation (0.15f).withMultipliedBrightness (0.6f);
            if (fruit)
            {
                paintFruitClip (g, a, index, r, valid, muted);
                return;
            }
            g.setColour (col.withAlpha (0.30f));
            g.fillRoundedRectangle (r, 3.0f);
            if (! look.skeuo)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.4f).reduced (1.0f, 1.0f), 2.0f);
            }
            // what is in it
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r.reduced (1.0f).toNearestInt());
                const auto inner = r.reduced (2.0f, 3.0f).withTrimmedTop (8.0f);
                g.setColour (col.brighter (0.4f).withAlpha (0.9f));
                if (p.kind == Kind::Pattern) paintNotes (g, a, p, inner);
                else if (p.kind == Kind::Auto)
                {
                    if (auto* au = a.automation (p.item))
                    {
                        juce::Path path;
                        const int n = juce::jlimit (8, 400, (int) inner.getWidth());
                        for (int i = 0; i <= n; ++i)
                        {
                            const float x = (float) i / (float) n;
                            const float y = inner.getBottom() - curveAt (au->points, x) * inner.getHeight();
                            if (i == 0) path.startNewSubPath (inner.getX(), y); else path.lineTo (inner.getX() + x * inner.getWidth(), y);
                        }
                        g.strokePath (path, juce::PathStrokeType (1.3f));
                    }
                }
                else paintWave (g, a, p, inner);
                g.setColour (juce::Colours::white.withAlpha (valid ? 0.85f : 0.5f));
                g.setFont (juce::Font (juce::FontOptions (9.5f)));
                g.drawText (itemName (p.kind, p.item), r.reduced (4.0f, 1.0f).withHeight (10.0f), juce::Justification::topLeft, false);
            }
            const bool lit = highlightKind == p.kind && highlightItem == p.item;
            g.setColour (index == selectedPill ? juce::Colours::white : lit ? col.brighter (0.6f) : col.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 3.0f, index == selectedPill ? 1.8f : lit ? 1.5f : 1.0f);
            if (! valid)
            {
                g.setColour (kBad.withAlpha (0.85f));
                g.drawLine (r.getX() + 2.0f, r.getY() + 2.0f, r.getRight() - 2.0f, r.getBottom() - 2.0f, 1.4f);
                g.drawLine (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getRight() - 2.0f, r.getY() + 2.0f, 1.4f);
            }
        }

        // a clip as Fruit DAW's playlist draws it: a body, a header strip with the name, the
        // notes (or the curve, the waveform) in light, a dark edge
        void paintFruitClip (juce::Graphics& g, FruitArrangerModule& a, int index, juce::Rectangle<float> r, bool valid, bool muted)
        {
            const auto& p = a.pills[(size_t) index];
            juce::Colour body = fruitlook::clipBody, head = fruitlook::clipHead, ink = fruitlook::light;
            if (p.kind == Kind::Auto) { body = fruitlook::loopRed.darker (0.45f); head = fruitlook::loopRed; }
            else if (p.kind == Kind::Sample) { body = fruitlook::sampleBody; head = fruitlook::sampleHead; ink = fruitlook::wave; }
            if (! valid || muted) { body = body.withSaturation (0.1f); head = head.withSaturation (0.1f); }
            const bool lit = highlightKind == p.kind && highlightItem == p.item;
            g.setColour (lit ? body.brighter (0.12f) : body);
            g.fillRect (r);
            const float headH = juce::jmin (9.0f, r.getHeight() * 0.45f);
            g.setColour (head);
            g.fillRect (r.withHeight (headH));
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r.reduced (1.0f).toNearestInt());
                const auto inner = r.withTrimmedTop (headH + 1.0f).reduced (1.0f, 1.0f);
                g.setColour ((p.kind == Kind::Sample ? ink : fruitlook::light).withAlpha (0.8f));
                if (p.kind == Kind::Pattern) paintNotes (g, a, p, inner);
                else if (p.kind == Kind::Auto)
                {
                    if (auto* au = a.automation (p.item))
                    {
                        juce::Path path;
                        const int n = juce::jlimit (8, 400, (int) inner.getWidth());
                        for (int i = 0; i <= n; ++i)
                        {
                            const float x = (float) i / (float) n;
                            const float y = inner.getBottom() - curveAt (au->points, x) * inner.getHeight();
                            if (i == 0) path.startNewSubPath (inner.getX(), y); else path.lineTo (inner.getX() + x * inner.getWidth(), y);
                        }
                        g.strokePath (path, juce::PathStrokeType (1.3f));
                    }
                }
                else paintWave (g, a, p, inner);
                g.setColour (fruitlook::light.withAlpha (valid ? 0.95f : 0.5f));
                g.setFont (juce::Font (juce::FontOptions (8.5f)));
                const auto name = p.kind == Kind::Pattern ? "Pattern " + itemName (p.kind, p.item) : itemName (p.kind, p.item);
                g.drawText (name, r.withHeight (headH).reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
            }
            g.setColour (index == selectedPill ? fruitlook::selected : fruitlook::darkText.withAlpha (0.8f));
            g.drawRect (r, index == selectedPill ? 1.6f : 1.0f);
            if (! valid)
            {
                g.setColour (kBad.withAlpha (0.85f));
                g.drawLine (r.getX() + 2.0f, r.getY() + 2.0f, r.getRight() - 2.0f, r.getBottom() - 2.0f, 1.4f);
                g.drawLine (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getRight() - 2.0f, r.getY() + 2.0f, 1.4f);
            }
        }

        // a pattern's notes, looped across its pill
        struct NotesCache { int counter { -1 }; std::vector<PianoRollNote> notes; int lo { 60 }, hi { 72 }; };
        std::map<int, NotesCache> notesCache;   // by roll id
        PianoRollModule* rollOf (FruitArrangerModule& a, int number) const
        {
            auto* p = a.pattern (number);
            if (p == nullptr || a.owner == nullptr) return nullptr;
            auto* inst = a.owner->getInstance (p->rollId);
            return inst != nullptr ? dynamic_cast<PianoRollModule*> (inst->dsp.get()) : nullptr;
        }
        const NotesCache* notesOf (FruitArrangerModule& a, int number)
        {
            auto* roll = rollOf (a, number);
            if (roll == nullptr) return nullptr;
            auto& c = notesCache[a.pattern (number)->rollId];
            if (c.counter != roll->getEditCounter())
            {
                c.counter = roll->getEditCounter();
                c.notes = roll->getNotes();
                c.lo = 127; c.hi = 0;
                for (const auto& n : c.notes) { c.lo = juce::jmin (c.lo, n.pitch); c.hi = juce::jmax (c.hi, n.pitch); }
                if (c.lo > c.hi) { c.lo = 60; c.hi = 72; }
            }
            return &c;
        }
        void paintNotes (juce::Graphics& g, FruitArrangerModule& a, const Pill& p, juce::Rectangle<float> r)
        {
            const auto* c = notesOf (a, p.item);
            auto* item = a.pattern (p.item);
            if (c == nullptr || item == nullptr || c->notes.empty()) return;
            const double loop = juce::jmax (1.0, item->loopBeats);
            const float span = (float) juce::jmax (6, c->hi - c->lo + 1);
            const float nh = juce::jmax (1.5f, r.getHeight() / span);
            for (double off = 0.0; off < p.length; off += loop)
                for (const auto& n : c->notes)
                {
                    if (n.start + off >= p.length) continue;
                    const float x = r.getX() + (float) ((off + n.start) * zoomNow());
                    if (x > r.getRight()) break;
                    const float w = juce::jmax (1.5f, (float) (juce::jmin (n.length, p.length - off - n.start) * zoomNow()) - 0.5f);
                    const float y = r.getBottom() - ((float) (n.pitch - c->lo) + 1.0f) * (r.getHeight() - nh) / juce::jmax (1.0f, span - 1.0f) - nh * 0.5f;
                    g.fillRect (juce::Rectangle<float> (x, y, w, nh * 0.8f));
                }
        }
        // a sample's outline
        std::map<int, std::vector<float>> peaks;
        const juce::AudioBuffer<float>* peaksFrom { nullptr };
        void paintWave (juce::Graphics& g, FruitArrangerModule& a, const Pill& p, juce::Rectangle<float> r)
        {
            const auto* s = a.sample (p.item);
            if (s == nullptr || a.sampleAudio == nullptr) return;
            if (peaksFrom != a.sampleAudio.get()) { peaks.clear(); peaksFrom = a.sampleAudio.get(); }
            auto& pk = peaks[s->number];
            if (pk.empty())
            {
                pk.assign (1024, 0.0f);
                for (int i = 0; i < 1024; ++i)
                {
                    const int a0 = s->offset + (int) ((int64_t) s->length * i / 1024), a1 = s->offset + (int) ((int64_t) s->length * (i + 1) / 1024);
                    float m = 0.0f;
                    for (int j = a0; j < juce::jmin (a1, a.sampleAudio->getNumSamples()); ++j) m = juce::jmax (m, std::abs (a.sampleAudio->getSample (0, j)));
                    pk[(size_t) i] = m;
                }
            }
            const double secs = (double) s->length / kRate;
            const double beats = secs * a.tempo() / 60.0;
            const float w = (float) (juce::jmin (beats, p.length) * zoomNow());
            const float cy = r.getCentreY(), hh = r.getHeight() * 0.5f;
            const int n = juce::jlimit (2, 1024, (int) w);
            for (int i = 0; i < n; ++i)
            {
                const double frac = (double) i / (double) n * (juce::jmin (beats, p.length) / juce::jmax (1.0e-6, beats));
                const float v = pk[(size_t) juce::jlimit (0, 1023, (int) (frac * 1024.0))];
                g.fillRect (juce::Rectangle<float> (r.getX() + w * (float) i / (float) n, cy - v * hh, juce::jmax (1.0f, w / (float) n), juce::jmax (0.5f, 2.0f * v * hh)));
            }
        }

        // below the tracks: the highlighted item - an automation's curve to draw, a
        // pattern's notes and its state, a sample; and what the check left out
        juce::Rectangle<float> curveArea() const { return bottomArea().toFloat().reduced (8.0f, 6.0f).withTrimmedTop (12.0f); }
        void paintBottom (juce::Graphics& g, FruitArrangerModule& a, const LookPalette& look)
        {
            const auto b = bottomArea().toFloat().reduced (4.0f, 3.0f);
            if (fruit) { g.setColour (fruitlook::mixerBg); g.fillRect (b); }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (look.skeuo ? 0.4f : 0.25f));
                g.fillRoundedRectangle (b, 4.0f);
            }
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            juce::String title, note;
            juce::Colour noteColour = juce::Colours::white.withAlpha (0.55f);
            if (highlightKind == Kind::Auto && a.automation (highlightItem) != nullptr)
            {
                auto* au = a.automation (highlightItem);
                title = "A" + juce::String (au->number) + "  " + au->label + "  -  draw its curve: tap to add a point, drag one, tap it twice to remove it";
                if (! au->valid) { note = "Crossed out: " + au->problem; noteColour = kBad; }
                const auto c = curveArea();
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.fillRect (c);
                juce::Path path;
                for (int i = 0; i <= 300; ++i)
                {
                    const float x = (float) i / 300.0f, y = c.getBottom() - curveAt (au->points, x) * c.getHeight();
                    if (i == 0) path.startNewSubPath (c.getX(), y); else path.lineTo (c.getX() + x * c.getWidth(), y);
                }
                LookPalette lp = look; lp.accent = kAutoColour;
                lp.strokeGlowing (g, path, 1.6f);
                for (const auto& q : au->points)
                {
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (c.getX() + q.x * c.getWidth() - 3.5f, c.getBottom() - q.y * c.getHeight() - 3.5f, 7.0f, 7.0f);
                }
            }
            else if (highlightKind == Kind::Pattern && a.pattern (highlightItem) != nullptr)
            {
                auto* p = a.pattern (highlightItem);
                title = "Pattern " + juce::String (p->number) + "  -  " + p->name + "  (" + juce::String (p->loopBeats / 4.0, 1).trimCharactersAtEnd (".0") + " bars; edit it in its Piano Roll)";
                if (! p->valid) { note = "Crossed out: " + p->problem; noteColour = kBad; }
                else note = "Tap an empty spot on a track to place it.";
            }
            else if (highlightKind == Kind::Sample && a.sample (highlightItem) != nullptr)
            {
                auto* s = a.sample (highlightItem);
                title = "Sample " + juce::String (s->number) + "  -  " + s->name + "  (" + juce::String ((double) s->length / kRate, 1) + " s)";
                note = "Tap an empty spot on a track to place it.";
            }
            else
            {
                title = "Highlight a pattern, automation or sample (sidebar, pill, or by playing with its piano roll), then tap an empty spot.";
            }
            if (a.skipped.size() > 0 && (note.isEmpty() || highlightKind != Kind::Auto))
            {
                if (note.isNotEmpty()) note << "   ";
                note << a.skipped[0] << (a.skipped.size() > 1 ? " (+" + juce::String (a.skipped.size() - 1) + " more)" : juce::String());
                if (noteColour != kBad) noteColour = juce::Colour (0xffffc35a);
            }
            g.setColour (fruit ? fruitlook::light : juce::Colours::white.withAlpha (0.8f));
            g.drawText (title, b.reduced (6.0f, 2.0f).withHeight (13.0f), juce::Justification::centredLeft, true);
            if (note.isNotEmpty() && highlightKind != Kind::Auto)
            {
                g.setColour (noteColour);
                g.drawFittedText (note, b.reduced (6.0f, 2.0f).withTrimmedTop (16.0f).toNearestInt(), juce::Justification::topLeft, 3);
            }
            else if (note.isNotEmpty())
            {
                g.setColour (noteColour);
                g.drawText (note, b.reduced (6.0f, 2.0f).withTrimmedTop (b.getHeight() - 16.0f), juce::Justification::centredRight, true);
            }
        }

        //=== touch =================================================================
        enum class Drag { None, Pan, MovePill, SizePill, Seek, Curve };
        Drag drag { Drag::None };
        juce::Point<float> downAt;
        double downScroll { 0.0 }, grabOffset { 0.0 }, downStart { 0.0 }, downLength { 0.0 };
        int downScrollTrack { 0 }, downTrack { 0 };
        bool moved { false };
        int lastTapPill { -1 }, curvePoint { -1 };
        juce::uint32 lastTapTime { 0 };
        juce::Point<float> lastCurveTap { -100.0f, -100.0f };

        int pillAt (juce::Point<float> pt) const
        {
            auto* a = arr();
            if (a == nullptr) return -1;
            for (int i = (int) a->pills.size() - 1; i >= 0; --i)
                if (pillRect (a->pills[(size_t) i]).contains (pt)) return i;
            return -1;
        }
        void highlight (Kind k, int item) { highlightKind = k; highlightItem = item; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* a = arr();
            if (a == nullptr) return;
            downAt = e.position;
            moved = false;
            drag = Drag::None;
            const auto pt = e.position;
            if (rulerArea().toFloat().contains (pt)) { drag = Drag::Seek; a->seek (juce::jmax (0.0, beatAtX (pt.x))); return; }
            if (sidebarArea().toFloat().contains (pt))
            {
                for (const auto& en : entries)
                    if (en.r.toFloat().contains (pt))
                    {
                        const bool again = highlightKind == en.kind && highlightItem == en.item
                                        && juce::Time::getMillisecondCounter() - lastTapTime < 400;
                        lastTapTime = juce::Time::getMillisecondCounter();
                        highlight (en.kind, en.item);
                        if (again || e.mods.isPopupMenu()) itemMenu (en.kind, en.item);
                        return;
                    }
                return;
            }
            if (headerArea().toFloat().contains (pt))
            {
                const int t = trackAtY (pt.y);
                if (t >= 0 && t < (int) a->tracks.size()) { a->tracks[(size_t) t].mute = ! a->tracks[(size_t) t].mute; a->publish(); repaint(); }
                return;
            }
            if (bottomArea().toFloat().contains (pt))
            {
                if (highlightKind == Kind::Auto && a->automation (highlightItem) != nullptr && curveArea().expanded (6.0f).contains (pt)) curveDown (pt);
                return;
            }
            if (! trackArea().toFloat().contains (pt)) return;
            const int i = pillAt (pt);
            if (i >= 0)
            {
                const auto& p = a->pills[(size_t) i];
                const auto r = pillRect (p);
                selectedPill = i;
                highlight (p.kind, p.item);
                drag = (r.getRight() - pt.x < 10.0f && r.getWidth() > 24.0f) ? Drag::SizePill : Drag::MovePill;
                grabOffset = beatAtX (pt.x) - p.start;
                downStart = p.start;
                downLength = p.length;
                downTrack = p.track;
                return;
            }
            selectedPill = -1;
            drag = Drag::Pan;
            downScroll = a->scrollBeats;
            downScrollTrack = a->scrollTrack;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* a = arr();
            if (a == nullptr) return;
            if (e.position.getDistanceFrom (downAt) > 6.0f) moved = true;
            const double snap = snapBeats();
            switch (drag)
            {
                case Drag::Seek: a->seek (juce::jmax (0.0, beatAtX (e.position.x))); break;
                case Drag::Pan:
                    if (! moved) break;
                    a->scrollBeats = juce::jmax (0.0, downScroll - (double) ((e.position.x - downAt.x) / zoomNow()));
                    a->scrollTrack = juce::jlimit (0, juce::jmax (0, (int) a->tracks.size() - 1), downScrollTrack - (int) std::round ((e.position.y - downAt.y) / (float) rowH));
                    break;
                case Drag::MovePill:
                    if (! moved || selectedPill < 0 || selectedPill >= (int) a->pills.size()) break;
                    {
                        auto& p = a->pills[(size_t) selectedPill];
                        p.start = juce::jmax (0.0, std::round ((beatAtX (e.position.x) - grabOffset) / snap) * snap);
                        p.track = juce::jlimit (0, (int) a->tracks.size() - 1, trackAtY (e.position.y));
                    }
                    break;
                case Drag::SizePill:
                    if (selectedPill < 0 || selectedPill >= (int) a->pills.size()) break;
                    {
                        auto& p = a->pills[(size_t) selectedPill];
                        p.length = juce::jmax (snap, std::round ((beatAtX (e.position.x) - p.start) / snap) * snap);
                    }
                    break;
                case Drag::Curve: curveDrag (e.position); break;
                case Drag::None: default: break;
            }
            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            auto* a = arr();
            if (a == nullptr) return;
            if ((drag == Drag::MovePill || drag == Drag::SizePill) && selectedPill >= 0)
            {
                if (moved || drag == Drag::SizePill) { a->growTracks(); a->publish(); }
                else
                {
                    // a tap on a pill: twice deletes it
                    const auto now = juce::Time::getMillisecondCounter();
                    if (lastTapPill == selectedPill && now - lastTapTime < 380) { a->removePill (selectedPill); selectedPill = -1; lastTapPill = -1; }
                    else { lastTapPill = selectedPill; lastTapTime = now; }
                }
            }
            else if (drag == Drag::Pan && ! moved && trackArea().toFloat().contains (e.position))
            {
                // a tap on an empty spot: another of what is highlighted
                if (highlightItem > 0 && a->itemValid (highlightKind, highlightItem))
                {
                    const double snap = snapBeats();
                    const int t = trackAtY (e.position.y);
                    if (t >= 0 && t < kMaxTracks)
                        selectedPill = a->placePill (highlightKind, highlightItem, t, std::floor (juce::jmax (0.0, beatAtX (e.position.x)) / snap) * snap);
                }
            }
            else if (drag == Drag::Curve) a->publish();
            drag = Drag::None;
            repaint();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            auto* a = arr();
            if (a == nullptr) return;
            if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { setZoom (zoomNow() * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f)); return; }
            if (std::abs (w.deltaX) > std::abs (w.deltaY) || e.mods.isShiftDown())
                a->scrollBeats = juce::jmax (0.0, a->scrollBeats - (double) ((std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) * 200.0f / zoomNow()));
            else
                a->scrollTrack = juce::jlimit (0, juce::jmax (0, (int) a->tracks.size() - 1), a->scrollTrack - (w.deltaY > 0 ? 1 : -1));
            repaint();
        }

        // the curve editor
        void curveDown (juce::Point<float> pt)
        {
            auto* a = arr();
            auto* au = a != nullptr ? a->automation (highlightItem) : nullptr;
            if (au == nullptr) return;
            const auto c = curveArea();
            curvePoint = -1;
            for (int i = 0; i < (int) au->points.size(); ++i)
            {
                const juce::Point<float> q (c.getX() + au->points[(size_t) i].x * c.getWidth(), c.getBottom() - au->points[(size_t) i].y * c.getHeight());
                if (q.getDistanceFrom (pt) < 12.0f) curvePoint = i;
            }
            const auto now = juce::Time::getMillisecondCounter();
            if (curvePoint >= 0 && pt.getDistanceFrom (lastCurveTap) < 14.0f && now - lastTapTime < 380 && au->points.size() > 2)
            {
                au->points.erase (au->points.begin() + curvePoint);     // tapped twice: away
                curvePoint = -1;
                a->publish();
                lastCurveTap = { -100.0f, -100.0f };
                return;
            }
            lastCurveTap = pt;
            lastTapTime = now;
            if (curvePoint < 0)
            {
                const juce::Point<float> v (juce::jlimit (0.0f, 1.0f, (pt.x - c.getX()) / c.getWidth()), juce::jlimit (0.0f, 1.0f, (c.getBottom() - pt.y) / c.getHeight()));
                au->points.push_back (v);
                std::sort (au->points.begin(), au->points.end(), [] (auto x, auto y) { return x.x < y.x; });
                for (int i = 0; i < (int) au->points.size(); ++i) if (au->points[(size_t) i] == v) curvePoint = i;
            }
            drag = Drag::Curve;
            a->publish();
        }
        void curveDrag (juce::Point<float> pt)
        {
            auto* a = arr();
            auto* au = a != nullptr ? a->automation (highlightItem) : nullptr;
            if (au == nullptr || curvePoint < 0 || curvePoint >= (int) au->points.size()) return;
            const auto c = curveArea();
            float x = juce::jlimit (0.0f, 1.0f, (pt.x - c.getX()) / c.getWidth());
            if (curvePoint > 0) x = juce::jmax (x, au->points[(size_t) curvePoint - 1].x + 0.002f);
            if (curvePoint + 1 < (int) au->points.size()) x = juce::jmin (x, au->points[(size_t) curvePoint + 1].x - 0.002f);
            au->points[(size_t) curvePoint] = { juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, (c.getBottom() - pt.y) / c.getHeight()) };
            a->publish();
        }

        // an item's menu (tapped twice in the sidebar)
        void itemMenu (Kind k, int item)
        {
            auto* a = arr();
            if (a == nullptr) return;
            juce::PopupMenu m;
            m.addSectionHeader (itemName (k, item));
            if (k == Kind::Pattern) if (auto* p = a->pattern (item); p != nullptr && ! p->valid) m.addItem (-1, "Crossed out: " + p->problem, false);
            if (k == Kind::Auto) if (auto* au = a->automation (item); au != nullptr && ! au->valid) m.addItem (-1, "Crossed out: " + au->problem, false);
            m.addItem (1, k == Kind::Pattern ? "Take off the arranger (Rescan brings it back)" : "Remove, with its pills");
            juce::Component::SafePointer<ArrangerView> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, k, item] (int r)
            {
                if (safe == nullptr || r != 1) return;
                if (auto* aa = safe->arr())
                {
                    aa->removeItem (k, item);
                    if (safe->requestPatchEdit != nullptr) safe->requestPatchEdit ([] (AquanodeModularAudioProcessor& proc, int) { proc.routingChanged(); });
                    safe->highlightItem = 0;
                    safe->repaint();
                }
            });
        }

        //=== Modulate Parameter: the knob last turned anywhere in the patch =======
        struct Knob { int module { -1 }; int param { -1 }; juce::String label; };
        Knob lastKnob;
        std::map<std::pair<int, int>, float> knobSnapshot;
        void scanKnobs()
        {
            auto* a = arr();
            if (a == nullptr || a->owner == nullptr) return;
            auto& proc = *a->owner;
            std::set<std::pair<int, juce::String>> automated;
            for (const auto& au : a->autos) automated.insert ({ au.moduleId, au.paramId });
            for (int id : proc.getModuleIds())
            {
                if (id == a->selfId) continue;
                auto* inst = proc.getInstance (id);
                if (inst == nullptr) continue;
                const auto& params = inst->descriptor().params;
                for (int k = 0; k < (int) params.size(); ++k)
                {
                    const auto& s = params[(size_t) k];
                    if (! s.modulatable || (s.hidden && ! s.hiddenCableTarget)) continue;
                    if (s.type != ParamType::Rotary && s.type != ParamType::RotarySteppedList) continue;
                    const auto key = std::make_pair (id, k);
                    const float v = inst->dsp->getParameterBase (k);
                    auto it = knobSnapshot.find (key);
                    if (it == knobSnapshot.end()) { knobSnapshot[key] = v; continue; }
                    if (std::abs (it->second - v) > 1.0e-6f)
                    {
                        it->second = v;
                        if (automated.count ({ id, s.id }) == 0)
                            lastKnob = { id, k, inst->descriptor().displayName + ": " + s.label };
                    }
                }
            }
        }
        void modulate()
        {
            auto* a = arr();
            if (a == nullptr) return;
            if (lastKnob.module < 0 || a->owner == nullptr || a->owner->getInstance (lastKnob.module) == nullptr)
            {
                juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit Arranger",
                    "Turn a knob anywhere in the patch first, then press Modulate Parameter.");
                return;
            }
            const auto k = lastKnob;
            const double at = a->uiBeat.load();
            if (requestPatchEdit == nullptr) return;
            juce::Component::SafePointer<ArrangerView> safe (this);
            requestPatchEdit ([k, at, safe] (AquanodeModularAudioProcessor& proc, int id)
            {
                auto* inst = proc.getInstance (id);
                auto* aa = inst != nullptr ? dynamic_cast<FruitArrangerModule*> (inst->dsp.get()) : nullptr;
                if (aa == nullptr) return;
                if (! aa->addAutomation (k.module, k.param, k.label, at))
                {
                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit Arranger", "All 16 automation lanes are in use.");
                    return;
                }
                proc.routingChanged();   // (its link onto the knob)
                if (safe != nullptr) safe->highlight (Kind::Auto, aa->autos.empty() ? 0 : [&] { int n = 0; for (const auto& au : aa->autos) if (au.moduleId == k.module && au.label == k.label) n = au.number; return n; }());
            });
            lastKnob = {};
        }

        //=== Load Sample ===========================================================
        std::unique_ptr<juce::FileChooser> chooser;
        void loadSample()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), aqp::android::pickerPatterns ("*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"));
            juce::Component::SafePointer<ArrangerView> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto url = fc.getURLResult();
                if (url.isEmpty()) return;
                auto data = std::make_shared<juce::MemoryBlock>();
                if (const auto problem = aqp::files::readDocument (url, *data); problem.isNotEmpty())
                {
                    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit Arranger", "The file could not be read: " + problem + ".");
                    return;
                }
                juce::String name = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : juce::String();
               #if JUCE_ANDROID
                if (name.isEmpty())
                    if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
                        name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
               #endif
                if (name.isEmpty()) name = juce::URL::removeEscapeChars (url.getFileName()).upToLastOccurrenceOf (".", false, false);
                // decoded and brought to 48 kHz off the message thread
                juce::Thread::launch ([safe, data, name]
                {
                    juce::AudioFormatManager fm;
                    fm.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (*data, false)));
                    auto audio = std::make_shared<juce::AudioBuffer<float>>();
                    juce::String problem;
                    if (reader == nullptr) problem = "not a sound file this app reads";
                    else if ((double) reader->lengthInSamples / juce::jmax (1.0, reader->sampleRate) > kMaxSampleSeconds) problem = "longer than ten minutes";
                    else
                    {
                        juce::AudioBuffer<float> raw ((int) std::min<unsigned int> (2u, reader->numChannels), (int) reader->lengthInSamples);
                        reader->read (&raw, 0, raw.getNumSamples(), 0, true, raw.getNumChannels() > 1);
                        *audio = std::abs (reader->sampleRate - kRate) < 1.0 ? raw
                               : SamplerModule::resample (raw, 0, raw.getNumSamples() - 1, reader->sampleRate / kRate);
                    }
                    juce::MessageManager::callAsync ([safe, audio, name, problem]
                    {
                        if (safe == nullptr) return;
                        auto* a = safe->arr();
                        if (a == nullptr) return;
                        if (problem.isEmpty() && ! a->addSample (name, *audio))
                        {
                            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit Arranger", "The samples together may last ten minutes at most.");
                            return;
                        }
                        if (problem.isNotEmpty())
                        {
                            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Fruit Arranger", "The file could not be loaded: " + problem + ".");
                            return;
                        }
                        safe->highlight (Kind::Sample, (int) a->samples.size());
                    });
                });
            });
        }

        //=== the timer =============================================================
        int tick { 0 }, seenVersion { -1 };
        std::map<int, int> seenEdits;   // roll id -> its edit counter: playing with a roll highlights its pattern
        void timerCallback() override
        {
            auto* a = arr();
            if (a == nullptr) return;
            ++tick;
            bool loopsChanged = false;
            for (auto& p : a->patterns)
                if (auto* roll = rollOf (*a, p.number))
                {
                    // its notes or its length changed: the pills follow (and the song's loop of it)
                    if (roll->loopLengthBeats() != p.loopBeats) { p.loopBeats = roll->loopLengthBeats(); loopsChanged = true; }
                    const int c = roll->getEditCounter();
                    auto it = seenEdits.find (p.rollId);
                    if (it == seenEdits.end()) seenEdits[p.rollId] = c;
                    else if (it->second != c) { it->second = c; highlight (Kind::Pattern, p.number); }
                }
            if (loopsChanged) a->publish();
            if (tick % 4 == 0) scanKnobs();
            const bool playingNow = a->isPlayingNow();
            playButton.setButtonText (playingNow ? "Stop" : "Play");
            playButton.setToggleState (playingNow, juce::dontSendNotification);
            loopButton.setButtonText (a->getParameter ("loop") > 0.5f ? "Once" : "Loop");
            if (buttonsFruit != fruitLook()) { applyButtonLook(); repaint(); }
            const auto mod = lastKnob.module >= 0 ? "Modulate: " + lastKnob.label : juce::String ("Modulate Parameter");
            if (modulateButton.getButtonText() != mod) modulateButton.setButtonText (mod);
            // keep the playhead in view while it plays
            if (playingNow && drag == Drag::None)
            {
                const double b = a->uiBeat.load();
                const double visible = (double) trackArea().getWidth() / zoomNow();
                if (b < a->scrollBeats || b > a->scrollBeats + visible * 0.92) a->scrollBeats = juce::jmax (0.0, b - visible * 0.1);
            }
            if (playingNow || a->version != seenVersion || tick % 10 == 0) { seenVersion = a->version; repaint(); }
        }

        // the card's corner: its size (saved with the patch); measured in the
        // panel's coordinates, whose top-left stays put while the card grows
        struct Grip final : juce::Component
        {
            Grip() { setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor); }
            void paint (juce::Graphics& g) override
            {
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                for (int i = 0; i < 3; ++i) g.drawLine ((float) getWidth() - 3.0f - 4.0f * (float) i, (float) getHeight() - 2.0f, (float) getWidth() - 2.0f, (float) getHeight() - 3.0f - 4.0f * (float) i, 1.2f);
            }
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (auto* p = getParentComponent()) grabOffset = getBounds().getBottomRight() - e.getEventRelativeTo (p).getPosition();
            }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                auto* v = dynamic_cast<ArrangerView*> (getParentComponent());
                if (v == nullptr || v->arr() == nullptr) return;
                const auto br = e.getEventRelativeTo (v).getPosition() + grabOffset;
                const int w = juce::jlimit (FruitArrangerModule::kMinW, FruitArrangerModule::kMaxW, br.x + 20);   // (the card: the panel and 10 px a side)
                const int h = juce::jlimit (FruitArrangerModule::kMinH, FruitArrangerModule::kMaxH, br.y);
                if (w == v->arr()->preferredModuleWidth() && h == v->arr()->extraContentHeight()) return;
                v->arr()->setParameter ("viewW", (float) w);
                v->arr()->setParameter ("viewH", (float) h);
                if (v->paramsChangedByContent) v->paramsChangedByContent();
            }
            juce::Point<int> grabOffset;
        };

        juce::WeakReference<SynthModule> module;
        juce::TextButton lookButton { "Fruit" };
        juce::TextButton playButton { "Play" }, startButton { "|<" }, loopButton { "Loop" }, zoomOut { "-" }, zoomIn { "+" },
                         rescanButton { "Rescan" }, modulateButton { "Modulate Parameter" }, loadButton { "Load Sample" };
        Grip grip;
        double playFrom { 0.0 };
        Kind highlightKind { Kind::Pattern };
        int highlightItem { 0 }, selectedPill { -1 };
    };
}

std::unique_ptr<juce::Component> FruitArrangerModule::createExtraContentComponent()
{
    return std::make_unique<ArrangerView> (*this);
}

AQUANODE_REGISTER_MODULE (FruitArrangerModule, FruitArrangerModule::descriptor)

//==============================================================================
// for tests
static FruitArrangerModule* asArranger (SynthModule* m) { return dynamic_cast<FruitArrangerModule*> (m); }
bool fruitArrangerTestAutomate (SynthModule* m, int moduleId, const juce::String& paramId, std::vector<juce::Point<float>> pts)
{
    auto* a = asArranger (m);
    if (a == nullptr || a->owner == nullptr) return false;
    auto* inst = a->owner->getInstance (moduleId);
    if (inst == nullptr) return false;
    int idx = -1;
    for (int k = 0; k < (int) inst->descriptor().params.size(); ++k) if (inst->descriptor().params[(size_t) k].id == paramId) idx = k;
    if (! a->addAutomation (moduleId, idx, inst->descriptor().displayName + ": " + paramId, 0.0)) return false;
    a->autos.back().points = std::move (pts);
    a->publish();
    return true;
}
bool fruitArrangerTestSample (SynthModule* m, const juce::AudioBuffer<float>& audio, double atBeat, int track)
{
    auto* a = asArranger (m);
    if (a == nullptr || ! a->addSample ("test", audio)) return false;
    a->placePill (Kind::Sample, (int) a->samples.size(), track, atBeat);
    return true;
}
void fruitArrangerTestMute (SynthModule* m, int track, bool mute)
{
    if (auto* a = asArranger (m); a != nullptr && track >= 0 && track < (int) a->tracks.size()) { a->tracks[(size_t) track].mute = mute; a->publish(); }
}
// the pills of an item replaced by one (length 0: none)
void fruitArrangerTestPill (SynthModule* m, int kind, int item, double start, double length, int track)
{
    auto* a = asArranger (m);
    if (a == nullptr) return;
    a->pills.erase (std::remove_if (a->pills.begin(), a->pills.end(), [&] (const Pill& p) { return (int) p.kind == kind && p.item == item; }), a->pills.end());
    if (length > 0.0) a->placePill ((Kind) kind, item, track, start, length);
    else a->publish();
}
juce::String fruitArrangerTestProblems (SynthModule* m)
{
    juce::StringArray s;
    if (auto* a = asArranger (m))
    {
        for (const auto& p : a->patterns) if (! p.valid) s.add ("pattern " + juce::String (p.number) + ": " + p.problem);
        for (const auto& au : a->autos) if (! au.valid) s.add ("automation " + juce::String (au.number) + ": " + au.problem);
        s.addArray (a->skipped);
    }
    return s.isEmpty() ? juce::String ("all clean") : s.joinIntoString ("; ");
}
double fruitArrangerTestEnd (SynthModule* m) { auto* a = asArranger (m); return a != nullptr ? a->songEnd() : 0.0; }
void fruitArrangerTestSeek (SynthModule* m, double beat) { if (auto* a = asArranger (m)) a->seek (beat); }
void fruitArrangerTestAddPill (SynthModule* m, int kind, int item, double start, int track)
{
    if (auto* a = asArranger (m)) a->placePill ((Kind) kind, item, track, start);
}
// (6.9.14) the piano rolls that are its patterns (valid or crossed out)
int fruitArrangerTestPatternCount (SynthModule* m, bool validOnly)
{
    int n = 0;
    if (auto* a = asArranger (m)) for (const auto& p : a->patterns) if (! validOnly || p.valid) ++n;
    return n;
}
