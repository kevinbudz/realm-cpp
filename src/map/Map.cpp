#include "realm/util/Telemetry.hpp"
#include "realm/map/Map.hpp"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace realm {
namespace {
net::Bytes read(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("Cannot read map: " + path.string());
  f.seekg(0, std::ios::end);
  const auto size = f.tellg();
  if (size < 0 || size > 128 * 1024 * 1024) throw std::runtime_error("Map exceeds file size limit");
  f.seekg(0);
  net::Bytes bytes(static_cast<std::size_t>(size));
  if (!bytes.empty() && !f.read(reinterpret_cast<char*>(bytes.data()), size))
    throw std::runtime_error("Truncated map file");
  return bytes;
}
net::Bytes base64(std::string_view source) {
  constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  net::Bytes result;
  result.reserve(source.size() / 4 * 3);
  std::uint32_t accumulator{};
  int bits{};
  bool padded{};
  for (const auto c : source) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    if (c == '=') {
      padded = true;
      continue;
    }
    const auto value = alphabet.find(c);
    if (padded || value == std::string_view::npos) throw std::runtime_error("Invalid map base64");
    accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<std::uint8_t>(accumulator >> static_cast<unsigned>(bits)));
    }
  }
  return result;
}
net::Bytes inflate_bounded(std::span<const std::uint8_t> input, std::size_t expected = 0) {
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) throw std::runtime_error("Cannot initialize map decompression");
  struct Guard {
    z_stream& s;
    ~Guard() { inflateEnd(&s); }
  } guard{stream};
  if (input.size() > std::numeric_limits<uInt>::max()) throw std::runtime_error("Compressed map too large");
  stream.next_in = const_cast<Bytef*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  net::Bytes result;
  result.reserve(expected ? expected : 65536);
  std::array<Bytef, 65536> block{};
  int code{};
  do {
    stream.next_out = block.data();
    stream.avail_out = static_cast<uInt>(block.size());
    code = inflate(&stream, Z_NO_FLUSH);
    if (code != Z_OK && code != Z_STREAM_END) throw std::runtime_error("Invalid compressed map stream");
    const auto produced = block.size() - stream.avail_out;
    if (result.size() + produced > (expected ? expected : 64 * 1024 * 1024))
      throw std::runtime_error("Inflated map exceeds length limit");
    result.insert(result.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(produced));
  } while (code != Z_STREAM_END);
  if (stream.avail_in || (expected && result.size() != expected))
    throw std::runtime_error("Map compressed data length mismatch");
  return result;
}
class LittleReader {
 public:
  explicit LittleReader(std::span<const std::uint8_t> bytes) : reader_(bytes) {}
  std::uint8_t u8() { return reader_.u8(); }
  std::uint16_t u16() {
    const auto lo = u8();
    return static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(u8()) << 8U));
  }
  std::int16_t i16() { return std::bit_cast<std::int16_t>(u16()); }
  std::int32_t i32() {
    const auto lo = u16();
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(u16()) << 16U));
  }
  std::string string() {
    std::uint32_t length{};
    for (unsigned shift = 0; shift < 35; shift += 7) {
      const auto byte = u8();
      if (shift == 28 && byte > 15) throw std::runtime_error("Wmap string length overflow");
      length |= static_cast<std::uint32_t>(byte & 127U) << shift;
      if (!(byte & 128U)) {
        const auto data = reader_.take(length);
        return {data.begin(), data.end()};
      }
    }
    throw std::runtime_error("Invalid wmap string length");
  }
  void finish() { reader_.finish(); }

 private:
  net::Reader reader_;
};
}
void World::initialize(int width, int height, std::string name) {
  ++generation_;
  ++hud_revision_;
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > 16 * 1024 * 1024)
    throw std::invalid_argument("Invalid map dimensions");
  width_ = width;
  height_ = height;
  name_ = std::move(name);
  ++terrain_revision_;
  terrain_chunks_.assign(static_cast<std::size_t>((width + 7) / 8) * static_cast<std::size_t>((height + 7) / 8),
                         terrain_revision_);
  tiles_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), {});
  ++minimap_revision_;
  minimap_rows_.assign(static_cast<std::size_t>(height), minimap_revision_);
  // A new map starts from a small footprint: capacity grown by the previous map's crowd (dense list, id table) is
  // released.
  particle_effects_.clear();
  entities_.clear();
  if (entities_.capacity() > 2048) std::vector<Entity>().swap(entities_);
  entity_lookup_.clear();
  interactive_.clear();
  sets_.clear();
  sets_.release();
  static_grid_.reset(width, height);
  unresolved_.clear();
  feedback_.clear();
  player_id_ = -1;
  spawn_ = {width / 2.0, height / 2.0};
}
std::uint64_t World::terrain_chunk_revision(int x, int y) const {
  return terrain_chunks_.at(static_cast<std::size_t>(x / 8 + (y / 8) * ((width_ + 7) / 8)));
}
void World::terrain_changed(int x, int y) {
  const auto revision = ++terrain_revision_;
  // A compositor samples a 3x3 neighbourhood: edge changes dirty adjacent chunks too.
  for (int cy = std::max(0, (y - 1) / 8); cy <= std::min((height_ - 1) / 8, (y + 1) / 8); ++cy)
    for (int cx = std::max(0, (x - 1) / 8); cx <= std::min((width_ - 1) / 8, (x + 1) / 8); ++cx)
      terrain_chunks_[static_cast<std::size_t>(cx + cy * ((width_ + 7) / 8))] = revision;
}
TileState* World::tile(int x, int y) {
  return x < 0 || y < 0 || x >= width_ || y >= height_ ? nullptr : &tiles_[static_cast<std::size_t>(x + y * width_)];
}
const TileState* World::tile(int x, int y) const { return const_cast<World*>(this)->tile(x, y); }
Entity* World::entity(std::int32_t id) {
  const auto slot = entity_lookup_.find(id);
  return slot == EntityIndex::none ? nullptr : &entities_[slot];
}
const Entity* World::entity(std::int32_t id) const { return const_cast<World*>(this)->entity(id); }
const Entity* World::interactive_target() const {
  REALM_ZONE("world.interactive_target");
  const auto* player = entity(player_id_);
  if (!player) return nullptr;
  // Same result as scanning the dense list in order with a strict `<`: the smallest distance, and among exact ties the
  // entity earliest in the list. Only interactive entities are candidates, so the cost no longer grows with
  // projectiles, players or scenery.
  const Entity* closest{};
  double best = 1;
  std::size_t closest_index{};
  for (const auto id : interactive_) {
    const auto found = entity_lookup_.find(id);
    if (found == EntityIndex::none) continue;
    const auto& e = entities_[found];
    const double distance =
        std::hypot(e.motion.position.x - player->motion.position.x, e.motion.position.y - player->motion.position.y);
    if (distance < best || (distance == best && closest && found < closest_index)) {
      best = distance;
      closest = &e;
      closest_index = found;
    }
  }
  return closest;
}
void World::assign_tile_object(Entity& e) {
  const auto* d = catalog_.object(e.type);
  if (d && d->stationary)
    if (auto* t =
            tile(static_cast<int>(std::floor(e.motion.position.x)), static_cast<int>(std::floor(e.motion.position.y))))
      t->object_id = e.id;
}
void World::remember_ground(int x, int y, std::uint16_t type) {
  if (auto* cell = tile(x, y)) {
    const auto code = 0x20000U | type;
    if (cell->minimap_code != code) {
      cell->minimap_code = code;
      minimap_rows_[static_cast<std::size_t>(y)] = ++minimap_revision_;
    }
  }
}
void World::remember_object(const Entity& e) {
  const auto* def = catalog_.object(e.type);
  if (!def || !def->stationary || !def->occupy || def->no_minimap) return;
  const int x = static_cast<int>(e.motion.position.x), y = static_cast<int>(e.motion.position.y);
  if (auto* cell = tile(x, y)) {
    minimap_object_ids_.try_emplace(e.type, static_cast<std::uint32_t>(e.id));
    const auto code = 0x30000U | e.type;
    if (cell->minimap_code != code) {
      cell->minimap_code = code;
      minimap_rows_[static_cast<std::size_t>(y)] = ++minimap_revision_;
    }
  }
}
Entity& World::add(std::int32_t id, std::uint16_t type, Vec2 position, int size, bool projectile) {
  ++hud_revision_;
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || std::abs(position.x) > 1048576 ||
      std::abs(position.y) > 1048576)
    throw net::ProtocolError("Invalid entity position");
  if (entity(id)) remove(id);
  Entity e;
  e.id = id;
  e.type = type;
  e.motion.position = e.motion.target = position;
  e.projectile = projectile;
  if (const auto* d = catalog_.object(type)) {
    e.player = d->player;
    e.appearance.size = size < 0 ? d->min_size : size;
    e.motion.z = d->z;
    e.combat.hp = e.combat.max_hp = d->max_hp;
    e.combat.defense = d->defense;
    e.appearance.tex1 = d->tex1;
    e.appearance.tex2 = d->tex2;
  }
  particle_effects_.add_entity(e, catalog_.object(type));
  if (const auto* d = catalog_.object(type); d && d->interactive) interactive_.push_back(id);
  if (entities_.size() >= EntityIndex::none - 1) throw std::length_error("Too many objects");
  const auto index = static_cast<std::uint32_t>(entities_.size());
  entity_lookup_.assign(id, index);
  entities_.push_back(std::move(e));
  assign_tile_object(entities_.back());
  remember_object(entities_.back());
  // A projectile visual only ever joins the dynamic set (Combat moves it; the other sets' consumers skip projectiles
  // anyway, and their membership is still decided by the type below for anything else).
  const auto mask = projectile ? type_sets(type) | (1U << set_dynamic) : type_sets(type);
  sets_.push(mask & 0x3fU);
  static_grid_.push_slot();
  if (!(mask & (1U << set_dynamic))) static_grid_.insert(index, position);
  return entities_.back();
}
std::uint32_t World::type_sets(std::uint16_t type) {
  if (type_sets_.empty()) type_sets_.assign(65536, 0);
  auto& cached = type_sets_[type];
  if (!(cached & 0x80U)) {
    const auto* d = catalog_.object(type);
    std::uint32_t mask = 0x80U;
    if (d && d->enemy) mask |= 1U << set_enemy;
    if (d && d->player) mask |= 1U << set_player;
    if (d && !d->no_minimap &&
        (d->player || d->class_name == "Character" || d->class_name == "Portal" || d->class_name == "GuildHallPortal"))
      mask |= 1U << set_marker;
    if (d && d->class_name == "Merchant") mask |= 1U << set_merchant;
    if (!(d && d->stationary)) mask |= 1U << set_dynamic;
    cached = static_cast<std::uint8_t>(mask);
  }
  return cached;
}
void World::set_player_id(std::int32_t id) {
  // Player.cpp drives the focus player's velocity outside World; once it is no longer the focus, update() must see
  // it.
  if (const auto index = entity_lookup_.find(player_id_); index != EntityIndex::none) sets_.set(set_moving, index);
  player_id_ = id;
}
std::int32_t World::allocate_fake_id() {
  // BasicObject.as reserves the upper byte; unsigned wrap is defined and an
  // existing object is never replaced when the 24-bit counter wraps.
  for (std::uint32_t attempts = 0; attempts < 0x1000000U; ++attempts) {
    const auto id = static_cast<std::int32_t>(0x7f000000U | (fake_counter_++ & 0xffffffU));
    if (!entity(id)) return id;
  }
  throw std::runtime_error("Local object ID space exhausted");
}
void World::remove(std::int32_t id) {
  ++hud_revision_;
  const auto index = entity_lookup_.find(id);
  if (index == EntityIndex::none) return;
  particle_effects_.remove_entity(id);
  const auto& e = entities_[index];
  const auto* definition = catalog_.object(e.type);
  const bool was_interactive = definition && definition->interactive;
  if (auto* t =
          tile(static_cast<int>(std::floor(e.motion.position.x)), static_cast<int>(std::floor(e.motion.position.y)));
      t && t->object_id == id)
    t->object_id = -1;
  if (static_grid_.contains(index)) static_grid_.erase(index);
  if (index != entities_.size() - 1) {
    entities_[index] = std::move(entities_.back());
    entity_lookup_.assign(entities_[index].id, static_cast<std::uint32_t>(index));
  }
  entities_.pop_back();
  entity_lookup_.erase(id);
  sets_.swap_remove(index);
  static_grid_.swap_remove(index);
  if (was_interactive)
    if (const auto slot = std::find(interactive_.begin(), interactive_.end(), id); slot != interactive_.end()) {
      *slot = interactive_.back();
      interactive_.pop_back();
    }
}
void World::goto_entity(std::int32_t id, Vec2 position) {
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || std::abs(position.x) > 1048576 ||
      std::abs(position.y) > 1048576)
    throw net::ProtocolError("Invalid teleport position");
  if (const auto index = entity_lookup_.find(id); index != EntityIndex::none) {
    auto* e = &entities_[index];
    if (auto* t = tile(static_cast<int>(std::floor(e->motion.position.x)),
                       static_cast<int>(std::floor(e->motion.position.y)));
        t && t->object_id == id)
      t->object_id = -1;
    e->motion.position = e->motion.target = position;
    e->motion.velocity = {};
    assign_tile_object(*e);
    sets_.reset(set_moving, index);
    static_grid_.move(index, position);
  }
}
void World::report_damage(std::int32_t id, int amount, std::int32_t time, std::span<const std::uint8_t> effects,
                          bool piercing, double hit_angle, double hit_speed) {
  ++hud_revision_;
  const auto* target = entity(id);
  if (!target) return;
  // GameServerConnection.onDamage: damage to other objects is dropped when Ally Damage is off.
  if (!preferences_.ally_damage && id != player_id_) return;
  // Character.damage: the hit sound comes with every damage call, even a zero-amount one.
  sound_hit(id);
  // GameObject.damage displays feedback. HP and conditions change only when
  // the authoritative server stats arrive, including locally detected hits.
  particle_effects_.damaged(*this, *target, amount, time, hit_angle, hit_speed);
  feedback_.report_damage(id, amount, time, effects, piercing, target->combat.condition);
}
void World::sound_hit(std::int32_t id) {
  const auto* target = entity(id);
  if (!target) return;
  sound_events_.hit(catalog_.object(target->type));
}
bool World::allows_notification(std::int32_t object_id) const {
  if (preferences_.ally_notifs || object_id == player_id_) return true;
  const auto found = entity_lookup_.find(object_id);
  return found == EntityIndex::none || !entities_[found].player;
}
void World::notify(std::int32_t object_id, std::string text, std::uint32_t color, int lifetime, int offset_time) {
  if (!entity(object_id)) return;
  feedback_.add_status({object_id, std::move(text), color, lifetime, offset_time});
}
std::vector<StatusEvent> World::take_status_events() { return feedback_.take_status_events(); }
void World::expire_damage(std::int32_t time) {
  feedback_.expire_damage(time, [this](std::int32_t id) { return entity(id) != nullptr; });
}
void World::status(Entity& e, const net::Status& s, bool interpolate, double dt_ms) {
  ++hud_revision_;
  const Vec2 position{s.position.x, s.position.y};
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || std::abs(position.x) > 1048576 ||
      std::abs(position.y) > 1048576)
    throw net::ProtocolError("Invalid status position");
  if (interpolate && e.id != player_id_ && (e.motion.target.x != position.x || e.motion.target.y != position.y)) {
    e.motion.target = position;
    e.motion.velocity = {(position.x - e.motion.position.x) / dt_ms, (position.y - e.motion.position.y) / dt_ms};
    if (!(e.motion.velocity.x == 0 && e.motion.velocity.y == 0))
      sets_.set(set_moving, static_cast<std::size_t>(&e - entities_.data()));
  } else if (!interpolate) {
    goto_entity(e.id, position);
  }
  // GameServerConnection.onNewTick: level, exp and fame as they were before this update.
  const auto previous = [&](flash::StatId id, int fallback) {
    const auto it = e.profile.stats.find(static_cast<std::uint8_t>(id));
    return it == e.profile.stats.end() ? fallback : it->second;
  };
  const int old_level = previous(flash::StatId::LEVEL_STAT, -1), old_exp = previous(flash::StatId::EXP_STAT, 0),
            old_fame = previous(flash::StatId::CHAR_FAME_STAT, 0);
  if (e.profile.stats.empty())
    e.profile.stats.reserve(s.stats.size());  // the first status carries the full set; later ones rewrite it
  for (const auto& stat : s.stats) {
    if (std::holds_alternative<std::string>(stat.value)) {
      if (stat.type == static_cast<std::uint8_t>(flash::StatId::NAME_STAT))
        e.profile.name = std::get<std::string>(stat.value);
      if (stat.type == static_cast<std::uint8_t>(flash::StatId::GUILD_NAME_STAT))
        e.profile.guild = std::get<std::string>(stat.value);
      continue;
    }
    const auto value = std::get<std::int32_t>(stat.value);
    if (stat.type == static_cast<std::uint8_t>(flash::StatId::LEVEL_STAT)) {
      const auto old = e.profile.stats.find(stat.type);
      if (e.player && old != e.profile.stats.end() && old->second >= 0 && value > old->second &&
          (e.id == player_id_ || preferences_.ally_notifs))
        particle_effects_.level_up(e.id);
      // Player.handleLevelUp runs for the own player only; other players' level-ups show the notification alone.
      if (e.player && e.id == player_id_ && old != e.profile.stats.end() && old->second >= 0 && value > old->second)
        sound_events_.own_player_level_up();
    }
    e.profile.stats[stat.type] = value;
    switch (static_cast<flash::StatId>(stat.type)) {
      case flash::StatId::HP_STAT:
        e.combat.hp = value;
        break;
      case flash::StatId::MAX_HP_STAT:
        e.combat.max_hp = value;
        break;
      case flash::StatId::SIZE_STAT:
        e.appearance.size = value;
        break;
      case flash::StatId::SPEED_STAT:
        e.motion.speed = value;
        break;
      case flash::StatId::ATTACK_STAT:
        e.combat.attack = value;
        break;
      case flash::StatId::DEXTERITY_STAT:
        e.combat.dexterity = value;
        break;
      case flash::StatId::DEFENSE_STAT:
        e.combat.defense = value;
        break;
      case flash::StatId::CONDITION_STAT:
        e.combat.condition = static_cast<std::uint32_t>(value);
        particle_effects_.condition_changed(e);
        break;
      case flash::StatId::TEX1_STAT:
        e.appearance.tex1 = static_cast<std::uint32_t>(value);
        break;
      case flash::StatId::TEX2_STAT:
        e.appearance.tex2 = static_cast<std::uint32_t>(value);
        break;
      case flash::StatId::ALT_TEXTURE_STAT:
        e.appearance.alt_texture = value;
        particle_effects_.texture_changed(e, catalog_.object(e.type));
        break;
      case flash::StatId::TEXTURE_STAT:
        e.appearance.texture = value;
        break;
      default:
        break;
    }
  }
  // Other players' level-ups and experience are shown only with Ally Notifications on.
  if (e.player && old_level != -1 && (e.id == player_id_ || preferences_.ally_notifs)) {
    const int level = previous(flash::StatId::LEVEL_STAT, -1), exp = previous(flash::StatId::EXP_STAT, 0),
              fame = previous(flash::StatId::CHAR_FAME_STAT, 0);
    feedback_.report_level_progress(e.id, old_level, old_exp, old_fame, level, exp, fame);
  }
}
void World::apply(const net::Update& u) {
  ++hud_revision_;
  for (const auto& t : u.tiles)
    if (auto* out = tile(t.x, t.y)) {
      if (out->type != t.type) terrain_changed(t.x, t.y);
      out->type = t.type;
      remember_ground(t.x, t.y, t.type);
    }
  for (const auto& o : u.objects) {
    auto& e = add(o.status.id, o.type, {o.status.position.x, o.status.position.y});
    status(e, o.status, false, 100);
    const auto* player = entity(player_id_);
    sound_events_.game_object_added(catalog_.object(o.type), {o.status.position.x, o.status.position.y},
                                    player ? &player->motion.position : nullptr);
  }
  for (const auto& d : u.drops) {
    // GameServerConnection.dropObject: only an exploding Character plays its death sound (Character.explode).
    if (d.explode) {
      const auto* e = entity(d.id);
      sound_events_.game_object_dropped(e ? catalog_.object(e->type) : nullptr, d.explode);
    }
    remove(d.id);
  }
}
void World::apply(const net::Tick& tick, double dt_ms) {
  ++hud_revision_;
  if (!std::isfinite(dt_ms) || dt_ms <= 0) throw std::invalid_argument("Invalid tick interval");
  for (const auto& s : tick.statuses)
    if (auto* e = entity(s.id)) status(*e, s, true, dt_ms);
  if (auto* e = entity(player_id_)) {
    net::Status s;
    s.stats = tick.player_stats;
    status(*e, s, true, dt_ms);
  }
}
void World::update(double dt_ms) {
  REALM_ZONE("world.update");
  if (!std::isfinite(dt_ms) || dt_ms < 0) throw std::invalid_argument("Invalid frame interval");
  // Only entities World set moving, in dense order: the same visits, in the same order, as the full pass this replaces
  // (the order matters only when two moving stationary objects claim one tile in the same frame).
  sets_.each(set_moving, [&](std::size_t i) {
    auto& e = entities_[i];
    if (e.id == player_id_) return;
    if (e.motion.velocity.x == 0 && e.motion.velocity.y == 0) {
      sets_.reset(set_moving, i);
      return;
    }
    Vec2 next{e.motion.position.x + e.motion.velocity.x * dt_ms, e.motion.position.y + e.motion.velocity.y * dt_ms};
    if ((e.motion.velocity.x > 0 && next.x > e.motion.target.x) ||
        (e.motion.velocity.x < 0 && next.x < e.motion.target.x)) {
      next.x = e.motion.target.x;
      e.motion.velocity.x = 0;
    }
    if ((e.motion.velocity.y > 0 && next.y > e.motion.target.y) ||
        (e.motion.velocity.y < 0 && next.y < e.motion.target.y)) {
      next.y = e.motion.target.y;
      e.motion.velocity.y = 0;
    }
    if (auto* t =
            tile(static_cast<int>(std::floor(e.motion.position.x)), static_cast<int>(std::floor(e.motion.position.y)));
        t && t->object_id == e.id)
      t->object_id = -1;
    e.motion.position = next;
    assign_tile_object(e);
    static_grid_.move(static_cast<std::uint32_t>(i), next);
    if (e.motion.velocity.x == 0 && e.motion.velocity.y == 0) sets_.reset(set_moving, i);
  });
}
bool World::walkable(double x, double y) const {
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= width_ || y >= height_) return false;
  const auto* t = tile(static_cast<int>(x), static_cast<int>(y));
  const auto* g = t ? catalog_.ground(t->type) : nullptr;
  if (!g || g->no_walk) return false;
  const auto* e = entity(t->object_id);
  const auto* d = e ? catalog_.object(e->type) : nullptr;
  return !d || !d->occupy;
}
void World::load_jm(const std::filesystem::path& path) {
  const auto bytes = read(path);
  load_jm_text({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  if (name_.empty()) name_ = path.stem().string();
}
void World::load_jm_text(std::string_view source, int wrap_border) {
  const auto jm = nlohmann::json::parse(source);
  const int width = jm.at("width").get<int>(), height = jm.at("height").get<int>();
  if (wrap_border < 0 || width <= 0 || wrap_border > width / 2) throw std::invalid_argument("Invalid JM wrap border");
  initialize(width + 2 * wrap_border, height, jm.value("name", std::string{}));
  const auto dict = jm.at("dict");
  if (!dict.is_array() || dict.empty() || dict.size() > 32768) throw std::runtime_error("Invalid JM dictionary");
  const auto data = inflate_bounded(base64(jm.at("data").get<std::string>()),
                                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2);
  net::Reader reader(data);
  std::int32_t fake_id = 2130706432;
  bool spawned{};
  for (int y = 0; y < height_; ++y)
    for (int source_x = 0; source_x < width; ++source_x) {
      const auto index = reader.i16();
      if (index < 0 || static_cast<std::size_t>(index) >= dict.size())
        throw std::runtime_error("JM dictionary index out of range");
      const auto& entry = dict[static_cast<std::size_t>(index)];
      // MapBackground writes the map again at its own width, clipped to the border.
      for (int x = source_x; x < width_; x += width) {
        auto* t = tile(x, y);
        if (entry.contains("ground")) {
          const auto id = entry["ground"].get<std::string>();
          const auto* g = catalog_.ground(id);
          if (g) {
            t->type = g->type;
            remember_ground(x, y, g->type);
          } else if (std::find(unresolved_.begin(), unresolved_.end(), id) == unresolved_.end())
            unresolved_.push_back(id);
        }
        if (entry.contains("regions"))
          for (const auto& r : entry["regions"])
            if (!spawned && r.value("id", "") == "Spawn") {
              spawn_ = {x + .5, y + .5};
              spawned = true;
            }
        if (entry.contains("objs"))
          for (const auto& obj : entry["objs"]) {
            const auto id = obj.at("id").get<std::string>();
            const auto* d = catalog_.object(id);
            if (d)
              add(fake_id++, d->type, {x + .5, y + .5}, obj.value("size", -1));
            else if (std::find(unresolved_.begin(), unresolved_.end(), id) == unresolved_.end())
              unresolved_.push_back(id);
          }
      }
    }
  reader.finish();
  if (!spawned) {
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x)
        if (walkable(x + .5, y + .5)) {
          spawn_ = {x + .5, y + .5};
          return;
        }
  }
}
void World::load_wmap(const std::filesystem::path& path) {
  unresolved_.clear();
  const auto source = read(path);
  if (source.empty() || source[0] > 2) throw std::runtime_error("Unsupported WMAP version");
  const auto version = source[0];
  const auto bytes = inflate_bounded(std::span(source).subspan(1));
  LittleReader r(bytes);
  struct Entry {
    std::uint16_t ground{}, object{};
    bool has_object{};
    std::string config;
    std::uint8_t terrain{}, region{}, elevation{};
  };
  const auto count = r.i16();
  if (count <= 0) throw std::runtime_error("Invalid WMAP dictionary length");
  std::vector<Entry> entries;
  entries.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    Entry e;
    e.ground = r.u16();
    if (!catalog_.ground(e.ground)) e.ground = 255;
    const auto id = r.string();
    if (const auto* d = catalog_.object(id)) {
      e.object = d->type;
      e.has_object = true;
    } else if (!id.empty() && std::find(unresolved_.begin(), unresolved_.end(), id) == unresolved_.end())
      unresolved_.push_back(id);
    e.config = r.string();
    e.terrain = r.u8();
    e.region = r.u8();
    if (version == 1) e.elevation = r.u8();
    entries.push_back(std::move(e));
  }
  const auto unresolved = std::move(unresolved_);
  const int width = r.i32(), height = r.i32();
  initialize(width, height, path.stem().string());
  unresolved_ = unresolved;
  std::int32_t fake_id = 2130706432;
  bool spawned{};
  for (int y = 0; y < height_; ++y)
    for (int x = 0; x < width_; ++x) {
      const auto index = r.i16();
      if (index < 0 || index >= count) throw std::runtime_error("WMAP index out of range");
      const auto& e = entries[static_cast<std::size_t>(index)];
      auto* t = tile(x, y);
      t->type = e.ground;
      remember_ground(x, y, e.ground);
      t->terrain = e.terrain;
      t->elevation = version == 2 ? r.u8() : e.elevation;
      if (e.has_object) add(fake_id++, e.object, {x + .5, y + .5});
      if (!spawned && e.region == 1) {
        spawn_ = {x + .5, y + .5};
        spawned = true;
      }
    }
  r.finish();
}
}
