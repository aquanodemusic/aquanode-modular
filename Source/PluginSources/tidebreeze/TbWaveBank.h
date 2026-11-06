#pragma once
//==============================================================================
// Tidebreeze - the wave bank
//
// Every wave a program can play, by its choice index: the factory waves, then
// the 16 user slots. A user slot holds a rolled wave (an archetype and a
// seed, rendered from its recipe) or a loaded one (an audio file, kept with
// the program as lossless audio). Waves render on a background thread of the
// bank's own when something asks for them, and are shared between every bank
// in the process by what they are made of, so two Tidebreezes (or the
// modules of a patch version) playing the same wave hold one copy.
//
// The card's processor has one bank; each Wave module of the patch version
// has its own (with the user waves its layer plays).
//==============================================================================
#include "TbParams.h"

namespace tb
{
    class WaveBank
    {
    public:
        WaveBank();
        ~WaveBank();

        struct Slot
        {
            enum Kind { Empty, Rolled, Sample };
            Kind kind = Empty;
            int archetype = 0; juce::uint32 seed = 0;   // Rolled
            SamplePtr sample; SampleSettings settings;   // Sample
            juce::String name;
            bool used() const { return kind != Empty; }
        };

        // the longest file a slot takes (seconds)
       #if JUCE_ANDROID
        static constexpr double kMaxSampleSeconds = 30.0;
       #else
        static constexpr double kMaxSampleSeconds = 60.0;
       #endif

        //=== the slots (message thread) =======================================
        const Slot& slot (int i) const { return slots[(size_t) juce::jlimit (0, kUserWaveSlots - 1, i)]; }
        static bool isUser (int choice) { return choice >= numFactoryWaves() && choice < numWaveChoices(); }
        static int slotOf (int choice) { return choice - numFactoryWaves(); }
        static int choiceOf (int slot) { return numFactoryWaves() + slot; }
        juce::String waveName (int choice) const;
        juce::String groupName (int choice) const;   // "Voice", "Water" ... "Rolled", "Sample"

        // a free slot, else the next one in turn (the oldest)
        int nextSlot();
        // rolls a wave of an archetype into a slot (-1: nextSlot()); its choice index
        int rollWave (int archetype, juce::uint32 seed = 0, int slot = -1);
        // loads an audio file into a slot (-1: nextSlot()); its choice index, or
        // -1 and the reason
        int loadFile (const juce::File& file, int slot, juce::String& error);
        // puts loaded audio into a slot (a copy between banks)
        int setSample (SamplePtr sample, const SampleSettings& settings, const juce::String& name, int slot);
        void setSampleSettings (int slot, const SampleSettings& settings);
        void clearSlot (int slot);
        void clearAll();
        // the slots of another bank (the ones include says yes to; all without it)
        void copyFrom (const WaveBank& other, const std::function<bool (int slot)>& include = {});

        //=== the rendered waves ================================================
        // asks for a wave (rendered in the background unless it is there already)
        void requestWave (int choice);
        // the wave if it is there; any thread (the audio thread only when it
        // may wait for a moment - it uses tryWave)
        WavePtr peekWave (int choice) const;
        // the audio thread: the wave if it is there and the lock is free
        WavePtr tryWave (int choice) const;
        // the waves a program uses: the others are dropped (and freed here,
        // once no voice holds them any more), these requested
        void ensure (const std::vector<bool>& used);
        bool pending() const { return jobsInFlight.load() > 0; }
        bool waitFor (int timeoutMs);

        std::atomic<bool> changed { false };          // a wave arrived (editors repaint)
        juce::uint32 slotsVersion() const { return version.load(); }   // bumps when any slot changes

        //=== saving ============================================================
        // <UserWaves> under parent (only the slots include says yes to, if given)
        void writeXml (juce::XmlElement& parent, const std::function<bool (int slot)>& include = {}) const;
        // the slots from <UserWaves> (none when it is missing)
        void readXml (const juce::XmlElement* userWaves);

    private:
        struct Job { int choice = 0; Slot::Kind kind = Slot::Empty; int archetype = 0; juce::uint32 seed = 0;
                     SamplePtr sample; SampleSettings settings; int gen = 0; };
        class Thread;

        void runJob (const Job&);
        void slotChanged (int slot, bool keepOldWave);
        void queue (int choice, bool evenIfThere);

        std::array<Slot, kUserWaveSlots> slots;
        int nextUserSlot = 0;
        std::atomic<juce::uint32> version { 1 };

        std::vector<WavePtr> liveWaves;               // by choice index
        mutable juce::SpinLock waveLock;
        std::vector<WavePtr> retired;                 // dropped, freed once no voice holds them
        juce::CriticalSection jobLock;
        std::vector<Job> jobs;
        std::vector<bool> queued;                     // by choice: waiting or rendering
        std::array<int, kUserWaveSlots> userGen {};   // bumped when a slot changes
        std::atomic<int> jobsInFlight { 0 };
        std::unique_ptr<Thread> thread;

        JUCE_DECLARE_NON_COPYABLE (WaveBank)
    };
}
