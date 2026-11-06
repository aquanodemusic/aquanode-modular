#pragma once
//==============================================================================
// Cove Groove - a hardgroove loop generator: eight synthesized drum lanes, a
// pattern of up to four bars written by GENERATE (or by hand), the top bus's
// phaser and moving band-pass, a tempo delay and a master drive.
//
// It plays with the DAW's transport (locked to its bars) when the DAW plays;
// otherwise RUN plays it at its own Tempo, or at the patch's tempo (Clock).
// Keys C2..G2 (MIDI 36..43) play the eight lanes by hand, C3..G3 (48..55) the
// same with their second note (the conga's high note, the bass an octave up).
// Any lane can play a sound file of your own instead (its Sound: Sample),
// shaped by a volume curve; the files are kept in the state.
//==============================================================================
#include <JuceHeader.h>
#include "CgEngine.h"
#include "CgSampleIO.h"

namespace covegroove
{
    class CoveGrooveProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        CoveGrooveProcessor();
        ~CoveGrooveProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }
        const juce::String getName() const override { return "Cove Groove"; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 2.0; }

        int getNumPrograms() override;
        int getCurrentProgram() override { return currentProgram; }
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState apvts;
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

        //=== the pattern (message thread) ====================================
        Pattern getPattern() const;
        void setPattern (const Pattern& p);
        void setStep (int lane, int step, Step s);

        // GENERATE: every lane (laneMask 0xff) or one; a fresh seed each time
        void generate (unsigned laneMask = 0xffu);
        // the Bars setting: a longer pattern repeats what is there
        void setBars (int bars);

        // the presets' groups (Hardgroove, Analog, Hybrid, Kit, Warehouse, Mixed)
        static juce::String getProgramCategory (int index);

        // KIT: a sound for every lane at once (the cymbal and FX lanes get new
        // steps where their sound plays differently)
        static int getNumKits();
        static juce::String getKitName (int index);
        void applyKit (int index);

        // the effect presets: the phaser, band-pass, delay and drive alone
        static int getNumFxPresets();
        static juce::String getFxPresetName (int index);
        void applyFxPreset (int index);

        // the dice: what they throw
        enum class Random { Everything, Groove, Kit, Sounds, Effects, Mix, Preset };
        void randomize (Random what);

        // .covegroove files (the whole state)
        bool savePreset (const juce::File& f);
        bool loadPreset (const juce::File& f);

        //=== the lanes' own samples (the Sample sound; message thread) =========
        struct LaneSample
        {
            std::shared_ptr<const SampleData> data;    // null: none loaded
            Curve curve;
            juce::String name;
        };
        // a sound file into a lane, which then plays it (false: nothing readable)
        bool loadLaneSample (int lane, const juce::File& file);
        void setLaneSample (int lane, std::shared_ptr<const SampleData> data, const juce::String& name);
        void clearLaneSample (int lane);
        void setLaneCurve (int lane, const Curve& c);
        LaneSample getLaneSample (int lane) const;
        // the lane plays once (the sample panel's PLAY)
        void audition (int lane) { auditionMask.fetch_or (1u << juce::jlimit (0, kNumLanes - 1, lane)); }
        std::atomic<int> sampleVersion { 0 };          // bumped when a sample or a curve changes

        //=== for the editor ==================================================
        std::atomic<int> playStep { -1 };            // -1: not playing
        std::atomic<int> laneHits[kNumLanes] {};      // bumped on every hit (the lane lights)
        std::atomic<int> patternVersion { 0 };        // bumped when the pattern changes

        static juce::String laneParam (int lane, const char* what) { return "l" + juce::String (lane) + what; }

    private:
        void parameterChanged (const juce::String& id, float value) override;
        void applySettings();
        void loadFactory (int index);
        juce::ValueTree stateTree();
        void setParam (const juce::String& id, float value);
        float getParam (const juce::String& id) const;
        unsigned rolesChangedMask (const std::array<int, kNumLanes>& before) const;
        juce::Random dice;
        void restoreTree (const juce::ValueTree& t);
        void restoreSamples (const juce::ValueTree& samplesTree);
        void publishSamples();          // to the audio thread (sampleMutex held)
        void purgeRetired();            // the samples nothing plays any more (sampleMutex held)

        Groove groove;
        Pattern pattern;                 // message thread's copy, under lock
        Pattern audioPattern;            // the audio thread's
        juce::SpinLock patternLock;
        std::atomic<int> patternSerial { 0 };
        int audioSerial { -1 };
        uint32_t seedCounter { 1 };
        int currentProgram { 0 };

        // the samples: the message thread's (sampleMutex) and the audio
        // thread's copy (handed over under handoffLock); a replaced sample is
        // kept in retired until no voice holds it, so the audio thread never
        // frees one
        std::array<LaneSample, kNumLanes> laneSamples;
        std::array<juce::String, kNumLanes> sampleCodes;     // each sample as stored, made once
        juce::CriticalSection sampleMutex;
        juce::SpinLock handoffLock;
        std::array<std::shared_ptr<const SampleData>, kNumLanes> handoffData;
        std::array<Curve, kNumLanes> handoffCurve;
        std::atomic<int> sampleSerial { 0 };
        int audioSampleSerial { -1 };
        std::vector<std::shared_ptr<const SampleData>> retired;
        std::atomic<unsigned> auditionMask { 0 };

        double ownPos16 { 0.0 };
        bool wasRunning { false };
        double sr { 48000.0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoveGrooveProcessor)
    };
}
