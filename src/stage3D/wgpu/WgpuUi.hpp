#pragma once
// RmlUi's WebGPU passes (the seam is realm/stage3D/wgpu/WebGpuUi.hpp), owned by WgpuRenderer. A UI pass is an
// open render pass on the canvas or on a layer; draws that target the same view share it, and any other pass
// (a layer push or pop, a composite, a texture copy) closes it first. Every pass carries the stencil of the clip
// masks, so one pipeline set serves the window and the layers. The passes record into the frame's encoder, in
// order: the world's passes before them, the HUD overlay's after.
#include "realm/stage3D/wgpu/WebGpuUi.hpp"
#include "WgpuContext.hpp"
#include "WgpuResources.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace realm::gfx::wgpu {
// The uniform block of every UI draw and post pass: the 176-byte WGSL struct UiBlock (WgpuShaders.cpp). The
// geometry draws read transform, translate and textured; the post passes read the rest.
struct UiBlock {
  float transform[16] = {};   // 0: column-major clip transform
  float translate[2] = {};    // 64
  std::int32_t textured = 0;  // 72
  float opacity = 1;          // 76
  float dst_rect[4] = {};     // 80: x0, y0, x1, y1 in window pixels
  float src_rect[4] = {};     // 96: u0, v0, u1, v1
  float step_uv[2] = {};      // 112: uv step of one blur tap
  float pad0[2] = {};         // 120
  float clamp_rect[4] = {};   // 128: blur region in uv
  float weights[4] = {};      // 144: blur weights for tap distance 0..3
  float window_size[2] = {};  // 160
  std::int32_t blur = 0;      // 168
  std::int32_t pad1 = 0;      // 172
};
static_assert(sizeof(UiBlock) == 176);
inline constexpr std::uint64_t ui_block_bytes = 176;

class WgpuUi {
 public:
  WgpuUi() = default;
  ~WgpuUi();
  WgpuUi(const WgpuUi&) = delete;
  WgpuUi& operator=(const WgpuUi&) = delete;

  // Creates the shader, layouts and pipelines for the canvas format (the device and pipelines() exist).
  void create(WGPUTextureFormat format);
  // Releases everything. The frame must have been submitted (WgpuRenderer closes the pass first).
  void destroy() noexcept;

  // Opens a UI frame of a window size: the targets follow it, and the stencil clears on the frame's first pass.
  void begin_frame(int width, int height);
  void end_frame() noexcept { close(); }
  // The frame was submitted: its stream data is free to be overwritten by the next frame's writes.
  void frame_submitted() noexcept { stream_.reset(); }
  // Ends the open UI pass, if any. Called before any other pass of the frame.
  void close() noexcept;

  void draw(const ui::Draw& draw);
  [[nodiscard]] std::uint32_t push_layer();
  void pop_layer();
  void composite(const ui::Composite& composite);

  // Writes a surface's pixels in frame order: a copy is recorded into the frame's encoder, so the draws recorded
  // before it keep the old pixels. The caller closes the open pass first.
  static void write_surface(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba);

 private:
  struct Target {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
  };

  WGPUTextureFormat format_ = WGPUTextureFormat_BGRA8Unorm;
  WGPUShaderModule module_ = nullptr;
  WGPUBindGroupLayout uniform_layout_ = nullptr;
  WGPUPipelineLayout layout_ = nullptr;
  // Geometry pipelines by clip: none, test, mask (set and set inverse), increment.
  std::array<WGPURenderPipeline, 4> geometry_{};
  // Post pipelines: plain (replace) and premultiplied (blend).
  std::array<WGPURenderPipeline, 2> post_{};
  Stream stream_;  // the frame's UiBlock, vertex and index data

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  WGPUTexture stencil_texture_ = nullptr;
  WGPUTextureView stencil_view_ = nullptr;
  std::array<Target, 2> filter_targets_{};  // the filters' working images
  std::vector<Target> layers_;              // layer k is layers_[k - 1] of the stack
  std::size_t depth_ = 0;                   // layers pushed and not popped
  bool stencil_cleared_ = false;            // the frame's first pass has cleared the stencil

  WGPURenderPassEncoder pass_ = nullptr;
  WGPUTextureView pass_view_ = nullptr;

  [[nodiscard]] bool ready() const { return width_ > 0 && height_ > 0 && stencil_view_ != nullptr; }
  // The window (the frame's swapchain view) or the top layer.
  [[nodiscard]] WGPUTextureView current_view();
  // Opens a pass on view (cleared, or kept) unless the open pass already targets it.
  void ensure_pass(WGPUTextureView view, bool clear);
  [[nodiscard]] WGPURenderPassEncoder begin_pass(WGPUTextureView view, bool clear);
  // Sets the group 1 uniform block of the next draw, and the surface group 0.
  void bind_block(WGPURenderPassEncoder pass, WGPUBindGroup surface, const UiBlock& block);
  // One full-screen post pass from source into target (cleared, or kept), in a pass of its own.
  void resample(WGPUTextureView target, bool clear, WGPUTextureView source, const UiBlock& values,
                bool premultiplied);
  // RenderInterface_GL3's blur split of the filter target `first` over region; returns the target holding the result.
  [[nodiscard]] std::size_t blur(std::size_t first, std::array<int, 4> region, float sigma);
  void release_targets() noexcept;
  [[nodiscard]] Target make_target(std::uint32_t width, std::uint32_t height) const;
};
}  // namespace realm::gfx::wgpu
