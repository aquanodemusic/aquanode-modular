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

#include "spectral_warp_editor.h"

#include "fonts.h"
#include "line_generator.h"
#include "open_gl_image_component.h"
#include "open_gl_multi_quad.h"
#include "paths.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_slider.h"

SpectralWarpEditor::SpectralWarpEditor(int index, LineGenerator* curve) :
    SynthSection("spectral_warp_" + String(index + 1)), index_(index) {
  editor_ = std::make_unique<LineEditor>(curve);
  editor_->addListener(this);
  editor_->setFill(true);
  addOpenGlComponent(editor_.get());
  addOpenGlComponent(editor_->getTextEditorComponent());

  std::string number = std::to_string(index + 1);
  phase_ = std::make_unique<SynthSlider>("osc_" + number + "_spectral_morph_phase");
  addSlider(phase_.get());
  phase_->setSliderStyle(Slider::LinearBar);
  phase_->setPopupPrefix("Spectral Phase: ");
  phase_->setModulationPlacement(BubbleComponent::above);

  close_button_ = std::make_unique<OpenGlShapeButton>("close");
  addAndMakeVisible(close_button_.get());
  addOpenGlComponent(close_button_->getGlComponent());
  close_button_->addListener(this);
  close_button_->setShape(Paths::exitX());

  title_ = std::make_unique<PlainTextComponent>("title", "OSC " + String(index + 1) + " SPECTRAL WARP");
  addOpenGlComponent(title_.get());
  title_->setFontType(PlainTextComponent::kLight);
  title_->setJustification(Justification::centredLeft);

  setSkinOverride(Skin::kOscillator);
  setWantsKeyboardFocus(true);
}

SpectralWarpEditor::~SpectralWarpEditor() = default;

void SpectralWarpEditor::paintBackground(Graphics& g) {
  // Opaque panel: it is drawn on top of the neighbouring oscillator.
  g.setColour(findColour(Skin::kBackground, true).withAlpha(1.0f));
  g.fillRect(getLocalBounds());
  g.setColour(findColour(Skin::kBody, true).withAlpha(1.0f));
  g.fillRoundedRectangle(getLocalBounds().toFloat(), findValue(Skin::kBodyRounding));
  paintContainer(g);
  paintBorder(g);
  title_->setColor(findColour(Skin::kBodyText, true));
  close_button_->setColour(Skin::kIconButtonOff, findColour(Skin::kUiButton, true));
  close_button_->setColour(Skin::kIconButtonOffHover, findColour(Skin::kUiButtonHover, true));
  close_button_->setColour(Skin::kIconButtonOffPressed, findColour(Skin::kUiButtonPressed, true));
  editor_->setColor(findColour(Skin::kWidgetPrimary1, true));
  editor_->setFillColors(findColour(Skin::kWidgetSecondary1, true), findColour(Skin::kWidgetSecondary2, true));
  paintChildrenBackgrounds(g);
}

void SpectralWarpEditor::resized() {
  int widget_margin = findValue(Skin::kWidgetMargin);
  int title_height = getTitleWidth();
  int slider_width = getSliderWidth();
  int slider_overlap = getSliderOverlap();

  title_->setBounds(widget_margin, 0, getWidth() - title_height - widget_margin, title_height);
  title_->setTextSize(title_height * 0.5f);
  close_button_->setBounds(getWidth() - title_height, 0, title_height, title_height);

  int editor_height = getHeight() - title_height - slider_width + slider_overlap;
  editor_->setBounds(widget_margin, title_height, getWidth() - 2 * widget_margin, editor_height);
  editor_->setSizeRatio(getSizeRatio());
  phase_->setBounds(0, editor_->getBottom() - slider_overlap, getWidth(), slider_width);
  SynthSection::resized();
}

void SpectralWarpEditor::buttonClicked(Button* clicked_button) {
  if (clicked_button == close_button_.get()) {
    setVisible(false);
    for (Listener* listener : listeners_)
      listener->spectralWarpEditorClosed(this);
  }
  else
    SynthSection::buttonClicked(clicked_button);
}

Slider* SpectralWarpEditor::getPhaseSlider() {
  return phase_.get();
}
