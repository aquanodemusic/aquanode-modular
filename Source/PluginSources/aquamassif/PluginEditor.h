#pragma once
// Massif - the panel.
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace amui
{
    // The look: brushed-silver plates on a dark steel frame, knurled knobs
    // with a glowing LED arc, and bright cyan CRT glass for every display.
    //
    // Two colours are yours to choose (tap the MASSIF name): the metal
    // and the glow. Every shade keeps its relation to its family's reference
    // shade (hue offset, saturation, brightness ratio), so the picked
    // colour becomes the plates' silver / the glow and all the other levels
    // follow; the lettering turns light when the metal gets dark. The palette
    // is process-global: two Massif windows share the theme of the one
    // opened (or changed) last.
    //
    // The colourway you get without choosing: AlphaBeta's light grey
    // (0xFFB8BDC8, the darker of its two panel greys, which lands the plates
    // on its main grey) and the bright Aquanode cyan of Tidebreeze's glass.
    inline const juce::Colour metalRef { 0xffa4b5be }, glowRef { 0xff3fe0f5 };         // what the shades below are written in
    inline const juce::Colour metalDefault { 0xffb8bdc8 }, glowDefault { 0xff3fe0f5 }; // AlphaBeta's grey, the Aquanode cyan
    inline juce::Colour metalTint = metalDefault, glowTint = glowDefault;

    inline float relMap (float c, float r, float p)      // maps c so that r -> p, keeping 0 and 1 fixed
    {
        if (p <= r) return r > 1.0e-4f ? c * p / r : c;
        return r < 0.9999f ? c + (1.0f - c) * (p - r) / (1.0f - r) : c;
    }
    inline juce::Colour mapShade (juce::Colour c, juce::Colour ref, juce::Colour tint, bool keepBrightness)
    {
        if (tint == ref) return c;
        float h, s, v, rh, rs, rv, th, ts, tv;
        c.getHSB (h, s, v); ref.getHSB (rh, rs, rv); tint.getHSB (th, ts, tv);
        h += th - rh; h -= std::floor (h);
        s = juce::jlimit (0.0f, 1.0f, relMap (s, rs, ts));
        if (! keepBrightness) v = juce::jlimit (0.0f, 1.0f, rv > 1.0e-4f ? v * tv / rv : v);   // as a ratio: dark shades stay dark
        return juce::Colour::fromHSV (h, s, v, c.getFloatAlpha());
    }
    inline juce::Colour mt (juce::uint32 argb) { return mapShade (juce::Colour (argb), metalRef, metalTint, false); }  // a metal shade
    inline juce::Colour mk (juce::uint32 argb) { return mapShade (juce::Colour (argb), metalRef, metalTint, true); }   // rubber, recesses: the tint only
    inline juce::Colour gl (juce::uint32 argb) { return mapShade (juce::Colour (argb), glowRef, glowTint, false); }    // a shade of the glow

    inline juce::Colour frameHi, frameLo, panelHi, panel, navy, text, textDim, keyText, bg, line,
                        accent, accentDeep, crtCentre, crtEdge, phosphor, crtInk;
    inline bool darkPlates = false;                   // the metal is dark: light lettering, sources as lights
    inline int themeVersion = 0;

    inline void applyPalette()
    {
        frameHi = mt (0xff333f47); frameLo = mt (0xff1a2227);     // the dark steel frame
        panelHi = mt (0xffdde5e9); panel = mt (0xffa4b5be);       // the brushed plates
        bg = mk (0xff161d22); line = mk (0xff10171b);             // recesses
        keyText = juce::Colour (0xffe2ecef);                      // lettering on the rubber keys
        darkPlates = panel.getPerceivedBrightness() < 0.52f;
        navy = mk (0xff13304a);
        text = darkPlates ? juce::Colour (0xffe8f0f3) : navy;     // lettering on the plates
        textDim = darkPlates ? juce::Colour (0xffb4c4cb) : mk (0xff3d5668);
        accent = gl (0xff3fe0f5); accentDeep = gl (0xff1593b0);   // the glow
        crtCentre = gl (0xff23b4d4); crtEdge = gl (0xff0a5f78); phosphor = gl (0xfff2feff);   // bright cyan glass, near-white trace
        crtInk = gl (0xff04313f);                                 // dark lettering where the glass is brightest
        ++themeVersion;
    }
    inline void setTheme (juce::Colour metal, juce::Colour glow) { metalTint = metal.withAlpha (1.0f); glowTint = glow.withAlpha (1.0f); applyPalette(); }
    inline const bool paletteReady = (applyPalette(), true);

    // the modulation sources, as lights (on screens and in the slot boxes)
    const juce::Colour envCol    { 0xff62a8ff };
    const juce::Colour modCol    { 0xff52e2a2 };
    const juce::Colour macroCol  { 0xffffab4d };
    const juce::Colour midiCol   { 0xfff0dc5a };

    juce::Colour sourceInk (int src);                 // the same, as ink on the plates
    juce::Colour sourceColour (int src);
    juce::String sourceShort (int src);
    juce::String sourceName (int src);

    void crtGlass (juce::Graphics&, juce::Rectangle<float>, float corner = 3.0f);          // glass, vignette, scanlines, bezel
    void glowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, juce::Colour, float size, bool bold = false);

    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();
        void refresh();                               // the theme changed
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minP, float maxP, juce::Slider::SliderStyle, juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.0f)); }
        juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (11.0f)); }
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (11.5f, juce::Font::bold)); }
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    };
}

class AquaMassifEditor;

//==============================================================================
// a knob with its modulation: rings for the slots, three slot boxes below
class MKnob : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    MKnob (AquaMassifEditor& e, int knobIndex, const juce::String& label, int numSlots = 2, bool sideChainSlot = true);
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setLabel (const juce::String& s) { label = s; repaint(); }
    void setHidden (bool h) { hiddenKnob = h; slider.setVisible (! h); repaint(); }
    juce::Slider slider;
    const int knob;
private:
    juce::Rectangle<int> slotRect (int i) const;
    int slotAt (juce::Point<int> p) const;
    void slotMenu (int s);
    AquaMassifEditor& ed;
    juce::String label;
    int slots; bool sc;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    int draggingSlot { -1 }; float dragStartAmount { 0 };
    bool dropHover { false }, hiddenKnob { false };
};

// a source handle: drag it onto a knob
class SourceHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    SourceHandle (int src) : source (src) { setTooltip ("Drag onto a knob to modulate it with " + amui::sourceName (src)); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    const int source;
};

//==============================================================================
// stepper / performer step editor
class StepEditor : public juce::Component
{
public:
    StepEditor (AquaMassifEditor& e) : ed (e) {}
    void setPage (int p) { page = p; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    void edit (const juce::MouseEvent&, bool start);
    AquaMassifEditor& ed; int page { 3 };
    int dragRow { -1 }; bool dragValue { false };
};

// envelope / LFO curve display
class CurveView : public juce::Component, private juce::Timer
{
public:
    CurveView (AquaMassifEditor& e) : ed (e) { startTimerHz (20); }
    void setSource (int s) { source = s; repaint(); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    AquaMassifEditor& ed; int source { 0 };
};

// an oscillator's table on a little screen: the frame at the playing position
// (or, as the generator's preview, the whole table as a stack of frames)
class WaveView : public juce::Component, private juce::Timer
{
public:
    WaveView (AquaMassifEditor& e, int oscIndex, bool stackView) : ed (e), osc (oscIndex), stack (stackView) { startTimerHz (12); }
    void setOsc (int o) { osc = o; repaint(); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    AquaMassifEditor& ed; int osc; bool stack;
    float lastPos { -1 }; int lastSlot { -1 }; const void* lastTable { nullptr };
};

//==============================================================================
class AquaMassifEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit AquaMassifEditor (AquaMassifProcessor&);
    ~AquaMassifEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    AquaMassifProcessor& proc;
    void modsChanged() { proc.extraChanged(); for (auto* k : knobs) k->repaint(); }
    void showPage (int page);   // 0..7 env/mod sources, 8 osc, 9 voicing, 10 wavetable generator
    int uiTick { 0 }, lastModType { -1 };
    int currentPage { 0 };

private:
    void timerCallback() override;
    MKnob* addKnob (int knob, const juce::String& label, int slots = 2, bool sc = true);
    juce::ComboBox* addCombo (const juce::String& paramId, juce::Component* parent = nullptr);
    juce::ToggleButton* addToggle (const juce::String& paramId, const juce::String& text);
    juce::Slider* addFader (int knob, bool vertical);
    void layoutPage();
    void refreshAll();
    void loadPreset();                                 // Massif's own presets (.massif)
    void savePreset();
    void importSound();                                // the original's sound files (.nmsv)
    void exportSound();
    void chooseExportTemplate (std::function<void()> then);
    void stepPreset (int dir);
    void importBundle (const juce::MemoryBlock& zip, const juce::String& fileName);
    void chooseFolder();
    void tablesMenu();
    void loadTableZip();
    void loadOscTable (int osc);
    void updateLabels();
    void presetMenu();
    void rebuildChassis();
    void applyTheme();                                 // the palette from proc.themeMetal / themeGlow, everything repainted
    void showThemePicker();
    juce::Rectangle<int> logoArea() const;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void generate();
    void exportTable();
    void recipeChanged();

    amui::Look look;
    juce::TooltipWindow tooltips { this, 600 };
    juce::OwnedArray<MKnob> knobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::Slider> faders;
    juce::OwnedArray<SourceHandle> handles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> faderAtt;
    std::map<juce::String, juce::Component*> byId;
    std::map<int, MKnob*> knobById;
    std::map<int, juce::Slider*> faderById;

    // header
    juce::TextButton loadB { "Load" }, saveB { "Save" }, importB { "Import" }, exportB { "Export" }, prevB { "<" }, nextB { ">" }, initB { "Init" }, folderB { "Wavetables" };
    juce::TextButton presetName;                      // the preset display: tap for the built-in sounds
    juce::ComboBox genreBox, typeBox;                 // the patch generator: what to aim for
    juce::TextButton genB { "Generate" };
    void generatePatch();
    std::array<juce::TextButton, 6> wtStep;          // osc prev / next
    std::array<juce::TextButton, 3> wtLoad;          // osc: a table from a file
    // center window
    juce::OwnedArray<juce::TextButton> tabs;
    std::unique_ptr<StepEditor> steps;
    std::unique_ptr<CurveView> curve;
    juce::ComboBox envPlay, envLoops, envShapeA, envShapeB, lfoShapeA, lfoShapeB, syncNum, syncDen;
    juce::ToggleButton envLinear { "Linear decay" }, envRetrig { "Retrigger" }, lfoMono { "Mono" };
    juce::Slider bendUp, bendDown, uniRange, tune, bpm;
    juce::ToggleButton glideRate { "Glide by interval" }, chordMode { "Chord spread" }, restartPhase { "Restart phase" };
    juce::Label pageInfo;
    std::array<juce::Label, 8> macroNames;
    juce::Label voicesInfo;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ScopedMessageBox messageBox;               // closes with the editor
    juce::Rectangle<int> oscArea[3], modOscArea, noiseArea, fbArea, fltArea[2], routeArea, insArea[2], fxArea[2], eqArea, ampArea, masterArea, centerArea, macroArea;
    std::array<juce::Label, 3> wtInfo;
    float meter { 0 };
    juce::Image chassis;
    juce::OwnedArray<WaveView> waves;
    // the wavetable generator page
    juce::ComboBox genOsc, genRecipe, genFrames;
    std::array<juce::Slider, 3> genAmt;
    std::array<juce::Label, 3> genAmtLabel;
    juce::Slider genSeed;
    juce::TextButton genDice { "Dice" }, genMake { "Generate" }, genRestore { "Restore" }, genExport { "Export .wav" };
    std::unique_ptr<WaveView> genView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaMassifEditor)
};
