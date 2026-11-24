#pragma once
//==============================================================================
// (6.9.18) Save IR: an impulse response written as a 32-bit float .wav where the
// user picks (a document on Android, as every save in the app is). Equal
// Loudness and Quadrature Stereo use it; IR Forge and the Convolver load it.
//==============================================================================
#include <JuceHeader.h>
#include "../../Platform/AndroidDocuments.h"

namespace aquanode::impulse
{
    inline juce::MemoryBlock toWav (const juce::AudioBuffer<float>& ir, double rate)
    {
        juce::MemoryBlock block;
        {
            auto stream = std::make_unique<juce::MemoryOutputStream> (block, false);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, (unsigned int) ir.getNumChannels(), 32, {}, 0));
            if (writer == nullptr) return {};
            stream.release();   // (the writer owns it now)
            writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
        }
        return block;
    }

    // the save dialog; the chooser lives in the caller (a module)
    inline void save (std::unique_ptr<juce::FileChooser>& holder, const juce::AudioBuffer<float>& ir, double rate,
                      const juce::String& suggestedName, const juce::String& title)
    {
        auto tell = [title] (const juce::String& text)
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, text);
        };
        auto data = std::make_shared<juce::MemoryBlock> (toWav (ir, rate));
        if (data->getSize() == 0) { tell ("The impulse response could not be made."); return; }
        const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                   .getNonexistentChildFile (suggestedName, ".wav", false);
        holder = std::make_unique<juce::FileChooser> ("Save the impulse response", suggested, "*.wav");
        holder->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [data, tell] (const juce::FileChooser& fc)
        {
            const auto url = fc.getURLResult();
            if (url.isEmpty()) return;
            const auto problem = aqp::files::writeDocument (url, data->getData(), data->getSize());
            if (problem.isNotEmpty()) tell ("The impulse response could not be saved: " + problem + ".");
        });
    }
}
