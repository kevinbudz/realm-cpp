#include "realm/world.hpp"
#include "realm/animation.hpp"
#include "realm/processed.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <functional>
int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("Usage: realm_bake_inputs OUTPUT_DIR MAP.jm|MAP.wmap|--all");
        const auto root = std::filesystem::path(realm::flash::source_root);
        const auto output = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(output / "raw");
        realm::Catalog catalog(root); realm::AnimationLibrary animation(root);
        std::vector<realm::Image> originals; for (const auto& asset : realm::flash::assets) originals.push_back(realm::load_png(root / asset.path));
        struct Entry { realm::Image image; int size{}; bool pad{}; };
        std::map<std::string, Entry> textures;
        std::function<void(const realm::TextureDef&, int, bool)> add_texture = [&](const auto& t, int size, bool pad) {
            for (const auto& variant : t.random) add_texture(variant, size, pad);
            for (const auto& [id, variant] : t.alternate) { (void)id; add_texture(variant, size, pad); }
            if (t.sheet >= 0) {
                const auto& sheet = realm::flash::sheets.at(static_cast<std::size_t>(t.sheet)); if (sheet.asset < 0) return;
                const auto& image = originals.at(static_cast<std::size_t>(sheet.asset)); const int cols = image.width / sheet.width;
                textures.try_emplace(realm::static_texture_key(t.sheet, t.index, size, pad), Entry{realm::crop(image, t.index % cols * sheet.width, t.index / cols * sheet.height, sheet.width, sheet.height), size, pad});
            }
            if (const auto* c = animation.character(t.animated, t.index))
                for (const auto& direction : c->directions) for (const auto& action : direction) for (const auto& frame : action)
                    textures.try_emplace(realm::animated_texture_key(frame.image, size, pad), Entry{animation.images().at(frame.image), size, pad});
        };
        if (std::string_view(argv[2]) == "--skins") {
            for(const auto& skin:catalog.skins())add_texture(skin.texture,100,false);
        } else if (std::string_view(argv[2]) == "--all") {
            for (const auto& object : catalog.objects()) {
                if (!object.draw_on_ground && object.model.empty()) { add_texture(object.texture, 100, false); add_texture(object.texture, object.min_size, false); }
            }
        } else {
            for (int i = 2; i < argc; ++i) {
                realm::World world(catalog); const std::filesystem::path map(argv[i]);
                if (map.extension() == ".wmap") world.load_wmap(map); else world.load_jm(map);
                for (const auto& entity : world.entities()) if (const auto* d = catalog.object(entity.type); d && !d->draw_on_ground && d->model.empty()) add_texture(d->texture, entity.size, false);
            }
        }
        if (std::string_view(argv[2]) != "--skins") {
        if (const auto* wizard = catalog.object("Wizard")) add_texture(wizard->texture, 100, false);
        // Projectiles can arrive from equipment and enemies outside the baked
        // map. Include their visual definitions and configured container sizes.
        for (const auto& container : catalog.objects()) for (const auto& projectile : container.projectiles)
            if (const auto* visual = catalog.object(projectile.object_id)) {
                add_texture(visual->texture, projectile.size >= 0 ? projectile.size : container.size, true);
            }
        }
        nlohmann::json tasks = nlohmann::json::array(), entries = nlohmann::json::array();
        for (const auto& [key, entry] : textures) {
            const auto raw = output / "raw" / (key + ".png"), final = output / (key + ".png"); realm::save_png(raw, entry.image);
            tasks.push_back({{"input", raw.string()}, {"output", final.string()}, {"size", entry.size}, {"padBottom", entry.pad}, {"glow", 0}});
            entries.push_back({{"key", key}, {"file", final.filename().string()}});
        }
        std::ofstream(output / "tasks.json") << tasks.dump(2);
        std::ofstream(output / "manifest.json") << nlohmann::json({{"schema", 1}, {"source_sha256", realm::flash::source_sha256}, {"images", entries}}).dump(2);
        std::cout << textures.size() << " reference texture inputs exported\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
