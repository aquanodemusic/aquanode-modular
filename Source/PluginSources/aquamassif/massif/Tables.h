#pragma once
// Massif - wavetables.
//
// A wavetable is a stack of 2048-sample single-cycle frames. None of the
// original's tables are part of this code: the bank is filled at run time
// from a folder of wavetable .wav files (Serum-style "clm" files: 2048
// samples per frame, any number of frames) and, for every slot without a
// file, from the procedural generator below.
//
// Each frame is kept as ten cumulative octave mipmaps: level L holds the
// harmonics 1 .. 2^(L+1)-1, so the Spectrum mode (which lets one octave band
// of harmonics in after the other) and the anti-aliasing both come down to a
// crossfade between two neighbouring levels.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cctype>
#include <functional>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace massif
{
constexpr int kFrameSize = 2048;
constexpr int kLevels = 10;               // harmonics up to 1023

struct Wavetable
{
    std::string name;
    int numFrames { 0 };
    float pitchDiv { 1.0f };              // the frame is played at note / pitchDiv (measured per table)
    // [frame][level][kFrameSize + 1] (one guard sample)
    std::vector<float> data;
    const float* level (int frame, int L) const
    {
        return data.data() + ((size_t) frame * kLevels + (size_t) L) * (kFrameSize + 1);
    }
};

// ---------------------------------------------------------------------------
// small real FFT (radix 2), enough for building mipmaps
inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * 3.14159265358979323846 / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= (double) n;
}

// builds the mipmaps from raw frames (numFrames * 2048 samples)
inline std::shared_ptr<Wavetable> makeTable (const std::string& name, const std::vector<float>& raw, float pitchDiv = 1.0f)
{
    auto t = std::make_shared<Wavetable>();
    t->name = name;
    t->numFrames = (int) (raw.size() / kFrameSize);
    t->pitchDiv = pitchDiv;
    if (t->numFrames < 1) return nullptr;
    t->data.assign ((size_t) t->numFrames * kLevels * (kFrameSize + 1), 0.0f);
    std::vector<std::complex<double>> spec (kFrameSize), tmp (kFrameSize);
    for (int f = 0; f < t->numFrames; ++f)
    {
        for (int i = 0; i < kFrameSize; ++i) spec[(size_t) i] = raw[(size_t) f * kFrameSize + (size_t) i];
        fft (spec, false);
        spec[0] = 0.0;                    // no DC
        for (int L = 0; L < kLevels; ++L)
        {
            const int top = std::min (kFrameSize / 2 - 1, (1 << (L + 1)) - 1);
            std::fill (tmp.begin(), tmp.end(), std::complex<double> (0.0, 0.0));
            for (int h = 1; h <= top; ++h) { tmp[(size_t) h] = spec[(size_t) h]; tmp[(size_t) (kFrameSize - h)] = spec[(size_t) (kFrameSize - h)]; }
            fft (tmp, true);
            float* d = t->data.data() + ((size_t) f * kLevels + (size_t) L) * (kFrameSize + 1);
            for (int i = 0; i < kFrameSize; ++i) d[i] = (float) tmp[(size_t) i].real();
            d[kFrameSize] = d[0];
        }
    }
    return t;
}

// ---------------------------------------------------------------------------
// WAV reading (PCM 16/24/32, float 32/64), with the "clm " chunk's frame size
inline bool readWavMemory (const std::vector<char>& b, std::vector<float>& out, int& frameSize)
{
    return [&]
    {
        if (b.size() < 44 || std::memcmp (b.data(), "RIFF", 4) || std::memcmp (b.data() + 8, "WAVE", 4)) return false;
        size_t p = 12; int fmt = 0, ch = 1, bits = 16; frameSize = kFrameSize;
        const char* data = nullptr; uint32_t dataLen = 0;
        auto u16 = [&] (size_t o) { uint16_t v; std::memcpy (&v, b.data() + o, 2); return v; };
        auto u32 = [&] (size_t o) { uint32_t v; std::memcpy (&v, b.data() + o, 4); return v; };
        while (p + 8 <= b.size())
        {
            const uint32_t len = u32 (p + 4);
            const char* id = b.data() + p;
            if (! std::memcmp (id, "fmt ", 4)) { fmt = u16 (p + 8); ch = u16 (p + 10); bits = u16 (p + 22);
                                               if (fmt == 0xFFFE && len >= 26) fmt = u16 (p + 32); }
            else if (! std::memcmp (id, "clm ", 4))
            {
                std::string s (b.data() + p + 8, b.data() + p + 8 + std::min<uint32_t> (len, 64));
                const auto q = s.find ("<!>");
                if (q != std::string::npos) { const int n = std::atoi (s.c_str() + q + 3); if (n >= 64 && n <= 65536) frameSize = n; }
            }
            else if (! std::memcmp (id, "data", 4)) { data = b.data() + p + 8; dataLen = (uint32_t) std::min<size_t> (len, b.size() - p - 8); }
            p += 8 + len + (len & 1);
        }
        if (data == nullptr || ch < 1) return false;
        const int bps = bits / 8; const size_t n = dataLen / (size_t) (bps * ch);
        out.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const char* s = data + i * (size_t) (bps * ch);
            float v = 0;
            if (fmt == 3 && bits == 32) { float x; std::memcpy (&x, s, 4); v = x; }
            else if (fmt == 3 && bits == 64) { double x; std::memcpy (&x, s, 8); v = (float) x; }
            else if (bits == 16) { int16_t x; std::memcpy (&x, s, 2); v = (float) x / 32768.0f; }
            else if (bits == 24) { int32_t x = (int32_t) ((uint32_t) (uint8_t) s[0] << 8 | (uint32_t) (uint8_t) s[1] << 16 | (uint32_t) (uint8_t) s[2] << 24); v = (float) x / 2147483648.0f; }
            else if (bits == 32) { int32_t x; std::memcpy (&x, s, 4); v = (float) x / 2147483648.0f; }
            else if (bits == 8) { v = ((float) (uint8_t) s[0] - 128.0f) / 128.0f; }
            out[i] = v;
        }
        return ! out.empty();
    }();
}
inline bool readWav (const std::string& path, std::vector<float>& out, int& frameSize)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::vector<char> b ((std::istreambuf_iterator<char> (f)), {});
    return readWavMemory (b, out, frameSize);
}

// resample frames of any size to 2048 (spectral)
inline std::vector<float> toFrames2048 (const std::vector<float>& in, int frameSize)
{
    if (frameSize == kFrameSize) { std::vector<float> o (in.begin(), in.begin() + (long) (in.size() / kFrameSize * kFrameSize)); return o; }
    const size_t nf = in.size() / (size_t) frameSize; std::vector<float> o (nf * kFrameSize);
    for (size_t f = 0; f < nf; ++f)
        for (int i = 0; i < kFrameSize; ++i)
        {
            const double x = (double) i * frameSize / kFrameSize; const int i0 = (int) x; const double fr = x - i0;
            const float a = in[f * (size_t) frameSize + (size_t) i0], c = in[f * (size_t) frameSize + (size_t) ((i0 + 1) % frameSize)];
            o[f * kFrameSize + (size_t) i] = (float) (a + (c - a) * fr);
        }
    return o;
}

// ---------------------------------------------------------------------------
// The bank: 87 slots in the original menu order. Per slot: the menu name, the
// words its .wav file is usually named after, and the measured playback
// octave (pitchDiv) of that slot.
struct SlotInfo { const char* name; const char* fileHint; float pitchDiv; int group; };
inline const std::array<SlotInfo, 87>& slotInfos()
{
    static const std::array<SlotInfo, 87> s { {
        { "VA-PWM", "va-pwm", 1, 0 }, { "VA-Sync", "va-sync", 1, 0 },
        { "Squ-Sw I", "square-saw i", 1, 1 }, { "Squ-Sw II", "square-saw ii", 1, 1 }, { "SmoSqu", "smooth square", 1, 1 },
        { "Sin-Squ", "sin-square", 1, 1 }, { "Sin-Tri", "sin-triangle", 1, 1 }, { "Plysaw I", "polysaw i", 2, 1 },
        { "Plysaw II", "polysaw ii", 2, 1 }, { "Math I", "roughmath i", 2, 1 }, { "Math II", "roughmath ii", 1, 1 },
        { "Math III", "roughmath iii", 2, 1 }, { "Esca I", "escalation i", 2, 1 }, { "Esca II", "escalation ii", 2, 1 },
        { "M-plex", "multiplex", 1, 1 }, { "SinForm", "sinformant", 2, 1 }, { "AddOct", "additiveoctaves", 1, 1 },
        { "Additiv I", "additivmix i", 1, 1 }, { "Additiv II", "additivmix ii", 1, 1 }, { "Additiv III", "additivmix iii", 1, 1 },
        { "Additiv IV", "additivmix iv", 1, 1 }, { "Additiv V", "additivmix v", 1, 1 }, { "Additiv VI", "additivmix vi", 1, 1 },
        { "Inharm", "inharmonic", 1, 1 }, { "Sinarm I", "sinarmonic i", 1, 1 }, { "Sinarm II", "sinarmonic ii", 1, 1 },
        { "Woody", "woody", 1, 2 }, { "Sync", "sync", 1, 2 }, { "Acid", "acid", 1, 2 }, { "Grn I", "groan i", 1, 2 },
        { "Grn II", "groan ii", 1, 2 }, { "Grn III", "groan iii", 1, 2 }, { "Grn IV", "groan iv", 1, 2 }, { "Crude", "crude", 1, 2 },
        { "Drive I", "drive i", 1, 2 }, { "Drive II", "drive ii", 1, 2 }, { "Drive III", "drive iii", 1, 2 }, { "Electr", "electric", 1, 2 },
        { "Scrim", "screamer", 2, 2 }, { "Needl", "dirty needle", 1, 2 }, { "Throat", "dirty throat", 1, 2 }, { "PWM", "dirty pwm", 1, 2 },
        { "Stront", "strontium", 2, 2 }, { "Crush", "crusher", 1, 2 }, { "Reduc", "reducer", 2, 2 }, { "Kanga", "kangaroo", 1, 2 },
        { "Frozen", "frozen", 1, 2 }, { "Vulgar", "vulgar", 2, 2 }, { "Classic", "classic", 1, 2 }, { "Disto", "disto", 1, 2 },
        { "Carbon", "carbon", 1, 2 },
        { "Speech", "gentle speech", 1, 3 }, { "M-Talk", "modern talking", 2, 3 }, { "D-Throat", "deep throat", 2, 3 },
        { "Melom", "melomantic", 1, 3 }, { "Melof", "melofant", 1, 3 }, { "Grain I", "digigrain i", 2, 3 },
        { "Grain II", "digigrain ii", 2, 3 }, { "F-Saw", "formant saw", 1, 3 }, { "F-Squ", "formant square", 1, 3 },
        { "Sonic", "sonic", 1, 3 }, { "Skelchy", "squelchy", 2, 3 }, { "D-org", "duckorgan", 2, 3 }, { "Flend I", "flenders i", 1, 3 },
        { "Flend II", "flenders ii", 1, 3 }, { "Flend III", "flenders iii", 1, 3 }, { "Digi I", "digi cook i", 1, 3 },
        { "Digi II", "digi cook ii", 1, 3 }, { "Digi III", "digi cook iii", 1, 3 }, { "Chrom", "chrome", 2, 3 },
        { "Magyr", "magyr", 2, 3 }, { "A.I.", "a.i", 1, 3 }, { "Silver", "silver", 1, 3 }, { "S-Yard", "scrap yard", 1, 3 },
        { "Wicked", "wicked", 2, 3 }, { "Bronz", "bronze", 2, 3 }, { "Arctic", "arctic", 1, 3 }, { "Herby", "herby", 1, 3 },
        { "Bender", "bender", 1, 3 }, { "G-Pulse", "guitar pulse", 1, 3 }, { "B-Pulse", "e-bass pulse", 2, 3 },
        { "Chord", "camchord", 8, 4 }, { "Color", "colors", 16, 4 }, { "Iron", "iron", 8, 4 }, { "Cica", "cicada", 16, 4 },
        { "Melan", "melancholy", 4, 4 }, { "Luna", "luna", 16, 4 },
    } };
    return s;
}

inline int slotGroup (int slot) { return slot >= 0 && slot < 87 ? slotInfos()[(size_t) slot].group : 1; }

// ---------------------------------------------------------------------------
// procedural tables: classic shapes, built additively (band-limited by the
// mipmaps anyway)
namespace gen
{
    constexpr double kPi = 3.14159265358979323846;
    // a frame from harmonic amplitudes (and phases): sum of a(h) sin (2 pi h n / N + phase(h)), by inverse FFT
    inline void additive (std::vector<float>& frame, const std::function<double (int)>& amp, const std::function<double (int)>& phase = nullptr)
    {
        std::vector<std::complex<double>> c (kFrameSize, { 0.0, 0.0 });
        for (int h = 1; h < kFrameSize / 2; ++h)
        {
            const double a = amp (h); if (a == 0.0) continue;
            // a negative amplitude is a phase flip. Not std::polar: libc++ (Android, macOS) returns NaN for a
            // negative radius, and every table with one (the saw) came out as NaN there - Massif was silent
            const double ph = phase ? phase (h) : 0.0;
            c[(size_t) h] = { a * std::cos (ph), a * std::sin (ph) };
        }
        fft (c, true);                      // y[n] = (1/N) sum c[h] e^(+i 2 pi h n / N)
        frame.assign (kFrameSize, 0.0f);
        for (int i = 0; i < kFrameSize; ++i) frame[(size_t) i] = (float) (c[(size_t) i].imag() * kFrameSize);
    }
    inline std::vector<float> saw()      { std::vector<float> f; additive (f, [] (int h) { return 2.0 / (kPi * h) * (h % 2 ? 1 : -1); }); return f; }
    inline std::vector<float> square()   { std::vector<float> f; additive (f, [] (int h) { return h % 2 ? 4.0 / (kPi * h) : 0.0; }); return f; }
    inline std::vector<float> triangle() { std::vector<float> f; additive (f, [] (int h) { return h % 2 ? 8.0 / (kPi * kPi * h * h) * ((h / 2) % 2 ? -1 : 1) : 0.0; }); return f; }
    inline std::vector<float> sine()     { std::vector<float> f (kFrameSize); for (int i = 0; i < kFrameSize; ++i) f[(size_t) i] = (float) std::sin (2 * kPi * i / kFrameSize); return f; }
    inline std::vector<float> pulse (double w)
    {
        std::vector<float> f; additive (f, [w] (int h) { return 2.0 / (kPi * h) * std::sin (kPi * h * w); }, [] (int) { return kPi / 2; });
        return f;
    }
    inline std::vector<float> sync (double ratio)       // hard-synced saw, slave at ratio
    {
        std::vector<float> f (kFrameSize);
        for (int i = 0; i < kFrameSize; ++i) { const double ph = std::fmod ((double) i / kFrameSize * ratio, 1.0); f[(size_t) i] = (float) (1.0 - 2.0 * ph); }
        return f;
    }
    inline void morphFrames (std::vector<float>& out, const std::vector<float>& a, const std::vector<float>& b, int n)
    {
        for (int k = 0; k < n; ++k)
        {
            const double t = n > 1 ? (double) k / (n - 1) : 0.0;
            for (int i = 0; i < kFrameSize; ++i) out.push_back ((float) ((1 - t) * a[(size_t) i] + t * b[(size_t) i]));
        }
    }
    // per frame: towards an RMS of 0.4, the peak never over 1.9 (peaky frames stay audible)
    inline void normalize (std::vector<float>& v)
    {
        const size_t nf = v.size() / kFrameSize;
        for (size_t f = 0; f < nf; ++f)
        {
            float pk = 1e-9f; double e = 0.0;
            for (int i = 0; i < kFrameSize; ++i) { const float x = v[f * kFrameSize + (size_t) i]; pk = std::max (pk, std::abs (x)); e += (double) x * x; }
            const float rms = (float) std::sqrt (e / kFrameSize) + 1e-9f;
            const float g = std::max (1.0f / pk, std::min (0.4f / rms, 1.9f / pk));
            for (int i = 0; i < kFrameSize; ++i) v[f * kFrameSize + (size_t) i] *= g;
        }
    }

    // -----------------------------------------------------------------------
    // The wavetable generator: recipes that write a whole table (frames of
    // 2048 samples) from three amounts and a seed. Every recipe is a sweep,
    // so Wt-Position always has somewhere to go.
    enum Recipe { rcPwm = 0, rcSync, rcShapes, rcHarmonicSweep, rcVowels, rcFm, rcAdditive, rcFold, rcCrush,
                  rcOrgan, rcSpectral, rcChord, rcNumRecipes };
    inline const char* recipeName (int r)
    {
        static const char* n[rcNumRecipes] = { "Pulse Width", "Sync Sweep", "Shape Morph", "Harmonic Sweep", "Vowels", "FM",
                                               "Additive Dice", "Wavefolder", "Bit Crush", "Organ Stops", "Spectral Cloud", "Chord Stack" };
        return r >= 0 && r < rcNumRecipes ? n[r] : "?";
    }
    // what the three amounts (0..1) do in a recipe
    inline const char* recipeParam (int r, int i)
    {
        static const char* n[rcNumRecipes][3] = {
            { "Narrowest", "Saw Mix", "Soften" }, { "Ratio", "Window", "Square" }, { "Brightness", "Fold In", "Odd Only" },
            { "Width", "Body", "Peaks" }, { "Shift", "Sharpness", "Buzz" }, { "Ratio", "Index", "Feedback" },
            { "Density", "Tilt", "Movement" }, { "Drive", "Asymmetry", "Stages" }, { "Bits", "Rate", "Source" },
            { "Stops", "Tilt", "Chiff" }, { "Width", "Center", "Detail" }, { "Chord", "Spread", "Brightness" } };
        return r >= 0 && r < rcNumRecipes && i >= 0 && i < 3 ? n[r][i] : "";
    }

    // the octave a recipe's frames are written in (the frame is played at note / this): the chord tables put
    // their root on the 16th harmonic so the chord tones can sit on whole harmonics
    inline float recipeDiv (int r) { return r == rcChord ? 16.0f : 1.0f; }
    // chord shapes (semitones above the root) for the Chord Stack recipe
    inline const std::vector<int>& chordShape (float a)
    {
        static const std::vector<std::vector<int>> c = { { 0, 3, 7 }, { 0, 3, 7, 10 }, { 0, 3, 7, 10, 14 }, { 0, 2, 7 }, { 0, 5, 7 },
                                                          { 0, 4, 7, 11 }, { 0, 7, 12 } };
        return c[(size_t) std::clamp ((int) (a * 6.999f), 0, 6)];
    }
    inline const char* chordName (float a)
    {
        static const char* n[7] = { "minor", "minor 7", "minor 9", "sus 2", "sus 4", "major 7", "fifths" };
        return n[std::clamp ((int) (a * 6.999f), 0, 6)];
    }

    inline uint32_t rnd (uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline double rnd01 (uint32_t& s) { return (double) (rnd (s) & 0xffffff) / 16777216.0; }

    inline std::vector<float> recipe (int type, float a, float b, float c, uint32_t seed, int frames)
    {
        frames = std::max (2, std::min (64, frames));
        std::vector<float> v; v.reserve ((size_t) frames * kFrameSize);
        uint32_t rs = seed * 2654435761u + 12345u; if (rs == 0) rs = 1;
        const auto S = saw(); const auto N = sine();
        // seeded sets used by the recipes that morph between random spectra
        std::vector<std::vector<double>> sets;
        const int nSets = 2 + (int) std::round (c * 4.0f);
        for (int k = 0; k < nSets + 1; ++k)
        {
            std::vector<double> h (129, 0.0);
            for (int i = 1; i <= 128; ++i) h[(size_t) i] = rnd01 (rs) < (0.08 + 0.9 * a) ? rnd01 (rs) : 0.0;
            h[1] = std::max (h[1], 0.6);
            sets.push_back (h);
        }
        for (int k = 0; k < frames; ++k)
        {
            const double t = (double) k / (double) (frames - 1);
            std::vector<float> f (kFrameSize, 0.0f);
            switch (type)
            {
                case rcPwm:
                {
                    const double w = 0.5 - (0.5 - (0.02 + 0.2 * (1.0 - a))) * t;
                    const double soft = 1.0 + 60.0 * c;
                    additive (f, [w, soft, c] (int h) { return 2.0 / (kPi * h) * std::sin (kPi * h * w) * (c > 0 ? std::exp (-(double) h / (soft * 4.0) * c * 3.0) : 1.0); },
                              [] (int) { return kPi / 2; });
                    for (int i = 0; i < kFrameSize; ++i) f[(size_t) i] = (float) ((1.0 - b) * f[(size_t) i] + b * 0.6 * S[(size_t) i]);
                    break;
                }
                case rcSync:
                {
                    const double ratio = 1.0 + (1.0 + 15.0 * a) * t;
                    for (int i = 0; i < kFrameSize; ++i)
                    {
                        const double u = (double) i / kFrameSize, ph = std::fmod (u * ratio, 1.0);
                        const double w = std::pow (0.5 - 0.5 * std::cos (2 * kPi * u), b * 1.5);
                        const double sw = 1.0 - 2.0 * ph, sq = ph < 0.5 ? 1.0 : -1.0;
                        f[(size_t) i] = (float) (((1.0 - c) * sw + c * sq) * w);
                    }
                    break;
                }
                case rcShapes:
                {
                    // sine -> triangle -> saw -> square, then (Fold In) a folded copy
                    const double x = t * 3.0; const int seg = std::min (2, (int) x); const double fr = x - seg;
                    const double tilt = 0.5 + 1.5 * (1.0 - a);
                    additive (f, [seg, fr, tilt, c] (int h)
                    {
                        auto sh = [h, tilt] (int which) -> double
                        {
                            switch (which)
                            {
                                case 0: return h == 1 ? 1.0 : 0.0;
                                case 1: return h % 2 ? 8.0 / (kPi * kPi * h * h) * ((h / 2) % 2 ? -1 : 1) : 0.0;
                                case 2: return 2.0 / (kPi * std::pow ((double) h, tilt)) * (h % 2 ? 1 : -1);
                                default: return h % 2 ? 4.0 / (kPi * std::pow ((double) h, tilt)) : 0.0;
                            }
                        };
                        double r = (1.0 - fr) * sh (seg) + fr * sh (seg + 1);
                        if (c > 0.5 && h % 2 == 0) r *= 2.0 * (1.0 - c);
                        return r;
                    });
                    if (b > 0.0f) for (int i = 0; i < kFrameSize; ++i) f[(size_t) i] = (float) std::sin (f[(size_t) i] * (1.0 + 4.0 * b * t) * kPi * 0.5);
                    break;
                }
                case rcHarmonicSweep:
                {
                    const double fc = 1.0 + 60.0 * t * t, wd = 0.6 + 8.0 * a; const int peaks = 1 + (int) std::round (c * 3.0);
                    additive (f, [fc, wd, b, peaks] (int h)
                    {
                        double r = b * 1.0 / h;
                        for (int q = 1; q <= peaks; ++q) { const double d = (h - fc * q) / (wd * q); r += std::exp (-d * d) / q; }
                        return r;
                    });
                    break;
                }
                case rcVowels:
                {
                    // two formants moving through a ring of vowel-like positions (in harmonics of a low note)
                    static const double F1[6] = { 7.3, 2.7, 3.9, 5.7, 4.4, 7.3 }, F2[6] = { 10.9, 22.9, 18.4, 8.4, 8.7, 10.9 };
                    const double x = t * 5.0; const int i0 = std::min (4, (int) x); const double fr = x - i0;
                    const double sh = 0.6 + 1.2 * a;
                    const double f1 = (F1[i0] + fr * (F1[i0 + 1] - F1[i0])) * sh, f2 = (F2[i0] + fr * (F2[i0 + 1] - F2[i0])) * sh;
                    const double q = 0.8 + 3.0 * (1.0 - b);
                    additive (f, [f1, f2, q, c] (int h)
                    {
                        const double d1 = (h - f1) / q, d2 = (h - f2) / (q * 1.6);
                        return (std::exp (-d1 * d1) + 0.6 * std::exp (-d2 * d2) + 0.05) * (c + (1.0 - c) / std::sqrt ((double) h));
                    });
                    break;
                }
                case rcFm:
                {
                    const double ratio = std::round (1.0 + 7.0 * a), idx = (0.2 + 9.0 * b) * t; double fbk = 0.0;
                    for (int rep = 0; rep < 2; ++rep)      // second pass settles the feedback
                        for (int i = 0; i < kFrameSize; ++i)
                        {
                            const double u = 2 * kPi * i / kFrameSize;
                            const double m = std::sin (u * ratio + c * 1.5 * fbk); fbk = m;
                            f[(size_t) i] = (float) std::sin (u + idx * m);
                        }
                    break;
                }
                case rcAdditive:
                {
                    const double x = t * (double) (nSets - 1); const int i0 = std::min (nSets - 2, (int) x); const double fr = x - i0;
                    const double tilt = 0.3 + 1.7 * b;
                    const auto& A = sets[(size_t) i0]; const auto& B = sets[(size_t) i0 + 1];
                    additive (f, [&A, &B, fr, tilt] (int h) { return h <= 128 ? ((1.0 - fr) * A[(size_t) h] + fr * B[(size_t) h]) / std::pow ((double) h, tilt) : 0.0; });
                    break;
                }
                case rcFold:
                {
                    const double drive = 1.0 + (2.0 + 14.0 * a) * t, asym = 0.6 * b; const int stages = 1 + (int) std::round (c * 2.0);
                    for (int i = 0; i < kFrameSize; ++i)
                    {
                        double x = N[(size_t) i] * drive + asym * drive * 0.5;
                        for (int q = 0; q < stages; ++q) x = std::sin (x * kPi * 0.5);
                        f[(size_t) i] = (float) x;
                    }
                    double dc = 0; for (float x : f) dc += x; dc /= kFrameSize; for (auto& x : f) x -= (float) dc;
                    break;
                }
                case rcCrush:
                {
                    const double bits = 12.0 - (10.0 - 8.0 * (1.0 - a)) * t; const double lv = std::pow (2.0, bits - 1.0);
                    const int hold = 1 << (int) std::round (b * 7.0 * t);
                    for (int i = 0; i < kFrameSize; ++i)
                    {
                        const int j = (i / hold) * hold;
                        const double src = (1.0 - c) * S[(size_t) j] + c * N[(size_t) j];
                        f[(size_t) i] = (float) (std::round (src * lv) / lv);
                    }
                    break;
                }
                case rcOrgan:
                {
                    static const int stops[9] = { 1, 3, 2, 4, 6, 8, 10, 12, 16 };
                    const double n = 1.0 + (1.0 + 7.0 * a) * t;
                    additive (f, [n, b, c] (int h)
                    {
                        double r = 0.0;
                        for (int q = 0; q < 9; ++q) if (stops[q] == h) { const double on = std::min (1.0, std::max (0.0, n - q)); r = on / (1.0 + b * q); }
                        return r + c * 0.04 * std::exp (-(double) (h - 40) * (h - 40) / 300.0);
                    });
                    break;
                }
                case rcChord:
                {
                    // a chord on whole harmonics of a frame four octaves below the root (root = harmonic 16; the
                    // table plays at note / 16). Spread adds the octave below the root and doubles the upper tones an
                    // octave up; the table runs from dark to the Brightness set (partials per tone, 1/k^p)
                    const auto& sh = chordShape (a);
                    struct Tone { int h; double w; };
                    std::vector<Tone> tones;
                    for (size_t q = 0; q < sh.size(); ++q)
                    {
                        const int h = (int) std::lround (16.0 * std::exp2 (sh[q] / 12.0));
                        tones.push_back ({ h, q == 0 ? 1.0 : 0.8 });
                        if (q > 0 && b > 0.0f) tones.push_back ({ 2 * h, 0.6 * b });
                    }
                    if (b > 0.0f) tones.push_back ({ 8, 0.7 * b });
                    const double br = c * (0.25 + 0.75 * t), p = 2.6 - 1.7 * br;
                    std::vector<double> amp (kFrameSize / 2, 0.0);
                    for (auto& tn : tones)
                        for (int kk = 1; kk * tn.h < kFrameSize / 2; ++kk)
                        {
                            const double w = tn.w / std::pow ((double) kk, p) * (kk > 1 ? std::exp (-(double) kk / (2.0 + 30.0 * br)) * 1.5 : 1.0);
                            amp[(size_t) (kk * tn.h)] += w;
                        }
                    uint32_t ps = seed * 2891336453u + 1u; if (ps == 0) ps = 1;
                    std::vector<double> ph (kFrameSize / 2); for (auto& x : ph) x = 2 * kPi * rnd01 (ps);
                    additive (f, [&amp] (int h) { return h < (int) amp.size() ? amp[(size_t) h] : 0.0; }, [&ph] (int h) { return ph[(size_t) h % ph.size()]; });
                    break;
                }
                default:    // rcSpectral: a band of partials with seeded phases, the band moving up
                {
                    const double fc = 2.0 + 90.0 * b * t + 6.0 * t, wd = 1.5 + 30.0 * a; uint32_t ps = seed * 747796405u + 7u + (uint32_t) (c > 0.5f ? k : 0); if (ps == 0) ps = 1;
                    std::vector<double> ph (1024); for (auto& x : ph) x = 2 * kPi * rnd01 (ps);
                    additive (f, [fc, wd] (int h) { const double d = (h - fc) / wd; return h < 400 ? std::exp (-d * d) + (h == 1 ? 0.5 : 0.0) : 0.0; },
                              [&ph] (int h) { return ph[(size_t) (h % 1024)]; });
                    break;
                }
            }
            v.insert (v.end(), f.begin(), f.end());
        }
        normalize (v);
        return v;
    }

    // stand-ins for every slot without a file: the plain shapes are what their
    // names say; the rest take a recipe of the same family, seeded by the slot
    inline std::vector<float> forSlot (int slot)
    {
        std::vector<float> v;
        auto S = saw(), Q = square(), T = triangle(), N = sine();
        const float u = (float) ((slot * 37) % 17) / 16.0f, w = (float) ((slot * 53) % 13) / 12.0f;
        switch (slot)
        {
            case 0: case 41: return recipe (rcPwm, 0.9f, 0.0f, 0.0f, 1, 32);
            case 1: case 27: return recipe (rcSync, 0.45f, 0.0f, 0.0f, 1, 32);
            case 2: case 3: morphFrames (v, Q, S, 2); break;
            case 4: return recipe (rcPwm, 0.2f, 0.0f, 0.6f, 1, 8);
            case 5: morphFrames (v, N, Q, 5); break;
            case 6: morphFrames (v, N, T, 2); break;
            case 7: case 8: return recipe (rcShapes, 0.8f, 0.0f, 0.0f, 1, 9);
            case 16: case 62: return recipe (rcOrgan, 0.8f, 0.3f, 0.0f, (uint32_t) slot, 9);
            case 46: morphFrames (v, T, S, 2); break;
            case 48: morphFrames (v, S, Q, 2); break;
            case 79: case 80: return recipe (rcPwm, 0.5f, 0.2f, 0.3f, 1, 8);
            default:
            {
                const int g = slotGroup (slot);
                if (g == 1) return recipe (slot % 2 ? rcAdditive : rcHarmonicSweep, 0.2f + 0.5f * u, 0.4f + 0.5f * w, 0.3f, (uint32_t) slot, 16);
                if (g == 2) return recipe (slot % 3 == 0 ? rcSync : (slot % 3 == 1 ? rcFold : rcCrush), 0.3f + 0.6f * u, 0.5f * w, 0.4f * u, (uint32_t) slot, 16);
                if (g == 3) return recipe (slot % 3 == 0 ? rcVowels : (slot % 3 == 1 ? rcFm : rcHarmonicSweep), 0.2f + 0.6f * u, 0.3f + 0.6f * w, 0.5f * u, (uint32_t) slot, 24);
                return recipe (rcSpectral, 0.2f + 0.6f * u, 0.3f + 0.5f * w, 1.0f, (uint32_t) slot, 32);
            }
        }
        normalize (v);
        return v;
    }
}

// ---------------------------------------------------------------------------
struct Bank
{
    std::array<std::shared_ptr<const Wavetable>, 87> slot {};
    std::array<bool, 87> fromFile {};
    std::string folder;

    static std::string normalizeName (std::string s)
    {
        std::string o;
        for (char c : s) { c = (char) std::tolower ((unsigned char) c); if (std::isalnum ((unsigned char) c) || c == ' ' || c == '-' || c == '.') o += c; }
        // strip a leading "NN-" index and a trailing extension
        const auto dot = o.rfind (".wav"); if (dot != std::string::npos) o = o.substr (0, dot);
        size_t i = 0; while (i < o.size() && std::isdigit ((unsigned char) o[i])) ++i;
        if (i > 0 && i < o.size() && (o[i] == '-' || o[i] == ' ')) o = o.substr (i + 1);
        while (! o.empty() && o.back() == ' ') o.pop_back();
        return o;
    }

    // which slot a file belongs to (-1: none) - exact hint match first, then the menu name
    static int slotForFile (const std::string& fileName)
    {
        const auto n = normalizeName (fileName);
        const auto& s = slotInfos();
        for (int i = 0; i < 87; ++i) if (n == s[(size_t) i].fileHint) return i;
        for (int i = 0; i < 87; ++i) if (n == normalizeName (s[(size_t) i].name)) return i;
        return -1;
    }

    // a generated table for the slot (no file)
    static std::shared_ptr<const Wavetable> generated (int i)
    {
        return makeTable (slotInfos()[(size_t) i].name, gen::forSlot (i), 1.0f);
    }

    void fillGenerated()
    {
        for (int i = 0; i < 87; ++i) if (! slot[(size_t) i]) { slot[(size_t) i] = generated (i); fromFile[(size_t) i] = false; }
    }

    bool loadFile (int i, const std::string& path)
    {
        std::vector<float> raw; int fs = kFrameSize;
        if (! readWav (path, raw, fs)) return false;
        auto frames = toFrames2048 (raw, fs);
        if (frames.empty()) return false;
        auto t = makeTable (slotInfos()[(size_t) i].name, frames, slotInfos()[(size_t) i].pitchDiv);
        if (! t) return false;
        slot[(size_t) i] = t; fromFile[(size_t) i] = true;
        return true;
    }
};
} // namespace massif
