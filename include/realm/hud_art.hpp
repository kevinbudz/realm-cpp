#pragma once
#include "realm/image.hpp"
#include <fstream>
#include "realm/appearance.hpp"
#include <memory>
#include <list>
#include <unordered_map>
namespace realm {
// Original finite UI decorations and filtered icons, baked offline. Runtime
// decodes only requested PNG records and retains at most 128 / 4 MiB of pixels per pack (256 / 8 MiB with skins).
class HudArt {
public:
    explicit HudArt(const std::filesystem::path& directory);
    void appearances(const Appearances* value,const Catalog* catalog) { appearances_=value;catalog_=catalog; }
    [[nodiscard]] Image portrait(int type,int skin,std::uint32_t tex1,std::uint32_t tex2) const;
    [[nodiscard]] const Image& image(const std::string& key) const;
    [[nodiscard]] bool contains(const std::string& key) const;
    [[nodiscard]] std::size_t cache_entries() const { return cache_.size()+(skins_ ? skins_->cache_entries() : 0); }
    [[nodiscard]] std::size_t cache_bytes() const { return bytes_+(skins_ ? skins_->cache_bytes() : 0); }
    [[nodiscard]] std::uint64_t evictions() const { return evictions_+(skins_ ? skins_->evictions() : 0); }
private:
    const ObjectDef* dynamic_item(const std::string& key) const;
    const Appearances* appearances_{};
    const Catalog* catalog_{};
    struct Record { std::uint64_t offset{},length{}; };
    struct Entry { Image image; std::list<std::string>::iterator lru; };
    std::unique_ptr<HudArt> skins_;
    std::unordered_map<std::string,Record> records_;
    mutable std::ifstream bundle_;
    mutable std::unordered_map<std::string,Entry> cache_;
    mutable std::list<std::string> lru_;
    mutable std::size_t bytes_{};
    mutable std::uint64_t evictions_{};
};
}
