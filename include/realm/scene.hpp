#pragma once
#include "realm/animation.hpp"
#include "realm/appearance.hpp"
#include "realm/processed.hpp"
#include "realm/terrain.hpp"
#include "realm/renderer.hpp"
#include "realm/world.hpp"
#include "realm/status_effects.hpp"
#include "realm/model.hpp"
#include "realm/residency.hpp"
#include <optional>
#include <unordered_map>
namespace realm {
class Scene {
 public:
  // Actual entity draw decisions, excluding shadows/labels/terrain. Players, enemies, models, projectiles, other.
  [[nodiscard]] const std::array<std::uint64_t, 5>& submitted_counts() const { return submitted_; }
  [[nodiscard]] std::size_t appearance_entries() const { return appearances_.size(); }
  [[nodiscard]] std::size_t appearance_bytes() const { return appearance_bytes_; }
  // Condition icons and names are drawn with their objects; without art none are.
  void set_status_art(StatusArt* art) { status_art_ = art; }
  // Differential oracle: with culling off every entity and particle is built, so a test can prove the camera
  // bounds never drop anything that reaches the viewport. Never disabled in the client.
  void set_culling(bool enabled) { cull_ = enabled; }
  // Differential oracle: culling still on, but every entity is tested (the pre-grid scan) instead of the dynamic
  // entities plus the static-grid cells that reach the camera footprint. Both must yield the same visible set. Never
  // enabled in the client.
  void set_visibility_scan(bool enabled) { scan_visibility_ = enabled; }
  void set_cached_terrain(bool enabled) { cached_terrain_ = enabled; }
  [[nodiscard]] std::uint64_t terrain_rebuilds() const { return terrain_rebuilds_; }
  // World rectangle holding every static entity the camera cull test can accept: the margin rectangle on the ground
  // plane, widened by the catalog's static z range and largest static model reach, inverse-projected (plus 0.01 tile of
  // slack).
  [[nodiscard]] std::pair<Vec2, Vec2> static_footprint(const Catalog& catalog, const Camera& camera, double zoom,
                                                       int width, int height);
  // Status labels and icons: 4096 surfaces / 64 MiB, least recently used evicted; in-flight draws keep their own owner.
  static constexpr std::size_t maximum_status_surfaces = 4096, maximum_status_surface_bytes = 64 * 1024 * 1024;
  // With the dynamic atlas active the SurfaceTexture caches only hold overflow, so each is capped at this instead (see
  // set_overflow_budgets).
  static constexpr std::size_t overflow_surface_bytes = 16 * 1024 * 1024;
  [[nodiscard]] std::size_t status_surfaces() const { return status_surfaces_.size(); }
  [[nodiscard]] std::size_t status_surface_bytes() const { return status_surface_bytes_; }
  // GPU copy of a status label or icon; kept alive by its owner image.
  std::shared_ptr<SurfaceTexture> status_surface(const std::shared_ptr<const Image>& image);
  // Composed appearances, names and condition icons live in the atlas' dynamic pages (one batch with the sprites) when
  // it has any; an image that does not fit, or every slot in use this frame, takes the per-image SurfaceTexture path
  // above. Test-only reference: false is exactly that SurfaceTexture path for everything. Always on in the client.
  void set_dynamic_atlas(bool enabled) { dynamic_ = enabled; }
  // Frames a resident bitmap stays unevictable after its last use (default 2; see TextureResidency).
  // The atlas starts the scene with `initial` dynamic pages and may grow on demand up to `maximum` (0: fixed at
  // whatever it has). It grows instead of evicting when the least recently used bitmap was used within `grow_frames`,
  // and shrinks back to `initial` on a map reset.
  void set_dynamic_atlas_pages(int initial, int maximum, std::uint64_t grow_frames = 120) {
    initial_pages_ = initial;
    max_pages_ = maximum;
    grow_frames_ = grow_frames;
  }
  // Byte budgets of the appearance and status SurfaceTexture caches while the dynamic atlas is active (defaults: 16 MiB
  // each).
  void set_overflow_budgets(std::size_t surface_bytes, std::size_t status_bytes) {
    overflow_surface_bytes_ = surface_bytes;
    overflow_status_bytes_ = status_bytes;
  }
  void set_atlas_protect_frames(std::uint64_t frames) {
    protect_frames_ = frames ? frames : 1;
    residency_.set_protected_frames(protect_frames_);
  }
  // A kind 5 overlay quad of the image in the atlas (floating status text), or nullopt: use status_surface.
  [[nodiscard]] std::optional<Quad> status_quad(const std::shared_ptr<const Image>& image, Atlas& atlas, Vec2 origin,
                                                Vec2 ax, Vec2 ay);
  [[nodiscard]] const TextureResidency& residency() const { return residency_; }
  [[nodiscard]] std::uint64_t atlas_fallbacks() const { return fallbacks_; }
  // Test diagnostic: every live residency entry's atlas region and its source image (null for a label whose image
  // died). Does not touch the LRU order; appearances are re-fetched by key through `appearances`.
  struct ResidentImage {
    SpriteRegion region;
    std::shared_ptr<const Image> image;
  };
  [[nodiscard]] std::vector<ResidentImage> resident_images(const Appearances* appearances, const Atlas& atlas) const;
  void build(const World& world, const Camera& camera, Atlas& atlas, const AnimationLibrary& animations,
             const ProcessedTextures* processed, const Terrain* terrain, const Appearances* appearances,
             Vec2 camera_position, double camera_angle, double zoom, int width, int height, std::int32_t time_ms,
             std::vector<Quad>& output);

 private:
  struct GroundChunk {
    std::uint64_t revision{}, used{};
    bool valid{};
    std::vector<Quad> instances;
    std::vector<Terrain::SlotStamp> slots;
  };
  std::unordered_map<int, GroundChunk> ground_chunks_;
  std::shared_ptr<TerrainBuffer> ground_buffer_;
  std::array<int, 4> ground_bounds_{-1, -1, -1, -1};
  std::uint64_t ground_buffer_revision_{};
  const Terrain* cached_source_{};
  std::uint64_t terrain_frame_{}, terrain_rebuilds_{};
  bool cached_terrain_{};
  void build_terrain(const World& world, const Terrain& terrain, const Camera& camera, Atlas& atlas, Vec2 position,
                     int distance, std::int32_t time, std::vector<Quad>& out);
  std::array<std::uint64_t, 5> submitted_{};
  struct Cached {
    std::weak_ptr<const Image> image;
    std::shared_ptr<SurfaceTexture> surface;
    std::size_t bytes{};
    const Image* key{};
  };
  std::list<Cached> appearances_;
  // O(1) lookup by image identity. An entry whose image died (address possibly reused) is detected by the weak_ptr and
  // dropped.
  std::unordered_map<const Image*, std::list<Cached>::iterator> appearance_index_;
  std::size_t appearance_bytes_{};
  std::uint64_t generation_{};
  const World* appearance_world_{};
  std::shared_ptr<SurfaceTexture> surface(std::shared_ptr<const Image> image);
  StatusArt* status_art_{};
  std::list<Cached> status_list_;
  std::unordered_map<const Image*, std::list<Cached>::iterator> status_surfaces_;
  std::size_t status_surface_bytes_{};
  std::vector<std::uint8_t> icon_frames_;
  // SpiderWeb.wallFound_: webs stay hidden until a Wall touches them, then keep that rotation.
  std::unordered_map<std::int32_t, double> webs_;
  // One catalog lookup and one depth projection per camera-visible entity, reused by every pass below.
  struct Visible {
    const Entity* entity;
    const ObjectDef* def;
    int depth;
    std::int32_t id;
  };
  std::vector<Visible> visible_, visible_scratch_;
  struct DrawItem {
    const Entity* entity{};
    const Particle* particle{};
    int depth{};
    std::uint64_t order{};
    const ObjectDef* def{};
  };
  std::vector<DrawItem> ordered_, particle_items_, merged_;
  std::vector<std::uint32_t> depth_counts_;
  // A model can stand and spread well beyond its ground point (a wide footprint reaches back onto the screen from
  // above, a tall one from below), so offscreen model objects are re-tested against their own extent.
  [[nodiscard]] bool model_reaches(const ObjectDef& def, const Entity& entity, const Camera& camera, int width,
                                   int height);
  std::unordered_map<const ObjectDef*, ModelExtent> model_extents_;
  // Catalog-wide bounds for static (stationary) objects: their z range and the largest model reach in tiles beyond the
  // ground point. They widen the camera footprint so the grid query is a superset of what the cull test accepts.
  struct StaticBounds {
    const Catalog* catalog{};
    double z_low{}, z_high{}, model_reach{};
  };
  StaticBounds static_bounds_;
  [[nodiscard]] const StaticBounds& static_bounds(const Catalog& catalog);
  bool cull_{true}, scan_visibility_{false};
  // Dynamic atlas residency. Appearances are keyed by content (a CPU cache eviction does not force a re-upload), labels
  // and icons by image identity (weak_ptr guards a reused address). Residency ids are never reused; owners maps an
  // evicted id back.
  struct ResidentAppearance {
    std::uint64_t id{};
    int width{}, height{};
    double density{1};
  };
  struct ResidentLabel {
    std::weak_ptr<const Image> image;
    std::uint64_t id{};
    int width{}, height{};
  };
  struct ResidentOwner {
    bool label{};
    Appearances::Key key{};
    const Image* image{};
  };
  struct Resident {
    SpriteRegion region;
    double density{1};
  };
  bool dynamic_{true};
  int initial_pages_{}, max_pages_{};
  std::uint64_t grow_frames_{120};
  std::size_t overflow_surface_bytes_{overflow_surface_bytes}, overflow_status_bytes_{overflow_surface_bytes};
  [[nodiscard]] bool overflow_only() const { return dynamic_ && max_pages_ > 0; }
  std::uint64_t protect_frames_{2};
  TextureResidency residency_;
  const Atlas* residency_atlas_{};
  std::uint32_t residency_texture_{};
  std::uint64_t next_resident_{1}, fallbacks_{};
  std::unordered_map<Appearances::Key, ResidentAppearance, Appearances::KeyHash> resident_appearances_;
  // Last hit of resident_appearances_, cleared by every erase from it.
  Appearances::Key memo_key_{};
  ResidentAppearance memo_entry_{};
  bool memo_valid_{};
  std::unordered_map<const Image*, ResidentLabel> resident_labels_;
  std::unordered_map<std::uint64_t, ResidentOwner> resident_owners_;
  [[nodiscard]] bool dynamic_ready(Atlas& atlas);
  void reset_residency();
  // Reserves a slot (dropping whatever the residency evicted for it), growing the atlas first if the residency asks;
  // nullopt counts as a fallback.
  [[nodiscard]] std::optional<TextureSlot> place(Atlas& atlas, std::uint64_t id, int width, int height);
  [[nodiscard]] std::optional<Resident> resident_appearance(const Appearances::Key& key, const Appearances& appearances,
                                                            Atlas& atlas);
  [[nodiscard]] std::optional<SpriteRegion> resident_label(const std::shared_ptr<const Image>& image, Atlas& atlas);
};
}
