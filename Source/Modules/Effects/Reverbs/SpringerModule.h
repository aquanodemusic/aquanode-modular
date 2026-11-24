#pragma once

#include "../../Core/ModuleCore.h"

// Spring Reverb - two engines:
//
// Tank (the default): a model of a classic spring tank, after the parametric
// spring model in the literature (cascades of stretched all-pass filters in a
// delay loop), fitted to measured impulse responses of real tanks. Each spring
// is a loop: its transducer's signal runs through a cascade of stretched
// first-order all-passes - below the spring's transition frequency the high
// notes travel slower than the low ones, so every pass arrives as a rising
// chirp that bends up toward the transition (the "drip"; about 40 ms between
// its low and its top end, as measured) - then the loop delay (Size: the time
// down the spring and back) and the loss of each round trip (Resonance sets
// the decay, Damping how much sooner the highs die). Blur Rate is the springs'
// imperfection: the loop length trembles a little and each pass scatters a
// little, so the clean early chirps blur into the dense, washy tail of a real
// tank. Chirp Tone is the springs' transition frequency - where the chirp bends
// up to, set on a real tank by the wire's thickness and the coil's size: the
// stretch of the all-passes follows it smoothly (whole samples plus a
// first-order fractional all-pass, so it stays an all-pass at any setting). Out of the tank, a steep low-pass at the transition (as the springs
// themselves carry nothing much above it). A second, quieter path carries the
// faster, falling chirps above the transition (Drip). Up to four springs
// (Coils) of slightly different length and tension spread across the stereo
// field; Stages is the length of the all-pass cascade (how far the chirp is
// spread), Chirp the all-passes' strength.
//
// Coils (the engine of 5.15 and before - patches made with it open with it):
// up to seven parallel feedback "coils", each a delay -> density all-pass ->
// N-stage all-pass dispersion chain -> low-pass damping -> tanh.
//
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpringerModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pSizeMs = 0, pCoils, pStages, pResonance, pDamping, pChirp, pDryWet, pModel, pDrip, pTone, pChaos, pChirpTone };
    static constexpr int maxCoils = 7;
    static constexpr int maxStages = 128;          // Coils
    static constexpr int maxTankStages = 200;      // Tank
    static constexpr double kMinChirpHz = 1000.0, kMaxChirpHz = 8000.0;   // Chirp Tone

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

private:
    //=== Coils (the original engine) ==========================================
    struct Coil
    {
        std::vector<float> delayBuf;
        int writePos { 0 };
        std::vector<float> densityBuf;
        int densityPos { 0 };
        float apMem[maxStages] {};
        float coeffs[maxStages] {};
        float dampingMem { 0.0f };
        float hpMem { 0.0f };
        float lastOut { 0.0f };
    };
    void processCoils (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs);
    Coil coils[maxCoils];

    //=== Tank =================================================================
    static constexpr int kMaxSprings = 4;
    static constexpr int kHfStages = 48;

    struct Biquad
    {
        float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }, z1 { 0 }, z2 { 0 };
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0.0f; }
        void set (double fc, double q, double sr, bool highpass) noexcept;
        void setHighShelf (double fc, double gainDb, double sr) noexcept;
        void setNotch (double fc, double q, double sr) noexcept;
    };

    struct Spring
    {
        // the stretched all-pass cascade: one line per stage (lineLen apart),
        // read N samples back and through a first-order fractional all-pass
        std::vector<float> ap;            // lineLen * maxTankStages (position-major: [pos * maxTankStages + stage])
        std::vector<float> fx1, fy1;      // the fractional all-passes' memories
        int readN { 4 };                  // whole samples of the stretch
        float eta { 0.0f };               // the fractional part's all-pass coefficient
        double stretch { 5.0 };           // K = N + fraction
        Biquad shelf;                     // the round trip's high loss
        std::vector<float> loop;          // the round trip
        int loopPos { 0 };
        float loopOut { 0.0f };
        float dc { 0.0f };
        // the scattering (two short all-passes)
        std::vector<float> sc1, sc2;
        int sc1Pos { 0 }, sc2Pos { 0 };
        // the loop length's tremble (smoothed random)
        double chaos { 0.0 }, chaos1 { 0.0 }, chaosTarget { 0.0 };
        int chaosCount { 0 };
        // the high chirp path
        float hfMem[kHfStages] {};
        Biquad hfHp1, hfHp2, hfLp;
        std::vector<float> hfLoop;
        int hfPos { 0 };
        float hfOut { 0.0f };
    };
    void processTank (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs);
    void updateTank();

    Spring springs[kMaxSprings];
    int apPos { 0 };                      // shared write position in every stage's line
    int lineLen { 32 };
    float lastTankKey[8] { -1, -1, -1, -1, -1, -1, -1, -1 };
    Biquad outHp[2], toneLp[2], foldLp[2][4], foldNotch[2];
    juce::Random rng { 0x5e9a };
    float tankFb[kMaxSprings] {}, hfFb[kMaxSprings] {};
};
