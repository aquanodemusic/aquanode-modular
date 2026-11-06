//==============================================================================
// Grand - the piano model as a concert grand, with what a grand adds:
//
//   VOICING   long bass strings and three-string unisons from F#2, the
//             inharmonicity and stretch measured on a recorded grand, longer
//             sustain than the uprights, a darker top, no dampers above E6
//   PHANTOMS  partials at the sums of partials (the tension swing of a hard
//             blow): the bass growls louder the harder you play
//   STRINGS   the other 87 strings ring along (their first partials, tuned
//             like the struck ones): with the sustain pedal down every one is
//             free; with it up only the top octave and a half (no dampers)
//             and keys held silently answer - the shimmer and bloom of a real
//             pedal. Strings sets how much.
//   MICS      a spaced pair over the strings: the bass reaches the left mic
//             first, the treble the right (up to 1.6 ms at Mics 100 %), and
//             the lid's reflection arrives a few ms later, differently on each
//             side - the width and air of a recorded grand. Lid sets the
//             reflection.
//
// Per-voice generator. Inputs: 0 = Pitch In, 1 = Env In, 2 = Add Midi In.
// Output: 0 = Audio Out.
//==============================================================================
#include "PianoModelCore.h"

using namespace aquanode;
using aquanode::piano::Voicing;

namespace
{
    // Clear: the first grand, brighter and more open (the inharmonicity and
    // stretch measured on its recordings, C2..C6)
    const Voicing& grandVoicing()
    {
        static const Voicing v
        {
            { 24.0, 36.0, 48.0, 60.0, 72.0, 84.0, 96.0 },                             // anchor keys

            { 1.6e-04, 4.7e-05, 0.00012, 0.00035, 0.00108, 0.0030, 0.0050 },          // B (measured C2..C6; the top as a recorded grand's)
            { -10.0, -1.0, -1.0, -0.5, 0.5, 4.0, 12.0 },                              // stretch, cents
            { 2.05, 1.62, 1.12, 0.64, 0.33, 0.165, 0.075 },                           // length, m
            { 0.055, 0.026, 0.011, 0.0062, 0.0045, 0.0039, 0.0035 },                  // density, kg/m
            { 0.122, 0.122, 0.122, 0.122, 0.117, 0.11, 0.095 },                       // strike point
            { 0.36, 0.48, 0.56, 0.72, 1.35, 2.5, 4.2 },                               // sigma0, 1/s
            0.6,                                                                      // sigmaF: high partials die first (fitted)
            23, 41,                                                                   // 1 / 2 strings up to
            { -0.22, 0.22 }, { -0.28, 0.04, 0.3 },                                    // unison detune, cents

            { 0.0108, 0.0098, 0.0091, 0.0086, 0.0079, 0.0072, 0.0066 },               // hammer mass, kg
            { 24.4, 22.8, 25.2, 23.4, 24.9, 24.0, 25.0 },                             // log K
            2.5, 0.4,                                                                 // Hertz p, Hunt-Crossley

            { 35.0, 70.0, 140.0, 300.0, 700.0, 1600.0, 4000.0, 1e+04 },               // radiation curve at, Hz
            { -54.0, -31.0, -36.5, -39.5, -36.0, -39.5, -48.5, -93.0 },               // radiation, dB
            { 9.5, 8.3, 6.7, 9.0, 5.8, 5.2, 13.5 },                                   // register, dB

            9.0, 30.0, 1500.0, 1.2,                                                   // Brightness

            { 52.0, 66.5, 78.0, 89.5, 104.0, 121.0, 139.0, 166.0, 196.0, 231.0 },     // soundboard modes, Hz
            { 0.6, 1.0, 0.95, 0.85, 0.75, 0.65, 0.5, 0.42, 0.35, 0.3 },
            1.3, 34.0, 0.16, 0.22,                                                    // body T60s, levels
            0.36, 420.0, 0.5, 0.06,                                                   // knock
            0.0, 6000.0, 0.015,                                                       // click

            10.5, 9.0, 0.04, 88,                                                      // damper, damped up to
            0.35,                                                                     // pan range
            1.2,                                                                      // output, dB
            0.07,                                                                     // phantom partials
            { 0, 0, 0, 0, 0, 0, 0 },                                                  // aftersound share: as the knob
            { 0, 0, 0, 0, 0, 0, 0 },                                                  // thump by register
            { 0, 0, 0, 0, 0, 0, 0 },                                                  // click by register
            0.0,                                                                      // click low-pass: none
            300.0,                                                                    // the give taken out above 300 Hz (no spike at a bass blow)
            1.0,                                                                      // the after-sound keeps the high loss
            { 21.0, 24.0, 30.0, 36.0, 42.0, 48.0, 93.0, 96.0, 108.0 },               // trim at these keys...
            { -1.5, -3.0, -3.0, -2.0, -0.5, 0.0, 0.0, -2.0, -7.0 },                   // ... dB: a quieter low bass, a softer top
            { 85.0, 89.0, 91.0, 97.0, 101.0, 104.0 },                                 // above C6 the partials over the first...
            { 0.0, -16.0, -28.0, -28.0, -14.0, -4.0 }                                 // ... quieter (dB per octave): a piano's top, not a bell
        };
        return v;
    }

    // Felt: voiced after a second grand, recorded note by note at two
    // strengths over all 88 keys. Its features were measured (inharmonicity,
    // stretch, partial levels, band spectra, register levels, decay rates) and
    // the tables fitted to them by rendering the model and comparing - no part
    // of the recordings is in here. A softer, rounder bass whose fundamental
    // sits under its second to fourth partials, a darker middle, a treble that
    // rings longer on a strong second partial with a woody tock and a warm
    // thump under it, and a quieter sympathetic bed. Its notes start the way
    // the recordings do: the bass swells in over the first milliseconds
    // instead of a thump at the blow
    const Voicing& grandVoicingB()
    {
        static const Voicing v
        {
            { 21.0, 30.0, 42.0, 57.0, 72.0, 87.0, 105.0 },                            // anchor keys
            { 1.5e-04, 7.1e-05, 5.7e-05, 0.000248, 0.00085, 0.00236, 0.0076 },         // B (measured; the top a little less stretched)
            { -17.0, -3.0, -2.0, -1.0, 1.5, 4.5, 27.0 },                              // stretch, cents (measured)
            { 2.1, 1.75, 1.3, 0.8, 0.38, 0.16, 0.06 },                                // length, m
            { 0.06, 0.03, 0.014, 0.0068, 0.0045, 0.0039, 0.0035 },                    // density, kg/m
            { 0.122, 0.122, 0.122, 0.122, 0.117, 0.105, 0.09 },                       // strike point
            { 0.50, 0.38, 0.91, 1.27, 1.35, 1.25, 1.8 },                              // sigma0, 1/s (fitted; the top as long as the recordings')
            0.6,                                                                      // sigmaF: high partials die first (fitted)
            23, 41,                                                                   // 1 / 2 strings up to
            { -0.22, 0.22 }, { -0.28, 0.04, 0.3 },                                    // unison detune, cents
            { 0.011, 0.0102, 0.0094, 0.0087, 0.00886, 0.00744, 0.00644 },             // hammer mass, kg (fitted)
            { 20.9, 19.25, 20.65, 24.2, 23.5, 27.1, 24.6 },                           // log K (fitted)
            2.5, 0.4,                                                                 // Hertz p, Hunt-Crossley
            { 35.0, 70.0, 140.0, 300.0, 700.0, 1600.0, 4000.0, 1e+04 },               // radiation curve at, Hz
            { -49.5, -25.9, -26.9, -33.2, -46.7, -47.8, -68.3, -82.6 },               // radiation, dB (fitted)
            { 3.5, 5.5, 1.0, -2.8, 7.6, 10.5, 28.8 },                                 // register, dB (fitted)
            9.0, 30.0, 1500.0, 1.2,                                                   // Brightness
            { 52.0, 66.5, 78.0, 89.5, 104.0, 121.0, 139.0, 166.0, 196.0, 231.0 },     // soundboard modes, Hz
            { 0.6, 1.0, 0.95, 0.85, 0.75, 0.65, 0.5, 0.42, 0.35, 0.3 },
            1.3, 34.0, 0.027, 0.22,                                                   // body T60s, levels
            0.25, 220.0, 0.5, 0.06,                                                   // knock (low: the bass stays dark)
            0.05, 1000.0, 0.08,                                                       // click (the treble's tock)
            10.5, 9.0, 0.04, 88,                                                      // damper, damped up to
            0.35,                                                                     // pan range
            2.7,                                                                      // output, dB (sits with Clear)
            0.07,                                                                     // phantom partials
            { 0.5, 0.5, 0.6, 0.8, 1.0, 1.2, 1.2 },                                    // aftersound share by register
            { 0.0, 0.0, 0.0, 1.8, 7.0, 15.6, 2.2 },                                   // thump by register, dB (fitted)
            { -60.0, -60.0, -50.0, -30.0, -19.4, 0.8, 6.7 },                          // click by register, dB (fitted)
            2500.0,                                                                   // click low-pass, Hz
            3000.0,                                                                   // the give taken out above (the bass starts without a thump)
            1.0,                                                                      // the after-sound keeps the high loss
            { 21.0, 23.0, 24.0, 30.0, 36.0, 40.0, 45.0, 93.0, 96.0, 108.0 },         // trim at these keys...
            { -3.0, -3.5, -6.0, -7.0, -4.0, -2.0, -0.5, 0.0, -2.0, -7.0 },           // ... dB: a quieter low bass, a softer top
            { 86.0, 90.0, 100.0, 108.0 },                                             // above C6 the partials over the first...
            { 0.0, -12.0, -22.0, -22.0 }                                              // ... quieter (dB per octave): a piano's top, not a bell
        };
        return v;
    }

    const Voicing& voicingForMode (int mode) { return mode == 1 ? grandVoicingB() : grandVoicing(); }

    // the voicing's tables between their anchors (as the model reads them)
    double interp (const Voicing& v, double x, const double (&t)[7])
    {
        const auto& a = v.anchors;
        if (x <= a[0]) return t[0];
        if (x >= a[6]) return t[6];
        int i = 0;
        while (x > a[i + 1]) ++i;
        return t[i] + (x - a[i]) / (a[i + 1] - a[i]) * (t[i + 1] - t[i]);
    }
}

class GrandPianoModule final : public PianoModel
{
public:
    static constexpr int pMics = pVoices + 1, pLid = pVoices + 2, pStrings = pVoices + 3, pMode = pVoices + 4;

    static ModuleDescriptor descriptor()
    {
        auto d = PianoModel::makeDescriptor ("osc.grand", "Grand",
            juce::String ("The piano model as a concert grand: long bass strings and three-string unisons, phantom partials that "
                          "make the bass growl on hard blows, the other strings ringing along (all of them with the sustain pedal down, "
                          "the undamped top a little), and a spaced pair of mics with the lid's reflection for its width. Voicing picks "
                          "one of two grands, each voiced after its own recordings: Felt (round and warm) or Clear (brighter and more "
                          "open). Mics sets the spacing, Lid the reflection, Strings the sympathetic ring.")
                + PianoModel::controlsDescription(), 16, 35.0f, 3);
        d.params.push_back (makeRotary ("mics", "Mics", 0.0f, 100.0f, 70.0f, 3, "%", false, 1.0f));        // pMics
        d.params.push_back (makeRotary ("lid", "Lid", 0.0f, 100.0f, 50.0f, 3, "%", false, 1.0f));          // pLid
        d.params.push_back (makeRotary ("strings", "Strings", 0.0f, 200.0f, 100.0f, 3, "%", false, 1.0f)); // pStrings
        d.params.push_back (makeCombo ("mode", "Voicing", { "Clear", "Felt" }, 1, 3, 2).noMod());            // pMode
        d.params.push_back (makeOutputTrim (-2.0f));   // brought down to sit with the other sources
        return d;
    }

    GrandPianoModule() : PianoModel (grandVoicing())
    {
        for (auto& n : voiceNote) n = -1;
    }

    // Clear / Felt: a new voicing from the next block (and its strings)
    void blockStart() override
    {
        PianoModel::blockStart();
        const int m = juce::jlimit (0, 1, (int) std::lround (getParameterBase (pMode)));
        if (m != mode)
        {
            mode = m;
            setVoicing (voicingForMode (m));
            if (sampleRate > 0.0) buildStrings();
        }
    }

    void prepare (double sr) override
    {
        PianoModel::prepare (sr);
        buildStrings();
        reset();
    }

    void reset() override
    {
        PianoModel::reset();
        for (auto& m : mic) m = MicState();
        for (auto& r : bedRe) r.fill (0.0f);
        for (auto& r : bedIm) r.fill (0.0f);
        keyEnergy.fill (0.0f);
        bedIn = bedOutL = bedOutR = 0.0f;
        bedEnergy = 0.0f;
        lastVoice = 1 << 30;
        firstOfSample = -1;
    }

    void voiceReset (int v) override
    {
        PianoModel::voiceReset (v);
        if (v >= 0 && v < kMaxVoices) { mic[(size_t) v] = MicState(); voiceNote[(size_t) v] = -1; }
    }

    void voiceNoteOn (int v, int note, bool retrigger) override
    {
        PianoModel::voiceNoteOn (v, note, retrigger);
        if (v >= 0 && v < kMaxVoices) voiceNote[(size_t) v] = juce::jlimit (0, 127, note);
    }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        // a new sample begins when the voices come round again (they run in order)
        if (v <= lastVoice)
        {
            runStrings();
            firstOfSample = v;
        }
        lastVoice = v;

        PianoModel::processVoiceSample (v, inputs, outputs);
        float l = outputs[0][0], r = outputs[0][1];
        bedIn += 0.5f * (l + r);

        // the mics
        if (v >= 0 && v < kMaxVoices)
        {
            auto& m = mic[(size_t) v];
            if (m.note != voiceNote[(size_t) v]) setMics (m, voiceNote[(size_t) v]);
            m.bufL[(size_t) m.pos] = l;
            m.bufR[(size_t) m.pos] = r;
            auto tap = [&m] (const std::array<float, kMicLen>& b, float d)
            {
                float rp = (float) m.pos - d;
                while (rp < 0.0f) rp += (float) kMicLen;
                const int i0 = (int) rp % kMicLen, i1 = (i0 + 1) % kMicLen;
                const float f = rp - std::floor (rp);
                return b[(size_t) i0] + f * (b[(size_t) i1] - b[(size_t) i0]);
            };
            const float dl = tap (m.bufL, m.dL), dr = tap (m.bufR, m.dR);
            // the lid: a later, darker copy, crossed over a little
            const float rl = tap (m.bufL, m.lidL) * 0.8f + tap (m.bufR, m.lidL) * 0.2f;
            const float rr = tap (m.bufR, m.lidR) * 0.8f + tap (m.bufL, m.lidR) * 0.2f;
            m.lpL += m.lidK * (rl - m.lpL);
            m.lpR += m.lidK * (rr - m.lpR);
            l = dl + m.lidGain * m.lpL;
            r = dr + m.lidGain * m.lpR;
            m.pos = (m.pos + 1) % kMicLen;
        }

        // the ringing strings come out with the first voice of the sample
        if (v == firstOfSample) { l += bedOutL; r += bedOutR; }
        outputs[0] = { l, r };
    }

private:
    //=== the mics ===============================================================
    static constexpr int kMicLen = 512;
    struct MicState
    {
        std::array<float, kMicLen> bufL {}, bufR {};
        int pos { 0 }, note { -2 };
        float dL { 0 }, dR { 0 }, lidL { 1 }, lidR { 1 }, lidGain { 0 }, lidK { 0.3f }, lpL { 0 }, lpR { 0 };
    };

    void setMics (MicState& m, int note)
    {
        m.note = note;
        const float x = juce::jlimit (-1.0f, 1.0f, ((float) note - 64.0f) / 40.0f);   // the string's place, left to right
        const float spacing = param (pMics) * 0.01f * 0.0016f * (float) sampleRate;    // up to 1.6 ms
        // the time between the mics, held under a fifth of the fundamental's
        // period up top, so a note never cancels itself when L and R are summed
        // (Felt under a tenth: its treble lives on the second partial too)
        const float f1 = 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);
        const float cap = (mode == 1 ? 0.1f : 0.2f) / f1 * (float) sampleRate;
        const float itd = juce::jlimit (-cap, cap, x * spacing);
        m.dL = 1.0f + 0.5f * (spacing + itd);
        m.dR = 1.0f + 0.5f * (spacing - itd);
        const float lid = param (pLid) * 0.01f;
        m.lidL = (0.0031f + 0.0004f * x) * (float) sampleRate;
        m.lidR = (0.0043f - 0.0004f * x) * (float) sampleRate;
        m.lidGain = 0.75f * lid;
        m.lidK = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5500.0f / (float) sampleRate);
    }

    //=== the sympathetic strings ===================================================
    static constexpr int kFirstKey = 21, kKeys = 88, kParts = 4;

    void buildStrings()
    {
        const auto& vc = getVoicing();
        for (int k = 0; k < kKeys; ++k)
        {
            const double key = kFirstKey + k;
            const double f1 = aquanode::midiNoteToHz (key) * std::pow (2.0, interp (vc, key, vc.stretchCents) / 1200.0);   // (MTS-ESP aware)
            const double B = interp (vc, key, vc.B);
            const double sig = interp (vc, key, vc.sigma0);
            int parts = 0;
            for (int p = 1; p <= kParts; ++p)
            {
                const double f = p * f1 * std::sqrt (1.0 + B * p * p);
                if (f > std::min (5000.0, 0.45 * sampleRate)) break;
                bedW[(size_t) k][(size_t) p - 1] = (float) (juce::MathConstants<double>::twoPi * f / sampleRate);
                bedSigFree[(size_t) k][(size_t) p - 1] = (float) ((sig + vc.sigmaF * (f / 2000.0) * (f / 2000.0)) * 1.3);
                bedGain[(size_t) k][(size_t) p - 1] = (float) (1.0 / p);
                ++parts;
            }
            bedParts[(size_t) k] = parts;
            const float x = juce::jlimit (-1.0f, 1.0f, (float) (key - 64.0) / 40.0f);
            const float a = (x * 0.7f + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            bedPanL[(size_t) k] = std::cos (a);
            bedPanR[(size_t) k] = std::sin (a);
        }
        lastPedal = -1;
        ctl = 0;
    }

    // which strings are free, how fast they lose their ring - every 64 samples
    void updateStrings()
    {
        const bool pedal = midiControls != nullptr && midiControls->sustain;
        std::array<bool, kKeys> sounding {};
        for (int v = 0; v < kMaxVoices; ++v)
            if (voiceNote[(size_t) v] >= kFirstKey && voiceNote[(size_t) v] < kFirstKey + kKeys)
                sounding[(size_t) (voiceNote[(size_t) v] - kFirstKey)] = true;
        const int top = getVoicing().damperTop;
        const bool changed = (int) pedal != lastPedal || sounding != lastSounding;
        if (! changed) return;
        lastPedal = (int) pedal;
        lastSounding = sounding;
        for (int k = 0; k < kKeys; ++k)
        {
            const bool free = pedal || (kFirstKey + k) > top;
            for (int p = 0; p < bedParts[(size_t) k]; ++p)
            {
                // a struck string is the voice itself; a damped one dies in a moment
                const float sig = sounding[(size_t) k] ? 400.0f : (free ? bedSigFree[(size_t) k][(size_t) p] : 30.0f);
                const float mag = std::exp (-sig / (float) sampleRate);
                bedMr[(size_t) k][(size_t) p] = mag * std::cos (bedW[(size_t) k][(size_t) p]);
                bedMi[(size_t) k][(size_t) p] = mag * std::sin (bedW[(size_t) k][(size_t) p]);
                // with the pedal up only the undamped top rings along, and only a
                // little (a high ring after every note otherwise)
                bedDrive[(size_t) k][(size_t) p] = (sounding[(size_t) k] || ! free) ? 0.0f : bedGain[(size_t) k][(size_t) p] * (pedal ? 1.0f : 0.25f);
            }
            keyFree[(size_t) k] = free && ! sounding[(size_t) k];
        }
    }

    // one sample of the strings, driven by the voices' last sample
    void runStrings()
    {
        const float amount = param (pStrings) * 0.01f;
        const float x = bedIn * 1.0e-5f * amount;
        bedIn = 0.0f;
        if (--ctl < 0) { ctl = 63; updateStrings(); }
        if (amount <= 0.0f || (std::abs (x) < 1.0e-9f && bedEnergy < 1.0e-12f))
        {
            bedOutL = bedOutR = 0.0f;
            return;
        }
        float l = 0.0f, r = 0.0f, e = 0.0f;
        for (int k = 0; k < kKeys; ++k)
        {
            // a damped string that has gone quiet costs nothing
            if (! keyFree[(size_t) k] && keyEnergy[(size_t) k] < 1.0e-14f) continue;
            float s = 0.0f, ke = 0.0f;
            for (int p = 0; p < bedParts[(size_t) k]; ++p)
            {
                const float zr = bedRe[(size_t) k][(size_t) p], zi = bedIm[(size_t) k][(size_t) p];
                const float mr = bedMr[(size_t) k][(size_t) p], mi = bedMi[(size_t) k][(size_t) p];
                const float nr = zr * mr - zi * mi + x * bedDrive[(size_t) k][(size_t) p];
                const float ni = zr * mi + zi * mr;
                bedRe[(size_t) k][(size_t) p] = nr;
                bedIm[(size_t) k][(size_t) p] = ni;
                s += ni;
                ke += nr * nr + ni * ni;
            }
            keyEnergy[(size_t) k] = ke;
            e += ke;
            l += s * bedPanL[(size_t) k];
            r += s * bedPanR[(size_t) k];
        }
        bedEnergy = e;
        bedOutL = std::tanh (l * 0.6f);
        bedOutR = std::tanh (r * 0.6f);
    }

    std::array<MicState, kMaxVoices> mic {};
    std::array<int, kMaxVoices> voiceNote {};

    std::array<std::array<float, kParts>, kKeys> bedRe {}, bedIm {}, bedMr {}, bedMi {}, bedW {}, bedSigFree {}, bedGain {}, bedDrive {};
    std::array<int, kKeys> bedParts {};
    std::array<float, kKeys> bedPanL {}, bedPanR {};
    std::array<bool, kKeys> lastSounding {}, keyFree {};
    std::array<float, kKeys> keyEnergy {};
    float bedIn { 0 }, bedOutL { 0 }, bedOutR { 0 }, bedEnergy { 0 };
    int lastVoice { 1 << 30 }, firstOfSample { -1 }, lastPedal { -1 }, ctl { 0 };
    int mode { 0 };
};

AQUANODE_REGISTER_MODULE (GrandPianoModule, GrandPianoModule::descriptor)
