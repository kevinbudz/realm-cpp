#pragma once
// Shaders, descriptor layouts, the pipeline layout and the descriptor sets of the quad programs.
#include "VkCommon.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace realm::gfx::vk {
// The push constants of the quad programs: the std430 layout of the GLSL block in quad.vert and
// quad.frag (84 bytes). Changing one needs the other.
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

class Pipelines {
 public:
  // Creates the shader modules, layouts and pools, and the dummy surface and wall buffer that
  // stand in for handle 0. Needs the frames and the resources.
  void create();
  // Releases everything. The frames must have finished (their retired sets are freed first).
  void destroy() noexcept;

  [[nodiscard]] VkPipelineLayout layout() const { return layout_; }
  // A quad pipeline (triangle strip, or triangle list for walls) for render passes compatible with pass.
  [[nodiscard]] VkPipeline quad_pipeline(VkRenderPass pass, bool list) const;

  // Set 0: the atlas array (binding 0) and the wall corners (binding 1, vertex stage).
  [[nodiscard]] VkDescriptorSet atlas_set(std::uint32_t atlas);
  // Set 1: a surface (binding 0). Handle 0 is the dummy, which the shader never samples.
  [[nodiscard]] VkDescriptorSet surface_set(std::uint32_t surface);

  // The dummy wall buffer of set 0 until walls are drawn (phase S5).
  [[nodiscard]] VkBuffer wall_buffer() const { return walls_.buffer; }

 private:
  struct Cached {
    std::uint32_t key = 0;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
  };

  VkDescriptorSetLayout atlas_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout surface_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkShaderModule quad_vertex_ = VK_NULL_HANDLE;
  VkShaderModule quad_fragment_ = VK_NULL_HANDLE;
  std::vector<VkDescriptorPool> pools_;
  Cached atlas_;
  std::vector<Cached> surfaces_;  // by surface handle - 1
  std::uint32_t dummy_surface_ = 0;
  Buffer walls_;

  [[nodiscard]] std::pair<VkDescriptorSet, VkDescriptorPool> allocate(VkDescriptorSetLayout layout);
  void retire(Cached& cached) noexcept;
};

// The process's pipelines (created by the Vulkan backend with the device).
[[nodiscard]] Pipelines& pipelines();
}  // namespace realm::gfx::vk
