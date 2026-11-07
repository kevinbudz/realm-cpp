#include "WgpuUi.hpp"
#include "WgpuPipelines.hpp"
#include "WgpuRenderer.hpp"
#include "WgpuShaders.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace realm::gfx::wgpu {
namespace {
constexpr WGPUTextureFormat stencil_format = WGPUTextureFormat_Depth24PlusStencil8;
static_assert(sizeof(ui::Vertex) == 20);

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
    throw std::runtime_error("WebGPU UI shader module failed");
  }
  return module;
}

WGPUVertexAttribute attribute(WGPUVertexFormat format, std::uint64_t offset, std::uint32_t location) {
  WGPUVertexAttribute result{};
  result.format = format;
  result.offset = offset;
  result.shaderLocation = location;
  return result;
}

// What a pipeline does with the frame: its topology, blend, colour writes and stencil test.
struct Shape {
  WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
  bool blend = true;  // premultiplied: ONE, ONE_MINUS_SRC_ALPHA
  bool colour = true;
  WGPUCompareFunction stencil_compare = WGPUCompareFunction_Always;
  WGPUStencilOperation stencil_pass = WGPUStencilOperation_Keep;
  bool stencil_write = false;
};

WGPURenderPipeline make_pipeline(WGPUDevice device, WGPUTextureFormat format, WGPUShaderModule module,
                                 const char* vertex_entry, const char* fragment_entry,
                                 const WGPUVertexBufferLayout* buffers, std::size_t buffer_count,
                                 const Shape& shape, WGPUPipelineLayout layout) {
  WGPUVertexState vertex{};
  vertex.module = module;
  vertex.entryPoint = view_of(vertex_entry);
  vertex.bufferCount = buffer_count;
  vertex.buffers = buffers;

  // Rml's premultiplied colour: ONE, ONE_MINUS_SRC_ALPHA for colour and alpha (the Vulkan ui pipeline's blend).
  WGPUBlendState blend{};
  blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
  blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
  WGPUColorTargetState target{};
  target.format = format;
  target.blend = shape.blend ? &blend : nullptr;
  target.writeMask = shape.colour ? WGPUColorWriteMask_All : WGPUColorWriteMask_None;

  WGPUFragmentState fragment{};
  fragment.module = module;
  fragment.entryPoint = view_of(fragment_entry);
  fragment.targetCount = 1;
  fragment.targets = &target;

  // The stencil is part of every UI pass (the window and the layers), so every UI pipeline has it. Depth is unused.
  WGPUStencilFaceState face{};
  face.compare = shape.stencil_compare;
  face.failOp = WGPUStencilOperation_Keep;
  face.depthFailOp = WGPUStencilOperation_Keep;
  face.passOp = shape.stencil_pass;
  WGPUDepthStencilState depth{};
  depth.format = stencil_format;
  depth.depthWriteEnabled = WGPUOptionalBool_False;
  depth.depthCompare = WGPUCompareFunction_Always;
  depth.stencilFront = face;
  depth.stencilBack = face;
  depth.stencilReadMask = 0xFF;
  depth.stencilWriteMask = shape.stencil_write ? 0xFF : 0;

  WGPURenderPipelineDescriptor descriptor{};
  descriptor.layout = layout;
  descriptor.vertex = vertex;
  descriptor.primitive.topology = shape.topology;
  descriptor.primitive.frontFace = WGPUFrontFace_CCW;
  descriptor.primitive.cullMode = WGPUCullMode_None;
  descriptor.depthStencil = &depth;
  descriptor.multisample.count = 1;
  descriptor.multisample.mask = 0xFFFFFFFFU;
  descriptor.fragment = &fragment;
  WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
  if (!pipeline) {
    throw std::runtime_error("WebGPU UI render pipeline failed");
  }
  return pipeline;
}

WGPUBindGroup make_scene_group(WGPUTextureView source) {
  std::array<WGPUBindGroupEntry, 2> entries{};
  entries[0].binding = 0;
  entries[0].textureView = source;
  entries[1].binding = 1;
  entries[1].sampler = resources().sampler(true);  // blur taps are linear, as the Vulkan post sampler
  WGPUBindGroupDescriptor descriptor{};
  descriptor.layout = pipelines().surface_layout();
  descriptor.entryCount = entries.size();
  descriptor.entries = entries.data();
  WGPUBindGroup group = wgpuDeviceCreateBindGroup(context().device(), &descriptor);
  if (!group) {
    throw std::runtime_error("WebGPU UI bind group failed");
  }
  return group;
}
}  // namespace

WgpuUi::~WgpuUi() {
  destroy();
}

void WgpuUi::create(WGPUTextureFormat format) {
  auto* device = context().device();
  format_ = format;
  module_ = make_module(device, shaders::ui);

  // Group 0 is the surface layout the pipelines share (texture and sampler); group 1 is the UiBlock uniform.
  WGPUBindGroupLayoutEntry uniform_entry{};
  uniform_entry.binding = 0;
  uniform_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
  uniform_entry.buffer.type = WGPUBufferBindingType_Uniform;
  uniform_entry.buffer.hasDynamicOffset = 1;
  uniform_entry.buffer.minBindingSize = ui_block_bytes;
  WGPUBindGroupLayoutDescriptor uniform_descriptor{};
  uniform_descriptor.entryCount = 1;
  uniform_descriptor.entries = &uniform_entry;
  uniform_layout_ = wgpuDeviceCreateBindGroupLayout(device, &uniform_descriptor);
  if (!uniform_layout_) {
    throw std::runtime_error("WebGPU UI bind group layout failed");
  }
  stream_.create(uniform_layout_, ui_block_bytes);

  std::array<WGPUBindGroupLayout, 2> layouts{pipelines().surface_layout(), uniform_layout_};
  WGPUPipelineLayoutDescriptor layout_descriptor{};
  layout_descriptor.bindGroupLayoutCount = layouts.size();
  layout_descriptor.bindGroupLayouts = layouts.data();
  layout_ = wgpuDeviceCreatePipelineLayout(device, &layout_descriptor);

  // Rml::Vertex: position (0), premultiplied RGBA8 (1, unorm8x4, read as vec4), texture coordinate (2).
  std::array<WGPUVertexAttribute, 3> attributes{
      attribute(WGPUVertexFormat_Float32x2, 0, 0),
      attribute(WGPUVertexFormat_Unorm8x4, 8, 1),
      attribute(WGPUVertexFormat_Float32x2, 12, 2),
  };
  WGPUVertexBufferLayout vertex_buffer{};
  vertex_buffer.stepMode = WGPUVertexStepMode_Vertex;
  vertex_buffer.arrayStride = sizeof(ui::Vertex);
  vertex_buffer.attributeCount = attributes.size();
  vertex_buffer.attributes = attributes.data();

  Shape plain;
  geometry_[0] = make_pipeline(device, format, module_, "geometry_vs", "geometry_fs", &vertex_buffer, 1, plain, layout_);
  Shape test;
  test.stencil_compare = WGPUCompareFunction_Equal;
  geometry_[1] = make_pipeline(device, format, module_, "geometry_vs", "geometry_fs", &vertex_buffer, 1, test, layout_);
  // A mask draw writes the stencil only: no colour.
  Shape replace;
  replace.colour = false;
  replace.blend = false;
  replace.stencil_pass = WGPUStencilOperation_Replace;
  replace.stencil_write = true;
  geometry_[2] = make_pipeline(device, format, module_, "geometry_vs", "geometry_fs", &vertex_buffer, 1, replace, layout_);
  Shape increment = replace;
  increment.stencil_pass = WGPUStencilOperation_IncrementClamp;
  geometry_[3] = make_pipeline(device, format, module_, "geometry_vs", "geometry_fs", &vertex_buffer, 1, increment, layout_);

  // The post passes are a strip over the target (no vertex buffer): plain copies, premultiplied composites.
  Shape post;
  post.topology = WGPUPrimitiveTopology_TriangleStrip;
  post.blend = false;
  post_[0] = make_pipeline(device, format, module_, "post_vs", "post_fs", nullptr, 0, post, layout_);
  post.blend = true;
  post_[1] = make_pipeline(device, format, module_, "post_vs", "post_fs", nullptr, 0, post, layout_);
}

void WgpuUi::destroy() noexcept {
  close();
  for (auto& pipeline : geometry_) {
    if (pipeline) {
      wgpuRenderPipelineRelease(pipeline);
      pipeline = nullptr;
    }
  }
  for (auto& pipeline : post_) {
    if (pipeline) {
      wgpuRenderPipelineRelease(pipeline);
      pipeline = nullptr;
    }
  }
  if (layout_) {
    wgpuPipelineLayoutRelease(layout_);
    layout_ = nullptr;
  }
  if (uniform_layout_) {
    wgpuBindGroupLayoutRelease(uniform_layout_);
    uniform_layout_ = nullptr;
  }
  if (module_) {
    wgpuShaderModuleRelease(module_);
    module_ = nullptr;
  }
  release_targets();
  for (auto& staging : free_staging_) {
    wgpuBufferRelease(staging.buffer);
  }
  for (auto& staging : busy_staging_) {  // the frame's encoder keeps the copies' sources alive until its submit
    wgpuBufferRelease(staging.buffer);
  }
  free_staging_.clear();
  busy_staging_.clear();
  upload_bytes_.clear();
  stream_.destroy();
}

void WgpuUi::release_targets() noexcept {
  for (auto& scene : scene_groups_) {
    wgpuBindGroupRelease(scene.group);
  }
  scene_groups_.clear();
  for (auto& target : filter_targets_) {
    if (target.view) {
      wgpuTextureViewRelease(target.view);
      target.view = nullptr;
    }
    if (target.texture) {
      wgpuTextureRelease(target.texture);
      target.texture = nullptr;
    }
  }
  for (auto& layer : layers_) {
    if (layer.view) {
      wgpuTextureViewRelease(layer.view);
    }
    if (layer.texture) {
      wgpuTextureRelease(layer.texture);
    }
  }
  layers_.clear();
  if (stencil_view_) {
    wgpuTextureViewRelease(stencil_view_);
    stencil_view_ = nullptr;
  }
  if (stencil_texture_) {
    wgpuTextureRelease(stencil_texture_);
    stencil_texture_ = nullptr;
  }
}

WgpuUi::Target WgpuUi::make_target(std::uint32_t width, std::uint32_t height) const {
  Target target;
  WGPUTextureDescriptor descriptor{};
  descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
  descriptor.dimension = WGPUTextureDimension_2D;
  descriptor.size = {width, height, 1};
  descriptor.format = format_;
  descriptor.mipLevelCount = 1;
  descriptor.sampleCount = 1;
  target.texture = wgpuDeviceCreateTexture(context().device(), &descriptor);
  if (!target.texture) {
    throw std::runtime_error("WebGPU UI target failed");
  }
  WGPUTextureViewDescriptor view{};
  view.format = format_;
  view.dimension = WGPUTextureViewDimension_2D;
  view.mipLevelCount = 1;
  view.arrayLayerCount = 1;
  view.aspect = WGPUTextureAspect_All;
  target.view = wgpuTextureCreateView(target.texture, &view);
  return target;
}

void WgpuUi::begin_frame(int width, int height) {
  close();
  depth_ = 0;
  stencil_cleared_ = false;
  if (width <= 0 || height <= 0) {
    width_ = 0;
    height_ = 0;
    return;
  }
  const auto w = static_cast<std::uint32_t>(width);
  const auto h = static_cast<std::uint32_t>(height);
  if (!stencil_view_ || w != width_ || h != height_) {
    // A new size: the layers go (they are empty between frames) and the targets follow the window.
    release_targets();
    WGPUTextureDescriptor descriptor{};
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = {w, h, 1};
    descriptor.format = stencil_format;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    stencil_texture_ = wgpuDeviceCreateTexture(context().device(), &descriptor);
    if (!stencil_texture_) {
      throw std::runtime_error("WebGPU UI stencil failed");
    }
    WGPUTextureViewDescriptor view{};
    view.format = stencil_format;
    view.dimension = WGPUTextureViewDimension_2D;
    view.mipLevelCount = 1;
    view.arrayLayerCount = 1;
    view.aspect = WGPUTextureAspect_All;
    stencil_view_ = wgpuTextureCreateView(stencil_texture_, &view);
    for (auto& target : filter_targets_) {
      target = make_target(w, h);
    }
  }
  width_ = w;
  height_ = h;
}

WGPUTextureView WgpuUi::current_view() {
  if (depth_ > 0) {
    return layers_[depth_ - 1].view;
  }
  return context().target(width_, height_);
}

WGPURenderPassEncoder WgpuUi::begin_pass(WGPUTextureView view, bool clear) {
  WGPURenderPassColorAttachment color{};
  color.view = view;
  color.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
  color.storeOp = WGPUStoreOp_Store;
  color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
  color.clearValue = WGPUColor{0, 0, 0, 0};  // RmlUi's layers start transparent
  // The stencil is cleared by the frame's first pass and kept by the rest, as RmlUi's frame does. The depth
  // is not used: it is cleared and discarded.
  WGPURenderPassDepthStencilAttachment depth{};
  depth.view = stencil_view_;
  depth.depthLoadOp = WGPULoadOp_Clear;
  depth.depthClearValue = 1.0f;
  depth.depthStoreOp = WGPUStoreOp_Discard;
  depth.stencilLoadOp = stencil_cleared_ ? WGPULoadOp_Load : WGPULoadOp_Clear;
  depth.stencilStoreOp = WGPUStoreOp_Store;
  depth.stencilClearValue = 0;
  stencil_cleared_ = true;
  WGPURenderPassDescriptor descriptor{};
  descriptor.colorAttachmentCount = 1;
  descriptor.colorAttachments = &color;
  descriptor.depthStencilAttachment = &depth;
  bound_ = Bound{};
  return wgpuCommandEncoderBeginRenderPass(context().encoder(), &descriptor);
}

void WgpuUi::ensure_pass(WGPUTextureView view, bool clear) {
  if (pass_ && pass_view_ == view) {
    return;
  }
  close();
  pass_ = begin_pass(view, clear);
  pass_view_ = view;
}

void WgpuUi::close() noexcept {
  if (pass_) {
    wgpuRenderPassEncoderEnd(pass_);
    wgpuRenderPassEncoderRelease(pass_);
    pass_ = nullptr;
    pass_view_ = nullptr;
  }
}

void WgpuUi::bind_block(WGPURenderPassEncoder pass, WGPUBindGroup surface, const UiBlock& block) {
  const auto uniform = stream_.write(&block, ui_block_bytes, uniform_alignment);
  const auto offset = static_cast<std::uint32_t>(uniform.offset);
  if (bound_.surface != surface) {
    wgpuRenderPassEncoderSetBindGroup(pass, 0, surface, 0, nullptr);
    bound_.surface = surface;
  }
  // Each block sits at its own dynamic offset, so the uniform group is normally set for every draw.
  if (bound_.uniform != uniform.uniform || bound_.uniform_offset != offset) {
    wgpuRenderPassEncoderSetBindGroup(pass, 1, uniform.uniform, 1, &offset);
    bound_.uniform = uniform.uniform;
    bound_.uniform_offset = offset;
  }
}

WGPUBindGroup WgpuUi::scene_group(WGPUTextureView source) {
  for (const auto& scene : scene_groups_) {
    if (scene.view == source) {
      return scene.group;
    }
  }
  const WGPUBindGroup group = make_scene_group(source);
  scene_groups_.push_back(SceneGroup{source, group});
  return group;
}

void WgpuUi::draw(const ui::Draw& draw) {
  const auto& geometry = draw.geometry;
  if (!ready() || !geometry.vertices || !geometry.indices || geometry.vertex_count == 0 ||
      geometry.index_count == 0) {
    return;
  }
  const WGPUTextureView view = current_view();
  if (!view) {
    return;  // no swapchain texture this frame
  }
  // The scissor: RmlUi's region clipped to the window (or the whole window). Set before any clear, so a mask's
  // clear is limited to it, as GL's glClear is.
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t width = width_;
  std::uint32_t height = height_;
  if (draw.scissor) {
    const int left = std::max(0, draw.region[0]);
    const int top = std::max(0, draw.region[1]);
    const int right = std::min(static_cast<int>(width_), draw.region[0] + draw.region[2]);
    const int bottom = std::min(static_cast<int>(height_), draw.region[1] + draw.region[3]);
    if (right <= left || bottom <= top) {
      return;  // an empty region draws nothing (and clears nothing)
    }
    x = static_cast<std::uint32_t>(left);
    y = static_cast<std::uint32_t>(top);
    width = static_cast<std::uint32_t>(right - left);
    height = static_cast<std::uint32_t>(bottom - top);
  }
  ensure_pass(view, false);
  const WGPURenderPassEncoder pass = pass_;
  // The open pass's state: a call is made only when its value differs from what the pass has bound.
  const std::array<std::uint32_t, 4> scissor{x, y, width, height};
  if (!bound_.scissor_set || bound_.scissor != scissor) {
    wgpuRenderPassEncoderSetScissorRect(pass, x, y, width, height);
    bound_.scissor = scissor;
    bound_.scissor_set = true;
  }
  const auto set_pipeline = [&](WGPURenderPipeline next) {
    if (bound_.pipeline != next) {
      wgpuRenderPassEncoderSetPipeline(pass, next);
      bound_.pipeline = next;
    }
  };
  const auto set_stencil = [&](std::uint32_t reference) {
    if (!bound_.stencil_set || bound_.stencil_reference != reference) {
      wgpuRenderPassEncoderSetStencilReference(pass, reference);
      bound_.stencil_reference = reference;
      bound_.stencil_set = true;
    }
  };

  const auto vertex_bytes = geometry.vertex_count * sizeof(ui::Vertex);
  const auto index_bytes = geometry.index_count * sizeof(std::uint32_t);
  const auto vertices = stream_.write(geometry.vertices, vertex_bytes, 4);
  const auto indices = stream_.write(geometry.indices, index_bytes, 4);

  UiBlock block;
  std::memcpy(block.transform, draw.transform, sizeof block.transform);
  block.translate[0] = draw.translate[0];
  block.translate[1] = draw.translate[1];
  block.textured = draw.textured ? 1 : 0;
  const WGPUBindGroup surface = pipelines().surface_group(draw.textured ? draw.texture : 0);

  std::uint32_t reference = draw.stencil_reference;
  WGPURenderPipeline pipeline = geometry_[0];
  switch (draw.clip) {
    case ui::Clip::none:
      break;
    case ui::Clip::test:
      pipeline = geometry_[1];
      break;
    case ui::Clip::mask_set:
    case ui::Clip::mask_set_inverse: {
      // Set clears the stencil to 0 and writes 1 inside the geometry; SetInverse clears to 1 and writes 0. A
      // WebGPU pass cannot clear mid-pass, so the clear is a full-target quad in clip space (the replace pipeline,
      // reference = the clear value) limited by the scissor, as a clear is.
      const bool inverse = draw.clip == ui::Clip::mask_set_inverse;
      std::array<ui::Vertex, 6> quad{};
      const float corners[6][2] = {{-1, 1}, {1, 1}, {-1, -1}, {-1, -1}, {1, 1}, {1, -1}};
      for (std::size_t i = 0; i < quad.size(); ++i) {
        quad[i].x = corners[i][0];
        quad[i].y = corners[i][1];
      }
      const auto clear_vertices = stream_.write(quad.data(), sizeof quad, 4);
      UiBlock clear;
      clear.transform[0] = clear.transform[5] = clear.transform[10] = clear.transform[15] = 1;
      set_pipeline(geometry_[2]);
      bind_block(pass, pipelines().surface_group(0), clear);
      wgpuRenderPassEncoderSetVertexBuffer(pass, 0, clear_vertices.buffer, clear_vertices.offset, WGPU_WHOLE_SIZE);
      set_stencil(inverse ? 1 : 0);
      wgpuRenderPassEncoderDraw(pass, static_cast<std::uint32_t>(quad.size()), 1, 0, 0);
      reference = inverse ? 0 : 1;
      pipeline = geometry_[2];
      break;
    }
    case ui::Clip::mask_intersect:
      pipeline = geometry_[3];
      break;
  }

  set_pipeline(pipeline);
  bind_block(pass, surface, block);
  wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertices.buffer, vertices.offset, WGPU_WHOLE_SIZE);
  wgpuRenderPassEncoderSetIndexBuffer(pass, indices.buffer, WGPUIndexFormat_Uint32, indices.offset, WGPU_WHOLE_SIZE);
  set_stencil(reference);
  wgpuRenderPassEncoderDrawIndexed(pass, static_cast<std::uint32_t>(geometry.index_count), 1, 0, 0, 0);
}

void WgpuUi::resample(WGPUTextureView target, bool clear, WGPUTextureView source, const UiBlock& values,
                      bool premultiplied) {
  close();
  const WGPUBindGroup scene = scene_group(source);
  const auto uniform = stream_.write(&values, ui_block_bytes, uniform_alignment);
  const auto offset = static_cast<std::uint32_t>(uniform.offset);
  auto pass = begin_pass(target, clear);
  wgpuRenderPassEncoderSetPipeline(pass, post_[premultiplied ? 1 : 0]);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, scene, 0, nullptr);
  wgpuRenderPassEncoderSetBindGroup(pass, 1, uniform.uniform, 1, &offset);
  wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
}

std::uint32_t WgpuUi::push_layer() {
  if (!ready()) {
    return 0;
  }
  close();
  const auto index = depth_;
  if (layers_.size() <= index) {
    layers_.push_back(make_target(width_, height_));
  }
  ++depth_;
  // The layer's pass clears it (RmlUi's PushLayer clears to transparent); the draws that follow keep it open.
  ensure_pass(layers_[index].view, true);
  return static_cast<std::uint32_t>(depth_);
}

void WgpuUi::pop_layer() {
  close();
  if (depth_ > 0) {
    --depth_;
  }
}

std::size_t WgpuUi::blur(std::size_t first, WGPUTextureView input, std::array<int, 4> region, float sigma) {
  // RenderInterface_GL3's split (SigmaToParameters): a pass level of halvings, and the sigma left for one pass of
  // 7 taps. The downscaled result is then upscaled to the requested region.
  int level = 0;
  const int scaled = static_cast<int>(sigma * (2.0f / 3.0f));
  if (scaled > 0) {
    level = std::clamp(static_cast<int>(std::log2(static_cast<float>(scaled))), 0, 10);
  }
  const float pass_sigma = std::clamp(sigma / static_cast<float>(1 << level), 0.0f, 3.0f);
  // SetBlurWeights: the Gaussian for distances 0..3, normalised over both sides.
  std::array<float, 4> weights{};
  float normalisation = 0;
  for (std::size_t i = 0; i < weights.size(); ++i) {
    weights[i] = std::fabs(pass_sigma) < 0.1f
                     ? (i == 0 ? 1.0f : 0.0f)
                     : std::exp(-static_cast<float>(i * i) / (2.0f * pass_sigma * pass_sigma)) /
                           (std::sqrt(2.0f * 3.14159265f) * pass_sigma);
    normalisation += (i == 0 ? 1.0f : 2.0f) * weights[i];
  }
  for (auto& weight : weights) {
    weight /= normalisation;
  }
  const auto width = static_cast<float>(width_);
  const auto height = static_cast<float>(height_);
  const auto requested = region;
  auto uv = [&](const std::array<int, 4>& r) {
    return std::array<float, 4>{r[0] / width, r[1] / height, r[2] / width, r[3] / height};
  };
  auto block_for = [&](const std::array<int, 4>& dst, const std::array<int, 4>& src_region, bool blur_pass,
                       std::array<float, 2> offset) {
    UiBlock block;
    const auto src_uv = uv(src_region);
    for (std::size_t i = 0; i < 4; ++i) {
      const auto edge = static_cast<float>(dst[i]);
      block.dst_rect[i] = edge;
      block.src_rect[i] = src_uv[i];
      block.clamp_rect[i] = blur_pass ? (i < 2 ? (edge + 0.5f) / (i == 0 ? width : height)
                                               : (edge - 0.5f) / (i == 2 ? width : height))
                                      : 0.0f;
      block.weights[i] = weights[i];
    }
    block.step_uv[0] = offset[0];
    block.step_uv[1] = offset[1];
    block.window_size[0] = width;
    block.window_size[1] = height;
    block.blur = blur_pass ? 1 : 0;
    return block;
  };
  std::size_t source = first;
  std::size_t target = 1 - first;
  // Each pass reads the previous one's target; the first reads input (the layer, or the copy it replaces).
  WGPUTextureView from = input;
  const auto step = [&](const UiBlock& block) {
    resample(filter_targets_[target].view, true, from, block, false);
    from = filter_targets_[target].view;
    std::swap(source, target);
  };
  // Downscales: each halves the region (rounding the start up, as GL's scissor does); the content stays at the
  // region's pixels of the target.
  for (int i = 0; i < level; ++i) {
    std::array<int, 4> next{(region[0] + 1) / 2, (region[1] + 1) / 2, 0, 0};
    next[2] = std::max(region[2] / 2, next[0]);
    next[3] = std::max(region[3] / 2, next[1]);
    step(block_for(next, region, false, {0, 0}));
    region = next;
  }
  // The vertical and then the horizontal pass over the region, clamped to it.
  step(block_for(region, region, true, {0.0f, 1.0f / height}));
  step(block_for(region, region, true, {1.0f / width, 0.0f}));
  // The linear upscale to the requested region.
  step(block_for(requested, region, false, {0, 0}));
  return source;
}

void WgpuUi::composite(const ui::Composite& composite) {
  close();
  if (!ready() || composite.source == 0 || composite.source > layers_.size() ||
      composite.destination > layers_.size()) {
    return;
  }
  const auto w = static_cast<int>(width_);
  const auto h = static_cast<int>(height_);
  // The region: the scissor clipped to the window, or the window.
  std::array<int, 4> region{0, 0, w, h};
  if (composite.scissor) {
    region = {std::max(0, composite.region[0]), std::max(0, composite.region[1]),
              std::min(w, composite.region[0] + composite.region[2]),
              std::min(h, composite.region[1] + composite.region[3])};
  }
  if (region[2] <= region[0] || region[3] <= region[1]) {
    return;
  }
  const WGPUTextureView destination =
      composite.destination ? layers_[composite.destination - 1].view : context().target(width_, height_);
  if (!destination) {
    return;
  }
  WGPUTextureView current = layers_[composite.source - 1].view;
  // Group opacity: the opacity filters multiply, and the final draw applies the product once. The blur
  // passes run at 1; blur is linear, so applying the product last matches applying it in filter order.
  float opacity = 1;
  if (composite.filter_count > 0) {
    // The filters' working image is the layer itself: the first blur reads it directly, where GL copies it into
    // PostprocessPrimary first. A copy at full size and full uv is the same pixels, so the output is unchanged.
    std::size_t index = 0;
    for (std::size_t i = 0; i < composite.filter_count; ++i) {
      const auto& filter = composite.filters[i];
      opacity *= filter.opacity;
      if (filter.sigma > 0) {
        index = blur(index, current, region, filter.sigma);
        current = filter_targets_[index].view;
      }
    }
  }
  UiBlock final_draw;
  for (std::size_t i = 0; i < 4; ++i) {
    final_draw.dst_rect[i] = static_cast<float>(region[i]);
  }
  final_draw.src_rect[0] = static_cast<float>(region[0]) / static_cast<float>(w);
  final_draw.src_rect[1] = static_cast<float>(region[1]) / static_cast<float>(h);
  final_draw.src_rect[2] = static_cast<float>(region[2]) / static_cast<float>(w);
  final_draw.src_rect[3] = static_cast<float>(region[3]) / static_cast<float>(h);
  final_draw.window_size[0] = static_cast<float>(w);
  final_draw.window_size[1] = static_cast<float>(h);
  final_draw.opacity = opacity;
  resample(destination, false, current, final_draw, !composite.replace);
}

WgpuUi::StagingBuffer WgpuUi::acquire_staging(std::uint64_t bytes) {
  // The smallest free buffer that holds the bytes. Free buffers came from earlier submitted frames, so the queue
  // writes below run after the submission that read their last copy.
  auto best = free_staging_.end();
  for (auto it = free_staging_.begin(); it != free_staging_.end(); ++it) {
    if (it->size >= bytes && (best == free_staging_.end() || it->size < best->size)) {
      best = it;
    }
  }
  if (best != free_staging_.end()) {
    const StagingBuffer staging = *best;
    free_staging_.erase(best);
    return staging;
  }
  // A new buffer rounds up to a power of two (at least 4 KiB) so that near sizes can reuse it.
  std::uint64_t size = 4096;
  while (size < bytes) {
    size *= 2;
  }
  WGPUBufferDescriptor descriptor{};
  descriptor.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
  descriptor.size = size;
  WGPUBuffer buffer = wgpuDeviceCreateBuffer(context().device(), &descriptor);
  if (!buffer) {
    throw std::runtime_error("WebGPU UI staging buffer failed");
  }
  return StagingBuffer{buffer, size};
}

void WgpuUi::write_surface(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba) {
  const WGPUTexture target = resources().texture(texture);
  if (!target || !rgba || width <= 0 || height <= 0) {
    return;
  }
  // The copy's rows are padded to WebGPU's 256-byte alignment; the rows are packed into the upload bytes first.
  const auto row = static_cast<std::size_t>(width) * 4;
  const auto padded_row = (row + 255) / 256 * 256;
  const auto bytes = padded_row * static_cast<std::size_t>(height);
  upload_bytes_.resize(bytes);
  for (std::size_t line = 0; line < static_cast<std::size_t>(height); ++line) {
    std::memcpy(upload_bytes_.data() + line * padded_row, rgba + line * row, row);
  }
  const StagingBuffer staging = acquire_staging(bytes);
  // queue.writeBuffer copies its data at the call, so upload_bytes_ is free again at once.
  wgpuQueueWriteBuffer(context().queue(), staging.buffer, 0, upload_bytes_.data(), bytes);

  WGPUTexelCopyBufferInfo source{};
  source.buffer = staging.buffer;
  source.layout.bytesPerRow = static_cast<std::uint32_t>(padded_row);
  source.layout.rowsPerImage = static_cast<std::uint32_t>(height);
  WGPUTexelCopyTextureInfo destination{};
  destination.texture = target;
  destination.origin = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), 0};
  destination.aspect = WGPUTextureAspect_All;
  const WGPUExtent3D size{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
  wgpuCommandEncoderCopyBufferToTexture(context().encoder(), &source, &destination, &size);
  // The copy reads the buffer at this frame's submit, so it is reused only after frame_submitted.
  busy_staging_.push_back(staging);
}

void WgpuUi::frame_submitted() noexcept {
  stream_.reset();
  // The frame's encoder was submitted before this call (WgpuRenderer::finish_frame), so the copies that read the
  // busy buffers have been submitted. They become free for the next frame's writes.
  for (const auto& staging : busy_staging_) {
    free_staging_.push_back(staging);
  }
  busy_staging_.clear();
  // Keep a few buffers: beyond the count or total size cap, the excess is released.
  constexpr std::size_t max_free = 8;
  constexpr std::uint64_t max_free_bytes = 16ull * 1024 * 1024;
  std::uint64_t total = 0;
  for (const auto& staging : free_staging_) {
    total += staging.size;
  }
  while (!free_staging_.empty() && (free_staging_.size() > max_free || total > max_free_bytes)) {
    total -= free_staging_.front().size;
    wgpuBufferRelease(free_staging_.front().buffer);
    free_staging_.erase(free_staging_.begin());
  }
}
}  // namespace realm::gfx::wgpu

// RmlUi's WebGPU seam (include/realm/stage3D/wgpu/WebGpuUi.hpp): the work happens in the active renderer's UI state.
namespace realm::gfx::wgpu::ui {
void begin_frame(int width, int height) {
  if (auto* renderer = active_renderer()) {
    renderer->rml_ui().begin_frame(width, height);
  }
}

void draw(const Draw& draw) {
  if (auto* renderer = active_renderer()) {
    renderer->rml_ui().draw(draw);
  }
}

void end_frame() {
  if (auto* renderer = active_renderer()) {
    renderer->rml_ui().end_frame();
  }
}

std::uint32_t push_layer() {
  if (auto* renderer = active_renderer()) {
    return renderer->rml_ui().push_layer();
  }
  return 0;
}

void pop_layer() {
  if (auto* renderer = active_renderer()) {
    renderer->rml_ui().pop_layer();
  }
}

void composite(const Composite& composite) {
  if (auto* renderer = active_renderer()) {
    renderer->rml_ui().composite(composite);
  }
}

std::uint32_t create_texture(int width, int height, bool linear) {
  // The surface needs the device and its resources, not a frame: RmlUi loads documents before the first frame.
  if (width <= 0 || height <= 0) {
    return 0;
  }
  auto& store = resources();
  const auto texture = store.create_surface(linear);
  store.allocate_surface(texture, width, height);
  return texture;
}

void upload_texture(std::uint32_t texture, int x, int y, int width, int height, const std::byte* rgba) {
  if (!texture) {
    return;
  }
  // The copy is recorded between passes: the open UI pass closes first.
  if (auto* renderer = active_renderer()) {
    renderer->rml_ui().close();
    renderer->rml_ui().write_surface(texture, x, y, width, height, rgba);
  } else {
    WgpuUi().write_surface(texture, x, y, width, height, rgba);  // no renderer: a temporary, released after the copy
  }
}

void release_texture(std::uint32_t texture) noexcept {
  if (texture) {
    resources().release(texture);
  }
}

int max_texture_size() {
  return static_cast<int>(context().max_texture_size());
}
}  // namespace realm::gfx::wgpu::ui
