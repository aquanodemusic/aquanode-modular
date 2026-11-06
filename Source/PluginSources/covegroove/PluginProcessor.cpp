#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace covegroove
{
    namespace
    {
        const juce::StringArray kDelayTimes { "1/16", "1/16 dot", "1/8", "1/8 dot", "1/4", "1/4 dot" };
        const float kDelaySteps[6] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };
        const juce::StringArray kFilterRates { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" };
        const float kFilterBars[5] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };

        juce::StringArray modelChoices (int lane)
        {
            juce::StringArray a;
            for (int m = 0; m < kNumModels; ++m) a.add (modelName (lane, m));
            return a;
        }

        juce::StringArray styleChoices()
        {
            juce::StringArray a;
            for (int s = 0; s < NumStyles; ++s) a.add (styleName (s));
            return a;
        }

        // the sounds a fresh Cove Groove starts with, lane by lane
        struct LaneDefault { int model; float level, pan, send; bool top; };
        const LaneDefault kLaneDefaults[kNumLanes] = {
            { 0, 46.0f, 0.0f, 0.0f, false },     // Kick
            { 0, 22.0f, 0.0f, 0.0f, false },     // Bass
            { 0, 60.0f, 8.0f, 22.0f, false },    // Clap
            { 0, 85.0f, -22.0f, 0.0f, true },    // Closed Hat
            { 0, 65.0f, 18.0f, 10.0f, true },    // Open Hat
            { 2, 60.0f, 30.0f, 12.0f, true },    // Cymbal: shaker
            { 0, 75.0f, -30.0f, 35.0f, true },   // Perc
            { 0, 60.0f, 0.0f, 45.0f, true } };   // FX

        //=== the factory programs =============================================
        struct Program
        {
            const char* name;
            const char* category;
            int style, bars; float density, variation, swing, tempo;
            int models[kNumLanes];
            float levels[kNumLanes];
            float phaser, filterFreq, filterReso, filterMotion, filterMix; int filterRate, filterShape;
            int delayTime; float delayFb, delayMix;
            int driveMode; float drive, driveTone;
            float tunes[kNumLanes];      // semitones (left out: 0)
            float decays[kNumLanes];     // % (left out / 0: 100)
            float volume;                // % (left out / 0: 80)
        };

        const Program kPrograms[] = {
            // the warehouse sounds (the shaped ones, fitted to recorded hits), the
            // rumble under the kick, swung hard - Warehouse Rumble is where a new
            // Cove Groove starts
            { "Warehouse Rumble", "Warehouse", Rumble, 2, 0.60f, 0.45f, 66.0f, 140.0f, { 8, 8, 8, 8, 8, 8, 8, 9 }, { 50, 26, 58, 80, 60, 50, 72, 50 },
              20.0f, 1600.0f, 35.0f, 45.0f, 20.0f, 2, 0, 3, 40.0f, 18.0f, 1, 30.0f, -5.0f },
            { "Loading Bay",      "Warehouse", Rumble, 2, 0.55f, 0.40f, 67.0f, 140.0f, { 8, 8, 8, 8, 8, 9, 8, 9 }, { 50, 27, 56, 80, 58, 46, 72, 48 },
              25.0f, 1400.0f, 40.0f, 55.0f, 25.0f, 3, 0, 3, 45.0f, 20.0f, 1, 34.0f, -8.0f },
            { "Night Shift",      "Warehouse", Rumble, 4, 0.65f, 0.50f, 66.0f, 141.0f, { 8, 9, 8, 8, 8, 8, 9, 9 }, { 50, 25, 58, 82, 60, 50, 70, 48 },
              15.0f, 1800.0f, 30.0f, 40.0f, 18.0f, 2, 0, 3, 38.0f, 16.0f, 1, 28.0f, -2.0f },
            { "Steel Pier",       "Warehouse", Rumble, 2, 0.70f, 0.35f, 64.0f, 142.0f, { 8, 8, 9, 8, 9, 8, 8, 8 }, { 50, 26, 54, 82, 56, 50, 74, 52 },
              30.0f, 2200.0f, 35.0f, 50.0f, 22.0f, 2, 1, 1, 40.0f, 18.0f, 0, 25.0f, 0.0f },
            { "Freight Rumble",   "Warehouse", Rumble, 1, 0.60f, 0.30f, 68.0f, 139.0f, { 8, 8, 8, 9, 8, 8, 8, 9 }, { 50, 28, 58, 78, 60, 50, 72, 50 },
              20.0f, 1500.0f, 45.0f, 60.0f, 30.0f, 3, 0, 3, 50.0f, 22.0f, 1, 36.0f, -10.0f },
            { "Rust Belt",        "Warehouse", Rumble, 4, 0.55f, 0.55f, 65.0f, 138.0f, { 9, 8, 8, 8, 8, 8, 8, 9 }, { 50, 26, 58, 80, 60, 50, 72, 50 },
              25.0f, 1200.0f, 50.0f, 65.0f, 30.0f, 4, 2, 5, 45.0f, 18.0f, 1, 30.0f, -6.0f },
            { "Dock Lights",      "Warehouse", Rumble, 2, 0.50f, 0.45f, 66.0f, 140.0f, { 8, 8, 9, 8, 8, 9, 8, 9 }, { 50, 26, 54, 80, 60, 46, 72, 50 },
              35.0f, 2000.0f, 30.0f, 40.0f, 20.0f, 2, 0, 3, 42.0f, 24.0f, 1, 26.0f, 2.0f },
            { "Concrete Verb",    "Warehouse", Rumble, 4, 0.55f, 0.50f, 64.0f, 140.0f, { 9, 9, 9, 9, 8, 9, 9, 9 }, { 50, 24, 55, 80, 60, 45, 70, 50 },
              15.0f, 1400.0f, 40.0f, 55.0f, 25.0f, 3, 2, 3, 45.0f, 20.0f, 0, 35.0f, -10.0f },
            { "Gate Tunnel",      "Warehouse", Rolling, 1, 0.65f, 0.35f, 66.0f, 142.0f, { 8, 8, 8, 8, 8, 2, 8, 8 }, { 50, 25, 60, 85, 62, 55, 75, 55 },
              30.0f, 2000.0f, 30.0f, 40.0f, 20.0f, 2, 0, 1, 35.0f, 16.0f, 2, 30.0f, 0.0f },
            { "Dark Crash Drive", "Warehouse", Tribal, 4, 0.60f, 0.55f, 62.0f, 140.0f, { 9, 8, 8, 9, 9, 8, 8, 8 }, { 52, 25, 58, 82, 60, 50, 75, 50 },
              25.0f, 1800.0f, 45.0f, 60.0f, 30.0f, 3, 1, 3, 45.0f, 22.0f, 3, 35.0f, 5.0f },

            // the hardgroove sounds the generator was fitted to
            { "Cove Rolling",     "Hardgroove", Rolling, 2, 0.55f, 0.40f, 60.0f, 138.0f, { 0, 0, 0, 0, 0, 2, 0, 0 }, { 50, 25, 60, 85, 65, 60, 75, 60 },
              30.0f, 1800.0f, 30.0f, 45.0f, 20.0f, 2, 0, 3, 40.0f, 22.0f, 0, 22.0f, 0.0f },
            { "Tide Tribal",      "Hardgroove", Tribal, 4, 0.60f, 0.50f, 58.0f, 136.0f, { 0, 3, 1, 1, 0, 0, 1, 1 }, { 52, 24, 50, 70, 55, 55, 85, 55 },
              20.0f, 1200.0f, 45.0f, 60.0f, 30.0f, 3, 2, 3, 45.0f, 25.0f, 0, 28.0f, -20.0f },
            { "Shuffle Reef",     "Hardgroove", Shuffle, 2, 0.50f, 0.55f, 68.0f, 132.0f, { 0, 0, 0, 0, 1, 0, 2, 2 }, { 48, 25, 62, 80, 60, 55, 70, 50 },
              50.0f, 2400.0f, 35.0f, 40.0f, 25.0f, 2, 0, 3, 35.0f, 20.0f, 1, 25.0f, 10.0f },
            { "Stomp Harbour",    "Hardgroove", Stomp, 1, 0.65f, 0.30f, 54.0f, 142.0f, { 2, 2, 3, 0, 0, 1, 3, 3 }, { 55, 22, 65, 80, 60, 45, 60, 55 },
              15.0f, 3000.0f, 25.0f, 20.0f, 15.0f, 1, 0, 1, 30.0f, 15.0f, 2, 35.0f, 0.0f },
            { "Sea Cave Drive",   "Hardgroove", Rolling, 4, 0.70f, 0.45f, 62.0f, 140.0f, { 0, 1, 0, 0, 0, 2, 0, 0 }, { 50, 26, 58, 85, 62, 60, 70, 60 },
              35.0f, 1500.0f, 55.0f, 70.0f, 40.0f, 3, 2, 3, 50.0f, 25.0f, 0, 45.0f, 15.0f },
            { "Breaker Loop",     "Hardgroove", Tribal, 2, 0.45f, 0.40f, 62.0f, 128.0f, { 1, 0, 2, 1, 0, 2, 0, 0 }, { 52, 25, 55, 75, 60, 62, 78, 60 },
              25.0f, 1800.0f, 30.0f, 35.0f, 20.0f, 2, 0, 5, 60.0f, 28.0f, 1, 20.0f, -10.0f },
            { "Riptide",          "Hardgroove", Shuffle, 4, 0.60f, 0.65f, 72.0f, 145.0f, { 0, 0, 0, 0, 0, 3, 0, 2 }, { 50, 24, 60, 85, 65, 50, 75, 55 },
              40.0f, 2000.0f, 40.0f, 55.0f, 30.0f, 3, 1, 3, 45.0f, 24.0f, 0, 30.0f, 5.0f },
            { "Low Tide Minimal", "Hardgroove", Stomp, 1, 0.20f, 0.20f, 56.0f, 125.0f, { 3, 0, 1, 1, 0, 0, 3, 3 }, { 55, 28, 55, 70, 55, 50, 60, 50 },
              20.0f, 1600.0f, 25.0f, 25.0f, 15.0f, 2, 0, 3, 35.0f, 18.0f, 1, 15.0f, 0.0f },

            // the analog box's sounds
            { "Analog Lagoon",       "Analog", Rolling, 2, 0.50f, 0.40f, 58.0f, 132.0f, { 4, 4, 4, 4, 4, 4, 4, 5 }, { 55, 20, 58, 82, 60, 45, 55, 55 },
              20.0f, 1600.0f, 25.0f, 30.0f, 15.0f, 2, 0, 3, 35.0f, 18.0f, 1, 18.0f, 0.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 70, 60, 100, 100, 70, 80, 100, 100 }, 117.0f },
            { "Cowbell Coast",    "Analog", Shuffle, 2, 0.55f, 0.55f, 64.0f, 128.0f, { 4, 0, 4, 4, 4, 2, 4, 5 }, { 52, 24, 55, 80, 58, 55, 58, 50 },
              35.0f, 2200.0f, 30.0f, 40.0f, 20.0f, 2, 0, 1, 40.0f, 20.0f, 1, 20.0f, 5.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 60, 100, 100, 100, 60, 100, 70, 100 }, 101.0f },
            { "Night Swim",   "Analog", Stomp, 1, 0.30f, 0.30f, 56.0f, 124.0f, { 4, 4, 5, 4, 4, 4, 6, 6 }, { 58, 22, 50, 75, 55, 40, 55, 45 },
              15.0f, 1200.0f, 35.0f, 50.0f, 25.0f, 3, 0, 3, 50.0f, 24.0f, 1, 12.0f, -15.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 90, 70, 100, 100, 80, 100, 100, 100 }, 120.0f },

            // the hybrid box's
            { "Hybrid Pier",         "Hybrid", Rolling, 4, 0.60f, 0.45f, 60.0f, 136.0f, { 5, 5, 5, 5, 5, 5, 1, 7 }, { 52, 22, 55, 80, 60, 45, 60, 45 },
              30.0f, 1800.0f, 35.0f, 50.0f, 25.0f, 3, 0, 3, 40.0f, 22.0f, 0, 25.0f, 0.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, 99.0f },
            { "Acid Brine",       "Hybrid", Tribal, 2, 0.65f, 0.55f, 62.0f, 138.0f, { 5, 5, 6, 5, 5, 2, 0, 1 }, { 52, 24, 52, 78, 58, 50, 70, 50 },
              25.0f, 1400.0f, 45.0f, 60.0f, 30.0f, 2, 2, 3, 45.0f, 24.0f, 0, 32.0f, 10.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 100, 130, 100, 100, 100, 100, 100, 100 }, 90.0f },
            { "Undertow",     "Hybrid", Stomp, 2, 0.55f, 0.35f, 55.0f, 140.0f, { 7, 3, 5, 5, 5, 3, 3, 7 }, { 50, 22, 55, 80, 58, 45, 55, 45 },
              15.0f, 2600.0f, 30.0f, 30.0f, 20.0f, 1, 0, 1, 35.0f, 16.0f, 2, 45.0f, -10.0f },
            { "Ferry Lights",     "Hybrid", Shuffle, 4, 0.45f, 0.50f, 66.0f, 130.0f, { 5, 6, 5, 5, 5, 5, 5, 4 }, { 52, 22, 55, 78, 58, 45, 55, 50 },
              40.0f, 2000.0f, 30.0f, 45.0f, 25.0f, 3, 1, 5, 50.0f, 24.0f, 1, 20.0f, 5.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, 110.0f },

            // a played kit
            { "Driftwood Kit",    "Kit", Shuffle, 2, 0.50f, 0.50f, 62.0f, 122.0f, { 6, 7, 7, 6, 6, 6, 7, 4 }, { 55, 26, 55, 70, 50, 42, 55, 45 },
              10.0f, 2600.0f, 20.0f, 20.0f, 10.0f, 2, 0, 3, 30.0f, 14.0f, 1, 12.0f, 0.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, 86.0f },
            { "Saltmarsh Kit",    "Kit", Tribal, 4, 0.55f, 0.55f, 58.0f, 126.0f, { 6, 7, 7, 6, 6, 7, 0, 5 }, { 55, 26, 52, 70, 50, 45, 72, 50 },
              20.0f, 1800.0f, 30.0f, 40.0f, 20.0f, 3, 0, 3, 40.0f, 18.0f, 1, 16.0f, -10.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, 100.0f },
            { "Boathouse Kit",    "Kit", Rolling, 2, 0.60f, 0.40f, 60.0f, 134.0f, { 6, 0, 7, 7, 7, 2, 7, 6 }, { 55, 24, 55, 72, 52, 55, 55, 40 },
              25.0f, 2000.0f, 30.0f, 35.0f, 20.0f, 2, 0, 3, 40.0f, 20.0f, 0, 22.0f, 0.0f },

            // across the boxes
            { "Tidepool Mix",     "Mixed", Rolling, 2, 0.55f, 0.50f, 60.0f, 134.0f, { 5, 4, 0, 4, 6, 5, 4, 4 }, { 52, 22, 58, 80, 52, 45, 55, 50 },
              30.0f, 1800.0f, 35.0f, 45.0f, 25.0f, 2, 0, 3, 40.0f, 22.0f, 1, 22.0f, 0.0f,
              { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, 115.0f },
            { "Storm Crunch",     "Mixed", Stomp, 4, 0.65f, 0.45f, 56.0f, 142.0f, { 7, 1, 2, 2, 3, 3, 3, 7 }, { 50, 24, 60, 80, 60, 45, 60, 45 },
              20.0f, 2400.0f, 40.0f, 55.0f, 30.0f, 3, 2, 3, 45.0f, 22.0f, 3, 40.0f, 10.0f } };
        constexpr int kNumPrograms = (int) (sizeof (kPrograms) / sizeof (kPrograms[0]));

        //=== the kits (a sound for every lane at once) ==========================
        struct Kit { const char* name; int models[kNumLanes]; float levels[kNumLanes]; float decays[kNumLanes]; };
        const Kit kKits[] = {
            { "Hardgroove", { 0, 0, 0, 0, 0, 2, 0, 0 }, { 46, 22, 60, 85, 65, 60, 75, 60 }, { 100, 100, 100, 100, 100, 100, 100, 100 } },
            { "Analog",        { 4, 4, 4, 4, 4, 4, 4, 5 }, { 55, 20, 58, 82, 60, 45, 55, 55 }, { 70, 60, 100, 100, 70, 80, 100, 100 } },
            { "Hybrid",        { 5, 5, 5, 5, 5, 5, 1, 4 }, { 52, 22, 55, 80, 60, 45, 60, 50 }, { 100, 100, 100, 100, 100, 100, 100, 100 } },
            { "Hybrid Acid",   { 5, 5, 6, 5, 5, 2, 0, 7 }, { 52, 24, 52, 78, 58, 50, 70, 45 }, { 100, 130, 100, 100, 100, 100, 100, 100 } },
            { "Kit",        { 6, 7, 7, 6, 6, 6, 7, 4 }, { 55, 26, 55, 70, 50, 42, 55, 45 }, { 100, 100, 100, 100, 100, 100, 100, 100 } },
            { "Crunch",     { 7, 3, 2, 2, 3, 3, 3, 7 }, { 50, 24, 60, 80, 60, 45, 60, 45 }, { 100, 100, 100, 100, 100, 100, 100, 100 } },
            { "Warehouse",  { 8, 8, 8, 8, 8, 8, 8, 9 }, { 50, 26, 58, 80, 60, 50, 72, 50 }, { 100, 100, 100, 100, 100, 100, 100, 100 } },
            { "Warehouse Verb", { 9, 9, 9, 9, 9, 9, 9, 8 }, { 50, 24, 55, 80, 60, 45, 70, 50 }, { 100, 100, 100, 100, 100, 100, 100, 100 } } };
        constexpr int kNumKits = (int) (sizeof (kKits) / sizeof (kKits[0]));

        //=== the effect presets (the phaser, band-pass, delay and drive alone) =====
        struct FxPreset
        {
            const char* name;
            float phaser, phaserRate, filterFreq, filterReso, filterMotion, filterMix; int filterRate, filterShape;
            int delayTime; float delayFb, delayTone, delayMix; bool ping;
            int driveMode; float drive, driveTone, driveMix;
        };
        const FxPreset kFxPresets[] = {
            { "Clean",          0.0f, 0.20f, 1800.0f, 20.0f, 0.0f, 0.0f, 2, 0,   3, 25.0f, 6000.0f, 8.0f, true,    0, 0.0f, 0.0f, 100.0f },
            { "Cove Phase",    30.0f, 0.20f, 1800.0f, 30.0f, 45.0f, 20.0f, 2, 0, 3, 40.0f, 5000.0f, 22.0f, true,  0, 22.0f, 0.0f, 100.0f },
            { "Tunnel Sweep",  15.0f, 0.15f, 1200.0f, 65.0f, 85.0f, 55.0f, 3, 0, 3, 35.0f, 4000.0f, 18.0f, true,  0, 25.0f, -10.0f, 100.0f },
            { "Dub Echo",      10.0f, 0.10f, 2000.0f, 25.0f, 20.0f, 10.0f, 2, 0, 3, 68.0f, 2200.0f, 34.0f, true,  1, 18.0f, -25.0f, 100.0f },
            { "Ping Shuffle",  25.0f, 0.30f, 2400.0f, 30.0f, 35.0f, 20.0f, 1, 0, 1, 45.0f, 7000.0f, 26.0f, true,  0, 15.0f, 10.0f, 100.0f },
            { "Tape Warmth",   20.0f, 0.12f, 1600.0f, 25.0f, 30.0f, 15.0f, 3, 0, 4, 30.0f, 3500.0f, 14.0f, false, 1, 45.0f, -20.0f, 100.0f },
            { "Fuzz Pressure", 20.0f, 0.25f, 2600.0f, 35.0f, 40.0f, 25.0f, 2, 1, 3, 35.0f, 5000.0f, 18.0f, true,  2, 50.0f, 5.0f, 80.0f },
            { "Crushed Cave",  35.0f, 0.40f, 1500.0f, 50.0f, 60.0f, 35.0f, 2, 2, 5, 50.0f, 3000.0f, 24.0f, true,  3, 40.0f, -5.0f, 70.0f },
            { "Slow Tide",     55.0f, 0.06f, 1000.0f, 40.0f, 70.0f, 40.0f, 4, 0, 5, 55.0f, 4500.0f, 25.0f, true,  0, 18.0f, 0.0f, 100.0f },
            { "Random Gate",   20.0f, 0.50f, 2200.0f, 55.0f, 75.0f, 45.0f, 0, 2, 2, 40.0f, 6000.0f, 20.0f, true,  0, 28.0f, 10.0f, 100.0f } };
        constexpr int kNumFxPresets = (int) (sizeof (kFxPresets) / sizeof (kFxPresets[0]));
    }

    //==========================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout CoveGrooveProcessor::createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;
        auto pct = [] (const char* id, const char* name, float lo, float hi, float def)
        { return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f), def, AudioParameterFloatAttributes().withLabel ("%")); };

        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "run", 1 }, "Run", false));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo", NormalisableRange<float> (70.0f, 190.0f, 0.1f), 138.0f,
                                                            AudioParameterFloatAttributes().withLabel ("BPM")));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "clock", 1 }, "Clock", StringArray { "Own Tempo", "Patch Tempo" }, 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "bars", 1 }, "Bars", StringArray { "1", "2", "4" }, 1));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style", styleChoices(), 0));
        p.push_back (pct ("density", "Density", 0.0f, 100.0f, 55.0f));
        p.push_back (pct ("variation", "Variation", 0.0f, 100.0f, 40.0f));
        p.push_back (pct ("swing", "Swing", 50.0f, 75.0f, 60.0f));

        for (int l = 0; l < kNumLanes; ++l)
        {
            const auto& d = kLaneDefaults[l];
            const String n (laneName (l));
            auto id = [l] (const char* w) { return laneParam (l, w); };
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id ("model"), 1 }, n + " Sound", modelChoices (l), d.model));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id ("tune"), 1 }, n + " Tune", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                                AudioParameterFloatAttributes().withLabel ("st")));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id ("decay"), 1 }, n + " Decay", NormalisableRange<float> (25.0f, 400.0f, 1.0f, 0.5f), 100.0f,
                                                                AudioParameterFloatAttributes().withLabel ("%")));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id ("tone"), 1 }, n + " Tone", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                                                AudioParameterFloatAttributes().withLabel ("%")));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id ("level"), 1 }, n + " Level", NormalisableRange<float> (0.0f, 150.0f, 0.1f), d.level,
                                                                AudioParameterFloatAttributes().withLabel ("%")));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id ("pan"), 1 }, n + " Pan", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), d.pan));
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id ("send"), 1 }, n + " Delay Send", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.send,
                                                                AudioParameterFloatAttributes().withLabel ("%")));
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id ("top"), 1 }, n + " Top Bus", d.top));
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id ("mute"), 1 }, n + " Mute", false));
        }

        p.push_back (pct ("phaser", "Phaser", 0.0f, 100.0f, 30.0f));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "phaserRate", 1 }, "Phaser Rate", NormalisableRange<float> (0.03f, 4.0f, 0.01f, 0.4f), 0.2f,
                                                            AudioParameterFloatAttributes().withLabel ("Hz")));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "filterFreq", 1 }, "Filter Freq", NormalisableRange<float> (100.0f, 10000.0f, 1.0f, 0.3f), 1800.0f,
                                                            AudioParameterFloatAttributes().withLabel ("Hz")));
        p.push_back (pct ("filterReso", "Filter Reso", 0.0f, 100.0f, 30.0f));
        p.push_back (pct ("filterMotion", "Filter Motion", 0.0f, 100.0f, 45.0f));
        p.push_back (pct ("filterMix", "Filter Mix", 0.0f, 100.0f, 20.0f));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "filterRate", 1 }, "Filter Rate", kFilterRates, 2));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "filterShape", 1 }, "Filter Shape", StringArray { "Sine", "Ramp", "Random" }, 0));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "delayTime", 1 }, "Delay Time", kDelayTimes, 3));
        p.push_back (pct ("delayFeedback", "Delay Feedback", 0.0f, 90.0f, 40.0f));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "delayTone", 1 }, "Delay Tone", NormalisableRange<float> (500.0f, 16000.0f, 1.0f, 0.3f), 5000.0f,
                                                            AudioParameterFloatAttributes().withLabel ("Hz")));
        p.push_back (pct ("delayMix", "Delay Mix", 0.0f, 100.0f, 22.0f));
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "delayPing", 1 }, "Ping-Pong", true));
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "driveMode", 1 }, "Drive Mode", StringArray { "Tube", "Tape", "Fuzz", "Crush" }, 0));
        p.push_back (pct ("drive", "Drive", 0.0f, 100.0f, 22.0f));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "driveTone", 1 }, "Drive Tone", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
        p.push_back (pct ("driveMix", "Drive Mix", 0.0f, 100.0f, 100.0f));
        p.push_back (pct ("volume", "Volume", 0.0f, 150.0f, 43.0f));
        return { p.begin(), p.end() };
    }

    //==========================================================================
    CoveGrooveProcessor::CoveGrooveProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "CoveGroove", createLayout())
    {
        loadFactory (0);
        apvts.addParameterListener ("bars", this);
    }

    CoveGrooveProcessor::~CoveGrooveProcessor() { apvts.removeParameterListener ("bars", this); }

    // Bars: the pattern grows (repeating itself) or shrinks
    void CoveGrooveProcessor::parameterChanged (const juce::String& id, float value)
    {
        if (id == "bars")
        {
            static const int barChoices[3] = { 1, 2, 4 };
            setBars (barChoices[juce::jlimit (0, 2, (int) std::lround (value))]);
        }
    }

    bool CoveGrooveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    }

    void CoveGrooveProcessor::prepareToPlay (double sampleRate, int)
    {
        sr = sampleRate;
        groove.prepare (sampleRate);
        wasRunning = false;
        playStep = -1;
    }

    //==========================================================================
    void CoveGrooveProcessor::applySettings()
    {
        auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
        for (int l = 0; l < kNumLanes; ++l)
        {
            auto& s = groove.lanes.sound[(size_t) l];
            s.model = (int) v (laneParam (l, "model"));
            s.tune = v (laneParam (l, "tune"));
            s.decay = v (laneParam (l, "decay")) * 0.01f;
            s.tone = v (laneParam (l, "tone")) * 0.01f;
            auto& m = groove.lanes.mix[(size_t) l];
            m.level = v (laneParam (l, "level")) * 0.01f;
            m.pan = v (laneParam (l, "pan")) * 0.01f;
            m.send = v (laneParam (l, "send")) * 0.01f;
            m.top = v (laneParam (l, "top")) > 0.5f;
            m.mute = v (laneParam (l, "mute")) > 0.5f;
        }
        // the lanes' samples and curves, when they changed
        if (const int serial = sampleSerial.load (std::memory_order_acquire); serial != audioSampleSerial)
        {
            const juce::SpinLock::ScopedTryLockType lock (handoffLock);
            if (lock.isLocked())
            {
                for (int l = 0; l < kNumLanes; ++l)
                {
                    auto& s = groove.lanes.sound[(size_t) l];
                    s.sample = handoffData[(size_t) l];
                    s.curve = handoffCurve[(size_t) l];
                }
                audioSampleSerial = serial;
            }
        }
        groove.seq.swing = v ("swing") * 0.01f;
        auto& b = groove.bus;
        b.phaser.amount = v ("phaser") * 0.01f;
        b.phaser.rateHz = v ("phaserRate");
        b.filter.freqHz = v ("filterFreq");
        b.filter.reso = v ("filterReso") * 0.01f;
        b.filter.motion = v ("filterMotion") * 0.01f;
        b.filter.mix = v ("filterMix") * 0.01f;
        b.filter.shape = (int) v ("filterShape");
        b.filterRateBars = kFilterBars[juce::jlimit (0, 4, (int) v ("filterRate"))];
        b.delay.steps = kDelaySteps[juce::jlimit (0, 5, (int) v ("delayTime"))];
        b.delay.feedback = v ("delayFeedback") * 0.01f;
        b.delay.toneHz = v ("delayTone");
        b.delay.mix = v ("delayMix") * 0.01f;
        b.delay.pingPong = v ("delayPing") > 0.5f;
        b.drive.mode = (int) v ("driveMode");
        b.drive.drive = v ("drive") * 0.01f;
        b.drive.tone = v ("driveTone") * 0.01f;
        b.drive.mix = v ("driveMix") * 0.01f;
        b.volume = v ("volume") * 0.01f;
    }

    void CoveGrooveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        buffer.clear();
        if (n <= 0) return;

        // the pattern, when the editor changed it
        if (const int serial = patternSerial.load (std::memory_order_acquire); serial != audioSerial)
        {
            const juce::SpinLock::ScopedTryLockType lock (patternLock);
            if (lock.isLocked())
            {
                audioPattern = pattern;
                audioSerial = serial;
            }
        }

        applySettings();

        // the sample panel's PLAY
        if (const unsigned a = auditionMask.exchange (0u))
            for (int l = 0; l < kNumLanes; ++l)
                if ((a >> l) & 1u) groove.lanes.trigger (l, 0.85f, false);

        // the clock: the DAW's transport when it plays, else RUN at our own (or the patch's) tempo
        double bpm = apvts.getRawParameterValue ("tempo")->load();
        double pos16 = 0.0;
        bool running = false;
        double hostBpm = 120.0;
        bool hostPlaying = false, hostHasPpq = false;
        double hostPpq = 0.0;
        if (auto* ph = getPlayHead())
            if (auto info = ph->getPosition())
            {
                if (auto b = info->getBpm()) hostBpm = *b;
                hostPlaying = info->getIsPlaying();
                if (auto q = info->getPpqPosition()) { hostPpq = *q; hostHasPpq = true; }
            }
        if (hostPlaying && hostHasPpq)
        {
            bpm = hostBpm;
            pos16 = hostPpq * 4.0;
            running = true;
        }
        else if (apvts.getRawParameterValue ("run")->load() > 0.5f)
        {
            if ((int) apvts.getRawParameterValue ("clock")->load() == 1) bpm = hostBpm;
            if (! wasRunning) ownPos16 = 0.0;            // RUN starts at the top of the pattern
            pos16 = ownPos16;
            running = true;
        }
        wasRunning = running && ! hostPlaying;
        bpm = juce::jlimit (20.0, 400.0, bpm);
        const double inc16 = bpm / 60.0 * 4.0 / sr;

        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        float scratchR[512];

        // played in pieces between the keys
        int done = 0;
        auto renderTo = [&] (int end)
        {
            while (done < end)
            {
                const int len = juce::jmin (end - done, 512);
                groove.process (audioPattern, L + done, R != nullptr ? R + done : scratchR, len, pos16 + inc16 * done, bpm, running);
                if (R == nullptr)
                    for (int i = 0; i < len; ++i) L[done + i] = 0.5f * (L[done + i] + scratchR[i]);   // mono
                done += len;
            }
        };
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOn()) continue;
            renderTo (juce::jlimit (0, n, meta.samplePosition));
            const int note = m.getNoteNumber();
            if (note >= 36 && note < 36 + kNumLanes) groove.lanes.trigger (note - 36, m.getFloatVelocity(), false);
            else if (note >= 48 && note < 48 + kNumLanes) groove.lanes.trigger (note - 48, m.getFloatVelocity(), true);
        }
        renderTo (n);

        // the lane lights and the playhead
        if (running)
        {
            const int stepBefore = Sequencer::stepAt (audioPattern, pos16);
            const int stepAfter = Sequencer::stepAt (audioPattern, pos16 + inc16 * n);
            if (stepAfter != stepBefore || playStep.load() < 0)
                for (int l = 0; l < kNumLanes; ++l)
                    if (audioPattern.lanes[(size_t) l][(size_t) stepAfter].vel > 0)
                        laneHits[l].fetch_add (1, std::memory_order_relaxed);
            playStep = stepAfter;
        }
        else
            playStep = -1;
        if (wasRunning) ownPos16 += inc16 * n;
    }

    //==========================================================================
    Pattern CoveGrooveProcessor::getPattern() const
    {
        const juce::SpinLock::ScopedLockType lock (patternLock);
        return pattern;
    }

    void CoveGrooveProcessor::setPattern (const Pattern& p)
    {
        {
            const juce::SpinLock::ScopedLockType lock (patternLock);
            pattern = p;
        }
        patternSerial.fetch_add (1, std::memory_order_release);
        ++patternVersion;
    }

    void CoveGrooveProcessor::setStep (int lane, int step, Step s)
    {
        auto p = getPattern();
        if (lane < 0 || lane >= kNumLanes || step < 0 || step >= kMaxSteps) return;
        p.lanes[(size_t) lane][(size_t) step] = s;
        setPattern (p);
    }

    void CoveGrooveProcessor::generate (unsigned laneMask)
    {
        auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
        Generator::Settings gs;
        gs.style = (int) v ("style");
        gs.density = v ("density") * 0.01f;
        gs.variation = v ("variation") * 0.01f;
        static const int barChoices[3] = { 1, 2, 4 };
        gs.bars = barChoices[juce::jlimit (0, 2, (int) v ("bars"))];
        for (int l = 0; l < kNumLanes; ++l) gs.models[(size_t) l] = (int) apvts.getRawParameterValue (laneParam (l, "model"))->load();
        auto p = getPattern();
        // one lane alone keeps the pattern's length
        if (laneMask != 0xffu) gs.bars = p.bars;
        Generator::generate (p, gs, (uint32_t) juce::Time::getMillisecondCounter() * 2654435761u + (++seedCounter) * 40503u, laneMask);
        setPattern (p);
    }

    void CoveGrooveProcessor::setBars (int bars)
    {
        auto p = getPattern();
        bars = juce::jlimit (1, kMaxBars, bars);
        if (bars == p.bars) return;
        const int oldLen = p.length();
        for (auto& lane : p.lanes)
            for (int s = oldLen; s < bars * kStepsPerBar; ++s)
                lane[(size_t) s] = lane[(size_t) (s % oldLen)];
        p.bars = bars;
        setPattern (p);
    }

    //==========================================================================
    void CoveGrooveProcessor::setParam (const juce::String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    float CoveGrooveProcessor::getParam (const juce::String& id) const
    {
        if (auto* v = apvts.getRawParameterValue (id)) return v->load();
        return 0.0f;
    }

    // the lanes whose steps follow their sound (cymbal, FX) and whose sound now plays another way
    unsigned CoveGrooveProcessor::rolesChangedMask (const std::array<int, kNumLanes>& before) const
    {
        unsigned mask = 0;
        const int cy = (int) getParam (laneParam (Cymbal, "model")), fx = (int) getParam (laneParam (Fx, "model"));
        if (cymbalRole (cy) != cymbalRole (before[Cymbal])) mask |= 1u << Cymbal;
        if (fxRole (fx) != fxRole (before[Fx])) mask |= 1u << Fx;
        return mask;
    }

    juce::String CoveGrooveProcessor::getProgramCategory (int index)
    {
        return index >= 0 && index < kNumPrograms ? juce::String (kPrograms[index].category) : juce::String();
    }

    int CoveGrooveProcessor::getNumKits() { return kNumKits; }
    juce::String CoveGrooveProcessor::getKitName (int index) { return index >= 0 && index < kNumKits ? juce::String (kKits[index].name) : juce::String(); }

    void CoveGrooveProcessor::applyKit (int index)
    {
        if (index < 0 || index >= kNumKits) return;
        const auto& k = kKits[index];
        std::array<int, kNumLanes> before {};
        for (int l = 0; l < kNumLanes; ++l) before[(size_t) l] = (int) getParam (laneParam (l, "model"));
        for (int l = 0; l < kNumLanes; ++l)
        {
            setParam (laneParam (l, "model"), (float) k.models[l]);
            setParam (laneParam (l, "level"), k.levels[l]);
            setParam (laneParam (l, "tune"), 0.0f);
            setParam (laneParam (l, "decay"), k.decays[l]);
            setParam (laneParam (l, "tone"), 50.0f);
        }
        if (const unsigned mask = rolesChangedMask (before)) generate (mask);
    }

    int CoveGrooveProcessor::getNumFxPresets() { return kNumFxPresets; }
    juce::String CoveGrooveProcessor::getFxPresetName (int index) { return index >= 0 && index < kNumFxPresets ? juce::String (kFxPresets[index].name) : juce::String(); }

    void CoveGrooveProcessor::applyFxPreset (int index)
    {
        if (index < 0 || index >= kNumFxPresets) return;
        const auto& f = kFxPresets[index];
        setParam ("phaser", f.phaser);           setParam ("phaserRate", f.phaserRate);
        setParam ("filterFreq", f.filterFreq);   setParam ("filterReso", f.filterReso);
        setParam ("filterMotion", f.filterMotion); setParam ("filterMix", f.filterMix);
        setParam ("filterRate", (float) f.filterRate); setParam ("filterShape", (float) f.filterShape);
        setParam ("delayTime", (float) f.delayTime); setParam ("delayFeedback", f.delayFb);
        setParam ("delayTone", f.delayTone);     setParam ("delayMix", f.delayMix);
        setParam ("delayPing", f.ping ? 1.0f : 0.0f);
        setParam ("driveMode", (float) f.driveMode); setParam ("drive", f.drive);
        setParam ("driveTone", f.driveTone);     setParam ("driveMix", f.driveMix);
    }

    // the dice, in ranges that keep it a groove: the kick stays near its pitch,
    // the delay on its dotted times more often than not, the drive mostly gentle
    void CoveGrooveProcessor::randomize (Random what)
    {
        auto& r = dice;
        auto between = [&r] (float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); };
        auto pick = [&r] (std::initializer_list<int> options) { return *(options.begin() + r.nextInt ((int) options.size())); };
        std::array<int, kNumLanes> before {};
        for (int l = 0; l < kNumLanes; ++l) before[(size_t) l] = (int) getParam (laneParam (l, "model"));

        if (what == Random::Preset)
        {
            int next = r.nextInt (kNumPrograms);
            if (next == currentProgram) next = (next + 1) % kNumPrograms;
            loadFactory (next);
            return;
        }

        const bool all = what == Random::Everything;
        if (all || what == Random::Kit)
        {
            applyKit (r.nextInt (kNumKits));
            // a lane or two from elsewhere now and then
            if (r.nextFloat() < 0.5f)
                for (int i = 0, n = 1 + r.nextInt (2); i < n; ++i)
                    setParam (laneParam (r.nextInt (kNumLanes), "model"), (float) r.nextInt (kSampleModel));
        }
        if (what == Random::Sounds || all)
        {
            for (int l = 0; l < kNumLanes; ++l)
            {
                if (what == Random::Sounds) setParam (laneParam (l, "model"), (float) r.nextInt (kSampleModel));   // not Sample
                const float tuneRange = l == Kick ? 2.0f : (l == Bass ? 5.0f : 3.0f);
                setParam (laneParam (l, "tune"), std::round (between (-tuneRange, tuneRange)));
                setParam (laneParam (l, "decay"), between (70.0f, 150.0f));
                setParam (laneParam (l, "tone"), between (25.0f, 75.0f));
            }
        }
        if (what == Random::Effects || all)
        {
            setParam ("phaser", r.nextFloat() < 0.25f ? 0.0f : between (10.0f, 60.0f));
            setParam ("phaserRate", between (0.05f, 0.6f));
            setParam ("filterFreq", 600.0f * std::pow (2.0f, between (0.0f, 2.7f)));
            setParam ("filterReso", between (10.0f, 60.0f));
            setParam ("filterMotion", between (10.0f, 85.0f));
            setParam ("filterMix", r.nextFloat() < 0.2f ? 0.0f : between (10.0f, 45.0f));
            setParam ("filterRate", (float) pick ({ 1, 2, 2, 3, 3, 4 }));
            setParam ("filterShape", (float) pick ({ 0, 0, 1, 2 }));
            setParam ("delayTime", (float) pick ({ 1, 3, 3, 3, 2, 5 }));
            setParam ("delayFeedback", between (20.0f, 60.0f));
            setParam ("delayTone", 2000.0f * std::pow (2.0f, between (0.0f, 2.2f)));
            setParam ("delayMix", between (8.0f, 32.0f));
            setParam ("delayPing", r.nextFloat() < 0.7f ? 1.0f : 0.0f);
            setParam ("driveMode", (float) pick ({ 0, 0, 1, 1, 2, 3 }));
            setParam ("drive", between (5.0f, 50.0f));
            setParam ("driveTone", between (-35.0f, 30.0f));
            setParam ("driveMix", between (65.0f, 100.0f));
        }
        if (what == Random::Mix || all)
        {
            for (int l = 0; l < kNumLanes; ++l)
            {
                const float lvl = kLaneDefaults[l].level;
                setParam (laneParam (l, "level"), juce::jlimit (0.0f, 150.0f, lvl * between (0.8f, 1.2f)));
                const bool centre = l == Kick || l == Bass;
                setParam (laneParam (l, "pan"), centre ? 0.0f : between (-45.0f, 45.0f));
                setParam (laneParam (l, "send"), centre ? 0.0f : (r.nextFloat() < 0.4f ? 0.0f : between (5.0f, 45.0f)));
                if (! centre) setParam (laneParam (l, "top"), r.nextFloat() < (l >= ClosedHat ? 0.75f : 0.3f) ? 1.0f : 0.0f);
            }
        }
        if (what == Random::Groove || all)
        {
            setParam ("style", (float) r.nextInt (NumStyles));
            setParam ("density", between (25.0f, 80.0f));
            setParam ("variation", between (20.0f, 70.0f));
            setParam ("swing", between (54.0f, 70.0f));
            generate();
        }
        else if (const unsigned mask = rolesChangedMask (before))
            generate (mask);
    }

    //==========================================================================
    void CoveGrooveProcessor::loadFactory (int index)
    {
        index = juce::jlimit (0, kNumPrograms - 1, index);
        const auto& pr = kPrograms[index];
        auto set = [this] (const juce::String& id, float value)
        {
            if (auto* prm = apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        };
        // everything back to the defaults first
        for (auto* prm : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                r->setValueNotifyingHost (r->getDefaultValue());
        set ("style", (float) pr.style);
        set ("bars", pr.bars == 4 ? 2.0f : (pr.bars == 2 ? 1.0f : 0.0f));
        set ("density", pr.density * 100.0f);
        set ("variation", pr.variation * 100.0f);
        set ("swing", pr.swing);
        set ("tempo", pr.tempo);
        for (int l = 0; l < kNumLanes; ++l)
        {
            set (laneParam (l, "model"), (float) pr.models[l]);
            set (laneParam (l, "level"), pr.levels[l]);
            set (laneParam (l, "tune"), pr.tunes[l]);
            set (laneParam (l, "decay"), pr.decays[l] > 0.0f ? pr.decays[l] : 100.0f);
        }
        set ("phaser", pr.phaser);
        set ("filterFreq", pr.filterFreq);
        set ("filterReso", pr.filterReso);
        set ("filterMotion", pr.filterMotion);
        set ("filterMix", pr.filterMix);
        set ("filterRate", (float) pr.filterRate);
        set ("filterShape", (float) pr.filterShape);
        set ("delayTime", (float) pr.delayTime);
        set ("delayFeedback", pr.delayFb);
        set ("delayMix", pr.delayMix);
        set ("driveMode", (float) pr.driveMode);
        set ("drive", pr.drive);
        set ("driveTone", pr.driveTone);
        set ("volume", (pr.volume > 0.0f ? pr.volume : 80.0f) * 0.54f);   // the kits' levels, brought down to sit with the other sources

        Generator::Settings gs;
        gs.style = pr.style; gs.density = pr.density; gs.variation = pr.variation; gs.bars = pr.bars;
        for (int l = 0; l < kNumLanes; ++l) gs.models[(size_t) l] = pr.models[l];
        Pattern p;
        Generator::generate (p, gs, (uint32_t) (index + 1) * 7919u);
        setPattern (p);
        currentProgram = index;
    }

    int CoveGrooveProcessor::getNumPrograms() { return kNumPrograms; }
    void CoveGrooveProcessor::setCurrentProgram (int index) { loadFactory (index); }
    const juce::String CoveGrooveProcessor::getProgramName (int index)
    {
        return index >= 0 && index < kNumPrograms ? juce::String (kPrograms[index].name) : juce::String();
    }

    //==========================================================================
    juce::ValueTree CoveGrooveProcessor::stateTree()
    {
        auto t = apvts.copyState();
        t.setProperty ("pattern", juce::String (getPattern().toString()), nullptr);
        t.setProperty ("program", currentProgram, nullptr);

        // the lanes' own samples (as 16-bit FLAC) and their curves
        juce::ValueTree smp ("Samples");
        {
            const juce::ScopedLock sl (sampleMutex);
            for (int l = 0; l < kNumLanes; ++l)
            {
                const auto& ls = laneSamples[(size_t) l];
                if (ls.data == nullptr && ls.curve.isFlat()) continue;
                juce::ValueTree lt ("Lane");
                lt.setProperty ("index", l, nullptr);
                lt.setProperty ("curve", juce::String (ls.curve.toString()), nullptr);
                if (ls.data != nullptr)
                {
                    auto& code = sampleCodes[(size_t) l];
                    if (code.isEmpty()) code = samples::encode (*ls.data);
                    lt.setProperty ("name", ls.name, nullptr);
                    lt.setProperty ("audio", code, nullptr);
                }
                smp.appendChild (lt, nullptr);
            }
        }
        if (smp.getNumChildren() > 0) t.appendChild (smp, nullptr);
        return t;
    }

    void CoveGrooveProcessor::restoreTree (const juce::ValueTree& t)
    {
        if (! t.isValid() || ! t.hasType (apvts.state.getType())) return;
        auto copy = t.createCopy();
        const auto smp = copy.getChildWithName ("Samples");
        if (smp.isValid()) copy.removeChild (smp, nullptr);
        apvts.replaceState (copy);
        Pattern p;
        if (p.fromString (t.getProperty ("pattern").toString().toStdString()))
            setPattern (p);
        currentProgram = (int) t.getProperty ("program", 0);
        restoreSamples (smp);
    }

    // a state without samples (an older one) leaves every lane without
    void CoveGrooveProcessor::restoreSamples (const juce::ValueTree& smp)
    {
        const juce::ScopedLock sl (sampleMutex);
        for (int l = 0; l < kNumLanes; ++l)
        {
            LaneSample ls;
            juce::String code;
            for (const auto lt : smp)
                if (lt.hasType ("Lane") && (int) lt.getProperty ("index", -1) == l)
                {
                    code = lt.getProperty ("audio").toString();
                    ls.data = samples::decode (code);
                    if (ls.data == nullptr) code.clear();
                    ls.name = lt.getProperty ("name").toString();
                    ls.curve.fromString (lt.getProperty ("curve").toString().toStdString());
                }
            if (laneSamples[(size_t) l].data != nullptr) retired.push_back (laneSamples[(size_t) l].data);
            laneSamples[(size_t) l] = ls;
            sampleCodes[(size_t) l] = code;
        }
        publishSamples();
    }

    //==========================================================================
    bool CoveGrooveProcessor::loadLaneSample (int lane, const juce::File& file)
    {
        auto data = samples::readFile (file);
        if (data == nullptr) return false;
        setLaneSample (lane, std::move (data), file.getFileNameWithoutExtension());
        setParam (laneParam (lane, "model"), (float) kSampleModel);
        return true;
    }

    void CoveGrooveProcessor::setLaneSample (int lane, std::shared_ptr<const SampleData> data, const juce::String& name)
    {
        if (lane < 0 || lane >= kNumLanes) return;
        const juce::String code = data != nullptr ? samples::encode (*data) : juce::String();
        const juce::ScopedLock sl (sampleMutex);
        auto& ls = laneSamples[(size_t) lane];
        if (ls.data != nullptr) retired.push_back (ls.data);
        ls.data = std::move (data);
        ls.name = ls.data != nullptr ? name : juce::String();
        sampleCodes[(size_t) lane] = code;
        publishSamples();
    }

    void CoveGrooveProcessor::clearLaneSample (int lane) { setLaneSample (lane, nullptr, {}); }

    void CoveGrooveProcessor::setLaneCurve (int lane, const Curve& c)
    {
        if (lane < 0 || lane >= kNumLanes) return;
        const juce::ScopedLock sl (sampleMutex);
        auto& curve = laneSamples[(size_t) lane].curve;
        curve = c;
        curve.tidy();
        publishSamples();
    }

    CoveGrooveProcessor::LaneSample CoveGrooveProcessor::getLaneSample (int lane) const
    {
        const juce::ScopedLock sl (sampleMutex);
        return laneSamples[(size_t) juce::jlimit (0, kNumLanes - 1, lane)];
    }

    void CoveGrooveProcessor::publishSamples()
    {
        {
            const juce::SpinLock::ScopedLockType lock (handoffLock);
            for (int l = 0; l < kNumLanes; ++l)
            {
                handoffData[(size_t) l] = laneSamples[(size_t) l].data;
                handoffCurve[(size_t) l] = laneSamples[(size_t) l].curve;
            }
        }
        sampleSerial.fetch_add (1, std::memory_order_release);
        ++sampleVersion;
        purgeRetired();
    }

    void CoveGrooveProcessor::purgeRetired()
    {
        retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& d) { return d.use_count() <= 1; }), retired.end());
    }

    void CoveGrooveProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        if (auto xml = stateTree().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void CoveGrooveProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            restoreTree (juce::ValueTree::fromXml (*xml));
    }

    bool CoveGrooveProcessor::savePreset (const juce::File& f)
    {
        if (auto xml = stateTree().createXml())
            return xml->writeTo (f);
        return false;
    }

    bool CoveGrooveProcessor::loadPreset (const juce::File& f)
    {
        if (auto xml = juce::XmlDocument::parse (f))
        {
            const auto t = juce::ValueTree::fromXml (*xml);
            if (t.hasType (apvts.state.getType())) { restoreTree (t); return true; }
        }
        return false;
    }

    juce::AudioProcessorEditor* CoveGrooveProcessor::createEditor() { return new CoveGrooveEditor (*this); }
}

namespace aqp_covegroove
{
    juce::AudioProcessor* createPluginFilter() { return new covegroove::CoveGrooveProcessor(); }
}
