#include "realm/ui/tooltip/EquipmentToolTip.hpp"
#include "realm/ui/tooltip/TextToolTip.hpp"
#include "realm/util/GraphicsUtil.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
constexpr std::uint32_t body_color = 0xb3b3b3;
const ObjectDef* item_definition(const Catalog& catalog, int type) {
  return type > 0 && type <= 65535 ? catalog.object(static_cast<std::uint16_t>(type)) : nullptr;
}
std::string plain(const RichLine& line) {
  std::string result;
  for (const auto& span : line.spans) result += span.text;
  return result;
}
// Wrap complete paragraphs, then slice the original colored spans at the same byte offsets.
std::vector<RichLine> wrap(const FontBook& fonts, std::span<const RichLine> paragraphs, int size, int field_width,
                           bool indent = false) {
  std::vector<RichLine> result;
  for (const auto& paragraph : paragraphs) {
    const auto text = plain(paragraph);
    std::size_t offset{};
    const int available = field_width - static_cast<int>(paragraph.x);
    auto parts = fonts.wrap(text, size, paragraph.bold, available);
    if (indent && parts.size() > 1) {
      auto continuation =
          fonts.wrap(std::string_view(text).substr(parts.front().size()), size, paragraph.bold, available - 10);
      parts.resize(1);
      parts.insert(parts.end(), continuation.begin(), continuation.end());
    }
    std::size_t line_index{};
    for (const auto& part : parts) {
      RichLine line;
      line.bold = paragraph.bold;
      line.x = paragraph.x + (indent && line_index++ ? 10 : 0);
      std::size_t start{};
      for (const auto& span : paragraph.spans) {
        const auto from = std::max(offset, start), to = std::min(offset + part.size(), start + span.text.size());
        if (to > from) line.spans.push_back({span.text.substr(from - start, to - from), span.color});
        start += span.text.size();
      }
      result.push_back(std::move(line));
      offset += part.size();
      if (offset < text.size() && text[offset] == '\n') ++offset;
    }
  }
  return result;
}
RichLine paragraph(std::string text, std::uint32_t color, bool bold = false) {
  return {{{std::move(text), color}}, bold, 0};
}
}
bool ItemTooltip::update(const std::optional<ItemTooltipTarget>& target) {
  if (!target || !item_definition(catalog_, target->item.type)) {
    target_.reset();
    return false;
  }
  if (target_ == target && image_.pixel_scale == fonts_.pixel_scale()) return false;
  target_ = target;
  render(*target);
  ++rebuilds_;
  return true;
}
void ItemTooltip::blit(const Image& source, int x, int y, bool premultiplied) {
  x += padding;
  y += padding;
  if (image_.pixel_scale != 1) {
    ui_blit(image_, source, x, y, premultiplied, true);
    return;
  }
  for (int row = std::max(0, -y); row < std::min(source.height, image_.height - y); ++row)
    for (int col = std::max(0, -x); col < std::min(source.width, width - x); ++col) {
      const auto src = static_cast<std::size_t>(row * source.width + col) * 4,
                 dst = static_cast<std::size_t>((row + y) * width + col + x) * 4;
      const unsigned alpha = source.pixels[src + 3];
      if (!alpha) continue;
      for (unsigned c = 0; c < 3; ++c) {
        const unsigned value =
            premultiplied || alpha == 255 ? source.pixels[src + c] : (source.pixels[src + c] * alpha + 255U) >> 8U;
        image_.pixels[dst + c] =
            static_cast<unsigned char>(std::min(255U, value + (image_.pixels[dst + c] * (256 - alpha) >> 8U)));
      }
      image_.pixels[dst + 3] =
          static_cast<unsigned char>(std::min(255U, alpha + (image_.pixels[dst + 3] * (256 - alpha) >> 8U)));
    }
}
void ItemTooltip::text(std::span<const RichLine> lines, int size, bool bold, int field_width, double field_height,
                       double x, double y) {
  const double snapped = std::round(y * 4) / 4, phase = snapped - std::floor(snapped);
  auto bitmap = fonts_.rasterize_rich(lines, size, bold, field_width, field_height, phase, TextFilter::soft_shadow);
  if (image_.pixel_scale != 1)
    ui_blit_text(image_, bitmap, padding + x + bitmap.offset.x, padding + std::floor(snapped) + bitmap.offset.y, false,
                 true);
  else
    blit(bitmap.image, static_cast<int>(x + bitmap.offset.x), static_cast<int>(std::floor(snapped) + bitmap.offset.y),
         false);
}
void ItemTooltip::render(const ItemTooltipTarget& target) {
  const auto content = item_tooltip_content(catalog_, target);
  const auto icon = "astrumtipicon" + std::to_string(target.item.type);
  const int icon_height = art_.contains(icon) ? art_.image(icon).height : 50;
  const int icon_width = art_.contains(icon) ? art_.image(icon).width : 50;
  constexpr int body_width = content_width - 10;
  const int title_width = std::max(30, content_width - icon_width - 30);
  const auto title = wrap(fonts_, std::array{paragraph(content.title, content.title_color, true)}, 17, title_width);
  const auto description = wrap(fonts_, std::array{paragraph(content.description, body_color)}, 14, body_width);
  const auto custom = wrap(fonts_, content.custom, 14, body_width);
  const auto effects = wrap(fonts_, content.effects, 14, body_width, true);
  const auto restrictions = wrap(fonts_, content.restrictions, 14, body_width, true);
  const auto field_height = [](const auto& lines, int size) {
    return lines.empty() ? 0.0 : static_cast<double>(lines.size()) * size * 1.2 + 4;
  };
  const double title_height = field_height(title, 17);
  const double title_y = (icon_height - title_height) / 4 - 3;
  const double origin = -std::min(0.0, title_y);
  const double speciality_y = title_y + title_height - 4;
  const double description_y = std::max(static_cast<double>(icon_height), speciality_y + 20.8) + 3;
  const double description_height = field_height(description, 14);
  const double line1 = description_y + description_height + 4;
  const double custom_y = line1 + 5;
  const double effects_y = custom.empty() ? custom_y : custom_y + field_height(custom, 14) - 3;
  const double attributes_bottom =
      effects.empty() ? custom.empty() ? description_y + description_height : custom_y + field_height(custom, 14)
                      : effects_y + field_height(effects, 14);
  const double line2 = attributes_bottom + 4, restrictions_y = line2 + 5;
  const double bottom = restrictions.empty() ? attributes_bottom : restrictions_y + field_height(restrictions, 14);
  content_height_ = static_cast<int>(std::ceil(bottom + origin));
  image_ = ui_image(width, height(), fonts_.pixel_scale());
  const bool usable_frame = target.usable || target.player_type == 0;
  ui_path(image_, cut_edge_rect(padding - 6, padding - 6, content_width + 12, content_height_ + 12, 4),
          usable_frame ? 0x363636 : 6036765, usable_frame ? 0x9b9b9b : 10965039, 1);
  if (art_.contains(icon)) blit(art_.image(icon), 0, static_cast<int>(origin), true);
  text(title, 17, true, title_width, title_height, icon_width, title_y + origin);
  const std::array speciality{paragraph(content.speciality, content.speciality_color)};
  text(speciality, 14, false, title_width, 20.8, icon_width, speciality_y + origin);
  const double tier_width = content.tier.empty() ? 0 : fonts_.layout(content.tier, 16, true).width;
  if (!content.tier.empty()) {
    const std::array tier{paragraph(content.tier, content.tier_color, true)};
    text(tier, 16, true, static_cast<int>(std::ceil(tier_width)), 23.2, content_width - tier_width, title_y + origin);
  }
  const auto bag = "tipbag" + std::to_string(content.bag_type);
  if (art_.contains(bag)) {
    const auto& bitmap = art_.image(bag);
    const double x = content.tier.empty() ? content_width - bitmap.width
                                          : content_width - tier_width - (bitmap.width - tier_width) / 2;
    const double y = content.tier.empty() ? (icon_height - bitmap.height) / 2.0 : title_y + 23.2 - 9;
    blit(bitmap, static_cast<int>(x), static_cast<int>(y + origin), true);
  }
  text(description, 14, false, body_width, description_height, 5, description_y + origin);
  auto divider = [&](double y) {
    ui_rectangle(image_, padding + 2, padding + y + origin - 1, content_width - 2, 2,
                 usable_frame ? 0x828282 : 10965039, true);
  };
  if (!custom.empty() || !effects.empty()) divider(line1);
  if (!custom.empty()) text(custom, 14, false, body_width, field_height(custom, 14), 5, custom_y + origin);
  if (!effects.empty()) text(effects, 14, false, body_width, field_height(effects, 14), 5, effects_y + origin);
  if (!restrictions.empty()) {
    divider(line2);
    text(restrictions, 14, false, body_width, field_height(restrictions, 14), 5, restrictions_y + origin);
  }
  shadow();
}
void ItemTooltip::shadow() {
  if (image_.pixel_scale != 1) {
    ui_shadow(image_, 8, width, height());
    return;
  }
  const int w = width, h = height();
  std::vector<unsigned char> horizontal(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned sum{};
      for (int k = -8; k <= 8; ++k)
        if (x + k >= 0 && x + k < w)
          sum += (std::abs(k) == 8 ? 1U : 2U) * image_.pixels[static_cast<std::size_t>(y * w + x + k) * 4 + 3];
      horizontal[static_cast<std::size_t>(y * w + x)] = static_cast<unsigned char>(sum >> 5U);
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned sum{};
      for (int k = -8; k <= 8; ++k)
        if (y + k >= 0 && y + k < h)
          sum += (std::abs(k) == 8 ? 1U : 2U) * horizontal[static_cast<std::size_t>((y + k) * w + x)];
      auto& alpha = image_.pixels[static_cast<std::size_t>(y * w + x) * 4 + 3];
      alpha = static_cast<unsigned char>(alpha + ((sum >> 5U) * (256U - alpha) >> 8U));
    }
  for (std::size_t at = 0; at < image_.pixels.size(); at += 4)
    if (const unsigned alpha = image_.pixels[at + 3]; alpha && alpha != 255)
      for (unsigned c = 0; c < 3; ++c)
        image_.pixels[at + c] = static_cast<unsigned char>(std::min(255U, image_.pixels[at + c] * 256U / alpha));
}
Vec2 ItemTooltip::position(Vec2 pointer, int window_width, int window_height, int tooltip_height, double scale) {
  const auto at = text_tooltip_position(pointer, content_width + 12, tooltip_height - padding * 2 + 12, window_width,
                                        window_height, scale);
  return {at.x - (padding - 6) * scale, at.y - (padding - 6) * scale};
}
}
