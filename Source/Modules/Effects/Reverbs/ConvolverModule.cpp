#include "ConvolverModule.h"

using namespace aquanode;

namespace
{
    constexpr size_t kMaxFileBytes = 400u * 1024u * 1024u;

    juce::AudioFormatManager& irFormats()
    {
        struct Formats
        {
            Formats()
            {
                manager.registerBasicFormats();
               #if JUCE_USE_MP3AUDIOFORMAT
                manager.registerFormat (new juce::MP3AudioFormat(), false);
               #endif
            }
            juce::AudioFormatManager manager;
        };
        static Formats formats;   // thread-safe initialisation
        return formats.manager;
    }

    inline float onePoleCoef (double hz, double sr)
    {
        return (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jlimit (5.0, sr * 0.45, hz) / sr);
    }
}

ConvolverModule::ConvolverModule() : juce::Thread ("Convolver IR") {}

ConvolverModule::~ConvolverModule()
{
    signalThreadShouldExit();
    notify();
    stopThread (5000);
}

void ConvolverModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    {
        const juce::ScopedLock sl (convLock);
        convolution.prepare ({ sr, (juce::uint32) kBlock, 2 });
        convolution.reset();
    }
    for (auto& l : preLine)
        l.assign ((size_t) (sr * 0.26) + 8, 0.0f);
    fadeCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
    smCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
    reset();

    shapedGen = -1;   // the convolver was prepared afresh: load the IR into it again
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
    notify();
}

void ConvolverModule::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < kBlock; ++i)
            inBuf[c][i] = outBuf[c][i] = dlyBuf[c][i] = 0.0f;
        std::fill (preLine[c].begin(), preLine[c].end(), 0.0f);
        hpZ[c][0] = hpZ[c][1] = lpZ[c][0] = lpZ[c][1] = 0.0f;
    }
    blockPos = prePos = 0;
    fade = irReady.load() ? 1.0f : 0.0f;
    smMix = param (pMix) * 0.01f;
    smGain = juce::Decibels::decibelsToGain (param (pGain));
    const juce::ScopedTryLock tl (convLock);   // never wait on the audio thread
    if (tl.isLocked())
        convolution.reset();
}

//==============================================================================
void ConvolverModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float inL = inputs[0][0], inR = inputs[0][1];

    // through the 64-sample blocks (always: the latency stays what was reported)
    const float wetL0 = outBuf[0][blockPos], wetR0 = outBuf[1][blockPos];
    const float dryL = dlyBuf[0][blockPos], dryR = dlyBuf[1][blockPos];
    dlyBuf[0][blockPos] = inBuf[0][blockPos] = inL;
    dlyBuf[1][blockPos] = inBuf[1][blockPos] = inR;
    if (++blockPos >= kBlock)
    {
        blockPos = 0;
        if (irReady.load (std::memory_order_relaxed))
        {
            float* inPtrs[2] = { inBuf[0], inBuf[1] };
            float* outPtrs[2] = { outBuf[0], outBuf[1] };
            juce::dsp::AudioBlock<float> inBlock (inPtrs, 2, (size_t) kBlock);
            juce::dsp::AudioBlock<float> outBlock (outPtrs, 2, (size_t) kBlock);
            const juce::ScopedTryLock tl (convLock);
            if (tl.isLocked())
                convolution.process (juce::dsp::ProcessContextNonReplacing<float> (inBlock, outBlock));
        }
        else
        {
            for (auto& ch : outBuf)
                std::fill (std::begin (ch), std::end (ch), 0.0f);
        }
    }

    const float target = irReady.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    fade += fadeCoef * (target - fade);
    if (target < 0.5f && fade < 1.0e-4f)
    {
        outputs[0][0] = dryL;   // no IR: the (late) dry signal
        outputs[0][1] = dryR;
        return;
    }

    // the wet path: pre-delay, low / high cut, width
    float wet[2] = { wetL0, wetR0 };
    const int psize = (int) preLine[0].size();
    const int pre = juce::jlimit (0, psize - 2, (int) std::lround (param (pPredelay) * 0.001 * sampleRate));
    const float lowCut = param (pLowCut), highCut = param (pHighCut);
    const bool lowCutOn = lowCut > 12.0f, highCutOn = highCut < 20500.0f;   // (at their ends: off)
    const float hpA = onePoleCoef (lowCut, sampleRate);
    const float lpA = onePoleCoef (highCut, sampleRate);
    for (int c = 0; c < 2; ++c)
    {
        float x = wet[c];
        if (pre > 0)
        {
            preLine[c][(size_t) prePos] = x;
            int rp = prePos - pre;
            if (rp < 0) rp += psize;
            x = preLine[c][(size_t) rp];
        }
        if (highCutOn)
            for (int k = 0; k < 2; ++k)
            {
                lpZ[c][k] = x + lpA * (lpZ[c][k] - x);       // low-pass section
                x = lpZ[c][k];
            }
        if (lowCutOn)
            for (int k = 0; k < 2; ++k)
            {
                hpZ[c][k] = x + hpA * (hpZ[c][k] - x);       // its low part, taken away
                x -= hpZ[c][k];
            }
        wet[c] = x;
    }
    prePos = (prePos + 1) % psize;

    const float w = param (pWidth) * 0.01f;
    const float mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * w;
    smMix += smCoef * (juce::jlimit (0.0f, 1.0f, param (pMix) * 0.01f) - smMix);
    smGain += smCoef * (juce::Decibels::decibelsToGain (param (pGain)) - smGain);
    const float g = smGain * fade;
    outputs[0][0] = dryL * (1.0f - smMix * fade) + (mid + side) * g * smMix;
    outputs[0][1] = dryR * (1.0f - smMix * fade) + (mid - side) * g * smMix;
}

//==============================================================================
// the IR: the file as loaded, kept; the convolver gets it trimmed / turned
//==============================================================================
bool ConvolverModule::hasAudioToSave() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return original != nullptr;
}

std::shared_ptr<const juce::AudioBuffer<float>> ConvolverModule::getAudioToSave (double& rate) const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    rate = originalRate;
    return original;
}

void ConvolverModule::restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
{
    if (audio == nullptr || audio->getNumSamples() < 2 || rate <= 0.0)
        return;
    setOriginal (std::move (audio), rate, "IR kept in the patch");
}

void ConvolverModule::setOriginal (std::shared_ptr<const juce::AudioBuffer<float>> ir, double rate, const juce::String& name)
{
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        original = std::move (ir);
        originalRate = rate;
        irName = name;
        errorText = {};
    }
    originalGen.fetch_add (1);
    notify();
}

void ConvolverModule::clearIR()
{
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        original = nullptr;
        shapedView = nullptr;
        irName = {};
    }
    irReady.store (false);
    originalGen.fetch_add (1);
}

std::shared_ptr<const ConvolverModule::View> ConvolverModule::view() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return shapedView;
}

juce::String ConvolverModule::lastError() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return errorText;
}

void ConvolverModule::loadFileData (std::shared_ptr<juce::MemoryBlock> data, const juce::String& name)
{
    if (data == nullptr || data->getSize() == 0)
        return;
    {
        const juce::ScopedLock sl (loadLock);
        pendingData = std::move (data);
        pendingName = name;
    }
    loading.store (true);
    notify();
}

void ConvolverModule::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load an impulse response", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::WeakReference<SynthModule> weakThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weakThis] (const juce::FileChooser& fc)
        {
            auto* self = dynamic_cast<ConvolverModule*> (weakThis.get());
            if (self == nullptr)
                return;
            auto data = std::make_shared<juce::MemoryBlock>();
            juce::String name;
           #if JUCE_ANDROID
            // a content:// document has no path: read it as a stream
            const auto url = fc.getURLResult();
            if (url.isEmpty())
                return;
            auto doc = juce::AndroidDocument::fromDocument (url);
            if (! doc.hasValue())
                return;
            if (auto in = doc.createInputStream())
                in->readIntoMemoryBlock (*data, (int) kMaxFileBytes);
            name = doc.getInfo().getName().upToLastOccurrenceOf (".", false, false);
           #else
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;
            if ((size_t) file.getSize() > kMaxFileBytes)
            {
                const juce::SpinLock::ScopedLockType sl (self->stateLock);
                self->errorText = "That file is too large";
                return;
            }
            file.loadFileAsData (*data);
            name = file.getFileNameWithoutExtension();
           #endif
            self->loadFileData (std::move (data), name);
        });
}

//==============================================================================
// the worker thread: decoding a picked file, shaping the IR when the knobs move
//==============================================================================
void ConvolverModule::run()
{
    while (! threadShouldExit())
    {
        decodePending();
        shapeIfNeeded (false);
        wait (120);
    }
}

void ConvolverModule::decodePending()
{
    std::shared_ptr<juce::MemoryBlock> data;
    juce::String name;
    {
        const juce::ScopedLock sl (loadLock);
        data = std::move (pendingData);
        name = pendingName;
    }
    if (data == nullptr)
        return;

    std::unique_ptr<juce::AudioFormatReader> reader (irFormats().createReaderFor (
        std::make_unique<juce::MemoryInputStream> (data->getData(), data->getSize(), false)));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        errorText = "Can't read that file";
        loading.store (false);
        return;
    }
    const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);
    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (kMaxSeconds * reader->sampleRate));
    auto ir = std::make_shared<juce::AudioBuffer<float>> (numCh, len);
    reader->read (ir.get(), 0, len, 0, true, numCh > 1);
    setOriginal (std::move (ir), reader->sampleRate, name);
    loading.store (false);
}

void ConvolverModule::shapeIfNeeded (bool force)
{
    const int gen = originalGen.load();
    const float length = getParameter ("length"), reverse = getParameter ("reverse"), normalize = getParameter ("normalize");
    if (! force && gen == shapedGen && length == shapedLength && reverse == shapedReverse && normalize == shapedNormalize)
        return;

    std::shared_ptr<const juce::AudioBuffer<float>> src;
    double rate;
    juce::String name;
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        src = original;
        rate = originalRate;
        name = irName;
    }
    shapedGen = gen;
    shapedLength = length;
    shapedReverse = reverse;
    shapedNormalize = normalize;
    if (src == nullptr)
    {
        irReady.store (false);
        return;
    }

    // Length: the front part of the file, faded out over its last 10 ms (or a tenth)
    const int full = src->getNumSamples();
    const int len = juce::jlimit (2, full, (int) std::lround (full * juce::jlimit (0.01f, 1.0f, length * 0.01f)));
    juce::AudioBuffer<float> ir (src->getNumChannels(), len);
    for (int c = 0; c < src->getNumChannels(); ++c)
        ir.copyFrom (c, 0, *src, c, 0, len);
    if (len < full)
    {
        const int fadeLen = juce::jmax (1, juce::jmin (len / 10, (int) (0.01 * rate)));
        ir.applyGainRamp (len - fadeLen, fadeLen, 1.0f, 0.0f);
    }
    if (reverse > 0.5f)
        ir.reverse (0, len);

    // Normalize: the IR's energy to one level, so files of all kinds sit alike
    // (a full-band input comes out about as loud as it went in)
    if (normalize > 0.5f)
    {
        double e = 0.0;
        for (int c = 0; c < ir.getNumChannels(); ++c)
        {
            const float* d = ir.getReadPointer (c);
            for (int i = 0; i < len; ++i)
                e += (double) d[i] * d[i];
        }
        e /= (double) ir.getNumChannels();
        if (e > 1.0e-12)
            ir.applyGain ((float) (0.6 / std::sqrt (e)));
    }

    // the card's picture: the envelope of what plays
    auto v = std::make_shared<View>();
    v->name = name;
    v->seconds = (double) len / rate;
    v->channels = ir.getNumChannels();
    float maxPeak = 1.0e-9f;
    for (int p = 0; p < kOverview; ++p)
    {
        const int a = (int) ((juce::int64) len * p / kOverview), b = juce::jmax (a + 1, (int) ((juce::int64) len * (p + 1) / kOverview));
        float pk = 0.0f;
        for (int c = 0; c < ir.getNumChannels(); ++c)
            pk = juce::jmax (pk, ir.getMagnitude (c, a, juce::jmin (b, len) - a));
        v->peak[(size_t) p] = pk;
        maxPeak = juce::jmax (maxPeak, pk);
    }
    for (auto& p : v->peak)
        p = std::sqrt (p / maxPeak);   // a softer scale shows the tail

    {
        const juce::ScopedLock sl (convLock);
        convolution.loadImpulseResponse (std::move (ir), rate, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }
    {
        const juce::SpinLock::ScopedLockType sl (stateLock);
        shapedView = std::move (v);
    }
    irReady.store (true);
}

//==============================================================================
// The card: LOAD / CLEAR and the IR's envelope
//==============================================================================
class ConvolverPanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit ConvolverPanel (ConvolverModule& m) : module (&m) { startTimerHz (10); }

    void paint (juce::Graphics& g) override
    {
        auto* m = mod();
        if (m == nullptr)
            return;
        const auto well = juce::Colours::black.withAlpha (0.55f);
        const auto text = juce::Colours::white.withAlpha (0.82f);
        const auto cyan = juce::Colour (0xff4fd1e0);

        for (int b = 0; b < 2; ++b)
        {
            auto r = buttonBounds (b);
            g.setColour (well);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (text);
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText (b == 0 ? "LOAD IR" : "CLEAR", r, juce::Justification::centred, false);
        }

        auto wv = waveBounds();
        g.setColour (well);
        g.fillRoundedRectangle (wv, 4.0f);
        const auto v = m->view();
        juce::String line;
        if (m->isLoading())
            line = "Reading the file...";
        else if (v != nullptr)
        {
            auto area = wv.reduced (4.0f, 4.0f).withTrimmedBottom (14.0f);
            juce::Path p;
            p.startNewSubPath (area.getX(), area.getBottom());
            for (int i = 0; i < ConvolverModule::kOverview; ++i)
                p.lineTo (area.getX() + area.getWidth() * (float) i / (float) (ConvolverModule::kOverview - 1),
                          area.getBottom() - area.getHeight() * v->peak[(size_t) i]);
            p.lineTo (area.getRight(), area.getBottom());
            p.closeSubPath();
            g.setColour (cyan.withAlpha (0.35f));
            g.fillPath (p);
            g.setColour (cyan.withAlpha (0.9f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
            line = v->name + "   " + juce::String (v->seconds, v->seconds < 1.0 ? 3 : 2) + " s"
                 + (v->channels > 1 ? "  stereo" : "  mono");
        }
        else
            line = "No IR - LOAD a file (wav, aif, flac, ogg, mp3)";
        if (const auto err = m->lastError(); err.isNotEmpty())
            line = err;
        g.setColour (text);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (line, wv.reduced (6.0f, 2.0f).removeFromBottom (14.0f), juce::Justification::centredLeft, true);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* m = mod();
        if (m == nullptr)
            return;
        if (buttonBounds (0).contains (e.position)) m->openChooser();
        else if (buttonBounds (1).contains (e.position)) m->clearIR();
        repaint();
    }

private:
    ConvolverModule* mod() const { return dynamic_cast<ConvolverModule*> (module.get()); }
    juce::Rectangle<float> buttonBounds (int b) const
    {
        const float w = ((float) getWidth() - 16.0f) * 0.5f;
        return { 6.0f + (float) b * (w + 4.0f), 4.0f, w, 20.0f };
    }
    juce::Rectangle<float> waveBounds() const
    {
        return getLocalBounds().toFloat().reduced (6.0f, 0.0f).withTrimmedTop (28.0f).withTrimmedBottom (4.0f);
    }
    void timerCallback() override { repaint(); }

    juce::WeakReference<aquanode::SynthModule> module;
};

std::unique_ptr<juce::Component> ConvolverModule::createExtraContentComponent()
{
    return std::make_unique<ConvolverPanel> (*this);
}

//==============================================================================
static ModuleDescriptor convolverDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.convolver";
    d.displayName = "Convolver";
    d.description =
        "Convolution with any impulse response you load: a hall, a plate, a spring, a guitar cabinet, a telephone, "
        "or any sound at all - the file is the IR (stereo files convolve in true stereo). Dry/Wet at 100 % is the "
        "convolution alone (cabinets, filters); lower it for reverbs. Length trims the IR, Reverse turns it round, "
        "Normalize brings every file to one level; Pre-Delay, Low / High Cut and Width shape the wet sound. The IR "
        "is saved with the patch. 64 samples of latency.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 64;
    d.listedAfter = "fx.irforge";
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("mix",       "Dry/Wet",   0.0f, 100.0f, 100.0f, 0, "%"),
        makeRotary ("gain",      "Wet Gain",  -24.0f, 24.0f, 0.0f, 0, "dB"),
        makeRotary ("predelay",  "Pre-Delay", 0.0f, 250.0f, 0.0f, 0, "ms"),
        makeRotary ("lowCut",    "Low Cut",   10.0f, 2000.0f, 10.0f, 1, "Hz", true),
        makeRotary ("highCut",   "High Cut",  500.0f, 22000.0f, 22000.0f, 1, "Hz", true),
        makeRotary ("width",     "Width",     0.0f, 200.0f, 100.0f, 1, "%"),
        makeRotary ("length",    "Length",    1.0f, 100.0f, 100.0f, 2, "%").noMod(),
        makeCombo  ("reverse",   "Reverse",   { "Off", "On" }, 0, 2, 1).noMod(),
        makeCombo  ("normalize", "Normalize", { "Off", "On" }, 1, 2, 1).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ConvolverModule, convolverDescriptor)
