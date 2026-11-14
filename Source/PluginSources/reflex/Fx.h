// Reflex master effects: trance gate, tempo delay, Freeverb-style reverb (all measured against the reference)
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace rfx {

// quarter-note lengths of the 27 delay time slots (9 straight, 9 triplet, 9 dotted)
inline double delayQuarters(int idx) {
  static const double base[9] = { 0.125, 0.25, 0.5, 1, 2, 4, 8, 12, 16 };
  idx = std::clamp(idx, 0, 26);
  double q = base[idx % 9];
  if (idx / 9 == 1) q *= 2.0 / 3.0; else if (idx / 9 == 2) q *= 1.5;
  return q;
}
// quarter-note lengths of the 21 arp / gate rate slots (7 straight from 1/64 to 1/1, 7 triplet, 7 dotted)
inline double rateQuarters(int idx) {
  idx = std::clamp(idx, 0, 20);
  double q = 0.0625 * std::pow(2.0, idx % 7);
  if (idx / 7 == 1) q *= 2.0 / 3.0; else if (idx / 7 == 2) q *= 1.5;
  return q;
}
inline const char* rateName(int idx) {
  static const char* n[21] = { "1/64","1/32","1/16","1/8","1/4","1/2","1/1","1/64T","1/32T","1/16T","1/8T","1/4T","1/2T","1/1T",
                               "1/64.","1/32.","1/16.","1/8.","1/4.","1/2.","1/1." };
  return n[std::clamp(idx, 0, 20)];
}
inline const char* delayName(int idx) {
  static const char* n[27] = { "1/32","1/16","1/8","1/4","1/2","1/1","2/1","3/1","4/1","1/32T","1/16T","1/8T","1/4T","1/2T","1/1T","2/1T","3/1T","4/1T",
                               "1/32.","1/16.","1/8.","1/4.","1/2.","1/1.","2/1.","3/1.","4/1." };
  return n[std::clamp(idx, 0, 26)];
}

struct TranceGate {
  double g = 1.0;        // current gate gain (0..1)
  double target = 1.0;
  void reset() { g = 1.0; target = 1.0; }
};

struct TempoDelay {
  static constexpr int Len = 1 << 18;
  std::vector<float> bufL, bufR;
  int pos = 0;
  double lpL = 0, lpR = 0;
  TempoDelay() : bufL(Len, 0.f), bufR(Len, 0.f) {}
  void reset() { std::fill(bufL.begin(), bufL.end(), 0.f); std::fill(bufR.begin(), bufR.end(), 0.f); lpL = lpR = 0; pos = 0; }
  inline void processCross(double& L, double& R, int delaySamples) {
    (void)L;
    bufR[pos] = (float)R;
    int rd = (pos - std::clamp(delaySamples, 0, Len - 1)) & (Len - 1);
    R = 1.3 * bufR[rd];
    pos = (pos + 1) & (Len - 1);
  }
  // mode 0 mono, 1 stereo, 2 ping-pong
  inline void process(double& L, double& R, int delaySamples, double fb, double damp, double dry, double wet, int mode) {
    double inL = L, inR = R;
    if (mode == 0) { double m = 0.5 * (inL + inR); inL = inR = m; }
    if (delaySamples <= 0 || delaySamples >= Len) {
      bufL[pos] = 0.f; bufR[pos] = 0.f; pos = (pos + 1) & (Len - 1);
      L = L * dry; R = R * dry; return;
    }
    int rd = (pos - delaySamples) & (Len - 1);
    double dL = bufL[rd], dR = bufR[rd];
    lpL = (1.0 - damp) * dL + damp * lpL;
    lpR = (1.0 - damp) * dR + damp * lpR;
    if (mode == 2) { bufL[pos] = (float)(inR + fb * lpR); bufR[pos] = (float)(inL + fb * lpL); }
    else { bufL[pos] = (float)(inL + fb * lpL); bufR[pos] = (float)(inR + fb * lpR); }
    pos = (pos + 1) & (Len - 1);
    L = L * dry + lpL * wet; R = R * dry + lpR * wet;
  }
};

// Jezar's Freeverb topology (8 combs + 4 allpasses per channel)
struct Freeverb {
  struct Comb { std::vector<float> b; int i = 0; float store = 0;
    inline float process(float x, float fb, float d1, float d2) {
      float out = b[i]; store = out * d2 + store * d1; b[i] = x + store * fb; if (++i >= (int)b.size()) i = 0; return out; } };
  struct Allpass { std::vector<float> b; int i = 0;
    inline float process(float x) { float bo = b[i]; float out = -x + bo; b[i] = x + bo * 0.5f; if (++i >= (int)b.size()) i = 0; return out; } };
  Comb cL[8], cR[8]; Allpass aL[4], aR[4];
  std::vector<float> pre; int preI = 0;
  void setup(double sr) {
    static const int cl[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int al[4] = { 556, 441, 341, 225 };
    double s = sr / 44100.0;
    for (int k = 0; k < 8; ++k) { cL[k].b.assign(std::max(1, (int)(cl[k] * s)), 0.f); cR[k].b.assign(std::max(1, (int)((cl[k] + 23) * s)), 0.f); cL[k].i = cR[k].i = 0; cL[k].store = cR[k].store = 0; }
    for (int k = 0; k < 4; ++k) { aL[k].b.assign(std::max(1, (int)(al[k] * s)), 0.f); aR[k].b.assign(std::max(1, (int)((al[k] + 23) * s)), 0.f); aL[k].i = aR[k].i = 0; }
    pre.assign((size_t)(0.25 * sr) + 2, 0.f); preI = 0;
  }
  // returns wet L/R (unscaled freeverb outputs)
  inline void process(double inL, double inR, int preSamples, float room, float damp, double& wL, double& wR) {
    float x = (float)((inL + inR) * 0.015);
    int n = (int)pre.size();
    pre[preI] = x;
    int rd = preI - preSamples; if (rd < 0) rd += n;
    float in = pre[rd];
    if (++preI >= n) preI = 0;
    float oL = 0, oR = 0, d1 = damp, d2 = 1.f - damp;
    for (int k = 0; k < 8; ++k) { oL += cL[k].process(in, room, d1, d2); oR += cR[k].process(in, room, d1, d2); }
    for (int k = 0; k < 4; ++k) { oL = aL[k].process(oL); oR = aR[k].process(oR); }
    wL = oL; wR = oR;
  }
};

} // namespace rfx
