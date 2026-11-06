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
#include "line_editor.h"
#include "synth_section.h"

class LineGenerator;
class OpenGlShapeButton;
class PlainTextComponent;
class SynthSlider;

// Editor for an oscillator's "custom warp" curve, the shape used by the Spectral Filter, Flanger and
// Phaser spectral morphs (added in 1.5.5), together with the "Spectral Phase" control that scrolls
// the curve. Opened with the pencil button next to the spectral morph knob.
class SpectralWarpEditor : public SynthSection, public LineEditor::Listener {
  public:
    class Listener {
      public:
        virtual ~Listener() = default;
        virtual void spectralWarpEditorClosed(SpectralWarpEditor* editor) = 0;
    };

    SpectralWarpEditor(int index, LineGenerator* curve);
    virtual ~SpectralWarpEditor();

    void paintBackground(Graphics& g) override;
    void resized() override;
    void buttonClicked(Button* clicked_button) override;

    void setPhase(float phase) override { }
    void lineEditorScrolled(const MouseEvent& e, const MouseWheelDetails& wheel) override { }
    void togglePaintMode(bool enabled, bool temporary_switch) override { }
    void fileLoaded() override { }
    void importLfo() override { }
    void exportLfo() override { }

    void addListener(Listener* listener) { listeners_.push_back(listener); }
    int getIndex() const { return index_; }
    Slider* getPhaseSlider();

  private:
    int index_;
    std::unique_ptr<LineEditor> editor_;
    std::unique_ptr<SynthSlider> phase_;
    std::unique_ptr<OpenGlShapeButton> close_button_;
    std::unique_ptr<PlainTextComponent> title_;
    std::vector<Listener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralWarpEditor)
};
