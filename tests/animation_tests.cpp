#include "realm/animation.hpp"
#include "flash_assets.hpp"
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace {
int checks{};
void check(bool ok, const char* why) {
  ++checks;
  if (!ok) throw std::runtime_error(why);
}
void pixel(realm::Image& image, int x, int y, unsigned char value) {
  auto i = static_cast<std::size_t>((y * image.width + x) * 4);
  image.pixels[i] = value;
  image.pixels[i + 3] = 255;
}
unsigned char red(const realm::Image& image, int x, int y) {
  return image.pixels[static_cast<std::size_t>((y * image.width + x) * 4)];
}
}
int main() {
  try {
    realm::AnimationLibrary library;
    realm::Image strip{56, 24, std::vector<unsigned char>(56 * 24 * 4)};
    for (int row = 0; row < 3; ++row) {
      pixel(strip, 0, row * 8, static_cast<unsigned char>(10 + row));
      pixel(strip, 8, row * 8, static_cast<unsigned char>(20 + row));
    }
    pixel(strip, 40, 0, 40);
    pixel(strip, 48, 0, 50);
    auto character = library.build(strip, nullptr, 8, 8, 0);
    const auto& images = library.images();
    check(red(images[character.directions[0][0][0].image], 0, 0) == 10, "right stand");
    check(red(images[character.directions[1][0][0].image], 7, 0) == 10, "left stand mirror");
    check(character.directions[0][1].size() == 2, "walk frame count");
    check(red(images[character.directions[0][1][1].image], 0, 0) == 10, "non-symmetric walk fallback to stand");
    check(red(images[character.directions[2][1][1].image], 6, 0) == 21, "symmetric walk mirrors first seven pixels");
    check(red(images[character.directions[2][1][1].image], 7, 0) == 0,
          "symmetric fallback leaves eighth pixel transparent");
    check(character.directions[0][2].size() == 1, "single attack frame");
    const auto& attack = images[character.directions[0][2][0].image];
    check(attack.width == 24 && red(attack, 0, 0) == 0 && red(attack, 8, 0) == 40 && red(attack, 16, 0) == 50,
          "extended attack composition");
    const auto& left_attack = images[character.directions[1][2][0].image];
    check(red(left_attack, 15, 0) == 40 && red(left_attack, 7, 0) == 50, "extended attack mirrors entire image");
    check(character.directions[3][2][0].image == character.directions[3][1][0].image,
          "empty attack falls back to walk");
    check(character.frame(0, 0, realm::Action::stand, 0).image == character.directions[0][0][0].image, "right facing");
    check(
        character.frame(std::numbers::pi / 2, 0, realm::Action::stand, 0).image == character.directions[2][0][0].image,
        "down facing");
    check(
        character.frame(0, std::numbers::pi / 2, realm::Action::stand, 0).image == character.directions[3][0][0].image,
        "camera-relative up facing");
    check(character.frame(0, 0, realm::Action::walk, 1).image == character.directions[0][1][1].image,
          "phase clamped to last frame");
    realm::AnimationLibrary actual(realm::flash::source_root);
    std::size_t characters{};
    for (std::size_t s = 0; s < realm::flash::animated.size(); ++s) {
      for (int i = 0; const auto* c = actual.character(static_cast<int>(s), i); ++i) {
        ++characters;
        for (int dir = 0; dir < 8; ++dir)
          for (auto action : {realm::Action::stand, realm::Action::walk, realm::Action::attack}) {
            const auto f = c->frame(dir * std::numbers::pi / 4, 0, action, .75);
            check(f.image < actual.images().size(), "actual animation frame handle");
            if (f.mask >= 0)
              check(actual.images()[static_cast<std::size_t>(f.mask)].width == actual.images()[f.image].width,
                    "actual mask matches composed image");
          }
      }
    }
    std::cout << characters << " characters, " << actual.images().size() << " images; " << checks
              << " animation checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
