/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "synth_preset_selector.h"
#include "vita_soundscapes.h"
#include "vita_file_chooser.h"

#include "paths.h"
#include "skin.h"
#include "default_look_and_feel.h"
#include "fonts.h"
#include "load_save.h"
#include "full_interface.h"
#include "save_section.h"
#include "synth_gui_interface.h"
#include "synth_base.h"
#include "tuning.h"

namespace {
  constexpr int kLibraryPresetOffset = 1000;
  constexpr int kSoundscapeOffset = 100000;
  constexpr int kSoundscapes = 99999;
  constexpr int kGenerateGrowl = 99998;
  constexpr int kGenerateKind = 99900;   // (6.9.11) + the kind's index: Generate: Pads, Basses ...

  void menuCallback(int result, SynthPresetSelector* preset_selector) {
    if (preset_selector == nullptr)
      return;
    if (result >= kSoundscapeOffset)
      preset_selector->loadSoundscape(result - kSoundscapeOffset);
    else if (result == kSoundscapes)
      preset_selector->showSoundscapes();
    else if (result == kGenerateGrowl)
      preset_selector->generateGrowl();
    else if (result >= kGenerateKind && result < kGenerateKind + 50)
      preset_selector->generateKind(result - kGenerateKind);
    else if (result >= kLibraryPresetOffset)
      preset_selector->loadLibraryPreset(result - kLibraryPresetOffset);
    else if (result == SynthPresetSelector::kInitPreset)
      preset_selector->initPreset();
    else if (result == SynthPresetSelector::kOpenPreset)
      preset_selector->openPreset();
    else if (result == SynthPresetSelector::kImportBank)
      preset_selector->importBank();
    else if (result == SynthPresetSelector::kExportBank)
      preset_selector->exportBank();
    else if (result == SynthPresetSelector::kSavePreset)
      preset_selector->savePreset();
    else if (result == SynthPresetSelector::kRemoveFromLibrary)
      preset_selector->removeFromLibrary();
    else if (result == SynthPresetSelector::kClearLibrary)
      preset_selector->clearLibrary();
    else if (result == SynthPresetSelector::kKeepLibraryInPatch)
      preset_selector->toggleKeepLibraryInPatch();
    else if (result == SynthPresetSelector::kLoadTuning)
      preset_selector->loadTuningFile();
    else if (result == SynthPresetSelector::kClearTuning)
      preset_selector->clearTuning();
  }

  String sizeText(size_t bytes) {
    if (bytes < 1024 * 1024)
      return String(std::max<size_t>(1, bytes / 1024)) + " KB";
    return String(bytes / (1024.0 * 1024.0), 1) + " MB";
  }
}

SynthPresetSelector::SynthPresetSelector() : SynthSection("preset_selector"), save_section_(nullptr),
                                             modified_(false) {
  selector_ = std::make_unique<PresetSelector>();
  addSubSection(selector_.get());
  selector_->addListener(this);

  menu_button_ = std::make_unique<OpenGlShapeButton>("Menu");
  addAndMakeVisible(menu_button_.get());
  addOpenGlComponent(menu_button_->getGlComponent());
  menu_button_->addListener(this);
  menu_button_->setTriggeredOnMouseDown(true);
  menu_button_->setShape(Paths::menu());

  save_button_ = std::make_unique<OpenGlShapeButton>("Save");
  addAndMakeVisible(save_button_.get());
  addOpenGlComponent(save_button_->getGlComponent());
  save_button_->addListener(this);

  save_button_->setShape(Paths::save());
}

SynthPresetSelector::~SynthPresetSelector() = default;

void SynthPresetSelector::resized() {
  static constexpr float kSelectorButtonPaddingHeightPercent = 0.2f;

  int height = getHeight();
  selector_->setRoundAmount(height / 2.0f);
  int padding = kSelectorButtonPaddingHeightPercent * height;
  selector_->setBounds(0, 0, getWidth() - 2 * height - padding, height);
  save_button_->setBounds(getWidth() - 2 * height, 0, height, height);
  save_button_->setShape(Paths::save(height));
  menu_button_->setBounds(getWidth() - height, 0, height, height);
  menu_button_->setShape(Paths::menu(height));

  resetText();

  Component::resized();
}

SynthBase* SynthPresetSelector::getSynth() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  return parent ? parent->getSynth() : nullptr;
}

void SynthPresetSelector::showError(const std::string& title, const std::string& error) {
  NativeMessageBox::showMessageBoxAsync(MessageBoxIconType::WarningIcon, String(title), String(error));
}

void SynthPresetSelector::buttonClicked(Button* clicked_button) {
  if (clicked_button == menu_button_.get())
    showPopupMenu(menu_button_.get());
  else if (clicked_button == save_button_.get())
    savePreset();
}

void SynthPresetSelector::resetText() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;

  String preset_text = synth->getPresetName();
  if (preset_text == "")
    preset_text = TRANS("Init Preset");

  if (modified_)
    preset_text = "*" + preset_text;

  selector_->setText(preset_text);
  repaint();
}

void SynthPresetSelector::prevClicked() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  VitaLibrary& library = synth->getLibrary();
  int count = library.size(VitaLibrary::kPreset);
  if ((count == 0 || library.currentPreset() < 0) && synth->currentSoundscape() >= 0) {
    int n = VitaSoundscapes::count();
    loadSoundscape((synth->currentSoundscape() + n - 1) % n);
    return;
  }
  if (count == 0)
    return;
  int current = library.currentPreset();
  loadLibraryPreset(current < 0 ? count - 1 : (current + count - 1) % count);
}

void SynthPresetSelector::nextClicked() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  VitaLibrary& library = synth->getLibrary();
  int count = library.size(VitaLibrary::kPreset);
  if ((count == 0 || library.currentPreset() < 0) && synth->currentSoundscape() >= 0) {
    loadSoundscape((synth->currentSoundscape() + 1) % VitaSoundscapes::count());
    return;
  }
  if (count == 0)
    return;
  int current = library.currentPreset();
  loadLibraryPreset(current < 0 ? 0 : (current + 1) % count);
}

void SynthPresetSelector::textMouseUp(const MouseEvent& e) {
  if (e.mods.isPopupMenu())
    showPopupMenu(selector_.get());
  else
    showLibraryMenu(selector_.get());
}

void SynthPresetSelector::showLibraryMenu(Component* anchor) {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;

  PopupItems options;
  options.addItem(kSoundscapes, "Aquanode Soundscapes...");
  // (6.9.11) new presets of a kind, learned from the Aquanode Soundscapes
  {
    const auto kinds = VitaSoundscapes::generatorKinds();
    for (int k = 0; k < (int)kinds.size(); ++k)
      if (VitaSoundscapes::kindSize(kinds[(size_t)k]) > 0)
        options.addItem(kGenerateKind + k, "Generate: " + kinds[(size_t)k]);
  }
  options.addItem(kOpenPreset, "Open Preset...");
  options.addItem(kImportBank, "Import Bank...");

  VitaLibrary& library = synth->getLibrary();
  const std::vector<VitaLibrary::Entry>& presets = library.entries(VitaLibrary::kPreset);
  if (!presets.empty())
    options.addItem(-1, "");
  for (int i = 0; i < (int)presets.size(); ++i) {
    std::string name = presets[i].displayName();
    if (presets[i].edited)
      name += " *";
    options.addItem(kLibraryPresetOffset + i, name, i == library.currentPreset());
  }

  showPopupSelector(this, Point<int>(anchor->getX(), anchor->getBottom()), options,
                    [=](int selection) { menuCallback(selection, this); });
}

void SynthPresetSelector::showPopupMenu(Component* anchor) {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  VitaLibrary& library = synth->getLibrary();

  PopupItems options;
  options.addItem(kOpenPreset, "Open Preset...");
  options.addItem(kSavePreset, "Save Preset...");
  options.addItem(-1, "");
  options.addItem(kImportBank, "Import Bank...");
  if (!library.empty())
    options.addItem(kExportBank, "Export Library as Bank...");
  if (library.currentPreset() >= 0)
    options.addItem(kRemoveFromLibrary, "Remove Preset from Library");
  if (!library.empty())
    options.addItem(kClearLibrary, "Clear Library");
  String keep = library.keepInPatch() ? "Keep Library in Patch: On" : "Keep Library in Patch: Off";
  if (!library.empty())
    keep += " (" + sizeText(library.approximateSize()) + ")";
  options.addItem(kKeepLibraryInPatch, keep.toStdString());
  options.addItem(-1, "");
  options.addItem(kInitPreset, "Initialize Preset");
  options.addItem(-1, "");
  options.addItem(kLoadTuning, "Load Tuning File...");
  if (!hasDefaultTuning())
    options.addItem(kClearTuning, "Clear Tuning: " + getTuningName());

  showPopupSelector(this, Point<int>(anchor->getX(), anchor->getBottom()), options,
                    [=](int selection) { menuCallback(selection, this); });
}

void SynthPresetSelector::setModified(bool modified) {
  if (modified_ == modified)
    return;

  modified_ = modified;
  String text = selector_->getText();
  if (modified_ && text.length() && text[0] != '*')
    selector_->setText("*" + text);
  else if (!modified_ && text.length() && text[0] == '*')
    selector_->setText(text.substring(1));
}

void SynthPresetSelector::loadLibraryPreset(int index) {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  std::string error;
  if (!synth->loadLibraryPreset(index, error) && !error.empty())
    showError("Error opening preset", "There was an error opening the preset. " + error);
  resetText();
}

void SynthPresetSelector::loadSoundscape(int index) {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  std::string error;
  if (!synth->loadSoundscape(index, error) && !error.empty())
    showError("Error opening preset", "There was an error opening the preset. " + error);
  resetText();
}

// the built-in presets: categories on the left, their presets on the right
void SynthPresetSelector::showSoundscapes() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  PopupItems options;
  for (const std::string& category : VitaSoundscapes::categories()) {
    PopupItems sub(category);
    for (int i = 0; i < VitaSoundscapes::count(); ++i)
      if (VitaSoundscapes::category(i) == category) {
        sub.addItem(kSoundscapeOffset + i, VitaSoundscapes::name(i), i == synth->currentSoundscape());
        if (i == synth->currentSoundscape()) sub.selected = true;
      }
    options.addItem(sub);
  }
  showDualPopupSelector(this, Point<int>(selector_->getX(), selector_->getBottom()), 500 * size_ratio_,
                        options, [=](int selection) { menuCallback(selection, this); });
}

void SynthPresetSelector::generateKind(int kind) {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  const auto kinds = VitaSoundscapes::generatorKinds();
  if (kind >= 0 && kind < (int)kinds.size())
    synth->generateKind(kinds[(size_t)kind]);
  resetText();
}

void SynthPresetSelector::generateGrowl() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  synth->generateGrowl();
  resetText();
}

void SynthPresetSelector::initPreset() {
  SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
  if (parent == nullptr)
    return;
  SynthBase* synth = parent->getSynth();
  synth->storeCurrentPreset();
  synth->getLibrary().setCurrentPreset(-1);
  synth->loadInitPreset();
  synth->markPresetUnchanged();
  parent->updateFullGui();
  parent->notifyFresh();
  resetText();
}

void SynthPresetSelector::savePreset() {
  if (save_section_) {
    save_section_->setIsPreset(true);
    save_section_->setVisible(true);
  }
}

void SynthPresetSelector::openPreset() {
  vitaBrowse(this, VitaBrowseMode::kOpen, "Open Preset", File(), String("*.") + vital::kPresetExtension,
             [this](const File& choice) {
    SynthBase* synth = getSynth();
    if (synth == nullptr || !choice.existsAsFile())
      return;

    std::string error;
    if (!synth->openPresetFile(choice, error))
      showError("Error opening preset", "There was an error opening the preset. " + error);
    resetText();
  });
}

void SynthPresetSelector::importBank() {
  vitaBrowse(this, VitaBrowseMode::kOpen, "Import Bank", File(), String("*.") + vital::kBankExtension,
             [this](const File& result) {
    SynthBase* synth = getSynth();
    if (synth == nullptr)
      return;

    VitaLibrary& library = synth->getLibrary();
    int presets_before = library.size(VitaLibrary::kPreset);
    std::string bank_name, error;
    if (library.importBank(result, bank_name, error) == 0) {
      showError("Error importing bank", error);
      return;
    }

    // the bank's first preset plays
    const std::vector<VitaLibrary::Entry>& presets = library.entries(VitaLibrary::kPreset);
    for (int i = 0; i < (int)presets.size(); ++i) {
      if (presets[i].bank == bank_name || i >= presets_before) {
        loadLibraryPreset(i);
        break;
      }
    }
  });
}

void SynthPresetSelector::exportBank() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  synth->storeCurrentPreset();

  vitaBrowse(this, VitaBrowseMode::kSave, "Export Bank", File(), String("*.") + vital::kBankExtension,
             [this](const File& result) {
    SynthBase* synth = getSynth();
    if (synth == nullptr)
      return;
    File file = result.withFileExtension(vital::kBankExtension);
    std::string error;
    if (!synth->getLibrary().exportBank(file, file.getFileNameWithoutExtension().toStdString(), error))
      showError("Error exporting bank", error);
  });
}

void SynthPresetSelector::removeFromLibrary() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  VitaLibrary& library = synth->getLibrary();
  library.remove(VitaLibrary::kPreset, library.currentPreset());
  resetText();
}

void SynthPresetSelector::clearLibrary() {
  Component::SafePointer<SynthPresetSelector> safe(this);
  NativeMessageBox::showOkCancelBox(MessageBoxIconType::QuestionIcon, "Clear Library",
                                    "Remove all presets, wavetables, LFOs and samples from Vita's library? "
                                    "The sound playing now stays.", nullptr,
                                    ModalCallbackFunction::create([safe](int result) {
    if (result == 0 || safe == nullptr)
      return;
    if (SynthBase* synth = safe->getSynth())
      synth->getLibrary().clear();
    safe->resetText();
  }));
}

void SynthPresetSelector::toggleKeepLibraryInPatch() {
  SynthBase* synth = getSynth();
  if (synth == nullptr)
    return;
  VitaLibrary& library = synth->getLibrary();
  library.setKeepInPatch(!library.keepInPatch());
}

void SynthPresetSelector::loadTuningFile() {
  vitaBrowse(this, VitaBrowseMode::kOpen, "Load Tuning", File(), Tuning::allFileExtensions(),
             [this](const File& result) {
    if (SynthBase* synth = getSynth())
      synth->loadTuningFile(result);
  });
}

void SynthPresetSelector::clearTuning() {
  if (SynthBase* synth = getSynth())
    synth->getTuning()->setDefaultTuning();
}

std::string SynthPresetSelector::getTuningName() {
  SynthBase* synth = getSynth();
  return synth ? synth->getTuning()->getName() : "";
}

bool SynthPresetSelector::hasDefaultTuning() {
  SynthBase* synth = getSynth();
  return synth == nullptr || synth->getTuning()->isDefault();
}
