#pragma once

//==============================================================================
// Spectral Resynthesis's own analysis: a copy of the Reassigned Spectrogram's
// (SpectralAnalysis.h, which the visualizers keep exactly as it was), with two
// additions only this module uses:
//   - every reassigned point remembers the bin it came from (Event::bin), so
//     a peak of the magnitude spectrum can take its frequency and level from
//     its own bin's reassignment
//   - Config::maxEvents: an offline analysis may keep every bin of a frame
//
// Spectral reassignment (Auger & Flandrin): besides the windowed transform
// X_h, two more transforms - with the window's derivative (X_dh) and with the
// time-ramped window (X_th) - move each bin's energy to where it really is:
//     frequency  f = k * fs / N - fs / (2 pi) * Im (X_dh / X_h)
//     time       t = t_frame + Re (X_th / X_h)          (in samples)
// Before the FFT the signal can be decimated (an 8th-order Butterworth
// low-pass, then every n-th sample) for finer resolution over the low range.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include <complex>
#include <optional>
#include <tuple>

namespace aquanode::resynth
{
    //=== settings ==============================================================
    constexpr int kMinOrder = 10, kMaxOrder = 14;          // 1024 .. 16384 points
    constexpr int kMaxN = 1 << kMaxOrder;
    constexpr int kMaxEvents = 3072;                       // reassigned points kept per frame

    struct Config
    {
        int order { 12 };              // FFT size 2^order
        int hopDenom { 8 };            // hop = N / hopDenom
        int window { 3 };              // 0 Hann, 1 Blackman-Harris, 2 Nuttall, 3 Kaiser (beta 9)
        int decim { 1 };               // 1, 2, 4, 8, 16
        bool timeReassign { true };
        float maxShiftBins { 1.0f };   // events moved further than this are dropped (noise)
        float thresholdDb { -60.0f };  // relative to the frame's RMS level
        bool keepBins { false };       // also hand back the plain spectrum (Spectrum Compare)
        int maxEvents { kMaxEvents };  // the most points one frame keeps (an offline analysis may want every bin)

        bool sameAnalysis (const Config& o) const
        {
            return order == o.order && hopDenom == o.hopDenom && window == o.window && decim == o.decim;
        }
    };

    struct Event { float hz, db, hops; int bin = 0; };   // where (Hz), how loud (dBFS), when (hops off the frame time); the bin it came from

    //=== one input's analysis ===================================================
    class Analyzer
    {
    public:
        void prepare (double sampleRate)
        {
            rate = sampleRate;
            for (int o = kMinOrder; o <= kMaxOrder; ++o)
                if (ffts[(size_t) (o - kMinOrder)] == nullptr)
                    ffts[(size_t) (o - kMinOrder)] = std::make_unique<juce::dsp::FFT> (o);
            ring.assign ((size_t) kMaxN, 0.0f);
            frame.assign ((size_t) kMaxN, 0.0f);
            for (auto* v : { &winH, &winDH, &winTH })
                v->assign ((size_t) kMaxN, 0.0f);
            for (auto* v : { &workH, &workDH, &workTH })
                v->assign ((size_t) kMaxN * 2, 0.0f);
            bins.assign ((size_t) kMaxN / 2, -200.0f);
            events.resize ((size_t) kMaxEvents);
            configured = false;
        }

        // applies new settings; the analysis starts over when they change it
        void setConfig (const Config& c)
        {
            if ((int) events.size() < c.maxEvents)   // only ever grows past kMaxEvents off the audio thread
                events.resize ((size_t) c.maxEvents);
            if (configured && c.sameAnalysis (cfg))
            {
                cfg = c;
                return;
            }
            cfg = c;
            cfg.order = juce::jlimit (kMinOrder, kMaxOrder, cfg.order);
            N = 1 << cfg.order;
            hop = juce::jmax (1, N / juce::jmax (1, cfg.hopDenom));
            mask = N - 1;
            writePos = 0;
            hopCounter = 0;
            filled = 0;
            decimPhase = 0;
            std::fill (ring.begin(), ring.end(), 0.0f);
            buildWindows();
            buildDecimator();
            configured = true;
        }

        // the next setConfig starts the analysis over whatever it is
        void restart() { configured = false; }

        const Config& config() const { return cfg; }
        int fftSize() const { return N; }
        int hopSize() const { return hop; }
        double analysisRate() const { return rate / (double) juce::jmax (1, cfg.decim); }
        double binHz() const { return analysisRate() / (double) N; }

        // one sample in; true when a frame is due (then call analyse)
        bool step (float x) noexcept
        {
            if (cfg.decim > 1)
            {
                for (auto& b : decimator)
                    x = b.process (x);
                if (++decimPhase < cfg.decim)
                    return false;
                decimPhase = 0;
            }
            ring[(size_t) writePos] = x;
            writePos = (writePos + 1) & mask;
            filled = juce::jmin (N, filled + 1);
            if (++hopCounter < hop)
                return false;
            hopCounter = 0;
            return filled >= N;
        }

        // the frame that is due: events (and bins with keepBins) are filled in
        void analyse()
        {
            for (int i = 0; i < N; ++i)
                frame[(size_t) i] = ring[(size_t) ((writePos + i) & mask)];   // oldest first

            std::fill (workH.begin(), workH.begin() + 2 * N, 0.0f);
            std::fill (workDH.begin(), workDH.begin() + 2 * N, 0.0f);
            for (int n = 0; n < N; ++n)
            {
                workH[(size_t) n] = frame[(size_t) n] * winH[(size_t) n];
                workDH[(size_t) n] = frame[(size_t) n] * winDH[(size_t) n];
            }
            auto& fft = *ffts[(size_t) (cfg.order - kMinOrder)];
            fft.performRealOnlyForwardTransform (workH.data(), true);
            fft.performRealOnlyForwardTransform (workDH.data(), true);

            const bool doTime = cfg.timeReassign;
            if (doTime)
            {
                std::fill (workTH.begin(), workTH.begin() + 2 * N, 0.0f);
                for (int n = 0; n < N; ++n)
                    workTH[(size_t) n] = frame[(size_t) n] * winTH[(size_t) n];
                fft.performRealOnlyForwardTransform (workTH.data(), true);
            }

            auto* cH = reinterpret_cast<const std::complex<float>*> (workH.data());
            auto* cDH = reinterpret_cast<const std::complex<float>*> (workDH.data());
            auto* cTH = reinterpret_cast<const std::complex<float>*> (workTH.data());

            const float fs = (float) analysisRate();
            const float binW = fs / (float) N;
            const int nBins = N / 2;

            // the frame's RMS level: the threshold is relative to it
            double sumSq = 0.0;
            for (int k = 1; k < nBins; ++k)
                sumSq += (double) std::norm (cH[k]);
            const float rmsDb = 10.0f * std::log10 ((float) (sumSq / nBins) + 1.0e-20f) + normDb;
            const float thresh = juce::jmax (-140.0f, rmsDb + cfg.thresholdDb);
            const float maxShift = cfg.maxShiftBins * binW;

            numEvents = 0;
            for (int k = 1; k < nBins - 1; ++k)
            {
                const float mag = std::abs (cH[k]);
                const float db = 20.0f * std::log10 (mag + 1.0e-12f) + normDb;
                if (cfg.keepBins)
                    bins[(size_t) k] = db;
                if (db < thresh || numEvents >= cfg.maxEvents)
                    continue;

                const auto denom = cH[k] + std::complex<float> (1.0e-12f, 0.0f);
                const float shift = -(fs / juce::MathConstants<float>::twoPi) * (cDH[k] / denom).imag();
                if (std::abs (shift) > maxShift)
                    continue;
                const float hz = (float) k * binW + shift;
                if (hz < 1.0f || hz > fs * 0.5f)
                    continue;

                float hops = 0.0f;
                if (doTime)
                {
                    hops = (cTH[k] / denom).real() / (float) hop;
                    if (std::abs (hops) > 2.0f)
                        continue;   // energy from another frame's time: not this one's
                }
                // the bin sat off the partial by shift: its level is the window's
                // response there, so the partial's own level is put back (a bin so
                // far out that it would need more than 12 dB is a side lobe: dropped)
                const float corr = levelCorrection (shift / binW);
                if (corr > 12.0f)
                    continue;
                events[(size_t) numEvents++] = { hz, db + corr, hops, k };
            }
            if (cfg.keepBins)
            {
                bins[0] = 20.0f * std::log10 (std::abs (cH[0]) + 1.0e-12f) + normDb;
                bins[(size_t) nBins - 1] = 20.0f * std::log10 (std::abs (cH[nBins - 1]) + 1.0e-12f) + normDb;
            }
        }

        const Event* eventData() const { return events.data(); }
        int eventCount() const { return numEvents; }
        const float* binData() const { return bins.data(); }   // dBFS per bin (keepBins)
        int binCount() const { return N / 2; }

    private:
        struct Biquad
        {
            float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, x1 { 0 }, x2 { 0 }, y1 { 0 }, y2 { 0 };
            void design (double fc, double fs, double q)
            {
                const double w0 = juce::MathConstants<double>::twoPi * fc / fs;
                const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), inv = 1.0 / (1.0 + alpha);
                b0 = (float) ((1.0 - cw) * 0.5 * inv); b1 = (float) ((1.0 - cw) * inv); b2 = b0;
                a1 = (float) (-2.0 * cw * inv); a2 = (float) ((1.0 - alpha) * inv);
                x1 = x2 = y1 = y2 = 0.0f;
            }
            float process (float x) noexcept
            {
                const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                return y;
            }
        };

        static float besselI0 (float x)
        {
            // the power series - converges fast for the beta used here
            double sum = 1.0, term = 1.0;
            const double q = (double) x * x / 4.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= q / ((double) k * k);
                sum += term;
                if (term < 1.0e-12 * sum)
                    break;
            }
            return (float) sum;
        }

        void buildWindows()
        {
            const double twoPi = juce::MathConstants<double>::twoPi;
            const int half = N / 2;
            double sum = 0.0;
            for (int n = 0; n < N; ++n)
            {
                const double ph = twoPi * n / N;
                double w;
                switch (cfg.window)
                {
                    case 0:  w = 0.5 * (1.0 - std::cos (ph)); break;
                    case 1:  w = 0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2 * ph) - 0.01168 * std::cos (3 * ph); break;
                    case 2:  w = 0.3635819 - 0.4891775 * std::cos (ph) + 0.1365995 * std::cos (2 * ph) - 0.0106411 * std::cos (3 * ph); break;
                    default:
                    {
                        const double x = 2.0 * n / (N - 1) - 1.0;
                        w = besselI0 (9.0f * (float) std::sqrt (juce::jmax (0.0, 1.0 - x * x))) / besselI0 (9.0f);
                        break;
                    }
                }
                winH[(size_t) n] = (float) w;
                sum += w;
            }
            // the derivative (per sample): central differences work for every window
            winDH[0] = winH[1] - winH[0];
            winDH[(size_t) N - 1] = winH[(size_t) N - 1] - winH[(size_t) N - 2];
            for (int n = 1; n < N - 1; ++n)
                winDH[(size_t) n] = (winH[(size_t) n + 1] - winH[(size_t) n - 1]) * 0.5f;
            for (int n = 0; n < N; ++n)
                winTH[(size_t) n] = (float) (n - half) * winH[(size_t) n];

            // a full-scale sine reads 0 dBFS
            normDb = 20.0f * std::log10 (2.0f / (float) juce::jmax (1.0e-9, sum));

            // the window's response a fraction of a bin off its centre (dB, as a
            // correction to add): a reassigned point gets its partial's true level
            // back, whichever bin of the main lobe it came from (no scalloping)
            for (int i = 0; i < kCorrSteps; ++i)
            {
                const double delta = (double) i / kCorrPerBin;
                double re = 0.0, im = 0.0;
                for (int n = 0; n < N; ++n)
                {
                    const double ph = twoPi * delta * n / N;
                    re += winH[(size_t) n] * std::cos (ph);
                    im += winH[(size_t) n] * std::sin (ph);
                }
                const double mag = std::sqrt (re * re + im * im) / juce::jmax (1.0e-9, sum);
                correction[(size_t) i] = (float) juce::jlimit (0.0, 40.0, -20.0 * std::log10 (juce::jmax (1.0e-6, mag)));
            }
        }

        static constexpr int kCorrPerBin = 50, kCorrSteps = 4 * kCorrPerBin + 2;   // up to 4 bins off
        std::array<float, (size_t) kCorrSteps> correction {};

        float levelCorrection (float binsOff) const noexcept
        {
            const float x = juce::jlimit (0.0f, (float) (kCorrSteps - 2), std::abs (binsOff) * (float) kCorrPerBin);
            const int i = (int) x;
            const float f = x - (float) i;
            return correction[(size_t) i] + (correction[(size_t) i + 1] - correction[(size_t) i]) * f;
        }

        void buildDecimator()
        {
            static constexpr double q[4] = { 0.5098, 0.6013, 0.8999, 2.5629 };   // 8th-order Butterworth
            const int f = juce::jmax (1, cfg.decim);
            const double fc = (rate / (2.0 * f)) * 0.9;
            for (int i = 0; i < 4; ++i)
                decimator[(size_t) i].design (juce::jmin (fc, rate * 0.45), rate, q[i]);
        }

        double rate { 44100.0 };
        Config cfg;
        bool configured { false };
        int N { 4096 }, hop { 512 }, mask { 4095 }, writePos { 0 }, hopCounter { 0 }, filled { 0 }, decimPhase { 0 };
        float normDb { 0.0f };
        std::array<std::unique_ptr<juce::dsp::FFT>, (size_t) (kMaxOrder - kMinOrder + 1)> ffts;
        std::vector<float> ring, frame, winH, winDH, winTH, workH, workDH, workTH, bins;
        std::vector<Event> events;
        int numEvents { 0 };
        std::array<Biquad, 4> decimator;
    };

    //=== helpers for the displays =============================================
    inline juce::String noteName (float hz)
    {
        if (hz <= 0.0f)
            return {};
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int n = juce::roundToInt (midi);
        const int cents = juce::roundToInt ((midi - (float) n) * 100.0f);
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1)
             + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents) + " ct");
    }

    inline juce::String hzText (float hz)
    {
        if (hz >= 10000.0f) return juce::String (hz / 1000.0f, 1) + " kHz";
        if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 2) + " kHz";
        if (hz >= 100.0f)   return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz, 1) + " Hz";
    }

    // the largest decimation that still keeps everything up to hiHz
    inline int autoDecimation (double sampleRate, float hiHz)
    {
        int f = 1;
        while (f < 16 && (sampleRate / (2.0 * f * 2)) * 0.9 > (double) hiHz * 1.05)
            f *= 2;
        return f;
    }
}
