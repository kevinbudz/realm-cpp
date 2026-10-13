#pragma once
#include "realm/map/Camera.hpp"
#include "realm/map/Map.hpp"
#include "realm/messaging/Messages.hpp"
#include "realm/ui/fonts/FontBook.hpp"
#include "realm/util/Image.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace realm {
// SpeechBalloon/MapOverlay: one balloon per speaking object above its sprite.
// Balloons live in map pixels; they scale with the world zoom, not the UI.
class SpeechBalloons {
 public:
  static constexpr std::size_t capacity = 32;
  struct Balloon {
    std::int32_t object_id{-1};
    // Starts on the first frame the balloon is drawn, as SpeechBalloon.draw.
    std::int32_t start{-1}, lifetime{};
    std::string text;
    std::uint32_t background{}, outline{}, color{};
    // Source texture height of the object's sprite, for the height offset.
    int texture_height{8};
    // GPU slot, and a revision bumped on every rasterization.
    std::size_t slot{};
    std::uint64_t revision{};
    Image image;
    // Text field size and the field origin inside the image, in map pixels.
    double field_width{}, field_height{}, full_height{};
  };
  // GameServerConnection.onText: objects in the map get a balloon colored by
  // sender kind; a new balloon replaces the object's previous one.
  void add(const World& world, const net::incoming::Text& message);
  void clear() { balloons_.clear(); }
  // Rasterizes new balloons at the font's current density and drops expired
  // ones or those whose object left the map.
  void update(const World& world, const FontBook& fonts, std::int32_t time);
  [[nodiscard]] const std::vector<Balloon>& balloons() const { return balloons_; }
  // Top-left of the image in screen pixels for an object's ground position
  // on screen, with the map scale (pixels per map pixel).
  [[nodiscard]] static Vec2 position(const Balloon& balloon, Vec2 object_screen, int object_size, double scale);
  static constexpr double margin = 17;

 private:
  void render(Balloon& balloon, const FontBook& fonts);
  std::vector<Balloon> balloons_;
  std::uint64_t revisions_{};
};
}
