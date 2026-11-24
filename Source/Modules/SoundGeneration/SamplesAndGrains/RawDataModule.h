#pragma once

#include "SamplerModule.h"

// Raw Data (7.0.0): any file read as sound. The bytes are taken as samples in
// the chosen encoding, byte order and channel count, from Start Offset on, for
// Amount of what follows, at Sample Rate; Detect tries the encodings and picks
// the one that reads most like audio. What comes out plays like the Sampler
// (root C4, Start / End / Cycle, Save Sample): this module is the Sampler with
// a raw reader in front. Sampler's parameters come first, in Sampler's order,
// so its engine reads them by index; the raw settings follow.
class RawDataModule : public SamplerModule
{
public:
    enum RawParam { pEncoding = pSaveFormat + 1, pByteOrder, pChannels, pOffset, pAmount, pRawRate, pDetect, pImport };

    enum Encoding { encS16 = 0, encS24, encS32, encU8, encS8, encF32, encF64, encULaw, encALaw, numEncodings };

    void uiButtonClicked (const juce::String& paramId) override;

    // the decode itself (message thread; tests too). Empty: nothing read.
    struct Settings { int encoding = encS16; bool bigEndian = false; int channels = 1; juce::int64 offset = 0; float amount = 1.0f; };
    static juce::AudioBuffer<float> decode (const juce::MemoryBlock& bytes, const Settings& s, juce::int64 maxFrames);
    // the encoding, byte order and channels that read most like audio
    static Settings detect (const juce::MemoryBlock& bytes, juce::int64 offset);

    // reads the kept bytes with the current settings into the sample
    bool importNow();
    void setRawBytes (juce::MemoryBlock b) { rawBytes = std::move (b); }
    bool hasRawBytes() const { return rawBytes.getSize() > 0; }

    static double rateForIndex (int index);

private:
    void openRawChooser();
    Settings currentSettings() const;
    juce::MemoryBlock rawBytes;   // the file last chosen (not saved with the patch: the decoded sound is)
    std::unique_ptr<juce::FileChooser> rawChooser;
};
