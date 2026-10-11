#include "realm/hud_art.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <limits>
namespace realm {
HudArt::HudArt(const std::filesystem::path& directory) {
    std::ifstream input(directory/"manifest.json");
    if(!input)throw std::runtime_error("Missing HUD art manifest");
    const auto manifest=nlohmann::json::parse(input);
    if(manifest.at("schema")!=1||manifest.at("source_sha256")!=flash::source_sha256)
        throw std::runtime_error("HUD art does not match Flash source; rebake assets");
    bundle_.open(directory/"images.bin",std::ios::binary);
    if(!bundle_)throw std::runtime_error("Missing HUD art bundle");
    const auto size=std::filesystem::file_size(directory/"images.bin");
    for(const auto& value:manifest.at("images")) {
        Record record{value.at("offset"),value.at("length")};
        if(record.offset>size||record.length>size-record.offset||record.length==0||record.length>4*1024*1024)
            throw std::runtime_error("Invalid HUD art bundle record");
        if(!records_.emplace(value.at("key").get<std::string>(),record).second)throw std::runtime_error("Duplicate HUD art record");
    }
}
const Image& HudArt::image(const std::string& key) const {
    if(const auto found=cache_.find(key);found!=cache_.end()) {
        lru_.splice(lru_.begin(),lru_,found->second.lru);return found->second.image;
    }
    const auto record=records_.find(key);
    if(record==records_.end())throw std::runtime_error("Missing HUD artwork: "+key+"; regenerate the HUD art with tools/bake_hud_art.py");
    std::vector<unsigned char> encoded(static_cast<std::size_t>(record->second.length));
    bundle_.clear();bundle_.seekg(static_cast<std::streamoff>(record->second.offset));
    if(!bundle_.read(reinterpret_cast<char*>(encoded.data()),static_cast<std::streamsize>(encoded.size())))throw std::runtime_error("Truncated HUD art bundle");
    auto decoded=load_png(encoded);premultiply_flash_bitmap(decoded);
    if(decoded.pixels.size()>4*1024*1024)throw std::runtime_error("HUD artwork exceeds decoded cache budget");
    while(!cache_.empty()&&(cache_.size()>=128||bytes_+decoded.pixels.size()>4*1024*1024)) {
        const auto found=cache_.find(lru_.back());bytes_-=found->second.image.pixels.size();cache_.erase(found);lru_.pop_back();++evictions_;
    }
    bytes_+=decoded.pixels.size();lru_.push_front(key);
    return cache_.emplace(key,Entry{std::move(decoded),lru_.begin()}).first->second.image;
}
}
