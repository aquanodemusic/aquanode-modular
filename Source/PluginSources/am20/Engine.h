#pragma once
// AM-20: a polyphonic model of the MS-20 voice (both VCOs, the ring modulator,
// the Korg35-style high- and low-pass filters, EG1 (delay / attack / release),
// EG2 (hold / ADSR), the MG, the modulation VCA, sample & hold, noise, the
// external signal processor and the whole 35-jack patch panel), fitted to
// measurements of the original. JUCE-free.
//
// Every voice runs the full patch panel, so patches with cables play
// polyphonically too. The voices run at twice the host rate (below 88.2 kHz)
// and are decimated once after mixing.
#include "../../Engine/Tuning.h"   // (Aquanode: the patch tuning, 6.9.10)
#include "Params.h"
#include "Curves.h"
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace am20
{
// ---------------------------------------------------------------- patch panel
enum Jack
{
    jTotal = 0, jMgTri = 1, jShIn = 2, jEspIn = 3, jShClock = 4, jFreq = 5, jMgRect = 6, jShOut = 7,
    jEspAmp = 8, jExtSig = 9, jEg1Out = 10, jEg1Rev = 11, jMvcaIn = 12, jHpCut = 13, jMvcaCtl = 14,
    jEspBpf = 15, jMvcaOut = 16, jLpCut = 17, jEg1Trig = 18, jPink = 19, jEspFv = 20, jInitGain = 21,
    jEg2Rev = 22, jWhite = 23, jVco2Cv = 24, jVco12Cv = 25, jTrigIn = 26, jWheel = 27, jEspEnv = 28,
    jSigOut = 29, jPhones = 30, jKbdCv = 31, jKbdTrig = 32, jMsw = 33, jEspTrig = 34,
    NumJacks = 35
};

inline bool jackIsInput (int j)
{
    switch (j)
    {
        case jTotal: case jShIn: case jEspIn: case jShClock: case jFreq: case jExtSig: case jMvcaIn:
        case jHpCut: case jMvcaCtl: case jLpCut: case jEg1Trig: case jInitGain: case jVco2Cv:
        case jVco12Cv: case jTrigIn: return true;
        default: return false;
    }
}

inline const char* jackName (int j)
{
    static const char* n[NumJacks] = {
        "TOTAL", "MG TRI OUT", "S&H IN", "ESP SIGNAL IN", "S&H CLOCK", "FREQ", "MG RECT OUT", "S&H OUT",
        "ESP AMP OUT", "EXT SIGNAL IN", "EG1 OUT", "EG1 REV OUT", "MVCA IN", "HPF CUTOFF", "MVCA CONTROL",
        "ESP BPF OUT", "MVCA OUT", "LPF CUTOFF", "EG1 TRIG IN", "PINK", "ESP F-V OUT", "INITIAL GAIN",
        "EG2 REV OUT", "WHITE", "VCO2 CV IN", "VCO1+2 CV IN", "TRIG IN", "WHEEL", "ESP ENV OUT",
        "SIGNAL OUT", "PHONES", "KBD CV OUT", "KBD TRIG OUT", "MOMENTARY SW", "ESP TRIG OUT" };
    return (j >= 0 && j < NumJacks) ? n[j] : "";
}

struct Cable { int out = -1, in = -1; };

// a stored pair (either order, as the original writes them) as output -> input;
// false when it does not join an output to an input
inline bool orientCable (int a, int b, Cable& c)
{
    if (a < 0 || b < 0 || a >= NumJacks || b >= NumJacks || a == b) return false;
    const bool ai = jackIsInput (a), bi = jackIsInput (b);
    if (ai == bi) return false;
    c.out = ai ? b : a; c.in = ai ? a : b;
    return true;
}

struct MidiEv { int frame = 0; uint8_t b0 = 0, b1 = 0, b2 = 0; };

// ---------------------------------------------------------------- DSP helpers
namespace dsp
{
    constexpr float twoPi = 6.283185307179586f;
    constexpr float pi = 3.141592653589793f;

    inline float fastTanh (float x)
    {
        if (x > 4.5f) return 1.0f;
        if (x < -4.5f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
    inline float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    struct Rng
    {
        uint32_t s = 0x9e3779b9u;
        void seed (uint32_t v) { s = v ? v : 0x1234567u; }
        inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        inline float uni() { return (float) (next() >> 8) * (1.0f / 8388608.0f) - 1.0f; }   // -1..1
        inline float gauss() { return (uni() + uni() + uni()) * 0.577f; }
    };

    // one-pole TPT section (Zavalishin); fb() is the state scaled by beta for the Korg35 loops
    struct OnePole
    {
        float a = 0.0f, beta = 0.0f, z = 0.0f;
        inline float lp (float x) { const float v = (x - z) * a; const float y = v + z; z = y + v; return y; }
        inline float hp (float x) { return x - lp (x); }
        inline float fb() const { return beta * z; }
        void reset() { z = 0.0f; }
    };

    // the MS-20's (Korg35) 12 dB filters with the saturating feedback path
    struct Korg35
    {
        bool high = false;
        OnePole s1, s2, s3;     // LPF: lp1, lp2, hp(feedback)   HPF: hp1, hp2, lp(feedback)
        float k = 0.01f, a0 = 1.0f, drive = 1.0f;
        static inline float sat (float x)       // a firm knee: x / (1 + x^4)^(1/4)
        {
            const float x2 = x * x;
            return x / std::sqrt (std::sqrt (1.0f + x2 * x2));
        }
        void reset() { s1.reset(); s2.reset(); s3.reset(); }
        void set (float fc, float fs, float kk)
        {
            const float g = std::tan (pi * std::min (fc, 0.45f * fs) / fs);
            const float G = g / (1.0f + g);
            k = std::max (0.01f, kk);
            s1.a = s2.a = s3.a = G;
            if (! high) { s2.beta = (k - k * G) / (1.0f + g); s3.beta = -1.0f / (1.0f + g); }
            else        { s2.beta = -G / (1.0f + g);         s3.beta = 1.0f / (1.0f + g); }
            a0 = 1.0f / (1.0f - k * G + k * G * G);
        }
        inline float process (float x)
        {
            if (! high)
            {
                const float y1 = s1.lp (x);
                float u = a0 * (y1 + s3.fb() + s2.fb());
                u = sat (u * drive) / drive;
                const float y = k * s2.lp (u);
                s3.hp (y);
                return y / k;
            }
            const float y1 = s1.hp (x);
            const float u = a0 * (y1 + s2.fb() + s3.fb());
            float y = k * u;
            y = sat (y * drive) / drive;
            s3.lp (s2.hp (y));
            return y / k;
        }
    };

    // RBJ biquad (the ESP's band-pass halves)
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        void setLP (float fc, float fs, float q) { calc (fc, fs, q, false); }
        void setHP (float fc, float fs, float q) { calc (fc, fs, q, true); }
        void calc (float fc, float fs, float q, bool hp)
        {
            const float w = twoPi * std::min (fc, 0.45f * fs) / fs, c = std::cos (w), al = std::sin (w) / (2.0f * q);
            const float n = 1.0f / (1.0f + al);
            if (hp) { b0 = (1 + c) * 0.5f * n; b1 = -(1 + c) * n; b2 = b0; }
            else    { b0 = (1 - c) * 0.5f * n; b1 = (1 - c) * n;  b2 = b0; }
            a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
        }
        inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };

    // half-band decimator by 2 (Kaiser-windowed sinc)
    struct Decimator
    {
        static constexpr int N = 47;
        float h[N] {}; float buf[2 * N] {}; int pos = 0;
        Decimator()
        {
            const int M = (N - 1) / 2; const double beta = 8.0;
            auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
            double sum = 0;
            for (int n = 0; n < N; ++n)
            {
                const double m = n - M, sinc = m == 0 ? 0.5 : std::sin (3.141592653589793 * m * 0.5) / (3.141592653589793 * m);
                const double r = (double) (2 * n - (N - 1)) / (N - 1);
                h[n] = (float) (sinc * i0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0 (beta));
                sum += h[n];
            }
            for (auto& v : h) v = (float) (v / sum);
        }
        void reset() { std::memset (buf, 0, sizeof buf); pos = 0; }
        inline void push (float x) { buf[pos] = x; buf[pos + N] = x; if (++pos >= N) pos = 0; }
        // a half-band filter: every other tap is zero, apart from the middle one
        inline float out() const
        {
            const float* b = buf + pos; constexpr int M = (N - 1) / 2;
            float s = h[M] * b[N - 1 - M];
            for (int n = (M & 1) ? 0 : 1; n < N; n += 2) s += h[n] * b[N - 1 - n];
            return s;
        }
    };
}

// ---------------------------------------------------------------- the voice
struct Globals;   // per-sample values shared by every voice

struct Voice
{
    // identity
    int note = -1, group = -1; float velocity = 0; bool gate = false, active = false;
    int channel = 0;                          // MPE (Aquanode): the member channel (2..16) it plays, 0 otherwise
    uint64_t age = 0; int unisonIndex = 0;
    float detuneSemis = 0, pan = 0;           // unison
    float analogSemis = 0, analogDrift = 0, analogCut = 0;
    // pitch
    float kbdSemis = 60, kbdTarget = 60;      // keyboard CV with portamento
    float ph1 = 0, ph2 = 0;                   // VCO phases
    float noiseLp = 0;
    float vcoS1 = 0, vcoS2 = 0, vcoCoef = 0.5f;
    dsp::Rng rng;
    float pinkB0 = 0, pinkB1 = 0, pinkB2 = 0;
    dsp::Korg35 hpf, lpf;
    int coefCounter = 0; float f1 = 261.6f, f2 = 261.6f;
    // envelopes
    int eg1Stage = 0; float eg1 = 0, eg1Delay = 0;     // 0 idle, 1 delay, 2 attack/hold, 3 release
    int eg2Stage = 0; float eg2 = 0, holdLeft = 0;     // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
    bool eg1GateIn = false, eg2GateIn = false;
    double timeOn = 0;
    // mg (key-synced voices keep their own phase)
    float mgPhase = 0;
    // s&h
    float shValue = 0; bool shClockHigh = false;
    // esp
    dsp::Biquad espHp1, espHp2, espLp1, espLp2; float espEnv = 0; bool espTrig = false;
    float espLastX = 0; float espPeriodCount = 0; float espFreqCv = 0;
    // patch panel
    float jack[NumJacks] {};
    float out = 0;

    void reset()
    {
        hpf.reset(); lpf.reset(); eg1 = eg2 = 0; eg1Stage = eg2Stage = 0; gate = active = false;
        std::memset (jack, 0, sizeof jack); noiseLp = 0; pinkB0 = pinkB1 = pinkB2 = 0; shValue = 0;
        espHp1.reset(); espHp2.reset(); espLp1.reset(); espLp2.reset(); espEnv = 0; out = 0; vcoS1 = vcoS2 = 0;
    }
};

struct Globals
{
    float mgTri = 0, mgRect = 0, mgPhase = 0;
    float wheel = 0, msw = 0;
    float bend = 0, pressure = 0, breath = 0, cc16 = 0, cc17 = 0;
};

// ---------------------------------------------------------------- the engine
class Engine
{
public:
    static constexpr int MaxVoices = 64;
    float p[NumParams] {};

    Engine() { for (int i = 0; i < NumParams; ++i) p[i] = defaultValue (i); for (int v = 0; v < MaxVoices; ++v) voices[v].rng.seed (0x2545F491u * (uint32_t) (v + 1)); }

    // eco: no oversampling (lighter on phones; the top octave aliases a little more)
    void setOversampling (bool on) { if (on != oversample) { oversample = on; setSampleRate (fsHost); } }
    bool oversample = true;

    void setSampleRate (double sr)
    {
        fsHost = (float) sr;
        os = (sr < 88200.0 && oversample) ? 2 : 1;
        fs = fsHost * (float) os;
        decL.reset(); decR.reset();
        for (auto& v : voices) { v.reset(); v.hpf.high = true; v.lpf.high = false; }
        dcL = dcR = 0;
    }
    void setTempo (double bpm) { if (bpm > 1.0) tempo = (float) bpm; }

    // patch cables, as stored (any order)
    void setCables (const std::vector<std::pair<int, int>>& pairs)
    {
        std::array<std::vector<int>, NumJacks> src;
        for (auto& pr : pairs) { Cable c; if (orientCable (pr.first, pr.second, c)) src[(size_t) c.in].push_back (c.out); }
        pendingSources = src; cablesChanged = true;
    }

    void allNotesOff()
    {
        for (auto& v : voices) { v.gate = false; if (v.eg2Stage) v.eg2Stage = 4; if (v.eg1Stage) v.eg1Stage = 3; }
        heldNotes.clear();
    }
    void allSoundOff() { for (auto& v : voices) v.reset(); heldNotes.clear(); }
    void setMomentary (bool on) { momentary = on; }
    void setWheel (float v01) { modWheel = std::clamp (v01, 0.0f, 1.0f); }   // the on-screen wheel (as CC 1)

    void processBlock (float* L, float* R, int n, const MidiEv* ev, int nev)
    {
        if (cablesChanged) { sources = pendingSources; cablesChanged = false; anyCables = false; for (auto& v : sources) anyCables |= ! v.empty(); }
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0) updateBlockValues();
            while (e < nev && ev[e].frame <= i) handleMidi (ev[e++]);
            float l = 0, r = 0;
            for (int k = 0; k < os; ++k)
            {
                float sl = 0, sr = 0;
                renderSample (sl, sr);
                if (os == 2) { decL.push (sl); decR.push (sr); }
                else { l = sl; r = sr; }
            }
            if (os == 2) { l = decL.out(); r = decR.out(); }
            // the original's output is DC-free (its VCA follows the 12 Hz minimum of the HPF)
            dcL += 0.00005f * (l - dcL); dcR += 0.00005f * (r - dcR);
            L[i] = l - dcL; R[i] = r - dcR;
        }
        while (e < nev) handleMidi (ev[e++]);
    }

    // a single voice for the editor's scope
    bool anyVoiceActive() const { for (auto& v : voices) if (v.active) return true; return false; }

private:
    float fsHost = 48000, fs = 96000; int os = 2; float tempo = 120;
    Voice voices[MaxVoices];
    Globals g;
    dsp::Decimator decL, decR;
    float dcL = 0, dcR = 0;
    std::array<std::vector<int>, NumJacks> sources, pendingSources;
    bool cablesChanged = false, anyCables = false;
    std::vector<int> heldNotes;           // mono note stack
    uint64_t ageCounter = 0;
    int groupCounter = 0;
    float lastKbd = 60.0f;                // portamento source for new notes
    bool momentary = false;
    float modWheel = 0;
    // values refreshed every 16 frames
    float smVol = 1, smPan = 0.5f;
    int nUnison = 1, nPoly = 8; bool polyMode = true, multiTrig = false;
    float mgInc = 0, unisonGain = 1.0f;

    // ---- MIDI ----
public:
    // MPE (Aquanode), switched on by the host at the first note on a member channel:
    // channels 2..16 then carry one note each, with its own bend (+-48 semitones, on
    // top of the global bend) and its own pressure (the Pressure ext source, in place
    // of the global one). Channel 1 stays the global channel.
    bool mpe = false;
    int noteChannel = 0;
    float chanBend[17] {};
    float chanPress[17] { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    float pressureOf (const Voice& v) const { return v.channel != 0 && chanPress[v.channel] >= 0.0f ? chanPress[v.channel] : g.pressure; }

private:
    void handleMidi (const MidiEv& m)
    {
        const int st = m.b0 & 0xf0;
        const int ch = mpe ? (m.b0 & 0x0f) + 1 : 1;
        const bool member = ch >= 2 && ch <= 16;
        if (member && st == 0xE0) { chanBend[ch] = (float) (((int) m.b2 << 7 | m.b1) - 8192) / 8192.0f * 48.0f; return; }
        if (member && st == 0xD0) { chanPress[ch] = m.b1 / 127.0f; return; }
        noteChannel = member ? ch : 0;
        handleMidiOn (m, st);
        noteChannel = 0;
    }

    void handleMidiOn (const MidiEv& m, int st)
    {
        if (st == 0x90 && m.b2 > 0) noteOn (m.b1, m.b2);
        else if (st == 0x80 || (st == 0x90 && m.b2 == 0)) noteOff (m.b1);
        else if (st == 0xE0) g.bend = (float) (((int) m.b2 << 7 | m.b1) - 8192) / 8192.0f;
        else if (st == 0xD0) g.pressure = m.b1 / 127.0f;
        else if (st == 0xB0)
        {
            const float v = m.b2 / 127.0f;
            switch (m.b1)
            {
                case 1: modWheel = v; break;
                case 2: g.breath = v; break;
                case 16: g.cc16 = v * 2 - 1; break;
                case 17: g.cc17 = v * 2 - 1; break;
                case 64: momentary = m.b2 >= 64; break;
                case 120: allSoundOff(); break;
                case 123: allNotesOff(); break;
                default: break;
            }
        }
    }

    void startVoice (Voice& v, int note, float vel, int group, int uIndex, bool retrigger, bool fresh)
    {
        v.note = note; v.group = group; v.velocity = vel; v.gate = true; v.active = true;
        v.channel = noteChannel;
        v.age = ++ageCounter; v.unisonIndex = uIndex;
        const float off = nUnison > 1 ? ((float) uIndex / (float) (nUnison - 1) - 0.5f) * 2.0f : 0.0f;
        v.detuneSemis = off * p[UnisonDetune] * 0.5f;       // 10 = +-50 cents
        v.pan = off * p[UnisonSpread];
        v.kbdTarget = (float) note;
        if (fresh)
        {
            v.kbdSemis = p[Porta] > 0.0f ? lastKbd : (float) note;
            v.analogSemis = v.rng.gauss() * p[Analog] * 0.08f;   // 10 = about 8 cents spread
            v.analogCut = v.rng.gauss() * p[Analog] * 0.05f;
            v.ph1 = v.ph2 = 0.5f * (v.rng.uni() + 1.0f);   // both VCOs start together, as in the original
        }
        if (retrigger)
        {
            v.timeOn = 0; v.holdLeft = curves::eg2Hold (p[EG2Hold]);
            v.eg1Stage = 1; v.eg1Delay = curves::eg1Delay (p[EG1Delay]);
            v.eg2Stage = 1;
            if (p[MGKeySync] >= 0.5f) v.mgPhase = 0;
        }
    }

    void noteOn (int note, int vel)
    {
        const float fv = vel / 127.0f;
        const int key = std::clamp (note + transposeSemis (p[Transpose]), 0, 127);
        if (! polyMode)
        {
            const bool legato = ! heldNotes.empty();
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), key), heldNotes.end());
            heldNotes.push_back (key);
            bool anyActive = false;
            for (int u = 0; u < nUnison; ++u) anyActive |= voices[u].active;
            for (int u = 0; u < nUnison; ++u)
            {
                Voice& v = voices[u];
                const bool fresh = ! v.active;
                startVoice (v, key, fv, 0, u, ! legato || multiTrig || ! anyActive, fresh);
            }
            for (int u = nUnison; u < MaxVoices; ++u) if (voices[u].active && voices[u].group == 0) { voices[u].gate = false; voices[u].eg2Stage = voices[u].eg2Stage ? 4 : 0; }
            lastKbd = (float) key;
            return;
        }
        // poly: one group of nUnison voices per note
        const int maxGroups = std::max (1, std::min (nPoly, MaxVoices / std::max (1, nUnison)));
        // retrigger a note that is still sounding on the same key
        int groupsInUse = 0;
        std::vector<int> groupIds;
        for (auto& v : voices) if (v.active && std::find (groupIds.begin(), groupIds.end(), v.group) == groupIds.end()) groupIds.push_back (v.group);
        groupsInUse = (int) groupIds.size();
        if (groupsInUse >= maxGroups)
        {
            // steal the oldest group, preferring released ones
            int best = -1; uint64_t bestAge = ~0ull; bool bestReleased = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                const bool rel = ! v.gate;
                if (best < 0 || (rel && ! bestReleased) || (rel == bestReleased && v.age < bestAge)) { best = v.group; bestAge = v.age; bestReleased = rel; }
            }
            for (auto& v : voices) if (v.active && v.group == best) { v.active = false; v.gate = false; }
        }
        const int grp = ++groupCounter;
        int placed = 0;
        for (int i = 0; i < MaxVoices && placed < nUnison; ++i)
        {
            Voice& v = voices[i];
            if (v.active) continue;
            v.reset(); v.hpf.high = true; v.lpf.high = false;
            startVoice (v, key, fv, grp, placed, true, true);
            ++placed;
        }
        lastKbd = (float) key;
    }

    void noteOff (int note)
    {
        const int key = std::clamp (note + transposeSemis (p[Transpose]), 0, 127);
        if (! polyMode)
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), key), heldNotes.end());
            if (! heldNotes.empty())
            {
                const int back = heldNotes.back();
                for (int u = 0; u < nUnison; ++u)
                {
                    Voice& v = voices[u];
                    if (! v.active) continue;
                    v.note = back; v.kbdTarget = (float) back;
                    if (multiTrig) startVoice (v, back, v.velocity, 0, u, true, false);
                }
                lastKbd = (float) back;
                return;
            }
            for (int u = 0; u < MaxVoices; ++u) if (voices[u].active && voices[u].group == 0 && voices[u].note == key) voices[u].gate = false;
            return;
        }
        for (auto& v : voices) if (v.active && v.gate && v.note == key && (noteChannel == 0 || v.channel == noteChannel)) v.gate = false;
    }

    void updateBlockValues()
    {
        const bool wasPoly = polyMode; const int wasU = nUnison;
        polyMode = p[Assign] >= 0.5f;
        multiTrig = p[Trigger] >= 0.5f;
        nPoly = polyVoices (p[PolyVoices]);
        nUnison = std::min (unisonVoices (p[UnisonVoices]), 16);
        if (wasPoly != polyMode || wasU != nUnison) { for (auto& v : voices) { v.active = false; v.gate = false; } heldNotes.clear(); }
        if (p[MGTempoSync] >= 0.5f)
        {
            static const float beats[10] = { 4.0f, 2.0f, 4.0f / 3.0f, 1.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
            const float period = (float) mgTimes (p[MGTimes]) * beats[mgBaseNoteIndex (p[MGBaseNote])] * 60.0f / tempo;
            mgInc = 1.0f / (period * fs);
        }
        else mgInc = curves::mgHz (p[MGFreq]) / fs;
        g.wheel = modWheel * 2.0f - 1.0f;
        // measured over the original's factory programs: a unison stack grows slowly in level, and the
        // poly mode plays about 3 dB below the mono mode
        unisonGain = std::pow ((float) nUnison, -0.42f) * (polyMode ? 0.7f : 1.0f);
        g.msw = momentary ? 1.0f : 0.0f;
        computeControls();
    }

    // MG waveform: 0 = falling saw, 0.5 = triangle, 1 = rising saw (+-0.5); rect 0/1
    inline void mgShape (float ph, float& tri, float& rect) const
    {
        const float r = std::clamp (p[MGWave], 0.0005f, 0.9995f);
        tri = ph < r ? ph / r : 1.0f - (ph - r) / (1.0f - r);    // 0..1..0
        tri -= 0.5f;
        rect = ph < r ? 0.0f : 1.0f;
    }

    inline float in (const Voice& v, int j, float normal) const
    {
        const auto& s = sources[(size_t) j];
        if (s.empty()) return normal;
        float sum = 0; for (int o : s) sum += v.jack[o];
        return sum;
    }
    inline bool patched (int j) const { return ! sources[(size_t) j].empty(); }
    // EXT SIGNAL IN takes 5 Vpp: hotter signals (feedback from SIGNAL OUT) are rounded off
    static inline float extSignal (float x) { return 1.2f * dsp::fastTanh (x * (1.0f / 1.2f)); }
    // an S-trigger input: on while any cable into it is pulled low (the momentary
    // switch counts as low while it is pressed); 'normal' when nothing is patched
    inline bool trigActive (const Voice& v, int j, bool normal) const
    {
        const auto& s = sources[(size_t) j];
        if (s.empty()) return normal;
        for (int o : s) { if (o == jMsw ? momentary : v.jack[o] < 0.5f) return true; }
        return false;
    }

    // ext mod sources, as the original scales them: velocity and the bipolar
    // controls swing -1..+1 around their middle; amounts of +-5 then move the
    // filters over the whole knob range and VCO2 by two octaves (measured)
    float extSource (const Voice& v, int idx) const
    {
        switch (idx)
        {
            case 1: return v.velocity * 2.0f - 1.0f;
            case 2: return std::clamp ((v.kbdSemis - 60.0f) / 30.0f, -1.0f, 1.0f);
            case 3: return pressureOf (v);
            case 4: return g.bend;
            case 5: return modWheel;
            case 6: return g.breath;
            case 7: return g.cc16;
            case 8: return g.cc17;
            case 9: return v.jack[jMgTri] * 2.0f;
            case 10: return v.jack[jMgRect];
            case 11: return v.eg1;
            case 12: return 1.0f - v.eg1;
            case 13: return v.eg2 / curves::eg2Peak;
            case 14: return -v.eg2 / curves::eg2Peak;
            default: return 0.0f;
        }
    }
    // the amp destination: velocity acts on the square of its value
    float extAmpSource (const Voice& v, int idx) const
    {
        if (idx == 1) return 2.0f * v.velocity * v.velocity - 1.0f;
        return extSource (v, idx);
    }

    struct Controls
    {
        bool keySync {};
        float porta {};
        float portaCoef {};
        float bendSemis {};
        float fine {};
        float oct1 {};
        float oct2 {};
        float tune2 {};
        int w1 {};
        int w2 {};
        float lvl1 {};
        float lvl2 {};
        float dPitchMg {};
        float dPitchEg {};
        float dHpMg {};
        float dHpEg {};
        float dLpMg {};
        float dLpEg {};
        float hpBase {};
        float lpBase {};
        float kHp {};
        float kLp {};
        float hpTrim {};
        float lpTrim {};
        float sus {};
        float cA {};
        float cD {};
        float cR {};
        float c1A {};
        float c1R {};
        int src1 {};
        int src2 {};
        float a1[5] {};
        float a2[5] {};
    } ctl;
    // the knobs as the voices use them, refreshed every 16 frames (not per sample)
    void computeControls()
    {
        const float dt = 1.0f / fs;
        ctl.keySync = p[MGKeySync] >= 0.5f;
        ctl.porta = curves::portaTau (p[Porta]);
        ctl.portaCoef = ctl.porta > 0 ? 1.0f - std::exp (-dt / ctl.porta) : 1.0f;
        ctl.bendSemis = g.bend * bendRangeSemis (p[BendRange]);
        ctl.fine = (p[FineTune] - 0.5f) * 2.0f;
        static const float sc1[4] = { -24, -12, 0, 12 }, sc2[4] = { -12, 0, 12, 24 };
        ctl.oct1 = sc1[scaleIndex (p[VCO1Scale])];
        ctl.oct2 = sc2[scaleIndex (p[VCO2Scale])];
        ctl.tune2 = (p[VCO2Tune] - 0.5f) * 24.0f;
        ctl.w1 = vco1WaveIndex (p[VCO1Wave]);
        ctl.w2 = vco2WaveIndex (p[VCO2Wave]);
        ctl.lvl1 = curves::mixLevel (p[VCO1Level]);
        ctl.lvl2 = curves::mixLevel (p[VCO2Level]);
        ctl.dPitchMg = curves::pitchDepth (p[PitchMG]);
        ctl.dPitchEg = curves::pitchDepth (p[PitchEG1]);
        ctl.dHpMg = curves::filterDepthOct (p[HPFMG]);
        ctl.dHpEg = curves::filterDepthOct (p[HPFEG2]);
        ctl.dLpMg = curves::filterDepthOct (p[LPFMG]);
        ctl.dLpEg = curves::filterDepthOct (p[LPFEG2]);
        ctl.hpBase = std::log2 (curves::cutoffHz (p[HPFCut]));
        ctl.lpBase = std::log2 (curves::cutoffHz (p[LPFCut]));
        ctl.kHp = peakToK (p[HPFPeak], true);
        ctl.kLp = peakToK (p[LPFPeak], false);
        ctl.hpTrim = std::log2 (peakPitchTrim (p[HPFPeak]));
        ctl.lpTrim = std::log2 (peakPitchTrim (p[LPFPeak]));
        ctl.sus = curves::eg2SustainLevel (p[EG2Sustain]);
        ctl.cA = 1.0f - std::exp (-dt / curves::eg2AttackTau (p[EG2Attack]));
        ctl.cD = 1.0f - std::exp (-dt / curves::eg2Tau (p[EG2Decay]));
        ctl.cR = 1.0f - std::exp (-dt / curves::eg2Tau (p[EG2Release]));
        ctl.c1A = 1.0f - std::exp (-dt / curves::eg1Tau (p[EG1Attack]));
        ctl.c1R = 1.0f - std::exp (-dt / curves::eg1Tau (p[EG1Release]));
        ctl.src1 = extSourceIndex (p[ExtSrc1]);
        ctl.src2 = extSourceIndex (p[ExtSrc2]);
        { const float t[5] = { (p[Ext1PW] - 0.5f) * 2, (p[Ext1VCO2] - 0.5f) * 2, (p[Ext1HPF] - 0.5f) * 2, (p[Ext1LPF] - 0.5f) * 2, (p[Ext1Amp] - 0.5f) * 2 }; for (int i = 0; i < 5; ++i) ctl.a1[i] = t[i]; }
        { const float t[5] = { (p[Ext2PW] - 0.5f) * 2, (p[Ext2VCO2] - 0.5f) * 2, (p[Ext2HPF] - 0.5f) * 2, (p[Ext2LPF] - 0.5f) * 2, (p[Ext2Amp] - 0.5f) * 2 }; for (int i = 0; i < 5; ++i) ctl.a2[i] = t[i]; }
    }

    void renderSample (float& outL, float& outR)
    {
        const float dt = 1.0f / fs;
        // global MG
        g.mgPhase += mgInc; if (g.mgPhase >= 1.0f) g.mgPhase -= 1.0f;
        mgShape (g.mgPhase, g.mgTri, g.mgRect);
        const bool keySync = ctl.keySync;
        const float portaCoef = ctl.portaCoef;
        const float bendSemis = ctl.bendSemis;
        const float fine = ctl.fine;
        const float oct1 = ctl.oct1;
        const float oct2 = ctl.oct2;
        const float tune2 = ctl.tune2;
        const int w1 = ctl.w1;
        const int w2 = ctl.w2;
        const float lvl1 = ctl.lvl1;
        const float lvl2 = ctl.lvl2;
        const float dPitchMg = ctl.dPitchMg;
        const float dPitchEg = ctl.dPitchEg;
        const float dHpMg = ctl.dHpMg;
        const float dHpEg = ctl.dHpEg;
        const float dLpMg = ctl.dLpMg;
        const float dLpEg = ctl.dLpEg;
        const float hpBase = ctl.hpBase;
        const float lpBase = ctl.lpBase;
        const float kHp = ctl.kHp;
        const float kLp = ctl.kLp;
        const float hpTrim = ctl.hpTrim;
        const float lpTrim = ctl.lpTrim;
        const float sus = ctl.sus;
        const float cA = ctl.cA;
        const float cD = ctl.cD;
        const float cR = ctl.cR;
        const float c1A = ctl.c1A;
        const float c1R = ctl.c1R;
        const int src1 = ctl.src1;
        const int src2 = ctl.src2;
        const float* a1 = ctl.a1;
        const float* a2 = ctl.a2;
        smVol += 0.002f * (curves::mixLevel (p[Volume]) - smVol);
        smPan += 0.002f * (p[Pan] - smPan);

        float sumL = 0, sumR = 0;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            float* J = v.jack;
            // ---- sources that do not depend on the audio ----
            if (keySync) { v.mgPhase += mgInc; if (v.mgPhase >= 1.0f) v.mgPhase -= 1.0f; mgShape (v.mgPhase, J[jMgTri], J[jMgRect]); }
            else { J[jMgTri] = g.mgTri; J[jMgRect] = g.mgRect; }
            v.kbdSemis += (v.kbdTarget - v.kbdSemis) * portaCoef;
            J[jKbdCv] = (v.kbdSemis - 40.0f) / 20.0f;          // measured: 0 at E2, +1 unit per 20 semitones
            J[jKbdTrig] = v.gate ? 0.0f : 1.0f;                // S-trigger: high at rest, pulled low while a key is down
            J[jWheel] = g.wheel;                               // -1 .. +1
            J[jMsw] = 0.0f;                                    // a switch to ground: no voltage of its own
            const float white = v.rng.uni() * 0.288f;          // rms 0.166 (as the original's)
            J[jWhite] = white;
            if (anyCables)
            {
                v.pinkB0 = 0.99765f * v.pinkB0 + white * 0.0990460f;
                v.pinkB1 = 0.96300f * v.pinkB1 + white * 0.2965164f;
                v.pinkB2 = 0.57000f * v.pinkB2 + white * 1.0526913f;
                J[jPink] = (v.pinkB0 + v.pinkB1 + v.pinkB2 + white * 0.1848f) * 0.3f;
            }

            // ---- envelopes ----
            // trigger inputs fire while pulled low (S-trigger); the momentary switch fires while pressed
            const bool trigAll = trigActive (v, jTrigIn, v.gate);
            const bool trig1 = patched (jEg1Trig) ? trigActive (v, jEg1Trig, true) : trigAll;
            if (patched (jTrigIn) || patched (jEg1Trig))
            {
                if (trig1 && ! v.eg1GateIn) { v.eg1Stage = 1; v.eg1Delay = curves::eg1Delay (p[EG1Delay]); }
                if (trigAll && ! v.eg2GateIn) { v.eg2Stage = 1; v.holdLeft = curves::eg2Hold (p[EG2Hold]); }
            }
            const bool gate1 = patched (jEg1Trig) || patched (jTrigIn) ? trig1 : v.gate;
            const bool gate2 = patched (jTrigIn) ? trigAll : v.gate;
            v.eg1GateIn = trig1; v.eg2GateIn = trigAll;
            // EG1: delay, attack (RC to 1), release
            if (v.eg1Stage == 1) { v.eg1Delay -= dt; if (v.eg1Delay <= 0) v.eg1Stage = 2; if (! gate1) v.eg1Stage = 3; }
            else if (v.eg1Stage == 2) { v.eg1 += (1.0f - v.eg1) * c1A; if (! gate1) v.eg1Stage = 3; }
            else if (v.eg1Stage == 3) { v.eg1 -= v.eg1 * c1R; if (v.eg1 < 1e-5f) { v.eg1 = 0; v.eg1Stage = 0; } }
            // EG2: hold, attack toward 1.6 (stop at 1), decay / release overshooting below target
            v.holdLeft -= dt;
            const bool g2 = gate2 || v.holdLeft > 0;
            switch (v.eg2Stage)
            {
                case 1: v.eg2 += (curves::eg2AttackTarget - v.eg2) * cA; if (v.eg2 >= curves::eg2Peak) { v.eg2 = curves::eg2Peak; v.eg2Stage = 2; } if (! g2) v.eg2Stage = 4; break;
                case 2:
                    if (sus < v.eg2) { v.eg2 += (sus - curves::eg2Undershoot - v.eg2) * cD; if (v.eg2 <= sus) { v.eg2 = sus; v.eg2Stage = 3; } }
                    else { v.eg2 += (sus - v.eg2) * cD; if (v.eg2 >= sus - 1e-4f) { v.eg2 = sus; v.eg2Stage = 3; } }
                    if (! g2) v.eg2Stage = 4;
                    break;
                case 3: v.eg2 = sus; if (! g2) v.eg2Stage = 4; break;
                case 4: v.eg2 += (-curves::eg2Undershoot - v.eg2) * cR; if (v.eg2 <= 0) { v.eg2 = 0; v.eg2Stage = 0; } break;
                default: break;
            }
            J[jEg1Out] = v.eg1; J[jEg1Rev] = 1.0f - v.eg1;
            J[jEg2Rev] = -(v.eg2 - sus) / curves::eg2Peak;     // measured: the mirror of what EG2 sends the filters

            // ---- MVCA, S&H ----
            J[jMvcaOut] = in (v, jMvcaIn, 0.0f) * std::max (0.0f, in (v, jMvcaCtl, v.eg1));
            if (patched (jShClock))
            {
                const bool clk = trigActive (v, jShClock, false);      // samples as the clock is pulled low
                if (clk && ! v.shClockHigh) v.shValue = in (v, jShIn, 0.0f);
                v.shClockHigh = clk;
            }
            else v.shValue = in (v, jShIn, 0.0f);                       // measured: with no clock it tracks its input
            J[jShOut] = v.shValue;

            // ---- external modulation ----
            const float s1 = src1 ? extSource (v, src1) : 0.0f, s2 = src2 ? extSource (v, src2) : 0.0f;
            const float mPW = a1[0] * s1 + a2[0] * s2;
            const float mV2 = (a1[1] * s1 + a2[1] * s2) * 24.0f;
            const float mHP = (a1[2] * s1 + a2[2] * s2) * curves::octavesPerKnob();
            const float mLP = (a1[3] * s1 + a2[3] * s2) * curves::octavesPerKnob();
            const float mAmp = std::max (0.0f, 1.0f + a1[4] * (src1 ? extAmpSource (v, src1) : 0.0f))
                             * std::max (0.0f, 1.0f + a2[4] * (src2 ? extAmpSource (v, src2) : 0.0f));

            // ---- pitch ----
            const float total = in (v, jTotal, J[jMgTri]);
            const float freqIn = in (v, jFreq, v.eg1 - 1.0f);
            const float kb12 = patched (jVco12Cv) ? 40.0f + in (v, jVco12Cv, 0) * 20.0f : v.kbdSemis;
            const float kb2 = patched (jVco2Cv) ? 40.0f + in (v, jVco2Cv, 0) * 20.0f : kb12;
            v.analogDrift += (v.rng.uni() * 0.02f - v.analogDrift) * 0.00002f;
            const float common = bendSemis + (v.channel != 0 ? chanBend[v.channel] : 0.0f) + fine + dPitchMg * total + dPitchEg * freqIn + v.detuneSemis + v.analogSemis + v.analogDrift * p[Analog] * 3.0f;
            // pitch and filter coefficients run at a quarter of the voice rate (24 kHz at 48 kHz)
            const bool ctlTick = (v.coefCounter++ & 3) == 0;
            if (ctlTick)
            {
                const float semi1 = kb12 + oct1 + common;
                const float semi2 = kb2 + oct2 + tune2 + common + mV2;
                v.f1 = (float) ::aquanode::tuning::noteToHz (semi1); v.f2 = (float) ::aquanode::tuning::noteToHz (semi2);
            }
            const float f1 = v.f1, f2 = v.f2;
            const float dt1 = std::min (0.45f, f1 * dt), dt2 = std::min (0.45f, f2 * dt);

            // ---- oscillators ----
            v.ph1 += dt1; if (v.ph1 >= 1.0f) v.ph1 -= 1.0f;
            v.ph2 += dt2; if (v.ph2 >= 1.0f) v.ph2 -= 1.0f;
            const float sq1 = v.ph1 < 0.5f ? 1.0f : -1.0f;
            const float sq2 = v.ph2 < 0.5f ? 1.0f : -1.0f;
            float o1 = 0, o2 = 0;
            switch (w1)
            {
                case 0: {   // the MS-20's triangle: slightly skewed and rounded, with a trace of saw
                    const float s = 0.534f, ph = v.ph1;
                    const float t = ph < s ? -1.0f + 2.0f * ph / s : 1.0f - 2.0f * (ph - s) / (1.0f - s);
                    const float sawPart = 1.0f - 2.0f * ph + dsp::polyBlep (ph, dt1);
                    const float t2 = t * t, sn = t * (1.5707963f - t2 * (0.6459640f - t2 * (0.0796926f - t2 * 0.0046818f)));   // sin (pi/2 t)
                    o1 = (0.694f * t + 0.306f * sn + 0.096f * sawPart) * 0.4187f;
                    break; }
                case 1: o1 = (0.5f - v.ph1 + 0.5f * dsp::polyBlep (v.ph1, dt1)) ; break;      // falling saw, +-0.5
                case 2: {
                    const float duty = std::clamp (0.5f * (1.0f - std::clamp (p[VCO1PW] + mPW, 0.0f, 1.0f)), 0.0f, 0.5f);
                    if (duty <= 0.0005f) { o1 = 0; break; }
                    float x = v.ph1 < duty ? 1.0f : -1.0f;
                    x += dsp::polyBlep (v.ph1, dt1);
                    float t2 = v.ph1 - duty; if (t2 < 0) t2 += 1.0f;
                    x -= dsp::polyBlep (t2, dt1);
                    o1 = x * 0.4165f; break; }
                default: {  // noise, through the VCO's ~3.8 kHz roll-off
                    const float c = 1.0f - std::exp (-dsp::twoPi * 3800.0f * dt);
                    v.noiseLp += c * (white - v.noiseLp);
                    o1 = v.noiseLp * 2.95f; break; }
            }
            switch (w2)
            {
                case 0: o2 = 0.5f - v.ph2 + 0.5f * dsp::polyBlep (v.ph2, dt2); break;
                case 1: { float x = sq2 + dsp::polyBlep (v.ph2, dt2); float t2 = v.ph2 - 0.5f; if (t2 < 0) t2 += 1.0f; x -= dsp::polyBlep (t2, dt2); o2 = x * 0.4165f; break; }
                case 2: { float x = v.ph2 < 0.1f ? 1.0f : -1.0f; x += dsp::polyBlep (v.ph2, dt2); float t2 = v.ph2 - 0.1f; if (t2 < 0) t2 += 1.0f; x -= dsp::polyBlep (t2, dt2); o2 = x * 0.285f; break; }
                default: o2 = (sq1 * sq2 + 0.28f * (sq1 + sq2)) * ringGain; break;   // ring: the rectangles multiplied, with some of each leaking through
            }

            // ---- mixer, filters ----
            // the original's VCOs are a little softer at the very top
            const float vco = (w1 == 3 ? 0.0f : lvl1 * o1) + lvl2 * o2;
            v.vcoS1 += v.vcoCoef * (vco - v.vcoS1);
            v.vcoS2 += v.vcoCoef * (v.vcoS1 - v.vcoS2);
            const float vcoSoft = v.vcoS2;
            const float mix = vcoSoft + (w1 == 3 ? lvl1 * o1 : 0.0f) + extSignal (in (v, jExtSig, 0.0f));
            if (ctlTick)
            {
                const float hpE = in (v, jHpCut, (v.eg2 - sus) / curves::eg2Peak);
                const float lpE = in (v, jLpCut, (v.eg2 - sus) / curves::eg2Peak);
                float hpOct = hpBase + hpTrim + dHpMg * total + dHpEg * hpE + mHP + v.analogCut;
                float lpOct = lpBase + lpTrim + dLpMg * total + dLpEg * lpE + mLP + v.analogCut;
                const float fh = std::clamp (std::exp2 (hpOct), 5.0f, 24000.0f), fl = std::clamp (std::exp2 (lpOct), 5.0f, 24000.0f);
                auto top = [this] (float f) { return 1.0f - hfLoss * std::clamp (std::log2 (f / 2200.0f) / 3.2f, 0.0f, 1.0f); };
                v.hpf.set (fh, fs, kHp * top (fh));
                v.lpf.set (fl, fs, kLp * top (fl));
                v.hpf.drive = driveHP; v.lpf.drive = driveLP;
                const float fTop = std::min (0.45f * fs, vcoTop * std::pow (std::max (f1, f2) / 261.63f, -vcoTopSlope));
                v.vcoCoef = 1.0f - std::exp (-dsp::twoPi * fTop / fs);
            }
            const float hp = v.hpf.process (mix + white * 1.0e-3f);   // the circuit's own noise lets the filters self-oscillate
            const float lp = v.lpf.process (hp);

            // ---- VCA ----
            const float initGain = in (v, jInitGain, 0.0f);
            const float gain = std::min (1.25f, std::clamp ((v.eg2 - curves::vcaThreshold) / (1.0f - curves::vcaThreshold), 0.0f, 1.0f) + 1.22f * std::max (0.0f, initGain)) * mAmp;   // the VCA runs out at about +2 dB
            const float sig = lp * gain;
            J[jSigOut] = sig;
            J[jPhones] = sig * smVol;

            // ---- external signal processor (only when something is patched into it) ----
            if (patched (jEspIn)) processEsp (v, in (v, jEspIn, 0.0f), dt);

            // ---- voice life ----
            v.timeOn += dt;
            if (v.eg2Stage == 0 && ! v.gate && ! patched (jInitGain)) v.active = false;
            if (patched (jInitGain) && ! v.gate && v.eg1Stage == 0 && v.eg2Stage == 0 && std::fabs (sig) < 1e-6f) v.active = false;

            const float pn = std::clamp ((smPan - 0.5f) * 2.0f + v.pan, -1.0f, 1.0f);
            const float o = sig * smVol * curves::outputGain * unisonGain;
            sumL += o * std::sqrt (0.5f * (1.0f - pn)) * 1.41421356f;
            sumR += o * std::sqrt (0.5f * (1.0f + pn)) * 1.41421356f;
        }
        outL = sumL; outR = sumR;
    }

    void processEsp (Voice& v, float x, float dt)
    {
        float* J = v.jack;
        const float amp = dsp::fastTanh (x * 4.0f * curves::mixLevel (p[ESPLevel]));
        J[jEspAmp] = amp;
        if ((v.coefCounter & 31) == 1)
        {
            const float lo = 50.0f * std::pow (50.0f, p[ESPLowCut]);       // 50 Hz .. 2.5 kHz
            const float hi = 100.0f * std::pow (50.0f, p[ESPHighCut]);     // 100 Hz .. 5 kHz
            v.espHp1.setHP (lo, fs, 0.541f); v.espHp2.setHP (lo, fs, 1.307f);
            v.espLp1.setLP (hi, fs, 0.541f); v.espLp2.setLP (hi, fs, 1.307f);
        }
        const float bp = v.espLp2.process (v.espLp1.process (v.espHp2.process (v.espHp1.process (amp))));
        J[jEspBpf] = bp;
        const float r = std::fabs (bp) * 2.0f;
        v.espEnv += (r > v.espEnv ? 0.02f : 0.0008f) * (r - v.espEnv) * (96000.0f / fs);
        J[jEspEnv] = std::min (1.0f, v.espEnv);
        const float th = 0.02f + 0.8f * p[ESPThreshold];
        if (! v.espTrig && v.espEnv > th) v.espTrig = true; else if (v.espTrig && v.espEnv < th * 0.7f) v.espTrig = false;
        J[jEspTrig] = v.espTrig ? 1.0f : 0.0f;
        // F-V converter: period between rising zero crossings -> 1 V/oct around C4
        v.espPeriodCount += 1.0f;
        if (v.espLastX < 0.0f && bp >= 0.0f && v.espEnv > 0.01f)
        {
            const float f = fs / std::max (1.0f, v.espPeriodCount);
            if (f > 20.0f && f < 5000.0f) v.espFreqCv = std::log2 (f / 261.63f) / 5.0f * (p[ESPCVAdjust] * 2.0f);
            v.espPeriodCount = 0;
        }
        v.espLastX = bp;
        J[jEspFv] = v.espFreqCv;
        (void) dt;
    }

public:
    // knob -> Korg35 feedback; fitted so the resonance peak and the
    // self-oscillation threshold (about 7.2 on the knob) match the original
    float kLpTab[11] = { 0.0f, 0.75f, 1.33f, 1.56f, 1.72f, 1.84f, 1.89f, 2.03f, 2.22f, 2.45f, 2.8f };
    float kHpTab[11] = { 0.0f, 0.75f, 1.32f, 1.57f, 1.73f, 1.85f, 1.92f, 2.04f, 2.22f, 2.45f, 2.8f };
    float peakToK (float pk, bool high) const { return curves::lerpTable (high ? kHpTab : kLpTab, 11, pk); }
    static float peakPitchTrim (float pk)
    {
        static const float t[9] = { 1.0f, 1.006f, 1.012f, 1.0183f, 1.0211f, 1.0252f, 1.0294f, 1.0372f, 1.0436f };   // knob 6 .. 10
        return pk <= 0.6f ? 1.0f : curves::lerpTable (t, 9, (pk - 0.6f) / 0.4f);
    }
    // the original's resonance weakens toward the top of the range
    float hfLoss = 0.08f;
    float driveLP = 0.85f, driveHP = 0.45f;
    float ringGain = 0.31f;
    // the original's VCOs lose their top harmonics, the more the higher they play
    float vcoTop = 13000.0f, vcoTopSlope = 0.35f;
};
}
