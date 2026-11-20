// Reflex engine - a measured re-implementation of a classic 2005-era VA synth.
// JUCE-free so it can be driven both by the plugin and by the CLI measurement host.
#pragma once
#include "Params.h"
#include "Waves.h"
#include "Fx.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <array>

namespace rfx {
extern std::vector<float>* g_tap;   // debug tap (measurement builds only)

struct MidiEv { int frame; uint8_t b0, b1, b2; };

struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  inline double process(double x) {
    double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void reset() { z1 = z2 = 0; }
};

// 4-stage ladder of bilinear (TPT) one-poles with unit-delayed resonance feedback
struct Ladder {
  double s[4] = { 0, 0, 0, 0 };
  double y4prev = 0;
  void reset() { for (auto& v : s) v = 0; y4prev = 0; }
  inline void tick(double x, double G, double k, double& u, double* y) {
    u = x - k * y4prev;
    double in = u;
    for (int i = 0; i < 4; ++i) {
      double v = (in - s[i]) * G;
      double out = v + s[i];
      s[i] = out + v;
      y[i + 1] = out;
      in = out;
    }
    y4prev = y[4];
  }
};

struct Envelope {
  enum Stage { Idle, Attack, Decay, Sustain, Release };
  Stage stage = Idle;
  bool atkEnd = false;
  double level = 0, lin = 0, inv = 0, t = 0, relStart = 0, relStartLin = 0, smooth = 0;
  void noteOn() { stage = Attack; t = 0; }
  void noteOff(double rMs, double dtMs) {
    if (stage == Idle) return;
    stage = Release; relStart = level; relStartLin = lin;
    if (rMs <= 0.0) { level = lin = 0; stage = Idle; return; }
    t = dtMs; level = relStart * std::exp(-6.0 * t / rMs); lin = relStartLin * (1.0 - t / rMs); t += dtMs;
  }
  void tick(double dtMs, double aMs, double dMs, double sus, double rMs);
};

struct Voice {
  bool active = false, gate = false;
  int note = 60, vel = 100, slot = 0, offDelay = 0;
  int channel = 0;   // MPE (Aquanode): the member channel (2..16) it plays, 0 otherwise
  uint64_t age = 0;
  static constexpr int MaxCopies = 11;
  double phase[3][MaxCopies];
  double inc[3][MaxCopies];
  bool wrapped[3][MaxCopies];
  OscState ost[3][MaxCopies];
  double pwm[3] = { 0, 0, 0 };
  double panG[2] = { 0.7071, 0.7071 };
  double vcaOff = 1.0, linPrev = 0.0;
  int nCopies = 1;
  double copyGainL[MaxCopies], copyGainR[MaxCopies];
  Envelope env1, env2;
  Ladder lad[2][2];
  double G = 1, K = 0, G2 = 1, K2 = 0;
  double lfoPh[3] = { 0, 0, 0 };
  double vibPh = 0;
  double pitch = 60, glideFrom = 60, glideT = 0, glideDur = 0;
  void reset();
};

class Engine {
public:
  Engine();
  void setSampleRate(double sr);
  void setParam(int i, float v) { if (i >= 0 && i < NumParams) p[i] = v; }
  float getParam(int i) const { return (i >= 0 && i < NumParams) ? p[i] : 0.f; }
  void setAll(const float* v) { for (int i = 0; i < NumParams; ++i) p[i] = v[i]; }
  void processBlock(float* outL, float* outR, int n, const MidiEv* ev, int nev);
  // Aquanode patch version: 0 = the whole synth, 1 = voices only (delay and reverb left out),
  // 2 = effects only (processEffectsOnly runs the delay and reverb on a signal)
  int split = 0;
  void processEffectsOnly(float* L, float* R, int n);
  void setTempo(double bpm) { if (bpm > 1.0) tempo = bpm; }
  void setTransport(bool playing, double ppq) { hostPlaying = playing; hostPpq = ppq; hostPpqValid = true; }
  void allNotesOff();
  void reset();
  float p[NumParams];
  // MPE (Aquanode), switched on by the host at the first note on a member channel:
  // channels 2..16 then carry one note each, with its own bend (+-48 semitones, on
  // top of the global bend) and its own pressure (in place of the global aftertouch,
  // for the vibrato). Channel 1 stays the global channel.
  bool mpe = false;

private:
  double sr = 44100.0;
  int tickLen = 44;
  int tickCounter = 0;
  double samplesPerTick = 44.1;
  long long sampleCount = 0, tickIndex = 0;
  double tempo = 120.0;
  bool hostPlaying = false, hostPpqValid = false;
  double hostPpq = 0, blockPpq = 0;
  int blockPos = 0;

  std::vector<Voice> voices;
  uint64_t ageCounter = 0;
  std::vector<MidiEv> pending;
  double lastNote = -1;
  Voice* lastVoice = nullptr;
  double freeLfoPh[3] = { 0.5, 0.5, 0.5 };   // free-running LFOs start at their peak

  // master
  Biquad eq[4][2];
  int eqMode = -1;
  double modWheel = 0, pitchBend = 0, aftertouch = 0;
  int noteChannel = 0;   // MPE: the member channel of the event being handled, 0 otherwise
  double chanBend[17] = {};
  double chanPress[17] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
  TranceGate tg;
  TempoDelay dly;
  Freeverb rev;
  double ownPpq = 0;

  // arpeggiator
  std::vector<std::pair<int, int>> held;   // note, velocity (in press order)
  int arpIndex = -1, arpDir = 1;
  double arpNextPpq = 0;
  std::vector<std::pair<int, double>> arpNotes;   // sounding arp notes and their note-off ppq
  uint32_t arpRng = 0x2468ace1u;
  bool arpWasOn = false;
  bool keyDown[128] = {};
  int numKeysDown = 0;
  double tgOrigin = 0;

  void onTick();
  void fxTail(double& L, double& R);
  void doTick(const MidiEv* ev, int nev, int& ei, int segEnd);
  void handleEvent(const MidiEv& e);
  void handleNoteEvent(const MidiEv& e, int st);
  void noteOn(int note, int vel);
  void noteOff(int note);
  void startVoice(int note, int vel);
  void releaseVoice(int note);
  void updateVoiceControl(Voice& v);
  void renderSample(double& L, double& R);
  void setupEQ();
  void arpTick(double ppqNow);
  double currentPpq() const;
  double ppqAtFrame(int frame) const;
  double eventPpq = 0;
  bool monoMode() const { return Map::onOff(p[dummy13]) || Map::onOff(p[GLDst]); }   // glide switches the synth to mono
  std::vector<std::pair<int, int>> monoStack;
  std::vector<MidiEv> batch;
  bool arpOn() const { return Map::onOff(p[ARPsta]) && Map::onOff(p[dummy7]); }   // hidden param 7 is the arp master switch

  double cutoffG(double v) const;
  double resoK(double v) const;
};

} // namespace rfx
