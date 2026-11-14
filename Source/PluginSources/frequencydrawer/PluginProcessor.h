/*
  ==============================================================================
    FrequencyDrawer -- PluginProcessor.h
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <memory>
#include <cmath>

//==============================================================================
/** A single stamped frequency event (individual sine hit). */
struct DrawnEvent
{
    double time;                    // seconds  [0, 30]
    double frequency;               // Hz       [20, 20000]
    double amplitude;               // linear   (pre-normalised by the harmonic series)
    bool   blurEnabled = false;
    float  blurSecs = 0.0f;
};

//==============================================================================
/** A complete interpolation stroke stored as (time, fundamentalFreq) waypoints. */
struct DrawnPath
{
    std::vector<std::pair<double, double>> waypoints; ///< (time s, fundamental Hz)
    int    numHarmonics = 1;
    double amplitude = 0.25;
    bool   blurEnabled = false;
    float  blurSecs = 0.0f;
};

//==============================================================================
class FrequencyDrawerAudioProcessor;

/** The one thread that renders the drawing into audio. Owned by the
    processor and joined when it goes, so a render can never outlive it (the
    old fire-and-forget threads could, when a card was removed mid-render). */
class FrequencyDrawerRenderThread : public juce::Thread
{
public:
    explicit FrequencyDrawerRenderThread (FrequencyDrawerAudioProcessor& p)
        : juce::Thread ("Frequency Drawer render"), owner (p) {}
    void run() override;
private:
    FrequencyDrawerAudioProcessor& owner;
};

//==============================================================================
class FrequencyDrawerAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr double kDuration = 30.0;
    static constexpr double kFreqMin = 20.0;
    static constexpr double kFreqMax = 20000.0;

    //==========================================================================
    FrequencyDrawerAudioProcessor();
    ~FrequencyDrawerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources()                                       override;
#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()                    const  override;

    const juce::String getName()               const override;
    bool  acceptsMidi()                        const override;
    bool  producesMidi()                       const override;
    bool  isMidiEffect()                       const override;
    double getTailLengthSeconds()              const override;

    int   getNumPrograms()                           override;
    int   getCurrentProgram()                        override;
    void  setCurrentProgram(int index)               override;
    const juce::String getProgramName(int index)     override;
    void  changeProgramName(int index, const juce::String& newName) override;

    /** The whole drawing (every stamped event and every interpolated path),
        the harmonics, blur and engine: saved with the patch. */
    void  getStateInformation(juce::MemoryBlock& destData)          override;
    void  setStateInformation(const void* data, int sizeInBytes)    override;

    //==========================================================================
    // Editor API
    //==========================================================================
    void addEvent(double time, double freq, double amplitude = 0.25,
        bool blurEnabled = false, float blurSecs = 0.0f);
    void addEvents(std::vector<DrawnEvent> newEvents);
    void addPath(DrawnPath path);

    /** Stop playback, silence the audio, and forget the whole drawing. */
    void clearAllEvents();

    std::vector<DrawnEvent> getAllEventsCopy() const;
    std::vector<DrawnPath>  getAllPathsCopy()  const;
    std::atomic<int> drawingVersion { 0 };     // bumped when a state load replaces the drawing

    void requestSeek(double seconds);
    void setPlaying(bool shouldPlay);

    double getPlayheadSeconds() const noexcept { return playheadSeconds_.load(); }
    bool   getIsPlaying()       const noexcept { return playing_.load(); }
    bool   getIsRendering()     const noexcept { return isRendering_.load(); }

    /** Renders what was drawn since the last render and adds it in. */
    void triggerBackgroundRender();

    void  setNumHarmonics(int n) { numHarmonics_.store(juce::jlimit(1, 64, n)); }
    int   getNumHarmonics()  const { return numHarmonics_.load(); }

    void  setBlurStrength(float s)
    {
        const float old = blurStrength_.exchange(s);
        blurEnabled_.store(s > 0.001f);
        // the global decay is in every rendered event: re-render all of it
        if (std::abs(s - old) > 0.001f)
            triggerFullRerender();
    }
    float getBlurStrength()  const { return blurStrength_.load(); }

    //==========================================================================
    // Engine selection
    //  0 = Engine 1  (per-stroke blur, interpolated paths)
    //  1 = Engine 2  (one global decay for every event; paths not heard - default)
    //==========================================================================
    void setEngineMode(int mode);
    int  getEngineMode() const noexcept { return engineMode_.load(); }

    /** Re-renders the whole drawing (blur or engine changed, a state loaded). */
    void triggerFullRerender();

    /** Writes the drawing to a FLAC file (44.1 kHz, 24 bit). */
    bool exportToFlac(const juce::File& file);

private:
    friend class FrequencyDrawerRenderThread;
    void renderWork();      // the render thread's job

    // every event and path drawn, and those not rendered yet
    mutable juce::CriticalSection drawingCS_;
    std::vector<DrawnEvent> allEvents_, newEvents_;
    std::vector<DrawnPath>  allPaths_,  newPaths_;

    std::atomic<bool> fullPending_ { false }, newPending_ { false }, clearPending_ { false };
    std::atomic<bool> isRendering_ { false };

    // the render thread's own: the raw sum of every rendered event (before the soft clip)
    juce::AudioBuffer<float> raw_, play_;
    double rawRate_ = 0.0;

    // finished audio, handed to the audio thread without allocating or freeing there
    juce::CriticalSection                     bufferSwapCS_;
    std::unique_ptr<juce::AudioBuffer<float>> activeBuffer_;    // audio thread
    std::unique_ptr<juce::AudioBuffer<float>> pendingBuffer_;   // render thread -> audio thread (or back, used)
    std::atomic<bool>                         newBufferReady_ { false };
    void publish (int s0, int s1);    // copies play_ to the audio thread

    // Playback state
    std::atomic<bool>   playing_{ false };
    std::atomic<double> playheadSeconds_{ 0.0 };
    std::atomic<bool>   seekPending_{ false };
    std::atomic<double> seekTarget_{ 0.0 };

    std::atomic<double> sampleRate_ { 0.0 };
    int64_t playheadSamples_ = 0;

    std::atomic<int>   numHarmonics_{ 1 };
    std::atomic<float> blurStrength_{ 0.0f };
    std::atomic<bool>  blurEnabled_{ false };
    std::atomic<int>   engineMode_{ 1 };

    /** Adds the events (and, for Engine 1, the paths) into buf; returns the
        sample range they touched. Stops early (false) if the thread is asked
        to exit or the drawing is replaced. */
    bool renderInto (juce::AudioBuffer<float>& buf, const std::vector<DrawnEvent>& evts,
                     const std::vector<DrawnPath>& paths, double sr, int engine,
                     int& s0, int& s1, juce::Thread* thread) const;

    FrequencyDrawerRenderThread renderThread_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyDrawerAudioProcessor)
};
