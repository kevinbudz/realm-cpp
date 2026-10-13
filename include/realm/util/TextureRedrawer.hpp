#pragma once
#include "realm/util/AnimatedChars.hpp"
#include "realm/objects/ObjectLibrary.hpp"
#include "realm/map/Map.hpp"
#include <array>
#include <list>
#include <unordered_map>
#include <memory>
namespace realm {
[[nodiscard]] std::array<std::uint32_t, 2> world_dyes(const World& world, const Entity& entity);
// Source frames are finite; composed appearances use an LRU with both bounds.
// Returned owners remain valid across eviction (including in-flight draws).
class Appearances {
 public:
  // Content key: which source art (kind 0: sheet/index and mask sheet/index; kind 1: animation image and mask) at which
  // size, padding and dyes. A plain struct, so a cache hit builds no string and crops no source art.
  struct Key {
    std::int32_t a{}, b{}, c{}, d{}, size{};
    std::uint32_t tex1{}, tex2{};
    std::uint8_t kind{}, pad{};
    [[nodiscard]] bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    [[nodiscard]] std::size_t operator()(const Key& key) const noexcept;
  };
  static constexpr std::size_t maximum_entries = 4096, maximum_bytes = 64 * 1024 * 1024;
  Appearances(const std::filesystem::path& root, const AnimationLibrary& animations);
  [[nodiscard]] std::shared_ptr<const Image> animated(AnimationFrame frame, int size, bool pad, std::uint32_t tex1,
                                                      std::uint32_t tex2) const;
  // The cache key `animated`/`texture` would use, and the image for a key: the source and mask derive from the key
  // alone.
  [[nodiscard]] Key animated_key(AnimationFrame frame, int size, bool pad, std::uint32_t tex1,
                                 std::uint32_t tex2) const;
  // Resolves an animated character to its animated key exactly as `texture` does.
  [[nodiscard]] Key texture_key(const TextureDef& texture, std::uint32_t id, int size, bool pad, std::uint32_t tex1,
                                std::uint32_t tex2) const;
  [[nodiscard]] std::shared_ptr<const Image> image(const Key& key) const;
  [[nodiscard]] std::shared_ptr<const Image> texture(const TextureDef& texture, std::uint32_t id, int size, bool pad,
                                                     std::uint32_t tex1, std::uint32_t tex2) const;
  [[nodiscard]] std::shared_ptr<const Image> portrait(const Catalog& catalog, int type, int skin, std::uint32_t tex1,
                                                      std::uint32_t tex2) const;
  // ConditionEffect.getIconsFromBit: a lofiInterface2 frame centered on a 16x16 canvas with
  // the sprite gradient, black outline and 0.3 alpha glow. Straight alpha, density 1.
  [[nodiscard]] std::shared_ptr<const Image> condition_icon(int frame) const;
  [[nodiscard]] std::size_t entries() const { return cache_.size(); }
  [[nodiscard]] std::size_t bytes() const { return bytes_; }
  [[nodiscard]] std::uint64_t evictions() const { return evictions_; }
  // Runtime bounds (defaults: the constants above); shrinking evicts immediately.
  void set_budget(std::size_t entries, std::size_t bytes) const;
  void clear() const;
  // Pure bitmap operation (TextureRedrawer.redraw), exposed for independent source-reference fixtures and UI art.
  // Without `glow` only the outline is drawn, as GlowRedrawer.outlineGlow with no glow color.
  [[nodiscard]] Image compose(const Image& source, const Image* mask, int size, bool pad, std::uint32_t tex1,
                              std::uint32_t tex2, bool glow = true) const;
  [[nodiscard]] const AnimationLibrary& animations() const { return animations_; }
  // The original per-pixel implementation, kept as the differential oracle for `compose` (identical bytes required).
  [[nodiscard]] Image compose_reference(const Image& source, const Image* mask, int size, bool pad, std::uint32_t tex1,
                                        std::uint32_t tex2) const;

 private:
  struct Entry {
    std::shared_ptr<const Image> image;
    std::list<Key>::iterator lru;
  };
  [[nodiscard]] Image sprite(int sheet, int index) const;
  // `make` runs only on a miss and returns the source art: `make(source,mask,normalize)` fills in an owned or borrowed
  // image.
  template <class Make>
  [[nodiscard]] std::shared_ptr<const Image> get(const Key& key, Make&& make) const;
  // Textile art for dye operations 4/5/9/10, cropped once per (sheet, index); the sheets are resolved at construction.
  [[nodiscard]] const Image& textile(std::uint32_t operation) const;
  std::array<int, 4> textile_sheets_{};
  mutable std::unordered_map<std::uint64_t, Image> textiles_;
  std::vector<Image> sheets_;
  const AnimationLibrary& animations_;
  mutable std::list<Key> lru_;
  mutable std::unordered_map<Key, Entry, KeyHash> cache_;
  mutable std::size_t bytes_{};
  mutable std::uint64_t evictions_{};
  mutable std::size_t budget_entries_{maximum_entries}, budget_bytes_{maximum_bytes};
};
}
