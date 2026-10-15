#pragma once
#include <cstdint>
#include <random>

namespace realm {
// Quest.as: the quest object the server names (QUESTOBJID) and when the arrow may show it. A new
// target is available 4 seconds after it arrives and reads as new for 2 seconds more. After "Quest
// Complete!" the next target waits 5 to 15 seconds. The timers start only when a target is first
// set; a target change while one is set keeps them running.
class Quest {
 public:
  explicit Quest(std::uint32_t seed = std::random_device{}()) : random_(seed) {}

  // Map.quest_ is new for every map (MAPINFO).
  void reset();
  // GameServerConnection.onQuestObjId -> Quest.setObject. -1 clears the target.
  void set_object(std::int32_t object_id, std::int32_t time);
  // Quest.completed, from the player's own "Quest Complete!" notification.
  void completed(std::int32_t time);

  // Quest.getObject: the target's object id once it is available at `time`, otherwise -1. The
  // caller resolves the object in World, where a removed object yields no arrow.
  [[nodiscard]] std::int32_t target(std::int32_t time) const {
    return time < available_at_ ? -1 : object_id_;
  }

  // Quest.isNew: the target still reads as new at `time`.
  [[nodiscard]] bool is_new(std::int32_t time) const { return time < old_at_; }

  [[nodiscard]] std::int32_t object_id() const { return object_id_; }

 private:
  std::int32_t object_id_{-1};
  std::int32_t available_at_{};
  std::int32_t old_at_{};
  // Math.random in completed(), seeded so that tests can pin the sequence.
  std::mt19937 random_;
  std::uniform_real_distribution<double> unit_{0.0, 1.0};
};
}
