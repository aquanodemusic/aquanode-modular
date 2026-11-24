// Vita in Aquanode Modular (6.9.12): its own wavetables - see vita_wavetables.h
#include "vita_wavetables.h"
#include "synth_constants.h"
#include "../../aquamassif/massif/Tables.h"

#include <cmath>

namespace VitaWavetables {
  namespace {
    namespace mg = massif::gen;
    constexpr int kSize = massif::kFrameSize;   // 2048, as Vita's frames
    constexpr double kTau = 6.283185307179586;

    uint32_t mix(uint32_t s) {
      s ^= s >> 16; s *= 0x7feb352du; s ^= s >> 15; s *= 0x846ca68bu; s ^= s >> 16;
      return s ? s : 1u;
    }
    double rand01(uint32_t& s) { return mg::rnd01(s); }

    // the kinds Vita adds to Massif's recipes
    enum OwnKind { kMovingSaws, kFilterSweep, kPhaseDistortion, kMouthHarp, kDronePipe, kNoise, kNumOwn };

    struct Kind { std::string name; int recipe; int own; int phizmo = -100; };   // phizmo: its archetype, -1 any
    const std::vector<Kind>& kinds() {
      static const std::vector<Kind> k = [] {
        std::vector<Kind> v = {
        { "Shape Morph", mg::rcShapes, -1 },          { "Pulse Width", mg::rcPwm, -1 },
        { "Sync Sweep", mg::rcSync, -1 },             { "Harmonic Sweep", mg::rcHarmonicSweep, -1 },
        { "Moving Saws", -1, kMovingSaws },           { "Filter Sweep", -1, kFilterSweep },
        { "Phase Distortion", -1, kPhaseDistortion }, { "FM", mg::rcFm, -1 },
        { "Wavefolder", mg::rcFold, -1 },             { "Vowels", mg::rcVowels, -1 },
        { "Organ Stops", mg::rcOrgan, -1 },           { "Additive Dice", mg::rcAdditive, -1 },
        { "Spectral Cloud", mg::rcSpectral, -1 },     { "Bit Crush", mg::rcCrush, -1 },
        { "Mouth Harp", -1, kMouthHarp },             { "Drone Pipe", -1, kDronePipe },
        { "Noise", -1, kNoise },
        };
        // (6.9.12) Phizmo's own generator: its Transwaves, any kind or one
        v.push_back({ "Phizmo: Any", -1, -1, -1 });
        for (int a = 0; a < ::aqp_phizmo::phizmoArchetypeCount(); ++a)
          v.push_back({ std::string("Phizmo: ") + ::aqp_phizmo::phizmoArchetypeName(a), -1, -1, a });
        return v;
      }();
      return k;
    }

    void addFrame(std::vector<float>& out, const std::vector<float>& f) { out.insert(out.end(), f.begin(), f.end()); }

    // Vita's own sweeps: a, b, c 0..1 as Massif's recipes take them
    std::vector<float> ownRecipe(int own, float a, float b, float c, uint32_t seed, int frames) {
      std::vector<float> v;
      v.reserve((size_t)frames * kSize);
      uint32_t rs = mix(seed + 17u * (uint32_t)own);
      std::vector<float> f;
      switch (own) {
        case kMovingSaws: {
          // a saw whose harmonics each drift in phase at their own pace over the table: scanning it
          // moves like several saws beating against each other (a: how far, b: brightness, c: odd/even)
          std::vector<double> drift(kSize / 2);
          for (auto& d : drift) d = rand01(rs) * 2.0 - 1.0;
          const double spread = 0.5 + 3.5 * a, tilt = 0.6 + 0.8 * (1.0 - b);
          for (int k = 0; k < frames; ++k) {
            const double t = frames > 1 ? (double)k / (frames - 1) : 0.0;
            mg::additive(f, [&](int h) { return (h % 2 == 0 ? 1.0 - 0.7 * c : 1.0) / std::pow((double)h, tilt); },
                         [&](int h) { return drift[(size_t)h] * spread * kTau * t * std::sqrt((double)h); });
            addFrame(v, f);
          }
          break;
        }
        case kFilterSweep: {
          // a saw (or a square) through a resonant low-pass whose cutoff opens over the table
          const bool square = c > 0.5f;
          const double reso = 1.0 + 6.0 * a, top = 24.0 + 200.0 * b;
          for (int k = 0; k < frames; ++k) {
            const double t = frames > 1 ? (double)k / (frames - 1) : 0.0;
            const double fc = 1.5 * std::pow(top / 1.5, t);
            mg::additive(f, [&](int h) {
              if (square && h % 2 == 0) return 0.0;
              const double x = h / fc;
              const double lp = 1.0 / std::sqrt(1.0 + std::pow(x, 8.0));
              const double peak = 1.0 + (reso - 1.0) * std::exp(-std::pow(std::log(x) * 3.0, 2.0));
              return lp * peak / h;
            });
            addFrame(v, f);
          }
          break;
        }
        case kPhaseDistortion: {
          // a cosine whose phase is bent at a knee that moves over the table (a: how far, b: a second
          // bend, c: resonant window)
          f.assign(kSize, 0.0f);
          for (int k = 0; k < frames; ++k) {
            const double t = frames > 1 ? (double)k / (frames - 1) : 0.0;
            const double knee = 0.5 - 0.48 * (0.3 + 0.7 * a) * t;
            for (int i = 0; i < kSize; ++i) {
              const double ph = (double)i / kSize;
              double w = ph < knee ? 0.5 * ph / knee : 0.5 + 0.5 * (ph - knee) / (1.0 - knee);
              if (b > 0.3f) w = w + (b - 0.3) * 0.5 * std::sin(kTau * 2.0 * w) * t;
              double y = -std::cos(kTau * w);
              if (c > 0.5f) y = std::sin(kTau * w * (1.0 + 6.0 * t * c)) * (1.0 - ph);
              f[(size_t)i] = (float)y;
            }
            addFrame(v, f);
          }
          break;
        }
        case kMouthHarp: {
          // a buzzing reed: every harmonic, and one narrow resonance the mouth moves from about the
          // 42nd harmonic down to the 13th over the table (measured on a mouth harp)
          const double q = 0.06 + 0.1 * (1.0 - a), from = 42.0 + 10.0 * (b - 0.5), to = 13.0 + 6.0 * (c - 0.5);
          for (int k = 0; k < frames; ++k) {
            const double t = frames > 1 ? (double)k / (frames - 1) : 0.0;
            const double centre = from * std::pow(to / from, t);
            mg::additive(f, [&](int h) {
              const double d = std::log((double)h / centre);
              return (0.25 / std::pow((double)h, 0.7) + 2.5 * std::exp(-0.5 * d * d / (q * q)) / std::sqrt((double)h))
                     + (h == 1 ? 0.6 : 0.0);
            });
            addFrame(v, f);
          }
          break;
        }
        case kDronePipe: {
          // a long pipe's drone: resonances near the 3rd and the 7th-8th harmonics (measured on a
          // drone pipe), a little nasal dip between, drifting over the table
          for (int k = 0; k < frames; ++k) {
            const double t = frames > 1 ? (double)k / (frames - 1) : 0.0;
            const double r1 = 3.0 * (1.0 + 0.15 * a * std::sin(kTau * t)), r2 = 7.6 * (1.0 + 0.25 * b * std::sin(kTau * t * 1.5 + 1.0));
            const double grit = 0.3 * c * t;
            mg::additive(f, [&](int h) {
              const double x = (double)h;
              const double f1 = std::exp(-0.5 * std::pow((x - r1) / 1.1, 2.0)), f2 = std::exp(-0.5 * std::pow((x - r2) / 1.8, 2.0));
              return (0.6 / x + 1.2 * f1 + 0.8 * f2 + grit / std::sqrt(x)) * (h % 2 == 0 ? 0.8 : 1.0);
            }, [&](int h) { return 0.3 * h * h * t * c; });
            addFrame(v, f);
          }
          break;
        }
        default: {   // kNoise: every frame its own random phases, white, pink or brown
          const double slope = a < 0.33f ? 0.0 : (a < 0.66f ? 0.5 : 1.0);
          for (int k = 0; k < frames; ++k) {
            uint32_t fs = mix(rs + 7919u * (uint32_t)k);
            mg::additive(f, [&](int h) { return h < 2 + (int)(b * 6.0) ? 0.0 : 1.0 / std::pow((double)h, slope); },
                         [&](int) { return kTau * rand01(fs); });
            addFrame(v, f);
          }
          break;
        }
      }
      mg::normalize(v);
      return v;
    }

    std::vector<float> frameData(int kind, float a, float b, float c, uint32_t seed, int frames) {
      const auto& k = kinds()[(size_t)juce::jlimit(0, (int)kinds().size() - 1, kind)];
      if (k.own >= 0)
        return ownRecipe(k.own, a, b, c, seed, frames);
      return mg::recipe(k.recipe, a, b, c, seed, frames);
    }

    struct Default { const char* name; int kind; float a, b, c; uint32_t seed; };
    const std::vector<Default>& defaults() {
      static const std::vector<Default> d = {
        { "Basic Shapes", 0, 0.8f, 0.0f, 0.0f, 1 },       { "Pulse Width", 1, 0.9f, 0.0f, 0.0f, 1 },
        { "Soft Pulse", 1, 0.6f, 0.3f, 0.7f, 2 },         { "Sync Sweep", 2, 0.5f, 0.0f, 0.0f, 1 },
        { "Harmonic Series", 3, 0.4f, 0.5f, 0.3f, 1 },    { "Moving Saws", 4, 0.5f, 0.6f, 0.0f, 3 },
        { "Low-Pass Saw", 5, 0.4f, 0.6f, 0.0f, 1 },       { "Low-Pass Square", 5, 0.6f, 0.5f, 1.0f, 2 },
        { "Phase Bend", 6, 0.8f, 0.0f, 0.0f, 1 },         { "Resonant Bend", 6, 0.6f, 0.5f, 1.0f, 2 },
        { "FM Bell", 7, 0.7f, 0.5f, 0.1f, 1 },            { "Folded Sine", 8, 0.6f, 0.2f, 0.4f, 1 },
        { "Vowels", 9, 0.5f, 0.6f, 0.2f, 1 },             { "Organ Stops", 10, 0.6f, 0.4f, 0.2f, 1 },
        { "Mouth Harp", 14, 0.5f, 0.5f, 0.5f, 1 },        { "Drone Pipe", 15, 0.5f, 0.5f, 0.3f, 1 },
        { "White Noise", 16, 0.1f, 0.0f, 0.0f, 1 },       { "Pink Noise", 16, 0.5f, 0.0f, 0.0f, 2 },
        { "Brown Noise", 16, 0.9f, 0.0f, 0.0f, 3 },
      };
      return d;
    }
  }

  // (6.9.14) a frame whose peaks stand far above its loudness (a pulse train with every
  // harmonic in step - the Vowels) gets each harmonic's phase spread (Newman's phases): the
  // same harmonics at the same levels, the peaks lower, so it can be as loud as the others
  void spreadPhases(float* x) {
    static juce::dsp::FFT fft(11);   // 2048
    std::vector<float> d(2 * kSize, 0.0f);
    std::copy(x, x + kSize, d.begin());
    fft.performRealOnlyForwardTransform(d.data(), true);
    for (int h = 1; h < kSize / 2; ++h) {
      const float re = d[(size_t)(2 * h)], im = d[(size_t)(2 * h + 1)];
      const float mag = std::sqrt(re * re + im * im);
      const double ph = juce::MathConstants<double>::pi * (double)h * (double)h / (double)(kSize / 2);
      d[(size_t)(2 * h)] = mag * (float)std::cos(ph);
      d[(size_t)(2 * h + 1)] = mag * (float)std::sin(ph);
    }
    d[(size_t)kSize] = d[(size_t)kSize + 1] = 0.0f;   // (Nyquist)
    std::vector<float> mags((size_t)(kSize / 2), 0.0f);
    for (int h = 1; h < kSize / 2; ++h)
      mags[(size_t)h] = std::sqrt(d[(size_t)(2 * h)] * d[(size_t)(2 * h)] + d[(size_t)(2 * h + 1)] * d[(size_t)(2 * h + 1)]);
    fft.performRealOnlyInverseTransform(d.data());
    // a few rounds of: clip the highest peaks, then put every harmonic's level back (its phase
    // kept) - the peaks come down further, the harmonics stay as they were
    for (int round = 0; round < 12; ++round) {
      double e2 = 0.0;
      for (int i = 0; i < kSize; ++i) e2 += (double)d[(size_t)i] * d[(size_t)i];
      const float lim = 2.0f * (float)std::sqrt(e2 / kSize);
      for (int i = 0; i < kSize; ++i) d[(size_t)i] = juce::jlimit(-lim, lim, d[(size_t)i]);
      std::fill(d.begin() + kSize, d.end(), 0.0f);
      fft.performRealOnlyForwardTransform(d.data(), true);
      d[0] = d[1] = 0.0f;
      for (int h = 1; h < kSize / 2; ++h) {
        const float re = d[(size_t)(2 * h)], im = d[(size_t)(2 * h + 1)];
        const float m = std::sqrt(re * re + im * im);
        const float k = m > 1.0e-12f ? mags[(size_t)h] / m : 0.0f;
        d[(size_t)(2 * h)] = re * k;
        d[(size_t)(2 * h + 1)] = im * k;
      }
      d[(size_t)kSize] = d[(size_t)kSize + 1] = 0.0f;
      fft.performRealOnlyInverseTransform(d.data());
    }
    std::copy(d.begin(), d.begin() + kSize, x);
  }

  // (6.9.15) the frames as every table gets them: peaky ones phase-spread, each frame brought
  // to an RMS of 0.45 with its peak never over 0.98 (for Vita and Aquanode's Wavetable alike)
  std::vector<float> prepareFrames(const std::vector<float>& frames) {
    const int n = (int)(frames.size() / kSize);
    std::vector<float> out((size_t)n * kSize, 0.0f);
    // (6.9.14) peaky tables have their phases spread, frame by frame alike (so they still morph)
    bool peaky = false;
    for (int k = 0; k < n && ! peaky; ++k) {
      float pk = 1.0e-9f; double e2 = 0.0;
      for (int i = 0; i < kSize; ++i) { const float x = frames[(size_t)k * kSize + (size_t)i]; pk = std::max(pk, std::abs(x)); e2 += (double)x * x; }
      const float r = (float)std::sqrt(e2 / kSize) + 1.0e-9f;
      peaky = pk / r > 0.98f / 0.45f * 1.6f;   // (more than ~3.5: the peak cap would leave it 4 dB short or more)
    }
    std::vector<float> spread;
    if (peaky) {
      spread = frames;
      for (int k = 0; k < n; ++k) spreadPhases(spread.data() + (size_t)k * kSize);
    }
    const std::vector<float>& src = peaky ? spread : frames;
    for (int k = 0; k < n; ++k) {
      // every frame towards the same loudness (an RMS of 0.45), its peak never over 0.98
      float peak = 1.0e-9f;
      double e = 0.0;
      for (int i = 0; i < kSize; ++i) {
        const float x = src[(size_t)k * kSize + (size_t)i];
        peak = std::max(peak, std::abs(x));
        e += (double)x * x;
      }
      const float rms = (float)std::sqrt(e / kSize) + 1.0e-9f;
      const float g = std::min(0.45f / rms, 0.98f / peak);
      for (int i = 0; i < kSize; ++i)
        out[(size_t)k * kSize + (size_t)i] = src[(size_t)k * kSize + (size_t)i] * g;
    }
    return out;
  }

  json fromFrames(const std::string& name, const std::vector<float>& rawFrames) {
    const std::vector<float> frames = prepareFrames(rawFrames);
    const int n = (int)(frames.size() / kSize);
    json keyframes = json::array();
    for (int k = 0; k < n; ++k) {
      const int position = n > 1 ? (int)std::lround((double)k * (vital::kNumOscillatorWaveFrames - 1) / (n - 1)) : 0;
      json key;
      key["position"] = position;
      key["wave_data"] = Base64::toBase64(frames.data() + (size_t)k * kSize, sizeof(float) * (size_t)kSize).toStdString();
      keyframes.push_back(key);
    }
    json component;
    component["type"] = "Wave Source";
    component["interpolation_style"] = 1;   // linear between keyframes
    component["interpolation"] = 0;         // in time, as Massif crossfades its frames
    component["keyframes"] = keyframes;
    json group;
    group["components"] = json::array({ component });
    json table;
    table["name"] = name;
    table["author"] = "Aquanode";
    table["version"] = ProjectInfo::versionString;
    table["remove_all_dc"] = true;
    table["full_normalize"] = false;
    table["groups"] = json::array({ group });
    return table;
  }

  const std::vector<std::string>& generatorKinds() {
    static const std::vector<std::string> names = [] {
      std::vector<std::string> v;
      for (const auto& k : kinds())
        v.push_back(k.name);
      return v;
    }();
    return names;
  }

  json generate(int kind, uint32_t seed) {
    std::string name;
    auto frames = generateFrames(kind, seed, name);
    return frames.empty() ? json() : fromFrames(name, frames);
  }

  std::vector<float> generateFrames(int kind, uint32_t seed, std::string& name) {
    const auto& kd = kinds()[(size_t)juce::jlimit(0, (int)kinds().size() - 1, kind)];
    if (kd.phizmo > -100) {
      // a Transwave of Phizmo's generator, at Vita's frame length
      juce::Random rng((juce::int64)mix(seed) * 7919 + seed);
      int arch = kd.phizmo;
      if (arch < 0)
        arch = rng.nextInt(::aqp_phizmo::phizmoArchetypeCount());
      std::vector<std::vector<float>> frames;
      juce::String label, status;
      if (!::aqp_phizmo::phizmoBuildTranswave(arch, (juce::int64)seed, rng, 0, kSize, frames, label, status) || frames.empty())
        return {};
      std::vector<float> flat;
      flat.reserve(frames.size() * kSize);
      for (auto& f : frames) {
        f.resize(kSize, 0.0f);
        flat.insert(flat.end(), f.begin(), f.end());
      }
      // the archetype's whole name (Phizmo's own label is cut to fit its display) and the seed
      name = std::string(::aqp_phizmo::phizmoArchetypeName(arch)) + " " + std::to_string(1000 + (int)(seed % 9000u));
      return flat;
    }
    uint32_t rs = mix(seed);
    const float a = (float)(0.1 + 0.8 * rand01(rs)), b = (float)(0.1 + 0.8 * rand01(rs)), c = (float)rand01(rs);
    const int frames = 16 + (int)(rand01(rs) * 48.0);
    const auto& k = kinds()[(size_t)juce::jlimit(0, (int)kinds().size() - 1, kind)];
    name = k.name + " " + std::to_string(1000 + (int)(seed % 9000u));
    return frameData(kind, a, b, c, rs, frames);
  }

  const std::vector<std::string>& defaultNames() {
    static const std::vector<std::string> names = [] {
      std::vector<std::string> v;
      for (const auto& d : defaults())
        v.push_back(d.name);
      return v;
    }();
    return names;
  }

  json defaultTable(int index) {
    std::string name;
    auto frames = defaultFrames(index, name);
    return fromFrames(name, frames);
  }

  std::vector<float> defaultFrames(int index, std::string& name) {
    const auto& d = defaults()[(size_t)juce::jlimit(0, (int)defaults().size() - 1, index)];
    const int frames = d.kind == 0 ? 9 : 32;
    name = d.name;
    return frameData(d.kind, d.a, d.b, d.c, d.seed, frames);
  }

  int massifCount() { return (int)massif::slotInfos().size(); }

  std::string massifName(int slot) {
    return massif::slotInfos()[(size_t)juce::jlimit(0, massifCount() - 1, slot)].name;
  }

  File massifFile(int slot) {
    // where Aqua Massif looks for the user's own table files (its folder setting aside)
    const File docs = File::getSpecialLocation(File::userDocumentsDirectory);
    const File app = File::getSpecialLocation(File::userApplicationDataDirectory);
    for (const File& dir : { app.getChildFile("Massif").getChildFile("Wavetables"),
                             docs.getChildFile("Massif").getChildFile("Wavetables"), docs.getChildFile("Massif"),
                             docs.getChildFile("Aqua Massif").getChildFile("Wavetables"), docs.getChildFile("Aqua Massif") }) {
      if (!dir.isDirectory())
        continue;
      for (const File& f : dir.findChildFiles(File::findFiles, true, "*.wav;*.WAV;*.flac;*.FLAC"))
        if (massif::Bank::slotForFile(f.getFileName().toStdString()) == slot)
          return f;
    }
    return File();
  }

  std::vector<float> massifFrames(int slot) {
    return massif::gen::forSlot(juce::jlimit(0, massifCount() - 1, slot));
  }

  json massifStandIn(int slot) {
    return fromFrames(massifName(slot), massifFrames(slot));
  }
}
