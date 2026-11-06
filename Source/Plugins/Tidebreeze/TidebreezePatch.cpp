//==============================================================================
// Tidebreeze - Patch version
//
// A Tidebreeze preset is one or two layers, each a wave (High or Low by
// velocity) through its own filter and amp, with a filter envelope, an amp
// envelope and two LFOs; a pitch envelope common to both; glide, mono /
// legato and hold; the arpeggio in front; three insert effects in series,
// two send effects beside them, the master EQ and volume behind. The patch
// version is that, module by module:
//
//   [Tidebreeze - Arp] --midi--> Wave A / Wave B
//
//   Pitch Env --+--> Wave A --> Filter A --> Amp A --+
//   LFO 1 / 2 --+--------------^ ------------^       |
//   Filter Env -----------------^                    +--> Insert 1 > 2 > 3 --+--> Output
//   Amp Env ----------------------------------^      |                       +--> Send 1 --^
//               (the same for layer B) ------------->+                       +--> Send 2 --^
//
// Every module runs Tidebreeze's own code: the wave reader with its mip
// levels and loops, the four-level envelopes, the LFO shapes, the resonant
// filter, the effects and the arpeggiator, on Tidebreeze's control clock (16
// samples, counted from each note-on). The modulation inputs come with the
// plugin's own depth knobs (Env Depth, LFO 1 to Cutoff ...), so the numbers
// mean what they mean on the card, and every knob takes cables on top.
//
// The waves are the card's: each Wave module has the 16 user slots of its
// own (the ones its layer plays come over from the preset), saved with the
// patch - rolled waves as their recipe, loaded files as their audio. Load
// Wave and Roll Wave on the module put a new one into its High wave's slot.
//==============================================================================
#include "PluginTidebreezeConfig.h"

namespace aqp_tidebreeze
{
    #include AQP_TIDEBREEZE_SRC(PluginProcessor.h)
    #include AQP_TIDEBREEZE_SRC(PluginEditor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "tidebreeze" };
    using TB = aqp_tidebreeze::TidebreezeAudioProcessor;
    namespace tb = aqp_tidebreeze::tb;
    namespace tbui = aqp_tidebreeze::tbui;

    constexpr int kCtl = 16;                  // Tidebreeze's control block (samples)
    constexpr float kSemisPerUnit = 60.0f;    // pitch cables: semitones / 60

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.tidebreeze." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    juce::StringArray offOn() { return { "Off", "On" }; }
    // a knob in whole steps, as the plugin's integer parameters
    ParamSpec steps (const char* id, const char* label, int lo, int hi, int def, int row, const char* unit = "")
    {
        return makeRotary (id, label, (float) lo, (float) hi, (float) def, row, unit, false, 1.0f);
    }
    juce::StringArray waveChoices()
    {
        juce::StringArray a;
        for (const auto& w : tb::factoryWaves()) a.add (w.name);
        for (int s = 0; s < tb::kUserWaveSlots; ++s) a.add ("User " + juce::String (s + 1));
        return a;
    }
    juce::StringArray voiceChoices() { juce::StringArray a; for (int v = 1; v <= kMaxVoices; ++v) a.add (juce::String (v)); return a; }
    juce::StringArray archetypeChoices() { juce::StringArray a; for (int i = 0; i < tb::NumArchetypes; ++i) a.add (tb::archetypeName (i)); return a; }

    inline float ampCurve (float level01) { const float l = juce::jlimit (0.0f, 1.0f, level01); return l * l; }

    // a free-running LFO's phase at a note-on (the card's run on its own clock)
    float freePhase (float hz)
    {
        const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double p = t * (double) hz;
        return (float) (p - std::floor (p));
    }
}

//==============================================================================
// Tidebreeze - Wave
//==============================================================================
class TidebreezeWaveModule final : public SynthModule, private juce::AsyncUpdater
{
public:
    enum P { pHiWave, pHiLvl, pVelSw, pLoWave, pLoLvl, pVelLo, pHiRev, pHiOfs, pLoRev, pLoOfs, pDelay,
             pOctave, pTranspose, pTune, pSlope, pVelHi, pPegInt, pL1Pitch, pL2Pitch, pBend, pPorta,
             pVoices, pLegato, pRollType, pLoad, pRoll };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("wave", "Wave", ModuleSection::Oscillator, 0);
        d.description = "A Tidebreeze layer's waves, played the way the card plays them: the High wave from the "
                        "velocity split up, the Low wave below it, each reversed or started at its loop on request, "
                        "read band-limited at any pitch. Pitch Env and LFO 1 / 2 bend it by their depth knobs, Pitch In "
                        "by semitones / 60. Load Wave puts an audio file into the High wave's user slot (or the next "
                        "free one), Roll Wave a new wave of the Roll type; both play on High and Low. The user waves "
                        "are saved with the patch.";
        d.sockets = { midiIn ("midiIn", "Midi In"), modIn ("pitchIn", "Pitch In"), modIn ("pegIn", "Pitch Env"),
                      modIn ("lfo1In", "LFO 1"), modIn ("lfo2In", "LFO 2"), audioOut ("audioOut", "Audio Out") };
        const int saw = tb::factoryWaveIndex ("Saw");
        d.params = {
            makeCombo ("hiWave", "High Wave", waveChoices(), saw, 0, 3).noMod(),
            steps ("hiLvl", "High Lvl", 0, 127, 127, 0),
            steps ("velSw", "Vel Split", 1, 127, 1, 0),
            makeCombo ("loWave", "Low Wave", waveChoices(), saw, 1, 3).noMod(),
            steps ("loLvl", "Low Lvl", 0, 127, 127, 1),
            steps ("velLo", "Vel Low", 1, 127, 1, 1),

            makeCombo ("hiRev", "Hi Reverse", offOn(), 0, 2, 1),
            makeCombo ("hiOfs", "Hi Skip Atk", offOn(), 0, 2, 1),
            makeCombo ("loRev", "Lo Reverse", offOn(), 0, 2, 1),
            makeCombo ("loOfs", "Lo Skip Atk", offOn(), 0, 2, 1),
            steps ("delay", "Delay", 0, 5000, 0, 2, "ms"),

            makeSteppedList ("octave", "Octave", tb::octaveNames(), 2, 3),
            steps ("transpose", "Transpose", -12, 12, 0, 3, "st"),
            steps ("tune", "Tune", -1200, 1200, 0, 3, "ct"),
            steps ("pSlope", "Key Follow", -10, 20, 10, 3),
            steps ("velHi", "Vel High", 1, 127, 127, 3),

            steps ("pegInt", "Env Depth", -12, 12, 0, 4, "st"),
            steps ("l1Pitch", "LFO1 Pitch", -120, 120, 0, 4),
            steps ("l2Pitch", "LFO2 Pitch", -120, 120, 0, 4),
            steps ("bend", "Bend", 0, 12, 2, 4, "st"),
            steps ("porta", "Glide", 0, 99, 0, 4),

            makeSteppedList ("voices", "Voices", voiceChoices(), kMaxVoices - 1, 5).noMod(),
            makeCombo ("legato", "Legato", offOn(), 0, 5, 1).noMod(),
            makeCombo ("rollType", "Roll Type", archetypeChoices(), 0, 5, 3).noMod(),
            makeButton ("load", "Load Wave", 6, 2),
            makeButton ("roll", "Roll Wave", 6, 2)
        };
        // Key Follow in the card's terms: +1.0 is normal, 0 the same pitch on every key
        d.params[(size_t) pSlope].textFromValue = [] (double v) { return (v >= 0 ? "+" : "") + juce::String (v / 10.0, 1); };
        return d;
    }

    ~TidebreezeWaveModule() override { cancelPendingUpdate(); }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    double voiceTailSeconds() const override { return 0.05; }

    tb::WaveBank& waves() { return bank; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
        ensureWaves();
    }
    void reset() override
    {
        for (auto& s : vs) s = {};
        pool.resetAll();
        tracker.reset();
        liveValid = false;
    }

    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        if (id == "hiWave" || id == "loWave")
        {
            if (juce::MessageManager::existsAndIsCurrentThread()) ensureWaves();
            else triggerAsyncUpdate();
        }
    }

    // the waves this module plays are rendered; the others let go
    void ensureWaves()
    {
        std::vector<bool> used ((size_t) tb::numWaveChoices(), false);
        for (int p : { (int) pHiWave, (int) pLoWave })
        {
            const int w = (int) std::lround (getParameterBase (p));
            if (w >= 0 && w < tb::numWaveChoices()) used[(size_t) w] = true;
        }
        bank.ensure (used);
    }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& s = vs[(size_t) v];
        const bool mono = monoMode();
        const int from = tracker.noteOn (v, mono && param (pLegato) >= 0.5f);
        pool.noteOn (v, voiceCount());
        latest = v;
        if (from >= 0)
        {
            // legato: the line goes on - only the pitch moves
            s = vs[(size_t) from];
            s.note = note;
            s.glideTarget = (float) note;
            if (param (pPorta) < 0.5f) s.glide = (float) note;
            return;
        }
        s = {};
        s.note = note;
        s.glideTarget = (float) note;
        s.glide = mono && param (pPorta) >= 0.5f && liveValid ? liveGlide : (float) note;
        s.pending = true;
    }
    void voiceVelocity (int v, float velocity01) override
    {
        vs[(size_t) v].velocity = velocity01;
        start (v);
    }
    void voiceNoteOff (int v) override
    {
        tracker.noteOff();
        pool.noteOff (v, voiceCount());
        auto& s = vs[(size_t) v];
        if (s.delay > 0) s.active = false;   // let go before it started
    }
    void voiceReset (int v) override
    {
        vs[(size_t) v] = {};
        pool.resetVoice (v);
        if (v == latest) liveValid = false;
    }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& s = vs[(size_t) v];
        out[0] = { 0.0f, 0.0f };
        if (s.pending) start (v);
        if (! s.active) return;
        if (s.delay > 0) { --s.delay; return; }
        if (s.k == 0)
        {
            // one control block: glide, then the pitch (the card's renderVoices)
            const float porta = param (pPorta);
            const float glideK = porta >= 0.5f ? glideMemo (porta, [this] (float p)
                                 { return 1.0f - std::exp (-1.0f / (tb::egTime (p) * 0.35f * (float) sampleRate) * (float) kCtl); }) : 1.0f;
            s.glide += (s.glideTarget - s.glide) * glideK;
            if (v == latest) { liveGlide = s.glide; liveValid = true; }
            const float bend = midiControls != nullptr ? midiControls->bend * param (pBend) : 0.0f;
            const float semis = 60.0f + (s.glide - 60.0f) * param (pSlope) * 0.1f
                              + 12.0f * (std::round (param (pOctave)) - 2.0f) + param (pTranspose) + param (pTune) * 0.01f
                              + bend + param (pPegInt) * in[2][0] + 0.1f * (param (pL1Pitch) * in[3][0] + param (pL2Pitch) * in[4][0])
                              + in[1][0] * kSemisPerUnit;
            s.inc = (double) tb::midiToHz (semis) / (double) s.reader.rootHz() * s.reader.rate() / sampleRate;
        }
        s.k = (s.k + 1) % kCtl;
        const float x = s.reader.next (s.inc) * s.level * pool.nextGain (v, sampleRate);
        if (s.reader.done) s.active = false;
        out[0] = { x, x };
    }

    //=== the waves' buttons ===================================================
    // the slot a new wave goes into: the High wave's, if that is a user slot
    int targetSlot()
    {
        const int hi = (int) std::lround (getParameterBase (pHiWave));
        return tb::WaveBank::isUser (hi) ? tb::WaveBank::slotOf (hi) : bank.nextSlot();
    }
    void playWave (int choice)
    {
        if (choice < 0) return;
        setParameter ("hiWave", (float) choice);
        setParameter ("loWave", (float) choice);
        if (onParamsChanged) onParamsChanged();
    }
    void uiButtonClicked (const juce::String& id) override
    {
        if (id == "roll")
        {
            playWave (bank.rollWave ((int) std::lround (getParameterBase (pRollType)), 0, targetSlot()));
        }
        else if (id == "load")
        {
            // on Android the picked document is copied into the app's storage
            // first (the plugins' file chooser, PluginPrelude.h); the wave then
            // keeps the audio itself
            chooser = std::make_unique<juce::FileChooser> ("Load a wave (audio file)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            juce::WeakReference<SynthModule> weak (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [weak] (const juce::FileChooser& fc)
                {
                    auto* self = dynamic_cast<TidebreezeWaveModule*> (weak.get());
                    if (self == nullptr || ! fc.getResult().existsAsFile()) return;
                    juce::String error;
                    const int choice = self->bank.loadFile (fc.getResult(), self->targetSlot(), error);
                    if (choice < 0)
                        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tidebreeze - Wave",
                                                                     "The wave could not be loaded.\n\n" + error);
                    self->playWave (choice);
                });
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 112; }
    std::function<void()> onParamsChanged;   // the card's knobs follow a wave chosen on the card

    //=== saving: the user waves this module plays =============================
    juce::String saveCustomState() const override
    {
        juce::XmlElement xml ("TidebreezeWave");
        const int hi = (int) std::lround (getParameterBase (pHiWave)), lo = (int) std::lround (getParameterBase (pLoWave));
        bank.writeXml (xml, [hi, lo] (int slot) { return tb::WaveBank::choiceOf (slot) == hi || tb::WaveBank::choiceOf (slot) == lo; });
        return xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    }
    void loadCustomState (const juce::String& state) override
    {
        if (auto xml = juce::parseXML (state); xml != nullptr && xml->hasTagName ("TidebreezeWave"))
        {
            bank.readXml (xml->getChildByName ("UserWaves"));
            ensureWaves();
        }
    }

private:
    bool monoMode() const { return (int) std::lround (param (pVoices)) == 0; }
    int voiceCount() const { return (int) std::lround (param (pVoices)) + 1; }

    void handleAsyncUpdate() override { ensureWaves(); }

    void start (int v)
    {
        auto& s = vs[(size_t) v];
        if (! s.pending) return;
        s.pending = false;
        s.active = false;
        const int vel127 = juce::jlimit (1, 127, (int) std::lround (s.velocity * 127.0f));
        if (vel127 < (int) param (pVelLo) || vel127 > (int) param (pVelHi)) return;
        const bool high = vel127 >= (int) param (pVelSw);
        const int choice = (int) std::lround (getParameterBase (high ? pHiWave : pLoWave));
        auto w = bank.tryWave (choice);
        if (w == nullptr) { triggerAsyncUpdate(); return; }   // not rendered yet: ask, and stay silent (as the card)
        // the zone follows where the layer really plays, tune included
        const float played = 60.0f + ((float) s.note - 60.0f) * param (pSlope) * 0.1f
                           + 12.0f * (std::round (param (pOctave)) - 2.0f) + param (pTranspose) + param (pTune) * 0.01f;
        s.reader.start (std::move (w), played, param (high ? pHiOfs : pLoOfs) >= 0.5f, param (high ? pHiRev : pLoRev) >= 0.5f);
        if (s.reader.done) return;
        s.level = param (high ? pHiLvl : pLoLvl) / 127.0f;
        s.delay = (int) (param (pDelay) * 0.001 * sampleRate);
        s.k = 0;
        s.active = true;
    }

    struct VoiceState
    {
        tb::WaveReader reader;
        double inc = 1.0;
        int note = 60, delay = 0, k = 0;
        float velocity = 0.8f, level = 1.0f, glide = 60.0f, glideTarget = 60.0f;
        bool pending = false, active = false;
    };
    std::array<VoiceState, kMaxVoices> vs {};
    tb::WaveBank bank;
    ModuleVoicePool pool;
    kit::LegatoTracker tracker;
    Memo glideMemo;
    int latest = -1;
    float liveGlide = 60.0f;
    bool liveValid = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

namespace
{
    // the Wave card: the High wave in Tidebreeze's phosphor, and for a loaded
    // file its root key and loop
    class WaveCard final : public juce::Component, public ExtraContentParamListener, private juce::Timer
    {
    public:
        explicit WaveCard (TidebreezeWaveModule& m) : module (&m)
        {
            setLookAndFeel (&laf);
            for (int n = 0; n < 128; ++n) root.addItem (midiNoteName (n), n + 1);
            loop.addItemList (tb::loopModeNames(), 1);
            for (auto* sl : { &start, &xfade })
            {
                sl->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
                sl->setMouseDragSensitivity (180);
                sl->onValueChange = [this] { apply(); };
                addChildComponent (*sl);
            }
            start.setRange (2.0, 95.0, 0.1);
            xfade.setRange (0.0, 500.0, 1.0);
            xfade.setSkewFactorFromMidPoint (80.0);
            for (auto* c : { &root, &loop }) { c->onChange = [this] { apply(); }; addChildComponent (*c); }
            m.onParamsChanged = [this] { if (paramsChangedByContent) paramsChangedByContent(); refresh(); };
            refresh();
            startTimerHz (8);
        }
        ~WaveCard() override
        {
            if (auto* m = mod()) m->onParamsChanged = nullptr;
            setLookAndFeel (nullptr);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (4, 2);
            auto row = r.removeFromBottom (40);
            root.setBounds (row.removeFromLeft (70).withSizeKeepingCentre (70, 22).translated (0, 6));
            row.removeFromLeft (4);
            loop.setBounds (row.removeFromLeft (84).withSizeKeepingCentre (84, 22).translated (0, 6));
            row.removeFromLeft (4);
            start.setBounds (row.removeFromLeft (52).withTrimmedTop (10));
            xfade.setBounds (row.removeFromLeft (52).withTrimmedTop (10));
            scope = r.reduced (0, 2);
        }

        void paint (juce::Graphics& g) override
        {
            using Pal = tbui::Palette;
            auto* m = mod();
            g.setColour (Pal::glassEdge());
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            auto sc = scope.toFloat();
            g.setColour (Pal::inset());
            g.fillRoundedRectangle (sc, 4.0f);
            if (m == nullptr) return;
            const int hi = (int) std::lround (m->getParameter ("hiWave")), lo = (int) std::lround (m->getParameter ("loWave"));
            auto& bank = m->waves();
            const auto wave = bank.peekWave (hi);
            if (wave != nullptr && ! wave->zones.empty())
            {
                const auto& z = wave->zoneFor (60.0f);
                const int cols = std::max (1, (int) sc.getWidth() - 4);
                if (cachedZone != (const void*) &z || cachedCols != cols)
                {
                    size_t level = 0;
                    while (level + 1 < z.levels.size() && (int) z.levels[level + 1].size() >= cols * 8) ++level;
                    const auto& buf = z.levels[level];
                    const int n = (int) buf.size();
                    peak = 1.0e-6f; for (auto v : buf) peak = std::max (peak, std::abs (v));
                    columns.assign ((size_t) cols, { 0.0f, 0.0f });
                    for (int x = 0; x < cols; ++x)
                    {
                        const int a = (int) ((double) x / cols * n), b = std::max (a + 1, (int) ((double) (x + 1) / cols * n));
                        float l = 0, h = 0;
                        for (int i = a; i < b && i < n; ++i) { l = std::min (l, buf[(size_t) i]); h = std::max (h, buf[(size_t) i]); }
                        columns[(size_t) x] = { h, l };
                    }
                    cachedZone = (const void*) &z; cachedCols = cols; held = wave;
                }
                if (! wave->oneShot)
                {
                    const float x0 = sc.getX() + 2.0f + (float) cols * (float) z.loopStart / (float) std::max (1, z.length());
                    g.setColour (Pal::phMid().withAlpha (0.08f));
                    g.fillRect (juce::Rectangle<float> (x0, sc.getY() + 1.0f, sc.getRight() - 1.0f - x0, sc.getHeight() - 2.0f));
                }
                const float h2 = sc.getHeight() * 0.38f, cy = sc.getCentreY() + 4.0f;
                g.setColour (Pal::phMid().withAlpha (0.7f));
                for (int x = 0; x < cols; ++x)
                    g.drawVerticalLine ((int) sc.getX() + 2 + x, cy - columns[(size_t) x].first / peak * h2, cy - columns[(size_t) x].second / peak * h2 + 1.0f);
            }
            g.setFont (tbui::LookAndFeel::crt (10.0f, true));
            auto label = bank.waveName (hi).toUpperCase();
            if (lo != hi) label << "  /  LOW " << bank.waveName (lo).toUpperCase();
            if (wave == nullptr && ! (tb::WaveBank::isUser (hi) && ! bank.slot (tb::WaveBank::slotOf (hi)).used())) label << "   RENDERING_";
            tbui::glowText (g, label, sc.reduced (6.0f, 3.0f), juce::Justification::topLeft, Pal::phBright());
            if (showsSample)
            {
                g.setFont (tbui::LookAndFeel::crt (9.0f, true));
                g.setColour (Pal::phDim());
                const int y = getHeight() - 42;
                g.drawText ("ROOT", root.getX(), y, root.getWidth(), 12, juce::Justification::centredLeft);
                g.drawText ("LOOP", loop.getX(), y, loop.getWidth(), 12, juce::Justification::centredLeft);
                g.drawText ("START", start.getX(), y, start.getWidth(), 12, juce::Justification::centred);
                g.drawText ("X-FADE", xfade.getX(), y, xfade.getWidth(), 12, juce::Justification::centred);
                g.setColour (Pal::phBright());
                const int x = xfade.getRight() + 6;
                g.drawFittedText (juce::String (start.getValue(), 1) + " %\n" + juce::String ((int) xfade.getValue()) + " ms",
                                  x, getHeight() - 34, getWidth() - x - 4, 30, juce::Justification::centredLeft, 2);
            }
            else
            {
                g.setFont (tbui::LookAndFeel::crt (9.0f, true));
                g.setColour (Pal::phDim());
                g.drawFittedText ("LOAD WAVE: AN AUDIO FILE INTO THE HIGH WAVE'S USER SLOT (OR THE NEXT FREE ONE). ROLL WAVE: A NEW WAVE OF THE ROLL TYPE.",
                                  getLocalBounds().reduced (6, 2).removeFromBottom (36), juce::Justification::centredLeft, 3);
            }
        }

    private:
        TidebreezeWaveModule* mod() const { return dynamic_cast<TidebreezeWaveModule*> (module.get()); }

        void timerCallback() override
        {
            auto* m = mod();
            if (m == nullptr) return;
            const int hi = (int) std::lround (m->getParameter ("hiWave"));
            const auto version = m->waves().slotsVersion();
            if (hi != shownHi || version != shownVersion) refresh();
            if (m->waves().changed.exchange (false) || version != shownVersion) repaint();
        }

        // the controls follow the High wave's slot
        void refresh()
        {
            auto* m = mod();
            if (m == nullptr) return;
            shownHi = (int) std::lround (m->getParameter ("hiWave"));
            shownVersion = m->waves().slotsVersion();
            const bool user = tb::WaveBank::isUser (shownHi);
            const auto& s = m->waves().slot (user ? tb::WaveBank::slotOf (shownHi) : 0);
            showsSample = user && s.kind == tb::WaveBank::Slot::Sample;
            for (juce::Component* c : { (juce::Component*) &root, (juce::Component*) &loop, (juce::Component*) &start, (juce::Component*) &xfade })
                c->setVisible (showsSample);
            if (showsSample)
            {
                const juce::ScopedValueSetter<bool> quiet (updating, true);
                root.setSelectedId (s.settings.rootNote + 1, juce::dontSendNotification);
                loop.setSelectedId (s.settings.loopMode + 1, juce::dontSendNotification);
                start.setValue (s.settings.loopStart * 100.0, juce::dontSendNotification);
                xfade.setValue (s.settings.xfadeMs, juce::dontSendNotification);
                start.setEnabled (s.settings.loopMode == tb::LoopSustain);
                xfade.setEnabled (start.isEnabled());
            }
            repaint();
        }

        void apply()
        {
            auto* m = mod();
            if (m == nullptr || updating || ! showsSample) return;
            tb::SampleSettings st;
            st.rootNote = juce::jlimit (0, 127, root.getSelectedId() - 1);
            st.loopMode = juce::jlimit (0, tb::NumLoopModes - 1, loop.getSelectedId() - 1);
            st.loopStart = (float) start.getValue() * 0.01f;
            st.xfadeMs = (float) xfade.getValue();
            m->waves().setSampleSettings (tb::WaveBank::slotOf (shownHi), st);
            start.setEnabled (st.loopMode == tb::LoopSustain);
            xfade.setEnabled (start.isEnabled());
            shownVersion = m->waves().slotsVersion();
            repaint();
        }

        juce::WeakReference<SynthModule> module;
        tbui::LookAndFeel laf;
        juce::ComboBox root, loop;
        juce::Slider start { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                     xfade { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Rectangle<int> scope;
        int shownHi = -1;
        juce::uint32 shownVersion = 0;
        bool showsSample = false, updating = false;
        const void* cachedZone = nullptr; int cachedCols = 0; float peak = 1.0f;
        std::vector<std::pair<float, float>> columns;
        tb::WavePtr held;
    };
}

std::unique_ptr<juce::Component> TidebreezeWaveModule::createExtraContentComponent()
{
    return std::make_unique<WaveCard> (*this);
}

//==============================================================================
// Tidebreeze - Envelope (the filter, amp and pitch envelopes)
//==============================================================================
class TidebreezeEnvModule final : public SynthModule
{
public:
    enum P { pKind, pStart, pAttack, pBreak, pSustain, pRelease, pAttackT, pDecayT, pSlopeT, pReleaseT, pDelay, pHold, pLegato };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("env", "Envelope", ModuleSection::Utility, 1);
        d.description = "Tidebreeze's four-level envelope: from Start it rises to Peak over Attack, falls to Knee over "
                        "Decay, settles to Sustain over Settle and goes to End over Release once the key is let go. "
                        "Levels are -99..99 (Env Out -1..1). As the card's amp envelope (Kind Amp) its End is 0 and it "
                        "decides how long a released note sounds; Hold keeps notes from releasing. Delay starts it late "
                        "(the layer's delay), Legato carries a held line's envelope on.";
        d.sockets = { modOut ("envOut", "Env Out") };
        d.params = {
            makeCombo ("kind", "Kind", { "Filter", "Amp", "Pitch" }, 0, 0, 2).noMod(),
            steps ("start", "Start", -99, 99, 0, 1),
            steps ("attack", "Peak", -99, 99, 99, 1),
            steps ("break", "Knee", -99, 99, 99, 1),
            steps ("sustain", "Sustain", -99, 99, 99, 1),
            steps ("release", "End", -99, 99, 0, 1),
            steps ("attackT", "Attack", 0, 99, 0, 2),
            steps ("decayT", "Decay", 0, 99, 40, 2),
            steps ("slopeT", "Settle", 0, 99, 40, 2),
            steps ("releaseT", "Release", 0, 99, 30, 2),
            steps ("delay", "Delay", 0, 5000, 0, 0, "ms"),
            makeCombo ("hold", "Hold", offOn(), 0, 0, 1).noMod(),
            makeCombo ("legato", "Legato", offOn(), 0, 0, 1).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& s : vs) s = {}; tracker.reset(); }
    void voiceReset (int v) override { vs[(size_t) v] = {}; }

    bool isAmp() const { return (int) std::lround (getParameterBase (pKind)) == 1; }
    double voiceTailSeconds() const override
    {
        // only the amp envelope says how long a released voice sounds
        return isAmp() ? (double) tb::egTime (getParameterBase (pReleaseT)) * 1.05 + 0.05 : 0.0;
    }
    bool canHoldVoicesOpen() const override { return true; }
    bool voiceHeldOpen (int v) const override
    {
        return isAmp() && getParameterBase (pHold) >= 0.5f && vs[(size_t) v].on && ! vs[(size_t) v].env.finished();
    }

    void voiceNoteOn (int v, int, bool) override
    {
        auto& s = vs[(size_t) v];
        const int from = tracker.noteOn (v, param (pLegato) >= 0.5f);
        if (from >= 0) { s = vs[(size_t) from]; s.on = true; return; }
        s = {};
        s.on = true;
        configure (s.env);
        s.delay = (int) (param (pDelay) * 0.001 * sampleRate);
        s.env.stage = tb::Envelope::Idle;
        s.env.value = s.env.startL;
        if (s.delay == 0) s.env.trigger();
        s.out = s.env.value / 99.0f;
    }
    void voiceNoteOff (int v) override
    {
        tracker.noteOff();
        if (param (pHold) >= 0.5f) return;   // Hold: the note stays as it is
        auto& s = vs[(size_t) v];
        if (s.delay > 0) { s.delay = -1; return; }   // let go before it started: it never does
        s.env.release();
    }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        auto& s = vs[(size_t) v];
        if (s.delay != 0)
        {
            if (s.delay > 0 && --s.delay == 0) { configure (s.env); s.env.trigger(); s.k = 0; }
            out[0] = { s.out, 0.0f };
            if (s.delay != 0) return;
        }
        if (s.k == 0)
        {
            configure (s.env);   // the knobs (and their cables) now
            s.out = s.env.advance ((float) kCtl / (float) sampleRate);
        }
        s.k = (s.k + 1) % kCtl;
        out[0] = { s.out, 0.0f };
    }

private:
    void configure (tb::Envelope& e) const
    {
        e.startL = param (pStart); e.attackL = param (pAttack); e.breakL = param (pBreak);
        e.sustainL = param (pSustain); e.releaseL = param (pRelease);
        e.attackT = tb::egTime (param (pAttackT)); e.decayT = tb::egTime (param (pDecayT));
        e.slopeT = tb::egTime (param (pSlopeT)); e.releaseT = tb::egTime (param (pReleaseT));
    }

    struct VoiceState { tb::Envelope env; int k = 0, delay = 0; float out = 0.0f; bool on = false; };
    std::array<VoiceState, kMaxVoices> vs {};
    kit::LegatoTracker tracker;
};

//==============================================================================
// Tidebreeze - LFO
//==============================================================================
class TidebreezeLfoModule final : public SynthModule
{
public:
    enum P { pWave, pFreq, pSync, pDiv, pKey, pFade, pDelay, pOfs, pLegato };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("lfo", "LFO", ModuleSection::Utility, 2);
        d.description = "Tidebreeze's LFO: 18 shapes from triangle to stairs and drifting randoms, a speed or a tempo "
                        "division, Fade In and Delay after the note-on, Offset. Restart starts every note at the same "
                        "phase; off, it runs freely. LFO Out is -1..1, for the depth knobs of the Wave, Filter and Amp.";
        d.sockets = { modOut ("lfoOut", "LFO Out") };
        d.params = {
            makeCombo ("wave", "Shape", tb::lfoShapeNames(), 0, 0, 3).noMod(),
            steps ("freq", "Speed", 0, 99, 50, 0),
            makeCombo ("key", "Restart", offOn(), 1, 0, 1).noMod(),
            makeCombo ("sync", "Sync", offOn(), 0, 1, 1).noMod(),
            makeCombo ("div", "Division", tb::lfoDivNames(), 6, 1, 2).noMod(),
            steps ("fade", "Fade In", 0, 99, 0, 1),
            steps ("delay", "Delay", 0, 99, 0, 1),
            steps ("ofs", "Offset", -99, 99, 0, 2),
            makeCombo ("legato", "Legato", offOn(), 0, 2, 1).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& s : vs) s = {}; tracker.reset(); }

    void voiceNoteOn (int v, int, bool) override
    {
        auto& s = vs[(size_t) v];
        const int from = tracker.noteOn (v, param (idx (pLegato)) >= 0.5f);
        if (from >= 0) { s = vs[(size_t) from]; return; }
        s = {};
        configure (s.lfo);
        s.lfo.seed = (juce::uint32) (++counter * 2654435761u + (juce::uint32) v * 40503u);
        s.lfo.trigger (s.lfo.keySync ? 0.0f : freePhase (s.lfo.hz));
    }
    void voiceNoteOff (int) override { tracker.noteOff(); }

    void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override
    {
        auto& s = vs[(size_t) v];
        if (s.k == 0)
        {
            configure (s.lfo);
            s.out = s.lfo.advance ((float) kCtl / (float) sampleRate);
        }
        s.k = (s.k + 1) % kCtl;
        out[0] = { s.out, 0.0f };
    }

private:
    // the descriptor's order differs from the enum (Restart sits in the first row)
    static int idx (P p)
    {
        static const int order[] = { 0, 1, 3, 4, 2, 5, 6, 7, 8 };   // wave freq sync div key fade delay ofs legato
        return order[(int) p];
    }
    void configure (tb::Lfo& l) const
    {
        l.shape = (int) std::lround (param (idx (pWave)));
        l.hz = param (idx (pSync)) >= 0.5f ? (float) (tempoBpm / 60.0) / tb::divBeats ((int) std::lround (param (idx (pDiv))))
                                           : tb::lfoHz (param (idx (pFreq)));
        l.keySync = param (idx (pKey)) >= 0.5f;
        l.fadeSec = tb::egTime (param (idx (pFade)));
        l.delaySec = tb::egTime (param (idx (pDelay)));
        l.offset = param (idx (pOfs)) / 99.0f;
    }

    struct VoiceState { tb::Lfo lfo; int k = 0; float out = 0.0f; };
    std::array<VoiceState, kMaxVoices> vs {};
    kit::LegatoTracker tracker;
    juce::uint32 counter = 0;
};

//==============================================================================
// Tidebreeze - Filter
//==============================================================================
class TidebreezeFilterModule final : public SynthModule
{
public:
    enum P { pType, pCut, pRes, pHp, pTrim, pKt, pEgA, pEgB, pVelA, pL1A, pL2A, pLegato };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("filter", "Filter", ModuleSection::Filter, 3);
        d.description = "A Tidebreeze layer's filter: a 24 dB resonant low-pass, or a 12 dB low-pass and high-pass in "
                        "series. Cutoff and High-Pass are 0..99 as on the card (30 Hz to 20 kHz); Env In moves them by "
                        "Env Depth / HP Depth, LFO 1 / 2 the cutoff by their depths, velocity and the key by Velocity and "
                        "Keytrack. Input sets the level into the filter.";
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("envIn", "Env In"), modIn ("lfo1In", "LFO 1"), modIn ("lfo2In", "LFO 2"),
                      audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeCombo ("fType", "Type", { "Resonant Low-Pass", "Low-Pass + High-Pass" }, 0, 0, 3).noMod(),
            makeRotary ("fA", "Cutoff", 0.0f, 104.0f, 99.0f, 0),
            steps ("fRes", "Resonance", 0, 99, 0, 0),
            steps ("fB", "High-Pass", 0, 99, 0, 1),
            steps ("fTrim", "Input", 0, 99, 90, 1),
            steps ("fKt", "Keytrack", -99, 99, 0, 1),
            steps ("fEgA", "Env Depth", -99, 99, 0, 1),
            steps ("fEgB", "HP Depth", -99, 99, 0, 1),
            steps ("fVelA", "Velocity", -99, 99, 0, 2),
            steps ("fL1A", "LFO1 Cut", -99, 99, 0, 2),
            steps ("fL2A", "LFO2 Cut", -99, 99, 0, 2),
            makeCombo ("legato", "Legato", offOn(), 0, 2, 1).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& s : vs) s = {}; tracker.reset(); }
    void voiceReset (int v) override { vs[(size_t) v].fl.reset(); vs[(size_t) v].fr.reset(); }

    void voiceNoteOn (int v, int note, bool) override
    {
        auto& s = vs[(size_t) v];
        const int from = tracker.noteOn (v, param (pLegato) >= 0.5f);
        if (from >= 0) { s = vs[(size_t) from]; s.note = note; s.legato = true; return; }
        s = {};
        s.note = note;
    }
    // a legato note keeps the velocity the line started with (as the card does)
    void voiceVelocity (int v, float velocity01) override { if (! vs[(size_t) v].legato) vs[(size_t) v].velocity = velocity01; }
    void voiceNoteOff (int) override { tracker.noteOff(); }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& s = vs[(size_t) v];
        if (s.k == 0)
        {
            // the cutoff of one control block (the card's renderVoices)
            const float keyUnits = param (pKt) / 99.0f * 0.8777f * ((float) s.note - 60.0f);
            const float env = in[1][0];
            const float cA = param (pCut) + keyUnits + param (pEgA) * env + param (pVelA) * s.velocity * 0.6f
                           + 0.5f * (param (pL1A) * in[2][0] + param (pL2A) * in[3][0]);
            const float cB = param (pHp) + param (pEgB) * env + keyUnits;
            const float reso = juce::jlimit (0.0f, 1.0f, param (pRes) / 99.0f);
            const int type = (int) std::lround (param (pType));
            s.fl.type = s.fr.type = type;
            const float a = tb::cutoffHz (juce::jlimit (0.0f, 104.0f, cA)), b = tb::cutoffHz (juce::jlimit (0.0f, 104.0f, cB));
            s.fl.set (a, b, reso, sampleRate);
            // the right side's filter shares the coefficients (its own state)
            for (auto [to, from] : { std::make_pair (&s.fr.s1, &s.fl.s1), std::make_pair (&s.fr.s2, &s.fl.s2) })
            {
                to->g = from->g; to->k = from->k; to->a1 = from->a1; to->a2 = from->a2; to->a3 = from->a3;
            }
            s.trim = trimMemo (param (pTrim), [] (float t) { return tb::dbToGain ((t - 99.0f) * 0.3f + 3.0f); });
        }
        s.k = (s.k + 1) % kCtl;
        const float l = s.fl.process (in[0][0] * s.trim);
        // a layer is mono until its Amp pans it: the right side is worked out only when it differs
        const float r = juce::exactlyEqual (in[0][0], in[0][1]) ? l : s.fr.process (in[0][1] * s.trim);
        out[0] = std::isfinite (l) && std::isfinite (r) ? StereoFrame { l, r } : StereoFrame { 0.0f, 0.0f };
        if (! std::isfinite (l) || ! std::isfinite (r)) { s.fl.reset(); s.fr.reset(); }
    }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        vs[0].velocity = 1.0f; vs[0].note = 60;
        processVoiceSample (0, in, out);
    }

private:
    struct VoiceState { tb::VoiceFilter fl, fr; int k = 0, note = 60; float velocity = 0.8f, trim = 1.0f; bool legato = false; };
    std::array<VoiceState, kMaxVoices> vs {};
    kit::LegatoTracker tracker;
    Memo trimMemo;
};

//==============================================================================
// Tidebreeze - Amp
//==============================================================================
class TidebreezeAmpModule final : public SynthModule
{
public:
    enum P { pLvl, pPan, pVel, pL1, pL2, pLegato };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("amp", "Amp", ModuleSection::Utility, 4);
        d.description = "A Tidebreeze layer's amp: Env In (the amp envelope, 0..1) on the card's curve, Level, the "
                        "velocity's say (Velocity), LFO 1 / 2 as tremolo by their depths, and Pan. With nothing in Env "
                        "In it follows the key.";
        d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("envIn", "Env In"), modIn ("lfo1In", "LFO 1"), modIn ("lfo2In", "LFO 2"),
                      audioOut ("audioOut", "Audio Out") };
        d.params = {
            steps ("aLvl", "Level", 0, 127, 100, 0),
            steps ("aPan", "Pan", -63, 63, 0, 0),
            steps ("aVel", "Velocity", 0, 99, 50, 0),
            steps ("aL1", "LFO1 Amp", -99, 99, 0, 1),
            steps ("aL2", "LFO2 Amp", -99, 99, 0, 1),
            makeCombo ("legato", "Legato", offOn(), 0, 1, 1).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { for (auto& s : vs) s = {}; tracker.reset(); }
    void voiceReset (int v) override { vs[(size_t) v] = {}; }

    void voiceNoteOn (int v, int, bool) override
    {
        auto& s = vs[(size_t) v];
        const int from = tracker.noteOn (v, param (pLegato) >= 0.5f);
        if (from >= 0) { s = vs[(size_t) from]; s.on = true; s.legato = true; return; }
        s = {};
        s.on = true;
    }
    // a legato note keeps the velocity the line started with (as the card does)
    void voiceVelocity (int v, float velocity01) override { if (! vs[(size_t) v].legato) vs[(size_t) v].velocity = velocity01; }
    void voiceNoteOff (int v) override { tracker.noteOff(); vs[(size_t) v].on = false; }

    void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
    {
        auto& s = vs[(size_t) v];
        if (s.k == 0)
        {
            // the level of one control block, reached by a ramp across it
            const float sens = param (pVel) / 99.0f;
            const float velGain = velMemo (sens * 3.0f + s.velocity * 1000.0f, [&s, sens] (float) { return std::pow (juce::jmax (0.01f, s.velocity), sens * 3.0f); });
            const float env = isInputConnected (1) ? in[1][0] : (s.on ? 1.0f : 0.0f);
            const float trem = 1.0f + 0.5f * (param (pL1) * in[2][0] + param (pL2) * in[3][0]) / 99.0f;
            const float target = param (pLvl) / 127.0f * velGain * ampCurve (env) * juce::jmax (0.0f, trem);
            s.dg = (target - s.gain) / (float) kCtl;
            const float pan = juce::jlimit (-1.0f, 1.0f, param (pPan) / 63.0f);
            s.panL = panMemoL (pan, [] (float p) { return std::cos ((p + 1.0f) * tb::kPi * 0.25f) * 1.414f; });
            s.panR = panMemoR (pan, [] (float p) { return std::sin ((p + 1.0f) * tb::kPi * 0.25f) * 1.414f; });
        }
        s.k = (s.k + 1) % kCtl;
        const float g = s.gain;
        s.gain += s.dg;
        out[0] = { in[0][0] * g * s.panL, in[0][1] * g * s.panR };
    }
    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        vs[0].velocity = 1.0f; vs[0].on = true;
        processVoiceSample (0, in, out);
    }

private:
    struct VoiceState { int k = 0; float velocity = 0.8f, gain = 0.0f, dg = 0.0f, panL = 1.0f, panR = 1.0f; bool on = false, legato = false; };
    std::array<VoiceState, kMaxVoices> vs {};
    kit::LegatoTracker tracker;
    Memo velMemo, panMemoL, panMemoR;
};

//==============================================================================
// Tidebreeze - Arp
//==============================================================================
class TidebreezeArpModule final : public SynthModule
{
public:
    enum P { pPat, pRes, pOct, pGate, pVel, pSwing, pLatch, pTempo };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("arp", "Arp", ModuleSection::Utility, 5);
        d.description = "Tidebreeze's arpeggiator: holds the keys (or the stream on its Midi In) and steps through them - "
                        "Up, Down, Up & Down, Random, As Played, Chord - or plays the rhythm patterns (Bossa, 3+3+2, Gallop, "
                        "Rise & Fall, Echo Steps) with their own accents and rests. Velocity 0 keeps the played velocity. "
                        "It runs from the host's tempo, or Tempo without one. Its Midi Out replaces the keys for what it feeds.";
        d.sockets = { midiIn ("midiIn", "Midi In"), midiOut ("midiOut", "Midi Out") };
        d.params = {
            makeCombo ("arpPat", "Pattern", tb::arpPatternNames(), 0, 0, 2).noMod(),
            makeCombo ("arpRes", "Rate", tb::arpResNames(), 2, 0, 1).noMod(),
            steps ("arpOct", "Octaves", 1, 4, 1, 0),
            steps ("arpGate", "Gate", 0, 100, 60, 0, "%"),
            steps ("arpVel", "Velocity", 0, 127, 0, 1),
            steps ("arpSwing", "Swing", -50, 50, 0, 1),
            makeCombo ("arpLatch", "Latch", offOn(), 0, 1, 1).noMod(),
            steps ("arpTempo", "Tempo", 40, 300, 120, 1, "bpm")
        };
        return d;
    }

    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }
    bool isMidiProcessor() const override { return true; }

    void prepare (double sr) override { SynthModule::prepare (sr); buf.ensureSize (512); reset(); }
    void reset() override { arp = tb::Arp(); pendingOffs.clear(); }

    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent*, int) override
    {
        if (in.isOn) arp.noteOn (in.note, in.velocity);
        else arp.noteOff (in.note);
        return 0;
    }
    void midiAllNotesOff() override
    {
        buf.clear();
        arp.allOff (buf, 0);
        for (const auto meta : buf) pendingOffs.push_back (meta.getMessage().getNoteNumber());
        buf.clear();
    }

    int midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents) override
    {
        int n = 0;
        while (! pendingOffs.empty() && n < maxEvents) { events[n++] = { pendingOffs.back(), 0.0f, false }; pendingOffs.pop_back(); }
        const tb::Arp::Settings s { (int) std::lround (param (pPat)), (int) std::lround (param (pRes)), (int) std::lround (param (pOct)),
                                    (int) std::lround (param (pGate)), (int) std::lround (param (pVel)), (int) std::lround (param (pSwing)),
                                    param (pLatch) >= 0.5f };
        const double bpm = hostTransport.hasBpm ? tempoBpm : (double) param (pTempo);
        buf.clear();
        arp.process (s, bpm, sr, 1, buf);
        for (const auto meta : buf)
        {
            if (n >= maxEvents) break;
            const auto m = meta.getMessage();
            if (m.isNoteOn()) events[n++] = { m.getNoteNumber(), m.getFloatVelocity(), true };
            else if (m.isNoteOff()) events[n++] = { m.getNoteNumber(), 0.0f, false };
        }
        return n;
    }

private:
    tb::Arp arp;
    juce::MidiBuffer buf;
    std::vector<int> pendingOffs;
};

//==============================================================================
// Tidebreeze - Insert FX / Send FX: one of its effects
//==============================================================================
namespace
{
    // the five knobs' names for the effect chosen (the knobs themselves are P1..P5)
    class FxNames final : public juce::Component, private juce::Timer
    {
    public:
        explicit FxNames (SynthModule& m) : module (&m) { startTimerHz (4); }
        void paint (juce::Graphics& g) override
        {
            auto* m = module.get();
            if (m == nullptr) return;
            const int type = (int) std::lround (m->getParameter ("type"));
            const auto names = tb::effectParamNames (type);
            juce::String t;
            for (int k = 0; k < 5; ++k) t << (k > 0 ? "   " : "") << "P" << (k + 1) << " " << names[k];
            g.setColour (tbui::Palette::glassEdge().withAlpha (0.85f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            g.setFont (tbui::LookAndFeel::crt (9.5f, true));
            tbui::glowText (g, t.toUpperCase(), getLocalBounds().toFloat().reduced (6.0f, 0.0f), juce::Justification::centredLeft, tbui::Palette::phBright());
        }
    private:
        void timerCallback() override
        {
            if (auto* m = module.get())
            {
                const int type = (int) std::lround (m->getParameter ("type"));
                if (type != shown) { shown = type; repaint(); }
            }
        }
        juce::WeakReference<SynthModule> module;
        int shown = -1;
    };
}

class TidebreezeFxModule : public SynthModule
{
public:
    static constexpr int kBlock = 32;   // the effects run in blocks, kBlock samples late

    explicit TidebreezeFxModule (bool sendSlot) : send (sendSlot) {}

    static std::vector<ParamSpec> fxParams (bool send)
    {
        std::vector<ParamSpec> p {
            makeCombo ("type", "Effect", tb::effectNames(), 0, 0, 3).noMod(),
            makeCombo ("on", "On", offOn(), 1, 0, 1).noMod()
        };
        for (int k = 0; k < 5; ++k)
        {
            const juce::String id = "p" + juce::String (k + 1);
            p.push_back (makeRotary (id, "P" + juce::String (k + 1), 0.0f, 100.0f, 50.0f, 1, "", false, 1.0f));
        }
        if (send)
        {
            p.push_back (steps ("send", "Send", 0, 127, 0, 0));
            p.push_back (steps ("ret", "Return", 0, 127, 100, 2));
        }
        else
            p.push_back (steps ("mix", "Dry/Wet", 0, 100, 50, 0));
        return p;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        builtType = -1;
        rebuild();
        for (auto& f : inFifo) f.fill (0.0f);
        for (auto& f : outFifo) f.fill (0.0f);
        pos = 0;
    }
    void reset() override
    {
        const juce::SpinLock::ScopedLockType sl (fxLock);
        if (fx != nullptr) fx->reset();
    }
    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        if (id == "type" && juce::MessageManager::existsAndIsCurrentThread()) rebuild();
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        inFifo[0][(size_t) pos] = in[0][0];
        inFifo[1][(size_t) pos] = in[0][1];
        out[0] = { outFifo[0][(size_t) pos], outFifo[1][(size_t) pos] };
        if (++pos >= kBlock) { pos = 0; runBlock(); }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override { return std::make_unique<FxNames> (*this); }
    int extraContentHeight() const override { return 20; }

private:
    // a new effect is made here (the message thread) and swapped in
    void rebuild()
    {
        const int type = (int) std::lround (getParameterBase (0));
        if (type == builtType) return;
        std::unique_ptr<tb::Effect> made = tb::makeEffect (type);
        if (made != nullptr) { made->prepare (sampleRate); made->reset(); }
        {
            const juce::SpinLock::ScopedLockType sl (fxLock);
            fx.swap (made);
            fxType = type;
        }
        builtType = type;   // the old one goes here, outside the lock
    }

    void runBlock()
    {
        const int type = (int) std::lround (param (0));
        const bool on = param (1) >= 0.5f && type != tb::FxNone;
        float pr[5];
        for (int k = 0; k < 5; ++k) pr[k] = param (2 + k) * 0.01f;
        float* L = blockL.data();
        float* R = blockR.data();
        const juce::SpinLock::ScopedTryLockType sl (fxLock);
        const bool ready = sl.isLocked() && fx != nullptr && fxType == type && on;
        if (! send)
        {
            // an insert: its output mixed with the dry signal (Dry/Wet)
            std::copy (inFifo[0].begin(), inFifo[0].end(), L);
            std::copy (inFifo[1].begin(), inFifo[1].end(), R);
            if (ready)
            {
                fx->set (pr, tempoBpm);
                fx->process (L, R, kBlock);
                const float wet = param (7) * 0.01f, dry = 1.0f - wet;
                for (int i = 0; i < kBlock; ++i) { L[i] = inFifo[0][(size_t) i] * dry + L[i] * wet; R[i] = inFifo[1][(size_t) i] * dry + R[i] * wet; }
            }
        }
        else
        {
            // a send: the wet return (reverbs and Mod Delay return their wet
            // signal; the others their output minus the dry they were sent)
            const float sendG = param (7) / 127.0f, ret = param (8) / 127.0f;
            if (ready && sendG > 0.0f)
            {
                for (int i = 0; i < kBlock; ++i) { L[i] = inFifo[0][(size_t) i] * sendG; R[i] = inFifo[1][(size_t) i] * sendG; }
                fx->set (pr, tempoBpm);
                fx->process (L, R, kBlock);
                const bool wetOnly = tb::returnsWetOnly (type);
                for (int i = 0; i < kBlock; ++i)
                {
                    L[i] = ret * (wetOnly ? L[i] : L[i] - 0.5f * inFifo[0][(size_t) i] * sendG);
                    R[i] = ret * (wetOnly ? R[i] : R[i] - 0.5f * inFifo[1][(size_t) i] * sendG);
                }
            }
            else
            {
                std::fill (blockL.begin(), blockL.end(), 0.0f);
                std::fill (blockR.begin(), blockR.end(), 0.0f);
            }
        }
        for (int i = 0; i < kBlock; ++i)
        {
            outFifo[0][(size_t) i] = std::isfinite (L[i]) ? L[i] : 0.0f;
            outFifo[1][(size_t) i] = std::isfinite (R[i]) ? R[i] : 0.0f;
        }
    }

    const bool send;
    std::unique_ptr<tb::Effect> fx;
    int fxType = -1, builtType = -1;
    juce::SpinLock fxLock;
    std::array<std::array<float, kBlock>, 2> inFifo {}, outFifo {};
    std::array<float, kBlock> blockL {}, blockR {};
    int pos = 0;
};

class TidebreezeInsertModule final : public TidebreezeFxModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("insert", "Insert FX", ModuleSection::Effect, 6);
        d.description = "One of Tidebreeze's insert effects, in series: phaser, chorus, ensemble, flanger, delays, drift "
                        "filter, auto pan, decimator, tilt EQ, halls, plate, undertow. P1..P5 are the effect's five "
                        "controls (named on the card), Dry/Wet mixes it with what comes in. 32 samples late.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = fxParams (false);
        return d;
    }
    TidebreezeInsertModule() : TidebreezeFxModule (false) {}
};

class TidebreezeSendModule final : public TidebreezeFxModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("send", "Send FX", ModuleSection::Effect, 7);
        d.description = "One of Tidebreeze's two send effects, beside the inserts: what comes in is sent to the effect "
                        "at Send, and Audio Out is only its return (at Return) - patch it into the Output along with the "
                        "dry signal. P1..P5 are the effect's five controls (named on the card). 32 samples late.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Return Out") };
        d.params = fxParams (true);
        return d;
    }
    TidebreezeSendModule() : TidebreezeFxModule (true) {}
};

//==============================================================================
// Tidebreeze - Output
//==============================================================================
class TidebreezeOutputModule final : public SynthModule
{
public:
    enum P { pLo, pMid, pMidHz, pHi, pVolume };

    static ModuleDescriptor descriptor()
    {
        auto d = base ("output", "Output", ModuleSection::Utility, 8);
        d.description = "Tidebreeze's end: everything patched in (the inserts' output and the sends' returns) through the "
                        "master EQ (low shelf, mid bell, high shelf), the program volume and the card's soft knee above "
                        "-2 dBFS.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            steps ("eqLo", "EQ Low", -18, 18, 0, 0, "dB"),
            steps ("eqMid", "EQ Mid", -18, 18, 0, 0, "dB"),
            makeRotary ("eqMidHz", "Mid Freq", 200.0f, 8000.0f, 1000.0f, 0, "Hz", true, 1.0f),
            steps ("eqHi", "EQ High", -18, 18, 0, 0, "dB"),
            steps ("volume", "Volume", 0, 127, 105, 0)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override
    {
        for (int c = 0; c < 2; ++c) { lo[c].reset(); mid[c].reset(); hi[c].reset(); }
        lastMidHz = -1.0f;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (--counter < 0)
        {
            counter = 31;
            gLo = tb::dbToGain (param (pLo)); gMid = tb::dbToGain (param (pMid)); gHi = tb::dbToGain (param (pHi));
            eq = std::abs (param (pLo)) > 1.0e-4f || std::abs (param (pMid)) > 1.0e-4f || std::abs (param (pHi)) > 1.0e-4f;
            const float midHz = param (pMidHz);
            if (! juce::exactlyEqual (midHz, lastMidHz))
            {
                for (int c = 0; c < 2; ++c) { lo[c].set (150.0f, 0.6f, sampleRate); mid[c].set (midHz, 0.8f, sampleRate); hi[c].set (6000.0f, 0.6f, sampleRate); }
                lastMidHz = midHz;
            }
            const float v = param (pVolume) / 127.0f;
            vol = v * v * 1.2f;
        }
        float x[2] = { in[0][0], in[0][1] };
        for (int c = 0; c < 2; ++c)
        {
            float y = x[c];
            if (eq)
            {
                float b, h;
                const float l = lo[c].process (y, b, h); y += (gLo - 1.0f) * l;
                mid[c].process (y, b, h); y += (gMid - 1.0f) * b;
                hi[c].process (y, b, h); y += (gHi - 1.0f) * h;
            }
            y *= vol;
            const float ay = std::abs (y);
            if (ay > 0.8f) y = std::copysign (0.8f + 0.2f * std::tanh ((ay - 0.8f) * 5.0f), y);
            x[c] = std::isfinite (y) ? y : 0.0f;
        }
        out[0] = { x[0], x[1] };
    }

private:
    tb::Svf lo[2], mid[2], hi[2];
    float gLo = 1.0f, gMid = 1.0f, gHi = 1.0f, vol = 1.0f, lastMidHz = -1.0f;
    bool eq = false;
    int counter = 0;
};

AQUANODE_REGISTER_MODULE (TidebreezeWaveModule,   TidebreezeWaveModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeEnvModule,    TidebreezeEnvModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeLfoModule,    TidebreezeLfoModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeFilterModule, TidebreezeFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeAmpModule,    TidebreezeAmpModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeArpModule,    TidebreezeArpModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeInsertModule, TidebreezeInsertModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeSendModule,   TidebreezeSendModule::descriptor)
AQUANODE_REGISTER_MODULE (TidebreezeOutputModule, TidebreezeOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::tidebreeze
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        auto* src = dynamic_cast<TB*> (&original);
        auto P = [&] (const juce::String& id, float def = 0.0f) { return kit::param (original, id, def); };

        const bool two = P ("mode") >= 0.5f;
        const bool mono = P ("voiceMode") >= 0.5f;
        const bool legato = mono && P ("legato") >= 0.5f;
        const bool hold = P ("hold") >= 0.5f;
        // the macro knobs, folded into what they move
        const float rtCutoff = P ("rtCutoff"), rtReso = P ("rtReso"), rtRelease = P ("rtRelease");
        const float egScale = 1.0f + P ("rtEgInt") / 63.0f;

        PatchPorts ports;
        ports.name = src != nullptr && src->programName.isNotEmpty() ? "Tidebreeze - " + src->programName : juce::String ("Tidebreeze");

        // the effects: inserts in series, sends beside them, the output
        constexpr int xIns = 1500, xSend = 1880, xOut = 2260;
        std::vector<int> inserts;
        for (int n = 0; n < 3; ++n)
        {
            const int type = (int) std::lround (P (tb::fxId ("ifx", n, "type")));
            if (type == tb::FxNone) continue;
            const int m = b.add ("plugin.tidebreeze.insert", xIns, 260 * (int) inserts.size());
            b.set (m, "type", (float) type);
            b.set (m, "on", P (tb::fxId ("ifx", n, "on"), 1.0f));
            for (int k = 0; k < 5; ++k) b.set (m, "p" + juce::String (k + 1), P (tb::fxId ("ifx", n, ("p" + juce::String (k + 1)).toRawUTF8()), 50.0f));
            b.set (m, "mix", P (tb::fxId ("ifx", n, "mix"), 50.0f));
            if (! inserts.empty()) b.cable (inserts.back(), "audioOut", m, "audioIn");
            inserts.push_back (m);
        }
        const int out = b.add ("plugin.tidebreeze.output", xOut, 200);
        for (const char* id : { "eqLo", "eqMid", "eqMidHz", "eqHi", "volume" }) b.set (out, id, P (id));
        ports.audioOut = { { out, "audioOut" } };

        // where the layers go in, and what the sends hear
        const int into = inserts.empty() ? out : inserts.front();
        auto layersTo = [&] (int amp) { b.cable (amp, "audioOut", into, "audioIn"); };
        int sendCount = 0;
        std::vector<int> sendSources;   // sends fed by the layers themselves (no inserts)
        for (int n = 0; n < 2; ++n)
        {
            const int type = (int) std::lround (P (tb::fxId ("mfx", n, "type")));
            if (type == tb::FxNone) continue;
            const int m = b.add ("plugin.tidebreeze.send", xSend, 420 * sendCount++);
            b.set (m, "type", (float) type);
            b.set (m, "on", P (tb::fxId ("mfx", n, "on"), 1.0f));
            for (int k = 0; k < 5; ++k) b.set (m, "p" + juce::String (k + 1), P (tb::fxId ("mfx", n, ("p" + juce::String (k + 1)).toRawUTF8()), 50.0f));
            b.set (m, "send", P (tb::fxId ("mfx", n, "send")));
            b.set (m, "ret", P (tb::fxId ("mfx", n, "ret"), 100.0f));
            b.cable (m, "audioOut", out, "audioIn");
            if (! inserts.empty()) b.cable (inserts.back(), "audioOut", m, "audioIn");
            else sendSources.push_back (m);
        }
        if (! inserts.empty()) b.cable (inserts.back(), "audioOut", out, "audioIn");

        // the arpeggio in front of the layers
        int arp = -1;
        if (P ("arpOn") >= 0.5f)
        {
            arp = b.add ("plugin.tidebreeze.arp", -420, 0);
            for (const char* id : { "arpPat", "arpRes", "arpOct", "arpGate", "arpVel", "arpSwing", "arpLatch", "arpTempo" }) b.set (arp, id, P (id));
            ports.midiIn = { { arp, "midiIn" } };
        }

        // the pitch envelope, common to both layers
        const int peg = b.add ("plugin.tidebreeze.env", 0, 0);
        b.set (peg, "kind", 2.0f);
        b.set (peg, "start", P ("pegStart")); b.set (peg, "attack", P ("pegAttack"));
        b.set (peg, "break", 0.0f); b.set (peg, "sustain", 0.0f); b.set (peg, "release", P ("pegRelease"));
        b.set (peg, "attackT", P ("pegAttackT")); b.set (peg, "decayT", P ("pegDecayT"));
        b.set (peg, "slopeT", 0.0f); b.set (peg, "releaseT", P ("pegReleaseT"));
        b.set (peg, "hold", hold ? 1.0f : 0.0f); b.set (peg, "legato", legato ? 1.0f : 0.0f);

        constexpr int layerH = 1000;
        for (int o = 0; o < (two ? 2 : 1); ++o)
        {
            const int y = 360 + o * layerH;
            auto id = [o] (const char* n) { return tb::oscId (o, n); };
            auto Po = [&] (const char* n, float def = 0.0f) { return P (id (n), def); };

            // the wave, with the user waves it plays
            const int wave = b.add ("plugin.tidebreeze.wave", 400, y);
            for (const char* n : { "hiWave", "loWave", "velSw", "hiRev", "hiOfs", "hiLvl", "loRev", "loOfs", "loLvl", "octave",
                                   "transpose", "tune", "delay", "pSlope", "velLo", "velHi", "pegInt", "l1Pitch", "l2Pitch" })
                b.set (wave, n, Po (n));
            b.set (wave, "bend", P ("bend", 2.0f));
            b.set (wave, "porta", P ("porta"));
            b.set (wave, "voices", mono ? 0.0f : (float) (kMaxVoices - 1));
            b.set (wave, "legato", legato ? 1.0f : 0.0f);
            if (auto* wm = dynamic_cast<TidebreezeWaveModule*> (b.dsp (wave)); wm != nullptr && src != nullptr)
            {
                const int hi = (int) std::lround (Po ("hiWave")), lo = (int) std::lround (Po ("loWave"));
                wm->waves().copyFrom (src->bank, [hi, lo] (int s) { return tb::WaveBank::choiceOf (s) == hi || tb::WaveBank::choiceOf (s) == lo; });
                wm->ensureWaves();
            }
            if (arp >= 0) b.cable (arp, "midiOut", wave, "midiIn");
            else ports.midiIn.push_back ({ wave, "midiIn" });
            b.cable (peg, "envOut", wave, "pegIn");

            // its envelopes and LFOs
            const int feg = b.add ("plugin.tidebreeze.env", 0, y + 330);
            const char* fegIds[9] = { "fegStart", "fegAttack", "fegBreak", "fegSustain", "fegRelease", "fegAttackT", "fegDecayT", "fegSlopeT", "fegReleaseT" };
            const char* envIds[9] = { "start", "attack", "break", "sustain", "release", "attackT", "decayT", "slopeT", "releaseT" };
            for (int k = 0; k < 9; ++k) b.set (feg, envIds[k], Po (fegIds[k]));
            b.set (feg, "kind", 0.0f);
            const int aeg = b.add ("plugin.tidebreeze.env", 0, y + 660);
            const char* aegIds[8] = { "aegStart", "aegAttack", "aegBreak", "aegSustain", "aegAttackT", "aegDecayT", "aegSlopeT", "aegReleaseT" };
            const char* envIds8[8] = { "start", "attack", "break", "sustain", "attackT", "decayT", "slopeT", "releaseT" };
            for (int k = 0; k < 8; ++k) b.set (aeg, envIds8[k], Po (aegIds[k]));
            b.set (aeg, "release", 0.0f);
            b.set (aeg, "releaseT", juce::jlimit (0.0f, 99.0f, Po ("aegReleaseT", 30.0f) + rtRelease * 0.6f));
            b.set (aeg, "kind", 1.0f);
            for (int e : { feg, aeg })
            {
                b.set (e, "delay", Po ("delay"));
                b.set (e, "hold", hold ? 1.0f : 0.0f);
                b.set (e, "legato", legato ? 1.0f : 0.0f);
            }
            int lfo[2];
            for (int l = 0; l < 2; ++l)
            {
                lfo[l] = b.add ("plugin.tidebreeze.lfo", -420, y + 300 + l * 330);
                const juce::String p = "l" + juce::String (l + 1);
                for (const char* n : { "Wave", "Freq", "Sync", "Div", "Key", "Fade", "Delay", "Ofs" })
                    b.set (lfo[l], juce::String (n).toLowerCase(), P (id ((p + n).toRawUTF8())));
                b.set (lfo[l], "legato", legato ? 1.0f : 0.0f);
                b.cable (lfo[l], "lfoOut", wave, l == 0 ? "lfo1In" : "lfo2In");
            }

            // the filter and the amp
            const int filt = b.add ("plugin.tidebreeze.filter", 780, y);
            b.set (filt, "fType", Po ("fType"));
            b.set (filt, "fA", juce::jlimit (0.0f, 104.0f, Po ("fA", 99.0f) + rtCutoff * 0.8f));
            b.set (filt, "fRes", juce::jlimit (0.0f, 99.0f, Po ("fRes") + rtReso * 0.8f));
            for (const char* n : { "fB", "fTrim", "fKt", "fVelA", "fL1A", "fL2A" }) b.set (filt, n, Po (n));
            b.set (filt, "fEgA", juce::jlimit (-99.0f, 99.0f, Po ("fEgA") * egScale));
            b.set (filt, "fEgB", juce::jlimit (-99.0f, 99.0f, Po ("fEgB") * egScale));
            b.set (filt, "legato", legato ? 1.0f : 0.0f);
            const int amp = b.add ("plugin.tidebreeze.amp", 1140, y);
            for (const char* n : { "aLvl", "aPan", "aVel", "aL1", "aL2" }) b.set (amp, n, Po (n));
            b.set (amp, "legato", legato ? 1.0f : 0.0f);

            b.cable (wave, "audioOut", filt, "audioIn");
            b.cable (feg, "envOut", filt, "envIn");
            b.cable (lfo[0], "lfoOut", filt, "lfo1In");
            b.cable (lfo[1], "lfoOut", filt, "lfo2In");
            b.cable (filt, "audioOut", amp, "audioIn");
            b.cable (aeg, "envOut", amp, "envIn");
            b.cable (lfo[0], "lfoOut", amp, "lfo1In");
            b.cable (lfo[1], "lfoOut", amp, "lfo2In");
            layersTo (amp);
            // with no inserts, the sends hear the layers themselves
            for (int sm : sendSources) b.cable (amp, "audioOut", sm, "audioIn");
        }
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* tbp = dynamic_cast<TB*> (&original);
        juce::MemoryBlock mb;
        if (tbp == nullptr || ! file.loadFileAsData (mb)) return false;
        // a Tidebreeze preset is the plugin's state (XML in JUCE's binary wrapper)
        std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()));
        if (xml == nullptr || ! xml->hasTagName ("Tidebreeze")) return false;
        tbp->setStateInformation (mb.getData(), (int) mb.getSize());
        return true;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
