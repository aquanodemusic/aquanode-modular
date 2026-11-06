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

#include "full_interface.h"

#include "bend_section.h"
#include "extra_mod_section.h"
#include "skin.h"
#include "effects_interface.h"
#include "fonts.h"
#include "sound_engine.h"
#include "keyboard_interface.h"
#include "load_save.h"
#include "master_controls_interface.h"
#include "modulation_interface.h"
#include "modulation_manager.h"
#include "modulation_matrix.h"
#include "modulation_meter.h"
#include "wavetable_edit_section.h"
#include "overlay.h"
#include "portamento_section.h"
#include "synthesis_interface.h"
#include "synth_gui_interface.h"
#include "synth_base.h"
#include "text_look_and_feel.h"
#include "voice_section.h"

FullInterface::FullInterface(SynthGuiData* synth_data) : SynthSection("full_interface"), width_(0), resized_width_(0),
                                                         last_render_scale_(0.0f), display_scale_(1.0f),
                                                         pixel_multiple_(1), setting_all_values_(false),
                                                         unsupported_(false), animate_(true),
                                                         enable_redo_background_(true) {
  full_screen_section_ = nullptr;
  Skin default_skin;
  setSkinValues(default_skin, true);
  default_skin.copyValuesToLookAndFeel(DefaultLookAndFeel::instance());

  bool synth = synth_data->wavetable_creators[0];
  if (synth) {
    LineGenerator* spectral_warp_curves[vital::kNumOscillators];
    for (int i = 0; i < vital::kNumOscillators; ++i)
      spectral_warp_curves[i] = synth_data->synth->getCustomWarp(i);
    synthesis_interface_ = std::make_unique<SynthesisInterface>(synth_data->mono_modulations,
                                                                synth_data->poly_modulations,
                                                                spectral_warp_curves);

    // Vita: the wavetable editors are made when first needed (opening one, loading a wavetable)
    for (int i = 0; i < vital::kNumOscillators; ++i)
      wavetable_creators_[i] = synth_data->wavetable_creators[i];

    addSubSection(synthesis_interface_.get());
  }

  effects_interface_ = std::make_unique<EffectsInterface>(synth_data->mono_modulations);
  addSubSection(effects_interface_.get());
  effects_interface_->setVisible(false);
  effects_interface_->addListener(this);

  master_controls_interface_ = std::make_unique<MasterControlsInterface>(synth_data->mono_modulations,
                                                                         synth_data->poly_modulations,
                                                                         synth);
  addSubSection(master_controls_interface_.get());
  master_controls_interface_->setVisible(false);

  if (synthesis_interface_) {
    for (int i = 0; i < vital::kNumOscillators; ++i)
      master_controls_interface_->passOscillatorSection(i, synthesis_interface_->getOscillatorSection(i));
  }

  header_ = std::make_unique<HeaderSection>();
  addSubSection(header_.get());
  header_->addListener(this);

  modulation_interface_ = std::make_unique<ModulationInterface>(synth_data);
  addSubSection(modulation_interface_.get());

  extra_mod_section_ = std::make_unique<ExtraModSection>("extra_mod_section", synth_data);
  addSubSection(extra_mod_section_.get());

  keyboard_interface_ = std::make_unique<KeyboardInterface>(synth_data->synth->getKeyboardState());
  addSubSection(keyboard_interface_.get());

  bend_section_ = std::make_unique<BendSection>("BEND");
  addSubSection(bend_section_.get());

  portamento_section_ = std::make_unique<PortamentoSection>("PORTAMENTO");
  addSubSection(portamento_section_.get());

  voice_section_ = std::make_unique<VoiceSection>("VOICE");
  addSubSection(voice_section_.get());

  modulation_matrix_ = std::make_unique<ModulationMatrix>(synth_data->modulation_sources, synth_data->mono_modulations);
  addSubSection(modulation_matrix_.get());
  modulation_matrix_->setVisible(false);
  modulation_matrix_->addListener(this);
  createModulationSliders(synth_data->mono_modulations, synth_data->poly_modulations);

  popup_selector_ = std::make_unique<SinglePopupSelector>();
  addSubSection(popup_selector_.get());
  popup_selector_->setVisible(false);
  popup_selector_->setAlwaysOnTop(true);
  popup_selector_->setWantsKeyboardFocus(true);

  dual_popup_selector_ = std::make_unique<DualPopupSelector>();
  addSubSection(dual_popup_selector_.get());
  dual_popup_selector_->setVisible(false);
  dual_popup_selector_->setAlwaysOnTop(true);
  dual_popup_selector_->setWantsKeyboardFocus(true);

  popup_display_1_ = std::make_unique<PopupDisplay>();
  addSubSection(popup_display_1_.get());
  popup_display_1_->setVisible(false);
  popup_display_1_->setAlwaysOnTop(true);
  popup_display_1_->setWantsKeyboardFocus(false);

  popup_display_2_ = std::make_unique<PopupDisplay>();
  addSubSection(popup_display_2_.get());
  popup_display_2_->setVisible(false);
  popup_display_2_->setAlwaysOnTop(true);
  popup_display_2_->setWantsKeyboardFocus(false);

  save_section_ = std::make_unique<SaveSection>("save_section");
  addSubSection(save_section_.get(), false);
  addChildComponent(save_section_.get());
  header_->setSaveSection(save_section_.get());

  if (synthesis_interface_)
    synthesis_interface_->toFront(true);

  master_controls_interface_->toFront(true);
  effects_interface_->toFront(true);
  modulation_interface_->toFront(true);
  extra_mod_section_->toFront(true);
  keyboard_interface_->toFront(true);
  bend_section_->toFront(true);
  portamento_section_->toFront(true);
  voice_section_->toFront(true);
  modulation_manager_->toFront(false);
  save_section_->toFront(true);
  popup_selector_->toFront(true);
  dual_popup_selector_->toFront(true);
  popup_display_1_->toFront(true);
  popup_display_2_->toFront(true);

  setAllValues(synth_data->controls);
  setOpaque(true);
  setSkinValues(default_skin, true);

  // Vita: the frame is painted by the editor (paintFrame); the interface's components only take the
  // mouse and keys, as they did under Vital's OpenGL context
  setAlpha(0.0f);
  addMouseListener(this, true);
  startTimerHz(kFrameRate);
}

FullInterface::FullInterface() : SynthSection("EMPTY") {
  Skin default_skin;
  setSkinValues(default_skin, true);

  // Vita: the frame is painted by the editor (paintFrame); the interface's components only take the
  // mouse and keys, as they did under Vital's OpenGL context
  setAlpha(0.0f);
  addMouseListener(this, true);
  startTimerHz(kFrameRate);

  reset();
  setOpaque(true);
}

FullInterface::~FullInterface() {
  stopTimer();
}

void FullInterface::paintBackground(Graphics& g) {
  g.fillAll(findColour(Skin::kBackground, true));
  paintChildrenShadows(g);

  if (effects_interface_ == nullptr)
    return;

  int padding = getPadding();
  int bar_width = 6 * padding;
  g.setColour(header_->findColour(Skin::kBody, true));
  int y = header_->getBottom();
  int height = keyboard_interface_->getY() - y;
  int x1 = extra_mod_section_->getRight() + padding;
  g.fillRect(x1, y, bar_width, height);

  if (synthesis_interface_) {
    int x2 = synthesis_interface_->getRight() + padding;
    g.fillRect(x2, y, bar_width, height);
  }

  paintKnobShadows(g);
  paintChildrenBackgrounds(g);
}

void FullInterface::copySkinValues(const Skin& skin) {
  skin_ = skin;
  ScopedLock open_gl_lock(open_gl_critical_section_);
  skin.copyValuesToLookAndFeel(DefaultLookAndFeel::instance());
  setSkinValues(skin, true);
}

void FullInterface::reloadSkin(const Skin& skin) {
  copySkinValues(skin);
  Rectangle<int> bounds = getBounds();
  setBounds(0, 0, bounds.getWidth() / 4, bounds.getHeight() / 4);
  setBounds(bounds);
}

void FullInterface::repaintChildBackground(SynthSection* child) {
  if (!background_image_.isValid() || setting_all_values_)
    return; 

  if (child->getParentComponent() == synthesis_interface_.get()) {
    repaintSynthesisSection();
    return;
  }

  if (effects_interface_ != nullptr && effects_interface_->isParentOf(child))
    child = effects_interface_.get();

  Graphics g(background_image_);
  g.addTransform(AffineTransform::scale(background_scale_));
  paintChildBackground(g, child);
  wakeUp();
}

void FullInterface::repaintSynthesisSection() {
  if (synthesis_interface_ == nullptr || !synthesis_interface_->isVisible() || !background_image_.isValid())
    return;

  Graphics g(background_image_);
  g.addTransform(AffineTransform::scale(background_scale_));
  int padding = findValue(Skin::kPadding);
  g.setColour(findColour(Skin::kBackground, true));
  g.fillRect(synthesis_interface_->getBounds().expanded(padding));
  paintChildShadow(g, synthesis_interface_.get());
  paintChildBackground(g, synthesis_interface_.get());

  wakeUp();
}

void FullInterface::repaintOpenGlBackground(OpenGlComponent* component) {
  if (!background_image_.isValid())
    return;

  Graphics g(background_image_);
  g.addTransform(AffineTransform::scale(background_scale_));
  paintOpenGlBackground(g, component);
  wakeUp();
}

void FullInterface::redoBackground() {
  // Vita: the background is drawn at the resolution the interface is shown at
  if (getWidth() < vital::kMinWindowWidth || getHeight() < vital::kMinWindowHeight)
    return;
  background_scale_ = render_scale_;
  int width = std::ceil(background_scale_ * getWidth());
  int height = std::ceil(background_scale_ * getHeight());

  ScopedLock open_gl_lock(open_gl_critical_section_);

  background_image_ = Image(Image::RGB, width, height, true);
  Graphics g(background_image_);
  g.addTransform(AffineTransform::scale(background_scale_));
  paintBackground(g);
  wakeUp();
}

void FullInterface::checkShouldReposition(bool resize) {
  float old_scale = display_scale_;
  int old_pixel_multiple = pixel_multiple_;
  // Vita: laid out in the interface's own coordinates, whatever the zoom and pixel density - the
  // software renderer draws the frame at the resolution it is shown at
  display_scale_ = 1.0f;
  pixel_multiple_ = 1;

  if (resize && (old_scale != display_scale_ || old_pixel_multiple != pixel_multiple_))
    resized();
}

void FullInterface::resized() {
  checkShouldReposition(false);

  width_ = getWidth();
  if (!enable_redo_background_)
    return;

  resized_width_ = width_;

  ScopedLock lock(open_gl_critical_section_);
  static constexpr int kTopHeight = 48;

  if (effects_interface_ == nullptr)
    return;

  int left = 0;
  int top = 0;
  int width = std::ceil(getWidth() * display_scale_);
  int height = std::ceil(getHeight() * display_scale_);
  Rectangle<int> bounds(0, 0, width, height);

  float width_ratio = getWidth() / (1.0f * vital::kDefaultWindowWidth);
  float ratio = width_ratio * display_scale_;
  float height_ratio = getHeight() / (1.0f * vital::kDefaultWindowHeight);
  if (width_ratio > height_ratio + 1.0f / vital::kDefaultWindowHeight) {
    ratio = height_ratio;
    width = height_ratio * vital::kDefaultWindowWidth * display_scale_;
    left = (getWidth() - width) / 2;
  }
  if (height_ratio > width_ratio + 1.0f / vital::kDefaultWindowHeight) {
    ratio = width_ratio;
    height = ratio * vital::kDefaultWindowHeight * display_scale_;
    top = (getHeight() - height) / 2;
  }

  setSizeRatio(ratio);

  int padding = getPadding();
  int voice_padding = findValue(Skin::kLargePadding);
  int extra_mod_width = findValue(Skin::kModulationButtonWidth);
  int main_x = left + extra_mod_width + 2 * voice_padding;
  int top_height = kTopHeight * ratio;

  int knob_section_height = getKnobSectionHeight();
  int keyboard_section_height = knob_section_height * 0.7f;
  int voice_height = height - top_height - keyboard_section_height;

  int section_one_width = 350 * ratio;
  int section_two_width = section_one_width;
  int audio_width = section_one_width + section_two_width + padding;
  int modulation_width = width - audio_width - extra_mod_width - 4 * voice_padding;

  header_->setTabOffset(extra_mod_width + 2 * voice_padding);
  header_->setBounds(left, top, width, top_height);
  Rectangle<int> main_bounds(main_x, top + top_height, audio_width, voice_height);

  if (synthesis_interface_)
    synthesis_interface_->setBounds(main_bounds);
  effects_interface_->setBounds(main_bounds.withRight(main_bounds.getRight() + voice_padding));
  modulation_matrix_->setBounds(main_bounds);
  int modulation_height = voice_height - knob_section_height - padding;
  modulation_interface_->setBounds(main_bounds.getRight() + voice_padding,
                                   main_bounds.getY(), modulation_width, modulation_height);

  int voice_y = top + height - knob_section_height - keyboard_section_height;

  int portamento_width = 4 * (int)findValue(Skin::kModulationButtonWidth);
  int portamento_x = modulation_interface_->getRight() - portamento_width;
  portamento_section_->setBounds(portamento_x, voice_y, portamento_width, knob_section_height);

  int voice_width = modulation_interface_->getWidth() - portamento_width - padding;
  voice_section_->setBounds(modulation_interface_->getX(), voice_y, voice_width, knob_section_height);

  bend_section_->setBounds(left + voice_padding, top + height - knob_section_height - padding,
                           extra_mod_width, knob_section_height);

  int extra_mod_height = height - top_height - knob_section_height - padding - 1;
  extra_mod_section_->setBounds(left + voice_padding, top + top_height, extra_mod_width, extra_mod_height);

  int keyboard_height = keyboard_section_height - voice_padding - padding;
  int keyboard_x = extra_mod_section_->getRight() + voice_padding;
  int keyboard_width = modulation_interface_->getRight() - keyboard_x;
  keyboard_interface_->setBounds(keyboard_x, top + height - keyboard_height - padding, keyboard_width, keyboard_height);

  save_section_->setBounds(bounds);
  SynthSection::resized();

  modulation_manager_->setBounds(bounds);
  
  wavetable_edit_bounds_ = Rectangle<int>(left, 0, width, height);
  for (int i = 0; i < vital::kNumOscillators; ++i) {
    if (wavetable_edits_[i])
      wavetable_edits_[i]->setBounds(wavetable_edit_bounds_);
  }

  if (synthesis_interface_) {
    for (int i = 0; i < vital::kNumOscillators; ++i)
      master_controls_interface_->setOscillatorBounds(i, synthesis_interface_->getOscillatorBounds(i));
  }
  master_controls_interface_->setBounds(main_bounds);

  if (full_screen_section_) {
    Rectangle<float> relative = synthesis_interface_->getOscillatorSection(0)->getWavetableRelativeBounds();
    int total_width = getWidth() / relative.getWidth();
    full_screen_section_->setBounds(-total_width * relative.getX(), 0, total_width, getHeight());
  }

  if (getWidth() && getHeight())
    redoBackground();
}

void FullInterface::setOscilloscopeMemory(const vital::poly_float* memory) {
  if (header_)
    header_->setOscilloscopeMemory(memory);
  if (master_controls_interface_)
    master_controls_interface_->setOscilloscopeMemory(memory);
}

void FullInterface::setAudioMemory(const vital::StereoMemory* memory) {
  if (header_)
    header_->setAudioMemory(memory);
  if (master_controls_interface_)
    master_controls_interface_->setAudioMemory(memory);
}

void FullInterface::createModulationSliders(const vital::output_map& mono_modulations,
                                            const vital::output_map& poly_modulations) {
  std::map<std::string, SynthSlider*> all_sliders = getAllSliders();
  std::map<std::string, SynthSlider*> modulatable_sliders;

  for (auto& destination : mono_modulations) {
    if (all_sliders.count(destination.first))
      modulatable_sliders[destination.first] = all_sliders[destination.first];
  }

  modulation_manager_ = std::make_unique<ModulationManager>(getAllModulationButtons(),
                                                            modulatable_sliders, mono_modulations, poly_modulations);
  modulation_manager_->setOpaque(false);
  addSubSection(modulation_manager_.get());
}

void FullInterface::animate(bool animate) {
  animate_ = animate;
  wakeUp();
  SynthSection::animate(animate);
}

void FullInterface::reset() {
  ScopedLock lock(open_gl_critical_section_);

  if (modulation_interface_)
    modulation_interface_->reset();

  setting_all_values_ = true;
  SynthSection::reset();
  modulationChanged();
  if (effects_interface_ && effects_interface_->isVisible())
    effects_interface_->redoBackgroundImage();

  setWavetableNames();
  setting_all_values_ = false;
  repaintSynthesisSection();
}

void FullInterface::setAllValues(vital::control_map& controls) {
  ScopedLock lock(open_gl_critical_section_);
  setting_all_values_ = true;
  SynthSection::setAllValues(controls);
  setting_all_values_ = false;
}

void FullInterface::setWavetableNames() {
  for (int i = 0; i < vital::kNumOscillators; ++i) {
    if (wavetable_creators_[i])
      synthesis_interface_->setWavetableName(i, wavetable_creators_[i]->getName());
  }
}

//==================================================================================================
// Vita: frames. The editor's paint() draws the frame straight into the card: the background image
// (the sections' static parts, drawn at the resolution the card is shown at), then every visible
// component's render(), as Vital's OpenGL loop did, now with juce::Graphics. A timer repaints at
// kFrameRate while something moves (the synth sounds, the mouse is about, a fade runs out) and stops
// when the interface is still.
//==================================================================================================
void FullInterface::paintFrame(Graphics& g) {
  double frame_start = Time::getMillisecondCounterHiRes();
  float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
  if (scale > 0.0f && std::abs(scale - render_scale_) > 0.02f * render_scale_)
    settleRenderScale(scale);

  ScopedLock lock(open_gl_critical_section_);
  if (!background_image_.isValid())
    redoBackground();

  if (background_image_.isValid())
    g.drawImage(background_image_, getLocalBounds().toFloat());
  else
    g.fillAll(findColour(Skin::kBackground, true));

  OpenGlWrapper open_gl(g, &shaders_);
  open_gl.display_scale = display_scale_;
  if (!components_initialised_) {
    components_initialised_ = true;
    initOpenGlComponents(open_gl);
  }

  if (modulation_manager_)
    modulation_manager_->renderMeters(open_gl, animate_);
  renderOpenGlComponents(open_gl, animate_);
  open_gl.finish();

  last_frame_ms_ = 0.8 * last_frame_ms_ + 0.2 * (Time::getMillisecondCounterHiRes() - frame_start);
}

void FullInterface::settleRenderScale(float scale) {
  // the images are redrawn at the new resolution once the zoom stops changing
  if (std::abs(scale - pending_render_scale_) < 0.001f)
    return;
  pending_render_scale_ = scale;
  int generation = ++scale_change_generation_;
  Component::SafePointer<FullInterface> safe(this);
  Timer::callAfterDelay(250, [safe, generation] {
    if (safe == nullptr || generation != safe->scale_change_generation_)
      return;
    safe->render_scale_ = safe->pending_render_scale_;
    safe->redoBackground();
    safe->wakeUp();
  });
}

void FullInterface::wakeUp() {
  last_activity_ms_ = Time::getMillisecondCounter();
}

void FullInterface::mouseActivity() {
  wakeUp();
  if (Component* editor = getParentComponent())
    editor->repaint();
}

void FullInterface::timerCallback() {
  Component* editor = getParentComponent();
  if (editor == nullptr || !isShowing())
    return;

  uint32 now = Time::getMillisecondCounter();
  if (animate_) {
    SynthGuiInterface* gui = findParentComponentOfClass<SynthGuiInterface>();
    if (gui && gui->getSynth()) {
      const vital::StatusOutput* voices = gui->getSynth()->getStatusOutput("num_voices");
      const vital::StatusOutput* peak = gui->getSynth()->getStatusOutput("peak_meter");
      bool sounding = (voices && voices->value()[0] > 0.0f) || (peak && peak->value()[0] + peak->value()[1] > 1.0e-5f);
      if (sounding)
        last_sound_ms_ = now;
    }
  }

  // fades and meters run out for a while after the last movement
  static constexpr uint32 kStillAfterMs = 1500;
  if (now - last_activity_ms_ >= kStillAfterMs && now - last_sound_ms_ >= kStillAfterMs)
    return;

  // frames get rarer when drawing them is slow (a big zoom on a small device), so drawing never
  // takes more than about a third of the time
  static constexpr double kMaxDrawingShare = 0.33;
  double interval = std::max(1000.0 / kFrameRate, last_frame_ms_ / kMaxDrawingShare);
  if (now - last_repaint_ms_ + 2.0 < interval)
    return;
  last_repaint_ms_ = now;
  editor->repaint();
}

void FullInterface::mouseMove(const MouseEvent& e) { mouseActivity(); }
void FullInterface::mouseEnter(const MouseEvent& e) { mouseActivity(); }
void FullInterface::mouseExit(const MouseEvent& e) { mouseActivity(); }
void FullInterface::mouseDown(const MouseEvent& e) { mouseActivity(); }
void FullInterface::mouseDrag(const MouseEvent& e) { mouseActivity(); }
void FullInterface::mouseUp(const MouseEvent& e) { mouseActivity(); }
void FullInterface::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) {
  mouseActivity();
  if (e.eventComponent == this)
    SynthSection::mouseWheelMove(e, wheel);
}

void FullInterface::tabSelected(int index) {
  ScopedLock lock(open_gl_critical_section_);
  bool make_visible = true;

  if (synthesis_interface_)
    synthesis_interface_->setVisible(index == 0 && make_visible);

  effects_interface_->setVisible(index == 1 && make_visible);
  modulation_matrix_->setVisible(index == 2 && make_visible);
  master_controls_interface_->setVisible(index == 3 && make_visible);
  modulation_manager_->setModulationAmounts();
  modulation_manager_->resized();
  modulation_manager_->setVisibleMeterBounds();
  modulation_manager_->hideUnusedHoverModulations();
  redoBackground();
}

void FullInterface::clearTemporaryTab(int current_tab) {
  ScopedLock lock(open_gl_critical_section_);
  modulation_interface_->setVisible(true);
  portamento_section_->setVisible(true);
  voice_section_->setVisible(true);
  tabSelected(current_tab);
}

void FullInterface::effectsMoved() {
  modulation_manager_->setVisibleMeterBounds();
}

void FullInterface::modulationsScrolled() {
  modulation_manager_->setVisibleMeterBounds();
}

void FullInterface::setFocus() {
  if (synthesis_interface_ && synthesis_interface_->isShowing())
    synthesis_interface_->setFocus();
}

void FullInterface::notifyChange() {
  if (header_)
    header_->notifyChange();
}

void FullInterface::notifyFresh() {
  if (header_)
    header_->notifyFresh();
}

void FullInterface::showFullScreenSection(SynthSection* full_screen) {
  ScopedLock lock(open_gl_critical_section_);
  full_screen_section_ = full_screen;

  if (full_screen_section_) {
    addSubSection(full_screen_section_);
    full_screen_section_->setBounds(getLocalBounds());
  }

  for (int i = 0; i < vital::kNumOscillators; ++i) {
    if (wavetable_edits_[i])
      wavetable_edits_[i]->setVisible(false);
  }

  bool show_rest = full_screen == nullptr;
  header_->setVisible(show_rest);
  synthesis_interface_->setVisible(show_rest);
  modulation_interface_->setVisible(show_rest);
  keyboard_interface_->setVisible(show_rest);
  extra_mod_section_->setVisible(show_rest);
  modulation_manager_->setVisible(show_rest);
  voice_section_->setVisible(show_rest);
  bend_section_->setVisible(show_rest);
  portamento_section_->setVisible(show_rest);
  redoBackground();
}

WavetableEditSection* FullInterface::wavetableEditor(int index) {
  if (index < 0 || index >= vital::kNumOscillators || wavetable_creators_[index] == nullptr)
    return nullptr;

  if (wavetable_edits_[index] == nullptr) {
    ScopedLock lock(open_gl_critical_section_);
    wavetable_edits_[index] = std::make_unique<WavetableEditSection>(index, wavetable_creators_[index]);
    WavetableEditSection* editor = wavetable_edits_[index].get();
    addSubSection(editor);
    editor->setVisible(false);
    if (synthesis_interface_)
      editor->setWaveFrameSlider(synthesis_interface_->getWaveFrameSlider(index));
    editor->setSkinValues(skin_, false);
    editor->setBounds(wavetable_edit_bounds_);
    editor->reset();
    components_initialised_ = false;
  }
  return wavetable_edits_[index].get();
}

void FullInterface::showWavetableEditSection(int index) {
  if (index >= 0 && wavetableEditor(index) == nullptr)
    return;

  ScopedLock lock(open_gl_critical_section_);
  for (int i = 0; i < vital::kNumOscillators; ++i) {
    if (wavetable_edits_[i])
      wavetable_edits_[i]->setVisible(i == index);
  }

  bool show_rest = index < 0;
  header_->setVisible(show_rest);
  synthesis_interface_->setVisible(show_rest);
  modulation_interface_->setVisible(show_rest);
  keyboard_interface_->setVisible(show_rest);
  extra_mod_section_->setVisible(show_rest);
  modulation_manager_->setVisible(show_rest);
  voice_section_->setVisible(show_rest);
  bend_section_->setVisible(show_rest);
  portamento_section_->setVisible(show_rest);
  redoBackground();
}

std::string FullInterface::getLastBrowsedWavetable(int index) {
  return wavetable_creators_[index] ? wavetable_creators_[index]->getLastFileLoaded() : "";
}

std::string FullInterface::getWavetableName(int index) {
  return wavetable_creators_[index] ? wavetable_creators_[index]->getName() : "";
}

void FullInterface::hideWavetableEditSection() {
  showWavetableEditSection(-1);
}

void FullInterface::loadWavetableFile(int index, const File& wavetable) {
  if (WavetableEditSection* editor = wavetableEditor(index))
    editor->loadFile(wavetable);
}

void FullInterface::loadWavetable(int index, json& wavetable_data) {
  if (WavetableEditSection* editor = wavetableEditor(index))
    editor->loadWavetable(wavetable_data);
}

void FullInterface::loadDefaultWavetable(int index) {
  if (WavetableEditSection* editor = wavetableEditor(index))
    editor->loadDefaultWavetable();
}

void FullInterface::resynthesizeToWavetable(int index) {
  if (WavetableEditSection* editor = wavetableEditor(index))
    editor->resynthesizeToWavetable();
}

void FullInterface::saveWavetable(int index) {
  save_section_->setIsPreset(false);
  save_section_->setSaveBounds();
  save_section_->setFileExtension(vital::kWavetableExtension);
  save_section_->setFileType("Wavetable");
  save_section_->setFileData(getWavetableJson(index));
  save_section_->setVisible(true);
}

void FullInterface::saveLfo(const json& data) {
  save_section_->setIsPreset(false);
  save_section_->setFileExtension(vital::kLfoExtension);
  save_section_->setFileType("LFO");
  save_section_->setFileData(data);
  save_section_->setVisible(true);
}

json FullInterface::getWavetableJson(int index) {
  if (wavetable_creators_[index])
    return wavetable_creators_[index]->stateToJson();
  return json();
}

bool FullInterface::loadAudioAsWavetable(int index, const String& name, InputStream* audio_stream,
                                         WavetableCreator::AudioFileLoadStyle style) {
  if (WavetableEditSection* editor = wavetableEditor(index))
    return editor->loadAudioAsWavetable(name, audio_stream, style);

  delete audio_stream;
  return true;
}

namespace {
  // Vita's menus are JUCE's own PopupMenu, in the app's own look and size like every
  // other menu of Aquanode Modular: drawn by their own window at the screen's pixel scale.
  // Vital's items -> a menu; ids of the menu are positions in `ids` + 1 (0 is "dismissed",
  // while 0 is a real id for Vital)
  void addVitaItems(PopupMenu& menu, const PopupItems& items, std::vector<int>& ids) {
    for (const auto& item : items.items) {
      if (item.id < 0 && item.items.empty()) {
        if (item.name.empty() || item.name == "-")
          menu.addSeparator();
        else
          menu.addSectionHeader(item.name);
        continue;
      }
      if (!item.items.empty()) {
        PopupMenu sub;
        addVitaItems(sub, item, ids);
        menu.addSubMenu(item.name, sub, true, nullptr, item.selected);
        continue;
      }
      ids.push_back(item.id);
      menu.addItem((int)ids.size(), item.name, true, item.selected);
    }
  }

  void showVitaMenu(Component* source, Point<int> position, const PopupItems& options,
                    std::function<void(int)> callback, std::function<void()> cancel) {
    if (source == nullptr || options.items.empty())
      return;
    PopupMenu menu;
    auto ids = std::make_shared<std::vector<int>>();
    addVitaItems(menu, options, *ids);

    // below the component when asked for at its bottom left, at the point otherwise
    Rectangle<int> local(position.x, position.y, 1, 1);
    if (position.y >= source->getHeight() - 1)
      local = Rectangle<int>(position.x, 0, std::max(1, source->getWidth() - position.x), source->getHeight());
    auto screen = source->localAreaToGlobal(local);

    auto opts = PopupMenu::Options().withTargetScreenArea(screen);
    Component::SafePointer<Component> safe(source);
    menu.showMenuAsync(opts, [safe, ids, callback, cancel](int result) {
      if (safe == nullptr)
        return;
      if (result <= 0 || result > (int)ids->size()) {
        if (cancel) cancel();
        return;
      }
      if (callback) callback((*ids)[result - 1]);
    });
  }
}

void FullInterface::popupSelector(Component* source, Point<int> position, const PopupItems& options,
                                  std::function<void(int)> callback, std::function<void()> cancel) {
  showVitaMenu(source, position, options, callback, cancel);
}

void FullInterface::dualPopupSelector(Component* source, Point<int> position, int width,
                                      const PopupItems& options, std::function<void(int)> callback) {
  showVitaMenu(source, position, options, callback, nullptr);
}

void FullInterface::popupDisplay(Component* source, const std::string& text,
                                 BubbleComponent::BubblePlacement placement, bool primary) {
  PopupDisplay* display = primary ? popup_display_1_.get() : popup_display_2_.get();
  display->setContent(text, getLocalArea(source, source->getLocalBounds()), placement);
  display->setVisible(true);
}

void FullInterface::hideDisplay(bool primary) {
  PopupDisplay* display = primary ? popup_display_1_.get() : popup_display_2_.get();
  if (display)
    display->setVisible(false);
}

void FullInterface::modulationChanged() {
  if (modulation_matrix_)
    modulation_matrix_->updateModulations();
  if (modulation_interface_)
    modulation_interface_->checkNumShown();
  if (modulation_manager_)
    modulation_manager_->reset();
}

void FullInterface::modulationValueChanged(int index) {
  if (modulation_matrix_)
    modulation_matrix_->updateModulationValue(index);
  if (modulation_manager_)
    modulation_manager_->setModulationAmounts();
}

void FullInterface::toggleOscillatorZoom(int index) {
  if (full_screen_section_)
    showFullScreenSection(nullptr);
  else
    showFullScreenSection(synthesis_interface_->getOscillatorSection(index));
}

void FullInterface::toggleFilter1Zoom() {
  if (full_screen_section_)
    showFullScreenSection(nullptr);
  else
    showFullScreenSection(synthesis_interface_->getFilterSection1());
}

void FullInterface::toggleFilter2Zoom() {
  if (full_screen_section_)
    showFullScreenSection(nullptr);
  else
    showFullScreenSection(synthesis_interface_->getFilterSection2());
}
