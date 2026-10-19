#include "VkPipelines.hpp"
#include "VkFrame.hpp"
#include "VkResources.hpp"
#include "realm/stage3D/DrawTypes.hpp"
#include "../DrawPlan.hpp"
#include "vk_shaders.hpp"
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
    case 4:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:
      throw std::logic_error("Quad attribute with an unsupported component count");
  }
}
}  // namespace

void Pipelines::create() {
  auto& device = context();
  quad_vertex_ = make_module(spirv::quad_vert_spv, spirv::quad_vert_spv_size);
  quad_fragment_ = make_module(spirv::quad_frag_spv, spirv::quad_frag_spv_size);

  // Set 0: atlas array, then the wall corners.
  std::array<VkDescriptorSetLayoutBinding, 2> atlas_bindings{};
  atlas_bindings[0].binding = 0;
  atlas_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  atlas_bindings[0].descriptorCount = 1;
  atlas_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  atlas_bindings[1].binding = 1;
  atlas_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  atlas_bindings[1].descriptorCount = 1;
  atlas_bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  auto atlas_info = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
  atlas_info.bindingCount = static_cast<std::uint32_t>(atlas_bindings.size());
  atlas_info.pBindings = atlas_bindings.data();
  check(device.fn.vkCreateDescriptorSetLayout(device.device, &atlas_info, nullptr, &atlas_layout_),
        "vkCreateDescriptorSetLayout");

  // Set 1: one surface.
  VkDescriptorSetLayoutBinding surface_binding{};
  surface_binding.binding = 0;
  surface_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  surface_binding.descriptorCount = 1;
  surface_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  auto surface_info = make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
  surface_info.bindingCount = 1;
  surface_info.pBindings = &surface_binding;
  check(device.fn.vkCreateDescriptorSetLayout(device.device, &surface_info, nullptr, &surface_layout_),
        "vkCreateDescriptorSetLayout");

  const std::array<VkDescriptorSetLayout, 2> set_layouts{atlas_layout_, surface_layout_};
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

  // Stand-ins for handle 0: a 16-byte wall buffer, and a 1x1 surface (never sampled when standalone is 0).
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
  retire(atlas_);
  for (auto pool : pools_) {
    device.fn.vkDestroyDescriptorPool(device.device, pool, nullptr);
  }
  pools_.clear();
  device.fn.vkDestroyPipelineLayout(device.device, layout_, nullptr);
  device.fn.vkDestroyDescriptorSetLayout(device.device, atlas_layout_, nullptr);
  device.fn.vkDestroyDescriptorSetLayout(device.device, surface_layout_, nullptr);
  device.fn.vkDestroyShaderModule(device.device, quad_vertex_, nullptr);
  device.fn.vkDestroyShaderModule(device.device, quad_fragment_, nullptr);
  destroy_buffer(walls_);
  layout_ = VK_NULL_HANDLE;
  atlas_layout_ = surface_layout_ = VK_NULL_HANDLE;
  quad_vertex_ = quad_fragment_ = VK_NULL_HANDLE;
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

VkDescriptorSet Pipelines::atlas_set(std::uint32_t atlas) {
  if (atlas_.set != VK_NULL_HANDLE && atlas_.key == atlas) {
    return atlas_.set;
  }
  auto& device = context();
  retire(atlas_);
  const auto [set, pool] = allocate(atlas_layout_);
  VkDescriptorImageInfo image{};
  image.sampler = resources().sampler(false);
  image.imageView = resources().view(atlas);
  image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorBufferInfo walls{walls_.buffer, 0, VK_WHOLE_SIZE};
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
  writes[1].pBufferInfo = &walls;
  device.fn.vkUpdateDescriptorSets(device.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
  atlas_ = Cached{atlas, set, pool, nullptr, nullptr};
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
  cached = Cached{surface, set, pool, view, sampler};
  return set;
}

VkPipeline Pipelines::quad_pipeline(VkRenderPass pass, bool list) const {
  auto& device = context();
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  stages[0] = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = quad_vertex_;
  stages[0].pName = "main";
  stages[1] = make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = quad_fragment_;
  stages[1].pName = "main";

  // The instance layout of a Quad: one binding, nine attributes at the offsets of draw_plan.
  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = static_cast<std::uint32_t>(quad_instance_bytes);
  binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
  std::array<VkVertexInputAttributeDescription, draw_plan::quad_sizes.size()> attributes{};
  for (std::size_t i = 0; i < attributes.size(); ++i) {
    attributes[i].location = static_cast<std::uint32_t>(i);
    attributes[i].binding = 0;
    attributes[i].format = format_of(draw_plan::quad_sizes[i]);
    attributes[i].offset = static_cast<std::uint32_t>(draw_plan::quad_offsets[i]);
  }
  auto vertex_input = make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
  vertex_input.pVertexAttributeDescriptions = attributes.data();

  auto assembly = make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
  assembly.topology = list ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

  auto viewport = make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  auto raster = make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  auto multisample = make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // The GL blend: SRC_ALPHA / ONE_MINUS_SRC_ALPHA for colour, ONE / ONE_MINUS_SRC_ALPHA for alpha.
  VkPipelineColorBlendAttachmentState blend{};
  blend.blendEnable = VK_TRUE;
  blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.colorBlendOp = VK_BLEND_OP_ADD;
  blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.alphaBlendOp = VK_BLEND_OP_ADD;
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  auto color = make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
  color.attachmentCount = 1;
  color.pAttachments = &blend;

  const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
  info.pDynamicState = &dynamic;
  info.layout = layout_;
  info.renderPass = pass;
  info.subpass = 0;
  VkPipeline pipeline = VK_NULL_HANDLE;
  check(device.fn.vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
        "vkCreateGraphicsPipelines");
  return pipeline;
}

Pipelines& pipelines() {
  static Pipelines instance;
  return instance;
}
}  // namespace realm::gfx::vk
