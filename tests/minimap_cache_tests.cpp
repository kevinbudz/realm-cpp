#include "realm/minimap.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace { int checks{};void check(bool ok,const char* reason) { ++checks;if(!ok)throw std::runtime_error(reason); } }
int main() {
    try {
        realm::Catalog catalog(realm::flash::source_root);realm::AnimationLibrary animations(realm::flash::source_root);
        realm::World world(catalog);world.initialize(100,100);realm::Minimap mini(catalog,animations,realm::flash::source_root);
        const auto* grass=catalog.ground("Grass");const auto* wizard=catalog.object("Wizard");
        const realm::ObjectDef* wall{};for(const auto& def:catalog.objects())if(def.class_name=="Wall"&&def.stationary&&def.occupy&&!def.no_minimap) { wall=&def;break; }
        check(wall!=nullptr,"static wall fixture missing");
        realm::net::Update update;update.tiles.push_back({3,7,grass->type});world.apply(update);
        world.add(-2,wizard->type,{50.5,50.5});world.set_player_id(-2);
        check(world.tile(3,7)->minimap_code==(0x20000U|grass->type),"received ground must be remembered");
        check(world.tile(0,0)->minimap_code==0,"unseen tiles must remain black");
        check(mini.update(world,*world.entity(-2),0),"first minimap view must render");
        check(mini.colored_rows()==100,"initial map must color all rows once");
        const auto rows=mini.colored_rows(),draws=mini.redraws();
        check(!mini.update(world,*world.entity(-2),0),"unchanged minimap redrew");
        check(mini.colored_rows()==rows&&mini.redraws()==draws,"unchanged minimap performed hidden work");
        world.apply(update);check(!mini.update(world,*world.entity(-2),0),"duplicate ground update redrew");
        update.tiles[0].type=catalog.grounds().front().type;world.apply(update);
        check(mini.update(world,*world.entity(-2),0),"ground change did not invalidate view");
        check(mini.colored_rows()==rows+1,"ground change recolored unrelated rows");
        world.add(11,wall->type,{3.5,7.5});const auto remembered=world.tile(3,7)->minimap_code;
        check(remembered==(0x30000U|wall->type),"static occupying object must replace ground memory");
        mini.update(world,*world.entity(-2),0);const auto before_drop=mini.colored_rows();world.remove(11);
        check(world.tile(3,7)->minimap_code==remembered,"dropping an object erased fog memory");
        check(!mini.update(world,*world.entity(-2),0)&&mini.colored_rows()==before_drop,"object drop repainted remembered map");
        world.apply(update);check(world.tile(3,7)->minimap_code==(0x20000U|update.tiles[0].type),"new ground must overwrite object memory");
        mini.update(world,*world.entity(-2),0);const auto before_move=mini.colored_rows();world.goto_entity(-2,{51,50.5});
        check(mini.update(world,*world.entity(-2),0)&&mini.colored_rows()==before_move,"focus movement recolored terrain");
        check(mini.zoom_levels()==std::vector<double>({4,2,1.92}),"minimap zoom levels differ from source");
        mini.zoom_out();mini.zoom_out();mini.zoom_out();check(mini.zoom_index()==2,"zoom out did not clamp");
        mini.zoom_in();mini.zoom_in();mini.zoom_in();check(mini.zoom_index()==0,"zoom in did not clamp");
        world.initialize(100,100);world.add(-2,wizard->type,{50.5,50.5});
        const auto reset_rows=mini.colored_rows();mini.update(world,*world.entity(-2),0);
        check(mini.colored_rows()==reset_rows+100,"same-size map transition did not reset memory");
        check(world.tile(3,7)->minimap_code==0,"map transition retained explored pixels");
        bool rejected{};try { mini.render(world,{{0,0},std::numeric_limits<double>::quiet_NaN(),{}}); }catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"invalid camera angle accepted");
        std::cout<<checks<<" minimap fog, cache and zoom checks passed\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
