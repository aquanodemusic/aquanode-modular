// Reflex oscillator waveforms (naive, sampled), measured from the reference.
// ph in [0,1), p = pulse-width modulation amount in [0,1] (|modulation|).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "WaveTables.h"
#include "WaveTables2.h"

namespace rfx {

struct OscState {
  uint32_t rng = 0x12345678u;
  double held = 0.0;
  int lastSeg = -1;
  double y1 = 0.0;
};

#ifndef NOISE_C64
#define NOISE_C64 0.515
#define NOISE_HP 0.285
#define NOISE_W 0.349
#endif
inline double frac(double x) { return x - std::floor(x); }

// parabolic sine (odd harmonics fall as 1/k^3), peak 1 at 0.25
inline double psin(double u) {
  u = frac(u);
  return u < 0.5 ? 16.0 * u * (0.5 - u) : -16.0 * (u - 0.5) * (1.0 - u);
}
inline double sq(double u) { return frac(u) < 0.5 ? -1.0 : 1.0; }
inline double saw(double u) { return 2.0 * frac(u) - 1.0; }
inline double ajxBase(double ph) { return ph < 0.5 ? 3.0 * ph : 3.0 * ph - 3.0; }
inline bool inWin(double ph, double a, double b) { return ph >= a && ph < b; }

inline double rnd(OscState& s) { s.rng = s.rng * 1664525u + 1013904223u; return (double)(s.rng >> 8) / 8388608.0 - 1.0; }

// 2-D table lookup: linear in phase; across p linear (or nearest for shapes that switch in steps)
inline double tab2(const float (*T)[256], double ph, double p, bool nearest) {
  const double x = ph * 256.0; const int i0 = ((int)x) & 255, i1 = (i0 + 1) & 255; const double fx = x - std::floor(x);
  const double y = std::fmin(std::fmax(p, 0.0), 1.0) * 20.0;
  if (nearest) { const int r = (int)(y + 0.5); return T[r][i0] + (T[r][i1] - T[r][i0]) * fx; }
  const int r0 = std::min(19, (int)y); const double fy = y - r0;
  const double a = T[r0][i0] + (T[r0][i1] - T[r0][i0]) * fx, b = T[r0 + 1][i0] + (T[r0 + 1][i1] - T[r0 + 1][i0]) * fx;
  return a + (b - a) * fy;
}

// returns sample value in "saw units" (saw = +-1)
inline double waveValueRaw(int w, double ph, double p, double inc, bool wrapped, OscState& st) {
  switch (w) {
    case 0: return 0.0;
    case 1: return sq(ph);                                                    // Sqr
    case 2: return ph < 0.75 ? -1.0 : 1.0;                                     // Sqr2 (25% pulse)
    case 3: return saw(ph);                                                   // Saw
    case 4: return wrapped ? 2.0 : 0.0;                                       // Peak: impulse at cycle start (height 2; not zero-mean, drive shows it)
    case 5: {                                                                 // Ppwm: two impulses moving with p
      double a = 0.25 + p * 0.25, b = 0.75 - p * 0.25;
      bool hitA = ph >= a && ph - inc < a, hitB = ph >= b && ph - inc < b;
      return 2.5 * ((hitA ? 1.0 : 0.0) + (hitB ? 1.0 : 0.0));                  // height 2.5
    }
    case 9: {                                                                 // IO: accelerating ramps
      if (ph < 0.5) return 4.0 * ph - 1.0;
      if (ph < 0.75) return 8.0 * (ph - 0.5) - 1.0;
      if (ph < 0.875) return 16.0 * (ph - 0.75) - 1.0;
      return 16.0 * (ph - 0.875) - 1.0;
    }
    case 10: return 0.625 * psin(ph);                                         // Sin
    case 11: return frac(ph + p / 6.0) < 0.5 + p * 0.5 ? 1.0 : -1.0;          // pwm
    case 12: {                                                                // pw2
      bool hi = ph < 0.25 + p * 0.5 || (p < 0.5 && ph >= 0.5 && ph < 0.75 - p * 0.5);
      return hi ? 0.8 : -0.8;
    }
    case 13: {                                                                // pw3
      const double e1 = 0.125 + p / 24.0, e2 = 0.25 - p / 12.0, e3 = 0.375 - p / 12.0, e4 = 0.5 - p / 12.0, e5 = 0.75 + p / 6.0, e6 = 1.0 - p / 12.0;
      if (ph < e1) return -1; if (ph < e2) return 1; if (ph < e3) return -1; if (ph < e4) return 1;
      if (ph < e5) return -1; if (ph < e6) return 1; return -1;
    }
    case 14: {                                                                // BiSq: three levels
      if (ph < p * 0.5) return 0.0;               // plateau sits at 0 (DC found through the drive stage)
      if (ph < 0.5) return 1.0;
      if (ph < 0.5 + p * 0.5) return 0.0;
      return -1.4;
    }
    case 16: {                                                                // JXSa
      if (inWin(ph, p * 0.5, 2.0 / 3.0 - p / 6.0) || inWin(ph, 2.0 / 3.0 + p / 6.0, 1.0 - p / 6.0)) return saw(ph);
      return -1.0 - p / 3.0;
    }
    case 17: {                                                                // BiSa: two ramps with moving split
      double a = 0.5 + p / 6.0, b = 1.0 - p / 3.0;
      if (ph >= a && ph < b) return -1.0 + 2.0 * (ph - a) / (b - a);
      double len = a + (1.0 - b), t = ph >= b ? ph - b : ph + (1.0 - b);
      return -1.0 + 2.0 * t / len;
    }
    case 18: {                                                                // Sync: hard-synced slave at ratio 1/(1-p)
      double r = 1.0 / std::max(1e-3, 1.0 - p);
      return saw(ph * r);
    }
    case 19: {                                                                // AJX
      if (inWin(ph, p / 3.0, 0.5 - p / 6.0) || inWin(ph, 0.5 + p / 3.0, 1.0 - p / 6.0)) return ajxBase(ph);
      return -1.5 - p * 0.5;
    }
    case 20: {                                                                // AJX2: three windows centred on 0, 1/3, 2/3
      double hw = (1.0 - p) / 6.0;
      for (int k = 0; k < 4; ++k) { double c = k / 3.0; if (std::fabs(ph - c) < hw || (k == 0 && ph > 1.0 - hw)) return ajxBase(ph); }
      return -1.5 - p * 0.5;
    }
    case 21: {                                                                // AJX3
      if (inWin(ph, p / 12.0, 0.25 - p / 6.0) || inWin(ph, 0.25 + p / 12.0, 0.5 - p / 6.0)) return ajxBase(ph);
      if (inWin(ph, 0.5 - p / 6.0, 1.0 - p / 6.0)) return 3.0 * ph - 3.0;
      return -1.5 - p * 0.5;
    }
    case 22: {                                                                // Tri, skewed by p
      double P = 1.0 - p / 3.0, T = 0.5 + p / 6.0;
      double x = ph;
      if (x >= P) x -= 1.0;                                                   // falling part starts at P-1
      if (x < T) { double a = P - 1.0; return 1.0 - 2.0 * (x - a) / (T - a); }
      return -1.0 + 2.0 * (x - T) / (P - T);
    }
    case 23: { double L = 1.0 - p; return (L > 1e-6 && ph < L) ? 1.4 * psin(ph / L) : 0.0; }   // Puls
    case 24: return tab2(wtab2::fm2, ph, p, false);                           // FM (measured tables)
    case 25: return tab2(wtab2::am2, ph, p, false);                           // AM (measured tables)
    case 26: return 2.0 * psin(ph + p * 20.0 * psin(ph * 7.0));               // R2D2 (approx)
    case 27: return p <= 1e-6 ? 1.4 * sq(ph) : tab2(wtab2::digi2, ph, std::max(p, 0.05), true);   // Digi: square only at p=0 exactly, then stepped shapes
    case 6: return wtab::cmb2[(int)(ph * 1024) & 1023];
    case 7: return wtab::cmb3[(int)(ph * 1024) & 1023];
    case 8: return wtab::cmb4[(int)(ph * 1024) & 1023];
    case 15: return tab2(wtab2::fmsq2, ph, p, false);
    case 28: return tab2(wtab2::syns2, ph, p, false);
    case 29: {                                                                // C64: random value held for 1/8 cycle
      const int seg = (int)(ph * 8.0);
      if (seg != st.lastSeg) { st.lastSeg = seg; st.held = rnd(st); }
      return NOISE_C64 * st.held;
    }
    case 30: { const double r = rnd(st); st.y1 = 0.54 * (st.y1 + r - st.held); st.held = r; return NOISE_HP * st.y1; }   // HpNs: white noise through a ~3.9 kHz one-pole high-pass
    case 31: return NOISE_W * rnd(st);                                       // Ns: white noise
    default: return saw(ph);
  }
}

// numerically integrated mean of each deterministic shape vs p (65 steps), used to make all shapes zero-mean
struct WaveMeans {
  float m[32][65];
  WaveMeans() {
    for (int w = 0; w < 32; ++w)
      for (int k = 0; k <= 64; ++k) {
        const double p = k / 64.0;
        if (w == 0 || w == 4 || w == 5 || w == 14 || w >= 24) { m[w][k] = 0.f; continue; }   // impulses, FM-family, noise: leave as is
        OscState st; double acc = 0; const int N = 4096;
        for (int i = 0; i < N; ++i) acc += waveValueRaw(w, (i + 0.5) / N, p, 1.0 / N, i == 0, st);
        m[w][k] = (float)(acc / N);
      }
  }
};
inline double waveMean(int w, double p) {
  static const WaveMeans t;
  const double x = std::fmin(std::fmax(p, 0.0), 1.0) * 64.0;
  const int i = std::min(63, (int)x); const double f = x - i;
  return t.m[w & 31][i] * (1.0 - f) + t.m[w & 31][i + 1] * f;
}

#ifdef ORX_MEASURE
inline double& dcOverride() { static double d = 0.0; return d; }
#endif
inline double waveValue(int w, double ph, double p, double inc, bool wrapped, OscState& st) {
#ifdef ORX_MEASURE
  return waveValueRaw(w, ph, p, inc, wrapped, st) - waveMean(w, p) + dcOverride();
#else
  return waveValueRaw(w, ph, p, inc, wrapped, st) - waveMean(w, p);
#endif
}

} // namespace rfx
