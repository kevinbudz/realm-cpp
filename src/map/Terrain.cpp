#include "realm/map/Terrain.hpp"
#include "realm/assets/AnimationLibrary.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <unordered_map>
namespace realm {
std::uint32_t tile_hash(int x, int y) {
  constexpr int lookup[] = {26171, 44789, 20333, 70429, 98257, 59393, 33961};
  const auto value =
      std::bit_cast<std::int32_t>((static_cast<std::uint32_t>(x) << 16U | static_cast<std::uint32_t>(y)) ^ 81397550U);
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(value) * lookup[(x + y) % 7] % 65535);
}
Image rotate(const Image& input, int turns) {
  turns = (turns % 4 + 4) % 4;
  Image result = input;
  while (turns--) {
    Image next{result.height, result.width, std::vector<unsigned char>(result.pixels.size())};
    for (int y = 0; y < result.height; ++y)
      for (int x = 0; x < result.width; ++x)
        std::copy_n(result.pixels.begin() + static_cast<std::ptrdiff_t>((x + y * result.width) * 4), 4,
                    next.pixels.begin() + static_cast<std::ptrdiff_t>((result.height - 1 - y + x * next.width) * 4));
    result = std::move(next);
  }
  return result;
}
namespace {
// Straight-alpha source over, with an 8-bit alpha mask. Ground assets are
// opaque, so transition composition does not lose color through premultiplication.
void overlay(Image& target, const Image& source, int sx, int sy, int dx, int dy, int w, int h,
             const Image* mask = nullptr) {
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (sx + x >= source.width || sy + y >= source.height || dx + x >= target.width || dy + y >= target.height)
        continue;
      const auto si = static_cast<std::size_t>((sx + x + (sy + y) * source.width) * 4),
                 di = static_cast<std::size_t>((dx + x + (dy + y) * target.width) * 4);
      const unsigned source_alpha = source.pixels[si + 3];
      unsigned mask_alpha = mask ? mask->pixels[static_cast<std::size_t>((x + y * mask->width) * 4 + 3)] : 255U;
      if (mask_alpha == 255) mask_alpha = 256;
      const unsigned alpha = source_alpha * mask_alpha >> 8U;
      const unsigned da = target.pixels[di + 3], out_alpha = alpha + (da * (256U - alpha) >> 8U);
      for (unsigned c = 0; c < 3; ++c) {
        const unsigned source_premultiplied =
            source_alpha == 255 ? source.pixels[si + c]
                                : (static_cast<unsigned>(source.pixels[si + c]) * source_alpha + 255U) >> 8U;
        const unsigned target_premultiplied =
            da == 255 ? target.pixels[di + c] : (static_cast<unsigned>(target.pixels[di + c]) * da + 255U) >> 8U;
        const unsigned numerator =
            (source_premultiplied * mask_alpha >> 8U) + (target_premultiplied * (256U - alpha) >> 8U);
        target.pixels[di + c] =
            static_cast<unsigned char>(out_alpha ? std::min(255U, (numerator << 8U) / out_alpha) : 0);
      }
      target.pixels[di + 3] = static_cast<unsigned char>(out_alpha);
    }
}
}
struct Terrain::Impl {
  struct Cached {
    std::size_t image{};
    int overlay{-1};
    bool redrawn{};
    std::uint64_t touched{};
  };
  std::vector<Image> originals;
  std::array<std::array<std::vector<Image>, 6>, 4> masks;
  std::map<std::uint16_t, Image> ground_images;
  std::unordered_map<std::string, Cached> cache;
  const World& world;
  std::vector<Image>& images;
  std::vector<std::size_t>& changes;
  std::vector<std::size_t> free_slots;
  std::vector<Cached*> owners;
  std::vector<std::uint64_t> revisions;
  std::uint64_t next_revision{};
  std::size_t first;
  int mask_variant;
  bool live;
  std::uint64_t frame{}, evictions{};
  Impl(const World& world_ref, const std::filesystem::path& root, std::vector<Image>& image_store,
       std::vector<std::size_t>& uploads, std::size_t first_index, int variant, bool dynamic)
      : world(world_ref),
        images(image_store),
        changes(uploads),
        first(first_index),
        mask_variant(variant),
        live(dynamic) {
    originals.reserve(flash::assets.size());
    for (const auto& asset : flash::assets) {
      auto image = load_png(root / asset.path);
      normalize_flash_png(image);
      originals.push_back(std::move(image));
    }
    constexpr const char* names[6] = {"inner_mask", "sides_mask",   "sides_mask",
                                      "outer_mask", "innerP1_mask", "innerP2_mask"};
    constexpr int turns[4] = {-1, 0, 2, 1};
    for (int quadrant = 0; quadrant < 4; ++quadrant)
      for (int kind = 0; kind < 6; ++kind) {
        const auto s = std::find_if(flash::sheets.begin(), flash::sheets.end(),
                                    [&](const auto& sheet) { return sheet.name == names[kind]; });
        const auto sheet = static_cast<int>(s - flash::sheets.begin());
        for (int i = 0; i < s->count; ++i) {
          TextureDef def;
          def.sheet = sheet;
          def.index = i;
          masks[static_cast<std::size_t>(quadrant)][static_cast<std::size_t>(kind)].push_back(
              rotate(texture(def), turns[quadrant] - (kind == 1 ? 1 : 0)));
        }
      }

    if (live) {
      images.assign(8192, Image{8, 8, std::vector<unsigned char>(256)});
      owners.resize(8192);
      revisions.resize(8192);
      for (std::size_t i = 8192; i > 0; --i) free_slots.push_back(i - 1);
    }
  }
  Image texture(const TextureDef& def, std::uint32_t hash = 0) const {
    const auto& t = def.select(hash);
    if (t.sheet < 0) return Image{8, 8, std::vector<unsigned char>(256)};
    const auto& s = flash::sheets.at(static_cast<std::size_t>(t.sheet));
    if (s.asset < 0) return Image{8, 8, std::vector<unsigned char>(256)};
    const auto& image = originals.at(static_cast<std::size_t>(s.asset));
    const int cols = image.width / s.width;
    return crop(image, t.index % cols * s.width, t.index / cols * s.height, s.width, s.height);
  }

  const Image& ground_image(std::uint16_t type) {
    const auto it = ground_images.find(type);
    if (it != ground_images.end()) return it->second;
    const auto* ground = world.catalog().ground(type);
    return ground_images.emplace(type, ground ? texture(ground->texture) : Image{8, 8, std::vector<unsigned char>(256)})
        .first->second;
  }
  void reserve_slots() {
    if (!live || free_slots.size() >= 2) return;
    // A slot used in this frame remains pinned until its draw is submitted.
    auto victim = cache.end();
    for (auto it = cache.begin(); it != cache.end(); ++it)
      if (it->second.touched != frame && (victim == cache.end() || it->second.touched < victim->second.touched))
        victim = it;
    if (victim == cache.end()) throw std::runtime_error("Visible terrain exceeds texture cache capacity");
    owners[victim->second.image - first] = nullptr;
    if (victim->second.overlay >= 0) owners[static_cast<std::size_t>(victim->second.overlay) - first] = nullptr;
    free_slots.push_back(victim->second.image - first);
    if (victim->second.overlay >= 0) free_slots.push_back(static_cast<std::size_t>(victim->second.overlay) - first);
    cache.erase(victim);
    ++evictions;
    reserve_slots();
  }
  std::size_t add(const Image& image) {
    if (!live) {
      const auto index = first + images.size();
      images.push_back(image);
      return index;
    }
    if (image.width != 8 || image.height != 8) throw std::runtime_error("Unexpected live terrain texture dimensions");
    const auto slot = free_slots.back();
    free_slots.pop_back();
    images[slot] = image;
    revisions[slot] = ++next_revision;
    changes.push_back(slot);
    return first + slot;
  }
  TileVisual compose(int x, int y) {
    const auto* state = world.tile(x, y);
    const auto* g = world.catalog().ground(state->type);
    if (!g || state->type == 255) return {};
    const auto hash = tile_hash(x, y);
    std::array<std::uint16_t, 9> sig;
    const bool edged = g->edge.sheet >= 0 || !g->edge.random.empty();
    for (int ny = -1; ny <= 1; ++ny)
      for (int nx = -1; nx <= 1; ++nx) {
        const auto i = static_cast<std::size_t>(nx + 1 + (ny + 1) * 3);
        const auto* neighbor = world.tile(x + nx, y + ny);
        const auto* ng = neighbor ? world.catalog().ground(neighbor->type) : nullptr;
        if (edged)
          sig[i] = (!neighbor || (g->same_type_edge ? neighbor->type == state->type : neighbor->type != 255)) ? 1 : 0;
        else if (state->type == 253)
          sig[i] = neighbor ? neighbor->type : 255;
        else
          sig[i] = !ng || ng->blend_priority <= g->blend_priority ? state->type : neighbor->type;
      }
    sig[4] = state->type;
    const auto& selected = g->texture.select(hash);
    std::string key =
        std::to_string(state->type) + ':' + std::to_string(selected.sheet) + ':' + std::to_string(selected.index) + ':';
    for (const auto type : sig) {
      key.push_back(static_cast<char>(type & 255U));
      key.push_back(static_cast<char>(type >> 8U));
    }
    bool redrawn{};
    auto found = cache.find(key);
    if (found == cache.end()) {
      reserve_slots();
      auto original = texture(g->texture, hash);
      auto base = original;
      if (!edged && state->type != 253 && std::any_of(sig.begin(), sig.end(), [&](auto t) { return t != state->type; }))
        base = ground_image(state->type);
      if (g->animate && !edged && state->type != 253) std::fill(base.pixels.begin(), base.pixels.end(), 0);
      if (state->type == 253) {
        auto priority = [&](int nx, int ny) {
          const auto* t = world.tile(x + nx, y + ny);
          const auto* d = t ? world.catalog().ground(t->type) : nullptr;
          return d ? d->composite_priority : -1;
        };
        constexpr int nxs[4][3] = {{0, -1, -1}, {0, 1, 1}, {-1, 0, -1}, {1, 0, 1}},
                      nys[4][3] = {{-1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, 1, 1}};
        base = {8, 8, std::vector<unsigned char>(256)};
        for (int q = 0; q < 4; ++q) {
          const int p0 = priority(nxs[q][0], nys[q][0]), p1 = priority(nxs[q][1], nys[q][1]);
          const int chosen = p0 < 0 && p1 < 0 ? 2 : p0 < p1 ? 1 : 0;
          const auto* t = world.tile(x + nxs[q][chosen], y + nys[q][chosen]);
          const auto& source =
              t && priority(nxs[q][chosen], nys[q][chosen]) >= 0 ? ground_image(t->type) : ground_image(255);
          overlay(base, source, q % 2 * 4, q / 2 * 4, q % 2 * 4, q / 2 * 4, 4, 4);
        }
        redrawn = true;
      } else if (edged) {
        const auto edge = texture(g->edge);
        constexpr int edges[4] = {3, 1, 5, 7};
        for (int i = 0; i < 4; ++i)
          if (!sig[static_cast<std::size_t>(edges[i])]) {
            const auto rotated = rotate(edge, i);
            overlay(base, rotated, 0, 0, 0, 0, base.width, base.height);
            redrawn = true;
          }
        constexpr int corners[4] = {0, 2, 8, 6}, side0[4] = {3, 1, 5, 3}, side1[4] = {1, 5, 7, 7};
        for (int i = 0; i < 4; ++i) {
          const bool a = sig[static_cast<std::size_t>(side0[i])] != 0, b = sig[static_cast<std::size_t>(side1[i])] != 0;
          const auto* def = a && b && !sig[static_cast<std::size_t>(corners[i])] ? &g->corner
                            : !a && !b                                           ? &g->inner_corner
                                                                                 : nullptr;
          if (def && (def->sheet >= 0 || !def->random.empty())) {
            const auto image = rotate(texture(*def), i);
            overlay(base, image, 0, 0, 0, 0, base.width, base.height);
            redrawn = true;
          }
        }
      } else if (base.width == 8 && base.height == 8) {
        const int neighbor[4][3] = {{3, 0, 1}, {1, 2, 5}, {7, 6, 3}, {5, 8, 7}};
        for (int q = 0; q < 4; ++q) {
          const auto n0 = sig[static_cast<std::size_t>(neighbor[q][0])],
                     n1 = sig[static_cast<std::size_t>(neighbor[q][1])],
                     n2 = sig[static_cast<std::size_t>(neighbor[q][2])], type = state->type;
          if (n0 == type && n1 == type && n2 == type) continue;
          auto blend = [&](std::uint16_t nt, int kind) {
            const auto& options = masks[static_cast<std::size_t>(q)][static_cast<std::size_t>(kind)];
            const auto& mask = options[mask_variant < 0 ? hash % options.size()
                                                        : static_cast<std::size_t>(mask_variant) % options.size()];
            const auto& image = ground_image(nt);
            overlay(base, image, q % 2 * 4, q / 2 * 4, q % 2 * 4, q / 2 * 4, 4, 4, &mask);
          };
          if (n0 == type && n2 == type)
            blend(n1, 3);
          else if (n0 != type && n2 != type) {
            if (n0 != n2) {
              blend(n0, 4);
              blend(n2, 5);
            } else
              blend(n0, 0);
          } else if (n0 != type)
            blend(n0, 1);
          else
            blend(n2, 2);
          redrawn = true;
        }
      }
      Cached result;
      result.redrawn = redrawn;
      result.touched = frame;
      if (g->animate && redrawn) {
        result.overlay = static_cast<int>(add(base));
        result.image = add(original);
      } else
        result.image = add(g->animate ? original : base);
      found = cache.emplace(key, result).first;
      if (live) {
        owners[result.image - first] = &found->second;
        if (result.overlay >= 0) owners[static_cast<std::size_t>(result.overlay) - first] = &found->second;
      }
    }
    found->second.touched = frame;
    TileVisual visual;
    redrawn = found->second.redrawn;
    visual.image = found->second.image;
    visual.overlay = found->second.overlay;
    if (g->random_offset && !redrawn)
      visual.offset = {static_cast<double>(hash & 1U) / 2, static_cast<double>((hash >> 1U) & 1U) / 2};
    else if (!redrawn)
      visual.offset = {g->offset_x, g->offset_y};
    visual.sink = g->sink ? (redrawn ? 6 : 12) : 0;
    return visual;
  }
};
Terrain::Terrain(const World& world, const std::filesystem::path& root, std::size_t first, int variant, bool live)
    : width_(world.width()), impl_(std::make_unique<Impl>(world, root, images_, changes_, first, variant, live)) {
  if (!live) {
    tiles_.resize(static_cast<std::size_t>(world.width()) * static_cast<std::size_t>(world.height()));
    for (int y = 0; y < world.height(); ++y)
      for (int x = 0; x < world.width(); ++x) tiles_[static_cast<std::size_t>(x + y * width_)] = impl_->compose(x, y);
  }
}
Terrain::~Terrain() = default;
TileVisual Terrain::tile(int x, int y) const {
  if (!impl_->world.tile(x, y)) return {};
  return impl_->live ? impl_->compose(x, y) : tiles_.at(static_cast<std::size_t>(x + y * width_));
}
Terrain::SlotStamp Terrain::stamp(std::size_t image) const {
  return {image, impl_->live ? impl_->revisions.at(image - impl_->first) : 0};
}
bool Terrain::retain(std::span<const SlotStamp> slots) const {
  if (!impl_->live) return true;
  for (const auto& slot : slots) {
    const auto index = slot.image - impl_->first;
    if (index >= impl_->revisions.size() || !impl_->owners[index] || impl_->revisions[index] != slot.revision)
      return false;
  }
  for (const auto& slot : slots) impl_->owners[slot.image - impl_->first]->touched = impl_->frame;
  return true;
}
void Terrain::begin_frame() {
  ++impl_->frame;
  changes_.clear();
}
std::uint64_t Terrain::cache_evictions() const { return impl_->evictions; }
}
