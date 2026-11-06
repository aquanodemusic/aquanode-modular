#include "PresetGenerators.h"
#include "ChordWavetableGenerator.h"
#include "DX7Generator.h"
#include "GenreGenerator.h"
#include "VaporizationPatch.h"
#include "RandomAmpGrowl.h"
#include "Acid303.h"
#include "ExtraEngines.h"
#include "PolSweepPatch.h"
#include "EffectAdders.h"
#include "CuratedPresets.h"
#include "../Engine/PluginProcessor.h"
#include <map>

namespace aquanode::presets
{

namespace
{
    //==========================================================================
    // the engines behind the categories
    //==========================================================================
    struct Engine
    {
        juce::String name;                                  // "Ambient", "Colorbass", ...
        std::function<BuildFn (juce::uint32 seed)> make;    // a random patch of the category
    };

    struct Category
    {
        juce::String name;                                  // "Chord"
        std::vector<Engine> engines;                        // A-Z
    };

    // every category with the engines that make it, A-Z (FM is its own row)
    const std::vector<Category>& categoryTable()
    {
        static const std::vector<Category> table = []
        {
            std::map<juce::String, std::vector<Engine>> byName;

            // Colorbass: one category per archetype (Chord, Growl, Pluck, Pad).
            // Its growls are the Growl row's "Spectral" kind; the "Random" kind
            // is the Warp Osc's Random Amp morph swept through every note.
            const auto archetypes = chordWavetableArchetypeNames();
            for (int a = 0; a < archetypes.size(); ++a)
                byName[archetypes[a]].push_back ({ archetypes[a] == "Growl" ? "Spectral" : "Colorbass",
                                                   [a] (juce::uint32 seed) -> BuildFn
                {
                    return [seed, a] (AquanodeModularAudioProcessor& p) { return buildChordWavetablePatch (p, seed, a); };
                } });
            byName["Growl"].push_back ({ "Random", [] (juce::uint32 seed) -> BuildFn
            {
                return [seed] (AquanodeModularAudioProcessor& p) { return buildRandomAmpGrowl (p, seed); };
            } });

            // Chord: the hand-made presets (Source/Presets/Curated), one of them per roll
            byName["Chord"].push_back ({ "Presets", [] (juce::uint32 seed) -> BuildFn
            {
                Dice dice (seed ^ 0xc0ffee5u);
                const int pick = dice.index (juce::jmax (1, curatedPresetCount()));
                return [pick] (AquanodeModularAudioProcessor& p) { return loadCuratedPreset (p, pick); };
            } });

            // 303: the acid voice
            byName["303"].push_back ({ "Acid", [] (juce::uint32 seed) -> BuildFn
            {
                return [seed] (AquanodeModularAudioProcessor& p) { return buildAcid303 (p, seed); };
            } });

            // Psytrance, Slow FM, Tidebreeze, Phizmo, Liquid DnB
            for (const auto& e : extraEngines())
            {
                auto build = e.build;
                byName[e.category].push_back ({ e.engine, [build] (juce::uint32 seed) -> BuildFn
                {
                    return [build, seed] (AquanodeModularAudioProcessor& p) { return build (p, seed); };
                } });
            }

            // the genre engines: whatever categories their recipes cover
            const auto names = genre::genreNames();
            for (int g = 0; g < names.size(); ++g)
            {
                juce::StringArray cats;
                for (const auto& e : genre::recipesOf (g))
                    cats.addIfNotAlreadyThere (e.category);
                for (const auto& cat : cats)
                    byName[cat].push_back ({ names[g], [g, cat] (juce::uint32 seed)
                    {
                        return genre::randomBuilder (g, seed, cat);
                    } });
            }

            // the Vaporization Pad is one more recipe of the Ambient pads: it
            // comes up as often as any single recipe of theirs does
            for (int g = 0; g < names.size(); ++g)
            {
                if (names[g] != "Ambient")
                    continue;
                int padRecipes = 0;
                for (const auto& e : genre::recipesOf (g))
                    if (e.category == "Pad")
                        ++padRecipes;
                for (auto& e : byName["Pad"])
                    if (e.name == "Ambient")
                    {
                        auto inner = e.make;
                        e.make = [inner, padRecipes] (juce::uint32 seed) -> BuildFn
                        {
                            Dice dice (seed ^ 0x7a9e5u);
                            if (dice.index (padRecipes + 1) == 0)
                                return [] (AquanodeModularAudioProcessor& p) { return buildVaporizationPatch (p); };
                            return inner (seed);
                        };
                    }
            }

            std::vector<Category> out;
            for (auto& [name, engines] : byName)
            {
                // engines of one name in a row (the Psychill genre recipes and
                // the Psychill squelch patches) are one engine there: a tap
                // on the row picks either of them
                std::vector<Engine> merged;
                for (auto& e : engines)
                {
                    auto same = std::find_if (merged.begin(), merged.end(), [&e] (const Engine& m) { return m.name == e.name; });
                    if (same == merged.end())
                    {
                        merged.push_back (e);
                        continue;
                    }
                    auto a = same->make, b = e.make;   // (two at most: an even share)
                    same->make = [a, b] (juce::uint32 seed) -> BuildFn
                    {
                        Dice dice (seed ^ 0x6e3a91u);
                        return dice.chance (0.5f) ? a (seed) : b (seed);
                    };
                }
                engines = std::move (merged);

                // Pol Sweep (the swept chord, tuned kick and ticks that play by
                // themselves on four Piano Rolls) turns up now and then in the
                // Synth row: about one tap in ten, whichever engine it lands on
                if (name == "Synth")
                    for (auto& e : engines)
                    {
                        auto inner = e.make;
                        e.make = [inner] (juce::uint32 seed) -> BuildFn
                        {
                            Dice dice (seed ^ 0x9015eedu);
                            if (dice.chance (0.1f))
                                return [seed] (AquanodeModularAudioProcessor& p) { return buildPolSweepPatch (p, seed); };
                            return inner (seed);
                        };
                    }

                std::sort (engines.begin(), engines.end(), [] (const Engine& a, const Engine& b)
                           { return a.name.compareNatural (b.name) < 0; });
                out.push_back ({ name, std::move (engines) });
            }
            std::sort (out.begin(), out.end(), [] (const Category& a, const Category& b)
                       { return a.name.compareNatural (b.name) < 0; });
            return out;
        }();
        return table;
    }

    // the ticked engine of each row (category name -> engine name; absent = all)
    // for this session
    std::map<juce::String, juce::String>& ticked()
    {
        static std::map<juce::String, juce::String> t;
        return t;
    }

    juce::String tickedOf (const juce::String& row)
    {
        const auto it = ticked().find (row);
        return it != ticked().end() ? it->second : juce::String();
    }

    void toggleTick (const juce::String& row, const juce::String& item)
    {
        if (tickedOf (row) == item)
            ticked().erase (row);
        else
            ticked()[row] = item;
    }

    // one tick item per choice: picking the ticked one unticks it
    void addTickItems (juce::PopupMenu& menu, const juce::String& row, const juce::StringArray& items,
                       const MenuContext& ctx)
    {
        const auto current = tickedOf (row);
        const auto changed = ctx.selectionChanged;
        for (const auto& item : items)
            menu.addItem (item, true, item == current, [row, item, changed]
            {
                toggleTick (row, item);
                if (changed != nullptr)
                    changed();
            });
    }

    BuildFn categoryBuilder (const Category& cat, juce::uint32 seed, const juce::String& engineName)
    {
        if (cat.engines.empty())
            return {};

        for (const auto& e : cat.engines)
            if (e.name == engineName)
                return e.make (seed);

        Dice dice (seed);
        const auto& e = cat.engines[(size_t) dice.index ((int) cat.engines.size())];
        return e.make (seed * 2246822519u + 3u);
    }

    Generator makeCategory (const Category& cat)
    {
        Generator g;
        g.id = "gen." + cat.name;
        g.label = cat.name;
        g.tooltipTitle = cat.name;

        juce::StringArray engineNames;
        for (const auto& e : cat.engines)
            engineNames.add (e.name);

        const auto name = cat.name;
        const auto lower = name == "FX" ? juce::String ("effect") : name.toLowerCase();
        if (name == "303")
            g.tooltipBody = "Clears the patch and builds a new acid patch with every tap, a third of the time each: "
                            "the Warp Osc voice into a resonant Ladder Filter, the 303 module played from the keys, or "
                            "the 303 playing a hook of its own on its grid (a pentatonic arp, octave steps or an acid "
                            "line). Then drive and delay; some come with an Arp in front.";
        else if (name == "Growl")
            g.tooltipBody = "Clears the patch and builds a new growl with every tap, of either kind: Spectral "
                            "(Colorbass chord wavetables with their spectral morph, formant and comb) or Random "
                            "(Warp Osc layers whose Random Amp morph is swept through every note).";
        else if (engineNames.size() > 1)
            g.tooltipBody = "Clears the patch and builds a new " + lower + " sound with every tap, from one of the "
                            "engines that make " + lower + " sounds.";
        else
            g.tooltipBody = "Clears the patch and builds a new " + lower + " sound with every tap ("
                          + engineNames.joinIntoString (", ") + ").";

        // every roll draws from all of the category's engines (no engine
        // sub-menus any more), and is named "Random <Category>"
        g.random = [name] (juce::uint32 seed) -> BuildFn
        {
            for (const auto& c : categoryTable())
                if (c.name == name)
                {
                    auto build = categoryBuilder (c, seed, {});
                    if (build == nullptr)
                        return build;
                    const bool growl = name == "Growl";
                    return [build, seed, name, growl] (AquanodeModularAudioProcessor& p)
                    {
                        build (p);
                        // a Growl ends in a Spectral Lock one time in four, tuned to
                        // the major or the minor pentatonic
                        if (growl)
                        {
                            Dice dice (seed ^ 0x5bd1e995u);
                            if (dice.chance (0.25f))
                                addSpectralLockEnding (p, seed ^ 0x27d4eb2fu, dice.chance (0.5f) ? 8 : 9);
                        }
                        return juce::String ("Random ") + name;
                    };
                }
            return {};
        };

        return g;
    }

    //==========================================================================
    // FM: the built-in voices by sub-category, plus loaded .syx files
    //==========================================================================
    const juce::String fmRow { "FM" };

    // a loaded file's voices, numbered by their slot in the file
    juce::PopupMenu fileMenu (const dx7::Bank& bank, std::function<void (BuildFn)> apply)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < bank.voices.size(); ++i)
        {
            const auto voice = bank.voices[i];
            const auto label = voice.name.trim().isNotEmpty() ? voice.name.trim() : bank.title;
            sub.addItem (juce::String ((int) i + 1).paddedLeft ('0', 2) + "  " + voice.name, [apply, voice, label]
            {
                apply ([voice, label] (AquanodeModularAudioProcessor& p) { return dx7::buildVoicePatch (p, voice, label); });
            });
        }
        return sub;
    }

    // files loaded this session, newest first
    std::vector<dx7::Bank>& loadedFiles()
    {
        static std::vector<dx7::Bank> files;
        return files;
    }

    BuildFn fmBuilder (juce::uint32 seed, const juce::String& subCategory)
    {
        return dx7::randomVoiceBuilder (seed, dx7::categories().indexOf (subCategory));
    }

    Generator makeFM()
    {
        Generator g;
        g.id = "gen.FM";
        g.label = fmRow;
        g.tooltipTitle = "FM";
        g.tooltipBody = "Clears the patch and builds a 6-operator FM voice from modules with every tap: six DX "
                        "Operators wired to the voice's algorithm (laid out as the algorithm chart, operators "
                        "folded - tap a card's top tab to open it), a DX Pitch EG and a DX LFO for the pitch "
                        "envelope and vibrato, and the mod wheel on the vibrato. The arrow on the right ticks a "
                        "sub-category (" + dx7::categories().joinIntoString (", ") + "; tick it again for all), "
                        "or loads a DX7 .syx file and lists its voices.";

        g.random = [] (juce::uint32 seed) -> BuildFn
        {
            auto build = fmBuilder (seed, tickedOf (fmRow));
            if (build == nullptr)
                return build;
            return [build] (AquanodeModularAudioProcessor& p) { build (p); return juce::String ("Random FM"); };
        };
        g.selection = [] { return tickedOf (fmRow); };

        g.fillMenu = [] (juce::PopupMenu& menu, const MenuContext& ctx)
        {
            menu.addSectionHeader ("FM");
            addTickItems (menu, fmRow, dx7::categories(), ctx);

            const auto apply = ctx.apply;
            if (! loadedFiles().empty())
            {
                menu.addSectionHeader ("Loaded");
                for (const auto& bank : loadedFiles())
                    menu.addSubMenu (bank.title, fileMenu (bank, apply));
            }

            menu.addSeparator();
            const auto pickFile = ctx.pickFile;
            juce::Component::SafePointer<juce::Component> anchor (ctx.anchor);
            menu.addItem ("Load DX7 SysEx (.syx)...", [pickFile, apply, anchor]
            {
                if (pickFile == nullptr)
                    return;
                pickFile ("Load DX7 SysEx", "*.syx;*.SYX", [apply, anchor] (juce::MemoryBlock data, juce::String name)
                {
                    auto bank = dx7::parseSysex (data, name.isNotEmpty() ? name : juce::String ("SysEx"));
                    if (bank.voices.empty())
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "FM",
                                                                "No DX7 voices found in that file.");
                        return;
                    }

                    // remember it for later menus, then offer its voices right away
                    auto& files = loadedFiles();
                    files.erase (std::remove_if (files.begin(), files.end(),
                                                 [&bank] (const dx7::Bank& b) { return b.title == bank.title; }),
                                 files.end());
                    files.insert (files.begin(), bank);
                    if (files.size() > 8)
                        files.pop_back();

                    auto menuOfFile = fileMenu (bank, apply);
                    auto opts = juce::PopupMenu::Options();
                    if (anchor != nullptr)
                        opts = opts.withTargetComponent (anchor.getComponent());
                    menuOfFile.showMenuAsync (opts);
                });
            });
        };
        return g;
    }

    //==========================================================================
    // DAW: an empty two-track arrangement to fill in - two Piano Rolls, each
    // playing a sine Oscillator, one through a Delay, one through a Reverb,
    // added together. One of two layouts, as often as the other:
    //   - Sends first: each Oscillator into a Reroute Send of its own bus, its
    //     Return through the track's effect into an Audio Out of its own (a
    //     mixer's channel strips: the effects sit by the outputs)
    //   - Effects first: each Oscillator through its effect into a Send on one
    //     shared bus, whose Return goes to the one Audio Out
    // Both play the same: the buses add up exactly, and Soft Clip acts on the
    // patch's whole output, whichever Audio Out it comes from.
    //==========================================================================
    void setChoice (AquanodeModularAudioProcessor& p, int id, const juce::String& param, const juce::String& text)
    {
        if (auto* inst = p.getInstance (id))
            if (const auto* spec = inst->descriptor().findParam (param))
                if (const int i = spec->choices.indexOf (text); i >= 0)
                    inst->dsp->setParameter (param, (float) i);
    }

    juce::String buildDaw (AquanodeModularAudioProcessor& p, bool sendsFirst)
    {
        auto roll = [&p] (juce::Point<int> at, float bars)
        {
            const int id = p.addModule ("util.pianoroll", at);
            auto* d = p.getInstance (id)->dsp.get();
            d->setParameter ("tempo", 120.0f);
            d->setParameter ("bars", bars);
            d->setParameter ("playing", 0.0f);
            setChoice (p, id, "sync", "Own Tempo");
            setChoice (p, id, "loop", "Loop");
            return id;
        };
        auto osc = [&p] (juce::Point<int> at)
        {
            const int id = p.addModule ("osc.basic", at);
            p.getInstance (id)->dsp->setParameter ("volume", 0.8f);
            setChoice (p, id, "waveform", "Sine");
            return id;
        };
        auto reroute = [&p] (const char* type, juce::Point<int> at, const char* bus)
        {
            const int id = p.addModule (type, at);
            setChoice (p, id, "bus", bus);
            return id;
        };

        const int roll1 = roll ({ -409, 97 }, 4.0f), roll2 = roll ({ -412, 518 }, 256.0f);
        const int osc1 = osc ({ 136, 93 });
        p.addCable (roll1, "midiOut", osc1, "addMidiIn");

        if (sendsFirst)
        {
            const int osc2 = osc ({ 129, 516 });
            p.addCable (roll2, "midiOut", osc2, "addMidiIn");
            const int send1 = reroute ("util.reroutesend", { 403, 98 }, "Bus 1");
            const int send2 = reroute ("util.reroutesend", { 395, 518 }, "Bus 2");
            const int ret1 = reroute ("util.reroutereturn", { 776, 819 }, "Bus 1");
            const int ret2 = reroute ("util.reroutereturn", { 1101, 823 }, "Bus 2");
            const int delay = p.addModule ("fx.delay", { 777, 1012 });
            const int reverb = p.addModule ("fx.reverb", { 1102, 1014 });
            const int out1 = p.addModule ("io.audioOut", { 775, 1212 });
            const int out2 = p.addModule ("io.audioOut", { 1099, 1210 });
            p.addCable (osc1, "audioOut", send1, "audioIn");
            p.addCable (osc2, "audioOut", send2, "audioIn");
            p.addCable (ret1, "audioOut", delay, "audioIn");
            p.addCable (ret2, "audioOut", reverb, "audioIn");
            p.addCable (delay, "audioOut", out1, "audioIn");
            p.addCable (reverb, "audioOut", out2, "audioIn");
        }
        else
        {
            const int osc2 = osc ({ 132, 512 });
            p.addCable (roll2, "midiOut", osc2, "addMidiIn");
            const int delay = p.addModule ("fx.delay", { 421, 90 });
            const int reverb = p.addModule ("fx.reverb", { 419, 516 });
            const int send1 = reroute ("util.reroutesend", { 703, 95 }, "Bus 1");
            const int send2 = reroute ("util.reroutesend", { 697, 517 }, "Bus 1");
            const int ret = reroute ("util.reroutereturn", { -708, 542 }, "Bus 1");
            const int out = p.addModule ("io.audioOut", { -709, 721 });
            p.addCable (osc1, "audioOut", delay, "audioIn");
            p.addCable (osc2, "audioOut", reverb, "audioIn");
            p.addCable (delay, "audioOut", send1, "audioIn");
            p.addCable (reverb, "audioOut", send2, "audioIn");
            p.addCable (ret, "audioOut", out, "audioIn");
        }

        // a new arrangement to build on: its Oscillators at today's levels, like
        // ones added by hand (not the full level the other generators keep)
        p.generatedKeepsTrims = true;
        return "DAW";
    }

    Generator makeDaw()
    {
        Generator g;
        g.id = "gen.DAW";
        g.label = "DAW";
        g.tooltipTitle = "DAW";
        g.tooltipBody = "Clears the patch and builds an empty two-track arrangement to draw into: two Piano Rolls (4 "
                        "and 256 bars), each playing a sine Oscillator, one through a Delay and one through a Reverb. "
                        "Half the time the Oscillators go through Reroute buses to the effects by two Audio Outs, "
                        "half the time through their effects onto one bus and one Audio Out - both play the same. "
                        "Draw notes, swap the Oscillators for any sound, and Render the result.";
        g.random = [] (juce::uint32 seed) -> BuildFn
        {
            Dice dice (seed);
            const bool sendsFirst = dice.chance (0.5f);
            return [sendsFirst] (AquanodeModularAudioProcessor& p) { return buildDaw (p, sendsFirst); };
        };
        return g;
    }

    //==========================================================================
    // Random: any category, any engine (the ticks are ignored)
    //==========================================================================
    Generator makeRandom()
    {
        Generator g;
        g.id = "gen.random";
        g.label = "Random";
        g.tooltipTitle = "Random";
        g.tooltipBody = "Clears the patch and builds a completely random one with every tap: any category, "
                        "any engine, FM included.";

        g.random = [] (juce::uint32 seed) -> BuildFn
        {
            const auto& table = categoryTable();
            Dice dice (seed);
            const int pick = dice.index ((int) table.size() + 1);   // + 1 = FM
            const auto next = seed * 2654435761u + 7u;
            const bool fm = pick == (int) table.size();
            auto build = fm ? fmBuilder (next, {}) : categoryBuilder (table[(size_t) pick], next, {});
            if (build == nullptr)
                return build;
            const juce::String type = fm ? juce::String ("FM") : table[(size_t) pick].name;
            return [build, type] (AquanodeModularAudioProcessor& p) { build (p); return "Random " + type; };
        };
        return g;
    }
}

const std::vector<Generator>& generators()
{
    static const std::vector<Generator> list = []
    {
        std::vector<Generator> v;
        for (const auto& cat : categoryTable())
            v.push_back (makeCategory (cat));
        v.push_back (makeFM());
        v.push_back (makeDaw());   // a template, not a sound: Random never picks it

        std::sort (v.begin(), v.end(), [] (const Generator& a, const Generator& b)
                   { return a.label.compareNatural (b.label) < 0; });

        v.push_back (makeRandom());   // always last
        return v;
    }();
    return list;
}

const Generator* findGenerator (const juce::String& id)
{
    for (const auto& g : generators())
        if (g.id == id)
            return &g;
    return nullptr;
}

} // namespace aquanode::presets
