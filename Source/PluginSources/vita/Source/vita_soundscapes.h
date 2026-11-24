#pragma once
// Vita in Aquanode Modular: the built-in Aquanode Soundscapes - presets by Aquanode, packed small by
// Tools/VitaSoundscapes/pack_vita_soundscapes.py (VitaSoundscapesData.cpp). On load a preset is put
// back together: its wavetable frames from the shared store, its noises made again, the stand-ins
// for audio that is not shipped (noise shaped like the original, drawn tables) rendered.
#include "JuceHeader.h"
#include "json/json.h"
#include <string>
#include <vector>

namespace VitaSoundscapes {
  int count();
  std::string name(int index);
  std::string category(int index);
  std::vector<std::string> categories();
  // the preset as Vital's JSON; false (and error) if it can not be read
  bool build(int index, nlohmann::json& out, std::string& error);
  // Generate Growl: an init preset's state turned into a growl - three oscillators running the
  // Random Amplitudes morph swept by a synced LFO, a filter swept by another, levels gated, a comb
  // of flanger and delay that follows the note, and compression, EQ, drive and space
  void growl(uint32_t seed, nlohmann::json& state);
  // Generate (6.9.11): a new preset of a kind (Pads, Basses, Leads, Plucks, Keys,
  // Textures, DnB, Dub Techno, Psychill - two soundscapes of that kind crossed -
  // or Growl); kindSize: how many soundscapes it learns from
  std::vector<std::string> generatorKinds();
  int kindSize(const std::string& kind);
  bool generate(const std::string& kind, uint32_t seed, nlohmann::json& state, std::string& error);
}
