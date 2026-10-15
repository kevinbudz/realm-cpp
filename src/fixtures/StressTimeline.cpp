#include "realm/fixtures/StressTimeline.hpp"
#include "realm/engine3d/Model3D.hpp"
#include "realm/stage3D/WallGeometry.hpp"
#include "realm/util/Telemetry.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <set>

namespace realm::quality {
using Json = nlohmann::json;

namespace {
void hash(std::uint64_t& h, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h ^= (v >> (i * 8)) & 255;
    h *= 1099511628211ULL;
  }
}

template <std::size_t N>
void hash_floats(std::uint64_t& h, const std::array<float, N>& values) {
  for (const auto v : values) {
    hash(h, std::bit_cast<std::uint32_t>(v));
  }
}

void save(const std::filesystem::path& p, const Json& j) {
  if (!p.parent_path().empty()) {
    std::filesystem::create_directories(p.parent_path());
  }
  std::ofstream f(p);
  if (!f) {
    throw std::runtime_error("Cannot write stress result");
  }
  f << j.dump(2) << '\n';
}

void status(net::Writer& w, const net::Status& s) {
  w.i32(s.id);
  w.f32(s.position.x);
  w.f32(s.position.y);
  w.u8(static_cast<std::uint8_t>(s.stats.size()));
  for (const auto& stat : s.stats) {
    const bool string_type = std::find(flash::string_stats.begin(), flash::string_stats.end(),
                                       stat.type) != flash::string_stats.end();
    if (string_type != std::holds_alternative<std::string>(stat.value)) {
      throw net::ProtocolError("Fixture stat wire type mismatch");
    }
    w.u8(stat.type);
    if (const auto* text = std::get_if<std::string>(&stat.value)) {
      w.utf(*text);
    } else {
      w.i32(std::get<std::int32_t>(stat.value));
    }
  }
}

net::Packet encode(const net::Update& u) {
  REALM_ZONE("fixture.encode");
  net::Writer w;
  w.u16(static_cast<std::uint16_t>(u.tiles.size()));
  for (const auto& t : u.tiles) {
    w.u16(static_cast<std::uint16_t>(t.x));
    w.u16(static_cast<std::uint16_t>(t.y));
    w.u16(t.type);
  }
  w.u16(static_cast<std::uint16_t>(u.objects.size()));
  for (const auto& o : u.objects) {
    w.u16(o.type);
    status(w, o.status);
  }
  w.u16(static_cast<std::uint16_t>(u.drops.size()));
  for (const auto& d : u.drops) {
    w.i32(d.id);
    w.boolean(d.explode);
  }
  return {static_cast<std::uint8_t>(flash::PacketId::UPDATE), w.bytes()};
}

net::Packet encode(const net::Tick& t) {
  REALM_ZONE("fixture.encode");
  net::Writer w;
  w.u16(static_cast<std::uint16_t>(t.statuses.size()));
  for (const auto& s : t.statuses) {
    status(w, s);
  }
  return {static_cast<std::uint8_t>(flash::PacketId::NEWTICK), w.bytes()};
}

net::Packet encode_shot(const net::Event& e) {
  REALM_ZONE("fixture.encode");
  net::Writer w;
  if (e.id == flash::PacketId::ENEMYSHOOT) {
    const auto& s = std::get<net::incoming::EnemyShoot>(e.data);
    w.i32(s.bullet_id);
    w.i32(s.owner_id);
    w.u8(s.bullet_type);
    w.f32(s.position.x);
    w.f32(s.position.y);
    w.f32(s.angle);
    w.u16(static_cast<std::uint16_t>(s.damage));
    w.u8(s.num_shots);
    w.f32(s.angle_increment);
  } else {
    const auto& s = std::get<net::incoming::ServerPlayerShoot>(e.data);
    w.i32(s.bullet_id);
    w.i32(s.owner_id);
    w.u16(std::bit_cast<std::uint16_t>(s.container_type));
    w.f32(s.position.x);
    w.f32(s.position.y);
    w.f32(s.angle);
    w.f32(s.angle_increment);
    w.u8(static_cast<std::uint8_t>(s.damages.size()));
    for (auto d : s.damages) {
      w.u16(std::bit_cast<std::uint16_t>(d));
    }
  }
  return {static_cast<std::uint8_t>(e.id), w.bytes()};
}

const ObjectDef& require(const Catalog& c, const std::string& id) {
  const auto* d = c.object(id);
  if (!d) {
    throw std::invalid_argument("Unknown fixture definition: " + id);
  }
  return *d;
}
}

StressConfig StressConfig::load(const std::filesystem::path& path) {
  const auto j = Json::parse(std::ifstream(path));
  StressConfig c;
  c.version = j.at("version");
  c.workload = j.at("workload");
  c.seed = j.at("seed");
  c.step_ms = j.value("step_ms", 20);
  c.players = j.value("players", 0);
  c.enemies = j.value("enemies", 0);
  c.models = j.value("models", 0);
  c.projectiles = j.value("projectiles", 0);
  c.arrangement = j.value("arrangement", std::string("dense"));
  c.camera = j.value("camera", std::string("idle"));
  c.moving = j.value("moving", true);
  c.rotate_models = j.value("rotate_models", false);
  c.working_set = j.value("working_set", 16);
  c.reset_every = j.value("reset_every", 0);
  c.replace_every = j.value("replace_every", 0);
  c.player = j.at("player");
  c.enemy = j.at("enemy");
  c.weapon = j.at("weapon");
  c.multi_weapon = j.at("multi_weapon");
  c.model_objects = j.at("model_objects").get<std::vector<std::string>>();
  c.skins = j.at("skins").get<std::vector<int>>();
  c.dyes = j.at("dyes").get<std::vector<std::uint32_t>>();
  if (j.contains("map")) {
    c.map = j.at("map").get<std::string>();
    if (c.map.is_relative()) {
      c.map = path.parent_path() / c.map;
    }
  }
  const std::set<std::string> workloads{"NEXUS", "POPULATION", "COMBAT", "MODELS", "VARIETY",
                                        "CHURN", "UI",         "MIXED",  "MEDUSA"};
  if (c.version != 1 || c.step_ms != 20 || !workloads.contains(c.workload) || c.players < 0 ||
      c.enemies < 0 || c.models < 0 || c.projectiles < 0 || c.players > 6000 || c.enemies > 6000 ||
      c.models > 6000 || c.projectiles > 40000 || c.working_set < 1 || c.working_set > 8192 ||
      c.reset_every < 0 || c.replace_every < 0 || c.model_objects.empty() || c.dyes.empty() ||
      c.skins.empty() ||
      (c.arrangement != "dense" && c.arrangement != "spread" && c.arrangement != "cluster" &&
       c.arrangement != "near") ||
      (c.camera != "idle" && c.camera != "pan" && c.camera != "rotate" && c.camera != "zoom" &&
       c.camera != "walk")) {
    throw std::invalid_argument("Invalid stress manifest");
  }
  return c;
}

StressTimeline::StressTimeline(World& w, StressConfig c)
    : world_(w), config_(std::move(c)), combat_(w) {
  const auto& cat = w.catalog();
  if (!require(cat, config_.player).player || !require(cat, config_.enemy).enemy) {
    throw std::invalid_argument("Wrong stress entity class");
  }
  for (const auto& id : config_.model_objects) {
    if (!has_model(require(cat, id).model)) {
      throw std::invalid_argument("Unregistered stress model");
    }
  }
  for (const auto id : config_.skins) {
    if (id != 0 && !cat.skin(id, require(cat, config_.player).type)) {
      throw std::invalid_argument("Invalid stress skin/class pair");
    }
  }
  for (const auto& id : {config_.weapon, config_.multi_weapon}) {
    const auto& d = require(cat, id);
    if (d.slot_type != require(cat, config_.player).slot_types.at(0) || d.projectiles.empty() ||
        d.projectiles.front().id != 0 || !cat.object(d.projectiles.front().object_id)) {
      throw std::invalid_argument("Invalid stress weapon");
    }
  }
  if (config_.workload == "MIXED") {
    SessionOptions options;
    options.username = "Q02Fixture";
    options.password = "disposable-mock";
    options.character_id = 7;
    replay_ = std::make_unique<Session>(
        world_, [this](const net::Packet& p) { packets({p}); }, std::move(options));
  }
  populate();
}

Vec2 StressTimeline::placement(int i, int kind) const {
  const auto center = world_.spawn_position();
  // Dense is entirely inside the 800x600 camera footprint at ordinary zoom.
  const double radius = config_.arrangement == "spread"    ? 45
                        : config_.arrangement == "cluster" ? .08
                        : config_.arrangement == "near"    ? 7.0
                                                           : 2.0;
  const auto s = config_.seed + static_cast<std::uint32_t>(i) * 2654435761U +
                 static_cast<std::uint32_t>(kind) * 97U;
  return {center.x + radius * (static_cast<double>(s & 1023U) / 512 - 1),
          center.y + radius * (static_cast<double>((s >> 10) & 1023U) / 512 - 1)};
}

net::Object StressTimeline::object(int i, int kind) const {
  const auto& cat = world_.catalog();
  const auto& id =
      kind == 0 ? config_.player
      : kind == 1
          ? config_.enemy
          : config_.model_objects.at(static_cast<std::size_t>(i) % config_.model_objects.size());
  const auto& d = require(cat, id);
  const auto at = placement(i, kind);
  net::Object o;
  o.type = d.type;
  o.status.id = 100 + kind * 10000 + i;
  o.status.position = {static_cast<float>(at.x), static_cast<float>(at.y)};
  if (kind < 2) {
    o.status.stats = {{0, 1000},
                      {1, 700},
                      {static_cast<std::uint8_t>(flash::StatId::NAME_STAT),
                       std::string(kind == 0 ? "Player" : "Enemy") + std::to_string(i)},
                      {static_cast<std::uint8_t>(flash::StatId::SIZE_STAT), 100 + (i % 3) * 25}};
    if (kind == 0) {
      const int appearance = (i + tick_ / 25) % config_.working_set;
      const auto skin =
          config_.skins.at(static_cast<std::size_t>(appearance) % config_.skins.size());
      o.status.stats.push_back({70, skin});
      o.status.stats.push_back(
          {29, std::bit_cast<std::int32_t>(
                   config_.dyes.at(static_cast<std::size_t>(appearance) % config_.dyes.size()))});
      o.status.stats.push_back({30, std::bit_cast<std::int32_t>(config_.dyes.at(
                                        static_cast<std::size_t>(appearance / config_.dyes.size()) %
                                        config_.dyes.size()))});
    }
  }
  return o;
}

void StressTimeline::feed(net::Packet packet) {
  REALM_ZONE("fixture.framed_replay");
  const auto bytes = net::frame(packet.id, packet.body);
  telemetry::add(telemetry::Counter::packet_rx_bytes, bytes.size());
  // Split the length field, then feed ordinary reusable receive-sized chunks.
  for (std::size_t at = 0; at < bytes.size();) {
    const auto n = std::min<std::size_t>(at == 0 ? 3 : 4096, bytes.size() - at);
    for (const auto& p : framer_.feed(std::span<const std::uint8_t>(bytes).subspan(at, n))) {
      try {
        replay_->replay(p, time());
      } catch (const net::ProtocolError& e) {
        throw net::ProtocolError("Replay packet " + std::to_string(p.id) + ": " + e.what());
      }
      ++replayed_packets_;
    }
    at += n;
  }
  (void)replay_->take_events();
}

void StressTimeline::apply(const net::Update& u) {
  if (replay_) {
    feed(encode(u));
  } else {
    world_.apply(u);
  }
}

void StressTimeline::apply(const net::Tick& t) {
  if (replay_) {
    feed(encode(t));
  } else {
    world_.apply(t, 127);
  }
}

void StressTimeline::receive(const net::Event& e, std::int32_t now) {
  if (replay_) {
    feed(encode_shot(e));
  } else {
    packets(combat_.receive(e, now));
  }
}

void StressTimeline::populate() {
  combat_.reset(config_.seed);
  if (replay_) {
    net::Writer info;
    info.i32(128);
    info.i32(128);
    info.utf("Nexus");
    info.utf("Quality stress v1");
    info.u32(config_.seed);
    info.i32(0);
    info.boolean(true);
    info.boolean(true);
    feed({static_cast<std::uint8_t>(flash::PacketId::MAPINFO), info.bytes()});
  }
  if (!config_.map.empty()) {
    if (config_.map.extension() == ".wmap") {
      world_.load_wmap(config_.map);
    } else {
      world_.load_jm(config_.map);
    }
  } else {
    if (!replay_) {
      world_.initialize(128, 128, "Quality stress v1");
    }
    const auto grass = world_.catalog().ground("Grass")->type;
    net::Update tiles;
    for (int y = 0; y < 128; ++y) {
      for (int x = 0; x < 128; ++x) {
        tiles.tiles.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), grass});
      }
    }
    apply(tiles);
  }
  const auto at = world_.spawn_position();
  world_.add(1, require(world_.catalog(), config_.player).type, at);
  world_.set_player_id(1);
  auto* p = world_.entity(1);
  p->profile.name = "QualityFixture";
  p->combat.max_hp = p->combat.hp = 1000;
  p->profile.stats[8] = require(world_.catalog(), config_.weapon).type;
  p->profile.stats[4] = 1000;
  p->profile.stats[3] = 1000;
  p->profile.stats[5] = 50;
  p->profile.stats[6] = 0;
  p->profile.stats[7] = 1;
  p->motion.speed = 50;
  p->combat.dexterity = 50;
  p->combat.attack = 50;
  net::Update update;
  const int counts[] = {config_.players, config_.enemies, config_.models};
  for (int kind = 0; kind < 3; ++kind) {
    for (int i = 0; i < counts[kind]; ++i) {
      update.objects.push_back(object(i, kind));
    }
  }
  apply(update);
  spawned_ += update.objects.size();
  if (replay_) {
    net::Writer success;
    success.i32(1);
    success.i32(7);
    feed({static_cast<std::uint8_t>(flash::PacketId::CREATE_SUCCESS), success.bytes()});
  }
}

void StressTimeline::packets(std::vector<net::Packet> output) {
  for (const auto& p : output) {
    ++packet_counts_[p.id];
    if (p.id == static_cast<int>(flash::PacketId::ENEMYHIT) ||
        p.id == static_cast<int>(flash::PacketId::PLAYERHIT)) {
      ++hits_;
    }
    for (auto b : net::frame(p.id, p.body)) {
      packet_hash_ ^= b;
      packet_hash_ *= 1099511628211ULL;
    }
  }
}

void StressTimeline::step() {
  REALM_ZONE("fixture.timeline");
  const auto now = time();
  if (tick_ && config_.reset_every && tick_ % config_.reset_every == 0) {
    removed_ += world_.entities().size();
    populate();
    ++resets_;
  }
  if (tick_ && config_.replace_every && tick_ % config_.replace_every == 0) {
    net::Update u;
    for (int kind = 0; kind < 3; ++kind) {
      const int counts[] = {config_.players, config_.enemies, config_.models};
      for (int i = 0; i < std::min(32, counts[kind]); ++i) {
        auto o = object(i, kind);
        u.drops.push_back({o.status.id, false});
        u.objects.push_back(std::move(o));
      }
    }
    removed_ += u.drops.size();
    spawned_ += u.objects.size();
    replacements_ += static_cast<int>(u.objects.size());
    net::Update drops;
    drops.drops = std::move(u.drops);
    apply(drops);
    apply(u);
  }
  if (config_.moving && tick_ % 5 == 0) {
    net::Tick t;
    for (int kind = 0; kind < 2; ++kind) {
      const int count = kind ? config_.enemies : config_.players;
      for (int i = 0; i < count; ++i) {
        auto o = object(i, kind);
        const auto at = placement(i, kind);
        o.status.position = {static_cast<float>(at.x + .35 * std::sin(now * .001 + i)),
                             static_cast<float>(at.y + .35 * std::cos(now * .001 + i))};
        if (config_.workload != "VARIETY" && config_.workload != "MIXED") {
          o.status.stats.clear();
        }
        t.statuses.push_back(std::move(o.status));
      }
    }
    apply(t);
  }
  world_.update(config_.step_ms);
  if (config_.rotate_models) {
    for (auto& e : world_.entities()) {
      if (e.id >= 20100 && e.id < 26100) {
        e.appearance.model_rotation = std::fmod(now * .04 + e.id, 360.0);
      }
    }
  }
  // Replenish actual Combat projectiles, via the same incoming shoot event as Session.
  // Half originate far enough from enemies to survive, half cross dense target cells.
  if (config_.projectiles && tick_ % 25 == 0) {
    if (replay_) {
      replay_->shoot(0, now);
    } else {
      packets(combat_.shoot(0, now));
    }
  }
  if (config_.enemies && config_.projectiles && tick_ % 5 == 0) {
    const auto& enemy = require(world_.catalog(), config_.enemy);
    const auto center = world_.spawn_position();
    net::incoming::EnemyShoot s;
    s.owner_id = 10100;
    s.bullet_id = next_bullet_++;
    s.bullet_type = static_cast<std::uint8_t>(enemy.projectiles.front().id);
    s.position = {static_cast<float>(center.x - .25), static_cast<float>(center.y)};
    s.damage = 30;
    s.num_shots = 1;
    receive({flash::PacketId::ENEMYSHOOT, s}, now);
    ++spawned_;
  }
  if (config_.workload == "MEDUSA") {
    medusa(now);
  }
  const auto before = flights().size();
  const auto need = static_cast<std::size_t>(config_.projectiles) > before
                        ? static_cast<std::size_t>(config_.projectiles) - before
                        : 0;
  for (std::size_t i = 0; i < need; i += 128) {
    const int count = static_cast<int>(std::min<std::size_t>(128, need - i));
    const auto center = world_.spawn_position();
    net::incoming::ServerPlayerShoot s;
    s.owner_id = 1;
    s.bullet_id = next_bullet_;
    next_bullet_ += count;
    s.container_type = std::bit_cast<std::int16_t>(
        require(world_.catalog(),
                ((next_bullet_ / 128) % 2) ? config_.multi_weapon : config_.weapon)
            .type);
    s.position = {static_cast<float>(center.x - 2),
                  static_cast<float>(center.y + ((i / 128) % 2 ? 1.2 : 0))};
    s.angle = static_cast<float>((next_bullet_ % 8) * .3);
    s.angle_increment = .001F;
    s.damages.assign(static_cast<std::size_t>(count), 30);
    receive({flash::PacketId::SERVERPLAYERSHOOT, std::move(s)}, now);
    spawned_ += static_cast<std::size_t>(count);
  }
  simulated_ += world_.entities().size();
  // Driver bookkeeping (a full pass over every resident entity, ~2 ms with a Realm map's 246,000
  // statics): zoned so it can be told apart from client work.
  {
    REALM_ZONE("fixture.tally");
    for (const auto& e : world_.entities()) {
      const auto* d = world_.catalog().object(e.type);
      ++simulated_by_kind_[static_cast<std::size_t>(e.projectile             ? 3
                                                    : e.player               ? 0
                                                    : d && !d->model.empty() ? 2
                                                    : d && d->enemy          ? 1
                                                                             : 4)];
    }
  }
  particle_steps_ += world_.particles().particles().size();
  const auto active = flights().size();
  if (replay_) {
    replay_->poll(now);
    replay_->finish_frame(now);
  } else {
    packets(combat_.update(now));
  }
  removed_ += active - flights().size();
  world_.expire_damage(now);
  world_.particles().update(world_, now);
  ++tick_;
}

// Server Medusa (Mountain.cs): Shoot(count 5, shootAngle 10, cooldown 1000) at the player and
// Grenade(radius 4, damage 150, cooldown 3000), which sends a Throw ShowEffect (1500 ms travel)
// followed by the AOE. Phases are staggered per enemy.
void StressTimeline::medusa(std::int32_t now) {
  const auto& def = require(world_.catalog(), config_.enemy);
  const auto* player = world_.entity(1);
  if (!player) {
    return;
  }
  const auto target = player->motion.position;
  constexpr double spread = 10 * std::numbers::pi / 180;
  for (int i = 0; i < config_.enemies; ++i) {
    const auto* e = world_.entity(10100 + i);
    if (!e) {
      continue;
    }
    if ((tick_ + i) % 50 == 0) {
      net::incoming::EnemyShoot s;
      s.owner_id = e->id;
      s.bullet_id = next_bullet_;
      next_bullet_ += 5;
      s.bullet_type = static_cast<std::uint8_t>(def.projectiles.front().id);
      s.position = {static_cast<float>(e->motion.position.x),
                    static_cast<float>(e->motion.position.y)};
      s.angle = static_cast<float>(
          std::atan2(target.y - e->motion.position.y, target.x - e->motion.position.x) -
          2 * spread);
      s.damage = 30;
      s.num_shots = 5;
      s.angle_increment = static_cast<float>(spread);
      receive({flash::PacketId::ENEMYSHOOT, s}, now);
      spawned_ += 5;
    }
    if ((tick_ + i * 7) % 150 == 0) {
      net::incoming::ShowEffect t;
      t.type = 4;
      t.target_id = e->id;
      t.color = 0xffff0000U;
      t.position = {static_cast<float>(target.x), static_cast<float>(target.y)};
      t.position2 = {1500, 0};
      t.has_position2 = true;
      world_.particles().show(world_, t, now);
      grenades_.push_back({tick_ + 75, target});
    }
  }
  while (!grenades_.empty() && grenades_.front().first <= tick_) {
    const auto at = grenades_.front().second;
    grenades_.pop_front();
    net::incoming::Aoe a;
    a.position = {static_cast<float>(at.x), static_cast<float>(at.y)};
    a.radius = 4;
    a.damage = 150;
    a.color = 0xffff0000U;
    receive({flash::PacketId::AOE, a}, now);
  }
}

Vec2 StressTimeline::camera_position() const {
  const auto p = world_.spawn_position();
  // "walk": 8 tiles per second back and forth across +-40 tiles, so entities stream through the
  // viewport as when a player runs.
  if (config_.camera == "walk") {
    const double phase = std::fmod(time() * .008, 160.0);
    return {p.x + (phase < 80 ? phase - 40 : 120 - phase), p.y};
  }
  return config_.camera == "pan"
             ? Vec2{p.x + 2 * std::sin(time() * .001), p.y + 2 * std::cos(time() * .001)}
             : p;
}

double StressTimeline::camera_angle() const {
  return config_.camera == "rotate" ? time() * .0002 : 0;
}

double StressTimeline::zoom_factor() const {
  return config_.camera == "zoom" ? 1 + .15 * std::sin(time() * .002) : 1;
}

void StressTimeline::sample_scene(const Camera& camera, int width, int height,
                                  std::span<const Quad> quads,
                                  const std::array<std::uint64_t, 5>& submitted) {
  ++frames_;
  visible_.fill(0);
  submitted_.fill(0);
  for (const auto& e : world_.entities()) {
    const auto p = camera.world_to_screen(e.motion.position, e.motion.z);
    if (p.x < 0 || p.x >= camera.playable_width() || p.y < 0 || p.y >= height) {
      continue;
    }
    const auto* d = world_.catalog().object(e.type);
    const int k = e.projectile             ? 3
                  : e.player               ? 0
                  : d && !d->model.empty() ? 2
                  : d && d->enemy          ? 1
                                           : 4;
    ++visible_[static_cast<std::size_t>(k)];
  }
  // Per-kind submitted counts come from Scene's actual per-entity draw decisions.
  submitted_ = submitted;
  // Order-sensitive digest of the painter stream (REALM_QUAD_CHECKSUM=1): proves a
  // visibility/ordering change submitted exactly the same quads, meshes and order as another
  // binary. REALM_QUAD_CHECKSUM=models-as-markers hashes every non-model quad but each model entry
  // only as a marker, so a model submission change (prepared mesh vs instance) can still prove
  // every other quad and every model's stream position equal.
  static const char* hash_mode = std::getenv("REALM_QUAD_CHECKSUM");
  static const bool hash_quads = hash_mode != nullptr,
                    model_markers =
                        hash_quads && std::string_view(hash_mode) == "models-as-markers";
  if (hash_quads) {
    for (const auto& q : quads) {
      if (model_markers && q.kind == 9) {
        hash(quad_hash_, 0x9e3779b9ULL);
        continue;
      }
      hash_floats(quad_hash_, q.origin);
      hash_floats(quad_hash_, q.axis_x);
      hash_floats(quad_hash_, q.axis_y);
      hash_floats(quad_hash_, q.uv);
      hash_floats(quad_hash_, q.multiply);
      hash_floats(quad_hash_, q.offset);
      hash_floats(quad_hash_, q.uv_offset);
      hash(quad_hash_, std::bit_cast<std::uint32_t>(q.page));
      hash(quad_hash_, std::bit_cast<std::uint32_t>(q.kind));
      if (!q.owner) {
        hash(quad_hash_,
             q.texture);  // a surface's GL name is a handle, not content (cache sizing changes
                          // which names are reused)
      }
      if (q.kind == 9 && q.owner) {
        const auto& mesh = *std::static_pointer_cast<const MeshDraw>(q.owner);
        for (const auto& v : mesh.vertices) {
          const std::array<float, 5> values{v.x, v.y, v.u, v.v, v.shade};
          hash_floats(quad_hash_, values);
        }
        for (const auto& g : mesh.groups) {
          hash(quad_hash_, g.solid);
          hash(quad_hash_, g.repeat);
          hash(quad_hash_, g.first);
          hash(quad_hash_, g.count);
        }
        hash_floats(quad_hash_, mesh.color);
        hash_floats(quad_hash_, mesh.bounds);
      }
      if (q.kind == 17 && q.owner) {
        // A wall instance also digests its corners, so the stream still covers the geometry the
        // mesh path submitted.
        const auto& store = *static_cast<const WallGeometryStore*>(q.owner.get());
        const auto first = static_cast<std::size_t>(q.multiply[0]),
                   count = static_cast<std::size_t>(q.multiply[1]);
        for (std::size_t i = first; i < first + count; ++i) {
          const auto& v = store.vertices().at(i);
          hash_floats(quad_hash_,
                      std::array<float, 8>{v.x, v.y, v.z, v.u, v.v, v.shade, v.flags, v.pad});
        }
      }
      hash(quad_hash_, 0xfeedULL);
    }
  }
  for (const auto& q : quads) {
    ++quad_count_;
    if (q.kind == 9 && q.owner) {
      triangle_count_ += std::static_pointer_cast<const MeshDraw>(q.owner)->vertices.size() / 3;
    } else if (q.kind == 9) {
      triangle_count_ += model_range(static_cast<std::uint32_t>(q.offset[2])).count /
                         3;  // submitted, before GPU back-face culling
    } else if (q.kind == 17) {
      triangle_count_ += static_cast<std::uint64_t>(q.multiply[1]) /
                         3;  // a wall instance's corners, as mesh triangles
    } else {
      triangle_count_ += 2;
      coverage_sum_ += std::min(
          static_cast<double>(width) * height,
          static_cast<double>(std::abs(q.axis_x[0] * q.axis_y[1] - q.axis_x[1] * q.axis_y[0])));
    }
  }
}

void StressTimeline::sample_resources(double rss, std::size_t ce, std::size_t cb, std::size_t ge,
                                      std::size_t gb) {
  if (rss > 0) {
    if (rss_first_ == 0) {
      rss_first_ = rss;
    }
    rss_peak_ = std::max(rss_peak_, rss);
    rss_last_ = rss;
  }
  cache_last_ = {ce, cb, ge, gb};
  for (std::size_t i = 0; i < 4; ++i) {
    cache_peak_[i] = std::max(cache_peak_[i], cache_last_[i]);
  }
}

void StressTimeline::write_summary(const std::filesystem::path& path) const {
  std::uint64_t state = 1469598103934665603ULL;
  std::array<std::size_t, 5> resident{};
  std::size_t named_players{};
  std::set<int> actor_sizes;
  for (const auto& e : world_.entities()) {
    const auto* d = world_.catalog().object(e.type);
    if (e.player && !e.profile.name.empty()) {
      ++named_players;
    }
    if (e.player || (d && d->enemy && !e.projectile)) {
      actor_sizes.insert(e.appearance.size);
    }
    ++resident[static_cast<std::size_t>(e.projectile             ? 3
                                        : e.player               ? 0
                                        : d && !d->model.empty() ? 2
                                        : d && d->enemy          ? 1
                                                                 : 4)];
    hash(state, static_cast<std::uint32_t>(e.id));
    hash(state, e.type);
    hash(state, std::bit_cast<std::uint64_t>(e.motion.position.x));
    hash(state, std::bit_cast<std::uint64_t>(e.motion.position.y));
    hash(state, static_cast<std::uint32_t>(e.combat.hp));
    hash(state, e.appearance.tex1);
    hash(state, e.appearance.tex2);
    hash(state, static_cast<std::uint32_t>(e.appearance.texture));
    hash(state, std::bit_cast<std::uint64_t>(e.appearance.model_rotation));
    std::vector<int> keys;
    for (const auto& [key, value] : e.profile.stats) {
      (void)value;
      keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    for (int key : keys) {
      hash(state, static_cast<std::uint64_t>(key));
      hash(state, static_cast<std::uint32_t>(e.profile.stats.at(static_cast<std::uint8_t>(key))));
    }
    for (unsigned char b : e.profile.name) {
      hash(state, b);
    }
    hash(state, std::bit_cast<std::uint64_t>(e.motion.velocity.x));
    hash(state, std::bit_cast<std::uint64_t>(e.motion.velocity.y));
  }
  for (const auto& p : flights()) {
    hash(state, static_cast<std::uint32_t>(p.bullet_id));
    std::vector<std::int32_t> hit(p.hit.begin(), p.hit.end());
    std::sort(hit.begin(), hit.end());
    for (auto id : hit) {
      hash(state, static_cast<std::uint32_t>(id));
    }
  }
  for (const auto& p : world_.particles().particles()) {
    hash(state, std::bit_cast<std::uint64_t>(p.position.x));
    hash(state, std::bit_cast<std::uint64_t>(p.position.y));
    hash(state, p.color);
    hash(state, static_cast<std::uint64_t>(p.born));
  }
  Json counts = Json::object();
  for (const auto& [id, count] : packet_counts_) {
    counts[std::to_string(id)] = count;
  }
  save(path, {{"version", 1},
              {"workload", config_.workload},
              {"ticks", tick_},
              {"simulation_ms", time()},
              {"seed", config_.seed},
              {"state_checksum", std::to_string(state)},
              {"packet_checksum", std::to_string(packet_hash_)},
              {"packets", counts},
              {"hits", hits_},
              {"quad_stream_checksum", std::to_string(quad_hash_)},
              {"resident", resident},
              {"named_players", named_players},
              {"actor_sizes", actor_sizes},
              {"simulated_entity_steps", simulated_},
              {"simulated_by_kind", simulated_by_kind_},
              {"particle_steps", particle_steps_},
              {"counts_order", {"players", "enemies", "models", "projectiles", "other"}},
              {"active_projectiles", flights().size()},
              {"target_queries", target_stats().queries},
              {"target_examined", target_stats().examined},
              {"target_builds", target_stats().builds},
              {"target_indexed", target_stats().indexed},
              {"particles", world_.particles().particles().size()},
              {"emitters", world_.particles().emitter_count()},
              {"rss_mb_first", rss_first_},
              {"rss_mb_peak", rss_peak_},
              {"rss_mb_last", rss_last_},
              {"cache_order", {"cpu_entries", "cpu_bytes", "gpu_entries", "gpu_bytes"}},
              {"cache_peak", cache_peak_},
              {"cache_last", cache_last_},
              {"optional_drops", world_.particles().dropped()},
              {"spawned", spawned_},
              {"removed", removed_},
              {"resets", resets_},
              {"replacements", replacements_},
              {"session_replay", static_cast<bool>(replay_)},
              {"replayed_packets", replayed_packets_},
              {"rendered_frames", frames_},
              {"visible_last", visible_},
              {"submitted_last", submitted_},
              {"submitted_quads_total", quad_count_},
              {"submitted_triangles_total", triangle_count_},
              {"quad_area_sum_pixels", coverage_sum_},
              {"coverage_note",
               "sum of quad parallelogram area, includes overlap; excludes mesh area and alpha "
               "coverage"}});
}

void StressTimeline::describe(const Catalog& cat, const std::filesystem::path& path) {
  Json j = {{"version", 1},
            {"seed", 77},
            {"step_ms", 20},
            {"workload", "POPULATION"},
            {"player", "Priest"},
            {"model_objects", Json::array()},
            {"skins", Json::array({0})},
            {"dyes", Json::array({0})}};
  std::set<std::string> families;
  std::string weapon, multi, enemy;
  for (const auto& d : cat.objects()) {
    if (has_model(d.model)) {
      j["model_objects"].push_back(d.id);
      families.insert(d.model);
    }

    if (d.xml.child("Activate").text() == "Dye" && d.xml.has("Tex1")) {
      j["dyes"].push_back(static_cast<std::uint32_t>(d.xml.number("Tex1")));
    }
    if (d.enemy && !d.occupy && !d.enemy_occupy && !d.stationary && d.model.empty() &&
        !d.projectiles.empty() && d.projectiles.front().id >= 0 && d.projectiles.front().id < 256 &&
        cat.object(d.projectiles.front().object_id) && enemy.empty()) {
      enemy = d.id;
    }
    if (!d.enemy && d.slot_type == 8 && d.rate_of_fire > 0 && !d.projectiles.empty() &&
        d.projectiles.front().id == 0 && cat.object(d.projectiles.front().object_id)) {
      if (d.projectiles.front().multi_hit && multi.empty()) {
        multi = d.id;
      } else if (weapon.empty()) {
        weapon = d.id;
      }
    }
  }
  for (const auto& skin : cat.skins()) {
    if (skin.class_type == require(cat, "Priest").type) {
      j["skins"].push_back(skin.id);
    }
  }
  if (enemy.empty() || weapon.empty() || multi.empty()) {
    throw std::runtime_error("Missing legal stress definitions");
  }
  j["unreferenced_model_families"] = Json::array();
  for (std::size_t i = 0; i < model_count(); ++i) {
    if (!families.contains(std::string(model_name(i)))) {
      j["unreferenced_model_families"].push_back(model_name(i));
    }
  }
  j["enemy"] = enemy;
  j["weapon"] = weapon;
  j["multi_weapon"] = multi;
  j["model_families"] = families;
  j["registered_families"] = model_count();
  save(path, j);
}
}
