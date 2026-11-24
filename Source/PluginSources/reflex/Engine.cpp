#include "../../Engine/Tuning.h"   // (Aquanode: the patch tuning, 6.9.10)
#include "Engine.h"
#include "Tables.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#ifndef EQ_RESAMPLE
#define EQ_RESAMPLE 1
#endif
#ifndef G_RESAMPLE
#define G_RESAMPLE 1
#endif

namespace rfx {
std::vector<float>* g_tap = nullptr;

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kRefPan = 64.5 / 127.0;          // pan used when the output EQ was measured
static constexpr double kRefD6 = 0.705;

static inline double tri(double ph) { ph -= std::floor(ph); return ph < 0.5 ? -1.0 + 4.0 * ph : 3.0 - 4.0 * ph; }
static inline double amt(float v) { return ((double)v - 0.5) * 2.0; }

// ---------------- envelope ----------------
void Envelope::tick(double dtMs, double aMs, double dMs, double sus, double rMs) {
  switch (stage) {
    case Idle: level = 0; lin = 0; break;
    case Attack:
      t += dtMs;                                   // first attack tick already counts one elapsed tick
      if (aMs <= 0.0 || t >= aMs) { level = lin = 1.0; stage = Decay; t = 0; goto decay; }
      level = lin = t / aMs;
      inv = 1.0 - std::exp(-6.0 * (t + dtMs) / aMs);   // the inverted VCA path sees an exponential attack, one tick ahead (measured)
      if (t + dtMs >= aMs) inv = 0.0;                    // ...and a one-tick dropout right before the attack ends (measured glitch)
      break;
    case Decay:
    decay:
      if (dMs <= 0.0 || t >= dMs) { level = lin = sus; stage = Sustain; break; }
      level = sus + (1.0 - sus) * std::exp(-6.0 * t / dMs);
      lin = 1.0 - (1.0 - sus) * t / dMs;           // modulation routings see straight segments
      t += dtMs;
      break;
    case Sustain: level = lin = sus; break;
    case Release:
      if (rMs <= 0.0 || t >= rMs) { level = lin = 0; stage = Idle; break; }
      level = relStart * std::exp(-6.0 * t / rMs);
      lin = relStartLin * (1.0 - t / rMs);
      t += dtMs;
      break;
  }
  if (stage != Attack) inv = lin;
}

void Voice::reset() {
  active = false; gate = false; offDelay = 0;
  // power-up phases of the three oscillators (measured with OSC trg off); copies keep the 0.1-cycle unison spread
  static const double ph0[3][32] = {
    { 0.698, 0.250, 0.720, 0.559, 0.519, 0.363, 0.327, 0.904, 0.449, 0.578, 0.717, 0.874, 0.037, 0.066, 0.714, 0.104, 0.770, 0.679, 0.013, 0.952, 0.017, 0.870, 0.841, 0.615, 0.440, 0.993, 0.955, 0.580, 0.567, 0.041, 0.060, 0.644 },
    { 0.556, 0.624, 0.931, 0.052, 0.612, 0.915, 0.059, 0.435, 0.560, 0.029, 0.032, 0.185, 0.407, 0.712, 0.573, 0.110, 0.331, 0.245, 0.349, 0.054, 0.567, 0.559, 0.031, 0.720, 0.637, 0.678, 0.725, 0.728, 0.988, 0.572, 0.753, 0.255 },
    { 0.950, 0.162, 0.206, 0.170, 0.331, 0.635, 0.076, 0.177, 0.280, 0.870, 0.554, 0.546, 0.449, 0.490, 0.313, 0.519, 0.559, 0.873, 0.568, 0.927, 0.931, 0.162, 0.276, 0.357, 0.382, 0.604, 0.757, 0.766, 0.499, 0.352, 0.607, 0.308 } };
  for (int o = 0; o < 3; ++o) for (int c = 0; c < MaxCopies; ++c) {
    phase[o][c] = frac(ph0[o][slot & 31] + 0.1 * (c / 2)); inc[o][c] = 0; wrapped[o][c] = false;
    ost[o][c] = OscState(); ost[o][c].rng = 0x9E3779B9u * (uint32_t)(1 + slot * 64 + o * 16 + c);   // independent noise per voice/osc/copy
  }
  env1 = Envelope(); env2 = Envelope();
  for (auto& ch : lad) for (auto& l : ch) l.reset();
}

// ---------------- engine ----------------
Engine::Engine() {
  initPatch(p);
  voices.resize(32);
  for (int i = 0; i < 32; ++i) { voices[i].slot = i; voices[i].reset(); }
  setSampleRate(44100.0);
}

void Engine::setSampleRate(double s) {
  sr = s;
  tickLen = std::max(1, (int)(sr / 1000.0));
  samplesPerTick = (double)(float)(sr / 1000.0);
  tickCounter = 0; sampleCount = 0; tickIndex = 0;
  eqMode = -1;
  rev.setup(sr);
}

void Engine::reset() {
  for (auto& v : voices) v.reset();
  for (auto& row : eq) for (auto& b : row) b.reset();
  tickCounter = 0; sampleCount = 0; tickIndex = 0; pending.clear();
  tg.reset(); dly.reset(); rev.setup(sr);
  held.clear(); arpIndex = -1; arpNotes.clear(); ownPpq = 0;
  lastNote = -1; lastVoice = nullptr;
  for (auto& ph : freeLfoPh) ph = 0.5;
  for (auto& k : keyDown) k = false;
  numKeysDown = 0; tgOrigin = 0;
}

void Engine::allNotesOff() {
  held.clear();
  for (auto& k : keyDown) k = false;
  numKeysDown = 0; monoStack.clear();
  for (auto& an : arpNotes) releaseVoice(an.first);
  arpNotes.clear();
  for (auto& v : voices) if (v.active && v.gate) { v.gate = false; v.offDelay = 1; }
}

void Engine::setupEQ() {
  int mode = Map::onOff(p[dummy18]) ? 1 : 0;
  if (mode == eqMode) return;
  eqMode = mode;
  const double (*sos)[6] = mode ? tbl::eqSos1 : tbl::eqSos0;
  for (int s = 0; s < 4; ++s) for (int c = 0; c < 2; ++c) {
    Biquad& b = eq[s][c];
    double b0 = sos[s][0] / sos[s][3], b1 = sos[s][1] / sos[s][3], b2 = sos[s][2] / sos[s][3];
    double a0 = 1.0, a1 = sos[s][4] / sos[s][3], a2 = sos[s][5] / sos[s][3];
    if (std::fabs(sr - 44100.0) > 1e-6 && EQ_RESAMPLE) {
      // measured at 44.1 kHz: map back to the analog prototype (inverse bilinear) and re-discretise at sr
      const double K = 2.0 * 44100.0, Kn = 2.0 * sr;
      auto toS = [K](double c0, double c1, double c2, double& s2, double& s1, double& s0) {
        s2 = (c0 - c1 + c2) / (K * K); s1 = 2.0 * (c0 - c2) / K; s0 = c0 + c1 + c2; };
      double ns2, ns1, ns0, ds2, ds1, ds0; toS(b0, b1, b2, ns2, ns1, ns0); toS(a0, a1, a2, ds2, ds1, ds0);
      auto toZ = [Kn](double s2, double s1, double s0, double& c0, double& c1, double& c2) {
        c0 = s2 * Kn * Kn + s1 * Kn + s0; c1 = 2.0 * (s0 - s2 * Kn * Kn); c2 = s2 * Kn * Kn - s1 * Kn + s0; };
      double nb0, nb1, nb2, na0, na1, na2; toZ(ns2, ns1, ns0, nb0, nb1, nb2); toZ(ds2, ds1, ds0, na0, na1, na2);
      b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    b.b0 = b0; b.b1 = b1; b.b2 = b2; b.a1 = a1; b.a2 = a2;
  }
}

static double interp101(const double* t, double v) {
  v = std::clamp(v, 0.0, 1.0) * 100.0;
  int i = std::min(99, (int)v); double f = v - i;
  return t[i] + (t[i + 1] - t[i]) * f;
}
double Engine::cutoffG(double v) const {
  v = std::clamp(v, 0.0, 1.0) * 100.0;
  int i = std::min(99, (int)v); double f = v - i;
  const double g = std::exp(std::log(tbl::cutoffG[i]) * (1 - f) + std::log(tbl::cutoffG[i + 1]) * f);
  if (std::fabs(sr - 44100.0) < 1e-6 || !G_RESAMPLE) return g;
  return std::tan(std::atan(g) * 44100.0 / sr);   // table holds tan(pi fc / 44100): keep fc in Hz at other rates
}
double Engine::resoK(double v) const { return interp101(tbl::resoK, v); }

// drive: y = A x / (1 + B |x|); A from a spline through 14 measured settings, B = 80 d^4 + 5.2 d^10.8 (fit)
static const float driveA[101] = { 1.00000f, 0.99784f, 0.99553f, 0.99312f, 0.99069f, 0.98830f, 0.98603f, 0.98394f, 0.98209f, 0.98056f, 0.97940f, 0.97870f, 0.97854f, 0.97905f, 0.98033f, 0.98250f, 0.98567f, 0.98998f, 0.99559f, 1.00264f, 1.01130f, 1.02174f, 1.03412f, 1.04860f, 1.06536f, 1.08460f, 1.10653f, 1.13135f, 1.15927f, 1.19053f, 1.22540f, 1.26416f, 1.30700f, 1.35412f, 1.40571f, 1.46200f, 1.52324f, 1.58968f, 1.66161f, 1.73931f, 1.82310f, 1.91330f, 2.01022f, 2.11419f, 2.22554f, 2.34460f, 2.47167f, 2.60709f, 2.75113f, 2.90409f, 3.06620f, 3.23772f, 3.41902f, 3.61051f, 3.81259f, 4.02566f, 4.25011f, 4.48632f, 4.73465f, 4.99544f, 5.26900f, 5.55564f, 5.85580f, 6.16992f, 6.49845f, 6.84186f, 7.20058f, 7.57508f, 7.96578f, 8.37311f, 8.79750f, 9.23938f, 9.69925f, 10.17767f, 10.67520f, 11.19239f, 11.72984f, 12.28812f, 12.86785f, 13.46963f, 14.09410f, 14.74190f, 15.41378f, 16.11050f, 16.83287f, 17.58174f, 18.35799f, 19.16252f, 19.99631f, 20.86035f, 21.75570f, 22.68345f, 23.64476f, 24.64084f, 25.67296f, 26.74245f, 27.85072f, 28.99923f, 30.18955f, 31.42330f, 32.70220f };
static const float driveB[101] = { 0.00000f, 0.00000f, 0.00001f, 0.00006f, 0.00020f, 0.00050f, 0.00104f, 0.00192f, 0.00328f, 0.00525f, 0.00800f, 0.01171f, 0.01659f, 0.02285f, 0.03073f, 0.04050f, 0.05243f, 0.06682f, 0.08398f, 0.10426f, 0.12800f, 0.15559f, 0.18741f, 0.22387f, 0.26542f, 0.31250f, 0.36558f, 0.42516f, 0.49173f, 0.56583f, 0.64801f, 0.73883f, 0.83888f, 0.94877f, 1.06911f, 1.20056f, 1.34378f, 1.49944f, 1.66826f, 1.85095f, 2.04826f, 2.26095f, 2.48980f, 2.73561f, 2.99921f, 3.28143f, 3.58315f, 3.90524f, 4.24861f, 4.61419f, 5.00292f, 5.41577f, 5.85375f, 6.31786f, 6.80914f, 7.32866f, 7.87752f, 8.45681f, 9.06769f, 9.71132f, 10.38889f, 11.10165f, 11.85084f, 12.63776f, 13.46372f, 14.33010f, 15.23828f, 16.18970f, 17.18584f, 18.22823f, 19.31842f, 20.45805f, 21.64878f, 22.89233f, 24.19050f, 25.54513f, 26.95815f, 28.43154f, 29.96737f, 31.56780f, 33.23506f, 34.97150f, 36.77955f, 38.66176f, 40.62079f, 42.65943f, 44.78062f, 46.98742f, 49.28305f, 51.67091f, 54.15457f, 56.73777f, 59.42450f, 62.21892f, 65.12545f, 68.14876f, 71.29378f, 74.56572f, 77.97011f, 81.51281f, 85.20000f };
static void driveCoeffs(double d, double& A, double& B) {
  const double x = std::clamp(d, 0.0, 1.0) * 100.0;
  const int i = std::min(99, (int)x); const double f = x - i;
  A = driveA[i] + (driveA[i + 1] - driveA[i]) * f;
  B = 80.0 * std::pow(d, 4.0) + 5.2 * std::pow(d, 10.8);
}

// second-section cutoff (knob units) of the -X- / -M- modes (placeholder, being measured)
static double dualSecondCutoff(double fv, double q) { return std::max(-3.0, fv + std::log(std::max(1e-9, q)) / 6.0); }   // measured: v2 = v + ln(Q)/6 (knob units)

// ---------------- notes / voices ----------------
void Engine::startVoice(int note, int vel) {
  bool mono = monoMode();
  int maxVoices = mono ? 1 : Map::voices(p[Voices]);
  Voice* v = nullptr;
  for (int i = 0; i < maxVoices; ++i) if (!voices[i].active) { v = &voices[i]; break; }
  if (!v) {
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < maxVoices; ++i) if (voices[i].age < best) { best = voices[i].age; v = &voices[i]; }
  }
  bool wasActive = v->active;
  const bool legato = mono && wasActive && v->gate;   // mono legato: no envelope retrigger (measured)
  v->active = true; v->gate = true; v->note = note; v->vel = vel; v->age = ++ageCounter; v->offDelay = 0;
  v->channel = noteChannel;
  // glide
  static const double glideMs[8] = { 0, 31.25, 62.5, 125, 250, 500, 1000, 2000 };
  int gi = std::clamp((int)(p[GLDspd] * 24.f), 0, 23) % 8;
  if (Map::onOff(p[GLDst]) && lastNote >= 0 && glideMs[gi] > 0) {
    // glide starts from wherever the previous note's pitch currently is (constant glide time)
    v->glideFrom = lastVoice ? lastVoice->pitch : lastNote;
    v->glideT = 0; v->glideDur = glideMs[gi];
  } else { v->glideFrom = note; v->glideT = 0; v->glideDur = 0; }
  v->pitch = v->glideFrom;
  lastNote = note; lastVoice = v;
  (void)wasActive;
  if (Map::onOff(p[OSCtrg]) && v->glideDur <= 0)   // with OSC trg off (or while gliding) the oscillators run on
    { // unison pair k starts 0.1 cycle ahead of pair k-1; the extra copy of an odd count starts at 0
      const int nvc = Map::fatVoices(p[FATvcs]);
      for (int o = 0; o < 3; ++o) for (int c = 0; c < Voice::MaxCopies; ++c) {
        bool extra = (nvc & 1) && c == nvc;
        v->phase[o][c] = extra ? 0.0 : frac(0.1 * (c / 2)); v->wrapped[o][c] = (v->phase[o][c] == 0.0);
      }
    }
  for (int l = 0; l < 3; ++l) v->lfoPh[l] = Map::onOff(p[LFO1trg + 5 * l]) ? 0.0 : freeLfoPh[l];
  v->vibPh = 0;
  if (!legato) { v->env1.noteOn(); v->env2.noteOn(); }
  if (!wasActive) { v->env1.smooth = 0; for (auto& ch : v->lad) for (auto& l : ch) l.reset(); }
}

void Engine::releaseVoice(int note) {
  for (auto& v : voices) if (v.active && v.gate && v.note == note && (noteChannel == 0 || v.channel == noteChannel)) { v.gate = false; v.offDelay = 1; }
}

void Engine::noteOn(int note, int vel) {
  // the trance gate pattern restarts on the first key pressed while no other key is held
  if (numKeysDown == 0) tgOrigin = currentPpq();   // gate grid restarts at the key's control tick
  if (!keyDown[note & 127]) { keyDown[note & 127] = true; ++numKeysDown; }
  if (arpOn()) {
    for (auto& h : held) if (h.first == note) return;
    if (held.empty()) {
      // first key: the arp grid restarts here and the first step plays immediately
      arpNextPpq = eventPpq;
      if (Map::onOff(p[ARPtrg])) { arpIndex = -1; arpDir = 1; }
    }
    held.push_back({ note, vel });
    return;
  }
  if (monoMode()) {
    // mono (and glide) mode: last-note priority with a key stack
    monoStack.erase(std::remove_if(monoStack.begin(), monoStack.end(), [note](const std::pair<int, int>& h) { return h.first == note; }), monoStack.end());
    monoStack.push_back({ note, vel });
  }
  startVoice(note, vel);
}

void Engine::noteOff(int note) {
  if (keyDown[note & 127]) { keyDown[note & 127] = false; --numKeysDown; }
  held.erase(std::remove_if(held.begin(), held.end(), [note](const std::pair<int, int>& h) { return h.first == note; }), held.end());
  if (arpOn()) return;
  if (monoMode()) {
    const bool wasTop = !monoStack.empty() && monoStack.back().first == note;
    monoStack.erase(std::remove_if(monoStack.begin(), monoStack.end(), [note](const std::pair<int, int>& h) { return h.first == note; }), monoStack.end());
    if (wasTop && !monoStack.empty()) { startVoice(monoStack.back().first, monoStack.back().second); return; }   // fall back to the held key
    if (!wasTop) return;
  }
  releaseVoice(note);
}

void Engine::handleEvent(const MidiEv& e) {
  int st = e.b0 & 0xF0;
  eventPpq = ppqAtFrame(e.frame);
  const int ch = mpe ? (e.b0 & 0x0F) + 1 : 1;
  const bool member = ch >= 2 && ch <= 16;
  if (member && st == 0xE0) { int v = (e.b2 << 7) | e.b1; chanBend[ch] = (v - 8192) / 8192.0 * 48.0; return; }
  if (member && st == 0xD0) { chanPress[ch] = e.b1 / 127.0; return; }
  noteChannel = member && ! arpOn() ? ch : 0;   // the arpeggiator's notes are not MPE notes
  handleNoteEvent(e, st);
  noteChannel = 0;
}

void Engine::handleNoteEvent(const MidiEv& e, int st) {
  if (st == 0x90 && e.b2 > 0) noteOn(e.b1, e.b2);
  else if (st == 0x80 || (st == 0x90 && e.b2 == 0)) noteOff(e.b1);
  else if (st == 0xB0) {
    if (e.b1 == 1) modWheel = e.b2 / 127.0;
    else if (e.b1 == 123 || e.b1 == 120) allNotesOff();
  } else if (st == 0xE0) {
    int v = (e.b2 << 7) | e.b1; pitchBend = (v - 8192) / 8192.0;
  } else if (st == 0xD0) {
    aftertouch = e.b1 / 127.0;
  }
}

double Engine::ppqAtFrame(int frame) const {
  const double base = (hostPpqValid && hostPlaying) ? blockPpq : ownPpq;
  return base + frame / sr * tempo / 60.0;
}
double Engine::currentPpq() const {
  // musical time of the current control tick: ticks are exactly 1 ms apart in musical time,
  // even though they land on floor(k * float(sr/1000)) samples (measured on the arp grid)
  const double base = (hostPpqValid && hostPlaying) ? blockPpq : ownPpq;
  const double pos = tickIndex * (sr / 1000.0) - (double)sampleCount;
  return base + pos / sr * tempo / 60.0;
}

// ---------------- arpeggiator ----------------
void Engine::arpTick(double ppq) {
  bool on = arpOn();
  if (!on) {
    if (arpWasOn) { for (auto& an : arpNotes) releaseVoice(an.first); arpNotes.clear(); held.clear(); }
    arpWasOn = false; return;
  }
  arpWasOn = true;
  double stepQ = rateQuarters(Map::syncIdx20(p[ARPtme]));
  // arp notes end after their own gate length, so long gates overlap the following steps
  // in mono/glide mode a note ending exactly when the next step starts is tied to it (legato)
  const bool stepDue = !held.empty() && ppq + 1e-9 >= arpNextPpq;
  for (size_t k = 0; k < arpNotes.size();)
    if (ppq >= arpNotes[k].second && !(monoMode() && stepDue)) { releaseVoice(arpNotes[k].first); arpNotes.erase(arpNotes.begin() + k); } else ++k;
  if (held.empty()) { for (auto& an : arpNotes) releaseVoice(an.first); arpNotes.clear(); return; }
  if (ppq + 1e-9 < arpNextPpq) return;
  // build note sequence
  std::vector<std::pair<int, int>> seq = held;
  int mode = Map::arpMode(p[ARPmod]);
  if (mode != 3) std::sort(seq.begin(), seq.end());     // mode 3 plays in the order keys were pressed
  int oct = Map::arpOct(p[ARPoct]);
  // per octave the pattern runs over the held notes, octaves ascend in the outer loop (measured)
  const int nb = (int)seq.size();
  std::vector<int> pat;
  if (mode == 1) for (int i = nb - 1; i >= 0; --i) pat.push_back(i);
  else if (mode == 2) { for (int i = 0; i < nb; ++i) pat.push_back(i); for (int i = nb - 2; i >= 1; --i) pat.push_back(i); }
  else for (int i = 0; i < nb; ++i) pat.push_back(i);
  std::vector<std::pair<int, int>> full;
  for (int o = 0; o < oct; ++o) for (int i : pat) full.push_back({ std::min(127, seq[i].first + 12 * o), seq[i].second });
  int n = (int)full.size();
  int idx;
  if (mode == 4) { arpRng = arpRng * 1664525u + 1013904223u; idx = (int)((arpRng >> 8) % (uint32_t)n); }
  else idx = (arpIndex + 1) % n;
  arpIndex = idx;
  const int note = full[idx].first;
  if (monoMode() && !arpNotes.empty()) {
    // mono/glide mode: the next step takes over the sounding voice legato (no release, no retrigger)
    arpNotes.clear();
    startVoice(note, full[idx].second);
  } else {
    for (auto& an : arpNotes) releaseVoice(an.first);   // the arp is monophonic: a new step ends the previous note
    arpNotes.clear();
    startVoice(note, full[idx].second);
  }
  double gateQ = rateQuarters(std::clamp((int)(p[ARPgte] * 20.99f), 0, 20));
  arpNotes.push_back({ note, arpNextPpq + gateQ - 1e-9 });
  arpNextPpq += stepQ;
  while (arpNextPpq < ppq) arpNextPpq += stepQ;
}

// ---------------- per-tick voice control ----------------
void Engine::updateVoiceControl(Voice& v) {
  const double m1 = v.env1.lin, m2 = v.env2.lin;   // modulation envelopes: straight-line segments (measured on PWM)
#ifndef MODLIN
#define MODLIN 1
#endif
  const double l1 = MODLIN ? m1 : v.env1.level, l2 = MODLIN ? m2 : v.env2.level;
  double lfo[3];
  for (int l = 0; l < 3; ++l) lfo[l] = tri(v.lfoPh[l]);

  // ----- pitch -----
  if (v.glideDur > 0 && v.glideT < v.glideDur) {
    v.pitch = v.glideFrom + (v.note - v.glideFrom) * (v.glideT / v.glideDur);
    v.glideT += 1.0;
  } else v.pitch = v.note;
  double semis = amt(p[EG2Ptc]) * 48.0 * l2;
  semis += pitchBend * Map::pbRange(p[PBrng]);
  const bool member = v.channel >= 2 && v.channel <= 16;
  if (member) semis += chanBend[v.channel];
  const double at = member && chanPress[v.channel] >= 0 ? chanPress[v.channel] : aftertouch;
  semis += at * p[VIBdpt] * tri(v.vibPh);
  double detune = 1.0 + 0.0225 * amt(p[EGDet]) * l1;

  // ----- unison -----
  double fat = p[OSCfat]; if (fat > 0.5) fat -= 0.5;
  double d0 = 0.025 * fat;
  int nv = Map::fatVoices(p[FATvcs]);
  int copies; double det[Voice::MaxCopies]; int side[Voice::MaxCopies];
  if (d0 <= 0.0) { copies = 1; det[0] = 0; side[0] = 0; }
  else {
    int pairs = (nv + 1) / 2; copies = 0;
    for (int k = 0; k < pairs; ++k) {
      double dk = d0 + 0.00125 * k;
      det[copies] = -dk; side[copies] = -1; copies++;
      bool lastOdd = (nv & 1) && (k == pairs - 1);
      det[copies] = lastOdd ? -d0 : dk; side[copies] = +1; copies++;
    }
  }
  v.nCopies = copies;
  int n = std::clamp(v.note, 0, 127);
  double base = ::aquanode::tuning::noteToHz((double) (v.pitch + semis)) * 0.5 * tbl::tuneRatio[n] * detune;
  const int smP[3] = { OSC1sm, OSC2sm, OSC3sm };
  for (int o = 0; o < 3; ++o) {
    const double la = amt(p[LFO1De + 5 * o]);          // LFO n modulates oscillator n
    double f = base * std::pow(2.0, (Map::semis(p[smP[o]]) + 12.0 * la * la * la * lfo[o]) / 12.0);
    // upper half of the Fat knob also spreads the oscillators: osc 2 up, osc 3 down by 2 % * amount (measured)
    if (p[OSCfat] > 0.5f && o > 0) f *= (o == 1 ? 1.0 + 0.02 * fat : 1.0 - 0.02 * fat);
    for (int c = 0; c < copies; ++c) v.inc[o][c] = std::min(0.49, f * (1.0 + det[c]) / sr);
  }
  // ----- pan / spread -----
  double vp = p[AMPpan] + ((v.slot & 1) ? 0.5 : -0.5) * p[AMPspd];
  // unison copies are routed to one side each, keeping the voice's own pan gain for that side
  const double cp = std::clamp(vp, 0.0, 1.0);
  const double gl = std::cos(cp * kPi * 0.5), gr = std::sin(cp * kPi * 0.5);
  v.panG[0] = gl; v.panG[1] = gr;
  for (int c = 0; c < copies; ++c) {
    v.copyGainL[c] = side[c] <= 0 ? gl : 0.0;
    v.copyGainR[c] = side[c] >= 0 ? gr : 0.0;
  }
  // ----- pulse width -----
  // PW reads the LFO one tick ahead (measured)
  double lfoP[3]; for (int l = 0; l < 3; ++l) lfoP[l] = tri(v.lfoPh[l] + Map::lfoHz(p[LFO1frq + 5 * l]) * 0.001);
  const double pwBase = amt(p[EGPWM]) * m1 + amt(p[EG2PWM]) * m2;
  for (int o = 0; o < 3; ++o) v.pwm[o] = pwBase + amt(p[LFO1PW + 5 * o]) * (lfoP[o] + 1.0) * 0.5;
  // ----- filter -----
  double va = amt(p[VelCut]), vel = v.vel / 127.0;
  double fv = p[FLTf] * (va >= 0 ? (1.0 - va) + va * vel : 1.0 + va * vel);
  fv += amt(p[KeyCut]) * (v.note - 64) / 100.0;
  fv += amt(p[EGFlt]) * l1 + amt(p[EG2Flt]) * l2;
  for (int l = 0; l < 3; ++l) fv += amt(p[LFO1Fl + 5 * l]) * (lfo[l] + 1.0) * 0.5;
  fv = std::clamp(fv, 0.0, 1.0);
  double q = std::clamp((double)p[FLTQ] + amt(p[EG2Res]) * l2, 0.0, 1.0);
  int ft = Map::filterType(p[FLTtp]);
  if (ft == 5 || ft == 10) {          // dual-ladder modes: both sections at Q 0.5, second cutoff follows Q
    double g = cutoffG(fv); v.G = g / (1.0 + g); v.K = 0.5 * resoK(fv);
    double v2 = dualSecondCutoff(fv, q);
#ifdef ORX_MEASURE
    static const char* ov = getenv("ORX_V2"); if (ov) v2 = atof(ov);
#endif
    // below the bottom of the cutoff table the second section keeps falling exponentially
    // below the table the cutoff keeps falling with the table's (decreasing) slope, then meets a ~21 Hz floor (measured)
    double g2;
    if (v2 >= 0.0) g2 = cutoffG(v2);
    else { const double k = -v2 * 100.0, kq = std::min(k, 60.0);
           g2 = tbl::cutoffG[0] * std::exp(-(0.051 * kq - 0.00025 * kq * (kq + 1.0) + 0.0207 * (k - kq))); }
    { const double gF = 0.001507; g2 = std::cbrt(g2 * g2 * g2 + gF * gF * gF); }
#ifdef ORX_MEASURE
    static const char* og = getenv("ORX_G2"); if (og) g2 = atof(og);
#endif
    v.G2 = g2 / (1.0 + g2);
    v.K2 = 0.5 * resoK(std::max(0.0, v2));
  } else {
    double g = cutoffG(fv);
#ifdef ORX_MEASURE
    static const char* og1 = getenv("ORX_G1"); if (og1) g = atof(og1);
#endif
    v.G = g / (1.0 + g); v.K = q * resoK(fv);
  }
  // advance per-voice LFOs (1 ms per tick)
  for (int l = 0; l < 3; ++l) v.lfoPh[l] += Map::lfoHz(p[LFO1frq + 5 * l]) * 0.001;
  v.vibPh += Map::vibHz(p[VIBspd]) * 0.001;
}

void Engine::onTick() {
  setupEQ();
  for (int l = 0; l < 3; ++l) freeLfoPh[l] += Map::lfoHz(p[LFO1frq + 5 * l]) * 0.001;
  // arpeggiator and trance gate run on the tick grid
  double ppq = currentPpq();
  arpTick(ppq);
  if (Map::onOff(p[TGstat])) {
    double stepQ = rateQuarters(Map::syncIdx20(p[TGspd]));
    long long step = (long long)std::floor((ppq - 0.001 * tempo / 60.0 - tgOrigin) / stepQ + 1e-9);   // gate follows one tick late
    unsigned mask = Map::tgPattern(p[TGpat]);
    tg.target = ((mask >> (int)(((step % 16) + 16) % 16)) & 1u) ? 1.0 : 0.0;
  } else tg.target = 1.0;
  const double dtMs = 1.0;
  for (auto& v : voices) {
    if (!v.active) continue;
    v.linPrev = v.env1.inv;   // the inverted VCA path sees the envelope one tick late
    v.env1.tick(dtMs, Map::envMs(p[Attack]), Map::envMs(p[Decay]), p[Sustain], Map::envMs(p[Release]));
    v.env2.tick(dtMs, Map::envMs(p[Attack2]), Map::envMs(p[Decay2]), p[Sustain2], Map::envMs(p[Release2]));
    updateVoiceControl(v);
  }
}

// ---------------- audio ----------------
void Engine::renderSample(double& outL, double& outR) {
  const int wv[3] = { Map::wave(p[OSC1]), Map::wave(p[OSC2]), Map::wave(p[OSC3]) };
  const double lv[3] = { (double)p[OSC1vl] * p[OSC1vl], (double)p[OSC2vl] * p[OSC2vl], (double)p[OSC3vl] * p[OSC3vl] };
  const int ftype = Map::filterType(p[FLTtp]);
  const double drv = p[AMPdrv];
  const double upA = 1000.0 / sr;
  const double dnA = 0.0075 * 44100.0 / sr;
  const double velAmt = amt(p[VelVol]);
  const double egVol = amt(p[EGVol]);
  double sumL = 0, sumR = 0;
  for (auto& v : voices) {
    if (!v.active) continue;
    double xL = 0, xR = 0;
    for (int o = 0; o < 3; ++o) {
      const double pw = std::min(1.0, std::fabs(v.pwm[o]));
      for (int c = 0; c < v.nCopies; ++c) {
        double ph = v.phase[o][c];
        if (wv[o] != 0 && lv[o] != 0.0) {
          double s = waveValue(wv[o], ph, pw, v.inc[o][c], v.wrapped[o][c], v.ost[o][c]) * lv[o];
          xL += s * v.copyGainL[c]; xR += s * v.copyGainR[c];
        }
        ph += v.inc[o][c];
        bool w = ph >= 1.0;
        if (w) ph -= 1.0;
        v.phase[o][c] = ph; v.wrapped[o][c] = w;
      }
    }
    double ch[2] = { xL, xR };
    for (int c = 0; c < 2; ++c) {
      double u, y[5];
      v.lad[c][0].tick(ch[c], v.G, v.K, u, y);
      double o;
      switch (ftype) {
        case 0: o = y[1]; break;
        case 1: o = y[2]; break;
        case 2: o = y[4]; break;
        case 3: o = u - y[4]; break;
        case 4: o = u - y[1]; break;
        case 5: { double u2, y2[5]; v.lad[c][1].tick(u, v.G2, v.K2, u2, y2); o = u2; break; }
        case 6: o = 2.0 * (y[4] - y[2]); break;
        case 7: o = u; break;
        case 8: o = 0.5 * (u + y[1]); break;
        case 9: o = 0.5 * (u + y[1]) - 1.5 * y[4]; break;
        case 10: { double u2, y2[5]; v.lad[c][1].tick(y[4], v.G2, v.K2, u2, y2); o = 2.25 * u2; break; }
        case 11: case 12: {                                                          // Wv1 / Wv2: shaped band-pass
          const double bp = 2.0 * (y[4] - y[2]);
#ifdef ORX_MEASURE
          if (g_tap && c == 0 && &v == &voices[0]) g_tap->push_back((float)bp);
#endif
          // measured: Wv1 = sin(1.397 * BP), Wv2 = sin(2.79 * BP) on the (un-panned) voice signal - a wave folder
          const double g = std::max(1e-6, v.panG[c]);
          o = g * std::sin((ftype == 11 ? 1.397 : 2.79) * bp / g);
          break;
        }
        default: o = y[1]; break;
      }
      ch[c] = o;
    }
    if (drv > 0.0) {
      double A, B; driveCoeffs(drv, A, B);
      // the drive acts on the voice signal before panning: undo the pan gain for the curve
      for (int c = 0; c < 2; ++c) ch[c] = A * ch[c] / (1.0 + B * std::fabs(ch[c]) / std::max(1e-6, v.panG[c]));
    }
    if (v.offDelay > 0 && --v.offDelay == 0) {
      v.vcaOff = -egVol * (1.0 - v.linPrev);   // inverted EG>Vol: shape part frozen at key-up, then the release curve scales it
      v.env1.noteOff(Map::envMs(p[Release]), 1.0); v.env2.noteOff(Map::envMs(p[Release2]), 1.0);
    }
    // VCA target (measured EG>Vol semantics): positive amounts mix a key gate with the envelope,
    // negative amounts invert the straight-line envelope while held and follow the release curve after key-up
    const bool released = v.env1.stage == Envelope::Release || v.env1.stage == Envelope::Idle;
    double target;
    if (egVol >= 0) target = (1.0 - egVol) * (released ? 0.0 : 1.0) + egVol * v.env1.level;
    else target = released ? v.vcaOff * (v.env1.relStart > 0 ? v.env1.level / v.env1.relStart : 0.0) : 1.0 + egVol * v.linPrev;
    double out = v.env1.smooth;
    v.env1.smooth += (target > v.env1.smooth ? upA : dnA) * (target - v.env1.smooth);
    double vel = v.vel / 127.0;
    double vg = velAmt >= 0 ? (1.0 - velAmt) + velAmt * vel : 1.0 + velAmt * vel;
    double g = out * vg;
    sumL += ch[0] * g; sumR += ch[1] * g;
    if (v.env1.stage == Envelope::Idle && v.env1.smooth < 1.7e-7) v.active = false;   // threshold matched to the reference (phase after voice reuse)
  }
  double master = (double)p[dummy6] * p[dummy6] * p[AMPvol] / (std::cos(kRefPan * kPi * 0.5) * kRefD6 * kRefD6);
  sumL *= master; sumR *= master;
  // trance gate (linear ramp, speed from contour)
  if (Map::onOff(p[TGstat])) {
    double c = p[TGcont];
    double rate = (0.3288 * c * c + 0.001656) * 1000.0 / sr;   // per-ms ramp, measured (has a floor at contour 0)
    if (tg.g < tg.target) tg.g = std::min(tg.target, tg.g + rate); else if (tg.g > tg.target) tg.g = std::max(tg.target, tg.g - rate);
    double gg = 1.0 - p[TGmix] * (1.0 - tg.g);
    sumL *= gg; sumR *= gg;
  } else tg.g = 1.0;
  for (int s = 0; s < 4; ++s) { sumL = eq[s][0].process(sumL); sumR = eq[s][1].process(sumR); }
  if (split != 1) fxTail(sumL, sumR);
  outL = sumL; outR = sumR;
}

// the delay and the reverb (the patch version's Effects module runs them alone)
void Engine::fxTail(double& sumL, double& sumR) {
  const bool fxOn = Map::onOff(p[dummy8]);
  if (fxOn && Map::onOff(p[DLYsta])) {
    int mode = Map::dlyMode(p[DLYmod]);
    if (mode == 3) {                // "Cross": right channel delayed by v*100 ms (min 1 ms) at +2.28 dB, left dry; mix/feedback unused
      int ds = std::max((int)std::lround(0.001 * sr), (int)std::lround(p[DLYtme] * 0.1 * sr));
      dly.processCross(sumL, sumR, ds);
    } else {
      double q = delayQuarters(Map::dlyTimeIdx(p[DLYtme]));
      int ds = (int)(q * 60.0 / tempo * sr);
      double m = p[DLYmix];
      double d = p[DLYdmp]; double damp = 1.0 - (1.0 - d) * (1.0 - d);
      dly.process(sumL, sumR, ds, 0.95 * p[DLYfbk], damp, std::cos(m * kPi * 0.5), std::sin(m * kPi * 0.5), mode);
    }
  }
  if (fxOn && Map::onOff(p[REVsta])) {
    double wL, wR;
    float room = (float)(p[REVroo] * 0.28 + 0.7), damp = (float)(p[REVdmp] * 0.4);
    rev.process(sumL, sumR, (int)(p[REVpdl] * 0.2 * sr), room, damp, wL, wR);
    double width = p[REVwdt], m = p[REVmix];
    double wet = std::sin(m * kPi * 0.5) * 1.5, dry = std::cos(m * kPi * 0.5);
    double w1 = wet * (width * 0.5 + 0.5), w2 = wet * ((1.0 - width) * 0.5);
    // measured: the left output gets no cross-feed from the right tank (only the right output is widened)
    double nL = sumL * dry + wL * w1;
    double nR = sumR * dry + wR * w1 + wL * w2;
    sumL = nL; sumR = nR;
  }
}

void Engine::processEffectsOnly(float* L, float* R, int n) {
  for (int i = 0; i < n; ++i) {
    double l = L[i], r = R[i];
    fxTail(l, r);
    L[i] = (float)l; R[i] = (float)r;
  }
}

void Engine::doTick(const MidiEv* ev, int nev, int& ei, int segEnd) {
  // events of one tick: events sharing a frame are handled last-to-first (measured: chords, arp order)
  batch.assign(pending.begin(), pending.end());
  pending.clear();
  while (ev && ei < nev && ev[ei].frame < segEnd) { batch.push_back(ev[ei]); ++ei; }
  for (size_t i = 0; i < batch.size();) {
    size_t j = i; while (j < batch.size() && batch[j].frame == batch[i].frame) ++j;
    std::reverse(batch.begin() + i, batch.begin() + j);
    i = j;
  }
  for (auto& e : batch) handleEvent(e);
  onTick();
  ++tickIndex;
}

void Engine::processBlock(float* outL, float* outR, int n, const MidiEv* ev, int nev) {
  int ei = 0, pos = 0;
  blockPpq = hostPpq;
  if (tickIndex == 0 && tickCounter == 0) {
    blockPos = 0;
    int segEnd = std::min(n, (int)std::floor(samplesPerTick));
    doTick(ev, nev, ei, segEnd);
    tickCounter = (int)((long long)std::floor(tickIndex * samplesPerTick) - sampleCount);
  }
  while (pos < n) {
    if (tickCounter == 0) {
      blockPos = pos;
      long long next = (long long)std::floor((tickIndex + 1) * samplesPerTick);
      int segEnd = (int)std::min<long long>(n, next - sampleCount);
      doTick(ev, nev, ei, segEnd);
      tickCounter = (int)(next - (sampleCount + pos));
    }
    int segEnd = std::min(n, pos + tickCounter);
    while (ei < nev && ev[ei].frame < segEnd) { pending.push_back(ev[ei]); ++ei; }
    for (int i = pos; i < segEnd; ++i) {
      double L, R; renderSample(L, R);
      outL[i] = (float)L; outR[i] = (float)R;
    }
    tickCounter -= (segEnd - pos);
    pos = segEnd;
  }
  while (ei < nev) { pending.push_back(ev[ei]); ++ei; }
  for (auto& e : pending) e.frame -= n;      // keep frames relative to the next block
  sampleCount += n;
  ownPpq += n / sr * tempo / 60.0;
  if (hostPpqValid) hostPpq += n / sr * tempo / 60.0;   // advanced copy in case the host doesn't update
  if (tickCounter == 0) {
    // a tick landing exactly on the block boundary runs now; ppq bases are already advanced
    blockPpq = hostPpq; blockPos = 0;
    int dummy = 0;
    doTick(nullptr, 0, dummy, 0);
    tickCounter = (int)((long long)std::floor(tickIndex * samplesPerTick) - sampleCount);
  }
}

} // namespace rfx
