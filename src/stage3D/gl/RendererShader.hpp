#pragma once

#include "GlApi.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace realm::renderer_detail {
// The GLSL source of a program body (the text after its version line): desktop GLSL 3.30 core, or
// GLSL ES 3.00 for WebGL 2 with the default precisions the body relies on. REALM_GLES is defined in
// the GLES source so a body can branch on it.
inline std::string shader_source(std::string_view body) {
#if REALM_GLES
  constexpr std::string_view header =
      "#version 300 es\n"
      "#define REALM_GLES 1\n"
      "precision highp float;\n"
      "precision highp int;\n"
      "precision highp sampler2D;\n"
      "precision highp sampler2DArray;\n";
#else
  constexpr std::string_view header = "#version 330 core\n";
#endif
  std::string source(header);
  source.append(body);
  return source;
}

inline GLuint compile_shader(GLenum type, const std::string& source) {
  const auto id = glCreateShader(type);
  const char* text = source.c_str();
  glShaderSource(id, 1, &text, nullptr);
  glCompileShader(id);
  GLint ok{};
  glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    std::string log(4096, '\0');
    glGetShaderInfoLog(id, static_cast<GLsizei>(log.size()), nullptr, log.data());
    glDeleteShader(id);
    throw std::runtime_error("Shader compilation: " + log);
  }
  return id;
}
}
