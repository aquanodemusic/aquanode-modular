#pragma once
// Vita: Vital opens its file choosers modally (browseForFileToOpen() and friends), which JUCE 9 and
// Android don't allow. vitaBrowse() runs the same chooser asynchronously and calls back with the
// picked file - not at all if nothing was picked or the component that asked is gone by then.

#include "JuceHeader.h"
#include <map>

enum class VitaBrowseMode { kOpen, kSave, kDirectory };

inline std::unique_ptr<FileChooser>& vitaCurrentChooser() {
  // (Aquanode: a chooser left over is let go when JUCE shuts down, not when the DLL unloads)
  struct Holder : public DeletedAtShutdown {
    std::unique_ptr<FileChooser> chooser;
    ~Holder() override { slot() = nullptr; }
    static Holder*& slot() { static Holder* h = nullptr; return h; }
  };
  auto*& h = Holder::slot();
  if (h == nullptr)
    h = new Holder();
  return h->chooser;
}

// Android hands a picked document over under its document number (e.g. "271702"); the name the
// person sees in the picker is remembered here by path, so a loaded preset can show it
inline std::map<String, String>& vitaPickedNames() {
  static std::map<String, String> names;
  return names;
}

inline String vitaDisplayName(const File& file) {   // without the extension
  auto& names = vitaPickedNames();
  auto it = names.find(file.getFullPathName());
  if (it == names.end() || it->second.isEmpty())
    return file.getFileNameWithoutExtension();
  return it->second.containsChar('.') ? it->second.upToLastOccurrenceOf(".", false, false) : it->second;
}

inline void vitaBrowse(Component* guard, VitaBrowseMode mode, const String& title, const File& start,
                       const String& patterns, std::function<void(const File&)> callback) {
  // one chooser at a time; an earlier one goes away after the current message
  if (vitaCurrentChooser() != nullptr) {
    std::shared_ptr<FileChooser> old(vitaCurrentChooser().release());
    MessageManager::callAsync([old] { });
  }
  vitaCurrentChooser() = std::make_unique<FileChooser>(title, start, patterns);

  int flags = FileBrowserComponent::canSelectFiles | FileBrowserComponent::openMode;
  if (mode == VitaBrowseMode::kSave)
    flags = FileBrowserComponent::canSelectFiles | FileBrowserComponent::saveMode |
            FileBrowserComponent::warnAboutOverwriting;
  else if (mode == VitaBrowseMode::kDirectory)
    flags = FileBrowserComponent::canSelectDirectories | FileBrowserComponent::openMode;

  Component::SafePointer<Component> safe(guard);
  bool guarded = guard != nullptr;
  vitaCurrentChooser()->launchAsync(flags, [safe, guarded, callback](const FileChooser& chooser) {
    File result = chooser.getResult();
    if (result == File() || (guarded && safe == nullptr))
      return;
#if JUCE_ANDROID
    const URL url = chooser.getURLResult();
    if (url.getScheme() == "content") {
      if (auto doc = AndroidDocument::fromDocument(url)) {
        const String shown = doc.getInfo().getName();
        if (shown.isNotEmpty())
          vitaPickedNames()[result.getFullPathName()] = shown;
      }
    }
#endif
    callback(result);
  });
}
