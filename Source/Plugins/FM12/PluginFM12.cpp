// FM12, compiled into Aquanode Modular: the plugin's sources (Aquanode
// Modular's own copy, in Source/PluginSources/fm12) inside namespace
// aqp_fm12 (see PluginPrelude.h), and its GUI module.
#include "PluginFM12Config.h"

namespace aqp_fm12
{
    #include "../../PluginSources/fm12/PluginProcessor.cpp"
    #include "../../PluginSources/fm12/PluginEditor.cpp"
}

#include "../Host/PluginCatalog.h"

AQP_DEFINE_PLUGIN (FM12GuiModule, "fm12", aqp_fm12)

//==============================================================================
// the patch version: the plugin whole, one module (Host/WholePluginPatch.h)
//==============================================================================
#include "../Host/WholePluginPatch.h"

namespace
{
    const aquanode::plugins::whole::Def& FM12WholeModuleDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            aquanode::plugins::whole::Def x;
            x.pluginId = "fm12";
            x.kind = "Synth";
            x.description = "FM12 whole, as one module: twelve operators with their envelopes, ratios, levels, delays and feedback, the engine modes, the LFO, the pitch envelope and the chorus, played by the plugin's own engine. Its knobs are the plugin's parameters and take cables (the 12 x 12 routing comes with the sound and is kept, not shown). Built from its default sound, a GUI card or an .fm12preset.";
            x.section = aquanode::ModuleSection::Oscillator;
            x.midi = true;
            x.audioIn = false;
            x.sideIn = false;
            x.hide = [] (const juce::String& id) { return id.startsWith ("route_"); };
            return x;
        }();
        return def;
    }

    bool FM12WholeModuleLoad (juce::AudioProcessor& o, const juce::File& f)
    {
        juce::MemoryBlock mb;
        if (f.hasFileExtension ("syx") || ! f.loadFileAsData (mb) || mb.getSize() == 0) return false;   // (a .syx: in the GUI card's DX7 menu)
        o.setStateInformation (mb.getData(), (int) mb.getSize());
        return true;
    }
}

//==============================================================================
// (6.9.14) the patch version in smaller cards: FM12 - Routing (the engine modes and
// the 12 x 12 routing, drawn as a grid) and twelve operator cards, chained into
// FM12 - Engine (master volume, LFO, pitch envelope, chorus). The plugin's own
// engine plays it all.
namespace
{
    using aquanode::plugins::kit::CardDef;

    // the routing grid: a row per operator that modulates, a column per operator it reaches
    class RouteGrid final : public juce::Component, private juce::Timer
    {
    public:
        explicit RouteGrid (aquanode::SynthModule& m) : module (m) { startTimerHz (5); }
        void paint (juce::Graphics& g) override
        {
            const auto a = area();
            const float c = cell();
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            for (int i = 0; i < 12; ++i)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.drawText (juce::String (i + 1), juce::Rectangle<float> (a.getX() - 20.0f, a.getY() + c * (float) i, 18.0f, c), juce::Justification::centredRight);
                g.drawText (juce::String (i + 1), juce::Rectangle<float> (a.getX() + c * (float) i, a.getY() - 14.0f, c, 13.0f), juce::Justification::centred);
            }
            for (int f = 0; f < 12; ++f)
                for (int t = 0; t < 12; ++t)
                {
                    const auto r = juce::Rectangle<float> (a.getX() + c * (float) t, a.getY() + c * (float) f, c, c).reduced (1.5f);
                    if (f == t) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRect (r); continue; }
                    const bool on = module.getParameter (id (f, t)) > 0.5f;
                    g.setColour (on ? juce::Colour (0xff38b6d8) : juce::Colours::white.withAlpha (0.13f));
                    g.fillRoundedRectangle (r, 2.0f);
                }
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText ("from (rows) > to (columns)", getLocalBounds().removeFromBottom (14).toFloat(), juce::Justification::centred);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto a = area();
            const float c = cell();
            const int t = (int) std::floor ((e.position.x - a.getX()) / c), f = (int) std::floor ((e.position.y - a.getY()) / c);
            if (f < 0 || f >= 12 || t < 0 || t >= 12 || f == t) return;
            module.setParameter (id (f, t), module.getParameter (id (f, t)) > 0.5f ? 0.0f : 1.0f);
            repaint();
        }
        static juce::String id (int f, int t) { return "route_" + juce::String (f) + "_" + juce::String (t); }
    private:
        float cell() const { return juce::jmin ((float) (getWidth() - 30) / 12.0f, (float) (getHeight() - 32) / 12.0f); }
        juce::Rectangle<float> area() const
        {
            const float c = cell();
            return { (float) getWidth() * 0.5f - c * 6.0f + 10.0f, 16.0f, c * 12.0f, c * 12.0f };
        }
        void timerCallback() override { repaint(); }
        aquanode::SynthModule& module;
    };

    juce::String opLabel (juce::String label)
    {
        // "OP3 Attack" -> "Attack", "Cancel 4" -> "Cancel", "OSC 4" -> "Wave", "Feedback 3" -> "Feedback"
        if (label.startsWith ("OP") && label.containsChar (' ')) label = label.fromFirstOccurrenceOf (" ", false, false);
        for (auto w : { "Cancel", "Delay", "Feedback" })
            if (label.startsWith (w)) return w;
        if (label.startsWith ("OSC")) return "Wave";
        return label;
    }

    const std::vector<CardDef>& FM12Cards()
    {
        static const std::vector<CardDef> cards = []
        {
            std::vector<CardDef> v;
            CardDef r;
            r.pluginId = "fm12"; r.id = "routing"; r.kind = "Routing";
            r.description = "FM12's engine modes (phase, linear or exponential FM, the feedback's curve, DX7 mode, the "
                            "Nyquist limit) and its 12 x 12 routing: tap a square to send the row's operator into the "
                            "column's. Link Out goes on to the operator cards, the last one into FM12 - Engine.";
            r.ids = { "fmEngineMode", "feedbackModeExp", "", "dx7Mode", "nyquistLimit", "" };
            for (int f = 0; f < 12; ++f)
                for (int t = 0; t < 12; ++t)
                    if (f != t) r.ids.push_back (RouteGrid::id (f, t));
            r.spec = [] (const juce::String& pid) { return aquanode::plugins::whole::pluginSpec ("fm12", pid); };
            r.extra = [] (aquanode::SynthModule& m) { return std::make_unique<RouteGrid> (m); };
            r.extraHeight = 230;
            r.width = 280;
            r.drawn = [] (const juce::String& id) { return id.startsWith ("route_"); };
            v.push_back (std::move (r));
            for (int op = 0; op < 12; ++op)
            {
                CardDef c;
                const auto p = "op" + juce::String (op) + "_";
                c.pluginId = "fm12"; c.id = "op" + juce::String (op + 1); c.kind = "Operator " + juce::String (op + 1);
                c.description = "One of FM12's twelve operators: its envelope (attack, decay, sustain, release), level, "
                                "ratio, onset delay, wave, feedback and Cancel (only its FM sidebands). Where it goes: FM12 - Routing.";
                c.ids = { p + "attack", p + "decay", p + "sustain", p + "release", "", p + "level", p + "ratio", p + "delay",
                          "feedback_" + juce::String (op), "", p + "osc", p + "cancel" };
                c.spec = [] (const juce::String& pid)
                {
                    auto spec = aquanode::plugins::whole::pluginSpec ("fm12", pid);
                    spec.label = opLabel (spec.label);
                    return spec;
                };
                v.push_back (std::move (c));
            }
            return v;
        }();
        return cards;
    }

    bool onFM12Card (const juce::String& id)
    {
        if (id.startsWith ("route_")) return true;
        for (const auto& c : FM12Cards())
            for (const auto& i : c.ids)
                if (i == id) return true;
        return false;
    }

    const aquanode::plugins::whole::Def& FM12EngineDef()
    {
        static const aquanode::plugins::whole::Def def = []
        {
            auto x = FM12WholeModuleDef();
            x.suffix = "engine";
            x.kind = "Engine";
            x.cardsIn = true;
            x.description = "FM12's engine, played by the plugin itself: master volume, the LFO (wave, rate, delay, "
                            "pitch, wheel, amp and its target, key sync), the pitch envelope and the chorus. Its "
                            "routing and its twelve operators are cards of their own, chained into Cards In; "
                            "without them it plays as they were last set.";
            x.hide = [] (const juce::String& id) { return onFM12Card (id); };
            return x;
        }();
        return def;
    }
}

extern "C" AQUANODE_USED_ATTR bool FM12Cards_aqRegistered =
    aquanode::plugins::whole::registerWithCards (FM12WholeModuleDef(), FM12EngineDef(), FM12Cards(), FM12WholeModuleLoad, 4);
AQUANODE_KEEP_SYMBOL (FM12Cards_aqRegistered)

//==============================================================================
// (6.9.17) FM12's second patch version: Two Operators - a modulator's Audio Out into a
// carrier's FM In, two FM12 - Operator modules with FM12's engine mode and Nyquist Limit,
// the carrier as FM12's operator 1, the modulator as the first operator routed into it
// (else operator 2). FM In takes any audio: effects can go between them, more
// operators can be added. The Patch pills ask which version (Fixed Architecture: the
// engine and its cards, as before).
//==============================================================================
#include "../../Engine/PluginProcessor.h"

namespace
{
    float fm12Value (juce::AudioProcessor& p, const juce::String& id, float fallback)
    {
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm); r != nullptr && r->getParameterID() == id)
                return r->convertFrom0to1 (r->getValue());
        return fallback;
    }

    aquanode::plugins::PatchPorts buildFM12TwoOperators (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                                                          juce::Point<int> origin)
    {
        const float mode = std::round (fm12Value (original, "fmEngineMode", 0.0f));
        const float nyquist = fm12Value (original, "nyquistLimit", 24000.0f);
        int modOp = 1;
        bool routed = false;
        for (int from = 1; from < 12 && ! routed; ++from)
            if (fm12Value (original, "route_" + juce::String (from) + "_0", 0.0f) > 0.5f) { modOp = from; routed = true; }

        const int modulator = patch.addModule ("osc.fm12op", origin);
        const int carrier = patch.addModule ("osc.fm12op", origin + juce::Point<int> (380, 0));
        auto setUp = [&] (int module, int op, bool isCarrier)
        {
            auto* inst = patch.getInstance (module);
            if (inst == nullptr) return;
            auto set = [&] (const char* id, float v) { inst->dsp->setParameter (id, v); };
            const juce::String p = "op" + juce::String (op) + "_";
            set ("mode", mode);
            set ("nyquist", juce::jlimit (100.0f, 24000.0f, nyquist));
            const int osc = (int) std::round (fm12Value (original, p + "osc", 0.0f));
            set ("wave", osc <= 3 ? (float) osc : 0.0f);   // (FM12's drawn waves: a sine here)
            set ("ratio", fm12Value (original, p + "ratio", 1.0f));
            set ("attack", juce::jmax (0.001f, fm12Value (original, p + "attack", 0.05f)));
            set ("decay", juce::jmax (0.001f, fm12Value (original, p + "decay", 0.1f)));
            set ("sustain", fm12Value (original, p + "sustain", 0.8f));
            set ("release", juce::jmax (0.001f, fm12Value (original, p + "release", 0.5f)));
            set ("feedback", fm12Value (original, "feedback_" + juce::String (op), 0.0f));
            const float level = fm12Value (original, p + "level", isCarrier ? 1.0f : 0.0f);
            // (a modulator FM12 does not route yet: a moderate one, so the pair is heard as FM)
            set ("level", juce::jlimit (0.0001f, 1.0f, isCarrier || (routed && level > 0.0001f) ? level : 0.02f));
            set ("velSens", isCarrier ? 1.0f : 0.0f);   // (FM12: velocity sets the carriers)
        };
        setUp (modulator, modOp, false);
        setUp (carrier, 0, true);
        // Linear FM swings by the modulator's frequency in FM12: FM Depth at its ratio does that
        if (mode == 1.0f || mode == 2.0f)
            if (auto* c = patch.getInstance (carrier))
                c->dsp->setParameter ("depth", juce::jlimit (0.0f, 20.0f, fm12Value (original, "op" + juce::String (modOp) + "_ratio", 1.0f)));
        patch.addCable (modulator, "audioOut", carrier, "fmIn");

        aquanode::plugins::PatchPorts ports;
        ports.name = "FM12 - Two Operators";
        ports.audioOut = { { carrier, "audioOut" } };
        ports.midiIn = { { modulator, "addMidiIn" }, { carrier, "addMidiIn" } };
        return ports;
    }
}

extern "C" AQUANODE_USED_ATTR bool FM12TwoOperators_aqRegistered =
    aquanode::plugins::registerPatchKind ("fm12", "Fixed Architecture", "Two Operators", buildFM12TwoOperators);
AQUANODE_KEEP_SYMBOL (FM12TwoOperators_aqRegistered)
