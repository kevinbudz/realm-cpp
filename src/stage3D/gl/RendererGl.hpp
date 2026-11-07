#pragma once
// The OpenGL renderer: the frame implementation behind the Renderer facade (gfx::RendererBackend).
// Its members are GL object names and uniform locations, so nothing outside gl/ sees them.
#include "../GraphicsBackend.hpp"
#include "RendererGpuTimers.hpp"
#include "RendererPostProcess.hpp"
#include "RendererStream.hpp"
#include "../DrawPlan.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace realm {
class RendererGl final : public gfx::RendererBackend {
 public:
  RendererGl(std::size_t capacity, bool persistent_streaming, std::size_t persistent_minimum_bytes);
  ~RendererGl() override;
  RendererGl(const RendererGl&) = delete;
  RendererGl& operator=(const RendererGl&) = delete;

  void set_clear_color(std::array<float, 4> color) override { clear_color_ = color; }
  void finish_frame() override {}

  void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,
            std::span<const Quad> overlay, const ScreenEffects& effects) override;
  void draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width,
                    int height) override;

  [[nodiscard]] std::size_t draw_calls() const override { return draw_calls_; }

  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) override;
  void set_gpu_timing(bool enabled) override;
  void poll_gpu() override;
  [[nodiscard]] std::size_t pending_gpu() const override;
  [[nodiscard]] std::size_t skipped_gpu() const override;
  [[nodiscard]] double gpu_ms() const override;
  [[nodiscard]] bool persistent_streaming() const override;
  [[nodiscard]] Image capture(int width, int height, bool presented) const override;

  void set_base_instance_draws(bool enabled) override {
    base_instance_ = enabled && base_instance_supported_;
  }

  [[nodiscard]] bool base_instance_draws() const override { return base_instance_; }

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

  // Prepared meshes (kind 9 with a MeshDraw owner), expanded per submission in painter order; see
  // DrawPlan.hpp.
  draw_plan::MeshBatch meshes_;
  std::vector<QuadMeta> meta_;  // run data of the submission being drawn (quads, then overlay)
  // Wall instances (kind 17) fetch their corners from a texture buffer (RGBA32F, two texels per
  // corner) mirroring the submission's WallGeometryStore: a new generation uploads everything,
  // appended corners upload as a tail, and growth reallocates with headroom.
  std::uint32_t wall_geometry_{};
  std::uint32_t wall_texture_{};
  int wall_texel_limit_{};
  draw_plan::WallSync walls_;
  void bind_walls();
  void upload_walls(const WallGeometryStore& store);

  // Draws a painter-ordered quad list whose instances start at offset (bytes) in buffer. meta
  // holds the run data of list's quads, in order.
  void draw_batch(std::span<const Quad> list, std::span<const QuadMeta> meta, std::size_t offset,
                  int width, int height, std::uint32_t buffer, bool measure_models);
  void bind_quad_attributes(std::size_t offset);
  void draw_instanced(std::uint32_t mode, std::int32_t first_vertex, std::int32_t vertices,
                      std::int32_t instances, std::size_t first_instance) const;
  // Packs and uploads one submission (quads, then overlay). Returns the stream buffer.
  [[nodiscard]] std::uint32_t upload_submission(std::span<const Quad> quads,
                                                std::span<const Quad> overlay);
  void upload_mesh_vertices();
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

  // Uniform values last set on each program (-1, or {-1, -1}, when unknown), so an unchanged uniform
  // skips its glUniform call. The programs are linked once in the constructor and only this renderer
  // sets their uniforms, so the caches start empty and are never invalidated.
  int cache_standalone_{-1};
  int cache_ground_{-1};
  int cache_clip_{-1};
  int cache_surface_{-1};
  int cache_triangle_list_{-1};
  std::array<float, 2> cache_quad_viewport_{-1.0f, -1.0f};
  std::array<float, 2> cache_mesh_viewport_{-1.0f, -1.0f};
  std::array<float, 2> cache_model_viewport_{-1.0f, -1.0f};
  // The mesh vertices last uploaded to mesh_buffer_, which holds exactly those.
  std::vector<draw_plan::MeshBatchVertex> uploaded_mesh_vertices_;
};
}  // namespace realm
