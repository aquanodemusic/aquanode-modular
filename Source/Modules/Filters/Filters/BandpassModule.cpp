// Bandpass - one band let through, as narrow or as wide as you like, its
// centre free or locked to the notes of a scale.
//
// Up to four TPT state-variable band-pass stages in a row (12 to 48 dB/oct on
// each side), each normalised to 0 dB at the centre, so turning Q never jumps
// the level. Q runs from 0.3 (wider than two octaves) to 150 (a single
// singing partial). Note Lock rounds the centre - after Mod In and the knob -
// to the nearest note of the chosen scale on Root (Fine shifts the grid), and
// Glide slides from note to note: turning Freq steps through a scale, an LFO
// on Mod In plays arpeggios through the band.
//
// Flexible lane, like the other filters: per voice when fed per voice.
// Inputs: 0 = Audio In, 1 = Mod In (+-5 octaves at 100 % Mod Depth).
// Output: 0 = Audio Out.
#include "SimpleFiltersModule.h"
#include "../../Core/FilterResponse.h"

using namespace aquanode;

namespace
{
    const char* const kLockNames[] = { "Off", "Chromatic", "Major", "Minor", "Penta Major", "Penta Minor", "Blues",
                                       "Dorian", "Harmonic Minor", "Whole Tone", "Fifths", "Octaves" };
    constexpr int kNumLocks = (int) (sizeof (kLockNames) / sizeof (kLockNames[0]));
    const int kLockMasks[kNumLocks] = {
        0, 0xfff,
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),
        (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),
        (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),
        (1 << 0) | (1 << 7),
        (1 << 0) };
    const char* const kRoots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

class BandpassModule : public SynthModule
{
public:
    enum ParamIndex { pFreq = 0, pQ, pSlope, pLock, pRoot, pFine, pGlide, pGain, pMix, pModDepth };

    VoiceMode voiceMode() const override { return VoiceMode::Flexible; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }

    void reset() override
    {
        for (int v = 0; v < kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceReset (int v) override
    {
        for (auto& ch : stages[(size_t) v])
            for (auto& st : ch)
                st.reset();
        noteNow[(size_t) v] = -1.0f;
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        processVoiceSample (0, inputs, outputs);
    }

    void processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs) override
    {
        // the centre, as a (fractional) MIDI note: the knob, Mod In, then the lock
        const float depth = param (pModDepth) * 0.01f;
        float note = 69.0f + 12.0f * std::log2 (juce::jmax (10.0f, param (pFreq)) / 440.0f)
                   + 60.0f * depth * inputs[1][0];   // (5 octaves at 100 %)
        note = locked (note, param (pLock), param (pRoot), param (pFine));

        // glide toward it (in notes: equal time for equal intervals)
        float& now = noteNow[(size_t) v];
        if (now < 0.0f) now = note;
        const float glideMs = param (pGlide);
        if (glideMs <= 0.5f) now = note;
        else now += (note - now) * (1.0f - std::exp (-1.0f / (glideMs * 0.001f * (float) sampleRate)));

        const float hz = juce::jlimit (10.0f, (float) sampleRate * 0.47f, 440.0f * std::exp2 ((now - 69.0f) / 12.0f));
        const float g = tanMemo[(size_t) v] (juce::MathConstants<float>::pi * hz / (float) sampleRate,
                                             [] (float x) { return std::tan (x); });
        const int n = juce::jlimit (1, 4, (int) std::lround (param (pSlope)) + 1);
        const float q = juce::jlimit (0.3f, 150.0f, param (pQ)) * kQScale[n - 1];
        const float k = 1.0f / q;
        const float gain = juce::Decibels::decibelsToGain (param (pGain));
        const float mix = param (pMix) * 0.01f;

        for (int c = 0; c < 2; ++c)
        {
            const float x = inputs[0][(size_t) c];
            float y = x, band = 0.0f, low = 0.0f;
            for (int s = 0; s < n; ++s)
            {
                stages[(size_t) v][(size_t) c][(size_t) s].tick (y, g, k, band, low);
                y = band * k;   // 0 dB at the centre
            }
            if (! std::isfinite (y))
            {
                for (auto& st : stages[(size_t) v][(size_t) c]) st.reset();
                y = 0.0f;
            }
            outputs[0][(size_t) c] = x + mix * (y * gain - x);
        }
    }

    // the static curve: the knob's centre (locked, no glide), the same stages
    bool magnitudeResponseDb (const float* hz, float* db, int n) const override
    {
        const float note = locked (69.0f + 12.0f * std::log2 (juce::jmax (10.0f, getParameterBase (pFreq)) / 440.0f),
                                   getParameterBase (pLock), getParameterBase (pRoot), getParameterBase (pFine));
        const double fc = juce::jlimit (10.0, sampleRate * 0.47, 440.0 * std::exp2 ((note - 69.0) / 12.0));
        const int stagesN = juce::jlimit (1, 4, (int) std::lround (getParameterBase (pSlope)) + 1);
        const double q = juce::jlimit (0.3, 150.0, (double) getParameterBase (pQ)) * kQScale[stagesN - 1];
        const double k = 1.0 / q;
        const double gain = juce::Decibels::decibelsToGain ((double) getParameterBase (pGain));
        const double mix = getParameterBase (pMix) * 0.01;
        for (int i = 0; i < n; ++i)
        {
            const auto one = k * response::svf (response::sOf (hz[i], fc, sampleRate), k).band;
            response::C y = 1.0;
            for (int st = 0; st < stagesN; ++st) y *= one;
            db[i] = response::db (1.0 + mix * (y * gain - 1.0));
        }
        return true;
    }

private:
    // a cascade narrows: each stage's Q eased so the whole stays near the knob's width
    static constexpr float kQScale[4] = { 1.0f, 0.644f, 0.51f, 0.435f };

    // a note rounded to the nearest one of the lock's scale on Root (Fine shifts the grid)
    static float locked (float note, float lockKnob, float rootKnob, float fineKnob)
    {
        const int lock = juce::jlimit (0, kNumLocks - 1, (int) std::lround (lockKnob));
        if (lock <= 0)
            return note;
        const float fine = fineKnob * 0.01f;
        const int root = juce::jlimit (0, 11, (int) std::lround (rootKnob));
        const int mask = kLockMasks[lock];
        const float rel = note - fine;
        const int lo = (int) std::floor (rel) - 12, hi = (int) std::ceil (rel) + 12;
        float best = rel, bestDist = 1.0e9f;
        for (int m = lo; m <= hi; ++m)
            if (mask & (1 << (((m - root) % 12 + 12) % 12)))
                if (std::abs ((float) m - rel) < bestDist) { bestDist = std::abs ((float) m - rel); best = (float) m; }
        return best + fine;
    }

    mutable std::array<Memo, kMaxVoices> tanMemo;
    std::array<std::array<std::array<TptSvf, 4>, 2>, kMaxVoices> stages {};
    std::array<float, kMaxVoices> noteNow {};
};

static ModuleDescriptor bandpassDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.bandpass";
    d.displayName = "Bandpass";
    d.description =
        "Lets one band through, 0 dB at its centre: Q from 0.3 (wider than two octaves) to 150 (one singing partial), "
        "Slope 12 to 48 dB/oct. Note Lock rounds the centre to the nearest note of a scale on Root (Fine shifts it), "
        "Glide slides between notes - turning Freq steps through the scale, an LFO on Mod In plays arpeggios through "
        "the band. Gain and Dry/Wet after it. Per voice when fed per voice.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 21;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        modIn    ("modIn",    "Mod In"),
        audioOut ("audioOut", "Audio Out")
    };
    juce::StringArray locks, roots;
    for (auto* l : kLockNames) locks.add (l);
    for (auto* r : kRoots) roots.add (r);
    d.params = {
        makeRotary ("freq",     "Freq",     20.0f, 20000.0f, 1000.0f, 0, "Hz", true),
        makeRotary ("q",        "Q",        0.3f, 150.0f, 4.0f, 0, {}, true),
        makeCombo  ("slope",    "Slope",    { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 0, 1, 2),
        makeCombo  ("lock",     "Note Lock", locks, 0, 2, 2),
        makeCombo  ("root",     "Root",     roots, 0, 2, 1),
        makeRotary ("fine",     "Fine",     -100.0f, 100.0f, 0.0f, 3, "ct"),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 30.0f, 3, "ms"),
        makeRotary ("gain",     "Gain",     -24.0f, 24.0f, 0.0f, 4, "dB"),
        makeRotary ("mix",      "Dry/Wet",  0.0f, 100.0f, 100.0f, 4, "%"),
        makeRotary ("modDepth", "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%")
    };
    d.rowSections = { { 2, "Note Lock" } };
    return d;
}

AQUANODE_REGISTER_MODULE (BandpassModule, bandpassDescriptor)
