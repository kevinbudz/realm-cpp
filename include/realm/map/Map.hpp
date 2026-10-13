#pragma once
#include "realm/objects/ObjectLibrary.hpp"
#include "realm/objects/GameObject.hpp"
#include "realm/map/EntityIndex.hpp"
#include "realm/map/EntitySets.hpp"
#include "realm/messaging/Protocol.hpp"
#include "realm/parameters/Parameters.hpp"
#include "realm/map/WorldFeedback.hpp"
#include "realm/map/WorldParticleEffects.hpp"
#include "realm/map/WorldSoundEvents.hpp"
#include <filesystem>
#include <utility>
#include <unordered_map>

namespace realm {
struct TileState {
  std::uint16_t type{255};
  std::int32_t object_id{-1};
  std::uint8_t elevation{}, terrain{};
  // Persistent fog memory: known bit, static object bit, and source type.
  // Dropping an object does not erase the original Flash minimap pixel.
  std::uint32_t minimap_code{};
};
class World {
 public:
  explicit World(const Catalog& catalog) : catalog_(catalog) {}
  void initialize(int width, int height, std::string name = {});
  void load_jm(const std::filesystem::path& path);
  // A nonzero wrap_border widens the map by twice that many columns and
  // repeats its first columns there, as MapBackground does for scrolling.
  void load_jm_text(std::string_view json, int wrap_border = 0);
  void load_wmap(const std::filesystem::path& path);
  void apply(const net::Update& update);
  void apply(const net::Tick& tick, double interpolation_ms = 127);
  void update(double dt_ms);
  void goto_entity(std::int32_t id, Vec2 position);
  void report_damage(std::int32_t id, int damage, std::int32_t time, std::span<const std::uint8_t> effects = {},
                     bool piercing = false, double hit_angle = 0, double hit_speed = -1);
  void expire_damage(std::int32_t time);
  // MapOverlay.addStatusText: Notification packets, damage, conditions, level/exp/fame.
  void notify(std::int32_t object_id, std::string text, std::uint32_t color, int lifetime, int offset_time = 0);
  // Hands pending floating-text requests to the overlay; bounded when nothing drains them.
  [[nodiscard]] std::vector<StatusEvent> take_status_events();
  // Parameters.data_ flags the game systems consult (hpBars, allyDamage, allyNotifs, ...). Applying
  // them also gates the optional particle effects.
  void set_preferences(const Preferences& preferences) {
    preferences_ = preferences;
    particle_effects_.set_enabled(preferences.particles);
  }
  [[nodiscard]] const Preferences& preferences() const { return preferences_; }
  // GameServerConnection.onNotification: other players' notifications are dropped with Ally Notifications off.
  [[nodiscard]] bool allows_notification(std::int32_t object_id) const;
  [[nodiscard]] Particles& particles() { return particle_effects_.particles(); }
  [[nodiscard]] const Particles& particles() const { return particle_effects_.particles(); }
  [[nodiscard]] const std::deque<DamageCue>& damage_cues() const { return feedback_.damage_cues(); }
  // Sounds requested by world events (hits, deaths, own level-ups, loot, preloads), drained once per rendered frame.
  [[nodiscard]] SoundCues& sounds() { return sound_events_.cues(); }
  // Character.damage plays the hit sound. report_damage does this for each report it keeps; a caller whose report is
  // dropped under Ally Damage still plays it for the player's own projectiles hitting enemies.
  void sound_hit(std::int32_t id);
  // `projectile` marks a projectile visual at insertion (Combat moves those itself, so they never join the static
  // grid).
  Entity& add(std::int32_t id, std::uint16_t type, Vec2 position, int size = -1, bool projectile = false);
  void remove(std::int32_t id);
  [[nodiscard]] Entity* entity(std::int32_t id);
  [[nodiscard]] const Entity* entity(std::int32_t id) const;
  [[nodiscard]] TileState* tile(int x, int y);
  [[nodiscard]] const TileState* tile(int x, int y) const;
  [[nodiscard]] bool walkable(double x, double y) const;
  [[nodiscard]] Vec2 spawn_position() const { return spawn_; }
  [[nodiscard]] std::uint64_t generation() const { return generation_; }
  static constexpr int terrain_chunk_size = 8;
  [[nodiscard]] std::uint64_t terrain_chunk_revision(int x, int y) const;
  [[nodiscard]] std::uint64_t hud_revision() const { return hud_revision_; }
  void invalidate_hud() { ++hud_revision_; }
  [[nodiscard]] std::uint64_t minimap_revision() const { return minimap_revision_; }
  [[nodiscard]] const std::vector<std::uint64_t>& minimap_rows() const { return minimap_rows_; }
  [[nodiscard]] const std::unordered_map<std::uint16_t, std::uint32_t>& minimap_object_ids() const {
    return minimap_object_ids_;
  }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::vector<Entity>& entities() const { return entities_; }
  [[nodiscard]] std::vector<Entity>& entities() { return entities_; }
  [[nodiscard]] const std::vector<std::string>& unresolved_ids() const { return unresolved_; }
  [[nodiscard]] const Catalog& catalog() const { return catalog_; }
  std::int32_t allocate_fake_id();
  void set_player_id(std::int32_t id);
  [[nodiscard]] std::int32_t player_id() const { return player_id_; }
  // GameSprite.updateNearestInteractive: the closest interactive object
  // strictly within one tile of the player; ties keep the first found.
  [[nodiscard]] const Entity* interactive_target() const;
  // Per-frame consumers visit only the entities that can matter to them instead of the whole dense list (a Realm map
  // holds ~246,000 static objects). Each visits its candidates in dense-list order, i.e. the order a scan of entities()
  // meets them, and callers keep their own filters, so results equal the scans they replaced. Membership is fixed at
  // add (type never changes): enemies (Combat target index), minimap marker kinds (players, Character, Portal,
  // GuildHallPortal; projectile and focus filters stay with the caller), players (party, hover, menus) and merchants
  // (world dyes).
  template <class Visit>
  void each_enemy(Visit&& visit) const {
    sets_.each(set_enemy, [&](std::size_t i) { visit(entities_[i]); });
  }
  template <class Visit>
  void each_marker_candidate(Visit&& visit) const {
    sets_.each(set_marker, [&](std::size_t i) { visit(entities_[i]); });
  }
  template <class Visit>
  void each_player(Visit&& visit) const {
    sets_.each(set_player, [&](std::size_t i) { visit(entities_[i]); });
  }
  template <class Visit>
  void each_merchant(Visit&& visit) const {
    sets_.each(set_merchant, [&](std::size_t i) { visit(entities_[i]); });
  }
  // Camera visibility: every entity is either dynamic (visited in dense order) or static, i.e. of a stationary
  // definition and not added as a projectile, and kept in an 8-tile grid. Static entities move only through World
  // (goto_entity, status, update), which re-buckets them; their z stays their definition's. `each_static_in` visits at
  // least every static entity inside the world rectangle, in arbitrary order.
  template <class Visit>
  void each_dynamic(Visit&& visit) const {
    sets_.each(set_dynamic, [&](std::size_t i) { visit(entities_[i]); });
  }
  template <class Visit>
  void each_static_in(Vec2 low, Vec2 high, Visit&& visit) const {
    static_grid_.query(low.x, low.y, high.x, high.y, [&](std::uint32_t i) { visit(entities_[i]); });
  }
  [[nodiscard]] std::size_t static_count() const { return static_grid_.size(); }
  [[nodiscard]] bool is_static(const Entity& e) const {
    const auto i = entity_lookup_.find(e.id);
    return i != EntityIndex::none && static_grid_.contains(i);
  }
  // Entities update() may still move: a superset of those with nonzero velocity (cleared lazily), for diagnostics.
  [[nodiscard]] std::size_t moving_candidates() const { return sets_.count(set_moving); }

 private:
  void terrain_changed(int x, int y);
  std::vector<std::uint64_t> terrain_chunks_;
  std::uint64_t terrain_revision_{}, hud_revision_{};
  void status(Entity& entity, const net::Status& status, bool interpolate, double dt_ms);
  void assign_tile_object(Entity& entity);
  void remember_ground(int x, int y, std::uint16_t type);
  void remember_object(const Entity& entity);
  const Catalog& catalog_;
  int width_{}, height_{};
  std::string name_;
  Vec2 spawn_{};
  std::vector<TileState> tiles_;
  std::vector<Entity> entities_;
  EntityIndex entity_lookup_;
  // Ids of entities whose definition is interactive, maintained by add/remove: the nearby-interaction query visits only
  // these.
  std::vector<std::int32_t> interactive_;
  // Dense-order membership bits (see each_enemy above). The moving set holds every entity whose velocity World made
  // nonzero (status interpolation) plus the previous focus player when the id changes (Player.cpp drives the focus
  // player's velocity directly); update() visits only these and drops the ones that stopped.
  enum : std::size_t { set_moving, set_enemy, set_marker, set_player, set_merchant, set_dynamic, set_count };
  DenseSets<set_count> sets_;
  // Membership mask per object type (bit 7 = computed), so add() does no catalog string compares after a type's first
  // object.
  std::vector<std::uint8_t> type_sets_;
  [[nodiscard]] std::uint32_t type_sets(std::uint16_t type);
  StaticGrid static_grid_;
  std::vector<std::string> unresolved_;
  WorldFeedback feedback_;
  WorldParticleEffects particle_effects_;
  WorldSoundEvents sound_events_;
  std::int32_t player_id_{-1};
  Preferences preferences_;
  std::uint32_t fake_counter_{};
  std::uint64_t minimap_revision_{}, generation_{};
  std::vector<std::uint64_t> minimap_rows_;
  std::unordered_map<std::uint16_t, std::uint32_t> minimap_object_ids_;
};
}
