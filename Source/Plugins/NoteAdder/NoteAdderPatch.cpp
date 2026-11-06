//==============================================================================
// NoteAdder - Patch version
//
// NoteAdder's MIDI chain, stage by stage:
//
//   Midi In -> Note Filter -> (one of the three chords) -> Transpose -> Midi Out
//
//   Note Filter      the scale filter on the played notes: let all through,
//                    discard the ones outside the scale, or play the nearest
//                    in-scale note instead.
//   Manual Chord     seven rows of interval + octave added to every note.
//   Scale Chord      harmonies from a scale (stacking, second layer,
//                    inversions, lock bass, random skip, scale transpose).
//   Per Note Chord   a chord of its own for every pitch class, typed in for
//                    Custom.
//   Transpose        the global semitone transpose, re-voicing held notes when
//                    it moves.
//
// Each chord carries humanize, add length and its own set of NoteAdder's
// animation modes (Manual: ReVoice, Cloud; Scale: ReVoice, Wander, Drift,
// Cloud, Cluster; Per Note: ReVoice, Cloud, Cluster), whose knobs show only
// while that mode is picked. Every stage is optional - the patch version
// builds the chord of the preset's mode, the others are in the sidebar.
//
// Every stage runs NoteAdder's own processBlock (an instance of the plugin
// inside the module) with only its own stage switched on, in 32-sample steps
// like the GUI version, so its notes come out exactly as the plugin plays
// them; the knobs are the plugin's parameters and all take cables.
// (plugin.noteadder.chord, the earlier all-in-one Chord, still loads with old
// patches; it is no longer listed.)
//==============================================================================
#include "PluginNoteAdderConfig.h"

namespace aqp_noteadder
{
    #include AQP_NOTEADDER_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"
#include "../../Modules/Core/TapToTypeEditor.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "noteadder" };
    using NA = aqp_noteadder::NoteAdderProcessor;

    enum class Stage { Filter, Chord, Manual, Scale, PerNote, Transpose };

    juce::StringArray noteNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
    juce::StringArray offOn() { return { "Off", "On" }; }
    juce::StringArray scaleNames()
    {
        juce::StringArray a;
        for (int i = 0; i < NA::NUM_SCALES; ++i)
            a.add (NA::SCALE_NAMES[i]);
        return a;
    }
    juce::StringArray subdivNames() { return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T" }; }
    juce::StringArray velNames() { return { "8", "16", "24", "32", "48", "64", "80", "96", "112", "127" }; }
    juce::StringArray chordNames()
    {
        return { "None", "Maj", "Maj7", "Maj9", "Min", "Min7", "Min9", "6/9", "Dom7", "Sus2", "Sus4", "Power",
                 "Aug", "Dim", "Custom" };
    }
    juce::StringArray range (int lo, int hi, bool plus = false)
    {
        juce::StringArray a;
        for (int i = lo; i <= hi; ++i)
            a.add ((plus && i > 0 ? "+" : "") + juce::String (i));
        return a;
    }

    ParamSpec intKnob (const juce::String& id, const juce::String& label, int lo, int hi, int def, int row,
                       const juce::String& suffix = {})
    {
        return makeRotary (id, label, (float) lo, (float) hi, (float) def, row, suffix, false, 1.0f);
    }

    // visible in one of the three modes (0 Manual, 1 Scale, 2 Per Note)
    ParamSpec inMode (ParamSpec p, int mode) { return p.visibleWhen ("mode", (float) mode); }

    //=== the Chord stage's knobs ==============================================
    std::vector<ParamSpec> chordParams()
    {
        std::vector<ParamSpec> p;
        p.push_back (makeCombo ("mode", "Mode", { "Manual", "Scale", "Per Note" }, 0, 0, 2).noMod());
        p.push_back (intKnob ("humanizetime", "Hum Time", 0, 200, 0, 0, "ms"));
        p.push_back (intKnob ("humanizevel", "Hum Vel", 0, 100, 0, 0));
        p.push_back (makeRotary ("addlength", "Add Length", 0.0f, 20.0f, 0.0f, 0, "s"));
        p.push_back (makeCombo ("discardInput", "Mute Input", offOn(), 0, 1, 1));

        // Manual: seven rows of interval + octave
        for (int i = 0; i < NA::NUM_ROWS; ++i)
        {
            const juce::String s (i), n (i + 1);
            p.push_back (inMode (makeCombo ("enabled" + s, "Row " + n, offOn(), 0, 2 + i, 1), 0));
            p.push_back (inMode (intKnob ("semi" + s, "Semi " + n, -12, 12, 0, 2 + i, "st"), 0));
            p.push_back (inMode (intKnob ("oct" + s, "Oct " + n, -7, 7, 0, 2 + i), 0));
        }

        // Scale
        p.push_back (inMode (makeSteppedList ("root", "Root", noteNames(), 0, 10), 1));
        p.push_back (inMode (makeCombo ("scale", "Scale", scaleNames(), 0, 10, 2), 1));
        p.push_back (inMode (intKnob ("count", "Count", 0, 7, 3, 10), 1));
        p.push_back (inMode (makeSteppedList ("harmonicmode", "Stack",
                                              { "2nds", "3rds", "4ths", "5ths", "6ths", "7ths" }, 1, 11), 1));
        p.push_back (inMode (makeSteppedList ("harmonicmode2", "Stack 2",
                                              { "None", "2nds", "3rds", "4ths", "5ths", "6ths", "7ths" }, 0, 11), 1));
        p.push_back (inMode (makeCombo ("invmode", "Inversion",
                                        { "Off", "Pick", "Closest", "Drop 2", "Cycle Up", "Cycle Down", "Random" }, 0, 11, 2), 1));
        p.push_back (inMode (intKnob ("invpicker", "Inv Pick", 0, 7, 0, 11), 1));
        p.push_back (inMode (makeCombo ("randomskip", "Skip", offOn(), 0, 12, 1), 1));
        p.push_back (inMode (makeCombo ("lockbass", "Lock Bass", offOn(), 0, 12, 1), 1));
        p.push_back (inMode (intKnob ("scaletranspose", "Scale Tr", -12, 12, 0, 12, "steps"), 1));
        p.push_back (inMode (makeCombo ("scaletransposeroot", "Tr Root", offOn(), 0, 12, 1), 1));

        // Per Note: a chord for every pitch class (Custom: the text below)
        const auto names = noteNames();
        for (int i = 0; i < 12; ++i)
            p.push_back (inMode (makeSteppedList ("pnchord" + juce::String (i), names[i], chordNames(), 0, 13 + i / 4), 2));

        // Animation, one set per mode as in the plugin
        p.push_back (inMode (makeCombo ("manimmode", "Animate", { "Off", "ReVoice", "Cloud" }, 0, 20, 2), 0));
        p.push_back (inMode (makeCombo ("animmode", "Animate", { "Off", "ReVoice", "Wander", "Drift", "Cloud", "Cluster" }, 0, 20, 2), 1));
        p.push_back (inMode (makeCombo ("pnanimmode", "Animate", { "Off", "ReVoice", "Cloud", "Cluster" }, 0, 20, 2), 2));
        p.push_back (makeCombo ("cloudmuteinput", "Cloud Mute", offOn(), 0, 20, 1));

        struct Family { const char* prefix; const char* cloudPrefix; int mode; };
        for (auto f : { Family { "manim", "mcloud", 0 }, Family { "anim", "cloud", 1 }, Family { "pnanim", "pncloud", 2 } })
        {
            const juce::String a (f.prefix), c (f.cloudPrefix);
            p.push_back (inMode (makeCombo (a + "sync", "Sync", offOn(), 1, 21, 1), f.mode));
            p.push_back (inMode (makeSteppedList (a + "rate", "Rate", subdivNames(), 3, 21), f.mode));
            p.push_back (inMode (makeRotary (a + "ratefree", "Rate ms", 10.0f, 2000.0f, 200.0f, 21, "ms", true), f.mode));
            p.push_back (inMode (makeCombo (a + "uptovar", "Up To", offOn(), 0, 21, 1), f.mode));
            p.push_back (inMode (intKnob (c + "density", "Density", 0, 7, 1, 22), f.mode));
            if (f.mode == 1)
                p.push_back (inMode (intKnob ("cloudspread", "Spread", 0, 13, 3, 22), 1));
            else
                p.push_back (inMode (intKnob (c + "octave", "Octaves", 0, 3, 1, 22), f.mode));
            p.push_back (inMode (makeSteppedList (c + "velmin", "Vel Min", velNames(), 1, 22), f.mode));
            p.push_back (inMode (makeSteppedList (c + "velmax", "Vel Max", velNames(), 4, 22), f.mode));
            p.push_back (inMode (makeRotary (c + "decay", "Decay", 20.0f, 2000.0f, 200.0f, 22, "ms", true), f.mode));
        }
        p.push_back (inMode (intKnob ("wanderprob", "Wander %", 0, 9, 4, 23), 1));
        p.push_back (inMode (intKnob ("wandermax", "Wander Max", 0, 6, 2, 23), 1));

        // Cluster (scale and per note)
        for (auto [prefix, mode] : { std::pair<const char*, int> { "clust", 1 }, std::pair<const char*, int> { "pnclust", 2 } })
        {
            const juce::String c (prefix);
            p.push_back (inMode (intKnob (c + "lownotes", "Low Notes", 1, 12, 3, 24), mode));
            p.push_back (inMode (intKnob (c + "lowostrt", "Low Oct", 0, 9, 1, 24), mode));
            p.push_back (inMode (intKnob (c + "lowoend", "Low To", 0, 9, 3, 24), mode));
            p.push_back (inMode (intKnob (c + "highnotes", "High Notes", 1, 12, 3, 25), mode));
            p.push_back (inMode (intKnob (c + "highostrt", "High Oct", 0, 9, 4, 25), mode));
            p.push_back (inMode (intKnob (c + "highoend", "High To", 0, 9, 6, 25), mode));
            p.push_back (inMode (makeSteppedList (c + "velmin", "Vel Min", velNames(), 1, 26), mode));
            p.push_back (inMode (makeSteppedList (c + "velmax", "Vel Max", velNames(), 6, 26), mode));
            p.push_back (inMode (makeRotary (c + "decay", "Decay", 20.0f, 5000.0f, 2000.0f, 26, "ms", true), mode));
        }
        return p;
    }

    //=== the three chord modules' knobs ========================================
    // humanize and add length: the first row of each
    void addHumanize (std::vector<ParamSpec>& p)
    {
        p.push_back (intKnob ("humanizetime", "Hum Time", 0, 200, 0, 0, "ms"));
        p.push_back (intKnob ("humanizevel", "Hum Vel", 0, 100, 0, 0));
        p.push_back (makeRotary ("addlength", "Add Length", 0.0f, 20.0f, 0.0f, 0, "s"));
        p.push_back (makeCombo ("discardInput", "Mute Input", offOn(), 0, 0, 1));
    }

    // the animation: the mode and the rate on two rows, Cloud's knobs (and
    // Wander's, Cluster's) on rows of their own, shown while that mode is picked
    void addAnimation (std::vector<ParamSpec>& p, int row, const juce::String& modeId, juce::StringArray modes,
                       const juce::String& a, const juce::String& c, int cloudValue, int wanderValue, int clusterValue,
                       const juce::String& clusterPrefix)
    {
        p.push_back (makeCombo (modeId, "Animate", modes, 0, row, 2).noMod());
        p.push_back (makeCombo ("cloudmuteinput", "Cloud Mute", offOn(), 0, row, 1));
        p.push_back (makeCombo (a + "sync", "Sync", offOn(), 1, row + 1, 1));
        p.push_back (makeSteppedList (a + "rate", "Rate", subdivNames(), 3, row + 1));
        p.push_back (makeRotary (a + "ratefree", "Rate ms", 10.0f, 2000.0f, 200.0f, row + 1, "ms", true));
        p.push_back (makeCombo (a + "uptovar", "Up To", offOn(), 0, row + 1, 1));

        auto when = [&] (ParamSpec spec, int value) { return spec.visibleWhen (modeId, (float) value); };
        const int r = row + 2;
        p.push_back (when (intKnob (c + "density", "Density", 0, 7, 1, r), cloudValue));
        if (c == "cloud")
            p.push_back (when (intKnob ("cloudspread", "Spread", 0, 13, 3, r), cloudValue));
        else
            p.push_back (when (intKnob (c + "octave", "Octaves", 0, 3, 1, r), cloudValue));
        p.push_back (when (makeSteppedList (c + "velmin", "Vel Min", velNames(), 1, r), cloudValue));
        p.push_back (when (makeSteppedList (c + "velmax", "Vel Max", velNames(), 4, r), cloudValue));
        p.push_back (when (makeRotary (c + "decay", "Decay", 20.0f, 2000.0f, 200.0f, r, "ms", true), cloudValue));

        if (wanderValue >= 0)
        {
            p.push_back (when (intKnob ("wanderprob", "Wander %", 0, 9, 4, r + 1), wanderValue));
            p.push_back (when (intKnob ("wandermax", "Wander Max", 0, 6, 2, r + 1), wanderValue));
        }
        if (clusterValue >= 0)
        {
            const juce::String k (clusterPrefix);
            p.push_back (when (intKnob (k + "lownotes", "Low Notes", 1, 12, 3, r + 2), clusterValue));
            p.push_back (when (intKnob (k + "lowostrt", "Low Oct", 0, 9, 1, r + 2), clusterValue));
            p.push_back (when (intKnob (k + "lowoend", "Low To", 0, 9, 3, r + 2), clusterValue));
            p.push_back (when (intKnob (k + "highnotes", "High Notes", 1, 12, 3, r + 3), clusterValue));
            p.push_back (when (intKnob (k + "highostrt", "High Oct", 0, 9, 4, r + 3), clusterValue));
            p.push_back (when (intKnob (k + "highoend", "High To", 0, 9, 6, r + 3), clusterValue));
            p.push_back (when (makeSteppedList (k + "velmin", "Vel Min", velNames(), 1, r + 4), clusterValue));
            p.push_back (when (makeSteppedList (k + "velmax", "Vel Max", velNames(), 6, r + 4), clusterValue));
            p.push_back (when (makeRotary (k + "decay", "Decay", 20.0f, 5000.0f, 2000.0f, r + 4, "ms", true), clusterValue));
        }
    }

    std::vector<ParamSpec> manualParams()
    {
        std::vector<ParamSpec> p;
        addHumanize (p);
        for (int i = 0; i < NA::NUM_ROWS; ++i)
        {
            const juce::String s (i), n (i + 1);
            p.push_back (makeCombo ("enabled" + s, "Row " + n, offOn(), 0, 1 + i, 1));
            p.push_back (intKnob ("semi" + s, "Semi " + n, -12, 12, 0, 1 + i, "st"));
            p.push_back (intKnob ("oct" + s, "Oct " + n, -7, 7, 0, 1 + i));
        }
        addAnimation (p, 10, "manimmode", { "Off", "ReVoice", "Cloud" }, "manim", "mcloud", 2, -1, -1, {});
        return p;
    }

    std::vector<ParamSpec> scaleParams()
    {
        std::vector<ParamSpec> p;
        addHumanize (p);
        p.push_back (makeSteppedList ("root", "Root", noteNames(), 0, 1));
        p.push_back (makeCombo ("scale", "Scale", scaleNames(), 0, 1, 2));
        p.push_back (intKnob ("count", "Count", 0, 7, 3, 1));
        p.push_back (makeSteppedList ("harmonicmode", "Stack", { "2nds", "3rds", "4ths", "5ths", "6ths", "7ths" }, 1, 2));
        p.push_back (makeSteppedList ("harmonicmode2", "Stack 2", { "None", "2nds", "3rds", "4ths", "5ths", "6ths", "7ths" }, 0, 2));
        p.push_back (makeCombo ("invmode", "Inversion", { "Off", "Pick", "Closest", "Drop 2", "Cycle Up", "Cycle Down", "Random" }, 0, 2, 2));
        p.push_back (intKnob ("invpicker", "Inv Pick", 0, 7, 0, 2));
        p.push_back (makeCombo ("randomskip", "Skip", offOn(), 0, 3, 1));
        p.push_back (makeCombo ("lockbass", "Lock Bass", offOn(), 0, 3, 1));
        p.push_back (intKnob ("scaletranspose", "Scale Tr", -12, 12, 0, 3, "steps"));
        p.push_back (makeCombo ("scaletransposeroot", "Tr Root", offOn(), 0, 3, 1));
        addAnimation (p, 10, "animmode", { "Off", "ReVoice", "Wander", "Drift", "Cloud", "Cluster" }, "anim", "cloud", 4, 2, 5, "clust");
        return p;
    }

    std::vector<ParamSpec> perNoteParams()
    {
        std::vector<ParamSpec> p;
        addHumanize (p);
        const auto names = noteNames();
        for (int i = 0; i < 12; ++i)
            p.push_back (makeSteppedList ("pnchord" + juce::String (i), names[i], chordNames(), 0, 1 + i / 4));
        addAnimation (p, 10, "pnanimmode", { "Off", "ReVoice", "Cloud", "Cluster" }, "pnanim", "pncloud", 2, -1, 3, "pnclust");
        return p;
    }
}

//==============================================================================
// one stage of NoteAdder: the plugin itself, with only this stage switched on
//==============================================================================
template <Stage S>
class NoteAdderStageModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.section = ModuleSection::Utility;
        d.accent = kit::accentOf (kPlugin);
        d.sockets = { midiIn ("midiIn", "Midi In"), midiOut ("midiOut", "Midi Out") };

        if constexpr (S == Stage::Filter)
        {
            d.typeId = "plugin.noteadder.filter";
            d.displayName = kit::moduleName (kPlugin, "Note Filter");
            d.sidebarOrder = 400;
            d.description = "NoteAdder's note filter: notes outside the scale pass, are dropped, or play the "
                            "nearest note of the scale instead.";
            d.params = {
                makeSteppedList ("root", "Root", noteNames(), 0, 0),
                makeCombo ("scale", "Scale", scaleNames(), 0, 0, 2),
                makeCombo ("discard", "Outside", { "Allow", "Discard", "Nearest" }, 0, 1, 2)
            };
        }
        else if constexpr (S == Stage::Manual)
        {
            d.typeId = "plugin.noteadder.manual";
            d.displayName = kit::moduleName (kPlugin, "Manual Chord");
            d.sidebarOrder = 401;
            d.description = "NoteAdder's Manual chord: up to seven intervals (semitones and octaves) added to "
                            "every note, with humanize, add length and the ReVoice and Cloud animations.";
            d.params = manualParams();
        }
        else if constexpr (S == Stage::Scale)
        {
            d.typeId = "plugin.noteadder.scale";
            d.displayName = kit::moduleName (kPlugin, "Scale Chord");
            d.sidebarOrder = 402;
            d.description = "NoteAdder's Scale chord: harmonies stacked in a scale, with inversions, lock bass, "
                            "random skip and scale transpose, humanize, add length and the ReVoice, Wander, "
                            "Drift, Cloud and Cluster animations.";
            d.params = scaleParams();
        }
        else if constexpr (S == Stage::PerNote)
        {
            d.typeId = "plugin.noteadder.pernote";
            d.displayName = kit::moduleName (kPlugin, "Per Note Chord");
            d.sidebarOrder = 403;
            d.description = "NoteAdder's Per Note chords: a chord for every pitch class, Custom ones typed below "
                            "(semitones from the note, e.g. 0 4 7 11), with humanize, add length and the "
                            "ReVoice, Cloud and Cluster animations.";
            d.params = perNoteParams();
        }
        else if constexpr (S == Stage::Chord)
        {
            d.typeId = "plugin.noteadder.chord";
            d.displayName = kit::moduleName (kPlugin, "Chord");
            d.sidebarOrder = 409;
            d.listedInSidebar = false;   // the earlier all-in-one chord, for old patches
            d.description = "NoteAdder's note adder: Manual rows, Scale harmonies or a chord per note, with "
                            "humanize, add length and the animation modes. Custom per-note chords are typed "
                            "below (semitones from the note, e.g. 0 4 7 11).";
            d.params = chordParams();
        }
        else
        {
            d.typeId = "plugin.noteadder.transpose";
            d.displayName = kit::moduleName (kPlugin, "Transpose");
            d.sidebarOrder = 404;
            d.description = "NoteAdder's global transpose: every note moves by Transpose semitones, and held "
                            "notes are re-voiced when it changes.";
            d.params = { makeRotary ("globaltranspose", "Transpose", -12.0f, 12.0f, 0.0f, 0, "st", false, 1.0f) };
        }
        return d;
    }

    NoteAdderStageModule()
    {
        auto p = createOriginal (kPlugin, 48000.0);
        na.reset (dynamic_cast<NA*> (p.get()));
        if (na != nullptr)
            p.release();
        if (na != nullptr)
            na->setPlayHead (&playHead);
        events.reserve (1024);
    }

    ~NoteAdderStageModule() override
    {
        if (na != nullptr)
            na->setPlayHead (nullptr);
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool midiSourceReplacesInput() const override { return true; }
    bool isMidiNoteDriver() const override { return true; }
    bool isMidiProcessor() const override { return true; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        if (na == nullptr)
            return;

        // the knobs this stage owns, and what the other stages own held still
        links.clear();
        forced.clear();
        const auto& d = getDescriptor();
        for (int i = 0; i < (int) d.params.size(); ++i)
            if (auto* prm = kit::findParam (*na, d.params[(size_t) i].id))
                links.push_back ({ prm, i, -1.0f });
        for (auto& [id, value] : forcedValues())
            if (auto* prm = kit::findParam (*na, id))
                forced.push_back ({ prm, prm->convertTo0to1 (value) });

        {
            const juce::ScopedLock sl (na->getCallbackLock());
            na->releaseResources();
            na->setRateAndBufferSizeDetails (sr, kBlock);
            na->prepareToPlay (sr, kBlock);
        }
        audio.setSize (2, kBlock);
        inBuffer.ensureSize (2048);
        pos = 0;
        events.clear();
        counter = 0;
    }

    void reset() override
    {
        inBuffer.clear();
        inBuffer.addEvent (juce::MidiMessage::allNotesOff (1), pos);
    }

    int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent*, int) override
    {
        inBuffer.addEvent (in.isOn ? juce::MidiMessage::noteOn (1, in.note, juce::jlimit (0.0f, 1.0f, in.velocity))
                                   : juce::MidiMessage::noteOff (1, in.note), pos);
        return 0;
    }

    void midiAllNotesOff() override { inBuffer.addEvent (juce::MidiMessage::allNotesOff (1), pos); }

    // once per sample: hand on what NoteAdder played for this sample, and
    // every 32 samples run it on what came in
    int midiDriverAdvanceEvents (double, MidiDriverEvent* out, int maxOut) override
    {
        int n = 0;
        for (size_t i = 0; i < events.size();)
        {
            if (events[i].due <= counter && n < maxOut)
            {
                out[n++] = events[i].ev;
                events.erase (events.begin() + (long) i);
                continue;
            }
            ++i;
        }
        ++counter;

        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
        return n;
    }

    juce::String saveCustomState() const override
    {
        if (na == nullptr)
            return {};
        const juce::ScopedReadLock lk (na->pnCustomLock);
        juce::StringArray s;
        for (auto& c : na->pnCustomStrings)
            s.add (c);
        return s.joinIntoString ("|");
    }

    void loadCustomState (const juce::String& state) override
    {
        if (na == nullptr)
            return;
        juce::StringArray s;
        s.addTokens (state, "|", {});
        const juce::ScopedWriteLock lk (na->pnCustomLock);
        for (int i = 0; i < 12; ++i)
            na->pnCustomStrings[(size_t) i] = i < s.size() ? s[i] : juce::String();
    }

    void setCustomChord (int pitchClass, const juce::String& text)
    {
        if (na == nullptr || pitchClass < 0 || pitchClass >= 12)
            return;
        const juce::ScopedWriteLock lk (na->pnCustomLock);
        na->pnCustomStrings[(size_t) pitchClass] = text;
    }
    juce::String customChord (int pitchClass) const
    {
        if (na == nullptr || pitchClass < 0 || pitchClass >= 12)
            return {};
        const juce::ScopedReadLock lk (na->pnCustomLock);
        return na->pnCustomStrings[(size_t) pitchClass];
    }

    int extraContentHeight() const override { return S == Stage::Chord ? 52 : S == Stage::PerNote ? 72 : 0; }
    std::unique_ptr<juce::Component> createExtraContentComponent() override;

private:
    static constexpr int kBlock = 32;

    std::vector<std::pair<juce::String, float>> forcedValues() const
    {
        if constexpr (S == Stage::Filter)
            return { { "mode", 1.0f }, { "count", 0.0f }, { "harmonicmode2", 0.0f }, { "invmode", 0.0f },
                     { "animmode", 0.0f }, { "globaltranspose", 0.0f }, { "scaletranspose", 0.0f },
                     { "scaletransposeroot", 0.0f }, { "discardInput", 0.0f }, { "humanizetime", 0.0f },
                     { "humanizevel", 0.0f }, { "addlength", 0.0f }, { "randomskip", 0.0f }, { "lockbass", 0.0f } };
        else if constexpr (S == Stage::Chord)
            return { { "discard", 0.0f }, { "globaltranspose", 0.0f } };
        else if constexpr (S == Stage::Manual)
            return { { "mode", 0.0f }, { "discard", 0.0f }, { "globaltranspose", 0.0f } };
        else if constexpr (S == Stage::Scale)
            return { { "mode", 1.0f }, { "discard", 0.0f }, { "globaltranspose", 0.0f } };
        else if constexpr (S == Stage::PerNote)
            return { { "mode", 2.0f }, { "discard", 0.0f }, { "globaltranspose", 0.0f } };
        else
        {
            std::vector<std::pair<juce::String, float>> v { { "mode", 0.0f }, { "manimmode", 0.0f }, { "humanizetime", 0.0f },
                                                            { "humanizevel", 0.0f }, { "addlength", 0.0f },
                                                            { "discardInput", 0.0f } };
            for (int i = 0; i < NA::NUM_ROWS; ++i)
                v.push_back ({ "enabled" + juce::String (i), 0.0f });
            return v;
        }
    }

    void runBlock()
    {
        // knobs (with their cables) into the plugin's own parameters
        for (auto& l : links)
        {
            const float raw = param (l.knob);   // unmoved knob: nothing to convert or send
            if (juce::exactlyEqual (raw, l.raw)) continue;
            l.raw = raw;
            const float v = l.param->convertTo0to1 (raw);
            if (v != l.last)
            {
                l.param->setValue (v);
                l.last = v;
            }
        }
        for (auto& f : forced)
            if (f.param->getValue() != f.value)
                f.param->setValue (f.value);

        auto info = juce::AudioPlayHead::PositionInfo();
        info.setBpm (tempoBpm);
        info.setIsPlaying (hostTransport.isPlaying);
        playHead.info = info;

        {
            const juce::ScopedTryLock sl (na->getCallbackLock());
            if (! sl.isLocked())
                return;
            audio.clear();
            na->processBlock (audio, inBuffer);
        }

        // NoteAdder's output: played one block later, each at its own sample
        for (const auto meta : inBuffer)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff())
                continue;
            MidiDriverEvent ev;
            ev.note = m.getNoteNumber();
            ev.isOn = m.isNoteOn();
            ev.velocity = m.getFloatVelocity();
            if (events.size() < events.capacity())
                events.push_back ({ counter + meta.samplePosition, ev });
        }
        inBuffer.clear();
    }

    struct PlayHeadImpl : public juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    };
    struct Link { juce::RangedAudioParameter* param; int knob; float last; float raw = std::numeric_limits<float>::quiet_NaN(); };
    struct Forced { juce::RangedAudioParameter* param; float value; };
    struct Pending { juce::int64 due; MidiDriverEvent ev; };

    std::unique_ptr<NA> na;
    PlayHeadImpl playHead;
    std::vector<Link> links;
    std::vector<Forced> forced;
    juce::AudioBuffer<float> audio;
    juce::MidiBuffer inBuffer;
    std::vector<Pending> events;
    juce::int64 counter { 0 };
    int pos { 0 };
};

//==============================================================================
// the Chord stage's custom per-note chords: twelve little text fields
namespace
{
    template <typename Module>
    class CustomChordEditor final : public juce::Component
    {
    public:
        CustomChordEditor (Module& m, int columns) : module (&m), cols (columns)
        {
            const auto names = noteNames();
            for (int i = 0; i < 12; ++i)
            {
                auto& e = fields[(size_t) i];
                e.setTextToShowWhenEmpty (names[i], juce::Colours::white.withAlpha (0.35f));
                e.setText (m.customChord (i), false);
                e.setFont (juce::FontOptions (11.0f));
                e.setJustification (juce::Justification::centred);
                e.setTooltip (names[i] + ": a Custom chord, semitones from the note (e.g. 0 4 7 11)");
                e.onTextChange = [this, i] { if (auto* mm = dynamic_cast<Module*> (module.get())) mm->setCustomChord (i, fields[(size_t) i].getText()); };
                addAndMakeVisible (e);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2);
            const int rows = 12 / cols, w = r.getWidth() / cols, h = r.getHeight() / rows;
            for (int i = 0; i < 12; ++i)
                fields[(size_t) i].setBounds (r.getX() + (i % cols) * w + 1, r.getY() + (i / cols) * h + 1, w - 2, h - 2);
        }

    private:
        juce::WeakReference<SynthModule> module;
        int cols;
        std::array<TapToTypeEditor, 12> fields;   // focus only when tapped
    };
}

template <Stage S>
std::unique_ptr<juce::Component> NoteAdderStageModule<S>::createExtraContentComponent()
{
    if constexpr (S == Stage::Chord)
        return std::make_unique<CustomChordEditor<NoteAdderStageModule<S>>> (*this, 6);
    else if constexpr (S == Stage::PerNote)
        return std::make_unique<CustomChordEditor<NoteAdderStageModule<S>>> (*this, 4);   // three rows of four
    else
        return nullptr;
}

using NoteAdderFilterModule    = NoteAdderStageModule<Stage::Filter>;
using NoteAdderChordModule     = NoteAdderStageModule<Stage::Chord>;
using NoteAdderTransposeModule = NoteAdderStageModule<Stage::Transpose>;
using NoteAdderManualModule    = NoteAdderStageModule<Stage::Manual>;
using NoteAdderScaleModule     = NoteAdderStageModule<Stage::Scale>;
using NoteAdderPerNoteModule   = NoteAdderStageModule<Stage::PerNote>;

AQUANODE_REGISTER_MODULE (NoteAdderFilterModule,    NoteAdderFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (NoteAdderChordModule,     NoteAdderChordModule::descriptor)
AQUANODE_REGISTER_MODULE (NoteAdderTransposeModule, NoteAdderTransposeModule::descriptor)
AQUANODE_REGISTER_MODULE (NoteAdderManualModule,    NoteAdderManualModule::descriptor)
AQUANODE_REGISTER_MODULE (NoteAdderScaleModule,     NoteAdderScaleModule::descriptor)
AQUANODE_REGISTER_MODULE (NoteAdderPerNoteModule,   NoteAdderPerNoteModule::descriptor)

//==============================================================================
namespace aquanode::plugins::noteadder
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        // the chord of the preset's mode (the other two are in the sidebar)
        const int mode = juce::jlimit (0, 2, (int) kit::param (original, "mode", 0.0f));
        const char* chordType = mode == 0 ? "plugin.noteadder.manual"
                              : mode == 1 ? "plugin.noteadder.scale" : "plugin.noteadder.pernote";
        const int filter = b.add ("plugin.noteadder.filter", 0, 0);
        const int chord = b.add (chordType, 290, 0);
        const int transpose = b.add ("plugin.noteadder.transpose", 580, 0);
        b.copy (filter, original);
        b.copy (chord, original);
        b.copy (transpose, original);

        // the per-note custom chords
        if (auto* na = dynamic_cast<NA*> (&original))
            if (auto* m = dynamic_cast<NoteAdderPerNoteModule*> (b.dsp (chord)))
            {
                const juce::ScopedReadLock lk (na->pnCustomLock);
                for (int i = 0; i < 12; ++i)
                    m->setCustomChord (i, na->pnCustomStrings[(size_t) i]);
            }

        // the filter only works on Scale mode's scale - in the other modes
        // NoteAdder lets every note through
        if (mode != 1)
            b.set (filter, "discard", 0.0f);

        b.cable (filter, "midiOut", chord, "midiIn");
        b.cable (chord, "midiOut", transpose, "midiIn");

        PatchPorts ports;
        ports.name = "NoteAdder";
        ports.midiIn = { { filter, "midiIn" } };
        ports.midiOut = { { transpose, "midiOut" } };
        return ports;
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* na = dynamic_cast<NA*> (&original);
        return na != nullptr && na->loadPresetFile (file);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
