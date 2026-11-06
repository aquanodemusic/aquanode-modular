#pragma once
//==============================================================================
// Tidebreeze - the program's parameters
//
// The parameters of one program, laid out like a workstation's: two
// oscillators ("o1_", "o2_"), each with its waves, pitch, filter, filter EG,
// amp, amp EG and two LFOs; the common pitch EG and voice settings; the
// arpeggiator; three insert and two master effects; the master EQ.
// Integer 0..99 values behave like the workstation's (tb::egTime, cutoffHz,
// lfoHz map them).
//==============================================================================
#include "TbWaves.h"
#include "TbDsp.h"
#include "TbEffects.h"

namespace tb
{
    constexpr int kUserWaveSlots = 16;
    inline int numFactoryWaves() { return (int) factoryWaves().size(); }
    inline int numWaveChoices() { return numFactoryWaves() + kUserWaveSlots; }
    inline int factoryWaveIndex (const juce::String& name)
    {
        const auto& w = factoryWaves();
        for (int i = 0; i < (int) w.size(); ++i) if (name == w[(size_t) i].name) return i;
        return 0;
    }

    inline juce::String oscId (int osc, const char* name) { return "o" + juce::String (osc + 1) + "_" + name; }
    inline juce::String fxId (const char* slot, int n, const char* name) { return juce::String (slot) + juce::String (n + 1) + "_" + name; }

    inline const juce::StringArray& octaveNames() { static const juce::StringArray n { "32'", "16'", "8'", "4'" }; return n; }

    // arpeggiator patterns
    inline const juce::StringArray& arpPatternNames()
    {
        static const juce::StringArray n { "Up", "Down", "Up & Down", "Up & Down 2", "Random", "As Played", "Chord", "Bossa",
                                           "3+3+2", "Gallop", "Rise & Fall", "Echo Steps" };
        return n;
    }
    inline const juce::StringArray& arpResNames()
    {
        static const juce::StringArray n { "1/32", "1/24", "1/16", "1/12", "1/8", "1/6", "1/4" };
        return n;
    }
    inline float arpResBeats (int i) { static const float b[] = { 0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f }; return b[juce::jlimit (0, 6, i)]; }

    // a function that names a wave choice: factory names, then the user slots
    // (their current names come from the processor)
    using WaveNamer = std::function<juce::String (int)>;

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout (WaveNamer waveName)
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;
        auto i = [&] (const String& id, const String& name, int lo, int hi, int def)
        {
            p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
        };
        auto b = [&] (const String& id, const String& name, bool def)
        {
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
        };
        auto c = [&] (const String& id, const String& name, const StringArray& items, int def)
        {
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
        };
        auto wave = [&] (const String& id, const String& name, int def)
        {
            auto attrs = AudioParameterIntAttributes().withStringFromValueFunction ([waveName] (int v, int) { return waveName (v); });
            p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, 0, numWaveChoices() - 1, def, attrs));
        };

        i ("mode", "Layers (0 one, 1 two)", 0, 1, 0);
        c ("voiceMode", "Voice Mode", { "Poly", "Mono" }, 0);
        b ("legato", "Legato", false);
        b ("hold", "Hold", false);
        i ("porta", "Glide Time", 0, 99, 0);
        i ("bend", "Bend Range", 0, 12, 2);
        i ("volume", "Volume", 0, 127, 105);

        // the common pitch EG
        i ("pegStart", "Pitch Env Start", -99, 99, 0);
        i ("pegAttack", "Pitch Env Peak", -99, 99, 0);
        i ("pegRelease", "Pitch Env End", -99, 99, 0);
        i ("pegAttackT", "Pitch Env Attack", 0, 99, 0);
        i ("pegDecayT", "Pitch Env Decay", 0, 99, 0);
        i ("pegReleaseT", "Pitch Env Release", 0, 99, 0);

        // the four macro knobs: offsets on both layers
        i ("rtCutoff", "Macro Cutoff", -63, 63, 0);
        i ("rtReso", "Macro Resonance", -63, 63, 0);
        i ("rtEgInt", "Macro Env Depth", -63, 63, 0);
        i ("rtRelease", "Macro Release", -63, 63, 0);

        const int saw = factoryWaveIndex ("Saw");
        for (int o = 0; o < 2; ++o)
        {
            auto id = [o] (const char* n) { return oscId (o, n); };
            const String pre = String ("Layer ") + (o == 0 ? "A " : "B ");
            wave (id ("hiWave"), pre + "High Wave", saw);
            wave (id ("loWave"), pre + "Low Wave", saw);
            i (id ("velSw"), pre + "Velocity Split", 1, 127, 1);
            b (id ("hiRev"), pre + "High Reverse", false);
            b (id ("hiOfs"), pre + "High Skip Attack", false);
            i (id ("hiLvl"), pre + "High Level", 0, 127, 127);
            b (id ("loRev"), pre + "Low Reverse", false);
            b (id ("loOfs"), pre + "Low Skip Attack", false);
            i (id ("loLvl"), pre + "Low Level", 0, 127, 127);
            c (id ("octave"), pre + "Octave", octaveNames(), 2);
            i (id ("transpose"), pre + "Transpose", -12, 12, 0);
            i (id ("tune"), pre + "Tune (cents)", -1200, 1200, 0);
            i (id ("delay"), pre + "Delay (ms)", 0, 5000, 0);
            {
                // how far the pitch follows the keys: +1.0 normal, 0 the same pitch everywhere
                auto attrs = AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v >= 0 ? "+" : "") + String ((double) v / 10.0, 1); });
                p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id ("pSlope"), 1 }, pre + "Pitch Slope", -10, 20, 10, attrs));
            }
            i (id ("velLo"), pre + "Velocity Range Low", 1, 127, 1);
            i (id ("velHi"), pre + "Velocity Range High", 1, 127, 127);

            i (id ("pegInt"), pre + "Pitch Env Depth (st)", -12, 12, 0);
            i (id ("l1Pitch"), pre + "LFO1 to Pitch (x0.1 st)", -120, 120, 0);
            i (id ("l2Pitch"), pre + "LFO2 to Pitch (x0.1 st)", -120, 120, 0);

            c (id ("fType"), pre + "Filter Type", { "Resonant Low-Pass", "Low-Pass + High-Pass" }, 0);
            i (id ("fA"), pre + "Cutoff", 0, 99, 99);
            i (id ("fRes"), pre + "Resonance", 0, 99, 0);
            i (id ("fB"), pre + "High-Pass", 0, 99, 0);
            i (id ("fTrim"), pre + "Filter Input", 0, 99, 90);
            i (id ("fKt"), pre + "Filter Keytrack", -99, 99, 0);
            i (id ("fEgA"), pre + "Filter Env Depth", -99, 99, 0);
            i (id ("fEgB"), pre + "High-Pass Env Depth", -99, 99, 0);
            i (id ("fVelA"), pre + "Velocity to Cutoff", -99, 99, 0);
            i (id ("fL1A"), pre + "LFO1 to Cutoff", -99, 99, 0);
            i (id ("fL2A"), pre + "LFO2 to Cutoff", -99, 99, 0);

            i (id ("fegStart"), pre + "Filter Env Start", -99, 99, 0);
            i (id ("fegAttack"), pre + "Filter Env Peak", -99, 99, 0);
            i (id ("fegBreak"), pre + "Filter Env Knee", -99, 99, 0);
            i (id ("fegSustain"), pre + "Filter Env Sustain", -99, 99, 0);
            i (id ("fegRelease"), pre + "Filter Env End", -99, 99, 0);
            i (id ("fegAttackT"), pre + "Filter Env Attack", 0, 99, 0);
            i (id ("fegDecayT"), pre + "Filter Env Decay", 0, 99, 40);
            i (id ("fegSlopeT"), pre + "Filter Env Settle", 0, 99, 50);
            i (id ("fegReleaseT"), pre + "Filter Env Release", 0, 99, 40);

            i (id ("aLvl"), pre + "Amp Level", 0, 127, 100);
            i (id ("aPan"), pre + "Pan", -63, 63, 0);
            i (id ("aVel"), pre + "Velocity to Level", 0, 99, 50);
            i (id ("aL1"), pre + "LFO1 to Amp", -99, 99, 0);
            i (id ("aL2"), pre + "LFO2 to Amp", -99, 99, 0);

            i (id ("aegStart"), pre + "Amp Env Start", 0, 99, 0);
            i (id ("aegAttack"), pre + "Amp Env Peak", 0, 99, 99);
            i (id ("aegBreak"), pre + "Amp Env Knee", 0, 99, 99);
            i (id ("aegSustain"), pre + "Amp Env Sustain", 0, 99, 99);
            i (id ("aegAttackT"), pre + "Amp Env Attack", 0, 99, 0);
            i (id ("aegDecayT"), pre + "Amp Env Decay", 0, 99, 40);
            i (id ("aegSlopeT"), pre + "Amp Env Settle", 0, 99, 40);
            i (id ("aegReleaseT"), pre + "Amp Env Release", 0, 99, 30);

            for (int l = 0; l < 2; ++l)
            {
                auto lid = [o, l] (const char* n) { return oscId (o, (juce::String ("l") + juce::String (l + 1) + n).toRawUTF8()); };
                const String lp = pre + "LFO" + String (l + 1) + " ";
                c (lid ("Wave"), lp + "Waveform", lfoShapeNames(), l == 0 ? LfoTriangle : LfoSine);
                i (lid ("Freq"), lp + "Frequency", 0, 99, l == 0 ? 50 : 40);
                b (lid ("Sync"), lp + "Tempo Sync", false);
                c (lid ("Div"), lp + "Sync Division", lfoDivNames(), 6);
                b (lid ("Key"), lp + "Key Sync", true);
                i (lid ("Fade"), lp + "Fade In", 0, 99, 0);
                i (lid ("Delay"), lp + "Delay", 0, 99, 0);
                i (lid ("Ofs"), lp + "Offset", -99, 99, 0);
            }
        }

        b ("arpOn", "Arpeggiator On", false);
        c ("arpPat", "Arp Pattern", arpPatternNames(), 0);
        c ("arpRes", "Arp Resolution", arpResNames(), 2);
        i ("arpOct", "Arp Octaves", 1, 4, 1);
        i ("arpGate", "Arp Gate", 0, 100, 60);
        i ("arpVel", "Arp Velocity (0 = Key)", 0, 127, 0);
        i ("arpSwing", "Arp Swing", -50, 50, 0);
        b ("arpLatch", "Arp Latch", false);
        i ("arpTempo", "Tempo (without host)", 40, 300, 120);

        for (int n = 0; n < 3; ++n)
        {
            const String pre = "Insert " + String (n + 1) + " ";
            c (fxId ("ifx", n, "type"), pre + "Type", effectNames(), 0);
            b (fxId ("ifx", n, "on"), pre + "On", true);
            for (int k = 0; k < 5; ++k)
                i (fxId ("ifx", n, ("p" + String (k + 1)).toRawUTF8()), pre + "Param " + String (k + 1), 0, 100, 50);
            i (fxId ("ifx", n, "mix"), pre + "Dry/Wet", 0, 100, 50);
        }
        for (int n = 0; n < 2; ++n)
        {
            const String pre = "Send FX " + String (n + 1) + " ";
            c (fxId ("mfx", n, "type"), pre + "Type", effectNames(), 0);
            b (fxId ("mfx", n, "on"), pre + "On", true);
            for (int k = 0; k < 5; ++k)
                i (fxId ("mfx", n, ("p" + String (k + 1)).toRawUTF8()), pre + "Param " + String (k + 1), 0, 100, 50);
            i (fxId ("mfx", n, "send"), pre + "Send", 0, 127, 0);
            i (fxId ("mfx", n, "ret"), pre + "Return", 0, 127, 100);
        }
        i ("eqLo", "Master EQ Low (dB)", -18, 18, 0);
        i ("eqMid", "Master EQ Mid (dB)", -18, 18, 0);
        i ("eqMidHz", "Master EQ Mid Freq (Hz)", 200, 8000, 1000);
        i ("eqHi", "Master EQ High (dB)", -18, 18, 0);

        return { p.begin(), p.end() };
    }
}
