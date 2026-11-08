#include "realm/mapeditor/EditorScreen.hpp"
#include "realm/assets/AssetStore.hpp"
#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace realm {
namespace {
// EditingScreen.as, MECommandMenu.as, MEMap.as, Chooser.as, DropDown.as, InfoPane.as positions in
// 800x600 design units.
constexpr double menu_x = 15, menu_y = 78, menu_width = 80, menu_height = 25, menu_step = 30;
constexpr double canvas_x = 144, canvas_y = 78, canvas_size = 512;
constexpr double layer_x = 660, layer_y = 78, layer_width = 136, layer_height = 26;
constexpr double chooser_x = 660, chooser_y = 108, chooser_width = 136, chooser_height = 480;
constexpr double element_size = 50, scroll_window = 480;
constexpr double info_x = 4, info_y = 440, info_width = 134, info_height = 150;
constexpr double min_zoom = .125, max_zoom = 16;
constexpr std::uint32_t panel = 0x363636, hover_fill = 0x565656, pressed_fill = 0x7f7f7f,
                        grey = 0xb3b3b3, white = 0xffffff;
// MEMap.drawTile tints a region with alpha 0x5F; RegionElement fills with alpha .5.
constexpr unsigned region_tile_alpha = 0x5f, region_element_alpha = 128;

struct MenuSlot {
  std::string_view label;
  int row;  // 30 px slots; the break after Clear is row 7.
};

// Flash MECommandMenu.as order. The rows are the 30 px slots of EditingScreen's layout.
constexpr std::array<MenuSlot, 10> menu_slots{{{"(D)raw", 0},
                                               {"(E)rase", 1},
                                               {"S(A)mple", 2},
                                               {"Ed(I)t", 3},
                                               {"(U)ndo", 4},
                                               {"(R)edo", 5},
                                               {"(C)lear", 6},
                                               {"(L)oad", 8},
                                               {"(S)ave", 9},
                                               {"(T)est", 10}}};

constexpr std::array<std::string_view, 3> layer_names{"Ground", "Objects", "Regions"};

// Frame.as: a 288 wide frame whose sprite sits at (DESIGN_WIDTH - (w - 6)) / 2 and is centered
// vertically by its height; the body is drawn 6 px up and left of the sprite origin.
constexpr double frame_width = 288, frame_x = (800 - (frame_width - 6)) / 2;
constexpr int message_width = 254;
constexpr std::uint32_t title_fill = 0x4d4d4d, input_fill = 0x333333, input_stroke = 0x454545,
                        hover_text = 0xffdc85, tooltip_fill = 0x363636,
                        tooltip_outline = 0x9b9b9b;
// TextInputField: the box is 238 wide and its text starts 6 px in; the caret stays inside it.
constexpr double input_width = 238, input_text_x = 6, input_text_room = 226;
constexpr std::size_t name_limit = 64;
// TextInputField.HEIGHT.
constexpr double TextInputFieldHeight = 88;

double frame_top(double height) {
  return (600 - height) / 2;
}

// ObjectTypeToolTip.as: the description lines in Flash's order, straight from the object's XML.
std::string object_body(const Xml& xml) {
  std::string desc;
  if (xml.has("Group")) {
    desc += "Group: " + xml.child("Group").text() + "\n";
  }
  if (xml.has("Static")) {
    desc += "Static\n";
  }
  if (xml.has("Enemy")) {
    desc += "Enemy\n";
    if (xml.has("MaxHitPoints")) {
      desc += "MaxHitPoints: " + xml.child("MaxHitPoints").text() + "\n";
    }
    if (xml.has("Defense")) {
      desc += "Defense: " + xml.child("Defense").text() + "\n";
    }
  }
  if (xml.has("God")) {
    desc += "God\n";
  }
  if (xml.has("Quest")) {
    desc += "Quest\n";
  }
  if (xml.has("Hero")) {
    desc += "Hero\n";
  }
  if (xml.has("Encounter")) {
    desc += "Encounter\n";
  }
  if (xml.has("Level")) {
    desc += "Level: " + xml.child("Level").text() + "\n";
  }
  if (xml.has("Terrain")) {
    desc += "Terrain: " + xml.child("Terrain").text() + "\n";
  }
  for (const auto& projectile : xml.children("Projectile")) {
    desc += "Projectile " + projectile.attribute("id") + ": " +
            projectile.child("ObjectId").text() + "\n\tDamage: " +
            projectile.child("Damage").text() + "\n\tSpeed: " +
            projectile.child("Speed").text() + "\n";
    if (projectile.has("PassesCover")) {
      desc += "\tPassesCover\n";
    }
    if (projectile.has("MultiHit")) {
      desc += "\tMultiHit\n";
    }
    if (const auto effect = projectile.child("ConditionEffect")) {
      desc += "\t" + effect.text() + " for " + effect.attribute("duration") + " secs\n";
    }
    if (projectile.has("Parametric")) {
      desc += "\tParametric\n";
    }
  }
  return desc;
}

// GroundTypeToolTip.as: a missing Speed reads as 1.00, and each flag adds its own line.
std::string ground_body(const Xml& xml) {
  std::string desc;
  if (xml.has("Speed")) {
    char speed[32];
    std::snprintf(speed, sizeof speed, "%.2f", xml.number("Speed"));
    desc += std::string("Speed: ") + speed + "\n";
  } else {
    desc += "Speed: 1.00\n";
  }
  if (xml.has("NoWalk")) {
    desc += "Unwalkable\n";
  }
  if (xml.has("Push")) {
    desc += "Push\n";
  }
  if (xml.has("Sink")) {
    desc += "Sink\n";
  }
  if (xml.has("Sinking")) {
    desc += "Sinking\n";
  }
  if (xml.has("Animate")) {
    desc += "Animated\n";
  }
  if (xml.has("RandomOffset")) {
    desc += "Randomized\n";
  }
  return desc;
}

bool inside(Vec2 p, double x, double y, double w, double h) {
  return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
}

// Chooser.as content height: rows of 50 px plus the 6 px top margin.
double content_height(std::size_t count) {
  return static_cast<double>((count + 1) / 2) * element_size + 6;
}

std::uint32_t hash_color(std::string_view id, std::uint32_t base) {
  std::uint32_t hash = 2166136261U;
  for (const auto c : id) {
    hash = (hash ^ static_cast<unsigned char>(c)) * 16777619U;
  }
  const int r = static_cast<int>((base >> 16U) & 255U), g = static_cast<int>((base >> 8U) & 255U),
            b = base & 255U;
  const int shade = 74 + static_cast<int>((hash >> 8U) % 54U);
  return (static_cast<std::uint32_t>((r * shade / 128) & 255) << 16U) |
         (static_cast<std::uint32_t>((g * shade / 128) & 255) << 8U) |
         static_cast<std::uint32_t>((b * shade / 128) & 255);
}

// Flash saves an empty object name as no name at all.
std::optional<std::string> to_name(std::string value) {
  return value.empty() ? std::nullopt : std::optional<std::string>(std::move(value));
}

void pop_utf8(std::string& value) {
  while (!value.empty()) {
    const auto byte = static_cast<unsigned char>(value.back());
    value.pop_back();
    if ((byte & 0xc0U) != 0x80U) {
      break;
    }
  }
}

// A premultiplied color; alpha 255 gives the opaque color.
std::array<unsigned char, 4> premultiplied(std::uint32_t color, unsigned alpha) {
  const auto channel = [&](unsigned shift) {
    return static_cast<unsigned char>(((color >> shift) & 255U) * alpha / 255U);
  };
  return {channel(16), channel(8), channel(0), static_cast<unsigned char>(alpha)};
}

// Source-over of a premultiplied pixel onto a premultiplied pixel.
void over(unsigned char* destination, const unsigned char* source) {
  const unsigned inverse = 255U - source[3];
  for (std::size_t c = 0; c < 4; ++c) {
    destination[c] = static_cast<unsigned char>(std::min(
        255U, static_cast<unsigned>(source[c]) + (destination[c] * inverse + 127U) / 255U));
  }
}

// Nearest-neighbour texel of a premultiplied image at normalized coordinates in [0, 1).
const unsigned char* texel(const Image& image, double u, double v) {
  const int x = std::clamp(static_cast<int>(u * image.width), 0, image.width - 1);
  const int y = std::clamp(static_cast<int>(v * image.height), 0, image.height - 1);
  return &image.pixels[(static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) *
                       4];
}

// Source-over of a premultiplied color over the device pixels of a logical rectangle, using pixel
// centers so neighbouring rectangles never double-paint a pixel.
void paint_rectangle(Image& image, double x, double y, double width, double height,
                     const std::array<unsigned char, 4>& color) {
  const double scale = image.pixel_scale;
  const int left = std::max(0, static_cast<int>(std::floor(x * scale)));
  const int top = std::max(0, static_cast<int>(std::floor(y * scale)));
  const int right = std::min(image.width, static_cast<int>(std::ceil((x + width) * scale)));
  const int bottom = std::min(image.height, static_cast<int>(std::ceil((y + height) * scale)));
  for (int py = top; py < bottom; ++py) {
    const double ly = (py + .5) / scale;
    if (ly < y || ly >= y + height) {
      continue;
    }
    for (int px = left; px < right; ++px) {
      const double lx = (px + .5) / scale;
      if (lx < x || lx >= x + width) {
        continue;
      }
      over(&image.pixels[(static_cast<std::size_t>(py) * image.width + px) * 4], color.data());
    }
  }
}
}  // namespace

EditorScreen::EditorScreen(const Catalog& catalog, const FontBook& fonts,
                           const std::filesystem::path& flash_root, const HudArt* art)
    : catalog_(catalog), fonts_(fonts), art_(art), document_(catalog, flash_root),
      dialog_(std::make_unique<platform::FileDialog>()) {
  // MEMap_transbackgroundEmbed_: the checker bitmap, loaded from the Flash root like the other UI
  // PNGs.
  const auto checker =
      flash_root / "src/com/company/assembleegameclient/mapeditor/MEMap_transbackgroundEmbed_.png";
  if (assets::asset_exists(checker)) {
    checker_ = load_png(checker);
    normalize_flash_png(checker_);
    premultiply_flash_bitmap(checker_);
  }
  for (std::size_t i = 0; i < choosers_.size(); ++i) {
    auto& chooser = choosers_[i];
    chooser.layer = static_cast<EditorLayer>(i);
    const auto count = document_.choices(chooser.layer).size();
    chooser.art.resize(count);
    chooser.scroll.indicator(scroll_window, content_height(count));
  }
  for (const auto& choice : document_.choices(EditorLayer::region)) {
    region_colors_.emplace(choice.id, choice.color & 0xffffffU);
  }
  // MEMap posT_ starts centered: 128 - sizeInTiles / 2.
  origin_ = {MapDocument::extent / 2 - size_tiles() / 2, MapDocument::extent / 2 - size_tiles() / 2};
}

void EditorScreen::resize(double width, double height, double pixel_scale) {
  const auto safe_scale = std::isfinite(pixel_scale) && pixel_scale > 0 ? pixel_scale : 1.0;
  if (drawable_width_ == width && drawable_height_ == height && scale_ == safe_scale) {
    return;
  }
  cancel_pointer();
  if (scale_ != safe_scale) {
    clear_art();
  }
  drawable_width_ = std::max(width, 1.0);
  drawable_height_ = std::max(height, 1.0);
  scale_ = safe_scale;
  mark_dirty();
}

Vec2 EditorScreen::logical(Vec2 drawable) const {
  const double layout_scale = drawable_height_ / 600.0;
  const double offset_x = (drawable_width_ - 800.0 * layout_scale) / 2.0;
  const double offset_y = (drawable_height_ - 600.0 * layout_scale) / 2.0;
  return {(drawable.x - offset_x) / layout_scale, (drawable.y - offset_y) / layout_scale};
}

bool EditorScreen::covers(Vec2 point) const {
  const auto p = logical(point);
  return p.x >= 0 && p.x < 800 && p.y >= 30 && p.y < 600;
}

bool EditorScreen::in_canvas(Vec2 p) const {
  return inside(p, canvas_x, canvas_y, canvas_size, canvas_size);
}

bool EditorScreen::over_canvas() const {
  return pointer_valid_ && in_canvas(pointer_);
}

int EditorScreen::size_tiles() const {
  // MEMap.sizeInTiles.
  return static_cast<int>(canvas_size / (16 * zoom_));
}

EditorScreen::Point EditorScreen::mouse_tile() const {
  // MEMap.mousePosT: the pointer clamped to the canvas, then posT plus the tile under it.
  const double tile = 16 * zoom_;
  const double x = std::clamp(pointer_.x - canvas_x, 0.0, canvas_size - 1);
  const double y = std::clamp(pointer_.y - canvas_y, 0.0, canvas_size - 1);
  return {origin_.x + static_cast<int>(x / tile), origin_.y + static_cast<int>(y / tile)};
}

EditorScreen::TileRect EditorScreen::mouse_rect() const {
  // MEMap.mouseRectT: one tile, or the inclusive rectangle between the Shift anchor and the pointer.
  const auto t = mouse_tile();
  if (!rect_anchor_) {
    return {t.x, t.y, 1, 1};
  }
  const auto anchor = *rect_anchor_;
  return {std::min(t.x, anchor.x), std::min(t.y, anchor.y), std::abs(t.x - anchor.x) + 1,
          std::abs(t.y - anchor.y) + 1};
}

bool EditorScreen::can_move() const {
  return !rect_anchor_ && !move_anchor_;
}

void EditorScreen::move_view(int dx, int dy) {
  // MEMap.movePosT clamps the view to the 256 tile map.
  const int limit = MapDocument::extent - size_tiles();
  origin_.x = std::clamp(origin_.x + dx, 0, limit);
  origin_.y = std::clamp(origin_.y + dy, 0, limit);
  mark_dirty();
}

void EditorScreen::zoom_by(double factor) {
  // MEMap.modifyZoom keeps the tile under the pointer fixed.
  if ((factor > 1 && zoom_ >= max_zoom) || (factor < 1 && zoom_ <= min_zoom)) {
    return;
  }
  const auto before = mouse_tile();
  zoom_ = std::clamp(zoom_ * factor, min_zoom, max_zoom);
  const auto after = mouse_tile();
  move_view(before.x - after.x, before.y - after.y);
  mark_dirty();
}

double EditorScreen::scroll_offset(const Chooser& chooser) const {
  return chooser.scroll.pos() * (content_height(chooser.art.size()) + 12 - scroll_window);
}

Vec2 EditorScreen::scroll_local(Vec2 p) const {
  return {p.x - (chooser_x + 110), p.y - (chooser_y + 4)};
}

std::optional<std::size_t> EditorScreen::element_at(Vec2 p) const {
  // Chooser.as: elements sit at chooser-local (4 + 52 * column, 12 + 50 * row - scroll) and are
  // masked to (0, 2, 112, 476). An open layer list is drawn over the chooser's top rows.
  if (!inside(p, chooser_x, chooser_y + 2, 112, 476) || (dropdown_open_ && in_layer_list(p))) {
    return std::nullopt;
  }
  const auto& chooser = current_chooser();
  const double local_x = p.x - (chooser_x + 4);
  if (local_x < 0 || local_x >= 102) {
    return std::nullopt;
  }
  const std::size_t column = local_x >= 52 ? 1 : 0;
  if (local_x - 52.0 * static_cast<double>(column) >= element_size) {
    return std::nullopt;
  }
  const double local_y = p.y - (chooser_y + 12) + scroll_offset(chooser);
  if (local_y < 0) {
    return std::nullopt;
  }
  const auto row = static_cast<std::size_t>(local_y / element_size);
  if (local_y - static_cast<double>(row) * element_size >= element_size) {
    return std::nullopt;
  }
  const auto index = row * 2 + column;
  if (index >= chooser.art.size()) {
    return std::nullopt;
  }
  return index;
}

std::optional<EditorScreen::MenuItem> EditorScreen::menu_at(Vec2 p) const {
  for (std::size_t i = 0; i < menu_slots.size(); ++i) {
    if (inside(p, menu_x, menu_y + menu_step * menu_slots[i].row, menu_width, menu_height)) {
      return static_cast<MenuItem>(i);
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> EditorScreen::layer_item_at(Vec2 p) const {
  if (!dropdown_open_) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < layer_names.size(); ++i) {
    if (inside(p, layer_x, layer_y + layer_height * static_cast<double>(i), layer_width,
               layer_height)) {
      return i;
    }
  }
  return std::nullopt;
}

bool EditorScreen::in_layer_list(Vec2 p) const {
  return inside(p, layer_x, layer_y, layer_width, layer_height * 3);
}

std::vector<EditorScreen::Point> EditorScreen::tiles_in(TileRect rect) {
  std::vector<Point> tiles;
  tiles.reserve(static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height));
  for (int x = rect.x; x < rect.x + rect.width; ++x) {
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
      tiles.push_back({x, y});
    }
  }
  return tiles;
}

void EditorScreen::mark_dirty() {
  dirty_ = true;
}

const Image* EditorScreen::sprite(SpriteKind kind, int type) {
  if (!art_ || type < 0) {
    return nullptr;
  }
  const auto key = (static_cast<std::uint64_t>(kind) << 32U) | static_cast<std::uint32_t>(type);
  auto [found, inserted] = sprites_.try_emplace(key);
  if (inserted) {
    const auto id = std::string(kind == SpriteKind::ground    ? "editorground"
                                : kind == SpriteKind::object  ? "editorobject"
                                                              : "editorelement") +
                    std::to_string(type);
    if (art_->contains(id)) {
      found->second = art_->image(id);
    }
  }
  return found->second ? &*found->second : nullptr;
}

const Image& EditorScreen::element_art(std::size_t index) {
  auto& chooser = current_chooser();
  auto& slot = chooser.art[index];
  if (slot) {
    return *slot;
  }
  const auto& choice = document_.choices(chooser.layer)[index];
  Image art = ui_image(element_size, element_size, scale_);
  const double s = scale_;
  if (chooser.layer == EditorLayer::ground) {
    // GroundElement: the ground texture on a 45 degree square, 30 px a side, at the element center.
    const auto* texture = sprite(SpriteKind::ground, choice.type);
    const auto solid = premultiplied(hash_color(choice.id, 0x4e7950), 255);
    constexpr double half = 15, center = element_size / 2;
    const double root_half = std::sqrt(.5);
    for (int py = 0; py < art.height; ++py) {
      for (int px = 0; px < art.width; ++px) {
        const double x = (px + .5) / s - center, y = (py + .5) / s - center;
        const double a = (x + y) * root_half, b = (y - x) * root_half;
        if (std::abs(a) > half || std::abs(b) > half) {
          continue;
        }
        const unsigned char* source =
            texture ? texel(*texture, (a + half) / (2 * half), (b + half) / (2 * half))
                    : solid.data();
        over(&art.pixels[(static_cast<std::size_t>(py) * art.width + px) * 4], source);
      }
    }
  } else if (chooser.layer == EditorLayer::object) {
    // ObjectElement: the redrawn object texture scaled to 46 px on its longer side, centered.
    // An invisible object's redrawn texture is empty, so its element shows only the background.
    const auto* texture = sprite(SpriteKind::element, choice.type);
    if (texture && texture->width > 0 && texture->height > 0) {
      const double k = (element_size - 4) / std::max(texture->width, texture->height);
      Image scaled = *texture;
      scaled.pixel_scale = 1 / k;
      ui_blit(art, scaled, (element_size - texture->width * k) / 2,
              (element_size - texture->height * k) / 2, true, true);
    } else if (!texture) {
      paint_rectangle(art, 2, 2, element_size - 4, element_size - 4,
                      premultiplied(hash_color(choice.id, 0xb87842), 255));
    }
  } else {
    // RegionElement: a 42 px square of the region color at half alpha.
    paint_rectangle(art, 4, 4, element_size - 8, element_size - 8,
                    premultiplied(choice.color & 0xffffffU, region_element_alpha));
  }
  slot = std::move(art);
  return *slot;
}

void EditorScreen::clear_art() {
  for (auto& chooser : choosers_) {
    for (auto& slot : chooser.art) {
      slot.reset();
    }
  }
}

void EditorScreen::build_map() {
  // MEMap.draw: the checker behind the tiles, then each visible tile's ground, object and region
  // at 16 px per tile, nearest-neighbour scaled by zoom.
  const MapKey key{document_.revision(), origin_.x, origin_.y, zoom_, scale_};
  if (map_key_ && *map_key_ == key) {
    return;
  }
  map_key_ = key;
  map_ = ui_image(canvas_size, canvas_size, scale_);
  if (checker_.width > 0 && checker_.height > 0) {
    for (int y = 0; y < map_.height; ++y) {
      const int ly = static_cast<int>((y + .5) / scale_) % checker_.height;
      for (int x = 0; x < map_.width; ++x) {
        const int lx = static_cast<int>((x + .5) / scale_) % checker_.width;
        const auto* source =
            &checker_.pixels[(static_cast<std::size_t>(ly) * checker_.width + lx) * 4];
        std::copy(source, source + 4,
                  &map_.pixels[(static_cast<std::size_t>(y) * map_.width + x) * 4]);
      }
    }
  }
  const double tile = 16 * zoom_;
  const int right = std::min(MapDocument::extent, origin_.x + size_tiles() + 1);
  const int bottom = std::min(MapDocument::extent, origin_.y + size_tiles() + 1);
  for (int y = origin_.y; y < bottom; ++y) {
    for (int x = origin_.x; x < right; ++x) {
      draw_tile(x, y, tile);
    }
  }
}

void EditorScreen::draw_tile(int x, int y, double tile) {
  const auto& value = document_.tile(x, y);
  if (value.ids[0].empty() && value.ids[1].empty() && value.ids[2].empty()) {
    return;
  }
  const double left = (x - origin_.x) * tile, top = (y - origin_.y) * tile;
  const double s = scale_;
  const int x0 = std::max(0, static_cast<int>(std::floor(left * s)));
  const int y0 = std::max(0, static_cast<int>(std::floor(top * s)));
  const int x1 = std::min(map_.width, static_cast<int>(std::ceil((left + tile) * s)));
  const int y1 = std::min(map_.height, static_cast<int>(std::ceil((top + tile) * s)));
  if (x0 >= x1 || y0 >= y1) {
    return;
  }

  // Ground, then object (the unknown-texture fallback is a solid color), then the region tint.
  const Image* ground = nullptr;
  const Image* object = nullptr;
  std::array<unsigned char, 4> ground_color{}, object_color{}, region_color{};
  bool has_ground = false, has_object = false, has_region = false;
  if (!value.ids[0].empty()) {
    const auto* definition = catalog_.ground(value.ids[0]);
    ground = definition ? sprite(SpriteKind::ground, definition->type) : nullptr;
    ground_color = premultiplied(hash_color(value.ids[0], 0x4e7950), 255);
    has_ground = true;
  }
  if (!value.ids[1].empty()) {
    const auto* definition = catalog_.object(value.ids[1]);
    object = definition ? sprite(SpriteKind::object, definition->type) : nullptr;
    object_color = premultiplied(
        hash_color(value.ids[1], definition ? definition->color : 0xb87842), 255);
    has_object = true;
  }
  if (!value.ids[2].empty()) {
    const auto found = region_colors_.find(value.ids[2]);
    region_color = premultiplied(
        found == region_colors_.end() ? hash_color(value.ids[2], 0x7d5988) : found->second,
        region_tile_alpha);
    has_region = true;
  }

  for (int py = y0; py < y1; ++py) {
    const double v = ((py + .5) / s - top) / tile;
    if (v < 0 || v >= 1) {
      continue;
    }
    for (int px = x0; px < x1; ++px) {
      const double u = ((px + .5) / s - left) / tile;
      if (u < 0 || u >= 1) {
        continue;
      }
      auto* destination = &map_.pixels[(static_cast<std::size_t>(py) * map_.width + px) * 4];
      if (ground) {
        over(destination, texel(*ground, u, v));
      } else if (has_ground) {
        over(destination, ground_color.data());
      }
      if (object) {
        over(destination, texel(*object, u, v));
      } else if (has_object) {
        over(destination, object_color.data());
      }
      if (has_region) {
        over(destination, region_color.data());
      }
    }
  }
}

void EditorScreen::select_layer(EditorLayer layer) {
  // Each Chooser keeps its own selection and scroll position, as Flash keeps each chooser object.
  // Changing the chooser removes the element ToolTip.
  layer_ = layer;
  dropdown_open_ = false;
  tooltip_.reset();
  mark_dirty();
}

void EditorScreen::activate(MenuItem item) {
  switch (item) {
    case MenuItem::draw:
      command_ = EditorTool::draw;
      break;
    case MenuItem::erase:
      command_ = EditorTool::erase;
      break;
    case MenuItem::sample:
      command_ = EditorTool::sample;
      break;
    case MenuItem::edit:
      command_ = EditorTool::edit;
      break;
    case MenuItem::undo:
      status_ = document_.undo() ? "Undo." : "Nothing to undo.";
      break;
    case MenuItem::redo:
      status_ = document_.redo() ? "Redo." : "Nothing to redo.";
      break;
    case MenuItem::clear:
      clear_map();
      break;
    case MenuItem::load:
      return start_file_dialog(FileMode::load);
    case MenuItem::save:
      return start_file_dialog(FileMode::save);
    case MenuItem::test:
      cancel_pointer();
      test_request_ = true;
      break;
  }
  mark_dirty();
}

void EditorScreen::clear_map() {
  // Flash onClear: one undoable list, and the save name resets only when something was removed.
  const auto revision = document_.revision();
  document_.clear();
  if (document_.revision() != revision) {
    file_path_ = "map.jm";
  }
  mark_dirty();
}

void EditorScreen::use_file_dialog(std::unique_ptr<platform::FileDialog> dialog) {
  dialog_ = std::move(dialog);
}

void EditorScreen::start_file_dialog(FileMode mode) {
  // Flash onSave: a map with no tiles has no JSON, so Save does nothing and opens no picker.
  if (mode == FileMode::save && !document_.bounds()) {
    return;
  }
  if (!dialog_->available()) {
    return set_status("No system file picker found (install zenity or kdialog).");
  }
  // Flash onLoad/onSave: the picker opens on the remembered file name, which Clear resets to map.jm.
  const bool load = mode == FileMode::load;
  if (!dialog_->start(load ? platform::FileDialog::Mode::open : platform::FileDialog::Mode::save,
                      load ? "Load map" : "Save map", file_path_, "JSON Map (*.jm)", "*.jm")) {
    return;
  }
  dialog_mode_ = mode;
  tooltip_.reset();
  status_ = load ? "Choose a .jm map to load." : "Choose where to save the map.";
  mark_dirty();
}

void EditorScreen::update() {
  if (dialog_mode_ == FileMode::none) {
    return;
  }
  const auto chosen = dialog_->poll();
  if (!chosen) {
    return;
  }
  const auto mode = std::exchange(dialog_mode_, FileMode::none);
  // Cancelling a picker does nothing, as Flash's cancel handlers do.
  if (*chosen) {
    finish_file_dialog(mode, **chosen);
  }
  mark_dirty();
}

void EditorScreen::sample(Point tile) {
  // Flash SAMPLE_COMMAND: an empty tile does nothing; otherwise the chooser selects the tile's
  // object and the command switches to Draw. A type missing from the chooser leaves the selection.
  const auto id = document_.tile(tile.x, tile.y).ids[static_cast<std::size_t>(layer_)];
  if (id.empty()) {
    return;
  }
  const auto& choices = document_.choices(layer_);
  for (std::size_t i = 0; i < choices.size(); ++i) {
    if (choices[i].id == id) {
      current_chooser().selected = i;
      break;
    }
  }
  command_ = EditorTool::draw;
}

void EditorScreen::dispatch(const std::vector<Point>& tiles) {
  // Flash MEMap.TilesEvent -> EditingScreen.onTilesEvent. Each dispatch is one undo step.
  if (tiles.empty()) {
    return;
  }
  if (command_ == EditorTool::edit) {
    return open_edit(tiles);
  }
  if (command_ == EditorTool::sample) {
    sample(tiles.front());
    return mark_dirty();
  }
  const auto& choices = document_.choices(layer_);
  std::string id;
  if (command_ == EditorTool::draw) {
    if (choices.empty()) {
      return mark_dirty();
    }
    id = choices[std::min(current_chooser().selected, choices.size() - 1)].id;
  }
  document_.begin_edit();
  for (const auto& tile : tiles) {
    document_.paint(tile.x, tile.y, layer_, id);
  }
  document_.end_edit();
  mark_dirty();
}

void EditorScreen::open_edit(const std::vector<Point>& tiles) {
  // EditTileProperties(tiles, oldName): the frame opens prefilled with the first tile's name. Nothing
  // changes until Save.
  edit_tiles_ = tiles;
  edit_name_ = document_.tile(tiles.front().x, tiles.front().y).name.value_or("");
  edit_open_ = true;
  tooltip_.reset();
  mark_dirty();
}

void EditorScreen::close_edit(bool save) {
  if (save) {
    // Save applies the name (empty means no name) to every dispatched tile whose name differs, as
    // one undo step. Cancel and Escape leave the map alone.
    const auto name = to_name(edit_name_);
    document_.begin_edit();
    for (const auto& tile : edit_tiles_) {
      if (document_.tile(tile.x, tile.y).name != name) {
        document_.name(tile.x, tile.y, name);
      }
    }
    document_.end_edit();
    status_ = "Object name saved.";
  } else {
    status_ = "Object name cancelled.";
  }
  edit_open_ = false;
  edit_tiles_.clear();
  edit_name_.clear();
  mark_dirty();
}

void EditorScreen::close_error() {
  error_open_ = false;
  mark_dirty();
}

void EditorScreen::finish_file_dialog(FileMode mode, const std::filesystem::path& chosen) {
  try {
    if (mode == FileMode::load) {
      document_.load_file(chosen);
      // Flash onFileLoadComplete: the loaded file's name becomes the default for Save.
      file_path_ = chosen;
      status_ = "Loaded " + chosen.string() + ".";
    } else {
      // Flash names a saved map with the .jm extension; saving leaves the remembered name alone.
      auto path = chosen;
      if (path.extension().empty()) {
        path += ".jm";
      }
      document_.save_file(path);
      status_ = "Saved " + path.string() + ".";
    }
  } catch (const std::exception& error) {
    set_status(error.what());
  }
}

void EditorScreen::set_status(std::string message) {
  status_ = std::move(message);
  if (!status_.empty()) {
    error_message_ = status_;
    error_open_ = true;
    tooltip_.reset();
  }
  mark_dirty();
}

bool EditorScreen::tooltip_visible() const {
  return tooltip_ && pointer_valid_ && modal() == Modal::none;
}

bool EditorScreen::wants_text() const {
  return edit_open_;
}

EditorScreen::Modal EditorScreen::modal() const {
  if (error_open_) {
    return Modal::error;
  }
  if (edit_open_) {
    return Modal::edit;
  }
  return Modal::none;
}

double EditorScreen::modal_height(Modal kind) const {
  // Frame.as: 100 px plus the added components. TextInputField adds 88; the error frame adds its
  // wrapped message.
  if (kind == Modal::error) {
    return 100 + static_cast<double>(lines_of(error_message_, 14, false, message_width).size()) *
                     fonts_.line_advance(14);
  }
  return 100 + TextInputFieldHeight;
}

std::array<std::string_view, 2> EditorScreen::button_labels(Modal kind) const {
  // The left button is omitted when its label is empty, as Frame.as does.
  switch (kind) {
    case Modal::edit:
      return {"Cancel", "Save"};
    case Modal::error:
      return {"", "Ok"};
    case Modal::none:
      break;
  }
  return {"", ""};
}

EditorScreen::Area EditorScreen::button_area(Modal kind, std::size_t index) const {
  // Frame.as: ClickableText at y = h - 52; the left one at x 109, the right one ending 26 px short
  // of the frame's right edge.
  const double height = modal_height(kind);
  const double width = fonts_.measure(button_labels(kind)[index], 18, true) + 4;
  const double x = index == 0 ? frame_x + 109 : frame_x + frame_width - 26 - width;
  return {x, frame_top(height) + height - 52, width, fonts_.line_advance(18, true) + 4};
}

std::optional<std::size_t> EditorScreen::dialog_button_at(Vec2 p) const {
  const auto kind = modal();
  if (kind == Modal::none) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < 2; ++i) {
    if (button_labels(kind)[i].empty()) {
      continue;
    }
    const auto area = button_area(kind, i);
    if (inside(p, area.x, area.y, area.width, area.height)) {
      return i;
    }
  }
  return std::nullopt;
}

void EditorScreen::dialog_action(std::size_t index) {
  switch (modal()) {
    case Modal::error:
      close_error();
      break;
    case Modal::edit:
      close_edit(index == 1);
      break;
    case Modal::none:
      break;
  }
  mark_dirty();
}

void EditorScreen::pointer_down(Vec2 p, int button, bool shift) {
  pointer_ = p;
  pointer_valid_ = true;
  refresh_tooltip();
  if (button != 1) {
    return mark_dirty();
  }
  if (modal() != Modal::none) {
    // A dialog is modal: only its buttons respond, and the dimmed editor below takes no clicks.
    if (const auto index = dialog_button_at(p)) {
      press_ = {Press::Kind::dialog, static_cast<int>(*index)};
    }
    return mark_dirty();
  }
  button_down_ = true;
  if (dropdown_open_) {
    if (const auto item = layer_item_at(p)) {
      press_ = {Press::Kind::layer_item, static_cast<int>(*item)};
      return mark_dirty();
    }
    if (!in_layer_list(p)) {
      dropdown_open_ = false;
    }
  }
  if (const auto item = menu_at(p)) {
    press_ = {Press::Kind::menu, static_cast<int>(*item)};
    return mark_dirty();
  }
  if (!dropdown_open_ && inside(p, layer_x, layer_y, layer_width, layer_height)) {
    press_ = {Press::Kind::layer, 0};
    return mark_dirty();
  }
  auto& chooser = current_chooser();
  if (inside(p, chooser_x + 110, chooser_y + 4, 20, 472)) {
    chooser.scroll.down(scroll_local(p));
    scrolling_ = true;
    return mark_dirty();
  }
  if (const auto index = element_at(p)) {
    chooser.selected = *index;
    return mark_dirty();
  }
  if (in_canvas(p)) {
    if (shift && !rect_anchor_) {
      rect_anchor_ = mouse_tile();
    }
    dispatch(tiles_in(mouse_rect()));
  }
  mark_dirty();
}

void EditorScreen::pointer_move(Vec2 p, bool shift, bool control) {
  pointer_ = p;
  pointer_valid_ = true;
  if (modal() != Modal::none) {
    // Dialogs take the pointer: the map and the chooser underneath do not follow it.
    return mark_dirty();
  }
  if (dropdown_open_ && !in_layer_list(p)) {
    dropdown_open_ = false;
  }
  // MEMap.onMouseMove: Shift and Ctrl anchors follow the modifier state of each move.
  if (!shift) {
    rect_anchor_.reset();
  } else if (!rect_anchor_) {
    rect_anchor_ = mouse_tile();
  }
  if (!control) {
    move_anchor_.reset();
  } else if (!move_anchor_) {
    move_anchor_ = mouse_tile();
  }
  current_chooser().scroll.move(scroll_local(p));
  if (button_down_ && in_canvas(p) && command_ != EditorTool::edit) {
    dispatch({mouse_tile()});
  }
  if (move_anchor_) {
    const auto t = mouse_tile();
    move_view(move_anchor_->x - t.x, move_anchor_->y - t.y);
  }
  refresh_tooltip();
  mark_dirty();
}

void EditorScreen::pointer_up(Vec2 p) {
  pointer_ = p;
  pointer_valid_ = true;
  button_down_ = false;
  if (scrolling_) {
    current_chooser().scroll.up();
    scrolling_ = false;
  }
  const auto press = std::exchange(press_, Press{});
  switch (press.kind) {
    case Press::Kind::dialog:
      if (const auto index = dialog_button_at(p); index && static_cast<int>(*index) == press.index) {
        dialog_action(*index);
      }
      break;
    case Press::Kind::menu:
      if (const auto item = menu_at(p); item && static_cast<int>(*item) == press.index) {
        activate(*item);
      }
      break;
    case Press::Kind::layer:
      if (inside(p, layer_x, layer_y, layer_width, layer_height)) {
        dropdown_open_ = true;
      }
      break;
    case Press::Kind::layer_item:
      if (const auto item = layer_item_at(p); item && static_cast<int>(*item) == press.index) {
        select_layer(static_cast<EditorLayer>(*item));
      }
      break;
    case Press::Kind::none:
      break;
  }
  mark_dirty();
}

void EditorScreen::wheel(Vec2 p, double steps) {
  pointer_ = p;
  pointer_valid_ = true;
  if (modal() != Modal::none) {
    return;
  }
  if (inside(p, chooser_x, chooser_y, chooser_width, chooser_height)) {
    current_chooser().scroll.wheel(steps);
    refresh_tooltip();
    return mark_dirty();
  }
  // MEMap.onMouseWheel: a positive delta zooms in by 2, a negative one out by half.
  if (in_canvas(p) && steps != 0 && can_move()) {
    zoom_by(steps > 0 ? 2 : .5);
  }
  mark_dirty();
}

void EditorScreen::append_text(std::string_view value) {
  // TextInputField: typed text goes to the field of the open dialog.
  if (!edit_open_ || edit_name_.size() + value.size() > name_limit) {
    return;
  }
  edit_name_.append(value);
  mark_dirty();
}

void EditorScreen::backspace() {
  // Backspace removes one whole UTF-8 character from the focused field.
  if (edit_open_) {
    pop_utf8(edit_name_);
  }
  mark_dirty();
}

void EditorScreen::cancel_pointer() {
  button_down_ = false;
  if (scrolling_) {
    current_chooser().scroll.up();
    scrolling_ = false;
  }
  press_ = {};
  mark_dirty();
}

void EditorScreen::refresh_tooltip() {
  // The ToolTip follows the element under the pointer and is removed when the pointer leaves it.
  tooltip_ = pointer_valid_ && modal() == Modal::none ? element_at(pointer_) : std::nullopt;
}

bool EditorScreen::take_test_request() {
  return std::exchange(test_request_, false);
}

bool EditorScreen::take_exit_request() {
  return std::exchange(exit_request_, false);
}

void EditorScreen::key(EditorInputKey key, bool down) {
  // MEMap.onKeyDown/onKeyUp: Shift anchors the rectangle and Ctrl anchors the pan at the pointer.
  if (key == EditorInputKey::shift) {
    if (!down) {
      rect_anchor_.reset();
    } else if (!rect_anchor_ && modal() == Modal::none) {
      rect_anchor_ = mouse_tile();
    }
    return mark_dirty();
  }
  if (key == EditorInputKey::control) {
    if (!down) {
      move_anchor_.reset();
    } else if (!move_anchor_ && modal() == Modal::none) {
      move_anchor_ = mouse_tile();
    }
    return mark_dirty();
  }
  if (!down) {
    return;
  }
  // The open frame takes Enter, Escape and Backspace; Escape with no frame leaves the editor.
  if (key == EditorInputKey::escape) {
    switch (modal()) {
      case Modal::error:
        close_error();
        break;
      case Modal::edit:
        close_edit(false);
        break;
      case Modal::none:
        cancel_pointer();
        exit_request_ = true;
        return;
    }
    return mark_dirty();
  }
  if (key == EditorInputKey::enter) {
    switch (modal()) {
      case Modal::error:
        close_error();
        break;
      case Modal::edit:
        close_edit(true);
        break;
      case Modal::none:
        break;
    }
    return mark_dirty();
  }
  if (key == EditorInputKey::backspace) {
    if (modal() == Modal::edit) {
      backspace();
    }
    return;
  }
  // A dialog traps every other key, so the command hotkeys and the map keys wait for it to close.
  if (modal() != Modal::none) {
    return;
  }
  // Command hotkeys fire on every key down, including OS auto-repeat.
  switch (key) {
    case EditorInputKey::draw:
      return activate(MenuItem::draw);
    case EditorInputKey::erase:
      return activate(MenuItem::erase);
    case EditorInputKey::sample:
      return activate(MenuItem::sample);
    case EditorInputKey::edit:
      return activate(MenuItem::edit);
    case EditorInputKey::undo:
      return activate(MenuItem::undo);
    case EditorInputKey::redo:
      return activate(MenuItem::redo);
    case EditorInputKey::clear:
      return activate(MenuItem::clear);
    case EditorInputKey::load:
      return activate(MenuItem::load);
    case EditorInputKey::save:
      return activate(MenuItem::save);
    case EditorInputKey::test:
      return activate(MenuItem::test);
    case EditorInputKey::left:
      if (can_move()) {
        move_view(-1, 0);
      }
      break;
    case EditorInputKey::right:
      if (can_move()) {
        move_view(1, 0);
      }
      break;
    case EditorInputKey::up:
      if (can_move()) {
        move_view(0, -1);
      }
      break;
    case EditorInputKey::down:
      if (can_move()) {
        move_view(0, 1);
      }
      break;
    case EditorInputKey::zoom_in:
      if (can_move()) {
        zoom_by(2);
      }
      break;
    case EditorInputKey::zoom_out:
      if (can_move()) {
        zoom_by(.5);
      }
      break;
    default:
      break;
  }
  mark_dirty();
}

void EditorScreen::handle(const EditorInput& event) {
  // The system picker takes all input while it is open, as Flash's modal browse does. Key releases
  // and focus loss still reach the editor so the Shift and Ctrl anchors cannot stick.
  if (dialog_mode_ != FileMode::none && event.kind != EditorInput::Kind::key_up &&
      event.kind != EditorInput::Kind::focus_lost) {
    return;
  }
  switch (event.kind) {
    case EditorInput::Kind::pointer_move:
      pointer_move(logical(event.point), event.shift, event.control);
      break;
    case EditorInput::Kind::pointer_down:
      pointer_down(logical(event.point), event.button, event.shift);
      break;
    case EditorInput::Kind::pointer_up:
      pointer_up(logical(event.point));
      break;
    case EditorInput::Kind::wheel:
      wheel(logical(event.point), event.wheel);
      break;
    case EditorInput::Kind::key_down:
      key(event.key, true);
      break;
    case EditorInput::Kind::key_up:
      key(event.key, false);
      break;
    case EditorInput::Kind::text:
      append_text(event.text);
      break;
    case EditorInput::Kind::pointer_left:
      pointer_valid_ = false;
      cancel_pointer();
      tooltip_.reset();
      break;
    case EditorInput::Kind::focus_lost:
      // Losing window focus ends the pointer gestures and the Shift and Ctrl anchors. An open
      // dialog stays open, as a Flash modal does.
      cancel_pointer();
      rect_anchor_.reset();
      move_anchor_.reset();
      tooltip_.reset();
      mark_dirty();
      break;
  }
}

EditorScreen::Area EditorScreen::clipped(double x, double y, double width, double height) const {
  const double left = std::max(x, clip_.x), top = std::max(y, clip_.y);
  const double right = std::min(x + width, clip_.x + clip_.width);
  const double bottom = std::min(y + height, clip_.y + clip_.height);
  return {left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
}

void EditorScreen::fill(double x, double y, double width, double height, std::uint32_t color) {
  const auto area = clipped(x, y, width, height);
  if (area.width > 0 && area.height > 0) {
    ui_rectangle(image_, area.x, area.y, area.width, area.height, color);
  }
}

void EditorScreen::outline(double x, double y, double width, double height, std::uint32_t color,
                           double thickness) {
  fill(x, y, width, thickness, color);
  fill(x, y + height - thickness, width, thickness, color);
  fill(x, y, thickness, height, color);
  fill(x + width - thickness, y, thickness, height, color);
}

void EditorScreen::tint(double x, double y, double width, double height, std::uint32_t color,
                        double alpha) {
  // Straight-alpha source-over of one color at `alpha`, as a Flash fill with alpha.
  const auto area = clipped(x, y, width, height);
  const double s = scale_;
  const int left = std::max(0, static_cast<int>(std::floor(area.x * s)));
  const int top = std::max(0, static_cast<int>(std::floor(area.y * s)));
  const int right = std::min(image_.width, static_cast<int>(std::ceil((area.x + area.width) * s)));
  const int bottom =
      std::min(image_.height, static_cast<int>(std::ceil((area.y + area.height) * s)));
  for (int py = top; py < bottom; ++py) {
    for (int px = left; px < right; ++px) {
      auto* destination =
          &image_.pixels[(static_cast<std::size_t>(py) * image_.width + px) * 4];
      const double destination_alpha = destination[3] / 255.0;
      const double out_alpha = alpha + destination_alpha * (1 - alpha);
      for (unsigned c = 0; c < 3; ++c) {
        const double source = ((color >> (16 - c * 8)) & 255U) / 255.0;
        const double old = destination[c] / 255.0;
        destination[c] = static_cast<unsigned char>(std::lround(
            (source * alpha + old * destination_alpha * (1 - alpha)) / out_alpha * 255));
      }
      destination[3] = static_cast<unsigned char>(std::lround(out_alpha * 255));
    }
  }
}

void EditorScreen::text(std::string_view value, int size, std::uint32_t color, double x, double y,
                        bool bold, bool shadow) {
  const auto bitmap = fonts_.rasterize_at_scale(value, size, scale_, bold, shadow, color);
  ui_blit_text(image_, bitmap, x + bitmap.offset.x, y + bitmap.offset.y, false, false, 1, clip_.x,
               clip_.y, clip_.width,
               clip_.height);
}

void EditorScreen::paragraphs(std::string_view value, double x, double y, double hanging) {
  // InfoPane's CSS: wrapped lines of the typeText span hang 10 px in from its first line.
  const double advance = fonts_.line_advance(12, false);
  double line_y = y;
  std::size_t start = 0;
  while (true) {
    const auto end = value.find('\n', start);
    auto lines = fonts_.wrap(value.substr(start, end == std::string_view::npos ? end : end - start),
                             12, false, 124);
    if (lines.empty()) {
      lines.emplace_back();
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
      text(lines[i], 12, white, x + (i > 0 ? hanging : 0), line_y, false, true);
      line_y += advance;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
}

void EditorScreen::render_map() {
  build_map();
  ui_blit(image_, map_, canvas_x, canvas_y, true, false, 1, clip_.x, clip_.y, clip_.width,
          clip_.height);
  // MEMap.drawOverlay: the mouse rectangle, outlined and tinted, while the pointer is over the map.
  if (over_canvas()) {
    const auto area = mouse_rect();
    const double tile = 16 * zoom_;
    const double x = canvas_x + (area.x - origin_.x) * tile;
    const double y = canvas_y + (area.y - origin_.y) * tile;
    const double width = area.width * tile - 1, height = area.height * tile - 1;
    clip_ = {canvas_x, canvas_y, canvas_size, canvas_size};
    tint(x, y, width, height, white, .1);
    outline(x, y, width, height, white);
    clip_ = {0, 0, 800, 600};
  }
}

void EditorScreen::render_menu() {
  for (std::size_t i = 0; i < menu_slots.size(); ++i) {
    const double x = menu_x, y = menu_y + menu_step * menu_slots[i].row;
    const auto item = static_cast<MenuItem>(i);
    const bool hovered = pointer_valid_ && menu_at(pointer_) == item;
    const bool tool = i < 4 && static_cast<int>(command_) == static_cast<int>(i);
    const bool pressed = hovered && press_.kind == Press::Kind::menu &&
                         press_.index == static_cast<int>(i);
    // CommandMenuItem.redraw: normal 1 px outline; hover 2 px; selected or pressed 2 px with fill.
    if (tool || pressed) {
      fill(x, y, menu_width, menu_height, pressed_fill);
      outline(x, y, menu_width, menu_height, white, 2);
    } else {
      outline(x, y, menu_width, menu_height, white, hovered ? 2 : 1);
    }
    text(menu_slots[i].label, 16, white, x + 2, y, true);
  }
}

void EditorScreen::render_layers() {
  // DropDown: the closed item shows the layer; opening it lists all three.
  const auto item = [&](double y, std::size_t index) {
    const auto name = layer_names[index];
    const bool hovered = pointer_valid_ && inside(pointer_, layer_x, y, layer_width, layer_height);
    fill(layer_x, y, layer_width, layer_height, hovered ? hover_fill : panel);
    outline(layer_x, y, layer_width, layer_height, grey);
    const double width = fonts_.measure(name, 16, true);
    const double height = fonts_.layout(name, 16, true).height;
    text(name, 16, grey, layer_x + (layer_width - width) / 2, y + (layer_height - height) / 2,
         true, true);
  };
  if (!dropdown_open_) {
    item(layer_y, static_cast<std::size_t>(layer_));
    return;
  }
  for (std::size_t i = 0; i < layer_names.size(); ++i) {
    item(layer_y + layer_height * static_cast<double>(i), i);
  }
}

void EditorScreen::render_chooser() {
  // Chooser.as: a cut-edge panel with the elements masked to (0, 2, 112, 476) chooser-local.
  ui_path(image_, cut_edge_rect(chooser_x, chooser_y, chooser_width, chooser_height, 4), panel,
          white, 1);
  auto& chooser = current_chooser();
  const double offset = scroll_offset(chooser);
  const auto hovered = pointer_valid_ ? element_at(pointer_) : std::nullopt;
  const Area mask{chooser_x, chooser_y + 2, 112, 476};
  clip_ = {mask.x, mask.y, mask.width, mask.height};
  const std::size_t count = chooser.art.size();
  const double top = chooser_y + 12 - offset;
  const auto first = static_cast<std::size_t>(
      std::max(0.0, std::floor((mask.y - top) / element_size)));
  for (std::size_t row = first; row * 2 < count; ++row) {
    const double y = top + static_cast<double>(row) * element_size;
    if (y >= mask.y + mask.height) {
      break;
    }
    for (std::size_t column = 0; column < 2; ++column) {
      const std::size_t index = row * 2 + column;
      if (index >= count) {
        break;
      }
      const double x = chooser_x + 4 + (column ? 52 : 0);
      fill(x + 2, y + 2, 46, 46, hovered && *hovered == index ? hover_fill : panel);
      ui_blit(image_, element_art(index), x, y, true, false, 1, clip_.x, clip_.y, clip_.width,
              clip_.height);
      if (chooser.selected == index) {
        outline(x + 2, y + 2, 46, 46, white);
      }
    }
  }
  clip_ = {0, 0, 800, 600};
  chooser.scroll.draw(image_, {chooser_x + 110, chooser_y + 4});
}

void EditorScreen::render_info() {
  // InfoPane.as: the pointer's tile position and rectangle, then the tile's layers.
  ui_path(image_, cut_edge_rect(info_x, info_y, info_width, info_height, 4), panel, white, 1);
  const auto area = mouse_rect();
  std::string position = "Position: " + std::to_string(area.x) + ", " + std::to_string(area.y);
  if (area.width > 1 || area.height > 1) {
    position += "\nRect: " + std::to_string(area.width) + ", " + std::to_string(area.height);
  }
  paragraphs(position, info_x + 4, info_y + 4, 0);
  const auto& tile = document_.tile(area.x, area.y);
  const auto or_none = [](const std::string& id) { return id.empty() ? std::string("None") : id; };
  std::string object = or_none(tile.ids[1]);
  if (tile.name) {
    object += " (" + *tile.name + ")";
  }
  paragraphs("Ground: " + or_none(tile.ids[0]) + "\nObject: " + object +
                 "\nRegion: " + or_none(tile.ids[2]),
             info_x + 4, info_y + 36, 10);
}

std::optional<EditorScreen::ElementTip> EditorScreen::element_tip(std::size_t index) const {
  // GroundTypeToolTip, ObjectTypeToolTip and RegionTypeToolTip: the title is the type id.
  const auto& choice = document_.choices(current_chooser().layer)[index];
  switch (current_chooser().layer) {
    case EditorLayer::ground: {
      const auto* definition = catalog_.ground(choice.id);
      return ElementTip{choice.id, ground_body(definition ? definition->xml : Xml{}), true};
    }
    case EditorLayer::object: {
      const auto* definition = catalog_.object(choice.id);
      return ElementTip{choice.id, object_body(definition ? definition->xml : Xml{}), true};
    }
    case EditorLayer::region:
      return ElementTip{choice.id, {}, false};
  }
  return std::nullopt;
}

std::vector<std::string> EditorScreen::lines_of(std::string_view value, int size, bool bold,
                                                int width) const {
  // A TextField's paragraphs wrap separately; a paragraph that wraps to nothing is still one line.
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (true) {
    const auto end = value.find('\n', start);
    auto wrapped = fonts_.wrap(value.substr(start, end == std::string_view::npos ? end : end - start),
                               size, bold, width);
    if (wrapped.empty()) {
      wrapped.emplace_back();
    }
    lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return lines;
}

void EditorScreen::render_modal() {
  const auto kind = modal();
  if (kind == Modal::none) {
    return;
  }
  // EditTileProperties.darkBox_: black at .8 over the whole design area.
  tint(0, 0, 800, 600, 0, .8);

  // Frame.as draws the body and the title bar, then the body outline again. The whole frame casts a
  // DropShadowFilter(0, 0, 0, .5, 12, 12), so the frame is drawn into its own layer first.
  const double height = modal_height(kind);
  const double x = frame_x, y = frame_top(height);
  Image layer = ui_image(800, 600, scale_);
  std::swap(image_, layer);
  ui_path(image_, cut_edge_rect(x - 6, y - 6, frame_width, height, 4), panel);
  ui_path(image_, cut_edge_rect(x - 6, y - 6, frame_width, 32, 4, {true, true, false, false}),
          title_fill);
  ui_path(image_, cut_edge_rect(x - 6, y - 6, frame_width, height, 4), std::nullopt, white, 1);
  const std::string_view title = kind == Modal::error ? "Map editor" : "Tile properties";
  text(title, 12, grey, x + 5, y, false, true);

  if (kind == Modal::error) {
    // The message is 14 pt grey text at frame (17, 40), wrapped to 254 px.
    double line_y = y + 40;
    for (const auto& line : lines_of(error_message_, 14, false, message_width)) {
      text(line, 14, grey, x + 17, line_y, false, true);
      line_y += fonts_.line_advance(14);
    }
  } else {
    // TextInputField at frame (17, 40): a 18 pt bold label, then the 238 x 30 box with its text.
    const double field_x = x + 17, field_y = y + 40;
    const double box_x = field_x, box_y = field_y + 30;
    text("Object Name", 18, grey, field_x, field_y, true, true);
    fill(box_x, box_y, input_width, 30, input_fill);
    outline(box_x - 1, box_y - 1, input_width + 2, 32, input_stroke, 2);
    const double value_width = fonts_.measure(edit_name_, 20, false);
    // The field scrolls so the caret at the end stays visible.
    const double scroll = std::min(0.0, input_text_room - value_width);
    clip_ = {box_x, box_y, input_width, 30};
    text(edit_name_, 20, grey, box_x + input_text_x + scroll, box_y);
    fill(box_x + input_text_x + scroll + value_width, box_y + 4, 1, 22, grey);
    clip_ = {0, 0, 800, 600};
  }

  // ClickableText buttons: hover recolors the text; the Cancel and Ok labels are left or right.
  const auto labels = button_labels(kind);
  for (std::size_t i = 0; i < labels.size(); ++i) {
    if (labels[i].empty()) {
      continue;
    }
    const auto area = button_area(kind, i);
    const bool hovered = pointer_valid_ && dialog_button_at(pointer_) == i;
    text(labels[i], 18, hovered ? hover_text : white, area.x, area.y, true, true);
  }
  std::swap(image_, layer);

  // Frame's drop shadow: a black blur of the frame at half alpha, then the frame over it.
  Image shadow = layer;
  for (std::size_t i = 0; i < shadow.pixels.size(); i += 4) {
    shadow.pixels[i] = shadow.pixels[i + 1] = shadow.pixels[i + 2] = 0;
  }
  ui_shadow(shadow, 6, 800, 600);
  for (std::size_t i = 3; i < shadow.pixels.size(); i += 4) {
    shadow.pixels[i] = static_cast<unsigned char>(shadow.pixels[i] / 2);
  }
  ui_blit(image_, shadow, 0, 0, false, false);
  ui_blit(image_, layer, 0, 0, false, false);
}

void EditorScreen::render_tooltip() {
  if (!tooltip_visible()) {
    return;
  }
  const auto tip = element_tip(*tooltip_);
  if (!tip) {
    return;
  }
  // ToolTip.as with the element's type tip: the title is 16 pt bold in 176 px, the description 14 pt
  // grey in 180 px, two pixels below the title. The box is the content plus six on each side.
  const auto title_lines = lines_of(tip->title, 16, true, 176);
  const auto body_lines =
      tip->has_body ? lines_of(tip->body, 14, false, 180) : std::vector<std::string>{};
  const double title_height = static_cast<double>(title_lines.size()) * fonts_.line_advance(16, true) + 4;
  const double body_y = title_height + 2;
  const double body_height = static_cast<double>(body_lines.size()) * fonts_.line_advance(14) + 4;
  const double content_width = tip->has_body ? 180 : 176;
  const double content_height = tip->has_body ? body_y + body_height : title_height;
  const double box_width = content_width + 12, box_height = content_height + 12;
  const auto at = text_tooltip_position(logical(pointer_), box_width, box_height, 800, 600, 1);

  // The box is drawn at a 12 px margin so its shadow (DropShadowFilter(0, 0, 0, 1, 16, 16)) fits.
  constexpr double margin = 12, padding = 6;
  Image box = ui_image(box_width + margin * 2, box_height + margin * 2, scale_);
  ui_path(box, cut_edge_rect(margin, margin, box_width, box_height, 4), tooltip_fill,
          tooltip_outline, 1);
  const auto blit_line = [&](std::string_view value, int size, std::uint32_t color, double line_x,
                             double line_y, bool bold) {
    if (value.empty()) {
      return;
    }
    const auto bitmap = fonts_.rasterize_at_scale(value, size, scale_, bold, true, color);
    ui_blit_text(box, bitmap, line_x + bitmap.offset.x, line_y + bitmap.offset.y, false, true);
  };
  double line_y = margin + padding;
  for (const auto& line : title_lines) {
    blit_line(line, 16, white, margin + padding, line_y, true);
    line_y += fonts_.line_advance(16, true);
  }
  line_y = margin + padding + body_y;
  for (const auto& line : body_lines) {
    blit_line(line, 14, grey, margin + padding, line_y, false);
    line_y += fonts_.line_advance(14);
  }
  ui_shadow(box, 8, box.width / scale_, box.height / scale_);
  ui_blit(image_, box, at.x - margin, at.y - margin, false, false);
}

void EditorScreen::render() {
  image_ = ui_image(800, 600, scale_);
  render_map();
  render_menu();
  render_chooser();
  render_layers();
  render_info();
  render_modal();
  render_tooltip();
  dirty_ = false;
  ++image_revision_;
}

const Image& EditorScreen::image() {
  if (dirty_) {
    render();
  }
  return image_;
}

}  // namespace realm
