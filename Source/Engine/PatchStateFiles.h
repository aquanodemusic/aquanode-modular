#pragma once
//==============================================================================
// How an exported patch (.aqua) keeps each module's own state (its
// CustomState): readable, with no audio in it.
//
// A module's state is often a whole plugin's state - XML or JSON, sometimes a
// zip - packed into one encoded string, with the plugin's loaded sounds
// encoded inside it (Cove Groove's lane samples, Tidebreeze's user waves,
// FlowEQ's and AlphaBeta's wavetables, Phizmo's wavetable files, Aqua
// Vibrio's sampled waves, Vita's sample and wavetables, ...). In a .aqua:
//
//   - the state is written out as what it is - the plugin's XML as XML, its
//     JSON as JSON text, a zip as its entries - inside a <State> element of
//     patch.xml, so patch.xml reads as plain XML;
//   - every sound found in it becomes a file of its own in samples/ - the
//     very file the plugin had (FLAC, WAV, ...) where it kept one, else a WAV
//     of exactly its data - and the place in the state says which file
//     ("aqua-file:<codec>:<base64 flavour>:n=<count>:samples/<name>");
//   - importing the patch puts the very same state back together from these
//     and hands it to the module as before.
//
// Nothing is changed that would not come back exactly: every state is put
// back together right after it is written out and compared with the
// original (a zip: entry by entry); a state that would not come back the
// same stays as it was (encoded, in <CustomState>). The plugins themselves
// know nothing of this.
//
// DAW sessions and the app's own saved state keep the states as before.
//==============================================================================
#include <JuceHeader.h>
#include <functional>
#include <map>
#include <set>

namespace aquanode::patchfiles
{
    // the samples/ files of a patch being exported
    class FileSink
    {
    public:
        explicit FileSink (juce::ZipFile::Builder& zipToFill) : zip (zipToFill) {}

        // keeps the bytes as samples/<base>.<ext> - once: the same bytes again
        // get the same file - and returns that path. Staged until commit().
        juce::String add (const juce::MemoryBlock& bytes, const juce::String& base, const juce::String& ext);
        bool read (const juce::String& path, juce::MemoryBlock& out) const;

        void commit();    // the staged files go into the zip
        void discard();   // forgotten (the state they came from stays as it was)

    private:
        juce::ZipFile::Builder& zip;
        std::map<juce::String, juce::MemoryBlock> committed, staged;   // path -> bytes
        std::set<juce::String> usedNames;
    };

    using FileSource = std::function<bool (const juce::String& path, juce::MemoryBlock& bytes)>;

    // A module's custom state as a .aqua keeps it; nullptr: keep it as it is
    // (as <CustomState> text) - too small to matter, a format of its own, or
    // one that would not come back exactly.
    std::unique_ptr<juce::XmlElement> writeState (const juce::String& customState, const juce::String& typeId,
                                                  int instanceId, FileSink& files);

    // The custom state back from its <State> element. A file that is missing
    // leaves that sound out (silence of the same length where the plugin
    // needs one). False when the element cannot be read at all.
    bool readState (const juce::XmlElement& state, const FileSource& files, juce::String& customState);
}
