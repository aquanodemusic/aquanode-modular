// Reflex preset randomizer and mutator.
//
// The source material is the 384 presets of the three original banks (PresetData.h), sorted into
// sound categories. "Random <category>" picks one of those presets as a seed, crosses it with a
// second preset of the same category and spreads every sound parameter by the category's own
// variance, scaled by the Variation amount: at 0 % the result is the seed preset itself, at
// 100 % it wanders far but still inside what that category does.
//
// "Mutate" nudges the current sound and never touches pitch (semitones, pitch modulation,
// vibrato, glide, bend), polyphony, rhythm settings or volume.
#pragma once
#include "Params.h"
#include <array>
#include <random>
#include <string>
#include <vector>

namespace rfx { namespace randomizer {

using Patch = std::array<float, NumParams>;

// Menu entries in display order. Kinds tell what an entry does.
enum class Kind { Category, Anything, SuperRandom, Mutate, Blend };
struct MenuEntry { std::string name; Kind kind; int category; std::string heading; };
const std::vector<MenuEntry>& menu();

// Builds a new patch for the given menu entry. `current` is the patch being played (used by Mutate),
// `variation` is 0..1. Returns the suggested preset name in `name`.
Patch generate(int menuIndex, const Patch& current, float variation, std::mt19937& rng, std::string& name);

} }
