//==============================================================================
// Springer - Patch version
//
// Springer is up to seven spring lines in a ring: the input (both channels,
// driven into a soft tanh) feeds every coil, each coil also hears the coil
// before it (X-Couple), and the coils alternate left / right into the output.
// So its patch version is that ring, laid out:
//
//   Springer - Coil     one spring line, running Springer's own SpringLine:
//                       the modulated delay (Size x Coil Scale x Pitch, spread
//                       by its place among the coils), the density all-pass
//                       (Pre- / Tank-Diffuse, alternating), the dispersion
//                       all-passes (Stages, Chirp - its own random curve,
//                       Random AP rolls a new one), the damping and the tanh
//                       in its feedback. Couple In takes the previous coil.
//   Springer - Output   the coils' sum with Springer's loudness for the coil
//                       count, Mix (its wet gain) and Wet Only.
//
// The builder wires the ring as the plugin runs it (coil 1 hears the last
// coil); re-patch it, add coils of different sizes, modulate any knob.
//==============================================================================
#include "PluginSpringerConfig.h"

namespace aqp_springer
{
    #include AQP_SPRINGER_SRC(PluginProcessor.h)
}

#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "springer" };
    using SpringLine = aqp_springer::SpringerAudioProcessor::SpringLine;
    constexpr int kMaxDelay = 1500000;   // as Springer: Size 50000 x Coil Scale 4 x the lowest Pitch, with room
    constexpr int kStages = 256;
}

//==============================================================================
class SpringerCoilModule final : public SynthModule
{
public:
    enum { pWidth = 0, pResonance, pCoupling, pDamping, pSpread, pChirp, pStages, pPitch,
           pLfoRate, pLfoDepth, pDensityA, pDensityB, pNumCoils, pCoil, pCoilLen, pRandom };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.springer.coil";
        d.displayName = kit::moduleName (kPlugin, "Coil");
        d.description = "One of Springer's spring lines: Audio In (both channels, softly driven) plus the previous "
                        "coil at Couple In (by X-Couple) into a delay of Size x Coil Scale (deeper with Pitch, "
                        "spread apart by its place among the Coils), Pre- or Tank-Diffuse smearing, Stages of "
                        "dispersion all-passes bent by Chirp, Damping and a soft clip - fed back by Feedback. Mod "
                        "Rate / Depth wobble the delay. Random AP rolls the all-passes' curve anew.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("coupleIn", "Couple In"), audioOut ("audioOut", "Coil Out") };
        d.params = {
            makeRotary ("width",     "Size",         0.001f, 50000.0f, 500.0f, 0, {}, true),
            makeRotary ("resonance", "Feedback",     0.0f, 1.2001f, 0.65f, 0),
            makeRotary ("coupling",  "X-Couple",     0.0f, 3.001f, 0.2f, 0),
            makeRotary ("damping",   "Damping",      0.0f, 1.0f, 0.4f, 0),

            makeRotary ("spread",    "Spread",       0.0f, 1.0f, 0.2f, 1),
            makeRotary ("chirp",     "Chirp",        0.0f, 1.0f, 0.5f, 1),
            makeRotary ("numStages", "Stages",       1.0f, (float) kStages, 32.0f, 1, {}, false, 1.0f),
            makeRotary ("pitch",     "Pitch",        -32.0f, 32.0f, 0.0f, 1, "st"),

            makeRotary ("lfoRate",   "Mod Rate",     0.01f, 100.0f, 0.1f, 2, "Hz", true),
            makeRotary ("lfoDepth",  "Mod Depth",    0.001f, 50.0f, 0.2f, 2, {}, true),
            makeRotary ("densityA",  "Pre-Diffuse",  50.0f, 1000.0f, 223.0f, 2),
            makeRotary ("densityB",  "Tank-Diffuse", 50.0f, 1000.0f, 307.0f, 2),

            makeRotary ("numCoils",  "Coils",        1.0f, 7.0f, 2.0f, 3, {}, false, 1.0f).noMod(),
            makeRotary ("coil",      "Coil",         1.0f, 7.0f, 1.0f, 3, {}, false, 1.0f).noMod(),
            makeRotary ("coilLen",   "Coil Scale",   0.1f, 4.0f, 1.0f, 3),
            makeButton ("random",    "Random AP", 3)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    SpringerCoilModule() { rollCurve(); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        spring.prepare (sr, kMaxDelay);
        spring.maxSafeDelay = (float) (kMaxDelay - 32);
        countdown = 0;
    }

    void reset() override
    {
        spring.reset();
        countdown = 0;
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (id == "random")
            rollCurve();
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        if (--countdown <= 0)
        {
            update();
            countdown = 32;
        }

        // Springer's ring: the driven input plus the previous coil, through this line
        const float resonance = param (pResonance), coupling = param (pCoupling);
        const float feedback = juce::jlimit (0.0f, 1.02f, resonance * (1.02f / 1.2f) - coupling * 0.05f);
        const float driveIn = std::tanh ((in[0][0] + in[0][1]) * 0.75f);
        const float coupled = driveIn + in[1][0] * coupling * 0.15f;
        const float y = spring.process (coupled, feedback, param (pLfoRate), sampleRate);
        out[0] = { y, y };
    }

private:
    // Springer's randomizeSprings, for this coil
    void rollCurve()
    {
        auto& rng = juce::Random::getSystemRandom();
        std::array<float, kStages> c {};
        for (int i = 0; i < kStages; ++i)
        {
            const float norm = (float) i / 255.0f;
            c[(size_t) i] = juce::jlimit (0.1f, 0.95f, 0.65f + 0.3f * norm * norm + rng.nextFloat() * 0.04f - 0.02f);
        }
        const juce::SpinLock::ScopedLockType sl (curveLock);
        pendingCurve = c;
        curveFresh = true;
    }

    // Springer's block-rate setup, for this coil
    void update()
    {
        {
            const juce::SpinLock::ScopedTryLockType sl (curveLock);
            if (sl.isLocked() && curveFresh)
            {
                curve = pendingCurve;
                curveFresh = false;
            }
        }
        const float pitchScale = std::pow (2.0f, -param (pPitch) / 12.0f);
        const int stages = juce::jlimit (1, kStages, (int) std::lround (param (pStages)));
        const int coils = juce::jlimit (1, 7, (int) std::lround (param (pNumCoils)));
        const int c = juce::jlimit (0, coils - 1, (int) std::lround (param (pCoil)) - 1);

        spring.numDispersionStages = stages;
        spring.dampingCoeff = 1.0f - juce::jlimit (0.0f, 0.99f, param (pDamping) + (pitchScale - 1.0f) * 0.15f);
        spring.targetDepthScaled = param (pLfoDepth) * pitchScale;

        float spreadFactor = 1.0f;
        if (coils > 1)
            spreadFactor = 1.0f + ((float) c / (float) (coils - 1) - 0.5f) * param (pSpread) * 0.15f;
        spring.targetDelayScaled = param (pWidth) * param (pCoilLen) * pitchScale * spreadFactor;
        spring.densityFilter.setDelay (((c & 1) ? param (pDensityB) : param (pDensityA)) * pitchScale);

        const float shift = (pitchScale - 1.0f) * 0.08f + (param (pChirp) - 0.5f) * 0.4f;
        for (int i = 0; i < stages; ++i)
            spring.cachedCoeffs[(size_t) i] = juce::jlimit (0.05f, 0.985f, curve[(size_t) i] + shift);
    }

    SpringLine spring;
    std::array<float, kStages> curve {}, pendingCurve {};
    juce::SpinLock curveLock;
    bool curveFresh { false };
    int countdown { 0 };
};

AQUANODE_REGISTER_MODULE (SpringerCoilModule, SpringerCoilModule::descriptor)

//==============================================================================
class SpringerOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.springer.output";
        d.displayName = kit::moduleName (kPlugin, "Output");
        d.description = "Springer's output: the coils patched into Left / Right Coils (one coil: both sides), "
                        "levelled for the number of Coils as Springer does, at Mix (its wet gain, up to 2x) on "
                        "top of the dry signal from Dry In - or alone with Wet Only.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 401;
        d.sockets = { audioIn ("audioIn", "Dry In"), audioIn ("leftIn", "Left Coils"), audioIn ("rightIn", "Right Coils"),
                      audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("wet",      "Mix",      0.0f, 2.0f, 0.5f, 0),
            makeCombo  ("muteDry",  "Wet Only", { "Off", "On" }, 0, 0, 1),
            makeRotary ("numCoils", "Coils",    1.0f, 7.0f, 2.0f, 0, {}, false, 1.0f).noMod()
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const int coils = juce::jlimit (1, 7, (int) std::lround (param (2)));
        const float wetGain = param (0) * 1.2f / (1.0f + std::sqrt ((float) coils) * 0.4f);
        const bool dry = std::lround (param (1)) == 0;
        float l = in[1][0], r = in[2][0];
        if (coils <= 1)
            l = r = l + r;   // one coil: on both sides, as Springer
        out[0] = { (dry ? in[0][0] : 0.0f) + l * wetGain, (dry ? in[0][1] : 0.0f) + r * wetGain };
    }
};

AQUANODE_REGISTER_MODULE (SpringerOutputModule, SpringerOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::springer
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int coils = juce::jlimit (1, 7, (int) std::lround (kit::param (original, "numCoils", 2.0f)));
        const int perRow = juce::jmin (coils, 4);
        const int output = b.add ("plugin.springer.output", perRow * 330, 0);
        b.copy (output, original);

        PatchPorts ports;
        ports.name = "Springer";
        std::vector<int> ids;
        for (int c = 0; c < coils; ++c)
        {
            const int coil = b.add ("plugin.springer.coil", (c % 4) * 330, (c / 4) * 430);
            b.copy (coil, original);
            b.set (coil, "coil", (float) (c + 1));
            b.set (coil, "coilLen", kit::param (original, "coilLen" + juce::String (c + 1), 1.0f));
            b.cable (coil, "audioOut", output, (coils > 1 && (c & 1)) ? "rightIn" : "leftIn");
            ports.audioIn.push_back ({ coil, "audioIn" });
            ids.push_back (coil);
        }
        // the ring: each coil hears the one before it, the first the last
        for (int c = 0; c < coils; ++c)
            b.cable (ids[(size_t) (c == 0 ? coils - 1 : c - 1)], "audioOut", ids[(size_t) c], "coupleIn");

        ports.audioIn.push_back ({ output, "audioIn" });
        ports.audioOut = { { output, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
