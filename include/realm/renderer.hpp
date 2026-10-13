#pragma once
#include "realm/atlas.hpp"
#include "realm/telemetry.hpp"
#include "realm/draw_types.hpp"
#include "realm/post_effects.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <memory>
#include <vector>

namespace realm {
class RendererPostProcess;
class RendererGpuTimers;
class RendererStream;

// Queried once after the platform adapter has made a GL context current.
struct GraphicsInfo {
  std::string vendor, renderer, version;
};
[[nodiscard]] GraphicsInfo graphics_info();
void check_graphics_error();

// Resizing a surface replaces only its own storage, never the world atlas.
class SurfaceTexture {
 public:
  ~SurfaceTexture();
  explicit SurfaceTexture(bool retain_capacity = false) : retain_capacity_(retain_capacity) {}
  SurfaceTexture(const SurfaceTexture&) = delete;
  SurfaceTexture& operator=(const SurfaceTexture&) = delete;
  void update(const Image& image, bool premultiplied = false);
  // Animated content can reserve its full size range before the first upload.
  void reserve(int width, int height) {
    reserved_width_ = width;
    reserved_height_ = height;
  }
  void update_region(const Image& image, int x, int y, int width, int height, bool premultiplied = false);
  [[nodiscard]] Quad quad(Vec2 origin, Vec2 axis_x, Vec2 axis_y, int x = 0, int y = 0, int width = 0,
                          int height = 0) const;

 private:
  std::uint32_t texture_{};
  int width_{}, height_{}, storage_width_{}, storage_height_{};
  int reserved_width_{}, reserved_height_{};
  bool retain_capacity_{};
  int texture_limit_{};
};

// Retained, chunk-local ground instances. Camera motion changes only the command's affine transform.
class TerrainBuffer {
 public:
  explicit TerrainBuffer(std::span<const Quad> quads);
  ~TerrainBuffer();
  TerrainBuffer(const TerrainBuffer&) = delete;
  TerrainBuffer& operator=(const TerrainBuffer&) = delete;
  [[nodiscard]] std::uint32_t buffer() const { return buffer_; }
  [[nodiscard]] std::size_t count() const { return count_; }

 private:
  std::uint32_t buffer_{};
  std::size_t count_{};
};
// A clipped overlay kept together in painter order (minimap map, markers, arrow and buttons).
struct QuadLayer {
  QuadLayer() = default;
  QuadLayer(const QuadLayer&) = delete;
  QuadLayer& operator=(const QuadLayer&) = delete;
  ~QuadLayer();
  std::vector<Quad> quads;
  std::uint32_t buffer{};
};

class Renderer {
 public:
  explicit Renderer(std::size_t capacity, bool persistent_streaming = true,
                    std::size_t persistent_minimum_bytes = 64 * 1024);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  void clear_color(std::array<float, 4> color) { clear_color_ = color; }
  void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,
            std::span<const Quad> overlay = {}, const ScreenEffects& effects = {});
  // Extra overlay quads after another GL pass (e.g. RmlUi's) in the same frame: re-establishes every GL state it uses
  // and draws over the whole window. Quads and draw calls are counted like draw()'s.
  void draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width, int height);
  // Resolves to the default framebuffer; the world is rendered offscreen only while a filter is active.
  [[nodiscard]] std::size_t draw_calls() const { return draw_calls_; }
  // Timing is opt-in. The fixed query ring skips busy slots; never waits.
  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal);
  void poll_gpu();
  [[nodiscard]] std::size_t pending_gpu() const;
  [[nodiscard]] std::size_t skipped_gpu() const;
  [[nodiscard]] double gpu_ms() const;
  [[nodiscard]] bool persistent_streaming() const;
  [[nodiscard]] Image capture(int width, int height, bool presented = false) const;

 private:
  std::array<float, 4> clear_color_{0, 0, 0, 1};
  std::uint32_t program_{}, vao_{}, mesh_program_{}, mesh_vao_{}, mesh_buffer_{}, model_program_{}, model_vao_{},
      model_geometry_{};
  std::unique_ptr<RendererPostProcess> post_process_;
  std::unique_ptr<RendererGpuTimers> gpu_timers_;
  std::unique_ptr<RendererStream> stream_;
  std::size_t draw_calls_{};

  // Draws a painter-ordered quad list whose bytes start at offset in buffer.
  void draw_batch(std::span<const Quad> list, std::size_t offset, int width, int height, std::uint32_t buffer,
                  bool measure_models);
  void release_gl_resources() noexcept;
  int viewport_location_{}, model_viewport_{}, mesh_viewport_{}, mesh_bounds_{}, mesh_color_{}, mesh_page_{},
      mesh_textured_{};
  int standalone_location_{}, surface_location_{}, ground_location_{}, ground_origin_{}, ground_x_{}, ground_y_{},
      ground_time_{}, clip_location_{}, clip_rect_{};
};
}
