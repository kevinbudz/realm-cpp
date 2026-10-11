#include "realm/movement.hpp"
#include "flash_assets.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace {
int checks{};
void check(bool ok, const char* why) { ++checks;if(!ok)throw std::runtime_error(why); }
void near(double a,double b,const char* why) { check(std::abs(a-b)<1e-10,why); }
}
int main() {
try {
realm::Entity e;near(realm::move_speed(e),.004,"minimum move speed");e.speed=75;near(realm::move_speed(e),.0096,"maximum move speed");
e.condition=1U<<13U;near(realm::move_speed(e),.0144,"speedy move speed");e.condition|=1U<<2U;near(realm::move_speed(e),.004,"slowed overrides speedy");
e.condition=0;e.dexterity=75;near(realm::attack_frequency(e),.008,"maximum attack frequency");e.condition=1U<<17U;near(realm::attack_frequency(e),.012,"berserk");e.condition|=1U<<4U;near(realm::attack_frequency(e),.0015,"dazed overrides berserk");
e.attack=75;e.condition=0;near(realm::attack_multiplier(e),2,"attack multiplier");e.condition=1U<<16U;near(realm::attack_multiplier(e),3,"damaging");e.condition|=1U<<1U;near(realm::attack_multiplier(e),.5,"weak overrides damaging");
check(realm::damage_with_defense(100,20,false,0)==80,"defense damage");check(realm::damage_with_defense(101,1000,false,0)==15,"damage floor truncates");
check(realm::damage_with_defense(100,20,false,1U<<22U)==60,"armored doubles defense");check(realm::damage_with_defense(100,20,false,(1U<<22U)|(1U<<23U))==100,"armor broken overrides armored");
check(realm::damage_with_defense(100,20,true,0)==100,"piercing");check(realm::damage_with_defense(100,20,true,1U<<21U)==0,"invulnerable ignores piercing");
realm::Catalog catalog(realm::flash::source_root);realm::World world(catalog);world.initialize(8,8);const auto* grass=catalog.ground("Grass");check(grass,"grass loaded");
for(int y=0;y<8;++y)for(int x=0;x<8;++x)world.tile(x,y)->type=grass->type;
const auto* wizard=catalog.object("Wizard");auto& p=world.add(99,wizard->type,{2.5,2.5});world.set_player_id(99);double angle{};
realm::move_player(world,p,{1,1},0,100,angle);near(p.position.x,2.5+.4/std::sqrt(2.0),"diagonal x normalized");near(p.position.y,2.5+.4/std::sqrt(2.0),"diagonal y normalized");
p.position={2.5,2.5};p.condition=1U<<12U;realm::move_player(world,p,{1,0},0,100,angle);near(p.position.x,2.5,"paralysis stops movement");
p.condition=1U<<9U;angle=0;realm::move_player(world,p,{1,0},0,100,angle);near(p.position.x,2.5,"confusion x");near(p.position.y,2.1,"confusion y");
p.condition=0;p.position={2.5,2.5};angle=0;realm::move_player(world,p,{},1,100,angle);near(angle,.3,"camera rotation 3 radians per second");
world.tile(3,2)->type=255;p.position={2.5,2.5};
const auto missing=realm::modify_player_move(world,p,{3.1,2.5});near(missing.x,2.5,"unseen tile imposes half tile clearance");
world.tile(3,2)->type=grass->type;
const realm::ObjectDef* blocking{};for(const auto& d:catalog.objects())if(d.stationary&&d.occupy&&!d.full_occupy){blocking=&d;break;}
check(blocking,"partial blocking definition exists");world.add(100,blocking->type,{3.5,2.5});
auto& current=*world.entity(99);current.position={2.5,2.5};const auto stopped=realm::modify_player_move(world,current,{3.1,2.5});near(stopped.x,2.99,"blocked border leaves 0.01 clearance");
const auto slid=realm::modify_player_move(world,current,{3.1,2.8});check(slid.x<3 && slid.y>2.5,"wall collision slides");
check(!realm::valid_player_position(world,current,{3.2,2.5}),"missing tile blocks movement");
current.velocity={.01,0};current.target={3.5,2.5};world.update(20);near(current.position.x,2.5,"world interpolation excludes local player");
std::cout<<checks<<" player movement/combat checks passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
