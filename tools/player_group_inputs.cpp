#include "realm/player_group.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
try {
    if(argc!=5)throw std::runtime_error("Expected fonts, HUD art, cases and output");
    realm::Catalog catalog(realm::flash::source_root);realm::FontBook fonts(argv[1]);fonts.enable_native(realm::flash::source_root);realm::HudArt art(argv[2]);realm::AnimationLibrary animations(realm::flash::source_root);realm::Appearances appearances(realm::flash::source_root,animations);art.appearances(&appearances,&catalog);realm::PlayerGroup group(catalog,fonts,art);
    std::ifstream file(argv[3]);const auto cases=nlohmann::json::parse(file);const std::filesystem::path output(argv[4]);std::filesystem::create_directories(output);nlohmann::json metrics=nlohmann::json::array();
    for(const auto& c:cases) {
        fonts.set_pixel_scale(c.at("scale"));std::vector<realm::PlayerMenuTarget> players;
        for(const auto& row:c.at("players")) { realm::PlayerMenuTarget target;target.id=static_cast<int>(players.size())+1;target.type=row.at("type");target.skin=row.value("skin",0);target.tex1=row.value("tex1",0U);target.tex2=row.value("tex2",0U);target.name=row.at("name");target.level=row.at("level");target.fellow_guild=row.at("fellowGuild");players.push_back(target); }
        group.render_state(players,c.at("menu"),c.at("teleport"),c.at("hover"),c.at("cooldown"));
        const auto id=c.at("id").get<std::string>();realm::save_png(output/(id+".png"),group.image());
        metrics.push_back({{"id",id},{"width",group.frame_width()+1},{"height",group.frame_height()+1}});
    }
    std::ofstream(output/"metrics.json")<<metrics.dump()<<'\n';
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
