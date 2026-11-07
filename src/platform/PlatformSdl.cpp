#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
#include <SDL.h>
#if REALM_VULKAN_ENABLED
#include <vulkan/vulkan.h>  // before SDL_vulkan.h: SDL then uses these handle types
#endif
#include <SDL_vulkan.h>
#include "realm/platform/PlatformSdl.hpp"
#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace realm::platform {
WindowSession::~WindowSession() {
  close();
  SDL_Quit();
}

void WindowSession::close() noexcept {
  if (context) {
    SDL_GL_DeleteContext(context);
    context = nullptr;
  }
  if (window) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }
}

void WindowSession::open(gfx::Api api, const char* title, int width, int height, bool hidden,
                         [[maybe_unused]] int swap_interval) {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    throw std::runtime_error(SDL_GetError());
  }
  api_ = api;

  if (api == gfx::Api::webgpu) {
#if defined(__EMSCRIPTEN__) && REALM_WEBGPU_ENABLED
    // The canvas only: the WebGPU backend makes its surface from "#canvas" once the device is ready.
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                  (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
    if (!window) {
      throw std::runtime_error(SDL_GetError());
    }
    return;
#else
    throw std::runtime_error("WebGPU renderer not available in this build");
#endif
  }

  if (api == gfx::Api::vulkan) {
    // The window only: the Vulkan backend (not built yet) creates its instance and surface from it.
    if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
      throw std::runtime_error(std::string("Vulkan loader: ") + SDL_GetError());
    }
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                  (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
    if (!window) {
      throw std::runtime_error(std::string("Vulkan window: ") + SDL_GetError());
    }
    return;
  }

#if defined(__EMSCRIPTEN__)
  // WebGL 2 on the page's canvas: a GLES 3.0 context, the one WebGL 2 implements.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
  // OpenGL: the 3.3 core context and GLEW, as before the graphics backend seam.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#if defined(__EMSCRIPTEN__)
  // RmlUi clips with the stencil buffer; the web build has no depth buffer.
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#endif

  window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN));
  if (!window) {
    throw std::runtime_error(SDL_GetError());
  }

  context = SDL_GL_CreateContext(window);
  if (!context) {
    throw std::runtime_error(SDL_GetError());
  }

#if !defined(__EMSCRIPTEN__)
  // The web has no GLEW and no swap interval: SDL's swap there is the browser's requestAnimationFrame,
  // and changing it would replace the main loop's pacing.
  glewExperimental = GL_TRUE;
  const auto glew_status = glewInit();
  // Under EGL (native Wayland, X11 with SDL_VIDEO_FORCE_EGL) GLEW has no current GLX display and
  // returns GLEW_ERROR_NO_GLX_DISPLAY only after the GL core entry points and version flags are
  // loaded; the 3.3 check below covers them.
  if (glew_status != GLEW_OK && glew_status != GLEW_ERROR_NO_GLX_DISPLAY) {
    throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(glew_status)));
  }

  // GLEW can leave GL_INVALID_ENUM on core contexts during extension discovery.
  while (glGetError() != GL_NO_ERROR) {
  }
  if (!GLEW_VERSION_3_3) {
    throw std::runtime_error("OpenGL 3.3 required");
  }

  if (SDL_GL_SetSwapInterval(swap_interval) != 0) {
    std::cerr << "Swap interval unavailable: " << SDL_GetError() << '\n';
  }
#endif
}

#if REALM_VULKAN_ENABLED
void* WindowSession::vulkan_instance_proc() const {
  return SDL_Vulkan_GetVkGetInstanceProcAddr();
}

std::vector<std::string> WindowSession::vulkan_instance_extensions() const {
  unsigned int count = 0;
  if (!SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr)) {
    throw std::runtime_error(std::string("Vulkan extensions: ") + SDL_GetError());
  }
  std::vector<const char*> names(count);
  if (!SDL_Vulkan_GetInstanceExtensions(window, &count, names.data())) {
    throw std::runtime_error(std::string("Vulkan extensions: ") + SDL_GetError());
  }
  return {names.begin(), names.end()};
}

std::uint64_t WindowSession::vulkan_surface(void* instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), &surface)) {
    return 0;
  }
  return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(surface));
}
#else
void* WindowSession::vulkan_instance_proc() const {
  return nullptr;
}

std::vector<std::string> WindowSession::vulkan_instance_extensions() const {
  return {};
}

std::uint64_t WindowSession::vulkan_surface(void* /*instance*/) const {
  return 0;
}
#endif

void WindowSession::drawable_size(int& width, int& height) const {
  if (api_ == gfx::Api::vulkan) {
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
  } else if (api_ == gfx::Api::webgpu) {
#if defined(__EMSCRIPTEN__) && REALM_WEBGPU_ENABLED
    // No GL context on the WebGPU canvas: its backing size is the drawable size.
    emscripten_get_canvas_element_size("#canvas", &width, &height);
#else
    SDL_GL_GetDrawableSize(window, &width, &height);
#endif
  } else {
    SDL_GL_GetDrawableSize(window, &width, &height);
  }
}

void WindowSession::present() {
  // Vulkan presents through its swapchain, which the Vulkan backend will own.
  if (api_ == gfx::Api::opengl) {
    SDL_GL_SwapWindow(window);
  }
}

int WindowSession::swap_interval() const {
  return api_ == gfx::Api::opengl ? SDL_GL_GetSwapInterval() : 0;
}

void WindowSession::set_swap_interval(int interval) {
#if !defined(__EMSCRIPTEN__)
  if (api_ == gfx::Api::opengl) {
    SDL_GL_SetSwapInterval(interval);
  }
#else
  // Browser pacing: requestAnimationFrame, so the interval is not settable.
  (void)interval;
#endif
}

int WindowSession::refresh_rate() const {
  SDL_DisplayMode mode{};
  const int display = std::max(SDL_GetWindowDisplayIndex(window), 0);
  if (SDL_GetCurrentDisplayMode(display, &mode) != 0) {
    return 0;
  }
  return mode.refresh_rate;
}

CursorSet::CursorSet(std::vector<unsigned char> pixels, int width, int height)
    : pixels_(std::move(pixels)), width_(width), height_(height) {}

CursorSet::~CursorSet() {
  SDL_SetCursor(SDL_GetDefaultCursor());
  for (auto* cursor : cursors_) {
    if (cursor) {
      SDL_FreeCursor(cursor);
    }
  }
}

void CursorSet::select(int index) {
  if (index == shown_) {
    return;
  }
  shown_ = index;

  if (index < 0 || index >= 16 || width_ < 128 || height_ < 128) {
    SDL_SetCursor(SDL_GetDefaultCursor());
    return;
  }
  auto& cursor = cursors_[static_cast<std::size_t>(index)];
  if (!cursor) {
    std::vector<unsigned char> pixels(32 * 32 * 4);
    const int left = (index % 4) * 32;
    const int top = (index / 4) * 32;

    for (int y = 0; y < 32; ++y) {
      std::copy_n(
          pixels_.data() + (static_cast<std::size_t>(top + y) * static_cast<std::size_t>(width_) +
                            static_cast<std::size_t>(left)) *
                               4,
          32 * 4, pixels.data() + static_cast<std::size_t>(y) * 32 * 4);
    }

    if (auto* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), 32, 32, 32, 32 * 4,
                                                           SDL_PIXELFORMAT_RGBA32)) {
      cursor = SDL_CreateColorCursor(surface, 15, 15);
      SDL_FreeSurface(surface);
    }
  }
  SDL_SetCursor(cursor ? cursor : SDL_GetDefaultCursor());
}
}
