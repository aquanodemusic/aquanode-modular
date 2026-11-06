#pragma once
//==============================================================================
// Cove Groove - a lane's own sample (the Sample sound): reading a sound file,
// keeping it in the state (16-bit FLAC as base64, WAV where FLAC will not take
// the rate) and the volume curve's ready-made shapes. Message thread.
//==============================================================================
#include <JuceHeader.h>
#include "CgVoices.h"

namespace covegroove::samples
{
    constexpr double kMaxSeconds = 10.0;       // a longer file is cut here

    // the frames of a buffer (its first two channels; a stereo file whose
    // sides are the same is kept as mono). trim: the silence before and after
    // the sound left out, so it starts on its attack and the curve spans it.
    inline std::shared_ptr<const SampleData> fromBuffer (const juce::AudioBuffer<float>& b, double rate, bool trim)
    {
        const int ch = juce::jmin (2, b.getNumChannels());
        const int n = b.getNumSamples();
        if (ch < 1 || n < 2 || ! (rate >= 1000.0 && rate <= 400000.0)) return nullptr;
        auto loudness = [&b, ch] (int i)
        {
            float m = 0.0f;
            for (int c = 0; c < ch; ++c) m = std::max (m, std::abs (b.getSample (c, i)));
            return std::isfinite (m) ? m : 0.0f;
        };
        int start = 0, end = n;
        if (trim)
        {
            constexpr float floor = 0.0003f;                       // about -70 dB
            while (start < n && loudness (start) < floor) ++start;
            while (end > start && loudness (end - 1) < floor) --end;
            if (end - start < 2) return nullptr;                    // nothing to hear in it
            end = std::min (n, end + (int) (0.005 * rate));         // the last of the tail
        }
        auto d = std::make_shared<SampleData>();
        d->rate = rate;
        const int len = end - start;
        auto copy = [len, start] (const float* src, std::vector<float>& dst)
        {
            dst.resize ((size_t) len);
            for (int i = 0; i < len; ++i)
            {
                const float x = src[start + i];
                dst[(size_t) i] = std::isfinite (x) ? clampf (x, -4.0f, 4.0f) : 0.0f;
            }
        };
        copy (b.getReadPointer (0), d->left);
        if (ch > 1)
        {
            copy (b.getReadPointer (1), d->right);
            bool same = true;
            for (int i = 0; i < len && same; ++i) same = std::abs (d->left[(size_t) i] - d->right[(size_t) i]) < 1.0e-6f;
            if (same) d->right.clear();
        }
        return d;
    }

    inline juce::AudioBuffer<float> toBuffer (const SampleData& d)
    {
        juce::AudioBuffer<float> b (d.stereo() ? 2 : 1, std::max (1, d.frames()));
        b.clear();
        if (d.frames() > 0)
        {
            b.copyFrom (0, 0, d.left.data(), d.frames());
            if (d.stereo()) b.copyFrom (1, 0, d.right.data(), d.frames());
        }
        return b;
    }

    // a sound file: WAV, AIFF, FLAC, Ogg, MP3. Read by what it is, not by its
    // name: every format is asked in turn, the ones that know their header
    // first (an MP3 reader would take a WAV called .mp3 for noise), so a file
    // whose extension is missing or wrong still loads.
    inline std::shared_ptr<const SampleData> readFile (const juce::File& file)
    {
        if (! file.existsAsFile()) return nullptr;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file.createInputStream()));
        if (r == nullptr || r->lengthInSamples < 2 || ! (r->sampleRate >= 1000.0)) return nullptr;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (kMaxSeconds * r->sampleRate));
        const int ch = juce::jlimit (1, 2, (int) r->numChannels);
        juce::AudioBuffer<float> b (ch, n);
        b.clear();
        r->read (&b, 0, n, 0, true, ch > 1);
        return fromBuffer (b, r->sampleRate, true);
    }

    // the sample as text for the state
    inline juce::String encode (const SampleData& d)
    {
        if (d.frames() < 2) return {};
        const auto b = toBuffer (d);
        juce::MemoryBlock mb;
        auto writeWith = [&] (juce::AudioFormat& format)
        {
            mb.reset();
            auto* stream = new juce::MemoryOutputStream (mb, false);
            std::unique_ptr<juce::AudioFormatWriter> w (format.createWriterFor (stream, d.rate, (unsigned int) b.getNumChannels(), 16, {}, 0));
            if (w == nullptr) { delete stream; return false; }
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            w.reset();
            return mb.getSize() > 0;
        };
        juce::FlacAudioFormat flac;
        juce::WavAudioFormat wav;
        if (! writeWith (flac) && ! writeWith (wav)) return {};
        return juce::Base64::toBase64 (mb.getData(), mb.getSize());
    }

    inline std::shared_ptr<const SampleData> decode (const juce::String& text)
    {
        if (text.isEmpty()) return nullptr;
        juce::MemoryOutputStream decoded;
        if (! juce::Base64::convertFromBase64 (decoded, text) || decoded.getDataSize() == 0) return nullptr;
        std::unique_ptr<juce::AudioFormatReader> r;
        {
            juce::FlacAudioFormat flac;
            r.reset (flac.createReaderFor (new juce::MemoryInputStream (decoded.getData(), decoded.getDataSize(), false), true));
        }
        if (r == nullptr)
        {
            juce::WavAudioFormat wav;
            r.reset (wav.createReaderFor (new juce::MemoryInputStream (decoded.getData(), decoded.getDataSize(), false), true));
        }
        if (r == nullptr || r->lengthInSamples < 2) return nullptr;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (kMaxSeconds * r->sampleRate) + 1024);
        const int ch = juce::jlimit (1, 2, (int) r->numChannels);
        juce::AudioBuffer<float> b (ch, n);
        b.clear();
        r->read (&b, 0, n, 0, true, ch > 1);
        return fromBuffer (b, r->sampleRate, false);
    }

    // "0.42 s  stereo  44.1 kHz"
    inline juce::String describe (const SampleData& d)
    {
        return juce::String ((double) d.frames() / d.rate, 2) + " s  " + (d.stereo() ? "stereo" : "mono") + "  "
               + juce::String (d.rate / 1000.0, 1) + " kHz";
    }

    //=== the curve's shapes ====================================================
    constexpr int kNumShapes = 5;
    inline const char* shapeName (int i)
    {
        static const char* n[kNumShapes] = { "Flat", "Fade", "Punch", "Gate", "Swell" };
        return n[std::clamp (i, 0, kNumShapes - 1)];
    }

    inline Curve shape (int i)
    {
        Curve c;
        auto set = [&c] (std::initializer_list<std::pair<float, float>> pts)
        {
            c.n = 0;
            for (auto& p : pts) { c.x[c.n] = p.first; c.y[c.n] = p.second; ++c.n; }
        };
        switch (std::clamp (i, 0, kNumShapes - 1))
        {
            case 1:  set ({ { 0.0f, 1.0f }, { 0.25f, 0.5f }, { 0.6f, 0.15f }, { 1.0f, 0.0f } }); break;                 // Fade
            case 2:  set ({ { 0.0f, 1.0f }, { 0.06f, 1.0f }, { 0.18f, 0.3f }, { 0.5f, 0.08f }, { 1.0f, 0.0f } }); break; // Punch
            case 3:  set ({ { 0.0f, 1.0f }, { 0.4f, 1.0f }, { 0.44f, 0.0f }, { 1.0f, 0.0f } }); break;                   // Gate
            case 4:  set ({ { 0.0f, 0.0f }, { 0.75f, 1.0f }, { 1.0f, 0.0f } }); break;                                   // Swell
            default: set ({ { 0.0f, 1.0f }, { 1.0f, 1.0f } }); break;                                                     // Flat
        }
        return c;
    }
}
