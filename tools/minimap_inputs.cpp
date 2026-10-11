#include "realm/minimap.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: realm_minimap_inputs TASKS.json");
        realm::Catalog catalog(realm::flash::source_root);realm::AnimationLibrary animations(realm::flash::source_root);
        realm::Minimap colors(catalog,animations,realm::flash::source_root);nlohmann::json metadata={{"grounds",nlohmann::json::object()},{"objects",nlohmann::json::object()}};
        for(const auto& def:catalog.grounds())metadata["grounds"][std::to_string(def.type)]=colors.ground_color(def.type);
        for(const auto& def:catalog.objects())metadata["objects"][std::to_string(def.type)]=colors.object_color(def.type);
        const auto directory=std::filesystem::path(argv[1]).parent_path()/"native";std::filesystem::create_directories(directory);std::ofstream(directory/"colors.json")<<metadata;
        std::ifstream input(argv[1]);const auto tasks=nlohmann::json::parse(input);
        for(const auto& task:tasks.at("cases")) {
            const auto& definition=tasks.contains("maps") ? tasks.at("maps").at(task.at("map").get<std::size_t>()) : task;
            realm::World world(catalog);world.initialize(definition.at("width"),definition.at("height"));realm::net::Update update;
            for(const auto& cell:definition.at("tiles"))update.tiles.push_back({cell.at("x"),cell.at("y"),cell.at("type")});
            world.apply(update);
            realm::Minimap mini(catalog,animations,realm::flash::source_root);
            realm::MinimapView view{{task.at("focus").at(0),task.at("focus").at(1)},task.at("angle"),{}};
            auto& focus=world.add(-2,catalog.object("Wizard")->type,view.focus);world.set_player_id(-2);focus.guild="fixture";
            for(const auto& marker:task.at("markers")) { auto& object=world.add(marker.at("id"),marker.at("type"),{marker.at("x"),marker.at("y")});if(marker.value("fellow",false))object.guild="fixture"; }
            // First establish the source zoom levels, then apply the fixture.
            mini.update(world,*world.entity(-2),view.angle);for(int z=0;z<task.at("zoom").get<int>();++z)mini.zoom_out();
            mini.update(world,*world.entity(-2),view.angle);
            const auto rows=mini.colored_rows(),draws=mini.redraws();if(mini.update(world,*world.entity(-2),view.angle)||mini.colored_rows()!=rows||mini.redraws()!=draws)throw std::runtime_error("Unchanged minimap rebuilt");
            realm::save_png(directory/(task.at("id").get<std::string>()+".png"),mini.image());
        }
        std::cout<<tasks.at("cases").size()<<" minimaps rendered; unchanged views reused\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
