#pragma once

#include <JuceHeader.h>

//==============================================================================
// The plugins the Random Plugin Preset module can roll a preset in: each one's own
// generator (or, for VirtualDX7, the built-in FM voices), registered by the
// plugin's translation unit (Plugin<Name>.cpp) during static initialisation.
// The lists are made when they are first asked for, not at registration.
//==============================================================================
namespace aquanode::plugins::randompreset
{
    struct Lists
    {
        juce::StringArray kinds;           // the plugin's own names for its kinds of preset
        juce::StringArray kindHeadings;    // a heading before kind i, or "" (may be shorter than kinds)
        int defaultKind { 0 };             // its "Anything"
        juce::StringArray genres;          // empty: the plugin has no genres (the dropdown shows "-")
        int defaultGenre { 0 };
        juce::StringArray engines;         // "Native", "Original" where a plugin has both; empty: "-"
        int defaultEngine { 0 };
    };

    struct Source
    {
        juce::String pluginId;             // catalog id, "aquamassif"
        juce::String name;                 // "Massif"
        std::function<Lists()> lists;
        // rolls a preset of that kind (and genre, in that engine) into the plugin -
        // message thread, the plugin may be playing; returns the preset's name
        std::function<juce::String (juce::AudioProcessor&, int kind, int genre, int engine, juce::uint32 seed)> roll;

        // the plugin's sound as the plugin's own preset file (presetExtension,
        // "massif" ...), named name where the format has a name in it
        juce::String presetExtension;
        std::function<bool (juce::AudioProcessor&, const juce::String& name, juce::MemoryBlock&)> presetData;
    };

    // for the formats a plugin only writes to a file: written to a temporary
    // file, read back and the file deleted again
    inline bool viaTempFile (const juce::String& extension, const std::function<bool (const juce::File&)>& write,
                             juce::MemoryBlock& out)
    {
        const auto tmp = juce::File::createTempFile ("." + extension);
        const bool ok = write (tmp) && tmp.loadFileAsData (out) && out.getSize() > 0;
        tmp.deleteFile();
        return ok;
    }

    // what the Random Plugin Preset module shows the rest of the app: the
    // plugin playing, for Open in Plugin
    struct PluginPresetHolder
    {
        virtual ~PluginPresetHolder() = default;
        virtual juce::String heldPluginId() const = 0;
        virtual juce::AudioProcessor* heldPlugin() const = 0;
    };

    inline std::vector<Source>& registry()
    {
        static std::vector<Source> r;
        return r;
    }

    inline bool registerSource (Source s)
    {
        registry().push_back (std::move (s));
        return true;
    }

    // A-Z by name
    inline std::vector<const Source*> sources()
    {
        std::vector<const Source*> v;
        for (auto& s : registry())
            v.push_back (&s);
        std::sort (v.begin(), v.end(), [] (const Source* a, const Source* b)
                   { return a->name.compareIgnoreCase (b->name) < 0; });
        return v;
    }

    inline const Source* find (const juce::String& pluginId)
    {
        for (auto& s : registry())
            if (s.pluginId == pluginId)
                return &s;
        return nullptr;
    }

    inline const Lists& listsOf (const Source& s)
    {
        // made once per plugin, on the message thread
        static std::map<juce::String, Lists> made;
        auto it = made.find (s.pluginId);
        if (it == made.end())
            it = made.emplace (s.pluginId, s.lists != nullptr ? s.lists() : Lists {}).first;
        return it->second;
    }
}
