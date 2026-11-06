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

#include "peak_meter_viewer.h"

#include "skin.h"
#include "shaders.h"
#include "synth_gui_interface.h"
#include "utils.h"

namespace {
  constexpr float kClampDecay = 0.014f;
  constexpr float kMinDb = -80.0f;
  constexpr float kMaxDb = 6.0f;
} // namespace;

PeakMeterViewer::PeakMeterViewer(bool left) : clamped_(0.0f), left_(left) {
  addRoundedCorners();
  peak_output_ = nullptr;
  peak_memory_output_ = nullptr;
  float position_vertices[kNumPositions] = {
    -1.0f, 1.0f,
    -1.0f, -1.0f,
    1.0f, -1.0f,
    1.0f, 1.0f,
  };
  memcpy(position_vertices_, position_vertices, kNumPositions * sizeof(float));
}

PeakMeterViewer::~PeakMeterViewer() { }

void PeakMeterViewer::resized() {
  if (peak_output_ == nullptr || peak_memory_output_ == nullptr) {
    SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
    peak_output_ = parent->getSynth()->getStatusOutput("peak_meter");
    peak_memory_output_ = parent->getSynth()->getStatusOutput("peak_meter_memory");
  }

  OpenGlComponent::resized();
}

void PeakMeterViewer::init(OpenGlWrapper& open_gl) {
  OpenGlComponent::init(open_gl);
}

void PeakMeterViewer::updateVertices() {
  if (peak_output_ == nullptr)
    return;

  float val = peak_output_->value()[left_ ? 0 : 1];
  if (val > 1.0f)
    clamped_ = 1.0f;

  float db = vital::utils::magnitudeToDb(val);
  float t = std::max((db - kMinDb) / (kMaxDb - kMinDb), 0.0f);
  t *= t;
  float position = vital::utils::interpolate(-1.0f, 1.0f, t);
  position_vertices_[0] = -1.0f;
  position_vertices_[2] = -1.0f;
  position_vertices_[4] = position;
  position_vertices_[6] = position;
}

void PeakMeterViewer::updateVerticesMemory() {
  if (peak_memory_output_ == nullptr)
    return;

  float val = peak_memory_output_->value()[left_ ? 0 : 1];
  if (val > 1.0f)
    clamped_ = 1.0f;

  float db = vital::utils::magnitudeToDb(val);
  float t = std::max((db - kMinDb) / (kMaxDb - kMinDb), 0.0f);
  t *= t;
  float position = vital::utils::interpolate(-1.0f, 1.0f, t);
  float width = 2.0f / getWidth();
  position_vertices_[0] = position - width;
  position_vertices_[2] = position - width;
  position_vertices_[4] = position;
  position_vertices_[6] = position;
}

void PeakMeterViewer::render(OpenGlWrapper& open_gl, bool animate) {
  if (!animate || peak_output_ == nullptr)
    return;

  if (!setViewPort(open_gl))
    return;

  if (clamped_ > 0.0f) {
    color_from_ = findColour(Skin::kWidgetAccent1, true);
    color_to_ = findColour(Skin::kWidgetAccent2, true);
  }
  else {
    color_from_ = findColour(Skin::kWidgetSecondary1, true);
    color_to_ = findColour(Skin::kWidgetSecondary2, true);
  }

  updateVertices();
  draw(open_gl);
  updateVerticesMemory();
  draw(open_gl);

  clamped_ = vital::utils::max(clamped_ - kClampDecay, 0.0f);

  float rounding = std::min(getHeight() / 3.0f, findValue(Skin::kWidgetRoundedCorner) * 0.5f);
  renderCorners(open_gl, animate, findColour(Skin::kBackground, true), rounding);
}

// Vita: the meter's bar, coloured from color_from at the meter's left to color_to at its right
void PeakMeterViewer::draw(OpenGlWrapper& open_gl) {
  float left = open_gl.x(std::min(position_vertices_[0], position_vertices_[4]));
  float right = open_gl.x(std::max(position_vertices_[0], position_vertices_[4]));
  if (right <= left)
    return;

  ColourGradient gradient(color_from_, 0.0f, 0.0f, color_to_, (float)getWidth(), 0.0f, false);
  open_gl.g.setGradientFill(gradient);
  open_gl.g.fillRect(Rectangle<float>(left, 0.0f, right - left, (float)getHeight()));
}

void PeakMeterViewer::destroy(OpenGlWrapper& open_gl) {
  OpenGlComponent::destroy(open_gl);
}

void PeakMeterViewer::paintBackground(Graphics& g) {
  float t = -kMinDb / (kMaxDb - kMinDb);
  t *= t;
  float x = getWidth() * t;
  Colour background = findColour(Skin::kWidgetBackground, true);
  g.setColour(background.interpolatedWith(findColour(Skin::kBackground, true), 0.5f));
  g.fillRect(x, 0.0f, getWidth() - x, 1.0f * getHeight());

  g.setColour(background);
  g.fillRect(0.0f, 0.0f, x, 1.0f * getHeight());

  g.setColour(findColour(Skin::kLightenScreen, true));
  g.fillRect((int)x, 0, 1, getHeight());
}
