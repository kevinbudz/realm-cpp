#include "realm/hud/Hud.hpp"
#include "realm/ui/dialogs/InteractionDialog.hpp"
#include "realm/minimap/Minimap.hpp"
#include "realm/ui/tooltip/PlayerTooltip.hpp"
#include "realm/ui/tooltip/ItemTooltip.hpp"
#include "flash_assets.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
void check(bool ok, const char* reason) {
  if (!ok) {
    throw std::runtime_error(reason);
  }
}
}

int main() {
  try {
    using namespace realm;
    // Check physical clipping and blending at a fractional drawable density.
    auto surface = ui_image(10, 10, 1.25);
    ui_rectangle(surface, -2, -2, 6, 6, 0x123456);
    check(surface.width == 13 && surface.height == 13, "fractional surfaces round up");
    check(surface.pixels[3] == 255 && surface.pixels[(5 * 13 + 5) * 4 + 3] == 0,
          "rectangles clip at physical bounds");
    const Image icon{1, 1, {255, 0, 0, 128}};
    ui_blit(surface, icon, 9, 9);
    check(surface.pixels[(12 * 13 + 12) * 4 + 3] != 0,
          "bitmap drawing reaches final rounded pixel");
    FontBook fonts;
    HudArt art;
    Catalog catalog(flash::source_root);
    AnimationLibrary animations(flash::source_root);
    World world(catalog);
    world.initialize(8, 8);
    const auto wizard = catalog.object("Wizard")->type,
               staff = catalog.object("Energy Staff")->type;
    world.add(99, wizard, {3, 3});
    world.set_player_id(99);
    world.entity(99)->profile.name = "Resize Test";
    set_inventory_item(*world.entity(99), 0, {staff, -1});
    const auto state = HudState::player(world, *world.entity(99));
    Hud hud(fonts, &art, &catalog);
    ItemTooltip item(catalog, fonts, art);
    PlayerTooltip player(catalog, fonts, art);
    Minimap minimap(catalog, animations, flash::source_root);
    InteractionDialog dialog;
    HudState::Social panel{HudState::SocialKind::choose_name,
                           77,
                           "Choose Account Name",
                           "",
                           "Choose",
                           0,
                           -1,
                           true,
                           false};
    check(dialog.open(panel, *world.entity(99)), "dialog fixture opens");
    ItemTooltipTarget item_target;
    item_target.item = {staff, -1};
    item_target.player_type = wizard;
    item_target.usable = true;
    PlayerTooltipTarget player_target;
    player_target.type = wizard;
    player_target.name = "Resize Test";
    player_target.level = 20;
    player_target.stars = 12;
    player_target.bars = {StatusBarState{"HP", 14693428, 0, 100, 200, 0, 200},
                          StatusBarState{"MP", 6325472, 0, 75, 100, 0, 100}};
    player_target.equipment[0] = {staff, -1};
    player_target.slot_types = {17, 11, 14, 9};
    const auto base = fonts.rasterize("AVATAR 100/100", 14, true);
    const auto centroid = [](const Image& image) {
      double sum{}, x{}, y{};
      for (int row = 0; row < image.height; ++row) {
        for (int col = 0; col < image.width; ++col) {
          const auto alpha =
              image.pixels[(static_cast<std::size_t>(row) * image.width + col) * 4 + 3];
          sum += alpha;
          x += alpha * (col + .5);
          y += alpha * (row + .5);
        }
      }
      return Vec2{x / sum, y / sum};
    };
    const auto origin = fonts.rasterize_at_scale("H", 36, 2, true, false, 0xffffff);
    const auto fractional = fonts.rasterize_at_scale("H", 36, 2, true, false, 0xffffff, {.25, .25});
    const auto first = centroid(origin.image), second = centroid(fractional.image);
    // Pens keep their sub-pixel horizontal phase; baselines snap to whole pixels for sharp hinting.
    check(std::abs(second.x - first.x - .25) < .04 && std::abs(second.y - first.y) < .04,
          "animated glyph coverage follows fractional horizontal motion on whole-pixel baselines");
    check(fonts.pixel_scale() == 1, "animated rasterization leaves retained UI density unchanged");
    const auto base_hud = [&] {
      hud.update(state);
      return hud.image().pixels;
    }();
    for (double density : {1.2, 1.5, 2.0, 3.6, 0.75, 1.0}) {
      fonts.set_pixel_scale(density);
      const auto text = fonts.rasterize("AVATAR 100/100", 14, true);
      check(text.width == base.width && text.height == base.height,
            "logical text metrics survive resize");
      check(text.image.width == static_cast<int>(std::ceil(base.image.width * density)),
            "glyph field uses target pixels");
      if (density == 2) {
        std::size_t differences{};
        for (int y = 0; y < text.image.height; ++y) {
          for (int x = 0; x < text.image.width; ++x) {
            differences +=
                text.image.pixels[(static_cast<std::size_t>(y) * text.image.width + x) * 4 + 3] !=
                base.image
                    .pixels[(static_cast<std::size_t>(y / 2) * base.image.width + x / 2) * 4 + 3];
          }
        }
        check(differences > 100, "larger text is rerasterized, not replicated base pixels");
        const auto tag = tier_bitmap(fonts, *catalog.object(staff));
        check(tag && tag->image.pixel_scale == 2 && tag->offset.x == -2,
              "tiers use target-density glyphs and the tight two-pixel outline padding");
        // The 1 px black outline reaches at most 2 device pixels past the ink, so the outermost
        // device ring of the padded tag stays clear (no glow fills the padding).
        std::size_t ring_alpha{};
        for (int y = 0; y < tag->image.height; ++y) {
          for (int x = 0; x < tag->image.width; ++x) {
            if (x == 0 || y == 0 || x == tag->image.width - 1 || y == tag->image.height - 1) {
              ring_alpha +=
                  tag->image.pixels[(static_cast<std::size_t>(y) * tag->image.width + x) * 4 + 3];
            }
          }
        }
        check(ring_alpha == 0, "tier outline stays inside its padding");
      }
      check(hud.update(state), "same HUD content redraws after density change");
      check(hud.image().height == static_cast<int>(std::ceil(600 * density)),
            "HUD uses drawable height");
      for (const auto& region : hud.changes()) {
        check(region.x + region.width <= hud.image().width &&
                  region.y + region.height <= hud.image().height,
              "upload regions fit resized HUD");
      }
      check(!hud.update(state), "unchanged HUD stays cached");
      check(item.update(item_target) && !item.update(item_target),
            "item tooltip invalidates once per size");
      check(player.update(player_target) && !player.update(player_target),
            "player tooltip invalidates once per size");
      check(dialog.render(fonts) && !dialog.render(fonts), "dialog invalidates once per size");
      minimap.set_pixel_scale(density);
      check(minimap.update(world, *world.entity(99), 0) &&
                !minimap.update(world, *world.entity(99), 0),
            "minimap redraws once per size");
      check(item.image().height == static_cast<int>(std::ceil(item.height() * density)) &&
                player.image().height == static_cast<int>(std::ceil(380 * density)),
            "tooltip textures resize");
      check(minimap.image().width == static_cast<int>(std::ceil(minimap_size * density)),
            "minimap renders target pixels");
      // Exercise stats, hotkeys, native tier labels and the panel button.
      auto varied = state;
      varied.inventory.selected = 1;
      check(hud.update(varied), "stats redraw after resize");
      varied.inventory.selected = 0;
      varied.portal = {77, "Nexus", false, true, true};
      check(hud.update(varied), "portal redraw after resize");
      hud.update(state);
      if (density == 1) {
        check(hud.image().pixels == base_hud,
              "returning to base size restores original HUD rendering");
      }
    }
    std::cout << "Fractional, enlarged, 4K-density, shrinking and restored UI checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
