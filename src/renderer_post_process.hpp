#pragma once

#include "realm/post_effects.hpp"
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
  [[nodiscard]] std::size_t run_post(int width, int height, int clip_width, const ScreenEffects& effects);
  [[nodiscard]] std::size_t run_screen_overlays(int width, int height, int clip_width, const ScreenEffects& effects);

 private:
  std::uint32_t program_{}, vao_{};
  std::array<std::uint32_t, 2> textures_{}, framebuffers_{};
  int width_{}, height_{};
  int rect_{}, viewport_{}, mode_{}, axis_{}, blur_{}, blind_{}, region_{}, alpha_{}, sidebar_{};

  void release() noexcept;
};
}
