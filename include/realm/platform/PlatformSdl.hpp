#pragma once
#include "realm/stage3D/GraphicsApi.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <array>
struct SDL_Window;
struct SDL_Cursor;

namespace realm::platform {
// Construct before GPU resources; its destructor runs after their teardown.
class WindowSession {
 public:
  WindowSession() = default;
  ~WindowSession();
  WindowSession(const WindowSession&) = delete;
  WindowSession& operator=(const WindowSession&) = delete;
  // Creates the window and the API's surface: an OpenGL 3.3 core context (GLEW), or a Vulkan window
  // without a context. Throws with the reason when either cannot be made.
  void open(gfx::Api api, const char* title, int width, int height, bool hidden, int swap_interval);
  // Destroys the window and context but keeps SDL initialised, so another API's window can open.
  void close() noexcept;
  // Presentation: drawable size in pixels, present (swap) and the vsync interval.
  void drawable_size(int& width, int& height) const;
  void present();
  [[nodiscard]] int swap_interval() const;
  void set_swap_interval(int interval);

  [[nodiscard]] gfx::Api api() const { return api_; }
  // Vulkan sessions: the loader's vkGetInstanceProcAddr (opaque), the instance extensions the window
  // needs, and the surface for an instance (VkInstance as an opaque pointer; 0 on failure).
  [[nodiscard]] void* vulkan_instance_proc() const;
  [[nodiscard]] std::vector<std::string> vulkan_instance_extensions() const;
  [[nodiscard]] std::uint64_t vulkan_surface(void* instance) const;
  SDL_Window* window{};

 private:
  gfx::Api api_{gfx::Api::opengl};
  void* context{};
};

class CursorSet {
 public:
  CursorSet(std::vector<unsigned char> pixels, int width, int height);
  ~CursorSet();
  CursorSet(const CursorSet&) = delete;
  CursorSet& operator=(const CursorSet&) = delete;
  void select(int index);

 private:
  std::vector<unsigned char> pixels_;
  int width_{};
  int height_{};
  std::array<SDL_Cursor*, 16> cursors_{};
  int shown_{-2};
};
}
