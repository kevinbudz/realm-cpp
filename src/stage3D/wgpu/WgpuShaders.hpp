#pragma once
// The WGSL programs of the WebGPU backend: hand-ports of the Vulkan GLSL in src/stage3D/vk/shaders
// (quad, mesh, model, post). Each is one module; WgpuPipelines.cpp compiles them into pipelines.
// Bind groups and the uniform layouts are documented at the top of WgpuPipelines.hpp.
#include <string_view>

namespace realm::gfx::wgpu::shaders {
// Quads: world and overlay instances, retained terrain, clipped layers and walls (corner texels).
extern const std::string_view quad;
// Prepared meshes (MeshBatchVertex, drawn from the frame's stream).
extern const std::string_view mesh;
// Registered models (ModelVertex geometry, instanced quads).
extern const std::string_view model;
// Post effects: the blur, the blind grey, the vignette and the sidebar.
extern const std::string_view post;
// RmlUi (WgpuUi.cpp): geometry (geometry_vs, geometry_fs) and the layer and blur passes (post_vs, post_fs), with
// the 176-byte UiBlock uniform.
extern const std::string_view ui;
}  // namespace realm::gfx::wgpu::shaders
