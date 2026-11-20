#pragma once
//==============================================================================
// Cove Groove - the physically modelled kits (Aquanode's Shared/PhysDrumSynth.h:
// the TR-808 and TR-909 fitted to recordings of the machines, the orchestral
// physical kit, and the struck-body kits fitted to recorded hits).
//
// Every lane has one sound per kit (its Sound menu: 808, 909, Classic Kit ...),
// and the tweak row above the steps picks any of the kit's sounds for the lane
// (Model) and moves the model itself (three knobs named for what they do).
// A sound is rendered whole, off the audio thread, whenever the lane's Tune,
// Decay, Tone, Model or model knobs change - three round-robin variations -
// and the hits play those renders back.
//==============================================================================
#include "../../Modules/Shared/PhysDrumPresets.h"
#include <array>
#include <string>
#include <vector>

namespace covegroove
{
    constexpr int kPhysModels = 8;    // per lane, after the drum machines

    namespace phys
    {
        namespace pd = aquanode::physdrums;

        struct KitDef
        {
            const char* name;                 // in the lane's Sound menu
            const char* prefixes[2];          // its presets: keys starting with these
            const char* laneDefault[8];       // Kick, Bass, Clap, Closed Hat, Open Hat, Cymbal, Perc, FX
        };

        inline const KitDef& kit (int k)
        {
            static const KitDef kits[kPhysModels] = {
                { "808",         { "808/", nullptr },      { "808/BD2550", "808/BD2575", "808/CP", "808/CH", "808/OH25", "808/CY5050", "808/MC50", "808/CB" } },
                { "909",         { "909/", nullptr },      { "909/bd01", "909/bd05", "909/cp01", "909/hh01", "909/oh01", "909/rd01", "909/lt01", "909/rs01" } },
                { "Classic Kit", { "classic/", nullptr },  { "classic/01", "classic/00", "classic/10", "classic/06", "classic/08", "classic/09", "classic/04", "classic/05" } },
                { "Electro Kit", { "electro/", nullptr },  { "electro/00", "electro/01", "electro/06", "electro/05", "electro/07", "electro/09", "electro/04", "electro/08" } },
                { "Large Drums", { "large/", nullptr },    { "large/03", "large/01", "large/04", "large/08", "large/07", "large/00", "large/06", "large/05" } },
                { "Metal Kit",   { "metal/", nullptr },    { "metal/03", "metal/00", "metal/02", "metal/07", "metal/06", "metal/08", "metal/01", "metal/04" } },
                { "Orchestral",  { "orch/", "orchperc/" }, { "orch/kick", "orch/tom_floor", "orch/snare", "orch/hat", "orch/hat_open", "orch/crash", "orch/conga", "orchperc/04" } },
                { "World Kit",   { "world/", nullptr },    { "world/01", "world/03", "world/07", "world/08", "world/09", "world/11", "world/05", "world/10" } } };
            return kits[std::clamp (k, 0, kPhysModels - 1)];
        }

        // the kit's sounds, in the order of the Model menu
        inline const std::vector<const pd::Preset*>& kitPresets (int k)
        {
            static const std::array<std::vector<const pd::Preset*>, kPhysModels> lists = []
            {
                std::array<std::vector<const pd::Preset*>, kPhysModels> out;
                for (int i = 0; i < kPhysModels; ++i)
                    for (const char* pre : kit (i).prefixes)
                        if (pre != nullptr)
                            for (const auto& p : pd::allPresets())
                                if (std::string (p.key).rfind (pre, 0) == 0) out[(size_t) i].push_back (&p);
                return out;
            }();
            return lists[(size_t) std::clamp (k, 0, kPhysModels - 1)];
        }

        // the lane's sound: the Model choice (0: the lane's own default) of kit k
        inline const pd::Preset* presetFor (int k, int lane, int choice)
        {
            const auto& list = kitPresets (k);
            if (choice > 0 && choice <= (int) list.size()) return list[(size_t) choice - 1];
            return pd::findPreset (kit (k).laneDefault[std::clamp (lane, 0, 7)]);
        }

        // what the Model menu shows for a preset ("808 Bass Drum 25/50" -> "Bass Drum 25/50")
        inline std::string shortName (const pd::Preset& p, int k)
        {
            std::string n (p.name);
            for (const char* cut : { "808 ", "909 ", "Classic ", "Electro ", "Large Drums ", "Metal ", "Orchestral ", "World " })
                if (n.rfind (cut, 0) == 0 && n.size() > std::string (cut).size()) { n = n.substr (std::string (cut).size()); break; }
            (void) k;
            return n;
        }

        inline std::array<std::string, 3> knobNames (const pd::Preset* p)
        {
            std::array<std::string, 3> out;
            if (p == nullptr) return out;
            const auto& kn = pd::physicalKnobs (p->voice);
            for (int i = 0; i < 3; ++i) out[(size_t) i] = kn[(size_t) i].param[0] != 0 ? kn[(size_t) i].label : "";
            return out;
        }

        // one lane's settings, as the render needs them
        struct Spec
        {
            int kit { 0 }, lane { 0 }, choice { 0 };
            float tune { 0.0f }, decay { 1.0f }, tone { 0.5f };
            float extra[3] { 0.0f, 0.0f, 0.0f };
            double sr { 48000.0 };
            bool operator== (const Spec& o) const
            {
                return kit == o.kit && lane == o.lane && choice == o.choice && tune == o.tune && decay == o.decay && tone == o.tone
                    && extra[0] == o.extra[0] && extra[1] == o.extra[1] && extra[2] == o.extra[2] && sr == o.sr;
            }
            bool operator!= (const Spec& o) const { return ! (*this == o); }
        };
    }
}
