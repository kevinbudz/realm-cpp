#include "realm/combat.hpp"
#include "flash_assets.hpp"
#include "realm/item_data.hpp"
#include "realm/movement.hpp"
#include <iostream>
#include <stdexcept>
namespace { int checks{};void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);} }
int main() {
try {
realm::Catalog catalog(realm::flash::source_root);realm::World world(catalog);world.initialize(8,8);const auto grass=catalog.ground("Grass")->type;
for(int y=0;y<8;++y)for(int x=0;x<8;++x)world.tile(x,y)->type=grass;
world.add(99,catalog.object("Wizard")->type,{2.5,2.5});world.set_player_id(99);world.entity(99)->hp=200;
const realm::ObjectDef* enemy{};const realm::ProjectileDef* projectile{};
for(const auto& object:catalog.objects())if(object.enemy)for(const auto& p:object.projectiles)
if(!p.motion.wavy&&!p.motion.parametric&&!p.motion.boomerang&&!p.motion.amplitude&&!p.motion.accelerate&&!p.motion.decelerate&&!p.multi_hit&&p.motion.speed>0&&p.motion.speed<=1000&&p.motion.lifetime_ms>=500&&catalog.object(p.object_id)){enemy=&object;projectile=&p;break;}
check(enemy&&projectile,"straight hostile projectile fixture exists");world.add(88,enemy->type,{1.5,2.5});
realm::Combat combat(world);combat.reset(77);
realm::net::incoming::EnemyShoot shot;shot.bullet_id=123;shot.owner_id=88;shot.bullet_type=static_cast<std::uint8_t>(projectile->id);shot.position={1.5,2.5};shot.num_shots=1;shot.damage=30;
auto ack=combat.receive({realm::flash::PacketId::ENEMYSHOOT,shot},0);check(ack.size()==1&&ack[0].id==31,"enemy shot ACK");check(combat.projectiles().size()==1,"enemy projectile created");
const auto entity_id=combat.projectiles()[0].entity_id;check(world.entity(entity_id)&&world.entity(entity_id)->projectile,"drawable projectile state");
const auto elapsed=static_cast<std::int32_t>(10000/projectile->motion.speed);auto hits=combat.update(elapsed);
check(hits.size()==1&&hits[0].id==28,"player hit wire message");realm::net::Reader hit(hits[0].body);check(hit.i32()==123,"player hit preserves full bullet ID");
check(world.entity(99)->hp==200 && world.damage_cues().back().damage==30,"damage feedback preserves server-authoritative HP");check(combat.projectiles().empty()&&!world.entity(entity_id),"single hit projectile removed");
world.entity(99)->condition=1U<<20U;(void)combat.receive({realm::flash::PacketId::ENEMYSHOOT,shot},1000);hits=combat.update(1000+elapsed);check(hits.empty()&&world.entity(99)->hp==200,"invincible player not targetable");
(void)combat.update(1000+static_cast<std::int32_t>(projectile->motion.lifetime_ms)+1);check(combat.projectiles().empty(),"lifetime expiry removes projectile");
world.entity(99)->condition=0;realm::net::incoming::Aoe aoe;aoe.position={2.5,2.5};aoe.radius=1;aoe.damage=20;
ack=combat.receive({realm::flash::PacketId::AOE,aoe},2000);check(ack.size()==1&&ack[0].id==30,"AOE acknowledged");check(world.entity(99)->hp==200 && world.damage_cues().back().damage==20,"AOE feedback preserves authoritative HP");
aoe.position={7,7};(void)combat.receive({realm::flash::PacketId::AOE,aoe},2001);check(world.entity(99)->hp==200 && world.damage_cues().size()==2,"outside AOE no feedback");
const realm::ObjectDef* weapon{};
for(const auto& object:catalog.objects())if(!object.projectiles.empty()&&object.xml.number("RateOfFire")>0&&object.xml.integer("NumProjectiles",1)==1&&object.projectiles[0].id==0&&catalog.object(object.projectiles[0].object_id)){weapon=&object;break;}
check(weapon,"weapon fixture exists");world.entity(99)->stats[8]=weapon->type;world.entity(99)->attack_start=-100000;
auto outgoing=combat.shoot(0,3000);check(outgoing.size()==1&&outgoing[0].id==3,"local shot sends PLAYERSHOOT");check(combat.projectiles().size()==1&&combat.projectiles()[0].bullet_id==0,"first local bullet ID zero");
check(combat.shoot(0,3001).empty(),"rate of fire enforced");outgoing=combat.shoot(0,4000);check(!outgoing.empty()&&combat.projectiles().back().bullet_id==-1,"subsequent local bullet ID negative");
combat.reset(123);check(combat.projectiles().empty(),"map transition clears projectiles");
check(realm::item_data::rank(-1)==-1 && realm::item_data::rank(0)==-1,"missing item rank");
check(realm::item_data::rank(0xff)==1 && realm::item_data::rank(128)==8,"rank chooses lowest set tier bit");
check(realm::item_data::stat(128|static_cast<int>(realm::item_data::damage),realm::item_data::damage)==.4,"tier eight damage modifier");
world.entity(99)->stats[71]=128|static_cast<int>(realm::item_data::damage|realm::item_data::rate_of_fire);world.entity(99)->attack_start=-100000;
realm::FlashRandom expected(123);const auto& pdef=weapon->projectiles[0];
const int minimum=pdef.min_damage+static_cast<int>(pdef.min_damage*.4),maximum=pdef.max_damage+static_cast<int>(pdef.max_damage*.4);
const int expected_damage=static_cast<int>(expected.range(static_cast<std::uint32_t>(minimum),static_cast<std::uint32_t>(maximum))*realm::attack_multiplier(*world.entity(99)));
outgoing=combat.shoot(0,5000);check(!outgoing.empty()&&combat.projectiles().back().damage==expected_damage,"ItemData damage bounds truncate before RNG");
check(std::abs(world.entity(99)->attack_period-1/realm::attack_frequency(*world.entity(99))/(weapon->rate_of_fire*1.4))<1e-9,"ItemData rate persists in animation period");
const realm::ObjectDef* ability{};
for(const auto& object:catalog.objects())if(object.usable&&object.ability_shoot&&!object.projectiles.empty()&&object.num_projectiles>0&&object.num_projectiles<=255&&catalog.object(object.projectiles[0].object_id)){ability=&object;break;}
check(ability,"shoot ability fixture exists");world.entity(99)->stats[9]=ability->type;world.entity(99)->stats[4]=ability->mp_cost;
outgoing=combat.use_ability({5.5,2.5},6000);check(outgoing.size()==2&&outgoing[0].id==13&&outgoing[1].id==3,"shoot ability sends USEITEM before PLAYERSHOOT");
realm::net::Reader ability_shot(outgoing[1].body);(void)ability_shot.i32();(void)ability_shot.f32();(void)ability_shot.f32();(void)ability_shot.f32();check(ability_shot.boolean(),"ability shot wire flag");
check(combat.use_ability({5.5,2.5},6001).empty(),"ability cooldown enforced");
world.entity(99)->stats[4]=ability->mp_cost-1;check(combat.use_ability({5.5,2.5},20000).empty(),"insufficient MP rejects ability");
check(world.entity(99)->hp==200,"all local effects preserve authoritative HP");world.expire_damage(20000);check(world.damage_cues().empty(),"damage feedback expires");
std::cout<<checks<<" projectile simulation/acknowledgment checks passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
