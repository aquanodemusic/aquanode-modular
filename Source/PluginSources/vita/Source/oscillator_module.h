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

#include <atomic>

#include "synth_module.h"
#include "synth_oscillator.h"

namespace vital {
  class Wavetable;

  class OscillatorModule : public SynthModule {
    public:
      enum {
        kReset,
        kRetrigger,
        kMidi,
        kActiveVoices,
        kAqSlide,   // (Aquanode, 6.9.12) the voices' Slide for the default MPE map
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      OscillatorModule(std::string prefix = "");
      virtual ~OscillatorModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new OscillatorModule(*this); }

      Wavetable* getWavetable() { return wavetable_.get(); }
      force_inline SynthOscillator* oscillator() { return oscillator_; }
      SynthOscillator::DistortionType getDistortionType() {
        int val = distortion_type_->value();
        return static_cast<SynthOscillator::DistortionType>(val);
      }

    protected:
      std::string prefix_;
      std::shared_ptr<Wavetable> wavetable_;
      std::shared_ptr<bool> was_on_;
      std::shared_ptr<std::atomic<float>> aq_slide_depth_;   // (Aquanode, 6.9.12)
    public:
      void setAqSlideDepth(float frames) { aq_slide_depth_->store(frames, std::memory_order_relaxed); }
      // (Aquanode, 6.9.15) how far the latest note's Slide moves the frame now - for the display
      float aqSlideShown() const { return aq_slide_shown_->load(std::memory_order_relaxed); }
      std::shared_ptr<std::atomic<float>> aq_slide_shown_;
    protected:

      Value* on_;
      SynthOscillator* oscillator_;
      Value* distortion_type_;

      JUCE_LEAK_DETECTOR(OscillatorModule)
  };
} // namespace vital

