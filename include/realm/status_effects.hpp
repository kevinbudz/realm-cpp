#pragma once
#include "realm/appearance.hpp"
#include "realm/conditions.hpp"
#include "realm/font.hpp"
#include "realm/world.hpp"
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace realm {
// Raster art for world-space status cues: CharacterStatusText (24 pt bold, black
// glow), object names (16 pt bold white, black glow) and condition icons. All of
// it is drawn in map pixels and scales with the world zoom, like speech balloons.
// Labels are rasterized at the font book's current density and kept in a bounded LRU.
class StatusArt {
 public:
  static constexpr std::size_t maximum_labels = 4096;
  static constexpr std::size_t maximum_bytes = 64 * 1024 * 1024;
  struct Label {
    std::shared_ptr<const Image> image;
    // Text field size in map pixels, and where the image starts relative to the field origin.
    double width{}, height{};
    Vec2 offset{};
  };
  // Without text, labels are empty and only icons are drawn.
  StatusArt(const FontBook& fonts, const Appearances* appearances, bool text = true)
      : fonts_(fonts), appearances_(appearances), text_(text) {}
  [[nodiscard]] Label status_text(std::string_view text, std::uint32_t color);
  [[nodiscard]] Label name(std::string_view text);
  [[nodiscard]] std::shared_ptr<const Image> icon(int frame);
  [[nodiscard]] double scale() const { return fonts_.pixel_scale(); }
  [[nodiscard]] std::size_t entries() const { return labels_.size(); }
  [[nodiscard]] std::size_t bytes() const { return bytes_; }
  [[nodiscard]] std::uint64_t evictions() const { return evictions_; }
  void clear();

 private:
  struct Entry {
    Label label;
    std::list<std::string>::iterator lru;
  };
  Label label(std::string key, std::string_view text, int size, std::uint32_t color, unsigned strength);
  const FontBook& fonts_;
  const Appearances* appearances_;
  bool text_{true};
  std::list<std::string> lru_;
  std::unordered_map<std::string, Entry> labels_;
  std::unordered_map<int, std::shared_ptr<const Image>> icons_;
  std::size_t bytes_{};
  std::uint64_t evictions_{};
};

struct StatusText {
  std::int32_t object_id{};
  std::string text;
  std::uint32_t color{};
  int lifetime{}, offset_time{};
  // First update after creation sets the clock, as CharacterStatusText.startTime_.
  bool started{};
  std::int32_t start{};
  // Source texture height of the object's sprite (the text floats above it).
  int texture_height{8};
  StatusArt::Label label;
  double label_scale{};
};

// MapOverlay's CharacterStatusText list.
class StatusTexts {
 public:
  static constexpr std::size_t capacity = 48;
  static constexpr double max_drift = 40;
  void add(const World& world, StatusEvent event);
  void clear() { texts_.clear(); }
  // Starts new timers, rasterizes visible labels, drops expired texts and those whose object left.
  void update(const World& world, StatusArt& art, std::int32_t time);
  [[nodiscard]] const std::vector<StatusText>& texts() const { return texts_; }
  [[nodiscard]] static bool visible(const StatusText& text, std::int32_t time) {
    return text.started && time >= text.start;
  }
  // Top-left of the label image in screen pixels for the object's ground position on screen.
  [[nodiscard]] static Vec2 position(const StatusText& text, Vec2 object_screen, int object_size, double zoom,
                                     std::int32_t time);

 private:
  std::vector<StatusText> texts_;
};

// Player.drawBreathBar: back color lerps from 0x545454 to red with |sin(t/300)| * b below the threshold.
[[nodiscard]] std::uint32_t breath_bar_back_color(int breath, std::int32_t time_ms);
// Player.getTexture's low-health pulse: rv = int(|sin(t/200)| * 10) / 10 of the 128 offset.
[[nodiscard]] double low_health_pulse(std::int32_t time_ms);
}
