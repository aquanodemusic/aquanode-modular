#include "TbPrograms.h"

//==============================================================================
// Tidebreeze - the factory programs
//
// Values are the workstation's 0..99 scale unless noted (TbCommon.h maps them:
// egTime 50 = 0.18 s, 65 = 0.86 s, 75 = 2.5 s; cutoff 50 = 830 Hz, 70 = 3.1 kHz;
// LFO 40 = 0.32 Hz, 60 = 1.5 Hz). Effect parameters are 0..100.
//==============================================================================
namespace tb
{
namespace
{
    using B = ProgramBuilder;

    //==========================================================================
    // Motion Synth
    //==========================================================================
    void dolphinDrift (ProgramTarget& t)
    {
        // a dark, breathy "ah" an octave down, swelling slowly; two octaves up,
        // now and then, a dolphin's click train gliding through - through
        // modulation delay, a wide phaser and a long smooth hall
        B b (t); b.dbl();
        b.osc (1).wave ("Dolphin Clicks").pitch (0, 12, 1200).delay (700)
                 .filter (80, 12, 0).fegInt (10, 20).feg (60, 30, 10, 0, 0, 40, 66, 70, 60)
                 .amp (30, -16, 25).aeg (0, 99, 92, 85, 58, 66, 72, 74)
                 .lfo (1, LfoTriangle, 40, 2, 10, 0, 40);
        b.osc (2).wave ("Soft Ah").pitch (-1, -4, 400).offset()
                 .filter (48, 22, 99).fegInt (30).feg (99, 99, 55, 8, 0, 0, 70, 77, 62)
                 .amp (112, 12, 25).aeg (40, 80, 99, 88, 64, 72, 74, 75)
                 .lfo (1, LfoSine, 38, 1, 10, 0, 30)
                 .lfo (2, LfoTriangle, 30, 0, 0, 10);
        b.ifx (1, FxModDelay, { 62, 20, 35, 40, 30 }, 55);
        b.ifx (2, FxPhaser, { 34, 75, 40, 62, 100 }, 75);
        b.mfx (1, FxChorus, { 30, 40, 40, 10, 50 }, 0);
        b.mfx (2, FxSmoothHall, { 74, 20, 45, 75, 0 }, 110);
        b.volume (100);
    }
    void emberFifths (ProgramTarget& t)
    {
        // a reversed wave sweep below, a breathy flute a fifth above it, a
        // resonant filter closing after the attack, phaser into a sequence delay
        B b (t); b.dbl();
        b.osc (1).wave ("Tide Sweep").pitch (-1).reverse()
                 .filter (74, 46, 20).fegInt (-55, 10).feg (99, 99, 20, 10, 0, 0, 52, 70, 55)
                 .amp (104, -40, 40).aeg (0, 99, 70, 60, 2, 62, 74, 48);
        b.osc (2).wave ("Bamboo Breath").pitch (0, 12, -500)
                 .filter (64, 30, 25).fegInt (-30).feg (99, 99, 30, 10, 0, 0, 55, 70, 55)
                 .amp (98, 40, 40).aeg (0, 99, 80, 70, 10, 64, 74, 48)
                 .lfo (1, LfoTriangle, 42, 0, 8, 0, 20);
        b.ifx (1, FxPhaser, { 40, 75, 52, 66, 50 }, 55);
        b.ifx (2, FxSequenceDelay, { 0, 50, 40, 50, 80 }, 35);
        b.mfx (1, FxMultitap, { 70, 30, 40, 30, 70 }, 35);
        b.mfx (2, FxWetPlate, { 45, 10, 40, 60, 25 }, 64);
        b.volume (100);
    }
    void hydrophonics (ProgramTarget& t)
    {
        // an airy noise spectrum a fourth below, a bamboo breath on top, both
        // dark and round; a random filter and a sequence delay keep it moving
        // (switch the arpeggiator on for its bossa rhythm)
        B b (t); b.dbl();
        b.osc (1).wave ("Noise Spectrum").pitch (-1, -2, 893)
                 .filter (44, 20, 40, 1, 10).fegInt (30, 25, 20).feg (99, 60, 30, 10, 0, 0, 62, 70, 58)
                 .amp (70, -20, 50).aeg (0, 99, 80, 70, 66, 62, 70, 60);
        b.osc (2).wave ("Bamboo Breath").pitch (0)
                 .filter (46, 30, 50).fegInt (12, 30).feg (40, 60, 30, 10, 0, 40, 60, 66, 60)
                 .amp (112, 20, 50).aeg (0, 99, 90, 80, 66, 64, 72, 60);
        b.set ("arpPat", 7); b.set ("arpRes", 2); b.set ("arpOct", 3); b.set ("arpGate", 70);
        b.ifx (1, FxSequenceDelay, { 30, 50, 45, 40, 80 }, 30);
        b.ifx (2, FxRandomFilter, { 30, 35, 35, 40, 50 }, 40);
        b.mfx (1, FxHarmonicChorus, { 30, 50, 40, 20, 60 }, 45);
        b.mfx (2, FxWetPlate, { 52, 10, 50, 55, 15 }, 64);
        b.volume (104);
    }
    void swirlingShoals (ProgramTarget& t)
    {
        // a glassy vox swelling up and fading to a glow, over a running stream
        // that keeps its own pitch on every key - gritty, phased, echoing
        B b (t); b.dbl();
        b.osc (1).wave ("Glass Choir").pitch (0)
                 .filter (74, 20, 14, 1, 0).fegInt (0, 40).feg (60, 20, 0, 10, 0, 10, 50, 60, 50)
                 .amp (112, -10, 45).aeg (0, 99, 70, 22, 68, 74, 78, 66)
                 .lfo (1, LfoSine, 36, 2, 8, 0, 30);
        b.osc (2).wave ("Stream Flow").pitch (0).slope (0).delay (6)
                 .filter (80, 10, 0, 1, 30).feg (50, 20, 0, 10, 0, 10, 50, 60, 50)
                 .amp (84, 20, 20).aeg (0, 99, 90, 85, 56, 66, 74, 70);
        b.ifx (1, FxDecimator, { 68, 70, 100, 15, 0 }, 40);
        b.ifx (2, FxPhaser, { 30, 45, 50, 55, 100 }, 70);
        b.mfx (1, FxSequenceDelay, { 40, 50, 35, 45, 100 }, 60);
        b.mfx (2, FxWetPlate, { 66, 15, 35, 65, 10 }, 110);
        b.volume (104);
    }
    void sonarDrift (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Sonar Clicks").pitch (1, 7).filter (75, 10, 30).amp (90, -30, 30).aeg (0, 99, 90, 80, 10, 60, 70, 60);
        b.osc (2).wave ("Airy Vox").pitch (0).filter (60, 15, 20).fegInt (20).feg (0, 40, 30, 20, 0, 60, 66, 70, 60)
                 .amp (100, 20, 40).aeg (0, 99, 99, 90, 55, 70, 74, 66).lfo (1, LfoRandom4, 48, 0, 20, 0, 20);
        b.ifx (1, FxPhaser, { 30, 80, 45, 65, 50 }, 50);
        b.ifx (2, FxModDelay, { 70, 75, 40, 50, 25 }, 18);
        b.mfx (2, FxSmoothHall, { 70, 25, 45, 75, 30 }, 85);
    }
    void tideTurner (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Tide Sweep").pitch (0).filter (68, 35, 20).lfo (1, LfoTriangle, 34, 0, 18, 0, 30)
                 .amp (100, -25).aeg (0, 99, 99, 90, 45, 66, 74, 64);
        b.osc (2).wave ("Tide Sweep").pitch (0, 0, 9).reverse().filter (64, 30, 20).lfo (1, LfoTriangle, 36, 0, -18, 0, 30)
                 .amp (100, 25).aeg (0, 99, 99, 90, 50, 66, 74, 64);
        b.ifx (1, FxPhaser, { 20, 60, 55, 60, 60 }, 50);
        b.mfx (1, FxChorus, { 25, 45, 45, 10, 60 }, 40);
        b.mfx (2, FxHall, { 60, 25, 45, 70, 30 }, 70);
    }

    //==========================================================================
    // Slow Synth, Vocal/Airy, Strings
    //==========================================================================
    void glassLagoon (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Glass Choir").pitch (0).filter (84, 5, 20).amp (106, -63, 40).aeg (0, 99, 99, 95, 50, 70, 74, 64)
                 .lfo (1, LfoSine, 32, 1, 0, 0, 40);
        b.osc (2).wave ("Airy Vox").pitch (0, 0, 7).filter (80, 10, 20).amp (84, 63, 40).aeg (0, 99, 99, 90, 58, 70, 74, 64);
        b.mfx (1, FxChorus, { 28, 45, 40, 10, 100 }, 60);
        b.mfx (2, FxSmoothHall, { 66, 20, 50, 72, 30 }, 80);
    }
    void oohTideChoir (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Choir Ooh").pitch (0).filter (95, 5, 10).amp (106, -45, 40).aeg (0, 99, 99, 95, 40, 70, 74, 56);
        b.osc (2).wave ("Soft Ah").pitch (0, 0, -6).filter (80, 10, 10).amp (84, 45, 40).aeg (0, 99, 99, 90, 46, 70, 74, 58);
        b.mfx (1, FxEnsemble, { 35, 50, 30, 60, 100 }, 55);
        b.mfx (2, FxSmoothHall, { 64, 25, 50, 70, 30 }, 75);
    }
    void seafoamStrings (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Ensemble Strings").pitch (0).filter (77, 10, 20).fegInt (12, 30).feg (0, 30, 10, 0, 0, 50, 60, 66, 60)
                 .amp (104, -63, 55).aeg (0, 99, 99, 95, 40, 70, 74, 60);
        b.osc (2).wave ("Ensemble Strings").pitch (0, 4, -393).filter (72, 10, 20).amp (90, 63, 55).aeg (0, 99, 99, 95, 44, 70, 74, 60);
        b.ifx (1, FxTiltEq, { 52, 58, 55, 20, 56 }, 100);
        b.mfx (1, FxEnsemble, { 30, 45, 20, 70, 100 }, 60);
        b.mfx (2, FxSmoothHall, { 60, 20, 50, 68, 30 }, 70);
    }
    void breezyAirPad (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Airy Vox").pitch (0).filter (66, 22, 18).lfo (1, LfoTriangle, 30, 0, 16, 0, 50)
                 .amp (104, -20, 35).aeg (0, 99, 99, 92, 64, 70, 74, 70);
        b.osc (2).wave ("White Pad").pitch (1, 0, 5).filter (62, 15, 18).lfo (1, LfoTriangle, 28, 0, -16, 0, 50)
                 .amp (78, 22, 35).aeg (0, 99, 99, 90, 68, 70, 74, 70);
        b.ifx (1, FxPhaser, { 24, 55, 50, 58, 60 }, 40);
        b.mfx (2, FxHall, { 64, 30, 50, 70, 30 }, 80);
    }
    void bambooBreeze (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Bamboo Breath").pitch (0).filter (76, 10, 20).amp (106, -10, 40).aeg (0, 99, 90, 80, 30, 66, 72, 64);
        b.osc (2).wave ("Breakwater Whistle").pitch (1).filter (70, 10, 10).amp (70, 15, 30).aeg (0, 99, 99, 90, 60, 70, 74, 68);
        b.mfx (1, FxModDelay, { 70, 70, 35, 40, 25 }, 25);
        b.mfx (2, FxSmoothHall, { 66, 25, 50, 72, 30 }, 75);
    }
    void hazeCurrent (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Air Vortex").pitch (0).filter (70, 30, 10).lfo (1, LfoRandom5, 34, 0, 20, 0, 0)
                 .amp (96, -25, 30).aeg (0, 99, 99, 95, 66, 70, 74, 70);
        b.osc (2).wave ("Analog Strings").pitch (0, 0, -8).filter (60, 20, 20).fegInt (25).feg (-40, 40, 20, 10, 0, 70, 70, 74, 66)
                 .amp (100, 20, 40).aeg (0, 99, 99, 95, 60, 70, 74, 68);
        b.ifx (1, FxPhaser, { 18, 70, 50, 62, 60 }, 50);
        b.mfx (2, FxSmoothHall, { 70, 30, 55, 75, 30 }, 85);
    }
    void moonlitChoir (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Glass Choir").pitch (1).filter (76, 0, 10).amp (84, -15, 35).aeg (0, 99, 99, 95, 60, 70, 74, 70);
        b.osc (2).wave ("Choir Ooh").pitch (0).filter (72, 5, 10).amp (106, 15, 35).aeg (0, 99, 99, 95, 55, 70, 74, 66);
        b.mfx (1, FxEnsemble, { 30, 45, 25, 60, 60 }, 45);
        b.mfx (2, FxHall, { 66, 30, 50, 72, 30 }, 80);
    }
    void sirenVoices (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Soft Ah").pitch (0).filter (72, 15, 10).lfo (1, LfoSine, 62, 3, 0, 0, 55)
                 .amp (104, -20, 40).aeg (0, 99, 99, 95, 50, 70, 74, 64);
        b.osc (2).wave ("Breakwater Whistle").pitch (1).filter (74, 10, 10).amp (70, 20, 30).aeg (0, 99, 99, 92, 62, 70, 74, 68);
        b.mfx (1, FxModDelay, { 74, 80, 40, 50, 30 }, 28);
        b.mfx (2, FxSmoothHall, { 68, 30, 50, 74, 30 }, 80);
    }
    void whisperWaves (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Noise Spectrum").pitch (0).filter (72, 20, 10).amp (100, -25, 30).aeg (0, 99, 99, 95, 58, 70, 74, 68)
                 .lfo (2, LfoRandom4, 40, 0, 0, 0, 0);
        b.osc (2).wave ("Airy Vox").pitch (0, 0, 6).filter (68, 10, 10).amp (90, 25, 30).aeg (0, 99, 99, 92, 62, 70, 74, 68);
        b.ifx (1, FxAutoPan, { 30, 50, 10, 0, 50 }, 100);
        b.mfx (2, FxWetPlate, { 62, 20, 45, 70, 30 }, 80);
    }
    void lighthouseStrings (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Analog Strings").pitch (0).filter (58, 18, 30).fegInt (34, 20).feg (-20, 50, 30, 20, 0, 68, 70, 74, 64)
                 .amp (104, -20, 45).aeg (0, 99, 99, 95, 52, 70, 74, 64);
        b.osc (2).wave ("Dream Strings").pitch (0, 0, 7).filter (66, 10, 20).amp (88, 20, 45).aeg (0, 99, 99, 95, 58, 70, 74, 66);
        b.mfx (1, FxEnsemble, { 34, 55, 30, 60, 70 }, 55);
        b.mfx (2, FxSmoothHall, { 62, 20, 50, 70, 30 }, 70);
    }

    //==========================================================================
    // Fast Synth, Lead, Bass
    //==========================================================================
    void superSurf (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Super Saw").pitch (0).filter (72, 20, 30).fegInt (20, 30).feg (40, 60, 20, 0, 0, 0, 55, 62, 45)
                 .amp (96, -25, 55).aeg (0, 99, 90, 80, 0, 60, 66, 48);
        b.osc (2).wave ("Super Saw").pitch (1, 0, 10).filter (68, 20, 30).amp (70, 25, 55).aeg (0, 99, 90, 80, 0, 60, 66, 48);
        b.ifx (1, FxChorus, { 30, 50, 40, 10, 60 }, 50);
        b.mfx (1, FxSequenceDelay, { 0, 50, 35, 50, 80 }, 35);
        b.mfx (2, FxHall, { 50, 10, 50, 60, 30 }, 50);
    }
    void pwmCurrent (ProgramTarget& t)
    {
        B b (t); b.single();
        b.osc (1).wave ("Detuned PWM").pitch (0).filter (64, 25, 30).fegInt (30, 40).feg (50, 70, 30, 10, 0, 0, 56, 64, 46)
                 .amp (110, 0, 55).aeg (0, 99, 90, 85, 0, 60, 66, 50).lfo (1, LfoTriangle, 46, 0, 8, 0, 0);
        b.ifx (1, FxEnsemble, { 40, 50, 40, 70, 70 }, 50);
        b.mfx (2, FxHall, { 50, 15, 50, 60, 30 }, 55);
    }
    void tidePluck (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Saw").pitch (0).filter (40, 40, 40).fegInt (60, 30).feg (99, 99, 20, 0, 0, 0, 52, 58, 48)
                 .amp (104, -15, 60).aeg (0, 99, 40, 0, 0, 56, 66, 50);
        b.osc (2).wave ("Glass Choir").pitch (1).filter (70, 10, 20).amp (60, 20, 50).aeg (0, 99, 30, 0, 0, 58, 66, 52);
        b.ifx (1, FxSequenceDelay, { 0, 50, 45, 45, 90 }, 40);
        b.mfx (2, FxWetPlate, { 45, 10, 45, 55, 30 }, 55);
    }
    void gullLead (ProgramTarget& t)
    {
        B b (t); b.dbl(); b.mono (true, 40);
        b.osc (1).wave ("Saw").pitch (0).filter (66, 30, 40).fegInt (24, 30).feg (40, 60, 30, 20, 0, 0, 56, 66, 50)
                 .amp (100, -10, 50).aeg (0, 99, 99, 90, 0, 60, 70, 46).lfo (1, LfoSine, 66, 2, 0, 0, 60);
        b.osc (2).wave ("Square").pitch (0, 0, 8).filter (62, 20, 40).amp (80, 10, 50).aeg (0, 99, 99, 90, 0, 60, 70, 46)
                 .lfo (1, LfoSine, 66, 2, 0, 0, 60);
        b.mfx (1, FxModDelay, { 72, 75, 40, 30, 20 }, 28);
        b.mfx (2, FxHall, { 50, 20, 50, 60, 30 }, 50);
    }
    void dolphinLead (ProgramTarget& t)
    {
        B b (t); b.dbl(); b.mono (true, 30);
        b.osc (1).wave ("Dolphin Whistle").pitch (0).filter (80, 10, 30).amp (100, -10, 40).aeg (0, 99, 99, 95, 10, 60, 70, 50);
        b.osc (2).wave ("Sine").pitch (1).amp (60, 10, 40).aeg (0, 99, 99, 95, 10, 60, 70, 50).lfo (1, LfoSine, 64, 2, 0, 0, 50);
        b.mfx (1, FxModDelay, { 74, 70, 45, 30, 20 }, 32);
        b.mfx (2, FxSmoothHall, { 60, 25, 45, 70, 30 }, 65);
    }
    void deepCurrentBass (ProgramTarget& t)
    {
        B b (t); b.dbl(); b.mono (false, 0);
        b.osc (1).wave ("Saw").pitch (-1).filter (44, 45, 30).fegInt (40, 40).feg (99, 99, 30, 10, 0, 0, 50, 60, 40)
                 .amp (108, 0, 60).aeg (0, 99, 90, 85, 0, 56, 66, 34);
        b.osc (2).wave ("Sine").pitch (-1).amp (96, 0, 50).aeg (0, 99, 99, 95, 0, 56, 66, 34);
        b.volume (110);
    }
    void undertowBass (ProgramTarget& t)
    {
        B b (t); b.single(); b.mono (false, 0);
        b.osc (1).wave ("Square").pitch (-1).filter (50, 50, 30).fegInt (36, 30).feg (90, 99, 20, 0, 0, 0, 52, 60, 40)
                 .amp (112, 0, 60).aeg (0, 99, 90, 80, 0, 56, 66, 36);
        b.ifx (1, FxUndertow, { 45, 30, 20, 40, 40 }, 60);
        b.volume (110);
    }

    //==========================================================================
    // Bell/Mallet, Keyboard, Organ
    //==========================================================================
    void coralBells (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Digital Bell").pitch (0).amp (104, -15, 60).aeg (0, 99, 99, 99, 0, 60, 70, 66);
        b.osc (2).wave ("Glass Bell").pitch (1, 0, 4).amp (80, 15, 60).aeg (0, 99, 99, 99, 0, 60, 70, 66);
        b.mfx (1, FxChorus, { 30, 40, 40, 10, 60 }, 40);
        b.mfx (2, FxWetPlate, { 64, 15, 40, 70, 30 }, 75);
        b.volume (92);
    }
    void harbourKeys (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Tine Keys").pitch (0).filter (80, 0, 30).fegInt (10, 40).amp (108, -10, 65).aeg (0, 99, 99, 99, 0, 60, 70, 52);
        b.osc (2).wave ("Glass Choir").pitch (1).amp (40, 10, 50).aeg (0, 99, 60, 0, 0, 66, 72, 56);
        b.ifx (1, FxChorus, { 34, 45, 40, 10, 70 }, 45);
        b.mfx (2, FxHall, { 50, 15, 50, 60, 30 }, 50);
    }
    void deckOrgan (ProgramTarget& t)
    {
        B b (t); b.single();
        b.osc (1).wave ("Drawbar Organ").pitch (0).amp (104, 0, 20).aeg (0, 99, 99, 99, 0, 40, 40, 30);
        b.ifx (1, FxEnsemble, { 55, 40, 50, 70, 50 }, 40);
        b.ifx (2, FxAutoPan, { 62, 30, 0, 0, 50 }, 100);
        b.mfx (2, FxHall, { 40, 10, 50, 55, 30 }, 40);
    }

    //==========================================================================
    // Water/SE, Hit/Arpg
    //==========================================================================
    void oceanSwell (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Surf Swell").pitch (0).filter (78, 10, 0).amp (104, -20, 20).aeg (0, 99, 99, 95, 64, 70, 74, 72);
        b.osc (2).wave ("Hydrophone").pitch (-1).amp (84, 20, 20).aeg (0, 99, 99, 95, 70, 70, 74, 72);
        b.ifx (1, FxUndertow, { 40, 24, 30, 30, 60 }, 50);
        b.mfx (2, FxSmoothHall, { 70, 30, 50, 75, 30 }, 70);
    }
    void reefSparklePad (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Reef Sparkle").pitch (1).amp (90, -25, 30).aeg (0, 99, 99, 95, 40, 70, 74, 66);
        b.osc (2).wave ("Glass Choir").pitch (0).filter (76, 5, 10).amp (100, 20, 35).aeg (0, 99, 99, 95, 58, 70, 74, 68);
        b.mfx (1, FxModDelay, { 70, 75, 45, 40, 25 }, 32);
        b.mfx (2, FxSmoothHall, { 68, 25, 45, 72, 30 }, 80);
    }
    void whaleCall (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Whale Song").pitch (0).filter (66, 20, 10).amp (104, -15, 30).aeg (0, 99, 99, 95, 66, 70, 74, 74);
        b.osc (2).wave ("Hydrophone").pitch (-1).amp (70, 15, 20).aeg (0, 99, 99, 95, 70, 70, 74, 74);
        b.ifx (1, FxUndertow, { 30, 20, 40, 30, 70 }, 40);
        b.mfx (2, FxSmoothHall, { 80, 40, 55, 85, 30 }, 85);
    }
    void bubbleChoirPad (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Bubble Choir").pitch (0).filter (80, 5, 10).amp (104, -15, 35).aeg (0, 99, 99, 95, 55, 70, 74, 68);
        b.osc (2).wave ("Airy Vox").pitch (0, 0, -7).filter (68, 10, 10).amp (76, 18, 35).aeg (0, 99, 99, 92, 62, 70, 74, 70);
        b.mfx (1, FxChorus, { 30, 45, 40, 10, 60 }, 40);
        b.mfx (2, FxSmoothHall, { 66, 25, 50, 72, 30 }, 80);
    }
    void rainStream (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Stream Flow").pitch (0).filter (80, 10, 0).amp (100, -25, 20).aeg (0, 99, 99, 95, 50, 70, 74, 66);
        b.osc (2).wave ("Bubble Choir").pitch (1).amp (60, 25, 30).aeg (0, 99, 99, 95, 60, 70, 74, 68);
        b.mfx (2, FxHall, { 60, 20, 45, 68, 30 }, 65);
    }
    void sonarArp (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Glass Bell").pitch (0).amp (96, -20, 55).aeg (0, 99, 99, 99, 0, 60, 66, 50);
        b.osc (2).wave ("Sonar Clicks").pitch (1, 0, 0).filter (80, 10, 20).amp (70, 25, 40).aeg (0, 99, 80, 0, 0, 52, 60, 46);
        b.arp (0, 2, 2, 50);
        b.ifx (1, FxSequenceDelay, { 30, 50, 40, 50, 80 }, 35);
        b.mfx (2, FxWetPlate, { 50, 10, 45, 60, 30 }, 60);
        b.volume (72);
    }
    void lagoonArp (ProgramTarget& t)
    {
        B b (t); b.dbl();
        b.osc (1).wave ("Tine Keys").pitch (0).filter (70, 20, 30).fegInt (20, 50).amp (104, -15, 60).aeg (0, 99, 70, 0, 0, 56, 64, 48);
        b.osc (2).wave ("Bamboo Breath").pitch (1).amp (60, 20, 50).aeg (0, 99, 70, 0, 0, 56, 64, 48);
        b.arp (8, 2, 2, 60);
        b.ifx (1, FxSequenceDelay, { 70, 50, 45, 50, 90 }, 40);
        b.mfx (1, FxChorus, { 30, 40, 40, 10, 60 }, 30);
        b.mfx (2, FxHall, { 50, 15, 50, 60, 30 }, 55);
    }
    //--------------------------------------------------------------------------
    // Breezes: gentle airy pads heard through moving band-passes
    //--------------------------------------------------------------------------
    void breakwaterBreeze (ProgramTarget& t)
    {
        // a whistle over the harbour wall, breath an octave above it, both
        // through a cloud of slowly wandering narrow bands
        B b (t); b.dbl();
        b.osc (1).wave ("Breakwater Whistle").pitch (0).filter (78, 25, 15, 1, 30).lfo (1, LfoRandom4, 22, 0, 30, 0, 40)
                 .amp (120, -15, 30).aeg (0, 99, 99, 92, 62, 70, 74, 72);
        b.osc (2).wave ("Noise Spectrum").pitch (1).filter (72, 30, 10, 1, 40).lfo (1, LfoRandom5, 18, 0, -30, 0, 50)
                 .amp (86, 20, 25).aeg (0, 99, 99, 90, 70, 70, 74, 74);
        b.ifx (1, FxBreezeBands, { 25, 55, 50, 60, 70 }, 70);
        b.ifx (2, FxPhaser, { 10, 50, 50, 50, 80 }, 30);
        b.mfx (2, FxSmoothHall, { 72, 30, 50, 80, 30 }, 90);
        b.volume (127);
    }
    void duneGrass (ProgramTarget& t)
    {
        // breath through reeds, the wind combing the grass behind it
        B b (t); b.dbl();
        b.osc (1).wave ("Bamboo Breath").pitch (0).filter (72, 15, 15).lfo (1, LfoSine, 20, 0, 18, 0, 60)
                 .amp (118, -10, 35).aeg (0, 99, 99, 92, 58, 70, 74, 70);
        b.osc (2).wave ("Swish").pitch (0, 12).slope (3).filter (80, 10, 0, 1, 35)
                 .amp (64, 25, 20).aeg (0, 99, 99, 95, 72, 70, 74, 74);
        b.ifx (1, FxBreezeBands, { 35, 70, 60, 50, 90 }, 60);
        b.ifx (2, FxAutoPan, { 8, 40, 30, 0, 50 }, 50);
        b.mfx (1, FxEnsemble, { 25, 45, 40, 55, 90 }, 30);
        b.mfx (2, FxHall, { 70, 25, 50, 75, 30 }, 85);
        b.volume (127);
    }
    void pulseMistral (ProgramTarget& t)
    {
        // two thin pulses turned to whistling air: band-passed, then
        // through many narrow bands that wander apart on the two sides
        B b (t); b.dbl();
        b.osc (1).wave ("Pulse 25%").pitch (0).filter (66, 35, 20, 1, 30).lfo (1, LfoRandom2, 20, 0, 25, 0, 30)
                 .amp (124, -20, 30).aeg (0, 99, 99, 90, 64, 70, 74, 72);
        b.osc (2).wave ("Pulse 10%").pitch (1, 0, 6).filter (74, 40, 20, 1, 38).lfo (1, LfoSine, 16, 0, -28, 0, 40)
                 .amp (100, 20, 30).aeg (0, 99, 99, 90, 68, 70, 74, 74);
        b.ifx (1, FxBreezeBands, { 20, 80, 75, 55, 100 }, 85);
        b.ifx (2, FxEnsemble, { 25, 40, 35, 50, 80 }, 35);
        b.mfx (2, FxWetPlate, { 70, 20, 45, 75, 35 }, 80);
        b.volume (127);
    }
    void seaOatLullaby (ProgramTarget& t)
    {
        // a soft triangle and an airy voice, swaying gently
        B b (t); b.dbl();
        b.osc (1).wave ("Triangle").pitch (0).filter (70, 10, 20).lfo (1, LfoTriangle, 26, 1, 10, 0, 60)
                 .amp (116, -18, 35).aeg (0, 99, 99, 92, 56, 70, 74, 70);
        b.osc (2).wave ("Airy Vox").pitch (1, 0, -5).filter (68, 20, 15).lfo (1, LfoRandom4, 20, 0, 22, 0, 50)
                 .amp (82, 18, 30).aeg (0, 99, 99, 92, 66, 70, 74, 72);
        b.ifx (1, FxBreezeBands, { 15, 45, 40, 65, 50 }, 50);
        b.mfx (1, FxChorus, { 22, 45, 40, 10, 80 }, 35);
        b.mfx (2, FxSmoothHall, { 70, 30, 50, 78, 30 }, 85);
        b.volume (112);
    }
    void zephyrVeil (ProgramTarget& t)
    {
        // a white pad behind a veil of moving bands, a vortex of air beside it
        B b (t); b.dbl();
        b.osc (1).wave ("White Pad").pitch (0).filter (70, 15, 15).lfo (1, LfoRandom5, 24, 0, 20, 0, 40)
                 .amp (110, -22, 35).aeg (0, 99, 99, 94, 64, 70, 74, 72);
        b.osc (2).wave ("Air Vortex").pitch (0, 7).filter (74, 25, 10, 1, 30).lfo (1, LfoRandom4, 16, 0, -26, 0, 50)
                 .amp (76, 22, 30).aeg (0, 99, 99, 92, 70, 70, 74, 74);
        b.ifx (1, FxBreezeBands, { 30, 60, 55, 45, 80 }, 75);
        b.ifx (2, FxRandomFilter, { 8, 45, 30, 55, 90 }, 35);
        b.mfx (2, FxHall, { 74, 30, 55, 80, 30 }, 90);
        b.volume (118);
    }

    void initProgram (ProgramTarget& t)
    {
        B b (t); b.single();
        b.osc (1).wave ("Saw");
    }
}

const std::vector<FactoryProgram>& factoryPrograms()
{
    static const std::vector<FactoryProgram> p {
        { "Dolphin Drift",      CatMotion,    dolphinDrift },
        { "Ember Fifths",       CatMotion,    emberFifths },
        { "Hydrophonics",       CatMotion,    hydrophonics },
        { "Swirling Shoals",    CatMotion,    swirlingShoals },
        { "Sonar Drift",        CatMotion,    sonarDrift },
        { "Tide Turner",        CatMotion,    tideTurner },
        { "Glass Lagoon",       CatVocal,     glassLagoon },
        { "Ooh Tide Choir",     CatVocal,     oohTideChoir },
        { "Moonlit Choir",      CatVocal,     moonlitChoir },
        { "Siren Voices",       CatVocal,     sirenVoices },
        { "Whisper Waves",      CatVocal,     whisperWaves },
        { "Breezy Air Pad",     CatSlowSynth, breezyAirPad },
        { "Bamboo Breeze",      CatSlowSynth, bambooBreeze },
        { "Haze Current",       CatSlowSynth, hazeCurrent },
        { "Seafoam Strings",    CatStrings,   seafoamStrings },
        { "Lighthouse Strings", CatStrings,   lighthouseStrings },
        { "Super Surf",         CatFastSynth, superSurf },
        { "PWM Current",        CatFastSynth, pwmCurrent },
        { "Tide Pluck",         CatFastSynth, tidePluck },
        { "Gull Lead",          CatLead,      gullLead },
        { "Dolphin Lead",       CatLead,      dolphinLead },
        { "Deep Current Bass",  CatBass,      deepCurrentBass },
        { "Undertow Bass",      CatBass,      undertowBass },
        { "Coral Bells",        CatBell,      coralBells },
        { "Harbour Keys",       CatKeyboard,  harbourKeys },
        { "Deck Organ",         CatOrgan,     deckOrgan },
        { "Ocean Swell",        CatWater,     oceanSwell },
        { "Reef Sparkle",       CatWater,     reefSparklePad },
        { "Whale Call",         CatWater,     whaleCall },
        { "Bubble Choir Pad",   CatWater,     bubbleChoirPad },
        { "Rain Stream",        CatWater,     rainStream },
        { "Sonar Arp",          CatArp,       sonarArp },
        { "Lagoon Arp",         CatArp,       lagoonArp },
        { "Breakwater Breeze",   CatBreeze,    breakwaterBreeze },
        { "Dune Grass",          CatBreeze,    duneGrass },
        { "Pulse Mistral",       CatBreeze,    pulseMistral },
        { "Sea Oat Lullaby",     CatBreeze,    seaOatLullaby },
        { "Zephyr Veil",         CatBreeze,    zephyrVeil },
        { "Init Preset",        CatSlowSynth, initProgram },
    };
    return p;
}

//==============================================================================
// random programs
//==============================================================================
namespace
{
    using Pick = std::vector<const char*>;
    template <typename T> T pickOne (Rng& r, const std::vector<T>& v) { return v[(size_t) r.between (0, (int) v.size() - 1)]; }
    int pickFx (Rng& r, std::initializer_list<int> types) { std::vector<int> v (types); return pickOne (r, v); }

    // musical settings of an effect's five parameters (0..100); slow = slow modulation
    std::array<int, 5> fxSettings (Rng& r, int type, bool slow, bool wild)
    {
        std::array<int, 5> p {};
        for (auto& v : p) v = wild ? r.between (0, 100) : r.between (30, 70);
        switch (type)
        {
            case FxPhaser:         p = { slow ? r.between (4, 28) : r.between (15, 55), r.between (45, 90), r.between (25, 65), r.between (35, 70), r.between (60, 100) }; break;
            case FxChorus:         p = { r.between (15, 45), r.between (30, 70), r.between (25, 60), r.between (0, 30), r.between (50, 100) }; break;
            case FxEnsemble:       p = { r.between (20, 45), r.between (35, 70), r.between (20, 60), r.between (40, 75), r.between (60, 100) }; break;
            case FxHarmonicChorus: p = { r.between (20, 45), r.between (35, 70), r.between (30, 60), r.between (0, 30), r.between (50, 100) }; break;
            case FxFlanger:        p = { slow ? r.between (4, 25) : r.between (15, 45), r.between (30, 80), r.between (20, 60), r.between (30, 65), r.between (50, 100) }; break;
            case FxModDelay:       p = { r.between (40, 75), r.between (10, 90), r.between (20, 55), r.between (20, 60), r.between (15, 45) }; break;
            case FxSequenceDelay:  p = { r.between (0, 100), r.between (30, 70), r.between (25, 55), r.between (30, 60), r.between (60, 100) }; break;
            case FxMultitap:       p = { r.between (50, 80), r.between (20, 45), r.between (25, 55), r.between (15, 40), r.between (50, 90) }; break;
            case FxRandomFilter:   p = { slow ? r.between (3, 25) : r.between (15, 50), r.between (35, 80), r.between (20, 60), r.between (30, 70), slow ? r.between (60, 100) : r.between (20, 90) }; break;
            case FxAutoPan:        p = { slow ? r.between (5, 30) : r.between (20, 55), r.between (30, 80), r.between (0, 100), 0, r.between (40, 60) }; break;
            case FxDecimator:      p = { r.between (55, 85), r.between (55, 85), r.between (60, 100), r.between (0, 30), 0 }; break;
            case FxTiltEq:         p = { r.between (40, 62), r.between (45, 70), r.between (40, 65), r.between (0, 40), r.between (45, 70) }; break;
            case FxHall: case FxSmoothHall: case FxWetPlate:
                                   p = { r.between (45, wild ? 88 : 78), r.between (5, 40), r.between (30, 60), r.between (50, 85), r.between (0, 35) }; break;
            case FxUndertow:       p = { r.between (30, 75), slow ? r.between (5, 35) : r.between (20, 60), r.between (10, 50), r.between (20, 60), r.between (20, 70) }; break;
            case FxBreezeBands:    p = { slow ? r.between (5, 30) : r.between (20, 55), r.between (35, 85), r.between (30, 75), r.between (30, 70), r.between (40, 100) }; break;
            default: break;
        }
        return p;
    }
    void insert (B& b, Rng& r, int slot, int type, bool slow, bool wild, int mixLo, int mixHi)
    {
        const auto p = fxSettings (r, type, slow, wild);
        int mix = r.between (mixLo, mixHi);
        if (type == FxDecimator) mix = std::min (mix, 45);
        if (type == FxTiltEq) mix = 100;
        b.ifx (slot, type, { p[0], p[1], p[2], p[3], p[4] }, mix);
    }
    void send (B& b, Rng& r, int slot, int type, bool wild, int sendLo, int sendHi)
    {
        const auto p = fxSettings (r, type, false, wild);
        b.mfx (slot, type, { p[0], p[1], p[2], p[3], p[4] }, r.between (sendLo, sendHi));
    }

    // waves that are a texture (clicks, sparkle, swishes, streams) rather than a
    // body of tone: they sit behind the layer that plays the notes
    bool isTexture (const juce::String& name)
    {
        for (auto* n : { "Sonar Clicks", "Dolphin Clicks", "Swish", "Reef Sparkle", "Stream Flow" }) if (name == n) return true;
        return false;
    }
    bool isTextureArchetype (int a) { return a == ArchClicks || a == ArchBubbles; }

    // slow filter movement on a layer: a slow (or bar-long) LFO on the cutoff,
    // or a long filter envelope sweep - with room above the cutoff to move in
    void slowFilterMotion (B::Osc& osc, Rng& r, int lfoSlot, int& cutoff)
    {
        static const int shapes[] = { LfoTriangle, LfoSine, LfoRandom2, LfoRandom4, LfoExpTri, LfoRandom5 };
        if (r.chance (0.65f))
        {
            const int depth = (r.chance (0.5f) ? 1 : -1) * r.between (22, 52);
            osc.lfo (lfoSlot, shapes[r.between (0, 5)], r.between (4, 30), 0, depth, 0, r.between (0, 40), r.chance (0.5f));
            if (r.chance (0.3f)) osc.lfoSync (lfoSlot, r.between (9, 11));      // 2, 4 or 8 bars
            cutoff = juce::jlimit (40, 82, cutoff + (depth < 0 ? 8 : -4));
        }
        else
        {
            const int up = r.between (25, 50);
            osc.fegInt (r.chance (0.8f) ? up : -up, r.between (0, 25));
            osc.feg (r.between (-40, 10), r.between (60, 99), r.between (20, 80), r.between (0, 50), r.between (-20, 10),
                     r.between (66, 84), r.between (72, 86), r.between (74, 88), r.between (62, 76));
            cutoff = juce::jlimit (38, 76, cutoff - 6);
        }
    }

    const char* const seaWords[] = { "Tidal", "Coral", "Lagoon", "Drift", "Azure", "Kelp", "Harbour", "Saltwater", "Moon", "Reef",
                                     "Pearl", "Seaglass", "Current", "Shoal", "Brine", "Mist", "Foam", "Undersea", "Spindrift", "Cove" };
}

juce::String randomProgram (ProgramTarget& t, int category, juce::uint32 seed)
{
    if (category == kSuperRandom) return superRandomProgram (t, seed);
    Rng r (seed * 2246822519u + (juce::uint32) category * 31u);
    if (category < 0 || category >= NumCategories) category = r.between (0, NumCategories - 1);
    B b (t);

    // the waves a category draws from: a body that plays the notes, and the
    // textures that may join it; archetypes for freshly rolled waves of each
    struct Pool { Pick body, texture; std::vector<int> bodyArch, textureArch; };
    const Pool pool = [&] () -> Pool
    {
        switch (category)
        {
            case CatKeyboard:  return { { "Tine Keys", "Glass Bell", "Glass Choir", "Triangle" }, { "Reef Sparkle" }, { ArchDigitalBell }, {} };
            case CatOrgan:     return { { "Drawbar Organ", "Square", "Sine", "Pulse 25%" }, {}, {}, {} };
            case CatBell:      return { { "Digital Bell", "Glass Bell", "Tine Keys" }, { "Reef Sparkle" }, { ArchDigitalBell }, { ArchBubbles } };
            case CatStrings:   return { { "Ensemble Strings", "Analog Strings", "Dream Strings", "White Pad" }, {}, { ArchStrings }, {} };
            case CatVocal:     return { { "Airy Vox", "Glass Choir", "Soft Ah", "Choir Ooh", "Breakwater Whistle", "Noise Spectrum" }, { "Stream Flow" }, { ArchVoice, ArchGlassVox, ArchGhost, ArchAirPad }, {} };
            case CatSlowSynth: return { { "Airy Vox", "White Pad", "Analog Strings", "Bamboo Breath", "Air Vortex", "Dream Strings" }, { "Swish" }, { ArchAirPad, ArchBreathFlute, ArchStrings }, {} };
            case CatFastSynth: return { { "Super Saw", "Detuned PWM", "Saw", "Square", "Pulse 10%" }, {}, { ArchSuperSaw }, {} };
            case CatLead:      return { { "Saw", "Square", "Pulse 25%", "Dolphin Whistle", "Breakwater Whistle" }, {}, { ArchSuperSaw, ArchWhale }, {} };
            case CatMotion:    return { { "Tide Sweep", "Bamboo Breath", "Glass Choir", "Soft Ah", "Airy Vox", "Air Vortex", "Noise Spectrum", "Dream Strings", "White Pad" },
                                        { "Sonar Clicks", "Dolphin Clicks", "Swish", "Reef Sparkle", "Stream Flow" },
                                        { ArchWaveSweep, ArchBreathFlute, ArchNoiseSpectrum, ArchAirPad, ArchGlassVox }, { ArchClicks, ArchBubbles } };
            case CatBass:      return { { "Saw", "Square", "Sine", "Detuned PWM", "Hydrophone" }, {}, { ArchSuperSaw }, {} };
            case CatWater:     return { { "Surf Swell", "Whale Song", "Bubble Choir", "Hydrophone", "Dolphin Whistle", "Airy Vox", "Soft Ah", "White Pad" },
                                        { "Stream Flow", "Reef Sparkle", "Dolphin Clicks", "Sonar Clicks" },
                                        { ArchSurf, ArchWhale, ArchAirPad }, { ArchBubbles, ArchClicks } };
            case CatBreeze:    return { { "Airy Vox", "White Pad", "Bamboo Breath", "Air Vortex", "Breakwater Whistle", "Noise Spectrum", "Soft Ah", "Pulse 25%" },
                                        { "Swish", "Stream Flow" },
                                        { ArchAirPad, ArchBreathFlute, ArchNoiseSpectrum, ArchGhost }, {} };
            default:           return { { "Tine Keys", "Glass Bell", "Saw", "Bamboo Breath", "Digital Bell" }, { "Sonar Clicks", "Dolphin Clicks" }, { ArchDigitalBell }, { ArchClicks } };
        }
    }();
    // returns true when the layer got a texture
    auto pickWave = [&] (B::Osc osc, bool texture, float rollChance)
    {
        const auto& names = texture ? pool.texture : pool.body;
        const auto& arch = texture ? pool.textureArch : pool.bodyArch;
        if (! arch.empty() && r.chance (rollChance)) osc.waveIndex (t.rollWave (pickOne (r, arch), (juce::uint32) r.next()));
        else osc.wave (pickOne (r, names));
    };

    const bool breeze = category == CatBreeze;
    const bool pad = category == CatSlowSynth || category == CatVocal || category == CatStrings || category == CatMotion || category == CatWater || breeze;
    const bool percussive = category == CatKeyboard || category == CatBell || category == CatArp;
    const bool dbl = category == CatOrgan ? r.chance (0.3f) : (category == CatBass || category == CatLead ? r.chance (0.6f) : (breeze ? true : r.chance (0.85f)));
    if (dbl) b.dbl(); else b.single();
    if (category == CatBass || category == CatLead) b.mono (category == CatLead ? r.chance (0.7f) : r.chance (0.4f), category == CatLead ? r.between (0, 50) : 0);

    // the extras of this roll: slow filter movement, more effects
    const float richness = pad ? 0.6f : 0.35f;
    const bool slowFilter = r.chance (richness);
    const bool moreFx = r.chance (pad ? 0.55f : 0.4f);

    const bool arp = category == CatArp || (category == CatMotion && r.chance (0.35f));

    // which layer is a texture: in the moving and sea categories often the second
    const float textureChance = category == CatMotion ? 0.5f : (category == CatWater ? 0.45f : (breeze ? 0.35f : 0.2f));
    const bool textureB = dbl && ! pool.texture.empty() && r.chance (textureChance);

    const int oscs = dbl ? 2 : 1;
    for (int o = 1; o <= oscs; ++o)
    {
        auto osc = b.osc (o);
        const bool texture = o == 2 && textureB;
        pickWave (osc, texture, category == CatMotion || category == CatWater ? 0.4f : 0.25f);

        // pitch: the second layer an octave, a fifth or a detune away
        int octave = category == CatBass ? -1 : 0, transpose = 0, tune = 0;
        if (o == 2)
        {
            const int choice = r.between (0, 9);
            if (choice < 3) tune = r.between (-12, 12);
            else if (choice < 5) octave += 1;
            else if (choice < 6) octave -= 1;
            else if (choice < 8) transpose = 7;
            else { transpose = r.chance (0.5f) ? 12 : 5; tune = r.between (-8, 8); }
            if (texture && r.chance (0.5f)) { transpose = r.chance (0.5f) ? 12 : 7; tune = r.chance (0.5f) ? 1200 : 0; }
        }
        osc.pitch (octave, transpose, tune);
        if (texture && ! arp && r.chance (0.5f)) osc.delay (r.between (100, 900));
        if (texture && r.chance (0.25f)) osc.slope (r.between (0, 5));          // a texture that barely follows the keys
        if ((category == CatMotion || category == CatWater) && ! texture && r.chance (0.25f)) osc.reverse();
        if (r.chance (pad ? 0.3f : 0.1f)) osc.offset();

        // filter and its envelope (a body keeps its tone: no filter closing it down to nothing)
        int cutoff = category == CatBass ? r.between (36, 58) : (percussive ? r.between (60, 90) : r.between (55, 85));
        if (texture) cutoff = r.between (68, 92);
        const int reso = category == CatMotion ? r.between (10, 55) : r.between (0, 35);
        const int egInt = category == CatBass || category == CatFastSynth || percussive ? r.between (15, 60) : r.between (-30, 45);
        osc.fegInt (egInt, r.between (0, 45));
        if (pad) osc.feg (r.between (-30, 99), r.between (0, 99), r.between (-10, 60), r.between (-10, 30), 0,
                          r.between (20, 72), r.between (55, 74), r.between (60, 78), r.between (55, 70));
        else osc.feg (r.between (60, 99), 99, r.between (0, 40), r.between (-10, 20), 0, 0, r.between (45, 60), r.between (55, 68), r.between (40, 56));
        if (slowFilter && ! texture && ! breeze && (o == 1 || r.chance (0.6f))) slowFilterMotion (osc, r, 2, cutoff);
        // (a bass keeps its fundamental: no high-pass)
        const bool hp = category != CatBass && r.chance (0.25f);
        if (breeze)
        {
            // a band-pass of its own (low-pass and high-pass about one and a half
            // octaves apart), its upper edge swaying on a slow random LFO
            // (the LFO sways the upper edge by at most half the band's width, so
            // the band never closes)
            // (the high-pass stays below ~480 Hz: a pad keeps the body it has there)
            const int top = r.between (64, 80), width = r.between (22, 34);
            osc.filter (top, r.between (15, 40), r.between (10, 30), 1, juce::jlimit (0, 42, top - width));
            static const int drifts[] = { LfoRandom2, LfoRandom4, LfoRandom5, LfoSine };
            osc.lfo (2, drifts[r.between (0, 3)], r.between (10, 28), 0, (r.chance (0.5f) ? 1 : -1) * r.between (14, width), 0, r.between (20, 60), false);
        }
        else
            osc.filter (cutoff, reso, r.between (0, 45), hp ? 1 : 0, hp ? r.between (0, texture ? 50 : 28) : 0);

        // amp: the body full, a second body a little under it, a texture well behind
        const int pan = dbl ? (o == 1 ? -r.between (5, 35) : r.between (5, 35)) : 0;
        const int level = o == 1 ? (category == CatBass || category == CatLead ? r.between (112, 124) : r.between (102, 114)) : (texture ? r.between (52, 78) : r.between (80, 102));
        osc.amp (level, pan, percussive ? r.between (45, 70) : r.between (25, 50));
        if (pad && arp) osc.aeg (0, 99, 99, r.between (80, 99), r.between (0, 24), 70, 74, r.between (50, 66));   // arpeggiated: the notes must speak
        else if (pad) osc.aeg (0, 99, 99, r.between (80, 99), r.between (40, 68), 70, 74, r.between (60, 74));
        else if (percussive) osc.aeg (0, 99, r.between (40, 90), r.between (0, 40), 0, r.between (54, 66), r.between (60, 72), r.between (46, 60));
        else osc.aeg (0, 99, r.between (80, 99), r.between (70, 95), r.between (0, 20), r.between (50, 64), r.between (60, 70), r.between (34, 52));

        // movement
        if (r.chance (pad ? 0.7f : 0.35f))
        {
            static const int shapes[] = { LfoTriangle, LfoSine, LfoRandom2, LfoRandom4, LfoStepTri4, LfoExpTri };
            osc.lfo (1, shapes[r.between (0, 5)], r.between (24, 50), r.chance (0.3f) ? r.between (1, 3) : 0,
                     r.between (-25, 25), r.chance (0.2f) ? r.between (-20, 20) : 0, r.between (0, 50));
        }
        if (category == CatMotion && ! slowFilter && r.chance (0.5f))
        {
            osc.lfo (2, r.chance (0.5f) ? LfoStepTri6 : LfoRandom1, 60, 0, r.between (-30, 30), 0, 0);
            osc.lfoSync (2, r.between (2, 6));
        }
        if (category == CatLead) osc.lfo (1, LfoSine, r.between (60, 68), r.between (1, 3), 0, 0, r.between (40, 60));
    }

    // effects: one or two inserts, a modulation send and a reverb send - and
    // on a rich roll the inserts filled up, often with something slow
    int nextInsert = 1;
    if (breeze)
    {
        b.ifx (nextInsert++, FxBreezeBands, { r.between (10, 40), r.between (40, 85), r.between (35, 75), r.between (35, 70), r.between (50, 100) }, r.between (55, 85));
        if (r.chance (0.6f)) insert (b, r, nextInsert++, pickFx (r, { FxPhaser, FxRandomFilter, FxAutoPan, FxEnsemble }), true, false, 25, 45);
    }
    else if (category == CatMotion || category == CatWater || r.chance (0.5f))
    {
        const int type = category == CatWater ? pickFx (r, { FxUndertow, FxPhaser, FxModDelay })
                                              : pickFx (r, { FxPhaser, FxModDelay, FxSequenceDelay, FxRandomFilter, FxAutoPan, FxEnsemble });
        insert (b, r, nextInsert++, type, false, false, 30, 60);
    }
    if (category == CatMotion && r.chance (0.6f))
        insert (b, r, nextInsert++, pickFx (r, { FxSequenceDelay, FxPhaser, FxModDelay, FxDecimator }), false, false, 25, 45);
    if (moreFx)
    {
        // a slow filter / phase / pan movement, and maybe one more colour
        insert (b, r, nextInsert++, pickFx (r, { FxRandomFilter, FxRandomFilter, FxPhaser, FxUndertow, FxAutoPan, FxFlanger, FxBreezeBands }), true, false, 35, 65);
        while (nextInsert <= 3 && r.chance (0.65f))
            insert (b, r, nextInsert++, pickFx (r, { FxEnsemble, FxChorus, FxModDelay, FxSequenceDelay, FxTiltEq, FxPhaser, FxDecimator, FxRandomFilter }), r.chance (0.5f), false, 20, 50);
    }
    if (arp) b.arp (r.between (0, 11), pad ? r.between (3, 6) : r.between (2, 4), r.between (1, 3), pad ? r.between (60, 95) : r.between (40, 80));
    send (b, r, 1, pickFx (r, { FxChorus, FxEnsemble, FxHarmonicChorus, FxMultitap, FxModDelay }), false, 20, moreFx ? 70 : 60);
    send (b, r, 2, pickFx (r, { FxSmoothHall, FxWetPlate, FxHall }), false, pad ? 60 : 35, pad ? 95 : 65);
    // one layer plays softer than two; struck sounds and arpeggios peak higher
    // (breezes give up some level to their band-passes: a little more volume)
    b.volume (category == CatBell ? (dbl ? 72 : 82) : (percussive ? (dbl ? 86 : 98) : (breeze ? 118 : (dbl ? 100 : 116))));

    // a name from the category's words
    static const char* second[NumCategories][4] = {
        { "Keys", "Tines", "Ivories", "Piano" }, { "Organ", "Bars", "Drawbars", "Chapel" }, { "Bells", "Chimes", "Mallets", "Glass" },
        { "Strings", "Ensemble", "Bows", "Section" }, { "Voices", "Choir", "Airs", "Breath" }, { "Pad", "Haze", "Bloom", "Horizon" },
        { "Saws", "Stabs", "Pulse", "Rush" }, { "Lead", "Call", "Glide", "Cry" }, { "Current", "Sweep", "Ride", "Swirl" },
        { "Bass", "Depths", "Low", "Keel" }, { "Swell", "Shore", "Spray", "Deep" }, { "Arp", "Ripples", "Steps", "Pings" },
        { "Breeze", "Zephyr", "Gust", "Veil" } };
    juce::String name = juce::String (seaWords[r.between (0, 19)]) + " " + second[category][r.between (0, 3)];
    if (name.startsWith ("Current Current")) name = "Current Sweep";
    return name;
}

//==============================================================================
// Super Random: every part of the program rolled over its whole range, with
// the few guards that keep it a sound - something plays, its envelope opens,
// the filter lets it through, the feedback stays below a howl, the level sane
//==============================================================================
juce::String superRandomProgram (ProgramTarget& t, juce::uint32 seed)
{
    Rng r (seed * 3266489917u + 0x5eedu);
    B b (t);
    const bool dbl = r.chance (0.85f);
    if (dbl) b.dbl(); else b.single();
    if (r.chance (0.15f)) b.mono (r.chance (0.6f), r.chance (0.5f) ? r.between (10, 45) : 0);
    else if (r.chance (0.15f)) b.set ("porta", (float) r.between (5, 35));
    b.set ("bend", (float) r.between (1, 12));

    auto anyWave = [&] () -> int
    {
        if (r.chance (0.45f)) return t.rollWave (r.between (0, NumArchetypes - 1), (juce::uint32) r.next());
        return r.between (0, numFactoryWaves() - 1);
    };
    for (int o = 1; o <= (dbl ? 2 : 1); ++o)
    {
        auto osc = b.osc (o);
        const int hi = anyWave();
        osc.waveIndex (hi);
        if (r.chance (0.3f)) { osc.set ("loWave", (float) anyWave()); osc.set ("velSw", (float) r.between (40, 105)); }
        const bool texture = hi < numFactoryWaves() && isTexture (factoryWaves()[(size_t) hi].name);

        static const int intervals[] = { 0, 0, 0, 7, 12, -12, 5, -5, 3, 4, 10, -7 };
        osc.pitch (r.between (-1, 1), o == 1 && r.chance (0.6f) ? 0 : intervals[r.between (0, 11)],
                   r.chance (0.6f) ? 0 : (r.chance (0.85f) ? r.between (-30, 30) : (r.chance (0.5f) ? 1200 : -1200)));
        if (r.chance (0.15f)) osc.delay (r.between (40, 1500));
        if (r.chance (0.1f)) osc.slope (r.between (0, 8));
        if (r.chance (0.15f)) osc.reverse();
        if (r.chance (0.3f)) osc.offset();
        if (r.chance (0.15f)) osc.set ("pegInt", (float) r.between (-5, 5));

        // filter: anywhere, but never shut - where the envelope holds it (its
        // sustain), the cutoff stays open enough to hear the wave
        const int type = r.chance (0.35f) ? 1 : 0;
        int cutoff = r.between (40, 95);
        const int reso = r.between (0, cutoff < 50 ? 55 : 80);
        const int egInt = r.between (-60, 70);
        const int fegSustain = r.between (-60, 60);
        const int keytrack = r.between (-10, 80);
        const int held = cutoff + egInt * fegSustain / 99;
        if (held < 56) cutoff = std::min (99, cutoff + (56 - held));
        osc.filter (cutoff, reso, keytrack, type, type == 1 ? r.between (0, 40) : 0);
        osc.fegInt (egInt, r.between (0, 50), type == 1 ? r.between (-30, 30) : 0);
        osc.feg (r.between (-99, 99), r.between (-99, 99), r.between (-99, 99), fegSustain, r.between (-50, 50),
                 r.between (0, 85), r.between (20, 85), r.between (20, 88), r.between (20, 80));
        const bool slow = r.chance (0.5f);
        if (slow) slowFilterMotion (osc, r, 2, cutoff);
        osc.set ("fA", (float) cutoff);

        // amp: opens, holds something, comes back
        osc.amp (o == 1 ? r.between (98, 116) : (texture ? r.between (50, 76) : r.between (70, 108)), r.between (-55, 55), r.between (0, 60));
        osc.aeg (r.between (0, 50), r.between (75, 99), r.between (50, 99), r.between (52, 99),
                 r.between (0, 78), r.between (30, 80), r.between (40, 84), r.between (30, 76));

        // the LFOs: any shape and speed; pitch mostly gentle
        for (int l = 1; l <= (slow ? 1 : 2); ++l)
        {
            if (! r.chance (0.6f)) continue;
            const int pitch = r.chance (0.7f) ? 0 : (r.chance (0.7f) ? r.between (-5, 5) : r.between (-60, 60));
            osc.lfo (l, r.between (0, NumLfoShapes - 1), r.between (5, 75), pitch, r.between (-60, 60),
                     r.chance (0.3f) ? r.between (-40, 40) : 0, r.between (0, 70), r.chance (0.6f));
            if (r.chance (0.25f)) osc.lfoSync (l, r.between (0, 11));
            if (r.chance (0.3f)) osc.set (l == 1 ? "l1Delay" : "l2Delay", (float) r.between (20, 70));
        }
    }
    if (r.chance (0.25f))
        b.peg (r.between (-40, 40), r.between (-40, 40), r.between (-10, 10), r.between (0, 60), r.between (20, 70), r.between (20, 60));

    // every effect slot may be anything
    for (int n = 1; n <= 3; ++n)
        if (r.chance (n == 1 ? 0.85f : 0.65f))
            insert (b, r, n, r.between (1, NumEffectTypes - 1), r.chance (0.5f), true, 15, 70);
    if (r.chance (0.85f)) send (b, r, 1, pickFx (r, { FxChorus, FxEnsemble, FxHarmonicChorus, FxFlanger, FxModDelay, FxSequenceDelay, FxMultitap, FxPhaser, FxAutoPan }), true, 10, 80);
    if (r.chance (0.9f)) send (b, r, 2, pickFx (r, { FxHall, FxSmoothHall, FxWetPlate }), true, 20, 100);
    if (r.chance (0.2f)) b.arp (r.between (0, 11), r.between (0, 6), r.between (1, 4), r.between (20, 100), r.chance (0.3f) ? r.between (60, 127) : 0);
    if (r.chance (0.4f)) b.eq (r.between (-5, 5), r.between (-4, 4), r.between (300, 5000), r.between (-5, 5));
    b.volume (r.between (108, 118));

    static const char* wild[] = { "Riptide", "Maelstrom", "Rogue Swell", "Squall", "Whirlpool", "Sea Fret", "Breaker", "Storm Surge",
                                  "Spindrift", "Brackish", "Flotsam", "Jetsam", "Kraken", "Waterspout", "Crosscurrent", "Deep Trench" };
    return juce::String (wild[r.between (0, 15)]) + " " + juce::String (r.between (100, 999));
}
}
