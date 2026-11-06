#include "PatchStateFiles.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aquanode::patchfiles
{
namespace
{
    const juce::String kRef { "aqua-file:" };
    constexpr int kMinBlob = 64;        // a value shorter than this is left alone
    constexpr int kMinState = 64;       // so is a state shorter than this

    //=== base64, both flavours ================================================
    // "std": RFC 4648 (juce::Base64). "mb": JUCE's own "<size>.<chars>"
    // (MemoryBlock::toBase64Encoding). A value is only taken for one when it
    // encodes back to exactly the same text.
    enum class Flavour { none, stdB64, juceB64 };

    juce::String nameOf (Flavour f) { return f == Flavour::juceB64 ? "mb" : f == Flavour::stdB64 ? "std" : "none"; }
    Flavour flavourOf (const juce::String& s) { return s == "mb" ? Flavour::juceB64 : s == "std" ? Flavour::stdB64 : Flavour::none; }

    bool decodeJuce (const juce::String& s, juce::MemoryBlock& out)
    {
        const int dot = s.indexOfChar ('.');
        if (dot <= 0 || dot > 12 || ! s.substring (0, dot).containsOnly ("0123456789"))
            return false;
        if (! s.substring (dot + 1).containsOnly (".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"))
            return false;
        out.reset();
        return out.fromBase64Encoding (s) && out.toBase64Encoding() == s;
    }

    bool decodeStd (const juce::String& s, juce::MemoryBlock& out)
    {
        if (s.length() % 4 != 0 || ! s.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="))
            return false;
        juce::MemoryOutputStream m;
        if (! juce::Base64::convertFromBase64 (m, s))
            return false;
        out = m.getMemoryBlock();
        return juce::Base64::toBase64 (out.getData(), out.getSize()) == s;
    }

    bool decodeAny (const juce::String& s, juce::MemoryBlock& out, Flavour& f)
    {
        if (s.length() < kMinBlob) return false;
        if (decodeJuce (s, out)) { f = Flavour::juceB64; return true; }
        if (decodeStd (s, out))  { f = Flavour::stdB64;  return true; }
        return false;
    }

    juce::String encode (const juce::MemoryBlock& b, Flavour f)
    {
        return f == Flavour::juceB64 ? b.toBase64Encoding() : juce::Base64::toBase64 (b.getData(), b.getSize());
    }

    //=== sound files ==========================================================
    bool startsWith (const juce::MemoryBlock& b, const char* magic, size_t at = 0)
    {
        const auto n = std::strlen (magic);
        return b.getSize() >= at + n && std::memcmp (static_cast<const char*> (b.getData()) + at, magic, n) == 0;
    }

    // what kind of sound file the bytes are ("" = none)
    juce::String audioExtension (const juce::MemoryBlock& b)
    {
        if (startsWith (b, "RIFF") && startsWith (b, "WAVE", 8)) return "wav";
        if (startsWith (b, "RF64") || startsWith (b, "BW64"))   return "wav";
        if (startsWith (b, "FORM") && (startsWith (b, "AIFF", 8) || startsWith (b, "AIFC", 8))) return "aiff";
        if (startsWith (b, "fLaC")) return "flac";
        if (startsWith (b, "OggS")) return "ogg";
        if (startsWith (b, "ID3"))  return "mp3";
        return {};
    }

    // raw samples (16-bit PCM or 32-bit float, one buffer per channel) as a
    // WAV file holding exactly them
    juce::MemoryBlock makeWav (const std::vector<const char*>& channels, size_t frames, int bits, bool isFloat, int rate)
    {
        const int numCh = (int) channels.size(), bytesPer = bits / 8;
        const auto dataSize = (juce::uint32) (frames * (size_t) numCh * (size_t) bytesPer);
        juce::MemoryOutputStream o;
        o.write ("RIFF", 4);
        o.writeInt ((int) (36 + dataSize));
        o.write ("WAVE", 4);
        o.write ("fmt ", 4);
        o.writeInt (16);
        o.writeShort ((short) (isFloat ? 3 : 1));
        o.writeShort ((short) numCh);
        o.writeInt (rate);
        o.writeInt (rate * numCh * bytesPer);
        o.writeShort ((short) (numCh * bytesPer));
        o.writeShort ((short) bits);
        o.write ("data", 4);
        o.writeInt ((int) dataSize);
        for (size_t i = 0; i < frames; ++i)
            for (int c = 0; c < numCh; ++c)
                o.write (channels[(size_t) c] + i * (size_t) bytesPer, (size_t) bytesPer);
        return o.getMemoryBlock();
    }

    // one channel of a sound file (any format JUCE reads) as floats
    bool readChannel (const juce::MemoryBlock& file, int channel, std::vector<float>& out)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (file, false)));
        if (r == nullptr || r->lengthInSamples <= 0 || r->numChannels < 1)
            return false;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) 1 << 30);
        juce::AudioBuffer<float> b ((int) r->numChannels, n);
        b.clear();
        r->read (&b, 0, n, 0, true, true);
        const int ch = juce::jlimit (0, b.getNumChannels() - 1, channel);
        out.assign (b.getReadPointer (ch), b.getReadPointer (ch) + n);
        return true;
    }

    //=== names ================================================================
    juce::String clean (const juce::String& s)
    {
        auto t = juce::File::createLegalFileName (s).removeCharacters (":\"\\/").trim();
        while (t.contains ("  ")) t = t.replace ("  ", " ");
        return t.substring (0, 60).trim();
    }

    juce::String shortType (const juce::String& typeId)
    {
        return clean ((typeId.startsWith ("plugin.") ? typeId.substring (7) : typeId).replaceCharacter ('.', '_'));
    }

    struct Ctx
    {
        FileSink& files;
        juce::String base;      // "<id>_<type>"
        juce::String typeId;

        juce::String store (const juce::MemoryBlock& bytes, const juce::String& label, const juce::String& ext)
        {
            const auto l = clean (label);
            return files.add (bytes, base + (l.isNotEmpty() ? "_" + l : juce::String()), ext);
        }

        // "aqua-file:<codec>:<flavour>:n=<count>:<path>"
        static juce::String ref (const juce::String& codec, Flavour f, juce::int64 count, const juce::String& path)
        {
            return kRef + codec + ":" + nameOf (f) + ":n=" + juce::String (count) + ":" + path;
        }

        juce::String fileRef (const juce::MemoryBlock& bytes, Flavour f, const juce::String& label, const juce::String& ext)
        {
            return ref ("file", f, (juce::int64) bytes.getSize(), store (bytes, label, ext));
        }

        juce::String f32Ref (const juce::MemoryBlock& bytes, Flavour f, const juce::String& label, int rate)
        {
            const size_t n = bytes.getSize() / 4;
            return ref ("f32", f, (juce::int64) n, store (makeWav ({ static_cast<const char*> (bytes.getData()) }, n, 32, true, rate), label, "wav"));
        }
    };

    //=== references back to data ==============================================
    struct Ref
    {
        juce::String codec, path;
        Flavour flavour { Flavour::none };
        juce::int64 count { 0 };
    };

    bool parseRef (const juce::String& v, Ref& r)
    {
        if (! v.startsWith (kRef)) return false;
        auto rest = v.substring (kRef.length());
        r.codec = rest.upToFirstOccurrenceOf (":", false, false);
        rest = rest.fromFirstOccurrenceOf (":", false, false);
        r.flavour = flavourOf (rest.upToFirstOccurrenceOf (":", false, false));
        rest = rest.fromFirstOccurrenceOf (":", false, false);
        if (! rest.startsWith ("n=")) return false;
        r.count = rest.substring (2).upToFirstOccurrenceOf (":", false, false).getLargeIntValue();
        r.path = rest.fromFirstOccurrenceOf (":", false, false);
        return r.codec.isNotEmpty() && r.path.isNotEmpty();
    }

    // the data a reference stands for, as the bytes the state held
    juce::MemoryBlock dataOf (const Ref& r, const FileSource& src)
    {
        juce::MemoryBlock file;
        const bool have = src != nullptr && src (r.path, file) && file.getSize() > 0;
        if (r.codec == "file")
            return have ? file : juce::MemoryBlock();

        const auto n = (size_t) juce::jlimit<juce::int64> (0, (juce::int64) 1 << 30, r.count);
        if (r.codec == "f32" || r.codec.startsWith ("f32@"))
        {
            // "f32@<first>": its frames start there in the file (a wavetable's keyframes)
            const auto first = (size_t) std::max<juce::int64> (0, r.codec.fromFirstOccurrenceOf ("@", false, false).getLargeIntValue());
            std::vector<float> v;
            if (have) readChannel (file, 0, v);
            std::vector<float> part (n, 0.0f);    // a missing file: silence of the same length
            for (size_t i = 0; i < n && first + i < v.size(); ++i) part[i] = v[first + i];
            return juce::MemoryBlock (part.data(), n * sizeof (float));
        }
        if (r.codec.startsWith ("i16"))
        {
            std::vector<float> v;
            if (have) readChannel (file, r.codec.fromFirstOccurrenceOf (".", false, false).getIntValue(), v);
            v.resize (n, 0.0f);
            std::vector<juce::int16> pcm (n);
            for (size_t i = 0; i < n; ++i)
                pcm[i] = (juce::int16) juce::jlimit (-32768, 32767, juce::roundToInt (v[i] * 32768.0f));
            return juce::MemoryBlock (pcm.data(), n * sizeof (juce::int16));
        }
        return {};
    }

    juce::String partBack (const juce::String& part);
    template <typename Fn> juce::String mapParts (const juce::String& v, Fn&& fn);

    juce::String valueOf (const juce::String& v, const FileSource& src)
    {
        Ref r;
        if (parseRef (v, r))
            return encode (dataOf (r, src), r.flavour);
        if (! v.contains ("aqua-")) return v;
        return mapParts (v, [] (const juce::String& p) { return partBack (p); });
    }

    //=== other encoded data as plain numbers ==================================
    // A value that is encoded data but no sound - a drawn curve's points, a
    // step pattern's bytes - is written as its numbers:
    //   "aqua-f32:<flavour>:0.5 0.52 ..."   32-bit floats (base64)
    //   "aqua-i16:<flavour>:-120 340 ..."   16-bit integers (base64)
    //   "aqua-u8:<flavour>:12 255 ..."      bytes (base64)
    //   "aqua-hex:122 0 0 119 ..."          bytes (hex digits)
    // Values separated by '|' or ';' are taken a part at a time.
    const juce::String kNum { "aqua-" };

    // the shortest text that reads back as exactly this float (0.032, not 0.0320000015)
    juce::String formatFloat (float v)
    {
        char buf[40];
        for (int digits = 6; digits <= 9; ++digits)
        {
            std::snprintf (buf, sizeof (buf), "%.*g", digits, (double) v);
            if (std::strtof (buf, nullptr) == v && std::signbit (std::strtof (buf, nullptr)) == std::signbit (v))
                break;
        }
        return buf;
    }

    juce::String numbersOf (const juce::MemoryBlock& b, Flavour f, juce::String kind)
    {
        const auto size = b.getSize();
        const auto* d = static_cast<const char*> (b.getData());
        if (kind.isEmpty() && size % 4 == 0)
        {
            kind = "f32";
            for (size_t i = 0; i < size; i += 4)
            {
                float v;
                std::memcpy (&v, d + i, 4);
                if (! std::isfinite (v)) { kind.clear(); break; }
            }
        }
        if (kind.isEmpty()) kind = size % 2 == 0 ? "i16" : "u8";
        juce::StringArray parts;
        if (kind == "f32")
            for (size_t i = 0; i + 4 <= size; i += 4) { float v; std::memcpy (&v, d + i, 4); parts.add (formatFloat (v)); }
        else if (kind == "i16")
            for (size_t i = 0; i + 2 <= size; i += 2) { juce::int16 v; std::memcpy (&v, d + i, 2); parts.add (juce::String ((int) v)); }
        else
            for (size_t i = 0; i < size; ++i) parts.add (juce::String ((int) (unsigned char) d[i]));
        return kNum + kind + ":" + nameOf (f) + ":" + parts.joinIntoString (" ");
    }

    bool isHexRun (const juce::String& v)
    {
        return v.length() >= kMinBlob && v.length() % 2 == 0 && v.containsOnly ("0123456789abcdef");
    }

    // one part of a value: as numbers when it is encoded data
    juce::String readablePart (const juce::String& part, const juce::String& forcedKind)
    {
        if (part.length() < kMinBlob) return part;
        juce::MemoryBlock b;
        Flavour f;
        if (! isHexRun (part) && decodeAny (part, b, f) && b.getSize() > 0)
            return numbersOf (b, f, forcedKind);
        if (isHexRun (part))   // bytes as hex digits (Cove Groove's steps), checked first
        {
            juce::MemoryBlock o;
            o.loadFromHexString (part);
            juce::StringArray parts;
            const auto* d = static_cast<const unsigned char*> (o.getData());
            for (size_t i = 0; i < o.getSize(); ++i) parts.add (juce::String ((int) d[i]));
            return kNum + "hex:" + parts.joinIntoString (" ");
        }
        return part;
    }

    juce::String partBack (const juce::String& part)
    {
        if (! part.startsWith (kNum) || part.startsWith (kRef)) return part;
        const auto kind = part.substring (kNum.length()).upToFirstOccurrenceOf (":", false, false);
        if (kind != "f32" && kind != "i16" && kind != "u8" && kind != "hex") return part;
        auto rest = part.substring (kNum.length()).fromFirstOccurrenceOf (":", false, false);
        const auto values = juce::StringArray::fromTokens (kind == "hex" ? rest : rest.fromFirstOccurrenceOf (":", false, false), " ", {});
        if (kind == "hex")
        {
            juce::String hex;
            for (const auto& v : values) hex << juce::String::toHexString (v.getIntValue() & 255).paddedLeft ('0', 2);
            return hex;
        }
        const auto f = flavourOf (rest.upToFirstOccurrenceOf (":", false, false));
        juce::MemoryOutputStream o;
        for (const auto& v : values)
        {
            if (kind == "f32")      { const float x = std::strtof (v.toRawUTF8(), nullptr); o.write (&x, 4); }
            else if (kind == "i16") { const auto x = (juce::int16) v.getIntValue(); o.write (&x, 2); }
            else                    { const auto x = (juce::uint8) v.getIntValue(); o.write (&x, 1); }
        }
        return encode (o.getMemoryBlock(), f);
    }

    // every part of a value, separators kept
    template <typename Fn>
    juce::String mapParts (const juce::String& v, Fn&& fn)
    {
        juce::String out;
        int start = 0;
        for (int i = 0; i <= v.length(); ++i)
            if (i == v.length() || v[i] == '|' || v[i] == ';')
            {
                out << fn (v.substring (start, i));
                if (i < v.length()) out << juce::String::charToString (v[i]);
                start = i + 1;
            }
        return out;
    }

    juce::String readableValue (const juce::String& v, const juce::String& forcedKind = {})
    {
        if (v.length() < kMinBlob || v.startsWith (kRef)) return v;
        return mapParts (v, [&] (const juce::String& p) { return readablePart (p, forcedKind); });
    }

    //=== XML: sounds in attributes and texts ==================================
    juce::String labelFor (const juce::XmlElement& e, const juce::String& attr, const juce::String& parentName)
    {
        // Cove Groove's lanes by their names ("Bass - My Hit")
        if (e.hasTagName ("Lane") && e.hasAttribute ("index") && e.hasAttribute ("curve"))
        {
            static const char* lanes[] = { "Kick", "Bass", "Clap", "Closed Hat", "Open Hat", "Cymbal", "Perc", "FX" };
            const int i = e.getIntAttribute ("index", -1);
            if (i >= 0 && i < 8)
                return juce::String (lanes[i]) + (e.getStringAttribute ("name").isNotEmpty() ? " - " + e.getStringAttribute ("name") : juce::String());
        }
        if (attr.endsWith ("_data"))
            if (const auto stem = attr.dropLastCharacters (5); e.hasAttribute (stem + "_name"))
                return stem + " " + e.getStringAttribute (stem + "_name");
        juce::String l = e.getStringAttribute ("name");
        if (l.isEmpty()) l = parentName;
        for (auto key : { "index", "slot", "id", "lane" })
            if (e.hasAttribute (key))
            {
                l = e.getTagName() + " " + e.getStringAttribute (key) + (l.isNotEmpty() ? " - " + l : juce::String());
                break;
            }
        if (l.isEmpty()) l = e.getTagName();
        if (attr.isNotEmpty() && attr != "audio" && attr != "data")
            l = attr + " " + l;
        return l;
    }

    // a raw wavetable of 32-bit floats (AlphaBeta's WAVETABLE data)
    bool isRawFloats (const Ctx& ctx, const juce::XmlElement& e, const juce::String& attr)
    {
        return ctx.typeId.contains ("alphabeta") && e.hasTagName ("WAVETABLE") && attr == "data";
    }

    juce::String soundRef (Ctx& ctx, const juce::String& value, const juce::String& label, bool rawFloats)
    {
        juce::MemoryBlock bytes;
        Flavour f;
        if (! decodeAny (value, bytes, f)) return {};
        if (const auto ext = audioExtension (bytes); ext.isNotEmpty())
            return ctx.fileRef (bytes, f, label, ext);
        if (rawFloats && bytes.getSize() % 4 == 0 && bytes.getSize() > 0)
            return ctx.f32Ref (bytes, f, label, 44100);
        return {};
    }

    void extractXml (juce::XmlElement& e, Ctx& ctx, const juce::String& parentName)
    {
        for (int i = 0; i < e.getNumAttributes(); ++i)
        {
            const auto attr = e.getAttributeName (i);
            const auto value = e.getAttributeValue (i);
            if (value.length() < kMinBlob) continue;
            if (const auto r = soundRef (ctx, value, labelFor (e, attr, parentName), isRawFloats (ctx, e, attr)); r.isNotEmpty())
                e.setAttribute (attr, r);
            else if (const auto n = readableValue (value); n != value)
                e.setAttribute (attr, n);
        }
        const auto ownName = e.getStringAttribute ("name");
        for (auto* c : e.getChildIterator())
        {
            if (c->isTextElement())
            {
                const auto t = c->getText();
                if (t.length() >= kMinBlob && t.trim() == t)
                {
                    if (const auto r = soundRef (ctx, t, labelFor (e, {}, parentName), false); r.isNotEmpty())
                        c->setText (r);
                    else if (const auto n = readableValue (t); n != t)
                        c->setText (n);
                }
            }
            else
                extractXml (*c, ctx, ownName.isNotEmpty() ? ownName : parentName);
        }
    }

    void restoreXml (juce::XmlElement& e, const FileSource& src)
    {
        for (int i = 0; i < e.getNumAttributes(); ++i)
            if (const auto v = e.getAttributeValue (i); v.contains ("aqua-"))
                e.setAttribute (e.getAttributeName (i), valueOf (v, src));
        for (auto* c : e.getChildIterator())
        {
            if (c->isTextElement())
            {
                if (const auto t = c->getText(); t.contains ("aqua-"))
                    c->setText (valueOf (t, src));
            }
            else
                restoreXml (*c, src);
        }
    }

    // how an XML text was written, so it is written the same again
    juce::XmlElement::TextFormat formatOf (const juce::XmlElement& st)
    {
        juce::XmlElement::TextFormat f;
        if (st.getStringAttribute ("lines") == "single") f = f.singleLine();
        if (! st.getBoolAttribute ("header", false)) f = f.withoutHeader();
        return f;
    }

    void noteFormat (juce::XmlElement& st, const juce::String& text)
    {
        st.setAttribute ("header", text.trimStart().startsWith ("<?xml") ? 1 : 0);
        st.setAttribute ("lines", text.containsChar ('\n') ? "multi" : "single");
    }

    //=== JSON (Vita's states): sounds in string values ========================
    // Finds the string values that are sounds and replaces them by references:
    // Vita's "samples" / "samples_stereo" (16-bit PCM: one stereo file),
    // "audio_file" (16-bit PCM) and "wave_data" (a keyframe's 2048 floats: one
    // file per wavetable, its keyframes end to end); and anywhere a value that
    // is a whole sound file. Other long encoded values become numbers.
    // Everything else is copied as it is.
    struct JsonPair
    {
        std::string key;
        size_t valueStart = 0, valueEnd = 0;   // the value's quotes (a string) or its first character
        bool isString = false;
        std::string nameBefore;                // the last "name" seen before it
        double sampleRateBefore = 0;           // the last "sample_rate" seen before it
        int keyframesGroup = -1;               // which "keyframes" array it is in (counted)
    };

    std::vector<JsonPair> jsonPairs (const std::string& s)
    {
        std::vector<JsonPair> pairs;
        auto endOfString = [&s] (size_t open)
        {
            size_t j = open + 1;
            while (j < s.size() && s[j] != '"') { if (s[j] == '\\') ++j; ++j; }
            return j;
        };
        auto skipSpace = [&s] (size_t k) { while (k < s.size() && std::isspace ((unsigned char) s[k])) ++k; return k; };
        std::string lastName;
        double lastRate = 0;
        int group = -1;
        size_t i = 0;
        while (i < s.size())
        {
            if (s[i] != '"') { ++i; continue; }
            const size_t keyEnd = endOfString (i);
            if (keyEnd >= s.size()) break;
            const size_t colon = skipSpace (keyEnd + 1);
            if (colon >= s.size() || s[colon] != ':') { i = keyEnd + 1; continue; }
            JsonPair p;
            p.key = s.substr (i + 1, keyEnd - i - 1);
            p.valueStart = skipSpace (colon + 1);
            if (p.valueStart >= s.size()) break;
            p.nameBefore = lastName;
            p.sampleRateBefore = lastRate;
            if (p.key == "keyframes") ++group;
            p.keyframesGroup = group;
            if (s[p.valueStart] == '"')
            {
                p.isString = true;
                p.valueEnd = endOfString (p.valueStart);
                if (p.key == "name") lastName = s.substr (p.valueStart + 1, p.valueEnd - p.valueStart - 1);
                i = p.valueEnd + 1;
            }
            else
            {
                p.valueEnd = p.valueStart;
                if (p.key == "sample_rate") lastRate = std::atof (s.c_str() + p.valueStart);
                i = p.valueStart;
            }
            pairs.push_back (std::move (p));
        }
        return pairs;
    }

    juce::String extractJson (const juce::String& text, Ctx& ctx)
    {
        const bool vita = ctx.typeId.startsWith ("plugin.vita");
        const std::string s = text.toStdString();
        const auto pairs = jsonPairs (s);
        std::map<size_t, std::string> replace;   // value's opening quote -> its new contents
        auto valueOfPair = [&s] (const JsonPair& p) { return s.substr (p.valueStart + 1, p.valueEnd - p.valueStart - 1); };

        // Vita's wavetables: every keyframe of one "keyframes" array into one file
        if (vita)
        {
            struct Group { std::vector<char> bytes; std::vector<std::pair<size_t, size_t>> frames; Flavour flavour = Flavour::stdB64; std::string name; };
            std::map<int, Group> groups;
            for (const auto& p : pairs)
                if (p.isString && p.key == "wave_data" && p.keyframesGroup >= 0)
                {
                    juce::MemoryBlock b;
                    Flavour f;
                    if (! decodeAny (juce::String (valueOfPair (p)), b, f) || b.getSize() % 4 != 0) continue;
                    auto& g = groups[p.keyframesGroup];
                    if (! g.frames.empty() && g.flavour != f) continue;
                    g.flavour = f;
                    const auto* d = static_cast<const char*> (b.getData());
                    g.frames.push_back ({ p.valueStart, g.bytes.size() / 4 });
                    g.bytes.insert (g.bytes.end(), d, d + b.getSize());
                }
            // a wavetable's name comes after its keyframes ("name" sorts after "groups")
            for (auto& [index, g] : groups)
            {
                const size_t lastFrame = g.frames.empty() ? 0 : g.frames.back().first;
                for (const auto& p : pairs)
                    if (p.valueStart > lastFrame && p.isString && p.key == "name" && p.keyframesGroup == index)
                    {
                        g.name = valueOfPair (p);
                        break;
                    }
            }
            int number = 0;
            for (auto& [index, g] : groups)
            {
                const size_t floats = g.bytes.size() / 4;
                const auto path = ctx.store (makeWav ({ g.bytes.data() }, floats, 32, true, 44100),
                                             "Wavetable " + juce::String (++number) + (g.name.empty() ? juce::String() : " - " + juce::String (g.name)), "wav");
                for (size_t k = 0; k < g.frames.size(); ++k)
                {
                    const size_t start = g.frames[k].second;
                    const size_t end = k + 1 < g.frames.size() ? g.frames[k + 1].second : floats;
                    replace[g.frames[k].first] = Ctx::ref ("f32@" + juce::String ((juce::int64) start), g.flavour, (juce::int64) (end - start), path).toStdString();
                }
            }
        }

        for (size_t k = 0; k < pairs.size(); ++k)
        {
            const auto& p = pairs[k];
            if (! p.isString || replace.count (p.valueStart) > 0) continue;
            const auto value = valueOfPair (p);
            if (value.size() < (size_t) kMinBlob || value.find ('\\') != std::string::npos) continue;
            juce::MemoryBlock bytes;
            Flavour f;
            if (! decodeAny (juce::String (value), bytes, f))
            {
                if (const auto n = readableValue (juce::String (value)); n != juce::String (value))
                    replace[p.valueStart] = n.toStdString();
                continue;
            }
            const auto label = juce::String (p.nameBefore).isNotEmpty() ? juce::String (p.nameBefore) : juce::String (p.key);
            if (const auto ext = audioExtension (bytes); ext.isNotEmpty())
                replace[p.valueStart] = ctx.fileRef (bytes, f, p.key == "audio" ? "Library - " + label : label, ext).toStdString();
            else if (vita && p.key == "audio_file" && bytes.getSize() % 2 == 0)
            {
                int rate = 44100;
                for (size_t q = k + 1; q < pairs.size() && q < k + 12; ++q)
                    if (pairs[q].key == "audio_sample_rate" && ! pairs[q].isString)
                        rate = juce::jlimit (1000, 768000, (int) std::atof (s.c_str() + pairs[q].valueStart));
                const size_t n = bytes.getSize() / 2;
                const auto path = ctx.store (makeWav ({ static_cast<const char*> (bytes.getData()) }, n, 16, false, rate),
                                             "Wavetable audio - " + label, "wav");
                replace[p.valueStart] = Ctx::ref ("i16.0", f, (juce::int64) n, path).toStdString();
            }
            else if (vita && p.key == "samples" && bytes.getSize() % 2 == 0)
            {
                const int rate = juce::jlimit (1000, 768000, p.sampleRateBefore > 0 ? (int) p.sampleRateBefore : 44100);
                const size_t n = bytes.getSize() / 2;
                // its right channel, "samples_stereo", comes next: one stereo file
                juce::MemoryBlock right;
                Flavour rf = Flavour::none;
                const JsonPair* stereo = nullptr;
                if (k + 1 < pairs.size() && pairs[k + 1].key == "samples_stereo" && pairs[k + 1].isString
                    && decodeAny (juce::String (valueOfPair (pairs[k + 1])), right, rf) && right.getSize() == bytes.getSize())
                    stereo = &pairs[k + 1];
                std::vector<const char*> chans { static_cast<const char*> (bytes.getData()) };
                if (stereo != nullptr) chans.push_back (static_cast<const char*> (right.getData()));
                const auto path = ctx.store (makeWav (chans, n, 16, false, rate), "Sample - " + label, "wav");
                replace[p.valueStart] = Ctx::ref ("i16.0", f, (juce::int64) n, path).toStdString();
                if (stereo != nullptr)
                    replace[stereo->valueStart] = Ctx::ref ("i16.1", rf, (juce::int64) n, path).toStdString();
            }
            else if (const auto num = readableValue (juce::String (value)); num != juce::String (value))
                replace[p.valueStart] = num.toStdString();
        }

        std::string out;
        out.reserve (s.size());
        size_t at = 0;
        for (const auto& [quote, contents] : replace)
        {
            size_t close = quote + 1;
            while (close < s.size() && s[close] != '"') { if (s[close] == '\\') ++close; ++close; }
            out += s.substr (at, quote + 1 - at);
            out += contents;
            at = close;   // the closing quote is copied with what follows
        }
        out += s.substr (at);
        return juce::String::fromUTF8 (out.data(), (int) out.size());
    }

    juce::String restoreJson (const juce::String& text, const FileSource& src)
    {
        const std::string s = text.toStdString();
        std::string out;
        out.reserve (s.size() * 2);
        const std::string mark = "\"aqua-";
        size_t i = 0;
        for (;;)
        {
            const auto at = s.find (mark, i);
            if (at == std::string::npos) { out += s.substr (i); break; }
            const auto end = s.find ('"', at + 1);
            if (end == std::string::npos) { out += s.substr (i); break; }
            out += s.substr (i, at + 1 - i);
            out += valueOf (juce::String (s.substr (at + 1, end - at - 1)), src).toStdString();
            out += '"';
            i = end + 1;
        }
        return juce::String::fromUTF8 (out.data(), (int) out.size());
    }

    //=== a ValueTree's binary (GrainFreeze) as typed XML ======================
    std::unique_ptr<juce::XmlElement> valueTreeToXml (const juce::ValueTree& t)
    {
        if (! juce::XmlElement::isValidXmlName (t.getType().toString())) return nullptr;
        auto e = std::make_unique<juce::XmlElement> (t.getType().toString());
        juce::StringArray types;
        for (int i = 0; i < t.getNumProperties(); ++i)
        {
            const auto name = t.getPropertyName (i);
            const auto& v = t.getProperty (name);
            if (! juce::XmlElement::isValidXmlName (name.toString()) || name.toString() == "aqua-types") return nullptr;
            juce::String code, text;
            if (v.isBool())        { code = "b"; text = (bool) v ? "1" : "0"; }
            else if (v.isInt())    { code = "i"; text = juce::String ((int) v); }
            else if (v.isInt64())  { code = "l"; text = juce::String ((juce::int64) v); }
            else if (v.isDouble()) { char buf[40]; std::snprintf (buf, sizeof (buf), "%.17g", (double) v); code = "d"; text = buf; }
            else if (v.isString()) { code = "s"; text = v.toString(); }
            else return nullptr;    // binary, arrays, objects: not this way
            types.add (name.toString() + ":" + code);
            e->setAttribute (name, text);
        }
        e->setAttribute ("aqua-types", types.joinIntoString (","));
        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            auto c = valueTreeToXml (t.getChild (i));
            if (c == nullptr) return nullptr;
            e->addChildElement (c.release());
        }
        return e;
    }

    juce::ValueTree valueTreeFromXml (const juce::XmlElement& e)
    {
        juce::ValueTree t (e.getTagName());
        std::map<juce::String, juce::String> types;
        for (const auto& item : juce::StringArray::fromTokens (e.getStringAttribute ("aqua-types"), ",", {}))
            types[item.upToLastOccurrenceOf (":", false, false)] = item.fromLastOccurrenceOf (":", false, false);
        for (int i = 0; i < e.getNumAttributes(); ++i)
        {
            const auto name = e.getAttributeName (i), text = e.getAttributeValue (i);
            if (name == "aqua-types") continue;
            const auto code = types[name];
            juce::var v;
            if (code == "b")      v = text == "1";
            else if (code == "i") v = text.getIntValue();
            else if (code == "l") v = text.getLargeIntValue();
            else if (code == "d") v = std::strtod (text.toRawUTF8(), nullptr);
            else                  v = text;
            t.setProperty (name, v, nullptr);
        }
        for (auto* c : e.getChildIterator())
            if (! c->isTextElement())
                t.appendChild (valueTreeFromXml (*c), nullptr);
        return t;
    }

    //=== text in bytes ========================================================
    bool isText (const juce::MemoryBlock& b, bool allowTrailingNul)
    {
        auto n = b.getSize();
        const auto* d = static_cast<const unsigned char*> (b.getData());
        if (allowTrailingNul && n > 0 && d[n - 1] == 0) --n;
        if (n == 0) return false;
        for (size_t i = 0; i < n; ++i)
            if (d[i] == 0) return false;
        return juce::CharPointer_UTF8::isValidString (static_cast<const char*> (b.getData()), (int) n);
    }

    juce::String textOf (const juce::MemoryBlock& b, bool& nul)
    {
        auto n = b.getSize();
        nul = n > 0 && static_cast<const char*> (b.getData())[n - 1] == 0;
        if (nul) --n;
        return juce::String::fromUTF8 (static_cast<const char*> (b.getData()), (int) n);
    }

    juce::MemoryBlock bytesOf (const juce::String& text, bool nul)
    {
        juce::MemoryBlock b (text.toRawUTF8(), text.getNumBytesAsUTF8());
        if (nul) b.append ("", 1);
        return b;
    }

    //=== a whole state ========================================================
    // the payload's bytes as a <State>'s contents; false: not a format for it
    bool writeBinary (juce::XmlElement& st, const juce::MemoryBlock& b, Ctx& ctx)
    {
        if (startsWith (b, "VC2!"))
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (b.getData(), (int) b.getSize());
            if (xml == nullptr) return false;
            extractXml (*xml, ctx, {});
            st.setAttribute ("format", "xml");
            st.addChildElement (xml.release());
            return true;
        }
        if (startsWith (b, "PK\x03\x04"))
        {
            juce::ZipFile zip (new juce::MemoryInputStream (b, false), true);
            if (zip.getNumEntries() <= 0) return false;
            st.setAttribute ("format", "zip");
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto* entry = zip.getEntry (i);
                std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                if (entry == nullptr || in == nullptr) return false;
                juce::MemoryBlock data;
                in->readIntoMemoryBlock (data);
                auto* en = st.createNewChildElement ("Entry");
                en->setAttribute ("name", entry->filename);
                if (const auto ext = audioExtension (data); ext.isNotEmpty())
                {
                    en->setAttribute ("file", ctx.fileRef (data, Flavour::none,
                                                           juce::File::createFileWithoutCheckingPath (entry->filename).getFileNameWithoutExtension(), ext));
                }
                else if (isText (data, false))
                {
                    bool nul = false;
                    const auto text = textOf (data, nul);
                    auto xml = text.trimStart().startsWith ("<") ? juce::parseXML (text) : nullptr;
                    if (xml != nullptr)
                    {
                        extractXml (*xml, ctx, {});
                        en->setAttribute ("format", "xml");
                        noteFormat (*en, text);
                        en->addChildElement (xml.release());
                    }
                    else
                    {
                        en->setAttribute ("format", "text");
                        en->addTextElement (text);
                    }
                }
                else
                    en->setAttribute ("base64", juce::Base64::toBase64 (data.getData(), data.getSize()));
            }
            return true;
        }
        if (isText (b, true))
        {
            bool nul = false;
            const auto text = textOf (b, nul);
            const auto t = text.trimStart();
            if (t.startsWith ("{") || t.startsWith ("["))
            {
                st.setAttribute ("format", "json");
                if (nul) st.setAttribute ("nul", 1);
                st.addTextElement (extractJson (text, ctx));
                return true;
            }
            if (t.startsWith ("<"))
                if (auto xml = juce::parseXML (text))
                {
                    extractXml (*xml, ctx, {});
                    st.setAttribute ("format", "xmltext");
                    if (nul) st.setAttribute ("nul", 1);
                    noteFormat (st, text);
                    st.addChildElement (xml.release());
                    return true;
                }
        }
        if (const auto tree = juce::ValueTree::readFromData (b.getData(), b.getSize()); tree.isValid())
            if (auto xml = valueTreeToXml (tree))
            {
                st.setAttribute ("format", "valuetree");
                st.addChildElement (xml.release());
                return true;
            }
        return false;
    }

    bool writeText (juce::XmlElement& st, const juce::String& text, Ctx& ctx)
    {
        const auto t = text.trimStart();
        if (! t.startsWith ("{") && ! t.startsWith ("[") && ! t.startsWith ("<"))
        {
            // plain text (a curve's points, a pattern, a seed): its encoded parts as numbers
            const auto forced = ctx.typeId == "util.drawlfo" ? juce::String ("i16") : juce::String();
            const auto readable = readableValue (text, forced);
            if (readable == text) return false;   // nothing to make readable: as it is
            st.setAttribute ("format", "text");
            st.addTextElement (readable);
            return true;
        }
        if (t.startsWith ("{") || t.startsWith ("["))
        {
            st.setAttribute ("format", "json");
            st.addTextElement (extractJson (text, ctx));
            return true;
        }
        if (t.startsWith ("<"))
            if (auto xml = juce::parseXML (text))
            {
                extractXml (*xml, ctx, {});
                st.setAttribute ("format", "xmltext");
                noteFormat (st, text);
                st.addChildElement (xml.release());
                return true;
            }
        return false;
    }

    // the payload back: bytes (encoding base64) or text (encoding none)
    bool readPayload (const juce::XmlElement& st, const FileSource& src, juce::MemoryBlock& bytes, juce::String& text)
    {
        const auto format = st.getStringAttribute ("format");
        const bool nul = st.getBoolAttribute ("nul", false);
        if (format == "xml")
        {
            const auto* child = st.getFirstChildElement();
            if (child == nullptr) return false;
            juce::XmlElement copy (*child);
            restoreXml (copy, src);
            juce::AudioProcessor::copyXmlToBinary (copy, bytes);
            return true;
        }
        if (format == "xmltext")
        {
            const auto* child = st.getFirstChildElement();
            if (child == nullptr) return false;
            juce::XmlElement copy (*child);
            restoreXml (copy, src);
            text = copy.toString (formatOf (st));
            bytes = bytesOf (text, nul);
            return true;
        }
        if (format == "json")
        {
            text = restoreJson (st.getAllSubText(), src);
            bytes = bytesOf (text, nul);
            return true;
        }
        if (format == "text")
        {
            text = valueOf (st.getAllSubText(), src);
            bytes = bytesOf (text, nul);
            return true;
        }
        if (format == "valuetree")
        {
            const auto* child = st.getFirstChildElement();
            if (child == nullptr) return false;
            juce::MemoryOutputStream o;
            valueTreeFromXml (*child).writeToStream (o);
            bytes = o.getMemoryBlock();
            return true;
        }
        if (format == "zip")
        {
            juce::ZipFile::Builder zb;
            for (auto* en : st.getChildWithTagNameIterator ("Entry"))
            {
                juce::MemoryBlock data;
                if (en->hasAttribute ("file"))
                {
                    Ref r;
                    if (! parseRef (en->getStringAttribute ("file"), r)) return false;
                    data = dataOf (r, src);
                    if (data.getSize() == 0) continue;   // a missing file: left out
                }
                else if (en->getStringAttribute ("format") == "xml")
                {
                    const auto* child = en->getFirstChildElement();
                    if (child == nullptr) return false;
                    juce::XmlElement copy (*child);
                    restoreXml (copy, src);
                    data = bytesOf (copy.toString (formatOf (*en)), false);
                }
                else if (en->getStringAttribute ("format") == "text")
                    data = bytesOf (en->getAllSubText(), false);
                else
                {
                    juce::MemoryOutputStream m;
                    if (! juce::Base64::convertFromBase64 (m, en->getStringAttribute ("base64"))) return false;
                    data = m.getMemoryBlock();
                }
                zb.addEntry (new juce::MemoryInputStream (data, true), 6, en->getStringAttribute ("name"), juce::Time::getCurrentTime());
            }
            juce::MemoryOutputStream o;
            if (! zb.writeToStream (o, nullptr)) return false;
            bytes = o.getMemoryBlock();
            return true;
        }
        return false;
    }

    bool readImpl (const juce::XmlElement& st, const FileSource& src, juce::String& customState)
    {
        juce::MemoryBlock bytes;
        juce::String text;
        if (! readPayload (st, src, bytes, text)) return false;
        customState = st.getStringAttribute ("prefix")
                      + (st.getStringAttribute ("encoding") == "base64" ? bytes.toBase64Encoding() : text);
        return true;
    }

    // two zips with the same entries holding the same bytes
    bool sameZip (const juce::String& a, const juce::String& b)
    {
        juce::MemoryBlock ma, mb;
        if (! ma.fromBase64Encoding (a) || ! mb.fromBase64Encoding (b)) return false;
        juce::ZipFile za (new juce::MemoryInputStream (ma, false), true), zb (new juce::MemoryInputStream (mb, false), true);
        if (za.getNumEntries() != zb.getNumEntries()) return false;
        for (int i = 0; i < za.getNumEntries(); ++i)
        {
            const int j = zb.getIndexOfFileName (za.getEntry (i)->filename);
            if (j < 0) return false;
            std::unique_ptr<juce::InputStream> ia (za.createStreamForEntry (i)), ib (zb.createStreamForEntry (j));
            if (ia == nullptr || ib == nullptr) return false;
            juce::MemoryBlock da, db;
            ia->readIntoMemoryBlock (da);
            ib->readIntoMemoryBlock (db);
            if (da != db) return false;
        }
        return true;
    }

    // "w=520;h=300;" - a GUI card's size in front of its state
    bool isPrefixItem (const juce::String& s)
    {
        int i = 0;
        while (i < s.length() && i < 8 && juce::CharacterFunctions::isLowerCase (s[i])) ++i;
        if (i == 0 || i >= s.length() || s[i] != '=') return false;
        ++i;
        const int start = i;
        while (i < s.length() && i - start < 12 && (juce::CharacterFunctions::isDigit (s[i]) || s[i] == '.' || s[i] == '-')) ++i;
        return i < s.length() && s[i] == ';';
    }
}

//==============================================================================
juce::String FileSink::add (const juce::MemoryBlock& bytes, const juce::String& base, const juce::String& ext)
{
    // the same bytes again: the same file
    for (auto* m : { &committed, &staged })
        for (const auto& [path, data] : *m)
            if (data.getSize() == bytes.getSize() && data == bytes)
                return path;

    auto stem = base.isNotEmpty() ? base : juce::String ("sound");
    auto path = "samples/" + stem + "." + ext;
    for (int n = 2; usedNames.count (path.toLowerCase()) > 0; ++n)
        path = "samples/" + stem + " " + juce::String (n) + "." + ext;
    usedNames.insert (path.toLowerCase());
    staged[path] = bytes;
    return path;
}

bool FileSink::read (const juce::String& path, juce::MemoryBlock& out) const
{
    for (auto* m : { &committed, &staged })
        if (auto it = m->find (path); it != m->end())
        {
            out = it->second;
            return true;
        }
    return false;
}

void FileSink::commit()
{
    for (auto& [path, data] : staged)
    {
        const auto ext = path.fromLastOccurrenceOf (".", false, false);
        const int level = (ext == "wav" || ext == "aiff") ? 6 : 0;   // FLAC, Ogg, MP3: compressed already
        zip.addEntry (new juce::MemoryInputStream (data, true), level, path, juce::Time::getCurrentTime());
        committed[path] = data;
    }
    staged.clear();
}

void FileSink::discard()
{
    for (const auto& [path, data] : staged)
        usedNames.erase (path.toLowerCase());
    staged.clear();
}

//==============================================================================
std::unique_ptr<juce::XmlElement> writeState (const juce::String& customState, const juce::String& typeId,
                                              int instanceId, FileSink& files)
{
    if (customState.length() < kMinState)
        return nullptr;

    juce::String prefix, rest = customState;
    while (isPrefixItem (rest))
    {
        prefix += rest.upToFirstOccurrenceOf (";", true, false);
        rest = rest.fromFirstOccurrenceOf (";", false, false);
    }

    auto st = std::make_unique<juce::XmlElement> ("State");
    if (prefix.isNotEmpty()) st->setAttribute ("prefix", prefix);
    Ctx ctx { files, juce::String (instanceId) + "_" + shortType (typeId), typeId };

    juce::MemoryBlock bytes;
    bool written = false;
    if (decodeJuce (rest, bytes))
    {
        st->setAttribute ("encoding", "base64");
        written = writeBinary (*st, bytes, ctx);
    }
    else
    {
        st->setAttribute ("encoding", "none");
        written = writeText (*st, rest, ctx);
    }

    // put back together as an import will - from patch.xml's text and the
    // files - and kept only if that gives the very same state
    if (written)
    {
        juce::String back;
        const auto asText = st->toString (juce::XmlElement::TextFormat().withoutHeader());
        if (auto reparsed = juce::parseXML (asText))
            if (readImpl (*reparsed, [&files] (const juce::String& p, juce::MemoryBlock& out) { return files.read (p, out); }, back)
                && (back == customState || (st->getStringAttribute ("format") == "zip" && back.startsWith (prefix)
                                            && sameZip (back.substring (prefix.length()), rest))))
            {
                files.commit();
                return st;
            }
    }
    files.discard();
    return nullptr;
}

bool readState (const juce::XmlElement& state, const FileSource& files, juce::String& customState)
{
    return readImpl (state, files, customState);
}
}
