#include "VkPipelines.hpp"
#include "VkFrame.hpp"
#include "VkResources.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "../DrawPlan.hpp"
#include "vk_shaders.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace realm::gfx::vk {
namespace {
constexpr std::uint32_t pool_sets = 512;
constexpr std::uint32_t pool_images = 512;
constexpr std::uint32_t pool_storage = 64;

template <typename T>
T make(VkStructureType type) {
  T value{};
  value.sType = type;
  return value;
}

VkShaderModule make_module(const unsigned char* code, std::size_t size) {
  // The embedded bytes are copied into words: the array is not guaranteed to be 4-byte aligned.
  std::vector<std::uint32_t> words(size / sizeof(std::uint32_t));
  std::memcpy(words.data(), code, words.size() * sizeof(std::uint32_t));
  auto info = make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
  info.codeSize = words.size() * sizeof(std::uint32_t);
  info.pCode = words.data();
  VkShaderModule module = VK_NULL_HANDLE;
  check(context().fn.vkCreateShaderModule(context().device, &info, nullptr, &module),
        "vkCreateShaderModule");
  return module;
}

VkFormat format_of(int components) {
  switch (components) {
    case 1:
      return VK_FORMAT_R32_SFLOAT;
    case 2:
      return VK_FORMAT_R32G32_SFLOAT;
    case 3:
      return VK_FORMAT_R32G32B32_SFLOAT;
    case 4:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:
      throw std::logic_error("Vertex attribute with an unsupported component count");
  }
}

// The instance attributes of a Quad (locations first..first+8), as the quad program reads them.
void quad_instance_attributes(std::uint32_t binding, std::uint32_t first,
                              std::vector<VkVertexInputAttributeDescription>& out) {
  for (std::size_t i = 0; i < draw_plan::quad_sizes.size(); ++i) {
    out.push_back({first + static_cast<std::uint32_t>(i), binding,
                   format_of(draw_plan::quad_sizes[i]),
                   static_cast<std::uint32_t>(draw_plan::quad_offsets[i])});
  }
}

VkDescriptorSetLayoutBinding layout_binding(std::uint32_t binding, VkDescriptorType type,
                                            VkShaderStageFlags stages, std::uint32_t count = 1) {
  VkDescriptorSetLayoutBinding result{};
  result.binding = binding;
  result.descriptorType = type;
  result.descriptorCount = count;
  result.stageFlags = stages;
  return result;
}
}  // namespace

void Pipelines::create() {
  auto& device = context();
  quad_vertex_ = make_module(spirv::quad_vert_spv, spirv::quad_vert_spv_size);
  quad_fragment_ = make_module(spirv::quad_frag_spv, spirv::quad_frag_spv_size);
  model_vertex_ = make_module(spirv::model_vert_spv, spirv::model_vert_spv_size);
  model_fragment_ = make_module(spirv::model_frag_spv, spirv::model_frag_spv_size);
  mesh_vertex_ = make_module(spirv::mesh_vert_spv, spirv::mesh_vert_spv_size);
  mesh_fragment_ = make_module(spirv::mesh_frag_spv, spirv::mesh_frag_spv_size);
  post_vertex_ = make_module(spirv::post_vert_spv, spirv::post_vert_spv_size);
  post_fragment_ = make_module(spirv::post_frag_spv, spirv::post_frag_spv_size);
  ui_vertex_ = make_module(spirv::ui_vert_spv, spirv::ui_vert_spv_size);
  ui_fragment_ = make_module(spirv::ui_frag_spv, spirv::ui_frag_spv_size);
  ui_post_vertex_ = make_module(spirv::ui_post_vert_spv, spirv::ui_post_vert_spv_size);
  ui_post_fragment_ = make_module(spirv::ui_post_frag_spv, spirv::ui_post_frag_spv_size);

  // Set 0: the atlas array, then the wall corners (vertex stage).
  const std::array<VkDescriptorSetLayoutBinding, 2> atlas_bindings{
      layout_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
      layout_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)};
  auto atlas_info = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
  atlas_info.bindingCount = static_cast<std::uint32_t>(atlas_bindings.size());
  atlas_info.pBindings = atlas_bindings.data();
  check(device.fn.vkCreateDescriptorSetLayout(device.device, &atlas_info, nullptr, &atlas_layout_),
        "vkCreateDescriptorSetLayout");

  // Set 1: one surface. Set 2: the post pass's scene image.
  const auto surface_binding =
      layout_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
  auto surface_info = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
  surface_info.bindingCount = 1;
  surface_info.pBindings = &surface_binding;
  check(device.fn.vkCreateDescriptorSetLayout(device.device, &surface_info, nullptr, &surface_layout_),
        "vkCreateDescriptorSetLayout");
  check(
      device.fn.vkCreateDescriptorSetLayout(device.device, &surface_info, nullptr, &scene_layout_),
      "vkCreateDescriptorSetLayout");

  const std::array<VkDescriptorSetLayout, 3> set_layouts{atlas_layout_, surface_layout_,
                                                         scene_layout_};
  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push.offset = 0;
  push.size = sizeof(QuadPush);
  auto layout_info = make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
  layout_info.setLayoutCount = static_cast<std::uint32_t>(set_layouts.size());
  layout_info.pSetLayouts = set_layouts.data();
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push;
  check(device.fn.vkCreatePipelineLayout(device.device, &layout_info, nullptr, &layout_),
        "vkCreatePipelineLayout");

  // Stand-ins for handle 0 and for slots without walls: a 16-byte wall buffer, and a 1x1 surface
  // (never sampled when standalone is 0).
  walls_ = create_device_buffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  auto& resources_ = resources();
  dummy_surface_ = resources_.create_surface(false);
  resources_.allocate_surface(dummy_surface_, 1, 1);
  const std::array<std::byte, 4> black{};
  resources_.upload_surface(dummy_surface_, 0, 0, 1, 1, black.data());
  resources_.set_release_hook([this](std::uint32_t handle) {
    if (handle - 1 < surfaces_.size()) {
      retire(surfaces_[handle - 1]);
    }
  });
}

void Pipelines::destroy() noexcept {
  auto& device = context();
  if (!device.ready()) {
    return;
  }
  // Shutdown after the frames: the device is idle, so the sets are freed now (the frames are gone).
  device.fn.vkDeviceWaitIdle(device.device);
  for (auto& cached : surfaces_) {
    retire(cached);
  }
  surfaces_.clear();
  for (auto& cached : atlas_) {
    retire(cached);
  }
  for (auto& cached : scenes_) {
    retire(cached);
  }
  scenes_.clear();
  for (auto pool : pools_) {
    device.fn.vkDestroyDescriptorPool(device.device, pool, nullptr);
  }
  pools_.clear();
  device.fn.vkDestroyPipelineLayout(device.device, layout_, nullptr);
  device.fn.vkDestroyDescriptorSetLayout(device.device, atlas_layout_, nullptr);
  device.fn.vkDestroyDescriptorSetLayout(device.device, surface_layout_, nullptr);
  device.fn.vkDestroyDescriptorSetLayout(device.device, scene_layout_, nullptr);
  for (const auto module : {quad_vertex_, quad_fragment_, model_vertex_, model_fragment_,
                            mesh_vertex_, mesh_fragment_, post_vertex_, post_fragment_,
                            ui_vertex_, ui_fragment_, ui_post_vertex_, ui_post_fragment_}) {
    device.fn.vkDestroyShaderModule(device.device, module, nullptr);
  }
  destroy_buffer(walls_);
  layout_ = VK_NULL_HANDLE;
  atlas_layout_ = surface_layout_ = scene_layout_ = VK_NULL_HANDLE;
  quad_vertex_ = quad_fragment_ = model_vertex_ = model_fragment_ = VK_NULL_HANDLE;
  mesh_vertex_ = mesh_fragment_ = post_vertex_ = post_fragment_ = VK_NULL_HANDLE;
  ui_vertex_ = ui_fragment_ = ui_post_vertex_ = ui_post_fragment_ = VK_NULL_HANDLE;
}

std::pair<VkDescriptorSet, VkDescriptorPool> Pipelines::allocate(VkDescriptorSetLayout layout) {
  auto& device = context();
  for (int attempt = 0; attempt < 2; ++attempt) {
    // The second attempt follows an exhausted pool: a fresh one takes the set.
    if (attempt == 1 || pools_.empty()) {
      std::array<VkDescriptorPoolSize, 2> sizes{};
      sizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, pool_images};
      sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, pool_storage};
      auto info = make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
      info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
      info.maxSets = pool_sets;
      info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
      info.pPoolSizes = sizes.data();
      VkDescriptorPool pool = VK_NULL_HANDLE;
      check(device.fn.vkCreateDescriptorPool(device.device, &info, nullptr, &pool), "vkCreateDescriptorPool");
      pools_.push_back(pool);
    }
    const auto pool = pools_.back();
    auto info = make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    const auto result = device.fn.vkAllocateDescriptorSets(device.device, &info, &set);
    if (result == VK_SUCCESS) {
      return {set, pool};
    }
    if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
      check(result, "vkAllocateDescriptorSets");
    }
  }
  throw std::runtime_error("Vulkan descriptor pool exhausted");
}

void Pipelines::retire(Cached& cached) noexcept {
  if (cached.set == VK_NULL_HANDLE) {
    return;
  }
  const auto set = cached.set;
  const auto pool = cached.pool;
  frames().retire([set, pool] {
    auto& dev = context();
    dev.fn.vkFreeDescriptorSets(dev.device, pool, 1, &set);
  });
  cached = Cached{};
}

VkDescriptorSet Pipelines::atlas_set(std::uint32_t atlas, VkBuffer walls, std::uint32_t slot) {
  auto& cached = atlas_[slot];
  if (cached.set != VK_NULL_HANDLE && cached.key == atlas && cached.extra == walls) {
    return cached.set;
  }
  auto& device = context();
  retire(cached);
  const auto [set, pool] = allocate(atlas_layout_);
  VkDescriptorImageInfo image{};
  image.sampler = resources().sampler(false);
  image.imageView = resources().view(atlas);
  image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorBufferInfo storage{walls, 0, VK_WHOLE_SIZE};
  std::array<VkWriteDescriptorSet, 2> writes{};
  writes[0] = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
  writes[0].dstSet = set;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[0].pImageInfo = &image;
  writes[1] = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
  writes[1].dstSet = set;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[1].pBufferInfo = &storage;
  device.fn.vkUpdateDescriptorSets(device.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  cached = Cached{atlas, set, pool, nullptr, nullptr, walls};
  return set;
}

VkDescriptorSet Pipelines::surface_set(std::uint32_t surface) {
  if (surface == 0) {
    surface = dummy_surface_;
  }
  auto& resources_ = resources();
  const auto view = resources_.view(surface);
  const auto sampler = resources_.sampler(resources_.linear(surface));
  if (surfaces_.size() < surface) {
    surfaces_.resize(surface);
  }
  auto& cached = surfaces_[surface - 1];
  if (cached.set != VK_NULL_HANDLE && cached.view == view && cached.sampler == sampler) {
    return cached.set;
  }
  // A new view (reallocated or reused handle) or sampler: a fresh set, and the old one retires.
  retire(cached);
  auto& device = context();
  const auto [set, pool] = allocate(surface_layout_);
  VkDescriptorImageInfo image{};
  image.sampler = sampler;
  image.imageView = view;
  image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  auto write = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image;
  device.fn.vkUpdateDescriptorSets(device.device, 1, &write, 0, nullptr);
  cached = Cached{surface, set, pool, view, sampler, VK_NULL_HANDLE};
  return set;
}

VkDescriptorSet Pipelines::scene_set(VkImageView view, bool linear) {
  // The post passes sample at most two offscreen targets: a set per view and filter, kept until a rebuild.
  for (const auto& known : scenes_) {
    if (known.view == view && known.set != VK_NULL_HANDLE && known.key == (linear ? 1u : 0u)) {
      return known.set;
    }
  }
  auto& device = context();
  const auto [set, pool] = allocate(scene_layout_);
  VkDescriptorImageInfo image{};
  image.sampler = resources().sampler(linear);
  image.imageView = view;
  image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  auto write = make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image;
  device.fn.vkUpdateDescriptorSets(device.device, 1, &write, 0, nullptr);
  Cached cached{linear ? 1u : 0u, set, pool, view, image.sampler, VK_NULL_HANDLE};
  scenes_.push_back(cached);
  return set;
}

void Pipelines::release_scene() noexcept {
  for (auto& cached : scenes_) {
    retire(cached);
  }
  scenes_.clear();
}

std::size_t Pipelines::surface_sets() const {
  return static_cast<std::size_t>(
      std::count_if(surfaces_.begin(), surfaces_.end(),
                    [](const Cached& cached) { return cached.set != VK_NULL_HANDLE; }));
}

VkPipeline Pipelines::create_pipeline(VkRenderPass pass, const PipelineShape& shape) const {
  auto& device = context();
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  stages[0] = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = shape.vertex;
  stages[0].pName = "main";
  stages[1] = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = shape.fragment;
  stages[1].pName = "main";

  auto vertex_input = make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
  vertex_input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(shape.bindings.size());
  vertex_input.pVertexBindingDescriptions = shape.bindings.data();
  vertex_input.vertexAttributeDescriptionCount =
      static_cast<std::uint32_t>(shape.attributes.size());
  vertex_input.pVertexAttributeDescriptions = shape.attributes.data();

  auto assembly = make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
  assembly.topology = shape.topology;

  auto viewport = make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  auto raster = make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = shape.cull;
  raster.frontFace = shape.front;
  raster.lineWidth = 1.0f;

  auto multisample = make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // The GL blend: SRC_ALPHA / ONE_MINUS_SRC_ALPHA for colour, ONE / ONE_MINUS_SRC_ALPHA for alpha.
  VkPipelineColorBlendAttachmentState blend{};
  blend.blendEnable = shape.blend ? VK_TRUE : VK_FALSE;
  blend.srcColorBlendFactor = shape.premultiplied ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
  blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.colorBlendOp = VK_BLEND_OP_ADD;
  blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.alphaBlendOp = VK_BLEND_OP_ADD;
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  const bool mask_write = shape.stencil == PipelineShape::Stencil::replace ||
                          shape.stencil == PipelineShape::Stencil::increment;
  if (mask_write) {
    blend.colorWriteMask = 0;  // a mask draw writes only the stencil
  }
  auto color = make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
  color.attachmentCount = 1;
  color.pAttachments = &blend;

  // The stencil comes from the pass (passes without one ignore this state). Its reference is
  // dynamic, set before each draw that uses it.
  auto depth = make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
  depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  VkStencilOpState face{};
  face.failOp = VK_STENCIL_OP_KEEP;
  face.passOp = VK_STENCIL_OP_KEEP;
  face.depthFailOp = VK_STENCIL_OP_KEEP;
  face.compareOp = VK_COMPARE_OP_ALWAYS;
  face.compareMask = 0xff;
  face.writeMask = 0;
  switch (shape.stencil) {
    case PipelineShape::Stencil::off:
      break;
    case PipelineShape::Stencil::test:
      depth.stencilTestEnable = VK_TRUE;
      face.compareOp = VK_COMPARE_OP_EQUAL;
      break;
    case PipelineShape::Stencil::replace:
      depth.stencilTestEnable = VK_TRUE;
      face.passOp = VK_STENCIL_OP_REPLACE;
      face.writeMask = 0xff;
      break;
    case PipelineShape::Stencil::increment:
      depth.stencilTestEnable = VK_TRUE;
      face.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
      face.writeMask = 0xff;
      break;
  }
  depth.front = face;
  depth.back = face;

  std::vector<VkDynamicState> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  if (shape.stencil != PipelineShape::Stencil::off) {
    dynamic_states.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
  }
  auto dynamic = make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
  dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
  dynamic.pDynamicStates = dynamic_states.data();

  auto info = make<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
  info.stageCount = static_cast<std::uint32_t>(stages.size());
  info.pStages = stages.data();
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pColorBlendState = &color;
  info.pDepthStencilState = &depth;
  info.pDynamicState = &dynamic;
  info.layout = layout_;
  info.renderPass = pass;
  info.subpass = 0;
  VkPipeline pipeline = VK_NULL_HANDLE;
  check(device.fn.vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
        "vkCreateGraphicsPipelines");
  return pipeline;
}

VkPipeline Pipelines::quad(VkRenderPass pass, bool list) const {
  PipelineShape shape;
  shape.vertex = quad_vertex_;
  shape.fragment = quad_fragment_;
  shape.bindings.push_back(
      {0, static_cast<std::uint32_t>(quad_instance_bytes), VK_VERTEX_INPUT_RATE_INSTANCE});
  quad_instance_attributes(0, 0, shape.attributes);
  shape.topology =
      list ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  return create_pipeline(pass, shape);
}

VkPipeline Pipelines::model(VkRenderPass pass) const {
  PipelineShape shape;
  shape.vertex = model_vertex_;
  shape.fragment = model_fragment_;
  // Binding 0: the model geometry (ModelVertex, 36 bytes); binding 1: the quad instances.
  shape.bindings.push_back({0, sizeof(ModelVertex), VK_VERTEX_INPUT_RATE_VERTEX});
  shape.bindings.push_back(
      {1, static_cast<std::uint32_t>(quad_instance_bytes), VK_VERTEX_INPUT_RATE_INSTANCE});
  shape.attributes.push_back(
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(ModelVertex, x))});
  shape.attributes.push_back(
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(ModelVertex, nx))});
  shape.attributes.push_back(
      {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(ModelVertex, u))});
  shape.attributes.push_back(
      {3, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(ModelVertex, solid))});
  // Locations 4..11 are the Quad fields the model program reads: origin, axisX, axisY, axisZ (the
  // uv offset), sprite UV, page, multiply and rotation (the offset vector).
  const std::array<std::size_t, 8> fields{0, 1, 2, 8, 3, 4, 5, 6};
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const auto field = fields[i];
    shape.attributes.push_back({static_cast<std::uint32_t>(4 + i), 1,
                                format_of(draw_plan::quad_sizes[field]),
                                static_cast<std::uint32_t>(draw_plan::quad_offsets[field])});
  }
  shape.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  shape.cull = VK_CULL_MODE_BACK_BIT;
  // Measured on RADV against GL (S3-models-dense): with the negative viewport, GL's
  // counter-clockwise front stays counter-clockwise here.
  shape.front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  return create_pipeline(pass, shape);
}

VkPipeline Pipelines::mesh(VkRenderPass pass) const {
  PipelineShape shape;
  shape.vertex = mesh_vertex_;
  shape.fragment = mesh_fragment_;
  shape.bindings.push_back({0, static_cast<std::uint32_t>(sizeof(draw_plan::MeshBatchVertex)),
                            VK_VERTEX_INPUT_RATE_VERTEX});
  using V = draw_plan::MeshBatchVertex;
  shape.attributes.push_back(
      {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, x))});
  shape.attributes.push_back(
      {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, u))});
  shape.attributes.push_back(
      {2, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, shade))});
  shape.attributes.push_back(
      {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, bounds))});
  shape.attributes.push_back(
      {4, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, page))});
  shape.attributes.push_back(
      {5, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, color))});
  shape.attributes.push_back(
      {6, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, textured))});
  shape.attributes.push_back(
      {7, 0, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(V, repeat))});
  shape.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  shape.cull = VK_CULL_MODE_NONE;  // the GL mesh program draws both faces
  return create_pipeline(pass, shape);
}

VkPipeline Pipelines::post(VkRenderPass pass, bool blend) const {
  PipelineShape shape;
  shape.vertex = post_vertex_;
  shape.fragment = post_fragment_;
  shape.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  shape.blend = blend;
  return create_pipeline(pass, shape);
}

VkPipeline Pipelines::ui_post(VkRenderPass pass, bool premultiplied) const {
  PipelineShape shape;
  shape.vertex = ui_post_vertex_;
  shape.fragment = ui_post_fragment_;
  shape.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  shape.cull = VK_CULL_MODE_NONE;
  shape.blend = premultiplied;
  shape.premultiplied = true;
  return create_pipeline(pass, shape);
}

VkPipeline Pipelines::ui(VkRenderPass pass, PipelineShape::Stencil stencil) const {
  PipelineShape shape;
  shape.stencil = stencil;
  shape.vertex = ui_vertex_;
  shape.fragment = ui_fragment_;
  // Rml::Vertex: position (2 floats), premultiplied RGBA8, texture coordinate (2 floats).
  shape.bindings.push_back({0, 20, VK_VERTEX_INPUT_RATE_VERTEX});
  shape.attributes.push_back({0, 0, VK_FORMAT_R32G32_SFLOAT, 0});
  shape.attributes.push_back({1, 0, VK_FORMAT_R8G8B8A8_UNORM, 8});
  shape.attributes.push_back({2, 0, VK_FORMAT_R32G32_SFLOAT, 12});
  shape.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  shape.cull = VK_CULL_MODE_NONE;
  shape.blend = true;
  shape.premultiplied = true;
  return create_pipeline(pass, shape);
}

Pipelines& pipelines() {
  static Pipelines instance;
  return instance;
}
}  // namespace realm::gfx::vk
