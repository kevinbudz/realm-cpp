#include "realm/player_tooltip.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
try {
    if(argc!=5)throw std::runtime_error("Usage: realm_player_tooltip_inputs FONTS HUD_ART TASKS OUTPUT");
    realm::Catalog catalog(realm::flash::source_root);realm::FontBook fonts(argv[1]);realm::HudArt art(argv[2]);realm::PlayerTooltip tooltip(catalog,fonts,art);
    std::ifstream file(argv[3]);const auto tasks=nlohmann::json::parse(file);const std::filesystem::path output(argv[4]);std::filesystem::create_directories(output);
    nlohmann::json sizes=nlohmann::json::array();
    for(const auto& task:tasks.at("cases")) {
        realm::World world(catalog);world.initialize(1,1);world.add(1,task.at("type"),{});world.set_player_id(1);world.add(2,task.at("type"),{});
        auto& player=*world.entity(2);player.name=task.at("name");player.guild=task.at("guild");if(task.at("fellowGuild").get<bool>())world.entity(1)->guild=player.guild;
        player.hp=task.at("hp");player.max_hp=task.at("maxHp");player.stats[3]=task.at("maxMp");player.stats[4]=task.at("mp");player.stats[7]=task.at("level");player.stats[27]=task.at("stars");player.stats[57]=task.at("guildRank");player.stats[43]=task.at("hpBoost");player.stats[44]=task.at("mpBoost");
        for(int i=0;i<4;++i)realm::set_inventory_item(player,i,{task.at("items").at(i),task.at("datas").at(i)});
        auto target=realm::player_tooltip_target(world,2);if(!target)throw std::runtime_error("Invalid player tooltip fixture");
        tooltip.update(target);realm::save_png(output/(task.at("id").get<std::string>()+".png"),tooltip.image());
        const auto& hp=target->bars[0];const int value=hp.maximum>0 ? std::clamp(hp.value,0,hp.maximum) : hp.value;
        const auto amount=std::to_string(value)+(hp.maximum>0 ? "/"+std::to_string(hp.maximum) : "");
        const auto color=hp.cap>0&&static_cast<std::int64_t>(hp.maximum)-hp.boost==hp.cap ? 16572160U : hp.boost>0 ? 6206769U : 0xffffffU;
        const auto boost=hp.boost ? " ("+std::string(hp.boost>0 ? "+" : "")+std::to_string(hp.boost)+")" : "";
        const double x=88-(fonts.layout(amount,14,true).width+(boost.empty() ? 0 : fonts.layout(boost,14,true).width))/2;
        const auto bitmap=fonts.rasterize(amount,14,true,true,color,x-std::floor(x));
        realm::Image field{100,40,std::vector<unsigned char>(100*40*4)};
        for(int y=0;y<bitmap.image.height;++y)for(int c=0;c<bitmap.image.width;++c)if(y+8+bitmap.offset.y>=0&&y+8+bitmap.offset.y<40&&c+8+bitmap.offset.x>=0&&c+8+bitmap.offset.x<100) {
            const auto to=static_cast<std::size_t>((y+8+static_cast<int>(bitmap.offset.y))*100+c+8+static_cast<int>(bitmap.offset.x))*4;
            std::copy_n(bitmap.image.pixels.begin()+static_cast<std::ptrdiff_t>(y*bitmap.image.width+c)*4,4,field.pixels.begin()+static_cast<std::ptrdiff_t>(to));
        }
        realm::save_png(output/(task.at("id").get<std::string>()+"-hp-value.png"),field);
        sizes.push_back({{"id",task.at("id")},{"width",tooltip.draw_width()},{"height",tooltip.height()}});
    }
    std::ofstream(output/"sizes.json")<<sizes.dump();
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
