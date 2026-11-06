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

#include "open_gl_component.h"

#include "open_gl_multi_quad.h"
#include "full_interface.h"
#include "skin.h"

namespace {
  Rectangle<int> getGlobalBounds(Component* component, Rectangle<int> bounds) {
    Component* parent = component->getParentComponent();
    while (parent && dynamic_cast<FullInterface*>(component) == nullptr) {
      bounds = bounds + component->getPosition();
      component = parent;
      parent = component->getParentComponent();
    }

    return bounds;
  }

  Rectangle<int> getGlobalVisibleBounds(Component* component, Rectangle<int> visible_bounds) {
    Component* parent = component->getParentComponent();
    while (parent && dynamic_cast<FullInterface*>(parent) == nullptr) {
      visible_bounds = visible_bounds + component->getPosition();
      parent->getLocalBounds().intersectRectangle(visible_bounds);
      component = parent;
      parent = component->getParentComponent();
    }

    return visible_bounds + component->getPosition();
  }
}

OpenGlComponent::OpenGlComponent(String name) : Component(name), only_bottom_corners_(false),
                                                parent_(nullptr), skin_override_(Skin::kNone),
                                                num_voices_readout_(nullptr) {
  background_color_ = Colours::transparentBlack;
}

OpenGlComponent::~OpenGlComponent() { }

bool OpenGlComponent::setViewPort(Component* component, Rectangle<int> bounds, OpenGlWrapper& open_gl) {
  Rectangle<int> global_bounds = getGlobalBounds(component, bounds);
  Rectangle<int> visible_bounds = getGlobalVisibleBounds(component, bounds);
  return open_gl.setViewport(global_bounds, visible_bounds);
}

bool OpenGlComponent::setViewPort(Component* component, OpenGlWrapper& open_gl) {
  return setViewPort(component, component->getLocalBounds(), open_gl);
}

bool OpenGlComponent::setViewPort(OpenGlWrapper& open_gl) {
  return setViewPort(this, open_gl);
}

void OpenGlComponent::setScissor(Component* component, OpenGlWrapper& open_gl) {
  setScissorBounds(component, component->getLocalBounds(), open_gl);
}

void OpenGlComponent::setScissorBounds(Component* component, Rectangle<int> bounds, OpenGlWrapper& open_gl) {
  if (component == nullptr)
    return;

  Rectangle<int> visible_bounds = getGlobalVisibleBounds(component, bounds);
  if (visible_bounds.getHeight() > 0 && visible_bounds.getWidth() > 0)
    open_gl.setScissor(visible_bounds);
}

void OpenGlComponent::paintBackground(Graphics& g) {
  if (!isVisible())
    return;

  g.fillAll(findColour(Skin::kWidgetBackground, true));
}

void OpenGlComponent::repaintBackground() {
  if (!isShowing())
    return;

  FullInterface* parent = findParentComponentOfClass<FullInterface>();
  if (parent)
    parent->repaintOpenGlBackground(this);
}

void OpenGlComponent::resized() {
  if (corners_)
    corners_->setBounds(getLocalBounds());

  body_color_ = findColour(Skin::kBody, true);
}

void OpenGlComponent::parentHierarchyChanged() {
  if (num_voices_readout_ == nullptr) {
    SynthGuiInterface* parent = findParentComponentOfClass<SynthGuiInterface>();
    if (parent)
      num_voices_readout_ = parent->getSynth()->getStatusOutput("num_voices");
  }

  Component::parentHierarchyChanged();
}

void OpenGlComponent::addRoundedCorners() {
  corners_ = std::make_unique<OpenGlCorners>();
  addAndMakeVisible(corners_.get());
}

void OpenGlComponent::addBottomRoundedCorners() {
  only_bottom_corners_ = true;
  addRoundedCorners();
}

void OpenGlComponent::init(OpenGlWrapper& open_gl) {
  if (corners_)
    corners_->init(open_gl);
}

void OpenGlComponent::renderCorners(OpenGlWrapper& open_gl, bool animate, Colour color, float rounding) {
  if (corners_) {
    if (only_bottom_corners_)
      corners_->setBottomCorners(getLocalBounds(), rounding);
    else
      corners_->setCorners(getLocalBounds(), rounding);
    corners_->setColor(color);
    corners_->render(open_gl, animate);
  }
}

void OpenGlComponent::renderCorners(OpenGlWrapper& open_gl, bool animate) {
  renderCorners(open_gl, animate, body_color_, findValue(Skin::kWidgetRoundedCorner));
}

void OpenGlComponent::destroy(OpenGlWrapper& open_gl) {
  if (corners_)
    corners_->destroy(open_gl);
}

float OpenGlComponent::findValue(Skin::ValueId value_id) {
  if (parent_)
    return parent_->findValue(value_id);

  VITAL_ASSERT(false);
  return 0.0f;
}
