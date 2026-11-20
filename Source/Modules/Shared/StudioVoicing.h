#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <iterator>

// Studio voicings (6.9.1): the physical models' extra modes, fitted to studio recordings.
// The instrument's radiation toward the microphone (and the room's colour) is a short chain
// of EQ bands - a peak, a low and a high shelf - fitted per harmonic, note by note, so the
// strings, the bore and the excitation stay the model's own; only what reaches the listener
// changes. The bands were fitted with an optimiser against the recordings' spectra.
namespace aquanode::studio
{
    struct Band { char type; double hz, db, q; };   // 'L' low shelf, 'H' high shelf, 'P' peak

    struct Biquad
    {
        double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, z1 { 0 }, z2 { 0 };

        void set (const Band& b, double sr)
        {
            const double A = std::pow (10.0, b.db / 40.0);
            const double w = 2.0 * 3.14159265358979323846 * std::min (b.hz, 0.45 * sr) / sr;
            const double cw = std::cos (w), al = std::sin (w) / (2.0 * b.q), sA = std::sqrt (A);
            double n0, n1, n2, d0, d1, d2;
            if (b.type == 'L')
            {
                n0 = A * ((A + 1) - (A - 1) * cw + 2 * sA * al); n1 = 2 * A * ((A - 1) - (A + 1) * cw); n2 = A * ((A + 1) - (A - 1) * cw - 2 * sA * al);
                d0 = (A + 1) + (A - 1) * cw + 2 * sA * al;       d1 = -2 * ((A - 1) + (A + 1) * cw);     d2 = (A + 1) + (A - 1) * cw - 2 * sA * al;
            }
            else if (b.type == 'H')
            {
                n0 = A * ((A + 1) + (A - 1) * cw + 2 * sA * al); n1 = -2 * A * ((A - 1) + (A + 1) * cw); n2 = A * ((A + 1) + (A - 1) * cw - 2 * sA * al);
                d0 = (A + 1) - (A - 1) * cw + 2 * sA * al;       d1 = 2 * ((A - 1) - (A + 1) * cw);      d2 = (A + 1) - (A - 1) * cw - 2 * sA * al;
            }
            else
            {
                n0 = 1 + al * A; n1 = -2 * cw; n2 = 1 - al * A;
                d0 = 1 + al / A; d1 = -2 * cw; d2 = 1 - al / A;
            }
            b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0; a1 = d1 / d0; a2 = d2 / d0;
        }
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0.0; }
    };

    struct Voicing { const Band* bands; int count; double gainDb; };

    // the chain of one voicing (no allocation: rebuilt in place when the voicing changes)
    struct EqChain
    {
        static constexpr int kMax = 12;
        std::array<Biquad, kMax> bq;
        int n { 0 };
        double gain { 1.0 };

        void build (const Voicing& v, double sr)
        {
            n = std::min (v.count, kMax);
            for (int i = 0; i < n; ++i) bq[(size_t) i].set (v.bands[i], sr);
            gain = std::pow (10.0, v.gainDb / 20.0);
        }
        double process (double x)
        {
            for (int i = 0; i < n; ++i) x = bq[(size_t) i].process (x);
            return x * gain;
        }
        void reset() { for (auto& b : bq) b.reset(); }
    };

    //==========================================================================
    // the fitted voicings
    //==========================================================================
    // a violin, close (on the bowed strings' 157-mode violin body; violas share it)
    inline constexpr Band kViolinArco[] = {
        { 'L', 232.3, -4.42, 0.70 }, { 'H', 9264.6, -2.87, 0.70 }, { 'P', 1487.0, 16.26, 4.23 }, { 'P', 217.1, -13.82, 5.59 },
        { 'P', 279.1, 15.89, 1.79 }, { 'P', 4401.2, -7.37, 5.85 }, { 'P', 3611.5, -8.23, 0.74 }, { 'P', 5614.6, 14.28, 1.13 },
        { 'P', 1141.7, -9.11, 6.91 }, { 'H', 3000.0, 3.50, 0.70 } };
    // cellos in a hall (the basses share it)
    inline constexpr Band kCelloArco[] = {
        { 'L', 355.6, -3.08, 0.70 }, { 'H', 2261.0, 10.51, 0.70 }, { 'P', 250.1, 18.72, 0.74 }, { 'P', 1808.7, 12.37, 3.85 },
        { 'P', 10823.9, -6.22, 0.39 }, { 'P', 892.3, 12.97, 3.67 }, { 'P', 2971.9, -7.61, 3.93 }, { 'P', 5782.5, -1.72, 1.84 },
        { 'P', 3971.9, 9.68, 1.39 } };
    // the clarinet in front of the microphone (the bell and the open tone holes)
    inline constexpr Band kClarinet[] = {
        { 'L', 532.0, 1.41, 0.70 }, { 'H', 1144.3, 12.38, 0.70 }, { 'P', 3765.6, 7.17, 1.12 }, { 'P', 317.9, -7.44, 6.13 },
        { 'P', 867.2, 14.78, 6.01 }, { 'P', 1920.8, 3.26, 6.82 }, { 'P', 160.5, -2.92, 3.73 }, { 'P', 482.8, -3.33, 6.29 },
        { 'P', 7312.9, -11.19, 0.86 }, { 'H', 3000.0, -2.50, 0.70 } };
    // cello pizzicato, bright (finger snapped off the string) and mellow (the flesh of the finger)
    inline constexpr Band kPizzBright[] = {
        { 'L', 580.6, -11.53, 0.70 }, { 'H', 1086.5, -1.25, 0.70 }, { 'P', 74.9, -13.45, 8.00 }, { 'P', 1781.0, 10.84, 3.81 },
        { 'P', 659.6, -11.19, 7.05 }, { 'P', 256.1, 8.33, 8.00 }, { 'P', 498.2, -8.64, 8.00 }, { 'P', 164.1, 8.06, 1.33 },
        { 'P', 1319.3, -9.24, 1.20 }, { 'H', 3000.0, -6.00, 0.70 } };
    inline constexpr Band kPizzMellow[] = {
        { 'L', 315.8, 4.68, 0.70 }, { 'H', 1302.7, 7.97, 0.70 }, { 'P', 322.5, -10.50, 2.02 }, { 'P', 2272.2, 7.83, 1.19 },
        { 'P', 805.6, 9.46, 6.14 }, { 'P', 98.4, -8.16, 2.06 }, { 'P', 136.6, 9.55, 1.68 }, { 'P', 992.6, 6.12, 1.97 },
        { 'P', 265.4, 3.06, 6.68 }, { 'H', 3000.0, 5.00, 0.70 } };
    // the concert harp's soundboard
    inline constexpr Band kHarp[] = {
        { 'L', 88.8, -23.85, 0.70 }, { 'H', 1973.1, 9.09, 0.70 }, { 'P', 61.7, -23.55, 3.13 }, { 'P', 198.6, 19.52, 2.13 },
        { 'P', 129.7, -15.69, 4.72 }, { 'P', 460.5, -6.47, 8.00 }, { 'P', 1595.5, -7.18, 4.53 }, { 'P', 1135.8, 2.01, 6.75 },
        { 'P', 670.3, -4.82, 8.00 }, { 'H', 3500.0, -12.00, 0.70 } };

    enum Id { ViolinArco, CelloArco, Clarinet, PizzBright, PizzMellow, Harp };

    inline Voicing voicing (Id id)
    {
        switch (id)
        {
            case ViolinArco: return { kViolinArco, (int) std::size (kViolinArco), -6.6 };
            case CelloArco:  return { kCelloArco, (int) std::size (kCelloArco), -11.2 };
            case Clarinet:   return { kClarinet, (int) std::size (kClarinet), -8.6 };
            case PizzBright: return { kPizzBright, (int) std::size (kPizzBright), 8.5 };
            case PizzMellow: return { kPizzMellow, (int) std::size (kPizzMellow), -8.3 };
            case Harp:       return { kHarp, (int) std::size (kHarp), 2.6 };
        }
        return { kHarp, 0, 0.0 };
    }

    // linear interpolation in a table (keys ascending)
    inline double interp (const double* xs, const double* ys, int n, double x)
    {
        if (x <= xs[0]) return ys[0];
        for (int i = 1; i < n; ++i)
            if (x <= xs[i]) return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
        return ys[n - 1];
    }
}
