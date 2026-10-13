#include <GL/glew.h>
#include <SDL.h>
#include "realm/platform/PlatformSDL.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace realm::platform {
WindowSession::~WindowSession() {
  if (context) SDL_GL_DeleteContext(context);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
}
void WindowSession::open(const char* title, int width, int height, bool hidden, int swap_interval) {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) throw std::runtime_error(SDL_GetError());
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
  if (!window) throw std::runtime_error(SDL_GetError());
  context = SDL_GL_CreateContext(window);
  if (!context) throw std::runtime_error(SDL_GetError());
  glewExperimental = GL_TRUE;
  const auto glew_status = glewInit();
  if (glew_status != GLEW_OK) throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(glew_status)));
  // GLEW can leave GL_INVALID_ENUM on core contexts during extension discovery.
  while (glGetError() != GL_NO_ERROR) {
  }
  if (!GLEW_VERSION_3_3) throw std::runtime_error("OpenGL 3.3 required");
  if (SDL_GL_SetSwapInterval(swap_interval) != 0) std::cerr << "Swap interval unavailable: " << SDL_GetError() << '\n';
}
CursorSet::CursorSet(std::vector<unsigned char> pixels, int width, int height)
    : pixels_(std::move(pixels)), width_(width), height_(height) {}
CursorSet::~CursorSet() {
  SDL_SetCursor(SDL_GetDefaultCursor());
  for (auto* cursor : cursors_)
    if (cursor) SDL_FreeCursor(cursor);
}
void CursorSet::select(int index) {
  if (index == shown_) return;
  shown_ = index;
  if (index < 0 || index >= 16 || width_ < 128 || height_ < 128) {
    SDL_SetCursor(SDL_GetDefaultCursor());
    return;
  }
  auto& cursor = cursors_[static_cast<std::size_t>(index)];
  if (!cursor) {
    std::vector<unsigned char> pixels(32 * 32 * 4);
    const int left = (index % 4) * 32, top = (index / 4) * 32;
    for (int y = 0; y < 32; ++y)
      std::copy_n(pixels_.data() + (static_cast<std::size_t>(top + y) * static_cast<std::size_t>(width_) +
                                    static_cast<std::size_t>(left)) *
                                       4,
                  32 * 4, pixels.data() + static_cast<std::size_t>(y) * 32 * 4);
    if (auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), 32, 32, 32, 32 * 4, SDL_PIXELFORMAT_RGBA32)) {
      cursor = SDL_CreateColorCursor(surface, 15, 15);
      SDL_FreeSurface(surface);
    }
  }
  SDL_SetCursor(cursor ? cursor : SDL_GetDefaultCursor());
}
}
