#include "TunerModule.h"
#include "../../../Engine/Tuning.h"

using namespace aquanode;

namespace
{
    // YIN: the period with the deepest dip of the cumulative-mean-normalised difference
    double yinPitch (const float* x, int n, double rate, double minHz, double maxHz, double& clarity)
    {
        const int maxTau = juce::jmin (n / 2, (int) (rate / minHz)), minTau = juce::jmax (2, (int) (rate / maxHz));
        const int W = n - maxTau;
        std::vector<double> d ((size_t) maxTau + 1, 0.0);
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double s = 0.0;
            for (int i = 0; i < W; ++i) { const double v = x[i] - x[i + tau]; s += v * v; }
            d[(size_t) tau] = s;
        }
        double run = 0.0;
        std::vector<double> c ((size_t) maxTau + 1, 1.0);
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            run += d[(size_t) tau];
            c[(size_t) tau] = run > 0.0 ? d[(size_t) tau] * tau / run : 1.0;
        }
        int best = -1;
        for (int tau = minTau; tau < maxTau; ++tau)
            if (c[(size_t) tau] < 0.15)
            {
                while (tau + 1 < maxTau && c[(size_t) tau + 1] < c[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            double lo = 1.0;
            for (int tau = minTau; tau < maxTau; ++tau) if (c[(size_t) tau] < lo) { lo = c[(size_t) tau]; best = tau; }
            if (best < 0 || lo > 0.4) { clarity = 0.0; return 0.0; }
        }
        clarity = 1.0 - c[(size_t) best];
        // parabolic refinement
        double t = best;
        if (best > 1 && best + 1 <= maxTau)
        {
            const double a = c[(size_t) best - 1], b = c[(size_t) best], e = c[(size_t) best + 1];
            const double den = a - 2.0 * b + e;
            if (std::abs (den) > 1.0e-12) t += 0.5 * (a - e) / den;
        }
        return rate / t;
    }

    const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    class TunerDisplay final : public juce::Component, private juce::Timer
    {
    public:
        explicit TunerDisplay (TunerModule& m) : module (&m) { startTimerHz (15); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff121417));
            g.fillRoundedRectangle (r, 4.0f);
            auto in = r.reduced (8.0f, 6.0f);
            const auto cyan = juce::Colour (0xff1ad6ec), green = juce::Colour (0xff35e08a), amber = juce::Colour (0xffffb648);
            const bool have = hz > 0.0;
            const bool inTune = have && std::abs (cents) < 3.0;

            // the needle's scale: -50 .. +50 cents
            auto scale = in.removeFromBottom (30.0f);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            for (int c = -50; c <= 50; c += 10)
            {
                const float x = scale.getX() + scale.getWidth() * (float) (c + 50) / 100.0f;
                const float h = c == 0 ? scale.getHeight() : (c % 50 == 0 ? 14.0f : 8.0f);
                g.fillRect (x - 0.5f, scale.getBottom() - h, 1.0f, h);
            }
            if (have)
            {
                const float x = scale.getX() + scale.getWidth() * (float) (juce::jlimit (-50.0, 50.0, shownCents) + 50.0) / 100.0f;
                g.setColour (inTune ? green : amber);
                g.fillRoundedRectangle (x - 2.0f, scale.getY(), 4.0f, scale.getHeight(), 2.0f);
            }

            auto top = in;
            g.setColour (have ? (inTune ? green : juce::Colours::white) : juce::Colours::white.withAlpha (0.3f));
            g.setFont (juce::Font (juce::FontOptions (40.0f, juce::Font::bold)));
            g.drawText (have ? name : juce::String ("-"), top.removeFromLeft (top.getWidth() * 0.45f), juce::Justification::centred, false);
            g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
            g.setColour (have ? (inTune ? green : amber) : juce::Colours::white.withAlpha (0.3f));
            auto upper = top.removeFromTop (top.getHeight() * 0.5f);
            g.drawText (have ? (juce::String (shownCents >= 0.0 ? "+" : "") + juce::String (shownCents, 1) + " ct") : juce::String ("no pitch"),
                        upper, juce::Justification::centredLeft, false);
            g.setColour (cyan.withAlpha (have ? 0.9f : 0.3f));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (have ? juce::String (hz, 2) + " Hz" + (mts ? "  (MTS-ESP)" : "") : juce::String(), top, juce::Justification::centredLeft, false);
        }

    private:
        void timerCallback() override
        {
            auto* m = dynamic_cast<TunerModule*> (module.get());
            if (m == nullptr) return;
            constexpr int n = 4096;
            buf.resize (n);
            m->latest (buf.data(), n);
            float peak = 0.0f;
            for (float v : buf) peak = juce::jmax (peak, std::abs (v));
            double clarity = 0.0;
            const double f = peak > 1.0e-4f ? TunerModule::detectPitch (buf.data(), n, m->ringRate(), clarity) : 0.0;
            if (f <= 0.0 || clarity < 0.6)
            {
                if (++misses > 4) { hz = 0.0; repaint(); }
                return;
            }
            misses = 0;
            hz = f;
            // the nearest key: the MTS-ESP master's, else equal temperament on A4
            mts = tuning::hasMaster();
            int key = 69;
            double ref = 440.0;
            if (mts)
            {
                double bestD = 1.0e9;
                for (int k = 0; k < 128; ++k)
                {
                    const double kh = tuning::noteToHz ((double) k);
                    const double dd = std::abs (std::log2 (f / kh));
                    if (kh > 0.0 && dd < bestD) { bestD = dd; key = k; ref = kh; }
                }
            }
            else
            {
                const double a4 = m->getParameter ("a4");
                key = (int) std::lround (69.0 + 12.0 * std::log2 (f / a4));
                ref = a4 * std::pow (2.0, (key - 69) / 12.0);
            }
            cents = 1200.0 * std::log2 (f / ref);
            shownCents = std::abs (shownCents - cents) > 20.0 ? cents : shownCents + 0.5 * (cents - shownCents);
            name = juce::String (kNames[((key % 12) + 12) % 12]) + juce::String (key / 12 - 1);
            repaint();
        }

        juce::WeakReference<SynthModule> module;
        std::vector<float> buf;
        double hz { 0.0 }, cents { 0.0 }, shownCents { 0.0 };
        juce::String name;
        bool mts { false };
        int misses { 0 };
    };
}

double TunerModule::detectPitch (const float* x, int n, double rate, double& clarity)
{
    return yinPitch (x, n, rate, 25.0, 4000.0, clarity);
}

std::unique_ptr<juce::Component> TunerModule::createExtraContentComponent()
{
    return std::make_unique<TunerDisplay> (*this);
}

static ModuleDescriptor tunerDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.tuner";
    d.displayName = "Tuner";
    d.description =
        "Shows the pitch of what comes into Audio In: the note, the cents it is off and the frequency, with a "
        "needle; the audio passes straight through. Against A4 (the knob) in equal temperament, or against the "
        "nearest key of an MTS-ESP master when one is connected. One steady note, 25 Hz to 4 kHz.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 17;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("a4", "A4", 415.0f, 466.0f, 440.0f, 0, "Hz", false, 0.1f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (TunerModule, tunerDescriptor)
