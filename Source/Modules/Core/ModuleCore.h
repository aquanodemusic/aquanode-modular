#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
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
    bool showGrid { true };   // the "+" markers on the patch area
    // the Skeuomorphic look: the module cards shaded like hardware panels, their
    // knobs as glossy caps, their readouts as small green screens (the colours
    // stay the colourway's). Not touched by Reset All.
    bool skeuomorphic { false };

    ThemeColours() { resetAll(); }
    void resetAll() { for (int i = 0; i < NumSlots; ++i) colour[i] = defaultColour (i); showGrid = true; }
    juce::Colour get (int slot) const { return colour[slot]; }

    // text on a background: the Dark Text colour on light ones, the Light Text
    // colour on dark ones (above `threshold` perceived brightness counts as light)
    juce::Colour textOn (juce::Colour background, float threshold = 0.55f) const
    {
        return background.getPerceivedBrightness() > threshold ? colour[DarkText] : colour[LightText];
    }
    // module cards keep dark text on every default card colour; only a card
    // given a really dark colour of its own switches to the light text
    juce::Colour textOnCard (juce::Colour card) const { return textOn (card, 0.33f); }
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
    return themeColours().get (ThemeColours::Sidebar) != ThemeColours::defaultColour (ThemeColours::Sidebar);
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
    auto p = makeRotary ("outTrim", "Output Trim", 0.0f, 1.0f, juce::Decibels::decibelsToGain (decibels));
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
};

// The most events a driver may report in one sample (a Piano Roll chord
// ending as the next one starts, plus previews, fits comfortably)
static constexpr int kMaxDriverEventsPerSample = 160;

// What the host said about its transport this block. In the standalone app,
// on Android and in hosts that report nothing, hasBpm stays false - anything
// offering "lock to host tempo" must fall back to its own tempo then.
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
// SynthModule - per-instance DSP object base class
//==============================================================================
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
        paramMod.assign (n, 0.0f);
        paramMin.resize (n);
        paramMax.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            paramMin[i] = d.params[i].minValue;
            paramMax[i] = d.params[i].maxValue;
        }
        for (auto& f : inputConnectedFlags) f.store (false, std::memory_order_relaxed);
    }

    const ModuleDescriptor& getDescriptor() const { return *descriptor; }

    //=== lifecycle ============================================================
    virtual void prepare (double newSampleRate) { sampleRate = newSampleRate; }
    virtual void reset() {}                            // wipe transient state (voices/filters/delays), no reallocation
    virtual void blockStart() {}                       // once per processBlock (latch buffers etc.)
    virtual void setTempo (double bpm) { tempoBpm = bpm; }
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

    // The host's play head for this block (nullptr in the standalone app and
    // on Android), and how many samples of the block have been processed so
    // far - for modules that host something needing a position. Audio thread.
    void setHostPlayHead (juce::AudioPlayHead* ph) { hostPlayHead = ph; }

    //=== voice architecture ===================================================
    // The engine evaluates the whole patch once per active voice.
    // Global modules see voice-summed inputs; PerVoice/Flexible modules are
    // called once per voice with that voice's own signals.
    virtual VoiceMode voiceMode() const { return VoiceMode::Global; }

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

    //=== parameters ===========================================================
    float param (int index) const
    {
        const float base = paramValues[(size_t) index].load (std::memory_order_relaxed);
        const float mod = paramMod[(size_t) index];
        return mod == 0.0f ? base
                           : juce::jlimit (paramMin[(size_t) index], paramMax[(size_t) index], base + mod);
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

    //=== custom state ========================================================
    // For data too big or too structured to live as parameters - e.g. Draw
    // LFO's 1024-point curve, which as hidden params would mean 1024 XML
    // elements per instance. Returning a non-empty string from saveCustomState
    // gets it stored in the patch (and copied on clone) and handed back to
    // loadCustomState. Message thread only.
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
    HostTransport hostTransport;
    const MidiControlState* midiControls { nullptr };

    // the played pitch bend in semitones, for pitched generators
    float pitchBendSemitones() const { return midiControls != nullptr ? midiControls->bendSemitones() : 0.0f; }
    const float* hostModValues { nullptr };
    juce::AudioPlayHead* hostPlayHead { nullptr };

private:
    const ModuleDescriptor* descriptor { nullptr };
    std::unique_ptr<std::atomic<float>[]> paramValues;
    std::vector<float> paramMod, paramMin, paramMax;   // paramMod: audio thread only
    std::map<juce::String, float> legacyValues;          // removed depth knobs (message thread)
    int voicesParamIndex { -1 };
    int outTrimParamIndex { -1 };
    int glideParamIndex { -1 };
    std::unordered_map<std::string, int> paramIndexById;
    std::array<std::atomic<bool>, 8> inputConnectedFlags;

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
// ModuleFactory - Meyer's singleton, self-registration target
//==============================================================================
struct RegisteredModule
{
    ModuleDescriptor descriptor;
    std::function<std::unique_ptr<SynthModule>()> create;
};

class ModuleFactory
{
public:
    static ModuleFactory& instance();   // Meyer's singleton (defined in ModuleCore.cpp)

    bool registerModule (ModuleDescriptor descriptor, std::function<std::unique_ptr<SynthModule>()> create)
    {
        registered.push_back ({ std::move (descriptor), std::move (create) });
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
