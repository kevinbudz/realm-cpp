#include "realm/assets/AnimationLibrary.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace realm {
namespace {
Image blank(int width, int height) {
  return {width, height,
          std::vector<unsigned char>(static_cast<std::size_t>(width) *
                                     static_cast<std::size_t>(height) * 4)};
}

bool empty(const Image& image) {
  for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
    if (image.pixels[i]) {
      return false;
    }
  }
  return true;
}

void copy(const Image& source, Image& target, int x) {
  for (int y = 0; y < source.height; ++y) {
    std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(y * source.width * 4),
                source.width * 4,
                target.pixels.begin() + static_cast<std::ptrdiff_t>((y * target.width + x) * 4));
  }
}
}

Image crop(const Image& source, int x, int y, int width, int height) {
  if (width <= 0 || height <= 0 || x < 0 || y < 0 || x + width > source.width ||
      y + height > source.height) {
    throw std::invalid_argument("Invalid sprite crop");
  }

  auto result = blank(width, height);
  for (int row = 0; row < height; ++row) {
    std::copy_n(
        source.pixels.begin() + static_cast<std::ptrdiff_t>(((y + row) * source.width + x) * 4),
        width * 4, result.pixels.begin() + static_cast<std::ptrdiff_t>(row * width * 4));
  }
  return result;
}

Image mirror(const Image& source, int width) {
  if (!width) {
    width = source.width;
  }
  if (width < 0 || width > source.width) {
    throw std::invalid_argument("Invalid mirror width");
  }

  auto result = blank(source.width, source.height);
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < width; ++x) {
      std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>((y * source.width + x) * 4),
                  4,
                  result.pixels.begin() +
                      static_cast<std::ptrdiff_t>((y * source.width + width - x - 1) * 4));
    }
  }
  return result;
}

AnimationFrame Character::frame(double facing, double camera_angle, Action action,
                                double phase) const {
  constexpr int sectors[8][3] = {{1, 3, 2},  {3, 1, 2},  {3, 0, 2},  {0, 3, 2},
                                 {0, 2, -1}, {2, 0, -1}, {2, 1, -1}, {1, 2, -1}};
  if (!std::isfinite(facing) || !std::isfinite(camera_angle) || !std::isfinite(phase)) {
    throw std::invalid_argument("Invalid animation phase/angle");
  }

  double angle = facing - camera_angle;
  // Keep the inclusive [-PI, PI] boundary, including +PI.
  if (angle > std::numbers::pi) {
    angle -=
        std::ceil((angle - std::numbers::pi) / (2 * std::numbers::pi)) * (2 * std::numbers::pi);
  }
  if (angle < -std::numbers::pi) {
    angle +=
        std::ceil((-std::numbers::pi - angle) / (2 * std::numbers::pi)) * (2 * std::numbers::pi);
  }
  const int sector = static_cast<int>(angle / (std::numbers::pi / 4) + 4) % 8;
  const int index = static_cast<int>(action);
  if (index < 0 || index >= 3) {
    throw std::invalid_argument("Invalid animation action");
  }

  for (const int direction : sectors[sector]) {
    if (direction < 0) {
      continue;
    }
    const auto& frames =
        directions[static_cast<std::size_t>(direction)][static_cast<std::size_t>(index)];
    if (!frames.empty()) {
      return frames[static_cast<std::size_t>(std::clamp(phase, 0.0, .99999) *
                                             static_cast<double>(frames.size()))];
    }
  }
  throw std::runtime_error("Animation has no available direction");
}

Character AnimationLibrary::build(const Image& strip, const Image* mask, int width, int height,
                                  int first) {
  if (width < 7 || height <= 0 || strip.width != width * 7 || strip.height % height ||
      (first != 0 && first != 2)) {
    throw std::invalid_argument("Invalid character strip");
  }
  if (mask && (mask->width != strip.width || mask->height != strip.height)) {
    throw std::invalid_argument("Animation mask dimensions differ");
  }

  struct Frame {
    Image image;
    Image mask;
    bool has_mask{};
  };

  auto get = [&](int offset) {
    const int x = offset % 7 * width;
    const int y = offset / 7 * height;
    Frame frame{crop(strip, x, y, width, height), {}, mask != nullptr};
    if (mask) {
      frame.mask = crop(*mask, x, y, width, height);
    }
    return frame;
  };
  auto flipped = [](Frame frame, int width = 0) {
    frame.image = mirror(frame.image, width);
    if (frame.has_mask) {
      frame.mask = mirror(frame.mask, width);
    }
    return frame;
  };
  auto store = [&](const Frame& frame) {
    AnimationFrame handle{images_.size(), -1};
    images_.push_back(frame.image);
    if (frame.has_mask) {
      handle.mask = static_cast<int>(images_.size());
      images_.push_back(frame.mask);
    }
    return handle;
  };
  Character result;
  auto direction = [&](int dir, int offset, bool flip, bool symmetric) {
    const auto stand = get(offset);
    const auto walk1 = get(offset + 1);
    const auto walk2 = get(offset + 2);
    const auto attack1 = get(offset + 4);
    auto attack2 = get(offset + 5);
    const auto sword = get(offset + 6);

    if (!empty(attack2.image) && !empty(sword.image)) {
      Frame extended{blank(width * 3, height), {}, attack2.has_mask || sword.has_mask};
      copy(attack2.image, extended.image, width);
      copy(sword.image, extended.image, width * 2);
      if (extended.has_mask) {
        extended.mask = blank(width * 3, height);
        if (attack2.has_mask) {
          copy(attack2.mask, extended.mask, width);
        }
        if (sword.has_mask) {
          copy(sword.mask, extended.mask, width * 2);
        }
      }
      attack2 = std::move(extended);
    }
    auto add = [&](Action action, const Frame& frame) {
      result.directions[static_cast<std::size_t>(dir)][static_cast<std::size_t>(action)].push_back(
          store(flip ? flipped(frame) : frame));
    };

    add(Action::stand, stand);
    add(Action::walk, walk1);
    if (!empty(walk2.image)) {
      add(Action::walk, walk2);
    } else if (symmetric) {
      result.directions[static_cast<std::size_t>(dir)][1].push_back(
          store(!flip ? flipped(walk1, 7) : walk1));
    } else {
      add(Action::walk, stand);
    }
    if (empty(attack1.image) && empty(attack2.image)) {
      result.directions[static_cast<std::size_t>(dir)][2] =
          result.directions[static_cast<std::size_t>(dir)][1];
    } else {
      if (!empty(attack1.image)) {
        add(Action::attack, attack1);
      }
      if (!empty(attack2.image)) {
        add(Action::attack, attack2);
      }
    }
  };
  const int rows = strip.height / height;
  if (first == 0) {
    direction(0, 0, false, false);
    direction(1, 0, true, false);
    if (rows >= 2) {
      direction(2, 7, false, true);
    }
  } else {
    direction(2, 0, false, true);
    if (rows >= 2) {
      direction(0, 7, false, false);
      direction(1, 7, true, false);
    }
  }
  if (rows >= 3) {
    direction(3, 14, false, true);
  }
  return result;
}

FlashImages::FlashImages(const std::filesystem::path& root)
    : raw(flash::assets.size()), normalized(flash::assets.size()) {
  for (const auto& sheet : flash::animated) {
    for (const int asset : {sheet.asset, sheet.mask}) {
      if (asset >= 0 && raw.at(static_cast<std::size_t>(asset)).pixels.empty()) {
        raw[static_cast<std::size_t>(asset)] =
            load_png(root / flash::assets.at(static_cast<std::size_t>(asset)).path);
      }
    }
  }
  for (std::size_t asset = 0; asset < flash::assets.size(); ++asset) {
    normalized[asset] = raw[asset].pixels.empty() ? load_png(root / flash::assets[asset].path)
                                                  : raw[asset];
    normalize_flash_png(normalized[asset]);
  }
}

AnimationLibrary::AnimationLibrary(const std::filesystem::path& root)
    : AnimationLibrary(FlashImages(root)) {}

AnimationLibrary::AnimationLibrary(const FlashImages& pngs) {
  sheets_.reserve(flash::animated.size());
  for (const auto& sheet : flash::animated) {
    const auto& image = pngs.raw.at(static_cast<std::size_t>(sheet.asset));
    const Image* mask =
        sheet.mask >= 0 ? &pngs.raw.at(static_cast<std::size_t>(sheet.mask)) : nullptr;

    std::vector<Character> characters;
    for (int y = 0; y + sheet.strip_height <= image.height; y += sheet.strip_height) {
      for (int x = 0; x + sheet.strip_width <= image.width; x += sheet.strip_width) {
        const auto strip = crop(image, x, y, sheet.strip_width, sheet.strip_height);
        const auto mask_strip =
            mask ? crop(*mask, x, y, sheet.strip_width, sheet.strip_height) : Image{};
        characters.push_back(build(strip, sheet.mask >= 0 ? &mask_strip : nullptr, sheet.width,
                                   sheet.height, sheet.first));
      }
    }
    sheets_.push_back(std::move(characters));
  }
}

const Character* AnimationLibrary::character(int sheet, int index) const {
  if (sheet < 0 || index < 0 || static_cast<std::size_t>(sheet) >= sheets_.size() ||
      static_cast<std::size_t>(index) >= sheets_[static_cast<std::size_t>(sheet)].size()) {
    return nullptr;
  }
  return &sheets_[static_cast<std::size_t>(sheet)][static_cast<std::size_t>(index)];
}
}
