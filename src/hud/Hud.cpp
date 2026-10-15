#include "realm/hud/Hud.hpp"
#include "flash_party_transforms.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
// The tab panel (y 300-500) and the interaction panel below it redraw separately.
HudState::Inventory upper(HudState::Inventory inventory) {
  std::fill(inventory.items.begin() + container_slot, inventory.items.end(), -1);
  std::fill(inventory.restricted.begin() + container_slot, inventory.restricted.end(), false);
  inventory.container = -1;
  if (inventory.dragging >= container_slot) {
    inventory.dragging = -1;
  }
  return inventory;
}

bool same_lower(const HudState::Inventory& first, const HudState::Inventory& second) {
  const auto dragged = [](const HudState::Inventory& inventory) {
    return inventory.dragging >= container_slot ? inventory.dragging : -1;
  };
  return first.enabled == second.enabled && first.tiers == second.tiers &&
         first.container == second.container && dragged(first) == dragged(second) &&
         std::equal(first.items.begin() + container_slot, first.items.end(),
                    second.items.begin() + container_slot) &&
         std::equal(first.restricted.begin() + container_slot, first.restricted.end(),
                    second.restricted.begin() + container_slot);
}

// One premultiplied channel under a PartyPanel transform: an 8.8 multiplier
// and an offset truncated toward zero, scaled by alpha, then the measured
// Flash corrections (tools/bake_party_transforms.py).
unsigned char party_channel(int multiplier, int offset, unsigned alpha, unsigned value) {
  const std::int64_t numerator = static_cast<std::int64_t>(value) * multiplier * 255 +
                                 static_cast<std::int64_t>(offset) * alpha * 256;
  const auto quotient = numerator / 65280 - (numerator % 65280 < 0 ? 1 : 0);
  auto result = std::clamp<std::int64_t>(quotient, 0, alpha);
  const auto key = ((static_cast<std::uint32_t>(multiplier == 153) << 24U) |
                    (static_cast<std::uint32_t>(offset + 128) << 16U) | (alpha << 8U) | value)
                   << 1U;
  const auto found = std::lower_bound(flash_party_transform_corrections.begin(),
                                      flash_party_transform_corrections.end(), key);
  if (found != flash_party_transform_corrections.end() && (*found >> 1U) == (key >> 1U)) {
    result += (*found & 1U) ? 1 : -1;
  }
  return static_cast<unsigned char>(result);
}

// Applies the transform to premultiplied pixels.
void transform(Image& image, const ColorTransform& color_transform) {
  std::array<int, 3> multiplier{};
  std::array<int, 3> offset{};
  for (std::size_t c = 0; c < 3; ++c) {
    multiplier[c] = static_cast<int>(color_transform.multiplier[c] * 256);
    offset[c] = static_cast<int>(std::trunc(color_transform.offset[c]));
    if ((multiplier[c] != 256 && multiplier[c] != 153) || offset[c] < -128 || offset[c] > 127) {
      throw std::runtime_error("Unmeasured HUD color transform");
    }
  }
  for (std::size_t at = 0; at < image.pixels.size(); at += 4) {
    const unsigned alpha = image.pixels[at + 3];
    if (alpha == 0) {
      continue;
    }
    for (std::size_t c = 0; c < 3; ++c) {
      image.pixels[at + c] = party_channel(multiplier[c], offset[c], alpha, image.pixels[at + c]);
    }
  }
}

ColorTransform party_transform(const HudState::PartyMember& member) {
  ColorTransform result;
  if (member.pulse >= 0) {
    const double rv = member.pulse / 10.0;
    result.offset = {rv * 128, -rv * 128, -rv * 128};
  }
  // ColorTransform.concat(MoreColorUtil.darkCT).
  if (member.dark) {
    for (auto& m : result.multiplier) {
      m *= .6;
    }
  }
  return result;
}
}

Hud::Hud(const FontBook& fonts, const HudArt* art, const Catalog* catalog)
    : fonts_(fonts), art_(art), state_catalog_(catalog) {}

std::optional<TextBitmap> tier_bitmap(const FontBook& fonts, const ObjectDef& item) {
  if (item.consumable || item.xml.has("Treasure") || item.xml.has("PetFood") ||
      item.xml.has("NoTierTag")) {
    return {};
  }
  for (const auto& activation : item.xml.children("Activate")) {
    if (activation.text() == "PermaPet") {
      return {};
    }
  }
  const bool set = !item.xml.attribute("setType").empty();
  const auto label = item.tier >= 0 ? "T" + std::to_string(item.tier) : set ? "ST" : "UT";
  // ItemTile tier tag: GlowFilter(0,1,2,2,10), blur 2 and strength 10.
  return fonts.rasterize_glow(label, 12, true,
                              item.tier >= 0 ? 0xffffffU
                              : set          ? 0xff9900U
                                             : 0x8a2be2U,
                              10, 2);
}

void Hud::tier(const ObjectDef& item, int x, int y) {
  if (auto label = tier_bitmap(fonts_, item)) {
    draw(*label, x + 40 - label->width, y + 24);
  }
}

Image rank_star_bitmap(const FontBook& fonts, const HudArt& art, int stars) {
  const auto& source = art.image("playerrank" + std::to_string(std::clamp(stars, 0, 99)));
  const int begin =
      static_cast<int>(fonts.layout("Rank: ", 16).width +
                       fonts.layout(std::to_string(std::clamp(stars, 0, 99)), 16).width + 2);
  int left = source.width;
  int right = begin;
  int top = source.height;
  int bottom = 0;
  unsigned peak{};
  for (int y = 0; y < source.height; ++y) {
    for (int x = begin; x < source.width; ++x) {
      const auto at = static_cast<std::size_t>(y * source.width + x) * 4;
      const unsigned value =
          std::max({source.pixels[at], source.pixels[at + 1], source.pixels[at + 2]});
      if (value) {
        left = std::min(left, x);
        right = std::max(right, x + 1);
        top = std::min(top, y);
        bottom = std::max(bottom, y + 1);
        peak = std::max(peak, value);
      }
    }
  }
  if (!peak) {
    return ui_image(1, 1, 1);
  }
  auto result = ui_image(right - left, bottom - top, 1);
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      const auto from = static_cast<std::size_t>(y * source.width + x) * 4;
      const auto to = static_cast<std::size_t>((y - top) * result.width + x - left) * 4;
      std::copy_n(source.pixels.data() + from, 3, result.pixels.data() + to);
      result.pixels[to + 3] = static_cast<unsigned char>(
          std::max({source.pixels[from], source.pixels[from + 1], source.pixels[from + 2]}) * 255U /
          peak);
    }
  }
  return result;  // premultiplied icon, with the baked number/background removed
}

void Hud::blit(const Image& source, int x, int y, HudRegion clip) {
  if (image_.pixel_scale != 1) {
    ui_blit(image_, source, x, y, true, false, 1, clip.x, clip.y, clip.width, clip.height);
    return;
  }
  for (int row = std::max({0, -y, clip.y - y});
       row < std::min({source.height, 600 - y, clip.y + clip.height - y}); ++row) {
    for (int col = std::max({0, -x, clip.x - x});
         col < std::min({source.width, 200 - x, clip.x + clip.width - x}); ++col) {
      const auto src = (static_cast<std::size_t>(row) * static_cast<std::size_t>(source.width) +
                        static_cast<std::size_t>(col)) *
                       4;
      const auto dst =
          (static_cast<std::size_t>(y + row) * 200 + static_cast<std::size_t>(x + col)) * 4;
      const unsigned alpha = source.pixels[src + 3];
      if (!alpha) {
        continue;
      }
      for (unsigned c = 0; c < 3; ++c) {
        image_.pixels[dst + c] = static_cast<unsigned char>(
            source.pixels[src + c] +
            (static_cast<unsigned>(image_.pixels[dst + c]) * (256 - alpha) >> 8U));
      }
    }
  }
}

void Hud::heading(const HudState& state) {
  if (art_ && state.inventory.enabled) {
    blit(art_->portrait(state.inventory.appearance_type ? state.inventory.appearance_type
                                                        : state.inventory.portrait,
                        state.inventory.skin, state.inventory.tex1, state.inventory.tex2),
         -2, 190);
  }
  text(state.name, 20, true, 11776947, 36, 198);
  if (art_ && state.header.button != HudState::Header::Button::none) {
    const std::string key =
        std::string("iconbutton-") +
        (state.header.button == HudState::Header::Button::options ? "options" : "nexus") +
        (state.header.hover ? "hover" : "");
    if (art_->contains(key)) {
      blit(art_->image(key), static_cast<int>(header_button.x), static_cast<int>(header_button.y));
    }
  }
}

void Hud::inventory(const HudState::Inventory& state) {
  if (!art_ || !state.enabled) {
    return;
  }
  if (state.selected < 0 || state.selected > 2 || (state.selected == 2 && !state.backpack)) {
    throw std::runtime_error("Invalid HUD inventory tab");
  }
  blit(art_->image(
           "backdrop" + std::to_string(state.backpack) + "s" + std::to_string(state.selected),
           image_.pixel_scale),
       0, 300);
  for (int i = 0; i < 4; ++i) {
    tile(state, i, i, 14 + 44 * i, 304, true);
  }
  if (state.selected != 1) {
    for (int i = 0; i < 8; ++i) {
      tile(state, (state.selected == 2 ? 12 : 4) + i, i, 14 + 44 * (i % 4), 380 + 44 * (i / 4),
           false);
    }
    if (state.potions[0] >= 0 && state.potions[1] >= 0) {
      potions(state.potions);
    }
  }
}

void Hud::tile(const HudState::Inventory& state, int index, int position, int x, int y,
               bool equipped) {
  const int type = index == state.dragging ? -1 : state.items.at(static_cast<std::size_t>(index));
  std::string background;
  if (equipped) {
    background = type <= 0 && state.types.at(static_cast<std::size_t>(position)) > 0
                     ? "empty" +
                           std::to_string(state.types.at(static_cast<std::size_t>(position))) +
                           "p" + std::to_string(position)
                     : "equipment" + std::to_string(position);
  }
  // ContainerGrid tiles have no hotkey numbers.
  else {
    background =
        (type <= 0 && index < container_slot ? "hotkey" : "inventory") + std::to_string(position);
  }
  if (image_.pixel_scale != 1 && background.starts_with("hotkey")) {
    blit(art_->image("inventory" + std::to_string(position), image_.pixel_scale), x, y);
    const auto label = std::to_string(position + 1);
    draw_direct(fonts_.rasterize(label, 26, true, false, 3552822, 0, 0, 1, 26, 30),
                x + 20 - fonts_.layout(label, 26, true).width / 2, y + 2, 3552822);
  } else {
    blit(art_->image(background, image_.pixel_scale), x, y);
  }
  if (type == -1) {
    return;
  }
  if (state.restricted.at(static_cast<std::size_t>(index))) {
    blit(art_->image((equipped ? "equiprestricted" : "restricted") + std::to_string(position),
                     image_.pixel_scale),
         x, y);
  }
  const auto& icon =
      art_->image((equipped && state.dim.at(static_cast<std::size_t>(position)) ? "dim" : "item") +
                  std::to_string(type));
  blit(icon, x + 20 - icon.width / 2, y + 20 - icon.height / 2);
  if (state.tiers) {
    if (image_.pixel_scale != 1 || !art_->contains("tier" + std::to_string(type))) {
      // HUD art carries the original labels at base density only.
      // Read item metadata for a freshly rasterized tier at larger sizes.
      const auto* item =
          state_catalog_ ? state_catalog_->object(static_cast<std::uint16_t>(type)) : nullptr;
      if (!item) {
        throw std::logic_error("Resized tier labels require item definitions");
      }
      tier(*item, x, y);
    } else {
      blit(art_->image("tier" + std::to_string(type)), x, y);
    }
  }
}

void Hud::container(const HudState::Inventory& state) {
  if (!art_ || !state.enabled || state.container < 0) {
    return;
  }
  for (int i = 0; i < 8; ++i) {
    tile(state, container_slot + i, i, container_x + 44 * (i % 4), container_y + 44 * (i / 4),
         false);
  }
}

void Hud::potions(const std::array<int, 2>& counts) {
  // PotionSlotView.setData: the source never clears available, so its
  // grayscale filter is not reachable.
  for (std::size_t i = 0; i < counts.size(); ++i) {
    const int x = potion_view_x[i];
    const bool stocked = counts[i] > 0;
    blit(art_->image((stocked ? "potionuse" : "potionbuy") + std::to_string(i), image_.pixel_scale),
         x, potion_view_y);
    text(stocked ? std::to_string(counts[i]) : "0", 13, false, stocked ? 0xffffffU : 11184810U,
         x + 47, potion_view_y + 2, 1, 2, 24);
    blit(art_->image("potion" + std::to_string(potion_types[i])), x + 13, potion_view_y - 11);
  }
}

void Hud::portal(const HudState::Portal& state) {
  if (!art_ || state.id < 0) {
    return;
  }
  // The panel origin is (6,508); the name field is 188 wide at y 6, or 0
  // once it is taller than 30 pixels.
  auto name = fonts_.rasterize_centered(state.name, 18, true, true, 0xffffffU, 188);
  const int y = name.height > 30 ? 508 : 514;
  draw(std::move(name), 6, y);
  if (image_.pixel_scale != 1) {
    if (!state.locked && state.active) {
      native_button(portal_button, "Enter", state.hover);
    } else {
      auto full = fonts_.rasterize_centered(state.locked ? "Locked" : "Full", 18, true, true,
                                            0xff0000, 188);
      const auto full_y = 580 - full.height;
      draw(std::move(full), 6, full_y);
    }
  } else {
    blit(art_->image(!state.locked && state.active ? "portalenter" + std::to_string(state.hover)
                     : state.locked                ? "portallocked"
                                                   : "portalfull"),
         0, 500);
  }
}

void Hud::button(const HudState::Button& state) {
  if (!art_ || state.id < 0) {
    return;
  }
  const auto kind = static_cast<std::size_t>(state.kind);
  // The 188-pixel centered title is at y 6; ReskinPanel never positions its title.
  draw(fonts_.rasterize_centered(button_panel_titles.at(kind), 18, true, true, 0xffffffU, 188), 6,
       kind == 3 ? 508 : 514);
  if (image_.pixel_scale != 1) {
    constexpr std::array labels{"View", "View", "Change", "Choose"};
    native_button(button_panel_buttons.at(kind), labels.at(kind), state.hover);
  } else {
    blit(art_->image("buttonpanel" + std::to_string(kind) + "h" + std::to_string(state.hover)), 0,
         500);
  }
}

void Hud::sellable(const HudState::Sellable& state) {
  if (!art_ || state.id < 0) {
    return;
  }
  // Children in order: nameText (144 wide at x 44; y 12, or 0 once taller
  // than 30), buyButton, the icon bitmap at (-4,-8), then notices.
  auto name = fonts_.rasterize_centered(state.name, 16, true, true, 0xffffffU, 144);
  const int name_y = name.height > 30 ? 508 : 520;
  draw(std::move(name), 50, name_y);
  if (state.button) {
    const auto layout = buy_button_layout(fonts_, state);
    // The baked 40-pixel shape provides both cut ends; inner columns are uniform.
    // Its 41st column is covered only at a half-pixel position.
    const bool half = layout.area.x != std::floor(layout.area.x);
    const int x0 = static_cast<int>(std::floor(layout.area.x));
    const auto& shape = art_->image("buybutton" +
                                    std::to_string(!state.enabled ? 2
                                                   : state.hover  ? 1
                                                                  : 0) +
                                    "p" + std::to_string(half));
    const int columns = static_cast<int>(layout.area.width) + (half ? 1 : 0);
    const int extent = half ? 41 : 40;
    Image body{columns, shape.height,
               std::vector<unsigned char>(static_cast<std::size_t>(columns) *
                                          static_cast<std::size_t>(shape.height) * 4)};
    for (int col = 0; col < columns; ++col) {
      const int source = col < 8 ? col : col >= columns - 8 ? extent - (columns - col) : 20;
      for (int row = 0; row < shape.height; ++row) {
        std::copy_n(
            shape.pixels.begin() + (static_cast<std::ptrdiff_t>(row) * shape.width + source) * 4, 4,
            body.pixels.begin() + (static_cast<std::ptrdiff_t>(row) * columns + col) * 4);
      }
    }
    blit(body, x0, 562);
    // The stage rounds the field origin to a quarter pixel; glyphs then
    // round to quarters within the field.
    const double text_x =
        std::floor(layout.text_x) + std::round((layout.text_x - std::floor(layout.text_x)) * 4) / 4;
    draw_direct(fonts_.rasterize(std::to_string(state.price), 16, true, false, 3552822,
                                 text_x - std::floor(text_x)),
                text_x, 563, 3552822);
    // Unsmoothed bitmaps sample the nearest source pixel.
    if (layout.icon) {
      blit(art_->image("currency" + std::to_string(state.currency)),
           static_cast<int>(std::floor(layout.icon_x + .5)), 565);
    }
  }
  if (art_->contains(state.icon)) {
    blit(art_->image(state.icon), 2, 500);
  }
  if (image_.pixel_scale != 1) {
    if (state.rank > 0) {
      const auto icon = rank_star_bitmap(fonts_, *art_, state.rank);
      const auto label = fonts_.layout("Rank Required:", 16, true);
      const auto number = fonts_.layout(std::to_string(state.rank), 16);
      const double x = 100 - (label.width + 4 + number.width + 2 + icon.width + 4) / 2;
      const double y = 572 - label.height / 2;
      text("Rank Required:", 16, true, 0xffffff, x, y);
      text(std::to_string(state.rank), 16, false, 11776947, x + label.width + 4, y);
      blit(icon, static_cast<int>(x + label.width + number.width + 6),
           static_cast<int>(y + (number.height - icon.height) / 2 + 1));
    }
    if (state.guild) {
      const auto label = fonts_.layout("Leader Rank Required", 16, true);
      text("Leader Rank Required", 16, true, 0xff0000, 100 - label.width / 2,
           572 - label.height / 2);
    }
  } else {
    if (state.rank > 0 && art_->contains("sellrank" + std::to_string(state.rank))) {
      blit(art_->image("sellrank" + std::to_string(state.rank)), 0, 500);
    }
    if (state.guild) {
      blit(art_->image("sellguild"), 0, 500);
    }
  }
}

void Hud::native_button(HudRect area, std::string_view label, bool hover) {
  const int x0 = static_cast<int>(std::floor(area.x));
  const int w = static_cast<int>(std::ceil(area.width));
  for (int y = 0; y < 26; ++y) {
    const int cut = std::max(0, 4 - std::min(y, 25 - y));
    rectangle(x0 + cut, static_cast<int>(area.y) + y, w - cut * 2, 1,
              image_.pixel_scale != 1 ? (hover ? 16768133U : 0xffffffU)
                                      : (hover ? 0xffffffU : 0xb3b3b3U));
  }
  draw_direct(fonts_.rasterize(label, 16, true, false, 0x363636U), area.x + 9, area.y + 1,
              0x363636U);
}

void Hud::social(const HudState::Social& state) {
  if (state.id < 0) {
    return;
  }
  // Keep long guild names in the title region, clear of the action row.
  int size = 18;
  auto lines = fonts_.wrap(state.title, size, true, 184);
  if (lines.size() > 2) {
    size = 14;
    lines = fonts_.wrap(state.title, size, true, 184);
  }
  if (lines.size() > 3) {
    lines.resize(3);
    lines.back() = "...";
  }
  for (std::size_t i = 0; i < lines.size(); ++i) {
    text(lines[i], size, true, 0xffffffU, 8 + (184 - fonts_.measure(lines[i], size, true)) / 2,
         508 + static_cast<double>(i) * (size + 2));
  }
  if (!state.notice.empty()) {
    draw(fonts_.rasterize_centered(state.notice, 16, true, true, 0xff4444U, 188), 6, 562);
  }
  if (state.enabled) {
    const double width =
        std::ceil(fonts_.measure(state.label, 16, true)) + 18 + (state.currency >= 0 ? 24 : 0);
    const HudRect area{100 - width / 2, 562, width, 26};
    native_button(area, state.label, state.hover);
    if (art_ && state.currency >= 0) {
      blit(art_->image("currency" + std::to_string(state.currency)),
           static_cast<int>(area.x + width - 26), 565);
    }
  }
}

void Hud::party(const HudState::Party& state) {
  if (!art_ || !state.shown) {
    return;
  }
  // PartyPanel is at (6,8) in the interaction panel; its items are on a 100x32 grid.
  for (std::size_t i = 0; i < state.members.size(); ++i) {
    if (const auto& member = state.members[i]) {
      const int x = 6 + 100 * static_cast<int>(i % 2);
      const int y = 508 + 32 * static_cast<int>(i / 2);
      // A zero pulse step is an identity ColorTransform, which is left unapplied.
      const bool identity = !member->dark && member->pulse <= 0;
      const auto color_transform = party_transform(*member);
      Image portrait = art_->portrait(member->type, member->skin, member->tex1, member->tex2);
      if (!identity) {
        transform(portrait, color_transform);
      }
      blit(portrait, x - 4, y - 4);
      // SimpleText(13,color,false,66,20) with a default DropShadowFilter.
      draw(fonts_.rasterize(member->name, 13, true, true, member->color, 0, 0, 1, 66, 20), x + 32,
           y + 6, 1, identity ? nullptr : &color_transform);
    }
  }
}

void Hud::stats(const std::array<HudStatState, 6>& state) {
  constexpr std::array labels{"ATT -", "DEF -", "SPD -", "DEX -", "VIT -", "WIS -"};
  std::array<std::string, 6> amounts;
  std::array<double, 6> widths;
  double left{};
  double right{};
  for (std::size_t i = 0; i < state.size(); ++i) {
    const auto& stat = state[i];
    amounts[i] = std::to_string(stat.value);
    if (stat.boost) {
      amounts[i] +=
          " (" + std::string(stat.boost > 0 ? "+" : "") + std::to_string(stat.boost) + ")";
    }
    widths[i] = fonts_.layout(labels[i], 13).width;
    if (i % 2 == 0) {
      left = std::max(left, widths[i]);
    } else {
      right = std::max(right, fonts_.layout(amounts[i], 13, true).width);
    }
  }
  // StatsView centers its entire two-column bounds. Its vertical position is
  // set once with the initial '-' fields, before any server values arrive.
  const double x = 7 + 30 + (191 - (left + 95.5 + right)) / 2;
  const double y = 373 + (126 - (30 + std::max(fonts_.layout("ATT -", 13).height,
                                               fonts_.layout("-", 13, true).height))) /
                             2;
  for (std::size_t i = 0; i < state.size(); ++i) {
    const auto& stat = state[i];
    const auto color = static_cast<std::int64_t>(stat.value) - stat.boost >= stat.cap ? 16572160U
                       : ((i != 1 && stat.value <= 0) || stat.boost < 0)              ? 16726072U
                       : stat.boost > 0                                               ? 6206769U
                                                                                      : 11776947U;
    const double sx = x + static_cast<double>(i % 2) * 95.5;
    const double sy = y + static_cast<double>(i / 2) * 15;
    // Finite source label fields preserve their antialiased clip edges;
    // dynamic values use the native font layout/rasterizer below.
    const double lx = sx - widths[i];
    const int phase = static_cast<int>(std::lround((lx - std::floor(lx)) * 20));
    if (image_.pixel_scale != 1) {
      text(labels[i], 13, false, 11776947, lx, sy);
    } else {
      blit(art_->image("statlabel" + std::to_string(i) + "p" + std::to_string(phase)),
           static_cast<int>(std::floor(lx)) - 8, static_cast<int>(std::floor(sy)) - 8);
    }
    text(amounts[i], 13, true, color, sx, sy);
  }
}

bool Hud::update(const HudState& state) {
  changes_.clear();
  if (image_.pixel_scale != fonts_.pixel_scale()) {
    image_ = ui_image(200, 600, fonts_.pixel_scale());
    initialized_ = false;
  }
  if (initialized_ && state_ == state) {
    return false;
  }
  if (!initialized_) {
    rectangle(0, 0, 200, 600, 3552822);
    heading(state);
    inventory(state.inventory);
    container(state.inventory);
    portal(state.portal);
    button(state.button);
    sellable(state.sellable);
    social(state.social);
    party(state.party);
    if (art_ && state.inventory.enabled && state.inventory.selected == 1) {
      stats(state.stats);
    }
    for (const auto& meter : state.bars) {
      bar(meter);
    }
    changes_.push_back({0, 0, 200, 600});
  } else {
    const bool heading_changed = state_.name != state.name ||
                                 (art_ && (state_.inventory.portrait != state.inventory.portrait ||
                                           state_.inventory.tex1 != state.inventory.tex1 ||
                                           state_.inventory.tex2 != state.inventory.tex2 ||
                                           state_.inventory.enabled != state.inventory.enabled)) ||
                                 state_.header != state.header;
    if (heading_changed) {
      rectangle(0, 190, 200, 110, 3552822);
      heading(state);
      for (const auto& meter : state.bars) {
        bar(meter);
      }
      changes_.push_back({0, 190, 200, 110});
    }
    if (art_ && (!same_lower(state_.inventory, state.inventory) || state_.portal != state.portal ||
                 state_.button != state.button || state_.sellable != state.sellable ||
                 state_.party != state.party || state_.social != state.social)) {
      rectangle(0, 500, 200, 100, 3552822);
      container(state.inventory);
      portal(state.portal);
      button(state.button);
      sellable(state.sellable);
      social(state.social);
      party(state.party);
      changes_.push_back({0, 500, 200, 100});
    }
    if (art_ && upper(state_.inventory) != upper(state.inventory)) {
      rectangle(0, 300, 200, 200, 3552822);
      inventory(state.inventory);
      if (state.inventory.enabled && state.inventory.selected == 1) {
        stats(state.stats);
      }
      changes_.push_back({0, 300, 200, 200});
    } else if (art_ && state.inventory.enabled && state.inventory.selected == 1 &&
               state_.stats != state.stats) {
      const HudRegion area{0, 373, 200, 127};
      blit(art_->image("backdrop" + std::to_string(state.inventory.backpack) + "s1",
                       image_.pixel_scale),
           0, 300, area);
      stats(state.stats);
      changes_.push_back(area);
    }
    for (std::size_t i = 0; i < state.bars.size(); ++i) {
      if (!heading_changed && state_.bars[i] != state.bars[i]) {
        if (state_.bars[i].y != state.bars[i].y) {
          throw std::runtime_error("HUD meter positions changed");
        }
        const auto& meter = state.bars[i];
        rectangle(0, meter.y - 4, 200, 24, 3552822);
        bar(meter);
        changes_.push_back({0, meter.y - 4, 200, 24});
      }
    }
  }
  state_ = state;
  initialized_ = true;
  if (changes_.empty()) {
    return false;
  }
  if (image_.pixel_scale != 1) {
    for (auto& area : changes_) {
      const double s = image_.pixel_scale;
      const int right = static_cast<int>(std::ceil((area.x + area.width) * s));
      const int bottom = static_cast<int>(std::ceil((area.y + area.height) * s));
      area.x = static_cast<int>(std::floor(area.x * s));
      area.y = static_cast<int>(std::floor(area.y * s));
      area.width = right - area.x;
      area.height = bottom - area.y;
    }
  }
  ++rebuilds_;
  return true;
}

void Hud::rectangle(double x, int y, double width, int height, std::uint32_t color) {
  if (image_.pixel_scale != 1) {
    ui_rectangle(image_, x, y, width, height, color);
    return;
  }
  for (int row = std::max(0, y); row < std::min(image_.height, y + height); ++row) {
    for (int col = std::max(0, static_cast<int>(std::floor(x)));
         col < std::min(image_.width, static_cast<int>(std::ceil(x + width))); ++col) {
      const double coverage = std::clamp(
          std::min(x + width, col + 1.0) - std::max(x, static_cast<double>(col)), 0.0, 1.0);
      const unsigned alpha = static_cast<unsigned>(std::lround(coverage * 4)) * 64U;
      const auto at = (static_cast<std::size_t>(row) * 200 + static_cast<std::size_t>(col)) * 4;
      for (unsigned c = 0; c < 3; ++c) {
        image_.pixels[at + c] = static_cast<unsigned char>(
            (((color >> (16U - 8U * c)) & 255U) * alpha +
             static_cast<unsigned>(image_.pixels[at + c]) * (256 - alpha)) >>
            8U);
      }
      image_.pixels[at + 3] = 255;
    }
  }
}

void Hud::snapped_rectangle(double x, double y, double width, double height, std::uint32_t color) {
  // Each edge rounds to a device pixel on its own, so neighbouring edges never split a device pixel
  // and the fill is opaque (equal to a fully covered ui_rectangle pixel).
  const double s = image_.pixel_scale;
  const auto edge = [](double value, int limit) {
    return std::clamp(static_cast<int>(std::lround(value)), 0, limit);
  };
  const int left = edge(x * s, image_.width);
  const int right = edge((x + width) * s, image_.width);
  const int top = edge(y * s, image_.height);
  const int bottom = edge((y + height) * s, image_.height);
  const unsigned char solid[3] = {static_cast<unsigned char>(color >> 16U),
                                  static_cast<unsigned char>(color >> 8U),
                                  static_cast<unsigned char>(color)};
  for (int row = top; row < bottom; ++row) {
    for (int col = left; col < right; ++col) {
      const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image_.width) +
                       static_cast<std::size_t>(col)) *
                      4;
      std::copy_n(solid, 3, image_.pixels.begin() + static_cast<std::ptrdiff_t>(at));
      image_.pixels[at + 3] = 255;
    }
  }
}

void Hud::text(std::string_view value, int size, bool bold, std::uint32_t color, double x, double y,
               double opacity, unsigned shadow_strength, int field_width) {
  draw(fonts_.rasterize(value, size, bold, true, color, x - std::floor(x), y - std::floor(y),
                        shadow_strength, field_width),
       x, y, opacity);
}

void Hud::draw(TextBitmap bitmap, double x, double y, double opacity,
               const ColorTransform* color_transform) {
  // Text is re-rendered at its exact device position when blitted at another density.
  double bx = x + bitmap.offset.x;
  double by = std::floor(y) + bitmap.offset.y;
  if (image_.pixel_scale != 1) {
    bitmap = place_text(bitmap, image_.pixel_scale, bx, by);
  }
  premultiply_flash_bitmap(bitmap.image);
  if (color_transform) {
    transform(bitmap.image, *color_transform);
  }
  if (image_.pixel_scale != 1) {
    ui_blit(image_, bitmap.image, bx, by, true, false, opacity);
    return;
  }
  const int ox = static_cast<int>(std::round(x + bitmap.offset.x));
  const int oy = static_cast<int>(std::floor(y) + bitmap.offset.y);
  for (int row = 0; row < bitmap.image.height; ++row) {
    for (int col = 0; col < bitmap.image.width; ++col) {
      const int px = ox + col;
      const int py = oy + row;
      if (px < 0 || py < 0 || px >= 200 || py >= 600) {
        continue;
      }
      const auto src =
          (static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.image.width) +
           static_cast<std::size_t>(col)) *
          4;
      const auto dst = (static_cast<std::size_t>(py) * 200 + static_cast<std::size_t>(px)) * 4;
      const unsigned multiplier = static_cast<unsigned>(opacity * 256);
      const unsigned alpha = static_cast<unsigned>(bitmap.image.pixels[src + 3]) * multiplier >> 8U;
      for (unsigned c = 0; c < 3; ++c) {
        image_.pixels[dst + c] = static_cast<unsigned char>(
            (static_cast<unsigned>(bitmap.image.pixels[src + c]) * multiplier >> 8U) +
            (static_cast<unsigned>(image_.pixels[dst + c]) * (256 - alpha) >> 8U));
      }
    }
  }
}

void Hud::draw_direct(const TextBitmap& bitmap, double x, double y, std::uint32_t color) {
  if (image_.pixel_scale != 1) {
    ui_blit_text(image_, bitmap, x + bitmap.offset.x, std::floor(y) + bitmap.offset.y);
    return;
  }
  const int ox = static_cast<int>(std::round(x + bitmap.offset.x));
  const int oy = static_cast<int>(std::floor(y) + bitmap.offset.y);
  for (int row = 0; row < bitmap.image.height; ++row) {
    for (int col = 0; col < bitmap.image.width; ++col) {
      const int px = ox + col;
      const int py = oy + row;
      if (px < 0 || py < 0 || px >= 200 || py >= 600) {
        continue;
      }
      const int alpha = bitmap.image.pixels[(static_cast<std::size_t>(row) *
                                                 static_cast<std::size_t>(bitmap.image.width) +
                                             static_cast<std::size_t>(col)) *
                                                4 +
                                            3];
      if (!alpha) {
        continue;
      }
      const auto dst = (static_cast<std::size_t>(py) * 200 + static_cast<std::size_t>(px)) * 4;
      for (unsigned c = 0; c < 3; ++c) {
        const int to = static_cast<int>((color >> (16U - 8U * c)) & 255U);
        const int from = image_.pixels[dst + c];
        image_.pixels[dst + c] =
            static_cast<unsigned char>(from + ((to - from) * (alpha + 1) >> 8));
      }
    }
  }
}

void Hud::bar(const StatusBarState& state) {
  const int value = state.maximum > 0 ? std::clamp(state.value, 0, state.maximum) : state.value;
  const auto color =
      state.cap > 0 && static_cast<std::int64_t>(state.maximum) - state.boost == state.cap
          ? 16572160U
      : state.boost > 0 ? 6206769U
                        : 0xffffffU;
  // Track and fill snap to whole device pixels, so their edges are hard on every side at fractional
  // UI scales.
  snapped_rectangle(12, state.y, 176, 16, 5526612);
  snapped_rectangle(12, state.y, state.maximum > 0 ? 176.0 * value / state.maximum : 176, 16,
                    state.color);
  text(state.label, 14, true, 0xffffff, 12, state.y - 3);
  const auto amount =
      std::to_string(value) + (state.maximum > 0 ? "/" + std::to_string(state.maximum) : "");
  const auto boost = state.boost ? " (" + std::string(state.boost > 0 ? "+" : "") +
                                       std::to_string(state.boost) + ")"
                                 : "";
  const auto amount_width = fonts_.layout(amount, 14, true).width;
  const auto boost_width = boost.empty() ? 0 : fonts_.layout(boost, 14, true).width;
  const double ax = 12 + 88 - (amount_width + boost_width) / 2;
  text(amount, 14, true, color, ax, state.y - 3);
  if (!boost.empty()) {
    text(boost, 14, true, color, ax + amount_width, state.y - 3, .6);
  }
}
}
