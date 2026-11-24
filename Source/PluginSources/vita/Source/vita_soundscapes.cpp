// Vita in Aquanode Modular: the built-in Aquanode Soundscapes (see vita_soundscapes.h)
#include "vita_soundscapes.h"

extern "C" const unsigned char* aqVitaSoundscapesData (size_t* size);

#include <map>

namespace VitaSoundscapes {
  namespace {
    constexpr int kN = 2048;

    struct Pack {
      bool ok = false;
      juce::MemoryBlock raw;
      nlohmann::json index;
      size_t presets = 0, waves = 0, shaped = 0;   // where each section starts in raw
      std::vector<std::string> names, cats;
    };

    uint32_t readU32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
    float readF32(const unsigned char* p) { uint32_t u = readU32(p); float f; std::memcpy(&f, &u, 4); return f; }

    const Pack& pack() {
      static const Pack p = [] {
        Pack k;
        size_t size = 0;
        const unsigned char* data = aqVitaSoundscapesData(&size);
        juce::MemoryInputStream in(data, size, false);
        juce::GZIPDecompressorInputStream unzip(&in, false, juce::GZIPDecompressorInputStream::zlibFormat);
        unzip.readIntoMemoryBlock(k.raw);
        const unsigned char* b = (const unsigned char*)k.raw.getData();
        if (k.raw.getSize() < 24 || std::memcmp(b, "AQVS", 4) != 0)
          return k;
        size_t head = readU32(b + 8), presets = readU32(b + 12), waves = readU32(b + 16);
        try {
          k.index = nlohmann::json::parse(std::string((const char*)b + 24, head));
        }
        catch (...) { return k; }
        k.presets = 24 + head;
        k.waves = k.presets + presets;
        k.shaped = k.waves + waves;
        for (auto& e : k.index["presets"]) {
          k.names.push_back(e["n"].get<std::string>());
          k.cats.push_back(e["c"].get<std::string>());
        }
        k.ok = true;
        return k;
      }();
      return p;
    }

    struct Rng {
      uint32_t s;
      explicit Rng(uint32_t seed) : s(seed | 1u) { }
      float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)((double)s / 4294967296.0 * 2.0 - 1.0); }
    };

    std::string pcmBase64(const std::vector<float>& x) {
      std::vector<int16_t> q(x.size());
      for (size_t i = 0; i < x.size(); ++i)
        q[i] = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, x[i])) * 32767.0f);
      return juce::Base64::toBase64(q.data(), q.size() * sizeof(int16_t)).toStdString();
    }

    void scaleToStd(std::vector<float>& x, float target) {
      double m = 0, e = 0;
      for (float v : x) m += v;
      m /= std::max<size_t>(1, x.size());
      for (float& v : x) { v -= (float)m; e += v * v; }
      float sd = (float)std::sqrt(e / std::max<size_t>(1, x.size()));
      if (sd > 0) for (float& v : x) v *= target / sd;
    }

    // white (uniform +-0.9), pink (-3 dB an octave), brown (-6 dB), one sine cycle
    std::vector<float> generate(char kind, int n, uint32_t seed) {
      std::vector<float> x((size_t)std::max(0, n));
      Rng r(seed);
      if (kind == 's') {
        for (int i = 0; i < n; ++i) x[(size_t)i] = (float)std::sin(2.0 * 3.141592653589793 * i / std::max(1, n));
        return x;
      }
      if (kind == 'w') {
        for (auto& v : x) v = 0.9f * r.next();
        return x;
      }
      float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, br = 0;
      for (auto& v : x) {
        float w = r.next();
        if (kind == 'p') {
          b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
          b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
          v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f; b6 = w * 0.115926f;
        }
        else { br = 0.998f * br + w; v = br; }
      }
      scaleToStd(x, kind == 'p' ? 0.205f : 0.27f);
      return x;
    }

    // a drawn frame from the store: 16 bit ('T') or harmonics ('S')
    std::string waveBase64(const Pack& k, int id) {
      std::vector<float> y(kN, 0.0f);
      const auto& offs = k.index["waves"];
      if (id >= 0 && id < (int)offs.size()) {
        const unsigned char* p = (const unsigned char*)k.raw.getData() + k.waves + offs[(size_t)id].get<size_t>();
        if (p[0] == 0) {
          float scale = readF32(p + 1);
          for (int i = 0; i < kN; ++i) {
            int16_t q = (int16_t)(p[5 + 2 * i] | (p[6 + 2 * i] << 8));
            y[(size_t)i] = q / 32767.0f * scale;
          }
        }
        else {
          float mx = readF32(p + 1);
          int H = p[5] | (p[6] << 8);
          bool hasPhase = p[7] != 0;
          const unsigned char* mag = p + 8;
          const unsigned char* ph = mag + H;
          juce::dsp::FFT fft(11);
          std::vector<std::complex<float>> in(kN), out(kN);
          for (int h = 1; h <= H && h < kN / 2; ++h) {
            int q = mag[h - 1];
            if (q == 0) continue;
            float m = mx * std::pow(10.0f, (q * 0.5f - 120.0f) / 20.0f);
            float phase = hasPhase ? ph[h - 1] / 256.0f * 6.2831853f - 3.1415927f : (float)std::fmod(h * 2.39996323, 6.283185307179586);
            in[(size_t)h] = std::polar(0.5f * m, phase);
            in[(size_t)(kN - h)] = std::conj(in[(size_t)h]);
          }
          fft.perform(in.data(), out.data(), true);
          for (int i = 0; i < kN; ++i) y[(size_t)i] = out[(size_t)i].real() * kN;
        }
      }
      return juce::Base64::toBase64(y.data(), y.size() * sizeof(float)).toStdString();
    }

    // a stand-in: noise with the original's band levels, every hop samples
    std::vector<float> shapedNoise(const Pack& k, int id, int n, uint32_t seed) {
      std::vector<float> out((size_t)std::max(0, n), 0.0f);
      const auto& offs = k.index["shaped"];
      if (id < 0 || id >= (int)offs.size()) return out;
      const unsigned char* p = (const unsigned char*)k.raw.getData() + k.shaped + offs[(size_t)id].get<size_t>();
      int frames = (int)readU32(p), hop = (int)readU32(p + 4);
      float rate = readF32(p + 8);
      const unsigned char* bands = p + 12;
      float edges[25];
      for (int b = 0; b <= 24; ++b) edges[b] = 20.0f * std::pow(rate / 2.0f / 20.0f, b / 24.0f);
      int bandOf[kN / 2 + 1];
      for (int i = 0; i <= kN / 2; ++i) {
        float f = i * rate / kN; bandOf[i] = -1;
        for (int b = 0; b < 24; ++b) if (f >= edges[b] && f < edges[b + 1]) { bandOf[i] = b; break; }
      }
      juce::dsp::FFT fft(11);
      std::vector<std::complex<float>> in(kN), sp(kN);
      Rng r(seed);
      const float gain = 1.886f / kN;
      for (int fr = 0; fr < frames; ++fr) {
        std::fill(in.begin(), in.end(), std::complex<float>());
        for (int i = 1; i < kN / 2; ++i) {
          int b = bandOf[i];
          if (b < 0) continue;
          float db = bands[fr * 24 + b] * 0.5f - 100.0f;
          float m = std::sqrt(std::pow(10.0f, db / 10.0f));
          in[(size_t)i] = std::polar(m, 3.1415927f * r.next());
          in[(size_t)(kN - i)] = std::conj(in[(size_t)i]);
        }
        fft.perform(in.data(), sp.data(), true);
        int start = fr * hop - kN / 2;
        for (int i = 0; i < kN; ++i) {
          int at = start + i;
          if (at < 0 || at >= n) continue;
          float w = 0.5f - 0.5f * std::cos(6.2831853f * i / kN);
          out[(size_t)at] += sp[(size_t)i].real() * kN * gain * w;
        }
      }
      return out;
    }

    void rebuild(const Pack& k, nlohmann::json& o) {
      if (o.is_object() || o.is_array()) {
        for (auto& v : o) rebuild(k, v);
        return;
      }
      if (!o.is_string()) return;
      const std::string& s = o.get_ref<const std::string&>();
      if (s.size() < 3 || s[0] != '@') return;
      if (s[1] == 'w') { o = waveBase64(k, std::atoi(s.c_str() + 2)); return; }
      // "@g<kind>,<n>,<seed>" / "@s<id>,<n>,<seed>"
      int a = 0, n = 0; unsigned seed = 1;
      if (s[1] == 'g') {
        std::sscanf(s.c_str() + 3, "%d,%u", &n, &seed);
        o = pcmBase64(generate(s[2], n, seed));
      }
      else if (s[1] == 's') {
        std::sscanf(s.c_str() + 2, "%d,%d,%u", &a, &n, &seed);
        o = pcmBase64(shapedNoise(k, a, n, seed));
      }
    }
  }

  int count() { return (int)pack().names.size(); }
  std::string name(int index) { const auto& k = pack(); return index >= 0 && index < (int)k.names.size() ? k.names[(size_t)index] : std::string(); }
  std::string category(int index) { const auto& k = pack(); return index >= 0 && index < (int)k.cats.size() ? k.cats[(size_t)index] : std::string(); }
  std::vector<std::string> categories() {
    std::vector<std::string> c;
    for (const auto& x : pack().cats) if (std::find(c.begin(), c.end(), x) == c.end()) c.push_back(x);
    return c;
  }

  bool build(int index, nlohmann::json& out, std::string& error) {
    const Pack& k = pack();
    if (!k.ok || index < 0 || index >= (int)k.names.size()) { error = "The built-in presets could not be read."; return false; }
    const auto& e = k.index["presets"][(size_t)index];
    const char* text = (const char*)k.raw.getData() + k.presets + e["o"].get<size_t>();
    try {
      out = nlohmann::json::parse(std::string(text, e["l"].get<size_t>()));
      rebuild(k, out);
    }
    catch (...) { error = "Preset is corrupted."; return false; }
    return true;
  }

  static void growlClassic(uint32_t seed, nlohmann::json& state) {
    juce::Random r((juce::int64)seed * 7919 + 17);
    auto range = [&](float a, float b) { return a + (b - a) * r.nextFloat(); };
    auto chance = [&](float p) { return r.nextFloat() < p; };
    auto pick = [&](std::initializer_list<float> v) { auto it = v.begin(); std::advance(it, r.nextInt((int)v.size())); return *it; };
    nlohmann::json& st = state["settings"];
    auto set = [&](const std::string& k, float v) { st[k] = v; };

    set("polyphony", 1.0f);
    set("env_1_attack", range(0.0f, 0.3f));
    set("env_1_release", range(0.35f, 0.7f));
    // three oscillators, each scrambling its harmonics (Random Amplitudes)
    const float tops[] = { 12, 19, 24, 27, 31, 34, 36, 38 };
    for (int o = 1; o <= 3; ++o) {
      std::string k = "osc_" + std::to_string(o) + "_";
      set(k + "on", 1.0f);
      set(k + "spectral_morph_type", 6.0f);
      set(k + "spectral_morph_amount", range(0.0f, 0.25f));
      set(k + "transpose", o == 1 ? pick({ 0, 0, 12 }) : tops[r.nextInt(8)]);
      set(k + "level", range(0.45f, 0.8f));
      set(k + "destination", 0.0f);
    }
    if (chance(0.7f)) { set("osc_1_unison_voices", pick({ 5, 8, 16 })); set("osc_1_unison_detune", range(2.5f, 5.0f)); }
    if (chance(0.4f)) {   // the third a sine sub instead
      set("osc_3_transpose", pick({ -12, 0 })); set("osc_3_spectral_morph_type", 0.0f); set("osc_3_level", 0.6f);
    }
    // the LFOs, at the tempo: shapes of their own
    auto shape = [&](int lfo) {
      int kind = r.nextInt(5);
      nlohmann::json l;
      if (kind == 0) l = { {"name","Triangle"}, {"num_points",3}, {"points",{0.0,1.0,0.5,0.0,1.0,1.0}}, {"powers",{0.0,0.0,0.0}}, {"smooth",false} };
      else if (kind == 1) l = { {"name","Saw Down"}, {"num_points",2}, {"points",{0.0,0.0,1.0,1.0}}, {"powers",{0.0,0.0}}, {"smooth",false} };
      else if (kind == 2) l = { {"name","Saw Up"}, {"num_points",2}, {"points",{0.0,1.0,1.0,0.0}}, {"powers",{0.0,0.0}}, {"smooth",false} };
      else if (kind == 3) l = { {"name","Pulse"}, {"num_points",4}, {"points",{0.0,0.0,0.5,0.0,0.5,1.0,1.0,1.0}}, {"powers",{0.0,0.0,0.0,0.0}}, {"smooth",false} };
      else {
        int n = 4 + r.nextInt(5);
        std::vector<double> pts, pw;
        for (int i = 0; i < n; ++i) { double y = r.nextFloat(); pts.push_back((double)i / n); pts.push_back(y); pts.push_back((double)(i + 1) / n - 0.001); pts.push_back(y); pw.push_back(0); pw.push_back(0); }
        l = { {"name","Steps"}, {"num_points",2 * n}, {"points",pts}, {"powers",pw}, {"smooth",false} };
      }
      if (st.count("lfos") && st["lfos"].size() > (size_t)lfo) st["lfos"][(size_t)lfo] = l;
    };
    shape(0); shape(1);
    set("lfo_1_sync", 1.0f); set("lfo_1_tempo", pick({ 5, 6, 7, 7, 8 }));
    set("lfo_2_sync", 1.0f); set("lfo_2_tempo", pick({ 5, 6, 7, 8 }));
    // the filter
    if (chance(0.65f)) {
      set("filter_1_on", 1.0f); set("filter_1_model", 0.0f); set("filter_1_style", 0.0f);
      set("filter_1_cutoff", range(50.0f, 80.0f)); set("filter_1_resonance", range(0.45f, 0.72f)); set("filter_1_mix", 1.0f);
    }
    // effects
    set("compressor_on", 1.0f); set("eq_on", 1.0f);
    set("distortion_on", chance(0.75f) ? 1.0f : 0.0f); set("distortion_type", 0.0f); set("distortion_drive", range(4.0f, 24.0f));
    set("flanger_on", chance(0.65f) ? 1.0f : 0.0f); set("flanger_dry_wet", range(0.3f, 0.7f)); set("flanger_feedback", range(0.3f, 0.8f));
    set("chorus_on", chance(0.5f) ? 1.0f : 0.0f);
    set("delay_on", chance(0.65f) ? 1.0f : 0.0f); set("delay_dry_wet", range(0.15f, 0.35f));
    set("phaser_on", chance(0.35f) ? 1.0f : 0.0f);
    set("reverb_on", chance(0.3f) ? 1.0f : 0.0f); set("reverb_dry_wet", range(0.1f, 0.25f));

    // the routings, into the free modulation slots
    int slot = 0;
    auto mod = [&](const std::string& src, const std::string& dst, float amount) {
      auto& m = st["modulations"];
      while (slot < (int)m.size() && !m[(size_t)slot].value("source", std::string()).empty()) ++slot;
      if (slot >= (int)m.size()) return;
      m[(size_t)slot]["source"] = src; m[(size_t)slot]["destination"] = dst;
      st["modulation_" + std::to_string(slot + 1) + "_amount"] = amount;
      ++slot;
    };
    for (int o = 1; o <= 3; ++o) {
      std::string k = "osc_" + std::to_string(o) + "_";
      if (st[k + "spectral_morph_type"].get<float>() == 6.0f) mod("lfo_1", k + "spectral_morph_amount", range(0.5f, 1.0f));
      if (chance(0.45f)) mod(chance(0.5f) ? "lfo_1" : "lfo_2", k + "level", range(-0.7f, -0.3f));
    }
    if (st.value("filter_1_on", 0.0f) > 0.5f) mod(chance(0.6f) ? "lfo_2" : "lfo_1", "filter_1_cutoff", range(0.25f, 0.6f));
    if (st["flanger_on"].get<float>() > 0.5f) mod("note", "flanger_center", 1.0f);
    if (st["delay_on"].get<float>() > 0.5f && chance(0.5f)) mod("note", "delay_frequency", 1.0f);
    state["preset_name"] = "Growl " + std::to_string((seed % 9000) + 1000);
    state["author"] = "Aquanode";
    state["comments"] = "";
  }

  static void growlOneHit(uint32_t seed, nlohmann::json& state) {
    juce::Random r((juce::int64)seed * 7919 + 17);
    auto range = [&](float a, float b) { return a + (b - a) * r.nextFloat(); };
    auto chance = [&](float p) { return r.nextFloat() < p; };
    auto pick = [&](std::initializer_list<float> v) { auto it = v.begin(); std::advance(it, r.nextInt((int)v.size())); return *it; };
    nlohmann::json& st = state["settings"];
    auto set = [&](const std::string& k, float v) { st[k] = v; };

    // one hit: no attack, a long curved decay to almost nothing, a long release
    set("polyphony", 1.0f);
    set("env_1_attack", range(0.0f, 0.03f));
    set("env_1_decay", range(0.85f, 1.35f)); set("env_1_decay_power", range(-3.6f, -2.6f));
    set("env_1_sustain", range(0.02f, 0.08f));
    set("env_1_release", range(0.85f, 1.2f)); set("env_1_release_power", -2.0f);
    // three oscillators: a high one scrambling its harmonics (Random Amplitudes) on wide unison, the
    // body at the note opened by the envelope, a third an octave or so up, scrambling too
    set("osc_1_on", 1.0f); set("osc_1_transpose", pick({ 19, 24, 27, 27, 31 })); set("osc_1_level", 1.0f);
    set("osc_1_spectral_morph_type", 6.0f); set("osc_1_spectral_morph_amount", range(0.12f, 0.25f));
    set("osc_1_unison_voices", pick({ 8, 16, 16 })); set("osc_1_unison_detune", range(3.5f, 4.8f)); set("osc_1_unison_blend", 0.8f);
    set("osc_2_on", 1.0f); set("osc_2_transpose", 0.0f); set("osc_2_level", 1.0f);
    set("osc_2_spectral_morph_type", 0.0f); set("osc_2_distortion_type", chance(0.5f) ? 1.0f : 0.0f);
    set("osc_3_on", 1.0f); set("osc_3_transpose", pick({ 12, 12, 19, 24 })); set("osc_3_level", range(0.55f, 0.75f));
    set("osc_3_spectral_morph_type", 6.0f); set("osc_3_spectral_morph_amount", range(0.2f, 0.35f));
    for (int o = 1; o <= 3; ++o) set("osc_" + std::to_string(o) + "_destination", 0.0f);
    // the LFOs play once per note (envelope mode), smoothed: ramps, not switches
    auto shape = [&](int lfo) {
      int kind = r.nextInt(4);
      nlohmann::json l;
      if (kind <= 1) l = { {"name","Ramp Down"}, {"num_points",3}, {"points",{0.0,1.0,0.0,0.0,1.0,1.0}}, {"powers",{0.0,range(-2.0f,2.0f),0.0}}, {"smooth",false} };
      else if (kind == 2) l = { {"name","Rise and Fall"}, {"num_points",3}, {"points",{0.0,1.0,(double)range(0.15f,0.5f),0.0,1.0,1.0}}, {"powers",{0.0,0.0,0.0}}, {"smooth",false} };
      else {
        int n = 3 + r.nextInt(3);
        std::vector<double> pts, pw;
        for (int i = 0; i <= n; ++i) { pts.push_back((double)i / n); pts.push_back(i == 0 ? 1.0 : (i == n ? 1.0 : r.nextFloat())); pw.push_back(0.0); }
        l = { {"name","Contour"}, {"num_points",n + 1}, {"points",pts}, {"powers",pw}, {"smooth",true} };
      }
      if (st.count("lfos") && st["lfos"].size() > (size_t)lfo) st["lfos"][(size_t)lfo] = l;
    };
    shape(0); shape(1);
    for (int l = 1; l <= 2; ++l) {
      std::string k = "lfo_" + std::to_string(l) + "_";
      set(k + "sync", 1.0f); set(k + "sync_type", 2.0f); set(k + "smooth_mode", 1.0f); set(k + "smooth_time", -7.5f);
    }
    set("lfo_1_tempo", pick({ 6, 7, 7, 8 })); set("lfo_2_tempo", pick({ 7, 8, 9 }));
    // the filter: low, its sweep the growl
    set("filter_1_on", 1.0f); set("filter_1_model", 0.0f); set("filter_1_style", 0.0f); set("filter_1_mix", 1.0f);
    set("filter_1_cutoff", range(34.0f, 48.0f)); set("filter_1_resonance", range(0.4f, 0.6f));
    if (chance(0.6f)) {   // a comb that follows the note
      set("filter_fx_on", 1.0f); set("filter_fx_model", 6.0f); set("filter_fx_keytrack", 1.0f);
      set("filter_fx_cutoff", range(96.0f, 108.0f)); set("filter_fx_resonance", range(0.7f, 0.85f)); set("filter_fx_mix", 1.0f);
    }
    // effects: low at rest, brought in by the note's own sweep
    set("compressor_on", 1.0f); set("compressor_mix", 0.0f);
    set("eq_on", 1.0f); set("eq_low_gain", range(8.0f, 13.0f));
    set("distortion_on", 1.0f); set("distortion_type", 0.0f); set("distortion_drive", range(1.0f, 6.0f)); set("distortion_mix", range(0.3f, 0.55f));
    set("chorus_on", chance(0.7f) ? 1.0f : 0.0f); set("chorus_dry_wet", range(0.3f, 0.45f));
    set("flanger_on", chance(0.7f) ? 1.0f : 0.0f); set("flanger_dry_wet", 0.5f); set("flanger_feedback", range(0.4f, 0.6f)); set("flanger_mod_depth", 0.0f);
    set("delay_on", 1.0f); set("delay_sync", 0.0f); set("delay_dry_wet", 0.0f); set("delay_feedback", range(0.55f, 0.75f));
    set("phaser_on", 0.0f);
    set("reverb_on", chance(0.15f) ? 1.0f : 0.0f); set("reverb_dry_wet", range(0.08f, 0.15f));

    // the routings, into the free modulation slots
    int slot = 0;
    auto mod = [&](const std::string& src, const std::string& dst, float amount) {
      auto& m = st["modulations"];
      while (slot < (int)m.size() && !m[(size_t)slot].value("source", std::string()).empty()) ++slot;
      if (slot >= (int)m.size()) return;
      m[(size_t)slot]["source"] = src; m[(size_t)slot]["destination"] = dst;
      st["modulation_" + std::to_string(slot + 1) + "_amount"] = amount;
      ++slot;
    };
    mod("lfo_1", "osc_1_spectral_morph_amount", range(0.35f, 0.65f));
    mod(chance(0.5f) ? "lfo_1" : "lfo_2", "osc_3_spectral_morph_amount", range(0.0f, 0.4f));
    mod("env_1", "osc_2_level", 1.0f);
    mod("lfo_1", "filter_1_cutoff", range(0.45f, 0.65f));
    mod("lfo_1", "delay_dry_wet", range(0.6f, 1.0f));
    mod("lfo_1", "compressor_mix", 1.0f);
    mod("lfo_1", "distortion_mix", range(0.6f, 1.0f));
    mod("lfo_1", "chorus_dry_wet", range(-0.15f, 0.0f));
    mod("note", "delay_frequency", 1.0f);
    if (st["flanger_on"].get<float>() > 0.5f) mod("note", "flanger_center", 1.0f);
    mod("env_1", "eq_high_cutoff", range(0.25f, 0.4f));
    if (chance(0.5f)) mod("lfo_2", "osc_2_transpose", range(0.3f, 0.5f));
    state["preset_name"] = "Growl " + std::to_string((seed % 9000) + 1000);
    state["author"] = "Aquanode";
    state["comments"] = "";
  }

  void growl(uint32_t seed, nlohmann::json& state) {
    // half the time the sustained, LFO-driven growl of 6.9.5, half the time a one-hit growl
    if (juce::Random((juce::int64)seed * 104729 + 3).nextBool()) growlOneHit(seed, state);
    else growlClassic(seed, state);
  }

  //==== Generate (6.9.11): new presets of a kind, learned from the soundscapes ====
  // Every soundscape is put into the kinds it fits once - by its category, its
  // name and its sound (the amp envelope, the voices, how low it plays). A new
  // preset of a kind is two of them crossed: the first one's routings,
  // wavetables and switches, each section's knobs (oscillators, filters,
  // envelopes, LFOs, effects) somewhere between the two at random, and a
  // little jitter on what moves smoothly - new, not a copy.
  std::vector<std::string> generatorKinds() {
    return { "Pads", "Basses", "Leads", "Plucks", "Keys", "Textures", "DnB", "Dub Techno", "Psychill", "Growl" };
  }

  static float num(const nlohmann::json& s, const char* key, float fallback) {
    auto it = s.find(key);
    return it != s.end() && it->is_number() ? it->get<float>() : fallback;
  }

  static const std::vector<std::vector<int>>& kindMembers() {
    static const std::vector<std::vector<int>> members = [] {
      const auto kinds = generatorKinds();
      std::vector<std::vector<int>> m(kinds.size());
      for (int i = 0; i < count(); ++i) {
        nlohmann::json j; std::string error;
        if (!build(i, j, error)) continue;
        const auto& s = j["settings"];
        const std::string cat = category(i), nm = name(i);
        auto has = [&](const char* w) { return juce::String(nm).containsIgnoreCase(w); };
        const float attack = num(s, "env_1_attack", 0.15f), decay = num(s, "env_1_decay", 1.0f);
        const float sustain = num(s, "env_1_sustain", 1.0f), poly = num(s, "polyphony", 8.0f);
        const float transpose = num(s, "osc_1_transpose", 0.0f);
        const bool pad = cat == "Pads" || has("Pad") || attack > 0.7f;
        const bool bass = cat == "Bass" || has("Bass") || has("Sub") || transpose <= -12.0f;
        const bool pluck = !pad && sustain < 0.2f && decay < 1.3f;
        const bool lead = !pad && !pluck && !bass && (poly <= 1.0f || num(s, "legato", 0.0f) > 0.5f) && sustain > 0.4f;
        const bool keys = !pad && !pluck && !bass && !lead && attack < 0.3f && sustain > 0.15f && poly > 1.0f
                          && (cat == "Instruments" || cat == "Basics" || cat == "FM Patches" || cat == "Psychill" || cat == "Inspired by");
        const bool texture = cat == "Ambient" || cat == "Ecophony" || cat == "FX Patches";
        const bool flags[] = { pad, bass, lead, pluck, keys, texture,
                               cat == "DnB" || has("DnB"), cat == "Dub Techno", cat == "Psychill", false };
        for (size_t k = 0; k < kinds.size(); ++k)
          if (flags[k]) m[k].push_back(i);
      }
      return m;
    }();
    return members;
  }

  int kindSize(const std::string& kind) {
    const auto kinds = generatorKinds();
    for (size_t k = 0; k < kinds.size(); ++k)
      if (kinds[k] == kind) return kind == "Growl" ? 1 : (int)kindMembers()[k].size();
    return 0;
  }

  bool generate(const std::string& kind, uint32_t seed, nlohmann::json& state, std::string& error) {
    if (kind == "Growl") { growl(seed, state); return true; }
    const auto kinds = generatorKinds();
    size_t k = 0;
    while (k < kinds.size() && kinds[k] != kind) ++k;
    if (k >= kinds.size() || kindMembers()[k].size() < 2) { error = "no presets of that kind"; return false; }
    const auto& members = kindMembers()[k];
    juce::Random r((juce::int64)seed * 2654435761u + 17);
    const int a = members[(size_t)r.nextInt((int)members.size())];
    int b = a;
    while (b == a) b = members[(size_t)r.nextInt((int)members.size())];
    nlohmann::json base, other;
    if (!build(a, base, error) || !build(b, other, error)) return false;
    auto& s = base["settings"];
    const auto& o = other["settings"];
    // one blend per section, so a section keeps its own sense
    static const char* sections[] = { "osc_1_", "osc_2_", "osc_3_", "filter_1_", "filter_2_", "filter_fx_", "env_", "lfo_",
                                      "chorus_", "compressor_", "delay_", "distortion_", "eq_", "flanger_", "phaser_", "reverb_" };
    std::map<std::string, float> blend;
    for (const char* sec : sections) blend[sec] = r.nextFloat();
    for (auto it = s.begin(); it != s.end(); ++it) {
      const std::string key = it.key();
      if (!it->is_number() || o.count(key) == 0 || !o.at(key).is_number()) continue;
      std::string sec;
      for (const char* p : sections) if (key.rfind(p, 0) == 0) { sec = p; break; }
      if (sec.empty()) continue;   // (voices, tuning, master: the first one's)
      const float va = it->get<float>(), vb = o[key].get<float>();
      if (va == std::floor(va) && vb == std::floor(vb) && std::abs(va - vb) >= 1.0f) {
        *it = r.nextFloat() < blend[sec] ? vb : va;   // a switch or a choice: one or the other
        continue;
      }
      float v = va + blend[sec] * (vb - va);
      if (std::abs(va - vb) > 1.0e-6f || std::abs(va) > 1.0e-6f)
        v += (r.nextFloat() * 2.0f - 1.0f) * 0.03f * std::max(std::abs(va), std::abs(vb));
      *it = v;
    }
    state = base;
    state["preset_name"] = kind + " " + std::to_string((seed % 9000) + 1000);
    state["settings"]["mpe_enabled"] = 0.0f;
    return true;
  }
}
