#pragma once
#include "realm/animation.hpp"
#include "realm/catalog.hpp"
#include "realm/world.hpp"
#include <list>
#include <unordered_map>
#include <memory>
namespace realm {
[[nodiscard]] std::array<std::uint32_t,2> world_dyes(const World& world,const Entity& entity);
// Source frames are finite; composed appearances use an LRU with both bounds.
// Returned owners remain valid across eviction (including in-flight draws).
class Appearances {
public:
    static constexpr std::size_t maximum_entries=256, maximum_bytes=32*1024*1024;
    Appearances(const std::filesystem::path& root,const AnimationLibrary& animations);
    [[nodiscard]] std::shared_ptr<const Image> animated(AnimationFrame frame,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const;
    [[nodiscard]] std::shared_ptr<const Image> texture(const TextureDef& texture,std::uint32_t id,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const;
    [[nodiscard]] std::shared_ptr<const Image> portrait(const Catalog& catalog,int type,int skin,std::uint32_t tex1,std::uint32_t tex2) const;
    // ConditionEffect.getIconsFromBit: a lofiInterface2 frame centered on a 16x16 canvas with
    // the sprite gradient, black outline and 0.3 alpha glow. Straight alpha, density 1.
    [[nodiscard]] std::shared_ptr<const Image> condition_icon(int frame) const;
    [[nodiscard]] std::size_t entries() const { return cache_.size(); }
    [[nodiscard]] std::size_t bytes() const { return bytes_; }
    [[nodiscard]] std::uint64_t evictions() const { return evictions_; }
    void clear() const;
    // Pure bitmap operation, exposed for independent source-reference fixtures.
    [[nodiscard]] Image compose(const Image& source,const Image* mask,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2) const;
private:
    struct Entry { std::shared_ptr<const Image> image;std::list<std::string>::iterator lru; };
    [[nodiscard]] Image sprite(int sheet,int index) const;
    [[nodiscard]] std::shared_ptr<const Image> get(std::string key,const Image& source,const Image* mask,int size,bool pad,std::uint32_t tex1,std::uint32_t tex2,bool normalize=false) const;
    std::vector<Image> sheets_;
    const AnimationLibrary& animations_;
    mutable std::list<std::string> lru_;
    mutable std::unordered_map<std::string,Entry> cache_;
    mutable std::size_t bytes_{};
    mutable std::uint64_t evictions_{};
};
}
