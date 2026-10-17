#include "realm/util/Telemetry.hpp"
#include "realm/assets/Appearances.hpp"
#include "flash_assets.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace realm {
namespace {
// std::lround for a non-negative value without the libm call (truncate, then round the fraction
// half up; exact).
inline long round_up(double value) noexcept {
  const auto whole = static_cast<long>(value);
  return value - static_cast<double>(whole) >= .5 ? whole + 1 : whole;
}

// v / 255.0 for every byte v, computed once (the same division the per-pixel code performs).
const std::array<double, 256>& unit_alpha() {
  static const std::array<double, 256> table = [] {
    std::array<double, 256> values{};
    for (std::size_t v = 0; v < values.size(); ++v) {
      values[v] = static_cast<double>(v) / 255.0;
    }
    return values;
  }();
  return table;
}
}

std::array<std::uint32_t, 2> world_dyes(const World& world, const Entity& entity) {
  std::array<std::uint32_t, 2> result{entity.appearance.tex1, entity.appearance.tex2};
  if (!entity.player || entity.id != world.player_id()) {
    return result;
  }

  REALM_ZONE("scene.world_dyes");
  const int x = static_cast<int>(entity.motion.position.x);
  const int y = static_cast<int>(entity.motion.position.y);
  const int dx = entity.motion.position.x - x > .5 ? 1 : -1;
  const int dy = entity.motion.position.y - y > .5 ? 1 : -1;
  // The first Merchant in (neighbour order {0,0},{1,0},{0,1},{1,1}, then entity order) decides the
  // dye. One pass over the merchants (in entity order) finds it: the four neighbour tiles are
  // distinct, so each belongs to at most one neighbour slot.
  const Entity* merchant{};
  int slot = 4;
  world.each_merchant([&](const Entity& candidate) {
    if (!slot) {
      return;
    }
    const int ox = static_cast<int>(candidate.motion.position.x) - x;
    const int oy = static_cast<int>(candidate.motion.position.y) - y;
    const int kx = ox == 0 ? 0 : ox == dx ? 1 : -1;
    const int ky = oy == 0 ? 0 : oy == dy ? 1 : -1;
    if (kx < 0 || ky < 0 || kx + 2 * ky >= slot) {
      return;
    }
    const auto* definition = world.catalog().object(candidate.type);
    if (!definition || definition->class_name != "Merchant") {
      return;
    }
    merchant = &candidate;
    slot = kx + 2 * ky;
  });
  if (!merchant) {
    return result;
  }
  const auto& seller = *merchant;
  const double ex = seller.motion.position.x - entity.motion.position.x;
  const double ey = seller.motion.position.y - entity.motion.position.y;
  if (ex * ex + ey * ey >= 1) {
    return result;
  }
  const auto stat =
      seller.profile.stats.find(static_cast<std::uint8_t>(flash::StatId::MERCHANDISE_TYPE_STAT));
  const auto* item = stat == seller.profile.stats.end()
                         ? nullptr
                         : world.catalog().object(static_cast<std::uint16_t>(stat->second));
  if (item && item->xml.child("Activate").text() == "Dye") {
    if (item->xml.has("Tex1")) {
      result[0] = static_cast<std::uint32_t>(item->xml.number("Tex1"));
    }
    if (item->xml.has("Tex2")) {
      result[1] = static_cast<std::uint32_t>(item->xml.number("Tex2"));
    }
  }
  return result;
}

Appearances::Appearances(const std::filesystem::path& root, const AnimationLibrary& animations)
    : Appearances(std::make_shared<const FlashImages>(root), animations) {}

Appearances::Appearances(std::shared_ptr<const FlashImages> pngs,
                         const AnimationLibrary& animations)
    : pngs_(std::move(pngs)), animations_(animations) {
  textile_sheets_.fill(-1);
  for (std::size_t sheet = 0; sheet < flash::sheets.size(); ++sheet) {
    const std::string_view name = flash::sheets[sheet].name;
    if (name == "textile4x4") {
      textile_sheets_[0] = static_cast<int>(sheet);
    } else if (name == "textile5x5") {
      textile_sheets_[1] = static_cast<int>(sheet);
    } else if (name == "textile9x9") {
      textile_sheets_[2] = static_cast<int>(sheet);
    } else if (name == "textile10x10") {
      textile_sheets_[3] = static_cast<int>(sheet);
    }
  }
}

Image Appearances::sprite(int sheet, int index) const {
  if (sheet < 0 || static_cast<std::size_t>(sheet) >= flash::sheets.size()) {
    return {1, 1, std::vector<unsigned char>(4)};
  }
  const auto& data = flash::sheets[static_cast<std::size_t>(sheet)];
  if (data.asset < 0 || index < 0 || index >= data.count) {
    return {1, 1, std::vector<unsigned char>(4)};
  }
  const auto& image = pngs_->normalized[static_cast<std::size_t>(data.asset)];
  const int columns = image.width / data.width;
  return crop(image, index % columns * data.width, index / columns * data.height, data.width,
              data.height);
}

std::shared_ptr<const Image> Appearances::condition_icon(int frame) const {
  const auto sheet = std::find_if(flash::sheets.begin(), flash::sheets.end(),
                                  [](const auto& value) { return value.name == "lofiInterface2"; });
  if (sheet == flash::sheets.end()) {
    return {};
  }

  const auto source = this->sprite(static_cast<int>(sheet - flash::sheets.begin()), frame);
  constexpr int size = 16;
  constexpr int margin = 4;
  Image icon{size, size, std::vector<unsigned char>(size * size * 4)};
  for (int y = 0; y < source.height && y + margin < size; ++y) {
    for (int x = 0; x < source.width && x + margin < size; ++x) {
      for (int channel = 0; channel < 4; ++channel) {
        icon.pixels[static_cast<std::size_t>(((y + margin) * size + x + margin) * 4 + channel)] =
            source.pixels[static_cast<std::size_t>((y * source.width + x) * 4 + channel)];
      }
    }
  }

  // The SUBTRACT gradient runs from half height down, up to 0x28 at the bottom edge.
  for (int y = 0; y < size; ++y) {
    const int subtract = static_cast<int>(40 * std::clamp((2.0 * (y + .5) / size - 1), 0.0, 1.0));
    for (int x = 0; x < size; ++x) {
      const auto at = static_cast<std::size_t>((y * size + x) * 4);
      for (int channel = 0; channel < 3; ++channel) {
        icon.pixels[at + static_cast<std::size_t>(channel)] = static_cast<unsigned char>(
            std::max(0, icon.pixels[at + static_cast<std::size_t>(channel)] - subtract));
      }
    }
  }

  // OUTLINE_FILTER (black, alpha 0.8, blur 1.4, strength 255): every pixel next to the art.
  // GLOW_FILTER (black, alpha 0.3, blur 6, strength 2): a six-wide box blur of the outlined alpha.
  std::vector<double> outlined(static_cast<std::size_t>(size * size));
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      int neighbor = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int sx = x + dx;
          const int sy = y + dy;
          if (sx < 0 || sy < 0 || sx >= size || sy >= size) {
            continue;
          }
          neighbor = std::max(
              neighbor,
              static_cast<int>(icon.pixels[static_cast<std::size_t>((sy * size + sx) * 4 + 3)]));
        }
      }
      const double alpha = icon.pixels[static_cast<std::size_t>((y * size + x) * 4 + 3)] / 255.0;
      outlined[static_cast<std::size_t>(y * size + x)] =
          alpha + neighbor * .8 / 255.0 * (1 - alpha);
    }
  }
  std::vector<double> halo(outlined.size());
  for (int pass = 0; pass < 2; ++pass) {
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        double sum = 0;
        for (int offset = -3; offset < 3; ++offset) {
          const int sx = pass ? x : x + offset;
          const int sy = pass ? y + offset : y;
          if (sx >= 0 && sy >= 0 && sx < size && sy < size) {
            sum += (pass ? halo : outlined)[static_cast<std::size_t>(sy * size + sx)];
          }
        }
        (pass ? outlined : halo)[static_cast<std::size_t>(y * size + x)] = sum / 6;
      }
    }
  }
  auto result = std::make_shared<Image>(icon);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const auto at = static_cast<std::size_t>((y * size + x) * 4);
      const double art_alpha = icon.pixels[at + 3] / 255.0;
      double neighbor = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int sx = x + dx;
          const int sy = y + dy;
          if (sx < 0 || sy < 0 || sx >= size || sy >= size) {
            continue;
          }
          neighbor = std::max(
              neighbor, icon.pixels[static_cast<std::size_t>((sy * size + sx) * 4 + 3)] / 255.0);
        }
      }
      // Black behind the art: the outline, then the glow behind both.
      const double outline = neighbor * .8 * (1 - art_alpha);
      const double glow = std::min(1.0, outlined[static_cast<std::size_t>(y * size + x)] * 2) * .3;
      const double behind = outline + glow * (1 - outline - art_alpha);
      const double out = art_alpha + std::max(0.0, behind) * (1 - art_alpha);
      result->pixels[at + 3] =
          static_cast<unsigned char>(std::clamp(std::lround(out * 255), 0L, 255L));
      if (out > 0) {
        for (int channel = 0; channel < 3; ++channel) {
          result->pixels[at + static_cast<std::size_t>(channel)] = static_cast<unsigned char>(
              std::lround(icon.pixels[at + static_cast<std::size_t>(channel)] * art_alpha / out));
        }
      }
    }
  }
  return result;
}

const Image& Appearances::textile(std::uint32_t op) const {
  static const Image none;
  static const Image blank{1, 1, std::vector<unsigned char>(4)};
  const auto kind = op >> 24U;
  if (kind != 4 && kind != 5 && kind != 9 && kind != 10) {
    return none;
  }
  // The last sheet carrying the name wins, as before; the (kind, index) art is cropped once and
  // kept (bounded by the sheet's count).
  const int sheet = textile_sheets_[kind == 4 ? 0 : kind == 5 ? 1 : kind == 9 ? 2 : 3];
  if (sheet < 0) {
    return none;
  }
  const int index = static_cast<int>(op & 0xffffffU);
  const auto& s = flash::sheets[static_cast<std::size_t>(sheet)];
  if (s.asset < 0 || index < 0 || index >= s.count) {
    return blank;
  }
  const auto key = (static_cast<std::uint64_t>(sheet) << 32U) | static_cast<std::uint32_t>(index);
  auto found = textiles_.find(key);
  if (found == textiles_.end()) {
    found = textiles_.emplace(key, sprite(sheet, index)).first;
  }
  return found->second;
}

Image Appearances::compose(const Image& source, const Image* mask, int size, bool pad,
                           std::uint32_t tex1, std::uint32_t tex2, bool glow, double device_scale,
                           bool sharp) const {
  REALM_ZONE("appearance.compose");
  // Texturing is done on a 5x bitmap before size scaling. Textile origin is
  // the frame origin, including transparent extended-attack padding.
  const bool dyed = mask && (tex1 || tex2);
  Image dyed_source;
  const Image* input = &source;
  if (dyed) {
    dyed_source = {source.width * 5, source.height * 5,
                   std::vector<unsigned char>(static_cast<std::size_t>(source.width * 5) *
                                              static_cast<std::size_t>(source.height * 5) * 4)};
    const Image* textiles[2] = {};
    const std::uint32_t ops[2] = {tex1, tex2};
    for (int channel = 0; channel < 2; ++channel) {
      textiles[channel] = &textile(ops[channel]);
    }
    for (int y = 0; y < dyed_source.height; ++y) {
      for (int x = 0; x < dyed_source.width; ++x) {
        const auto origin = static_cast<std::size_t>((y / 5 * source.width + x / 5) * 4);
        const auto target = static_cast<std::size_t>((y * dyed_source.width + x) * 4);
        std::copy_n(source.pixels.data() + origin, 4, dyed_source.pixels.data() + target);
        if (x / 5 >= mask->width || y / 5 >= mask->height) {
          continue;
        }
        const auto index = static_cast<std::size_t>((y / 5 * mask->width + x / 5) * 4);
        if (!mask->pixels[index + 3]) {
          continue;
        }
        const int channel = mask->pixels[index] > 0 ? 0 : mask->pixels[index + 1] > 0 ? 1 : -1;
        if (channel < 0 || ops[channel] == 0) {
          continue;
        }
        const auto& tile = *textiles[channel];
        for (int channel = 0; channel < 3; ++channel) {
          const auto color =
              tile.pixels.empty()
                  ? static_cast<unsigned char>((ops[channel] >> 24U) == 1
                                                   ? (ops[channel] >> (16 - 8 * channel)) & 255U
                                                   : 0)
                  : tile.pixels[static_cast<std::size_t>(
                        (y % tile.height * tile.width + x % tile.width) * 4 + channel)];
          dyed_source.pixels[target + static_cast<std::size_t>(channel)] =
              static_cast<unsigned char>(
                  (static_cast<unsigned>(color) *
                       mask->pixels[index + static_cast<std::size_t>(channel)] +
                   127U) /
                  255U);
        }
        dyed_source.pixels[target + 3] = 255;
      }
    }
    input = &dyed_source;
  }

  // Raster bounds protect against hostile size stats; density tracks the
  // reduction so world placement still preserves the authoritative size.
  const double factor = 5 * std::max(1, dyed ? size / 5 * 5 : size) / 100.0 * device_scale;
  // Sharp sizes round to the nearest device pixel; the default truncates as before.
  const auto scaled = [sharp](double extent) {
    return std::max(1, sharp ? static_cast<int>(std::lround(extent)) : static_cast<int>(extent));
  };
  const int wanted_w = scaled(factor * source.width);
  const int wanted_h = scaled(factor * source.height);
  const double density = std::min(1.0, 1024.0 / std::max(wanted_w, wanted_h));
  const int w = std::max(1, static_cast<int>(wanted_w * density));
  const int h = std::max(1, static_cast<int>(wanted_h * density));
  // The sprite block sits `margin` pixels from the top and left, with `bottom` pixels below it.
  // Sharp sizes scale both with the device scale; the defaults are 12 and 1 (12 when padded).
  const int margin = sharp ? std::max(1, static_cast<int>(std::lround(12 * device_scale))) : 12;
  const int bottom = sharp
                         ? std::max(1, static_cast<int>(std::lround((pad ? 12 : 1) * device_scale)))
                         : (pad ? 12 : 1);
  Image result{w + 2 * margin, h + margin + bottom,
               std::vector<unsigned char>(static_cast<std::size_t>(w + 2 * margin) *
                                          static_cast<std::size_t>(h + margin + bottom) * 4)};
  result.pixel_scale = density;

  // Sharp bilinear: an exact nearest upscale by the integer ceil(w / input width), then an area
  // reduction to w (below). Upscaling copies texels, so the reduction sees the same texel grid.
  Image upscaled;
  if (sharp && (w > input->width || h > input->height)) {
    const int kx = std::max(1, (w + input->width - 1) / input->width);
    const int ky = std::max(1, (h + input->height - 1) / input->height);
    upscaled = {input->width * kx, input->height * ky,
                std::vector<unsigned char>(static_cast<std::size_t>(input->width * kx) *
                                           static_cast<std::size_t>(input->height * ky) * 4)};
    for (int y = 0; y < upscaled.height; ++y) {
      for (int x = 0; x < upscaled.width; ++x) {
        const auto from = static_cast<std::size_t>(((y / ky) * input->width + x / kx) * 4);
        const auto to = static_cast<std::size_t>((y * upscaled.width + x) * 4);
        std::copy_n(input->pixels.data() + from, 4, upscaled.pixels.data() + to);
      }
    }
    input = &upscaled;
  }

  // Same arithmetic as compose_reference, with per-column and per-row terms computed once instead
  // of once per pixel.
  struct Span {
    int first{};
    int last{};
    double extent{};
    std::size_t offset{};
  };

  // `area` weights every upscaled destination by the source texels it covers, as the reduction
  // does; the default picks one texel per destination when upscaling.
  const auto spans = [&](int count, int from, int source_count, std::vector<Span>& out,
                         std::vector<double>& weights, bool area) {
    out.resize(static_cast<std::size_t>(count));
    weights.clear();
    const bool nearest = count >= source_count && !area;
    for (int i = 0; i < count; ++i) {
      const double start = nearest ? std::floor((i + .49999) * source_count / count)
                                   : static_cast<double>(i) * source_count / count;
      const double end = nearest ? start + 1 : static_cast<double>(i + 1) * source_count / count;
      auto& span = out[static_cast<std::size_t>(i)];
      span.first = static_cast<int>(start);
      span.last = std::min(source_count, static_cast<int>(std::ceil(end)));
      span.extent = end - start;
      span.offset = weights.size();
      for (int index = span.first; index < span.last; ++index) {
        weights.push_back(std::min(end, index + 1.0) - std::max(start, static_cast<double>(index)));
      }
    }
    (void)from;
  };
  static thread_local std::vector<Span> column_spans;
  static thread_local std::vector<Span> row_spans;
  static thread_local std::vector<double> column_weights;
  static thread_local std::vector<double> row_weights;
  spans(w, input->width, input->width, column_spans, column_weights, sharp);
  spans(h, input->height, input->height, row_spans, row_weights, sharp);
  const auto& unit = unit_alpha();
  const unsigned char* const pixels = input->pixels.data();
  for (int y = 0; y < h; ++y) {
    const auto& ry = row_spans[static_cast<std::size_t>(y)];
    const int subtract =
        static_cast<int>(40 * std::max(0.0, 2.0 * (y + margin) / result.height - 1));
    for (int x = 0; x < w; ++x) {
      const auto& rx = column_spans[static_cast<std::size_t>(x)];
      const double area = rx.extent * ry.extent;
      double channels[4] = {};
      for (int sy = ry.first; sy < ry.last; ++sy) {
        const double wy = row_weights[ry.offset + static_cast<std::size_t>(sy - ry.first)];
        for (int sx = rx.first; sx < rx.last; ++sx) {
          const auto src = static_cast<std::size_t>((sy * input->width + sx) * 4);
          const unsigned char a = pixels[src + 3];
          // A transparent texel adds +0 to every channel, so it is skipped.
          if (!a) {
            continue;
          }
          const double weight =
              column_weights[rx.offset + static_cast<std::size_t>(sx - rx.first)] * wy / area;
          const double alpha = unit[a];
          for (int channel = 0; channel < 3; ++channel) {
            channels[channel] += pixels[src + static_cast<std::size_t>(channel)] * alpha * weight;
          }
          channels[3] += a * weight;
        }
      }
      const auto dst = static_cast<std::size_t>(((y + margin) * result.width + x + margin) * 4);
      if (channels[3] > 0) {
        for (int channel = 0; channel < 3; ++channel) {
          result.pixels[dst + static_cast<std::size_t>(channel)] = static_cast<unsigned char>(
              std::clamp(round_up(channels[channel] * 255 / channels[3]) - subtract, 0L, 255L));
        }
      }
      result.pixels[dst + 3] = static_cast<unsigned char>(std::min(round_up(channels[3]), 255L));
    }
  }

  // Black silhouette outline and a soft halo; original pixels stay crisp.
  const int rw = result.width;
  const int rh = result.height;
  const auto count = static_cast<std::size_t>(rw) * static_cast<std::size_t>(rh);
  static thread_local std::vector<unsigned char> alpha_plane;
  static thread_local std::vector<unsigned char> row_max;
  static thread_local std::vector<unsigned char> outline_plane;
  static thread_local std::vector<double> halo;
  static thread_local std::vector<double> next;
  alpha_plane.resize(count);
  row_max.resize(count);
  outline_plane.resize(count);
  halo.resize(count);
  next.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    alpha_plane[i] = result.pixels[i * 4 + 3];
    halo[i] = alpha_plane[i];
  }
  // Two seven-pixel box passes per axis form the separable triangular halo.
  for (int pass = 0; pass < 4; ++pass) {
    const bool vertical = pass >= 2;
    const int rows = vertical ? rw : rh;
    const int columns = vertical ? rh : rw;
    const auto at = [&](int row, int column) {
      return static_cast<std::size_t>(vertical ? column * rw + row : row * rw + column);
    };
    for (int row = 0; row < rows; ++row) {
      double sum = 0;
      for (int column = 0; column < std::min(3, columns); ++column) {
        sum += halo[at(row, column)];
      }
      for (int column = 0; column < columns; ++column) {
        if (column + 3 < columns) {
          sum += halo[at(row, column + 3)];
        }
        if (column - 4 >= 0) {
          sum -= halo[at(row, column - 4)];
        }
        next[at(row, column)] = sum / 7;
      }
    }
    halo.swap(next);
  }

  // 3x3 maximum of the original alpha as two 1-D passes (exact for a rectangle clipped to the
  // image).
  for (int y = 0; y < rh; ++y) {
    for (int x = 0; x < rw; ++x) {
      unsigned char maximum = alpha_plane[static_cast<std::size_t>(y * rw + x)];
      if (x > 0) {
        maximum = std::max(maximum, alpha_plane[static_cast<std::size_t>(y * rw + x - 1)]);
      }
      if (x + 1 < rw) {
        maximum = std::max(maximum, alpha_plane[static_cast<std::size_t>(y * rw + x + 1)]);
      }
      row_max[static_cast<std::size_t>(y * rw + x)] = maximum;
    }
  }
  for (int y = 0; y < rh; ++y) {
    for (int x = 0; x < rw; ++x) {
      unsigned char maximum = row_max[static_cast<std::size_t>(y * rw + x)];
      if (y > 0) {
        maximum = std::max(maximum, row_max[static_cast<std::size_t>((y - 1) * rw + x)]);
      }
      if (y + 1 < rh) {
        maximum = std::max(maximum, row_max[static_cast<std::size_t>((y + 1) * rw + x)]);
      }
      outline_plane[static_cast<std::size_t>(y * rw + x)] = maximum;
    }
  }
  for (std::size_t i = 0; i < count; ++i) {
    const auto dst = i * 4;
    const double alpha = unit[alpha_plane[i]];
    const double backdrop = std::max(outline_plane[i] * .8, glow ? halo[i] * .6 : 0) / 255.0;
    const double output = alpha + backdrop * (1 - alpha);
    result.pixels[dst + 3] = static_cast<unsigned char>(std::min(round_up(output * 255), 255L));
    if (output > 0) {
      for (int channel = 0; channel < 3; ++channel) {
        result.pixels[dst + static_cast<std::size_t>(channel)] = static_cast<unsigned char>(
            round_up(result.pixels[dst + static_cast<std::size_t>(channel)] * alpha / output));
      }
    }
  }
  return result;
}

Image Appearances::compose_reference(const Image& source, const Image* mask, int size, bool pad,
                                     std::uint32_t tex1, std::uint32_t tex2) const {
  // The original implementation, kept verbatim as the differential oracle for `compose`.
  // Texturing is done on a 5x bitmap before size scaling. Textile origin is
  // the frame origin, including transparent extended-attack padding.
  const bool dyed = mask && (tex1 || tex2);
  Image dyed_source;
  const Image* input = &source;
  if (dyed) {
    dyed_source = {source.width * 5, source.height * 5,
                   std::vector<unsigned char>(static_cast<std::size_t>(source.width * 5) *
                                              static_cast<std::size_t>(source.height * 5) * 4)};
    Image textiles[2];
    const std::uint32_t ops[2] = {tex1, tex2};
    for (int channel = 0; channel < 2; ++channel) {
      const auto kind = ops[channel] >> 24U;
      if (kind == 4 || kind == 5 || kind == 9 || kind == 10) {
        const auto name = "textile" + std::to_string(kind) + "x" + std::to_string(kind);
        for (std::size_t sheet = 0; sheet < flash::sheets.size(); ++sheet) {
          if (flash::sheets[sheet].name == name) {
            textiles[channel] =
                sprite(static_cast<int>(sheet), static_cast<int>(ops[channel] & 0xffffffU));
          }
        }
      }
    }
    for (int y = 0; y < dyed_source.height; ++y) {
      for (int x = 0; x < dyed_source.width; ++x) {
        const auto src = static_cast<std::size_t>((y / 5 * source.width + x / 5) * 4);
        const auto dst = static_cast<std::size_t>((y * dyed_source.width + x) * 4);
        std::copy_n(source.pixels.data() + src, 4, dyed_source.pixels.data() + dst);
        if (x / 5 >= mask->width || y / 5 >= mask->height) {
          continue;
        }
        const auto m = static_cast<std::size_t>((y / 5 * mask->width + x / 5) * 4);
        if (!mask->pixels[m + 3]) {
          continue;
        }
        const int c = mask->pixels[m] > 0 ? 0 : mask->pixels[m + 1] > 0 ? 1 : -1;
        if (c < 0 || ops[c] == 0) {
          continue;
        }
        const auto& tile = textiles[c];
        for (int k = 0; k < 3; ++k) {
          const auto color = tile.pixels.empty()
                                 ? static_cast<unsigned char>(
                                       (ops[c] >> 24U) == 1 ? (ops[c] >> (16 - 8 * k)) & 255U : 0)
                                 : tile.pixels[static_cast<std::size_t>(
                                       (y % tile.height * tile.width + x % tile.width) * 4 + k)];
          dyed_source.pixels[dst + static_cast<std::size_t>(k)] = static_cast<unsigned char>(
              (static_cast<unsigned>(color) * mask->pixels[m + static_cast<std::size_t>(c)] +
               127U) /
              255U);
        }
        dyed_source.pixels[dst + 3] = 255;
      }
    }
    input = &dyed_source;
  }
  // Raster bounds protect against hostile size stats; density tracks the
  // reduction so world placement still preserves the authoritative size.
  const double factor = 5 * std::max(1, dyed ? size / 5 * 5 : size) / 100.0;
  const int wanted_w = std::max(1, static_cast<int>(factor * source.width));
  const int wanted_h = std::max(1, static_cast<int>(factor * source.height));
  const double density = std::min(1.0, 1024.0 / std::max(wanted_w, wanted_h));
  const int w = std::max(1, static_cast<int>(wanted_w * density));
  const int h = std::max(1, static_cast<int>(wanted_h * density));
  Image result{w + 24, h + (pad ? 24 : 13),
               std::vector<unsigned char>(static_cast<std::size_t>(w + 24) *
                                          static_cast<std::size_t>(h + (pad ? 24 : 13)) * 4)};
  result.pixel_scale = density;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const auto dst = static_cast<std::size_t>(((y + 12) * result.width + x + 12) * 4);
      const double x0 = w >= input->width ? std::floor((x + .49999) * input->width / w)
                                          : static_cast<double>(x) * input->width / w;
      const double x1 = w >= input->width ? x0 + 1 : static_cast<double>(x + 1) * input->width / w;
      const double y0 = h >= input->height ? std::floor((y + .49999) * input->height / h)
                                           : static_cast<double>(y) * input->height / h;
      const double y1 =
          h >= input->height ? y0 + 1 : static_cast<double>(y + 1) * input->height / h;
      double channels[4] = {};
      for (int sy = static_cast<int>(y0);
           sy < std::min(input->height, static_cast<int>(std::ceil(y1))); ++sy) {
        for (int sx = static_cast<int>(x0);
             sx < std::min(input->width, static_cast<int>(std::ceil(x1))); ++sx) {
          const double weight = (std::min(x1, sx + 1.0) - std::max(x0, static_cast<double>(sx))) *
                                (std::min(y1, sy + 1.0) - std::max(y0, static_cast<double>(sy))) /
                                ((x1 - x0) * (y1 - y0));
          const auto src = static_cast<std::size_t>((sy * input->width + sx) * 4);
          const double alpha = input->pixels[src + 3] / 255.0;
          for (int k = 0; k < 3; ++k) {
            channels[k] += input->pixels[src + static_cast<std::size_t>(k)] * alpha * weight;
          }
          channels[3] += input->pixels[src + 3] * weight;
        }
      }
      const int subtract = static_cast<int>(40 * std::max(0.0, 2.0 * (y + 12) / result.height - 1));
      if (channels[3] > 0) {
        for (int k = 0; k < 3; ++k) {
          result.pixels[dst + static_cast<std::size_t>(k)] = static_cast<unsigned char>(
              std::clamp(std::lround(channels[k] * 255 / channels[3]) - subtract, 0L, 255L));
        }
      }
      result.pixels[dst + 3] =
          static_cast<unsigned char>(std::clamp(std::lround(channels[3]), 0L, 255L));
    }
  }
  // Black silhouette outline and a soft halo; original pixels stay crisp.
  const auto original = result;
  std::vector<double> halo(static_cast<std::size_t>(result.width) *
                           static_cast<std::size_t>(result.height));
  for (std::size_t i = 0; i < halo.size(); ++i) {
    halo[i] = original.pixels[i * 4 + 3];
  }
  // Two seven-pixel box passes per axis form the separable triangular halo.
  for (int pass = 0; pass < 4; ++pass) {
    std::vector<double> next(halo.size());
    const bool vertical = pass >= 2;
    const int rows = vertical ? result.width : result.height;
    const int columns = vertical ? result.height : result.width;
    const auto at = [&](int row, int column) {
      return static_cast<std::size_t>(vertical ? column * result.width + row
                                               : row * result.width + column);
    };
    for (int row = 0; row < rows; ++row) {
      double sum = 0;
      for (int c = 0; c < std::min(3, columns); ++c) {
        sum += halo[at(row, c)];
      }
      for (int c = 0; c < columns; ++c) {
        if (c + 3 < columns) {
          sum += halo[at(row, c + 3)];
        }
        if (c - 4 >= 0) {
          sum -= halo[at(row, c - 4)];
        }
        next[at(row, c)] = sum / 7;
      }
    }
    halo = std::move(next);
  }
  for (int y = 0; y < result.height; ++y) {
    for (int x = 0; x < result.width; ++x) {
      const auto dst = static_cast<std::size_t>((y * result.width + x) * 4);
      int outline = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int sx = x + dx;
          const int sy = y + dy;
          if (sx < 0 || sy < 0 || sx >= result.width || sy >= result.height) {
            continue;
          }
          outline = std::max(
              outline,
              static_cast<int>(
                  original.pixels[static_cast<std::size_t>((sy * result.width + sx) * 4 + 3)]));
        }
      }
      const double a = original.pixels[dst + 3] / 255.0;
      const double b = std::max(outline * .8, halo[dst / 4] * .6) / 255.0;
      const double out = a + b * (1 - a);
      result.pixels[dst + 3] =
          static_cast<unsigned char>(std::clamp(std::lround(out * 255), 0L, 255L));
      if (out > 0) {
        for (int k = 0; k < 3; ++k) {
          result.pixels[dst + static_cast<std::size_t>(k)] = static_cast<unsigned char>(
              std::lround(original.pixels[dst + static_cast<std::size_t>(k)] * a / out));
        }
      }
    }
  }
  return result;
}

std::size_t Appearances::KeyHash::operator()(const Key& key) const noexcept {
  std::uint64_t h = 1469598103934665603ULL;
  const auto mix = [&](std::uint64_t value) {
    h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
  };
  mix(static_cast<std::uint32_t>(key.a));
  mix(static_cast<std::uint32_t>(key.b));
  mix(static_cast<std::uint32_t>(key.c));
  mix(static_cast<std::uint32_t>(key.d));
  mix(static_cast<std::uint32_t>(key.size));
  mix(key.tex1);
  mix(key.tex2);
  mix(static_cast<std::uint64_t>(key.kind) << 8U | key.pad);
  return static_cast<std::size_t>(h * 0x2545F4914F6CDD1DULL);
}

template <class Make>
std::shared_ptr<const Image> Appearances::get(const Key& key, Make&& make) const {
  REALM_ZONE("appearance.lookup");
  if (const auto it = cache_.find(key); it != cache_.end()) {
    telemetry::add(telemetry::Counter::appearance_hits);
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    return it->second.image;
  }

  Image owned_source;
  Image owned_mask;
  const Image* source = nullptr;
  const Image* mask = nullptr;
  bool normalize = false;
  make(owned_source, owned_mask, source, mask, normalize);
  Image normalized_source;
  Image normalized_mask;
  const Image* input = source;
  if (normalize) {
    normalized_source = *source;
    normalize_flash_png(normalized_source);
    input = &normalized_source;
    if (mask) {
      normalized_mask = *mask;
      normalize_flash_png(normalized_mask);
      mask = &normalized_mask;
    }
  }
  telemetry::add(telemetry::Counter::appearance_misses);
  auto image =
      std::make_shared<const Image>(compose(*input, mask, key.size, key.pad, key.tex1, key.tex2));
  while (!cache_.empty() &&
         (cache_.size() >= budget_entries_ || bytes_ + image->pixels.size() > budget_bytes_)) {
    const auto it = cache_.find(lru_.back());
    bytes_ -= it->second.image->pixels.size();
    cache_.erase(it);
    lru_.pop_back();
    ++evictions_;
    telemetry::add(telemetry::Counter::appearance_evictions);
  }
  bytes_ += image->pixels.size();
  lru_.push_front(key);
  cache_.emplace(key, Entry{image, lru_.begin()});
  return image;
}

void Appearances::set_budget(std::size_t entries, std::size_t bytes) const {
  budget_entries_ = entries;
  budget_bytes_ = bytes;
  while (!cache_.empty() && (cache_.size() > budget_entries_ || bytes_ > budget_bytes_)) {
    const auto it = cache_.find(lru_.back());
    bytes_ -= it->second.image->pixels.size();
    cache_.erase(it);
    lru_.pop_back();
    ++evictions_;
    telemetry::add(telemetry::Counter::appearance_evictions);
  }
}

Appearances::Key Appearances::animated_key(AnimationFrame frame, int size, bool pad,
                                           std::uint32_t tex1, std::uint32_t tex2) const {
  return {static_cast<std::int32_t>(frame.image),
          static_cast<std::int32_t>(frame.mask),
          0,
          0,
          size,
          tex1,
          tex2,
          1,
          static_cast<std::uint8_t>(pad)};
}

Appearances::Key Appearances::texture_key(const TextureDef& definition, std::uint32_t id, int size,
                                          bool pad, std::uint32_t tex1, std::uint32_t tex2) const {
  const auto& selected = definition.select(id);
  if (const auto* character = animations_.character(selected.animated, selected.index)) {
    return animated_key(character->frame(0, 0, Action::stand, 0), size, pad, tex1, tex2);
  }
  return {selected.sheet,
          selected.index,
          selected.mask_sheet,
          selected.mask_index,
          size,
          tex1,
          tex2,
          0,
          static_cast<std::uint8_t>(pad)};
}

std::shared_ptr<const Image> Appearances::image(const Key& key) const {
  return get(key, [&](Image& source, Image& mask, const Image*& source_ref, const Image*& mask_ref,
                      bool& normalize) {
    if (key.kind == 1) {
      source_ref = &animations_.images().at(static_cast<std::size_t>(key.a));
      mask_ref = key.b < 0 ? nullptr : &animations_.images().at(static_cast<std::size_t>(key.b));
      normalize = true;
      return;
    }
    source = sprite(key.a, key.b);
    source_ref = &source;
    if (key.c >= 0) {
      mask = sprite(key.c, key.d);
      mask_ref = &mask;
    }
  });
}

std::shared_ptr<const Image> Appearances::animated(AnimationFrame frame, int size, bool pad,
                                                   std::uint32_t tex1, std::uint32_t tex2) const {
  return image(animated_key(frame, size, pad, tex1, tex2));
}

std::shared_ptr<const Image> Appearances::texture(const TextureDef& definition, std::uint32_t id,
                                                  int size, bool pad, std::uint32_t tex1,
                                                  std::uint32_t tex2) const {
  return image(texture_key(definition, id, size, pad, tex1, tex2));
}

std::shared_ptr<const Image> Appearances::portrait(const Catalog& catalog, int type, int skin,
                                                   std::uint32_t tex1, std::uint32_t tex2) const {
  const auto& selected = catalog.player_texture(type, skin).select(0);
  if (const auto* character = animations_.character(selected.animated, selected.index)) {
    const auto frame = character->frame(0, 0, Action::stand, 0);
    return animated(frame, 400 / animations_.images().at(frame.image).width, true, tex1, tex2);
  }
  return texture(selected, 0, 50, true, tex1, tex2);
}

void Appearances::clear() const {
  cache_.clear();
  lru_.clear();
  bytes_ = 0;
}
}
