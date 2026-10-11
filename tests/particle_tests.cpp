#include "realm/combat.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace { int checks{};void check(bool ok,const char* why) { ++checks;if(!ok)throw std::runtime_error(why); } }
int main() { try {
using namespace realm;Catalog catalog(flash::source_root);World world(catalog);world.initialize(100,100);
world.add(99,catalog.object("Wizard")->type,{50,50});world.set_player_id(99);world.entity(99)->hp=200;
auto& particles=world.particles();
const int expected[]={0,10,20,5,1,10,10,30,24,5,12,6,24,7,0,0,1};
for(int type=0;type<=16;++type) {
 particles.clear();net::incoming::ShowEffect e;e.type=static_cast<std::uint8_t>(type);e.target_id=99;e.position={52,50};e.position2={54,50};e.color=0xff0088ff;
 if(type==5||type==10)e.position={3,0};
 if(type==13)e.position2={3,0};
 if(type==11)e.position2={200,0};
 if(type==15)e.position={.3f,3};
 if(type==16)e.color=catalog.object("Health Potion")->type;
 particles.show(world,e,0);check(particles.particles().size()==static_cast<std::size_t>(expected[type]),"SHOWEFFECT source particle counts");
 particles.update(world,0);for(int t=17;t<11000;t+=17)particles.update(world,t);
 check(particles.particles().empty()&&particles.flashes().empty(),"all effects expire");
 check(world.entity(99)->hp==200,"effects preserve authoritative HP");
 e.target_id=123456;particles.clear();particles.show(world,e,0);
 if(type!=2&&type!=3&&type!=4&&type!=14&&type!=16)check(particles.particles().empty(),"missing required targets ignored");
}
particles.clear();net::incoming::ShowEffect thrown;thrown.type=4;thrown.target_id=99;thrown.position={60,50};thrown.position2={800,0};thrown.has_position2=true;particles.show(world,thrown,0);particles.update(world,400);
check(std::abs(particles.particles()[0].position.x-55)<1e-9&&std::abs(particles.particles()[0].z-2)<1e-9,"fork custom throw midpoint and arc");particles.update(world,801);check(std::none_of(particles.particles().begin(),particles.particles().end(),[](auto& p){return p.motion==ParticleMotion::thrown_spark;}),"custom duration expires");
particles.clear();thrown.position2={-100,0};particles.show(world,thrown,0);check(particles.particles()[0].lifetime==100,"throw duration minimum");
thrown.position2={100000,0};particles.show(world,thrown,0);check(particles.particles().back().lifetime==10000,"throw duration maximum");
particles.clear();const auto cues=world.damage_cues().size();world.report_damage(99,0,0);check(particles.particles().size()==10&&world.damage_cues().size()==cues,"zero damage retains source blood without adding damage text");
particles.clear();world.report_damage(99,10,0);check(particles.particles().size()==10,"damage burst");
world.report_damage(99,200,0);check(particles.particles().size()==10,"fatal damage source suppresses blood");
particles.clear();particles.level_up(99,0);particles.update(world,1000);check(particles.particles().size()==40,"level up two swirls");
check(particles.particles()[0].z==0&&std::abs(particles.particles()[0].position.x-49.5)<1e-9,"source swirl midpoint");
world.remove(99);check(particles.particles().empty(),"removing target destroys following effects");world.add(99,catalog.object("Wizard")->type,{50,50});world.entity(99)->hp=200;
particles.clear();net::Tick tick;net::Status stat;stat.id=99;stat.stats={{26,static_cast<std::int32_t>(1U<<15U)}};tick.statuses={stat};world.apply(tick);particles.update(world,0);check(particles.emitter_count()==1&&particles.particles().size()==1,"healing stat creates white source emitter");
stat.stats={{26,0}};tick.statuses={stat};world.apply(tick);check(particles.emitter_count()==0,"healing stat clears emitter");
std::size_t emitters{};
for(const auto& def:catalog.objects()) {
 if(def.texture.effect.kind.empty())continue;
 ++emitters;particles.clear();world.add(20,def.type,{50,50});particles.update(world,0);particles.update(world,100);particles.update(world,200);
 check(particles.emitter_count()==1&&!particles.particles().empty(),"every actual XML object emitter emits");
 world.remove(20);check(particles.emitter_count()==0,"source emitter target removed");
}
check(emitters==4,"registered source effect inventory covered");
particles.clear();TextureDef mist;mist.effect.kind="XMLEffect";mist.effect.particle="SwirlingMist";mist.effect.cooldown=.1;particles.attach(*world.entity(99),mist);particles.update(world,0);particles.update(world,101);
check(particles.particles().size()==1&&particles.particles()[0].definition&&particles.texture(particles.particles()[0],300),"source XML particle art/animation supported");particles.remove_target(99);

particles.clear();world.add(20,catalog.object("Hexxer")->type,{50,50});world.entity(20)->hp=200;world.report_damage(20,10,0);check(particles.particles().empty(),"transparent art never creates white blood");world.remove(20);
particles.clear();for(int n=0;n<1000;++n)particles.trail({50,50},0);check(particles.particles().size()==Particles::maximum_spawns&&particles.dropped()>0,"per frame flood bounded");
for(int t=1;t<20;++t) { particles.update(world,t);for(int n=0;n<1000;++n)particles.trail({50,50},t); }
check(particles.particles().size()<=Particles::maximum_particles,"dense storage bounded");particles.update(world,11000);check(particles.particles().empty(),"hitch expires stale particles without backlog");
particles.show(world,{14,99,0,{},{},false},0);check(std::hypot(particles.camera_offset(10000).x,particles.camera_offset(10000).y)<=std::sqrt(.5),"jitter amplitude bounded");
world.initialize(20,20);check(particles.particles().empty()&&particles.emitter_count()==0&&particles.camera_offset(10000).x==0,"map teardown clears all effect state");
for(int i=0;i<1300;++i)world.add(1000+i,catalog.object("White Fountain")->type,{5,5});
check(particles.emitter_count()==Particles::maximum_emitters,"emitter flood capped");particles.update(world,100000);check(particles.particles().size()<=Particles::maximum_spawns,"emitter hitch spawn budget capped");world.initialize(20,20);
world.add(99,catalog.object("Wizard")->type,{5,5});world.set_player_id(99);world.entity(99)->hp=200;
for(int y=0;y<20;++y)for(int x=0;x<20;++x)world.tile(x,y)->type=catalog.ground("Grass")->type;
const auto* quiver=catalog.object("Magic Quiver");Combat combat(world);combat.reset(123);net::incoming::ServerPlayerShoot arrow;arrow.owner_id=99;arrow.bullet_id=500;arrow.container_type=static_cast<std::int16_t>(quiver->type);arrow.position={5,5};arrow.damages={10};
const auto ack=combat.receive({flash::PacketId::SERVERPLAYERSHOOT,arrow},0);check(ack.size()==1&&ack[0].id==31,"source trail arrow SHOOTACK retained");(void)combat.update(100);
check(std::count_if(particles.particles().begin(),particles.particles().end(),[](const auto& p){return p.color==0xff00ff;})==3,"actual projectile update creates three magenta trail sparks");
(void)combat.update(200);check(!combat.projectiles().empty(),"passing-cover arrow remains alive");
combat.reset(123);particles.clear();arrow.container_type=static_cast<std::int16_t>(catalog.object("Falchion")->type);(void)combat.receive({flash::PacketId::SERVERPLAYERSHOOT,arrow},0);
const auto& flying=combat.projectiles().front();const auto impact_at=projectile_position(flying.definition->motion,flying.start,flying.angle,flying.bullet_id,200);
const ObjectDef* wall{};for(const auto& d:catalog.objects())if(d.class_name=="Wall"&&d.stationary&&d.occupy) { wall=&d;break; }check(wall,"cover fixture exists");world.add(55,wall->type,{std::floor(impact_at.x)+.5,std::floor(impact_at.y)+.5});
particles.clear();(void)combat.update(200);check(combat.projectiles().empty()&&particles.particles().size()==3,"source cover hit removes arrow and creates impact debris");
world.initialize(20,20);
particles.set_enabled(false);particles.trail({1,1},0);check(particles.particles().empty(),"particle option respected");
particles.set_enabled(true);net::incoming::ShowEffect invalid;invalid.type=2;invalid.position.x=std::numeric_limits<float>::quiet_NaN();particles.show(world,invalid,0);check(particles.particles().empty(),"nonfinite effect dropped");
std::cout<<checks<<" particle source/count/motion/lifetime/authority/budget/teardown checks; "<<emitters<<" actual XML emitters passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
