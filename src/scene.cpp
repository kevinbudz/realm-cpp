#include "realm/scene.hpp"
#include "realm/model.hpp"
#include "realm/movement.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
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
std::shared_ptr<SurfaceTexture> Scene::surface(std::shared_ptr<const Image> image) {
    for(auto it=appearances_.begin();it!=appearances_.end();++it)if(it->image.lock()==image) { const auto result=it->surface;appearances_.splice(appearances_.begin(),appearances_,it);return result; }
    while(!appearances_.empty()&&(appearances_.size()>=Appearances::maximum_entries||appearance_bytes_+image->pixels.size()>Appearances::maximum_bytes)) {
        appearance_bytes_-=appearances_.back().bytes;appearances_.pop_back();
    }
    auto result=std::make_shared<SurfaceTexture>();result->update(*image);appearance_bytes_+=image->pixels.size();appearances_.push_front({image,result,image->pixels.size()});return result;
}
std::shared_ptr<SurfaceTexture> Scene::status_surface(const std::shared_ptr<const Image>& image) {
    if(const auto it=status_surfaces_.find(image.get());it!=status_surfaces_.end()) {
        if(it->second.image.lock()==image)return it->second.surface;
        status_surfaces_.erase(it);
    }
    if(status_surfaces_.size()>=256) {
        std::erase_if(status_surfaces_,[](const auto& entry){ return entry.second.image.expired(); });
        if(status_surfaces_.size()>=256)status_surfaces_.clear();
    }
    auto result=std::make_shared<SurfaceTexture>();result->update(*image);
    status_surfaces_[image.get()]={image,result};return result;
}
void Scene::build(const World& world, const Camera& camera, const Atlas& atlas, const AnimationLibrary& animations, const ProcessedTextures* processed, const Terrain* terrain, const Appearances* appearances,
                  Vec2 position, double angle, double zoom, int width, int height, std::int32_t time, std::vector<Quad>& out) {
    if(appearance_world_!=&world||generation_!=world.generation()) { appearances_.clear();webs_.clear();status_surfaces_.clear();appearance_bytes_=0;if(appearances)appearances->clear();generation_=world.generation();appearance_world_=&world; }
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
    const auto flash_tint=[&](Quad& q,const Entity& e) {
        for(const auto& f:world.particles().flashes())if(f.target==e.id) {
            const auto age=static_cast<std::int64_t>(time)-f.born;
            if(age<0||age>static_cast<std::int64_t>(f.period)*f.repeats)break;
            const float v=static_cast<float>(std::sin(static_cast<double>(age%f.period)/f.period*std::numbers::pi)*.5);
            for(int channel=0;channel<3;++channel) { q.multiply[static_cast<std::size_t>(channel)]*=1-v;q.offset[static_cast<std::size_t>(channel)]=v*static_cast<float>((f.color>>(16-8*channel))&255U)/255; }break;
        }
    };
    // Sprite-level condition cues: Stasis greys the texture (PAUSED_FILTER, uv_offset.x flags the
    // shader), Invisible players draw at 40% alpha, and players under 20% health pulse toward red.
    const auto condition_tint=[&](Quad& q,const Entity& e) {
        if(e.condition&condition::bit(condition::stasis))q.uv_offset[0]=1;
        else if(e.player&&(e.condition&condition::bit(condition::invisible)))for(auto& channel:q.multiply)channel*=.4F;
        if(e.player&&e.hp<e.max_hp*.2) {
            const auto shift=static_cast<float>(low_health_pulse(time)*128/255);
            q.offset[0]+=shift;q.offset[1]-=shift;q.offset[2]-=shift;
        }
    };
    const auto* focus = world.entity(world.player_id());
    const bool hallucinating = focus && (focus->condition&condition::bit(condition::hallucinating));
    int hallucination_sheet = -1;
    for (std::size_t i = 0; i < flash::sheets.size(); ++i) if (std::string_view(flash::sheets[i].name) == "lofiChar8x8") hallucination_sheet = static_cast<int>(i);
    TextureDef hallucinated;
    auto draw = [&](const Entity& e, const ObjectDef& d) {
        auto dyes=world_dyes(world,e);
        const TextureDef* t = e.player ? &world.catalog().player_texture(e.type,e.texture) : &d.texture;
        if(d.class_name=="Merchant")if(const auto it=e.stats.find(static_cast<std::uint8_t>(flash::StatId::MERCHANDISE_TYPE_STAT));it!=e.stats.end())if(const auto* item=world.catalog().object(static_cast<std::uint16_t>(it->second))) {
            t=&item->texture;dyes={static_cast<std::uint32_t>(item->xml.number("Tex1")),static_cast<std::uint32_t>(item->xml.number("Tex2"))};
        }
        if (e.alt_texture) if (const auto it = t->alternate.find(e.alt_texture); it != t->alternate.end()) t = &it->second;
        // Player.getTexture: a Hexed player takes the look of a random Hexable object (stable per player), keeping its dyes.
        if (e.player && (e.condition&condition::bit(condition::hexed)) && !world.catalog().hex_transforms().empty()) {
            const auto& hexes=world.catalog().hex_transforms();
            if (const auto* hex=world.catalog().object(hexes[static_cast<std::uint32_t>(e.id)*2654435761U%hexes.size()]))t=&hex->texture;
        }
        auto sprite = texture(*t, static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
        int shown_size = e.size;
        // GameObject/Player.getTexture: while the focus player hallucinates, every ordinary object draws a random
        // lofiChar8x8 tile (stable per object); non-players are scaled by min(1.5, original width / 8).
        if (hallucinating && !e.projectile && !d.draw_on_ground && d.model.empty() && d.class_name != "Wall" && hallucination_sheet >= 0) {
            hallucinated = TextureDef{};
            hallucinated.sheet = hallucination_sheet;
            hallucinated.index = static_cast<int>(static_cast<std::uint32_t>(e.id) * 2654435761U % 239U);
            if (!e.player) shown_size = static_cast<int>(e.size * std::min(1.5, sprite.width / 8.0));
            t = &hallucinated;dyes = {};
            sprite = texture(*t, static_cast<std::uint32_t>(e.id), &e, angle, time, atlas, animations);
        }
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
        if (!d.model.empty() && has_model(d.model)) {
            // GameObject.draw3d: a registered Model replaces the 2D sprite. The sprite is only
            // its texture; Solid* groups and sprite-less objects use the XML Color.
            const auto& chosen = t->select(static_cast<std::uint32_t>(e.id));
            std::optional<SpriteRegion> model_sprite;
            if (chosen.sheet >= 0 || chosen.animated >= 0) model_sprite = sprite;
            double rotation = d.rotation;
            if (d.class_name == "SpiderWeb") {
                // SpiderWeb.findWall: west, north, east, then south neighbor; the web turns away from its wall.
                auto found = webs_.find(e.id);
                if (found == webs_.end()) {
                    constexpr int dx[4] = {-1,0,1,0}, dy[4] = {0,-1,0,1};
                    for (int side = 0; side < 4; ++side) {
                        const auto* tile = world.tile(static_cast<int>(e.position.x + dx[side]), static_cast<int>(e.position.y + dy[side]));
                        const auto* object = tile ? world.entity(tile->object_id) : nullptr;
                        const auto* object_def = object ? world.catalog().object(object->type) : nullptr;
                        if (object_def && object_def->class_name == "Wall") { found = webs_.emplace(e.id, side ? 90.0 * side : d.rotation).first; break; }
                    }
                }
                if (found == webs_.end()) return;
                rotation = found->second;
            }
            if (auto mesh = project_model(d.model, camera, e.position, rotation, d.color, model_sprite)) out.push_back(mesh_quad(std::move(mesh)));
            return;
        }
        // Projectile.draw: with Projectile Outline off the plain texture fills the projectile's square.
        bool baked = (processed || appearances) && d.model.empty() && !(e.projectile && !world.preferences().proj_outline);
        std::shared_ptr<SurfaceTexture> lazy;
        double density=1;
        if (baked) {
            const auto& chosen = t->select(static_cast<std::uint32_t>(e.id));
            const auto* c=animations.character(chosen.animated,chosen.index);
            const auto frame=c ? animation_frame(*c,&e,angle,time) : AnimationFrame{};
            const auto key=c ? animated_texture_key(frame.image,shown_size,e.projectile) : static_texture_key(chosen.sheet,chosen.index,shown_size,e.projectile);
            if(processed&&processed->contains(key)&&!(dyes[0]||dyes[1]))sprite=atlas.additional(processed->find(key));
            else if(appearances) {
                auto image=c ? appearances->animated(frame,shown_size,e.projectile,dyes[0],dyes[1]) : appearances->texture(chosen,static_cast<std::uint32_t>(e.id),shown_size,e.projectile,dyes[0],dyes[1]);
                density=image->pixel_scale;sprite={0,0,0,image->width,image->height};lazy=surface(std::move(image));
            } else baked=false;
        }
        if (e.projectile) {
            const double rotation = d.rotation == 0 ? 0 : time / d.rotation;
            auto q=projectile_quad(sprite,camera.world_to_screen(e.position,e.z),e.facing-angle+d.angle_correction+rotation,e.size,zoom,baked);
            if(lazy) {
                const auto base=lazy->quad({}, {}, {});q.texture=base.texture;q.owner=lazy;q.kind=6;
                q.uv[0]*=Atlas::page_size/static_cast<float>(sprite.width);q.uv[1]*=Atlas::page_size/static_cast<float>(sprite.height);
                q.uv[2]*=Atlas::page_size/static_cast<float>(sprite.width);q.uv[3]*=Atlas::page_size/static_cast<float>(sprite.height);
            }
            flash_tint(q,e);out.push_back(std::move(q));
            return;
        }
        const double scale = baked ? zoom / density : 5 * shown_size / 100.0 * zoom;
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
        if (sprite.height > 0) {
            auto q=lazy ? lazy->quad({p.x-w/2,p.y-h+clipped},{w,0},{0,h-clipped},0,0,sprite.width,sprite.height) : sprite_quad(sprite,{p.x-w/2,p.y-h+clipped},{w,0},{0,h-clipped});
            if(lazy)q.kind=6;
            q.owner=lazy;flash_tint(q,e);condition_tint(q,e);out.push_back(std::move(q));
        }
        if(status_art_&&e.condition) {
            // GameObject.drawConditionIcons: 16 px icons centered over the sprite's top edge.
            condition_icon_frames(e.condition,time,icon_frames_);
            const double top=p.y-h+clipped,left=p.x-8.0*static_cast<double>(icon_frames_.size())*zoom;
            for(std::size_t i=0;i<icon_frames_.size();++i) {
                const auto image=status_art_->icon(icon_frames_[i]);if(!image)continue;
                const auto surface=status_surface(image);
                auto q=surface->quad({left+16.0*static_cast<double>(i)*zoom,top-8*zoom},{16*zoom,0},{0,16*zoom});q.kind=6;q.owner=surface;out.push_back(std::move(q));
            }
        }
        if(status_art_&&!e.name.empty()&&(d.show_name||(d.player&&e.id!=world.player_id()))) {
            // GameObject.drawName: the name bitmap is centered on the object, top at its ground position.
            const auto label=status_art_->name(e.name);
            if(label.image) {
                const auto ground=camera.world_to_screen(e.position);
                const auto surface=status_surface(label.image);
                const double image_w=label.image->width/label.image->pixel_scale,image_h=label.image->height/label.image->pixel_scale;
                const double field_w=std::floor(label.width/2+1)*2;
                auto q=surface->quad({ground.x-field_w/2*zoom+label.offset.x*zoom,ground.y+label.offset.y*zoom},{image_w*zoom,0},{0,image_h*zoom});q.kind=6;q.owner=surface;out.push_back(std::move(q));
            }
        }
        if (world.preferences().hp_bars && (d.player || d.enemy) && !d.no_minimap && !(e.condition & (1U<<20U)) && (d.player || !(e.condition & (1U<<21U)))) {
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
        if (e.player && e.id==world.player_id()) {
            // Player.drawBreathBar: 40x4 under the health bar position, only while the server sends a breath stat.
            if(const int breath=player_breath(e);breath>=0) {
                const auto ground=camera.world_to_screen(e.position);
                const double y=ground.y+9*zoom;
                auto solid=[&](Vec2 origin,Vec2 size,std::uint32_t color) {
                    Quad q;q.kind=2;q.origin={static_cast<float>(origin.x),static_cast<float>(origin.y)};
                    q.axis_x={static_cast<float>(size.x),0};q.axis_y={0,static_cast<float>(size.y)};
                    q.multiply={static_cast<float>((color>>16U)&255U)/255,static_cast<float>((color>>8U)&255U)/255,static_cast<float>(color&255U)/255,1};out.push_back(q);
                };
                solid({ground.x-20*zoom,y},{40*zoom,4*zoom},breath_bar_back_color(breath,time));
                if(breath>0)solid({ground.x-20*zoom,y},{std::min(breath,100)/100.0*40*zoom,4*zoom},0x26caffU);
            }
        }
    };
    for (const auto* e : visible_) if (const auto* d = world.catalog().object(e->type); d && d->draw_under) draw(*e, *d);
    if (world.preferences().draw_shadows) for (const auto* e : visible_) {
        const auto* d = world.catalog().object(e->type); if (!d || d->draw_under || d->class_name == "Wall" || !d->shadow_size) continue;
        const auto sprite = texture(e->player ? world.catalog().player_texture(e->type,e->texture) : d->texture, static_cast<std::uint32_t>(e->id), e, angle, time, atlas, animations);
        const double scale = e->projectile ? d->shadow_size / 400.0 * zoom : e->size / 100.0 * d->shadow_size / 100.0 * sprite.height / 8.0 * zoom;
        const auto p = camera.world_to_screen(e->position);
        const auto color = d->shadow_color;
        out.push_back(radial_shadow({p.x - 30 * scale, p.y - 15 * scale}, {60 * scale, 0}, {0, 30 * scale},
            {static_cast<float>((color >> 16U) & 255U) / 255, static_cast<float>((color >> 8U) & 255U) / 255, static_cast<float>(color & 255U) / 255}, .5F));
    }
    ordered_.clear();
    for(const auto* e:visible_)if(const auto* d=world.catalog().object(e->type);d&&!d->draw_under)
        ordered_.push_back({e,nullptr,static_cast<int>(camera.world_to_screen(e->position).y/zoom+camera.clip_rect().y),static_cast<std::uint32_t>(e->id)});
    for(const auto& p:world.particles().particles()) {
        if(p.motion==ParticleMotion::swirl&&(p.z<0||p.z>1))continue;
        const auto at=camera.world_to_screen(p.position,p.z);
        if(at.x < -100*zoom||at.x>width+100*zoom||at.y < -100*zoom||at.y>height+100*zoom)continue;
        ordered_.push_back({nullptr,&p,static_cast<int>(camera.world_to_screen(p.position).y/zoom+camera.clip_rect().y),0x80000000ULL+p.order});
    }
    std::sort(ordered_.begin(),ordered_.end(),[](const auto& a,const auto& b){return a.depth==b.depth ? a.order<b.order : a.depth<b.depth;});
    for(const auto& item:ordered_) {
        if(item.entity) { draw(*item.entity,*world.catalog().object(item.entity->type));continue; }
        const auto& p=*item.particle;const auto at=camera.world_to_screen(p.position,p.z);
        if(const auto* t=world.particles().texture(p,time)) {
            auto sprite=texture(*t,static_cast<std::uint32_t>(p.order),nullptr,angle,time,atlas,animations);
            std::shared_ptr<SurfaceTexture> lazy;double density=1;
            if(appearances) { auto image=appearances->texture(*t,static_cast<std::uint32_t>(p.order),static_cast<int>(p.size),p.bottom_anchor||p.motion==ParticleMotion::thrown_sprite,0,0);density=image->pixel_scale;sprite={0,0,0,image->width,image->height};lazy=surface(std::move(image)); }
            const double scale=lazy ? zoom/density : 5*p.size/100*zoom,w=sprite.width*scale,h=sprite.height*scale;
            auto q=lazy ? lazy->quad({at.x-w/2,at.y-(p.bottom_anchor ? h : h/2)},{w,0},{0,h}) : sprite_quad(sprite,{at.x-w/2,at.y-(p.bottom_anchor ? h : h/2)},{w,0},{0,h});
            if(lazy) { q.kind=6;q.owner=lazy; }
            if(p.motion==ParticleMotion::thrown_sprite) {
                // Source rotates the bitmap fill inside its fixed square.
                const double rotation=p.angle+static_cast<double>(static_cast<std::int64_t>(time)-p.born)*.012;
                q.kind=8;q.uv_offset={static_cast<float>(std::cos(rotation)),static_cast<float>(std::sin(rotation))};
            }
            out.push_back(std::move(q));
        } else {
            const double size=p.custom_size ? p.size : std::floor(p.size/100*5);
            if(size<=0)continue;
            const double square_extent=(size+8)*zoom;
            Quad q;q.kind=7;q.origin={static_cast<float>(at.x-square_extent/2),static_cast<float>(at.y-square_extent/2)};
            q.axis_x={static_cast<float>(square_extent),0};q.axis_y={0,static_cast<float>(square_extent)};
            q.multiply={static_cast<float>((p.color>>16U)&255U)/255,static_cast<float>((p.color>>8U)&255U)/255,static_cast<float>(p.color&255U)/255,1};q.uv_offset={static_cast<float>(size),static_cast<float>(size)};out.push_back(q);
        }
    }
    for (int y = std::max(0, cy - distance); y < std::min(world.height(), cy + distance + 1); ++y)
        for (int x = std::max(0, cx - distance); x < std::min(world.width(), cx + distance + 1); ++x) {
            const auto* ground = world.catalog().ground(world.tile(x, y)->type); if (!ground) continue;
            if (ground->top.sheet >= 0 || ground->top.animated >= 0)
                out.push_back(ground_quad(texture(ground->top, static_cast<std::uint32_t>(x + y * world.width()), nullptr, angle, time, atlas, animations), {static_cast<double>(x), static_cast<double>(y)}, camera, 1));
        }
}
}
