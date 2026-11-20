#pragma once

#include "../../Core/ModuleCore.h"

// Sampler - keyboard-mapped one-shot playback (root fixed at C4). PerVoice:
// each engine voice plays its own copy of the sample; Env In arrives as that
// voice's own envelope signal.
// Inputs: 0 = FM In (Mod, playback-rate mod), 1 = Env In (Mod). Output: 0 = Audio Out.
class SamplerModule : public aquanode::SynthModule
{
public:
    // NB: these indices must match the order of d.params in the descriptor.
    // voices/glide are read through voiceLimit()/glideMillis(), but they still
    // occupy slots 2 and 3, so the new params start at 4.
    enum ParamIndex { pVolume = 0, pLoadSample, pVoices, pGlide, pStart, pEnd, pCycle, pViewZoom, pViewPos,
                      pSaveSample, pSaveAs, pSaveFormat };   // (6.7.12: Save Sample, Save As, Format)

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override;
    void reset() override;
    void blockStart() override;

    void processVoiceSample (int voice, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;
    void renderVoice (int voice, const aquanode::StereoFrame* inputs,
                      aquanode::StereoFrame* outputs);

    void voiceNoteOn (int voice, int note, bool retrigger) override;
    void voiceVelocity (int voice, float velocity01) override { vel[voice] = velocity01; }
    void voiceNoteOff (int voice) override;
    void voiceReset (int voice) override;
    double voiceTailSeconds() const override { return 0.02; }

    bool usesLoadedSample() const override { return true; }
    void uiButtonClicked (const juce::String& paramId) override
    {
        if (paramId == "loadSample")
            openSampleChooser();
        else if (paramId == "saveSample")
            saveInterval();
    }

    // Save Sample (6.7.12): Start..End, played as the key Save As semitones
    // from C4 plays it - pitch and speed together, as the keys do (+12: an
    // octave up, twice as fast) - written as FLAC (smaller, lossless) or WAV
    void saveInterval();
    // the interval as it would be saved (message thread; tests too); empty: no sample
    juce::AudioBuffer<float> renderInterval (double& rate) const;
    // a windowed-sinc resampler: src[a..b] read speed times as fast, low-passed
    // below the new Nyquist when it speeds up (no aliasing at +36)
    static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& src, int a, int b, double speed);
    static juce::MemoryBlock encode (const juce::AudioBuffer<float>& audio, double rate, bool flac);

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 84; }

    // long files: drum loops are one thing, a whole set or a field recording another
   #if JUCE_ANDROID
    double maxSampleSeconds() const override { return 600.0; }
   #else
    double maxSampleSeconds() const override { return 1200.0; }
   #endif

private:
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    double position [aquanode::kMaxVoices] {};

    // resolved once per sample from the Start/End params, so a voice always
    // sees a consistent window even while the sliders are being dragged
    void windowFor (int& startIdx, int& endIdx, int total) const;
    double rate     [aquanode::kMaxVoices] {};
    float  gateLvl  [aquanode::kMaxVoices] {};
    bool   gateOn   [aquanode::kMaxVoices] {};
    float  vel      [aquanode::kMaxVoices] {};

    std::unique_ptr<juce::FileChooser> saveChooser;
    std::shared_ptr<const juce::AudioBuffer<float>> latchedSample;
    double latchedSourceRate { 44100.0 };

    static constexpr double rootHz = 261.6255653005986;   // C4 (MIDI note 60)
    static constexpr double gateRampSeconds = 0.003;
};

// Multi Sample - up to eight audio files played side by side from one common
// position, and a Track knob for which one goes out (or all of them). For
// comparing takes or renders, for stems, or as a test-signal player: switch
// the track while it plays and it stays in place. No sockets but Audio Out.
// The files are saved with the patch.
class MultiSampleModule : public aquanode::SynthModule
{
public:
    static constexpr int kSlots = 8;
    enum ParamIndex { pPlaying = 0, pLoop, pTrack, pLevel, pRestart };

    struct Slot
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double rate { 48000.0 };
        juce::String name;
        double seconds() const { return audio != nullptr && rate > 0.0 ? audio->getNumSamples() / rate : 0.0; }
    };

    void prepare (double sr) override { sampleRate = juce::jmax (1000.0, sr); }
    void blockStart() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;
    void uiButtonClicked (const juce::String& paramId) override;
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return kSlots * 26 + 14; }
    double maxSampleSeconds() const override { return 600.0; }

    // saved with the patch: the files one after the other as one stereo buffer, and who is who
    bool hasAudioToSave() const override { return true; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override;
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override;
    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

    // message thread
    Slot getSlot (int index) const;
    void setSlot (int index, Slot slot);
    void chooseFile (int index);
    double positionSeconds() const { return uiPosition.load (std::memory_order_relaxed); }
    double lengthSeconds() const;
    int version() const { return slotVersion.load (std::memory_order_acquire); }

private:
    void unpackIfComplete();

    mutable juce::SpinLock slotLock;
    std::array<Slot, kSlots> slots;                                   // guarded by slotLock
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> retired;   // freed here, never on the audio thread
    std::atomic<int> slotVersion { 0 };
    std::shared_ptr<juce::AudioBuffer<float>> packed;                 // from a saved patch, until the index arrives
    juce::String packedIndex;
    std::unique_ptr<juce::FileChooser> chooser;

    // audio thread
    std::array<Slot, kSlots> playing;
    int seenVersion { -1 };
    double sampleRate { 48000.0 }, position { 0.0 }, length { 0.0 };
    std::atomic<bool> restart { false };
    std::atomic<double> uiPosition { 0.0 };
};
