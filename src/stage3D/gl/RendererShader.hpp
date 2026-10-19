#pragma once

#include <GL/glew.h>
#include <stdexcept>
#include <string>

namespace realm::renderer_detail {
inline GLuint compile_shader(GLenum type, const char* source) {
  const auto id = glCreateShader(type);
  glShaderSource(id, 1, &source, nullptr);
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
