#pragma once
#include "realm/assets/Image.hpp"
#include "realm/assets/Appearances.hpp"
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace realm {
// The original Flash UI decorations (item tiles and icons, buttons, rank badges, frames, menu
// icons, skin art), drawn natively from the Flash source's sprite sheets, Myriad fonts and vector
// symbols when first requested. Keys name the original widget and state, e.g. "item2594",
// "portalenter1" or "tradeempty3t9". Images are premultiplied; at most 256 decoded images / 8 MiB
// are retained.
class HudArt {
 public:
  HudArt();
  ~HudArt();
  // Shares the caller's compositor and catalog; otherwise private copies are loaded on first use.
  void appearances(const Appearances* value, const Catalog* catalog);
  [[nodiscard]] Image portrait(int type, int skin, std::uint32_t tex1, std::uint32_t tex2) const;
  // GameObject.getPortrait: the live object's texture at portrait scale, including non-player quest
  // targets, at the same size for every texture width.
  [[nodiscard]] Image game_object_portrait(int type, int skin, std::uint32_t tex1,
                                           std::uint32_t tex2) const;
  [[nodiscard]] Image fame_portrait(int type, int skin, std::uint32_t tex1, std::uint32_t tex2,
                                    int size = 250) const;
  // FameUtil.numStarsToBigImage, used by GameSprite's large safe-area RankText.
  [[nodiscard]] Vec2 rank_star_size(double scale = 1.4) const;
  [[nodiscard]] Image rank_star_icon(int stars, double scale = 1.4) const;
  // StarGraphic without filters, tinted by RGB multipliers (0xffffff keeps the star's own color).
  [[nodiscard]] Image star_graphic(std::uint32_t tint, double scale = 1) const;
  [[nodiscard]] const Image& image(const std::string& key) const;
  // The same art at `scale` device pixels per logical unit. Vector-drawn tiles, panels and buttons
  // are rasterized at that density with their edges on device pixel boundaries; bitmap art keeps
  // its native density for ui_blit to resample. Images are cached per scale.
  [[nodiscard]] const Image& image(const std::string& key, double scale) const;
  [[nodiscard]] bool contains(const std::string& key) const;

  [[nodiscard]] std::size_t cache_entries() const { return cache_.size(); }

  [[nodiscard]] std::size_t cache_bytes() const { return bytes_; }

  [[nodiscard]] std::uint64_t evictions() const { return evictions_; }

 private:
  struct Sources;

  struct Entry {
    Image image;
    std::list<std::string>::iterator lru;
  };

  [[nodiscard]] Sources& sources() const;
  // Draws the image a key names; with `dry_run` only reports whether the key names one.
  [[nodiscard]] std::optional<Image> make(const std::string& key, bool dry_run,
                                          double density = 1) const;
  mutable std::unique_ptr<Sources> sources_;
  const Appearances* shared_appearances_{};
  const Catalog* shared_catalog_{};
  mutable std::unordered_map<std::string, Entry> cache_;
  mutable std::list<std::string> lru_;
  mutable std::size_t bytes_{};
  mutable std::uint64_t evictions_{};
};
}
