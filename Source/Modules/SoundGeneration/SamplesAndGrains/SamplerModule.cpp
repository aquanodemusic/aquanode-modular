#include "SamplerModule.h"
#include "../../../Platform/AndroidDocuments.h"

using namespace aquanode;

//==============================================================================
// The Sampler's waveform: zoom with the mouse wheel (around the pointer),
// slide along by dragging or with shift+wheel, drag the Start / End markers
// directly, double-click to see the whole file again. Long files draw from a
// peak cache built once per loaded sample.
class SamplerWaveformDisplay : public juce::Component,
                               private juce::Timer
{
public:
    explicit SamplerWaveformDisplay (SynthModule& moduleToShow)
        : module (&moduleToShow)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        auto* m = module.get();
        if (m == nullptr)
            return;

        if (sample == nullptr || sample->getNumSamples() == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("no sample loaded", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int w = juce::jmax (1, getWidth());
        const int h = getHeight() - kBarH;
        const float midY = h * 0.5f;
        const double total = (double) sample->getNumSamples();
        const float* data = sample->getReadPointer (0);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        for (int x = 0; x < w; ++x)
        {
            const auto s0 = (juce::int64) (viewStart + viewLen * x / w);
            const auto s1 = juce::jmax (s0 + 1, (juce::int64) (viewStart + viewLen * (x + 1) / w));
            float mn = 0.0f, mx = 0.0f;
            if (s1 - s0 >= 2 * kPeakBlock && ! peaks.empty())
            {
                for (auto b = s0 / kPeakBlock; b < juce::jmin ((juce::int64) peaks.size(), s1 / kPeakBlock + 1); ++b)
                {
                    mn = juce::jmin (mn, peaks[(size_t) b].first);
                    mx = juce::jmax (mx, peaks[(size_t) b].second);
                }
            }
            else
            {
                for (auto i = s0; i < s1 && i < (juce::int64) total; ++i)
                {
                    mn = juce::jmin (mn, data[i]);
                    mx = juce::jmax (mx, data[i]);
                }
            }
            g.drawVerticalLine (x, midY - mx * (midY - 2.0f), midY - mn * (midY - 2.0f) + 1.0f);
        }

        // dim everything outside the Start..End window
        const float x0 = (float) xOf (juce::jmin (startPos(), endPos()));
        const float x1 = (float) xOf (juce::jmax (startPos(), endPos()));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (0.0f, 0.0f, juce::jlimit (0.0f, (float) w, x0), (float) h);
        if (x1 < (float) w)
            g.fillRect (juce::jmax (0.0f, x1), 0.0f, (float) w - juce::jmax (0.0f, x1), (float) h);

        g.setColour (juce::Colours::aqua.withAlpha (0.9f));
        if (x0 >= 0.0f && x0 <= (float) w) g.fillRect (x0 - 1.0f, 0.0f, 2.0f, (float) h);
        if (x1 >= 0.0f && x1 <= (float) w) g.fillRect (juce::jmin ((float) w - 2.0f, x1 - 1.0f), 0.0f, 2.0f, (float) h);

        // what part of the file is on screen, and how long it is
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (0, h, w, kBarH);
        g.setColour (juce::Colours::aqua.withAlpha (0.55f));
        g.fillRect ((float) (viewStart / total * w), (float) h, juce::jmax (3.0f, (float) (viewLen / total * w)), (float) kBarH);

        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        const double sr = juce::jmax (1.0, rate);
        const auto label = timeText (viewStart / sr) + " - " + timeText ((viewStart + viewLen) / sr)
                         + (viewLen < total - 1.0 ? "" : "  (wheel / Zoom knob: zoom, drag: slide)");
        g.drawText (label, getLocalBounds().withHeight (h).reduced (4, 2), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = Drag::None;
        if (sample == nullptr)
            return;
        const double xs = xOf (startPos()), xe = xOf (endPos());
        if (std::abs (e.x - xs) <= kGrab)      dragging = Drag::Start;
        else if (std::abs (e.x - xe) <= kGrab) dragging = Drag::End;
        else { dragging = Drag::Pan; panFrom = viewStart; }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* m = module.get();
        if (m == nullptr || sample == nullptr)
            return;
        const double total = (double) sample->getNumSamples();
        if (dragging == Drag::Pan)
        {
            viewStart = panFrom - (double) e.getDistanceFromDragStartX() * viewLen / juce::jmax (1, getWidth());
            clampView();
            storeView();
            repaint();
        }
        else if (dragging != Drag::None)
        {
            const double pos = juce::jlimit (0.0, total - 1.0, sampleAt (e.x));
            m->setParameter (dragging == Drag::Start ? "start" : "end", (float) (pos / juce::jmax (1.0, total - 1.0)));
            repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (sample == nullptr) return;
        const bool nearMarker = std::abs (e.x - xOf (startPos())) <= kGrab || std::abs (e.x - xOf (endPos())) <= kGrab;
        setMouseCursor (nearMarker ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (sample == nullptr)
            return;
        const float dy = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        {
            viewStart -= (double) dy * viewLen * 0.5;          // slide along
        }
        else
        {
            const double anchor = sampleAt (e.x);              // zoom around the pointer
            const double factor = std::pow (0.5, (double) dy * 2.0);
            const double minLen = juce::jmin (64.0, (double) sample->getNumSamples());
            viewLen = juce::jlimit (minLen, (double) sample->getNumSamples(), viewLen * factor);
            viewStart = anchor - (double) e.x / juce::jmax (1, getWidth()) * viewLen;
        }
        clampView();
        storeView();
        repaint();
    }

    void mouseMagnify (const juce::MouseEvent& e, float scale) override
    {
        if (sample == nullptr || scale <= 0.0f)
            return;
        const double anchor = sampleAt (e.x);
        viewLen = juce::jlimit (juce::jmin (64.0, (double) sample->getNumSamples()),
                                (double) sample->getNumSamples(), viewLen / (double) scale);
        viewStart = anchor - (double) e.x / juce::jmax (1, getWidth()) * viewLen;
        clampView();
        storeView();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (sample == nullptr) return;
        viewStart = 0.0;
        viewLen = (double) sample->getNumSamples();
        storeView();
        repaint();
    }

private:
    // the view lives in the Zoom / View Pos knobs, so either moves it
    void storeView()
    {
        auto* m = module.get();
        if (m == nullptr || sample == nullptr) return;
        const double zoom = total() / juce::jmax (1.0, viewLen);
        const double room = total() - viewLen;
        lastZoom = (float) juce::jlimit (1.0, 1000.0, zoom);
        lastPos = room > 0.5 ? (float) juce::jlimit (0.0, 1.0, viewStart / room) : 0.0f;
        m->setParameter ("viewZoom", lastZoom);
        m->setParameter ("viewPos", lastPos);
    }
    void loadView()
    {
        auto* m = module.get();
        if (m == nullptr) return;
        lastZoom = juce::jlimit (1.0f, 1000.0f, m->getParameter ("viewZoom"));
        lastPos = juce::jlimit (0.0f, 1.0f, m->getParameter ("viewPos"));
        viewLen = juce::jlimit (juce::jmin (64.0, total()), total(), total() / (double) lastZoom);
        viewStart = (double) lastPos * (total() - viewLen);
        clampView();
    }

    static constexpr int kPeakBlock = 256, kBarH = 4, kGrab = 5;
    enum class Drag { None, Start, End, Pan };

    double total() const { return sample != nullptr ? (double) sample->getNumSamples() : 1.0; }
    double startPos() const { return juce::jlimit (0.0f, 1.0f, module->getParameter ("start")) * (total() - 1.0); }
    double endPos() const   { return juce::jlimit (0.0f, 1.0f, module->getParameter ("end"))   * (total() - 1.0); }
    double xOf (double pos) const { return (pos - viewStart) / juce::jmax (1.0, viewLen) * getWidth(); }
    double sampleAt (int x) const { return viewStart + viewLen * x / juce::jmax (1, getWidth()); }

    void clampView()
    {
        viewLen = juce::jlimit (1.0, total(), viewLen);
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, total() - viewLen), viewStart);
    }

    static juce::String timeText (double secs)
    {
        const int m = (int) (secs / 60.0);
        return juce::String (m) + ":" + juce::String (secs - m * 60.0, 1).paddedLeft ('0', 4);
    }

    void rebuildPeaks()
    {
        peaks.clear();
        if (sample == nullptr) return;
        const int n = sample->getNumSamples(), chans = sample->getNumChannels();
        peaks.resize ((size_t) (n / kPeakBlock + 1), { 0.0f, 0.0f });
        for (int c = 0; c < juce::jmin (1, chans); ++c)
        {
            const float* d = sample->getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                auto& p = peaks[(size_t) (i / kPeakBlock)];
                p.first = juce::jmin (p.first, d[i]);
                p.second = juce::jmax (p.second, d[i]);
            }
        }
    }

    void timerCallback() override
    {
        auto* m = module.get();
        if (m == nullptr)
            return;

        const int counter = m->getSampleChangeCounter();
        if (counter != lastCounter)
        {
            const bool first = lastCounter < 0;
            lastCounter = counter;
            sample = m->getLoadedSample (&rate);
            rebuildPeaks();
            if (first)
                loadView();   // the view the patch was saved with
            else
            {
                viewStart = 0.0;   // a new sample: all of it
                viewLen = total();
                storeView();
            }
            repaint();
        }
        if (m->getParameter ("viewZoom") != lastZoom || m->getParameter ("viewPos") != lastPos)
        {
            loadView();
            repaint();
        }
        const float a = m->getParameter ("start"), b = m->getParameter ("end");
        if (a != lastStart || b != lastEnd)
        {
            lastStart = a;
            lastEnd = b;
            repaint();
        }
    }

    juce::WeakReference<SynthModule> module;
    std::shared_ptr<const juce::AudioBuffer<float>> sample;
    std::vector<std::pair<float, float>> peaks;
    double rate { 44100.0 }, viewStart { 0.0 }, viewLen { 1.0 }, panFrom { 0.0 };
    Drag dragging { Drag::None };
    int lastCounter { -1 };
    float lastStart { -1.0f }, lastEnd { -1.0f }, lastZoom { -1.0f }, lastPos { -1.0f };
};

std::unique_ptr<juce::Component> SamplerModule::createExtraContentComponent()
{
    return std::make_unique<SamplerWaveformDisplay> (*this);
}

//==============================================================================
void SamplerModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void SamplerModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void SamplerModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    position[v] = 0.0;
    rate[v] = 0.0;
    gateLvl[v] = 0.0f;
    gateOn[v] = false;
    vel[v] = 1.0f;
}

void SamplerModule::blockStart()
{
    latchedSample = getLoadedSample (&latchedSourceRate);
}

void SamplerModule::windowFor (int& startIdx, int& endIdx, int total) const
{
    const float a = juce::jlimit (0.0f, 1.0f, param (pStart));
    const float b = juce::jlimit (0.0f, 1.0f, param (pEnd));

    // in double: a float loses whole samples along a 20-minute file
    startIdx = (int) ((double) juce::jmin (a, b) * (double) (total - 1));
    endIdx   = (int) ((double) juce::jmax (a, b) * (double) (total - 1));

    // Start above End is allowed on the sliders (and is a natural thing to do
    // while dragging); it reads as the same span the other way round
    // rather than producing silence or a negative-length loop.
    startIdx = juce::jlimit (0, total - 2, startIdx);
    endIdx   = juce::jlimit (startIdx + 1, total - 1, endIdx);
}

void SamplerModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    juce::ignoreUnused (retrigger);
    // start from the window, not from 0
    if (auto sample = getLoadedSample())
    {
        int a = 0, b = 0;
        if (sample->getNumSamples() >= 2)
        {
            windowFor (a, b, sample->getNumSamples());
            position[v] = (double) a;
        }
        else
            position[v] = 0.0;
    }
    else
        position[v] = 0.0;
    gateLvl[v] = 0.0f;
    gateOn[v] = true;
    glide.noteOn (v, (float) note, isMonoVoice());
}

void SamplerModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gateOn[v] = false;
}

void SamplerModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // voice-steal de-click: a muted voice ramps out over a few ms instead of
    // being cut dead, and a re-used voice ramps back in
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void SamplerModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    outputs[0][0] = 0.0f;
    outputs[0][1] = 0.0f;

    // mono glide slides the playback rate between notes (no-op when polyphonic)
    rate[v] = midiNoteToHz ((double) (glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate) + pitchBendSemitones())) / rootHz;

    if (latchedSample == nullptr || latchedSample->getNumSamples() < 2)
        return;

    const int total = latchedSample->getNumSamples();

    int startIdx = 0, endIdx = total - 1;
    windowFor (startIdx, endIdx, total);

    const bool cycling = param (pCycle) > 0.5f;

    if (position[v] < (double) startIdx)
        position[v] = (double) startIdx;   // window moved under a playing voice

    if (position[v] >= (double) endIdx)
    {
        // Cycle re-triggers the window for as long as the note is held. Once
        // the key is released the loop is allowed to finish, so the release
        // tail of an ADSR on Env In still gets something to fade out.
        if (! (cycling && gateOn[v]))
            return;                // one-shot finished for this voice

        const double span = (double) (endIdx - startIdx);
        if (span < 1.0)
            return;
        position[v] = (double) startIdx + std::fmod (position[v] - (double) startIdx, span);
    }

    const float fmIn = inputs[0][0];                       // modulates playback rate
    const bool envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;

    const int numCh = latchedSample->getNumChannels();
    const float* dataL = latchedSample->getReadPointer (0);
    const float* dataR = latchedSample->getReadPointer (numCh > 1 ? 1 : 0);

    const int i0 = juce::jlimit (0, total - 2, (int) position[v]);
    const float frac = (float) (position[v] - i0);
    const float l = dataL[i0] + (dataL[i0 + 1] - dataL[i0]) * frac;
    const float r = dataR[i0] + (dataR[i0 + 1] - dataR[i0]) * frac;

    const double invSr = 1.0 / sampleRate;
    position[v] += rate[v] * (latchedSourceRate * invSr) * juce::jmax (0.0, 1.0 + (double) fmIn);

    float gain;
    if (envConnected)
    {
        gain = envIn;              // this voice's own envelope
    }
    else
    {
        const float gateStep = (float) (invSr / gateRampSeconds);
        const float target = gateOn[v] ? 1.0f : 0.0f;
        if (gateLvl[v] < target)      gateLvl[v] = juce::jmin (target, gateLvl[v] + gateStep);
        else if (gateLvl[v] > target) gateLvl[v] = juce::jmax (target, gateLvl[v] - gateStep);
        gain = gateLvl[v];
    }

    const float volume = param (pVolume) * vel[v];   // velocity always applies
    outputs[0][0] = l * gain * volume;
    outputs[0][1] = r * gain * volume;
}

//==============================================================================
static ModuleDescriptor samplerDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.sampler";
    d.displayName = "Sampler";
    d.description =
        "Keyboard-mapped sample playback, root fixed at C4, with each voice playing its own copy. "
        "Takes long files (20 minutes, 10 on Android): zoom the waveform with the wheel, slide by dragging, "
        "and drag Start and End right in it. Start and End bound the section that plays, as a fraction of the file. With Cycle off the "
        "window plays once per note; with Cycle on it repeats for as long as the note is held, and "
        "is allowed to finish after release so an ADSR on Env In still has something to fade. "
        "FM In modulates the playback rate; Env In wants an ADSR.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 1;
    d.sockets = {
        modIn ("fmIn",  "FM In"),
        modIn ("envIn", "Env In"),
        midiIn ("addMidiIn", "Add MIDI In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.8f, 1),
        makeButton ("loadSample", "Load Sample", 0, 2)
    ,
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f)
    ,
        makeRotary ("start", "Start", 0.0f, 1.0f, 0.0f, 1),
        makeRotary ("end",   "End",   0.0f, 1.0f, 1.0f, 1),
        makeCombo  ("cycle", "Cycle", { "Off", "On" }, 0, 3, 2),
        // the waveform view (touch screens have no wheel): how far in, and where
        makeRotary ("viewZoom", "Zoom", 1.0f, 1000.0f, 1.0f, 2, "x", true).noMod(),
        makeRotary ("viewPos", "View Pos", 0.0f, 1.0f, 0.0f, 2).noMod(),
        // (6.7.12) Start..End saved as a file, played as a key: Save As semitones from C4
        makeButton ("saveSample", "Save Sample", 0, 2),
        makeRotary ("saveAs", "Save As", -36.0f, 36.0f, 0.0f, 0, " st", false, 1.0f).noMod(),
        makeCombo  ("saveFormat", "Format", { "FLAC", "WAV" }, 0, 1, 2).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SamplerModule, samplerDescriptor)

//==============================================================================
// Save Sample (6.7.12)
//==============================================================================
juce::AudioBuffer<float> SamplerModule::resample (const juce::AudioBuffer<float>& src, int a, int b, double speed)
{
    a = juce::jlimit (0, juce::jmax (0, src.getNumSamples() - 1), a);
    b = juce::jlimit (a, juce::jmax (a, src.getNumSamples() - 1), b);
    const int chans = juce::jmax (1, src.getNumChannels());
    const int inLen = b - a + 1;
    if (std::abs (speed - 1.0) < 1.0e-9)
    {
        juce::AudioBuffer<float> out (chans, inLen);
        for (int c = 0; c < chans; ++c) out.copyFrom (c, 0, src, juce::jmin (c, src.getNumChannels() - 1), a, inLen);
        return out;
    }
    const int outLen = juce::jmax (1, (int) std::floor ((double) (inLen - 1) / speed) + 1);
    juce::AudioBuffer<float> out (chans, outLen);
    // the kernel: a sinc at the lower of the two Nyquists, in a Hann window
    const double cutoff = juce::jmin (1.0, 1.0 / speed) * 0.96;
    const int half = juce::jmax (8, (int) std::ceil ((speed > 1.0 ? 16.0 : 8.0) / cutoff));
    const double pi = juce::MathConstants<double>::pi;
    std::vector<double> taps ((size_t) (2 * half));
    for (int c = 0; c < chans; ++c)
    {
        const float* in = src.getReadPointer (juce::jmin (c, src.getNumChannels() - 1));
        float* o = out.getWritePointer (c);
        for (int j = 0; j < outLen; ++j)
        {
            const double pos = (double) a + (double) j * speed;
            const int centre = (int) std::floor (pos);
            double acc = 0.0;
            for (int k = centre - half + 1; k <= centre + half; ++k)
            {
                if (k < a || k > b) continue;
                const double x = pos - (double) k;
                const double sx = x * cutoff * pi;
                const double sinc = std::abs (sx) < 1.0e-12 ? 1.0 : std::sin (sx) / sx;
                const double w = 0.5 + 0.5 * std::cos (pi * x / (double) half);
                acc += (double) in[k] * sinc * w;
            }
            o[j] = (float) (acc * cutoff);
        }
    }
    return out;
}

juce::AudioBuffer<float> SamplerModule::renderInterval (double& rateOut) const
{
    double srcRate = 44100.0;
    auto sample = getLoadedSample (&srcRate);
    rateOut = srcRate;
    if (sample == nullptr || sample->getNumSamples() < 2)
        return {};
    int a = 0, b = 0;
    windowFor (a, b, sample->getNumSamples());
    const double st = juce::jlimit (-36.0, 36.0, (double) std::round (getParameterBase (pSaveAs)));
    return resample (*sample, a, b, std::pow (2.0, st / 12.0));
}

juce::MemoryBlock SamplerModule::encode (const juce::AudioBuffer<float>& audio, double rate, bool flac)
{
    juce::MemoryBlock data;
    if (audio.getNumSamples() == 0) return data;
    std::unique_ptr<juce::AudioFormat> format;
    if (flac) format = std::make_unique<juce::FlacAudioFormat>();
    else      format = std::make_unique<juce::WavAudioFormat>();
    // (FLAC up to 655 kHz and 24 bits: a high-rate file keeps its rate)
    std::unique_ptr<juce::AudioFormatWriter> w (format->createWriterFor (new juce::MemoryOutputStream (data, false), rate,
                                                                          (unsigned int) audio.getNumChannels(), 24, {}, flac ? 5 : 0));
    if (w == nullptr) return {};
    w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    w.reset();   // (flushes into data)
    return data;
}

void SamplerModule::saveInterval()
{
    auto tell = [] (const juce::String& text)
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Sampler", text);
    };
    if (getLoadedSample() == nullptr) { tell ("Load a sample first."); return; }
    const bool flac = getParameterBase (pSaveFormat) < 0.5f;
    const int st = (int) std::lround (getParameterBase (pSaveAs));
    const auto ext = flac ? juce::String (".flac") : juce::String (".wav");
    const auto name = "Sample " + juce::MidiMessage::getMidiNoteName (60 + st, true, true, 4)
                    + (st == 0 ? juce::String() : " (" + juce::String (st > 0 ? "+" : "") + juce::String (st) + " st)");
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getNonexistentChildFile (name, ext, false);
    saveChooser = std::make_unique<juce::FileChooser> (flac ? "Save the interval as a FLAC file" : "Save the interval as a WAV file",
                                                       suggested, "*" + ext);
    juce::WeakReference<SynthModule> weak (this);
    saveChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [weak, flac, tell] (const juce::FileChooser& fc)
    {
        const auto url = fc.getURLResult();   // (the address: an Android pick is a document, not a path)
        auto* self = dynamic_cast<SamplerModule*> (weak.get());
        if (url.isEmpty() || self == nullptr) return;
        double rate = 44100.0;
        auto sample = self->getLoadedSample (&rate);
        if (sample == nullptr || sample->getNumSamples() < 2) { tell ("There is nothing to save."); return; }
        int a = 0, b = 0;
        self->windowFor (a, b, sample->getNumSamples());
        const double speed = std::pow (2.0, juce::jlimit (-36.0, 36.0, (double) std::round (self->getParameterBase (pSaveAs))) / 12.0);
        // the resampling and encoding off the message thread, the writing back on it
        juce::Thread::launch ([sample, a, b, speed, rate, flac, url, tell]
        {
            const auto audio = resample (*sample, a, b, speed);
            auto data = std::make_shared<juce::MemoryBlock> (encode (audio, rate, flac));
            juce::MessageManager::callAsync ([data, url, tell, flac]
            {
                if (data->getSize() == 0) { tell (flac ? "The FLAC file could not be made." : "The WAV file could not be made."); return; }
                const auto problem = aqp::files::writeDocument (url, data->getData(), data->getSize());
                if (problem.isNotEmpty()) tell ("The file could not be saved: " + problem + ".");
            });
        });
    });
}

// for tests: the interval as Save Sample would write it
juce::AudioBuffer<float> samplerRenderForTest (aquanode::SynthModule* m, double& rate)
{
    if (auto* s = dynamic_cast<SamplerModule*> (m)) return s->renderInterval (rate);
    return {};
}

//==============================================================================
// Multi Sample
//==============================================================================
void MultiSampleModule::blockStart()
{
    const int v = slotVersion.load (std::memory_order_acquire);
    if (v == seenVersion)
        return;
    const juce::SpinLock::ScopedTryLockType sl (slotLock);
    if (! sl.isLocked())
        return;                       // next block
    double longest = 0.0;
    for (int i = 0; i < kSlots; ++i)
    {
        playing[(size_t) i].audio = slots[(size_t) i].audio;      // (the message thread keeps the old ones alive)
        playing[(size_t) i].rate = slots[(size_t) i].rate;
        longest = juce::jmax (longest, slots[(size_t) i].seconds());
    }
    length = longest;
    if (position >= length)
        position = 0.0;
    seenVersion = v;
}

void MultiSampleModule::processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs)
{
    outputs[0] = { 0.0f, 0.0f };
    if (restart.exchange (false, std::memory_order_relaxed))
        position = 0.0;
    if (param (pPlaying) > 0.5f && length > 0.0)
    {
        const int track = juce::jlimit (0, kSlots, juce::roundToInt (param (pTrack)));
        const float level = param (pLevel);
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < kSlots; ++i)
        {
            const auto& s = playing[(size_t) i];
            if (s.audio == nullptr || (track != 0 && track != i + 1))
                continue;
            const double x = position * s.rate;
            const int i0 = (int) x, n = s.audio->getNumSamples();
            if (i0 >= n)
                continue;                                  // a shorter file than the longest: silent after its end
            const float f = (float) (x - i0);
            const int i1 = juce::jmin (i0 + 1, n - 1);
            const float* a = s.audio->getReadPointer (0);
            const float* b = s.audio->getReadPointer (s.audio->getNumChannels() > 1 ? 1 : 0);
            l += a[i0] + (a[i1] - a[i0]) * f;
            r += b[i0] + (b[i1] - b[i0]) * f;
        }
        outputs[0] = { l * level, r * level };
        position += 1.0 / sampleRate;
        if (position >= length)
        {
            position = 0.0;
            if (param (pLoop) < 0.5f)
                setParameterByIndex (pPlaying, 0.0f);      // once through, then it waits at the start
        }
    }
    uiPosition.store (position, std::memory_order_relaxed);
}

void MultiSampleModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "restart")
        restart.store (true, std::memory_order_relaxed);
}

MultiSampleModule::Slot MultiSampleModule::getSlot (int index) const
{
    const juce::SpinLock::ScopedLockType sl (slotLock);
    return slots[(size_t) juce::jlimit (0, kSlots - 1, index)];
}

double MultiSampleModule::lengthSeconds() const
{
    const juce::SpinLock::ScopedLockType sl (slotLock);
    double longest = 0.0;
    for (const auto& s : slots) longest = juce::jmax (longest, s.seconds());
    return longest;
}

void MultiSampleModule::setSlot (int index, Slot slot)
{
    if (index < 0 || index >= kSlots)
        return;
    {
        const juce::SpinLock::ScopedLockType sl (slotLock);
        if (slots[(size_t) index].audio != nullptr)
            retired.push_back (slots[(size_t) index].audio);     // the audio thread may still be reading it
        if (retired.size() > 2 * kSlots)
            retired.erase (retired.begin(), retired.begin() + kSlots);
        slots[(size_t) index] = std::move (slot);
    }
    slotVersion.fetch_add (1, std::memory_order_release);
}

void MultiSampleModule::chooseFile (int index)
{
    chooser = std::make_unique<juce::FileChooser> ("Load into track " + juce::String (index + 1), juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::WeakReference<aquanode::SynthModule> weak (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weak, index] (const juce::FileChooser& fc)
        {
            auto* self = dynamic_cast<MultiSampleModule*> (weak.get());
            if (self == nullptr)
                return;
            juce::String name;
           #if JUCE_ANDROID
            const auto url = fc.getURLResult();
            if (url.isEmpty()) return;
            if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue()) name = doc.getInfo().getName();
            self->loadSampleFromURL (url);
           #else
            const auto file = fc.getResult();
            if (! file.existsAsFile()) return;
            name = file.getFileName();
            self->loadSampleFromFile (file);
           #endif
            // the shared loader leaves the file in the one-sample buffer: move it into its track
            double rate = 48000.0;
            if (auto audio = self->getLoadedSample (&rate))
            {
                self->setSlot (index, { audio, rate, name.isNotEmpty() ? name : "File " + juce::String (index + 1) });
                self->setLoadedSample (nullptr, 48000.0);
            }
        });
}

//-- saved with the patch --------------------------------------------------------
juce::String MultiSampleModule::saveCustomState() const
{
    const juce::SpinLock::ScopedLockType sl (slotLock);
    juce::StringArray parts;
    for (const auto& s : slots)
        parts.add (juce::String (s.audio != nullptr ? s.audio->getNumSamples() : 0) + "," + juce::String (s.rate, 3) + ","
                   + juce::Base64::toBase64 (s.name));
    return "v1|" + parts.joinIntoString (";");
}

std::shared_ptr<const juce::AudioBuffer<float>> MultiSampleModule::getAudioToSave (double& rate) const
{
    rate = 48000.0;
    const juce::SpinLock::ScopedLockType sl (slotLock);
    int total = 0;
    for (const auto& s : slots) if (s.audio != nullptr) total += s.audio->getNumSamples();
    if (total == 0)
        return nullptr;
    auto all = std::make_shared<juce::AudioBuffer<float>> (2, total);
    int at = 0;
    for (const auto& s : slots)
        if (s.audio != nullptr)
        {
            const int n = s.audio->getNumSamples();
            all->copyFrom (0, at, *s.audio, 0, 0, n);
            all->copyFrom (1, at, *s.audio, s.audio->getNumChannels() > 1 ? 1 : 0, 0, n);
            at += n;
        }
    return all;
}

void MultiSampleModule::restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double)
{
    packed = std::move (audio);
    unpackIfComplete();
}

void MultiSampleModule::loadCustomState (const juce::String& state)
{
    packedIndex = state;
    juce::int64 total = 0;
    if (state.startsWith ("v1|"))
        for (const auto& part : juce::StringArray::fromTokens (state.substring (3), ";", ""))
            total += juce::jmax (0, part.upToFirstOccurrenceOf (",", false, false).getIntValue());
    if (total == 0)
    {
        // nothing loaded in that patch: no audio will follow
        for (int i = 0; i < kSlots; ++i) setSlot (i, {});
        packedIndex = {};
        packed = nullptr;
        return;
    }
    unpackIfComplete();
}

void MultiSampleModule::unpackIfComplete()
{
    // the audio and the index arrive separately, in either order
    if (packed == nullptr || ! packedIndex.startsWith ("v1|"))
        return;
    const auto parts = juce::StringArray::fromTokens (packedIndex.substring (3), ";", "");
    int at = 0;
    for (int i = 0; i < kSlots; ++i)
    {
        const auto f = juce::StringArray::fromTokens (i < parts.size() ? parts[i] : juce::String(), ",", "");
        const int n = f[0].getIntValue();
        if (n <= 0 || at + n > packed->getNumSamples()) { setSlot (i, {}); continue; }
        auto one = std::make_shared<juce::AudioBuffer<float>> (packed->getNumChannels(), n);
        for (int c = 0; c < packed->getNumChannels(); ++c) one->copyFrom (c, 0, *packed, c, at, n);
        at += n;
        juce::MemoryOutputStream nameBytes;
        juce::Base64::convertFromBase64 (nameBytes, f[2]);
        setSlot (i, { one, juce::jmax (1000.0, f[1].getDoubleValue()), nameBytes.toString() });
    }
    packed = nullptr;
    packedIndex = {};
}

//-- the card --------------------------------------------------------------------
namespace
{
    class MultiSampleList : public juce::Component, private juce::Timer
    {
    public:
        explicit MultiSampleList (MultiSampleModule& m) : weak (&m) { startTimerHz (15); }

        void paint (juce::Graphics& g) override
        {
            auto* m = module.get();
            if (m == nullptr) return;
            const int track = juce::roundToInt (m->getParameter ("track"));
            const double pos = m->positionSeconds(), len = m->lengthSeconds();
            for (int i = 0; i < MultiSampleModule::kSlots; ++i)
            {
                auto r = row (i).toFloat();
                const auto s = m->getSlot (i);
                const bool out = s.audio != nullptr && (track == 0 || track == i + 1);
                g.setColour (juce::Colours::black.withAlpha (out ? 0.42f : 0.24f));
                g.fillRoundedRectangle (r, 3.0f);
                if (s.audio != nullptr && len > 0.0)
                {
                    // how much of the common length it covers, and where the playback is
                    auto bar = r.reduced (1.0f).withWidth ((r.getWidth() - 2.0f) * (float) (s.seconds() / len));
                    g.setColour (juce::Colours::white.withAlpha (out ? 0.16f : 0.07f));
                    g.fillRoundedRectangle (bar, 2.0f);
                    if (out && pos < s.seconds())
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.85f));
                        g.fillRect (r.getX() + 1.0f + (r.getWidth() - 2.0f) * (float) (pos / len), r.getY() + 2.0f, 1.5f, r.getHeight() - 4.0f);
                    }
                }
                g.setColour (juce::Colours::white.withAlpha (out ? 1.0f : 0.55f));
                g.setFont (juce::Font (juce::FontOptions (13.0f, track == i + 1 ? juce::Font::bold : juce::Font::plain)));
                auto text = r.reduced (6.0f, 0.0f).withTrimmedRight (26.0f);
                g.drawText (juce::String (i + 1), text.removeFromLeft (16.0f), juce::Justification::centredLeft);
                if (s.audio != nullptr)
                {
                    const int secs = (int) std::round (s.seconds());
                    g.drawText (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2), text.removeFromRight (40.0f), juce::Justification::centredRight);
                    g.drawText (s.name, text, juce::Justification::centredLeft, true);
                    g.setColour (juce::Colours::white.withAlpha (0.6f));
                    g.drawText ("x", clearArea (i), juce::Justification::centred);
                }
                else
                    g.drawText ("tap to load a file", text, juce::Justification::centredLeft);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            auto* m = module.get();
            if (m == nullptr || e.mouseWasDraggedSinceMouseDown()) return;
            for (int i = 0; i < MultiSampleModule::kSlots; ++i)
            {
                if (! row (i).contains (e.getPosition())) continue;
                const bool has = m->getSlot (i).audio != nullptr;
                if (has && clearArea (i).expanded (4, 0).contains (e.getPosition())) m->setSlot (i, {});
                else if (! has || e.mods.isPopupMenu()) m->chooseFile (i);          // long press / right click: another file
                else m->setParameter ("track", juce::roundToInt (m->getParameter ("track")) == i + 1 ? 0.0f : (float) (i + 1));   // this one / all
                repaint();
                return;
            }
        }

    private:
        juce::Rectangle<int> row (int i) const { return { 4, 6 + i * 26, getWidth() - 8, 23 }; }
        juce::Rectangle<int> clearArea (int i) const { return row (i).removeFromRight (26); }
        void timerCallback() override { repaint(); }
        struct Ref { juce::WeakReference<aquanode::SynthModule>& w; MultiSampleModule* get() const { return dynamic_cast<MultiSampleModule*> (w.get()); } };
        juce::WeakReference<aquanode::SynthModule> weak;
        Ref module { weak };
    };
}

std::unique_ptr<juce::Component> MultiSampleModule::createExtraContentComponent()
{
    return std::make_unique<MultiSampleList> (*this);
}

static ModuleDescriptor multiSampleDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.multisample";
    d.displayName = "Multi Sample";
    d.description =
        "Up to eight audio files played side by side from one common position, and Track for which one goes out: "
        "All, or one of them. Switch while it plays and it stays in place - for comparing takes or renders, for stems, "
        "or as a test-signal player in front of an effect. Tap an empty row to load a file, a loaded row to hear it alone "
        "(again for all), long-press / right-click for another file, x to remove it. Shorter files are silent after their "
        "end; with Loop off it plays once and waits at the start. The files (up to 10 minutes each) are saved with the patch.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 1;
    d.sockets = { audioOut ("audioOut", "Audio Out") };
    d.params = {
        makeCombo ("playing", "Play", { "Stopped", "Playing" }, 0, 0, 2),
        makeCombo ("loop", "Loop", { "Off", "On" }, 1, 0, 2),
        makeCombo ("track", "Track", { "All", "1", "2", "3", "4", "5", "6", "7", "8" }, 0, 1, 2),
        makeRotary ("level", "Level", 0.0f, 2.0f, 1.0f, 1),
        makeButton ("restart", "From Start", 1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MultiSampleModule, multiSampleDescriptor)
