#include "realm/processed.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
namespace realm {
std::string static_texture_key(int sheet, int index, int size, bool pad) { return "s" + std::to_string(sheet) + "i" + std::to_string(index) + "z" + std::to_string(size) + (pad ? "p" : ""); }
std::string animated_texture_key(std::size_t frame, int size, bool pad) { return "a" + std::to_string(frame) + "z" + std::to_string(size) + (pad ? "p" : ""); }
ProcessedTextures::ProcessedTextures(const std::filesystem::path& directory, std::size_t first) {
    if (directory.empty()) return;
    append(directory,first);
    if(std::filesystem::exists(directory.parent_path()/"baked-skins/manifest.json")&&directory.filename()!="baked-skins")append(directory.parent_path()/"baked-skins",first);
}
void ProcessedTextures::append(const std::filesystem::path& directory,std::size_t first) {
    std::ifstream input(directory / "manifest.json");
    if (!input) throw std::runtime_error("Missing processed texture manifest");
    const auto manifest = nlohmann::json::parse(input);
    if (manifest.at("schema") != 1 || manifest.at("source_sha256") != flash::source_sha256)
        throw std::runtime_error("Processed textures do not match current Flash source; rebake assets");
    std::unordered_set<std::string> seen;
    for (const auto& entry : manifest.at("images")) {
        const auto filename = entry.at("file").get<std::string>();
        if (std::filesystem::path(filename).is_absolute() || filename.find("..") != std::string::npos) throw std::runtime_error("Invalid processed texture path");
        const auto key = entry.at("key").get<std::string>();
        if(!seen.insert(key).second)throw std::runtime_error("Duplicate processed texture key");
        if(lookup_.contains(key))continue;
        lookup_.emplace(key,first+images_.size());
        images_.push_back(load_png(directory / filename));
    }
}
std::size_t ProcessedTextures::find(const std::string& key) const {
    const auto it = lookup_.find(key);
    if (it == lookup_.end()) throw std::runtime_error("Missing processed texture " + key + "; rebake assets for this map");
    return it->second;
}
}
