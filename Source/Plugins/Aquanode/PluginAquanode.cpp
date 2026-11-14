// Aquanode Modular as a plugin of its own: the whole instrument - this very
// one - on one card in a patch, with its own patch, sidebar, keys, Mutator and
// Render. Audio In feeds its Audio In modules, Midi In (or the keys) plays it,
// and its patch is saved inside the card. One may hold another: the app plus
// four nested ones (AquanodeModularAudioProcessor::kMaxNestingLevel); a Fruit
// GUI's engine counts as a level too.
#include "../Host/PluginCatalog.h"
#include "../../Engine/PluginProcessor.h"

namespace
{
    juce::AudioProcessor* createNestedAquanode()
    {
        auto* p = new AquanodeModularAudioProcessor();
        // the patch being built decides how deep this one sits
        p->nestingLevel = juce::jmin (AquanodeModularAudioProcessor::kMaxNestingLevel,
                                      AquanodeModularAudioProcessor::creatingLevel() + 1);
        p->initializePatch();     // ready to play: the default patch (in the FX version, Audio In -> Audio Out)
        return p;
    }

    const aquanode::plugins::HostedSpec& AquanodeNestedSpec()
    {
        static const aquanode::plugins::HostedSpec s = []
        {
            auto x = aquanode::plugins::baseSpec ("aquanode");
            x.create = [] { return createNestedAquanode(); };
            return x;
        }();
        return s;
    }

    const bool aquanodeCreatorRegistered = aquanode::plugins::registerCreate ("aquanode", [] { return createNestedAquanode(); });
}

AQUANODE_HOSTED_PLUGIN (AquanodeNestedGuiModule, AquanodeNestedSpec)
