#pragma once

#include "realm/map/PostEffects.hpp"
#include <array>
#include <cstdint>
#include <cstddef>

namespace realm {
class RendererPostProcess {
 public:
  RendererPostProcess();
  ~RendererPostProcess();
  RendererPostProcess(const RendererPostProcess&) = delete;
  RendererPostProcess& operator=(const RendererPostProcess&) = delete;

  void ensure_targets(int width, int height);

  [[nodiscard]] std::uint32_t framebuffer(std::size_t index) const { return framebuffers_[index]; }

  [[nodiscard]] std::size_t run_post(int width, int height, int clip_width,
                                     const ScreenEffects& effects);
  [[nodiscard]] std::size_t run_screen_overlays(int width, int height, int clip_width,
                                                const ScreenEffects& effects);

 private:
  std::uint32_t program_{};
  std::uint32_t vao_{};
  std::array<std::uint32_t, 2> textures_{};
  std::array<std::uint32_t, 2> framebuffers_{};
  int width_{};
  int height_{};
  int rect_{};
  int viewport_{};
  int mode_{};
  int axis_{};
  int blur_{};
  int blind_{};
  int region_{};
  int alpha_{};
  int sidebar_{};

  void release() noexcept;
};
}
