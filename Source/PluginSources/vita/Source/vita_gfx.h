#pragma once
// Vita draws its interface with juce::Graphics, straight into the card it sits in: no OpenGL and no
// software rasterizer. Vital's drawing code keeps its shape (OpenGlComponent::render(open_gl, animate),
// the "shaders" a quad or a line is drawn with), but every primitive now paints itself with JUCE
// (open_gl_multi_quad.cpp, open_gl_line_renderer.cpp, ...), so it is sharp at every zoom.
//
// What stays of Vital's GLSL is the maths of its filter-response vertex shaders - the curves the filter,
// EQ, phaser, flanger and distortion displays show - ported to C++ here and run on the CPU.

namespace vgl {
  // one of Vital's filter-response vertex shaders (Shaders::k...FilterResponseVertex) with its
  // uniforms: response() is the shader's response_out for a vertex
  class ShaderProgram {
    public:
      static constexpr int kMaxUniforms = 24;

      explicit ShaderProgram(int vertex_shader) : vertex_(vertex_shader) { }

      int getProgramID() const noexcept { return vertex_; }
      void use() const noexcept { }
      bool hasUniform(const char* name) const;

      struct Uniform {
        Uniform(const ShaderProgram& program, const char* name);
        void set(float n1) const noexcept { set4(n1, 0.0f, 0.0f, 0.0f); }
        void set(int n1) const noexcept { set4((float)n1, 0.0f, 0.0f, 0.0f); }
        void set(float n1, float n2) const noexcept { set4(n1, n2, 0.0f, 0.0f); }
        void set(float n1, float n2, float n3) const noexcept { set4(n1, n2, n3, 0.0f); }
        void set(float n1, float n2, float n3, float n4) const noexcept { set4(n1, n2, n3, n4); }

        int uniformID;

      private:
        void set4(float a, float b, float c, float d) const noexcept;
        ShaderProgram* program_;
      };

      struct Attribute {
        Attribute(const ShaderProgram&, const char*) { }
        int attributeID = 0;
      };

      // the response (GL's y, -1 .. 1) at the vertex (x, y), both in GL coordinates
      float response(float x, float y) const;
      const float* uniform(int slot) const { return uniforms_[slot]; }

    private:
      int vertex_;
      float uniforms_[kMaxUniforms][4] = {};

      JUCE_DECLARE_NON_COPYABLE(ShaderProgram)
  };
}
