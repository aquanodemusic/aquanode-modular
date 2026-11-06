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

#include "common.h"
#include <map>

// Vita draws with juce::Graphics (see vita_gfx.h). The names stay Vital's: a FragmentShader is how an
// OpenGlMultiQuad draws its quads, and the filter-response VertexShaders compute the curves of the
// filter displays on the CPU.
class Shaders {
  public:
    enum VertexShader {
      kImageVertex,
      kPassthroughVertex,
      kScaleVertex,
      kRotaryModulationVertex,
      kLinearModulationVertex,
      kGainMeterVertex,
      kAnalogFilterResponseVertex,
      kCombFilterResponseVertex,
      kPositiveFlangeFilterResponseVertex,
      kNegativeFlangeFilterResponseVertex,
      kDigitalFilterResponseVertex,
      kDiodeFilterResponseVertex,
      kDirtyFilterResponseVertex,
      kFormantFilterResponseVertex,
      kLadderFilterResponseVertex,
      kPhaserFilterResponseVertex,
      kEqFilterResponseVertex,
      kLineVertex,
      kFillVertex,
      kBarHorizontalVertex,
      kBarVerticalVertex,
      kNumVertexShaders
    };

    enum FragmentShader {
      kImageFragment,
      kTintedImageFragment,
      kGainMeterFragment,
      kFilterResponseFragment,
      kColorFragment,
      kFadeSquareFragment,
      kCircleFragment,
      kRingFragment,
      kDiamondFragment,
      kRoundedCornerFragment,
      kRoundedRectangleFragment,
      kRoundedRectangleBorderFragment,
      kRotarySliderFragment,
      kRotaryModulationFragment,
      kHorizontalSliderFragment,
      kVerticalSliderFragment,
      kLinearModulationFragment,
      kModulationKnobFragment,
      kLineFragment,
      kFillFragment,
      kBarFragment,
      kNumFragmentShaders
    };

    Shaders() = default;

    // the uniforms and maths of a filter-response vertex shader (others have nothing to compute)
    vgl::ShaderProgram* getShaderProgram(VertexShader vertex_shader, FragmentShader fragment_shader = kColorFragment,
                                         const char** varyings = nullptr);

  private:
    std::map<int, std::unique_ptr<vgl::ShaderProgram>> shader_programs_;
};

// What Vital's render(open_gl, animate) calls draw with: the frame's Graphics, in the coordinates of
// the interface (FullInterface). setViewport stands for glViewport + glScissor: from then on drawing is
// in the component's own pixels (origin at its top left) and clipped to what of it is visible.
struct OpenGlWrapper {
  OpenGlWrapper(Graphics& graphics, Shaders* s) : g(graphics), shaders(s), display_scale(1.0f) { }
  ~OpenGlWrapper() { finish(); }

  bool setViewport(Rectangle<int> bounds, Rectangle<int> visible);
  void setScissor(Rectangle<int> visible);
  void finish();

  // GL's normalized device coordinates (-1 .. 1, y up) to the viewport's pixels
  float x(float ndc_x) const { return (ndc_x + 1.0f) * 0.5f * viewport.getWidth(); }
  float y(float ndc_y) const { return (1.0f - ndc_y) * 0.5f * viewport.getHeight(); }
  float width() const { return (float)viewport.getWidth(); }
  float height() const { return (float)viewport.getHeight(); }

  Graphics& g;
  Shaders* shaders;
  float display_scale;
  Rectangle<int> viewport;

  private:
    bool state_saved_ = false;

    JUCE_DECLARE_NON_COPYABLE(OpenGlWrapper)
};
