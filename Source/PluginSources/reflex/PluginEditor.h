#pragma once
#include <cstring>
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Font helper that builds on JUCE 7 and JUCE 8
juce::Font uiFont(float height, bool bold = false);

// Two looks, switched by clicking the logo:
//   Aero  - sky/meadow backdrop with bubbles, frosted glass panels, gel orb knobs, pill controls
//   Metal - brushed green metal plates, chrome knobs with LED rings, LCD read-outs
class ReflexLook : public juce::LookAndFeel_V4 {
public:
  enum Theme { Aero = 0, Metal = 1 };
  ReflexLook();
  void setTheme(int t);
  int theme = Aero;
  bool metal() const { return theme == Metal; }

  void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
  void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                        juce::Slider::SliderStyle, juce::Slider&) override;
  void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
  void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
  void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
  void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
  void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
  void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                         bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                         const juce::Drawable* icon, const juce::Colour* textColour) override;
  juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(13.5f); }
  juce::Font getPopupMenuFont() override { return uiFont(14.0f); }
  juce::Font getTextButtonFont(juce::TextButton&, int) override { return uiFont(14.0f, true); }

  // current palette (depends on the theme)
  juce::Colour bg, plateTop, plateBottom, text, caption, accent, accentDim, lcd, lcdText, gelTop, gelBottom, edge;

  // caption text in the style of the theme (engraved on metal, embossed on glass)
  void drawCaption(juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification) const;
  juce::Image makePanel(int w, int h, uint32_t seed) const;
  static ReflexLook& of(juce::Component&);
};

// Knob with caption and live value read-out
class Knob : public juce::Component {
public:
  Knob(ReflexProcessor& p, int index, const juce::String& caption);
  void resized() override;
  void paint(juce::Graphics& g) override;
  void lookAndFeelChanged() override;
  juce::Slider slider;
private:
  juce::String caption;
  juce::Label value;
  std::unique_ptr<juce::SliderParameterAttachment> att;
};

// ComboBox bound to a stepped float parameter
class Choice : public juce::Component {
public:
  Choice(ReflexProcessor& p, int index, const juce::String& caption, const juce::StringArray& items, int nSteps = -1);
  void resized() override;
  void paint(juce::Graphics& g) override;
  juce::ComboBox box;
private:
  juce::String caption;
  int n; bool rounded = false;
  std::unique_ptr<juce::ParameterAttachment> att;
};

// On/off switch bound to a float parameter (>= 0.5 = on)
class Switch : public juce::Component {
public:
  Switch(ReflexProcessor& p, int index, const juce::String& caption);
  void resized() override;
  juce::ToggleButton button;
private:
  std::unique_ptr<juce::ParameterAttachment> att;
};

// 16-step trance gate pattern editor
class GateSteps : public juce::Component {
public:
  GateSteps(ReflexProcessor& p, int index);
  void paint(juce::Graphics& g) override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
private:
  unsigned mask = 0; int dragValue = -1;
  int stepAt(juce::Point<float> p) const;
  void setStep(int s, bool on);
  std::unique_ptr<juce::ParameterAttachment> att;
};

class Section : public juce::Component {
public:
  explicit Section(const juce::String& t) : title(t) { setInterceptsMouseClicks(false, true); }
  void paint(juce::Graphics& g) override;
  juce::String title;
private:
  juce::Image plate; int plateTheme = -1;
};

// The full-size panel; the editor scales it to the window size
class Canvas : public juce::Component {
public:
  void paint(juce::Graphics& g) override;
  void mouseDown(const juce::MouseEvent&) override;
  void mouseMove(const juce::MouseEvent&) override;
  std::function<void()> onLogoClick;
  static juce::Rectangle<int> logoArea() { return { 14, 8, 270, 96 }; }
private:
  juce::Image headerPlate, background; int cacheTheme = -1;
};

class ReflexEditor : public juce::AudioProcessorEditor {
public:
  explicit ReflexEditor(ReflexProcessor&);
  ~ReflexEditor() override;
  void paint(juce::Graphics&) override;
  void resized() override;
  static constexpr int BaseW = 1390, BaseH = 1062;
private:
  ReflexProcessor& proc;
  ReflexLook look;
  Canvas canvas;
  juce::OwnedArray<Section> sections;
  juce::OwnedArray<juce::Component> controls;
  std::map<juce::String, std::vector<juce::Component*>> sectionControls;

  juce::ComboBox presetBox;
  juce::TextButton prevBtn { "<" }, nextBtn { ">" }, loadBtn { "Load..." }, saveBtn { "Save Preset" }, saveBankBtn { "Save Bank" };
  juce::ComboBox randomType, zoomBox;
  juce::TextButton randomBtn { "RANDOM" }, mutateBtn { "MUTATE" };
  int mutateIndex = -1;
  juce::Slider variation;
  juce::Label variationLabel, randomLabel;
  std::unique_ptr<juce::FileChooser> chooser;
  GateSteps* gate = nullptr;
  juce::TooltipWindow tooltips { this, 600 };

  Knob* knob(const juce::String& sec, int idx, const juce::String& cap);
  Choice* choice(const juce::String& sec, int idx, const juce::String& cap, const juce::StringArray& items, int n = -1);
  Switch* sw(const juce::String& sec, int idx, const juce::String& cap);
  void refreshPresetList();
  void layoutCanvas();
  void layoutSection(const juce::String& name, juce::Rectangle<int> r, int cols);
  void setZoom(float z);
  void applyTheme(int t);
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReflexEditor)
};
