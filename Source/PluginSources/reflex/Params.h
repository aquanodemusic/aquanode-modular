// Reflex - parameter model (102 raw normalized slots, identical layout to the original bank format)
#pragma once
#include <cmath>
#include <string>
#include <cstdio>
#include <algorithm>
#include "Fx.h"

namespace rfx {

enum P {
  OSC1 = 0, OSC2, OSC3, OSCfat, OSC1sm, OSC2sm, OSC3sm,
  FLTf, FLTQ, FLTtp,
  Attack, Decay, Sustain, Release, EGVol, EGPWM, EGFlt, EGDet,
  Attack2, Decay2, Sustain2, Release2, EG2Res, EG2PWM, EG2Flt, EG2Ptc,
  LFO1frq, LFO1De, LFO1PW, LFO1Fl, LFO1trg,
  LFO2frq, LFO2De, LFO2PW, LFO2Fl, LFO2trg,
  LFO3frq, LFO3De, LFO3PW, LFO3Fl, LFO3trg,
  DLYmix, DLYtme, DLYfbk, DLYdmp,
  ARPsta, ARPtrg, ARPmod, ARPtme, ARPgte, ARPoct,
  GLDst, GLDspd, PBrng, VIBspd, Voices, VIBdpt, dummy1,
  AMPdrv, AMPvol, AMPpan, AMPspd,
  TGcont, TGspd, TGpat, TGpat2, TGstr, dummy2,
  VelCut, KeyCut, VelVol, TGstat, TGmix, DLYsta, DLYmod,
  REVsta, REVmix, REVpdl, REVroo, REVdmp, REVwdt,
  OSCtrg, OSC1vl, OSC2vl, OSC3vl,
  dummy6, dummy7, dummy8, dummy9, dummy10, dummy11, dummy12, dummy13, dummy14, dummy15,
  dummy16, dummy17, dummy18, dummy19, dummy20,
  FATvcs, OSCage,
  NumParams
};

static_assert(NumParams == 102, "param count");

// Short names exactly as stored/reported by the original (used for FXB/host compatibility)
inline const char* shortName(int i) {
  static const char* n[NumParams] = {
    "OSC1","OSC2","OSC3","OSCfat","OSC1sm","OSC2sm","OSC3sm","FLT f","FLT Q","FLT tp",
    "Attack","Decay","Sustain","Release","EG>Vol","EG>PWM","EG>Flt","EG>Det",
    "Attack2","Decay2","Sustai2","Releas2","EG2>Res","EG2>PWM","EG2>Flt","EG2>Ptc",
    "LFO1frq","LFO1>De","LFO1>PW","LFO1>Fl","LFO1trg","LFO2frq","LFO2>De","LFO2>PW","LFO2>Fl","LFO2trg",
    "LFO3frq","LFO3>De","LFO3>PW","LFO3>Fl","LFO3trg","DLY mix","DLY tme","DLY fbk","DLY dmp",
    "ARP sta","ARP trg","ARP mod","ARP tme","ARP gte","ARP oct","Gld st","Gld spd","PB rng","Vib spd","Voices","vib dpt","dummy1",
    "AMP drv","AMP vol","AMP pan","AMP spd","TG cont","TG spd","TG pat","TG pat2","TG str","dummy2",
    "Vel>Cut","Key>Cut","Vel>Vol","TG stat","TG mix","DLY sta","DLY mod","REV sta","REV mix","REV pdl","REV roo","REV dmp","REV wdt",
    "OSC trg","OSC1 vl","OSC2 vl","OSC3 vl",
    "dummy6","dummy7","dummy8","dummy9","dummy10","dummy11","dummy12","dummy13","dummy14","dummy15","dummy16","dummy17","dummy18","dummy19","dummy20",
    "fat vcs","OSC age" };
  return n[i];
}

inline bool isHidden(int i) {
  return i == dummy1 || i == dummy2 || i == TGpat2 || (i >= dummy6 && i <= dummy20);
}

inline const char* waveName(int w) {
  static const char* n[32] = { "---","Sqr","Sqr2","Saw","Peak","Ppwm","Cmb2","Cmb3","Cmb4","IO","Sin","pwm","pw2","pw3","BiSq","FmSq",
    "JXSa","BiSa","Sync","AJX","AJX2","AJX3","Tri","Puls","FM","AM","R2D2","Digi","SyNs","C64","HpNs","Ns" };
  return n[std::clamp(w, 0, 31)];
}
inline const char* filterName(int t) {
  static const char* n[13] = { "LP 6","LP 12","LP 24","HP 6","HP 24","-X-","BP","Fmnt","Ntch","NtLP","-M-","Wv1","Wv2" };
  return n[std::clamp(t, 0, 12)];
}

// Tempo-synced divisions (in quarter notes) used by arp / gate / delay selectors
inline double syncDivision(int idx) {
  static const double d[27] = { 4.0, 3.0, 2.0, 4.0/3, 1.5, 1.0, 2.0/3, 0.75, 0.5, 1.0/3, 0.375, 0.25, 1.0/6, 0.1875, 0.125, 1.0/12, 0.09375, 0.0625,
                                1.0/24, 0.046875, 0.03125, 8.0, 6.0, 16.0/3, 12.0, 16.0, 32.0 };
  return d[std::clamp(idx, 0, 26)];
}
inline const char* syncName(int idx) {
  static const char* n[27] = { "1/1","1/2.","1/2","1/2T","1/4.","1/4","1/4T","1/8.","1/8","1/8T","1/16.","1/16","1/16T","1/32.","1/32","1/32T","1/64.","1/64",
                               "1/64T","1/128.","1/128","2/1","3/2","4/3","3/1","4/1","8/1" };
  return n[std::clamp(idx, 0, 26)];
}

// ---- value mappings (measured from the original's parameter display) ----
struct Map {
  static int wave(float v) { return std::clamp((int)(v * 32.f), 0, 31); }
  static int filterType(float v) { return std::clamp((int)(v * 13.f), 0, 12); }
  static int u127(float v) { return std::clamp((int)(v * 127.f + 1e-4f), 0, 127); }
  static int s127(float v) { return std::clamp((int)std::lround((v - 0.5f) * 254.f), -127, 127); }
  static int semis(float v) { return std::clamp((int)(v * 72.f - 36.f), -36, 36); }
  static double cutoffHz(double v) { return v * v * 22050.0; }
  static double envMs(double v) { return v * v * v * 4000.0; }
  static double lfoHz(double v) { return v * v * v * 60.0; }
  static double vibHz(double v) { return 1.0 + 19.0 * v; }
  static double egDetCents(double v) { return (v - 0.5) * 200.0; }
  static double eg2PitchSemis(double v) { return (v - 0.5) * 96.0; }
  static double dlyFeedback(double v) { return v * 0.95; }
  static double revPredelayMs(double v) { return v * 200.0; }
  static int voices(float v) { return std::clamp((int)(v * 32.f) + 1, 1, 32); }
  static int fatVoices(float v) { return std::clamp((int)(v * 9.f) + 2, 2, 10); }
  static int oscAge(float v) { return std::clamp((int)(v * 50.f + 1e-3f), 0, 50); }
  static bool onOff(float v) { return v >= 0.5f; }
  static int syncIdx20(float v) { return std::clamp((int)std::lround(v * 20.f), 0, 20); }
  static int arpMode(float v) { return std::clamp((int)(v * 4.99f), 0, 4); }
  static int arpOct(float v) { return std::clamp((int)(v * 3.99f), 0, 3) + 1; }
  static int pbRange(float v) { return std::clamp((int)std::lround(v * 24.f), 0, 24); }
  static int dlyMode(float v) { return std::clamp((int)(v * 3.99f), 0, 3); }
  static int dlyTimeIdx(float v) { return std::clamp((int)std::lround(v * 26.f), 0, 26); }
  static unsigned tgPattern(float v) { return (unsigned)std::clamp((long)std::lround((double)v * 65535.0), 0L, 65535L); }
};

inline const char* arpModeName(int m) { static const char* n[5] = { "Up", "Down", "Up/Dn", "Order", "Random" }; return n[std::clamp(m, 0, 4)]; }
inline const char* dlyModeName(int m) { static const char* n[4] = { "Mono", "Stereo", "PingPong", "Cross" }; return n[std::clamp(m, 0, 3)]; }

inline std::string fmtMs(double ms) {
  char b[32];
  if (ms < 1000.0) std::snprintf(b, sizeof b, "%dms", (int)ms); else std::snprintf(b, sizeof b, "%.2fs", ms / 1000.0);
  return b;
}

// Display text, formatted like the original where it showed text
inline std::string displayText(int i, float v) {
  char b[64];
  switch (i) {
    case OSC1: case OSC2: case OSC3: return waveName(Map::wave(v));
    case FLTtp: return filterName(Map::filterType(v));
    case OSC1sm: case OSC2sm: case OSC3sm: std::snprintf(b, sizeof b, "%d", Map::semis(v)); return b;
    case FLTf: {
      double f = Map::cutoffHz(v);
      if (f < 1000.0) std::snprintf(b, sizeof b, "%dHz", (int)f); else std::snprintf(b, sizeof b, "%.2fkHz", f / 1000.0);
      return b;
    }
    case FLTQ: case VIBdpt: std::snprintf(b, sizeof b, "%d%%", (int)(v * 100.f + 1e-3f)); return b;
    case Attack: case Decay: case Release: case Attack2: case Decay2: case Release2: return fmtMs(Map::envMs(v));
    case EGVol: case EGPWM: case EGFlt: case EG2Res: case EG2PWM: case EG2Flt:
    case LFO1De: case LFO1PW: case LFO1Fl: case LFO2De: case LFO2PW: case LFO2Fl: case LFO3De: case LFO3PW: case LFO3Fl:
      std::snprintf(b, sizeof b, "%d", Map::s127(v)); return b;
    case EGDet: std::snprintf(b, sizeof b, "%d cent", (int)std::lround(Map::egDetCents(v))); return b;
    case EG2Ptc: std::snprintf(b, sizeof b, "%.2f", std::round(Map::eg2PitchSemis(v) * 4.0) / 4.0); return b;
    case LFO1frq: case LFO2frq: case LFO3frq: std::snprintf(b, sizeof b, "%.2fHz", Map::lfoHz(v)); return b;
    case VIBspd: std::snprintf(b, sizeof b, "%.2fHz", Map::vibHz(v)); return b;
    case LFO1trg: case LFO2trg: case LFO3trg: case ARPsta: case GLDst: case TGstat: case DLYsta: case REVsta: case OSCtrg: case TGstr:
      return Map::onOff(v) ? "On" : "Off";
    case DLYfbk: std::snprintf(b, sizeof b, "%d%%", (int)(Map::dlyFeedback(v) * 100.0 + 1e-3)); return b;
    case REVpdl: std::snprintf(b, sizeof b, "%dms", (int)(Map::revPredelayMs(v) + 1e-3)); return b;
    case Voices: std::snprintf(b, sizeof b, "%d", Map::voices(v)); return b;
    case FATvcs: std::snprintf(b, sizeof b, "%d", Map::fatVoices(v)); return b;
    case OSCage: std::snprintf(b, sizeof b, "%d", Map::oscAge(v)); return b;
    case AMPpan: {
      int p = (int)(v * 127.f + 1e-4f) - 64;
      if (p == 0) return "<C>";
      std::snprintf(b, sizeof b, "%c%d", p < 0 ? 'L' : 'R', std::abs(p)); return b;
    }
    case VelCut: case KeyCut: case VelVol: std::snprintf(b, sizeof b, "%d%%", (int)std::lround((v - 0.5f) * 200.f)); return b;
    case ARPtme: case TGspd: return rateName(Map::syncIdx20(v));
    case DLYtme: return delayName(Map::dlyTimeIdx(v));
    case ARPmod: return arpModeName(Map::arpMode(v));
    case ARPoct: std::snprintf(b, sizeof b, "%d Oct", Map::arpOct(v)); return b;
    case ARPgte: return rateName(std::clamp((int)(v * 20.99f), 0, 20));
    case ARPtrg: return v >= 0.5f ? "Retrig" : "Free";
    case PBrng: std::snprintf(b, sizeof b, "%d st", Map::pbRange(v)); return b;
    case GLDspd: { static const char* g[8] = { "0ms", "31ms", "63ms", "125ms", "250ms", "500ms", "1s", "2s" };
                   return g[std::clamp((int)(v * 24.f), 0, 23) % 8]; }
    case DLYmod: return dlyModeName(Map::dlyMode(v));
    case TGpat: {
      unsigned m = Map::tgPattern(v); std::string s;
      for (int k = 15; k >= 0; --k) s += ((m >> k) & 1) ? '#' : '.';
      return s;
    }
    default: std::snprintf(b, sizeof b, "%d", Map::u127(v)); return b;
  }
}

// Default patch ("Init") - a plain detuned saw lead
inline void initPatch(float* p) {
  for (int i = 0; i < NumParams; ++i) p[i] = 0.f;
  p[OSC1] = 3.5f / 32; p[OSC2] = 3.5f / 32; p[OSC3] = 0.f;
  p[OSCfat] = 0.25f; p[OSC1sm] = 0.5f; p[OSC2sm] = 0.5f; p[OSC3sm] = 0.5f;
  p[FLTf] = 0.75f; p[FLTQ] = 0.15f; p[FLTtp] = 2.5f / 13;
  p[Attack] = 0.f; p[Decay] = 0.5f; p[Sustain] = 1.f; p[Release] = 0.3f; p[EGVol] = 1.f;
  p[EGPWM] = 0.5f; p[EGFlt] = 0.5f; p[EGDet] = 0.5f;
  p[Attack2] = 0.f; p[Decay2] = 0.4f; p[Sustain2] = 0.f; p[Release2] = 0.3f;
  p[EG2Res] = 0.5f; p[EG2PWM] = 0.5f; p[EG2Flt] = 0.5f; p[EG2Ptc] = 0.5f;
  for (int l = 0; l < 3; ++l) { p[LFO1frq + 5 * l] = 0.35f; p[LFO1De + 5 * l] = 0.5f; p[LFO1PW + 5 * l] = 0.5f; p[LFO1Fl + 5 * l] = 0.5f; }
  p[DLYmix] = 0.2f; p[DLYtme] = 3.f / 26; p[DLYfbk] = 0.4f; p[DLYdmp] = 0.3f; p[DLYsta] = 0.f; p[DLYmod] = 1.f / 3;
  p[ARPtme] = 2.f / 20; p[ARPgte] = 2.5f / 21; p[GLDspd] = 0.1f; p[PBrng] = 2.f / 24;
  p[VIBspd] = 0.25f; p[VIBdpt] = 0.f; p[Voices] = 15.5f / 32;
  p[AMPdrv] = 0.f; p[AMPvol] = 0.7f; p[AMPpan] = 64.5f / 127; p[AMPspd] = 0.f;
  p[TGcont] = 0.3f; p[TGspd] = 2.f / 20; p[TGpat] = (float)(0x7575 / 65535.0); p[TGmix] = 1.f;
  p[VelCut] = 0.5f; p[KeyCut] = 0.5f; p[VelVol] = 0.5f;
  p[REVmix] = 0.15f; p[REVpdl] = 0.1f; p[REVroo] = 0.6f; p[REVdmp] = 0.3f; p[REVwdt] = 0.8f; p[REVsta] = 0.f;
  p[OSC1vl] = 0.8f; p[OSC2vl] = 0.8f; p[OSC3vl] = 0.8f;
  p[dummy6] = 0.705f; p[dummy7] = 1.f; p[dummy8] = 1.f; p[dummy18] = 1.f; p[dummy12] = 0.22449f; p[dummy14] = 1.f; p[dummy15] = 1.f;
  p[FATvcs] = 0.f; p[OSCage] = 0.f;
}

} // namespace rfx
