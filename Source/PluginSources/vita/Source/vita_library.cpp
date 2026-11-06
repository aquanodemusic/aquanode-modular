#include "vita_library.h"

#include "synth_constants.h"

#include <set>

namespace {
  const char* kFolderNames[VitaLibrary::kNumKinds] = { "Presets", "Wavetables", "LFOs", "Samples" };

  bool isAudioExtension(String extension) {
    extension = extension.trimCharactersAtStart(".").toLowerCase();
    return extension == "wav" || extension == "flac" || extension == "aif" || extension == "aiff" ||
           extension == "ogg" || extension == "mp3";
  }

  json parseJson(const String& text, bool& ok) {
    json parsed = json::parse(text.toStdString(), nullptr, false);
    ok = !parsed.is_discarded();
    return parsed;
  }
}

const char* VitaLibrary::folderName(Kind kind) {
  return kFolderNames[kind];
}

bool VitaLibrary::empty() const {
  for (const auto& kind : entries_) {
    if (!kind.empty())
      return false;
  }
  return true;
}

void VitaLibrary::clear() {
  for (auto& kind : entries_)
    kind.clear();
  current_preset_ = -1;
}

int VitaLibrary::add(Kind kind, Entry entry) {
  std::vector<Entry>& list = entries_[kind];
  for (int i = 0; i < (int)list.size(); ++i) {
    if (list[i].name == entry.name && list[i].bank == entry.bank && list[i].folder == entry.folder) {
      list[i] = std::move(entry);
      return i;
    }
  }
  list.push_back(std::move(entry));
  return (int)list.size() - 1;
}

void VitaLibrary::remove(Kind kind, int index) {
  std::vector<Entry>& list = entries_[kind];
  if (index < 0 || index >= (int)list.size())
    return;
  list.erase(list.begin() + index);
  if (kind == kPreset) {
    if (current_preset_ == index)
      current_preset_ = -1;
    else if (current_preset_ > index)
      current_preset_--;
  }
}

int VitaLibrary::find(Kind kind, const std::string& name) const {
  const std::vector<Entry>& list = entries_[kind];
  for (int i = 0; i < (int)list.size(); ++i) {
    if (list[i].name == name || list[i].displayName() == name)
      return i;
  }
  return -1;
}

int VitaLibrary::neighbour(Kind kind, const std::string& name, int delta) const {
  int count = size(kind);
  if (count == 0)
    return -1;
  int index = find(kind, name);
  if (index < 0)
    return delta > 0 ? 0 : count - 1;
  return (index + delta + count) % count;
}

bool VitaLibrary::kindForExtension(const String& extension, Kind& kind) {
  String ext = extension.trimCharactersAtStart(".").toLowerCase();
  if (ext == String(vital::kPresetExtension))
    kind = kPreset;
  else if (ext == String(vital::kWavetableExtension))
    kind = kWavetable;
  else if (ext == String(vital::kLfoExtension))
    kind = kLfo;
  else if (isAudioExtension(ext))
    kind = kSample;
  else
    return false;
  return true;
}

bool VitaLibrary::addFile(const File& file, int& index, Kind& kind) {
  Kind file_kind;
  if (!kindForExtension(file.getFileExtension(), file_kind))
    return false;
  if (file_kind == kSample && kind == kWavetable)
    file_kind = kWavetable;   // audio asked for as a wavetable

  Entry entry;
  entry.name = file.getFileNameWithoutExtension().toStdString();
  entry.extension = file.getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString();
  if (isAudioExtension(file.getFileExtension())) {
    if (!file.loadFileAsData(entry.audio) || entry.audio.getSize() == 0)
      return false;
  }
  else {
    bool ok = false;
    entry.data = parseJson(file.loadFileAsString(), ok);
    if (!ok)
      return false;
  }

  kind = file_kind;
  index = add(kind, std::move(entry));
  return true;
}

int VitaLibrary::importBank(const File& file, std::string& bank_name, std::string& error) {
  FileInputStream input(file);
  if (!input.openedOk()) {
    error = "The bank could not be opened.";
    return 0;
  }

  ZipFile zip(input);
  bank_name = file.getFileNameWithoutExtension().toStdString();
  int added = 0;
  for (int i = 0; i < zip.getNumEntries(); ++i) {
    const ZipFile::ZipEntry* zip_entry = zip.getEntry(i);
    if (zip_entry == nullptr || zip_entry->filename.endsWithChar('/'))
      continue;

    StringArray parts;
    parts.addTokens(zip_entry->filename.replaceCharacter('\\', '/'), "/", "");
    parts.removeEmptyStrings();
    if (parts.isEmpty())
      continue;

    // <bank>/<Presets|Wavetables|LFOs|Samples>/<folders...>/<file>
    String file_name = parts[parts.size() - 1];
    String extension = file_name.fromLastOccurrenceOf(".", false, false);
    Kind kind;
    if (!kindForExtension(extension, kind))
      continue;

    int kind_folder = -1;
    for (int p = 0; p < parts.size() - 1 && kind_folder < 0; ++p) {
      for (int k = 0; k < kNumKinds; ++k) {
        if (parts[p].equalsIgnoreCase(kFolderNames[k])) {
          kind_folder = p;
          if (k == kWavetable && kind == kSample)
            kind = kWavetable;   // audio in the Wavetables folder is a wavetable
        }
      }
    }

    Entry entry;
    entry.name = file_name.upToLastOccurrenceOf(".", false, false).toStdString();
    entry.extension = extension.toLowerCase().toStdString();
    entry.bank = kind_folder > 0 ? parts[kind_folder - 1].toStdString() : bank_name;
    if (kind_folder >= 0) {
      StringArray folders;
      for (int p = kind_folder + 1; p < parts.size() - 1; ++p)
        folders.add(parts[p]);
      entry.folder = folders.joinIntoString("/").toStdString();
    }

    std::unique_ptr<InputStream> stream(zip.createStreamForEntry(i));
    if (stream == nullptr)
      continue;

    if (isAudioExtension(extension)) {
      stream->readIntoMemoryBlock(entry.audio);
      if (entry.audio.getSize() == 0)
        continue;
    }
    else {
      bool ok = false;
      entry.data = parseJson(stream->readEntireStreamAsString(), ok);
      if (!ok)
        continue;
    }

    add(kind, std::move(entry));
    added++;
  }

  if (added == 0)
    error = "There was nothing Vita can read in the bank.";
  return added;
}

bool VitaLibrary::exportBank(const File& file, const std::string& bank_name, std::string& error) const {
  ZipFile::Builder builder;
  std::set<std::string> used;
  String bank = File::createLegalFileName(bank_name.empty() ? "Vita Bank" : String(bank_name));
  int count = 0;

  for (int k = 0; k < kNumKinds; ++k) {
    for (const Entry& entry : entries_[k]) {
      String folder = bank + "/" + kFolderNames[k] + "/";
      if (!entry.folder.empty())
        folder += String(entry.folder) + "/";
      String name = File::createLegalFileName(entry.name.empty() ? String("Untitled") : String(entry.name));
      String extension = entry.extension.empty() ? String(k == kPreset ? vital::kPresetExtension :
                                                          k == kWavetable ? vital::kWavetableExtension :
                                                          k == kLfo ? vital::kLfoExtension : "wav")
                                                 : String(entry.extension);
      String path = folder + name + "." + extension;
      for (int n = 2; used.count(path.toStdString()); ++n)
        path = folder + name + " " + String(n) + "." + extension;
      used.insert(path.toStdString());

      MemoryBlock bytes;
      if (entry.isAudio())
        bytes = entry.audio;
      else {
        std::string text = entry.data.dump();
        bytes.append(text.data(), text.size());
      }
      builder.addEntry(new MemoryInputStream(std::move(bytes)), 9, path, Time::getCurrentTime());
      count++;
    }
  }

  if (count == 0) {
    error = "The library is empty.";
    return false;
  }

  file.deleteFile();
  FileOutputStream output(file);
  if (!output.openedOk()) {
    error = "The bank could not be written.";
    return false;
  }
  if (!builder.writeToStream(output, nullptr)) {
    error = "The bank could not be written.";
    return false;
  }
  output.flush();
  return true;
}

size_t VitaLibrary::approximateSize() const {
  size_t total = 0;
  for (const auto& kind : entries_) {
    for (const Entry& entry : kind)
      total += entry.isAudio() ? entry.audio.getSize() * 4 / 3 : entry.data.dump().size();
  }
  return total;
}

json VitaLibrary::toJson() const {
  json data;
  data["keep_in_patch"] = keep_in_patch_;
  if (!keep_in_patch_)
    return data;

  data["current_preset"] = current_preset_;
  for (int k = 0; k < kNumKinds; ++k) {
    json list = json::array();
    for (const Entry& entry : entries_[k]) {
      json item;
      item["name"] = entry.name;
      item["bank"] = entry.bank;
      item["folder"] = entry.folder;
      item["extension"] = entry.extension;
      item["edited"] = entry.edited;
      if (entry.isAudio())
        item["audio"] = entry.audio.toBase64Encoding().toStdString();
      else
        item["data"] = entry.data;
      list.push_back(item);
    }
    data[kFolderNames[k]] = list;
  }
  return data;
}

void VitaLibrary::fromJson(const json& data) {
  clear();
  if (!data.is_object())
    return;

  keep_in_patch_ = data.value("keep_in_patch", true);
  current_preset_ = data.value("current_preset", -1);
  for (int k = 0; k < kNumKinds; ++k) {
    if (!data.count(kFolderNames[k]) || !data[kFolderNames[k]].is_array())
      continue;
    for (const json& item : data[kFolderNames[k]]) {
      Entry entry;
      entry.name = item.value("name", std::string());
      entry.bank = item.value("bank", std::string());
      entry.folder = item.value("folder", std::string());
      entry.extension = item.value("extension", std::string());
      entry.edited = item.value("edited", false);
      if (item.count("audio"))
        entry.audio.fromBase64Encoding(String(item["audio"].get<std::string>()));
      else if (item.count("data"))
        entry.data = item["data"];
      entries_[k].push_back(std::move(entry));
    }
  }
  if (current_preset_ >= size(kPreset))
    current_preset_ = -1;
}
