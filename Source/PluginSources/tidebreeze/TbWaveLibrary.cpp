#include "TbWaves.h"

//==============================================================================
// Tidebreeze - the factory waves, and the archetypes new waves are rolled from
//
// The first group was modelled on measurements of a classic workstation's
// synth multisamples (spectra, breath, beating and loop rates at C4) - the
// numbers, not the recordings. The others are designed in the same spirit,
// and the water ones borrow Soundscape Ecology's bubble physics.
//==============================================================================
namespace tb
{
namespace
{
    void setHarm (WaveSpec& s, std::initializer_list<float> db)
    {
        int i = 0;
        for (auto v : db) if (i < 32) s.harm[i++] = v;
        for (; i < 32; ++i) s.harm[i] = (i > 0 ? s.harm[i - 1] : 0.0f) + s.tiltDbOct * std::log2 ((float) (i + 1) / (float) std::max (1, i));
    }
    // a spectrum falling at slope dB per octave from partial 1 (saw: -6, triangle-ish: -12)
    void slope (WaveSpec& s, float dbPerOct, float evenDb = 0.0f)
    {
        for (int k = 1; k <= 32; ++k) s.harm[k - 1] = dbPerOct * std::log2 ((float) k);
        s.tiltDbOct = dbPerOct;
        s.evenDb = evenDb;
    }
    void vowel (WaveSpec& s, std::initializer_list<float> hz, std::initializer_list<float> bw, std::initializer_list<float> db)
    {
        int i = 0; for (auto v : hz) s.formHz[i++] = v; s.numFormants = i;
        i = 0; for (auto v : bw) s.formBw[i++] = v;
        i = 0; for (auto v : db) s.formDb[i++] = v;
    }
    void vowelB (WaveSpec& s, std::initializer_list<float> hz, std::initializer_list<float> bw, std::initializer_list<float> db, float cycles)
    {
        int i = 0; for (auto v : hz) s.formBHz[i++] = v;
        i = 0; for (auto v : bw) s.formBBw[i++] = v;
        i = 0; for (auto v : db) s.formBDb[i++] = v;
        s.formMorphCycles = cycles;
    }
    void sweep (WaveSpec& s, float from, float to, float widthOct, float gainDb, float cycles, int shape, float phase = 0.0f)
    {
        if (s.numSweeps >= 2) return;
        auto& w = s.sweep[s.numSweeps++];
        w.fromHz = from; w.toHz = to; w.widthOct = widthOct; w.gainDb = gainDb; w.cycles = cycles; w.shape = shape; w.phase = phase;
    }

    //==========================================================================
    // modelled on measurements
    //==========================================================================
    WaveSpec airyVox()
    {
        WaveSpec s; s.seed = 101;
        s.tiltDbOct = -3.0f;
        setHarm (s, { 0, -4, -14, -31, -21, -36, -48, -36, -39, -34, -33, -35, -41, -32, -35, -45,
                      -38, -40, -42, -41, -43, -44, -43, -45, -46, -46, -47, -48, -48, -49, -49, -50 });
        s.copies = 3; s.detuneCents = 13.0f;           // gentle beating, ~2 Hz on the fundamental at C4
        s.vibRateHz = 4.8f; s.vibCents = 2.5f;
        s.noiseDb = -19.0f; s.noiseLowHz = 300.0f; s.noiseHighHz = 16000.0f; s.noiseTiltDbOct = 1.0f;
        s.noiseFocus = 0.55f; s.noiseFocusBw = 0.12f; s.noiseFormants = false;
        s.attackSec = 0.5f; s.loopSec = 2.36f; s.attackRise = 0.04f;
        return s;
    }
    WaveSpec glassChoir()
    {
        WaveSpec s; s.seed = 102;
        s.tiltDbOct = -14.0f;
        setHarm (s, { 0, -32, -19, -50, -60, -61, -45, -58, -56, -69, -85, -79, -89, -91, -94, -99 });
        s.copies = 2; s.detuneCents = 5.5f;            // a slow 1.7 Hz beating at C4
        s.vibRateHz = 4.0f; s.vibCents = 3.0f;
        s.driftCents = 2.0f; s.driftRateHz = 0.4f;
        s.noiseDb = -37.0f; s.noiseLowHz = 60.0f; s.noiseHighHz = 9000.0f; s.noiseFocus = 0.6f; s.noiseFocusBw = 0.08f;
        s.noiseTiltDbOct = -4.0f;
        s.attackSec = 0.5f; s.loopSec = 2.4f; s.attackRise = 0.06f;
        return s;
    }
    WaveSpec softAh()
    {
        WaveSpec s; s.seed = 103;
        s.tiltDbOct = -6.0f;
        slope (s, -6.0f);
        // a pop "ah": a strong low body and F1/F2 around 0.7-1.1 kHz, F3 at 2.5 kHz,
        // fixed in Hz over octave zones (the way voices are multisampled)
        vowel (s, { 280, 720, 1150, 2600 }, { 360, 220, 300, 600 }, { 0, -1, -3, -9 });
        s.formFloorDb = -24.0f;
        s.zoneStepSemis = 12; s.zoneLowNote = 48.0f;
        s.copies = 3; s.detuneCents = 9.0f;
        s.vibRateHz = 5.2f; s.vibCents = 4.0f; s.driftCents = 3.0f; s.driftRateHz = 0.5f;
        s.noiseDb = -24.0f; s.noiseLowHz = 50.0f; s.noiseHighHz = 9000.0f; s.noiseFocus = 0.45f; s.noiseFocusBw = 0.1f;
        s.attackSec = 0.45f; s.loopSec = 2.0f; s.attackRise = 0.08f; s.scoopCents = -25.0f; s.scoopSec = 0.12f;
        return s;
    }
    WaveSpec bambooBreath()
    {
        WaveSpec s; s.seed = 104;
        s.tiltDbOct = -6.0f;
        setHarm (s, { 0, -22, -12, -48, -43, -60, -53, -63, -58, -68, -67, -70, -70, -71, -72, -73 });
        s.copies = 1;
        s.vibRateHz = 0.7f; s.vibCents = 18.0f;        // the scooping bends of each loop cycle
        s.driftCents = 6.0f; s.driftRateHz = 1.4f;
        s.tiltDbOct = -12.0f;
        s.noiseDb = -14.0f; s.noiseLowHz = 240.0f; s.noiseHighHz = 1600.0f; s.noiseTiltDbOct = -6.0f;
        s.noiseFocus = 0.3f; s.noiseFocusBw = 0.25f; s.noiseFormants = false;
        s.noiseAmRateHz = 0.7f; s.noiseAmDb = 6.0f;
        s.levelSwellCycles = 2.0f; s.levelSwellDb = 13.0f;
        s.attackSec = 0.4f; s.loopSec = 1.43f; s.attackRise = 0.05f; s.scoopCents = -57.0f; s.scoopSec = 0.1f;
        s.attackNoiseDb = -6.0f; s.attackNoiseSec = 0.06f;
        return s;
    }
    WaveSpec tideSweep()
    {
        WaveSpec s; s.seed = 105;
        slope (s, -9.0f);
        for (auto& h : s.harm) h -= 10.0f;
        sweep (s, 300.0f, 2600.0f, 0.35f, 18.0f, 1.0f, 1);
        sweep (s, 600.0f, 3400.0f, 0.3f, 12.0f, 3.0f, 0, 0.45f);
        s.copies = 1;
        s.noiseDb = -34.0f; s.noiseLowHz = 300.0f; s.noiseHighHz = 10000.0f;
        s.attackSec = 0.35f; s.loopSec = 1.44f; s.attackRise = 0.02f;
        return s;
    }
    WaveSpec sonarClicks()
    {
        WaveSpec s; s.seed = 106;
        s.eventType = 1; s.eventsReplaceTone = true;
        s.eventRateHz = 3.46f;                      // a click every 0.29 s at C4
        s.eventAccents = 0x55; s.eventDecaySec = 0.035f; s.clickGateSec = 0.045f;
        s.clickPitchMul = 1.0f;
        float ch[12] = { -40, 0, -39, -57, -63, -48, -19, -48, -60, -49, -23, -62 };
        for (int i = 0; i < 12; ++i) s.clickHarm[i] = ch[i];
        s.clickNoiseDb = -18.0f; s.clickThumpDb = -17.0f; s.clickThumpMul = 0.3f;
        s.noiseDb = -100.0f;
        s.attackNoiseDb = -100.0f;
        s.attackSec = 0.3f; s.loopSec = 2.31f; s.attackRise = 0.001f;
        return s;
    }

    //==========================================================================
    // strings, pads, synths
    //==========================================================================
    WaveSpec ensembleStrings()
    {
        WaveSpec s; s.seed = 201;
        slope (s, -6.0f);
        vowel (s, { 280, 1000, 2600, 4200 }, { 200, 500, 900, 1400 }, { 0, -2, -6, -14 });
        s.formFloorDb = -12.0f;
        s.copies = 5; s.detuneCents = 11.0f;
        s.vibRateHz = 5.4f; s.vibCents = 6.0f; s.driftCents = 5.0f; s.driftRateHz = 0.6f;
        s.noiseDb = -30.0f; s.noiseLowHz = 1500.0f; s.noiseHighHz = 11000.0f;
        s.attackSec = 0.5f; s.loopSec = 2.6f; s.attackRise = 0.12f;
        s.zoneStepSemis = 12; s.zoneLowNote = 36.0f;   // a section's attack and body stay put across the range
        return s;
    }
    WaveSpec analogStrings()
    {
        WaveSpec s; s.seed = 202;
        slope (s, -6.5f);
        s.copies = 3; s.detuneCents = 14.0f;
        s.driftCents = 3.0f; s.driftRateHz = 0.3f;
        sweep (s, 500.0f, 2500.0f, 1.2f, -6.0f, 1.0f, 3);
        s.attackSec = 0.4f; s.loopSec = 2.2f; s.attackRise = 0.08f;
        return s;
    }
    WaveSpec whitePad()
    {
        WaveSpec s; s.seed = 203;
        slope (s, -5.0f);
        s.copies = 4; s.detuneCents = 9.0f;
        s.noiseDb = -13.0f; s.noiseLowHz = 1800.0f; s.noiseHighHz = 16000.0f; s.noiseFocus = 0.3f;
        sweep (s, 1200.0f, 4000.0f, 0.8f, 6.0f, 1.0f, 0);
        s.attackSec = 0.5f; s.loopSec = 3.0f; s.attackRise = 0.15f;
        return s;
    }
    WaveSpec dreamStrings()
    {
        WaveSpec s = ensembleStrings(); s.seed = 204;
        s.noiseDb = -18.0f; s.noiseLowHz = 2500.0f; s.noiseHighHz = 16000.0f;
        sweep (s, 900.0f, 3600.0f, 0.6f, 8.0f, 1.0f, 3);
        s.loopSec = 3.4f;
        return s;
    }
    WaveSpec superSaw()
    {
        WaveSpec s; s.seed = 205;
        slope (s, -6.0f);
        s.copies = 5; s.detuneCents = 24.0f;
        s.attackSec = 0.3f; s.loopSec = 2.0f; s.attackRise = 0.005f;
        return s;
    }
    WaveSpec detunedPwm()
    {
        WaveSpec s; s.seed = 206;
        slope (s, -6.0f, -9.0f);
        s.copies = 3; s.detuneCents = 12.0f;
        sweep (s, 800.0f, 3000.0f, 0.5f, 5.0f, 2.0f, 3);
        s.attackSec = 0.3f; s.loopSec = 2.0f; s.attackRise = 0.005f;
        return s;
    }
    WaveSpec choirOoh()
    {
        WaveSpec s; s.seed = 207;
        slope (s, -7.0f);
        vowel (s, { 330, 870, 2250, 3000 }, { 90, 160, 260, 350 }, { 0, -10, -24, -30 });
        vowelB (s, { 420, 950, 2350, 3100 }, { 110, 180, 280, 380 }, { 0, -7, -22, -28 }, 1.0f);
        s.formFloorDb = -40.0f;
        s.copies = 4; s.detuneCents = 9.0f;
        s.vibRateHz = 5.0f; s.vibCents = 7.0f; s.driftCents = 5.0f;
        s.noiseDb = -22.0f; s.noiseLowHz = 2200.0f; s.noiseHighHz = 14000.0f; s.noiseTiltDbOct = -2.0f;
        s.noiseFormants = false;                  // the breath above the vowel
        s.attackSec = 0.5f; s.loopSec = 2.8f; s.attackRise = 0.15f;
        return s;
    }
    WaveSpec breakwaterWhistle()
    {
        WaveSpec s; s.seed = 208;
        setHarm (s, { 0, -24, -30, -42, -50, -60 });
        s.copies = 2; s.detuneCents = 4.0f;
        s.vibRateHz = 0.9f; s.vibCents = 22.0f; s.driftCents = 10.0f; s.driftRateHz = 0.25f;
        s.noiseDb = -18.0f; s.noiseLowHz = 1500.0f; s.noiseHighHz = 9000.0f; s.noiseFocus = 0.35f;
        sweep (s, 1200.0f, 4500.0f, 0.4f, 12.0f, 1.0f, 0);
        s.noiseSweeps = true;
        s.attackSec = 0.6f; s.loopSec = 3.2f; s.attackRise = 0.3f;
        return s;
    }
    WaveSpec airVortex()
    {
        WaveSpec s; s.seed = 209;
        slope (s, -9.0f);
        s.harmDb = 0.0f;
        s.noiseDb = 4.0f; s.noiseLowHz = 200.0f; s.noiseHighHz = 14000.0f; s.noiseFocus = 0.25f;
        sweep (s, 400.0f, 7000.0f, 0.45f, 18.0f, 1.0f, 0);
        sweep (s, 6000.0f, 900.0f, 0.5f, 10.0f, 1.0f, 0, 0.3f);
        s.attackSec = 0.4f; s.loopSec = 3.0f; s.attackRise = 0.2f;
        return s;
    }
    WaveSpec noiseSpectrum()
    {
        WaveSpec s; s.seed = 210;
        s.harmDb = 0.0f; slope (s, -12.0f); for (auto& h : s.harm) h -= 20.0f;
        s.noiseDb = 10.0f; s.noiseLowHz = 150.0f; s.noiseHighHz = 12000.0f;
        s.noiseFocus = 1.0f; s.noiseFocusBw = 0.025f;
        sweep (s, 300.0f, 6000.0f, 0.6f, 14.0f, 1.0f, 3);
        s.attackSec = 0.35f; s.loopSec = 2.5f; s.attackRise = 0.05f;
        return s;
    }
    WaveSpec swish()
    {
        WaveSpec s; s.seed = 211;
        s.harmDb = -100.0f;
        s.noiseDb = 0.0f; s.noiseLowHz = 500.0f; s.noiseHighHz = 15000.0f;
        sweep (s, 900.0f, 11000.0f, 0.35f, 20.0f, 2.0f, 1);
        s.levelSwellCycles = 2.0f; s.levelSwellDb = 8.0f;
        s.attackSec = 0.3f; s.loopSec = 2.0f; s.attackRise = 0.1f;
        return s;
    }

    //==========================================================================
    // bells, keys, organ
    //==========================================================================
    WaveSpec digitalBell()
    {
        WaveSpec s; s.seed = 301;
        setHarm (s, { 0, -6, -10, -14, -20, -24, -30, -36 });
        s.stretch = 0.226f;                 // 1, 2.76, 5.3, 8.6 ... (a bar's partials)
        s.maxPartials = 10;
        s.copies = 2; s.detuneCents = 3.0f;
        s.oneShot = true; s.oneShotDecay = 3.5f;
        s.attackSec = 0.05f; s.loopSec = 3.5f; s.attackRise = 0.002f;
        return s;
    }
    WaveSpec glassBell()
    {
        WaveSpec s; s.seed = 302;
        setHarm (s, { 0, -14, -8, -20, -26, -18, -32, -40 });
        s.stretch = 0.012f;
        s.maxPartials = 14;
        s.copies = 2; s.detuneCents = 2.0f;
        s.oneShot = true; s.oneShotDecay = 4.5f;
        s.attackSec = 0.05f; s.loopSec = 4.5f; s.attackRise = 0.001f;
        return s;
    }
    WaveSpec tineKeys()
    {
        WaveSpec s; s.seed = 303;
        setHarm (s, { 0, -14, -22, -28, -36, -42, -46, -50 });
        s.tiltDbOct = -12.0f;
        s.attackNoiseDb = 2.0f; s.attackNoiseSec = 0.01f; s.noiseLowHz = 1500.0f; s.noiseHighHz = 8000.0f;
        s.noiseDb = -100.0f;
        s.oneShot = true; s.oneShotDecay = 3.0f;
        s.attackSec = 0.05f; s.loopSec = 3.0f; s.attackRise = 0.001f;
        return s;
    }
    WaveSpec drawbarOrgan()
    {
        WaveSpec s; s.seed = 304;
        setHarm (s, { 0, -4, -6, -8, -100, -10, -100, -12, -100, -100, -100, -18, -100, -100, -100, -22 });
        s.tiltDbOct = -40.0f;
        s.attackNoiseDb = -8.0f; s.attackNoiseSec = 0.005f; s.noiseLowHz = 800.0f;
        s.attackSec = 0.2f; s.loopSec = 1.0f; s.attackRise = 0.003f;
        return s;
    }

    //==========================================================================
    // water
    //==========================================================================
    WaveSpec bubbleChoir()
    {
        WaveSpec s = glassChoir(); s.seed = 401;
        s.eventType = 2; s.eventRateHz = 14.0f; s.eventDb = -6.0f; s.eventDecaySec = 0.03f;
        s.bubbleLowMul = 2.0f; s.bubbleHighMul = 12.0f; s.bubbleChirp = 0.18f; s.bubbleHarmonic = true;
        s.loopSec = 3.0f;
        return s;
    }
    WaveSpec surfSwell()
    {
        WaveSpec s; s.seed = 402;
        s.harmDb = -100.0f;
        s.noiseDb = 0.0f; s.noiseLowHz = 120.0f; s.noiseHighHz = 9000.0f; s.noiseTiltDbOct = -2.0f;
        s.levelSwellCycles = 1.0f; s.levelSwellDb = 14.0f;
        sweep (s, 400.0f, 3000.0f, 1.0f, 8.0f, 1.0f, 0);
        s.eventType = 2; s.eventRateHz = 30.0f; s.eventDb = -12.0f; s.eventDecaySec = 0.02f;
        s.bubbleLowMul = 3.0f; s.bubbleHighMul = 24.0f; s.bubbleHarmonic = false; s.bubbleChirp = 0.25f;
        s.attackSec = 0.6f; s.loopSec = 6.0f; s.attackRise = 0.5f;
        return s;
    }
    WaveSpec dolphinClicks()
    {
        // echolocation: now and then a burst of rapid pulses, a glassy tone on
        // odd partials gliding up or down - in zones an octave apart, so the
        // pulses stay near their own pitch and speed wherever it plays
        WaveSpec s; s.seed = 409;
        s.eventType = 3; s.eventsReplaceTone = true;
        s.eventRateHz = 21.0f; s.eventDecaySec = 0.004f; s.clickGateSec = 0.012f;
        float ch[12] = { 0, -100, -5, -100, -8, -100, -14, -100, -19, -100, -26, -100 };
        for (int i = 0; i < 12; ++i) s.clickHarm[i] = ch[i];
        s.trainCount = 2; s.trainSec = 2.2f; s.trainToneHz = 1500.0f; s.trainToneTrack = 0.3f; s.trainRateTrack = 0.25f; s.trainGlideOct = 0.28f;
        s.noiseDb = -100.0f; s.attackNoiseDb = -100.0f;
        s.attackSec = 1.2f; s.loopSec = 6.4f; s.xfadeSec = 0.3f; s.attackRise = 0.001f;
        s.zoneStepSemis = 12; s.zoneLowNote = 48.0f;
        return s;
    }
    WaveSpec dolphinWhistle()
    {
        WaveSpec s; s.seed = 403;
        setHarm (s, { 0, -18, -30, -42 });
        s.copies = 1;
        s.vibRateHz = 1.2f; s.vibCents = 260.0f;     // the whistle's glides
        s.scoopCents = -300.0f; s.scoopSec = 0.25f;
        s.noiseDb = -26.0f; s.noiseLowHz = 2000.0f; s.noiseHighHz = 12000.0f;
        s.harmAmRateHz = 2.4f; s.harmAmDb = 9.0f;
        s.attackSec = 0.4f; s.loopSec = 2.5f; s.attackRise = 0.03f;
        return s;
    }
    WaveSpec whaleSong()
    {
        WaveSpec s; s.seed = 404;
        slope (s, -8.0f);
        vowel (s, { 380, 900, 2300 }, { 120, 220, 320 }, { 0, -6, -18 });
        vowelB (s, { 650, 1300, 2600 }, { 140, 240, 340 }, { 0, -3, -14 }, 1.0f);
        s.formFloorDb = -30.0f;
        s.copies = 2; s.detuneCents = 6.0f;
        s.vibRateHz = 0.2f; s.vibCents = 140.0f; s.driftCents = 20.0f; s.driftRateHz = 0.15f;
        s.noiseDb = -24.0f; s.noiseLowHz = 100.0f; s.noiseHighHz = 3000.0f;
        s.attackSec = 0.8f; s.loopSec = 5.0f; s.attackRise = 0.5f;
        return s;
    }
    WaveSpec reefSparkle()
    {
        WaveSpec s; s.seed = 405;
        s.eventType = 2; s.eventsReplaceTone = true; s.eventRateHz = 22.0f; s.eventDecaySec = 0.05f;
        s.bubbleLowMul = 4.0f; s.bubbleHighMul = 20.0f; s.bubbleChirp = 0.12f; s.bubbleHarmonic = true;
        s.noiseDb = -100.0f;
        s.attackSec = 0.3f; s.loopSec = 3.0f; s.attackRise = 0.01f;
        return s;
    }
    WaveSpec streamFlow()
    {
        WaveSpec s; s.seed = 406;
        s.harmDb = -100.0f;
        s.noiseDb = 0.0f; s.noiseLowHz = 300.0f; s.noiseHighHz = 13000.0f; s.noiseTiltDbOct = -1.5f;
        s.eventType = 2; s.eventRateHz = 45.0f; s.eventDb = 2.0f; s.eventDecaySec = 0.015f;
        s.bubbleLowMul = 2.0f; s.bubbleHighMul = 14.0f; s.bubbleHarmonic = false; s.bubbleChirp = 0.3f;
        s.attackSec = 0.3f; s.loopSec = 3.0f; s.attackRise = 0.05f;
        return s;
    }
    WaveSpec hydrophone()
    {
        WaveSpec s; s.seed = 407;
        setHarm (s, { 0, -10, -20, -30, -40 });
        s.copies = 2; s.detuneCents = 3.0f;
        s.noiseDb = -10.0f; s.noiseLowHz = 30.0f; s.noiseHighHz = 600.0f; s.noiseTiltDbOct = -4.0f;
        s.harmAmRateHz = 0.4f; s.harmAmDb = 5.0f;
        s.attackSec = 0.6f; s.loopSec = 5.0f; s.attackRise = 0.3f;
        return s;
    }

    //==========================================================================
    // basic shapes
    //==========================================================================
    WaveSpec basic (int kind)
    {
        WaveSpec s; s.seed = 500 + (juce::uint32) kind;
        switch (kind)
        {
            case 0: setHarm (s, { 0 }); for (int i = 1; i < 32; ++i) s.harm[i] = -200.0f; s.tiltDbOct = -200.0f; break;   // sine
            case 1: for (int k = 1; k <= 32; ++k) s.harm[k - 1] = (k & 1) ? -12.0f * std::log2 ((float) k) : -200.0f; s.tiltDbOct = -12.0f; break; // triangle
            case 2: slope (s, -6.0f); break;                                                                      // saw
            case 3: for (int k = 1; k <= 32; ++k) s.harm[k - 1] = (k & 1) ? -6.0f * std::log2 ((float) k) : -200.0f; s.tiltDbOct = -6.0f; break;  // square
            case 4:                                                                                                 // pulse 25%
                for (int k = 1; k <= 32; ++k) s.harm[k - 1] = gainToDb (std::abs (std::sin (kPi * 0.25f * (float) k)) / (float) k + 1.0e-6f);
                s.tiltDbOct = -6.0f; break;
            default:                                                                                                // pulse 10%
                for (int k = 1; k <= 32; ++k) s.harm[k - 1] = gainToDb (std::abs (std::sin (kPi * 0.1f * (float) k)) / (float) k + 1.0e-6f);
                s.tiltDbOct = -6.0f; break;
        }
        s.attackSec = 0.2f; s.loopSec = 0.5f; s.attackRise = 0.002f; s.xfadeSec = 0.1f;
        return s;
    }
}

const std::vector<FactoryWave>& factoryWaves()
{
    static const std::vector<FactoryWave> waves {
        { "Airy Vox",        "Air",     airyVox },
        { "Glass Choir",     "Voice",   glassChoir },
        { "Soft Ah",         "Voice",   softAh },
        { "Bamboo Breath",   "Air",     bambooBreath },
        { "Tide Sweep",      "Motion",  tideSweep },
        { "Sonar Clicks",    "Motion",  sonarClicks },
        { "Ensemble Strings","Strings", ensembleStrings },
        { "Analog Strings",  "Strings", analogStrings },
        { "White Pad",       "Air",     whitePad },
        { "Dream Strings",   "Strings", dreamStrings },
        { "Super Saw",       "Synth",   superSaw },
        { "Detuned PWM",     "Synth",   detunedPwm },
        { "Choir Ooh",       "Voice",   choirOoh },
        { "Breakwater Whistle", "Air",  breakwaterWhistle },
        { "Air Vortex",      "Motion",  airVortex },
        { "Noise Spectrum",  "Motion",  noiseSpectrum },
        { "Swish",           "Motion",  swish },
        { "Digital Bell",    "Bell",    digitalBell },
        { "Glass Bell",      "Bell",    glassBell },
        { "Tine Keys",       "Bell",    tineKeys },
        { "Drawbar Organ",   "Synth",   drawbarOrgan },
        { "Bubble Choir",    "Water",   bubbleChoir },
        { "Surf Swell",      "Water",   surfSwell },
        { "Dolphin Whistle", "Water",   dolphinWhistle },
        { "Dolphin Clicks",  "Water",   dolphinClicks },
        { "Whale Song",      "Water",   whaleSong },
        { "Reef Sparkle",    "Water",   reefSparkle },
        { "Stream Flow",     "Water",   streamFlow },
        { "Hydrophone",      "Water",   hydrophone },
        { "Sine",            "Basic",   [] { return basic (0); } },
        { "Triangle",        "Basic",   [] { return basic (1); } },
        { "Saw",             "Basic",   [] { return basic (2); } },
        { "Square",          "Basic",   [] { return basic (3); } },
        { "Pulse 25%",       "Basic",   [] { return basic (4); } },
        { "Pulse 10%",       "Basic",   [] { return basic (5); } },
    };
    return waves;
}

const char* archetypeName (int a)
{
    static const char* n[NumArchetypes] = { "Air Pad", "Glass Vox", "Voice", "Breath Flute", "Wave Sweep", "Clicks",
                                            "Strings", "Super Saw", "Ghost", "Noise Spectrum", "Digital Bell",
                                            "Bubbles", "Surf", "Whale" };
    return n[juce::jlimit (0, (int) NumArchetypes - 1, a)];
}

//==============================================================================
// a new wave of an archetype: the factory design it starts from, varied by the seed
//==============================================================================
WaveSpec roll (int archetype, juce::uint32 seed)
{
    Rng r (seed * 2654435761u + (juce::uint32) archetype * 97u);
    WaveSpec s;
    auto jitterHarm = [&] (float amount)
    {
        for (int k = 1; k < 32; ++k) s.harm[k] += r.bi() * amount * std::sqrt ((float) k / 32.0f + 0.2f);
    };
    switch (archetype)
    {
        case ArchAirPad:
            s = r.chance (0.5f) ? airyVox() : whitePad();
            jitterHarm (6.0f);
            s.copies = r.between (2, 5); s.detuneCents = r.uni (5.0f, 16.0f);
            s.noiseDb = r.uni (-26.0f, -10.0f); s.noiseLowHz = r.lg (200.0f, 2500.0f); s.noiseFocus = r.uni (0.0f, 0.7f);
            if (r.chance (0.5f)) sweep (s, r.lg (600.0f, 1500.0f), r.lg (2500.0f, 7000.0f), r.uni (0.4f, 1.0f), r.uni (4.0f, 12.0f), 1.0f, r.between (0, 3));
            s.loopSec = r.uni (2.0f, 3.6f);
            break;
        case ArchGlassVox:
            s = glassChoir();
            for (int k = 2; k <= 8; ++k) s.harm[k - 1] = r.uni (-50.0f, -14.0f) - (float) k * 2.0f;
            s.copies = r.between (2, 3); s.detuneCents = r.uni (3.0f, 9.0f);
            s.vibCents = r.uni (0.0f, 5.0f);
            s.loopSec = r.uni (1.8f, 3.2f);
            break;
        case ArchVoice:
        {
            s = r.chance (0.5f) ? softAh() : choirOoh();
            static const float vowels[5][3] = { { 760, 1150, 2500 }, { 330, 870, 2250 }, { 500, 1700, 2500 }, { 300, 2200, 2900 }, { 600, 950, 2450 } };
            const int a = r.between (0, 4), b = r.between (0, 4);
            vowel (s, { vowels[a][0], vowels[a][1], vowels[a][2], 3400.0f }, { 130, 200, 280, 380 }, { 0, r.uni (-10.0f, -2.0f), r.uni (-24.0f, -12.0f), -26 });
            if (a != b && r.chance (0.6f))
                vowelB (s, { vowels[b][0], vowels[b][1], vowels[b][2], 3400.0f }, { 130, 200, 280, 380 }, { 0, -6, -18, -26 }, (float) r.between (1, 2));
            s.copies = r.between (1, 4); s.detuneCents = r.uni (4.0f, 12.0f);
            s.loopSec = r.uni (1.8f, 3.0f);
            break;
        }
        case ArchBreathFlute:
            s = bambooBreath();
            jitterHarm (5.0f);
            s.noiseDb = r.uni (-18.0f, -8.0f); s.noiseHighHz = r.lg (1500.0f, 6000.0f);
            s.vibCents = r.uni (5.0f, 25.0f); s.vibRateHz = r.uni (0.5f, 5.5f);
            s.loopSec = r.uni (1.2f, 2.4f);
            break;
        case ArchWaveSweep:
            s = tideSweep();
            s.numSweeps = 0;
            sweep (s, r.lg (150.0f, 600.0f), r.lg (2500.0f, 10000.0f), r.uni (0.2f, 0.6f), r.uni (14.0f, 26.0f), (float) r.between (1, 2), r.between (0, 3));
            if (r.chance (0.6f)) sweep (s, r.lg (500.0f, 2000.0f), r.lg (4000.0f, 12000.0f), r.uni (0.2f, 0.5f), r.uni (8.0f, 18.0f), (float) r.between (1, 3), r.between (0, 3), r.uni());
            slope (s, r.uni (-7.0f, -3.0f), r.chance (0.3f) ? -12.0f : 0.0f);
            for (auto& h : s.harm) h -= r.uni (8.0f, 16.0f);
            s.loopSec = r.uni (1.0f, 2.6f);
            break;
        case ArchClicks:
            if (r.chance (0.35f))
            {
                // echolocation: bursts of pulses gliding up or down
                s = dolphinClicks();
                for (int m = 0; m < 12; m += 2) s.clickHarm[m] = m == 0 ? 0.0f : r.uni (-30.0f, -3.0f);
                s.trainToneHz = r.lg (900.0f, 3200.0f);
                s.eventRateHz = r.lg (12.0f, 34.0f);
                s.trainCount = r.between (1, 3); s.trainSec = r.uni (1.0f, 3.0f);
                s.trainGlideOct = r.uni (0.1f, 0.5f);
                s.loopSec = r.uni (4.5f, 7.0f);
                break;
            }
            s = sonarClicks();
            for (int m = 0; m < 12; ++m) s.clickHarm[m] = r.uni (-64.0f, -6.0f);
            s.clickGateSec = r.chance (0.5f) ? r.uni (0.02f, 0.07f) : 0.0f;
            s.clickHarm[r.between (0, 3)] = 0.0f;
            s.eventRateHz = r.lg (1.5f, 8.0f);
            s.eventAccents = (juce::uint32) r.between (1, 255);
            s.eventDecaySec = r.lg (0.005f, 0.05f);
            s.loopSec = r.uni (1.5f, 3.0f);
            break;
        case ArchStrings:
            s = r.chance (0.5f) ? ensembleStrings() : analogStrings();
            s.copies = r.between (3, 5); s.detuneCents = r.uni (6.0f, 18.0f);
            s.vibCents = r.uni (0.0f, 8.0f);
            if (r.chance (0.4f)) { s.noiseDb = r.uni (-24.0f, -14.0f); s.noiseLowHz = r.lg (2000.0f, 5000.0f); s.noiseHighHz = 16000.0f; }
            s.loopSec = r.uni (2.0f, 3.6f);
            break;
        case ArchSuperSaw:
            s = r.chance (0.6f) ? superSaw() : detunedPwm();
            s.copies = r.between (3, 5); s.detuneCents = r.uni (8.0f, 32.0f);
            break;
        case ArchGhost:
            s = breakwaterWhistle();
            s.vibCents = r.uni (6.0f, 40.0f); s.vibRateHz = r.uni (0.3f, 2.0f);
            s.numSweeps = 0; sweep (s, r.lg (600.0f, 2000.0f), r.lg (3000.0f, 9000.0f), r.uni (0.3f, 0.7f), r.uni (6.0f, 16.0f), 1.0f, r.between (0, 3));
            s.loopSec = r.uni (2.4f, 4.0f);
            break;
        case ArchNoiseSpectrum:
            s = r.chance (0.5f) ? noiseSpectrum() : airVortex();
            s.noiseFocusBw = r.lg (0.015f, 0.15f); s.noiseFocus = r.uni (0.3f, 1.0f);
            s.numSweeps = 0;
            sweep (s, r.lg (200.0f, 1500.0f), r.lg (3000.0f, 12000.0f), r.uni (0.3f, 0.9f), r.uni (8.0f, 20.0f), (float) r.between (1, 2), r.between (0, 3));
            s.loopSec = r.uni (2.0f, 4.0f);
            break;
        case ArchDigitalBell:
            s = r.chance (0.5f) ? digitalBell() : glassBell();
            s.stretch = r.chance (0.5f) ? r.uni (0.0f, 0.03f) : r.uni (0.1f, 0.3f);
            jitterHarm (10.0f);
            s.oneShotDecay = r.uni (1.5f, 6.0f);
            break;
        case ArchBubbles:
            s = r.chance (0.5f) ? bubbleChoir() : reefSparkle();
            s.eventRateHz = r.lg (6.0f, 40.0f); s.bubbleLowMul = r.uni (1.0f, 4.0f); s.bubbleHighMul = s.bubbleLowMul * r.uni (2.0f, 8.0f);
            s.bubbleChirp = r.uni (0.05f, 0.35f); s.eventDecaySec = r.lg (0.01f, 0.08f);
            break;
        case ArchSurf:
            s = r.chance (0.5f) ? surfSwell() : streamFlow();
            s.levelSwellDb = r.uni (4.0f, 16.0f); s.loopSec = r.uni (3.0f, 7.0f);
            s.noiseHighHz = r.lg (3000.0f, 12000.0f);
            break;
        default:
            s = r.chance (0.5f) ? whaleSong() : dolphinWhistle();
            s.vibCents = r.uni (60.0f, 300.0f); s.vibRateHz = r.uni (0.15f, 1.5f);
            s.loopSec = r.uni (2.5f, 6.0f);
            break;
    }
    s.seed = seed;
    return s;
}
}
