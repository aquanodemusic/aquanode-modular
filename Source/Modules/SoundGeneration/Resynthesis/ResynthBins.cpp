//==============================================================================
// Spectral Resynthesis - the Bins engine: the sound's own short-time spectrum
// (Hann frames, a quarter-frame hop), edited bin by bin, and played back by an
// overlap-add phase vocoder.
//
// Untouched, at Root and Speed 1, every frame goes back through the inverse
// transform with its own phases: Hann analysis x Hann synthesis at four
// overlap adds up to 1.5 everywhere, so the sound comes out as it went in
// (the magnitudes are kept to 1/256 dB, the phases to 16 bits: about -70 dB).
// Otherwise - another key, another Speed, Freeze, between two frames, or a
// bin an edit moved - the phases run on from the previous frame at each
// peak's own frequency, and every bin around a peak keeps its phase relative
// to the peak (phase locking): a peak's region moves as one when the pitch
// does, which keeps the partials clean.
//==============================================================================
#include "SpectralResynthShared.h"

using namespace aquanode;
using namespace aquanode::resynthdetail;

namespace
{
    constexpr float kOlaNorm = 1.0f / 1.5f;   // Hann x Hann at a quarter-frame hop

    inline float hannAt (int n, int N) { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N); }
}

//==============================================================================
// the analysis
//==============================================================================
std::shared_ptr<SpectralResynthModule::BinModel> SpectralResynthModule::analyseBins (const AnalysisKey& key,
                                                                                   const juce::AudioBuffer<float>& sound,
                                                                                   double fileRate, float& f0, float& f0Confidence)
{
    const double rate = key.rate > 0.0 ? key.rate : fileRate;
    const int channels = juce::jlimit (1, 2, sound.getNumChannels());

    // the sound at the engine's rate (so a frame's bins are this rate's bins)
    juce::AudioBuffer<float> x;
    if (std::abs (rate - fileRate) < 0.5)
        x.makeCopyOf (sound);
    else
    {
        const double speed = fileRate / rate;
        const int outLen = (int) std::ceil (sound.getNumSamples() / speed);
        x.setSize (channels, outLen);
        for (int c = 0; c < channels; ++c)
        {
            juce::WindowedSincInterpolator interp;
            std::vector<float> in ((size_t) sound.getNumSamples() + 256, 0.0f);
            std::memcpy (in.data(), sound.getReadPointer (c), sizeof (float) * (size_t) sound.getNumSamples());
            interp.process (speed, in.data(), x.getWritePointer (c), outLen);
        }
    }

    auto m = std::make_shared<BinModel>();
    m->N = 1 << key.order;
    m->hop = m->N / 4;
    m->bins = m->N / 2 + 1;
    m->channels = channels;
    m->rate = rate;
    const int N = m->N, hop = m->hop, bins = m->bins;
    const int len = x.getNumSamples();
    m->numFrames = len / hop + 2;

    juce::dsp::FFT fft (key.order);
    std::vector<float> work ((size_t) N * 2), window ((size_t) N);
    for (int n = 0; n < N; ++n) window[(size_t) n] = hannAt (n, N);

    float top = 0.0f;
    m->frames.reserve ((size_t) m->numFrames);
    for (int f = -1; f < m->numFrames; ++f)   // -1: the frame before the first (its tail overlaps the sound's first hop)
    {
        auto fr = std::make_shared<BinFrame>();
        fr->mag.resize ((size_t) (channels * bins));
        fr->phase.resize ((size_t) (channels * bins));
        const int start = f * hop - N / 2;   // frame f is centred on sample f * hop
        for (int c = 0; c < channels; ++c)
        {
            const float* d = x.getReadPointer (c);
            std::fill (work.begin(), work.end(), 0.0f);
            for (int n = 0; n < N; ++n)
            {
                const int s = start + n;
                if (s >= 0 && s < len)
                    work[(size_t) n] = d[s] * window[(size_t) n];
            }
            fft.performRealOnlyForwardTransform (work.data(), true);
            for (int k = 0; k < bins; ++k)
            {
                const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
                const float a = std::sqrt (re * re + im * im);
                top = juce::jmax (top, a);
                fr->mag[(size_t) (c * bins + k)] = encodeMag (a);
                fr->phase[(size_t) (c * bins + k)] = encodePhase (std::atan2 (im, re));
            }
        }
        if (f < 0)
        {
            m->before = std::move (fr);
            continue;
        }
        m->frames.push_back (std::move (fr));
        if ((f & 63) == 0)
        {
            progress.store ((float) f / (float) m->numFrames);
            if (! keepGoing (key))
                return nullptr;
        }
    }
    m->topDb = 20.0f * std::log10 (top + 1.0e-20f);

    // the sound's fundamental, from every other frame's peaks
    Model probe;
    probe.binsEngine = true;
    probe.bins = m;
    std::vector<Peak> peaks;
    std::vector<std::pair<float, double>> f0s;
    double totalEnergy = 0.0;
    for (int f = 0; f < m->numFrames; f += 2)
    {
        framePeaks (probe, f, peaks);
        double e = 0.0;
        for (const auto& p : peaks) e += (double) p.amp * p.amp;
        totalEnergy += e;
        float conf = 0.0f;
        const float ff = frameFundamental (peaks, conf);
        if (conf >= 0.6f && e > 0.0)
            f0s.push_back ({ ff, e });
    }
    soundFundamental (std::move (f0s), totalEnergy, f0, f0Confidence);
    return m;
}

//==============================================================================
// the edits: frames copied when an edit first touches them (the rest stay shared)
//==============================================================================
namespace
{
    using Edit = SpectralResynthModule::Edit;
    using Op = SpectralResynthModule::Op;
    using BinFrame = SpectralResynthModule::BinFrame;
    using BinModel = SpectralResynthModule::BinModel;

    struct BinEditor
    {
        BinEditor (const BinModel& baseModel, BinModel& target) : base (baseModel), m (target), owned ((size_t) target.numFrames) {}

        // a frame this edit may change: copied once, given each bin's frequency
        // (from the original phases) so a bin that is moved keeps its pitch
        BinFrame& writable (int f)
        {
            auto& slot = owned[(size_t) f];
            if (slot == nullptr)
            {
                slot = std::make_shared<BinFrame> (*m.frames[(size_t) f]);
                if (! slot->edited())
                {
                    slot->freq.resize ((size_t) m.bins);
                    slot->freePhase.assign ((size_t) m.bins, 0);
                    const auto& here = *base.frames[(size_t) f];
                    const auto* nextFrame = f + 1 < base.numFrames ? base.frames[(size_t) f + 1].get() : nullptr;
                    for (int k = 0; k < m.bins; ++k)
                    {
                        float bin = (float) k;
                        if (nextFrame != nullptr)
                        {
                            const float expected = juce::MathConstants<float>::twoPi * (float) k * (float) m.hop / (float) m.N;
                            const float d = princarg (decodePhase (nextFrame->phase[(size_t) k]) - decodePhase (here.phase[(size_t) k]) - expected);
                            bin = (float) k + d * (float) m.N / (juce::MathConstants<float>::twoPi * (float) m.hop);
                        }
                        slot->freq[(size_t) k] = bin;
                    }
                }
                m.frames[(size_t) f] = slot;
            }
            return *slot;
        }

        float mag (const BinFrame& fr, int c, int k) const { return decodeMag (fr.mag[(size_t) (c * m.bins + k)]); }
        void setMag (BinFrame& fr, int c, int k, float a) const { fr.mag[(size_t) (c * m.bins + k)] = encodeMag (a); }

        void scale (int f, int k0, int k1, float g)
        {
            auto& fr = writable (f);
            for (int c = 0; c < m.channels; ++c)
                for (int k = juce::jmax (0, k0); k <= juce::jmin (m.bins - 1, k1); ++k)
                    setMag (fr, c, k, mag (fr, c, k) * g);
        }

        // the bins [k0, k1] of one frame as they are (take: and cleared from it)
        struct Block { int k0 = 0; std::vector<float> mags; std::vector<juce::int16> phases; std::vector<float> freqs; };
        Block copy (int f, int k0, int k1, bool clear = false)
        {
            Block b;
            k0 = juce::jmax (0, k0);
            k1 = juce::jmin (m.bins - 1, k1);
            b.k0 = k0;
            auto& fr = writable (f);
            for (int c = 0; c < m.channels; ++c)
                for (int k = k0; k <= k1; ++k)
                {
                    b.mags.push_back (mag (fr, c, k));
                    b.phases.push_back (fr.phase[(size_t) (c * m.bins + k)]);
                    if (clear) setMag (fr, c, k, 0.0f);
                }
            for (int k = k0; k <= k1; ++k)
                b.freqs.push_back (fr.freq[(size_t) k]);
            return b;
        }
        Block take (int f, int k0, int k1) { return copy (f, k0, k1, true); }

        // a block put back shifted by `shift` bins (its frequencies times
        // freqScale), adding by power to what is there; its phases run free
        void put (int f, const Block& b, int shift, float freqScale, float gain = 1.0f)
        {
            if (b.freqs.empty()) return;
            auto& fr = writable (f);
            const int width = (int) b.freqs.size();
            for (int i = 0; i < width; ++i)
            {
                const int j = b.k0 + i + shift;
                if (j < 0 || j >= m.bins) continue;
                for (int c = 0; c < m.channels; ++c)
                {
                    const float add = b.mags[(size_t) (c * width + i)] * gain;
                    const float was = mag (fr, c, j);
                    setMag (fr, c, j, std::sqrt (was * was + add * add));
                    if (add >= was)
                        fr.phase[(size_t) (c * m.bins + j)] = b.phases[(size_t) (c * width + i)];
                }
                fr.freq[(size_t) j] = b.freqs[(size_t) i] * freqScale;
                fr.freePhase[(size_t) j] = 1;
            }
        }

        // the peaks of [k0, k1] (summed over the channels) and each one's region
        std::vector<std::pair<int, int>> regions (int f, int k0, int k1)
        {
            std::vector<std::pair<int, int>> out;
            k0 = juce::jmax (0, k0);
            k1 = juce::jmin (m.bins - 1, k1);
            if (k1 < k0) return out;
            const auto& fr = *m.frames[(size_t) f];
            std::vector<float> s ((size_t) (k1 - k0 + 1));
            for (int k = k0; k <= k1; ++k)
                for (int c = 0; c < m.channels; ++c) s[(size_t) (k - k0)] += mag (fr, c, k);
            std::vector<int> peaks;
            for (int i = 0; i < (int) s.size(); ++i)
            {
                const float v = s[(size_t) i];
                if (v <= 0.0f) continue;
                if ((i == 0 || v > s[(size_t) i - 1]) && (i + 1 == (int) s.size() || v >= s[(size_t) i + 1]))
                    peaks.push_back (i);
            }
            int from = 0;
            for (size_t p = 0; p < peaks.size(); ++p)
            {
                int to = (int) s.size() - 1;
                if (p + 1 < peaks.size())
                {
                    to = peaks[p];
                    for (int i = peaks[p]; i <= peaks[p + 1]; ++i) if (s[(size_t) i] < s[(size_t) to]) to = i;
                    to = juce::jmax (peaks[p], to - 1);
                }
                out.push_back ({ k0 + from, k0 + to });
                out.back().first = juce::jmin (out.back().first, k0 + peaks[p]);
                from = to + 1;
            }
            return out;
        }

        // the regions of [k0, k1] moved in frequency by ratio, each as one (its peak lands on peak x ratio)
        void transpose (int f, int k0, int k1, float ratio)
        {
            const auto regs = regions (f, k0, k1);
            std::vector<std::pair<Block, int>> moved;
            for (const auto& [a, b] : regs)
            {
                // the region's peak bin
                const auto& fr = *m.frames[(size_t) f];
                int peak = a;
                float best = -1.0f;
                for (int k = a; k <= b; ++k)
                {
                    float v = 0.0f;
                    for (int c = 0; c < m.channels; ++c) v += mag (fr, c, k);
                    if (v > best) { best = v; peak = k; }
                }
                const int shift = juce::roundToInt ((float) peak * ratio) - peak;
                moved.push_back ({ take (f, a, b), shift });
            }
            for (const auto& [blk, shift] : moved)
                put (f, blk, shift, ratio);
        }

        const BinModel& base;
        BinModel& m;
        std::vector<std::shared_ptr<BinFrame>> owned;
    };
}

std::shared_ptr<SpectralResynthModule::BinModel> SpectralResynthModule::editBins (const std::shared_ptr<const BinModel>& base,
                                                                                const std::vector<Edit>& ed)
{
    auto m = std::make_shared<BinModel> (*base);   // the frames shared, until an edit touches one
    if (ed.empty())
        return m;

    const double hopS = m->hopSeconds();
    auto binOf = [&] (float hz) { return hz * (float) m->N / (float) m->rate; };

    for (const auto& e : ed)
    {
        BinEditor be (*base, *m);
        const float g = juce::Decibels::decibelsToGain (e.amount);
        const float ratio = std::pow (2.0f, e.amount / 12.0f);

        if (e.isNoteOp())
        {
            const int fa = juce::jmax (0, m->frameAt (e.note.start()) - 1);
            const int fb = juce::jmin (m->numFrames - 1, m->frameAt (e.note.end()) + 1);
            const int voices = juce::jlimit (2, 7, (int) e.amount);
            for (int f = fa; f <= fb; ++f)
            {
                const float t = (float) (f * hopS);
                if (t < e.note.start() - (float) hopS || t > e.note.end() + (float) hopS) continue;
                const float c = e.note.f0At (t);
                if (c <= 0.0f) continue;
                // each harmonic's bins: its main lobe (2.5 bins) or 35 cents, whichever is wider
                std::vector<std::tuple<int, int, int, float>> harmonics;   // h, k0, k1, centre
                for (int h = 1; c * (float) h < (float) m->rate * 0.5f && h < 400; ++h)
                {
                    const float kc = binOf (c * (float) h);
                    const float half = juce::jmax (2.5f, kc * (std::pow (2.0f, 35.0f / 1200.0f) - 1.0f));
                    harmonics.push_back ({ h, juce::roundToInt (kc - half), juce::roundToInt (kc + half), kc });
                }
                auto drop = [&] (int h) { return (e.op == Op::NoteRemove) || (e.op == Op::NoteFundOnly && h >= 2)
                                              || (e.op == Op::NoteNoFund && h == 1) || (e.op == Op::NoteOddOnly && (h & 1) == 0); };
                if (e.op == Op::NoteGain)
                {
                    for (auto& [h, k0, k1, kc] : harmonics) be.scale (f, k0, k1, g);
                }
                else if (e.op == Op::NoteTranspose || e.op == Op::NoteUnison)
                {
                    std::vector<std::tuple<BinEditor::Block, float>> blocks;
                    for (auto& [h, k0, k1, kc] : harmonics)
                        blocks.push_back ({ be.take (f, k0, k1), kc });
                    for (auto& [blk, kc] : blocks)
                    {
                        if (e.op == Op::NoteTranspose)
                            be.put (f, blk, juce::roundToInt (kc * ratio) - juce::roundToInt (kc), ratio);
                        else
                            for (int i = 0; i < voices; ++i)
                            {
                                // in bins a copy needs its own bins: at least one apart
                                const float d = e.amount2 * (2.0f * (float) i / (float) (voices - 1) - 1.0f);
                                const float r = std::pow (2.0f, d / 1200.0f);
                                int shift = juce::roundToInt (kc * r) - juce::roundToInt (kc);
                                if (shift == 0 && i != (voices - 1) / 2 + ((voices & 1) == 0 ? 1 : 0) && d != 0.0f)
                                    shift = d > 0.0f ? 1 : -1;
                                be.put (f, blk, shift, r, 1.0f / std::sqrt ((float) voices));
                            }
                    }
                }
                else
                {
                    for (auto& [h, k0, k1, kc] : harmonics)
                        if (drop (h)) be.scale (f, k0, k1, 0.0f);
                }
            }
            continue;
        }

        // the bins a region covers in frame f, as [k0, k1] stretches: a box one,
        // a drawn shape as many as it crosses there
        std::vector<std::pair<float, float>> hzSpans;
        std::vector<std::pair<int, int>> spans;
        auto spansAt = [&] (const Box& r, int f)
        {
            spans.clear();
            r.slice ((float) (f * hopS), hzSpans);
            for (const auto& [lo, hi] : hzSpans)
            {
                const int a0 = juce::jlimit (0, m->bins - 1, (int) std::ceil (binOf (lo)));
                const int a1 = juce::jlimit (0, m->bins - 1, (int) std::floor (binOf (hi)));
                if (a1 >= a0) spans.push_back ({ a0, a1 });
            }
        };

        for (const auto& r : e.regions)
        {
            const int fa = juce::jlimit (0, m->numFrames - 1, (int) std::ceil (r.t0 / hopS));
            const int fb = juce::jlimit (0, m->numFrames - 1, (int) std::floor (r.t1 / hopS));
            const int k0 = juce::jlimit (0, m->bins - 1, (int) std::ceil (binOf (r.f0)));
            const int k1 = juce::jlimit (0, m->bins - 1, (int) std::floor (binOf (r.f1)));
            if (fb < fa || k1 < k0) continue;
            const int width = k1 - k0 + 1;
            switch (e.op)
            {
                case Op::Delete:
                case Op::Gain:
                    for (int f = fa; f <= fb; ++f)
                    {
                        spansAt (r, f);
                        for (const auto& [a0, a1] : spans) be.scale (f, a0, a1, e.op == Op::Delete ? 0.0f : g);
                    }
                    break;
                case Op::Transpose:
                    for (int f = fa; f <= fb; ++f)
                    {
                        spansAt (r, f);
                        for (const auto& [a0, a1] : spans) be.transpose (f, a0, a1, ratio);
                    }
                    break;
                case Op::Reverse:
                {
                    if (! r.isShape())
                    {
                        std::vector<BinEditor::Block> blocks;
                        for (int f = fa; f <= fb; ++f) blocks.push_back (be.take (f, k0, k1));
                        for (int f = fa; f <= fb; ++f) be.put (f, blocks[(size_t) (fb - f)], 0, 1.0f);
                        break;
                    }
                    // a shape: each of its cells takes the one mirrored in time, where that is inside too
                    std::vector<BinEditor::Block> snap;
                    for (int f = fa; f <= fb; ++f) snap.push_back (be.copy (f, k0, k1));
                    for (int f = fa; f <= fb; ++f)
                    {
                        spansAt (r, f);
                        const int sf = fa + fb - f;
                        const auto& src = snap[(size_t) (sf - fa)];
                        auto& fr = be.writable (f);
                        for (const auto& [a0, a1] : spans)
                            for (int k = juce::jmax (a0, k0); k <= juce::jmin (a1, k1); ++k)
                            {
                                const bool srcIn = r.contains ((float) (sf * hopS), m->binHz ((float) k));
                                for (int c = 0; c < m->channels; ++c)
                                {
                                    const size_t i = (size_t) (c * width + (k - k0));
                                    be.setMag (fr, c, k, srcIn ? src.mags[i] : 0.0f);
                                    if (srcIn) fr.phase[(size_t) (c * m->bins + k)] = src.phases[i];
                                }
                                fr.freq[(size_t) k] = src.freqs[(size_t) (k - k0)];
                                fr.freePhase[(size_t) k] = 1;
                            }
                    }
                    break;
                }
                case Op::Rotate:
                {
                    // each cell of the region takes what the turn brings to it (in time x log-frequency)
                    const float th = e.amount * juce::MathConstants<float>::pi / 180.0f;
                    const float cs = std::cos (th), sn = std::sin (th);
                    std::vector<BinEditor::Block> before;
                    for (int f = fa; f <= fb; ++f) before.push_back (be.copy (f, k0, k1));
                    const float tc = 0.5f * (r.t0 + r.t1), wt = juce::jmax (1.0e-4f, 0.5f * (r.t1 - r.t0));
                    const float l0 = std::log2 (juce::jmax (1.0f, r.f0)), l1 = std::log2 (juce::jmax (1.0f, r.f1));
                    const float lc = 0.5f * (l0 + l1), wl = juce::jmax (1.0e-4f, 0.5f * (l1 - l0));
                    for (int f = fa; f <= fb; ++f)
                    {
                        spansAt (r, f);
                        auto& fr = be.writable (f);
                        for (const auto& [a0, a1] : spans)
                            for (int k = juce::jmax (a0, k0); k <= juce::jmin (a1, k1); ++k)
                            {
                                const float u = ((float) (f * hopS) - tc) / wt;
                                const float v = (std::log2 (juce::jmax (1.0f, m->binHz ((float) k))) - lc) / wl;
                                const float su = u * cs + v * sn, sv = -u * sn + v * cs;
                                float st = 0.0f, shz = 0.0f;
                                bool srcIn = std::abs (su) <= 1.0f && std::abs (sv) <= 1.0f;
                                if (srcIn)
                                {
                                    st = tc + su * wt;
                                    shz = std::exp2 (lc + sv * wl);
                                    srcIn = ! r.isShape() || r.contains (st, shz);
                                }
                                const int sf = srcIn ? juce::jlimit (fa, fb, (int) std::lround (st / hopS)) : fa;
                                const int sk = srcIn ? juce::jlimit (k0, k1, (int) std::lround (binOf (shz))) : k0;
                                const auto& src = before[(size_t) (sf - fa)];
                                for (int c = 0; c < m->channels; ++c)
                                    be.setMag (fr, c, k, srcIn ? src.mags[(size_t) (c * width + (sk - k0))] : 0.0f);
                                fr.freq[(size_t) k] = (float) k;
                                fr.freePhase[(size_t) k] = 1;
                            }
                    }
                    break;
                }
                case Op::Smear:
                {
                    // what sounds rings on: each bin keeps the louder of itself and the
                    // ring of what came before, fading by the time set (to -60 dB in
                    // about 7 times it); a ringing bin runs on at the pitch that rang
                    const float decay = std::exp (-(float) hopS / juce::jmax (0.005f, e.amount * 0.001f));
                    std::vector<float> ring ((size_t) (m->channels * width), 0.0f), ringFreq ((size_t) width, 0.0f);
                    std::vector<juce::int16> ringPhase ((size_t) (m->channels * width), 0);
                    std::vector<char> in ((size_t) width);
                    for (int f = fa; f <= fb; ++f)
                    {
                        spansAt (r, f);
                        std::fill (in.begin(), in.end(), 0);
                        for (const auto& [a0, a1] : spans)
                            for (int k = juce::jmax (a0, k0); k <= juce::jmin (a1, k1); ++k) in[(size_t) (k - k0)] = 1;
                        auto& fr = be.writable (f);
                        for (int k = k0; k <= k1; ++k)
                        {
                            const size_t j = (size_t) (k - k0);
                            if (! in[j])
                            {
                                for (int c = 0; c < m->channels; ++c) ring[(size_t) c * (size_t) width + j] = 0.0f;
                                continue;
                            }
                            bool rings = false;
                            for (int c = 0; c < m->channels; ++c)
                            {
                                const size_t i = (size_t) c * (size_t) width + j;
                                const float a = be.mag (fr, c, k), rv = ring[i] * decay;
                                if (rv > a)
                                {
                                    be.setMag (fr, c, k, rv);
                                    fr.phase[(size_t) (c * m->bins + k)] = ringPhase[i];
                                    ring[i] = rv;
                                    rings = true;
                                }
                                else
                                {
                                    ring[i] = a;
                                    ringPhase[i] = fr.phase[(size_t) (c * m->bins + k)];
                                }
                            }
                            if (rings)
                            {
                                fr.freq[(size_t) k] = ringFreq[j];
                                fr.freePhase[(size_t) k] = 1;
                            }
                            else
                                ringFreq[j] = fr.freq[(size_t) k];
                        }
                    }
                    break;
                }
                default: break;
            }
        }
        if (e.op == Op::Keep)
        {
            // everything outside the regions goes
            std::vector<char> keep ((size_t) m->bins);
            for (int f = 0; f < m->numFrames; ++f)
            {
                std::fill (keep.begin(), keep.end(), 0);
                for (const auto& r : e.regions)
                {
                    spansAt (r, f);
                    for (const auto& [a0, a1] : spans)
                        for (int k = a0; k <= a1; ++k) keep[(size_t) k] = 1;
                }
                auto& fr = be.writable (f);
                for (int k = 0; k < m->bins; ++k)
                    if (! keep[(size_t) k])
                        for (int c = 0; c < m->channels; ++c) be.setMag (fr, c, k, 0.0f);
            }
        }
    }
    return m;
}

//==============================================================================
// playback: an overlap-add phase vocoder per voice, one frame per hop
//==============================================================================
void SpectralResynthModule::renderBins (int v, float fmIn)
{
    auto& vc = voices[(size_t) v];
    const Model* model = latchedModel.get();
    const BinModel* b = model != nullptr ? model->bins.get() : nullptr;
    if (b == nullptr)
    {
        vc.blockLen = kBlock;
        for (int c = 0; c < 2; ++c) std::fill (vc.out[c].begin(), vc.out[c].begin() + kBlock, 0.0f);
        return;
    }

    const int N = b->N, hop = b->hop, bins = b->bins;
    const int order = juce::roundToInt (std::log2 ((double) N));
    vc.blockLen = hop;

    if (model->serial != vc.modelSerial)
    {
        // other edits: carry on from here (the phases run on); another frame size: start over
        vc.modelSerial = model->serial;
        vc.lastFrameInt = -1000000;
        if (vc.modelN != N)
        {
            vc.modelN = N;
            vc.primed = false;
            vc.prevFrame = -1000000;
            for (int c = 0; c < 2; ++c) std::fill (vc.ola[c].begin(), vc.ola[c].end(), 0.0f);
        }
    }

    const float ratio = pitchRatio (v, fmIn);
    const double speed = param (pSpeed);
    const double hopS = b->hopSeconds();
    double ts = juce::jlimit (0.0f, 1.0f, param (pStart)) * model->duration / hopS;
    double te = juce::jlimit (0.0f, 1.0f, param (pEnd)) * model->duration / hopS;
    if (te < ts) std::swap (ts, te);
    te = juce::jmax (te, ts + 1.0);

    auto& sc = *scratch;
    auto& fft = sc.fft (order);
    if (sc.hannN != N)
    {
        for (int n = 0; n < N; ++n) sc.hann[(size_t) n] = hannAt (n, N);
        sc.hannN = N;
    }

    // tone: tilt and the cuts, per output bin (at the ends of their knobs, 20 Hz
    // and 20 kHz, the cuts are off: the sound keeps its lowest and highest bins)
    const float tiltExp = param (pTilt) / 6.0206f;
    const float lowCut = param (pLowCut), highCut = param (pHighCut);
    for (int j = 0; j < bins; ++j)
    {
        const float hz = juce::jmax (1.0f, b->binHz ((float) j));
        float g = 1.0f;
        if (tiltExp != 0.0f) g *= std::pow (hz / 1000.0f, tiltExp);
        if (lowCut > 20.5f && hz < lowCut)      g *= juce::jmax (0.0f, 1.0f - 3.0f * std::log2 (lowCut / hz));
        if (highCut < 19990.0f && hz > highCut) g *= juce::jmax (0.0f, 1.0f - 3.0f * std::log2 (hz / highCut));
        sc.toneGain[(size_t) j] = g;
    }

    const float twoPiHopOverN = juce::MathConstants<float>::twoPi * (float) hop / (float) N;

    // one frame at source position src, overlap-added at `offset` (samples
    // into the buffer; what falls before 0 is dropped)
    auto synthFrame = [&] (double src, int offset)
    {
        const int i = (int) std::floor (src);
        const float a = (float) (src - i);
        // frame -1 is the one before the first, there while the first is as analysed
        // (an edit there leaves the sound's first hop to fade in instead)
        const bool beforeFirst = i == -1 && b->before != nullptr && b->numFrames > 0 && ! b->frames[0]->edited();
        if ((i < 0 && ! beforeFirst) || i >= b->numFrames || vc.ended)
        {
            vc.lastFrameInt = -1000000;
            return;
        }
        const int i0 = beforeFirst ? -1 : juce::jlimit (0, b->numFrames - 1, i);
        const int i1 = beforeFirst ? 0 : juce::jmin (i0 + 1, b->numFrames - 1);
        const auto& f0 = beforeFirst ? *b->before : *b->frames[(size_t) i0];
        const auto& f1 = *b->frames[(size_t) i1];
        // the exact path: the original phases - at Root, Speed 1, on the frame grid,
        // frame after frame (or starting again where the sound jumped: a loop's wrap)
        const bool forwardOne = std::abs (speed - 1.0) < 1.0e-6 && param (pFreeze) < 0.5f;
        const bool jumped = i0 < vc.prevFrame || i0 > vc.prevFrame + 2;
        const bool exact = forwardOne && std::abs (ratio - 1.0f) < 1.0e-6f && a < 1.0e-6f
                        && (vc.lastFrameInt == i0 - 1 || ! vc.primed || jumped);
        vc.lastFrameInt = exact ? i0 : -1000000;
        vc.prevFrame = i0;

        for (int c = 0; c < 2; ++c)
        {
            const int ch = juce::jmin (c, b->channels - 1);
            auto& run = vc.runPhase[c];
            const size_t base = (size_t) (ch * bins);
            for (int k = 0; k < bins; ++k)
            {
                float m0 = decodeMag (f0.mag[base + (size_t) k]);
                if (a > 0.0f) m0 += (decodeMag (f1.mag[base + (size_t) k]) - m0) * a;
                sc.mag[(size_t) k] = m0;
                sc.inPhase[(size_t) k] = decodePhase (f0.phase[base + (size_t) k]);
                // each bin's frequency (in bins): as an edit left it, or from the phase to the next frame
                float w;
                if (f0.edited())
                    w = f0.freq[(size_t) k];
                else if (i1 != i0 && ! (f1.edited() && f1.freePhase[(size_t) k]))
                    w = (float) k + princarg (decodePhase (f1.phase[base + (size_t) k]) - sc.inPhase[(size_t) k] - twoPiHopOverN * (float) k)
                                    / twoPiHopOverN;
                else
                    w = (float) k;
                sc.omega[(size_t) k] = w;
            }

            if (exact)
            {
                for (int k = 0; k < bins; ++k)
                {
                    sc.outMag[(size_t) k] = sc.mag[(size_t) k] * sc.toneGain[(size_t) k];
                    sc.outPhase[(size_t) k] = sc.inPhase[(size_t) k];
                }
                // the bins an edit moved run on from the last frame: each stretch of
                // them split into its peaks' regions, every peak at its own frequency
                // and the bins around it locked to it (as the block was moved)
                if (f0.edited())
                    for (int k0 = 0; k0 < bins; )
                    {
                        if (! f0.freePhase[(size_t) k0]) { ++k0; continue; }
                        int k1 = k0;
                        while (k1 + 1 < bins && f0.freePhase[(size_t) k1 + 1]) ++k1;
                        int np = 0;
                        for (int k = k0; k <= k1; ++k)
                        {
                            const float mk = sc.mag[(size_t) k];
                            bool isPeak = mk > 0.0f;
                            for (int d = -2; d <= 2 && isPeak; ++d)
                                if (d != 0 && k + d >= k0 && k + d <= k1 && sc.mag[(size_t) (k + d)] > mk) isPeak = false;
                            if (isPeak) sc.peaks[(size_t) np++] = k;
                        }
                        int from = k0;
                        for (int p = 0; p < np; ++p)
                        {
                            const int pk = sc.peaks[(size_t) p];
                            int to = k1;
                            if (p + 1 < np)
                            {
                                to = pk;
                                for (int k = pk; k <= sc.peaks[(size_t) p + 1]; ++k) if (sc.mag[(size_t) k] < sc.mag[(size_t) to]) to = k;
                            }
                            const float peakPhase = run[(size_t) pk] + twoPiHopOverN * sc.omega[(size_t) pk];
                            for (int k = from; k <= to; ++k)
                                sc.outPhase[(size_t) k] = peakPhase + (sc.inPhase[(size_t) k] - sc.inPhase[(size_t) pk]);
                            from = to + 1;
                        }
                        k0 = k1 + 1;
                    }
            }
            else
            {
                // the peaks and their regions; each region moves as one, its
                // phases locked to its peak's, which runs on at its own frequency
                std::fill (sc.outMag.begin(), sc.outMag.begin() + bins, 0.0f);
                float top = 0.0f;
                for (int k = 0; k < bins; ++k) top = juce::jmax (top, sc.mag[(size_t) k]);
                const float floorMag = top * 1.0e-6f;
                int np = 0;
                for (int k = 0; k < bins; ++k)
                {
                    const float m = sc.mag[(size_t) k];
                    if (m <= floorMag) continue;
                    bool isPeak = true;
                    for (int d = -2; d <= 2 && isPeak; ++d)
                        if (d != 0 && k + d >= 0 && k + d < bins && sc.mag[(size_t) (k + d)] > m) isPeak = false;
                    if (isPeak) sc.peaks[(size_t) np++] = k;
                }
                int from = 0;
                for (int p = 0; p < np; ++p)
                {
                    const int pk = sc.peaks[(size_t) p];
                    int to = bins - 1;
                    if (p + 1 < np)
                    {
                        to = pk;
                        for (int k = pk; k <= sc.peaks[(size_t) p + 1]; ++k) if (sc.mag[(size_t) k] < sc.mag[(size_t) to]) to = k;
                    }
                    const float wOut = sc.omega[(size_t) pk] * ratio;
                    const int target = juce::roundToInt ((float) pk * ratio);
                    const int shift = target - pk;
                    if (target > 0 && target < bins)
                    {
                        const float peakPhase = run[(size_t) target] + twoPiHopOverN * wOut;
                        for (int k = from; k <= to; ++k)
                        {
                            const int j = k + shift;
                            if (j <= 0 || j >= bins) continue;
                            const float mm = sc.mag[(size_t) k] * sc.toneGain[(size_t) j];
                            if (mm > sc.outMag[(size_t) j])
                            {
                                sc.outMag[(size_t) j] = mm;
                                sc.outPhase[(size_t) j] = peakPhase + (sc.inPhase[(size_t) k] - sc.inPhase[(size_t) pk]);
                            }
                        }
                    }
                    from = to + 1;
                }
                for (int j = 0; j < bins; ++j)
                    if (sc.outMag[(size_t) j] <= 0.0f)
                        sc.outPhase[(size_t) j] = run[(size_t) j] + twoPiHopOverN * (float) j;
            }

            // back to time: the inverse transform, the Hann window, overlap-add
            auto& work = sc.work;
            for (int j = 0; j < bins; ++j)
            {
                const float ph = princarg (sc.outPhase[(size_t) j]);
                run[(size_t) j] = ph;
                work[(size_t) (2 * j)] = sc.outMag[(size_t) j] * std::cos (ph);
                work[(size_t) (2 * j + 1)] = sc.outMag[(size_t) j] * std::sin (ph);
            }
            work[1] = 0.0f;
            work[(size_t) (2 * (bins - 1) + 1)] = 0.0f;
            fft.performRealOnlyInverseTransform (work.data());
            float* ola = vc.ola[c].data();
            for (int n = juce::jmax (0, -offset); n < N; ++n)
                ola[offset + n] += work[(size_t) n] * sc.hann[(size_t) n] * kOlaNorm;
        }
        vc.primed = true;
    };

    if (! vc.primed)
    {
        // the frames before this hop's: their tails overlap its first samples
        vc.t = vc.srcFrame;
        synthFrame (vc.t - speed, -3 * hop);
        synthFrame (vc.t, -2 * hop);
        synthFrame (advance (vc, speed, ts, te), -hop);
    }
    synthFrame (advance (vc, speed, ts, te), 0);
    voicePos[(size_t) v].store ((float) ((vc.t - 2.0 * speed) * hopS), std::memory_order_relaxed);

    // this hop's samples out, the rest moved up
    for (int c = 0; c < 2; ++c)
    {
        auto& ola = vc.ola[c];
        std::copy (ola.begin(), ola.begin() + hop, vc.out[c].begin());
        std::memmove (ola.data(), ola.data() + hop, sizeof (float) * (size_t) N);
        std::fill (ola.begin() + N, ola.begin() + N + hop, 0.0f);
    }
}
