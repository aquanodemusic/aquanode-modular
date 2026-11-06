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

#include "bar_renderer.h"

#include "skin.h"
#include "shaders.h"
#include "utils.h"

BarRenderer::BarRenderer(int num_points, bool vertical) :
    vertical_(vertical), additive_blending_(true), display_scale_(1.0f), power_scale_(false),
    square_scale_(false), dirty_(false), num_points_(num_points), total_points_(num_points) {
  addRoundedCorners();

  scale_ = 1.0f;
  offset_ = 0.0f;
  bar_data_ = std::make_unique<float[]>(kFloatsPerBar * total_points_);
  bar_indices_ = std::make_unique<int[]>(kTriangleIndicesPerBar * total_points_);
  bar_corner_data_ = std::make_unique<float[]>(kCornerFloatsPerBar * total_points_);
  bar_width_ = 1.0f;

  for (int i = 0; i < total_points_; ++i) {
    float t = i / (total_points_ * 1.0f);
    int index = i * kFloatsPerBar;

    for (int v = 0; v < kVerticesPerBar; ++v) {
      int vertex_index = v * kFloatsPerVertex + index;
      bar_data_[vertex_index] = 2.0f * t - 1.0f;
      bar_data_[vertex_index + 1] = -1.0f;
    }

    int bar_index = i * kTriangleIndicesPerBar;
    int bar_vertex_index = i * kVerticesPerBar;
    bar_indices_[bar_index] = bar_vertex_index;
    bar_indices_[bar_index + 1] = bar_vertex_index + 1;
    bar_indices_[bar_index + 2] = bar_vertex_index + 2;
    bar_indices_[bar_index + 3] = bar_vertex_index + 1;
    bar_indices_[bar_index + 4] = bar_vertex_index + 2;
    bar_indices_[bar_index + 5] = bar_vertex_index + 3;

    int corner_index = i * kCornerFloatsPerBar;
    bar_corner_data_[corner_index] = 0.0f;
    bar_corner_data_[corner_index + 1] = 1.0f;
    bar_corner_data_[corner_index + 2] = 1.0f;
    bar_corner_data_[corner_index + 3] = 1.0f;
    bar_corner_data_[corner_index + 4] = 0.0f;
    bar_corner_data_[corner_index + 5] = 0.0f;
    bar_corner_data_[corner_index + 6] = 1.0f;
    bar_corner_data_[corner_index + 7] = 0.0f;
  }
}

BarRenderer::~BarRenderer() { }

void BarRenderer::init(OpenGlWrapper& open_gl) {
  OpenGlComponent::init(open_gl);
}

// Vita: each bar is a rectangle, placed as Vital's bar vertex shaders placed it
void BarRenderer::drawBars(OpenGlWrapper& open_gl) {
  if (!setViewPort(open_gl))
    return;

  if (dirty_) {
    dirty_ = false;
    setBarSizes();
  }

  float min_width = 4.0f / getWidth();
  float width_percent = std::max(min_width, bar_width_ * scale_ * 2.0f / num_points_);

  RectangleList<float> bars;
  for (int i = 0; i < total_points_; ++i) {
    const float* bar = bar_data_.get() + i * kFloatsPerBar;
    const float* corners = bar_corner_data_.get() + i * kCornerFloatsPerBar;
    float left = 2.0f, right = -2.0f, bottom = 2.0f, top = -2.0f;
    for (int v = 0; v < kVerticesPerBar; ++v) {
      const float* position = bar + v * kFloatsPerVertex;
      const float* corner = corners + v * kCornerFloatsPerVertex;
      float x = scale_ * (position[0] + 1.0f) - 1.0f;
      float y = position[1];
      if (vertical_)
        x += offset_ + width_percent * corner[0];
      else
        y += offset_ - width_percent * corner[1];
      left = std::min(left, x);
      right = std::max(right, x);
      bottom = std::min(bottom, y);
      top = std::max(top, y);
    }

    Rectangle<float> rect = Rectangle<float>::leftTopRightBottom(open_gl.x(left), open_gl.y(top),
                                                                 open_gl.x(right), open_gl.y(bottom));
    if (!rect.isEmpty())
      bars.addWithoutMerging(rect);
  }

  open_gl.g.setColour(color_);
  open_gl.g.fillRectList(bars);
}

void BarRenderer::render(OpenGlWrapper& open_gl, bool animate) {
  drawBars(open_gl);
}

void BarRenderer::destroy(OpenGlWrapper& open_gl) {
  OpenGlComponent::destroy(open_gl);
}

void BarRenderer::setBarSizes() {
  if (vertical_) {
    for (int i = 0; i < total_points_; ++i) {
      int index = i * kFloatsPerBar;
      float height = fabsf(yAt(i) - bottomAt(i)) * 0.5f * display_scale_;

      bar_data_[index + 2] = height;
      bar_data_[index + kFloatsPerVertex + 2] = height;
      bar_data_[index + 2 * kFloatsPerVertex + 2] = height;
      bar_data_[index + 3 * kFloatsPerVertex + 2] = height;
    }
  }
  else {
    for (int i = 0; i < total_points_; ++i) {
      int index = i * kFloatsPerBar;
      float width = fabsf(xAt(i) - rightAt(i)) * 0.5f * display_scale_;

      bar_data_[index + 2] = width;
      bar_data_[index + kFloatsPerVertex + 2] = width;
      bar_data_[index + 2 * kFloatsPerVertex + 2] = width;
      bar_data_[index + 3 * kFloatsPerVertex + 2] = width;
    }
  }
}

void BarRenderer::setPowerScale(bool power_scale) {
  if (power_scale == power_scale_)
    return;

  bool old_power_scale = power_scale_;
  for (int i = 1; i < num_points_; ++i) {
    power_scale_ = old_power_scale;
    float old_y = scaledYAt(i);
    power_scale_ = power_scale;
    setScaledY(i, old_y);
  }

  dirty_ = true;
}

void BarRenderer::setSquareScale(bool square_scale) {
  if (square_scale == square_scale_)
    return;

  bool old_square_scale = square_scale_;
  for (int i = 0; i < num_points_; ++i) {
    square_scale_ = old_square_scale;
    float old_y = scaledYAt(i);
    square_scale_ = square_scale;
    setScaledY(i, old_y);
  }

  dirty_ = true;
}
