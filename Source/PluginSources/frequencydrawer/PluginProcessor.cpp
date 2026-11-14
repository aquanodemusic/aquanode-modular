/*
  ==============================================================================
    FrequencyDrawer - PluginProcessor.cpp
  ==============================================================================

  Every drawn dot is a sine that starts at its time and fades out at a set
  rate; an interpolated stroke is a run of such dots 5 ms apart. They are all
  independent, so the drawing is rendered by adding each one into a 30-second
  buffer over its own lifetime (a complex rotation per sample, no sin(), no
  oscillator pool to scan), and a new stroke only adds its own dots - the
  full drawing is re-rendered only when the blur or the engine changes, or a
  saved drawing is loaded. A soft clip (tanh) turns the sum into what plays.

  ENGINE MODES
    0 = Engine 1  per-stroke blur; interpolated paths
    1 = Engine 2  one global decay for every dot; paths are not heard
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void FrequencyDrawerRenderThread::run()
{
    while (! threadShouldExit())
    {
        owner.renderWork();
        wait (-1);
    }
}

//==============================================================================
FrequencyDrawerAudioProcessor::FrequencyDrawerAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    renderThread_.startThread (juce::Thread::Priority::low);
}

FrequencyDrawerAudioProcessor::~FrequencyDrawerAudioProcessor()
{
    renderThread_.signalThreadShouldExit();
    renderThread_.notify();
    renderThread_.stopThread (10000);
}

//==============================================================================
const juce::String FrequencyDrawerAudioProcessor::getName()          const { return JucePlugin_Name; }
bool   FrequencyDrawerAudioProcessor::acceptsMidi()                  const { return false; }
bool   FrequencyDrawerAudioProcessor::producesMidi()                 const { return false; }
bool   FrequencyDrawerAudioProcessor::isMidiEffect()                 const { return false; }
double FrequencyDrawerAudioProcessor::getTailLengthSeconds()         const { return 0.0; }
int    FrequencyDrawerAudioProcessor::getNumPrograms() { return 1; }
int    FrequencyDrawerAudioProcessor::getCurrentProgram() { return 0; }
void   FrequencyDrawerAudioProcessor::setCurrentProgram(int) {}
const juce::String FrequencyDrawerAudioProcessor::getProgramName(int) { return {}; }
void   FrequencyDrawerAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void FrequencyDrawerAudioProcessor::prepareToPlay(double sampleRate, int)
{
    if (std::abs (sampleRate - sampleRate_.load()) > 0.5)
    {
        sampleRate_.store (sampleRate);
        triggerFullRerender();
    }
}

void FrequencyDrawerAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FrequencyDrawerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

//==============================================================================
// processBlock - plays the rendered buffer; no synthesis here
//==============================================================================
void FrequencyDrawerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // pick up a new render: swap, so nothing is allocated or freed here
    if (newBufferReady_.load())
    {
        const juce::ScopedTryLock lk (bufferSwapCS_);
        if (lk.isLocked() && newBufferReady_.exchange (false))
            std::swap (activeBuffer_, pendingBuffer_);
    }

    if (! playing_.load()) return;

    const int    numSamples = buffer.getNumSamples();
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : sampleRate_.load();
    if (sr <= 0.0) return;

    if (seekPending_.exchange(false))
    {
        const double pos = seekTarget_.load();
        playheadSamples_ = static_cast<int64_t>(pos * sr);
        playheadSeconds_.store(pos);
    }

    if (activeBuffer_ && activeBuffer_->getNumSamples() > 0)
    {
        const int total = activeBuffer_->getNumSamples();
        const int startSmp = static_cast<int>(juce::jmin(static_cast<int64_t>(total), playheadSamples_));
        const int toCopy = juce::jmin(numSamples, total - startSmp);
        if (toCopy > 0)
            for (int ch = 0; ch < juce::jmin(buffer.getNumChannels(), 2); ++ch)
                buffer.copyFrom(ch, 0, *activeBuffer_, juce::jmin(ch, activeBuffer_->getNumChannels() - 1), startSmp, toCopy);
    }

    playheadSamples_ += numSamples;
    const double newPos = static_cast<double>(playheadSamples_) / sr;
    playheadSeconds_.store(newPos);
    if (newPos >= kDuration)
    {
        playing_.store(false);
        playheadSamples_ = static_cast<int64_t>(kDuration * sr);
        playheadSeconds_.store(kDuration);
    }
}

//==============================================================================
bool FrequencyDrawerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* FrequencyDrawerAudioProcessor::createEditor()
{
    return new FrequencyDrawerAudioProcessorEditor(*this);
}

//==============================================================================
void FrequencyDrawerAudioProcessor::setEngineMode(int mode)
{
    const int clamped = juce::jlimit(0, 1, mode);
    if (engineMode_.exchange (clamped) != clamped)
        triggerFullRerender();      // the two engines render the same drawing differently
}

//==============================================================================
// Editor-facing API
//==============================================================================
static void insertSorted (std::vector<DrawnEvent>& v, const DrawnEvent& e)
{
    auto it = std::lower_bound (v.begin(), v.end(), e,
                                [] (const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
    v.insert (it, e);
}

void FrequencyDrawerAudioProcessor::addEvent(double time, double freq, double amplitude, bool blurEnabled, float blurSecs)
{
    if (time < 0.0 || time > kDuration) return;
    DrawnEvent evt{ time, juce::jlimit(kFreqMin, kFreqMax, freq), amplitude, blurEnabled, blurSecs };
    const juce::ScopedLock lk (drawingCS_);
    insertSorted (allEvents_, evt);
    newEvents_.push_back (evt);
}

void FrequencyDrawerAudioProcessor::addEvents(std::vector<DrawnEvent> events)
{
    const juce::ScopedLock lk (drawingCS_);
    for (auto& e : events)
    {
        if (e.time < 0.0 || e.time > kDuration) continue;
        e.frequency = juce::jlimit (kFreqMin, kFreqMax, e.frequency);
        insertSorted (allEvents_, e);
        newEvents_.push_back (e);
    }
}

void FrequencyDrawerAudioProcessor::addPath(DrawnPath path)
{
    const juce::ScopedLock lk (drawingCS_);
    allPaths_.push_back (path);
    newPaths_.push_back (std::move (path));
}

void FrequencyDrawerAudioProcessor::clearAllEvents()
{
    {
        const juce::ScopedLock lk (drawingCS_);
        allEvents_.clear(); newEvents_.clear();
        allPaths_.clear();  newPaths_.clear();
    }
    playing_.store(false);
    seekTarget_.store (0.0);
    seekPending_.store (true);
    playheadSeconds_.store(0.0);
    clearPending_.store (true);
    fullPending_.store (false);
    renderThread_.notify();
}

std::vector<DrawnEvent> FrequencyDrawerAudioProcessor::getAllEventsCopy() const
{
    const juce::ScopedLock lk (drawingCS_);
    return allEvents_;
}

std::vector<DrawnPath> FrequencyDrawerAudioProcessor::getAllPathsCopy() const
{
    const juce::ScopedLock lk (drawingCS_);
    return allPaths_;
}

void FrequencyDrawerAudioProcessor::requestSeek(double seconds)
{
    const double clamped = juce::jlimit(0.0, kDuration, seconds);
    seekTarget_.store(clamped);
    seekPending_.store(true);
    playheadSeconds_.store(clamped);
}

void FrequencyDrawerAudioProcessor::setPlaying(bool shouldPlay)
{
    if (shouldPlay && playheadSeconds_.load() >= kDuration)
        requestSeek(0.0);
    playing_.store(shouldPlay);
}

void FrequencyDrawerAudioProcessor::triggerBackgroundRender()
{
    newPending_.store (true);
    renderThread_.notify();
}

void FrequencyDrawerAudioProcessor::triggerFullRerender()
{
    fullPending_.store (true);
    renderThread_.notify();
}

//==============================================================================
// rendering
//==============================================================================
bool FrequencyDrawerAudioProcessor::renderInto (juce::AudioBuffer<float>& buf, const std::vector<DrawnEvent>& evts,
                                                const std::vector<DrawnPath>& paths, double sr, int engine,
                                                int& s0, int& s1, juce::Thread* thread) const
{
    const int total = buf.getNumSamples();
    float* out = buf.getWritePointer (0);

    const float bt = blurStrength_.load();
    const double defDecaySec = (blurEnabled_.load() && bt > 0.05f) ? (double) bt : 0.04;
    const double defDm = std::exp (-5.0 / (juce::jmax (0.01, defDecaySec) * sr));

    auto addOne = [&] (const DrawnEvent& e, double dm)
    {
        // the sample it starts on: the first at or after its time
        const int start = (int) std::ceil (e.time * sr);
        if (start >= total || e.amplitude <= 1.0e-7) return;
        // it lasts until its amplitude falls under 1e-7, as the old oscillators did
        const int len = (int) juce::jmin ((double) (total - start), std::ceil (std::log (e.amplitude / 1.0e-7) / -std::log (dm)) + 1.0);
        const double w = juce::MathConstants<double>::twoPi * e.frequency / sr;
        const double cr = dm * std::cos (w), ci = dm * std::sin (w);
        double zr = e.amplitude, zi = 0.0;     // amplitude * dm^k * e^(i k w); the sine is its imaginary part
        float* o = out + start;
        for (int k = 0; k < len; ++k)
        {
            o[k] += (float) zi;
            const double nr = zr * cr - zi * ci;
            zi = zr * ci + zi * cr;
            zr = nr;
        }
        s0 = juce::jmin (s0, start);
        s1 = juce::jmax (s1, start + len);
    };

    int n = 0;
    for (const auto& e : evts)
    {
        // Engine 1 honours a stroke's own blur; Engine 2 fades every dot the same
        double dm = defDm;
        if (engine == 0 && e.blurEnabled && e.blurSecs > 0.05f)
            dm = std::exp (-5.0 / ((double) e.blurSecs * sr));
        addOne (e, dm);
        if ((++n & 255) == 0 && thread != nullptr && (thread->threadShouldExit() || clearPending_.load()))
            return false;
    }

    if (engine == 0)
    {
        for (const auto& path : paths)
        {
            if (path.waypoints.size() < 2) continue;
            const double stepSec = 0.005;   // 5 ms between the stroke's dots
            const int nHarm = juce::jlimit (1, 64, path.numHarmonics);
            const double baseAmp = path.amplitude / (double) nHarm;
            double dm = defDm;
            if (path.blurEnabled && path.blurSecs > 0.05f)
                dm = std::exp (-5.0 / ((double) path.blurSecs * sr));
            for (size_t wi = 0; wi + 1 < path.waypoints.size(); ++wi)
            {
                const auto& [t0, f0] = path.waypoints[wi];
                const auto& [t1, f1] = path.waypoints[wi + 1];
                const double span = t1 - t0;
                if (span <= 0.0) continue;
                const int steps = juce::jmax (1, (int) (span / stepSec));
                for (int si = 0; si < steps; ++si)
                {
                    const double alpha = (double) si / steps;
                    const double t = t0 + alpha * span, f = f0 + alpha * (f1 - f0);
                    for (int h = 1; h <= nHarm; ++h)
                        addOne ({ t, juce::jlimit (kFreqMin, kFreqMax, f * h), baseAmp / h, path.blurEnabled, path.blurSecs }, dm);
                }
                if (thread != nullptr && (thread->threadShouldExit() || clearPending_.load()))
                    return false;
            }
        }
    }
    return true;
}

void FrequencyDrawerAudioProcessor::publish (int s0, int s1)
{
    const int total = play_.getNumSamples();
    s0 = juce::jlimit (0, total, s0);
    s1 = juce::jlimit (0, total, s1);
    // the soft clip, only where something changed
    const float* r = raw_.getReadPointer (0);
    float* L = play_.getWritePointer (0);
    float* R = play_.getWritePointer (1);
    for (int i = s0; i < s1; ++i)
        L[i] = R[i] = std::tanh (r[i] * 0.6f) * 0.9f;

    const juce::ScopedLock lk (bufferSwapCS_);
    // reuse the buffer the audio thread handed back (or the one it never took)
    if (pendingBuffer_ == nullptr || pendingBuffer_->getNumSamples() != total)
        pendingBuffer_ = std::make_unique<juce::AudioBuffer<float>> (2, total);
    pendingBuffer_->makeCopyOf (play_, true);
    newBufferReady_.store (true);
}

void FrequencyDrawerAudioProcessor::renderWork()
{
    while (! renderThread_.threadShouldExit())
    {
        const double sr = sampleRate_.load();
        if (sr <= 0.0) return;
        const int total = (int) (kDuration * sr);

        if (clearPending_.exchange (false) || rawRate_ != sr || raw_.getNumSamples() != total)
        {
            raw_.setSize (1, total);  raw_.clear();
            play_.setSize (2, total); play_.clear();
            const bool sizeChanged = rawRate_ != sr;
            rawRate_ = sr;
            isRendering_.store (true);
            publish (0, 0);
            isRendering_.store (false);
            if (sizeChanged) fullPending_.store (true);
        }

        std::vector<DrawnEvent> evts;
        std::vector<DrawnPath> paths;
        bool full = false;
        if (fullPending_.exchange (false))
        {
            full = true;
            newPending_.store (false);
            const juce::ScopedLock lk (drawingCS_);
            evts = allEvents_;
            paths = allPaths_;
            newEvents_.clear();
            newPaths_.clear();
        }
        else if (newPending_.exchange (false))
        {
            const juce::ScopedLock lk (drawingCS_);
            evts.swap (newEvents_);
            paths.swap (newPaths_);
        }
        else
            return;

        isRendering_.store (true);
        int s0 = total, s1 = 0;
        if (full)
        {
            raw_.clear();
            s0 = 0; s1 = total;
        }
        std::sort (evts.begin(), evts.end(), [] (const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });
        const bool done = renderInto (raw_, evts, paths, sr, engineMode_.load(), s0, s1, &renderThread_);
        if (done && ! clearPending_.load() && s1 > s0)
            publish (s0, s1);
        isRendering_.store (false);
    }
}

//==============================================================================
// state: the drawing itself
//==============================================================================
void FrequencyDrawerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("FrequencyDrawer");
    xml.setAttribute ("harmonics", getNumHarmonics());
    xml.setAttribute ("blur", (double) getBlurStrength());
    xml.setAttribute ("engine", getEngineMode());

    const auto evts = getAllEventsCopy();
    {
        juce::MemoryOutputStream mo;
        for (const auto& e : evts)
        {
            mo.writeDouble (e.time); mo.writeDouble (e.frequency); mo.writeDouble (e.amplitude);
            mo.writeBool (e.blurEnabled); mo.writeFloat (e.blurSecs);
        }
        auto* ev = xml.createNewChildElement ("Events");
        ev->setAttribute ("count", (int) evts.size());
        ev->addTextElement (mo.getMemoryBlock().toBase64Encoding());
    }
    for (const auto& p : getAllPathsCopy())
    {
        juce::MemoryOutputStream mo;
        for (const auto& [t, f] : p.waypoints) { mo.writeDouble (t); mo.writeDouble (f); }
        auto* pe = xml.createNewChildElement ("Path");
        pe->setAttribute ("harmonics", p.numHarmonics);
        pe->setAttribute ("amplitude", p.amplitude);
        pe->setAttribute ("blur", p.blurEnabled);
        pe->setAttribute ("blurSecs", (double) p.blurSecs);
        pe->addTextElement (mo.getMemoryBlock().toBase64Encoding());
    }
    copyXmlToBinary (xml, destData);
}

void FrequencyDrawerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("FrequencyDrawer"))
        return;

    std::vector<DrawnEvent> evts;
    std::vector<DrawnPath> paths;
    if (auto* ev = xml->getChildByName ("Events"))
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (ev->getAllSubText().trim()))
        {
            juce::MemoryInputStream mi (mb, false);
            const int count = ev->getIntAttribute ("count");
            for (int i = 0; i < count && ! mi.isExhausted(); ++i)
            {
                DrawnEvent e {};
                e.time = mi.readDouble(); e.frequency = mi.readDouble(); e.amplitude = mi.readDouble();
                e.blurEnabled = mi.readBool(); e.blurSecs = mi.readFloat();
                evts.push_back (e);
            }
        }
    }
    for (auto* pe : xml->getChildWithTagNameIterator ("Path"))
    {
        DrawnPath p;
        p.numHarmonics = pe->getIntAttribute ("harmonics", 1);
        p.amplitude = pe->getDoubleAttribute ("amplitude", 0.25);
        p.blurEnabled = pe->getBoolAttribute ("blur");
        p.blurSecs = (float) pe->getDoubleAttribute ("blurSecs");
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (pe->getAllSubText().trim()))
        {
            juce::MemoryInputStream mi (mb, false);
            while (mi.getNumBytesRemaining() >= 16)
            {
                const double t = mi.readDouble();
                const double f = mi.readDouble();
                p.waypoints.push_back ({ t, f });
            }
        }
        paths.push_back (std::move (p));
    }
    std::sort (evts.begin(), evts.end(), [] (const DrawnEvent& a, const DrawnEvent& b) { return a.time < b.time; });

    setNumHarmonics (xml->getIntAttribute ("harmonics", 1));
    blurStrength_.store ((float) xml->getDoubleAttribute ("blur", 0.0));
    blurEnabled_.store (blurStrength_.load() > 0.001f);
    engineMode_.store (juce::jlimit (0, 1, xml->getIntAttribute ("engine", 1)));
    {
        const juce::ScopedLock lk (drawingCS_);
        allEvents_ = std::move (evts);
        allPaths_ = std::move (paths);
        newEvents_.clear();
        newPaths_.clear();
    }
    ++drawingVersion;
    triggerFullRerender();
}

//==============================================================================
// FLAC export
//==============================================================================
bool FrequencyDrawerAudioProcessor::exportToFlac(const juce::File& file)
{
    constexpr double targetSR = 44100.0;
    const auto evts = getAllEventsCopy();
    const auto paths = getAllPathsCopy();
    juce::AudioBuffer<float> raw (1, (int) (kDuration * targetSR));
    raw.clear();
    int s0 = 0, s1 = 0;
    renderInto (raw, evts, paths, targetSR, engineMode_.load(), s0, s1, nullptr);

    juce::AudioBuffer<float> buf (2, raw.getNumSamples());
    for (int i = 0; i < raw.getNumSamples(); ++i)
    {
        const float v = std::tanh (raw.getSample (0, i) * 0.6f) * 0.9f;
        buf.setSample (0, i, v);
        buf.setSample (1, i, v);
    }

    juce::FlacAudioFormat fmt;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr) return false;
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (targetSR).withNumChannels (2)
                    .withBitsPerSample (24).withQualityOptionIndex (8);
    auto writer = fmt.createWriterFor (stream, opts);
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequencyDrawerAudioProcessor();
}
