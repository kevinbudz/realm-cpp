#pragma once
#include "realm/catalog.hpp"
#include "realm/protocol.hpp"
#include <filesystem>
#include <limits>
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
struct Entity {
    std::int32_t id{};
    std::uint16_t type{};
    bool player{}, projectile{};
    Vec2 position{}, target{}, velocity{};
    double z{}, facing{}, attack_angle{};
    std::int32_t attack_start{std::numeric_limits<std::int32_t>::min() / 2};
    double attack_period{500}, move_multiplier{1};
    Vec2 push{};
    int speed{}, attack{}, dexterity{};
    int size{100}, hp{}, max_hp{}, defense{}, sink_level{}, alt_texture{}, texture{};
    std::uint32_t condition{}, tex1{}, tex2{};
    std::string name, guild;
    // Cold stats (inventory, currency, account data) are allocated only when
    // actually received, rather than enlarging every static world object.
    std::unordered_map<std::uint8_t, std::int32_t> stats;
};
struct DamageCue {
    std::int32_t object_id{}, time_ms{};
    int damage{};
    bool pierced{};
    std::vector<std::uint8_t> effects;
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
    void report_damage(std::int32_t id, int damage, std::int32_t time,
                       std::span<const std::uint8_t> effects = {}, bool piercing = false);
    void expire_damage(std::int32_t time);
    [[nodiscard]] const std::vector<DamageCue>& damage_cues() const { return damage_cues_; }
    Entity& add(std::int32_t id, std::uint16_t type, Vec2 position, int size = -1);
    void remove(std::int32_t id);
    [[nodiscard]] Entity* entity(std::int32_t id);
    [[nodiscard]] const Entity* entity(std::int32_t id) const;
    [[nodiscard]] TileState* tile(int x, int y);
    [[nodiscard]] const TileState* tile(int x, int y) const;
    [[nodiscard]] bool walkable(double x, double y) const;
    [[nodiscard]] Vec2 spawn_position() const { return spawn_; }
    [[nodiscard]] std::uint64_t minimap_revision() const { return minimap_revision_; }
    [[nodiscard]] const std::vector<std::uint64_t>& minimap_rows() const { return minimap_rows_; }
    [[nodiscard]] const std::unordered_map<std::uint16_t,std::uint32_t>& minimap_object_ids() const { return minimap_object_ids_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::vector<Entity>& entities() const { return entities_; }
    [[nodiscard]] std::vector<Entity>& entities() { return entities_; }
    [[nodiscard]] const std::vector<std::string>& unresolved_ids() const { return unresolved_; }
    [[nodiscard]] const Catalog& catalog() const { return catalog_; }
    std::int32_t allocate_fake_id();
    void set_player_id(std::int32_t id) { player_id_ = id; }
    [[nodiscard]] std::int32_t player_id() const { return player_id_; }
    // GameSprite.updateNearestInteractive: the closest interactive object
    // strictly within one tile of the player; ties keep the first found.
    [[nodiscard]] const Entity* interactive_target() const;
private:
    void status(Entity& entity, const net::Status& status, bool interpolate, double dt_ms);
    void assign_tile_object(Entity& entity);
    void remember_ground(int x,int y,std::uint16_t type);
    void remember_object(const Entity& entity);
    const Catalog& catalog_;
    int width_{}, height_{};
    std::string name_;
    Vec2 spawn_{};
    std::vector<TileState> tiles_;
    std::vector<Entity> entities_;
    std::unordered_map<std::int32_t, std::size_t> entity_lookup_;
    std::vector<std::string> unresolved_;
    std::vector<DamageCue> damage_cues_;
    std::int32_t player_id_{-1};
    std::uint32_t fake_counter_{};
    std::uint64_t minimap_revision_{};
    std::vector<std::uint64_t> minimap_rows_;
    std::unordered_map<std::uint16_t,std::uint32_t> minimap_object_ids_;
};
}
