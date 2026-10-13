#include "realm/minimap_gpu.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
std::array<float, 2> floats(Vec2 v) { return {static_cast<float>(v.x), static_cast<float>(v.y)}; }
}
Quad MinimapGpu::build(Minimap& minimap, const World& world, const Entity& focus, double angle, Vec2 origin,
                       double scale) {
  REALM_ZONE("minimap.gpu_build");
  const auto& view = minimap.prepare_gpu(world, focus, angle);
  const auto& image = minimap.map_image();
  if (source_ != &minimap || generation_ != world.generation() || width_ != image.width || height_ != image.height) {
    map_.update(image, true);
    arrow_.update(minimap.arrow_image(), true);
    out_.update(minimap.out_button_image(), true);
    in_.update(minimap.in_button_image(), true);
    source_ = &minimap;
    generation_ = world.generation();
    width_ = image.width;
    height_ = image.height;
  } else {
    // Coalesce adjacent dirty rows. Panning and marker motion upload no map pixels.
    const auto rows = minimap.changed_rows();
    for (std::size_t first = 0; first < rows.size();) {
      auto end = first + 1;
      while (end < rows.size() && rows[end] == rows[end - 1] + 1) ++end;
      map_.update_region(image, 0, rows[first], image.width, rows[end - 1] - rows[first] + 1, true);
      first = end;
    }
  }
  auto& quads = layer_->quads;
  quads.clear();
  quads.reserve(view.markers.size() + 4);
  if (!minimap.zoom_levels().empty()) {
    const double zoom = minimap.zoom_levels()[static_cast<std::size_t>(minimap.zoom_index())];
    const double tx = std::clamp(-view.focus.x * zoom, 96 - image.width * zoom, -96.0),
                 ty = std::clamp(-view.focus.y * zoom, 96 - image.height * zoom, -96.0);
    auto map = map_.quad(origin, {192 * scale, 0}, {0, 192 * scale});
    map.kind = 13;
    map.uv = {static_cast<float>((-96 - std::trunc(tx * 20) / 20) / zoom / image.width),
              static_cast<float>((-96 - std::trunc(ty * 20) / 20) / zoom / image.height),
              static_cast<float>(192 / zoom / image.width), static_cast<float>(192 / zoom / image.height)};
    quads.push_back(map);
    for (const auto& marker : view.markers) {
      double x = marker.position.x * zoom + tx, y = marker.position.y * zoom + ty;
      const auto outside = std::max(std::abs(x), std::abs(y));
      if (outside >= 96) {
        x *= 96 / outside;
        y *= 96 / outside;
      }
      // A padded quad supplies fractional marker coverage at its edges.
      const double left = std::round((96 + x - 2) * 20) / 20, top = std::round((96 + y - 2) * 20) / 20;
      Quad q;
      q.kind = 15;
      q.origin = floats({origin.x + std::floor(left * scale), origin.y + std::floor(top * scale)});
      q.axis_x = {static_cast<float>(std::ceil((left + 4) * scale) - std::floor(left * scale)), 0};
      q.axis_y = {0, static_cast<float>(std::ceil((top + 4) * scale) - std::floor(top * scale))};
      q.multiply = {static_cast<float>((marker.color >> 16U) & 255U) / 255,
                    static_cast<float>((marker.color >> 8U) & 255U) / 255,
                    static_cast<float>(marker.color & 255U) / 255, 1};
      q.offset = {static_cast<float>(origin.x + left * scale), static_cast<float>(origin.y + top * scale), 0, 0};
      q.uv_offset[0] = static_cast<float>(scale);
      quads.push_back(q);
    }
    const double px = 96 + view.focus.x * zoom + tx, py = 96 + view.focus.y * zoom + ty;
    auto arrow =
        arrow_.quad({origin.x + (px - 16) * scale, origin.y + (py - 16) * scale}, {32 * scale, 0}, {0, 32 * scale});
    arrow.kind = 16;
    arrow.uv_offset = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    quads.push_back(arrow);
    auto out = out_.quad({origin.x + 172 * scale, origin.y + 4 * scale}, {16 * scale, 0}, {0, 16 * scale});
    auto in = in_.quad({origin.x + 172 * scale, origin.y + 14 * scale}, {16 * scale, 0}, {0, 16 * scale});
    if (minimap.button_zoom() <= 0) out.multiply = {.5F, .5F, .5F, 1};
    if (minimap.button_zoom() >= static_cast<int>(minimap.zoom_levels().size()) - 1) in.multiply = {.5F, .5F, .5F, 1};
    quads.push_back(out);
    quads.push_back(in);
  }
  Quad command;
  command.kind = 14;
  command.owner = layer_;
  command.origin = floats(origin);
  command.axis_x = {static_cast<float>(192 * scale), 0};
  command.axis_y = {0, static_cast<float>(192 * scale)};
  return command;
}
}
