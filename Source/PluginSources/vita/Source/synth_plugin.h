/* Copyright 2013-2019 Matt Tytel
 *
 * pylon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * pylon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pylon.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "JuceHeader.h"

#include "synth_base.h"
#include "value_bridge.h"

class ValueBridge;

class SynthPlugin : public SynthBase, public AudioProcessor, public ValueBridge::Listener,
                    public aquanode::MpeReceiver {   // (Aquanode: MPE once a member channel plays)
  public:
    static constexpr int kSetProgramWaitMilliseconds = 500;

    // effectOnly: Vita FX - an audio input into the effects chain (the voices still play from MIDI)
    explicit SynthPlugin(bool effectOnly = false);
    bool isEffectOnly() const { return effect_only_; }
    // (Aquanode, 6.9.10) MPE: Follow Aquanode (on once a member channel plays -
    // the default) or the preset's own MPE Enabled; the per-finger bend range
    // 48 (MPE) or the preset's Bend range - the card's two choices
    void enableMpe(bool on) override { aquanode_mpe_ = on; applyMpe(); }
    void setMpePolicy(bool fromPreset, bool bendFromPreset) override {
      mpe_from_preset_ = fromPreset;
      bend_from_preset_ = bendFromPreset;
      applyMpe();
    }
    void presetMpeLoaded() override { applyMpe(); }
    // (Aquanode, 6.9.13) how far a full Slide moves the frame each way (0..1 of the table)
    void setSlideWavetableDepth(float each_way) override { aq_slide_each_way_.store(each_way, std::memory_order_relaxed); }
    std::atomic<float> aq_slide_each_way_ { 0.0f };

    void applyMpe() {
      auto& controls = getControls();
      auto it = controls.find("mpe_enabled");
      const bool preset = it != controls.end() && it->second->value() >= 0.5f;
      setMpeEnabled(mpe_from_preset_ ? preset : aquanode_mpe_);
    }
    bool aquanode_mpe_ = false, mpe_from_preset_ = false, bend_from_preset_ = false;
    virtual ~SynthPlugin();

    SynthGuiInterface* getGuiInterface() override;
    void beginChangeGesture(const std::string& name) override;
    void endChangeGesture(const std::string& name) override;
    void setValueNotifyHost(const std::string& name, vital::mono_float value) override;
    const CriticalSection& getCriticalSection() override;
    void pauseProcessing(bool pause) override;

    void prepareToPlay(double sample_rate, int buffer_size) override;
    void releaseResources() override;
    void processBlock(AudioSampleBuffer&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const String getName() const override;
    bool supportsMPE() const override { return true; }

    const String getInputChannelName(int channel_index) const override;
    const String getOutputChannelName(int channel_index) const override;
    bool isInputChannelStereoPair(int index) const override;
    bool isOutputChannelStereoPair(int index) const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool silenceInProducesSilenceOut() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { }
    const String getProgramName(int index) override;
    void changeProgramName(int index, const String& new_name) override { }

    void getStateInformation(MemoryBlock& destData) override;
    void setStateInformation(const void* data, int size_in_bytes) override;
    AudioProcessorParameter* getBypassParameter() const override { return bypass_parameter_; }

    void parameterChanged(std::string name, vital::mono_float value) override;

  private:
    bool effect_only_ = false;
    ValueBridge* bypass_parameter_;
    double last_seconds_time_;

    AudioPlayHead::CurrentPositionInfo position_info_;

    std::map<std::string, ValueBridge*> bridge_lookup_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPlugin)
};

