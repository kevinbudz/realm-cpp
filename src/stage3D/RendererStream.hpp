#pragma once

#include "realm/stage3D/DrawTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace realm {
class RendererStream {
 public:
  RendererStream(std::size_t capacity, bool persistent_streaming,
                 std::size_t persistent_minimum_bytes);
  ~RendererStream();
  RendererStream(const RendererStream&) = delete;
  RendererStream& operator=(const RendererStream&) = delete;

  void ensure_capacity(std::size_t total);
  [[nodiscard]] std::uint32_t upload(std::span<const Quad> quads, std::span<const Quad> overlay);
  [[nodiscard]] std::uint32_t upload_overlay(std::span<const Quad> overlay);
  void finish_frame(std::uint32_t buffer);

  [[nodiscard]] bool persistent() const { return persistent_; }

 private:
  static constexpr std::size_t slots = 3;
  std::array<std::uint32_t, slots> buffers_{};
  std::array<void*, slots> mapped_{};
  std::array<void*, slots> fences_{};
  std::array<std::size_t, slots> capacities_{};
  std::uint32_t fallback_buffer_{};
  std::uint32_t overlay_buffer_{};
  bool persistent_{};
  std::size_t minimum_bytes_{};
  std::size_t capacity_{};
  std::size_t frame_{};

  void release();
};
}
