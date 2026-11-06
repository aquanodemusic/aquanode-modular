//==============================================================================
// Spectral Resynthesis - the Partials engine: the analysis (reassigned peaks
// and a noise floor per frame), the edits on that cloud of peaks, the partial
// tracks joined from it, and the bank of sine oscillators that plays them.
//==============================================================================
#include "SpectralResynthShared.h"

using namespace aquanode;
using namespace aquanode::resynthdetail;

namespace
{
    // the window as the analysis makes it: its sums turn bin levels into noise power
    void windowSums (int window, int n, double& sum, double& sumSq)
    {
        auto besselI0 = [] (double x)
        {
            double s = 1.0, term = 1.0;
            const double q = x * x / 4.0;
            for (int k = 1; k < 40; ++k) { term *= q / ((double) k * k); s += term; if (term < 1.0e-12 * s) break; }
            return s;
        };
        sum = sumSq = 0.0;
        const double twoPi = juce::MathConstants<double>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            const double ph = twoPi * i / n;
            double w;
            switch (window)
            {
                case 0:  w = 0.5 * (1.0 - std::cos (ph)); break;
                case 1:  w = 0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2 * ph) - 0.01168 * std::cos (3 * ph); break;
                case 2:  w = 0.3635819 - 0.4891775 * std::cos (ph) + 0.1365995 * std::cos (2 * ph) - 0.0106411 * std::cos (3 * ph); break;
                default:
                {
                    const double x = 2.0 * i / (n - 1) - 1.0;
                    w = besselI0 (9.0 * std::sqrt (juce::jmax (0.0, 1.0 - x * x))) / besselI0 (9.0);
                    break;
                }
            }
            sum += w;
            sumSq += w * w;
        }
    }

    // half the main lobe of each window, in bins (what a partial covers)
    int lobeHalfWidth (int window) { return window == 0 ? 2 : window == 3 ? 3 : 4; }

    struct CloudPoint { float t, hz, amp; };
}

//==============================================================================
std::shared_ptr<SpectralResynthModule::PeakBase> SpectralResynthModule::analysePeaks (const AnalysisKey& key,
                                                                                     const juce::AudioBuffer<float>& sound,
                                                                                     double rate)
{
    using namespace aquanode::resynth;

    const int total = sound.getNumSamples();
    const int chans = juce::jmax (1, sound.getNumChannels());

    Analyzer an;
    an.prepare (rate);
    Config cfg;
    cfg.order = key.order;
    cfg.hopDenom = key.hopDenom;
    cfg.window = key.window;
    cfg.decim = key.decim;
    cfg.timeReassign = true;
    cfg.maxShiftBins = (float) lobeHalfWidth (key.window) + 0.5f;   // every bin of a partial's main lobe
    cfg.thresholdDb = key.threshold;
    cfg.keepBins = true;
    cfg.maxEvents = (1 << key.order) / 2;
    an.setConfig (cfg);

    const int N = an.fftSize(), hop = an.hopSize(), decim = key.decim;
    const double fsA = an.analysisRate();
    const float binW = (float) an.binHz();
    const int nBins = an.binCount();
    const float nyq = (float) (fsA * 0.5 * (decim > 1 ? 0.88 : 0.98));

    auto base = std::make_shared<PeakBase>();
    base->fileRate = rate;
    base->duration = total / rate;
    base->hopSeconds = (double) hop * decim / rate;
    base->nyquist = nyq;
    base->binHz = binW;
    base->minLen = key.minLen;
    base->frameStart.push_back (0);

    const float lo = 40.0f, hi = juce::jmin (18000.0f, nyq);
    for (int b = 0; b <= kBands; ++b)
        base->bandEdges[(size_t) b] = lo * std::pow (hi / lo, (float) b / kBands);
    double wSum = 0.0, wSq = 0.0;
    windowSums (key.window, N, wSum, wSq);
    const double powerScale = (wSum * wSum) / (4.0 * wSq * (N / 2.0));
    const int lobe = lobeHalfWidth (key.window);

    std::vector<Peak> peaks;
    std::vector<int> eventOfBin ((size_t) nBins + 1, -1);
    std::vector<char> masked ((size_t) nBins);
    std::vector<float> bandVals;
    std::vector<std::pair<float, double>> f0s;
    double totalEnergy = 0.0;

    auto processFrame = [&]
    {
        peaks.clear();
        const float* bins = an.binData();
        double sumP = 0.0;
        for (int k = 1; k < nBins; ++k)
            sumP += std::pow (10.0, bins[k] / 10.0);
        const float rmsDb = 10.0f * std::log10 ((float) (sumP / nBins) + 1.0e-20f);
        const float thresh = juce::jmax (-110.0f, rmsDb + key.threshold);

        std::fill (eventOfBin.begin(), eventOfBin.end(), -1);
        const int ne = an.eventCount();
        const auto* evs = an.eventData();
        for (int i = 0; i < ne; ++i)
            if (evs[i].bin > 0 && evs[i].bin < nBins)
                eventOfBin[(size_t) evs[i].bin] = i;

        // the magnitude spectrum's peaks - a partial always has one, even when
        // vibrato sweeps it over several bins - each with its own bin's
        // reassigned frequency and level; a partial's neighbouring bins move
        // towards it too, noise's scatter
        for (int k = 2; k < nBins - 2; ++k)
        {
            const float b = bins[k];
            if (b < thresh || b <= bins[k - 1] || b < bins[k + 1])
                continue;
            const int e = eventOfBin[(size_t) k];
            if (e < 0)
                continue;   // the reassignment turned this bin down: a side lobe, or energy from another time
            const float hz = evs[e].hz, db = evs[e].db;
            int agree = 0, present = 0;
            for (int d : { -1, 1 })
            {
                const int en = eventOfBin[(size_t) (k + d)];
                if (en < 0) continue;
                ++present;
                if (std::abs (evs[en].hz - hz) < binW) ++agree;
            }
            if (present > 0 && agree == 0)
                continue;
            if (hz > 15.0f && hz < nyq && db > -110.0f)
                peaks.push_back ({ hz, juce::Decibels::decibelsToGain (db) });
        }
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.amp > b.amp; });
        if ((int) peaks.size() > key.maxPeaks)
            peaks.resize ((size_t) key.maxPeaks);

        // the noise floor: each band's median bin with the partials masked out
        std::fill (masked.begin(), masked.end(), (char) 0);
        for (const auto& p : peaks)
        {
            const int k = juce::roundToInt (p.hz / binW);
            for (int d = -lobe; d <= lobe; ++d)
                if (k + d >= 0 && k + d < nBins) masked[(size_t) (k + d)] = 1;
        }
        for (int b = 0; b < kBands; ++b)
        {
            const int k0 = juce::jmax (1, (int) std::ceil (base->bandEdges[(size_t) b] / binW));
            const int k1 = juce::jmin (nBins - 1, (int) std::floor (base->bandEdges[(size_t) b + 1] / binW));
            float power = 0.0f;
            if (k1 >= k0)
            {
                bandVals.clear();
                for (int k = k0; k <= k1; ++k)
                    if (! masked[(size_t) k]) bandVals.push_back (std::pow (10.0f, bins[k] / 10.0f));
                if (! bandVals.empty())
                {
                    auto mid = bandVals.begin() + (long) bandVals.size() / 2;
                    std::nth_element (bandVals.begin(), mid, bandVals.end());
                    const float est = bandVals.size() >= 3 ? *mid / 0.6931f : *std::min_element (bandVals.begin(), bandVals.end());
                    power = (float) (est * (k1 - k0 + 1) * powerScale);
                }
            }
            base->noise.push_back (power);
        }

        // the frame's fundamental, for the sound's
        float conf = 0.0f;
        const float f0 = frameFundamental (peaks, conf);
        double e = 0.0;
        for (const auto& p : peaks) e += (double) p.amp * p.amp;
        totalEnergy += e;
        if (conf >= 0.6f && e > 0.0)
            f0s.push_back ({ f0, e });

        base->peaks.insert (base->peaks.end(), peaks.begin(), peaks.end());
        base->frameStart.push_back ((int) base->peaks.size());
        ++base->numFrames;
    };

    const int pad = N * decim / 2;
    const int post = N * decim / 2 + hop * decim * 2;
    const int length = pad + total + post;
    for (int i = 0; i < length; ++i)
    {
        float x = 0.0f;
        const int s = i - pad;
        if (s >= 0 && s < total)
        {
            for (int c = 0; c < chans; ++c) x += sound.getSample (c, s);
            x /= (float) chans;
        }
        if (an.step (x))
        {
            an.analyse();
            processFrame();
        }
        if ((i & 0xffff) == 0)
        {
            progress.store ((float) i / (float) length);
            if (! keepGoing (key))
                return nullptr;
        }
    }
    soundFundamental (std::move (f0s), totalEnergy, base->f0, base->f0Confidence);
    return base;
}

//==============================================================================
// the edits, one after another, on the cloud of peaks and the noise floor
//==============================================================================
namespace
{
    using Edit = SpectralResynthModule::Edit;
    using Op = SpectralResynthModule::Op;
    using Box = SpectralResynthModule::Box;

    // how much of band [lo, hi] lies inside [f0, f1], on a log axis
    float bandOverlap (float lo, float hi, float f0, float f1)
    {
        const float a = std::log2 (juce::jmax (lo, f0)), b = std::log2 (juce::jmin (hi, f1));
        return b <= a ? 0.0f : (b - a) / juce::jmax (1.0e-6f, std::log2 (hi / lo));
    }

    // a box's own coordinates: -1..1 across its time and its log-frequency span
    struct BoxFrame
    {
        explicit BoxFrame (const Box& b)
            : tc (0.5f * (b.t0 + b.t1)), wt (juce::jmax (1.0e-4f, 0.5f * (b.t1 - b.t0))),
              lc (0.5f * (std::log2 (juce::jmax (1.0f, b.f0)) + std::log2 (juce::jmax (1.0f, b.f1)))),
              wl (juce::jmax (1.0e-4f, 0.5f * (std::log2 (juce::jmax (1.0f, b.f1)) - std::log2 (juce::jmax (1.0f, b.f0))))) {}
        float tc, wt, lc, wl;
        void toBox (float t, float hz, float& u, float& v) const { u = (t - tc) / wt; v = (std::log2 (juce::jmax (1.0f, hz)) - lc) / wl; }
        void fromBox (float u, float v, float& t, float& hz) const { t = tc + u * wt; hz = std::exp2 (lc + v * wl); }
    };

    void applyEdit (const Edit& e, std::vector<CloudPoint>& cloud, std::vector<float>& noise, int frames,
                    double hop, const std::array<float, SpectralResynthModule::kBands + 1>& edges)
    {
        constexpr int B = SpectralResynthModule::kBands;
        const float g = juce::Decibels::decibelsToGain (e.amount);
        const float ratio = std::pow (2.0f, e.amount / 12.0f);
        auto cell = [&] (int f, int b) -> float& { return noise[(size_t) f * B + (size_t) b]; };
        auto bandCentre = [&] (int b) { return std::sqrt (edges[(size_t) b] * edges[(size_t) b + 1]); };
        const float bandOct = std::log2 (edges[B] / edges[0]) / (float) B;

        if (e.isNoteOp())
        {
            std::vector<CloudPoint> out;
            out.reserve (cloud.size());
            const int voices = juce::jlimit (2, 7, (int) e.amount);
            for (const auto& p : cloud)
            {
                const int h = e.note.harmonicOf (p.t, p.hz, (float) hop);
                if (h == 0) { out.push_back (p); continue; }
                switch (e.op)
                {
                    case Op::NoteRemove:    break;
                    case Op::NoteGain:      out.push_back ({ p.t, p.hz, p.amp * g }); break;
                    case Op::NoteTranspose: out.push_back ({ p.t, p.hz * ratio, p.amp }); break;
                    case Op::NoteFundOnly:  if (h == 1) out.push_back (p); break;
                    case Op::NoteNoFund:    if (h != 1) out.push_back (p); break;
                    case Op::NoteOddOnly:   if ((h & 1) == 1) out.push_back (p); break;
                    case Op::NoteUnison:
                        for (int i = 0; i < voices; ++i)
                        {
                            const float d = e.amount2 * (2.0f * (float) i / (float) (voices - 1) - 1.0f);
                            out.push_back ({ p.t, p.hz * std::pow (2.0f, d / 1200.0f), p.amp / std::sqrt ((float) voices) });
                        }
                        break;
                    default: out.push_back (p); break;
                }
            }
            cloud = std::move (out);
            return;
        }

        auto inside = [&e] (float t, float hz) { for (const auto& r : e.regions) if (r.contains (t, hz)) return true; return false; };
        // how much of noise band b a region covers at time t (a drawn shape: its stretches there)
        std::vector<std::pair<float, float>> spans;
        auto bandShare = [&] (const Box& r, float t, int b)
        {
            r.slice (t, spans);
            float in = 0.0f;
            for (const auto& [lo, hi] : spans)
                in = juce::jmax (in, bandOverlap (edges[(size_t) b], edges[(size_t) b + 1], lo, hi));
            return in;
        };

        switch (e.op)
        {
            case Op::Delete:
            case Op::Keep:
            {
                const bool keep = e.op == Op::Keep;
                cloud.erase (std::remove_if (cloud.begin(), cloud.end(),
                                             [&] (const CloudPoint& p) { return inside (p.t, p.hz) != keep; }), cloud.end());
                for (int f = 0; f < frames; ++f)
                    for (int b = 0; b < B; ++b)
                    {
                        float in = 0.0f;
                        const float t = (float) (f * hop);
                        for (const auto& r : e.regions)
                            in = juce::jmax (in, bandShare (r, t, b));
                        cell (f, b) *= keep ? in : 1.0f - in;
                    }
                break;
            }
            case Op::Gain:
                for (auto& p : cloud) if (inside (p.t, p.hz)) p.amp *= g;
                for (const auto& r : e.regions)
                    for (int f = 0; f < frames; ++f)
                        if (const float t = (float) (f * hop); t >= r.t0 && t <= r.t1)
                            for (int b = 0; b < B; ++b)
                            {
                                const float in = bandShare (r, t, b);
                                cell (f, b) *= (1.0f - in) + in * g * g;
                            }
                break;
            case Op::Transpose:
            {
                for (auto& p : cloud) if (inside (p.t, p.hz)) p.hz *= ratio;
                const float shiftBands = std::log2 (ratio) / bandOct;
                std::vector<float> moved ((size_t) B);
                for (const auto& r : e.regions)
                    for (int f = 0; f < frames; ++f)
                    {
                        const float t = (float) (f * hop);
                        if (t < r.t0 || t > r.t1) continue;
                        std::fill (moved.begin(), moved.end(), 0.0f);
                        bool any = false;
                        for (int b = 0; b < B; ++b)
                        {
                            const float c = bandCentre (b);
                            if (! r.contains (t, c)) continue;
                            const float v = cell (f, b);
                            cell (f, b) = 0.0f;
                            const float to = (float) b + shiftBands;
                            const int b0 = (int) std::floor (to);
                            const float frac = to - (float) b0;
                            if (b0 >= 0 && b0 < B)         moved[(size_t) b0] += v * (1.0f - frac);
                            if (b0 + 1 >= 0 && b0 + 1 < B) moved[(size_t) b0 + 1] += v * frac;
                            any = true;
                        }
                        if (any)
                            for (int b = 0; b < B; ++b) cell (f, b) += moved[(size_t) b];
                    }
                break;
            }
            case Op::Reverse:
            {
                for (auto& p : cloud)
                    for (const auto& r : e.regions)
                        if (r.contains (p.t, p.hz)) { p.t = r.t0 + r.t1 - p.t; break; }
                for (const auto& r : e.regions)
                {
                    const int fa = juce::jlimit (0, frames - 1, (int) std::ceil (r.t0 / hop));
                    const int fb = juce::jlimit (0, frames - 1, (int) std::floor (r.t1 / hop));
                    for (int b = 0; b < B; ++b)
                    {
                        const float c = bandCentre (b);
                        if (c < r.f0 || c > r.f1) continue;
                        for (int i = 0; fa + i < fb - i; ++i)
                            if (r.contains ((float) ((fa + i) * hop), c) && r.contains ((float) ((fb - i) * hop), c))
                                std::swap (cell (fa + i, b), cell (fb - i, b));
                    }
                }
                break;
            }
            case Op::Rotate:
            {
                const float th = e.amount * juce::MathConstants<float>::pi / 180.0f;
                const float cs = std::cos (th), sn = std::sin (th);
                for (auto& p : cloud)
                    for (const auto& r : e.regions)
                        if (r.contains (p.t, p.hz))
                        {
                            const BoxFrame bf (r);
                            float u, v;
                            bf.toBox (p.t, p.hz, u, v);
                            bf.fromBox (u * cs - v * sn, u * sn + v * cs, p.t, p.hz);
                            break;
                        }
                // the noise in the box: each cell takes what the turn brings to it
                const std::vector<float> before = noise;
                for (const auto& r : e.regions)
                {
                    const BoxFrame bf (r);
                    for (int f = 0; f < frames; ++f)
                    {
                        const float t = (float) (f * hop);
                        if (t < r.t0 || t > r.t1) continue;
                        for (int b = 0; b < B; ++b)
                        {
                            const float c = bandCentre (b);
                            if (! r.contains (t, c)) continue;
                            float u, v, st, shz;
                            bf.toBox (t, c, u, v);
                            bf.fromBox (u * cs + v * sn, -u * sn + v * cs, st, shz);
                            float value = 0.0f;
                            if (r.contains (st, shz))
                            {
                                const int sf = juce::jlimit (0, frames - 1, (int) std::lround (st / hop));
                                const int sb = juce::jlimit (0, B - 1, (int) std::floor (std::log2 (shz / edges[0]) / bandOct));
                                value = before[(size_t) sf * B + (size_t) sb];
                            }
                            cell (f, b) = value;
                        }
                    }
                }
                break;
            }
            case Op::Smear:
            {
                // what sounds rings on: a partial carries on past where it stopped,
                // fading by the time set, wherever nothing louder sounds near it;
                // the noise bands ring the same way
                const float decay = std::exp (-(float) hop / juce::jmax (0.005f, e.amount * 0.001f));
                std::vector<std::vector<CloudPoint>> byFrame ((size_t) frames);
                for (const auto& p : cloud)
                    byFrame[(size_t) juce::jlimit (0, frames - 1, (int) std::lround (p.t / hop))].push_back (p);
                std::vector<CloudPoint> rings, next;
                const float near = std::pow (2.0f, 30.0f / 1200.0f);   // 30 cents
                for (int f = 0; f < frames; ++f)
                {
                    const float t = (float) (f * hop);
                    auto& pts = byFrame[(size_t) f];
                    next.clear();
                    for (auto rp : rings)
                    {
                        rp.amp *= decay;
                        if (rp.amp < 1.0e-4f || ! inside (t, rp.hz))
                            continue;
                        bool covered = false;
                        for (const auto& p : pts)
                            if (p.hz < rp.hz * near && p.hz > rp.hz / near && p.amp >= rp.amp) { covered = true; break; }
                        if (! covered && (int) pts.size() < SpectralResynthModule::kMaxFramePoints)
                            pts.push_back ({ t, rp.hz, rp.amp });
                        next.push_back (rp);
                    }
                    // this frame's partials (in the region) ring from here; one ring per pitch
                    for (const auto& p : pts)
                    {
                        if (! inside (t, p.hz)) continue;
                        bool merged = false;
                        for (auto& rp : next)
                            if (p.hz < rp.hz * near && p.hz > rp.hz / near)
                            {
                                if (p.amp >= rp.amp) rp = { t, p.hz, p.amp };
                                merged = true;
                                break;
                            }
                        if (! merged && next.size() < 512)
                            next.push_back ({ t, p.hz, p.amp });
                    }
                    rings.swap (next);
                }
                cloud.clear();
                for (const auto& v : byFrame)
                    cloud.insert (cloud.end(), v.begin(), v.end());

                std::vector<float> ringNoise ((size_t) B, 0.0f);
                for (int f = 0; f < frames; ++f)
                {
                    const float t = (float) (f * hop);
                    for (int b = 0; b < B; ++b)
                    {
                        if (! inside (t, bandCentre (b))) { ringNoise[(size_t) b] = 0.0f; continue; }
                        const float rv = ringNoise[(size_t) b] * decay * decay;   // the bands hold power
                        cell (f, b) = juce::jmax (cell (f, b), rv);
                        ringNoise[(size_t) b] = cell (f, b);
                    }
                }
                break;
            }
            default: break;
        }
    }
}

std::shared_ptr<SpectralResynthModule::PartialModel> SpectralResynthModule::buildPartials (const PeakBase& base,
                                                                                         const std::vector<Edit>& ed)
{
    auto m = std::make_shared<PartialModel>();
    m->hopSeconds = base.hopSeconds;
    m->nyquist = base.nyquist;
    m->numFrames = base.numFrames;
    m->bandEdges = base.bandEdges;
    m->noise = base.noise;
    const double hop = base.hopSeconds;
    const int frames = base.numFrames;

    // the peaks as a cloud of points, the edits on it
    std::vector<CloudPoint> cloud;
    cloud.reserve (base.peaks.size());
    for (int f = 0; f < frames; ++f)
        for (int i = base.frameStart[(size_t) f]; i < base.frameStart[(size_t) f + 1]; ++i)
            cloud.push_back ({ (float) (f * hop), base.peaks[(size_t) i].hz, base.peaks[(size_t) i].amp });
    for (const auto& e : ed)
        applyEdit (e, cloud, m->noise, frames, hop, base.bandEdges);

    // back into frames (an edit may have moved points in time), loudest first
    std::vector<std::vector<Peak>> byFrame ((size_t) frames);
    for (const auto& p : cloud)
    {
        if (! (p.amp > 1.0e-7f) || p.hz < 5.0f || p.hz > base.nyquist * 1.2f)
            continue;
        const int f = (int) std::lround (p.t / hop);
        if (f < 0 || f >= frames)
            continue;
        byFrame[(size_t) f].push_back ({ p.hz, p.amp });
    }
    cloud.clear();
    cloud.shrink_to_fit();

    // tracking: the loudest peaks pick their tracks first (nearest, within 40 cents a hop)
    struct RawTrack { int start; std::vector<float> hz, amp; };
    struct Active { int track; float hz; };
    std::vector<RawTrack> raw;
    std::vector<Active> active, stillActive;
    std::vector<char> taken;
    for (int f = 0; f < frames; ++f)
    {
        auto& peaks = byFrame[(size_t) f];
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.amp > b.amp; });
        if ((int) peaks.size() > kMaxFramePoints)
            peaks.resize ((size_t) kMaxFramePoints);
        taken.assign (active.size(), 0);
        stillActive.clear();
        for (const auto& p : peaks)
        {
            const float tolHz = juce::jmax (p.hz * 0.0234f, 0.2f * base.binHz);
            int best = -1;
            float bestD = tolHz;
            const auto it = std::lower_bound (active.begin(), active.end(), p.hz,
                                              [] (const Active& x, float hz) { return x.hz < hz; });
            const int mid = (int) (it - active.begin());
            for (int a = mid; a < (int) active.size() && active[(size_t) a].hz - p.hz < bestD; ++a)
                if (! taken[(size_t) a]) { bestD = active[(size_t) a].hz - p.hz; best = a; break; }
            for (int a = mid - 1; a >= 0 && p.hz - active[(size_t) a].hz < bestD; --a)
                if (! taken[(size_t) a]) { bestD = p.hz - active[(size_t) a].hz; best = a; break; }
            int t;
            if (best >= 0)
            {
                taken[(size_t) best] = 1;
                t = active[(size_t) best].track;
            }
            else
            {
                t = (int) raw.size();
                raw.push_back ({ f, {}, {} });
            }
            raw[(size_t) t].hz.push_back (p.hz);
            raw[(size_t) t].amp.push_back (p.amp);
            stillActive.push_back ({ t, p.hz });
        }
        std::swap (active, stillActive);
        std::sort (active.begin(), active.end(), [] (const Active& a, const Active& b) { return a.hz < b.hz; });
        peaks.clear();
        peaks.shrink_to_fit();
    }

    // the tracks long enough to be partials, numbered in order
    std::vector<int> idOf (raw.size(), -1);
    for (size_t r = 0; r < raw.size(); ++r)
        if ((int) raw[r].hz.size() >= base.minLen)
        {
            idOf[r] = (int) m->tracks.size();
            Track t;
            t.start = raw[r].start;
            t.length = (int) raw[r].hz.size();
            double e = 0.0, hzw = 0.0;
            float peak = 0.0f;
            for (int k = 0; k < t.length; ++k)
            {
                const double a2 = (double) raw[r].amp[(size_t) k] * raw[r].amp[(size_t) k];
                e += a2;
                hzw += a2 * raw[r].hz[(size_t) k];
                peak = juce::jmax (peak, raw[r].amp[(size_t) k]);
            }
            t.energy = (float) e;
            t.meanHz = e > 0.0 ? (float) (hzw / e) : raw[r].hz[0];
            t.peakDb = juce::Decibels::gainToDecibels (peak, -200.0f);
            t.hash = hash32 ((juce::uint32) m->tracks.size() * 2654435761u + 17u);
            m->tracks.push_back (t);
        }

    std::vector<int> counts ((size_t) frames + 1, 0);
    for (size_t r = 0; r < raw.size(); ++r)
        if (idOf[r] >= 0)
            for (int k = 0; k < (int) raw[r].hz.size(); ++k)
                ++counts[(size_t) (raw[r].start + k)];
    m->frameStart.assign ((size_t) frames + 1, 0);
    for (int f = 0; f < frames; ++f)
        m->frameStart[(size_t) f + 1] = m->frameStart[(size_t) f] + counts[(size_t) f];
    m->points.resize ((size_t) m->frameStart[(size_t) frames]);
    std::vector<int> fill (m->frameStart.begin(), m->frameStart.end());
    for (size_t r = 0; r < raw.size(); ++r)
        if (idOf[r] >= 0)
            for (int k = 0; k < (int) raw[r].hz.size(); ++k)
                m->points[(size_t) fill[(size_t) (raw[r].start + k)]++] = { idOf[r], raw[r].hz[(size_t) k], raw[r].amp[(size_t) k] };

    std::vector<int> order (m->tracks.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int) i;
    std::sort (order.begin(), order.end(), [&m] (int a, int b) { return m->tracks[(size_t) a].energy > m->tracks[(size_t) b].energy; });
    for (size_t i = 0; i < order.size(); ++i)
        m->tracks[(size_t) order[i]].rank = (int) i;
    return m;
}

//==============================================================================
// playback: a bank of sine oscillators per voice, 32-sample control blocks
//==============================================================================
void SpectralResynthModule::renderPartials (int v, float fmIn)
{
    auto& bank = *scratch;
    auto& targets = scratch->targets;
    targets.clear();

    auto& vc = voices[(size_t) v];
    vc.blockLen = kBlock;
    for (int c = 0; c < 2; ++c)
        std::fill (vc.out[c].begin(), vc.out[c].begin() + kBlock, 0.0f);

    const Model* model = latchedModel.get();
    const PartialModel* m = model != nullptr && ! model->binsEngine ? model->partials.get() : nullptr;

    // a new analysis: what was playing fades out (its tracks are not the new ones)
    const int serial = m != nullptr ? model->serial : -1;
    if (serial != vc.modelSerial)
    {
        for (int i = 0; i < vc.numSlots; ++i)
            vc.slots[(size_t) i].track = std::numeric_limits<int>::min() + i;
        vc.modelSerial = serial;
    }

    const double blockSec = kBlock / sampleRate;
    const float ratio = pitchRatio (v, fmIn);

    float t = 0.0f;
    if (m != nullptr && m->numFrames > 0)
    {
        double ts = juce::jlimit (0.0f, 1.0f, param (pStart)) * model->duration;
        double te = juce::jlimit (0.0f, 1.0f, param (pEnd)) * model->duration;
        if (te < ts) std::swap (ts, te);
        te = juce::jmax (te, ts + m->hopSeconds);
        t = (float) advance (vc, blockSec * (double) param (pSpeed), ts, te);
        voicePos[(size_t) v].store (t, std::memory_order_relaxed);

        if (! vc.ended)
        {
            // the tracks at t: the two frames around it, joined by track
            const double fpos = t / m->hopSeconds;
            const int i0 = juce::jlimit (0, m->numFrames - 1, (int) std::floor (fpos));
            const int i1 = juce::jmin (i0 + 1, m->numFrames - 1);
            const float a = juce::jlimit (0.0f, 1.0f, (float) (fpos - i0));
            int p = m->frameStart[(size_t) i0], pe = m->frameStart[(size_t) i0 + 1];
            int q = m->frameStart[(size_t) i1], qe = m->frameStart[(size_t) i1 + 1];
            if (i1 == i0) q = qe;
            while ((p < pe || q < qe) && (int) targets.size() < kMaxSlots * 2)
            {
                const auto* A = p < pe ? &m->points[(size_t) p] : nullptr;
                const auto* B = q < qe ? &m->points[(size_t) q] : nullptr;
                if (A != nullptr && (B == nullptr || A->track < B->track))
                {
                    targets.push_back ({ A->track, A->hz, A->amp * (i1 == i0 ? 1.0f : 1.0f - a) });
                    ++p;
                }
                else if (B != nullptr && (A == nullptr || B->track < A->track))
                {
                    targets.push_back ({ B->track, B->hz, B->amp * a });
                    ++q;
                }
                else
                {
                    targets.push_back ({ A->track, A->hz + (B->hz - A->hz) * a, A->amp + (B->amp - A->amp) * a });
                    ++p; ++q;
                }
            }
        }
    }

    // what each track becomes
    const float sines = param (pSines);
    const int keepRank = (int) param (pPartials);
    const float tiltExp = param (pTilt) / 6.0206f;
    const float lowCut = param (pLowCut), highCut = param (pHighCut);
    const float shiftHz = param (pShiftHz);
    const float scatter = param (pScatter);
    const float spread = param (pSpread);
    const float nyqFade0 = (float) sampleRate * 0.44f, nyqFade1 = (float) sampleRate * 0.48f;
    const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float) sampleRate;

    // (at the ends of their knobs, 20 Hz and 20 kHz, the cuts are off)
    auto cutGain = [lowCut, highCut] (float f)
    {
        float g = 1.0f;
        if (lowCut > 20.5f && f < lowCut)      g *= juce::jmax (0.0f, 1.0f - 3.0f * std::log2 (lowCut / f));
        if (highCut < 19990.0f && f > highCut) g *= juce::jmax (0.0f, 1.0f - 3.0f * std::log2 (f / highCut));
        return g;
    };

    int nTargets = 0;
    for (auto& tg : targets)
    {
        if (m == nullptr || tg.track < 0 || tg.track >= (int) m->tracks.size())
            continue;
        const auto& tr = m->tracks[(size_t) tg.track];
        float g = tg.amp * sines;
        float f = tg.hz * ratio;
        if (tr.rank >= keepRank)
            g = 0.0f;
        if (scatter > 0.0f)
            f *= std::pow (2.0f, scatter * (2.0f * hashUnit (tr.hash ^ vc.seed) - 1.0f) / 1200.0f);
        f += shiftHz;
        if (f < 5.0f) { g = 0.0f; f = 5.0f; }
        if (tiltExp != 0.0f) g *= std::pow (f / 1000.0f, tiltExp);
        g *= cutGain (f);
        if (f > nyqFade0) g *= juce::jmax (0.0f, (nyqFade1 - f) / (nyqFade1 - nyqFade0));
        if (! (g > 1.0e-7f)) g = 0.0f;   // also NaN
        targets[(size_t) nTargets++] = { tg.track, f, g };
    }

    // join with the oscillators already running (both lists sorted by track)
    auto panOf = [spread] (juce::uint32 h, float& gl, float& gr)
    {
        const float pan = juce::jlimit (-1.0f, 1.0f, spread * (2.0f * hashUnit (h + 11u) - 1.0f));
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        gl = std::cos (ang) * juce::MathConstants<float>::sqrt2;
        gr = std::sin (ang) * juce::MathConstants<float>::sqrt2;
    };
    int ns = 0;
    {
        int i = 0, j = 0;
        while ((i < vc.numSlots || j < nTargets) && ns < kMaxSlots)
        {
            const Slot* s = i < vc.numSlots ? &vc.slots[(size_t) i] : nullptr;
            const auto* tg = j < nTargets ? &targets[(size_t) j] : nullptr;
            Slot out;
            float target;
            if (s != nullptr && (tg == nullptr || s->track < tg->track))
            {
                ++i;
                if (s->amp <= 0.0f) continue;   // faded out last block: done
                out = *s;                       // fades out at its last frequency
                target = 0.0f;
            }
            else
            {
                const bool fresh = s == nullptr || tg->track < s->track;
                if (fresh) ++j; else { ++i; ++j; }
                if (fresh && tg->amp <= 0.0f) continue;
                if (! fresh && s->amp <= 0.0f && tg->amp <= 0.0f) continue;
                const auto& tr = m->tracks[(size_t) tg->track];
                if (fresh)
                {
                    const float ph = juce::MathConstants<float>::twoPi * hashUnit (tr.hash * 7u + 3u);
                    out.track = tg->track;
                    out.re = std::cos (ph);
                    out.im = std::sin (ph);
                    out.amp = 0.0f;
                }
                else
                    out = *s;
                panOf (tr.hash, out.gl, out.gr);
                const float w = tg->hz * twoPiOverSr;
                out.c = std::cos (w);
                out.s = std::sin (w);
                target = tg->amp;
            }
            const float mag = out.re * out.re + out.im * out.im;   // renormalise the rotator
            const float k = mag > 1.0e-12f ? 1.0f / std::sqrt (mag) : 1.0f;
            bank.re[ns] = out.re * k;
            bank.im[ns] = out.im * k;
            bank.c[ns] = out.c;
            bank.s[ns] = out.s;
            bank.a[ns] = out.amp;
            bank.da[ns] = (target - out.amp) / (float) kBlock;
            bank.gl[ns] = out.gl;
            bank.gr[ns] = out.gr;
            out.amp = target;
            vc.next[(size_t) ns] = out;
            ++ns;
        }
    }
    const int padded = (ns + 7) & ~7;
    for (int k = ns; k < padded; ++k)
    {
        bank.re[k] = 1.0f; bank.im[k] = 0.0f; bank.c[k] = 1.0f; bank.s[k] = 0.0f;
        bank.a[k] = 0.0f; bank.da[k] = 0.0f; bank.gl[k] = 0.0f; bank.gr[k] = 0.0f;
    }

    float* outL = vc.out[0].data();
    float* outR = vc.out[1].data();
    {
       #if JUCE_USE_SIMD
        // the bank, four (SSE / NEON) lanes at a time, four samples per pass
        using V = juce::dsp::SIMDRegister<float>;
        constexpr int lanes = (int) V::size();
        static_assert (8 % lanes == 0, "slots are padded to 8");
        for (int n = 0; n < kBlock; n += 4)
        {
            V l0 (0.0f), l1 (0.0f), l2 (0.0f), l3 (0.0f), r0 (0.0f), r1 (0.0f), r2 (0.0f), r3 (0.0f);
            for (int x = 0; x < padded; x += lanes)
            {
                V re = V::fromRawArray (bank.re + x), im = V::fromRawArray (bank.im + x);
                const V c = V::fromRawArray (bank.c + x), sn = V::fromRawArray (bank.s + x);
                V a = V::fromRawArray (bank.a + x);
                const V da = V::fromRawArray (bank.da + x);
                const V gl = V::fromRawArray (bank.gl + x), gr = V::fromRawArray (bank.gr + x);
                auto step = [&] (V& accL, V& accR)
                {
                    const V nr = re * c - im * sn;
                    im = re * sn + im * c;
                    re = nr;
                    const V y = a * im;
                    a += da;
                    accL += y * gl;
                    accR += y * gr;
                };
                step (l0, r0); step (l1, r1); step (l2, r2); step (l3, r3);
                re.copyToRawArray (bank.re + x);
                im.copyToRawArray (bank.im + x);
                a.copyToRawArray (bank.a + x);
            }
            outL[n] = l0.sum(); outL[n + 1] = l1.sum(); outL[n + 2] = l2.sum(); outL[n + 3] = l3.sum();
            outR[n] = r0.sum(); outR[n + 1] = r1.sum(); outR[n + 2] = r2.sum(); outR[n + 3] = r3.sum();
        }
       #else
        // Portable 8-lane bank (slots are padded to a multiple of 8), four samples per
        // pass. Plain float loops so clang auto-vectorises them to NEON on ARM / SSE on
        // x86, without needing juce::dsp::SIMDRegister (not available on aarch64).
        constexpr int W = 8;
        for (int n = 0; n < kBlock; n += 4)
        {
            float accL[4][W] = {}, accR[4][W] = {};
            for (int x = 0; x < padded; x += W)
            {
                float* __restrict re = bank.re + x;
                float* __restrict im = bank.im + x;
                float* __restrict a  = bank.a + x;
                const float* __restrict c  = bank.c + x;
                const float* __restrict sn = bank.s + x;
                const float* __restrict da = bank.da + x;
                const float* __restrict gl = bank.gl + x;
                const float* __restrict gr = bank.gr + x;
                for (int j = 0; j < W; ++j)
                {
                    float r = re[j], i = im[j], amp = a[j];
                    for (int q = 0; q < 4; ++q)
                    {
                        const float nr = r * c[j] - i * sn[j];
                        i = r * sn[j] + i * c[j];
                        r = nr;
                        const float y = amp * i;
                        amp += da[j];
                        accL[q][j] += y * gl[j];
                        accR[q][j] += y * gr[j];
                    }
                    re[j] = r; im[j] = i; a[j] = amp;
                }
            }
            for (int q = 0; q < 4; ++q)
            {
                float sl = 0.0f, sr = 0.0f;
                for (int j = 0; j < W; ++j) { sl += accL[q][j]; sr += accR[q][j]; }
                outL[n + q] = sl;
                outR[n + q] = sr;
            }
        }
       #endif
    }
    for (int k = 0; k < ns; ++k)
    {
        vc.next[(size_t) k].re = bank.re[k];
        vc.next[(size_t) k].im = bank.im[k];
    }
    std::swap (vc.slots, vc.next);
    vc.numSlots = ns;

    // the noise floor: 24 band-passed noises
    const float noiseLevel = param (pNoise);
    if (m != nullptr && m->numFrames > 0 && noiseLevel > 0.0f && ! vc.ended)
    {
        const double fpos = t / m->hopSeconds;
        const int i0 = juce::jlimit (0, m->numFrames - 1, (int) std::floor (fpos));
        const int i1 = juce::jmin (i0 + 1, m->numFrames - 1);
        const float a = juce::jlimit (0.0f, 1.0f, (float) (fpos - i0));
        float a1[kBands], a2[kBands], a3[kBands], kq[kBands], g0[kBands], dg[kBands];
        int bandOf[kBands];
        int nb = 0;
        for (int b = 0; b < kBands; ++b)
        {
            const float lo = m->bandEdges[(size_t) b], hi = m->bandEdges[(size_t) b + 1];
            const float fc = std::sqrt (lo * hi) * ratio;
            const float bw = (hi - lo) * ratio;
            float target = 0.0f;
            if (fc < nyqFade0 && fc > 10.0f)
            {
                const float power = m->noise[(size_t) i0 * kBands + (size_t) b] * (1.0f - a)
                                  + m->noise[(size_t) i1 * kBands + (size_t) b] * a;
                const float enbw = juce::MathConstants<float>::halfPi * bw;
                target = noiseLevel * std::sqrt (juce::jmax (0.0f, power) / juce::jmin (1.0f, enbw / ((float) sampleRate * 0.5f)));
                target *= cutGain (fc);
                if (tiltExp != 0.0f) target *= std::pow (fc / 1000.0f, tiltExp);
            }
            if (target <= 0.0f && vc.noiseGain[b] <= 0.0f)
                continue;
            const float gg = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, nyqFade1) / (float) sampleRate);
            const float k = juce::jmax (0.02f, bw / juce::jmax (1.0f, fc));
            a1[nb] = 1.0f / (1.0f + gg * (gg + k));
            a2[nb] = gg * a1[nb];
            a3[nb] = gg * a2[nb];
            kq[nb] = k;
            g0[nb] = vc.noiseGain[b];
            dg[nb] = (target - vc.noiseGain[b]) / (float) kBlock;
            vc.noiseGain[b] = target;
            bandOf[nb] = b;
            ++nb;
        }
        if (nb > 0)
            for (int n = 0; n < kBlock; ++n)
            {
                vc.noiseState = vc.noiseState * 1664525u + 1013904223u;
                const float white = ((float) (vc.noiseState >> 8) * (1.0f / 8388608.0f) - 1.0f) * 1.7320508f;
                float y = 0.0f;
                for (int j = 0; j < nb; ++j)
                {
                    const int b = bandOf[j];
                    float& ic1 = vc.bandIc1[b];
                    float& ic2 = vc.bandIc2[b];
                    const float v3 = white - ic2;
                    const float v1 = a1[j] * ic1 + a2[j] * v3;
                    const float v2 = ic2 + a2[j] * ic1 + a3[j] * v3;
                    ic1 = 2.0f * v1 - ic1;
                    ic2 = 2.0f * v2 - ic2;
                    y += kq[j] * v1 * (g0[j] + dg[j] * (float) n);
                }
                outL[n] += y;
                outR[n] += y;
            }
    }
    else
        std::fill (std::begin (vc.noiseGain), std::end (vc.noiseGain), 0.0f);
}