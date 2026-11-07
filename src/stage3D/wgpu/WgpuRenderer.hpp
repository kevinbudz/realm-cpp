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
#include "WgpuTimers.hpp"
#include "WgpuUi.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace realm::gfx::wgpu {
// A presented capture's map result, written by the map callback (which may run after the renderer is
// gone, so it writes through a holder that owns a reference to this).
struct ReadbackMap {
  bool done = false;
  WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
};

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

  // Whole-frame GPU time through timestamp queries, when the device has them (WgpuTimers.hpp).
  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) override {
    timers_.timing(recorder, frame_id, normal);
  }
  void set_gpu_timing(bool enabled) override { timers_.set_enabled(enabled); }
  void poll_gpu() override { timers_.poll(); }
  [[nodiscard]] std::size_t pending_gpu() const override { return timers_.pending(); }
  [[nodiscard]] std::size_t skipped_gpu() const override { return timers_.skipped(); }
  [[nodiscard]] double gpu_ms() const override { return timers_.gpu_ms(); }
  [[nodiscard]] bool persistent_streaming() const override { return true; }
  // capture(presented) starts an asynchronous readback of the last presented frame and returns a
  // transparent image of the requested size; take_capture() returns the frame once it is mapped.
  [[nodiscard]] Image capture(int width, int height, bool presented) const override;
  void set_presented_capture(bool enabled) override;
  [[nodiscard]] std::optional<Image> take_capture() override;
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
  WgpuTimers timers_;
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

  // Presented capture: while enabled, finish_frame copies the frame's swapchain texture into
  // last_frame_ (kept when disabled, so the death screen can read it).
  bool presented_capture_ = false;
  WGPUTexture last_frame_ = nullptr;
  std::uint32_t last_frame_width_ = 0;
  std::uint32_t last_frame_height_ = 0;
  // The readback that capture(presented) started, until take_capture() returns it.
  struct Readback {
    WGPUBuffer buffer = nullptr;
    std::shared_ptr<ReadbackMap> map;
    int width = 0;
    int height = 0;
    std::uint32_t row_bytes = 0;  // bytesPerRow, padded to 256
  };
  mutable std::optional<Readback> readback_;

  // The submission being drawn: its quads' run data and the meshes. The atlas group is cached across
  // submissions (keyed by the atlas and walls views it names); the stream spans are the frame's.
  std::vector<QuadMeta> meta_;
  draw_plan::MeshBatch meshes_;
  WGPUBindGroup atlas_group_ = nullptr;
  WGPUTextureView atlas_group_atlas_ = nullptr;
  WGPUTextureView atlas_group_walls_ = nullptr;
  WGPUTextureView frame_walls_ = nullptr;
  Stream::Span mesh_span_{};
  // Scene groups of the post targets, by index into post_ (rebuilt with the targets).
  std::array<WGPUBindGroup, 2> scene_groups_{};
  // Layer buffers that the frame's encoder already draws from: a change to one of these is written into a
  // new buffer object, since a queued write would reach the earlier draws of the same frame.
  std::vector<std::uint32_t> recorded_layers_;

  // What the open pass has bound, so a draw skips the calls that would not change it. Reset by begin_pass.
  struct Slot {
    WGPUBindGroup group = nullptr;
    std::uint32_t offset = 0;
    bool dynamic = false;
  };
  WGPURenderPipeline bound_pipeline_ = nullptr;
  std::array<Slot, 3> bound_slots_{};

  // Records a submission's pass (on the canvas or an offscreen target). A new pass binds nothing.
  [[nodiscard]] WGPURenderPassEncoder begin_pass(WGPUTextureView target, bool clear);
  static void end_pass(WGPURenderPassEncoder pass);
  // Sets the scissor of a pass to a rectangle of its target (top-left origin).
  static void scissor(WGPURenderPassEncoder pass, int x, int y, int width, int height);
  // Binds a pipeline or a group slot of the open pass, unless it is already bound there.
  void set_pipeline(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline);
  void set_group(WGPURenderPassEncoder pass, std::uint32_t slot, WGPUBindGroup group, std::uint32_t offset,
                 bool dynamic);
  // Binds what a quad-family draw reads and writes its uniform block for this draw.
  void bind_draw(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline, std::uint32_t surface,
                 const QuadUniform& state);
  // Binds a post pass's scene group and writes its uniform block, then draws the screen strip.
  void post_draw(WGPURenderPassEncoder pass, bool blend, WGPUBindGroup scene, const PostUniform& values);
  // The scene group of post target source (a post_ index), cached until the targets are rebuilt.
  [[nodiscard]] WGPUBindGroup scene_group(std::size_t source);
  void release_scene_groups() noexcept;
  // One full-screen post pass from post_[source] into target (clear or load), scissored to rect.
  void post_pass(WGPUTextureView target, bool clear, std::size_t source, int width, int height,
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
  // Makes the atlas group current for the frame's walls: reused while the atlas and walls views are the same.
  void begin_submission(const Atlas& atlas);
  void release_atlas_group() noexcept;
  void ensure_post_targets(std::uint32_t width, std::uint32_t height);
  // Copies the frame's swapchain texture into last_frame_ (its size is the frame's).
  void copy_presented_frame();
  // Starts the readback of last_frame_ into a buffer, when it has the requested size.
  void start_readback(int width, int height) const;
  // Releases the readback's buffer (a pending map is cancelled; its callback still runs safely).
  void drop_readback() const noexcept;
};

// The renderer that owns the canvas (null before it exists or after it is destroyed).
[[nodiscard]] WgpuRenderer* active_renderer() noexcept;
}  // namespace realm::gfx::wgpu
