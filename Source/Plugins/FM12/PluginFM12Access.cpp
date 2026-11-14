// FM12's ties to the rest of Aquanode Modular: its DX7 menu draws on the
// app's built-in DX7 voices (Presets/FMVoices.cpp), and the Random Plugin
// Preset module can roll FM12 sounds from them.
#include "PluginFM12Config.h"

namespace aqp_fm12
{
    #include AQP_FM12_SRC(PluginProcessor.h)
    #include AQP_FM12_SRC(FM12DX7.h)
}

#include "../../Presets/DX7Generator.h"
#include "../Host/RandomPresetSources.h"

namespace
{
    namespace dx = aqp_fm12::fm12dx7;

    std::vector<dx::Voice> voicesOf (int category)
    {
        std::vector<dx::Voice> out;
        const auto cats = aquanode::dx7::categories();
        for (const auto& fv : aquanode::dx7::factoryVoices())
            if (fv.category == category)
            {
                dx::Voice v;
                std::copy (fv.voice.vced.begin(), fv.voice.vced.end(), v.vced.begin());
                v.name = cats[fv.category] + " " + juce::String (fv.number).paddedLeft ('0', 2);
                out.push_back (v);
            }
        return out;
    }

    const bool fm12VoicesHandedIn = []
    {
        auto& s = dx::voiceSource();
        s.categories = [] { return aquanode::dx7::categories(); };
        s.voices = [] (int category) { return voicesOf (category); };
        return true;
    }();

    const bool fm12RandomRegistered = aquanode::plugins::randompreset::registerSource ([]
    {
        aquanode::plugins::randompreset::Source s;
        s.pluginId = "fm12";
        s.name = "FM12";
        s.lists = []
        {
            aquanode::plugins::randompreset::Lists l;
            l.kinds.add ("Anything");
            l.kinds.addArray (aquanode::dx7::categories());
            l.kinds.add ("Layered");
            l.defaultKind = 0;
            return l;
        };
        s.roll = [] (juce::AudioProcessor& p, int kind, int, int, juce::uint32 seed) -> juce::String
        {
            auto* fm = dynamic_cast<aqp_fm12::FM12SynthAudioProcessor*> (&p);
            if (fm == nullptr)
                return {};
            const int numCats = aquanode::dx7::categories().size();
            const bool layered = kind == numCats + 1;
            const int category = (kind >= 1 && kind <= numCats) ? kind - 1 : -1;
            std::vector<dx::Voice> pool;
            for (int c = 0; c < numCats; ++c)
                if (category < 0 || c == category)
                    for (auto& v : voicesOf (c))
                        pool.push_back (v);
            if (pool.empty())
                return {};
            juce::Random rng ((juce::int64) seed);
            for (int attempt = 0; attempt < 24; ++attempt)
            {
                const auto& a = pool[(size_t) rng.nextInt ((int) pool.size())];
                if (! layered)
                {
                    if (dx::applyVoice (*fm, a)) return fm->voiceName;
                }
                else
                {
                    const auto& b = pool[(size_t) rng.nextInt ((int) pool.size())];
                    if (&a != &b && dx::applyLayer (*fm, a, b)) return fm->voiceName;
                }
            }
            return {};
        };
        s.presetExtension = "fm12preset";
        s.presetData = [] (juce::AudioProcessor& p, const juce::String&, juce::MemoryBlock& out)
        {
            p.getStateInformation (out);
            return out.getSize() > 0;
        };
        return s;
    }());
}
