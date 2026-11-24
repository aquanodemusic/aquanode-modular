// Modal Body - any sound played into a resonating object: a bank of up to 24
// tuned modes whose frequency ratios are those of a bar, a plate, a drum skin,
// a glass, an open or a closed tube, or a wooden box. Material sets how much
// faster the high modes die (metal rings evenly, wood thuds), Bright how much
// of the input reaches the upper modes, Stretch bends the ratios. With Track
// on MIDI, notes on Midi In retune the body. Breath, plucks and noise through
// it turn into chimes, bowls and kalimbas.
// Global. Inputs: 0 = Audio In, 1 = Midi In. Output: 0 = Audio Out.
#include "../../Core/ModuleCore.h"
#include "../../Shared/FxKit.h"

using namespace aquanode;

namespace modalbody
{
    constexpr int kModes = 24;

    inline const juce::StringArray& shapeNames()
    {
        static const juce::StringArray n { "Bar", "Plate", "Membrane", "Glass", "Open Tube", "Closed Tube", "Wood Box" };
        return n;
    }

    // the shape's mode ratios, lowest first (the first is 1)
    inline std::array<float, kModes> ratios (int shape)
    {
        std::array<float, kModes> r {};
        switch (shape)
        {
            case 0:   // a free bar (Euler-Bernoulli beam)
            {
                const float b1 = 1.50562f;
                for (int n = 0; n < kModes; ++n)
                {
                    const float bn = n == 0 ? b1 : (n == 1 ? 2.49975f : (float) n + 0.5f);
                    r[(size_t) n] = (bn * bn) / (b1 * b1);
                }
                break;
            }
            case 1:   // a rectangular plate (sides 1 : 1.3), the lowest 24 of its (m, n) modes
            {
                std::vector<float> all;
                for (int m = 1; m <= 8; ++m)
                    for (int n = 1; n <= 8; ++n)
                        all.push_back ((float) (m * m) + (float) (n * n) / 1.69f);
                std::sort (all.begin(), all.end());
                for (int i = 0; i < kModes; ++i) r[(size_t) i] = all[(size_t) i] / all[0];
                break;
            }
            case 2:   // a circular membrane (Bessel function zeros)
            {
                static const float z[kModes] = { 1.0f, 1.593f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f,
                                                 3.600f, 3.652f, 4.060f, 4.154f, 4.230f, 4.601f, 4.832f, 4.903f,
                                                 5.131f, 5.412f, 5.552f, 5.649f, 5.936f, 6.120f, 6.177f, 6.208f };
                for (int i = 0; i < kModes; ++i) r[(size_t) i] = z[i];
                break;
            }
            case 3:   // a glass / bowl: ring modes n(n^2 - 1) / sqrt(n^2 + 1), each a slightly split pair
            {
                auto f = [] (float n) { return n * (n * n - 1.0f) / std::sqrt (n * n + 1.0f); };
                const float f2 = f (2.0f);
                for (int i = 0; i < kModes; ++i)
                {
                    const float n = 2.0f + (float) (i / 2);
                    r[(size_t) i] = f (n) / f2 * (i % 2 == 0 ? 1.0f : 1.0045f);
                }
                break;
            }
            case 4:   // an open tube: every harmonic
                for (int i = 0; i < kModes; ++i) r[(size_t) i] = (float) (i + 1);
                break;
            case 5:   // a closed tube: the odd harmonics
                for (int i = 0; i < kModes; ++i) r[(size_t) i] = (float) (2 * i + 1);
                break;
            default:  // a wooden box: dense, uneven low modes thinning out upward
            {
                fxkit::Rng rng (4242);
                float x = 1.0f;
                for (int i = 0; i < kModes; ++i)
                {
                    r[(size_t) i] = x;
                    x *= 1.18f + 0.22f * rng.uni() + 0.02f * (float) i;
                }
                break;
            }
        }
        return r;
    }
}

class ModalBodyModule : public SynthModule
{
public:
    enum ParamIndex { pShape = 0, pPitch, pDecay, pMaterial, pBright, pStretch, pSpread, pInput, pTrack, pDryWet };

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        pitchSmooth.prepare (sr / 32.0, 0.02f);
        pitchSmooth.snap (-1.0f);
        reset();
    }

    void reset() override
    {
        for (auto& ch : state) for (auto& m : ch) m = {};
        countdown = 0;
    }

    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override
    {
        if (m.isNoteOn())
            trackedHz.store ((float) midiNoteToHz (m.getNoteNumber()), std::memory_order_relaxed);
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        if (--countdown <= 0)
        {
            countdown = 32;
            updateModes();
        }

        float out[2];
        const float in = inGain;
        for (int c = 0; c < 2; ++c)
        {
            const float x = inputs[0][c] * in;
            const float xd = x - state[(size_t) c][0].x2;   // (1 - z^-2): the shared zeros
            float y = 0.0f;
            auto& st = state[(size_t) c];
            for (int k = 0; k < activeModes; ++k)
            {
                auto& s = st[(size_t) k];
                const auto& co = coef[(size_t) c][(size_t) k];
                const float v = co.b0 * xd + co.a1 * s.y1 - co.a2 * s.y2;
                s.y2 = s.y1;
                s.y1 = fxkit::flushDenormal (v);
                y += v;
            }
            st[0].x2 = st[0].x1;
            st[0].x1 = x;
            out[c] = inputs[0][c] * (1.0f - wet) + std::tanh (y * makeup * 0.5f) * 2.0f * wet;
        }
        outputs[0] = { out[0], out[1] };
    }

private:
    void updateModes()
    {
        const int shape = juce::jlimit (0, modalbody::shapeNames().size() - 1, (int) param (pShape));
        if (shape != builtShape) { ratio = modalbody::ratios (shape); builtShape = shape; }
        float hz = param (pPitch);
        if (param (pTrack) >= 0.5f)
            if (const float t = trackedHz.load (std::memory_order_relaxed); t > 0.0f) hz = t;
        if (pitchSmooth.value < 0.0f) pitchSmooth.snap (hz);
        hz = pitchSmooth.next (hz);

        const float decay = param (pDecay);
        const float material = param (pMaterial) * 0.01f;
        const float bright = param (pBright) * 0.01f;
        const float stretch = param (pStretch) * 0.01f;
        const float spread = param (pSpread) * 0.01f;
        const float nyq = (float) sampleRate * 0.45f;
        float sumSq = 0.0f;
        activeModes = 0;
        for (int k = 0; k < modalbody::kModes; ++k)
        {
            const float rk = std::pow (ratio[(size_t) k], 1.0f + stretch * 0.25f);
            const float f = hz * rk;
            if (f >= nyq) break;
            // the high modes die sooner the more wooden the material
            const float t60 = juce::jmax (0.01f, decay * std::pow (rk, -material * 1.6f));
            const float r = std::exp (-6.9078f / (t60 * (float) sampleRate));
            const float amp = std::pow ((float) (k + 1), -2.2f * (1.0f - bright)) * (k % 3 == 1 ? 0.8f : 1.0f);
            for (int c = 0; c < 2; ++c)
            {
                // the right bank a touch detuned against the left (Spread)
                const float detune = c == 0 ? 1.0f : 1.0f + spread * 0.006f * (1.0f + 0.3f * (float) (k % 4));
                const float w = 2.0f * fxkit::kPi * juce::jmin (nyq, f * detune) / (float) sampleRate;
                auto& co = coef[(size_t) c][(size_t) k];
                co.a1 = 2.0f * r * std::cos (w);
                co.a2 = r * r;
                co.b0 = amp * (1.0f - r * r) * 0.5f;
            }
            sumSq += amp * amp;
            ++activeModes;
        }
        // as loud for a long ring as for a short one, and for few modes as for many
        makeup = juce::jlimit (0.3f, 240.0f, 3.4f * std::sqrt (juce::jmax (0.02f, decay)) / std::sqrt (juce::jmax (1.0e-4f, sumSq)));
        inGain = juce::Decibels::decibelsToGain (param (pInput));
        wet = param (pDryWet) * 0.01f;
    }

    struct Coef { float b0 { 0 }, a1 { 0 }, a2 { 0 }; };
    struct State { float y1 { 0 }, y2 { 0 }, x1 { 0 }, x2 { 0 }; };
    std::array<std::array<Coef, modalbody::kModes>, 2> coef {};
    std::array<std::array<State, modalbody::kModes>, 2> state {};
    std::array<float, modalbody::kModes> ratio {};
    int builtShape { -1 }, activeModes { 0 }, countdown { 0 };
    float makeup { 1.0f }, inGain { 1.0f }, wet { 0.6f };
    fxkit::Smoother pitchSmooth;
    std::atomic<float> trackedHz { 0.0f };
};

static ModuleDescriptor modalBodyDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.modalbody";
    d.displayName = "Modal Body";
    d.description =
        "Plays any sound into a resonating object: up to 24 tuned modes shaped like a bar, a plate, a drum skin, a glass, "
        "a tube or a wooden box. Material makes the high modes die sooner (metal rings, wood thuds), Bright lets more of "
        "the input reach them, Stretch bends the ratios. With Track on MIDI, notes on MIDI In retune it.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 75;
    d.searchKeywords = "resonator corpus modal physical body bowl chime";
    d.sockets = { audioIn ("audioIn", "Audio In"), midiIn ("midiIn", "MIDI In"), audioOut ("audioOut", "Audio Out") };
    d.params = {
        makeCombo  ("shape",    "Shape", modalbody::shapeNames(), 3, 0, 2),
        makeRotary ("pitch",    "Pitch",    30.0f, 2000.0f, 220.0f, 0, "Hz", true),
        makeRotary ("decay",    "Decay",    0.05f, 10.0f, 1.5f, 0, "s", true),
        makeRotary ("material", "Material", 0.0f, 100.0f, 25.0f, 1, "%"),
        makeRotary ("bright",   "Bright",   0.0f, 100.0f, 55.0f, 1, "%"),
        makeRotary ("stretch",  "Stretch",  -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("spread",   "Spread",   0.0f, 100.0f, 40.0f, 1, "%"),
        makeRotary ("input",    "Input",    -18.0f, 12.0f, 0.0f, 2, "dB"),
        makeCombo  ("track",    "Track", { "Off", "MIDI" }, 0, 2, 2),
        makeRotary ("dryWet",   "Dry/Wet",  0.0f, 100.0f, 60.0f, 2, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ModalBodyModule, modalBodyDescriptor)
