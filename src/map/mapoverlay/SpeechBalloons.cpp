#include "realm/map/mapoverlay/SpeechBalloons.hpp"
#include "realm/ui/Chat.hpp"
#include "realm/ui/GraphicsUtil.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace realm {
namespace {
int texture_height(const ObjectDef& definition) {
  const auto& texture = definition.texture;
  if (texture.animated >= 0 &&
      static_cast<std::size_t>(texture.animated) < flash::animated.size()) {
    return flash::animated[static_cast<std::size_t>(texture.animated)].height;
  }
  if (texture.sheet >= 0 && static_cast<std::size_t>(texture.sheet) < flash::sheets.size()) {
    return flash::sheets[static_cast<std::size_t>(texture.sheet)].height;
  }
  return 8;
}
}

void SpeechBalloons::add(const World& world, const net::incoming::Text& message) {
  if (message.object_id < 0) {
    return;
  }
  const auto* object = world.entity(message.object_id);
  if (!object) {
    return;
  }
  const auto* definition = world.catalog().object(object->type);
  Balloon balloon;
  balloon.object_id = message.object_id;
  balloon.lifetime = message.bubble_time * 1000;
  balloon.text = message.text;
  // Normal, enemy, tell and guild color triples from onText.
  if (message.recipient == guild_chat_name) {
    balloon.background = 0x3e8a00;
    balloon.outline = 0xa6ff5d;
    balloon.color = 0xd3f7cc;
  } else if (!message.recipient.empty()) {
    balloon.background = 0x260ab6;
    balloon.outline = 0x00f0ff;
    balloon.color = 0xd3ccf7;
  } else if (definition && definition->enemy) {
    balloon.background = 0x561f1c;
    balloon.outline = 0xfc8642;
    balloon.color = 0xcdc0bf;
  } else {
    balloon.background = 0xe1dfdc;
    balloon.outline = 0xffffff;
    balloon.color = 0x545454;
  }
  if (definition) {
    balloon.texture_height = texture_height(*definition);
  }
  std::erase_if(balloons_,
                [&](const Balloon& existing) { return existing.object_id == balloon.object_id; });
  if (balloons_.size() >= capacity) {
    balloons_.erase(balloons_.begin());
  }
  // Reuse the lowest free GPU slot.
  std::vector<bool> used(capacity);
  for (const auto& existing : balloons_) {
    used[existing.slot] = true;
  }
  balloon.slot =
      static_cast<std::size_t>(std::find(used.begin(), used.end(), false) - used.begin());
  balloons_.push_back(std::move(balloon));
}

void SpeechBalloons::update(const World& world, const FontBook& fonts, std::int32_t time) {
  std::erase_if(balloons_, [&](const Balloon& b) {
    return !world.entity(b.object_id) || (b.start >= 0 && time - b.start > b.lifetime);
  });
  for (auto& balloon : balloons_) {
    if (balloon.start < 0) {
      balloon.start = time;
    }
    if (balloon.image.pixels.empty() || balloon.image.pixel_scale != fonts.pixel_scale()) {
      render(balloon, fonts);
    }
  }
}

void SpeechBalloons::render(Balloon& balloon, const FontBook& fonts) {
  // A 150-pixel wordWrap TextField with autoSize LEFT, regular 14-pixel text.
  std::string joined;
  const auto lines = fonts.wrap(balloon.text, 14, false, 150);
  for (const auto& line : lines) {
    if (!joined.empty()) {
      joined += '\n';
    }
    joined += line;
  }
  const auto run = fonts.layout(joined, 14);
  // textHeight is 16.8 pixels per regular 14-pixel Myriad line; autoSize adds 4.
  const double w = std::floor(run.width);
  const double h = 16.8 * static_cast<double>(std::max<std::size_t>(1, lines.size())) + 4;
  balloon.field_width = w;
  balloon.field_height = h;
  balloon.full_height = std::floor(h + 26);
  // drawCutEdgeRect(-6,-6,w+12,h+12,4) with the tail spliced into its bottom edge.
  const double l = -6;
  const double t = -6;
  const double r = w + 6;
  const double b = h + 6;
  const std::array<Vec2, 11> path{{{l + 4, t},
                                   {r - 4, t},
                                   {r, t + 4},
                                   {r, b - 4},
                                   {r - 4, b},
                                   {w / 2 + 8, h + 6},
                                   {w / 2, h + 18},
                                   {w / 2 - 8, h + 6},
                                   {l + 4, b},
                                   {l, b - 4},
                                   {l, t + 4}}};
  balloon.image = ui_image(w + margin * 2, h + 18 + margin * 2, fonts.pixel_scale());
  auto& image = balloon.image;
  const double s = image.pixel_scale;
  // Shift the path into the image's margin.
  std::array<Vec2, 11> shifted{};
  for (std::size_t i = 0; i < path.size(); ++i) {
    shifted[i] = {path[i].x + margin, path[i].y + margin};
  }
  ui_path(image, shifted, balloon.background, balloon.outline, 2);
  if (!joined.empty()) {
    const auto bitmap = fonts.rasterize(joined, 14, false, false, balloon.color);
    ui_blit_text(image, bitmap, margin + bitmap.offset.x, margin + bitmap.offset.y, false, true);
  }
  // SpeechBalloon's DropShadowFilter(0,0,0,1,16,16).
  ui_shadow(image, 8, image.width / s, image.height / s);
  balloon.revision = ++revisions_;
}

Vec2 SpeechBalloons::position(const Balloon& balloon, Vec2 object_screen, int object_size,
                              double scale) {
  // offset_ = (-w/2, -height - texture.height*size/100*5 - 2), truncated to whole pixels.
  const double x = std::trunc(object_screen.x / scale - balloon.field_width / 2);
  const double y = std::trunc(object_screen.y / scale - balloon.full_height -
                              balloon.texture_height * (object_size / 100.0) * 5 - 2);
  return {(x - margin) * scale, (y - margin) * scale};
}
}
