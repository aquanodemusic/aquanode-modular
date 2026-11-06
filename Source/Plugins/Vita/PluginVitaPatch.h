#pragma once
//==============================================================================
// Vita - Patch version
//
// Vita's voice taken apart along Vital's own signal flow, every part a module
// that runs Vital's own engine code (the same classes the GUI card runs):
//
//   Voice --pitch--> Osc 1..3, Sampler --(Vital's destinations)--> Filter 1 / 2
//     |                                                                |
//     +--mod outs (velocity, note, wheels, ...)    Env 1 ---> Amp <----+
//   Env, LFO, Random, Macros --mod outs--> any knob (Vita - Mod for the
//   bipolar / power / remap settings of a modulation)
//   Amp --> effects in Vita's chain order --> Output;  Amp Direct Out --> Output
//
// Each per-voice module keeps one Vital module (OscillatorModule, FilterModule,
// EnvelopeModule, LfoModule, RandomLfoModule, SampleModule) per Aquanode voice
// and runs it in blocks of 32 samples. A module's knobs are Vital's controls in
// Vital's own units (shown the way Vita shows them), and cables into knobs go
// into Vital's modulation inputs - so a cable of depth d is a Vital modulation
// of amount d. The effects and the output stage are one Vital module each.
//
// Where the patch differs from the GUI card:
//  - no 2x oversampling;
//  - modulation reaches a knob once per 32-sample block (ramped across it, as
//    Vital does for its control-rate modulation), and an audio input (a filter's,
//    an FM source's) arrives one block late;
//  - stereo modulation is heard as the mean of its two sides;
//  - a source that modulates an effect is a "Mono" copy of it that follows the
//    newest note, the way Vital lets the newest voice drive its effects;
//  - mono legato does not keep envelopes running (each note is its own voice),
//    and tuning files are not used.
//
// This file is compiled once, at the end of PluginVita.cpp, so that Vital's
// parameter tables exist before the module descriptors read them.
//==============================================================================
#include "../Host/PatchKit.h"

using aquanode::ModuleDescriptor;
using aquanode::StereoFrame;
using aquanode::VoiceMode;

namespace aqp_vita::vital
{
    // A source of values written from outside Vital's graph (a note, a trigger,
    // a block of audio, the modulation of a knob). Vital's processors want an
    // owner on every Output they read.
    class PatchFeed : public Processor
    {
    public:
        explicit PatchFeed (bool controlRate) : Processor (0, 1, controlRate) {}
        Processor* clone() const override { return new PatchFeed (*this); }
        void process (int) override {}
    };

    // Vita's output stage: stereo routing, volume and the final clamp
    // (SoundEngine without its voices and effects)
    class PatchOutputCore : public SynthModule
    {
    public:
        PatchOutputCore() : SynthModule (1, 1) {}
        Processor* clone() const override { return nullptr; }

        void init() override
        {
            Output* routing = createMonoModControl ("stereo_routing");
            Value* mode = createBaseControl ("stereo_mode");
            Output* volume = createMonoModControl ("volume");

            auto* decoder = new StereoEncoder (true);
            decoder->useInput (input (0), StereoEncoder::kAudio);
            decoder->plug (routing, StereoEncoder::kEncodingValue);
            decoder->plug (mode, StereoEncoder::kMode);
            addProcessor (decoder);

            auto* scaled = new SmoothVolume();
            scaled->plug (decoder, SmoothVolume::kAudioRate);
            scaled->plug (volume, SmoothVolume::kDb);
            addProcessor (scaled);

            auto* clamp = new Clamp (-2.1f, 2.1f);
            clamp->plug (scaled);
            addProcessor (clamp);
            clamp->useOutput (output());
            SynthModule::init();
        }
    };

    // The effects chain's filter (ReorderableEffectChain's FilterFxModule)
    class PatchFilterFxCore : public SynthModule
    {
    public:
        PatchFilterFxCore() : SynthModule (2, 1)
        {
            filter = new FilterModule ("filter_fx");
            filter->setCreateOnValue (false);
            filter->setMono (true);
            addSubmodule (filter);
            addProcessor (filter);
            filter->useOutput (output());
            filter->useInput (input (0), FilterModule::kAudio);
            filter->useInput (input (1), FilterModule::kKeytrack);
        }
        Processor* clone() const override { return nullptr; }

    private:
        FilterModule* filter;
    };
}

namespace vitapatch
{
    using namespace aquanode;
    using namespace aquanode::plugins;
    namespace vt = aqp_vita::vital;
    using Json = nlohmann::json;

    const juce::String kPlugin { "vita" };
    constexpr int kBlock = 32;

    //==========================================================================
    // Descriptors made from Vital's parameter tables
    //==========================================================================
    // the Vital control an Aquanode knob is, and how its value goes over
    struct KnobDef
    {
        std::string control;     // empty: not a Vital control (the module handles it)
        float offset { 0.0f };   // raw = knob + offset (lists whose names start above 0)
        bool round { false };    // a list / a count
    };

    struct Spec
    {
        ModuleDescriptor d;
        std::vector<KnobDef> knobs;   // one per d.params entry
    };

    inline juce::String shortLabel (const std::string& suffix, const vt::ValueDetails& det)
    {
        static const std::map<std::string, const char*> labels {
            { "wave_frame", "Frame" }, { "level", "Level" }, { "pan", "Pan" }, { "transpose", "Transpose" },
            { "tune", "Tune" }, { "unison_voices", "Unison" }, { "unison_detune", "Detune" },
            { "unison_blend", "Blend" }, { "detune_power", "Det Power" }, { "detune_range", "Det Range" },
            { "stereo_spread", "Stereo" }, { "frame_spread", "Frm Spread" }, { "phase", "Phase" },
            { "random_phase", "Rnd Phase" }, { "distortion_phase", "Warp Ph" }, { "distortion_type", "Warp" },
            { "distortion_amount", "Warp Amt" }, { "distortion_spread", "Warp Sprd" },
            { "spectral_morph_type", "Morph" }, { "spectral_morph_amount", "Morph Amt" },
            { "spectral_morph_spread", "Mrph Sprd" }, { "spectral_morph_phase", "Morph Ph" },
            { "stack_style", "Stack" }, { "midi_track", "Key Track" }, { "smooth_interpolation", "Smooth" },
            { "spectral_unison", "Spec Uni" }, { "transpose_quantize", "Quantize" },
            { "loop", "Loop" }, { "bounce", "Bounce" }, { "keytrack", "Key Track" },
            { "cutoff", "Cutoff" }, { "resonance", "Reso" }, { "drive", "Drive" }, { "blend", "Blend" },
            { "blend_transpose", "Comb Tr" }, { "mix", "Mix" }, { "formant_x", "Formant X" },
            { "formant_y", "Formant Y" }, { "formant_transpose", "Fmt Trans" },
            { "formant_resonance", "Fmt Reso" }, { "formant_spread", "Fmt Spread" },
            { "delay", "Delay" }, { "attack", "Attack" }, { "hold", "Hold" }, { "decay", "Decay" },
            { "sustain", "Sustain" }, { "release", "Release" }, { "attack_power", "Att Curve" },
            { "decay_power", "Dec Curve" }, { "release_power", "Rel Curve" },
            { "frequency", "Rate" }, { "sync", "Sync" }, { "tempo", "Tempo" }, { "sync_type", "Mode" },
            { "fade_time", "Fade In" }, { "delay_time", "Delay" }, { "stereo", "Stereo" },
            { "smooth_mode", "Smooth" }, { "smooth_time", "Smooth T" }, { "keytrack_transpose", "Key Trans" },
            { "keytrack_tune", "Key Tune" }, { "style", "Style" },
            { "portamento_time", "Glide" }, { "portamento_slope", "Gld Slope" },
            { "portamento_force", "Always" }, { "portamento_scale", "Gld Scale" },
            { "pitch_bend_range", "Bend" }, { "voice_tune", "Tune" }, { "voice_transpose", "Transpose" },
            { "velocity_track", "Vel Track" }, { "voice_amplitude", "Level" },
            { "dry_wet", "Mix" }, { "feedback", "Feedback" }, { "voices", "Voices" }, { "mod_depth", "Depth" },
            { "delay_1", "Delay 1" }, { "delay_2", "Delay 2" }, { "spread", "Spread" },
            { "enabled_bands", "Bands" }, { "low_gain", "Lo Gain" }, { "band_gain", "Mid Gain" },
            { "high_gain", "Hi Gain" }, { "aux_frequency", "Time 2" }, { "filter_cutoff", "Filter" },
            { "filter_spread", "Spread" }, { "aux_sync", "Sync 2" }, { "aux_tempo", "Tempo 2" },
            { "type", "Type" }, { "filter_order", "Filter" }, { "filter_resonance", "Reso" },
            { "filter_blend", "Blend" }, { "low_mode", "Low" }, { "low_cutoff", "Lo Freq" },
            { "low_resonance", "Lo Reso" }, { "band_mode", "Mid" }, { "band_cutoff", "Mid Freq" },
            { "band_resonance", "Mid Reso" }, { "high_mode", "High" }, { "high_cutoff", "Hi Freq" },
            { "high_resonance", "Hi Reso" }, { "center", "Center" }, { "phase_offset", "Offset" },
            { "pre_low_cutoff", "Lo Cut" }, { "pre_high_cutoff", "Hi Cut" }, { "low_shelf_cutoff", "Lo Shelf" },
            { "low_shelf_gain", "Lo Gain" }, { "high_shelf_cutoff", "Hi Shelf" },
            { "high_shelf_gain", "Hi Gain" }, { "decay_time", "Decay" }, { "size", "Size" },
            { "chorus_amount", "Chorus" }, { "chorus_frequency", "Chr Rate" },
            { "volume", "Volume" }, { "stereo_routing", "Stereo" }, { "stereo_mode", "St Mode" },
            { "amount", "Amount" }, { "power", "Power" }, { "bipolar", "Bipolar" } };

        if (auto it = labels.find (suffix); it != labels.end())
            return it->second;

        // the compressor's band thresholds and ratios: "low_upper_threshold" -> "Lo Up Thr"
        juce::StringArray words;
        words.addTokens (juce::String (suffix), "_", "");
        if (words.size() == 3)
        {
            static const std::map<juce::String, juce::String> abbrev { { "low", "Lo" }, { "band", "Mid" },
                { "high", "Hi" }, { "upper", "Up" }, { "lower", "Dn" }, { "threshold", "Thr" }, { "ratio", "Ratio" } };
            juce::StringArray out;
            for (auto& w : words)
                out.add (abbrev.count (w) ? abbrev.at (w) : w);
            return out.joinIntoString (" ");
        }
        return juce::String (det.display_name).substring (0, 10);
    }

    // a Vital value the way Vita's knobs show it (SynthSlider::getAdjustedValue)
    inline double adjusted (const vt::ValueDetails& d, double v)
    {
        switch (d.value_scale)
        {
            case vt::ValueDetails::kQuadratic:   v = v * v; break;
            case vt::ValueDetails::kCubic:       v = v * v * v; break;
            case vt::ValueDetails::kQuartic:     v = (v * v) * (v * v); break;
            case vt::ValueDetails::kExponential: v = std::pow (2.0, v); break;
            case vt::ValueDetails::kSquareRoot:  v = std::sqrt (std::max (v, 0.0)); break;
            default: break;
        }
        v += d.post_offset;
        if (d.display_invert)
            v = v != 0.0 ? 1.0 / v : 0.0;
        return v * d.display_multiply;
    }

    inline double unadjusted (const vt::ValueDetails& d, double v)
    {
        v /= d.display_multiply != 0.0f ? d.display_multiply : 1.0f;
        if (d.display_invert)
            v = v != 0.0 ? 1.0 / v : 0.0;
        v -= d.post_offset;
        switch (d.value_scale)
        {
            case vt::ValueDetails::kQuadratic:   return std::sqrt (std::max (v, 0.0));
            case vt::ValueDetails::kCubic:       return std::cbrt (std::max (v, 0.0));
            case vt::ValueDetails::kQuartic:     return std::sqrt (std::sqrt (std::max (v, 0.0)));
            case vt::ValueDetails::kExponential: return v > 0.0 ? std::log2 (v) : d.min;
            case vt::ValueDetails::kSquareRoot:  return v * v;
            default: return v;
        }
    }

    inline juce::String number (double v)
    {
        const double a = std::abs (v);
        return juce::String (v, a >= 100.0 ? 0 : a >= 10.0 ? 1 : 2);
    }

    inline juce::String hzText (double hz)
    {
        if (hz >= 10000.0) return juce::String (hz / 1000.0, 1) + "k";
        if (hz >= 1000.0)  return juce::String (hz / 1000.0, 2) + "k";
        return juce::String (hz, hz < 10.0 ? 2 : (hz < 100.0 ? 1 : 0)) + " Hz";
    }

    inline juce::String unitsOf (const vt::ValueDetails& d)
    {
        auto u = juce::String (d.display_units).trim();
        if (u == "secs") return " s";
        if (u == "semitones") return " st";
        if (u == "cents") return " ct";
        if (u == "%") return "%";
        if (u.isEmpty()) return {};
        return " " + u;
    }

    // a knob whose 8..136 note-number value is a frequency
    inline bool isFrequencyKnob (const std::string& suffix, const vt::ValueDetails& d)
    {
        const juce::String s (suffix);
        return d.value_scale == vt::ValueDetails::kLinear && d.display_units.empty() && d.max >= 128.0f
               && (s.endsWith ("cutoff") || s.endsWith ("center"));
    }

    inline void giveVitalText (ParamSpec& p, const vt::ValueDetails& d, bool frequency)
    {
        if (frequency)
        {
            p.textFromValue = [] (double v) { return hzText (440.0 * std::pow (2.0, (v - 69.0) / 12.0)); };
            p.valueFromText = [] (const juce::String& t)
            {
                const auto s = t.trim().toLowerCase();
                double hz = s.getDoubleValue() * (s.containsChar ('k') ? 1000.0 : 1.0);
                return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : 0.0;
            };
            return;
        }
        const auto units = unitsOf (d);
        p.textFromValue = [d, units] (double v) { return number (adjusted (d, v)) + units; };
        p.valueFromText = [d] (const juce::String& t) { return unadjusted (d, t.trim().getDoubleValue()); };
    }

    // fills the module's knob rows (5 units a row) from Vital's parameters
    struct Layout
    {
        Spec& s;
        int row { 0 }, units { 0 };

        void newRow() { if (units > 0) { ++row; units = 0; } }

        void add (ParamSpec p, KnobDef k)
        {
            const int w = juce::jlimit (1, 5, p.widthUnits);
            if (! p.hidden)
            {
                if (units + w > 5)
                    newRow();
                units += w;
            }
            p.row = row;
            s.d.params.push_back (std::move (p));
            s.knobs.push_back (std::move (k));
        }

        // one of Vital's controls: id is the knob's id, control its name in the
        // module's Vital instance (osc_1_wave_frame for every Osc module)
        void vital (const juce::String& id, const std::string& control, const std::string& suffix,
                    bool hidden = false, const char* label = nullptr)
        {
            if (! vt::Parameters::isParameter (control))
                return;
            const auto& det = vt::Parameters::getDetails (control);
            const auto name = label != nullptr ? juce::String (label) : shortLabel (suffix, det);

            KnobDef k { control, 0.0f, false };
            ParamSpec p;
            if (det.value_scale == vt::ValueDetails::kIndexed && det.string_lookup != nullptr)
            {
                juce::StringArray choices;
                for (int i = (int) det.min; i <= (int) det.max; ++i)
                    choices.add (det.string_lookup[i]);
                const bool offOn = choices.size() == 2 && choices[0] == "Off";
                p = makeCombo (id, name, choices, juce::roundToInt (det.default_value - det.min), 0, offOn ? 1 : 2);
                k.offset = det.min;
                k.round = true;
            }
            else if (det.value_scale == vt::ValueDetails::kIndexed)
            {
                p = makeRotary (id, name, det.min, det.max, det.default_value, 0, "", false, 1.0f);
                k.round = true;
            }
            else
            {
                p = makeRotary (id, name, det.min, det.max, det.default_value, 0);
                giveVitalText (p, det, isFrequencyKnob (suffix, det));
            }
            if (hidden)
                p.hide();
            add (std::move (p), std::move (k));
        }

        void group (const std::string& prefix, std::initializer_list<const char*> suffixes)
        {
            for (const char* sfx : suffixes)
                vital (sfx, prefix + sfx, sfx);
        }

        // a knob that is not one of Vital's controls
        void own (ParamSpec p) { add (std::move (p), {}); }

        // which of Vita's oscillators / filters / envelopes ... a module is, for its title, and
        // how many voices it keeps Vital modules for (both hidden; the builder sets them)
        void number()
        {
            own (makeRotary ("number", "Number", 0.0f, 8.0f, 0.0f, 0, "", false, 1.0f).hide().noMod());
            own (makeRotary ("lanes", "Lanes", 1.0f, (float) kMaxVoices, 8.0f, 0, "", false, 1.0f).hide().noMod());
        }
    };

    // A small drawing on a module card: the oscillator's wave frame, the LFO's shape, a
    // modulation's remap curve. fill gives the points (in lo..hi) and says whether they changed.
    class ShapeView : public juce::Component,
                      private juce::Timer
    {
    public:
        using Fill = std::function<bool (std::vector<float>&)>;

        ShapeView (Fill f, float low, float high, juce::Colour c) : fill (std::move (f)), lo (low), hi (high), colour (c)
        {
            setInterceptsMouseClicks (false, false);
            fill (points);
            startTimerHz (8);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (6.0f, 3.0f);
            g.setColour (juce::Colour (0xff15171c));
            g.fillRoundedRectangle (r, 4.0f);
            if (points.size() < 2)
                return;
            r = r.reduced (4.0f, 4.0f);
            auto y = [&] (float v) { return r.getBottom() - (juce::jlimit (lo, hi, v) - lo) / (hi - lo) * r.getHeight(); };
            const float zero = y (lo < 0.0f ? 0.0f : lo);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int) zero, r.getX(), r.getRight());

            juce::Path line, area;
            const int n = (int) points.size();
            for (int i = 0; i < n; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
                if (i == 0) { line.startNewSubPath (x, y (points[0])); area.startNewSubPath (x, zero); }
                line.lineTo (x, y (points[(size_t) i]));
                area.lineTo (x, y (points[(size_t) i]));
            }
            area.lineTo (r.getRight(), zero);
            area.closeSubPath();
            g.setColour (colour.withAlpha (0.25f));
            g.fillPath (area);
            g.setColour (colour);
            g.strokePath (line, juce::PathStrokeType (1.5f));
        }

    private:
        // repaints only when the shape really changed
        void timerCallback() override
        {
            if (fill (fresh) && fresh != points)
            {
                std::swap (fresh, points);
                repaint();
            }
        }

        std::vector<float> fresh;

        Fill fill;
        std::vector<float> points;
        float lo, hi;
        juce::Colour colour;
    };

    // Vital 1.5's die for Random Amplitudes: a tap rolls a new seed, a right click or a hold
    // opens Copy / Paste / Enter seed; shown while `shown` says so
    class SeedDice : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
    {
    public:
        SeedDice (std::function<int()> get, std::function<void (int)> set, std::function<bool()> isShown)
            : getSeed (std::move (get)), setSeed (std::move (set)), shown (std::move (isShown))
        {
            setTooltip ("Roll new random amplitudes (hold or right click: seed menu)");
            setVisible (shown());
            startTimerHz (4);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (3.0f);
            const float side = juce::jmin (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (side, side);
            g.setColour (juce::Colour (0xff15171c).withAlpha (0.85f));
            g.fillRoundedRectangle (r, side * 0.2f);
            g.setColour (juce::Colours::white.withAlpha (isMouseOverOrDragging() ? 1.0f : 0.75f));
            auto die = r.reduced (side * 0.14f);
            g.drawRoundedRectangle (die, side * 0.12f, juce::jmax (1.2f, side * 0.07f));
            const float pip = side * 0.12f;
            const float a = 0.28f, b = 0.72f;
            const float pips[5][2] = { { a, a }, { b, a }, { 0.5f, 0.5f }, { a, b }, { b, b } };
            for (auto& p : pips)
                g.fillEllipse (die.getX() + die.getWidth() * p[0] - pip * 0.5f,
                               die.getY() + die.getHeight() * p[1] - pip * 0.5f, pip, pip);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                showMenu();
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition()))
                return;
            if (e.getLengthOfMousePress() >= 450)
                showMenu();
            else
                roll();
        }

    private:
        void roll() { setSeed (juce::Random::getSystemRandom().nextInt (0x7fffffff)); }

        static bool parse (juce::String text, int& seed)
        {
            text = text.trim();
            if (text.startsWithIgnoreCase ("random seed:"))
                text = text.fromFirstOccurrenceOf (":", false, false).trim();
            const bool hex = text.startsWithIgnoreCase ("0x");
            if (hex)
                text = text.substring (2);
            if (text.isEmpty())
                return false;
            if (hex || ! text.containsOnly ("0123456789"))
            {
                if (! text.containsOnly ("0123456789abcdefABCDEF") || text.length() > 8)
                    return false;
                seed = (int) (text.getHexValue64() & 0x7fffffff);
                return true;
            }
            seed = (int) (text.getLargeIntValue() & 0x7fffffff);
            return true;
        }

        static juce::String hex (int seed) { return "0x" + juce::String::toHexString (seed).toUpperCase(); }

        void showMenu()
        {
            int pasted = 0;
            const bool canPaste = parse (juce::SystemClipboard::getTextFromClipboard(), pasted);
            juce::PopupMenu menu;
            menu.addSectionHeader ("Random seed: " + hex (getSeed()));
            menu.addItem (1, "Roll New Seed");
            menu.addSeparator();
            menu.addItem (2, "Copy seed");
            menu.addItem (3, "Paste seed", canPaste);
            menu.addItem (4, "Enter seed");
            menu.addItem (5, "Default seed");
            juce::Component::SafePointer<SeedDice> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, pasted] (int r)
            {
                if (safe == nullptr)
                    return;
                if (r == 1) safe->roll();
                else if (r == 2) juce::SystemClipboard::copyTextToClipboard (hex (safe->getSeed()));
                else if (r == 3) safe->setSeed (pasted);
                else if (r == 5) safe->setSeed (aqp_vita::vital::RandomValues::kSeed);
                else if (r == 4)
                {
                    auto* w = new juce::AlertWindow ("Random Seed", "Enter a seed (hex like 0x1A2B, or a number):",
                                                     juce::MessageBoxIconType::NoIcon);
                    w->addTextEditor ("seed", hex (safe->getSeed()));
                    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
                    {
                        int entered = 0;
                        if (result == 1 && safe != nullptr && parse (w->getTextEditorContents ("seed"), entered))
                            safe->setSeed (entered);
                    }), true);
                }
            });
        }

        void timerCallback() override
        {
            const bool want = shown();
            if (want != isVisible())
                setVisible (want);
        }

        std::function<int()> getSeed;
        std::function<void (int)> setSeed;
        std::function<bool()> shown;
    };

    // n points of a buffer of size, into out; true if they differ from what out held
    inline bool resample (const float* buffer, int size, int n, std::vector<float>& out)
    {
        bool changed = (int) out.size() != n;
        out.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float v = buffer[(size_t) juce::jlimit (0, size - 1, (int) ((float) i / (float) (n - 1) * (float) (size - 1)))];
            if (v != out[(size_t) i]) { out[(size_t) i] = v; changed = true; }
        }
        return changed;
    }

    inline ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order,
                                  const juce::String& description)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.vita." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        d.description = description;
        return d;
    }

    //==========================================================================
    // Vital modules driven from Aquanode Modular, one per voice
    //==========================================================================
    struct Lane
    {
        struct Knob
        {
            vt::Value* value { nullptr };          // the control's base value
            vt::Output* mod { nullptr };            // into its modulation sum (nullptr: base only)
            vt::ValueSwitch* switches[2] {};        // turned on once modulation arrives
            bool modOn { false };
            float lastBase { std::numeric_limits<float>::quiet_NaN() };
        };

        std::unique_ptr<vt::SynthModule> module;
        std::vector<Knob> knobs;
        std::vector<std::unique_ptr<vt::PatchFeed>> owned;
        vt::Output *reset { nullptr }, *retrigger { nullptr }, *midi { nullptr }, *keytrack { nullptr },
                   *noteCount { nullptr }, *active { nullptr };
        std::vector<vt::Output*> audio;                         // one per audio input socket
        std::vector<std::array<StereoFrame, kBlock>> fifo;      // audio arriving for the next block

        int pos { 0 };
        juce::uint64 started { 0 };   // when its note began (the oldest lane is taken first)
        vt::Value* extra[2] {};       // a module's own controls (a filter's model and style)
        bool ok { true };            // false: this block could not run (its data was being replaced)
        bool pendingOn { false }, pendingOff { false }, fresh { false }, silent { true };
        float note { 60.0f }, velocity { 1.0f };

        vt::Output* feed (bool controlRate)
        {
            owned.push_back (std::make_unique<vt::PatchFeed> (controlRate));
            return owned.back()->output();
        }
    };

    // The base of every module that runs Vital modules. A derived module
    // makes its Vital module per lane (createVital), wires the standard feeds
    // it needs and reads its outputs; this class runs the blocks, pushes the
    // knobs and the note events in, and buffers the audio inputs.
    class VitalModuleBase : public aquanode::SynthModule
    {
    public:
        enum class Lanes { PerVoice, Mono, Global };

        VitalModuleBase (const Spec& specToUse, Lanes laneMode, std::vector<int> audioSocketsToUse, int pitchSocketToUse)
            : spec (specToUse), mode (laneMode), audioSockets (std::move (audioSocketsToUse)), pitchSocket (pitchSocketToUse)
        {
            bps = std::make_unique<vt::PatchFeed> (true);
            bps->output()->buffer[0] = 2.0f;
            for (auto& v : laneOfVoice) v = -1;
            for (auto& v : voiceOfLane) v = -1;
        }

        VoiceMode voiceMode() const override { return mode == Lanes::PerVoice ? VoiceMode::PerVoice : VoiceMode::Global; }
        bool wantsRawMidi() const override { return mode == Lanes::Mono; }

        void prepare (double sr) override
        {
            aquanode::SynthModule::prepare (sr);
            preparedRate = (int) std::lround (sr);
            // the lanes the "lanes" knob asks for, made on the message thread once the knob is
            // set (a builder sets it right after adding the module, a patch as it loads)
            juce::WeakReference<VitalModuleBase> weak (this);
            juce::MessageManager::callAsync ([weak]
            {
                if (auto* m = weak.get())
                    m->ensureLanes (m->wantedLanes());
            });
            for (int i = 0; i < numLanes(); ++i)
                lane (i).module->setSampleRate (preparedRate);
            reset();
        }

        void reset() override
        {
            for (int i = 0; i < numLanes(); ++i)
            {
                auto& l = lane (i);
                l.pos = 0;
                l.pendingOn = l.pendingOff = false;
                l.silent = true;
                for (auto& f : l.fifo)
                    for (auto& s : f) s = { 0.0f, 0.0f };
            }
            for (auto& v : laneOfVoice) v = -1;
            for (auto& v : voiceOfLane) v = -1;
        }

        // How many Vital modules a per-voice module keeps (the "lanes" knob, hidden: the
        // builder sets it from Vita's polyphony). More voices than that take over the
        // oldest note's lane. Never fewer than are made already.
        void setParameter (const juce::String& id, float value) override
        {
            aquanode::SynthModule::setParameter (id, value);
            if (id == "lanes")
                ensureLanes (wantedLanes());
        }

        // --- per voice -------------------------------------------------------
        void voiceNoteOn (int v, int note, bool retrigger) override
        {
            if (mode != Lanes::PerVoice) return;
            const int li = laneFor (v);
            if (li < 0) return;
            auto& l = lane (li);
            l.note = (float) note;
            l.pendingOn = true;
            l.pendingOff = false;
            l.fresh = ! retrigger || l.silent;
            if (l.fresh)
            {
                l.pos = 0;
                for (auto& f : l.fifo)
                    for (auto& s : f) s = { 0.0f, 0.0f };
            }
            l.silent = false;
            l.started = ++noteCounter;
        }
        void voiceVelocity (int v, float vel) override { if (auto* l = laneOf (v)) l->velocity = vel; }
        void voiceNoteOff (int v) override { if (auto* l = laneOf (v)) l->pendingOff = true; }
        void voiceReset (int v) override
        {
            if (mode != Lanes::PerVoice || v < 0 || v >= kMaxVoices) return;
            if (auto* l = laneOf (v))
            {
                l->pendingOn = l->pendingOff = false;
                l->silent = true;
                voiceOfLane[(size_t) laneOfVoice[(size_t) v]] = -1;
            }
            laneOfVoice[(size_t) v] = -1;
        }

        void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
        {
            if (auto* l = laneOf (v))
                runSample (*l, in, out);
            else
                for (int o = 0; o < spec.d.numOutputs(); ++o)
                    out[o] = { 0.0f, 0.0f };
        }

        // --- mono: the newest note of the keys played -------------------------
        void rawMidiMessage (const juce::MidiMessage& m) override
        {
            if (mode != Lanes::Mono || numLanes() == 0) return;
            auto& l = lane (0);
            if (m.isNoteOn())
            {
                held.removeAllInstancesOf (m.getNoteNumber());
                l.fresh = held.isEmpty();
                held.add (m.getNoteNumber());
                l.note = (float) m.getNoteNumber();
                l.velocity = m.getFloatVelocity();
                l.pendingOn = true;
                l.pendingOff = false;
                l.silent = false;
                ++noteCounter;
            }
            else if (m.isNoteOff())
            {
                held.removeAllInstancesOf (m.getNoteNumber());
                if (held.isEmpty())
                    l.pendingOff = true;
                else
                    l.note = (float) held.getLast();
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                held.clear();
                l.pendingOff = true;
            }
        }
        void midiAllNotesOff() override { held.clear(); if (numLanes() > 0) lane (0).pendingOff = true; }

        juce::String titleSuffix() const override
        {
            const int i = paramIndex ("number");
            const int n = i >= 0 ? (int) getParameterBase (i) : 0;
            return n > 0 ? " " + juce::String (n) : juce::String();
        }

        void setTempo (double bpm) override
        {
            aquanode::SynthModule::setTempo (bpm);
            bps->output()->buffer[0] = (float) (bpm / 60.0);
        }

        void processSample (const StereoFrame* in, StereoFrame* out) override
        {
            if (numLanes() > 0)
                runSample (lane (0), in, out);
        }

    protected:
        // the Vital module for a lane, its controls made (init() called)
        virtual std::unique_ptr<vt::SynthModule> createVital (Lane& lane) = 0;
        // the lane's block, once the knobs and events are in (default: process the module)
        virtual void processBlock (Lane& lane) { lane.module->process (kBlock); }
        // this sample's outputs
        virtual void writeOutputs (Lane& lane, int i, StereoFrame* out) = 0;
        // after the standard feeds, before the block
        virtual void beforeBlock (Lane&, const StereoFrame*) {}

        // in the derived constructor: the first lane (the rest come with prepare / the lanes knob)
        void makeLanes() { ensureLanes (1); }

        int numLanes() const { return laneCount.load (std::memory_order_acquire); }
        Lane& lane (int i) { return *laneStore[(size_t) i]; }

        static vt::poly_float frameToPoly (const StereoFrame& f, bool perVoice)
        {
            return perVoice ? vt::poly_float (f[0], f[1], 0.0f, 0.0f) : vt::poly_float (f[0], f[1], f[0], f[1]);
        }

        static StereoFrame polyToFrame (const vt::poly_float& p) { return { p[0], p[1] }; }

        const Spec& spec;
        const Lanes mode;
        std::unique_ptr<vt::PatchFeed> bps;      // beats per second, for the tempo-synced ones
        juce::SpinLock dataLock;                 // held while a wavetable / shape / sample is replaced
        juce::uint64 noteCounter { 0 };

    private:
        int wantedLanes() const
        {
            if (mode != Lanes::PerVoice)
                return 1;
            const int i = paramIndex ("lanes");
            return juce::jlimit (1, kMaxVoices, i >= 0 ? (int) std::lround (getParameterBase (i)) : kMaxVoices);
        }

        // message thread: appends lanes (the audio thread only ever sees whole ones)
        void ensureLanes (int n)
        {
            n = juce::jlimit (1, mode == Lanes::PerVoice ? kMaxVoices : 1, n);
            for (int i = numLanes(); i < n; ++i)
            {
                auto l = std::make_unique<Lane>();
                l->reset = l->feed (true);
                l->retrigger = l->feed (true);
                l->midi = l->feed (true);
                l->keytrack = l->feed (true);
                l->noteCount = l->feed (true);
                l->active = l->feed (true);
                l->active->buffer[0] = mode == Lanes::PerVoice ? vt::poly_float (1.0f, 1.0f, 0.0f, 0.0f) : vt::poly_float (1.0f);
                for (size_t a = 0; a < audioSockets.size(); ++a)
                    l->audio.push_back (l->feed (false));
                l->fifo.resize (audioSockets.size());
                l->module = createVital (*l);
                linkKnobs (*l);
                if (preparedRate > 0)
                    l->module->setSampleRate (preparedRate);
                laneStore[(size_t) i] = std::move (l);
                laneCount.store (i + 1, std::memory_order_release);
            }
        }

        Lane* laneOf (int v)
        {
            if (mode != Lanes::PerVoice || v < 0 || v >= kMaxVoices) return nullptr;
            const int li = laneOfVoice[(size_t) v];
            return li >= 0 ? &lane (li) : nullptr;
        }

        // the lane voice v plays on: its own, a free one, or the one of the oldest note
        int laneFor (int v)
        {
            if (v < 0 || v >= kMaxVoices) return -1;
            if (laneOfVoice[(size_t) v] >= 0) return laneOfVoice[(size_t) v];
            const int n = numLanes();
            int pick = -1;
            juce::uint64 oldest = ~(juce::uint64) 0;
            for (int i = 0; i < n && pick < 0; ++i)
                if (voiceOfLane[(size_t) i] < 0)
                    pick = i;
            if (pick < 0)
                for (int i = 0; i < n; ++i)
                    if (lane (i).started < oldest) { oldest = lane (i).started; pick = i; }
            if (pick < 0) return -1;
            if (const int was = voiceOfLane[(size_t) pick]; was >= 0)
                laneOfVoice[(size_t) was] = -1;
            voiceOfLane[(size_t) pick] = v;
            laneOfVoice[(size_t) v] = pick;
            lane (pick).silent = true;   // a lane taken over starts afresh
            return pick;
        }

        JUCE_DECLARE_WEAK_REFERENCEABLE (VitalModuleBase)

        std::array<std::unique_ptr<Lane>, kMaxVoices> laneStore;
        std::atomic<int> laneCount { 0 };
        std::array<int, kMaxVoices> laneOfVoice {}, voiceOfLane {};
        int preparedRate { 0 };

        void linkKnobs (Lane& l)
        {
            auto controls = l.module->getControls();
            l.knobs.resize (spec.knobs.size());
            for (size_t k = 0; k < spec.knobs.size(); ++k)
            {
                const auto& def = spec.knobs[k];
                if (def.control.empty() || controls.count (def.control) == 0)
                    continue;
                auto& kn = l.knobs[k];
                kn.value = controls[def.control];

                vt::Processor* poly = l.module->getPolyModulationDestination (def.control);
                vt::Processor* mono = l.module->getMonoModulationDestination (def.control);
                if (poly != nullptr || mono != nullptr)
                {
                    kn.mod = l.feed (true);
                    (poly != nullptr ? poly : mono)->plugNext (kn.mod);
                    kn.switches[0] = l.module->getMonoModulationSwitch (def.control);
                    kn.switches[1] = poly != nullptr ? l.module->getPolyModulationSwitch (def.control) : nullptr;
                }
            }
        }

        void pushKnobs (Lane& l)
        {
            for (size_t k = 0; k < l.knobs.size(); ++k)
            {
                auto& kn = l.knobs[k];
                if (kn.value == nullptr)
                    continue;
                const auto& def = spec.knobs[k];
                const float base = getParameterBase ((int) k);
                const float now = param ((int) k);
                if (kn.mod == nullptr)
                {
                    float raw = now + def.offset;
                    if (def.round) raw = std::round (raw);
                    if (raw != kn.lastBase) { kn.value->set (raw); kn.lastBase = raw; }
                    continue;
                }
                float raw = base + def.offset;
                if (def.round) raw = std::round (raw);
                if (raw != kn.lastBase) { kn.value->set (raw); kn.lastBase = raw; }

                const float m = now - base;
                if (m != 0.0f && ! kn.modOn)
                {
                    for (auto* sw : kn.switches)
                        if (sw != nullptr) sw->set (1.0f);
                    kn.modOn = true;
                }
                if (kn.modOn)
                    kn.mod->buffer[0] = m;
            }
        }

        void startBlock (Lane& l, const StereoFrame* in)
        {
            const juce::SpinLock::ScopedTryLockType lock (dataLock);
            l.ok = lock.isLocked();
            if (! l.ok)
                return;

            const bool perVoice = mode == Lanes::PerVoice;
            for (size_t a = 0; a < l.audio.size(); ++a)
                for (int i = 0; i < kBlock; ++i)
                    l.audio[a]->buffer[i] = frameToPoly (l.fifo[a][(size_t) i], perVoice);

            pushKnobs (l);

            // A fresh voice: one block without the note first. Vital's modules read
            // some of their controls a block late (the scaled values a knob goes
            // through run after them), and a note must not start on stale ones -
            // an envelope would take its decay as 0 there.
            if (l.pendingOn && l.fresh)
            {
                l.midi->buffer[0] = l.note;
                processBlock (l);
            }

            const vt::poly_mask lanesMask = perVoice ? vt::constants::kFirstMask : vt::constants::kFullMask;
            if (l.pendingOn)
            {
                if (l.fresh)
                    l.reset->trigger (lanesMask, vt::kVoiceOn, 0);
                l.retrigger->trigger (lanesMask, vt::kVoiceOn, 0);
                l.pendingOn = false;
                l.fresh = false;
            }
            else if (l.pendingOff)
            {
                l.retrigger->trigger (lanesMask, vt::kVoiceOff, 0);
                l.pendingOff = false;
            }

            float pitch = l.note;
            if (pitchSocket >= 0 && isInputConnected (pitchSocket))
                pitch += in[pitchSocket][0];
            l.midi->buffer[0] = pitch;
            l.keytrack->buffer[0] = pitch - (float) vt::kMidiTrackCenter;
            l.noteCount->buffer[0] = (float) noteCounter;

            beforeBlock (l, in);
            processBlock (l);

            l.reset->clearTrigger();
            l.retrigger->clearTrigger();
            // clearTrigger() zeroes the value a control-rate output carries
            l.midi->buffer[0] = pitch;
        }

        void runSample (Lane& l, const StereoFrame* in, StereoFrame* out)
        {
            if (l.pos == 0)
                startBlock (l, in);

            for (size_t a = 0; a < audioSockets.size(); ++a)
                l.fifo[a][(size_t) l.pos] = in[audioSockets[a]];

            if (l.ok)
                writeOutputs (l, l.pos, out);
            else
                for (int o = 0; o < spec.d.numOutputs(); ++o)
                    out[o] = { 0.0f, 0.0f };

            if (++l.pos >= kBlock)
                l.pos = 0;
        }

        const std::vector<int> audioSockets;
        const int pitchSocket;
        juce::Array<int> held;
    };
}

//==============================================================================
// The modules
//==============================================================================
namespace vitapatch
{
    inline void renameChoices (Spec& s, const juce::String& id, std::initializer_list<std::pair<int, const char*>> names)
    {
        for (auto& p : s.d.params)
            if (p.id == id)
                for (auto& [i, n] : names)
                    if (i < p.choices.size())
                        p.choices.set (i, n);
    }

    inline int paramIndexIn (const Spec& s, const juce::String& id)
    {
        for (size_t i = 0; i < s.d.params.size(); ++i)
            if (s.d.params[i].id == id)
                return (int) i;
        return -1;
    }

    inline void setControl (vt::SynthModule& m, const std::string& name, float value)
    {
        auto controls = m.getControls();
        if (controls.count (name))
            controls[name]->set (value);
    }

    // a mod out of a Vital module: audio-rate outputs sample by sample,
    // control-rate ones hold their block's value
    inline StereoFrame modValue (const vt::Output* o, int i)
    {
        const auto& p = o->isControlRate() || o->owner == nullptr || o->owner->isControlRate() ? o->buffer[0] : o->buffer[i];
        return { p[0], p[1] };
    }

    //==========================================================================
    // Osc
    //==========================================================================
    inline const Spec& oscSpec()
    {
        static const Spec s = []
        {
            Spec s;
            s.d = base ("osc", "Osc", ModuleSection::Oscillator, 0,
                        "One of Vita's wavetable oscillators (Vital's own): wave frame, unison, the warps (FM / RM "
                        "from Mod A / B or the Sampler) and the spectral morphs. Pitch In adds semitones to the note "
                        "(Vita - Voice's Pitch Out); Raw Out feeds the other oscillators' Mod A / B In. The "
                        "wavetable comes with the module from the preset.");
            s.d.sockets = { modIn ("pitchIn", "Pitch In"), audioIn ("modAIn", "Mod A In"), audioIn ("modBIn", "Mod B In"),
                            audioIn ("sampleIn", "Sample In"), audioOut ("audioOut", "Audio Out"), audioOut ("rawOut", "Raw Out") };
            Layout L { s };
            const std::string p = "osc_1_";
            L.group (p, { "wave_frame", "level", "pan", "transpose", "tune" });                                   L.newRow();
            L.group (p, { "unison_voices", "unison_detune", "unison_blend", "detune_power", "detune_range" });  L.newRow();
            L.group (p, { "stereo_spread", "frame_spread", "phase", "random_phase" });                         L.newRow();
            L.group (p, { "distortion_type", "distortion_amount", "distortion_spread", "distortion_phase" });   L.newRow();
            L.group (p, { "spectral_morph_type", "spectral_morph_amount", "spectral_morph_spread" });            L.newRow();
            L.group (p, { "spectral_morph_phase", "stack_style", "midi_track", "spectral_unison" });            L.newRow();
            L.group (p, { "smooth_interpolation" });
            L.vital ("transpose_quantize", p + "transpose_quantize", "transpose_quantize", true);
            L.number();
            renameChoices (s, "distortion_type", { { 7, "FM <- Mod A" }, { 8, "FM <- Mod B" }, { 9, "FM <- Sample" },
                                                   { 10, "RM <- Mod A" }, { 11, "RM <- Mod B" }, { 12, "RM <- Sample" } });
            return s;
        }();
        return s;
    }

    class PatchOscillator : public vt::OscillatorModule
    {
    public:
        explicit PatchOscillator (std::shared_ptr<vt::Wavetable> shared) : vt::OscillatorModule ("osc_1")
        {
            wavetable_ = std::move (shared);
        }
    };
}

class VitaPatchOscModule final : public vitapatch::VitalModuleBase
{
public:
    static ModuleDescriptor descriptor() { return vitapatch::oscSpec().d; }

    VitaPatchOscModule() : VitalModuleBase (vitapatch::oscSpec(), Lanes::PerVoice, { 1, 2, 3 }, 0)
    {
        wavetable = std::make_shared<vitapatch::vt::Wavetable> (vitapatch::vt::kNumOscillatorWaveFrames);
        creator = std::make_unique<aqp_vita::WavetableCreator> (wavetable.get());
        creator->init();
        creator->render();
        warp = std::make_unique<aqp_vita::LineGenerator>();
        warp->jsonToState (defaultWarp());
        makeLanes();
    }

    juce::String saveCustomState() const override
    {
        vitapatch::Json j;
        j["wavetable"] = creator->stateToJson();
        j["seed"] = seed;
        j["warp"] = warp->stateToJson();
        return juce::String (j.dump());
    }

    void loadCustomState (const juce::String& state) override
    {
        auto j = vitapatch::Json::parse (state.toStdString(), nullptr, false);
        if (j.is_discarded() || ! j.is_object())
            return;
        const juce::SpinLock::ScopedLockType lock (dataLock);
        try
        {
            if (j.count ("wavetable"))
            {
                creator->jsonToState (j["wavetable"]);
                creator->render();
            }
            if (j.count ("warp"))
                warp->jsonToState (j["warp"]);
            if (j.count ("seed"))
            {
                seed = j["seed"].get<int>();
                for (int i = 0; i < numLanes(); ++i)
                    osc (lane (i))->oscillator()->setRandomSeed (seed);
            }
        }
        catch (const std::exception&) {}
    }

    aqp_vita::WavetableCreator& wavetableCreator() { return *creator; }

    int randomSeed() const { return seed; }
    void setRandomSeed (int newSeed)
    {
        newSeed &= 0x7fffffff;
        aqp_vita::vital::RandomValues::instance (newSeed);   // its table made here, not under the lock
        const juce::SpinLock::ScopedLockType lock (dataLock);
        seed = newSeed;
        for (int i = 0; i < numLanes(); ++i)
            osc (lane (i))->oscillator()->setRandomSeed (seed);
    }

    // the wave frame the Frame knob is on
    int extraContentHeight() const override { return 64; }
    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        juce::WeakReference<VitalModuleBase> weak (this);
        const int frameKnob = paramIndex ("wave_frame");
        const int morphKnob = paramIndex ("spectral_morph_type");

        // the wave, with the die over its right end while the morph is Random Amplitudes
        struct View : public juce::Component
        {
            std::unique_ptr<juce::Component> wave, dice;
            void resized() override
            {
                wave->setBounds (getLocalBounds());
                const int side = juce::jmin (getHeight() - 4, 34);
                dice->setBounds (getWidth() - side - 8, (getHeight() - side) / 2, side, side);
            }
        };
        auto view = std::make_unique<View>();
        view->dice = std::make_unique<vitapatch::SeedDice> (
            [weak] { auto* self = dynamic_cast<VitaPatchOscModule*> (weak.get()); return self != nullptr ? self->randomSeed() : 0; },
            [weak] (int s) { if (auto* self = dynamic_cast<VitaPatchOscModule*> (weak.get())) self->setRandomSeed (s); },
            [weak, morphKnob]
            {
                auto* self = dynamic_cast<VitaPatchOscModule*> (weak.get());
                return self != nullptr && morphKnob >= 0
                    && std::lround (self->getParameterBase (morphKnob)) == aqp_vita::vital::SynthOscillator::kRandomAmplitudes;
            });
        view->wave = std::make_unique<vitapatch::ShapeView> ([weak, frameKnob] (std::vector<float>& out)
        {
            auto* self = dynamic_cast<VitaPatchOscModule*> (weak.get());
            if (self == nullptr)
                return false;
            const juce::SpinLock::ScopedTryLockType lock (self->dataLock);
            if (! lock.isLocked())
                return false;
            auto& w = *self->wavetable;
            const int frames = juce::jmax (1, w.numFrames());
            const float position = self->getParameterBase (frameKnob) / (float) (vitapatch::vt::kNumOscillatorWaveFrames - 1);
            const int frame = juce::jlimit (0, frames - 1, juce::roundToInt (position * (float) (frames - 1)));
            return vitapatch::resample (w.getBuffer (frame), vitapatch::vt::Wavetable::kWaveformSize, 160, out);
        }, -1.0f, 1.0f, getDescriptor().colour().brighter (0.6f));
        view->addAndMakeVisible (*view->wave);
        view->addChildComponent (*view->dice);
        view->setInterceptsMouseClicks (false, true);   // only the die takes clicks; the rest drags the module
        return view;
    }

protected:
    static vitapatch::Json defaultWarp()
    {
        vitapatch::Json data;
        data["name"] = "Triangle";
        data["num_points"] = 3;
        data["points"] = { 0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f };
        data["powers"] = { 0.0f, 0.0f, 0.0f };
        data["smooth"] = false;
        return data;
    }

    static vitapatch::vt::OscillatorModule* osc (vitapatch::Lane& l)
    {
        return static_cast<vitapatch::vt::OscillatorModule*> (l.module.get());
    }

    std::unique_ptr<vitapatch::vt::SynthModule> createVital (vitapatch::Lane& l) override
    {
        namespace vt = vitapatch::vt;
        auto m = std::make_unique<vitapatch::PatchOscillator> (wavetable);
        m->init();
        m->plug (l.reset, vt::OscillatorModule::kReset);
        m->plug (l.retrigger, vt::OscillatorModule::kRetrigger);
        m->plug (l.midi, vt::OscillatorModule::kMidi);
        m->plug (l.active, vt::OscillatorModule::kActiveVoices);
        m->oscillator()->setFirstOscillatorOutput (l.audio[0]);
        m->oscillator()->setSecondOscillatorOutput (l.audio[1]);
        m->oscillator()->setSampleOutput (l.audio[2]);
        m->oscillator()->setRandomSeed (seed);
        m->oscillator()->setWarpCurve (warp.get());
        vitapatch::setControl (*m, "osc_1_on", 1.0f);
        m->enable (true);
        return m;
    }

    void writeOutputs (vitapatch::Lane& l, int i, StereoFrame* out) override
    {
        out[0] = polyToFrame (l.module->output (vitapatch::vt::OscillatorModule::kLevelled)->buffer[i]);
        out[1] = polyToFrame (l.module->output (vitapatch::vt::OscillatorModule::kRaw)->buffer[i]);
    }

private:
    std::shared_ptr<vitapatch::vt::Wavetable> wavetable;
    std::unique_ptr<aqp_vita::WavetableCreator> creator;
    std::unique_ptr<aqp_vita::LineGenerator> warp;
    int seed { aqp_vita::vital::RandomValues::kSeed };   // Vital's default when a preset has none
};

//==============================================================================
// Sampler
//==============================================================================
namespace vitapatch
{
    inline const Spec& samplerSpec()
    {
        static const Spec s = []
        {
            Spec s;
            s.d = base ("sampler", "Sampler", ModuleSection::Oscillator, 1,
                        "Vita's sampler, playing the preset's sample (kept in the patch). Pitch In adds semitones "
                        "to the note; Raw Out is what the oscillators' FM / RM <- Sample hear.");
            s.d.sockets = { modIn ("pitchIn", "Pitch In"), audioOut ("audioOut", "Audio Out"), audioOut ("rawOut", "Raw Out") };
            Layout L { s };
            L.group ("sample_", { "level", "pan", "transpose", "tune" });                     L.newRow();
            L.group ("sample_", { "keytrack", "loop", "bounce", "random_phase" });
            L.vital ("transpose_quantize", "sample_transpose_quantize", "transpose_quantize", true);
            return s;
        }();
        return s;
    }
}

class VitaPatchSamplerModule final : public vitapatch::VitalModuleBase
{
public:
    static ModuleDescriptor descriptor() { return vitapatch::samplerSpec().d; }

    VitaPatchSamplerModule() : VitalModuleBase (vitapatch::samplerSpec(), Lanes::PerVoice, {}, 0)
    {
        sample = std::make_shared<vitapatch::vt::Sample>();
        makeLanes();
    }

    juce::String saveCustomState() const override { return juce::String (sample->stateToJson().dump()); }

    void loadCustomState (const juce::String& state) override
    {
        auto j = vitapatch::Json::parse (state.toStdString(), nullptr, false);
        if (j.is_discarded() || ! j.is_object())
            return;
        const juce::SpinLock::ScopedLockType lock (dataLock);
        try { sample->jsonToState (j); } catch (const std::exception&) {}
    }

protected:
    std::unique_ptr<vitapatch::vt::SynthModule> createVital (vitapatch::Lane& l) override
    {
        namespace vt = vitapatch::vt;
        std::unique_ptr<vt::SampleModule> m;
        {
            // its own default sample would be replaced at once by the shared one
            const aqp_vita::vital::Sample::PlaceholderDefault placeholder;
            m = std::make_unique<vt::SampleModule>();
        }
        m->shareSample (sample);
        m->init();
        m->plug (l.reset, vt::SampleModule::kReset);
        m->plug (l.midi, vt::SampleModule::kMidi);
        m->plug (l.noteCount, vt::SampleModule::kNoteCount);
        vitapatch::setControl (*m, "sample_on", 1.0f);
        return m;
    }

    void writeOutputs (vitapatch::Lane& l, int i, StereoFrame* out) override
    {
        out[0] = polyToFrame (l.module->output (vitapatch::vt::SampleModule::kLevelled)->buffer[i]);
        out[1] = polyToFrame (l.module->output (vitapatch::vt::SampleModule::kRaw)->buffer[i]);
    }

private:
    std::shared_ptr<vitapatch::vt::Sample> sample;
};

//==============================================================================
// Filter (Filter 1 / 2, and the effects chain's filter)
//==============================================================================
namespace vitapatch
{
    struct FilterType { int model, style; juce::String name; };

    inline const std::vector<FilterType>& filterTypes()
    {
        static const std::vector<FilterType> types = []
        {
            namespace st = aqp_vita::strings;
            std::vector<FilterType> t;
            const int counts[] = { 5, 5, 5, 5, 2, 3, 6, 2 };
            for (int m = 0; m < 8; ++m)
                for (int s = 0; s < counts[m]; ++s)
                {
                    juce::String style;
                    switch (m)
                    {
                        case 4: style = st::kDiodeStyleNames[s]; break;
                        case 5: style = s == 0 ? "AOIE" : s == 1 ? "AIUO" : "The Mouth"; break;
                        case 6: style = st::kCombStyleNames[s]; break;
                        case 7: style = s ? "Negative" : "Positive"; break;
                        default: style = st::kFilterStyleNames[s]; break;
                    }
                    t.push_back ({ m, s, juce::String (st::kFilterModelNames[m]) + " " + style });
                }
            return t;
        }();
        return types;
    }

    inline int filterTypeIndex (int model, int style)
    {
        const auto& t = filterTypes();
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i].model == model && t[i].style == style)
                return (int) i;
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i].model == model)
                return (int) i;
        return 0;
    }

    inline void filterKnobs (Layout& L, const std::string& p)
    {
        juce::StringArray names;
        for (auto& t : filterTypes())
            names.add (t.name);
        L.own (makeCombo ("type", "Type", names, 0, 0, 2).noMod());
        L.group (p, { "cutoff", "resonance", "drive" });                                    L.newRow();
        L.group (p, { "blend", "keytrack", "mix", "blend_transpose" });                    L.newRow();
        L.group (p, { "formant_x", "formant_y", "formant_transpose", "formant_resonance", "formant_spread" });
    }

    inline const Spec& filterSpec()
    {
        static const Spec s = []
        {
            Spec s;
            s.d = base ("filter", "Filter", ModuleSection::Filter, 2,
                        "One of Vita's two voice filters with all of Vital's models (analog, dirty, ladder, "
                        "digital, diode, formant, comb, phaser). Pitch In adds semitones to the note it tracks. "
                        "Serial routing is a cable from the other filter's Audio Out.");
            s.d.sockets = { audioIn ("audioIn", "Audio In"), modIn ("pitchIn", "Pitch In"), audioOut ("audioOut", "Audio Out") };
            Layout L { s };
            filterKnobs (L, "filter_1_");
            L.number();
            return s;
        }();
        return s;
    }
}

class VitaPatchFilterModule final : public vitapatch::VitalModuleBase
{
public:
    static ModuleDescriptor descriptor() { return vitapatch::filterSpec().d; }

    VitaPatchFilterModule() : VitalModuleBase (vitapatch::filterSpec(), Lanes::PerVoice, { 0 }, 1)
    {
        typeIndex = vitapatch::paramIndexIn (spec, "type");
        makeLanes();
    }

protected:
    std::unique_ptr<vitapatch::vt::SynthModule> createVital (vitapatch::Lane& l) override
    {
        namespace vt = vitapatch::vt;
        auto m = std::make_unique<vt::FilterModule> ("filter_1");
        m->init();
        m->plug (l.audio[0], vt::FilterModule::kAudio);
        m->plug (l.reset, vt::FilterModule::kReset);
        m->plug (l.keytrack, vt::FilterModule::kKeytrack);
        m->plug (l.midi, vt::FilterModule::kMidi);
        auto controls = m->getControls();
        controls["filter_1_on"]->set (1.0f);
        l.extra[0] = controls["filter_1_model"];
        l.extra[1] = controls["filter_1_style"];
        return m;
    }

    void beforeBlock (vitapatch::Lane& l, const StereoFrame*) override
    {
        const auto& t = vitapatch::filterTypes()[(size_t) juce::jlimit (0, (int) vitapatch::filterTypes().size() - 1,
                                                                       (int) getParameterBase (typeIndex))];
        if (l.extra[0]->value() != (float) t.model) l.extra[0]->set ((float) t.model);
        if (l.extra[1]->value() != (float) t.style) l.extra[1]->set ((float) t.style);
    }

    void writeOutputs (vitapatch::Lane& l, int i, StereoFrame* out) override
    {
        out[0] = polyToFrame (l.module->output()->buffer[i]);
    }

private:
    int typeIndex { 0 };
};

//==============================================================================
// Envelope, LFO, Random - per voice, and the Mono copies effects listen to
//==============================================================================
namespace vitapatch
{
    inline void monoSockets (Spec& s, bool mono)
    {
        if (mono)
            s.d.sockets.insert (s.d.sockets.begin(), midiIn ("midiIn", "Midi In"));
    }

    inline const Spec& envSpec (bool mono)
    {
        static const auto make = [] (bool m)
        {
            Spec s;
            s.d = base (m ? "monoenv" : "env", m ? "Mono Env" : "Env", ModuleSection::Utility, m ? 11 : 5,
                        m ? "A Vita envelope that follows the newest note played (or arriving at Midi In): the copy "
                            "of an envelope that modulates an effect, as Vital lets the newest voice do."
                          : "One of Vita's envelopes (delay, attack, hold, decay, sustain, release and the three "
                            "curves). Out is 0..1. Env 1 is the amplitude envelope: it goes to Vita - Amp's Env In.");
            s.d.sockets = { modOut ("out", "Out") };
            monoSockets (s, m);
            Layout L { s };
            L.group ("env_1_", { "delay", "attack", "hold", "decay", "release" });            L.newRow();
            L.group ("env_1_", { "sustain", "attack_power", "decay_power", "release_power" });
            L.number();
            return s;
        };
        static const Spec poly = make (false), monoSpec = make (true);
        return mono ? monoSpec : poly;
    }

    template <bool Mono>
    class EnvModule : public VitalModuleBase
    {
    public:
        static ModuleDescriptor descriptor() { return envSpec (Mono).d; }
        EnvModule() : VitalModuleBase (envSpec (Mono), Mono ? Lanes::Mono : Lanes::PerVoice, {}, -1)
        {
            releaseIndex = paramIndexIn (spec, "release");
            makeLanes();
        }

        double voiceTailSeconds() const override
        {
            const double r = getParameterBase (releaseIndex);
            return r * r * r * r + 0.05;
        }

    protected:
        std::unique_ptr<vt::SynthModule> createVital (Lane& l) override
        {
            auto m = std::make_unique<vt::EnvelopeModule> ("env_1", true);
            m->init();
            m->plug (l.retrigger, vt::EnvelopeModule::kTrigger);
            return m;
        }

        void writeOutputs (Lane& l, int i, StereoFrame* out) override
        {
            out[0] = modValue (l.module->output (vt::EnvelopeModule::kValue), i);
        }

        int releaseIndex { 0 };
    };

    inline const Spec& lfoSpec (bool mono)
    {
        static const auto make = [] (bool m)
        {
            Spec s;
            s.d = base (m ? "monolfo" : "lfo", m ? "Mono LFO" : "LFO", ModuleSection::Utility, m ? 12 : 6,
                        m ? "A Vita LFO that follows the newest note played (or arriving at Midi In): the copy of an "
                            "LFO that modulates an effect, as Vital lets the newest voice do. Its shape comes from the preset."
                          : "One of Vita's LFOs, with its drawn shape from the preset, the sync modes (trigger, sync, "
                            "envelope, loop) and tempo. Out is 0..1; Pitch In moves its note for keytracking.");
            s.d.sockets = { modIn ("pitchIn", "Pitch In"), modOut ("out", "Out") };
            monoSockets (s, m);
            Layout L { s };
            L.group ("lfo_1_", { "sync", "tempo", "frequency" });                                  L.newRow();
            L.group ("lfo_1_", { "sync_type", "phase", "fade_time", "delay_time" });             L.newRow();
            L.group ("lfo_1_", { "smooth_mode", "smooth_time", "stereo" });                      L.newRow();
            L.group ("lfo_1_", { "keytrack_transpose", "keytrack_tune" });
            L.number();
            return s;
        };
        static const Spec poly = make (false), monoSpec = make (true);
        return mono ? monoSpec : poly;
    }

    template <bool Mono>
    class LfoModule : public VitalModuleBase
    {
    public:
        static ModuleDescriptor descriptor() { return lfoSpec (Mono).d; }
        LfoModule() : VitalModuleBase (lfoSpec (Mono), Mono ? Lanes::Mono : Lanes::PerVoice, {}, Mono ? 1 : 0)
        {
            shape = std::make_unique<aqp_vita::LineGenerator>();
            shape->setLoop (false);
            shape->initTriangle();
            makeLanes();
        }

        juce::String saveCustomState() const override { return juce::String (shape->stateToJson().dump()); }
        void loadCustomState (const juce::String& state) override
        {
            auto j = Json::parse (state.toStdString(), nullptr, false);
            if (j.is_discarded() || ! aqp_vita::LineGenerator::isValidJson (j))
                return;
            const juce::SpinLock::ScopedLockType lock (dataLock);
            shape->jsonToState (j);
            shape->render();
        }

        aqp_vita::LineGenerator& lineGenerator() { return *shape; }

        // the LFO's shape
        int extraContentHeight() const override { return 64; }
        std::unique_ptr<juce::Component> createExtraContentComponent() override
        {
            juce::WeakReference<VitalModuleBase> weak (this);
            auto* line = shape.get();
            return std::make_unique<ShapeView> ([weak, line] (std::vector<float>& out)
            {
                auto* self = dynamic_cast<LfoModule*> (weak.get());
                if (self == nullptr)
                    return false;
                const juce::SpinLock::ScopedTryLockType lock (self->dataLock);
                return lock.isLocked() && resample (line->getBuffer(), line->resolution(), 160, out);
            }, 0.0f, 1.0f, getDescriptor().colour().brighter (0.6f));
        }

    protected:
        std::unique_ptr<vt::SynthModule> createVital (Lane& l) override
        {
            auto m = std::make_unique<vt::LfoModule> ("lfo_1", shape.get(), bps->output());
            m->init();
            m->plug (l.retrigger, vt::LfoModule::kNoteTrigger);
            m->plug (l.noteCount, vt::LfoModule::kNoteCount);
            m->plug (l.midi, vt::LfoModule::kMidi);
            m->setControlRate (false);
            return m;
        }

        void writeOutputs (Lane& l, int i, StereoFrame* out) override
        {
            out[0] = modValue (l.module->output (vt::LfoModule::kValue), i);
        }

        std::unique_ptr<aqp_vita::LineGenerator> shape;
    };

    inline const Spec& randomSpec (bool mono)
    {
        static const auto make = [] (bool m)
        {
            Spec s;
            s.d = base (m ? "monorandom" : "random", m ? "Mono Random" : "Random", ModuleSection::Utility, m ? 13 : 7,
                        m ? "A Vita random LFO that follows the newest note played (or arriving at Midi In): the copy "
                            "of a random LFO that modulates an effect."
                          : "One of Vita's random LFOs (perlin, sample & hold, sine interpolate, lorenz attractor). "
                            "Out is 0..1.");
            s.d.sockets = { modIn ("pitchIn", "Pitch In"), modOut ("out", "Out") };
            monoSockets (s, m);
            Layout L { s };
            L.group ("random_1_", { "style", "sync", "tempo", "frequency" });           L.newRow();
            L.group ("random_1_", { "sync_type", "stereo", "keytrack_transpose", "keytrack_tune" });
            L.number();
            return s;
        };
        static const Spec poly = make (false), monoSpec = make (true);
        return mono ? monoSpec : poly;
    }

    template <bool Mono>
    class RandomModule : public VitalModuleBase
    {
    public:
        static ModuleDescriptor descriptor() { return randomSpec (Mono).d; }
        RandomModule() : VitalModuleBase (randomSpec (Mono), Mono ? Lanes::Mono : Lanes::PerVoice, {}, Mono ? 1 : 0)
        {
            makeLanes();
        }

    protected:
        std::unique_ptr<vt::SynthModule> createVital (Lane& l) override
        {
            auto m = std::make_unique<vt::RandomLfoModule> ("random_1", bps->output());
            m->init();
            m->plug (l.retrigger, vt::RandomLfoModule::kNoteTrigger);
            m->plug (l.midi, vt::RandomLfoModule::kMidi);
            return m;
        }

        void writeOutputs (Lane& l, int, StereoFrame* out) override
        {
            const auto& p = l.module->output()->buffer[0];
            out[0] = { p[0], p[1] };
        }
    };
}

using VitaPatchEnvModule        = vitapatch::EnvModule<false>;
using VitaPatchMonoEnvModule    = vitapatch::EnvModule<true>;
using VitaPatchLfoModule        = vitapatch::LfoModule<false>;
using VitaPatchMonoLfoModule    = vitapatch::LfoModule<true>;
using VitaPatchRandomModule     = vitapatch::RandomModule<false>;
using VitaPatchMonoRandomModule = vitapatch::RandomModule<true>;

//==============================================================================
// Effects (one Vital effect each) and the output stage
//==============================================================================
namespace vitapatch
{
    inline const char* effectKind (int e)
    {
        static const char* kinds[] = { "Chorus", "Compressor", "Delay", "Distortion", "EQ", "Effects Filter",
                                       "Flanger", "Phaser", "Reverb" };
        return kinds[e];
    }

    inline Spec makeEffectSpec (int e)
    {
        namespace C = vt::constants;
        Spec s;
        const std::string name = aqp_vita::strings::kEffectOrder[e];
        s.d = base (juce::String (name).removeCharacters ("_"), effectKind (e), ModuleSection::Effect, 20 + e,
                    juce::String ("Vita's ") + effectKind (e) + " (Vital's own), with every knob of its panel. "
                    "Vita's patch chains its effects in the preset's order.");
        s.d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        if (e == C::kFilterFx)
            s.d.sockets.insert (s.d.sockets.begin() + 1, modIn ("pitchIn", "Key In"));
        Layout L { s };
        const std::string p = name + "_";
        switch (e)
        {
            case C::kChorus:
                L.group (p, { "dry_wet", "feedback", "cutoff", "spread", "voices" });          L.newRow();
                L.group (p, { "sync", "tempo", "frequency", "mod_depth" });                    L.newRow();
                L.group (p, { "delay_1", "delay_2" });
                break;
            case C::kCompressor:
                L.group (p, { "mix", "attack", "release", "enabled_bands" });                  L.newRow();
                for (const char* band : { "low", "band", "high" })
                {
                    const std::string b = band;
                    L.group (p, { (b + "_upper_threshold").c_str() });
                    L.group (p, { (b + "_lower_threshold").c_str() });
                    L.group (p, { (b + "_upper_ratio").c_str() });
                    L.group (p, { (b + "_lower_ratio").c_str() });
                    L.group (p, { (b + "_gain").c_str() });
                    L.newRow();
                }
                break;
            case C::kDelay:
                L.group (p, { "dry_wet", "feedback", "style" });                                L.newRow();
                L.group (p, { "sync", "tempo", "frequency" });                                  L.newRow();
                L.group (p, { "aux_sync", "aux_tempo", "aux_frequency" });                      L.newRow();
                L.group (p, { "filter_cutoff", "filter_spread" });
                break;
            case C::kDistortion:
                L.group (p, { "type", "drive", "mix" });                                        L.newRow();
                L.group (p, { "filter_order", "filter_cutoff", "filter_resonance", "filter_blend" });
                break;
            case C::kEq:
                for (const char* band : { "low", "band", "high" })
                {
                    const std::string b = band;
                    L.group (p, { (b + "_mode").c_str() });
                    L.group (p, { (b + "_cutoff").c_str() });
                    L.group (p, { (b + "_gain").c_str() });
                    L.group (p, { (b + "_resonance").c_str() });
                    L.newRow();
                }
                break;
            case C::kFilterFx:
                filterKnobs (L, p);
                break;
            case C::kFlanger:
                L.group (p, { "dry_wet", "feedback", "center", "mod_depth" });                L.newRow();
                L.group (p, { "sync", "tempo", "frequency", "phase_offset" });
                break;
            case C::kPhaser:
                L.group (p, { "dry_wet", "feedback", "center", "blend", "mod_depth" });       L.newRow();
                L.group (p, { "sync", "tempo", "frequency", "phase_offset" });
                break;
            case C::kReverb:
                L.group (p, { "dry_wet", "size", "decay_time", "delay" });                     L.newRow();
                L.group (p, { "pre_low_cutoff", "pre_high_cutoff", "chorus_amount", "chorus_frequency" });   L.newRow();
                L.group (p, { "low_shelf_cutoff", "low_shelf_gain", "high_shelf_cutoff", "high_shelf_gain" });
                break;
            default: break;
        }
        return s;
    }

    inline const Spec& effectSpec (int e)
    {
        static const std::vector<Spec> specs = []
        {
            std::vector<Spec> v;
            for (int i = 0; i < vt::constants::kNumEffects; ++i)
                v.push_back (makeEffectSpec (i));
            return v;
        }();
        return specs[(size_t) e];
    }

    class EffectModule : public VitalModuleBase
    {
    public:
        explicit EffectModule (int e) : VitalModuleBase (effectSpec (e), Lanes::Global, { 0 },
                                                          e == vt::constants::kFilterFx ? 1 : -1),
                                        effect (e)
        {
            typeIndex = paramIndexIn (spec, "type");
            makeLanes();
        }

    protected:
        std::unique_ptr<vt::SynthModule> createVital (Lane& l) override
        {
            namespace C = vt::constants;
            const vt::Output* tempo = bps->output();
            std::unique_ptr<vt::SynthModule> m;
            switch (effect)
            {
                case C::kChorus:     m = std::make_unique<vt::ChorusModule> (tempo); break;
                case C::kCompressor: m = std::make_unique<vt::CompressorModule>(); break;
                case C::kDelay:      m = std::make_unique<vt::DelayModule> (tempo); break;
                case C::kDistortion: m = std::make_unique<vt::DistortionModule>(); break;
                case C::kEq:         m = std::make_unique<vt::EqualizerModule>(); break;
                case C::kFlanger:    m = std::make_unique<vt::FlangerModule> (tempo); break;
                case C::kPhaser:     m = std::make_unique<vt::PhaserModule> (tempo); break;
                case C::kReverb:     m = std::make_unique<vt::ReverbModule>(); break;
                default:
                {
                    auto f = std::make_unique<vt::PatchFilterFxCore>();
                    f->plug (l.audio[0], 0);
                    f->plug (l.keytrack, 1);
                    m = std::move (f);
                    break;
                }
            }
            m->init();
            m->enable (true);
            if (effect == C::kFilterFx)
            {
                auto controls = m->getControls();
                model = controls["filter_fx_model"];
                style = controls["filter_fx_style"];
            }
            return m;
        }

        void beforeBlock (Lane& l, const StereoFrame*) override
        {
            if (effect == vt::constants::kFilterFx)
            {
                // with nothing at Key In the filter tracks middle C, as Vital's does with no voices
                if (! isInputConnected (1))
                    l.keytrack->buffer[0] = 0.0f;
                const auto& t = filterTypes()[(size_t) juce::jlimit (0, (int) filterTypes().size() - 1,
                                                                     (int) getParameterBase (typeIndex))];
                if (model->value() != (float) t.model) model->set ((float) t.model);
                if (style->value() != (float) t.style) style->set ((float) t.style);
            }
        }

        void processBlock (Lane& l) override
        {
            if (effect == vt::constants::kFilterFx)
                l.module->process (kBlock);
            else
                l.module->processWithInput (l.audio[0]->buffer, kBlock);
        }

        void writeOutputs (Lane& l, int i, StereoFrame* out) override
        {
            out[0] = polyToFrame (l.module->output()->buffer[i]);
        }

        const int effect;
        int typeIndex { -1 };
        vt::Value *model { nullptr }, *style { nullptr };
    };

    inline const Spec& outputSpec()
    {
        static const Spec s = []
        {
            Spec s;
            s.d = base ("output", "Output", ModuleSection::Effect, 30,
                        "The end of Vita: stereo routing, the volume knob and Vita's output clamp. The effects' "
                        "chain and the Amp's Direct Out both come in here.");
            s.d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
            Layout L { s };
            L.group ("", { "volume", "stereo_routing", "stereo_mode" });
            return s;
        }();
        return s;
    }

    class OutputModule : public VitalModuleBase
    {
    public:
        static ModuleDescriptor descriptor() { return outputSpec().d; }
        OutputModule() : VitalModuleBase (outputSpec(), Lanes::Global, { 0 }, -1) { makeLanes(); }

    protected:
        std::unique_ptr<vt::SynthModule> createVital (Lane& l) override
        {
            auto m = std::make_unique<vt::PatchOutputCore>();
            m->plug (l.audio[0], 0);
            m->init();
            return m;
        }
        void writeOutputs (Lane& l, int i, StereoFrame* out) override
        {
            out[0] = polyToFrame (l.module->output()->buffer[i]);
        }
    };
}

#define VITA_PATCH_EFFECT(ClassName, effectIndex) \
    class ClassName final : public vitapatch::EffectModule \
    { \
    public: \
        static ModuleDescriptor descriptor() { return vitapatch::effectSpec (effectIndex).d; } \
        ClassName() : EffectModule (effectIndex) {} \
    };

VITA_PATCH_EFFECT (VitaPatchChorusModule,     vitapatch::vt::constants::kChorus)
VITA_PATCH_EFFECT (VitaPatchCompressorModule, vitapatch::vt::constants::kCompressor)
VITA_PATCH_EFFECT (VitaPatchDelayModule,      vitapatch::vt::constants::kDelay)
VITA_PATCH_EFFECT (VitaPatchDistortionModule, vitapatch::vt::constants::kDistortion)
VITA_PATCH_EFFECT (VitaPatchEqModule,         vitapatch::vt::constants::kEq)
VITA_PATCH_EFFECT (VitaPatchFilterFxModule,   vitapatch::vt::constants::kFilterFx)
VITA_PATCH_EFFECT (VitaPatchFlangerModule,    vitapatch::vt::constants::kFlanger)
VITA_PATCH_EFFECT (VitaPatchPhaserModule,     vitapatch::vt::constants::kPhaser)
VITA_PATCH_EFFECT (VitaPatchReverbModule,     vitapatch::vt::constants::kReverb)
#undef VITA_PATCH_EFFECT

using VitaPatchOutputModule = vitapatch::OutputModule;

//==============================================================================
// Voice: the note's pitch (portamento, bend, tune, transpose) and the voice's
// own modulation sources. Amp: Env 1 squared, velocity tracking, polyphony.
// Macros. Mod: one of Vital's modulation connections, when it is more than a
// plain amount (bipolar, power, a remap curve - or modulated itself).
//==============================================================================
namespace vitapatch
{
    // a knob's value in Vital's units, read by index from a module's Spec
    struct KnobReader
    {
        const aquanode::SynthModule& m;
        float operator() (int index) const { return index >= 0 ? m.param (index) : 0.0f; }
    };

    inline const Spec& voiceSpec (bool mono)
    {
        static const auto make = [] (bool m)
        {
            Spec s;
            s.d = base (m ? "monovoice" : "voice", m ? "Mono Voice" : "Voice", ModuleSection::Utility, m ? 10 : 4,
                        m ? "Vita's voice sources for the newest note played (or arriving at Midi In): the copy that "
                            "modulates effects. Pitch Out is that note's glide, bend, tune and transpose in semitones."
                          : "Vita's voice: Pitch Out (semitones added to the note - portamento, pitch bend, tune and "
                            "transpose) for the oscillators, sampler, filters and LFOs, and the voice's modulation "
                            "sources (velocity, note, wheels, aftertouch, a random value per note).");
            s.d.sockets = { modOut ("pitchOut", "Pitch Out"), modOut ("velocity", "Velocity"), modOut ("note", "Note"),
                            modOut ("noteInOctave", "Note In Octave"), modOut ("aftertouch", "Aftertouch"),
                            modOut ("modWheel", "Mod Wheel"), modOut ("pitchWheel", "Pitch Wheel"),
                            modOut ("random", "Random"), modOut ("lift", "Lift"), modOut ("slide", "Slide") };
            monoSockets (s, m);
            Layout L { s };
            L.group ("", { "portamento_time", "portamento_slope", "portamento_force", "portamento_scale" });  L.newRow();
            L.group ("", { "pitch_bend_range", "voice_tune", "voice_transpose" });
            return s;
        };
        static const Spec poly = make (false), monoSpec = make (true);
        return mono ? monoSpec : poly;
    }

    template <bool Mono>
    class VoiceModule : public aquanode::SynthModule
    {
    public:
        static ModuleDescriptor descriptor() { return voiceSpec (Mono).d; }

        VoiceModule()
        {
            const auto& s = voiceSpec (Mono);
            kTime = paramIndexIn (s, "portamento_time");
            kSlope = paramIndexIn (s, "portamento_slope");
            kForce = paramIndexIn (s, "portamento_force");
            kScale = paramIndexIn (s, "portamento_scale");
            kBend = paramIndexIn (s, "pitch_bend_range");
            kTune = paramIndexIn (s, "voice_tune");
            kTranspose = paramIndexIn (s, "voice_transpose");
        }

        VoiceMode voiceMode() const override { return Mono ? VoiceMode::Global : VoiceMode::PerVoice; }
        bool wantsRawMidi() const override { return Mono; }

        void reset() override
        {
            for (auto& v : voices) v = {};
            lastNote = -1.0f;
            pressed = 0;
            held.clear();
        }

        void voiceNoteOn (int v, int note, bool) override { if (! Mono) start (voices[(size_t) v], (float) note); }
        void voiceVelocity (int v, float vel) override { if (! Mono) voices[(size_t) v].velocity = vel; }
        void voiceNoteOff (int v) override
        {
            if (Mono) return;
            if (voices[(size_t) v].held) { voices[(size_t) v].held = false; pressed = juce::jmax (0, pressed - 1); }
        }

        void rawMidiMessage (const juce::MidiMessage& m) override
        {
            if (! Mono) return;
            if (m.isNoteOn())
            {
                held.removeAllInstancesOf (m.getNoteNumber());
                held.add (m.getNoteNumber());
                pressed = held.size() - 1;
                start (voices[0], (float) m.getNoteNumber());
                voices[0].velocity = m.getFloatVelocity();
            }
            else if (m.isNoteOff())
            {
                held.removeAllInstancesOf (m.getNoteNumber());
                pressed = held.size();
            }
        }

        void processVoiceSample (int v, const StereoFrame*, StereoFrame* out) override { run (voices[(size_t) v], out); }
        void processSample (const StereoFrame*, StereoFrame* out) override { run (voices[0], out); }

    private:
        struct State
        {
            float note { 60.0f }, source { 60.0f }, velocity { 1.0f }, random { 0.0f };
            float position { 1.0f }, glided { 60.0f };
            int pos { 0 };
            bool held { false };
        };

        void start (State& s, float note)
        {
            ++pressed;
            s.held = true;
            s.note = note;
            s.source = lastNote >= 0.0f ? lastNote : note;
            lastNote = note;
            // without "Always" Vital glides only when the note joins one already held
            s.position = (param (kForce) < 0.5f && pressed <= 1) ? 1.0f : 0.0f;
            s.random = rng.nextFloat();
            s.pos = 0;
        }

        void run (State& s, StereoFrame* out)
        {
            if (s.pos == 0)
            {
                // PortamentoSlope, once per block as in Vital
                float seconds = std::pow (2.0f, param (kTime));
                if (seconds <= vitapatch::vt::PortamentoSlope::kMinPortamentoTime)
                {
                    s.position = 1.0f;
                    s.glided = s.note;
                }
                else
                {
                    if (param (kScale) >= 0.5f)
                        seconds *= std::abs (s.note - s.source) / 12.0f;
                    const float delta = seconds > 0.0f ? (float) (kBlock / (seconds * sampleRate)) : 1.0f;
                    s.position = juce::jlimit (0.0f, 1.0f, s.position + delta);
                    const float shaped = vitapatch::vt::futils::powerScale (s.position, -param (kSlope));
                    s.glided = s.source + (s.note - s.source) * shaped;
                }
            }
            if (++s.pos >= kBlock)
                s.pos = 0;

            const float bend = midiControls != nullptr ? midiControls->bend : 0.0f;
            const float bent = s.glided + bend * param (kBend) + param (kTune) + param (kTranspose);
            auto mono = [] (float x) { return StereoFrame { x, x }; };
            out[0] = mono (bent - s.note);
            out[1] = mono (s.velocity);
            out[2] = mono (bent / 127.0f);
            const float octave = s.note / 12.0f;
            out[3] = mono (octave - std::floor (octave));
            out[4] = mono (midiControls != nullptr ? midiControls->aftertouch : 0.0f);
            out[5] = mono (midiControls != nullptr ? midiControls->modWheel : 0.0f);
            out[6] = mono (bend * 0.5f + 0.5f);
            out[7] = mono (s.random);
            out[8] = mono (0.0f);
            out[9] = mono (0.0f);
        }

        std::array<State, kMaxVoices> voices {};
        float lastNote { -1.0f };
        int pressed { 0 };
        juce::Array<int> held;
        juce::Random rng;
        int kTime, kSlope, kForce, kScale, kBend, kTune, kTranspose;
    };

    //==========================================================================
    inline const Spec& ampSpec()
    {
        static const Spec s = []
        {
            Spec s;
            s.d = base ("amp", "Amp", ModuleSection::Utility, 3,
                        "Vita's voice amplitude: Env In (Env 1) squared, times velocity tracking and the voice level, "
                        "on both the voice's main path (to the effects) and its Direct Out path (around them). "
                        "Voices is Vita's polyphony.");
            s.d.sockets = { audioIn ("audioIn", "Audio In"), audioIn ("directIn", "Direct In"), modIn ("envIn", "Env In"),
                            audioOut ("audioOut", "Audio Out"), audioOut ("directOut", "Direct Out") };
            Layout L { s };
            L.group ("", { "velocity_track", "voice_amplitude" });
            L.own (makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, 8.0f, 0, "", false, 1.0f).noMod());
            return s;
        }();
        return s;
    }

    class AmpModule : public aquanode::SynthModule
    {
    public:
        static ModuleDescriptor descriptor() { return ampSpec().d; }
        AmpModule()
        {
            kTrack = paramIndexIn (ampSpec(), "velocity_track");
            kLevel = paramIndexIn (ampSpec(), "voice_amplitude");
        }

        VoiceMode voiceMode() const override { return VoiceMode::PerVoice; }

        void reset() override { pool.resetAll(); gate.resetAll(); for (auto& v : voices) v = {}; }
        void voiceNoteOn (int v, int, bool retrigger) override
        {
            pool.noteOn (v, voiceLimit());
            gate.noteOn (v);
            auto& s = voices[(size_t) v];
            if (! retrigger) { s.pos = 0; s.fresh = true; }
        }
        void voiceVelocity (int v, float vel) override { voices[(size_t) v].velocity = vel; }
        void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); gate.noteOff (v); }
        void voiceReset (int v) override { pool.resetVoice (v); gate.resetVoice (v); }

        void processVoiceSample (int v, const StereoFrame* in, StereoFrame* out) override
        {
            auto& s = voices[(size_t) v];
            if (s.pos == 0)
            {
                // SmoothMultiply: the control-rate amplitude ramps across the block
                const float track = param (kTrack);
                const float target = (1.0f + (s.velocity - 1.0f) * track) * param (kLevel);
                s.from = s.fresh ? target : s.to;
                s.to = target;
                s.fresh = false;
            }
            const float a = s.from + (s.to - s.from) * (float) (s.pos + 1) / (float) kBlock;
            if (++s.pos >= kBlock) s.pos = 0;

            const float env = isInputConnected (2) ? in[2][0] : gate.next (v, false, 0.0f, sampleRate);
            float g = env * a;
            g = g * g * pool.nextGain (v, sampleRate);
            out[0] = { in[0][0] * g, in[0][1] * g };
            out[1] = { in[1][0] * g, in[1][1] * g };
        }

    private:
        struct State { float velocity { 1.0f }, from { 0.0f }, to { 0.0f }; int pos { 0 }; bool fresh { true }; };
        std::array<State, kMaxVoices> voices {};
        ModuleVoicePool pool;
        ModuleVoiceGate gate;
        int kTrack, kLevel;
    };

    //==========================================================================
    inline const Spec& macroSpec()
    {
        static const Spec s = []
        {
            Spec s;
            s.d = base ("macros", "Macros", ModuleSection::Utility, 8,
                        "Vita's four macro knobs, each a mod out (0..1) - take them anywhere, and take cables into them.");
            s.d.sockets = { modOut ("macro1", "Macro 1"), modOut ("macro2", "Macro 2"),
                            modOut ("macro3", "Macro 3"), modOut ("macro4", "Macro 4") };
            Layout L { s };
            for (int i = 1; i <= 4; ++i)
                L.vital ("macro" + juce::String (i), "macro_control_" + std::to_string (i), "macro",
                         false, i == 1 ? "Macro 1" : i == 2 ? "Macro 2" : i == 3 ? "Macro 3" : "Macro 4");
            return s;
        }();
        return s;
    }

    class MacrosModule : public aquanode::SynthModule
    {
    public:
        static ModuleDescriptor descriptor() { return macroSpec().d; }
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        void processSample (const StereoFrame*, StereoFrame* out) override
        {
            for (int i = 0; i < 4; ++i)
                out[i] = { param (i), param (i) };
        }
    };

    //==========================================================================
    inline const Spec& modSpec()
    {
        static const Spec s = []
        {
            Spec s;
            s.d = base ("mod", "Mod", ModuleSection::Utility, 9,
                        "One of Vita's modulation connections, for the ones a plain cable depth cannot be: In is the "
                        "source, Out goes to the destination knob at full depth. Amount, Power (Vital's curve), Bipolar "
                        "and the remap curve from the preset work as in Vita's matrix; Amount takes cables.");
            s.d.sockets = { modIn ("in", "In"), modOut ("out", "Out") };
            Layout L { s };
            L.vital ("amount", "modulation_1_amount", "amount");
            L.vital ("power", "modulation_1_power", "power");
            L.vital ("bipolar", "modulation_1_bipolar", "bipolar");
            return s;
        }();
        return s;
    }

    class ModModule : public aquanode::SynthModule
    {
    public:
        static ModuleDescriptor descriptor() { return modSpec().d; }
        ModModule() { map.initLinear(); }

        VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

        juce::String saveCustomState() const override
        {
            auto* self = const_cast<ModModule*> (this);
            return self->map.linear() ? juce::String() : juce::String (self->map.stateToJson().dump());
        }
        void loadCustomState (const juce::String& state) override
        {
            auto j = Json::parse (state.toStdString(), nullptr, false);
            const juce::SpinLock::ScopedLockType lock (mapLock);
            if (j.is_discarded() || ! aqp_vita::LineGenerator::isValidJson (j))
                map.initLinear();
            else
            {
                map.jsonToState (j);
                map.render();
            }
        }

        void processSample (const StereoFrame* in, StereoFrame* out) override { out[0] = transform (in[0][0]); }
        void processVoiceSample (int, const StereoFrame* in, StereoFrame* out) override { out[0] = transform (in[0][0]); }

    private:
        StereoFrame transform (float v)
        {
            {
                const juce::SpinLock::ScopedTryLockType lock (mapLock);
                if (lock.isLocked() && ! map.linear())
                    v = remap (v);
            }
            const float amount = juce::jlimit (-1.0f, 1.0f, param (0));
            const float power = -param (1);
            const float bipolar = param (2) >= 0.5f ? 1.0f : 0.0f;
            float y;
            if (std::abs (power) < 0.005f)
                y = (v - 0.5f * bipolar) * amount;
            else
            {
                const float shift = v * (bipolar + 1.0f) - bipolar;
                const float shaped = amount * vt::futils::powerScale (std::abs (shift), power);
                y = (shift < 0.0f ? -shaped : shaped) * (1.0f - 0.5f * bipolar);
            }
            return { y, y };
        }

        // the remap curve, read with Vital's Catmull-Rom interpolation
        float remap (float v)
        {
            const float* buffer = map.getCubicInterpolationBuffer();
            const float res = (float) map.resolution();
            const float boost = juce::jlimit (0.0f, res, v * res);
            const int index = juce::jlimit (0, map.resolution() - 1, (int) boost);
            const float t = boost - (float) index;
            const float h = 0.5f * t, h2 = t * h, h3 = h2 * t, h33 = 3.0f * h3;
            const float w0 = 2.0f * h2 - h3 - h, w1 = h33 - 5.0f * h2 + 1.0f, w2 = h + 4.0f * h2 - h33, w3 = h3 - h2;
            const float* p = buffer + index;
            return juce::jlimit (-1.0f, 1.0f, w0 * p[0] + w1 * p[1] + w2 * p[2] + w3 * p[3]);
        }

        aqp_vita::LineGenerator map;
        juce::SpinLock mapLock;
    };
}

using VitaPatchVoiceModule     = vitapatch::VoiceModule<false>;
using VitaPatchMonoVoiceModule = vitapatch::VoiceModule<true>;
using VitaPatchAmpModule       = vitapatch::AmpModule;
using VitaPatchMacrosModule    = vitapatch::MacrosModule;
using VitaPatchModModule       = vitapatch::ModModule;

AQUANODE_REGISTER_MODULE (VitaPatchVoiceModule,      VitaPatchVoiceModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchOscModule,        VitaPatchOscModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchSamplerModule,    VitaPatchSamplerModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchFilterModule,     VitaPatchFilterModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchAmpModule,        VitaPatchAmpModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchEnvModule,        VitaPatchEnvModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchLfoModule,        VitaPatchLfoModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchRandomModule,     VitaPatchRandomModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchMacrosModule,     VitaPatchMacrosModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchModModule,        VitaPatchModModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchMonoVoiceModule,  VitaPatchMonoVoiceModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchMonoEnvModule,    VitaPatchMonoEnvModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchMonoLfoModule,    VitaPatchMonoLfoModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchMonoRandomModule, VitaPatchMonoRandomModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchChorusModule,     VitaPatchChorusModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchCompressorModule, VitaPatchCompressorModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchDelayModule,      VitaPatchDelayModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchDistortionModule, VitaPatchDistortionModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchEqModule,         VitaPatchEqModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchFilterFxModule,   VitaPatchFilterFxModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchFlangerModule,    VitaPatchFlangerModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchPhaserModule,     VitaPatchPhaserModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchReverbModule,     VitaPatchReverbModule::descriptor)
AQUANODE_REGISTER_MODULE (VitaPatchOutputModule,     VitaPatchOutputModule::descriptor)

//==============================================================================
// The patch builder: a Vita sound (the GUI card's, or a .vital preset's) as
// modules and cables
//==============================================================================
namespace aquanode::plugins::vita
{
    using namespace vitapatch;

    struct Ref { int module { -1 }; juce::String socket; };

    class PatchMaker
    {
    public:
        PatchMaker (AquanodeModularAudioProcessor& p, const Json& preset, juce::Point<int> origin)
            : b { p, origin }, data (preset), settings (preset.count ("settings") ? preset["settings"] : Json::object()) {}

        PatchPorts make()
        {
            namespace C = vt::constants;

            // Vital modules for as many voices as Vita plays, and a few for the releases
            const int polyphony = (int) value ("polyphony", 8.0f);
            lanes = polyphony <= 1 ? 3 : juce::jlimit (2, kMaxVoices, polyphony + 4);

            // --- the voice -------------------------------------------------
            voice = add ("plugin.vita.voice", 680, 0, voiceSpec (false), "", "");
            amp = add ("plugin.vita.amp", 1700, 0, ampSpec(), "", "");
            b.set (amp, "voices", juce::jlimit (1.0f, (float) kMaxVoices, value ("polyphony", 8.0f)));
            const int env1 = envModule (1, false);
            b.cable (env1, "out", amp, "envIn");

            for (int i = 0; i < vt::kNumOscillators; ++i)
            {
                if (value ("osc_" + std::to_string (i + 1) + "_on") < 0.5f)
                    continue;
                const std::string n = "osc_" + std::to_string (i + 1) + "_";
                osc[i] = add ("plugin.vita.osc", 1020, i * 800, oscSpec(), "osc_1_", n);
                Json custom;
                if (settings.count ("wavetables") && settings["wavetables"].size() > (size_t) i)
                    custom["wavetable"] = settings["wavetables"][(size_t) i];
                if (settings.count ("random_values") && settings["random_values"].size() > (size_t) i
                    && settings["random_values"][(size_t) i].count ("seed"))
                    custom["seed"] = settings["random_values"][(size_t) i]["seed"];
                if (settings.count ("custom_warps") && settings["custom_warps"].size() > (size_t) i)
                    custom["warp"] = settings["custom_warps"][(size_t) i];
                b.setCustomState (osc[i], juce::String (custom.dump()));
                b.set (osc[i], "number", (float) (i + 1));
                b.cable (voice, "pitchOut", osc[i], "pitchIn");
            }
            if (value ("sample_on") >= 0.5f)
            {
                sampler = add ("plugin.vita.sampler", 1020, 3 * 800, samplerSpec(), "", "");
                if (settings.count ("sample"))
                    b.setCustomState (sampler, juce::String (settings["sample"].dump()));
                b.cable (voice, "pitchOut", sampler, "pitchIn");
            }

            // the oscillators' FM / RM sources
            for (int i = 0; i < vt::kNumOscillators; ++i)
            {
                if (osc[i] < 0) continue;
                const int type = (int) value ("osc_" + std::to_string (i + 1) + "_distortion_type");
                const int first = vt::ProducersModule::getFirstModulationIndex (i);
                const int second = vt::ProducersModule::getSecondModulationIndex (i);
                if ((type == 7 || type == 10) && osc[first] >= 0) b.cable (osc[first], "rawOut", osc[i], "modAIn");
                if ((type == 8 || type == 11) && osc[second] >= 0) b.cable (osc[second], "rawOut", osc[i], "modBIn");
                if ((type == 9 || type == 12) && sampler >= 0) b.cable (sampler, "rawOut", osc[i], "sampleIn");
            }

            // --- the filters and where every source goes ----------------------
            bool filterOn[2];
            for (int f = 0; f < 2; ++f)
            {
                const std::string n = "filter_" + std::to_string (f + 1) + "_";
                filterOn[f] = value (n + "on") >= 0.5f;
                if (! filterOn[f]) continue;
                filter[f] = add ("plugin.vita.filter", 1360, f * 520, filterSpec(), "filter_1_", n);
                b.set (filter[f], "number", (float) (f + 1));
                b.set (filter[f], "type", (float) filterTypeIndex ((int) value (n + "model"), (int) value (n + "style")));
                b.cable (voice, "pitchOut", filter[f], "pitchIn");
            }

            auto route = [&] (int source, int destination)
            {
                const bool to1 = destination == C::kFilter1 || destination == C::kDualFilters;
                const bool to2 = destination == C::kFilter2 || destination == C::kDualFilters;
                if (destination == C::kEffects || (to1 && ! to2 && ! filterOn[0]) || (to2 && ! to1 && ! filterOn[1])
                    || (to1 && to2 && ! filterOn[0] && ! filterOn[1]))
                    b.cable (source, "audioOut", amp, "audioIn");
                if (to1 && filterOn[0]) b.cable (source, "audioOut", filter[0], "audioIn");
                if (to2 && filterOn[1]) b.cable (source, "audioOut", filter[1], "audioIn");
                if (destination == C::kDirectOut) b.cable (source, "audioOut", amp, "directIn");
            };
            for (int i = 0; i < vt::kNumOscillators; ++i)
                if (osc[i] >= 0)
                    route (osc[i], (int) value ("osc_" + std::to_string (i + 1) + "_destination"));
            if (sampler >= 0)
                route (sampler, (int) value ("sample_destination", 3.0f));

            const bool backward = filterOn[0] && value ("filter_1_filter_input") >= 0.5f;
            const bool forward = ! backward && filterOn[1] && value ("filter_2_filter_input") >= 0.5f;
            if (backward)      { b.cable (filter[1] >= 0 ? filter[1] : -1, "audioOut", filter[0], "audioIn"); b.cable (filter[0], "audioOut", amp, "audioIn"); }
            else if (forward)  { b.cable (filter[0] >= 0 ? filter[0] : -1, "audioOut", filter[1], "audioIn"); b.cable (filter[1], "audioOut", amp, "audioIn"); }
            else
                for (int f = 0; f < 2; ++f)
                    if (filter[f] >= 0) b.cable (filter[f], "audioOut", amp, "audioIn");

            // --- the effects, in the chain's order -------------------------------
            int order[C::kNumEffects];
            vt::utils::decodeFloatToOrder (order, value ("effect_chain_order"), C::kNumEffects);
            Ref last { amp, "audioOut" };
            int x = 2040;
            for (int k = 0; k < C::kNumEffects; ++k)
            {
                const int e = order[k];
                const std::string name = aqp_vita::strings::kEffectOrder[e];
                if (value (name + "_on") < 0.5f)
                    continue;
                const auto& s = effectSpec (e);
                const int m = add (s.d.typeId, x, 0, s, "", "");
                if (e == C::kFilterFx)
                    b.set (m, "type", (float) filterTypeIndex ((int) value ("filter_fx_model"), (int) value ("filter_fx_style")));
                effect[e] = m;
                b.cable (last.module, last.socket, m, "audioIn");
                last = { m, "audioOut" };
                x += 340;
            }
            output = add ("plugin.vita.output", x, 0, outputSpec(), "", "");
            b.cable (last.module, last.socket, output, "audioIn");
            b.cable (amp, "directOut", output, "audioIn");

            // --- the modulation matrix ---------------------------------------------
            modulations();

            PatchPorts ports;
            ports.name = data.count ("preset_name") && data["preset_name"].is_string() && ! data["preset_name"].get<std::string>().empty()
                             ? "Vita - " + juce::String (data["preset_name"].get<std::string>()) : juce::String ("Vita");
            ports.audioOut = { { output, "audioOut" } };
            for (int m : monoModules)
                ports.midiIn.push_back ({ m, "midiIn" });
            return ports;
        }

    private:
        float value (const std::string& name, float fallback = 0.0f) const
        {
            if (settings.count (name) && settings[name].is_number())
                return settings[name].get<float>();
            if (vt::Parameters::isParameter (name))
                return vt::Parameters::getDetails (name).default_value;
            return fallback;
        }

        // a module, its Vital knobs set from the preset (the spec's control
        // names start with `from`, the preset's with `to`)
        int add (const juce::String& typeId, int dx, int dy, const Spec& s, const std::string& from, const std::string& to)
        {
            const int m = b.add (typeId, dx, dy);
            if (m < 0)
                return m;
            for (size_t k = 0; k < s.knobs.size(); ++k)
            {
                const auto& def = s.knobs[k];
                if (def.control.empty())
                    continue;
                std::string name = def.control;
                if (! from.empty() && name.rfind (from, 0) == 0)
                    name = to + name.substr (from.size());
                b.set (m, s.d.params[k].id, value (name) - def.offset);
            }
            specOf[m] = &s;
            if (paramIndexIn (s, "lanes") >= 0)
                b.set (m, "lanes", (float) lanes);
            return m;
        }

        int envModule (int n, bool mono)
        {
            auto& slot = (mono ? monoEnv : env)[n - 1];
            if (slot < 0)
            {
                slot = add (mono ? "plugin.vita.monoenv" : "plugin.vita.env", mono ? 2040 + (n - 1) * 340 : 680, mono ? 1300 : 640 + (n - 1) * 360,
                            envSpec (mono), "env_1_", "env_" + std::to_string (n) + "_");
                b.set (slot, "number", (float) n);
                if (mono) monoModules.push_back (slot);
            }
            return slot;
        }

        int lfoModule (int n, bool mono)
        {
            auto& slot = (mono ? monoLfo : lfo)[n - 1];
            if (slot < 0)
            {
                slot = add (mono ? "plugin.vita.monolfo" : "plugin.vita.lfo", mono ? 2040 + (n - 1) * 340 : 340, mono ? 1700 : (n - 1) * 460,
                            lfoSpec (mono), "lfo_1_", "lfo_" + std::to_string (n) + "_");
                b.set (slot, "number", (float) n);
                if (settings.count ("lfos") && settings["lfos"].size() >= (size_t) n)
                    b.setCustomState (slot, juce::String (settings["lfos"][(size_t) n - 1].dump()));
                if (mono) monoModules.push_back (slot);
                else      b.cable (voice, "pitchOut", slot, "pitchIn");
            }
            return slot;
        }

        int randomModule (int n, bool mono)
        {
            auto& slot = (mono ? monoRandom : random)[n - 1];
            if (slot < 0)
            {
                slot = add (mono ? "plugin.vita.monorandom" : "plugin.vita.random", mono ? 2040 + (n - 1) * 340 : 0, mono ? 2200 : (n - 1) * 300,
                            randomSpec (mono), "random_1_", "random_" + std::to_string (n) + "_");
                b.set (slot, "number", (float) n);
                if (mono) monoModules.push_back (slot);
                else      b.cable (voice, "pitchOut", slot, "pitchIn");
            }
            return slot;
        }

        int voiceModule (bool mono)
        {
            if (! mono)
                return voice;
            if (monoVoice < 0)
            {
                monoVoice = add ("plugin.vita.monovoice", 1700, 1300, voiceSpec (true), "", "");
                monoModules.push_back (monoVoice);
            }
            return monoVoice;
        }

        int macros()
        {
            if (macroModule < 0)
                macroModule = add ("plugin.vita.macros", 0, 1300, macroSpec(), "", "");
            return macroModule;
        }

        // a modulation source, as the copy that follows the newest note when it modulates something global
        Ref source (const std::string& name, bool global)
        {
            auto numbered = [&name] (const char* prefix, int count) -> int
            {
                const std::string p = prefix;
                if (name.rfind (p, 0) != 0) return -1;
                const auto rest = name.substr (p.size());
                if (rest.empty() || rest.find_first_not_of ("0123456789") != std::string::npos) return -1;
                const int n = std::stoi (rest);
                return n >= 1 && n <= count ? n : -1;
            };
            if (const int n = numbered ("env_", vt::kNumEnvelopes); n > 0)       return { envModule (n, global), "out" };
            if (const int n = numbered ("lfo_", vt::kNumLfos); n > 0)            return { lfoModule (n, global), "out" };
            if (const int n = numbered ("random_", vt::kNumRandomLfos); n > 0)   return { randomModule (n, global), "out" };
            if (const int n = numbered ("macro_control_", vt::kNumMacros); n > 0) return { macros(), "macro" + juce::String (n) };
            static const std::map<std::string, const char*> voiceSockets {
                { "velocity", "velocity" }, { "note", "note" }, { "note_in_octave", "noteInOctave" },
                { "aftertouch", "aftertouch" }, { "mod_wheel", "modWheel" }, { "pitch_wheel", "pitchWheel" },
                { "random", "random" }, { "lift", "lift" }, { "slide", "slide" } };
            if (auto it = voiceSockets.find (name); it != voiceSockets.end())
                return { voiceModule (global), it->second };
            return {};
        }

        struct Dest { int module { -1 }; juce::String param; bool global { false }; };

        Dest destination (const std::string& name)
        {
            auto knobOf = [this] (int m, const std::string& id) -> juce::String
            {
                if (m < 0 || specOf.count (m) == 0) return {};
                return paramIndexIn (*specOf[m], id) >= 0 ? juce::String (id) : juce::String();
            };
            auto starts = [&name] (const std::string& p) { return name.rfind (p, 0) == 0; };

            for (int i = 0; i < vt::kNumOscillators; ++i)
                if (const std::string p = "osc_" + std::to_string (i + 1) + "_"; starts (p))
                    return { osc[i], knobOf (osc[i], name.substr (p.size())) };
            for (int f = 0; f < 2; ++f)
                if (const std::string p = "filter_" + std::to_string (f + 1) + "_"; starts (p))
                    return { filter[f], knobOf (filter[f], name.substr (p.size())) };
            if (starts ("sample_"))
                return { sampler, knobOf (sampler, name.substr (7)) };
            for (int n = 1; n <= vt::kNumEnvelopes; ++n)
                if (const std::string p = "env_" + std::to_string (n) + "_"; starts (p) && env[n - 1] >= 0)
                    return { env[n - 1], knobOf (env[n - 1], name.substr (p.size())) };
            for (int n = 1; n <= vt::kNumLfos; ++n)
                if (const std::string p = "lfo_" + std::to_string (n) + "_"; starts (p) && lfo[n - 1] >= 0)
                    return { lfo[n - 1], knobOf (lfo[n - 1], name.substr (p.size())) };
            for (int n = 1; n <= vt::kNumRandomLfos; ++n)
                if (const std::string p = "random_" + std::to_string (n) + "_"; starts (p) && random[n - 1] >= 0)
                    return { random[n - 1], knobOf (random[n - 1], name.substr (p.size())) };
            for (int e = 0; e < vt::constants::kNumEffects; ++e)
                if (const std::string p = std::string (aqp_vita::strings::kEffectOrder[e]) + "_"; starts (p))
                    return { effect[e], knobOf (effect[e], name.substr (p.size())), true };
            for (int n = 1; n <= vt::kNumMacros; ++n)
                if (name == "macro_control_" + std::to_string (n))
                    return { macroModule, macroModule >= 0 ? "macro" + juce::String (n) : juce::String(), true };
            if (name == "volume" || name == "stereo_routing")
                return { output, knobOf (output, name), true };
            if (name == "velocity_track" || name == "voice_amplitude")
                return { amp, knobOf (amp, name) };
            if (starts ("portamento_") || name == "pitch_bend_range" || name == "voice_tune" || name == "voice_transpose")
                return { voice, knobOf (voice, name) };
            return {};
        }

        void modulations()
        {
            if (! settings.count ("modulations") || ! settings["modulations"].is_array())
                return;
            const auto& list = settings["modulations"];
            const int count = (int) list.size();

            struct Connection { std::string source, destination; Json map; float amount, power; bool bipolar, bypass; };
            std::vector<Connection> c ((size_t) count);
            for (int i = 0; i < count; ++i)
            {
                const auto& m = list[(size_t) i];
                const std::string n = "modulation_" + std::to_string (i + 1) + "_";
                auto& k = c[(size_t) i];
                k.source = m.value ("source", std::string());
                k.destination = m.value ("destination", std::string());
                if (m.count ("line_mapping")) k.map = m["line_mapping"];
                k.amount = value (n + "amount");
                k.power = value (n + "power");
                k.bipolar = value (n + "bipolar") >= 0.5f;
                k.bypass = value (n + "bypass") >= 0.5f;
            }

            auto isModTarget = [&] (int i)
            {
                const std::string amount = "modulation_" + std::to_string (i + 1) + "_amount";
                const std::string power = "modulation_" + std::to_string (i + 1) + "_power";
                for (auto& k : c)
                    if (! k.source.empty() && ! k.bypass && (k.destination == amount || k.destination == power))
                        return true;
                return false;
            };

            // the connections that need a Mod module, and where each one ends
            std::vector<int> modModule ((size_t) count, -1);
            std::vector<Dest> dest ((size_t) count);
            int modX = 0;
            for (int i = 0; i < count; ++i)
            {
                auto& k = c[(size_t) i];
                if (k.source.empty() || k.destination.empty() || k.bypass
                    || k.destination.rfind ("modulation_", 0) == 0)
                    continue;
                dest[(size_t) i] = destination (k.destination);
                if (dest[(size_t) i].module < 0 || dest[(size_t) i].param.isEmpty())
                    continue;
                const bool plain = ! k.bipolar && std::abs (k.power) < 0.005f && ! k.map.is_object() && ! isModTarget (i);
                if (plain)
                    continue;
                const int m = b.add ("plugin.vita.mod", modX, 2700);
                modX += 300;
                b.set (m, "amount", k.amount);
                b.set (m, "power", k.power);
                b.set (m, "bipolar", k.bipolar ? 1.0f : 0.0f);
                if (k.map.is_object())
                    b.setCustomState (m, juce::String (k.map.dump()));
                modModule[(size_t) i] = m;
            }

            for (int i = 0; i < count; ++i)
            {
                auto& k = c[(size_t) i];
                if (k.source.empty() || k.destination.empty() || k.bypass)
                    continue;

                Dest d;
                if (k.destination.rfind ("modulation_", 0) == 0)
                {
                    // a modulation of another connection's amount or power
                    const auto rest = k.destination.substr (11);
                    const auto underscore = rest.find ('_');
                    if (underscore == std::string::npos) continue;
                    const int j = std::atoi (rest.substr (0, underscore).c_str()) - 1;
                    const auto what = rest.substr (underscore + 1);
                    if (j < 0 || j >= count || modModule[(size_t) j] < 0 || (what != "amount" && what != "power"))
                        continue;
                    d = { modModule[(size_t) j], juce::String (what), dest[(size_t) j].global };
                }
                else
                    d = dest[(size_t) i];
                if (d.module < 0 || d.param.isEmpty())
                    continue;

                const Ref src = source (k.source, d.global);
                if (src.module < 0)
                    continue;
                if (modModule[(size_t) i] >= 0)
                {
                    b.cable (src.module, src.socket, modModule[(size_t) i], "in");
                    b.mod (modModule[(size_t) i], "out", d.module, d.param, 1.0f);
                }
                else
                    b.mod (src.module, src.socket, d.module, d.param, k.amount);
            }
        }

        kit::Builder b;
        const Json& data;
        const Json settings;
        std::map<int, const Spec*> specOf;
        int lanes { 8 };
        int voice { -1 }, amp { -1 }, sampler { -1 }, output { -1 }, monoVoice { -1 }, macroModule { -1 };
        int osc[vt::kNumOscillators] { -1, -1, -1 };
        int filter[2] { -1, -1 };
        int effect[vt::constants::kNumEffects] { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
        std::array<int, vt::kNumEnvelopes> env = filled(), monoEnv = filled();
        std::array<int, vt::kNumLfos> lfo = filledL(), monoLfo = filledL();
        std::array<int, vt::kNumRandomLfos> random = filledR(), monoRandom = filledR();
        std::vector<int> monoModules;

        static std::array<int, vt::kNumEnvelopes> filled() { std::array<int, vt::kNumEnvelopes> a; a.fill (-1); return a; }
        static std::array<int, vt::kNumLfos> filledL() { std::array<int, vt::kNumLfos> a; a.fill (-1); return a; }
        static std::array<int, vt::kNumRandomLfos> filledR() { std::array<int, vt::kNumRandomLfos> a; a.fill (-1); return a; }
    };

    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original, juce::Point<int> origin)
    {
        auto* synth = dynamic_cast<aqp_vita::SynthPlugin*> (&original);
        if (synth == nullptr)
            return {};
        const Json preset = synth->saveToJson();
        return PatchMaker (patch, preset, origin).make();
    }

    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* synth = dynamic_cast<aqp_vita::SynthPlugin*> (&original);
        std::string error;
        return synth != nullptr && synth->loadFromFile (file, error);
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
