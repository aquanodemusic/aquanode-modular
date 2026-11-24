#pragma once
// (6.9.15) A colour picked as light: a knob in nanometres. 380 - 700 nm are the
// spectral colours (violet, blue, cyan, green, yellow, orange, red, at full
// brightness); past 700 the knob goes on into the purples - the magentas and
// pinks no single wavelength makes (the eye mixes them from red and violet),
// from red back round to violet. The periodogram's and spectrogram's A, B and
// C/Delta colours use it; at their own defaults they keep the colours they had.
#include "../../Core/ModuleCore.h"

namespace aquanode::light
{
    constexpr float kMinNm = 380.0f, kSpectralMaxNm = 700.0f, kMaxNm = 780.0f;

    inline juce::Colour colourOf (float v)
    {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (v > kSpectralMaxNm)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (v - kSpectralMaxNm) / (kMaxNm - kSpectralMaxNm));
            r = 1.0f - 0.55f * t; g = 0.0f; b = t;   // red through magenta to violet
        }
        else if (v < 440.0f) { r = (440.0f - v) / 60.0f * 0.6f; b = 1.0f; }
        else if (v < 490.0f) { g = (v - 440.0f) / 50.0f; b = 1.0f; }
        else if (v < 510.0f) { g = 1.0f; b = (510.0f - v) / 20.0f; }
        else if (v < 580.0f) { r = (v - 510.0f) / 70.0f; g = 1.0f; }
        else if (v < 645.0f) { r = 1.0f; g = (645.0f - v) / 65.0f; }
        else                 { r = 1.0f; }
        // a little lighter, so the deep blues and reds still read on the dark plots
        auto mix = [] (float c) { return juce::jlimit (0.0f, 1.0f, 0.22f + 0.78f * c); };
        return juce::Colour::fromFloatRGBA (mix (r), mix (g), mix (b), 1.0f);
    }

    inline juce::String textOf (double v)
    {
        if (v > kSpectralMaxNm)
            return "Purple " + juce::String (juce::roundToInt ((v - kSpectralMaxNm) / (kMaxNm - kSpectralMaxNm) * 100.0)) + "%";
        return juce::String (juce::roundToInt (v)) + " nm";
    }

    // the knob: its default, and the exact colour it gives there (the colour it always had)
    inline juce::Colour colourAt (float v, float defaultNm, juce::Colour original)
    {
        return std::abs (v - defaultNm) < 0.5f ? original : colourOf (v);
    }
}
