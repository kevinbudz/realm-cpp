#pragma once
#include "realm/messaging/Messages.hpp"
#include "realm/map/Map.hpp"
#include <algorithm>
#include <unordered_set>
namespace realm {
// Ids a multi-hit projectile has already struck: a sorted contiguous array (24 bytes empty, no node per hit) instead of
// a node-based hash set that cost 56 bytes per projectile whether or not it ever hit anything.
class HitSet {
 public:
  [[nodiscard]] bool contains(std::int32_t id) const noexcept {
    return std::binary_search(ids_.begin(), ids_.end(), id);
  }
  void insert(std::int32_t id) {
    const auto at = std::lower_bound(ids_.begin(), ids_.end(), id);
    if (at == ids_.end() || *at != id) ids_.insert(at, id);
  }
  [[nodiscard]] auto begin() const noexcept { return ids_.begin(); }
  [[nodiscard]] auto end() const noexcept { return ids_.end(); }
  [[nodiscard]] std::size_t size() const noexcept { return ids_.size(); }

 private:
  std::vector<std::int32_t> ids_;
};
struct FlyingProjectile {
  std::int32_t entity_id{}, bullet_id{}, owner_id{}, start_time{};
  std::uint16_t container_type{};
  int bullet_type{}, damage{};
  std::int32_t last_trail{};
  Vec2 start{};
  double angle{};
  const ProjectileDef* definition{};
  bool damages_players{};
  HitSet hit;
};
// How enemy-targeting projectiles find candidates. `grid` (the default) queries a per-update uniform grid. `scan` is
// the full entity scan, kept for tests as the reference: both must select the identical target.
enum class TargetSearch { grid, scan };
struct TargetStats {
  std::uint64_t queries{}, examined{}, builds{}, indexed{}, empty_cells_skipped{};
};
class Combat {
 public:
  explicit Combat(World& world);
  void reset(std::uint32_t seed);
  [[nodiscard]] std::vector<net::Packet> receive(const net::Event& event, std::int32_t time_ms);
  [[nodiscard]] std::vector<net::Packet> update(std::int32_t time_ms);
  [[nodiscard]] std::vector<net::Packet> shoot(double angle, std::int32_t time_ms, bool ability = false);
  [[nodiscard]] std::vector<net::Packet> use_ability(Vec2 target, std::int32_t time_ms);
  [[nodiscard]] const std::vector<FlyingProjectile>& projectiles() const { return projectiles_; }
  // Test-only: select the reference full scan.
  void set_target_search(TargetSearch search) { search_ = search; }
  [[nodiscard]] TargetSearch target_search() const { return search_; }
  // Cumulative since construction. `examined` counts entities tested per query (grid candidates or scanned entities).
  [[nodiscard]] const TargetStats& target_stats() const { return stats_; }

 private:
  struct Candidate {
    double x{}, y{};
    std::int32_t id{};
  };
  void build_target_index();
  // Enemy candidates whose cell the +/-0.5 tile box around `position` touches; calls `visit(Entity&)` for each.
  template <class Visit>
  void query_target_index(Vec2 position, Visit&& visit);
  void spawn(std::uint16_t container_type, int bullet_type, std::int32_t owner_id, std::int32_t bullet_id, Vec2 start,
             double angle, std::int32_t time, int damage);
  void attack(std::int32_t owner_id, std::uint16_t container_type, double angle, std::int32_t time);
  [[nodiscard]] std::vector<net::Packet> fire(const ObjectDef& weapon, std::int32_t item_data, double angle,
                                              std::int32_t time, bool ability);
  World& world_;
  FlashRandom random_;
  std::vector<FlyingProjectile> projectiles_;
  std::int32_t next_bullet_id_{}, fake_bullet_id_{};
  std::int32_t next_ability_time_{};
  TargetSearch search_{TargetSearch::grid};
  TargetStats stats_;
  // Rebuilt lazily once per update() from the entity list, so movement, add/remove, map changes and
  // client prediction can never leave it stale. Positions are fixed while update() runs.
  bool index_ready_{};
  double origin_x_{}, origin_y_{}, cell_inverse_{1};
  std::size_t columns_{}, rows_{};
  std::vector<Candidate> gathered_, sorted_;
  std::vector<std::uint32_t> cell_of_, cell_start_;
};
// MapUserInput.useSpecial: a key press attempts the ability; a success latches the key until it is released (the key-up
// clears the latch whatever else is open), so a held key does not replay its failure every frame of a cooldown. A
// failure keeps the latch open, so the attempt repeats while the key stays down.
class SpecialKeyLatch {
 public:
  // Whether a held key should attempt the ability now.
  [[nodiscard]] bool attempts() const { return !latched_; }
  // Records the outcome of an attempt made while attempts() was true.
  void record(bool success) { latched_ = success; }
  // The key is up.
  void released() { latched_ = false; }
  [[nodiscard]] bool latched() const { return latched_; }

 private:
  bool latched_{};
};
}
