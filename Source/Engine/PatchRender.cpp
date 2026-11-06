#include "PatchRender.h"
#include "PluginProcessor.h"
#include "../Modules/Utilities/MIDI/PianoRollModule.h"

namespace aquanode::render
{
    std::vector<RollInfo> rollsToRender (const AquanodeModularAudioProcessor& patch)
    {
        std::vector<RollInfo> rolls;
        for (int id : patch.getModuleIds())
        {
            auto* inst = patch.getInstance (id);
            auto* roll = inst != nullptr ? dynamic_cast<PianoRollModule*> (inst->dsp.get()) : nullptr;
            if (roll == nullptr || roll->getNotes().empty())
                continue;
            const auto& cables = patch.getCables();
            const bool patched = std::any_of (cables.begin(), cables.end(), [id] (const CableInfo& c)
                                              { return c.fromModule == id && c.fromSocket == "midiOut"; });
            if (! patched)
                continue;
            rolls.push_back ({ id, juce::jlimit (1.0, 999.0, (double) roll->getParameter ("tempo")), roll->loopLengthBeats() });
        }
        return rolls;
    }

    double rollSeconds (const std::vector<RollInfo>& rolls, const Settings& settings)
    {
        double longest = 0.0;
        for (const auto& r : rolls)
            longest = juce::jmax (longest, r.beats * 60.0 / (settings.oneTempo ? juce::jlimit (1.0, 999.0, settings.bpm) : r.bpm));
        return longest;
    }

    //==========================================================================
    Job::Job (AquanodeModularAudioProcessor& patch, const Settings& s, const juce::File& file)
        : juce::Thread ("Aquanode render"), settings (s), outputFile (file)
    {
        const auto rolls = rollsToRender (patch);
        playSeconds = rollSeconds (rolls, settings);
        if (rolls.empty())
        {
            problem = "no Piano Roll with notes is patched to anything";
            return;
        }

        // the patch as it is now, in an engine of its own (the live one plays on)
        juce::MemoryBlock state;
        patch.getStateInformation (state);
        copy = std::make_unique<AquanodeModularAudioProcessor>();
        copy->setPlayConfigDetails (2, 2, settings.sampleRate, 512);
        copy->setStateInformation (state.getData(), (int) state.getSize());

        if (settings.oneTempo)
            for (int id : copy->getModuleIds())
                if (auto* roll = dynamic_cast<PianoRollModule*> (copy->getInstance (id)->dsp.get()))
                {
                    roll->setParameter ("sync", 0.0f);
                    roll->setParameter ("tempo", (float) settings.bpm);
                }
    }

    Job::~Job()
    {
        stopThread (10000);
    }

    void Job::run()
    {
        if (problem.isEmpty())
            problem = render();
        if (onFinished != nullptr && isThreadRunning())
        {
            auto done = onFinished;
            juce::MessageManager::callAsync (done);
        }
    }

    juce::String Job::render()
    {
        constexpr int block = 512;
        const double sr = settings.sampleRate;
        copy->prepareToPlay (sr, block);

        outputFile.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outputFile);
        if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
            return "the file could not be written";

        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                             .withNumChannels (2)
                                                             .withBitsPerSample (24)
                                                             .withQualityOptionIndex (settings.flac ? 8 : 0);   // FLAC: 8 = smallest
        std::unique_ptr<juce::AudioFormat> format;
        if (settings.flac) format = std::make_unique<juce::FlacAudioFormat>();
        else               format = std::make_unique<juce::WavAudioFormat>();
        auto writer = format->createWriterFor (stream, options);
        if (writer == nullptr)
            return "this sample rate cannot be written as " + juce::String (settings.flac ? "FLAC" : "WAV");

        // everything from the start, together: the rolls stopped, a moment for every
        // module to settle in (a roll takes a Play All only once it has seen the patch
        // running), then Play All
        for (int id : copy->getModuleIds())
            if (auto* roll = dynamic_cast<PianoRollModule*> (copy->getInstance (id)->dsp.get()))
                roll->setPlaying (false);
        {
            juce::AudioBuffer<float> settle (2, kSettleSamples);
            settle.clear();
            juce::MidiBuffer none;
            copy->processBlock (settle, none);
        }
        copy->playAllPianoRolls (true);

        const auto playSamples = (juce::int64) std::ceil (playSeconds * sr);
        const auto tailMax = (juce::int64) (kMaxTailSeconds * sr);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;

        for (juce::int64 done = 0; done < playSamples; done += block)
        {
            if (threadShouldExit())
                return "cancelled";
            const int n = (int) juce::jmin ((juce::int64) block, playSamples - done);
            buf.setSize (2, n, false, false, true);
            buf.clear();
            midi.clear();
            copy->processBlock (buf, midi);
            if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
                return "writing the file failed";
            progressValue = (float) (0.95 * (double) (done + n) / (double) (playSamples + tailMax / 4));
        }

        // the rolls stop; the tails ring out until half a second of silence
        copy->playAllPianoRolls (false);
        juce::AudioBuffer<float> tail (2, (int) tailMax);
        tail.clear();
        const int quietNeeded = (int) (0.5 * sr);
        int quiet = 0, tailLength = 0;
        bool silent = false;
        while (tailLength < (int) tailMax && ! silent)
        {
            if (threadShouldExit())
                return "cancelled";
            const int n = juce::jmin (block, (int) tailMax - tailLength);
            buf.setSize (2, n, false, false, true);
            buf.clear();
            midi.clear();
            copy->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                tail.copyFrom (c, tailLength, buf, c, 0, n);
            for (int i = 0; i < n; ++i)
            {
                const bool q = std::abs (buf.getSample (0, i)) < 3.2e-5f && std::abs (buf.getSample (1, i)) < 3.2e-5f;   // -90 dBFS
                quiet = q ? quiet + 1 : 0;
            }
            tailLength += n;
            silent = quiet >= quietNeeded;
            progressValue = (float) juce::jmin (0.99, 0.95 * (double) (playSamples + tailLength / 4) / (double) (playSamples + tailMax / 4));
        }

        if (silent)
            tailLength = juce::jmax (0, tailLength - quiet + (int) (0.05 * sr));   // keep 50 ms of the silence
        else
        {
            // still sounding after 8 s: fade out over the last half second
            const int fade = juce::jmin (tailLength, (int) (0.5 * sr));
            for (int c = 0; c < 2; ++c)
                tail.applyGainRamp (c, tailLength - fade, fade, 1.0f, 0.0f);
        }
        if (tailLength > 0 && ! writer->writeFromAudioSampleBuffer (tail, 0, tailLength))
            return "writing the file failed";

        writer.reset();   // finishes the file
        seconds = (double) (playSamples + tailLength) / sr;
        progressValue = 1.0f;
        return {};
    }
}
