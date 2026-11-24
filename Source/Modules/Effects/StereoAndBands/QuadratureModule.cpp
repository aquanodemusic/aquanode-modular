//==============================================================================
// Quadrature Stereo (6.9.18) - stereo that sums back to mono cleanly: the left
// and the right are the same sound 90 degrees apart at every frequency (two
// chains of all-pass filters whose phases stay a quarter turn apart), so their
// sum has the same level at every frequency - no comb filtering, as a Haas
// delay or a chorus would leave when folded to mono.
//
// The all-pass pair is our own: nine second-order (in z^-2) sections on each
// side, one side a sample later, the coefficients fitted numerically so the
// phase difference stays within 0.02 degrees of 90 from 4.5 Hz to 23 kHz at
// 48 kHz (so 18 Hz up at 192 kHz). Both sides keep the sound's level exactly
// (all-pass), so nothing gets louder or duller.
//   Width       how far the right side turns from the left (0: both the same)
//   Keep Bass Mono (off by default)  below Bass Freq the right stays with the
//               left (a 12 dB/oct high-pass on the turn)
// Input (6.9.19): Mono First takes the input as mono (left plus right, halved);
// Keep Stereo turns only its middle (L + R) into the pair and adds its side
// (L - R) back through the same all-pass as the left, so it stays in line: the
// sound keeps its own stereo, and in mono the side cancels as it would anyway
// while the middle stays flat. Save IR writes the pair as a stereo .wav at the
// knobs' settings (the middle's - an impulse response cannot hold Keep Stereo's
// routing; IR Forge and the Convolver load it). No latency. Global.
// Input: Audio In. Output: Audio Out.
//==============================================================================
#include "../../Core/ModuleCore.h"
#include "../../Shared/ImpulseSave.h"

using namespace aquanode;

namespace quadrature
{
    constexpr int kSections = 9;
    constexpr double kA[kSections] = { 0.053381310533, 0.370010370222, 0.683964114979, 0.863407806692, 0.944754145367,
                                       0.978267507803, 0.991591224292, 0.996839619970, 0.999043456374 };
    constexpr double kB[kSections] = { 0.192485691714, 0.542018508954, 0.789661592429, 0.912686317297, 0.965277023002,
                                       0.986455111657, 0.994806391882, 0.998154792950, 0.999704919714 };

    struct Core
    {
        // one section: y[n] = a x[n] - x[n-2] + a y[n-2]
        struct Section { double x1 { 0 }, x2 { 0 }, y1 { 0 }, y2 { 0 }; };
        Section a[kSections], b[kSections], s[kSections];   // (s: the side, Keep Stereo)
        double bDelay { 0 };
        double hz1 { 0 }, hz2 { 0 };   // the high-pass (transposed direct form II)
        double hb0 { 1 }, hb1 { 0 }, hb2 { 0 }, ha1 { 0 }, ha2 { 0 };

        static double run (Section* s, const double* coef, double x)
        {
            for (int k = 0; k < kSections; ++k)
            {
                auto& q = s[k];
                const double y = coef[k] * x - q.x2 + coef[k] * q.y2;
                q.x2 = q.x1; q.x1 = x;
                q.y2 = q.y1; q.y1 = y;
                x = y;
            }
            return x;
        }

        void setHighPass (double hz, double sr)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jlimit (5.0, 0.45 * sr, hz) / sr;
            const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * 0.70710678);
            const double a0 = 1.0 + alpha;
            hb0 = (1.0 + cw) * 0.5 / a0; hb1 = -(1.0 + cw) / a0; hb2 = hb0;
            ha1 = -2.0 * cw / a0; ha2 = (1.0 - alpha) / a0;
        }

        std::array<float, 2> process (double m, double width, bool bassMono)
        {
            const double left = run (a, kA, m);
            const double right90 = run (b, kB, bDelay);
            bDelay = m;
            double turn = right90 - left;
            if (bassMono)
            {
                const double y = hb0 * turn + hz1;
                hz1 = hb1 * turn - ha1 * y + hz2;
                hz2 = hb2 * turn - ha2 * y;
                turn = y;
            }
            return { (float) left, (float) (left + width * turn) };
        }

        // (6.9.19) the side through the left's all-pass: added to the left, taken from the right
        double side (double x) { return run (s, kA, x); }
    };
}

class QuadratureModule final : public SynthModule
{
public:
    enum P { pWidth = 0, pBassMono, pBassFreq, pSave, pInput };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "fx.quadrature";
        d.displayName = "Quadrature Stereo";
        d.searchKeywords = "quadrature hilbert 90 degree phase stereo width mono compatible allpass impulse response ir";
        d.description = "Stereo that folds back to mono cleanly: left and right are the same sound 90 degrees apart at every "
                        "frequency (two all-pass chains), so their sum keeps the same level everywhere - no comb filtering. "
                        "Width: how far the right turns from the left. Keep Bass Mono (off by default) keeps the lows below "
                        "Bass Freq together. Input: Mono First takes the sound as mono; Keep Stereo keeps its own stereo "
                        "and turns only its middle. Save IR writes the pair (the middle's) as a stereo .wav.";
        d.section = ModuleSection::Effect;
        d.sidebarOrder = 30;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("width", "Width", 0.0f, 100.0f, 100.0f, 0, "%", false, 1.0f),
            makeCombo ("bassMono", "Keep Bass Mono", { "Off", "On" }, 0, 0, 2).noMod(),
            makeRotary ("bassFreq", "Bass Freq", 20.0f, 500.0f, 120.0f, 0, "Hz", true).noMod().visibleWhen ("bassMono", 1.0f),
            makeButton ("saveIR", "Save IR", 1, 2),
            makeCombo ("input", "Input", { "Mono First", "Keep Stereo" }, 0, 0, 2).noMod()   // (6.9.19)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override { core = {}; shownHz = -1.0f; hpTick = 64; }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        const bool bass = param (pBassMono) > 0.5f;
        if (bass && ++hpTick >= 64)
        {
            hpTick = 0;
            if (const float hz = param (pBassFreq); std::abs (hz - shownHz) > 0.01f) { shownHz = hz; core.setHighPass (hz, sampleRate); }
        }
        const double m = 0.5 * ((double) inputs[0][0] + (double) inputs[0][1]);
        auto o = core.process (m, param (pWidth) * 0.01, bass);
        if (param (pInput) > 0.5f)   // Keep Stereo: the side back, in line with the left
        {
            const float sd = (float) core.side (0.5 * ((double) inputs[0][0] - (double) inputs[0][1]));
            o[0] += sd;
            o[1] -= sd;
        }
        outputs[0] = { std::isfinite (o[0]) ? o[0] : 0.0f, std::isfinite (o[1]) ? o[1] : 0.0f };
    }

    // the pair at the knobs' settings (message thread: Save IR, and the tests)
    juce::AudioBuffer<float> impulseResponse() const
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        quadrature::Core c;
        const bool bass = getParameterBase (pBassMono) > 0.5f;
        c.setHighPass (getParameterBase (pBassFreq), sr);
        juce::AudioBuffer<float> ir (2, (int) (sr * 1.5));
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            const auto o = c.process (i == 0 ? 1.0 : 0.0, getParameterBase (pWidth) * 0.01, bass);
            ir.setSample (0, i, o[0]);
            ir.setSample (1, i, o[1]);
        }
        return ir;
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id != "saveIR") return;
        impulse::save (chooser, impulseResponse(), sampleRate > 0.0 ? sampleRate : 48000.0,
                       "Quadrature Stereo " + juce::String (juce::roundToInt (getParameterBase (pWidth))) + "%", "Quadrature Stereo");
    }

private:
    quadrature::Core core;
    float shownHz { -1.0f };
    int hpTick { 64 };
    std::unique_ptr<juce::FileChooser> chooser;
};

AQUANODE_REGISTER_MODULE (QuadratureModule, QuadratureModule::descriptor)
