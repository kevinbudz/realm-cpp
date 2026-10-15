#include "realm/map/Terrain.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
int checks{};

void check(bool ok, const char* why) {
  ++checks;
  if (!ok) {
    throw std::runtime_error(why);
  }
}
}

int main() {
  try {
    realm::Catalog catalog(realm::flash::source_root);
    realm::World world(catalog);
    world.load_jm(std::filesystem::path(realm::flash::source_root).parent_path() /
                  "realm-server/Resources/Worlds/Nexus/0.jm");
    realm::Terrain reference(world, realm::flash::source_root, 0, 0),
        live(world, realm::flash::source_root, 0, 0, true);
    live.begin_frame();
    for (int y = 0; y < world.height(); ++y) {
      for (int x = 0; x < world.width(); ++x) {
        if (world.tile(x, y)->type == 255) {
          continue;
        }
        const auto a = reference.tile(x, y), b = live.tile(x, y);
        check(reference.images().at(a.image).pixels == live.images().at(b.image).pixels,
              "live terrain differs from offline compositor");
        check(a.offset.x == b.offset.x && a.offset.y == b.offset.y && a.sink == b.sink,
              "live terrain presentation metadata differs");
      }
    }
    check(!live.changes().empty(), "initial visible terrain must upload");
    live.begin_frame();
    for (int y = 0; y < world.height(); ++y) {
      for (int x = 0; x < world.width(); ++x) {
        if (world.tile(x, y)->type != 255) {
          (void)live.tile(x, y);
        }
      }
    }
    check(live.changes().empty(), "unchanged terrain must not upload");
    world.initialize(4, 4);
    std::vector<const realm::GroundDef*> blends;
    for (const auto& g : catalog.grounds()) {
      if (g.blend_priority >= 0 && g.edge.sheet < 0 && g.edge.random.empty() &&
          g.texture.sheet >= 0) {
        blends.push_back(&g);
      }
    }
    std::sort(blends.begin(), blends.end(),
              [](const auto* a, const auto* b) { return a->blend_priority < b->blend_priority; });
    check(blends.size() > 10, "transition stress fixtures exist");
    std::uint32_t random = 123;
    auto fill = [&] {
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          random = random * 1664525U + 1013904223U;
          world.tile(x, y)->type = blends[1 + random % (blends.size() - 1)]->type;
        }
      }
      world.tile(1, 1)->type = blends.front()->type;
    };
    for (int n = 0; n < 12000; ++n) {
      live.begin_frame();
      fill();
      const auto a = live.tile(1, 1);
      const auto pixels = live.images().at(a.image).pixels;
      std::array<std::uint16_t, 16> saved;
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          saved[static_cast<std::size_t>(x + 4 * y)] = world.tile(x, y)->type;
        }
      }
      fill();
      (void)live.tile(1, 1);
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          world.tile(x, y)->type = saved[static_cast<std::size_t>(x + 4 * y)];
        }
      }
      const auto pinned = live.tile(1, 1);
      check(pinned.image == a.image && live.images().at(a.image).pixels == pixels,
            "cache evicted a texture pinned by current frame");
    }
    check(live.cache_evictions() > 1000, "stress exercise must cross bounded cache capacity");
    check(live.images().size() == 8192, "live terrain image storage must remain bounded");
    realm::Terrain final_reference(world, realm::flash::source_root, 0, 0);
    const auto a = final_reference.tile(1, 1), b = live.tile(1, 1);
    check(final_reference.images().at(a.image).pixels == live.images().at(b.image).pixels,
          "eviction/reuse corrupted composition");
    std::cout << checks << " terrain/cache checks passed, evictions=" << live.cache_evictions()
              << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
