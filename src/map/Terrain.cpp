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
  const auto value = std::bit_cast<std::int32_t>(
      (static_cast<std::uint32_t>(x) << 16U | static_cast<std::uint32_t>(y)) ^ 81397550U);
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(value) * lookup[(x + y) % 7] % 65535);
}

Image rotate(const Image& input, int turns) {
  turns = (turns % 4 + 4) % 4;
  Image result = input;
  while (turns--) {
    Image next{result.height, result.width, std::vector<unsigned char>(result.pixels.size())};
    for (int y = 0; y < result.height; ++y) {
      for (int x = 0; x < result.width; ++x) {
        std::copy_n(result.pixels.begin() + static_cast<std::ptrdiff_t>((x + y * result.width) * 4),
                    4,
                    next.pixels.begin() +
                        static_cast<std::ptrdiff_t>((result.height - 1 - y + x * next.width) * 4));
      }
    }
    result = std::move(next);
  }
  return result;
}

namespace {
// Straight-alpha source over, with an 8-bit alpha mask. Ground assets are
// opaque, so transition composition does not lose color through premultiplication.
void overlay(Image& target, const Image& source, int sx, int sy, int dx, int dy, int w, int h,
             const Image* mask = nullptr) {
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (sx + x >= source.width || sy + y >= source.height || dx + x >= target.width ||
          dy + y >= target.height) {
        continue;
      }
      const auto si = static_cast<std::size_t>((sx + x + (sy + y) * source.width) * 4);
      const auto di = static_cast<std::size_t>((dx + x + (dy + y) * target.width) * 4);
      const unsigned source_alpha = source.pixels[si + 3];
      unsigned mask_alpha =
          mask ? mask->pixels[static_cast<std::size_t>((x + y * mask->width) * 4 + 3)] : 255U;
      if (mask_alpha == 255) {
        mask_alpha = 256;
      }
      const unsigned alpha = source_alpha * mask_alpha >> 8U;
      const unsigned da = target.pixels[di + 3];
      const unsigned out_alpha = alpha + (da * (256U - alpha) >> 8U);
      for (unsigned c = 0; c < 3; ++c) {
        const unsigned source_premultiplied =
            source_alpha == 255
                ? source.pixels[si + c]
                : (static_cast<unsigned>(source.pixels[si + c]) * source_alpha + 255U) >> 8U;
        const unsigned target_premultiplied =
            da == 255 ? target.pixels[di + c]
                      : (static_cast<unsigned>(target.pixels[di + c]) * da + 255U) >> 8U;
        const unsigned numerator = (source_premultiplied * mask_alpha >> 8U) +
                                   (target_premultiplied * (256U - alpha) >> 8U);
        target.pixels[di + c] = static_cast<unsigned char>(
            out_alpha ? std::min(255U, (numerator << 8U) / out_alpha) : 0);
      }
      target.pixels[di + 3] = static_cast<unsigned char>(out_alpha);
    }
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
  std::uint64_t frame{};
  std::uint64_t evictions{};

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
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
      for (int kind = 0; kind < 6; ++kind) {
        const auto match =
            std::find_if(flash::sheets.begin(), flash::sheets.end(),
                         [&](const auto& sheet) { return sheet.name == names[kind]; });
        const auto sheet = static_cast<int>(match - flash::sheets.begin());
        for (int i = 0; i < match->count; ++i) {
          TextureDef def;
          def.sheet = sheet;
          def.index = i;
          masks[static_cast<std::size_t>(quadrant)][static_cast<std::size_t>(kind)].push_back(
              rotate(texture(def), turns[quadrant] - (kind == 1 ? 1 : 0)));
        }
      }
    }

    if (live) {
      images.assign(8192, Image{8, 8, std::vector<unsigned char>(256)});
      owners.resize(8192);
      revisions.resize(8192);
      for (std::size_t i = 8192; i > 0; --i) {
        free_slots.push_back(i - 1);
      }
    }
  }

  Image texture(const TextureDef& def, std::uint32_t hash = 0) const {
    const auto& selected = def.select(hash);
    if (selected.sheet < 0) {
      return Image{8, 8, std::vector<unsigned char>(256)};
    }

    const auto& sheet = flash::sheets.at(static_cast<std::size_t>(selected.sheet));
    if (sheet.asset < 0) {
      return Image{8, 8, std::vector<unsigned char>(256)};
    }

    const auto& image = originals.at(static_cast<std::size_t>(sheet.asset));
    const int columns = image.width / sheet.width;
    return crop(image, selected.index % columns * sheet.width,
                selected.index / columns * sheet.height, sheet.width, sheet.height);
  }

  const Image& ground_image(std::uint16_t type) {
    const auto it = ground_images.find(type);
    if (it != ground_images.end()) {
      return it->second;
    }

    const auto* ground = world.catalog().ground(type);
    return ground_images
        .emplace(type,
                 ground ? texture(ground->texture) : Image{8, 8, std::vector<unsigned char>(256)})
        .first->second;
  }

  void reserve_slots() {
    if (!live || free_slots.size() >= 2) {
      return;
    }
    // A slot used in this frame remains pinned until its draw is submitted.
    auto victim = cache.end();
    for (auto it = cache.begin(); it != cache.end(); ++it) {
      if (it->second.touched != frame &&
          (victim == cache.end() || it->second.touched < victim->second.touched)) {
        victim = it;
      }
    }
    if (victim == cache.end()) {
      throw std::runtime_error("Visible terrain exceeds texture cache capacity");
    }

    owners[victim->second.image - first] = nullptr;
    if (victim->second.overlay >= 0) {
      owners[static_cast<std::size_t>(victim->second.overlay) - first] = nullptr;
    }
    free_slots.push_back(victim->second.image - first);
    if (victim->second.overlay >= 0) {
      free_slots.push_back(static_cast<std::size_t>(victim->second.overlay) - first);
    }
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
    if (image.width != 8 || image.height != 8) {
      throw std::runtime_error("Unexpected live terrain texture dimensions");
    }
    const auto slot = free_slots.back();
    free_slots.pop_back();
    images[slot] = image;
    revisions[slot] = ++next_revision;
    changes.push_back(slot);
    return first + slot;
  }

  TileVisual compose(int x, int y) {
    const auto* state = world.tile(x, y);
    const auto* ground = world.catalog().ground(state->type);
    if (!ground || state->type == 255) {
      return {};
    }

    const auto hash = tile_hash(x, y);
    std::array<std::uint16_t, 9> signature;
    const bool edged = ground->edge.sheet >= 0 || !ground->edge.random.empty();
    for (int ny = -1; ny <= 1; ++ny) {
      for (int nx = -1; nx <= 1; ++nx) {
        const auto i = static_cast<std::size_t>(nx + 1 + (ny + 1) * 3);
        const auto* neighbor = world.tile(x + nx, y + ny);
        const auto* adjacent = neighbor ? world.catalog().ground(neighbor->type) : nullptr;
        if (edged) {
          signature[i] = (!neighbor || (ground->same_type_edge ? neighbor->type == state->type
                                                               : neighbor->type != 255))
                             ? 1
                             : 0;
        } else if (state->type == 253) {
          signature[i] = neighbor ? neighbor->type : 255;
        } else {
          signature[i] = !adjacent || adjacent->blend_priority <= ground->blend_priority
                             ? state->type
                             : neighbor->type;
        }
      }
    }
    signature[4] = state->type;

    const auto& selected = ground->texture.select(hash);
    std::string key = std::to_string(state->type) + ':' + std::to_string(selected.sheet) + ':' +
                      std::to_string(selected.index) + ':';
    for (const auto type : signature) {
      key.push_back(static_cast<char>(type & 255U));
      key.push_back(static_cast<char>(type >> 8U));
    }

    bool redrawn{};

    auto found = cache.find(key);
    if (found == cache.end()) {
      reserve_slots();
      auto original = texture(ground->texture, hash);
      auto base = original;
      if (!edged && state->type != 253 &&
          std::any_of(signature.begin(), signature.end(),
                      [&](auto type) { return type != state->type; })) {
        base = ground_image(state->type);
      }
      if (ground->animate && !edged && state->type != 253) {
        std::fill(base.pixels.begin(), base.pixels.end(), 0);
      }
      if (state->type == 253) {
        auto priority = [&](int nx, int ny) {
          const auto* tile = world.tile(x + nx, y + ny);
          const auto* definition = tile ? world.catalog().ground(tile->type) : nullptr;
          return definition ? definition->composite_priority : -1;
        };
        constexpr int nxs[4][3] = {{0, -1, -1}, {0, 1, 1}, {-1, 0, -1}, {1, 0, 1}};
        constexpr int nys[4][3] = {{-1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, 1, 1}};
        base = {8, 8, std::vector<unsigned char>(256)};
        for (int q = 0; q < 4; ++q) {
          const int first = priority(nxs[q][0], nys[q][0]);
          const int second = priority(nxs[q][1], nys[q][1]);
          const int chosen = first < 0 && second < 0 ? 2 : first < second ? 1 : 0;
          const auto* tile = world.tile(x + nxs[q][chosen], y + nys[q][chosen]);
          const auto& source = tile && priority(nxs[q][chosen], nys[q][chosen]) >= 0
                                   ? ground_image(tile->type)
                                   : ground_image(255);
          overlay(base, source, q % 2 * 4, q / 2 * 4, q % 2 * 4, q / 2 * 4, 4, 4);
        }
        redrawn = true;
      } else if (edged) {
        const auto edge = texture(ground->edge);
        constexpr int edges[4] = {3, 1, 5, 7};
        for (int i = 0; i < 4; ++i) {
          if (!signature[static_cast<std::size_t>(edges[i])]) {
            const auto rotated = rotate(edge, i);
            overlay(base, rotated, 0, 0, 0, 0, base.width, base.height);
            redrawn = true;
          }
        }
        constexpr int corners[4] = {0, 2, 8, 6};
        constexpr int side0[4] = {3, 1, 5, 3};
        constexpr int side1[4] = {1, 5, 7, 7};
        for (int i = 0; i < 4; ++i) {
          const bool first = signature[static_cast<std::size_t>(side0[i])] != 0;
          const bool second = signature[static_cast<std::size_t>(side1[i])] != 0;
          const auto* definition =
              first && second && !signature[static_cast<std::size_t>(corners[i])] ? &ground->corner
              : !first && !second ? &ground->inner_corner
                                  : nullptr;
          if (definition && (definition->sheet >= 0 || !definition->random.empty())) {
            const auto image = rotate(texture(*definition), i);
            overlay(base, image, 0, 0, 0, 0, base.width, base.height);
            redrawn = true;
          }
        }
      } else if (base.width == 8 && base.height == 8) {
        const int neighbor[4][3] = {{3, 0, 1}, {1, 2, 5}, {7, 6, 3}, {5, 8, 7}};
        for (int q = 0; q < 4; ++q) {
          const auto neighbor0 = signature[static_cast<std::size_t>(neighbor[q][0])];
          const auto neighbor1 = signature[static_cast<std::size_t>(neighbor[q][1])];
          const auto neighbor2 = signature[static_cast<std::size_t>(neighbor[q][2])];
          const auto type = state->type;
          if (neighbor0 == type && neighbor1 == type && neighbor2 == type) {
            continue;
          }
          auto blend = [&](std::uint16_t candidate, int kind) {
            const auto& options =
                masks[static_cast<std::size_t>(q)][static_cast<std::size_t>(kind)];
            const auto& mask =
                options[mask_variant < 0 ? hash % options.size()
                                         : static_cast<std::size_t>(mask_variant) % options.size()];
            const auto& image = ground_image(candidate);
            overlay(base, image, q % 2 * 4, q / 2 * 4, q % 2 * 4, q / 2 * 4, 4, 4, &mask);
          };
          if (neighbor0 == type && neighbor2 == type) {
            blend(neighbor1, 3);
          } else if (neighbor0 != type && neighbor2 != type) {
            if (neighbor0 != neighbor2) {
              blend(neighbor0, 4);
              blend(neighbor2, 5);
            } else {
              blend(neighbor0, 0);
            }
          } else if (neighbor0 != type) {
            blend(neighbor0, 1);
          } else {
            blend(neighbor2, 2);
          }
          redrawn = true;
        }
      }
      Cached result;
      result.redrawn = redrawn;
      result.touched = frame;
      if (ground->animate && redrawn) {
        result.overlay = static_cast<int>(add(base));
        result.image = add(original);
      } else {
        result.image = add(ground->animate ? original : base);
      }
      found = cache.emplace(key, result).first;
      if (live) {
        owners[result.image - first] = &found->second;
        if (result.overlay >= 0) {
          owners[static_cast<std::size_t>(result.overlay) - first] = &found->second;
        }
      }
    }

    found->second.touched = frame;

    TileVisual visual;
    redrawn = found->second.redrawn;
    visual.image = found->second.image;
    visual.overlay = found->second.overlay;
    if (ground->random_offset && !redrawn) {
      visual.offset = {static_cast<double>(hash & 1U) / 2,
                       static_cast<double>((hash >> 1U) & 1U) / 2};
    } else if (!redrawn) {
      visual.offset = {ground->offset_x, ground->offset_y};
    }
    visual.sink = ground->sink ? (redrawn ? 6 : 12) : 0;
    return visual;
  }
};

Terrain::Terrain(const World& world, const std::filesystem::path& root, std::size_t first,
                 int variant, bool live)
    : width_(world.width()),
      impl_(std::make_unique<Impl>(world, root, images_, changes_, first, variant, live)) {
  if (!live) {
    tiles_.resize(static_cast<std::size_t>(world.width()) *
                  static_cast<std::size_t>(world.height()));
    for (int y = 0; y < world.height(); ++y) {
      for (int x = 0; x < world.width(); ++x) {
        tiles_[static_cast<std::size_t>(x + y * width_)] = impl_->compose(x, y);
      }
    }
  }
}

Terrain::~Terrain() = default;

TileVisual Terrain::tile(int x, int y) const {
  if (!impl_->world.tile(x, y)) {
    return {};
  }
  return impl_->live ? impl_->compose(x, y) : tiles_.at(static_cast<std::size_t>(x + y * width_));
}

Terrain::SlotStamp Terrain::stamp(std::size_t image) const {
  return {image, impl_->live ? impl_->revisions.at(image - impl_->first) : 0};
}

bool Terrain::retain(std::span<const SlotStamp> slots) const {
  if (!impl_->live) {
    return true;
  }
  for (const auto& slot : slots) {
    const auto index = slot.image - impl_->first;
    if (index >= impl_->revisions.size() || !impl_->owners[index] ||
        impl_->revisions[index] != slot.revision) {
      return false;
    }
  }
  for (const auto& slot : slots) {
    impl_->owners[slot.image - impl_->first]->touched = impl_->frame;
  }
  return true;
}

void Terrain::begin_frame() {
  ++impl_->frame;
  changes_.clear();
}

std::uint64_t Terrain::cache_evictions() const {
  return impl_->evictions;
}
}
