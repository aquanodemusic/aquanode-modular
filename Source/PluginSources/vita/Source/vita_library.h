#pragma once
// Vita in Aquanode Modular: what Vita knows about besides its current sound - the presets (and a bank's
// wavetables, LFOs and samples) that were loaded into it. Vita reads no folders and writes no files of
// its own accord: things come in through Open Preset / Import Bank and go out through Save Preset /
// Export Bank (as .vital and .vitalbank files, the same as Vital's), and the library is kept in the
// Aquanode Modular patch when keepInPatch() is on.
//
// The presets in the library can be edited: switching to another one keeps the edits of the one left
// (they go into its entry), so an exported bank holds every preset as it was last heard.

#include "JuceHeader.h"
#include "json/json.h"

#include <string>
#include <vector>

using json = nlohmann::json;

class VitaLibrary {
  public:
    enum Kind { kPreset, kWavetable, kLfo, kSample, kNumKinds };

    struct Entry {
      std::string name;        // the file's name, without extension
      std::string bank;        // the bank it came from; empty for a single file
      std::string folder;      // its sub-folder inside the bank's Presets / Wavetables / ... folder
      std::string extension;   // the file's extension, without the dot ("vital", "vitaltable", "wav", ...)
      json data;               // preset / wavetable / LFO
      MemoryBlock audio;       // the bytes of an audio file (a sample, an audio wavetable)
      bool edited = false;     // a preset changed since it was loaded or saved

      bool isAudio() const { return audio.getSize() > 0; }
      std::string displayName() const { return bank.empty() ? name : bank + " / " + name; }
    };

    VitaLibrary() = default;

    std::vector<Entry>& entries(Kind kind) { return entries_[kind]; }
    const std::vector<Entry>& entries(Kind kind) const { return entries_[kind]; }
    int size(Kind kind) const { return (int)entries_[kind].size(); }
    bool empty() const;
    void clear();

    // adds an entry (replacing one of the same kind, bank, folder and name); its index
    int add(Kind kind, Entry entry);
    void remove(Kind kind, int index);
    int find(Kind kind, const std::string& name) const;
    // the entry after (delta 1) or before (delta -1) the one called name, wrapping; -1 if none
    int neighbour(Kind kind, const std::string& name, int delta) const;

    // files: one item of the kind (by extension) - true if understood
    static bool kindForExtension(const String& extension, Kind& kind);
    bool addFile(const File& file, int& index, Kind& kind);

    // a .vitalbank: everything in it comes into the library. The number of entries added.
    int importBank(const File& file, std::string& bank_name, std::string& error);
    // writes the library (all kinds) as a .vitalbank whose folder is called bank_name
    bool exportBank(const File& file, const std::string& bank_name, std::string& error) const;

    // the preset playing now (an index into entries(kPreset), -1 for one not in the library)
    int currentPreset() const { return current_preset_; }
    void setCurrentPreset(int index) { current_preset_ = index; }

    bool keepInPatch() const { return keep_in_patch_; }
    void setKeepInPatch(bool keep) { keep_in_patch_ = keep; }
    // about how much the library adds to a patch, in bytes
    size_t approximateSize() const;

    json toJson() const;
    void fromJson(const json& data);

    static const char* folderName(Kind kind);

  private:
    std::vector<Entry> entries_[kNumKinds];
    int current_preset_ = -1;
    bool keep_in_patch_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VitaLibrary)
};
