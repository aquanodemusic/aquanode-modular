// Granulate, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/granulate, from aquanode-plugins)
// inside namespace aqp_granulate (see PluginPrelude.h), and its GUI module.
#include "PluginGranulateConfig.h"

namespace aqp_granulate
{
    #include "../../PluginSources/granulate/PluginProcessor.cpp"
    #include "../../PluginSources/granulate/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (GranulateGuiModule, "granulate", aqp_granulate)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& GranulateWholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "granulate";
            x.kind = "Synth";
            x.description = "Granulate whole, as one module: the grains (number, size, position, spray, window, "
                            "reversed grains), each grain's envelope, the note's envelope, amplitude modulation, pitch "
                            "and its dispersion, stereo spread and volume, played by the plugin's own engine on the "
                            "sample it has (load one on its GUI card, or pick an audio file as its preset).";
            x.section = aquanode::ModuleSection::Oscillator;
            x.midi = true;
            x.audioIn = false;
            x.sideIn = false;
            return x;
        }();
        return def;
    }

    bool GranulateWholeModuleLoad (juce::AudioProcessor& o, const juce::File& f)
    {
        auto* g = dynamic_cast<aqp_granulate::GranulateAudioProcessor*> (&o);
        if (g == nullptr || ! f.existsAsFile()) return false;
        g->loadSample (f);
        return true;
    }
}

AQP_WHOLE_PLUGIN_PATCH (GranulateWholeModule, GranulateWholeModuleDef, GranulateWholeModuleLoad)

#if AQUANODE_TEST_HARNESS
// 7.0.0: one grain through both engines (harness mode "graincompare")
#include <sstream>
std::string aqGranulateGrainCompare()
{
    std::ostringstream os;
    const double fileRate = 44100.0, hostRate = 48000.0;
    juce::AudioBuffer<float> smp (2, (int) (fileRate * 2));
    for (int i = 0; i < smp.getNumSamples(); ++i)
    {
        smp.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / fileRate));
        smp.setSample (1, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 660.0 * i / fileRate));
    }
    for (int engine = 0; engine < 2; ++engine)
    {
        aqp_granulate::GranulateAudioProcessor p;
        p.setPlayConfigDetails (0, 2, hostRate, 512);
        p.prepareToPlay (hostRate, 512);
        p.setSampleForTest (smp, fileRate);
        auto set = [&p] (const char* id, float v) { if (auto* q = p.getValueTreeState().getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        set ("numGrains", 1); set ("grainSize", 0.2f); set ("grainPosition", 0.25f); set ("spray", 0.0f);
        set ("pitchDispersion", 0.0f); set ("stereoSpread", 0.0f); set ("amplitudeMod", 0.0f); set ("amDispersion", 0.0f);
        set ("engine", (float) engine);
        const int total = (int) (hostRate * 0.45);
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        for (int pos = 0; pos < total; pos += 512)
        {
            const int n = juce::jmin (512, total - pos);
            juce::AudioBuffer<float> blk (2, n);
            blk.clear();
            juce::MidiBuffer mb;
            if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            p.processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
        }
        // tone levels in each side (40 ms .. 160 ms), and the correlation of the sides
        auto level = [&] (int ch, double hz)
        {
            double re = 0, im = 0; const int a = (int) (0.24 * hostRate), b = (int) (0.36 * hostRate);
            for (int i = a; i < b; ++i)
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * hz * i / hostRate;
                re += out.getSample (ch, i) * std::cos (w); im += out.getSample (ch, i) * std::sin (w);
            }
            return 2.0 * std::sqrt (re * re + im * im) / (b - a);
        };
        auto peakHz = [&] (int ch)
        {
            double best = 0, bestHz = 0;
            for (double hz = 380; hz <= 760; hz += 1.0) { const double v = level (ch, hz); if (v > best) { best = v; bestHz = hz; } }
            return bestHz;
        };
        double sLR = 0, sLL = 0, sRR = 0;
        for (int i = (int) (0.24 * hostRate); i < (int) (0.36 * hostRate); ++i)
        {
            const double l = out.getSample (0, i), r = out.getSample (1, i);
            sLR += l * r; sLL += l * l; sRR += r * r;
        }
        const double ratioL = fileRate / hostRate;
        os << (engine == 0 ? "Original" : "Improved")
           << ": left peak " << peakHz (0) << " Hz, right peak " << peakHz (1) << " Hz"
           << ", left has 440: " << level (0, engine ? 440.0 : 440.0 / ratioL) << " 660: " << level (0, engine ? 660.0 : 660.0 / ratioL)
           << ", right has 440: " << level (1, engine ? 440.0 : 440.0 / ratioL) << " 660: " << level (1, engine ? 660.0 : 660.0 / ratioL)
           << ", L/R correlation " << sLR / std::sqrt (sLL * sRR + 1e-30) << "\n";
    }
    return os.str();
}
#endif

#if AQUANODE_TEST_HARNESS
// 7.0.0: Engine for new instances, and for a state saved before it existed
std::string aqGranulateEngineState()
{
    aqp_granulate::GranulateAudioProcessor fresh;
    const float a = fresh.getValueTreeState().getRawParameterValue ("engine")->load();
    juce::MemoryBlock mb;
    fresh.getStateInformation (mb);
    // the same state with the engine entry taken out: a state from 6.9.x
    std::unique_ptr<juce::XmlElement> x (juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()));
    for (auto* e : x->getChildIterator())
        if (e->getStringAttribute ("id") == "engine") { x->removeChildElement (e, true); break; }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*x, old);
    aqp_granulate::GranulateAudioProcessor reopened;
    reopened.setStateInformation (old.getData(), (int) old.getSize());
    const float b = reopened.getValueTreeState().getRawParameterValue ("engine")->load();
    aqp_granulate::GranulateAudioProcessor again;
    again.setStateInformation (mb.getData(), (int) mb.getSize());
    const float c = again.getValueTreeState().getRawParameterValue ("engine")->load();
    return juce::String (a, 0).toStdString() + " " + juce::String (b, 0).toStdString() + " " + juce::String (c, 0).toStdString();
}
#endif
