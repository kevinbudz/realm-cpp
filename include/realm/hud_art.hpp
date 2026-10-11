#pragma once
#include "realm/image.hpp"
#include <fstream>
#include <list>
#include <unordered_map>
namespace realm {
// Original finite UI decorations and filtered icons, baked offline. Runtime
// decodes only requested PNG records and retains at most 128 / 4 MiB of pixels.
class HudArt {
public:
    explicit HudArt(const std::filesystem::path& directory);
    [[nodiscard]] const Image& image(const std::string& key) const;
    [[nodiscard]] bool contains(const std::string& key) const { return records_.contains(key); }
    [[nodiscard]] std::size_t cache_entries() const { return cache_.size(); }
    [[nodiscard]] std::size_t cache_bytes() const { return bytes_; }
    [[nodiscard]] std::uint64_t evictions() const { return evictions_; }
private:
    struct Record { std::uint64_t offset{},length{}; };
    struct Entry { Image image; std::list<std::string>::iterator lru; };
    std::unordered_map<std::string,Record> records_;
    mutable std::ifstream bundle_;
    mutable std::unordered_map<std::string,Entry> cache_;
    mutable std::list<std::string> lru_;
    mutable std::size_t bytes_{};
    mutable std::uint64_t evictions_{};
};
}
