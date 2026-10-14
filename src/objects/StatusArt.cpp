#include "realm/objects/StatusArt.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
int texture_height(const ObjectDef& definition) {
  const auto& texture = definition.texture;
  if (texture.animated >= 0 && static_cast<std::size_t>(texture.animated) < flash::animated.size())
    return flash::animated[static_cast<std::size_t>(texture.animated)].height;
  if (texture.sheet >= 0 && static_cast<std::size_t>(texture.sheet) < flash::sheets.size())
    return flash::sheets[static_cast<std::size_t>(texture.sheet)].height;
  return 8;
}
}

StatusArt::Label StatusArt::label(std::string key, std::string_view text, int size, std::uint32_t color,
                                  unsigned strength, double blur) {
  if (!text_) return {};
  key += ':' + std::to_string(fonts_.pixel_scale());
  if (const auto it = labels_.find(key); it != labels_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return it->second.label;
  }
  const auto bitmap = fonts_.rasterize_glow(text, size, true, color, strength, blur);
  Label result{std::make_shared<const Image>(bitmap.image), bitmap.width, bitmap.height, bitmap.offset};
  const std::size_t bytes = result.image->pixels.size();
  while (!labels_.empty() && (labels_.size() >= maximum_labels || bytes_ + bytes > maximum_bytes)) {
    const auto victim = labels_.find(lru_.back());
    bytes_ -= victim->second.label.image->pixels.size();
    labels_.erase(victim);
    lru_.pop_back();
    ++evictions_;
  }
  lru_.push_front(key);
  labels_.emplace(std::move(key), Entry{result, lru_.begin()});
  bytes_ += bytes;
  return result;
}
StatusArt::Label StatusArt::status_text(std::string_view text, std::uint32_t color) {
  color &= 0xffffffU;
  // CharacterStatusText: GlowFilter(0,1,4,4,2,1), blur 4 and strength 2.
  return label("t:" + std::to_string(color) + ':' + std::string(text), text, 24, color, 2, 4);
}
StatusArt::Label StatusArt::name(std::string_view text) {
  return label("n:" + std::string(text), text, 16, 0xffffffU, 2, 2);
}
StatusArt::Label StatusArt::sign_text(std::string_view text, int object_size) {
  if (!text_) return {};
  const std::string key =
      "s:" + std::to_string(object_size) + ':' + std::string(text) + ':' + std::to_string(fonts_.pixel_scale());
  if (const auto it = labels_.find(key); it != labels_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return it->second.label;
  }
  std::string multiline(text);
  std::replace(multiline.begin(), multiline.end(), '|', '\n');
  double widest{};
  for (std::size_t start = 0; start <= multiline.size();) {
    const auto newline = multiline.find('\n', start);
    const auto end = newline == std::string::npos ? multiline.size() : newline;
    widest = std::max(widest, fonts_.measure(std::string_view(multiline).substr(start, end - start), 24, true));
    if (newline == std::string::npos) break;
    start = newline + 1;
  }
  const int field_width = std::clamp(static_cast<int>(std::ceil(widest)) + 4, 4, 8192);
  const auto bitmap = fonts_.rasterize_centered(multiline, 24, true, false, 0xffffffU, field_width);
  Label result{std::make_shared<const Image>(bitmap.image), bitmap.width, bitmap.height, bitmap.offset};
  const std::size_t bytes = result.image->pixels.size();
  while (!labels_.empty() && (labels_.size() >= maximum_labels || bytes_ + bytes > maximum_bytes)) {
    const auto victim = labels_.find(lru_.back());
    bytes_ -= victim->second.label.image->pixels.size();
    labels_.erase(victim);
    lru_.pop_back();
    ++evictions_;
  }
  lru_.push_front(key);
  labels_.emplace(key, Entry{result, lru_.begin()});
  bytes_ += bytes;
  return result;
}
std::shared_ptr<const Image> StatusArt::icon(int frame) {
  if (!appearances_) return {};
  auto& slot = icons_[frame];
  if (!slot) slot = appearances_->condition_icon(frame);
  return slot;
}
void StatusArt::clear() {
  labels_.clear();
  lru_.clear();
  icons_.clear();
  bytes_ = 0;
}

void StatusTexts::add(const World& world, StatusEvent event) {
  const auto* object = world.entity(event.object_id);
  if (!object) return;
  StatusText text;
  text.object_id = event.object_id;
  text.text = std::move(event.text);
  text.color = event.color;
  text.lifetime = std::max(1, event.lifetime);
  text.offset_time = event.offset_time;
  // The constructor reads the texture of the object when the text is created.
  if (const auto* definition = world.catalog().object(object->type)) text.texture_height = texture_height(*definition);
  if (texts_.size() >= capacity) texts_.erase(texts_.begin());
  texts_.push_back(std::move(text));
}
void StatusTexts::update(const World& world, StatusArt& art, std::int32_t time) {
  std::erase_if(texts_, [&](StatusText& text) {
    if (!world.entity(text.object_id)) return true;
    if (!text.started) {
      text.started = true;
      text.start = time + text.offset_time;
    }
    if (time < text.start) return false;
    if (static_cast<std::int64_t>(time) - text.start > text.lifetime) return true;
    if (!text.label.image || text.label_scale != art.scale()) {
      text.label = art.status_text(text.text, text.color);
      text.label_scale = art.scale();
    }
    return false;
  });
}
Vec2 StatusTexts::position(const StatusText& text, Vec2 object_screen, int object_size, double zoom,
                           std::int32_t time) {
  // offset_ = (0, -texture.height * size / 100 * 5 - 20); drift = dt / lifetime * 40 upward;
  // the label is centered on that point.
  const double drift = static_cast<double>(time - text.start) / text.lifetime * max_drift;
  const double y = -text.texture_height * (object_size / 100.0) * 5 - 20 - drift;
  return {object_screen.x + (-text.label.width / 2 + text.label.offset.x) * zoom,
          object_screen.y + (y - text.label.height / 2 + text.label.offset.y) * zoom};
}

std::uint32_t breath_bar_back_color(int breath, std::int32_t time_ms) {
  constexpr std::uint32_t from = 0x545454U, to = 0xff0000U;
  if (breath > 20) return from;
  const double progress = std::abs(std::sin(time_ms / 300.0)) * (20 - breath) / 20.0, q = 1 - progress;
  std::uint32_t result = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const double a = (from >> shift) & 255U, b = (to >> shift) & 255U;
    result |= static_cast<std::uint32_t>(a * q + b * progress) << shift;
  }
  return result;
}
double low_health_pulse(std::int32_t time_ms) {
  return static_cast<int>(std::abs(std::sin(time_ms / 200.0)) * 10) / 10.0;
}
}
