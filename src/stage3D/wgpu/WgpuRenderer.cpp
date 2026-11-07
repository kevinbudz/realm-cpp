#include "WgpuRenderer.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "realm/assets/Image.hpp"
#include "realm/stage3D/Atlas.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/util/Telemetry.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace realm::gfx::wgpu {
namespace {
WgpuRenderer* active = nullptr;  // see active_renderer()
// The wall texture is 1024 texels wide: texel i is column i % 1024 of row i / 1024.
constexpr std::uint64_t wall_row_texels = 1024;
constexpr std::uint64_t texel_bytes = 16;  // RGBA32F
constexpr std::uint64_t quad_bytes = quad_instance_bytes;

void validate_dimensions(int width, int height, int clip_width, std::size_t total) {
  if (width <= 0 || height <= 0 || clip_width < 0 || clip_width > width || total > 1000000) {
    throw std::invalid_argument("Invalid draw dimensions or quad count");
  }
}

// Two layer buffers hold the same instances when their bytes match (the owner and texture of a quad
// are not in the instances, so they do not count).
bool same_instances(const std::vector<std::byte>& uploaded, const std::vector<std::byte>& packed) {
  return uploaded.size() == packed.size() &&
         (packed.empty() || std::memcmp(uploaded.data(), packed.data(), packed.size()) == 0);
}

WGPUExtent3D extent_of(std::uint32_t width, std::uint32_t height) {
  return WGPUExtent3D{width, height, 1};
}
}  // namespace

WgpuRenderer* active_renderer() noexcept {
  return active;
}

WgpuRenderer::WgpuRenderer(std::size_t capacity, bool /*persistent_streaming*/,
                           std::size_t /*persistent_minimum_bytes*/) {
  if (!capacity || capacity > 1000000) {
    throw std::invalid_argument("Invalid quad capacity");
  }
  stream_.create(pipelines().uniform_layout(), uniform_bytes);
  rml_ui_.create(context().format());
  active = this;
  // The model triangles are uploaded once; every instance refers to a range of them.
  const auto geometry = model_geometry();
  model_geometry_ = resources().create_buffer(std::as_bytes(geometry));
}

WgpuRenderer::~WgpuRenderer() {
  rml_ui_.destroy();
  if (atlas_group_) {
    end_submission();
  }
  if (active == this) {
    active = nullptr;
  }
  resources().release(model_geometry_);
  if (walls_view_) {
    wgpuTextureViewRelease(walls_view_);
  }
  if (walls_texture_) {
    wgpuTextureRelease(walls_texture_);
  }
  for (auto& target : post_) {
    if (target.view) {
      wgpuTextureViewRelease(target.view);
    }
    if (target.texture) {
      wgpuTextureRelease(target.texture);
    }
  }
  stream_.destroy();
}

WGPURenderPassEncoder WgpuRenderer::begin_pass(WGPUTextureView target, bool clear) {
  WGPURenderPassColorAttachment color{};
  color.view = target;
  color.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
  color.storeOp = WGPUStoreOp_Store;
  color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
  color.clearValue = WGPUColor{clear_color_[0], clear_color_[1], clear_color_[2], clear_color_[3]};
  WGPURenderPassDescriptor descriptor{};
  descriptor.colorAttachmentCount = 1;
  descriptor.colorAttachments = &color;
  return wgpuCommandEncoderBeginRenderPass(context().encoder(), &descriptor);
}

void WgpuRenderer::end_pass(WGPURenderPassEncoder pass) {
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
}

void WgpuRenderer::scissor(WGPURenderPassEncoder pass, int x, int y, int width, int height) {
  wgpuRenderPassEncoderSetScissorRect(pass, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                      static_cast<std::uint32_t>(width),
                                      static_cast<std::uint32_t>(height));
}

void WgpuRenderer::bind_draw(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline, std::uint32_t surface,
                             const QuadUniform& state) {
  const auto uniform = stream_.write(&state, uniform_bytes, uniform_alignment);
  const auto offset = static_cast<std::uint32_t>(uniform.offset);
  wgpuRenderPassEncoderSetPipeline(pass, pipeline);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, atlas_group_, 0, nullptr);
  wgpuRenderPassEncoderSetBindGroup(pass, 1, pipelines().surface_group(surface), 0, nullptr);
  wgpuRenderPassEncoderSetBindGroup(pass, 2, uniform.uniform, 1, &offset);
  ++draw_calls_;
}

void WgpuRenderer::post_draw(WGPURenderPassEncoder pass, bool blend, WGPUBindGroup scene,
                             const PostUniform& values) {
  const auto uniform = stream_.write(&values, uniform_bytes, uniform_alignment);
  const auto offset = static_cast<std::uint32_t>(uniform.offset);
  wgpuRenderPassEncoderSetPipeline(pass, pipelines().post(blend));
  wgpuRenderPassEncoderSetBindGroup(pass, 0, scene, 0, nullptr);
  wgpuRenderPassEncoderSetBindGroup(pass, 1, uniform.uniform, 1, &offset);
  wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
  ++draw_calls_;
}

void WgpuRenderer::post_pass(WGPUTextureView target, bool clear, WGPUTextureView source, int width,
                             int height, const PostUniform& values, bool blend) {
  const WGPUBindGroup scene = pipelines().scene_group(source);
  auto pass = begin_pass(target, clear);
  scissor(pass, 0, 0, width, height);
  post_draw(pass, blend, scene, values);
  end_pass(pass);
  wgpuBindGroupRelease(scene);
}

void WgpuRenderer::prepare_layers(std::span<const Quad> list) {
  for (const auto& quad : list) {
    if (quad.kind != 14) {
      continue;
    }
    auto* layer = const_cast<QuadLayer*>(static_cast<const QuadLayer*>(quad.owner.get()));
    if (!layer || layer->quads.size() > 1000000) {
      throw std::logic_error("Invalid quad layer");
    }
    layer->packed.resize(layer->quads.size() * quad_bytes);
    layer->meta.resize(layer->quads.size());
    const WallGeometryStore* unused_walls{};
    draw_plan::MeshBatch unused_meshes;
    draw_plan::pack_quads(layer->quads, layer->packed.data(), layer->meta.data(), unused_walls, false,
                          unused_meshes);
    if (!same_instances(layer->uploaded, layer->packed)) {
      // A new buffer per change; the old one goes with its last reference.
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

void WgpuRenderer::write_wall_texels(std::uint64_t first, std::uint64_t count, const std::byte* data) {
  auto* queue = context().queue();
  while (count > 0) {
    const auto column = first % wall_row_texels;
    const auto row = first / wall_row_texels;
    const auto run = std::min(count, wall_row_texels - column);
    WGPUTexelCopyTextureInfo destination{};
    destination.texture = walls_texture_;
    destination.origin = {static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row), 0};
    destination.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = static_cast<std::uint32_t>(run * texel_bytes);
    layout.rowsPerImage = 1;
    const WGPUExtent3D size{static_cast<std::uint32_t>(run), 1, 1};
    wgpuQueueWriteTexture(queue, &destination, data, static_cast<std::size_t>(run * texel_bytes), &layout, &size);
    data += run * texel_bytes;
    first += run;
    count -= run;
  }
}

WGPUTextureView WgpuRenderer::upload_walls(const WallGeometryStore& store) {
  const auto step = draw_plan::plan_walls(wall_sync_, store, store.limit());
  if (step.none) {
    return walls_view_ ? walls_view_ : pipelines().dummy_walls();
  }
  if (step.reallocate) {
    // The old texture goes with its last reference; the new one holds the store's capacity in texels.
    if (walls_view_) {
      wgpuTextureViewRelease(walls_view_);
      walls_view_ = nullptr;
    }
    if (walls_texture_) {
      wgpuTextureRelease(walls_texture_);
      walls_texture_ = nullptr;
    }
    const std::uint64_t texels = static_cast<std::uint64_t>(step.capacity) * 2;
    walls_rows_ = std::max<std::uint64_t>(1, (texels + wall_row_texels - 1) / wall_row_texels);
    WGPUTextureDescriptor descriptor{};
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = extent_of(static_cast<std::uint32_t>(wall_row_texels), static_cast<std::uint32_t>(walls_rows_));
    descriptor.format = WGPUTextureFormat_RGBA32Float;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    walls_texture_ = wgpuDeviceCreateTexture(context().device(), &descriptor);
    if (!walls_texture_) {
      throw std::runtime_error("WebGPU wall texture failed");
    }
    WGPUTextureViewDescriptor view{};
    view.format = WGPUTextureFormat_RGBA32Float;
    view.dimension = WGPUTextureViewDimension_2D;
    view.mipLevelCount = 1;
    view.arrayLayerCount = 1;
    view.aspect = WGPUTextureAspect_All;
    walls_view_ = wgpuTextureCreateView(walls_texture_, &view);
  }
  if (step.end > step.first) {
    // Corners are two texels each, and WallGpuVertex is exactly those two texels.
    const auto bytes = (step.end - step.first) * sizeof(WallGpuVertex);
    write_wall_texels(static_cast<std::uint64_t>(step.first) * 2, static_cast<std::uint64_t>(step.end - step.first) * 2,
                      reinterpret_cast<const std::byte*>(store.vertices().data() + step.first));
    telemetry::add(telemetry::Counter::mesh_upload_bytes, bytes);
  }
  return walls_view_;
}

Stream::Span WgpuRenderer::pack(std::span<const Quad> quads, std::span<const Quad> overlay) {
  const auto total = quads.size() + overlay.size();
  meshes_.clear();
  meta_.resize(total);
  // The same count as GL's instance upload of the submission.
  telemetry::add(telemetry::Counter::quad_upload_bytes, total * quad_bytes);
  instance_bytes_.resize(total * quad_bytes);
  const WallGeometryStore* walls{};
  draw_plan::pack_quads(quads, instance_bytes_.data(), meta_.data(), walls, true, meshes_);
  draw_plan::pack_quads(overlay, instance_bytes_.data() + quads.size() * quad_bytes, meta_.data() + quads.size(),
                        walls, true, meshes_);
  frame_walls_ = walls ? upload_walls(*walls) : pipelines().dummy_walls();
  mesh_span_ = {};
  if (!meshes_.vertices.empty()) {
    const auto bytes = meshes_.vertices.size() * sizeof(draw_plan::MeshBatchVertex);
    mesh_span_ = stream_.write(meshes_.vertices.data(), bytes, 16);
    telemetry::add(telemetry::Counter::mesh_upload_bytes, bytes);
  }
  if (total == 0) {
    return {};
  }
  return stream_.write(instance_bytes_.data(), total * quad_bytes, 16);
}

void WgpuRenderer::begin_submission(const Atlas& atlas) {
  atlas_group_ = pipelines().atlas_group(resources().view(atlas.texture()), frame_walls_);
}

void WgpuRenderer::end_submission() noexcept {
  if (atlas_group_) {
    wgpuBindGroupRelease(atlas_group_);
    atlas_group_ = nullptr;
  }
}

void WgpuRenderer::draw_list(WGPURenderPassEncoder pass, std::span<const Quad> list,
                             std::span<const QuadMeta> meta, WGPUBuffer buffer, std::uint64_t base,
                             const QuadUniform& state) {
  for (std::size_t first = 0; first < list.size();) {
    if (meta[first].kind == 10) {
      // Retained terrain: one instance per chunk quad, drawn in ground space.
      const auto& command_quad = list[first];
      const auto* terrain = static_cast<const TerrainBuffer*>(meta[first].owner);
      if (!terrain) {
        throw std::logic_error("Missing terrain buffer");
      }
      QuadUniform push_state = state;
      push_state.standalone = 0;
      push_state.ground_space = 1;
      std::copy_n(command_quad.origin.data(), 2, push_state.ground_origin);
      std::copy_n(command_quad.axis_x.data(), 2, push_state.ground_x);
      std::copy_n(command_quad.axis_y.data(), 2, push_state.ground_y);
      push_state.ground_time = command_quad.uv_offset[0];
      bind_draw(pass, pipelines().quad(false), 0, push_state);
      wgpuRenderPassEncoderSetVertexBuffer(pass, 0, resources().buffer(terrain->buffer()), 0, WGPU_WHOLE_SIZE);
      wgpuRenderPassEncoderDraw(pass, 4, static_cast<std::uint32_t>(terrain->count()), 0, 0);
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
        QuadUniform clipped = state;
        clipped.clipped = 1;
        clipped.clip_rect[0] = command_quad.origin[0];
        clipped.clip_rect[1] = command_quad.origin[1];
        clipped.clip_rect[2] = command_quad.axis_x[0];
        clipped.clip_rect[3] = command_quad.axis_y[1];
        draw_list(pass, layer->quads, layer->meta, resources().buffer(layer->buffer), 0, clipped);
      }
      ++first;
      continue;
    }
    if (meta[first].kind == 9 && !meta[first].owner) {
      // Model instances keep their painter-stream position. A contiguous block binds the model geometry
      // once; consecutive copies of one model are one instanced draw.
      const auto end = draw_plan::model_block_end(meta, first);
      QuadUniform model_state = state;
      model_state.standalone = 0;
      bind_draw(pass, pipelines().model(), 0, model_state);
      wgpuRenderPassEncoderSetVertexBuffer(pass, 0, resources().buffer(model_geometry_), 0, WGPU_WHOLE_SIZE);
      wgpuRenderPassEncoderSetVertexBuffer(pass, 1, buffer, base, WGPU_WHOLE_SIZE);
      for (std::size_t run = first; run < end;) {
        const auto next = draw_plan::model_run_end(meta, run, end);
        const auto range = model_range(static_cast<std::uint32_t>(meta[run].model));
        wgpuRenderPassEncoderDraw(pass, range.count, static_cast<std::uint32_t>(next - run), range.first,
                                  static_cast<std::uint32_t>(run));
        telemetry::add(telemetry::Counter::models, next - run);
        telemetry::add(telemetry::Counter::triangles, (next - run) * (range.count / 3));
        run = next;
      }
      first = end;
      continue;
    }
    if (meta[first].kind == 9) {
      // Prepared meshes: a run of consecutive meshes is one draw over contiguous vertices of the stream.
      const auto end = draw_plan::mesh_block_end(meta, first);
      const auto run = draw_plan::take_mesh_ranges(meshes_, end - first);
      const auto totals = draw_plan::mesh_totals(run);
      if (totals.vertices) {
        bind_draw(pass, pipelines().mesh(), 0, state);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh_span_.buffer, mesh_span_.offset,
                                             WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(pass, static_cast<std::uint32_t>(totals.vertices), 1,
                                  static_cast<std::uint32_t>(run.front().first), 0);
        telemetry::add(telemetry::Counter::models, totals.models);
        telemetry::add(telemetry::Counter::triangles, totals.vertices / 3);
      }
      first = end;
      continue;
    }
    // A run of one texture: instances [first, end). Runs holding walls draw triangle lists, six per instance.
    const auto run = draw_plan::sprite_run(meta, first);
    const auto count = static_cast<std::uint32_t>(run.end - first);
    const auto sprites = count - static_cast<std::uint32_t>(run.walls);
    const bool walls = run.walls > 0;
    QuadUniform push_state = state;
    push_state.standalone = meta[first].texture != 0 ? 1 : 0;
    push_state.triangle_list = walls ? 1 : 0;
    bind_draw(pass, pipelines().quad(walls), meta[first].texture, push_state);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, buffer, base, WGPU_WHOLE_SIZE);
    if (walls) {
      wgpuRenderPassEncoderDraw(pass, static_cast<std::uint32_t>(std::max(6, run.max_vertices)), count, 0,
                                static_cast<std::uint32_t>(first));
      telemetry::add(telemetry::Counter::models, run.walls);
      telemetry::add(telemetry::Counter::triangles, 2 * static_cast<std::size_t>(sprites) + run.wall_triangles);
    } else {
      wgpuRenderPassEncoderDraw(pass, 4, count, 0, static_cast<std::uint32_t>(first));
      telemetry::add(telemetry::Counter::triangles, 2 * static_cast<std::size_t>(count));
    }
    telemetry::add(telemetry::Counter::quads, sprites);
    first = run.end;
  }
}

void WgpuRenderer::record_screen_overlays(WGPURenderPassEncoder pass, int width, int height, int clip_width,
                                          const ScreenEffects& effects) {
  // The breath vignette first, then the sidebar strip over it (Map.as child order). Both sample nothing,
  // so they bind the persistent dummy scene.
  if (effects.vignette > 0) {
    PostUniform values;
    values.viewport[0] = static_cast<float>(width);
    values.viewport[1] = static_cast<float>(height);
    values.region[0] = static_cast<float>(clip_width);
    values.region[1] = static_cast<float>(height);
    values.rect[2] = static_cast<float>(clip_width);
    values.rect[3] = static_cast<float>(height);
    values.mode = 1;
    values.alpha = effects.vignette;
    post_draw(pass, true, pipelines().dummy_scene_group(), values);
  }
  if (effects.sidebar > 0 && effects.sidebar <= static_cast<float>(clip_width)) {
    PostUniform values;
    values.viewport[0] = static_cast<float>(width);
    values.viewport[1] = static_cast<float>(height);
    values.region[0] = static_cast<float>(clip_width);
    values.region[1] = static_cast<float>(height);
    values.rect[0] = static_cast<float>(clip_width) - effects.sidebar;
    values.rect[2] = effects.sidebar;
    values.rect[3] = static_cast<float>(height);
    values.mode = 2;
    values.sidebar = effects.sidebar;
    post_draw(pass, true, pipelines().dummy_scene_group(), values);
  }
}

void WgpuRenderer::ensure_post_targets(std::uint32_t width, std::uint32_t height) {
  if (post_[0].texture && post_width_ == width && post_height_ == height) {
    return;
  }
  for (auto& target : post_) {
    if (target.view) {
      wgpuTextureViewRelease(target.view);
      target.view = nullptr;
    }
    if (target.texture) {
      wgpuTextureRelease(target.texture);
      target.texture = nullptr;
    }
  }
  for (auto& target : post_) {
    WGPUTextureDescriptor descriptor{};
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = extent_of(width, height);
    descriptor.format = context().format();
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    target.texture = wgpuDeviceCreateTexture(context().device(), &descriptor);
    if (!target.texture) {
      throw std::runtime_error("WebGPU offscreen target failed");
    }
    WGPUTextureViewDescriptor view{};
    view.format = context().format();
    view.dimension = WGPUTextureViewDimension_2D;
    view.mipLevelCount = 1;
    view.arrayLayerCount = 1;
    view.aspect = WGPUTextureAspect_All;
    target.view = wgpuTextureCreateView(target.texture, &view);
  }
  post_width_ = width;
  post_height_ = height;
}

void WgpuRenderer::draw(const Atlas& atlas, std::span<const Quad> quads, int width, int height, int clip_width,
                        std::span<const Quad> overlay, const ScreenEffects& effects) {
  validate_dimensions(width, height, clip_width, quads.size() + overlay.size());
  rml_ui_.close();
  draw_calls_ = 0;
  prepare_layers(quads);
  prepare_layers(overlay);
  const auto instances = pack(quads, overlay);
  begin_submission(atlas);
  const auto w = static_cast<std::uint32_t>(width);
  const auto h = static_cast<std::uint32_t>(height);
  const std::span<const QuadMeta> meta(meta_);
  const auto quad_meta = meta.first(quads.size());
  const auto overlay_meta = meta.subspan(quads.size());
  const std::uint64_t overlay_base = instances.offset + quads.size() * quad_bytes;
  QuadUniform state;
  state.viewport[0] = static_cast<float>(width);
  state.viewport[1] = static_cast<float>(height);

  const bool filtered = effects.filtered();
  const bool screen = effects.vignette > 0 || effects.sidebar > 0;
  const WGPUTextureView view = context().target(w, h);
  if (!view) {
    end_submission();
    return;
  }
  if (!filtered) {
    // The world, the screen overlays and the HUD overlay, one pass on the canvas.
    auto pass = begin_pass(view, true);
    scissor(pass, 0, 0, clip_width, height);
    draw_list(pass, quads, quad_meta, instances.buffer, instances.offset, state);
    if (screen) {
      record_screen_overlays(pass, width, height, clip_width, effects);
    }
    if (!overlay.empty()) {
      scissor(pass, 0, 0, width, height);
      draw_list(pass, overlay, overlay_meta, instances.buffer, overlay_base, state);
    }
    end_pass(pass);
    end_submission();
    telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
    return;
  }

  // A filter: the world renders offscreen, then the post passes resolve it into the canvas.
  ensure_post_targets(w, h);
  {
    // GL clears the canvas first; the post passes only write the clip region.
    auto pass = begin_pass(view, true);
    end_pass(pass);
  }
  {
    auto pass = begin_pass(post_[0].view, true);
    scissor(pass, 0, 0, clip_width, height);
    draw_list(pass, quads, quad_meta, instances.buffer, instances.offset, state);
    end_pass(pass);
  }
  PostUniform base;
  base.viewport[0] = static_cast<float>(width);
  base.viewport[1] = static_cast<float>(height);
  base.region[0] = static_cast<float>(clip_width);
  base.region[1] = static_cast<float>(height);
  base.rect[2] = static_cast<float>(clip_width);
  base.rect[3] = static_cast<float>(height);
  if (effects.blur > 0) {
    // Horizontal blur into offscreen 1, vertical into the canvas.
    PostUniform horizontal = base;
    horizontal.axis[0] = 1;
    horizontal.blur = effects.blur;
    post_pass(post_[1].view, true, post_[0].view, clip_width, height, horizontal, false);
    PostUniform vertical = base;
    vertical.axis[1] = 1;
    vertical.blur = effects.blur;
    vertical.blind = effects.blind ? 1 : 0;
    post_pass(view, false, post_[1].view, clip_width, height, vertical, false);
  } else {
    PostUniform plain = base;
    plain.blind = effects.blind ? 1 : 0;
    post_pass(view, false, post_[0].view, clip_width, height, plain, false);
  }
  if (screen) {
    auto pass = begin_pass(view, false);
    scissor(pass, 0, 0, clip_width, height);
    record_screen_overlays(pass, width, height, clip_width, effects);
    end_pass(pass);
  }
  if (!overlay.empty()) {
    auto pass = begin_pass(view, false);
    scissor(pass, 0, 0, width, height);
    draw_list(pass, overlay, overlay_meta, instances.buffer, overlay_base, state);
    end_pass(pass);
  }
  end_submission();
  telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
}

void WgpuRenderer::draw_overlay(const Atlas& atlas, std::span<const Quad> overlay, int width, int height) {
  validate_dimensions(width, height, width, overlay.size());
  if (overlay.empty()) {
    return;
  }
  rml_ui_.close();
  draw_calls_ = 0;
  prepare_layers(overlay);
  const auto instances = pack({}, overlay);
  begin_submission(atlas);
  const auto w = static_cast<std::uint32_t>(width);
  const auto h = static_cast<std::uint32_t>(height);
  const WGPUTextureView view = context().target(w, h);
  if (view) {
    QuadUniform state;
    state.viewport[0] = static_cast<float>(width);
    state.viewport[1] = static_cast<float>(height);
    auto pass = begin_pass(view, false);
    scissor(pass, 0, 0, width, height);
    draw_list(pass, overlay, std::span<const QuadMeta>(meta_), instances.buffer, instances.offset, state);
    end_pass(pass);
  }
  end_submission();
  telemetry::add(telemetry::Counter::draw_calls, draw_calls_);
}

void WgpuRenderer::finish_frame() {
  rml_ui_.close();
  context().finish_frame();
  // The frame's stream writes were queued ahead of this submission, so the next frame may reuse the chunks.
  // (Without the reset every frame would take new chunks.)
  stream_.reset();
  rml_ui_.frame_submitted();
}

Image WgpuRenderer::capture(int width, int height, bool /*presented*/) const {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("Invalid capture dimensions");
  }
  if (!capture_warned_) {
    std::cerr << "WebGPU: capture has no synchronous readback in the browser; returning a blank image\n";
    capture_warned_ = true;
  }
  // Transparent pixels of the requested size: a consumer that sizes its copy by the capture keeps working.
  return Image{width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0)};
}
}  // namespace realm::gfx::wgpu
