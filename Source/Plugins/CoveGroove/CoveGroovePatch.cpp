//==============================================================================
// Cove Groove - Patch version
//
//   Sequencer   the pattern and its generator (GENERATE / the step grid on the
//               card), the clock (the DAW's transport when it plays, else Run
//               at Tempo or at the patch's tempo) and Swing; one gate out per
//               lane. A gate carries the step's level and its second note:
//               0.6..1.0 for a plain step, 1.2..1.6 for a second-note step,
//               so any drum's Trig In (rising past 0.5) takes it too.
//   Kick, Bass, Clap, Closed Hat, Open Hat, Cymbal, Perc, FX Hits
//               one drum each, Cove Groove's own voice: Trig In (with the
//               gate's level and note), Choke In (the open hat, choked by the
//               closed hat), Midi In / the keys (its own key only: C2.. for the
//               lanes in order, C3.. for their second notes, as on the card);
//               Audio Out at Level and Pan, Send Out for the delay. Its card
//               holds its own sample (Sound: Sample) and the volume curve over
//               it; the sample is saved with the patch.
//   Bus         the busses: Top In through the phaser and the moving
//               band-pass, Send In into the tempo delay (its repeats join the
//               top bus before the band-pass), Dry In straight on; all into
//               the drive and Volume
//
// Every module runs Cove Groove's own code, so the patch plays what the card
// plays.
//==============================================================================
#include "../Host/PatchKit.h"
#include "../../PluginSources/covegroove/CgEngine.h"
#include "../../PluginSources/covegroove/CgSampleIO.h"
#include "../../PluginSources/covegroove/CgSampleView.h"
#if JUCE_ANDROID
 #include "../../Platform/AndroidFileChooserShim.h"     // a picked document as a real file
#endif

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "covegroove" };
    using namespace covegroove;

    juce::StringArray styleNames() { juce::StringArray a; for (int s = 0; s < NumStyles; ++s) a.add (styleName (s)); return a; }
    juce::StringArray modelNames (int lane) { juce::StringArray a; for (int m = 0; m < kNumModels; ++m) a.add (modelName (lane, m)); return a; }
    const float kDelaySteps[6] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
    const float kFilterBars[5] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };

    float encodeGate (float vel, bool alt) { return (alt ? 1.2f : 0.6f) + 0.4f * clampf (vel, 0.0f, 1.0f); }
    void decodeGate (float g, float& vel, bool& alt)
    {
        alt = g > 1.1f;
        vel = clampf ((g - (alt ? 1.2f : 0.6f)) / 0.4f, 0.0f, 1.0f);
    }

    // where the groove is: the DAW's bars when it plays, else a clock of our own
    struct GrooveClock
    {
        double ownPos16 { 0.0 }, blockPos16 { 0.0 }, inc16 { 0.0 };
        bool host { false };
        int sampleInBlock { 0 };

        void blockStart (juce::AudioPlayHead* ph, bool hostPlaying)
        {
            host = false;
            sampleInBlock = 0;
            if (ph != nullptr && hostPlaying)
                if (auto info = ph->getPosition())
                    if (auto q = info->getPpqPosition())
                    {
                        host = true;
                        blockPos16 = *q * 4.0;
                    }
        }
        double pos() const { return host ? blockPos16 + inc16 * sampleInBlock : ownPos16; }
        void tick (bool running) { ++sampleInBlock; if (! host && running) ownPos16 += inc16; }
    };
}

//==============================================================================
// Sequencer
//==============================================================================
class CoveGrooveSequencerModule final : public SynthModule
{
public:
    enum P { pRun, pTempo, pClock, pBars, pStyle, pDensity, pVariation, pSwing };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.covegroove.sequencer";
        d.displayName = kit::moduleName (kPlugin, "Sequencer");
        d.description = "Cove Groove's pattern, generator and clock: GENERATE writes a hardgroove pattern in the Style "
                        "(Density, Variation), the grid edits it (click, drag, right-click for the second note). It "
                        "follows the DAW's transport when it plays, else plays when Run is on, at Tempo or the patch's "
                        "tempo. One gate per lane, carrying the step's level and note.";
        d.section = ModuleSection::Utility;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        for (int l = 0; l < kNumLanes; ++l)
            d.sockets.push_back (modOut ("gate" + juce::String (l), laneName (l)));
        d.params = {
            makeCombo ("run", "Run", { "Stop", "Run" }, 0, 0, 1).noMod(),
            makeRotary ("tempo", "Tempo", 70.0f, 190.0f, 138.0f, 0, "BPM", false, 0.1f),
            makeCombo ("clock", "Clock", { "Own Tempo", "Patch Tempo" }, 0, 0, 2).noMod(),
            makeCombo ("bars", "Bars", { "1", "2", "4" }, 1, 1, 1).noMod(),
            makeCombo ("style", "Style", styleNames(), 0, 1, 2).noMod(),
            makeRotary ("density", "Density", 0.0f, 100.0f, 55.0f, 1, "%", false, 1.0f),
            makeRotary ("variation", "Variation", 0.0f, 100.0f, 40.0f, 2, "%", false, 1.0f),
            makeRotary ("swing", "Swing", 50.0f, 75.0f, 60.0f, 2, "%", false, 0.1f)
        };
        return d;
    }

    CoveGrooveSequencerModule()
    {
        Generator::Settings gs;
        gs.bars = 2;
        gs.models[Cymbal] = 2;
        Generator::generate (pattern, gs, 7919u);
        audioPattern = pattern;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    int preferredModuleWidth() const override { return 430; }
    int extraContentHeight() const override { return 150; }
    std::unique_ptr<juce::Component> createExtraContentComponent() override;

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        seq.reset();
        clock = GrooveClock();
        for (auto& g : gateLeft) g = 0;
        chunkLeft = 0; numEvents = 0;
        wasRunning = false;
    }

    void blockStart() override
    {
        if (const int s = serial.load (std::memory_order_acquire); s != audioSerial)
        {
            const juce::SpinLock::ScopedTryLockType lk (lock);
            if (lk.isLocked()) { audioPattern = pattern; audioSerial = s; }
        }
        clock.blockStart (hostPlayHead, hostTransport.isPlaying);
    }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        const bool run = clock.host || getParameterBase (pRun) >= 0.5f;
        if (run && ! wasRunning && ! clock.host) { clock.ownPos16 = 0.0; seq.reset(); }
        wasRunning = run;
        const double bpm = juce::jlimit (20.0, 400.0, (double) (getParameterBase (pClock) >= 0.5f ? tempoBpm : param (pTempo)));
        clock.inc16 = bpm / 60.0 * 4.0 / sampleRate;
        seq.swing = param (pSwing) * 0.01f;

        // the steps of the next 16 samples, worked out ahead
        if (chunkLeft <= 0)
        {
            numEvents = 0;
            chunkLeft = kChunk;
            chunkSample = 0;
            if (run)
                seq.advance (audioPattern, clock.pos(), clock.inc16, kChunk, [this] (int lane, float v, bool alt, int off)
                {
                    if (numEvents < kMaxEvents) events[numEvents++] = { lane, off, encodeGate (v, alt) };
                });
            else
                seq.reset();
            if (run) playStep = Sequencer::stepAt (audioPattern, clock.pos()); else playStep = -1;
        }
        for (int e = 0; e < numEvents; ++e)
            if (events[e].offset == chunkSample)
            {
                gateLevel[(size_t) events[e].lane] = events[e].level;
                gateLeft[(size_t) events[e].lane] = juce::jmax (1, (int) (0.002 * sampleRate));
            }
        for (int l = 0; l < kNumLanes; ++l)
        {
            float g = 0.0f;
            if (gateLeft[(size_t) l] > 0) { g = gateLevel[(size_t) l]; --gateLeft[(size_t) l]; }
            outputs[l] = { g, g };
        }
        ++chunkSample;
        --chunkLeft;
        clock.tick (run);
    }

    //=== the pattern (message thread) =========================================
    Pattern getPattern() const { const juce::SpinLock::ScopedLockType lk (lock); return pattern; }
    void setPattern (const Pattern& p)
    {
        { const juce::SpinLock::ScopedLockType lk (lock); pattern = p; }
        serial.fetch_add (1, std::memory_order_release);
        ++version;
    }
    void generate (unsigned mask = 0xffu)
    {
        Generator::Settings gs;
        gs.style = (int) std::lround (getParameterBase (pStyle));
        gs.density = getParameterBase (pDensity) * 0.01f;
        gs.variation = getParameterBase (pVariation) * 0.01f;
        static const int barChoices[3] = { 1, 2, 4 };
        gs.bars = barChoices[juce::jlimit (0, 2, (int) std::lround (getParameterBase (pBars)))];
        gs.models = models;
        auto p = getPattern();
        Generator::generate (p, gs, (uint32_t) juce::Time::getMillisecondCounter() * 2654435761u + (++seedCounter) * 40503u, mask);
        setPattern (p);
    }
    // the bars knob: the pattern grows (repeating) or shrinks
    void followBars()
    {
        static const int barChoices[3] = { 1, 2, 4 };
        const int bars = barChoices[juce::jlimit (0, 2, (int) std::lround (getParameterBase (pBars)))];
        auto p = getPattern();
        if (p.bars == bars) return;
        const int oldLen = p.length();
        for (auto& lane : p.lanes)
            for (int s = oldLen; s < bars * kStepsPerBar; ++s) lane[(size_t) s] = lane[(size_t) (s % oldLen)];
        p.bars = bars;
        setPattern (p);
    }

    // the pattern, then ";m" and the lanes' sounds (comma-separated; one
    // digit each in patches from before there were more than ten)
    juce::String saveCustomState() const override
    {
        juce::String s (getPattern().toString());
        s << ";m";
        for (int l = 0; l < kNumLanes; ++l) s << (l ? "," : "") << models[(size_t) l];
        return s;
    }
    void loadCustomState (const juce::String& state) override
    {
        Pattern p;
        if (p.fromString (state.upToFirstOccurrenceOf (";", false, false).toStdString())) setPattern (p);
        const auto m = state.fromFirstOccurrenceOf (";m", false, false);
        if (m.containsChar (','))
        {
            const auto parts = juce::StringArray::fromTokens (m, ",", {});
            for (int l = 0; l < kNumLanes && l < parts.size(); ++l) models[(size_t) l] = juce::jlimit (0, kNumModels - 1, parts[l].getIntValue());
        }
        else
            for (int l = 0; l < kNumLanes && l < m.length(); ++l) models[(size_t) l] = juce::jlimit (0, kNumModels - 1, (int) (m[l] - '0'));
    }

    std::array<int, kNumLanes> models {};       // the drums' sounds, for the generator (set by the builder)
    std::atomic<int> playStep { -1 };
    std::atomic<int> version { 0 };

private:
    static constexpr int kChunk = 16, kMaxEvents = 64;
    struct Event { int lane, offset; float level; };

    Sequencer seq;
    GrooveClock clock;
    Pattern pattern, audioPattern;
    juce::SpinLock lock;
    std::atomic<int> serial { 0 };
    int audioSerial { -1 };
    uint32_t seedCounter { 1 };
    std::array<float, kNumLanes> gateLevel {};
    std::array<int, kNumLanes> gateLeft {};
    Event events[kMaxEvents] {};
    int numEvents { 0 }, chunkLeft { 0 }, chunkSample { 0 };
    bool wasRunning { false };
};

namespace
{
    // the sequencer's card: the step grid (a step's bar as tall as it is loud;
    // the same touch gestures as the plugin's card), GENERATE
    class SequencerView final : public juce::Component, private juce::Timer
    {
    public:
        explicit SequencerView (CoveGrooveSequencerModule& m) : module (&m)
        {
            generate.setButtonText ("GENERATE");
            generate.onClick = [this] { if (auto* s = seq()) s->generate(); };
            addAndMakeVisible (generate);
            startTimerHz (25);
        }

        void resized() override { generate.setBounds (getLocalBounds().removeFromBottom (22).removeFromRight (110)); }

        void paint (juce::Graphics& g) override
        {
            auto* s = seq();
            if (s == nullptr) return;
            const auto r = gridArea().toFloat();
            g.setColour (juce::Colour (0xff1b1511));
            g.fillRect (r.expanded (2.0f));
            const int L = shown.length();
            const float w = r.getWidth() / (float) L;
            // the plugin card's lane colours, blue through cyan to green
            static const juce::uint32 cols[kNumLanes] = { 0xff3d7bff, 0xff2a9dff, 0xff14c3ff, 0xff00e1f0, 0xff1ee8c0, 0xff22e08d, 0xff52e05a, 0xff9be83a };
            for (int l = 0; l < kNumLanes; ++l)
                for (int st = 0; st < L; ++st)
                {
                    const auto c = slot (l, st);
                    const auto& step = shown.lanes[(size_t) l][(size_t) st];
                    g.setColour (st % 4 == 0 ? juce::Colour (0xff3b3028) : juce::Colour (0xff2c241e));
                    g.fillRect (c);
                    if (step.vel > 0)
                    {
                        const auto bar = c.withTop (c.getBottom() - c.getHeight() * (0.12f + 0.88f * (float) step.vel / 127.0f));
                        g.setColour (juce::Colour (cols[l]));      // one flat colour, as tall as loud
                        g.fillRect (bar);
                        if (step.alt)
                        {
                            g.setColour (juce::Colour (0xffeadcc0));
                            g.fillRect (bar.getX(), bar.getY(), bar.getWidth(), juce::jmin (3.0f, bar.getHeight()));
                        }
                    }
                }
            g.setColour (juce::Colour (0xff1ad6ec).withAlpha (0.45f));
            for (int b = 1; b < shown.bars; ++b) g.fillRect (r.getX() + (float) (b * kStepsPerBar) * w - 0.5f, r.getY(), 1.0f, r.getHeight());
            if (shownStep >= 0 && shownStep < L)
            {
                g.setColour (juce::Colour (0xff1ad6ec).withAlpha (0.16f));
                g.fillRect (r.getX() + (float) shownStep * w, r.getY(), w, r.getHeight());
            }
            g.setColour (juce::Colour (0xffb2a189));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText ("tap: on (as loud as the height)   drag up / down: level   drag across: paint   long-press: second note",
                        getLocalBounds().removeFromBottom (22).withTrimmedRight (116), juce::Justification::centredLeft, true);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* s = seq();
            int lane, step;
            gesture = Gesture::None;
            if (s == nullptr || ! cellAt (e.getPosition(), lane, step)) return;
            editing = s->getPattern();
            auto& st = editing.lanes[(size_t) lane][(size_t) step];
            if (e.mods.isPopupMenu()) { if (st.vel == 0) st.vel = 100; st.alt = ! st.alt; s->setPattern (editing); return; }
            pressLane = lane; pressStep = step; pressPos = e.getPosition();
            pressTime = juce::Time::getMillisecondCounter();
            pressWasOn = st.vel > 0;
            if (! pressWasOn) { st = { velocityAt (lane, (float) e.y), false }; s->setPattern (editing); }
            paintVel = st.vel;
            gesture = Gesture::Undecided;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* s = seq();
            if (s == nullptr || gesture == Gesture::None) return;
            const auto d = e.getPosition() - pressPos;
            if (gesture == Gesture::Undecided)
            {
                if (std::abs (d.x) < 5 && std::abs (d.y) < 5) return;
                gesture = std::abs (d.y) > std::abs (d.x) ? Gesture::Velocity : Gesture::Paint;
                paintOn = ! pressWasOn;
                if (gesture == Gesture::Paint && pressWasOn) { editing.lanes[(size_t) pressLane][(size_t) pressStep] = Step(); s->setPattern (editing); }
            }
            if (gesture == Gesture::Velocity)
            {
                auto& st = editing.lanes[(size_t) pressLane][(size_t) pressStep];
                const uint8_t v = velocityAt (pressLane, (float) e.y);
                if (st.vel != v) { st.vel = v; s->setPattern (editing); }
            }
            else
            {
                int lane, step;
                const auto r = gridArea();
                if (! cellAt ({ juce::jlimit (r.getX(), r.getRight() - 1, e.x), r.getY() + pressLane * r.getHeight() / kNumLanes + 1 }, lane, step)) return;
                bool changed = false;
                for (int i = juce::jmin (step, pressStep); i <= juce::jmax (step, pressStep); ++i)
                {
                    auto& st = editing.lanes[(size_t) pressLane][(size_t) i];
                    if ((st.vel > 0) != paintOn) { st = paintOn ? Step { paintVel, false } : Step(); changed = true; }
                }
                if (changed) s->setPattern (editing);
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (auto* s = seq(); s != nullptr && gesture == Gesture::Undecided && pressWasOn)
            {
                editing.lanes[(size_t) pressLane][(size_t) pressStep] = Step();
                s->setPattern (editing);
            }
            gesture = Gesture::None;
        }

    private:
        enum class Gesture { None, Undecided, Velocity, Paint };
        CoveGrooveSequencerModule* seq() const { return dynamic_cast<CoveGrooveSequencerModule*> (module.get()); }
        juce::Rectangle<int> gridArea() const { return getLocalBounds().reduced (2).withTrimmedBottom (26); }
        juce::Rectangle<float> slot (int lane, int step) const
        {
            const auto r = gridArea().toFloat();
            const float w = r.getWidth() / (float) shown.length(), h = r.getHeight() / (float) kNumLanes;
            return juce::Rectangle<float> (r.getX() + (float) step * w, r.getY() + (float) lane * h, w, h).reduced (0.6f, 1.0f);
        }
        uint8_t velocityAt (int lane, float y) const
        {
            const auto c = slot (lane, 0);
            const float frac = juce::jlimit (0.0f, 1.0f, (c.getBottom() - y) / juce::jmax (1.0f, c.getHeight()));
            return (uint8_t) juce::jlimit (20, 127, (int) std::lround (20.0f + 107.0f * frac));
        }
        bool cellAt (juce::Point<int> p, int& lane, int& step) const
        {
            const auto r = gridArea();
            if (! r.contains (p)) return false;
            lane = juce::jlimit (0, kNumLanes - 1, (p.y - r.getY()) * kNumLanes / juce::jmax (1, r.getHeight()));
            step = juce::jlimit (0, shown.length() - 1, (p.x - r.getX()) * shown.length() / juce::jmax (1, r.getWidth()));
            return true;
        }
        void timerCallback() override
        {
            auto* s = seq();
            if (s == nullptr) return;
            // a long press: the second note
            if (gesture == Gesture::Undecided && juce::Time::getMillisecondCounter() - pressTime > 450)
            {
                auto& st = editing.lanes[(size_t) pressLane][(size_t) pressStep];
                if (st.vel == 0) st.vel = 100;
                st.alt = ! st.alt;
                gesture = Gesture::None;
                s->setPattern (editing);
            }
            s->followBars();
            if (s->version.load() != shownVersion) { shownVersion = s->version.load(); shown = s->getPattern(); repaint(); }
            if (s->playStep.load() != shownStep) { shownStep = s->playStep.load(); repaint(); }
        }

        juce::WeakReference<SynthModule> module;
        juce::TextButton generate;
        Pattern shown, editing;
        int shownVersion { -1 }, shownStep { -2 };
        Gesture gesture { Gesture::None };
        int pressLane { 0 }, pressStep { 0 };
        bool pressWasOn { false }, paintOn { false };
        uint8_t paintVel { 100 };
        juce::Point<int> pressPos;
        juce::uint32 pressTime { 0 };
    };
}

std::unique_ptr<juce::Component> CoveGrooveSequencerModule::createExtraContentComponent()
{
    return std::make_unique<SequencerView> (*this);
}

//==============================================================================
// a drum's own sample and its volume curve (what every drum module shares)
//==============================================================================
class CoveGrooveDrumBase : public SynthModule
{
public:
    explicit CoveGrooveDrumBase (int laneType) : lane (laneType) {}
    const int lane;

    //=== the sample (message thread) ==========================================
    bool loadSampleFile (const juce::File& f)
    {
        auto d = samples::readFile (f);
        if (d == nullptr) return false;
        setSample (std::move (d), f.getFileNameWithoutExtension());
        setParameter ("model", (float) kSampleModel);
        return true;
    }
    void setSample (std::shared_ptr<const SampleData> d, const juce::String& n)
    {
        const juce::ScopedLock sl (mutex);
        if (sample != nullptr) retired.push_back (sample);
        sample = std::move (d);
        name = sample != nullptr ? n : juce::String();
        publish();
    }
    void setCurve (const Curve& c)
    {
        const juce::ScopedLock sl (mutex);
        curve = c;
        curve.tidy();
        publish();
    }
    std::shared_ptr<const SampleData> getSample() const { const juce::ScopedLock sl (mutex); return sample; }
    Curve getCurve() const { const juce::ScopedLock sl (mutex); return curve; }
    juce::String getSampleName() const { const juce::ScopedLock sl (mutex); return name; }
    void chooseSampleFile();
    void audition() { auditionFlag = true; }
    std::atomic<int> sampleVersion { 0 };

    //=== kept with the patch ==================================================
    bool hasAudioToSave() const override { return getSample() != nullptr; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        const auto d = getSample();
        if (d == nullptr) return nullptr;
        rate = d->rate;
        return std::make_shared<juce::AudioBuffer<float>> (samples::toBuffer (*d));
    }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override
    {
        setSample (audio != nullptr ? samples::fromBuffer (*audio, rate, false) : nullptr, getSampleName());
    }
    // "c<curve>" and "n<name>", a line each
    juce::String saveCustomState() const override
    {
        const juce::ScopedLock sl (mutex);
        if (curve.isFlat() && name.isEmpty()) return {};
        return "c" + juce::String (curve.toString()) + "\nn" + name;
    }
    void loadCustomState (const juce::String& state) override
    {
        Curve c;
        juce::String n;
        for (const auto& line : juce::StringArray::fromLines (state))
        {
            if (line.startsWithChar ('c')) c.fromString (line.substring (1).toStdString());
            else if (line.startsWithChar ('n')) n = line.substring (1);
        }
        const juce::ScopedLock sl (mutex);
        curve = c;
        name = sample != nullptr || n.isNotEmpty() ? n : juce::String();
        publish();
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 96; }

    // the audio thread's copy, when it changed
    void blockStart() override
    {
        if (const int s = serial.load (std::memory_order_acquire); s != audioSerial)
        {
            const juce::SpinLock::ScopedTryLockType lk (handoffLock);
            if (lk.isLocked()) { audioSample = handoffSample; audioCurve = handoffCurve; audioSerial = s; }
        }
    }

protected:
    std::shared_ptr<const SampleData> audioSample;      // the audio thread's
    Curve audioCurve;
    std::atomic<bool> auditionFlag { false };

private:
    // the audio thread takes the new one; a replaced sample waits in retired
    // until no voice holds it, so it is never freed on the audio thread
    void publish()
    {
        {
            const juce::SpinLock::ScopedLockType lk (handoffLock);
            handoffSample = sample;
            handoffCurve = curve;
        }
        serial.fetch_add (1, std::memory_order_release);
        ++sampleVersion;
        retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& d) { return d.use_count() <= 1; }), retired.end());
    }

    mutable juce::CriticalSection mutex;
    std::shared_ptr<const SampleData> sample;
    Curve curve;
    juce::String name;
    juce::SpinLock handoffLock;
    std::shared_ptr<const SampleData> handoffSample;
    Curve handoffCurve;
    std::atomic<int> serial { 0 };
    int audioSerial { -1 };
    std::vector<std::shared_ptr<const SampleData>> retired;
   #if JUCE_ANDROID
    std::unique_ptr<juce::AqpFileChooser> chooser;
   #else
    std::unique_ptr<juce::FileChooser> chooser;
   #endif
};

void CoveGrooveDrumBase::chooseSampleFile()
{
   #if JUCE_ANDROID
    using Chooser = juce::AqpFileChooser;
   #else
    using Chooser = juce::FileChooser;
   #endif
    chooser = std::make_unique<Chooser> ("A sample for Cove Groove's " + juce::String (laneName (lane)),
                                         juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                         "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::WeakReference<SynthModule> weak (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [weak] (const Chooser& fc)
                          {
                              if (auto* self = dynamic_cast<CoveGrooveDrumBase*> (weak.get()))
                                  if (const auto f = fc.getResult(); f.existsAsFile())
                                      self->loadSampleFile (f);
                          });
}

namespace
{
    // the drum card's strip: the sample and its curve, LOAD / CLEAR / SHAPE / PLAY
    class DrumSampleView final : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit DrumSampleView (CoveGrooveDrumBase& m) : module (&m)
        {
            curveEditor.accent = juce::Colour (0xff1ad6ec);
            curveEditor.emptyText = "Sound: Sample - LOAD a file";
            curveEditor.onChange = [this] (const Curve& c) { if (auto* d = drum()) d->setCurve (c); };
            addAndMakeVisible (curveEditor);
            load.onClick = [this] { if (auto* d = drum()) d->chooseSampleFile(); };
            clear.onClick = [this] { if (auto* d = drum()) d->setSample (nullptr, {}); };
            play.onClick = [this] { if (auto* d = drum()) d->audition(); };
            shape.onClick = [this]
            {
                juce::PopupMenu m;
                for (int i = 0; i < samples::kNumShapes; ++i) m.addItem (i + 1, samples::shapeName (i));
                // made here, not in the inner lambda's init-capture: there MSVC takes "this" for the
                // outer lambda's own closure (C2440 / C2439)
                juce::Component::SafePointer<DrumSampleView> safe (this);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shape),
                                 [safe] (int r)
                                 {
                                     if (safe != nullptr && r > 0)
                                         if (auto* d = safe->drum()) d->setCurve (samples::shape (r - 1));
                                 });
            };
            load.setTooltip ("A sound file of your own for this drum (Sound: Sample plays it)");
            shape.setTooltip ("A ready-made volume curve");
            for (auto* b : { &load, &clear, &shape, &play }) addAndMakeVisible (*b);
            refresh();
            startTimerHz (10);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2);
            auto row = r.removeFromBottom (22);
            r.removeFromBottom (4);
            curveEditor.setBounds (r);
            const int w = (row.getWidth() - 9) / 4;
            for (auto* b : { &load, &clear, &shape, &play }) { b->setBounds (row.removeFromLeft (w)); row.removeFromLeft (3); }
        }

    private:
        CoveGrooveDrumBase* drum() const { return dynamic_cast<CoveGrooveDrumBase*> (module.get()); }
        void refresh()
        {
            if (auto* d = drum())
            {
                curveEditor.setSample (d->getSample());
                curveEditor.setCurve (d->getCurve());
                shown = d->sampleVersion.load();
                clear.setEnabled (d->getSample() != nullptr);
                if (paramValuesChangedByContent) paramValuesChangedByContent();    // LOAD set the Sound to Sample
            }
        }
        void timerCallback() override
        {
            if (auto* d = drum(); d != nullptr && d->sampleVersion.load() != shown) refresh();
        }

        juce::WeakReference<SynthModule> module;
        CurveEditor curveEditor;
        juce::TextButton load { "LOAD" }, clear { "CLEAR" }, shape { "SHAPE" }, play { "PLAY" };
        int shown { -1 };
    };
}

std::unique_ptr<juce::Component> CoveGrooveDrumBase::createExtraContentComponent()
{
    return std::make_unique<DrumSampleView> (*this);
}

//==============================================================================
// one drum
//==============================================================================
template <int Lane>
class CoveGrooveDrumModule final : public CoveGrooveDrumBase
{
public:
    enum P { pModel, pTune, pDecay, pTone, pLevel, pPan, pSend };

    static ModuleDescriptor descriptor()
    {
        static const char* ids[kNumLanes] = { "kick", "bass", "clap", "closedhat", "openhat", "cymbal", "perc", "fxhit" };
        ModuleDescriptor d;
        d.typeId = juce::String ("plugin.covegroove.") + ids[Lane];
        d.displayName = kit::moduleName (kPlugin, Lane == Fx ? juce::String ("FX Hits") : juce::String (laneName (Lane)));
        d.description = juce::String ("Cove Groove's ") + laneName (Lane) + " voice. Trig In takes the Sequencer's gate (or any gate), "
                        "Choke In cuts it short, Midi In / the keys play it on its own key (" + juce::MidiMessage::getMidiNoteName (36 + Lane, true, true, 4)
                        + "; an octave up for its second note). Audio Out at Level and Pan, Send Out for the delay. "
                        "Sound: Sample plays a file of your own (LOAD on the card, up to 10 s), shaped by the volume curve "
                        "drawn over it; the sample is saved with the patch.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 401 + Lane;
        d.sockets = { modIn ("trigIn", "Trig In"), modIn ("chokeIn", "Choke In"), midiIn ("midiIn", "Midi In"),
                      audioOut ("audioOut", "Audio Out"), audioOut ("sendOut", "Send Out") };
        d.params = {
            makeCombo ("model", "Sound", modelNames (Lane), 0, 0, 2).noMod(),
            makeRotary ("tune", "Tune", -24.0f, 24.0f, 0.0f, 1, "st", false, 0.1f),
            makeRotary ("decay", "Decay", 25.0f, 400.0f, 100.0f, 1, "%", true, 1.0f),
            makeRotary ("tone", "Tone", 0.0f, 100.0f, 50.0f, 1, "%", false, 0.1f),
            makeRotary ("level", "Level", 0.0f, 150.0f, 80.0f, 0, "%", false, 0.1f),
            makeRotary ("pan", "Pan", -100.0f, 100.0f, 0.0f, 0, {}, false, 0.1f),
            makeRotary ("send", "Send", 0.0f, 100.0f, 0.0f, 1, "%", false, 0.1f)
        };
        return d;
    }

    CoveGrooveDrumModule() : CoveGrooveDrumBase (Lane) {}

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool acceptsGlobalMidiNotes() const override { return true; }
    bool appliesOwnVelocity() const override { return true; }
    double voiceTailSeconds() const override { return 0.05; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        for (auto& v : voices) v.prepare ((float) sr);
        for (auto& v : hitVoices) v.prepare ((float) sr);
        reset();
    }
    void reset() override
    {
        for (auto& v : voices) v.prepare ((float) sampleRate);
        for (auto& v : hitVoices) v.prepare ((float) sampleRate);
        lastTrig = lastChoke = 0.0f;
    }

    // a note from the keys or Midi In: only this lane's own key plays it
    void voiceNoteOn (int slot, int note, bool) override
    {
        if (slot <= 0 || slot >= kMaxVoices) return;
        const bool alt = note == 48 + Lane;
        if (note != 36 + Lane && ! alt) return;
        hitVelocity[(size_t) slot] = 1.0f;
        startVoice (hitVoices[(size_t) slot], 1.0f, alt);
        pendingHit[(size_t) slot] = true;
    }
    void voiceVelocity (int slot, float v) override
    {
        if (slot > 0 && slot < kMaxVoices && pendingHit[(size_t) slot])
        {
            pendingHit[(size_t) slot] = false;
            hitVelocity[(size_t) slot] = v;
        }
    }
    void voiceReset (int slot) override { if (slot > 0 && slot < kMaxVoices) hitVoices[(size_t) slot].prepare ((float) sampleRate); }

    // the hit slots (keys / Midi In)
    void processVoiceSample (int slot, const StereoFrame*, StereoFrame* out) override
    {
        float l = 0.0f, r = 0.0f;
        if (slot > 0 && slot < kMaxVoices && hitVoices[(size_t) slot].isActive())
            hitVoices[(size_t) slot].render (l, r);
        const float g = slot > 0 && slot < kMaxVoices ? hitVelocity[(size_t) slot] : 1.0f;
        write (l * g, r * g, out);
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float trig = in[0][0], choke = in[1][0];
        if (trig > 0.5f && lastTrig <= 0.5f)
        {
            float vel; bool alt;
            decodeGate (trig, vel, alt);
            if (Lane == Kick || Lane == Bass || Lane == ClosedHat || Lane == OpenHat)
                for (auto& v : voices) v.fadeOut (Lane == OpenHat ? 0.012f : 0.004f);
            startVoice (voices[(size_t) next], vel, alt);
            next = (next + 1) % kVoices;
        }
        lastTrig = trig;
        if (auditionFlag.exchange (false))
        {
            startVoice (voices[(size_t) next], 0.85f, false);
            next = (next + 1) % kVoices;
        }
        if (choke > 0.5f && lastChoke <= 0.5f)
            for (auto& v : voices) v.fadeOut (0.010f);
        lastChoke = choke;

        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
            if (v.isActive()) { float a, b; v.render (a, b); l += a; r += b; }
        write (l, r, out);
        renderGlobalMidiHits (in, out, 0);
    }

private:
    static constexpr int kVoices = 3;

    void startVoice (DrumVoice& v, float vel, bool alt)
    {
        Sound s;
        s.model = juce::jlimit (0, kNumModels - 1, (int) std::lround (getParameterBase (pModel)));
        s.tune = param (pTune);
        s.decay = param (pDecay) * 0.01f;
        s.tone = param (pTone) * 0.01f;
        s.sample = audioSample;
        s.curve = audioCurve;
        v.start (Lane, s, vel, alt, ++seed);
    }

    void write (float l, float r, StereoFrame* out)
    {
        float gl, gr;
        panGains (param (pPan) * 0.01f, gl, gr);
        const float lvl = param (pLevel) * 0.01f;
        l *= lvl * gl; r *= lvl * gr;
        out[0] = { l, r };
        const float send = param (pSend) * 0.01f;
        out[1] = { l * send, r * send };
    }

    std::array<DrumVoice, kVoices> voices {};
    std::array<DrumVoice, kMaxVoices> hitVoices {};
    std::array<float, kMaxVoices> hitVelocity {};
    std::array<bool, kMaxVoices> pendingHit {};
    int next { 0 };
    float lastTrig { 0.0f }, lastChoke { 0.0f };
    uint32_t seed { 77 };
};

//==============================================================================
// the busses
//==============================================================================
class CoveGrooveFxModule final : public SynthModule
{
public:
    enum P { pPhaser, pPhaserRate, pFilterFreq, pFilterReso, pFilterMotion, pFilterMix, pFilterRate, pFilterShape,
             pDelayTime, pDelayFeedback, pDelayTone, pDelayMix, pDelayPing, pDriveMode, pDrive, pDriveTone, pDriveMix,
             pVolume, pTempo, pClock };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.covegroove.bus";
        d.displayName = kit::moduleName (kPlugin, "Bus");
        d.description = "Cove Groove's busses: Top In through a subtle phaser and the moving band-pass (Motion sweeps it once "
                        "every Rate, as a sine, a ramp or a random walk), Send In into the tempo delay whose repeats join the "
                        "top bus, Dry In straight on - then the drive (Tube, Tape, Fuzz, Crush) and Volume. Tempo / Clock "
                        "should match the Sequencer's.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 409;
        d.sockets = { audioIn ("dryIn", "Dry In"), audioIn ("topIn", "Top In"), audioIn ("sendIn", "Send In"),
                      audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("phaser", "Phaser", 0.0f, 100.0f, 30.0f, 0, "%", false, 0.1f),
            makeRotary ("phaserRate", "Ph Rate", 0.03f, 4.0f, 0.2f, 0, "Hz", true),
            makeRotary ("filterFreq", "BP Freq", 100.0f, 10000.0f, 1800.0f, 1, "Hz", true),
            makeRotary ("filterReso", "BP Reso", 0.0f, 100.0f, 30.0f, 1, "%", false, 0.1f),
            makeRotary ("filterMotion", "Motion", 0.0f, 100.0f, 45.0f, 1, "%", false, 0.1f),
            makeRotary ("filterMix", "BP Mix", 0.0f, 100.0f, 20.0f, 1, "%", false, 0.1f),
            makeCombo ("filterRate", "Rate", { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" }, 2, 2, 2).noMod(),
            makeCombo ("filterShape", "Shape", { "Sine", "Ramp", "Random" }, 0, 2, 2).noMod(),
            makeCombo ("delayTime", "Delay", { "1/16", "1/16 dot", "1/8", "1/8 dot", "1/4", "1/4 dot" }, 3, 3, 2).noMod(),
            makeRotary ("delayFeedback", "Feedback", 0.0f, 90.0f, 40.0f, 3, "%", false, 0.1f),
            makeRotary ("delayTone", "Dly Tone", 500.0f, 16000.0f, 5000.0f, 3, "Hz", true),
            makeRotary ("delayMix", "Dly Mix", 0.0f, 100.0f, 22.0f, 4, "%", false, 0.1f),
            makeCombo ("delayPing", "Ping-Pong", { "Off", "On" }, 1, 4, 1).noMod(),
            makeCombo ("driveMode", "Drive", { "Tube", "Tape", "Fuzz", "Crush" }, 0, 5, 2).noMod(),
            makeRotary ("drive", "Amount", 0.0f, 100.0f, 22.0f, 5, "%", false, 0.1f),
            makeRotary ("driveTone", "Dr Tone", -100.0f, 100.0f, 0.0f, 5, {}, false, 0.1f),
            makeRotary ("driveMix", "Dr Mix", 0.0f, 100.0f, 100.0f, 6, "%", false, 0.1f),
            makeRotary ("volume", "Volume", 0.0f, 150.0f, 43.0f, 6, "%", false, 0.1f),
            makeRotary ("tempo", "Tempo", 70.0f, 190.0f, 138.0f, 4, "BPM", false, 0.1f),
            makeCombo ("clock", "Clock", { "Own Tempo", "Patch Tempo" }, 0, 6, 2).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); bus.prepare ((float) sr); reset(); }
    void reset() override { bus.reset(); clock = GrooveClock(); }
    void blockStart() override { clock.blockStart (hostPlayHead, hostTransport.isPlaying); }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (--ctl < 0)
        {
            ctl = 31;
            bus.phaser.amount = param (pPhaser) * 0.01f;
            bus.phaser.rateHz = param (pPhaserRate);
            bus.filter.freqHz = param (pFilterFreq);
            bus.filter.reso = param (pFilterReso) * 0.01f;
            bus.filter.motion = param (pFilterMotion) * 0.01f;
            bus.filter.mix = param (pFilterMix) * 0.01f;
            bus.filter.shape = (int) std::lround (getParameterBase (pFilterShape));
            bus.filterRateBars = kFilterBars[juce::jlimit (0, 4, (int) std::lround (getParameterBase (pFilterRate)))];
            bus.delay.steps = kDelaySteps[juce::jlimit (0, 5, (int) std::lround (getParameterBase (pDelayTime)))];
            bus.delay.feedback = param (pDelayFeedback) * 0.01f;
            bus.delay.toneHz = param (pDelayTone);
            bus.delay.mix = param (pDelayMix) * 0.01f;
            bus.delay.pingPong = getParameterBase (pDelayPing) >= 0.5f;
            bus.drive.mode = (int) std::lround (getParameterBase (pDriveMode));
            bus.drive.drive = param (pDrive) * 0.01f;
            bus.drive.tone = param (pDriveTone) * 0.01f;
            bus.drive.mix = param (pDriveMix) * 0.01f;
            bus.volume = param (pVolume) * 0.01f;
            bpm = juce::jlimit (20.0, 400.0, (double) (getParameterBase (pClock) >= 0.5f ? tempoBpm : param (pTempo)));
            clock.inc16 = bpm / 60.0 * 4.0 / sampleRate;
        }
        LaneOut o;
        o.dryL = in[0][0]; o.dryR = in[0][1];
        o.topL = in[1][0]; o.topR = in[1][1];
        o.sendL = in[2][0]; o.sendR = in[2][1];
        float l, r;
        bus.process (o, clock.pos(), bpm, l, r);
        out[0] = { l, r };
        clock.tick (true);
    }

private:
    Bus bus;
    GrooveClock clock;
    double bpm { 138.0 };
    int ctl { 0 };
};

using CoveGrooveKickModule      = CoveGrooveDrumModule<Kick>;
using CoveGrooveBassModule      = CoveGrooveDrumModule<Bass>;
using CoveGrooveClapModule      = CoveGrooveDrumModule<Clap>;
using CoveGrooveClosedHatModule = CoveGrooveDrumModule<ClosedHat>;
using CoveGrooveOpenHatModule   = CoveGrooveDrumModule<OpenHat>;
using CoveGrooveCymbalModule    = CoveGrooveDrumModule<Cymbal>;
using CoveGroovePercModule      = CoveGrooveDrumModule<Perc>;
using CoveGrooveFxDrumModule    = CoveGrooveDrumModule<Fx>;

AQUANODE_REGISTER_MODULE (CoveGrooveSequencerModule, CoveGrooveSequencerModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveKickModule,      CoveGrooveKickModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveBassModule,      CoveGrooveBassModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveClapModule,      CoveGrooveClapModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveClosedHatModule, CoveGrooveClosedHatModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveOpenHatModule,   CoveGrooveOpenHatModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveCymbalModule,    CoveGrooveCymbalModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGroovePercModule,      CoveGroovePercModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveFxDrumModule,    CoveGrooveFxDrumModule::descriptor)
AQUANODE_REGISTER_MODULE (CoveGrooveFxModule,        CoveGrooveFxModule::descriptor)

//==============================================================================
namespace aquanode::plugins::covegroove_patch
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        static const char* types[kNumLanes] = { "kick", "bass", "clap", "closedhat", "openhat", "cymbal", "perc", "fxhit" };

        const int seq = b.add ("plugin.covegroove.sequencer", 0, 0);
        b.copy (seq, original);
        const int fx = b.add ("plugin.covegroove.bus", 1590, 0);
        b.copy (fx, original);

        std::array<int, kNumLanes> drum {};
        for (int l = 0; l < kNumLanes; ++l)
        {
            drum[(size_t) l] = b.add (juce::String ("plugin.covegroove.") + types[l], 470 + (l % 4) * 270, (l / 4) * 410);   // (the card is as tall as its sample strip makes it)
            const juce::String prefix = "l" + juce::String (l);
            b.copy (drum[(size_t) l], original, [prefix] (const juce::String& id) { return prefix + id; });
            if (kit::param (original, prefix + "mute", 0.0f) >= 0.5f)
                b.set (drum[(size_t) l], "level", 0.0f);
            b.cable (seq, "gate" + juce::String (l), drum[(size_t) l], "trigIn");
            const bool top = kit::param (original, prefix + "top", 0.0f) >= 0.5f;
            b.cable (drum[(size_t) l], "audioOut", fx, top ? "topIn" : "dryIn");
            if (kit::param (original, prefix + "send", 0.0f) > 0.0f)
                b.cable (drum[(size_t) l], "sendOut", fx, "sendIn");
        }
        // the closed hat chokes the open hat
        b.cable (seq, "gate" + juce::String ((int) ClosedHat), drum[(size_t) OpenHat], "chokeIn");

        // the pattern and the sounds the generator plans for
        if (auto* s = dynamic_cast<CoveGrooveSequencerModule*> (b.dsp (seq)))
        {
            for (int l = 0; l < kNumLanes; ++l)
                s->models[(size_t) l] = (int) kit::param (original, "l" + juce::String (l) + "model", 0.0f);
            juce::MemoryBlock mb;
            original.getStateInformation (mb);
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
            {
                Pattern p;
                if (p.fromString (xml->getStringAttribute ("pattern").toStdString()))
                    s->setPattern (p);

                // the lanes' own samples and curves, into their drums
                if (auto* smp = xml->getChildByName ("Samples"))
                    for (auto* lt : smp->getChildWithTagNameIterator ("Lane"))
                    {
                        const int l = lt->getIntAttribute ("index", -1);
                        if (l < 0 || l >= kNumLanes) continue;
                        if (auto* d = dynamic_cast<CoveGrooveDrumBase*> (b.dsp (drum[(size_t) l])))
                        {
                            Curve c;
                            if (c.fromString (lt->getStringAttribute ("curve").toStdString())) d->setCurve (c);
                            if (auto data = samples::decode (lt->getStringAttribute ("audio")))
                                d->setSample (std::move (data), lt->getStringAttribute ("name"));
                        }
                    }
            }
        }

        PatchPorts ports;
        ports.name = "Cove Groove";
        ports.audioOut = { { fx, "audioOut" } };
        for (int l = 0; l < kNumLanes; ++l)
            ports.midiIn.push_back ({ drum[(size_t) l], "midiIn" });
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        if (auto xml = juce::XmlDocument::parse (file))
        {
            juce::MemoryBlock mb;
            juce::AudioProcessor::copyXmlToBinary (*xml, mb);
            original.setStateInformation (mb.getData(), (int) mb.getSize());
            return true;
        }
        return false;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
