#pragma once
//==============================================================================
// Fruit DAW - a small FL Studio-style song workspace on one card:
//
//   synth lanes 01-99     each holds one synth (a sound generation module or
//                         a synth plugin) bound to it - no cables
//   piano roll            patterns 01-99, each playing to a synth slot of
//                         its choosing; the keys play the slot shown there
//   playlist              99 tracks of pattern clips (moved, trimmed at both
//                         ends) and Knob Record automation clips (splines)
//   mixer                 inserts 01-99 (one per synth slot) and the master
//                         00, each with up to ten effects, a volume and a
//                         target: 00 the master; any other track also gets it
//                         as a sidechain ("sidechain to this track")
//
// Underneath it is an Aquanode Modular engine of its own, which Fruit DAW
// builds and wires itself: per slot a note feed into the synth, per insert an
// input junction, the effects, a sidechain bus into the effects' second
// inputs, a volume and a meter, and the master into Audio Out.
//==============================================================================
#include <JuceHeader.h>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "../../Engine/PluginProcessor.h"
#include "../Host/HostedPlugin.h"
#include "FruitModules.h"

namespace fruit
{
    constexpr int kSlots = 100;        // 1..99 used; 0 is the master insert
    constexpr int kFxPerInsert = 10;
    constexpr int kSends = 9;          // where an insert goes: up to nine tracks, each at its own level

    struct Note { double start = 0, length = 0.25; int pitch = 60; float velocity = 0.8f; };

    struct Pattern
    {
        std::vector<Note> notes;
        double lengthBeats = 16.0;     // four bars
        int slot = 1;                  // the synth slot it plays
    };

    struct Clip                        // a pattern, or a sample, on the playlist
    {
        int track = 1;
        double start = 0, length = 16, offset = 0;   // beats; offset: where in the pattern / sample it starts (trimmed left)
        int pattern = 1;
        int sample = 0;                // > 0: an audio clip of that sample (1-based), at its own speed
    };

    struct Sample                      // an audio file on the playlist ("Sample 1" ...)
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double rate = 48000.0;
        juce::String name;
        double bpm = 0.0;              // detected tempo; 0: none found
        int insert = 0;                // the mixer insert it plays through (00 the master)
        juce::String encoded;          // FLAC as base64, made once, for the state
        // Stretch: it follows the song's tempo at its own pitch. `stretched` is the audio stretched
        // for the song tempo stretchedBpm, by the Rubber Band Library; made again when the tempo moves.
        bool stretch = false;
        std::shared_ptr<const juce::AudioBuffer<float>> stretched;
        double stretchedBpm = 0.0;
        const juce::AudioBuffer<float>* playing() const { return stretch && stretched != nullptr ? stretched.get() : audio.get(); }
        double seconds() const { return audio != nullptr && rate > 0.0 ? audio->getNumSamples() / rate : 0.0; }
        // how long it is on the playlist at that song tempo
        double beats (double songBpm) const { return seconds() * (stretch && bpm > 0.0 ? bpm : songBpm) / 60.0; }
    };

    struct AutoClip                    // Knob Record: a knob's movement over time
    {
        int track = 1;
        double start = 0, length = 16;
        int moduleId = -1;             // the inner engine's module
        int paramIndex = -1;           // its knob (a native module's parameter) ...
        int pluginParam = -1;          // ... or a plugin card's parameter
        juce::String label;
        std::vector<juce::Point<float>> points;   // x 0..1 across the clip, y 0..1 (the knob's range); a spline through them
        float valueAt (float x) const;
    };

    struct Song
    {
        std::array<Pattern, kSlots> patterns;
        std::vector<Clip> clips;
        std::vector<AutoClip> autos;
        std::vector<Sample> samples;
        // the red stripe on the playlist's ruler: the stretch that Play Song loops, in beats;
        // none (loopEnd <= loopStart): the whole song
        double loopStart = 0.0, loopEnd = 0.0;
        void loopRange (double& from, double& to) const
        {
            if (loopEnd > loopStart + 0.01) { from = loopStart; to = loopEnd; }
            else { from = 0.0; to = lengthBeats(); }
        }
        double lengthBeats() const;    // to the end of the last clip, whole bars, at least one bar
    };

    struct Slot { int synthId = -1, feedId = -1; juce::String typeId, name; };

    struct Insert
    {
        int inId = -1, scId = -1, volId = -1, meterId = -1;
        int audioFeedId = -1;          // the playlist's samples that go to this insert
        std::array<int, kFxPerInsert> fx;
        // Where it goes, as in FL: each send feeds that track's input at its level
        // (through the track's effects and fader), and the track's sidechain bus
        // always hears it in full - so a send at 0 % is "sidechain to this track".
        // New inserts go to the master (00) at 100 %.
        struct Send { int target = -1; float level = 1.0f; int gainId = -1; };
        std::array<Send, kSends> sends;
        Insert() { fx.fill (-1); sends[0].target = 0; }
        bool exists() const { return inId >= 0; }
    };

    struct LastKnob { int moduleId = -1, paramIndex = -1, pluginParam = -1; juce::String label; };
}

class FruitProcessor : public juce::AudioProcessor,
                       public aquanode::plugins::SongPlayer,
                       public aquanode::TempoRequester,
                       public aquanode::plugins::LinkedGroupPlugin
{
public:
    FruitProcessor();
    ~FruitProcessor() override;

    //=== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Fruit DAW"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //=== SongPlayer (Render, Play All) =========================================
    double songBeats() const override;
    void playSongFromStart (bool play) override;

    //=== the model (message thread) ============================================
    AquanodeModularAudioProcessor& getEngine() { return *engine; }
    fruit::Song song;                  // edit, then call songChanged()
    void songChanged();                // publishes the song to the audio thread
    std::array<fruit::Slot, fruit::kSlots> slots;
    std::array<fruit::Insert, fruit::kSlots> inserts;

    // synth slots
    bool loadSynth (int slot, const juce::String& typeId);
    void clearSlot (int slot);
    // effects
    bool loadEffect (int insert, int index, const juce::String& typeId);
    void clearEffect (int insert, int index);
    // samples on the playlist. addSample: decodes the stream, finds its tempo, and puts a clip of
    // it on the first empty track; returns its number (0: not an audio file)
    int addSample (std::unique_ptr<juce::InputStream> stream, const juce::String& name);
    int addSample (std::shared_ptr<const juce::AudioBuffer<float>> audio, double rate, const juce::String& name);
    void removeSample (int number);                 // and its clips
    void setSampleInsert (int number, int insert);
    void setSampleBpm (int number, double bpm);     // (halve / double what was found)
    void setSampleStretch (int number, bool on);    // its clips keep their place and what they play
    bool updateStretches();                         // stretched samples made again for the tempo now; true: something changed
    // the patch's tempo (the BPM box) set from here; a DAW's tempo stays the DAW's
    void setSongTempo (double bpm) { tempoRequest.store (bpm); }
    double takeTempoRequest() override { return tempoRequest.exchange (0.0); }
    // the loop region (or the whole song) rendered as it sounds when it loops: the second of
    // two passes, so the tails of its end are in its start. Message thread; empty: nothing to play
    juce::AudioBuffer<float> renderRegion (double& rate);
    double currentBpm() const { return lastBpm.load(); }
    static double detectBpm (const juce::AudioBuffer<float>& audio, double rate);   // 0: no steady pulse found

    // sends: target -1 none, 0 the master, 1..99 a track. false: refused (itself, a loop back, twice the same)
    bool setSendTarget (int insert, int send, int target);
    void setSendLevel (int insert, int send, float level);     // 0..1
    bool wouldLoop (int insert, int target) const;
    void setTarget (int insert, int target);                   // 6.0's "sidechain to": a send to it at 0 %
    float getVolume (int insert) const;
    void setVolume (int insert, float level);
    float getPan (int insert) const;                          // -1 (left) .. 1 (right)
    void setPan (int insert, float pan);
    bool isInverted (int insert) const;                       // polarity
    void setInverted (int insert, bool on);
    int insertLatency (int insert) const;                     // its effects', in samples
    bool isInsertUsed (int insert) const;                     // a synth, effects, samples or a send lead to it
    void releaseIfUnused (int insert);                        // an insert nothing uses goes back to unused
    float meterLevel (int insert) const;
    juce::String insertName (int insert) const;
    aquanode::SynthModule* moduleFor (int moduleId) const;

    // what can go where: (typeId, name, is a plugin card)
    struct Choice { juce::String typeId, name; bool plugin; };
    static std::vector<Choice> synthChoices();
    static std::vector<Choice> effectChoices();

    // transport (message thread asks, the audio thread plays)
    enum class Mode { stopped, pattern, song };
    void playPattern (int pattern);    // loops one pattern (stops the song)
    void playSong();                   // the playlist from its start (stops a pattern)
    void stop();
    Mode getMode() const { return (Mode) mode.load(); }
    double getPosition() const { return position.load(); }   // beats, in the song or the pattern
    std::atomic<int> keysSlot { 1 };   // the synth slot the keys play (the piano roll's)
    std::atomic<int> playingPattern { 1 };

    // Knob Record: the last knob turned on any synth or effect (message thread)
    void scanForTurnedKnobs();
    fruit::LastKnob lastKnob;
    void addKnobRecordClip (int track, double start);   // a clip for lastKnob, flat at its current value

    std::atomic<int> uiVersion { 0 };  // bumped when the model changes (the panel refreshes)

    // what the panels show, shared by them (one panel, or the patch version's four cards)
    struct View { int pattern = 1, insert = 1, track = 1, sample = 0; } view;    // sample > 0: the playlist's brush is that sample
    // the patch version: its four cards find each other by this key (empty: one card)
    juce::String splitKey;
    // ... and which window the card that owns it shows: the mixer (it has the Audio Out; the channel
    // rack card passes its Midi In on through sendMidiTo), or the channel rack in patches from 6.1's first build
    int splitPart = 1;
    juce::String linkGroup() const override { return splitKey; }
    void setSplit (const juce::String& key, int part);
    // audio thread: MIDI from the channel rack card to the Fruit DAW with that key
    static void sendMidiTo (const juce::String& key, const juce::MidiMessage&);
    // Record in the piano roll: what is played on the keys while a pattern plays goes into it
    std::atomic<bool> recording { false };
    void collectRecorded();            // message thread: recorded notes into the pattern

private:
    std::unique_ptr<AquanodeModularAudioProcessor> engine;
    int masterOutId = -1;
    juce::CriticalSection processLock;     // held while the engine's modules are removed or reloaded
    double sr = 48000.0;
    int maxBlock = 512;

    // the song as the audio thread sees it
    std::shared_ptr<const fruit::Song> playing;
    std::vector<std::shared_ptr<const fruit::Song>> retired;   // freed on the message thread
    std::array<std::atomic<FruitNoteFeed*>, fruit::kSlots> feeds;

    std::atomic<int> mode { (int) Mode::stopped };
    std::atomic<double> position { 0.0 };
    std::atomic<int> command { 0 };        // 1 play pattern, 2 play song, 3 stop (from the message thread)
    struct Active { int slot = 0, pitch = 0; double offBeat = 0; };
    std::array<Active, 512> active;
    int numActive = 0;
    std::array<std::array<bool, 128>, fruit::kSlots> liveHeld {};
    // (6.9.14) MPE from the keys: each member channel's note and expression
    std::array<int, 17> mpeNote { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<float, 17> mpeBend {}, mpePress { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
                          mpeSlide { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    int liveSlot = 1;
    // the playlist's samples: one stereo bus per mixer insert that has any
    void resolveSamples();                           // message thread, under processLock
    void mixSamples (const fruit::Song&, double b0, double L, double bps, int n, double S);
    std::array<std::atomic<FruitAudioFeed*>, fruit::kSlots> audioFeeds {};
    std::array<int, fruit::kSlots> sampleBus {};     // insert -> bus index, -1 none
    std::vector<int> busInsert;                      // bus -> insert
    juce::AudioBuffer<float> sampleMix;
    std::atomic<double> lastBpm { 120.0 };
    std::atomic<double> tempoRequest { 0.0 };
    bool wholeSong = false;                          // Render / Play All: from the start to the end, no loop region
    std::array<juce::MidiMessage, 64> sentMidi;      // from the channel rack card, for the next block
    int numSentMidi = 0;
    // Record (audio thread writes, the message thread collects)
    std::array<double, 128> recStart {};
    std::array<float, 128> recVel {};
    juce::AbstractFifo recFifo { 512 };
    std::array<fruit::Note, 512> recBuf {};

    // automation targets resolved on the message thread (under processLock)
    struct AutoTarget { aquanode::SynthModule* dsp = nullptr; int index = -1; float lo = 0, hi = 1; bool log = false;
                        juce::AudioProcessorParameter* param = nullptr; };
    std::vector<AutoTarget> autoTargets;   // one per song.autos entry, same order
    void resolveAutomation();

    // knob scanning
    std::map<std::pair<int, int>, float> knobSnapshot;   // (module, param) -> value

    // building the engine
    int add (const juce::String& typeId, int x, int y);
    void ensureInsert (int insert);
    void rewire (int insert);              // the cables of one insert (and what feeds it)
    void rewireAll();
    void removeModuleLocked (int id);
    void connectSynth (int slot);
    void removeCablesTouching (int moduleId);
    static juce::String mainIn (const aquanode::ModuleDescriptor&);
    static juce::String sideIn (const aquanode::ModuleDescriptor&);
    static juce::String mainOut (const aquanode::ModuleDescriptor&);
    static juce::String midiInOf (const aquanode::ModuleDescriptor&);

    // sequencing (audio thread)
    void sequence (const fruit::Song& s, double b0, double b1, double beatsPerSample, int startSample, int numSamples);
    void emitClip (const fruit::Pattern& pat, double clipStart, double clipEnd, double offset,
                   double b0, double b1, double beatsPerSample, int startSample);
    void allNotesOff (int sampleOffset);
    void pushNote (int slot, int offset, int pitch, float vel, bool on);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FruitProcessor)
};
