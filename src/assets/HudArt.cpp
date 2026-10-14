#include "realm/assets/HudArt.hpp"
#include "realm/util/FlashDraw.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
namespace realm {
namespace fd = flash_draw;
namespace {
constexpr std::size_t cache_limit = 256, cache_byte_limit = 8 * 1024 * 1024;
constexpr std::uint32_t text_gray = 11776947, panel_gray = 3552822, tile_gray = 5526612, equipment_gray = 4539717,
                        restricted_red = 6036765;
constexpr double panel_width = 188, panel_height = 84;
// GraphicsUtil cut corners of the equipment, inventory and trade grids.
constexpr std::array<std::array<bool, 4>, 4> equipment_cuts{{{1, 0, 0, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 1, 1, 0}}};
constexpr std::array<std::array<bool, 4>, 8> inventory_cuts{
    {{1, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, 0}}};
constexpr std::array<std::array<bool, 4>, 12> trade_cuts{{{1, 0, 0, 1},
                                                          {0, 0, 0, 0},
                                                          {0, 0, 0, 0},
                                                          {0, 1, 1, 0},
                                                          {1, 0, 0, 0},
                                                          {0, 0, 0, 0},
                                                          {0, 0, 0, 0},
                                                          {0, 1, 0, 0},
                                                          {0, 0, 0, 1},
                                                          {0, 0, 0, 0},
                                                          {0, 0, 0, 0},
                                                          {0, 0, 1, 0}}};
constexpr std::array<std::array<bool, 4>, 2> potion_cuts{{{1, 0, 0, 1}, {0, 1, 1, 0}}};
struct SheetIndex {
  std::string_view sheet;
  int index{-1};
};
// EquipmentTile.setType / Slot.drawBackground slot-type art (Slot lacks the katana and shuriken entries).
SheetIndex slot_art(int type, bool equipment_tile) {
  switch (type) {
    case 1:
      return {"lofiObj5", 48};
    case 2:
      return {"lofiObj5", 96};
    case 3:
      return {"lofiObj5", 80};
    case 4:
      return {"lofiObj6", 80};
    case 5:
      return {"lofiObj6", 112};
    case 6:
      return {"lofiObj5", 0};
    case 7:
      return {"lofiObj5", 32};
    case 8:
      return {"lofiObj5", 64};
    case 9:
      return {"lofiObj", 44};
    case 11:
      return {"lofiObj6", 64};
    case 12:
      return {"lofiObj6", 160};
    case 13:
      return {"lofiObj6", 32};
    case 14:
      return {"lofiObj5", 16};
    case 15:
      return {"lofiObj6", 48};
    case 16:
      return {"lofiObj6", 96};
    case 17:
      return {"lofiObj5", 112};
    case 18:
      return {"lofiObj6", 128};
    case 19:
      return {"lofiObj6", 0};
    case 20:
      return {"lofiObj6", 16};
    case 21:
      return {"lofiObj6", 144};
    case 22:
      return {"lofiObj6", 176};
    case 23:
      return {"lofiObj6", 192};
    case 24:
      return equipment_tile ? SheetIndex{"lofiObj3", 540} : SheetIndex{};
    case 25:
      return equipment_tile ? SheetIndex{"lofiObj3", 555} : SheetIndex{};
    default:
      return {};
  }
}
bool number(std::string_view text, int& value) {
  if (text.empty()) return false;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
// Parses `prefix` followed by an integer in [low, high].
bool keyed(std::string_view key, std::string_view prefix, int low, int high, int& value) {
  return key.starts_with(prefix) && number(key.substr(prefix.size()), value) && value >= low && value <= high;
}
void premultiply(Image& image) { premultiply_flash_bitmap(image); }
Image crop_image(const Image& source, int x, int y, int width, int height) {
  auto result = fd::canvas(std::max(0, width), std::max(0, height));
  fd::draw(result, source, -x, -y);
  return result;
}
// SimpleText field metrics: updateMetrics sizes are the truncated line width/height plus the 4 pixel gutters.
struct Field {
  double width{}, height{}, text_width{}, text_height{};
};
}
struct HudArt::Sources {
  FontBook fonts;
  std::unique_ptr<Catalog> catalog_owner;
  std::unique_ptr<AnimationLibrary> animations_owner;
  std::unique_ptr<Appearances> appearances_owner;
  const Catalog* catalog{};
  const Appearances* appearances{};
  std::unordered_map<int, Image> sheets;
  std::optional<fd::Shape> star, cross, box;
  int classes{};
  Sources(const Appearances* shared_appearances, const Catalog* shared_catalog) {
    const std::filesystem::path root(flash::source_root);
    if (shared_catalog)
      catalog = shared_catalog;
    else {
      catalog_owner = std::make_unique<Catalog>(root);
      catalog = catalog_owner.get();
    }
    if (shared_appearances)
      appearances = shared_appearances;
    else {
      animations_owner = std::make_unique<AnimationLibrary>(root);
      appearances_owner = std::make_unique<Appearances>(root, *animations_owner);
      appearances = appearances_owner.get();
    }
    for (const auto& object : catalog->objects()) classes += object.player;
  }
  // AssetLibrary.getImageFromSet: one frame of a named image set, straight alpha.
  Image sprite(std::string_view name, int index) {
    for (const auto& sheet : flash::sheets) {
      if (sheet.name != name || sheet.asset < 0) continue;
      auto& image = sheets[sheet.asset];
      if (image.pixels.empty()) {
        image = load_png(std::filesystem::path(flash::source_root) /
                         flash::assets.at(static_cast<std::size_t>(sheet.asset)).path);
        normalize_flash_png(image);
      }
      const int columns = image.width / sheet.width;
      if (index < 0 || columns <= 0 || index >= columns * (image.height / sheet.height))
        throw std::runtime_error("Image set index out of range");
      return crop(image, index % columns * sheet.width, index / columns * sheet.height, sheet.width, sheet.height);
    }
    throw std::runtime_error("Unknown image set " + std::string(name));
  }
  // TextureRedrawer.redraw, premultiplied: the resized outlined texture with GlowRedrawer's
  // GlowFilter(0, .3, 12, 12, 2) drawn over the outline.
  Image redraw(const Image& source, const Image* mask, int size, bool pad, std::uint32_t tex1, std::uint32_t tex2,
               bool glow = true) const {
    auto image = appearances->compose(source, mask, size, pad, tex1, tex2, false);
    image.pixel_scale = 1;
    premultiply(image);
    if (glow) fd::glow(image, 0, .3, 12, 12, 2);
    return image;
  }
  Image redraw(const Image& source, int size, bool pad, bool glow = true) const {
    return redraw(source, nullptr, size, pad, 0, 0, glow);
  }
  // One animation frame redrawn, with its dye mask.
  Image frame_texture(AnimationFrame frame, int size, bool pad, std::uint32_t tex1, std::uint32_t tex2) const {
    const auto& images = appearances->animations().images();
    auto source = images.at(frame.image);
    normalize_flash_png(source);
    Image mask;
    if (frame.mask >= 0) {
      mask = images.at(static_cast<std::size_t>(frame.mask));
      normalize_flash_png(mask);
    }
    return redraw(source, frame.mask >= 0 ? &mask : nullptr, size, pad, tex1, tex2);
  }
  // GameObject.getPortrait: the right-facing standing frame at four times its width.
  Image portrait(int type, int skin, std::uint32_t tex1, std::uint32_t tex2) {
    const auto& t = catalog->player_texture(type, skin).select(0);
    if (const auto* character = appearances->animations().character(t.animated, t.index)) {
      const auto frame = character->directions[0][0].front();
      return frame_texture(frame, 400 / appearances->animations().images().at(frame.image).width, true, tex1, tex2);
    }
    return object_texture(type, 50, true);
  }
  // ObjectLibrary.getRedrawnTextureFromType, premultiplied.
  Image object_texture(int type, int size, bool pad) {
    const auto* object = catalog->object(static_cast<std::uint16_t>(type));
    if (!object) throw std::runtime_error("Unknown object type");
    const auto& t = object->texture.select(0);
    if (const auto* character = appearances->animations().character(t.animated, t.index)) {
      return frame_texture(character->frame(0, 0, Action::stand, 0), size, pad, object->tex1, object->tex2);
    }
    if (t.sheet < 0 || static_cast<std::size_t>(t.sheet) >= flash::sheets.size())
      return redraw(sprite("lofiObj3", 255), size, pad);
    const auto source = sprite(flash::sheets[static_cast<std::size_t>(t.sheet)].name, t.index);
    Image mask;
    if (t.mask_sheet >= 0 && static_cast<std::size_t>(t.mask_sheet) < flash::sheets.size())
      mask = sprite(flash::sheets[static_cast<std::size_t>(t.mask_sheet)].name, t.mask_index);
    return redraw(source, mask.pixels.empty() ? nullptr : &mask, size, pad, object->tex1, object->tex2);
  }
  const fd::Shape& star_shape() {
    if (!star)
      star = fd::load_swf_shape(std::filesystem::path(flash::source_root) /
                                "src/com/company/rotmg/graphics/StarGraphic.swf");
    return *star;
  }
  Image rank_star_icon(int stars, double scale) {
    const auto& shape = star_shape();
    const int margin = 8;
    auto image = fd::canvas(static_cast<int>(std::ceil(shape.width() * scale)) + margin * 2,
                            static_cast<int>(std::ceil(shape.height() * scale)) + margin * 2);
    fd::draw_shape(image, shape, margin - shape.left * scale, margin - shape.top * scale, scale, star_color(stars));
    fd::drop_shadow(image, 0, 0, 0, 1, 4, 4, 2);
    return image;
  }
  const fd::Shape& cross_shape() {
    if (!cross)
      cross = fd::load_swf_shape(std::filesystem::path(flash::source_root) /
                                 "src/com/company/rotmg/graphics/DeleteXGraphic.swf");
    return *cross;
  }
  const fd::Shape& box_shape() {
    if (!box)
      box = fd::load_swf_shape(std::filesystem::path(flash::source_root) /
                               "src/com/company/rotmg/graphics/FullCharBoxGraphic.swf");
    return *box;
  }
  Field field(std::string_view text, int size, bool bold) const {
    const auto run = fonts.layout(text, size, bold);
    return {run.width, run.height, fonts.measure(text, size, bold), fonts.line_advance(size, bold)};
  }
  // A SimpleText at (x, y); shadow is the strength of a DropShadowFilter(0,0,0,1,4,4,strength), or 0 for none.
  void text(Image& canvas, std::string_view value, int size, bool bold, std::uint32_t color, double x, double y,
            unsigned shadow = 0) const {
    // BitmapData.draw places glyphs on the twentieth-pixel grid horizontally and the quarter-pixel grid vertically.
    const double px = std::round(x * 20) / 20, py = std::floor(y * 4) / 4, fx = std::floor(px), fy = std::floor(py);
    auto bitmap = fonts.rasterize(value, size, bold, shadow > 0, color, px - fx, py - fy, std::max(1U, shadow));
    premultiply(bitmap.image);
    fd::draw(canvas, bitmap.image, px + bitmap.offset.x, fy + bitmap.offset.y);
  }
  // A fixed-width SimpleText with one centered paragraph.
  void centered(Image& canvas, std::string_view value, int size, bool bold, std::uint32_t color, int width, double x,
                double y, unsigned shadow) const {
    auto bitmap = fonts.rasterize_centered(value, size, bold, shadow > 0, color, width, std::max(1U, shadow));
    premultiply(bitmap.image);
    fd::draw(canvas, bitmap.image, std::round(x) + bitmap.offset.x, std::floor(y) + bitmap.offset.y);
  }
  // FameUtil.numStarsToImage: the star symbol tinted by rank.
  fd::ColorTransform star_color(int stars) const {
    static constexpr std::array<std::array<double, 3>, 5> colors{
        {{138, 152, 222}, {49, 77, 219}, {193, 39, 45}, {247, 147, 30}, {255, 255, 0}}};
    const int tier = classes > 0 ? stars / classes : 0;
    if (tier < 0 || tier > 4) return {};
    const auto& c = colors[static_cast<std::size_t>(tier)];
    return {c[0] / 255, c[1] / 255, c[2] / 255};
  }
  // RankText(stars, false, include_prefix) at (x, y). Returns its bounds relative to (x, y).
  struct Bounds {
    double left{}, top{}, right{}, bottom{};
  };
  Bounds rank(Image* canvas, int stars, bool prefix, double x, double y) {
    Bounds bounds;
    double sx = 0;
    if (prefix) {
      const auto label = field("Rank: ", 16, false);
      if (canvas) text(*canvas, "Rank: ", 16, false, text_gray, x, y, 1);
      bounds.right = label.width;
      bounds.bottom = label.height;
      sx = label.width;
    }
    if (stars < 0) return bounds;
    const auto value = std::to_string(stars);
    const auto number = field(value, 16, false);
    const auto& shape = star_shape();
    const double icon_x = number.width + 2, icon_y = std::trunc(number.height / 2 - shape.height() / 2) + 1;
    const double w = std::trunc(icon_x + shape.width());
    if (canvas) {
      fd::Path background;
      background.round_rect(x + sx - 2, y + icon_y - 3, w + 6, shape.height() + 8, 12);
      fd::fill(*canvas, background, 0, .4);
      text(*canvas, value, 16, false, text_gray, x + sx, y, 2);
      fd::draw_shape(*canvas, shape, x + sx + icon_x - shape.left, y + icon_y - shape.top, 1, star_color(stars));
    }
    bounds.left = std::min(bounds.left, sx - 2);
    bounds.top = std::min(bounds.top, icon_y - 3);
    bounds.right = std::max({bounds.right, sx + number.width, sx + w + 4});
    bounds.bottom = std::max({bounds.bottom, number.height, icon_y + shape.height() + 5});
    return bounds;
  }
  // TextButton(16, label) with its top-left corner at (x, y), filled in `fill`.
  void button(Image& canvas, std::string_view label, double x, double y, std::uint32_t fill) const {
    const auto f = field(label, 16, true);
    const double w = f.width + 12;
    fd::Path shape;
    shape.cut_edge_rect(x, y, w, std::floor(f.text_height) + 8, 4);
    fd::fill(canvas, shape, fill);
    text(canvas, label, 16, true, panel_gray, x + w / 2 - f.text_width / 2 - 2, y + 1);
  }
  // A Panel's TextButton centered at the bottom, in an InteractPanel image whose panel origin is (6, 8).
  void panel_button(Image& canvas, std::string_view label, bool hover) const {
    const auto f = field(label, 16, true);
    const double w = f.width + 12, h = std::floor(f.text_height) + 8;
    button(canvas, label, 6 + panel_width / 2 - w / 2, 8 + panel_height - h - 4, hover ? 16768133 : 16777215);
  }
  // Slot / EquipmentTile background art: the slot-type image at 4x in the panel gray, or a hotkey number.
  void slot_background(Image& canvas, int type, int hotkey, bool equipment_tile, Vec2 offset) {
    const auto art = slot_art(type, equipment_tile);
    if (art.index >= 0) {
      auto image = sprite(art.sheet, art.index);
      premultiply(image);
      fd::color_matrix(image, {0, 0, 0, 0, 54, 0, 0, 0, 0, 54, 0, 0, 0, 0, 54, 0, 0, 0, 1, 0});
      double dx = 0, dy = 0;
      if (!equipment_tile && type == 9) {
        dx = -2;
        dy = -2;
      } else if (!equipment_tile && type == 11)
        dy = -2;
      fd::draw(canvas, image, offset.x + 4 + dx, offset.y + 4 + dy, 4, 4);
    } else if (hotkey > 0) {
      const auto value = std::to_string(hotkey);
      const auto f = field(value, 26, true);
      // The number is drawn into a 26x30 bitmap, which snaps to the nearest pixel.
      auto bitmap = fd::canvas(26, 30);
      text(bitmap, value, 26, true, panel_gray, 0, 0);
      fd::draw(canvas, bitmap, offset.x + std::round(20 - f.width / 2), offset.y + 2);
    }
  }
};
HudArt::HudArt() = default;
HudArt::~HudArt() = default;
void HudArt::appearances(const Appearances* value, const Catalog* catalog) {
  if (sources_) throw std::logic_error("HUD art sources are already loaded");
  shared_appearances_ = value;
  shared_catalog_ = catalog;
}
HudArt::Sources& HudArt::sources() const {
  if (!sources_) sources_ = std::make_unique<Sources>(shared_appearances_, shared_catalog_);
  return *sources_;
}
std::optional<Image> HudArt::make(const std::string& key, bool dry) const {
  auto& s = sources();
  const auto& catalog = *s.catalog;
  int a{}, b{};
  const auto object = [&](int type) {
    return type > 0 && type <= 65535 ? catalog.object(static_cast<std::uint16_t>(type)) : nullptr;
  };
  const auto equipment = [&](int type) {
    const auto* o = object(type);
    return o && o->class_name == "Equipment" ? o : nullptr;
  };
  // Map-editor previews use the original unoutlined sprite so tile thumbnails retain their useful area.
  if (keyed(key, "editorground", 0, 65535, a) || keyed(key, "editorobject", 0, 65535, a)) {
    const bool ground = key.starts_with("editorground");
    const auto* ground_def = ground ? catalog.ground(static_cast<std::uint16_t>(a)) : nullptr;
    const auto* object_def = ground ? nullptr : catalog.object(static_cast<std::uint16_t>(a));
    if (!ground_def && !object_def) return std::nullopt;
    if (dry) return Image{};
    const auto& texture = (ground ? ground_def->texture : object_def->texture).select(0);
    Image image;
    if (const auto* character = s.appearances->animations().character(texture.animated, texture.index)) {
      image = s.appearances->animations().images().at(character->directions[0][0].front().image);
      normalize_flash_png(image);
    } else if (texture.sheet >= 0 && static_cast<std::size_t>(texture.sheet) < flash::sheets.size()) {
      image = s.sprite(flash::sheets[static_cast<std::size_t>(texture.sheet)].name, texture.index);
    } else {
      image = s.sprite("lofiObj3", 255);
    }
    premultiply(image);
    return image;
  }
  if (key == "newsfame" || key == "newsoryx" || key == "newsforum") {
    if (dry) return Image{};
    if (key == "newsfame") return s.redraw(s.sprite("lofiObj3", 224), 80, true);
    if (key == "newsoryx") return s.redraw(s.sprite("lofiChar16x16", 0x54), 40, true);
    return s.redraw(s.sprite("lofiInterface2", 4), 80, true);
  }
  // Item tiles: ItemTileSprite (with the dose count) and its dimmed state.
  for (const std::string_view prefix : {"item", "dim"})
    if (keyed(key, prefix, 1, 65535, a)) {
      const auto* o = object(a);
      if (!o) return std::nullopt;
      if (dry) return Image{};
      auto image = s.object_texture(a, 80, true);
      if (o->xml.has("Doses")) s.text(image, o->xml.child("Doses").text(), 12, false, 16777215, 10, 5);
      if (prefix == "dim") fd::color_matrix(image, {.4, 0, 0, 0, 0, 0, .4, 0, 0, 0, 0, 0, .4, 0, 0, 0, 0, 0, 1, 0});
      return image;
    }
  if (keyed(key, "tier", 1, 65535, a)) {
    const auto* o = equipment(a);
    if (!o) return std::nullopt;
    if (dry) return Image{};
    auto image = fd::canvas(40, 40);
    const auto& xml = o->xml;
    bool pet = false;
    for (const auto& activate : xml.children("Activate")) pet |= activate.text() == "PermaPet";
    if (xml.has("Consumable") || pet || xml.has("Treasure") || xml.has("PetFood") || xml.has("NoTierTag")) return image;
    // TierUtil.getTierTag with ItemTile's GlowFilter(0, 1, 2, 2, 10).
    std::string tag = "UT";
    std::uint32_t color = 0x8A2BE2;
    if (xml.has("Tier")) {
      tag = "T" + xml.child("Tier").text();
      color = 16777215;
    } else if (!xml.attribute("setType").empty()) {
      tag = "ST";
      color = 0xFF9900;
    }
    auto layer = fd::canvas(40, 40);
    s.text(layer, tag, 12, true, color, 40 - s.field(tag, 12, true).width, 24);
    fd::glow(layer, 0, 1, 2, 2, 10);
    fd::draw(image, layer, 0, 0);
    return image;
  }
  if (keyed(key, "astrumtipicon", 1, 65535, a)) {
    const auto* o = equipment(a);
    if (!o) return std::nullopt;
    if (dry) return Image{};
    auto texture = s.object_texture(a, 70, true);
    return crop_image(texture, 4, 4, texture.width - 8, texture.height - 8);
  }
  if (keyed(key, "tipbag", 0, 9, a)) {
    // Current client bag IDs, including its additional high-tier bags.
    constexpr std::array<int, 10> types{0x0500, 0x0506, 0x0507, 0x0508, 0x0509, 0x050b, 0x050c, 0x0510, 0x050c, 0x050c};
    if (!object(types[static_cast<std::size_t>(a)])) return std::nullopt;
    if (dry) return Image{};
    auto texture = s.object_texture(types[static_cast<std::size_t>(a)], 40, true);
    return crop_image(texture, 4, 4, texture.width - 8, texture.height - 8);
  }
  if (keyed(key, "tipicon", 1, 65535, a)) {
    const auto* o = equipment(a);
    if (!o) return std::nullopt;
    if (dry) return Image{};
    // EquipmentToolTip: the texture at size 60 with the item's ScaleValue, cropped by 4 pixels.
    const int scale = o->xml.has("ScaleValue") ? o->xml.integer("ScaleValue") : 5;
    auto texture = s.object_texture(a, 60 * scale / 5, true);
    return crop_image(texture, 4, 4, texture.width - 8, texture.height - 8);
  }
  if (keyed(key, "portrait", 1, 65535, a)) {
    const auto* o = object(a);
    int type = a, skin = 0;
    if (!o || !o->player) {
      const auto found =
          std::find_if(catalog.skins().begin(), catalog.skins().end(), [&](const SkinDef& d) { return d.id == a; });
      if (found == catalog.skins().end()) return std::nullopt;
      type = found->class_type;
      skin = found->id;
    }
    if (dry) return Image{};
    auto image = fd::canvas(80, 80);
    fd::draw(image, s.portrait(type, skin, 0, 0), 0, 0);
    return image;
  }
  // Equipment and inventory tiles: backgrounds, restricted-use overlays, slot-type art and hotkey numbers.
  if (keyed(key, "equipment", 0, 3, a) || keyed(key, "equiprestricted", 0, 3, a)) {
    if (dry) return Image{};
    auto image = fd::canvas(40, 40);
    fd::Path shape;
    shape.cut_edge_rect(0, 0, 40, 40, 4, equipment_cuts[static_cast<std::size_t>(a)]);
    fd::fill(image, shape, key.starts_with("equipment") ? equipment_gray : restricted_red);
    return image;
  }
  if (key.starts_with("empty")) {
    const auto p = key.find('p', 5);
    if (p == std::string::npos || !number(std::string_view(key).substr(5, p - 5), a) ||
        !number(std::string_view(key).substr(p + 1), b) || a < 1 || a > 25 || a == 10 || b < 0 || b > 3)
      return std::nullopt;
    if (dry) return Image{};
    auto image = fd::canvas(40, 40);
    fd::Path shape;
    shape.cut_edge_rect(0, 0, 40, 40, 4, equipment_cuts[static_cast<std::size_t>(b)]);
    fd::fill(image, shape, equipment_gray);
    s.slot_background(image, a, 0, true, {});
    return image;
  }
  for (const std::string_view prefix : {"inventory", "hotkey", "restricted"})
    if (keyed(key, prefix, 0, 7, a)) {
      if (dry) return Image{};
      auto image = fd::canvas(40, 40);
      fd::Path shape;
      shape.cut_edge_rect(0, 0, 40, 40, 4, inventory_cuts[static_cast<std::size_t>(a)]);
      fd::fill(image, shape, prefix == "restricted" ? restricted_red : tile_gray);
      if (prefix == "hotkey") s.slot_background(image, 0, a + 1, false, {});
      return image;
    }
  if (key.starts_with("backdrop") && key.size() == 11 && key[9] == 's') {
    const bool backpack = key[8] == '1';
    const int selected = key[10] - '0';
    if ((key[8] != '0' && key[8] != '1') || selected < 0 || selected > (backpack ? 2 : 1)) return std::nullopt;
    if (dry) return Image{};
    // The inventory side panel below the stats, in HUD coordinates offset by y 300.
    auto image = fd::canvas(200, 200);
    fd::Path base;
    base.rect(0, 0, 200, 200);
    fd::fill(image, base, panel_gray);
    fd::Path header;
    header.cut_edge_rect(11, 1, 178, 46, 6);
    fd::fill(image, header, 6776679);
    // TabStripView(186, 153) at (7, 46): tabs, then its content background above them.
    for (int tab = 0; tab < (backpack ? 3 : 2); ++tab) {
      const double tx = 7 + tab * 30, ty = 46 + 8;
      fd::Path shape;
      shape.round_rect(tx, ty, 28, 35, 9);
      fd::fill(image, shape, tab == selected ? 2368034 : 7039594);
      fd::draw(image, s.redraw(s.sprite("lofiInterfaceBig", 24 + tab), 20, true), tx - 5, ty - 11);
    }
    fd::Path content;
    content.cut_edge_rect(7, 46 + 27, 186, 126, 6);
    fd::fill(image, content, 2368034);
    return image;
  }
  if (key.starts_with("statlabel")) {
    const auto p = key.find('p', 9);
    if (p == std::string::npos || !number(std::string_view(key).substr(9, p - 9), a) ||
        !number(std::string_view(key).substr(p + 1), b) || a < 0 || a > 5 || (b != 5 && b != 15))
      return std::nullopt;
    if (dry) return Image{};
    static constexpr std::array<std::string_view, 6> names{"ATT -", "DEF -", "SPD -", "DEX -", "VIT -", "WIS -"};
    const auto f = s.field(names[static_cast<std::size_t>(a)], 13, false);
    auto image = fd::canvas(static_cast<int>(f.width) + 16, static_cast<int>(f.height) + 16);
    s.text(image, names[static_cast<std::size_t>(a)], 13, false, text_gray, 8 + b / 20.0, 8.5, 1);
    return image;
  }
  if (keyed(key, "potionuse", 0, 1, a) || keyed(key, "potionbuy", 0, 1, a)) {
    if (dry) return Image{};
    auto image = fd::canvas(84, 24);
    const auto& cuts = potion_cuts[static_cast<std::size_t>(a)];
    fd::Path outer;
    outer.cut_edge_rect(0, 0, 84, 24, 4, cuts);
    if (key.starts_with("potionuse"))
      fd::fill(image, outer, tile_gray);
    else {
      fd::fill(image, outer, 4078909);
      fd::Path inner;
      inner.cut_edge_rect(2, 2, 80, 20, 4, cuts);
      fd::fill(image, inner, 2368034);
    }
    return image;
  }
  if (keyed(key, "potion", 1, 65535, a)) {
    if (!object(a)) return std::nullopt;
    if (dry) return Image{};
    return s.object_texture(a, 55, false);
  }
  // Interaction panels (InteractPanel images whose panel origin is at (6, 8)), without the object name.
  if (keyed(key, "portalenter", 0, 1, a)) {
    if (dry) return Image{};
    auto image = fd::canvas(200, 100);
    s.panel_button(image, "Enter", a == 1);
    return image;
  }
  if (key == "portalfull" || key == "portallocked") {
    if (dry) return Image{};
    auto image = fd::canvas(200, 100);
    const std::string_view label = key == "portalfull" ? "Full" : "Locked";
    const double height = s.field(label, 18, true).text_height + 4;
    s.centered(image, label, 18, true, 16711680, static_cast<int>(panel_width), 6, 8 + panel_height - height - 12, 1);
    return image;
  }
  if (key.starts_with("buttonpanel") && key.size() == 14 && key[12] == 'h') {
    a = key[11] - '0';
    b = key[13] - '0';
    if (a < 0 || a > 3 || b < 0 || b > 1) return std::nullopt;
    if (dry) return Image{};
    static constexpr std::array<std::string_view, 4> labels{"View", "View", "Change", "Choose"};
    auto image = fd::canvas(200, 100);
    s.panel_button(image, labels[static_cast<std::size_t>(a)], b == 1);
    return image;
  }
  if (key.starts_with("buybutton") && key.size() == 12 && key[10] == 'p') {
    a = key[9] - '0';
    b = key[11] - '0';
    if (a < 0 || a > 2 || b < 0 || b > 1) return std::nullopt;
    if (dry) return Image{};
    static constexpr std::array<std::uint32_t, 3> fills{16777215, 16768133, 8355711};
    auto image = fd::canvas(41, 26);
    fd::Path shape;
    shape.cut_edge_rect(b * .5, 0, 40, 26, 4);
    fd::fill(image, shape, fills[static_cast<std::size_t>(a)]);
    return image;
  }
  if (keyed(key, "currency", 0, 2, a)) {
    if (dry) return Image{};
    // IconFactory.makeCoin/makeFame/makeGuildFame: outlined without glow, cropped by 10 pixels.
    const auto icon = s.redraw(s.sprite("lofiObj3", a == 0 ? 225 : a == 1 ? 224 : 226), 40, true, false);
    auto image = fd::canvas(20, 20);
    fd::draw(image, icon, -10, -10);
    return image;
  }
  if (keyed(key, "sellrank", 1, 99, a)) {
    if (dry) return Image{};
    // SellableObjectPanel's "Rank Required:" notice with a RankText, centered near the panel bottom.
    auto image = fd::canvas(200, 100);
    const auto label = s.field("Rank Required:", 16, true);
    const auto rank = s.rank(nullptr, a, false, 0, 0);
    const double rx = label.width + 4, ry = (label.height - (rank.bottom - rank.top)) * .5;
    const double width = std::max(label.width, rx + rank.right), top = std::min(0.0, ry + rank.top),
                 height = std::max(label.height, ry + rank.bottom) - top;
    const double x = 6 + panel_width / 2 - width / 2, y = 8 + panel_height - height / 2 - 20;
    s.text(image, "Rank Required:", 16, true, 16777215, x, y, 1);
    s.rank(&image, a, false, x + rx, y + ry);
    return image;
  }
  if (key == "sellguild") {
    if (dry) return Image{};
    auto image = fd::canvas(200, 100);
    const std::string_view label = "Leader Rank Required";
    const auto f = s.field(label, 16, true);
    const double width = f.text_width + 4, height = f.text_height + 4;
    s.text(image, label, 16, true, 16711680, 6 + (panel_width - width) * .5, 8 + panel_height - height / 2 - 20, 1);
    return image;
  }
  if (keyed(key, "sellicon", 1, 65535, a)) {
    const auto* o = object(a);
    if (!o) return std::nullopt;
    if (dry) return Image{};
    if (o->class_name == "ClosedVaultChest")
      if (const auto* chest = catalog.object("Vault Chest")) return s.object_texture(chest->type, 80, true);
    return s.object_texture(a, 80, true);
  }
  // PlayerToolTip pieces.
  if (keyed(key, "playerrank", -1, 99, a) || keyed(key, "playerbadge", -1, 99, a)) {
    if (dry) return Image{};
    auto image = fd::canvas(128, 32);
    if (key.starts_with("playerbadge")) {
      fd::Path back;
      back.rect(0, 0, 128, 32);
      fd::fill(image, back, panel_gray, .5);
    }
    s.rank(&image, a, true, 0, 0);
    return image;
  }
  if (keyed(key, "playerguild", 0, 40, a)) {
    if (a % 10) return std::nullopt;
    if (dry) return Image{};
    auto image = fd::canvas(48, 48);
    fd::draw(image, s.redraw(s.sprite("lofiInterfaceBig", 20 - a / 10), 20, true), 0, 0);
    return image;
  }
  if (key == "playerhint") {
    if (dry) return Image{};
    const auto f = s.field("(Click to open menu)", 12, false);
    auto image = fd::canvas(static_cast<int>(f.width) + 16, static_cast<int>(f.height) + 16);
    s.text(image, "(Click to open menu)", 12, false, text_gray, 8, 8, 1);
    return image;
  }
  if (key == "playerframe" || key == "menuframe" || key == "tipframe0" || key == "tipframe1") {
    if (dry) return Image{};
    const bool tip = key.starts_with("tip"), unusable = key == "tipframe1";
    auto image = fd::canvas(80, 80);
    fd::Path shape;
    shape.cut_edge_rect(16, 16, 48, 48, 4);
    fd::fill(image, shape, tip && unusable ? restricted_red : panel_gray, key == "playerframe" ? .5 : 1);
    fd::stroke(image, shape, 1, tip ? (unusable ? 10965039U : 0x9B9B9BU) : 16777215U);
    return image;
  }
  if (keyed(key, "tipline", 0, 3, a)) {
    if (dry) return Image{};
    // LineBreakDesign(218, 0) at a quarter-pixel height, its origin at (4, 8).
    auto image = fd::canvas(232, 17);
    const double y = 8 + a / 4.0;
    fd::Path shape;
    shape.diamond(4, y, 4);
    shape.diamond(222, y, 4);
    shape.rect(4, y - 1, 218, 2);
    fd::fill(image, shape, 0);
    return image;
  }
  if (key.starts_with("menuicon-")) {
    const auto dash = key.find('-', 9);
    if (dash == std::string::npos) return std::nullopt;
    const std::string sheet = key.substr(9, dash - 9);
    std::string rest = key.substr(dash + 1);
    const bool hover = rest.ends_with("hover"), inactive = rest.ends_with("inactive");
    if (hover)
      rest.resize(rest.size() - 5);
    else if (inactive)
      rest.resize(rest.size() - 8);
    if (!number(rest, a) || (sheet != "lofiInterface2" && sheet != "lofiInterfaceBig") || a < 0) return std::nullopt;
    if (dry) return Image{};
    // MenuOption: the icon redrawn (after the hover or inactive ColorTransform) with a DropShadowFilter.
    auto source = s.sprite(sheet, a);
    if (hover || inactive) {
      premultiply(source);
      fd::color_transform(source, hover ? fd::ColorTransform{1, 220 / 255.0, 133 / 255.0}
                                        : fd::ColorTransform{84 / 255.0, 84 / 255.0, 84 / 255.0});
      for (std::size_t i = 0; i < source.pixels.size(); i += 4)
        if (const unsigned alpha = source.pixels[i + 3])
          for (unsigned c = 0; c < 3; ++c)
            source.pixels[i + c] =
                static_cast<unsigned char>(std::min(255U, (source.pixels[i + c] * 255U + alpha / 2) / alpha));
    }
    auto icon = s.redraw(source, 40 / (source.width / 8), true);
    auto image = fd::canvas(64, 64);
    fd::draw(image, icon, 8, 8);
    fd::drop_shadow(image, 0, 0, 0, 1, 4, 4);
    return image;
  }
  if (key.starts_with("iconbutton-")) {
    std::string name = key.substr(11);
    const bool hover = name.ends_with("hover");
    if (hover) name.resize(name.size() - 5);
    if (name != "options" && name != "nexus") return std::nullopt;
    if (dry) return Image{};
    auto image = s.redraw(s.sprite("lofiInterfaceBig", name == "options" ? 5 : 6), 20, true);
    if (hover) fd::color_transform(image, {1, 220 / 255.0, 133 / 255.0});
    return image;
  }
  if (keyed(key, "chatstar", 0, 99, a)) {
    if (dry) return Image{};
    // FameUtil.numStarsToIcon: the star over a translucent disc, with DropShadowFilter(0,0,0,.5,6,6,1).
    const auto& shape = s.star_shape();
    const double w = std::trunc(shape.width() / 2 + 2), h = std::trunc(shape.height() / 2 + 2);
    auto image = fd::canvas(32, 32);
    fd::Path disc;
    disc.circle(8 + w, 8 + h, w);
    fd::fill(image, disc, 0, .4);
    fd::draw_shape(image, shape, 8 + 2 - shape.left, 8 + 1 - shape.top, 1, s.star_color(a));
    fd::drop_shadow(image, 0, 0, 0, .5, 6, 6, 1);
    return image;
  }
  if (key.starts_with("tradefill")) {
    std::string rest = key.substr(9);
    const bool on = rest.ends_with("on");
    if (on) rest.resize(rest.size() - 2);
    if (!number(rest, a) || a < 0 || a > 11) return std::nullopt;
    if (dry) return Image{};
    auto image = fd::canvas(48, 48);
    fd::Path shape;
    shape.cut_edge_rect(4, 4, 40, 40, 4, trade_cuts[static_cast<std::size_t>(a)]);
    fd::fill(image, shape, on ? 16764247 : tile_gray);
    if (on) fd::stroke(image, shape, 2, 16711310);
    return image;
  }
  if (key.starts_with("tradeempty")) {
    const auto t = key.find('t', 10);
    if (t == std::string::npos || !number(std::string_view(key).substr(10, t - 10), a) ||
        !number(std::string_view(key).substr(t + 1), b) || a < 0 || a > 11 || b < 0 || b > (a < 4 ? 30 : 0))
      return std::nullopt;
    if (dry) return Image{};
    auto image = fd::canvas(48, 48);
    fd::Path shape;
    shape.cut_edge_rect(4, 4, 40, 40, 4, trade_cuts[static_cast<std::size_t>(a)]);
    fd::fill(image, shape, tile_gray);
    s.slot_background(image, b, a - 3, false, {4, 4});
    return image;
  }
  // Skin chooser art.
  if (key.starts_with("skinicon") || key.starts_with("skinwalk")) {
    const bool walk = key.starts_with("skinwalk");
    const auto body = std::string_view(key).substr(8);
    const auto dash = body.find('-');
    if (dash == std::string_view::npos) return std::nullopt;
    int skin{}, frame{};
    const auto second = body.find('-', dash + 1);
    if (!number(body.substr(0, dash), a) ||
        !number(body.substr(dash + 1, (walk ? second : body.size()) - dash - 1), skin))
      return std::nullopt;
    if (walk && (second == std::string_view::npos || !number(body.substr(second + 1), frame) || frame < 0 || frame > 1))
      return std::nullopt;
    const auto* player = object(a);
    if (!player || !player->player || (skin && !catalog.skin(skin, a))) return std::nullopt;
    const auto& texture = catalog.player_texture(a, skin).select(0);
    const auto* character = s.appearances->animations().character(texture.animated, texture.index);
    if (!character) return std::nullopt;
    if (dry) return Image{};
    const auto& frames = character->directions[0][walk ? 1 : 0];
    const auto chosen = frames.at(walk && frame == 0 ? std::min<std::size_t>(frames.size() - 1, frames.size() / 2) : 0);
    auto image = s.frame_texture(chosen, walk ? 200 : 100, false, 0, 0);
    if (walk) return image;
    // CharacterFactory.makeIcon crops 6 pixels from the sides and top.
    return crop_image(image, 6, 6, image.width - 12, image.height - 6);
  }
  if (keyed(key, "skinstars", 0, 5, a)) {
    if (dry) return Image{};
    // StarsView: five stars on a rounded background, the first `a` filled.
    const auto& shape = s.star_shape();
    const double width = std::trunc(shape.width() * 5 + 8), height = std::trunc(shape.height() + 8);
    auto image = fd::canvas(static_cast<int>(std::ceil(width)), static_cast<int>(std::ceil(height)));
    fd::Path back;
    back.round_rect(0, 0, width, height, 15);
    fd::fill(image, back, 2434341);
    for (int i = 0; i < 5; ++i) {
      const std::uint32_t color = i < a ? 16777215 : 8618883;
      fd::draw_shape(image, shape, 4 + shape.width() * i - shape.left, 4 - shape.top, 1,
                     {0, 0, 0, 1, static_cast<double>(color >> 16U), static_cast<double>((color >> 8U) & 255U),
                      static_cast<double>(color & 255U)});
    }
    return image;
  }
  // SoundIcon: the lofiInterfaceBig speaker (frame 3 while sound effects play, 4 when muted) at 2x with
  // GlowFilter(0, 1, 4, 4, 2, 1). The 8-pixel margin holds the glow.
  if (key == "soundicon-on" || key == "soundicon-off") {
    if (dry) return Image{};
    auto source = s.sprite("lofiInterfaceBig", key == "soundicon-on" ? 3 : 4);
    premultiply(source);
    auto image = fd::canvas(source.width * 2 + 16, source.height * 2 + 16);
    fd::draw(image, source, 8, 8, 2, 2);
    fd::drop_shadow(image, 0, 0, 0, 1, 4, 4, 2);
    return image;
  }
  if (keyed(key, "guildrankicon", 0, 40, a)) {
    if (a % 10) return std::nullopt;
    if (dry) return Image{};
    return s.redraw(s.sprite("lofiInterfaceBig", 20 - a / 10), 20, true);
  }
  if (key == "classqueststar") {
    if (dry) return Image{};
    const auto& shape = s.star_shape();
    constexpr double scale = 1.2;
    constexpr int margin = 8;
    auto image = fd::canvas(static_cast<int>(std::ceil(shape.width() * scale)) + margin * 2,
                            static_cast<int>(std::ceil(shape.height() * scale)) + margin * 2);
    fd::draw_shape(image, shape, margin - shape.left * scale, margin - shape.top * scale, scale,
                   {179.0 / 255, 179.0 / 255, 179.0 / 255});
    fd::drop_shadow(image, 0, 0, 0, 1, 4, 4, 1);
    return image;
  }
  // NewCharacterScreen's CharacterBox frame: FullCharBoxGraphic's sprite places its shape at (-16, -13), so the art
  // covers -0.5 to 104.5 around the box origin, and the canvas starts half a pixel before it.
  if (key == "classbox") {
    if (dry) return Image{};
    auto image = fd::canvas(105, 105);
    fd::draw_shape(image, s.box_shape(), -15.5, -12.5);
    return image;
  }
  // CharacterBox.getStars: five StarGraphic symbols, the first N at fullCT (.8) and the rest at emptyCT (.2), under
  // DropShadowFilter(0,0,0,1,4,4). The canvas carries an 8 pixel margin around the row.
  if (keyed(key, "classstars", 0, 5, a)) {
    if (dry) return Image{};
    const auto& shape = s.star_shape();
    constexpr int margin = 8, count = 5;
    const double advance = shape.width();
    auto image = fd::canvas(static_cast<int>(std::ceil(advance * count)) + margin * 2,
                            static_cast<int>(std::ceil(shape.height())) + margin * 2);
    for (int i = 0; i < count; ++i)
      fd::draw_shape(image, shape, margin + i * advance - shape.left, margin - shape.top, 1,
                     i < a ? fd::ColorTransform{.8, .8, .8} : fd::ColorTransform{.2, .2, .2});
    fd::drop_shadow(image, 0, 0, 0, 1, 4, 4, 1);
    return image;
  }
  // CharacterBox.setImage: SavedCharacter.getImage's standing frame facing down at size 100, tinted by selectedCT.
  if (keyed(key, "classportrait", 1, 65535, a)) {
    const auto* o = object(a);
    if (!o || !o->player) return std::nullopt;
    if (dry) return Image{};
    const auto& t = catalog.player_texture(a, 0).select(0);
    Image image;
    if (const auto* character = s.appearances->animations().character(t.animated, t.index))
      image = s.frame_texture(character->directions[2][0].front(), 100, false, 0, 0);
    else
      image = s.object_texture(a, 100, false);
    fd::color_transform(image, {.75, .75, .75});
    return image;
  }
  // ClassToolTip's portrait: GameObject.getPortrait's redrawn texture at its own size, without portrait<N>'s canvas.
  if (keyed(key, "tooltipportrait", 1, 65535, a)) {
    const auto* o = object(a);
    if (!o || !o->player) return std::nullopt;
    if (dry) return Image{};
    return s.portrait(a, 0, 0, 0);
  }
  if (key == "skinfame" || key == "skincreditcoin") {
    if (dry) return Image{};
    return s.redraw(s.sprite("lofiObj3", key == "skinfame" ? 224 : 225), 40, true);
  }
  if (key == "famebackground") {
    if (dry) return Image{};
    const auto shape = fd::load_swf_shape(std::filesystem::path(flash::source_root) /
                                          "src/com/company/rotmg/graphics/FameIconBackgroundDesign.swf");
    auto image = fd::canvas(static_cast<int>(std::ceil(shape.width())), static_cast<int>(std::ceil(shape.height())));
    fd::draw_shape(image, shape, -shape.left, -shape.top);
    return image;
  }
  // GuildMembers icons: guild fame, the five guild ranks, and the remove cross.
  if (keyed(key, "guildicon", 0, 6, a)) {
    if (dry) return Image{};
    if (a == 0) return s.redraw(s.sprite("lofiObj3", 226), 40, true);
    if (a <= 5) return s.redraw(s.sprite("lofiInterfaceBig", 21 - a), 20, true);
    const auto& shape = s.cross_shape();
    auto image = fd::canvas(static_cast<int>(std::ceil(shape.width())), static_cast<int>(std::ceil(shape.height())));
    fd::draw_shape(image, shape, 0, 0);
    return image;
  }
  return std::nullopt;
}
bool HudArt::contains(const std::string& key) const { return cache_.contains(key) || make(key, true).has_value(); }
Image HudArt::portrait(int type, int skin, std::uint32_t tex1, std::uint32_t tex2) const {
  if (tex1 || tex2) return sources().portrait(type, skin, tex1, tex2);
  return image("portrait" + std::to_string(sources().catalog->portrait_type(type, skin)));
}
Image HudArt::game_object_portrait(int type, int skin, std::uint32_t tex1, std::uint32_t tex2) const {
  return sources().portrait(type, skin, tex1, tex2);
}
Image HudArt::fame_portrait(int type, int skin, std::uint32_t tex1, std::uint32_t tex2, int size) const {
  auto& s = sources();
  const auto& texture = s.catalog->player_texture(type, skin).select(0);
  if (const auto* character = s.appearances->animations().character(texture.animated, texture.index)) {
    auto image = s.frame_texture(character->directions[0][0].front(), size, false, tex1, tex2);
    return crop_image(image, 6, 6, image.width - 12, image.height - 6);
  }
  return s.object_texture(type, size, true);
}
Vec2 HudArt::rank_star_size(double scale) const {
  const auto& shape = sources().star_shape();
  return {shape.width() * scale, shape.height() * scale};
}
Image HudArt::rank_star_icon(int stars, double scale) const {
  if (stars < 0) return {};
  if (!std::isfinite(scale) || scale <= 0) throw std::invalid_argument("Invalid rank star scale");
  return sources().rank_star_icon(stars, scale);
}
Image HudArt::star_graphic(std::uint32_t tint, double scale) const {
  if (!std::isfinite(scale) || scale <= 0) throw std::invalid_argument("Invalid star scale");
  const auto& shape = sources().star_shape();
  auto image = fd::canvas(static_cast<int>(std::ceil(shape.width() * scale)),
                          static_cast<int>(std::ceil(shape.height() * scale)));
  fd::draw_shape(image, shape, -shape.left * scale, -shape.top * scale, scale,
                 {static_cast<double>((tint >> 16U) & 255U) / 255, static_cast<double>((tint >> 8U) & 255U) / 255,
                  static_cast<double>(tint & 255U) / 255});
  return image;
}
const Image& HudArt::image(const std::string& key) const {
  if (const auto found = cache_.find(key); found != cache_.end()) {
    lru_.splice(lru_.begin(), lru_, found->second.lru);
    return found->second.image;
  }
  auto made = make(key, false);
  if (!made) throw std::runtime_error("Unknown HUD artwork: " + key);
  if (made->pixels.size() > cache_byte_limit) throw std::runtime_error("HUD artwork exceeds decoded cache budget");
  while (!cache_.empty() && (cache_.size() >= cache_limit || bytes_ + made->pixels.size() > cache_byte_limit)) {
    const auto found = cache_.find(lru_.back());
    bytes_ -= found->second.image.pixels.size();
    cache_.erase(found);
    lru_.pop_back();
    ++evictions_;
  }
  bytes_ += made->pixels.size();
  lru_.push_front(key);
  return cache_.emplace(key, Entry{std::move(*made), lru_.begin()}).first->second.image;
}
}
