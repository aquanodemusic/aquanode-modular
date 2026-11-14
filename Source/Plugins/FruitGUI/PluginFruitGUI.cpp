// Fruit DAW as a plugin card (GUI only, like AudioStretcher): an FL Studio-
// style song workspace with its own engine (FruitProcessor.h).
#include "../Host/PluginCatalog.h"
#include "FruitProcessor.h"

namespace
{
    const aquanode::plugins::HostedSpec& FruitGuiSpec()
    {
        static const aquanode::plugins::HostedSpec s = []
        {
            auto x = aquanode::plugins::baseSpec ("fruitgui");
            x.create = [] () -> juce::AudioProcessor* { return new FruitProcessor(); };
            return x;
        }();
        return s;
    }
    const bool fruitCreatorRegistered = aquanode::plugins::registerCreate ("fruitgui", [] () -> juce::AudioProcessor* { return new FruitProcessor(); });
}

AQUANODE_HOSTED_PLUGIN (FruitGuiModule, FruitGuiSpec)

// The patch version's mixer card: the same Fruit DAW, showing its mixer, with the
// Audio Out and no Midi In (the channel rack card has that).
namespace
{
    const aquanode::plugins::HostedSpec& FruitMixerSpec()
    {
        static const aquanode::plugins::HostedSpec s = []
        {
            auto x = FruitGuiSpec();
            x.typeId = "plugin.fruitgui.mixer";
            x.displayName = "Fruit DAW - Mixer";
            x.description = "The mixer of Fruit DAW's patch version, with its Audio Out. The channel rack, piano roll and playlist are cards of their own; "
                            "the four work as one and are deleted together.";
            x.midiIn = false;
            x.defaultWidth = 900; x.defaultHeight = 440;
            return x;
        }();
        return s;
    }
}
AQUANODE_HOSTED_PLUGIN (FruitMixerModule, FruitMixerSpec)
