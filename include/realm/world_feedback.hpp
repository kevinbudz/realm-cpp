#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace realm {
struct DamageCue {
  std::int32_t object_id{}, time_ms{};
  int damage{};
  bool pierced{};
  std::vector<std::uint8_t> effects;
};

// A CharacterStatusText request: floating text above an object.
struct StatusEvent {
  std::int32_t object_id{};
  std::string text;
  std::uint32_t color{};
  int lifetime{}, offset_time{};
};

// Bounded world-space feedback waiting for the rendered status overlay.
class WorldFeedback {
 public:
  static constexpr std::size_t maximum_damage_cues = 256;
  static constexpr std::size_t maximum_status_events = 64;

  void clear();
  void add_status(StatusEvent event);
  [[nodiscard]] std::vector<StatusEvent> take_status_events();
  void report_damage(std::int32_t object_id, int amount, std::int32_t time, std::span<const std::uint8_t> effects,
                     bool piercing, std::uint32_t condition_mask);
  void report_level_progress(std::int32_t object_id, int old_level, int old_exp, int old_fame, int level, int exp,
                             int fame);
  template <class ContainsEntity>
  void expire_damage(std::int32_t time, ContainsEntity&& contains_entity) {
    std::erase_if(damage_cues_, [&](const auto& cue) {
      return static_cast<std::int64_t>(time) - cue.time_ms > 3500 || !contains_entity(cue.object_id);
    });
  }
  [[nodiscard]] const std::deque<DamageCue>& damage_cues() const { return damage_cues_; }

 private:
  void add_status(std::int32_t object_id, std::string text, std::uint32_t color, int lifetime, int offset_time);

  std::deque<DamageCue> damage_cues_;
  std::vector<StatusEvent> status_events_;
  std::size_t status_head_{};
};
}
