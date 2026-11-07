#pragma once
// The WebGPU renderer: the frame implementation behind the Renderer facade (gfx::RendererBackend). Its
// passes follow the Vulkan frame (VkRenderer.cpp): the world, the screen overlays and the HUD overlay
// in one pass on the canvas; with a filter, the world renders into an offscreen target and the post
// passes resolve it into the canvas. The CPU side (packing, runs) is shared with the other backends
// (DrawPlan.hpp).
//
// Each draw binds its own uniform block (a Stream span) and the groups of WgpuPipelines.hpp, so no
// state carries over from one draw to the next except the open pass.
#include "../DrawPlan.hpp"
#include "../GraphicsBackend.hpp"
#include "WgpuContext.hpp"
#include "WgpuPipelines.hpp"
#include "WgpuResources.hpp"
#include "WgpuUi.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace realm::gfx::wgpu {
class WgpuRenderer final : public RendererBackend {
 public:
  WgpuRenderer(std::size_t capacity, bool persistent_streaming, std::size_t persistent_minimum_bytes);
  ~WgpuRenderer() override;
  WgpuRenderer(const WgpuRenderer&) = delete;
  WgpuRenderer& operator=(const WgpuRenderer&) = delete;

  void set_clear_color(std::array<float, 4> color) override { clear_color_ = color; }

  void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,
            std::span<const Quad> overlay, const ScreenEffects& effects) override;
  void draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width, int height) override;

  [[nodiscard]] std::size_t draw_calls() const override { return draw_calls_; }

  // No GPU timer queries in the browser: the recorder is never fed.
  void timing(telemetry::Recorder* /*recorder*/, std::uint64_t /*frame_id*/, bool /*normal*/) override {}
  void poll_gpu() override {}
  [[nodiscard]] std::size_t pending_gpu() const override { return 0; }
  [[nodiscard]] std::size_t skipped_gpu() const override { return 0; }
  [[nodiscard]] double gpu_ms() const override { return -1; }
  [[nodiscard]] bool persistent_streaming() const override { return true; }
  [[nodiscard]] Image capture(int width, int height, bool presented) const override;

  // Presented capture needs a readback of the canvas, which the browser cannot do synchronously.
  void set_presented_capture(bool /*enabled*/) override {}
  void set_base_instance_draws(bool enabled) override { base_instance_ = enabled; }
  [[nodiscard]] bool base_instance_draws() const override { return base_instance_; }

  // Submits the frame. The browser presents the canvas when the animation frame returns.
  void finish_frame() override;

  // RmlUi's WebGPU passes (WgpuUi.hpp). They run between draw() and draw_overlay(), as the Vulkan UI pass does.
  [[nodiscard]] WgpuUi& rml_ui() { return rml_ui_; }

 private:
  struct Offscreen {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
  };

  std::array<float, 4> clear_color_{0, 0, 0, 1};
  bool base_instance_{true};
  std::size_t draw_calls_{};
  mutable bool capture_warned_ = false;

  Stream stream_;
  WgpuUi rml_ui_;
  std::uint32_t model_geometry_ = 0;
  // The frame's walls: a texture of wall corner texels (1024 texels wide), synced like GL's buffer.
  draw_plan::WallSync wall_sync_{};
  WGPUTexture walls_texture_ = nullptr;
  WGPUTextureView walls_view_ = nullptr;
  std::uint64_t walls_rows_ = 0;
  // The filter's offscreen targets (the canvas format), rebuilt on resize.
  std::array<Offscreen, 2> post_{};
  std::uint32_t post_width_ = 0;
  std::uint32_t post_height_ = 0;

  // The submission being drawn: its quads' run data, the meshes, the group-0 bind group, and the
  // stream spans of its instances and mesh vertices.
  std::vector<QuadMeta> meta_;
  std::vector<std::byte> instance_bytes_;
  draw_plan::MeshBatch meshes_;
  WGPUBindGroup atlas_group_ = nullptr;
  WGPUTextureView frame_walls_ = nullptr;
  Stream::Span mesh_span_{};

  // Records a submission's pass (on the canvas or an offscreen target).
  [[nodiscard]] WGPURenderPassEncoder begin_pass(WGPUTextureView target, bool clear);
  static void end_pass(WGPURenderPassEncoder pass);
  // Sets the scissor of a pass to a rectangle of its target (top-left origin).
  static void scissor(WGPURenderPassEncoder pass, int x, int y, int width, int height);
  // Binds what a quad-family draw reads and writes its uniform block for this draw.
  void bind_draw(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline, std::uint32_t surface,
                 const QuadUniform& state);
  // Binds a post pass's scene group and writes its uniform block, then draws the screen strip.
  void post_draw(WGPURenderPassEncoder pass, bool blend, WGPUBindGroup scene, const PostUniform& values);
  // One full-screen post pass from source into target (clear or load), scissored to rect.
  void post_pass(WGPUTextureView target, bool clear, WGPUTextureView source, int width, int height,
                 const PostUniform& values, bool blend);
  // Draws one painter-ordered list whose instances start at base in buffer; the same runs as GL's
  // draw_batch.
  void draw_list(WGPURenderPassEncoder pass, std::span<const Quad> list, std::span<const QuadMeta> meta,
                 WGPUBuffer buffer, std::uint64_t base, const QuadUniform& state);
  // The vignette and the sidebar, drawn over whatever the open pass holds.
  void record_screen_overlays(WGPURenderPassEncoder pass, int width, int height, int clip_width,
                              const ScreenEffects& effects);
  // Uploads what a submission's layers need (their instances, as static buffers).
  void prepare_layers(std::span<const Quad> list);
  // Packs the quads and the overlay into the stream, and the meshes and walls they draw.
  [[nodiscard]] Stream::Span pack(std::span<const Quad> quads, std::span<const Quad> overlay);
  [[nodiscard]] WGPUTextureView upload_walls(const WallGeometryStore& store);
  void write_wall_texels(std::uint64_t first, std::uint64_t count, const std::byte* data);
  // The atlas group for the frame's walls, for one submission (released with it).
  void begin_submission(const Atlas& atlas);
  void end_submission() noexcept;
  void ensure_post_targets(std::uint32_t width, std::uint32_t height);
};

// The renderer that owns the canvas (null before it exists or after it is destroyed).
[[nodiscard]] WgpuRenderer* active_renderer() noexcept;
}  // namespace realm::gfx::wgpu
