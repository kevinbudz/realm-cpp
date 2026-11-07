#pragma once
// The Vulkan renderer: the frame implementation behind the Renderer facade (gfx::RendererBackend).
// Its passes follow the GL frame: the world, the screen overlays and the HUD overlay, and the post
// effects through two offscreen targets when a filter is active. The CPU side of a submission
// (packing, runs) is shared with the GL renderer (DrawPlan.hpp).
#include "../DrawPlan.hpp"
#include "../GraphicsBackend.hpp"
#include "VkCommon.hpp"
#include "VkFrame.hpp"
#include "VkPipelines.hpp"
#include "realm/stage3D/vk/VkUi.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace realm {
// Runs before a frame is submitted: while presented capture is enabled, copies the image being
// presented into one retained device-local image (capture(presented) reads it back).
void record_presented_copy(VkCommandBuffer command);
void release_presented_copy() noexcept;
// REALM_VK_STATS=1: prints the high-water marks of the steady-state memory (rings, arena, surface
// sets).
void report_stats() noexcept;
// The renderer that owns the window's frames (null before it exists or after it is destroyed).
class VkRenderer;
[[nodiscard]] VkRenderer* active_renderer() noexcept;

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
  void poll_gpu() override;
  [[nodiscard]] std::size_t pending_gpu() const override;

  [[nodiscard]] std::size_t skipped_gpu() const override { return skipped_; }

  [[nodiscard]] double gpu_ms() const override { return gpu_ms_; }
  [[nodiscard]] bool persistent_streaming() const override { return true; }
  [[nodiscard]] Image capture(int width, int height, bool presented) const override;
  void set_presented_capture(bool enabled) override;

  // Vulkan has the firstInstance parameter in core, so runs always start at their own quad.
  void set_base_instance_draws(bool enabled) override { base_instance_ = enabled; }
  [[nodiscard]] bool base_instance_draws() const override { return base_instance_; }

  void finish_frame() override;

  // RmlUi (phase 3). ui_frame sets the window size of the frame's UI draws. ui_draw opens the UI pass
  // on demand and records the geometry. ui_close ends the pass; it must run before any transfer.
  void ui_frame(int width, int height);
  void ui_draw(const gfx::vk::ui::Draw& draw);
  void ui_close();
  // RmlUi layers and composites (see VkUi.hpp).
  [[nodiscard]] std::uint32_t ui_push_layer();
  void ui_pop_layer();
  void ui_composite(const gfx::vk::ui::Composite& composite);
  // The frame cache (see VkUi.hpp): replays the frame slot's recording, or records the draws and runs them.
  [[nodiscard]] bool ui_replay(std::uint64_t serial);
  void ui_record_bundle(const gfx::vk::ui::Draw* draws, std::size_t count, std::uint64_t serial);

 private:
  // GPU timers: one query pool per slot; a sample comes back from its slot once the slot's fence
  // has passed.
  static constexpr std::uint32_t timer_model_limit = 32;
  static constexpr std::uint32_t timer_queries = 5 + 2 * timer_model_limit;

  struct Timer {
    VkQueryPool pool = VK_NULL_HANDLE;
    bool pending = false;
    bool measuring = false;
    bool normal = false;
    std::uint64_t frame_id = 0;
    std::size_t models = 0;
    std::size_t models_total = 0;
  };

  std::array<Timer, gfx::vk::frames_in_flight> timers_{};
  Timer* active_timer_ = nullptr;
  telemetry::Recorder* recorder_ = nullptr;
  std::uint64_t sample_frame_ = 0;
  bool sample_normal_ = false;
  double gpu_ms_ = -1;
  std::size_t skipped_ = 0;
  [[nodiscard]] bool timers_supported() const;
  void begin_timing();
  void end_timing();
  void stamp(std::uint32_t index, VkPipelineStageFlagBits stage);
  void poll_timer(std::uint32_t slot);

  // capture(presented): the retained copy of the last presented image, or black (see
  // VkRenderer.cpp).
  [[nodiscard]] Image presented_image(int width, int height) const;

  // An offscreen colour target of the post passes (the swapchain format, so the passes are shared).
  struct Offscreen {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
  };

  std::array<float, 4> clear_color_{0, 0, 0, 1};
  bool base_instance_{true};
  std::size_t draw_calls_{};
  VkRenderPass clear_pass_ = VK_NULL_HANDLE;  // swapchain format; clears to the colour
  VkRenderPass load_pass_ = VK_NULL_HANDLE;   // swapchain format; keeps what is there
  VkPipeline strip_ = VK_NULL_HANDLE;         // quad instances as triangle strips
  VkPipeline list_ = VK_NULL_HANDLE;          // quad instances as triangle lists (wall runs)
  VkPipeline model_ = VK_NULL_HANDLE;
  VkPipeline mesh_ = VK_NULL_HANDLE;
  VkPipeline post_plain_ = VK_NULL_HANDLE;
  VkPipeline post_blend_ = VK_NULL_HANDLE;
  VkPipeline bound_ = VK_NULL_HANDLE;  // the pipeline the open pass has bound
  std::uint64_t framebuffer_generation_ = ~std::uint64_t{};
  std::vector<std::pair<VkImageView, VkFramebuffer>> framebuffers_;
  // Capture of the frame in flight: the same frame's second request reads the first one's pixels.
  mutable std::uint64_t captured_frame_ = ~std::uint64_t{};
  mutable Image captured_;
  // The submission being drawn: run data of the quads (then the overlay), its meshes, and the
  // buffers.
  std::vector<QuadMeta> meta_;
  draw_plan::MeshBatch meshes_;
  VkBuffer mesh_buffer_ = VK_NULL_HANDLE;  // the frame's ring: the submission's mesh vertices
  VkDeviceSize mesh_base_ = 0;
  VkBuffer walls_buffer_ = VK_NULL_HANDLE;  // set 0's wall corners for this submission
  VkDescriptorSet atlas_set_ = VK_NULL_HANDLE;
  std::uint32_t model_geometry_ = 0;  // arena range of every model's triangles
  // Walls: each slot has its own host-visible corner buffer, synced like GL's wall texture buffer.
  std::array<gfx::vk::Buffer, gfx::vk::frames_in_flight> walls_{};
  std::array<draw_plan::WallSync, gfx::vk::frames_in_flight> wall_sync_{};
  // Post targets: two offscreen images of the drawable size, rebuilt on resize.
  std::array<Offscreen, 2> post_{};
  VkExtent2D post_extent_{};

  [[nodiscard]] VkFramebuffer framebuffer(VkImageView view, VkExtent2D extent);
  void ensure_post_targets(std::uint32_t width, std::uint32_t height);
  // Uploads what a submission's layers need (outside any pass).
  void prepare_layers(std::span<const Quad> list);
  // Packs the quads and the overlay into the frame's ring, and the meshes and walls they draw.
  [[nodiscard]] gfx::vk::Ring::Span pack(std::span<const Quad> quads, std::span<const Quad> overlay);
  [[nodiscard]] VkBuffer upload_walls(const WallGeometryStore& store);
  // Begins a pass on a target (the swapchain, or an offscreen image), with the given scissor.
  void begin_pass(VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent, bool clear,
                  VkRect2D scissor, VkDescriptorSet scene, bool swapchain, bool clear_stencil = false);
  void end_pass();
  // Binds a pipeline when the open pass does not have it yet.
  void use(VkCommandBuffer command, VkPipeline pipeline);
  // Draws one painter-ordered list whose instances start at base in buffer; the same runs as GL's
  // draw_batch.
  void draw_list(VkCommandBuffer command, std::span<const Quad> list,
                 std::span<const QuadMeta> meta, VkBuffer buffer, VkDeviceSize base,
                 const gfx::vk::QuadPush& state);
  void push(VkCommandBuffer command, const gfx::vk::QuadPush& state);
  void bind_surface(VkCommandBuffer command, std::uint32_t texture);
  // The post passes: a filter from offscreen[0] (or [1] after the blur) into the target.
  void post_pass(VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent, bool clear,
                 bool swapchain, VkImageView source, VkRect2D scissor,
                 const gfx::vk::PostPush& values, bool blend);
  void barrier_to_sample(VkCommandBuffer command, VkImage image);
  // The vignette and the sidebar, drawn over whatever the open pass holds.
  void record_screen_overlays(VkCommandBuffer command, int width, int height, int clip_width,
                              const ScreenEffects& effects);
  bool swapchain_pass_ = false;  // the open pass writes the swapchain image
  // RmlUi (phase 3). A UI pass draws into the window or into the top layer. Passes are chosen by
  // (colour load, offscreen, stencil clear): an offscreen target ends sampleable (SHADER_READ_ONLY), the
  // window ends presentable. Each frame slot owns its stencil (clip masks), its two post targets (the
  // blur) and its layers, all window-sized and rebuilt on resize.
  struct UiImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };
  struct UiSlot {
    VkExtent2D extent{};
    UiImage stencil;             // D24S8
    UiImage post[2];             // the blur's two targets
    std::vector<UiImage> layers;  // layer k is handle k + 1
  };
  std::array<UiSlot, gfx::vk::frames_in_flight> ui_slots_{};
  VkRenderPass ui_passes_[2][2][2] = {};  // [clear colour][offscreen][clear stencil]
  VkPipeline ui_ = VK_NULL_HANDLE;        // plain triangles
  VkPipeline ui_clip_ = VK_NULL_HANDLE;   // test the clip mask
  VkPipeline ui_mask_replace_ = VK_NULL_HANDLE;
  VkPipeline ui_mask_increment_ = VK_NULL_HANDLE;
  VkPipeline ui_post_plain_ = VK_NULL_HANDLE;    // layer composites: copy, blur, upscale
  VkPipeline ui_post_premult_ = VK_NULL_HANDLE;  // layer composites with RmlUi's blend
  bool ui_open_ = false;     // a UI pass is open, on the top layer or the window
  bool ui_cleared_ = false;  // this frame's stencil has been cleared
  int ui_width_ = 0;
  int ui_height_ = 0;
  int ui_depth_ = 0;  // layers pushed
  struct UiFramebuffer {
    VkImageView color = VK_NULL_HANDLE;
    VkImageView stencil = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
  };
  std::vector<UiFramebuffer> ui_framebuffers_;
  std::uint64_t ui_generation_ = ~std::uint64_t{};

  [[nodiscard]] UiSlot& ui_slot(VkExtent2D extent);
  void ui_destroy_slot(UiSlot& slot) noexcept;
  [[nodiscard]] VkFramebuffer ui_framebuffer(VkImageView color, VkImageView stencil, VkExtent2D extent);
  void ui_reset_framebuffers() noexcept;
  // Begins a UI pass on framebuffer (the window when swapchain), and binds its sets. A secondary pass takes
  // secondary command buffers instead of inline commands and binds nothing (a recording binds its own state).
  void ui_begin(VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent, bool swapchain,
                bool secondary = false);
  // The frame cache (M4). A frame's queued draws are recorded once per frame slot into a secondary command
  // buffer (SIMULTANEOUS_USE, compatible with every UI pass) with their vertex and index bytes in the slot's own
  // host buffer. The slot's recording is rewritten only while the slot is being recorded, after its previous
  // frame has finished, so no frame in flight can read it: no retire is needed. A recording is replayed only for
  // the same serial (the draw list, bumped by RmlGpuVk), the same atlas set (per slot, rebuilt when its atlas or
  // walls change), the same swapchain generation and the same window size.
  struct UiBundle {
    VkCommandBuffer command = VK_NULL_HANDLE;
    gfx::vk::Buffer data;  // vertex and index bytes of the recorded draws (host-visible, grows only)
    bool valid = false;
    std::uint64_t serial = 0;
    VkDescriptorSet atlas = VK_NULL_HANDLE;
    std::uint64_t frame_generation = 0;
    VkExtent2D extent{};
    std::size_t draws = 0;  // draws recorded (those with geometry)
  };
  std::array<UiBundle, gfx::vk::frames_in_flight> ui_bundles_{};
  VkCommandPool ui_pool_ = VK_NULL_HANDLE;  // resettable pool of the slots' secondary command buffers

  // The frame slot's recording (the frame is opened).
  [[nodiscard]] UiBundle& ui_bundle();
  // Records one UI draw (the scissor, the stencil clip, the pipeline and the draw) into command. allocate
  // gives host memory for the vertex and index data (a ring span, or the recording's buffer).
  template <typename Allocate>
  void ui_record(VkCommandBuffer command, const gfx::vk::ui::Draw& draw, Allocate&& allocate);
  // Opens the stencil-clearing UI pass of the frame (or a later one), executes the recording, and ends the pass.
  void ui_run_bundle(const UiBundle& bundle, const gfx::vk::Target& target);
  // Opens the pass for the top target (the window or the top layer) if none is open.
  void ui_open(VkExtent2D extent);
  // One full resample: fills the target's rectangle from the source's, with the blur or a copy.
  void ui_resample(VkImageView target, bool offscreen, bool load, bool swapchain, VkExtent2D extent,
                   VkImageView source, const gfx::vk::UiPostPush& push, bool premultiplied);
  // RmlUi's blur (sigma split into downscales as RenderInterface_GL3 does) over a region; returns the
  // image that holds the result, one of the two post targets.
  [[nodiscard]] const UiImage* ui_blur(const UiImage* first, const UiImage* second, VkExtent2D extent,
                                       std::array<int, 4> region, float sigma);
};
}  // namespace realm
