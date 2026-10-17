#include "realm/util/Telemetry.hpp"
#include "realm/map/Scene.hpp"
#include "realm/engine3d/ConnectedObjects.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "realm/objects/Movement.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace realm {
namespace {
std::array<float, 2> floats(Vec2 v) {
  return {static_cast<float>(v.x), static_cast<float>(v.y)};
}
}

namespace {
AnimationFrame animation_frame(const Character& character, const Entity* e, double angle,
                               std::int32_t time) {
  Action action = Action::stand;
  double phase{};
  double facing = e ? e->motion.facing : 0;
  const double attack_period = e && e->player ? e->combat.attack_period : 500;
  if (e && time < e->combat.attack_start + attack_period) {
    action = Action::attack;
    phase = std::fmod(time - e->combat.attack_start, attack_period) / attack_period;
    facing = e->combat.attack_angle;
  } else if (e && (e->motion.velocity.x || e->motion.velocity.y)) {
    action = Action::walk;
    facing = std::atan2(e->motion.velocity.y, e->motion.velocity.x);
    int period{};
    if (e->player) {
      period = static_cast<int>(3.5 / move_speed(*e));
    } else {
      period = static_cast<int>(.5 / (std::hypot(e->motion.velocity.x, e->motion.velocity.y) * 4));
      period += 400 - period % 400;
    }
    period = std::max(1, period);
    phase = static_cast<double>(time % period) / period;
  }
  return character.frame(facing, angle, action, phase);
}

SpriteRegion texture(const TextureDef& definition, std::uint32_t id, const Entity* e, double angle,
                     std::int32_t time, const Atlas& atlas, const AnimationLibrary& animations) {
  const auto& selected = definition.select(id);
  if (selected.animated >= 0) {
    if (const auto* character = animations.character(selected.animated, selected.index)) {
      return atlas.additional(animation_frame(*character, e, angle, time).image);
    }
  }
  if (selected.sheet >= 0) {
    return atlas.sprite(static_cast<std::size_t>(selected.sheet), selected.index);
  }
  return {0, Atlas::page_size - 1, Atlas::page_size - 1, 1, 1};
}

// Stable counting sort on the integer `depth` member, O(n + depth span). Screen-space depths of
// what is on screen span about the viewport height, so this replaces comparison sorts of thousands
// of items. False (items untouched) when the span is too wide to bucket cheaply, or fewer than
// `minimum` items (clearing the counts costs more than sorting); the caller then sorts with the
// same order.
template <class T>
bool bucket_by_depth(std::vector<T>& items, std::vector<T>& scratch,
                     std::vector<std::uint32_t>& counts, std::size_t minimum) {
  if (items.size() < 2) {
    return true;
  }
  if (items.size() < minimum) {
    return false;
  }
  const auto [low, high] = std::minmax_element(
      items.begin(), items.end(), [](const T& a, const T& b) { return a.depth < b.depth; });
  const auto span =
      static_cast<std::size_t>(static_cast<std::int64_t>(high->depth) - low->depth) + 1;
  if (span > 4 * items.size() + 4096) {
    return false;
  }
  const int base = low->depth;
  counts.assign(span + 1, 0);
  for (const auto& item : items) {
    ++counts[static_cast<std::size_t>(item.depth - base) + 1];
  }
  for (std::size_t i = 1; i < counts.size(); ++i) {
    counts[i] += counts[i - 1];
  }
  scratch.resize(items.size());
  for (const auto& item : items) {
    scratch[counts[static_cast<std::size_t>(item.depth - base)]++] = item;
  }
  items.swap(scratch);
  return true;
}

Quad ground_quad(SpriteRegion sprite, Vec2 position, const Camera& camera, double z = 0) {
  const auto p = camera.world_to_screen(position, z);
  const auto px = camera.world_to_screen({position.x + 1, position.y}, z);
  const auto py = camera.world_to_screen({position.x, position.y + 1}, z);
  return sprite_quad(sprite, p, {px.x - p.x, px.y - p.y}, {py.x - p.x, py.y - p.y});
}

// A standalone surface shows nothing where a quad's uv leaves [0,1], but inside the atlas the
// neighbouring slot would show, so the quad is cut to the part inside its region. The local-to-uv
// map is axis aligned, so each axis clips on its own. False: nothing left.
bool clip_to_region(Quad& q, const SpriteRegion& r) {
  constexpr double inv = 1.0 / Atlas::page_size;
  constexpr double snap = 1e-5;
  double t0[2];
  double t1[2];
  for (int k = 0; k < 2; ++k) {
    const double lo = (k ? r.y : r.x) * inv;
    const double hi = lo + (k ? r.height : r.width) * inv;
    const double u = q.uv[static_cast<std::size_t>(k)];
    const double d = q.uv[static_cast<std::size_t>(k + 2)];
    t0[k] = std::max(0.0, (lo - u) / d);
    t1[k] = std::min(1.0, (hi - u) / d);
    if (t0[k] < snap) {
      t0[k] = 0;
    }
    if (t1[k] > 1 - snap) {
      t1[k] = 1;  // rounding noise at an exactly fitting edge must not move the quad
    }
    if (!(t1[k] > t0[k])) {
      return false;
    }
  }
  if (t0[0] == 0 && t0[1] == 0 && t1[0] == 1 && t1[1] == 1) {
    return true;
  }
  const auto ax = q.axis_x;
  const auto ay = q.axis_y;
  for (std::size_t channel = 0; channel < 2; ++channel) {
    q.origin[channel] =
        static_cast<float>(q.origin[channel] + t0[0] * ax[channel] + t0[1] * ay[channel]);
    q.axis_x[channel] = static_cast<float>(ax[channel] * (t1[0] - t0[0]));
    q.axis_y[channel] = static_cast<float>(ay[channel] * (t1[1] - t0[1]));
  }
  for (std::size_t k = 0; k < 2; ++k) {
    const double u = q.uv[k];
    const double d = q.uv[k + 2];
    q.uv[k] = static_cast<float>(u + t0[k] * d);
    q.uv[k + 2] = static_cast<float>(d * (t1[k] - t0[k]));
  }
  return true;
}
// The ObjectDef classes that Scene::build branches on, resolved from ObjectDef::class_name once per
// entity.
enum class DrawClass : std::uint8_t {
  other,
  merchant,
  sign,
  connected_wall,
  cave_wall,
  stalagmite,
  wall,
  spider_web
};

DrawClass draw_class(std::string_view name) {
  if (name == "Merchant") {
    return DrawClass::merchant;
  }
  if (name == "Sign") {
    return DrawClass::sign;
  }
  if (name == "ConnectedWall") {
    return DrawClass::connected_wall;
  }
  if (name == "CaveWall") {
    return DrawClass::cave_wall;
  }
  if (name == "Stalagmite") {
    return DrawClass::stalagmite;
  }
  if (name == "Wall") {
    return DrawClass::wall;
  }
  if (name == "SpiderWeb") {
    return DrawClass::spider_web;
  }
  return DrawClass::other;
}
}

std::shared_ptr<SurfaceTexture> Scene::surface(std::shared_ptr<const Image> image) {
  REALM_ZONE("appearance.gpu_cache");
  if (const auto found = appearance_index_.find(image.get()); found != appearance_index_.end()) {
    if (found->second->image.lock() == image) {
      telemetry::add(telemetry::Counter::gpu_appearance_hits);
      const auto result = found->second->surface;
      appearances_.splice(appearances_.begin(), appearances_, found->second);
      return result;
    }
    appearance_bytes_ -= found->second->bytes;
    appearances_.erase(found->second);
    appearance_index_.erase(found);  // address reused by a different image
  }
  telemetry::add(telemetry::Counter::gpu_appearance_misses);
  while (!appearances_.empty() &&
         (appearances_.size() >= Appearances::maximum_entries ||
          appearance_bytes_ + image->pixels.size() >
              (overflow_only() ? overflow_surface_bytes_ : Appearances::maximum_bytes))) {
    telemetry::add(telemetry::Counter::gpu_appearance_evictions);
    appearance_bytes_ -= appearances_.back().bytes;
    appearance_index_.erase(appearances_.back().key);
    appearances_.pop_back();
  }
  auto result = std::make_shared<SurfaceTexture>();
  result->update(*image);
  appearance_bytes_ += image->pixels.size();
  appearances_.push_front({image, result, image->pixels.size(), image.get()});
  appearance_index_[image.get()] = appearances_.begin();
  return result;
}

std::shared_ptr<SurfaceTexture> Scene::status_surface(const std::shared_ptr<const Image>& image) {
  if (const auto found = status_surfaces_.find(image.get()); found != status_surfaces_.end()) {
    if (found->second->image.lock() == image) {
      const auto result = found->second->surface;
      status_list_.splice(status_list_.begin(), status_list_, found->second);
      return result;
    }
    status_surface_bytes_ -= found->second->bytes;
    status_list_.erase(found->second);
    status_surfaces_.erase(found);  // address reused by a different image
  }
  const auto bytes = image->pixels.size();
  while (!status_list_.empty() &&
         (status_list_.size() >= maximum_status_surfaces ||
          status_surface_bytes_ + bytes >
              (overflow_only() ? overflow_status_bytes_ : maximum_status_surface_bytes))) {
    status_surface_bytes_ -= status_list_.back().bytes;
    status_surfaces_.erase(status_list_.back().key);
    status_list_.pop_back();
  }
  auto result = std::make_shared<SurfaceTexture>();
  result->update(*image);
  status_surface_bytes_ += bytes;
  status_list_.push_front({image, result, bytes, image.get()});
  status_surfaces_[image.get()] = status_list_.begin();
  return result;
}

bool Scene::dynamic_ready(Atlas& atlas) {
  if (!dynamic_ || !atlas.dynamic_pages()) {
    return false;
  }
  if (residency_atlas_ != &atlas || residency_texture_ != atlas.texture() ||
      residency_.pages() != atlas.dynamic_pages()) {
    reset_residency();
    residency_ = TextureResidency(atlas.dynamic_pages(), Atlas::page_size);
    residency_.set_protected_frames(protect_frames_);
    residency_.set_growth(max_pages_, grow_frames_);
    residency_atlas_ = &atlas;
    residency_texture_ = atlas.texture();
  }
  return true;
}

void Scene::reset_residency() {
  memo_valid_ = false;
  residency_.clear();
  resident_appearances_.clear();
  resident_labels_.clear();
  resident_owners_.clear();
}

std::optional<TextureSlot> Scene::place(Atlas& atlas, std::uint64_t id, int width, int height) {
  auto slot = residency_.insert(id, width, height);
  if (!slot && residency_.growth_requested() > residency_.pages()) {
    // Layer indices and the page size do not change, so quads already built this frame keep
    // sampling the same texels of the new texture.
    const int pages = residency_.growth_requested();
    atlas.resize_dynamic(pages);
    residency_.add_pages(pages - residency_.pages());
    residency_texture_ = atlas.texture();
    slot = residency_.insert(id, width, height);
  }
  for (const auto evicted : residency_.evicted()) {
    telemetry::add(telemetry::Counter::atlas_evictions);
    const auto owner = resident_owners_.find(evicted);
    if (owner == resident_owners_.end()) {
      continue;
    }
    if (owner->second.label) {
      resident_labels_.erase(owner->second.image);
    } else {
      resident_appearances_.erase(owner->second.key);
      memo_valid_ = false;
    }
    resident_owners_.erase(owner);
  }
  if (!slot) {
    ++fallbacks_;
    telemetry::add(telemetry::Counter::atlas_fallbacks);
  }
  return slot;
}

std::optional<Scene::Resident> Scene::resident_appearance(const Appearances::Key& key,
                                                          const Appearances& appearances,
                                                          Atlas& atlas) {
  if (!dynamic_ready(atlas)) {
    return std::nullopt;
  }
  const auto make = [&](const ResidentAppearance& r, const TextureSlot& s) {
    return Resident{{atlas.dynamic_first() + s.page, s.x, s.y, r.width, r.height}, r.density};
  };
  // Runs of identical projectiles ask for the same key back to back; the memo holds the entry the
  // map would return.
  if (memo_valid_ && ResidentKeyEq{}(key, memo_key_)) {
    if (const auto* slot = residency_.find(memo_entry_.id)) {
      return make(memo_entry_, *slot);
    }
  }
  if (const auto found = resident_appearances_.find(key); found != resident_appearances_.end()) {
    if (const auto* slot = residency_.find(found->second.id)) {
      memo_key_ = key;
      memo_entry_ = found->second;
      memo_valid_ = true;
      return make(found->second, *slot);
    }
    memo_valid_ = false;
    resident_owners_.erase(found->second.id);
    resident_appearances_.erase(found);  // not expected: eviction drops the entry
  }
  const auto image = appearances.image(key);
  const std::uint64_t id = next_resident_++;
  const auto slot = place(atlas, id, image->width, image->height);
  if (!slot) {
    return std::nullopt;
  }
  atlas.upload_dynamic(slot->page, slot->x, slot->y, *image);
  const ResidentAppearance entry{id, image->width, image->height, image->pixel_scale};
  resident_appearances_[key] = entry;
  resident_owners_[id] = {false, key, nullptr};
  return make(entry, *slot);
}

std::optional<SpriteRegion> Scene::resident_label(const std::shared_ptr<const Image>& image,
                                                  Atlas& atlas) {
  if (!dynamic_ready(atlas)) {
    return std::nullopt;
  }
  const auto make = [&](const TextureSlot& s) {
    return SpriteRegion{atlas.dynamic_first() + s.page, s.x, s.y, s.width, s.height};
  };
  if (const auto found = resident_labels_.find(image.get()); found != resident_labels_.end()) {
    if (found->second.image.lock() == image) {
      if (const auto* slot = residency_.find(found->second.id)) {
        return make(*slot);
      }
    }
    residency_.erase(found->second.id);
    resident_owners_.erase(found->second.id);
    resident_labels_.erase(found);  // address reused by a different image
  }
  const std::uint64_t id = next_resident_++;
  const auto slot = place(atlas, id, image->width, image->height);
  if (!slot) {
    return std::nullopt;
  }
  atlas.upload_dynamic(slot->page, slot->x, slot->y, *image);
  resident_labels_[image.get()] = {image, id, image->width, image->height};
  resident_owners_[id] = {true, {}, image.get()};
  return make(*slot);
}

std::vector<Scene::ResidentImage> Scene::resident_images(const Appearances* appearances,
                                                         const Atlas& atlas) const {
  std::vector<ResidentImage> result;
  for (const auto& [id, slot] : residency_.slots()) {
    const auto owner = resident_owners_.find(id);
    if (owner == resident_owners_.end()) {
      continue;
    }
    const SpriteRegion region{atlas.dynamic_first() + slot.page, slot.x, slot.y, slot.width,
                              slot.height};
    std::shared_ptr<const Image> image;
    if (owner->second.label) {
      const auto label = resident_labels_.find(owner->second.image);
      if (label != resident_labels_.end()) {
        image = label->second.image.lock();
      }
    } else if (appearances) {
      image = appearances->image(owner->second.key);
    }
    result.push_back({region, std::move(image)});
  }
  return result;
}

std::optional<Quad> Scene::status_quad(const std::shared_ptr<const Image>& image, Atlas& atlas,
                                       Vec2 origin, Vec2 ax, Vec2 ay) {
  const auto region = resident_label(image, atlas);
  if (!region) {
    return std::nullopt;
  }
  auto q = sprite_quad(*region, origin, ax, ay);
  q.kind = 5;
  return q;
}

bool Scene::model_reaches(const ObjectDef& def, const Entity& entity, const Camera& camera,
                          int width, int height) {
  auto found = model_extents_.find(&def);
  if (found == model_extents_.end()) {
    found = model_extents_.emplace(&def, model_extent(def.model)).first;
  }
  const auto& extent = found->second;
  const double unit = camera.pixels_per_unit();
  const double side = extent.radius * unit;
  const double up = side + std::max(0.0, extent.top) * unit;
  const double down = side + std::max(0.0, -extent.bottom) * unit;
  const auto p = camera.world_to_screen(entity.motion.position);
  return p.x + side > 0 && p.x - side < width && p.y + down > 0 && p.y - up < height;
}

const Scene::StaticBounds& Scene::static_bounds(const Catalog& catalog) {
  if (static_bounds_.catalog == &catalog) {
    return static_bounds_;
  }
  StaticBounds bounds;
  bounds.catalog = &catalog;
  for (const auto& def : catalog.objects()) {
    if (!def.stationary) {
      continue;
    }
    bounds.z_low = std::min(bounds.z_low, def.z);
    bounds.z_high = std::max(bounds.z_high, def.z);
    if (def.model.empty()) {
      continue;
    }
    auto found = model_extents_.find(&def);
    if (found == model_extents_.end()) {
      found = model_extents_.emplace(&def, model_extent(def.model)).first;
    }
    const auto& e = found->second;
    bounds.model_reach = std::max(bounds.model_reach, e.radius + std::max({0.0, e.top, -e.bottom}));
  }
  static_bounds_ = bounds;
  return static_bounds_;
}

std::pair<Vec2, Vec2> Scene::static_footprint(const Catalog& catalog, const Camera& camera,
                                              double zoom, int width, int height) {
  // The cull test accepts p = world_to_screen(position, z) inside (-400, w + 400) x (-100, h + 400)
  // * zoom, or a model whose extent around the z = 0 ground point reaches the screen. On the z = 0
  // plane z lifts the tested point by 50 * z * scale.
  const auto& bounds = static_bounds(catalog);
  const double lift = 50 * camera.scale();
  const double side = bounds.model_reach * camera.pixels_per_unit();
  const double x0 = std::min(-400 * zoom, -side);
  const double x1 = std::max(width + 400 * zoom, width + side);
  const double y0 = std::min(-100 * zoom + lift * bounds.z_low, -side);
  const double y1 = std::max(height + 400 * zoom + lift * bounds.z_high, height + side);
  constexpr double slack = .01;
  Vec2 low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
  Vec2 high{-low.x, -low.y};
  for (const Vec2 corner : {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x0, y1}, Vec2{x1, y1}}) {
    const auto w = camera.screen_to_world(corner);
    low = {std::min(low.x, w.x - slack), std::min(low.y, w.y - slack)};
    high = {std::max(high.x, w.x + slack), std::max(high.y, w.y + slack)};
  }
  return {low, high};
}

const Scene::TypeEntry& Scene::type_entry(const Catalog& catalog, std::uint16_t type) {
  if (types_catalog_ != &catalog) {
    types_.clear();
    types_catalog_ = &catalog;
  }
  if (type >= types_.size()) {
    types_.resize(static_cast<std::size_t>(type) + 1);
  }
  auto& entry = types_[type];
  if (!entry.known) {
    entry.def = catalog.object(type);
    entry.draw_class =
        static_cast<std::uint8_t>(entry.def ? draw_class(entry.def->class_name) : DrawClass::other);
    if (entry.def && !entry.def->model.empty()) {
      entry.model = model_index(entry.def->model);
    }
    entry.known = true;
  }
  return entry;
}

void Scene::build_terrain(const World& world, const Terrain& terrain, const Camera& camera,
                          Atlas& atlas, Vec2 position, int distance, std::int32_t time,
                          std::vector<Quad>& out) {
  // Connecting sessions have no map until MAPINFO arrives. Dividing the
  // empty map's -1 upper bound by eight would otherwise select chunk zero.
  if (world.width() <= 0 || world.height() <= 0) {
    return;
  }
  REALM_ZONE("scene.terrain");
  ++terrain_frame_;
  const int cx = static_cast<int>(std::floor(position.x));
  const int cy = static_cast<int>(std::floor(position.y));
  const int x0 = std::max(0, cx - distance) / 8;
  const int y0 = std::max(0, cy - distance) / 8;
  const int x1 = std::min(world.width() - 1, cx + distance) / 8;
  const int y1 = std::min(world.height() - 1, cy + distance) / 8;
  const int columns = (world.width() + 7) / 8;
  // Pin every valid visible chunk before composing any new one: composition may evict old atlas
  // slots.
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const auto found = ground_chunks_.find(x + y * columns);
      if (found == ground_chunks_.end()) {
        continue;
      }
      auto& chunk = found->second;
      if (chunk.revision != world.terrain_chunk_revision(x * 8, y * 8) ||
          !terrain.retain(chunk.slots)) {
        chunk.valid = false;
      } else {
        chunk.used = terrain_frame_;
      }
    }
  }
  const auto zero = camera.world_to_screen({0, 0});
  const auto px = camera.world_to_screen({1, 0});
  const auto py = camera.world_to_screen({0, 1});
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      auto& chunk = ground_chunks_[x + y * columns];
      if (!chunk.valid) {
        auto& instances = chunk.instances;
        instances.clear();
        instances.reserve(128);
        chunk.slots.clear();
        for (int ty = y * 8; ty < std::min(world.height(), y * 8 + 8); ++ty) {
          for (int tx = x * 8; tx < std::min(world.width(), x * 8 + 8); ++tx) {
            const auto* ground = world.catalog().ground(world.tile(tx, ty)->type);
            if (!ground || world.tile(tx, ty)->type == 255) {
              continue;
            }
            const auto visual = terrain.tile(tx, ty);
            auto q = sprite_quad(atlas.additional(visual.image),
                                 {static_cast<double>(tx - x * 8), static_cast<double>(ty - y * 8)},
                                 {1, 0}, {0, 1});
            chunk.slots.push_back(terrain.stamp(visual.image));
            if (ground->animate) {
              q.kind = ground->animate == 1 ? 12 : 11;
              q.uv_offset = {static_cast<float>(ground->animate_x),
                             static_cast<float>(ground->animate_y)};
              q.offset[3] = static_cast<float>(ground->offset_x);
            } else if (visual.offset.x || visual.offset.y) {
              q.kind = 3;
              q.uv_offset = floats(visual.offset);
            }
            instances.push_back(q);
            if (visual.overlay >= 0) {
              instances.push_back(
                  sprite_quad(atlas.additional(static_cast<std::size_t>(visual.overlay)),
                              {static_cast<double>(tx - x * 8), static_cast<double>(ty - y * 8)},
                              {1, 0}, {0, 1}));
              chunk.slots.push_back(terrain.stamp(static_cast<std::size_t>(visual.overlay)));
            }
          }
        }
        chunk.valid = true;
        chunk.revision = world.terrain_chunk_revision(x * 8, y * 8);
        ++terrain_rebuilds_;
      }
      chunk.used = terrain_frame_;
    }
  }
  const std::array<int, 4> bounds{x0, y0, x1, y1};
  // One retained draw for the visible patch: per-chunk draws would replace saved CPU work with
  // driver overhead.
  if (!ground_buffer_ || ground_bounds_ != bounds || ground_buffer_revision_ != terrain_rebuilds_) {
    std::vector<Quad> instances;
    std::size_t total = 0;
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        total += ground_chunks_.at(x + y * columns).instances.size();
      }
    }
    instances.reserve(total);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        for (const auto& q : ground_chunks_.at(x + y * columns).instances) {
          auto placed = q;
          placed.origin[0] += static_cast<float>((x - x0) * 8);
          placed.origin[1] += static_cast<float>((y - y0) * 8);
          instances.push_back(std::move(placed));
        }
      }
    }
    ground_buffer_ = std::make_shared<TerrainBuffer>(instances);
    ground_bounds_ = bounds;
    ground_buffer_revision_ = terrain_rebuilds_;
  }
  if (ground_buffer_->count()) {
    Quad command;
    command.kind = 10;
    command.owner = ground_buffer_;
    command.origin =
        floats(camera.world_to_screen({static_cast<double>(x0 * 8), static_cast<double>(y0 * 8)}));
    command.axis_x = floats({px.x - zero.x, px.y - zero.y});
    command.axis_y = floats({py.x - zero.x, py.y - zero.y});
    command.uv_offset[0] = static_cast<float>(time / 1000.0);
    out.push_back(std::move(command));
  }
  // Bound geometry memory independently of the size of the explored map. Current commands own their
  // buffers.
  if (ground_chunks_.size() > 1024) {
    for (auto it = ground_chunks_.begin(); it != ground_chunks_.end();) {
      if (it->second.used != terrain_frame_) {
        it = ground_chunks_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

bool Scene::ensure_wall(WallRange& range, const std::vector<ObjectFaceGeometry>& faces,
                        std::uint8_t material, double rotation) {
  if (range.generation == walls_->generation()) {
    return true;
  }
  wall_scratch_.clear();
  build_wall_gpu_vertices(faces, material, rotation, wall_scratch_);
  const auto appended = walls_->append(wall_scratch_);
  if (!appended) {
    return false;
  }
  range = *appended;
  return true;
}

void Scene::emit_wall(std::vector<Quad>& out, WallRange& range,
                      const std::vector<ObjectFaceGeometry>& faces, std::uint8_t material,
                      SpriteRegion sprite, const Camera& camera, Vec2 position, double rotation,
                      const World* world, int tile_x, int tile_y, std::uint32_t black_mask) {
  if (ensure_wall(range, faces, material, rotation)) {
    if (range.count) {
      out.push_back(wall_quad(walls_, range, camera, position, sprite, black_mask));
    }
    return;
  }
  // The store is full for this frame: the same triangles as a software mesh.
  if (const auto mesh = project_object_faces(faces, material, sprite, camera, position, 0, rotation,
                                             world, tile_x, tile_y, world != nullptr)) {
    out.push_back(mesh_quad(mesh));
  }
}

void Scene::build(const World& world, const Camera& camera, Atlas& atlas,
                  const AnimationLibrary& animations, const ProcessedTextures* processed,
                  const Terrain* terrain, const Appearances* appearances, Vec2 position,
                  double angle, double zoom, int width, int height, std::int32_t time,
                  std::vector<Quad>& out) {
  REALM_ZONE("scene.build");
  submitted_.fill(0);
  // A nearly full store is replaced rather than cleared: quads of an earlier build that still
  // reference it keep their contents.
  if (!walls_ || walls_->vertices().size() > walls_->limit() / 4 * 3) {
    walls_ = std::make_shared<WallGeometryStore>(wall_limit_);
  }
  if (dynamic_ready(atlas)) {
    residency_.begin_frame();
  }
  if (appearance_world_ != &world || generation_ != world.generation()) {
    ground_chunks_.clear();
    ground_buffer_.reset();
    cached_source_ = terrain;
    reset_residency();
    // A map reset drops the working set, so the pages it needed go back too (the residency is
    // rebuilt at the initial size).
    if (overflow_only() && initial_pages_ > 0 && atlas.dynamic_pages() > initial_pages_) {
      atlas.resize_dynamic(initial_pages_);
      if (dynamic_ready(atlas)) {
        residency_.begin_frame();
      }
    }
    appearances_.clear();
    appearance_index_.clear();
    webs_.clear();
    connected_geometries_.clear();
    connected_geometry_lru_.clear();
    stalagmite_geometries_.clear();
    connected_wall_ranges_.clear();
    walls_ = std::make_shared<WallGeometryStore>(wall_limit_);
    status_surfaces_.clear();
    status_list_.clear();
    status_surface_bytes_ = 0;
    appearance_bytes_ = 0;
    // The shared compositions are keyed by source art, size and dyes only, so they stay valid on
    // another map; their budget bounds them.
    generation_ = world.generation();
    appearance_world_ = &world;
  }
  const int distance = static_cast<int>(std::ceil(camera.max_distance())) + 3;
  const int cx = static_cast<int>(std::floor(position.x));
  const int cy = static_cast<int>(std::floor(position.y));
  const double extent = camera.pixels_per_unit() * 1.5;
  if (cached_source_ != terrain) {
    ground_chunks_.clear();
    ground_buffer_.reset();
    cached_source_ = terrain;
  }
  if (cached_terrain_ && terrain) {
    build_terrain(world, *terrain, camera, atlas, position, distance, time, out);
  } else {
    REALM_ZONE("scene.terrain");
    for (int y = std::max(0, cy - distance); y < std::min(world.height(), cy + distance + 1); ++y) {
      for (int x = std::max(0, cx - distance); x < std::min(world.width(), cx + distance + 1);
           ++x) {
        const auto* tile = world.tile(x, y);
        if (!tile) {
          continue;
        }
        const auto* ground = world.catalog().ground(tile->type);
        if (!ground || tile->type == 255) {
          continue;
        }
        const Vec2 world_pos{static_cast<double>(x), static_cast<double>(y)};
        const auto p = camera.world_to_screen(world_pos);
        if (p.x < -extent || p.x > width + extent || p.y < -extent || p.y > height + extent) {
          continue;
        }
        const auto visual = terrain ? terrain->tile(x, y) : TileVisual{};
        const auto sprite = terrain ? atlas.additional(visual.image)
                                    : texture(ground->texture, tile_hash(x, y), nullptr, angle,
                                              time, atlas, animations);
        auto q = ground_quad(sprite, world_pos, camera);
        auto offset = terrain ? visual.offset : Vec2{ground->offset_x, ground->offset_y};
        if (ground->animate) {
          const double t = time / 1000.0;
          offset.x = ground->offset_x + (ground->animate == 1 ? std::sin(ground->animate_x * t)
                                                              : ground->animate_x * t);
          offset.y = ground->offset_x + (ground->animate == 1 ? std::sin(ground->animate_y * t)
                                                              : ground->animate_y * t);
        }
        if (ground->animate || offset.x || offset.y) {
          q.kind = 3;
          q.uv_offset = {static_cast<float>(offset.x), static_cast<float>(offset.y)};
        }
        out.push_back(q);
        if (terrain && visual.overlay >= 0) {
          out.push_back(ground_quad(atlas.additional(static_cast<std::size_t>(visual.overlay)),
                                    world_pos, camera));
        }
      }
    }
  }
  bool negative_ids = false;
  {
    REALM_ZONE("scene.visibility");
    visible_.clear();
    // The cull rectangle in screen pixels, as Scene::build always had it (each bound is the same
    // double expression).
    const double cull_left = -400 * zoom;
    const double cull_right = width + 400 * zoom;
    const double cull_top = -100 * zoom;
    const double cull_bottom = height + 400 * zoom;
    const auto consider = [&](const Entity& e) {
      const auto& type = type_entry(world.catalog(), e.type);
      const auto* def = type.def;
      const auto at = camera.world_to_screen(e.motion.position, e.motion.z);
      if (cull_ &&
          !(at.x > cull_left && at.x < cull_right && at.y > cull_top && at.y < cull_bottom) &&
          !(def && !def->model.empty() && model_reaches(*def, e, camera, width, height))) {
        return;
      }
      // Player Opacity 0%: a hidden player is not built at all, shadow included.
      if (world.player_opacity(e) <= 0) {
        return;
      }
      negative_ids |= e.id < 0;
      const auto ground = camera.world_to_screen(e.motion.position);
      visible_.push_back({&e, def, static_cast<int>(ground.y / zoom + camera.clip_rect().y), e.id,
                          at, ground, type.draw_class});
    };
    if (!cull_ || scan_visibility_) {
      for (const auto& e : world.entities()) {
        consider(e);
      }
    } else {
      // Dynamic entities (actors, projectiles, loot) are all tested; static ones only from the grid
      // cells overlapping static_footprint(). Ids are unique, so the (depth, id) sort below fixes
      // the order whatever the gather order.
      world.each_dynamic(consider);
      const auto [low, high] = static_footprint(world.catalog(), camera, zoom, width, height);
      world.each_static_in(low, high, consider);
    }
    telemetry::set(telemetry::Counter::visible_entities, visible_.size());
  }
  {
    REALM_ZONE("scene.sort_entities");
    // Bucket by depth, then order each (short) equal-depth run by id: the same (depth, id) order as
    // one comparison sort.
    if (bucket_by_depth(visible_, visible_scratch_, depth_counts_, depth_bucket_minimum_)) {
      for (auto run = visible_.begin(); run != visible_.end();) {
        const auto end = std::find_if(run + 1, visible_.end(), [&](const Visible& visible) {
          return visible.depth != run->depth;
        });
        if (end - run > 1) {
          std::sort(run, end, [](const Visible& a, const Visible& b) { return a.id < b.id; });
        }
        run = end;
      }
    } else {
      std::sort(visible_.begin(), visible_.end(), [](const Visible& a, const Visible& b) {
        return a.depth == b.depth ? a.id < b.id : a.depth < b.depth;
      });
    }
  }
  // Each target answers with its first flash in list order, so the index keeps the first entry.
  flash_index_.clear();
  const auto flashes = world.particles().flashes();
  for (std::size_t i = 0; i < flashes.size(); ++i) {
    flash_index_.emplace(flashes[i].target, i);
  }
  const auto flash_tint = [&](Quad& q, const Entity& e) {
    const auto found = flash_index_.find(e.id);
    if (found == flash_index_.end()) {
      return;
    }
    const auto& flash = flashes[found->second];
    const auto age = static_cast<std::int64_t>(time) - flash.born;
    if (age < 0 || age > static_cast<std::int64_t>(flash.period) * flash.repeats) {
      return;
    }
    const float intensity = static_cast<float>(
        std::sin(static_cast<double>(age % flash.period) / flash.period * std::numbers::pi) * .5);
    for (int channel = 0; channel < 3; ++channel) {
      q.multiply[static_cast<std::size_t>(channel)] *= 1 - intensity;
      q.offset[static_cast<std::size_t>(channel)] =
          intensity * static_cast<float>((flash.color >> (16 - 8 * channel)) & 255U) / 255;
    }
  };
  // Sprite-level condition cues: Stasis greys the texture (PAUSED_FILTER, uv_offset.x flags the
  // shader), Invisible players draw at 40% alpha, and players under 20% health pulse toward red.
  const auto condition_tint = [&](Quad& q, const Entity& e) {
    if (e.combat.condition & condition::bit(condition::stasis)) {
      q.uv_offset[0] = 1;
    } else if (e.player && (e.combat.condition & condition::bit(condition::invisible))) {
      for (auto& channel : q.multiply) {
        channel *= .4F;
      }
    }
    if (e.player && e.combat.hp < e.combat.max_hp * .2) {
      const auto shift = static_cast<float>(low_health_pulse(time) * 128 / 255);
      q.offset[0] += shift;
      q.offset[1] -= shift;
      q.offset[2] -= shift;
    }
  };
  const auto* focus = world.entity(world.player_id());
  const bool hallucinating =
      focus && (focus->combat.condition & condition::bit(condition::hallucinating));
  // The lofiChar8x8 sheet is fixed by the Flash source, so its index is looked up once per process.
  static const int hallucination_sheet = [] {
    int found = -1;
    for (std::size_t i = 0; i < flash::sheets.size(); ++i) {
      if (std::string_view(flash::sheets[i].name) == "lofiChar8x8") {
        found = static_cast<int>(i);
      }
    }
    return found;
  }();
  TextureDef hallucinated;
  auto draw_impl = [&](const Entity& e, const ObjectDef& d, const Visible* visible) {
    const auto kind =
        visible ? static_cast<DrawClass>(visible->draw_class) : draw_class(d.class_name);
    // world_dyes returns the entity's own dyes unless it is the focus player.
    auto dyes = e.player ? world_dyes(world, e)
                         : std::array<std::uint32_t, 2>{e.appearance.tex1, e.appearance.tex2};
    const auto screen =
        visible ? visible->at : camera.world_to_screen(e.motion.position, e.motion.z);
    const TextureDef* const base_texture =
        e.player ? &world.catalog().player_texture(e.type, e.appearance.texture) : &d.texture;
    const TextureDef* tex = base_texture;
    if (kind == DrawClass::merchant) {
      if (const auto it =
              e.profile.stats.find(static_cast<std::uint8_t>(flash::StatId::MERCHANDISE_TYPE_STAT));
          it != e.profile.stats.end()) {
        if (const auto* item = world.catalog().object(static_cast<std::uint16_t>(it->second))) {
          tex = &item->texture;
          dyes = {static_cast<std::uint32_t>(item->xml.number("Tex1")),
                  static_cast<std::uint32_t>(item->xml.number("Tex2"))};
        }
      }
    }
    if (e.appearance.alt_texture) {
      if (const auto it = tex->alternate.find(e.appearance.alt_texture);
          it != tex->alternate.end()) {
        tex = &it->second;
      }
    }
    // Player.getTexture: a Hexed player takes the look of a random Hexable object (stable per
    // player), keeping its dyes.
    if (e.player && (e.combat.condition & condition::bit(condition::hexed)) &&
        !world.catalog().hex_transforms().empty()) {
      const auto& hexes = world.catalog().hex_transforms();
      if (const auto* hex = world.catalog().object(
              hexes[static_cast<std::uint32_t>(e.id) * 2654435761U % hexes.size()])) {
        tex = &hex->texture;
      }
    }
    // The shadow pass reads this same sprite for this entity: reuse it while no substitution
    // changed the texture.
    auto sprite =
        tex == base_texture && visible && visible->base_sprite_known
            ? visible->base_sprite
            : texture(*tex, static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
    int shown_size = e.appearance.size;
    // GameObject/Player.getTexture: while the focus player hallucinates, every ordinary object
    // draws a random lofiChar8x8 tile (stable per object); non-players are scaled by min(1.5,
    // original width / 8).
    if (hallucinating && !e.projectile && !d.draw_on_ground && d.model.empty() &&
        kind != DrawClass::wall && kind != DrawClass::connected_wall &&
        kind != DrawClass::cave_wall && kind != DrawClass::sign && kind != DrawClass::stalagmite &&
        hallucination_sheet >= 0) {
      hallucinated = TextureDef{};
      hallucinated.sheet = hallucination_sheet;
      hallucinated.index = static_cast<int>(static_cast<std::uint32_t>(e.id) * 2654435761U % 239U);
      if (!e.player) {
        shown_size = static_cast<int>(e.appearance.size * std::min(1.5, sprite.width / 8.0));
      }
      tex = &hallucinated;
      dyes = {};
      sprite = texture(*tex, static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
    }
    if (d.draw_on_ground) {
      out.push_back(ground_quad(sprite,
                                {std::floor(e.motion.position.x), std::floor(e.motion.position.y)},
                                camera, e.motion.z));
      return;
    }
    if (kind == DrawClass::sign) {
      if (!status_art_ || e.profile.name.empty()) {
        return;
      }
      const auto label = status_art_->sign_text(e.profile.name, e.appearance.size);
      if (!label.image) {
        return;
      }
      const double scale = 5 * e.appearance.size / 100.0 * zoom;
      const double w = label.width * scale;
      const double h = label.height * scale;
      const auto at = camera.world_to_screen(e.motion.position);
      const Vec2 origin{at.x - w / 2 + label.offset.x * scale,
                        at.y - h / 2 + label.offset.y * scale};
      if (const auto region = resident_label(label.image, atlas)) {
        auto q = sprite_quad(*region, origin, {w, 0}, {0, h});
        q.kind = 6;
        flash_tint(q, e);
        condition_tint(q, e);
        out.push_back(std::move(q));
      } else {
        const auto surface = status_surface(label.image);
        auto q =
            surface->quad(origin, {w, 0}, {0, h}, 0, 0, label.image->width, label.image->height);
        q.kind = 6;
        q.owner = surface;
        flash_tint(q, e);
        condition_tint(q, e);
        out.push_back(std::move(q));
      }
      return;
    }
    if (kind == DrawClass::connected_wall || kind == DrawClass::cave_wall) {
      const auto found =
          e.profile.stats.find(static_cast<std::uint8_t>(flash::StatId::CONNECT_STAT));
      const int tile_x = static_cast<int>(std::floor(e.motion.position.x));
      const int tile_y = static_cast<int>(std::floor(e.motion.position.y));
      const auto connect_stat = found != e.profile.stats.end()
                                    ? static_cast<std::uint32_t>(found->second)
                                    : connected_object_connections(world, e.type, tile_x, tile_y);
      const auto seed = static_cast<std::uint32_t>(e.id) * 2654435761U ^
                        static_cast<std::uint32_t>(tile_x) * 1259U ^
                        static_cast<std::uint32_t>(tile_y) * 2957U;
      auto cached = connected_geometries_.find(e.id);
      if (cached == connected_geometries_.end() || cached->second.type != e.type ||
          cached->second.connect_stat != connect_stat || cached->second.tile_x != tile_x ||
          cached->second.tile_y != tile_y || cached->second.seed != seed) {
        if (cached != connected_geometries_.end()) {
          connected_geometry_lru_.erase(cached->second.lru);
          connected_geometries_.erase(cached);
        }
        if (connected_geometries_.size() >= maximum_connected_geometries) {
          const auto victim = connected_geometry_lru_.back();
          connected_geometry_lru_.pop_back();
          connected_geometries_.erase(victim);
        }
        connected_geometry_lru_.push_front(e.id);
        auto geometry = connected_object_geometry(d, connect_stat, tile_x, tile_y, seed);
        cached = connected_geometries_
                     .emplace(e.id, ConnectedGeometryCacheEntry{e.type, connect_stat, seed, tile_x,
                                                                tile_y, std::move(geometry),
                                                                connected_geometry_lru_.begin(),
                                                                WallRange{}})
                     .first;
      } else {
        connected_geometry_lru_.splice(connected_geometry_lru_.begin(), connected_geometry_lru_,
                                       cached->second.lru);
        cached->second.lru = connected_geometry_lru_.begin();
      }
      const auto& geometry = cached->second.geometry;
      if (geometry.shape == ConnectedShape::none) {
        return;
      }
      const Vec2 object_position = e.motion.position;
      const double rotation = geometry.rotation * 90.0;
      // Off the map there is no chunk revision to key on, so the mask is recomputed.
      auto& entry = cached->second;
      const bool on_map = world.tile(tile_x, tile_y) != nullptr;
      const auto revision = on_map ? world.terrain_chunk_revision(tile_x, tile_y) : 0;
      if (!on_map || !entry.black_known || entry.black_revision != revision) {
        entry.black_mask = connected_black_mask(world, tile_x, tile_y);
        entry.black_revision = revision;
        entry.black_known = on_map;
      }
      const auto mask = entry.black_mask;
      if (kind == DrawClass::connected_wall) {
        // The body and cap ranges depend only on the definition, shape, rotation and material.
        const auto key = [&](std::uint64_t material) {
          return (static_cast<std::uint64_t>(e.type) << 16) |
                 (static_cast<std::uint64_t>(geometry.shape) << 8) |
                 (static_cast<std::uint64_t>(geometry.rotation) << 2) | material;
        };
        emit_wall(out, connected_wall_ranges_[key(0)], geometry.faces, 0, sprite, camera,
                  object_position, rotation, &world, tile_x, tile_y, mask);
        const auto index = static_cast<std::size_t>(geometry.shape) - 1;
        const auto& cap = d.connected_textures[index];
        const bool has_cap = cap.sheet >= 0 || cap.animated >= 0 || !cap.random.empty();
        const auto cap_sprite = has_cap ? texture(cap, static_cast<std::uint32_t>(e.id), &e, angle,
                                                  time, atlas, animations)
                                        : sprite;
        emit_wall(out, connected_wall_ranges_[key(1)], geometry.faces, 1, cap_sprite, camera,
                  object_position, rotation, &world, tile_x, tile_y, mask);
      } else {
        emit_wall(out, cached->second.wall, geometry.faces, 0, sprite, camera, object_position,
                  rotation, &world, tile_x, tile_y, mask);
      }
      return;
    }
    if (kind == DrawClass::stalagmite) {
      auto cached = stalagmite_geometries_.find(e.id);
      if (cached == stalagmite_geometries_.end()) {
        if (stalagmite_geometries_.size() >= maximum_connected_geometries) {
          stalagmite_geometries_.clear();
        }
        const auto seed = static_cast<std::uint32_t>(e.id) * 2654435761U ^ 0x7f4a7c15U;
        cached = stalagmite_geometries_
                     .emplace(e.id, StalagmiteEntry{stalagmite_geometry(seed), d.rotation, {}})
                     .first;
      } else if (cached->second.rotation != d.rotation) {
        cached->second.rotation = d.rotation;
        cached->second.wall = {};
      }
      emit_wall(out, cached->second.wall, cached->second.faces, 0, sprite, camera,
                e.motion.position, d.rotation, nullptr, 0, 0, 0U);
      return;
    }
    if (kind == DrawClass::wall) {
      const double x = std::floor(e.motion.position.x);
      const double y = std::floor(e.motion.position.y);
      constexpr int nx[4] = {0, 1, 0, -1};
      constexpr int ny[4] = {-1, 0, 1, 0};
      const Vec2 corners[4] = {{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}};
      for (int side = 0; side < 4; ++side) {
        const auto* neighbor =
            world.tile(static_cast<int>(x) + nx[side], static_cast<int>(y) + ny[side]);
        const auto* other = neighbor ? world.entity(neighbor->object_id) : nullptr;
        if (other && type_entry(world.catalog(), other->type).draw_class ==
                         static_cast<std::uint8_t>(DrawClass::wall)) {
          continue;
        }
        const auto a = camera.world_to_screen(corners[side], 1);
        const auto b = camera.world_to_screen(corners[(side + 1) % 4], 1);
        const auto bottom = camera.world_to_screen(corners[side], 0);
        const Vec2 ax{b.x - a.x, b.y - a.y};
        const Vec2 ay{bottom.x - a.x, bottom.y - a.y};
        if (ax.x * ay.y - ax.y * ay.x > 0) {
          continue;
        }
        auto q = sprite_quad(sprite, a, ax, ay);
        const double light =
            .75 + .25 * std::max(0.0, (nx[side] + 3.0 * ny[side]) / std::sqrt(14.0));
        q.multiply[0] = q.multiply[1] = q.multiply[2] = static_cast<float>(light);
        if (!neighbor || neighbor->type == 255) {
          q.kind = 2;
          q.multiply = {0, 0, 0, 1};
        }
        out.push_back(q);
      }
      const auto top =
          texture(d.top, static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
      auto q = ground_quad(top, {x, y}, camera, 1);
      q.multiply[0] = q.multiply[1] = q.multiply[2] =
          static_cast<float>(.75 + .5 / std::sqrt(14.0));
      out.push_back(q);
      return;
    }
    std::optional<std::uint32_t> model;
    if (!d.model.empty()) {
      const auto& type = type_entry(world.catalog(), e.type);
      model = type.def == &d ? type.model : model_index(d.model);
    }
    if (model) {
      // GameObject.draw3d: a registered Model replaces the 2D sprite. The sprite is only
      // its texture; Solid* groups and sprite-less objects use the XML Color.
      const auto& chosen = tex->select(static_cast<std::uint32_t>(e.id));
      std::optional<SpriteRegion> model_sprite;
      if (chosen.sheet >= 0 || chosen.animated >= 0) {
        model_sprite = sprite;
      }
      double rotation =
          std::isfinite(e.appearance.model_rotation) ? e.appearance.model_rotation : d.rotation;
      if (kind == DrawClass::spider_web) {
        // SpiderWeb.findWall: west, north, east, then south neighbor; the web turns away from its
        // wall.
        auto found = webs_.find(e.id);
        if (found == webs_.end()) {
          constexpr int dx[4] = {-1, 0, 1, 0};
          constexpr int dy[4] = {0, -1, 0, 1};
          for (int side = 0; side < 4; ++side) {
            const auto* tile = world.tile(static_cast<int>(e.motion.position.x + dx[side]),
                                          static_cast<int>(e.motion.position.y + dy[side]));
            const auto* object = tile ? world.entity(tile->object_id) : nullptr;
            if (object && type_entry(world.catalog(), object->type).draw_class ==
                              static_cast<std::uint8_t>(DrawClass::wall)) {
              found = webs_.emplace(e.id, side ? 90.0 * side : d.rotation).first;
              break;
            }
          }
        }
        if (found == webs_.end()) {
          return;
        }
        rotation = found->second;
      }
      out.push_back(model_quad(*model, camera, e.motion.position, rotation, d.color, model_sprite));
      return;
    }
    // Projectile.draw: with Projectile Outline off the plain texture fills the projectile's square.
    bool baked = (processed || appearances) && d.model.empty() &&
                 !(e.projectile && !world.preferences().proj_outline);
    std::shared_ptr<SurfaceTexture> lazy;
    bool resident = false;
    double density = 1;
    if (baked) {
      const auto& chosen = tex->select(static_cast<std::uint32_t>(e.id));
      const auto* character = animations.character(chosen.animated, chosen.index);
      const auto frame =
          character ? animation_frame(*character, &e, angle, time) : AnimationFrame{};
      // The processed-texture key is a string: build it only when a processed set exists and the
      // object is undyed.
      const auto processed_key =
          processed && !(dyes[0] || dyes[1])
              ? (character
                     ? animated_texture_key(frame.image, shown_size, e.projectile)
                     : static_texture_key(chosen.sheet, chosen.index, shown_size, e.projectile))
              : std::string{};
      const bool use_processed = !processed_key.empty() && processed->contains(processed_key);
      if (use_processed) {
        sprite = atlas.additional(processed->find(processed_key));
      } else if (appearances) {
        const auto key =
            character ? appearances->animated_key(frame, shown_size, e.projectile, dyes[0], dyes[1])
                      : appearances->texture_key(chosen, static_cast<std::uint32_t>(e.id),
                                                 shown_size, e.projectile, dyes[0], dyes[1]);
        if (const auto found = resident_appearance(key, *appearances, atlas)) {
          sprite = found->region;
          density = found->density;
          resident = true;
        } else {
          auto image = appearances->image(key);
          density = image->pixel_scale;
          sprite = {0, 0, 0, image->width, image->height};
          lazy = surface(std::move(image));
        }
      } else {
        baked = false;
      }
    }
    if (e.projectile) {
      const double rotation = d.rotation == 0 ? 0 : time / d.rotation;
      // Projectile.draw: Projectile Direction turns the sprite along its travel, not its launch.
      const double heading =
          world.preferences().smart_projectiles ? e.motion.travel : e.motion.facing;
      auto q = projectile_quad(sprite, screen, heading - angle + d.angle_correction + rotation,
                               e.appearance.size, zoom, baked);
      if (lazy) {
        const auto base = lazy->quad({}, {}, {});
        q.texture = base.texture;
        q.owner = lazy;
        q.kind = 6;
        q.uv[0] *= Atlas::page_size / static_cast<float>(sprite.width);
        q.uv[1] *= Atlas::page_size / static_cast<float>(sprite.height);
        q.uv[2] *= Atlas::page_size / static_cast<float>(sprite.width);
        q.uv[3] *= Atlas::page_size / static_cast<float>(sprite.height);
      } else if (resident) {
        if (!clip_to_region(q, sprite)) {
          return;
        }
        q.kind = 6;
      }
      flash_tint(q, e);
      out.push_back(std::move(q));
      return;
    }
    const double scale = baked ? zoom / density : 5 * shown_size / 100.0 * zoom;
    const double w = sprite.width * scale;
    const double h = sprite.height * scale;
    const auto p = screen;
    int sink = e.motion.sink_level;
    const int tile_x = static_cast<int>(std::floor(e.motion.position.x));
    const int tile_y = static_cast<int>(std::floor(e.motion.position.y));
    const auto* square = world.tile(tile_x, tile_y);
    if (terrain && square) {
      sink += terrain->tile(tile_x, tile_y).sink;
    }
    // Flying, or standing on a protected object, cancels the sink; a zero sink has nothing to
    // cancel.
    if (d.flying) {
      sink = 0;
    } else if (sink != 0 && square) {
      const auto* protector = world.entity(square->object_id);
      const auto* protector_def = protector ? world.catalog().object(protector->type) : nullptr;
      if (protector_def && protector_def->protect_sink) {
        sink = 0;
      }
    }
    const double clipped = std::clamp(sink * zoom, 0.0, h);
    if (baked) {
      sprite.height -= std::min(sink, sprite.height);
    }
    if (sprite.height > 0) {
      auto q =
          lazy ? lazy->quad({p.x - w / 2, p.y - h + clipped}, {w, 0}, {0, h - clipped}, 0, 0,
                            sprite.width, sprite.height)
               : sprite_quad(sprite, {p.x - w / 2, p.y - h + clipped}, {w, 0}, {0, h - clipped});
      if (lazy || resident) {
        q.kind = 6;
      }
      q.owner = lazy;
      flash_tint(q, e);
      condition_tint(q, e);
      out.push_back(std::move(q));
    }
    if (status_art_ && e.combat.condition) {
      // GameObject.drawConditionIcons: 16 px icons centered over the sprite's top edge.
      condition_icon_frames(e.combat.condition, time, icon_frames_);
      const double top = p.y - h + clipped;
      const double left = p.x - 8.0 * static_cast<double>(icon_frames_.size()) * zoom;
      for (std::size_t i = 0; i < icon_frames_.size(); ++i) {
        const auto image = status_art_->icon(icon_frames_[i]);
        if (!image) {
          continue;
        }
        const Vec2 at{left + 16.0 * static_cast<double>(i) * zoom, top - 8 * zoom};
        if (const auto region = resident_label(image, atlas)) {
          auto q = sprite_quad(*region, at, {16 * zoom, 0}, {0, 16 * zoom});
          q.kind = 6;
          out.push_back(std::move(q));
          continue;
        }
        const auto surface = status_surface(image);
        auto q = surface->quad(at, {16 * zoom, 0}, {0, 16 * zoom});
        q.kind = 6;
        q.owner = surface;
        out.push_back(std::move(q));
      }
    }
    if (status_art_ && !e.profile.name.empty() &&
        (d.show_name || d.nexus_portal || (d.player && e.id != world.player_id()))) {
      // GameObject.drawName: the name bitmap is centered on the object, top at its ground position.
      const auto label = status_art_->name(e.profile.name);
      if (label.image) {
        const auto ground = camera.world_to_screen(e.motion.position);
        const double image_w = label.image->width / label.image->pixel_scale,
                     image_h = label.image->height / label.image->pixel_scale;
        const double field_w = std::floor(label.width / 2 + 1) * 2;
        const Vec2 at{ground.x - field_w / 2 * zoom + label.offset.x * zoom,
                      ground.y + label.offset.y * zoom};
        if (const auto region = resident_label(label.image, atlas)) {
          auto q = sprite_quad(*region, at, {image_w * zoom, 0}, {0, image_h * zoom});
          q.kind = 6;
          out.push_back(std::move(q));
        } else {
          const auto surface = status_surface(label.image);
          auto q = surface->quad(at, {image_w * zoom, 0}, {0, image_h * zoom});
          q.kind = 6;
          q.owner = surface;
          out.push_back(std::move(q));
        }
      }
    }
    if (world.preferences().hp_bars && (d.player || d.enemy) && !d.no_minimap &&
        !(e.combat.condition & (1U << 20U)) && (d.player || !(e.combat.condition & (1U << 21U)))) {
      const auto ground = camera.world_to_screen(e.motion.position);
      const double y = ground.y + (5 + (d.player && e.id != world.player_id() ? 16 : 0)) * zoom;
      auto solid = [&](Vec2 origin, Vec2 size, std::uint32_t color) {
        Quad q;
        q.kind = 2;
        q.origin = {static_cast<float>(origin.x), static_cast<float>(origin.y)};
        q.axis_x = {static_cast<float>(size.x), 0};
        q.axis_y = {0, static_cast<float>(size.y)};
        q.multiply = {static_cast<float>((color >> 16U) & 255U) / 255,
                      static_cast<float>((color >> 8U) & 255U) / 255,
                      static_cast<float>(color & 255U) / 255, 1};
        out.push_back(q);
      };
      // Device pixels: the 40x5 bar origin and size round once, and the black frame is the same
      // border on all four sides, so its edges are hard at fractional UI scales.
      const double bar_x = std::round(ground.x - 20 * zoom);
      const double bar_y = std::round(y);
      const double bar_w = std::round(40 * zoom);
      const double bar_h = std::round(5 * zoom);
      const double border = std::max(1.0, std::round(1.2 * zoom));
      solid({bar_x - border, bar_y - border}, {bar_w + 2 * border, bar_h + 2 * border}, 0x111111);
      const double hp_fraction =
          static_cast<double>(e.combat.hp) /
          std::max(1, d.player ? e.combat.max_hp : std::max(e.combat.hp, e.combat.max_hp));
      if (e.combat.hp > 0) {
        solid({bar_x, bar_y}, {std::round(bar_w * hp_fraction), bar_h},
              hp_fraction < .2   ? 14684176U
              : hp_fraction < .5 ? 16744464U
                                 : 0x10ff00U);
      }
    }
    if (e.player && e.id == world.player_id()) {
      // Player.drawBreathBar: 40x4 under the health bar position, only while the server sends a
      // breath stat.
      if (const int breath = player_breath(e); breath >= 0) {
        const auto ground = camera.world_to_screen(e.motion.position);
        const double y = ground.y + 9 * zoom;
        auto solid = [&](Vec2 origin, Vec2 size, std::uint32_t color) {
          Quad q;
          q.kind = 2;
          q.origin = {static_cast<float>(origin.x), static_cast<float>(origin.y)};
          q.axis_x = {static_cast<float>(size.x), 0};
          q.axis_y = {0, static_cast<float>(size.y)};
          q.multiply = {static_cast<float>((color >> 16U) & 255U) / 255,
                        static_cast<float>((color >> 8U) & 255U) / 255,
                        static_cast<float>(color & 255U) / 255, 1};
          out.push_back(q);
        };
        // Whole device pixels, as the health bar.
        const double bar_x = std::round(ground.x - 20 * zoom);
        const double bar_y = std::round(y);
        const double bar_w = std::round(40 * zoom);
        const double bar_h = std::round(4 * zoom);
        solid({bar_x, bar_y}, {bar_w, bar_h}, breath_bar_back_color(breath, time));
        if (breath > 0) {
          solid({bar_x, bar_y}, {std::round(bar_w * std::min(breath, 100) / 100.0), bar_h},
                0x26caffU);
        }
      }
    }
  };
  // The sprite height of the entity's base texture, read once per visible entity (the draw pass
  // reuses the sprite).
  const auto base_sprite_height = [&](Visible& visible, const ObjectDef& d) {
    if (!visible.base_sprite_known) {
      const auto& e = *visible.entity;
      visible.base_sprite = texture(
          e.player ? world.catalog().player_texture(e.type, e.appearance.texture) : d.texture,
          static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
      visible.base_sprite_known = true;
    }
    return visible.base_sprite.height;
  };
  const auto draw = [&](const Entity& e, const ObjectDef& d, const Visible* visible) {
    const auto before = out.size();
    draw_impl(e, d, visible);
    // Player Opacity: the sprite, condition icons, name, and bars that draw_impl just emitted fade
    // together.
    if (const float opacity = world.player_opacity(e); opacity < 1) {
      for (auto i = before; i < out.size(); ++i) {
        fade_quad(out[i], opacity);
      }
    }
    if (out.size() > before) {
      ++submitted_[static_cast<std::size_t>(e.projectile       ? 3
                                            : e.player         ? 0
                                            : !d.model.empty() ? 2
                                            : d.enemy          ? 1
                                                               : 4)];
    }
  };
  for (const auto& visible : visible_) {
    if (visible.def && visible.def->draw_under) {
      draw(*visible.entity, *visible.def, &visible);
    }
  }
  {
    REALM_ZONE("scene.shadows");
    if (world.preferences().draw_shadows) {
      for (auto& visible : visible_) {
        const auto* e = visible.entity;
        const auto* d = visible.def;
        // Deliberate deviation from Flash: 3D objects (walls, caves, stalagmites and OBJ models)
        // never cast the radial shadow.
        if (!d || d->draw_under || !d->model.empty() || !d->shadow_size) {
          continue;
        }
        const auto kind = static_cast<DrawClass>(visible.draw_class);
        if (kind == DrawClass::wall || kind == DrawClass::connected_wall ||
            kind == DrawClass::cave_wall || kind == DrawClass::stalagmite) {
          continue;
        }
        // A projectile's shadow does not depend on its sprite.
        const double scale = e->projectile ? d->shadow_size / 400.0 * zoom
                                           : e->appearance.size / 100.0 * d->shadow_size / 100.0 *
                                                 base_sprite_height(visible, *d) / 8.0 * zoom;
        const auto p = visible.ground;
        const auto color = d->shadow_color;
        auto shadow = radial_shadow({p.x - 30 * scale, p.y - 15 * scale}, {60 * scale, 0},
                                    {0, 30 * scale},
                                    {static_cast<float>((color >> 16U) & 255U) / 255,
                                     static_cast<float>((color >> 8U) & 255U) / 255,
                                     static_cast<float>(color & 255U) / 255},
                                    .5F);
        fade_quad(shadow, world.player_opacity(*e));
        out.push_back(std::move(shadow));
      }
    }
  }
  // Painter stream: entities by (depth, id) interleaved with particles by (depth,
  // 0x80000000+order). visible_ is already in entity order, and with non-negative ids every entity
  // precedes every particle at equal depth, so only the (few) particles need sorting and the two
  // sorted runs merge. A negative id would compare above particles as unsigned, so that rare case
  // keeps the original single sort.
  const auto before = [](const DrawItem& a, const DrawItem& b) {
    return a.depth == b.depth ? a.order < b.order : a.depth < b.depth;
  };
  {
    REALM_ZONE("scene.particle_gather");
    ordered_.clear();
    particle_items_.clear();
    for (const auto& visible : visible_) {
      if (visible.def && !visible.def->draw_under) {
        ordered_.push_back({visible.entity, nullptr, visible.depth,
                            static_cast<std::uint32_t>(visible.id), visible.def, &visible});
      }
    }
    for (const auto& particle : world.particles().particles()) {
      if (particle.motion == ParticleMotion::swirl && (particle.z < 0 || particle.z > 1)) {
        continue;
      }
      const auto at = camera.world_to_screen(particle.position, particle.z);
      if (cull_ && (at.x < -100 * zoom || at.x > width + 100 * zoom || at.y < -100 * zoom ||
                    at.y > height + 100 * zoom)) {
        continue;
      }
      (negative_ids ? ordered_ : particle_items_)
          .push_back({nullptr, &particle,
                      static_cast<int>(camera.world_to_screen(particle.position).y / zoom +
                                       camera.clip_rect().y),
                      0x80000000ULL + particle.order, nullptr, nullptr});
    }
  }
  {
    REALM_ZONE("scene.sort_painter_stream");
    if (negative_ids) {
      std::sort(ordered_.begin(), ordered_.end(), before);
    } else {
      // particle_items_ is gathered in pool order, which is emission (order) order, so the stable
      // bucketing by depth yields exactly the (depth, order) sequence. AOE-heavy fights keep
      // thousands of particles live, where a comparison sort was the largest single cost of the
      // scene.
      if (!bucket_by_depth(particle_items_, merged_, depth_counts_, depth_bucket_minimum_)) {
        std::sort(particle_items_.begin(), particle_items_.end(), before);
      }
      merged_.resize(ordered_.size() + particle_items_.size());
      std::merge(ordered_.begin(), ordered_.end(), particle_items_.begin(), particle_items_.end(),
                 merged_.begin(), before);
      ordered_.swap(merged_);
    }
  }
  {
    REALM_ZONE("scene.draw_stream");
    for (const auto& item : ordered_) {
      if (item.entity) {
        draw(*item.entity, *item.def, item.visible);
        continue;
      }
      const auto& particle = *item.particle;
      const auto at = camera.world_to_screen(particle.position, particle.z);
      if (const auto* tex = world.particles().texture(particle, time)) {
        auto sprite = texture(*tex, static_cast<std::uint32_t>(particle.order), nullptr, angle,
                              time, atlas, animations);
        std::shared_ptr<SurfaceTexture> lazy;
        bool resident = false;
        double density = 1;
        if (appearances) {
          const auto key = appearances->texture_key(
              *tex, static_cast<std::uint32_t>(particle.order), static_cast<int>(particle.size),
              particle.bottom_anchor || particle.motion == ParticleMotion::thrown_sprite, 0, 0);
          if (const auto found = resident_appearance(key, *appearances, atlas)) {
            sprite = found->region;
            density = found->density;
            resident = true;
          } else {
            auto image = appearances->image(key);
            density = image->pixel_scale;
            sprite = {0, 0, 0, image->width, image->height};
            lazy = surface(std::move(image));
          }
        }
        const double scale = lazy || resident ? zoom / density : 5 * particle.size / 100 * zoom;
        const double w = sprite.width * scale;
        const double h = sprite.height * scale;
        auto q =
            lazy ? lazy->quad({at.x - w / 2, at.y - (particle.bottom_anchor ? h : h / 2)}, {w, 0},
                              {0, h})
                 : sprite_quad(sprite, {at.x - w / 2, at.y - (particle.bottom_anchor ? h : h / 2)},
                               {w, 0}, {0, h});
        if (lazy) {
          q.kind = 6;
          q.owner = lazy;
        } else if (resident) {
          q.kind = 6;
        }
        if (particle.motion == ParticleMotion::thrown_sprite) {
          // Source rotates the bitmap fill inside its fixed square.
          const double rotation =
              particle.angle +
              static_cast<double>(static_cast<std::int64_t>(time) - particle.born) * .012;
          q.kind = 8;
          q.uv_offset = {static_cast<float>(std::cos(rotation)),
                         static_cast<float>(std::sin(rotation))};
        }
        out.push_back(std::move(q));
      } else {
        const double size =
            particle.custom_size ? particle.size : std::floor(particle.size / 100 * 5);
        if (size <= 0) {
          continue;
        }
        const double square_extent = (size + 8) * zoom;
        Quad q;
        q.kind = 7;
        q.origin = {static_cast<float>(at.x - square_extent / 2),
                    static_cast<float>(at.y - square_extent / 2)};
        q.axis_x = {static_cast<float>(square_extent), 0};
        q.axis_y = {0, static_cast<float>(square_extent)};
        q.multiply = {static_cast<float>((particle.color >> 16U) & 255U) / 255,
                      static_cast<float>((particle.color >> 8U) & 255U) / 255,
                      static_cast<float>(particle.color & 255U) / 255, 1};
        q.uv_offset = {static_cast<float>(size), static_cast<float>(size)};
        out.push_back(q);
      }
    }
  }
  {
    REALM_ZONE("scene.ground_tops");
    // Tops of the view rectangle, visited in row-major order (index = x + y * map width) directly.
    const int x0 = std::max(0, cx - distance);
    const int y0 = std::max(0, cy - distance);
    const int x1 = std::min(world.width() - 1, cx + distance);
    const int y1 = std::min(world.height() - 1, cy + distance);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        const auto* ground = world.catalog().ground(world.tile(x, y)->type);
        if (!ground || (ground->top.sheet < 0 && ground->top.animated < 0)) {
          continue;
        }
        const auto index = static_cast<std::uint32_t>(x + y * world.width());
        out.push_back(
            ground_quad(texture(ground->top, index, nullptr, angle, time, atlas, animations),
                        {static_cast<double>(x), static_cast<double>(y)}, camera, 1));
      }
    }
  }
  telemetry::set(telemetry::Counter::atlas_resident_bytes, residency_.used_bytes());
  telemetry::set(telemetry::Counter::atlas_shelf_bytes, residency_.occupied_area() * 4);
  telemetry::set(telemetry::Counter::atlas_pages,
                 static_cast<std::uint64_t>(atlas.dynamic_pages()));
}
}
