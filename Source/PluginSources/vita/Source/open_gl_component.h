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

#include "common.h"
#include "shaders.h"
#include "skin.h"
#include "synth_module.h"

class SynthSection;
class OpenGlCorners;

class OpenGlComponent : public Component {
  public:
    static bool setViewPort(Component* component, Rectangle<int> bounds, OpenGlWrapper& open_gl);
    static bool setViewPort(Component* component, OpenGlWrapper& open_gl);
    static void setScissor(Component* component, OpenGlWrapper& open_gl);
    static void setScissorBounds(Component* component, Rectangle<int> bounds, OpenGlWrapper& open_gl);

    static std::unique_ptr<vgl::ShaderProgram::Uniform> getUniform(const OpenGlWrapper& open_gl,
                                                                    const vgl::ShaderProgram& program,
                                                                    const char* name) {
      if (program.hasUniform(name))
        return std::make_unique<vgl::ShaderProgram::Uniform>(program, name);
      return nullptr;
    }

    static std::unique_ptr<vgl::ShaderProgram::Attribute> getAttribute(const OpenGlWrapper& open_gl,
                                                                        const vgl::ShaderProgram& program,
                                                                        const char* name) {
      return std::make_unique<vgl::ShaderProgram::Attribute>(program, name);
    }

    OpenGlComponent(String name = "");
    virtual ~OpenGlComponent();

    virtual void resized() override;
    virtual void parentHierarchyChanged() override;

    void addRoundedCorners();
    void addBottomRoundedCorners();
    virtual void init(OpenGlWrapper& open_gl);
    virtual void render(OpenGlWrapper& open_gl, bool animate) = 0;
    void renderCorners(OpenGlWrapper& open_gl, bool animate, Colour color, float rounding);
    void renderCorners(OpenGlWrapper& open_gl, bool animate);
    virtual void destroy(OpenGlWrapper& open_gl);
    virtual void paintBackground(Graphics& g);
    void repaintBackground();

    Colour getBodyColor() const { return body_color_; }

    void setParent(const SynthSection* parent) { parent_ = parent; }
    float findValue(Skin::ValueId value_id);
    void setSkinValues(const Skin& skin) {
      skin.setComponentColors(this, skin_override_, false);
    }
    void setSkinOverride(Skin::SectionOverride skin_override) { skin_override_ = skin_override; }

    force_inline void checkGlError() { }

    void setBackgroundColor(const Colour& color) { background_color_ = color; }

  protected:
    bool setViewPort(OpenGlWrapper& open_gl);
    
    std::unique_ptr<OpenGlCorners> corners_;
    bool only_bottom_corners_;
    Colour background_color_;
    Colour body_color_;
    const SynthSection* parent_;
    Skin::SectionOverride skin_override_;
    const vital::StatusOutput* num_voices_readout_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGlComponent)
};

