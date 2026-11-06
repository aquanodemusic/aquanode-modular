//==============================================================================
// Soundscape Ecology - Patch version
//
// Soundscape Ecology renders three slots (each a field of individuals of one
// biophony / geophony / anthrophony model, with its own rhythm and space),
// gates them by MIDI, runs the sum through its habitat ambience and ends with
// width and the output stage. The patch version is that, module by module:
//
//   Field (one per slot) --> Gate --> Habitat --> Output
//        ^  Trig / Gate / Pitch  |
//        +-----------------------+         Climate --> every Field, Habitat
//
// Each Field plays its slot with the plugin's own engine (the field, the
// niche allocator, the weather that quietens the animals), running free; its
// knobs are that slot's parameters and take cables, and the drawn curves and
// everything else of the scene travel in the module's state. The Gate is the
// plugin's MIDI gate for the whole scene: it gates the fields' sum and tells
// the fields when a note opens it (they restart their chorus), when it is
// shut (they stop) and how far to transpose (Gated + Pitch). The Climate
// module holds the scene's temperature, humidity, time of day, niche and
// weather and sets every field's (and the habitat's) knobs to them. The
// Habitat runs the plugin's FDN, its damping moved by the same gusts; the
// Output is its width and soft limit.
//
// Patches made before the Field / Gate / Climate split still load: their
// Slot modules are fields with the gate and climate inside.
//==============================================================================
#include "PluginSoundscapeConfig.h"

namespace aqp_soundscape
{
    #include AQP_SOUNDSCAPE_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "soundscape" };
    using SE = aqp_soundscape::SoundscapeEcologyAudioProcessor;
    namespace seco = aqp_soundscape::soundeco;
    constexpr int kSlots = seco::kNumSlots;
    constexpr int kBlock = 32;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.soundscape." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    ParamSpec knob (const juce::String& id, const juce::String& label, float lo, float hi, float def, int row,
                    const char* unit = "", bool log = false, float step = 0.0f)
    {
        return makeRotary (id, label, lo, hi, def, row, unit, log, step);
    }

    // the Slot module's own knob ids that stand for "s<k>_<id>" in the plugin
    bool isSlotParam (const juce::String& id)
    {
        static const juce::StringArray ids { "phony", "model", "on", "level", "k0", "k1", "k2", "k3", "k4", "k5",
                                             "k6", "k7", "rate", "phrase", "rhythm", "jitter", "swing", "attack",
                                             "decay", "dur", "pop", "spread", "dist", "div", "sync", "elev", "para",
                                             "drawp", "drawa", "drawd" };
        return ids.contains (id);
    }
    juce::String pluginId (const juce::String& id, int slot)
    {
        return isSlotParam (id) ? "s" + juce::String (slot) + "_" + id : id;
    }
}

//==============================================================================
// Soundscape Ecology - Field (and Slot, the older all-in-one form of it)
//
// A Field plays one of the scene's slots with the plugin's own engine, running
// free: the MIDI gate is the Soundscape Gate module (its Trig Out restarts the
// field's chorus as the plugin does when a note opens the gate, its Gate Out
// stops the field while the gate is shut, its Pitch Out transposes it), and the
// climate can come from one Soundscape Climate module driving every field.
// The Slot - a field with the climate and the gate inside - is what patches
// made before the split were built from; it still loads, but is no longer
// listed.
//==============================================================================
template <bool IsField>
class SoundscapeSlotModuleT final : public SynthModule
{
public:
    static ModuleDescriptor fieldDescriptor()
    {
        auto d = slotDescriptor();
        d.typeId = "plugin.soundscape.field";
        d.displayName = kit::moduleName (kPlugin, "Field");
        d.description = "One of Soundscape Ecology's three slots, played by the plugin's own engine: a field of "
                        "individuals of one model (insects, frogs, birds; rain, wind, water; engines, bells...) "
                        "with its physics (P1..P8), its rhythm, its population and space, the acoustic niche and "
                        "the weather. Trig In restarts its chorus (Soundscape Gate's Trig Out), Gate In stops it "
                        "while the gate is shut, Pitch In transposes it in semitones. Its climate knobs follow a "
                        "Soundscape Climate module when one is cabled to them.";
        d.sockets = { modIn ("gateIn", "Gate In"), modIn ("trigIn", "Trig In"), modIn ("pitchIn", "Pitch In"),
                      audioOut ("audioOut", "Audio Out") };
        while (! d.params.empty() && d.params.back().id.startsWith ("midi"))
            d.params.pop_back();
        return d;
    }

    static ModuleDescriptor descriptor() { return IsField ? fieldDescriptor() : slotDescriptor(); }

    static ModuleDescriptor slotDescriptor()
    {
        auto d = base ("slot", "Slot", ModuleSection::Oscillator, 0);   // listed again: the Field played by its own Midi In
        d.description = "One of Soundscape Ecology's three slots, played by the plugin's own engine: a field of "
                        "individuals of one model (insects, frogs, birds; rain, wind, water; engines, bells...) "
                        "with its physics (P1..P8), its rhythm, its population and space, the acoustic niche "
                        "and the weather. Midi In opens its gate (and transposes it in Gated + Pitch). Its "
                        "output goes to the Soundscape Habitat. The drawn curves come with the module.";
        d.sockets = { midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeSteppedList ("slot", "Slot", { "1", "2", "3" }, 0, 0).noMod(),
            makeCombo ("phony", "Class", { "Biophony", "Geophony", "Anthrophony" }, 0, 0, 2),
            knob ("model", "Model", 0, (float) (seco::kMaxModelsPerClass - 1), 0, 0, "", false, 1.0f),
            makeCombo ("on", "On", { "Off", "On" }, 1, 0, 1),
            knob ("level", "Level", 0.0f, 1.5f, 0.8f, 0),

            knob ("k0", "P1", 0.0f, 1.0f, 0.5f, 1), knob ("k1", "P2", 0.0f, 1.0f, 0.5f, 1),
            knob ("k2", "P3", 0.0f, 1.0f, 0.5f, 1), knob ("k3", "P4", 0.0f, 1.0f, 0.5f, 1),
            knob ("k4", "P5", 0.0f, 1.0f, 0.5f, 1), knob ("k5", "P6", 0.0f, 1.0f, 0.5f, 1),
            knob ("k6", "P7", 0.0f, 1.0f, 0.5f, 1), knob ("k7", "P8", 0.0f, 1.0f, 0.5f, 1),

            knob ("rate", "Rate", 0.02f, 24.0f, 1.5f, 2, "Hz", true),
            knob ("phrase", "Phrase", 1.0f, 64.0f, 1.0f, 2, "", true),
            knob ("rhythm", "Rhythm", 0.0f, 1.0f, 0.0f, 2),
            knob ("jitter", "Jitter", 0.0f, 1.0f, 0.25f, 2),
            knob ("swing", "Hold", -0.5f, 0.5f, 0.0f, 2),
            knob ("attack", "Attack", 0.0f, 1.0f, 0.08f, 2),
            knob ("decay", "Decay", 0.0f, 1.0f, 0.35f, 2),
            knob ("dur", "Duration", 0.02f, 4.0f, 0.5f, 2, "s", true),

            knob ("pop", "Population", 1, (float) seco::kMaxIndividuals, 10, 3, "", false, 1.0f),
            knob ("spread", "Spread", 0.0f, 1.0f, 0.6f, 3),
            knob ("dist", "Distance", 0.0f, 1.0f, 0.35f, 3),
            knob ("div", "Diversity", 0.0f, 1.0f, 0.35f, 3),
            knob ("sync", "Synchrony", 0.0f, 1.0f, 0.3f, 3),
            knob ("elev", "Elevation", 0.02f, 30.0f, 0.15f, 3, "m", true),
            knob ("para", "Parallax", 0.05f, 1.0f, 0.5f, 3),

            makeCombo ("drawp", "Draw Pitch", { "Off", "On" }, 0, 4, 1),
            makeCombo ("drawa", "Draw Amp", { "Off", "On" }, 0, 4, 1),
            knob ("drawd", "Draw Depth", 0.0f, 1.0f, 0.5f, 4),
            knob ("temperature", "Temp", -5.0f, 42.0f, 22.0f, 4, "C"),
            knob ("humidity", "Humidity", 0.0f, 1.0f, 0.5f, 4),
            knob ("timeofday", "Time of Day", 0.0f, 1.0f, 0.75f, 4),
            knob ("niche", "Niche", 0.0f, 1.0f, 0.0f, 4),
            knob ("weather", "Weather", 0.0f, 1.0f, 0.45f, 4),

            makeCombo ("midimode", "MIDI", { "Free Run", "Gated", "Gated + Pitch" }, 1, 5, 2),
            knob ("midiattack", "Attack", 0.001f, 8.0f, 0.35f, 5, "s", true),
            knob ("midirelease", "Release", 0.005f, 20.0f, 1.6f, 5, "s", true),
            knob ("midivel", "Velocity", 0.0f, 1.0f, 0.4f, 5),
            knob ("midiroot", "Root", 0, 127, 60, 5, "", false, 1.0f),
            knob ("midipitchamt", "Pitch Amt", 0.0f, 1.0f, 1.0f, 5)
        };
        return d;
    }

    SoundscapeSlotModuleT()
    {
        auto p = createOriginal (kPlugin, 48000.0);
        se.reset (dynamic_cast<SE*> (p.get()));
        if (se != nullptr)
            p.release();
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return ! IsField; }
    void rawMidiMessage (const juce::MidiMessage& m) override { midiQueue.addEvent (m, pos); }
    void midiAllNotesOff() override { midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos); }

    // which of the scene's slots this module plays
    void useSlot (int k)
    {
        slot = juce::jlimit (0, kSlots - 1, k);
        relink();
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        if (se == nullptr)
            return;
        {
            const juce::ScopedLock sl (se->getCallbackLock());
            se->releaseResources();
            se->setPlayConfigDetails (0, 2, sr, kBlock);
            se->prepareToPlay (sr, kBlock);
        }
        useSlot ((int) std::lround (getParameterBase (0)));
        midiQueue.ensureSize (1024);
        io.setSize (2, kBlock);
        for (auto& o : out) o.fill (0.0f);
        pos = 0;
    }

    void reset() override { midiQueue.clear(); midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0); }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        if (IsField)
        {
            const float trig = isInputConnected (1) ? in[1][0] : 0.0f;
            if (trig > 0.5f && lastTrig <= 0.5f)
                trigPending = true;
            lastTrig = trig;
            if (isInputConnected (0) && in[0][0] > 1.0e-4f)
                gateSeen = true;
            if (pos == 0)
                pitch = isInputConnected (2) ? in[2][0] : 0.0f;
        }
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

    juce::String saveCustomState() const override
    {
        if (se == nullptr) return {};
        juce::MemoryBlock mb;
        se->getStateInformation (mb);
        return mb.toBase64Encoding();
    }
    void loadCustomState (const juce::String& s) override
    {
        juce::MemoryBlock mb;
        if (se != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
        {
            se->setStateInformation (mb.getData(), (int) mb.getSize());
            useSlot ((int) std::lround (getParameterBase (0)));
        }
    }

    SE* plugin() const { return se.get(); }

private:
    void relink()
    {
        links.clear();
        if (se == nullptr) return;
        midiModeParam = IsField ? kit::findParam (*se, "midimode") : nullptr;
        const auto& d = getDescriptor();
        for (int i = 1; i < (int) d.params.size(); ++i)
            if (auto* prm = kit::findParam (*se, pluginId (d.params[(size_t) i].id, slot)))
                links.push_back ({ prm, i, -1.0f });
    }

    void runBlock()
    {
        if (se == nullptr)
            return;
        const int wanted = (int) std::lround (getParameterBase (0));
        if (wanted != slot)
            useSlot (wanted);   // only re-points the knob links
        for (auto& l : links)
        {
            const float raw = param (l.knob);   // unmoved knob: nothing to convert or send
            if (juce::exactlyEqual (raw, l.raw)) continue;
            l.raw = raw;
            const float v = l.param->convertTo0to1 (raw);
            if (v != l.last) { l.param->setValueNotifyingHost (v); l.last = v; }
        }

        bool rendered = false;
        bool shut = false;
        if (IsField)
        {
            // the Gate plays the MIDI: the field itself runs free
            if (midiModeParam != nullptr && midiModeParam->getValue() != 0.0f)
                midiModeParam->setValueNotifyingHost (0.0f);
            se->externalPitchRatio = std::pow (2.0f, pitch / 12.0f);
            shut = isInputConnected (0) && ! gateSeen;
            gateSeen = false;
            if (trigPending)
            {
                se->requestRetrigger();
                trigPending = false;
            }
        }
        if (! shut)
        {
            const juce::ScopedTryLock sl (se->getCallbackLock());
            if (sl.isLocked())
            {
                se->renderSources (io, midiQueue, 1u << slot);
                rendered = true;
            }
        }
        midiQueue.clear();
        for (int c = 0; c < 2; ++c)
        {
            const float* s = io.getReadPointer (c);
            for (int i = 0; i < kBlock; ++i)
                out[(size_t) c][(size_t) i] = rendered && std::isfinite (s[i]) ? s[i] : 0.0f;
        }
    }

    struct Link { juce::RangedAudioParameter* param; int knob; float last; float raw = std::numeric_limits<float>::quiet_NaN(); };

    std::unique_ptr<SE> se;
    std::vector<Link> links;
    juce::MidiBuffer midiQueue;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> out {};
    int pos { 0 }, slot { 0 };
    juce::RangedAudioParameter* midiModeParam { nullptr };
    float lastTrig { 0.0f }, pitch { 0.0f };
    bool trigPending { false }, gateSeen { false };
};

using SoundscapeSlotModule = SoundscapeSlotModuleT<false>;
using SoundscapeFieldModule = SoundscapeSlotModuleT<true>;

//==============================================================================
// Soundscape Ecology - Gate: the plugin's MIDI gate (one gate for the whole
// scene, opened by any held note), applied to the fields' sum before the
// habitat so the ambience rings on after release
//==============================================================================
class SoundscapeGateModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("gate", "Gate", ModuleSection::Utility, 3);
        d.description = "Soundscape Ecology's MIDI gate: a held note opens the scene over Attack, letting go closes "
                        "it over Release, and Velocity sets how much the note's velocity counts. Audio In -> Out is "
                        "the fields' sum, gated. Trig Out restarts the fields when a note opens a silent gate, Gate "
                        "Out stops them while it is shut, Pitch Out transposes them (Gated + Pitch: semitones from "
                        "Root, times Pitch Amt).";
        d.sockets = { midiIn ("midiIn", "Midi In"), audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out"),
                      modOut ("gateOut", "Gate Out"), modOut ("trigOut", "Trig Out"), modOut ("pitchOut", "Pitch Out") };
        d.params = {
            makeCombo ("midimode", "MIDI", { "Free Run", "Gated", "Gated + Pitch" }, 1, 0, 2).noMod(),
            knob ("midiattack", "Attack", 0.001f, 8.0f, 0.35f, 0, "s", true),
            knob ("midirelease", "Release", 0.005f, 20.0f, 1.6f, 0, "s", true),
            knob ("midivel", "Velocity", 0.0f, 1.0f, 0.4f, 1),
            knob ("midiroot", "Root", 0, 127, 60, 1, "", false, 1.0f),
            knob ("midipitchamt", "Pitch Amt", 0.0f, 1.0f, 1.0f, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }

    void reset() override { velocity.fill (0); held = 0; target = env = 0.0f; trig = false; }

    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (held == 0) trig = true;
            if (velocity[(size_t) n] == 0) ++held;
            velocity[(size_t) n] = juce::jlimit (1, 127, (int) (m.getFloatVelocity() * 127.0f));
            last = n;
            target = 1.0f;
        }
        else if (m.isNoteOff())
        {
            const int n = m.getNoteNumber();
            if (velocity[(size_t) n] != 0) { velocity[(size_t) n] = 0; held = juce::jmax (0, held - 1); }
            if (held == 0) target = 0.0f;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            midiAllNotesOff();
    }
    void midiAllNotesOff() override { velocity.fill (0); held = 0; target = 0.0f; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int mode = (int) std::lround (param (0));
        if (mode == 0)
        {
            out[0] = in[1];
            out[1] = { 1.0f, 1.0f };
            out[2] = { 0.0f, 0.0f };
            out[3] = { 0.0f, 0.0f };
            trig = false;
            return;
        }
        const float atkInc = 1.0f / juce::jmax (1.0f, (float) (param (1) * sampleRate));
        const float relInc = 1.0f / juce::jmax (1.0f, (float) (param (2) * sampleRate));
        env = juce::jlimit (0.0f, 1.0f, env + (target > env ? atkInc : -relInc));
        const float g = env * env * (3.0f - 2.0f * env);

        const int v = velocity[(size_t) last];
        const float sens = param (3);
        if (v > 0)
            velGain = (1.0f - sens) + sens * (float) v / 127.0f;

        const float gain = g * velGain;
        out[0] = { in[1][0] * gain, in[1][1] * gain };
        const float open = (env > 1.0e-4f || target > 0.0f) ? 1.0f : 0.0f;
        out[1] = { open, open };
        out[2] = { trig ? 1.0f : 0.0f, trig ? 1.0f : 0.0f };
        trig = false;
        const float semis = mode == 2 ? (float) (last - (int) std::lround (param (4))) * param (5) : 0.0f;
        out[3] = { semis, semis };
    }

private:
    std::array<int, 128> velocity {};
    int held { 0 }, last { 60 };
    float target { 0.0f }, env { 0.0f }, velGain { 1.0f };
    bool trig { false };
};

//==============================================================================
// Soundscape Ecology - Climate: the scene's climate in one place. Its outputs
// drive the knobs they are cabled to absolutely (each field's climate, the
// habitat's weather), so a field follows this module's knob exactly.
//==============================================================================
class SoundscapeClimateModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("climate", "Climate", ModuleSection::Utility, 4);
        d.description = "Soundscape Ecology's climate - temperature, humidity, time of day, the acoustic niche and "
                        "the weather - for the whole scene: each output sets the knob it is cabled to (a field's "
                        "climate, the habitat's weather) to this module's value, so one knob turns every field. "
                        "Cable an LFO into these knobs and the day goes by.";
        d.sockets = { modOut ("temperature", "Temp"), modOut ("humidity", "Humidity"), modOut ("timeofday", "Time of Day"),
                      modOut ("niche", "Niche"), modOut ("weather", "Weather") };
        d.params = {
            knob ("temperature", "Temp", -5.0f, 42.0f, 22.0f, 0, "C"),
            knob ("humidity", "Humidity", 0.0f, 1.0f, 0.5f, 0),
            knob ("timeofday", "Time of Day", 0.0f, 1.0f, 0.75f, 0),
            knob ("niche", "Niche", 0.0f, 1.0f, 0.0f, 1),
            knob ("weather", "Weather", 0.0f, 1.0f, 0.45f, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool drivesKnobsAbsolutely() const override { return true; }

    void processSample (const StereoFrame*, StereoFrame* out) override
    {
        const auto& d = getDescriptor();
        for (int i = 0; i < 5; ++i)
        {
            const auto& p = d.params[(size_t) i];
            const float v = (param (i) - p.minValue) / (p.maxValue - p.minValue);
            out[i] = { v, v };
        }
    }
};

//==============================================================================
// Soundscape Ecology - Habitat
//==============================================================================
class SoundscapeHabitatModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("habitat", "Habitat", ModuleSection::Effect, 1);
        d.description = "Soundscape Ecology's habitat: a short, dense, heavily damped FDN ambience - forest "
                        "sound is diffusion and absorption more than a long tail. With Weather up, the gusts "
                        "move the canopy: the damping drops and the ambience brightens and stirs.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            knob ("habsize", "Size", 0.0f, 1.0f, 0.5f, 0),
            knob ("habdamping", "Damping", 0.0f, 1.0f, 0.65f, 0),
            knob ("habmix", "Mix", 0.0f, 1.0f, 0.2f, 0),
            knob ("weather", "Weather", 0.0f, 1.0f, 0.45f, 0)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        habitat.prepare (sr, kBlock);
        weather.prepare (sr);
        habitat.reset();
        io.setSize (2, kBlock);
        for (auto& o : out) o.fill (0.0f);
        pos = 0;
    }
    void reset() override { habitat.reset(); }

    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        io.setSample (0, pos, in[0][0]);
        io.setSample (1, pos, in[0][1]);
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            // as the plugin's block: the damping from the gust so far, then
            // the weather moves on, then the ambience
            const float damp = juce::jlimit (0.0f, 1.0f, param (1) - param (3) * weather.gust * 0.22f);
            habitat.setParams (param (0), damp, param (2));
            weather.update (kBlock, sampleRate);
            habitat.process (io);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = io.getSample (c, i);
                    out[(size_t) c][(size_t) i] = std::isfinite (v) ? v : 0.0f;
                }
        }
    }

private:
    seco::Habitat habitat;
    seco::Weather weather;
    juce::AudioBuffer<float> io;
    std::array<std::array<float, kBlock>, 2> out {};
    int pos { 0 };
};

//==============================================================================
// Soundscape Ecology - Output
//==============================================================================
class SoundscapeOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("output", "Output", ModuleSection::Utility, 2);
        d.description = "Soundscape Ecology's end: stereo Width (0 = mono, 2 = extra wide) and the Output "
                        "level into the plugin's gentle tanh limiter.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            knob ("width", "Width", 0.0f, 2.0f, 1.0f, 0),
            knob ("output", "Output", 0.0f, 2.0f, 1.0f, 0)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        width.reset (sr, 0.02);
        gain.reset (sr, 0.02);
        width.setCurrentAndTargetValue (param (0));
        gain.setCurrentAndTargetValue (param (1));
    }
    void processSample (const StereoFrame* in, StereoFrame* outputs) override
    {
        width.setTargetValue (param (0));
        gain.setTargetValue (param (1));
        const float w = width.getNextValue();
        const float mid = 0.5f * (in[0][0] + in[0][1]);
        const float side = 0.5f * (in[0][0] - in[0][1]) * w;
        const float g = gain.getNextValue() * SE::kMasterLoudness;
        outputs[0] = { std::tanh ((mid + side) * g), std::tanh ((mid - side) * g) };
    }

private:
    juce::SmoothedValue<float> width, gain;
};

AQUANODE_REGISTER_MODULE (SoundscapeSlotModule,    SoundscapeSlotModule::descriptor)
AQUANODE_REGISTER_MODULE (SoundscapeFieldModule,   SoundscapeFieldModule::descriptor)
AQUANODE_REGISTER_MODULE (SoundscapeGateModule,    SoundscapeGateModule::descriptor)
AQUANODE_REGISTER_MODULE (SoundscapeClimateModule, SoundscapeClimateModule::descriptor)
AQUANODE_REGISTER_MODULE (SoundscapeHabitatModule, SoundscapeHabitatModule::descriptor)
AQUANODE_REGISTER_MODULE (SoundscapeOutputModule,  SoundscapeOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::soundscape
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        auto P = [&] (const juce::String& id, float def) { return kit::param (original, id, def); };

        const int hab = b.add ("plugin.soundscape.habitat", 1400, 200);
        b.copy (hab, original);
        const int outM = b.add ("plugin.soundscape.output", 1750, 200);
        b.copy (outM, original);
        b.cable (hab, "audioOut", outM, "audioIn");

        // one gate for the scene, between the fields and the habitat, as in the plugin
        const int gate = b.add ("plugin.soundscape.gate", 1050, 200);
        b.copy (gate, original);
        b.cable (gate, "audioOut", hab, "audioIn");

        // one climate for every field and the habitat's gusts
        const int climate = b.add ("plugin.soundscape.climate", 1050, 600);
        b.copy (climate, original);
        b.mod (climate, "weather", hab, "weather", 1.0f);

        juce::MemoryBlock state;
        original.getStateInformation (state);

        PatchPorts ports;
        ports.name = "Soundscape Ecology";
        ports.audioOut = { { outM, "audioOut" } };
        ports.midiIn.push_back ({ gate, "midiIn" });

        int y = 0;
        for (int k = 0; k < kSlots; ++k)
        {
            if (P (pluginId ("on", k), 1.0f) < 0.5f)
                continue;
            const int m = b.add ("plugin.soundscape.field", 0, y);
            y += 560;
            auto* sm = dynamic_cast<SoundscapeFieldModule*> (b.dsp (m));
            if (sm == nullptr || sm->plugin() == nullptr)
                continue;

            // the whole scene (curves, presets' night variants), playing slot k
            if (state.getSize() > 0)
                sm->plugin()->setStateInformation (state.getData(), (int) state.getSize());
            b.copy (m, original, [k] (const juce::String& id) { return pluginId (id, k); });
            b.set (m, "slot", (float) k);
            sm->useSlot (k);

            b.cable (m, "audioOut", gate, "audioIn");
            b.cable (gate, "gateOut", m, "gateIn");
            b.cable (gate, "trigOut", m, "trigIn");
            b.cable (gate, "pitchOut", m, "pitchIn");
            for (const char* c : { "temperature", "humidity", "timeofday", "niche", "weather" })
                b.mod (climate, c, m, c, 1.0f);
        }
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* se = dynamic_cast<SE*> (&original);
        return se != nullptr && se->loadPresetFile (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
