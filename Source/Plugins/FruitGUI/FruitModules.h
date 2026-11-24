#pragma once
#include "../../Modules/Core/ModuleCore.h"

//==============================================================================
// Two small modules Fruit DAW builds its own engine from. Neither is listed in
// the sidebar; they only ever live inside a Fruit DAW card.
//==============================================================================

// The notes for one synth slot: Fruit DAW's sequencer (and the keys, for the
// slot the piano roll plays) hands it each block's notes with their sample
// positions, and it plays them out of its Midi Out into the slot's synth.
class FruitNoteFeed : public aquanode::SynthModule
{
public:
    bool isMidiNoteDriver() const override { return true; }
    bool midiSourceReplacesInput() const override { return true; }

    // audio thread, before the engine's block: this block's notes, in sample order
    void beginBlock() noexcept { count = 0; readIndex = 0; }
    void push (int sampleOffset, int note, float velocity, bool on) noexcept
    {
        if (count < kMax)
            events[count++] = { sampleOffset, { juce::jlimit (0, 127, note), velocity, on } };
    }
    // (6.9.14) MPE from the keys: a note with its own bend, pressure and Slide, and its
    // expression between - only for a synth that plays them (set when it is wired)
    std::atomic<bool> mpeOk { false };
    void pushEvent (int sampleOffset, const aquanode::MidiDriverEvent& e) noexcept
    {
        if (count < kMax)
            events[count++] = { sampleOffset, e };
    }
    void sortBlock() noexcept
    {
        std::stable_sort (events.begin(), events.begin() + count, [] (const Timed& a, const Timed& b)
        {
            if (a.offset != b.offset) return a.offset < b.offset;
            return ! a.ev.isOn && b.ev.isOn;     // note-offs first on a shared sample
        });
    }

    void blockStart() override { sample = 0; }
    int midiDriverAdvanceEvents (double, aquanode::MidiDriverEvent* out, int maxEvents) override
    {
        int n = 0;
        while (readIndex < count && events[(size_t) readIndex].offset <= sample && n < maxEvents)
            out[n++] = events[(size_t) readIndex++].ev;
        ++sample;
        return n;
    }

private:
    struct Timed { int offset; aquanode::MidiDriverEvent ev; };
    static constexpr int kMax = 512;
    std::array<Timed, kMax> events {};
    int count = 0, readIndex = 0, sample = 0;
};

// One of the patch version's cards: the channel rack, the piano roll or the
// playlist. The fourth, the mixer, is the card that owns the Fruit DAW and has
// its Audio Out ("plugin.fruitgui.mixer"). A part makes no sound: it shows its
// window of the Fruit DAW whose key it carries, found among the patch's cards,
// and the channel rack passes its Midi In on to it. The four are deleted
// together (linkGroup). (In patches from 6.1's first build the Fruit DAW card
// shows the rack and the mixer is a part.)
class FruitPartModule : public aquanode::SynthModule
{
public:
    explicit FruitPartModule (int whichPart);
    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame*) override {}
    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return width.load() + 20; }
    int extraContentHeight() const override { return height.load(); }
    juce::String linkGroup() const override { return key; }
    juce::String saveCustomState() const override { return key; }
    void loadCustomState (const juce::String& state) override { key = state.trim(); }
    const int part;
    juce::String key;
    std::atomic<int> width, height;
};
struct FruitRackPart     : FruitPartModule
{
    FruitRackPart();
    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override;
};
struct FruitRollPart     : FruitPartModule { FruitRollPart(); };
struct FruitPlaylistPart : FruitPartModule { FruitPlaylistPart(); };
struct FruitMixerPart    : FruitPartModule { FruitMixerPart(); };

// The audio clips of one mixer insert: Fruit DAW hands it each block's audio
// (the samples on the playlist that go to that insert) and it plays it out.
class FruitAudioFeed : public aquanode::SynthModule
{
public:
    // audio thread, before the engine's block (nullptr: silence)
    void setBlock (const float* left, const float* right, int numSamples) noexcept { l = left; r = right; num = numSamples; }
    void blockStart() override { index = 0; }
    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        if (l != nullptr && index < num) outputs[0] = { l[index], r[index] };
        else outputs[0] = { 0.0f, 0.0f };
        ++index;
    }
    bool runsWithoutListeners() const override { return false; }
private:
    const float* l = nullptr;
    const float* r = nullptr;
    int num = 0, index = 0;
};

// The level of one mixer insert, for its meter: peak per channel, falling back.
// The end of a mixer insert's strip (6.2): polarity and pan, then its meter.
// What leaves the insert - to the master, its sends and the sidechain buses -
// leaves from here (6.1 songs: re-wired on loading, nothing else changes).
class FruitMeter : public aquanode::SynthModule
{
public:
    enum ParamIndex { pPan = 0, pInvert };
    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        fall = (float) std::exp (-1.0 / (0.25 * sr));
        glide = 1.0f - (float) std::exp (-1.0 / (0.01 * sr));
        gl = targetL(); gr = targetR();
    }
    void processSample (const aquanode::StereoFrame* in, aquanode::StereoFrame* out) override
    {
        gl += (targetL() - gl) * glide;
        gr += (targetR() - gr) * glide;
        const float l = in[0][0] * gl, r = in[0][1] * gr;
        out[0] = { l, r };
        pl = juce::jmax (std::abs (l), pl * fall);
        pr = juce::jmax (std::abs (r), pr * fall);
    }
    void blockStart() override { peakL.store (pl, std::memory_order_relaxed); peakR.store (pr, std::memory_order_relaxed); }
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
private:
    // FL's pan of a stereo track: a balance (the far side down, the near one at full)
    float sign() const { return param (pInvert) > 0.5f ? -1.0f : 1.0f; }
    float targetL() const { return sign() * juce::jmin (1.0f, 1.0f - param (pPan) * 0.01f); }
    float targetR() const { return sign() * juce::jmin (1.0f, 1.0f + param (pPan) * 0.01f); }
    float pl = 0.0f, pr = 0.0f, fall = 0.9999f, gl = 1.0f, gr = 1.0f, glide = 0.002f;
};
