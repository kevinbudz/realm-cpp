#include "realm/util/Telemetry.hpp"
#include "realm/minimap/Minimap.hpp"
#include "flash_assets.hpp"
#include "flash_bitmap_colors.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
namespace realm {
namespace {
std::uint32_t most_common(const Image& image) {
  std::unordered_map<std::uint32_t, unsigned> counts;
  std::uint32_t best{};
  unsigned maximum{};
  // BitmapUtil intentionally iterates width on both axes. Rectangular
  // textures therefore ignore rows below width; out-of-bounds pixels are 0.
  for (int x = 0; x < image.width; ++x)
    for (int y = 0; y < std::min(image.width, image.height); ++y) {
      const auto at = static_cast<std::size_t>(x + y * image.width) * 4;
      if (!image.pixels[at + 3]) continue;
      const unsigned alpha = image.pixels[at + 3];
      auto channel = [&](unsigned c) {
        const unsigned value = image.pixels[at + c];
        return alpha == 255 ? value : flash_bitmap_colors[alpha * 256U + ((value * alpha + 255U) >> 8U)];
      };
      const auto color = (alpha << 24U) | (channel(0) << 16U) | (channel(1) << 8U) | channel(2);
      const auto count = ++counts[color];
      if (count > maximum || (count == maximum && color > best)) {
        maximum = count;
        best = color;
      }
    }
  return best & 0xffffffU;
}
void opaque_pixel(Image& image, int x, int y, std::uint32_t color) {
  const auto at = static_cast<std::size_t>(x + y * image.width) * 4;
  image.pixels[at] = static_cast<unsigned char>(color >> 16U);
  image.pixels[at + 1] = static_cast<unsigned char>(color >> 8U);
  image.pixels[at + 2] = static_cast<unsigned char>(color);
  image.pixels[at + 3] = 255;
}
}
Minimap::Minimap(const Catalog& catalog, const AnimationLibrary& animations, const std::filesystem::path& root)
    : catalog_(catalog), animations_(animations) {
  for (const auto& asset : flash::assets) {
    auto image = load_png(root / asset.path);
    normalize_flash_png(image);
    assets_.push_back(std::move(image));
  }
  const auto sheet = std::find_if(flash::sheets.begin(), flash::sheets.end(),
                                  [](const auto& value) { return value.name == "lofiInterface"; });
  if (sheet == flash::sheets.end()) throw std::runtime_error("Missing minimap interface sheet");
  auto get = [&](int index) {
    const auto& image = assets_.at(static_cast<std::size_t>(sheet->asset));
    const int cols = image.width / sheet->width;
    return crop(image, index % cols * sheet->width, index / cols * sheet->height, sheet->width, sheet->height);
  };
  out_button_ = get(54);
  in_button_ = get(55);
  arrow_ = out_button_;
  premultiply_flash_bitmap(out_button_);
  premultiply_flash_bitmap(in_button_);
  premultiply_flash_bitmap(arrow_);
  for (std::size_t at = 0; at < arrow_.pixels.size(); at += 4) arrow_.pixels[at] = arrow_.pixels[at + 1] = 0;
  // Retain colors, rather than a second complete copy of the source PNGs.
  std::function<void(const TextureDef&)> collect = [&](const TextureDef& texture) {
    if (texture.sheet >= 0 || texture.animated >= 0) (void)texture_color(texture, 0);
    for (const auto& child : texture.random) collect(child);
    for (const auto& [id, child] : texture.alternate) {
      (void)id;
      collect(child);
    }
  };
  for (const auto& def : catalog_.grounds()) collect(def.texture);
  for (const auto& def : catalog_.objects()) {
    collect(def.texture);
    collect(def.top);
  }
  std::vector<Image>().swap(assets_);
}
std::uint32_t Minimap::texture_color(const TextureDef& texture, std::uint32_t id) const {
  const auto& selected = texture.select(id);
  const auto key =
      (static_cast<std::uint64_t>(selected.animated >= 0 ? 0x10000 + selected.animated : selected.sheet + 1) << 32U) |
      static_cast<std::uint32_t>(selected.index);
  if (const auto cached = texture_colors_.find(key); cached != texture_colors_.end()) return cached->second;
  if (selected.animated >= 0)
    if (const auto* character = animations_.character(selected.animated, selected.index)) {
      auto image = animations_.images().at(character->frame(0, 0, Action::stand, 0).image);
      normalize_flash_png(image);
      return texture_colors_[key] = most_common(image);
    }
  if (selected.sheet >= 0) {
    const auto& sheet = flash::sheets.at(static_cast<std::size_t>(selected.sheet));
    if (sheet.asset < 0) return 0;
    const auto& image = assets_.at(static_cast<std::size_t>(sheet.asset));
    const int cols = image.width / sheet.width;
    return texture_colors_[key] = most_common(crop(image, selected.index % cols * sheet.width,
                                                   selected.index / cols * sheet.height, sheet.width, sheet.height));
  }
  return 0;
}
std::uint32_t Minimap::ground_color(std::uint16_t type) {
  if (const auto found = ground_colors_.find(type); found != ground_colors_.end()) return found->second;
  const auto* def = catalog_.ground(type);
  const auto color = def ? def->xml.has("Color") ? static_cast<std::uint32_t>(def->xml.number("Color")) & 0xffffffU
                                                 : texture_color(def->texture, 0)
                         : 0;
  return ground_colors_[type] = color;
}
std::uint32_t Minimap::object_color(std::uint16_t type, std::uint32_t id) {
  if (const auto found = object_colors_.find(type); found != object_colors_.end()) return found->second;
  const auto* def = catalog_.object(type);
  return object_colors_[type] = def ? texture_color(def->class_name == "Wall" ? def->top : def->texture, id) : 0;
}
bool Minimap::sync(const World& world) {
  changed_rows_.clear();
  if (!world.width() || !world.height()) return false;
  const bool resized = map_.width != world.width() || map_.height != world.height();
  if (resized) {
    map_ = {world.width(), world.height(),
            std::vector<unsigned char>(static_cast<std::size_t>(world.width()) *
                                       static_cast<std::size_t>(world.height()) * 4)};
    rows_.assign(static_cast<std::size_t>(world.height()), 0);
    zoom_levels_.clear();
    const double minimum = std::max(192.0 / world.width(), 192.0 / world.height());
    for (double zoom = 4; zoom > minimum; zoom /= 2) zoom_levels_.push_back(zoom);
    zoom_levels_.push_back(minimum);
    zoom_index_ = std::min(zoom_index_, static_cast<int>(zoom_levels_.size()) - 1);
  }
  if (!resized && revision_ == world.minimap_revision()) return false;
  for (const auto& [type, id] : world.minimap_object_ids()) (void)object_color(type, id);
  bool changed = resized;
  for (int y = 0; y < world.height(); ++y) {
    const auto row = static_cast<std::size_t>(y);
    if (rows_[row] == world.minimap_rows()[row]) continue;
    for (int x = 0; x < world.width(); ++x) {
      const auto code = world.tile(x, y)->minimap_code;
      const auto color = !(code & 0x20000U) ? 0
                         : code & 0x10000U  ? object_color(static_cast<std::uint16_t>(code))
                                            : ground_color(static_cast<std::uint16_t>(code));
      opaque_pixel(map_, x, y, color);
    }
    rows_[row] = world.minimap_rows()[row];
    ++colored_rows_;
    changed_rows_.push_back(y);
    changed = true;
  }
  revision_ = world.minimap_revision();
  return changed;
}
void Minimap::set_pixel_scale(double scale) {
  if (image_.pixel_scale == scale) return;
  image_ = ui_image(192, 192, scale);
  drawn_zoom_ = -1;
}
void Minimap::zoom_in() {
  zoom_index_ = std::max(0, zoom_index_ - 1);
  button_zoom_ = zoom_index_;
}
void Minimap::zoom_out() {
  if (!zoom_levels_.empty()) zoom_index_ = std::min(static_cast<int>(zoom_levels_.size()) - 1, zoom_index_ + 1);
  button_zoom_ = zoom_index_;
}
const MinimapView& Minimap::gather(const World& world, const Entity& focus, double angle) {
  REALM_ZONE("minimap.update");
  auto& view = pending_;
  view.focus = focus.motion.position;
  view.angle = angle;
  view.markers.clear();
  {
    REALM_ZONE("minimap.markers");
    // Marker kinds only, in dense order: markers blend, so they keep the order a full scan would draw them in.
    world.each_marker_candidate([&](const Entity& entity) {
      const auto* def = catalog_.object(entity.type);
      if (!def || def->no_minimap || entity.id == focus.id || entity.projectile) return;
      std::uint32_t color{};
      if (entity.player)
        color = !focus.profile.guild.empty() && focus.profile.guild == entity.profile.guild ? 65280U : 16776960U;
      else if (def->class_name == "Character")
        color = def->enemy ? 16711680U : object_color(entity.type, static_cast<std::uint32_t>(entity.id));
      else if (def->class_name == "Portal" || def->class_name == "GuildHallPortal")
        color = 255;
      else
        return;
      view.markers.push_back({entity.motion.position, color});
    });
  }
  return view;
}
bool Minimap::update(const World& world, const Entity& focus, double angle) {
  const auto& view = gather(world, focus, angle);
  REALM_ZONE("minimap.render");
  return render(world, view);
}
const MinimapView& Minimap::prepare_gpu(const World& world, const Entity& focus, double angle) {
  const auto& view = gather(world, focus, angle);
  if (!std::isfinite(view.focus.x) || !std::isfinite(view.focus.y) || !std::isfinite(angle) ||
      std::abs(view.focus.x) > 1048576 || std::abs(view.focus.y) > 1048576)
    throw std::invalid_argument("Invalid minimap view");
  sync(world);
  return view;
}
Vec2 Minimap::marker_position(const World& world, const Entity& focus, Vec2 position) const {
  if (zoom_levels_.empty()) return {-1000, -1000};
  const double zoom = zoom_levels_[static_cast<std::size_t>(zoom_index_)];
  const double tx = std::clamp(-focus.motion.position.x * zoom, 96 - world.width() * zoom, -96.0);
  const double ty = std::clamp(-focus.motion.position.y * zoom, 96 - world.height() * zoom, -96.0);
  double x = position.x * zoom + tx, y = position.y * zoom + ty;
  const double outside = std::max(std::abs(x), std::abs(y));
  if (outside >= 96) {
    x *= 96 / outside;
    y *= 96 / outside;
  }
  return {96 + x, 96 + y};
}
std::vector<std::int32_t> Minimap::hover_players(const World& world, const Entity& focus, Vec2 pointer) const {
  std::vector<std::int32_t> result;
  if (pointer.x < 0 || pointer.y < 0 || pointer.x >= 192 || pointer.y >= 192) return result;
  world.each_player([&](const Entity& entity) {
    const auto* def = catalog_.object(entity.type);
    if (!entity.player || entity.id == focus.id || !def || def->no_minimap) return;
    const auto p = marker_position(world, focus, entity.motion.position);
    const double dx = pointer.x - p.x, dy = pointer.y - p.y;
    if (dx * dx + dy * dy < 25) result.push_back(entity.id);
  });
  return result;
}
void Minimap::rectangle(double x, double y, double width, double height, std::uint32_t color) {
  if (image_.pixel_scale != 1) {
    ui_rectangle(image_, x, y, width, height, color);
    return;
  }
  // Graphics positions are stored in twips before coverage rasterization.
  x = std::round(x * 20) / 20;
  y = std::round(y * 20) / 20;
  for (int row = std::max(0, static_cast<int>(std::floor(y)));
       row < std::min(192, static_cast<int>(std::ceil(y + height))); ++row)
    for (int col = std::max(0, static_cast<int>(std::floor(x)));
         col < std::min(192, static_cast<int>(std::ceil(x + width))); ++col) {
      const double cx = std::clamp(std::min(x + width, col + 1.0) - std::max(x, static_cast<double>(col)), 0.0, 1.0),
                   cy = std::clamp(std::min(y + height, row + 1.0) - std::max(y, static_cast<double>(row)), 0.0, 1.0);
      const unsigned alpha = static_cast<unsigned>(std::lround(cx * 4) * std::lround(cy * 4)) * 16U;
      const auto at = static_cast<std::size_t>(row * 192 + col) * 4;
      for (unsigned c = 0; c < 3; ++c)
        image_.pixels[at + c] =
            static_cast<unsigned char>((((color >> (16U - 8U * c)) & 255U) * alpha +
                                        static_cast<unsigned>(image_.pixels[at + c]) * (256 - alpha)) >>
                                       8U);
    }
}
void Minimap::bitmap(const Image& source, double x, double y, double sx, double sy, double multiplier) {
  const double density = image_.pixel_scale;
  x *= density;
  y *= density;
  sx *= density;
  sy *= density;
  for (int row = std::max(0, static_cast<int>(std::floor(y)));
       row < std::min(image_.height, static_cast<int>(std::ceil(y + source.height * sy))); ++row)
    for (int col = std::max(0, static_cast<int>(std::floor(x)));
         col < std::min(image_.width, static_cast<int>(std::ceil(x + source.width * sx))); ++col) {
      const int ix = static_cast<int>(std::floor((col + .5 - x) / sx)),
                iy = static_cast<int>(std::floor((row + .5 - y) / sy));
      if (ix < 0 || iy < 0 || ix >= source.width || iy >= source.height) continue;
      const auto from = static_cast<std::size_t>(ix + iy * source.width) * 4,
                 to = static_cast<std::size_t>(col + row * image_.width) * 4;
      const unsigned alpha = source.pixels[from + 3];
      for (unsigned c = 0; c < 3; ++c)
        image_.pixels[to + c] =
            static_cast<unsigned char>(static_cast<unsigned>(source.pixels[from + c] * multiplier) +
                                       (static_cast<unsigned>(image_.pixels[to + c]) * (256 - alpha) >> 8U));
    }
}
bool Minimap::render(const World& world, const MinimapView& view) {
  if (!std::isfinite(view.focus.x) || !std::isfinite(view.focus.y) || !std::isfinite(view.angle) ||
      std::abs(view.focus.x) > 1048576 || std::abs(view.focus.y) > 1048576)
    throw std::invalid_argument("Invalid minimap view");
  for (const auto& marker : view.markers)
    if (!std::isfinite(marker.position.x) || !std::isfinite(marker.position.y) ||
        std::abs(marker.position.x) > 1048576 || std::abs(marker.position.y) > 1048576)
      throw std::invalid_argument("Invalid minimap marker");
  const bool changed = sync(world);
  if (zoom_levels_.empty()) return false;
  if (!changed && drawn_zoom_ == zoom_index_ && drawn_button_ == button_zoom_ && view_.focus.x == view.focus.x &&
      view_.focus.y == view.focus.y && view_.angle == view.angle && view_.markers == view.markers)
    return false;
  const double zoom = zoom_levels_[static_cast<std::size_t>(zoom_index_)];
  const double tx = std::clamp(-view.focus.x * zoom, 96 - map_.width * zoom, -96.0),
               ty = std::clamp(-view.focus.y * zoom, 96 - map_.height * zoom, -96.0);
  const double density = image_.pixel_scale;
  for (int y = 0; y < image_.height; ++y)
    for (int x = 0; x < image_.width; ++x) {
      const int mx = static_cast<int>(std::floor(((x + .5) / density - 96 - std::trunc(tx * 20) / 20) / zoom)),
                my = static_cast<int>(std::floor(((y + .5) / density - 96 - std::trunc(ty * 20) / 20) / zoom));
      if (mx < 0 || my < 0 || mx >= map_.width || my >= map_.height)
        opaque_pixel(image_, x, y, 1776411);
      else {
        const auto from = static_cast<std::size_t>(mx + my * map_.width) * 4,
                   to = static_cast<std::size_t>(x + y * image_.width) * 4;
        std::copy_n(map_.pixels.begin() + static_cast<std::ptrdiff_t>(from), 4,
                    image_.pixels.begin() + static_cast<std::ptrdiff_t>(to));
      }
    }
  for (const auto& marker : view.markers) {
    double x = marker.position.x * zoom + tx, y = marker.position.y * zoom + ty;
    const double outside = std::max(std::abs(x), std::abs(y));
    if (outside >= 96) {
      x *= 96 / outside;
      y *= 96 / outside;
    }
    rectangle(96 + x - 2, 96 + y - 2, 4, 4, marker.color);
  }
  const double px = 96 + view.focus.x * zoom + tx, py = 96 + view.focus.y * zoom + ty, c = std::cos(view.angle),
               s = std::sin(view.angle);
  for (int y = std::max(0, static_cast<int>(std::floor((py - 16) * density)));
       y < std::min(image_.height, static_cast<int>(std::ceil((py + 16) * density))); ++y)
    for (int x = std::max(0, static_cast<int>(std::floor((px - 16) * density)));
         x < std::min(image_.width, static_cast<int>(std::ceil((px + 16) * density))); ++x) {
      const double dx = (x + .5) / density - px, dy = (y + .5) / density - py;
      const int ax = static_cast<int>(std::floor((c * dx + s * dy) * arrow_.width / 8 + 4)),
                ay = static_cast<int>(std::floor((-s * dx + c * dy) * arrow_.height / 32 + 5));
      if (ax < 0 || ay < 0 || ax >= arrow_.width || ay >= arrow_.height) continue;
      const auto from = static_cast<std::size_t>(ax + ay * arrow_.width) * 4,
                 to = static_cast<std::size_t>(x + y * image_.width) * 4;
      const unsigned alpha = arrow_.pixels[from + 3];
      for (unsigned channel = 0; channel < 3; ++channel)
        image_.pixels[to + channel] = static_cast<unsigned char>(
            arrow_.pixels[from + channel] + (static_cast<unsigned>(image_.pixels[to + channel]) * (256 - alpha) >> 8U));
    }
  bitmap(out_button_, 172, 4, 2, 2, button_zoom_ > 0 ? 1 : .5);
  bitmap(in_button_, 172, 14, 2, 2, button_zoom_ < static_cast<int>(zoom_levels_.size()) - 1 ? 1 : .5);
  view_ = view;
  drawn_zoom_ = zoom_index_;
  drawn_button_ = button_zoom_;
  ++redraws_;
  return true;
}
}
