#pragma once
//==============================================================================
// Tidebreeze - the processor
//
// 32 voices of up to two oscillators each (Single / Double mode). Each
// oscillator reads a wave (High or Low by velocity), through its own filter
// and amp with their envelopes and two LFOs; a common pitch EG; portamento,
// mono / legato and hold. MIDI can go through the arpeggiator first. The
// voices' sum runs through three insert effects in series, then the two
// master effects (sends), the master EQ and the program volume.
//
// Waves are rendered from their recipes on the message thread when a
// program asks for them (TbWaves); the audio thread picks the rendered ones
// up at note-on.
//==============================================================================
#include "TbParams.h"
#include "TbReader.h"
#include "TbArp.h"
#include "TbPrograms.h"
#include "TbWaveBank.h"

class TidebreezeAudioProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::AsyncUpdater
{
public:
    TidebreezeAudioProcessor();
    ~TidebreezeAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tidebreeze"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    // the program
    juce::String programName { "Init Preset" }, programCategory { "Pads" };
    int currentProgram = 0;
    void loadFactoryProgram (int index);
    void rollProgram (int category, juce::uint32 seed = 0);   // a random program of a category
    void initProgram();

    // every wave a program can play: the factory waves and the 16 user slots
    // (rolled waves, stored as archetype + seed, and loaded audio files,
    // stored as their audio) - see TbWaveBank.h
    tb::WaveBank bank;
    juce::String waveName (int choice) const { return bank.waveName (choice); }
    // rolls a new wave of an archetype into a user slot (-1: the next free /
    // oldest one) and returns its wave choice index
    int rollWave (int archetype, juce::uint32 seed = 0, int slot = -1) { return bank.rollWave (archetype, seed, slot); }
    // loads an audio file into a user slot (-1: the next free / oldest one);
    // its wave choice index, or -1 and the reason
    int loadUserWave (const juce::File& file, int slot, juce::String& error) { return bank.loadFile (file, slot, error); }
    void setUserWaveSettings (int slot, const tb::SampleSettings& s) { bank.setSampleSettings (slot, s); }
    void clearUserWave (int slot) { bank.clearSlot (slot); ensureProgramWaves(); }
    // waves render on a background thread: request one, peek whether it is
    // there yet. ensureProgramWaves requests every wave the program uses
    void requestWave (int choice) { bank.requestWave (choice); }
    tb::WavePtr peekWave (int choice) const { return bank.peekWave (choice); }
    void ensureProgramWaves();
    bool wavesPending() const { return bank.pending(); }
    bool waitForWaves (int timeoutMs) { return bank.waitFor (timeoutMs); }   // offline renders and tests

    // for the editor: what the oscillators play right now
    float meterLevel() const { return meter.load(); }
    double currentTempo() const { return tempo.load(); }
    juce::MidiKeyboardState keyboardState;

    std::function<void()> onProgramChanged;      // the editor refreshes

private:
    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void syncEffects();
    void resetToDefaults();
    tb::ProgramTarget makeTarget();
    bool arpWasOn = false;
    int buildingProgram = 0;                     // > 0 while a program is being put together

    struct OscVoice
    {
        tb::WaveReader reader;
        tb::VoiceFilter filter;
        tb::Envelope feg, aeg;
        tb::Lfo lfo[2];
        float level = 1.0f, panL = 0.707f, panR = 0.707f;
        float gain = 0.0f, gainTarget = 0.0f;
        double inc = 1.0;
        int delaySamples = 0;
        bool active = false;
    };
    struct Voice
    {
        int note = -1; float velocity = 0.0f;
        bool active = false, held = false, sustained = false;
        juce::uint64 age = 0;
        float glideNote = 60.0f, glideTarget = 60.0f;
        tb::Envelope peg;
        OscVoice osc[2];
    };

    void startVoice (Voice& v, int note, float velocity, bool legatoRetrigger);
    void releaseVoice (Voice& v);
    void renderVoices (float* L, float* R, int n);
    void handleNoteOn (int note, float velocity);
    void handleNoteOff (int note);

    // a snapshot of the parameters the audio thread reads (refreshed per block)
    struct OscParams
    {
        int hiWave, loWave, velSw, hiLvl, loLvl, octave, transpose, tune, delayMs, velLo, velHi, pSlope;
        bool hiRev, hiOfs, loRev, loOfs;
        int pegInt, l1Pitch, l2Pitch;
        int fType, fA, fRes, fB, fTrim, fKt, fEgA, fEgB, fVelA, fL1A, fL2A;
        int feg[9], aeg[8];
        int aLvl, aPan, aVel, aL1, aL2;
        struct L { int wave, freq, div, fade, delay, ofs; bool sync, key; } lfo[2];
    };
    struct FxParams { int type, p[5], mix, send, ret; bool on; };
    struct Params
    {
        int mode, voiceMode, porta, bend, volume;
        bool legato, hold;
        int peg[6];
        int rtCutoff, rtReso, rtEgInt, rtRelease;
        OscParams osc[2];
        bool arpOn, arpLatch;
        int arpPat, arpRes, arpOct, arpGate, arpVel, arpSwing, arpTempo;
        FxParams fx[5];
        int eqLo, eqMid, eqMidHz, eqHi;
    } P {};
    // the raw parameter values behind each field of P, read once per block
    struct Binding { std::atomic<float>* raw; int* i; bool* b; };
    std::vector<Binding> bindings;
    template <typename F> void visitParams (Params& p, F&& f);
    void readParams();

    std::array<Voice, 32> voices;
    juce::uint64 noteCounter = 0;
    std::vector<int> heldNotes;                  // mono / legato note stack
    float bendSemis = 0.0f;
    bool sustainPedal = false;
    float freePhase[2][2] {};

    tb::Arp arp;
    juce::MidiBuffer arpOut;

    // effects
    struct FxSlot
    {
        int type = 0;
        std::unique_ptr<tb::Effect> fx;
        juce::String id;                         // "ifx1" / "mfx2"
    };
    std::array<FxSlot, 5> fxSlots;               // ifx1..3, mfx1..2
    juce::SpinLock fxLock;
    juce::AudioBuffer<float> sendBuf, mfxBuf;
    tb::Svf eqLo[2], eqMid[2], eqHi[2];

    double sr = 44100.0;
    std::atomic<double> tempo { 120.0 };
    std::atomic<float> meter { 0.0f };
    std::atomic<bool> wavesWanted { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TidebreezeAudioProcessor)
};
