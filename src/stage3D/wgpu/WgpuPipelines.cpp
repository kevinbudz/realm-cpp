#include "WgpuPipelines.hpp"
#include "WgpuResources.hpp"
#include "WgpuShaders.hpp"
#include "../DrawPlan.hpp"
#include "realm/engine3d/Model3D.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace realm::gfx::wgpu {
namespace {
Pipelines& owner() {
  static Pipelines instance;
  return instance;
}

WGPUStringView view_of(const char* text) {
  return WGPUStringView{text, std::strlen(text)};
}

WGPUShaderModule make_module(WGPUDevice device, std::string_view code) {
  WGPUShaderSourceWGSL source{};
  source.chain.sType = WGPUSType_ShaderSourceWGSL;
  source.code.data = code.data();
  source.code.length = code.size();
  WGPUShaderModuleDescriptor descriptor{};
  descriptor.nextInChain = &source.chain;
  WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &descriptor);
  if (!module) {
    throw std::runtime_error("WebGPU shader module failed");
  }
  return module;
}

WGPUVertexFormat float_format(int components) {
  switch (components) {
    case 1:
      return WGPUVertexFormat_Float32;
    case 2:
      return WGPUVertexFormat_Float32x2;
    case 3:
      return WGPUVertexFormat_Float32x3;
    case 4:
      return WGPUVertexFormat_Float32x4;
    default:
      throw std::logic_error("Vertex attribute with an unsupported component count");
  }
}

WGPUVertexAttribute attribute(WGPUVertexFormat format, std::uint64_t offset, std::uint32_t location) {
  WGPUVertexAttribute result{};
  result.format = format;
  result.offset = offset;
  result.shaderLocation = location;
  return result;
}

// The Quad fields of the instance (locations first..first+8), as the quad program reads them.
void quad_instance_attributes(std::uint32_t first, std::vector<WGPUVertexAttribute>& out) {
  for (std::size_t i = 0; i < draw_plan::quad_sizes.size(); ++i) {
    out.push_back(attribute(float_format(draw_plan::quad_sizes[i]), draw_plan::quad_offsets[i],
                            first + static_cast<std::uint32_t>(i)));
  }
}

WGPUTexture make_texture(WGPUDevice device, WGPUTextureFormat format, std::uint32_t width,
                         std::uint32_t height, WGPUTextureUsage usage, WGPUTextureView& view) {
  WGPUTextureDescriptor descriptor{};
  descriptor.usage = usage;
  descriptor.dimension = WGPUTextureDimension_2D;
  descriptor.size = {width, height, 1};
  descriptor.format = format;
  descriptor.mipLevelCount = 1;
  descriptor.sampleCount = 1;
  WGPUTexture texture = wgpuDeviceCreateTexture(device, &descriptor);
  if (!texture) {
    throw std::runtime_error("WebGPU dummy texture failed");
  }
  WGPUTextureViewDescriptor view_descriptor{};
  view_descriptor.format = format;
  view_descriptor.dimension = WGPUTextureViewDimension_2D;
  view_descriptor.mipLevelCount = 1;
  view_descriptor.arrayLayerCount = 1;
  view_descriptor.aspect = WGPUTextureAspect_All;
  view = wgpuTextureCreateView(texture, &view_descriptor);
  return texture;
}

WGPUBindGroupLayoutEntry layout_entry(std::uint32_t binding, WGPUShaderStage stages) {
  WGPUBindGroupLayoutEntry entry{};
  entry.binding = binding;
  entry.visibility = stages;
  return entry;
}

WGPUBindGroupEntry view_entry(std::uint32_t binding, WGPUTextureView view) {
  WGPUBindGroupEntry entry{};
  entry.binding = binding;
  entry.textureView = view;
  return entry;
}

WGPUBindGroupEntry sampler_entry(std::uint32_t binding, WGPUSampler sampler) {
  WGPUBindGroupEntry entry{};
  entry.binding = binding;
  entry.sampler = sampler;
  return entry;
}

WGPUBindGroup make_group(WGPUDevice device, WGPUBindGroupLayout layout,
                         std::span<WGPUBindGroupEntry> entries) {
  WGPUBindGroupDescriptor descriptor{};
  descriptor.layout = layout;
  descriptor.entryCount = entries.size();
  descriptor.entries = entries.data();
  WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &descriptor);
  if (!group) {
    throw std::runtime_error("WebGPU bind group failed");
  }
  return group;
}

WGPUBindGroupLayout make_layout(WGPUDevice device, std::span<WGPUBindGroupLayoutEntry> entries) {
  WGPUBindGroupLayoutDescriptor descriptor{};
  descriptor.entryCount = entries.size();
  descriptor.entries = entries.data();
  WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
  if (!layout) {
    throw std::runtime_error("WebGPU bind group layout failed");
  }
  return layout;
}

// The vertex buffer layouts of the programs.
WGPUVertexBufferLayout quad_instance_layout(std::vector<WGPUVertexAttribute>& attributes) {
  WGPUVertexBufferLayout layout{};
  layout.stepMode = WGPUVertexStepMode_Instance;
  layout.arrayStride = quad_instance_bytes;
  layout.attributeCount = attributes.size();
  layout.attributes = attributes.data();
  return layout;
}
}  // namespace

Pipelines& pipelines() {
  return owner();
}

void Pipelines::create(WGPUTextureFormat format) {
  auto* device = context().device();
  format_ = format;

  quad_module_ = make_module(device, shaders::quad);
  mesh_module_ = make_module(device, shaders::mesh);
  model_module_ = make_module(device, shaders::model);
  post_module_ = make_module(device, shaders::post);

  // Group 0 (draw): atlas array, sampler, wall texels (unfilterable float, read with textureLoad).
  std::array<WGPUBindGroupLayoutEntry, 3> atlas_entries{};
  atlas_entries[0] = layout_entry(0, WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
  atlas_entries[0].texture.sampleType = WGPUTextureSampleType_Float;
  atlas_entries[0].texture.viewDimension = WGPUTextureViewDimension_2DArray;
  atlas_entries[1] = layout_entry(1, WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
  atlas_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
  atlas_entries[2] = layout_entry(2, WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
  atlas_entries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
  atlas_entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
  atlas_layout_ = make_layout(device, atlas_entries);

  // Group 1 (draw): one surface and its sampler.
  std::array<WGPUBindGroupLayoutEntry, 2> surface_entries{};
  surface_entries[0] = layout_entry(0, WGPUShaderStage_Fragment);
  surface_entries[0].texture.sampleType = WGPUTextureSampleType_Float;
  surface_entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
  surface_entries[1] = layout_entry(1, WGPUShaderStage_Fragment);
  surface_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
  surface_layout_ = make_layout(device, surface_entries);

  // Group 2 (draw) and group 1 (post): the frame's uniform block, dynamic offset.
  std::array<WGPUBindGroupLayoutEntry, 1> uniform_entries{};
  uniform_entries[0] = layout_entry(0, WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
  uniform_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
  uniform_entries[0].buffer.hasDynamicOffset = 1;
  uniform_entries[0].buffer.minBindingSize = uniform_bytes;
  uniform_layout_ = make_layout(device, uniform_entries);

  // Post group 0: the scene texture, read by texel loads.
  std::array<WGPUBindGroupLayoutEntry, 1> scene_entries{};
  scene_entries[0] = layout_entry(0, WGPUShaderStage_Fragment);
  scene_entries[0].texture.sampleType = WGPUTextureSampleType_Float;
  scene_entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
  scene_layout_ = make_layout(device, scene_entries);

  std::array<WGPUBindGroupLayout, 3> draw_layouts{atlas_layout_, surface_layout_, uniform_layout_};
  WGPUPipelineLayoutDescriptor draw_descriptor{};
  draw_descriptor.bindGroupLayoutCount = draw_layouts.size();
  draw_descriptor.bindGroupLayouts = draw_layouts.data();
  draw_layout_ = wgpuDeviceCreatePipelineLayout(device, &draw_descriptor);

  std::array<WGPUBindGroupLayout, 2> post_layouts{scene_layout_, uniform_layout_};
  WGPUPipelineLayoutDescriptor post_descriptor{};
  post_descriptor.bindGroupLayoutCount = post_layouts.size();
  post_descriptor.bindGroupLayouts = post_layouts.data();
  post_layout_ = wgpuDeviceCreatePipelineLayout(device, &post_descriptor);

  // Quads: the instance stream (88 bytes, per instance). Walls are lists; everything else strips.
  std::vector<WGPUVertexAttribute> quad_attributes;
  quad_instance_attributes(0, quad_attributes);
  const auto quad_buffer = quad_instance_layout(quad_attributes);
  quad_strip_ = draw_pipeline(quad_module_, &quad_buffer, 1, WGPUPrimitiveTopology_TriangleStrip,
                              WGPUCullMode_None, WGPUFrontFace_CCW, true, draw_layout_);
  quad_list_ = draw_pipeline(quad_module_, &quad_buffer, 1, WGPUPrimitiveTopology_TriangleList,
                             WGPUCullMode_None, WGPUFrontFace_CCW, true, draw_layout_);

  // Models: buffer 0 is ModelVertex (36 bytes, per vertex), buffer 1 the quad instances. The fields
  // the model program reads are the Vulkan model's locations 4..11 (see VkPipelines.cpp).
  std::vector<WGPUVertexAttribute> model_attributes;
  model_attributes.push_back(attribute(WGPUVertexFormat_Float32x3, offsetof(ModelVertex, x), 0));
  model_attributes.push_back(attribute(WGPUVertexFormat_Float32x3, offsetof(ModelVertex, nx), 1));
  model_attributes.push_back(attribute(WGPUVertexFormat_Float32x2, offsetof(ModelVertex, u), 2));
  model_attributes.push_back(attribute(WGPUVertexFormat_Float32, offsetof(ModelVertex, solid), 3));
  std::vector<WGPUVertexAttribute> model_instance;
  const std::array<std::size_t, 8> fields{0, 1, 2, 8, 3, 4, 5, 6};
  for (std::size_t i = 0; i < fields.size(); ++i) {
    model_instance.push_back(attribute(float_format(draw_plan::quad_sizes[fields[i]]),
                                       draw_plan::quad_offsets[fields[i]],
                                       4 + static_cast<std::uint32_t>(i)));
  }
  std::array<WGPUVertexBufferLayout, 2> model_buffers{};
  model_buffers[0].stepMode = WGPUVertexStepMode_Vertex;
  model_buffers[0].arrayStride = sizeof(ModelVertex);
  model_buffers[0].attributeCount = model_attributes.size();
  model_buffers[0].attributes = model_attributes.data();
  model_buffers[1] = quad_instance_layout(model_instance);
  // Measured on Vulkan under the negative viewport (the same framebuffer orientation WebGPU gives
  // these positions): counter-clockwise is the front face.
  model_ = draw_pipeline(model_module_, model_buffers.data(), model_buffers.size(),
                         WGPUPrimitiveTopology_TriangleList, WGPUCullMode_Back, WGPUFrontFace_CCW, true,
                         draw_layout_);

  // Prepared meshes: MeshBatchVertex (60 bytes) with the mesh program's eight attributes.
  using V = draw_plan::MeshBatchVertex;
  std::vector<WGPUVertexAttribute> mesh_attributes{
      attribute(WGPUVertexFormat_Float32x2, offsetof(V, x), 0),
      attribute(WGPUVertexFormat_Float32x2, offsetof(V, u), 1),
      attribute(WGPUVertexFormat_Float32, offsetof(V, shade), 2),
      attribute(WGPUVertexFormat_Float32x4, offsetof(V, bounds), 3),
      attribute(WGPUVertexFormat_Float32, offsetof(V, page), 4),
      attribute(WGPUVertexFormat_Float32x3, offsetof(V, color), 5),
      attribute(WGPUVertexFormat_Float32, offsetof(V, textured), 6),
      attribute(WGPUVertexFormat_Float32, offsetof(V, repeat), 7),
  };
  WGPUVertexBufferLayout mesh_buffer{};
  mesh_buffer.stepMode = WGPUVertexStepMode_Vertex;
  mesh_buffer.arrayStride = sizeof(V);
  mesh_buffer.attributeCount = mesh_attributes.size();
  mesh_buffer.attributes = mesh_attributes.data();
  mesh_ = draw_pipeline(mesh_module_, &mesh_buffer, 1, WGPUPrimitiveTopology_TriangleList,
                        WGPUCullMode_None, WGPUFrontFace_CCW, true, draw_layout_);

  // Post passes: a strip over the screen, with and without the GL blend.
  post_plain_ = draw_pipeline(post_module_, nullptr, 0, WGPUPrimitiveTopology_TriangleStrip,
                              WGPUCullMode_None, WGPUFrontFace_CCW, false, post_layout_);
  post_blend_ = draw_pipeline(post_module_, nullptr, 0, WGPUPrimitiveTopology_TriangleStrip,
                              WGPUCullMode_None, WGPUFrontFace_CCW, true, post_layout_);

  // Dummies: a 1x1 black surface and scene, and a 1x1 wall texel texture (never read meaningfully).
  const std::array<std::uint8_t, 4> black{0, 0, 0, 255};
  dummy_surface_texture_ = make_texture(device, WGPUTextureFormat_RGBA8Unorm, 1, 1,
                                        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                                        dummy_surface_view_);
  dummy_scene_texture_ = make_texture(device, WGPUTextureFormat_RGBA8Unorm, 1, 1,
                                      WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                                      dummy_scene_view_);
  dummy_walls_texture_ = make_texture(device, WGPUTextureFormat_RGBA32Float, 1, 1,
                                      WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                                      dummy_walls_view_);
  WGPUTexelCopyTextureInfo destination{};
  destination.texture = dummy_surface_texture_;
  destination.aspect = WGPUTextureAspect_All;
  WGPUTexelCopyBufferLayout layout{};
  layout.bytesPerRow = 4;
  layout.rowsPerImage = 1;
  const WGPUExtent3D one{1, 1, 1};
  wgpuQueueWriteTexture(context().queue(), &destination, black.data(), black.size(), &layout, &one);
  destination.texture = dummy_scene_texture_;
  wgpuQueueWriteTexture(context().queue(), &destination, black.data(), black.size(), &layout, &one);
  const std::array<float, 4> zero_texel{0, 0, 0, 0};
  destination.texture = dummy_walls_texture_;
  layout.bytesPerRow = 16;
  wgpuQueueWriteTexture(context().queue(), &destination, zero_texel.data(), zero_texel.size() * 4,
                        &layout, &one);

  std::array<WGPUBindGroupEntry, 2> dummy_surface_entries{view_entry(0, dummy_surface_view_),
                                                          sampler_entry(1, resources().sampler(false))};
  dummy_surface_group_ = make_group(device, surface_layout_, dummy_surface_entries);
  std::array<WGPUBindGroupEntry, 1> dummy_scene_entries{view_entry(0, dummy_scene_view_)};
  dummy_scene_group_ = make_group(device, scene_layout_, dummy_scene_entries);
  created_ = true;
}

void Pipelines::destroy() noexcept {
  if (!created_) {
    return;
  }
  for (auto& group : surface_groups_) {
    if (group) {
      wgpuBindGroupRelease(group);
    }
  }
  surface_groups_.clear();
  const auto release_pipeline = [](WGPURenderPipeline& pipeline) {
    if (pipeline) {
      wgpuRenderPipelineRelease(pipeline);
      pipeline = nullptr;
    }
  };
  release_pipeline(quad_strip_);
  release_pipeline(quad_list_);
  release_pipeline(model_);
  release_pipeline(mesh_);
  release_pipeline(post_plain_);
  release_pipeline(post_blend_);
  const auto release_module = [](WGPUShaderModule& module) {
    if (module) {
      wgpuShaderModuleRelease(module);
      module = nullptr;
    }
  };
  release_module(quad_module_);
  release_module(mesh_module_);
  release_module(model_module_);
  release_module(post_module_);
  const auto release_group = [](WGPUBindGroup& group) {
    if (group) {
      wgpuBindGroupRelease(group);
      group = nullptr;
    }
  };
  release_group(dummy_surface_group_);
  release_group(dummy_scene_group_);
  const auto release_view = [](WGPUTextureView& view) {
    if (view) {
      wgpuTextureViewRelease(view);
      view = nullptr;
    }
  };
  release_view(dummy_surface_view_);
  release_view(dummy_scene_view_);
  release_view(dummy_walls_view_);
  const auto release_texture = [](WGPUTexture& texture) {
    if (texture) {
      wgpuTextureRelease(texture);
      texture = nullptr;
    }
  };
  release_texture(dummy_surface_texture_);
  release_texture(dummy_scene_texture_);
  release_texture(dummy_walls_texture_);
  const auto release_layout = [](WGPUBindGroupLayout& layout) {
    if (layout) {
      wgpuBindGroupLayoutRelease(layout);
      layout = nullptr;
    }
  };
  release_layout(atlas_layout_);
  release_layout(surface_layout_);
  release_layout(scene_layout_);
  release_layout(uniform_layout_);
  if (draw_layout_) {
    wgpuPipelineLayoutRelease(draw_layout_);
    draw_layout_ = nullptr;
  }
  if (post_layout_) {
    wgpuPipelineLayoutRelease(post_layout_);
    post_layout_ = nullptr;
  }
  created_ = false;
}

WGPURenderPipeline Pipelines::draw_pipeline(WGPUShaderModule module, const WGPUVertexBufferLayout* buffers,
                                            std::size_t buffer_count, WGPUPrimitiveTopology topology,
                                            WGPUCullMode cull, WGPUFrontFace front, bool blend,
                                            WGPUPipelineLayout layout) const {
  WGPUVertexState vertex{};
  vertex.module = module;
  vertex.entryPoint = view_of("vs_main");
  vertex.bufferCount = buffer_count;
  vertex.buffers = buffers;

  // The GL blend: SRC_ALPHA / ONE_MINUS_SRC_ALPHA for colour, ONE / ONE_MINUS_SRC_ALPHA for alpha.
  WGPUBlendState blend_state{};
  blend_state.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
  blend_state.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
  WGPUColorTargetState target{};
  target.format = format_;
  target.blend = blend ? &blend_state : nullptr;
  target.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState fragment{};
  fragment.module = module;
  fragment.entryPoint = view_of("fs_main");
  fragment.targetCount = 1;
  fragment.targets = &target;

  WGPURenderPipelineDescriptor descriptor{};
  descriptor.layout = layout;
  descriptor.vertex = vertex;
  descriptor.primitive.topology = topology;
  descriptor.primitive.frontFace = front;
  descriptor.primitive.cullMode = cull;
  descriptor.multisample.count = 1;
  descriptor.multisample.mask = 0xFFFFFFFFU;
  descriptor.fragment = &fragment;
  WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(context().device(), &descriptor);
  if (!pipeline) {
    throw std::runtime_error("WebGPU render pipeline failed");
  }
  return pipeline;
}

WGPURenderPipeline Pipelines::quad(bool list) const {
  return list ? quad_list_ : quad_strip_;
}

WGPURenderPipeline Pipelines::model() const {
  return model_;
}

WGPURenderPipeline Pipelines::mesh() const {
  return mesh_;
}

WGPURenderPipeline Pipelines::post(bool blend) const {
  return blend ? post_blend_ : post_plain_;
}

WGPUBindGroup Pipelines::atlas_group(WGPUTextureView atlas, WGPUTextureView walls) const {
  std::array<WGPUBindGroupEntry, 3> entries{view_entry(0, atlas), sampler_entry(1, resources().sampler(false)),
                                            view_entry(2, walls)};
  return make_group(context().device(), atlas_layout_, entries);
}

WGPUBindGroup Pipelines::scene_group(WGPUTextureView scene) const {
  std::array<WGPUBindGroupEntry, 1> entries{view_entry(0, scene)};
  return make_group(context().device(), scene_layout_, entries);
}

WGPUBindGroup Pipelines::surface_group(std::uint32_t surface) {
  if (surface == 0) {
    return dummy_surface_group_;
  }
  if (surface > surface_groups_.size()) {
    surface_groups_.resize(surface, nullptr);
  }
  auto& cached = surface_groups_[surface - 1];
  if (cached) {
    return cached;
  }
  const WGPUTextureView view = resources().view(surface);
  if (!view) {
    return dummy_surface_group_;  // allocated later: the dummy until then, never cached
  }
  std::array<WGPUBindGroupEntry, 2> entries{view_entry(0, view),
                                            sampler_entry(1, resources().sampler(resources().linear(surface)))};
  cached = make_group(context().device(), surface_layout_, entries);
  return cached;
}

void Pipelines::forget_surface(std::uint32_t surface) noexcept {
  if (surface == 0 || surface > surface_groups_.size()) {
    return;
  }
  auto& cached = surface_groups_[surface - 1];
  if (cached) {
    wgpuBindGroupRelease(cached);
    cached = nullptr;
  }
}
}  // namespace realm::gfx::wgpu
