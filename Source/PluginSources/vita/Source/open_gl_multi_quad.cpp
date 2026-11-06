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

#include "open_gl_multi_quad.h"

#include "common.h"
#include "shaders.h"

// Vita: the quads are drawn with juce::Graphics. Each of Vital's quad fragment shaders (shaders.cpp in
// Vital) becomes the shapes it drew: a quad's position, size, coordinates and shader values mean what
// they meant to the shader.

namespace {
  constexpr float kPi = 3.14159265359f;

  Colour opaque(Colour c) { return c.withAlpha(1.0f); }

  // GL's "the alt colour is unset" convention
  Colour orColor(Colour alt, Colour color) {
    return alt.getAlpha() ? alt : color.withAlpha(0.0f);
  }

  Path diamond(Rectangle<float> r) {
    Path p;
    p.startNewSubPath(r.getCentreX(), r.getY());
    p.lineTo(r.getRight(), r.getCentreY());
    p.lineTo(r.getCentreX(), r.getBottom());
    p.lineTo(r.getX(), r.getCentreY());
    p.closeSubPath();
    return p;
  }

  // a band of a ring, between two angles (clockwise from 12 o'clock)
  void fillArcBand(Graphics& g, Rectangle<float> circle, float from, float to, float inner_proportion) {
    if (to < from)
      std::swap(from, to);
    if (to - from <= 0.0001f)
      return;
    Path p;
    p.addPieSegment(circle, from, to, inner_proportion);
    g.fillPath(p);
  }

  void strokeArc(Graphics& g, Point<float> centre, float radius, float from, float to, float thickness) {
    if (to < from)
      std::swap(from, to);
    Path p;
    if (to - from <= 0.0001f) {
      Point<float> point = centre.getPointOnCircumference(radius, from);
      p.addEllipse(point.x - thickness * 0.5f, point.y - thickness * 0.5f, thickness, thickness);
      g.fillPath(p);
      return;
    }
    p.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, from, to, true);
    g.strokePath(p, PathStrokeType(thickness, PathStrokeType::beveled, PathStrokeType::rounded));
  }
}

OpenGlMultiQuad::OpenGlMultiQuad(int max_quads, Shaders::FragmentShader shader) :
    target_component_(nullptr), scissor_component_(nullptr), fragment_shader_(shader),
    max_quads_(max_quads), num_quads_(max_quads), draw_when_not_visible_(false),
    active_(true), dirty_(false), max_arc_(2.0f), thumb_amount_(0.5f), start_pos_(0.0f), 
    current_alpha_mult_(1.0f), alpha_mult_(1.0f), additive_blending_(false),
    current_thickness_(1.0f), thickness_(1.0f), rounding_(5.0f) {
  static const int triangles[] = {
    0, 1, 2,
    2, 3, 0
  };

  data_ = std::make_unique<float[]>(max_quads_ * kNumFloatsPerQuad);
  indices_ = std::make_unique<int[]>(max_quads_ * kNumIndicesPerQuad);

  mod_color_ = Colours::transparentBlack;

  for (int i = 0; i < max_quads_; ++i) {
    setCoordinates(i, -1.0f, -1.0f, 2.0f, 2.0f);
    setShaderValue(i, 1.0f);

    for (int j = 0; j < kNumIndicesPerQuad; ++j)
      indices_[i * kNumIndicesPerQuad + j] = triangles[j] + i * kNumVertices;
  }

  setInterceptsMouseClicks(false, false);
}

OpenGlMultiQuad::~OpenGlMultiQuad() { }

void OpenGlMultiQuad::init(OpenGlWrapper& open_gl) { }

void OpenGlMultiQuad::destroy(OpenGlWrapper& open_gl) { }

void OpenGlMultiQuad::render(OpenGlWrapper& open_gl, bool animate) {
  Component* component = target_component_ ? target_component_ : this;
  if (!active_ || (!draw_when_not_visible_ && !component->isVisible()) || !setViewPort(component, open_gl))
    return;

  Component* scissor_component = scissor_component_;
  if (scissor_component)
    setScissor(scissor_component, open_gl);

  if (current_alpha_mult_ == 0.0f && alpha_mult_ == 0.0f)
    return;

  if (dirty_) {
    dirty_ = false;
    for (int i = 0; i < num_quads_; ++i)
      setDimensions(i, getQuadWidth(i), getQuadHeight(i), component->getWidth(), component->getHeight());
  }

  if (alpha_mult_ > current_alpha_mult_)
    current_alpha_mult_ = std::min(alpha_mult_, current_alpha_mult_ + kAlphaInc);
  else
    current_alpha_mult_ = std::max(alpha_mult_, current_alpha_mult_ - kAlphaInc);

  current_thickness_ = current_thickness_ + kThicknessDecay * (thickness_ - current_thickness_);

  for (int i = 0; i < num_quads_; ++i)
    drawQuad(open_gl, i, current_alpha_mult_);
}

void OpenGlMultiQuad::drawQuad(OpenGlWrapper& open_gl, int i, float alpha) {
  Graphics& g = open_gl.g;
  const float* bottom_left = data_.get() + i * kNumFloatsPerQuad;
  const float* top_left = bottom_left + kNumFloatsPerVertex;
  const float* top_right = top_left + kNumFloatsPerVertex;

  float left = open_gl.x(std::min(bottom_left[0], top_right[0]));
  float right = open_gl.x(std::max(bottom_left[0], top_right[0]));
  float top = open_gl.y(std::max(bottom_left[1], top_left[1]));
  float bottom = open_gl.y(std::min(bottom_left[1], top_left[1]));
  Rectangle<float> rect(left, top, right - left, bottom - top);
  if (rect.getWidth() <= 0.0f || rect.getHeight() <= 0.0f)
    return;

  drawShape(g, i, rect, alpha);
}

void OpenGlMultiQuad::drawShape(Graphics& g, int i, Rectangle<float> rect, float alpha) {
  const float* bottom_left = data_.get() + i * kNumFloatsPerQuad;
  const float* top_left = bottom_left + kNumFloatsPerVertex;
  const float* top_right = top_left + kNumFloatsPerVertex;
  const float* values = bottom_left + 6;
  Colour color = color_.withMultipliedAlpha(alpha);
  float thickness = current_thickness_;
  float radius = rect.getWidth() * 0.5f;
  Point<float> centre = rect.getCentre();

  switch (fragment_shader_) {
    case Shaders::kFadeSquareFragment:
      g.setColour(color.withMultipliedAlpha(values[0]));
      g.fillRect(rect);
      break;

    case Shaders::kCircleFragment:
      g.setColour(color);
      g.fillEllipse(Rectangle<float>(rect.getWidth(), rect.getWidth()).withCentre(centre));
      break;

    case Shaders::kRingFragment:
    case Shaders::kDiamondFragment: {
      bool ring = fragment_shader_ == Shaders::kRingFragment;
      Rectangle<float> outer = Rectangle<float>(rect.getWidth(), rect.getWidth()).withCentre(centre);
      Rectangle<float> inner = outer.reduced(std::min(radius, thickness + 1.0f));
      Colour inside = orColor(alt_color_, color_).withMultipliedAlpha(alpha);
      if (inside.getAlpha()) {
        g.setColour(inside);
        if (ring)
          g.fillEllipse(inner);
        else
          g.fillPath(diamond(inner));
      }
      Path band;
      if (ring) {
        band.addEllipse(outer);
        band.addEllipse(inner);
      }
      else {
        band.addPath(diamond(outer));
        band.addPath(diamond(inner));
      }
      band.setUsingNonZeroWinding(false);
      g.setColour(color);
      g.fillPath(band);
      break;
    }

    case Shaders::kRoundedCornerFragment: {
      // the corner square, but the quarter circle around the corner the coordinates are 0 at
      const float* c_bottom_left = bottom_left + 4;
      const float* c_top_left = top_left + 4;
      const float* c_top_right = top_right + 4;
      float dx = c_top_right[0] - c_top_left[0];
      float dy = c_bottom_left[1] - c_top_left[1];
      float u = dx != 0.0f ? -c_top_left[0] / dx : 0.0f;
      float v = dy != 0.0f ? -c_top_left[1] / dy : 0.0f;
      Point<float> corner(rect.getX() + u * rect.getWidth(), rect.getY() + v * rect.getHeight());
      float r = rect.getWidth();
      Path p;
      p.addRectangle(rect);
      p.addEllipse(corner.x - r, corner.y - r, 2.0f * r, 2.0f * r);
      p.setUsingNonZeroWinding(false);
      g.saveState();
      g.reduceClipRegion(rect.getSmallestIntegerContainer());
      g.setColour(color);
      g.fillPath(p);
      g.restoreState();
      break;
    }

    case Shaders::kRoundedRectangleFragment: {
      float r = std::min({ rounding_ * 0.5f, rect.getWidth() * 0.5f, rect.getHeight() * 0.5f });
      g.setColour(color);
      g.fillRoundedRectangle(rect, r);
      break;
    }

    case Shaders::kRoundedRectangleBorderFragment: {
      float r = std::min({ rounding_ * 0.5f, rect.getWidth() * 0.5f, rect.getHeight() * 0.5f });
      g.setColour(color);
      g.drawRoundedRectangle(rect.reduced(thickness * 0.5f), std::max(0.0f, r - thickness * 0.5f), thickness);
      break;
    }

    case Shaders::kRotarySliderFragment: {
      float center_arc = radius - thickness * 0.5f - 0.5f;
      float value = values[0];
      g.setColour(orColor(alt_color_, color_).withMultipliedAlpha(alpha));
      strokeArc(g, centre, center_arc, -max_arc_, max_arc_, thickness);
      float from = std::max(-max_arc_, std::min(start_pos_, value));
      float to = std::min(max_arc_, std::max(start_pos_, value));
      if (to > from) {
        g.setColour(color);
        strokeArc(g, centre, center_arc, from, to, thickness);
      }

      float thumb_length = radius * thumb_amount_;
      if (thumb_length > 0.0f && thumb_color_.getAlpha()) {
        Point<float> outer = centre.getPointOnCircumference(center_arc, value);
        Point<float> inner = centre.getPointOnCircumference(center_arc - thumb_length, value);
        g.setColour(thumb_color_.withMultipliedAlpha(alpha));
        Path thumb;
        thumb.startNewSubPath(outer);
        thumb.lineTo(inner);
        g.strokePath(thumb, PathStrokeType(thickness, PathStrokeType::beveled, PathStrokeType::rounded));
      }
      break;
    }

    case Shaders::kRotaryModulationFragment: {
      // [x, y] the left channel's range, [z, w] the right's, where they meet the mod colour
      Rectangle<float> circle = Rectangle<float>(rect.getWidth(), rect.getWidth()).withCentre(centre);
      float inner = radius > 0.0f ? std::max(0.0f, (radius - thickness) / radius) : 0.0f;
      float offset = -start_pos_;
      float x = values[0] + offset, y = values[1] + offset;
      float z = values[2] + offset, w = values[3] + offset;
      g.setColour(opaque(color_).withMultipliedAlpha(alpha));
      fillArcBand(g, circle, x, y, inner);
      g.setColour(opaque(alt_color_).withMultipliedAlpha(alpha));
      fillArcBand(g, circle, z, w, inner);
      float center_from = std::max(std::min(x, y), std::min(z, w));
      float center_to = std::min(std::max(x, y), std::max(z, w));
      if (center_to > center_from) {
        g.setColour(opaque(mod_color_).withMultipliedAlpha(alpha));
        fillArcBand(g, circle, center_from, center_to, inner);
      }
      break;
    }

    case Shaders::kHorizontalSliderFragment:
    case Shaders::kVerticalSliderFragment: {
      bool horizontal = fragment_shader_ == Shaders::kHorizontalSliderFragment;
      float adjusted_value = values[0] * 2.0f - 1.0f;
      Rectangle<float> track = horizontal ? rect.withSizeKeepingCentre(rect.getWidth(), thickness)
                                          : rect.withSizeKeepingCentre(thickness, rect.getHeight());
      float r = std::min({ rounding_ * 0.5f, track.getWidth() * 0.5f, track.getHeight() * 0.5f });
      Path track_path;
      track_path.addRoundedRectangle(track, r);

      auto position = [&](float coordinate) {
        if (horizontal)
          return rect.getX() + (coordinate + 1.0f) * 0.5f * rect.getWidth();
        return rect.getY() + (1.0f - coordinate) * 0.5f * rect.getHeight();
      };
      float a = position(start_pos_);
      float b = position(adjusted_value);
      Rectangle<float> between = horizontal ? Rectangle<float>(std::min(a, b), track.getY(), std::fabs(b - a), track.getHeight())
                                            : Rectangle<float>(track.getX(), std::min(a, b), track.getWidth(), std::fabs(b - a));

      // as Vital's shaders: horizontal fills the range in color, vertical in alt_color
      Colour range_color = horizontal ? color : orColor(alt_color_, color_).withMultipliedAlpha(alpha);
      Colour rest_color = horizontal ? orColor(alt_color_, color_).withMultipliedAlpha(alpha) : color;

      g.saveState();
      g.reduceClipRegion(track_path);
      g.setColour(rest_color);
      g.fillRect(track);
      g.setColour(range_color);
      g.fillRect(between);

      if (thumb_color_.getAlpha()) {
        float half = thumb_amount_ * 0.5f;
        Rectangle<float> thumb = horizontal ? Rectangle<float>(b - half, track.getY(), 2.0f * half, track.getHeight())
                                            : Rectangle<float>(track.getX(), b - half, track.getWidth(), 2.0f * half);
        g.setColour(thumb_color_.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(thumb, std::min({ r, thumb.getWidth() * 0.5f, thumb.getHeight() * 0.5f }));
      }
      g.restoreState();
      break;
    }

    case Shaders::kLinearModulationFragment: {
      // position 0 .. 1 runs along the coordinates' x, which may be rotated to run up the quad
      const float* c_bottom_left = bottom_left + 4;
      const float* c_top_left = top_left + 4;
      const float* c_top_right = top_right + 4;
      bool along_x = c_top_right[0] != c_top_left[0];
      auto span = [&](float from, float to) {
        if (to < from)
          std::swap(from, to);
        float c0 = 2.0f * from - 1.0f, c1 = 2.0f * to - 1.0f;
        if (along_x) {
          float d = c_top_right[0] - c_top_left[0];
          float x0 = rect.getX() + (c0 - c_top_left[0]) / d * rect.getWidth();
          float x1 = rect.getX() + (c1 - c_top_left[0]) / d * rect.getWidth();
          return Rectangle<float>(std::min(x0, x1), rect.getY(), std::fabs(x1 - x0), rect.getHeight());
        }
        float d = c_bottom_left[0] - c_top_left[0];
        float y0 = rect.getY() + (c0 - c_top_left[0]) / d * rect.getHeight();
        float y1 = rect.getY() + (c1 - c_top_left[0]) / d * rect.getHeight();
        return Rectangle<float>(rect.getX(), std::min(y0, y1), rect.getWidth(), std::fabs(y1 - y0));
      };
      Rectangle<float> mono = span(values[0], values[1]).getIntersection(rect);
      Rectangle<float> stereo = span(values[2], values[3]).getIntersection(rect);
      g.setColour(opaque(color_).withMultipliedAlpha(alpha));
      g.fillRect(mono);
      g.setColour(opaque(alt_color_).withMultipliedAlpha(alpha));
      g.fillRect(stereo);
      Rectangle<float> both = mono.getIntersection(stereo);
      if (!both.isEmpty()) {
        g.setColour(opaque(mod_color_).withMultipliedAlpha(alpha));
        g.fillRect(both);
      }
      break;
    }

    case Shaders::kModulationKnobFragment: {
      Rectangle<float> circle = Rectangle<float>(rect.getWidth(), rect.getWidth()).withCentre(centre);
      float split = kPi - values[0];
      g.setColour(orColor(alt_color_, color_).withMultipliedAlpha(alpha));
      fillArcBand(g, circle, 0.0f, split, 0.0f);
      g.setColour(color);
      fillArcBand(g, circle, split, 2.0f * kPi, 0.0f);

      if (thumb_color_.getAlpha()) {
        float center_arc = radius - thickness * 0.5f - 0.5f;
        g.setColour(thumb_color_.withMultipliedAlpha(alpha));
        g.drawEllipse(Rectangle<float>(2.0f * center_arc, 2.0f * center_arc).withCentre(centre), thickness);
      }

      if (mod_color_.getAlpha()) {
        float mod_alpha = mod_color_.getFloatAlpha();
        g.setColour(background_color_.withMultipliedAlpha(mod_alpha * alpha));
        g.fillEllipse(Rectangle<float>(0.96f * radius, 0.96f * radius).withCentre(centre));
        g.setColour(mod_color_.withMultipliedAlpha(mod_alpha * alpha));
        g.fillEllipse(Rectangle<float>(0.7f * radius, 0.7f * radius).withCentre(centre));
      }
      break;
    }

    case Shaders::kColorFragment:
    default:
      g.setColour(color);
      g.fillRect(rect);
      break;
  }
}
