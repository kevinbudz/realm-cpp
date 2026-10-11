#pragma once
#include "realm/messages.hpp"
#include "realm/world.hpp"
#include <unordered_set>
namespace realm {
struct FlyingProjectile {
    std::int32_t entity_id{}, bullet_id{}, owner_id{}, start_time{};
    std::uint16_t container_type{};
    int bullet_type{}, damage{};
    std::int32_t last_trail{};
    Vec2 start{};
    double angle{};
    const ProjectileDef* definition{};
    bool damages_players{};
    std::unordered_set<std::int32_t> hit;
};
class Combat {
public:
    explicit Combat(World& world) : world_(world), random_(0) {}
    void reset(std::uint32_t seed);
    [[nodiscard]] std::vector<net::Packet> receive(const net::Event& event, std::int32_t time_ms);
    [[nodiscard]] std::vector<net::Packet> update(std::int32_t time_ms);
    [[nodiscard]] std::vector<net::Packet> shoot(double angle, std::int32_t time_ms, bool ability = false);
    [[nodiscard]] std::vector<net::Packet> use_ability(Vec2 target, std::int32_t time_ms);
    [[nodiscard]] const std::vector<FlyingProjectile>& projectiles() const { return projectiles_; }
private:
    void spawn(std::uint16_t container_type, int bullet_type, std::int32_t owner_id, std::int32_t bullet_id, Vec2 start, double angle, std::int32_t time, int damage);
    void attack(std::int32_t owner_id, std::uint16_t container_type, double angle, std::int32_t time);
    [[nodiscard]] std::vector<net::Packet> fire(const ObjectDef& weapon, std::int32_t item_data, double angle, std::int32_t time, bool ability);
    World& world_;
    FlashRandom random_;
    std::vector<FlyingProjectile> projectiles_;
    std::int32_t next_bullet_id_{}, fake_bullet_id_{};
    std::int32_t next_ability_time_{};
};
}
