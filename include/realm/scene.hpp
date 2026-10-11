#pragma once
#include "realm/animation.hpp"
#include "realm/appearance.hpp"
#include "realm/processed.hpp"
#include "realm/terrain.hpp"
#include "realm/renderer.hpp"
#include "realm/world.hpp"
#include "realm/status_effects.hpp"
#include <unordered_map>
namespace realm {
class Scene {
public:
    [[nodiscard]] std::size_t appearance_entries() const { return appearances_.size(); }
    [[nodiscard]] std::size_t appearance_bytes() const { return appearance_bytes_; }
    // Condition icons and names are drawn with their objects; without art none are.
    void set_status_art(StatusArt* art) { status_art_=art; }
    [[nodiscard]] std::size_t status_surfaces() const { return status_surfaces_.size(); }
    // GPU copy of a status label or icon; kept alive by its owner image.
    std::shared_ptr<SurfaceTexture> status_surface(const std::shared_ptr<const Image>& image);
    void build(const World& world, const Camera& camera, const Atlas& atlas, const AnimationLibrary& animations, const ProcessedTextures* processed, const Terrain* terrain, const Appearances* appearances,
               Vec2 camera_position, double camera_angle, double zoom, int width, int height, std::int32_t time_ms, std::vector<Quad>& output);
private:
    struct Cached { std::weak_ptr<const Image> image;std::shared_ptr<SurfaceTexture> surface;std::size_t bytes{}; };
    std::list<Cached> appearances_;
    std::size_t appearance_bytes_{};
    std::uint64_t generation_{};
    const World* appearance_world_{};
    std::shared_ptr<SurfaceTexture> surface(std::shared_ptr<const Image> image);
    struct StatusSurface { std::weak_ptr<const Image> image;std::shared_ptr<SurfaceTexture> surface; };
    StatusArt* status_art_{};
    std::unordered_map<const Image*,StatusSurface> status_surfaces_;
    std::vector<std::uint8_t> icon_frames_;
    // SpiderWeb.wallFound_: webs stay hidden until a Wall touches them, then keep that rotation.
    std::unordered_map<std::int32_t,double> webs_;
    std::vector<const Entity*> visible_;
    struct DrawItem { const Entity* entity{};const Particle* particle{};int depth{};std::uint64_t order{}; };
    std::vector<DrawItem> ordered_;
};
}
