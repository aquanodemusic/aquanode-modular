// Drum Morpher - four drum sounds at the corners of a square; the cursor
// anywhere in it plays a new drum made from them (DrumMorphEngine.h): the
// corners lined up at their landmarks, their cepstral envelopes and pitched
// fine structures blended, their level envelopes and transients carried over.
// An 8 x 8 grid of morphs is rendered in the background whenever a sound,
// a mark or the FFT size changes; playing reads between grid points, so X and
// Y (knobs, the pad, or cables onto them) move freely, even during a hit.
//
// Plays like a sampler: a note on Midi In (or a key) plays the morph at that
// pitch (Root plays it as it is), velocity sets its level; a rising edge on
// Trig In plays it at Root. Cepstral blends the morph with a plain crossfade
// of the lined-up corners; Transient lifts or tames the attack (its measured
// length); Decay shortens the sound.
//
// Card: the four corners (tap one to load a sound - the first 10 s, the
// silent start cut off - drag its start / end marks; between them its volume
// curve, as Cove Groove's: tap to add a point, drag it, double-tap or long-
// press to remove it - the corner's sound, and so every morph of it, follows
// the curve), the XY pad below (drag; a tap also plays it). The sounds and
// their curves are saved with the patch. Save WAV (6.7.9): the morph as it
// plays now - X, Y, Cepstral, Transient, Decay, Pitch and Level - one hit at
// Root, into a 24-bit mono WAV file.
// Flexible (per voice from keys, global from a Trig In / Midi In).
// Inputs: 0 = Trig In, 1 = Midi In. Output: 0 = Audio Out.
#include "../../../Platform/AndroidDocuments.h"
#include "../../Core/ModuleCore.h"
#include "DrumMorphEngine.h"
#include "../../Shared/PhysDrumPresets.h"

using namespace aquanode;

class DrumMorphModule : public SynthModule, private juce::Timer
{
public:
    enum ParamIndex { pX = 0, pY, pCepstral, pTransient, pDecay, pPitch, pRoot, pLevel, pFft };

    DrumMorphModule() : renderer (*this)
    {
        renderer.startThread (juce::Thread::Priority::low);
        startTimerHz (10);
    }

    ~DrumMorphModule() override
    {
        stopTimer();
        renderer.stop.store (true);
        renderer.signalThreadShouldExit();
        renderer.notify();
        renderer.stopThread (8000);
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    bool acceptsGlobalMidiNotes() const override { return true; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override
    {
        for (int v = 0; v < kMaxVoices; ++v) voiceReset (v);
    }

    void voiceReset (int v) override
    {
        auto& vs = voices[(size_t) v];
        vs = {};
    }

    void voiceNoteOn (int v, int note, bool) override { start (v, note); }

    double voiceTailSeconds() const override
    {
        const auto* g = live.load (std::memory_order_acquire);
        float longest = 0.5f;
        if (g != nullptr)
            for (const auto& c : g->cells) longest = juce::jmax (longest, (float) (c.length() / drummorph::kRate));
        return (double) longest * 2.0 + 0.1;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
        renderGlobalMidiHits (inputs, outputs, 0);
    }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        auto& vs = voices[(size_t) v];
        if (v == 0)
        {
            const float trig = inputs[0][0];
            if (trig > 0.5f && vs.lastTrig <= 0.5f) start (0, -1);
            vs.lastTrig = trig;
            if (auditionPending.exchange (false, std::memory_order_acq_rel)) start (0, -1);
        }
        const float s = vs.active ? renderVoice (vs) : 0.0f;
        outputs[0] = { s, s };
    }

    //=== the corners (message thread) =========================================
    std::array<drummorph::Corner, drummorph::kCorners> corners;   // raw + marks + names, analysed for the view

    bool loadCornerFromStream (int corner, std::unique_ptr<juce::InputStream> stream, const juce::String& name)
    {
        if (stream == nullptr || corner < 0 || corner >= drummorph::kCorners) return false;
        std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (std::move (stream)));
        if (reader == nullptr) return false;
        drummorph::Corner c;
        if (! drummorph::loadCorner (*reader, c)) return false;
        c.name = name;
        c.analyse();
        corners[(size_t) corner] = std::move (c);
        cornersChanged();
        return true;
    }

    // Random Kit: four sounds of one of the built-in kits (the fitted 808,
    // 909 ... models Cove Groove and the Drum Pads play), one per corner by
    // role where the kit has them - bottom left a kick, bottom right a snare
    // or clap, top left a hat or cymbal, top right a tom or percussion
    static const juce::StringArray& kitNames()
    {
        static const juce::StringArray n { "808", "909", "Classic", "Electro", "Large Drums", "Metal", "Orchestral", "Orchestral Perc", "World", "Any kit" };
        return n;
    }
    void loadRandomKit (int kit)
    {
        static const char* prefixes[] = { "808/", "909/", "classic/", "electro/", "large/", "metal/", "orch/", "orchperc/", "world/", "" };
        kit = juce::jlimit (0, kitNames().size() - 1, kit);
        lastKit = kit;
        using namespace aquanode::physdrums;
        std::vector<const Preset*> pool;
        for (const auto& p : allPresets())
            if (juce::String (p.key).startsWith (prefixes[kit])) pool.push_back (&p);
        if (pool.empty()) return;
        auto role = [] (const Preset& p) -> int
        {
            const juce::String name (p.name);
            switch (p.voice)
            {
                case aquanode::physdrums::Voice::Kick808: case aquanode::physdrums::Voice::Kick909: return 0;
                case aquanode::physdrums::Voice::Snare808: case aquanode::physdrums::Voice::Snare909: case aquanode::physdrums::Voice::Clap808: case aquanode::physdrums::Voice::Clap909: return 1;
                case aquanode::physdrums::Voice::Metal808: case aquanode::physdrums::Voice::Cymbal909: case aquanode::physdrums::Voice::Maracas808: return 2;
                case aquanode::physdrums::Voice::Tom808: case aquanode::physdrums::Voice::Tom909: case aquanode::physdrums::Voice::Cowbell808: case aquanode::physdrums::Voice::Res808: case aquanode::physdrums::Voice::Rim909: return 3;
                default: break;
            }
            if (name.containsIgnoreCase ("snare") || name.containsIgnoreCase ("clap") || name.containsIgnoreCase ("slap")) return 1;
            if (name.containsIgnoreCase ("metal") || name.containsIgnoreCase ("cymbal") || name.containsIgnoreCase ("hat")
                || name.containsIgnoreCase ("gong") || name.containsIgnoreCase ("crash") || name.containsIgnoreCase ("ride")) return 2;
            if (name.containsIgnoreCase ("tom") || name.containsIgnoreCase ("hand") || name.containsIgnoreCase ("bar")) return 3;
            if (name.containsIgnoreCase ("drum") || name.containsIgnoreCase ("bass")) return 0;
            return 3;
        };
        juce::Random rnd;
        std::vector<const Preset*> chosen;
        for (int r = 0; r < drummorph::kCorners; ++r)
        {
            std::vector<const Preset*> fit;
            for (auto* p : pool)
                if (role (*p) == r && std::find (chosen.begin(), chosen.end(), p) == chosen.end()) fit.push_back (p);
            if (fit.empty())
                for (auto* p : pool)
                    if (std::find (chosen.begin(), chosen.end(), p) == chosen.end()) fit.push_back (p);
            chosen.push_back (fit.empty() ? pool[(size_t) rnd.nextInt ((int) pool.size())] : fit[(size_t) rnd.nextInt ((int) fit.size())]);
        }
        for (int i = 0; i < drummorph::kCorners; ++i)
        {
            const auto* p = chosen[(size_t) i];
            const auto hit = aquanode::physdrums::render (*p, aquanode::physdrums::Mods {}, drummorph::kRate, 1.0, (uint64_t) rnd.nextInt (100000));
            std::vector<float> mono (hit.l.size());
            for (size_t k = 0; k < mono.size(); ++k)
                mono[k] = (float) (hit.stereo && k < hit.r.size() ? 0.5 * (hit.l[k] + hit.r[k]) : hit.l[k]);
            // each at -6 dB peak: the kits differ a lot, and a morph of two
            // in-phase hits can come out louder than either
            float pk = 0.0f;
            for (float v : mono) pk = juce::jmax (pk, std::abs (v));
            if (pk > 1.0e-6f) for (auto& v : mono) v *= 0.5f / pk;
            drummorph::Corner c;
            if (! drummorph::setCornerSound (std::move (mono), drummorph::kRate, c)) continue;
            c.name = p->name;
            c.analyse();
            corners[(size_t) i] = std::move (c);
        }
        cornersChanged();
    }
    int lastKit { 0 };

    void clearCorner (int corner)
    {
        corners[(size_t) corner] = {};
        cornersChanged();
    }

    void setMarks (int corner, float startFrac, float endFrac)
    {
        auto& c = corners[(size_t) corner];
        c.startFrac = juce::jlimit (0.0f, 0.98f, startFrac);
        c.endFrac = juce::jlimit (c.startFrac + 0.01f, 1.0f, endFrac);
        c.analyse();
        cornersChanged();
    }

    // a corner's volume curve (the card's points over its waveform)
    void setCurve (int corner, const drummorph::VolumeCurve& curve)
    {
        auto& c = corners[(size_t) corner];
        c.curve = curve;
        c.curve.tidy();
        c.analyse();
        cornersChanged();
    }

    void cornersChanged()
    {
        dirty = true;
        dirtySince = juce::Time::getMillisecondCounter();
        markSampleChanged();   // (wakes it, if asleep; the card redraws)
    }

    // the grid: being made (0..1) or done (1)
    float renderProgress() const { return renderer.busy.load() ? renderer.progress.load() : 1.0f; }
    bool renderPending() const { return dirty || renderer.busy.load(); }
    size_t gridBytes() const { auto* g = live.load(); return g != nullptr ? g->bytes() : 0; }
    void audition() { auditionPending.store (true); }
    std::atomic<float> shownX { 0.5f }, shownY { 0.5f };   // where it plays now (with cables)

    // Save WAV (6.7.9): one hit of the morph as it plays now (where X / Y and
    // the knobs are, cables included), at Root, worked out here and now beside
    // the playing (a voice of its own) - empty while there is no grid yet.
    // The silent tail is cut; a hit over full scale is brought just under it.
    std::vector<float> renderCurrentHit (double& rateOut)
    {
        const auto* g = live.load (std::memory_order_acquire);
        if (g == nullptr || ! g->any) return {};
        rateOut = sampleRate;   // (the rate the module plays at)
        Voice vs;
        vs.active = true;
        vs.note = -1;           // Root: the sound as it is (Pitch still applies)
        std::vector<float> out;
        const size_t cap = (size_t) (rateOut * drummorph::kMaxSeconds * 4.0);
        out.reserve ((size_t) (rateOut * 2.0));
        while (vs.active && out.size() < cap)
            out.push_back (renderVoice (vs));
        size_t end = out.size();
        while (end > 0 && std::abs (out[end - 1]) < 3.0e-5f) --end;   // (-90 dB)
        out.resize (end);
        float peak = 0.0f;
        for (auto s : out) peak = juce::jmax (peak, std::abs (s));
        if (peak > 0.989f) for (auto& s : out) s *= 0.989f / peak;   // (-0.1 dB)
        // a few milliseconds of fade at the very end, where the tail was cut
        const size_t fade = juce::jmin (out.size(), (size_t) (rateOut * 0.004));
        for (size_t i = 0; i < fade; ++i) out[out.size() - 1 - i] *= (float) i / (float) juce::jmax<size_t> (1, fade);
        return out;
    }

    static juce::MemoryBlock encodeWav (const std::vector<float>& mono, double rate)
    {
        juce::MemoryBlock block;
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::MemoryOutputStream (block, false), rate, 1, 24, {}, 0));
            if (w == nullptr) return {};
            const float* ch[1] = { mono.data() };
            w->writeFromFloatArrays (ch, 1, (int) mono.size());
        }   // (the writer's end finishes the file's header)
        return block;
    }

    // for tests / a host without a message loop: make the grid now
    void renderNowBlocking()
    {
        std::atomic<bool> stop { false };
        std::atomic<float> progress { 0.0f };
        auto g = drummorph::renderGrid (corners, fftOrder(), stop, progress);
        publish (std::move (g));
        dirty = false;
    }

    //=== saving: the corners end to end as one mono sound, their lengths, names
    // and marks in the custom state ==========================================
    bool hasAudioToSave() const override
    {
        for (const auto& c : corners) if (! c.raw.empty()) return true;
        return false;
    }

    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        size_t total = 0;
        for (const auto& c : corners) total += c.raw.size();
        if (total == 0) return nullptr;
        auto b = std::make_shared<juce::AudioBuffer<float>> (1, (int) total);
        size_t at = 0;
        for (const auto& c : corners)
        {
            if (! c.raw.empty()) b->copyFrom (0, (int) at, c.raw.data(), (int) c.raw.size());
            at += c.raw.size();
        }
        rate = drummorph::kRate;
        return b;
    }

    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override
    {
        if (audio == nullptr || audio->getNumSamples() == 0) return;
        packed.assign (audio->getReadPointer (0), audio->getReadPointer (0) + audio->getNumSamples());
        packedRate = rate;
        unpack();
    }

    juce::String saveCustomState() const override
    {
        juce::Array<juce::var> list;
        for (const auto& c : corners)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", c.name);
            o->setProperty ("len", (int) c.raw.size());
            o->setProperty ("start", c.startFrac);
            o->setProperty ("end", c.endFrac);
            if (! c.curve.isFlat()) o->setProperty ("curve", c.curve.toString());
            list.add (juce::var (o));
        }
        return juce::JSON::toString (juce::var (list), true);
    }

    void loadCustomState (const juce::String& state) override
    {
        const auto v = juce::JSON::parse (state);
        if (! v.isArray()) return;
        for (int i = 0; i < drummorph::kCorners && i < v.size(); ++i)
        {
            const auto& o = v[i];
            pendingMeta[(size_t) i] = { o["name"].toString(), (int) o["len"], (float) (double) o["start"], (float) (double) o["end"], o["curve"].toString() };
        }
        haveMeta = true;
        unpack();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 560; }
    int preferredModuleWidth() const override { return 460; }

private:
    //=== playing ==============================================================
    struct Voice
    {
        bool active { false };
        double pos { 0.0 };        // kRate samples into the sound
        double rate { 1.0 };
        int note { -1 };
        float lastTrig { 0.0f };
        int countdown { 0 };
        // updated every 32 samples
        float cw[4] {}; int cell[4] {};
        float w[drummorph::kCorners] {};
        float lm[4] {}, trans { 0.01f };
        float intensity { 1.0f }, transAmt { 0.0f }, decay { 1.0f }, gain { 1.0f };
    };

    void start (int v, int note)
    {
        auto& vs = voices[(size_t) v];
        const auto* g = live.load (std::memory_order_acquire);
        if (g == nullptr || ! g->any) return;
        vs.active = true;
        vs.pos = 0.0;
        vs.note = note;
        vs.countdown = 0;
    }

    void update (Voice& vs, const drummorph::Grid& g)
    {
        const float x = juce::jlimit (0.0f, 1.0f, param (pX) * 0.01f);
        const float y = juce::jlimit (0.0f, 1.0f, param (pY) * 0.01f);
        shownX.store (x, std::memory_order_relaxed);
        shownY.store (y, std::memory_order_relaxed);
        const float gx = x * (drummorph::kGrid - 1), gy = y * (drummorph::kGrid - 1);
        const int i0 = juce::jmin (drummorph::kGrid - 2, (int) gx), j0 = juce::jmin (drummorph::kGrid - 2, (int) gy);
        const float fx = gx - (float) i0, fy = gy - (float) j0;
        vs.cell[0] = j0 * drummorph::kGrid + i0;       vs.cw[0] = (1.0f - fx) * (1.0f - fy);
        vs.cell[1] = j0 * drummorph::kGrid + i0 + 1;   vs.cw[1] = fx * (1.0f - fy);
        vs.cell[2] = (j0 + 1) * drummorph::kGrid + i0; vs.cw[2] = (1.0f - fx) * fy;
        vs.cell[3] = (j0 + 1) * drummorph::kGrid + i0 + 1; vs.cw[3] = fx * fy;

        drummorph::cornerWeights (x, y, vs.w);
        float sum = 0.0f;
        for (int i = 0; i < drummorph::kCorners; ++i) { if (! g.corners[(size_t) i].valid()) vs.w[i] = 0.0f; sum += vs.w[i]; }
        if (sum > 0.0f) for (auto& w : vs.w) w /= sum;
        const drummorph::Corner* cp[4] = { &g.corners[0], &g.corners[1], &g.corners[2], &g.corners[3] };
        drummorph::blendLandmarks (cp, vs.w, vs.lm, vs.trans);

        vs.intensity = param (pCepstral) * 0.01f;
        vs.transAmt = param (pTransient) * 0.01f;
        vs.decay = juce::jlimit (0.05f, 1.0f, param (pDecay) * 0.01f);
        const float semis = (vs.note >= 0 ? (float) (vs.note - (int) param (pRoot)) : 0.0f) + param (pPitch) + pitchBendSemitones();
        vs.rate = std::pow (2.0, (double) semis / 12.0) * drummorph::kRate / sampleRate;
        vs.gain = juce::Decibels::decibelsToGain (param (pLevel));
    }

    float renderVoice (Voice& vs)
    {
        const auto* g = live.load (std::memory_order_acquire);
        if (g == nullptr || ! g->any) { vs.active = false; return 0.0f; }
        if (--vs.countdown <= 0) { vs.countdown = 32; update (vs, *g); }

        const float t = (float) (vs.pos / drummorph::kRate);
        if (t > vs.lm[3] + 0.05f) { vs.active = false; return 0.0f; }

        float out = 0.0f;
        if (vs.intensity > 0.0f)
        {
            float gm = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                if (vs.cw[k] <= 0.0f) continue;
                const auto& c = g->cells[(size_t) vs.cell[k]];
                if (c.pcm.empty()) continue;
                gm += vs.cw[k] * c.sampleAt ((double) drummorph::mapTime (t, vs.lm, c.landmarks) * drummorph::kRate);
            }
            out += vs.intensity * gm;
        }
        if (vs.intensity < 1.0f)
        {
            float xf = 0.0f;
            for (int i = 0; i < drummorph::kCorners; ++i)
                if (vs.w[i] > 0.0f)
                    xf += vs.w[i] * g->corners[(size_t) i].sampleAt ((double) drummorph::mapTime (t, vs.lm, g->corners[(size_t) i].landmarks) * drummorph::kRate);
            out += (1.0f - vs.intensity) * xf;
        }

        // Transient: the measured attack lifted or tamed (up to 12 dB)
        if (vs.transAmt != 0.0f)
        {
            const float T = vs.trans;
            const float shape = t < T ? 1.0f : (t < 2.0f * T ? 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (t - T) / T) : 0.0f);
            out *= std::pow (2.0f, 2.0f * vs.transAmt * shape);
        }
        // Decay: the sound faded out sooner
        if (vs.decay < 0.999f)
        {
            const float span = vs.lm[3] - vs.lm[1];
            const float fadeEnd = vs.lm[1] + span * vs.decay, fadeStart = vs.lm[1] + span * vs.decay * 0.4f;
            if (t >= fadeEnd) { vs.active = false; return 0.0f; }
            if (t > fadeStart) { const float a = (t - fadeStart) / (fadeEnd - fadeStart); out *= (1.0f - a) * (1.0f - a); }
        }

        vs.pos += vs.rate;
        return out * vs.gain;
    }

    //=== rendering in the background ==========================================
    int fftOrder() const
    {
        static const int orders[] = { 9, 10, 11, 12 };
        return orders[juce::jlimit (0, 3, (int) getParameterBase (pFft))];
    }

    struct Renderer : public juce::Thread
    {
        explicit Renderer (DrumMorphModule& m) : juce::Thread ("Drum Morpher grid"), owner (m) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (-1);
                if (threadShouldExit()) break;
                std::array<drummorph::Corner, drummorph::kCorners> job;
                int order = 11;
                {
                    const juce::ScopedLock sl (lock);
                    if (! hasJob) continue;
                    job = std::move (pendingJob);
                    order = pendingOrder;
                    hasJob = false;
                }
                busy.store (true);
                stop.store (false);
                progress.store (0.0f);
                auto g = drummorph::renderGrid (std::move (job), order, stop, progress);
                if (! stop.load())
                {
                    const juce::ScopedLock sl (lock);
                    finished = std::move (g);
                }
                busy.store (false);
            }
        }
        void request (std::array<drummorph::Corner, drummorph::kCorners> corners, int order)
        {
            {
                const juce::ScopedLock sl (lock);
                pendingJob = std::move (corners);
                pendingOrder = order;
                hasJob = true;
            }
            stop.store (true);   // (a render under way gives up: this one replaces it)
            notify();
        }
        std::shared_ptr<drummorph::Grid> takeFinished()
        {
            const juce::ScopedLock sl (lock);
            return std::move (finished);
        }

        DrumMorphModule& owner;
        juce::CriticalSection lock;
        std::array<drummorph::Corner, drummorph::kCorners> pendingJob;
        int pendingOrder { 11 };
        bool hasJob { false };
        std::shared_ptr<drummorph::Grid> finished;
        std::atomic<bool> stop { false }, busy { false };
        std::atomic<float> progress { 0.0f };
    };

    void timerCallback() override
    {
        // the FFT size changed: render again
        if (fftOrder() != renderedOrder && hasAudioToSave()) { cornersChanged(); renderedOrder = fftOrder(); }
        // a change settled for a moment: (re)render, in the background
        if (dirty && juce::Time::getMillisecondCounter() - dirtySince > 250)
        {
            dirty = false;
            renderedOrder = fftOrder();
            renderer.request (corners, renderedOrder);
        }
        if (auto g = renderer.takeFinished())
            publish (std::move (g));
        // a grid let go a second ago is no longer played by anyone
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (const auto& r) { return now - r.second > 1500; }), retired.end());
    }

    void publish (std::shared_ptr<drummorph::Grid> g)
    {
        live.store (g.get(), std::memory_order_release);
        if (owner != nullptr) retired.emplace_back (std::move (owner), juce::Time::getMillisecondCounter());
        owner = std::move (g);
        markSampleChanged();
    }

    // the saved audio and its lengths may arrive in either order
    void unpack()
    {
        if (! haveMeta || packed.empty()) return;
        size_t at = 0;
        for (int i = 0; i < drummorph::kCorners; ++i)
        {
            const auto& m = pendingMeta[(size_t) i];
            auto& c = corners[(size_t) i];
            c = {};
            c.name = m.name;
            const size_t len = (size_t) juce::jmax (0, m.len);
            if (len > 0 && at + len <= packed.size())
            {
                std::vector<float> src (packed.begin() + (long) at, packed.begin() + (long) (at + len));
                if (std::abs (packedRate - drummorph::kRate) > 1.0 && packedRate > 0.0)
                {
                    const double ratio = packedRate / drummorph::kRate;
                    c.raw.assign ((size_t) ((double) len / ratio), 0.0f);
                    src.resize (src.size() + 8, 0.0f);
                    juce::LagrangeInterpolator li;
                    li.process (ratio, src.data(), c.raw.data(), (int) c.raw.size());
                }
                else
                    c.raw = std::move (src);
                c.startFrac = m.start;
                c.endFrac = m.end > m.start ? m.end : 1.0f;
                if (m.curve.isNotEmpty()) c.curve.fromString (m.curve);
                c.analyse();
            }
            at += len;
        }
        packed.clear();
        haveMeta = false;
        cornersChanged();
    }

    static juce::AudioFormatManager& formats()
    {
        static juce::AudioFormatManager m;
        static bool done = false;
        if (! done)
        {
            m.registerBasicFormats();
           #if JUCE_USE_MP3AUDIOFORMAT
            m.registerFormat (new juce::MP3AudioFormat(), false);
           #endif
            done = true;
        }
        return m;
    }

    Renderer renderer;
    std::atomic<const drummorph::Grid*> live { nullptr };
    std::shared_ptr<drummorph::Grid> owner;
    std::vector<std::pair<std::shared_ptr<drummorph::Grid>, juce::uint32>> retired;
    bool dirty { false };
    juce::uint32 dirtySince { 0 };
    int renderedOrder { -1 };
    std::array<Voice, kMaxVoices> voices;
    std::atomic<bool> auditionPending { false };

    struct Meta { juce::String name; int len { 0 }; float start { 0.0f }, end { 1.0f }; juce::String curve; };
    std::array<Meta, drummorph::kCorners> pendingMeta;
    bool haveMeta { false };
    std::vector<float> packed;
    double packedRate { drummorph::kRate };

    friend class DrumMorphView;
};

//==============================================================================
// The card: the four corners above, the XY pad below
class DrumMorphView : public juce::Component, private juce::Timer
{
public:
    explicit DrumMorphView (DrumMorphModule& m) : module (&m)
    {
        addAndMakeVisible (kitButton);
        kitButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3038));
        kitButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        kitButton.setTooltip ("Loads four sounds of a built-in kit into the corners: a kick, a snare or clap, a hat or "
                              "cymbal and a tom or percussion, where the kit has them");
        kitButton.onClick = [this] { kitMenu(); };
        addAndMakeVisible (saveButton);
        saveButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3038));
        saveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        saveButton.setTooltip ("Saves the morph as it plays now - where X and Y are, with Cepstral, Transient, Decay, "
                               "Pitch and Level - as one hit in a WAV file");
        saveButton.onClick = [this] { saveWav(); };
        startTimerHz (20);
    }

    DrumMorphModule* mod() const { return dynamic_cast<DrumMorphModule*> (module.get()); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        auto top = r.removeFromTop (230);
        const int w = (top.getWidth() - 6) / 2, h = (top.getHeight() - 6) / 2;
        // the panels sit where their corners are on the pad: top-left 2, top-right 3, bottom-left 0, bottom-right 1
        panels[2] = { top.getX(), top.getY(), w, h };
        panels[3] = { top.getX() + w + 6, top.getY(), w, h };
        panels[0] = { top.getX(), top.getY() + h + 6, w, h };
        panels[1] = { top.getX() + w + 6, top.getY() + h + 6, w, h };
        r.removeFromTop (6);
        status = r.removeFromBottom (24);
        kitButton.setBounds (status.removeFromRight (104).reduced (0, 1));
        status.removeFromRight (4);
        saveButton.setBounds (status.removeFromRight (84).reduced (0, 1));
        const int side = juce::jmin (r.getWidth(), r.getHeight());
        pad = juce::Rectangle<int> (side, side).withCentre (r.getCentre());
    }

    void paint (juce::Graphics& g) override
    {
        auto* m = mod();
        if (m == nullptr) return;
        for (int i = 0; i < drummorph::kCorners; ++i) paintCorner (g, i, panels[i].toFloat(), m->corners[(size_t) i]);
        paintPad (g, *m);

        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        juce::String s;
        if (m->renderPending())
            s << "Morphing the 8 x 8 grid ... " << juce::String ((int) (m->renderProgress() * 100.0f)) << " %";
        else if (m->gridBytes() > 0)
            s << "Tap the pad to hear it - in a corner it plays that corner's own sound (" << juce::String ((double) m->gridBytes() / 1048576.0, 1) << " MB)";
        else
            s << "Tap one of the four panels above to load a sound, or Random Kit";
        g.drawFittedText (s, status.reduced (2, 0), juce::Justification::centredLeft, 2, 0.8f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        if (m == nullptr) return;
        drag = {};
        if (pad.contains (e.getPosition()))
        {
            drag.pad = true;
            setXY (e.position);
            m->audition();
            return;
        }
        for (int i = 0; i < drummorph::kCorners; ++i)
        {
            if (! panels[i].contains (e.getPosition())) continue;
            auto& c = m->corners[(size_t) i];
            if (c.raw.empty()) { if (e.mods.isPopupMenu()) cornerMenu (i); else chooseFile (i); return; }
            const auto wave = waveArea (panels[i].toFloat());
            // the volume curve: a point (drag it; right-click removes a middle one)
            curveEdit = c.curve;
            const int hit = curvePointAt (i, c, e.position);
            if (e.mods.isPopupMenu())
            {
                if (hit > 0 && hit < curveEdit.n - 1) { removeCurvePoint (i, hit); m->setCurve (i, curveEdit); }
                else cornerMenu (i);
                return;
            }
            pressPos = e.position;
            pressTime = juce::Time::getMillisecondCounter();
            if (hit >= 0)
            {
                drag.corner = i; drag.point = hit;
                longPressArmed = hit > 0 && hit < curveEdit.n - 1;   // (a long press removes a middle point)
                repaint();
                return;
            }
            // near a mark: move it
            const float xs = wave.getX() + wave.getWidth() * c.startFrac, xe = wave.getX() + wave.getWidth() * c.endFrac;
            if (std::abs (e.position.x - xs) < 12.0f) { drag.corner = i; drag.mark = 0; return; }
            if (std::abs (e.position.x - xe) < 12.0f) { drag.corner = i; drag.mark = 1; return; }
            // empty space over the marked part: a new point there (drag it on)
            if (e.position.x > xs && e.position.x < xe && wave.expanded (0.0f, 4.0f).contains (e.position)
                && curveEdit.n < drummorph::VolumeCurve::kMaxPoints)
            {
                const auto v = curveFromScreen (i, c, e.position);
                int at = 1;
                while (at < curveEdit.n - 1 && curveEdit.x[at] < v.x) ++at;
                for (int k = curveEdit.n; k > at; --k) { curveEdit.x[k] = curveEdit.x[k - 1]; curveEdit.y[k] = curveEdit.y[k - 1]; }
                curveEdit.x[at] = juce::jlimit (curveEdit.x[at - 1] + 0.001f, curveEdit.x[at + 1] - 0.001f, v.x);
                curveEdit.y[at] = v.y;
                ++curveEdit.n;
                drag.corner = i; drag.point = at; drag.added = true;
                repaint();
                return;
            }
            drag.corner = i;   // (a tap elsewhere: the corner's menu, on mouse up)
            return;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        if (m == nullptr) return;
        if (drag.pad) { setXY (e.position); return; }
        if (drag.corner >= 0 && drag.point >= 0)
        {
            if (e.position.getDistanceFrom (pressPos) > 5.0f) drag.moved = true;
            if (! drag.moved && ! drag.added) return;
            longPressArmed = false;
            const auto& c = m->corners[(size_t) drag.corner];
            const auto v = curveFromScreen (drag.corner, c, e.position);
            const int k = drag.point;
            if (k > 0 && k < curveEdit.n - 1)
                curveEdit.x[k] = juce::jlimit (curveEdit.x[k - 1] + 0.001f, curveEdit.x[k + 1] - 0.001f, v.x);   // (the ends: up and down only)
            curveEdit.y[k] = v.y;
            repaint();
            return;
        }
        if (drag.corner >= 0 && drag.mark >= 0)
        {
            const auto wave = waveArea (panels[drag.corner].toFloat());
            const float f = (e.position.x - wave.getX()) / juce::jmax (1.0f, wave.getWidth());
            auto& c = m->corners[(size_t) drag.corner];
            liveMarks = true;
            if (drag.mark == 0) c.startFrac = juce::jlimit (0.0f, c.endFrac - 0.01f, f);
            else c.endFrac = juce::jlimit (c.startFrac + 0.01f, 1.0f, f);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        if (m == nullptr) return;
        longPressArmed = false;
        if (drag.corner >= 0 && drag.point >= 0)
        {
            if (drag.moved || drag.added)
                m->setCurve (drag.corner, curveEdit);   // (the sound and the grid follow)
        }
        else if (drag.corner >= 0 && drag.mark >= 0 && liveMarks)
        {
            const auto& c = m->corners[(size_t) drag.corner];
            m->setMarks (drag.corner, c.startFrac, c.endFrac);
        }
        else if (drag.corner >= 0 && drag.mark < 0 && e.getDistanceFromDragStart() < 6 && e.getNumberOfClicks() == 1)
            cornerMenu (drag.corner);
        liveMarks = false;
        drag = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // a double-tap on a middle point removes it
        auto* m = mod();
        if (m == nullptr) return;
        for (int i = 0; i < drummorph::kCorners; ++i)
        {
            if (! panels[i].contains (e.getPosition())) continue;
            auto& c = m->corners[(size_t) i];
            if (c.raw.empty()) return;
            curveEdit = c.curve;
            const int hit = curvePointAt (i, c, e.position);
            if (hit > 0 && hit < curveEdit.n - 1) { removeCurvePoint (i, hit); m->setCurve (i, curveEdit); repaint(); }
            drag = {};
            return;
        }
    }

private:
    struct Drag { bool pad { false }; int corner { -1 }; int mark { -1 }; int point { -1 }; bool moved { false }, added { false }; };
    Drag drag;
    bool liveMarks { false };
    // the volume curve being edited (the corner's, until let go)
    drummorph::VolumeCurve curveEdit;
    juce::Point<float> pressPos;
    juce::uint32 pressTime { 0 };
    bool longPressArmed { false };

    // the curve lies over the marked part: level 1 at the top, 0 at the bottom
    juce::Rectangle<float> curveArea (int i, const drummorph::Corner& c) const
    {
        const auto wave = waveArea (panels[i].toFloat());
        const float xs = wave.getX() + wave.getWidth() * c.startFrac, xe = wave.getX() + wave.getWidth() * c.endFrac;
        return { xs, wave.getY(), juce::jmax (1.0f, xe - xs), wave.getHeight() };
    }
    juce::Point<float> curveToScreen (int i, const drummorph::Corner& c, const drummorph::VolumeCurve& cv, int k) const
    {
        const auto a = curveArea (i, c);
        return { a.getX() + cv.x[k] * a.getWidth(), a.getBottom() - cv.y[k] * a.getHeight() };
    }
    juce::Point<float> curveFromScreen (int i, const drummorph::Corner& c, juce::Point<float> p) const
    {
        const auto a = curveArea (i, c);
        return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()),
                 juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / juce::jmax (1.0f, a.getHeight())) };
    }
    int curvePointAt (int i, const drummorph::Corner& c, juce::Point<float> p) const
    {
        int best = -1;
        float bestD = 13.0f;   // (a fingertip)
        for (int k = 0; k < curveEdit.n; ++k)
            if (const float d = curveToScreen (i, c, curveEdit, k).getDistanceFrom (p); d < bestD) { bestD = d; best = k; }
        return best;
    }
    void removeCurvePoint (int, int k)
    {
        for (int q = k; q < curveEdit.n - 1; ++q) { curveEdit.x[q] = curveEdit.x[q + 1]; curveEdit.y[q] = curveEdit.y[q + 1]; }
        --curveEdit.n;
    }

    static juce::Rectangle<float> waveArea (juce::Rectangle<float> panel) { return panel.reduced (6.0f, 18.0f).withTrimmedBottom (-6.0f); }

    void setXY (juce::Point<float> p)
    {
        auto* m = mod();
        const float x = juce::jlimit (0.0f, 1.0f, (p.x - (float) pad.getX()) / (float) pad.getWidth());
        const float y = juce::jlimit (0.0f, 1.0f, 1.0f - (p.y - (float) pad.getY()) / (float) pad.getHeight());
        m->setParameter ("x", x * 100.0f);
        m->setParameter ("y", y * 100.0f);
        repaint();
    }

    // Save WAV (6.7.9)
    void saveWav()
    {
        auto* m = mod();
        if (m == nullptr) return;
        auto tell = [] (const juce::String& text)
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Drum Morpher", text);
        };
        if (! m->hasAudioToSave()) { tell ("Load a sound into a corner first (tap one), or a Random Kit."); return; }
        if (m->renderPending()) { tell ("The morphs are still being made - save again in a moment."); return; }
        double rate = drummorph::kRate;
        auto hit = m->renderCurrentHit (rate);
        if (hit.empty()) { tell ("There is nothing to save here yet."); return; }
        auto wav = std::make_shared<juce::MemoryBlock> (DrumMorphModule::encodeWav (hit, rate));
        if (wav->getSize() == 0) { tell ("The WAV file could not be made."); return; }
        const auto name = "Drum Morph X" + juce::String (juce::roundToInt (m->shownX.load() * 100.0f))
                        + " Y" + juce::String (juce::roundToInt (m->shownY.load() * 100.0f));
        const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getNonexistentChildFile (name, ".wav", false);
        saveChooser = std::make_unique<juce::FileChooser> ("Save the morph as a WAV file", suggested, "*.wav");
        // (the address, not the file: an Android pick is a document, not a path)
        saveChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [wav, tell] (const juce::FileChooser& fc)
        {
            const auto url = fc.getURLResult();
            if (url.isEmpty()) return;
            const auto problem = aqp::files::writeDocument (url, wav->getData(), wav->getSize());
            if (problem.isNotEmpty()) tell ("The WAV file could not be saved: " + problem + ".");
        });
    }

    void kitMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Random Kit: four sounds of");
        for (int k = 0; k < DrumMorphModule::kitNames().size(); ++k)
            menu.addItem (k + 1, DrumMorphModule::kitNames()[k], true, mod() != nullptr && mod()->lastKit == k);
        juce::Component::SafePointer<DrumMorphView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitButton), [safe] (int r)
        {
            if (safe == nullptr || safe->mod() == nullptr || r <= 0) return;
            safe->mod()->loadRandomKit (r - 1);
            safe->repaint();
        });
    }

    void cornerMenu (int i)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Load a sound ...");
        const bool has = ! mod()->corners[(size_t) i].raw.empty();
        menu.addItem (2, "Marks back to the whole sound", has);
        menu.addItem (3, "Clear this corner", has);
        juce::Component::SafePointer<DrumMorphView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, i] (int r)
        {
            if (safe == nullptr || safe->mod() == nullptr) return;
            if (r == 1) safe->chooseFile (i);
            if (r == 2) safe->mod()->setMarks (i, 0.0f, 1.0f);
            if (r == 3) safe->mod()->clearCorner (i);
            safe->repaint();
        });
    }

    void chooseFile (int i)
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a drum sound", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<DrumMorphView> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, i] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || safe->mod() == nullptr) return;
               #if JUCE_ANDROID
                const auto url = fc.getURLResult();
                if (url.isEmpty()) return;
                auto doc = juce::AndroidDocument::fromDocument (url);
                if (! doc.hasValue()) return;
                auto in = doc.createInputStream();
                if (in == nullptr) return;
                juce::MemoryBlock mb;
                in->readIntoMemoryBlock (mb);
                safe->mod()->loadCornerFromStream (i, std::make_unique<juce::MemoryInputStream> (std::move (mb)),
                                                   doc.getInfo().getName().upToLastOccurrenceOf (".", false, false));
               #else
                const auto f = fc.getResult();
                if (f.existsAsFile())
                    safe->mod()->loadCornerFromStream (i, std::make_unique<juce::FileInputStream> (f), f.getFileNameWithoutExtension());
               #endif
                safe->repaint();
            });
    }

    static juce::Colour cornerColour (int i)
    {
        static const juce::uint32 c[] = { 0xff7fd1ff, 0xffffb36b, 0xff9be07a, 0xffe08af0 };
        return juce::Colour (c[i & 3]);
    }

    void paintCorner (juce::Graphics& g, int i, juce::Rectangle<float> r, const drummorph::Corner& c)
    {
        const auto col = cornerColour (i);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.setColour (col);
        static const char* where[] = { "bottom left", "bottom right", "top left", "top right" };
        g.drawText (c.raw.empty() ? juce::String (where[i]) : c.name, r.reduced (6.0f, 3.0f).removeFromTop (12.0f), juce::Justification::centredLeft, true);
        if (c.raw.empty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("Tap to load a sound", r, juce::Justification::centred);
            return;
        }
        const auto wave = waveArea (r);
        // the curve being edited here, or the corner's own
        const bool editing = drag.corner == i && drag.point >= 0;
        const auto& cv = editing ? curveEdit : c.curve;
        // the waveform as Cove Groove's volume curve shows it: its outline with
        // a faint fill, as it is (fainter) and as the curve shapes it, over the
        // marked part; outside the marks, dim
        const int w = juce::jmax (2, (int) wave.getWidth());
        const double per = (double) c.raw.size() / (double) w;
        std::vector<float> peaks ((size_t) w, 0.0f);
        float top = 1.0e-6f;
        for (int px = 0; px < w; ++px)
        {
            const size_t a = (size_t) (px * per), b = std::min (c.raw.size(), (size_t) ((px + 1) * per) + 1);
            float mx = 0.0f;
            for (size_t k = a; k < b; ++k) mx = juce::jmax (mx, std::abs (c.raw[k]));
            peaks[(size_t) px] = mx;
            top = juce::jmax (top, mx);
        }
        for (auto& pk : peaks) pk = std::sqrt (pk / top) * 0.96f;   // (a gentle root, so the tail shows)
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        auto outline = [&] (int from, int to, bool shapedByCurve)
        {
            juce::Path p;
            to = juce::jmin (to, w - 1);
            if (to <= from) return p;
            auto k = [&] (int px)
            {
                if (! shapedByCurve) return 1.0f;
                const float frac = (float) px / (float) w;
                return cv.at (juce::jlimit (0.0f, 1.0f, (frac - c.startFrac) / juce::jmax (1.0e-4f, c.endFrac - c.startFrac)));
            };
            for (int px = from; px <= to; ++px)
            {
                const juce::Point<float> q (wave.getX() + (float) px, mid - peaks[(size_t) px] * k (px) * half);
                if (px == from) p.startNewSubPath (q); else p.lineTo (q);
            }
            for (int px = to; px >= from; --px)
                p.lineTo (wave.getX() + (float) px, mid + peaks[(size_t) px] * k (px) * half);
            p.closeSubPath();
            return p;
        };
        const int pxStart = juce::jlimit (0, w - 1, (int) (c.startFrac * (float) w));
        const int pxEnd = juce::jlimit (0, w - 1, (int) (c.endFrac * (float) w));
        // outside the marks
        g.setColour (col.withAlpha (0.18f));
        if (pxStart > 0) g.strokePath (outline (0, pxStart, false), juce::PathStrokeType (0.8f));
        if (pxEnd < w - 1) g.strokePath (outline (pxEnd, w - 1, false), juce::PathStrokeType (0.8f));
        // the marked part: as it is (faint), then as it plays
        if (! cv.isFlat())
        {
            g.setColour (col.withAlpha (0.3f));
            g.strokePath (outline (pxStart, pxEnd, false), juce::PathStrokeType (0.8f));
        }
        const auto shaped = outline (pxStart, pxEnd, true);
        g.setColour (col.withAlpha (0.12f));
        g.fillPath (shaped);
        g.setColour (col.withAlpha (0.9f));
        g.strokePath (shaped, juce::PathStrokeType (1.0f));

        // the start / end marks: lines, their handles at the bottom (the top
        // belongs to the curve's points)
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (float f : { c.startFrac, c.endFrac })
        {
            const float x = wave.getX() + wave.getWidth() * f;
            g.drawVerticalLine ((int) x, wave.getY(), wave.getBottom());
            g.fillRect (x - 3.0f, wave.getBottom() - 3.0f, 6.0f, 6.0f);
        }

        // the curve and its points (square handles, as in Cove Groove)
        {
            juce::Path line;
            for (int k = 0; k < cv.n; ++k)
            {
                const auto q = curveToScreen (i, c, cv, k);
                if (k == 0) line.startNewSubPath (q); else line.lineTo (q);
            }
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.strokePath (line, juce::PathStrokeType (3.2f));
            g.setColour (juce::Colour (0xffeadcc0));
            g.strokePath (line, juce::PathStrokeType (1.4f));
            for (int k = 0; k < cv.n; ++k)
            {
                const auto q = curveToScreen (i, c, cv, k);
                const auto hdl = juce::Rectangle<float> (8.0f, 8.0f).withCentre (q);
                const bool held = editing && k == drag.point;
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRect (hdl.expanded (1.0f).translated (0.0f, 1.0f));
                g.setColour (held ? col.brighter (0.4f) : juce::Colour (0xffeadcc0));
                g.fillRect (hdl);
                g.setColour (col.darker (0.3f));
                g.drawRect (hdl, 1.0f);
            }
        }
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        auto ms = [] (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s"; };
        g.drawText (ms (c.seconds()), r.reduced (6.0f, 3.0f).removeFromTop (12.0f), juce::Justification::centredRight, false);
    }

    void paintPad (juce::Graphics& g, DrumMorphModule& m)
    {
        const auto r = pad.toFloat();
        juce::ColourGradient grad (cornerColour (2).withAlpha (0.25f), r.getTopLeft(), cornerColour (1).withAlpha (0.25f), r.getBottomRight(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        const float x = m.getParameterBase (DrumMorphModule::pX) * 0.01f, y = m.getParameterBase (DrumMorphModule::pY) * 0.01f;
        const float lx = m.shownX.load(), ly = m.shownY.load();
        auto at = [&r] (float fx, float fy) { return juce::Point<float> (r.getX() + 10.0f + (r.getWidth() - 20.0f) * fx, r.getBottom() - 10.0f - (r.getHeight() - 20.0f) * fy); };
        // the grid points, brighter near the cursor
        for (int gy = 0; gy < drummorph::kGrid; ++gy)
            for (int gx = 0; gx < drummorph::kGrid; ++gx)
            {
                const float fx = (float) gx / (drummorph::kGrid - 1), fy = (float) gy / (drummorph::kGrid - 1);
                const float d = std::hypot (fx - lx, fy - ly);
                g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.12f, 0.8f, 0.8f - d * 1.6f)));
                const auto p = at (fx, fy);
                g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
            }
        // the corners' names
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        const juce::Point<float> cornerAt[] = { at (0, 0), at (1, 0), at (0, 1), at (1, 1) };
        for (int i = 0; i < drummorph::kCorners; ++i)
        {
            g.setColour (cornerColour (i));
            g.fillEllipse (cornerAt[i].x - 5.0f, cornerAt[i].y - 5.0f, 10.0f, 10.0f);
            const auto& c = m.corners[(size_t) i];
            if (! c.raw.empty())
            {
                const bool right = i % 2 == 1, upper = i >= 2;
                auto box = juce::Rectangle<float> (110.0f, 12.0f).withCentre (cornerAt[i].translated (right ? -60.0f : 60.0f, upper ? 11.0f : -11.0f));
                g.drawText (c.name, box, right ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
            }
        }
        // where it plays now (cables included), and the knobs' place
        const auto pl = at (lx, ly), pk = at (x, y);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawEllipse (pk.x - 9.0f, pk.y - 9.0f, 18.0f, 18.0f, 1.0f);
        g.setColour (juce::Colour (0xffffe08a));
        g.fillEllipse (pl.x - 6.0f, pl.y - 6.0f, 12.0f, 12.0f);
    }

    void timerCallback() override
    {
        // a long press on a middle point of a volume curve removes it
        if (longPressArmed && drag.corner >= 0 && drag.point > 0 && ! drag.moved
            && juce::Time::getMillisecondCounter() - pressTime > 480)
        {
            longPressArmed = false;
            if (auto* m = mod(); m != nullptr && drag.point < curveEdit.n - 1)
            {
                removeCurvePoint (drag.corner, drag.point);
                m->setCurve (drag.corner, curveEdit);
            }
            drag = {};
            repaint();
        }
        auto* m = mod();
        if (m == nullptr) return;
        const juce::String key = juce::String (m->shownX.load(), 3) + juce::String (m->shownY.load(), 3)
                               + juce::String (m->getParameterBase (DrumMorphModule::pX)) + juce::String (m->getParameterBase (DrumMorphModule::pY))
                               + juce::String (m->renderProgress(), 2) + (m->renderPending() ? "p" : "") + juce::String ((juce::int64) m->gridBytes())
                               + juce::String (m->getSampleChangeCounter());
        if (key != lastKey) { lastKey = key; repaint(); }
    }

    juce::WeakReference<SynthModule> module;
    juce::TextButton kitButton { "Random Kit" };
    juce::TextButton saveButton { "Save WAV" };   // (6.7.9)
    std::unique_ptr<juce::FileChooser> saveChooser;
    juce::Rectangle<int> panels[4], pad, status;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastKey;
};

std::unique_ptr<juce::Component> DrumMorphModule::createExtraContentComponent()
{
    return std::make_unique<DrumMorphView> (*this);
}

static ModuleDescriptor drumMorphDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.drummorph";
    d.displayName = "Drum Morpher";
    d.description =
        "Four drum sounds at the corners of a square, and the cursor anywhere in it plays a new drum made from them: "
        "lined up at their attack, decay and end, their cepstral tone shapes and pitched partials blended, their "
        "level envelopes and transients carried over. Tap one of the four panels to load a sound (the first 10 s, "
        "silent start cut off) or let Random Kit fill them from a built-in kit, drag the start / end marks, and shape each sound's volume between "
        "them with its curve (tap to add a point, drag it, double-tap or long-press to remove it). An 8 x 8 grid of morphs is made in the background; X and Y move freely "
        "while it plays. Plays like a sampler from MIDI In or the keys (Root plays it as it is); Trig In plays it at Root. "
        "Cepstral blends the morph with a plain crossfade, Transient lifts or tames the attack, Decay shortens it.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 186;
    d.searchKeywords = "drum morph sample xy pad cepstral blend kick snare";
    d.sockets = { modIn ("trigIn", "Trig In"), midiIn ("midiIn", "MIDI In"), audioOut ("audioOut", "Audio Out") };
    auto fft = makeCombo ("fft", "FFT Size", { "512", "1024", "2048", "4096" }, 2, 2, 2);
    fft.modulatable = false;
    auto root = makeRotary ("root", "Root", 24.0f, 96.0f, 60.0f, 1, {}, false, 1.0f);
    root.modulatable = false;
    d.params = {
        makeRotary ("x",         "X",         0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("y",         "Y",         0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("cepstral",  "Cepstral",  0.0f, 100.0f, 100.0f, 0, "%"),
        makeRotary ("transient", "Transient", -100.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("decay",     "Decay",     5.0f, 100.0f, 100.0f, 1, "%"),
        makeRotary ("pitch",     "Pitch",     -24.0f, 24.0f, 0.0f, 1, "st"),
        root,
        makeRotary ("level",     "Level",     -24.0f, 12.0f, 0.0f, 1, "dB"),
        fft
    };
    d.extraSection = "Corners and pad";
    return d;
}

AQUANODE_REGISTER_MODULE (DrumMorphModule, drumMorphDescriptor)

#if AQUANODE_TEST_HARNESS
// (the desktop test harness only)
void drumMorphRandomKit (aquanode::SynthModule* m, int kit)
{
    if (auto* d = dynamic_cast<DrumMorphModule*> (m)) d->loadRandomKit (kit);
}
juce::String drumMorphCornerNames (aquanode::SynthModule* m)
{
    juce::String s;
    if (auto* d = dynamic_cast<DrumMorphModule*> (m))
        for (const auto& c : d->corners) s << c.name << " (" << juce::String (c.seconds(), 2) << " s) | ";
    return s;
}
void drumMorphSetCurve (aquanode::SynthModule* m, int corner, const juce::String& curve)
{
    if (auto* d = dynamic_cast<DrumMorphModule*> (m))
    {
        drummorph::VolumeCurve c;
        if (c.fromString (curve)) d->setCurve (corner, c);
    }
}
float drumMorphCornerRms (aquanode::SynthModule* m, int corner, float fromFrac, float toFrac)
{
    auto* d = dynamic_cast<DrumMorphModule*> (m);
    if (d == nullptr) return -1.0f;
    const auto& a = d->corners[(size_t) corner].audio;
    const size_t s = (size_t) (fromFrac * (float) a.size()), e = (size_t) (toFrac * (float) a.size());
    double sq = 0.0;
    for (size_t i = s; i < e && i < a.size(); ++i) sq += (double) a[i] * a[i];
    return (float) std::sqrt (sq / (double) juce::jmax<size_t> (1, e - s));
}
// Save WAV's hit and file, for tests: its length in samples, written to the file
int drumMorphSaveHit (aquanode::SynthModule* m, const juce::File& f)
{
    auto* d = dynamic_cast<DrumMorphModule*> (m);
    if (d == nullptr) return -1;
    double rate = 0.0;
    auto hit = d->renderCurrentHit (rate);
    if (hit.empty()) return 0;
    const auto wav = DrumMorphModule::encodeWav (hit, rate);
    const auto problem = aqp::files::writeDocument (juce::URL (f), wav.getData(), wav.getSize());
    return problem.isEmpty() ? (int) hit.size() : -2;
}
void drumMorphRenderNow (aquanode::SynthModule* m)
{
    if (auto* d = dynamic_cast<DrumMorphModule*> (m)) d->renderNowBlocking();
}
#endif
