#include "realm/player_menu.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
// Renders PlayerMenu fixtures for side-by-side review against Flash captures.
int main(int argc,char** argv) {
try {
    if(argc<5||argc>6)throw std::runtime_error("Usage: realm_player_menu_inputs FONTS HUD_ART CASES OUTPUT [SCALE]");
    realm::Catalog catalog(realm::flash::source_root);realm::FontBook fonts(argv[1]);realm::HudArt art(argv[2]);
    if(argc==6) { fonts.enable_native(realm::flash::source_root);fonts.set_pixel_scale(std::stod(argv[5])); }
    realm::PlayerMenu menu(catalog,fonts,art);
    std::ifstream file(argv[3]);const auto cases=nlohmann::json::parse(file);const std::filesystem::path output(argv[4]);std::filesystem::create_directories(output);
    nlohmann::json layout=nlohmann::json::array();
    for(const auto& task:cases) {
        realm::PlayerMenuTarget target;target.id=2;target.type=task.at("type");target.name=task.at("name");target.level=task.at("level");target.fellow_guild=task.at("fellowGuild");
        for(const int option:task.at("options"))target.options.push_back(static_cast<realm::PlayerMenuAction>(option));
        menu.render_state(target,task.at("hover"),task.at("cooldown"));
        realm::save_png(output/(task.at("id").get<std::string>()+".png"),menu.image());
        layout.push_back({{"id",task.at("id")},{"width",menu.frame_width()+1},{"height",menu.frame_height()+1}});
    }
    std::ofstream(output/"layout.json")<<layout.dump()<<'\n';
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
