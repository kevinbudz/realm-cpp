#include "realm/scene.hpp"
#include "realm/movement.hpp"
#include <algorithm>
#include <cmath>
namespace realm {
namespace {
AnimationFrame animation_frame(const Character& character, const Entity* e, double angle, std::int32_t time) {
        Action action = Action::stand; double phase{}, facing = e ? e->facing : 0;
        const double attack_period = e && e->player ? e->attack_period : 500;
        if (e && time < e->attack_start + attack_period) {
            action = Action::attack; phase = std::fmod(time - e->attack_start,attack_period) / attack_period; facing = e->attack_angle;
        } else if (e && (e->velocity.x || e->velocity.y)) {
            action = Action::walk; facing = std::atan2(e->velocity.y, e->velocity.x);
            int period{};
            if (e->player) period = static_cast<int>(3.5 / move_speed(*e));
            else { period = static_cast<int>(.5 / (std::hypot(e->velocity.x, e->velocity.y) * 4)); period += 400 - period % 400; }
            period = std::max(1,period);
            phase = static_cast<double>(time % period) / period;
        }
        return character.frame(facing, angle, action, phase);
}
SpriteRegion texture(const TextureDef& definition, std::uint32_t id, const Entity* e, double angle, std::int32_t time,
                     const Atlas& atlas, const AnimationLibrary& animations) {
    const auto& selected = definition.select(id);
    if (selected.animated >= 0) if (const auto* character = animations.character(selected.animated, selected.index))
        return atlas.additional(animation_frame(*character, e, angle, time).image);
    if (selected.sheet >= 0) return atlas.sprite(static_cast<std::size_t>(selected.sheet), selected.index);
    return {0, Atlas::page_size - 1, Atlas::page_size - 1, 1, 1};
}
Quad ground_quad(SpriteRegion sprite, Vec2 position, const Camera& camera, double z = 0) {
    const auto p = camera.world_to_screen(position, z), px = camera.world_to_screen({position.x + 1, position.y}, z), py = camera.world_to_screen({position.x, position.y + 1}, z);
    return sprite_quad(sprite, p, {px.x - p.x, px.y - p.y}, {py.x - p.x, py.y - p.y});
}
}
void Scene::build(const World& world, const Camera& camera, const Atlas& atlas, const AnimationLibrary& animations, const ProcessedTextures* processed, const Terrain* terrain,
                  Vec2 position, double angle, double zoom, int width, int height, std::int32_t time, std::vector<Quad>& out) {
    const int distance = static_cast<int>(std::ceil(camera.max_distance())) + 3;
    const int cx = static_cast<int>(std::floor(position.x)), cy = static_cast<int>(std::floor(position.y));
    const double extent = camera.pixels_per_unit() * 1.5;
    for (int y = std::max(0, cy - distance); y < std::min(world.height(), cy + distance + 1); ++y)
        for (int x = std::max(0, cx - distance); x < std::min(world.width(), cx + distance + 1); ++x) {
            const auto* tile = world.tile(x, y); if (!tile) continue;
            const auto* ground = world.catalog().ground(tile->type); if (!ground || tile->type == 255) continue;
            const Vec2 world_pos{static_cast<double>(x), static_cast<double>(y)};
            const auto p = camera.world_to_screen(world_pos);
            if (p.x < -extent || p.x > width + extent || p.y < -extent || p.y > height + extent) continue;
            const auto visual = terrain ? terrain->tile(x,y) : TileVisual{};
            const auto sprite = terrain ? atlas.additional(visual.image) : texture(ground->texture, tile_hash(x,y), nullptr, angle, time, atlas, animations);
            auto q = ground_quad(sprite, world_pos, camera);
            auto offset = terrain ? visual.offset : Vec2{ground->offset_x,ground->offset_y};
            if (ground->animate) {
                const double t = time / 1000.0;
                offset.x = ground->offset_x + (ground->animate == 1 ? std::sin(ground->animate_x*t) : ground->animate_x*t);
                offset.y = ground->offset_x + (ground->animate == 1 ? std::sin(ground->animate_y*t) : ground->animate_y*t);
            }
            if (ground->animate || offset.x || offset.y) { q.kind = 3; q.uv_offset = {static_cast<float>(offset.x),static_cast<float>(offset.y)}; }
            out.push_back(q);
            if (terrain && visual.overlay >= 0)
                out.push_back(ground_quad(atlas.additional(static_cast<std::size_t>(visual.overlay)),world_pos,camera));
        }
    visible_.clear();
    for (const auto& e : world.entities()) {
        const auto p = camera.world_to_screen(e.position, e.z);
        if (p.x > -400 * zoom && p.x < width + 400 * zoom && p.y > -100 * zoom && p.y < height + 400 * zoom) visible_.push_back(&e);
    }
    std::sort(visible_.begin(), visible_.end(), [&](const auto* a, const auto* b) {
        const int ay = static_cast<int>(camera.world_to_screen(a->position).y / zoom + camera.clip_rect().y);
        const int by = static_cast<int>(camera.world_to_screen(b->position).y / zoom + camera.clip_rect().y);
        return ay == by ? a->id < b->id : ay < by;
    });
    auto draw = [&](const Entity& e, const ObjectDef& d) {
        const TextureDef* t = &d.texture;
        if (e.alt_texture) if (const auto it = t->alternate.find(e.alt_texture); it != t->alternate.end()) t = &it->second;
        auto sprite = texture(*t, static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
        if (d.draw_on_ground) { out.push_back(ground_quad(sprite, {std::floor(e.position.x), std::floor(e.position.y)}, camera, e.z)); return; }
        if (d.class_name == "Wall") {
            const double x = std::floor(e.position.x), y = std::floor(e.position.y);
            constexpr int nx[4] = {0,1,0,-1}, ny[4] = {-1,0,1,0};
            const Vec2 corners[4] = {{x,y},{x+1,y},{x+1,y+1},{x,y+1}};
            for (int side = 0; side < 4; ++side) {
                const auto* neighbor = world.tile(static_cast<int>(x) + nx[side], static_cast<int>(y) + ny[side]);
                const auto* other = neighbor ? world.entity(neighbor->object_id) : nullptr;
                const auto* other_def = other ? world.catalog().object(other->type) : nullptr;
                if (other_def && other_def->class_name == "Wall") continue;
                const auto a = camera.world_to_screen(corners[side], 1), b = camera.world_to_screen(corners[(side + 1) % 4], 1);
                const auto bottom = camera.world_to_screen(corners[side], 0);
                const Vec2 ax{b.x-a.x,b.y-a.y}, ay{bottom.x-a.x,bottom.y-a.y};
                if (ax.x * ay.y - ax.y * ay.x > 0) continue;
                auto q = sprite_quad(sprite, a, ax, ay);
                const double light = .75 + .25 * std::max(0.0, (nx[side] + 3.0 * ny[side]) / std::sqrt(14.0));
                q.multiply[0] = q.multiply[1] = q.multiply[2] = static_cast<float>(light);
                if (!neighbor || neighbor->type == 255) { q.kind = 2; q.multiply = {0,0,0,1}; }
                out.push_back(q);
            }
            const auto top = texture(d.top, static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
            auto q = ground_quad(top, {x,y}, camera, 1);
            q.multiply[0] = q.multiply[1] = q.multiply[2] = static_cast<float>(.75 + .5 / std::sqrt(14.0));
            out.push_back(q); return;
        }
        const bool baked = processed && d.model.empty();
        if (baked) {
            const auto& chosen = t->select(static_cast<std::uint32_t>(e.id));
            if (const auto* c = animations.character(chosen.animated, chosen.index))
                sprite = atlas.additional(processed->find(animated_texture_key(animation_frame(*c, &e, angle, time).image, e.size, e.projectile)));
            else if (chosen.sheet >= 0) sprite = atlas.additional(processed->find(static_texture_key(chosen.sheet, chosen.index, e.size, e.projectile)));
        }
        if (e.projectile) {
            const double rotation = d.rotation == 0 ? 0 : time / d.rotation;
            out.push_back(projectile_quad(sprite,camera.world_to_screen(e.position,e.z),
                                           e.facing-angle+d.angle_correction+rotation,e.size,zoom,baked));
            return;
        }
        const double scale = baked ? zoom : 5 * e.size / 100.0 * zoom;
        const double w = sprite.width * scale, h = sprite.height * scale;
        const auto p = camera.world_to_screen(e.position, e.z);
        int sink = e.sink_level;
        const int tile_x = static_cast<int>(std::floor(e.position.x)), tile_y = static_cast<int>(std::floor(e.position.y));
        if (terrain && world.tile(tile_x,tile_y)) sink += terrain->tile(tile_x,tile_y).sink;
        const auto* square = world.tile(tile_x,tile_y); const auto* protector = square ? world.entity(square->object_id) : nullptr;
        const auto* protector_def = protector ? world.catalog().object(protector->type) : nullptr;
        if (d.flying || (protector_def && protector_def->protect_sink)) sink = 0;
        const double clipped = std::clamp(sink * zoom, 0.0, h);
        if (baked) sprite.height -= std::min(sink,sprite.height);
        if (sprite.height > 0) out.push_back(sprite_quad(sprite, {p.x - w / 2, p.y - h + clipped}, {w, 0}, {0, h - clipped}));
        if ((d.player || d.enemy) && !d.no_minimap && !(e.condition & (1U<<20U)) && (d.player || !(e.condition & (1U<<21U)))) {
            const auto ground = camera.world_to_screen(e.position);
            const double y = ground.y + (5 + (d.player && e.id != world.player_id() ? 16 : 0)) * zoom;
            auto solid = [&](Vec2 origin, Vec2 size, std::uint32_t color) {
                Quad q; q.kind=2; q.origin={static_cast<float>(origin.x),static_cast<float>(origin.y)};
                q.axis_x={static_cast<float>(size.x),0};q.axis_y={0,static_cast<float>(size.y)};
                q.multiply={static_cast<float>((color>>16U)&255U)/255,static_cast<float>((color>>8U)&255U)/255,static_cast<float>(color&255U)/255,1};out.push_back(q);
            };
            solid({ground.x-21.2*zoom,y-1.2*zoom},{42.4*zoom,7.4*zoom},0x111111);
            const double hp_fraction = static_cast<double>(e.hp) / std::max(1,d.player ? e.max_hp : std::max(e.hp,e.max_hp));
            if (e.hp > 0) solid({ground.x-20*zoom,y},{40*hp_fraction*zoom,5*zoom},hp_fraction<.2 ? 14684176U : hp_fraction<.5 ? 16744464U : 0x10ff00U);
        }
    };
    for (const auto* e : visible_) if (const auto* d = world.catalog().object(e->type); d && d->draw_under) draw(*e, *d);
    for (const auto* e : visible_) {
        const auto* d = world.catalog().object(e->type); if (!d || d->draw_under || d->class_name == "Wall" || !d->shadow_size) continue;
        const auto sprite = texture(d->texture, static_cast<std::uint32_t>(e->id), e, angle, time, atlas, animations);
        const double scale = e->projectile ? d->shadow_size / 400.0 * zoom : e->size / 100.0 * d->shadow_size / 100.0 * sprite.height / 8.0 * zoom;
        const auto p = camera.world_to_screen(e->position);
        const auto color = d->shadow_color;
        out.push_back(radial_shadow({p.x - 30 * scale, p.y - 15 * scale}, {60 * scale, 0}, {0, 30 * scale},
            {static_cast<float>((color >> 16U) & 255U) / 255, static_cast<float>((color >> 8U) & 255U) / 255, static_cast<float>(color & 255U) / 255}, .5F));
    }
    for (const auto* e : visible_) if (const auto* d = world.catalog().object(e->type); d && !d->draw_under) draw(*e, *d);
    for (int y = std::max(0, cy - distance); y < std::min(world.height(), cy + distance + 1); ++y)
        for (int x = std::max(0, cx - distance); x < std::min(world.width(), cx + distance + 1); ++x) {
            const auto* ground = world.catalog().ground(world.tile(x, y)->type); if (!ground) continue;
            if (ground->top.sheet >= 0 || ground->top.animated >= 0)
                out.push_back(ground_quad(texture(ground->top, static_cast<std::uint32_t>(x + y * world.width()), nullptr, angle, time, atlas, animations), {static_cast<double>(x), static_cast<double>(y)}, camera, 1));
        }
}
}
