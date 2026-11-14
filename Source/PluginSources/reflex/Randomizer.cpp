#include "Randomizer.h"
#include "PresetData.h"
#include <cmath>
#include <algorithm>

namespace rfx { namespace randomizer {

namespace pd = presetdata;

// ---------------------------------------------------------------- parameter classes
enum class Cls { Cont, Disc, Lock };

static Cls classOf(int i) {
  switch (i) {
    // hidden / engine-internal slots, master level and analog drift: always taken from the seed
    case dummy1: case dummy2: case dummy6: case dummy7: case dummy8: case dummy9: case dummy10: case dummy11:
    case dummy12: case dummy14: case dummy15: case dummy16: case dummy17: case dummy18: case dummy19: case dummy20:
    case TGpat2: case OSCage: case AMPvol:
      return Cls::Lock;
    // stepped values: waveforms, filter type, modes, sync divisions, patterns, switches, semitones, voice counts
    case OSC1: case OSC2: case OSC3: case FLTtp: case OSC1sm: case OSC2sm: case OSC3sm:
    case LFO1trg: case LFO2trg: case LFO3trg: case DLYtme: case ARPsta: case ARPtrg: case ARPmod: case ARPtme:
    case ARPgte: case ARPoct: case GLDst: case PBrng: case Voices: case TGspd: case TGpat: case TGstr:
    case TGstat: case DLYsta: case DLYmod: case REVsta: case OSCtrg: case FATvcs: case dummy13:
      return Cls::Disc;
    default:
      return Cls::Cont;
  }
}

// never changed by Mutate: anything that moves pitch, plus structure/rhythm/volume
static bool mutateLocked(int i) {
  switch (i) {
    case OSC1sm: case OSC2sm: case OSC3sm: case EG2Ptc: case EGDet: case LFO1De: case LFO2De: case LFO3De:
    case VIBspd: case VIBdpt: case PBrng: case GLDst: case GLDspd: case Voices: case dummy13:
    case ARPsta: case ARPmod: case ARPtme: case ARPoct: case ARPtrg: case TGstat: case TGspd: case DLYtme:
    case AMPvol: case OSCtrg:
      return true;
    default:
      return classOf(i) == Cls::Lock;
  }
}

// ---------------------------------------------------------------- statistics per category
struct Stats {
  std::vector<int> members[pd::NumCategories];
  float sd[pd::NumCategories][NumParams];
  float sdAll[NumParams];
  std::vector<float> waves;                       // all oscillator waveform values that are not "off"
  Stats() {
    for (int i = 0; i < pd::NumPresets; ++i) members[pd::presets[i].category].push_back(i);
    auto sdOf = [](const std::vector<int>& idx, int k) {
      if (idx.size() < 2) return 0.1f;
      double m = 0, s = 0;
      for (int i : idx) m += pd::presets[i].v[k];
      m /= (double)idx.size();
      for (int i : idx) { double d = pd::presets[i].v[k] - m; s += d * d; }
      return (float)std::sqrt(s / (double)(idx.size() - 1));
    };
    std::vector<int> all(pd::NumPresets); for (int i = 0; i < pd::NumPresets; ++i) all[(size_t)i] = i;
    for (int k = 0; k < NumParams; ++k) {
      sdAll[k] = std::max(0.02f, sdOf(all, k));
      for (int c = 0; c < pd::NumCategories; ++c) sd[c][k] = std::max(0.015f, sdOf(members[c], k));
    }
    for (int i = 0; i < pd::NumPresets; ++i)
      for (int o : { OSC1, OSC2, OSC3 })
        if (Map::wave(pd::presets[i].v[o]) != 0) waves.push_back(pd::presets[i].v[o]);
  }
};
static const Stats& stats() { static const Stats s; return s; }

// ---------------------------------------------------------------- helpers
static float uni(std::mt19937& r) { return std::uniform_real_distribution<float>(0.f, 1.f)(r); }
static float gauss(std::mt19937& r) { return std::normal_distribution<float>(0.f, 1.f)(r); }
static int pick(std::mt19937& r, int n) { return std::uniform_int_distribution<int>(0, std::max(0, n - 1))(r); }
static float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }

static Patch fromPreset(int i) { Patch p; for (int k = 0; k < NumParams; ++k) p[(size_t)k] = pd::presets[i].v[k]; return p; }

// keeps a generated patch playable: something must sound, at a sane level, with sane FX
static void guard(Patch& p, std::mt19937& r) {
  bool audible = false;
  for (int o = 0; o < 3; ++o)
    if (Map::wave(p[(size_t)(OSC1 + o)]) != 0 && p[(size_t)(OSC1vl + o)] > 0.25f) audible = true;
  if (!audible) {
    const auto& w = stats().waves;
    p[OSC1] = w.empty() ? 3.5f / 32 : w[(size_t)pick(r, (int)w.size())];
    p[OSC1vl] = std::max(p[OSC1vl], 0.7f);
  }
  p[AMPvol] = std::min(0.95f, std::max(0.3f, p[AMPvol]));
  p[dummy7] = 1.f; p[dummy8] = 1.f;                       // arp and FX sections enabled
  if (p[DLYmix] > 0.6f) p[DLYmix] = 0.6f;
  if (p[DLYfbk] > 0.85f) p[DLYfbk] = 0.85f;
  if (p[REVmix] > 0.6f) p[REVmix] = 0.6f;
  if (p[REVmix] >= 0.999f) p[REVmix] = 0.99f;
  if (p[EGVol] < 0.5f && p[Sustain] < 0.05f && p[Decay] < 0.05f) p[EGVol] = 1.f;   // would be silent
}

static const char* adjectives[] = { "Neon", "Liquid", "Silver", "Dusty", "Hollow", "Bright", "Velvet", "Rusty", "Frozen", "Golden",
  "Wired", "Glassy", "Vintage", "Electric", "Lunar", "Solar", "Acid", "Smooth", "Gritty", "Floating", "Deep", "Distant",
  "Hyper", "Dirty", "Crystal", "Analog", "Shiny", "Wobbly", "Rising", "Stormy", "Toxic", "Cosmic", "Soft", "Heavy" };
static const char* nouns[] = { "Pulse", "Signal", "Circuit", "Orbit", "Engine", "Wave", "Comet", "Mirror", "Voltage", "Echo",
  "Ribbon", "Beacon", "Horizon", "Prism", "Rotor", "Spark", "Vector", "Drift", "Machine", "Vapor", "Relay", "Nova",
  "Static", "Saucer", "Phaser", "Cascade", "Sector", "Motion", "Reactor", "Mantra" };
static const char* catWord[pd::NumCategories] = { "LD", "BS", "ACD", "ARP", "GAT", "PAD", "SYN", "ATM", "FX", "DRM" };

static std::string makeName(std::mt19937& r, const char* prefix) {
  std::string n = prefix;
  n += " "; n += adjectives[pick(r, (int)(sizeof(adjectives) / sizeof(*adjectives)))];
  n += " "; n += nouns[pick(r, (int)(sizeof(nouns) / sizeof(*nouns)))];
  return n;
}

// ---------------------------------------------------------------- generators
static Patch fromCategory(int c, float var, std::mt19937& r) {
  const auto& st = stats();
  const auto& mem = st.members[c];
  const int s = mem[(size_t)pick(r, (int)mem.size())];
  const int t = mem[(size_t)pick(r, (int)mem.size())];
  Patch p = fromPreset(s);
  if (var <= 0.001f) return p;                               // 0 %: the original preset as it is
  const float w = var * uni(r) * 0.6f;                      // how far toward the partner preset
  const float pSwap = var * 0.35f;                           // chance to take a stepped value from elsewhere
  for (int k = 0; k < NumParams; ++k) {
    const float a = pd::presets[s].v[k], b = pd::presets[t].v[k];
    switch (classOf(k)) {
      case Cls::Lock: break;
      case Cls::Cont: p[(size_t)k] = clamp01(a + w * (b - a) + gauss(r) * var * 0.5f * st.sd[c][k]); break;
      case Cls::Disc:
        if (uni(r) < pSwap) p[(size_t)k] = uni(r) < 0.7f ? b : pd::presets[mem[(size_t)pick(r, (int)mem.size())]].v[k];
        break;
    }
  }
  // a switched-off oscillator stays off unless its level is up (keeps the seed's voicing)
  guard(p, r);
  return p;
}

static Patch superRandom(std::mt19937& r) {
  Patch p = fromPreset(pick(r, pd::NumPresets));            // locked slots and sensible defaults come from a real preset
  const auto& w = stats().waves;
  for (int k = 0; k < NumParams; ++k) {
    if (classOf(k) == Cls::Lock) continue;
    p[(size_t)k] = uni(r);
  }
  for (int o = 0; o < 3; ++o) p[(size_t)(OSC1 + o)] = uni(r) < 0.25f && o > 0 ? 0.f : w[(size_t)pick(r, (int)w.size())];
  for (int o = 0; o < 3; ++o) p[(size_t)(OSC1sm + o)] = (std::round(uni(r) * 2.f) * 12.f + 36.f - 12.f + 0.5f) / 72.f;   // -12/0/+12 st
  p[PBrng] = 2.f / 24; p[Voices] = 15.5f / 32; p[dummy13] = 0.f;
  p[Attack] *= 0.6f; p[Release] *= 0.7f; p[Attack2] *= 0.7f;
  p[ARPsta] = uni(r) < 0.2f ? 1.f : 0.f; p[TGstat] = uni(r) < 0.25f ? 1.f : 0.f; p[GLDst] = uni(r) < 0.15f ? 1.f : 0.f;
  p[DLYmix] *= 0.5f; p[REVmix] *= 0.5f; p[AMPdrv] *= 0.7f; p[EGVol] = 0.75f + 0.25f * uni(r);
  p[LFO1De] = p[LFO2De] = p[LFO3De] = 0.5f + (uni(r) - 0.5f) * 0.3f;   // gentle pitch modulation only
  p[EG2Ptc] = 0.5f + (uni(r) - 0.5f) * 0.2f;
  guard(p, r);
  return p;
}

static Patch mutate(const Patch& cur, float var, std::mt19937& r) {
  const auto& st = stats();
  Patch p = cur;
  const float amt = 0.05f + 0.95f * var;
  for (int k = 0; k < NumParams; ++k) {
    if (mutateLocked(k)) continue;
    if (classOf(k) == Cls::Cont) {
      p[(size_t)k] = clamp01(cur[(size_t)k] + gauss(r) * amt * 0.45f * st.sdAll[k]);
    }
  }
  // occasional changes of stepped sound parameters
  for (int o = 0; o < 3; ++o)
    if (Map::wave(cur[(size_t)(OSC1 + o)]) != 0 && uni(r) < 0.15f * amt)
      p[(size_t)(OSC1 + o)] = st.waves[(size_t)pick(r, (int)st.waves.size())];
  if (uni(r) < 0.1f * amt) p[FLTtp] = pd::presets[pick(r, pd::NumPresets)].v[FLTtp];
  if (Map::onOff(cur[TGstat])) {                            // flip a few gate steps
    unsigned m = Map::tgPattern(cur[TGpat]);
    for (int s = 0; s < 16; ++s) if (uni(r) < 0.12f * amt) m ^= 1u << s;
    p[TGpat] = (float)(m / 65535.0);
  }
  guard(p, r);
  p[AMPvol] = cur[AMPvol];
  return p;
}

static Patch blend(std::mt19937& r) {
  const int a = pick(r, pd::NumPresets), b = pick(r, pd::NumPresets);
  const float w = 0.3f + 0.4f * uni(r);
  Patch p = fromPreset(a);
  for (int k = 0; k < NumParams; ++k) {
    const float x = pd::presets[a].v[k], y = pd::presets[b].v[k];
    switch (classOf(k)) {
      case Cls::Lock: break;
      case Cls::Cont: p[(size_t)k] = x + w * (y - x); break;
      case Cls::Disc: p[(size_t)k] = uni(r) < w ? y : x; break;
    }
  }
  guard(p, r);
  return p;
}

// ---------------------------------------------------------------- public
const std::vector<MenuEntry>& menu() {
  static const std::vector<MenuEntry> m = [] {
    std::vector<MenuEntry> v;
    for (int c = 0; c < pd::NumCategories; ++c) v.push_back({ pd::categoryNames[c], Kind::Category, c, c == 0 ? "Common Preset Type" : "" });
    v.push_back({ "Anything", Kind::Anything, -1, "Surprise" });
    v.push_back({ "Super Random", Kind::SuperRandom, -1, "" });
    v.push_back({ "Mutate current", Kind::Mutate, -1, "Variations" });
    v.push_back({ "Blend two presets", Kind::Blend, -1, "" });
    return v;
  }();
  return m;
}

Patch generate(int menuIndex, const Patch& current, float variation, std::mt19937& rng, std::string& name) {
  const auto& m = menu();
  menuIndex = std::min((int)m.size() - 1, std::max(0, menuIndex));
  variation = std::min(1.f, std::max(0.f, variation));
  const auto& e = m[(size_t)menuIndex];
  switch (e.kind) {
    case Kind::Category: name = makeName(rng, catWord[e.category]); return fromCategory(e.category, variation, rng);
    case Kind::Anything: { const int c = pick(rng, pd::NumCategories); name = makeName(rng, catWord[c]); return fromCategory(c, variation, rng); }
    case Kind::SuperRandom: name = makeName(rng, "RND"); return superRandom(rng);
    case Kind::Mutate: name.clear(); return mutate(current, variation, rng);
    case Kind::Blend: name = makeName(rng, "MIX"); return blend(rng);
  }
  return current;
}

} }
