#include "realm/player_menu.hpp"
#include "flash_assets.hpp"
#include <iostream>

namespace {
int checks{};
void check(bool value,const char* reason) { ++checks;if(!value)throw std::runtime_error(reason); }
}
int main(int argc,char** argv) {
try {
    using namespace realm;
    using A=PlayerMenuAction;
    using S=flash::StatId;
    const auto stat=[](S id) { return static_cast<std::uint8_t>(id); };
    Catalog catalog(flash::source_root);World world(catalog);world.initialize(100,100);Party party;
    const auto type=[&](std::string_view name) { return catalog.object(name)->type; };
    world.add(99,type("Wizard"),{3,3});world.set_player_id(99);
    world.add(2,type("Knight"),{4,3});
    auto* member=world.entity(2);member->name="Tank";member->stats[stat(S::LEVEL_STAT)]=20;member->stats[stat(S::ACCOUNT_ID_STAT)]=500;
    const auto options=[&](bool teleport) { return player_menu_target(world,party,2,teleport)->options; };
    check(options(true)==std::vector{A::teleport,A::lock,A::trade,A::ignore},"default options follow PlayerMenu order");
    check(options(false)==std::vector{A::lock,A::trade,A::ignore},"maps without allowPlayerTeleport omit Teleport");
    member->condition=invisible_condition;
    check(options(true).front()==A::lock,"invisible players are not teleport eligible");member->condition=0;
    world.entity(99)->stats[stat(S::GUILD_RANK_STAT)]=20;
    check(options(false)==std::vector{A::invite,A::lock,A::trade,A::ignore},"officers can invite unguilded players");
    member->guild="Other";check(options(false).front()==A::lock,"guilded players cannot be invited");member->guild.clear();
    world.entity(99)->stats[stat(S::GUILD_RANK_STAT)]=10;check(options(false).front()==A::lock,"members below officer cannot invite");
    party.edit(500,2,false,true);party.update(world,0);
    check(party.starred(2)&&options(false).front()==A::unlock,"locking stars the account and offers Unlock");
    party.edit(500,2,false,false);party.update(world,1);
    check(!party.starred(2)&&options(false).front()==A::lock,"unlocking clears the star immediately");
    party.edit(500,2,true,true);
    check(party.ignored(500)&&options(false)==std::vector{A::lock,A::unignore},"ignored players offer only Unignore");
    party.edit(500,2,true,false);check(!party.ignored(500),"unignore removes the account");
    world.entity(99)->guild="Native Guild";member->guild="Native Guild";
    check(player_menu_target(world,party,2,false)->fellow_guild,"fellow guild header color");
    check(!player_menu_target(world,party,99,true)&&!player_menu_target(world,party,12345,true),"self and unknown objects have no menu");
    world.add(88,type("Loot Bag"),{10,10});check(!player_menu_target(world,party,88,true),"non-players have no menu");
    if(argc==3) {
        FontBook fonts(argv[1]);HudArt art(argv[2]);PlayerMenu menu(catalog,fonts,art);
        auto target=*player_menu_target(world,party,2,true);
        check(menu.open(target,{100,100},800,600,1),"menu opens for a player");
        // Left half / top third: right of and below the pointer.
        check(menu.canvas_position().x==112-PlayerMenu::origin&&menu.canvas_position().y==112-PlayerMenu::origin,"menu opens 12 pixels from the pointer");
        check(menu.frame_width()==154&&menu.frame_height()==40+28*4+16,"frame keeps the 154-pixel minimum and 28-pixel rows");
        // Rows overlap; the later option owns the overlap.
        const auto row=[&](int index,int dy=0) { return Vec2{112+30,112+40+28*index+dy}; };
        menu.update(world,{90,90},true,0,1);
        check(menu.visible(),"pointer within 40 pixels keeps the menu");
        menu.update(world,row(1),true,0,1);check(menu.hover()==1,"hover follows rows");
        menu.update(world,row(0,20),true,0,1);check(menu.hover()==1,"the next row owns the overlap");
        check(menu.pointer_down(row(3))&&!menu.pointer_up(row(2)),"release on another option does not click");
        check(menu.pointer_down(row(2)),"press inside the menu is consumed");
        const auto request=menu.pointer_up(row(2));
        check(request&&request->action==A::trade&&request->name=="Tank"&&request->object_id==2&&!menu.visible(),"click emits the option and closes");
        menu.open(target,{100,100},800,600,1);menu.update(world,row(0),true,0,1);
        menu.update(world,{90,90},true,0,1);check(!menu.visible(),"rolling out after entering closes the menu");
        menu.open(target,{100,100},800,600,1);menu.update(world,{40,40},true,0,1);check(!menu.visible(),"pointer over 40 pixels away closes the menu");
        menu.open(target,{700,500},800,600,1);
        check(menu.canvas_position().x+PlayerMenu::origin==700-155-1&&menu.canvas_position().y+PlayerMenu::origin==500-169-1,"right/bottom pointers open the menu left and above");
        menu.open(target,{700,100},800,600,2);check(menu.canvas_position().x+PlayerMenu::origin*2==700-155*2-1,"menu size scales with the window height");
        menu.open(target,{5,5},800,600,1);menu.update(world,{30,30},true,4000,1);
        const auto before=menu.rebuilds();menu.update(world,{30,30},true,4000,1);
        check(menu.rebuilds()==before,"unchanged state does not redraw");
        menu.update(world,{30,30},true,2000,1);check(menu.rebuilds()==before+1,"cooldown progress redraws");
        world.remove(2);menu.update(world,{30,30},true,0,1);check(!menu.visible(),"menu closes when its player leaves");
    }
    std::cout<<"Player menu checks: "<<checks<<'\n';
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
