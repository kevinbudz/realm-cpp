#include "realm/assets/HudArt.hpp"
#include "realm/objects/Catalog.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int checks{};

void check(bool value, const char* reason) {
  ++checks;
  if (!value) {
    throw std::runtime_error(reason);
  }
}

// Center of the opaque (alpha >= 128) extent of an icon, in edge coordinates: a 56-pixel icon
// centered on its tile is 28.
struct Center {
  double x{}, y{};
};

template <typename Inside>
Center center_of(const realm::Image& image, Inside inside) {
  int left = image.width, right = 0, top = image.height, bottom = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (inside(&image.pixels[static_cast<std::size_t>((y * image.width + x) * 4)])) {
        left = std::min(left, x);
        right = std::max(right, x + 1);
        top = std::min(top, y);
        bottom = std::max(bottom, y + 1);
      }
    }
  }
  return {(left + right) / 2.0, (top + bottom) / 2.0};
}

// Opaque pixels of an item icon.
Center opaque_center(const realm::Image& image) {
  return center_of(image, [](const unsigned char* p) { return p[3] >= 128; });
}

// Opaque silhouette pixels, tinted exactly RGB 54 on the RGB 69 tile. The tile's antialiased
// corners are not tinted.
Center dark_center(const realm::Image& image) {
  return center_of(image, [](const unsigned char* p) {
    return p[0] == 54 && p[1] == 54 && p[2] == 54 && p[3] == 255;
  });
}
}

int main() {
  try {
    using namespace realm;
    Catalog catalog(flash::source_root);
    HudArt art;
    const auto icon = [&](std::string_view name, const std::string& prefix = "item") {
      const auto* object = catalog.object(name);
      if (!object) {
        throw std::runtime_error("missing item " + std::string(name));
      }
      return art.image(prefix + std::to_string(object->type));
    };

    // Ring of Attack fills 7 of its 8 source columns, so the cell-based art sat half a cell right
    // of center.
    const auto ring = icon("Ring of Attack");
    const auto ring_center = opaque_center(ring);
    check(std::abs(ring_center.x - ring.width / 2.0) <= 0.5,
          "7-wide ring is centered horizontally in its tile");
    check(std::abs(ring_center.y - ring.height / 2.0) <= 0.5,
          "7-wide ring keeps its vertical placement");
    check(std::abs(opaque_center(icon("Ring of Attack", "dim")).x - ring.width / 2.0) <= 0.5,
          "dimmed ring shares the centered placement");

    // Fire Spray Spell is centered horizontally but sits low in its cell: the change is horizontal
    // only.
    const auto spell = icon("Fire Spray Spell");
    const auto spell_center = opaque_center(spell);
    check(std::abs(spell_center.x - spell.width / 2.0) <= 0.5,
          "spell with centered columns stays centered");
    check(std::abs(spell_center.y - (spell.height / 2.0 + 2)) <= 0.5,
          "spell keeps its low vertical placement");

    // Amulet of Resurrection's columns are already centered, so its placement must not move.
    const auto amulet = icon("Amulet of Resurrection");
    check(std::abs(opaque_center(amulet).x - amulet.width / 2.0) <= 0.5,
          "centered sprite is unchanged");

    // Equipped silhouettes are drawn 4x from the cell origin on a 40-pixel tile, so the tile center
    // is 20.
    const auto silhouette = [&](int slot_type) {
      return art.image("empty" + std::to_string(slot_type) + "p0");
    };

    // The ring silhouette's 7 columns sit half a cell right of center, as its item icon does; its
    // rows are unchanged.
    const auto ring_tile = silhouette(9);
    const auto ring_silhouette = dark_center(ring_tile);
    check(std::abs(ring_silhouette.x - ring_tile.width / 2.0) <= 0.5,
          "7-wide ring silhouette is centered in its equipped tile");
    check(std::abs(ring_silhouette.y - (ring_tile.height / 2.0 + 2)) <= 0.5,
          "ring silhouette keeps its vertical placement");

    const auto type22_tile = silhouette(22);
    check(std::abs(dark_center(type22_tile).x - type22_tile.width / 2.0) <= 0.5,
          "7-wide type 22 silhouette is centered in its equipped tile");

    const auto sword_tile = silhouette(1);
    check(std::abs(dark_center(sword_tile).x - sword_tile.width / 2.0) <= 0.5,
          "centered silhouette is unchanged");

    std::cout << checks << " item icon checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
