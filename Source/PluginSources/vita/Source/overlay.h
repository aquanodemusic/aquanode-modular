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
#include "synth_section.h"

#include <set>

class OverlayBackgroundRenderer : public OpenGlComponent {
  public:
    OverlayBackgroundRenderer() {
      additive_blending_ = false;
      color_ = Colours::black;
      setInterceptsMouseClicks(false, false);
    }

    virtual ~OverlayBackgroundRenderer() { }

    virtual void render(OpenGlWrapper& open_gl, bool animate) override {
      drawOverlay(open_gl);
    }

    void paintBackground(Graphics& g) override { }

    void setColor(const Colour& color) { color_ = color; }

    force_inline void setAdditiveBlending(bool additive_blending) { additive_blending_ = additive_blending; }

  protected:
    void drawOverlay(OpenGlWrapper& open_gl) {
      if (!setViewPort(open_gl))
        return;

      open_gl.g.setColour(color_);
      open_gl.g.fillRect(getLocalBounds());
    }

    Colour color_;
    bool additive_blending_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverlayBackgroundRenderer)
};

class Overlay : public SynthSection {
  public:
    class Listener {
      public:
        Listener() { }
        virtual ~Listener() { }

        virtual void overlayShown(Overlay* component) = 0;
        virtual void overlayHidden(Overlay* component) = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Listener)
    };

    Overlay(String name) : SynthSection(name), size_ratio_(1.0f) {
      setSkinOverride(Skin::kOverlay);
      addOpenGlComponent(&background_);
    }
    virtual ~Overlay() { }

    void setVisible(bool should_be_visible) override {
      for (Listener* listener : listeners_) {
        if (should_be_visible)
          listener->overlayShown(this);
        else
          listener->overlayHidden(this);
      }
      Component::setVisible(should_be_visible);
    }

    virtual void resized() override {
      background_.setColor(findColour(Skin::kOverlayScreen, true));
      background_.setBounds(getLocalBounds());
    }

    virtual void paintBackground(Graphics& g) override { paintOpenGlChildrenBackgrounds(g); }

    void addOverlayListener(Listener* listener) { listeners_.insert(listener); }
    void removeOverlayListener(Listener* listener) { listeners_.erase(listener); }
    void setSizeRatio(float ratio) override { size_ratio_ = ratio; }

  protected:
    float size_ratio_;
    std::set<Listener*> listeners_;
    OverlayBackgroundRenderer background_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Overlay)
};

