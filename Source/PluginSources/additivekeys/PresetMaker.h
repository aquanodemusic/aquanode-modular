#pragma once
// PresetMaker.h - Additive Keys' preset maker: new sounds in twelve kinds, and
// the twenty factory sounds (fixed rolls of the same recipes). Every sound it
// makes is brought to the same loudness afterwards (see matchLoudness in
// PluginProcessor.cpp). Included at the end of PluginProcessor.cpp.

namespace akmaker
{
    inline juce::StringArray kinds()
    {
        return { "Bell", "Organ", "Pad", "Strings", "Brass", "Choir",
                 "Bass", "Pluck", "E-Piano", "Lead", "Glass", "Sweep" };
    }

    struct Factory { const char* name; int kind; int variant; int seed; };

    inline const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> f {
            { "Glass Bells",     0, 0, 11 }, { "Temple Bell",    0, 1, 23 },
            { "Drawbar Organ",   1, 0, 5 },  { "Perc Organ",     1, 1, 8 },
            { "Warm Pad",        2, 0, 3 },  { "Evolving Pad",   2, 1, 17 },
            { "Silk Strings",    3, 0, 9 },  { "Chamber Strings",3, 1, 4 },
            { "Soft Brass",      4, 0, 6 },  { "Brass Section",  4, 1, 12 },
            { "Choir Aah",       5, 0, 2 },  { "Choir Ooh",      5, 1, 7 },
            { "Round Bass",      6, 0, 14 }, { "Growl Bass",     6, 1, 21 },
            { "Harp Pluck",      7, 0, 10 }, { "Tine Piano",     8, 0, 13 },
            { "Mellow Keys",     8, 1, 19 }, { "Square Lead",    9, 0, 15 },
            { "Crystal",        10, 0, 16 }, { "Spectral Bloom",11, 0, 18 },
        };
        return f;
    }

    //==========================================================================
    struct Maker
    {
        K5AudioProcessor& p;
        juce::Random& r;

        void set (const juce::String& id, float v)
        {
            if (auto* prm = p.apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        }
        float rnd (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); }
        bool chance (float c) { return r.nextFloat() < c; }

        void src (int s, const juce::String& name, float v) { set ("s" + juce::String (s + 1) + "_" + name, v); }
        void amp (int s, float a, float d, float su, float re)
        { src (s, "ampA", a); src (s, "ampD", d); src (s, "ampS", su); src (s, "ampR", re); }
        void filt (int s, float cutoff, float res, float envAmt, float a, float d, float su, float re, bool steep = false)
        {
            src (s, "cutoff", cutoff); src (s, "resonance", res); src (s, "filtEnvAmount", envAmt);
            src (s, "filtA", a); src (s, "filtD", d); src (s, "filtS", su); src (s, "filtR", re);
            src (s, "slope24", steep ? 1.0f : 0.0f);
        }
        void bus (int s, int g, float a, float d, float su, float re)
        {
            const auto gp = "grp" + juce::String (g) + "_";
            src (s, gp + "A", a); src (s, gp + "D", d); src (s, gp + "S", su); src (s, gp + "R", re);
        }
        void allBuses (int s, float a, float d, float su, float re)
        { for (int g = 0; g < 4; ++g) bus (s, g, a, d, su, re); }
        void formant (int b, float freq, float gain, float q)
        {
            const auto bp = "dftBand" + juce::String (b) + "_";
            set (bp + "freq", freq); set (bp + "gain", gain); set (bp + "q", q);
        }
        void lfo (int shape, float rate, float delay, float vib, float trem, float filt)
        {
            set ("lfoShape", (float) shape); set ("lfoRate", rate); set ("lfoDelay", delay);
            set ("lfoVibrato", vib); set ("lfoTremolo", trem); set ("lfoFilterMod", filt);
        }

        // harmonic spectra: f(n) for n = 1..63, and the bus each range follows
        template <typename F>
        void harmonics (int s, F level, int lowTop = 3, int midTop = 8, int highTop = 20)
        {
            for (int h = 0; h < 63; ++h)
            {
                const int n = h + 1;
                const int g = n <= lowTop ? 0 : n <= midTop ? 1 : n <= highTop ? 2 : 3;
                p.setHarmonic (s, h, juce::jlimit (0.0f, 1.0f, level (n)), g);
            }
        }
        // a few-percent wobble on every harmonic, so two rolls of a kind differ
        float wobble (float v) { return v * rnd (0.8f, 1.15f); }

        void vowel (int which, float gain, float q)
        {
            static const float f[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                           { 570, 840, 2410 }, { 300, 870, 2240 } };
            which = juce::jlimit (0, 4, which);
            formant (2, f[which][0], gain, q);
            formant (5, f[which][1], gain * 0.8f, q);
            formant (8, f[which][2], gain * 0.55f, q);
        }

        //======================================================================
        void make (int kind, int variant)
        {
            p.presetManager.resetToDefaults();
            set ("s2_level", 0.0f);

            switch (kind)
            {
                case 0: // Bell
                {
                    const float decay = variant == 1 ? rnd (4.0f, 6.0f) : rnd (2.0f, 3.5f);
                    const bool sparseOdd = chance (0.5f);
                    harmonics (0, [&] (int n)
                    {
                        if (n == 1) return 1.0f;
                        if (sparseOdd && n % 2 == 0) return 0.0f;
                        if (n > 30) return 0.0f;
                        return chance (0.45f) ? wobble (0.9f / std::sqrt ((float) n)) : 0.0f;
                    }, 1, 4, 12);
                    bus (0, 0, 0.001f, decay, 0.0f, decay * 0.6f);
                    bus (0, 1, 0.001f, decay * 0.5f, 0.0f, decay * 0.4f);
                    bus (0, 2, 0.001f, decay * 0.22f, 0.0f, 0.4f);
                    bus (0, 3, 0.001f, decay * 0.08f, 0.0f, 0.2f);
                    amp (0, 0.002f, decay, 0.0f, decay * 0.6f);
                    filt (0, 14000.0f, 0.1f, 0.0f, 0.01f, 0.2f, 0.3f, 0.2f);
                    // a quieter, detuned partner an octave of harmonics up (2, 4, 6 ...)
                    src (1, "level", rnd (0.3f, 0.5f));
                    src (1, "detune", rnd (3.0f, 9.0f) * (chance (0.5f) ? 1.0f : -1.0f));
                    harmonics (1, [&] (int n) { return (n % 2 == 0 && n <= 24) ? wobble (0.7f / (float) n) : 0.0f; }, 2, 6, 14);
                    bus (1, 0, 0.001f, decay * 0.8f, 0.0f, decay * 0.5f);
                    bus (1, 1, 0.001f, decay * 0.4f, 0.0f, 0.5f);
                    bus (1, 2, 0.001f, decay * 0.15f, 0.0f, 0.3f);
                    bus (1, 3, 0.001f, 0.1f, 0.0f, 0.2f);
                    amp (1, 0.002f, decay * 0.8f, 0.0f, decay * 0.5f);
                    filt (1, 14000.0f, 0.1f, 0.0f, 0.01f, 0.2f, 0.3f, 0.2f);
                    if (variant == 1) lfo (0, rnd (3.0f, 5.0f), 0.2f, 0.0f, rnd (0.05f, 0.15f), 0.0f);
                    break;
                }
                case 1: // Organ
                {
                    static const int bars[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
                    float lv[64] = {};
                    for (int b : bars) lv[b] = chance (0.75f) ? rnd (0.3f, 1.0f) : 0.0f;
                    lv[1] = juce::jmax (lv[1], 0.7f);
                    lv[2] = juce::jmax (lv[2], 0.5f);
                    harmonics (0, [&] (int n) { return n < 64 ? lv[n] : 0.0f; }, 63, 63, 63);   // all on bus 1
                    bus (0, 0, 0.004f, 0.1f, 1.0f, 0.06f);
                    amp (0, 0.004f, 0.1f, 1.0f, 0.07f);
                    filt (0, 12000.0f, 0.05f, 0.0f, 0.01f, 0.2f, 0.3f, 0.2f);
                    if (variant == 1)   // percussion: the 3rd (or 4th) harmonic, decaying, on bus 2
                    {
                        const int perc = chance (0.5f) ? 3 : 4;
                        for (int h = 0; h < 63; ++h) if (h + 1 == perc) p.setHarmonic (0, h, 1.0f, 1);
                        bus (0, 1, 0.002f, rnd (0.2f, 0.4f), 0.0f, 0.05f);
                    }
                    lfo (0, rnd (5.5f, 6.8f), 0.0f, rnd (0.03f, 0.08f), rnd (0.1f, 0.25f), 0.0f);
                    break;
                }
                case 2: // Pad
                {
                    const float tilt = rnd (-0.4f, -0.1f);
                    harmonics (0, [&] (int n) { return wobble (1.0f / (float) n); });
                    src (0, "harmTilt", tilt);
                    const float att = variant == 1 ? rnd (1.5f, 3.0f) : rnd (0.6f, 1.4f);
                    bus (0, 0, att * 0.6f, 1.0f, 1.0f, 2.5f);
                    bus (0, 1, att, 1.5f, 0.85f, 2.5f);
                    bus (0, 2, att * (variant == 1 ? 2.2f : 1.4f), 2.0f, 0.7f, 2.5f);
                    bus (0, 3, att * (variant == 1 ? 3.0f : 1.8f), 2.5f, 0.5f, 2.0f);
                    amp (0, att, 1.0f, 0.9f, rnd (2.0f, 4.0f));
                    filt (0, rnd (2500.0f, 6000.0f), 0.1f, 0.0f, 0.5f, 1.0f, 0.5f, 1.0f);
                    src (1, "level", rnd (0.6f, 0.8f));
                    src (1, "detune", rnd (7.0f, 13.0f));
                    src (0, "detune", -rnd (4.0f, 8.0f));
                    harmonics (1, [&] (int n) { return wobble (1.0f / (float) n); });
                    src (1, "harmTilt", tilt - 0.1f);
                    for (int g = 0; g < 4; ++g) bus (1, g, att * (1.0f + 0.5f * (float) g), 1.5f, 0.85f - 0.1f * (float) g, 2.5f);
                    amp (1, att * 1.2f, 1.0f, 0.9f, rnd (2.0f, 4.0f));
                    filt (1, rnd (2500.0f, 6000.0f), 0.1f, 0.0f, 0.5f, 1.0f, 0.5f, 1.0f);
                    lfo (0, rnd (0.12f, 0.4f), 0.0f, 0.0f, 0.0f, rnd (0.3f, variant == 1 ? 1.2f : 0.6f));
                    formant (3, rnd (600.0f, 1200.0f), rnd (0.1f, 0.25f), 0.5f);
                    break;
                }
                case 3: // Strings
                {
                    harmonics (0, [&] (int n) { return wobble (1.0f / (float) n); });
                    harmonics (1, [&] (int n) { return wobble (1.0f / (float) n); });
                    const float att = variant == 1 ? rnd (0.12f, 0.25f) : rnd (0.3f, 0.6f);
                    for (int s = 0; s < 2; ++s)
                    {
                        bus (s, 0, att * 0.8f, 0.5f, 1.0f, 0.8f);
                        bus (s, 1, att, 0.6f, 0.9f, 0.8f);
                        bus (s, 2, att * 1.3f, 0.8f, 0.8f, 0.7f);
                        bus (s, 3, att * 1.6f, 1.0f, 0.6f, 0.6f);
                        amp (s, att, 0.6f, 0.9f, rnd (0.6f, 1.2f));
                        filt (s, rnd (5000.0f, 8000.0f), 0.1f, 0.0f, 0.1f, 0.5f, 0.5f, 0.5f);
                    }
                    src (1, "level", rnd (0.7f, 0.85f));
                    src (1, "detune", rnd (6.0f, 9.0f));
                    src (0, "detune", -rnd (3.0f, 6.0f));
                    formant (1, 400.0f, rnd (0.2f, 0.35f), 0.6f);
                    formant (4, 1200.0f, rnd (0.15f, 0.3f), 0.6f);
                    formant (8, 3000.0f, rnd (0.1f, 0.25f), 0.55f);
                    lfo (0, rnd (5.0f, 6.0f), rnd (0.3f, 0.6f), rnd (0.08f, 0.18f), 0.0f, 0.0f);
                    break;
                }
                case 4: // Brass
                {
                    harmonics (0, [&] (int n) { return wobble (1.0f / (float) n); });
                    allBuses (0, 0.02f, 0.4f, 1.0f, 0.3f);
                    amp (0, rnd (0.03f, 0.08f), 0.3f, 0.85f, rnd (0.2f, 0.35f));
                    filt (0, rnd (700.0f, 1500.0f), rnd (0.1f, 0.25f), rnd (0.45f, 0.75f),
                          rnd (0.04f, 0.1f), rnd (0.3f, 0.6f), rnd (0.3f, 0.5f), 0.3f, chance (0.5f));
                    src (0, "pitchDepth", rnd (0.3f, 0.8f));
                    src (0, "pitchA", 0.001f); src (0, "pitchD", rnd (0.04f, 0.09f));
                    src (0, "pitchS", 0.0f);   src (0, "pitchR", 0.05f);
                    formant (4, rnd (1000.0f, 1400.0f), rnd (0.2f, 0.4f), 0.6f);
                    if (variant == 1)
                    {
                        src (1, "level", 0.75f);
                        src (1, "detune", rnd (8.0f, 14.0f));
                        harmonics (1, [&] (int n) { return wobble (1.0f / (float) n); });
                        allBuses (1, 0.02f, 0.4f, 1.0f, 0.3f);
                        amp (1, rnd (0.05f, 0.1f), 0.3f, 0.85f, 0.3f);
                        filt (1, rnd (800.0f, 1600.0f), 0.2f, rnd (0.45f, 0.7f), 0.07f, 0.5f, 0.4f, 0.3f);
                    }
                    lfo (0, 5.2f, 0.5f, rnd (0.05f, 0.12f), 0.0f, 0.0f);
                    break;
                }
                case 5: // Choir
                {
                    const int v = variant == 1 ? (chance (0.5f) ? 3 : 4) : (chance (0.7f) ? 0 : 1);
                    for (int s = 0; s < 2; ++s)
                    {
                        harmonics (s, [&] (int n) { return wobble (std::pow ((float) n, -1.2f)); });
                        allBuses (s, 0.3f, 0.6f, 0.95f, 1.0f);
                        amp (s, rnd (0.35f, 0.8f), 0.6f, 0.9f, rnd (1.0f, 1.6f));
                        filt (s, rnd (4000.0f, 7000.0f), 0.05f, 0.0f, 0.2f, 0.5f, 0.5f, 0.5f);
                    }
                    src (1, "level", 0.8f);
                    src (1, "detune", rnd (9.0f, 14.0f));
                    src (0, "detune", -rnd (4.0f, 7.0f));
                    vowel (v, rnd (0.6f, 0.9f), rnd (0.7f, 0.85f));
                    lfo (0, rnd (4.6f, 5.4f), rnd (0.4f, 0.8f), rnd (0.1f, 0.2f), 0.0f, 0.0f);
                    break;
                }
                case 6: // Bass
                {
                    const bool square = variant == 1 || chance (0.3f);
                    harmonics (0, [&] (int n) { return (square && n % 2 == 0) ? 0.0f : wobble (1.0f / (float) n); }, 2, 6, 14);
                    bus (0, 0, 0.002f, 0.5f, 1.0f, 0.1f);
                    bus (0, 1, 0.002f, rnd (0.2f, 0.4f), 0.4f, 0.1f);
                    bus (0, 2, 0.002f, rnd (0.08f, 0.2f), 0.1f, 0.08f);
                    bus (0, 3, 0.002f, 0.06f, 0.0f, 0.05f);
                    amp (0, 0.003f, 0.6f, 0.75f, rnd (0.08f, 0.18f));
                    filt (0, rnd (300.0f, 700.0f), rnd (0.2f, variant == 1 ? 0.6f : 0.35f), rnd (0.35f, 0.7f),
                          0.002f, rnd (0.15f, 0.35f), 0.2f, 0.1f, variant == 1);
                    // a pure sine underneath for weight
                    src (1, "level", rnd (0.4f, 0.6f));
                    harmonics (1, [] (int n) { return n == 1 ? 1.0f : 0.0f; });
                    allBuses (1, 0.003f, 0.5f, 1.0f, 0.1f);
                    amp (1, 0.003f, 0.6f, 0.8f, 0.12f);
                    if (variant == 1) lfo (2, rnd (3.0f, 7.0f), 0.0f, 0.0f, 0.0f, rnd (0.4f, 1.0f));
                    break;
                }
                case 7: // Pluck
                {
                    harmonics (0, [&] (int n) { return wobble (1.0f / (float) n); });
                    const float d = rnd (1.0f, 2.2f);
                    bus (0, 0, 0.001f, d, 0.0f, 0.4f);
                    bus (0, 1, 0.001f, d * 0.45f, 0.0f, 0.3f);
                    bus (0, 2, 0.001f, d * 0.18f, 0.0f, 0.2f);
                    bus (0, 3, 0.001f, d * 0.07f, 0.0f, 0.1f);
                    amp (0, 0.001f, d, 0.0f, rnd (0.3f, 0.6f));
                    filt (0, rnd (3000.0f, 8000.0f), 0.1f, rnd (0.1f, 0.3f), 0.001f, 0.3f, 0.0f, 0.2f);
                    src (1, "level", rnd (0.4f, 0.6f));
                    src (1, "detune", rnd (3.0f, 7.0f));
                    harmonics (1, [&] (int n) { return n % 2 == 1 ? wobble (1.0f / (float) n) : 0.0f; });
                    bus (1, 0, 0.001f, d * 0.8f, 0.0f, 0.3f);
                    bus (1, 1, 0.001f, d * 0.3f, 0.0f, 0.2f);
                    bus (1, 2, 0.001f, d * 0.1f, 0.0f, 0.1f);
                    bus (1, 3, 0.001f, 0.05f, 0.0f, 0.1f);
                    amp (1, 0.001f, d * 0.8f, 0.0f, 0.4f);
                    break;
                }
                case 8: // E-Piano
                {
                    const int tine = variant == 1 ? 7 : (chance (0.5f) ? 14 : 8);
                    harmonics (0, [&] (int n)
                    {
                        if (n == 1) return 1.0f;
                        if (n == 2) return wobble (0.35f);
                        if (n == 3) return wobble (0.12f);
                        if (n == 4) return wobble (0.06f);
                        if (n == tine || n == tine + 1) return wobble (variant == 1 ? 0.12f : 0.3f);
                        return 0.0f;
                    }, 4, 4, 30);
                    const float d = rnd (2.5f, 4.0f);
                    bus (0, 0, 0.002f, d, 0.0f, 0.5f);
                    bus (0, 2, 0.001f, rnd (0.2f, 0.4f), 0.0f, 0.2f);
                    amp (0, 0.002f, d, 0.0f, rnd (0.4f, 0.6f));
                    filt (0, 9000.0f, 0.05f, 0.0f, 0.01f, 0.2f, 0.3f, 0.2f);
                    src (1, "level", 0.5f);
                    src (1, "detune", rnd (1.5f, 4.0f));
                    harmonics (1, [&] (int n) { return n == 1 ? 1.0f : n == 2 ? 0.25f : 0.0f; });
                    bus (1, 0, 0.002f, d, 0.0f, 0.5f);
                    amp (1, 0.002f, d, 0.0f, 0.5f);
                    lfo (0, rnd (4.0f, 5.5f), 0.0f, 0.0f, rnd (0.15f, 0.35f), 0.0f);
                    break;
                }
                case 9: // Lead
                {
                    const bool square = variant == 0 ? chance (0.6f) : chance (0.3f);
                    harmonics (0, [&] (int n) { return (square && n % 2 == 0) ? 0.0f : wobble (1.0f / (float) n); });
                    allBuses (0, 0.01f, 0.3f, 1.0f, 0.2f);
                    amp (0, 0.01f, 0.3f, 1.0f, rnd (0.15f, 0.3f));
                    filt (0, rnd (2500.0f, 5000.0f), rnd (0.25f, 0.45f), rnd (0.15f, 0.4f), 0.005f, 0.3f, 0.4f, 0.2f);
                    src (1, "level", rnd (0.5f, 0.7f));
                    src (1, "detune", rnd (6.0f, 10.0f));
                    harmonics (1, [&] (int n) { return wobble (1.0f / (float) n); });
                    allBuses (1, 0.01f, 0.3f, 1.0f, 0.2f);
                    amp (1, 0.01f, 0.3f, 1.0f, 0.25f);
                    filt (1, rnd (2500.0f, 5000.0f), 0.3f, 0.2f, 0.005f, 0.3f, 0.4f, 0.2f);
                    lfo (0, rnd (5.4f, 6.2f), rnd (0.25f, 0.45f), rnd (0.15f, 0.3f), 0.0f, 0.0f);
                    break;
                }
                case 10: // Glass
                {
                    src (0, "harmMode", chance (0.5f) ? 3.0f : 4.0f);     // Octave / Fifth
                    src (0, "harmTilt", rnd (0.25f, 0.55f));
                    harmonics (0, [&] (int n) { return wobble (1.0f / (float) n); });
                    bus (0, 0, 0.005f, 2.5f, 0.5f, 3.0f);
                    bus (0, 1, 0.01f, 1.8f, 0.4f, 2.5f);
                    bus (0, 2, 0.02f, 1.2f, 0.3f, 2.0f);
                    bus (0, 3, 0.02f, 0.8f, 0.2f, 1.5f);
                    amp (0, rnd (0.005f, 0.2f), 2.0f, 0.45f, rnd (2.5f, 4.0f));
                    filt (0, 15000.0f, 0.1f, 0.0f, 0.01f, 0.2f, 0.3f, 0.2f);
                    src (1, "level", rnd (0.4f, 0.6f));
                    src (1, "detune", rnd (11.0f, 18.0f));
                    src (1, "harmMode", 3.0f);
                    harmonics (1, [&] (int n) { return wobble (1.0f / std::sqrt ((float) n)); });
                    allBuses (1, 0.05f, 2.0f, 0.4f, 3.0f);
                    amp (1, 0.1f, 2.0f, 0.4f, 3.0f);
                    formant (9, rnd (3500.0f, 5000.0f), rnd (0.2f, 0.35f), 0.6f);
                    lfo (0, rnd (2.0f, 4.0f), 0.0f, 0.0f, rnd (0.05f, 0.15f), 0.0f);
                    break;
                }
                case 11: default: // Sweep
                {
                    harmonics (0, [&] (int n) { return wobble (1.0f / (float) n); });
                    const float t = rnd (0.6f, 1.2f);
                    bus (0, 0, 0.01f, 1.0f, 1.0f, 2.0f);
                    bus (0, 1, t, 1.0f, 1.0f, 2.0f);
                    bus (0, 2, t * 2.0f, 1.0f, 1.0f, 2.0f);
                    bus (0, 3, t * 3.0f, 1.0f, 1.0f, 2.0f);
                    amp (0, 0.3f, 1.0f, 1.0f, rnd (2.0f, 3.0f));
                    filt (0, rnd (1500.0f, 4000.0f), rnd (0.4f, 0.6f), 0.0f, 0.5f, 1.0f, 0.5f, 1.0f, true);
                    src (1, "level", rnd (0.5f, 0.7f));
                    src (1, "detune", rnd (8.0f, 15.0f));
                    harmonics (1, [&] (int n) { return n % 2 == 1 ? wobble (1.0f / (float) n) : 0.0f; });
                    for (int g = 0; g < 4; ++g) bus (1, 3 - g, t * (float) g + 0.01f, 1.0f, 1.0f, 2.0f);
                    amp (1, 0.3f, 1.0f, 1.0f, 2.5f);
                    filt (1, rnd (1500.0f, 4000.0f), 0.4f, 0.0f, 0.5f, 1.0f, 0.5f, 1.0f);
                    lfo (r.nextInt (2), rnd (0.08f, 0.25f), 0.0f, 0.0f, 0.0f, rnd (1.0f, 2.0f));
                    break;
                }
            }
        }
    };
}
