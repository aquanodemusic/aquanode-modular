#pragma once
// AM-20's built-in sounds: Init, sounds brought over from the original, and
// sounds made for AM-20. The original's own factory programs load from the
// user's own .fxp files.
#include "PresetIO.h"
#include <initializer_list>

namespace am20::factory
{
// handy values for the stepped controls
constexpr float Tri = 0.0f, Saw1 = 1.0f / 3.0f, Pulse = 2.0f / 3.0f, Noise = 1.0f;          // VCO 1
constexpr float Saw2 = 0.0f, Square = 1.0f / 3.0f, Pulse10 = 2.0f / 3.0f, Ring = 1.0f;      // VCO 2
constexpr float S32 = 0.0f, S16 = 1.0f / 3.0f, S8 = 2.0f / 3.0f, S4 = 1.0f;                 // VCO 1 scale
constexpr float V16 = 0.0f, V8 = 1.0f / 3.0f, V4 = 2.0f / 3.0f, V2 = 1.0f;                  // VCO 2 scale
inline float semis (float st) { return 0.5f + st / 24.0f; }                                 // VCO 2 pitch
inline float voices (int n) { return (float) (n - 1) / 31.0f; }
inline float unison (int n) { return (float) (n - 1) / 15.0f; }
inline float amount (float a) { return 0.5f + a / 10.0f; }                                   // ext mod -5..+5

struct Set { int p; float v; };
struct Patch { int out, in; };

inline Preset make (const char* name, std::initializer_list<Set> sets, std::initializer_list<Patch> patches = {})
{
    Preset p; p.name = name;
    for (auto& s : sets) p.values[s.p] = s.v;
    for (auto& c : patches) p.cables.push_back ({ c.out, c.in });
    return p;
}

// sounds made in the original and brought over (all but Vaporizing Pad reworked a little for AM-20)
struct Sound { const char* name; float v[NumParams]; int c[17][2]; };
inline const std::vector<Sound>& sounds()
{
    static const std::vector<Sound> s {
        { "Vaporizing Pad",
          { 0.333333f, 0.0f, 0.666667f, 0.0f, 0.0f, 0.791667f, 0.333333f, 0.5f, 0.265f, 0.255f, 0.0f, 0.0f,
            0.648438f, 0.61f, 0.0f, 0.82f, 0.492188f, 0.465f, 0.345f, 0.0f, 1.0f, 0.41f, 0.0f, 0.36f,
            0.525f, 0.0f, 0.28f, 0.5f, 1.0f, 0.525f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.225806f, 0.0666667f, 0.3f, 0.5f, 0.5f, 0.166667f, 0.05f, 0.5f, 1.0f, 0.0f,
            0.777778f, 0.933333f, 0.142857f, 0.357143f, 0.5f, 0.5f, 0.75f, 0.75f, 0.4f, 0.5f, 0.5f, 0.5f,
            0.55f, 0.5f },
          { { 19, 2 }, { 6, 4 }, { 7, 13 } } },
        { "Vapor Haze",
          { 0.333333f, 0.0f, 0.666667f, 0.0f, 0.0f, 0.791667f, 0.333333f, 0.5f, 0.30484f, 0.2376f, 0.0f, 0.0f,
            0.652677f, 0.62888f, 0.0f, 0.82f, 0.492788f, 0.47552f, 0.345f, 0.0f, 1.0f, 0.40564f, 0.0f, 0.35676f,
            0.48728f, 0.0f, 0.27968f, 0.4968f, 1.0f, 0.54924f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.225806f, 0.0666667f, 0.3f, 0.5f, 0.5f, 0.166667f, 0.12005f, 0.5f, 1.0f, 0.0f,
            0.777778f, 0.933333f, 0.142857f, 0.357143f, 0.5f, 0.5f, 0.75f, 0.75f, 0.4f, 0.5f, 0.5f, 0.5f,
            0.55f, 0.5f },
          { { 19, 2 }, { 6, 4 }, { 7, 13 } } },
        { "Deep Sea Pad",
          { 0.333333f, 0.0f, 0.666667f, 0.0f, 0.0f, 0.508333f, 0.333333f, 0.5f, 0.77724f, 0.78732f, 0.0f, 0.0f,
            0.444003f, 0.19248f, 0.81f, 0.0f, 0.578848f, 0.1169f, 0.81f, 0.0f, 0.505f, 0.0f, 0.0f, 0.00656f,
            0.02584f, 0.0f, 0.29032f, 0.50068f, 1.0f, 0.65816f, 0.25f, 0.0f, 1.0f, 0.0f, 0.0f, 0.53f,
            1.0f, 0.0f, 0.225806f, 0.0f, 0.265f, 1.0f, 0.5f, 0.166667f, 0.07465f, 0.5f, 1.0f, 0.0f,
            0.111111f, 0.533333f, 0.0714286f, 0.142857f, 0.5f, 0.5f, 0.5f, 0.5f, 0.71f, 0.5f, 0.5f, 0.62f,
            0.62f, 0.5f },
          { { 23, 3 }, { 8, 9 } } },
        { "Poly Twenty",
          { 0.333333f, 0.0f, 0.666667f, 0.0f, 0.0f, 0.504167f, 0.333333f, 0.5f, 0.77464f, 0.81696f, 0.045f, 0.0f,
            0.26408f, 0.26188f, 0.0f, 0.0f, 0.596883f, 0.30342f, 0.0f, 0.875f, 0.5f, 0.3366f, 0.0f, 0.0f,
            0.00388f, 0.0f, 0.27004f, 0.5568f, 0.67988f, 0.7388f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.56f,
            1.0f, 1.0f, 0.354839f, 0.0f, 0.3f, 1.0f, 0.5f, 0.166667f, 0.0946f, 0.5f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.142857f, 0.0714286f, 0.5f, 0.5f, 0.75f, 0.745f, 0.5f, 0.5f, 0.5f, 0.5f,
            0.53f, 0.5f },
          { { 22, 17 }, { 1, 12 }, { 16, 0 }, { 27, 14 } } },
        { "Unison Stabber",
          { 0.333333f, 0.0f, 0.333333f, 0.0f, 0.0f, 0.508333f, 0.0f, 0.5f, 0.75164f, 0.73192f, 0.0f, 0.015f,
            0.654013f, 0.34686f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.345f, 0.495f, 0.01176f, 0.0f, 0.09332f,
            0.45092f, 0.0f, 0.0f, 0.28528f, 0.52668f, 0.05864f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.225806f, 0.0f, 0.335f, 0.54f, 0.5f, 0.166667f, 0.10085f, 0.5f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0714286f, 0.357143f, 0.5f, 0.5f, 0.65f, 0.535f, 0.9f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f },
          {  } },
        { "Stuttering Synth",
          { 0.333333f, 0.0f, 0.666667f, 0.0f, 0.0f, 0.791667f, 0.333333f, 0.5f, 1.0f, 0.92372f, 0.0f, 0.0f,
            0.398545f, 0.2043f, 0.0f, 0.805f, 0.0306925f, 0.0f, 0.945f, 0.375f, 0.595f, 0.4392f, 0.0f, 0.01372f,
            0.54812f, 0.0f, 0.0f, 0.75956f, 0.0f, 0.55568f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.225806f, 0.0666667f, 0.375f, 1.0f, 0.5f, 0.166667f, 0.09175f, 0.5f, 1.0f, 1.0f,
            0.777778f, 1.0f, 0.0714286f, 0.357143f, 0.5f, 0.5f, 0.545f, 0.5f, 0.605f, 0.5f, 0.5f, 0.54f,
            0.5f, 0.5f },
          { { 6, 0 }, { 10, 21 } } },
        { "Fresh Lead",
          { 0.333333f, 0.0f, 0.333333f, 0.3552f, 1.0f, 0.504167f, 0.666667f, 0.5f, 0.84736f, 0.68492f, 0.0f, 0.0f,
            0.398367f, 0.60398f, 0.41f, 0.0f, 0.626222f, 0.66422f, 0.0f, 0.0f, 0.26f, 0.21288f, 0.0f, 0.00376f,
            0.00916f, 0.0f, 0.02084f, 0.49504f, 1.0f, 0.00344f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.3f, 0.0f, 0.5f, 0.166667f, 0.05975f, 0.5f, 1.0f, 1.0f,
            0.333333f, 1.0f, 0.142857f, 0.357143f, 0.5f, 0.79f, 0.84f, 0.75f, 0.425f, 0.5f, 0.5f, 0.595f,
            0.655f, 0.5f },
          {  } },
        { "Drill Lead",
          { 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.5875f, 1.0f, 0.5f, 1.0f, 1.0f, 0.49f, 1.0f,
            0.208485f, 0.5253f, 0.4f, 0.0f, 0.47664f, 0.51216f, 0.495f, 1.0f, 0.5f, 0.20456f, 0.0f, 0.47068f,
            0.51608f, 0.0f, 0.0266f, 0.91936f, 0.49048f, 0.72036f, 0.5f, 0.615f, 0.615f, 1.0f, 0.0f, 0.83f,
            0.0f, 0.0f, 0.0322581f, 0.0f, 1.0f, 0.42f, 0.5f, 0.166667f, 0.09585f, 0.5f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0714286f, 0.357143f, 0.5f, 0.5f, 0.5f, 0.5f, 0.765f, 0.5f, 0.5f, 0.64f,
            0.5f, 0.5f },
          { { 30, 3 }, { 20, 0 }, { 15, 9 } } },
        { "Gliding Lead",
          { 0.333333f, 0.0f, 0.666667f, 0.0f, 0.0f, 0.795833f, 0.333333f, 0.5f, 0.50168f, 0.48616f, 0.0f, 1.0f,
            0.0198f, 0.0f, 0.0f, 0.0f, 0.572343f, 0.44148f, 0.0f, 1.0f, 0.5f, 0.35028f, 0.0f, 0.4074f,
            1.0f, 0.0f, 0.4016f, 0.89532f, 0.50724f, 0.18704f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 1.0f, 0.16129f, 0.0f, 0.3f, 0.0f, 0.5f, 0.166667f, 0.07905f, 0.5f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0714286f, 0.357143f, 0.5f, 0.5f, 0.5f, 0.5f, 0.64f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f },
          { { 11, 5 } } },
        { "Hard Edge",
          { 0.333333f, 0.965f, 0.333333f, 0.0f, 0.0f, 0.504167f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.52524f, 0.70942f, 0.0f, 0.425f, 0.52352f, 0.6753f, 0.0f, 0.425f, 0.5f, 0.35728f, 0.0f, 0.02864f,
            0.01004f, 0.0f, 0.69652f, 0.77036f, 0.10172f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.235f, 0.5f, 0.5f, 0.166667f, 0.0871f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.8f, 0.142857f, 0.357143f, 0.5f, 0.5f, 0.75f, 0.75f, 0.5f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f },
          { { 10, 21 } } },
        { "Slide One",
          { 0.666667f, 0.815f, 0.333333f, 0.24996f, 1.0f, 0.6625f, 0.333333f, 0.5f, 1.0f, 0.84984f, 0.0f, 0.665f,
            0.381148f, 0.57392f, 0.9f, 0.0f, 0.610403f, 0.6204f, 0.92f, 0.0f, 1.0f, 0.34212f, 0.0f, 0.01244f,
            0.34652f, 0.0f, 0.2272f, 0.0f, 1.0f, 0.11068f, 0.935f, 0.0f, 1.0f, 0.0f, 0.0f, 0.74f,
            0.0f, 0.0f, 0.0322581f, 0.0666667f, 0.28f, 1.0f, 0.5f, 0.166667f, 0.0602f, 0.5f, 1.0f, 1.0f,
            0.555556f, 1.0f, 0.0714286f, 0.357143f, 0.5f, 0.5f, 0.5f, 0.5f, 0.725f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f },
          { { 19, 2 }, { 6, 4 }, { 7, 0 }, { 10, 21 } } },
        { "Slide Two",
          { 0.333333f, 0.0f, 0.666667f, 0.51368f, 0.0f, 0.791667f, 0.333333f, 0.5f, 0.68932f, 0.71672f, 0.0f, 0.0f,
            0.501482f, 0.36832f, 1.0f, 0.83f, 0.670998f, 0.5917f, 0.995f, 0.805f, 1.0f, 0.21024f, 0.0f, 0.87844f,
            0.0f, 0.0f, 0.03724f, 0.63428f, 0.67552f, 0.62392f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.605f,
            1.0f, 1.0f, 0.0967742f, 0.0666667f, 0.315f, 1.0f, 0.5f, 0.166667f, 0.0936f, 0.5f, 1.0f, 1.0f,
            0.555556f, 1.0f, 0.0714286f, 0.357143f, 0.5f, 0.5f, 0.53f, 0.53f, 0.8f, 0.5f, 0.5f, 0.57f,
            0.49f, 0.5f },
          { { 6, 4 }, { 23, 2 }, { 7, 0 }, { 1, 12 }, { 16, 13 } } },
        { "Acidic Bass",
          { 0.333333f, 0.0f, 0.333333f, 0.20936f, 0.0f, 0.5f, 0.333333f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.011f, 0.01802f, 0.0f, 0.0f, 0.354985f, 0.60776f, 0.36f, 0.785f, 0.5f, 0.1698f, 0.0f, 0.02472f,
            0.0f, 0.0f, 0.0f, 0.40032f, 0.1404f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.3f, 0.0f, 0.5f, 0.166667f, 0.055f, 0.5f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0714286f, 0.142857f, 0.5f, 0.5f, 0.5f, 0.64f, 0.5f, 0.5f, 0.5f, 0.5f,
            0.73f, 0.5f },
          { { 27, 0 } } },
        { "Punchy Bass",
          { 0.0f, 0.0f, 0.333333f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.96796f, 0.0093f, 0.0f, 0.0f, 0.5f, 0.4546f, 0.0f, 0.0f,
            0.02432f, 0.0f, 0.01744f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.3f, 0.535f, 0.5f, 0.166667f, 0.1477f, 0.5f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0714286f, 0.357143f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f },
          { { 10, 21 }, { 29, 9 }, { 16, 5 }, { 11, 12 }, { 22, 14 } } },
        { "Reversed Bass",
          { 0.666667f, 0.0f, 0.333333f, 0.0f, 0.0f, 0.5f, 0.333333f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.02228f, 0.0f, 0.0f, 0.0f, 0.274213f, 0.45404f, 0.0f, 0.83f, 0.0f, 0.18896f, 0.0f, 0.27708f,
            0.21308f, 0.0f, 0.0f, 0.49052f, 1.0f, 0.17864f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.3f, 0.0f, 0.5f, 0.166667f, 0.1264f, 0.5f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.142857f, 0.357143f, 0.5f, 0.5f, 0.5f, 0.71f, 0.375f, 0.5f, 0.5f, 0.715f,
            0.635f, 0.5f },
          { { 10, 17 } } },
        { "VC Ten",
          { 0.666667f, 0.625f, 0.666667f, 0.0f, 0.666667f, 0.508333f, 0.333333f, 0.5f, 0.31124f, 0.27956f, 0.0f, 0.0f,
            0.42745f, 0.48634f, 0.67f, 0.0f, 0.573845f, 0.49752f, 0.0f, 0.4f, 0.31f, 0.0f, 0.0f, 0.56936f,
            0.57216f, 0.0f, 0.25788f, 0.39332f, 0.67288f, 0.28404f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.290323f, 0.0666667f, 0.3f, 1.0f, 0.5f, 0.166667f, 0.1387f, 0.5f, 1.0f, 0.0f,
            0.0f, 1.0f, 0.0714286f, 0.214286f, 0.5f, 0.5f, 0.56f, 0.47f, 0.5f, 0.5f, 0.5f, 0.46f,
            0.53f, 0.5f },
          { { 6, 18 }, { 11, 17 } } },
        { "Minimal Tone",
          { 0.333333f, 0.0f, 0.666667f, 0.45508f, 0.333333f, 0.495833f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.318f, 0.385f, 0.0f, 0.438592f, 0.64784f, 0.0f, 0.0f, 0.5f, 0.47092f, 0.315f, 0.0f,
            0.39848f, 0.0f, 0.0f, 0.27536f, 0.0f, 0.29188f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.96f,
            1.0f, 0.0f, 0.225806f, 0.0666667f, 0.3f, 1.0f, 0.5f, 1.0f, 0.51f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.533333f, 0.0714286f, 0.214286f, 0.5f, 0.5f, 0.85f, 0.76f, 0.495f, 0.5f, 0.5f, 0.5f,
            0.5f, 0.5f },
          { { 27, 0 }, { 23, 12 }, { 16, 9 }, { 11, 14 } } },
    };
    return s;
}

inline std::vector<Preset> presets()
{
    // the classic mod-wheel vibrato: MG through the modulation VCA, opened by the wheel, into TOTAL
    const std::initializer_list<Patch> wheelVibrato { { jMgTri, jMvcaIn }, { jWheel, jMvcaCtl }, { jMvcaOut, jTotal } };
    std::vector<Preset> v;
    v.push_back (make ("Init", {}));
    for (auto& snd : sounds())
    {
        Preset p; p.name = snd.name;
        for (int i = 0; i < NumParams; ++i) p.values[i] = snd.v[i];
        for (auto& c : snd.c) if (c[0] != c[1]) p.cables.push_back ({ c[0], c[1] });
        v.push_back (p);
    }
    v.push_back (make ("Fat Mono Bass", { { Assign, 0 }, { VCO1Wave, Saw1 }, { VCO1Scale, S16 }, { VCO2Wave, Square }, { VCO2Scale, V16 }, { VCO2Tune, semis (0.08f) },
                                          { VCO1Level, 0.95f }, { VCO2Level, 0.8f }, { HPFCut, 0.12f }, { LPFCut, 0.33f }, { LPFPeak, 0.5f }, { LPFEG2, 0.72f },
                                          { EG2Attack, 0.0f }, { EG2Decay, 0.38f }, { EG2Sustain, 0.3f }, { EG2Release, 0.15f }, { Porta, 0.0f } }));
    v.push_back (make ("Acid Squelch", { { Assign, 0 }, { VCO1Wave, Saw1 }, { VCO1Scale, S16 }, { VCO1Level, 1.0f }, { LPFCut, 0.28f }, { LPFPeak, 0.74f },
                                         { LPFEG2, 0.82f }, { HPFCut, 0.1f }, { EG2Decay, 0.32f }, { EG2Sustain, 0.0f }, { EG2Release, 0.12f },
                                         { Porta, 0.45f }, { ExtSrc1, 1.0f / 14.0f }, { Ext1LPF, amount (0.8f) } }));
    v.push_back (make ("Screaming Lead", { { Assign, 0 }, { VCO1Wave, Saw1 }, { VCO1Scale, S8 }, { VCO2Wave, Square }, { VCO2Scale, V8 }, { VCO2Tune, semis (0.12f) },
                                           { VCO1Level, 0.9f }, { VCO2Level, 0.75f }, { HPFCut, 0.32f }, { HPFPeak, 0.68f }, { LPFCut, 0.56f }, { LPFPeak, 0.66f },
                                           { LPFEG2, 0.45f }, { EG2Attack, 0.05f }, { EG2Decay, 0.5f }, { EG2Sustain, 0.7f }, { EG2Release, 0.25f },
                                           { Porta, 0.32f }, { PitchMG, 0.25f }, { MGFreq, 0.28f }, { MGWave, 0.5f } }, wheelVibrato));
    v.push_back (make ("Ring Bell", { { VCO1Level, 0.0f }, { VCO1Wave, Pulse }, { VCO2Wave, Ring }, { VCO2Tune, semis (5.4f) }, { VCO2Level, 0.9f },
                                      { HPFCut, 0.25f }, { LPFCut, 0.78f }, { LPFPeak, 0.2f }, { EG2Attack, 0.0f }, { EG2Decay, 0.62f }, { EG2Sustain, 0.0f },
                                      { EG2Release, 0.58f } }));
    v.push_back (make ("Analog Strings", { { PolyVoices, voices (8) }, { UnisonVoices, unison (2) }, { UnisonDetune, 0.22f }, { UnisonSpread, 0.7f },
                                           { VCO1Wave, Saw1 }, { VCO2Wave, Saw2 }, { VCO2Scale, V8 }, { VCO2Tune, semis (0.1f) }, { VCO1Level, 0.8f }, { VCO2Level, 0.8f },
                                           { HPFCut, 0.3f }, { LPFCut, 0.56f }, { LPFPeak, 0.12f }, { LPFEG2, 0.2f },
                                           { EG2Attack, 0.45f }, { EG2Decay, 0.5f }, { EG2Sustain, 0.85f }, { EG2Release, 0.56f },
                                           { PitchMG, 0.04f }, { MGFreq, 0.27f }, { Analog, 0.3f } }));
    v.push_back (make ("Slow Sweep Pad", { { PolyVoices, voices (8) }, { VCO1Wave, Pulse }, { VCO1PW, 0.35f }, { VCO2Wave, Saw2 }, { VCO2Scale, V8 }, { VCO2Tune, semis (-0.08f) },
                                           { VCO1Level, 0.85f }, { VCO2Level, 0.7f }, { LPFCut, 0.46f }, { LPFPeak, 0.58f }, { LPFMG, 0.38f },
                                           { MGFreq, 0.012f }, { MGWave, 0.5f }, { EG2Attack, 0.58f }, { EG2Decay, 0.6f }, { EG2Sustain, 0.9f }, { EG2Release, 0.62f } }));
    v.push_back (make ("S&H Bleeps", { { Assign, 0 }, { VCO1Level, 0.0f }, { LPFCut, 0.46f }, { LPFPeak, 0.84f }, { LPFEG2, 0.55f },
                                       { MGFreq, 0.3f }, { MGWave, 0.5f }, { EG2Sustain, 1.0f }, { EG2Release, 0.35f } },
                                     { { jPink, jShIn }, { jMgRect, jShClock }, { jShOut, jLpCut } }));
    v.push_back (make ("Noise Snare", { { VCO1Wave, Noise }, { VCO1Level, 1.0f }, { VCO2Wave, Square }, { VCO2Scale, V4 }, { VCO2Level, 0.35f },
                                        { HPFCut, 0.42f }, { HPFPeak, 0.35f }, { LPFCut, 0.74f }, { EG2Decay, 0.3f }, { EG2Sustain, 0.0f }, { EG2Release, 0.3f } }));
    v.push_back (make ("Wobble Bass", { { Assign, 0 }, { VCO1Wave, Saw1 }, { VCO1Scale, S16 }, { VCO2Wave, Square }, { VCO2Scale, V16 }, { VCO1Level, 0.9f }, { VCO2Level, 0.9f },
                                        { LPFCut, 0.3f }, { LPFPeak, 0.62f }, { LPFMG, 0.55f }, { MGWave, 0.5f }, { MGTempoSync, 1.0f }, { MGBaseNote, 5.0f / 9.0f },
                                        { MGTimes, 1.0f }, { MGKeySync, 1.0f }, { EG2Sustain, 1.0f }, { EG2Release, 0.1f } }));
    v.push_back (make ("Laser Zap", { { Assign, 0 }, { VCO1Wave, Pulse }, { VCO1PW, 0.2f }, { VCO1Level, 1.0f }, { PitchEG1, 0.6f }, { EG1Attack, 0.33f },
                                      { LPFCut, 0.7f }, { LPFPeak, 0.4f }, { EG2Decay, 0.45f }, { EG2Sustain, 0.0f }, { EG2Release, 0.4f } },
                                    { { jEg1Rev, jFreq } }));
    v.push_back (make ("Brass Section", { { PolyVoices, voices (8) }, { VCO1Wave, Saw1 }, { VCO2Wave, Saw2 }, { VCO2Scale, V8 }, { VCO2Tune, semis (0.07f) },
                                          { VCO1Level, 0.85f }, { VCO2Level, 0.8f }, { HPFCut, 0.22f }, { LPFCut, 0.4f }, { LPFPeak, 0.3f }, { LPFEG2, 0.52f },
                                          { EG2Attack, 0.22f }, { EG2Decay, 0.45f }, { EG2Sustain, 0.62f }, { EG2Release, 0.33f },
                                          { ExtSrc1, 1.0f / 14.0f }, { Ext1LPF, amount (0.6f) }, { Ext1Amp, amount (1.5f) } }));
    v.push_back (make ("Combo Organ", { { PolyVoices, voices (8) }, { VCO1Wave, Pulse }, { VCO1PW, 0.0f }, { VCO2Wave, Square }, { VCO2Scale, V4 },
                                        { VCO1Level, 0.8f }, { VCO2Level, 0.55f }, { LPFCut, 0.72f }, { LPFPeak, 0.1f }, { HPFCut, 0.2f },
                                        { EG2Attack, 0.02f }, { EG2Decay, 0.5f }, { EG2Sustain, 1.0f }, { EG2Release, 0.12f } }, wheelVibrato));
    v.push_back (make ("Analog Kick", { { Assign, 0 }, { VCO1Wave, Tri }, { VCO1Scale, S16 }, { VCO1Level, 1.0f }, { PitchEG1, 0.62f }, { EG1Attack, 0.25f },
                                        { LPFCut, 0.55f }, { EG2Decay, 0.42f }, { EG2Sustain, 0.0f }, { EG2Release, 0.38f } },
                                      { { jEg1Rev, jFreq } }));
    v.push_back (make ("Resonant Pluck", { { PolyVoices, voices (8) }, { VCO1Wave, Saw1 }, { VCO2Wave, Pulse10 }, { VCO2Scale, V8 }, { VCO1Level, 0.85f }, { VCO2Level, 0.6f },
                                           { HPFCut, 0.42f }, { HPFPeak, 0.72f }, { HPFEG2, 0.55f }, { LPFCut, 0.62f }, { LPFPeak, 0.35f },
                                           { EG2Decay, 0.42f }, { EG2Sustain, 0.0f }, { EG2Release, 0.4f } }));
    v.push_back (make ("Hollow Tones", { { PolyVoices, voices (6) }, { VCO1Wave, Tri }, { VCO2Wave, Pulse10 }, { VCO2Scale, V4 }, { VCO2Tune, semis (7.0f) },
                                              { VCO1Level, 0.9f }, { VCO2Level, 0.45f }, { LPFCut, 0.6f }, { LPFPeak, 0.55f }, { LPFEG2, 0.4f },
                                              { EG2Attack, 0.1f }, { EG2Decay, 0.55f }, { EG2Sustain, 0.5f }, { EG2Release, 0.5f }, { PitchMG, 0.05f }, { MGFreq, 0.3f } }));
    v.push_back (make ("Feedback Growl", { { Assign, 0 }, { VCO1Wave, Saw1 }, { VCO1Scale, S16 }, { VCO1Level, 0.8f }, { LPFCut, 0.42f }, { LPFPeak, 0.45f }, { LPFEG2, 0.5f },
                                           { ESPLevel, 0.45f }, { ESPLowCut, 0.15f }, { ESPHighCut, 0.55f }, { EG2Decay, 0.5f }, { EG2Sustain, 0.6f }, { EG2Release, 0.2f } },
                                         { { jSigOut, jEspIn }, { jEspBpf, jExtSig } }));
    return v;
}
}
