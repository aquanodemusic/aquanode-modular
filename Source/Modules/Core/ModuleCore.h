#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <cmath>
#include <functional>
#include <memory>
#include <map>
#include <unordered_map>
#include <vector>

#include "../../Engine/Tuning.h"

namespace aquanode
{

//==============================================================================
// Enums / small types
//==============================================================================
enum class ParamType { Rotary, RotarySteppedList, Combo, Button, HBar };
enum class SocketKind { Audio, Modulation, Midi };
enum class SocketDirection { Input, Output };
enum class ModuleSection { InputOutput = 0, Oscillator, Filter, Effect, Utility };

// A module family: the sets of modules that came out of one source. Only the DX
// modules carry a colour of their own - they are VirtualDX7's native engine, so
// their cards wear VirtualDX7's colour; the Colorbass and Vintage families keep
// their section's colour (they belong to no plugin).
enum class ModuleFamily { Standard = 0, Colorbass, DX, Vintage };

// VirtualDX7's card colour (the catalog's), which its DX modules share
constexpr juce::uint32 kVirtualDX7Colour = 0xffb8864a;

inline const char* sectionName (ModuleSection s)
{
    switch (s)
    {
        case ModuleSection::InputOutput: return "Input / Output";
        case ModuleSection::Oscillator:  return "Sound Generation";
        case ModuleSection::Filter:      return "Filter";
        case ModuleSection::Effect:      return "Effect";
        case ModuleSection::Utility:     return "Utility";
    }
    return "";
}

//==============================================================================
// The interface's colours. The defaults are Aquanode Modular's own; the colour
// selector (tap "Aquanode Modular" at the top of the sidebar) changes them and
// keeps them in the app's settings. Message thread only.
//==============================================================================
struct ThemeColours
{
    enum Slot
    {
        Background, Sidebar, LightText, DarkText,
        InputOutput, Oscillator, Filter, Effect, Utility,
        Plugins, Presets,
        KnobStart, KnobEnd,   // the Skeuomorphic knobs' value ring, from its start to its end
        NumSlots,
        NumGridSlots = KnobStart   // the colour panel's chips (the knob colours sit in its top row)
    };

    static const char* name (int slot)
    {
        static const char* names[NumSlots] = {
            "Patch Background", "Sidebar Background", "Light Text", "Dark Text",
            "Input / Output", "Sound Generation", "Filters", "Effects", "Utilities",
            "Plugin Titles", "Random Preset", "Knob Start", "Knob End" };
        return names[slot];
    }

    static const char* key (int slot)
    {
        static const char* keys[NumSlots] = {
            "background", "sidebar", "lightText", "darkText", "io", "oscillator", "filter", "effect", "utility",
            "plugins", "presets", "knobStart", "knobEnd" };
        return keys[slot];
    }

    static juce::Colour defaultColour (int slot)
    {
        static const juce::uint32 argb[NumSlots] = {
            0xff2b2723, 0xff000000, 0xffffffff, 0xff141414,
            0xffd9c34a, 0xff4a86d9, 0xff74cc8a, 0xff53b06a, 0xffd970b0,   // filters a brighter green than effects
            0xffc8a2ff, 0xff6fc7c9,
            0xff0a6cff, 0xff4ff0ff };   // the knob ring: a saturated blue into cyan
        return juce::Colour (argb[slot]);
    }

    juce::Colour colour[NumSlots];
    juce::Colour glassCol[NumSlots];   // the Glass look's own palette (its slots mean what glassName says)
    bool showGrid { true };   // the "+" markers on the patch area
    bool wireLines { false };  // the cables drawn as straight lines instead of hanging down
    bool fallingLeaves { true };   // Glass: the leaves drifting down over the patch area
    // (Grid, Wire and Leaves are kept in the app's settings and in every patch)
    // the Skeuomorphic look: the module cards shaded like hardware panels, their
    // knobs as glossy caps, their readouts as small green screens (the colours
    // stay the colourway's). Not touched by Reset All.
    bool skeuomorphic { false };
    // the Glass look (after the Soundscape Ecology plugin): a jungle gradient
    // under the patch, frosted translucent cards and panels, luminous knob arcs,
    // white text - in its own palette of greens, lagoon blues and aqua while
    // it is on (the colourway is kept and comes back with it off). Never on
    // together with Skeuomorphic.
    bool glass { false };

    ThemeColours() { resetAll(); resetGlass(); }
    void resetAll() { for (int i = 0; i < NumSlots; ++i) colour[i] = defaultColour (i); showGrid = true; }
    void resetGlass() { for (int i = 0; i < NumSlots; ++i) glassCol[i] = glassDefault (i); }
    juce::Colour get (int slot) const { return glass ? glassCol[slot] : colour[slot]; }
    // the palette the colour panel edits: the Glass one while Glass is on
    juce::Colour& edited (int slot) { return glass ? glassCol[slot] : colour[slot]; }
    juce::Colour editedDefault (int slot) const { return glass ? glassDefault (slot) : defaultColour (slot); }
    const char* label (int slot) const { return glass ? glassName (slot) : name (slot); }

    // the Glass look's palette (Soundscape Ecology's greens, lagoon blues and
    // aqua). Its two backgrounds are the jungle gradient's top and bottom (the
    // bottom also tints the frosted bars and panes); Sound Generation, Filters
    // and Effects colour the gradient's middle and, with Input / Output, its
    // light blooms; the knob slots are the drifting leaves and the glass sheen.
    static juce::Colour glassDefault (int slot)
    {
        static const juce::uint32 argb[NumSlots] = {
            0xff32a1a8, 0xff0c482e, 0xffffffff, 0xff0c2a24,
            0xff5fe3e0, 0xff2fd17a, 0xff17b8d4, 0xffa8f05a, 0xffe0a86a,   // aqua, canopy, lagoon, new growth, warm bark
            0xffc8f5d8, 0xff5fe3e0,
            0xffeafff2, 0xffffffff };
        return juce::Colour (argb[slot]);
    }
    static const char* glassName (int slot)
    {
        static const char* names[NumSlots] = {
            "Gradient Top", "Gradient Bottom", "Light Text", "Dark Text",
            "Input / Output", "Sound Generation", "Filters", "Effects", "Utilities",
            "Plugin Titles", "Random Preset", "Leaves", "Sheen" };
        return names[slot];
    }

    // text on a background: the Dark Text colour on light ones, the Light Text
    // colour on dark ones (above `threshold` perceived brightness counts as light)
    juce::Colour textOn (juce::Colour background, float threshold = 0.55f) const
    {
        return background.getPerceivedBrightness() > threshold ? get (DarkText) : get (LightText);
    }
    // module cards keep dark text on every default card colour; only a card
    // given a really dark colour of its own switches to the light text
    // (Glass: the cards are frosted glass over the green - white text on all)
    juce::Colour textOnCard (juce::Colour card) const { return glass ? juce::Colours::white.withAlpha (0.94f) : textOn (card, 0.33f); }
};

inline ThemeColours& themeColours()
{
    static ThemeColours t;
    return t;
}

inline juce::Colour sectionColour (ModuleSection s)
{
    const auto& t = themeColours();
    switch (s)
    {
        case ModuleSection::InputOutput: return t.get (ThemeColours::InputOutput);
        case ModuleSection::Oscillator:  return t.get (ThemeColours::Oscillator);
        case ModuleSection::Filter:      return t.get (ThemeColours::Filter);
        case ModuleSection::Effect:      return t.get (ThemeColours::Effect);
        case ModuleSection::Utility:     return t.get (ThemeColours::Utility);
    }
    return juce::Colours::grey;
}

inline juce::Colour familyColour (ModuleFamily f, ModuleSection fallback)
{
    if (f == ModuleFamily::DX)
        return juce::Colour (kVirtualDX7Colour);
    return sectionColour (fallback);
}

// The top bar and the mutator strip: near-black on the default colours, the
// sidebar's colour in a colourway (or any sidebar colour of your own)
inline bool barsAreThemed()
{
    return themeColours().glass || themeColours().get (ThemeColours::Sidebar) != ThemeColours::defaultColour (ThemeColours::Sidebar);
}
inline juce::Colour barColour()
{
    return barsAreThemed() ? themeColours().get (ThemeColours::Sidebar) : juce::Colour (0xff141210);
}
inline juce::Colour barTextColour() { return themeColours().textOn (barColour()); }

// The colour of the Plugins section title (the rows carry each plugin's accent)
inline juce::Colour pluginSectionColour() { return themeColours().get (ThemeColours::Plugins); }

// The colour the Preset Generation section and its buttons use
inline juce::Colour presetSectionColour() { return themeColours().get (ThemeColours::Presets); }

// Fixed sidebar section order. The Preset Generation section (the patch
// generators) is drawn after these by the sidebar itself.
inline const std::array<ModuleSection, 5>& allSections()
{
    static const std::array<ModuleSection, 5> s {
        ModuleSection::InputOutput, ModuleSection::Oscillator,
        ModuleSection::Filter, ModuleSection::Effect, ModuleSection::Utility };
    return s;
}

//==============================================================================
// ParamSpec / SocketSpec / ModuleDescriptor
//==============================================================================
struct ParamSpec
{
    juce::String id;
    juce::String label;
    ParamType type { ParamType::Rotary };
    float minValue { 0.0f }, maxValue { 1.0f }, defaultValue { 0.0f };
    bool logarithmic { false };
    float interval { 0.0f };            // slider step (0 = continuous)
    juce::StringArray choices;          // for Combo / RotarySteppedList
    int widthUnits { 1 };               // 1 = one knob slot, 2 = wide (combo)
    int row { 0 };                      // explicit row index inside the module
    juce::String unitSuffix;            // "Hz", "%", "ms", "s"
    juce::String visibleWhenParamId;    // empty = always visible
    float visibleWhenEquals { 0.0f };
    bool hidden { false };              // stored/saved normally, but NOT rendered
                                        // as a generic control (custom UI owns it)

    ParamSpec& visibleWhen (const juce::String& paramId, float equals)
    {
        visibleWhenParamId = paramId;
        visibleWhenEquals = equals;
        return *this;
    }
    // a second condition, both must hold (e.g. an engine's knob that also
    // waits for a mode)
    juce::String visibleWhen2ParamId;
    float visibleWhen2Equals { 0.0f };
    ParamSpec& alsoVisibleWhen (const juce::String& paramId, float equals)
    {
        if (visibleWhenParamId.isEmpty())
            return visibleWhen (paramId, equals);
        visibleWhen2ParamId = paramId;
        visibleWhen2Equals = equals;
        return *this;
    }

    // shown while another knob is at least a value (Band Splitter: Split 4 from Bands 5 on);
    // uses visibleWhenParamId / visibleWhenEquals as the knob and the least value
    bool visibleFromMin { false };
    ParamSpec& visibleFrom (const juce::String& paramId, float atLeast)
    {
        visibleWhenParamId = paramId;
        visibleWhenEquals = atLeast;
        visibleFromMin = true;
        return *this;
    }

    ParamSpec& hide() { hidden = true; return *this; }

    bool modulatable { true };          // false = knob refuses param-mod cables
                                        // (e.g. params that trigger cache rebuilds)
    ParamSpec& noMod() { modulatable = false; return *this; }

    // a hidden parameter that still accepts param-mod cables through the
    // module's custom extra-content UI (e.g. Pitch Lock Filter's note keys).
    // The extra-content component must implement CustomParamCableTargets so
    // the editor knows where cables should attach.
    bool hiddenCableTarget { false };
    ParamSpec& cableTargetInCustomUI() { hiddenCableTarget = true; return *this; }

    // A setting that changes how the graph is wired (a Reroute module's bus):
    // the card asks for the graph to be rebuilt when it changes, and no
    // mutation or randomizing ever touches it.
    bool affectsRouting { false };
    ParamSpec& routing() { affectsRouting = true; modulatable = false; return *this; }

    // The value a patch saved before this parameter existed opens with
    // (instead of the default a new card starts at): a level a module was
    // brought down to keeps the patches made before at the level they had.
    bool hasMissingValue { false };
    float missingValue { 0.0f };
    ParamSpec& whenMissing (float v) { hasMissingValue = true; missingValue = v; return *this; }

    // A fixed calibration, not a sound setting: never shown, modulated,
    // mutated or bred.
    bool calibration { false };

    // How a knob writes its value, when the plain number and unitSuffix are
    // not enough: a module whose knobs run in its engine's own units can
    // show what they mean (Vita's knobs: a cutoff in Hz, a time in seconds).
    // Both optional; valueFromText reads back what the user typed.
    std::function<juce::String (double)> textFromValue;
    std::function<double (const juce::String&)> valueFromText;

    // A logarithmic knob that turns evenly through the decades (each decade
    // the same share of its travel) instead of the usual skew around the
    // geometric middle, which crowds a wide range's bottom into the first
    // quarter: for ranges of many decades (Free Delay's 1 us .. 10 s).
    bool evenDecades { false };
    ParamSpec& decades() { logarithmic = true; evenDecades = true; return *this; }
};

inline ParamSpec makeRotary (juce::String id, juce::String label,
                             float mn, float mx, float def, int row = 0,
                             juce::String suffix = {}, bool log = false, float interval = 0.0f)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::Rotary;
    p.minValue = mn; p.maxValue = mx; p.defaultValue = def;
    p.logarithmic = log; p.interval = interval;
    p.row = row; p.unitSuffix = std::move (suffix);
    return p;
}

// A sound source's output level, brought down to sit with the others (about
// -12 dBFS at its peak and -16 dBFS in its loudest 100 ms for one note at its
// defaults). Patches saved before it open at 1 - their level as it was.
inline ParamSpec makeOutputTrim (float decibels)
{
    // (up to +12 dB: the physical models need a lift to meet the rest - 6.7.0)
    auto p = makeRotary ("outTrim", "Output Trim", 0.0f, 4.0f, juce::Decibels::decibelsToGain (decibels));
    p.hidden = true;
    p.modulatable = false;
    p.calibration = true;
    return p.whenMissing (1.0f);
}

inline ParamSpec makeCombo (juce::String id, juce::String label,
                            juce::StringArray choices, int defaultIndex,
                            int row = 0, int widthUnits = 2)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::Combo;
    p.choices = std::move (choices);
    p.minValue = 0.0f; p.maxValue = (float) (p.choices.size() - 1);
    p.defaultValue = (float) defaultIndex;
    p.widthUnits = widthUnits; p.row = row;
    return p;
}

inline ParamSpec makeSteppedList (juce::String id, juce::String label,
                                  juce::StringArray choices, int defaultIndex, int row = 0)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::RotarySteppedList;
    p.choices = std::move (choices);
    p.minValue = 0.0f; p.maxValue = (float) (p.choices.size() - 1);
    p.defaultValue = (float) defaultIndex;
    p.interval = 1.0f; p.row = row;
    return p;
}

// Horizontal bar slider with the value shown to its right. Meant for things
// that read as a position or a span rather than an amount - sample start/end
// being the obvious case, where a round knob tells you very little.
// Behaves exactly like a Rotary otherwise: modulatable, cable-droppable.
inline ParamSpec makeHBar (juce::String id, juce::String label,
                           float mn, float mx, float def, int row = 0,
                           juce::String suffix = {}, int widthUnits = 5)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::HBar;
    p.minValue = mn; p.maxValue = mx; p.defaultValue = def;
    p.row = row; p.widthUnits = widthUnits;
    p.unitSuffix = std::move (suffix);
    return p;
}

inline ParamSpec makeButton (juce::String id, juce::String label, int row = 0, int widthUnits = 1)
{
    ParamSpec p;
    p.id = std::move (id);   p.label = std::move (label);
    p.type = ParamType::Button;
    p.widthUnits = widthUnits; p.row = row;
    return p;
}

//==============================================================================
// Implemented by extra-content components (createExtraContentComponent) that
// expose their OWN drop targets for knob-modulation cables - e.g. the Pitch
// Lock Filter's keyboard, where each key is a hidden nc0..nc11 parameter.
// All coordinates are local to the extra-content component.
//==============================================================================
struct CustomParamCableTargets
{
    virtual ~CustomParamCableTargets() = default;

    // paramId of the target under localPos, or {} if none
    virtual juce::String paramTargetAt (juce::Point<int> localPos) const = 0;

    // where a cable to this param should visually attach (local coords)
    virtual juce::Point<int> paramTargetCentre (const juce::String& paramId) const = 0;
};

//==============================================================================
// Implemented by extra-content components that change ORDINARY (visible)
// parameters themselves - e.g. the Piano Roll's full-window editor, which has
// its own Tempo and Bars controls. The editor fills in the callback so the
// module's generic knobs follow along. Optional.
//==============================================================================
struct ExtraContentParamListener
{
    virtual ~ExtraContentParamListener() = default;

    // full refresh: knob values AND layout (the card may change size)
    std::function<void()> paramsChangedByContent;

    // light refresh: only the knob values, cheap enough to call on every
    // mouse-drag event (3-Bell EQ's display moving a bell)
    std::function<void()> paramValuesChangedByContent;
};

struct SocketSpec
{
    juce::String id;
    juce::String label;
    SocketKind kind { SocketKind::Audio };
    SocketDirection direction { SocketDirection::Input };

    // A socket no cable can be patched to and the card does not draw: the
    // graph wires it by itself (the Reroute modules' bus, which joins a Send
    // to the Returns of its bus as if a cable ran between them).
    bool hidden { false };

    // A modulation output that runs 0..1 (an envelope, a gate, Velocity, a
    // Macro) rather than -1..1 (an LFO): a knob cable from it starts unipolar
    // (the knob moves one way), and its Bipolar switch centres it (2x - 1)
    bool unipolar { false };
};

inline SocketSpec audioIn  (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Audio,      SocketDirection::Input  }; }
inline SocketSpec audioOut (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Audio,      SocketDirection::Output }; }
inline SocketSpec modIn    (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Modulation, SocketDirection::Input  }; }
inline SocketSpec midiIn   (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Midi,       SocketDirection::Input  }; }
inline SocketSpec midiOut  (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Midi,       SocketDirection::Output }; }
inline SocketSpec modOut   (juce::String id, juce::String label) { return { std::move (id), std::move (label), SocketKind::Modulation, SocketDirection::Output }; }
inline SocketSpec hiddenSocket (SocketSpec s) { s.hidden = true; return s; }
inline SocketSpec unipolarOut (juce::String id, juce::String label) { auto s = modOut (std::move (id), std::move (label)); s.unipolar = true; return s; }
// Tempo per lane (6.9.10): a module that follows the patch's BPM takes the
// tempo arriving here instead, while something is patched in (a Tempo
// module's Tempo Out); the engine does it (setTempo), the module does nothing.
// Always the module's last input.
inline SocketSpec tempoIn() { return modIn ("tempoIn", "Tempo In"); }
// the tempo as a signal on a cable: BPM / 1000
constexpr double kTempoSignalScale = 1000.0;

struct ModuleDescriptor
{
    juce::String typeId;        // e.g. "osc.basic"
    juce::String displayName;
    // What the sidebar shows on a long hover. One sentence on what the module
    // does; a second only where the socket names need it (several are
    // module-specific enough that the obvious partner is worth naming); a
    // third only where modulation behaves unexpectedly - refuses a knob that
    // looks ordinary, or works somewhere you would not think to try.
    juce::String description;
    ModuleSection section { ModuleSection::Utility };
    ModuleFamily family { ModuleFamily::Standard };
    int sidebarOrder { 0 };     // explicit position inside its section

    // A module that belongs to one of the plugins merged into Aquanode
    // Modular (Plugins section, "PluginName - Kind" modules) carries that
    // plugin's accent; it overrides the family and section colours.
    juce::Colour accent {};

    // false for modules that are not listed under their section in the
    // sidebar - the GUI versions of the merged plugins, which the Plugins
    // section offers instead
    bool listedInSidebar { true };

    // A module of Aquanode Modular's own (not a "plugin.<id>.<kind>" type)
    // that is in fact part of one of the merged plugins - the VirtualDX7
    // operator, envelopes and LFO its patch version is built from: the
    // sidebar lists it with that plugin's modules (its displayName then reads
    // "PluginName - Kind"). Empty for everything else.
    juce::String listedWithPlugin;

    // Extra words the sidebar's search finds the module by, as if they were
    // part of its group ("chord" for Midi Add). Empty for most modules.
    juce::String searchKeywords;

    bool isPluginModule() const { return typeId.startsWith ("plugin.") || listedWithPlugin.isNotEmpty(); }

    // card colour: the plugin accent, else the family accent, else the section's
    juce::Colour colour() const { return accent.isTransparent() ? familyColour (family, section) : accent; }
    std::vector<SocketSpec> sockets;
    std::vector<ParamSpec> params;

    // In the sidebar, listed right after this module (its type id) instead
    // of by name within its group (Free Delay right below Delay). Empty: by name.
    juce::String listedAfter;

    // Titled separators on a long card: before knob row r a line with the
    // title rowSections[r]; extraSection titles the custom content below the
    // knobs. Empty for most modules (no separators).
    std::map<int, juce::String> rowSections;
    juce::String extraSection;

    std::vector<const SocketSpec*> inputs() const
    {
        std::vector<const SocketSpec*> v;
        for (auto& s : sockets) if (s.direction == SocketDirection::Input) v.push_back (&s);
        return v;
    }

    std::vector<const SocketSpec*> outputs() const
    {
        std::vector<const SocketSpec*> v;
        for (auto& s : sockets) if (s.direction == SocketDirection::Output) v.push_back (&s);
        return v;
    }

    int numInputs()  const { return (int) inputs().size(); }
    int numOutputs() const { return (int) outputs().size(); }

    int inputIndexOf (const juce::String& socketId) const
    {
        auto v = inputs();
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i]->id == socketId) return i;
        return -1;
    }

    int outputIndexOf (const juce::String& socketId) const
    {
        auto v = outputs();
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i]->id == socketId) return i;
        return -1;
    }

    const ParamSpec* findParam (const juce::String& id) const
    {
        for (auto& p : params) if (p.id == id) return &p;
        return nullptr;
    }
};

//==============================================================================
// Misc DSP helpers
//==============================================================================
// The frequency of a (fractional) MIDI note: an MTS-ESP master's tuning when
// one runs in the session, equal temperament (A4 = 440 Hz) otherwise (Tuning.h)
inline double midiNoteToHz (int note) { return tuning::noteToHz ((double) note); }
inline double midiNoteToHz (double note) { return tuning::noteToHz (note); }

// "C-1" ... "G9" for MIDI 0..127 (middle C = C4 = 60)
inline juce::String midiNoteName (int note)
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    note = juce::jlimit (0, 127, note);
    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}

inline const juce::StringArray& midiNoteNameChoices()
{
    static const juce::StringArray choices = []
    {
        juce::StringArray a;
        for (int n = 0; n < 128; ++n)
            a.add (midiNoteName (n));
        return a;
    }();
    return choices;
}


static constexpr int kMaxVoices = 24;

// How long a one-pole exponential decay with time constant tauMs takes to
// fall 60 dB (ln 1000 = 6.91 time constants). The drums' Decay knobs are such
// time constants, and their voiceTailSeconds() must cover this whole fade:
// the engine frees a released voice once that tail has run out, so a tail of
// one time constant (what they used to report) cut every hit off while
// still clearly audible.
inline double decayTailSeconds (double tauMs) { return tauMs * 0.001 * 6.91; }

// Inputs that only did what a cable onto a knob does ("Pos In" with its "Pos Mod"
// depth knob, "Pan Mod" ...) were removed. A cable into one of them - from an older
// patch or a generator - becomes a cable onto the knob, with the depth the removed
// depth knob gave it: depth = depthKnob * depthScale, or fixedDepth without one.
struct LegacyKnobInput
{
    const char* typeId;
    const char* socket;
    const char* param;        // the knob the cable goes onto now
    const char* depthParam;   // the removed depth knob (nullptr: none)
    float depthScale;
    float depthDefault;       // the depth knob's default value
    float fixedDepth;         // used when there was no depth knob
};

inline const LegacyKnobInput* legacyKnobInput (const juce::String& typeId, const juce::String& socket)
{
    static const LegacyKnobInput table[] = {
        { "osc.chordwavetable", "posIn",    "position", "posDepth",   0.01f, 100.0f, 0.0f },
        { "osc.chordwavetable", "morphIn",  "morph",    "morphDepth", 0.01f, 100.0f, 0.0f },
        { "osc.complex",        "timbreIn", "timbre",   "timbreMod",  0.01f, 0.0f,   0.0f },
        { "util.crossfade",     "fadeIn",   "fade",     "modDepth",   0.01f, 0.0f,   0.0f },
        { "filter.formant",     "vowelIn",  "vowel",    "vowelMod",   0.01f, 0.0f,   0.0f },
        { "osc.formant",        "vowelIn",  "vowel",    "vowelMod",   0.01f, 0.0f,   0.0f },
        { "osc.wavetable",      "posIn",    "position", "posMod",     0.01f, 0.0f,   0.0f },
        { "osc.warp",           "shapeIn",  "shape",    "shapeMod",   0.01f, 0.0f,   0.0f },
        { "osc.wind",           "breathIn", "breath",   "breathMod",  1.0f / 150.0f, 0.0f, 0.0f },
        { "osc.pd",             "dcwIn",    "dcw",      "dcwMod",     0.01f, 0.0f,   0.0f },
        { "util.panning",       "panMod",   "pan",      nullptr,      0.0f,  0.0f,   0.5f },
        { "fx.stereowidth",     "widthMod", "width",    nullptr,      0.0f,  0.0f,   0.5f },
    };
    for (const auto& e : table)
        if (typeId == e.typeId && socket == e.socket)
            return &e;
    return nullptr;
}

inline const LegacyKnobInput* legacyDepthKnob (const juce::String& typeId, const juce::String& paramId)
{
    for (const auto* socket : { "posIn", "morphIn", "timbreIn", "fadeIn", "vowelIn", "shapeIn", "breathIn", "dcwIn" })
        if (const auto* e = legacyKnobInput (typeId, socket))
            if (e->depthParam != nullptr && paramId == e->depthParam)
                return e;
    return nullptr;
}

// The last result of an expensive function (tan, pow, exp ...) of one input,
// worked out again only when the input changes: the same number as calling the
// function every sample, without the cost while a knob or a pitch stands still.
struct Memo
{
    template <typename Fn>
    float operator() (float input, Fn&& fn) noexcept
    {
        if (! valid || ! juce::exactlyEqual (input, key))
        {
            key = input;
            value = fn (input);
            valid = true;
        }
        return value;
    }
    void forget() noexcept { valid = false; }

    float key { 0.0f }, value { 0.0f };
    bool valid { false };
};

struct MemoD
{
    template <typename Fn>
    double operator() (double input, Fn&& fn) noexcept
    {
        if (! valid || ! juce::exactlyEqual (input, key))
        {
            key = input;
            value = fn (input);
            valid = true;
        }
        return value;
    }
    void forget() noexcept { valid = false; }

    double key { 0.0 }, value { 0.0 };
    bool valid { false };
};

//==============================================================================
// Standard amplitude handling for a pitched generator, matching Oscillator's
// convention exactly: when Env In is patched the envelope IS the amplitude;
// when it is not, a simple on/off gate with a ~3 ms anti-click ramp stands in.
// Note velocity always multiplies on top.
//==============================================================================
class ModuleVoiceGate
{
public:
    static constexpr double kRampSeconds = 0.003;

    ModuleVoiceGate() { resetAll(); }

    void resetAll()
    {
        for (int v = 0; v < kMaxVoices; ++v)
            resetVoice (v);
    }

    void resetVoice (int v) { level[v] = 0.0f; on[v] = false; vel[v] = 1.0f; }

    void noteOn (int v)  { on[v] = true; }
    void noteOff (int v) { on[v] = false; }
    void setVelocity (int v, float velocity01) { vel[v] = velocity01; }

    float next (int v, bool envConnected, float envIn, double sampleRate)
    {
        if (envConnected)
            return envIn * vel[v];

        const float step = (float) (1.0 / (kRampSeconds * sampleRate));
        const float target = on[v] ? 1.0f : 0.0f;
        if (level[v] < target)      level[v] = juce::jmin (target, level[v] + step);
        else if (level[v] > target) level[v] = juce::jmax (target, level[v] - step);
        return level[v] * vel[v];
    }

    bool isOn (int v) const { return on[v]; }

private:
    float level[kMaxVoices] {};
    bool on[kMaxVoices] {};
    float vel[kMaxVoices] {};
};

//==============================================================================
// Mono glide (portamento). Only meaningful when a generator's voice limit is
// 1: it tracks the pitch that is actually sounding, so a new note starts from
// wherever the previous one left off and slides to its own target. Works in
// semitone space, which makes the slide musically linear rather than linear
// in Hz. With Glide at 0 the pitch jumps immediately and ModuleVoicePool's
// crossfade between the old and new voice hides the discontinuity.
//==============================================================================
class ModuleGlide
{
public:
    ModuleGlide() { resetAll(); }

    void resetAll()
    {
        for (int v = 0; v < kMaxVoices; ++v)
            current[v] = target[v] = 60.0f;
        liveSemi = 60.0f;
        hasLive = false;
    }

    void resetVoice (int v) { current[v] = target[v]; }

    // from voiceNoteOn: in mono the new voice picks up the sounding pitch
    void noteOn (int v, float semitones, bool mono)
    {
        target[v] = semitones;
        current[v] = (mono && hasLive) ? liveSemi : semitones;
    }

    // once per sample per voice; `sounding` is false for voices fading out
    // (they hold their pitch and must not steer the shared live pitch)
    float next (int v, float glideMs, bool mono, bool sounding, double sampleRate)
    {
        if (! sounding)
            return current[v];

        if (! mono || glideMs < 0.5f)
            current[v] = target[v];
        else
        {
            const float coeff = 1.0f - std::exp ((float) (-1.0 / (glideMs * 0.001 * sampleRate)));
            current[v] += coeff * (target[v] - current[v]);
        }

        if (mono)
        {
            liveSemi = current[v];
            hasLive = true;
        }
        return current[v];
    }

private:
    float current[kMaxVoices] {}, target[kMaxVoices] {};
    float liveSemi { 60.0f };
    bool hasLive { false };
};

//==============================================================================
// Per-generator polyphony limit with voice stealing: each sound generator
// can cap how many of the engine's global voices it actually sounds on.
// New notes always sound. The limit counts every voice still audible - held
// notes AND released ones ringing out their tails - so Voices 1 really is one
// voice (a new note cuts the last one's tail, as on a mono synth) and Voices 4
// never lets a fifth tail through. Over the limit, a releasing voice goes
// first (the oldest), then the oldest held one. A held note muted to make room
// comes back when a newer note is let go (note priority); a released voice
// that was muted stays silent for good.
//==============================================================================
class ModuleVoicePool
{
public:
    void noteOn (int v, int limit)
    {
        held[v] = true;
        releasing[v] = false;
        dead[v] = false;
        muted[v] = false;
        order[v] = ++counter;
        recompute (limit);
    }

    void noteOff (int v, int limit)
    {
        if (! held[v])
            return;
        held[v] = false;
        if (muted[v])
            dead[v] = true;          // muted to make room: its tail must not come back
        else
            releasing[v] = true;     // rings out - and still counts against the limit
        recompute (limit);
    }

    void resetVoice (int v)
    {
        held[v] = releasing[v] = dead[v] = false;
        muted[v] = false;
        gain[v] = 1.0f;     // a fresh voice starts audible; its envelope shapes the attack
        recompute (lastLimit);
    }

    void resetAll()
    {
        for (int v = 0; v < kMaxVoices; ++v)
        {
            held[v] = releasing[v] = dead[v] = muted[v] = false;
            gain[v] = 1.0f;
        }
        counter = 0;
    }

    ModuleVoicePool() { resetAll(); }

    bool isMuted (int v) const { return muted[v]; }

    // Per-voice fade, advanced once per sample by the generator. A stolen
    // voice ramps out over kFadeSeconds instead of being cut dead (which
    // clicks), and a re-used voice ramps back in.
    float nextGain (int v, double sampleRate)
    {
        const float target = muted[v] ? 0.0f : 1.0f;
        const float step = (float) (1.0 / (kFadeSeconds * sampleRate));
        if (gain[v] < target)      gain[v] = juce::jmin (target, gain[v] + step);
        else if (gain[v] > target) gain[v] = juce::jmax (target, gain[v] - step);
        return gain[v];
    }

    // true once a muted voice has fully faded: the generator can skip its DSP
    bool isSilent (int v) const { return muted[v] && gain[v] <= 0.0f; }

    // how many voices are audible now (held or ringing out, not muted)
    int audibleCount() const
    {
        int n = 0;
        for (int v = 0; v < kMaxVoices; ++v)
            if ((held[v] || releasing[v]) && ! muted[v]) ++n;
        return n;
    }

private:
    void recompute (int limit)
    {
        limit = juce::jlimit (1, kMaxVoices, limit);
        lastLimit = limit;

        // every held voice may sound again (note priority); released ones keep their state
        int active = 0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            if (held[v])
                muted[v] = false;
            else if (dead[v])
                muted[v] = true;
            if ((held[v] || releasing[v]) && ! muted[v])
                ++active;
        }

        while (active > limit)
        {
            // the oldest releasing voice first, then the oldest held one
            int pick = -1;
            juce::uint64 oldest = ~ (juce::uint64) 0;
            for (int pass = 0; pass < 2 && pick < 0; ++pass)
                for (int v = 0; v < kMaxVoices; ++v)
                    if (! muted[v] && (pass == 0 ? releasing[v] : held[v]) && order[v] < oldest)
                    {
                        oldest = order[v];
                        pick = v;
                    }
            if (pick < 0)
                break;
            muted[pick] = true;
            if (releasing[pick])
            {
                releasing[pick] = false;
                dead[pick] = true;
            }
            --active;
        }
    }

    static constexpr double kFadeSeconds = 0.004;   // 4 ms de-click fade

    juce::uint64 order[kMaxVoices] {};
    juce::uint64 counter { 0 };
    int lastLimit { kMaxVoices };
    bool held[kMaxVoices] {}, releasing[kMaxVoices] {}, dead[kMaxVoices] {};
    bool muted[kMaxVoices] {};
    float gain[kMaxVoices] { };
};

// shared tempo-division table (Clock / Step Seq / Euclid)
inline const juce::StringArray& seqDivisionChoices()
{
    static const juce::StringArray choices
        { "1/1", "1/2", "1/4", "1/4T", "1/4.", "1/8", "1/8T", "1/8.",
          "1/16", "1/16T", "1/16.", "1/32" };
    return choices;
}

inline double seqDivisionBeats (int choice)
{
    static const double beats[12] = { 4.0, 2.0, 1.0, 2.0 / 3.0, 1.5, 0.5, 1.0 / 3.0, 0.75,
                                      0.25, 1.0 / 6.0, 0.375, 0.125 };
    return beats[juce::jlimit (0, 11, choice)];
}

using StereoFrame = std::array<float, 2>;

// The non-note MIDI the engine has seen, kept up to date sample by sample.
// Every module can read it (Midi Controls turns it into cables; pitched
// generators read the bend). Written by the audio thread only.
struct MidiControlState
{
    float bend { 0.0f };            // -1..1, centre 0
    float bendRangeSemis { 2.0f };  // set from a Midi Controls module, 2 by default
    float modWheel { 0.0f };        // 0..1
    float aftertouch { 0.0f };      // 0..1, channel pressure or the strongest poly pressure
    float cc[128] {};               // 0..1
    bool sustain { false };

    // what moved last, for Midi Learn: goes up by one with every bend, CC or
    // pressure message; lastControl 0..127 a CC, 128 pitch bend, 129 pressure
    juce::uint32 controlCount { 0 };
    int lastControl { -1 };

    // MPE (the MPE switch beside the module search): every note on a channel
    // of its own (2..16) carries its own pitch bend (semitones, on top of the
    // played bend), Slide (CC 74) and pressure. currentVoice is the voice the
    // engine is running a module for right now (-1: the global lane), so a
    // pitched generator's bend is its own note's. All zero with MPE off.
    float voiceBend[kMaxVoices] {};
    float voiceSlide[kMaxVoices] {};
    float voicePressure[kMaxVoices] {};
    bool voiceSlideSet[kMaxVoices] {};   // (6.9.12) its Slide has been sent (else none: not 0)
    // (6.9.12) the default MPE map: how far a full Slide moves a wavetable position, each way, in
    // 0..1 of the range - the MPE module's Wavetable knob (the first one in the patch), else a quarter
    float slideWavetableDepth { 0.0f };   // (6.9.15: none unless an MPE module's knob says so)
    float mpeBendRange { 48.0f };         // (6.9.15) a finger's whole bend, in semitones (the MPE module's)
    bool mpeSwitch { false };             // (6.9.16) the MPE switch beside the module search is on (Super MIDI)
    int currentVoice { -1 };

    float bendSemitones() const
    {
        return bend * bendRangeSemis + (currentVoice >= 0 ? voiceBend[currentVoice] : 0.0f);
    }
};

// One note event reported by a MIDI note driver (Arp, Always Midi, Piano Roll)
struct MidiDriverEvent
{
    int note { -1 };
    float velocity { 0.85f };   // note-ons only
    bool isOn { false };

    // MPE notes (the MPE Piano Roll): a note-on, or an expression event
    // (isExpression: neither on nor off), carries the note's own bend on top
    // of its key, and its Pressure and Slide. Only sockets of modules that
    // accept MPE (acceptsMpeMidi) are ever fed such a source.
    bool mpe { false };
    bool isExpression { false };
    float bend { 0.0f };        // semitones
    float pressure { -1.0f };   // 0..1; < 0: unchanged
    float slide { -1.0f };      // 0..1; < 0: unchanged
};

// the modules an MPE note source (the MPE Piano Roll's MPE Out) may feed: those
// whose voices read their own bend (MidiControls::bendSemitones) and the raw-MIDI
// ones that tell their notes apart by channel
// the CPU / RAM module: is one in the patch (the engine then times the modules)
inline std::atomic<int>& cpuMeterCount() { static std::atomic<int> n { 0 }; return n; }
inline bool cpuMeterActive() { return cpuMeterCount().load (std::memory_order_relaxed) > 0; }
// the process's resident memory in bytes (0 where it cannot be read); Engine/MemoryUsage.cpp
juce::int64 processResidentBytes();

inline bool typeAcceptsMpe (const juce::String& typeId)
{
    static const juce::StringArray ids { "osc.basic", "osc.drift", "osc.complex", "osc.formant", "osc.pd", "osc.additive",
                                         "osc.unison", "osc.warp", "osc.dualvco", "osc.dualvcoanalog", "osc.pwmosc",
                                         "osc.chordwavetable", "osc.wavetable", "osc.transwave", "osc.sampler", "osc.multisample",
                                         "osc.granulate", "osc.spectralresynth", "osc.resynthpiano", "osc.piano", "osc.feltpiano",
                                         "osc.midpiano", "osc.hardpiano", "osc.grand", "osc.bowed", "osc.wind", "osc.dxop",
                                         "osc.strings", "osc.cello", "osc.clarinet", "osc.guitaracoustic", "osc.guitarelectric",
                                         "plugin.vita", "plugin.aquamassif", "plugin.virtualdx7", "plugin.additivekeys",
                                         "plugin.fm12", "plugin.aquanova", "plugin.reflex", "plugin.tidebreeze", "plugin.phizmo",
                                         "plugin.alphabeta", "plugin.am20", "plugin.aliassynth", "plugin.aquavibrio",
                                         "osc.formantfm", "osc.supersaw", "filter.playableeq", "osc.drawbarorgan", "osc.growl" };
    return ids.contains (typeId);
}

// The most events a driver may report in one sample (a Piano Roll chord
// ending as the next one starts, plus previews, fits comfortably)
static constexpr int kMaxDriverEventsPerSample = 160;

// What the host said about its transport this block. In the standalone app,
// on Android and in hosts that report nothing, hasBpm stays false - anything
// offering "lock to host tempo" must fall back to its own tempo then.
// A plugin that can ask for the tempo of the patch it sits in to change (Fruit DAW; an Aquanode
// Modular nested in one hands the request on). See SynthModule::takeTempoRequest.
// A plugin that plays MPE once asked to (Vita): its card switches MPE on at the
// first note on a member channel (an MPE Piano Roll, or the MPE keys)
struct MpeReceiver
{
    virtual ~MpeReceiver() = default;
    virtual void enableMpe (bool on) = 0;    // audio thread
    // (6.9.10, Vita's card) MPE as the preset says instead of on with
    // Aquanode's member channels; the per-finger bend range the preset's
    // Bend range instead of 48 (audio thread, when the card's choice moves)
    virtual void setMpePolicy (bool fromPreset, bool bendFromPreset) { juce::ignoreUnused (fromPreset, bendFromPreset); }
    // (6.9.12) the default MPE map: how far a full Slide moves a wavetable position each way
    // (0..1 of the range; a quarter unless the patch's MPE module says otherwise), every block
    virtual void setSlideWavetableDepth (float eachWay) { juce::ignoreUnused (eachWay); }
};

struct TempoRequester
{
    virtual ~TempoRequester() = default;
    virtual double takeTempoRequest() = 0;          // audio thread; 0: none
};

struct HostTransport
{
    bool hasBpm { false };
    double bpm { 120.0 };
    bool isPlaying { false };

    // Play All / Stop All on a Piano Roll card: a count that goes up with
    // every press, and what it asked for. Every Piano Roll sees the new count
    // in the same block and goes back to its start - playing, or stopped -
    // on that block's first sample, so they all run in step.
    juce::uint32 rollCommand { 0 };
    bool rollCommandPlays { false };
};

//==============================================================================
// Global polyphony: the WHOLE patch is evaluated once
// per voice. The engine owns a single global voice pool; modules hold
// per-voice state indexed by the engine's voice number.
//==============================================================================

enum class VoiceMode
{
    Global,     // one instance of state, voices already summed at its inputs (effects, IO)
    PerVoice,   // inherently per-voice (oscillators, envelopes, LFOs)
    Flexible    // per-voice when fed by a per-voice source, global otherwise (filters, volume...)
};


//==============================================================================
// CPU Sleep (sleep on silence; the top bar's CPU Sleep switch, app-wide). An
// effect or filter whose audio inputs have been silent long enough - longer
// than anything it can still bring back out (a delay's longest time, an FFT
// frame) - and whose outputs have been silent for a second stops being run
// until sound arrives again, MIDI reaches it or one of its knobs is moved.
//==============================================================================
static constexpr float kSleepSilence = 1.0e-5f;   // -100 dBFS

// the top bar's switch (on by default; the editor loads the remembered choice)
inline std::atomic<bool>& cpuSleepEnabled()
{
    static std::atomic<bool> on { true };
    return on;
}

inline bool canSleepOnSilence (const ModuleDescriptor& d)
{
    if (d.section != ModuleSection::Effect && d.section != ModuleSection::Filter)
        return false;
    // the parts of a plugin's patch version work together with their siblings
    if (d.listedWithPlugin.isNotEmpty()
        || (d.typeId.startsWith ("plugin.") && d.typeId.fromFirstOccurrenceOf ("plugin.", false, false).containsChar ('.')))
        return false;
    bool in = false, out = false;
    for (const auto& s : d.sockets)
    {
        if (! s.hidden && s.kind == SocketKind::Midi && s.direction == SocketDirection::Output)
            return false;   // (it may send notes on its own clock)
        if (s.hidden || s.kind != SocketKind::Audio) continue;
        (s.direction == SocketDirection::Input ? in : out) = true;
    }
    return in && out;
}

// How long silence going in may still come out later, read off the knobs:
// the largest time a "Time" / "Delay" knob reaches (in ms or s); a delay
// knob counted in beats is taken as up to 8 s.
inline double sleepMemoryFromKnobs (const ModuleDescriptor& d)
{
    double most = 0.0;
    for (const auto& p : d.params)
    {
        const auto key = (p.id + " " + p.label).toLowerCase();
        if (! (key.contains ("time") || key.contains ("delay") || key.contains ("echo")))
            continue;
        if (p.unitSuffix == "ms")      most = juce::jmax (most, (double) p.maxValue * 0.001);
        else if (p.unitSuffix == "s")  most = juce::jmax (most, (double) p.maxValue);
        else if (p.type == ParamType::Combo || p.type == ParamType::RotarySteppedList)
        {
            // a list of note lengths ("1/8", "1/4D" ...): up to 8 s
            for (const auto& c : p.choices)
                if (c.containsChar ('/')) { most = juce::jmax (most, 8.0); break; }
        }
        else if (p.unitSuffix.isEmpty())
            most = juce::jmax (most, 8.0);   // (a time in beats or steps)
    }
    return juce::jmin (most, 30.0);
}

//==============================================================================
// SynthModule - per-instance DSP object base class
//==============================================================================
//==============================================================================
// A patch tuning for MIDI that a module cannot retune itself (a hosted plugin
// plays its own pitches): every note goes out on an MPE member channel of its
// own (2..16), its pitch bend (+-48 semitones, MPE's member range) set to the
// table's pitch around the nearest key. A finger already on a member channel
// (MPE in) keeps its channel and its own bend on top. Without a table (or at
// 12-TET, A4 = 440) notes pass as they are; a note that went out retuned is
// always let go where it went (6.9.10).
class TuningRetuner
{
public:
    template <typename Send>
    void process (const tuning::Table* t, const juce::MidiMessage& m, Send&& send)
    {
        const int ch = m.getChannel();
        if (m.isNoteOn())
        {
            const int k = m.getNoteNumber();
            if (t == nullptr || t->identity) { send (m); return; }
            if (! t->mapped[k]) return;
            const double p = tuning::tablePitch (*t, k);
            const int outNote = juce::jlimit (0, 127, (int) std::lround (p));
            Slot s;
            s.used = true;
            s.inCh = ch; s.inNote = k; s.outNote = outNote;
            s.offset = (float) (p - outNote);
            s.age = ++clock;
            s.outCh = ch >= 2 ? ch : freeChannel (send);
            for (auto& o : slots)   // a key struck again on its channel lets the first go
                if (o.used && o.inCh == ch && o.inNote == k) { send (juce::MidiMessage::noteOff (o.outCh, o.outNote)); o.used = false; }
            for (auto& o : slots)
                if (! o.used) { o = s; break; }
            send (juce::MidiMessage::pitchWheel (s.outCh, wheel (s.offset + (ch >= 2 ? chanBend[ch] : 0.0f))));
            send (juce::MidiMessage::noteOn (s.outCh, outNote, m.getVelocity()));
            return;
        }
        if (m.isNoteOff())
        {
            for (auto& o : slots)
                if (o.used && o.inCh == ch && o.inNote == m.getNoteNumber())
                {
                    send (juce::MidiMessage::noteOff (o.outCh, o.outNote, m.getVelocity()));
                    o.used = false;
                    return;
                }
            send (m);
            return;
        }
        if (m.isPitchWheel() && ch >= 2)
        {
            chanBend[ch] = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 48.0f;
            bool mapped = false;
            for (auto& o : slots)
                if (o.used && o.outCh == ch && o.inCh == ch) { send (juce::MidiMessage::pitchWheel (ch, wheel (o.offset + chanBend[ch]))); mapped = true; }
            if (! mapped) send (m);
            return;
        }
        if (m.isAftertouch())
        {
            for (auto& o : slots)
                if (o.used && o.inCh == ch && o.inNote == m.getNoteNumber())
                {
                    send (juce::MidiMessage::aftertouchChange (o.outCh, o.outNote, m.getAfterTouchValue()));
                    return;
                }
            send (m);
            return;
        }
        if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& o : slots)
                if (o.used) { send (juce::MidiMessage::noteOff (o.outCh, o.outNote)); o.used = false; }
        send (m);
    }

private:
    struct Slot { bool used { false }; int inCh { 1 }, inNote { 0 }, outCh { 2 }, outNote { 0 }; float offset { 0.0f }; juce::uint32 age { 0 }; };

    static int wheel (float semis) { return juce::jlimit (0, 16383, 8192 + (int) std::lround (semis / 48.0f * 8192.0f)); }

    template <typename Send>
    int freeChannel (Send&& send)
    {
        for (int k = 0; k < 15; ++k)
        {
            const int c = 2 + (int) ((next + (juce::uint32) k) % 15u);
            bool taken = false;
            for (const auto& o : slots)
                taken = taken || (o.used && o.outCh == c);
            if (! taken) { next = (juce::uint32) (c - 1); return c; }
        }
        // all taken: the oldest note lets go of its channel
        Slot* oldest = nullptr;
        for (auto& o : slots)
            if (o.used && (oldest == nullptr || o.age < oldest->age)) oldest = &o;
        if (oldest == nullptr) return 2;
        send (juce::MidiMessage::noteOff (oldest->outCh, oldest->outNote));
        oldest->used = false;
        return oldest->outCh;
    }

    std::array<Slot, 32> slots {};
    float chanBend[17] {};
    juce::uint32 clock { 0 }, next { 0 };
};

class SynthModule
{
public:
    SynthModule() = default;
    virtual ~SynthModule() = default;

    // called by the factory right after construction
    void initialiseFromDescriptor (const ModuleDescriptor& d)
    {
        descriptor = &d;
        const auto n = d.params.size();
        paramValues.reset (n > 0 ? new std::atomic<float>[n] : nullptr);
        for (size_t i = 0; i < n; ++i)
        {
            paramValues[i].store (d.params[i].defaultValue, std::memory_order_relaxed);
            paramIndexById[d.params[i].id.toStdString()] = (int) i;
        }
        voicesParamIndex = paramIndexById.count ("voices") ? paramIndexById["voices"] : -1;
        glideParamIndex  = paramIndexById.count ("glide")  ? paramIndexById["glide"]  : -1;
        outTrimParamIndex = paramIndexById.count ("outTrim") ? paramIndexById["outTrim"] : -1;
        sleepEligible = canSleepOnSilence (d);
        audioInMask = audioOutMask = 0;
        {
            int ii = 0, oi = 0;
            for (const auto& s : d.sockets)
            {
                const bool isIn = s.direction == SocketDirection::Input;
                if (s.kind == SocketKind::Audio && ii < 32 && oi < 32)
                    (isIn ? audioInMask : audioOutMask) |= (isIn ? 1u << ii : 1u << oi);
                (isIn ? ii : oi)++;
            }
        }
        sleepKnobMemory = sleepMemoryFromKnobs (d);
        paramMod.assign (n, 0.0f);
        paramMin.resize (n);
        paramMax.resize (n);
        paramStepped.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            paramMin[i] = d.params[i].minValue;
            paramMax[i] = d.params[i].maxValue;
            const auto t = d.params[i].type;
            paramStepped[i] = (t == ParamType::Combo || t == ParamType::RotarySteppedList || t == ParamType::Button) ? 1 : 0;
        }
        for (auto& f : inputConnectedFlags) f.store (false, std::memory_order_relaxed);
    }

    const ModuleDescriptor& getDescriptor() const { return *descriptor; }

    //=== lifecycle ============================================================
    virtual void prepare (double newSampleRate) { sampleRate = newSampleRate; }
    virtual void reset() {}                            // wipe transient state (voices/filters/delays), no reallocation
    virtual void blockStart() {}                       // once per processBlock (latch buffers etc.)
    // A tempo this module wants the patch to take (Fruit DAW's "set the song's tempo to this
    // sample's"); 0: none. Asked once a block, audio thread; it goes into the BPM box, or up to
    // the host of a nested Aquanode.
    virtual double takeTempoRequest() { return 0.0; }
    virtual void setTempo (double bpm) { tempoBpm = bpm; }
    // (6.9.10) the tempo comes from a lane (a Tempo In, its own or upstream):
    // then it wins over the module's own tempo knob, and over the host's
    // position (a clock does not lock to the DAW's beats then)
    void setTempoFromLane (bool b) noexcept { laneTempo = b; }
    bool tempoFromLane() const noexcept { return laneTempo; }
    double currentTempo() const noexcept { return tempoBpm; }
    void setHostTransport (const HostTransport& t) { hostTransport = t; }
    void setMidiControls (const MidiControlState* state) { midiControls = state; }

    // A source whose knob cables are ABSOLUTE (Curve CV): its 0..1 output is
    // mapped onto the target knob's whole min..max range instead of being
    // added to the knob's current value. Cable depth then blends between the
    // knob's own setting and that mapped value; negative depth inverts it.
    virtual bool drivesKnobsAbsolutely() const { return false; }

    // An absolute source that may not be driving yet (Midi Learn, until its
    // control first moves): while false its knob cables leave the knobs at
    // their own settings instead of pulling them somewhere. Audio thread.
    virtual bool drivesKnobsNow() const { return true; }

    // AudioReroute: the bus (0..) a Reroute Send writes to or a Reroute
    // Return reads from; -1 for every other module. The graph joins each Send
    // to the Returns of its bus (hidden sockets "busOut" -> "busIn").
    virtual int rerouteBus() const { return -1; }
    virtual bool isRerouteSend() const { return false; }

    // Only Midi Controls answers: the pitch-bend range it sets, < 0 = none
    virtual float bendRangeSetting() const { return -1.0f; }
    // (6.9.12) the MPE module: how far Slide moves a wavetable (0..1 each way); < 0: no say
    virtual float slideWavetableSetting() const { return -1.0f; }
    virtual float mpeBendRangeSetting() const { return -1.0f; }   // (6.9.15) the MPE module's Bend Range
    // (6.9.15) a pill at the right of the card's top row, as the filters' Graph: its name (empty:
    // none), whether it is on, and what pressing it does (message thread; the card re-lays out)
    virtual juce::String titlePillName() const { return {}; }
    virtual bool titlePillOn() const { return false; }
    virtual void titlePillPressed() {}
    // (6.9.15) a Microtuning (Global, Linear) whose scale is not 12 notes: its notes per
    // period and the key of its first degree, for Aquanode's on-screen keys
    virtual bool keyboardSteps (int& notesPerPeriod, int& degreeZeroKey) const { juce::ignoreUnused (notesPerPeriod, degreeZeroKey); return false; }

    // Snapshot of the plugin's host-automatable modulation parameters, handed
    // to every module once per block. Only DAW Mod reads it.
    void setHostModValues (const float* values) { hostModValues = values; }

    // Appended to the module's name in its title bar (DAW Mod uses it to show
    // which host parameter slot it owns). Empty for everything else.
    virtual juce::String titleSuffix() const { return {}; }

    // For modules with a Midi Out: false means the notes are ADDED to what the
    // player is holding (Midi Add), true means they REPLACE them for any
    // generator listening (Arp, Always Midi). Everything else is irrelevant.
    virtual bool midiSourceReplacesInput() const { return false; }

    //=== MIDI note drivers ===================================================
    // A module that emits notes on its OWN clock rather than at the moment a
    // key is pressed (Arp, Always Midi). The engine ticks these once per
    // sample and turns what they report into real voices.
    virtual bool isMidiNoteDriver() const { return false; }
    // its Midi Out sends MPE notes (their own bend, Pressure, Slide): it only
    // connects to modules that accept them (typeAcceptsMpe)
    virtual bool emitsMpeMidi() const { return false; }

    // CPU / RAM module (Aquanode). While one is in the patch the engine times
    // the modules (a random sample in 16, scaled up; a hosted plugin times its
    // own blocks exactly) and keeps each one's share of real time here; the
    // memory a module took when it was made (and prepared) is noted once.
    std::atomic<float> cpuShare { 0.0f };          // 0..1 of real time, smoothed
    std::atomic<juce::int64> ramBytes { 0 };       // approx., from the process's resident memory
    double cpuNs { 0.0 };                          // (audio thread) this block's time so far
    virtual bool timesItself() const { return false; }
    struct CpuReading
    {
        float chainShare { 0.0f }, globalShare { 0.0f };
        juce::int64 chainRam { 0 };
        bool connected { false };
        int numTop { 0 };
        const juce::String* topName[3] { nullptr, nullptr, nullptr };   // (the registry's names: they live as long as the app)
        float topShare[3] { 0.0f, 0.0f, 0.0f };
    };
    virtual bool isCpuMeter() const { return false; }
    virtual void setCpuReading (const CpuReading&) {}

    // A driver whose own clock should keep running even while nothing it
    // feeds is audible (Piano Roll: pressing Play on an unpatched roll still
    // moves its playhead). Its notes start no voices until something listens.
    virtual bool runsWithoutListeners() const { return false; }

    // One sample of the driver's own time. Report at most one note-off and one
    // note-on (-1 for "nothing"); both may land on the same sample.
    virtual void midiDriverAdvance (double sr, int& onNote, int& offNote)
    {
        juce::ignoreUnused (sr);
        onNote = -1;
        offNote = -1;
    }

    // Multi-note form of the above, for drivers that can start several notes
    // on one sample (a Piano Roll chord) or care about velocity. The engine
    // only calls this one; the default wraps midiDriverAdvance, so the Arp and
    // Always Midi keep working untouched. Note-offs should come first.
    virtual int midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents)
    {
        int onNote = -1, offNote = -1;
        midiDriverAdvance (sr, onNote, offNote);
        int n = 0;
        if (offNote >= 0 && n < maxEvents) events[n++] = { offNote, 0.0f, false };
        if (onNote >= 0 && n < maxEvents)  events[n++] = { onNote, 0.85f, true };
        return n;
    }

    //=== MIDI processors =====================================================
    // Every MIDI module is a stream that can be routed anywhere. A PROCESSOR
    // takes a stream in on its Midi In and hands a stream on from its Midi
    // Out: Midi Add (chords), Discard Midi (filters), Arp (re-times), Keyboard
    // Midi (passes the keys). With nothing patched into its Midi In - or no
    // Midi In at all, like Keyboard Midi - a processor hears the played keys.
    // Several streams may feed one processor, and processors chain freely:
    // Piano Roll -> Midi Add -> Discard Midi -> Arp -> Oscillator works.
    virtual bool isMidiProcessor() const { return false; }

    // a processor that takes MPE notes as they are (6.9.10): their bend,
    // pressure and slide on the note-ons, and the expression events between
    // (isExpression). Any other processor gets plain notes and no expression,
    // so a note-off is the only event that is not a note-on.
    virtual bool acceptsMpeEvents() const { return false; }

    // One incoming note event in, the events to pass on out (audio thread).
    // Note-offs must undo exactly what the matching note-on produced, so a
    // processor that changes pitches remembers them. Default: pass through.
    virtual int processMidiEvent (const MidiDriverEvent& in, MidiDriverEvent* out, int maxOut)
    {
        if (maxOut <= 0)
            return 0;
        out[0] = in;
        return 1;
    }

    // all notes off arrived: forget anything held
    virtual void midiAllNotesOff() {}

    // The keys the player is currently holding, for drivers that care (the Arp
    // arpeggiates them; Always Midi ignores them entirely).
    virtual void midiDriverHeldNoteOn (int note)  { juce::ignoreUnused (note); }
    virtual void midiDriverHeldNoteOff (int note) { juce::ignoreUnused (note); }
    virtual void midiDriverAllHeldOff() {}

    //=== raw MIDI ==============================================================
    // A module that wants the MIDI stream itself instead of engine voices - a
    // merged plugin in its GUI version, which runs its own voice allocation.
    // It gets every note its Midi In hears (the played keys when nothing is
    // patched there, else the streams patched into it) plus all non-note host
    // MIDI (bend, wheels, CCs, sustain, pressure), each at its own sample.
    // It never takes part in the engine's voices. Audio thread.
    virtual bool wantsRawMidi() const { return false; }
    virtual void rawMidiMessage (const juce::MidiMessage& message) { juce::ignoreUnused (message); }
    // what the engine calls: MIDI wakes a sleeping module before it arrives
    // (with a patch tuning, a module that cannot retune itself gets its notes
    // bent on member channels - TuningRetuner)
    void deliverRawMidi (const juce::MidiMessage& message)
    {
        // (6.9.12) a member channel's bend, pressure or Slide an MPE module takes elsewhere
        if (const int off = mpeDefaultsOff.load (std::memory_order_relaxed); off != 0 && message.getChannel() >= 2)
        {
            if ((off & kMpeBend) != 0 && message.isPitchWheel()) return;
            if ((off & kMpePressure) != 0 && (message.isChannelPressure() || message.isAftertouch())) return;
            if ((off & kMpeSlide) != 0 && message.isController() && message.getControllerNumber() == 74) return;
        }
        wakeFromSleep();
        if (retuner != nullptr)
            retuner->process (tuning::current(), message, [this] (const juce::MidiMessage& m) { rawMidiMessage (m); });
        else
            rawMidiMessage (message);
    }

    //=== tuning and played-key filters (6.9.10) ================================
    // a Microtuning module in Global mode: its table for the whole patch
    // (audio thread, after blockStart)
    virtual const tuning::Table* globalTuningTable() const { return nullptr; }
    // a Microtuning module (either mode) that plays the keys as Linear steps
    virtual bool usesLinearKeys() const { return false; }
    // Discard MIDI in Global mode: a played key nothing should hear
    virtual bool discardsPlayedKey (int note) const { juce::ignoreUnused (note); return false; }
    // set by the engine once a block: does a Microtuning play the keys as Linear steps?
    void setLinearKeysInPatch (bool b) noexcept { linearKeys.store (b, std::memory_order_relaxed); }
    bool linearKeysInPatch() const noexcept { return linearKeys.load (std::memory_order_relaxed); }
    // MIDI it cannot retune itself (a hosted plugin's own voices): the engine
    // gives it a TuningRetuner (message thread, when the module is made)
    virtual bool retunesByMpeBend() const { return false; }
    std::unique_ptr<TuningRetuner> retuner;

    // The host's play head for this block (nullptr in the standalone app and
    // on Android), and how many samples of the block have been processed so
    // far - for modules that host something needing a position. Audio thread.
    void setHostPlayHead (juce::AudioPlayHead* ph) { hostPlayHead = ph; }

    //=== voice architecture ===================================================
    // The engine evaluates the whole patch once per active voice.
    // Global modules see voice-summed inputs; PerVoice/Flexible modules are
    // called once per voice with that voice's own signals.
    virtual VoiceMode voiceMode() const { return VoiceMode::Global; }

    // A per-voice modulation source that can just as well run on its own (the
    // LFOs). Run per voice, it only exists while voices sound - so on a knob of
    // a module in the global lane (a 303 playing its own notes, a delay, a
    // reverb) it did nothing between notes and was summed over the voices
    // while they played. When nothing it feeds runs per voice, the graph puts
    // such a module in the global lane instead, where it free-runs through
    // processSample. Its processSample must then work without note-ons.
    virtual bool freeRunsWhenGlobal() const { return false; }

    // Global lane. inputs/outputs indexed by descriptor input/output order.
    // Audio sockets use both channels; Modulation sockets use channel 0 only.
    // A module in the global lane that wants to know how many voices are
    // reaching it (Polyphony Sum where synths meet a global effect): the
    // engine counts the voices that sounded at its inputs in the last 20 ms
    // and hands the count over before each processSample.
    virtual bool wantsVoiceCount() const { return false; }
    virtual void setVoiceCount (int count) { juce::ignoreUnused (count); }

    virtual void processSample (const StereoFrame* inputs, StereoFrame* outputs)
    {
        juce::ignoreUnused (inputs);
        zeroOutputs (outputs);
    }

    // Per-voice lane. Same socket conventions; 'voice' is the engine's global
    // voice index (0..kMaxVoices-1).
    virtual void processVoiceSample (int voice, const StereoFrame* inputs, StereoFrame* outputs)
    {
        juce::ignoreUnused (voice, inputs);
        zeroOutputs (outputs);
    }

    // engine-driven voice lifecycle (PerVoice/Flexible modules)
    virtual void voiceNoteOn (int voice, int note, bool retrigger) { juce::ignoreUnused (voice, note, retrigger); }
    virtual void voiceVelocity (int voice, float velocity01) { juce::ignoreUnused (voice, velocity01); }   // called right after voiceNoteOn
    virtual void voiceNoteOff (int voice) { juce::ignoreUnused (voice); }
    virtual void voiceReset (int voice) { juce::ignoreUnused (voice); }

    // A per-voice module that also listens to one of its audio inputs outside
    // its voices - Spectral Resynthesis records its Rec In while no note
    // plays: the engine hands that input over once per sample, summed over
    // the voices as the global lane would hear it, whether or not any of the
    // module's voices sound. -1: none (every other module). Audio thread.
    virtual int globalTapInput() const { return -1; }
    virtual void globalTapSample (const StereoFrame& frame) { juce::ignoreUnused (frame); }

    // How many samples late this module's audio comes out (an FFT's frame).
    // The engine adds them up along every path from Audio In to Audio Out and
    // reports the longest to the host, so a DAW lines the track up; inside the
    // patch, a module with a Dry/Wet keeps its own dry path in step.
    virtual int latencySamples() const { return 0; }

    // how long this module keeps a voice audible after note-off (e.g. an
    // ADSR's release). The engine keeps the voice alive for the patch's max.
    virtual double voiceTailSeconds() const { return 0.0; }

    // A module whose envelope may keep a released voice sounding for as long
    // as it runs (Aquanova's Constant Gate ignores the key-up): while
    // voiceHeldOpen (v) is true the engine doesn't start that voice's tail.
    virtual bool canHoldVoicesOpen() const { return false; }
    virtual bool voiceHeldOpen (int voice) const { juce::ignoreUnused (voice); return false; }

    //=== MIDI (non-note messages; note events are routed via voiceNoteOn/Off) =
    virtual void handleMidiEvent (const juce::MidiMessage&) {}

    //=== MIDI hits in the global lane =========================================
    // The drums (and Droplets) are Flexible: fed by a Clock or by nothing they
    // run in the GLOBAL lane, where the engine never hands out note-ons - so
    // a key or a Piano Roll note could not reach them. A module that returns
    // true here gets the notes arriving on its Midi In as "hits" instead:
    // each lands on one of its spare internal voice slots (slot 0 stays with
    // Trig In / Gate In), so hits overlap and ring out like separate strikes,
    // and Trig In keeps working exactly as before. The module's processSample
    // calls renderGlobalMidiHits() after rendering slot 0.
    virtual bool acceptsGlobalMidiNotes() const { return false; }

    // false: the hit is scaled by note velocity here. A module that already
    // applies voiceVelocity itself (Droplets) returns true to avoid doubling.
    virtual bool appliesOwnVelocity() const { return false; }

    void globalMidiNoteOn (int engineVoice, int note, float velocity01)
    {
        if (engineVoice < 0 || engineVoice >= kMaxVoices)
            return;

        int slot = hits.slotOfVoice[engineVoice];
        if (slot <= 0)
        {
            // round-robin over the spare slots, preferring one that is idle
            slot = -1;
            for (int k = 0; k < kMaxVoices - 1 && slot < 0; ++k)
            {
                const int s = 1 + (hits.next + k) % (kMaxVoices - 1);
                if (! hits.active[s])
                    slot = s;
            }
            if (slot < 0)
                slot = 1 + hits.next % (kMaxVoices - 1);
            hits.next = slot % (kMaxVoices - 1);

            for (auto& s : hits.slotOfVoice)
                if (s == slot)
                    s = -1;
            hits.slotOfVoice[engineVoice] = slot;
        }

        wakeFromSleep();
        if (! hits.active[slot])
            ++hits.numActive;
        hits.active[slot] = true;
        hits.held[slot] = true;
        hits.quietSamples[slot] = 0;
        hits.velocity[slot] = velocity01;
        voiceNoteOn (slot, note, false);
        voiceVelocity (slot, velocity01);
    }

    void globalMidiNoteOff (int engineVoice)
    {
        if (engineVoice < 0 || engineVoice >= kMaxVoices)
            return;

        const int slot = hits.slotOfVoice[engineVoice];
        if (slot <= 0)
            return;

        hits.slotOfVoice[engineVoice] = -1;
        if (hits.held[slot])
        {
            hits.held[slot] = false;
            hits.tailLeft[slot] = voiceTailSeconds() + 0.05;
            voiceNoteOff (slot);
        }
    }

    void resetGlobalMidiHits()
    {
        for (auto& s : hits.slotOfVoice) s = -1;
        for (int s = 0; s < kMaxVoices; ++s)
        {
            hits.active[s] = hits.held[s] = false;
            hits.tailLeft[s] = 0.0;
        }
        hits.numActive = 0;
    }

    //=== sleep on silence (see canSleepOnSilence) =============================
    bool sleepSwitchedOn() const { return sleepEligible && cpuSleepEnabled().load (std::memory_order_relaxed); }
    bool hasSleepSwitch() const { return sleepEligible; }
    // how long input silence may still come out of this module (s): the knobs'
    // longest delay by default; a module that knows better says so
    virtual double silenceMemorySeconds() const { return sleepKnobMemory; }
    struct SleepState
    {
        int quietIn { 0 }, quietOut { 0 };
        bool asleep { false };
        double paramSum { 0.0 };
        int sampleCounter { 0 };
    };
    SleepState sleepState;                         // audio thread
    std::atomic<bool> sleepingNow { false };       // (for tests and the CPU meter)
private:
    std::atomic<bool> linearKeys { false };
public:
    uint32_t audioInMask { 0 }, audioOutMask { 0 };   // which inputs / outputs carry audio
    void wakeFromSleep() noexcept                  // audio thread
    {
        sleepState.asleep = false;
        sleepState.quietIn = sleepState.quietOut = 0;
        sleepingNow.store (false, std::memory_order_relaxed);
    }
    // the knobs as set, summed: moving one wakes a sleeping module
    double baseParamSum() const
    {
        double s = 0.0;
        if (descriptor != nullptr)
            for (size_t i = 0; i < descriptor->params.size(); ++i)
                s += (double) paramValues[i].load (std::memory_order_relaxed) * (double) (i + 1);
        return s;
    }

    //=== parameters ===========================================================
    float param (int index) const
    {
        const float base = paramValues[(size_t) index].load (std::memory_order_relaxed);
        const float mod = paramMod[(size_t) index];
        if (mod == 0.0f)
            return base;
        const float v = juce::jlimit (paramMin[(size_t) index], paramMax[(size_t) index], base + mod);
        // a list / switch moves to the nearest choice: modulated either way it
        // steps at the half-way points (most modules read it as (int) value)
        return paramStepped[(size_t) index] != 0 ? std::round (v) : v;
    }

    // polyphony limit for sound generators that expose a "voices" knob;
    // index resolved once at init so the audio thread never does a string lookup
    int voiceLimit() const
    {
        return voicesParamIndex >= 0 ? (int) param (voicesParamIndex) : kMaxVoices;
    }

    // glide is a mono-only behaviour: with more than one voice there is no
    // single "previous note" to slide from, so the knob is hidden and ignored
    bool isMonoVoice() const { return voiceLimit() == 1; }

    float glideMillis() const { return glideParamIndex >= 0 ? param (glideParamIndex) : 0.0f; }

    // the module's output trim (makeOutputTrim), 1 where it has none
    float outputTrim() const { return outTrimParamIndex >= 0 ? param (outTrimParamIndex) : 1.0f; }

    //=== parameter modulation (audio thread only) =============================
    void setParamMod (int index, float value) { paramMod[(size_t) index] = value; }
    void addParamMod (int index, float value) { paramMod[(size_t) index] += value; }
    void clearParamMods() { std::fill (paramMod.begin(), paramMod.end(), 0.0f); }

    int paramIndex (const juce::String& id) const
    {
        auto it = paramIndexById.find (id.toStdString());
        return it == paramIndexById.end() ? -1 : it->second;
    }

    virtual void setParameter (const juce::String& id, float value)
    {
        const int i = paramIndex (id);
        if (i >= 0)
            paramValues[(size_t) i].store (value, std::memory_order_relaxed);
        else if (legacyDepthKnob (getDescriptor().typeId, id) != nullptr)
            legacyValues[id] = value;   // a removed depth knob: what a cable into its input gets
    }

    // the last value a removed depth knob was given (see LegacyKnobInput)
    float legacyValue (const juce::String& id, float fallback) const
    {
        const auto it = legacyValues.find (id);
        return it != legacyValues.end() ? it->second : fallback;
    }

    // NOTE: returns the BASE value, deliberately WITHOUT parameter modulation.
    // The UI, patch serialization, cloning and the mutator all read through
    // here, so a modulated knob must never leak its live offset into them.
    // DSP code wants param(int) instead, which includes modulation.
    float getParameter (const juce::String& id) const
    {
        const int i = paramIndex (id);
        return i >= 0 ? paramValues[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
    }

    // index-based setter: no string lookup, so the audio thread may use it
    // (Piano Roll's "Once" mode switching itself off at the end)
    void setParameterByIndex (int index, float value)
    {
        if (descriptor != nullptr && index >= 0 && index < (int) descriptor->params.size())
            paramValues[(size_t) index].store (value, std::memory_order_relaxed);
    }

    float getParameterBase (int index) const
    {
        return paramValues[(size_t) index].load (std::memory_order_relaxed);
    }

    //=== connection info (set by the processor when topology changes) ========
    //=== (6.9.12) the default MPE map ==========================================
    // what of a note's MPE the module leaves to an MPE module (Ignore Default
    // MPE Map on, its cable reaching this module): bend, pressure, Slide.
    // Raw MIDI then comes without the member channels' messages for it.
    // Graph build (message thread); read on the audio thread.
    static constexpr int kMpeBend = 1, kMpePressure = 2, kMpeSlide = 4;
    // (the MPE module: which of its outputs took a default over, for its title)
    virtual void setMpeTakenOver (int bits) { juce::ignoreUnused (bits); }

    //=== (6.9.14) section cards: a plugin's patch version as one engine and smaller cards ===
    // A section card (VCOs, Filters, Envelopes ...) holds some of the engine's knobs and
    // shares their values through a CardLink; cards chain Link Out -> Link In and the last
    // one goes into the engine's Cards In. The graph hands the engine every card it finds
    // up that chain (message thread); the cards write, the engine reads (audio thread).
    struct CardLink
    {
        explicit CardLink (std::vector<juce::String> paramIds)
            : ids (std::move (paramIds)), base (new std::atomic<float>[ids.size() + 1]), mod (new std::atomic<float>[ids.size() + 1])
        {
            for (size_t i = 0; i <= ids.size(); ++i) { base[i].store (0.0f); mod[i].store (0.0f); }
        }
        const std::vector<juce::String> ids;
        std::unique_ptr<std::atomic<float>[]> base, mod;   // the knob as set, and with its cables
        std::atomic<bool> live { false };                  // written at least once
    };
    virtual std::shared_ptr<CardLink> cardLink() const { return {}; }
    virtual bool takesCardLinks() const { return false; }
    virtual void setCardLinks (std::vector<std::shared_ptr<CardLink>> links) { juce::ignoreUnused (links); }
    void setMpeDefaultsOff (int bits) noexcept { mpeDefaultsOff.store (bits, std::memory_order_relaxed); }
    int mpeDefaultsOffBits() const noexcept { return mpeDefaultsOff.load (std::memory_order_relaxed); }
    // which knobs (the first 64) have a cable onto them (graph build)
    void setCabledParams (juce::uint64 bits) noexcept { cabledParams.store (bits, std::memory_order_relaxed); }
    bool isParamCabled (int index) const noexcept
    {
        return index >= 0 && index < 64 && ((cabledParams.load (std::memory_order_relaxed) >> index) & 1u) != 0;
    }

    void setInputConnected (int inputIndex, bool connected)
    {
        if (inputIndex >= 0 && inputIndex < (int) inputConnectedFlags.size())
            inputConnectedFlags[(size_t) inputIndex].store (connected, std::memory_order_relaxed);
    }

    bool isInputConnected (int inputIndex) const
    {
        if (inputIndex >= 0 && inputIndex < (int) inputConnectedFlags.size())
            return inputConnectedFlags[(size_t) inputIndex].load (std::memory_order_relaxed);
        return false;
    }

    //=== host audio IO hooks (Audio In / Audio Out modules only) ==============
    virtual bool wantsHostInput() const { return false; }
    virtual void setHostInput (float, float) {}
    virtual bool providesHostOutput() const { return false; }
    virtual void getHostOutput (float& l, float& r) { l = 0.0f; r = 0.0f; }
    // whether a raw-MIDI module with its Midi In empty hears the played keys.
    // Sound sources do (a synth plugin plays from the keyboard); effects and
    // filters take notes only from what is patched into their Midi In (the
    // keys would retune Resonate, move FullFilter's keytracking...)
    virtual bool playedKeysReachRawMidi (const ModuleDescriptor& d) const
    {
        return d.section != ModuleSection::Effect && d.section != ModuleSection::Filter;
    }

    // how much the patch's summed output is soft-clipped (Audio Out's Soft Clip, 0..1)
    virtual float hostSoftClip() const { return 0.0f; }

    //=== loaded sample support (Oscillator "Sample" mode / Sampler) ===========
    virtual bool usesLoadedSample() const { return false; }

    void setLoadedSample (std::shared_ptr<const juce::AudioBuffer<float>> buffer, double sourceRate)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sampleLock);
            loadedSample = std::move (buffer);
            loadedSampleRate = sourceRate;
        }
        sampleChangeCounter.fetch_add (1, std::memory_order_release);
    }

    // a module keeping its own sounds (not the loaded sample) says they changed:
    // the card redraws, a sleeping module wakes
    void markSampleChanged() { sampleChangeCounter.fetch_add (1, std::memory_order_release); }

    std::shared_ptr<const juce::AudioBuffer<float>> getLoadedSample (double* sourceRate = nullptr) const
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        if (sourceRate != nullptr)
            *sourceRate = loadedSampleRate;
        return loadedSample;
    }

    //=== audio stored with the patch ==========================================
    // Whatever a module holds as audio is written into presets / the DAW
    // session through these. By default that is the loaded sample; Record
    // overrides them so its take is saved too.
    virtual bool hasAudioToSave() const { return usesLoadedSample(); }
    virtual std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const
    {
        return getLoadedSample (&rate);
    }
    virtual void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
    {
        setLoadedSample (std::move (audio), rate);
    }

    int getSampleChangeCounter() const { return sampleChangeCounter.load (std::memory_order_acquire); }

    // loads any supported audio file into the shared sample buffer (message thread)
    void loadSampleFromFile (const juce::File& file);

    // reads a picked audio source into the shared sample buffer; takes ownership
    // of the stream (message thread)
    void loadSampleFromStream (std::unique_ptr<juce::InputStream> stream);

   #if JUCE_ANDROID
    // the picker returns a content:// document URI with no filesystem path, so
    // it has to go through AndroidDocument streams (message thread)
    void loadSampleFromURL (const juce::URL& url);
   #endif

    //=== generic UI hooks =====================================================
    // Button params call this from the generic module UI
    virtual void uiButtonClicked (const juce::String& paramId)
    {
        juce::ignoreUnused (paramId);
    }

    // the one non-declarative UI case: Sampler's waveform display
    virtual std::unique_ptr<juce::Component> createExtraContentComponent() { return nullptr; }

    // Width of the module's card in the patch, 0 = the standard width. Only
    // modules with a big custom UI (Piano Roll) want more.
    virtual int preferredModuleWidth() const { return 0; }
    virtual int extraContentHeight() const { return 0; }

    //=== the card's live graph (filters' visualizer) ============================
    // While a card shows its graph, the engine copies the module's first input
    // and first output (mono, all voices summed) into this ring, sample by
    // sample; the card reads the newest stretch of it. The ring is made on the
    // message thread before the tap is switched on, and kept from then on.
    struct Tap
    {
        static constexpr int size = 8192;   // a power of two
        float in[size] {}, out[size] {};
        std::atomic<int> pos { 0 };
    };
    std::atomic<bool> tapOn { false };
    void enableTap (bool on)
    {
        if (on && tap == nullptr)
            tap = std::make_unique<Tap>();
        tapOn.store (on && tap != nullptr, std::memory_order_release);
    }
    void tapWrite (float in, float out) noexcept
    {
        auto& t = *tap;
        const int p = t.pos.load (std::memory_order_relaxed);
        t.in[p] = in;
        t.out[p] = out;
        t.pos.store ((p + 1) & (Tap::size - 1), std::memory_order_release);
    }
    // the newest n samples (oldest first), into in / out
    void tapRead (float* in, float* out, int n) const
    {
        if (tap == nullptr) return;
        const int end = tap->pos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const int p = (end - n + i) & (Tap::size - 1);
            in[i] = tap->in[p];
            out[i] = tap->out[p];
        }
    }
    std::unique_ptr<Tap> tap;
    double tapSampleRate() const { return sampleRate; }

    //=== the static curve (a filter card's Graph pill) =======================
    // The response the knobs set - worked out from the filter's own maths, no
    // signal involved: its gain in dB at each of n frequencies (Hz), from the
    // knobs as set (cable modulation left out). A drive that saturates rather
    // than amplifies is left out too: the curve is the filter's shape at
    // ordinary levels. False: this module has no curve (no Graph pill).
    // Message thread.
    virtual bool magnitudeResponseDb (const float* hz, float* db, int n) const
    {
        juce::ignoreUnused (hz, db, n);
        return false;
    }

    //=== custom state ========================================================
    // For data too big or too structured to live as parameters - e.g. Draw
    // LFO's 1024-point curve, which as hidden params would mean 1024 XML
    // elements per instance. Returning a non-empty string from saveCustomState
    // gets it stored in the patch (and copied on clone) and handed back to
    // loadCustomState. Message thread only.
    // Cards that belong together (Fruit DAW's four parts): modules with the same
    // non-empty group are deleted together and are not cloned. Message thread.
    virtual juce::String linkGroup() const { return {}; }
    virtual juce::String saveCustomState() const { return {}; }
    virtual void loadCustomState (const juce::String& state) { juce::ignoreUnused (state); }

protected:
    // opens an async file chooser and loads the chosen file (used by Oscillator/Sampler)
    void openSampleChooser();

    // The longest audio the sample loader keeps for this module, in seconds
    // (the Sampler takes long files; wavetables and the like need far less).
    virtual double maxSampleSeconds() const { return 240.0; }

    void zeroOutputs (StereoFrame* outputs) const
    {
        const int n = descriptor != nullptr ? juce::jmax (1, descriptor->numOutputs()) : 1;
        for (int i = 0; i < n; ++i)
            outputs[i] = { 0.0f, 0.0f };
    }

    // Adds every sounding MIDI hit (see acceptsGlobalMidiNotes) onto outputs.
    // triggerInput is the Trig In / Gate In socket: the hit slots see it as
    // silent, so the cable keeps driving slot 0 alone.
    void renderGlobalMidiHits (const StereoFrame* inputs, StereoFrame* outputs, int triggerInput)
    {
        if (hits.numActive <= 0 || descriptor == nullptr)
            return;

        constexpr int maxSockets = 8;
        const int numIn = juce::jlimit (1, maxSockets, descriptor->numInputs());
        const int numOut = juce::jlimit (1, maxSockets, descriptor->numOutputs());

        StereoFrame in[maxSockets];
        for (int i = 0; i < numIn; ++i)
            in[i] = inputs[i];
        if (triggerInput >= 0 && triggerInput < numIn)
            in[triggerInput] = { 0.0f, 0.0f };

        for (int slot = 1; slot < kMaxVoices; ++slot)
        {
            if (! hits.active[slot])
                continue;

            StereoFrame out[maxSockets] {};
            processVoiceSample (slot, in, out);

            const float g = appliesOwnVelocity() ? 1.0f : hits.velocity[slot];
            for (int o = 0; o < numOut; ++o)
            {
                outputs[o][0] += out[o][0] * g;
                outputs[o][1] += out[o][1] * g;
            }

            if (! hits.held[slot])
            {
                // A released hit ends once it has actually gone quiet (-80 dB
                // for 50 ms) - never by the clock alone, because several
                // modules ring far longer than voiceTailSeconds() admits
                // (Modal Drum's Decay is a time constant, ~7x longer to fade)
                // and cutting them there is an audible chop. The reported tail
                // is the minimum; 30 s past it is a safety net.
                float peak = 0.0f;
                for (int o = 0; o < numOut; ++o)
                    peak = juce::jmax (peak, std::abs (out[o][0]), std::abs (out[o][1]));
                hits.quietSamples[slot] = peak < 1.0e-4f ? hits.quietSamples[slot] + 1 : 0;

                hits.tailLeft[slot] -= 1.0 / sampleRate;
                const bool quiet = hits.quietSamples[slot] > (int) (0.05 * sampleRate);
                if ((hits.tailLeft[slot] <= 0.0 && quiet) || hits.tailLeft[slot] < -30.0)
                {
                    hits.active[slot] = false;
                    --hits.numActive;
                    voiceReset (slot);
                }
            }
        }
    }

    double sampleRate { 44100.0 };
    double tempoBpm { 120.0 };
    bool laneTempo { false };
    HostTransport hostTransport;
    const MidiControlState* midiControls { nullptr };

    // the played pitch bend in semitones, for pitched generators (6.9.12: a
    // note's own MPE bend left out while an MPE module takes it elsewhere)
    float pitchBendSemitones() const
    {
        if (midiControls == nullptr) return 0.0f;
        if ((mpeDefaultsOff.load (std::memory_order_relaxed) & kMpeBend) != 0)
            return midiControls->bend * midiControls->bendRangeSemis;
        return midiControls->bendSemitones();
    }
    // (6.9.12) a voice's own MPE pressure, 0 while an MPE module takes it elsewhere
    float mpeVoicePressure (int v) const
    {
        if (midiControls == nullptr || v < 0 || v >= kMaxVoices || (mpeDefaultsOff.load (std::memory_order_relaxed) & kMpePressure) != 0)
            return 0.0f;
        return midiControls->voicePressure[v];
    }
    // (6.9.12) the default MPE map: a voice's Slide moves a wavetable position,
    // the middle of the key the knob's own place, up or down a quarter of the
    // range - while the knob has no cable, the note has sent Slide and no MPE
    // module takes Slide elsewhere. In 0..1 of the knob's range.
    float mpeSlideOffset (int v, int paramIndex) const
    {
        if (midiControls == nullptr || v < 0 || v >= kMaxVoices || ! midiControls->voiceSlideSet[v]
            || (mpeDefaultsOff.load (std::memory_order_relaxed) & kMpeSlide) != 0 || isParamCabled (paramIndex))
            return 0.0f;
        return (midiControls->voiceSlide[v] - 0.5f) * 2.0f * midiControls->slideWavetableDepth;
    }
    bool mpeSlideDefaultOff() const noexcept { return (mpeDefaultsOff.load (std::memory_order_relaxed) & kMpeSlide) != 0; }
    const float* hostModValues { nullptr };
    juce::AudioPlayHead* hostPlayHead { nullptr };

private:
    const ModuleDescriptor* descriptor { nullptr };
    std::unique_ptr<std::atomic<float>[]> paramValues;
    std::vector<float> paramMod, paramMin, paramMax;   // paramMod: audio thread only
    std::vector<uint8_t> paramStepped;                  // a list / switch (param() rounds it)
    std::map<juce::String, float> legacyValues;          // removed depth knobs (message thread)
    int voicesParamIndex { -1 };
    int outTrimParamIndex { -1 };
    bool sleepEligible { false };
    double sleepKnobMemory { 0.0 };
    int glideParamIndex { -1 };
    std::unordered_map<std::string, int> paramIndexById;
    std::array<std::atomic<bool>, 8> inputConnectedFlags;
    std::atomic<int> mpeDefaultsOff { 0 };
    std::atomic<juce::uint64> cabledParams { 0 };

    mutable juce::SpinLock sampleLock;
    std::shared_ptr<const juce::AudioBuffer<float>> loadedSample;
    double loadedSampleRate { 44100.0 };
    std::atomic<int> sampleChangeCounter { 0 };

    std::unique_ptr<juce::FileChooser> activeChooser;

    // global-lane MIDI hits (audio thread only); see acceptsGlobalMidiNotes
    struct MidiHits
    {
        MidiHits() { for (auto& s : slotOfVoice) s = -1; }
        int slotOfVoice[kMaxVoices];        // engine voice -> internal slot, -1 = none
        bool active[kMaxVoices] {};
        bool held[kMaxVoices] {};
        float velocity[kMaxVoices] {};
        double tailLeft[kMaxVoices] {};
        int quietSamples[kMaxVoices] {};
        int numActive { 0 };
        int next { 0 };
    } hits;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SynthModule)
    JUCE_DECLARE_NON_COPYABLE (SynthModule)
};

//==============================================================================
// shutdownSafe<T>() - one T for the whole app, like a function's static, but
// deleted when JUCE shuts down (the last plugin instance closing) instead of
// when the plugin's DLL is unloaded. A static that holds images, typefaces or
// other GUI resources must not outlive JUCE: on Windows its Direct2D bitmap
// would be released after Direct2D itself is gone, and a DAW closing would
// crash. If JUCE starts again (a new instance), a new T is made.
//==============================================================================
template <typename T>
T& shutdownSafe()
{
    struct Holder final : public juce::DeletedAtShutdown
    {
        T object;
        ~Holder() override { slot() = nullptr; }
        static Holder*& slot() { static Holder* h = nullptr; return h; }
    };
    JUCE_ASSERT_MESSAGE_THREAD
    auto*& h = Holder::slot();
    if (h == nullptr)
        h = new Holder();
    return h->object;
}

//==============================================================================
// ModuleFactory - Meyer's singleton, self-registration target
//==============================================================================
struct RegisteredModule
{
    ModuleDescriptor descriptor;
    std::function<std::unique_ptr<SynthModule>()> create;
    // Filled in once, right before the first instance is made (a module whose
    // knobs are a merged plugin's own parameters, read from a live instance -
    // which only exists once the app runs). Empty for nearly every module.
    std::function<void (ModuleDescriptor&)> complete;
    bool completed { false };
};

class ModuleFactory
{
public:
    static ModuleFactory& instance();   // Meyer's singleton (defined in ModuleCore.cpp)

    bool registerModule (ModuleDescriptor descriptor, std::function<std::unique_ptr<SynthModule>()> create,
                         std::function<void (ModuleDescriptor&)> complete = {})
    {
        registered.push_back ({ std::move (descriptor), std::move (create), std::move (complete), false });
        return true;
    }

    const std::vector<RegisteredModule>& all() const { return registered; }

    const RegisteredModule* find (const juce::String& typeId) const
    {
        for (auto& r : registered)
            if (r.descriptor.typeId == typeId)
                return &r;
        return nullptr;
    }

    std::unique_ptr<SynthModule> createInstance (const juce::String& typeId) const
    {
        if (auto* r = find (typeId))
        {
            if (r->complete != nullptr && ! r->completed)
            {
                static std::mutex completing;
                const std::lock_guard<std::mutex> lk (completing);
                auto& rr = const_cast<RegisteredModule&> (*r);
                if (! rr.completed) { rr.complete (rr.descriptor); rr.completed = true; }
            }
            auto m = r->create();
            m->initialiseFromDescriptor (r->descriptor);
            return m;
        }
        return nullptr;
    }

private:
    ModuleFactory() = default;
    std::vector<RegisteredModule> registered;   // vector, NOT map - descriptor sidebarOrder rules ordering
};

} // namespace aquanode

//==============================================================================
// Self-registration macro - place at file scope in the module's .cpp
//==============================================================================
// The registration variable is only "used" for its side effect (inserting the
// module into the factory during static init). Nothing else references it, so
// a linker is free to discard it - and MSVC in particular does, which makes
// every self-registered module silently vanish. We therefore give the symbol
// external "C" linkage (so its name is predictable) and force the linker to
// keep it: /include: on MSVC, __attribute__((used)) on GCC/Clang.
#if defined(_MSC_VER)
  #if defined(_WIN64)
    // (two steps, so a symbol renamed by a macro - Aqua Vibrio's registrations
    // inside Aquanode Modular - is kept under its real name)
    #define AQUANODE_KEEP_SYMBOL(sym) AQUANODE_KEEP_SYMBOL_NAMED (sym)
    #define AQUANODE_KEEP_SYMBOL_NAMED(sym) __pragma (comment (linker, "/include:" #sym))
  #else
    #define AQUANODE_KEEP_SYMBOL(sym) AQUANODE_KEEP_SYMBOL_NAMED (sym)
    #define AQUANODE_KEEP_SYMBOL_NAMED(sym) __pragma (comment (linker, "/include:_" #sym))
  #endif
  #define AQUANODE_USED_ATTR
#else
  #define AQUANODE_KEEP_SYMBOL(sym)
  #define AQUANODE_USED_ATTR __attribute__ ((used))
#endif

#define AQUANODE_REGISTER_MODULE(ClassName, descriptorFn) \
    extern "C" AQUANODE_USED_ATTR bool ClassName##_aqRegistered = \
        aquanode::ModuleFactory::instance().registerModule ( \
            descriptorFn(), [] { return std::make_unique<ClassName>(); }); \
    AQUANODE_KEEP_SYMBOL (ClassName##_aqRegistered)
