#include "realm/hud_art.hpp"
#include "flash_assets.hpp"
#include <nlohmann/json.hpp>
#include <limits>
#include <charconv>
namespace realm {
HudArt::HudArt(const std::filesystem::path& directory) {
    std::ifstream input(directory/"manifest.json");
    if(!input)throw std::runtime_error("Missing HUD art manifest");
    const auto manifest=nlohmann::json::parse(input);
    if(manifest.at("schema")!=1||manifest.at("source_sha256")!=flash::source_sha256)
        throw std::runtime_error("HUD art does not match Flash source; rebake assets");
    bundle_.open(directory/"images.bin",std::ios::binary);
    if(!bundle_)throw std::runtime_error("Missing HUD art bundle");
    if(std::filesystem::exists(directory/"skins/manifest.json"))skins_=std::make_unique<HudArt>(directory/"skins");
    const auto size=std::filesystem::file_size(directory/"images.bin");
    for(const auto& value:manifest.at("images")) {
        Record record{value.at("offset"),value.at("length")};
        if(record.offset>size||record.length>size-record.offset||record.length==0||record.length>4*1024*1024)
            throw std::runtime_error("Invalid HUD art bundle record");
        if(!records_.emplace(value.at("key").get<std::string>(),record).second)throw std::runtime_error("Duplicate HUD art record");
    }
}
const ObjectDef* HudArt::dynamic_item(const std::string& key) const {
    if(!catalog_||!appearances_)return nullptr;
    const std::size_t prefix=key.starts_with("item") ? 4 : key.starts_with("dim") ? 3 : 0;
    if(!prefix)return nullptr;
    int type{};const auto parsed=std::from_chars(key.data()+prefix,key.data()+key.size(),type);
    return parsed.ec==std::errc{}&&parsed.ptr==key.data()+key.size()&&type>0&&type<=65535 ? catalog_->object(static_cast<std::uint16_t>(type)) : nullptr;
}
bool HudArt::contains(const std::string& key) const { return records_.contains(key)||(skins_&&skins_->contains(key))||dynamic_item(key); }
Image HudArt::portrait(int type,int skin,std::uint32_t tex1,std::uint32_t tex2) const {
    if(appearances_&&catalog_&&(tex1||tex2)) { auto value=*appearances_->portrait(*catalog_,type,skin,tex1,tex2);premultiply_flash_bitmap(value);return value; }
    return image("portrait"+std::to_string(catalog_ ? catalog_->portrait_type(type,skin) : (skin ? skin : type)));
}
const Image& HudArt::image(const std::string& key) const {
    if(const auto found=cache_.find(key);found!=cache_.end()) {
        lru_.splice(lru_.begin(),lru_,found->second.lru);return found->second.image;
    }
    if(!records_.contains(key)&&skins_&&skins_->contains(key))return skins_->image(key);
    Image decoded;
    if(const auto* item=dynamic_item(key);item&&!records_.contains(key)) {
        decoded=*appearances_->texture(item->texture,0,80,true,static_cast<std::uint32_t>(item->xml.number("Tex1")),static_cast<std::uint32_t>(item->xml.number("Tex2")));
        premultiply_flash_bitmap(decoded);
        if(key.starts_with("dim"))for(std::size_t i=0;i<decoded.pixels.size();i+=4)for(int c=0;c<3;++c)decoded.pixels[i+static_cast<std::size_t>(c)]=static_cast<unsigned char>(decoded.pixels[i+static_cast<std::size_t>(c)]*.4);
    } else {
        const auto record=records_.find(key);
        if(record==records_.end())throw std::runtime_error("Missing HUD artwork: "+key+"; regenerate the HUD art with tools/bake_hud_art.py");
        std::vector<unsigned char> encoded(static_cast<std::size_t>(record->second.length));
        bundle_.clear();bundle_.seekg(static_cast<std::streamoff>(record->second.offset));
        if(!bundle_.read(reinterpret_cast<char*>(encoded.data()),static_cast<std::streamsize>(encoded.size())))throw std::runtime_error("Truncated HUD art bundle");
        decoded=load_png(encoded);premultiply_flash_bitmap(decoded);
    }
    if(decoded.pixels.size()>4*1024*1024)throw std::runtime_error("HUD artwork exceeds decoded cache budget");
    while(!cache_.empty()&&(cache_.size()>=128||bytes_+decoded.pixels.size()>4*1024*1024)) {
        const auto found=cache_.find(lru_.back());bytes_-=found->second.image.pixels.size();cache_.erase(found);lru_.pop_back();++evictions_;
    }
    bytes_+=decoded.pixels.size();lru_.push_front(key);
    return cache_.emplace(key,Entry{std::move(decoded),lru_.begin()}).first->second.image;
}
}
