#pragma once
// The Vulkan renderer: the frame implementation behind the Renderer facade (gfx::RendererBackend).
// Its passes mirror the GL frame: the world (clear), the overlay on top of it, and draw_overlay's
// own pass. The CPU side of a submission (packing, runs) is shared with the GL renderer (DrawPlan.hpp).
#include "../DrawPlan.hpp"
#include "../GraphicsBackend.hpp"
#include "VkCommon.hpp"
#include "VkFrame.hpp"
#include "VkPipelines.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace realm {
class VkRenderer final : public gfx::RendererBackend {
 public:
  VkRenderer(std::size_t capacity, bool persistent_streaming, std::size_t persistent_minimum_bytes);
  ~VkRenderer() override;
  VkRenderer(const VkRenderer&) = delete;
  VkRenderer& operator=(const VkRenderer&) = delete;

  void set_clear_color(std::array<float, 4> color) override { clear_color_ = color; }

  void draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,
            std::span<const Quad> overlay, const ScreenEffects& effects) override;
  void draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width,
                    int height) override;

  [[nodiscard]] std::size_t draw_calls() const override { return draw_calls_; }

  void timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) override;
  void poll_gpu() override {}
  [[nodiscard]] std::size_t pending_gpu() const override { return 0; }
  [[nodiscard]] std::size_t skipped_gpu() const override { return 0; }
  [[nodiscard]] double gpu_ms() const override { return -1; }
  [[nodiscard]] bool persistent_streaming() const override { return true; }
  [[nodiscard]] Image capture(int width, int height, bool presented) const override;

  // Vulkan has the firstInstance parameter in core, so runs always start at their own quad.
  void set_base_instance_draws(bool enabled) override { base_instance_ = enabled; }
  [[nodiscard]] bool base_instance_draws() const override { return base_instance_; }

  void finish_frame() override;

 private:
  std::array<float, 4> clear_color_{0, 0, 0, 1};
  bool base_instance_{true};
  std::size_t draw_calls_{};
  VkRenderPass clear_pass_ = VK_NULL_HANDLE;  // swapchain format; clears to the colour
  VkRenderPass load_pass_ = VK_NULL_HANDLE;   // swapchain format; keeps what is there
  VkPipeline strip_ = VK_NULL_HANDLE;         // quad instances as triangle strips
  std::uint64_t framebuffer_generation_ = ~std::uint64_t{};
  std::vector<std::pair<VkImageView, VkFramebuffer>> framebuffers_;
  // Capture of the frame in flight: the same frame's second request reads the first one's pixels.
  mutable std::uint64_t captured_frame_ = ~std::uint64_t{};
  mutable Image captured_;
  // The submission being drawn: run data of the quads (then the overlay), and its meshes.
  std::vector<QuadMeta> meta_;
  draw_plan::MeshBatch meshes_;

  [[nodiscard]] VkFramebuffer framebuffer(VkImageView view, VkExtent2D extent);
  // Uploads what a submission's layers need (outside any pass).
  void prepare_layers(std::span<const Quad> list);
  // Packs the quads and the overlay into the frame's ring; fills meta_ and meshes_.
  [[nodiscard]] gfx::vk::Ring::Span pack(std::span<const Quad> quads, std::span<const Quad> overlay);
  // Begins a pass on the frame's target, with the window viewport and the given scissor.
  void begin_pass(VkRenderPass pass, bool clear, std::uint32_t width, std::uint32_t height,
                  VkRect2D scissor);
  void end_pass();
  // Draws one painter-ordered list whose instances start at base in buffer; the same runs as GL's
  // draw_batch.
  void draw_list(VkCommandBuffer command, std::span<const Quad> list,
                 std::span<const QuadMeta> meta, VkBuffer buffer, VkDeviceSize base,
                 const gfx::vk::QuadPush& state);
  void push(VkCommandBuffer command, const gfx::vk::QuadPush& state);
  void bind_surface(VkCommandBuffer command, std::uint32_t texture);
  VkDescriptorSet atlas_set_ = VK_NULL_HANDLE;
};
}  // namespace realm
