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

#include "open_gl_image.h"

#include "open_gl_component.h"
#include "shaders.h"
#include "utils.h"

namespace {
  constexpr int kNumPositions = 16;
} // namespace

// Vita: the image is drawn with juce::Graphics. As with Vital's texture, the positions and texture
// coordinates refer to the image put in a power-of-two texture, its top row at the texture's top.

OpenGlImage::OpenGlImage() : dirty_(true), image_(nullptr), image_width_(0), image_height_(0), 
                             additive_(false), use_alpha_(false), scissor_(false) {
  position_vertices_ = std::make_unique<float[]>(kNumPositions);
  float position_vertices[kNumPositions] = {
    0.0f, 1.0f, 0.0f, 1.0f,
    0.0f, -1.0f, 0.0f, 0.0f,
    0.1f, -1.0f, 1.0f, 0.0f,
    0.1f, 1.0f, 1.0f, 1.0f
  };
  memcpy(position_vertices_.get(), position_vertices, kNumPositions * sizeof(float));
}

OpenGlImage::~OpenGlImage() { }

void OpenGlImage::init(OpenGlWrapper& open_gl) { }

void OpenGlImage::drawImage(OpenGlWrapper& open_gl) {
  mutex_.lock();
  if (image_) {
    texture_ = *image_;
    image_ = nullptr;
  }
  mutex_.unlock();

  if (!texture_.isValid())
    return;

  const float* v = position_vertices_.get();
  // v[0..3] top left, v[4..7] bottom left, v[8..11] bottom right, v[12..15] top right: x, y, u, v
  float x0 = open_gl.x(v[0]), x1 = open_gl.x(v[8]);
  float y0 = open_gl.y(v[1]), y1 = open_gl.y(v[5]);
  float u0 = v[2], u1 = v[10];
  float t0 = v[3], t1 = v[7];
  if (u1 == u0 || t1 == t0)
    return;

  int texture_width = vital::utils::nextPowerOfTwo(texture_.getWidth());
  int texture_height = vital::utils::nextPowerOfTwo(texture_.getHeight());
  // the image's corners in texture coordinates: (0, 1) at its top left
  float image_u1 = texture_.getWidth() / (1.0f * texture_width);
  float image_v0 = 1.0f - texture_.getHeight() / (1.0f * texture_height);

  auto x_at = [&](float u) { return x0 + (u - u0) / (u1 - u0) * (x1 - x0); };
  auto y_at = [&](float t) { return y0 + (t - t0) / (t1 - t0) * (y1 - y0); };
  Rectangle<float> dest = Rectangle<float>::leftTopRightBottom(x_at(0.0f), y_at(1.0f), x_at(image_u1), y_at(image_v0));
  if (dest.isEmpty())
    return;

  Graphics& g = open_gl.g;
  AffineTransform transform = AffineTransform::scale(dest.getWidth() / texture_.getWidth(),
                                                     dest.getHeight() / texture_.getHeight())
                                              .translated(dest.getX(), dest.getY());
  bool tinted = color_.getRed() != 255 || color_.getGreen() != 255 || color_.getBlue() != 255;
  if (tinted) {
    g.setColour(color_);
    g.drawImageTransformed(texture_, transform, true);
  }
  else {
    g.setOpacity(color_.getFloatAlpha());
    g.drawImageTransformed(texture_, transform, false);
  }
}

void OpenGlImage::destroy(OpenGlWrapper& open_gl) {
  texture_ = Image();
}
