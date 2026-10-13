#pragma once
#include "realm/messaging/Messages.hpp"
#include "realm/map/World.hpp"
#include <array>
#include <unordered_set>
namespace realm {
// Party.as: the nearby players listed by PartyPanel. Membership is rebuilt at
// most every 500 ms; drawing reads the members' current state.
class Party {
 public:
  static constexpr std::size_t members_shown = 6;
  void reset();
  void update(const World& world, std::int32_t time_ms);
  // ACCOUNTLIST id 0 stars accounts and id 1 replaces the ignore list.
  void receive(const net::incoming::AccountList& list);
  // Local lock/unlock/ignore/unignore before the EDITACCOUNTLIST round trip.
  void edit(std::int32_t account_id, std::int32_t object_id, bool ignore, bool add);
  [[nodiscard]] const std::array<std::int32_t, members_shown>& members() const { return members_; }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }
  // Player.starred_: set once a starred account is seen and never cleared
  // by later lists, as Party.update does.
  [[nodiscard]] bool starred(std::int32_t object_id) const { return starred_objects_.contains(object_id); }
  [[nodiscard]] bool ignored(std::int32_t account_id) const { return ignored_.contains(account_id); }

 private:
  std::uint64_t revision_{};
  std::array<std::int32_t, members_shown> members_ = [] {
    std::array<std::int32_t, members_shown> a;
    a.fill(-1);
    return a;
  }();
  std::unordered_set<std::int32_t> starred_, ignored_, starred_objects_;
  std::int64_t last_update_{std::numeric_limits<std::int32_t>::min()};
};
}
