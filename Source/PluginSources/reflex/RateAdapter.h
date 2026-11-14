// Runs the Reflex engine at 44.1 kHz (the rate every measurement was made at) and resamples
// to the host rate with a Kaiser-windowed sinc interpolator. MIDI and musical time are mapped
// onto the internal timeline, so timing stays exact; the added delay is reported as latency.
#pragma once
#include "Engine.h"
#include <vector>
#include <cmath>
#include <algorithm>

class RateAdapter {
public:
  static constexpr double InternalRate = 44100.0;
  static constexpr int HalfTaps = 16;            // kernel half-width in input samples
  static constexpr int Phases = 512;             // kernel table resolution per input sample

  void prepare(rfx::Engine& e, double hostRate) {
    engine = &e;
    hostSr = hostRate;
    ratio = InternalRate / hostSr;               // input samples per output sample
    engine->setSampleRate(InternalRate);
    // anti-alias cutoff relative to the input Nyquist (only bites when the host rate is lower)
    const double fc = 0.94 * std::min(1.0, 1.0 / ratio);
    kernel.assign((size_t)(2 * HalfTaps * Phases + 1), 0.f);
    const double beta = 8.0, i0b = bessel0(beta);
    for (int i = 0; i <= 2 * HalfTaps * Phases; ++i) {
      const double t = (double)i / Phases - HalfTaps;
      const double x = t / HalfTaps;
      const double w = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - x * x))) / i0b;
      const double s = t == 0.0 ? 1.0 : std::sin(kPi * fc * t) / (kPi * fc * t);
      kernel[(size_t)i] = (float)(fc * s * w);
    }
    // look-ahead of at least HalfTaps+1 input samples, rounded so the latency is a whole number of host samples
    latency = (int)std::ceil((HalfTaps + 1) / ratio);
    Delay = latency * ratio;
    ring.assign(RingSize * 2, 0.f);
    produced = 0; hostPos = 0;
    pend.clear();
  }

  // latency in host samples
  int latencySamples() const { return latency; }

  void process(float* outL, float* outR, int n, const std::vector<rfx::MidiEv>& ev,
               bool playing, bool ppqValid, double ppqAtBlockStart, double tempo) {
    // queue events on the internal timeline (absolute input-sample index)
    for (const auto& e : ev) {
      Pending p; p.at = (long long)std::floor((double)(hostPos + e.frame) * ratio); p.ev = e;
      pend.push_back(p);
    }
    blockStartHost = hostPos; blockPpq = ppqAtBlockStart; bpm = tempo; isPlaying = playing; havePpq = ppqValid;
    for (int i = 0; i < n; ++i) {
      const double x = (double)(hostPos + i) * ratio - Delay;     // input position for this output sample
      const long long need = (long long)std::floor(x) + HalfTaps + 1;
      while (produced <= need) produceChunk();
      double accL = 0.0, accR = 0.0;
      const long long base = (long long)std::floor(x);
      const double frac = x - (double)base;
      for (int k = -HalfTaps + 1; k <= HalfTaps; ++k) {
        const double t = (double)k - frac;                          // distance from x to sample base+k
        const double ti = (t + HalfTaps) * Phases;
        const int i0 = (int)ti; const double f = ti - i0;
        if (i0 < 0 || i0 >= (int)kernel.size() - 1) continue;
        const double w = kernel[(size_t)i0] + (kernel[(size_t)i0 + 1] - kernel[(size_t)i0]) * f;
        const long long idx = base + k;
        if (idx < 0) continue;
        const size_t r = (size_t)(idx & (RingSize - 1));
        accL += w * ring[r * 2]; accR += w * ring[r * 2 + 1];
      }
      outL[i] = (float)accL; outR[i] = (float)accR;
    }
    hostPos += n;
  }

private:
  static constexpr int RingSize = 8192;          // power of two
  static constexpr int Chunk = 32;
  static constexpr double kPi = 3.14159265358979323846;
  struct Pending { long long at; rfx::MidiEv ev; };

  rfx::Engine* engine = nullptr;
  double hostSr = 44100.0, ratio = 1.0, Delay = HalfTaps + 1;
  int latency = 0;
  std::vector<float> kernel, ring;
  long long produced = 0, hostPos = 0, blockStartHost = 0;
  double blockPpq = 0.0, bpm = 120.0;
  bool isPlaying = false, havePpq = false;
  std::vector<Pending> pend;
  std::vector<rfx::MidiEv> chunkEv;
  float bufL[Chunk], bufR[Chunk];

  static double bessel0(double x) {
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
    return s;
  }

  void produceChunk() {
    chunkEv.clear();
    for (size_t i = 0; i < pend.size();) {
      if (pend[i].at < produced + Chunk) {
        rfx::MidiEv e = pend[i].ev; e.frame = (int)std::max(0LL, pend[i].at - produced);
        chunkEv.push_back(e); pend.erase(pend.begin() + (long)i);
      } else ++i;
    }
    std::stable_sort(chunkEv.begin(), chunkEv.end(), [](const rfx::MidiEv& a, const rfx::MidiEv& b) { return a.frame < b.frame; });
    engine->setTempo(bpm);
    if (havePpq) {
      // musical time of this internal chunk, derived from the host block start
      const double hostT = (double)produced / ratio - (double)blockStartHost;   // in host samples
      engine->setTransport(isPlaying, blockPpq + hostT / hostSr * bpm / 60.0);
    }
    engine->processBlock(bufL, bufR, Chunk, chunkEv.data(), (int)chunkEv.size());
    for (int i = 0; i < Chunk; ++i) {
      const size_t r = (size_t)((produced + i) & (RingSize - 1));
      ring[r * 2] = bufL[i]; ring[r * 2 + 1] = bufR[i];
    }
    produced += Chunk;
  }
};
