#pragma once
#include <JuceHeader.h>

//==============================================================================
// Simple transposed-direct-form-II biquad.
//==============================================================================
struct Biquad
{
    float b0{}, b1{}, b2{}, a1{}, a2{};
    float z1{}, z2{};

    inline float process(float x) noexcept
    {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setBandpass(float fc, float Q, float sr) noexcept
    {
        fc = juce::jlimit(20.f, sr * 0.499f, fc);
        Q = juce::jmax(0.05f, Q);

        const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
        const float sinW0 = std::sin(w0);
        const float cosW0 = std::cos(w0);
        const float alpha = sinW0 / (2.f * Q);
        const float inv = 1.f / (1.f + alpha);

        b0 = alpha * inv;
        b1 = 0.f;
        b2 = -alpha * inv;
        a1 = (-2.f * cosW0) * inv;
        a2 = (1.f - alpha) * inv;
    }

    void reset() noexcept { z1 = z2 = 0.f; }
};

//==============================================================================
struct VocoderBand
{
    static constexpr int kMaxOrder = 4;

    Biquad modFilter[kMaxOrder];
    Biquad carFilterL[kMaxOrder];
    Biquad carFilterR[kMaxOrder];

    float  envelope = 0.f;
    float  centerFreq = 440.f;

    std::atomic<float> displayLevel{ 0.f };
};

//==============================================================================
class VocodeAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxBands = 256;

    VocodeAudioProcessor();
    ~VocodeAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vocode"; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()  override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> bandLevels[kMaxBands];
    float              bandwidthCurve[kMaxBands];
    float              bandVolumeCurve[kMaxBands];

    std::atomic<bool>  isAutoVocoding{ false };

    // Called (on message thread) after setStateInformation restores state,
    // so the editor can sync palette, display curves, etc.
    std::function<void()> onStateRestored;

    // A band's Q: sized to the spacing of the bands (neighbours meet at Width
    // 1), times the drawn band width (BW curve: 0.5 = as set, 0 narrower, 1
    // wider), divided by Width.
    float bandQ(int band, bool chromatic, float width) const noexcept
    {
        const float lo = juce::jmax(20.f, currentFreqStart);       // as rebuildFilters spaces them
        const float hi = juce::jmax(lo + 1.f, currentFreqEnd);
        const float ratio = chromatic ? std::pow(2.f, 1.f / 12.f)
                          : currentNumBands > 1 ? std::pow(hi / lo, 1.f / (float)(currentNumBands - 1))
                                                : 2.f;
        const float meet  = 1.f / (std::sqrt(ratio) - 1.f / std::sqrt(ratio));
        const float qMult = juce::jmap(juce::jlimit(0.f, 1.f, bandwidthCurve[band]), 0.f, 1.f, 2.f, 0.5f);
        return juce::jmax(0.1f, meet * (qMult / 1.25f) / juce::jmax(0.05f, width));
    }

    // A band's gain from its envelope: as it is, rounding off toward 1 above
    // 0.8 (the first versions cut it hard at 1, flattening loud voices)
    static float bandGain(float x) noexcept
    {
        constexpr float knee = 0.8f;
        return x <= knee ? x : knee + (1.f - knee) * std::tanh((x - knee) / (1.f - knee));
    }

    static constexpr float kEnvSens   = 4.0f;   // band envelope -> gain (15 at first: most bands sat at the cut)
    static constexpr float kOutMakeup = 3.0f;   // ... and the level that gives back

private:
    void rebuildFilters();

    double currentSampleRate   = 44100.0;
    int    currentNumBands     = 16;
    bool   currentChromaticLock = false;
    float  currentFreqStart    = 80.f;
    float  currentFreqEnd      = 12000.f;

    float  attackCoeff  = 0.f;
    float  releaseCoeff = 0.f;
    float  compEnv      = 0.f;

    // Per-band long-term level average used by the Normalize feature.
    float  bandNormAvg[kMaxBands]{};

    VocoderBand bands[kMaxBands];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocodeAudioProcessor)
};
