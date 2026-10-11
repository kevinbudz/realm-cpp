#include "realm/tooltip.hpp"
#include "realm/item_data.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <iostream>

namespace {
int checks{};
void check(bool ok,const char* why) { ++checks;if(!ok)throw std::runtime_error(why); }
bool contains(const realm::ItemTooltipContent& content,std::string_view text) {
    return std::any_of(content.lines.begin(),content.lines.end(),[&](const auto& line) { return line.text==text; });
}
}
int main(int argc,char** argv) {
try {
    realm::Catalog catalog(realm::flash::source_root);realm::World world(catalog);world.initialize(8,8);
    world.add(99,catalog.object("Wizard")->type,{2.5,3.5});world.set_player_id(99);
    const auto type=[&](std::string_view name) { const auto* item=catalog.object(name);if(!item)throw std::runtime_error("Missing fixture "+std::string(name));return item->type; };
    auto& player=*world.entity(99);
    realm::set_inventory_item(player,0,{type("Energy Staff"),-1});
    realm::set_inventory_item(player,2,{type("Robe of the Neophyte"),-1});
    realm::set_inventory_item(player,4,{type("Staff of the Cosmic Whole"),128|static_cast<int>(realm::item_data::damage|realm::item_data::rate_of_fire)});
    realm::set_inventory_item(player,5,{type("Robe of the Grand Sorcerer"),-1});
    realm::set_inventory_item(player,12,{type("Prism of Apparitions"),-1});player.stats[69]=1;
    player.stats[25]=7;player.stats[23]=8;player.stats[24]=9;
    auto hud=realm::HudState::player(world,player);
    auto target=realm::item_tooltip_target(world,hud,{34,400});
    check(target&&target->item.type==type("Staff of the Cosmic Whole")&&target->equipped.type==type("Energy Staff"),"inventory hover selects item and matching equipped weapon");
    check(target->stats[5]==7&&target->stats[6]==8&&target->stats[7]==9,"requirement values use wire DEX/VIT/WIS IDs rather than XML IDs");
    const auto staff=realm::item_tooltip_content(catalog,*target);
    check(contains(staff,"Damage: 84 - 140 (+40%)")&&contains(staff,"Range: 8.55")&&contains(staff,"Rate of fire: 140%"),"tooltip reflects actual weapon modifiers and range");
    check(contains(staff,"+94.5 average damage per shot")&&contains(staff,"+40 % rate of fire"),"weapon comparison includes modifier changes");
    check(contains(staff,"Double-click to equip")&&contains(staff,"Ctrl-click to transfer to backpack / inventory"),"inventory offers supported controls");
    target=realm::item_tooltip_target(world,hud,{78,400});const auto robe=realm::item_tooltip_content(catalog,*target);
    check(contains(robe,"On equip: +6 WIS")&&contains(robe,"+11 DEF")&&contains(robe,"+55 Max MP"),"armor effects and comparisons use XML stat IDs");
    hud.inventory.selected=1;
    check(!realm::item_tooltip_target(world,hud,{34,400}),"stats body has no item hover");
    target=realm::item_tooltip_target(world,hud,{34,324});check(target&&target->equipped.type==-1,"equipped item remains available on stats tab without self comparison");
    hud.inventory.selected=2;target=realm::item_tooltip_target(world,hud,{34,400});
    check(target&&target->item.type==type("Prism of Apparitions")&&!target->usable,"backpack resolves wrong-class items");
    auto prism=realm::item_tooltip_content(catalog,*target);check(contains(prism,"Not usable by Wizard")&&contains(prism,"On equip: +2 VIT")&&contains(prism,"On equip: +2 WIS"),"wrong-class warning and XML VIT/WIS mapping");
    hud.inventory.dragging=12;check(!realm::item_tooltip_target(world,hud,{34,400}),"drag suppresses item tooltip");
    hud.inventory.dragging=-1;hud.inventory.selected=0;
    check(!realm::item_tooltip_target(world,hud,{78,324})&&!realm::item_tooltip_target(world,hud,{55,400})&&!realm::item_tooltip_target(world,hud,{-20,400}),"empty slots, grid gaps and playfield hide tooltip");
    target=realm::item_tooltip_target(world,hud,{30,458});check(target&&target->owner==realm::TooltipOwner::potion,"potion artwork takes precedence over inventory underneath");
    check(contains(realm::item_tooltip_content(catalog,*target),"Use: +100 HP"),"potion tooltip reports restore amount");
    world.add(77,type("Loot Bag"),{2.5,3.9});realm::set_inventory_item(*world.entity(77),0,{type("Potion of Dexterity"),-1});
    hud=realm::HudState::player(world,*world.entity(99));target=realm::item_tooltip_target(world,hud,{34,528});
    check(target&&target->owner==realm::TooltipOwner::container&&contains(realm::item_tooltip_content(catalog,*target),"Permanent: +1 DEX"),"loot container resolves item and correct permanent stat");
    world.remove(77);check(!realm::item_tooltip_target(world,hud,{34,528}),"removed container cannot leave a stale item tooltip");
    const auto merchant=std::find_if(catalog.objects().begin(),catalog.objects().end(),[](const auto& item) { return item.class_name=="Merchant"; });
    check(merchant!=catalog.objects().end(),"merchant fixture exists");world.add(78,merchant->type,{2.5,3.9});world.entity(78)->stats[31]=type("Robe of the Master");
    hud=realm::HudState::player(world,*world.entity(99));target=realm::item_tooltip_target(world,hud,{24,520});
    check(target&&target->owner==realm::TooltipOwner::merchant&&target->item.data==-1&&contains(realm::item_tooltip_content(catalog,*target),"Use the purchase button to buy"),"merchant icon shows current merchandise without fictitious modifiers");
    world.entity(78)->stats[31]=type("Energy Staff");target=realm::item_tooltip_target(world,hud,{24,520});check(target&&target->item.type==type("Energy Staff"),"merchant stock changes refresh tooltip");
    check(!realm::item_tooltip_target(world,hud,{100,575}),"purchase button does not trigger item hover");
    realm::ItemTooltipTarget fixture;fixture.player_type=type("Wizard");fixture.usable=true;
    for(const auto& item:catalog.objects())if(item.slot_type>=0) {
        fixture.item={item.type,-1};const auto content=realm::item_tooltip_content(catalog,fixture);
        check(!content.title.empty(),"every imported item has tooltip content");
    }
    for(const auto& dimensions:std::array<realm::Vec2,3>{{{800,600},{1280,720},{320,240}}})for(const auto pointer:std::array<realm::Vec2,4>{{{0,0},{dimensions.x,0},{0,dimensions.y},{dimensions.x,dimensions.y}}}) {
        const double scale=dimensions.y/600;const auto at=realm::ItemTooltip::position(pointer,static_cast<int>(dimensions.x),static_cast<int>(dimensions.y),580,scale);
        check(at.x>=0&&at.y>=0&&at.x+256*scale<=dimensions.x&&at.y+580*scale<=dimensions.y,"tooltip stays within window at all corners and sizes");
    }
    if(argc==4) {
        realm::FontBook fonts(argv[1]);realm::HudArt art(argv[2]);realm::ItemTooltip tooltip(catalog,fonts,art);
        const std::filesystem::path output(argv[3]);std::filesystem::create_directories(output);
        fixture.item={type("Staff of the Cosmic Whole"),128|static_cast<int>(realm::item_data::damage|realm::item_data::rate_of_fire)};fixture.equipped={type("Energy Staff"),-1};fixture.slot=4;fixture.backpack=true;
        check(tooltip.update(fixture)&&tooltip.visible(),"tooltip builds on first hover");const auto rebuilds=tooltip.rebuilds();
        for(int i=0;i<1000;++i)check(!tooltip.update(fixture),"unchanged hover reuses bitmap");
        check(tooltip.rebuilds()==rebuilds,"stationary hover performs no new rasterization");
        realm::save_png(output/"staff.png",tooltip.image());
        fixture.item.data=-1;check(tooltip.update(fixture),"item data changes invalidate tooltip");
        fixture.equipped.data=1|(1<<17);check(tooltip.update(fixture),"equipped item data changes invalidate comparison");
        check(!tooltip.update(std::nullopt)&&!tooltip.visible(),"hover exit hides tooltip");
        std::size_t rendered{};int tallest{};realm::ItemTooltipTarget longest;
        for(const auto& item:catalog.objects())if(item.slot_type>=0) {
            fixture.item={item.type,-1};fixture.equipped={};fixture.usable=true;fixture.slot=4;
            check(tooltip.update(fixture),"all imported items rasterize");++rendered;
            check(tooltip.height()>0&&tooltip.height()<=580,"viewport stays bounded");
            if(tooltip.content_height()>tallest) { tallest=tooltip.content_height();longest=fixture; }
            if(item.id=="Robe of the Grand Sorcerer"||item.id=="Health Potion"||item.id=="Cloak of the Planewalker")realm::save_png(output/(item.id+".png"),tooltip.image());
        }
        longest.usable=false;longest.equipped={longest.item.type,128|0xff00};
        check(tooltip.update(longest),"restricted appearance and expanded comparison render");realm::save_png(output/"long-top.png",tooltip.image());
        check(tooltip.content_height()>580,"expanded comparison exercises scrolling");
        check(tooltip.scroll(-1000000),"long tooltip scrolls to bottom without overflow");realm::save_png(output/"long-bottom.png",tooltip.image());
        check(!tooltip.scroll(-1),"scroll stops at content end");check(tooltip.scroll(1000000),"long tooltip scrolls back to top");
        check(!tooltip.update(std::nullopt)&&!tooltip.visible()&&tooltip.update(longest),"hover reentry restores tooltip at top");
        std::cout<<rendered<<" imported items rasterized; tallest content "<<tallest<<" pixels; captures in "<<output<<'\n';
    }
    std::cout<<checks<<" tooltip content, hover, comparison and cache checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
