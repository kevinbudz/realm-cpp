#pragma once
#include "realm/objects/Party.hpp"
#include "realm/util/Image.hpp"
#include <optional>

namespace realm {
struct PartyArrow {
  Vec2 position{};
  double rotation{};  // radians, the triangle points toward the member
  std::vector<std::int32_t> players;
  bool hover{};
};
// Six retained, ordered ID groups. No Entity pointers survive a frame.
class PartyArrows {
 public:
  static constexpr int canvas_size = 48, origin = 24;
  void reset() {
    arrows_ = {};
    hovered_ = -1;
    width_ = height_ = 0;
  }
  void cancel_hover() {
    hovered_ = -1;
    for (auto& a : arrows_) a.hover = false;
  }
  void update(const World& world, const Party& party, const Camera& camera, Vec2 pointer, bool valid, int width,
              int height);
  [[nodiscard]] const auto& arrows() const { return arrows_; }
  [[nodiscard]] const PartyArrow* hovered() const {
    return hovered_ >= 0 ? &arrows_[static_cast<std::size_t>(hovered_)] : nullptr;
  }
  [[nodiscard]] const PartyArrow* hit(Vec2 pointer) const;
  [[nodiscard]] static std::optional<PartyArrow> project(const Camera& camera, Vec2 target);
  [[nodiscard]] static bool drawn(const World& world, const Entity& player, const Camera& camera, int height);
  [[nodiscard]] static Image image(double rotation, bool hover, double density);

 private:
  [[nodiscard]] bool contains(const PartyArrow& arrow, Vec2 pointer) const;
  std::array<PartyArrow, Party::members_shown> arrows_{};
  double scale_{1};
  int hovered_{-1}, width_{}, height_{};
};
}
