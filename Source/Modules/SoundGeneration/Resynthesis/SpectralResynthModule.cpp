#include "SpectralResynthModule.h"
#include "SpectralResynthShared.h"

using namespace aquanode;
using namespace aquanode::resynthdetail;

//==============================================================================
// notes: which harmonic of which fundamental a peak is, and where it goes
//==============================================================================
float SpectralResynthModule::NoteContour::f0At (float t) const
{
    if (pts.empty())
        return 0.0f;
    if (t <= pts.front().x) return pts.front().y;
    if (t >= pts.back().x)  return pts.back().y;
    const auto it = std::lower_bound (pts.begin(), pts.end(), t, [] (const juce::Point<float>& p, float x) { return p.x < x; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const float u = (t - a.x) / juce::jmax (1.0e-6f, b.x - a.x);
    return a.y * std::pow (b.y / a.y, u);
}

int SpectralResynthModule::NoteContour::harmonicOf (float t, float hz, float hopSeconds) const
{
    if (pts.empty() || t < start() - hopSeconds * 0.75f || t > end() + hopSeconds * 0.75f)
        return 0;
    const float c = f0At (t);
    if (c <= 0.0f)
        return 0;
    const int h = juce::roundToInt (hz / c);
    if (h < 1)
        return 0;
    return centsBetween (hz, c * (float) h) < 35.0f ? h : 0;
}

void SpectralResynthModule::framePeaks (const Model& m, int frame, std::vector<Peak>& out)
{
    out.clear();
    if (frame < 0 || frame >= m.numFrames())
        return;
    if (! m.binsEngine)
    {
        const auto& p = *m.partials;
        for (int i = p.frameStart[(size_t) frame]; i < p.frameStart[(size_t) frame + 1]; ++i)
            out.push_back ({ p.points[(size_t) i].hz, p.points[(size_t) i].amp });
    }
    else
    {
        // the magnitude spectrum's peaks (the channels' power summed), refined by a parabola
        const auto& b = *m.bins;
        const auto& fr = *b.frames[(size_t) frame];
        std::vector<float> db ((size_t) b.bins);
        for (int k = 0; k < b.bins; ++k)
        {
            float pw = 0.0f;
            for (int c = 0; c < b.channels; ++c)
            {
                const float a = decodeMag (fr.mag[(size_t) (c * b.bins + k)]);
                pw += a * a;
            }
            db[(size_t) k] = 10.0f * std::log10 (pw / (float) b.channels + 1.0e-20f);
        }
        const float floorDb = b.topDb - 75.0f;
        const float ampScale = 2.0f / (0.5f * (float) b.N);   // a Hann window's gain: a full-scale sine reads 1
        for (int k = 2; k < b.bins - 2; ++k)
        {
            const float v = db[(size_t) k];
            if (v < floorDb || v <= db[(size_t) k - 1] || v < db[(size_t) k + 1])
                continue;
            const float a = db[(size_t) k - 1], c = db[(size_t) k + 1];
            const float den = a - 2.0f * v + c;
            const float p = std::abs (den) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f;
            const float peakDb = v - 0.25f * (a - c) * p;
            out.push_back ({ b.binHz ((float) k + p), std::pow (10.0f, peakDb / 20.0f) * ampScale });
        }
    }
    std::sort (out.begin(), out.end(), [] (const Peak& a, const Peak& b) { return a.amp > b.amp; });
    if (out.size() > 160)
        out.resize (160);
}

namespace
{
    using Peak = SpectralResynthModule::Peak;

    // the amplitude of the peak nearest to hz (within 35 cents), 0 for none
    float peakNear (const std::vector<Peak>& peaks, float hz, float* foundHz = nullptr)
    {
        float best = 35.0f, amp = 0.0f;
        for (const auto& p : peaks)
        {
            const float c = SpectralResynthModule::centsBetween (p.hz, hz);
            if (c < best) { best = c; amp = p.amp; if (foundHz != nullptr) *foundHz = p.hz; }
        }
        return amp;
    }

    // the fundamental from every harmonic of it found (least squares, weighted by level), twice
    float refineFundamental (const std::vector<Peak>& peaks, float c, float nyquist)
    {
        for (int it = 0; it < 2; ++it)
        {
            double num = 0.0, den = 0.0;
            const int K = juce::jmin (48, (int) (nyquist / juce::jmax (1.0f, c)));
            for (int k = 1; k <= K; ++k)
            {
                float hz = 0.0f;
                const float a = peakNear (peaks, c * (float) k, &hz);
                if (a > 0.0f)
                {
                    num += (double) a * k * hz;
                    den += (double) a * k * k;
                }
            }
            if (den > 0.0)
                c = (float) (num / den);
        }
        return c;
    }

    // which harmonic n of which fundamental the tapped peak is: every
    // n = 1..16 tried against saw, square and triangle spectra; a candidate
    // scores by how much of the level near it its harmonics explain (a
    // fundamental an octave too high misses half of it) times how well their
    // levels follow one of the shapes (an octave too low leaves every other
    // harmonic empty); near ties go to the higher fundamental
    bool searchNote (const std::vector<Peak>& peaks, float tapHz, float nyquist, float& f0, int& n)
    {
        struct Cand { float c; int n; float score; };
        std::vector<Cand> cands;
        for (int k = 1; k <= 16; ++k)
        {
            const float c = tapHz / (float) k;
            if (c < 20.0f)
                break;
            const int K = juce::jmax (1, juce::jmin (40, (int) (nyquist / c)));
            std::vector<float> p ((size_t) K + 1, 0.0f);
            double explained = 0.0;
            for (int h = 1; h <= K; ++h)
            {
                p[(size_t) h] = peakNear (peaks, c * (float) h);
                explained += (double) p[(size_t) h] * p[(size_t) h];
            }
            double total = 0.0;
            for (const auto& q : peaks)
                if (q.hz > c * 0.6f && q.hz < c * ((float) K + 0.5f))
                    total += (double) q.amp * q.amp;
            if (total <= 0.0)
                continue;
            auto corr = [&] (auto expected)
            {
                double pe = 0.0, pp = 0.0, ee = 0.0;
                for (int h = 1; h <= K; ++h)
                {
                    const double e = expected (h);
                    pe += p[(size_t) h] * e; pp += (double) p[(size_t) h] * p[(size_t) h]; ee += e * e;
                }
                return pp > 0.0 && ee > 0.0 ? pe / std::sqrt (pp * ee) : 0.0;
            };
            const double saw = corr ([] (int h) { return 1.0 / h; });
            const double square = corr ([] (int h) { return (h & 1) ? 1.0 / h : 0.0; });
            const double triangle = corr ([] (int h) { return (h & 1) ? 1.0 / ((double) h * h) : 0.0; });
            const double shape = juce::jmax (saw, square, triangle);
            cands.push_back ({ c, k, (float) (juce::jlimit (0.0, 1.0, explained / total) * shape) });
        }
        if (cands.empty())
            return false;
        float best = 0.0f;
        for (auto& c : cands) best = juce::jmax (best, c.score);
        if (best <= 0.05f)
            return false;
        for (auto& c : cands)
            if (c.score >= best - 0.03f)   // the first (highest) of the near-best
            {
                n = c.n;
                f0 = refineFundamental (peaks, c.c, nyquist);
                return true;
            }
        return false;
    }
}

SpectralResynthModule::NoteContour SpectralResynthModule::findNote (const Model& m, double t, float hz)
{
    NoteContour note;
    const int frames = m.numFrames();
    if (frames <= 0)
        return note;
    const float nyq = m.nyquist();
    std::vector<Peak> peaks;

    // the peak tapped: the nearest one (within a semitone), in this frame or the ones around it
    int f0Frame = m.frameAt (t);
    float tapHz = 0.0f;
    for (int d : { 0, 1, -1, 2, -2 })
    {
        const int f = f0Frame + d;
        if (f < 0 || f >= frames) continue;
        framePeaks (m, f, peaks);
        float bestC = 100.0f;
        for (const auto& p : peaks)
            if (const float c = centsBetween (p.hz, hz); c < bestC) { bestC = c; tapHz = p.hz; }
        if (tapHz > 0.0f) { f0Frame = f; break; }
    }
    if (tapHz <= 0.0f)
        return note;

    float c = 0.0f;
    int n = 1;
    if (! searchNote (peaks, tapHz, nyq, c, n))
        return note;
    note.pts.push_back ({ (float) m.frameTime (f0Frame), c });

    // followed through the frames on either side while the tapped harmonic goes on
    for (int dir : { 1, -1 })
    {
        float cPrev = c;
        int misses = 0;
        for (int f = f0Frame + dir; f >= 0 && f < frames && misses < 3; f += dir)
        {
            framePeaks (m, f, peaks);
            float found = 0.0f;
            if (peakNear (peaks, cPrev * (float) n, &found) <= 0.0f)
            {
                ++misses;
                continue;
            }
            misses = 0;
            const float cNew = refineFundamental (peaks, found / (float) n, nyq);
            if (centsBetween (cNew, cPrev) > 60.0f)
                break;
            cPrev = cNew;
            note.pts.push_back ({ (float) m.frameTime (f), cNew });
        }
    }
    std::sort (note.pts.begin(), note.pts.end(), [] (auto& a, auto& b) { return a.x < b.x; });
    return note;
}

juce::String SpectralResynthModule::describe (const Edit& e)
{
    auto signedAmount = [] (float v, int dp) { return (v > 0 ? "+" : "") + juce::String (v, dp); };
    const auto where = e.isNoteOp() ? "note " + resynth::noteName (e.note.f0At (e.note.start())) + ": "
                                    : juce::String();
    const auto regions = e.regions.size() == 1 ? juce::String() : " (" + juce::String ((int) e.regions.size()) + " regions)";
    switch (e.op)
    {
        case Op::Delete:        return "Delete" + regions;
        case Op::Keep:          return "Keep" + regions;
        case Op::Gain:          return "Gain " + signedAmount (e.amount, 0) + " dB" + regions;
        case Op::Transpose:     return "Transpose " + signedAmount (e.amount, 0) + " st" + regions;
        case Op::Reverse:       return "Reverse" + regions;
        case Op::Rotate:        return "Rotate " + juce::String (juce::roundToInt (e.amount)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + regions;
        case Op::Smear:         return "Smear " + juce::String (juce::roundToInt (e.amount)) + " ms" + regions;
        case Op::NoteRemove:    return where + "Remove";
        case Op::NoteGain:      return where + "Gain " + signedAmount (e.amount, 0) + " dB";
        case Op::NoteTranspose: return where + "Transpose " + signedAmount (e.amount, 0) + " st";
        case Op::NoteUnison:    return where + "Unison " + juce::String ((int) e.amount) + " x " + juce::String (e.amount2, 0) + " ct";
        case Op::NoteFundOnly:  return where + "Fundamental Only";
        case Op::NoteNoFund:    return where + "No Fundamental";
        case Op::NoteOddOnly:   return where + "Odd Harmonics Only";
    }
    return {};
}

//==============================================================================
struct SpectralResynthModule::RecTimer : public juce::Timer
{
    explicit RecTimer (SpectralResynthModule& m) : owner (m) {}
    void timerCallback() override
    {
        // the take reached two minutes and stopped by itself: hand it over here
        if (owner.takeReady.exchange (false))
            owner.finishRecording();
    }
    SpectralResynthModule& owner;
};

SpectralResynthModule::SpectralResynthModule()
    : juce::Thread ("Spectral Resynthesis"), scratch (std::make_unique<Scratch>())
{
    scratch->targets.reserve ((size_t) kMaxSlots * 2 + 8);
    for (auto& p : voicePos)
        p.store (-1.0f, std::memory_order_relaxed);
    for (auto& v : voices)
    {
        v.slots.resize ((size_t) kMaxSlots);
        v.next.resize ((size_t) kMaxSlots);
    }
    recTimer = std::make_unique<RecTimer> (*this);
}

SpectralResynthModule::~SpectralResynthModule()
{
    recTimer->stopTimer();
    signalThreadShouldExit();
    notify();
    stopThread (5000);
}

void SpectralResynthModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    // the phase vocoder's buffers, for the largest frame (message thread)
    const int nMax = 1 << kMaxBinOrder;
    for (auto& v : voices)
        for (int c = 0; c < 2; ++c)
        {
            v.runPhase[c].assign ((size_t) nMax / 2 + 1, 0.0f);
            v.ola[c].assign ((size_t) nMax * 2, 0.0f);
            v.out[c].assign ((size_t) nMax, 0.0f);
        }
    scratch->prepareBins();
    reset();
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void SpectralResynthModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void SpectralResynthModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "record")
    {
        toggleRecording();
        return;
    }
    if (paramId != "loadSample")
        return;

    // a sound picked by hand: its own Root, no edits of the last one
    chooser = std::make_unique<juce::FileChooser> ("Load Sound", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::WeakReference<SynthModule> weak (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weak, this] (const juce::FileChooser& fc)
        {
            if (weak.get() == nullptr)
                return;
           #if JUCE_ANDROID
            const auto url = fc.getURLResult();   // a content:// document: read as a stream
            if (url.isEmpty())
                return;
            newSoundByHand();
            loadSampleFromURL (url);
           #else
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;
            newSoundByHand();
            loadSampleFromFile (file);
           #endif
        });
}

void SpectralResynthModule::newSoundByHand()
{
    setEdits ({});
    selection.clear();
    selectedNote = {};
    rootFromNextAnalysis.store (true);
}

//==============================================================================
// recording: Rec In, up to two minutes, then the take is the sound
//==============================================================================
void SpectralResynthModule::toggleRecording()
{
    if (recording.load())
    {
        recording.store (false);
        juce::Thread::sleep (5);   // the audio thread's last write lands
        finishRecording();
        return;
    }
    const juce::ScopedLock sl (recLock);
    const auto cap = (size_t) (kMaxRecordSeconds * sampleRate) + 1;
    if (recL.size() < cap)
    {
        recL.assign (cap, 0.0f);   // here, never on the audio thread
        recR.assign (cap, 0.0f);
    }
    recCount.store (0);
    takeReady.store (false);
    recording.store (true, std::memory_order_release);
    recTimer->startTimerHz (10);
}

void SpectralResynthModule::globalTapSample (const StereoFrame& frame)
{
    if (! recording.load (std::memory_order_acquire))
        return;
    const int n = recCount.load (std::memory_order_relaxed);
    if (n >= (int) recL.size())
    {
        recording.store (false);
        takeReady.store (true);
        return;
    }
    recL[(size_t) n] = frame[0];
    recR[(size_t) n] = frame[1];
    recCount.store (n + 1, std::memory_order_release);
}

double SpectralResynthModule::recordedSeconds() const
{
    return sampleRate > 0.0 ? (double) recCount.load (std::memory_order_relaxed) / sampleRate : 0.0;
}

void SpectralResynthModule::finishRecording()
{
    recTimer->stopTimer();
    const juce::ScopedLock sl (recLock);
    const int n = juce::jmin (recCount.load(), (int) recL.size());
    if (n < 64)
        return;
    auto take = std::make_shared<juce::AudioBuffer<float>> (2, n);
    take->copyFrom (0, 0, recL.data(), n);
    take->copyFrom (1, 0, recR.data(), n);
    newSoundByHand();
    setLoadedSample (std::move (take), sampleRate);   // saved with the patch like a loaded file
    recL.clear(); recL.shrink_to_fit();
    recR.clear(); recR.shrink_to_fit();
}

//==============================================================================
// the edits
//==============================================================================
std::shared_ptr<const SpectralResynthModule::Model> SpectralResynthModule::currentModel() const
{
    const juce::SpinLock::ScopedLockType sl (shareLock);
    return model;
}

std::vector<SpectralResynthModule::Edit> SpectralResynthModule::getEdits() const
{
    const juce::ScopedLock sl (editLock);
    return edits;
}

void SpectralResynthModule::setEdits (std::vector<Edit> e)
{
    {
        const juce::ScopedLock sl (editLock);
        edits = std::move (e);
    }
    editGen.fetch_add (1, std::memory_order_relaxed);
    notify();
}

void SpectralResynthModule::addEdit (Edit e)
{
    auto all = getEdits();
    all.push_back (std::move (e));
    setEdits (std::move (all));
}

void SpectralResynthModule::undoEdit()
{
    auto all = getEdits();
    if (! all.empty())
    {
        all.pop_back();
        setEdits (std::move (all));
    }
}

void SpectralResynthModule::setRootFromAnalysis()
{
    if (auto m = currentModel(); m != nullptr && m->f0 > 0.0f)
    {
        const float midi = 69.0f + 12.0f * std::log2 (m->f0 / 440.0f);
        const int root = juce::jlimit (0, 127, juce::roundToInt (midi));
        setParameter ("root", (float) root);
        setParameter ("fine", juce::jlimit (-100.0f, 100.0f, (midi - (float) root) * 100.0f));
    }
}

namespace
{
    const char* opKey (SpectralResynthModule::Op op)
    {
        static const char* keys[] = { "delete", "keep", "gain", "transpose", "reverse", "rotate", "smear",
                                      "noteRemove", "noteGain", "noteTranspose", "noteUnison", "noteFundOnly", "noteNoFund", "noteOddOnly" };
        return keys[(int) op];
    }
}

juce::String SpectralResynthModule::saveCustomState() const
{
    const auto e = getEdits();
    if (e.empty())
        return {};
    juce::XmlElement x ("SpectralResynth");
    x.setAttribute ("version", 2);
    for (const auto& ed : e)
    {
        auto* c = x.createNewChildElement ("E");
        c->setAttribute ("op", opKey (ed.op));
        if (ed.amount != 0.0f)  c->setAttribute ("a", ed.amount);
        if (ed.amount2 != 0.0f) c->setAttribute ("b", ed.amount2);
        for (const auto& r : ed.regions)
        {
            auto* rc = c->createNewChildElement ("R");
            rc->setAttribute ("t0", r.t0); rc->setAttribute ("t1", r.t1);
            rc->setAttribute ("f0", r.f0); rc->setAttribute ("f1", r.f1);
            if (r.isShape())
            {
                // a drawn shape: its outline, "seconds,Hz" points
                juce::StringArray pts;
                for (const auto& p : r.poly)
                    pts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 2));
                rc->setAttribute ("shape", pts.joinIntoString (" "));
            }
        }
        for (const auto& p : ed.note.pts)
        {
            auto* nc = c->createNewChildElement ("N");
            nc->setAttribute ("t", p.x);
            nc->setAttribute ("f", p.y);
        }
    }
    return x.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

void SpectralResynthModule::loadCustomState (const juce::String& state)
{
    std::vector<Edit> e;
    if (auto x = juce::parseXML (state); x != nullptr && x->hasTagName ("SpectralResynth"))
    {
        auto boxOf = [] (const juce::XmlElement& r)
        {
            Box b;
            b.t0 = (float) r.getDoubleAttribute ("t0"); b.t1 = (float) r.getDoubleAttribute ("t1");
            b.f0 = (float) r.getDoubleAttribute ("f0"); b.f1 = (float) r.getDoubleAttribute ("f1");
            for (const auto& pt : juce::StringArray::fromTokens (r.getStringAttribute ("shape"), " ", ""))
                if (pt.containsChar (','))
                    b.poly.push_back ({ pt.upToFirstOccurrenceOf (",", false, false).getFloatValue(),
                                        pt.fromFirstOccurrenceOf (",", false, false).getFloatValue() });
            b.finishShape();
            return b;
        };
        for (auto* c : x->getChildWithTagNameIterator ("E"))
        {
            Edit ed;
            const auto key = c->getStringAttribute ("op");
            bool known = false;
            for (int i = 0; i <= (int) Op::NoteOddOnly; ++i)
                if (key == opKey ((Op) i)) { ed.op = (Op) i; known = true; }
            if (! known)
                continue;
            ed.amount = (float) c->getDoubleAttribute ("a");
            ed.amount2 = (float) c->getDoubleAttribute ("b");
            for (auto* r : c->getChildWithTagNameIterator ("R"))
                ed.regions.push_back (boxOf (*r));
            for (auto* n : c->getChildWithTagNameIterator ("N"))
                ed.note.pts.push_back ({ (float) n->getDoubleAttribute ("t"), (float) n->getDoubleAttribute ("f") });
            e.push_back (std::move (ed));
        }
        // the first version's boxes: Erase boxes became one Delete, Keep boxes one Keep
        Edit erase, keep;
        erase.op = Op::Delete;
        keep.op = Op::Keep;
        for (auto* b : x->getChildWithTagNameIterator ("B"))
            (b->getIntAttribute ("keep") != 0 ? keep : erase).regions.push_back (boxOf (*b));
        if (! erase.regions.empty()) e.push_back (erase);
        if (! keep.regions.empty())  e.push_back (keep);
    }
    setEdits (std::move (e));
}

//==============================================================================
// the analysis thread
//==============================================================================
SpectralResynthModule::AnalysisKey SpectralResynthModule::currentKey() const
{
    AnalysisKey k;
    k.counter = getSampleChangeCounter();
    k.engine = getParameter ("reassign") > 0.5f ? 1 : 0;
    k.order = 10 + juce::jlimit (0, 4, (int) getParameter ("fft"));
    if (k.engine == 1)
    {
        k.hopDenom = 4 << juce::jlimit (0, 2, (int) getParameter ("hop"));
        k.window = juce::jlimit (0, 3, (int) getParameter ("window"));
        k.decim = 1 << juce::jlimit (0, 3, (int) getParameter ("decim"));
        k.maxPeaks = juce::jlimit (4, kMaxPeaksLimit, (int) getParameter ("maxPeaks"));
        k.minLen = juce::jlimit (1, 32, (int) getParameter ("minLen"));
        k.threshold = std::round (getParameter ("threshold"));
    }
    else
        k.rate = sampleRate;   // Bins: the sound is resampled to the engine's rate
    return k;
}

bool SpectralResynthModule::keepGoing (const AnalysisKey& key) const
{
    return ! threadShouldExit() && currentKey() == key;
}

void SpectralResynthModule::freeRetired()
{
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const std::shared_ptr<const void>& p) { return p.use_count() <= 1; }),
                   retired.end());
}

bool SpectralResynthModule::waitUntilReady (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    notify();
    while (juce::Time::getMillisecondCounter() < until)
    {
        if (idle.load() && ! analysing.load() && currentKey() == lastKey && lastEditGen == editGen.load())
            return true;
        juce::Thread::sleep (5);
    }
    return false;
}

void SpectralResynthModule::run()
{
    while (! threadShouldExit())
    {
        idle.store (false);
        const auto key = currentKey();
        if (! (key == lastKey))
        {
            double rate = 44100.0;
            auto sound = getLoadedSample (&rate);
            std::shared_ptr<PeakBase> pb;
            std::shared_ptr<BinModel> bb;
            float f0 = 0.0f, conf = 0.0f;
            if (sound != nullptr && sound->getNumSamples() > 16)
            {
                analysing.store (true);
                progress.store (0.0f);
                if (key.engine == 1)
                {
                    pb = analysePeaks (key, *sound, rate);
                    if (pb != nullptr) { f0 = pb->f0; conf = pb->f0Confidence; }
                }
                else
                    bb = analyseBins (key, *sound, rate, f0, conf);
                analysing.store (false);
            }
            if (threadShouldExit())
                break;
            if (! (currentKey() == key))
                continue;   // settings changed again while it ran: start over

            lastKey = key;
            peakBase = pb;
            if (binBase != nullptr) retired.push_back (binBase);
            binBase = bb;
            baseF0 = f0;
            baseF0Confidence = conf;
            lastEditGen = -1;   // the edits go onto the new analysis
        }

        const int gen = editGen.load();
        if (gen != lastEditGen)
        {
            const auto ed = getEdits();
            std::shared_ptr<Model> m;
            if (peakBase != nullptr || binBase != nullptr)
            {
                analysing.store (true);
                m = std::make_shared<Model>();
                m->binsEngine = binBase != nullptr;
                double rate = 44100.0;
                getLoadedSample (&rate);
                m->fileRate = rate;
                m->f0 = baseF0;
                m->f0Confidence = baseF0Confidence;
                m->editsApplied = (int) ed.size();
                if (peakBase != nullptr)
                {
                    m->partials = buildPartials (*peakBase, ed);
                    m->duration = peakBase->duration;
                }
                else
                {
                    m->bins = editBins (binBase, ed);
                    m->duration = (double) juce::jmax (0, binBase->numFrames - 1) * binBase->hopSeconds();
                }
                m->serial = nextSerial++;
                analysing.store (false);
                if (rootFromNextAnalysis.exchange (false) && m->f0 > 0.0f && m->f0Confidence >= 0.35f)
                {
                    const float midi = 69.0f + 12.0f * std::log2 (m->f0 / 440.0f);
                    const int root = juce::jlimit (0, 127, juce::roundToInt (midi));
                    setParameter ("root", (float) root);
                    setParameter ("fine", juce::jlimit (-100.0f, 100.0f, (midi - (float) root) * 100.0f));
                }
            }
            if (! (currentKey() == lastKey))
                continue;
            std::shared_ptr<const Model> old;
            {
                const juce::SpinLock::ScopedLockType sl (shareLock);
                old = model;
                model = m;
            }
            if (old != nullptr)
                retired.push_back (old);
            lastEditGen = gen;
        }

        freeRetired();
        if (currentKey() == lastKey && editGen.load() == lastEditGen)
        {
            idle.store (true);
            wait (60);
        }
    }
}

//==============================================================================
// playback: what both engines share
//==============================================================================
void SpectralResynthModule::blockStart()
{
    const juce::SpinLock::ScopedLockType sl (shareLock);
    latchedModel = model;
}

void SpectralResynthModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    auto& vc = voices[(size_t) v];
    vc.numSlots = 0;
    vc.pos = 1 << 30;
    vc.ended = false;
    vc.t = 0.0;
    vc.dir = 1;
    vc.primed = false;
    vc.lastFrameInt = -1000000;
    vc.prevFrame = -1000000;
    std::fill (std::begin (vc.bandIc1), std::end (vc.bandIc1), 0.0f);
    std::fill (std::begin (vc.bandIc2), std::end (vc.bandIc2), 0.0f);
    std::fill (std::begin (vc.noiseGain), std::end (vc.noiseGain), 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        std::fill (vc.ola[c].begin(), vc.ola[c].end(), 0.0f);
        std::fill (vc.out[c].begin(), vc.out[c].end(), 0.0f);
    }
    gateLvl[v] = 0.0f;
    gateOn[v] = false;
    vel[v] = 1.0f;
    voicePos[(size_t) v].store (-1.0f, std::memory_order_relaxed);
}

void SpectralResynthModule::voiceNoteOn (int v, int note, bool retrigger)
{
    juce::ignoreUnused (retrigger);
    pool.noteOn (v, voiceLimit());
    auto& vc = voices[(size_t) v];
    vc.ended = false;
    vc.dir = 1;
    vc.pos = 1 << 30;   // render on the first sample
    vc.primed = false;
    vc.lastFrameInt = -1000000;
    vc.prevFrame = -1000000;
    for (int c = 0; c < 2; ++c)
        std::fill (vc.ola[c].begin(), vc.ola[c].end(), 0.0f);   // a new note: no tail of the last one in this voice
    seedCounter = seedCounter * 1664525u + 1013904223u;
    vc.seed = seedCounter;

    auto m = latchedModel != nullptr ? latchedModel : currentModel();
    if (m != nullptr)
    {
        const double a = juce::jlimit (0.0f, 1.0f, param (pStart)) * m->duration;
        const double b = juce::jlimit (0.0f, 1.0f, param (pEnd)) * m->duration;
        const double from = param (pSpeed) < 0.0f ? juce::jmax (a, b) : juce::jmin (a, b);
        if (m->binsEngine)
            vc.srcFrame = std::round (from / m->bins->hopSeconds());   // on the frame grid: played back exactly
        else
            vc.t = from;
    }
    gateLvl[v] = 0.0f;
    gateOn[v] = true;
    glide.noteOn (v, (float) note, isMonoVoice());
}

void SpectralResynthModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gateOn[v] = false;
}

// one step on through the sound (seconds or frames, as ts / te are), by
// Loop: Once ends, Loop wraps, Ping-Pong turns; Freeze holds Position
double SpectralResynthModule::advance (Voice& vc, double step, double ts, double te)
{
    const double span = juce::jmax (1.0e-9, te - ts);
    double& t = vc.t;
    if (param (pFreeze) > 0.5f)
        return t = ts + juce::jlimit (0.0f, 1.0f, param (pPosition)) * span;
    if (vc.ended)
        return t;
    const int loop = (int) param (pLoop);
    if (loop == 0)
    {
        t += step;
        if (t > te || t < ts)
        {
            vc.ended = true;
            t = juce::jlimit (ts, te, t);
        }
    }
    else if (loop == 1)
    {
        t += step;
        if (t >= te || t < ts)
            t = ts + std::fmod (std::fmod (t - ts, span) + span, span);
    }
    else
    {
        t += step * vc.dir;
        for (int guard = 0; guard < 4 && (t > te || t < ts); ++guard)
        {
            if (t > te) { t = 2.0 * te - t; vc.dir = -vc.dir; }
            if (t < ts) { t = 2.0 * ts - t; vc.dir = -vc.dir; }
        }
        t = juce::jlimit (ts, te, t);
    }
    return t;
}

float SpectralResynthModule::pitchRatio (int v, float fmIn)
{
    float note = 0.0f;
    for (int i = 0; i < voices[(size_t) v].blockLen; ++i)
        note = glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate);
    const float semis = note + pitchBendSemitones() + param (pTranspose) - (param (pRoot) + param (pFine) * 0.01f);
    return std::pow (2.0f, semis / 12.0f) * juce::jmax (0.0f, 1.0f + fmIn);
}

void SpectralResynthModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    outputs[0] = { 0.0f, 0.0f };
    const float voiceGain = pool.nextGain (v, sampleRate);
    auto& vc = voices[(size_t) v];
    if (pool.isSilent (v))
    {
        vc.pos = 1 << 30;
        return;
    }

    if (vc.pos >= vc.blockLen)
    {
        if (latchedModel != nullptr && latchedModel->binsEngine)
            renderBins (v, inputs[0][0]);
        else
            renderPartials (v, inputs[0][0]);
        vc.pos = 0;
    }

    float gain;
    if (isInputConnected (1))
        gain = inputs[1][0];
    else
    {
        const float step = (float) (1.0 / (0.003 * sampleRate));
        const float target = gateOn[v] ? 1.0f : 0.0f;
        if (gateLvl[v] < target)      gateLvl[v] = juce::jmin (target, gateLvl[v] + step);
        else if (gateLvl[v] > target) gateLvl[v] = juce::jmax (target, gateLvl[v] - step);
        gain = gateLvl[v];
    }
    const float g = gain * param (pVolume) * vel[v] * voiceGain;
    outputs[0][0] = vc.out[0][(size_t) vc.pos] * g;
    outputs[0][1] = vc.out[1][(size_t) vc.pos] * g;
    ++vc.pos;
}

//==============================================================================
static ModuleDescriptor spectralResynthDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.spectralresynth";
    d.displayName = "Spectral Resynthesis";
    d.description =
        "A sound - loaded, or recorded from Rec In (up to two minutes, saved with the patch) - taken apart and played "
        "from the keys. Bins plays its own spectrum back (untouched: as it was); Partials plays it as sine partials "
        "and noise. Select regions to delete, keep, transpose, reverse, rotate or turn up, one edit after another; tap "
        "a note to remove, move, double or thin it with all its harmonics. Speed, Freeze and Position play time "
        "without touching the pitch. FM In bends the pitch, Env In takes an ADSR.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 3;
    d.sockets = {
        modIn ("fmIn",  "FM In"),
        modIn ("envIn", "Env In"),
        midiIn ("addMidiIn", "Add Midi In"),
        audioIn ("recIn", "Rec In"),
        audioOut ("audioOut", "Audio Out")
    };

    auto hz = [] (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + "k" : juce::String (juce::roundToInt (v)) + " Hz"; };
    auto lowCut = makeRotary ("lowCut", "Low Cut", 20.0f, 20000.0f, 20.0f, 3, "Hz", true);
    lowCut.textFromValue = hz;
    auto highCut = makeRotary ("highCut", "High Cut", 20.0f, 20000.0f, 20000.0f, 3, "Hz", true);
    highCut.textFromValue = hz;
    auto partialsOnly = [] (ParamSpec p) { return p.visibleWhen ("reassign", 1.0f); };

    d.params = {
        // Sound
        makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.8f, 0),
        makeButton ("loadSample", "Load Sound", 0, 2),
        makeButton ("record", "Record", 0, 2),
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, 8.0f, 0, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 0, "ms").visibleWhen ("voices", 1.0f),

        // Analysis (each of these analyses the sound again)
        makeCombo ("reassign", "Engine", { "Bins", "Partials" }, 0, 1, 2).noMod().whenMissing (1.0f),
        makeCombo ("fft", "FFT", fftChoices(), 2, 1, 1).noMod(),
        partialsOnly (makeCombo ("hop", "Hop", hopChoices(), 1, 1, 1).noMod()),
        partialsOnly (makeCombo ("window", "Window", windowChoices(), 3, 1, 1).noMod()),
        partialsOnly (makeCombo ("decim", "Decimate", decimChoices(), 0, 1, 1).noMod()),
        partialsOnly (makeRotary ("threshold", "Floor", -100.0f, -10.0f, -60.0f, 1, "dB", false, 1.0f).noMod()),
        partialsOnly (makeRotary ("maxPeaks", "Peaks", 8.0f, (float) SpectralResynthModule::kMaxPeaksLimit, 128.0f, 1, {}, false, 1.0f).noMod()),
        partialsOnly (makeRotary ("minLen", "Min Len", 1.0f, 32.0f, 4.0f, 1, "hops", false, 1.0f).noMod()),

        // Playback
        makeRotary ("speed", "Speed", -2.0f, 4.0f, 1.0f, 2, "x"),
        makeRotary ("start", "Start", 0.0f, 1.0f, 0.0f, 2),
        makeRotary ("end", "End", 0.0f, 1.0f, 1.0f, 2),
        makeCombo ("loop", "Loop", { "Once", "Loop", "Ping-Pong" }, 1, 2, 1),
        makeCombo ("freeze", "Freeze", { "Off", "On" }, 0, 2, 1),
        makeRotary ("position", "Position", 0.0f, 1.0f, 0.0f, 2),
        makeSteppedList ("root", "Root", midiNoteNameChoices(), 60, 2).noMod(),
        makeRotary ("fine", "Fine", -100.0f, 100.0f, 0.0f, 2, "ct"),

        // Pitch & Tone
        makeRotary ("transpose", "Transpose", -48.0f, 48.0f, 0.0f, 3, "st"),
        partialsOnly (makeRotary ("shiftHz", "Shift", -1000.0f, 1000.0f, 0.0f, 3, "Hz")),
        partialsOnly (makeRotary ("scatter", "Scatter", 0.0f, 100.0f, 0.0f, 3, "ct")),
        makeRotary ("tilt", "Tilt", -12.0f, 12.0f, 0.0f, 3, "dB"),
        lowCut,
        highCut,
        partialsOnly (makeRotary ("sines", "Sines", 0.0f, 2.0f, 1.0f, 4)),
        partialsOnly (makeRotary ("noise", "Noise", 0.0f, 2.0f, 1.0f, 4)),
        partialsOnly (makeRotary ("partials", "Partials", 1.0f, 2048.0f, 2048.0f, 4, {}, true, 1.0f)),   // the N strongest tracks
        partialsOnly (makeRotary ("spread", "Spread", 0.0f, 1.0f, 0.25f, 4))
    };
    d.rowSections = { { 0, "Sound" }, { 1, "Analysis" }, { 2, "Playback" }, { 3, "Pitch & Tone" } };
    d.extraSection = "Editor";
    return d;
}

AQUANODE_REGISTER_MODULE (SpectralResynthModule, spectralResynthDescriptor)
