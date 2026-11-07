#pragma once

#include "realm/stage3D/DrawTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace realm {
class RendererStream {
 public:
  RendererStream(std::size_t capacity, bool persistent_streaming,
                 std::size_t persistent_minimum_bytes);
  ~RendererStream();
  RendererStream(const RendererStream&) = delete;
  RendererStream& operator=(const RendererStream&) = delete;

  void ensure_capacity(std::size_t total);
  // A frame's instances are written in two steps. begin_upload binds the frame's storage and
  // returns `bytes` writable bytes: the persistent mapping when its slot is free, else a staging
  // array. The caller packs the instances into that span, then end_upload completes an orphaned
  // (staged) upload and returns the buffer. Nothing else may bind GL_ARRAY_BUFFER in between.
  [[nodiscard]] std::span<std::byte> begin_upload(std::size_t bytes);
  [[nodiscard]] std::uint32_t end_upload();
  // The overlay buffer is always orphaned, so it is staged the same way.
  [[nodiscard]] std::span<std::byte> begin_overlay(std::size_t bytes);
  [[nodiscard]] std::uint32_t end_overlay();
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
  // The pending upload: its buffer, and whether it writes staging (orphaned) rather than the map.
  std::uint32_t pending_buffer_{};
  bool pending_orphan_{};
  std::vector<std::byte> staging_;
  std::vector<std::byte> overlay_staging_;

  void release();
};
}
