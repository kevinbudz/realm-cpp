#include "realm/telemetry.hpp"
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
// A standalone surface shows nothing where a quad's uv leaves [0,1], but inside the atlas the neighbouring slot would show, so the
// quad is cut to the part inside its region. The local-to-uv map is axis aligned, so each axis clips on its own. False: nothing left.
bool clip_to_region(Quad& q, const SpriteRegion& r) {
    constexpr double inv = 1.0 / Atlas::page_size, snap = 1e-5;
    double t0[2], t1[2];
    for (int k = 0; k < 2; ++k) {
        const double lo = (k ? r.y : r.x) * inv, hi = lo + (k ? r.height : r.width) * inv, u = q.uv[static_cast<std::size_t>(k)], d = q.uv[static_cast<std::size_t>(k + 2)];
        t0[k] = std::max(0.0, (lo - u) / d); t1[k] = std::min(1.0, (hi - u) / d);
        if (t0[k] < snap) t0[k] = 0;
        if (t1[k] > 1 - snap) t1[k] = 1;   // rounding noise at an exactly fitting edge must not move the quad
        if (!(t1[k] > t0[k])) return false;
    }
    if (t0[0] == 0 && t0[1] == 0 && t1[0] == 1 && t1[1] == 1) return true;
    const auto ax = q.axis_x, ay = q.axis_y;
    for (std::size_t c = 0; c < 2; ++c) {
        q.origin[c] = static_cast<float>(q.origin[c] + t0[0] * ax[c] + t0[1] * ay[c]);
        q.axis_x[c] = static_cast<float>(ax[c] * (t1[0] - t0[0])); q.axis_y[c] = static_cast<float>(ay[c] * (t1[1] - t0[1]));
    }
    for (std::size_t k = 0; k < 2; ++k) { const double u = q.uv[k], d = q.uv[k + 2]; q.uv[k] = static_cast<float>(u + t0[k] * d); q.uv[k + 2] = static_cast<float>(d * (t1[k] - t0[k])); }
    return true;
}
}
std::shared_ptr<SurfaceTexture> Scene::surface(std::shared_ptr<const Image> image) {
    REALM_ZONE("appearance.gpu_cache");
    if(const auto found=appearance_index_.find(image.get());found!=appearance_index_.end()) {
        if(found->second->image.lock()==image) { telemetry::add(telemetry::Counter::gpu_appearance_hits);const auto result=found->second->surface;appearances_.splice(appearances_.begin(),appearances_,found->second);return result; }
        appearance_bytes_-=found->second->bytes;appearances_.erase(found->second);appearance_index_.erase(found);   // address reused by a different image
    }
    telemetry::add(telemetry::Counter::gpu_appearance_misses);
    while(!appearances_.empty()&&(appearances_.size()>=Appearances::maximum_entries||appearance_bytes_+image->pixels.size()>(overflow_only() ? overflow_surface_bytes_ : Appearances::maximum_bytes))) {
        telemetry::add(telemetry::Counter::gpu_appearance_evictions);appearance_bytes_-=appearances_.back().bytes;appearance_index_.erase(appearances_.back().key);appearances_.pop_back();
    }
    auto result=std::make_shared<SurfaceTexture>();result->update(*image);appearance_bytes_+=image->pixels.size();appearances_.push_front({image,result,image->pixels.size(),image.get()});appearance_index_[image.get()]=appearances_.begin();return result;
}
std::shared_ptr<SurfaceTexture> Scene::status_surface(const std::shared_ptr<const Image>& image) {
    if(const auto found=status_surfaces_.find(image.get());found!=status_surfaces_.end()) {
        if(found->second->image.lock()==image) { const auto result=found->second->surface;status_list_.splice(status_list_.begin(),status_list_,found->second);return result; }
        status_surface_bytes_-=found->second->bytes;status_list_.erase(found->second);status_surfaces_.erase(found);   // address reused by a different image
    }
    const auto bytes=image->pixels.size();
    while(!status_list_.empty()&&(status_list_.size()>=maximum_status_surfaces||status_surface_bytes_+bytes>(overflow_only() ? overflow_status_bytes_ : maximum_status_surface_bytes))) {
        status_surface_bytes_-=status_list_.back().bytes;status_surfaces_.erase(status_list_.back().key);status_list_.pop_back();
    }
    auto result=std::make_shared<SurfaceTexture>();result->update(*image);status_surface_bytes_+=bytes;
    status_list_.push_front({image,result,bytes,image.get()});status_surfaces_[image.get()]=status_list_.begin();return result;
}
bool Scene::dynamic_ready(Atlas& atlas) {
    if(!dynamic_||!atlas.dynamic_pages())return false;
    if(residency_atlas_!=&atlas||residency_texture_!=atlas.texture()||residency_.pages()!=atlas.dynamic_pages()) {
        reset_residency();residency_=TextureResidency(atlas.dynamic_pages(),Atlas::page_size);residency_.set_protected_frames(protect_frames_);residency_.set_growth(max_pages_,grow_frames_);residency_atlas_=&atlas;residency_texture_=atlas.texture();
    }
    return true;
}
void Scene::reset_residency() { residency_.clear();resident_appearances_.clear();resident_labels_.clear();resident_owners_.clear(); }
std::optional<TextureSlot> Scene::place(Atlas& atlas,std::uint64_t id,int width,int height) {
    auto slot=residency_.insert(id,width,height);
    if(!slot&&residency_.growth_requested()>residency_.pages()) {
        // Layer indices and the page size do not change, so quads already built this frame keep sampling the same texels of the new texture.
        const int pages=residency_.growth_requested();
        atlas.resize_dynamic(pages);residency_.add_pages(pages-residency_.pages());residency_texture_=atlas.texture();
        slot=residency_.insert(id,width,height);
    }
    for(const auto evicted:residency_.evicted()) {
        telemetry::add(telemetry::Counter::atlas_evictions);
        const auto owner=resident_owners_.find(evicted);if(owner==resident_owners_.end())continue;
        if(owner->second.label)resident_labels_.erase(owner->second.image);else resident_appearances_.erase(owner->second.key);
        resident_owners_.erase(owner);
    }
    if(!slot) { ++fallbacks_;telemetry::add(telemetry::Counter::atlas_fallbacks); }
    return slot;
}
std::optional<Scene::Resident> Scene::resident_appearance(const Appearances::Key& key,const Appearances& appearances,Atlas& atlas) {
    if(!dynamic_ready(atlas))return std::nullopt;
    const auto make=[&](const ResidentAppearance& r,const TextureSlot& s) { return Resident{{atlas.dynamic_first()+s.page,s.x,s.y,r.width,r.height},r.density}; };
    if(const auto found=resident_appearances_.find(key);found!=resident_appearances_.end()) {
        if(const auto* slot=residency_.find(found->second.id))return make(found->second,*slot);
        resident_owners_.erase(found->second.id);resident_appearances_.erase(found);   // not expected: eviction drops the entry
    }
    const auto image=appearances.image(key);
    const std::uint64_t id=next_resident_++;
    const auto slot=place(atlas,id,image->width,image->height);
    if(!slot)return std::nullopt;
    atlas.upload_dynamic(slot->page,slot->x,slot->y,*image);
    const ResidentAppearance entry{id,image->width,image->height,image->pixel_scale};
    resident_appearances_[key]=entry;resident_owners_[id]={false,key,nullptr};
    return make(entry,*slot);
}
std::optional<SpriteRegion> Scene::resident_label(const std::shared_ptr<const Image>& image,Atlas& atlas) {
    if(!dynamic_ready(atlas))return std::nullopt;
    const auto make=[&](const TextureSlot& s) { return SpriteRegion{atlas.dynamic_first()+s.page,s.x,s.y,s.width,s.height}; };
    if(const auto found=resident_labels_.find(image.get());found!=resident_labels_.end()) {
        if(found->second.image.lock()==image)if(const auto* slot=residency_.find(found->second.id))return make(*slot);
        residency_.erase(found->second.id);resident_owners_.erase(found->second.id);resident_labels_.erase(found);   // address reused by a different image
    }
    const std::uint64_t id=next_resident_++;
    const auto slot=place(atlas,id,image->width,image->height);
    if(!slot)return std::nullopt;
    atlas.upload_dynamic(slot->page,slot->x,slot->y,*image);
    resident_labels_[image.get()]={image,id,image->width,image->height};resident_owners_[id]={true,{},image.get()};
    return make(*slot);
}
std::vector<Scene::ResidentImage> Scene::resident_images(const Appearances* appearances, const Atlas& atlas) const {
    std::vector<ResidentImage> result;
    for (const auto& [id, slot] : residency_.slots()) {
        const auto owner = resident_owners_.find(id);
        if (owner == resident_owners_.end()) continue;
        const SpriteRegion region{atlas.dynamic_first() + slot.page, slot.x, slot.y, slot.width, slot.height};
        std::shared_ptr<const Image> image;
        if (owner->second.label) { const auto label = resident_labels_.find(owner->second.image); if (label != resident_labels_.end()) image = label->second.image.lock(); }
        else if (appearances) image = appearances->image(owner->second.key);
        result.push_back({region, std::move(image)});
    }
    return result;
}
std::optional<Quad> Scene::status_quad(const std::shared_ptr<const Image>& image,Atlas& atlas,Vec2 origin,Vec2 ax,Vec2 ay) {
    const auto region=resident_label(image,atlas);if(!region)return std::nullopt;
    auto q=sprite_quad(*region,origin,ax,ay);q.kind=5;return q;
}
bool Scene::model_reaches(const ObjectDef& def,const Entity& entity,const Camera& camera,int width,int height) {
    auto found=model_extents_.find(&def);
    if(found==model_extents_.end())found=model_extents_.emplace(&def,model_extent(def.model)).first;
    const auto& extent=found->second;
    const double unit=camera.pixels_per_unit(),side=extent.radius*unit,up=side+std::max(0.0,extent.top)*unit,down=side+std::max(0.0,-extent.bottom)*unit;
    const auto p=camera.world_to_screen(entity.position);
    return p.x+side>0&&p.x-side<width&&p.y+down>0&&p.y-up<height;
}
const Scene::StaticBounds& Scene::static_bounds(const Catalog& catalog) {
    if(static_bounds_.catalog==&catalog)return static_bounds_;
    StaticBounds b;b.catalog=&catalog;
    for(const auto& def:catalog.objects()) {
        if(!def.stationary)continue;
        b.z_low=std::min(b.z_low,def.z);b.z_high=std::max(b.z_high,def.z);
        if(def.model.empty())continue;
        auto found=model_extents_.find(&def);
        if(found==model_extents_.end())found=model_extents_.emplace(&def,model_extent(def.model)).first;
        const auto& e=found->second;b.model_reach=std::max(b.model_reach,e.radius+std::max({0.0,e.top,-e.bottom}));
    }
    static_bounds_=b;return static_bounds_;
}
std::pair<Vec2,Vec2> Scene::static_footprint(const Catalog& catalog,const Camera& camera,double zoom,int width,int height) {
    // The cull test accepts p = world_to_screen(position, z) inside (-400, w + 400) x (-100, h + 400) * zoom, or a model whose
    // extent around the z = 0 ground point reaches the screen. On the z = 0 plane z lifts the tested point by 50 * z * scale.
    const auto& bounds = static_bounds(catalog);
    const double lift = 50 * camera.scale(), side = bounds.model_reach * camera.pixels_per_unit();
    const double x0 = std::min(-400 * zoom, -side), x1 = std::max(width + 400 * zoom, width + side);
    const double y0 = std::min(-100 * zoom + lift * bounds.z_low, -side), y1 = std::max(height + 400 * zoom + lift * bounds.z_high, height + side);
    constexpr double slack = .01;
    Vec2 low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}, high{-low.x, -low.y};
    for (const Vec2 corner : {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x0, y1}, Vec2{x1, y1}}) {
        const auto w = camera.screen_to_world(corner);
        low = {std::min(low.x, w.x - slack), std::min(low.y, w.y - slack)}; high = {std::max(high.x, w.x + slack), std::max(high.y, w.y + slack)};
    }
    return {low, high};
}
void Scene::build(const World& world, const Camera& camera, Atlas& atlas, const AnimationLibrary& animations, const ProcessedTextures* processed, const Terrain* terrain, const Appearances* appearances,
                  Vec2 position, double angle, double zoom, int width, int height, std::int32_t time, std::vector<Quad>& out) {
    REALM_ZONE("scene.build");
    submitted_.fill(0);
    if(dynamic_ready(atlas))residency_.begin_frame();
    if(appearance_world_!=&world||generation_!=world.generation()) {
        reset_residency();
        // A map reset drops the working set, so the pages it needed go back too (the residency is rebuilt at the initial size).
        if(overflow_only()&&initial_pages_>0&&atlas.dynamic_pages()>initial_pages_) { atlas.resize_dynamic(initial_pages_);if(dynamic_ready(atlas))residency_.begin_frame(); }
        appearances_.clear();appearance_index_.clear();webs_.clear();status_surfaces_.clear();status_list_.clear();status_surface_bytes_=0;appearance_bytes_=0;if(appearances)appearances->clear();generation_=world.generation();appearance_world_=&world;
    }
    const int distance = static_cast<int>(std::ceil(camera.max_distance())) + 3;
    const int cx = static_cast<int>(std::floor(position.x)), cy = static_cast<int>(std::floor(position.y));
    const double extent = camera.pixels_per_unit() * 1.5;
    { REALM_ZONE("scene.terrain");
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
    }
    bool negative_ids=false;
    { REALM_ZONE("scene.visibility");
    visible_.clear();
    const auto consider = [&](const Entity& e) {
        const auto p = camera.world_to_screen(e.position, e.z);
        const auto* def = world.catalog().object(e.type);
        if (cull_ && !(p.x > -400 * zoom && p.x < width + 400 * zoom && p.y > -100 * zoom && p.y < height + 400 * zoom)
            && !(def && !def->model.empty() && model_reaches(*def, e, camera, width, height))) return;
        negative_ids |= e.id < 0;
        visible_.push_back({&e, def, static_cast<int>(camera.world_to_screen(e.position).y / zoom + camera.clip_rect().y), e.id});
    };
    if (!cull_ || scan_visibility_) for (const auto& e : world.entities()) consider(e);
    else {
        // Dynamic entities (actors, projectiles, loot) are all tested; static ones only from the grid cells overlapping
        // static_footprint(). Ids are unique, so the (depth, id) sort below fixes the order whatever the gather order.
        world.each_dynamic(consider);
        const auto [low, high] = static_footprint(world.catalog(), camera, zoom, width, height);
        world.each_static_in(low, high, consider);
    }
    telemetry::set(telemetry::Counter::visible_entities,visible_.size());
    }
    { REALM_ZONE("scene.sort_entities");
    std::sort(visible_.begin(), visible_.end(), [](const Visible& a, const Visible& b) { return a.depth == b.depth ? a.id < b.id : a.depth < b.depth; });
    }
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
    auto draw_impl = [&](const Entity& e, const ObjectDef& d) {
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
        if (const auto model = d.model.empty() ? std::nullopt : model_index(d.model)) {
            // GameObject.draw3d: a registered Model replaces the 2D sprite. The sprite is only
            // its texture; Solid* groups and sprite-less objects use the XML Color.
            const auto& chosen = t->select(static_cast<std::uint32_t>(e.id));
            std::optional<SpriteRegion> model_sprite;
            if (chosen.sheet >= 0 || chosen.animated >= 0) model_sprite = sprite;
            double rotation = std::isfinite(e.model_rotation) ? e.model_rotation : d.rotation;
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
            out.push_back(model_quad(*model, camera, e.position, rotation, d.color, model_sprite));
            return;
        }
        // Projectile.draw: with Projectile Outline off the plain texture fills the projectile's square.
        bool baked = (processed || appearances) && d.model.empty() && !(e.projectile && !world.preferences().proj_outline);
        std::shared_ptr<SurfaceTexture> lazy;
        bool resident=false;
        double density=1;
        if (baked) {
            const auto& chosen = t->select(static_cast<std::uint32_t>(e.id));
            const auto* c=animations.character(chosen.animated,chosen.index);
            const auto frame=c ? animation_frame(*c,&e,angle,time) : AnimationFrame{};
            // The processed-texture key is a string: build it only when a processed set exists and the object is undyed.
            const bool use_processed=processed&&!(dyes[0]||dyes[1])&&processed->contains(c ? animated_texture_key(frame.image,shown_size,e.projectile) : static_texture_key(chosen.sheet,chosen.index,shown_size,e.projectile));
            if(use_processed)sprite=atlas.additional(processed->find(c ? animated_texture_key(frame.image,shown_size,e.projectile) : static_texture_key(chosen.sheet,chosen.index,shown_size,e.projectile)));
            else if(appearances) {
                const auto key=c ? appearances->animated_key(frame,shown_size,e.projectile,dyes[0],dyes[1]) : appearances->texture_key(chosen,static_cast<std::uint32_t>(e.id),shown_size,e.projectile,dyes[0],dyes[1]);
                if(const auto found=resident_appearance(key,*appearances,atlas)) { sprite=found->region;density=found->density;resident=true; }
                else { auto image=appearances->image(key);density=image->pixel_scale;sprite={0,0,0,image->width,image->height};lazy=surface(std::move(image)); }
            } else baked=false;
        }
        if (e.projectile) {
            const double rotation = d.rotation == 0 ? 0 : time / d.rotation;
            auto q=projectile_quad(sprite,camera.world_to_screen(e.position,e.z),e.facing-angle+d.angle_correction+rotation,e.size,zoom,baked);
            if(lazy) {
                const auto base=lazy->quad({}, {}, {});q.texture=base.texture;q.owner=lazy;q.kind=6;
                q.uv[0]*=Atlas::page_size/static_cast<float>(sprite.width);q.uv[1]*=Atlas::page_size/static_cast<float>(sprite.height);
                q.uv[2]*=Atlas::page_size/static_cast<float>(sprite.width);q.uv[3]*=Atlas::page_size/static_cast<float>(sprite.height);
            } else if(resident) {
                if(!clip_to_region(q,sprite))return;
                q.kind=6;
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
            if(lazy||resident)q.kind=6;
            q.owner=lazy;flash_tint(q,e);condition_tint(q,e);out.push_back(std::move(q));
        }
        if(status_art_&&e.condition) {
            // GameObject.drawConditionIcons: 16 px icons centered over the sprite's top edge.
            condition_icon_frames(e.condition,time,icon_frames_);
            const double top=p.y-h+clipped,left=p.x-8.0*static_cast<double>(icon_frames_.size())*zoom;
            for(std::size_t i=0;i<icon_frames_.size();++i) {
                const auto image=status_art_->icon(icon_frames_[i]);if(!image)continue;
                const Vec2 at{left+16.0*static_cast<double>(i)*zoom,top-8*zoom};
                if(const auto region=resident_label(image,atlas)) { auto q=sprite_quad(*region,at,{16*zoom,0},{0,16*zoom});q.kind=6;out.push_back(std::move(q));continue; }
                const auto surface=status_surface(image);
                auto q=surface->quad(at,{16*zoom,0},{0,16*zoom});q.kind=6;q.owner=surface;out.push_back(std::move(q));
            }
        }
        if(status_art_&&!e.name.empty()&&(d.show_name||(d.player&&e.id!=world.player_id()))) {
            // GameObject.drawName: the name bitmap is centered on the object, top at its ground position.
            const auto label=status_art_->name(e.name);
            if(label.image) {
                const auto ground=camera.world_to_screen(e.position);
                const double image_w=label.image->width/label.image->pixel_scale,image_h=label.image->height/label.image->pixel_scale;
                const double field_w=std::floor(label.width/2+1)*2;
                const Vec2 at{ground.x-field_w/2*zoom+label.offset.x*zoom,ground.y+label.offset.y*zoom};
                if(const auto region=resident_label(label.image,atlas)) { auto q=sprite_quad(*region,at,{image_w*zoom,0},{0,image_h*zoom});q.kind=6;out.push_back(std::move(q)); }
                else {
                    const auto surface=status_surface(label.image);
                    auto q=surface->quad(at,{image_w*zoom,0},{0,image_h*zoom});q.kind=6;q.owner=surface;out.push_back(std::move(q));
                }
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
    const auto draw = [&](const Entity& e,const ObjectDef& d) {
        const auto before=out.size();draw_impl(e,d);
        if(out.size()>before)++submitted_[static_cast<std::size_t>(e.projectile?3:e.player?0:!d.model.empty()?2:d.enemy?1:4)];
    };
    for (const auto& v : visible_) if (v.def && v.def->draw_under) draw(*v.entity, *v.def);
    if (world.preferences().draw_shadows) for (const auto& v : visible_) {
        const auto* e = v.entity; const auto* d = v.def; if (!d || d->draw_under || d->class_name == "Wall" || !d->shadow_size) continue;
        const auto sprite = texture(e->player ? world.catalog().player_texture(e->type,e->texture) : d->texture, static_cast<std::uint32_t>(e->id), e, angle, time, atlas, animations);
        const double scale = e->projectile ? d->shadow_size / 400.0 * zoom : e->size / 100.0 * d->shadow_size / 100.0 * sprite.height / 8.0 * zoom;
        const auto p = camera.world_to_screen(e->position);
        const auto color = d->shadow_color;
        out.push_back(radial_shadow({p.x - 30 * scale, p.y - 15 * scale}, {60 * scale, 0}, {0, 30 * scale},
            {static_cast<float>((color >> 16U) & 255U) / 255, static_cast<float>((color >> 8U) & 255U) / 255, static_cast<float>(color & 255U) / 255}, .5F));
    }
    // Painter stream: entities by (depth, id) interleaved with particles by (depth, 0x80000000+order). visible_ is already in
    // entity order, and with non-negative ids every entity precedes every particle at equal depth, so only the (few)
    // particles need sorting and the two sorted runs merge. A negative id would compare above particles as unsigned, so
    // that rare case keeps the original single sort.
    const auto before=[](const DrawItem& a,const DrawItem& b){return a.depth==b.depth ? a.order<b.order : a.depth<b.depth;};
    ordered_.clear();particle_items_.clear();
    for(const auto& v:visible_)if(v.def&&!v.def->draw_under)ordered_.push_back({v.entity,nullptr,v.depth,static_cast<std::uint32_t>(v.id),v.def});
    for(const auto& p:world.particles().particles()) {
        if(p.motion==ParticleMotion::swirl&&(p.z<0||p.z>1))continue;
        const auto at=camera.world_to_screen(p.position,p.z);
        if(cull_&&(at.x < -100*zoom||at.x>width+100*zoom||at.y < -100*zoom||at.y>height+100*zoom))continue;
        (negative_ids ? ordered_ : particle_items_).push_back({nullptr,&p,static_cast<int>(camera.world_to_screen(p.position).y/zoom+camera.clip_rect().y),0x80000000ULL+p.order,nullptr});
    }
    { REALM_ZONE("scene.sort_painter_stream");
    if(negative_ids)std::sort(ordered_.begin(),ordered_.end(),before);
    else {
        std::sort(particle_items_.begin(),particle_items_.end(),before);
        merged_.resize(ordered_.size()+particle_items_.size());
        std::merge(ordered_.begin(),ordered_.end(),particle_items_.begin(),particle_items_.end(),merged_.begin(),before);
        ordered_.swap(merged_);
    }
    }
    for(const auto& item:ordered_) {
        if(item.entity) { draw(*item.entity,*item.def);continue; }
        const auto& p=*item.particle;const auto at=camera.world_to_screen(p.position,p.z);
        if(const auto* t=world.particles().texture(p,time)) {
            auto sprite=texture(*t,static_cast<std::uint32_t>(p.order),nullptr,angle,time,atlas,animations);
            std::shared_ptr<SurfaceTexture> lazy;bool resident=false;double density=1;
            if(appearances) {
                const auto key=appearances->texture_key(*t,static_cast<std::uint32_t>(p.order),static_cast<int>(p.size),p.bottom_anchor||p.motion==ParticleMotion::thrown_sprite,0,0);
                if(const auto found=resident_appearance(key,*appearances,atlas)) { sprite=found->region;density=found->density;resident=true; }
                else { auto image=appearances->image(key);density=image->pixel_scale;sprite={0,0,0,image->width,image->height};lazy=surface(std::move(image)); }
            }
            const double scale=lazy||resident ? zoom/density : 5*p.size/100*zoom,w=sprite.width*scale,h=sprite.height*scale;
            auto q=lazy ? lazy->quad({at.x-w/2,at.y-(p.bottom_anchor ? h : h/2)},{w,0},{0,h}) : sprite_quad(sprite,{at.x-w/2,at.y-(p.bottom_anchor ? h : h/2)},{w,0},{0,h});
            if(lazy) { q.kind=6;q.owner=lazy; }
            else if(resident)q.kind=6;
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
    telemetry::set(telemetry::Counter::atlas_resident_bytes,residency_.used_bytes());telemetry::set(telemetry::Counter::atlas_shelf_bytes,residency_.occupied_area()*4);telemetry::set(telemetry::Counter::atlas_pages,static_cast<std::uint64_t>(atlas.dynamic_pages()));
}
}
