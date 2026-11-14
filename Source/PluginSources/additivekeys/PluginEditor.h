#pragma once
#include "PluginProcessor.h"

//==============================================================================
// Additive Keys' panel. One source at a time (the Source 1 / Source 2 tabs):
// its 63 harmonics as bars drawn with a finger, coloured by the envelope bus
// each one follows; its envelopes as curves with handles to drag; the formant
// shape (shared by both sources) as bands to drag; and the LFO.
//==============================================================================
namespace akui
{
    // the four envelope buses' colours, used by the bars and the curves alike
    inline juce::Colour busColour (int g)
    {
        static const juce::uint32 c[4] = { 0xffe0a95a, 0xff4fc3c9, 0xffe06a9a, 0xff9a86e8 };
        return juce::Colour (c[juce::jlimit (0, 3, g)]);
    }
    const juce::Colour bg { 0xff17191d }, panel { 0xff20232a }, panelEdge { 0xff2e323b },
                       text { 0xffd8dce3 }, dim { 0xff8a93a3 }, accent { 0xffe0a95a };

    struct LookAndFeel : public juce::LookAndFeel_V4
    {
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float start, float end, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f)); }
    };

    // a knob with its name above and value below, attached to one parameter
    struct Knob : public juce::Component
    {
        Knob (const juce::String& name);
        void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id);
        void resized() override;
        juce::Slider slider;
        juce::Label caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    //==========================================================================
    struct HarmonicBars : public juce::Component
    {
        HarmonicBars (K5AudioProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { lastBar = -1; }

        K5AudioProcessor& proc;
        int source = 0;
        int paintBus = -1;      // -1: draw levels; 0..3: paint the bars onto that bus
    private:
        int barAt (float x) const;
        float levelAt (float y) const;
        void apply (int bar, float y);
        int lastBar = -1;
        float lastY = 0;
    };

    //==========================================================================
    // an ADSR as a curve: drag the peak (attack), the knee (decay, sustain)
    // and the end (release)
    struct EnvelopeCurve : public juce::Component, private juce::Timer
    {
        EnvelopeCurve (K5AudioProcessor& p) : proc (p) { startTimerHz (20); }
        void setIds (const juce::String& a, const juce::String& d, const juce::String& s, const juce::String& r,
                     juce::Colour c, const juce::String& title);
        void setGhosts (std::vector<std::array<juce::String, 4>> ids) { ghosts = std::move (ids); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        K5AudioProcessor& proc;
    private:
        void timerCallback() override { repaint(); }
        juce::Path curveFor (const std::array<juce::String, 4>& ids, juce::Rectangle<float> area) const;
        float value (const juce::String& id) const;
        void setValue (const juce::String& id, float v);
        juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (10, 22).withTrimmedBottom (-12); }
        // time <-> width of a segment (square-root scale, so short times get room)
        float segWidth (const juce::String& id, float t, float maxW) const;
        float segTime (const juce::String& id, float w, float maxW) const;
        std::array<juce::String, 4> ids;
        std::vector<std::array<juce::String, 4>> ghosts;
        juce::Colour colour { accent };
        juce::String title;
        int dragging = -1;      // 0 attack, 1 decay/sustain, 2 release
        std::array<juce::Point<float>, 3> handles;
    };

    //==========================================================================
    // the formant (DFT) shape: eleven bands to drag across frequency and gain
    struct FormantView : public juce::Component, private juce::Timer
    {
        FormantView (K5AudioProcessor& p) : proc (p) { startTimerHz (20); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = -1; }
        std::function<void (int)> onSelect;
        int selected = 2;

        K5AudioProcessor& proc;
    private:
        void timerCallback() override { repaint(); }
        juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (10, 10).withTrimmedTop (14); }
        float xOf (float f) const;
        float fOf (float x) const;
        float get (int band, const char* what) const;
        void set (int band, const char* what, float v);
        int dragging = -1;
    };
}

//==============================================================================
class K5AudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit K5AudioProcessorEditor (K5AudioProcessor&);
    ~K5AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectSource (int s);
    void selectEnvelope (int e);     // 0..3 harmonic buses, 4 amp, 5 filter, 6 pitch
    void showPresetMenu();
    void setVowel (int v);

    K5AudioProcessor& processor;
    akui::LookAndFeel lnf;

    juce::TextButton presetButton { "Presets" }, prevButton { "<" }, nextButton { ">" };
    juce::TextButton sourceTabs[2];
    akui::Knob master { "Master" };

    // harmonics
    akui::HarmonicBars bars { processor };
    juce::TextButton paintLevel { "Draw Levels" };
    juce::TextButton paintBus[4];
    juce::TextButton resetHarm { "Reset" };
    juce::ComboBox harmMode;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> harmModeAtt;
    akui::Knob tilt { "Tilt" }, level { "Level" }, detune { "Detune" };

    // envelopes
    akui::EnvelopeCurve envelope { processor };
    juce::TextButton envTabs[7];
    akui::Knob cutoff { "Cutoff" }, resonance { "Resonance" }, envAmount { "Env Amt" }, pitchDepth { "Pitch Depth" };
    juce::ComboBox slope;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slopeAtt;

    // formants
    akui::FormantView formants { processor };
    juce::TextButton vowels[6];
    akui::Knob bandQ { "Band Width" };

    // LFO
    juce::ComboBox lfoShape;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoShapeAtt;
    akui::Knob lfoRate { "Rate" }, lfoDelay { "Delay" }, lfoVib { "Vibrato" }, lfoTrem { "Tremolo" }, lfoFilt { "Filter" };

    int source = 0, envSel = 4, factoryIndex = -1, shownNameVersion = -1;
    juce::Random rng;
    std::shared_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> harmArea, envArea, formantArea, lfoArea, topBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (K5AudioProcessorEditor)
};
