// Vita's drawing: see vita_gfx.h. The filter-response maths below is Vital's GLSL (shaders.cpp in
// Vital) ported line by line to C++, each function keeping the GLSL's names and order.

#include "vita_gfx.h"
#include "shaders.h"

namespace vgl {
  namespace {
    // --- a little GLSL ------------------------------------------------------------------------------
    struct vec2 {
      float x, y;
      vec2() : x(0.0f), y(0.0f) { }
      vec2(float a, float b) : x(a), y(b) { }
    };
    inline vec2 operator+(vec2 a, vec2 b) { return { a.x + b.x, a.y + b.y }; }
    inline vec2 operator-(vec2 a, vec2 b) { return { a.x - b.x, a.y - b.y }; }
    inline vec2 operator-(vec2 a) { return { -a.x, -a.y }; }
    inline vec2 operator*(vec2 a, vec2 b) { return { a.x * b.x, a.y * b.y }; }
    inline vec2 operator*(float s, vec2 a) { return { s * a.x, s * a.y }; }
    inline vec2 operator*(vec2 a, float s) { return { s * a.x, s * a.y }; }
    inline float length(vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
    inline vec2 abs(vec2 a) { return { std::fabs(a.x), std::fabs(a.y) }; }
    inline vec2 max(vec2 a, vec2 b) { return { std::max(a.x, b.x), std::max(a.y, b.y) }; }

    struct vec4 {
      float r, g, b, a;
      vec4() : r(0.0f), g(0.0f), b(0.0f), a(0.0f) { }
      vec4(float x, float y, float z, float w) : r(x), g(y), b(z), a(w) { }
      explicit vec4(const float* v) : r(v[0]), g(v[1]), b(v[2]), a(v[3]) { }
    };
    inline vec4 operator+(vec4 x, vec4 y) { return { x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a }; }
    inline vec4 operator*(float s, vec4 x) { return { s * x.r, s * x.g, s * x.b, s * x.a }; }
    inline vec4 operator*(vec4 x, float s) { return s * x; }

    inline float clamp(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
    inline float step(float edge, float v) { return v < edge ? 0.0f : 1.0f; }
    enum Uniforms {
      u_midi_cutoff, u_resonance, u_drive, u_mix, u_db24, u_stage0, u_stage1, u_stage2, u_stage3, u_stage4,
      u_formant_cutoff, u_formant_resonance, u_low, u_band, u_high,
      u_low_amount, u_band_amount, u_high_amount,
      kNumUniformNames
    };

    const char* kUniformNames[kNumUniformNames] = {
      "midi_cutoff", "resonance", "drive", "mix", "db24", "stage0", "stage1", "stage2", "stage3", "stage4",
      "formant_cutoff", "formant_resonance", "low", "band", "high",
      "low_amount", "band_amount", "high_amount"
    };

    static_assert(kNumUniformNames <= ShaderProgram::kMaxUniforms, "too many uniform names");

    int uniformSlot(const char* name) {
      for (int i = 0; i < kNumUniformNames; ++i) {
        if (std::strcmp(kUniformNames[i], name) == 0)
          return i;
      }
      return -1;
    }

    inline float U(const ShaderProgram& p, int name) { return p.uniform(name)[0]; }
    inline const float* Uv(const ShaderProgram& p, int name) { return p.uniform(name); }

    struct ResponseConstants {
      float min_midi_note, max_midi_note, min_db, max_db;
    };
    constexpr ResponseConstants kFilterResponse { 8.0f, 137.0f, -30.0f, 20.0f };
    constexpr ResponseConstants kEqResponse { 8.0f, 136.0f, -1.0f, 1.0f };
    constexpr float kPi = 3.14159265359f;
    constexpr float kMidi0Frequency = 8.1757989156f;

    inline vec2 complexMultiply(vec2 a, vec2 b) { return { a.x * b.x - a.y * b.y, a.y * b.x + a.x * b.y }; }
    inline vec2 complexReciprocal(vec2 num) {
      vec2 conjugate(num.x, -num.y);
      vec2 denominator = complexMultiply(num, conjugate);
      return { conjugate.x / denominator.x, conjugate.y / denominator.x };
    }
    inline vec2 complexDivide(vec2 a, vec2 b) {
      vec2 conjugate(b.x, -b.y);
      vec2 num = complexMultiply(a, conjugate);
      vec2 den = complexMultiply(b, conjugate);
      return { num.x / den.x, num.y / den.x };
    }
    inline vec2 onePoleInvertResponse(float cutoff) { return { 1.0f, cutoff }; }
    inline vec2 onePoleResponse(float cutoff) { return complexReciprocal(vec2(1.0f, cutoff)); }
    inline float magnitudeToDb(float magnitude) { return 8.685889638065037f * std::log(magnitude); }

    struct Response {
      const ResponseConstants& k;
      float getCutoffRatio(float x, float midi_cutoff) const {
        float percent = 0.5f * (x + 1.0f);
        float midi_note = k.min_midi_note + percent * (k.max_midi_note - k.min_midi_note);
        return std::pow(2.0f, std::min((midi_note - midi_cutoff) / 12.0f, 8.0f));
      }
      float getYForResponse(vec2 response) const {
        float magnitude_response = length(response);
        float db = magnitudeToDb(magnitude_response);
        return 2.0f * (db - k.min_db) / (k.max_db - k.min_db) - 1.0f;
      }
    };

    struct FilterUniforms {
      float midi_cutoff, resonance, drive, mix, db24, stage0, stage1, stage2, stage3, stage4;
      explicit FilterUniforms(const ShaderProgram& p) :
          midi_cutoff(U(p, u_midi_cutoff)), resonance(U(p, u_resonance)), drive(U(p, u_drive)),
          mix(U(p, u_mix)), db24(U(p, u_db24)), stage0(U(p, u_stage0)), stage1(U(p, u_stage1)),
          stage2(U(p, u_stage2)), stage3(U(p, u_stage3)), stage4(U(p, u_stage4)) { }
    };

    const vec2 kOne(1.0f, 0.0f);

    void analogResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      vec2 position(a[0][0], a[0][1]);
      vec2 one_pole = onePoleResponse(r.getCutoffRatio(position.x, u.midi_cutoff));
      vec2 low = complexMultiply(one_pole, one_pole);
      vec2 band = one_pole - low;
      vec2 high = kOne - one_pole - band;
      vec2 two_stage_pre = u.stage3 * low + u.stage1 * band + u.stage4 * high;
      vec2 two_stage = u.stage0 * low + u.stage1 * band + u.stage2 * high;
      vec2 feedback = complexMultiply(one_pole, kOne - one_pole);
      vec2 denominator_pre = kOne - feedback;
      vec2 denominator = kOne - u.resonance * feedback;
      vec2 response_pre = complexDivide(two_stage_pre, denominator_pre);
      vec2 response_res = complexDivide(two_stage, denominator);
      vec2 response = u.drive * response_res;
      response = response + u.db24 * (complexMultiply(response_pre, response) - response);
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void combResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      constexpr float kMaxCycles = 6.0f;
      vec2 position(a[0][0], a[0][1]);
      float ratio = r.getCutoffRatio(position.x, u.midi_cutoff);
      float ratio_diff = r.getCutoffRatio(position.x + 0.02f, u.midi_cutoff) - ratio;
      float max_step = step(kMaxCycles, ratio);
      vec2 tick = u.resonance * vec2(std::cos(2.0f * kPi * ratio), -std::sin(2.0f * kPi * ratio));
      vec2 low_pass = onePoleResponse(r.getCutoffRatio(position.x, u.stage2));
      vec2 high_pass = kOne - low_pass;
      vec2 one_pole = u.stage0 * low_pass + u.stage1 * high_pass;
      vec2 high_pass2 = kOne - onePoleResponse(r.getCutoffRatio(position.x, u.stage3));
      vec2 filter_input(1.0f - 0.5f * std::fabs(u.resonance), 0.0f);
      filter_input = complexMultiply(complexMultiply(filter_input, one_pole), high_pass2);
      vec2 denominator = kOne - complexMultiply(complexMultiply(tick, one_pole), high_pass2);
      float round_value = complexMultiply(one_pole, high_pass2).x * std::fabs(u.resonance);
      vec2 denominator_round(1.0f - round_value, 0.0f);
      vec2 denominator_round_down(1.0f + round_value, 0.0f);
      float max_step_mult = 1.0f - position.y;
      vec2 max_step_denominator = max_step_mult * denominator_round + (1.0f - max_step_mult) * denominator_round_down;
      denominator = max_step * max_step_denominator + (1.0f - max_step) * denominator;
      float denominator_round_step = 1.0f - std::max(max_step, step(ratio_diff, length(denominator)));
      denominator = denominator_round_step * denominator_round + (1.0f - denominator_round_step) * denominator;
      vec2 response = complexDivide(u.drive * filter_input, denominator);
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void positiveFlangeResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      constexpr float kMaxCycles = 8.0f;
      vec2 position(a[0][0], a[0][1]);
      float ratio = r.getCutoffRatio(position.x, u.midi_cutoff);
      float ratio_diff = r.getCutoffRatio(position.x + 0.02f, u.midi_cutoff) - ratio;
      float max_step = step(kMaxCycles, ratio);
      vec2 delay(std::cos(2.0f * kPi * ratio), -std::sin(2.0f * kPi * ratio));
      vec2 tick = u.resonance * delay;
      vec2 low_pass = onePoleResponse(r.getCutoffRatio(position.x, u.stage2));
      vec2 high_pass = kOne - low_pass;
      vec2 one_pole = u.stage0 * low_pass + u.stage1 * high_pass;
      vec2 high_pass2 = kOne - onePoleResponse(r.getCutoffRatio(position.x, u.stage3));
      vec2 filter_input(0.70710678119f, 0.0f);
      vec2 delay_input = complexMultiply(complexMultiply(filter_input, one_pole), high_pass2);
      vec2 denominator = kOne - tick;
      vec2 round_value = complexMultiply(one_pole, high_pass2) * u.resonance;
      vec2 denominator_round = complexMultiply(delay, kOne - round_value);
      vec2 denominator_round_down = complexMultiply(-delay, kOne + round_value);
      float max_step_mult = 1.0f - position.y;
      vec2 max_step_denominator = max_step_mult * denominator_round + (1.0f - max_step_mult) * denominator_round_down;
      denominator = max_step * max_step_denominator + (1.0f - max_step) * denominator;
      float denominator_round_step = 1.0f - std::max(max_step, step(ratio_diff, length(denominator)));
      denominator = denominator_round_step * denominator_round + (1.0f - denominator_round_step) * denominator;
      vec2 response = filter_input * u.drive + complexMultiply(complexDivide(delay_input, denominator), delay);
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void negativeFlangeResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      constexpr float kMaxCycles = 8.0f;
      vec2 position(a[0][0], a[0][1]);
      float ratio = r.getCutoffRatio(position.x, u.midi_cutoff + 12.0f);
      float max_step = step(kMaxCycles, ratio);
      vec2 delay(std::cos(2.0f * kPi * ratio), -std::sin(2.0f * kPi * ratio));
      vec2 tick = -u.resonance * delay;
      vec2 low_pass = onePoleResponse(r.getCutoffRatio(position.x, u.stage2));
      vec2 high_pass = kOne - low_pass;
      vec2 one_pole = u.stage0 * low_pass + u.stage1 * high_pass;
      vec2 high_pass2 = kOne - onePoleResponse(r.getCutoffRatio(position.x, u.stage3));
      vec2 filter_input(0.70710678119f, 0.0f);
      vec2 delay_input = complexMultiply(complexMultiply(filter_input, one_pole), high_pass2);
      vec2 denominator = kOne - complexMultiply(tick, complexMultiply(one_pole, high_pass2));
      vec2 round_value = -complexMultiply(one_pole, high_pass2) * u.resonance;
      vec2 denominator_round = complexMultiply(delay, kOne - round_value);
      vec2 denominator_round_down = complexMultiply(-delay, kOne + round_value);
      float max_step_mult = 1.0f - position.y;
      vec2 max_step_denominator = max_step_mult * denominator_round + (1.0f - max_step_mult) * denominator_round_down;
      denominator = max_step * max_step_denominator + (1.0f - max_step) * denominator;
      vec2 response = filter_input * u.drive - complexMultiply(complexDivide(delay_input, denominator), delay);
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void digitalResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      vec2 position(a[0][0], a[0][1]);
      float g = r.getCutoffRatio(position.x, u.midi_cutoff);
      vec2 g2(g * g, 0.0f);
      vec2 denominator = g2 + vec2(0.0f, g * u.resonance) + vec2(-1.0f, 0.0f);
      vec2 numerator = -u.stage0 * kOne + u.stage1 * vec2(0.0f, g) + u.stage2 * g2;
      vec2 numerator_pre = -u.stage3 * kOne + u.stage1 * vec2(0.0f, g) + u.stage4 * g2;
      vec2 response = complexDivide(numerator, denominator);
      vec2 pre_denominator = g2 + vec2(0.0f, g) + vec2(-1.0f, 0.0f);
      vec2 pre_response = complexDivide(numerator_pre, pre_denominator);
      response = response + u.db24 * (complexMultiply(response, pre_response) - response);
      response = response * u.drive;
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void diodeResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      vec2 position(a[0][0], a[0][1]);
      float ratio = r.getCutoffRatio(position.x, u.midi_cutoff);
      vec2 one_pole = onePoleResponse(ratio);
      vec2 high_pass_one_pole = onePoleResponse(ratio / u.stage0);
      vec2 high = kOne - high_pass_one_pole * 2.0f + complexMultiply(high_pass_one_pole, high_pass_one_pole);
      vec2 high_feedback = complexMultiply(high_pass_one_pole, kOne - high_pass_one_pole);
      vec2 high_denominator = kOne - high_feedback;
      vec2 high_pass_response = complexDivide(high, high_denominator);
      high_pass_response = kOne + u.db24 * (high_pass_response + vec2(-1.0f, 0.0f));
      vec2 loop = complexMultiply(one_pole, one_pole);
      vec2 series = 0.125f * complexMultiply(loop, loop);
      vec2 chain = complexDivide(series, kOne + series - loop);
      vec2 numerator = u.drive * chain;
      vec2 denominator = kOne + u.resonance * chain;
      vec2 response = complexDivide(numerator, denominator);
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      response = complexMultiply(response, high_pass_response);
      o[4] = r.getYForResponse(response);
    }

    void dirtyResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      vec2 position(a[0][0], a[0][1]);
      vec2 one_pole = onePoleResponse(r.getCutoffRatio(position.x, u.midi_cutoff));
      vec2 low = complexMultiply(one_pole, one_pole);
      vec2 band = one_pole - low;
      vec2 high = kOne - one_pole - band;
      vec2 two_stage_pre = u.stage3 * low + u.stage1 * band + u.stage4 * high;
      vec2 two_stage = u.stage0 * low + u.stage1 * band + u.stage2 * high;
      vec2 feedback = complexMultiply(one_pole, kOne - one_pole);
      vec2 denominator_pre = kOne - feedback;
      vec2 denominator = vec2(1.0f / u.resonance, 0.0f) - feedback;
      vec2 resonance_loop = complexDivide(band, denominator);
      vec2 response_pre = complexDivide(two_stage_pre, denominator_pre);
      vec2 response_res = complexMultiply(two_stage, kOne + resonance_loop);
      vec2 response = u.drive * response_res;
      response = response + u.db24 * (complexMultiply(response_pre, response) - response);
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void formantResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      Response r { kFilterResponse };
      vec2 position(a[0][0], a[0][1]);
      const float* formant_cutoff = Uv(p, u_formant_cutoff);
      const float* formant_resonance = Uv(p, u_formant_resonance);
      const float* low = Uv(p, u_low);
      const float* band = Uv(p, u_band);
      const float* high = Uv(p, u_high);
      float mix = U(p, u_mix);
      vec2 response;
      for (int i = 0; i < 4; ++i) {
        float g = r.getCutoffRatio(position.x, formant_cutoff[i]);
        vec2 g_sqr(g * g, 0.0f);
        vec2 denominator = g_sqr + vec2(0.0f, g * formant_resonance[i]) + vec2(-1.0f, 0.0f);
        vec2 numerator = -low[i] * kOne + band[i] * vec2(0.0f, g) + high[i] * g_sqr;
        response = response + complexDivide(numerator, denominator);
      }
      response = mix * response + vec2(1.0f - mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void ladderResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      vec2 position(a[0][0], a[0][1]);
      vec2 one_pole_invert = onePoleInvertResponse(r.getCutoffRatio(position.x, u.midi_cutoff));
      vec2 two_pole_invert = complexMultiply(one_pole_invert, one_pole_invert);
      vec2 three_pole_invert = complexMultiply(one_pole_invert, two_pole_invert);
      vec2 four_pole_invert = complexMultiply(one_pole_invert, three_pole_invert);
      vec2 numerator = u.drive * (u.stage0 * four_pole_invert + u.stage1 * three_pole_invert +
                                  u.stage2 * two_pole_invert + u.stage3 * one_pole_invert +
                                  vec2(u.stage4, 0.0f));
      vec2 denominator = four_pole_invert + vec2(u.resonance, 0.0f);
      vec2 response = complexDivide(numerator, denominator);
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void phaserResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      FilterUniforms u(p);
      Response r { kFilterResponse };
      vec2 position(a[0][0], a[0][1]);
      float g = r.getCutoffRatio(position.x, u.midi_cutoff);
      vec2 one_pole = onePoleResponse(g);
      vec2 all_pass = kOne - 2.0f * one_pole;
      vec2 half_peak = complexMultiply(all_pass, all_pass);
      vec2 peak1 = complexMultiply(half_peak, half_peak);
      vec2 peak3 = complexMultiply(peak1, peak1);
      vec2 peak5 = complexMultiply(peak3, peak1);
      vec2 chain = u.stage0 * peak1 + u.stage1 * peak3 + u.stage2 * peak5;
      float invert_mult = 1.0f - 2.0f * u.db24;
      vec2 feedback_chain = complexMultiply(chain, onePoleResponse(0.05f * g));
      feedback_chain = complexMultiply(feedback_chain, kOne - onePoleResponse(20.0f * g));
      vec2 denominator = kOne - invert_mult * u.resonance * feedback_chain;
      vec2 phase_response = complexDivide(chain, denominator);
      vec2 response = vec2(0.5f, 0.0f) + 0.5f * invert_mult * phase_response;
      response = u.mix * response + vec2(1.0f - u.mix, 0.0f);
      o[4] = r.getYForResponse(response);
    }

    void eqResponse(const ShaderProgram& p, const float (*a)[4], float* o) {
      Response r { kEqResponse };
      float position = a[0][0];
      const float* midi_cutoff = Uv(p, u_midi_cutoff);
      const float* resonance = Uv(p, u_resonance);
      const float* low_amount = Uv(p, u_low_amount);
      const float* band_amount = Uv(p, u_band_amount);
      const float* high_amount = Uv(p, u_high_amount);
      vec2 numerators[3], denominators[3];
      for (int i = 0; i < 3; ++i) {
        float g = r.getCutoffRatio(position, midi_cutoff[i]);
        vec2 g_sqr(g * g, 0.0f);
        denominators[i] = g_sqr + vec2(0.0f, g * resonance[i]) + vec2(-1.0f, 0.0f);
        numerators[i] = -low_amount[i] * kOne + band_amount[i] * vec2(0.0f, g) + high_amount[i] * g_sqr;
      }
      vec2 numerator = complexMultiply(numerators[0], complexMultiply(numerators[1], numerators[2]));
      vec2 denominator = complexMultiply(denominators[0], complexMultiply(denominators[1], denominators[2]));
      vec2 response = complexDivide(numerator, denominator);
      o[4] = r.getYForResponse(response);
    }
    using ResponseFunction = void (*)(const ShaderProgram&, const float (*)[4], float*);

    struct ResponseShader {
      ResponseFunction function;
      const char* uniforms[12];
    };

    #define FILTER_UNIFORM_NAMES "midi_cutoff", "resonance", "drive", "mix", "db24",                                  "stage0", "stage1", "stage2", "stage3", "stage4", nullptr

    const ResponseShader* responseShader(int vertex_shader) {
      static const ResponseShader kAnalog { analogResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kComb { combResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kPositiveFlange { positiveFlangeResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kNegativeFlange { negativeFlangeResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kDigital { digitalResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kDiode { diodeResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kDirty { dirtyResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kFormant { formantResponse, { "formant_cutoff", "formant_resonance", "low", "band",
                                                                 "high", "mix", nullptr } };
      static const ResponseShader kLadder { ladderResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kPhaser { phaserResponse, { FILTER_UNIFORM_NAMES } };
      static const ResponseShader kEq { eqResponse, { "midi_cutoff", "resonance", "low_amount", "band_amount",
                                                       "high_amount", nullptr } };

      switch (vertex_shader) {
        case Shaders::kAnalogFilterResponseVertex: return &kAnalog;
        case Shaders::kCombFilterResponseVertex: return &kComb;
        case Shaders::kPositiveFlangeFilterResponseVertex: return &kPositiveFlange;
        case Shaders::kNegativeFlangeFilterResponseVertex: return &kNegativeFlange;
        case Shaders::kDigitalFilterResponseVertex: return &kDigital;
        case Shaders::kDiodeFilterResponseVertex: return &kDiode;
        case Shaders::kDirtyFilterResponseVertex: return &kDirty;
        case Shaders::kFormantFilterResponseVertex: return &kFormant;
        case Shaders::kLadderFilterResponseVertex: return &kLadder;
        case Shaders::kPhaserFilterResponseVertex: return &kPhaser;
        case Shaders::kEqFilterResponseVertex: return &kEq;
        default: return nullptr;
      }
    }
  } // namespace

  bool ShaderProgram::hasUniform(const char* name) const {
    const ResponseShader* shader = responseShader(vertex_);
    if (shader == nullptr)
      return false;
    for (const char* const* n = shader->uniforms; *n; ++n) {
      if (std::strcmp(*n, name) == 0)
        return true;
    }
    return false;
  }

  ShaderProgram::Uniform::Uniform(const ShaderProgram& program, const char* name) :
      uniformID(uniformSlot(name)), program_(const_cast<ShaderProgram*>(&program)) { }

  void ShaderProgram::Uniform::set4(float a, float b, float c, float d) const noexcept {
    if (uniformID < 0)
      return;
    float* v = program_->uniforms_[uniformID];
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
  }

  float ShaderProgram::response(float x, float y) const {
    const ResponseShader* shader = responseShader(vertex_);
    if (shader == nullptr)
      return 0.0f;
    const float attributes[1][4] = { { x, y, 0.0f, 1.0f } };
    float out[8] = {};
    shader->function(*this, attributes, out);
    return out[4];
  }
} // namespace vgl

//==================================================================================================
// Shaders and OpenGlWrapper (shaders.h)
//==================================================================================================
vgl::ShaderProgram* Shaders::getShaderProgram(VertexShader vertex_shader, FragmentShader fragment_shader,
                                              const char** varyings) {
  int index = vertex_shader * kNumFragmentShaders + fragment_shader;
  auto found = shader_programs_.find(index);
  if (found != shader_programs_.end())
    return found->second.get();

  auto& program = shader_programs_[index];
  program = std::make_unique<vgl::ShaderProgram>(vertex_shader);
  return program.get();
}

bool OpenGlWrapper::setViewport(Rectangle<int> bounds, Rectangle<int> visible) {
  if (state_saved_)
    g.restoreState();
  g.saveState();
  state_saved_ = true;
  viewport = bounds;

  if (visible.isEmpty() || !g.reduceClipRegion(visible))
    return false;

  g.setOrigin(bounds.getPosition());
  return true;
}

void OpenGlWrapper::setScissor(Rectangle<int> visible) {
  g.reduceClipRegion(visible - viewport.getPosition());
}

void OpenGlWrapper::finish() {
  if (state_saved_)
    g.restoreState();
  state_saved_ = false;
}
