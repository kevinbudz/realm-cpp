#pragma once
#include "realm/session.hpp"
#include "realm/draw_types.hpp"
#include <filesystem>
#include <map>
namespace realm::quality {
// Offline test driver. Owns only a timeline and fixture inputs: World, Combat,
// interpolation, particles, and wire encoders remain the production systems.
struct StressConfig {
    int version{1}, step_ms{20}, players{}, enemies{}, models{}, projectiles{}, working_set{16};
    std::uint32_t seed{77};
    std::string workload{"POPULATION"}, arrangement{"dense"}, camera{"idle"};
    std::string player, enemy, weapon, multi_weapon;
    std::vector<std::string> model_objects;
    std::vector<int> skins;
    std::vector<std::uint32_t> dyes;
    std::filesystem::path map;
    bool moving{true}, rotate_models{};
    int reset_every{}, replace_every{};
    static StressConfig load(const std::filesystem::path& path);
};
class StressTimeline {
public:
    StressTimeline(World& world, StressConfig config);
    void step();
    [[nodiscard]] std::int32_t time() const { return tick_*config_.step_ms; }
    [[nodiscard]] const StressConfig& config() const { return config_; }
    [[nodiscard]] Vec2 camera_position() const;
    [[nodiscard]] double camera_angle() const;
    [[nodiscard]] double zoom_factor() const;
    [[nodiscard]] std::size_t projectile_count() const { return flights().size(); }
    void sample_scene(const Camera& camera, int width, int height, std::span<const Quad> quads, const std::array<std::uint64_t,5>& submitted);
    [[nodiscard]] const Party* party() const { return replay_?&replay_->party():nullptr; }
    void sample_resources(double rss_mb,std::size_t cpu_entries,std::size_t cpu_bytes,std::size_t gpu_entries,std::size_t gpu_bytes);
    void write_summary(const std::filesystem::path& path) const;
    static void describe(const Catalog& catalog, const std::filesystem::path& path);
private:
    void populate();
    void feed(net::Packet packet);
    void apply(const net::Update& update);
    void apply(const net::Tick& tick);
    void receive(const net::Event& event,std::int32_t now);
    [[nodiscard]] const std::vector<FlyingProjectile>& flights() const { return replay_?replay_->projectiles():combat_.projectiles(); }
    [[nodiscard]] const TargetStats& target_stats() const { return replay_?replay_->target_stats():combat_.target_stats(); }
    void packets(std::vector<net::Packet> packets);
    Vec2 placement(int index,int kind) const;
    net::Object object(int index,int kind) const;
    World& world_;
    StressConfig config_;
    Combat combat_;
    std::unique_ptr<Session> replay_;
    net::Framer framer_;
    std::uint64_t replayed_packets_{};
    int tick_{},next_bullet_{},resets_{},replacements_{};
    std::uint64_t packet_hash_{1469598103934665603ULL},hits_{},spawned_{},removed_{},simulated_{};
    std::map<int,std::uint64_t> packet_counts_;
    std::uint64_t frames_{},quad_count_{},triangle_count_{};
    double coverage_sum_{};
    std::uint64_t quad_hash_{1469598103934665603ULL};
    std::array<std::uint64_t,5> visible_{},submitted_{},simulated_by_kind_{};
    std::uint64_t particle_steps_{};
    double rss_first_{},rss_peak_{},rss_last_{};
    std::array<std::size_t,4> cache_peak_{},cache_last_{};
};
}
