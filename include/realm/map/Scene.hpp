#pragma once
#include "realm/assets/AnimationLibrary.hpp"
#include "realm/assets/Appearances.hpp"
#include "realm/assets/ProcessedTextures.hpp"
#include "realm/map/Terrain.hpp"
#include "realm/stage3D/Renderer.hpp"
#include "realm/map/World.hpp"
#include "realm/objects/StatusArt.hpp"
#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "realm/stage3D/TextureResidency.hpp"
#include <list>
#include <optional>
#include <unordered_map>

namespace realm {
class Scene {
 public:
  // Actual entity draw decisions, excluding shadows/labels/terrain. Players, enemies, models,
  // projectiles, other.
  [[nodiscard]] const std::array<std::uint64_t, 5>& submitted_counts() const { return submitted_; }

  [[nodiscard]] std::size_t appearance_entries() const { return appearances_.size(); }

  [[nodiscard]] std::size_t appearance_bytes() const { return appearance_bytes_; }

  // Condition icons and names are drawn with their objects; without art none are.
  void set_status_art(StatusArt* art) { status_art_ = art; }

  // Differential oracle: with culling off every entity and particle is built, so a test can prove
  // the camera bounds never drop anything that reaches the viewport. Never disabled in the client.
  void set_culling(bool enabled) { cull_ = enabled; }

  // Differential oracle: culling still on, but every entity is tested (the pre-grid scan) instead
  // of the dynamic entities plus the static-grid cells that reach the camera footprint. Both must
  // yield the same visible set. Never enabled in the client.
  void set_visibility_scan(bool enabled) { scan_visibility_ = enabled; }

  void set_cached_terrain(bool enabled) { cached_terrain_ = enabled; }

  // Differential oracle: draw lists shorter than this are comparison-sorted by (depth, id) or
  // (depth, order) instead of counting-sorted by depth. The order is identical either way. 0
  // counting-sorts every list. Never changed in the client.
  void set_depth_bucket_minimum(std::size_t items) { depth_bucket_minimum_ = items; }

  // Differential oracle: corner capacity of the wall stores this scene creates. A frame that
  // outgrows it draws its remaining walls as software meshes. Never changed in the client.
  void set_wall_store_limit(std::size_t corners) {
    wall_limit_ = corners;
    walls_.reset();
  }

  [[nodiscard]] std::uint64_t terrain_rebuilds() const { return terrain_rebuilds_; }

  // World rectangle holding every static entity the camera cull test can accept: the margin
  // rectangle on the ground plane, widened by the catalog's static z range and largest static model
  // reach, inverse-projected (plus 0.01 tile of slack).
  [[nodiscard]] std::pair<Vec2, Vec2> static_footprint(const Catalog& catalog, const Camera& camera,
                                                       double zoom, int width, int height);
  // Status labels and icons: 4096 surfaces / 64 MiB, least recently used evicted; in-flight draws
  // keep their own owner.
  static constexpr std::size_t maximum_status_surfaces = 4096;
  static constexpr std::size_t maximum_status_surface_bytes = 64 * 1024 * 1024;
  // With the dynamic atlas active the SurfaceTexture caches only hold overflow, so each is capped
  // at this instead (see set_overflow_budgets).
  static constexpr std::size_t overflow_surface_bytes = 16 * 1024 * 1024;

  [[nodiscard]] std::size_t status_surfaces() const { return status_surfaces_.size(); }

  [[nodiscard]] std::size_t status_surface_bytes() const { return status_surface_bytes_; }

  // GPU copy of a status label or icon; kept alive by its owner image.
  std::shared_ptr<SurfaceTexture> status_surface(const std::shared_ptr<const Image>& image);

  // Composed appearances, names and condition icons live in the atlas' dynamic pages (one batch
  // with the sprites) when it has any; an image that does not fit, or every slot in use this frame,
  // takes the per-image SurfaceTexture path above. Test-only reference: false is exactly that
  // SurfaceTexture path for everything. Always on in the client.
  void set_dynamic_atlas(bool enabled) { dynamic_ = enabled; }

  // Frames a resident bitmap stays unevictable after its last use (default 2; see
  // TextureResidency). The atlas starts the scene with `initial` dynamic pages and may grow on
  // demand up to `maximum` (0: fixed at whatever it has). It grows instead of evicting when the
  // least recently used bitmap was used within `grow_frames`, and shrinks back to `initial` on a
  // map reset.
  void set_dynamic_atlas_pages(int initial, int maximum, std::uint64_t grow_frames = 120) {
    initial_pages_ = initial;
    max_pages_ = maximum;
    grow_frames_ = grow_frames;
  }

  // Byte budgets of the appearance and status SurfaceTexture caches while the dynamic atlas is
  // active (defaults: 16 MiB each).
  void set_overflow_budgets(std::size_t surface_bytes, std::size_t status_bytes) {
    overflow_surface_bytes_ = surface_bytes;
    overflow_status_bytes_ = status_bytes;
  }

  void set_atlas_protect_frames(std::uint64_t frames) {
    protect_frames_ = frames ? frames : 1;
    residency_.set_protected_frames(protect_frames_);
  }

  // A kind 5 overlay quad of the image in the atlas (floating status text), or nullopt: use
  // status_surface.
  [[nodiscard]] std::optional<Quad> status_quad(const std::shared_ptr<const Image>& image,
                                                Atlas& atlas, Vec2 origin, Vec2 ax, Vec2 ay);

  [[nodiscard]] const TextureResidency& residency() const { return residency_; }

  [[nodiscard]] std::uint64_t atlas_fallbacks() const { return fallbacks_; }

  // Test diagnostic: every live residency entry's atlas region and its source image (null for a
  // label whose image died). Does not touch the LRU order; appearances are re-fetched by key
  // through `appearances`.
  struct ResidentImage {
    SpriteRegion region;
    std::shared_ptr<const Image> image;
  };

  [[nodiscard]] std::vector<ResidentImage> resident_images(const Appearances* appearances,
                                                           const Atlas& atlas) const;
  void build(const World& world, const Camera& camera, Atlas& atlas,
             const AnimationLibrary& animations, const ProcessedTextures* processed,
             const Terrain* terrain, const Appearances* appearances, Vec2 camera_position,
             double camera_angle, double zoom, int width, int height, std::int32_t time_ms,
             std::vector<Quad>& output);

 private:
  struct GroundChunk {
    std::uint64_t revision{};
    std::uint64_t used{};
    bool valid{};
    std::vector<Quad> instances;
    std::vector<Terrain::SlotStamp> slots;
  };

  std::unordered_map<int, GroundChunk> ground_chunks_;
  std::shared_ptr<TerrainBuffer> ground_buffer_;
  std::array<int, 4> ground_bounds_{-1, -1, -1, -1};
  std::uint64_t ground_buffer_revision_{};
  const Terrain* cached_source_{};
  std::uint64_t terrain_frame_{};
  std::uint64_t terrain_rebuilds_{};
  bool cached_terrain_{};
  void build_terrain(const World& world, const Terrain& terrain, const Camera& camera, Atlas& atlas,
                     Vec2 position, int distance, std::int32_t time, std::vector<Quad>& out);

  std::size_t depth_bucket_minimum_{256};
  std::array<std::uint64_t, 5> submitted_{};

  struct ConnectedGeometryCacheEntry {
    std::uint16_t type{};
    std::uint32_t connect_stat{};
    std::uint32_t seed{};
    int tile_x{};
    int tile_y{};
    ConnectedObjectGeometry geometry;
    std::list<std::int32_t>::iterator lru;
    WallRange wall;  // the CaveWall body in walls_, current while its generation is
    // connected_black_mask of the wall's tile, valid while the terrain revision of its chunk is
    // black_revision (every change to the nine tiles it reads bumps that chunk).
    std::uint64_t black_revision{};
    std::uint32_t black_mask{};
    bool black_known{};
  };

  // Above the wall count of a dense cull rectangle (~1500 tiles), so a full screen of walls never
  // cycles the LRU.
  static constexpr std::size_t maximum_connected_geometries = 4096;
  std::list<std::int32_t> connected_geometry_lru_;
  std::unordered_map<std::int32_t, ConnectedGeometryCacheEntry> connected_geometries_;

  // Stalagmite geometry depends only on its entity id's seed: built once per id, cleared with the
  // cache above. Its wall range bakes in the definition's rotation, so it is appended again when
  // that rotation changes.
  struct StalagmiteEntry {
    std::vector<ObjectFaceGeometry> faces;
    double rotation{};
    WallRange wall;
  };

  std::unordered_map<std::int32_t, StalagmiteEntry> stalagmite_geometries_;
  // Every wall instance (kind 17) draws from this store. Build resets it when nearly full, and
  // every map reset does.
  std::shared_ptr<WallGeometryStore> walls_;
  std::size_t wall_limit_{WallGeometryStore::capacity};
  std::vector<WallGpuVertex> wall_scratch_;
  // ConnectedWall triangles depend only on the definition, shape, rotation and material, so they
  // are shared by key.
  std::unordered_map<std::uint64_t, WallRange> connected_wall_ranges_;
  // Appends the faces' triangles for one material unless `range` is current in the store. False
  // when the store is full.
  [[nodiscard]] bool ensure_wall(WallRange& range, const std::vector<ObjectFaceGeometry>& faces,
                                 std::uint8_t material, double rotation);
  // Draws one object's triangles for one material: a store instance, or the software mesh when the
  // store is full. World null means no boundary tests (Stalagmite).
  void emit_wall(std::vector<Quad>& out, WallRange& range,
                 const std::vector<ObjectFaceGeometry>& faces, std::uint8_t material,
                 SpriteRegion sprite, const Camera& camera, Vec2 position, double rotation,
                 const World* world, int tile_x, int tile_y, std::uint32_t black_mask);

  struct Cached {
    std::weak_ptr<const Image> image;
    std::shared_ptr<SurfaceTexture> surface;
    std::size_t bytes{};
    const Image* key{};
  };

  std::list<Cached> appearances_;
  // O(1) lookup by image identity. An entry whose image died (address possibly reused) is detected
  // by the weak_ptr and dropped.
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

  // One catalog lookup and one depth projection per camera-visible entity, reused by every pass
  // below.
  struct Visible {
    const Entity* entity;
    const ObjectDef* def;
    int depth;
    std::int32_t id;
    // The entity's screen point at its z, and on the ground (z = 0, the depth's source). Projection
    // is a pure function of the camera and position, so every pass reads these instead of
    // projecting again.
    Vec2 at, ground;
    std::uint8_t draw_class;  // DrawClass of def, as TypeEntry::draw_class
    // The base texture's sprite, once the shadow pass has read it (draw_impl reuses it: same call,
    // same result).
    SpriteRegion base_sprite{};
    bool base_sprite_known{};
  };

  // Per object type, filled on first use for one catalog: the definition and its DrawClass, so
  // entities skip the catalog lookup and class-name compares. The catalog is immutable, so an entry
  // holds for the catalog's lifetime.
  struct TypeEntry {
    const ObjectDef* def{};
    std::uint8_t draw_class{};
    // model_index of def->model, when the def has a model.
    std::optional<std::uint32_t> model;
    bool known{};
  };

  std::vector<TypeEntry> types_;
  const Catalog* types_catalog_{};
  [[nodiscard]] const TypeEntry& type_entry(const Catalog& catalog, std::uint16_t type);
  // The first flash of each target, by index into world.particles().flashes(), for this build.
  std::unordered_map<std::int32_t, std::size_t> flash_index_;
  std::vector<Visible> visible_, visible_scratch_;

  struct DrawItem {
    const Entity* entity{};
    const Particle* particle{};
    int depth{};
    std::uint64_t order{};
    const ObjectDef* def{};
    const Visible* visible{};
  };

  std::vector<DrawItem> ordered_, particle_items_, merged_;
  std::vector<std::uint32_t> depth_counts_;
  // A model can stand and spread well beyond its ground point (a wide footprint reaches back onto
  // the screen from above, a tall one from below), so offscreen model objects are re-tested against
  // their own extent.
  [[nodiscard]] bool model_reaches(const ObjectDef& def, const Entity& entity, const Camera& camera,
                                   int width, int height);
  std::unordered_map<const ObjectDef*, ModelExtent> model_extents_;

  // Catalog-wide bounds for static (stationary) objects: their z range and the largest model reach
  // in tiles beyond the ground point. They widen the camera footprint so the grid query is a
  // superset of what the cull test accepts.
  struct StaticBounds {
    const Catalog* catalog{};
    double z_low{};
    double z_high{};
    double model_reach{};
  };

  StaticBounds static_bounds_;
  [[nodiscard]] const StaticBounds& static_bounds(const Catalog& catalog);
  bool cull_{true};
  bool scan_visibility_{false};

  // Dynamic atlas residency. Appearances are keyed by content (a CPU cache eviction does not force
  // a re-upload), labels and icons by image identity (weak_ptr guards a reused address). Residency
  // ids are never reused; owners maps an evicted id back.
  struct ResidentAppearance {
    std::uint64_t id{};
    int width{};
    int height{};
    double density{1};
  };

  struct ResidentLabel {
    std::weak_ptr<const Image> image;
    std::uint64_t id{};
    int width{};
    int height{};
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
  int initial_pages_{};
  int max_pages_{};
  std::uint64_t grow_frames_{120};
  std::size_t overflow_surface_bytes_{overflow_surface_bytes};
  std::size_t overflow_status_bytes_{overflow_surface_bytes};

  [[nodiscard]] bool overflow_only() const { return dynamic_ && max_pages_ > 0; }

  std::uint64_t protect_frames_{2};
  TextureResidency residency_;
  const Atlas* residency_atlas_{};
  std::uint32_t residency_texture_{};
  std::uint64_t next_resident_{1};
  std::uint64_t fallbacks_{};

  // Inline hash and equality over every Appearances::Key field (the map is only looked up, inserted
  // and erased).
  struct ResidentKeyHash {
    [[nodiscard]] std::size_t operator()(const Appearances::Key& key) const noexcept {
      std::uint64_t h = 0x9e3779b97f4a7c15ULL;
      const auto mix = [&h](std::uint64_t v) {
        h = (h ^ v) * 0xff51afd7ed558ccdULL;
        h ^= h >> 29;
      };
      mix(static_cast<std::uint32_t>(key.a) | std::uint64_t{static_cast<std::uint32_t>(key.b)}
                                                  << 32U);
      mix(static_cast<std::uint32_t>(key.c) | std::uint64_t{static_cast<std::uint32_t>(key.d)}
                                                  << 32U);
      mix(static_cast<std::uint32_t>(key.size) | std::uint64_t{key.tex1} << 32U);
      mix(key.tex2 | std::uint64_t{key.kind} << 32U | std::uint64_t{key.pad} << 40U);
      return static_cast<std::size_t>(h);
    }
  };

  struct ResidentKeyEq {
    [[nodiscard]] bool operator()(const Appearances::Key& a,
                                  const Appearances::Key& b) const noexcept {
      return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.size == b.size &&
             a.tex1 == b.tex1 && a.tex2 == b.tex2 && a.kind == b.kind && a.pad == b.pad;
    }
  };

  std::unordered_map<Appearances::Key, ResidentAppearance, ResidentKeyHash, ResidentKeyEq>
      resident_appearances_;
  // Last hit of resident_appearances_, cleared by every erase from it.
  Appearances::Key memo_key_{};
  ResidentAppearance memo_entry_{};
  bool memo_valid_{};
  std::unordered_map<const Image*, ResidentLabel> resident_labels_;
  std::unordered_map<std::uint64_t, ResidentOwner> resident_owners_;
  [[nodiscard]] bool dynamic_ready(Atlas& atlas);
  void reset_residency();
  // Reserves a slot (dropping whatever the residency evicted for it), growing the atlas first if
  // the residency asks; nullopt counts as a fallback.
  [[nodiscard]] std::optional<TextureSlot> place(Atlas& atlas, std::uint64_t id, int width,
                                                 int height);
  [[nodiscard]] std::optional<Resident> resident_appearance(const Appearances::Key& key,
                                                            const Appearances& appearances,
                                                            Atlas& atlas);
  [[nodiscard]] std::optional<SpriteRegion> resident_label(
      const std::shared_ptr<const Image>& image, Atlas& atlas);
};
}
