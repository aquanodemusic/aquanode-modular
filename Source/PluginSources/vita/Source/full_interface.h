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

#include "header_section.h"
#include "effects_interface.h"
#include "memory.h"
#include "modulation_matrix.h"
#include "popup_browser.h"
#include "shaders.h"
#include "synth_section.h"
#include "wavetable_creator.h"

class BendSection;
class ExtraModSection;
class HeaderSection;
class KeyboardInterface;
class MasterControlsInterface;
class ModulationInterface;
class ModulationManager;
class PortamentoSection;
class SaveSection;
class SynthesisInterface;
struct SynthGuiData;
class SynthSlider;
class WavetableEditSection;
class VoiceSection;

class FullInterface : public SynthSection, public HeaderSection::Listener,
                      public EffectsInterface::Listener, public ModulationMatrix::Listener,
                      public DragAndDropContainer, private Timer {
  public:
    static constexpr double kMinOpenGlVersion = 1.4;

    FullInterface(SynthGuiData* synth_gui_data);

    FullInterface();
    virtual ~FullInterface();

    void setOscilloscopeMemory(const vital::poly_float* memory);
    void setAudioMemory(const vital::StereoMemory* memory);

    void createModulationSliders(const vital::output_map& mono_modulations,
                                 const vital::output_map& poly_modulations);

    virtual void paintBackground(Graphics& g) override;
    void copySkinValues(const Skin& skin);
    void reloadSkin(const Skin& skin);

    void repaintChildBackground(SynthSection* child);
    void repaintSynthesisSection();
    void repaintOpenGlBackground(OpenGlComponent* component);
    void redoBackground();
    void checkShouldReposition(bool resize = true);
    void parentHierarchyChanged() override {
      SynthSection::parentHierarchyChanged();
      checkShouldReposition();
    }
    virtual void resized() override;
    void animate(bool animate) override;
    void reset() override;
    void setAllValues(vital::control_map& controls) override;

    void setWavetableNames();


    void tabSelected(int index) override;
    void clearTemporaryTab(int current_tab) override;

    void effectsMoved() override;
    void modulationsScrolled() override;

    void setFocus();
    void notifyChange();
    void notifyFresh();
    void showFullScreenSection(SynthSection* full_screen);
    void showWavetableEditSection(int index);
    std::string getLastBrowsedWavetable(int index);
    std::string getWavetableName(int index);
    void hideWavetableEditSection();
    void loadWavetableFile(int index, const File& wavetable);
    void loadWavetable(int index, json& wavetable_data);
    void loadDefaultWavetable(int index);
    void resynthesizeToWavetable(int index);
    void saveWavetable(int index);
    void saveLfo(const json& data);
    json getWavetableJson(int index);
    bool loadAudioAsWavetable(int index, const String& name, InputStream* audio_stream,
                              WavetableCreator::AudioFileLoadStyle style);
    void popupSelector(Component* source, Point<int> position, const PopupItems& options,
                       std::function<void(int)> callback, std::function<void()> cancel);
    void dualPopupSelector(Component* source, Point<int> position, int width,
                           const PopupItems& options, std::function<void(int)> callback);
    void popupDisplay(Component* source, const std::string& text,
                      BubbleComponent::BubblePlacement placement, bool primary);
    void hideDisplay(bool primary);
    void modulationChanged();
    void modulationValueChanged(int index);
    void openSaveDialog() { save_section_->setIsPreset(true); save_section_->setVisible(true); }
    void enableRedoBackground(bool enable) {
      enable_redo_background_ = enable;
      if (enable)
        resized();
    }

    // Vita: the interface's frame, drawn by the editor straight into the card (see vita_gfx.h)
    void paintFrame(Graphics& g);
    // the pixels per interface unit the images (background, text) are drawn at
    float getRenderScale() const { return render_scale_; }
    // a frame is wanted soon (something changed, the mouse moved)
    void wakeUp();
    float getResizingScale() const { return width_ * 1.0f / resized_width_; }
    float getPixelScaling() const { return display_scale_; }
    int getPixelMultiple() const override { return pixel_multiple_; }
    void toggleOscillatorZoom(int index);
    void toggleFilter1Zoom();
    void toggleFilter2Zoom();

  private:
    // the oscillator's wavetable editor, made the first time it is needed
    WavetableEditSection* wavetableEditor(int index);

    std::map<std::string, SynthSlider*> slider_lookup_;
    std::map<std::string, Button*> button_lookup_;
    std::unique_ptr<ModulationManager> modulation_manager_;
    std::unique_ptr<ModulationMatrix> modulation_matrix_;

    std::unique_ptr<Component> standalone_settings_section_;

    std::unique_ptr<HeaderSection> header_;
    std::unique_ptr<SynthesisInterface> synthesis_interface_;
    std::unique_ptr<MasterControlsInterface> master_controls_interface_;
    std::unique_ptr<ModulationInterface> modulation_interface_;
    std::unique_ptr<ExtraModSection> extra_mod_section_;
    std::unique_ptr<EffectsInterface> effects_interface_;
    std::unique_ptr<WavetableEditSection> wavetable_edits_[vital::kNumOscillators];
    WavetableCreator* wavetable_creators_[vital::kNumOscillators] = {};
    Rectangle<int> wavetable_edit_bounds_;
    Skin skin_;
    std::unique_ptr<KeyboardInterface> keyboard_interface_;
    std::unique_ptr<BendSection> bend_section_;
    std::unique_ptr<PortamentoSection> portamento_section_;
    std::unique_ptr<VoiceSection> voice_section_;
    std::unique_ptr<SinglePopupSelector> popup_selector_;
    std::unique_ptr<DualPopupSelector> dual_popup_selector_;
    std::unique_ptr<PopupDisplay> popup_display_1_;
    std::unique_ptr<PopupDisplay> popup_display_2_;
    std::unique_ptr<SaveSection> save_section_;
    SynthSection* full_screen_section_;

    int width_;
    int resized_width_;
    float last_render_scale_;
    float display_scale_;
    float background_scale_ = 1.0f;
    int pixel_multiple_;
    bool setting_all_values_;
    bool unsupported_;
    bool animate_;
    bool enable_redo_background_;
    CriticalSection open_gl_critical_section_;
    Shaders shaders_;
    Image background_image_;

    // Vita: frames are repainted at kFrameRate while something moves (sound, the mouse, a fade)
    static constexpr int kFrameRate =
    #if JUCE_ANDROID || JUCE_IOS
      24;
    #else
      30;
    #endif

    void timerCallback() override;
    void mouseActivity();
    void mouseMove(const MouseEvent& e) override;
    void mouseEnter(const MouseEvent& e) override;
    void mouseExit(const MouseEvent& e) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override;
    void settleRenderScale(float scale);
    bool components_initialised_ = false;
    float render_scale_ = 1.0f;
    float pending_render_scale_ = 1.0f;
    int scale_change_generation_ = 0;
    uint32 last_activity_ms_ = 0;
    uint32 last_sound_ms_ = 0;
    uint32 last_repaint_ms_ = 0;
    double last_frame_ms_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullInterface)
};
