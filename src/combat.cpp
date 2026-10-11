#include "realm/combat.hpp"
#include "realm/movement.hpp"
#include "realm/item_data.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
namespace realm {
namespace {
const ProjectileDef* definition(const ObjectDef& container, int id) {
    const auto it=std::find_if(container.projectiles.begin(),container.projectiles.end(),[&](const auto& p){return p.id==id;});
    return it==container.projectiles.end() ? nullptr : &*it;
}
bool targetable(const Entity& entity) { return !(entity.condition&((1U<<20U)|(1U<<18U))); }
}
void Combat::reset(std::uint32_t seed) {
    for (const auto& p:projectiles_) world_.remove(p.entity_id);
    projectiles_.clear();random_=FlashRandom(seed);next_bullet_id_=fake_bullet_id_=next_ability_time_=0;
}
void Combat::spawn(std::uint16_t type,int bullet_type,std::int32_t owner,std::int32_t bullet,Vec2 start,double angle,std::int32_t time,int damage) {
    const auto* container=world_.catalog().object(type);if(!container)return;
    const auto* def=definition(*container,bullet_type);if(!def)return;
    const auto* visual=world_.catalog().object(def->object_id);if(!visual)return;
    FlyingProjectile p;p.entity_id=world_.allocate_fake_id();p.bullet_id=bullet;p.owner_id=owner;p.start_time=time;p.container_type=type;p.bullet_type=bullet_type;p.damage=damage;p.start=start;p.angle=angle;p.definition=def;p.damages_players=container->enemy;
    auto& entity=world_.add(p.entity_id,visual->type,start,def->size>=0 ? def->size : container->size);
    entity.projectile=true;entity.facing=angle;entity.z=.5;
    if(const auto* ground=world_.tile(static_cast<int>(start.x),static_cast<int>(start.y))) {
        const auto* g=world_.catalog().ground(ground->type);const auto* cover=world_.entity(ground->object_id);const auto* cover_def=cover ? world_.catalog().object(cover->type) : nullptr;
        if(!container->flying && g && g->sink && (!cover_def||!cover_def->protect_sink))entity.z=.1;
        else if(const auto* owner_entity=world_.entity(owner);owner_entity&&owner_entity->sink_level)entity.z=.5-.4*owner_entity->sink_level/18.0;
    }
    projectiles_.push_back(std::move(p));
}
void Combat::attack(std::int32_t id,std::uint16_t type,double angle,std::int32_t time) {
    if(auto* e=world_.entity(id)) {
        e->attack_angle=angle;e->attack_start=time;
        if(e->player)if(const auto* weapon=world_.catalog().object(type)) { const double rate=weapon->rate_of_fire;if(rate>0)e->attack_period=1/attack_frequency(*e)/rate; }
    }
}
std::vector<net::Packet> Combat::receive(const net::Event& event,std::int32_t time) {
    using namespace net;std::vector<Packet> output;
    switch(event.id) {
    case flash::PacketId::ENEMYSHOOT: {
        const auto& shoot=std::get<incoming::EnemyShoot>(event.data);output.push_back(outgoing::encode(outgoing::ShootAck{time}));
        if(const auto* owner=world_.entity(shoot.owner_id)) {
            const auto type=owner->type;
            for(int i=0;i<shoot.num_shots;++i)spawn(type,shoot.bullet_type,shoot.owner_id,shoot.bullet_id+i,{shoot.position.x,shoot.position.y},static_cast<double>(shoot.angle)+static_cast<double>(shoot.angle_increment)*i,time,shoot.damage);
            attack(shoot.owner_id,type,static_cast<double>(shoot.angle)+static_cast<double>(shoot.angle_increment)*(shoot.num_shots-1)/2,time);
        }
        break;
    }
    case flash::PacketId::SERVERPLAYERSHOOT: {
        const auto& shoot=std::get<incoming::ServerPlayerShoot>(event.data);const bool local=shoot.owner_id==world_.player_id();
        if(local)output.push_back(outgoing::encode(outgoing::ShootAck{time}));
        if(world_.entity(shoot.owner_id)) {
            for(std::size_t i=0;i<shoot.damages.size();++i)spawn(static_cast<std::uint16_t>(shoot.container_type),0,shoot.owner_id,local ? shoot.bullet_id+static_cast<std::int32_t>(i) : 0,{shoot.position.x,shoot.position.y},shoot.angle+shoot.angle_increment*static_cast<double>(i),time,shoot.damages[i]);
            attack(shoot.owner_id,static_cast<std::uint16_t>(shoot.container_type),shoot.angle,time);
        }
        break;
    }
    case flash::PacketId::ALLYSHOOT: {
        const auto& shoot=std::get<incoming::AllyShoot>(event.data);const auto* weapon=world_.catalog().object(static_cast<std::uint16_t>(shoot.container_type));const auto* owner=world_.entity(shoot.owner_id);
        if(weapon&&owner) {
            const auto start=owner->position;const int count=weapon->num_projectiles;const double gap=weapon->arc_gap;
            for(int i=0;i<count;++i)spawn(weapon->type,0,shoot.owner_id,fake_bullet_id_++,start,shoot.angle-gap*(count-1)/2+gap*i,time,0);
            attack(shoot.owner_id,weapon->type,shoot.angle,time);
        }
        break;
    }
    case flash::PacketId::AOE: {
        const auto& aoe=std::get<incoming::Aoe>(event.data);
        if(auto* p=world_.entity(world_.player_id())) {
            if(!(p->condition&(1U<<20U))&&std::hypot(p->position.x-aoe.position.x,p->position.y-aoe.position.y)<aoe.radius) {
                const std::array<std::uint8_t,1> effects{aoe.effect};
                world_.report_damage(p->id,damage_with_defense(aoe.damage,p->defense,false,p->condition),time,aoe.effect ? std::span<const std::uint8_t>(effects) : std::span<const std::uint8_t>{});
            }
            output.push_back(outgoing::encode(outgoing::AoeAck{time,{static_cast<float>(p->position.x),static_cast<float>(p->position.y)}}));
        }
        break;
    }
    default:break;
    }
    return output;
}
std::vector<net::Packet> Combat::update(std::int32_t time) {
    using namespace net;std::vector<Packet> output;
    for(std::size_t i=0;i<projectiles_.size();) {
        auto& p=projectiles_[i];auto* visual=world_.entity(p.entity_id);const auto& def=*p.definition;const auto elapsed=time-p.start_time;
        bool alive=visual && elapsed<=def.motion.lifetime_ms;
        Vec2 position{};
        if(alive) {
            position=projectile_position(def.motion,p.start,p.angle,p.bullet_id,elapsed);
            const auto* tile=position.x<0||position.y<0||position.x>=world_.width()||position.y>=world_.height() ? nullptr : world_.tile(static_cast<int>(position.x),static_cast<int>(position.y));
            const auto* cover=tile ? world_.entity(tile->object_id) : nullptr;const auto* cover_def=cover ? world_.catalog().object(cover->type) : nullptr;
            if(!tile || tile->type==255 || (cover_def && (!cover_def->enemy||p.damages_players) && (cover_def->enemy_occupy||(!def.passes_cover&&cover_def->occupy)))) {
                if(p.damages_players)output.push_back(outgoing::encode(outgoing::SquareHit{time,p.bullet_id}));
                alive=false;
            } else visual->position=position;
        }
        if(alive) {
            Entity* target{};double distance=std::numeric_limits<double>::max();
            auto consider=[&](Entity& e) {
                if(!targetable(e)||p.hit.contains(e.id))return;
                const double dx=std::abs(e.position.x-position.x),dy=std::abs(e.position.y-position.y);
                if(dx>.5||dy>.5)return;
                const double d=dx*dx+dy*dy;if(d<distance){target=&e;distance=d;}
            };
            if(p.damages_players) { if(auto* player=world_.entity(world_.player_id()))consider(*player); }
            else for(auto& e:world_.entities())if(const auto* type=world_.catalog().object(e.type);type&&type->enemy&&!e.projectile)consider(e);
            if(target) {
                if(p.damages_players || p.owner_id==world_.player_id()) {
                    if(target->id==world_.player_id())output.push_back(outgoing::encode(outgoing::PlayerHit{p.bullet_id}));
                    else output.push_back(outgoing::encode(outgoing::EnemyHit{time,p.bullet_id,target->id}));
                    world_.report_damage(target->id,damage_with_defense(p.damage,target->defense,def.armor_piercing,target->condition),time,{},def.armor_piercing);
                }
                if(def.multi_hit)p.hit.insert(target->id);else alive=false;
            }
        }
        if(!alive) { world_.remove(p.entity_id);if(i!=projectiles_.size()-1)projectiles_[i]=std::move(projectiles_.back());projectiles_.pop_back(); }
        else ++i;
    }
    return output;
}
std::vector<net::Packet> Combat::fire(const ObjectDef& weapon,std::int32_t data,double angle,std::int32_t time,bool ability) {
    using namespace net;std::vector<Packet> output;const auto* player=world_.entity(world_.player_id());if(!player)return output;
    const auto* projectile=definition(weapon,0);const int count=weapon.num_projectiles;if(!projectile||count<=0||count>255)return output;
    const double multiplier=attack_multiplier(*player),mod=item_data::stat(data,item_data::damage);
    const int minimum=projectile->min_damage+static_cast<int>(projectile->min_damage*mod);
    const int maximum=projectile->max_damage+static_cast<int>(projectile->max_damage*mod);
    const auto owner=player->id;const auto start=player->position;
    const auto first=next_bullet_id_;next_bullet_id_=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(next_bullet_id_)-static_cast<std::uint32_t>(count));
    for(int i=0;i<count;++i) {
        const int damage=static_cast<int>(random_.range(static_cast<std::uint32_t>(minimum),static_cast<std::uint32_t>(maximum))*multiplier);
        const auto bullet=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first)-static_cast<std::uint32_t>(i));
        spawn(weapon.type,0,owner,bullet,start,angle-weapon.arc_gap*(count-1)/2+weapon.arc_gap*i,time,damage);
    }
    output.push_back(outgoing::encode(outgoing::PlayerShoot{time,{static_cast<float>(start.x),static_cast<float>(start.y)},static_cast<float>(angle),ability,static_cast<std::uint8_t>(count)}));
    return output;
}
std::vector<net::Packet> Combat::shoot(double angle,std::int32_t time,bool ability) {
    auto* player=world_.entity(world_.player_id());if(!player || (player->condition&(1U<<5U)) || !std::isfinite(angle))return {};
    const auto it=player->stats.find(static_cast<std::uint8_t>(flash::StatId::INVENTORY_0_STAT));if(it==player->stats.end()||it->second<0)return {};
    const auto* weapon=world_.catalog().object(static_cast<std::uint16_t>(it->second));if(!weapon||!definition(*weapon,0))return {};
    const auto data_it=player->stats.find(static_cast<std::uint8_t>(flash::StatId::ITEMDATA_0_STAT));const auto data=data_it==player->stats.end() ? -1 : data_it->second;
    const double rate=weapon->rate_of_fire*(1+item_data::stat(data,item_data::rate_of_fire));if(rate<=0)return {};
    player->attack_period=1/attack_frequency(*player)/rate;if(time<player->attack_start+player->attack_period)return {};
    player->attack_angle=angle;player->attack_start=time;
    return fire(*weapon,data,angle,time,ability);
}
std::vector<net::Packet> Combat::use_ability(Vec2 target,std::int32_t time) {
    using namespace net;const auto* player=world_.entity(world_.player_id());if(!player || time<next_ability_time_ || !std::isfinite(target.x) || !std::isfinite(target.y))return {};
    const auto it=player->stats.find(static_cast<std::uint8_t>(flash::StatId::INVENTORY_1_STAT));if(it==player->stats.end()||it->second<0)return {};
    const auto* ability=world_.catalog().object(static_cast<std::uint16_t>(it->second));if(!ability||!ability->usable)return {};
    const auto mana=player->stats.find(static_cast<std::uint8_t>(flash::StatId::MP_STAT));if(ability->mp_cost>(mana==player->stats.end() ? 0 : mana->second))return {};
    if(ability->ability_target && (target.x<0||target.y<0||target.x>=world_.width()||target.y>=world_.height()))return {};
    if(ability->ability_teleport && !valid_player_position(world_,*player,target))return {};
    const auto data_it=player->stats.find(static_cast<std::uint8_t>(flash::StatId::ITEMDATA_1_STAT));const auto data=data_it==player->stats.end() ? -1 : data_it->second;
    next_ability_time_=time+static_cast<int>(ability->cooldown_ms*(1-item_data::stat(data,item_data::cooldown)));
    std::vector<Packet> output{outgoing::encode(outgoing::UseItem{time,{player->id,1},{static_cast<float>(target.x),static_cast<float>(target.y)}})};
    if(ability->ability_shoot) {
        auto shots=fire(*ability,data,std::atan2(target.y-player->position.y,target.x-player->position.x),time,true);
        output.insert(output.end(),std::make_move_iterator(shots.begin()),std::make_move_iterator(shots.end()));
    }
    return output;
}
}
