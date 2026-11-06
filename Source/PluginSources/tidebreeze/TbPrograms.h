#pragma once
//==============================================================================
// Tidebreeze - factory programs and random programs
//
// A program is a set of parameter values plus the rolled waves it uses. The
// factory programs build themselves through ProgramBuilder (which reads
// almost like a workstation's edit sheet); random programs are put together
// the same way from a category's waves, envelopes, modulation and effects.
//==============================================================================
#include "TbParams.h"

namespace tb
{
    struct ProgramTarget
    {
        std::function<void (const juce::String& id, float value)> set;    // a parameter, in its own units
        std::function<int (int archetype, juce::uint32 seed)> rollWave;   // a rolled wave into a user slot; its choice index
    };

    inline const juce::StringArray& categoryNames()
    {
        static const juce::StringArray n { "Keys", "Organs", "Bells", "Strings", "Voices & Air", "Pads",
                                           "Plucks & Stabs", "Leads", "Currents", "Basses", "Sea & Shore", "Arps & Ripples", "Breezes" };
        return n;
    }
    // CatMotion is "Currents" (evolving, moving sounds), CatWater "Sea & Shore",
    // CatBreeze "Breezes" (gentle airy pads heard through moving band-passes)
    enum Category { CatKeyboard, CatOrgan, CatBell, CatStrings, CatVocal, CatSlowSynth, CatFastSynth, CatLead,
                    CatMotion, CatBass, CatWater, CatArp, CatBreeze, NumCategories };
    // the order the menus list them in
    inline const std::array<int, NumCategories>& categoryMenuOrder()
    {
        static const std::array<int, NumCategories> o { CatMotion, CatBreeze, CatSlowSynth, CatVocal, CatWater, CatStrings, CatKeyboard,
                                                        CatBell, CatOrgan, CatFastSynth, CatLead, CatBass, CatArp };
        return o;
    }

    struct FactoryProgram
    {
        const char* name;
        int category;
        std::function<void (ProgramTarget&)> build;
    };
    const std::vector<FactoryProgram>& factoryPrograms();

    // a random program of a category; returns its name
    juce::String randomProgram (ProgramTarget& t, int category, juce::uint32 seed);
    // every part rolled over its whole range (with guards); randomProgram with
    // category kSuperRandom does the same
    constexpr int kSuperRandom = 1000;
    juce::String superRandomProgram (ProgramTarget& t, juce::uint32 seed);

    //==========================================================================
    // writing programs
    //==========================================================================
    struct ProgramBuilder
    {
        ProgramTarget& t;
        explicit ProgramBuilder (ProgramTarget& target) : t (target) {}

        ProgramBuilder& single() { t.set ("mode", 0); return *this; }
        ProgramBuilder& dbl() { t.set ("mode", 1); return *this; }
        ProgramBuilder& set (const juce::String& id, float v) { t.set (id, v); return *this; }

        struct Osc
        {
            ProgramTarget& t; int o;
            Osc& set (const char* n, float v) { t.set (oscId (o, n), v); return *this; }
            Osc& wave (const juce::String& hi) { const int w = factoryWaveIndex (hi); set ("hiWave", (float) w); set ("loWave", (float) w); return *this; }
            Osc& wave (const juce::String& hi, const juce::String& lo, int velSw)
            { set ("hiWave", (float) factoryWaveIndex (hi)); set ("loWave", (float) factoryWaveIndex (lo)); set ("velSw", (float) velSw); return *this; }
            Osc& waveIndex (int w) { set ("hiWave", (float) w); set ("loWave", (float) w); return *this; }
            Osc& pitch (int octave, int transpose = 0, int tuneCents = 0)   // octave: -2 32', -1 16', 0 8', 1 4'
            { set ("octave", (float) (octave + 2)); set ("transpose", (float) transpose); set ("tune", (float) tuneCents); return *this; }
            Osc& delay (int ms) { return set ("delay", (float) ms); }
            Osc& slope (int tenths) { return set ("pSlope", (float) tenths); }   // 10 = +1.0 (normal), 0 = fixed pitch
            Osc& reverse() { set ("hiRev", 1); return set ("loRev", 1); }
            Osc& offset() { set ("hiOfs", 1); return set ("loOfs", 1); }
            Osc& filter (int cutoff, int reso, int keytrack = 0, int type = 0, int hpf = 0)
            { set ("fType", (float) type); set ("fA", (float) cutoff); set ("fRes", (float) reso); set ("fKt", (float) keytrack); return set ("fB", (float) hpf); }
            Osc& fegInt (int a, int vel = 0, int b = 0) { set ("fEgA", (float) a); set ("fVelA", (float) vel); return set ("fEgB", (float) b); }
            // levels start, attack, break, sustain, release; times attack, decay, slope, release
            Osc& feg (int sL, int aL, int bL, int suL, int rL, int aT, int dT, int slT, int rT)
            {
                set ("fegStart", (float) sL); set ("fegAttack", (float) aL); set ("fegBreak", (float) bL); set ("fegSustain", (float) suL); set ("fegRelease", (float) rL);
                set ("fegAttackT", (float) aT); set ("fegDecayT", (float) dT); set ("fegSlopeT", (float) slT); return set ("fegReleaseT", (float) rT);
            }
            Osc& amp (int level, int pan = 0, int velSens = 50) { set ("aLvl", (float) level); set ("aPan", (float) pan); return set ("aVel", (float) velSens); }
            Osc& aeg (int sL, int aL, int bL, int suL, int aT, int dT, int slT, int rT)
            {
                set ("aegStart", (float) sL); set ("aegAttack", (float) aL); set ("aegBreak", (float) bL); set ("aegSustain", (float) suL);
                set ("aegAttackT", (float) aT); set ("aegDecayT", (float) dT); set ("aegSlopeT", (float) slT); return set ("aegReleaseT", (float) rT);
            }
            // n = 1 or 2
            Osc& lfo (int n, int shape, int freq, int pitchInt10 = 0, int cutoffInt = 0, int ampInt = 0, int fade = 0, bool key = true)
            {
                const juce::String p = "l" + juce::String (n);
                t.set (oscId (o, (p + "Wave").toRawUTF8()), (float) shape);
                t.set (oscId (o, (p + "Freq").toRawUTF8()), (float) freq);
                t.set (oscId (o, (p + "Fade").toRawUTF8()), (float) fade);
                t.set (oscId (o, (p + "Key").toRawUTF8()), key ? 1.0f : 0.0f);
                set (n == 1 ? "l1Pitch" : "l2Pitch", (float) pitchInt10);
                set (n == 1 ? "fL1A" : "fL2A", (float) cutoffInt);
                return set (n == 1 ? "aL1" : "aL2", (float) ampInt);
            }
            Osc& lfoSync (int n, int div)
            {
                const juce::String p = "l" + juce::String (n);
                t.set (oscId (o, (p + "Sync").toRawUTF8()), 1.0f);
                t.set (oscId (o, (p + "Div").toRawUTF8()), (float) div);
                return *this;
            }
        };
        Osc osc (int n) { return Osc { t, n - 1 }; }    // 1 or 2

        ProgramBuilder& peg (int sL, int aL, int rL, int aT, int dT, int rT)
        {
            t.set ("pegStart", (float) sL); t.set ("pegAttack", (float) aL); t.set ("pegRelease", (float) rL);
            t.set ("pegAttackT", (float) aT); t.set ("pegDecayT", (float) dT); t.set ("pegReleaseT", (float) rT);
            return *this;
        }
        // n = 1..3 (insert) ; p are 0..100
        ProgramBuilder& ifx (int n, int type, std::initializer_list<int> p, int mix = 50)
        {
            t.set (fxId ("ifx", n - 1, "type"), (float) type);
            t.set (fxId ("ifx", n - 1, "on"), 1.0f);
            int k = 0; for (int v : p) { if (k < 5) t.set (fxId ("ifx", n - 1, ("p" + juce::String (++k)).toRawUTF8()), (float) v); }
            t.set (fxId ("ifx", n - 1, "mix"), (float) mix);
            return *this;
        }
        ProgramBuilder& mfx (int n, int type, std::initializer_list<int> p, int send, int ret = 100)
        {
            t.set (fxId ("mfx", n - 1, "type"), (float) type);
            t.set (fxId ("mfx", n - 1, "on"), 1.0f);
            int k = 0; for (int v : p) { if (k < 5) t.set (fxId ("mfx", n - 1, ("p" + juce::String (++k)).toRawUTF8()), (float) v); }
            t.set (fxId ("mfx", n - 1, "send"), (float) send);
            t.set (fxId ("mfx", n - 1, "ret"), (float) ret);
            return *this;
        }
        ProgramBuilder& arp (int pattern, int res, int octaves, int gate, int vel = 0)
        {
            t.set ("arpOn", 1); t.set ("arpPat", (float) pattern); t.set ("arpRes", (float) res);
            t.set ("arpOct", (float) octaves); t.set ("arpGate", (float) gate); t.set ("arpVel", (float) vel);
            return *this;
        }
        ProgramBuilder& eq (int lo, int mid, int midHz, int hi) { t.set ("eqLo", (float) lo); t.set ("eqMid", (float) mid); t.set ("eqMidHz", (float) midHz); t.set ("eqHi", (float) hi); return *this; }
        ProgramBuilder& volume (int v) { t.set ("volume", (float) v); return *this; }
        ProgramBuilder& mono (bool legato = true, int porta = 0) { t.set ("voiceMode", 1); t.set ("legato", legato ? 1.0f : 0.0f); t.set ("porta", (float) porta); return *this; }
    };
}
