#include "realm/hud.hpp"
#include "hud_fixture.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Usage: realm_hud_inputs FONT_DIRECTORY TASKS.json");
        std::ifstream input(argv[2]);const auto tasks=nlohmann::json::parse(input);
        realm::FontBook fonts(argv[1]);realm::Catalog catalog(realm::flash::source_root);
        std::unique_ptr<realm::HudArt> art;if(tasks.contains("art"))art=std::make_unique<realm::HudArt>(tasks.at("art").get<std::string>());
        realm::Hud hud(fonts,art.get());
        const auto output=std::filesystem::path(argv[2]).parent_path()/"native";
        for(const auto& data:tasks.at("cases")) {
            auto state=hud_fixture(data,catalog);
            const bool changed=hud.update(state);
            if(data.contains("redraw")&&changed!=data.at("redraw").get<bool>())throw std::runtime_error("Unexpected HUD redraw at "+data.at("id").get<std::string>());
            if(data.contains("regions")) {
                auto regions=nlohmann::json::array();
                for(const auto& area:hud.changes())regions.push_back({area.x,area.y,area.width,area.height});
                if(regions!=data.at("regions"))throw std::runtime_error("Unexpected HUD upload regions at "+data.at("id").get<std::string>());
            }
            realm::Hud fresh(fonts,art.get());fresh.update(state);
            if(fresh.image().pixels!=hud.image().pixels) { realm::save_png(output/"incremental-failure.png",hud.image());realm::save_png(output/"fresh-failure.png",fresh.image());throw std::runtime_error("Incremental HUD differs from fresh reconstruction at "+data.at("id").get<std::string>()); }
            const auto rebuilt=hud.rebuilds();
            if(hud.update(state)||hud.rebuilds()!=rebuilt)throw std::runtime_error("Unchanged HUD state caused a redraw");
            realm::save_png(output/(data.at("id").get<std::string>()+".png"),hud.image());
        }
        if(art&&tasks.value("stress",false)) {
            std::ifstream manifest(std::filesystem::path(tasks.at("art").get<std::string>())/"manifest.json");
            const auto records=nlohmann::json::parse(manifest);
            for(const auto& entry:records.at("images")) { (void)art->image(entry.at("key"));if(art->cache_entries()>128||art->cache_bytes()>4*1024*1024)throw std::runtime_error("HUD art cache exceeded budget"); }
            realm::Hud fresh(fonts,art.get());const auto state=hud_fixture(tasks.at("cases").back(),catalog);fresh.update(state);
            if(fresh.image().pixels!=hud.image().pixels)throw std::runtime_error("HUD cache eviction changed pixels");
            if(art->evictions()==0)throw std::runtime_error("HUD art stress did not evict");
            std::cout<<art->cache_entries()<<" cached UI records / "<<art->cache_bytes()<<" bytes / "<<art->evictions()<<" evictions\n";
        }
        std::cout<<hud.rebuilds()<<" changed panels rendered; unchanged panels reused\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
