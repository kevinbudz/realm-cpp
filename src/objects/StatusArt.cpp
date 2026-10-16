#include "realm/objects/StatusArt.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

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

int texture_width(const ObjectDef& definition) {
  const auto& texture = definition.texture;
  if (texture.animated >= 0 &&
      static_cast<std::size_t>(texture.animated) < flash::animated.size()) {
    return flash::animated[static_cast<std::size_t>(texture.animated)].width;
  }
  if (texture.sheet >= 0 && static_cast<std::size_t>(texture.sheet) < flash::sheets.size()) {
    return flash::sheets[static_cast<std::size_t>(texture.sheet)].width;
  }
  return 8;
}

// SplitMix64 finalizer: well-mixed bits from a key, so randomized texts need no RNG state.
std::uint64_t mix(std::uint64_t value) {
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

// The key of a randomized text: its object, the time it was reported and its sequence number.
// The same events in the same order give the same scatter, so a fixed clock repeats its frames.
std::uint64_t draw_seed(const StatusEvent& event, std::uint32_t sequence) {
  const auto object_time =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(event.object_id)) << 32U) |
      static_cast<std::uint32_t>(event.time_ms);
  return mix(mix(object_time) + sequence);
}

// RandomUtil.plusMinus(range), deterministic: uniform in [-range, range) for one salted draw.
double plus_minus(std::uint64_t seed, std::uint64_t salt, double range) {
  const std::uint64_t bits = mix(seed + salt * 0x9e3779b97f4a7c15ULL) >> 11U;
  return (static_cast<double>(bits) / 9007199254740992.0 * 2 - 1) * range;
}
}

StatusArt::Label StatusArt::label(std::string key, std::string_view text, int size,
                                  std::uint32_t color, unsigned strength, double blur,
                                  bool outline) {
  if (!text_) {
    return {};
  }
  key += ':' + std::to_string(fonts_.pixel_scale());
  if (const auto it = labels_.find(key); it != labels_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return it->second.label;
  }
  const auto bitmap = outline ? fonts_.rasterize(text, size, true, TextEffect::outline, color)
                              : fonts_.rasterize_glow(text, size, true, color, strength, blur);
  Label result{std::make_shared<const Image>(bitmap.image), bitmap.width, bitmap.height,
               bitmap.offset};
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
  // CharacterStatusText: the black outline in place of its GlowFilter(0,1,4,4,2,1).
  return label("t:" + std::to_string(color) + ':' + std::string(text), text, 24, color, 0, 0, true);
}

StatusArt::Label StatusArt::name(std::string_view text) {
  // GameObject.drawName: the black outline in place of its GlowFilter(0,1,3,3,2,1).
  return label("n:" + std::string(text), text, 16, 0xffffffU, 0, 0, true);
}

StatusArt::Label StatusArt::sign_text(std::string_view text, int object_size) {
  if (!text_) {
    return {};
  }
  const std::string key = "s:" + std::to_string(object_size) + ':' + std::string(text) + ':' +
                          std::to_string(fonts_.pixel_scale());
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
    widest = std::max(
        widest, fonts_.measure(std::string_view(multiline).substr(start, end - start), 24, true));
    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }
  const int field_width = std::clamp(static_cast<int>(std::ceil(widest)) + 4, 4, 8192);
  const auto bitmap = fonts_.rasterize_centered(multiline, 24, true, false, 0xffffffU, field_width);
  Label result{std::make_shared<const Image>(bitmap.image), bitmap.width, bitmap.height,
               bitmap.offset};
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
  if (!appearances_) {
    return {};
  }
  auto& slot = icons_[frame];
  if (!slot) {
    slot = appearances_->condition_icon(frame);
  }
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
  if (!object) {
    return;
  }
  StatusText text;
  text.object_id = event.object_id;
  text.text = std::move(event.text);
  text.color = event.color;
  text.lifetime = std::max(1, event.lifetime);
  text.offset_time = event.offset_time;
  // The constructor reads the texture of the object when the text is created.
  int width = 8;
  if (const auto* definition = world.catalog().object(object->type)) {
    text.texture_height = texture_height(*definition);
    width = texture_width(*definition);
  }
  if (event.randomized) {
    // randomized=true: offset_.x = plusMinus(texture.width / 2), driftDirection_ = plusMinus(20).
    const auto seed = draw_seed(event, sequence_);
    text.randomized = true;
    text.offset_x = plus_minus(seed, 1, width / 2.0);
    text.drift_x = plus_minus(seed, 2, randomized_drift);
  }
  ++sequence_;
  if (texts_.size() >= capacity) {
    texts_.erase(texts_.begin());
  }
  texts_.push_back(std::move(text));
}

void StatusTexts::update(const World& world, StatusArt& art, std::int32_t time) {
  std::erase_if(texts_, [&](StatusText& text) {
    if (!world.entity(text.object_id)) {
      return true;
    }
    if (!text.started) {
      text.started = true;
      text.start = time + text.offset_time;
    }
    if (time < text.start) {
      return false;
    }
    if (static_cast<std::int64_t>(time) - text.start > text.lifetime) {
      return true;
    }
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
  const double fraction = static_cast<double>(time - text.start) / text.lifetime;
  const double drift = fraction * max_drift;
  const double y = -text.texture_height * (object_size / 100.0) * 5 - 20 - drift;
  // randomized: x = offset.x + drift away from the center, drift = dt / lifetime * drift_x.
  double x = 0;
  if (text.randomized) {
    x = text.offset_x + (text.offset_x > 0 ? 1 : -1) * fraction * text.drift_x;
  }
  return {object_screen.x + (x - text.label.width / 2 + text.label.offset.x) * zoom,
          object_screen.y + (y - text.label.height / 2 + text.label.offset.y) * zoom};
}

double StatusTexts::opacity(const StatusText& text, std::int32_t time) {
  if (!text.randomized) {
    return 1;
  }
  // alpha = 1.0 / (drift / 15.0) with drift = dt / lifetime * 20; Flash clamps it to 1.
  const double drift = static_cast<double>(time - text.start) / text.lifetime * randomized_drift;
  return drift <= 15 ? 1 : 15 / drift;
}

std::uint32_t breath_bar_back_color(int breath, std::int32_t time_ms) {
  constexpr std::uint32_t from = 0x545454U;
  constexpr std::uint32_t to = 0xff0000U;
  if (breath > 20) {
    return from;
  }
  const double progress = std::abs(std::sin(time_ms / 300.0)) * (20 - breath) / 20.0;
  const double remaining = 1 - progress;
  std::uint32_t result = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const double source = (from >> shift) & 255U;
    const double target = (to >> shift) & 255U;
    result |= static_cast<std::uint32_t>(source * remaining + target * progress) << shift;
  }
  return result;
}

double low_health_pulse(std::int32_t time_ms) {
  return static_cast<int>(std::abs(std::sin(time_ms / 200.0)) * 10) / 10.0;
}
}
