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

#include "open_gl_line_renderer.h"

#include "shaders.h"
#include "utils.h"

namespace {
  constexpr float kLoopWidth = 2.001f;
  constexpr float kDefaultLineWidth = 7.0f;

  force_inline float inverseSqrt(float value) {
    static constexpr float kThreeHalves = 1.5f;
    int i;
    float x2, y;

    x2 = value * 0.5f;
    y = value;
    i = *(int *)&y;
    i = 0x5f3759df - (i >> 1);
    y = *(float *)&i;
    y = y * (kThreeHalves - (x2 * y * y));
    y = y * (kThreeHalves - (x2 * y * y));
    return y;
  }

  force_inline float inverseMagnitudeOfPoint(Point<float> point) {
    return inverseSqrt(point.x * point.x + point.y * point.y);
  }

  force_inline Point<float> normalize(Point<float> point) {
    return point * inverseMagnitudeOfPoint(point);
  }
}

OpenGlLineRenderer::OpenGlLineRenderer(int num_points, bool loop) :
    num_points_(num_points), boost_(0.0f), fill_(false), fill_center_(0.0f), fit_(false),
    boost_amount_(0.0f), fill_boost_amount_(0.0f), enable_backward_boost_(true),
    index_(0), dirty_(false), last_drawn_left_(false), loop_(loop), any_boost_value_(false) {
  addRoundedCorners();
  num_padding_ = 1;
  if (loop)
    num_padding_ = 2;

  num_line_vertices_ = kLineVerticesPerPoint * (num_points_ + 2 * num_padding_);
  num_fill_vertices_ = kFillVerticesPerPoint * (num_points_ + 2 * num_padding_);
  num_line_floats_ = kLineFloatsPerVertex * num_line_vertices_;
  num_fill_floats_ = kFillFloatsPerVertex * num_fill_vertices_;

  line_width_ = kDefaultLineWidth;

  x_ = std::make_unique<float[]>(num_points_);
  y_ = std::make_unique<float[]>(num_points_);
  boost_left_ = std::make_unique<float[]>(num_points_);
  boost_right_ = std::make_unique<float[]>(num_points_);
  last_negative_boost_ = false;

  for (int i = 0; i < num_points_; ++i)
    setXAt(i, 2.0f * i / (num_points_ - 1.0f) - 1.0f);
}

OpenGlLineRenderer::~OpenGlLineRenderer() { }

void OpenGlLineRenderer::init(OpenGlWrapper& open_gl) {
  OpenGlComponent::init(open_gl);
}

void OpenGlLineRenderer::boostLeftRange(float start, float end, int buffer_vertices, float min) {
  boostRange(boost_left_.get(), start, end, buffer_vertices, min);
}

void OpenGlLineRenderer::boostRightRange(float start, float end, int buffer_vertices, float min) {
  boostRange(boost_right_.get(), start, end, buffer_vertices, min);
}

void OpenGlLineRenderer::boostRange(float* boosts, float start, float end, int buffer_vertices, float min) {
  any_boost_value_ = true;
  dirty_ = true;
  
  int active_points = num_points_ - 2 * buffer_vertices;
  int start_index = std::max((int)std::ceil(start * (active_points - 1)), 0);
  float end_position = end * (active_points - 1);
  int end_index = std::max(std::ceil(end_position), 0.0f);
  float progress = end_position - (int)end_position;

  start_index %= active_points;
  end_index %= active_points;
  int num_points = end_index - start_index;
  int direction = 1;
  if (enable_backward_boost_) {
    if ((num_points < 0 && num_points > -num_points_ / 2) || (num_points == 0 && last_negative_boost_)) {
      num_points = -num_points;
      direction = -1;
    }
    else if (num_points > num_points_ / 2) {
      num_points -= active_points;
      num_points = -num_points;
      direction = -1;
    }
  }

  last_negative_boost_ = direction < 0;
  if (last_negative_boost_) {
    start_index = std::max((int)std::floor(start * (active_points - 1)), 0);
    end_index = std::max(std::floor(end_position), 0.0f);
    start_index %= active_points;
    end_index %= active_points;

    num_points = start_index - end_index;
    progress = 1.0f - progress;
  }

  float delta = (1.0f - min) / num_points;
  float val = min;

  for (int i = start_index; i != end_index; i = (i + active_points + direction) % active_points) {
    val += delta;
    val = std::min(1.0f, val);
    float last_value = boosts[i + buffer_vertices];
    boosts[i + buffer_vertices] = std::max(last_value, val);
  }

  float end_value = boosts[end_index + buffer_vertices];
  boosts[end_index + buffer_vertices] = std::max(end_value, progress * progress);
}

void OpenGlLineRenderer::boostRange(vital::poly_float start, vital::poly_float end,
                                    int buffer_vertices, vital::poly_float min) {
  boostLeftRange(start[0], end[0], buffer_vertices, min[0]);
  boostRightRange(start[1], end[1], buffer_vertices, min[1]);
}

void OpenGlLineRenderer::decayBoosts(vital::poly_float mult) {
  bool any_boost = false;
  for (int i = 0; i < num_points_; ++i) {
    boost_left_[i] *= mult[0];
    boost_right_[i] *= mult[1];
    any_boost = any_boost || boost_left_[i] || boost_right_[i];
  }
  
  any_boost_value_ = any_boost;
}

// Vita: the line is a stroked juce::Path, its fill a gradient down (or up) to the fill centre, as
// Vital's line and fill shaders drew them. Points closer together than a pixel are merged.

Point<float> OpenGlLineRenderer::transformPoint(int index, float x_scale, float y_scale) const {
  float half_width = getWidth() * 0.5f;
  float half_height = getHeight() * 0.5f;
  return { half_width + (x_[index] - half_width) * x_scale, half_height + (y_[index] - half_height) * y_scale };
}

void OpenGlLineRenderer::setFillVertices(bool left) { }

void OpenGlLineRenderer::setLineVertices(bool left) { }

void OpenGlLineRenderer::drawLines(OpenGlWrapper& open_gl, bool left) {
  if (num_points_ <= 0 || !setViewPort(open_gl))
    return;

  Graphics& g = open_gl.g;
  float width = getWidth();
  float height = getHeight();
  if (width <= 0.0f || height <= 0.0f)
    return;

  float x_scale = 1.0f;
  float y_scale = 1.0f;
  if (fit_) {
    x_scale = 1.0f - 0.33f * line_width_ / width;
    y_scale = 1.0f - 0.33f * line_width_ / height;
  }

  float* boosts = left ? boost_left_.get() : boost_right_.get();
  bool boosted = any_boost_value_ && (boost_amount_ > 0.0f || fill_boost_amount_ > 0.0f);
  float center_y = height * 0.5f * (1.0f - fill_center_ * y_scale);
  float half_width = line_width_ * 0.5f;

  // A line with more points than the display has pixel columns (a waveform, a sample) is drawn as the
  // band it covers in each column: much cheaper than stroking every point, and the same to the eye.
  float pixel_scale = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
  bool monotonic = true;
  for (int i = 1; i < num_points_ && monotonic; ++i)
    monotonic = x_[i] >= x_[i - 1];
  bool dense = monotonic && !loop_ && num_points_ >= width * pixel_scale;

  if (dense) {
    struct Column { float x, low, high, extreme, boost; };
    std::vector<Column> columns;
    columns.reserve((size_t)(width * pixel_scale) + 2);
    float previous_y = transformPoint(0, x_scale, y_scale).y;
    for (int i = 0; i < num_points_; ++i) {
      Point<float> point = transformPoint(i, x_scale, y_scale);
      float column_x = (std::floor(point.x * pixel_scale) + 0.5f) / pixel_scale;
      if (columns.empty() || columns.back().x != column_x) {
        // a new column starts where the line left the last one
        columns.push_back({ column_x, std::min(previous_y, point.y), std::max(previous_y, point.y), point.y, 0.0f });
      }
      Column& column = columns.back();
      column.low = std::min(column.low, point.y);
      column.high = std::max(column.high, point.y);
      if (std::fabs(point.y - center_y) > std::fabs(column.extreme - center_y))
        column.extreme = point.y;
      column.boost = std::max(column.boost, boosts[i]);
      previous_y = point.y;
    }

    float column_width = 1.0f / pixel_scale;
    if (fill_) {
      float top_y = height * 0.5f * (1.0f - y_scale);
      float bottom_y = height * 0.5f * (1.0f + y_scale);
      float span = std::max({ 1.0f, center_y - top_y, bottom_y - center_y });
      ColourGradient gradient(fill_color_to_, 0.0f, center_y - span, fill_color_to_, 0.0f, center_y + span, false);
      gradient.addColour(0.5, fill_color_from_);

      Path fill;
      fill.startNewSubPath(columns.front().x - 0.5f * column_width, center_y);
      for (const Column& column : columns) {
        fill.lineTo(column.x - 0.5f * column_width, column.extreme);
        fill.lineTo(column.x + 0.5f * column_width, column.extreme);
      }
      fill.lineTo(columns.back().x + 0.5f * column_width, center_y);
      fill.closeSubPath();
      g.setGradientFill(gradient);
      g.fillPath(fill);

      if (boosted && fill_boost_amount_ > 0.0f) {
        RectangleList<float> boosted_fill;
        float total = 0.0f;
        int count = 0;
        for (const Column& column : columns) {
          if (column.boost <= 0.01f)
            continue;
          float top = std::min(column.extreme, center_y);
          boosted_fill.addWithoutMerging({ column.x - 0.5f * column_width, top, column_width,
                                           std::fabs(column.extreme - center_y) });
          total += column.boost;
          ++count;
        }
        if (count) {
          g.setGradientFill(gradient);
          g.setOpacity(std::min(1.0f, fill_boost_amount_ * total / count));
          g.fillRectList(boosted_fill);
          g.setOpacity(1.0f);
        }
      }
    }

    Path band;
    band.startNewSubPath(columns.front().x - 0.5f * column_width, columns.front().low - half_width);
    for (const Column& column : columns)
      band.lineTo(column.x, column.low - half_width);
    band.lineTo(columns.back().x + 0.5f * column_width, columns.back().low - half_width);
    band.lineTo(columns.back().x + 0.5f * column_width, columns.back().high + half_width);
    for (auto column = columns.rbegin(); column != columns.rend(); ++column)
      band.lineTo(column->x, column->high + half_width);
    band.lineTo(columns.front().x - 0.5f * column_width, columns.front().high + half_width);
    band.closeSubPath();
    g.setColour(color_);
    g.fillPath(band);

    if (boosted && boost_amount_ > 0.0f) {
      RectangleList<float> boosted_line;
      for (const Column& column : columns) {
        if (column.boost <= 0.01f)
          continue;
        float extra = half_width * (1.0f + boost_amount_ * column.boost);
        boosted_line.addWithoutMerging({ column.x - 0.5f * column_width, column.low - extra, column_width,
                                         column.high - column.low + 2.0f * extra });
      }
      g.fillRectList(boosted_line);
    }
    return;
  }

  if (dirty_ || last_drawn_left_ != left || boosted) {
    dirty_ = false;
    last_drawn_left_ = left;

    line_path_.clear();
    float loop_offset = width * 1.0005f * x_scale;
    Point<float> last;
    bool started = false;
    auto add = [&](Point<float> point, bool force) {
      if (!started) {
        line_path_.startNewSubPath(point);
        started = true;
        last = point;
      }
      else if (force || std::fabs(point.x - last.x) + std::fabs(point.y - last.y) >= 0.6f) {
        line_path_.lineTo(point);
        last = point;
      }
    };

    if (loop_ && num_points_ >= 2) {
      add(transformPoint(num_points_ - 2, x_scale, y_scale) - Point<float>(loop_offset, 0.0f), true);
      add(transformPoint(num_points_ - 1, x_scale, y_scale) - Point<float>(loop_offset, 0.0f), true);
    }
    for (int i = 0; i < num_points_; ++i)
      add(transformPoint(i, x_scale, y_scale), i == num_points_ - 1);
    if (loop_ && num_points_ >= 2) {
      add(transformPoint(0, x_scale, y_scale) + Point<float>(loop_offset, 0.0f), true);
      add(transformPoint(1, x_scale, y_scale) + Point<float>(loop_offset, 0.0f), true);
    }

    if (fill_) {
      fill_path_ = line_path_;
      Rectangle<float> bounds = line_path_.getBounds();
      fill_path_.lineTo(bounds.getRight(), center_y);
      fill_path_.lineTo(bounds.getX(), center_y);
      fill_path_.closeSubPath();
    }
  }

  if (fill_) {
    // the fill colour runs from color_from at the centre to color_to a full height away
    float top_y = height * 0.5f * (1.0f - y_scale);
    float bottom_y = height * 0.5f * (1.0f + y_scale);
    float span = std::max({ 1.0f, center_y - top_y, bottom_y - center_y });
    ColourGradient gradient(fill_color_to_, 0.0f, center_y - span, fill_color_to_, 0.0f, center_y + span, false);
    gradient.addColour(0.5, fill_color_from_);
    g.setGradientFill(gradient);
    g.fillPath(fill_path_);

    if (boosted && fill_boost_amount_ > 0.0f) {
      // the boosted stretches brighter, as the fill shader multiplied their alpha
      for (int i = 0; i < num_points_ - 1; ++i) {
        float boost = 0.5f * (boosts[i] + boosts[i + 1]) * fill_boost_amount_;
        if (boost <= 0.01f)
          continue;
        Point<float> a = transformPoint(i, x_scale, y_scale);
        Point<float> b = transformPoint(i + 1, x_scale, y_scale);
        Path quad;
        quad.startNewSubPath(a);
        quad.lineTo(b);
        quad.lineTo(b.x, center_y);
        quad.lineTo(a.x, center_y);
        quad.closeSubPath();
        g.setGradientFill(gradient);
        g.setOpacity(std::min(1.0f, boost));
        g.fillPath(quad);
      }
      g.setOpacity(1.0f);
    }
  }

  g.setColour(color_);
  g.strokePath(line_path_, PathStrokeType(line_width_, PathStrokeType::beveled, PathStrokeType::rounded));

  if (boosted && boost_amount_ > 0.0f) {
    for (int i = 0; i < num_points_ - 1; ++i) {
      float boost = 0.5f * (boosts[i] + boosts[i + 1]);
      if (boost <= 0.01f)
        continue;
      Path segment;
      segment.startNewSubPath(transformPoint(i, x_scale, y_scale));
      segment.lineTo(transformPoint(i + 1, x_scale, y_scale));
      float thickness = line_width_ * (1.0f + boost_amount_ * boost);
      g.strokePath(segment, PathStrokeType(thickness, PathStrokeType::beveled, PathStrokeType::rounded));
    }
  }
}

void OpenGlLineRenderer::render(OpenGlWrapper& open_gl, bool animate) {
  drawLines(open_gl, true);
}

void OpenGlLineRenderer::destroy(OpenGlWrapper& open_gl) {
  OpenGlComponent::destroy(open_gl);
}
