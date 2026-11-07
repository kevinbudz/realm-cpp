#pragma once
// Bind group layouts, pipeline layouts and the pipelines of the WebGPU backend: quads (strips, and
// lists for walls), models, prepared meshes and the post-effect pass.
//
// Bind groups of the draw layout (quads, models, meshes):
//   group 0: the atlas array (binding 0), its nearest sampler (1), the wall corner texels (2)
//   group 1: one surface (binding 0) and its sampler (1); handle 0 is a 1x1 dummy
//   group 2: the frame's uniform block at binding 0, read through a dynamic offset (Stream)
// Post layout: group 0 is the scene texture (binding 0); group 1 is the uniform block.
//
// Uniform blocks are 80 bytes, the std140-like layout of the WGSL structs in WgpuShaders.cpp.
#include "WgpuContext.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace realm::gfx::wgpu {
// One quad draw's parameters (the Vulkan QuadPush, with 16-byte-aligned vectors as WGSL needs).
struct QuadUniform {
  float viewport[2] = {0, 0};        // 0
  float ground_origin[2] = {0, 0};   // 8
  float ground_x[2] = {0, 0};        // 16
  float ground_y[2] = {0, 0};        // 24
  float ground_time = 0;             // 32
  std::int32_t standalone = 0;       // 36: the surface (group 1) instead of the atlas
  std::int32_t triangle_list = 0;    // 40: wall runs
  std::int32_t ground_space = 0;     // 44: terrain
  float clip_rect[4] = {0, 0, 0, 0}; // 48
  std::int32_t clipped = 0;          // 64: a clipped layer
  std::int32_t pad[3] = {0, 0, 0};   // 68
};
static_assert(sizeof(QuadUniform) == 80);

// The post-effect pass's parameters (the Vulkan PostPush).
struct PostUniform {
  float viewport[2] = {0, 0};    // 0
  float pad[2] = {0, 0};         // 8
  float rect[4] = {0, 0, 0, 0};  // 16
  float region[2] = {0, 0};      // 32
  float axis[2] = {0, 0};        // 40
  float blur = 0;                // 48
  std::int32_t mode = 0;         // 52: 0 filter, 1 vignette, 2 sidebar
  std::int32_t blind = 0;        // 56
  float alpha = 0;               // 60
  float sidebar = 0;             // 64
  float tail[3] = {0, 0, 0};     // 68
};
static_assert(sizeof(PostUniform) == 80);

// The uniform block's size in bytes (both structs), the binding size of every uniform group.
inline constexpr std::uint64_t uniform_bytes = 80;
// Dynamic offsets are multiples of this (the device's minUniformBufferOffsetAlignment, 256 by default).
inline constexpr std::uint64_t uniform_alignment = 256;

class Pipelines {
 public:
  // Creates the shader modules, layouts, pipelines and the dummy textures. The context is initialised.
  void create(WGPUTextureFormat format);
  // Releases everything. The frames must have been submitted.
  void destroy() noexcept;

  [[nodiscard]] WGPUBindGroupLayout uniform_layout() const { return uniform_layout_; }
  // Group 1 of the draw layout (one surface and its sampler): the RmlUi pipelines share it as their group 0.
  [[nodiscard]] WGPUBindGroupLayout surface_layout() const { return surface_layout_; }

  // Pipelines for the window's format: quad strips, or lists for wall runs.
  [[nodiscard]] WGPURenderPipeline quad(bool list) const;
  [[nodiscard]] WGPURenderPipeline model() const;
  [[nodiscard]] WGPURenderPipeline mesh() const;
  [[nodiscard]] WGPURenderPipeline post(bool blend) const;

  // Bind groups. atlas_group and scene_group are new objects: the caller releases them once the
  // pass that binds them is recorded.
  [[nodiscard]] WGPUBindGroup atlas_group(WGPUTextureView atlas, WGPUTextureView walls) const;
  [[nodiscard]] WGPUBindGroup scene_group(WGPUTextureView scene) const;
  // Group 1 for a surface handle (0 or an unallocated surface: the dummy). Owned by the cache: valid
  // until forget_surface(handle).
  [[nodiscard]] WGPUBindGroup surface_group(std::uint32_t surface);
  // Drops a surface's cached group (the surface was released, reallocated or re-filtered).
  void forget_surface(std::uint32_t surface) noexcept;
  // The 1x1 wall texture that stands in for a frame without walls, and the persistent scene group the
  // overlay passes bind (the vignette never samples it).
  [[nodiscard]] WGPUTextureView dummy_walls() const { return dummy_walls_view_; }
  [[nodiscard]] WGPUBindGroup dummy_scene_group() const { return dummy_scene_group_; }
  [[nodiscard]] WGPUBindGroup dummy_surface_group() const { return dummy_surface_group_; }

 private:
  WGPUShaderModule quad_module_ = nullptr;
  WGPUShaderModule mesh_module_ = nullptr;
  WGPUShaderModule model_module_ = nullptr;
  WGPUShaderModule post_module_ = nullptr;
  WGPUBindGroupLayout atlas_layout_ = nullptr;
  WGPUBindGroupLayout surface_layout_ = nullptr;
  WGPUBindGroupLayout scene_layout_ = nullptr;
  WGPUBindGroupLayout uniform_layout_ = nullptr;
  WGPUPipelineLayout draw_layout_ = nullptr;
  WGPUPipelineLayout post_layout_ = nullptr;
  WGPURenderPipeline quad_strip_ = nullptr;
  WGPURenderPipeline quad_list_ = nullptr;
  WGPURenderPipeline model_ = nullptr;
  WGPURenderPipeline mesh_ = nullptr;
  WGPURenderPipeline post_plain_ = nullptr;
  WGPURenderPipeline post_blend_ = nullptr;
  WGPUTexture dummy_surface_texture_ = nullptr;
  WGPUTextureView dummy_surface_view_ = nullptr;
  WGPUTexture dummy_scene_texture_ = nullptr;
  WGPUTextureView dummy_scene_view_ = nullptr;
  WGPUTexture dummy_walls_texture_ = nullptr;
  WGPUTextureView dummy_walls_view_ = nullptr;
  WGPUBindGroup dummy_surface_group_ = nullptr;
  WGPUBindGroup dummy_scene_group_ = nullptr;
  // Groups by surface handle - 1.
  std::vector<WGPUBindGroup> surface_groups_;
  WGPUTextureFormat format_ = WGPUTextureFormat_BGRA8Unorm;
  bool created_ = false;

  [[nodiscard]] WGPURenderPipeline draw_pipeline(WGPUShaderModule module, const WGPUVertexBufferLayout* buffers,
                                                 std::size_t buffer_count, WGPUPrimitiveTopology topology,
                                                 WGPUCullMode cull, WGPUFrontFace front, bool blend,
                                                 WGPUPipelineLayout layout) const;
};

// The process's pipelines (the WebGPU backend creates them with its device).
[[nodiscard]] Pipelines& pipelines();
}  // namespace realm::gfx::wgpu
