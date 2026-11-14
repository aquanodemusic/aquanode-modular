#pragma once

// The pieces the filters' static curves are made of (SynthModule::
// magnitudeResponseDb): each filter writes its response from the same maths
// it runs, evaluated on the unit circle - exact, not measured.
//
//   TPT state-variable stages (Zavalishin / Simper) are the bilinear
//   transform of the analog SVF prewarped at the cutoff: at a frequency f
//   their s is j tan(pi f / fs) / tan(pi fc / fs), and low, band and high
//   are 1, s and s^2 over s^2 + k s + 1.
//   Everything else is written in z^-1 = e^(-j 2 pi f / fs).

#include <complex>
#include <JuceHeader.h>

namespace aquanode::response
{
    using C = std::complex<double>;

    inline double pi() { return juce::MathConstants<double>::pi; }

    // z^-1 at a frequency
    inline C zInv (double hz, double sr) { return std::polar (1.0, -2.0 * pi() * hz / sr); }
    inline C zPow (double hz, double sr, double m) { return std::polar (1.0, -2.0 * pi() * hz * m / sr); }

    // the bilinear s of a TPT stage tuned to fc (frequencies above Nyquist
    // are held just under it)
    inline C sOf (double hz, double fc, double sr)
    {
        const double h = juce::jlimit (0.0, sr * 0.4999, hz);
        return { 0.0, std::tan (pi() * h / sr) / std::tan (pi() * fc / sr) };
    }

    struct Svf { C low, band, high; };
    inline Svf svf (C s, double k)
    {
        const C d = s * s + k * s + 1.0;
        return { 1.0 / d, s / d, s * s / d };
    }

    // y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2
    inline C biquad (double b0, double b1, double b2, double a1, double a2, C z1)
    {
        const C z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    // the one-pole "s += g (x - s)"
    inline C onePole (double g, C z1) { return g / (1.0 - (1.0 - g) * z1); }

    inline float db (C h) { return (float) (20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h)))); }
}
