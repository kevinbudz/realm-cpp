#pragma once
#include "realm/stage3D/Atlas.hpp"
#include "realm/util/Telemetry.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include "realm/map/PostEffects.hpp"
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
  std::string vendor;
  std::string renderer;
  std::string version;
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

  void update_region(const Image& image, int x, int y, int width, int height,
                     bool premultiplied = false);
  [[nodiscard]] Quad quad(Vec2 origin, Vec2 axis_x, Vec2 axis_y, int x = 0, int y = 0,
                          int width = 0, int height = 0) const;

 private:
  std::uint32_t texture_{};
  int width_{};
  int height_{};
  int storage_width_{};
  int storage_height_{};
  int reserved_width_{};
  int reserved_height_{};
  bool retain_capacity_{};
  int texture_limit_{};
};

// Retained, chunk-local ground instances. Camera motion changes only the command's affine
// transform.
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
  // Extra overlay quads after another GL pass (e.g. RmlUi's) in the same frame: re-establishes
  // every GL state it uses and draws over the whole window. Quads and draw calls are counted like
  // draw()'s.
  void draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width, int height);

  // Resolves to the default framebuffer; the world is rendered offscreen only while a filter is
  // active.
  [[nodiscard]] std::size_t draw_calls() const { return draw_calls_; }

  // Timing is opt-in. The fixed query ring skips busy slots; never waits.
  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal);
  void poll_gpu();
  [[nodiscard]] std::size_t pending_gpu() const;
  [[nodiscard]] std::size_t skipped_gpu() const;
  [[nodiscard]] double gpu_ms() const;
  [[nodiscard]] bool persistent_streaming() const;
  [[nodiscard]] Image capture(int width, int height, bool presented = false) const;

  // Runs read their instances from their first quad (GL 4.2 base instance) instead of re-pointing
  // attributes per run. Only enabled where supported; false selects the per-run path, which renders
  // the same pixels.
  void set_base_instance_draws(bool enabled) {
    base_instance_ = enabled && base_instance_supported_;
  }

  [[nodiscard]] bool base_instance_draws() const { return base_instance_; }

 private:
  std::array<float, 4> clear_color_{0, 0, 0, 1};
  bool base_instance_supported_{};
  bool base_instance_{};
  std::uint32_t program_{};
  std::uint32_t vao_{};
  std::uint32_t mesh_program_{};
  std::uint32_t mesh_vao_{};
  std::uint32_t mesh_buffer_{};
  std::uint32_t model_program_{};
  std::uint32_t model_vao_{};
  std::uint32_t model_geometry_{};
  std::unique_ptr<RendererPostProcess> post_process_;
  std::unique_ptr<RendererGpuTimers> gpu_timers_;
  std::unique_ptr<RendererStream> stream_;
  std::size_t draw_calls_{};

  // Prepared meshes (kind 9 with a MeshDraw owner) are expanded per submission into one buffer, in
  // painter order. Each vertex carries its mesh's sprite and color and its group's material, so a
  // run of meshes is one draw.
  struct MeshBatchVertex {
    float x, y, u, v, shade;
    std::array<float, 4> bounds;
    float page;
    std::array<float, 3> color;
    float textured;
    float repeat;
  };

  struct MeshBatchRange {
    std::size_t first;
    std::size_t count;
  };

  std::vector<MeshBatchVertex> mesh_vertices_;
  std::vector<MeshBatchRange>
      mesh_ranges_;  // One per mesh quad, in the order draw_batch visits them.
  std::size_t mesh_cursor_{};
  // Wall instances (kind 17) fetch their corners from a texture buffer (RGBA32F, two texels per
  // corner) mirroring the submission's WallGeometryStore: a new generation uploads everything,
  // appended corners upload as a tail, and growth reallocates with headroom.
  std::uint32_t wall_geometry_{};
  std::uint32_t wall_texture_{};
  int wall_texel_limit_{};
  std::size_t wall_capacity_{};
  std::size_t wall_uploaded_{};  // corners
  std::uint64_t wall_generation_{};
  void find_walls(std::span<const Quad> list, const WallGeometryStore*& store);
  void bind_walls();
  void upload_walls(const WallGeometryStore& store);

  // Draws a painter-ordered quad list whose bytes start at offset in buffer.
  void draw_batch(std::span<const Quad> list, std::size_t offset, int width, int height,
                  std::uint32_t buffer, bool measure_models);
  void bind_quad_attributes(std::size_t offset);
  void draw_instanced(std::uint32_t mode, std::int32_t first_vertex, std::int32_t vertices,
                      std::int32_t instances, std::size_t first_instance) const;
  void collect_meshes(std::span<const Quad> list);
  void upload_meshes(std::span<const Quad> quads, std::span<const Quad> overlay);
  void release_gl_resources() noexcept;
  int viewport_location_{};
  int model_viewport_{};
  int mesh_viewport_{};
  int triangle_list_location_{};
  int standalone_location_{};
  int surface_location_{};
  int ground_location_{};
  int ground_origin_{};
  int ground_x_{};
  int ground_y_{};
  int ground_time_{};
  int clip_location_{};
  int clip_rect_{};
  int clip_cut_{};
};
}
