#include "realm/terrain.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <unordered_set>
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4) throw std::runtime_error("Usage: realm_terrain_inputs MAP.jm OUTPUT_DIR");
        const auto output = std::filesystem::absolute(argv[2]); std::filesystem::create_directories(output / "native");
        realm::Catalog catalog(realm::flash::source_root); realm::World world(catalog); world.load_jm(argv[1]);
        realm::Terrain terrain(world, realm::flash::source_root, 0, argc == 4 ? std::stoi(argv[3]) : -1);
        nlohmann::json tasks = nlohmann::json::array(); std::unordered_set<std::size_t> visited;
        for (int y = 0; y < world.height(); ++y) for (int x = 0; x < world.width(); ++x) {
            if (world.tile(x,y)->type == 255) continue;
            const auto& visual = terrain.tile(x,y);
            const auto* ground = catalog.ground(world.tile(x,y)->type);
            auto add = [&](std::size_t index, const char* mode) {
                if (!visited.insert(index).second) return;
                const auto name = std::to_string(index) + ".png";
                realm::save_png(output / "native" / name, terrain.images().at(index));
                tasks.push_back({{"x",x},{"y",y},{"mode",mode},{"output",(output/"flash"/name).string()}});
            };
            add(visual.image, ground && ground->animate ? "base" : "composed");
            if (visual.overlay >= 0) add(static_cast<std::size_t>(visual.overlay), "overlay");
        }
        std::ofstream(output / "tasks.json") << tasks.dump(2);
        std::cout << tasks.size() << " terrain fixtures exported\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
