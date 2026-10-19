#include "VkRenderer.hpp"
#include "VkArena.hpp"
#include "VkFrame.hpp"
#include "VkPipelines.hpp"
#include "VkResources.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "realm/stage3D/Atlas.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/util/Telemetry.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace realm {
namespace {
using gfx::vk::check;
using gfx::vk::context;
using gfx::vk::frames;
using gfx::vk::pipelines;
using gfx::vk::PostPush;
using gfx::vk::QuadPush;
using gfx::vk::resources;

template <typename T>
T make(VkStructureType type) {
  T value{};
  value.sType = type;
  return value;
}

// A single-subpass pass over the swapchain format: the first pass of a frame clears, later passes
// keep what is there. Each pass ends with the image in COLOR_ATTACHMENT_OPTIMAL.
VkRenderPass make_pass(VkFormat format, VkAttachmentLoadOp load, VkImageLayout initial) {
  VkAttachmentDescription attachment{};
  attachment.format = format;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = load;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = initial;
  attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;
  // Earlier transfers and the previous pass finish before this pass writes.
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                             VK_ACCESS_SHADER_READ_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  auto info = make<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;
  VkRenderPass pass = VK_NULL_HANDLE;
  check(context().fn.vkCreateRenderPass(context().device, &info, nullptr, &pass), "vkCreateRenderPass");
  return pass;
}

void validate_dimensions(int width, int height, int clip_width, std::size_t total) {
  if (width <= 0 || height <= 0 || clip_width < 0 || clip_width > width || total > 1000000) {
    throw std::invalid_argument("Invalid draw dimensions or quad count");
  }
}

VkRect2D rect(int width, int height) {
  VkRect2D result{};
  result.offset = {0, 0};
  result.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
  return result;
}

// Two layer buffers hold the same instances when their bytes match (the owner and texture of a quad
// are not in the instances, so they do not count).
bool same_instances(const std::vector<std::byte>& uploaded, const std::vector<std::byte>& packed) {
  return uploaded.size() == packed.size() &&
         (packed.empty() || std::memcmp(uploaded.data(), packed.data(), packed.size()) == 0);
}

// A device-local colour image for the post targets: sampled, and rendered into.
void create_color_image(VkFormat format, std::uint32_t width, std::uint32_t height, VkImage& image,
                        VkDeviceMemory& memory, VkImageView& view) {
  auto& device = context();
  auto info = make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = format;
  info.extent = {width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  check(device.fn.vkCreateImage(device.device, &info, nullptr, &image), "vkCreateImage");
  VkMemoryRequirements requirements{};
  device.fn.vkGetImageMemoryRequirements(device.device, image, &requirements);
  auto allocation = make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      device.memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(device.fn.vkAllocateMemory(device.device, &allocation, nullptr, &memory),
        "vkAllocateMemory");
  check(device.fn.vkBindImageMemory(device.device, image, memory, 0), "vkBindImageMemory");
  auto view_info = make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
  view_info.image = image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = format;
  view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  check(device.fn.vkCreateImageView(device.device, &view_info, nullptr, &view),
        "vkCreateImageView");
}
}  // namespace

VkRenderer::VkRenderer(std::size_t capacity, bool /*persistent_streaming*/,
                       std::size_t /*persistent_minimum_bytes*/) {
  if (!capacity || capacity > 1000000) {
    throw std::invalid_argument("Invalid quad capacity");
  }
  const auto format = frames().format();
  clear_pass_ = make_pass(format, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED);
  load_pass_ = make_pass(format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  strip_ = pipelines().quad(clear_pass_, false);
  list_ = pipelines().quad(clear_pass_, true);
  model_ = pipelines().model(clear_pass_);
  mesh_ = pipelines().mesh(clear_pass_);
  post_plain_ = pipelines().post(clear_pass_, false);
  post_blend_ = pipelines().post(clear_pass_, true);
  // The model triangles are uploaded once; every instance refers to a range of them.
  const auto geometry = model_geometry();
  model_geometry_ = resources().create_buffer(std::as_bytes(geometry));
  if (timers_supported()) {
    for (auto& timer : timers_) {
      auto info = make<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
      info.queryType = VK_QUERY_TYPE_TIMESTAMP;
      info.queryCount = timer_queries;
      check(context().fn.vkCreateQueryPool(context().device, &info, nullptr, &timer.pool),
            "vkCreateQueryPool");
    }
  }
}

VkRenderer::~VkRenderer() {
  auto& device = context();
  if (!device.ready()) {
    return;
  }
  // A frame may still be recording with this renderer's pipelines, framebuffers and sets in its
  // command buffer (draw() without finish_frame()). It is submitted now: recording into it later, or
  // submitting it, after those objects are gone is undefined (RADV crashes in the next
  // vkCmdClearColorImage, which saves and restores the bound pipeline).
  frames().finish();
  // Frames in flight may still use the pipelines, framebuffers and images.
  device.fn.vkDeviceWaitIdle(device.device);
  pipelines().release_scene();
  for (auto& [view, framebuffer] : framebuffers_) {
    device.fn.vkDestroyFramebuffer(device.device, framebuffer, nullptr);
  }
  for (auto& target : post_) {
    if (target.framebuffer != VK_NULL_HANDLE) {
      device.fn.vkDestroyFramebuffer(device.device, target.framebuffer, nullptr);
    }
    if (target.view != VK_NULL_HANDLE) {
      device.fn.vkDestroyImageView(device.device, target.view, nullptr);
    }
    if (target.image != VK_NULL_HANDLE) {
      device.fn.vkDestroyImage(device.device, target.image, nullptr);
    }
    if (target.memory != VK_NULL_HANDLE) {
      device.fn.vkFreeMemory(device.device, target.memory, nullptr);
    }
  }
  for (auto& buffer : walls_) {
    pipelines().forget_atlas_sets(0, buffer.buffer);
    gfx::vk::destroy_buffer(buffer);
  }
  resources().release(model_geometry_);
  for (const auto pipeline : {strip_, list_, model_, mesh_, post_plain_, post_blend_}) {
    device.fn.vkDestroyPipeline(device.device, pipeline, nullptr);
  }
  for (auto& timer : timers_) {
    if (timer.pool != VK_NULL_HANDLE) {
      device.fn.vkDestroyQueryPool(device.device, timer.pool, nullptr);
    }
  }
  device.fn.vkDestroyRenderPass(device.device, clear_pass_, nullptr);
  device.fn.vkDestroyRenderPass(device.device, load_pass_, nullptr);
}

VkFramebuffer VkRenderer::framebuffer(VkImageView view, VkExtent2D extent) {
  auto& device = context();
  if (framebuffer_generation_ != frames().generation()) {
    // The swapchain was rebuilt; its views are new and the rebuild waited for every frame.
    for (auto& [old_view, old] : framebuffers_) {
      device.fn.vkDestroyFramebuffer(device.device, old, nullptr);
    }
    framebuffers_.clear();
    framebuffer_generation_ = frames().generation();
  }
  for (const auto& [known, framebuffer] : framebuffers_) {
    if (known == view) {
      return framebuffer;
    }
  }
  auto info = make<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
  info.renderPass = clear_pass_;  // compatible with the load pass
  info.attachmentCount = 1;
  info.pAttachments = &view;
  info.width = extent.width;
  info.height = extent.height;
  info.layers = 1;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  check(device.fn.vkCreateFramebuffer(device.device, &info, nullptr, &framebuffer), "vkCreateFramebuffer");
  framebuffers_.emplace_back(view, framebuffer);
  return framebuffer;
}

void VkRenderer::ensure_post_targets(std::uint32_t width, std::uint32_t height) {
  if (post_[0].image != VK_NULL_HANDLE && post_extent_.width == width &&
      post_extent_.height == height) {
    return;
  }
  auto& device = context();
  auto& frame = frames();
  // The old targets may still be sampled by frames in flight: they go with the frame that retires
  // them.
  for (auto& target : post_) {
    const auto framebuffer = target.framebuffer;
    const auto view = target.view;
    const auto image = target.image;
    const auto memory = target.memory;
    if (image != VK_NULL_HANDLE) {
      frame.retire([framebuffer, view, image, memory] {
        auto& dev = context();
        if (framebuffer != VK_NULL_HANDLE) {
          dev.fn.vkDestroyFramebuffer(dev.device, framebuffer, nullptr);
        }
        dev.fn.vkDestroyImageView(dev.device, view, nullptr);
        dev.fn.vkDestroyImage(dev.device, image, nullptr);
        dev.fn.vkFreeMemory(dev.device, memory, nullptr);
      });
    }
    target = Offscreen{};
  }
  pipelines().release_scene();
  const auto format = frame.format();
  for (auto& target : post_) {
    create_color_image(format, width, height, target.image, target.memory, target.view);
    auto info = make<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    info.renderPass = clear_pass_;
    info.attachmentCount = 1;
    info.pAttachments = &target.view;
    info.width = width;
    info.height = height;
    info.layers = 1;
    check(device.fn.vkCreateFramebuffer(device.device, &info, nullptr, &target.framebuffer),
          "vkCreateFramebuffer");
  }
  post_extent_ = {width, height};
}

void VkRenderer::prepare_layers(std::span<const Quad> list) {
  for (const auto& quad : list) {
    if (quad.kind != 14) {
      continue;
    }
    auto* layer = const_cast<QuadLayer*>(static_cast<const QuadLayer*>(quad.owner.get()));
    if (!layer || layer->quads.size() > 1000000) {
      throw std::logic_error("Invalid quad layer");
    }
    layer->packed.resize(layer->quads.size() * quad_instance_bytes);
    layer->meta.resize(layer->quads.size());
    const WallGeometryStore* unused_walls{};
    draw_plan::MeshBatch unused_meshes;
    draw_plan::pack_quads(layer->quads, layer->packed.data(), layer->meta.data(), unused_walls, false,
                          unused_meshes);
    if (!same_instances(layer->uploaded, layer->packed)) {
      // A new range per change; the old one returns to the arena once the frames in flight are
      // done.
      if (layer->buffer) {
        resources().release(layer->buffer);
      }
      layer->buffer = resources().create_buffer(layer->packed);
      telemetry::add(telemetry::Counter::quad_upload_bytes, layer->packed.size());
      layer->uploaded.swap(layer->packed);
    }
    prepare_layers(layer->quads);
  }
}

VkBuffer VkRenderer::upload_walls(const WallGeometryStore& store) {
  const auto slot = frames().slot();
  auto& buffer = walls_[slot];
  const auto step = draw_plan::plan_walls(wall_sync_[slot], store, store.limit());
  if (step.none) {
    return buffer.buffer != VK_NULL_HANDLE ? buffer.buffer : pipelines().dummy_walls();
  }
  if (step.reallocate) {
    // Only this slot's frames used the old buffer, and they are finished: it goes now, from the
    // retire list.
    if (buffer.buffer != VK_NULL_HANDLE) {
      pipelines().forget_atlas_sets(0, buffer.buffer);
      gfx::vk::Buffer old = buffer;
      frames().retire([old]() mutable { gfx::vk::destroy_buffer(old); });
    }
    buffer = gfx::vk::create_host_buffer(
        static_cast<VkDeviceSize>(step.capacity) * sizeof(WallGpuVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  }
  if (step.end > step.first) {
    const auto bytes = (step.end - step.first) * sizeof(WallGpuVertex);
    std::memcpy(buffer.mapped + step.first * sizeof(WallGpuVertex),
                store.vertices().data() + step.first, bytes);
    telemetry::add(telemetry::Counter::mesh_upload_bytes, bytes);
  }
  return buffer.buffer;
}

gfx::vk::Ring::Span VkRenderer::pack(std::span<const Quad> quads, std::span<const Quad> overlay) {
  const auto total = quads.size() + overlay.size();
  meshes_.clear();
  meta_.resize(total);
  // The same count as GL's instance upload of the submission.
  telemetry::add(telemetry::Counter::quad_upload_bytes, total * quad_instance_bytes);
  auto span = frames().ring().allocate(std::max<VkDeviceSize>(total, 1) * quad_instance_bytes, 16);
  const WallGeometryStore* walls{};
  draw_plan::pack_quads(quads, span.data, meta_.data(), walls, true, meshes_);
  draw_plan::pack_quads(overlay, span.data + quads.size() * quad_instance_bytes,
                        meta_.data() + quads.size(), walls, true, meshes_);
  walls_buffer_ = walls ? upload_walls(*walls) : pipelines().dummy_walls();
  mesh_buffer_ = VK_NULL_HANDLE;
  mesh_base_ = 0;
  if (!meshes_.vertices.empty()) {
    const auto bytes = meshes_.vertices.size() * sizeof(draw_plan::MeshBatchVertex);
    auto mesh = frames().ring().allocate(bytes, 16);
    std::memcpy(mesh.data, meshes_.vertices.data(), bytes);
    telemetry::add(telemetry::Counter::mesh_upload_bytes, bytes);
    mesh_buffer_ = mesh.buffer;
    mesh_base_ = mesh.offset;
  }
  return span;
}

void VkRenderer::begin_pass(VkRenderPass pass, VkFramebuffer target, VkExtent2D extent, bool clear,
                            VkRect2D scissor, VkDescriptorSet scene, bool swapchain) {
  auto& frame = frames();
  const auto command = frame.record();
  // Transfers recorded so far are closed before the pass that may read them.
  resources().close_transfers(command);
  VkClearValue clear_value{};
  for (std::size_t i = 0; i < 4; ++i) {
    clear_value.color.float32[i] = clear_color_[i];
  }
  auto begin = make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
  begin.renderPass = pass;
  begin.framebuffer = target;
  begin.renderArea = rect(static_cast<int>(extent.width), static_cast<int>(extent.height));
  begin.clearValueCount = clear ? 1 : 0;
  begin.pClearValues = clear ? &clear_value : nullptr;
  auto& device = context();
  device.fn.vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
  swapchain_pass_ = swapchain;
  bound_ = VK_NULL_HANDLE;
  device.fn.vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines().layout(), 0, 1,
                                    &atlas_set_, 0, nullptr);
  if (scene != VK_NULL_HANDLE) {
    device.fn.vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      pipelines().layout(), 2, 1, &scene, 0, nullptr);
  }
  // The window viewport flipped (negative height): framebuffer y runs down the screen, as GL's y-up
  // NDC mapping required.
  VkViewport viewport{0.0f,
                      static_cast<float>(extent.height),
                      static_cast<float>(extent.width),
                      -static_cast<float>(extent.height),
                      0.0f,
                      1.0f};
  device.fn.vkCmdSetViewport(command, 0, 1, &viewport);
  device.fn.vkCmdSetScissor(command, 0, 1, &scissor);
}

void VkRenderer::end_pass() {
  auto& frame = frames();
  context().fn.vkCmdEndRenderPass(frame.record());
  if (swapchain_pass_) {
    frame.mark_written();
  }
  swapchain_pass_ = false;
}

void VkRenderer::use(VkCommandBuffer command, VkPipeline pipeline) {
  if (bound_ != pipeline) {
    context().fn.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    bound_ = pipeline;
  }
}

void VkRenderer::push(VkCommandBuffer command, const QuadPush& state) {
  context().fn.vkCmdPushConstants(command, pipelines().layout(),
                                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                  sizeof(QuadPush), &state);
}

void VkRenderer::bind_surface(VkCommandBuffer command, std::uint32_t texture) {
  const auto set = pipelines().surface_set(texture);
  context().fn.vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines().layout(), 1, 1,
                                       &set, 0, nullptr);
}

void VkRenderer::draw_list(VkCommandBuffer command, std::span<const Quad> list,
                           std::span<const QuadMeta> meta, VkBuffer buffer, VkDeviceSize base,
                           const QuadPush& state) {
  auto& device = context();
  for (std::size_t first = 0; first < list.size();) {
    if (meta[first].kind == 10) {
      // Retained terrain: one instance per chunk quad, drawn in ground space.
      const auto& command_quad = list[first];
      const auto* terrain = static_cast<const TerrainBuffer*>(meta[first].owner);
      if (!terrain) {
        throw std::logic_error("Missing terrain buffer");
      }
      QuadPush push_state = state;
      push_state.standalone = 0;
      push_state.ground_space = 1;
      std::copy_n(command_quad.origin.data(), 2, push_state.ground_origin);
      std::copy_n(command_quad.axis_x.data(), 2, push_state.ground_x);
      std::copy_n(command_quad.axis_y.data(), 2, push_state.ground_y);
      push_state.ground_time = command_quad.uv_offset[0];
      use(command, strip_);
      bind_surface(command, 0);
      push(command, push_state);
      const auto vertex_buffer = resources().buffer(terrain->buffer());
      VkDeviceSize offset = resources().buffer_offset(terrain->buffer());
      device.fn.vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer, &offset);
      device.fn.vkCmdDraw(command, 4, static_cast<std::uint32_t>(terrain->count()), 0, 0);
      ++draw_calls_;
      telemetry::add(telemetry::Counter::quads, terrain->count());
      telemetry::add(telemetry::Counter::triangles, terrain->count() * 2);
      ++first;
      continue;
    }
    if (meta[first].kind == 14) {
      // A clipped layer: its quads, clipped to the command's rectangle (drawn through the same runs).
      const auto& command_quad = list[first];
      auto* layer = const_cast<QuadLayer*>(static_cast<const QuadLayer*>(meta[first].owner));
      if (!layer || layer->quads.size() > 1000000) {
        throw std::logic_error("Invalid quad layer");
      }
      if (!layer->quads.empty()) {
        QuadPush clipped = state;
        clipped.clipped = 1;
        clipped.clip_rect[0] = command_quad.origin[0];
        clipped.clip_rect[1] = command_quad.origin[1];
        clipped.clip_rect[2] = command_quad.axis_x[0];
        clipped.clip_rect[3] = command_quad.axis_y[1];
        clipped.clip_cut = static_cast<float>(command_quad.uv_offset[0]);
        draw_list(command, layer->quads, layer->meta, resources().buffer(layer->buffer),
                  resources().buffer_offset(layer->buffer), clipped);
      }
      ++first;
      continue;
    }
    if (meta[first].kind == 9 && !meta[first].owner) {
      // Model instances keep their painter-stream position. A contiguous block binds the model
      // geometry once; consecutive copies of one model are one instanced draw.
      const auto end = draw_plan::model_block_end(meta, first);
      use(command, model_);
      const std::array<VkBuffer, 2> vertex_buffers{resources().buffer(model_geometry_), buffer};
      const std::array<VkDeviceSize, 2> offsets{resources().buffer_offset(model_geometry_), base};
      device.fn.vkCmdBindVertexBuffers(command, 0, 2, vertex_buffers.data(), offsets.data());
      QuadPush push_state = state;
      push_state.standalone = 0;
      push(command, push_state);
      for (std::size_t run = first; run < end;) {
        const auto next = draw_plan::model_run_end(meta, run, end);
        const auto range = model_range(static_cast<std::uint32_t>(meta[run].model));
        const bool measured = active_timer_ && active_timer_->models < timer_model_limit;
        if (measured) {
          stamp(5 + 2 * static_cast<std::uint32_t>(active_timer_->models),
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        }
        device.fn.vkCmdDraw(command, range.count, static_cast<std::uint32_t>(next - run),
                            range.first, static_cast<std::uint32_t>(run));
        if (measured) {
          stamp(6 + 2 * static_cast<std::uint32_t>(active_timer_->models),
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
          ++active_timer_->models;
        }
        if (active_timer_) {
          active_timer_->models_total += next - run;
        }
        ++draw_calls_;
        telemetry::add(telemetry::Counter::models, next - run);
        telemetry::add(telemetry::Counter::triangles, (next - run) * (range.count / 3));
        run = next;
      }
      first = end;
      continue;
    }
    if (meta[first].kind == 9) {
      // Prepared meshes: a run of consecutive meshes is one draw over contiguous vertices of the
      // ring.
      const auto end = draw_plan::mesh_block_end(meta, first);
      const auto run = draw_plan::take_mesh_ranges(meshes_, end - first);
      const auto totals = draw_plan::mesh_totals(run);
      if (totals.vertices) {
        use(command, mesh_);
        VkDeviceSize offset = mesh_base_;
        device.fn.vkCmdBindVertexBuffers(command, 0, 1, &mesh_buffer_, &offset);
        push(command, state);
        device.fn.vkCmdDraw(command, static_cast<std::uint32_t>(totals.vertices), 1,
                            static_cast<std::uint32_t>(run.front().first), 0);
        ++draw_calls_;
        telemetry::add(telemetry::Counter::models, totals.models);
        telemetry::add(telemetry::Counter::triangles, totals.vertices / 3);
      }
      first = end;
      continue;
    }
    // A run of one texture: instances [first, end). Runs holding walls draw triangle lists, six per
    // instance.
    const auto run = draw_plan::sprite_run(meta, first);
    const auto count = static_cast<std::uint32_t>(run.end - first);
    const auto sprites = count - static_cast<std::uint32_t>(run.walls);
    const bool walls = run.walls > 0;
    use(command, walls ? list_ : strip_);
    bind_surface(command, meta[first].texture);
    QuadPush push_state = state;
    push_state.standalone = meta[first].texture != 0 ? 1 : 0;
    push_state.triangle_list = walls ? 1 : 0;
    push(command, push_state);
    VkDeviceSize offset = base;
    device.fn.vkCmdBindVertexBuffers(command, 0, 1, &buffer, &offset);
    if (walls) {
      device.fn.vkCmdDraw(command, static_cast<std::uint32_t>(std::max(6, run.max_vertices)), count,
                          0, static_cast<std::uint32_t>(first));
      telemetry::add(telemetry::Counter::models, run.walls);
      telemetry::add(telemetry::Counter::triangles,
                     2 * static_cast<std::size_t>(sprites) + run.wall_triangles);
    } else {
      device.fn.vkCmdDraw(command, 4, count, 0, static_cast<std::uint32_t>(first));
      telemetry::add(telemetry::Counter::triangles, 2 * static_cast<std::size_t>(count));
    }
    telemetry::add(telemetry::Counter::quads, sprites);
    ++draw_calls_;
    first = run.end;
  }
}

void VkRenderer::barrier_to_sample(VkCommandBuffer command, VkImage image) {
  auto barrier = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
  barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  context().fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                                    nullptr, 1, &barrier);
}

void VkRenderer::post_pass(VkRenderPass pass, VkFramebuffer target, VkExtent2D extent, bool clear,
                           bool swapchain, VkImageView source, VkRect2D scissor,
                           const PostPush& values, bool blend) {
  const auto scene = pipelines().scene_set(source);
  begin_pass(pass, target, extent, clear, scissor, scene, swapchain);
  const auto command = frames().record();
  use(command, blend ? post_blend_ : post_plain_);
  context().fn.vkCmdPushConstants(command, pipelines().layout(),
                                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                  sizeof(PostPush), &values);
  context().fn.vkCmdDraw(command, 4, 1, 0, 0);
  end_pass();
}

void VkRenderer::record_screen_overlays(VkCommandBuffer command, int width, int height,
                                        int clip_width, const ScreenEffects& effects) {
  auto& device = context();
  // The breath vignette first, then the sidebar strip over it (Map.as child order).
  if (effects.vignette > 0) {
    PostPush values;
    values.viewport[0] = static_cast<float>(width);
    values.viewport[1] = static_cast<float>(height);
    values.region[0] = static_cast<float>(clip_width);
    values.region[1] = static_cast<float>(height);
    values.rect[2] = static_cast<float>(clip_width);
    values.rect[3] = static_cast<float>(height);
    values.mode = 1;
    values.alpha = effects.vignette;
    use(command, post_blend_);
    device.fn.vkCmdPushConstants(command, pipelines().layout(),
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(PostPush), &values);
    device.fn.vkCmdDraw(command, 4, 1, 0, 0);
    ++draw_calls_;
  }
  if (effects.sidebar > 0 && effects.sidebar <= static_cast<float>(clip_width)) {
    PostPush values;
    values.viewport[0] = static_cast<float>(width);
    values.viewport[1] = static_cast<float>(height);
    values.region[0] = static_cast<float>(clip_width);
    values.region[1] = static_cast<float>(height);
    values.rect[0] = static_cast<float>(clip_width) - effects.sidebar;
    values.rect[2] = effects.sidebar;
    values.rect[3] = static_cast<float>(height);
    values.mode = 2;
    values.sidebar = effects.sidebar;
    use(command, post_blend_);
    device.fn.vkCmdPushConstants(command, pipelines().layout(),
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                 sizeof(PostPush), &values);
    device.fn.vkCmdDraw(command, 4, 1, 0, 0);
    ++draw_calls_;
  }
}

void VkRenderer::draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height,
                      int clip_width, std::span<const Quad> overlay, const ScreenEffects& effects) {
  validate_dimensions(width, height, clip_width, quads.size() + overlay.size());
  draw_calls_ = 0;
  begin_timing();
  prepare_layers(quads);
  prepare_layers(overlay);
  const auto span = pack(quads, overlay);
  atlas_set_ = pipelines().atlas_set(atlas.texture(), walls_buffer_, frames().slot());
  const auto w = static_cast<std::uint32_t>(width);
  const auto h = static_cast<std::uint32_t>(height);
  const auto full = rect(width, height);
  const auto clipped = rect(clip_width, height);
  const std::span<const QuadMeta> meta(meta_);
  const auto quad_meta = meta.first(quads.size());
  const auto overlay_meta = meta.subspan(quads.size());
  const auto overlay_base = span.offset + quads.size() * quad_instance_bytes;
  QuadPush state;
  state.viewport[0] = static_cast<float>(width);
  state.viewport[1] = static_cast<float>(height);

  const bool filtered = effects.filtered();
  const bool screen = effects.vignette > 0 || effects.sidebar > 0;
  const bool overlay_filtered = filtered && effects.whole;
  const auto& target = frames().target(w, h);
  const auto swap = framebuffer(target.view, target.extent);
  if (!filtered) {
    // The world, the screen overlays and the HUD overlay, one pass on the window.
    const auto dummy = pipelines().scene_set(resources().view(pipelines().dummy_surface()));
    stamp(1, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    begin_pass(clear_pass_, swap, target.extent, true, clipped, screen ? dummy : VK_NULL_HANDLE,
               true);
    const auto command = frames().record();
    draw_list(command, quads, quad_meta, span.buffer, span.offset, state);
    stamp(2, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    if (screen) {
      record_screen_overlays(command, width, height, clip_width, effects);
    }
    if (!overlay.empty()) {
      context().fn.vkCmdSetScissor(command, 0, 1, &full);
      draw_list(command, overlay, overlay_meta, span.buffer, overlay_base, state);
    }
    stamp(3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    end_pass();
    end_timing();
    telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
    return;
  }

  // A filter: the world renders offscreen, then the post passes resolve it into the window.
  ensure_post_targets(w, h);
  const auto dummy = pipelines().scene_set(resources().view(pipelines().dummy_surface()));
  stamp(1, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  // GL clears the window first; the post passes only write the clip region.
  begin_pass(clear_pass_, swap, target.extent, true, full, VK_NULL_HANDLE, true);
  end_pass();
  begin_pass(clear_pass_, post_[0].framebuffer, target.extent, true, clipped, VK_NULL_HANDLE,
             false);
  draw_list(frames().record(), quads, quad_meta, span.buffer, span.offset, state);
  end_pass();
  stamp(2, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  if (overlay_filtered && screen) {
    begin_pass(load_pass_, post_[0].framebuffer, target.extent, false, clipped, dummy, false);
    record_screen_overlays(frames().record(), width, height, clip_width, effects);
    end_pass();
  }
  if (overlay_filtered && !overlay.empty()) {
    begin_pass(load_pass_, post_[0].framebuffer, target.extent, false, full, VK_NULL_HANDLE, false);
    draw_list(frames().record(), overlay, overlay_meta, span.buffer, overlay_base, state);
    end_pass();
  }
  const int region_width = overlay_filtered ? width : clip_width;
  auto scene_clip = rect(region_width, height);
  barrier_to_sample(frames().record(), post_[0].image);
  PostPush base;
  base.viewport[0] = static_cast<float>(width);
  base.viewport[1] = static_cast<float>(height);
  base.region[0] = static_cast<float>(region_width);
  base.region[1] = static_cast<float>(height);
  base.rect[2] = static_cast<float>(region_width);
  base.rect[3] = static_cast<float>(height);
  if (effects.blur > 0) {
    // Horizontal blur into offscreen 1, vertical into the window.
    PostPush horizontal = base;
    horizontal.axis[0] = 1;
    horizontal.blur = effects.blur;
    post_pass(clear_pass_, post_[1].framebuffer, target.extent, true, false, post_[0].view,
              scene_clip, horizontal, false);
    barrier_to_sample(frames().record(), post_[1].image);
    PostPush vertical = base;
    vertical.axis[1] = 1;
    vertical.blur = effects.blur;
    vertical.blind = effects.blind ? 1 : 0;
    post_pass(load_pass_, swap, target.extent, false, true, post_[1].view, scene_clip, vertical,
              false);
  } else {
    PostPush plain = base;
    plain.blind = effects.blind ? 1 : 0;
    post_pass(load_pass_, swap, target.extent, false, true, post_[0].view, scene_clip, plain,
              false);
  }
  stamp(3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  if (screen && !overlay_filtered) {
    begin_pass(load_pass_, swap, target.extent, false, clipped, dummy, true);
    record_screen_overlays(frames().record(), width, height, clip_width, effects);
    end_pass();
  }
  if (!overlay.empty() && !overlay_filtered) {
    begin_pass(load_pass_, swap, target.extent, false, full, VK_NULL_HANDLE, true);
    draw_list(frames().record(), overlay, overlay_meta, span.buffer, overlay_base, state);
    end_pass();
  }
  end_timing();
  telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
}

void VkRenderer::draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width,
                              int height) {
  validate_dimensions(width, height, width, overlay.size());
  if (overlay.empty()) {
    return;
  }
  draw_calls_ = 0;
  prepare_layers(overlay);
  const auto span = pack({}, overlay);
  atlas_set_ = pipelines().atlas_set(atlas.texture(), walls_buffer_, frames().slot());
  const auto w = static_cast<std::uint32_t>(width);
  const auto h = static_cast<std::uint32_t>(height);
  const auto& target = frames().target(w, h);
  const auto swap = framebuffer(target.view, target.extent);
  QuadPush state;
  state.viewport[0] = static_cast<float>(width);
  state.viewport[1] = static_cast<float>(height);
  begin_pass(load_pass_, swap, target.extent, false, rect(width, height), VK_NULL_HANDLE, true);
  draw_list(frames().record(), overlay, std::span<const QuadMeta>(meta_), span.buffer, span.offset, state);
  end_pass();
  telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
}

void VkRenderer::timing(telemetry::Recorder* recorder, std::uint64_t frame_id, bool normal) {
  recorder_ = recorder;
  sample_frame_ = frame_id;
  sample_normal_ = normal;
}

bool VkRenderer::timers_supported() const {
  return context().timestamp_bits > 0 && context().timestamp_period_ns > 0;
}

void VkRenderer::stamp(std::uint32_t index, VkPipelineStageFlagBits stage) {
  if (!active_timer_) {
    return;
  }
  context().fn.vkCmdWriteTimestamp(frames().record(), stage, active_timer_->pool, index);
}

void VkRenderer::begin_timing() {
  if (!timers_supported()) {
    return;
  }
  const auto slot = frames().slot();
  static_cast<void>(
      frames().record());  // the slot's fence has passed: its previous sample can be read
  poll_timer(slot);
  active_timer_ = nullptr;
  if (!recorder_) {
    return;
  }
  auto& timer = timers_[slot];
  if (timer.pending) {
    ++skipped_;  // the previous sample of this slot is still unread: no measurement this frame
    return;
  }
  context().fn.vkCmdResetQueryPool(frames().record(), timer.pool, 0, timer_queries);
  timer.frame_id = sample_frame_;
  timer.normal = sample_normal_;
  timer.models = 0;
  timer.models_total = 0;
  active_timer_ = &timer;
  stamp(0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
}

void VkRenderer::end_timing() {
  if (!active_timer_) {
    return;
  }
  stamp(4, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  active_timer_->pending = true;
  active_timer_ = nullptr;
}

void VkRenderer::poll_timer(std::uint32_t slot) {
  auto& timer = timers_[slot];
  if (!timer.pending) {
    return;
  }
  auto& device = context();
  // Each query returns its value and an availability word (64-bit results).
  std::array<std::uint64_t, 2 * timer_queries> data{};
  const auto result = device.fn.vkGetQueryPoolResults(
      device.device, timer.pool, 0, timer_queries, sizeof(data), data.data(),
      2 * sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if (result != VK_SUCCESS && result != VK_NOT_READY) {
    check(result, "vkGetQueryPoolResults");
  }
  const std::size_t count = 5 + 2 * timer.models;
  for (std::size_t i = 0; i < count; ++i) {
    if (data[2 * i + 1] == 0) {
      return;  // not ready yet
    }
  }
  const auto bits = context().timestamp_bits;
  const std::uint64_t mask = bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
  const double period = context().timestamp_period_ns;
  const auto ns = [&](std::size_t index) {
    return static_cast<double>(data[2 * index] & mask) * period;
  };
  const auto elapsed = [&](std::size_t a, std::size_t b) { return (ns(b) - ns(a)) / 1000000.0; };
  double models_ms = 0;
  for (std::size_t i = 0; i < timer.models; ++i) {
    models_ms += elapsed(5 + 2 * i, 6 + 2 * i);
  }
  gpu_ms_ = elapsed(0, 4);
  if (recorder_) {
    recorder_->gpu({timer.frame_id, timer.normal, gpu_ms_, elapsed(0, 1), elapsed(1, 2),
                    elapsed(2, 3), elapsed(3, 4), models_ms, timer.models, timer.models_total});
  }
  timer.pending = false;
}

void VkRenderer::poll_gpu() {
  for (std::uint32_t slot = 0; slot < timers_.size(); ++slot) {
    poll_timer(slot);
  }
}

std::size_t VkRenderer::pending_gpu() const {
  return static_cast<std::size_t>(std::count_if(timers_.begin(), timers_.end(),
                                                [](const Timer& timer) { return timer.pending; }));
}

Image VkRenderer::capture(int width, int height, bool presented) const {
  if (presented) {
    return presented_image(width, height);
  }
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Invalid capture dimensions");
  }
  auto& frame = frames();
  if (captured_frame_ == frame.serial() && captured_.width == width && captured_.height == height) {
    return captured_;
  }
  const auto w = static_cast<std::uint32_t>(width);
  const auto h = static_cast<std::uint32_t>(height);
  // Ensures the frame has its image; the image is read through read_target.
  static_cast<void>(frame.target(w, h));
  const auto bytes = static_cast<VkDeviceSize>(w) * h * 4;
  auto host = gfx::vk::create_host_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  try {
    frame.read_target(host.buffer, w, h);
  } catch (...) {
    gfx::vk::destroy_buffer(host);
    throw;
  }
  Image image{width, height, std::vector<unsigned char>(static_cast<std::size_t>(bytes))};
  std::memcpy(image.pixels.data(), host.mapped, image.pixels.size());
  gfx::vk::destroy_buffer(host);
  if (frame.format() == VK_FORMAT_B8G8R8A8_UNORM) {
    // The swapchain is BGRA; the capture is RGBA like the GL backbuffer read.
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
      std::swap(image.pixels[i], image.pixels[i + 2]);
    }
  }
  captured_ = image;
  captured_frame_ = frame.serial();
  return image;
}

namespace {
// REALM_VK_STATS=1 samples the steady-state holdings at each frame's end; report_stats prints the
// peaks.
struct StatsPeaks {
  std::uint64_t frames = 0;
  VkDeviceSize ring_slot = 0;
  std::size_t arena_blocks = 0;
  VkDeviceSize arena_bytes = 0;
  std::size_t surface_sets = 0;
  std::size_t pools = 0;
};

StatsPeaks& stats_peaks() {
  static StatsPeaks instance;
  return instance;
}

bool stats_enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("REALM_VK_STATS");
    return value && std::string_view(value) == "1";
  }();
  return enabled;
}

void sample_stats() {
  auto& peaks = stats_peaks();
  peaks.frames += 1;
  peaks.ring_slot = std::max(peaks.ring_slot, frames().ring_retained_max());
  peaks.arena_blocks = std::max(peaks.arena_blocks, gfx::vk::arena().live_blocks());
  peaks.arena_bytes = std::max(peaks.arena_bytes, gfx::vk::arena().reserved());
  peaks.surface_sets = std::max(peaks.surface_sets, pipelines().surface_sets());
  peaks.pools = std::max(peaks.pools, pipelines().pools());
}
}  // namespace

void VkRenderer::finish_frame() {
  if (stats_enabled()) {
    sample_stats();
  }
  frames().finish();
}

void report_stats() noexcept {
  if (!stats_enabled()) {
    return;
  }
  const auto& peaks = stats_peaks();
  constexpr double mb = 1048576.0;
  std::fprintf(stderr,
               "Vulkan stats (%llu frames sampled): ring per slot peak %.1f MB (now %.1f MB, slots "
               "total %.1f MB); "
               "arena blocks peak %zu, reserved peak %.1f MB (now %zu blocks, %.1f MB); "
               "surface descriptor sets peak %zu (now %zu); descriptor pools peak %zu (now %zu)\n",
               static_cast<unsigned long long>(peaks.frames),
               static_cast<double>(peaks.ring_slot) / mb,
               static_cast<double>(frames().ring_retained_max()) / mb,
               static_cast<double>(frames().ring_retained()) / mb, peaks.arena_blocks,
               static_cast<double>(peaks.arena_bytes) / mb, gfx::vk::arena().live_blocks(),
               static_cast<double>(gfx::vk::arena().reserved()) / mb, peaks.surface_sets,
               pipelines().surface_sets(), peaks.pools, pipelines().pools());
}

namespace {
// The retained copy of the last presented image. Vulkan leaves a presented image undefined, so the
// copy is the only way to read it back.
struct PresentedCopy {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkExtent2D extent{};
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool valid = false;    // holds the image of a presented frame
  bool enabled = false;  // set_presented_capture: frames are copied while true
};

PresentedCopy& presented() {
  static PresentedCopy instance;
  return instance;
}

// REALM_VK_PRESENTED_COPY=1 keeps copies on regardless of the app (the measurement and harness
// override).
bool presented_copy_forced() {
  static const bool forced = [] {
    const char* value = std::getenv("REALM_VK_PRESENTED_COPY");
    return value && std::string_view(value) == "1";
  }();
  return forced;
}

bool presented_copy_enabled() {
  return presented().enabled || presented_copy_forced();
}
}  // namespace

void VkRenderer::set_presented_capture(bool enabled) {
  auto& copy = presented();
  if (enabled && !copy.enabled) {
    // A copy left from an earlier session is not this session's last frame.
    copy.valid = false;
  }
  copy.enabled = enabled;
}

void release_presented_copy() noexcept {
  auto& copy = presented();
  auto& device = context();
  if (device.ready() && copy.image != VK_NULL_HANDLE) {
    device.fn.vkDestroyImage(device.device, copy.image, nullptr);
    device.fn.vkFreeMemory(device.device, copy.memory, nullptr);
  }
  copy = PresentedCopy{};
}

void record_presented_copy(VkCommandBuffer command) {
  auto& frame = frames();
  if (!presented_copy_enabled() || !frame.has_target() || !frame.written()) {
    return;
  }
  // A frame read back already ends with its image in PRESENT_SRC; the copy goes from and back to
  // that layout.
  const bool presentable = frame.presentable();
  const auto window_layout =
      presentable ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  auto& device = context();
  const auto& target = frame.target(frame.extent().width, frame.extent().height);
  auto& copy = presented();
  if (copy.image == VK_NULL_HANDLE || copy.extent.width != target.extent.width ||
      copy.extent.height != target.extent.height) {
    if (copy.image != VK_NULL_HANDLE) {
      const auto old_image = copy.image;
      const auto old_memory = copy.memory;
      frame.retire([old_image, old_memory] {
        auto& dev = context();
        dev.fn.vkDestroyImage(dev.device, old_image, nullptr);
        dev.fn.vkFreeMemory(dev.device, old_memory, nullptr);
      });
    }
    auto info = make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = frame.format();
    info.extent = {target.extent.width, target.extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(device.fn.vkCreateImage(device.device, &info, nullptr, &copy.image), "vkCreateImage");
    VkMemoryRequirements requirements{};
    device.fn.vkGetImageMemoryRequirements(device.device, copy.image, &requirements);
    auto allocation = make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        device.memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(device.fn.vkAllocateMemory(device.device, &allocation, nullptr, &copy.memory),
          "vkAllocateMemory");
    check(device.fn.vkBindImageMemory(device.device, copy.image, copy.memory, 0),
          "vkBindImageMemory");
    copy.extent = target.extent;
    copy.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }
  // The window image goes to TRANSFER_SRC for the copy, then back to COLOR_ATTACHMENT for the
  // presentation barrier.
  auto to_source = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
  to_source.srcAccessMask = presentable ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  to_source.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  to_source.oldLayout = window_layout;
  to_source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  to_source.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_source.image = target.image;
  to_source.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  auto to_copy = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
  to_copy.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  to_copy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_copy.oldLayout = copy.layout;
  to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_copy.image = copy.image;
  to_copy.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  const std::array<VkImageMemoryBarrier, 2> before{to_source, to_copy};
  device.fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                 static_cast<std::uint32_t>(before.size()), before.data());
  VkImageCopy region{};
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.extent = {target.extent.width, target.extent.height, 1};
  device.fn.vkCmdCopyImage(command, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, copy.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  auto back = to_source;
  back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  back.dstAccessMask = presentable ? 0 : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  back.newLayout = window_layout;
  device.fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &back);
  copy.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  copy.valid = true;
}

Image VkRenderer::presented_image(int width, int height) const {
  Image image{width, height,
              std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4, 0)};
  for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
    image.pixels[i] = 255;
  }
  auto& copy = presented();
  auto& frame = frames();
  if (!copy.valid || copy.extent.width != static_cast<std::uint32_t>(width) ||
      copy.extent.height != static_cast<std::uint32_t>(height)) {
    return image;  // black: no frame was copied at this size since presented capture was enabled
  }
  // A frame of its own: copy the retained image to the host, then finish it (nothing to present).
  const auto bytes = static_cast<VkDeviceSize>(width) * height * 4;
  auto host = gfx::vk::create_host_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  try {
    const auto command = frame.record();
    auto& device = context();
    auto to_source = make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    to_source.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_source.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_source.oldLayout = copy.layout;
    to_source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_source.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_source.image = copy.image;
    to_source.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    device.fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &to_source);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    device.fn.vkCmdCopyImageToBuffer(command, copy.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     host.buffer, 1, &region);
    copy.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    frame.submit_and_wait();
    std::memcpy(image.pixels.data(), host.mapped, image.pixels.size());
    frame.finish();
  } catch (...) {
    gfx::vk::destroy_buffer(host);
    throw;
  }
  gfx::vk::destroy_buffer(host);
  if (frame.format() == VK_FORMAT_B8G8R8A8_UNORM) {
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
      std::swap(image.pixels[i], image.pixels[i + 2]);
    }
  }
  return image;
}
}  // namespace realm
