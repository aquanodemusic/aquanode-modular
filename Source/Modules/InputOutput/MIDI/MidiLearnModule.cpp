#include "MidiLearnModule.h"

using namespace aquanode;

//==============================================================================
juce::String MidiLearnModule::sourceName (int source, bool shortForm)
{
    if (source <= 0 || source >= kNumSources)
        return shortForm ? juce::String() : juce::String ("-");
    if (source == 129) return shortForm ? "Bend" : "Pitch Bend";
    if (source == 130) return "Pressure";
    const int cc = source - 1;
    juce::String s ("CC " + juce::String (cc));
    if (shortForm)
        return s;
    switch (cc)
    {
        case 1:  return s + " Mod Wheel";
        case 2:  return s + " Breath";
        case 7:  return s + " Volume";
        case 10: return s + " Pan";
        case 11: return s + " Expression";
        case 64: return s + " Sustain";
        case 74: return s + " Slide";
        default: return s;
    }
}

float MidiLearnModule::controlValue (int source) const
{
    if (midiControls == nullptr || source <= 0 || source >= kNumSources)
        return -1.0f;
    if (source == 129) return 0.5f * (midiControls->bend + 1.0f);
    if (source == 130) return midiControls->aftertouch;
    return midiControls->cc[source - 1];
}

void MidiLearnModule::processSample (const StereoFrame*, StereoFrame* outputs)
{
    int source = (int) std::lround (getParameterBase (pSource));
    const bool opening = ! started;

    if (! started)
    {
        // a patch just opened: the control's last position (saved) drives, if it ever moved
        started = true;
        lastSource = source;
        driving = source > 0 && getParameterBase (pActive) > 0.5f;
        lastSeen = controlValue (source);
    }

    // Learn: the next control that moves after the tap
    if (midiControls != nullptr)
    {
        const int state = learnState.load (std::memory_order_acquire);
        if (state == 1)
        {
            baseline = midiControls->controlCount;
            int expected = 1;
            learnState.compare_exchange_strong (expected, 2);
        }
        else if (state == 2 && midiControls->controlCount != baseline)
        {
            const int c = midiControls->lastControl;
            const int heard = c >= 0 && c < 128 ? c + 1 : c == 128 ? 129 : c == 129 ? 130 : 0;
            if (heard > 0)
            {
                source = lastSource = heard;
                setParameterByIndex (pSource, (float) heard);
                lastSeen = controlValue (heard);
                setParameterByIndex (pValue, lastSeen);
                setParameterByIndex (pActive, 1.0f);
                driving = true;
                int expected = 2;
                learnState.compare_exchange_strong (expected, 0);
                learned.fetch_add (1, std::memory_order_release);
            }
            else
            {
                baseline = midiControls->controlCount;
            }
        }
    }

    if (source != lastSource)
    {
        // another control picked in the menu: it drives once it moves
        lastSource = source;
        driving = false;
        setParameterByIndex (pActive, 0.0f);
        lastSeen = controlValue (source);
    }

    // the control moving: its position, kept (and saved) as the value
    if (source > 0)
    {
        const float now = controlValue (source);
        if (now >= 0.0f && now != lastSeen)
        {
            lastSeen = now;
            setParameterByIndex (pValue, now);
            if (! driving)
            {
                driving = true;
                setParameterByIndex (pActive, 1.0f);
            }
        }
    }

    const float v = juce::jlimit (0.0f, 1.0f, getParameterBase (pValue));
    const float low = juce::jlimit (0.0f, 1.0f, param (pLow) * 0.01f), high = juce::jlimit (0.0f, 1.0f, param (pHigh) * 0.01f);
    const float target = low + (high - low) * v;

    const float ms = juce::jmax (0.0f, param (pSmooth));
    const float coeff = ms < 0.05f ? 1.0f : 1.0f - std::exp ((float) (-1.0 / (ms * 0.001 * sampleRate)));
    if (! driving || opening)
        smoothed = target;   // (nothing to glide from: the knobs were their own, or the patch just opened)
    smoothed += coeff * (target - smoothed);
    outputs[0] = { smoothed, smoothed };

    uiValue.store (v, std::memory_order_relaxed);
    uiActive.store (driving, std::memory_order_relaxed);
}

void MidiLearnModule::toggleLearning()
{
    // a second tap while listening gives up
    learnState.store (learnState.load (std::memory_order_relaxed) != 0 ? 0 : 1, std::memory_order_release);
}

void MidiLearnModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "learn")
        toggleLearning();
}

juce::String MidiLearnModule::titleSuffix() const
{
    const auto s = sourceName ((int) std::lround (getParameter ("source")), true);
    return s.isNotEmpty() ? " " + s : juce::String();
}

//==============================================================================
// the little screen: what it listens to and where the control is - or, while
// learning, the call to move one. A tap on it learns too.
namespace
{
    class MidiLearnDisplay final : public juce::Component,
                                   public ExtraContentParamListener,
                                   private juce::Timer
    {
    public:
        explicit MidiLearnDisplay (MidiLearnModule& m) : module (&m)
        {
            seenLearns = m.learnCount();
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            startTimerHz (20);
        }

        void paint (juce::Graphics& g) override
        {
            auto* m = dynamic_cast<MidiLearnModule*> (module.get());
            if (m == nullptr)
                return;
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (r, 5.0f);
            const auto cyan = juce::Colour (0xff3ce4ff);
            auto in = r.reduced (8.0f, 4.0f);

            if (m->isLearning())
            {
                // listening: pulsing, so it reads as waiting
                const float pulse = 0.55f + 0.45f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f);
                g.setColour (cyan.withAlpha (0.25f + 0.25f * pulse));
                g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f);
                g.setColour (cyan.withAlpha (0.6f + 0.4f * pulse));
                g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
                g.drawFittedText ("Move a knob, fader, wheel or pedal...", in.toNearestInt(), juce::Justification::centred, 1, 0.8f);
                return;
            }

            const int source = (int) std::lround (m->getParameter ("source"));
            if (source <= 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.setFont (juce::Font (juce::FontOptions (12.5f)));
                g.drawFittedText ("Tap Learn, then move a control", in.toNearestInt(), juce::Justification::centred, 1, 0.8f);
                return;
            }

            // the control's name, its position as a bar and a number
            const bool active = m->shownActive();
            const float v = m->shownValue();
            auto name = in.removeFromLeft (in.getWidth() * 0.42f);
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawFittedText (MidiLearnModule::sourceName (source), name.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
            auto number = in.removeFromRight (38.0f);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.setColour (active ? cyan : juce::Colours::white.withAlpha (0.45f));
            g.drawText (active ? juce::String ((int) std::lround (v * 127.0f)) : juce::String ("-"), number, juce::Justification::centredRight, false);
            auto bar = in.reduced (4.0f, 0.0f).withSizeKeepingCentre (in.getWidth() - 8.0f, 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.fillRoundedRectangle (bar, 4.0f);
            if (active)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0a7cff), bar.getX(), 0.0f, cyan, bar.getRight(), 0.0f, false));
                g.fillRoundedRectangle (bar.withWidth (juce::jmax (bar.getHeight(), bar.getWidth() * v)), 4.0f);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.setFont (juce::Font (juce::FontOptions (10.5f)));
                g.drawFittedText ("waiting for it to move", bar.expanded (0.0f, 6.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked())
                if (auto* m = dynamic_cast<MidiLearnModule*> (module.get()))
                {
                    m->toggleLearning();
                    repaint();
                }
        }

    private:
        void timerCallback() override
        {
            auto* m = dynamic_cast<MidiLearnModule*> (module.get());
            if (m == nullptr)
                return;
            const int n = m->learnCount();
            if (n != seenLearns)
            {
                // a control was learned on the audio thread: the menu and the title follow
                seenLearns = n;
                if (paramValuesChangedByContent)
                    paramValuesChangedByContent();
                if (auto* card = getParentComponent())
                    card->repaint();
            }
            repaint();
        }

        juce::WeakReference<SynthModule> module;
        int seenLearns { 0 };
    };
}

std::unique_ptr<juce::Component> MidiLearnModule::createExtraContentComponent()
{
    return std::make_unique<MidiLearnDisplay> (*this);
}

//==============================================================================
static ModuleDescriptor midiLearnDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "io.midilearn";
    d.displayName = "Midi Learn";
    d.description =
        "A knob, fader, wheel or pedal of your MIDI controller as a cable into any knob: tap Learn and move "
        "the control. Like a Macro, each knob it is patched into follows the control across its whole range "
        "(the cable's depth blends with the knob's own setting, negative turns it round); Low and High narrow "
        "the sweep. Nothing moves until the control does, and its last position is saved with the patch. "
        "One Midi Learn per control - with a keyboard on an Android phone, too.";
    d.section = ModuleSection::InputOutput;
    d.sidebarOrder = 7;
    d.sockets = {
        unipolarOut ("modOut", "Mod Out")
    };
    juce::StringArray sources;
    for (int i = 0; i < MidiLearnModule::kNumSources; ++i)
        sources.add (MidiLearnModule::sourceName (i));
    d.params = {
        makeButton ("learn",  "Learn", 0, 1),
        makeCombo  ("source", "Control", sources, 0, 0, 2).noMod(),
        makeRotary ("smooth", "Smooth", 0.0f, 200.0f, 5.0f, 1, "ms"),
        makeRotary ("low",    "Low",    0.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("high",   "High",   0.0f, 100.0f, 100.0f, 1, "%"),
        // where the control was left, and whether it has moved: saved, not shown
        makeRotary ("value",  "Value",  0.0f, 1.0f, 0.0f, 9).hide().noMod(),
        makeRotary ("active", "Active", 0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f).hide().noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (MidiLearnModule, midiLearnDescriptor)
