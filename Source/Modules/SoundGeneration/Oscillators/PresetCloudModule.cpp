// Preset Cloud - the presets of the synth plugins as a map of coloured dots:
// similar sounds lie close together (basses with basses, leads with leads),
// a tap on a dot plays it. Like the Random Plugin Preset module, but laid out.
//
// The dots: each synth's stock presets (the programs it ships with) and the
// random presets of the latest Roll (Per Roll of them, shared fairly over the
// synths and their kinds of preset). Nothing is listened to: every preset is
// read off its plugin's knobs (drive, filter, attack, release, movement,
// width, space ... found by their names) and its kind and name, made into a
// fingerprint and spread out in 2D (t-SNE) so neighbours sound alike. Colour:
// red harsh / distorted leads, blue gentle sweeps and pads, green natural
// (plucks, keys, acoustic), violet subtle and soft, brown basses.
//
// Show narrows the map to one synth; there a line drawn from one dot to
// another connects them and Morph glides that synth's knobs from the first
// preset to the second (switch-like knobs flip halfway; what a plugin keeps
// outside its knobs - samples, wavetables - stays the first preset's).
//
// The plugin plays inside like in Random Plugin Preset (a hosted card that
// never shows on the canvas). The patch keeps the map, the dots of the last
// Roll (each with the whole plugin state, so it plays back exactly) and what
// is playing.
// Global. Input: 0 = Midi In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../../Plugins/Host/HostedPlugin.h"
#include "../../../Plugins/Host/PluginCatalog.h"
#include "../../../Plugins/Host/RandomPresetSources.h"
#include <deque>
#include <map>
#include <random>
#include <set>

using namespace aquanode;
namespace rp = aquanode::plugins::randompreset;

namespace presetcloud
{
    enum Colour { cHarsh = 0, cSweep, cNatural, cSubtle, cBass, kNumColours };

    inline juce::Colour colourOf (int c)
    {
        static const juce::uint32 cols[] = { 0xffff5a52, 0xff5aa8ff, 0xff5fd477, 0xffb48cff, 0xffb07a4a };
        return juce::Colour (cols[juce::jlimit (0, 4, c)]);
    }
    inline const char* colourName (int c)
    {
        static const char* n[] = { "Harsh", "Sweep", "Natural", "Subtle", "Bass" };
        return n[juce::jlimit (0, 4, c)];
    }

    constexpr int kFeatures = 10;   // harsh, sweep, bright, reso, attack, release, movement, width, space, glide

    struct Point
    {
        juce::String pluginId, pluginName, name, kind;
        bool stock { false };
        int program { -1 };                 // stock: the plugin's program
        juce::MemoryBlock state;            // random: the whole plugin state (6.9.15: once played - till then its recipe)
        int rollKind { -1 };                // (6.9.15) random: the recipe it is rolled again from when played -
        juce::uint32 seed { 0 };            //   its kind and seed, on the synth's defaults
        std::vector<float> params;          // the knobs, 0..1 (for Morph)
        std::array<float, kFeatures> feat {};
        float classScore[kNumColours] {};
        int colour { cNatural };
        float x { 0.5f }, y { 0.5f };
    };

    //==========================================================================
    // the fingerprint: knobs found by their names
    inline void fingerprint (juce::AudioProcessor& p, Point& pt)
    {
        pt.params.clear();
        struct Acc { float sum { 0.0f }; int n { 0 }; void add (float v) { sum += v; ++n; } float get (float d = 0.5f) const { return n > 0 ? sum / (float) n : d; } };
        Acc drive, cutoff, reso, attack, release, lfo, width, space, glide;
        for (auto* prm : p.getParameters())
        {
            const float v = prm->getValue();
            pt.params.push_back (v);
            const auto n = prm->getName (64).toLowerCase();
            auto has = [&n] (const char* w) { return n.contains (w); };
            if (has ("drive") || has ("dist") || has ("satur") || has ("fold") || has ("crush") || has ("fuzz") || has ("overdrive") || has ("bitred"))
                drive.add (v);
            if ((has ("cutoff") || has ("cut off")) || (has ("freq") && (has ("filter") || has ("flt") || has ("vcf") || has ("lpf"))))
                cutoff.add (v);
            if (has ("reso") || has ("emphasis"))
                reso.add (v);
            if (has ("attack") || has ("atk"))
                attack.add (v);
            if (has ("release") || has ("rel ") || has ("decay"))
                release.add (v);
            if ((has ("lfo") || has ("mod")) && (has ("amount") || has ("depth") || has ("amt") || has ("rate")))
                lfo.add (v);
            if (has ("detune") || has ("unison") || has ("spread") || has ("chorus") || has ("width") || has ("stereo"))
                width.add (v);
            if ((has ("reverb") || has ("delay") || has ("echo") || has ("verb")) && (has ("mix") || has ("wet") || has ("amount") || has ("level") || has ("send")))
                space.add (v);
            if (has ("glide") || has ("porta"))
                glide.add (v);
        }
        const float d = drive.get (0.15f), c = cutoff.get (0.6f), r = reso.get (0.25f), a = attack.get (0.15f),
                    rl = release.get (0.35f), m = lfo.get (0.25f), w = width.get (0.3f), s = space.get (0.2f), g = glide.get (0.0f);
        pt.feat = { juce::jlimit (0.0f, 1.0f, d * 0.65f + c * 0.25f + r * 0.1f),
                    juce::jlimit (0.0f, 1.0f, a * 0.45f + m * 0.3f + rl * 0.25f),
                    c, r, a, rl, m, w, s, g };

        // what the kind and the name say
        const auto words = (pt.kind + " " + pt.name).toLowerCase();
        auto any = [&words] (std::initializer_list<const char*> l) { for (auto* w : l) if (words.contains (w)) return true; return false; };
        for (auto& v : pt.classScore) v = 0.0f;
        if (any ({ "bass", "sub", "808", "reese", "low end", "wobble" })) pt.classScore[cBass] = 1.0f;
        if (any ({ "lead", "solo", "scream", "growl", "distort", "dirty", "acid", "hard", "aggress", "saw lead", "rave" })) pt.classScore[cHarsh] = 1.0f;
        if (any ({ "pad", "sweep", "atmos", "drone", "evolv", "swell", "rise", "motion", "morph" })) pt.classScore[cSweep] = 1.0f;
        if (any ({ "pluck", "key", "piano", "epiano", "e-piano", "bell", "organ", "guitar", "string", "brass", "flute", "marimba",
                   "harp", "choir", "mallet", "clav", "vibes", "wood", "acoust", "horn", "sax", "violin" })) pt.classScore[cNatural] = 1.0f;
        if (any ({ "soft", "ambient", "texture", "whisper", "air", "gentle", "calm", "subtle", "dream", "quiet", "mellow", "warm" })) pt.classScore[cSubtle] = 1.0f;

        // the colour: what the words say first, the knobs decide the rest
        const float harsh = pt.feat[0], sweep = pt.feat[1];
        if (pt.classScore[cBass] > 0.0f) pt.colour = cBass;
        else if (pt.classScore[cHarsh] > 0.0f || harsh > 0.55f) pt.colour = cHarsh;
        else if (pt.classScore[cSweep] > 0.0f || sweep > 0.5f) pt.colour = cSweep;
        else if (pt.classScore[cNatural] > 0.0f) pt.colour = cNatural;
        else if (pt.classScore[cSubtle] > 0.0f || (harsh < 0.25f && c < 0.45f)) pt.colour = cSubtle;
        else pt.colour = cNatural;
    }

    // the numbers the map is laid out by
    inline std::vector<float> layoutVector (const Point& p, int pluginIndex, int numPlugins)
    {
        std::vector<float> v (p.feat.begin(), p.feat.end());
        for (int c = 0; c < kNumColours; ++c) v.push_back (c == p.colour ? 0.4f : 0.0f);   // (enough to group, not to split the map apart)
        // a little of which synth it is: its presets lean together inside a cluster
        const float ang = 6.2831853f * (float) pluginIndex / (float) juce::jmax (1, numPlugins);
        v.push_back (0.12f * std::cos (ang));
        v.push_back (0.12f * std::sin (ang));
        return v;
    }

    //==========================================================================
    // t-SNE (exact; a few hundred points): neighbours in the fingerprint stay
    // neighbours on the map. Positions come back in 0..1.
    inline void tsne (const std::vector<std::vector<float>>& X, std::vector<float>& ox, std::vector<float>& oy,
                      const std::atomic<bool>& stop, uint32_t seed = 7)
    {
        const int n = (int) X.size();
        ox.assign ((size_t) n, 0.5f); oy.assign ((size_t) n, 0.5f);
        if (n < 3) { for (int i = 0; i < n; ++i) ox[(size_t) i] = 0.3f + 0.4f * (float) i; return; }
        std::vector<double> D ((size_t) n * (size_t) n), P ((size_t) n * (size_t) n, 0.0);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
            {
                double s = 0.0;
                for (size_t k = 0; k < X[(size_t) i].size(); ++k) { const double d = X[(size_t) i][k] - X[(size_t) j][k]; s += d * d; }
                D[(size_t) (i * n + j)] = s;
            }
        const double perplexity = juce::jlimit (3.0, 25.0, (n - 1) / 3.0);
        const double logU = std::log (perplexity);
        for (int i = 0; i < n && ! stop.load(); ++i)
        {
            double beta = 1.0, lo = 0.0, hi = 1.0e30;
            for (int it = 0; it < 60; ++it)
            {
                double sum = 0.0, h = 0.0;
                for (int j = 0; j < n; ++j)
                {
                    const double p = j == i ? 0.0 : std::exp (-beta * D[(size_t) (i * n + j)]);
                    P[(size_t) (i * n + j)] = p;
                    sum += p;
                }
                if (sum <= 0.0) sum = 1.0e-12;
                for (int j = 0; j < n; ++j) h += beta * D[(size_t) (i * n + j)] * P[(size_t) (i * n + j)];
                h = h / sum + std::log (sum);
                for (int j = 0; j < n; ++j) P[(size_t) (i * n + j)] /= sum;
                if (std::abs (h - logU) < 1.0e-4) break;
                if (h > logU) { lo = beta; beta = hi >= 1.0e29 ? beta * 2.0 : 0.5 * (beta + hi); }
                else { hi = beta; beta = 0.5 * (beta + lo); }
            }
        }
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
            {
                const double s = juce::jmax (1.0e-12, (P[(size_t) (i * n + j)] + P[(size_t) (j * n + i)]) / (2.0 * n));
                P[(size_t) (i * n + j)] = P[(size_t) (j * n + i)] = s;
            }

        juce::Random rnd ((juce::int64) seed);
        std::vector<double> y ((size_t) n * 2), g ((size_t) n * 2), vel ((size_t) n * 2, 0.0), gains ((size_t) n * 2, 1.0), q ((size_t) n * (size_t) n);
        for (auto& v : y) v = rnd.nextDouble() * 1.0e-2 - 5.0e-3;
        const int iters = 450;
        const double eta = juce::jmax (50.0, n / 12.0);
        for (int it = 0; it < iters && ! stop.load(); ++it)
        {
            const double exag = it < 100 ? 12.0 : 1.0;
            const double mom = it < 120 ? 0.5 : 0.8;
            double qsum = 0.0;
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                {
                    const double dx = y[(size_t) (2 * i)] - y[(size_t) (2 * j)], dy = y[(size_t) (2 * i + 1)] - y[(size_t) (2 * j + 1)];
                    const double v = 1.0 / (1.0 + dx * dx + dy * dy);
                    q[(size_t) (i * n + j)] = q[(size_t) (j * n + i)] = v;
                    qsum += 2.0 * v;
                }
            std::fill (g.begin(), g.end(), 0.0);
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                {
                    if (i == j) continue;
                    const double qq = q[(size_t) (i * n + j)];
                    const double m = (exag * P[(size_t) (i * n + j)] - qq / qsum) * qq;
                    g[(size_t) (2 * i)] += 4.0 * m * (y[(size_t) (2 * i)] - y[(size_t) (2 * j)]);
                    g[(size_t) (2 * i + 1)] += 4.0 * m * (y[(size_t) (2 * i + 1)] - y[(size_t) (2 * j + 1)]);
                }
            for (size_t k = 0; k < y.size(); ++k)
            {
                gains[k] = (g[k] > 0.0) != (vel[k] > 0.0) ? gains[k] + 0.2 : juce::jmax (0.01, gains[k] * 0.8);
                vel[k] = mom * vel[k] - eta * gains[k] * g[k];
                y[k] += vel[k];
            }
        }
        double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
        for (int i = 0; i < n; ++i)
        {
            minX = juce::jmin (minX, y[(size_t) (2 * i)]); maxX = juce::jmax (maxX, y[(size_t) (2 * i)]);
            minY = juce::jmin (minY, y[(size_t) (2 * i + 1)]); maxY = juce::jmax (maxY, y[(size_t) (2 * i + 1)]);
        }
        const double sx = juce::jmax (1.0e-9, maxX - minX), sy = juce::jmax (1.0e-9, maxY - minY);
        for (int i = 0; i < n; ++i)
        {
            ox[(size_t) i] = (float) (0.06 + 0.88 * (y[(size_t) (2 * i)] - minX) / sx);
            oy[(size_t) i] = (float) (0.06 + 0.88 * (y[(size_t) (2 * i + 1)] - minY) / sy);
        }
    }

    // The map made denser without losing its clusters: the empty space between
    // the t-SNE islands partly closed (a quarter of each position is its rank,
    // so far-apart islands move closer), then dots that would overlap pushed
    // apart a little at a time - organic, touching blobs of similar sounds
    // rather than a grid. Returns the spacing the dots are kept apart by.
    inline float relaxLayout (std::vector<float>& xs, std::vector<float>& ys)
    {
        const int n = (int) xs.size();
        if (n < 4) return 0.2f;
        auto ranks = [n] (const std::vector<float>& v)
        {
            std::vector<int> idx ((size_t) n);
            for (int i = 0; i < n; ++i) idx[(size_t) i] = i;
            std::sort (idx.begin(), idx.end(), [&v] (int a, int b) { return v[(size_t) a] < v[(size_t) b]; });
            std::vector<float> r ((size_t) n);
            for (int k = 0; k < n; ++k) r[(size_t) idx[(size_t) k]] = (float) k / (float) (n - 1);
            return r;
        };
        const auto rx = ranks (xs), ry = ranks (ys);
        for (int i = 0; i < n; ++i)
        {
            xs[(size_t) i] = 0.75f * xs[(size_t) i] + 0.25f * (0.06f + 0.88f * rx[(size_t) i]);
            ys[(size_t) i] = 0.75f * ys[(size_t) i] + 0.25f * (0.06f + 0.88f * ry[(size_t) i]);
        }
        // no two dots closer than about 80 % of an even spacing
        const float spacing = std::sqrt (0.7744f / (float) n);
        const float minD = spacing * 0.8f;
        for (int it = 0; it < 60; ++it)
        {
            bool moved = false;
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                {
                    float dx = xs[(size_t) j] - xs[(size_t) i], dy = ys[(size_t) j] - ys[(size_t) i];
                    float d = std::sqrt (dx * dx + dy * dy);
                    if (d >= minD) continue;
                    if (d < 1.0e-6f) { dx = 1.0e-3f * (float) ((i * 7 + j * 13) % 11 - 5); dy = 1.0e-3f; d = std::sqrt (dx * dx + dy * dy); }
                    const float push = 0.5f * (minD - d) / d * 0.6f;
                    xs[(size_t) i] -= dx * push; ys[(size_t) i] -= dy * push;
                    xs[(size_t) j] += dx * push; ys[(size_t) j] += dy * push;
                    moved = true;
                }
            for (int i = 0; i < n; ++i)
            {
                xs[(size_t) i] = juce::jlimit (0.04f, 0.96f, xs[(size_t) i]);
                ys[(size_t) i] = juce::jlimit (0.04f, 0.96f, ys[(size_t) i]);
            }
            if (! moved) break;
        }
        return minD;
    }

    // the stock presets read once per run of the app (they never change)
    struct StockCache
    {
        std::map<juce::String, std::vector<Point>> byPlugin;
        std::set<juce::String> done;
    };
    inline StockCache& stockCache()
    {
        static StockCache c;
        return c;
    }

    // the synths: the catalog's plugins whose card is a sound generator
    inline std::vector<const plugins::CatalogEntry*> synths()
    {
        std::vector<const plugins::CatalogEntry*> v;
        for (const auto& e : plugins::catalog())
        {
            // not preset synths: a whole Aquanode inside, a whole DAW, a drum machine
            if (e.id.containsIgnoreCase ("aquanode") || e.id.containsIgnoreCase ("fruit") || e.id == "covegroove")
                continue;
            if (auto* r = ModuleFactory::instance().find (e.guiTypeId))
                if (r->descriptor.section == ModuleSection::Oscillator)
                    v.push_back (&e);
        }
        return v;
    }

    constexpr int kMaxStockPerPlugin = 48;
}

//==============================================================================
class PresetCloudModule final : public SynthModule,
                                public plugins::randompreset::PluginPresetHolder,
                                private juce::Timer
{
public:
    enum ParamIndex { pPerRoll = 0, pRoll, pMorph, pSmoother, pMode, pClarity };   // (Roll: the card's button, 6.7.5; Smoother 6.7.6; Mode, Clarity 6.9.12)
    static constexpr int kPerRoll[] = { 25, 50, 100, 200, 400 };

    PresetCloudModule() : layout (*this)
    {
        // (6.9.12) the Cepstral Morph mode's two cores: A heard through B's spectral envelope, and B through A's
        cepAB = ModuleFactory::instance().createInstance ("fx.speccepmorph");
        cepBA = ModuleFactory::instance().createInstance ("fx.speccepmorph");
        layout.startThread (juce::Thread::Priority::low);
        startTimerHz (30);
    }

    ~PresetCloudModule() override
    {
        stopTimer();
        layout.stopFlag.store (true);
        layout.signalThreadShouldExit();
        layout.notify();
        layout.stopThread (8000);
        std::shared_ptr<SynthModule> old, oldB;
        {
            const juce::SpinLock::ScopedLockType sl (swapLock);
            old = std::move (inner);
            oldB = std::move (innerB);
        }
    }

    //=== the map (message thread) =============================================
    std::vector<presetcloud::Point> points;
    juce::String showOnly;                    // "" = every synth
    int chosen { -1 };                        // the dot playing
    int morphA { -1 }, morphB { -1 };         // a connection (Show one synth)
    juce::String status;
    std::atomic<int> changeCounter { 0 };

    bool busy() const { return ! jobs.empty() || layout.busy.load() || layoutWanted; }
    float progress() const { return jobsTotal > 0 ? 1.0f - (float) jobs.size() / (float) jobsTotal : 1.0f; }

    // Roll: new random presets (Per Roll of them, fairly over synths and kinds)
    // the card's Roll button (between Per Roll and Morph)
    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "roll")
            roll();
    }

    void roll()
    {
        points.erase (std::remove_if (points.begin(), points.end(), [] (const auto& p) { return ! p.stock; }), points.end());
        chosen = morphA = morphB = -1;
        queueStock();
        const int count = kPerRoll[juce::jlimit (0, 4, (int) getParameterBase (pPerRoll))];
        // the synths that can roll, and their kinds (their "Anything" left out
        // where they have others): taken round-robin
        struct Slot { juce::String id; std::vector<int> kinds; size_t next { 0 }; };
        std::vector<Slot> slots;
        for (auto* s : rp::sources())
        {
            if (! showOnly.isEmpty() && s->pluginId != showOnly) continue;
            const auto& l = rp::listsOf (*s);
            Slot sl; sl.id = s->pluginId;
            for (int k = 0; k < l.kinds.size(); ++k)
                if (k != l.defaultKind && ! l.kinds[k].containsIgnoreCase ("random") && ! l.kinds[k].startsWithIgnoreCase ("Any ")) sl.kinds.push_back (k);
            if (sl.kinds.empty()) sl.kinds.push_back (l.defaultKind);
            juce::Random r; std::shuffle (sl.kinds.begin(), sl.kinds.end(), std::mt19937 ((unsigned) r.nextInt()));
            slots.push_back (std::move (sl));
        }
        for (int i = 0; i < count && ! slots.empty(); ++i)
        {
            auto& sl = slots[(size_t) i % slots.size()];
            Job j;
            j.kind = Job::Roll;
            j.pluginId = sl.id;
            j.rollKind = sl.kinds[sl.next++ % sl.kinds.size()];
            j.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt (0x7fffffff) | 1u;
            jobs.push_back (j);
        }
        jobs.push_back ({ Job::Layout });
        jobsTotal = (int) jobs.size();
        status = "Rolling " + juce::String (count) + " presets ...";
        configured = true;
        ++changeCounter;
    }

    void setShowOnly (const juce::String& id)
    {
        showOnly = id;
        disconnect();   // (the line goes, and its second copy with it)
        ++changeCounter;
    }

    bool visible (const presetcloud::Point& p) const { return showOnly.isEmpty() || p.pluginId == showOnly; }

    // a tap on a dot: it plays (and a connection goes)
    void play (int index)
    {
        if (index < 0 || index >= (int) points.size()) return;
        morphA = morphB = -1;
        setInnerB (nullptr);
        crossSynth.store (false);
        loadPoint (index);
    }

    // A line from the lit dot to another (6.7.6: any two dots). Two copies
    // play, each loaded whole with its own preset (nothing of an earlier sound
    // left in them). The same synth at both ends: Morph moves every knob from
    // one preset to the other on both copies (a "patchfade"); the switch-like
    // settings, and whatever a preset holds besides its knobs, change at the
    // middle, where the copy holding B's takes over - at once with Smoother
    // at 0, crossfaded over a wider middle as Smoother turns up. Two synths:
    // a plain (equal-power) crossfade of the two presets.
    void connect (int a, int b)
    {
        if (a < 0 || b < 0 || a == b || a >= (int) points.size() || b >= (int) points.size()) return;
        auto& pa = points[(size_t) a];
        auto& pb = points[(size_t) b];
        // both copies new (no tail or setting of an earlier sound in either),
        // each set up for the audio first and then given its preset whole (a
        // synth may reset some of its sound when it is set up)
        auto first = makeLoaded (pa);
        auto second = makeLoaded (pb);
        if (first == nullptr || second == nullptr) { ++changeCounter; return; }
        setInner (std::move (first), false);
        playingPlugin = pa.pluginId;
        setInnerB (std::move (second), false);
        chosen = a;
        morphA = a; morphB = b;
        crossSynth.store (pa.pluginId != pb.pluginId);
        retriggerAuto.store (true);
        lastMorph = -1.0f;   // (the knobs set on the next tick)
        status = pa.name + "  ->  " + pb.name + (crossSynth.load() ? "   (two synths: Morph crossfades)" : "   (Morph fades every knob)");
        ++changeCounter;
    }

    bool connected() const
    {
        return morphA >= 0 && morphB >= 0 && morphA < (int) points.size() && morphB < (int) points.size();
    }

    // Disconnect: the line goes, the lit dot plays alone again
    void disconnect()
    {
        if (! connected()) return;
        const int a = morphA;
        play (a);
    }

    // Open Preset / Open Position: the plugins and states to open as cards -
    // the lit dot's preset; with a line, the sound at Morph's position (the
    // same synth: the copy heard most, its knobs where Morph has them; two
    // synths: both presets)
    std::vector<std::pair<juce::String, juce::MemoryBlock>> statesToOpen()
    {
        std::vector<std::pair<juce::String, juce::MemoryBlock>> v;
        if (connected())
        {
            auto stateOf = [] (SynthModule* mod, juce::MemoryBlock& mb)
            {
                if (auto* h = dynamic_cast<plugins::HostedPluginModule*> (mod))
                    if (auto* p = h->getPlugin()) { p->getStateInformation (mb); return true; }
                return false;
            };
            const auto& pa = points[(size_t) morphA];
            const auto& pb = points[(size_t) morphB];
            const float m = juce::jlimit (0.0f, 1.0f, param (pMorph) * 0.01f);
            juce::MemoryBlock sa, sb;
            if (! crossSynth.load())
            {
                const bool second = m >= 0.5f;
                if (stateOf (second ? innerB.get() : inner.get(), sa))
                    v.push_back ({ pa.pluginId, sa });
            }
            else
            {
                if (stateOf (inner.get(), sa)) v.push_back ({ pa.pluginId, sa });
                if (stateOf (innerB.get(), sb)) v.push_back ({ pb.pluginId, sb });
            }
            return v;
        }
        if (chosen >= 0 && chosen < (int) points.size())
        {
            auto& pt = points[(size_t) chosen];
            ensureLoaded (pt);
            juce::MemoryBlock mb;
            if (auto* p = playingProcessor()) p->getStateInformation (mb);   // (the preset as it plays)
            else mb = pt.state;
            v.push_back ({ pt.pluginId, mb });
        }
        return v;
    }

    //=== PluginPresetHolder ===================================================
    juce::String heldPluginId() const override { return inner != nullptr ? playingPlugin : juce::String(); }
    juce::AudioProcessor* heldPlugin() const override { return playingProcessor(); }

    //=== SynthModule ==========================================================
    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        const juce::SpinLock::ScopedLockType sl (swapLock);
        if (inner != nullptr) inner->prepare (sr);
        if (innerB != nullptr) innerB->prepare (sr);
        for (auto* c : { cepAB.get(), cepBA.get() }) if (c != nullptr) c->prepare (sr);
        prepared = true;
    }
    void reset() override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (! sl.isLocked()) return;
        if (inner != nullptr) inner->reset();
        if (innerB != nullptr) innerB->reset();
        for (auto* c : { cepAB.get(), cepBA.get() }) if (c != nullptr) c->reset();
    }
    // (6.9.12) Cepstral Morph: the cores' FFT frames - while a line is drawn in that mode
    int latencySamples() const override
    {
        return cepstralMode() && connected() && cepAB != nullptr ? cepAB->latencySamples() : 0;
    }
    bool cepstralMode() const { return getParameterBase (pMode) > 0.5f; }
    void blockStart() override
    {
        {
            const float m = juce::jlimit (0.0f, 1.0f, param (pMorph) * 0.01f);
            const float s = juce::jlimit (0.0f, 1.0f, param (pSmoother) * 0.01f);
            if (crossSynth.load (std::memory_order_relaxed))
                xfadeTarget = m;
            else if (s < 0.005f)
                xfadeTarget = m >= 0.5f ? 1.0f : 0.0f;
            else
                xfadeTarget = juce::jlimit (0.0f, 1.0f, (m - 0.5f) / s + 0.5f);
            xfadeCoeff = 1.0f - std::exp (-1.0f / (0.008f * (float) juce::jmax (8000.0, sampleRate)));
            // (6.9.12) Cepstral Morph: the preset nearer the line's position is the carrier, the
            // other lends it its spectral envelope - not at all at the ends, fully at the middle,
            // where the two directions cross over (Smoother widens that handover)
            cepstralNow = cepstralMode() && connected() && cepAB != nullptr && cepBA != nullptr;
            if (cepstralNow)
            {
                const float strength = 100.0f * (1.0f - std::abs (2.0f * m - 1.0f));
                const float clarity = param (pClarity);
                for (auto* c : { cepAB.get(), cepBA.get() })
                {
                    c->setParameter ("morph", strength);
                    c->setParameter ("clarity", clarity);
                    c->setParameter ("maxBoost", 36.0f);   // (no 72 dB peaks out of a quiet band)
                    c->blockStart();
                }
                const float half = 0.03f + 0.3f * s;
                cepTarget = juce::jlimit (0.0f, 1.0f, (m - (0.5f - half)) / (2.0f * half));
                cepMorphNow = m;
                levelCoeff = 1.0f - std::exp (-1.0f / (0.3f * (float) juce::jmax (8000.0, sampleRate)));
            }
        }
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (! sl.isLocked()) return;
        for (auto* m : { inner.get(), innerB.get() })
        {
            if (m == nullptr) continue;
            m->setHostPlayHead (hostPlayHead);
            m->setTempo (tempoBpm);
            m->setHostTransport (hostTransport);
            m->setMidiControls (midiControls);
            m->setHostModValues (hostModValues);
            m->blockStart();
        }
        // Autoplay: C3 held, struck again every bar (a pluck would die away)
        const bool want = autoplay.load (std::memory_order_relaxed);
        const double bar = 4.0 * 60.0 / juce::jlimit (20.0, 400.0, tempoBpm) * sampleRate;
        const bool restrike = want && (retriggerAuto.exchange (false) || (double) autoSamples >= bar);
        if ((autoSounding && (! want || restrike)) )
        {
            sendToBoth (juce::MidiMessage::noteOff (1, kAutoNote));
            autoSounding = false;
        }
        if (want && ! autoSounding)
        {
            sendToBoth (juce::MidiMessage::noteOn (1, kAutoNote, (juce::uint8) 100));
            autoSounding = true;
            autoSamples = 0;
        }
    }
    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        ++autoSamples;
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (! sl.isLocked() || inner == nullptr) { outputs[0] = { 0.0f, 0.0f }; return; }
        inner->processSample (inputs, outputs);
        if (innerB == nullptr) { xfade = 0.0f; return; }
        // which copy is heard: two synths - Morph itself; the same synth - the
        // second copy from the middle on (Smoother widens the handover into a
        // crossfade). Always eased over ~8 ms, so a handover never clicks.
        StereoFrame b[8] {};
        innerB->processSample (inputs, b);
        if (cepstralNow)
        {
            // both directions run all the time (an FFT core needs its frames full before it is heard)
            const StereoFrame inAB[2] { outputs[0], b[0] }, inBA[2] { b[0], outputs[0] };
            StereoFrame oAB[4] {}, oBA[4] {};
            cepAB->processSample (inAB, oAB);
            cepBA->processSample (inBA, oBA);
            cepX += (cepTarget - cepX) * xfadeCoeff;
            const float ga = std::cos (cepX * 1.5707963f), gb = std::sin (cepX * 1.5707963f);
            StereoFrame y { oAB[0][0] * ga + oBA[0][0] * gb, oAB[0][1] * ga + oBA[0][1] * gb };
            // as loud as the two presets crossfaded at the same place would be: both levels
            // followed over ~300 ms, the morph's made up to the crossfade's (within -12..+12 dB)
            {
                const float m = cepMorphNow;
                const float ca = std::cos (m * 1.5707963f), cb = std::sin (m * 1.5707963f);
                const float rl = outputs[0][0] * ca + b[0][0] * cb, rr = outputs[0][1] * ca + b[0][1] * cb;
                refEnergy += (0.5f * (rl * rl + rr * rr) - refEnergy) * levelCoeff;
                outEnergy += (0.5f * (y[0] * y[0] + y[1] * y[1]) - outEnergy) * levelCoeff;
                const float want = outEnergy > 1.0e-12f ? std::sqrt (refEnergy / outEnergy) : 1.0f;
                const float g = juce::jlimit (0.25f, 4.0f, want);
                levelGain += (g - levelGain) * levelCoeff * 4.0f;
            }
            outputs[0] = { y[0] * levelGain, y[1] * levelGain };
            return;
        }
        xfade += (xfadeTarget - xfade) * xfadeCoeff;
        const float ga = std::cos (xfade * 1.5707963f), gb = std::sin (xfade * 1.5707963f);
        outputs[0] = { outputs[0][0] * ga + b[0][0] * gb, outputs[0][1] * ga + b[0][1] * gb };
    }
    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked()) sendToBothLocked (m);
    }
    void midiAllNotesOff() override
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (! sl.isLocked()) return;
        if (inner != nullptr) inner->midiAllNotesOff();
        if (innerB != nullptr) innerB->midiAllNotesOff();
        autoSounding = false;
    }

    // Autoplay (the card's switch): C3 held while it is on
    std::atomic<bool> autoplay { false };
    void setAutoplay (bool on) { autoplay.store (on); ++changeCounter; }
    bool connectMode { false };   // (the card's Connect switch - a drag from a dot draws a line)
    std::atomic<double> glidingUntil { 0.0 };   // (6.9.15) the card's highlighter glides till then: no jobs meanwhile

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 560; }
    int extraContentHeight() const override { return 470; }

    //=== the patch ============================================================
    juce::String saveCustomState() const override
    {
        juce::XmlElement root ("PresetCloud");
        root.setAttribute ("version", 1);
        root.setAttribute ("show", showOnly);
        root.setAttribute ("chosen", chosen);
        root.setAttribute ("morphA", morphA);
        root.setAttribute ("morphB", morphB);
        root.setAttribute ("playing", playingPlugin);
        root.setAttribute ("autoplay", autoplay.load() ? 1 : 0);
        for (const auto& p : points)
        {
            auto* e = root.createNewChildElement ("P");
            e->setAttribute ("plugin", p.pluginId);
            e->setAttribute ("name", p.name);
            e->setAttribute ("kind", p.kind);
            e->setAttribute ("stock", p.stock ? 1 : 0);
            e->setAttribute ("program", p.program);
            e->setAttribute ("colour", p.colour);
            e->setAttribute ("x", (double) p.x);
            e->setAttribute ("y", (double) p.y);
            if (! p.stock)
            {
                if (p.seed != 0) { e->setAttribute ("seed", (int) (p.seed & 0x7fffffff)); e->setAttribute ("rollKind", p.rollKind); }
                if (p.state.getSize() > 0) e->setAttribute ("state", p.state.toBase64Encoding());
                juce::MemoryBlock mb (p.params.data(), p.params.size() * sizeof (float));
                e->setAttribute ("params", mb.toBase64Encoding());
            }
        }
        if (inner != nullptr)
            root.createNewChildElement ("State")->addTextElement (inner->saveCustomState());
        // the whole of it compressed: the rolled states are much alike
        juce::MemoryBlock packed;
        {
            juce::MemoryOutputStream mo (packed, false);
            juce::GZIPCompressorOutputStream gz (mo, 6);
            gz << root.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
        }
        return "gz:" + packed.toBase64Encoding();
    }

    void loadCustomState (const juce::String& text) override
    {
        juce::String xmlText = text;
        if (text.startsWith ("gz:"))
        {
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (text.substring (3));
            juce::MemoryInputStream mi (mb, false);
            juce::GZIPDecompressorInputStream gz (mi);
            xmlText = gz.readEntireStreamAsString();
        }
        auto xml = juce::parseXML (xmlText);
        if (xml == nullptr || ! xml->hasTagName ("PresetCloud")) return;
        configured = true;
        points.clear();
        for (auto* e : xml->getChildWithTagNameIterator ("P"))
        {
            presetcloud::Point p;
            p.pluginId = e->getStringAttribute ("plugin");
            p.name = e->getStringAttribute ("name");
            p.kind = e->getStringAttribute ("kind");
            p.stock = e->getIntAttribute ("stock") != 0;
            p.program = e->getIntAttribute ("program", -1);
            p.colour = e->getIntAttribute ("colour");
            p.x = (float) e->getDoubleAttribute ("x", 0.5);
            p.y = (float) e->getDoubleAttribute ("y", 0.5);
            p.state.fromBase64Encoding (e->getStringAttribute ("state"));
            p.seed = (juce::uint32) e->getIntAttribute ("seed", 0);
            p.rollKind = e->getIntAttribute ("rollKind", -1);
            juce::MemoryBlock pm;
            pm.fromBase64Encoding (e->getStringAttribute ("params"));
            p.params.assign ((const float*) pm.getData(), (const float*) pm.getData() + pm.getSize() / sizeof (float));
            points.push_back (std::move (p));
        }
        showOnly = xml->getStringAttribute ("show");
        chosen = xml->getIntAttribute ("chosen", -1);
        morphA = xml->getIntAttribute ("morphA", -1);
        morphB = xml->getIntAttribute ("morphB", -1);
        const auto playing = xml->getStringAttribute ("playing");
        std::shared_ptr<SynthModule> fresh;
        if (playing.isNotEmpty())
            if ((fresh = makePlugin (playing)) != nullptr)
                fresh->loadCustomState (xml->getChildElementAllSubText ("State", {}));
        setInner (std::move (fresh));
        playingPlugin = inner != nullptr ? playing : juce::String();
        if (chosen >= 0 && chosen < (int) points.size())
            status = describe (points[(size_t) chosen]);
        autoplay.store (xml->getIntAttribute ("autoplay") != 0);
        lastMorph = -1.0f;
        setInnerB (nullptr);
        crossSynth.store (false);
        if (connected())
        {
            const int a = morphA, b = morphB;
            connect (a, b);   // (both copies again, at the saved Morph)
        }
        ++changeCounter;
    }

    bool isConfigured() const { return configured; }

   #if AQUANODE_TEST_HARNESS
    void testSetMorph (float m) { setParameter ("morph", m * 100.0f); lastMorph = m; applyMorph (m); }
    float testXfadeTarget() const { return xfadeTarget; }
   #endif

    juce::String describe (const presetcloud::Point& p) const
    {
        return p.pluginName.isNotEmpty() ? p.pluginName + ": " + p.name : p.pluginId + ": " + p.name;
    }

    juce::AudioProcessor* playingProcessor() const
    {
        auto* h = dynamic_cast<plugins::HostedPluginModule*> (inner.get());
        return h != nullptr ? h->getPlugin() : nullptr;
    }

private:
    //=== jobs, done a few at a time on the message thread (the plugins want it)
    struct Job
    {
        enum Kind { Stock, Roll, Layout } kind { Layout };
        juce::String pluginId;
        int rollKind { 0 };
        juce::uint32 seed { 1 };
    };
    std::deque<Job> jobs;
    int jobsTotal { 0 };
    std::map<juce::String, std::unique_ptr<juce::AudioProcessor>> scratch;   // offscreen copies of the plugins
    std::map<juce::String, juce::MemoryBlock> scratchDefaults;                // (6.9.15) their start-up states
    bool layoutWanted { false };

    void queueStock()
    {
        for (auto* e : presetcloud::synths())
            if (presetcloud::stockCache().done.count (e->id) == 0)
                jobs.push_front ({ Job::Stock, e->id });
    }

    juce::AudioProcessor* scratchFor (const juce::String& id)
    {
        auto it = scratch.find (id);
        if (it != scratch.end()) return it->second.get();
        auto p = plugins::createOriginal (id, 48000.0);
        auto* raw = p.get();
        scratch[id] = std::move (p);
        return raw;
    }

    static juce::String pluginNameOf (const juce::String& id)
    {
        if (auto* e = plugins::findEntry (id)) return e->name;
        return id;
    }

    void runJob (const Job& j)
    {
        if (j.kind == Job::Stock)
        {
            auto& cache = presetcloud::stockCache();
            cache.done.insert (j.pluginId);
            auto* p = scratchFor (j.pluginId);
            if (p == nullptr) return;
            const int n = p->getNumPrograms();
            if (n <= 1) { if (rp::find (j.pluginId) == nullptr) scratch.erase (j.pluginId); return; }
            std::vector<presetcloud::Point> list;
            juce::MemoryBlock defaults;
            p->getStateInformation (defaults);
            for (int i = 0; i < juce::jmin (n, presetcloud::kMaxStockPerPlugin); ++i)
            {
                // each program on the synth's defaults, as a fresh copy would
                // have it (a program sets only the knobs it names)
                if (defaults.getSize() > 0) p->setStateInformation (defaults.getData(), (int) defaults.getSize());
                p->setCurrentProgram (i);
                presetcloud::Point pt;
                pt.pluginId = j.pluginId;
                pt.pluginName = pluginNameOf (j.pluginId);
                pt.stock = true;
                pt.program = i;
                pt.name = p->getProgramName (i);
                if (pt.name.isEmpty()) pt.name = "Preset " + juce::String (i + 1);
                pt.kind = "stock";
                presetcloud::fingerprint (*p, pt);
                list.push_back (std::move (pt));
            }
            cache.byPlugin[j.pluginId] = std::move (list);
            // (only the synths that roll keep their offscreen copy for now)
            if (rp::find (j.pluginId) == nullptr) scratch.erase (j.pluginId);
        }
        else if (j.kind == Job::Roll)
        {
            auto* src = rp::find (j.pluginId);
            auto* p = scratchFor (j.pluginId);
            if (src == nullptr || p == nullptr) return;
            const auto& l = rp::listsOf (*src);
            // (6.9.15) every roll from the synth's defaults, so the dot can be rolled again the
            // same when it is played: only its recipe (kind, seed) and knobs are kept till then -
            // a hundred whole states (Vita's with their tables) took far too much memory
            auto& defaults = scratchDefaults[j.pluginId];
            if (defaults.getSize() == 0) p->getStateInformation (defaults);
            else p->setStateInformation (defaults.getData(), (int) defaults.getSize());
            presetcloud::Point pt;
            pt.pluginId = j.pluginId;
            pt.pluginName = src->name;
            pt.kind = j.rollKind < l.kinds.size() ? l.kinds[j.rollKind] : juce::String();
            pt.name = src->roll (*p, j.rollKind, l.defaultGenre, l.defaultEngine, j.seed);
            if (pt.name.isEmpty()) pt.name = pt.kind;
            pt.rollKind = j.rollKind;
            pt.seed = j.seed;
            presetcloud::fingerprint (*p, pt);
            points.push_back (std::move (pt));
        }
        else
        {
            // the stock presets join the rolled ones, then the map is laid out
            points.erase (std::remove_if (points.begin(), points.end(), [] (const auto& p) { return p.stock; }), points.end());
            for (const auto& kv : presetcloud::stockCache().byPlugin)
                for (const auto& pt : kv.second)
                    points.push_back (pt);
            scratch.clear();   // (the offscreen plugins are not needed any more)
            scratchDefaults.clear();
            layoutWanted = true;
        }
    }

    //=== laying out, in the background ========================================
    struct Layout : public juce::Thread
    {
        explicit Layout (PresetCloudModule& m) : juce::Thread ("Preset Cloud layout"), owner (m) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (-1);
                if (threadShouldExit()) break;
                std::vector<std::vector<float>> X;
                {
                    const juce::ScopedLock sl (lock);
                    if (! has) continue;
                    X = std::move (input);
                    has = false;
                }
                busy.store (true);
                stopFlag.store (false);
                std::vector<float> xs, ys;
                presetcloud::tsne (X, xs, ys, stopFlag);
                if (! stopFlag.load())
                    presetcloud::relaxLayout (xs, ys);
                {
                    const juce::ScopedLock sl (lock);
                    outX = std::move (xs); outY = std::move (ys);
                    done = ! stopFlag.load();
                }
                busy.store (false);
            }
        }
        PresetCloudModule& owner;
        juce::CriticalSection lock;
        std::vector<std::vector<float>> input;
        std::vector<float> outX, outY;
        bool has { false }, done { false };
        std::atomic<bool> busy { false }, stopFlag { false };
    };
    Layout layout;
    size_t layoutCount { 0 };

    void timerCallback() override
    {
        // a few jobs at a time, so the app keeps moving (6.9.15: 6 ms a tick, not 30 - the
        // highlighter's glide stays smooth while a Roll is made)
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const bool gliding = t0 < glidingUntil.load();   // (a card's highlighter on its way: the jobs wait)
        while (! gliding && ! jobs.empty() && juce::Time::getMillisecondCounterHiRes() - t0 < 6.0)
        {
            const auto j = jobs.front();
            jobs.pop_front();
            runJob (j);
            ++changeCounter;
        }
        if (layoutWanted && jobs.empty())
        {
            layoutWanted = false;
            std::map<juce::String, int> pluginIndex;
            for (const auto& p : points) pluginIndex.emplace (p.pluginId, (int) pluginIndex.size());
            std::vector<std::vector<float>> X;
            for (const auto& p : points)
            {
                const_cast<presetcloud::Point&> (p).pluginName = pluginNameOf (p.pluginId);
                X.push_back (presetcloud::layoutVector (p, pluginIndex[p.pluginId], (int) pluginIndex.size()));
            }
            layoutCount = points.size();
            {
                const juce::ScopedLock sl (layout.lock);
                layout.input = std::move (X);
                layout.has = true;
            }
            layout.stopFlag.store (true);
            layout.notify();
            status = "Laying out " + juce::String ((int) points.size()) + " presets ...";
            ++changeCounter;
        }
        {
            const juce::ScopedLock sl (layout.lock);
            if (layout.done)
            {
                layout.done = false;
                if (layout.outX.size() == points.size() && layoutCount == points.size())
                    for (size_t i = 0; i < points.size(); ++i) { points[i].x = layout.outX[i]; points[i].y = layout.outY[i]; }
                status = juce::String ((int) points.size()) + " presets - tap a dot to play it";
                ++changeCounter;
            }
        }

        // Morph: the connected synth's knobs between the two presets
        if (connected())
        {
            const float m = juce::jlimit (0.0f, 1.0f, param (pMorph) * 0.01f);
            if (cepstralMode() != lastCepstralApplied) { lastMorph = -1.0f; settleTicks = 0; }
            if (std::abs (m - lastMorph) > 0.002f)
            {
                lastMorph = m;
                settleTicks = 15;
                applyMorph (m);
            }
            else if (settleTicks > 0)
            {
                --settleTicks;
                applyMorph (m);   // (only knobs that moved away are set again)
            }
        }

        // a module just taken from the sidebar: its first map at once
        if (! configured && ! startedFirst)
        {
            startedFirst = true;
            roll();
        }
    }

    // the knobs of both copies for Morph at m (the same synth only: two
    // synths keep their presets as they are). Copy 1 holds A's switch-like
    // settings, copy 2 B's; every other knob of both goes from A's value to
    // B's. The switches first, then the rest (a switch may set other knobs).
    void applyMorph (float m)
    {
        if (! connected() || crossSynth.load()) return;
        const bool cepstral = cepstralMode();
        if (cepstral && lastCepstralApplied && settleTicks <= 0) return;
        auto& a = points[(size_t) morphA];
        auto& b = points[(size_t) morphB];
        ensureLoaded (a); ensureLoaded (b);
        auto fadeAt = [&a, &b] (juce::AudioProcessor& proc, bool switchesFromA, float m)
        {
            const auto& prms = proc.getParameters();
            const size_t n = juce::jmin ((size_t) prms.size(), a.params.size(), b.params.size());
            for (int pass = 0; pass < 2; ++pass)
                for (size_t i = 0; i < n; ++i)
                {
                    auto* prm = prms[(int) i];
                    const bool stepped = isSwitch (*prm);
                    if (stepped != (pass == 0)) continue;
                    const float v = stepped ? (switchesFromA ? a.params[i] : b.params[i]) : a.params[i] + (b.params[i] - a.params[i]) * m;
                    if (std::abs (prm->getValue() - v) > 1.0e-5f) prm->setValueNotifyingHost (v);
                }
        };
        // (6.9.12) Cepstral Morph: each copy keeps its own preset whole (the morph is in the sound)
        const float mA = cepstral ? 0.0f : m, mB = cepstral ? 1.0f : m;
        if (auto* p = playingProcessor()) fadeAt (*p, true, mA);
        if (auto* h = dynamic_cast<plugins::HostedPluginModule*> (innerB.get()))
            if (auto* pb = h->getPlugin())
                fadeAt (*pb, false, mB);
        lastCepstralApplied = cepstral;
    }

    static bool isSwitch (juce::AudioProcessorParameter& prm)
    {
        return prm.isDiscrete() || prm.isBoolean() || (prm.getNumSteps() > 1 && prm.getNumSteps() < 48);
    }

    // A dot's whole preset, read when first needed: a stock preset is its
    // program on a fresh copy of the synth (so nothing of another sound is
    // left in it - a program sets only the knobs it names), its state and
    // knobs kept with the dot. A rolled dot has both already.
    void ensureLoaded (presetcloud::Point& pt)
    {
        if (pt.state.getSize() > 0 && ! pt.params.empty()) return;
        auto proc = plugins::createOriginal (pt.pluginId, 48000.0);
        if (proc == nullptr) return;
        if (pt.stock) proc->setCurrentProgram (pt.program);
        else if (pt.state.getSize() > 0) proc->setStateInformation (pt.state.getData(), (int) pt.state.getSize());
        else if (pt.seed != 0)   // (6.9.15) a rolled dot played for the first time: rolled again, the same
            if (auto* src = rp::find (pt.pluginId))
            {
                const auto& l = rp::listsOf (*src);
                src->roll (*proc, pt.rollKind, l.defaultGenre, l.defaultEngine, pt.seed);
                proc->getStateInformation (pt.state);
                pt.params.clear();
                for (auto* prm : proc->getParameters()) pt.params.push_back (prm->getValue());
                return;
            }
        if (pt.state.getSize() == 0) proc->getStateInformation (pt.state);
        pt.params.clear();
        for (auto* prm : proc->getParameters()) pt.params.push_back (prm->getValue());
    }

    // a dot's preset into a copy of its synth, whole: its state, then its
    // knobs one by one (a state some synth reads only in part, or later,
    // still ends with exactly the preset's knobs)
    void loadInto (juce::AudioProcessor& proc, presetcloud::Point& pt)
    {
        ensureLoaded (pt);
        if (pt.state.getSize() > 0) proc.setStateInformation (pt.state.getData(), (int) pt.state.getSize());
        else if (pt.stock) proc.setCurrentProgram (pt.program);
        const auto& prms = proc.getParameters();
        const size_t n = juce::jmin ((size_t) prms.size(), pt.params.size());
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < n; ++i)
            {
                auto* prm = prms[(int) i];
                if (isSwitch (*prm) != (pass == 0)) continue;
                if (std::abs (prm->getValue() - pt.params[i]) > 1.0e-5f) prm->setValueNotifyingHost (pt.params[i]);
            }
    }

    // a new copy of a dot's synth, set up for the audio, then holding the dot's preset
    std::shared_ptr<SynthModule> makeLoaded (presetcloud::Point& pt)
    {
        auto fresh = makePlugin (pt.pluginId);
        if (fresh == nullptr) { status = "Could not load " + pluginNameOf (pt.pluginId); return nullptr; }
        if (prepared) fresh->prepare (sampleRate);
        if (auto* h = dynamic_cast<plugins::HostedPluginModule*> (fresh.get()))
            if (auto* proc = h->getPlugin())
                loadInto (*proc, pt);
        return fresh;
    }

    void loadPoint (int index)
    {
        auto& pt = points[(size_t) index];
        if (playingPlugin != pt.pluginId || inner == nullptr)
        {
            // another synth: a new copy (set up, then loaded)
            auto fresh = makeLoaded (pt);
            if (fresh == nullptr) { ++changeCounter; return; }
            setInner (std::move (fresh), false);
            playingPlugin = pt.pluginId;
        }
        else if (auto* p = playingProcessor())
            loadInto (*p, pt);   // (the same synth: its tail rings on into the next preset, as on any synth)
        chosen = index;
        retriggerAuto.store (true);
        status = describe (pt) + "   (" + presetcloud::colourName (pt.colour) + ")";
        ++changeCounter;
    }

    static std::shared_ptr<SynthModule> makePlugin (const juce::String& id)
    {
        auto* entry = plugins::findEntry (id);
        if (entry == nullptr) return nullptr;
        auto m = ModuleFactory::instance().createInstance (entry->guiTypeId);
        if (dynamic_cast<plugins::HostedPluginModule*> (m.get()) == nullptr) return nullptr;
        return std::shared_ptr<SynthModule> (std::move (m));
    }

    void setInner (std::shared_ptr<SynthModule> fresh, bool prepareIt = true)
    {
        if (fresh != nullptr && prepared && prepareIt) fresh->prepare (sampleRate);
        std::shared_ptr<SynthModule> old;
        {
            const juce::SpinLock::ScopedLockType sl (swapLock);
            old = std::move (inner);
            inner = std::move (fresh);
        }
        old = nullptr;
    }

    std::shared_ptr<SynthModule> inner, innerB;   // innerB: the second copy, while two presets are connected
    juce::SpinLock swapLock;
    std::atomic<bool> crossSynth { false };       // the line joins two different synths
    std::unique_ptr<SynthModule> cepAB, cepBA;    // (6.9.12) Cepstral Morph: A through B's envelope, B through A's
    bool cepstralNow { false };                   // audio thread
    float cepTarget { 0.0f }, cepX { 0.0f };      // audio thread: 0 = A the carrier, 1 = B
    float cepMorphNow { 0.0f }, refEnergy { 0.0f }, outEnergy { 0.0f }, levelGain { 1.0f }, levelCoeff { 1.0e-4f };
    bool lastCepstralApplied { false };           // message thread
    float xfade { 0.0f }, xfadeTarget { 0.0f }, xfadeCoeff { 0.003f };   // audio thread: 0 = copy 1, 1 = copy 2
    int settleTicks { 0 };
    static constexpr int kAutoNote = 48;          // C3, as the keyboard names it
    std::atomic<bool> retriggerAuto { false };
    bool autoSounding { false };                  // audio thread
    juce::int64 autoSamples { 0 };                // audio thread

    void sendToBoth (const juce::MidiMessage& m) { sendToBothLocked (m); }   // (called with the lock held)
    void sendToBothLocked (const juce::MidiMessage& m)
    {
        if (inner != nullptr) inner->rawMidiMessage (m);
        if (innerB != nullptr) innerB->rawMidiMessage (m);
    }

    void setInnerB (std::shared_ptr<SynthModule> fresh, bool prepareIt = true)
    {
        if (fresh != nullptr && prepared && prepareIt) fresh->prepare (sampleRate);
        std::shared_ptr<SynthModule> old;
        {
            const juce::SpinLock::ScopedLockType sl (swapLock);
            old = std::move (innerB);
            innerB = std::move (fresh);
        }
        old = nullptr;
    }
    bool prepared { false }, configured { false }, startedFirst { false };
    juce::String playingPlugin;
    float lastMorph { -1.0f };

    friend class PresetCloudView;
};

//==============================================================================
// The card: Show, Connect, Autoplay, Roll and the map. Drag the map to move
// it, pinch (or the wheel) to zoom; a tap on a dot plays it. With Connect on, a
// drag from a dot to another of the same synth connects them for Morph (and
// does not move the map). The chosen dot sits under a lens: the dots around
// it are spread out and grow, the further ones untouched.
class PresetCloudView : public juce::Component,
                        public plugins::PatchEditRequester,
                        private juce::Timer
{
public:
    static constexpr float kLensK = 1.1f;   // how strongly the lens spreads (Sarkar-Brown fisheye)

    explicit PresetCloudView (PresetCloudModule& m) : module (&m)
    {
        for (auto* b : { &connectButton, &autoButton, &openButton, &disconnectButton })
        {
            addAndMakeVisible (*b);
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3038));
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1a8fa6));
            b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        }
        addAndMakeVisible (showBox);
        showBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a3038));
        showBox.setColour (juce::ComboBox::textColourId, juce::Colours::white);
        connectButton.setTooltip ("On: a drag anywhere on the map draws a line from the lit dot; the dot it would join is "
                                  "lit and named while you hold, and letting go connects them (the map does not move while it is on). "
                                  "Morph then fades between the two: the same synth - every knob; two synths - a crossfade");
        openButton.setTooltip ("Adds the synth as a card of its own, with the lit dot's preset (with a line: Open Position - "
                               "the sound at Morph's position; two synths: both)");
        disconnectButton.setTooltip ("Removes the line: the lit dot plays alone again");
        openButton.onClick = [this]
        {
            auto* m = mod();
            if (m == nullptr || requestPatchEdit == nullptr) return;
            auto what = m->statesToOpen();
            if (what.empty()) return;
            requestPatchEdit ([what] (AquanodeModularAudioProcessor& patch, int self) { plugins::openPluginsBeside (patch, self, what); });
        };
        disconnectButton.onClick = [this] { if (auto* m = mod()) m->disconnect(); };
        autoButton.setTooltip ("Plays C3 by itself (struck again every bar), so a preset or a morph can be heard without "
                               "playing a note");
        connectButton.setClickingTogglesState (true);
        autoButton.setClickingTogglesState (true);
        connectButton.setToggleState (m.connectMode, juce::dontSendNotification);
        autoButton.setToggleState (m.autoplay.load(), juce::dontSendNotification);
        connectButton.onClick = [this] { if (auto* m = mod()) m->connectMode = connectButton.getToggleState(); };
        autoButton.onClick = [this] { if (auto* m = mod()) m->setAutoplay (autoButton.getToggleState()); };

        showBox.addItem ("All synths", 1);
        for (auto* e : presetcloud::synths()) { showBox.addItem (e->name, showBox.getNumItems() + 1); ids.add (e->id); }
        showBox.onChange = [this]
        {
            if (auto* m = mod())
            {
                const int i = showBox.getSelectedItemIndex();
                m->setShowOnly (i <= 0 ? juce::String() : ids[i - 1]);
                fitView();
            }
        };
        showBox.setSelectedItemIndex (m.showOnly.isEmpty() ? 0 : ids.indexOf (m.showOnly) + 1, juce::dontSendNotification);
        startTimerHz (30);
    }

    PresetCloudModule* mod() const { return dynamic_cast<PresetCloudModule*> (module.get()); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        auto top = r.removeFromTop (26);
        // (Roll is the card's own button, beside Per Roll)
        disconnectButton.setBounds (top.removeFromRight (88));
        top.removeFromRight (4);
        openButton.setBounds (top.removeFromRight (104));
        top.removeFromRight (4);
        autoButton.setBounds (top.removeFromRight (80));
        top.removeFromRight (4);
        connectButton.setBounds (top.removeFromRight (76));
        top.removeFromRight (6);
        showBox.setBounds (top);
        r.removeFromTop (4);
        bottom = r.removeFromBottom (34);
        map = r;
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = mod();
        if (m == nullptr) return;
        const auto mr = map.toFloat();
        g.setColour (juce::Colour (0xff10161c));
        g.fillRoundedRectangle (mr, 8.0f);
        g.saveState();
        g.reduceClipRegion (map);

        const int n = (int) m->points.size();
        // the lattice's spacing on screen: the dots fill most of it
        const float s0 = (float) juce::jmin (map.getWidth(), map.getHeight());
        const float cell = std::sqrt (0.7744f / (float) juce::jmax (1, n) / 1.12f * 1.1547f) * s0 * zoom;
        const float baseR = juce::jlimit (1.5f, 9.0f, cell * 0.36f);

        juce::Point<float> focus (-1e6f, -1e6f);
        const bool hasFocus = currentFocus (focus);
        const float lensR = juce::jlimit (50.0f, 110.0f, cell * 4.5f);

        // the lens (Sarkar-Brown fisheye, gentle): inside it a dot moves out
        // from the focus a little and grows a little; outside it nothing changes
        auto lens = [&] (juce::Point<float> p, float& scale)
        {
            scale = 1.0f;
            if (! hasFocus) return p;
            const auto d = p - focus;
            const float dist = d.getDistanceFromOrigin();
            if (dist >= lensR || dist < 1.0e-3f) return p;
            constexpr float k = kLensK;
            const float x = dist / lensR;
            const float y = (k + 1.0f) * x / (k * x + 1.0f);
            scale = juce::jlimit (1.0f, 1.45f, 1.0f + 0.45f * (1.0f - x) * (1.0f - x));
            return focus + d * (y / x);
        };

        if (hasFocus)
        {
            juce::ColourGradient halo (juce::Colours::white.withAlpha (0.07f), focus, juce::Colours::transparentBlack,
                                       focus.translated (lensR, 0.0f), true);
            g.setGradientFill (halo);
            g.fillEllipse (focus.x - lensR, focus.y - lensR, lensR * 2.0f, lensR * 2.0f);
            // thin spokes to its nearest neighbours
            std::vector<std::pair<float, int>> near;
            for (int i = 0; i < n; ++i)
                if (i != m->chosen && m->visible (m->points[(size_t) i]))
                    near.push_back ({ toScreen (m->points[(size_t) i]).getDistanceFrom (focus), i });
            std::partial_sort (near.begin(), near.begin() + juce::jmin ((int) near.size(), 10), near.end());
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            for (int k = 0; k < juce::jmin ((int) near.size(), 10); ++k)
            {
                float sc;
                const auto p = lens (toScreen (m->points[(size_t) near[(size_t) k].second]), sc);
                g.drawLine (juce::Line<float> (focus, p), 0.6f);
            }
        }

        // the two ends of a connection (Morph's line): both ringed and named
        const bool connected = m->morphA >= 0 && m->morphB >= 0 && m->morphA < n && m->morphB < n;
        auto isEnd = [&] (int i) { return connected && (i == m->morphA || i == m->morphB); };

        for (int i = 0; i < n; ++i)
        {
            const auto& p = m->points[(size_t) i];
            if (! m->visible (p) || i == m->chosen || isEnd (i)) continue;
            float sc;
            const auto at = lens (toScreen (p), sc);
            const float r = baseR * sc;
            if (! map.toFloat().expanded (r).contains (at)) continue;
            // the colour by kind, lighter or darker by how bright the preset is
            const auto col = presetcloud::colourOf (p.colour).withMultipliedBrightness (0.72f + 0.5f * p.feat[2]);
            g.setColour (col.withAlpha (hasFocus && sc <= 1.0f ? 0.85f : 1.0f));
            g.fillEllipse (at.x - r, at.y - r, r * 2.0f, r * 2.0f);
            if (p.stock && r > 3.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawEllipse (at.x - r, at.y - r, r * 2.0f, r * 2.0f, 0.7f);
            }
        }

        // the Morph line, and a line being drawn
        auto line = [&g] (juce::Point<float> a, juce::Point<float> b, float alpha)
        {
            g.setColour (juce::Colour (0xffffe08a).withAlpha (alpha));
            juce::Path p; p.startNewSubPath (a); p.lineTo (b);
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (p, p, dashes, 2);
            g.fillPath (p);
        };
        auto endAt = [&] (int i)
        {
            float sc;
            return i == m->chosen && hasFocus ? focus : lens (toScreen (m->points[(size_t) i]), sc);
        };
        if (m->connected())
        {
            const auto a = endAt (m->morphA);
            const auto b = endAt (m->morphB);
            line (a, b, 0.9f);
            const float t = juce::jlimit (0.0f, 1.0f, m->param (PresetCloudModule::pMorph) * 0.01f);
            const auto at = a + (b - a) * t;
            g.setColour (juce::Colour (0xffffe08a));
            g.fillEllipse (at.x - 5.0f, at.y - 5.0f, 10.0f, 10.0f);
        }
        // Connect: the line being drawn, from the lit dot to the finger (to the
        // dot it would join, once there is one)
        if (linking && hasFocus)
            line (focus, linkTarget >= 0 ? endAt (linkTarget) : drawTo, 0.75f);

        // the chosen dot and both ends of a connection (and the dot a line is
        // being drawn from): ringed, named - over the line. Names stay inside
        // the map, and when two would overlap the second goes under its dot.
        {
            g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
            std::vector<juce::Rectangle<float>> placed;
            auto named = [&] (int i, juce::Point<float> at, float r, int blendFrom)
            {
                const auto& p = m->points[(size_t) i];
                auto fill = presetcloud::colourOf (p.colour).brighter (0.2f);
                if (blendFrom >= 0 && blendFrom < n && glideT < 1.0f)   // (the highlighter on its way: its colour too)
                    fill = presetcloud::colourOf (m->points[(size_t) blendFrom].colour).brighter (0.2f).interpolatedWith (fill, glideT);
                g.setColour (fill);
                g.fillEllipse (at.x - r, at.y - r, r * 2.0f, r * 2.0f);
                g.setColour (juce::Colours::white);
                g.drawEllipse (at.x - r - 2.5f, at.y - r - 2.5f, r * 2.0f + 5.0f, r * 2.0f + 5.0f, 1.6f);
                const float w = juce::jmin (240.0f, (float) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), p.name) + 12.0f);
                const auto inside = mr.reduced (2.0f);
                auto fit = [&] (float dy)
                {
                    auto b = juce::Rectangle<float> (w, 16.0f).withCentre (at.translated (0.0f, dy));
                    b.setX (juce::jlimit (inside.getX(), juce::jmax (inside.getX(), inside.getRight() - w), b.getX()));
                    b.setY (juce::jlimit (inside.getY(), juce::jmax (inside.getY(), inside.getBottom() - 16.0f), b.getY()));
                    return b;
                };
                auto box = fit (-r - 14.0f);
                for (const auto& o : placed)
                    if (o.intersects (box)) { box = fit (r + 14.0f); break; }
                placed.push_back (box);
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillRoundedRectangle (box, 4.0f);
                g.setColour (juce::Colours::white);
                g.drawText (p.name, box, juce::Justification::centred, true);
            };
            if (hasFocus)
                named (m->chosen, focus, baseR * 1.7f, glideFrom);
            for (int e : { m->morphA, m->morphB })
                if (isEnd (e) && e != m->chosen && m->visible (m->points[(size_t) e]))
                    named (e, endAt (e), baseR * 1.5f, -1);
            if (linking && linkTarget >= 0 && linkTarget != m->chosen && ! isEnd (linkTarget))
                named (linkTarget, endAt (linkTarget), baseR * 1.5f, -1);
        }
        g.restoreState();

        // the legend (a dot and its name for each colour), then the status
        auto area = bottom;
        auto leg = area.removeFromTop (15).toFloat();
        const float w = leg.getWidth() / (float) presetcloud::kNumColours;
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        for (int c = 0; c < presetcloud::kNumColours; ++c)
        {
            auto cellArea = leg.removeFromLeft (w);
            g.setColour (presetcloud::colourOf (c));
            g.fillEllipse (cellArea.getX() + 3.0f, cellArea.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawText (presetcloud::colourName (c), cellArea.withTrimmedLeft (15.0f), juce::Justification::centredLeft, true);
        }
        juce::String s = m->status;
        if (m->busy())
            s = (m->status.isNotEmpty() ? m->status : juce::String ("Reading the presets ...")) + "  " + juce::String ((int) (m->progress() * 100.0f)) + " %";
        else if (m->connectMode && ! linking && ! m->connected())
            s = m->chosen >= 0 ? "Connect: drag from anywhere - the line starts at the lit dot; let go to join the lit target"
                               : "Connect: tap a dot to light it first";
        else if (linking && linkTarget >= 0)
            s = "Let go to connect to " + m->points[(size_t) linkTarget].name;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawFittedText (s, area.reduced (2, 0), juce::Justification::centredLeft, 1, 0.8f);
    }

    //=== touch ================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        if (m == nullptr || ! map.contains (e.getPosition())) return;
        // (a finger whose lifting never arrived would turn the next touch into
        // a pinch, and the map would leap: only fingers still down count)
        forgetLiftedTouches();
        touches[e.source.getIndex()] = e.position;
        if (touches.size() >= 2) { startPinch(); linking = false; linkTarget = -1; repaint(); return; }
        panStart = pan;
        downAt = e.position;
        downDot = dotAt (e.position);
        // Connect on (and a dot lit): wherever the finger goes down, a line
        // from the lit dot follows it (the map does not move)
        juce::Point<float> f;
        linking = m->connectMode && currentFocus (f);
        drawTo = e.position;
        linkTarget = linking ? targetAt (e.position) : -1;
        if (linking) repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (touches.count (e.source.getIndex()) == 0) return;   // (a finger the map never saw go down)
        touches[e.source.getIndex()] = e.position;
        if (touches.size() >= 2)
        {
            // pinch: zoomed about the point between the fingers, which stays
            // under them (and moves the map with them)
            const float d = pinchDistance();
            if (pinchStart > 20.0f)
            {
                zoom = juce::jlimit (0.6f, 8.0f, zoomStart * d / pinchStart);
                const auto mid = pinchMid();
                const auto c = map.toFloat().getCentre();
                pan = (mid - c) / zoom - pinchAnchor;
            }
            repaint();
            return;
        }
        if (linking)
        {
            drawTo = e.position;
            linkTarget = targetAt (e.position);
            repaint();
            return;
        }
        if (mod() != nullptr && mod()->connectMode) return;   // (no lit dot yet: a tap lights one)
        if (downDot < 0 || e.getDistanceFromDragStart() > 8)
        {
            pan = panStart + (e.position - downAt) / juce::jmax (0.1f, zoom);
            downDot = -1;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        const bool wasPinch = touches.size() >= 2;
        touches.erase (e.source.getIndex());
        if (wasPinch)
        {
            // one finger left: it pans on from where it is now (not from where
            // it went down before the pinch - the map would jump)
            if (touches.size() == 1) { panStart = pan; downAt = touches.begin()->second; downDot = -1; }
            else if (touches.size() >= 2) startPinch();
            afterPinch = true;
            return;
        }
        if (afterPinch) { afterPinch = false; downDot = -1; return; }   // (the last finger of a pinch: no tap)
        if (m == nullptr) return;
        if (linking)
        {
            // let go: joined to the lit target, if there is one
            const int to = targetAt (e.position);
            linking = false;
            linkTarget = -1;
            if (to >= 0 && to != m->chosen) m->connect (m->chosen, to);
            repaint();
            return;
        }
        if (downDot >= 0 && e.getDistanceFromDragStart() <= 8) m->play (downDot);
        downDot = -1;
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        zoom = juce::jlimit (0.6f, 8.0f, zoom * (1.0f + w.deltaY * 0.6f));
        repaint();
    }

    void mouseMagnify (const juce::MouseEvent&, float s) override
    {
        zoom = juce::jlimit (0.6f, 8.0f, zoom * s);
        repaint();
    }

private:
    juce::Point<float> toScreen (const presetcloud::Point& p) const { return toScreenXY (p.x, p.y); }
    juce::Point<float> toScreenXY (float x, float y) const
    {
        // pan is in unzoomed map pixels
        const auto c = map.toFloat().getCentre();
        const float s0 = (float) juce::jmin (map.getWidth(), map.getHeight());
        return { c.x + ((x - 0.5f) * s0 + pan.x) * zoom, c.y + ((y - 0.5f) * s0 + pan.y) * zoom };
    }

    // the highlighter: on the lit dot, or on its way there (a tap moves it
    // in a straight line over half a second); false without a lit dot
    bool currentFocus (juce::Point<float>& out) const
    {
        auto* m = mod();
        if (m == nullptr) return false;
        const int n = (int) m->points.size();
        if (m->chosen < 0 || m->chosen >= n || ! m->visible (m->points[(size_t) m->chosen])) return false;
        const auto& to = m->points[(size_t) m->chosen];
        juce::Point<float> at (to.x, to.y);
        if (glideT < 1.0f)
        {
            const float e = glideT * glideT * (3.0f - 2.0f * glideT);
            at = glideFromXY + (at - glideFromXY) * e;
        }
        out = toScreenXY (at.x, at.y);
        return true;
    }

    // Connect: the dot a line would join, near the finger (more generous than
    // a tap: the finger hides it) - never the lit dot itself
    int targetAt (juce::Point<float> q) const
    {
        auto* m = mod();
        if (m == nullptr) return -1;
        const int n = (int) m->points.size();
        const float s0 = (float) juce::jmin (map.getWidth(), map.getHeight());
        const float cell = std::sqrt (0.7744f / (float) juce::jmax (1, n) / 1.12f * 1.1547f) * s0 * zoom;
        return dotAt (q, juce::jmax (30.0f, cell * 1.8f), m->chosen);
    }

    // the dot under a finger - where it is drawn, through the lens
    int dotAt (juce::Point<float> q, float reach = -1.0f, int skip = -1) const
    {
        auto* m = mod();
        const int n = (int) m->points.size();
        juce::Point<float> focus;
        const bool hasFocus = currentFocus (focus);
        const float s0 = (float) juce::jmin (map.getWidth(), map.getHeight());
        const float cell = std::sqrt (0.7744f / (float) juce::jmax (1, n) / 1.12f * 1.1547f) * s0 * zoom;
        const float lensR = juce::jlimit (50.0f, 110.0f, cell * 4.5f);
        int best = -1; float bestD = reach > 0.0f ? reach : juce::jmax (14.0f, cell * 0.8f);
        for (int i = 0; i < n; ++i)
        {
            if (i == skip || ! m->visible (m->points[(size_t) i])) continue;
            auto p = toScreen (m->points[(size_t) i]);
            if (hasFocus)
            {
                const auto d = p - focus;
                const float dist = d.getDistanceFromOrigin();
                if (dist < lensR && dist > 1.0e-3f)
                {
                    const float x = dist / lensR;
                    p = focus + d * (((kLensK + 1.0f) * x / (kLensK * x + 1.0f)) / x);
                }
            }
            const float d = p.getDistanceFrom (q);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    // with one synth shown, the view frames its dots
    void fitView()
    {
        auto* m = mod();
        if (m == nullptr) return;
        float x0 = 1, x1 = 0, y0 = 1, y1 = 0; int n = 0;
        for (const auto& p : m->points)
            if (m->visible (p)) { x0 = juce::jmin (x0, p.x); x1 = juce::jmax (x1, p.x); y0 = juce::jmin (y0, p.y); y1 = juce::jmax (y1, p.y); ++n; }
        if (n == 0 || m->showOnly.isEmpty()) { zoom = 1.0f; pan = {}; repaint(); return; }
        const float span = juce::jmax (0.12f, juce::jmax (x1 - x0, y1 - y0));
        zoom = juce::jlimit (1.0f, 6.0f, 0.85f / span);
        const float s = (float) juce::jmin (map.getWidth(), map.getHeight());
        pan = { -(0.5f * (x0 + x1) - 0.5f) * s, -(0.5f * (y0 + y1) - 0.5f) * s };
        repaint();
    }

    void forgetLiftedTouches()
    {
        auto& desktop = juce::Desktop::getInstance();
        for (auto it = touches.begin(); it != touches.end();)
        {
            auto* src = desktop.getMouseSource (it->first);
            if (src == nullptr || ! src->isDragging()) it = touches.erase (it);
            else ++it;
        }
        if (touches.empty()) afterPinch = false;
    }

    void startPinch()
    {
        pinchStart = pinchDistance();
        zoomStart = zoom;
        // the map point between the fingers (in unzoomed map pixels, as pan)
        const auto c = map.toFloat().getCentre();
        pinchAnchor = (pinchMid() - c) / juce::jmax (0.1f, zoom) - pan;
        downDot = -1;
    }

    juce::Point<float> pinchMid() const
    {
        if (touches.size() < 2) return map.toFloat().getCentre();
        auto it = touches.begin();
        const auto a = it->second; ++it;
        return (a + it->second) * 0.5f;
    }

    float pinchDistance() const
    {
        if (touches.size() < 2) return 0.0f;
        auto it = touches.begin();
        const auto a = it->second; ++it;
        return a.getDistanceFrom (it->second);
    }

    void timerCallback() override
    {
        auto* m = mod();
        if (m == nullptr) return;
        // the highlighter's glide: from where it was to the newly lit dot
        if (m->chosen != glideChosen)
        {
            const int n = (int) m->points.size();
            juce::Point<float> from;
            const bool had = glideChosen >= 0 && glideChosen < n;
            if (had)
            {
                // from where it is now (it may still be on its way)
                const auto& old = m->points[(size_t) glideChosen];
                juce::Point<float> was (old.x, old.y);
                if (glideT < 1.0f) { const float e = glideT * glideT * (3.0f - 2.0f * glideT); was = glideFromXY + (was - glideFromXY) * e; }
                from = was;
            }
            glideFrom = had ? glideChosen : -1;
            glideChosen = m->chosen;
            glideFromXY = from;
            glideT = (had && m->chosen >= 0 && m->chosen < n) ? 0.0f : 1.0f;
            glideStart = juce::Time::getMillisecondCounterHiRes();
            if (glideT < 1.0f) { startTimerHz (60); m->glidingUntil.store (glideStart + 520.0); }
        }
        if (glideT < 1.0f)
        {
            glideT = juce::jlimit (0.0f, 1.0f, (float) ((juce::Time::getMillisecondCounterHiRes() - glideStart) / 500.0));
            if (glideT >= 1.0f) startTimerHz (30);
            repaint();
        }
        {
            const bool joined = m->connected();
            openButton.setButtonText (joined ? "Open Position" : "Open Preset");
            openButton.setEnabled (m->chosen >= 0);
            disconnectButton.setEnabled (joined);
        }
        const int c = m->changeCounter.load();
        const float morph = m->param (PresetCloudModule::pMorph);
        if (c != seen || morph != seenMorph)
        {
            seen = c;
            seenMorph = morph;
            const int want = m->showOnly.isEmpty() ? 0 : ids.indexOf (m->showOnly) + 1;
            if (showBox.getSelectedItemIndex() != want) showBox.setSelectedItemIndex (want, juce::dontSendNotification);
            autoButton.setToggleState (m->autoplay.load(), juce::dontSendNotification);
            repaint();
        }
    }

    juce::WeakReference<SynthModule> module;
    juce::ComboBox showBox;
    juce::TextButton connectButton { "Connect" }, autoButton { "Autoplay" }, openButton { "Open Preset" }, disconnectButton { "Disconnect" };
    // Connect: a line being drawn from the lit dot, and the dot it would join
    bool linking { false };
    int linkTarget { -1 };
    // the highlighter's glide to a newly lit dot (map coordinates; 1 = there)
    int glideChosen { -1 }, glideFrom { -1 };
    juce::Point<float> glideFromXY;
    float glideT { 1.0f };
    double glideStart { 0.0 };
    juce::StringArray ids;
    juce::Rectangle<int> map, bottom;
    float zoom { 1.0f }, zoomStart { 1.0f }, pinchStart { 0.0f };
    juce::Point<float> pan, panStart, downAt, drawTo, pinchAnchor;
    bool afterPinch { false };
    int downDot { -1 };
    std::map<int, juce::Point<float>> touches;
    int seen { -1 };
    float seenMorph { -1.0f };
};

std::unique_ptr<juce::Component> PresetCloudModule::createExtraContentComponent()
{
    return std::make_unique<PresetCloudView> (*this);
}

static ModuleDescriptor presetCloudDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.presetcloud";
    d.displayName = "Random Preset Cloud";
    d.description =
        "The synth plugins' presets as a map of coloured dots, similar sounds close together - tap one to play it. "
        "Their stock presets and the random presets of the latest Roll (Per Roll of them, fairly over the synths and "
        "their kinds), read off their knobs and kinds without listening. Red: harsh leads, blue: gentle sweeps, green: "
        "natural, violet: subtle, brown: basses. With Connect on, a drag draws a line from the lit dot to another (the "
        "one it would join is lit while you hold); Morph then fades between the two - the same synth: every knob "
        "(Smoother crossfades where its switches change), two synths: a crossfade. Open Preset adds the synth as a card "
        "with the lit preset (with a line: Open Position, the sound at Morph's position); Disconnect removes the line; "
        "Autoplay plays C3 by itself. Drag the map to move it, pinch to zoom; the lit dot sits under a lens. Mode Cepstral "
        "Morph: both presets play whole, and the one the line's position is nearer is heard through the other's spectral "
        "envelope (its formants, its tone) - not at all at the ends, fully at the middle, where the two cross over "
        "(Smoother widens the crossover); Clarity as on Cepstral Morph. It takes 3072 samples (told to the host, and lined up "
        "with the rest of the patch where it lines up latency).";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 8;
    d.searchKeywords = "preset map browser random cloud morph";
    d.sockets = { midiIn ("midiIn", "MIDI In"), audioOut ("audioOut", "Audio Out"), tempoIn() };
    auto perRoll = makeCombo ("perRoll", "Per Roll", { "25", "50", "100", "200", "400" }, 2, 0, 2);
    perRoll.modulatable = false;
    d.params = {
        perRoll,
        makeButton ("roll", "Roll", 0, 1),   // new random presets (Per Roll of them); between the two (6.7.5)
        makeRotary ("morph", "Morph", 0.0f, 100.0f, 0.0f, 0, "%"),
        // the same synth at both ends: a crossfade around the middle, where the
        // switches change (0: they change at once) - 6.7.6
        makeRotary ("smoother", "Smoother", 0.0f, 100.0f, 0.0f, 0, "%"),
        // (6.9.12) Crossfade (as before), or Cepstral Morph: one preset heard through the other's
        // spectral envelope, as much as the line's position says
        makeCombo ("mode", "Mode", { "Crossfade", "Cepstral Morph" }, 0, 0, 2).noMod(),
        makeRotary ("clarity", "Clarity", 0.0f, 200.0f, 100.0f, 0, "%").visibleWhen ("mode", 1.0f)
    };
    d.extraSection = "Map";
    return d;
}

AQUANODE_REGISTER_MODULE (PresetCloudModule, presetCloudDescriptor)

#if AQUANODE_TEST_HARNESS
// (the desktop test harness only)
// pairs of dots of the same synth (random ones, which carry their whole state)
std::vector<std::pair<int, int>> presetCloudPairs (aquanode::SynthModule* m, int count)
{
    std::vector<std::pair<int, int>> v;
    auto* c = dynamic_cast<PresetCloudModule*> (m);
    if (c == nullptr) return v;
    std::map<juce::String, std::vector<int>> by;
    for (int i = 0; i < (int) c->points.size(); ++i)
        if (! c->points[(size_t) i].stock) by[c->points[(size_t) i].pluginId].push_back (i);
    for (auto& kv : by)
        if (kv.second.size() >= 2 && (int) v.size() < count)
            v.push_back ({ kv.second[0], kv.second[kv.second.size() / 2] });
    return v;
}
// pairs by kind: 0 rolled presets of one synth, 1 stock presets of one synth, 2 two different synths
std::vector<std::pair<int, int>> presetCloudPairsOf (aquanode::SynthModule* m, int kind, int count)
{
    std::vector<std::pair<int, int>> v;
    auto* c = dynamic_cast<PresetCloudModule*> (m);
    if (c == nullptr) return v;
    std::map<juce::String, std::vector<int>> by;
    for (int i = 0; i < (int) c->points.size(); ++i)
        if (c->points[(size_t) i].stock == (kind == 1) || kind == 2) by[c->points[(size_t) i].pluginId].push_back (i);
    if (kind == 2)
    {
        std::vector<int> firsts;
        for (auto& kv : by) firsts.push_back (kv.second[kv.second.size() / 3]);
        for (size_t i = 0; i + 1 < firsts.size() && (int) v.size() < count; i += 2) v.push_back ({ firsts[i], firsts[i + 1] });
        return v;
    }
    for (auto& kv : by)
        if (kv.second.size() >= 3 && (int) v.size() < count)
            v.push_back ({ kv.second[1], kv.second[kv.second.size() - 1] });
    return v;
}
juce::String presetCloudLink (aquanode::SynthModule* m)
{
    auto* c = dynamic_cast<PresetCloudModule*> (m);
    if (c == nullptr) return {};
    return "lit " + juce::String (c->chosen) + (c->connected() ? ", line " + juce::String (c->morphA) + " - " + juce::String (c->morphB) : juce::String (", no line"));
}
void presetCloudSmoother (aquanode::SynthModule* m, float v) { if (m != nullptr) m->setParameter ("smoother", v); }
void presetCloudDisconnect (aquanode::SynthModule* m) { if (auto* c = dynamic_cast<PresetCloudModule*> (m)) c->disconnect(); }
int presetCloudOpenCount (aquanode::SynthModule* m) { auto* c = dynamic_cast<PresetCloudModule*> (m); return c != nullptr ? (int) c->statesToOpen().size() : 0; }
float presetCloudXfadeTarget (aquanode::SynthModule* m) { auto* c = dynamic_cast<PresetCloudModule*> (m); return c != nullptr ? c->testXfadeTarget() : -1.0f; }
void presetCloudPlay (aquanode::SynthModule* m, int i) { if (auto* c = dynamic_cast<PresetCloudModule*> (m)) c->play (i); }
void presetCloudConnect (aquanode::SynthModule* m, int a, int b) { if (auto* c = dynamic_cast<PresetCloudModule*> (m)) c->connect (a, b); }
void presetCloudMorph (aquanode::SynthModule* m, float v) { if (auto* c = dynamic_cast<PresetCloudModule*> (m)) c->testSetMorph (v); }
void presetCloudAutoplay (aquanode::SynthModule* m, bool on) { if (auto* c = dynamic_cast<PresetCloudModule*> (m)) c->setAutoplay (on); }
juce::String presetCloudPointName (aquanode::SynthModule* m, int i)
{
    auto* c = dynamic_cast<PresetCloudModule*> (m);
    return c != nullptr ? c->points[(size_t) i].pluginId + ": " + c->points[(size_t) i].name : juce::String();
}

bool presetCloudBusy (aquanode::SynthModule* m)
{
    auto* c = dynamic_cast<PresetCloudModule*> (m);
    return c != nullptr && (c->busy() || ! c->isConfigured());
}

juce::String presetCloudSummary (aquanode::SynthModule* m)
{
    auto* c = dynamic_cast<PresetCloudModule*> (m);
    if (c == nullptr) return "no cloud";
    std::map<juce::String, int> perPlugin, perPluginStock;
    int perColour[presetcloud::kNumColours] {};
    float minX = 1, maxX = 0, minY = 1, maxY = 0;
    for (const auto& p : c->points)
    {
        (p.stock ? perPluginStock : perPlugin)[p.pluginId]++;
        perColour[p.colour]++;
        minX = juce::jmin (minX, p.x); maxX = juce::jmax (maxX, p.x); minY = juce::jmin (minY, p.y); maxY = juce::jmax (maxY, p.y);
    }
    juce::String s;
    s << (int) c->points.size() << " points, x " << minX << ".." << maxX << " y " << minY << ".." << maxY << "\nrandom per plugin:";
    for (auto& kv : perPlugin) s << " " << kv.first << "=" << kv.second;
    s << "\nstock per plugin:";
    for (auto& kv : perPluginStock) s << " " << kv.first << "=" << kv.second;
    s << "\ncolours:";
    for (int i = 0; i < presetcloud::kNumColours; ++i) s << " " << presetcloud::colourName (i) << "=" << perColour[i];
    // how well colours cluster: mean distance to the nearest same-colour dot vs to any dot
    double same = 0, any = 0; int n = 0;
    for (size_t i = 0; i < c->points.size(); ++i)
    {
        float ds = 9, da = 9;
        for (size_t j = 0; j < c->points.size(); ++j)
        {
            if (i == j) continue;
            const float d = std::hypot (c->points[i].x - c->points[j].x, c->points[i].y - c->points[j].y);
            da = juce::jmin (da, d);
            if (c->points[j].colour == c->points[i].colour) ds = juce::jmin (ds, d);
        }
        same += ds; any += da; ++n;
    }
    s << "\nnearest same-colour " << same / juce::jmax (1, n) << " vs nearest any " << any / juce::jmax (1, n);
    // a few examples
    s << "\nexamples:";
    for (size_t i = 0; i < c->points.size(); i += juce::jmax ((size_t) 1, c->points.size() / 12))
        s << "\n  " << c->points[i].pluginId << " | " << c->points[i].kind << " | " << c->points[i].name << " -> " << presetcloud::colourName (c->points[i].colour);
    s << "\nstatus: " << c->status;
    // play one and morph two of the same synth
    if (! c->points.empty())
    {
        c->play (0);
        s << "\nplayed: " << c->status;
    }
    return s;
}
#endif
