#include "WavetableModule.h"

using namespace aquanode;

void WavetableModule::blockStart()
{
    latchedSample = getLoadedSample();
    if (latchedSample != nullptr && latchedSample->getNumSamples() > 0)
    {
        tableData = latchedSample->getReadPointer (0);
        tableLen = latchedSample->getNumSamples();
    }
    else
    {
        tableData = nullptr;
        tableLen = 0;
    }
}

// one single-cycle frame, linearly interpolated
float WavetableModule::readFrame (int frame, double phase01) const
{
    static const int sizes[4] = { 256, 512, 1024, 2048 };
    const int frameSize = sizes[juce::jlimit (0, 3, (int) param (pFrameSize))];

    const int base = frame * frameSize;
    const double pos = phase01 * frameSize;
    const int i0 = (int) pos;
    const int i1 = (i0 + 1) % frameSize;
    const float frac = (float) (pos - i0);

    const int a = base + i0;
    const int b = base + i1;
    const float s0 = a < tableLen ? tableData[a] : 0.0f;
    const float s1 = b < tableLen ? tableData[b] : 0.0f;
    return s0 + (s1 - s0) * frac;
}

void WavetableModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void WavetableModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double freq = midiNoteToHz ((double) (glide.next (v, glideMillis(), isMonoVoice(),
                                                           ! pool.isMuted (v), sampleRate) + pitchBendSemitones()));

    phase[v] += freq / sampleRate;
    phase[v] -= std::floor (phase[v]);

    if (tableData == nullptr || tableLen <= 0)
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    static const int sizes[4] = { 256, 512, 1024, 2048 };
    const int frameSize = sizes[juce::jlimit (0, 3, (int) param (pFrameSize))];
    const int numFrames = juce::jmax (1, tableLen / frameSize);

    // read phase, optionally warped inside the frame
    double rp = phase[v] + (double) inputs[0][0];      // FM In
    rp -= std::floor (rp);

    const float warp = param (pWarp) * 0.01f;
    if (warp > 0.001f)
    {
        // bend the first half of the cycle into a smaller window: the wave
        // completes early and holds, which brightens it like a pulse squeeze
        const double bp = juce::jmax (0.02, 0.5 * (1.0 - warp));
        rp = rp < bp ? 0.5 * rp / bp : 0.5 + 0.5 * (rp - bp) / (1.0 - bp);
    }

    // Position scans the stack and crossfades between adjacent frames
    const float posNorm = juce::jlimit (0.0f, 1.0f,
        param (pPosition) * 0.01f      // a cable onto Position moves it
        + mpeSlideOffset (v, pPosition));   // (6.9.12: MPE Slide, with no cable there)
    if (v == 0 || gate.isOn (v) || ! uiVoiceShown)   // (6.9.15) shown on the card, Slide and cables too
    {
        uiPosition.store (posNorm, std::memory_order_relaxed);
        uiVoiceShown = true;
    }
    const float fpos = posNorm * (float) (numFrames - 1);
    const int f0 = juce::jlimit (0, numFrames - 1, (int) fpos);
    const int f1 = juce::jmin (numFrames - 1, f0 + 1);
    const float mix = fpos - (float) f0;

    const float y = readFrame (f0, rp) * (1.0f - mix) + readFrame (f1, rp) * mix;

    const bool envConnected = isInputConnected (1);
    const float gain = gate.next (v, envConnected, inputs[1][0], sampleRate);

    const float out = y * gain * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor wavetableDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.wavetable";
    d.displayName = "Wavetable";
    d.description =
        "Load any audio file and it is read as a stack of single-cycle frames; Position scans and "
        "crossfades through them, morphing the timbre without touching the pitch (which the "
        "Oscillator's Sample mode cannot do - it only reads the first cycle). A cable onto "
        "Position is the first thing to reach for: an LFO or ADSR there is what makes a wavetable "
        "patch move. "
        "Frame Size must match how the file was made - 2048 is a common convention. Built-in: one of "
        "Vita's tables, Massif's, or a new one from a generator (Massif's recipes, Vita's own, Phizmo's), "
        "saved with the patch. The picture shows the frame it plays - the knob, a cable or a finger's Slide.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 15;
    d.sockets = {
        modIn    ("fmIn",      "FM In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add MIDI In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",   0.0f, 1.0f, 0.8f, 0),
        makeCombo  ("frameSize", "Frame",   { "256", "512", "1024", "2048" }, 3, 0, 2),
        makeRotary ("position", "Position", 0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("warp",     "Warp",     0.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("voices",   "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f),
        makeButton ("loadSample", "Load", 3, 2),   // own row: in row 0 it overflowed the card
        makeButton ("builtIn", "Built-in", 3, 2)   // (6.9.15) Vita's, Massif's or a generated table
    };
    return d;
}

//==============================================================================
// (6.9.15) built-in tables, and the card's picture of the frame it plays
int aqBuiltinTableCount (int group);
std::string aqBuiltinTableName (int group, int i);
std::vector<float> aqBuiltinTable (int group, int i, uint32_t seed, std::string& name);

void WavetableModule::loadBuiltIn (int group, int index, uint32_t seed)
{
    std::string name;
    const auto frames = aqBuiltinTable (group, index, seed, name);
    if (frames.empty()) return;
    auto buf = std::make_shared<juce::AudioBuffer<float>> (1, (int) frames.size());
    std::copy (frames.begin(), frames.end(), buf->getWritePointer (0));
    setParameter ("frameSize", 3.0f);   // 2048
    { const juce::SpinLock::ScopedLockType sl (nameLock); builtInName = juce::String (name); }
    setLoadedSample (std::move (buf), 48000.0);
}

void WavetableModule::showBuiltInMenu()
{
    juce::PopupMenu m;
    static const char* const groups[3] = { "Vita's Tables", "Massif's Tables", "Generate (a new one each time)" };
    for (int g = 0; g < 3; ++g)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < aqBuiltinTableCount (g); ++i)
            sub.addItem (1 + g * 1000 + i, juce::String (aqBuiltinTableName (g, i)));
        m.addSubMenu (groups[g], sub);
    }
    juce::WeakReference<SynthModule> self (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [self] (int r)
    {
        if (r <= 0 || self == nullptr) return;
        auto* w = dynamic_cast<WavetableModule*> (self.get());
        if (w == nullptr) return;
        const int g = (r - 1) / 1000, i = (r - 1) % 1000;
        w->loadBuiltIn (g, i, (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30));
    });
}

namespace
{
    class WavetableDisplay final : public juce::Component, private juce::Timer
    {
    public:
        explicit WavetableDisplay (WavetableModule& m) : module (&m) { startTimerHz (20); }
        void paint (juce::Graphics& g) override
        {
            auto* w = dynamic_cast<WavetableModule*> (module.get());
            auto box = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (box, 4.0f);
            if (w == nullptr) return;
            auto area = box.reduced (6.0f, 4.0f);
            auto caption = area.removeFromTop (12.0f);
            auto strip = area.removeFromBottom (5.0f);
            const auto table = w->getLoadedSample();
            static const int sizes[4] = { 256, 512, 1024, 2048 };
            const int fs = sizes[juce::jlimit (0, 3, (int) std::lround (w->getParameter ("frameSize")))];
            const int frames = table != nullptr ? table->getNumSamples() / fs : 0;
            g.setFont (juce::Font (juce::FontOptions (10.5f)));
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText (frames > 0 ? (w->tableName().isNotEmpty() ? w->tableName() + "  " : juce::String()) + juce::String (frames) + " frames"
                                   : juce::String ("no table: Load a file, or a Built-in one"),
                        caption, juce::Justification::centredLeft, true);
            const float pos = w->getUiPosition();
            if (frames > 0)
            {
                const int f = juce::jlimit (0, frames - 1, juce::roundToInt (pos * (float) (frames - 1)));
                const float* d = table->getReadPointer (0) + (size_t) f * (size_t) fs;
                juce::Path p;
                const int n = juce::jmax (1, (int) area.getWidth());
                for (int i = 0; i <= n; ++i)
                {
                    const float x = area.getX() + (float) i;
                    const float y = area.getCentreY() - d[juce::jlimit (0, fs - 1, i * fs / n)] * area.getHeight() * 0.48f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (juce::Colour (0xff4a86d9).brighter (0.6f));
                g.strokePath (p, juce::PathStrokeType (1.2f));
            }
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (strip, 2.0f);
            g.setColour (juce::Colour (0xff00ffff));
            g.fillRoundedRectangle (strip.withWidth (juce::jmax (3.0f, strip.getWidth() * pos)), 2.0f);
        }
    private:
        void timerCallback() override { repaint(); }
        juce::WeakReference<SynthModule> module;
    };
}

std::unique_ptr<juce::Component> WavetableModule::createExtraContentComponent()
{
    return std::make_unique<WavetableDisplay> (*this);
}

AQUANODE_REGISTER_MODULE (WavetableModule, wavetableDescriptor)
