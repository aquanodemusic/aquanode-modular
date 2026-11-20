#pragma once
//==============================================================================
// MidiTake (6.7.10) - a MIDI module's own recorder: what it plays, from Rec on
// to Rec off, saved as a .mid file (MIDI Rain, MIDI Bloom, MIDI Stamp).
//
// The module tells it where it is in its own beat clock with every event
// (the clock its steps and chords run on), so the file lines up with the
// module's grid: the take starts at the bar of its first note. Notes still
// sounding when Rec goes off end there. The tempo written into the file is
// the patch's at the end of the take.
//
// Audio thread: add(). Message thread: start(), stop(), toMidiFile(),
// save() (the system's save dialog; on Android the picked document is
// written through aqp::files::writeDocument, as every save in the app is).
//==============================================================================
#include "../Core/ModuleCore.h"
#include "../../Platform/AndroidDocuments.h"

namespace aquanode
{
struct MidiTake
{
    struct Ev { double beat; int note; float velocity; bool on; };
    static constexpr int kMax = 16384;

    MidiTake() { evs.resize ((size_t) kMax); }

    //=== audio thread ==========================================================
    void add (double beat, int note, float velocity, bool isOn) noexcept
    {
        if (! recording.load (std::memory_order_acquire)) return;
        const int n = count.load (std::memory_order_relaxed);
        if (n >= kMax) return;
        evs[(size_t) n] = { beat, juce::jlimit (0, 127, note), velocity, isOn };
        count.store (n + 1, std::memory_order_release);
    }

    //=== message thread ========================================================
    void start() { count.store (0, std::memory_order_relaxed); recording.store (true, std::memory_order_release); }
    void stop()  { recording.store (false, std::memory_order_release); }
    bool isRecording() const { return recording.load (std::memory_order_relaxed); }
    int numEvents() const { return count.load (std::memory_order_acquire); }

    // the take as a type-1 .mid file (a tempo track, then the notes); empty when
    // nothing was played
    juce::MemoryBlock toMidiFile (double bpm, int beatsPerBar = 4) const
    {
        const int n = numEvents();
        double first = -1.0, last = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto& e = evs[(size_t) i];
            if (e.on && first < 0.0) first = e.beat;
            last = juce::jmax (last, e.beat);
        }
        if (first < 0.0) return {};
        const double origin = std::floor (first / beatsPerBar) * beatsPerBar;   // (the bar of the first note)
        constexpr int ppq = 480;
        juce::MidiMessageSequence seq;
        bool sounding[128] {};
        for (int i = 0; i < n; ++i)
        {
            const auto& e = evs[(size_t) i];
            const double ticks = juce::jmax (0.0, e.beat - origin) * ppq;
            if (e.on)
            {
                if (sounding[e.note])   // struck again: the earlier one ends here
                    seq.addEvent (juce::MidiMessage::noteOff (1, e.note).withTimeStamp (ticks));
                seq.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) juce::jlimit (1, 127, (int) std::lround (e.velocity * 127.0f)))
                                  .withTimeStamp (ticks));
                sounding[e.note] = true;
            }
            else if (sounding[e.note])
            {
                seq.addEvent (juce::MidiMessage::noteOff (1, e.note).withTimeStamp (ticks));
                sounding[e.note] = false;
            }
        }
        const double endTicks = juce::jmax (0.0, last - origin) * ppq + 1.0;
        for (int k = 0; k < 128; ++k)
            if (sounding[k]) seq.addEvent (juce::MidiMessage::noteOff (1, k).withTimeStamp (endTicks));
        seq.sort();
        seq.updateMatchedPairs();

        juce::MidiMessageSequence tempo;
        tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (beatsPerBar, 4));
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (20.0, 999.0, bpm))));
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (tempo);
        file.addTrack (seq);
        juce::MemoryOutputStream out;
        if (! file.writeTo (out)) return {};
        return out.getMemoryBlock();
    }

    // the save dialog for the take; the chooser lives in the caller (a card)
    void save (std::unique_ptr<juce::FileChooser>& holder, const juce::String& title, const juce::String& suggestedName,
               double bpm, int beatsPerBar = 4) const
    {
        const juce::String boxTitle = title.isEmpty() ? juce::String ("MIDI") : title;
        auto tell = [boxTitle] (const juce::String& text)
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, boxTitle, text);
        };
        auto data = std::make_shared<juce::MemoryBlock> (toMidiFile (bpm, beatsPerBar));
        if (data->getSize() == 0) { tell ("Nothing was played while Rec was on."); return; }
        const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                   .getNonexistentChildFile (suggestedName, ".mid", false);
        holder = std::make_unique<juce::FileChooser> ("Save the take as a MIDI file", suggested, "*.mid");
        holder->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [data, tell] (const juce::FileChooser& fc)
        {
            const auto url = fc.getURLResult();   // (the address: an Android pick is a document, not a path)
            if (url.isEmpty()) return;
            const auto problem = aqp::files::writeDocument (url, data->getData(), data->getSize());
            if (problem.isNotEmpty()) tell ("The MIDI file could not be saved: " + problem + ".");
        });
    }

private:
    std::vector<Ev> evs;
    std::atomic<int> count { 0 };
    std::atomic<bool> recording { false };
};

//==============================================================================
// the beat clock MIDI generators share (6.7.10): the song's position where a
// host plays, else their own count of beats at the patch's tempo
struct BeatClock
{
    double beat { 0.0 };          // where it is now
    double beatsPerSample { 0.0 };

    // once a block: the tempo, and the host's position when it plays
    void blockStart (double bpm, double sampleRate, juce::AudioPlayHead* playHead)
    {
        beatsPerSample = (bpm > 1.0 ? bpm : 120.0) / 60.0 / juce::jmax (1000.0, sampleRate);
        if (playHead != nullptr)
            if (auto pos = playHead->getPosition())
                if (pos->getIsPlaying())
                    if (auto ppq = pos->getPpqPosition())
                    {
                        // a jump (or the start): where the song is
                        if (std::abs (*ppq - beat) > 0.05) beat = *ppq;
                    }
    }
    void tick() noexcept { beat += beatsPerSample; }
};

// the shader hash the Bandpass Modulator moves by: 0..1, the same for the same n
inline float seededRandom (double n) noexcept
{
    const double v = std::sin (n * 12.9898) * 43758.5453;
    return (float) (v - std::floor (v));
}
} // namespace aquanode
