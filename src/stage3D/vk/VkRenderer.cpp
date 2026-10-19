#include "VkRenderer.hpp"
#include "VkFrame.hpp"
#include "VkPipelines.hpp"
#include "VkResources.hpp"
#include "realm/stage3D/Atlas.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/util/Telemetry.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace realm {
namespace {
using gfx::vk::check;
using gfx::vk::context;
using gfx::vk::frames;
using gfx::vk::pipelines;
using gfx::vk::QuadPush;
using gfx::vk::resources;

[[noreturn]] void not_yet(const char* what) {
  throw std::runtime_error(std::string("Vulkan ") + what + " not implemented yet");
}

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
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
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
}  // namespace

VkRenderer::VkRenderer(std::size_t capacity, bool /*persistent_streaming*/,
                       std::size_t /*persistent_minimum_bytes*/) {
  if (!capacity || capacity > 1000000) {
    throw std::invalid_argument("Invalid quad capacity");
  }
  const auto format = frames().format();
  clear_pass_ = make_pass(format, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED);
  load_pass_ = make_pass(format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  strip_ = pipelines().quad_pipeline(clear_pass_, false);
}

VkRenderer::~VkRenderer() {
  auto& device = context();
  if (!device.ready()) {
    return;
  }
  // Frames in flight may still use the pipelines and framebuffers.
  device.fn.vkDeviceWaitIdle(device.device);
  for (auto& [view, framebuffer] : framebuffers_) {
    device.fn.vkDestroyFramebuffer(device.device, framebuffer, nullptr);
  }
  device.fn.vkDestroyPipeline(device.device, strip_, nullptr);
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
      // A new buffer per change: frames in flight keep reading the old one until they finish.
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
  if (walls) {
    not_yet("wall instances (phase S5)");
  }
  if (!meshes_.ranges.empty()) {
    not_yet("prepared meshes (phase S5)");
  }
  return span;
}

void VkRenderer::begin_pass(VkRenderPass pass, bool clear, std::uint32_t width, std::uint32_t height,
                            VkRect2D scissor) {
  auto& frame = frames();
  const auto& target = frame.target(width, height);
  const auto command = frame.record();
  // Transfers recorded so far are closed before the pass that may read them.
  resources().close_transfers(command);
  VkClearValue clear_value{};
  for (std::size_t i = 0; i < 4; ++i) {
    clear_value.color.float32[i] = clear_color_[i];
  }
  auto begin = make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
  begin.renderPass = pass;
  begin.framebuffer = framebuffer(target.view, target.extent);
  begin.renderArea = rect(static_cast<int>(width), static_cast<int>(height));
  begin.clearValueCount = clear ? 1 : 0;
  begin.pClearValues = clear ? &clear_value : nullptr;
  auto& device = context();
  device.fn.vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
  device.fn.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, strip_);
  device.fn.vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines().layout(), 0, 1,
                                    &atlas_set_, 0, nullptr);
  // The window viewport flipped (negative height): framebuffer y runs down the screen, as GL's y-up
  // NDC mapping required.
  VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width),
                      -static_cast<float>(height), 0.0f, 1.0f};
  device.fn.vkCmdSetViewport(command, 0, 1, &viewport);
  device.fn.vkCmdSetScissor(command, 0, 1, &scissor);
}

void VkRenderer::end_pass() {
  auto& frame = frames();
  context().fn.vkCmdEndRenderPass(frame.record());
  frame.mark_written();
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
      bind_surface(command, 0);
      push(command, push_state);
      const auto vertex_buffer = resources().buffer(terrain->buffer());
      VkDeviceSize offset = 0;
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
        draw_list(command, layer->quads, layer->meta, resources().buffer(layer->buffer), 0, clipped);
      }
      ++first;
      continue;
    }
    if (meta[first].kind == 9) {
      not_yet(meta[first].owner ? "prepared meshes (phase S5)" : "model instances (phase S5)");
    }
    // A run of one texture: instances [first, end) from base, one strip each.
    const auto run = draw_plan::sprite_run(meta, first);
    if (run.walls) {
      not_yet("wall instances (phase S5)");
    }
    const auto count = static_cast<std::uint32_t>(run.end - first);
    bind_surface(command, meta[first].texture);
    QuadPush push_state = state;
    push_state.standalone = meta[first].texture != 0 ? 1 : 0;
    push(command, push_state);
    VkDeviceSize offset = base;
    device.fn.vkCmdBindVertexBuffers(command, 0, 1, &buffer, &offset);
    device.fn.vkCmdDraw(command, 4, count, 0, static_cast<std::uint32_t>(first));
    ++draw_calls_;
    telemetry::add(telemetry::Counter::quads, count);
    telemetry::add(telemetry::Counter::triangles, 2 * static_cast<std::size_t>(count));
    first = run.end;
  }
}

void VkRenderer::draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height,
                      int clip_width, std::span<const Quad> overlay, const ScreenEffects& effects) {
  validate_dimensions(width, height, clip_width, quads.size() + overlay.size());
  if (effects.filtered() || effects.vignette > 0 || effects.sidebar > 0) {
    not_yet("screen effects (phase S6)");
  }
  draw_calls_ = 0;
  prepare_layers(quads);
  prepare_layers(overlay);
  const auto span = pack(quads, overlay);
  atlas_set_ = pipelines().atlas_set(atlas.texture());
  const auto w = static_cast<std::uint32_t>(width);
  const auto h = static_cast<std::uint32_t>(height);
  // The world is clipped to clip_width; the overlay is drawn over the whole window, as GL does.
  begin_pass(clear_pass_, true, w, h, rect(clip_width, height));
  const auto command = frames().record();
  QuadPush state;
  state.viewport[0] = static_cast<float>(width);
  state.viewport[1] = static_cast<float>(height);
  const std::span<const QuadMeta> meta(meta_);
  draw_list(command, quads, meta.first(quads.size()), span.buffer, span.offset, state);
  if (!overlay.empty()) {
    const auto full = rect(width, height);
    context().fn.vkCmdSetScissor(command, 0, 1, &full);
    draw_list(command, overlay, meta.subspan(quads.size()), span.buffer,
              span.offset + quads.size() * quad_instance_bytes, state);
  }
  end_pass();
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
  atlas_set_ = pipelines().atlas_set(atlas.texture());
  begin_pass(load_pass_, false, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
             rect(width, height));
  QuadPush state;
  state.viewport[0] = static_cast<float>(width);
  state.viewport[1] = static_cast<float>(height);
  draw_list(frames().record(), overlay, std::span<const QuadMeta>(meta_), span.buffer, span.offset, state);
  end_pass();
  telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
}

void VkRenderer::timing(telemetry::Recorder* /*recorder*/, std::uint64_t /*frame_id*/,
                        bool /*normal*/) {}

Image VkRenderer::capture(int width, int height, bool presented) const {
  if (presented) {
    not_yet("presented capture (phase S6)");
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

void VkRenderer::finish_frame() {
  frames().finish();
}
}  // namespace realm
