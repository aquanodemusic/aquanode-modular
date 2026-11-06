#pragma once
//==============================================================================
// Render to File: the patch played through its Piano Rolls into a WAV or FLAC
// file, faster than real time and without touching what is playing live.
//
// What is rendered: every Piano Roll that has notes and whose Midi Out is
// patched somewhere, all started together from their beginning (as Play All
// does), for as long as the longest of them lasts - its bars at its tempo, or
// at the one tempo chosen for all. Shorter rolls loop if their Loop is on.
// What plays by itself without a Piano Roll (Always Midi, an Arp on the keys,
// a 303's own grid) sounds along but does not make the file longer. Then the
// rolls stop and the tails ring out: up to 8 seconds, cut at the first half
// second of silence (a tail that never ends - a drone - fades out).
//
// The render runs on a copy of the patch (a second engine, prepared at the
// chosen sample rate), so the live sound goes on meanwhile. 24-bit; FLAC at
// its highest compression level.
//==============================================================================
#include <JuceHeader.h>

class AquanodeModularAudioProcessor;

namespace aquanode::render
{
    struct Settings
    {
        bool flac { false };
        double sampleRate { 48000.0 };
        bool oneTempo { false };     // every Piano Roll at `bpm`, else each at its own tempo
        double bpm { 120.0 };
    };

    struct RollInfo
    {
        int moduleId { -1 };
        double bpm { 120.0 };        // its own tempo
        double beats { 16.0 };       // its length
    };

    // the Piano Rolls a render plays through (message thread)
    std::vector<RollInfo> rollsToRender (const AquanodeModularAudioProcessor& patch);
    inline bool canRender (const AquanodeModularAudioProcessor& patch) { return ! rollsToRender (patch).empty(); }

    // how long the rolls play, in seconds (without the tails)
    double rollSeconds (const std::vector<RollInfo>& rolls, const Settings& settings);

    constexpr double kMaxTailSeconds = 8.0;
    constexpr int kSettleSamples = 64;   // run (and left out) before Play All

    //==========================================================================
    // One render. Made on the message thread (it copies the patch there),
    // run on a background thread with run(), or with start() as a juce::Thread.
    class Job : public juce::Thread
    {
    public:
        Job (AquanodeModularAudioProcessor& patch, const Settings& settings, const juce::File& outputFile);
        ~Job() override;

        void run() override;   // the render itself; calls onFinished on the message thread when started as a thread

        float progress() const { return progressValue.load(); }   // 0..1
        juce::String error() const { return problem; }            // empty when it worked
        double renderedSeconds() const { return seconds; }

        std::function<void()> onFinished;   // message thread, after a threaded run

    private:
        juce::String render();

        std::unique_ptr<AquanodeModularAudioProcessor> copy;
        Settings settings;
        juce::File outputFile;
        double playSeconds { 0.0 };
        std::atomic<float> progressValue { 0.0f };
        juce::String problem;
        double seconds { 0.0 };
    };
}
