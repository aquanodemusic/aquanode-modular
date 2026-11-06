//==============================================================================
// PrecisionUtility - Patch version
//
// One module, since PrecisionUtility is one thing: "PrecisionUtility -
// Utility" is the plugin's processBlock sample for sample - the volume per
// channel, the blendable polarity invert, up to 10 s of artificial latency
// (a delay of the signal, no echo, at whole samples), then where each input
// channel goes in the stereo field - with every knob taking cables. At its
// defaults it is bit for bit transparent, like the plugin.
//
// (Align, the older utility module, is a different design: a sub-sample
// delay per channel up to 200 ms. It stays as it was.)
//==============================================================================
#include "../Host/PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "precisionutility" };
}

class PrecisionUtilityUtilityModule final : public SynthModule
{
public:
    enum P { pDelay, pPhase, pVolL, pVolR, pPanL, pPanR };

    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.precisionutility.utility";
        d.displayName = kit::moduleName (kPlugin, "Utility");
        d.description = "PrecisionUtility whole: Vol L / Vol R, then Phase blends towards the inverted signal "
                        "(50 % cancels it, 100 % flips it), Latency delays the signal by up to 10 s (no echo - "
                        "the sound simply comes later), and Pan L / Pan R place each input channel (-100 left, "
                        "+100 right). Bit for bit transparent at its defaults.";
        d.section = ModuleSection::Effect;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("delay", "Latency", 0.0f, 10000.0f, 0.0f, 0, "ms", false, 0.025f),
            makeRotary ("phase", "Phase", 0.0f, 100.0f, 0.0f, 0, "%"),
            makeRotary ("volLeft", "Vol L", 0.0f, 2.0f, 1.0f, 1, "x"),
            makeRotary ("volRight", "Vol R", 0.0f, 2.0f, 1.0f, 1, "x"),
            makeRotary ("panLeft", "Pan L", -100.0f, 100.0f, -100.0f, 1),
            makeRotary ("panRight", "Pan R", -100.0f, 100.0f, 100.0f, 1)
        };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        bufferSize = juce::jmax (1, (int) (sr * 10.0) + 1);
        for (auto& b : buffer)
            b.assign ((size_t) bufferSize, 0.0f);
        reset();
    }

    void reset() override
    {
        for (auto& b : buffer)
            std::fill (b.begin(), b.end(), 0.0f);
        writePos = 0;
    }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        const float delayMs = param (pDelay), phasePct = param (pPhase);
        const float volL = param (pVolL), volR = param (pVolR);
        const float panL = param (pPanL), panR = param (pPanR);

        float l = in[0][0], r = in[0][1];
        const int delaySamples = juce::jlimit (0, bufferSize - 1, (int) ((delayMs / 1000.0) * sampleRate));

        // the plugin's bypass: nothing changes, nothing is delayed
        if (delayMs == 0.0f && phasePct == 0.0f && panL == -100.0f && panR == 100.0f && volL == 1.0f && volR == 1.0f)
        {
            out[0] = in[0];
            return;
        }

        l *= volL;
        r *= volR;

        const float phase = phasePct / 100.0f;
        if (delaySamples > 0 || phase > 0.0f)
        {
            l = l * (1.0f - phase) - l * phase;
            r = r * (1.0f - phase) - r * phase;
            if (delaySamples > 0 && bufferSize > 1)
            {
                buffer[0][(size_t) writePos] = l;
                buffer[1][(size_t) writePos] = r;
                int readPos = writePos - delaySamples;
                if (readPos < 0)
                    readPos += bufferSize;
                l = buffer[0][(size_t) readPos];
                r = buffer[1][(size_t) readPos];
                if (++writePos >= bufferSize)
                    writePos = 0;
            }
        }

        if (panL != -100.0f || panR != 100.0f)
        {
            const float lp = (panL + 100.0f) / 200.0f, rp = (panR + 100.0f) / 200.0f;
            const float nl = l * (1.0f - lp) + r * (1.0f - rp);
            const float nr = l * lp + r * rp;
            l = nl;
            r = nr;
        }
        out[0] = { l, r };
    }

private:
    std::array<std::vector<float>, 2> buffer;
    int bufferSize { 1 }, writePos { 0 };
};

AQUANODE_REGISTER_MODULE (PrecisionUtilityUtilityModule, PrecisionUtilityUtilityModule::descriptor)

//==============================================================================
namespace aquanode::plugins::precisionutility
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int m = b.add ("plugin.precisionutility.utility", 0, 0);
        b.copy (m, original);

        PatchPorts ports;
        ports.name = "PrecisionUtility";
        ports.audioOut = { { m, "audioOut" } };
        ports.audioIn = { { m, "audioIn" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
