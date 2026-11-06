#pragma once
//==============================================================================
// Android: writing into a document the system's picker handed back.
//
// JUCE opens a picked document for writing with ContentResolver's plain "w"
// mode. Since Android 10 some document providers no longer cut the file to
// length in that mode, so writing a shorter file over a longer one (saving a
// patch over an older, bigger one) leaves the old file's tail behind it - and
// a zip with someone else's tail opens as the old patch, or not at all. This
// opens the document with "wt" (write, truncate) instead, falling back to
// JUCE's own stream when a provider refuses that mode.
//
// Used by Export Patch and by the merged plugins' save pickers
// (AndroidFileChooserShim.h), which also list picked folders with it. Message
// thread. Elsewhere it does nothing.
//==============================================================================
#include <JuceHeader.h>

namespace aqp::android
{
   #if JUCE_ANDROID
    // A document inside a folder the folder picker handed back
    struct ChildDocument
    {
        juce::URL url;            // the child, as a document of the picked tree
        juce::String name, mime;
        juce::int64 size { -1 };  // -1: the provider did not say
        bool isFolder { false };
    };

    // What a picked folder holds (a tree from the folder picker, or a folder
    // inside one), straight from the document provider. JUCE 9's
    // AndroidDocumentIterator walks the folder as a plain path instead, which
    // the app may not read under scoped storage - so a picked folder looked
    // empty. `problem` says what went wrong when the list is empty for a reason.
    std::vector<ChildDocument> listChildren (const juce::URL& folder, juce::String* problem = nullptr);

    // empty when it worked, otherwise what went wrong
    juce::String replaceDocument (const juce::URL& document, const void* data, size_t size);
    // the same, from a file (a rendered sound: too big to hold in memory)
    juce::String replaceDocument (const juce::URL& document, const juce::File& source);
   #endif

    // The patterns to give Android's document picker for a chooser's
    // "*.json;*.fxb" list. The picker shows only the documents whose type
    // matches the patterns' MIME types, and a format with no well-known type
    // (.fxb, .syx, .fxp, a plugin's own .aqua) has none: its documents would be
    // greyed out even with "*.fxb" in the list. So the picker is only filtered
    // when every pattern has a type it knows; otherwise (empty) it shows every
    // document and the loader checks what it was given. Other platforms filter
    // by extension and keep the list.
    inline juce::String pickerPatterns (const juce::String& patterns)
    {
       #if JUCE_ANDROID
        static const juce::StringArray known { "wav", "aif", "aiff", "flac", "ogg", "mp3", "m4a", "mid", "midi",
                                               "json", "txt", "xml", "zip", "png", "jpg", "jpeg" };
        for (auto w : juce::StringArray::fromTokens (patterns, ";,", ""))
        {
            w = w.trim();
            if (w.isEmpty())
                continue;
            if (! w.startsWith ("*.") || ! known.contains (w.substring (2).toLowerCase()))
                return {};
        }
       #endif
        return patterns;
    }

    // what a file's first bytes say it is ("wav", "mp3", "aqua" for a zip,
    // "xml", ...), or empty when they say nothing known
    inline juce::String sniffExtension (const void* data, size_t size)
    {
        auto* b = static_cast<const unsigned char*> (data);
        auto is = [b, size] (size_t at, const char* magic)
        {
            const auto n = std::strlen (magic);
            return size >= at + n && std::memcmp (b + at, magic, n) == 0;
        };
        if (b == nullptr || size < 4) return {};
        if (is (0, "RIFF") && (is (8, "WAVE")))                     return "wav";
        if (is (0, "RF64") || is (0, "BW64"))                       return "wav";
        if (is (0, "FORM") && (is (8, "AIFF") || is (8, "AIFC")))  return "aiff";
        if (is (0, "fLaC"))                                         return "flac";
        if (is (0, "OggS"))                                         return "ogg";
        if (is (0, "ID3"))                                          return "mp3";
        if (b[0] == 0xff && (b[1] & 0xe0) == 0xe0 && (b[1] & 0x06) != 0) return "mp3";   // an MPEG audio frame
        if (is (0, "MThd"))                                         return "mid";
        if (is (0, "CcnK") && size >= 12)                           return (is (8, "FBCh") || is (8, "FxBk")) ? "fxb" : "fxp";   // a VST bank / preset
        if (b[0] == 0xf0)                                           return "syx";
        if (is (0, "PK\x03\x04") || is (0, "PK\x05\x06"))         return "zip";
        if (is (0, "VC2!"))                                         return "state";      // a JUCE plugin state
        // text: skip a byte-order mark and white space, then '<' is XML, '{' JSON
        size_t i = is (0, "\xef\xbb\xbf") ? 3 : 0;
        while (i < size && i < 256 && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n')) ++i;
        if (i < size && b[i] == '<') return "xml";
        if (i < size && b[i] == '{') return "json";
        return {};
    }
}
