#pragma once
// Shaders, descriptor layouts, the pipeline layout and the descriptor sets of the draw programs:
// quads, models, prepared meshes and the post-effect pass.
#include "VkCommon.hpp"
#include "VkFrame.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace realm::gfx::vk {
// The push constants of the programs: the std430 layout of the GLSL block in the shaders (84
// bytes). Changing one needs the others.
struct QuadPush {
  float viewport[2] = {0, 0};        // 0
  float ground_origin[2] = {0, 0};   // 8
  float ground_x[2] = {0, 0};        // 16
  float ground_y[2] = {0, 0};        // 24
  float ground_time = 0;             // 32
  float pad[3] = {0, 0, 0};          // 36
  float clip_rect[4] = {0, 0, 0, 0}; // 48
  float clip_cut = 0;                // 64
  std::int32_t standalone = 0;       // 68: the surface (set 1) instead of the atlas
  std::int32_t triangle_list = 0;    // 72: wall runs
  std::int32_t ground_space = 0;     // 76: terrain
  std::int32_t clipped = 0;          // 80: a clipped layer
};
static_assert(sizeof(QuadPush) == 84);
static_assert(offsetof(QuadPush, clip_rect) == 48);

// The push constants of the post-effect pass (the same 84-byte range, a different block).
struct PostPush {
  float viewport[2] = {0, 0};    // 0
  float pad[2] = {0, 0};         // 8: std430 aligns the vec4 below to 16
  float rect[4] = {0, 0, 0, 0};  // 16
  float region[2] = {0, 0};      // 32
  float axis[2] = {0, 0};        // 40
  float blur = 0;                // 48
  std::int32_t mode = 0;         // 52: 0 filter, 1 vignette, 2 sidebar
  std::int32_t blind = 0;        // 56
  float alpha = 0;               // 60
  float sidebar = 0;             // 64
};

static_assert(sizeof(PostPush) <= 84);

// What differs between the pipelines: the shaders, the vertex inputs, the topology and the culling.
struct PipelineShape {
  VkShaderModule vertex = VK_NULL_HANDLE;
  VkShaderModule fragment = VK_NULL_HANDLE;
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
  // The negative viewport flips winding: GL's counter-clockwise front is clockwise here.
  VkFrontFace front = VK_FRONT_FACE_CLOCKWISE;
  bool blend = true;
  // RmlUi's blend (ONE, ONE_MINUS_SRC_ALPHA): its colours are premultiplied.
  bool premultiplied = false;
  // RmlUi's clip masks: test the stencil (equal to the dynamic reference), or write it.
  enum class Stencil : std::uint8_t { off, test, replace, increment };
  Stencil stencil = Stencil::off;
};

// The RmlUi push constants (76 bytes of the 84-byte range): the projection times RmlUi's transform,
// the translation, and whether the fragment samples the surface (set 1).
struct UiPush {
  float transform[16] = {};  // column-major, window pixels to clip space
  float translate[2] = {0, 0};
  std::int32_t textured = 0;
};
static_assert(sizeof(UiPush) == 76);

// RmlUi layer composites (ui_post.vert/frag): a target rectangle filled from a source rectangle, and for the
// blur the tap step, the clamp region and the weights. Scalar arrays, so the std430 layout is 84 bytes.
struct UiPostPush {
  float dst[4] = {};       // x0 y0 x1 y1, window pixels
  float src[4] = {};       // u0 v0 u1 v1
  float offset[2] = {};    // uv step of one blur tap
  float clamp_uv[4] = {};  // blur region: min x, min y, max x, max y
  float weights[4] = {};   // blur weights for tap distance 0..3
  float window[2] = {};    // window size in pixels
  std::int32_t blur = 0;   // 0 copy, 1 blur
};
static_assert(sizeof(UiPostPush) == 84);

class Pipelines {
 public:
  // Creates the shader modules, layouts and pools, and the dummy surface and wall buffer that
  // stand in for handle 0. Needs the frames and the resources.
  void create();
  // Releases everything. The frames must have finished (their retired sets are freed first).
  void destroy() noexcept;

  [[nodiscard]] VkPipelineLayout layout() const { return layout_; }

  // Pipelines for render passes compatible with pass.
  [[nodiscard]] VkPipeline quad(VkRenderPass pass, bool list) const;   // strips, or lists for walls
  [[nodiscard]] VkPipeline model(VkRenderPass pass) const;             // instanced model geometry
  [[nodiscard]] VkPipeline mesh(VkRenderPass pass) const;              // prepared meshes
  [[nodiscard]] VkPipeline post(VkRenderPass pass, bool blend) const;  // full-screen post passes
  // RmlUi triangles, premultiplied, with the clip-mask stencil mode (passes with a stencil attachment).
  [[nodiscard]] VkPipeline ui(VkRenderPass pass, PipelineShape::Stencil stencil) const;
  // RmlUi layer composites: a fullscreen-strip draw from the scene set (set 2), premultiplied or plain.
  [[nodiscard]] VkPipeline ui_post(VkRenderPass pass, bool premultiplied) const;
  [[nodiscard]] VkPipeline create_pipeline(VkRenderPass pass, const PipelineShape& shape) const;

  // Set 0 for a frame of `slot`: the atlas array (binding 0) and that slot's wall corners (binding
  // 1). The set changes when either does; the old one retires.
  [[nodiscard]] VkDescriptorSet atlas_set(std::uint32_t atlas, VkBuffer walls, std::uint32_t slot);
  // Set 1: a surface (binding 0). Handle 0 is the dummy, which the shader never samples.
  [[nodiscard]] VkDescriptorSet surface_set(std::uint32_t surface);
  // Set 2: the scene image of a post pass (binding 0). Valid until the next call for the same slot
  // of a pass; the caller retires the views' sets through release_scene().
  [[nodiscard]] VkDescriptorSet scene_set(VkImageView view, bool linear = false);
  // Drops the cached scene sets (offscreen targets were rebuilt; the old views go away).
  void release_scene() noexcept;
  // Drops the set-0 sets that name a released atlas handle or a wall buffer about to be destroyed.
  // Handles and buffer handles are reused, so a cached set must not outlive what it points at.
  void forget_atlas_sets(std::uint32_t atlas, VkBuffer walls) noexcept;

  // The surface handle that stands in for handle 0 (sampled by no draw that needs a real surface).
  [[nodiscard]] std::uint32_t dummy_surface() const { return dummy_surface_; }

  // The wall buffer that stands in for a slot before any walls (until a slot has its own).
  [[nodiscard]] VkBuffer dummy_walls() const { return walls_.buffer; }

  // Statistics for the summary: live surface sets, descriptor pools.
  [[nodiscard]] std::size_t surface_sets() const;

  [[nodiscard]] std::size_t pools() const { return pools_.size(); }

 private:
  struct Cached {
    std::uint32_t key = 0;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkBuffer extra = VK_NULL_HANDLE;  // set 0: the wall buffer
  };

  VkShaderModule quad_vertex_ = VK_NULL_HANDLE;
  VkShaderModule quad_fragment_ = VK_NULL_HANDLE;
  VkShaderModule model_vertex_ = VK_NULL_HANDLE;
  VkShaderModule model_fragment_ = VK_NULL_HANDLE;
  VkShaderModule mesh_vertex_ = VK_NULL_HANDLE;
  VkShaderModule mesh_fragment_ = VK_NULL_HANDLE;
  VkShaderModule post_vertex_ = VK_NULL_HANDLE;
  VkShaderModule post_fragment_ = VK_NULL_HANDLE;
  VkShaderModule ui_vertex_ = VK_NULL_HANDLE;
  VkShaderModule ui_fragment_ = VK_NULL_HANDLE;
  VkShaderModule ui_post_vertex_ = VK_NULL_HANDLE;
  VkShaderModule ui_post_fragment_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout atlas_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout surface_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout scene_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  std::vector<VkDescriptorPool> pools_;
  std::array<Cached, frames_in_flight> atlas_{};
  std::vector<Cached> surfaces_;  // by surface handle - 1
  std::vector<Cached> scenes_;
  std::uint32_t dummy_surface_ = 0;
  Buffer walls_;

  [[nodiscard]] std::pair<VkDescriptorSet, VkDescriptorPool> allocate(VkDescriptorSetLayout layout);
  void retire(Cached& cached) noexcept;
};

// The process's pipelines (created by the Vulkan backend with the device).
[[nodiscard]] Pipelines& pipelines();
}  // namespace realm::gfx::vk
