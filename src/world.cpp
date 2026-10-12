#include "realm/telemetry.hpp"
#include "realm/world.hpp"
#include "realm/conditions.hpp"
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
    f.seekg(0, std::ios::end); const auto size = f.tellg();
    if (size < 0 || size > 128 * 1024 * 1024) throw std::runtime_error("Map exceeds file size limit");
    f.seekg(0); net::Bytes bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !f.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("Truncated map file");
    return bytes;
}
net::Bytes base64(std::string_view source) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    net::Bytes result; result.reserve(source.size() / 4 * 3);
    std::uint32_t accumulator{}; int bits{}; bool padded{};
    for (const auto c : source) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') { padded = true; continue; }
        const auto value = alphabet.find(c);
        if (padded || value == std::string_view::npos) throw std::runtime_error("Invalid map base64");
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value); bits += 6;
        if (bits >= 8) { bits -= 8; result.push_back(static_cast<std::uint8_t>(accumulator >> static_cast<unsigned>(bits))); }
    }
    return result;
}
net::Bytes inflate_bounded(std::span<const std::uint8_t> input, std::size_t expected = 0) {
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) throw std::runtime_error("Cannot initialize map decompression");
    struct Guard { z_stream& s; ~Guard() { inflateEnd(&s); } } guard{stream};
    if (input.size() > std::numeric_limits<uInt>::max()) throw std::runtime_error("Compressed map too large");
    stream.next_in = const_cast<Bytef*>(input.data()); stream.avail_in = static_cast<uInt>(input.size());
    net::Bytes result; result.reserve(expected ? expected : 65536);
    std::array<Bytef, 65536> block{};
    int code{};
    do {
        stream.next_out = block.data(); stream.avail_out = static_cast<uInt>(block.size());
        code = inflate(&stream, Z_NO_FLUSH);
        if (code != Z_OK && code != Z_STREAM_END) throw std::runtime_error("Invalid compressed map stream");
        const auto produced = block.size() - stream.avail_out;
        if (result.size() + produced > (expected ? expected : 64 * 1024 * 1024)) throw std::runtime_error("Inflated map exceeds length limit");
        result.insert(result.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(produced));
    } while (code != Z_STREAM_END);
    if (stream.avail_in || (expected && result.size() != expected)) throw std::runtime_error("Map compressed data length mismatch");
    return result;
}
class LittleReader {
public:
    explicit LittleReader(std::span<const std::uint8_t> bytes) : reader_(bytes) {}
    std::uint8_t u8() { return reader_.u8(); }
    std::uint16_t u16() { const auto lo = u8(); return static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(u8()) << 8U)); }
    std::int16_t i16() { return std::bit_cast<std::int16_t>(u16()); }
    std::int32_t i32() { const auto lo = u16(); return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(u16()) << 16U)); }
    std::string string() {
        std::uint32_t length{};
        for (unsigned shift = 0; shift < 35; shift += 7) {
            const auto byte = u8();
            if (shift == 28 && byte > 15) throw std::runtime_error("Wmap string length overflow");
            length |= static_cast<std::uint32_t>(byte & 127U) << shift;
            if (!(byte & 128U)) { const auto data = reader_.take(length); return {data.begin(), data.end()}; }
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
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > 16 * 1024 * 1024)
        throw std::invalid_argument("Invalid map dimensions");
    width_ = width; height_ = height; name_ = std::move(name);
    tiles_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), {});
    ++minimap_revision_;minimap_rows_.assign(static_cast<std::size_t>(height),minimap_revision_);
    // A new map starts from a small footprint: capacity grown by the previous map's crowd (dense list, id table) is released.
    particles_.clear();entities_.clear(); if (entities_.capacity() > 2048) std::vector<Entity>().swap(entities_); entity_lookup_.clear(); interactive_.clear();
    sets_.clear(); sets_.release();
    static_grid_.reset(width, height); unresolved_.clear(); damage_cues_.clear(); status_events_.clear(); status_head_=0; player_id_ = -1;
    spawn_ = {width / 2.0, height / 2.0};
}
TileState* World::tile(int x, int y) {
    return x < 0 || y < 0 || x >= width_ || y >= height_ ? nullptr : &tiles_[static_cast<std::size_t>(x + y * width_)];
}
const TileState* World::tile(int x, int y) const { return const_cast<World*>(this)->tile(x, y); }
Entity* World::entity(std::int32_t id) {
    const auto slot = entity_lookup_.find(id); return slot == EntityIndex::none ? nullptr : &entities_[slot];
}
const Entity* World::entity(std::int32_t id) const { return const_cast<World*>(this)->entity(id); }
const Entity* World::interactive_target() const {
    REALM_ZONE("world.interactive_target");
    const auto* player=entity(player_id_);if(!player)return nullptr;
    // Same result as scanning the dense list in order with a strict `<`: the smallest distance, and among exact ties the
    // entity earliest in the list. Only interactive entities are candidates, so the cost no longer grows with projectiles,
    // players or scenery.
    const Entity* closest{};double best=1;std::size_t closest_index{};
    for(const auto id:interactive_) {
        const auto found=entity_lookup_.find(id);if(found==EntityIndex::none)continue;
        const auto& e=entities_[found];
        const double distance=std::hypot(e.position.x-player->position.x,e.position.y-player->position.y);
        if(distance<best||(distance==best&&closest&&found<closest_index)) { best=distance;closest=&e;closest_index=found; }
    }
    return closest;
}
void World::assign_tile_object(Entity& e) {
    const auto* d = catalog_.object(e.type);
    if (d && d->stationary) if (auto* t = tile(static_cast<int>(std::floor(e.position.x)), static_cast<int>(std::floor(e.position.y)))) t->object_id = e.id;
}
void World::remember_ground(int x,int y,std::uint16_t type) {
    if(auto* cell=tile(x,y)) {
        const auto code=0x20000U|type;
        if(cell->minimap_code!=code) { cell->minimap_code=code;minimap_rows_[static_cast<std::size_t>(y)]=++minimap_revision_; }
    }
}
void World::remember_object(const Entity& e) {
    const auto* def=catalog_.object(e.type);
    if(!def||!def->stationary||!def->occupy||def->no_minimap)return;
    const int x=static_cast<int>(e.position.x),y=static_cast<int>(e.position.y);
    if(auto* cell=tile(x,y)) {
        minimap_object_ids_.try_emplace(e.type,static_cast<std::uint32_t>(e.id));
        const auto code=0x30000U|e.type;
        if(cell->minimap_code!=code) { cell->minimap_code=code;minimap_rows_[static_cast<std::size_t>(y)]=++minimap_revision_; }
    }
}
Entity& World::add(std::int32_t id, std::uint16_t type, Vec2 position, int size, bool projectile) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || std::abs(position.x) > 1048576 || std::abs(position.y) > 1048576)
        throw net::ProtocolError("Invalid entity position");
    if (entity(id)) remove(id);
    Entity e; e.id = id; e.type = type; e.position = e.target = position; e.projectile = projectile;
    if (const auto* d = catalog_.object(type)) {
        e.player = d->player; e.size = size < 0 ? d->min_size : size; e.z = d->z; e.hp = e.max_hp = d->max_hp; e.defense = d->defense;
        e.tex1 = d->tex1; e.tex2 = d->tex2;
    }
    if(const auto* d=catalog_.object(type))particles_.attach(e,d->texture);
    if(const auto* d=catalog_.object(type);d&&d->interactive)interactive_.push_back(id);
    if (entities_.size() >= EntityIndex::none - 1) throw std::length_error("Too many objects");
    const auto index = static_cast<std::uint32_t>(entities_.size());
    entity_lookup_.assign(id, index); entities_.push_back(std::move(e)); assign_tile_object(entities_.back()); remember_object(entities_.back());
    // A projectile visual only ever joins the dynamic set (Combat moves it; the other sets' consumers skip projectiles anyway,
    // and their membership is still decided by the type below for anything else).
    const auto mask = projectile ? type_sets(type) | (1U << set_dynamic) : type_sets(type);
    sets_.push(mask & 0x3fU); static_grid_.push_slot();
    if (!(mask & (1U << set_dynamic))) static_grid_.insert(index, position);
    return entities_.back();
}
std::uint32_t World::type_sets(std::uint16_t type) {
    if (type_sets_.empty()) type_sets_.assign(65536, 0);
    auto& cached = type_sets_[type];
    if (!(cached & 0x80U)) {
        const auto* d = catalog_.object(type); std::uint32_t mask = 0x80U;
        if (d && d->enemy) mask |= 1U << set_enemy;
        if (d && d->player) mask |= 1U << set_player;
        if (d && !d->no_minimap && (d->player || d->class_name == "Character" || d->class_name == "Portal" || d->class_name == "GuildHallPortal")) mask |= 1U << set_marker;
        if (d && d->class_name == "Merchant") mask |= 1U << set_merchant;
        if (!(d && d->stationary)) mask |= 1U << set_dynamic;
        cached = static_cast<std::uint8_t>(mask);
    }
    return cached;
}
void World::set_player_id(std::int32_t id) {
    // movement.cpp drives the focus player's velocity outside World; once it is no longer the focus, update() must see it.
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
    const auto index = entity_lookup_.find(id); if (index == EntityIndex::none) return;
    particles_.remove_target(id); const auto& e = entities_[index];
    const auto* definition = catalog_.object(e.type); const bool was_interactive = definition && definition->interactive;
    if (auto* t = tile(static_cast<int>(std::floor(e.position.x)), static_cast<int>(std::floor(e.position.y)));
        t && t->object_id == id) t->object_id = -1;
    if (static_grid_.contains(index)) static_grid_.erase(index);
    if (index != entities_.size() - 1) { entities_[index] = std::move(entities_.back()); entity_lookup_.assign(entities_[index].id, static_cast<std::uint32_t>(index)); }
    entities_.pop_back(); entity_lookup_.erase(id);
    sets_.swap_remove(index);
    static_grid_.swap_remove(index);
    if(was_interactive)if(const auto slot=std::find(interactive_.begin(),interactive_.end(),id);slot!=interactive_.end()) { *slot=interactive_.back();interactive_.pop_back(); }
}
void World::goto_entity(std::int32_t id, Vec2 position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || std::abs(position.x) > 1048576 || std::abs(position.y) > 1048576)
        throw net::ProtocolError("Invalid teleport position");
    if (const auto index = entity_lookup_.find(id); index != EntityIndex::none) {
        auto* e = &entities_[index];
        if (auto* t = tile(static_cast<int>(std::floor(e->position.x)), static_cast<int>(std::floor(e->position.y)));
            t && t->object_id == id) t->object_id = -1;
        e->position = e->target = position; e->velocity = {}; assign_tile_object(*e);
        sets_.reset(set_moving, index); static_grid_.move(index, position);
    }
}
void World::report_damage(std::int32_t id,int amount,std::int32_t time,std::span<const std::uint8_t> effects,bool piercing,double hit_angle,double hit_speed) {
    const auto* target=entity(id); if (!target) return;
    // GameServerConnection.onDamage: damage to other objects is dropped when Ally Damage is off.
    if (!preferences_.ally_damage && id!=player_id_) return;
    // GameObject.damage displays feedback. HP and conditions change only when
    // the authoritative server stats arrive, including locally detected hits.
    particles_.hit(*this,*target,amount,time,hit_angle,hit_speed,hit_speed>=0);
    if (amount<=0 && effects.empty()) return;
    // GameObject.damage: condition names stagger by 500 ms, then the damage amount.
    const bool pierced=piercing || (target->condition&condition::bit(condition::armor_broken));
    int offset=0;
    for (const auto effect:effects) {
        unsigned shown=effect;
        switch (effect) {
        case condition::quiet: case condition::weak: case condition::slowed: case condition::sick: case condition::dazed:
        case condition::blind: case condition::hallucinating: case condition::drunk: case condition::confused:
        case condition::stun_immune: case condition::invisible: case condition::paralyzed: case condition::speedy:
        case condition::bleeding: case condition::stasis: case condition::stasis_immune: case condition::armor_broken: break;
        case condition::stunned:
            if (target->condition&condition::bit(condition::stun_immune)) { notify(id,"Immune",0xff0000U,3000);shown=condition::nothing; }
            break;
        default: shown=condition::nothing;
        }
        if (!shown) continue;
        notify(id,std::string(condition_effect(shown).name),0xff0000U,3000,offset);offset+=500;
    }
    if (amount>0) notify(id,"-"+std::to_string(amount),pierced ? 0x9000ffU : 0xff0000U,1000);
    if (damage_cues_.size()>=256) damage_cues_.pop_front();
    damage_cues_.push_back({id,time,amount,pierced,{effects.begin(),effects.end()}});
}
bool World::allows_notification(std::int32_t object_id) const {
    if (preferences_.ally_notifs || object_id==player_id_) return true;
    const auto found=entity_lookup_.find(object_id);
    return found==EntityIndex::none || !entities_[found].player;
}
void World::notify(std::int32_t object_id,std::string text,std::uint32_t color,int lifetime,int offset_time) {
    if (!entity(object_id)) return;
    if (status_events_.size()-status_head_>=64) ++status_head_;
    status_events_.push_back({object_id,std::move(text),color,lifetime,offset_time});
    if (status_head_>=64) { status_events_.erase(status_events_.begin(),status_events_.begin()+static_cast<std::ptrdiff_t>(status_head_));status_head_=0; }
}
std::vector<StatusEvent> World::take_status_events() {
    status_events_.erase(status_events_.begin(),status_events_.begin()+static_cast<std::ptrdiff_t>(status_head_));status_head_=0;
    return std::exchange(status_events_,{});
}
void World::expire_damage(std::int32_t time) {
    std::erase_if(damage_cues_,[&](const auto& cue){ return static_cast<std::int64_t>(time)-cue.time_ms>3500 || !entity(cue.object_id); });
}
void World::status(Entity& e, const net::Status& s, bool interpolate, double dt_ms) {
    const Vec2 position{s.position.x, s.position.y};
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || std::abs(position.x) > 1048576 || std::abs(position.y) > 1048576)
        throw net::ProtocolError("Invalid status position");
    if (interpolate && e.id != player_id_ && (e.target.x != position.x || e.target.y != position.y)) {
        e.target = position; e.velocity = {(position.x - e.position.x) / dt_ms, (position.y - e.position.y) / dt_ms};
        if (!(e.velocity.x == 0 && e.velocity.y == 0)) sets_.set(set_moving, static_cast<std::size_t>(&e - entities_.data()));
    } else if (!interpolate) { goto_entity(e.id, position); }
    // GameServerConnection.onNewTick: level, exp and fame as they were before this update.
    const auto previous=[&](flash::StatId id,int fallback) { const auto it=e.stats.find(static_cast<std::uint8_t>(id));return it==e.stats.end() ? fallback : it->second; };
    const int old_level=previous(flash::StatId::LEVEL_STAT,-1),old_exp=previous(flash::StatId::EXP_STAT,0),old_fame=previous(flash::StatId::CHAR_FAME_STAT,0);
    if (e.stats.empty()) e.stats.reserve(s.stats.size());   // the first status carries the full set; later ones rewrite it
    for (const auto& stat : s.stats) {
        if (std::holds_alternative<std::string>(stat.value)) {
            if (stat.type == static_cast<std::uint8_t>(flash::StatId::NAME_STAT)) e.name = std::get<std::string>(stat.value);
            if (stat.type == static_cast<std::uint8_t>(flash::StatId::GUILD_NAME_STAT)) e.guild = std::get<std::string>(stat.value);
            continue;
        }
        const auto value = std::get<std::int32_t>(stat.value);
        if(stat.type==static_cast<std::uint8_t>(flash::StatId::LEVEL_STAT)) {
            const auto old=e.stats.find(stat.type);
            if(e.player&&old!=e.stats.end()&&old->second>=0&&value>old->second&&(e.id==player_id_||preferences_.ally_notifs))particles_.level_up(e.id,static_cast<std::int32_t>(particles_.time()));
        }
        e.stats[stat.type] = value;
        switch (static_cast<flash::StatId>(stat.type)) {
        case flash::StatId::HP_STAT: e.hp = value; break;
        case flash::StatId::MAX_HP_STAT: e.max_hp = value; break;
        case flash::StatId::SIZE_STAT: e.size = value; break;
        case flash::StatId::SPEED_STAT: e.speed = value; break;
        case flash::StatId::ATTACK_STAT: e.attack = value; break;
        case flash::StatId::DEXTERITY_STAT: e.dexterity = value; break;
        case flash::StatId::DEFENSE_STAT: e.defense = value; break;
        case flash::StatId::CONDITION_STAT: e.condition = static_cast<std::uint32_t>(value);if(e.player)particles_.healing(e);break;
        case flash::StatId::TEX1_STAT: e.tex1 = static_cast<std::uint32_t>(value); break;
        case flash::StatId::TEX2_STAT: e.tex2 = static_cast<std::uint32_t>(value); break;
        case flash::StatId::ALT_TEXTURE_STAT:
            e.alt_texture = value;
            if(const auto* d=catalog_.object(e.type)) {
                const auto it=d->texture.alternate.find(value);particles_.attach(e,it==d->texture.alternate.end() ? d->texture : it->second);
            }break;
        case flash::StatId::TEXTURE_STAT: e.texture = value; break;
        default: break;
        }
    }
    // Other players' level-ups and experience are shown only with Ally Notifications on.
    if (e.player&&old_level!=-1&&(e.id==player_id_||preferences_.ally_notifs)) {
        const int level=previous(flash::StatId::LEVEL_STAT,-1),exp=previous(flash::StatId::EXP_STAT,0),fame=previous(flash::StatId::CHAR_FAME_STAT,0);
        if (level>old_level) notify(e.id,"Level Up!",0x00ff00U,2000);
        else if (exp>old_exp) {
            if (level!=20) notify(e.id,"+"+std::to_string(exp-old_exp)+" EXP",0x00ff00U,1000);
            if (fame>old_fame&&level==20) notify(e.id,"+"+std::to_string(fame-old_fame)+" Fame",0xe25f00U,1000);
        }
    }
}
void World::apply(const net::Update& u) {
    for (const auto& t : u.tiles) if (auto* out = tile(t.x, t.y)) { out->type = t.type;remember_ground(t.x,t.y,t.type); }
    for (const auto& o : u.objects) {
        auto& e = add(o.status.id, o.type, {o.status.position.x, o.status.position.y}); status(e, o.status, false, 100);
    }
    for (const auto& d : u.drops) remove(d.id);
}
void World::apply(const net::Tick& tick, double dt_ms) {
    if (!std::isfinite(dt_ms) || dt_ms <= 0) throw std::invalid_argument("Invalid tick interval");
    for (const auto& s : tick.statuses) if (auto* e = entity(s.id)) status(*e, s, true, dt_ms);
    if (auto* e = entity(player_id_)) { net::Status s; s.stats = tick.player_stats; status(*e, s, true, dt_ms); }
}
void World::update(double dt_ms) {
    REALM_ZONE("world.update");
    if (!std::isfinite(dt_ms) || dt_ms < 0) throw std::invalid_argument("Invalid frame interval");
    // Only entities World set moving, in dense order: the same visits, in the same order, as the full pass this replaces
    // (the order matters only when two moving stationary objects claim one tile in the same frame).
    sets_.each(set_moving, [&](std::size_t i) {
        auto& e = entities_[i];
        if (e.id == player_id_) return;
        if (e.velocity.x == 0 && e.velocity.y == 0) { sets_.reset(set_moving, i); return; }
        Vec2 next{e.position.x + e.velocity.x * dt_ms, e.position.y + e.velocity.y * dt_ms};
        if ((e.velocity.x > 0 && next.x > e.target.x) || (e.velocity.x < 0 && next.x < e.target.x)) { next.x = e.target.x; e.velocity.x = 0; }
        if ((e.velocity.y > 0 && next.y > e.target.y) || (e.velocity.y < 0 && next.y < e.target.y)) { next.y = e.target.y; e.velocity.y = 0; }
        if (auto* t = tile(static_cast<int>(std::floor(e.position.x)), static_cast<int>(std::floor(e.position.y)));
            t && t->object_id == e.id) t->object_id = -1;
        e.position = next; assign_tile_object(e); static_grid_.move(static_cast<std::uint32_t>(i), next);
        if (e.velocity.x == 0 && e.velocity.y == 0) sets_.reset(set_moving, i);
    });
}
bool World::walkable(double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= width_ || y >= height_) return false;
    const auto* t = tile(static_cast<int>(x), static_cast<int>(y)); const auto* g = t ? catalog_.ground(t->type) : nullptr;
    if (!g || g->no_walk) return false;
    const auto* e = entity(t->object_id); const auto* d = e ? catalog_.object(e->type) : nullptr;
    return !d || !d->occupy;
}
void World::load_jm(const std::filesystem::path& path) {
    const auto bytes = read(path); load_jm_text({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    if (name_.empty()) name_ = path.stem().string();
}
void World::load_jm_text(std::string_view source, int wrap_border) {
    const auto jm = nlohmann::json::parse(source);
    const int width = jm.at("width").get<int>(), height = jm.at("height").get<int>();
    if (wrap_border < 0 || width <= 0 || wrap_border > width / 2) throw std::invalid_argument("Invalid JM wrap border");
    initialize(width + 2 * wrap_border, height, jm.value("name", std::string{}));
    const auto dict = jm.at("dict"); if (!dict.is_array() || dict.empty() || dict.size() > 32768) throw std::runtime_error("Invalid JM dictionary");
    const auto data = inflate_bounded(base64(jm.at("data").get<std::string>()), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2); net::Reader reader(data);
    std::int32_t fake_id = 2130706432; bool spawned{};
    for (int y = 0; y < height_; ++y) for (int source_x = 0; source_x < width; ++source_x) {
        const auto index = reader.i16(); if (index < 0 || static_cast<std::size_t>(index) >= dict.size()) throw std::runtime_error("JM dictionary index out of range");
        const auto& entry = dict[static_cast<std::size_t>(index)];
        // MapBackground writes the map again at its own width, clipped to the border.
        for (int x = source_x; x < width_; x += width) {
            auto* t = tile(x, y);
            if (entry.contains("ground")) {
                const auto id = entry["ground"].get<std::string>(); const auto* g = catalog_.ground(id);
                if (g) { t->type = g->type;remember_ground(x,y,g->type); } else if (std::find(unresolved_.begin(), unresolved_.end(), id) == unresolved_.end()) unresolved_.push_back(id);
            }
            if (entry.contains("regions")) for (const auto& r : entry["regions"])
                if (!spawned && r.value("id", "") == "Spawn") { spawn_ = {x + .5, y + .5}; spawned = true; }
            if (entry.contains("objs")) for (const auto& obj : entry["objs"]) {
                const auto id = obj.at("id").get<std::string>(); const auto* d = catalog_.object(id);
                if (d) add(fake_id++, d->type, {x + .5, y + .5}, obj.value("size", -1));
                else if (std::find(unresolved_.begin(), unresolved_.end(), id) == unresolved_.end()) unresolved_.push_back(id);
            }
        }
    }
    reader.finish();
    if (!spawned) {
        for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x)
            if (walkable(x + .5, y + .5)) { spawn_ = {x + .5, y + .5}; return; }
    }
}
void World::load_wmap(const std::filesystem::path& path) {
    unresolved_.clear();
    const auto source = read(path);
    if (source.empty() || source[0] > 2) throw std::runtime_error("Unsupported WMAP version");
    const auto version = source[0]; const auto bytes = inflate_bounded(std::span(source).subspan(1)); LittleReader r(bytes);
    struct Entry { std::uint16_t ground{}, object{}; bool has_object{}; std::string config; std::uint8_t terrain{}, region{}, elevation{}; };
    const auto count = r.i16(); if (count <= 0) throw std::runtime_error("Invalid WMAP dictionary length");
    std::vector<Entry> entries; entries.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Entry e; e.ground = r.u16(); if (!catalog_.ground(e.ground)) e.ground = 255;
        const auto id = r.string(); if (const auto* d = catalog_.object(id)) { e.object = d->type; e.has_object = true; }
        else if (!id.empty() && std::find(unresolved_.begin(), unresolved_.end(), id) == unresolved_.end()) unresolved_.push_back(id);
        e.config = r.string(); e.terrain = r.u8(); e.region = r.u8(); if (version == 1) e.elevation = r.u8(); entries.push_back(std::move(e));
    }
    const auto unresolved = std::move(unresolved_); const int width = r.i32(), height = r.i32(); initialize(width, height, path.stem().string()); unresolved_ = unresolved;
    std::int32_t fake_id = 2130706432; bool spawned{};
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
        const auto index = r.i16(); if (index < 0 || index >= count) throw std::runtime_error("WMAP index out of range");
        const auto& e = entries[static_cast<std::size_t>(index)]; auto* t = tile(x, y);
        t->type = e.ground;remember_ground(x,y,e.ground); t->terrain = e.terrain; t->elevation = version == 2 ? r.u8() : e.elevation;
        if (e.has_object) add(fake_id++, e.object, {x + .5, y + .5});
        if (!spawned && e.region == 1) { spawn_ = {x + .5, y + .5}; spawned = true; }
    }
    r.finish();
}
}
