#pragma once

#include <JuceHeader.h>
#include "PresetCommon.h"

// The Preset Generation section at the bottom of the sidebar: one row per
// sound category (Arp, Bass, Bell, Chord, FM, ...), A-Z, and a Random row at
// the end. Tapping a row builds a fresh patch of that category (a new random
// one with every tap), drawn from every engine that makes that category
// (Ambient, Colorbass, DnB, Dub Techno, Psychill ...) - there is no engine
// menu - and named "Random <Category>". Only the FM row keeps its arrow: its
// menu ticks an FM sub-category and loads DX7 .syx files.
//
// Every generator REPLACES the whole patch, like Initialize Patch does, and
// works in the instrument and the FX build alike.
namespace aquanode::presets
{
    // What a generator's menu needs from the editor besides building a patch:
    // a file picker (the FM row reads .syx files), a call to redraw the
    // sidebar when the ticked engine changes, and a component to anchor
    // follow-up menus on.
    struct MenuContext
    {
        std::function<void (BuildFn)> apply;

        // opens the platform's file chooser (SAF on Android) and hands back
        // the file's bytes plus its name, or nothing if cancelled
        std::function<void (const juce::String& title, const juce::String& patterns,
                            std::function<void (juce::MemoryBlock data, juce::String name)>)> pickFile;

        std::function<void()> selectionChanged;

        juce::Component* anchor { nullptr };
    };

    struct Generator
    {
        juce::String id;            // "gen.Chord" - reported by the sidebar row
        juce::String label;         // "Chord"
        juce::String tooltipTitle;
        juce::String tooltipBody;

        // a new random patch; the seed decides everything
        std::function<BuildFn (juce::uint32 seed)> random;

        // fills the row's menu; empty for rows without one (no arrow)
        std::function<void (juce::PopupMenu&, const MenuContext&)> fillMenu;

        // the ticked engine / sub-category, shown small on the row; empty = all
        std::function<juce::String()> selection;

        bool hasMenu() const { return fillMenu != nullptr; }
    };

    const std::vector<Generator>& generators();
    const Generator* findGenerator (const juce::String& id);
}
