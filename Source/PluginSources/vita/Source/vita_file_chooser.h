#pragma once
// Vita: Vital opens its file choosers modally (browseForFileToOpen() and friends), which JUCE 9 and
// Android don't allow. vitaBrowse() runs the same chooser asynchronously and calls back with the
// picked file - not at all if nothing was picked or the component that asked is gone by then.

#include "JuceHeader.h"

enum class VitaBrowseMode { kOpen, kSave, kDirectory };

inline std::unique_ptr<FileChooser>& vitaCurrentChooser() {
  static std::unique_ptr<FileChooser> chooser;
  return chooser;
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
    callback(result);
  });
}
