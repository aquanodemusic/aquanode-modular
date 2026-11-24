//==============================================================================
// Spectral Difference (6.9.16) - two sounds in, what one has that the other
// has not out: bin by bin, the louder's magnitude less the other's.
//
//   A - B      |A| - |B| (at A's phase): what A has more of than B
//   B - A      |B| - |A| (at B's phase)
//   Both Ways  | |A| - |B| | (at the louder one's phase): wherever they differ
//
// Amount scales the one taken away (100 %: all of it; past it, more), the drawn
// curve per bin where in the spectrum that happens. Nothing can get louder than
// it came in: a bin's result is never above the louder of the two there (a
// difference of magnitudes cannot be), and a safety limiter at the output
// keeps the sum of the bins under full scale. Mute Where One Is Silent: the bins
// where A or B has no sound (under -70 dB) go quiet too - only where both sound
// is their difference heard (else B - A passes what only B has, and so on).
//
// The card: A's spectrum (cyan), B's (pink) - each colour a knob as light, as
// on the periodogram - and the difference over them in white; the curve below.
// Inputs: A In, B In. Output: Audio Out (stereo). Latency: one FFT.
//==============================================================================
#include "SpectralCore.h"
#include "../../Utilities/Analysis/LightColour.h"

using namespace aquanode;

namespace specdiff
{
    constexpr float kNmA = 487.0f, kNmB = 730.0f;   // cyan, pink (a purple): the periodogram's A and B
    const juce::Colour kOriginalA (0xff3fe6f2), kOriginalB (0xffff6fbe);
}

class SpecDiffModule final : public SpectralModuleBase
{
public:
    enum P { pMode = 0, pAmount, pMute, pDryWet, pColA, pColB };   // (then the curve, then the FFT size)
    static constexpr int kCurveOffset = 6;

    SpecDiffModule() : SpectralModuleBase (2, 1.0f)
    {
        for (auto* a : { &dispA, &dispB, &dispOut })
            for (auto& v : *a) v.store (-120.0f, std::memory_order_relaxed);
    }

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.specdiff";
        d.displayName = "Spectral Difference";
        d.searchKeywords = "spectral difference subtract delta compare a b bins";
        d.description = "Two sounds in, their difference out, bin by bin: A - B is what A has more of than B (at A's "
                        "phase), B - A the other way round, Both Ways wherever they differ (at the louder one's phase). "
                        "Amount scales the one taken away, the curve sets where in the spectrum. Nothing comes out louder "
                        "than it went in: each bin stays under the louder input there, and a safety limiter guards the "
                        "output. Mute Where One Is Silent leaves only the bins where both sound. The card shows A in cyan "
                        "and B in pink (each colour a knob, as light) and their difference in white.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 24;
        d.sockets = { audioIn ("aIn", "A In"), audioIn ("bIn", "B In"), audioOut ("audioOut", "Audio Out") };
        auto lightKnob = [] (const char* id, const char* label, float def)
        {
            auto p = makeRotary (id, label, light::kMinNm, light::kMaxNm, def, 1, {}, false, 1.0f).noMod();
            p.textFromValue = [] (double v) { return light::textOf (v); };
            return p;
        };
        d.params = {
            makeCombo ("mode", "Mode", { "A - B", "B - A", "Both Ways" }, 0, 0, 2).noMod(),
            makeRotary ("amount", "Amount", 0.0f, 200.0f, 100.0f, 0, "%"),
            makeCombo ("mute", "Mute Where One Is Silent", { "Off", "On" }, 0, 0, 2).noMod(),
            makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 100.0f, 0, "%"),
            lightKnob ("colA", "A Colour", specdiff::kNmA),
            lightKnob ("colB", "B Colour", specdiff::kNmB)
        };
        SpectralModuleBase::addCurveParams (d, 1.0f);
        return d;
    }

    void prepare (double sr) override
    {
        SpectralModuleBase::prepare (sr);
        limitRelease = std::exp (-1.0f / (0.1f * (float) sr));
        limitEnv = 0.0f;
    }

    void spectralReset() override { limitEnv = 0.0f; }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        SpectralModuleBase::processSample (inputs, outputs);
        // the safety limiter: never over full scale (instant down, back up over 100 ms)
        const float peak = juce::jmax (std::abs (outputs[0][0]), std::abs (outputs[0][1]));
        limitEnv = peak > limitEnv ? peak : peak + limitRelease * (limitEnv - peak);
        if (! std::isfinite (limitEnv)) { limitEnv = 0.0f; outputs[0] = { 0.0f, 0.0f }; return; }
        constexpr float ceiling = 0.98f;
        if (limitEnv > ceiling)
        {
            const float g = ceiling / limitEnv;
            outputs[0] = { outputs[0][0] * g, outputs[0][1] * g };
        }
    }

    // the card (message thread): 0 A, 1 B, 2 the difference; dB (0: a full-scale sine)
    float display (int which, int point) const
    {
        const auto& a = which == 0 ? dispA : which == 1 ? dispB : dispOut;
        return a[(size_t) juce::jlimit (0, kDisplayPoints - 1, point)].load (std::memory_order_relaxed);
    }
    float knob (int index) const { return getParameterBase (index); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 150 + 64; }
    int preferredModuleWidth() const override { return 420; }

protected:
    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return kCurveOffset; }

    void processSpectrum (int channel, std::complex<float>* bins, const std::complex<float>* sideBins) override
    {
        if (sideBins == nullptr) return;
        const int mode = (int) std::lround (param (pMode));
        const float amount = param (pAmount) * 0.01f;
        const bool mute = param (pMute) > 0.5f;
        const float silent = (float) fftSize * 0.25f * 3.1623e-4f;   // -70 dB of a full-scale sine's bin
        if (channel == 0)
            noteInput (bins);   // (A as it came, for the card)

        for (int b = 0; b < numBins; ++b)
        {
            const std::complex<float> a = bins[b], s = sideBins[b];
            const float ma = std::abs (a), mb = std::abs (s);
            if (! std::isfinite (ma) || ! std::isfinite (mb)) { bins[b] = {}; continue; }
            if (mute && (ma < silent || mb < silent)) { bins[b] = {}; continue; }
            const float k = amount * binCurve[b];
            float out = 0.0f;
            std::complex<float> phaseOf = a;
            switch (mode)
            {
                case 1:  out = mb - k * ma; phaseOf = s; break;
                case 2:  out = std::abs (ma - k * mb); phaseOf = ma >= mb ? a : s; break;
                default: out = ma - k * mb; break;
            }
            // never louder than the louder input here
            out = juce::jlimit (0.0f, juce::jmax (ma, mb), out);
            const float mp = std::abs (phaseOf);
            bins[b] = mp > 1.0e-20f ? phaseOf * (out / mp) : std::complex<float> (out, 0.0f);
        }

        if (channel == 0)
            updateViews (sideBins, bins);
    }

private:
    // the card's spectra: A (noted before it changed), B and the result, per display point
    void updateViews (const std::complex<float>* sideBins, const std::complex<float>* outBins)
    {
        const float toDb = 4.0f / (float) fftSize;   // a full-scale sine's bin reads 1
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            const double t = (double) p / (double) (kDisplayPoints - 1);
            const double f0 = 20.0 * std::pow (sampleRate * 0.5 / 20.0, t);
            const double f1 = 20.0 * std::pow (sampleRate * 0.5 / 20.0, (double) (p + 1) / (double) (kDisplayPoints - 1));
            const int b0 = juce::jlimit (0, numBins - 1, (int) (f0 * fftSize / sampleRate));
            const int b1 = juce::jlimit (b0, numBins - 1, (int) (f1 * fftSize / sampleRate));
            float mb = 0.0f, mo = 0.0f;
            for (int b = b0; b <= b1; ++b)
            {
                mb = juce::jmax (mb, std::abs (sideBins[b]));
                mo = juce::jmax (mo, std::abs (outBins[b]));
            }
            auto dbOf = [toDb] (float m) { return juce::Decibels::gainToDecibels (m * toDb, -120.0f); };
            auto ease = [] (std::atomic<float>& slot, float db)
            {
                const float old = slot.load (std::memory_order_relaxed);
                slot.store (db > old ? db : old + 0.25f * (db - old), std::memory_order_relaxed);
            };
            ease (dispA[(size_t) p], dbOf (lastA[(size_t) p]));
            ease (dispB[(size_t) p], dbOf (mb));
            ease (dispOut[(size_t) p], dbOf (mo));
        }
    }

    // A's spectrum for the card, taken before it is changed
    void noteInput (const std::complex<float>* aBins)
    {
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            const double t = (double) p / (double) (kDisplayPoints - 1);
            const double f0 = 20.0 * std::pow (sampleRate * 0.5 / 20.0, t);
            const double f1 = 20.0 * std::pow (sampleRate * 0.5 / 20.0, (double) (p + 1) / (double) (kDisplayPoints - 1));
            const int b0 = juce::jlimit (0, numBins - 1, (int) (f0 * fftSize / sampleRate));
            const int b1 = juce::jlimit (b0, numBins - 1, (int) (f1 * fftSize / sampleRate));
            float m = 0.0f;
            for (int b = b0; b <= b1; ++b) m = juce::jmax (m, std::abs (aBins[b]));
            lastA[(size_t) p] = m;
        }
    }

    std::array<std::atomic<float>, kDisplayPoints> dispA, dispB, dispOut;
    std::array<float, kDisplayPoints> lastA {};
    float limitEnv { 0.0f }, limitRelease { 0.999f };
};

//==============================================================================
// The card: A, B and their difference; the curve below
//==============================================================================
namespace
{
    class DiffView final : public juce::Component, private juce::Timer
    {
    public:
        explicit DiffView (SpecDiffModule& m) : module (&m), raw (&m) { startTimerHz (24); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (juce::Colour (0xff0b0d10));
            g.fillRoundedRectangle (r, 4.0f);
            auto* m = mod();
            if (m == nullptr) return;
            const auto colA = light::colourAt (m->knob (SpecDiffModule::pColA), specdiff::kNmA, specdiff::kOriginalA);
            const auto colB = light::colourAt (m->knob (SpecDiffModule::pColB), specdiff::kNmB, specdiff::kOriginalB);
            auto plot = r.reduced (6.0f, 4.0f).withTrimmedBottom (12.0f);
            constexpr float lo = -96.0f, hi = 0.0f;
            auto yOf = [&] (float db) { return plot.getBottom() - (juce::jlimit (lo, hi, db) - lo) / (hi - lo) * plot.getHeight(); };
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            for (float db : { -24.0f, -48.0f, -72.0f })
            {
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
            }
            for (double hz : { 100.0, 1000.0, 10000.0 })
            {
                const float x = plot.getX() + (float) (std::log (hz / 20.0) / std::log (22050.0 / 20.0)) * plot.getWidth();
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x - 16.0f, plot.getBottom() + 1.0f, 32.0f, 11.0f), juce::Justification::centred, false);
            }
            auto curve = [&] (int which, juce::Colour c, bool fill)
            {
                juce::Path p;
                for (int i = 0; i < SpecDiffModule::kDisplayPoints; ++i)
                {
                    const float x = plot.getX() + (float) i / (float) (SpecDiffModule::kDisplayPoints - 1) * plot.getWidth();
                    const float y = yOf (m->display (which, i));
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                if (fill)
                {
                    auto f = p;
                    f.lineTo (plot.getRight(), plot.getBottom());
                    f.lineTo (plot.getX(), plot.getBottom());
                    f.closeSubPath();
                    g.setColour (c.withAlpha (0.14f));
                    g.fillPath (f);
                }
                g.setColour (c.withAlpha (0.9f));
                g.strokePath (p, juce::PathStrokeType (fill ? 1.3f : 1.8f));
            };
            curve (0, colA, true);
            curve (1, colB, true);
            curve (2, juce::Colours::white, false);
            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            auto legend = plot.removeFromTop (14.0f).reduced (4.0f, 0.0f);
            g.setColour (colA); g.drawText ("A", legend.removeFromLeft (16.0f), juce::Justification::centredLeft, false);
            g.setColour (colB); g.drawText ("B", legend.removeFromLeft (16.0f), juce::Justification::centredLeft, false);
            g.setColour (juce::Colours::white);
            static const char* const modes[] = { "A - B", "B - A", "Both Ways" };
            g.drawText (modes[juce::jlimit (0, 2, (int) std::lround (m->knob (SpecDiffModule::pMode)))], legend, juce::Justification::centredLeft, false);
        }

    private:
        SpecDiffModule* mod() const { return module.get() != nullptr ? raw : nullptr; }
        void timerCallback() override { repaint(); }
        juce::WeakReference<SynthModule> module;
        SpecDiffModule* raw;
    };

    class DiffCard final : public juce::Component, public ExtraContentParamListener
    {
    public:
        DiffCard (SpecDiffModule& m, std::unique_ptr<juce::Component> curveEditor) : view (m), curve (std::move (curveEditor))
        {
            addAndMakeVisible (view);
            if (curve != nullptr) addAndMakeVisible (*curve);
            // (the curve's edits reach the card through this one)
            if (auto* l = dynamic_cast<ExtraContentParamListener*> (curve.get()))
            {
                l->paramsChangedByContent = [this] { if (paramsChangedByContent) paramsChangedByContent(); };
                l->paramValuesChangedByContent = [this] { if (paramValuesChangedByContent) paramValuesChangedByContent(); };
            }
        }
        void resized() override
        {
            auto r = getLocalBounds();
            if (curve != nullptr) curve->setBounds (r.removeFromBottom (64));
            view.setBounds (r);
        }
        DiffView view;
        std::unique_ptr<juce::Component> curve;
    };
}

std::unique_ptr<juce::Component> SpecDiffModule::createExtraContentComponent()
{
    return std::make_unique<DiffCard> (*this, SpectralModuleBase::createExtraContentComponent());
}

AQUANODE_REGISTER_MODULE (SpecDiffModule, SpecDiffModule::descriptor)
