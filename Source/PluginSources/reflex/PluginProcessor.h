#pragma once
#include <cstring>
#include <JuceHeader.h>
#include "Engine.h"
#include "RateAdapter.h"
#include "Randomizer.h"
#include <random>

class ReflexProcessor : public juce::AudioProcessor,
                        public aquanode::MpeReceiver {   // (Aquanode: MPE once a member channel plays)
public:
  void enableMpe(bool on) override { engine.mpe = on; }
  ReflexProcessor();
  ~ReflexProcessor() override = default;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "Reflex"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 4.0; }

  int getNumPrograms() override { return (int)programs.size(); }
  int getCurrentProgram() override { return currentProgram; }
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String& newName) override;

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // ---- preset I/O ----
  struct Program { juce::String name; std::array<float, rfx::NumParams> values; };
  bool loadFxbOrFxp(const juce::File& f, juce::String& error);
  bool loadXmlPreset(const juce::File& f, juce::String& error);
  bool saveXmlPreset(const juce::File& f);
  bool saveXmlBank(const juce::File& f);
  bool saveFxp(const juce::File& f);   // original-compatible single preset
  bool saveFxb(const juce::File& f);   // original-compatible bank
  bool loadOriginalChunk(const uint8_t* d, size_t sz, bool singleProgram, juce::String& error);
  void storeCurrentIntoProgram();
  void applyValues(const std::array<float, rfx::NumParams>& v);
  std::array<float, rfx::NumParams> currentValues() const;
  juce::String currentPresetName;

  juce::AudioProcessorValueTreeState apvts;
  std::vector<Program> programs;
  int currentProgram = 0;
  std::function<void()> onPresetListChanged;   // set by editor

  static juce::String paramID(int i) { return "p" + juce::String(i); }

  // ---- randomizer / mutator ----
  void randomize(int menuIndex, float variation);   // message thread
  int randomMenuIndex = 0;          // remembered with the plugin state
  float randomVariation = 0.35f;

  // ---- editor zoom (fraction of the full-size layout), remembered with the plugin state ----
  float uiScale = 0.8f;
  // Aquanode patch version: which part of the engine runs (see rfx::Engine::split) and the
  // patch's tempo (the host's play head is not there)
  int split = 0;
  double tempoOverride = 0.0;
  void allNotesOffNow() { engine.allNotesOff(); }   // (the engine would take an all-notes-off sharing a frame with a note after it)
  int uiTheme = 0;   // 0 = glossy aero, 1 = metallic green

private:
  rfx::Engine engine;
  RateAdapter adapter;
  bool useAdapter = false;
  double lastBpm = 120.0;
  std::mt19937 rng { std::random_device{}() };
  std::array<std::atomic<float>*, rfx::NumParams> raw {};
  std::vector<rfx::MidiEv> events;
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
  std::unique_ptr<juce::XmlElement> valuesToXml(const juce::String& name, const std::array<float, rfx::NumParams>& v) const;
  static bool xmlToValues(const juce::XmlElement& e, std::array<float, rfx::NumParams>& v, juce::String& name);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReflexProcessor)
};
