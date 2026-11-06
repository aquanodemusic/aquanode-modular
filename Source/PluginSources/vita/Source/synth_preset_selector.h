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

#pragma once

#include "JuceHeader.h"
#include "preset_selector.h"
#include "synth_button.h"
#include "synth_section.h"

class SaveSection;

// Vita: the preset bar. It knows the presets loaded into Vita (its library, see vita_library.h) - not
// any folder: the arrows step through the library, the name opens its list, and the menu opens and
// saves presets and banks through file dialogs.
class SynthPresetSelector : public SynthSection, public PresetSelector::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() { }
    };

    enum MenuItems {
      kCancelled,
      kInitPreset,
      kSavePreset,
      kOpenPreset,
      kImportBank,
      kExportBank,
      kRemoveFromLibrary,
      kClearLibrary,
      kKeepLibraryInPatch,
      kLoadTuning,
      kClearTuning,
      kNumMenuItems
    };

    SynthPresetSelector();
    ~SynthPresetSelector();

    void resized() override;
    void paintBackground(Graphics& g) override { selector_ ->paintBackground(g); }
    void buttonClicked(Button* buttonThatWasClicked) override;
    void resetText();

    void prevClicked() override;
    void nextClicked() override;
    void textMouseUp(const MouseEvent& e) override;

    void showPopupMenu(Component* anchor);
    void showLibraryMenu(Component* anchor);

    void setModified(bool modified);
    void setSaveSection(SaveSection* save_section) { save_section_ = save_section; }

    void initPreset();
    void savePreset();
    void openPreset();
    void importBank();
    void exportBank();
    void removeFromLibrary();
    void clearLibrary();
    void toggleKeepLibraryInPatch();
    void loadLibraryPreset(int index);
    void loadTuningFile();
    void clearTuning();
    std::string getTuningName();
    bool hasDefaultTuning();

    void addListener(Listener* listener) { listeners_.push_back(listener); }

  private:
    SynthBase* getSynth();
    void showError(const std::string& title, const std::string& error);

    std::vector<Listener*> listeners_;

    std::unique_ptr<PresetSelector> selector_;
    std::unique_ptr<OpenGlShapeButton> menu_button_;
    std::unique_ptr<OpenGlShapeButton> save_button_;
    SaveSection* save_section_;
    bool modified_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPresetSelector)
};
