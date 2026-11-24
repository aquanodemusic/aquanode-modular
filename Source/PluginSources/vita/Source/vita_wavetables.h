#pragma once
// Vita in Aquanode Modular (6.9.12): wavetables of its own, without any files.
//  - the Aquanode Tables: a default set, made by the generator below with fixed settings
//  - the Massif Tables: Aqua Massif's 87 table names - the user's own table file for one where
//    Massif would find it (its folders), else Massif's own stand-in for it
//  - Generate: a new table of a kind each time (a sweep, so the frame knob always has somewhere to go)
// Everything is computed here (Massif's table recipes and some of Vita's own); no table data is shipped.

#include "JuceHeader.h"
#include "json/json.h"

#include <string>
#include <vector>

using json = nlohmann::json;

namespace VitaWavetables {
  // the generator's kinds, and a new table of one (seed: which one)
  const std::vector<std::string>& generatorKinds();
  json generate(int kind, uint32_t seed);

  // the default set
  const std::vector<std::string>& defaultNames();
  json defaultTable(int index);

  // Massif's tables: a file of the user's for it (or File()), else Massif's stand-in
  int massifCount();
  std::string massifName(int slot);
  File massifFile(int slot);
  json massifStandIn(int slot);

  // frames of 2048 samples into a table Vita loads (a wave source of keyframes across the frames)
  json fromFrames(const std::string& name, const std::vector<float>& frames);

  // (6.9.15) the same tables as 2048-sample frames (raw; prepareFrames evens them out)
  std::vector<float> generateFrames(int kind, uint32_t seed, std::string& name);
  std::vector<float> defaultFrames(int index, std::string& name);
  std::vector<float> massifFrames(int slot);
  std::vector<float> prepareFrames(const std::vector<float>& frames);
}
